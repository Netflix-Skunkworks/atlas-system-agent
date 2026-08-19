#include "cgroup.h"
#include <lib/util/src/util.h>
#include <cstdlib>
#include <charconv>
#include <map>
#include <unordered_set>
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

std::unordered_map<std::string, std::string> CGroup::MergeTags(const std::unordered_map<std::string, std::string>& local_tags) const noexcept
{
    if (extra_tags_.empty())
    {
        return local_tags;
    }

    auto merged = extra_tags_;
    for (const auto& [key, value] : local_tags)
    {
        merged[key] = value;
    }
    return merged;
}

void CGroup::CpuThrottleV2(const std::unordered_map<std::string, int64_t>& stats) noexcept
{
    // stats.at() below would throw std::out_of_range -- and since this function is noexcept,
    // terminate the whole process -- if cpu.stat could not be read this tick (e.g. a pod's
    // cgroup directory was removed between discovery and this call, which parse_kv_from_file()
    // handles by silently leaving `stats` empty rather than throwing). Bail out instead of
    // touching prev_throttled_time_, so a single missing read is skipped cleanly and the next
    // successful read still computes a correct delta from the last real baseline.
    auto throttled_it = stats.find("throttled_usec");
    auto nr_throttled_it = stats.find("nr_throttled");
    if (throttled_it == stats.end() || nr_throttled_it == stats.end())
    {
        return;
    }

    auto cur_throttled_time = throttled_it->second;
    if (prev_throttled_time_ >= 0)
    {
        auto seconds = (cur_throttled_time - prev_throttled_time_) / MICROS;
        registry_->CreateCounter("cgroup.cpu.throttledTime", MergeTags({})).Increment(seconds);
    }
    prev_throttled_time_ = cur_throttled_time;

    registry_->CreateMonotonicCounter("cgroup.cpu.numThrottled", MergeTags({})).Set(nr_throttled_it->second);
}

void CGroup::CpuTimeV2(const std::unordered_map<std::string, int64_t>& stats) noexcept
{
    // See CpuThrottleV2() above: bail out instead of letting .at() throw out of a noexcept
    // function (and terminate the process) when cpu.stat couldn't be read this tick.
    auto usage_it = stats.find("usage_usec");
    auto system_it = stats.find("system_usec");
    auto user_it = stats.find("user_usec");
    if (usage_it == stats.end() || system_it == stats.end() || user_it == stats.end())
    {
        return;
    }

    if (prev_proc_time_ >= 0)
    {
        auto secs = (usage_it->second - prev_proc_time_) / MICROS;
        registry_->CreateCounter("cgroup.cpu.processingTime", MergeTags({})).Increment(secs);
    }
    prev_proc_time_ = usage_it->second;

    if (prev_sys_usage_ >= 0)
    {
        auto secs = (system_it->second - prev_sys_usage_) / MICROS;
        registry_->CreateCounter("cgroup.cpu.usageTime", MergeTags({{"id", "system"}})).Increment(secs);
    }
    prev_sys_usage_ = system_it->second;

    if (prev_user_usage_ >= 0)
    {
        auto secs = (user_it->second - prev_user_usage_) / MICROS;
        registry_->CreateCounter("cgroup.cpu.usageTime", MergeTags({{"id", "user"}})).Increment(secs);
    }
    prev_user_usage_ = user_it->second;
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
    if (cpu_count_override_.has_value())
    {
        return cpu_count_override_.value();
    }

    auto env_num_cpu = std::getenv("TITUS_NUM_CPU");
    auto cpuCount = 0.0;
    if (env_num_cpu != nullptr)
    {
        cpuCount = strtod(env_num_cpu, nullptr);
    }
    return cpuCount;
}

std::optional<double> CGroup::QuotaCpuCount() const noexcept
{
    auto lines = read_lines_fields(path_prefix_, "cpu.max");
    if (lines.empty() || lines[0].size() < 2)
    {
        return std::nullopt;
    }

    const auto& quota_field = lines[0][0];
    if (quota_field == "max")
    {
        // Unlimited quota.
        return std::nullopt;
    }

    auto quota = std::strtod(quota_field.c_str(), nullptr);
    auto period = std::strtod(lines[0][1].c_str(), nullptr);
    if (period <= 0)
    {
        return std::nullopt;
    }

    return quota / period;
}

