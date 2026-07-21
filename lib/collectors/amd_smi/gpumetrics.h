#pragma once

#include "amd_smi.h"

#include <fmt/format.h>
#include <lib/logger/src/logger.h>
#include <thirdparty/spectator-cpp/spectator/registry.h>

#include <chrono>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
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

// Assumes a metric is supported until the firmware proves otherwise. The first
// Unsupported() call logs once and latches off; subsequent calls are silent, so
// the error log isn't flooded every 60s. Used for BOTH whole-call reads (skipped
// entirely once unsupported) and individual firmware metrics fields.
class FirmwareSupport
{
   public:
    explicit operator bool() const noexcept { return supported_; }

    template <typename... Args>
    void Unsupported(spdlog::format_string_t<Args...> fmt, Args&&... args) noexcept
    {
        if (supported_)
        {
            Logger()->error(fmt, std::forward<Args>(args)...);
            supported_ = false;
        }
    }

   private:
    bool supported_{true};
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

    // Gathers metrics from `self` only when present; a no-op when no AMD GPUs are available. The mirror
    // of Create(): it owns the has_value() guard so callers don't repeat it.
    static void Collect(std::optional<GpuMetricsAMD>& self) noexcept;

   private:
    struct XgmiSample
    {
        std::chrono::steady_clock::time_point timestamp;
        uint64_t read_kb;
        uint64_t write_kb;
    };

    // Per-GPU support latches for each read and each firmware metrics field.
    // Each one logs its "not supported by firmware" error once on first
    // occurrence then goes quiet; the whole-call latches additionally cause the
    // SMI read to be skipped entirely on subsequent ticks (see FirmwareSupport).
    struct GpuSupport
    {
        // Whole-call reads — skipped entirely once unsupported.
        FirmwareSupport memoryCall;
        FirmwareSupport pcieCall;
        FirmwareSupport metricsCall;
        // Individual fields within the firmware metrics struct.
        FirmwareSupport temperature;
        FirmwareSupport gfxActivity;
        FirmwareSupport umcActivity;
        FirmwareSupport gfxClock;
        FirmwareSupport memClock;
        FirmwareSupport power;
        FirmwareSupport xgmi;
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
    std::vector<GpuSupport> support_;
};

}  // namespace atlasagent
