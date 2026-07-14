#pragma once

// Declarations shared between the entry point (atlas-agent.cpp) and the two
// mutually-exclusive collector implementations (system-agent.cpp and
// titus-agent.cpp). Only one collector source is compiled per build, selected
// by the TITUS_SYSTEM_SERVICE definition (see the top-level CMakeLists.txt).

#include <lib/collectors/nvml/src/gpumetrics.h>
#include <lib/logger/src/logger.h>
#include <thirdparty/spectator-cpp/spectator/registry.h>

#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

using atlasagent::Logger;
using atlasagent::Nvml;
using GpuMetrics = atlasagent::GpuMetrics<Nvml>;

// Graceful-shutdown coordination shared by main()'s signal handler (which kills
// it) and the collector loops (which wait on it).
struct terminator
{
    terminator() noexcept = default;

    // returns false if killed:
    template <class R, class P>
    bool wait_for(std::chrono::duration<R, P> const& time)
    {
        if (time.count() <= 0)
        {
            Logger()->warn("waiting for zero ticks!");
            return true;
        }
        std::unique_lock<std::mutex> lock(m);
        return !cv.wait_for(lock, time, [&] { return terminate; });
    }
    void kill()
    {
        std::unique_lock<std::mutex> lock(m);
        terminate = true;
        cv.notify_all();
    }

   private:
    std::condition_variable cv;
    std::mutex m;
    bool terminate = false;
};

extern terminator runner;

// Attempt to initialize the NVML library and construct a GpuMetrics collector.
// Returns an empty pointer (and logs) if the library is unavailable, so callers
// can simply skip GPU collection when the result is falsy.
std::unique_ptr<GpuMetrics> init_gpu(Registry* registry, std::unique_ptr<Nvml> lib);

// Randomized initial delay (in seconds) to avoid publishing right on a minute
// boundary. Shared by both collector loops.
long initial_polling_delay();

#if defined(TITUS_SYSTEM_SERVICE)
void collect_titus_metrics(Registry* registry, std::unique_ptr<atlasagent::Nvml> nvidia_lib,
                           const std::unordered_map<std::string, std::string>& net_tags,
                           const int& max_monitored_services);
#else
void collect_system_metrics(Registry* registry, std::unique_ptr<atlasagent::Nvml> nvidia_lib,
                            const std::unordered_map<std::string, std::string>& net_tags,
                            const int& max_monitored_services);
#endif
