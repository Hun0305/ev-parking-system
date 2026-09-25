#include "file_logger.h"

#include <sys/stat.h>

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <mutex>
#include <sstream>
#include <vector>

namespace {
constexpr const char* kLogDirectory = "../res/log";
constexpr const char* kLogPath = "../res/log/cv_snapshot_api.log";
constexpr std::size_t kMaxLogSize = 1024 * 1024;
constexpr std::size_t kMaxReadSize = 256 * 1024;
constexpr int kBackupCount = 3;

std::mutex& LogMutex() {
  static std::mutex mutex;
  return mutex;
}

bool EnsureLogDirectory() {
  struct stat info = {};
  if (stat(kLogDirectory, &info) == 0) {
    return S_ISDIR(info.st_mode);
  }
  if (mkdir(kLogDirectory, 0755) == 0) {
    return true;
  }
  return errno == EEXIST;
}

std::string BackupPath(int index) {
  return std::string(kLogPath) + "." + std::to_string(index);
}

std::string PathForIndex(int backup_index) {
  return backup_index == 0 ? std::string(kLogPath) : BackupPath(backup_index);
}

void RotateIfNeeded(std::size_t incoming_size) {
  struct stat info = {};
  if (stat(kLogPath, &info) != 0 ||
      static_cast<std::size_t>(info.st_size) + incoming_size <= kMaxLogSize) {
    return;
  }

  std::remove(BackupPath(kBackupCount).c_str());
  for (int index = kBackupCount - 1; index >= 1; --index) {
    std::rename(BackupPath(index).c_str(), BackupPath(index + 1).c_str());
  }
  std::rename(kLogPath, BackupPath(1).c_str());
}

std::string CurrentTimestamp() {
  const auto now = std::chrono::system_clock::now();
  const auto milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(
      now.time_since_epoch()) % 1000;
  const std::time_t time = std::chrono::system_clock::to_time_t(now);
  std::tm local_time = {};
  localtime_r(&time, &local_time);

  std::ostringstream timestamp;
  timestamp << std::put_time(&local_time, "%Y-%m-%d %H:%M:%S") << '.'
            << std::setfill('0') << std::setw(3) << milliseconds.count();
  return timestamp.str();
}
}  // namespace

void FileLogger::Trace(const char* level, const char* stage, const char* format, ...) {
  char message[1024] = {};
  va_list args;
  va_start(args, format);
  std::vsnprintf(message, sizeof(message), format, args);
  va_end(args);

  std::fprintf(stderr, "[cv_snapshot_api][%s][%s] %s\n", level, stage, message);
  std::fflush(stderr);
  Write(level, stage, message);
}

bool FileLogger::Write(const std::string& level,
                       const std::string& stage,
                       const std::string& message) {
  std::lock_guard<std::mutex> lock(LogMutex());
  if (!EnsureLogDirectory()) {
    return false;
  }

  std::ostringstream line;
  line << CurrentTimestamp() << " [" << level << "] [" << stage << "] "
       << message << '\n';
  const std::string text = line.str();
  RotateIfNeeded(text.size());

  std::ofstream output(kLogPath, std::ios::binary | std::ios::app);
  if (!output.is_open()) {
    return false;
  }
  output.write(text.data(), static_cast<std::streamsize>(text.size()));
  output.flush();
  return output.good();
}

bool FileLogger::ReadTail(int backup_index,
                          int max_lines,
                          std::string& content,
                          std::size_t& file_size,
                          int& returned_lines,
                          bool& truncated,
                          std::string& error_code,
                          std::string& error_message) {
  std::lock_guard<std::mutex> lock(LogMutex());
  content.clear();
  file_size = 0;
  returned_lines = 0;
  truncated = false;
  error_code.clear();
  error_message.clear();

  if (backup_index < 0 || backup_index > kBackupCount) {
    error_code = "INVALID_LOG_FILE";
    error_message = "requested log file is not supported";
    return false;
  }
  if (max_lines < 1 || max_lines > 500) {
    error_code = "INVALID_LOG_LINES";
    error_message = "log lines must be between 1 and 500";
    return false;
  }

  const std::string path = PathForIndex(backup_index);
  struct stat info = {};
  if (stat(path.c_str(), &info) != 0) {
    error_code = errno == ENOENT ? "LOG_FILE_NOT_FOUND" : "LOG_FILE_STAT_FAILED";
    error_message = errno == ENOENT ? "requested log file does not exist"
                                    : "log file metadata could not be read";
    return false;
  }
  if (info.st_size < 0) {
    error_code = "LOG_FILE_STAT_FAILED";
    error_message = "log file size is invalid";
    return false;
  }

  file_size = static_cast<std::size_t>(info.st_size);
  const std::size_t read_size = std::min(file_size, kMaxReadSize);
  const std::size_t start_offset = file_size - read_size;
  std::ifstream input(path, std::ios::binary);
  if (!input.is_open()) {
    error_code = "LOG_FILE_OPEN_FAILED";
    error_message = "log file could not be opened";
    return false;
  }
  input.seekg(static_cast<std::streamoff>(start_offset), std::ios::beg);
  std::string buffer(read_size, '\0');
  input.read(buffer.data(), static_cast<std::streamsize>(read_size));
  buffer.resize(static_cast<std::size_t>(input.gcount()));
  if (input.bad()) {
    error_code = "LOG_FILE_READ_FAILED";
    error_message = "log file could not be read";
    return false;
  }

  if (start_offset > 0) {
    const std::size_t first_newline = buffer.find('\n');
    if (first_newline == std::string::npos) {
      buffer.clear();
    } else {
      buffer.erase(0, first_newline + 1);
    }
    truncated = true;
  }

  std::vector<std::string> lines;
  std::istringstream stream(buffer);
  std::string line;
  while (std::getline(stream, line)) {
    lines.push_back(line);
  }
  const std::size_t first_line = lines.size() > static_cast<std::size_t>(max_lines)
                                     ? lines.size() - static_cast<std::size_t>(max_lines)
                                     : 0;
  if (first_line > 0) {
    truncated = true;
  }

  std::ostringstream selected;
  for (std::size_t index = first_line; index < lines.size(); ++index) {
    selected << lines[index] << '\n';
  }
  content = selected.str();
  returned_lines = static_cast<int>(lines.size() - first_line);
  return true;
}

bool FileLogger::Clear(bool include_backups,
                       std::string& error_code,
                       std::string& error_message) {
  std::lock_guard<std::mutex> lock(LogMutex());
  error_code.clear();
  error_message.clear();
  if (!EnsureLogDirectory()) {
    error_code = "LOG_DIRECTORY_CREATE_FAILED";
    error_message = "log directory could not be created";
    return false;
  }

  std::ofstream current(kLogPath, std::ios::binary | std::ios::trunc);
  if (!current.is_open()) {
    error_code = "LOG_CLEAR_FAILED";
    error_message = "current log file could not be cleared";
    return false;
  }
  current.close();

  if (include_backups) {
    for (int index = 1; index <= kBackupCount; ++index) {
      const std::string path = BackupPath(index);
      if (std::remove(path.c_str()) != 0 && errno != ENOENT) {
        error_code = "LOG_BACKUP_DELETE_FAILED";
        error_message = "one or more backup log files could not be deleted";
        return false;
      }
    }
  }
  return true;
}

const char* FileLogger::LogPath() { return kLogPath; }