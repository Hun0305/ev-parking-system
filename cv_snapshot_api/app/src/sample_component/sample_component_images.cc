#include "sample_component.h"

#include "file_logger.h"

#include <dispatcher_serialize.h>

#include <cctype>
#include <chrono>
#include <cstdint>
#include <limits>
#include <unordered_set>
#include <utility>

namespace {
bool IsSafeResultId(const std::string& value) {
  if (value.empty() || value.size() > 64) {
    return false;
  }
  for (const unsigned char character : value) {
    if (!std::isalnum(character) && character != '_' && character != '-') {
      return false;
    }
  }
  return true;
}

bool ReadRoiInt(const rapidjson::Value& value,
                const char* name,
                int& output) {
  if (!value.HasMember(name) || !value[name].IsInt()) {
    return false;
  }
  output = value[name].GetInt();
  return true;
}

bool ParseRoi(const rapidjson::Value& input,
              ImageRoi& roi,
              std::string& error_message) {
  roi = ImageRoi{};
  if (!input.HasMember("roi")) {
    return true;
  }
  const auto& value = input["roi"];
  if (!value.IsObject()) {
    error_message = "roi must be an object";
    return false;
  }

  const bool has_xywh = value.HasMember("x") || value.HasMember("y") ||
                        value.HasMember("width") || value.HasMember("height");
  const bool has_corners = value.HasMember("x1") || value.HasMember("y1") ||
                           value.HasMember("x2") || value.HasMember("y2");
  if (has_xywh == has_corners) {
    error_message = "roi must contain either x, y, width, height or x1, y1, x2, y2";
    return false;
  }

  if (has_xywh) {
    if (!ReadRoiInt(value, "x", roi.x) || !ReadRoiInt(value, "y", roi.y) ||
        !ReadRoiInt(value, "width", roi.width) ||
        !ReadRoiInt(value, "height", roi.height)) {
      error_message = "roi x, y, width, and height must be integers";
      return false;
    }
  } else {
    int x2 = 0;
    int y2 = 0;
    if (!ReadRoiInt(value, "x1", roi.x) || !ReadRoiInt(value, "y1", roi.y) ||
        !ReadRoiInt(value, "x2", x2) || !ReadRoiInt(value, "y2", y2)) {
      error_message = "roi x1, y1, x2, and y2 must be integers";
      return false;
    }
    const long long width = static_cast<long long>(x2) - roi.x;
    const long long height = static_cast<long long>(y2) - roi.y;
    if (width <= 0 || height <= 0 ||
        width > std::numeric_limits<int>::max() ||
        height > std::numeric_limits<int>::max()) {
      error_message = "roi x2 must be greater than x1 and y2 must be greater than y1";
      return false;
    }
    roi.width = static_cast<int>(width);
    roi.height = static_cast<int>(height);
  }
  roi.applied = true;
  return true;
}

const char* LightingAssistRecommendation(CaptureEnvironment environment) {
  switch (environment) {
    case CaptureEnvironment::kBlackout:
    case CaptureEnvironment::kExtremeLowLight:
      return "recommended";
    case CaptureEnvironment::kIrNight:
    case CaptureEnvironment::kIrReflection:
      return "unknown";
    default:
      return "not_recommended";
  }
}
}  // namespace

bool SampleComponent::GetFilters(OpenAppSerializable* request) {
  JsonUtility::JsonDocument document(JsonUtility::Type::kObjectType);
  auto& alloc = document.GetAllocator();
  JsonUtility::set(document, "success", true, alloc);

  JsonUtility::ValueType filters(rapidjson::kArrayType);
  for (const auto& descriptor : EnvironmentProcessor::Catalog()) {
    if (!descriptor.applies_processing) {
      continue;
    }
    JsonUtility::ValueType item(rapidjson::kObjectType);
    JsonUtility::set(item, "id", descriptor.selected_variant, alloc);
    JsonUtility::set(item, "environment", descriptor.id, alloc);
    JsonUtility::set(item, "validation_status",
                     descriptor.validation_status, alloc);
    filters.PushBack(item, alloc);
  }
  document.AddMember(JsonUtility::ValueType("filters", alloc), filters, alloc);

  rapidjson::StringBuffer buffer;
  rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
  document.Accept(writer);
  request->SetStatusCode(200);
  request->SetResponseBody(buffer.GetString(), buffer.GetLength());
  return true;
}

