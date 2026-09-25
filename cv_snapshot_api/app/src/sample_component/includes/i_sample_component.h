#pragma once

#include <fstream>
#include <map>
#include <string>

#include "json_utility.h"
#include "serializable_json.h"
#include "typedef_application.h"

constexpr ClassID _SampleComponent_Id = GET_CLASS_UID(_ELayer_Application::_eSampleComponent);

class ISampleComponent {
 public:
  enum class EEventType { eBegin = _SampleComponent_Id, eEnd };

  struct AppAttributeInfo {
    std::string jpeg_path;
    std::string processed_jpeg_path;
    int channel_count;
    int default_channel;
    std::string processing_mode;
    int blur_kernel_size;
    int canny_low_threshold;
    int canny_high_threshold;
    int jpeg_quality;
    int processing_min_interval_ms;
    int processed_jpeg_cache_ttl_ms;
    int opencv_thread_limit;

    void Reset() {
      jpeg_path = "../res/capture.jpg";
      processed_jpeg_path = "../res/processed.jpg";
      channel_count = 4;
      default_channel = 0;
      processing_mode = "canny";
      blur_kernel_size = 3;
      canny_low_threshold = 100;
      canny_high_threshold = 200;
      jpeg_quality = 90;
      processing_min_interval_ms = 2500;
      processed_jpeg_cache_ttl_ms = 2500;
      opencv_thread_limit = 1;
    }

    AppAttributeInfo() { Reset(); }
  };

  struct SampleAppInfoList : public SerializableJson {
    AppAttributeInfo app_attribute_info;
    std::string attribute_version_;

    SampleAppInfoList() {
      sizeOfThis = sizeof(*this);
      attribute_version_ = "1.0.0";
    }

    std::string Serialize(const std::string& group_name,
                          std::map<std::string, SerializerAttribute>& key_value_map,
                          std::string version) override {
      (void)key_value_map;
      (void)version;
      JsonUtility::JsonDocument document;
      auto& alloc = document.GetAllocator();
      document.SetObject();
      JsonUtility::set(document, "Version", attribute_version_, alloc);
      JsonUtility::set(document, "Name", group_name, alloc);

      JsonUtility::ValueType attributes(rapidjson::kArrayType);
      JsonUtility::ValueType app_info(rapidjson::kObjectType);
      JsonUtility::set(app_info, "jpeg_path", app_attribute_info.jpeg_path, alloc);
      JsonUtility::set(app_info, "processed_jpeg_path", app_attribute_info.processed_jpeg_path, alloc);
      JsonUtility::set(app_info, "channel_count", app_attribute_info.channel_count, alloc);
      JsonUtility::set(app_info, "default_channel", app_attribute_info.default_channel, alloc);
      JsonUtility::set(app_info, "processing_mode", app_attribute_info.processing_mode, alloc);
      JsonUtility::set(app_info, "blur_kernel_size", app_attribute_info.blur_kernel_size, alloc);
      JsonUtility::set(app_info, "canny_low_threshold", app_attribute_info.canny_low_threshold, alloc);
      JsonUtility::set(app_info, "canny_high_threshold", app_attribute_info.canny_high_threshold, alloc);
      JsonUtility::set(app_info, "jpeg_quality", app_attribute_info.jpeg_quality, alloc);
      JsonUtility::set(app_info, "processing_min_interval_ms",
                       app_attribute_info.processing_min_interval_ms, alloc);
      JsonUtility::set(app_info, "processed_jpeg_cache_ttl_ms",
                       app_attribute_info.processed_jpeg_cache_ttl_ms, alloc);
      JsonUtility::set(app_info, "opencv_thread_limit",
                       app_attribute_info.opencv_thread_limit, alloc);
      attributes.PushBack(app_info, alloc);
      document.AddMember(JsonUtility::ValueType("Attributes", alloc), attributes, alloc);

      rapidjson::StringBuffer strbuf;
      rapidjson::PrettyWriter<rapidjson::StringBuffer> writer(strbuf);
      document.Accept(writer);
      return strbuf.GetString();
    }

    bool Deserialize(const std::string& input_string,
                     const std::string& group_name,
                     std::map<std::string, SerializerAttribute>& key_value_map) override {
      (void)key_value_map;
      JsonUtility::JsonDocument document;
      const rapidjson::ParseResult parse_result = document.Parse(input_string);
      if (!parse_result || !document.IsObject()) {
        Log::Print("Attribute file parsing failed at %s Error Code : %d\n",
                   group_name.c_str(), parse_result.Code());
        return false;
      }
      if (document.HasMember("Version") && document["Version"].IsString()) {
        attribute_version_ = document["Version"].GetString();
      }
      auto attributes = document.FindMember("Attributes");
      if (attributes == document.MemberEnd() || !attributes->value.IsArray() ||
          attributes->value.GetArray().Empty()) {
        Log::Print("Attribute Field is not found at %s\n", group_name.c_str());
        return false;
      }

      app_attribute_info.Reset();
      auto& app_info = attributes->value[0];
      if (!app_info.IsObject()) {
        return false;
      }
      JsonUtility::get(app_info, "jpeg_path", app_attribute_info.jpeg_path);
      JsonUtility::get(app_info, "processed_jpeg_path", app_attribute_info.processed_jpeg_path);
      JsonUtility::get(app_info, "channel_count", app_attribute_info.channel_count);
      JsonUtility::get(app_info, "default_channel", app_attribute_info.default_channel);
      JsonUtility::get(app_info, "processing_mode", app_attribute_info.processing_mode);
      JsonUtility::get(app_info, "blur_kernel_size", app_attribute_info.blur_kernel_size);
      JsonUtility::get(app_info, "canny_low_threshold", app_attribute_info.canny_low_threshold);
      JsonUtility::get(app_info, "canny_high_threshold", app_attribute_info.canny_high_threshold);
      JsonUtility::get(app_info, "jpeg_quality", app_attribute_info.jpeg_quality);
      JsonUtility::get(app_info, "processing_min_interval_ms",
                       app_attribute_info.processing_min_interval_ms);
      JsonUtility::get(app_info, "processed_jpeg_cache_ttl_ms",
                       app_attribute_info.processed_jpeg_cache_ttl_ms);
      JsonUtility::get(app_info, "opencv_thread_limit",
                       app_attribute_info.opencv_thread_limit);
      return true;
    }

    bool WriteFile(const std::string& filename,
                   const std::string& group_name,
                   std::string& output_string) override {
      (void)group_name;
      std::ofstream output_file(filename.c_str(), std::ofstream::trunc);
      if (!output_file.is_open()) {
        return false;
      }
      output_file << output_string;
      return output_file.good();
    }
  };
};
