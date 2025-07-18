#pragma once

#include <lib/files/src/files.h>
#include <lib/util/src/util.h>
#include <sys/stat.h>

#include <thirdparty/spectator-cpp/spectator/registry.h>

namespace atlasagent {

namespace detail {
inline bool is_directory(const std::string& directory) {
  struct stat st;
  if (stat(directory.c_str(), &st) != 0) {
    return false;
  }

  return st.st_mode & S_IFDIR;
}
}  // namespace detail


class CpuFreq {
 public:
  explicit CpuFreq(Registry* registry, std::string path_prefix = "/sys/devices/system/cpu/cpufreq") noexcept;

  void Stats() noexcept;

 private:
  Registry* registry_;
  std::string path_prefix_;
  bool enabled_;

};
}  // namespace atlasagent
