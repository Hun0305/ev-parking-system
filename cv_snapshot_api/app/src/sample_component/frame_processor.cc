#include "frame_processor.h"

#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <exception>

bool FrameProcessor::ProcessJpegFile(const std::string& input_path,
                                     const ProcessingOptions& options,
                                     std::vector<unsigned char>& output_jpeg,
                                     std::string& error_code,
                                     std::string& error_message) const {
  output_jpeg.clear();
  error_code.clear();
  error_message.clear();

  if (options.mode != "canny") {
    error_code = "UNSUPPORTED_PROCESSING_MODE";
    error_message = "unsupported processing mode";
    return false;
  }
  if (options.blur_kernel_size <= 0 || options.blur_kernel_size % 2 == 0) {
    error_code = "INVALID_PROCESSING_OPTIONS";
    error_message = "blur kernel size must be a positive odd number";
    return false;
  }
  if (options.canny_low_threshold < 0 ||
      options.canny_high_threshold <= options.canny_low_threshold) {
    error_code = "INVALID_PROCESSING_OPTIONS";
    error_message = "invalid Canny thresholds";
    return false;
  }

  try {
    const cv::Mat gray = cv::imread(input_path, cv::IMREAD_GRAYSCALE);
    if (gray.empty()) {
      error_code = "IMAGE_DECODE_FAILED";
      error_message = "captured JPEG could not be decoded";
      return false;
    }
    cv::Mat blurred;
    cv::GaussianBlur(gray, blurred,
                     cv::Size(options.blur_kernel_size, options.blur_kernel_size), 0);
    cv::Mat edges;
    cv::Canny(blurred, edges, options.canny_low_threshold,
              options.canny_high_threshold, 3, false);

    const int jpeg_quality = std::clamp(options.jpeg_quality, 1, 100);
    const std::vector<int> encode_options{cv::IMWRITE_JPEG_QUALITY, jpeg_quality};
    if (!cv::imencode(".jpg", edges, output_jpeg, encode_options)) {
      error_code = "JPEG_ENCODE_FAILED";
      error_message = "processed frame JPEG encoding failed";
      return false;
    }
  } catch (const cv::Exception& exception) {
    error_code = "OPENCV_FAILED";
    error_message = exception.what();
    return false;
  } catch (const std::exception& exception) {
    error_code = "PROCESSING_FAILED";
    error_message = exception.what();
    return false;
  }
  return true;
}
