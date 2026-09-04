#pragma once

#include <thirdparty/spectator-cpp/spectator/registry.h>
#include <absl/container/flat_hash_map.h>

#include <lib/collectors/cgroup/src/cgroup.h>

#include "cgroup_pod_discovery.h"
#include "pod_info.h"

#include <string>
#include <unordered_map>
#include <utility>

namespace atlasagent
{

// One tracked container: owns a private CGroup so its CPU/IO/memory delta-tracking baselines
// never collide with another container's or with Titus's node-level CGroup. Erasing the map
// entry destroys the CGroup -- and its baselines -- in one step; there is no separate reset.
struct TrackedContainer
{
    CGroup cgroup;
    std::string container_id;
    std::string container_name;

    TrackedContainer(Registry* registry, std::string cgroup_path, std::string id, std::string name) noexcept
        : cgroup(registry, std::move(cgroup_path)), container_id(std::move(id)), container_name(std::move(name))
    {
    }
};

// Container id (bare hex) -> that container's tracked CGroup + identity.
using ContainerTrackedMap = absl::flat_hash_map<std::string, TrackedContainer>;

// One tracked pod. Holds no CGroup of its own -- per-container is the only granularity cgroup
// metrics are emitted at, since only a container's own cgroup gives independent delta-tracking
// baselines. nf.app/nf.stack/nf.detail/nf.cluster are pod-level (see ResolvePodTags) and shared
// by every container; only nf.process varies per container. `name` feeds the nf.node tag (via
// ResolvePodTags) and `pod_namespace` feeds the k8s.namespace.name tag Refresh() adds to
// pod_tags -- both also serve log-line identity. `containers` holds every container currently
// gated in for metrics.
struct TrackedPod
{
    std::string name;
    std::string pod_namespace;
    ContainerTrackedMap containers;

    TrackedPod(std::string pod_name, std::string ns) noexcept : name(std::move(pod_name)), pod_namespace(std::move(ns))
    {
    }
};

// Pod UID (canonical dashed form) -> that pod's tracked identity + containers.
using PodTrackedMap = absl::flat_hash_map<std::string, TrackedPod>;

// Owns the tracked pod/container set and reconciles it every cycle against a caller-supplied
// PodInfoMap (evict, upsert, resolve tags, reconcile containers), and hosts the cadence-driven
// metric-emission loops (EmitCpuStats/EmitIOStats/EmitMemoryStats) -- the only place with
// mutable access to the CGroups it owns.
class TrackedPodRegistry
{
   public:
    explicit TrackedPodRegistry(Registry* registry) noexcept;

    // Reconciles the tracked pod/container set against `discovered`: evict pods no longer
    // discovered (EvictUntrackedPods), then for each discovered pod, upsert its identity
    // (UpsertPodIdentity), resolve its tags once (ResolvePodTags -- also the Gating decision for
    // every one of its containers), and reconcile its own containers (EvictUntrackedContainers,
    // then ReconcileContainers) -- see each step's own doc comment for detail.
    void Refresh(const PodInfoMap& discovered) noexcept;

    // Read-only view of the tracked pod/container set, for test assertions and debug tooling.
    [[nodiscard]] const PodTrackedMap& TrackedPods() const noexcept { return tracked_pods_; }

    // Emits CGroup::PodCpuStats's cgroup.cpu.* metrics plus its sys.cpu.*/titus.cpu.* metrics
    // (disambiguated per-container via SetExtraTags -- see PodCpuStats's own doc comment) for
    // every container of every currently tracked pod. Never changes tracked pod/container
    // membership itself -- that only happens inside Refresh().
    void EmitCpuStats(const bool fiveSecondMetricsEnabled, const bool sixtySecondMetricsEnabled) noexcept;

    // Emits cgroup I/O metrics (CGroup::IOStats) for every container of every currently tracked
    // pod.
    void EmitIOStats() noexcept;

    // Emits CGroup::MemoryStatsV2's cgroup.mem.* metrics and CGroup::MemoryStatsStdV2's mem.*
    // metrics (despite both reading memory.current/memory.max/memory.stat, StdV2 uses mem.*
    // names, not cgroup.mem.*) for every tracked container, tagged with the nf.*/k8s.* tags
    // ResolvePodTags resolved for its pod plus its own nf.process. Call Refresh() first to
    // include a pod/container discovered this same cycle -- this method itself never changes
    // tracked pod/container membership.
    void EmitMemoryStats() noexcept;

   private:
    // Refresh() step 1: evicts every tracked entry whose UID is no longer in discovered
    // (erasing a TrackedPod destroys its containers' owned CGroups in one step -- see
    // TrackedContainer). absl::flat_hash_map's erase() returns void, not the next iterator like
    // std::unordered_map -- advance the iterator by post-increment before erasing the
    // now-invalidated copy.
    void EvictUntrackedPods(const PodInfoMap& discovered) noexcept;

    // Refresh() step 2: try_emplace's a fresh TrackedPod for this uid (from info.name/
    // info.pod_namespace) if not already tracked, else self-heals name/pod_namespace in place --
    // but only when info carries a non-blank identity. A blank name/pod_namespace pair means
    // "identity unknown this cycle" (a failed kubelet lookup -- see
    // PodMonitor::JoinCgroupAndIdentity), not "this pod's identity became blank"; self-healing
    // only on non-blank input keeps a transient kubelet failure from wiping out an
    // already-tracked pod's identity. The returned reference is valid only for the current loop
    // iteration -- absl::flat_hash_map gives no reference stability across insert/erase.
    [[nodiscard]] TrackedPod& UpsertPodIdentity(const std::string& uid, const PodInfo& info) noexcept;

    // Refresh() step 3 (container pass 1 of 2): evicts pod's tracked containers whose cgroup
    // scope disappeared (id no longer in discovered_containers) -- same eviction as
    // EvictUntrackedPods, one level deeper.
    void EvictUntrackedContainers(TrackedPod& pod, const ContainerCgroupMap& discovered_containers) noexcept;

    // Refresh() step 4 (container pass 2 of 2), only reached once pod_tags has resolved (Gating
    // passed): for every discovered container, applies pod_tags plus its own name for
    // nf.process, and tracks/updates it. pod_tags may already include k8s.namespace.name (added
    // by Refresh() when the pod's namespace is known); every container shares it verbatim aside
    // from nf.process. Skips (without evicting) a container not yet in info.containers -- an
    // expected transient race where a cgroup scope can appear slightly before kubelet reports it
    // in containerStatuses, or vice versa. Re-resolves the CPU count override every cycle, not
    // only at first insertion, since a container's cpu.max can be set slightly after its cgroup
    // directory appears, and an in-place resize can change it later.
    void ReconcileContainers(TrackedPod& pod, const PodInfo& info, const ContainerCgroupMap& discovered_containers,
                              const std::unordered_map<std::string, std::string>& pod_tags) noexcept;

    // The CPU count to configure on a container's CGroup: its own cgroup quota (cpu.max) when
    // set, otherwise the node's total logical CPU count (a container with no quota can burst
    // across every core on the node).
    [[nodiscard]] static double ResolveCpuCountForPod(const CGroup& cgroup) noexcept;

    // Read by Refresh() to construct each newly-discovered container's own CGroup instance.
    Registry* registry_;
    // This agent's own K8S_CLUSTER environment variable, resolved once at construction (not
    // per cycle). May be empty if unset.
    std::string k8s_cluster_;
    PodTrackedMap tracked_pods_;
};

}  // namespace atlasagent
