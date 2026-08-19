// K8s-agent metric collection. Compiled only when AGENT_FLAVOR=k8s
// (AGENT_FLAVOR_K8S is defined; see AtlasAgent/CMakeLists.txt). The system-agent
// and titus-agent equivalents live in system-agent.cpp / titus-agent.cpp; shared
// helpers are declared in atlas-agent.h.

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
#include <lib/collectors/pod_monitor/src/pod_monitor.h>
#include <lib/collectors/pressure_stall/src/pressure_stall.h>
#include <lib/collectors/proc/src/proc.h>
#include <lib/collectors/service_monitor/src/service_monitor.h>
#include <lib/util/src/util.h>

#include <fmt/chrono.h>

#include <optional>
#include <regex>
#include <unordered_set>
#include <vector>

static void gather_peak_system_metrics(atlasagent::Proc* proc, const bool fiveSecondMetricsEnabled,
                                       const bool sixtySecondMetricsEnabled)
{
    proc->CpuStats(fiveSecondMetricsEnabled, sixtySecondMetricsEnabled);
}

static void gather_scaling_metrics(atlasagent::CpuFreq* cpufreq) { cpufreq->Stats(); }

static void gather_slow_system_metrics(atlasagent::Proc* proc, atlasagent::Disk* disk, atlasagent::Ethtool* ethtool,
                                       atlasagent::Ntp<>* ntp, atlasagent::PressureStall* pressureStall,
                                       atlasagent::Aws* aws)
{
    aws->collect();
    disk->disk_stats();
    ethtool->collect();
    ntp->collect();
    pressureStall->collect();
    proc->CollectSystem();
}

void collect_k8s_metrics(Registry* registry, const std::unordered_map<std::string, std::string>& net_tags,
                         const int& max_monitored_services)
{
    atlasagent::Aws aws{registry};
    atlasagent::CpuFreq cpufreq{registry};
    atlasagent::Disk disk{registry, ""};
    atlasagent::Ethtool ethtool{registry, net_tags};
    atlasagent::Ntp<> ntp{registry};
    atlasagent::PerfMetrics perf_metrics{registry, ""};
    atlasagent::PodMonitor podMonitor{registry};
    atlasagent::PressureStall pressureStall{registry};
    atlasagent::Proc proc{registry, net_tags};

    auto gpu = GpuMetrics::Create(registry);
    auto gpuDCGM = GpuMetricsDCGM::Create(registry);
    auto serviceMetrics = ServiceMonitor::Create(registry, max_monitored_services);
    auto perfspectMetrics = Perfspect::Create(registry);
    auto ebsMetrics = EBSCollector::Create(registry);
    auto gpuAMD = atlasagent::GpuMetricsAMD::Create(registry);

    // Initial polling delay, to prevent publishing too close to a minute boundary
    auto delay = initial_polling_delay();
    Logger()->info("Initial polling delay is {}s", delay);
    if (delay > 0)
    {
        runner.wait_for(std::chrono::seconds(delay));
    }

    // The first call to this gather function takes ~100ms, so it must be
    // done before we start calculating times to wait for peak metrics
    gather_slow_system_metrics(&proc, &disk, &ethtool, &ntp, &pressureStall, &aws);
    Logger()->info("Published slow system metrics (first iteration)");

    // Both cadence flags below are false on the very first loop tick, so without this call the
    // tracked-pod set would stay empty for up to 60 seconds after process startup.
    podMonitor.CollectMemoryStats();

    auto now = std::chrono::system_clock::now();
    auto next_run = now;
    auto next_sixty_second_run = now + std::chrono::seconds(60);
    auto next_five_second_run = now + std::chrono::seconds(5);
    std::chrono::nanoseconds time_to_sleep;

    do
    {
        auto start = std::chrono::system_clock::now();
        bool fiveSecondMetricsEnabled = (start >= next_five_second_run);
        bool sixtySecondMetricsEnabled = (start >= next_sixty_second_run);

        // Gather one second metrics
        // Proc has been modified to optionally gather 5 second and 60 second metrics during this call
        // This prevents having to read proc/stat multiple times if both 5 and 60 second metrics are enabled
        gather_peak_system_metrics(&proc, fiveSecondMetricsEnabled, sixtySecondMetricsEnabled);
        gather_scaling_metrics(&cpufreq);
        podMonitor.CollectCpuStats(fiveSecondMetricsEnabled, sixtySecondMetricsEnabled);

        // If it's time to gather the 5 second metrics
        if (fiveSecondMetricsEnabled == true)
        {
            Logger()->debug("Gathering 5 second metrics");
            Perfspect::Collect(perfspectMetrics);
            podMonitor.CollectIOStats();
            next_five_second_run += std::chrono::seconds(5);
        }

        // If it's time to gather the 60 second metrics
        if (sixtySecondMetricsEnabled == true)
        {
            Logger()->debug("Gathering 60 second metrics");
            gather_slow_system_metrics(&proc, &disk, &ethtool, &ntp, &pressureStall, &aws);
            perf_metrics.collect();
            EBSCollector::Collect(ebsMetrics);
            podMonitor.CollectMemoryStats();

            auto elapsed =
                std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now() - start);
            Logger()->debug("Published system metrics (delay={})", elapsed);
            next_sixty_second_run += std::chrono::seconds(60);
        }

        next_run += std::chrono::seconds(1);
        time_to_sleep = next_run - std::chrono::system_clock::now();
    } while (runner.wait_for(time_to_sleep));
}
