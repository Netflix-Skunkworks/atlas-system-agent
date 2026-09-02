// Standalone debug tool: runs PodMonitor::FindAllActivePods2() against a real (or
// overridden) cgroup root, plus a live call to kubelet's own local API for pod
// Name/Namespace/annotations/labels, and prints what it discovers, so behavior can be
// checked by hand against `kubectl get pods` on a live node. Not part of the
// atlas_system_agent binary itself.
//
// Identity resolution issues one plain HTTP GET to kubelet's own local, unauthenticated
// /pods endpoint (http://localhost:10255/pods by default) -- no kubeconfig, no bearer
// token, no node-name lookup needed, since kubelet only ever knows about pods on its own
// node. If that port isn't reachable (e.g. disabled by cluster hardening), every pod
// still appears (from the cgroup walk) but with empty name/namespace/annotations/labels.
//
// Usage: find-active-pods2 [cgroup_path_prefix] [filtered]
// Pass "filtered" as the second argument to only print pods whose tags actually resolve
// via PodMonitor::ResolvePodTags -- i.e. pods that would really get metrics emitted for
// them, the same Gating decision RefreshTrackedPods() makes. Gating is pod-level: every
// container in a gated-in pod shares that pod's resolved tags (plus its own nf.process),
// so there's no separate per-container filter once the pod itself passes.

#include <lib/collectors/pod_monitor/src/pod_monitor.h>

#include <thirdparty/spectator-cpp/spectator/registry.h>

#include <fmt/format.h>

#include <algorithm>
#include <cstdlib>
#include <optional>
#include <unordered_map>
#include <utility>
#include <vector>

namespace
{

// PodMonitor keeps ResolvePodTags() protected -- this repo's own convention for exposing it to
// a standalone caller outside the class (see pod_monitor_test.cpp's PodMonitorTest,
// mock-agent-2.cpp's PodMonitorIntrospect) is a thin subclass with a `using` declaration, not
// new public API on PodMonitor itself just for a debug tool.
class PodMonitorIntrospect : public atlasagent::PodMonitor
{
   public:
    using PodMonitor::PodMonitor;
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

}  // namespace

int main(int argc, char** argv)
{
    std::string path_prefix = argc > 1 ? argv[1] : "/sys/fs/cgroup";
    bool filtered = argc > 2 && std::string(argv[2]) == "filtered";

    // Read the same way PodMonitor's own constructor does (ResolveK8sClusterEnv in
    // pod_monitor.cpp), purely for the filtered-mode ResolvePodTags call below -- this tool
    // doesn't have access to the private k8s_cluster_ member PodMonitor resolved internally.
    const auto* k8s_cluster_env = std::getenv("K8S_CLUSTER");
    std::string k8s_cluster = k8s_cluster_env != nullptr ? std::string(k8s_cluster_env) : std::string();

    auto config = Config(WriterConfig(WriterTypes::Memory));
    auto registry = Registry(config);
    PodMonitorIntrospect podMonitor{&registry, path_prefix};

    auto pods = podMonitor.FindAllActivePods2();

    fmt::print("Scanned cgroup root: {}\n", path_prefix);
    if (filtered)
    {
        fmt::print(
            "Filtering: only printing pods whose tags resolve via ResolvePodTags (i.e. pods that\n"
            "would actually get metrics emitted -- the same Gating decision RefreshTrackedPods()\n"
            "makes). {} pod(s) discovered in total; shown below are only the ones that pass.\n",
            pods.size());
    }
    else
    {
        fmt::print("Found {} pod(s):\n", pods.size());
    }

    int shown = 0;
    for (const auto& [uid, info] : pods)
    {
        std::optional<std::unordered_map<std::string, std::string>> pod_tags;
        if (filtered)
        {
            pod_tags = PodMonitorIntrospect::ResolvePodTags(info.annotations, info.labels, info.name, k8s_cluster);
            if (!pod_tags.has_value())
            {
                continue;  // Gated out -- no metrics for this pod or any of its containers.
            }
        }
        ++shown;

        fmt::print("Pod {}\n", uid);
        fmt::print("  uid:           {}\n", info.uid);
        fmt::print("  cgroup_path:   {}\n", info.cgroup_path.string());
        fmt::print("  name:          {}\n", info.name);
        fmt::print("  pod_namespace: {}\n", info.pod_namespace);
        fmt::print("  annotations:   {} total\n", info.annotations.size());
        PrintSortedMap(info.annotations, "    ");
        fmt::print("  labels:        {} total\n", info.labels.size());
        PrintSortedMap(info.labels, "    ");

        if (filtered)
        {
            fmt::print("  resolved tags (every container below would carry these, plus its own nf.process):\n");
            PrintSortedMap(*pod_tags, "    ");
        }

        fmt::print("  containers (kubelet-known, {} total):\n", info.containers.size());
        for (const auto& [container_id, container_name] : info.containers)
        {
            fmt::print("    {} -> {}\n", container_id, container_name);
        }
    }

    if (filtered)
    {
        fmt::print("Shown {} of {} discovered pod(s) after filtering.\n", shown, pods.size());
    }

    return 0;
}
