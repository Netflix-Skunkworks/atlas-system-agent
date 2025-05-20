#pragma once

#include <lib/files/src/files.h>
#include <lib/tagging/src/tagging_registry.h>
#include <lib/util/src/util.h>
#include <sys/stat.h>

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

template <typename Reg = TaggingRegistry>
class CpuFreq {
 public:
  explicit CpuFreq(Reg* registry, std::string path_prefix = "/sys/devices/system/cpu/cpufreq") noexcept;

  void Stats() noexcept;

 private:
  Reg* registry_;
  std::string path_prefix_;
  bool enabled_;

  typename Reg::dist_summary_ptr min_ds_;
  typename Reg::dist_summary_ptr max_ds_;
  typename Reg::dist_summary_ptr cur_ds_;
};
}  // namespace atlasagent
