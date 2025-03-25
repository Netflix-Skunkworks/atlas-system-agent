#include "service_monitor.h"
#include <sdbus-c++/sdbus-c++.h>
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

// Define the Unit structure matching the D-Bus signature (ssssssouso)
using Unit =
    sdbus::Struct<std::string, std::string, std::string, std::string, std::string, std::string,
                  sdbus::ObjectPath, uint32_t, std::string, sdbus::ObjectPath>;

struct ServiceProperties {
  uint32_t mainPid;
  std::string activeState;
};

void list_all_units() try {
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

  return;
} catch (const sdbus::Error& e) {
  std::cerr << "D-Bus error: " << e.getName() << " with message: " << e.getMessage() << std::endl;
  return;
} catch (const std::exception& e) {
  std::cerr << "Error: " << e.what() << std::endl;
  return;
}

void GetServiceProperties(const std::string& serviceName) try {
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
} catch (const sdbus::Error& e) {
  std::cerr << "D-Bus error: " << e.getName() << " - " << e.getMessage() << std::endl;
} catch (const std::exception& e) {
  std::cerr << "Error: " << e.what() << std::endl;
}

std::optional<std::vector<std::regex>> parse_service_monitor_config(const char* configPath) {
  std::optional<std::vector<std::string>> stringPatterns = atlasagent::read_file(configPath);

  if (stringPatterns.has_value() == false) {
    atlasagent::Logger()->error("Error reading service_monitor config.");
  }

  if (stringPatterns.value().empty()) {
    atlasagent::Logger()->debug("Debug service_monitor config present but empty");
    return std::nullopt;
  }

  std::cout << "StringPatterns Size: " << stringPatterns.value().size() << std::endl;
  std::vector<std::regex> regexPatterns(stringPatterns.value().size());

  for (const auto& regex_pattern : stringPatterns.value()) {
    try {
      std::cout << "Pattern: " << regex_pattern << std::endl;
      regexPatterns.emplace_back(regex_pattern);
    } catch (const std::regex_error& e) {
      atlasagent::Logger()->error("Exception thrown creating regex:{} {}", regex_pattern, e.what());
      return std::nullopt;
    }
  }

  return regexPatterns;
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

template <class Reg>
void ServiceMonitor<Reg>::init_monitored_services() {
  // DBus dbus{};

  // Todo: Rename this to get AllUnits
  // auto all_units = dbus.GetAllServices();
  // if (all_units.has_value() == false) {
  //  return;
  //}

  // Units were retrieved init is now success because monitoredServices now initialized
  // this->initSuccess = true;

  // for (const auto& regex : this->config_) {
  //   for (const auto& unit : all_units.value()) {
  //     if (std::regex_search(unit.name, regex)) {
  //       this->monitoredServices_.insert(unit.name.c_str());
  //     }
  //   }
  // }
}

template <class Reg>
bool ServiceMonitor<Reg>::updateMetrics() {
  return true;
}

// Todo change to optional bool because if size is 0 not a failure but user error
// Maybe make a way to shutdown this module.
template <class Reg>
bool ServiceMonitor<Reg>::gather_metrics() {
  // Pattern match to find the services we want to monitor
  if (this->initSuccess == false) {
    this->init_monitored_services();
  }

  // We initialized but there are no services to monitor
  // No services matched the pattern
  if (this->monitoredServices_.size() == 0) {
    return false;
  }

  if (false == this->updateMetrics()) {
    return false;
  }

  return true;
}
