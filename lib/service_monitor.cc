#include "service_monitor.h"
#include "util.h"


template class ServiceMonitor<atlasagent::TaggingRegistry>;

template <class Reg>
bool ServiceMonitor<Reg>::init_monitored_services() {
  // CPU Cores is used to calculate the % CPU usage
  auto cpuCores = get_cpu_cores();
  if (cpuCores.has_value() == false){
    atlasagent::Logger()->error("Error determining the number of cpu cores");
    return false;
  }
  this->numCpuCores = cpuCores.value();

  auto all_units = list_all_units();
  if (all_units.has_value() == false) {
    atlasagent::Logger()->error("Error gathering all units from Systemd");
    return false;
  }

  for (const auto& regex : this->config_) {
    for (const auto& unit : all_units.value()) {
      if (std::regex_search(std::get<0>(unit), regex)) {
        this->monitoredServices_.insert(std::get<0>(unit).c_str());
      }
    }
  }
  // Units were retrieved. initSuccess is now true because monitoredServices now initialized
  // with pattern matched services. Metrics will be computed for those services on the subsequent
  // iteration
  this->initSuccess = true;
  if (this->monitoredServices_.size() == 0) {
    atlasagent::Logger()->error("User Error: Monitor Service config provided but no services matched pattern");
  }
  return true;
}

template <class Reg>
bool ServiceMonitor<Reg>::updateMetrics() {

  std::vector<ServiceProperties> servicesStates{};
  servicesStates.reserve(monitoredServices_.size());

  for (const auto service : monitoredServices_){
    auto serviceState = get_service_properties(service);
    
    if (serviceState.has_value() == false){
      atlasagent::Logger()->error("Failed to get {} properties", service);
      continue;
    }
    servicesStates.emplace_back(serviceState.value());
  }

  auto newProcessTimes = create_pid_map(servicesStates);
  auto newCpuTime = get_total_cpu_time();


  for (const auto& service : servicesStates){
    auto serviceRSS = get_rss(service.mainPid);
    auto serviceFds = get_number_fds(service.mainPid);
    
    std::optional<double> cpuUsage {std::nullopt};
    if (newCpuTime.has_value() && currentProcessTimes.find(service.mainPid) != currentProcessTimes.end() && newProcessTimes.find(service.mainPid) != newProcessTimes.end()){
      auto newProcessTime = newProcessTimes[service.mainPid];
      auto oldProcessTime = currentProcessTimes[service.mainPid];
      cpuUsage.emplace(calculate_cpu_usage(currentCpuTime, newCpuTime.value(), oldProcessTime, newProcessTime, this->numCpuCores));
    }
    
    if (serviceRSS.has_value()){
      //std::cout << "service.rss " << serviceRSS.value() << " service " << service.name << std::endl;
      detail::counter(this->registry_, ServiceMonitorConstants::rssName, service.name.c_str())->Add(serviceRSS.value());
    }
    if (serviceFds.has_value()){
      //std::cout << "service.fds " << serviceFds.value() << " service " << service.name << std::endl;
      detail::counter(this->registry_, ServiceMonitorConstants::fdsName, service.name.c_str())->Add(serviceFds.value());
    }
    if (cpuUsage.has_value()){
      //std::cout << "service.cpu_usage " << cpuUsage.value() << " service " << service.name << std::endl;
      detail::gauge(this->registry_, ServiceMonitorConstants::fdsName, service.name.c_str())->Set(cpuUsage.value());
    }
  }


  // Now we update the currentProcessTimes and currentCpuTime. This is done after all metrics are computed.
  // If we failed to get new ProcessTimes for a few of the services, thats okay. We will use the times for the processes
  // we were able to retrieve. If we failed to get the cpu time, this will prevent us from updating the currentProcessTimes.
  // This is because we will not be able to calculate the cpu usage for the services on the next iteration.
  if (newCpuTime.has_value() == false){
    this->currentCpuTime = 0;
    this->currentProcessTimes.clear();
    return false;
  }


  this->currentProcessTimes = std::move(newProcessTimes);
  if (newCpuTime.has_value() == true){
    this->currentCpuTime = newCpuTime.value();
  }
  return true;
}

// To Do: Shutdown module if no services are being monitored
template <class Reg>
bool ServiceMonitor<Reg>::gather_metrics() {
  if (this->initSuccess == false) {
    bool success = this->init_monitored_services();
    if (success == false) {
      return false;
    }
  }

  // To Do: We initialized but there are no services to monitor (no patterns matched)
  // This would be user error. We should create a way to remove this Collector from 60 sec
  // collection. I have logged this error in init_monitored_services. Returning true because
  // this is not a failure but a user error.
  if (this->monitoredServices_.size() == 0) {
    return true;
  }

  if (false == this->updateMetrics()) {
    return false;
  }

  return true;
}
