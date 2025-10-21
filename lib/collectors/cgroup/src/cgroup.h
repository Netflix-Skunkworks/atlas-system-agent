#pragma once

#include <thirdparty/spectator-cpp/spectator/registry.h>
#include <absl/container/flat_hash_map.h>
#include <absl/time/clock.h>
#include <optional>

namespace atlasagent
{


struct IOStats
{
    std::string deviceName{"unknown"};
    std::string majorMinor;
    std::optional<double> rBytes = std::nullopt;
    std::optional<double> wBytes = std::nullopt;
    std::optional<double> rOperations = std::nullopt;
    std::optional<double> wOperations = std::nullopt;
    std::optional<double> dBytes = std::nullopt;
    std::optional<double> dOperations = std::nullopt;
};

struct IOThrottle
{
    std::string device;
    std::optional<double> rBps = std::nullopt;
    std::optional<double> wBps = std::nullopt;
    std::optional<double> rIops = std::nullopt;
    std::optional<double> wIops = std::nullopt;
};

class CGroup
{
   public:
    explicit CGroup(Registry* registry, std::string path_prefix = "/sys/fs/cgroup") noexcept
        : path_prefix_(std::move(path_prefix)), registry_(registry)
    {
    }

    void CpuStats(const bool fiveSecondMetricsEnabled, const bool sixtySecondMetricsEnabled);
    void IOStats();
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
    void CpuPeakUtilizationV2(const absl::Time& now, const std::unordered_map<std::string, int64_t>& stats, const double cpuCount) noexcept;
    void CpuProcessingCapacity(const absl::Time& now, const double cpuCount, const absl::Duration& interval) noexcept;
    
   private:
    double GetAvailCpuTime(const double delta_t, const double cpuCount) noexcept;

    Registry* registry_;
};

// TODO: Stop exposing these functions publicly, currently required for testing
std::unordered_map<std::string, IOStats> ParseIOLines(const std::vector<std::vector<std::string>>& lines, const std::unordered_map<std::string, std::string>& devMap);
std::unordered_map<std::string, IOThrottle> ParseIOThrottleLines(const std::vector<std::vector<std::string>>& lines);

}  // namespace atlasagent
