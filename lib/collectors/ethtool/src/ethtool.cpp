#include "ethtool.h"

#include <absl/strings/str_join.h>

#include <filesystem>
#include <system_error>

namespace atlasagent
{

Ethtool::Ethtool(Registry* registry, std::unordered_map<std::string, std::string> net_tags,
                 std::string path_prefix) noexcept
    : registry_(registry), net_tags_{std::move(net_tags)}, path_prefix_{std::move(path_prefix)}
{
}

void Ethtool::collect() noexcept
{
    if (can_execute("ethtool") == false)
    {
        return;
    }

    // Re-enumerated every cycle rather than cached once: this is a directory scan with no fork,
    // and ENIs come and go at runtime (the AWS VPC CNI attaches and detaches them as pods are
    // scheduled), so a list captured at startup goes stale.
    const auto interfaces = enumerate_interfaces();
    Logger()->debug("Collecting ethtool stats for [{}]", absl::StrJoin(interfaces, ", "));

    for (const auto& iface : interfaces)
    {
        auto nic_stats = read_output_lines(fmt::format("ethtool -S {}", iface).c_str());
        ethtool_stats(nic_stats, iface.c_str());
    }
}

// Enumerate physical NICs from /sys/class/net, where every interface in the netns appears as an
// entry named with its true kernel name. There is nothing to parse, and no way to produce a name
// the kernel would not accept -- unlike scraping `ip link show`, whose `<name>@<peer>` display
// format yielded names such as "veth1a2b3c4d@if2" that are not device names at all and that
// `ethtool` rejects ("ioctl-only request, device name longer than 15 not supported").
//
// The filter is the `device` symlink, which points into the PCI/device tree and so exists only
// for hardware-backed interfaces. Virtual interfaces do not have it: lo, the veth peer of every
// pod on a k8s node, bridges, vxlan, dummy, bond. That keeps exactly the interfaces whose ENA
// allowance counters this collector reads (eth0 plus any secondary ENIs) without matching on
// interface names, which vary with instance type and kernel naming policy (eth0, ens5, enp0s5).
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
        if (std::filesystem::exists(entry->path() / "device", device_ec) == false)
        {
            continue;
        }
        result.emplace_back(entry->path().filename().string());
    }

    return result;
}

void Ethtool::update_metric(const std::string& stat_line, MonotonicCounter metric)
{
    std::vector<std::string> stat_fields = absl::StrSplit(stat_line, ':');
    try
    {
        auto number = std::stoll(stat_fields[1]);
        metric.Set(number);
    }
    catch (const std::invalid_argument& e)
    {
        atlasagent::Logger()->error("Unable to parse {} as a number: {}", stat_fields[1], e.what());
    }
}

void Ethtool::ethtool_stats(const std::vector<std::string>& nic_stats, const char* iface) noexcept
{
    std::size_t found;

    for (const auto& stat_line : nic_stats)
    {
        found = stat_line.find("bw_in_allowance_exceeded:");
        if (found != std::string::npos)
        {
            auto tags = net_tags_;
            tags["iface"] = iface;
            tags["id"] = "in";

            auto metric = registry_->CreateMonotonicCounter("net.perf.bwAllowanceExceeded", tags);
            update_metric(stat_line, metric);
            continue;
        }

        found = stat_line.find("bw_out_allowance_exceeded:");
        if (found != std::string::npos)
        {
            auto tags = net_tags_;
            tags["iface"] = iface;
            tags["id"] = "out";
            auto metric = registry_->CreateMonotonicCounter("net.perf.bwAllowanceExceeded", tags);

            update_metric(stat_line, metric);
            continue;
        }

        found = stat_line.find("conntrack_allowance_exceeded:");
        if (found != std::string::npos)
        {
            auto tags = net_tags_;
            tags["iface"] = iface;
            auto metric = registry_->CreateMonotonicCounter("net.perf.conntrackAllowanceExceeded", tags);
            update_metric(stat_line, metric);
            continue;
        }

        found = stat_line.find("conntrack_allowance_available:");
        if (found != std::string::npos)
        {
            auto tags = net_tags_;
            tags["iface"] = iface;
            auto metric = registry_->CreateGauge("net.perf.conntrackAllowanceAvailable", tags);

            std::vector<std::string> stat_fields = absl::StrSplit(stat_line, ':');
            try
            {
                auto number = std::stoll(stat_fields[1]);
                metric.Set(number);
            }
            catch (const std::invalid_argument& e)
            {
                atlasagent::Logger()->error("Unable to parse {} as a number: {}", stat_fields[1], e.what());
            }
            continue;
        }

        found = stat_line.find("linklocal_allowance_exceeded:");
        if (found != std::string::npos)
        {
            auto tags = net_tags_;
            tags["iface"] = iface;
            auto metric = registry_->CreateMonotonicCounter("net.perf.linklocalAllowanceExceeded", tags);
            update_metric(stat_line, metric);
            continue;
        }

        found = stat_line.find("pps_allowance_exceeded:");
        if (found != std::string::npos)
        {
            auto tags = net_tags_;
            tags["iface"] = iface;
            auto metric = registry_->CreateMonotonicCounter("net.perf.ppsAllowanceExceeded", tags);
            update_metric(stat_line, metric);
        }
    }
}

}  // namespace atlasagent
