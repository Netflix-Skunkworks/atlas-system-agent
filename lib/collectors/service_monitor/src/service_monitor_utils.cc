#include <filesystem>

#include "absl/strings/str_split.h"
#include "service_monitor_utils.h"
#include <lib/util/src/util.h>


// The function returns a vector of Unit structs, which contain information about each unit.
std::optional<std::vector<Unit>> list_all_units() try {
  // Create system bus connection
  auto connection = sdbus::createSystemBusConnection();

  // Create the proxy
  auto proxy = sdbus::createProxy(*connection, sdbus::ServiceName{DBusConstants::Service},
                                  sdbus::ObjectPath{DBusConstants::Path});

  // Create method call message
  auto methodCall = proxy->createMethodCall(sdbus::InterfaceName{DBusConstants::Interface},
                                            sdbus::MethodName{DBusConstants::MethodListUnits});
  
  // Store all the results from the method MethodListUnits into a vector of Unit structs
  std::vector<Unit> units{};
  proxy->callMethod(sdbus::MethodName{DBusConstants::MethodListUnits})
      .onInterface(sdbus::InterfaceName{DBusConstants::Interface})
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
  auto managerProxy = sdbus::createProxy(*connection, sdbus::ServiceName{DBusConstants::Service},
                                         sdbus::ObjectPath{DBusConstants::Path});

  // Get the proper object path for the unit
  managerProxy->callMethod(DBusConstants::MethodGetUnit)
      .onInterface(DBusConstants::Interface)
      .withArguments(serviceName)
      .storeResultsTo(unitObjectPath);

  // Create a proxy to the service object with the correct path
  auto proxy =
      sdbus::createProxy(*connection, sdbus::ServiceName{DBusConstants::Service}, unitObjectPath);

  // Get MainPID property
  sdbus::Variant mainPidVariant;
  proxy->callMethod(DBusConstants::MethodGet)
      .onInterface(DBusConstants::PropertiesInterface)
      .withArguments(DBusConstants::ServiceInterface, DBusConstants::PropertyMainPID)
      .storeResultsTo(mainPidVariant);

  // Get ActiveState property
  sdbus::Variant activeStateVariant;
  proxy->callMethod(DBusConstants::MethodGet)
      .onInterface(DBusConstants::PropertiesInterface)
      .withArguments(DBusConstants::UnitInterface, DBusConstants::PropertyActiveState)
      .storeResultsTo(activeStateVariant);

  // Get SubState property
  sdbus::Variant subStateVariant;
  proxy->callMethod(DBusConstants::MethodGet)
      .onInterface(DBusConstants::PropertiesInterface)
      .withArguments(DBusConstants::UnitInterface, DBusConstants::PropertySubState)
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

std::optional<std::vector<std::regex>> parse_regex_config_file(const char* configFilePath) try {
  // Read the all the regex patterns in the config file
  std::optional<std::vector<std::string>> stringPatterns = atlasagent::read_file(configFilePath);
  if (stringPatterns.has_value() == false) {
    atlasagent::Logger()->error("Error reading config file {}", configFilePath);
    return std::nullopt;
  }

  // Skip empty files
  if (stringPatterns.value().empty()) {
    atlasagent::Logger()->debug("Empty config file {}", configFilePath);
    return std::nullopt;
  }

  // Read all the lines in the file and if the line is a valid regex pattern, add it to regexPatterns
  // If any of the regex patterns are invalid, log the error and return nullopt
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
} catch (const std::exception& e) {
  atlasagent::Logger()->error("Exception: {} in parse_regex_config_file", e.what());
  return std::nullopt;
}

std::optional<std::vector<std::regex>> parse_service_monitor_config_directory(const char* directoryPath) try {
  if (std::filesystem::exists(directoryPath) == false || std::filesystem::is_directory(directoryPath) == false) {
    atlasagent::Logger()->error("Invalid service monitor config directory {}", directoryPath);
    return std::nullopt;
  }

  std::regex configFileExtPattern(ServiceMonitorUtilConstants::ConfigFileExtPattern);
  std::vector<std::regex> allRegexPatterns{};

  // Iterate through all files in the config directory, but do not process them if they do not match the service 
  // monitoring config regex pattern ".systemd-unit"
  for (const auto& file : std::filesystem::recursive_directory_iterator(directoryPath)) {
    if (std::regex_match(file.path().filename().string(), configFileExtPattern) == false) {
      continue;
    }

    // If parsing the file succeeds, add the regex patterns to allRegexPatterns, otherwise log & continue to the next file
    auto regexExpressions = parse_regex_config_file(file.path().c_str());
    if (regexExpressions.has_value() == false) {
      atlasagent::Logger()->error("Could not add regex expressions from file {}", file.path().c_str());
      continue;
    }
    allRegexPatterns.insert(allRegexPatterns.end(), regexExpressions.value().begin(), regexExpressions.value().end());
  }

  // If no regex patterns were found in the directory, log the error and return nullopt
  if (allRegexPatterns.empty()) {
    atlasagent::Logger()->info("No regex patterns found in directory {}", directoryPath);
    return std::nullopt;
  }
  
  return allRegexPatterns;
} catch (const std::exception& e) {
  atlasagent::Logger()->error("Exception: {} in parse_service_monitor_config_directory", e.what());
  return std::nullopt;
}

std::optional<std::vector<std::string>> get_proc_fields(const unsigned int &pid) {
  std::filesystem::path procPath = std::filesystem::path(ServiceMonitorUtilConstants::ProcPath) /
                                   std::to_string(pid) / ServiceMonitorUtilConstants::StatPath;
  auto pidStats = atlasagent::read_file(procPath.string().c_str());
  return pidStats;
}

std::optional<ProcessTimes> parse_process_times(const std::vector<std::string>& pidStats) try {
  auto statLine = pidStats.at(0);
  std::vector<std::string> statTokens = absl::StrSplit(statLine, ' ', absl::SkipWhitespace());

  // Check if we have enough tokens before accessing them
  if (statTokens.size() <= ServiceMonitorUtilConstants::STimeIndex) {
    atlasagent::Logger()->error("Not enough tokens in proc stat file. Expected at least {}, got {}",
                                ServiceMonitorUtilConstants::STimeIndex + 1, statTokens.size());
    return std::nullopt;
  }

  unsigned long uTime = std::stoul(statTokens[ServiceMonitorUtilConstants::UTimeIndex]);
  unsigned long sTime = std::stoul(statTokens[ServiceMonitorUtilConstants::STimeIndex]);
  return ProcessTimes{uTime, sTime};
} catch (const std::exception& e) {
  atlasagent::Logger()->error("Exception: {} in parse_process_times", e.what());
  return std::nullopt;
}

std::optional<ProcessTimes> get_process_times(const unsigned int &pid) {
  auto pidStats = get_proc_fields(pid);
  if (pidStats.has_value() == false) {
    return std::nullopt;
  }
  return parse_process_times(pidStats.value());
}

std::optional<unsigned long> parse_rss(const std::vector<std::string>& pidStats) try {
  auto statLine = pidStats.at(0);
  std::vector<std::string> statTokens = absl::StrSplit(statLine, ' ', absl::SkipWhitespace());
  // Check if we have enough tokens before accessing them
  if (statTokens.size() <= ServiceMonitorUtilConstants::RssIndex) {
    atlasagent::Logger()->error("Not enough tokens in proc stat file. Expected at least {}, got {}",
                                ServiceMonitorUtilConstants::RssIndex + 1, statTokens.size());
    return std::nullopt;
  }

  return std::stoul(statTokens[ServiceMonitorUtilConstants::RssIndex]);
} catch (const std::exception& e) {
  atlasagent::Logger()->error("Exception: {} in parse_rss", e.what());
  return std::nullopt;
}

std::optional<unsigned long> get_rss(const unsigned int &pid) {
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
  for (unsigned int i = ServiceMonitorUtilConstants::AggregateCpuDataIndex; i < statTokens.size();
       i++) {
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

std::optional<unsigned int> get_number_fds(const unsigned int& pid) try {
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

double calculate_cpu_usage(const unsigned long long &oldCpuTime, const unsigned long long &newCpuTime,
                           const ProcessTimes &oldProcessTime, const ProcessTimes &newProcessTime,
                           const unsigned int &numCores) {
  unsigned long long processTimeDelta = (newProcessTime.uTime - oldProcessTime.uTime) + (newProcessTime.sTime - oldProcessTime.sTime);
  unsigned long long cpuTimeDelta = newCpuTime - oldCpuTime;
  double cpuUsage = (100.0 * processTimeDelta / cpuTimeDelta) * numCores;

  return cpuUsage;
}

unsigned int parse_cores(const std::string& cpuInfo) try {
  if (cpuInfo == "0") {
    return ServiceMonitorUtilConstants::DefaultCoreCount;
  }

  // Format is typically "0-N" where N+1 is the number of cores
  size_t dashPos = cpuInfo.find('-');
  if (dashPos == std::string::npos) {
    atlasagent::Logger()->error("Unexpected format in CPU info: {}", cpuInfo);
    return ServiceMonitorUtilConstants::DefaultCoreCount;
  }

  unsigned int lastCore = std::stoul(cpuInfo.substr(dashPos + 1));
  return lastCore + 1;
} catch (const std::exception& e) {
  atlasagent::Logger()->error("Exception in parse_cores");
  return ServiceMonitorUtilConstants::DefaultCoreCount;
}

std::optional<unsigned int> get_cpu_cores() {
  auto possible = atlasagent::read_file(ServiceMonitorUtilConstants::CpuInfoPath);
  if (possible.has_value() == false || possible.value().empty()) {
    atlasagent::Logger()->error("Error reading {}", ServiceMonitorUtilConstants::CpuInfoPath);
    return std::nullopt;
  }

  // File should only be one line
  if (possible.value().size() != 1) {
    return std::nullopt;
  }

  return parse_cores(possible.value()[0]);
}


/* Iterate through the services and get the main PID and process times for each service
Store the results in a map where the key is the PID and the value is the ProcessTimes struct
If any of the process times cannot be retrieved, log the error and continue to the next service
Return the map of PIDs and process times */
std::unordered_map<unsigned int, ProcessTimes> create_pid_map(const std::vector<ServiceProperties>& services) try {
  std::unordered_map<unsigned int, ProcessTimes> pidMap{};
  for (const auto& service : services) {
    if (service.activeState != ServiceMonitorUtilConstants::Active || service.subState != ServiceMonitorUtilConstants::Running) {
      atlasagent::Logger()->debug("Service {} is not active and running, not gathering process time", service.name);
      continue;
    }
    auto pid = service.mainPid;
    auto processTimes = get_process_times(pid);
    if (processTimes.has_value() == false) {
      atlasagent::Logger()->error("Error getting {} process times", service.name);
      continue;
    }
    pidMap[pid] = processTimes.value();
  }
  return pidMap;
} catch (const std::exception& e) {
  atlasagent::Logger()->error("Exception: {} in create_pid_map", e.what());
  return {};
}