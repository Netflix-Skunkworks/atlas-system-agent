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

// Pod UID (canonical dashed form) -> cgroup path plus identity resolved from the apiserver.
struct PodInfo
{
    std::string uid;
    std::filesystem::path cgroup_path;
    std::string name;
    std::string pod_namespace;
};
using PodInfoMap = absl::flat_hash_map<std::string, PodInfo>;

// One tracked pod: its own private CGroup instance (so its CPU/IO/memory delta-tracking
// baselines never collide with another pod's, or with Titus's node-level CGroup), plus the
// identity fields it was last tagged with. Owning the CGroup here means erasing the map entry
// destroys it -- and with it every one of its private delta-tracking baselines -- in a single
// step; there is no separate "reset" operation.
struct TrackedPod
{
    CGroup cgroup;
    std::string name;
    std::string pod_namespace;

    TrackedPod(Registry* registry, std::string cgroup_path, std::string pod_name, std::string ns) noexcept
        : cgroup(registry, std::move(cgroup_path)), name(std::move(pod_name)), pod_namespace(std::move(ns))
    {
    }
};

// Pod UID (canonical dashed form) -> that pod's tracked CGroup + identity.
using PodTrackedMap = absl::flat_hash_map<std::string, TrackedPod>;

class PodMonitor
{
   public:
    explicit PodMonitor(Registry* registry, std::string path_prefix = "/sys/fs/cgroup",
                         std::string kubeconfig_path = PodIdentityClientConstants::KubeconfigPath) noexcept
        : registry_(registry), path_prefix_(std::move(path_prefix)),
          identity_client_(registry, std::move(kubeconfig_path))
    {
    }

    // Discovers every pod-level cgroup directory on this node, keyed by pod UID in
    // canonical (dashed) form. Detects the cgroup v2 driver (systemd vs cgroupfs) fresh
    // on each call, and walks at most two levels deep, so it only ever finds pod-aggregate
    // cgroups, never per-container leaves.
    [[nodiscard]] PodCgroupMap FindAllActivePods() const noexcept;

    // Same as FindAllActivePods(), but also resolves each pod's Name and Namespace via a live
    // apiserver call. Slower and network-dependent; FindAllActivePods() alone is sufficient when
    // only cgroup paths are needed.
    [[nodiscard]] PodInfoMap FindAllActivePods2() const noexcept;

    void SetPrefix(std::string new_prefix) noexcept { path_prefix_ = std::move(new_prefix); }

    // Emits cgroup.cpu.* pod-scoped metrics (CGroup::PodCpuStats) for every currently tracked
    // pod. Never changes tracked_pods_ membership itself -- that only happens inside
    // RefreshTrackedPods(), called from CollectMemoryStats().
    void CollectCpuStats(const bool fiveSecondMetricsEnabled, const bool sixtySecondMetricsEnabled) noexcept;

    // Emits cgroup I/O metrics (CGroup::IOStats) for every currently tracked pod.
    void CollectIOStats() noexcept;

    // Refreshes tracked_pods_ against the currently discovered pod set (see
    // RefreshTrackedPods()), then emits both CGroup::MemoryStatsV2's cgroup.mem.* metrics and
    // CGroup::MemoryStatsStdV2's mem.* metrics (mem.cached/mem.shared/mem.availReal/mem.freeReal/
    // mem.totalReal/mem.availSwap/mem.totalSwap/mem.totalFree -- NOT cgroup.mem.* names, despite
    // reading the same per-cgroup files) for every now-current tracked pod, each disambiguated by
    // the pod's SetExtraTags()-applied tag -- so a newly-discovered pod is sampled the same cycle
    // it's discovered, and an evicted pod's already-destroyed CGroup is never touched.
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

    // Discovers the current pod set (FindAllActivePods2()) and reconciles tracked_pods_ against
    // it in two fully separate passes -- never interleaved, since absl::flat_hash_map gives no
    // iterator/reference stability across insertion/erasure:
    //   1. Evict every tracked_pods_ entry whose UID is no longer discovered. Erasing the map
    //      entry destroys its TrackedPod, which destroys its owned CGroup, freeing all of its
    //      delta-tracking baselines in one step.
    //   2. For each discovered pod: if not already tracked, try_emplace a new TrackedPod and tag
    //      its CGroup (SetExtraTags). If already tracked and the freshly discovered info carries
    //      a non-empty resolved identity that differs from what's stored (self-heal for identity
    //      resolution racing the cgroup appearing), update the tracked identity and re-tag -- a
    //      blank identity (nullopt/omitted from FetchPodIdentities() this cycle) is never treated
    //      as authoritative, so a transient identity-fetch failure can't wipe out a previously-
    //      good name/namespace. Every discovered pod (new or already tracked) also gets its CPU
    //      count override re-resolved (SetCpuCountOverride) every cycle, so a quota that appears
    //      or changes after initial discovery is picked up rather than sticking forever.
    void RefreshTrackedPods() noexcept;

    // Read-only view of tracked_pods_, for test assertions only.
    [[nodiscard]] const PodTrackedMap& TrackedPods() const noexcept { return tracked_pods_; }

   private:
    // The tags every metric emitted by a newly- (or freshly-identified) pod's CGroup should
    // carry.
    [[nodiscard]] static std::unordered_map<std::string, std::string> BuildPodTags(const PodInfo& info) noexcept;

    // The CPU count to configure on a pod's CGroup: its own cgroup quota (cpu.max) when set,
    // otherwise the node's total logical CPU count (a pod with no quota can burst across every
    // core on the node).
    [[nodiscard]] static double ResolveCpuCountForPod(const CGroup& cgroup) noexcept;

    // Read by RefreshTrackedPods() to construct each newly-discovered pod's own CGroup instance.
    Registry* registry_;
    std::string path_prefix_;
    PodIdentityClient identity_client_;
    PodTrackedMap tracked_pods_;
};

}  // namespace atlasagent
