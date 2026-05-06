#pragma once

#include "amd_smi.h"

#include <fmt/format.h>
#include <thirdparty/spectator-cpp/spectator/registry.h>

#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace atlasagent
{

struct AmdSmiConstants
{
    /* AMD SMI reports bytes as bytes per second; multiply by this constant
       (the gather interval in seconds) to keep counters consistent with DCGM. */
    static constexpr auto BytesConversion{60};
};

// Nested under amd_smi_detail to avoid colliding with atlasagent::detail::gauge
// defined in lib/collectors/nvml/src/gpumetrics.h, which atlas-agent.cpp also
// includes. Used only at GpuMetricsAMD construction time to build the cached
// per-GPU meter handles below.
namespace amd_smi_detail
{
inline auto gauge(Registry* registry, const char* name, unsigned int gpu, const char* id = nullptr)
{
    std::unordered_map<std::string, std::string> tags = {
        {"gpu", fmt::format("{}", gpu)},
        {"provider", "amd"},
    };
    if (id != nullptr)
    {
        tags["id"] = id;
    }
    return registry->CreateGauge(name, tags);
}

inline auto counter(Registry* registry, const char* name, unsigned int gpu,
                    const char* id = nullptr)
{
    std::unordered_map<std::string, std::string> tags = {
        {"gpu", fmt::format("{}", gpu)},
        {"provider", "amd"},
    };
    if (id != nullptr)
    {
        tags["id"] = id;
    }
    return registry->CreateCounter(name, tags);
}
}  // namespace amd_smi_detail

// Cached meter handles for a single GPU. Built once at construction; each
// gather tick just invokes Set/Increment on the cached handles, avoiding the
// per-tick tag-map allocation and registry lookup.
struct PerGpuMeters
{
    Gauge usedMemory;
    Gauge freeMemory;
    Gauge totalMemory;
    Gauge utilization;
    Gauge memoryActivity;
    Gauge gfxClock;
    Gauge memoryClock;
    Gauge power;
    spectator::Counter pcieIn;
    spectator::Counter pcieOut;
    spectator::Counter xgmiIn;
    spectator::Counter xgmiOut;

    PerGpuMeters(Registry* registry, unsigned int gpu)
        : usedMemory{amd_smi_detail::gauge(registry, "gpu.usedMemory", gpu)}
        , freeMemory{amd_smi_detail::gauge(registry, "gpu.freeMemory", gpu)}
        , totalMemory{amd_smi_detail::gauge(registry, "gpu.totalMemory", gpu)}
        , utilization{amd_smi_detail::gauge(registry, "gpu.utilization", gpu)}
        , memoryActivity{amd_smi_detail::gauge(registry, "gpu.memoryActivity", gpu)}
        , gfxClock{amd_smi_detail::gauge(registry, "gpu.clockFrequency", gpu, "gpu")}
        , memoryClock{amd_smi_detail::gauge(registry, "gpu.clockFrequency", gpu, "memory")}
        , power{amd_smi_detail::gauge(registry, "gpu.power", gpu)}
        , pcieIn{amd_smi_detail::counter(registry, "gpu.pcie.bytes", gpu, "in")}
        , pcieOut{amd_smi_detail::counter(registry, "gpu.pcie.bytes", gpu, "out")}
        , xgmiIn{amd_smi_detail::counter(registry, "gpu.xgmi.bytes", gpu, "in")}
        , xgmiOut{amd_smi_detail::counter(registry, "gpu.xgmi.bytes", gpu, "out")}
    {
    }
};

class GpuMetricsAMD
{
   public:
    ~GpuMetricsAMD() = default;

    GpuMetricsAMD(const GpuMetricsAMD&) = delete;
    GpuMetricsAMD& operator=(const GpuMetricsAMD&) = delete;
    GpuMetricsAMD(GpuMetricsAMD&&) noexcept = default;
    GpuMetricsAMD& operator=(GpuMetricsAMD&&) = delete;

    // Returns std::nullopt if AMD SMI is unavailable or no AMD GPUs are present.
    static std::optional<GpuMetricsAMD> Create(Registry* registry) noexcept;

    void GPUMetrics() noexcept;

   private:
    GpuMetricsAMD(Registry* registry, std::unique_ptr<AmdSmi> smi, uint32_t count) noexcept;

    void GetMemoryMetrics(unsigned int i, amdsmi_processor_handle handle) noexcept;
    void GetActivityMetrics(unsigned int i, amdsmi_processor_handle handle) noexcept;
    void GetClockMetrics(unsigned int i, amdsmi_processor_handle handle) noexcept;
    void GetTemperatureMetric(amdsmi_processor_handle handle) noexcept;
    void GetPowerMetric(unsigned int i, amdsmi_processor_handle handle) noexcept;
    void GetPcieMetrics(unsigned int i, amdsmi_processor_handle handle) noexcept;
    void GetXgmiMetrics(unsigned int i, amdsmi_processor_handle handle) noexcept;

    Registry* registry_;
    std::unique_ptr<AmdSmi> smi_;
    Gauge gpuCount_;
    spectator::DistributionSummary temperature_;
    std::vector<PerGpuMeters> meters_;
};

}  // namespace atlasagent
