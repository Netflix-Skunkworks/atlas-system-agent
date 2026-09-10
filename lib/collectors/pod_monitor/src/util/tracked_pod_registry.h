#pragma once

#include <thirdparty/spectator-cpp/spectator/registry.h>
#include <absl/container/flat_hash_map.h>

#include <lib/collectors/cgroup/src/cgroup.h>

#include "active_pod_builder.h"

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>

namespace atlasagent
{

enum class CpuMetricState
{
    kUnknown,
    kEnabled,
    kDisabled,
};

// One tracked container: owns a private CGroup so its CPU/IO/memory delta-tracking baselines do not
// collide with another CGroup instance's state. Erasing the map entry destroys the CGroup and its
// baselines in one step; ResetCpuStats() can independently reset CPU state when quota becomes unreadable.
struct TrackedContainer
{
    CGroup cgroup;
    // This container's cgroup scope directory. Duplicated here because cgroup's path_prefix_ is
    // protected, so the emit loops can test whether it still exists -- see ContainerIsLive.
    std::filesystem::path cgroup_path;
    std::string container_name;
    // CPU-only availability. Memory and I/O emission do not depend on this state.
    CpuMetricState cpu_metric_state = CpuMetricState::kUnknown;

    // `path` is COPIED into cgroup and only then moved into cgroup_path -- members initialize in
    // declaration order, so cgroup is built first. Do not std::move() it into cgroup, or
    // cgroup_path would be constructed from a moved-from value.
    TrackedContainer(Registry* registry, std::filesystem::path path, std::string name) noexcept
        : cgroup(registry, path.string()),
          cgroup_path(std::move(path)),
          container_name(std::move(name))
    {
    }
};

// Runtime container-id key admitted by BuildActivePods() -> that container's tracked CGroup and
// identity.
using ContainerTrackedMap = absl::flat_hash_map<std::string, TrackedContainer>;

// One admitted pod. Holds no CGroup of its own; each container owns the independent baselines used
// for metric emission. The reconciler updates name, namespace, tags, and container membership from
// each successful active snapshot, so no identity inference occurs in this class.
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

// Owns the active pod/container set and the mutable CGroups used for metric baselines.
// BuildActivePods() decides admission before this class sees the data.
class TrackedPodRegistry
{
   public:
    explicit TrackedPodRegistry(Registry* registry) noexcept;

    // Applies one successful active snapshot before subsequent emitter calls. Containers absent
    // from active_pods are evicted, including entries whose current kubelet status could not be
    // confirmed.
    void Reconcile(const ActivePodMap& active_pods) noexcept;

    // Fail closed on a source error: drop every CGroup baseline and prevent emission until the next
    // successful Reconcile(). This also prevents deltas from spanning an unverified interval.
    void Suspend() noexcept;

    // Read-only view of the tracked pod/container set, for test assertions and debug tooling.
    [[nodiscard]] const PodTrackedMap& TrackedPods() const noexcept { return tracked_pods_; }

    // Emits CGroup::PodCpuStats (cgroup.cpu.* plus sys.cpu.*/k8s.cpu.*, disambiguated
    // per-container via SetExtraTags -- see PodCpuStats's own doc comment) for every LIVE container
    // of every tracked pod; one whose cgroup scope vanished since the last Reconcile() is skipped,
    // as is one whose cpu.max is unreadable at emission time. See ContainerIsLive. Never
    // changes tracked membership -- only Reconcile()/Suspend() do.
    void EmitCpuStats(const bool fiveSecondMetricsEnabled, const bool sixtySecondMetricsEnabled) noexcept;

    // Emits cgroup I/O metrics (CGroup::IOStats) for every LIVE container of every currently
    // tracked pod -- see ContainerIsLive for the liveness skip.
    void EmitIOStats() noexcept;

    // Emits CGroup::MemoryStatsV2 (cgroup.mem.*) and CGroup::MemoryStatsStdV2 (mem.*, NOT
    // cgroup.mem.*, despite both reading memory.current/memory.max/memory.stat) for every LIVE
    // tracked container (see ContainerIsLive), tagged with the pod tags supplied by the active
    // snapshot and its own nf.process. Never changes tracked membership; call Reconcile() first to
    // include a pod/container admitted this cycle.
    void EmitMemoryStats() noexcept;

   private:
    [[nodiscard]] TrackedPod& UpsertPod(const std::string& uid, const ActivePod& active) noexcept;

    void ReconcileContainers(const std::string& uid, TrackedPod& pod, const ActivePod& active) noexcept;

    // Resolves a numeric cpu.max limit directly and an explicit unlimited quota to the online
    // processor count. Returns nullopt when cpu.max or the processor count is unreadable.
    [[nodiscard]] static std::optional<double> ResolveCpuCount(const CGroup& cgroup) noexcept;

    // Refreshes the container's CPU count immediately before emission. On an enabled -> disabled
    // transition, resets CPU baselines so recovery cannot bridge the unreadable interval.
    [[nodiscard]] static bool UpdateCpuMetricState(TrackedContainer& container,
                                                    std::string_view container_id) noexcept;

    // Whether a tracked container's cgroup scope still exists. In the shipped k8s-agent caller,
    // reconciliation normally runs on the 60-second refresh cadence while EmitCpuStats() runs every
    // second, so an entry can remain tracked until the next refresh after containerd removes its
    // scope directory. Without this check, emitting during that interval would publish wrong data
    // under the POD's live nf.app/nf.cluster tags: CpuProcessingCapacity reads no files, so it could
    // accumulate phantom capacity into a Counter, and sys.cpu.numProcessors / k8s.cpu.requested are
    // emitted before CpuUtilizationV2's cpu.stat guard.
    //
    // Tests only whether the scope DIRECTORY exists; it does not inspect cgroup.events or verify that
    // the cgroup is populated. Keying on one metric file would conflate "container gone" with "that
    // file unreadable", and would gate out every fixture-driven test in pod_monitor_test.cpp (no
    // checked-in fixture carries cpu.stat).
    //
    // Skipping, not evicting: membership changes belong to Reconcile()/Suspend() (see the Emit*
    // docs above), and erasing mid-iteration would invalidate the loop's own iterator.
    [[nodiscard]] static bool ContainerIsLive(const TrackedContainer& container) noexcept;

    // Shared traversal for the three Emit* methods. The template is defined in the .cpp because all
    // instantiations are private to that translation unit. CPU callers can additionally require a
    // readable quota without suppressing memory or I/O for the same container.
    template <typename EmitFn>
    void ForEachLiveContainer(std::string_view metric_type, bool require_cpu_metrics,
                              EmitFn&& emit) noexcept;

    // Read by Reconcile() to construct each newly admitted container's own CGroup instance.
    Registry* registry_;
    PodTrackedMap tracked_pods_;
};

}  // namespace atlasagent
