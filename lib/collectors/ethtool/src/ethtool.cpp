#include "ethtool.h"

#include <absl/strings/ascii.h>
#include <absl/strings/numbers.h>
#include <absl/strings/str_join.h>

#include <array>
#include <cstdint>
#include <filesystem>
#include <string_view>
#include <system_error>

namespace atlasagent
{

namespace
{

enum class MetricType
{
    Counter,
    Gauge,
};

struct StatDefinition
{
    std::string_view ethtool_name;
    const char* metric_name;
    MetricType metric_type;
    const char* id = nullptr;
};

constexpr std::array kStatDefinitions{
    StatDefinition{"bw_in_allowance_exceeded", "net.perf.bwAllowanceExceeded", MetricType::Counter, "in"},
    StatDefinition{"bw_out_allowance_exceeded", "net.perf.bwAllowanceExceeded", MetricType::Counter, "out"},
    StatDefinition{"conntrack_allowance_exceeded", "net.perf.conntrackAllowanceExceeded", MetricType::Counter},
    StatDefinition{"conntrack_allowance_available", "net.perf.conntrackAllowanceAvailable", MetricType::Gauge},
    StatDefinition{"linklocal_allowance_exceeded", "net.perf.linklocalAllowanceExceeded", MetricType::Counter},
    StatDefinition{"pps_allowance_exceeded", "net.perf.ppsAllowanceExceeded", MetricType::Counter},
};

// Budget for the first line of output, covering fork and two execs as well as the ioctl. Normally
// a few milliseconds, but exec latency spikes under CPU throttling, and a spurious timeout costs
// an interface its metrics for the cycle.
constexpr int kEthtoolTimeoutMillis = 500;

// Returns the matching definition with its value in result, or nullptr for both an untracked line
// (the common case) and a tracked name whose value will not parse (logged).
const StatDefinition* parse_stat(std::string_view stat_line, std::int64_t& result) noexcept
{
    const auto colon = stat_line.find(':');
    if (colon == std::string_view::npos)
    {
        return nullptr;
    }

    // ethtool -S prints "    <name>: <value>". Match the whole name, so a statistic that merely
    // contains a tracked one (a per-queue variant, say) is not folded into the aggregate metric.
    const auto key = absl::StripAsciiWhitespace(stat_line.substr(0, colon));
    for (const auto& definition : kStatDefinitions)
    {
        if (key != definition.ethtool_name)
        {
            continue;
        }

        const auto value = stat_line.substr(colon + 1);
        if (!absl::SimpleAtoi(value, &result))
        {
            Logger()->error("Unable to parse {} as a number", value);
            return nullptr;
        }
        return &definition;
    }
    return nullptr;
}

}  // namespace

Ethtool::Ethtool(Registry* registry, std::unordered_map<std::string, std::string> net_tags,
                 std::string path_prefix) noexcept
    : registry_(registry), net_tags_{std::move(net_tags)}, path_prefix_{std::move(path_prefix)}
{
}

std::vector<std::string> Ethtool::run_ethtool(const std::string& iface)
{
    if (!can_execute("ethtool"))
    {
        return {};
    }

    const auto command = fmt::format("ethtool -S {}", iface);
    return read_output_lines(command.c_str(), kEthtoolTimeoutMillis);
}

void Ethtool::collect() noexcept
{
    // Re-enumerated every cycle, not cached: the AWS VPC CNI attaches and detaches ENIs as pods
    // are scheduled, so a list captured at startup goes stale. It is only a directory scan.
    const auto interfaces = enumerate_interfaces();
    Logger()->debug("Collecting ethtool stats for [{}]", absl::StrJoin(interfaces, ", "));

    for (const auto& iface : interfaces)
    {
        ethtool_stats(run_ethtool(iface), iface);
    }
}

// Every interface in the netns appears in /sys/class/net under its true kernel name, so there is
// nothing to parse -- unlike `ip link show`, whose `<name>@<peer>` format yielded names such as
// "veth1a2b3c4d@if2" that ethtool rejects ("device name longer than 15 not supported").
//
// The `device` symlink points into the PCI/device tree, so it exists only for hardware-backed
// interfaces, never for lo, pod veth peers, bridges, vxlan, dummy or bond. That selects exactly
// the ENA interfaces carrying the allowance counters above (eth0 and any secondary ENIs) without
// matching on names, which vary by instance type and naming policy (eth0, ens5, enp0s5).
std::vector<std::string> Ethtool::enumerate_interfaces() noexcept
{
    std::vector<std::string> result;

    std::error_code ec;
    auto entry = std::filesystem::directory_iterator{path_prefix_, ec};
    if (ec)
    {
        Logger()->warn("Unable to list network interfaces in {}: {}", path_prefix_, ec.message());
        return result;
    }

    const std::filesystem::directory_iterator end{};
    for (; entry != end; entry.increment(ec))
    {
        if (ec)
        {
            Logger()->warn("Unable to finish listing network interfaces in {}: {}", path_prefix_, ec.message());
            break;
        }

        std::error_code device_ec;
        if (!std::filesystem::exists(entry->path() / "device", device_ec))
        {
            continue;
        }
        result.emplace_back(entry->path().filename().string());
    }

    return result;
}

void Ethtool::ethtool_stats(const std::vector<std::string>& nic_stats, const std::string& iface) noexcept
{
    for (const auto& stat_line : nic_stats)
    {
        std::int64_t value{};
        const auto* definition = parse_stat(stat_line, value);
        if (definition == nullptr)
        {
            continue;
        }

        auto tags = net_tags_;
        tags["iface"] = iface;
        if (definition->id != nullptr)
        {
            tags["id"] = definition->id;
        }

        if (definition->metric_type == MetricType::Gauge)
        {
            registry_->CreateGauge(definition->metric_name, tags).Set(value);
        }
        else
        {
            registry_->CreateMonotonicCounter(definition->metric_name, tags).Set(value);
        }
    }
}

}  // namespace atlasagent