bool SampleComponent::GenerateImages(OpenAppSerializable* request) {
  int channel = 0;
  std::string error_code;
  std::string error_message;
  if (!ParseChannelRequest(request, channel, error_code, error_message)) {
    SetJsonResponse(request, 400, "error", error_message, -1,
                    "image_generation", error_code);
    return false;
  }
  if (!server_start_requested_) {
    SetJsonResponse(request, 409, "error",
                    "image server must be started before generating images", -1,
                    "image_generation", "IMAGE_SERVER_NOT_STARTED");
    return false;
  }

  std::vector<ImageOutputRequest> outputs;
  ImageRoi roi;
  std::string request_id;
  const std::string body = request->GetRequestBody();
  if (!body.empty()) {
    JsonUtility::JsonDocument input;
    const rapidjson::ParseResult parsed = input.Parse(body.c_str());
    if (!parsed || !input.IsObject()) {
      SetJsonResponse(request, 400, "error",
                      "request body must be a JSON object", -1,
                      "image_generation", "REQUEST_BODY_PARSE_ERROR");
      return false;
    }
    if (!ParseRoi(input, roi, error_message)) {
      SetJsonResponse(request, 400, "error", error_message, -1,
                      "image_generation", "INVALID_ROI");
      return false;
    }
    if (input.HasMember("request_id")) {
      if (!input["request_id"].IsString()) {
        SetJsonResponse(request, 400, "error",
                        "request_id must be a string", -1,
                        "image_generation", "INVALID_REQUEST_ID");
        return false;
      }
      request_id = input["request_id"].GetString();
      if (!IsSafeResultId(request_id)) {
        SetJsonResponse(request, 400, "error",
                        "request_id must use letters, numbers, underscore, or hyphen and be at most 64 characters", -1,
                        "image_generation", "INVALID_REQUEST_ID");
        return false;
      }
    }
    if (input.HasMember("outputs")) {
      if (!input["outputs"].IsArray() || input["outputs"].GetArray().Empty() ||
          input["outputs"].Size() > kMaxGeneratedOutputs) {
        SetJsonResponse(request, 400, "error",
                        "outputs must contain between 1 and 7 items", -1,
                        "image_generation", "INVALID_IMAGE_OUTPUTS");
        return false;
      }
      std::unordered_set<std::string> result_ids;
      for (const auto& item : input["outputs"].GetArray()) {
        if (!item.IsObject() || !item.HasMember("id") ||
            !item["id"].IsString() || !item.HasMember("type") ||
            !item["type"].IsString()) {
          SetJsonResponse(request, 400, "error",
                          "each output requires string id and type", -1,
                          "image_generation", "INVALID_IMAGE_OUTPUT");
          return false;
        }
        ImageOutputRequest output;
        output.result_id = item["id"].GetString();
        output.type = item["type"].GetString();
        if (!IsSafeResultId(output.result_id) ||
            !result_ids.insert(output.result_id).second) {
          SetJsonResponse(request, 400, "error",
                          "output id must be unique and use letters, numbers, underscore, or hyphen", -1,
                          "image_generation", "INVALID_RESULT_ID");
          return false;
        }
        if (output.type == "filter") {
          if (!item.HasMember("filter") || !item["filter"].IsString()) {
            SetJsonResponse(request, 400, "error",
                            "filter output requires a filter string", -1,
                            "image_generation", "FILTER_REQUIRED");
            return false;
          }
          output.filter = item["filter"].GetString();
          CaptureEnvironment unused = CaptureEnvironment::kNormalDay;
          if (!EnvironmentProcessor::ParseFilter(output.filter, unused)) {
            SetJsonResponse(request, 400, "error",
                            "requested filter is not registered", -1,
                            "image_generation", "INVALID_FILTER");
            return false;
          }
        } else if (output.type != "original" && output.type != "auto") {
          SetJsonResponse(request, 400, "error",
                          "output type must be original, auto, or filter", -1,
                          "image_generation", "INVALID_OUTPUT_TYPE");
          return false;
        }
        outputs.push_back(std::move(output));
      }
    }
  }
  if (outputs.empty()) {
    outputs.push_back(ImageOutputRequest{"original", "original", ""});
    outputs.push_back(ImageOutputRequest{"enhanced", "auto", ""});
  }

  int retry_after_ms = 0;
  std::string admission_code;
  if (!TryBeginProcessing(retry_after_ms, admission_code)) {
    const int status = admission_code == "PROCESSING_RATE_LIMITED" ? 429 : 503;
    SetJsonResponse(request, status, "error",
                    admission_code == "PROCESSING_RATE_LIMITED"
                        ? "image generation is rate limited; retry after " +
                              std::to_string(retry_after_ms) + " ms"
                        : "frame processing is busy",
                    -1, "image_generation", admission_code);
    return false;
  }
  const std::string capture_path = CapturePathForChannel(channel);
  if (!CaptureCurrentFrame(channel, capture_path, error_message)) {
    FinishProcessing();
    SetJsonResponse(request, 502, "error", error_message, -1,
                    "snapshot", "SNAPSHOT_FAILED");
    return false;
  }

  const auto captured_at_epoch_ms = static_cast<std::int64_t>(
      std::chrono::duration_cast<std::chrono::milliseconds>(
          std::chrono::system_clock::now().time_since_epoch())
          .count());

  ImageSetResult generated;
  if (!environment_processor_.GenerateImageSet(
          capture_path, outputs, roi, GetProcessingOptions().jpeg_quality,
          generated, error_code, error_message)) {
    FinishProcessing();
    const int status = error_code == "INVALID_FILTER" ||
                       error_code == "INVALID_OUTPUT_TYPE" ||
                       error_code == "INVALID_ROI" ? 400 : 500;
    SetJsonResponse(request, status, "error", error_message, -1,
                    "image_generation", error_code);
    return false;
  }

  const std::string run_id =
      "img-" + std::to_string(++image_run_counter_);
  std::unordered_map<std::string, std::vector<unsigned char>> run_images;

  JsonUtility::JsonDocument document(JsonUtility::Type::kObjectType);
  auto& alloc = document.GetAllocator();
  JsonUtility::set(document, "success", true, alloc);
  if (!request_id.empty()) {
    JsonUtility::set(document, "request_id", request_id, alloc);
  }
  JsonUtility::set(document, "run_id", run_id, alloc);
  JsonUtility::ValueType captured_at_value;
  captured_at_value.SetInt64(captured_at_epoch_ms);
  document.AddMember(JsonUtility::ValueType("captured_at_epoch_ms", alloc),
                     captured_at_value, alloc);
  JsonUtility::set(document, "channel", channel, alloc);
  JsonUtility::set(
      document, "detected_environment",
      EnvironmentProcessor::EnvironmentName(generated.analysis.environment),
      alloc);
  JsonUtility::set(
      document, "auto_filter",
      EnvironmentProcessor::SelectedVariantName(generated.analysis.environment),
      alloc);
  JsonUtility::set(document, "image_server_port", server_port_, alloc);

  JsonUtility::ValueType scene_assessment(rapidjson::kObjectType);
  const char* environment_name =
      EnvironmentProcessor::EnvironmentName(generated.analysis.environment);
  const char* lighting_recommendation =
      LightingAssistRecommendation(generated.analysis.environment);
  JsonUtility::set(scene_assessment, "lighting_condition", environment_name,
                   alloc);
  JsonUtility::set(scene_assessment, "lighting_assist_recommendation",
                   lighting_recommendation, alloc);
  JsonUtility::set(scene_assessment, "assessment_source", "image_heuristic",
                   alloc);
  JsonUtility::set(scene_assessment, "assessment_version", "cv-scene-v1",
                   alloc);
  document.AddMember(JsonUtility::ValueType("scene_assessment", alloc),
                     scene_assessment, alloc);

  JsonUtility::ValueType roi_response(rapidjson::kObjectType);
  JsonUtility::set(roi_response, "applied", generated.roi.applied, alloc);
  if (generated.roi.applied) {
    JsonUtility::set(roi_response, "x", generated.roi.x, alloc);
    JsonUtility::set(roi_response, "y", generated.roi.y, alloc);
    JsonUtility::set(roi_response, "width", generated.roi.width, alloc);
    JsonUtility::set(roi_response, "height", generated.roi.height, alloc);
  }
  document.AddMember(JsonUtility::ValueType("roi", alloc), roi_response,
                     alloc);

  JsonUtility::ValueType results(rapidjson::kArrayType);
  for (std::size_t index = 0; index < generated.images.size(); ++index) {
    auto& image = generated.images[index];
    const auto& requested = outputs[index];
    const std::size_t jpeg_bytes = image.jpeg.size();
    run_images[image.result_id] = std::move(image.jpeg);

    JsonUtility::ValueType item(rapidjson::kObjectType);
    JsonUtility::set(item, "id", image.result_id, alloc);
    JsonUtility::set(item, "type", requested.type, alloc);
    JsonUtility::set(item, "applied_filter", image.executed_variant, alloc);
    JsonUtility::set(item, "jpeg_bytes", static_cast<int>(jpeg_bytes), alloc);
    JsonUtility::set(item, "processing_ms", image.processing_ms, alloc);
    const std::string image_path =
        "/images/result/jpg?run_id=" + run_id +
        "&result=" + image.result_id;
    JsonUtility::set(item, "image_path", image_path, alloc);
    results.PushBack(item, alloc);
  }
  document.AddMember(JsonUtility::ValueType("results", alloc), results, alloc);

  image_result_runs_[run_id] = std::move(run_images);
  image_run_order_.push_back(run_id);
  while (image_run_order_.size() > kMaxImageRuns) {
    image_result_runs_.erase(image_run_order_.front());
    image_run_order_.erase(image_run_order_.begin());
  }
  FinishProcessing();

  FileLogger::Trace(
      "INFO", "IMAGE_SET",
      "generated request_id=%s run_id=%s channel=%d captured_at_epoch_ms=%lld environment=%s auto_filter=%s lighting_assist=%s roi_applied=%d roi_x=%d roi_y=%d roi_width=%d roi_height=%d outputs=%zu cached_runs=%zu",
      request_id.empty() ? "-" : request_id.c_str(), run_id.c_str(), channel,
      static_cast<long long>(captured_at_epoch_ms), environment_name,
      EnvironmentProcessor::SelectedVariantName(generated.analysis.environment),
      lighting_recommendation, generated.roi.applied ? 1 : 0, generated.roi.x,
      generated.roi.y, generated.roi.width, generated.roi.height,
      generated.images.size(), image_run_order_.size());

  rapidjson::StringBuffer buffer;
  rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
  document.Accept(writer);
  request->SetStatusCode(200);
  request->SetResponseBody(buffer.GetString(), buffer.GetLength());
  return true;
}

void SampleComponent::HandleGeneratedImageRequest(
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
    SendErrorResponse(client_id, 400, "Bad Request", "image_generation",
                      "IMAGE_RESULT_QUERY_REQUIRED",
                      "run_id and result are required");
    return;
  }
  const auto run = image_result_runs_.find(run_id);
  if (run == image_result_runs_.end()) {
    SendErrorResponse(client_id, 404, "Not Found", "image_generation",
                      "IMAGE_RUN_NOT_FOUND",
                      "image run was not found or expired");
    return;
  }
  const auto image = run->second.find(result_id);
  if (image == run->second.end()) {
    SendErrorResponse(client_id, 404, "Not Found", "image_generation",
                      "IMAGE_RESULT_NOT_FOUND",
                      "generated image was not found");
    return;
  }

  FileLogger::Trace("INFO", "IMAGE_SET",
                    "response run_id=%s result=%s jpeg_bytes=%zu",
                    run_id.c_str(), result_id.c_str(), image->second.size());
  SendHttpResponse(client_id, 200, "OK", "image/jpeg", image->second);
}
