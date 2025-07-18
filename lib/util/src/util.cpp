#include "util.h"
#include <lib/logger/src/logger.h>
#include <absl/strings/str_split.h>
#include <cinttypes>
#include <filesystem>
#include <sstream>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>
#include <fstream>

extern char** environ;

namespace atlasagent {

StdIoFile open_file(const std::string& prefix, const char* name) {
  auto resolved_path = fmt::format("{}/{}", prefix, name);
  return StdIoFile(resolved_path.c_str());
}

std::vector<std::vector<std::string>> read_lines_fields(const std::string& prefix, const char* fn) {
  std::vector<std::vector<std::string>> result;
  std::string fp = fmt::format("{}/{}", prefix, fn);
  std::ifstream in(fp);

  if (!in) {
    Logger()->warn("Unable to open {}", fp);
    return result;
  }

  std::string line;
  while (std::getline(in, line)) {
    result.push_back(absl::StrSplit(line, ' ', absl::SkipEmpty()));
  }

  return result;
}

int64_t read_num_from_file(const std::string& prefix, const char* fn) {
  auto fp = open_file(prefix, fn);
  if (fp == nullptr) {
    return -1;
  }
  int64_t n = -1;
  // shouldn't happen
  if (std::fscanf(fp, "%" PRId64, &n) == 0) {
    n = -1;
  }
  return n;
}

std::vector<int64_t> read_num_vector_from_file(const std::string& prefix, const char* fn) {
  std::vector<int64_t> num_vector;
  std::string fp = fmt::format("{}/{}", prefix, fn);
  std::ifstream in(fp);

  if (!in) {
    Logger()->warn("Unable to open {}", fp);
    return num_vector;
  }

  std::string line;
  std::getline(in, line);
  std::vector<std::string> fields = absl::StrSplit(line, ' ', absl::SkipEmpty());

  for (const auto& field : fields) {
    num_vector.push_back(std::strtoul(field.c_str(), nullptr, 10));
  }

  return num_vector;
}

void parse_kv_from_file(const std::string& prefix, const char* fn,
                        std::unordered_map<std::string, int64_t>* stats) {
  auto fp = open_file(prefix, fn);
  if (fp == nullptr) {
    return;
  }

  char buffer[1024];
  char key[1024];
  int64_t value;
  while (fgets(buffer, sizeof buffer, fp) != nullptr) {
    if (sscanf(buffer, "%s %" PRId64, key, &value) == 2) {
      std::string str_key{key};
      (*stats)[str_key] = value;
    }
  }
}

bool starts_with(const char* line, const char* prefix) noexcept {
  auto prefix_len = std::strlen(prefix);
  auto line_len = std::strlen(line);
  if (line_len < prefix_len) {
    return false;
  }

  return std::memcmp(line, prefix, prefix_len) == 0;
}

enum class read_result_t { timeout, error, success };

static read_result_t read_with_timeout(int fd, int timeout_millis, std::string* output) {
  char buf[4096];
  fd_set input_set;
  timeval timeout;

  std::string result;
  while (true) {
    FD_ZERO(&input_set);
    FD_SET(fd, &input_set);

    timeout.tv_sec = timeout_millis / 1000;
    timeout.tv_usec = (timeout_millis % 1000) * 1000L;
    auto ready = select(fd + 1, &input_set, nullptr, nullptr, &timeout);
    if (ready < 0) {
      return read_result_t::error;
    }
    if (ready == 0) {
      return read_result_t::timeout;
    }

    auto read_bytes = read(fd, buf, sizeof buf);
    if (read_bytes < 0) {
      // error
      return read_result_t::error;
    }
    if (read_bytes == 0) {
      // end of file
      *output = std::move(result);
      return read_result_t::success;
    }
    std::string partial{buf, static_cast<std::string::size_type>(read_bytes)};
    result += partial;
  }
}

std::string read_output_string(const char* cmd, int timeout_millis) {
  std::string result;

  int pipe_descriptors[2];
  char* argp[] = {const_cast<char*>("sh"), const_cast<char*>("-c"), nullptr, nullptr};
  if (pipe(pipe_descriptors) < 0) {
    Logger()->warn("Unable to create a pipe: {}", strerror(errno));
    return "";
  }

  int pid = fork();
  switch (pid) {
    case -1:  // error
      close(pipe_descriptors[0]);
      close(pipe_descriptors[1]);
      Logger()->warn("Unable to fork when trying to read output for {}: {}", cmd, strerror(errno));
      return "";
    case 0:  // child
      // close child's input
      close(pipe_descriptors[0]);
      // ensure stdout for the child is the parent's end of the pipe
      if (pipe_descriptors[1] != STDOUT_FILENO) {
        dup2(pipe_descriptors[1], STDOUT_FILENO);
        close(pipe_descriptors[1]);
      }
      argp[2] = const_cast<char*>(cmd);
      execve("/bin/sh", argp, environ);
      // if we couldn't exec the shell just die
      _exit(255);
  }

  // parent

  close(pipe_descriptors[1]);
  auto res = read_with_timeout(pipe_descriptors[0], timeout_millis, &result);
  close(pipe_descriptors[0]);

  if (res != read_result_t::success) {
    std::string err_msg;
    if (res == read_result_t::timeout) {
      Logger()->warn("timeout - killing child (pid={})", pid);
      kill(pid, SIGKILL);
      err_msg = fmt::format("timeout after {}ms", timeout_millis);
    } else {
      err_msg = strerror(errno);
    }
    Logger()->warn("Unable to read output from {}: {}", cmd, err_msg);
  }

  int wait_pid = 0;
  do {
    int pstat;
    wait_pid = waitpid(pid, &pstat, 0);
  } while (wait_pid == -1 && errno == EINTR);

  return result;
}

std::vector<std::string> read_output_lines(const char* cmd, int timeout_millis) {
  // use read whole-string with timeout and then split for simplicity
  return absl::StrSplit(read_output_string(cmd, timeout_millis), '\n', absl::SkipEmpty());
}

inline bool can_execute_full_path(const std::string& program) {
  return access(program.c_str(), X_OK) == 0;
}

bool can_execute(const std::string& program) {
  if (program[0] == '/') {
    return can_execute_full_path(program);
  }

  auto path = std::getenv("PATH");
  // should never happen
  if (path == nullptr) {
    return false;
  }

  std::vector<std::string> dirs = absl::StrSplit(path, ':');
  for (const auto& dir : dirs) {
    auto full_path = fmt::format("{}/{}", dir, program);
    if (can_execute_full_path(full_path)) {
      Logger()->debug("Looking for {} found {}", program, full_path);
      return true;
    }
  }
  Logger()->debug("Could not find {} in {}", program, path);
  return false;
}

std::unordered_map<std::string, std::string> parse_tags(const char* s) {
  std::unordered_map<std::string, std::string> tags{};
  auto fields = absl::StrSplit(s, absl::ByAnyChar(", "));
  for (const auto& f : fields) {
    auto pos = f.find('=');
    if (pos != std::string::npos) {
      std::string key = std::string(f.substr(0, pos));
      std::string value = std::string(f.substr(pos + 1, f.length()));
      if (!key.empty() && !value.empty()) {
        tags[key] = value;
      }
    }
  }
  return tags;
}

bool is_service_running(const char* serviceName) {
  std::string command =
      std::string(UtilConstants::ServiceActiveCmd) + " " + std::string(serviceName);
  int returnCode = system(command.c_str());
  return returnCode == 0;
}

bool is_file_present(const char* fileName) try {
  return std::filesystem::exists(fileName) == true;
} catch (const std::exception& e) {
  atlasagent::Logger()->error("Exception thrown in is_file_present: {}", e.what());
  return false;
}

std::optional<std::vector<std::string>> read_file(const std::string& filePath) try {
  if (is_file_present(filePath.c_str()) == false){
    return std::nullopt;
  }

  std::ifstream file(filePath);
  if (file.is_open() == false) {
    return std::nullopt;
  }

  // Read lines into a vector
  std::vector<std::string> lines{};
  std::string line{};
  while (std::getline(file, line)) {
    lines.push_back(line);
  }
  return lines;
}
catch (const std::exception& e){
  atlasagent::Logger()->error("Exception thrown in read_file: {}", e.what());
  return std::nullopt;
}

}  // namespace atlasagent