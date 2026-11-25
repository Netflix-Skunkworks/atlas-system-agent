#pragma once

#include <string>

#include <thirdparty/spectator-cpp/spectator/registry.h>

namespace atlasagent
{

class CpuFreq
{
   public:
    explicit CpuFreq(spectator::Registry* registry,
                     std::string path_prefix = "/sys/devices/system/cpu/cpufreq") noexcept;

    void Stats() const noexcept;

   private:
    std::string path_prefix_;
    spectator::DistributionSummary min_freq_;
    spectator::DistributionSummary max_freq_;
    spectator::DistributionSummary cur_freq_;
};
}  // namespace atlasagent
