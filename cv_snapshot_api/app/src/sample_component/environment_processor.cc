#include "environment_processor.h"

#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <exception>
#include <utility>

namespace {

const std::vector<EnvironmentDescriptor> kCatalog{
    {CaptureEnvironment::kNormalDay, "normal_day", "none",
     "csiq_original_no_regression", false},
    {CaptureEnvironment::kSensorNoise, "sensor_noise", "bilateral_d5",
     "csiq_test_passed", true},
    {CaptureEnvironment::kBlur, "blur", "none",
     "improvement_gate_failed", false},
    {CaptureEnvironment::kLowContrast, "low_contrast", "stretch_1_99",
     "csiq_test_passed", true},
    {CaptureEnvironment::kExtremeLowLight, "extreme_low_light", "fast_bilateral",
     "synthetic_test_passed_field_pending", true},
    {CaptureEnvironment::kBlackout, "blackout", "none",
     "regression_gate_failed", false},
    {CaptureEnvironment::kIrNight, "ir_night", "bilateral_gamma_clahe",
     "synthetic_test_passed_field_pending", true},
    {CaptureEnvironment::kIrReflection, "ir_reflection", "none",
     "regression_gate_failed", false},
    {CaptureEnvironment::kBacklight, "backlight", "backlight_combined",
     "synthetic_test_passed_field_pending", true},
    {CaptureEnvironment::kRain, "rain", "none",
     "regression_gate_failed", false},
    {CaptureEnvironment::kDefocus, "defocus", "none",
     "improvement_gate_failed", false},
    {CaptureEnvironment::kJpegArtifact, "jpeg_artifact", "none",
     "selected_none", false},
};

double Percentile8u(const cv::Mat& gray, double percentile) {
  std::array<std::size_t, 256> histogram{};
  for (int row = 0; row < gray.rows; ++row) {
    const auto* pixels = gray.ptr<unsigned char>(row);
    for (int col = 0; col < gray.cols; ++col) {
      ++histogram[pixels[col]];
    }
  }
  const double rank = std::clamp(percentile, 0.0, 100.0) *
                      static_cast<double>(gray.total() - 1) / 100.0;
  const std::size_t lower_rank = static_cast<std::size_t>(std::floor(rank));
  const std::size_t upper_rank = static_cast<std::size_t>(std::ceil(rank));
  auto value_at_rank = [&histogram](std::size_t requested_rank) {
    std::size_t cumulative = 0;
    for (std::size_t value = 0; value < histogram.size(); ++value) {
      cumulative += histogram[value];
      if (cumulative > requested_rank) {
        return static_cast<double>(value);
      }
    }
    return 255.0;
  };
  const double lower = value_at_rank(lower_rank);
  const double upper = value_at_rank(upper_rank);
  return lower + (upper - lower) * (rank - static_cast<double>(lower_rank));
}

cv::Mat AdaptiveGamma(const cv::Mat& gray,
                      double target_luminance,
                      double minimum) {
  const double median = std::clamp(
      std::max(1.0, Percentile8u(gray, 50.0)) / 255.0,
      1.0 / 255.0, 0.999);
  const double target =
      std::clamp(target_luminance / 255.0, 0.05, 0.95);
  const double gamma =
      std::clamp(std::log(target) / std::log(median), minimum, 1.0);

  cv::Mat lookup(1, 256, CV_8U);
  for (int value = 0; value < 256; ++value) {
    const double corrected =
        std::pow(static_cast<double>(value) / 255.0, gamma) * 255.0;
    lookup.at<unsigned char>(value) = static_cast<unsigned char>(
        std::clamp(corrected, 0.0, 255.0));
  }
  cv::Mat result;
  cv::LUT(gray, lookup, result);
  return result;
}

cv::Mat CompressHighlights(const cv::Mat& gray) {
  const double knee = Percentile8u(gray, 90.0);
  if (knee < 150.0) {
    return gray.clone();
  }
  cv::Mat lookup(1, 256, CV_8U);
  for (int value = 0; value < 256; ++value) {
    double output = static_cast<double>(value);
    if (knee >= 254.0 && value >= 250) {
      output = 235.0 + static_cast<double>(value - 250) * 2.0;
    } else if (value > knee) {
      output =
          210.0 + (static_cast<double>(value) - knee) * 35.0 / (255.0 - knee);
    }
    lookup.at<unsigned char>(value) = static_cast<unsigned char>(
        std::clamp(output, 0.0, 255.0));
  }
  cv::Mat result;
  cv::LUT(gray, lookup, result);
  return result;
}

cv::Mat EnhanceLowContrast(const cv::Mat& image) {
  cv::Mat lab;
  cv::cvtColor(image, lab, cv::COLOR_BGR2Lab);
  std::vector<cv::Mat> channels;
  cv::split(lab, channels);
  const double low = Percentile8u(channels[0], 1.0);
  const double high = Percentile8u(channels[0], 99.0);
  if (high <= low + 1.0) {
    return image.clone();
  }
  channels[0].convertTo(channels[0], CV_8U,
                        255.0 / (high - low),
                        -low * 255.0 / (high - low));
  cv::merge(channels, lab);
  cv::Mat result;
  cv::cvtColor(lab, result, cv::COLOR_Lab2BGR);
  return result;
}

cv::Mat EnhanceLowLight(const cv::Mat& image) {
  cv::Mat denoised;
  cv::bilateralFilter(image, denoised, 5, 25.0, 25.0);

  cv::Mat lab;
  cv::cvtColor(denoised, lab, cv::COLOR_BGR2Lab);
  std::vector<cv::Mat> channels;
  cv::split(lab, channels);
  channels[0] = AdaptiveGamma(channels[0], 105.0, 0.55);
  cv::createCLAHE(1.6, cv::Size(8, 8))->apply(channels[0], channels[0]);
  cv::merge(channels, lab);

  cv::Mat enhanced;
  cv::cvtColor(lab, enhanced, cv::COLOR_Lab2BGR);
  cv::Mat blurred;
  cv::GaussianBlur(enhanced, blurred, cv::Size(0, 0), 0.65);
  cv::Mat sharpened;
  cv::addWeighted(enhanced, 1.08, blurred, -0.08, 0.0, sharpened);
  return sharpened;
}

cv::Mat EnhanceIrNight(const cv::Mat& image) {
  cv::Mat gray;
  cv::cvtColor(image, gray, cv::COLOR_BGR2GRAY);
  cv::Mat denoised;
  cv::bilateralFilter(gray, denoised, 5, 25.0, 25.0);
  gray = AdaptiveGamma(denoised, 100.0, 0.60);
  cv::createCLAHE(1.7, cv::Size(8, 8))->apply(gray, gray);
  cv::Mat result;
  cv::cvtColor(gray, result, cv::COLOR_GRAY2BGR);
  return result;
}

cv::Mat EnhanceBacklight(const cv::Mat& image) {
  cv::Mat lab;
  cv::cvtColor(image, lab, cv::COLOR_BGR2Lab);
  std::vector<cv::Mat> channels;
  cv::split(lab, channels);
  channels[0] = CompressHighlights(channels[0]);
  channels[0] = AdaptiveGamma(channels[0], 105.0, 0.65);
  cv::createCLAHE(1.4, cv::Size(8, 8))->apply(channels[0], channels[0]);
  cv::merge(channels, lab);
  cv::Mat result;
  cv::cvtColor(lab, result, cv::COLOR_Lab2BGR);
  return result;
}

double PercentPixels(const cv::Mat& gray, int comparison, int value) {
  cv::Mat mask;
  cv::compare(gray, value, mask, comparison);
  return 100.0 * static_cast<double>(cv::countNonZero(mask)) /
         static_cast<double>(gray.total());
}

EnvironmentAnalysis AnalyzeEnvironment(
    const cv::Mat& image,
    bool has_explicit_hint,
    CaptureEnvironment explicit_hint) {
  EnvironmentAnalysis analysis;
  cv::Mat gray;
  cv::cvtColor(image, gray, cv::COLOR_BGR2GRAY);
  cv::Scalar mean;
  cv::Scalar stddev;
  cv::meanStdDev(gray, mean, stddev);
  analysis.mean_luminance = mean[0];
  analysis.contrast_stddev = stddev[0];
  analysis.shadow_percent = PercentPixels(gray, cv::CMP_LE, 5);
  analysis.highlight_percent = PercentPixels(gray, cv::CMP_GE, 250);

  cv::Mat hsv;
  cv::cvtColor(image, hsv, cv::COLOR_BGR2HSV);
  analysis.mean_saturation = cv::mean(hsv)[1];

  cv::Mat laplacian;
  cv::Laplacian(gray, laplacian, CV_32F);
  cv::meanStdDev(laplacian, mean, stddev);
  analysis.laplacian_variance = stddev[0] * stddev[0];

  const int margin_x = gray.cols / 10;
  const int margin_y = gray.rows / 10;
  const cv::Rect content_rect(margin_x, margin_y,
                              gray.cols - 2 * margin_x,
                              gray.rows - 2 * margin_y);
  const cv::Mat content_gray = gray(content_rect);
  cv::Mat content_median;
  cv::medianBlur(content_gray, content_median, 3);
  const double p5_luminance = Percentile8u(content_median, 5.0);
  analysis.p95_luminance = Percentile8u(content_median, 95.0);
  analysis.dynamic_range = analysis.p95_luminance - p5_luminance;
  cv::Mat content_edges;
  cv::Canny(content_median, content_edges, 12.0, 36.0);
  analysis.edge_density =
      static_cast<double>(cv::countNonZero(content_edges)) /
      static_cast<double>(content_edges.total());

  cv::Mat median;
  cv::medianBlur(gray, median, 3);
  cv::Mat residual;
  cv::absdiff(gray, median, residual);
  analysis.noise_mad = cv::mean(residual)[0];

  double adjacent_x_total = 0.0;
  std::size_t adjacent_x_count = 0;
  double adjacent_y_total = 0.0;
  std::size_t adjacent_y_count = 0;
  double boundary_x_total = 0.0;
  std::size_t boundary_x_count = 0;
  double boundary_y_total = 0.0;
  std::size_t boundary_y_count = 0;
  for (int row = 0; row < gray.rows; ++row) {
    const auto* pixels = gray.ptr<unsigned char>(row);
    for (int col = 1; col < gray.cols; ++col) {
      const double difference =
          std::abs(static_cast<int>(pixels[col]) - pixels[col - 1]);
      adjacent_x_total += difference;
      ++adjacent_x_count;
      if (col % 8 == 0) {
        boundary_x_total += difference;
        ++boundary_x_count;
      }
    }
  }
  for (int row = 1; row < gray.rows; ++row) {
    const auto* pixels = gray.ptr<unsigned char>(row);
    const auto* previous = gray.ptr<unsigned char>(row - 1);
    for (int col = 0; col < gray.cols; ++col) {
      const double difference =
          std::abs(static_cast<int>(pixels[col]) - previous[col]);
      adjacent_y_total += difference;
      ++adjacent_y_count;
      if (row % 8 == 0) {
        boundary_y_total += difference;
        ++boundary_y_count;
      }
    }
  }
  const double adjacent_x_mean = adjacent_x_count == 0
                                     ? 0.0
                                     : adjacent_x_total / adjacent_x_count;
  const double adjacent_y_mean = adjacent_y_count == 0
                                     ? 0.0
                                     : adjacent_y_total / adjacent_y_count;
  const double adjacent_mean = (adjacent_x_mean + adjacent_y_mean) / 2.0;
  double boundary_mean = 0.0;
  int boundary_axis_count = 0;
  if (boundary_x_count > 0) {
    boundary_mean += boundary_x_total / boundary_x_count;
    ++boundary_axis_count;
  }
  if (boundary_y_count > 0) {
    boundary_mean += boundary_y_total / boundary_y_count;
    ++boundary_axis_count;
  }
  if (boundary_axis_count > 0) {
    boundary_mean /= boundary_axis_count;
  }
  analysis.jpeg_blockiness =
      std::max(0.0, boundary_mean - adjacent_mean);

  if (has_explicit_hint) {
    analysis.environment = explicit_hint;
    analysis.used_explicit_hint = true;
  } else if (analysis.mean_luminance < 8.0 &&
             analysis.shadow_percent >= 90.0 &&
             analysis.p95_luminance < 20.0 &&
             analysis.dynamic_range < 18.0 &&
             analysis.edge_density < 0.002) {
    analysis.environment = CaptureEnvironment::kBlackout;
  } else if (analysis.mean_saturation < 8.0 &&
             analysis.mean_luminance < 110.0) {
    analysis.environment =
        analysis.highlight_percent >= 2.0
            ? CaptureEnvironment::kIrReflection
            : CaptureEnvironment::kIrNight;
  } else if (analysis.highlight_percent >= 0.4 &&
             analysis.contrast_stddev >= 35.0 &&
             analysis.mean_saturation >= 15.0) {
    analysis.environment = CaptureEnvironment::kBacklight;
  } else if (analysis.mean_luminance < 35.0 ||
             analysis.shadow_percent >= 30.0) {
    analysis.environment = CaptureEnvironment::kExtremeLowLight;
  } else if (analysis.contrast_stddev < 22.0) {
    analysis.environment = CaptureEnvironment::kLowContrast;
  } else if (analysis.laplacian_variance < 55.0) {
    analysis.environment = CaptureEnvironment::kDefocus;
  } else if (analysis.laplacian_variance >= 800.0 &&
             analysis.noise_mad >= 3.0 &&
             analysis.highlight_percent < 0.5) {
    analysis.environment = CaptureEnvironment::kRain;
  } else if (analysis.noise_mad >= 7.0 &&
             analysis.laplacian_variance >= 100.0) {
    analysis.environment = CaptureEnvironment::kSensorNoise;
  } else if (analysis.jpeg_blockiness >= 4.0) {
    analysis.environment = CaptureEnvironment::kJpegArtifact;
  } else {
    analysis.environment = CaptureEnvironment::kNormalDay;
  }
  return analysis;
}

cv::Mat EnhanceForEnvironment(const cv::Mat& image,
                              CaptureEnvironment environment) {
  switch (environment) {
    case CaptureEnvironment::kSensorNoise: {
      cv::Mat result;
      cv::bilateralFilter(image, result, 5, 25.0, 25.0);
      return result;
    }
    case CaptureEnvironment::kLowContrast:
      return EnhanceLowContrast(image);
    case CaptureEnvironment::kExtremeLowLight:
      return EnhanceLowLight(image);
    case CaptureEnvironment::kIrNight:
      return EnhanceIrNight(image);
    case CaptureEnvironment::kBacklight:
      return EnhanceBacklight(image);
    default:
      return image.clone();
  }
}

bool EncodeJpeg(const cv::Mat& image,
                int jpeg_quality,
                std::vector<unsigned char>& jpeg) {
  const std::vector<int> options{
      cv::IMWRITE_JPEG_QUALITY, std::clamp(jpeg_quality, 1, 100)};
  return cv::imencode(".jpg", image, jpeg, options);
}

bool ApplyRoi(const cv::Mat& source,
              const ImageRoi& requested_roi,
              cv::Mat& cropped,
              std::string& error_code,
              std::string& error_message) {
  if (!requested_roi.applied) {
    cropped = source;
    return true;
  }
  if (requested_roi.x < 0 || requested_roi.y < 0 ||
      requested_roi.width <= 0 || requested_roi.height <= 0) {
    error_code = "INVALID_ROI";
    error_message = "roi x and y must be non-negative and width and height must be greater than zero";
    return false;
  }
  const long long right = static_cast<long long>(requested_roi.x) +
                          static_cast<long long>(requested_roi.width);
  const long long bottom = static_cast<long long>(requested_roi.y) +
                           static_cast<long long>(requested_roi.height);
  if (right > source.cols || bottom > source.rows) {
    error_code = "INVALID_ROI";
    error_message = "roi must be fully inside the captured image";
    return false;
  }
  cropped = source(cv::Rect(requested_roi.x, requested_roi.y,
                            requested_roi.width, requested_roi.height));
  return true;
}

EnvironmentProcessedImage ProcessOne(const cv::Mat& source,
                                     CaptureEnvironment environment,
                                     const std::string& result_id,
                                     int jpeg_quality) {
  EnvironmentProcessedImage output;
  output.result_id = result_id;
  output.environment = environment;
  output.expected_variant =
      EnvironmentProcessor::SelectedVariantName(environment);

  const auto started = std::chrono::steady_clock::now();
  const cv::Mat processed = EnhanceForEnvironment(source, environment);
  const auto completed = std::chrono::steady_clock::now();
  output.processing_ms =
      std::chrono::duration<double, std::milli>(completed - started).count();
  output.executed_variant =
      EnvironmentProcessor::SelectedVariantName(environment);
  output.applied = output.executed_variant != "none";
  output.mapping_match =
      output.expected_variant == output.executed_variant;
  if (processed.empty() ||
      !EncodeJpeg(processed, jpeg_quality, output.jpeg)) {
    output.executed_variant.clear();
    output.mapping_match = false;
    output.jpeg.clear();
  }
  return output;
}

}  // namespace

