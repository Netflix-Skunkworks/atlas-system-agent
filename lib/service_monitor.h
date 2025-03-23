#include "tagging_registry.h"
#include "spectator/registry.h"

#include <optional>
#include <string>
#include <vector>

struct ServiceMonitorConstants
{
    static constexpr auto ConfigPath{"/opt/service_config.txt"};
};


std::optional<std::vector<std::string>> parse_service_monitor_config(const char* configPath);

template <typename Reg = atlasagent::TaggingRegistry>
class ServiceMonitor {
 public:
 ServiceMonitor(Reg* registry, std::vector<std::string> config) : registry_{registry}, config_{config} {}
  ~ServiceMonitor(){};

  // Abide by the C++ rule of 5
  ServiceMonitor(const ServiceMonitor& other) = delete;
  ServiceMonitor& operator=(const ServiceMonitor& other) = delete;
  ServiceMonitor(ServiceMonitor&& other) = delete;
  ServiceMonitor& operator=(ServiceMonitor&& other) = delete;
  bool gather_metrics();

 private:
  Reg* registry_;
  std::vector<std::string> config_;
};