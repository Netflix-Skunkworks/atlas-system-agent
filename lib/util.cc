#include "util.h"
#include "logger.h"
#include <cinttypes>
#include <sstream>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>

extern char** environ;

namespace atlasagent {

StdIoFile open_file(const std::string& prefix, const char* name) {
  auto resolved_path = fmt::format("{}/{}", prefix, name);
  return StdIoFile(resolved_path.c_str());
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

void parse_kv_from_file(const std::string& prefix, const char* fn,
                        std::unordered_map<std::string, int64_t>* stats) {
  auto fp = open_file(prefix, fn);
  if (fp == nullptr) {
    return;
  }

  char buffer[1024];
  char key[1024];
  int64_t value;
  while (fgets(buffer, sizeof key, fp) != nullptr) {
    if (sscanf(buffer, "%s %" PRId64, key, &value) == 2) {
      std::string str_key{key};
      (*stats)[str_key] = value;
    }
  }
}

void split(const char* line, const std::function<bool(int)>& is_sep,
           std::vector<std::string>* fields) noexcept {
  const char* p = line;
  const char* end = line + strlen(line);
  char field[256];
  constexpr size_t max = sizeof field - 1;
  size_t i = 0;

  while (p < end && is_sep(*p)) {
    ++p;
  }

  while (p < end) {
    while (!is_sep(*p) && i < max) {
      field[i] = *p;
      ++i;
      ++p;
    }
    while (is_sep(*p)) {
      ++p;
    }
    field[i] = '\0';
    fields->emplace_back(field);
    i = 0;
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
  auto output_str = read_output_string(cmd, timeout_millis);
  std::vector<std::string> lines{};
  split(
      output_str.c_str(), [](int ch) { return ch == '\n'; }, &lines);
  return lines;
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

  auto is_colon = [](int c) { return c == ':'; };
  std::vector<std::string> dirs{};
  split(path, is_colon, &dirs);
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

spectator::Tags parse_tags(const char* s) {
  spectator::Tags tags{};
  std::vector<std::string> fields{};
  split(
      s, [](int ch) { return ch == ',' || ch == ' '; }, &fields);
  for (const auto& f : fields) {
    auto pos = f.find('=');
    if (pos != std::string::npos) {
      auto key = f.substr(0, pos);
      auto value = f.substr(pos + 1, f.length());
      if (!key.empty() && !value.empty()) {
        tags.add(key, value);
      }
    }
  }
  return tags;
}

}  // namespace atlasagent
