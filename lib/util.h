#pragma once

#include <cstdio>
#include <string>
#include <unordered_map>
#include <vector>
#include "files.h"
#include <spectator/id.h>

namespace atlasagent {

StdIoFile open_file(const std::string& prefix, const char* name);

int64_t read_num_from_file(const std::string& prefix, const char* fn);

void parse_kv_from_file(const std::string& prefix, const char* fn,
                        std::unordered_map<std::string, int64_t>* stats);

void split(const char* line, const std::function<bool(int)>& is_sep,
           std::vector<std::string>* fields) noexcept;

bool starts_with(const char* line, const char* prefix) noexcept;

// Execute cmd using the shell, and return its output as a string
std::string read_output_string(const char* cmd, int timeout_millis = 1000);

// Execute cmd using the shell and return its output as a vector of lines
std::vector<std::string> read_output_lines(const char* cmd, int timeout_millis = 1000);

// determine whether the program passed is available
bool can_execute(const std::string& program);

// parse a string of the form key=val,key2=val2 into spectator Tags
spectator::Tags parse_tags(const char* s);

}  // namespace atlasagent