void CGroup::CpuProcessingCapacity(const absl::Time& now, const double cpuCount, const absl::Duration& interval) noexcept
{
    if (capacity_last_updated_ == absl::UnixEpoch())
    {
        capacity_last_updated_ = now - interval;
    }
    auto delta_t = absl::ToDoubleSeconds(now - capacity_last_updated_);
    capacity_last_updated_ = now;
    registry_->CreateCounter("cgroup.cpu.processingCapacity", MergeTags({})).Increment(delta_t * cpuCount);
}

void CGroup::CpuWeight() noexcept
{
    auto weight = read_num_from_file(path_prefix_, "cpu.weight");
    if (weight >= 0)
    {
        registry_->CreateGauge("cgroup.cpu.weight", MergeTags({})).Set(weight);
    }
}

void CGroup::CpuUtilizationV2(const absl::Time& now, const double cpuCount, const std::unordered_map<std::string, int64_t>& stats, const absl::Duration& interval) noexcept
{
    CpuWeight();

    if (utilization_last_updated_ == absl::UnixEpoch())
    {
        utilization_last_updated_ = now - interval;
    }
    auto delta_t = absl::ToDoubleSeconds(now - utilization_last_updated_);
    utilization_last_updated_ = now;

    auto avail_cpu_time = GetAvailCpuTime(delta_t, cpuCount);
    registry_->CreateGauge("sys.cpu.numProcessors", MergeTags({})).Set(cpuCount);
    registry_->CreateGauge("titus.cpu.requested", MergeTags({})).Set(cpuCount);

    if (utilization_prev_system_time_ >= 0)
    {
        auto secs = (stats.at("system_usec") - utilization_prev_system_time_) / MICROS;
        registry_->CreateGauge("sys.cpu.utilization", MergeTags({{"id", "system"}})).Set((secs / avail_cpu_time) * 100);
    }
    utilization_prev_system_time_ = stats.at("system_usec");

    if (utilization_prev_user_time_ >= 0)
    {
        auto secs = (stats.at("user_usec") - utilization_prev_user_time_) / MICROS;
        registry_->CreateGauge("sys.cpu.utilization", MergeTags({{"id", "user"}})).Set((secs / avail_cpu_time) * 100);
    }
    utilization_prev_user_time_ = stats.at("user_usec");
}

void CGroup::CpuPeakUtilizationV2(const absl::Time& now, const std::unordered_map<std::string, int64_t>& stats, const double cpuCount) noexcept
{
    auto delta_t = absl::ToDoubleSeconds(now - peak_last_updated_);
    peak_last_updated_ = now;

    auto avail_cpu_time = GetAvailCpuTime(delta_t, cpuCount);

    if (peak_prev_system_time_ >= 0)
    {
        auto secs = (stats.at("system_usec") - peak_prev_system_time_) / MICROS;
        registry_->CreateMaxGauge("sys.cpu.peakUtilization", MergeTags({{"id", "system"}})).Set((secs / avail_cpu_time) * 100);
    }
    peak_prev_system_time_ = stats.at("system_usec");

    if (peak_prev_user_time_ >= 0)
    {
        auto secs = (stats.at("user_usec") - peak_prev_user_time_) / MICROS;
        registry_->CreateMaxGauge("sys.cpu.peakUtilization", MergeTags({{"id", "user"}})).Set((secs / avail_cpu_time) * 100);
    }
    peak_prev_user_time_ = stats.at("user_usec");
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
        CpuUtilizationV2(absl::Now(), cpuCount, stats, absl::Seconds(60));
    }

    // Collect 5 second metrics if enabled
    if (fiveSecondMetricsEnabled)
    {
        CpuTimeV2(stats);
        CpuProcessingCapacity(absl::Now(), cpuCount, absl::Seconds(5));
    }

    // Always collect peak stats (called every 1 second)
    CpuPeakUtilizationV2(absl::Now(), stats, cpuCount);
}

