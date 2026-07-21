#pragma once

#include "nvml.h"
#include <lib/logger/src/logger.h>
#include <thirdparty/spectator-cpp/spectator/registry.h>

#include <cstdio>
#include <memory>
#include <optional>
#include <utility>

namespace atlasagent
{

namespace detail
{
inline auto gauge(Registry* registry, const char* name, unsigned int gpu, const char* id = nullptr)
{
    std::unordered_map<std::string, std::string> tags = {
        {"gpu", fmt::format("{}", gpu)},
        {"provider", "nvidia"},
    };
    if (id != nullptr)
    {
        tags["id"] = id;
    }
    return registry->CreateGauge(name, tags);
}

}  // namespace detail

template <typename Lib>
class GpuMetrics
{
   public:
    GpuMetrics(Registry* registry, std::unique_ptr<Lib> nvml) noexcept : registry_(registry), nvml_(std::move(nvml)) {}

    // Availability-aware factory: loads the NVML library and initializes it, logging along the way.
    // Returns std::nullopt when the library is unavailable or fails to initialize, so callers can
    // skip GPU collection. The mirror of GpuMetricsAMD::Create().
    static std::optional<GpuMetrics> Create(Registry* registry)
    {
        std::unique_ptr<Lib> lib;
        try
        {
            lib = std::make_unique<Lib>();
            Logger()->info("Will attempt to collect GPU metrics");
        }
        catch (NvmlException& e)
        {
            Logger()->info("Will not collect GPU metrics: {}", e.what());
            return std::nullopt;
        }

        try
        {
            lib->initialize();
            return GpuMetrics(registry, std::move(lib));
        }
        catch (NvmlException& e)
        {
            fprintf(stderr, "Will not collect GPU metrics: %s\n", e.what());
        }
        return std::nullopt;
    }

    // Gathers metrics from `self` only when present; a no-op when GPU collection is disabled. The
    // mirror of Create(): it owns the has_value() guard so callers don't repeat it.
    static void Collect(std::optional<GpuMetrics>& self) noexcept
    {
        if (self.has_value())
        {
            self->gpu_metrics();
        }
    }

    void gpu_metrics() noexcept
    {
        static auto gpuCountGauge = registry_->CreateGauge(
            "gpu.count", std::unordered_map<std::string, std::string>{{"provider", "nvidia"}});
        static auto gpuTemperature = registry_->CreateDistributionSummary(
            "gpu.temperature", std::unordered_map<std::string, std::string>{{"provider", "nvidia"}});

        unsigned count;
        if (!nvml_->get_count(&count))
        {
            return;
        }

        gpuCountGauge.Set(count);
        for (auto i = 0u; i < count; ++i)
        {
            NvmlDeviceHandle device;
            if (nvml_->get_by_index(i, &device))
            {
                NvmlMemory memory;
                if (nvml_->get_memory_info(device, &memory))
                {
                    detail::gauge(registry_, "gpu.usedMemory", i).Set(memory.used);
                    detail::gauge(registry_, "gpu.freeMemory", i).Set(memory.free);
                    detail::gauge(registry_, "gpu.totalMemory", i).Set(memory.total);
                }

                NvmlUtilization utilization;
                if (nvml_->get_utilization_rates(device, &utilization))
                {
                    detail::gauge(registry_, "gpu.utilization", i).Set(utilization.gpu);
                    detail::gauge(registry_, "gpu.memoryActivity", i).Set(utilization.memory);
                }

                NvmlPerfState perf_state = -1;
                if (nvml_->get_performance_state(device, &perf_state))
                {
                    detail::gauge(registry_, "gpu.perfState", i).Set(static_cast<double>(perf_state));
                }

                unsigned temp;
                if (nvml_->get_temperature(device, &temp))
                {
                    gpuTemperature.Record(temp);
                }
            }
        }
    }

   private:
    Registry* registry_;
    std::unique_ptr<Lib> nvml_;
};

}  // namespace atlasagent
