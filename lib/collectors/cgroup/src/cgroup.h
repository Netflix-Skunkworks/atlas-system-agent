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
    // Pod-scoped counterpart to CpuStats(). sys.cpu.*/titus.cpu.* are not node/Titus-only --
    // Titus already emits them per-container via CpuStats(), since titus-agent is one process
    // per container and gets its container identity for free from which spectatord instance
    // that container's sidecar talks to. Once SetExtraTags() gives a pod's CGroup instance a
    // disambiguating tag (which MergeTags() then applies at every CpuUtilizationV2 /
    // CpuPeakUtilizationV2 call site), those same metrics are just as meaningful per-pod. So
    // PodCpuStats() intentionally delegates to CpuStats() for full Titus-name parity for pods
    // too; it remains a separate method only so PodMonitor's call site stays self-documenting.
    void PodCpuStats(const bool fiveSecondMetricsEnabled, const bool sixtySecondMetricsEnabled);
    // The cgroup.cpu.weight gauge emission, extracted out of CpuUtilizationV2 (which now calls
    // this as its first action) purely so it has its own name and its own standalone test;
    // reached from PodCpuStats() indirectly via CpuStats() -> CpuUtilizationV2() -> CpuWeight(),
    // not called directly by either.
    void CpuWeight() noexcept;
    void IOStats();
    void MemoryStatsV2() noexcept;
    void MemoryStatsStdV2() noexcept;
    void NetworkStats() noexcept;
    void PressureStall() noexcept;
    void SetPrefix(std::string new_prefix) noexcept { path_prefix_ = std::move(new_prefix); }

    // Extra tags merged into every metric emitted by the pod-scoped surface (CpuThrottleV2,
    // CpuTimeV2, CpuProcessingCapacity, CpuWeight, the node-scoped lines of CpuUtilizationV2 /
    // CpuPeakUtilizationV2, UpdateIOMetrics, MemoryStatsV2, MemoryStatsStdV2). Titus code never
    // calls this, so extra_tags_ stays empty and Titus's wire output is byte-for-byte unaffected.
    void SetExtraTags(std::unordered_map<std::string, std::string> tags) noexcept { extra_tags_ = std::move(tags); }

    // Lets a caller (e.g. a per-pod monitor) supply the CPU count directly instead of relying
    // on the TITUS_NUM_CPU environment variable. Titus code never calls this, so GetNumCpu()'s
    // behavior for Titus is identical to today.
    void SetCpuCountOverride(std::optional<double> count) noexcept { cpu_count_override_ = count; }

    // Reads cpu.max and returns quota/period, or std::nullopt when the quota is "max"
    // (unlimited).
    std::optional<double> QuotaCpuCount() const noexcept;

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

    // Folded in from the old file-scope free function of the same name so it can use
    // registry_ and io_previous_stats_ directly instead of taking a Registry* parameter.
    // IOStats must be qualified here: unqualified, it would resolve to the sibling member
    // method IOStats() below (a class member hides an outer-namespace name of the same
    // identifier, even across different entity kinds), not the atlasagent::IOStats struct.
    void UpdateIOMetrics(const std::unordered_map<std::string, atlasagent::IOStats>& ioStats, const std::unordered_map<std::string, IOThrottle>& ioThrottles);

    // Returns local_tags unchanged when extra_tags_ is empty; otherwise the union of
    // extra_tags_ and local_tags, with local_tags winning on key collision.
    std::unordered_map<std::string, std::string> MergeTags(const std::unordered_map<std::string, std::string>& local_tags) const noexcept;

    Registry* registry_;

    // Per-instance delta-tracking state. These were previously function-local `static`
    // variables (and, for I/O, a file-scope `static` map) shared by every CGroup instance in
    // the process -- which made it unsafe to run two instances concurrently, since each
    // instance would clobber the other's baseline reading. Scoping them to the instance lets
    // two independent CGroup objects (e.g. one tracking Titus's cgroup, one tracking a pod's)
    // track their own deltas without interfering with each other.
    int64_t prev_throttled_time_ = -1;           // CpuThrottleV2
    int64_t prev_proc_time_ = -1;                // CpuTimeV2
    int64_t prev_sys_usage_ = -1;                // CpuTimeV2
    int64_t prev_user_usage_ = -1;                // CpuTimeV2
    absl::Time capacity_last_updated_;           // CpuProcessingCapacity (default == UnixEpoch())
    absl::Time utilization_last_updated_;        // CpuUtilizationV2 (default == UnixEpoch())
    int64_t utilization_prev_system_time_ = -1;  // CpuUtilizationV2
    int64_t utilization_prev_user_time_ = -1;    // CpuUtilizationV2
    absl::Time peak_last_updated_;               // CpuPeakUtilizationV2 (default == UnixEpoch())
    int64_t peak_prev_system_time_ = -1;         // CpuPeakUtilizationV2
    int64_t peak_prev_user_time_ = -1;           // CpuPeakUtilizationV2
    std::unordered_map<std::string, atlasagent::IOStats> io_previous_stats_;  // UpdateIOMetrics

    std::unordered_map<std::string, std::string> extra_tags_;
    std::optional<double> cpu_count_override_;
};

// TODO: Stop exposing these functions publicly, currently required for testing
std::unordered_map<std::string, IOStats> ParseIOLines(const std::vector<std::vector<std::string>>& lines, const std::unordered_map<std::string, std::string>& devMap);
std::unordered_map<std::string, IOThrottle> ParseIOThrottleLines(const std::vector<std::vector<std::string>>& lines);

}  // namespace atlasagent
