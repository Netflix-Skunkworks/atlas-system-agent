#pragma once

#include <thirdparty/spectator-cpp/spectator/registry.h>
#include <absl/time/clock.h>

namespace atlasagent
{

class CGroup
{
   public:
    explicit CGroup(Registry* registry, std::string path_prefix = "/sys/fs/cgroup") noexcept
        : path_prefix_(std::move(path_prefix)), registry_(registry)
    {
    }

    void CpuStats(const bool fiveSecondMetricsEnabled, const bool sixtySecondMetricsEnabled);
    void MemoryStatsV2() noexcept;
    void MemoryStatsStdV2() noexcept;
    void NetworkStats() noexcept;
    void PressureStall() noexcept;
    void SetPrefix(std::string new_prefix) noexcept { path_prefix_ = std::move(new_prefix); }

   protected:
    // For testing access
    std::string path_prefix_;
    double GetNumCpu() noexcept;
    void CpuThrottleV2(const std::unordered_map<std::string, int64_t>& stats) noexcept;
    void CpuTimeV2(const std::unordered_map<std::string, int64_t>& stats) noexcept;
    void CpuUtilizationV2(const absl::Time& now, const double cpuCount, const std::unordered_map<std::string, int64_t>& stats, const absl::Duration& interval) noexcept;
    void CpuPeakUtilizationV2(const absl::Time& now, const std::unordered_map<std::string, int64_t>& stats,
                              const double cpuCount) noexcept;
    void CpuProcessingCapacity(const absl::Time& now, const double cpuCount, const absl::Duration& interval) noexcept;
    
   private:
    double GetAvailCpuTime(const double delta_t, const double cpuCount) noexcept;

    Registry* registry_;
};

}  // namespace atlasagent
