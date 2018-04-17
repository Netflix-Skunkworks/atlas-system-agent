#pragma once

#include <atlas/meter/registry.h>
#include "nvml.h"

namespace atlasagent {

namespace detail {
inline std::shared_ptr<atlas::meter::Gauge<double>> gauge(atlas::meter::Registry* registry,
                                                          const char* name, unsigned gpu,
                                                          const char* id = nullptr) {
  char buf[32];
  snprintf(buf, sizeof buf, "gpu-%u", gpu);
  auto tags = atlas::meter::Tags{{"gpu", buf}};
  if (id != nullptr) {
    tags.add("id", id);
  }
  return registry->gauge(registry->CreateId(name, tags));
}

}  // namespace detail

template <typename Lib>
class GpuMetrics {
 public:
  GpuMetrics(atlas::meter::Registry* registry, Lib* nvml) noexcept
      : registry_(registry), nvml_(nvml) {}

  void gpu_metrics() noexcept {
    static auto gpuCountGauge =
        registry_->gauge(registry_->CreateId("gpu.count", atlas::meter::Tags{}));
    static auto gpuTemperature = registry_->distribution_summary(
        registry_->CreateId("gpu.temperature", atlas::meter::Tags{}));

    unsigned count;
    if (!nvml_->get_count(&count)) return;

    gpuCountGauge->Update(count);
    for (auto i = 0u; i < count; ++i) {
      NvmlDeviceHandle device;
      if (nvml_->get_by_index(i, &device)) {
        NvmlMemory memory;
        if (nvml_->get_memory_info(device, &memory)) {
          detail::gauge(registry_, "gpu.memoryUsage", i, "free")->Update(memory.free);
          detail::gauge(registry_, "gpu.memoryUsage", i, "used")->Update(memory.used);
        }

        NvmlUtilization utilization;
        if (nvml_->get_utilization_rates(device, &utilization)) {
          detail::gauge(registry_, "gpu.utilization", i)->Update(utilization.gpu);
          detail::gauge(registry_, "gpu.memoryActivity", i)->Update(utilization.memory);
        }

        unsigned temp;
        if (nvml_->get_temperature(device, &temp)) {
          gpuTemperature->Record(temp);
        }
      }
    }
  }

 private:
  atlas::meter::Registry* registry_;
  Lib* nvml_;
};

}  // namespace atlasagent
