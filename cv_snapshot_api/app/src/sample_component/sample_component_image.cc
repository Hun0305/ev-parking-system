#include "sample_component.h"

#include "file_logger.h"

#include <dispatcher_serialize.h>
#include <i_p_open_platform_manager.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <exception>
#include <fstream>
#include <sstream>
#include <stdexcept>

bool SampleComponent::GetChannels(OpenAppSerializable* request) {
  JsonUtility::JsonDocument document(JsonUtility::Type::kObjectType);
  auto& alloc = document.GetAllocator();
  const auto& attributes = info_list_.app_attribute_info;
  JsonUtility::set(document, "success", true, alloc);
  JsonUtility::set(document, "channel_count", attributes.channel_count, alloc);
  JsonUtility::set(document, "default_channel", attributes.default_channel, alloc);

  JsonUtility::ValueType channels(rapidjson::kArrayType);
  for (int channel = 0; channel < attributes.channel_count; ++channel) {
    JsonUtility::ValueType item(rapidjson::kObjectType);
    JsonUtility::set(item, "id", channel, alloc);
    JsonUtility::set(item, "label", "CH " + std::to_string(channel + 1), alloc);
    channels.PushBack(item, alloc);
  }
  document.AddMember(JsonUtility::ValueType("channels", alloc), channels, alloc);

  rapidjson::StringBuffer strbuf;
  rapidjson::Writer<rapidjson::StringBuffer> writer(strbuf);
  document.Accept(writer);
  request->SetStatusCode(200);
  request->SetResponseBody(strbuf.GetString(), strbuf.GetLength());
  return true;
}

bool SampleComponent::ParseChannelRequest(OpenAppSerializable* request,
                                          int& channel,
                                          std::string& error_code,
                                          std::string& error_message) const {
  channel = info_list_.app_attribute_info.default_channel;
  error_code.clear();
  error_message.clear();

  const std::string body = request->GetRequestBody();
  if (body.empty()) {
    if (!ValidateChannel(channel, error_message)) {
      error_code = "INVALID_CHANNEL_CONFIGURATION";
      return false;
    }
    return true;
  }

  JsonUtility::JsonDocument document;
  const rapidjson::ParseResult parse_result = document.Parse(body.c_str());
  if (!parse_result || !document.IsObject()) {
    error_code = "REQUEST_BODY_PARSE_ERROR";
    error_message = "request body must be a JSON object";
    return false;
  }
  if (!document.HasMember("channel")) {
    if (!ValidateChannel(channel, error_message)) {
      error_code = "INVALID_CHANNEL_CONFIGURATION";
      return false;
    }
    return true;
  }

  const auto& value = document["channel"];
  try {
    if (value.IsInt()) {
      channel = value.GetInt();
    } else if (value.IsString()) {
      const std::string text = value.GetString();
      std::size_t parsed_length = 0;
      channel = std::stoi(text, &parsed_length);
      if (parsed_length != text.length()) {
        throw std::invalid_argument("channel contains non-numeric characters");
      }
    } else {
      error_code = "INVALID_CHANNEL";
      error_message = "channel must be an integer";
      return false;
    }
  } catch (const std::exception&) {
    error_code = "INVALID_CHANNEL";
    error_message = "channel must be an integer";
    return false;
  }

  if (!ValidateChannel(channel, error_message)) {
    error_code = "INVALID_CHANNEL";
    return false;
  }
  return true;
}

bool SampleComponent::ValidateChannel(int channel, std::string& error_message) const {
  const int channel_count = info_list_.app_attribute_info.channel_count;
  if (channel_count <= 0) {
    error_message = "channel_count must be greater than zero";
    return false;
  }
  if (channel < 0 || channel >= channel_count) {
    error_message = "channel must be between 0 and " +
                    std::to_string(channel_count - 1);
    return false;
  }
  return true;
}

std::string SampleComponent::ChannelPath(const std::string& base_path,
                                         int channel) const {
  const std::string suffix = "_ch" + std::to_string(channel);
  const std::size_t extension = base_path.find_last_of('.');
  if (extension == std::string::npos) {
    return base_path + suffix;
  }
  return base_path.substr(0, extension) + suffix + base_path.substr(extension);
}

std::string SampleComponent::CapturePathForChannel(int channel) const {
  return ChannelPath(info_list_.app_attribute_info.jpeg_path, channel);
}

std::string SampleComponent::ProcessedPathForChannel(int channel) const {
  return ChannelPath(info_list_.app_attribute_info.processed_jpeg_path, channel);
}