const std::vector<EnvironmentDescriptor>& EnvironmentProcessor::Catalog() {
  return kCatalog;
}

const char* EnvironmentProcessor::EnvironmentName(
    CaptureEnvironment environment) {
  for (const auto& item : kCatalog) {
    if (item.environment == environment) {
      return item.id;
    }
  }
  return "normal_day";
}

const char* EnvironmentProcessor::SelectedVariantName(
    CaptureEnvironment environment) {
  for (const auto& item : kCatalog) {
    if (item.environment == environment) {
      return item.selected_variant;
    }
  }
  return "none";
}

bool EnvironmentProcessor::ParseEnvironment(
    const std::string& text,
    CaptureEnvironment& environment) {
  for (const auto& item : kCatalog) {
    if (text == item.id) {
      environment = item.environment;
      return true;
    }
  }
  return false;
}

bool EnvironmentProcessor::ParseFilter(
    const std::string& text,
    CaptureEnvironment& environment) {
  for (const auto& item : kCatalog) {
    if (item.applies_processing && text == item.selected_variant) {
      environment = item.environment;
      return true;
    }
  }
  return false;
}

bool EnvironmentProcessor::GenerateImageSet(
    const std::string& input_path,
    const std::vector<ImageOutputRequest>& outputs,
    const ImageRoi& requested_roi,
    int jpeg_quality,
    ImageSetResult& result,
    std::string& error_code,
    std::string& error_message) const {
  result = ImageSetResult{};
  error_code.clear();
  error_message.clear();

  if (outputs.empty()) {
    error_code = "IMAGE_OUTPUTS_REQUIRED";
    error_message = "at least one image output is required";
    return false;
  }

  try {
    const cv::Mat source = cv::imread(input_path, cv::IMREAD_COLOR);
    if (source.empty() || source.type() != CV_8UC3) {
      error_code = "IMAGE_DECODE_FAILED";
      error_message = "captured JPEG could not be decoded as BGR";
      return false;
    }

    cv::Mat working;
    if (!ApplyRoi(source, requested_roi, working, error_code, error_message)) {
      return false;
    }
    result.roi = requested_roi;
    result.analysis = AnalyzeEnvironment(
        working, false, CaptureEnvironment::kNormalDay);
    for (const auto& request : outputs) {
      EnvironmentProcessedImage output;
      if (request.type == "original") {
        output.result_id = request.result_id;
        output.environment = result.analysis.environment;
        output.expected_variant = "original";
        output.executed_variant = "original";
        output.mapping_match = true;
        if (!EncodeJpeg(working, jpeg_quality, output.jpeg)) {
          error_code = "JPEG_ENCODE_FAILED";
          error_message = "original frame JPEG encoding failed";
          return false;
        }
      } else {
        CaptureEnvironment environment = result.analysis.environment;
        if (request.type == "filter" &&
            !ParseFilter(request.filter, environment)) {
          error_code = "INVALID_FILTER";
          error_message = "filter is not registered in the current catalog";
          return false;
        }
        if (request.type != "auto" && request.type != "filter") {
          error_code = "INVALID_OUTPUT_TYPE";
          error_message = "output type must be original, auto, or filter";
          return false;
        }
        output = ProcessOne(working, environment, request.result_id,
                            jpeg_quality);
        if (output.jpeg.empty()) {
          error_code = "JPEG_ENCODE_FAILED";
          error_message = "processed image JPEG encoding failed";
          return false;
        }
      }
      result.images.push_back(std::move(output));
    }
  } catch (const cv::Exception& exception) {
    error_code = "OPENCV_FAILED";
    error_message = exception.what();
    return false;
  } catch (const std::exception& exception) {
    error_code = "IMAGE_GENERATION_FAILED";
    error_message = exception.what();
    return false;
  }
  return true;
}

