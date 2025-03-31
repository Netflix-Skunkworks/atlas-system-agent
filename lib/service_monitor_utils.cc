#include <filesystem>

#include "absl/strings/str_split.h"
#include "service_monitor_utils.h"
#include "util.h"

// The function returns a vector of Unit structs, which contain information about each unit.
std::optional<std::vector<Unit>> list_all_units() try {
  // Create system bus connection
  auto connection = sdbus::createSystemBusConnection();

  // Create the proxy
  auto proxy = sdbus::createProxy(*connection, sdbus::ServiceName{DBusConstants::service},
                                  sdbus::ObjectPath{DBusConstants::path});

  // Create method call message
  auto methodCall = proxy->createMethodCall(sdbus::InterfaceName{DBusConstants::interface},
                                            sdbus::MethodName{DBusConstants::MethodListUnits});

  std::vector<Unit> units{};
  proxy->callMethod(sdbus::MethodName{DBusConstants::MethodListUnits})
      .onInterface(sdbus::InterfaceName{DBusConstants::interface})
      .storeResultsTo(units);
  return units;
} catch (const sdbus::Error& e) {
  atlasagent::Logger()->error("D-Bus Exception: {} with message: {}", e.getName(), e.getMessage());
  return std::nullopt;
} catch (const std::exception& e) {
  atlasagent::Logger()->error("list_all_units exception: {}", e.what());
  return std::nullopt;
}

// The function returns a ServiceProperties struct containing the properties of the service.
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
      atlasagent::Logger()->error("Exception: {}, for regex:{}, in config file {}", e.what(),
                                  regex_pattern, configFilePath);
      return std::nullopt;
    }
  }
  return regexPatterns;
}

std::optional<std::vector<std::regex>> parse_service_monitor_config_directory(
    const char* directoryPath) {
  if (std::filesystem::exists(directoryPath) == false ||
      std::filesystem::is_directory(directoryPath) == false) {
    atlasagent::Logger()->error("Invalid service monitor config directory {}", directoryPath);
    return std::nullopt;
  }

  std::vector<std::regex> allRegexPatterns;
  // Create the regex pattern from the string constant
  std::regex configFileExtPattern(ServiceMonitorUtilConstants::ConfigFileExtPattern);
  
  for (const auto& file : std::filesystem::recursive_directory_iterator(directoryPath)) {
    // Check if the file matches our pattern before processing
    if (std::regex_match(file.path().filename().string(), configFileExtPattern) == false) {
      continue;
    }
    
    auto regexExpressions = parse_regex_config_file(file.path().c_str());

    if (regexExpressions.has_value() == false) {
      atlasagent::Logger()->error("Could not add regex expressions from file {}",
                                  file.path().c_str());
      continue;
    }

    for (const auto& regex : regexExpressions.value()) {
      allRegexPatterns.emplace_back(regex);
    }
  }

  return allRegexPatterns;
}

std::optional<std::vector<std::string>> get_proc_fields(pid_t pid) {
  std::filesystem::path procPath = std::filesystem::path(ServiceMonitorUtilConstants::ProcPath) /
                                   std::to_string(pid) / ServiceMonitorUtilConstants::StatPath;
  auto pidStats = atlasagent::read_file(procPath.string().c_str());
  return pidStats;
}

ProcessTimes parse_process_times(const std::vector<std::string>& pidStats) {
  auto statLine = pidStats.at(0);
  std::vector<std::string> statTokens = absl::StrSplit(statLine, ' ', absl::SkipWhitespace());
  
  // Check if we have enough tokens before accessing them
  if (statTokens.size() <= ServiceMonitorUtilConstants::sTimeIndex) {
    atlasagent::Logger()->error("Not enough tokens in proc stat file. Expected at least {}, got {}",
                               ServiceMonitorUtilConstants::sTimeIndex + 1, statTokens.size());
    return ProcessTimes{0, 0};  // Return zeros for invalid data
  }
  
  unsigned long uTime = std::stoul(statTokens[ServiceMonitorUtilConstants::uTimeIndex]);
  unsigned long sTime = std::stoul(statTokens[ServiceMonitorUtilConstants::sTimeIndex]);
  return ProcessTimes{uTime, sTime};
}

