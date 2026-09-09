#include "active_pod_builder.h"

#include "pod_tag_resolver.h"

#include <lib/logger/src/logger.h>

#include <utility>

namespace atlasagent
{

ActivePodMap BuildActivePods(const CgroupSnapshot& cgroups, const PodIdentityMap& identities,
                             const std::string& k8s_cluster) noexcept
{
    ActivePodMap active_pods;
    active_pods.reserve(cgroups.size());

    auto logger = Logger();
    const bool debug_logging = logger->should_log(spdlog::level::debug);

    for (const auto& [uid, cgroup_pod] : cgroups)
    {
        auto identity_it = identities.find(uid);
        if (identity_it == identities.end())
        {
            if (debug_logging)
            {
                logger->debug("Ignoring cgroup pod {}: no matching kubelet identity was reported", uid);
            }
            continue;
        }

        const auto& identity = identity_it->second;
        if (identity.name.empty())
        {
            if (debug_logging)
            {
                logger->debug("Ignoring cgroup pod {}: kubelet reported an empty pod name", uid);
            }
            continue;
        }
        if (identity.pod_namespace.empty())
        {
            if (debug_logging)
            {
                logger->debug("Ignoring cgroup pod {}: kubelet reported an empty namespace", uid);
            }
            continue;
        }

        auto tags = ResolvePodTags(identity.annotations, identity.labels, identity.name, k8s_cluster);
        if (!tags.has_value())
        {
            if (debug_logging)
            {
                logger->debug("Ignoring pod {}/{} (uid={}): no application identity tags resolved",
                              identity.pod_namespace, identity.name, uid);
            }
            continue;
        }
        (*tags)["k8s.namespace.name"] = identity.pod_namespace;

        ActivePod pod{identity.name, identity.pod_namespace, std::move(*tags), {}};
        pod.containers.reserve(cgroup_pod.containers.size());

        for (const auto& [container_id, cgroup_path] : cgroup_pod.containers)
        {
            auto container_it = identity.containers.find(container_id);
            if (container_it == identity.containers.end())
            {
                if (debug_logging)
                {
                    logger->debug(
                        "Ignoring cgroup scope {} in pod {}/{} (uid={}): no matching kubelet container identity was "
                        "reported (pod sandbox scopes are expected here)",
                        container_id, identity.pod_namespace, identity.name, uid);
                }
                continue;
            }

            const auto& container = container_it->second;
            if (container.name.empty())
            {
                if (debug_logging)
                {
                    logger->debug("Ignoring container {} in pod {}/{} (uid={}): kubelet reported an empty name",
                                  container_id, identity.pod_namespace, identity.name, uid);
                }
                continue;
            }

            pod.containers.emplace(container_id,
                                   ActiveContainer{cgroup_path, container.name, container.cpu_request});
        }

        active_pods.emplace(uid, std::move(pod));
    }

    return active_pods;
}

}  // namespace atlasagent
