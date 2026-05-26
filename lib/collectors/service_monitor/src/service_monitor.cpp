#include "service_monitor.h"
#include <lib/util/src/util.h>

// The constructor takes a registry, a vector of regex patterns, and a maximum number of services to monitor.
// If the maximum number of services is not equal to the default value, it logs a message indicating the custom value.

ServiceMonitor::ServiceMonitor(Registry* registry, std::vector<std::regex> config, unsigned int max_services,
                               bool use_cgroup_cpu, std::string cgroup_root)
    : registry_{registry},
      config_{std::move(config)},
      maxMonitoredServices{max_services == ServiceMonitorConstants::DefaultMonitoredServices
                               ? ServiceMonitorConstants::DefaultMonitoredServices
                               : max_services},
      useCgroupCpu_{use_cgroup_cpu},
      cgroupRoot_{std::move(cgroup_root)}
{
    if (this->maxMonitoredServices != ServiceMonitorConstants::DefaultMonitoredServices)
    {
        atlasagent::Logger()->info("Custom max monitored services value set: {} (default is {})", maxMonitoredServices,
                                   ServiceMonitorConstants::DefaultMonitoredServices);
    }
    atlasagent::Logger()->info("systemd.service.cpuUsage source: {}",
                               useCgroupCpu_ ? "cgroup cpu.stat (entire unit)" : "MainPID /proc/<pid>/stat (legacy)");
}

bool ServiceMonitor::init_monitored_services()
try
{
    // Read & set the CPU core count which is used to calculate CPU usage
    auto cpuCores = get_cpu_cores();
    if (cpuCores.has_value() == false)
    {
        atlasagent::Logger()->error("Error determining the number of cpu cores");
        return false;
    }
    this->numCpuCores = cpuCores.value();

    // Read & set the page size in bytes to convert rss from pages to bytes
    this->pageSize = sysconf(_SC_PAGESIZE);
    if (this->pageSize == -1)
    {
        atlasagent::Logger()->error("Error getting page size");
        return false;
    }

    // Get all the systemd units on the system
    auto all_units = list_all_units();
    if (all_units.has_value() == false)
    {
        atlasagent::Logger()->error("Error gathering all units from Systemd");
        return false;
    }

    // Iterate through all the units and check if the unit name matches any of the regex patterns in the config
    // If it does, add the unit name to the monitoredServices_ as long as we are beneath the maxMonitoredServices
    // threshold
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
                "Reached maximum number of monitored services ({}). Ignoring service {} and remaining "
                "services.",
                maxMonitoredServices, unit_name);
            break;
        }

        monitoredServices_.emplace_back(unit_name);
        atlasagent::Logger()->info("Added service {} to monitoring list ({}/{})", unit_name, monitoredServices_.size(),
                                   this->maxMonitoredServices);
    }

    // Units were retrieved. initSuccess is now true because monitoredServices now initialized
    // with pattern matched services.
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

