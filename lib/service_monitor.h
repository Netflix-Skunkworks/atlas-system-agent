#pragma once

#include "tagging_registry.h"
#include "spectator/registry.h"
#include "service_monitor_utils.h"


struct ServiceMonitorConstants {
  static constexpr auto ConfigPath{"/opt/atlas-configs"};
  static constexpr auto rssName{"service.rss"};
  static constexpr auto fdsName{"service.fds"};
  static constexpr auto cpuUsageName{"service.cpu_usage"};
  static constexpr auto serviceStatusName{"service.status"};
};


namespace detail {
  template <typename Reg>
  inline auto gauge(Reg* registry, const char* name, const char* serviceName, const char* id = nullptr) {
    auto tags = spectator::Tags{{"service_name", fmt::format("{}", serviceName)}};
    if (id != nullptr) {
      tags.add("id", id);
    }
    return registry->GetGauge(name, tags);
  }

  template <typename Reg>
  inline auto guageServiceState(Reg* registry, const char* name, const char* serviceName, const char* activeState, const char* subState){ 
    auto tags = spectator::Tags{{"service_name", fmt::format("{}", serviceName)}};
    if (activeState != nullptr) {
      tags.add("active_state", activeState);
    }
    if (subState != nullptr) {
      tags.add("sub_state", subState);
    }
    return registry->GetGauge(name, tags);
  }
}  // namespace detail
  


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
  std::unordered_set<std::string> monitoredServices_;
};