// Pod-scoped entry point. sys.cpu.*/titus.cpu.* are not actually node/Titus-only: Titus already
// emits them per-container today via CpuStats(), because titus-agent runs one process per
// container (a sidecar) with zero per-container tagging in this code -- container identity comes
// entirely from which spectatord instance that sidecar talks to. k8s-agent's PodMonitor is the
// one architecturally different case, multiplexing N pods through a single Registry in one
// process -- which is exactly why the extra_tags_/SetExtraTags()/MergeTags() mechanism exists, and
// it is already wired into every CpuUtilizationV2/CpuPeakUtilizationV2 call site. So once
// SetExtraTags() has given a pod's CGroup instance a disambiguating tag, sys.cpu.*/titus.cpu.* are
// just as meaningful per-pod as they are per-container for Titus. PodCpuStats() therefore
// delegates straight to CpuStats() for full Titus-name parity; it stays a separate, distinctly
// named method purely so PodMonitor's call site remains self-documenting about intent, even
// though the two bodies are now identical.
void CGroup::PodCpuStats(const bool fiveSecondMetricsEnabled, const bool sixtySecondMetricsEnabled)
{
    CpuStats(fiveSecondMetricsEnabled, sixtySecondMetricsEnabled);
}

void CGroup::MemoryStatsV2() noexcept
{
    auto usage_bytes = read_num_from_file(path_prefix_, "memory.current");
    if (usage_bytes >= 0)
    {
        registry_->CreateGauge("cgroup.mem.used", MergeTags({})).Set(usage_bytes);
    }

    auto limit_bytes = read_num_from_file(path_prefix_, "memory.max");
    if (limit_bytes >= 0)
    {
        registry_->CreateGauge("cgroup.mem.limit", MergeTags({})).Set(limit_bytes);
    }

    std::unordered_map<std::string, int64_t> events;
    parse_kv_from_file(path_prefix_, "memory.events", &events);
    auto mem_fail = events["max"];
    if (mem_fail >= 0)
    {
        registry_->CreateMonotonicCounter("cgroup.mem.failures", MergeTags({})).Set(mem_fail);
    }

    // kmem_stats not available for v2

    std::unordered_map<std::string, int64_t> stats;
    parse_kv_from_file(path_prefix_, "memory.stat", &stats);

    registry_->CreateGauge("cgroup.mem.processUsage", MergeTags({{"id", "cache"}})).Set(stats["file"]);

    registry_->CreateGauge("cgroup.mem.processUsage", MergeTags({{"id", "rss"}})).Set(stats["anon"]);

    registry_->CreateGauge("cgroup.mem.processUsage", MergeTags({{"id", "rss_huge"}})).Set(stats["anon_thp"]);

    registry_->CreateGauge("cgroup.mem.processUsage", MergeTags({{"id", "mapped_file"}})).Set(stats["file_mapped"]);

    registry_->CreateMonotonicCounter("cgroup.mem.pageFaults", MergeTags({{"id", "minor"}})).Set(stats["pgfault"]);

    registry_->CreateMonotonicCounter("cgroup.mem.pageFaults", MergeTags({{"id", "major"}})).Set(stats["pgmajfault"]);
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
    registry_->CreateGauge("mem.cached", MergeTags({})).Set(cache);

    registry_->CreateGauge("mem.shared", MergeTags({})).Set(stats["shmem"]);

    if (mem_limit >= 0 && mem_usage >= 0)
    {
        registry_->CreateGauge("mem.availReal", MergeTags({})).Set(mem_limit - mem_usage + cache);
        registry_->CreateGauge("mem.freeReal", MergeTags({})).Set(mem_limit - mem_usage);
        registry_->CreateGauge("mem.totalReal", MergeTags({})).Set(mem_limit);
    }

    if (memsw_limit >= 0 && memsw_usage >= 0)
    {
        registry_->CreateGauge("mem.availSwap", MergeTags({})).Set(memsw_limit - memsw_usage);
        registry_->CreateGauge("mem.totalSwap", MergeTags({})).Set(memsw_limit);
    }

    if (mem_limit >= 0 && mem_usage >= 0 && memsw_limit >= 0 && memsw_usage >= 0)
    {
        registry_->CreateGauge("mem.totalFree", MergeTags({})).Set((mem_limit - mem_usage) + (memsw_limit - memsw_usage) + cache);
    }
}

