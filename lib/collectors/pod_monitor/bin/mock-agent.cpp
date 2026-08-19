// Standalone debug tool: mirrors AtlasAgent/src/k8s-agent.cpp's 1s/5s/60s polling loop, but
// constructs only PodMonitor (no Aws/CpuFreq/Disk/Ethtool/Ntp/PerfMetrics/PressureStall/Proc/GPU/
// ServiceMonitor/EBS) and stops after two 60-second cadence intervals instead of running forever,
// so PodMonitor's real per-pod cadence wiring can be exercised by hand over a couple of full
// collection cycles without pulling in the rest of k8s-agent.cpp's collectors. Not part of the
// atlas_system_agent binary and not run by ctest.
//
// Uses a plain sleep_for instead of k8s-agent.cpp's runner.wait_for(): that helper's backing
// "terminator" object lives in the AtlasAgent target (atlas-agent.cpp), which this standalone
// tool doesn't link against.

#include <lib/collectors/pod_monitor/src/pod_monitor.h>
#include <lib/logger/src/logger.h>

#include <thirdparty/spectator-cpp/spectator/registry.h>
#include <thirdparty/spectator-cpp/libs/writer/writer_wrapper/writer_test_helper.h>

#include <fmt/format.h>

#include <chrono>
#include <cstdlib>
#include <thread>

namespace
{

void PrintAndClearMessages(MemoryWriter* writer, int intervalNumber)
{
    auto messages = writer->GetMessages();
    fmt::print("=== 60s interval {}/2 complete: {} message(s) emitted ===\n", intervalNumber, messages.size());
    for (const auto& message : messages)
    {
        fmt::print("{}", message);
    }
    writer->Clear();
}

}  // namespace

int main(int argc, char** argv)
{
    // Matches AtlasAgent/src/atlas-agent.cpp's VERBOSE_AGENT convention -- set it to see the
    // Logger()->debug(...) lines PodMonitor emits per pod (e.g. "Collecting IO stats for pod
    // ..."), which are otherwise suppressed at spdlog's default info level.
    if (std::getenv("VERBOSE_AGENT") != nullptr)
    {
        atlasagent::Logger()->set_level(spdlog::level::debug);
    }

    std::string path_prefix = argc > 1 ? argv[1] : "/sys/fs/cgroup";

    auto config = Config(WriterConfig(WriterTypes::Memory));
    auto registry = Registry(config);
    atlasagent::PodMonitor podMonitor{&registry, path_prefix};

    auto* memoryWriter = static_cast<MemoryWriter*>(WriterTestHelper::GetImpl());

    // Both cadence flags below are false on the very first loop tick, so without this call the
    // tracked-pod set would stay empty for up to 60 seconds after process startup -- mirrors
    // k8s-agent.cpp's own pre-loop call exactly.
    podMonitor.CollectMemoryStats();

    auto now = std::chrono::system_clock::now();
    auto next_run = now;
    auto next_sixty_second_run = now + std::chrono::seconds(60);
    auto next_five_second_run = now + std::chrono::seconds(5);
    std::chrono::nanoseconds time_to_sleep;

    int sixtySecondIntervalsCompleted = 0;

    do
    {
        auto start = std::chrono::system_clock::now();
        bool fiveSecondMetricsEnabled = (start >= next_five_second_run);
        bool sixtySecondMetricsEnabled = (start >= next_sixty_second_run);

        podMonitor.CollectCpuStats(fiveSecondMetricsEnabled, sixtySecondMetricsEnabled);

        // If it's time to gather the 5 second metrics
        if (fiveSecondMetricsEnabled == true)
        {
            podMonitor.CollectIOStats();
            next_five_second_run += std::chrono::seconds(5);
        }

        // If it's time to gather the 60 second metrics
        if (sixtySecondMetricsEnabled == true)
        {
            podMonitor.CollectMemoryStats();
            ++sixtySecondIntervalsCompleted;
            PrintAndClearMessages(memoryWriter, sixtySecondIntervalsCompleted);
            next_sixty_second_run += std::chrono::seconds(60);
        }

        next_run += std::chrono::seconds(1);
        time_to_sleep = next_run - std::chrono::system_clock::now();
        if (time_to_sleep.count() > 0)
        {
            std::this_thread::sleep_for(time_to_sleep);
        }
    } while (sixtySecondIntervalsCompleted < 2);

    fmt::print("mock-agent: completed {} 60-second interval(s), exiting.\n", sixtySecondIntervalsCompleted);
    return 0;
}
