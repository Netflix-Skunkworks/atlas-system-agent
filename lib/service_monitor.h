#pragma once

#include "tagging_registry.h"
#include "spectator/registry.h"
#include "service_monitor_utils.h"

struct ServiceMonitorConstants {
  static constexpr auto DefaultMonitoredServices{10};
  static constexpr auto GaugeTTLSeconds{60};
  static constexpr auto ConfigPath{"/etc/atlas-system-agent/conf.d"};
  static constexpr auto RssName{"systemd.service.rss"};
  static constexpr auto FdsName{"systemd.service.fds"};
  static constexpr auto CpuUsageName{"systemd.service.cpuUsage"};
  static constexpr auto ServiceStatusName{"systemd.service.status"};
};

namespace detail {
template <typename Reg>
inline auto gauge(Reg* registry, const std::string_view name, const std::string_view serviceName) {
  auto tags = spectator::Tags{{"service.name", fmt::format("{}", serviceName)}};
  return registry->GetGaugeTTL(name, ServiceMonitorConstants::GaugeTTLSeconds, tags);
}

template <typename Reg>
inline auto gaugeServiceState(Reg* registry, const std::string_view name, const std::string_view serviceName, const std::string_view state) {
  auto tags = spectator::Tags{{"service.name", fmt::format("{}", serviceName)}};
  tags.add("state", state);
  return registry->GetGaugeTTL(name, ServiceMonitorConstants::GaugeTTLSeconds, tags);
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
  std::unordered_map<unsigned int, ProcessTimes> currentProcessTimes{};
  unsigned int numCpuCores{};
  long pageSize{};
  bool initSuccess{false};
  std::vector<std::string> monitoredServices_{};
  std::unordered_map<std::string, std::string> previousStates{};
};