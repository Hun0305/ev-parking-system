#pragma once

#include <cstddef>
#include <string>

class FileLogger {
 public:
  static void Trace(const char* level, const char* stage, const char* format, ...);
  static bool Write(const std::string& level,
                    const std::string& stage,
                    const std::string& message);
  static bool ReadTail(int backup_index,
                       int max_lines,
                       std::string& content,
                       std::size_t& file_size,
                       int& returned_lines,
                       bool& truncated,
                       std::string& error_code,
                       std::string& error_message);
  static bool Clear(bool include_backups,
                    std::string& error_code,
                    std::string& error_message);
  static const char* LogPath();
};