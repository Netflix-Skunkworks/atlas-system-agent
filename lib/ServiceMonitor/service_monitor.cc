#include "service_monitor.h"
#include "../Util/util.h"

template class ServiceMonitor<atlasagent::TaggingRegistry>;

// The constructor takes a registry, a vector of regex patterns, and a maximum number of services to monitor.
// If the maximum number of services is not equal to the default value, it logs a message indicating the custom value.
template <typename Reg>
ServiceMonitor<Reg>::ServiceMonitor(Reg* registry, std::vector<std::regex> config, unsigned int max_services)
    : registry_{registry},
      config_{std::move(config)},
      maxMonitoredServices{max_services == ServiceMonitorConstants::DefaultMonitoredServices ? ServiceMonitorConstants::DefaultMonitoredServices : max_services} {
  if (this->maxMonitoredServices != ServiceMonitorConstants::DefaultMonitoredServices) {
    atlasagent::Logger()->info("Custom max monitored services value set: {} (default is {})",
                               maxMonitoredServices, ServiceMonitorConstants::DefaultMonitoredServices);
  }
}

template <class Reg>
bool ServiceMonitor<Reg>::init_monitored_services() try {
  // Read & set the CPU core count which is used to calculate CPU usage 
  auto cpuCores = get_cpu_cores();
  if (cpuCores.has_value() == false) {
    atlasagent::Logger()->error("Error determining the number of cpu cores");
    return false;
  }
  this->numCpuCores = cpuCores.value();

  // Read & set the page size in bytes to convert rss from pages to bytes
  this->pageSize = sysconf(_SC_PAGESIZE);
  if (this->pageSize == -1) {
    atlasagent::Logger()->error("Error getting page size");
    return false;
  }

  // Get all the systemd units on the system
  auto all_units = list_all_units();
  if (all_units.has_value() == false) {
    atlasagent::Logger()->error("Error gathering all units from Systemd");
    return false;
  }

  // Iterate through all the units and check if the unit name matches any of the regex patterns in the config
  // If it does, add the unit name to the monitoredServices_ as long as we are beneath the maxMonitoredServices threshold
  for (const auto& unit : all_units.value()) {
    const auto& unit_name = std::get<0>(unit);

    bool matched = std::any_of(
        config_.begin(), config_.end(),
        [&unit_name](const std::regex& regex) { return std::regex_search(unit_name, regex); });

    if (matched == false) {
      continue;
    }

    if (monitoredServices_.size() >= maxMonitoredServices) {
      atlasagent::Logger()->info(
          "Reached maximum number of monitored services ({}). Ignoring service {} and remaining "
          "services.",
          maxMonitoredServices, unit_name);
      break;
    }

    monitoredServices_.emplace_back(unit_name);
    atlasagent::Logger()->info("Added service {} to monitoring list ({}/{})", unit_name,
                               monitoredServices_.size(), this->maxMonitoredServices);
  }

  // Units were retrieved. initSuccess is now true because monitoredServices now initialized
  // with pattern matched services.
  this->initSuccess = true;
  if (this->monitoredServices_.empty() == true) {
    atlasagent::Logger()->error(
        "User Error: Monitor Service config provided but no services matched pattern");
  }
  return true;
} catch (const std::exception& e) {
  atlasagent::Logger()->error("Exception: {} in init_monitored_services", e.what());
  return false;
}

