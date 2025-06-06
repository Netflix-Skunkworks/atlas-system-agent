#pragma once

#include "nvml.h"
#include <lib/spectator/registry.h>

namespace atlasagent {

namespace detail {
template <typename Reg>
inline auto gauge(Reg* registry, const char* name, unsigned int gpu, const char* id = nullptr) {
  auto tags = spectator::Tags{{"gpu", fmt::format("{}", gpu)}};
  if (id != nullptr) {
    tags.add("id", id);
  }
  return registry->GetGauge(name, tags);
}

}  // namespace detail

template <typename Reg, typename Lib>
class GpuMetrics {
 public:
  GpuMetrics(Reg* registry, std::unique_ptr<Lib> nvml) noexcept
      : registry_(registry), nvml_(std::move(nvml)) {}

  void gpu_metrics() noexcept {
    static auto gpuCountGauge = registry_->GetGauge("gpu.count");
    static auto gpuTemperature = registry_->GetDistributionSummary("gpu.temperature");

    unsigned count;
    if (!nvml_->get_count(&count)) {
      return;
    }

    gpuCountGauge->Set(count);
    for (auto i = 0u; i < count; ++i) {
      NvmlDeviceHandle device;
      if (nvml_->get_by_index(i, &device)) {
        NvmlMemory memory;
        if (nvml_->get_memory_info(device, &memory)) {
          detail::gauge(registry_, "gpu.usedMemory", i)->Set(memory.used);
          detail::gauge(registry_, "gpu.freeMemory", i)->Set(memory.free);
          detail::gauge(registry_, "gpu.totalMemory", i)->Set(memory.total);
        }

        NvmlUtilization utilization;
        if (nvml_->get_utilization_rates(device, &utilization)) {
          detail::gauge(registry_, "gpu.utilization", i)->Set(utilization.gpu);
          detail::gauge(registry_, "gpu.memoryActivity", i)->Set(utilization.memory);
        }

        NvmlPerfState perf_state = -1;
        if (nvml_->get_performance_state(device, &perf_state)) {
          detail::gauge(registry_, "gpu.perfState", i)->Set(static_cast<double>(perf_state));
        }

        unsigned temp;
        if (nvml_->get_temperature(device, &temp)) {
          gpuTemperature->Record(temp);
        }
      }
    }
  }

 private:
  Reg* registry_;
  std::unique_ptr<Lib> nvml_;
};

}  // namespace atlasagent
