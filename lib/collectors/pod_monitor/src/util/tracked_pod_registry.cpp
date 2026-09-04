#include "tracked_pod_registry.h"

#include "pod_tag_resolver.h"

#include <lib/logger/src/logger.h>

#include <cstdlib>
#include <unistd.h>
#include <utility>

namespace atlasagent
{

namespace
{

std::string ResolveK8sClusterEnv() noexcept
{
    const auto* value = std::getenv("K8S_CLUSTER");
    return value != nullptr ? std::string(value) : std::string();
}

}  // namespace

TrackedPodRegistry::TrackedPodRegistry(Registry* registry) noexcept : registry_(registry), k8s_cluster_(ResolveK8sClusterEnv())
{
}

double TrackedPodRegistry::ResolveCpuCountForPod(const CGroup& cgroup) noexcept
{
    if (auto quota = cgroup.QuotaCpuCount(); quota.has_value())
    {
        return *quota;
    }
    return static_cast<double>(sysconf(_SC_NPROCESSORS_ONLN));
}

void TrackedPodRegistry::EvictUntrackedPods(const PodInfoMap& discovered) noexcept
{
    // Unlike std::unordered_map, absl::flat_hash_map's single-iterator erase() returns void (not
    // the next iterator), so the iterator must be advanced with post-increment before the (now
    // invalidated) copy is erased -- see raw_hash_set.h's own erase() doc comment for this exact
    // idiom.
    for (auto it = tracked_pods_.begin(); it != tracked_pods_.end();)
    {
        if (discovered.find(it->first) == discovered.end())
        {
            tracked_pods_.erase(it++);
        }
        else
        {
            ++it;
        }
    }
}

TrackedPod& TrackedPodRegistry::UpsertPodIdentity(const std::string& uid, const PodInfo& info) noexcept
{
    auto [it, inserted] = tracked_pods_.try_emplace(uid, info.name, info.pod_namespace);
    if (!inserted && !info.name.empty() && !info.pod_namespace.empty() &&
        (it->second.name != info.name || it->second.pod_namespace != info.pod_namespace))
    {
        it->second.name = info.name;
        it->second.pod_namespace = info.pod_namespace;
    }
    return it->second;
}

void TrackedPodRegistry::EvictUntrackedContainers(TrackedPod& pod, const ContainerCgroupMap& discovered_containers) noexcept
{
    for (auto cit = pod.containers.begin(); cit != pod.containers.end();)
    {
        if (discovered_containers.find(cit->first) == discovered_containers.end())
        {
            pod.containers.erase(cit++);
        }
        else
        {
            ++cit;
        }
    }
}

void TrackedPodRegistry::ReconcileContainers(TrackedPod& pod, const PodInfo& info,
                                              const ContainerCgroupMap& discovered_containers,
                                              const std::unordered_map<std::string, std::string>& pod_tags) noexcept
{
    for (const auto& [container_id, container_cgroup_path] : discovered_containers)
    {
        auto container_name_it = info.containers.find(container_id);
        if (container_name_it == info.containers.end())
        {
            // Not ready yet -- expected transient race: a container's cgroup scope can appear
            // slightly before kubelet reports it in containerStatuses (or vice versa). Skip this
            // cycle without evicting it if it was already tracked.
            continue;
        }
        const std::string& container_name = container_name_it->second;

        auto container_tags = pod_tags;
        container_tags["nf.process"] = container_name;

        auto [cit, container_inserted] = pod.containers.try_emplace(
            container_id, registry_, container_cgroup_path.string(), container_id, container_name);
        if (!container_inserted)
        {
            cit->second.container_name = container_name;
        }
        cit->second.cgroup.SetExtraTags(std::move(container_tags));

        // Re-resolve every refresh cycle (not only at first insertion) -- a container's cgroup
        // directory can appear slightly before cpu.max is set to its real quota, and an in-place
        // resize can change it later.
        cit->second.cgroup.SetCpuCountOverride(ResolveCpuCountForPod(cit->second.cgroup));
    }
}

void TrackedPodRegistry::Refresh(const PodInfoMap& discovered) noexcept
{
    EvictUntrackedPods(discovered);

    for (const auto& [uid, info] : discovered)
    {
        TrackedPod& pod = UpsertPodIdentity(uid, info);

        // Resolve this pod's tags ONCE -- annotations/labels are pod-level, not per-container,
        // so every container in this pod shares the same nf.app/nf.stack/nf.detail/nf.cluster.
        // This is also the single Gating decision for every container below (see
        // ResolvePodTags's own doc comment for why nf.node/nf.process are excluded from it).
        auto pod_tags = ResolvePodTags(info.annotations, info.labels, pod.name, k8s_cluster_);
        auto discovered_containers = CgroupPodDiscovery::FindContainersInPod(info.cgroup_path);
        EvictUntrackedContainers(pod, discovered_containers);

        if (!pod_tags.has_value())
        {
            // Gating: this pod's identity didn't resolve -- no metrics for ANY of its
            // containers. Evict everything still tracked (a pod can lose its resolved identity
            // across a relabel/rollout, the same way a single container losing its tags could
            // under the superseded per-container design).
            pod.containers.clear();
            continue;
        }

        if (!pod.pod_namespace.empty())
        {
            (*pod_tags)["k8s.namespace.name"] = pod.pod_namespace;
        }

        ReconcileContainers(pod, info, discovered_containers, *pod_tags);
    }
}

void TrackedPodRegistry::CollectCpuStats(const bool fiveSecondMetricsEnabled, const bool sixtySecondMetricsEnabled) noexcept
{
    for (auto& pod_entry : tracked_pods_)
    {
        for (auto& container_entry : pod_entry.second.containers)
        {
            atlasagent::Logger()->debug("Collecting CPU stats for pod {}/{} (uid={}) container {} ({})",
                                        pod_entry.second.pod_namespace, pod_entry.second.name, pod_entry.first,
                                        container_entry.first, container_entry.second.container_name);
            container_entry.second.cgroup.PodCpuStats(fiveSecondMetricsEnabled, sixtySecondMetricsEnabled);
        }
    }
}

void TrackedPodRegistry::CollectIOStats() noexcept
{
    for (auto& pod_entry : tracked_pods_)
    {
        for (auto& container_entry : pod_entry.second.containers)
        {
            atlasagent::Logger()->debug("Collecting IO stats for pod {}/{} (uid={}) container {} ({})",
                                        pod_entry.second.pod_namespace, pod_entry.second.name, pod_entry.first,
                                        container_entry.first, container_entry.second.container_name);
            container_entry.second.cgroup.IOStats();
        }
    }
}

void TrackedPodRegistry::EmitMemoryStats() noexcept
{
    for (auto& pod_entry : tracked_pods_)
    {
        for (auto& container_entry : pod_entry.second.containers)
        {
            atlasagent::Logger()->debug("Collecting memory stats for pod {}/{} (uid={}) container {} ({})",
                                        pod_entry.second.pod_namespace, pod_entry.second.name, pod_entry.first,
                                        container_entry.first, container_entry.second.container_name);
            container_entry.second.cgroup.MemoryStatsV2();
            container_entry.second.cgroup.MemoryStatsStdV2();
        }
    }
}

}  // namespace atlasagent
