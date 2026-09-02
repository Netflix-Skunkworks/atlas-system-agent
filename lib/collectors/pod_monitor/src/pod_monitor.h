#pragma once

#include <thirdparty/spectator-cpp/spectator/registry.h>
#include <absl/container/flat_hash_map.h>

#include <lib/collectors/cgroup/src/cgroup.h>

#include "pod_identity_client.h"

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>

namespace atlasagent
{

// Pod UID (canonical dashed form) -> that pod's cgroup v2 directory.
using PodCgroupMap = absl::flat_hash_map<std::string, std::filesystem::path>;

// Container id (bare hex, no runtime scheme prefix) -> that container's cgroup v2 scope
// directory, one level below its pod's own cgroup directory.
using ContainerCgroupMap = absl::flat_hash_map<std::string, std::filesystem::path>;

// Pod UID (canonical dashed form) -> cgroup path plus identity resolved from kubelet's local API.
struct PodInfo
{
    std::string uid;
    std::filesystem::path cgroup_path;
    std::string name;
    std::string pod_namespace;
    // Container id (bare hex) -> container name, from kubelet's
    // status.containerStatuses[] (see PodIdentity::containers). Empty when identity resolution
    // didn't resolve this uid this cycle, or the pod has no containers reported yet.
    std::unordered_map<std::string, std::string> containers;
    // Pod annotations/labels, from kubelet's metadata.annotations/metadata.labels (see
    // PodIdentity::annotations/labels). Empty when identity resolution didn't resolve this uid
    // this cycle, or the pod genuinely has none. Feeds ResolvePodTags's fallback chain.
    std::unordered_map<std::string, std::string> annotations;
    std::unordered_map<std::string, std::string> labels;
};
using PodInfoMap = absl::flat_hash_map<std::string, PodInfo>;

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

class PodMonitor
{
   public:
    explicit PodMonitor(Registry* registry, std::string path_prefix = "/sys/fs/cgroup",
                         std::string kubelet_url = PodIdentityClientConstants::KubeletUrl) noexcept;

    // Discovers every pod-level cgroup directory on this node, keyed by pod UID in
    // canonical (dashed) form. Detects the cgroup v2 driver (systemd vs cgroupfs) fresh
    // on each call, and walks at most two levels deep, so it only ever finds pod-aggregate
    // cgroups, never per-container leaves.
    [[nodiscard]] PodCgroupMap FindAllActivePods() const noexcept;

    // Same as FindAllActivePods(), but also resolves each pod's Name, Namespace, annotations,
    // labels, and each of its containers' id -> name mapping via a live call to kubelet's local
    // API. Slower and network-dependent; FindAllActivePods() alone is sufficient when only
    // cgroup paths are needed.
    [[nodiscard]] PodInfoMap FindAllActivePods2() const noexcept;

    void SetPrefix(std::string new_prefix) noexcept { path_prefix_ = std::move(new_prefix); }

    // Emits cgroup.cpu.* container-scoped metrics (CGroup::PodCpuStats) for every container of
    // every currently tracked pod. Never changes tracked_pods_/its containers membership itself
    // -- that only happens inside RefreshTrackedPods(), called from CollectMemoryStats().
    void CollectCpuStats(const bool fiveSecondMetricsEnabled, const bool sixtySecondMetricsEnabled) noexcept;

    // Emits cgroup I/O metrics (CGroup::IOStats) for every container of every currently tracked
    // pod.
    void CollectIOStats() noexcept;

    // Refreshes tracked_pods_ (and, nested within each, its containers) against the currently
    // discovered pod/container set (see RefreshTrackedPods()), then emits both
    // CGroup::MemoryStatsV2's cgroup.mem.* metrics and CGroup::MemoryStatsStdV2's mem.* metrics
    // (mem.cached/mem.shared/mem.availReal/mem.freeReal/mem.totalReal/mem.availSwap/
    // mem.totalSwap/mem.totalFree -- NOT cgroup.mem.* names, despite reading the same per-cgroup
    // files) for every now-current tracked container, each disambiguated by the nf.*/k8s.* tags
    // ResolvePodTags resolved for its pod (plus its own nf.process) -- so a newly-discovered
    // container is sampled the same cycle it's discovered, and an evicted container's
    // already-destroyed CGroup is never touched.
    void CollectMemoryStats() noexcept;

   protected:
    // For testing access
    static void ScanPodSliceDirectory(const std::filesystem::path& dir, std::string_view name_prefix,
                                       std::string_view name_suffix, PodCgroupMap* pods) noexcept;
    static std::optional<std::string_view> MatchPodSliceName(std::string_view name, std::string_view name_prefix,
                                                               std::string_view name_suffix) noexcept;
    static std::optional<std::string> NormalizePodUid(std::string_view raw_uid) noexcept;
    [[nodiscard]] static PodInfoMap JoinCgroupAndIdentity(const PodCgroupMap& cgroup_pods,
                                                           const std::optional<PodIdentityMap>& identities) noexcept;

