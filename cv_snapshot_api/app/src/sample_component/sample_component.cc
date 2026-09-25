#include "sample_component.h"

#include "file_logger.h"

#include <dispatcher_serialize.h>
#include <i_app_dispatcher.h>
#include <i_app_network_manager.h>
#include <life_cycle_manager_openapp.h>

#include <opencv2/core/utility.hpp>

#include <algorithm>
#include <cstdint>

SampleComponent::SampleComponent()
    : SampleComponent(_SampleComponent_Id, "SampleComponent") {}

SampleComponent::SampleComponent(ClassID id, const char* name)
    : Component(id, name),
      server_port_(-1),
      server_start_requested_(false),
      processing_frame_(false),
      next_processing_allowed_at_(std::chrono::steady_clock::time_point::min()),
      environment_run_counter_(0),
      image_run_counter_(0) {}

SampleComponent::~SampleComponent() = default;

bool SampleComponent::Initialize() {
  FileLogger::Trace("INFO", "INIT", "component initialization started");
  FileLogger::Trace("INFO", "INIT", "file logging path=%s", FileLogger::LogPath());
  RegisterOpenAPIURI();
  PrepareAttributes(&info_list_, GetObjectName());
  const int thread_limit = std::max(
      1, info_list_.app_attribute_info.opencv_thread_limit);
  cv::setNumThreads(thread_limit);
  FileLogger::Trace("INFO", "RESOURCE",
                    "OpenCV thread limit configured threads=%d",
                    thread_limit);
  const bool initialized = Component::Initialize();
  FileLogger::Trace(initialized ? "INFO" : "ERROR", "INIT", "component initialization %s",
        initialized ? "completed" : "failed");
  return initialized;
}

bool SampleComponent::ProcessAEvent(Event* event) {
  bool result = true;
  switch (event->GetType()) {
    case static_cast<int32_t>(IAppDispatcher::EEventType::eHttpRequest):
      result = ParseOpenApiEvent(event);
      break;

    case static_cast<int32_t>(LifeCycleManagerOpenApp::EEventType::eInformAppInfo): {
      auto blob = event->GetBlobArgument();
      auto* base_object = blob.GetBaseObject();
      if (base_object == nullptr) {
        FileLogger::Trace("ERROR", "APP_INFO", "application info payload is null");
        return false;
      }
      const auto app_info = *static_cast<String*>(base_object);
      JsonUtility::JsonDocument document;
      document.Parse(app_info.c_str());
      if (document.IsObject() && document.HasMember("AppId") &&
          document["AppId"].IsString()) {
        app_id_ = document["AppId"].GetString();
        FileLogger::Trace("INFO", "APP_INFO", "application ID received");
      } else {
        FileLogger::Trace("ERROR", "APP_INFO", "AppId was not found in application info");
      }
      break;
    }

    case static_cast<int32_t>(IAppNetworkManager::EEventType::eNewClientConnected): {
      const int client_id = event->GetArgument();
      request_buffers_[client_id] = std::string();
      FileLogger::Trace("INFO", "CLIENT", "connected client_id=%d", client_id);
      break;
    }

    case static_cast<int32_t>(IAppNetworkManager::EEventType::eClientDisconnected): {
      const int client_id = event->GetArgument();
      request_buffers_.erase(client_id);
      FileLogger::Trace("INFO", "CLIENT", "disconnected client_id=%d", client_id);
      break;
    }

    case static_cast<int32_t>(IAppNetworkManager::EEventType::eClientData):
      FileLogger::Trace("INFO", "NETWORK", "event=eClientData");
      HandleNetworkData(event);
      break;

    case static_cast<int32_t>(IAppNetworkManager::EEventType::eServerData):
      FileLogger::Trace("INFO", "NETWORK", "event=eServerData");
      HandleNetworkData(event);
      break;

    default:
      result = Component::ProcessAEvent(event);
      break;
  }
  return result;
}

