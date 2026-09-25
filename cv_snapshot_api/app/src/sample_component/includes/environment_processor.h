#pragma once

#include <string>
#include <vector>

enum class CaptureEnvironment {
  kNormalDay,
  kSensorNoise,
  kBlur,
  kLowContrast,
  kExtremeLowLight,
  kBlackout,
  kIrNight,
  kIrReflection,
  kBacklight,
  kRain,
  kDefocus,
  kJpegArtifact,
};

struct EnvironmentDescriptor {
  CaptureEnvironment environment;
  const char* id;
  const char* selected_variant;
  const char* validation_status;
  bool applies_processing;
};

struct EnvironmentAnalysis {
  CaptureEnvironment environment{CaptureEnvironment::kNormalDay};
  bool used_explicit_hint{false};
  double mean_luminance{0.0};
  double contrast_stddev{0.0};
  double shadow_percent{0.0};
  double highlight_percent{0.0};
  double mean_saturation{0.0};
  double laplacian_variance{0.0};
  double p95_luminance{0.0};
  double dynamic_range{0.0};
  double edge_density{0.0};
  double noise_mad{0.0};
  double jpeg_blockiness{0.0};
};

struct EnvironmentProcessedImage {
  std::string result_id;
  CaptureEnvironment environment{CaptureEnvironment::kNormalDay};
  std::string expected_variant;
  std::string executed_variant;
  bool applied{false};
  bool mapping_match{false};
  double processing_ms{0.0};
  std::vector<unsigned char> jpeg;
};

struct EnvironmentTestResult {
  std::string test_mode;
  EnvironmentAnalysis analysis;
  std::vector<EnvironmentProcessedImage> images;
};

struct ImageOutputRequest {
  std::string result_id;
  std::string type;
  std::string filter;
};

struct ImageRoi {
  bool applied{false};
  int x{0};
  int y{0};
  int width{0};
  int height{0};
};

struct ImageSetResult {
  EnvironmentAnalysis analysis;
  ImageRoi roi;
  std::vector<EnvironmentProcessedImage> images;
};

class EnvironmentProcessor {
 public:
  static const std::vector<EnvironmentDescriptor>& Catalog();
  static const char* EnvironmentName(CaptureEnvironment environment);
  static const char* SelectedVariantName(CaptureEnvironment environment);
  static bool ParseEnvironment(const std::string& text,
                               CaptureEnvironment& environment);
  static bool ParseFilter(const std::string& text,
                          CaptureEnvironment& environment);

  bool GenerateImageSet(const std::string& input_path,
                        const std::vector<ImageOutputRequest>& outputs,
                        const ImageRoi& requested_roi,
                        int jpeg_quality,
                        ImageSetResult& result,
                        std::string& error_code,
                        std::string& error_message) const;

  bool ProcessJpegFile(const std::string& input_path,
                       const std::string& test_mode,
                       const std::string& explicit_environment,
                       int jpeg_quality,
                       EnvironmentTestResult& result,
                       std::string& error_code,
                       std::string& error_message) const;
};