std::unordered_map<std::string, std::string> FindDeviceNames()
{
    auto lines = read_lines_fields("/proc", "diskstats");
    std::unordered_map<std::string, std::string> deviceMap(lines.size());
    static constexpr unsigned int EXPECTED_FIELDS = 20;
    for (const auto& fields : lines)
    {
        if (fields.size() != EXPECTED_FIELDS) [[unlikely]]
        {
            atlasagent::Logger()->warn("Unexpected number of fields in /proc/diskstats line: {}", fields.size());
            continue;
        }

        std::string majorMinor = fields[0] + ':' + fields[1];
        deviceMap.emplace(std::move(majorMinor), fields[2]);
    }
    return deviceMap;
}

std::optional<IOStats> ParseIOLine(const std::vector<std::string>& fields, const std::unordered_map<std::string, std::string>& devMap) try
{
    // Set the key to the device name
    IOStats stats;
    stats.majorMinor = fields[0];

    auto it = devMap.find(stats.majorMinor);
    if (it != devMap.end())
    {
        stats.deviceName = it->second;
    }
    else
    {
        atlasagent::Logger()->warn("Device major:minor {} not found in /proc/diskstats mapping", stats.majorMinor);
    }

    // Iterate through the remaining fields and parse key-value pairs
    for (size_t i = 1; i < fields.size(); ++i)
    {
        auto pos = fields[i].find('=');
        if (pos == std::string::npos)
        {
            throw std::runtime_error("Malformed key=value pair in io.stat: " + fields[i]);
        }

        std::string_view currentField(fields[i]);
        std::string_view key = currentField.substr(0, pos);

        // Skip unknown extension keys (e.g. cost.vrate, cost.usage added by io.cost)
        // before parsing the value — floating-point from_chars is incomplete on some
        // C++17 toolchains (libstdc++ < GCC 11), so values like "100.00" must never
        // reach from_chars for keys we do not intend to use.
        static const std::unordered_set<std::string_view> knownKeys{"rbytes", "wbytes", "rios", "wios", "dbytes", "dios"};
        if (!knownKeys.count(key))
            continue;

        std::string_view value_str = currentField.substr(pos + 1);

        double value;
        auto [ptr, ec] = std::from_chars(value_str.data(), value_str.data() + value_str.size(), value);
        if (ec != std::errc())
        {
            throw std::runtime_error("Failed to parse expected integer from io.stat");
        };

        if (value < 0)
        {
            throw std::runtime_error("Negative value in io.stat for key: " + std::string(key));
        }

        if (key == "rbytes" && stats.rBytes.has_value() == false)
            stats.rBytes = value;
        else if (key == "wbytes" && stats.wBytes.has_value() == false)
            stats.wBytes = value;
        else if (key == "rios" && stats.rOperations.has_value() == false)
            stats.rOperations = value;
        else if (key == "wios" && stats.wOperations.has_value() == false)
            stats.wOperations = value;
        else if (key == "dbytes" && stats.dBytes.has_value() == false)
            stats.dBytes = value;
        else if (key == "dios" && stats.dOperations.has_value() == false)
            stats.dOperations = value;
        else
            // Unknown keys were already filtered above, so reaching here means a duplicate.
            throw std::runtime_error("Duplicate key in io.stat: " + std::string(key));
    }

    // Validate that all required IO statistics are set
    bool all_stats_valid = stats.rBytes.has_value() && stats.wBytes.has_value() && stats.rOperations.has_value() &&
                           stats.wOperations.has_value() && stats.dBytes.has_value() && stats.dOperations.has_value();

    if (!all_stats_valid)
    {
        throw std::runtime_error("Incomplete IO statistics for device: " + stats.majorMinor);
    }

    return stats;
}
catch (const std::exception& ex)
{
    atlasagent::Logger()->error("Exception parsing IO stat line: {}", ex.what());
    return std::nullopt;
}

std::unordered_map<std::string, IOStats> ParseIOLines(const std::vector<std::vector<std::string>>& lines, const std::unordered_map<std::string, std::string>& devMap) try
{
    std::unordered_map<std::string, IOStats> ioStats;

    // Iterate through each line from io.stat
    for (const auto& fields : lines)
    {
        // Skip completely empty lines (device with no stats)
        if (fields.size() == 1) continue;

        // Each line needs at least 7 fields: device rbytes= wbytes= rios= wios= dbytes= dios=
        // Extra fields (e.g. cost.vrate=, cost.usage= added by io.cost) are tolerated
        if (fields.size() < 7)
        {
            throw std::runtime_error("Invalid number of fields in io.stat line: " + std::to_string(fields.size()));
        }

        // Parse the individual line into an IOStats object
        auto ioStatObject = ParseIOLine(fields, devMap);
        if (ioStatObject.has_value() == false)
        {
            return {};
        }
        ioStats.emplace(ioStatObject->majorMinor, std::move(ioStatObject.value()));
    }
    return ioStats;
}
catch (const std::exception& ex)
{
    atlasagent::Logger()->error("Exception parsing IO stat lines: {}", ex.what());
    return {};
}

