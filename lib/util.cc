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

  char key[1024];
  int64_t value;
  while (std::fscanf(fp, "%s %" PRId64, key, &value) != EOF) {
    std::string str_key{key};
    (*stats)[str_key] = value;
  }
}

void split(const char* line, std::vector<std::string>* fields) noexcept {
  const char* p = line;
  const char* end = line + strlen(line);
  char field[256];
  constexpr size_t max = sizeof field - 1;
  size_t i = 0;

  while (p < end && isspace(*p)) {
    ++p;
  }

  while (p < end) {
    while (!isspace(*p) && i < max) {
      field[i] = *p;
      ++i;
      ++p;
    }
    while (isspace(*p)) {
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

}  // namespace atlasagent
