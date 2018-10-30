#include "logger.h"
#include "strings.h"
#include <cstdlib>
#include <iostream>
#include <sstream>
#include <spdlog/async.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>

namespace atlasagent {

static constexpr const char* const kMainLogger = "main_logger";

static std::vector<std::string> logging_directories = {"/logs/atlasd", "./logs"};

static std::string current_logging_directory;

static bool is_writable_dir(const std::string& dir) {
  struct stat dir_stat;
  const char* cdir = dir.c_str();
  if (stat(cdir, &dir_stat) != 0 || !S_ISDIR(dir_stat.st_mode)) {
    mkdir(cdir, 0777);
  }

  bool error = stat(cdir, &dir_stat) != 0;                        // couldn't even stat it
  error |= !S_ISDIR(dir_stat.st_mode);                            // or not a dir
  error |= S_ISDIR(dir_stat.st_mode) && access(cdir, W_OK) != 0;  // dir, but can't write to it
  return !error;
}

static bool writable_or_missing(const std::string& file) {
  bool exists = access(file.c_str(), F_OK) == 0;
  if (!exists) {
    // missing
    return true;
  }

  // exists, check whether we can write to it
  return access(file.c_str(), W_OK) == 0;
}

static constexpr const char* const kLogFileName = "atlasclient.log";

std::string join_path(const std::string& dir, const char* file_name) noexcept {
  std::ostringstream os;
  if (dir.empty()) {
    os << "./";
  } else {
    os << dir;
    if (dir[dir.length() - 1] != '/') {
      os << "/";
    }
  }
  os << file_name;
  return os.str();
}

static std::string get_logging_directory() {
  for (const auto& log_dir : logging_directories) {
    if (is_writable_dir(log_dir) && writable_or_missing(join_path(log_dir, kLogFileName))) {
      return log_dir;
    }
  }
  return "/tmp";
}

static void fallback_init_for_logger() noexcept {
  auto logger = spdlog::get(kMainLogger);
  if (logger) {
    spdlog::drop(kMainLogger);
  }

  spdlog::create_async_nb<spdlog::sinks::ansicolor_stderr_sink_mt>(kMainLogger);
  current_logging_directory = "";
}

static void initialize_logger(const std::string& log_dir) {
  const auto& file_name = join_path(log_dir, kLogFileName);
  if (writable_or_missing(file_name)) {
    spdlog::set_error_handler(
        [](const std::string& msg) { std::cerr << "Log error: " << msg << "\n"; });
    auto logger_ = spdlog::rotating_logger_mt(kMainLogger, join_path(log_dir, "atlasclient.log"),
                                              1024 * 1024, 8);
    current_logging_directory = log_dir;
    logger_->flush_on(spdlog::level::info);
  } else {
    fallback_init_for_logger();
  }
}

static void initialize() {
  try {
    // initialize our async logging with a queue of 8k entries and 1 background thread
    // to flush it
    spdlog::init_thread_pool(8192, 1);
    initialize_logger(get_logging_directory());
  } catch (const spdlog::spdlog_ex& ex) {
    std::cerr << "Log initialization failed: " << ex.what() << "\n";
  }
}

static std::mutex logger_mutex;
std::shared_ptr<spdlog::logger> Logger() noexcept {
  std::lock_guard<std::mutex> lock(logger_mutex);
  auto logger = spdlog::get(kMainLogger);
  if (logger) {
    return logger;
  }

  initialize();
  return spdlog::get(kMainLogger);
}

static spdlog::level::level_enum level_from_int(int l) {
  if (l < 0 || l > static_cast<int>(spdlog::level::off)) {
    l = 2;  // default
  }
  return static_cast<spdlog::level::level_enum>(l);
}

void SetLoggingDirs(const std::vector<std::string>& dirs) noexcept {
  std::lock_guard<std::mutex> lock(logger_mutex);
  auto logger = spdlog::get(kMainLogger);
  if (logger) {
    spdlog::drop(kMainLogger);
    current_logging_directory = "";
  }

  if (!dirs.empty()) {
    logging_directories.assign(dirs.begin(), dirs.end());
  }
  initialize();
}

void SetLoggingLevel(int level) noexcept { Logger()->set_level(level_from_int(level)); }

void UseConsoleLogger() noexcept {
  std::lock_guard<std::mutex> lock(logger_mutex);
  auto logger = spdlog::get(kMainLogger);
  if (logger) {
    spdlog::drop(kMainLogger);
  }

  logger = spdlog::create_async_nb<spdlog::sinks::ansicolor_stdout_sink_mt>(kMainLogger);
  logger->set_level(spdlog::level::debug);
  current_logging_directory = "";
}

std::string GetLoggingDir() noexcept { return current_logging_directory; }

}  // namespace atlasagent
