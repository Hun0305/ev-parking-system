#include "sample_component.h"

#include "file_logger.h"

#include <dispatcher_serialize.h>
#include <i_app_network_manager.h>

#include <cstdint>
#include <exception>
#include <sstream>

namespace {
bool ParseChannelQuery(const std::string& target,
                       int default_channel,
                       int& channel,
                       std::string& error_message) {
  channel = default_channel;
  const std::size_t query_pos = target.find('?');
  if (query_pos == std::string::npos || query_pos + 1 >= target.size()) {
    return true;
  }

  bool channel_seen = false;
  std::size_t begin = query_pos + 1;
  while (begin <= target.size()) {
    const std::size_t end = target.find('&', begin);
    const std::string item = target.substr(
        begin, end == std::string::npos ? std::string::npos : end - begin);
    const std::size_t equals = item.find('=');
    const std::string key = item.substr(0, equals);
    const std::string value =
        equals == std::string::npos ? std::string() : item.substr(equals + 1);
    if (key == "channel") {
      if (channel_seen || value.empty()) {
        error_message = channel_seen ? "channel must be specified once"
                                     : "channel value is required";
        return false;
      }
      channel_seen = true;
      try {
        std::size_t parsed_length = 0;
        channel = std::stoi(value, &parsed_length);
        if (parsed_length != value.length()) {
          error_message = "channel must be an integer";
          return false;
        }
      } catch (const std::exception&) {
        error_message = "channel must be an integer";
        return false;
      }
    }
    if (end == std::string::npos) {
      break;
    }
    begin = end + 1;
  }
  return true;
}
}  // namespace

void SampleComponent::SetJsonResponse(OpenAppSerializable* response,
                                      int status_code,
                                      const std::string& status,
                                      const std::string& message,
                                      int port,
                                      const std::string& stage,
                                      const std::string& error_code) {
  JsonUtility::JsonDocument document(JsonUtility::Type::kObjectType);
  auto& alloc = document.GetAllocator();
  JsonUtility::set(document, "success", status_code >= 200 && status_code < 300, alloc);
  JsonUtility::set(document, "http_status", status_code, alloc);
  JsonUtility::set(document, "status", status, alloc);
  JsonUtility::set(document, "message", message, alloc);
  if (!stage.empty()) {
    JsonUtility::set(document, "stage", stage, alloc);
  }
  if (!error_code.empty()) {
    JsonUtility::set(document, "error_code", error_code, alloc);
  }
  if (port > 0) {
    JsonUtility::set(document, "port", port, alloc);
  }

  rapidjson::StringBuffer strbuf;
  rapidjson::Writer<rapidjson::StringBuffer> writer(strbuf);
  document.Accept(writer);
  response->SetStatusCode(status_code);
  response->SetResponseBody(strbuf.GetString(), strbuf.GetLength());
}

