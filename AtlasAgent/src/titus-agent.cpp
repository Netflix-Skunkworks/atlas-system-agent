// Titus-agent metric collection. Compiled only when TITUS_SYSTEM_SERVICE is
// defined (see AtlasAgent/CMakeLists.txt). The system-agent equivalent lives in
// system-agent.cpp; shared helpers are declared in atlas-agent.h.

#include "atlas-agent.h"

#include <lib/collectors/aws/src/aws.h>
#include <lib/collectors/cgroup/src/cgroup.h>
#include <lib/collectors/disk/src/disk.h>
#include <lib/collectors/perf_metrics/src/perf_metrics.h>
#include <lib/collectors/proc/src/proc.h>
#include <lib/collectors/service_monitor/src/service_monitor.h>

#include <fmt/chrono.h>

#include <optional>
#include <regex>
#include <vector>

using Aws = atlasagent::Aws;
using CGroup = atlasagent::CGroup;
using Disk = atlasagent::Disk;
using PerfMetrics = atlasagent::PerfMetrics;
using Proc = atlasagent::Proc;

static void gather_peak_titus_metrics(CGroup* cGroup, const bool fiveSecondMetricsEnabled, const bool sixtySecondMetricsEnabled)
{
    cGroup->CpuStats(fiveSecondMetricsEnabled, sixtySecondMetricsEnabled);
}

static void gather_slow_titus_metrics(CGroup* cGroup, Proc* proc, Disk* disk, Aws* aws)
{
    aws->collect();
    cGroup->MemoryStatsV2();
    cGroup->MemoryStatsStdV2();
    cGroup->NetworkStats();
    disk->titus_disk_stats();
    proc->CollectTitus();
}

void collect_titus_metrics(Registry* registry, std::unique_ptr<atlasagent::Nvml> nvidia_lib,
                           const std::unordered_map<std::string, std::string>& net_tags,
                           const int& max_monitored_services)
{
    using std::chrono::duration_cast;
    using std::chrono::milliseconds;
    using std::chrono::seconds;
    using std::chrono::system_clock;

    Aws aws{registry};
    CGroup cGroup{registry};
    Disk disk{registry, ""};
    PerfMetrics perf_metrics{registry, ""};
    Proc proc{registry, std::move(net_tags)};

    auto gpu = init_gpu(registry, std::move(nvidia_lib));

    // TODO: DCGM & ServiceMonitor have Dynamic metric collection. During each iteration we have to
    // check if these optionals have a set value. lets improve how we handle this
    auto serviceMetrics = ServiceMonitor::Create(registry, max_monitored_services);

    // initial polling delay, to prevent publishing too close to a minute boundary
    auto delay = initial_polling_delay();
    Logger()->info("Initial polling delay is {}s", delay);
    if (delay > 0)
    {
        runner.wait_for(seconds(delay));
    }

    // the first call to this gather function takes ~100ms, so it must be
    // done before we start calculating times to wait for peak metrics
    gather_slow_titus_metrics(&cGroup, &proc, &disk, &aws);
    Logger()->info("Published slow Titus metrics (first iteration)");

    auto now = system_clock::now();
    auto next_run = now;
    auto next_sixty_second_run = now + seconds(60);
    auto next_five_second_run = now + seconds(5);
    std::chrono::nanoseconds time_to_sleep;

    do
    {
        auto start = system_clock::now();
        bool fiveSecondMetricsEnabled = (start >= next_five_second_run);
        bool sixtySecondMetricsEnabled = (start >= next_sixty_second_run);

        // 1 second, 5 second, and 60 second CPU metrics are gathered here because they read from
        // the same /proc/stat file
        gather_peak_titus_metrics(&cGroup, fiveSecondMetricsEnabled, sixtySecondMetricsEnabled);

        // If its time to gather 5 second metrics, update the next run time
        // Currently we only have CPU metrics that run every 5 seconds, but if we add more in the future
        // we can gather them here
        if (fiveSecondMetricsEnabled == true)
        {
            cGroup.IOStats();
            next_five_second_run += seconds(5);
        }

        // If its time to gather 60 second metrics, gather the metrics and update the next run time
        if (sixtySecondMetricsEnabled == true)
        {
            gather_slow_titus_metrics(&cGroup, &proc, &disk, &aws);
            perf_metrics.collect();
            if (gpu)
            {
                gpu->gpu_metrics();
            }
            ServiceMonitor::Collect(serviceMetrics);
            auto elapsed = duration_cast<milliseconds>(system_clock::now() - start);
            Logger()->info("Published Titus metrics (delay={})", elapsed);
            next_sixty_second_run += seconds(60);
        }

        next_run += seconds(1);
        time_to_sleep = next_run - system_clock::now();
    } while (runner.wait_for(time_to_sleep));
}