bool ServiceMonitor::update_metrics()
try
{
    // Tracks non-critical metric update failures
    bool success = true;

    // Iterate over all monitored services that match the config, retrieving their properties
    // (main PID, active state, substate). If a service's properties can't be retrieved, update succes,
    // log the error and continue.
    std::vector<ServiceProperties> servicesStates{};
    servicesStates.reserve(monitoredServices_.size());
    for (const auto& service : monitoredServices_)
    {
        auto serviceState = get_service_properties(service);

        if (serviceState.has_value() == false)
        {
            atlasagent::Logger()->error("Failed to get {} properties", service);
            success = false;
            continue;
        }
        servicesStates.emplace_back(serviceState.value());
    }

    // If we couldn't get the properties for any of the services we are monitoring, log an error and return
    if (servicesStates.empty())
    {
        atlasagent::Logger()->error("Could not get properties for any monitored services");
        return false;
    }

    // Some services may fail to return properties, but this isn’t a full failure.
    // We will still update metrics for the services we were able to get the properties for.

    // CPU sampling path A (legacy): MainPID /proc/<pid>/stat. Pre-fetched here so per-service
    // logic can branch cheaply. Skipped entirely when cgroup mode is on.
    std::unordered_map<unsigned int, ProcessTimes> newProcessTimes{};
    std::optional<unsigned long long> newCpuTime{};
    if (!useCgroupCpu_)
    {
        newProcessTimes = create_pid_map(servicesStates);
        newCpuTime = get_total_cpu_time();
    }

    // CPU sampling path B (cgroup): one wall-clock measurement covers every service in this pass.
    auto nowMono = std::chrono::steady_clock::now();
    double cgroupIntervalSeconds = 0.0;
    bool haveCgroupInterval = false;
    if (useCgroupCpu_ && lastCgroupSampleTime_.time_since_epoch().count() != 0)
    {
        cgroupIntervalSeconds = std::chrono::duration<double>(nowMono - lastCgroupSampleTime_).count();
        haveCgroupInterval = cgroupIntervalSeconds > 0.0;
    }
    std::unordered_map<std::string, unsigned long long> nextCgroupCpuUsec{};

    // Iterate throught the services and update the metrics for each service
    for (const auto& service : servicesStates)
    {
        const auto newServiceState = fmt::format("{}.{}", service.activeState, service.subState);
        detail::gaugeServiceState(this->registry_, ServiceMonitorConstants::ServiceStatusName, service.name.c_str(),
                                  newServiceState.c_str())
            .Set(1);

        // If the service is not active and running, we do not want to send metrics that depend on /proc/[pid]
        // The systemd service variable 'main pid' remains set even if a process/service is not running.
        if (service.activeState != ServiceMonitorUtilConstants::Active ||
            service.subState != ServiceMonitorUtilConstants::Running)
        {
            atlasagent::Logger()->info(
                "Service {} is not active and running, not sending metrics dependent on /proc/[pid]", service.name);
            continue;
        }

        auto serviceRSS = get_rss(service.mainPid);
        auto serviceFds = get_number_fds(service.mainPid);

        std::optional<double> cpuUsage{std::nullopt};
        bool hadPriorCpuSample = false;
        if (useCgroupCpu_)
        {
            // Cgroup path: read usage_usec for the unit's cgroup. Covers the entire systemd unit
            // (MainPID + children + threads), unlike the legacy /proc/<MainPID>/stat sampler.
            auto cgroupUsec = get_cgroup_cpu_usec(cgroupRoot_, service.controlGroup);
            if (cgroupUsec.has_value())
            {
                nextCgroupCpuUsec[service.name] = cgroupUsec.value();
                auto prior = currentCgroupCpuUsec_.find(service.name);
                hadPriorCpuSample = prior != currentCgroupCpuUsec_.end();
                if (hadPriorCpuSample && haveCgroupInterval)
                {
                    cpuUsage.emplace(
                        calculate_cgroup_cpu_usage(prior->second, cgroupUsec.value(), cgroupIntervalSeconds));
                }
            }
        }
        else
        {
            // Legacy path: derive CPU from /proc/<MainPID>/stat. Requires both the prior and current
            // PID samples, and a non-null /proc/stat aggregate read. Identical semantics to before.
            hadPriorCpuSample = currentProcessTimes.find(service.mainPid) != currentProcessTimes.end();
            if (newCpuTime.has_value() && hadPriorCpuSample &&
                newProcessTimes.find(service.mainPid) != newProcessTimes.end())
            {
                auto newProcessTime = newProcessTimes[service.mainPid];
                auto oldProcessTime = currentProcessTimes[service.mainPid];
                cpuUsage.emplace(calculate_cpu_usage(currentCpuTime, newCpuTime.value(), oldProcessTime, newProcessTime,
                                                    this->numCpuCores));
            }
        }

        // Suppress the "missing CPU sample" warning on the first iteration after a service starts —
        // CPU usage is a delta and needs two samples to compute, so the first pass is expected to be empty.
        if (serviceRSS.has_value() == false || serviceFds.has_value() == false ||
            (hadPriorCpuSample && cpuUsage.has_value() == false))
        {
            success = false;
            atlasagent::Logger()->error("Failed to get metric(s) for {}", service.name);
        }
        if (serviceRSS.has_value())
        {
            detail::gauge(this->registry_, ServiceMonitorConstants::RssName, service.name.c_str())
                .Set(serviceRSS.value() * this->pageSize);
        }
        if (serviceFds.has_value())
        {
            detail::gauge(this->registry_, ServiceMonitorConstants::FdsName, service.name.c_str())
                .Set(serviceFds.value());
        }
        if (cpuUsage.has_value())
        {
            detail::gauge(this->registry_, ServiceMonitorConstants::CpuUsageName, service.name.c_str())
                .Set(cpuUsage.value());
        }
    }

    // Roll the CPU-sample state forward for the next iteration. Each path owns its own bookkeeping;
    // mode flips at runtime would discard the unused path's history, which is fine.
    if (useCgroupCpu_)
    {
        currentCgroupCpuUsec_ = std::move(nextCgroupCpuUsec);
        lastCgroupSampleTime_ = nowMono;
    }
    else
    {
        // If we failed to read /proc/stat we can't compute usage for any service this pass, so
        // wipe the prior PID map to prevent stale deltas.
        if (newCpuTime.has_value() == false)
        {
            this->currentCpuTime = 0;
            this->currentProcessTimes.clear();
            return false;
        }
        this->currentProcessTimes = std::move(newProcessTimes);
        this->currentCpuTime = newCpuTime.value();
    }
    return success;
}
catch (const std::exception& e)
{
    atlasagent::Logger()->error("Exception: {} in service moniotr update_metrics", e.what());
    return false;
}

// TODO: Shutdown module if no services are being monitored
bool ServiceMonitor::gather_metrics()
{
    // To begin sending metrics, we must first determine the core count, page size, and determine all the systemd
    // services that match our config. Once init_monitored_services() completes successfully, we can start
    // sending metrics to spectatord. If we fail to determine these values, we will continue to retry until success.
    if (this->initSuccess == false && this->init_monitored_services() == false)
    {
        return false;
    }

    // TODO: We successfully initialized but none of the services on the system matched any of
    // the regex patterns in our configs. This results in no services to monitor.
    // We should create a way to remove this collector from the 60 sec
    // collection interval. This error is logged in init_monitored_services. We return true here
    // because this is a user error rather than a system error.
    if (this->monitoredServices_.size() == 0)
    {
        atlasagent::Logger()->error(
            "No systemd services to monitor, but configs were provided."
            "Configured maximum services to monitor: {}.",
            this->maxMonitoredServices);
        return true;
    }

    return this->update_metrics();
}