bool SampleComponent::CaptureFrame(OpenAppSerializable* request) {
  int channel = 0;
  std::string error_code;
  std::string error_message;
  if (!ParseChannelRequest(request, channel, error_code, error_message)) {
    SetJsonResponse(request, 400, "error", error_message, -1,
                    "snapshot", error_code);
    return false;
  }

  const std::string path = CapturePathForChannel(channel);
  FileLogger::Trace("INFO", "SNAPSHOT",
                    "diagnostic capture API requested channel=%d", channel);
  if (!CaptureCurrentFrame(channel, path, error_message)) {
    FileLogger::Trace("ERROR", "SNAPSHOT",
                      "diagnostic capture failed channel=%d error=%s",
                      channel, error_message.c_str());
    SetJsonResponse(request, 502, "error", error_message, -1,
                    "snapshot", "SNAPSHOT_FAILED");
    return false;
  }

  std::ifstream input(path, std::ios::binary);
  if (!input.is_open()) {
    FileLogger::Trace("ERROR", "SNAPSHOT",
                      "capture reply succeeded but file is missing channel=%d path=%s",
                      channel, path.c_str());
    SetJsonResponse(request, 500, "error",
                    "capture reply succeeded but output file was not found", -1,
                    "snapshot", "CAPTURE_FILE_NOT_FOUND");
    return false;
  }

  std::ostringstream buffer;
  buffer << input.rdbuf();
  const std::string body = buffer.str();
  if (body.empty()) {
    SetJsonResponse(request, 500, "error", "captured JPEG file is empty", -1,
                    "snapshot", "CAPTURE_FILE_EMPTY");
    return false;
  }

  FileLogger::Trace("INFO", "SNAPSHOT",
                    "diagnostic capture completed channel=%d path=%s bytes=%zu",
                    channel, path.c_str(), body.size());
  request->SetStatusCode(200);
  request->SetResponseBody(body, OpenAppResponseType::FILE);
  return true;
}

bool SampleComponent::TryBeginProcessing(int& retry_after_ms,
                                         std::string& rejection_code) {
  retry_after_ms = 0;
  rejection_code.clear();
  if (processing_frame_) {
    rejection_code = "PROCESSING_BUSY";
    return false;
  }

  const auto now = std::chrono::steady_clock::now();
  if (now < next_processing_allowed_at_) {
    retry_after_ms = static_cast<int>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            next_processing_allowed_at_ - now)
            .count());
    retry_after_ms = std::max(1, retry_after_ms);
    rejection_code = "PROCESSING_RATE_LIMITED";
    return false;
  }

  const int interval_ms = std::clamp(
      info_list_.app_attribute_info.processing_min_interval_ms, 0, 60000);
  processing_frame_ = true;
  next_processing_allowed_at_ =
      now + std::chrono::milliseconds(interval_ms);
  return true;
}

void SampleComponent::FinishProcessing() {
  processing_frame_ = false;
}

bool SampleComponent::GetCachedProcessedJpeg(
    int channel,
    std::vector<unsigned char>& jpeg,
    long long& age_ms) const {
  jpeg.clear();
  age_ms = -1;
  const auto found = processed_jpeg_cache_.find(channel);
  if (found == processed_jpeg_cache_.end() || found->second.jpeg.empty()) {
    return false;
  }
  jpeg = found->second.jpeg;
  age_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now() - found->second.completed_at)
               .count();
  return true;
}

void SampleComponent::CacheProcessedJpeg(
    int channel,
    const std::vector<unsigned char>& jpeg) {
  if (jpeg.empty()) {
    return;
  }
  processed_jpeg_cache_[channel] =
      CachedProcessedJpeg{jpeg, std::chrono::steady_clock::now()};
}

