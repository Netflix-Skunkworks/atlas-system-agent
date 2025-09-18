#include "cgroup.h"
#include <lib/util/src/util.h>
#include <cstdlib>
#include <map>
#include <unistd.h>

namespace atlasagent
{

constexpr auto MICROS = 1000 * 1000.0;

void CGroup::NetworkStats() noexcept
{
    auto megabits = std::getenv("TITUS_NUM_NETWORK_BANDWIDTH");

    if (megabits != nullptr)
    {
        auto n = strtol(megabits, nullptr, 10);
        if (n > 0)
        {
            auto bytes = n * 125000.0;  // 1 megabit = 1,000,000 bits / 8 = 125,000 bytes
            registry_->CreateGauge("cgroup.net.bandwidthBytes").Set(bytes);
        }
    }
}

void CGroup::PressureStall() noexcept
{
    auto lines = read_lines_fields(path_prefix_, "cpu.pressure");

    if (lines.size() == 2)
    {
        auto usecs = std::strtoul(lines[0][4].substr(6).c_str(), nullptr, 10);
        registry_->CreateMonotonicCounter("sys.pressure.some", {{"id", "cpu"}}).Set(usecs / MICROS);

        usecs = std::strtoul(lines[1][4].substr(6).c_str(), nullptr, 10);
        registry_->CreateMonotonicCounter("sys.pressure.full", {{"id", "cpu"}}).Set(usecs / MICROS);
    }

    lines = read_lines_fields(path_prefix_, "io.pressure");
    if (lines.size() == 2)
    {
        auto usecs = std::strtoul(lines[0][4].substr(6).c_str(), nullptr, 10);
        registry_->CreateMonotonicCounter("sys.pressure.some", {{"id", "io"}}).Set(usecs / MICROS);

        usecs = std::strtoul(lines[1][4].substr(6).c_str(), nullptr, 10);
        registry_->CreateMonotonicCounter("sys.pressure.full", {{"id", "io"}}).Set(usecs / MICROS);
    }

    lines = read_lines_fields(path_prefix_, "memory.pressure");
    if (lines.size() == 2)
    {
        auto usecs = std::strtoul(lines[0][4].substr(6).c_str(), nullptr, 10);
        registry_->CreateMonotonicCounter("sys.pressure.some", {{"id", "memory"}}).Set(usecs / MICROS);

        usecs = std::strtoul(lines[1][4].substr(6).c_str(), nullptr, 10);
        registry_->CreateMonotonicCounter("sys.pressure.full", {{"id", "memory"}}).Set(usecs / MICROS);
    }
}

void CGroup::CpuThrottleV2(const std::unordered_map<std::string, int64_t>& stats) noexcept
{
    static auto prev_throttled_time = static_cast<int64_t>(-1);
    auto cur_throttled_time = stats.at("throttled_usec");
    if (prev_throttled_time >= 0)
    {
        auto seconds = (cur_throttled_time - prev_throttled_time) / MICROS;
        registry_->CreateCounter("cgroup.cpu.throttledTime").Increment(seconds);
    }
    prev_throttled_time = cur_throttled_time;

    registry_->CreateMonotonicCounter("cgroup.cpu.numThrottled").Set(stats.at("nr_throttled"));
}

void CGroup::CpuTimeV2(const std::unordered_map<std::string, int64_t>& stats) noexcept
{
    static auto prev_proc_time = static_cast<int64_t>(-1);
    if (prev_proc_time >= 0)
    {
        auto secs = (stats.at("usage_usec") - prev_proc_time) / MICROS;
        registry_->CreateCounter("cgroup.cpu.processingTime").Increment(secs);
    }
    prev_proc_time = stats.at("usage_usec");

    static auto prev_sys_usage = static_cast<int64_t>(-1);
    if (prev_sys_usage >= 0)
    {
        auto secs = (stats.at("system_usec") - prev_sys_usage) / MICROS;
        registry_->CreateCounter("cgroup.cpu.usageTime", {{"id", "system"}}).Increment(secs);
    }
    prev_sys_usage = stats.at("system_usec");

    static auto prev_user_usage = static_cast<int64_t>(-1);
    if (prev_user_usage >= 0)
    {
        auto secs = (stats.at("user_usec") - prev_user_usage) / MICROS;
        registry_->CreateCounter("cgroup.cpu.usageTime", {{"id", "user"}}).Increment(secs);
    }
    prev_user_usage = stats.at("user_usec");
}

double CGroup::GetAvailCpuTime(const double delta_t, const double cpuCount) noexcept
{
    auto cpu_max = read_num_vector_from_file(path_prefix_, "cpu.max");
    auto cfs_period = cpu_max[1];
    auto cfs_quota = cfs_period * cpuCount;
    return (delta_t / cfs_period) * cfs_quota;
}

double CGroup::GetNumCpu() noexcept
{
    auto env_num_cpu = std::getenv("TITUS_NUM_CPU");
    auto cpuCount = 0.0;
    if (env_num_cpu != nullptr)
    {
        cpuCount = strtod(env_num_cpu, nullptr);
    }
    return cpuCount;
}

void CGroup::CpuProcessingTime(const absl::Time& now, const double cpuCount, const absl::Duration& interval) noexcept
{
    static absl::Time last_updated;
    if (last_updated == absl::UnixEpoch())
    {
        last_updated = now - interval;
    }
    auto delta_t = absl::ToDoubleSeconds(now - last_updated);
    last_updated = now;
    registry_->CreateCounter("cgroup.cpu.processingCapacity").Increment(delta_t * cpuCount);
}

void CGroup::CpuUtilizationV2(const absl::Time& now, const double cpuCount, const absl::Duration& interval) noexcept
{
    static absl::Time last_updated;
    if (last_updated == absl::UnixEpoch())
    {
        last_updated = now - interval;
    }
    auto delta_t = absl::ToDoubleSeconds(now - last_updated);
    last_updated = now;

    auto weight = read_num_from_file(path_prefix_, "cpu.weight");
    if (weight >= 0)
    {
        registry_->CreateGauge("cgroup.cpu.weight").Set(weight);
    }

    auto avail_cpu_time = GetAvailCpuTime(delta_t, cpuCount);
    registry_->CreateGauge("sys.cpu.numProcessors").Set(cpuCount);
    registry_->CreateGauge("titus.cpu.requested").Set(cpuCount);

    std::unordered_map<std::string, int64_t> stats;
    parse_kv_from_file(path_prefix_, "cpu.stat", &stats);

    static auto prev_system_time = static_cast<int64_t>(-1);
    if (prev_system_time >= 0)
    {
        auto secs = (stats["system_usec"] - prev_system_time) / MICROS;
        registry_->CreateGauge("sys.cpu.utilization", {{"id", "system"}}).Set((secs / avail_cpu_time) * 100);
    }
    prev_system_time = stats["system_usec"];

    static auto prev_user_time = static_cast<int64_t>(-1);
    if (prev_user_time >= 0)
    {
        auto secs = (stats["user_usec"] - prev_user_time) / MICROS;
        registry_->CreateGauge("sys.cpu.utilization", {{"id", "user"}}).Set((secs / avail_cpu_time) * 100);
    }
    prev_user_time = stats["user_usec"];
}

void CGroup::CpuPeakUtilizationV2(const absl::Time& now, const std::unordered_map<std::string, int64_t>& stats,
                                  const double cpuCount) noexcept
{
    static absl::Time last_updated;
    auto delta_t = absl::ToDoubleSeconds(now - last_updated);
    last_updated = now;

    auto avail_cpu_time = GetAvailCpuTime(delta_t, cpuCount);

    static auto prev_system_time = static_cast<int64_t>(-1);
    if (prev_system_time >= 0)
    {
        auto secs = (stats.at("system_usec") - prev_system_time) / MICROS;
        registry_->CreateMaxGauge("sys.cpu.peakUtilization", {{"id", "system"}}).Set((secs / avail_cpu_time) * 100);
    }
    prev_system_time = stats.at("system_usec");

    static auto prev_user_time = static_cast<int64_t>(-1);
    if (prev_user_time >= 0)
    {
        auto secs = (stats.at("user_usec") - prev_user_time) / MICROS;
        registry_->CreateMaxGauge("sys.cpu.peakUtilization", {{"id", "user"}}).Set((secs / avail_cpu_time) * 100);
    }
    prev_user_time = stats.at("user_usec");
}

void CGroup::CpuStats(const bool fiveSecondMetricsEnabled, const bool sixtySecondMetricsEnabled)
{
    std::unordered_map<std::string, int64_t> stats;
    parse_kv_from_file(path_prefix_, "cpu.stat", &stats);
    auto cpuCount = GetNumCpu();

    // Collect 60 second metrics if enabled
    if (sixtySecondMetricsEnabled)
    {
        CpuThrottleV2(stats);
        CpuUtilizationV2(absl::Now(), cpuCount, absl::Seconds(60));
    }

    // Collect 5 second metrics if enabled
    if (fiveSecondMetricsEnabled)
    {
        CpuTimeV2(stats);
        CpuProcessingTime(absl::Now(), cpuCount, absl::Seconds(5));
    }

    // Always collect peak stats (called every 1 second)
    CpuPeakUtilizationV2(absl::Now(), stats, cpuCount);
}

void CGroup::MemoryStatsV2() noexcept
{
    auto usage_bytes = read_num_from_file(path_prefix_, "memory.current");
    if (usage_bytes >= 0)
    {
        registry_->CreateGauge("cgroup.mem.used").Set(usage_bytes);
    }

    auto limit_bytes = read_num_from_file(path_prefix_, "memory.max");
    if (limit_bytes >= 0)
    {
        registry_->CreateGauge("cgroup.mem.limit").Set(limit_bytes);
    }

    std::unordered_map<std::string, int64_t> events;
    parse_kv_from_file(path_prefix_, "memory.events", &events);
    auto mem_fail = events["max"];
    if (mem_fail >= 0)
    {
        registry_->CreateMonotonicCounter("cgroup.mem.failures").Set(mem_fail);
    }

    // kmem_stats not available for v2

    std::unordered_map<std::string, int64_t> stats;
    parse_kv_from_file(path_prefix_, "memory.stat", &stats);

    registry_->CreateGauge("cgroup.mem.processUsage", {{"id", "cache"}}).Set(stats["file"]);

    registry_->CreateGauge("cgroup.mem.processUsage", {{"id", "rss"}}).Set(stats["anon"]);

    registry_->CreateGauge("cgroup.mem.processUsage", {{"id", "rss_huge"}}).Set(stats["anon_thp"]);

    registry_->CreateGauge("cgroup.mem.processUsage", {{"id", "mapped_file"}}).Set(stats["file_mapped"]);

    registry_->CreateMonotonicCounter("cgroup.mem.pageFaults", {{"id", "minor"}}).Set(stats["pgfault"]);

    registry_->CreateMonotonicCounter("cgroup.mem.pageFaults", {{"id", "major"}}).Set(stats["pgmajfault"]);
}

void CGroup::MemoryStatsStdV2() noexcept
{
    auto mem_limit = read_num_from_file(path_prefix_, "memory.max");
    auto mem_usage = read_num_from_file(path_prefix_, "memory.current");
    auto memsw_limit = read_num_from_file(path_prefix_, "memory.swap.max");
    auto memsw_usage = read_num_from_file(path_prefix_, "memory.swap.current");

    std::unordered_map<std::string, int64_t> stats;
    parse_kv_from_file(path_prefix_, "memory.stat", &stats);

    auto cache = stats["file"];
    registry_->CreateGauge("mem.cached").Set(cache);

    registry_->CreateGauge("mem.shared").Set(stats["shmem"]);

    if (mem_limit >= 0 && mem_usage >= 0)
    {
        registry_->CreateGauge("mem.availReal").Set(mem_limit - mem_usage + cache);
        registry_->CreateGauge("mem.freeReal").Set(mem_limit - mem_usage);
        registry_->CreateGauge("mem.totalReal").Set(mem_limit);
    }

    if (memsw_limit >= 0 && memsw_usage >= 0)
    {
        registry_->CreateGauge("mem.availSwap").Set(memsw_limit - memsw_usage);
        registry_->CreateGauge("mem.totalSwap").Set(memsw_limit);
    }

    if (mem_limit >= 0 && mem_usage >= 0 && memsw_limit >= 0 && memsw_usage >= 0)
    {
        registry_->CreateGauge("mem.totalFree").Set((mem_limit - mem_usage) + (memsw_limit - memsw_usage) + cache);
    }
}

}  // namespace atlasagent