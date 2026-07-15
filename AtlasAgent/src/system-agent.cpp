// System-agent metric collection. Compiled only when TITUS_SYSTEM_SERVICE is
// NOT defined (see AtlasAgent/CMakeLists.txt). The titus-agent equivalent lives
// in titus-agent.cpp; shared helpers are declared in atlas-agent.h.

#include "atlas-agent.h"

#include <lib/collectors/amd_smi/gpumetrics.h>
#include <lib/collectors/aws/src/aws.h>
#include <lib/collectors/cpu_freq/src/cpu_freq.h>
#include <lib/collectors/dcgm/src/dcgm_stats.h>
#include <lib/collectors/disk/src/disk.h>
#include <lib/collectors/ebs/src/ebs.h>
#include <lib/collectors/ethtool/src/ethtool.h>
#include <lib/collectors/ntp/src/ntp.h>
#include <lib/collectors/perf_metrics/src/perf_metrics.h>
#include <lib/collectors/perfspect/src/perfspect.h>
#include <lib/collectors/pressure_stall/src/pressure_stall.h>
#include <lib/collectors/proc/src/proc.h>
#include <lib/collectors/service_monitor/src/service_monitor.h>
#include <lib/util/src/util.h>

#include <fmt/chrono.h>

#include <optional>
#include <regex>
#include <unordered_set>
#include <vector>

using Aws = atlasagent::Aws;
using CpuFreq = atlasagent::CpuFreq;
using Disk = atlasagent::Disk;
using Ethtool = atlasagent::Ethtool;
using Ntp = atlasagent::Ntp<>;
using PerfMetrics = atlasagent::PerfMetrics;
using PressureStall = atlasagent::PressureStall;
using Proc = atlasagent::Proc;

static void gather_peak_system_metrics(Proc* proc, const bool fiveSecondMetricsEnabled, const bool sixtySecondMetricsEnabled)
{
    proc->CpuStats(fiveSecondMetricsEnabled, sixtySecondMetricsEnabled);
}

static void gather_scaling_metrics(CpuFreq* cpufreq) { cpufreq->Stats(); }

static void gather_slow_system_metrics(Proc* proc, Disk* disk, Ethtool* ethtool, Ntp* ntp, PressureStall* pressureStall,
                                       Aws* aws)
{
    aws->collect();
    disk->disk_stats();
    ethtool->collect();
    ntp->collect();
    pressureStall->collect();
    proc->CollectSystem();
}

void collect_system_metrics(Registry* registry, std::unique_ptr<atlasagent::Nvml> nvidia_lib,
                            const std::unordered_map<std::string, std::string>& net_tags,
                            const int& max_monitored_services)
{
    using std::chrono::duration_cast;
    using std::chrono::milliseconds;
    using std::chrono::seconds;
    using std::chrono::system_clock;

    Aws aws{registry};
    CpuFreq cpufreq{registry};
    Disk disk{registry, ""};
    Ethtool ethtool{registry, net_tags};
    Ntp ntp{registry};
    PerfMetrics perf_metrics{registry, ""};
    PressureStall pressureStall{registry};
    Proc proc{registry, net_tags};

    auto gpu = init_gpu(registry, std::move(nvidia_lib));

    // TODO: DCGM, EBS, and ServiceMonitor have Dynamic metric collection. During each iteration we have to
    // check if these optionals have a set value. lets improve how we handle this
    // Each collector's availability check + logging lives in its own Create() factory.
    auto gpuDCGM = GpuMetricsDCGM::Create(registry);
    auto serviceMetrics = ServiceMonitor::Create(registry, max_monitored_services);
    auto perfspectMetrics = Perfspect::Create(registry);
    auto ebsMetrics = EBSCollector::Create(registry);
    auto gpuAMD = atlasagent::GpuMetricsAMD::Create(registry);

    // initial polling delay, to prevent publishing too close to a minute boundary
    auto delay = initial_polling_delay();
    Logger()->info("Initial polling delay is {}s", delay);
    if (delay > 0)
    {
        runner.wait_for(seconds(delay));
    }

    // the first call to this gather function takes ~100ms, so it must be
    // done before we start calculating times to wait for peak metrics
    gather_slow_system_metrics(&proc, &disk, &ethtool, &ntp, &pressureStall, &aws);
    Logger()->info("Published slow system metrics (first iteration)");

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

        // Gather one second metrics
        // Proc has been modified to optionally gather 5 second and 60 second metrics during this call
        // This prevents having to read proc/stat multiple times if both 5 and 60 second metrics are enabled
        gather_peak_system_metrics(&proc, fiveSecondMetricsEnabled, sixtySecondMetricsEnabled);
        gather_scaling_metrics(&cpufreq);

        // If it's time to gather the 5 second metrics
        if (fiveSecondMetricsEnabled == true)
        {
            Logger()->debug("Gathering 5 second metrics");
            Perfspect::Collect(perfspectMetrics);
            next_five_second_run += seconds(5);
        }

        // If it's time to gather the 60 second metrics
        if (sixtySecondMetricsEnabled == true)
        {
            Logger()->debug("Gathering 60 second metrics");
            gather_slow_system_metrics(&proc, &disk, &ethtool, &ntp, &pressureStall, &aws);
            perf_metrics.collect();
            if (gpu)
            {
                gpu->gpu_metrics();
            }

            atlasagent::GpuMetricsAMD::Collect(gpuAMD);
            GpuMetricsDCGM::Collect(gpuDCGM);
            EBSCollector::Collect(ebsMetrics);
            ServiceMonitor::Collect(serviceMetrics);

            auto elapsed = duration_cast<milliseconds>(system_clock::now() - start);
            Logger()->debug("Published system metrics (delay={})", elapsed);
            next_sixty_second_run += seconds(60);
        }

        next_run += seconds(1);
        time_to_sleep = next_run - system_clock::now();
    } while (runner.wait_for(time_to_sleep));
}
