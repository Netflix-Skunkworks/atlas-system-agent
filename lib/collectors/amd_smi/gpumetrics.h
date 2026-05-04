#pragma once

#include "amd_smi.h"

#include <fmt/format.h>
#include <thirdparty/spectator-cpp/spectator/registry.h>

#include <memory>
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

namespace detail
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
}  // namespace detail

template <typename Lib>
class GpuMetricsAMD
{
   public:
    GpuMetricsAMD(Registry* registry, std::unique_ptr<Lib> smi) noexcept
        : registry_{registry}, smi_{std::move(smi)}
    {
    }

    GpuMetricsAMD(const GpuMetricsAMD&) = delete;
    GpuMetricsAMD& operator=(const GpuMetricsAMD&) = delete;
    GpuMetricsAMD(GpuMetricsAMD&&) = delete;
    GpuMetricsAMD& operator=(GpuMetricsAMD&&) = delete;

    void gpu_metrics() noexcept;

   private:
    Registry* registry_;
    std::unique_ptr<Lib> smi_;
};

}  // namespace atlasagent
