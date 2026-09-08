#pragma once

#include <thirdparty/spectator-cpp/spectator/registry.h>
#include <absl/container/flat_hash_map.h>
#include <absl/time/clock.h>
#include <optional>
#include <unordered_map>

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
    // The cgroup.cpu.weight gauge emission, split out of CpuUtilizationV2 (which calls it first)
    // purely so it has its own name and standalone test. CpuStats() reaches it only indirectly,
    // via CpuUtilizationV2().
    void CpuWeight() noexcept;
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

    // Folded in from the free function of the same name so it can reach registry_ and
    // io_previous_stats_ directly. atlasagent::IOStats must stay qualified: the member function
    // IOStats() hides that struct name inside the class, even across entity kinds.
    void UpdateIOMetrics(const std::unordered_map<std::string, atlasagent::IOStats>& ioStats, const std::unordered_map<std::string, IOThrottle>& ioThrottles);

    Registry* registry_;

    // Per-instance delta-tracking state, previously function-local `static` variables (and, for
    // I/O, a file-scope `static` map). Process-wide state in a class that can be instantiated more
    // than once: harmless while each agent builds exactly one CGroup, but a second instance would
    // silently consume the first's baselines and publish deltas against the wrong cgroup.
    int64_t prev_throttled_time_ = -1;           // CpuThrottleV2
    int64_t prev_proc_time_ = -1;                // CpuTimeV2
    int64_t prev_sys_usage_ = -1;                // CpuTimeV2
    int64_t prev_user_usage_ = -1;               // CpuTimeV2
    absl::Time capacity_last_updated_;           // CpuProcessingCapacity (default == UnixEpoch())
    absl::Time utilization_last_updated_;        // CpuUtilizationV2 (default == UnixEpoch())
    int64_t utilization_prev_system_time_ = -1;  // CpuUtilizationV2
    int64_t utilization_prev_user_time_ = -1;    // CpuUtilizationV2
    absl::Time peak_last_updated_;               // CpuPeakUtilizationV2 (default == UnixEpoch())
    int64_t peak_prev_system_time_ = -1;         // CpuPeakUtilizationV2
    int64_t peak_prev_user_time_ = -1;           // CpuPeakUtilizationV2
    std::unordered_map<std::string, atlasagent::IOStats> io_previous_stats_;  // UpdateIOMetrics
};

// TODO: Stop exposing these functions publicly, currently required for testing
std::unordered_map<std::string, IOStats> ParseIOLines(const std::vector<std::vector<std::string>>& lines, const std::unordered_map<std::string, std::string>& devMap);
std::unordered_map<std::string, IOThrottle> ParseIOThrottleLines(const std::vector<std::vector<std::string>>& lines);

}  // namespace atlasagent
