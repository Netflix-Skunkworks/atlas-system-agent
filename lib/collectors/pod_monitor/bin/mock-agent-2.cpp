// Standalone debug tool: a sibling of mock-agent.cpp that runs no collectors at all. Every 60
// seconds it refreshes PodMonitor's discovery/tracking state and prints, for every pod, its
// PodInfo (everything kubelet's local API knows: name/namespace/uid, annotations, labels, and
// every container it reports) side by side with the ResolvePodTags decision for that pod and its
// TrackedPod/TrackedContainer state (only the containers that actually gated in and are being
// emitted for). Useful for inspecting Gating behavior on a real node without touching spectatord
// at all. Not part of the atlas_system_agent binary and not run by ctest.
//
// Uses a plain sleep_for instead of k8s-agent.cpp's runner.wait_for(): that helper's backing
// "terminator" object lives in the AtlasAgent target (atlas-agent.cpp), which this standalone
// tool doesn't link against.

#include <lib/collectors/pod_monitor/src/pod_monitor.h>
#include <lib/logger/src/logger.h>

#include <thirdparty/spectator-cpp/spectator/registry.h>

#include <fmt/format.h>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <ctime>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

namespace
{

// PodMonitor keeps RefreshTrackedPods()/TrackedPods()/ResolvePodTags() protected -- this repo's
// own convention for exposing them to a standalone caller outside the class (see
// pod_monitor_test.cpp's PodMonitorTest) is a thin subclass with `using` declarations, not new
// public API on PodMonitor itself just for a debug tool.
class PodMonitorIntrospect : public atlasagent::PodMonitor
{
   public:
    using PodMonitor::PodMonitor;
    using PodMonitor::RefreshTrackedPods;
    using PodMonitor::TrackedPods;
    using PodMonitor::FindContainersInPod;
    using PodMonitor::ResolvePodTags;
};

void PrintSortedMap(const std::unordered_map<std::string, std::string>& values, const char* indent)
{
    std::vector<std::pair<std::string, std::string>> sorted_entries(values.begin(), values.end());
    std::sort(sorted_entries.begin(), sorted_entries.end());
    for (const auto& [key, value] : sorted_entries)
    {
        fmt::print("{}{}={}\n", indent, key, value);
    }
}

void PrintSnapshot(const atlasagent::PodInfoMap& discovered, const atlasagent::PodTrackedMap& tracked,
                    const std::string& k8s_cluster, int intervalNumber)
{
    auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    fmt::print("=== interval {} -- {} pod(s) discovered, {} tracked -- {}", intervalNumber, discovered.size(),
               tracked.size(), std::ctime(&now));

    for (const auto& [uid, info] : discovered)
    {
        fmt::print("  pod {} (name={}, namespace={}, cgroup_path={})\n", uid, info.name, info.pod_namespace,
                    info.cgroup_path.string());

        fmt::print("    PodInfo.containers (kubelet-known, {} total):\n", info.containers.size());
        for (const auto& [container_id, container_name] : info.containers)
        {
            fmt::print("      {} -> {}\n", container_id, container_name);
        }

        fmt::print("    Pod annotations ({} total):\n", info.annotations.size());
        PrintSortedMap(info.annotations, "      ");

        fmt::print("    Pod labels ({} total):\n", info.labels.size());
        PrintSortedMap(info.labels, "      ");

        // This is the exact call RefreshTrackedPods() makes internally -- shown here purely for
        // display, never fed back into anything.
        auto pod_tags = PodMonitorIntrospect::ResolvePodTags(info.annotations, info.labels, info.name, k8s_cluster);
        if (!pod_tags.has_value())
        {
            fmt::print(
                "    ResolvePodTags: GATED OUT -- none of netflix.com/app, netflix.com/stack, "
                "netflix.com/detail annotation, or app.kubernetes.io/name, k8s-app, app, "
                "app.kubernetes.io/instance, app.kubernetes.io/component label resolved. No metrics "
                "for any container in this pod.\n");
        }
        else
        {
            fmt::print("    ResolvePodTags (applies to every container in this pod, nf.process added per-container):\n");
            PrintSortedMap(*pod_tags, "      ");
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

        // Every cgroup-discovered container, tracked or not -- lets you see a container that's
        // been discovered on disk but isn't in PodInfo.containers yet (kubelet racing the cgroup
        // appearing), which is why it wouldn't show up as tracked above even when the pod itself
        // resolved tags.
        auto containers_in_pod = PodMonitorIntrospect::FindContainersInPod(info.cgroup_path);
        fmt::print("    Container cgroup scopes discovered ({} total, tracked or not):\n", containers_in_pod.size());
        for (const auto& [container_id, container_cgroup_path] : containers_in_pod)
        {
            fmt::print("      {} -> {}\n", container_id, container_cgroup_path.string());
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

    // Read the same way PodMonitor's own constructor does (ResolveK8sClusterEnv in
    // pod_monitor.cpp), purely for display here -- this tool doesn't have access to the private
    // k8s_cluster_ member PodMonitor resolved internally.
    const auto* k8s_cluster_env = std::getenv("K8S_CLUSTER");
    std::string k8s_cluster = k8s_cluster_env != nullptr ? std::string(k8s_cluster_env) : std::string();

    // No collector ever runs here, so no metric is ever emitted -- Memory is the simplest writer
    // that needs no real spectatord socket/UDP listener to construct successfully.
    auto config = Config(WriterConfig(WriterTypes::Memory));
    auto registry = Registry(config);
    PodMonitorIntrospect podMonitor{&registry, path_prefix};

    constexpr int kTotalIntervals = 60;
    for (int interval = 1; interval <= kTotalIntervals; ++interval)
    {
        podMonitor.RefreshTrackedPods();
        auto discovered = podMonitor.FindActivePodInfo();
        PrintSnapshot(discovered, podMonitor.TrackedPods(), k8s_cluster, interval);

        if (interval < kTotalIntervals)
        {
            std::this_thread::sleep_for(std::chrono::seconds(60));
        }
    }

    fmt::print("mock-agent-2: completed {} interval(s), exiting.\n", kTotalIntervals);
    return 0;
}
