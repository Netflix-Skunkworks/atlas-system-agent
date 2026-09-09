#include "tracked_pod_registry.h"

#include <lib/logger/src/logger.h>

#include <filesystem>
#include <optional>
#include <system_error>
#include <unistd.h>
#include <utility>

namespace atlasagent
{

namespace
{

template <typename TrackedMap, typename DiscoveredMap>
void EraseMissing(TrackedMap& tracked, const DiscoveredMap& discovered) noexcept
{
    // absl::flat_hash_map's single-iterator erase() returns void, not the next iterator like
    // std::unordered_map. Advance with post-increment and erase the now-invalidated copy.
    for (auto it = tracked.begin(); it != tracked.end();)
    {
        if (discovered.find(it->first) == discovered.end())
        {
            tracked.erase(it++);
        }
        else
        {
            ++it;
        }
    }
}

}  // namespace

TrackedPodRegistry::TrackedPodRegistry(Registry* registry) noexcept : registry_(registry)
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
    return std::filesystem::is_directory(container.cgroup_path, ec);
}

void TrackedPodRegistry::EvictUntrackedPods(const ActivePodMap& active_pods) noexcept
{
    EraseMissing(tracked_pods_, active_pods);
}

TrackedPod& TrackedPodRegistry::UpsertPod(const std::string& uid, const ActivePod& active) noexcept
{
    auto [it, inserted] = tracked_pods_.try_emplace(uid, active.name, active.pod_namespace);
    if (!inserted)
    {
        it->second.name = active.name;
        it->second.pod_namespace = active.pod_namespace;
    }
    return it->second;
}

void TrackedPodRegistry::EvictUntrackedContainers(TrackedPod& pod, const ActiveContainerMap& active) noexcept
{
    EraseMissing(pod.containers, active);
}

void TrackedPodRegistry::ReconcileContainers(TrackedPod& pod, const ActivePod& active) noexcept
{
    for (const auto& [container_id, active_container] : active.containers)
    {
        auto existing = pod.containers.find(container_id);
        if (existing != pod.containers.end() && existing->second.cgroup_path != active_container.cgroup_path)
        {
            // Recreate the CGroup so delta baselines cannot span two paths for the same runtime id.
            pod.containers.erase(existing);
        }

        auto container_tags = active.tags;
        container_tags["nf.process"] = active_container.name;

        auto [cit, container_inserted] = pod.containers.try_emplace(
            container_id, registry_, active_container.cgroup_path, active_container.name);
        if (!container_inserted)
        {
            cit->second.container_name = active_container.name;
        }
        cit->second.cgroup.SetExtraTags(std::move(container_tags));

        // Re-resolve every cycle, not only at first insertion -- cpu.max can be set to its real
        // quota slightly after the cgroup directory appears, and an in-place resize changes it
        // later.
        cit->second.cgroup.SetCpuCountOverride(ResolveCpuCountForPod(cit->second.cgroup));

        cit->second.cgroup.SetCpuRequestOverride(active_container.cpu_request);
    }
}

void TrackedPodRegistry::Suspend() noexcept
{
    tracked_pods_.clear();
    emission_enabled_ = false;
}

void TrackedPodRegistry::Reconcile(const ActivePodMap& active_pods) noexcept
{
    EvictUntrackedPods(active_pods);

    for (const auto& [uid, active] : active_pods)
    {
        auto& pod = UpsertPod(uid, active);
        EvictUntrackedContainers(pod, active.containers);
        ReconcileContainers(pod, active);
    }
    emission_enabled_ = true;
}

template <typename EmitFn>
void TrackedPodRegistry::ForEachLiveContainer(std::string_view metric_type, EmitFn&& emit) noexcept
{
    if (!emission_enabled_)
    {
        return;
    }
    for (auto& [pod_uid, pod] : tracked_pods_)
    {
        for (auto& [container_id, container] : pod.containers)
        {
            if (!ContainerIsLive(container))
            {
                continue;
            }
            atlasagent::Logger()->debug("Collecting {} stats for pod {}/{} (uid={}) container {} ({})", metric_type,
                                        pod.pod_namespace, pod.name, pod_uid, container_id, container.container_name);
            emit(container.cgroup);
        }
    }
}

void TrackedPodRegistry::EmitCpuStats(const bool fiveSecondMetricsEnabled,
                                      const bool sixtySecondMetricsEnabled) noexcept
{
    ForEachLiveContainer("CPU", [fiveSecondMetricsEnabled, sixtySecondMetricsEnabled](CGroup& cgroup) {
        cgroup.PodCpuStats(fiveSecondMetricsEnabled, sixtySecondMetricsEnabled);
    });
}

void TrackedPodRegistry::EmitIOStats() noexcept
{
    ForEachLiveContainer("IO", [](CGroup& cgroup) { cgroup.IOStats(); });
}

void TrackedPodRegistry::EmitMemoryStats() noexcept
{
    ForEachLiveContainer("memory", [](CGroup& cgroup) {
        cgroup.MemoryStatsV2();
        cgroup.MemoryStatsStdV2();
    });
}

}  // namespace atlasagent
