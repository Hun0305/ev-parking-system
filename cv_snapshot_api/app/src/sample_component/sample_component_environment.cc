#include "sample_component.h"

#include "file_logger.h"

#include <dispatcher_serialize.h>

#include <utility>

bool SampleComponent::GetEnvironmentCatalog(OpenAppSerializable* request) {
  JsonUtility::JsonDocument document(JsonUtility::Type::kObjectType);
  auto& alloc = document.GetAllocator();
  JsonUtility::set(document, "success", true, alloc);
  JsonUtility::set(document, "source", "cv_logic_python_v1_reduced_catalog", alloc);
  JsonUtility::set(document, "auto_selector_validation_status",
                   "pending_after_catalog_change", alloc);
  JsonUtility::set(document, "explicit_hint_recommended", true, alloc);

  JsonUtility::ValueType environments(rapidjson::kArrayType);
  for (const auto& descriptor : EnvironmentProcessor::Catalog()) {
    JsonUtility::ValueType item(rapidjson::kObjectType);
    JsonUtility::set(item, "environment", descriptor.id, alloc);
    JsonUtility::set(item, "selected_variant",
                     descriptor.selected_variant, alloc);
    JsonUtility::set(item, "validation_status",
                     descriptor.validation_status, alloc);
    JsonUtility::set(item, "applies_processing",
                     descriptor.applies_processing, alloc);
    environments.PushBack(item, alloc);
  }
  document.AddMember(JsonUtility::ValueType("environments", alloc),
                     environments, alloc);

  rapidjson::StringBuffer buffer;
  rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
  document.Accept(writer);
  request->SetStatusCode(200);
  request->SetResponseBody(buffer.GetString(), buffer.GetLength());
  return true;
}

