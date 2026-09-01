// Standalone debug tool: a sibling of mock-agent.cpp that runs no collectors at all. Every 60
// seconds it refreshes PodMonitor's discovery/tracking state and prints, for every pod, its
// PodInfo (everything the apiserver knows: name/namespace/uid plus every container it reports,
// regardless of whether that container passed the env-var Gating rule) side by side with its
// TrackedPod/TrackedContainer state (only the containers that actually gated in and are being
// emitted for). Useful for inspecting Gating behavior on a real node without touching spectatord
// at all. Not part of the atlas_system_agent binary and not run by ctest.
//
// Uses a plain sleep_for instead of k8s-agent.cpp's runner.wait_for(): that helper's backing
// "terminator" object lives in the AtlasAgent target (atlas-agent.cpp), which this standalone
// tool doesn't link against.

#include <lib/collectors/pod_monitor/src/pod_monitor.h>
#include <lib/logger/src/logger.h>
#include <lib/util/src/util.h>

#include <thirdparty/spectator-cpp/spectator/registry.h>

#include <fmt/format.h>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <optional>
#include <string>
#include <thread>
#include <unistd.h>
#include <unordered_map>
#include <utility>
#include <vector>

namespace
{

// PodMonitor keeps RefreshTrackedPods()/TrackedPods() protected -- this repo's own convention for
// exposing them to a standalone caller outside the class (see pod_monitor_test.cpp's
// PodMonitorTest) is a thin subclass with `using` declarations, not new public API on PodMonitor
// itself just for a debug tool.
class PodMonitorIntrospect : public atlasagent::PodMonitor
{
   public:
    using PodMonitor::PodMonitor;
    using PodMonitor::RefreshTrackedPods;
    using PodMonitor::TrackedPods;
    using PodMonitor::FindContainersInPod;
};

// Reads a container's environment the same way PodMonitor::RefreshTrackedPods() does internally
// (cgroup.procs -> first PID -> /proc/<pid>/environ), purely for printing here -- this tool never
// feeds the result into ResolveContainerTags/Gating, it just shows what's actually there.
std::optional<std::unordered_map<std::string, std::string>> ReadContainerEnviron(
    const std::filesystem::path& container_cgroup_path)
{
    auto pid_lines = atlasagent::read_file((container_cgroup_path / "cgroup.procs").string());
    if (!pid_lines.has_value() || pid_lines->empty())
    {
        return std::nullopt;
    }
    auto pid = static_cast<pid_t>(std::strtol(pid_lines->front().c_str(), nullptr, 10));
    return atlasagent::read_process_environ(pid);
}

void PrintSnapshot(const atlasagent::PodInfoMap& discovered, const atlasagent::PodTrackedMap& tracked,
                    int intervalNumber)
{
    auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    fmt::print("=== interval {} -- {} pod(s) discovered, {} tracked -- {}", intervalNumber, discovered.size(),
               tracked.size(), std::ctime(&now));

    for (const auto& [uid, info] : discovered)
    {
        fmt::print("  pod {} (name={}, namespace={}, cgroup_path={})\n", uid, info.name, info.pod_namespace,
                    info.cgroup_path.string());

        fmt::print("    PodInfo.containers (apiserver-known, {} total):\n", info.containers.size());
        for (const auto& [container_id, container_name] : info.containers)
        {
            fmt::print("      {} -> {}\n", container_id, container_name);
        }

        auto tracked_it = tracked.find(uid);
        if (tracked_it == tracked.end())
        {
            fmt::print("    TrackedPod: none (not tracked -- no container gated in yet)\n");
            continue;
        }

        const auto& tracked_pod = tracked_it->second;
        fmt::print("    TrackedPod (name={}, namespace={}), {} container(s) gated in:\n", tracked_pod.name,
                    tracked_pod.pod_namespace, tracked_pod.containers.size());
        for (const auto& [container_id, tracked_container] : tracked_pod.containers)
        {
            fmt::print("      {} (container_id={}, container_name={})\n", container_id,
                        tracked_container.container_id, tracked_container.container_name);
        }

        // Every cgroup-discovered container, tracked or not -- this is what actually feeds
        // Gating, so it's shown regardless of whether the container above gated in.
        auto containers_in_pod = PodMonitorIntrospect::FindContainersInPod(info.cgroup_path);
        fmt::print("    Container environments ({} cgroup-discovered):\n", containers_in_pod.size());
        for (const auto& [container_id, container_cgroup_path] : containers_in_pod)
        {
            auto environ_now = ReadContainerEnviron(container_cgroup_path);
            if (!environ_now.has_value())
            {
                fmt::print("      {}: environ unreadable (no PID in cgroup.procs yet, or process exited)\n",
                            container_id);
                continue;
            }

            std::vector<std::pair<std::string, std::string>> sorted_entries(environ_now->begin(), environ_now->end());
            std::sort(sorted_entries.begin(), sorted_entries.end());

            fmt::print("      {} ({} env var(s)):\n", container_id, sorted_entries.size());
            for (const auto& [key, value] : sorted_entries)
            {
                fmt::print("        {}={}\n", key, value);
            }
        }
    }

    fmt::print("\n");
}

}  // namespace

int main(int argc, char** argv)
{
    // Matches AtlasAgent/src/atlas-agent.cpp's VERBOSE_AGENT convention -- set it to see the
    // Logger()->debug(...) lines PodMonitor emits internally, otherwise suppressed at spdlog's
    // default info level.
    if (std::getenv("VERBOSE_AGENT") != nullptr)
    {
        atlasagent::Logger()->set_level(spdlog::level::debug);
    }

    std::string path_prefix = argc > 1 ? argv[1] : "/sys/fs/cgroup";

    // No collector ever runs here, so no metric is ever emitted -- Memory is the simplest writer
    // that needs no real spectatord socket/UDP listener to construct successfully.
    auto config = Config(WriterConfig(WriterTypes::Memory));
    auto registry = Registry(config);
    PodMonitorIntrospect podMonitor{&registry, path_prefix};

    constexpr int kTotalIntervals = 60;
    for (int interval = 1; interval <= kTotalIntervals; ++interval)
    {
        podMonitor.RefreshTrackedPods();
        auto discovered = podMonitor.FindAllActivePods2();
        PrintSnapshot(discovered, podMonitor.TrackedPods(), interval);

        if (interval < kTotalIntervals)
        {
            std::this_thread::sleep_for(std::chrono::seconds(60));
        }
    }

    fmt::print("mock-agent-2: completed {} interval(s), exiting.\n", kTotalIntervals);
    return 0;
}
