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

// One tracked container within a pod: its own private CGroup instance (so its CPU/IO/memory
// delta-tracking baselines never collide with another container's, or with Titus's node-level
// CGroup), plus the identity fields useful for logging. Owning the CGroup here means erasing the
// map entry destroys it -- and with it every one of its private delta-tracking baselines -- in a
// single step; there is no separate "reset" operation.
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

// One tracked pod. Unlike the pod-level design this superseded, TrackedPod no longer owns a
// CGroup of its own -- per-container is now the only granularity cgroup metrics are emitted at,
// since only a container's own cgroup gives independent CPU/IO/memory delta-tracking baselines
// (the nf.app/nf.stack/nf.detail/nf.cluster tags themselves are pod-level -- see ResolvePodTags
// -- shared by every container in a gated-in pod; only nf.process varies per container).
// `name`/`pod_namespace` are kept purely for log-line identity; `containers` holds every
// container currently qualifying for metrics under the annotation/label-gated tag scheme (see
// ResolvePodTags).
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
// discovered PodInfoMap (evict/upsert/tag-resolve/evict-containers/reconcile-containers), and
// hosts the cadence-driven metric-emission loops (CollectCpuStats/CollectIOStats/
// EmitMemoryStats) -- the only place with mutable access to the CGroups it owns.
class TrackedPodRegistry
{
   public:
    explicit TrackedPodRegistry(Registry* registry) noexcept;

    // Reconciles the tracked pod/container set against `discovered`: evict pods no longer
    // discovered (EvictUntrackedPods), then for each discovered pod, upsert its identity
    // (UpsertPodIdentity), resolve its tags once (ResolvePodTags -- also the Gating decision for
    // every one of its containers), and reconcile its own containers (EvictUntrackedContainers,
    // then ReconcileContainers) -- see each helper's own doc comment for the detail of that step.
    void Refresh(const PodInfoMap& discovered) noexcept;

    // Read-only view of the tracked pod/container set, for test assertions and debug tooling.
    [[nodiscard]] const PodTrackedMap& TrackedPods() const noexcept { return tracked_pods_; }

    // Emits cgroup.cpu.* container-scoped metrics (CGroup::PodCpuStats) for every container of
    // every currently tracked pod. Never changes tracked pod/container membership itself --
    // that only happens inside Refresh().
    void CollectCpuStats(const bool fiveSecondMetricsEnabled, const bool sixtySecondMetricsEnabled) noexcept;

    // Emits cgroup I/O metrics (CGroup::IOStats) for every container of every currently tracked
    // pod.
    void CollectIOStats() noexcept;

    // Emits both CGroup::MemoryStatsV2's cgroup.mem.* metrics and CGroup::MemoryStatsStdV2's
    // mem.* metrics (mem.cached/mem.shared/mem.availReal/mem.freeReal/mem.totalReal/
    // mem.availSwap/mem.totalSwap/mem.totalFree -- NOT cgroup.mem.* names, despite reading the
    // same per-cgroup files) for every currently tracked container, each disambiguated by the
    // nf.*/k8s.* tags ResolvePodTags resolved for its pod (plus its own nf.process). Callers
    // that want a newly-discovered pod/container sampled the same cycle it's discovered must
    // call Refresh() first -- this method itself never changes tracked pod/container membership.
    void EmitMemoryStats() noexcept;

   private:
    // Refresh() step 1: evict every tracked entry whose UID is no longer in discovered. Erasing
    // the map entry destroys its TrackedPod, which destroys every one of its still-tracked
    // containers' owned CGroups, freeing all of their delta-tracking baselines in one step --
    // there is no separate "reset". Unlike std::unordered_map, absl::flat_hash_map's
    // single-iterator erase() returns void (not the next iterator), so the iterator must be
    // advanced with post-increment before the (now invalidated) copy is erased.
    void EvictUntrackedPods(const PodInfoMap& discovered) noexcept;

    // Refresh() step 2: try_emplace this uid into tracked_pods_ (inserting a fresh TrackedPod
    // from info.name/info.pod_namespace if not already tracked), self-healing an already-tracked
    // pod's name/pod_namespace in place if the fresh info carries a different, non-blank
    // identity. `info.name`/`info.pod_namespace` are only ever both populated together, from a
    // successful identity lookup for this exact uid (see PodMonitor::JoinCgroupAndIdentity) -- a
    // nullopt/omitted identity this cycle collapses both to "". Treat that blank pair as
    // "identity unknown this cycle", not as "this pod's identity became blank": self-heal only
    // when the fresh info actually carries a resolved identity, so a transient kubelet HTTP call
    // failure never wipes out an already-correctly-tracked pod's name/namespace. Returns a
    // reference into tracked_pods_, valid only within the same loop iteration it was returned in
    // -- absl::flat_hash_map gives no iterator/reference stability across insertion/erasure.
    [[nodiscard]] TrackedPod& UpsertPodIdentity(const std::string& uid, const PodInfo& info) noexcept;

    // Refresh() step 3 (container sub-pass 1 of 2): evict any of pod's tracked containers whose
    // scope directory disappeared entirely (container id no longer in discovered_containers) --
    // same "disappeared" eviction as EvictUntrackedPods, just one level deeper.
    void EvictUntrackedContainers(TrackedPod& pod, const ContainerCgroupMap& discovered_containers) noexcept;

    // Refresh() step 4 (container sub-pass 2 of 2), only reached once pod_tags has resolved
    // (Gating passed): for every currently discovered container, apply pod_tags plus its own
    // name for nf.process, and track/update it accordingly. pod_tags is pod-level and already
    // includes k8s.namespace.name -- every container below shares it verbatim aside from
    // nf.process. Skips (without evicting) a container not yet in info.containers -- an expected
    // transient race where a container's cgroup scope can appear slightly before kubelet reports
    // it in containerStatuses, or vice versa. Re-resolves SetCpuCountOverride every cycle (not
    // only at first insertion) since a container's cgroup directory can appear slightly before
    // cpu.max is set to its real quota, and an in-place resize can change it later.
    void ReconcileContainers(TrackedPod& pod, const PodInfo& info, const ContainerCgroupMap& discovered_containers,
                              const std::unordered_map<std::string, std::string>& pod_tags) noexcept;

    // The CPU count to configure on a container's CGroup: its own cgroup quota (cpu.max) when
    // set, otherwise the node's total logical CPU count (a container with no quota can burst
    // across every core on the node).
    [[nodiscard]] static double ResolveCpuCountForPod(const CGroup& cgroup) noexcept;

    // Read by Refresh() to construct each newly-discovered pod's/container's own CGroup
    // instance.
    Registry* registry_;
    // This agent's own K8S_CLUSTER environment variable, resolved once at construction (not
    // per-container-per-cycle) -- the direct equivalent of OTel's ${env:K8S_CLUSTER}
    // config-load-time interpolation; this agent has no separate config-load phase, so
    // construction is the equivalent point in its own lifecycle. May be empty if unset.
    std::string k8s_cluster_;
    PodTrackedMap tracked_pods_;
};

}  // namespace atlasagent