std::optional<IOThrottle> ParseIOThrottleLine(const std::vector<std::string>& fields) try
{
    // Set the device name
    IOThrottle throttle;
    throttle.device = fields[0];

    // Iterate through the remaining throttle fields
    for (size_t i = 1; i < fields.size(); ++i)
    {
        auto pos = fields[i].find('=');
        if (pos == std::string::npos)
        {
            throw std::runtime_error("Malformed key=value pair in io.max: " + fields[i]);
        }

        std::string_view currentField(fields[i]);
        std::string_view key = currentField.substr(0, pos);
        std::string_view value_str = currentField.substr(pos + 1);

        double value;
        if (value_str == "max")
        {
            value = -1.0;
        }
        else
        {
            auto [ptr, ec] = std::from_chars(value_str.data(), value_str.data() + value_str.size(), value);
            if (ec != std::errc())
            {
                throw std::runtime_error("Failed to parse expected integer from io.max");
            };

            if (value < 0)
            {
                throw std::runtime_error("Negative value in io.max for key: " + std::string(key));
            }
        }

        // Direct assignment based on key
        if (key == "rbps" && throttle.rBps.has_value() == false)
            throttle.rBps = value;
        else if (key == "wbps" && throttle.wBps.has_value() == false)
            throttle.wBps = value;
        else if (key == "riops" && throttle.rIops.has_value() == false)
            throttle.rIops = value;
        else if (key == "wiops" && throttle.wIops.has_value() == false)
            throttle.wIops = value;
        else
            throw std::runtime_error("Unexpected or duplicate key in io.max: " + std::string(key));
    }

    bool validThrottle = throttle.rBps.has_value() && throttle.wBps.has_value() && throttle.rIops.has_value() &&
                         throttle.wIops.has_value();

    if (!validThrottle)
    {
        throw std::runtime_error("Incomplete IO throttle settings for device: " + throttle.device);
    }
    return throttle;
}
catch (const std::exception& ex)
{
    atlasagent::Logger()->error("Exception parsing IO throttle line: {}", ex.what());
    return std::nullopt;
}

std::unordered_map<std::string, IOThrottle> ParseIOThrottleLines(const std::vector<std::vector<std::string>>& lines) try
{
    std::unordered_map<std::string, IOThrottle> ioThrottles;

    // Iterate through each line from io.max
    for (const auto& fields : lines)
    {
        // Each line should have exactly 5 fields: device rbps= wbps= riops= wiops=
        if (fields.size() != 5)
        {
            throw std::runtime_error("Unexpected number of fields in io.max line " + std::to_string(fields.size()));
        }

        auto throttle = ParseIOThrottleLine(fields);
        if (throttle.has_value() == false)
        {
            return {};
        }
        ioThrottles.emplace(throttle->device, throttle.value());
    }
    return ioThrottles;
}
catch (const std::exception& ex)
{
    atlasagent::Logger()->error("Exception parsing IO throttle lines: {}", ex.what());
    return {};
}

