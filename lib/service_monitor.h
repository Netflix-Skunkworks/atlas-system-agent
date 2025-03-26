#include "tagging_registry.h"
#include "spectator/registry.h"

#include <optional>
#include <regex>
#include <string>
#include <vector>
#include <sdbus-c++/sdbus-c++.h>

// Define the Unit structure matching the D-Bus signature (ssssssouso)
using Unit =
    sdbus::Struct<std::string, std::string, std::string, std::string, std::string, std::string,
                  sdbus::ObjectPath, uint32_t, std::string, sdbus::ObjectPath>;

struct ServiceMonitorConstants {
  static constexpr auto ConfigPath{"/opt/service_config.txt"};
  static constexpr auto uTimeIndex{13};
  static constexpr auto sTimeIndex{14};
  static constexpr auto rssIndex{23};
  static constexpr auto ProcStatPath{"/proc/stat"};
  static constexpr auto CpuInfoPath{"/proc/cpuinfo"};
  static constexpr auto AggregateCpuIndex{0};
  static constexpr unsigned int AggregateCpuDataIndex{1};
  static constexpr auto ProcPath{"/proc"};
  static constexpr auto StatPath{"stat"};
  static constexpr auto FdPath{"fd"};
};

struct ProcessTimes {
  unsigned long uTime{};
  unsigned long sTime{};
};

struct ServiceProperties{
  std::string name;
  std::string activeState;
  std::string subState;
  unsigned int mainPid;
};


std::optional<std::vector<std::regex>> parse_service_monitor_config_directory(const char* directoryPath);
std::optional<std::vector<std::regex>> parse_regex_config_file(const char* configFilePath);
std::optional<std::vector<Unit>> list_all_units();
std::optional<ServiceProperties> get_service_properties(const std::string& serviceName);

template <typename Reg = atlasagent::TaggingRegistry>
class ServiceMonitor {
 public:
  ServiceMonitor(Reg* registry, std::vector<std::regex> config)
      : registry_{registry}, config_{config} {}
  ~ServiceMonitor(){};

  // Abide by the C++ rule of 5
  ServiceMonitor(const ServiceMonitor& other) = delete;
  ServiceMonitor& operator=(const ServiceMonitor& other) = delete;
  ServiceMonitor(ServiceMonitor&& other) = delete;
  ServiceMonitor& operator=(ServiceMonitor&& other) = delete;
  bool gather_metrics();

 private:
  bool init_monitored_services();
  bool updateMetrics();

  unsigned long long currentCpuTime{0};
  std::unordered_map<pid_t, ProcessTimes> currentProcessTimes{};
  
  unsigned int numCpuCores;
  bool initSuccess{false};
  Reg* registry_;
  std::vector<std::regex> config_;
  std::unordered_set<const char*> monitoredServices_;
};