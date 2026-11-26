#include "cpu_freq.h"

#include <lib/util/src/util.h>

#include <filesystem>
#include <utility>

namespace atlasagent
{

CpuFreq::CpuFreq(spectator::Registry* registry, std::string path_prefix) noexcept
    : path_prefix_{std::move(path_prefix)},
      min_freq_{registry->CreateDistributionSummary("sys.minCoreFrequency")},
      max_freq_{registry->CreateDistributionSummary("sys.maxCoreFrequency")},
      cur_freq_{registry->CreateDistributionSummary("sys.curCoreFrequency")}
{
}

void CpuFreq::Stats() const noexcept
{
    // each logical cpu provides a directory with a name like policy%d. Hosts without a cpufreq
    // driver (common on VMs) have no such directory: the error code is set and nothing is logged.
    // The error_code overloads are used because a throw from this noexcept function would terminate.
    std::error_code ec;
    const std::filesystem::directory_iterator end;
    for (std::filesystem::directory_iterator it{path_prefix_, ec}; !ec && it != end; it.increment(ec))
    {
        const auto prefix = it->path().string();
        const auto min = read_num_from_file(prefix, "scaling_min_freq");
        if (min < 0) continue;
        const auto max = read_num_from_file(prefix, "scaling_max_freq");
        if (max < 0) continue;
        const auto cur = read_num_from_file(prefix, "scaling_cur_freq");
        if (cur < 0) continue;

        min_freq_.Record(static_cast<double>(min));
        max_freq_.Record(static_cast<double>(max));
        cur_freq_.Record(static_cast<double>(cur));
    }
}

}  // namespace atlasagent