bool EnvironmentProcessor::ProcessJpegFile(    const std::string& input_path,
    const std::string& test_mode,
    const std::string& explicit_environment,
    int jpeg_quality,
    EnvironmentTestResult& result,
    std::string& error_code,
    std::string& error_message) const {
  result = EnvironmentTestResult{};
  result.test_mode = test_mode;
  error_code.clear();
  error_message.clear();

  if (test_mode != "auto" &&
      test_mode != "explicit" &&
      test_mode != "all_selected") {
    error_code = "INVALID_TEST_MODE";
    error_message = "test_mode must be auto, explicit, or all_selected";
    return false;
  }

  CaptureEnvironment explicit_hint = CaptureEnvironment::kNormalDay;
  const bool has_explicit_hint = test_mode == "explicit";
  if (has_explicit_hint &&
      !ParseEnvironment(explicit_environment, explicit_hint)) {
    error_code = "INVALID_ENVIRONMENT";
    error_message = "environment is not registered in the current catalog";
    return false;
  }

  try {
    const cv::Mat source = cv::imread(input_path, cv::IMREAD_COLOR);
    if (source.empty() || source.type() != CV_8UC3) {
      error_code = "IMAGE_DECODE_FAILED";
      error_message = "captured JPEG could not be decoded as BGR";
      return false;
    }

    result.analysis =
        AnalyzeEnvironment(source, has_explicit_hint, explicit_hint);

    EnvironmentProcessedImage original;
    original.result_id = "original";
    original.environment = result.analysis.environment;
    original.expected_variant = "original";
    original.executed_variant = "original";
    original.mapping_match = true;
    if (!EncodeJpeg(source, jpeg_quality, original.jpeg)) {
      error_code = "JPEG_ENCODE_FAILED";
      error_message = "original frame JPEG encoding failed";
      return false;
    }
    result.images.push_back(std::move(original));

    if (test_mode == "all_selected") {
      const std::array<CaptureEnvironment, 5> selected{
          CaptureEnvironment::kSensorNoise,
          CaptureEnvironment::kLowContrast,
          CaptureEnvironment::kExtremeLowLight,
          CaptureEnvironment::kIrNight,
          CaptureEnvironment::kBacklight,
      };
      for (const auto environment : selected) {
        EnvironmentProcessedImage output =
            ProcessOne(source, environment, EnvironmentName(environment),
                       jpeg_quality);
        if (output.jpeg.empty()) {
          error_code = "JPEG_ENCODE_FAILED";
          error_message =
              "selected environment result JPEG encoding failed";
          return false;
        }
        result.images.push_back(std::move(output));
      }
    } else {
      EnvironmentProcessedImage output =
          ProcessOne(source, result.analysis.environment, "applied",
                     jpeg_quality);
      if (output.jpeg.empty()) {
        error_code = "JPEG_ENCODE_FAILED";
        error_message = "selected environment result JPEG encoding failed";
        return false;
      }
      result.images.push_back(std::move(output));
    }
  } catch (const cv::Exception& exception) {
    error_code = "OPENCV_FAILED";
    error_message = exception.what();
    return false;
  } catch (const std::exception& exception) {
    error_code = "ENVIRONMENT_PROCESSING_FAILED";
    error_message = exception.what();
    return false;
  }
  return true;
}
