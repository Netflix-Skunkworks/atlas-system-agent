#include "service_monitor.h"
#include "dbus.h"
#include "util.h"
#include <filesystem>
#include <regex>

template class ServiceMonitor<atlasagent::TaggingRegistry>;

std::optional<std::vector<std::regex>> parse_service_monitor_config(const char* configPath) {
  std::optional<std::vector<std::string>> stringPatterns = atlasagent::read_file(configPath);
  // If reading the config fails or the config is empty return
  if (!stringPatterns.has_value() || stringPatterns.value().empty()) {
    return std::nullopt;
  }

  std::vector<std::regex> regexPatterns;
  regexPatterns.reserve(stringPatterns.value().size());

  for (const auto& regex_pattern : stringPatterns.value()) {
    try {
      regexPatterns.emplace_back(regex_pattern);
    } catch (const std::regex_error& e) {
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
  if (cpuStats.has_value() == false || cpuStats.value().size() >= ServiceMonitorConstants::AggregateCpuDataIndex) {
    return std::nullopt;
  }

  const auto& aggregateStats = cpuStats.value()[ServiceMonitorConstants::AggregateCpuIndex];

  unsigned long long totalCpuTime{0};
  for (unsigned int i = ServiceMonitorConstants::AggregateCpuDataIndex; i < aggregateStats.size();i++) {
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
void ServiceMonitor<Reg>::InitMonitoredServices(){
    DBus dbus{};

    // Todo: Rename this to get AllUnits
    auto all_units = dbus.GetAllServices();
    if (all_units.has_value() == false){
        return;
    }

    // Units were retrieved init is now success because monitoredServices now initialized
    this->initSuccess = true;

    for (const auto& regex : this->config_) {
        for (const auto& unit : all_units.value()) {
            if (std::regex_search(unit.name, regex)) {
                this->monitoredServices_.insert(unit.name.c_str());
            }
        }
    }
}


template <class Reg>
bool ServiceMonitor<Reg>::updateMetrics(){
    return true;
}

// Todo change to optional bool because if size is 0 not a failure but user error
// Maybe make a way to shutdown this module. 
template <class Reg>
bool ServiceMonitor<Reg>::gather_metrics() {

    // Pattern match to find the services we want to monitor
    if (this->initSuccess == false) {
        this->InitMonitoredServices();
    }

    // We initialized but there are no services to monitor 
    // No services matched the pattern
    if (this->monitoredServices_.size() == 0){
        return false;
    }

    if (false == this->updateMetrics()){
        return false;
    }

    return true;
}
