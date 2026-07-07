// Method bodies for ServiceMonitor. The class declaration, constants, the detail gauge helpers, and
// the CpuRateTracker template live in service_monitor.h.

#include "service_monitor.h"
#include <lib/util/src/util.h>

#include <algorithm>
#include <unistd.h>

ServiceMonitor::ServiceMonitor(Registry* registry, std::vector<std::regex> config, unsigned int max_services)
    : registry_{registry},
      config_{std::move(config)},
      // max_services == 0 means "not set" -> fall back to the default; any positive value overrides it.
      maxMonitoredServices{max_services == 0 ? ServiceMonitorConstants::DefaultMonitoredServices : max_services}
{
    if (this->maxMonitoredServices != ServiceMonitorConstants::DefaultMonitoredServices)
    {
        atlasagent::Logger()->info("Custom max monitored services value set: {} (default is {})", maxMonitoredServices,
                                   ServiceMonitorConstants::DefaultMonitoredServices);
    }
}

bool ServiceMonitor::init_monitored_services()
try
{
    // Reject any non-positive result: -1 is sysconf's error return, and 0 would later divide-by-zero
    // (clkTck_ at the CPU conversion) or silently zero every RSS sample (pageSize_ as the scale).
    this->pageSize_ = sysconf(_SC_PAGESIZE);
    if (this->pageSize_ <= 0)
    {
        atlasagent::Logger()->error("Error getting page size");
        return false;
    }

    this->clkTck_ = sysconf(_SC_CLK_TCK);
    if (this->clkTck_ <= 0)
    {
        atlasagent::Logger()->error("Error getting clock ticks per second");
        return false;
    }

    auto all_units = list_all_units();
    if (all_units.has_value() == false)
    {
        atlasagent::Logger()->error("Error gathering all units from Systemd");
        return false;
    }

    for (const auto& unit : all_units.value())
    {
        const auto& unit_name = std::get<0>(unit);

        bool matched = std::any_of(config_.begin(), config_.end(), [&unit_name](const std::regex& regex)
                                   { return std::regex_search(unit_name, regex); });

        if (matched == false)
        {
            continue;
        }

        if (monitoredServices_.size() >= maxMonitoredServices)
        {
            atlasagent::Logger()->info(
                "Reached maximum number of monitored services ({}). Ignoring service {} and remaining services.",
                maxMonitoredServices, unit_name);
            break;
        }

        monitoredServices_.emplace_back(unit_name);
        atlasagent::Logger()->info("Added service {} to monitoring list ({}/{})", unit_name, monitoredServices_.size(),
                                   this->maxMonitoredServices);
    }

    this->initSuccess = true;
    if (this->monitoredServices_.empty() == true)
    {
        atlasagent::Logger()->error("User Error: Monitor Service config provided but no services matched pattern");
    }
    return true;
}
catch (const std::exception& e)
{
    atlasagent::Logger()->error("Exception: {} in init_monitored_services", e.what());
    return false;
}

template <typename T>
bool ServiceMonitor::publish_metric(const std::string& service, std::optional<T> val, double scale,
                                    std::string_view name, std::string_view scope, std::string_view status,
                                    std::string_view errMsg) const
{
    if (val)
    {
        auto g = scope.empty() ? detail::gauge(registry_, name, service, status)
                               : detail::gaugeScoped(registry_, name, service, scope, status);
        g.Set(static_cast<double>(*val) * scale);
        return true;
    }
    if (!errMsg.empty())
    {
        atlasagent::Logger()->error("{} for {}", errMsg, service);
    }
    return errMsg.empty();
}

