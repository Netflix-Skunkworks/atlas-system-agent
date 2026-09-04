#pragma once

#include <thirdparty/spectator-cpp/spectator/registry.h>

#include <lib/collectors/pod_monitor/src/util/cgroup_pod_discovery.h>
#include <lib/collectors/pod_monitor/src/util/pod_identity_client.h>
#include <lib/collectors/pod_monitor/src/util/pod_info.h>
#include <lib/collectors/pod_monitor/src/util/tracked_pod_registry.h>

#include <optional>
#include <string>

namespace atlasagent
{

// Facade composing three collaborators: CgroupPodDiscovery (cgroup-filesystem discovery),
// PodIdentityClient (kubelet-sourced identity), and TrackedPodRegistry (tracked pod/container
// reconciliation plus metric emission). See each collaborator's own header for detail.
class PodMonitor
{
   public:
    explicit PodMonitor(Registry* registry, std::string path_prefix = "/sys/fs/cgroup",
                         std::string kubelet_url = PodIdentityClientConstants::KubeletUrl) noexcept;

    // Forwards to CgroupPodDiscovery::FindActivePodCgroups(); see there for detail.
    [[nodiscard]] PodCgroupMap FindActivePodCgroups() const noexcept { return discovery_.FindActivePodCgroups(); }

    // Like FindActivePodCgroups(), but also resolves each pod's name, namespace, annotations,
    // labels, and container id -> name map via a live kubelet API call. Slower and
    // network-dependent; prefer FindActivePodCgroups() when only cgroup paths are needed.
    [[nodiscard]] PodInfoMap FindActivePodInfo() const noexcept;

    void SetPrefix(std::string new_prefix) noexcept { discovery_.SetPrefix(std::move(new_prefix)); }

    // Forwards to TrackedPodRegistry::CollectCpuStats(); see there for detail.
    void CollectCpuStats(const bool fiveSecondMetricsEnabled, const bool sixtySecondMetricsEnabled) noexcept
    {
        tracked_registry_.CollectCpuStats(fiveSecondMetricsEnabled, sixtySecondMetricsEnabled);
    }

    // Forwards to TrackedPodRegistry::CollectIOStats(); see there for detail.
    void CollectIOStats() noexcept { tracked_registry_.CollectIOStats(); }

    // Refreshes the tracked pod/container set (RefreshTrackedPods()) before emitting memory
    // metrics (TrackedPodRegistry::EmitMemoryStats()), so a newly-discovered container is
    // sampled the same cycle it appears and an evicted container's already-destroyed CGroup is
    // never touched. See EmitMemoryStats() for the metrics themselves.
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

    // Discovers the current pod set and reconciles the tracked pod/container set against it; see
    // TrackedPodRegistry::Refresh() for detail.
    void RefreshTrackedPods() noexcept { tracked_registry_.Refresh(FindActivePodInfo()); }

    // Read-only view of the tracked pod/container set, for test assertions and debug tooling.
    [[nodiscard]] const PodTrackedMap& TrackedPods() const noexcept { return tracked_registry_.TrackedPods(); }

   private:
    CgroupPodDiscovery discovery_;
    PodIdentityClient identity_client_;
    TrackedPodRegistry tracked_registry_;
};

}  // namespace atlasagent