bool SampleComponent::StartServer(OpenAppSerializable* request) {
  JsonUtility::JsonDocument document;
  const rapidjson::ParseResult parse_result = document.Parse(request->GetRequestBody());
  if (!parse_result || !document.IsObject()) {
    FileLogger::Trace("ERROR", "SERVER", "request body parse failed");
    SetJsonResponse(request, 400, "error", "request body parse error", -1,
                    "server", "REQUEST_BODY_PARSE_ERROR");
    return false;
  }
  if (!document.HasMember("port")) {
    FileLogger::Trace("ERROR", "SERVER", "port is missing");
    SetJsonResponse(request, 400, "error", "port is required", -1,
                    "server", "PORT_REQUIRED");
    return false;
  }

  int port = -1;
  const auto& port_value = document["port"];
  try {
    if (port_value.IsString()) {
      std::size_t parsed_length = 0;
      const std::string port_text = port_value.GetString();
      port = std::stoi(port_text, &parsed_length);
      if (parsed_length != port_text.length()) {
        port = -1;
      }
    } else if (port_value.IsInt()) {
      port = port_value.GetInt();
    }
  } catch (const std::exception&) {
    port = -1;
  }

  if (port < 1024 || port > 65535) {
    FileLogger::Trace("ERROR", "SERVER", "invalid port=%d", port);
    SetJsonResponse(request, 400, "error", "port must be between 1024 and 65535", -1,
                    "server", "INVALID_PORT");
    return false;
  }

  if (document.HasMember("app_id") && document["app_id"].IsString() &&
      app_id_.empty()) {
    app_id_ = document["app_id"].GetString();
    FileLogger::Trace("INFO", "APP_INFO", "application ID accepted from startserver request");
  }

  if (server_start_requested_) {
    if (server_port_ == port) {
      FileLogger::Trace("INFO", "SERVER", "start already requested port=%d", port);
      SetJsonResponse(request, 200, "ok", "server start was already requested", port);
      return true;
    }
    FileLogger::Trace("ERROR", "SERVER", "port change rejected current=%d requested=%d",
          server_port_, port);
    SetJsonResponse(request, 409, "error",
                    "server is already configured on another port; restart the app to change it",
                    server_port_, "server", "SERVER_ALREADY_CONFIGURED");
    return false;
  }

  auto* config = new ("NetworkConfig") NetworkConfig(
      static_cast<int>(IAppNetworkManager::EServiceType::eServer),
      static_cast<int>(IAppNetworkManager::ESocketType::eTCP), "", port);
  SendNoReplyEvent("AppNetworkManager",
                   static_cast<int32_t>(IAppNetworkManager::EEventType::eStartService),
                   0, config);
  server_port_ = port;
  server_start_requested_ = true;
  FileLogger::Trace("INFO", "SERVER", "TCP server start requested port=%d", port);
  SetJsonResponse(request, 202, "accepted", "server start requested", port);
  return true;
}

void SampleComponent::HandleNetworkData(Event* event) {
  auto* data = static_cast<NetworkBufferData*>(event->GetBaseObjectArgument());
  if (data == nullptr || data->buffer() == nullptr || data->buffer_size() <= 0) {
    FileLogger::Trace("ERROR", "HTTP", "empty network data received");
    return;
  }

  int client_id = data->client_id();
  if (client_id <= 0) {
    client_id = event->GetArgument();
  }
  if (client_id <= 0) {
    FileLogger::Trace("ERROR", "HTTP", "network data has invalid client_id=%d", client_id);
    return;
  }

  std::string& request = request_buffers_[client_id];
  request.append(data->buffer(), static_cast<std::size_t>(data->buffer_size()));
  FileLogger::Trace("INFO", "HTTP", "data received client_id=%d chunk_bytes=%d buffered_bytes=%zu",
        client_id, data->buffer_size(), request.size());
  if (request.size() > kMaxHttpHeaderSize) {
    FileLogger::Trace("ERROR", "HTTP", "header too large client_id=%d bytes=%zu",
          client_id, request.size());
    request_buffers_.erase(client_id);
    SendErrorResponse(client_id, 431, "Request Header Fields Too Large", "http",
                      "HEADER_TOO_LARGE", "request header is too large");
    return;
  }

  const std::size_t header_end = request.find("\r\n\r\n");
  if (header_end == std::string::npos) {
    return;
  }

  const std::string header = request.substr(0, header_end);
  request_buffers_.erase(client_id);
  const std::size_t first_line_end = header.find("\r\n");
  const std::string request_line = header.substr(0, first_line_end);
  std::istringstream request_line_stream(request_line);
  std::string method;
  std::string target;
  std::string http_version;
  request_line_stream >> method >> target >> http_version;
  if (method.empty() || target.empty() || http_version.rfind("HTTP/", 0) != 0) {
    FileLogger::Trace("ERROR", "HTTP", "invalid request line client_id=%d", client_id);
    SendErrorResponse(client_id, 400, "Bad Request", "http",
                      "BAD_REQUEST_LINE", "invalid HTTP request line");
    return;
  }
  if (method != "GET") {
    FileLogger::Trace("ERROR", "HTTP", "method not allowed client_id=%d method=%s",
          client_id, method.c_str());
    SendErrorResponse(client_id, 405, "Method Not Allowed", "http",
                      "METHOD_NOT_ALLOWED", "GET method is required");
    return;
  }

  const std::size_t query_pos = target.find('?');
  const std::string path = target.substr(0, query_pos);
  if (path == "/environment/result/jpg" ||
      path == "/environment/jpg") {
    HandleEnvironmentImageRequest(client_id, target);
    return;
  }
  if (path == "/images/result/jpg") {
    HandleGeneratedImageRequest(client_id, target);
    return;
  }
  if (path != "/image/jpg") {
    FileLogger::Trace("ERROR", "HTTP", "path not found client_id=%d path=%s",
          client_id, path.c_str());
    SendErrorResponse(client_id, 404, "Not Found", "http",
                      "PATH_NOT_FOUND", "requested path is not supported");
    return;
  }

  const int default_channel = info_list_.app_attribute_info.default_channel;
  int channel = default_channel;
  std::string channel_error;
  if (!ParseChannelQuery(target, default_channel, channel, channel_error) ||
      !ValidateChannel(channel, channel_error)) {
    FileLogger::Trace("ERROR", "HTTP",
                      "invalid channel client_id=%d target=%s error=%s",
                      client_id, target.c_str(), channel_error.c_str());
    SendErrorResponse(client_id, 400, "Bad Request", "snapshot",
                      "INVALID_CHANNEL", channel_error);
    return;
  }

  FileLogger::Trace("INFO", "HTTP",
                    "request client_id=%d method=%s path=%s channel=%d",
                    client_id, method.c_str(), path.c_str(), channel);
  HandleImageRequest(client_id, channel);
}

