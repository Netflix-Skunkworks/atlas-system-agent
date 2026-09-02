#pragma once

// Declarations shared between the entry point (atlas-agent.cpp) and the three
// mutually-exclusive collector implementations (system-agent.cpp,
// titus-agent.cpp, and k8s-agent.cpp). Only one collector source is compiled per
// build, selected by AGENT_FLAVOR (system|titus|k8s), which maps to one of the
// AGENT_FLAVOR_{SYSTEM,TITUS,K8S} defines (see the top-level CMakeLists.txt).

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

// Randomized initial delay (in seconds) to avoid publishing right on a minute
// boundary. Shared by both collector loops.
long initial_polling_delay();

#if defined(AGENT_FLAVOR_TITUS)
void collect_titus_metrics(Registry* registry, const std::unordered_map<std::string, std::string>& net_tags,
                           const int& max_monitored_services);
#elif defined(AGENT_FLAVOR_K8S)
// Both atlas-agent.cpp's shared Registry (common_tags supplied explicitly, see main()) and
// PodMonitor's own dedicated Registry (deliberately no common_tags -- see k8s-agent.cpp) point
// at this same spectatord socket variant; named once here so neither copy can drift.
struct K8sAgentConstants
{
    static constexpr auto SpectatordSocket = "unix:///run/spectatord-notags/spectatord.unix";
};

void collect_k8s_metrics(Registry* registry, const std::unordered_map<std::string, std::string>& net_tags,
                         const int& max_monitored_services);
#else
void collect_system_metrics(Registry* registry, const std::unordered_map<std::string, std::string>& net_tags,
                            const int& max_monitored_services);
#endif
