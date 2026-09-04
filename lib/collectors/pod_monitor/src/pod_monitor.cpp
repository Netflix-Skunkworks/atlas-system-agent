#include "pod_monitor.h"

#include <utility>

namespace atlasagent
{

PodMonitor::PodMonitor(Registry* registry, std::string path_prefix, std::string kubelet_url) noexcept
    : discovery_(std::move(path_prefix)),
      identity_client_(registry, std::move(kubelet_url)),
      tracked_registry_(registry)
{
}

PodInfoMap PodMonitor::JoinCgroupAndIdentity(const PodCgroupMap& cgroup_pods,
                                             const std::optional<PodIdentityMap>& identities) noexcept
{
    PodInfoMap result;
    result.reserve(cgroup_pods.size());
    for (const auto& [uid, cgroup_path] : cgroup_pods)
    {
        PodInfo info{uid, cgroup_path, "", "", {}, {}, {}};
        if (identities.has_value())
        {
            auto it = identities->find(uid);
            if (it != identities->end())
            {
                info.name = it->second.name;
                info.pod_namespace = it->second.pod_namespace;
                info.containers = it->second.containers;
                info.annotations = it->second.annotations;
                info.labels = it->second.labels;
            }
        }
        result.emplace(uid, std::move(info));
    }
    return result;
}

PodInfoMap PodMonitor::FindActivePodInfo() const noexcept
{
    auto cgroup_pods = discovery_.FindActivePodCgroups();
    auto identities = identity_client_.FetchPodIdentities();
    return JoinCgroupAndIdentity(cgroup_pods, identities);
}

}  // namespace atlasagent
