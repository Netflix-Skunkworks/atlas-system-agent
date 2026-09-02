// Standalone debug tool: runs PodMonitor::FindActivePodInfo() against a real (or
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
// Usage: find-activepods [cgroup_path_prefix] [filtered]  (either order; both optional)
// Pass "filtered" as one of the arguments to see the same PASS/FAIL decision
// RefreshTrackedPods() makes for every pod and container, always with a reason:
//   - Pod-level Gating (PodMonitor::ResolvePodTags): if none of nf.app/nf.stack/nf.detail
//     resolve (from either the primary netflix.com/{app,stack,detail} annotations or their
//     label fallbacks), the whole pod is GATED OUT -- printed with exactly which
//     annotation/label keys were checked and found missing, and every one of its
//     containers listed as excluded for that reason.
//   - Container-level mismatch (PodMonitor::ReconcileContainers's real matching): even in a
//     PASSED pod, a container only gets tracked if its cgroup-discovered id
//     (FindContainersInPod) is also present in kubelet's reported container list, and vice
//     versa -- a container visible on only one side is excluded independently of Gating,
//     printed with that reason.

#include <lib/collectors/pod_monitor/src/pod_monitor.h>

#include <thirdparty/spectator-cpp/spectator/registry.h>

#include <fmt/format.h>