void SampleComponent::RegisterOpenAPIURI() {
  Vector<String> post_methods;
  post_methods.push_back("POST");
  Vector<String> get_methods;
  get_methods.push_back("GET");
  auto* start_registrar = new ("OpenAPI") IAppDispatcher::OpenAPIRegistrar(
      String("/startserver"), GetInstanceName(), post_methods);
  auto* capture_registrar = new ("OpenAPI") IAppDispatcher::OpenAPIRegistrar(
      String("/capture"), GetInstanceName(), post_methods);
  auto* process_registrar = new ("OpenAPI") IAppDispatcher::OpenAPIRegistrar(
      String("/process"), GetInstanceName(), post_methods);
  auto* channels_registrar = new ("OpenAPI") IAppDispatcher::OpenAPIRegistrar(
      String("/channels"), GetInstanceName(), get_methods);
  auto* environment_catalog_registrar =
      new ("OpenAPI") IAppDispatcher::OpenAPIRegistrar(
          String("/environment/catalog"), GetInstanceName(), get_methods);
  auto* environment_analyze_registrar =
      new ("OpenAPI") IAppDispatcher::OpenAPIRegistrar(
          String("/environment/analyze"), GetInstanceName(), post_methods);
  auto* environment_test_registrar =
      new ("OpenAPI") IAppDispatcher::OpenAPIRegistrar(
          String("/environment/test"), GetInstanceName(), post_methods);
  auto* filters_registrar = new ("OpenAPI") IAppDispatcher::OpenAPIRegistrar(
      String("/filters"), GetInstanceName(), get_methods);
  auto* generate_images_registrar =
      new ("OpenAPI") IAppDispatcher::OpenAPIRegistrar(
          String("/images/generate"), GetInstanceName(), post_methods);
  auto* logs_registrar = new ("OpenAPI") IAppDispatcher::OpenAPIRegistrar(
      String("/logs"), GetInstanceName(), get_methods);
  auto* clear_logs_registrar = new ("OpenAPI") IAppDispatcher::OpenAPIRegistrar(
      String("/logs/clear"), GetInstanceName(), post_methods);
  SendNoReplyEvent("AppDispatcher",
                   static_cast<int32_t>(IAppDispatcher::EEventType::eRegisterCommand),
                   0, start_registrar);
  SendNoReplyEvent("AppDispatcher",
                   static_cast<int32_t>(IAppDispatcher::EEventType::eRegisterCommand),
                   0, capture_registrar);
  SendNoReplyEvent("AppDispatcher",
                   static_cast<int32_t>(IAppDispatcher::EEventType::eRegisterCommand),
                   0, process_registrar);
  SendNoReplyEvent("AppDispatcher",
                   static_cast<int32_t>(IAppDispatcher::EEventType::eRegisterCommand),
                   0, channels_registrar);
  SendNoReplyEvent("AppDispatcher",
                   static_cast<int32_t>(IAppDispatcher::EEventType::eRegisterCommand),
                   0, environment_catalog_registrar);
  SendNoReplyEvent("AppDispatcher",
                   static_cast<int32_t>(IAppDispatcher::EEventType::eRegisterCommand),
                   0, environment_analyze_registrar);
  SendNoReplyEvent("AppDispatcher",
                   static_cast<int32_t>(IAppDispatcher::EEventType::eRegisterCommand),
                   0, environment_test_registrar);
  SendNoReplyEvent("AppDispatcher",
                   static_cast<int32_t>(IAppDispatcher::EEventType::eRegisterCommand),
                   0, filters_registrar);
  SendNoReplyEvent("AppDispatcher",
                   static_cast<int32_t>(IAppDispatcher::EEventType::eRegisterCommand),
                   0, generate_images_registrar);
  SendNoReplyEvent("AppDispatcher",
                   static_cast<int32_t>(IAppDispatcher::EEventType::eRegisterCommand),
                   0, logs_registrar);
  SendNoReplyEvent("AppDispatcher",
                   static_cast<int32_t>(IAppDispatcher::EEventType::eRegisterCommand),
                   0, clear_logs_registrar);
  FileLogger::Trace("INFO", "INIT",
                    "registered environment verification, image, channel and log APIs");
}
bool SampleComponent::ParseOpenApiEvent(Event* event) {
  if (event->IsReply()) {
    return true;
  }

  auto* request = static_cast<OpenAppSerializable*>(event->GetBaseObjectArgument());
  if (request == nullptr) {
    FileLogger::Trace("ERROR", "OPEN_API", "request payload is null");
    return false;
  }

  const std::string path = request->GetFCGXParam("PATH_INFO");
  const std::string method = request->GetMethod();
  if (path != "/logs") {
    FileLogger::Trace("INFO", "OPEN_API", "request method=%s path=%s",
                      method.c_str(), path.c_str());
  }

  if (path == "/channels") {
    if (method != "GET") {
      SetJsonResponse(request, 405, "error", "GET method is required", -1,
                      "open_api", "METHOD_NOT_ALLOWED");
      return false;
    }
    return GetChannels(request);
  }
  if (path == "/environment/catalog") {
    if (method != "GET") {
      SetJsonResponse(request, 405, "error", "GET method is required", -1,
                      "open_api", "METHOD_NOT_ALLOWED");
      return false;
    }
    return GetEnvironmentCatalog(request);
  }
  if (path == "/filters") {
    if (method != "GET") {
      SetJsonResponse(request, 405, "error", "GET method is required", -1,
                      "open_api", "METHOD_NOT_ALLOWED");
      return false;
    }
    return GetFilters(request);
  }
  if (path == "/logs") {
    if (method != "GET") {
      SetJsonResponse(request, 405, "error", "GET method is required", -1,
                      "open_api", "METHOD_NOT_ALLOWED");
      return false;
    }
    return GetLogs(request);
  }
  if (path == "/logs/clear") {
    if (method != "POST") {
      SetJsonResponse(request, 405, "error", "POST method is required", -1,
                      "open_api", "METHOD_NOT_ALLOWED");
      return false;
    }
    return ClearLogs(request);
  }

  if (method != "POST") {
    FileLogger::Trace("ERROR", "OPEN_API", "method not allowed method=%s", method.c_str());
    SetJsonResponse(request, 405, "error", "POST method is required", -1,
                    "open_api", "METHOD_NOT_ALLOWED");
    return false;
  }
  if (path == "/startserver") {
    return StartServer(request);
  }
  if (path == "/capture") {
    return CaptureFrame(request);
  }
  if (path == "/process") {
    return ProcessCapturedFrame(request);
  }
  if (path == "/environment/analyze" ||
      path == "/environment/test") {
    return RunEnvironmentTest(request);
  }
  if (path == "/images/generate") {
    return GenerateImages(request);
  }

  FileLogger::Trace("ERROR", "OPEN_API", "unsupported path=%s", path.c_str());
  SetJsonResponse(request, 404, "error", "requested path is not supported", -1,
                  "open_api", "PATH_NOT_FOUND");
  return false;
}
extern "C" {
SampleComponent* create_component(void* mem_manager) {
  Component::allocator = decltype(Component::allocator)(mem_manager);
  Event::allocator = decltype(Event::allocator)(mem_manager);
  return new ("SampleComponent") SampleComponent();
}

void destroy_component(SampleComponent* ptr) { delete ptr; }
}
