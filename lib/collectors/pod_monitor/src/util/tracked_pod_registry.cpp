#include "tracked_pod_registry.h"

#include "pod_tag_resolver.h"

#include <lib/logger/src/logger.h>

#include <cstdlib>
#include <filesystem>
#include <optional>
#include <system_error>
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

bool TrackedPodRegistry::ContainerIsLive(const TrackedContainer& container) noexcept
{
    // The std::error_code overload is mandatory, not a style choice: the throwing overload's
    // std::filesystem::filesystem_error, out of the noexcept Emit* callers, would terminate the
    // agent. Reporting false on error is also the answer we want -- a scope we cannot stat is not
    // one to emit for.
    std::error_code ec;
    return std::filesystem::exists(container.cgroup_path, ec);
}

void TrackedPodRegistry::EvictUntrackedPods(const PodInfoMap& discovered) noexcept
{
    // absl::flat_hash_map's single-iterator erase() returns void, not the next iterator like
    // std::unordered_map -- so advance by post-increment and erase the now-invalidated copy. This
    // exact idiom is spelled out in raw_hash_set.h's own erase() doc comment.
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
    // try_emplace leaves an already-tracked uid untouched: info.name/info.pod_namespace are used
    // as constructor args only for a *new* TrackedPod, hence the self-heal below.
    auto [it, inserted] = tracked_pods_.try_emplace(uid, info.name, info.pod_namespace);
    // Update the stored pair only when both incoming name and namespace are non-blank. Partial or
    // blank input leaves the previous pair unchanged; container Gating is handled separately below.
    if (!inserted && !info.name.empty() && !info.pod_namespace.empty())
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
            // Common case: a pod sandbox (pause) scope. Containerd gives the sandbox the same
            // cri-containerd-<id>.scope shape, but that id appears in neither parsed status array,
            // so it cannot resolve to a container name and is intentionally skipped. Also landing
            // here: a cgroup scope that appears before its kubelet status entry, and an ephemeral
            // (kubectl debug) container, whose status array PodIdentityClient does not parse.
            //
            // Do not insert or update this id. Eviction above is based only on cgroup discovery, so
            // an existing tracked entry with the same id remains eligible for later Emit* calls.
            // DEBUG rather than warn because a sandbox is indistinguishable from another unmatched
            // scope by path alone, and kubelet's /pods never reports a sandbox id.
            atlasagent::Logger()->debug("Pod {} container {} has a cgroup scope but no kubelet-reported name; skipping",
                                        info.uid, container_id);
            continue;
        }
        const std::string& container_name = container_name_it->second;

        auto container_tags = pod_tags;
        container_tags["nf.process"] = container_name;

        auto [cit, container_inserted] = pod.containers.try_emplace(
            container_id, registry_, container_cgroup_path, container_id, container_name);
        if (!container_inserted)
        {
            cit->second.container_name = container_name;
        }
        cit->second.cgroup.SetExtraTags(std::move(container_tags));

        // Re-resolve every cycle, not only at first insertion -- cpu.max can be set to its real
        // quota slightly after the cgroup directory appears, and an in-place resize changes it
        // later.
        cit->second.cgroup.SetCpuCountOverride(ResolveCpuCountForPod(cit->second.cgroup));

        // The parsed declared CPU request, keyed by container name because that is how the pod spec
        // identifies containers. Absent means no usable request was parsed (for example, a
        // BestEffort or limits-only container); CGroup then omits k8s.cpu.requested rather than
        // reporting the limit as if it were the request. Re-resolve every cycle so an in-place
        // resize is picked up.
        auto request_it = info.cpu_requests.find(container_name);
        cit->second.cgroup.SetCpuRequestOverride(
            request_it != info.cpu_requests.end() ? std::optional<double>{request_it->second} : std::nullopt);
    }
}

void TrackedPodRegistry::Refresh(const PodInfoMap& discovered) noexcept
{
    EvictUntrackedPods(discovered);

    for (const auto& [uid, info] : discovered)
    {
        TrackedPod& pod = UpsertPodIdentity(uid, info);

        // Resolve ONCE -- annotations/labels are pod-level, so every container in this pod shares
        // the same emitted nf.app/nf.stack/nf.cluster tags. nf.detail can affect Gating and cluster
        // construction but is not currently emitted. This is also the single Gating decision for
        // all containers (see ResolvePodTags for why nf.node/nf.process are excluded from it).
        auto pod_tags = ResolvePodTags(info.annotations, info.labels, pod.name, k8s_cluster_);
        auto discovered_containers = CgroupPodDiscovery::FindContainersInPod(info.cgroup_path);
        EvictUntrackedContainers(pod, discovered_containers);

        if (!pod_tags.has_value())
        {
            // Gating: none of this pod's app-identity annotations or label fallbacks resolved, so
            // emit no metrics for any of its containers. This also covers a failed kubelet lookup,
            // because JoinCgroupAndIdentity leaves all identity-derived maps empty in that case.
            // Clear everything still tracked to avoid publishing under previously resolved tags.
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

void TrackedPodRegistry::EmitCpuStats(const bool fiveSecondMetricsEnabled, const bool sixtySecondMetricsEnabled) noexcept
{
    for (auto& pod_entry : tracked_pods_)
    {
        for (auto& container_entry : pod_entry.second.containers)
        {
            if (!ContainerIsLive(container_entry.second))
            {
                continue;
            }
            atlasagent::Logger()->debug("Collecting CPU stats for pod {}/{} (uid={}) container {} ({})",
                                        pod_entry.second.pod_namespace, pod_entry.second.name, pod_entry.first,
                                        container_entry.first, container_entry.second.container_name);
            container_entry.second.cgroup.PodCpuStats(fiveSecondMetricsEnabled, sixtySecondMetricsEnabled);
        }
    }
}

void TrackedPodRegistry::EmitIOStats() noexcept
{
    for (auto& pod_entry : tracked_pods_)
    {
        for (auto& container_entry : pod_entry.second.containers)
        {
            if (!ContainerIsLive(container_entry.second))
            {
                continue;
            }
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
            if (!ContainerIsLive(container_entry.second))
            {
                continue;
            }
            atlasagent::Logger()->debug("Collecting memory stats for pod {}/{} (uid={}) container {} ({})",
                                        pod_entry.second.pod_namespace, pod_entry.second.name, pod_entry.first,
                                        container_entry.first, container_entry.second.container_name);
            container_entry.second.cgroup.MemoryStatsV2();
            container_entry.second.cgroup.MemoryStatsStdV2();
        }
    }
}

}  // namespace atlasagent
