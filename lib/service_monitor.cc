#include "service_monitor.h"
#include "util.h"
#include <filesystem>
#include <regex>

template class ServiceMonitor<atlasagent::TaggingRegistry>;

struct DBusConstants {
  // Service and path constants
  static constexpr auto service = "org.freedesktop.systemd1";
  static constexpr auto path = "/org/freedesktop/systemd1";

  // Manager interface constants
  static constexpr auto interface = "org.freedesktop.systemd1.Manager";
  static constexpr auto MethodListUnits = "ListUnits";
  static constexpr auto MethodGetUnit = "GetUnit";

  // Properties interface constants
  static constexpr auto propertiesInterface = "org.freedesktop.DBus.Properties";
  static constexpr auto MethodGet = "Get";

  // Unit interface constants
  static constexpr auto unitInterface = "org.freedesktop.systemd1.Unit";
  static constexpr auto PropertyActiveState = "ActiveState";
  static constexpr auto PropertyLoadState = "LoadState";
  static constexpr auto PropertySubState = "SubState";

  // Service interface constants
  static constexpr auto serviceInterface = "org.freedesktop.systemd1.Service";
  static constexpr auto PropertyMainPID = "MainPID";
};

std::optional<std::vector<Unit>> list_all_units() try {
  // Create system bus connection
  auto connection = sdbus::createSystemBusConnection();

  // Create the proxy
  auto proxy = sdbus::createProxy(*connection, sdbus::ServiceName{DBusConstants::service}, sdbus::ObjectPath{DBusConstants::path});

  // Create method call message
  auto methodCall = proxy->createMethodCall(sdbus::InterfaceName{DBusConstants::interface}, sdbus::MethodName{DBusConstants::MethodListUnits});

  std::vector<Unit> units{};
  proxy->callMethod(sdbus::MethodName{DBusConstants::MethodListUnits}).onInterface(sdbus::InterfaceName{DBusConstants::interface}).storeResultsTo(units);
  return units;
} catch (const sdbus::Error& e) {
  atlasagent::Logger()->error("D-Bus Exception: {} with message: {}", e.getName(), e.getMessage());
  return std::nullopt;
} catch (const std::exception& e) {
  atlasagent::Logger()->error("list_all_units exception: {}", e.what());
  return std::nullopt;
}

std::optional<ServiceProperties> get_service_properties(const std::string& serviceName) try {
  // Connect to the system bus
  auto connection = sdbus::createSystemBusConnection();

  // First get the unit object path using Manager.GetUnit method
  sdbus::ObjectPath unitObjectPath;
  auto managerProxy = sdbus::createProxy(*connection, sdbus::ServiceName{DBusConstants::service},sdbus::ObjectPath{DBusConstants::path});

  // Get the proper object path for the unit
  managerProxy->callMethod(DBusConstants::MethodGetUnit).onInterface(DBusConstants::interface).withArguments(serviceName).storeResultsTo(unitObjectPath);

  // Create a proxy to the service object with the correct path
  auto proxy = sdbus::createProxy(*connection, sdbus::ServiceName{DBusConstants::service}, unitObjectPath);

  // Get MainPID property
  sdbus::Variant mainPidVariant;
  proxy->callMethod(DBusConstants::MethodGet).onInterface(DBusConstants::propertiesInterface).withArguments(DBusConstants::serviceInterface, DBusConstants::PropertyMainPID).storeResultsTo(mainPidVariant);

  // Get ActiveState property
  sdbus::Variant activeStateVariant;
  proxy->callMethod(DBusConstants::MethodGet).onInterface(DBusConstants::propertiesInterface).withArguments(DBusConstants::unitInterface, DBusConstants::PropertyActiveState).storeResultsTo(activeStateVariant);

  // Get SubState property
  sdbus::Variant subStateVariant;
  proxy->callMethod(DBusConstants::MethodGet).onInterface(DBusConstants::propertiesInterface).withArguments(DBusConstants::unitInterface, DBusConstants::PropertySubState).storeResultsTo(subStateVariant);
  

  uint32_t mainPid = mainPidVariant.get<uint32_t>();
  std::string activeState = activeStateVariant.get<std::string>();
  std::string subState = subStateVariant.get<std::string>();

  return ServiceProperties{serviceName, activeState, subState, mainPid};
} catch (const sdbus::Error& e) {
  atlasagent::Logger()->error("D-Bus Exception: {} with message: {}", e.getName(), e.getMessage());
  return std::nullopt;
} catch (const std::exception& e) {
  atlasagent::Logger()->error("get_service_properties exception: {}", e.what());
  return std::nullopt;
}

std::optional<std::vector<std::regex>> parse_regex_config_file(const char* configFilePath) {
  std::optional<std::vector<std::string>> stringPatterns = atlasagent::read_file(configFilePath);
  if (stringPatterns.has_value() == false) {
    atlasagent::Logger()->error("Error reading config file {}", configFilePath);
    return std::nullopt;
  }

  if (stringPatterns.value().empty()) {
    atlasagent::Logger()->debug("Empty config file {}", configFilePath);
    return std::nullopt;
  }

  std::vector<std::regex> regexPatterns{};
  for (const auto& regex_pattern : stringPatterns.value()) {
    if (regex_pattern.empty()) {
      continue;
    }
    try {
      regexPatterns.emplace_back(regex_pattern);
    } catch (const std::regex_error& e) {
      atlasagent::Logger()->error("Exception: {}, for regex:{}, in config file {}",e.what(), regex_pattern, configFilePath);
      return std::nullopt;
    }
  }
  return regexPatterns;
}

