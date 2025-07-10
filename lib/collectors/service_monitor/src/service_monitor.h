#pragma once

#include <thirdparty/spectator-cpp/spectator/registry.h>

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

inline auto gauge(Registry* registry, const std::string_view name, const std::string_view serviceName) {
  auto tags = std::unordered_map<std::string, std::string>{{"service.name", fmt::format("{}", serviceName)}};
  return registry->CreateGauge(std::string(name), tags, ServiceMonitorConstants::GaugeTTLSeconds);
}

inline auto gaugeServiceState(Registry* registry, const std::string_view name, const std::string_view serviceName, const std::string_view state) {
  auto tags = std::unordered_map<std::string, std::string>{{"service.name", fmt::format("{}", serviceName)}};
  tags.emplace("state", fmt::format("{}", state));
  return registry->CreateGauge(std::string(name), tags, ServiceMonitorConstants::GaugeTTLSeconds);
}
}  // namespace detail


class ServiceMonitor {
 public:
  ServiceMonitor(Registry* registry, std::vector<std::regex> config, unsigned int max_services);
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

  Registry* registry_;
  std::vector<std::regex> config_;
  unsigned int maxMonitoredServices{};
  unsigned long long currentCpuTime{0};
  std::unordered_map<unsigned int, ProcessTimes> currentProcessTimes{};
  unsigned int numCpuCores{};
  long pageSize{};
  bool initSuccess{false};
  std::vector<std::string> monitoredServices_{};
};