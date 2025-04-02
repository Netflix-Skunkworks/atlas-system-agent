#pragma once

#include "tagging_registry.h"
#include "spectator/registry.h"
#include "service_monitor_utils.h"

struct ServiceMonitorConstants {
  static constexpr auto MaxMonitoredServices{10};
  static constexpr auto ConfigPath{"/etc/atlas-system-agent/conf.d"};
  static constexpr auto rssName{"service.rss"};
  static constexpr auto fdsName{"service.fds"};
  static constexpr auto cpuUsageName{"service.cpu_usage"};
  static constexpr auto serviceStatusName{"service.status"};
};

namespace detail {
template <typename Reg>
inline auto gauge(Reg* registry, const char* name, const char* serviceName) {
  auto tags = spectator::Tags{{"service.name", fmt::format("{}", serviceName)}};
  return registry->GetGauge(name, tags);
}

template <typename Reg>
inline auto gaugeServiceState(Reg* registry, const char* name, const char* serviceName,
                              const char* activeState, const char* subState) {
  auto tags = spectator::Tags{{"service.name", fmt::format("{}", serviceName)}};
  if (activeState != nullptr) {
    tags.add("active.state", activeState);
  }
  if (subState != nullptr) {
    tags.add("sub.state", subState);
  }
  return registry->GetGauge(name, tags);
}
}  // namespace detail

template <typename Reg = atlasagent::TaggingRegistry>
class ServiceMonitor {
 public:
  ServiceMonitor(Reg* registry, std::vector<std::regex> config, unsigned int max_services);
  ~ServiceMonitor(){};

  // Abide by the C++ rule of 5
  ServiceMonitor(const ServiceMonitor& other) = delete;
  ServiceMonitor& operator=(const ServiceMonitor& other) = delete;
  ServiceMonitor(ServiceMonitor&& other) = delete;
  ServiceMonitor& operator=(ServiceMonitor&& other) = delete;
  bool gather_metrics();

 private:
  bool init_monitored_services();
  bool update_metrics();

  Reg* registry_;
  std::vector<std::regex> config_;
  unsigned int maxMonitoredServices{};
  unsigned long long currentCpuTime{0};
  std::unordered_map<pid_t, ProcessTimes> currentProcessTimes{};
  unsigned int numCpuCores{};
  long pageSize{};
  bool initSuccess{false};
  std::unordered_set<std::string> monitoredServices_{};
};