#include "cpufreq.h"
#include "files.h"
#include "util.h"

#include <sys/stat.h>

namespace atlasagent {

namespace detail {
bool is_directory(const std::string& directory) {
  struct stat st;
  if (stat(directory.c_str(), &st) != 0) {
    return false;
  }

  return st.st_mode & S_IFDIR;
}
}  // namespace detail

CpuFreq::CpuFreq(spectator::Registry* registry, std::string path_prefix) noexcept
    : registry_{registry},
      path_prefix_{std::move(path_prefix)},
      enabled_{detail::is_directory(path_prefix_)},
      min_ds_{registry_->GetDistributionSummary("sys.minCoreFrequency")},
      max_ds_{registry_->GetDistributionSummary("sys.maxCoreFrequency")},
      cur_ds_{registry_->GetDistributionSummary("sys.curCoreFrequency")} {}

void CpuFreq::Stats() noexcept {
  if (!enabled_) return;

  DirHandle dh{path_prefix_.c_str()};

  struct dirent* direntry;
  // each logical cpu provides a directory with a name like policy%d
  while ((direntry = readdir(dh)) != nullptr) {
    if (direntry->d_name[0] == '.') continue;

    auto prefix = fmt::format("{}/{}", path_prefix_, direntry->d_name);
    auto min = static_cast<double>(read_num_from_file(prefix, "scaling_min_freq"));
    if (min < 0) continue;
    auto max = static_cast<double>(read_num_from_file(prefix, "scaling_max_freq"));
    if (max < 0) continue;
    auto cur = static_cast<double>(read_num_from_file(prefix, "scaling_cur_freq"));
    if (cur < 0) continue;

    min_ds_->Record(min);
    max_ds_->Record(max);
    cur_ds_->Record(cur);
  };
}

}  // namespace atlasagent
