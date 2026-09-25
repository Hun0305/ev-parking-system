#include "sample_component.h"

#include "file_logger.h"

#include <dispatcher_serialize.h>

#include <exception>
#include <string>

namespace {
std::string QueryValue(const std::string& query, const std::string& key) {
  std::size_t start = 0;
  while (start <= query.size()) {
    const std::size_t end = query.find('&', start);
    const std::string item = query.substr(
        start, end == std::string::npos ? std::string::npos : end - start);
    const std::size_t separator = item.find('=');
    if (separator != std::string::npos && item.substr(0, separator) == key) {
      return item.substr(separator + 1);
    }
    if (end == std::string::npos) {
      break;
    }
    start = end + 1;
  }
  return std::string();
}

int LogFileIndex(const std::string& file) {
  if (file == "current") {
    return 0;
  }
  if (file == "backup1") {
    return 1;
  }
  if (file == "backup2") {
    return 2;
  }
  if (file == "backup3") {
    return 3;
  }
  return -1;
}

void SetLogReadResponse(OpenAppSerializable* response,
                        const std::string& file,
                        int requested_lines,
                        const std::string& content,
                        std::size_t file_size,
                        int returned_lines,
                        bool truncated) {
  JsonUtility::JsonDocument document(JsonUtility::Type::kObjectType);
  auto& alloc = document.GetAllocator();
  JsonUtility::set(document, "success", true, alloc);
  JsonUtility::set(document, "http_status", 200, alloc);
  JsonUtility::set(document, "file", file, alloc);
  JsonUtility::set(document, "requested_lines", requested_lines, alloc);
  JsonUtility::set(document, "returned_lines", returned_lines, alloc);
  JsonUtility::set(document, "file_size", static_cast<int>(file_size), alloc);
  JsonUtility::set(document, "truncated", truncated, alloc);
  JsonUtility::set(document, "content", content, alloc);

  rapidjson::StringBuffer buffer;
  rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
  document.Accept(writer);
  response->SetStatusCode(200);
  response->SetResponseBody(buffer.GetString(), buffer.GetLength());
}
}  // namespace

bool SampleComponent::GetLogs(OpenAppSerializable* request) {
  const std::string query = request->GetFCGXParam("QUERY_STRING");
  std::string file = QueryValue(query, "file");
  std::string line_text = QueryValue(query, "lines");
  if (file.empty()) {
    file = "current";
  }
  if (line_text.empty()) {
    line_text = "200";
  }

  const int backup_index = LogFileIndex(file);
  if (backup_index < 0) {
    SetJsonResponse(request, 400, "error",
                    "log file must be current or backup1 through backup3",
                    -1, "log", "INVALID_LOG_FILE");
    return false;
  }

  int max_lines = 0;
  try {
    std::size_t parsed_length = 0;
    max_lines = std::stoi(line_text, &parsed_length);
    if (parsed_length != line_text.length()) {
      max_lines = 0;
    }
  } catch (const std::exception&) {
    max_lines = 0;
  }
  if (max_lines < 1 || max_lines > 500) {
    SetJsonResponse(request, 400, "error", "log lines must be between 1 and 500",
                    -1, "log", "INVALID_LOG_LINES");
    return false;
  }

  std::string content;
  std::size_t file_size = 0;
  int returned_lines = 0;
  bool truncated = false;
  std::string error_code;
  std::string error_message;
  if (!FileLogger::ReadTail(backup_index, max_lines, content, file_size,
                            returned_lines, truncated, error_code, error_message)) {
    const int status_code = error_code == "LOG_FILE_NOT_FOUND" ? 404 : 500;
    FileLogger::Trace("ERROR", "LOG", "read failed file=%s code=%s",
                      file.c_str(), error_code.c_str());
    SetJsonResponse(request, status_code, "error", error_message, -1, "log", error_code);
    return false;
  }

  SetLogReadResponse(request, file, max_lines, content, file_size,
                     returned_lines, truncated);
  return true;
}

bool SampleComponent::ClearLogs(OpenAppSerializable* request) {
  JsonUtility::JsonDocument document;
  const rapidjson::ParseResult parse_result = document.Parse(request->GetRequestBody());
  if (!parse_result || !document.IsObject() || !document.HasMember("scope") ||
      !document["scope"].IsString() || !document.HasMember("confirm") ||
      !document["confirm"].IsString()) {
    SetJsonResponse(request, 400, "error", "scope and confirm strings are required",
                    -1, "log", "INVALID_LOG_CLEAR_REQUEST");
    return false;
  }

  const std::string scope = document["scope"].GetString();
  const std::string confirmation = document["confirm"].GetString();
  const bool clear_all = scope == "all";
  const bool valid_scope = scope == "current" || clear_all;
  const bool valid_confirmation =
      (scope == "current" && confirmation == "CLEAR_LOG") ||
      (clear_all && confirmation == "CLEAR_ALL_LOGS");
  if (!valid_scope) {
    SetJsonResponse(request, 400, "error", "scope must be current or all",
                    -1, "log", "INVALID_LOG_CLEAR_REQUEST");
    return false;
  }
  if (!valid_confirmation) {
    SetJsonResponse(request, 400, "error", "log clear confirmation does not match scope",
                    -1, "log", "LOG_CLEAR_CONFIRMATION_REQUIRED");
    return false;
  }

  std::string error_code;
  std::string error_message;
  if (!FileLogger::Clear(clear_all, error_code, error_message)) {
    FileLogger::Trace("ERROR", "LOG", "clear failed scope=%s code=%s",
                      scope.c_str(), error_code.c_str());
    SetJsonResponse(request, 500, "error", error_message, -1, "log", error_code);
    return false;
  }

  FileLogger::Trace("INFO", "LOG", "logs cleared scope=%s", scope.c_str());
  SetJsonResponse(request, 200, "ok", "log cleared", -1, "log");
  return true;
}