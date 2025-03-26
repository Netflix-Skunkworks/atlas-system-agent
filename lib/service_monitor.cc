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
  static constexpr auto MethodGetAll = "GetAll";

  // Unit interface constants
  static constexpr auto unitInterface = "org.freedesktop.systemd1.Unit";
  static constexpr auto PropertyActiveState = "ActiveState";
  static constexpr auto PropertyLoadState = "LoadState";
  static constexpr auto PropertySubState = "SubState";
  static constexpr auto PropertyDescription = "Description";

  // Service interface constants
  static constexpr auto serviceInterface = "org.freedesktop.systemd1.Service";
  static constexpr auto PropertyMainPID = "MainPID";
};

std::optional<std::vector<Unit>> list_all_units() try {
  // Create system bus connection
  auto connection = sdbus::createSystemBusConnection();

  // Create the proxy
  auto proxy = sdbus::createProxy(*connection, sdbus::ServiceName{DBusConstants::service},
                                  sdbus::ObjectPath{DBusConstants::path});

  // Create method call message
  auto methodCall = proxy->createMethodCall(sdbus::InterfaceName{DBusConstants::interface},
                                            sdbus::MethodName{DBusConstants::MethodListUnits});

  std::vector<Unit> units;
  proxy->callMethod(sdbus::MethodName{DBusConstants::MethodListUnits})
      .onInterface(sdbus::InterfaceName{DBusConstants::interface})
      .storeResultsTo(units);

  std::cout << "Number of units: " << units.size() << std::endl;

  for (size_t i = 0; i < std::min(units.size(), size_t(5)); i++) {
    const auto& unit = units[i];
    std::cout << "Name: " << std::get<0>(unit) << "\n"
              << "Description: " << std::get<1>(unit) << "\n"
              << "Load State: " << std::get<2>(unit) << "\n"
              << "Active State: " << std::get<3>(unit) << "\n"
              << "Sub State: " << std::get<4>(unit) << "\n"
              << "Followed Unit" << std::get<5>(unit) << "\n"
              << "Object Path: " << std::get<6>(unit) << "\n"
              << "Job ID: " << std::get<7>(unit) << "\n"
              << "Job Type: " << std::get<8>(unit) << "\n"
              << "Job Path: " << std::get<9>(unit) << "\n"
              << "------------------------\n";
  }
  return units;
} catch (const sdbus::Error& e) {
  atlasagent::Logger()->error("D-Bus Exception: {} with message: {}", e.getName(), e.getMessage());
  return std::nullopt;
} catch (const std::exception& e) {
  atlasagent::Logger()->error("List All Units exception: {}", e.what());
  return std::nullopt;
}

std::optional<ServiceProperties> get_service_properties(const std::string& serviceName) try {
  // Connect to the system bus
  auto connection = sdbus::createSystemBusConnection();

  // First get the unit object path using Manager.GetUnit method
  sdbus::ObjectPath unitObjectPath;
  auto managerProxy = sdbus::createProxy(*connection, sdbus::ServiceName{DBusConstants::service},
                                         sdbus::ObjectPath{DBusConstants::path});

  // Get the proper object path for the unit
  managerProxy->callMethod(DBusConstants::MethodGetUnit)
      .onInterface(DBusConstants::interface)
      .withArguments(serviceName)
      .storeResultsTo(unitObjectPath);

  std::cout << "Unit path: " << unitObjectPath << std::endl;

  // Create a proxy to the service object with the correct path
  auto proxy =
      sdbus::createProxy(*connection, sdbus::ServiceName{DBusConstants::service}, unitObjectPath);

  // Get MainPID property
  sdbus::Variant mainPidVariant;
  proxy->callMethod(DBusConstants::MethodGet)
      .onInterface(DBusConstants::propertiesInterface)
      .withArguments(DBusConstants::serviceInterface, DBusConstants::PropertyMainPID)
      .storeResultsTo(mainPidVariant);

  // Get ActiveState property
  sdbus::Variant activeStateVariant;
  proxy->callMethod(DBusConstants::MethodGet)
      .onInterface(DBusConstants::propertiesInterface)
      .withArguments(DBusConstants::unitInterface, DBusConstants::PropertyActiveState)
      .storeResultsTo(activeStateVariant);

  // Get SubState property
  sdbus::Variant subStateVariant;
  proxy->callMethod(DBusConstants::MethodGet)
      .onInterface(DBusConstants::propertiesInterface)
      .withArguments(DBusConstants::unitInterface, DBusConstants::PropertySubState)
      .storeResultsTo(subStateVariant);
  
  

    
  uint32_t mainPid = mainPidVariant.get<uint32_t>();
  std::string activeState = activeStateVariant.get<std::string>();
  std::string subState = subStateVariant.get<std::string>();

  // Print all properties
  std::cout << "Service: " << serviceName << std::endl;
  std::cout << "MainPID: " << mainPid << std::endl;
  std::cout << "Active State: " << activeState << std::endl;
  std::cout << "Sub State: " << subState << std::endl;
  return std::nullopt;
} catch (const sdbus::Error& e) {
  std::cerr << "D-Bus error: " << e.getName() << " - " << e.getMessage() << std::endl;
  return std::nullopt;
} catch (const std::exception& e) {
  std::cerr << "Error: " << e.what() << std::endl;
  return std::nullopt;
}

