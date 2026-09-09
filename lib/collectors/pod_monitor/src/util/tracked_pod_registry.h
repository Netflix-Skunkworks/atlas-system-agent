#pragma once

#include <thirdparty/spectator-cpp/spectator/registry.h>
#include <absl/container/flat_hash_map.h>

#include <lib/collectors/cgroup/src/cgroup.h>

#include "cgroup_pod_discovery.h"
#include "pod_info.h"

#include <filesystem>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>

namespace atlasagent
{

// One tracked container: owns a private CGroup so its CPU/IO/memory delta-tracking baselines do not
// collide with another CGroup instance's state. Erasing the map entry destroys the CGroup and its
// baselines in one step; there is no separate reset.
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

// Runtime container-id key matched between cgroup discovery and PodIdentity -> that container's
// tracked CGroup and identity.
using ContainerTrackedMap = absl::flat_hash_map<std::string, TrackedContainer>;

// One tracked pod. Holds no CGroup of its own -- only a container's own cgroup gives independent
// delta-tracking baselines, so per-container is the sole emission granularity.
// nf.app/nf.stack/nf.cluster are pod-level emitted tags (see ResolvePodTags), shared by every
// container; nf.detail can affect Gating and nf.cluster construction but is not currently emitted.
// Among these identity tags, only nf.process varies by container. `name` feeds nf.node and
// `pod_namespace` feeds the k8s.namespace.name tag Refresh() adds. `containers` holds entries
// admitted during a successful pod-level gate; a later missing container-status entry does not by
// itself evict an existing entry.
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
    // tracked container (see ContainerIsLive), tagged with the pod tags assembled by Refresh()
    // (ResolvePodTags plus k8s.namespace.name when known) and its own nf.process. Never changes
    // tracked membership; call Refresh() first to include a pod/container discovered this cycle.
    void EmitMemoryStats() noexcept;

   private:
    // Refresh() step 1: evicts every tracked entry whose UID is no longer in discovered -- erasing
    // a TrackedPod destroys its containers' owned CGroups in one step (see TrackedContainer). See
    // the .cpp for the absl::flat_hash_map post-increment erase idiom it needs.
    void EvictUntrackedPods(const PodInfoMap& discovered) noexcept;

    // Refresh() step 2: try_emplace's a fresh TrackedPod for this uid, else updates name and
    // pod_namespace together when both incoming fields are non-blank. Blank fields leave the stored
    // name/namespace unchanged. This preserves those two display/tag inputs only; if annotations or
    // labels do not pass Gating later in Refresh(), the pod's tracked containers are still cleared.
    // The returned reference is valid only for the current loop iteration -- absl::flat_hash_map
    // gives no reference stability across insert/erase.
    [[nodiscard]] TrackedPod& UpsertPodIdentity(const std::string& uid, const PodInfo& info) noexcept;

    // Refresh() step 3 (container pass 1 of 2): evicts pod's tracked containers whose id is no
    // longer in discovered_containers -- same eviction as EvictUntrackedPods, one level deeper.
    void EvictUntrackedContainers(TrackedPod& pod, const ContainerCgroupMap& discovered_containers) noexcept;

    // Refresh() step 4 (container pass 2 of 2), only reached once pod_tags has resolved (Gating
    // passed): for every discovered container, applies pod_tags plus its own name for nf.process
    // and tracks/updates it. pod_tags may already include k8s.namespace.name (added by Refresh()
    // when the pod's namespace is known); every container shares it verbatim aside from
    // nf.process. If a discovered id is absent from info.containers, it is not inserted or updated;
    // an existing tracked entry with that id remains eligible for emission. Re-resolves the CPU
    // count override every cycle rather than only at first insertion -- see the .cpp for details.
    void ReconcileContainers(TrackedPod& pod, const PodInfo& info, const ContainerCgroupMap& discovered_containers,
                              const std::unordered_map<std::string, std::string>& pod_tags) noexcept;

    // The CPU count configured on a container's CGroup: the value returned by QuotaCpuCount() when
    // present, otherwise the online processor count returned by sysconf(_SC_NPROCESSORS_ONLN).
    [[nodiscard]] static double ResolveCpuCountForPod(const CGroup& cgroup) noexcept;

    // Whether a tracked container's cgroup scope still exists. In the shipped k8s-agent caller,
    // Refresh() normally runs on the 60-second memory cadence while EmitCpuStats() runs every
    // second, so an entry can remain tracked until the next refresh after containerd removes its
    // scope directory. Without this check,
    // emitting during that interval would publish wrong data under the POD's live nf.app/nf.cluster
    // tags: CpuProcessingCapacity reads no files, so it could accumulate phantom capacity into a
    // Counter, and sys.cpu.numProcessors / k8s.cpu.requested are emitted before CpuUtilizationV2's
    // cpu.stat guard.
    //
    // Tests only whether the scope DIRECTORY exists; it does not inspect cgroup.events or verify that
    // the cgroup is populated. Keying on one metric file would conflate "container gone" with "that
    // file unreadable", and would gate out every fixture-driven test in pod_monitor_test.cpp (no
    // checked-in fixture carries cpu.stat).
    //
    // Skipping, not evicting: membership changes belong to Refresh() alone (see the Emit* docs
    // above), and erasing mid-iteration would invalidate the loop's own iterator.
    [[nodiscard]] static bool ContainerIsLive(const TrackedContainer& container) noexcept;

    // Shared traversal for the three Emit* methods. The template is defined in the .cpp because all
    // instantiations are private to that translation unit.
    template <typename EmitFn>
    void ForEachLiveContainer(std::string_view metric_type, EmitFn&& emit) noexcept;

    // Read by Refresh() to construct each newly-discovered container's own CGroup instance.
    Registry* registry_;
    // This agent's own K8S_CLUSTER env var, resolved once at construction (not per cycle). May be
    // empty if unset.
    std::string k8s_cluster_;
    PodTrackedMap tracked_pods_;
};

}  // namespace atlasagent