void CGroup::UpdateIOMetrics(const std::unordered_map<std::string, atlasagent::IOStats>& ioStats, const std::unordered_map<std::string, IOThrottle>& ioThrottles)
{
    constexpr double INTERVAL_SECONDS = 5.0;
    constexpr double PERCENT_MULTIPLIER = 100.0;

    // Iterate through current IO statistics
    for (const auto& [deviceKey, currentStat] : ioStats)
    {
        atlasagent::Logger()->debug("IO Stats for device {}:", deviceKey);
        atlasagent::Logger()->debug("\tIO Object: {{current_rbytes: {}, current_rios: {}, current_wbytes: {}, current_wios: {}}}",
                        currentStat.rBytes.value(), currentStat.rOperations.value(), currentStat.wBytes.value(),
                        currentStat.wOperations.value());

        // Check if we have previous data (from this instance's own last reading) for a delta
        // calculation
        auto prev_it = io_previous_stats_.find(deviceKey);
        if (prev_it != io_previous_stats_.end())
        {
            const auto& prevStat = prev_it->second;

            // Calculate deltas
            const auto delta_rbytes = currentStat.rBytes.value() - prevStat.rBytes.value();
            const auto delta_wbytes = currentStat.wBytes.value() - prevStat.wBytes.value();
            const auto delta_rios = currentStat.rOperations.value() - prevStat.rOperations.value();
            const auto delta_wios = currentStat.wOperations.value() - prevStat.wOperations.value();

            atlasagent::Logger()->debug("\tRealtime Counter: {{delta_rbytes: {}, delta_rios: {}, delta_wbytes: {}, delta_wios: {}}}",
                            delta_rbytes, delta_rios, delta_wbytes, delta_wios);

            // Update byte and operation counters
            registry_->CreateCounter("disk.io.bytes", MergeTags({{"dev", currentStat.deviceName}, {"id", "read"}})).Increment(delta_rbytes);
            registry_->CreateCounter("disk.io.bytes", MergeTags({{"dev", currentStat.deviceName}, {"id", "write"}})).Increment(delta_wbytes);
            registry_->CreateCounter("disk.io.ops", MergeTags({{"dev", currentStat.deviceName}, {"id", "read"}, {"statistic", "count"}})).Increment(delta_rios);
            registry_->CreateCounter("disk.io.ops", MergeTags({{"dev", currentStat.deviceName}, {"id", "write"}, {"statistic", "count"}})).Increment(delta_wios);

            // Calculate throttle utilization if throttle data is available
            auto throttle_it = ioThrottles.find(deviceKey);
            if (throttle_it != ioThrottles.end())
            {
                const auto& throttle = throttle_it->second;

                atlasagent::Logger()->debug("\tThrottle Settings: {{read_bps: {}, write_bps: {}, read_iops: {}, write_iops: {}}}",
                                throttle.rBps.value(), throttle.wBps.value(), throttle.rIops.value(),
                                throttle.wIops.value());

                // Helper lambda to record throttle utilization
                auto record_throttle_utilization = [&](double delta, const std::optional<double>& limit, const std::string& metric_name, const std::string& operation)
                {
                    if (limit.has_value() && limit.value() > 0)
                    {
                        // Utilization = (delta / (limit * interval)) * 100
                        const auto utilization = (delta / (limit.value() * INTERVAL_SECONDS)) * PERCENT_MULTIPLIER;
                        registry_->CreateDistributionSummary(metric_name, MergeTags({{"dev", currentStat.deviceName}, {"id", operation}})).Record(utilization);
                    }
                };

                record_throttle_utilization(delta_rbytes, throttle.rBps, "cgroup.disk.io.throttleActivityBytes", "read");
                record_throttle_utilization(delta_wbytes, throttle.wBps, "cgroup.disk.io.throttleActivityBytes", "write");
                record_throttle_utilization(delta_rios, throttle.rIops, "cgroup.disk.io.throttleActivityOperations", "read");
                record_throttle_utilization(delta_wios, throttle.wIops, "cgroup.disk.io.throttleActivityOperations", "write");
            }
        }
        // Update previous stats for next iteration
        io_previous_stats_[deviceKey] = currentStat;
    }
}

void CGroup::IOStats()
{
    // Read the contents of io.stat
    auto ioStatLines = read_lines_fields(path_prefix_, "io.stat");

    // Find all the device names from /proc/diskstats and create mapping of {major:minor, device name}
    auto deviceNames = FindDeviceNames();

    // Parse the io.stat lines into structured IOStats objects
    auto ioStats = ParseIOLines(ioStatLines, deviceNames);
    if (ioStats.empty())
    {
        atlasagent::Logger()->info("No valid IO statistics found in io.stat");
        return;
    }

    // Read the contents of io.max and parse into structured IOThrottle objects
    auto throttleLines = read_lines_fields(path_prefix_, "io.max");
    auto ioThrottles = ParseIOThrottleLines(throttleLines);

    // Update metrics based on parsed IO statistics and throttling information
    UpdateIOMetrics(ioStats, ioThrottles);
}

}  // namespace atlasagent