void SampleComponent::SendHttpResponse(int client_id,
                                       int status_code,
                                       const std::string& reason,
                                       const std::string& content_type,
                                       const std::vector<unsigned char>& body) {
  FileLogger::Trace("INFO", "HTTP", "response client_id=%d status=%d content_type=%s body_bytes=%zu",
        client_id, status_code, content_type.c_str(), body.size());
  std::ostringstream header;
  header << "HTTP/1.1 " << status_code << ' ' << reason << "\r\n"
         << "Content-Length: " << body.size() << "\r\n"
         << "Content-Type: " << content_type << "\r\n"
         << "Cache-Control: no-store\r\n"
         << "Access-Control-Allow-Origin: *\r\n"
         << "Connection: close\r\n\r\n";

  const std::string header_text = header.str();
  std::vector<char> response(header_text.begin(), header_text.end());
  response.insert(response.end(), body.begin(), body.end());
  auto* data = new ("NetworkBufferData") NetworkBufferData(
      response.data(), static_cast<int>(response.size()), client_id);
  SendNoReplyEvent("AppNetworkManager",
                   static_cast<int32_t>(IAppNetworkManager::EEventType::eSendData),
                   0, data);
  CloseClient(client_id);
}

void SampleComponent::SendErrorResponse(int client_id,
                                        int status_code,
                                        const std::string& reason,
                                        const std::string& stage,
                                        const std::string& error_code,
                                        const std::string& message) {
  JsonUtility::JsonDocument document(JsonUtility::Type::kObjectType);
  auto& alloc = document.GetAllocator();
  JsonUtility::set(document, "success", false, alloc);
  JsonUtility::set(document, "http_status", status_code, alloc);
  JsonUtility::set(document, "stage", stage, alloc);
  JsonUtility::set(document, "error_code", error_code, alloc);
  JsonUtility::set(document, "message", message, alloc);

  rapidjson::StringBuffer strbuf;
  rapidjson::Writer<rapidjson::StringBuffer> writer(strbuf);
  document.Accept(writer);
  const std::string json = strbuf.GetString();
  const std::vector<unsigned char> body(json.begin(), json.end());
  SendHttpResponse(client_id, status_code, reason, "application/json; charset=utf-8", body);
}

void SampleComponent::CloseClient(int client_id) {
  FileLogger::Trace("INFO", "CLIENT", "close requested client_id=%d", client_id);
  SendNoReplyEvent("AppNetworkManager",
                   static_cast<int32_t>(IAppNetworkManager::EEventType::eCloseClient),
                   client_id);
}

