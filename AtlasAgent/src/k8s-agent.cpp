// Kubernetes-agent metric collection. Compiled only when AGENT_FLAVOR=k8s
// (AGENT_FLAVOR_K8S is defined; see the top-level CMakeLists.txt). The titus-agent
// and system-agent equivalents live in titus-agent.cpp / system-agent.cpp; shared
// helpers are declared in atlas-agent.h.
//
// The k8s flavor currently mirrors the Titus (container) collector set: both are
// cgroup-v2 container-scoped agents. It uses dedicated k8s collection methods
// (Proc::CollectK8s, Disk::k8s_disk_stats) that currently duplicate the Titus
// logic, giving k8s its own place to diverge as its resource semantics differ
// from Atlas/Titus.

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

static void gather_peak_k8s_metrics(CGroup* cGroup, const bool fiveSecondMetricsEnabled, const bool sixtySecondMetricsEnabled)
{
    cGroup->CpuStats(fiveSecondMetricsEnabled, sixtySecondMetricsEnabled);
}

static void gather_slow_k8s_metrics(CGroup* cGroup, Proc* proc, Disk* disk, Aws* aws)
{
    aws->collect();
    cGroup->MemoryStatsV2();
    cGroup->MemoryStatsStdV2();
    cGroup->NetworkStats();
    disk->k8s_disk_stats();
    proc->CollectK8s();
}

void collect_k8s_metrics(Registry* registry, const std::unordered_map<std::string, std::string>& net_tags,
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

    auto gpu = GpuMetrics::Create(registry);

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
    gather_slow_k8s_metrics(&cGroup, &proc, &disk, &aws);
    Logger()->info("Published slow Kubernetes metrics (first iteration)");

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
        gather_peak_k8s_metrics(&cGroup, fiveSecondMetricsEnabled, sixtySecondMetricsEnabled);

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
            gather_slow_k8s_metrics(&cGroup, &proc, &disk, &aws);
            perf_metrics.collect();
            GpuMetrics::Collect(gpu);
            ServiceMonitor::Collect(serviceMetrics);
            auto elapsed = duration_cast<milliseconds>(system_clock::now() - start);
            Logger()->info("Published Kubernetes metrics (delay={})", elapsed);
            next_sixty_second_run += seconds(60);
        }

        next_run += seconds(1);
        time_to_sleep = next_run - system_clock::now();
    } while (runner.wait_for(time_to_sleep));
}
