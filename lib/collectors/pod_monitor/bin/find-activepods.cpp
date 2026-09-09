// Standalone debug tool, not part of atlas_system_agent. It runs the same cgroup discovery,
// kubelet identity fetch, and reconciliation planner as PodMonitor, then prints admitted pods and
// containers. Excluded entries are available through PodMonitor's debug logging.
//
// Usage: find-activepods [cgroup_path_prefix] [filtered]  (either order; both optional)
// "filtered" enables debug logging so excluded entries are visible.

#include <lib/collectors/pod_monitor/src/pod_monitor.h>
#include <lib/logger/src/logger.h>

#include <thirdparty/spectator-cpp/spectator/registry.h>

#include <fmt/format.h>

#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace
{

void PrintSortedMap(const std::unordered_map<std::string, std::string>& values, const char* indent)
{
    std::vector<std::pair<std::string, std::string>> entries(values.begin(), values.end());
    std::sort(entries.begin(), entries.end());
    for (const auto& [key, value] : entries)
    {
        fmt::print("{}{}={}\n", indent, key, value);
    }
}

void PrintContainers(const atlasagent::ActiveContainerMap& containers)
{
    std::vector<std::string> ids;
    ids.reserve(containers.size());
    for (const auto& entry : containers)
    {
        ids.push_back(entry.first);
    }
    std::sort(ids.begin(), ids.end());

    for (const auto& id : ids)
    {
        const auto& container = containers.at(id);
        fmt::print("    {} -> {} [{}]\n", id, container.name, container.cgroup_path.string());
    }
}

std::vector<std::string> SortedPodUids(const atlasagent::ActivePodMap& pods)
{
    std::vector<std::string> uids;
    uids.reserve(pods.size());
    for (const auto& entry : pods)
    {
        uids.push_back(entry.first);
    }
    std::sort(uids.begin(), uids.end());
    return uids;
}

std::size_t CountActiveContainers(const atlasagent::ActivePodMap& pods)
{
    std::size_t count = 0;
    for (const auto& entry : pods)
    {
        count += entry.second.containers.size();
    }
    return count;
}

}  // namespace

int main(int argc, char** argv)
{
    std::string path_prefix = "/sys/fs/cgroup";
    bool show_excluded = false;
    for (int i = 1; i < argc; ++i)
    {
        if (std::string(argv[i]) == "filtered")
        {
            show_excluded = true;
        }
        else
        {
            path_prefix = argv[i];
        }
    }

    auto config = Config(WriterConfig(WriterTypes::Memory));
    auto registry = Registry(config);
    atlasagent::PodMonitor pod_monitor{&registry, path_prefix};

    if (show_excluded)
    {
        atlasagent::Logger()->set_level(spdlog::level::debug);
    }

    const auto refresh = pod_monitor.Refresh();
    fmt::print("Scanned cgroup root: {}\n", path_prefix);
    if (refresh.state == atlasagent::PodMonitorState::kCgroupUnavailable)
    {
        const auto& error = *refresh.cgroup_error;
        fmt::print(stderr, "Cgroup discovery failed: kind=");
        fmt::print(stderr, "{}", atlasagent::ToString(error.kind));
        if (!error.path.empty())
        {
            fmt::print(stderr, " path={}", error.path.string());
        }
        if (error.cause)
        {
            fmt::print(stderr, " cause={}", error.cause.message());
        }
        fmt::print(stderr, "\n");
        return 1;
    }
    if (refresh.state == atlasagent::PodMonitorState::kIdentityUnavailable)
    {
        const auto& error = *refresh.identity_error;
        fmt::print(stderr, "Kubelet identity fetch failed: kind={} status={} response_bytes={} parse_offset={}\n",
                   atlasagent::ToString(error.kind), error.http_status, error.response_size, error.parse_offset);
        return 1;
    }

    const auto& active_pods = refresh.active_pods;
    fmt::print("Admitted pods: {}, admitted containers: {}\n", active_pods.size(), CountActiveContainers(active_pods));

    for (const auto& uid : SortedPodUids(active_pods))
    {
        const auto& pod = active_pods.at(uid);
        fmt::print("Pod {}\n", uid);
        fmt::print("  name:          {}\n", pod.name);
        fmt::print("  pod_namespace: {}\n", pod.pod_namespace);

        fmt::print("  resolved tags: {}\n", pod.tags.size());
        PrintSortedMap(pod.tags, "    ");

        fmt::print("  containers:\n");
        PrintContainers(pod.containers);
    }

    return 0;
}
