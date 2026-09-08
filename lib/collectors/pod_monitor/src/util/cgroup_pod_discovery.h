#pragma once

#include <absl/container/flat_hash_map.h>

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace atlasagent
{

// Pod UID (canonical dashed form) -> that pod's cgroup v2 directory.
using PodCgroupMap = absl::flat_hash_map<std::string, std::filesystem::path>;

// Container id (bare hex, no runtime scheme prefix) -> that container's cgroup v2 scope
// directory, one level below its pod's own cgroup directory.
using ContainerCgroupMap = absl::flat_hash_map<std::string, std::filesystem::path>;

// Pure cgroup-v2 filesystem discovery of pod- and container-level cgroup directories under a
// configurable root -- zero Kubernetes/identity/HTTP knowledge.
//
// Two independent variables name a cgroup directory: the DRIVER (systemd | cgroupfs) fixes the
// STRUCTURE (.slice/.scope unit suffixes; underscores vs dashes in a pod uid), the RUNTIME fixes
// the container name's PREFIX. They do NOT compose independently at the container level, so the
// matrix is literal (verified against containerd, CRI-O and moby source, not inferred):
//
//   runtime     driver     container directory        matched here?
//   containerd  systemd    cri-containerd-<id>.scope  YES -- the only supported combination
//   containerd  cgroupfs   <id>                       no
//   CRI-O       systemd    crio-<id>.scope            no
//   CRI-O       cgroupfs   crio-<id>                  no
//   docker      systemd    docker-<id>.scope          no (dockershim removed in k8s 1.24)
//   docker      cgroupfs   <id>                       no
//
// The trap: cgroupfs does NOT simply mean "bare id" -- CRI-O keeps its crio- prefix there and only
// drops .scope, the one runtime whose prefix survives a driver change. containerd and docker under
// cgroupfs are byte-for-byte indistinguishable from each other.
//
// Pod discovery (FindActivePodCgroups) handles BOTH drivers; container discovery handles ONE row.
// The asymmetry is the dangerous shape: pods are tracked while every container is silently missed,
// so the node publishes no container metrics while looking healthy -- a pod-discovery failure would
// announce itself. FindContainersInPodFindsNothingUnderCgroupfsDriverKnownGap pins it, with evidence.
//
// Before widening: CRI-O also creates a sibling crio-conmon-<id>[.scope] cgroup per container for
// its monitor process. That is NOT a container, and any matcher loose enough to accept crio-<id>
// accepts it too -- exclude it explicitly, do not rely on the downstream kubelet id->name filter.
//
// See FindActivePodCgroups in the .cpp for the pod-level layouts drawn side by side.
//
// As of 2026-09 unreachable: the whole fleet runs containerd (no CRI-O) with the systemd driver.
// Point-in-time, not an invariant -- RE-VERIFY before relying on it, and gate any CRI-O/cgroupfs
// migration on it.
class CgroupPodDiscovery
{
   public:
    explicit CgroupPodDiscovery(std::string path_prefix = "/sys/fs/cgroup") noexcept : path_prefix_(std::move(path_prefix))
    {
    }

    // Every pod-level cgroup directory on this node, keyed by pod UID in canonical (dashed) form.
    // Re-detects the cgroup v2 driver (systemd vs cgroupfs) on each call, and walks at most two
    // levels deep, so it only ever finds pod-aggregate cgroups, never per-container leaves.
    [[nodiscard]] PodCgroupMap FindActivePodCgroups() const noexcept;

    void SetPrefix(std::string new_prefix) noexcept { path_prefix_ = std::move(new_prefix); }

    // pod_cgroup_dir's immediate subdirectories matching "cri-containerd-<hex-id>.scope"
    // (containerd + systemd driver), keyed by the stripped id. The id is validated on LENGTH ONLY,
    // never on its characters, so a long enough non-hex id is accepted as-is. Never throws: an
    // unopenable dir yields an empty map, an error mid-iteration yields whatever matched so far.
    //
    // This is ONE of the six runtime x driver spellings above; the other five yield an empty map,
    // not an error. A widening cannot treat cgroupfs as meaning "bare id" -- see the trap above.
    //
    // INCLUDES THE POD SANDBOX (pause) container: containerd's sandbox setup calls the identical
    // path builder, so the sandbox carries the very same cri-containerd-<id>.scope shape and
    // nothing in the path distinguishes it. Its id is in none of kubelet's status arrays, so
    // ReconcileContainers skips it downstream -- expected, not a fault. Measured on a live node as
    // exactly one such scope per pod (55 scopes == 35 running containers + 20 live sandboxes).
    //
    // Runtime-specific, so do not generalize: containerd creates the sandbox cgroup under BOTH
    // drivers, while modern CRI-O generally does not create one at all.
    [[nodiscard]] static ContainerCgroupMap FindContainersInPod(const std::filesystem::path& pod_cgroup_dir) noexcept;

   protected:
    // For testing access
    static void ScanPodSliceDirectory(const std::filesystem::path& dir, std::string_view name_prefix,
                                       std::string_view name_suffix, PodCgroupMap* pods) noexcept;
    static std::optional<std::string_view> MatchPodSliceName(std::string_view name, std::string_view name_prefix,
                                                               std::string_view name_suffix) noexcept;
    static std::optional<std::string> NormalizePodUid(std::string_view raw_uid) noexcept;

   private:
    std::string path_prefix_;
};

}  // namespace atlasagent
