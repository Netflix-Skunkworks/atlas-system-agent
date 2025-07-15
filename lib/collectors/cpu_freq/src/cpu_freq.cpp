#include "cpu_freq.h"

namespace atlasagent {

CpuFreq::CpuFreq(Registry* registry, std::string path_prefix) noexcept
    : registry_{registry},
      path_prefix_{std::move(path_prefix)},
      enabled_{detail::is_directory(path_prefix_)} {}

void CpuFreq::Stats() noexcept {
    if (!enabled_) return;

    DirHandle dh{path_prefix_.c_str()};

    struct dirent* direntry;
    // each logical cpu provides a directory with a name like policy%d
    while ((direntry = readdir(dh)) != nullptr) {
      if (direntry->d_name[0] == '.') continue;
      std::cout << "Directory entry: " << direntry->d_name << std::endl;
      auto prefix = fmt::format("{}/{}", path_prefix_, direntry->d_name);
      auto min = static_cast<double>(read_num_from_file(prefix, "scaling_min_freq"));
      if (min < 0) continue;
      auto max = static_cast<double>(read_num_from_file(prefix, "scaling_max_freq"));
      if (max < 0) continue;
      auto cur = static_cast<double>(read_num_from_file(prefix, "scaling_cur_freq"));
      if (cur < 0) continue;


      registry_->CreateDistributionSummary("sys.minCoreFrequency").Record(min);
      registry_->CreateDistributionSummary("sys.maxCoreFrequency").Record(max);
      registry_->CreateDistributionSummary("sys.curCoreFrequency").Record(cur);
    }
}

} // namespace atlasagent