bool SampleComponent::RunEnvironmentTest(OpenAppSerializable* request) {
  int channel = 0;
  std::string error_code;
  std::string error_message;
  if (!ParseChannelRequest(request, channel, error_code, error_message)) {
    SetJsonResponse(request, 400, "error", error_message, -1,
                    "environment", error_code);
    return false;
  }

  std::string test_mode = "auto";
  std::string explicit_environment;
  const std::string body = request->GetRequestBody();
  if (!body.empty()) {
    JsonUtility::JsonDocument input;
    const rapidjson::ParseResult parsed = input.Parse(body.c_str());
    if (!parsed || !input.IsObject()) {
      SetJsonResponse(request, 400, "error",
                      "request body must be a JSON object", -1,
                      "environment", "REQUEST_BODY_PARSE_ERROR");
      return false;
    }
    const char* mode_key = input.HasMember("mode")
                               ? "mode"
                               : input.HasMember("test_mode")
                                     ? "test_mode"
                                     : nullptr;
    if (mode_key != nullptr) {
      if (!input[mode_key].IsString()) {
        SetJsonResponse(request, 400, "error",
                        "mode must be a string", -1,
                        "environment", "INVALID_TEST_MODE");
        return false;
      }
      test_mode = input[mode_key].GetString();
      if (test_mode == "compare") {
        test_mode = "all_selected";
      }
    }
    if (input.HasMember("environment")) {
      if (!input["environment"].IsString()) {
        SetJsonResponse(request, 400, "error",
                        "environment must be a string", -1,
                        "environment", "INVALID_ENVIRONMENT");
        return false;
      }
      explicit_environment = input["environment"].GetString();
    }
  }

  int retry_after_ms = 0;
  std::string admission_code;
  if (!TryBeginProcessing(retry_after_ms, admission_code)) {
    const int status = admission_code == "PROCESSING_RATE_LIMITED" ? 429 : 503;
    SetJsonResponse(request, status, "error",
                    admission_code == "PROCESSING_RATE_LIMITED"
                        ? "environment processing is rate limited; retry after " +
                              std::to_string(retry_after_ms) + " ms"
                        : "frame processing is busy",
                    -1, "environment", admission_code);
    return false;
  }

  const std::string capture_path = CapturePathForChannel(channel);
  FileLogger::Trace("INFO", "ENVIRONMENT",
                    "test started channel=%d mode=%s",
                    channel, test_mode.c_str());
  if (!CaptureCurrentFrame(channel, capture_path, error_message)) {
    FinishProcessing();
    FileLogger::Trace("ERROR", "ENVIRONMENT",
                      "test capture failed channel=%d error=%s",
                      channel, error_message.c_str());
    SetJsonResponse(request, 502, "error", error_message, -1,
                    "snapshot", "SNAPSHOT_FAILED");
    return false;
  }

  EnvironmentTestResult result;
  const int jpeg_quality = GetProcessingOptions().jpeg_quality;
  if (!environment_processor_.ProcessJpegFile(
          capture_path, test_mode, explicit_environment, jpeg_quality,
          result, error_code, error_message)) {
    FinishProcessing();
    FileLogger::Trace("ERROR", "ENVIRONMENT",
                      "test processing failed channel=%d code=%s error=%s",
                      channel, error_code.c_str(), error_message.c_str());
    const int status =
        error_code == "INVALID_TEST_MODE" ||
        error_code == "INVALID_ENVIRONMENT" ? 400 : 500;
    SetJsonResponse(request, status, "error", error_message, -1,
                    "environment", error_code);
    return false;
  }

  FileLogger::Trace(
      "INFO", "ENVIRONMENT",
      "analysis channel=%d environment=%s mean=%.3f shadow=%.3f p95=%.3f dynamic_range=%.3f edge_density=%.6f",
      channel, EnvironmentProcessor::EnvironmentName(result.analysis.environment),
      result.analysis.mean_luminance, result.analysis.shadow_percent,
      result.analysis.p95_luminance, result.analysis.dynamic_range,
      result.analysis.edge_density);

  const std::string run_id =
      "env-" + std::to_string(++environment_run_counter_);
  environment_result_jpegs_.clear();
  environment_run_id_ = run_id;

  JsonUtility::JsonDocument document(JsonUtility::Type::kObjectType);
  auto& alloc = document.GetAllocator();
  JsonUtility::set(document, "success", true, alloc);
  JsonUtility::set(document, "source", "cv_logic_python_v1_reduced_catalog", alloc);
  JsonUtility::set(document, "run_id", run_id, alloc);
  JsonUtility::set(document, "channel", channel, alloc);
  const std::string response_mode =
      result.test_mode == "all_selected" ? "compare" : result.test_mode;
  JsonUtility::set(document, "mode", response_mode, alloc);
  JsonUtility::set(document, "test_mode", result.test_mode, alloc);
  JsonUtility::set(
      document, "detected_environment",
      EnvironmentProcessor::EnvironmentName(result.analysis.environment),
      alloc);
  JsonUtility::set(document, "selected_variant",
                   EnvironmentProcessor::SelectedVariantName(
                       result.analysis.environment),
                   alloc);
  JsonUtility::set(document, "used_explicit_hint",
                   result.analysis.used_explicit_hint, alloc);
  JsonUtility::set(document, "auto_selector_validation_status",
                   "pending_after_catalog_change", alloc);
  if (server_port_ > 0) {
    JsonUtility::set(document, "image_server_port", server_port_, alloc);
  }

  JsonUtility::ValueType analysis(rapidjson::kObjectType);
  JsonUtility::set(analysis, "mean_luminance",
                   result.analysis.mean_luminance, alloc);
  JsonUtility::set(analysis, "contrast_stddev",
                   result.analysis.contrast_stddev, alloc);
  JsonUtility::set(analysis, "shadow_percent",
                   result.analysis.shadow_percent, alloc);
  JsonUtility::set(analysis, "highlight_percent",
                   result.analysis.highlight_percent, alloc);
  JsonUtility::set(analysis, "mean_saturation",
                   result.analysis.mean_saturation, alloc);
  JsonUtility::set(analysis, "laplacian_variance",
                   result.analysis.laplacian_variance, alloc);
  JsonUtility::set(analysis, "p95_luminance",
                   result.analysis.p95_luminance, alloc);
  JsonUtility::set(analysis, "dynamic_range",
                   result.analysis.dynamic_range, alloc);
  JsonUtility::set(analysis, "edge_density",
                   result.analysis.edge_density, alloc);
  JsonUtility::set(analysis, "noise_mad",
                   result.analysis.noise_mad, alloc);
  JsonUtility::set(analysis, "jpeg_blockiness",
                   result.analysis.jpeg_blockiness, alloc);
  document.AddMember(JsonUtility::ValueType("analysis", alloc),
                     analysis, alloc);

  JsonUtility::ValueType images(rapidjson::kArrayType);
  for (auto& image : result.images) {
    const std::size_t jpeg_bytes = image.jpeg.size();
    environment_result_jpegs_[image.result_id] = std::move(image.jpeg);

    JsonUtility::ValueType item(rapidjson::kObjectType);
    JsonUtility::set(item, "result_id", image.result_id, alloc);
    JsonUtility::set(
        item, "environment",
        EnvironmentProcessor::EnvironmentName(image.environment), alloc);
    JsonUtility::set(item, "expected_variant",
                     image.expected_variant, alloc);
    JsonUtility::set(item, "executed_variant",
                     image.executed_variant, alloc);
    JsonUtility::set(item, "applied", image.applied, alloc);
    JsonUtility::set(item, "mapping_match",
                     image.mapping_match, alloc);
    JsonUtility::set(item, "processing_ms",
                     image.processing_ms, alloc);
    JsonUtility::set(item, "jpeg_bytes",
                     static_cast<int>(jpeg_bytes), alloc);
    const std::string image_path =
        "/environment/result/jpg?run_id=" + run_id +
        "&result=" + image.result_id;
    JsonUtility::set(item, "image_path", image_path, alloc);
    images.PushBack(item, alloc);

    FileLogger::Trace(
        "INFO", "ENVIRONMENT",
        "result run_id=%s channel=%d environment=%s expected=%s executed=%s applied=%d mapping_match=%d processing_ms=%.3f jpeg_bytes=%zu",
        run_id.c_str(), channel,
        EnvironmentProcessor::EnvironmentName(image.environment),
        image.expected_variant.c_str(), image.executed_variant.c_str(),
        image.applied ? 1 : 0, image.mapping_match ? 1 : 0,
        image.processing_ms, jpeg_bytes);
  }
  document.AddMember(JsonUtility::ValueType("results", alloc),
                     images, alloc);

  FinishProcessing();
  FileLogger::Trace(
      "INFO", "ENVIRONMENT",
      "analysis completed run_id=%s channel=%d environment=%s selected=%s explicit=%d",
      run_id.c_str(), channel,
      EnvironmentProcessor::EnvironmentName(result.analysis.environment),
      EnvironmentProcessor::SelectedVariantName(
          result.analysis.environment),
      result.analysis.used_explicit_hint ? 1 : 0);

  rapidjson::StringBuffer buffer;
  rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
  document.Accept(writer);
  request->SetStatusCode(200);
  request->SetResponseBody(buffer.GetString(), buffer.GetLength());
  return true;
}

