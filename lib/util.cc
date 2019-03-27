#include "util.h"
#include "logger.h"
#include <cinttypes>
#include <sstream>

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

}  // namespace atlasagent