std::optional<ProcessTimes> get_process_times(pid_t pid) {
  auto pidStats = get_proc_fields(pid);
  if (pidStats.has_value() == false) {
    return std::nullopt;
  }
  return parse_process_times(pidStats.value());
}

unsigned long parse_rss(const std::vector<std::string>& pidStats) {
  auto statLine = pidStats.at(0);
  std::vector<std::string> statTokens = absl::StrSplit(statLine, ' ', absl::SkipWhitespace());
  
  // Check if we have enough tokens before accessing them
  if (statTokens.size() <= ServiceMonitorUtilConstants::rssIndex) {
    atlasagent::Logger()->error("Not enough tokens in proc stat file. Expected at least {}, got {}",
                               ServiceMonitorUtilConstants::rssIndex + 1, statTokens.size());
    return 0;  // Return zero for invalid data
  }
  
  return std::stoul(statTokens[ServiceMonitorUtilConstants::rssIndex]);
}

std::optional<unsigned long> get_rss(pid_t pid) {
  auto pidStats = get_proc_fields(pid);
  if (pidStats.has_value() == false) {
    return std::nullopt;
  }
  return parse_rss(pidStats.value());
}

unsigned long long parse_cpu_time(const std::vector<std::string>& cpuStats) {
  const auto& aggregateStats = cpuStats[ServiceMonitorUtilConstants::AggregateCpuIndex];
  std::vector<std::string> statTokens = absl::StrSplit(aggregateStats, ' ', absl::SkipWhitespace());

  unsigned long long totalCpuTime{0};
  for (unsigned int i = ServiceMonitorUtilConstants::AggregateCpuDataIndex;
       i < statTokens.size(); i++) {
    totalCpuTime += std::stoul(statTokens.at(i));
  }

  return totalCpuTime;
}

std::optional<unsigned long long> get_total_cpu_time() {
  auto cpuStats = atlasagent::read_file(ServiceMonitorUtilConstants::ProcStatPath);
  if (cpuStats.has_value() == false || cpuStats.value().empty()) {
    atlasagent::Logger()->error("Error reading {}", ServiceMonitorUtilConstants::ProcStatPath);
    return std::nullopt;
  }

  return parse_cpu_time(cpuStats.value());
}

std::optional<unsigned int> get_number_fds(pid_t pid) try {
  auto path = std::filesystem::path(ServiceMonitorUtilConstants::ProcPath) / std::to_string(pid) /
              ServiceMonitorUtilConstants::FdPath;

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

double calculate_cpu_usage(unsigned long long oldCpuTime, unsigned long long newCpuTime,
                           ProcessTimes oldProcessTime, ProcessTimes newProcessTime,
                           unsigned int numCores) {
  unsigned long long processTimeDelta =
      (newProcessTime.uTime - oldProcessTime.uTime) + (newProcessTime.sTime - oldProcessTime.sTime);
  unsigned long long cpuTimeDelta = newCpuTime - oldCpuTime;

  double cpuUsage = 100.0 * (double)processTimeDelta / (double)cpuTimeDelta * numCores;

  return cpuUsage;
}

unsigned int parse_cores(const std::vector<std::string>& cpuInfo) {
  unsigned int cpuCores{0};
  for (const auto& line : cpuInfo) {
    if (line.substr(0, 9) == ServiceMonitorUtilConstants::Processor) {
      cpuCores++;
    }
  }
  return (cpuCores == 0) ? 1 : cpuCores;
}

std::optional<unsigned int> get_cpu_cores() {
  auto cpuInfo = atlasagent::read_file(ServiceMonitorUtilConstants::CpuInfoPath);
  if (cpuInfo.has_value() == false) {
    return std::nullopt;
  }

  return parse_cores(cpuInfo.value());
}

std::unordered_map<pid_t, ProcessTimes> create_pid_map(
    const std::vector<ServiceProperties>& services) {
  std::unordered_map<pid_t, ProcessTimes> pidMap{};
  for (const auto& service : services) {
    auto pid = service.mainPid;
    auto processTimes = get_process_times(pid);
    if (processTimes.has_value() == false) {
      atlasagent::Logger()->error("Error getting {} process times", service.name);
      continue;
    }
    pidMap[pid] = processTimes.value();
  }
  return pidMap;
}