void SampleComponent::HandleEnvironmentImageRequest(
    int client_id,
    const std::string& target) {
  std::string run_id;
  std::string result_id;
  const std::size_t query = target.find('?');
  if (query != std::string::npos) {
    std::size_t begin = query + 1;
    while (begin <= target.size()) {
      const std::size_t end = target.find('&', begin);
      const std::string item = target.substr(
          begin,
          end == std::string::npos ? std::string::npos : end - begin);
      const std::size_t equals = item.find('=');
      if (equals != std::string::npos) {
        const std::string key = item.substr(0, equals);
        const std::string value = item.substr(equals + 1);
        if (key == "run_id") {
          run_id = value;
        } else if (key == "result") {
          result_id = value;
        }
      }
      if (end == std::string::npos) {
        break;
      }
      begin = end + 1;
    }
  }

  if (run_id.empty() || result_id.empty()) {
    SendErrorResponse(client_id, 400, "Bad Request", "environment",
                      "ENVIRONMENT_RESULT_QUERY_REQUIRED",
                      "run_id and result are required");
    return;
  }
  if (run_id != environment_run_id_) {
    SendErrorResponse(client_id, 404, "Not Found", "environment",
                      "ENVIRONMENT_RUN_NOT_FOUND",
                      "environment analysis run was not found or expired");
    return;
  }
  const auto found = environment_result_jpegs_.find(result_id);
  if (found == environment_result_jpegs_.end()) {
    SendErrorResponse(client_id, 404, "Not Found", "environment",
                      "ENVIRONMENT_RESULT_NOT_FOUND",
                      "environment analysis image was not found");
    return;
  }

  FileLogger::Trace(
      "INFO", "ENVIRONMENT",
      "image response run_id=%s result=%s jpeg_bytes=%zu",
      run_id.c_str(), result_id.c_str(), found->second.size());
  SendHttpResponse(client_id, 200, "OK", "image/jpeg", found->second);
}
