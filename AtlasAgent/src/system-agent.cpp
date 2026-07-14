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
    aws->update_stats();
    disk->disk_stats();
    ethtool->update_stats();
    ntp->update_stats();
    pressureStall->update_stats();
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

    std::optional<GpuMetricsDCGM> gpuDCGM{std::nullopt};
    if (atlasagent::is_file_present(DCGMConstants::dcgmiPath))
    {
        gpuDCGM.emplace(registry);
    }

    // TODO: DCGM, EBS, and ServiceMonitor have Dynamic metric collection. During each iteration we have to
    // check if these optionals have a set value. lets improve how we handle this

    // Create a ServiceMonitor object to monitor Systemd services if any configs are valid
    std::optional<ServiceMonitor> serviceMetrics{};
    std::optional<std::vector<std::regex> > serviceConfig{
        parse_service_monitor_config_directory(ServiceMonitorConstants::ConfigPath)};
    if (serviceConfig.has_value())
    {
        serviceMetrics.emplace(registry, serviceConfig.value(), max_monitored_services);
    }
    else
    {
        Logger()->info("Service Monitoring is disabled.");
    }

    std::optional<Perfspect> perfspectMetrics{};
    auto instanceInfo = Perfspect::IsValidInstance();
    if (instanceInfo.has_value())
    {
        perfspectMetrics.emplace(registry, instanceInfo.value());
    }
    else
    {
        Logger()->info("PerfSpect Monitoring is disabled.");
    }

    // Create an EBS collector object to monitor EBS devices if any configs are valid
    std::optional<EBSCollector> ebsMetrics{};
    std::optional<std::unordered_set<std::string> > ebsConfig{parse_ebs_config_directory(EBSConstants::ConfigPath)};
    if (ebsConfig.has_value())
    {
        ebsMetrics.emplace(registry, ebsConfig.value());
    }
    else
    {
        Logger()->info("EBS Monitoring is disabled.");
    }

    if (gpuDCGM.has_value())
    {
        std::string serviceStatus = atlasagent::is_service_running(DCGMConstants::ServiceName) ? "ON" : "OFF";
        Logger()->info(
            "DCGMI binary present. Agent will collect DCGM metrics if service is ON. DCGM service state: {}.",
            serviceStatus);
    }
    else
    {
        Logger()->info("DCGMI binary not present. Agent will not collect DCGM metrics.");
    }

    auto gpuAMD = atlasagent::GpuMetricsAMD::Create(registry);
    if (gpuAMD.has_value())
    {
        Logger()->info("AMD GPU(s) detected. Agent will collect AMD SMI metrics.");
    }
    else
    {
        Logger()->info("No AMD GPUs detected. Agent will not collect AMD SMI metrics.");
    }

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
            if (perfspectMetrics.has_value())
            {
                perfspectMetrics->GatherMetrics();
            }
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

            if (gpuAMD.has_value())
            {
                gpuAMD->GPUMetrics();
            }

            if (gpuDCGM.has_value() && atlasagent::is_service_running(DCGMConstants::ServiceName))
            {
                if (gpuDCGM.value().gather_metrics() == false)
                {
                    Logger()->error("Failed to gather DCGM metrics");
                }
            }

            if (ebsMetrics.has_value() && ebsMetrics.value().gather_metrics() == false)
            {
                Logger()->error("Failed to gather EBS metrics");
            }

            if (serviceMetrics.has_value() && serviceMetrics.value().gather_metrics() == false)
            {
                Logger()->error("Failed to gather Service metrics");
            }

            auto elapsed = duration_cast<milliseconds>(system_clock::now() - start);
            Logger()->debug("Published system metrics (delay={})", elapsed);
            next_sixty_second_run += seconds(60);
        }

        next_run += seconds(1);
        time_to_sleep = next_run - system_clock::now();
    } while (runner.wait_for(time_to_sleep));
}
