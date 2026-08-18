// Standalone debug tool: runs PodMonitor::FindAllActivePods() against a real (or
// overridden) cgroup root and prints what it discovers, so behavior can be checked
// by hand against `kubectl get pods` on a live node. Not part of the
// atlas_system_agent binary itself.

#include <lib/collectors/pod_monitor/src/pod_monitor.h>

#include <thirdparty/spectator-cpp/spectator/registry.h>

#include <fmt/format.h>

int main(int argc, char** argv)
{
    std::string path_prefix = argc > 1 ? argv[1] : "/sys/fs/cgroup";

    auto config = Config(WriterConfig(WriterTypes::Memory));
    auto registry = Registry(config);
    atlasagent::PodMonitor podMonitor{&registry, path_prefix};

    auto pods = podMonitor.FindAllActivePods();

    fmt::print("Scanned cgroup root: {}\n", path_prefix);
    fmt::print("Found {} pod(s):\n", pods.size());
    for (const auto& [uid, path] : pods)
    {
        fmt::print("  {}\t{}\n", uid, path.string());
    }

    return 0;
}
