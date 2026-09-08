#pragma once

#include <thirdparty/spectator-cpp/spectator/registry.h>
#include <absl/container/flat_hash_map.h>

#include <lib/collectors/cgroup/src/cgroup.h>

#include "cgroup_pod_discovery.h"
#include "pod_info.h"

#include <filesystem>
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
    // This container's cgroup scope directory. Duplicated here because cgroup's path_prefix_ is
    // protected, so the emit loops can test whether it still exists -- see ContainerIsLive.
    std::filesystem::path cgroup_path;
    std::string container_id;
    std::string container_name;

    // `path` is COPIED into cgroup and only then moved into cgroup_path -- members initialize in
    // declaration order, so cgroup is built first. Do not std::move() it into cgroup, or
    // cgroup_path would be constructed from a moved-from value.
    TrackedContainer(Registry* registry, std::filesystem::path path, std::string id, std::string name) noexcept
        : cgroup(registry, path.string()),
          cgroup_path(std::move(path)),
          container_id(std::move(id)),
          container_name(std::move(name))
    {
    }
};

// Container id (bare hex) -> that container's tracked CGroup + identity.
using ContainerTrackedMap = absl::flat_hash_map<std::string, TrackedContainer>;

// One tracked pod. Holds no CGroup of its own -- only a container's own cgroup gives independent
// delta-tracking baselines, so per-container is the sole emission granularity.
// nf.app/nf.stack/nf.detail/nf.cluster are pod-level (see ResolvePodTags), shared by every
// container; only nf.process varies. `name` feeds nf.node, `pod_namespace` feeds the
// k8s.namespace.name tag Refresh() adds; `containers` holds only containers gated in for metrics.
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

// Owns the tracked pod/container set, reconciles it each cycle against a caller-supplied
// PodInfoMap (Refresh), and hosts the cadence-driven metric-emission loops
// (EmitCpuStats/EmitIOStats/EmitMemoryStats) -- the only place with mutable access to its CGroups.
class TrackedPodRegistry
{
   public:
    explicit TrackedPodRegistry(Registry* registry) noexcept;

    // Reconciles the tracked set against `discovered`: EvictUntrackedPods, then per discovered pod
    // UpsertPodIdentity, ResolvePodTags once (also the Gating decision for all its containers),
    // EvictUntrackedContainers, ReconcileContainers. See each step's own doc comment for detail.
    void Refresh(const PodInfoMap& discovered) noexcept;

    // Read-only view of the tracked pod/container set, for test assertions and debug tooling.
    [[nodiscard]] const PodTrackedMap& TrackedPods() const noexcept { return tracked_pods_; }

    // Emits CGroup::PodCpuStats (cgroup.cpu.* plus sys.cpu.*/k8s.cpu.*, disambiguated
    // per-container via SetExtraTags -- see PodCpuStats's own doc comment) for every LIVE container
    // of every tracked pod; one whose cgroup scope vanished since the last Refresh() is skipped,
    // see ContainerIsLive. Never changes tracked membership -- only Refresh() does.
    void EmitCpuStats(const bool fiveSecondMetricsEnabled, const bool sixtySecondMetricsEnabled) noexcept;

    // Emits cgroup I/O metrics (CGroup::IOStats) for every LIVE container of every currently
    // tracked pod -- see ContainerIsLive for the liveness skip.
    void EmitIOStats() noexcept;

    // Emits CGroup::MemoryStatsV2 (cgroup.mem.*) and CGroup::MemoryStatsStdV2 (mem.*, NOT
    // cgroup.mem.*, despite both reading memory.current/memory.max/memory.stat) for every LIVE
    // tracked container (see ContainerIsLive), tagged with the nf.*/k8s.* tags ResolvePodTags
    // resolved for its pod plus its own nf.process. Never changes tracked membership; call
    // Refresh() first to include a pod/container discovered this same cycle.
    void EmitMemoryStats() noexcept;

