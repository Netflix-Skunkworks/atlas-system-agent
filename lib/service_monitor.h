#include "tagging_registry.h"
#include "spectator/registry.h"

#include <optional>
#include <regex>
#include <string>
#include <vector>

struct ServiceMonitorConstants {
  static constexpr auto ConfigPath{"/opt/service_config.txt"};
  static constexpr auto uTimeIndex{13};
  static constexpr auto sTimeIndex{14};
  static constexpr auto rssIndex{23};
  static constexpr auto ProcStatPath{"/proc/stat"};
  static constexpr auto AggregateCpuIndex{0};
  static constexpr unsigned int AggregateCpuDataIndex{1};
};

struct ProcessTimes {
  unsigned long uTime{};
  unsigned long sTime{};
};

std::optional<std::vector<std::regex>> parse_service_monitor_config(const char* configPath);

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
  void InitMonitoredServices();
  bool updateMetrics();

  bool initSuccess{false};
  Reg* registry_;
  std::vector<std::regex> config_;
  std::unordered_set<const char*> monitoredServices_;
};