#include <algorithm>
#include <cstdlib>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace
{

// PodMonitor keeps ResolvePodTags()/FindContainersInPod()/PodTagKeys protected -- this repo's
// own convention for exposing them to a standalone caller outside the class (see
// pod_monitor_test.cpp's PodMonitorTest, mock-agent-2.cpp's PodMonitorIntrospect) is a thin
// subclass with `using` declarations, not new public API on PodMonitor itself just for a debug
// tool.
class PodMonitorIntrospect : public atlasagent::PodMonitor
{
   public:
    using PodMonitor::PodMonitor;
    using PodMonitor::FindContainersInPod;
    using PodMonitor::PodTagKeys;
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

// Mirrors ResolvePodTags's own per-tag fallback check (primary annotation, else the first
// non-empty fallback label, in the exact order ResolvePodTags checks them) against the same
// PodTagKeys constants it uses, so this can never name a key ResolvePodTags doesn't actually
// check. Returns a one-line explanation of which key resolved this tag, or every key that was
// checked and found missing.
std::string DescribeTagResolution(const std::unordered_map<std::string, std::string>& annotations,
                                   const std::unordered_map<std::string, std::string>& labels, const char* tag_name,
                                   std::string_view annotation_key,
                                   std::initializer_list<std::string_view> fallback_label_keys)
{
    auto has_value = [](const std::unordered_map<std::string, std::string>& values, std::string_view key) {
        auto it = values.find(std::string(key));
        return it != values.end() && !it->second.empty();
    };

    if (has_value(annotations, annotation_key))
    {
        return fmt::format("{}: resolved from annotation {}", tag_name, annotation_key);
    }
    for (auto key : fallback_label_keys)
    {
        if (has_value(labels, key))
        {
            return fmt::format("{}: resolved from label {} (fallback)", tag_name, key);
        }
    }
    std::string checked = fmt::format("annotation {}", annotation_key);
    for (auto key : fallback_label_keys)
    {
        checked += fmt::format(", label {}", key);
    }
    return fmt::format("{}: UNRESOLVED (checked {})", tag_name, checked);
}

// The three outcomes PodMonitor::ReconcileContainers's real loop produces when it iterates
// cgroup-discovered containers and looks each one up by id in kubelet's reported container
// list: matched (both sides agree -- this is what actually gets tracked), cgroup_only (a cgroup
// scope exists but kubelet hasn't reported that id -- ReconcileContainers skips it this cycle),
// and kubelet_only (kubelet reports an id with no matching cgroup scope -- ReconcileContainers
// never even visits it, since its loop is driven by the cgroup-discovered set). Sorted for
// deterministic output.
struct ContainerClassification
{
    std::vector<std::string> matched;
    std::vector<std::string> cgroup_only;
    std::vector<std::string> kubelet_only;
};

ContainerClassification ClassifyContainers(const atlasagent::ContainerCgroupMap& discovered_containers,
                                            const std::unordered_map<std::string, std::string>& kubelet_containers)
{
    ContainerClassification result;
    for (const auto& entry : discovered_containers)
    {
        const std::string& container_id = entry.first;
        if (kubelet_containers.find(container_id) != kubelet_containers.end())
        {
            result.matched.push_back(container_id);
        }
        else
        {
            result.cgroup_only.push_back(container_id);
        }
    }
    for (const auto& entry : kubelet_containers)
    {
        const std::string& container_id = entry.first;
        if (discovered_containers.find(container_id) == discovered_containers.end())
        {
            result.kubelet_only.push_back(container_id);
        }
    }
    std::sort(result.matched.begin(), result.matched.end());
    std::sort(result.cgroup_only.begin(), result.cgroup_only.end());
    std::sort(result.kubelet_only.begin(), result.kubelet_only.end());
    return result;
}

// Used only for a GATED OUT pod, where every container is excluded for the same pod-level
// reason regardless of which ContainerClassification bucket it's in.
void PrintAllExcluded(const ContainerClassification& classification,
                       const std::unordered_map<std::string, std::string>& kubelet_containers, const char* reason)
{
    std::vector<std::string> ids;
    ids.insert(ids.end(), classification.matched.begin(), classification.matched.end());
    ids.insert(ids.end(), classification.cgroup_only.begin(), classification.cgroup_only.end());
    ids.insert(ids.end(), classification.kubelet_only.begin(), classification.kubelet_only.end());
    std::sort(ids.begin(), ids.end());
    for (const auto& id : ids)
    {
        auto name_it = kubelet_containers.find(id);
        std::string name = name_it != kubelet_containers.end() ? name_it->second : "(name unknown)";
        fmt::print("    {} -> {} (excluded: {})\n", id, name, reason);
    }
}

// Used for a PASSED pod: matched containers are genuinely tracked; cgroup_only/kubelet_only are
// excluded independently of Gating, for the container-level mismatch reason.
void PrintClassifiedContainers(const ContainerClassification& classification,
                                const std::unordered_map<std::string, std::string>& kubelet_containers)
{
    for (const auto& id : classification.matched)
    {
        fmt::print("    {} -> {} (tracked)\n", id, kubelet_containers.at(id));
    }
    for (const auto& id : classification.cgroup_only)
    {
        fmt::print(
            "    {} -> (name unknown -- cgroup scope found, kubelet hasn't reported this container id yet) "
            "(excluded: container-level mismatch)\n",
            id);
    }
    for (const auto& id : classification.kubelet_only)
    {
        fmt::print(
            "    {} -> {} (kubelet reports this container, but no matching cgroup scope was found under this "
            "pod) (excluded: container-level mismatch)\n",
            id, kubelet_containers.at(id));
    }
}

}  // namespace

int main(int argc, char** argv)
{
    // Scanned rather than fixed-positional, so "filtered" is recognized wherever it appears
    // (including as the only argument, defaulting path_prefix) instead of being misread as a
    // literal cgroup path override.
    std::string path_prefix = "/sys/fs/cgroup";
    bool filtered = false;
    for (int i = 1; i < argc; ++i)
    {
        if (std::string(argv[i]) == "filtered")
        {
            filtered = true;
        }
        else
        {
            path_prefix = argv[i];
        }
    }

    // Read the same way PodMonitor's own constructor does (ResolveK8sClusterEnv in
    // pod_monitor.cpp), purely for the filtered-mode ResolvePodTags call below -- this tool
    // doesn't have access to the private k8s_cluster_ member PodMonitor resolved internally.
    const auto* k8s_cluster_env = std::getenv("K8S_CLUSTER");
    std::string k8s_cluster = k8s_cluster_env != nullptr ? std::string(k8s_cluster_env) : std::string();

    auto config = Config(WriterConfig(WriterTypes::Memory));
    auto registry = Registry(config);
    PodMonitorIntrospect podMonitor{&registry, path_prefix};

    auto pods = podMonitor.FindActivePodInfo();

    fmt::print("Scanned cgroup root: {}\n", path_prefix);
    if (filtered)
    {
        fmt::print(
            "Filtering: showing the PASS/FAIL decision RefreshTrackedPods() makes for every pod and\n"
            "container, always with a reason. {} pod(s) discovered in total.\n",
            pods.size());
    }
    else
    {
        fmt::print("Found {} pod(s):\n", pods.size());
    }

    int passed = 0;
    int gated_out = 0;
    for (const auto& [uid, info] : pods)
    {
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
            auto pod_tags = PodMonitorIntrospect::ResolvePodTags(info.annotations, info.labels, info.name, k8s_cluster);
            auto discovered_containers = PodMonitorIntrospect::FindContainersInPod(info.cgroup_path);
            auto classification = ClassifyContainers(discovered_containers, info.containers);

            if (pod_tags.has_value())
            {
                ++passed;
                fmt::print("  PASSED -- resolved tags (every tracked container below carries these, plus its own nf.process):\n");
                PrintSortedMap(*pod_tags, "    ");
                fmt::print("  containers:\n");
                PrintClassifiedContainers(classification, info.containers);
            }
            else
            {
                ++gated_out;
                fmt::print("  GATED OUT -- none of nf.app/nf.stack/nf.detail resolved:\n");
                fmt::print("    {}\n", DescribeTagResolution(info.annotations, info.labels, "nf.app", PodMonitorIntrospect::PodTagKeys::kAnnotationApp,
                                                               {PodMonitorIntrospect::PodTagKeys::kLabelAppName,
                                                                PodMonitorIntrospect::PodTagKeys::kLabelK8sApp,
                                                                PodMonitorIntrospect::PodTagKeys::kLabelApp}));
                fmt::print("    {}\n", DescribeTagResolution(info.annotations, info.labels, "nf.stack",
                                                               PodMonitorIntrospect::PodTagKeys::kAnnotationStack,
                                                               {PodMonitorIntrospect::PodTagKeys::kLabelAppInstance}));
                fmt::print("    {}\n", DescribeTagResolution(info.annotations, info.labels, "nf.detail",
                                                               PodMonitorIntrospect::PodTagKeys::kAnnotationDetail,
                                                               {PodMonitorIntrospect::PodTagKeys::kLabelAppComponent}));
                fmt::print("  containers (all excluded -- pod gated out):\n");
                PrintAllExcluded(classification, info.containers, "pod gated out");
            }
        }
        else
        {
            fmt::print("  containers (kubelet-known, {} total):\n", info.containers.size());
            for (const auto& [container_id, container_name] : info.containers)
            {
                fmt::print("    {} -> {}\n", container_id, container_name);
            }
        }
    }

    if (filtered)
    {
        fmt::print("Passed {}, gated out {}, of {} discovered pod(s).\n", passed, gated_out, pods.size());
    }

    return 0;
}
