#pragma once

#include <string>
#include <vector>

struct ProcessingOptions {
  std::string mode;
  int blur_kernel_size;
  int canny_low_threshold;
  int canny_high_threshold;
  int jpeg_quality;
};

class FrameProcessor {
 public:
  bool ProcessJpegFile(const std::string& input_path,
                       const ProcessingOptions& options,
                       std::vector<unsigned char>& output_jpeg,
                       std::string& error_code,
                       std::string& error_message) const;
};