bool SampleComponent::ProcessCapturedFrame(OpenAppSerializable* request) {
  int channel = 0;
  std::string error_code;
  std::string error_message;
  if (!ParseChannelRequest(request, channel, error_code, error_message)) {
    SetJsonResponse(request, 400, "error", error_message, -1,
                    "opencv", error_code);
    return false;
  }

  const std::string path = CapturePathForChannel(channel);
  FileLogger::Trace("INFO", "OPENCV",
                    "diagnostic process API requested channel=%d path=%s",
                    channel, path.c_str());
  std::ifstream input(path, std::ios::binary);
  if (!input.is_open()) {
    SetJsonResponse(request, 404, "error", "captured JPEG file was not found", -1,
                    "opencv", "CAPTURE_FILE_NOT_FOUND");
    return false;
  }
  input.close();

  int retry_after_ms = 0;
  if (!TryBeginProcessing(retry_after_ms, error_code)) {
    const int status = error_code == "PROCESSING_RATE_LIMITED" ? 429 : 503;
    SetJsonResponse(request, status, "error",
                    error_code == "PROCESSING_RATE_LIMITED"
                        ? "processing is rate limited; retry after " +
                              std::to_string(retry_after_ms) + " ms"
                        : "frame processing is busy",
                    -1, "opencv", error_code);
    return false;
  }

  const ProcessingOptions options = GetProcessingOptions();
  std::vector<unsigned char> jpeg;
  if (!frame_processor_.ProcessJpegFile(path, options, jpeg,
                                        error_code, error_message)) {
    FinishProcessing();
    FileLogger::Trace("ERROR", "OPENCV",
                      "diagnostic processing failed channel=%d code=%s error=%s",
                      channel, error_code.c_str(), error_message.c_str());
    SetJsonResponse(request, 500, "error", error_message, -1,
                    "opencv", error_code.empty() ? "OPENCV_FAILED" : error_code);
    return false;
  }
  if (!SaveProcessedJpeg(channel, jpeg, error_message)) {
    FinishProcessing();
    SetJsonResponse(request, 500, "error", error_message, -1,
                    "opencv", "PROCESSED_FILE_WRITE_FAILED");
    return false;
  }

  CacheProcessedJpeg(channel, jpeg);
  FinishProcessing();

  FileLogger::Trace("INFO", "OPENCV",
                    "diagnostic processing completed channel=%d path=%s jpeg_bytes=%zu",
                    channel, ProcessedPathForChannel(channel).c_str(), jpeg.size());
  const std::string body(reinterpret_cast<const char*>(jpeg.data()), jpeg.size());
  request->SetStatusCode(200);
  request->SetResponseBody(body, OpenAppResponseType::FILE);
  return true;
}

void SampleComponent::HandleImageRequest(int client_id, int channel) {
  FileLogger::Trace("INFO", "IMAGE",
                    "request started client_id=%d channel=%d", client_id, channel);
  if (app_id_.empty()) {
    FileLogger::Trace("ERROR", "IMAGE",
                      "application ID is not ready client_id=%d channel=%d",
                      client_id, channel);
    SendErrorResponse(client_id, 503, "Service Unavailable", "app_info",
                      "APP_ID_NOT_READY", "application ID is not ready");
    return;
  }

  std::string error_message;
  if (!ValidateChannel(channel, error_message)) {
    SendErrorResponse(client_id, 400, "Bad Request", "snapshot",
                      "INVALID_CHANNEL", error_message);
    return;
  }

  std::vector<unsigned char> cached_jpeg;
  long long cache_age_ms = -1;
  const bool has_cached_jpeg =
      GetCachedProcessedJpeg(channel, cached_jpeg, cache_age_ms);
  const int cache_ttl_ms = std::clamp(
      info_list_.app_attribute_info.processed_jpeg_cache_ttl_ms, 0, 60000);
  if (has_cached_jpeg && cache_ttl_ms > 0 && cache_age_ms <= cache_ttl_ms) {
    FileLogger::Trace("INFO", "IMAGE",
                      "cache hit client_id=%d channel=%d age_ms=%lld ttl_ms=%d bytes=%zu",
                      client_id, channel, cache_age_ms, cache_ttl_ms,
                      cached_jpeg.size());
    SendHttpResponse(client_id, 200, "OK", "image/jpeg", cached_jpeg);
    return;
  }

  int retry_after_ms = 0;
  std::string admission_code;
  if (!TryBeginProcessing(retry_after_ms, admission_code)) {
    if (has_cached_jpeg) {
      FileLogger::Trace("INFO", "IMAGE",
                        "stale cache returned client_id=%d channel=%d age_ms=%lld reason=%s bytes=%zu",
                        client_id, channel, cache_age_ms, admission_code.c_str(),
                        cached_jpeg.size());
      SendHttpResponse(client_id, 200, "OK", "image/jpeg", cached_jpeg);
      return;
    }
    const int status = admission_code == "PROCESSING_RATE_LIMITED" ? 429 : 503;
    const std::string message = admission_code == "PROCESSING_RATE_LIMITED"
        ? "processing is rate limited; retry after " +
              std::to_string(retry_after_ms) + " ms"
        : "frame processing is busy";
    FileLogger::Trace("ERROR", "IMAGE",
                      "processing rejected client_id=%d channel=%d code=%s retry_after_ms=%d",
                      client_id, channel, admission_code.c_str(), retry_after_ms);
    SendErrorResponse(client_id, status,
                      status == 429 ? "Too Many Requests" : "Service Unavailable",
                      "image", admission_code, message);
    return;
  }

  std::string error_code;
  const std::string capture_path = CapturePathForChannel(channel);
  if (!CaptureCurrentFrame(channel, capture_path, error_message)) {
    FinishProcessing();
    FileLogger::Trace("ERROR", "SNAPSHOT",
                      "capture failed client_id=%d channel=%d error=%s",
                      client_id, channel, error_message.c_str());
    SendErrorResponse(client_id, 502, "Bad Gateway", "snapshot",
                      "SNAPSHOT_FAILED", error_message);
    return;
  }

  const ProcessingOptions options = GetProcessingOptions();
  FileLogger::Trace("INFO", "OPENCV",
                    "processing started client_id=%d channel=%d mode=%s blur=%d canny=%d:%d quality=%d",
                    client_id, channel, options.mode.c_str(), options.blur_kernel_size,
                    options.canny_low_threshold, options.canny_high_threshold,
                    options.jpeg_quality);
  std::vector<unsigned char> jpeg;
  if (!frame_processor_.ProcessJpegFile(capture_path, options, jpeg,
                                        error_code, error_message)) {
    FinishProcessing();
    FileLogger::Trace("ERROR", "OPENCV",
                      "processing failed client_id=%d channel=%d code=%s error=%s",
                      client_id, channel, error_code.c_str(), error_message.c_str());
    SendErrorResponse(client_id, 500, "Internal Server Error", "opencv",
                      error_code.empty() ? "OPENCV_FAILED" : error_code,
                      error_message);
    return;
  }

  if (!SaveProcessedJpeg(channel, jpeg, error_message)) {
    FinishProcessing();
    FileLogger::Trace("ERROR", "OPENCV",
                      "processed file save failed client_id=%d channel=%d error=%s",
                      client_id, channel, error_message.c_str());
    SendErrorResponse(client_id, 500, "Internal Server Error", "opencv",
                      "PROCESSED_FILE_WRITE_FAILED", error_message);
    return;
  }

  CacheProcessedJpeg(channel, jpeg);
  FinishProcessing();
  FileLogger::Trace("INFO", "OPENCV",
                    "processing completed client_id=%d channel=%d path=%s jpeg_bytes=%zu",
                    client_id, channel, ProcessedPathForChannel(channel).c_str(),
                    jpeg.size());
  SendHttpResponse(client_id, 200, "OK", "image/jpeg", jpeg);
}