   private:
    // Refresh() step 1: evicts every tracked entry whose UID is no longer in discovered -- erasing
    // a TrackedPod destroys its containers' owned CGroups in one step (see TrackedContainer). See
    // the .cpp for the absl::flat_hash_map post-increment erase idiom it needs.
    void EvictUntrackedPods(const PodInfoMap& discovered) noexcept;

    // Refresh() step 2: try_emplace's a fresh TrackedPod for this uid, else self-heals
    // name/pod_namespace in place -- but only when info carries a non-blank identity. Blank means
    // "identity unknown this cycle" (a failed kubelet lookup -- see PodMonitor::JoinCgroupAndIdentity),
    // not "this pod's identity became blank", so ignoring it keeps a transient kubelet failure from
    // wiping an already-tracked identity. The returned reference is valid only for the current loop
    // iteration -- absl::flat_hash_map gives no reference stability across insert/erase.
    [[nodiscard]] TrackedPod& UpsertPodIdentity(const std::string& uid, const PodInfo& info) noexcept;

    // Refresh() step 3 (container pass 1 of 2): evicts pod's tracked containers whose id is no
    // longer in discovered_containers -- same eviction as EvictUntrackedPods, one level deeper.
    void EvictUntrackedContainers(TrackedPod& pod, const ContainerCgroupMap& discovered_containers) noexcept;

    // Refresh() step 4 (container pass 2 of 2), only reached once pod_tags has resolved (Gating
    // passed): for every discovered container, applies pod_tags plus its own name for nf.process
    // and tracks/updates it. pod_tags may already include k8s.namespace.name (added by Refresh()
    // when the pod's namespace is known); every container shares it verbatim aside from
    // nf.process. Skips (without evicting) a container not in info.containers, and re-resolves the
    // CPU count override every cycle rather than only at first insertion -- see the .cpp for why
    // in both cases.
    void ReconcileContainers(TrackedPod& pod, const PodInfo& info, const ContainerCgroupMap& discovered_containers,
                              const std::unordered_map<std::string, std::string>& pod_tags) noexcept;

    // The CPU count to configure on a container's CGroup: its own cgroup quota (cpu.max) when set,
    // otherwise the node's logical CPU count -- a container with no quota can burst across every
    // core on the node.
    [[nodiscard]] static double ResolveCpuCountForPod(const CGroup& cgroup) noexcept;

    // Whether a tracked container's cgroup scope still exists. EvictUntrackedContainers runs only
    // on Refresh()'s 60s cadence while EmitCpuStats() runs every second, so a container stays
    // tracked for up to a minute after containerd removed its scope directory. Emitting then
    // publishes wrong data under the POD's live nf.app/nf.cluster tags: CpuProcessingCapacity reads
    // no files, so nothing stops it accumulating phantom capacity into a Counter, and
    // sys.cpu.numProcessors / k8s.cpu.requested leak because CpuUtilizationV2 emits both BEFORE its
    // cpu.stat guard.
    //
    // Tests the scope DIRECTORY, not a file inside it: containerd removes the whole directory on
    // termination, the unambiguous signal. Keying on a file would conflate "container gone" with
    // "that file unreadable", and would gate out every fixture-driven test in pod_monitor_test.cpp
    // (no fixture carries cpu.stat).
    //
    // Skipping, not evicting: membership changes belong to Refresh() alone (see the Emit* docs
    // above), and erasing mid-iteration would invalidate the loop's own iterator.
    [[nodiscard]] static bool ContainerIsLive(const TrackedContainer& container) noexcept;

    // Read by Refresh() to construct each newly-discovered container's own CGroup instance.
    Registry* registry_;
    // This agent's own K8S_CLUSTER env var, resolved once at construction (not per cycle). May be
    // empty if unset.
    std::string k8s_cluster_;
    PodTrackedMap tracked_pods_;
};

}  // namespace atlasagent
