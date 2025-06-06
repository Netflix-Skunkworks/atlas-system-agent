#pragma once

#include <cstdio>
#include <string>
#include <unordered_map>
#include <optional>
#include <vector>
#include <lib/files/src/files.h>
#include <lib/spectator/id.h>

struct UtilConstants {
  static constexpr auto ServiceActiveCmd{"systemctl is-active --quiet"};
};

namespace atlasagent {

StdIoFile open_file(const std::string& prefix, const char* name);

std::vector<std::vector<std::string>> read_lines_fields(const std::string& prefix, const char* fn);

int64_t read_num_from_file(const std::string& prefix, const char* fn);

std::vector<int64_t> read_num_vector_from_file(const std::string& prefix, const char* fn);

void parse_kv_from_file(const std::string& prefix, const char* fn,
                        std::unordered_map<std::string, int64_t>* stats);

bool starts_with(const char* line, const char* prefix) noexcept;

// Execute cmd using the shell, and return its output as a string
std::string read_output_string(const char* cmd, int timeout_millis = 1000);

// Execute cmd using the shell and return its output as a vector of lines
std::vector<std::string> read_output_lines(const char* cmd, int timeout_millis = 1000);

// determine whether the program passed is available
bool can_execute(const std::string& program);

// parse a string of the form key=val,key2=val2 into spectator Tags
spectator::Tags parse_tags(const char* s);

// construct a spectator id with extra tags - intended for use with network interface metrics
inline spectator::IdPtr id_for(const char* name, const char* iface, const char* idStr,
                               const spectator::Tags& extra) noexcept {
  spectator::Tags tags{extra};
  tags.add("iface", iface);
  if (idStr != nullptr) {
    tags.add("id", idStr);
  }
  return spectator::Id::of(name, tags);
}

bool is_service_running(const char* serviceName);

bool is_file_present(const char* fileName);

// read a file line by line into a vector
std::optional<std::vector<std::string>> read_file(const std::string& filePath);

}  // namespace atlasagent