bool SampleComponent::CaptureCurrentFrame(int channel,
                                          const std::string& capture_path,
                                          std::string& error_message) {
  FileLogger::Trace("INFO", "SNAPSHOT",
                    "capture requested channel=%d path=%s",
                    channel, capture_path.c_str());
  JsonUtility::JsonDocument document(JsonUtility::Type::kObjectType);
  auto& alloc = document.GetAllocator();
  JsonUtility::set(document, "jpeg_path", capture_path, alloc);
  JsonUtility::set(document, "channel", std::to_string(channel), alloc);
  JsonUtility::set(document, "app_name", app_id_, alloc);

  rapidjson::StringBuffer strbuf;
  rapidjson::Writer<rapidjson::StringBuffer> writer(strbuf);
  document.Accept(writer);
  auto response = SendReplyEventWait(
      static_cast<uint64_t>(Component::EReceivers::eOpenPlatformManager),
      static_cast<int32_t>(IPOpenPlatformManager::EAppEventType::eAppSnapshotJpeg),
      0, new ("Query") SerializableString(strbuf.GetString()));
  if (!response) {
    error_message = "camera snapshot JPEG request failed";
    return false;
  }
  FileLogger::Trace("INFO", "SNAPSHOT",
                    "capture completed channel=%d path=%s",
                    channel, capture_path.c_str());
  return true;
}

bool SampleComponent::SaveProcessedJpeg(int channel,
                                        const std::vector<unsigned char>& jpeg,
                                        std::string& error_message) const {
  const std::string path = ProcessedPathForChannel(channel);
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  if (!output.is_open()) {
    error_message = "processed JPEG output file could not be opened";
    FileLogger::Trace("ERROR", "OPENCV",
                      "processed file open failed channel=%d path=%s",
                      channel, path.c_str());
    return false;
  }
  output.write(reinterpret_cast<const char*>(jpeg.data()),
               static_cast<std::streamsize>(jpeg.size()));
  if (!output.good()) {
    error_message = "processed JPEG output file write failed";
    FileLogger::Trace("ERROR", "OPENCV",
                      "processed file write failed channel=%d path=%s",
                      channel, path.c_str());
    return false;
  }
  FileLogger::Trace("INFO", "OPENCV",
                    "processed file saved channel=%d path=%s bytes=%zu",
                    channel, path.c_str(), jpeg.size());
  return true;
}

ProcessingOptions SampleComponent::GetProcessingOptions() const {
  const auto& attributes = info_list_.app_attribute_info;
  return ProcessingOptions{attributes.processing_mode,
                           attributes.blur_kernel_size,
                           attributes.canny_low_threshold,
                           attributes.canny_high_threshold,
                           attributes.jpeg_quality};
}
