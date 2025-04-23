
#include <lib/tagging/src/tagging_registry.h>

#include <lib/spectator/registry.h>

#include "string.h"
#include "unistd.h"
#include <iostream>
#include <map>
#include <unordered_map>
#include <time.h>
#include <vector>
#include <optional>
#include <array>

struct DCGMConstants {
  static constexpr auto ServiceName{"nvidia-dcgm.service"};
  static constexpr auto dcgmiPath{"/usr/bin/dcgmi"};
  static constexpr auto dcgmiArgs{"dmon -c 1 -e 1001,1002,1003,1004,1005,1007,1008,1009,1010,1011,1012"};
  static constexpr auto ConsecutiveFailureThreshold{5};
  static constexpr auto ExpectedCountOfTokens{13};
  static constexpr auto ExpectedCountOfProfileValues{11};
  static constexpr auto RequiredLines{3};
  static constexpr auto DataStartLineIndex{2};
  static constexpr auto DataStartTokenIndex{2};
  static constexpr auto GPUIdTokenIndex{1};
  /* DCGMI reports bytes as bytes per second, multiply by this constant to
    ensure consistency with our Counters */
  static constexpr auto BytesConversion{60};
  /* DCGMI reports percentages as decimals, so .93 is actually 93 percent, and
    we need to multiply our values by this const */
  static constexpr auto PercentileConversion{100};
};

namespace detail {
template <typename Reg>
inline auto gauge(Reg* registry, const char* name, unsigned int gpu, const char* id = nullptr) {
  auto tags = spectator::Tags{{"gpu", fmt::format("{}", gpu)}};
  if (id != nullptr) {
    tags.add("id", id);
  }
  return registry->GetGauge(name, tags);
}

template <typename Reg>
inline auto counter(Reg* registry, const char* name, unsigned int gpu, const char* id = nullptr) {
  auto tags = spectator::Tags{{"gpu", fmt::format("{}", gpu)}};
  if (id != nullptr) {
    tags.add("id", id);
  }
  return registry->GetCounter(name, tags);
}
}  // namespace detail

template <typename Reg = atlasagent::TaggingRegistry>
class GpuMetricsDCGM {
 public:
  GpuMetricsDCGM(Reg* registry) : registry_{registry} {};
  ~GpuMetricsDCGM(){};

  // Abide by the C++ rule of 5
  GpuMetricsDCGM(const GpuMetricsDCGM& other) = delete;
  GpuMetricsDCGM& operator=(const GpuMetricsDCGM& other) = delete;
  GpuMetricsDCGM(GpuMetricsDCGM&& other) = delete;
  GpuMetricsDCGM& operator=(GpuMetricsDCGM&& other) = delete;
  bool gather_metrics();

 private:
  bool update_metrics(std::map<int, std::vector<double>>& dataMap);
  Reg* registry_;
};