    // Container discovery: the level below pod discovery that FindAllActivePods()'s own doc
    // comment already anticipates. Lists pod_cgroup_dir's immediate subdirectories and matches
    // container scope names shaped "cri-containerd-<64-hex-id>.scope" (containerd/CRI
    // convention), keyed by the stripped id. Never throws; returns an empty map on any
    // filesystem error, exactly like ScanPodSliceDirectory.
    [[nodiscard]] static ContainerCgroupMap FindContainersInPod(const std::filesystem::path& pod_cgroup_dir) noexcept;

    // Pure fallback-chain resolution of one pod's tags from its own already-resolved annotations
    // and labels (PodIdentity::annotations/labels, populated from kubelet's local /pods endpoint
    // -- see pod_identity_client.{h,cpp}) plus this agent's own K8S_CLUSTER value (may be
    // empty). No I/O -- easily unit-testable with canned input maps. Mirrors the Netflix
    // K8s-native observability design (a mutating admission webhook stamps netflix.com/app,
    // netflix.com/stack, netflix.com/detail annotations onto every pod as the tagging source of
    // truth): primary annotation tier, then (only when still unset) a label fallback tier --
    // app.kubernetes.io/name -> k8s-app -> app for nf.app, app.kubernetes.io/instance for
    // nf.stack, app.kubernetes.io/component for nf.detail -- covering pods that webhook hasn't
    // (yet) annotated. nf.cluster is gated on the *primary* netflix.com/app annotation
    // specifically, never on a label-fallback-resolved nf.app (a deliberate asymmetry). Returns
    // nullopt if NONE of nf.app/nf.stack/nf.detail resolved -- Gating: no metrics for any
    // container in this pod. nf.node/nf.process are deliberately excluded from the Gating
    // decision -- they're always structurally available once a pod's identity resolves at all
    // (nf.node from pod_name here; nf.process from the caller, per-container -- see
    // RefreshTrackedPods), so including them would make Gating vacuous. Otherwise the returned
    // map holds nf.app/nf.stack/nf.detail/nf.cluster (whichever resolved), nf.node=pod_name if
    // pod_name is non-empty, nf.platform="k8s" (unconditionally), and, if k8s_cluster is
    // non-empty, k8s.cluster.name. nf.process is NOT set here -- it's per-container, applied by
    // the caller.
    [[nodiscard]] static std::optional<std::unordered_map<std::string, std::string>> ResolvePodTags(
        const std::unordered_map<std::string, std::string>& annotations,
        const std::unordered_map<std::string, std::string>& labels, const std::string& pod_name,
        const std::string& k8s_cluster) noexcept;

    // Discovers the current pod set (FindAllActivePods2()) and reconciles tracked_pods_ against
    // it in two fully separate passes -- never interleaved, since absl::flat_hash_map gives no
    // iterator/reference stability across insertion/erasure:
    //   1. Evict every tracked_pods_ entry whose UID is no longer discovered. Erasing the map
    //      entry destroys its TrackedPod, which destroys every one of its still-tracked
    //      containers' owned CGroups, freeing all of their delta-tracking baselines in one step.
    //   2. For each discovered pod: self-heal its name/pod_namespace if a fresh, non-blank
    //      identity differs from what's stored (a blank identity this cycle is never treated as
    //      authoritative -- see the note in the .cpp). Resolve that pod's tags ONCE via
    //      ResolvePodTags() -- annotations/labels are pod-level, not per-container, so every
    //      container in a gated-in pod shares the same nf.app/nf.stack/nf.detail/nf.cluster.
    //      Then, nested one level under this same per-pod pass, reconcile that pod's own
    //      containers against FindContainersInPod() -- again as two fully separate sequential
    //      sub-passes (evict-by-disappearance, then track-or-gate-out), never interleaved with
    //      each other or with the outer per-pod passes. If the pod's tags didn't resolve
    //      (Gating), every one of its containers is evicted/skipped this cycle -- there is no
    //      per-container fallback. A container that does get tracked has nf.process filled in
    //      from its own name (kubelet's containerStatuses, via PodInfo::containers) and
    //      k8s.namespace.name from the pod's namespace.
    void RefreshTrackedPods() noexcept;

    // Read-only view of tracked_pods_, for test assertions only.
    [[nodiscard]] const PodTrackedMap& TrackedPods() const noexcept { return tracked_pods_; }

   private:
    // The CPU count to configure on a container's CGroup: its own cgroup quota (cpu.max) when
    // set, otherwise the node's total logical CPU count (a container with no quota can burst
    // across every core on the node).
    [[nodiscard]] static double ResolveCpuCountForPod(const CGroup& cgroup) noexcept;

    // Read by RefreshTrackedPods() to construct each newly-discovered pod's/container's own
    // CGroup instance.
    Registry* registry_;
    std::string path_prefix_;
    PodIdentityClient identity_client_;
    // This agent's own K8S_CLUSTER environment variable, resolved once at construction (not
    // per-container-per-cycle) -- the direct equivalent of OTel's ${env:K8S_CLUSTER}
    // config-load-time interpolation; this agent has no separate config-load phase, so
    // construction is the equivalent point in its own lifecycle. May be empty if unset.
    std::string k8s_cluster_;
    PodTrackedMap tracked_pods_;
};

}  // namespace atlasagent
