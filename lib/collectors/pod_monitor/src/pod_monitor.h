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

    // Joins FindActivePodCgroups() with identities returned by a live kubelet API call. If the call
    // fails or a discovered UID is absent from the response, that pod remains in the result with
    // empty name/namespace/containers/annotations/labels/cpu_requests fields. Slower and
    // network-dependent; prefer FindActivePodCgroups() when only cgroup paths are needed.
    [[nodiscard]] PodInfoMap FindActivePodInfo() const noexcept;

    void SetPrefix(std::string new_prefix) noexcept { discovery_.SetPrefix(std::move(new_prefix)); }

    // Forwards to TrackedPodRegistry::EmitCpuStats(); see there for detail.
    void CollectCpuStats(const bool fiveSecondMetricsEnabled, const bool sixtySecondMetricsEnabled) noexcept
    {
        tracked_registry_.EmitCpuStats(fiveSecondMetricsEnabled, sixtySecondMetricsEnabled);
    }

    // Forwards to TrackedPodRegistry::EmitIOStats(); see there for detail.
    void CollectIOStats() noexcept { tracked_registry_.EmitIOStats(); }

    // Refreshes the tracked pod/container set, then emits memory metrics. A newly seen container is
    // tracked only when cgroup discovery, kubelet identity matching, and pod-level tag Gating all
    // succeed; it is then eligible for the immediately following memory pass. Entries removed by
    // Refresh() are gone before emission, and EmitMemoryStats() performs another liveness check.
    void CollectMemoryStats() noexcept
    {
        RefreshTrackedPods();
        tracked_registry_.EmitMemoryStats();
    }

   protected:
    // For testing access. Joins CgroupPodDiscovery's cgroup paths with PodIdentityClient's
    // kubelet-sourced identities into one PodInfoMap -- lives here since neither collaborator
    // alone naturally owns it.
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
