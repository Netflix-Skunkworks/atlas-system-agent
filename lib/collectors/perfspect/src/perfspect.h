#pragma once

#include <boost/process.hpp>

#include <vector>
#include <string>
#include <memory>

#include <thirdparty/spectator-cpp/spectator/registry.h>

struct PerfspectConstants {
  static constexpr auto BinaryLocation{"/apps/nflx-perfspect/bin"};
  static constexpr auto BinaryName{"perfspect"};
  
  // Individual argument constants
  static constexpr auto command{"metrics"};
  static constexpr auto eventfileFlag{"--eventfile"};
  static constexpr auto eventfilePath{"/apps/nflx-perfspect/etc/events-amd.txt"};
  static constexpr auto metricfileFlag{"--metricfile"};
  static constexpr auto metricfilePath{"/apps/nflx-perfspect/etc/metrics-amd.json"};
  static constexpr auto durationFlag{"--duration"};
  static constexpr auto durationValue{"60"};
  static constexpr auto liveFlag{"--live"};
};
bool valid_instance();



namespace detail {
  inline auto perfspectGauge(Registry* registry, const std::string_view name) {
  return registry->CreateGauge(std::string(name));
  }
  inline auto perfspectCounter(Registry* registry, const std::string_view name){
    return registry->CreateCounter(std::string(name));
  }

}

class Perfspect {
 public:
  Perfspect(Registry* registry) : registry_(registry){};
  ~Perfspect() { cleanup_process(); };

  // Abide by the C++ rule of 5
  Perfspect(const Perfspect& other) = delete;
  Perfspect& operator=(const Perfspect& other) = delete;
  Perfspect(Perfspect&& other) = delete;
  Perfspect& operator=(Perfspect&& other) = delete;

  bool gather_metrics();

 private:
  bool process_completed();
  bool start_script();
  bool read_output(std::vector<std::string>& perfspectOutput);
  void cleanup_process();
  bool sendMetricsAMD(const std::vector<std::string>& perfspectOutput);
  

  Registry* registry_;
  bool scriptStarted{false};
  std::unique_ptr<boost::process::child> scriptProcess;
  std::unique_ptr<boost::process::ipstream> outStream;  // Pipe for reading stdout
  std::unique_ptr<boost::process::ipstream> errStream;  // Pipe for reading stderr
};