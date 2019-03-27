#pragma once

#include <cstdio>
#include <string>
#include <unordered_map>
#include <vector>
#include "files.h"

namespace atlasagent {

StdIoFile open_file(const std::string& prefix, const char* name);

int64_t read_num_from_file(const std::string& prefix, const char* fn);

void parse_kv_from_file(const std::string& prefix, const char* fn,
                        std::unordered_map<std::string, int64_t>* stats);

void split(const char* line, const std::function<bool(int)>& is_sep,
           std::vector<std::string>* fields) noexcept;

bool starts_with(const char* line, const char* prefix) noexcept;

// Execute cmd using the shell, and return its output as a string
std::string read_output_string(const char* cmd);

// Execute cmd using the shell and return its output as a vector of lines
std::vector<std::string> read_output_lines(const char* cmd);

}  // namespace atlasagent
