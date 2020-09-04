#pragma once

#include <spectator/registry.h>

namespace atlasagent {
class CpuFreq {
 public:
  explicit CpuFreq(spectator::Registry* registry,
                   std::string path_prefix = "/sys/devices/system/cpu/cpufreq") noexcept;

  void Stats() noexcept;

 private:
  spectator::Registry* registry_;
  std::string path_prefix_;
  bool enabled_;

  std::shared_ptr<spectator::DistributionSummary> min_ds_;
  std::shared_ptr<spectator::DistributionSummary> max_ds_;
  std::shared_ptr<spectator::DistributionSummary> cur_ds_;
};
}  // namespace atlasagent