std::optional<std::vector<std::regex>> parse_regex_config_file(const char* configFilePath) {
  std::optional<std::vector<std::string>> stringPatterns = atlasagent::read_file(configFilePath);
  if (stringPatterns.has_value() == false) {
    atlasagent::Logger()->error("Error reading service_monitor config.");
    return std::nullopt;
  }

  if (stringPatterns.value().empty()) {
    atlasagent::Logger()->debug("Debug service_monitor config present but empty");
    return std::nullopt;
  }
  std::vector<std::regex> regexPatterns{};
  for (const auto& regex_pattern : stringPatterns.value()) {
    try {
      regexPatterns.emplace_back(regex_pattern);
    } catch (const std::regex_error& e) {
      atlasagent::Logger()->error("Exception thrown creating regex:{} {}", regex_pattern, e.what());
      return std::nullopt;
    }
  }
  return regexPatterns;
}

std::optional<std::vector<std::regex>> parse_service_monitor_config_directory(const char* directoryPath) {
  if (std::filesystem::exists(directoryPath) == false || std::filesystem::is_directory(directoryPath) == false) {  
    atlasagent::Logger()->error("Invalid service monitor config directory path {}", directoryPath);
    return std::nullopt;
  }

  std::vector<std::regex> allRegexPatterns{};
  for (const auto& file : std::filesystem::recursive_directory_iterator(directoryPath)) {
    auto regexExpressions = parse_regex_config_file(file.path().c_str());

    if (regexExpressions.has_value() == false){
      atlasagent::Logger()->error("Error parsing service monitor config {}", file.path().c_str());
    }

    for (const auto& regex : regexExpressions.value()){
      allRegexPatterns.push_back(regex);
    }
  }
  return allRegexPatterns;

}

std::optional<std::vector<std::string>> get_proc_fields(pid_t pid) {
  std::string procPath = "/proc/" + std::to_string(pid) + "/stat";
  auto pidStats = atlasagent::read_file(procPath);
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
  if (cpuStats.has_value() == false ||
      cpuStats.value().size() >= ServiceMonitorConstants::AggregateCpuDataIndex) {
    return std::nullopt;
  }

  const auto& aggregateStats = cpuStats.value()[ServiceMonitorConstants::AggregateCpuIndex];

  unsigned long long totalCpuTime{0};
  for (unsigned int i = ServiceMonitorConstants::AggregateCpuDataIndex; i < aggregateStats.size();
       i++) {
    totalCpuTime += aggregateStats.at(i);
  }

  return totalCpuTime;
}

std::optional<unsigned int> get_number_fds(pid_t pid) try {
  const std::string path = "/proc/" + std::to_string(pid) + "/fd";

  int fd_count = 0;
  for (const auto& entry : std::filesystem::directory_iterator(path)) {
    // Only count symbolic links, which represent open file descriptors
    if (entry.is_symlink()) {
      ++fd_count;
    }
  }
  return fd_count;

} catch (const std::exception& e) {
  std::cerr << "Error accessing directory: " << e.what() << std::endl;
  return std::nullopt;
}

void calculate_cpu_usage(unsigned long long oldCpuTime, unsigned long long newCpuTime, ProcessTimes oldProcessTime, ProcessTimes newProcessTime, unsigned int numCores){
  unsigned long long processTimeDelta = (newProcessTime.uTime - oldProcessTime.uTime) + (newProcessTime.sTime - oldProcessTime.uTime);
  unsigned long long cpuTimeDelta = newCpuTime - oldCpuTime;


  double cpuUsage = 100.0 * (double)processTimeDelta / (double)cpuTimeDelta * numCores;

  std::cout << cpuUsage << std::endl;
  return;
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

template <class Reg>
void ServiceMonitor<Reg>::init_monitored_services() {
  // CPU Cores is used to calculate the % CPU usage
  auto cpuCores = get_cpu_cores();
  if (cpuCores.has_value() == false){
    atlasagent::Logger()->error("Error determining the number of cpu cores");
    return;
  }
  this->numCpuCores = cpuCores.value();

  auto all_units = list_all_units();
  if (all_units.has_value() == false) {
    atlasagent::Logger()->error("Error listing all units");
    return;
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
    atlasagent::Logger()->error(
        "User Error: Monitor Service config provided but no services matched pattern");
  }
}

template <class Reg>
bool ServiceMonitor<Reg>::updateMetrics() {













  /*
  for (const auto service : monitoredServices_){
    detail::guage(registry_, "cpu_usage")->Set(0);
    detail::guage(registry_, "rss")->Set(0);
    detail::guage(registry_, "fds")->Set(0)
  }
  */
  return true;
}


// To Do: Shutdown module if no services are being monitored
template <class Reg>
bool ServiceMonitor<Reg>::gather_metrics() {
  if (this->initSuccess == false) {
    this->init_monitored_services();
    return false;
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