template <class Reg>
bool ServiceMonitor<Reg>::update_metrics() try {
  // Tracks non-critical metric update failures
  bool success = true;

  // Iterate over all monitored services that match the config, retrieving their properties
  // (main PID, active state, substate). If a service's properties can't be retrieved, update succes,
  // log the error and continue.
  std::vector<ServiceProperties> servicesStates{};
  servicesStates.reserve(monitoredServices_.size());
  for (const auto& service : monitoredServices_) {
    auto serviceState = get_service_properties(service);

    if (serviceState.has_value() == false) {
      atlasagent::Logger()->error("Failed to get {} properties", service);
      success = false;
      continue;
    }
    servicesStates.emplace_back(serviceState.value());
  }

  // If we couldn't get the properties for any of the services we are monitoring, log an error and return
  if (servicesStates.empty()) {
    atlasagent::Logger()->error("Could not get properties for any monitored services");
    return false;
  }

  // Some services may fail to return properties, but this isn’t a full failure.
  // We will still update metrics for the services we were able to get the properties for. 
  
  // Get the new process times for the services and the new CPU time
  // If we fail to get the new CPU time, we will not be able to calculate the CPU usage for the 
  // current iteration and the next iteration
  auto newProcessTimes = create_pid_map(servicesStates);
  auto newCpuTime = get_total_cpu_time();
  
  // Iterate throught the services and update the metrics for each service
  for (const auto& service : servicesStates) {

    const auto newServiceState = fmt::format("{}.{}", service.activeState, service.subState);
    detail::gaugeServiceState(this->registry_, ServiceMonitorConstants::ServiceStatusName, service.name.c_str(), newServiceState.c_str())->Set(1);

    // If the service is not active and running, we do not want to send metrics that depend on /proc/[pid]
    // The systemd service variable 'main pid' remains set even if a process/service is not running.
    if (service.activeState != ServiceMonitorUtilConstants::Active || service.subState != ServiceMonitorUtilConstants::Running) {
      atlasagent::Logger()->info("Service {} is not active and running, not sending metrics dependent on /proc/[pid]", service.name);
      continue;
    }

    auto serviceRSS = get_rss(service.mainPid);
    auto serviceFds = get_number_fds(service.mainPid);

    // Only calculate the cpu usage for a service if we have the new cpu time, the processes previous time,
    // and the new processes time. There is no need to check that the old cpu time (this->currentCpuTime) is not 0.
    // This is because if on the previous iteration we failed to get the cpu time, the previous process time map is empty.
    // Very unlikely but it is possible that two services die within the same 60 second interval, and then be reassigned 
    // each others pids. We could fix this by also tracking the service name in the process time map.
    std::optional<double> cpuUsage{std::nullopt};
    if (newCpuTime.has_value() && currentProcessTimes.find(service.mainPid) != currentProcessTimes.end() &&
        newProcessTimes.find(service.mainPid) != newProcessTimes.end()) {
      auto newProcessTime = newProcessTimes[service.mainPid];
      auto oldProcessTime = currentProcessTimes[service.mainPid];
      cpuUsage.emplace(calculate_cpu_usage(currentCpuTime, newCpuTime.value(), oldProcessTime,
                                            newProcessTime, this->numCpuCores));
    }

    // If we failed to get the RSS, FDs, or CPU usage for a service, log the error and set success to false
    // We check currentProcessTimes to see if we have the old process time for a service b/c we dont want to unnecessarily log
    // erros when calculating cpu usage. Cpu usage requires two 60 second iterations in order to calculate. Without this check 
    // we would unecessarily log errors during the first iteration on startup, or when a new process is started for the first time. 
    if (serviceRSS.has_value() == false || serviceFds.has_value() == false || 
    (currentProcessTimes.find(service.mainPid) != currentProcessTimes.end() && cpuUsage.has_value() == false)) {
      success = false;
      atlasagent::Logger()->error("Failed to get metric(s) for {}", service.name);
    }
    if (serviceRSS.has_value()) {
      detail::gauge(this->registry_, ServiceMonitorConstants::RssName, service.name.c_str())
          ->Set(serviceRSS.value() * this->pageSize);
    }
    if (serviceFds.has_value()) {
      detail::gauge(this->registry_, ServiceMonitorConstants::FdsName, service.name.c_str())
          ->Set(serviceFds.value());
    }
    if (cpuUsage.has_value()) {
      detail::gauge(this->registry_, ServiceMonitorConstants::CpuUsageName, service.name.c_str())
          ->Set(cpuUsage.value());
    }
  }

  // Update currentProcessTimes and currentCpuTime. If we failed to get process times for some 
  // services, that's fine—we'll compute CPU usage for the ones we do have next time.
  // However, if cpuTime retrieval fails, we can’t compute usage for any service, so we reset 
  // currentProcessTimes and set currentCpuTime to 0.
  if (newCpuTime.has_value() == false) {
    this->currentCpuTime = 0;
    this->currentProcessTimes.clear();
    return false;
  }

  this->currentProcessTimes = std::move(newProcessTimes);
  this->currentCpuTime = newCpuTime.value();
  return success;
} catch (const std::exception& e) {
  atlasagent::Logger()->error("Exception: {} in service moniotr update_metrics", e.what());
  return false;
}

// TODO: Shutdown module if no services are being monitored
template <class Reg>
bool ServiceMonitor<Reg>::gather_metrics() {
  // To begin sending metrics, we must first determine the core count, page size, and determine all the systemd 
  // services that match our config. Once init_monitored_services() completes successfully, we can start 
  // sending metrics to spectatord. If we fail to determine these values, we will continue to retry until success.
  if (this->initSuccess == false && this->init_monitored_services() == false) {
    return false;
  }

  // TODO: We successfully initialized but none of the services on the system matched any of
  // the regex patterns in our configs. This results in no services to monitor.
  // We should create a way to remove this collector from the 60 sec
  // collection interval. This error is logged in init_monitored_services. We return true here 
  // because this is a user error rather than a system error.
  if (this->monitoredServices_.size() == 0) {
    atlasagent::Logger()->error(
        "No systemd services to monitor, but configs were provided."
        "Configured maximum services to monitor: {}.",
        this->maxMonitoredServices);
    return true;
  }

  return this->update_metrics();
}
