#pragma once

#include <boost/process.hpp>

#include <vector>
#include <string>

#include <thirdparty/spectator-cpp/spectator/registry.h>

struct PerfspectConstants {
  static constexpr auto BinaryLocation{"/apps/nflx-perfspect/bin"};
  static constexpr auto BinaryName{"perfspect"};
  
  // Individual argument constants
  static constexpr auto command{"metrics"};
  static constexpr auto eventfileFlag{"--eventfile"};
  static constexpr auto eventfilePath{"/apps/nflx-perfspect/etc/events-intel.txt"};
  static constexpr auto metricfileFlag{"--metricfile"};
  static constexpr auto metricfilePath{"/apps/nflx-perfspect/etc/metrics-intel.json"};
};
bool valid_instance();

namespace detail {}  // namespace detail

class Perfspect {
 public:
  Perfspect(Registry* registry) : registry_(registry){};
  ~Perfspect(){};

  // Abide by the C++ rule of 5
  Perfspect(const Perfspect& other) = delete;
  Perfspect& operator=(const Perfspect& other) = delete;
  Perfspect(Perfspect&& other) = delete;
  Perfspect& operator=(Perfspect&& other) = delete;

  bool gather_metrics();

 private:
  bool start_script();
  bool read_output(std::vector<std::string>& perfspectOutput);
  

  Registry* registry_;
  bool scriptStarted{false};
  boost::process::child scriptProcess;
  boost::process::ipstream outStream;  // Pipe for reading stdout
};