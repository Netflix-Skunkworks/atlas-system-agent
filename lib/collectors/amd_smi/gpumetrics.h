#pragma once

#include "amd_smi.h"

#include <fmt/format.h>
#include <thirdparty/spectator-cpp/spectator/registry.h>

#include <chrono>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace atlasagent
{

// Nested under amd_smi_detail to avoid colliding with atlasagent::detail::gauge
// defined in lib/collectors/nvml/src/gpumetrics.h, which atlas-agent.cpp also
// includes. Used only at construction time to build the cached per-GPU meter
// handles below.
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
    Counter pcieIn;
    Counter pcieOut;
    Counter xgmiIn;
    Counter xgmiOut;

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
    // Atlas-side state for converting xGMI accumulated counters into rates.
    // Lives here (the consumer) rather than in AmdSmi because the cadence
    // and bookkeeping are collector concerns, not SMI ones.
    struct XgmiSample
    {
        std::chrono::steady_clock::time_point timestamp;
        uint64_t read_kb;
        uint64_t write_kb;
    };

    GpuMetricsAMD(Registry* registry, std::unique_ptr<AmdSmi> smi, uint32_t count) noexcept;

    void Collect(uint32_t gpu_id) noexcept;
    void RecordMemory(uint32_t gpu_id, const AmdSmiMemory& memory) noexcept;
    void RecordPCIEThroughput(uint32_t gpu_id, const AmdSmiThroughput& pcie) noexcept;
    void RecordTemperature(uint32_t gpu_id, const amdsmi_gpu_metrics_t& m) noexcept;
    void RecordActivity(uint32_t gpu_id, const amdsmi_gpu_metrics_t& m) noexcept;
    void RecordClocks(uint32_t gpu_id, const amdsmi_gpu_metrics_t& m) noexcept;
    void RecordPower(uint32_t gpu_id, const amdsmi_gpu_metrics_t& m) noexcept;
    void RecordXgmi(uint32_t gpu_id, const amdsmi_gpu_metrics_t& m) noexcept;

    std::unique_ptr<AmdSmi> smi_;
    Gauge gpuCount_;
    DistributionSummary temperature_;
    std::vector<PerGpuMeters> meters_;
    std::vector<std::optional<XgmiSample>> last_xgmi_;
};

}  // namespace atlasagent
