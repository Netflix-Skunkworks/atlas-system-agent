#include "tracked_pod_registry.h"

#include <lib/logger/src/logger.h>

#include <cstddef>
#include <filesystem>
#include <optional>
#include <system_error>
#include <unistd.h>
#include <utility>

namespace atlasagent
{

namespace
{

// on_erase is invoked BEFORE the entry is destroyed, so it can still read the value being evicted.
template <typename TrackedMap, typename DiscoveredMap, typename OnEraseFn>
void EraseMissing(TrackedMap& tracked, const DiscoveredMap& discovered, OnEraseFn&& on_erase) noexcept
{
    // absl::flat_hash_map's single-iterator erase() returns void, not the next iterator like
    // std::unordered_map. Advance with post-increment and erase the now-invalidated copy.
    for (auto it = tracked.begin(); it != tracked.end();)
    {
        if (discovered.find(it->first) == discovered.end())
        {
            on_erase(it->first, it->second);
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

std::optional<double> TrackedPodRegistry::ResolveCpuCount(const CGroup& cgroup) noexcept
{
    auto quota = cgroup.QuotaCpuCount();
    switch (quota.state)
    {
        case CpuQuotaState::kLimited:
            return quota.cores;
        case CpuQuotaState::kUnlimited:
        {
            const auto online_processors = sysconf(_SC_NPROCESSORS_ONLN);
            if (online_processors > 0)
            {
                return static_cast<double>(online_processors);
            }
            return std::nullopt;
        }
        case CpuQuotaState::kUnreadable:
            return std::nullopt;
    }
    return std::nullopt;
}

bool TrackedPodRegistry::UpdateCpuMetricState(TrackedContainer& container,
                                               std::string_view container_id) noexcept
{
    auto cpu_count = ResolveCpuCount(container.cgroup);
    if (cpu_count.has_value())
    {
        if (container.cpu_metric_state == CpuMetricState::kDisabled)
        {
            atlasagent::Logger()->debug("CPU metrics re-enabled for container {}", container_id);
        }
        container.cgroup.SetCpuCountOverride(cpu_count);
        container.cpu_metric_state = CpuMetricState::kEnabled;
        return true;
    }

    if (container.cpu_metric_state == CpuMetricState::kEnabled)
    {
        container.cgroup.ResetCpuStats();
    }
    if (container.cpu_metric_state != CpuMetricState::kDisabled)
    {
        atlasagent::Logger()->debug("CPU metrics disabled for container {}: CPU capacity is unreadable", container_id);
    }
    container.cgroup.SetCpuCountOverride(std::nullopt);
    container.cpu_metric_state = CpuMetricState::kDisabled;
    return false;
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

TrackedPod& TrackedPodRegistry::UpsertPod(const std::string& uid, const ActivePod& active) noexcept
{
    auto [it, inserted] = tracked_pods_.try_emplace(uid, active.name, active.pod_namespace);
    if (inserted)
    {
        atlasagent::Logger()->info("Tracking new pod {}/{} (uid={})", active.pod_namespace, active.name, uid);
    }
    else
    {
        it->second.name = active.name;
        it->second.pod_namespace = active.pod_namespace;
    }
    return it->second;
}

void TrackedPodRegistry::ReconcileContainers(const std::string& uid, TrackedPod& pod,
                                             const ActivePod& active) noexcept
{
    for (const auto& [container_id, active_container] : active.containers)
    {
        auto existing = pod.containers.find(container_id);
        bool recreated = false;
        if (existing != pod.containers.end() && existing->second.cgroup_path != active_container.cgroup_path)
        {
            // Recreate the CGroup so delta baselines cannot span two paths for the same runtime id.
            atlasagent::Logger()->info(
                "Recreating container {} ({}) in pod {}/{} (uid={}): cgroup path changed from {} to {}", container_id,
                active_container.name, pod.pod_namespace, pod.name, uid, existing->second.cgroup_path.string(),
                active_container.cgroup_path.string());
            pod.containers.erase(existing);
            recreated = true;
        }

        auto container_tags = active.tags;
        container_tags["nf.process"] = active_container.name;

        auto [cit, container_inserted] = pod.containers.try_emplace(
            container_id, registry_, active_container.cgroup_path, active_container.name);
        if (!container_inserted)
        {
            cit->second.container_name = active_container.name;
        }
        else if (!recreated)
        {
            // Only for a genuinely new runtime id; the recreate path above already reported itself.
            atlasagent::Logger()->info("Tracking new container {} ({}) in pod {}/{} (uid={}) at {}", container_id,
                                       active_container.name, pod.pod_namespace, pod.name, uid,
                                       active_container.cgroup_path.string());
        }
        cit->second.cgroup.SetExtraTags(std::move(container_tags));

        cit->second.cgroup.SetCpuRequestOverride(active_container.cpu_request);
    }
}

void TrackedPodRegistry::Suspend() noexcept
{
    // Only when something was actually dropped: a persistent probe outage calls Suspend() on every
    // refresh, and an unconditional line would be pure noise once the map is already empty.
    if (!tracked_pods_.empty())
    {
        std::size_t containers = 0;
        for (const auto& [uid, pod] : tracked_pods_)
        {
            containers += pod.containers.size();
        }
        atlasagent::Logger()->info("Suspending emission: evicted all {} tracked pod(s) and {} container(s)",
                                   tracked_pods_.size(), containers);
    }
    tracked_pods_.clear();
}

void TrackedPodRegistry::Reconcile(const ActivePodMap& active_pods) noexcept
{
    EraseMissing(tracked_pods_, active_pods, [](const std::string& uid, const TrackedPod& pod) {
        atlasagent::Logger()->info("Evicted pod {}/{} (uid={}) and its {} tracked container(s): absent from the "
                                   "active snapshot",
                                   pod.pod_namespace, pod.name, uid, pod.containers.size());
    });

    for (const auto& [uid, active] : active_pods)
    {
        auto& pod = UpsertPod(uid, active);
        EraseMissing(pod.containers, active.containers,
                     [&uid, &pod](const std::string& container_id, const TrackedContainer& container) {
                         atlasagent::Logger()->info(
                             "Evicted container {} ({}) from pod {}/{} (uid={}): absent from the active snapshot",
                             container_id, container.container_name, pod.pod_namespace, pod.name, uid);
                     });
        ReconcileContainers(uid, pod, active);
    }
}

template <typename EmitFn>
void TrackedPodRegistry::ForEachLiveContainer(std::string_view metric_type, const bool require_cpu_metrics,
                                              EmitFn&& emit) noexcept
{
    for (auto& [pod_uid, pod] : tracked_pods_)
    {
        for (auto& [container_id, container] : pod.containers)
        {
            if (!ContainerIsLive(container))
            {
                continue;
            }
            if (require_cpu_metrics && !UpdateCpuMetricState(container, container_id))
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
    ForEachLiveContainer(
        "CPU", /*require_cpu_metrics=*/true,
        [fiveSecondMetricsEnabled, sixtySecondMetricsEnabled](CGroup& cgroup) {
            cgroup.PodCpuStats(fiveSecondMetricsEnabled, sixtySecondMetricsEnabled);
        });
}

void TrackedPodRegistry::EmitIOStats() noexcept
{
    ForEachLiveContainer("IO", /*require_cpu_metrics=*/false, [](CGroup& cgroup) { cgroup.IOStats(); });
}

void TrackedPodRegistry::EmitMemoryStats() noexcept
{
    ForEachLiveContainer("memory", /*require_cpu_metrics=*/false, [](CGroup& cgroup) {
        cgroup.MemoryStatsV2();
        cgroup.MemoryStatsStdV2();
    });
}

}  // namespace atlasagent
