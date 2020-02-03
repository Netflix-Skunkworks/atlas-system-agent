#include "util.h"
#include "logger.h"
#include <cinttypes>
#include <sstream>
#include <unistd.h>

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

std::string read_output_string(const char* cmd) {
  StdPipe fp{cmd};
  std::string result;
  static constexpr int kBufSize = 4096;
  char buf[kBufSize] = {0};
  while ((std::fgets(buf, kBufSize, fp)) != nullptr) {
    result += buf;
  }
  return result;
}

std::vector<std::string> read_output_lines(const char* cmd) {
  StdPipe fp{cmd};
  std::vector<std::string> result;
  std::string line;
  static constexpr int kBufSize = 4096;
  char buf[kBufSize] = {0};
  char* st;
  while ((st = std::fgets(buf, kBufSize, fp)) != nullptr) {
    result.emplace_back(buf);
  }
  return result;
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
      tags.add(key, value);
    }
  }
  return tags;
}

}  // namespace atlasagent
