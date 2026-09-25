#pragma once

#include <chrono>
#include <string>
#include <unordered_map>
#include <vector>

#include "component.h"
#include "environment_processor.h"
#include "frame_processor.h"
#include "i_sample_component.h"

class OpenAppSerializable;

class SampleComponent : public Component, public ISampleComponent {
 public:
  SampleComponent();
  SampleComponent(ClassID id, const char* name);
  virtual ~SampleComponent();

  bool ProcessAEvent(Event* event) override;

 protected:
  bool Initialize() override;

 private:
  void RegisterOpenAPIURI();
  bool ParseOpenApiEvent(Event* event);
  void SetJsonResponse(OpenAppSerializable* response,
                       int status_code,
                       const std::string& status,
                       const std::string& message,
                       int port,
                       const std::string& stage = std::string(),
                       const std::string& error_code = std::string());
  bool StartServer(OpenAppSerializable* request);
  bool GetChannels(OpenAppSerializable* request);
  bool GetEnvironmentCatalog(OpenAppSerializable* request);
  bool GetFilters(OpenAppSerializable* request);
  bool GenerateImages(OpenAppSerializable* request);
  bool RunEnvironmentTest(OpenAppSerializable* request);
  bool CaptureFrame(OpenAppSerializable* request);
  bool ProcessCapturedFrame(OpenAppSerializable* request);
  bool GetLogs(OpenAppSerializable* request);
  bool ClearLogs(OpenAppSerializable* request);
  bool ParseChannelRequest(OpenAppSerializable* request,
                           int& channel,
                           std::string& error_code,
                           std::string& error_message) const;
  bool ValidateChannel(int channel, std::string& error_message) const;
  std::string CapturePathForChannel(int channel) const;
  std::string ProcessedPathForChannel(int channel) const;
  std::string ChannelPath(const std::string& base_path, int channel) const;
  void HandleNetworkData(Event* event);
  void HandleImageRequest(int client_id, int channel);
  void HandleEnvironmentImageRequest(int client_id,
                                     const std::string& target);
  void HandleGeneratedImageRequest(int client_id,
                                   const std::string& target);
  bool CaptureCurrentFrame(int channel,
                           const std::string& capture_path,
                           std::string& error_message);
  ProcessingOptions GetProcessingOptions() const;
  bool SaveProcessedJpeg(int channel,
                         const std::vector<unsigned char>& jpeg,
                         std::string& error_message) const;
  bool TryBeginProcessing(int& retry_after_ms,
                          std::string& rejection_code);
  void FinishProcessing();
  bool GetCachedProcessedJpeg(int channel,
                              std::vector<unsigned char>& jpeg,
                              long long& age_ms) const;
  void CacheProcessedJpeg(int channel,
                          const std::vector<unsigned char>& jpeg);
  void SendHttpResponse(int client_id,
                        int status_code,
                        const std::string& reason,
                        const std::string& content_type,
                        const std::vector<unsigned char>& body);
  void SendErrorResponse(int client_id,
                         int status_code,
                         const std::string& reason,
                         const std::string& stage,
                         const std::string& error_code,
                         const std::string& message);
  void CloseClient(int client_id);

 private:
  static constexpr std::size_t kMaxHttpHeaderSize = 16 * 1024;
  static constexpr std::size_t kMaxImageRuns = 8;
  static constexpr std::size_t kMaxGeneratedOutputs = 7;

  struct CachedProcessedJpeg {
    std::vector<unsigned char> jpeg;
    std::chrono::steady_clock::time_point completed_at;
  };

  SampleAppInfoList info_list_;
  FrameProcessor frame_processor_;
  EnvironmentProcessor environment_processor_;
  std::string app_id_;
  std::unordered_map<int, std::string> request_buffers_;
  int server_port_;
  bool server_start_requested_;
  bool processing_frame_;
  std::chrono::steady_clock::time_point next_processing_allowed_at_;
  std::unordered_map<int, CachedProcessedJpeg> processed_jpeg_cache_;
  unsigned long long environment_run_counter_;
  unsigned long long image_run_counter_;
  std::string environment_run_id_;
  std::unordered_map<std::string, std::vector<unsigned char>>
      environment_result_jpegs_;
  std::vector<std::string> image_run_order_;
  std::unordered_map<
      std::string,
      std::unordered_map<std::string, std::vector<unsigned char>>>
      image_result_runs_;
};
