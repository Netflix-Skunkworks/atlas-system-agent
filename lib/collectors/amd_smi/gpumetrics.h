#pragma once

#include "amd_smi.h"

#include <fmt/format.h>
#include <thirdparty/spectator-cpp/spectator/registry.h>

#include <memory>
#include <optional>
#include <string>
#include <unordered_map>

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
// includes. Same signature in the same namespace would be an ODR violation.
namespace amd_smi_detail
{
inline auto gauge(Registry* registry, const char* name, unsigned int gpu, const char* id = nullptr)
{
    std::unordered_map<std::string, std::string> tags = {{"gpu", fmt::format("{}", gpu)}};
    if (id != nullptr)
    {
        tags["id"] = id;
    }
    return registry->CreateGauge(name, tags);
}

inline auto counter(Registry* registry, const char* name, unsigned int gpu,
                    const char* id = nullptr)
{
    std::unordered_map<std::string, std::string> tags = {{"gpu", fmt::format("{}", gpu)}};
    if (id != nullptr)
    {
        tags["id"] = id;
    }
    return registry->CreateCounter(name, tags);
}
}  // namespace amd_smi_detail

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
    GpuMetricsAMD(Registry* registry, std::unique_ptr<AmdSmi> smi) noexcept;

    void GetMemoryMetrics(unsigned int i, amdsmi_processor_handle handle) noexcept;
    void GetActivityMetrics(unsigned int i, amdsmi_processor_handle handle) noexcept;
    void GetClockMetrics(unsigned int i, amdsmi_processor_handle handle) noexcept;
    void GetTemperatureMetric(amdsmi_processor_handle handle) noexcept;
    void GetPowerMetric(unsigned int i, amdsmi_processor_handle handle) noexcept;
    void GetPcieMetrics(unsigned int i, amdsmi_processor_handle handle) noexcept;
    void GetXgmiMetrics(unsigned int i, amdsmi_processor_handle handle) noexcept;

    Registry* registry_;
    std::unique_ptr<AmdSmi> smi_;
};

}  // namespace atlasagent
