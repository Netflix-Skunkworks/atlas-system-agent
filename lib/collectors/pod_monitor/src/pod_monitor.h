#pragma once

#include <thirdparty/spectator-cpp/spectator/registry.h>

#include "cgroup_pod_discovery.h"
#include "pod_identity_client.h"
#include "pod_info.h"
#include "tracked_pod_registry.h"

#include <optional>
#include <string>

namespace atlasagent
{

// Facade composing three collaborators: CgroupPodDiscovery (pure cgroup-filesystem discovery),
// PodIdentityClient (kubelet-sourced identity), and TrackedPodRegistry (tracked pod/container
// reconciliation + cadence-driven metric emission). See each collaborator's own header for the
// detail of what it owns.
class PodMonitor
{
   public:
    explicit PodMonitor(Registry* registry, std::string path_prefix = "/sys/fs/cgroup",
                         std::string kubelet_url = PodIdentityClientConstants::KubeletUrl) noexcept;

    // Discovers every pod-level cgroup directory on this node, keyed by pod UID in
    // canonical (dashed) form. Detects the cgroup v2 driver (systemd vs cgroupfs) fresh
    // on each call, and walks at most two levels deep, so it only ever finds pod-aggregate
    // cgroups, never per-container leaves.
    [[nodiscard]] PodCgroupMap FindActivePodCgroups() const noexcept { return discovery_.FindActivePodCgroups(); }

    // Same as FindActivePodCgroups(), but also resolves each pod's Name, Namespace, annotations,
    // labels, and each of its containers' id -> name mapping via a live call to kubelet's local
    // API. Slower and network-dependent; FindActivePodCgroups() alone is sufficient when only
    // cgroup paths are needed.
    [[nodiscard]] PodInfoMap FindActivePodInfo() const noexcept;

    void SetPrefix(std::string new_prefix) noexcept { discovery_.SetPrefix(std::move(new_prefix)); }

    // Emits cgroup.cpu.* container-scoped metrics (CGroup::PodCpuStats) for every container of
    // every currently tracked pod. Never changes tracked pod/container membership itself -- that
    // only happens inside RefreshTrackedPods(), called from CollectMemoryStats().
    void CollectCpuStats(const bool fiveSecondMetricsEnabled, const bool sixtySecondMetricsEnabled) noexcept
    {
        tracked_registry_.CollectCpuStats(fiveSecondMetricsEnabled, sixtySecondMetricsEnabled);
    }

    // Emits cgroup I/O metrics (CGroup::IOStats) for every container of every currently tracked
    // pod.
    void CollectIOStats() noexcept { tracked_registry_.CollectIOStats(); }

    // Refreshes the tracked pod/container set (see RefreshTrackedPods()), then emits both
    // CGroup::MemoryStatsV2's cgroup.mem.* metrics and CGroup::MemoryStatsStdV2's mem.* metrics
    // (mem.cached/mem.shared/mem.availReal/mem.freeReal/mem.totalReal/mem.availSwap/
    // mem.totalSwap/mem.totalFree -- NOT cgroup.mem.* names, despite reading the same per-cgroup
    // files) for every now-current tracked container, each disambiguated by the nf.*/k8s.* tags
    // ResolvePodTags resolved for its pod (plus its own nf.process) -- so a newly-discovered
    // container is sampled the same cycle it's discovered, and an evicted container's
    // already-destroyed CGroup is never touched.
    void CollectMemoryStats() noexcept
    {
        RefreshTrackedPods();
        tracked_registry_.EmitMemoryStats();
    }

   protected:
    // For testing access. Facade-level glue joining CgroupPodDiscovery's cgroup-path output with
    // PodIdentityClient's kubelet-sourced identity output into one PodInfoMap -- kept here since
    // it isn't naturally owned by either collaborator alone.
    [[nodiscard]] static PodInfoMap JoinCgroupAndIdentity(const PodCgroupMap& cgroup_pods,
                                                           const std::optional<PodIdentityMap>& identities) noexcept;

    // Discovers the current pod set (FindActivePodInfo()) and reconciles the tracked pod/
    // container set against it -- see TrackedPodRegistry::Refresh()'s own doc comment for the
    // detail of that step.
    void RefreshTrackedPods() noexcept { tracked_registry_.Refresh(FindActivePodInfo()); }

    // Read-only view of the tracked pod/container set, for test assertions and debug tooling.
    [[nodiscard]] const PodTrackedMap& TrackedPods() const noexcept { return tracked_registry_.TrackedPods(); }

   private:
    CgroupPodDiscovery discovery_;
    PodIdentityClient identity_client_;
    TrackedPodRegistry tracked_registry_;
};

}  // namespace atlasagent
