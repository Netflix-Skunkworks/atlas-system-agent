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

#include <lib/collectors/pod_monitor/src/pod_monitor.h>

#include <thirdparty/spectator-cpp/spectator/registry.h>

#include <fmt/format.h>

int main(int argc, char** argv)
{
    std::string path_prefix = argc > 1 ? argv[1] : "/sys/fs/cgroup";

    auto config = Config(WriterConfig(WriterTypes::Memory));
    auto registry = Registry(config);
    atlasagent::PodMonitor podMonitor{&registry, path_prefix};

    auto pods = podMonitor.FindAllActivePods2();

    fmt::print("Scanned cgroup root: {}\n", path_prefix);
    fmt::print("Found {} pod(s):\n", pods.size());
    for (const auto& [uid, info] : pods)
    {
        fmt::print("Pod {}\n", uid);
        fmt::print("  uid:           {}\n", info.uid);
        fmt::print("  cgroup_path:   {}\n", info.cgroup_path.string());
        fmt::print("  name:          {}\n", info.name);
        fmt::print("  pod_namespace: {}\n", info.pod_namespace);
        fmt::print("  annotations:   {} total\n", info.annotations.size());
        for (const auto& [key, value] : info.annotations)
        {
            fmt::print("    {}={}\n", key, value);
        }
        fmt::print("  labels:        {} total\n", info.labels.size());
        for (const auto& [key, value] : info.labels)
        {
            fmt::print("    {}={}\n", key, value);
        }
    }

    return 0;
}