std::optional<std::vector<std::regex>> parse_service_monitor_config_directory(const char* directoryPath) {
  if (std::filesystem::exists(directoryPath) == false || std::filesystem::is_directory(directoryPath) == false) {  
    atlasagent::Logger()->error("Invalid service monitor config directory {}", directoryPath);
    return std::nullopt;
  }

  std::vector<std::regex> allRegexPatterns{};
  for (const auto& file : std::filesystem::recursive_directory_iterator(directoryPath)) {
    auto regexExpressions = parse_regex_config_file(file.path().c_str());

    if (regexExpressions.has_value() == false){
      atlasagent::Logger()->error("Could not add regex expressions from file {}", file.path().c_str());
      continue;
    }

    for (const auto& regex : regexExpressions.value()){
      allRegexPatterns.emplace_back(regex);
    }
  }

  return allRegexPatterns;
}

std::optional<std::vector<std::string>> get_proc_fields(pid_t pid) {
  std::filesystem::path procPath = std::filesystem::path(ServiceMonitorConstants::ProcPath) / std::to_string(pid) / ServiceMonitorConstants::StatPath;
  auto pidStats = atlasagent::read_file(procPath.string().c_str());
  return pidStats;
}

std::optional<ProcessTimes> get_process_times(pid_t pid) {
  auto pidStats = get_proc_fields(pid);
  if (pidStats.has_value() == false) {
    return std::nullopt;
  }

  unsigned long uTime = std::stoul(pidStats.value()[ServiceMonitorConstants::uTimeIndex]);
  unsigned long sTime = std::stoul(pidStats.value()[ServiceMonitorConstants::sTimeIndex]);
  return ProcessTimes{uTime, sTime};
}

std::optional<unsigned long> get_rss(pid_t pid) {
  auto pidStats = get_proc_fields(pid);
  if (pidStats.has_value() == false) {
    return std::nullopt;
  }

  unsigned long rss = std::stoul(pidStats.value()[ServiceMonitorConstants::rssIndex]);
  return rss;
}

std::optional<unsigned long long> get_total_cpu_time() {
  auto cpuStats = atlasagent::read_file(ServiceMonitorConstants::ProcStatPath);
  if (cpuStats.has_value() == false || cpuStats.value().empty()) {
    atlasagent::Logger()->error("Error reading {}" , ServiceMonitorConstants::ProcStatPath);
    return std::nullopt;
  }

  const auto& aggregateStats = cpuStats.value()[ServiceMonitorConstants::AggregateCpuIndex];

  unsigned long long totalCpuTime{0};
  for (unsigned int i = ServiceMonitorConstants::AggregateCpuDataIndex; i < aggregateStats.size(); i++) {
    totalCpuTime += aggregateStats.at(i);
  }

  return totalCpuTime;
}

std::optional<unsigned int> get_number_fds(pid_t pid) try {
  auto path = std::filesystem::path(ServiceMonitorConstants::ProcPath) / std::to_string(pid) / ServiceMonitorConstants::FdPath;

  int fd_count = 0;
  for (const auto& entry : std::filesystem::directory_iterator(path)) {
    // Only count symbolic links, which represent open file descriptors
    if (entry.is_symlink()) {
      ++fd_count;
    }
  }
  return fd_count;

} catch (const std::exception& e) {
  atlasagent::Logger()->error("get_number_fds execption: {} ", e.what());
  return std::nullopt;
}

double calculate_cpu_usage(unsigned long long oldCpuTime, unsigned long long newCpuTime, ProcessTimes oldProcessTime, ProcessTimes newProcessTime, unsigned int numCores){
  unsigned long long processTimeDelta = (newProcessTime.uTime - oldProcessTime.uTime) + (newProcessTime.sTime - oldProcessTime.uTime);
  unsigned long long cpuTimeDelta = newCpuTime - oldCpuTime;


  double cpuUsage = 100.0 * (double)processTimeDelta / (double)cpuTimeDelta * numCores;

  return cpuUsage;
}

std::optional<unsigned int> get_cpu_cores() {
  auto cpuInfo = atlasagent::read_file(ServiceMonitorConstants::CpuInfoPath);
  if (cpuInfo.has_value() == false){
    return std::nullopt;
  }
  
  unsigned int cpuCores{0};
  for (const auto& line : cpuInfo.value()){
    if (line.substr(0, 9) == "processor") {
      cpuCores++;
    }
  }
  return cpuCores;
}


std::unordered_map<pid_t, ProcessTimes> create_pid_map(const std::vector<ServiceProperties>& services){

  std::unordered_map<pid_t, ProcessTimes> pidMap{};
  for (const auto& service : services){
    auto pid = service.mainPid;
    auto processTimes = get_process_times(pid);
    if (processTimes.has_value() == false){
      atlasagent::Logger()->error("Error getting {} process times", service.name);
      continue;
    }
    pidMap[pid] = processTimes.value();
  }
  return pidMap;
}

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
      std::cout << "service.rss " << serviceRSS.value() << " service " << service.name << std::endl;
    }
    if (serviceFds.has_value()){
      std::cout << "service.fds " << serviceFds.value() << " service " << service.name << std::endl;
    }
    if (cpuUsage.has_value()){
      std::cout << "service.cpu_usage " << cpuUsage.value() << " service " << service.name << std::endl;
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


  this->currentProcessTimes = std::move(newProcessMap);
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