bool ServiceMonitor::collect_process_metrics(const std::string& service, const ServiceProperties& props,
                                             std::chrono::steady_clock::time_point now,
                                             CpuRateTracker<unsigned int>& cpu) const
{
    bool success = true;
    success &= publish_metric(service, get_rss(props.mainPid), static_cast<double>(pageSize_),
                              ServiceMonitorConstants::RssName, "", props.statusText, "Failed to get RSS");
    success &= publish_metric(service, get_number_fds(props.mainPid), 1.0, ServiceMonitorConstants::FdsName, "process",
                              props.statusText, "Failed to get FD count");

    // Read the main PID's accumulated CPU time. On a failed read there is no sample this cycle: skip
    // the tracker update so its baseline is preserved and the next good read spans the gap correctly.
    auto times = get_process_times(props.mainPid);
    if (!times)
    {
        atlasagent::Logger()->error("Failed to get process times for {}", service);
        return false;
    }

    const unsigned long long curr_us =
        (times->uTime + times->sTime) * 1'000'000ULL / static_cast<unsigned long long>(clkTck_);
    // The tracker keys on the PID, so a restart (new PID) resets the baseline rather than producing a
    // cross-generation delta. A missing percentage on the first cycle is normal, not a failure.
    publish_metric(service, cpu.update(props.mainPid, curr_us, now), 1.0, ServiceMonitorConstants::CpuUsageName,
                   "process", props.statusText);
    return success;
}

bool ServiceMonitor::collect_cgroup_metrics(const std::string& service, const ServiceProperties& props,
                                            std::chrono::steady_clock::time_point now,
                                            CpuRateTracker<std::string>& cpu) const
{
    bool success = true;
    success &= publish_metric(service, get_cgroup_memory(props.controlGroup), 1.0, ServiceMonitorConstants::MemoryName,
                              "", props.statusText, "Failed to get cgroup memory");
    success &= publish_metric(service, get_total_fds(props.controlGroup), 1.0, ServiceMonitorConstants::FdsName,
                              "service", props.statusText, "Failed to get cgroup total FDs");

    // As above: a failed read means no sample this cycle, so skip the tracker update.
    auto usage = get_cgroup_cpu_usage(props.controlGroup);
    if (!usage)
    {
        atlasagent::Logger()->error("Failed to get cgroup CPU usage for {}", service);
        return false;
    }

    // The tracker keys on the control-group path, so a recreated cgroup resets the baseline.
    publish_metric(service, cpu.update(props.controlGroup, *usage, now), 1.0, ServiceMonitorConstants::CpuUsageName,
                   "service", props.statusText);
    return success;
}

bool ServiceMonitor::update_metrics()
try
{
    bool success = true;
    const auto now = std::chrono::steady_clock::now();

    for (const auto& service : monitoredServices_)
    {
        auto props = get_service_properties(service);
        if (!props)
        {
            atlasagent::Logger()->error("Failed to get {} properties", service);
            success = false;
            continue;
        }

        const auto stateStr = fmt::format("{}.{}", props->activeState, props->subState);
        detail::gaugeServiceState(registry_, ServiceMonitorConstants::ServiceStatusName, service, stateStr,
                                  props->statusText)
            .Set(1);

        if (props->activeState != ServiceMonitorUtilConstants::Active ||
            props->subState != ServiceMonitorUtilConstants::Running)
        {
            atlasagent::Logger()->info("Service {} is not active and running, skipping metrics", service);
            continue;
        }

        // operator[] default-constructs the entry on a service's first active cycle; the trackers
        // start invalid, so that cycle publishes no CPU%. The trackers are mutated in place -- there
        // is no per-cycle state object to rebuild and reassign.
        ServiceCpuState& state = cpuState_[service];
        success &= collect_process_metrics(service, *props, now, state.process);
        success &= collect_cgroup_metrics(service, *props, now, state.cgroup);
    }

    return success;
}
catch (const std::exception& e)
{
    atlasagent::Logger()->error("Exception: {} in update_metrics", e.what());
    return false;
}

bool ServiceMonitor::gather_metrics()
{
    if (this->initSuccess == false && this->init_monitored_services() == false)
    {
        return false;
    }

    if (this->monitoredServices_.size() == 0)
    {
        atlasagent::Logger()->error(
            "No systemd services to monitor, but configs were provided. "
            "Configured maximum services to monitor: {}.",
            this->maxMonitoredServices);
        return true;
    }

    return this->update_metrics();
}
