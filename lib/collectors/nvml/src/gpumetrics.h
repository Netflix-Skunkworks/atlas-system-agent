#pragma once

#include "nvml.h"
#include "utilization_samples.h"
#include <absl/base/thread_annotations.h>
#include <absl/synchronization/mutex.h>
#include <lib/logger/src/logger.h>
#include <thirdparty/spectator-cpp/spectator/registry.h>

#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>
#include <utility>
#include <vector>

namespace atlasagent
{

namespace detail
{
inline auto gauge(spectator::Registry* registry, const char* name, unsigned int gpu, const char* id = nullptr)
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
    GpuMetrics(spectator::Registry* registry, std::unique_ptr<Lib> nvml) noexcept
        : registry_(registry), nvml_(std::move(nvml))
    {
    }

    // Availability-aware factory: loads the NVML library and initializes it, logging along the way.
    // Returns std::nullopt when the library is unavailable or fails to initialize, so callers can
    // skip GPU collection. The mirror of GpuMetricsAMD::Create().
    static std::optional<GpuMetrics> Create(spectator::Registry* registry)
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

    // The returned worker must be destroyed before self. A dedicated worker
    // keeps slow minute collectors from blocking the NVML ring drain. jthread
    // stops and joins promptly on destruction, including during the wait.
    static std::jthread StartPolling(std::optional<GpuMetrics>& self)
    {
        if (!self.has_value())
        {
            return {};
        }
        self->poll_samples();  // exclude retained startup history
        return std::jthread(
            [collector = &*self](std::stop_token stop)
            {
                std::mutex mutex;
                std::condition_variable_any wake;
                std::unique_lock lock(mutex);
                while (!stop.stop_requested())
                {
                    wake.wait_for(lock, stop, std::chrono::seconds(5), [] { return false; });
                    if (!stop.stop_requested())
                    {
                        collector->poll_samples();
                    }
                }
            });
    }

    void poll_samples() noexcept
    {
        absl::MutexLock lock(&sample_data_->mutex);
        unsigned int count;
        if (!nvml_->get_count(&count))
        {
            return;
        }
        sample_data_->states.resize(count);
        for (auto i = 0u; i < count; ++i)
        {
            NvmlDeviceHandle device;
            if (!nvml_->get_by_index(i, &device))
            {
                continue;
            }
            auto& state = sample_data_->states[i];
            if (state.device != device)
            {
                state = SampleState{};
            }
            state.device = device;
            if (state.unsupported)
            {
                continue;
            }
            std::vector<NvmlSample> samples;
            auto result = nvml_->get_gpu_utilization_samples(device, state.window.last_seen(), &samples);
            if (result == NvmlRet::Success)
            {
                state.window.Add(samples);
            }
            else if (result == NvmlRet::ErrorNotSupported || result == NvmlRet::ErrorFunctionNotFound)
            {
                state.unsupported = true;
                Logger()->info("GPU {} utilization samples unavailable: {}", i, static_cast<int>(result));
            }
            else
            {
                Logger()->warn("GPU {} utilization sample poll failed: {}", i, static_cast<int>(result));
            }
        }
    }

    void gpu_metrics() noexcept
    {
        poll_samples();  // include the tail since the last background poll

        static auto gpuCountGauge =
            registry_->CreateGauge("gpu.count", std::unordered_map<std::string, std::string>{{"provider", "nvidia"}});
        static auto gpuTemperature = registry_->CreateDistributionSummary(
            "gpu.temperature", std::unordered_map<std::string, std::string>{{"provider", "nvidia"}});

        // Publish each device's sample-weighted mean once per collection
        // interval. Empty intervals emit nothing, rather than a false zero.
        std::vector<std::pair<unsigned int, double>> averages;
        {
            absl::MutexLock lock(&sample_data_->mutex);
            for (auto index = 0u; index < sample_data_->states.size(); ++index)
            {
                if (auto average = sample_data_->states[index].window.Publish())
                {
                    averages.emplace_back(index, *average);
                }
            }
        }
        // Do not hold the sampler's mutex while writing metrics.
        for (const auto& [index, average] : averages)
        {
            detail::gauge(registry_, "gpu.utilizationAvg", index).Set(average);
        }

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
    struct SampleState
    {
        NvmlDeviceHandle device{nullptr};
        GpuUtilizationWindow window;
        bool unsupported{false};
    };

    struct SampleData
    {
        // TODO: Build with Clang -Wthread-safety to check the guard annotation.
        absl::Mutex mutex;
        std::vector<SampleState> states ABSL_GUARDED_BY(mutex);
    };

    // Heap storage keeps the collector movable for the availability factory.
    std::unique_ptr<SampleData> sample_data_{std::make_unique<SampleData>()};
    spectator::Registry* registry_;
    std::unique_ptr<Lib> nvml_;
};

}  // namespace atlasagent
