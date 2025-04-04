#include "service_monitor.h"
#include "util.h"

template class ServiceMonitor<atlasagent::TaggingRegistry>;

template <typename Reg>
ServiceMonitor<Reg>::ServiceMonitor(Reg* registry, std::vector<std::regex> config, unsigned int max_services)
    : registry_{registry},
      config_{std::move(config)},
      maxMonitoredServices{max_services == 0 ? ServiceMonitorConstants::MaxMonitoredServices
                                             : max_services} {
  if (this->maxMonitoredServices != ServiceMonitorConstants::MaxMonitoredServices) {
    atlasagent::Logger()->info(
        "Custom max monitored services value set: {} (default is {})", maxMonitoredServices,
        ServiceMonitorConstants::MaxMonitoredServices);
  }
}

template <class Reg>
bool ServiceMonitor<Reg>::init_monitored_services() try {
  // CPU Cores is used to calculate the % CPU usage
  auto cpuCores = get_cpu_cores();
  if (cpuCores.has_value() == false) {
    atlasagent::Logger()->error("Error determining the number of cpu cores");
    return false;
  }
  this->numCpuCores = cpuCores.value();

  // Get the page size (used to convert rss to bytes)
  this->pageSize = sysconf(_SC_PAGESIZE);
  if (this->pageSize == -1) {
    atlasagent::Logger()->error("Error getting page size");
    return false;
  }

  auto all_units = list_all_units();
  if (all_units.has_value() == false) {
    atlasagent::Logger()->error("Error gathering all units from Systemd");
    return false;
  }

  for (const auto& unit : all_units.value()) {
    const auto& unit_name = std::get<0>(unit);

    bool matched = std::any_of(config_.begin(), config_.end(),[&unit_name](const std::regex& regex) {
      return std::regex_search(unit_name, regex);});

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

    monitoredServices_.insert(unit_name);
    atlasagent::Logger()->debug("Added service {} to monitoring list ({}/{})", unit_name,
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
  std::vector<ServiceProperties> servicesStates{};
  servicesStates.reserve(monitoredServices_.size());
  bool success = true;

  for (const auto& service : monitoredServices_) {
    auto serviceState = get_service_properties(service);

    if (serviceState.has_value() == false) {
      atlasagent::Logger()->error("Failed to get {} properties", service);
      success = false;
      continue;
    }
    servicesStates.emplace_back(serviceState.value());
  }

  if (servicesStates.empty()) {
    atlasagent::Logger()->error("Could not get properties for any monitored services");
    return false;
  }

  // It is possible that we could not get the properties for some of the services we are monitoring.
  // This is not a complete failure. We will still update the metrics for the services we were
  // able to get the properties for. This is why we iterate over serviceStates and not
  // monitoredServices_ to update the metrics.
  auto newProcessTimes = create_pid_map(servicesStates);
  auto newCpuTime = get_total_cpu_time();
  for (const auto& service : servicesStates) {
    auto serviceRSS = get_rss(service.mainPid);
    auto serviceFds = get_number_fds(service.mainPid);

    std::optional<double> cpuUsage{std::nullopt};  // this must be an optional because reading the
                                                   // new cpu time could have failed
    if (newCpuTime.has_value() &&
        currentProcessTimes.find(service.mainPid) != currentProcessTimes.end() &&
        newProcessTimes.find(service.mainPid) != newProcessTimes.end()) {
      auto newProcessTime = newProcessTimes[service.mainPid];
      auto oldProcessTime = currentProcessTimes[service.mainPid];
      cpuUsage.emplace(calculate_cpu_usage(currentCpuTime, newCpuTime.value(), oldProcessTime,
                                           newProcessTime, this->numCpuCores));
    }

    if (serviceRSS.has_value() == false || serviceFds.has_value() == false ||
        cpuUsage.has_value() == false) {
      success = false;
      atlasagent::Logger()->error("Failed to get {} metrics", service.name);
    }
    if (serviceRSS.has_value()) {
      detail::gauge(this->registry_, ServiceMonitorConstants::rssName, service.name.c_str())
          ->Set(serviceRSS.value() * this->pageSize);
    }
    if (serviceFds.has_value()) {
      detail::gauge(this->registry_, ServiceMonitorConstants::fdsName, service.name.c_str())
          ->Set(serviceFds.value());
    }
    if (cpuUsage.has_value()) {
      detail::gauge(this->registry_, ServiceMonitorConstants::cpuUsageName, service.name.c_str())
          ->Set(cpuUsage.value());
    }
    detail::gaugeServiceState(this->registry_, ServiceMonitorConstants::serviceStatusName,
                              service.name.c_str(), service.activeState.c_str(),
                              service.subState.c_str())
        ->Set(1);
  }

  // Now we update the currentProcessTimes and currentCpuTime.
  // If we failed to get new ProcessTimes for a few of the services, thats okay on the next
  // iteration we will compute cpu usage for the services we have. If we failed to get the new cpu
  // time, we will not be able to calculate the cpu usage for any of the services on the next
  // iteration. So we need to set the currentProcessTimes to empty and currentCpuTime to 0.
  if (newCpuTime.has_value() == false) {
    this->currentCpuTime = 0;
    this->currentProcessTimes.clear();
    return false;
  }

  this->currentProcessTimes = std::move(newProcessTimes);
  this->currentCpuTime = newCpuTime.value();
  return success;
} catch (const std::exception& e) {
  atlasagent::Logger()->error("Exception: {} in servic moniotr update_metrics", e.what());
  return false;
}

// To Do: Shutdown module if no services are being monitored
template <class Reg>
bool ServiceMonitor<Reg>::gather_metrics() {
  if (this->initSuccess == false) {
    if (this->init_monitored_services() == false) {
      return false;
    }
  }

  // To Do: We initialized but there are no services to monitor (no patterns matched)
  // This would be user error. We should create a way to remove this Collector from 60 sec
  // collection. I have logged this error in init_monitored_services. Returning true because
  // this is not a failure but a user error.
  if (this->monitoredServices_.size() == 0) {
    atlasagent::Logger()->error(
        "No services are being monitored, monitored services list is empty."
        "Configured maximum services to monitor: {}.",
        this->maxMonitoredServices);
    return true;
  }

  if (false == this->update_metrics()) {
    return false;
  }

  return true;
}
