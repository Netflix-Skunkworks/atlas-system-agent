// Standalone debug tool: runs PodMonitor::FindAllActivePods2() against a real (or
// overridden) cgroup root, plus a live apiserver call for pod Name/Namespace, and
// prints what it discovers, so behavior can be checked by hand against `kubectl get
// pods` on a live node. Not part of the atlas_system_agent binary itself.
//
// Identity resolution reads kubelet's own kubeconfig at /run/kubernetes/config (must be
// present and readable), runs its exec-credential plugin (typically "aws eks get-token")
// to obtain a bearer token, and reads NETFLIX_INSTANCE_ID from the environment as the
// node name -- all of which are already present on a real node, no manual setup needed
// (unlike the old design). Without a readable kubeconfig, every pod still appears (from
// the cgroup walk) but with empty name/namespace.

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
    }

    return 0;
}
