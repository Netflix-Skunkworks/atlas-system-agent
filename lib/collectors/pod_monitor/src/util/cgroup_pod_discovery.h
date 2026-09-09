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

// Runtime container-id suffix extracted from the cgroup directory name -> that container's cgroup
// v2 scope directory, one level below its pod's own cgroup directory. The current matcher strips
// the systemd/containerd prefix and suffix but validates only the id's minimum length, not its
// contents.
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
// The asymmetry is the dangerous shape: pod directories are still discovered while every container
// is silently missed, so the node publishes no container metrics without an explicit discovery
// error. FindContainersInPodFindsNothingUnderCgroupfsDriverKnownGap pins that behavior.
//
// Before widening: CRI-O also creates a sibling crio-conmon-<id>[.scope] cgroup per container for
// its monitor process. That is NOT a container, and any matcher loose enough to accept crio-<id>
// accepts it too -- exclude it explicitly, do not rely on the downstream kubelet id->name filter.
//
// See FindActivePodCgroups in the .cpp for the pod-level layouts drawn side by side.
//
// Deployment constraint: container metrics require containerd with the systemd driver. Re-verify
// that constraint before enabling this collector on CRI-O or cgroupfs nodes.
class CgroupPodDiscovery
{
   public:
    explicit CgroupPodDiscovery(std::string path_prefix = "/sys/fs/cgroup") noexcept : path_prefix_(std::move(path_prefix))
    {
    }

    // Every pod-level cgroup directory found under the configured root, keyed by pod UID in
    // canonical (dashed) form. Chooses the systemd or cgroupfs layout on each call and scans pod
    // directories directly under the Kubernetes root or one QoS directory below it; it does not
    // recurse into pod directories or return per-container leaves.
    [[nodiscard]] PodCgroupMap FindActivePodCgroups() const noexcept;

    void SetPrefix(std::string new_prefix) noexcept { path_prefix_ = std::move(new_prefix); }

    // pod_cgroup_dir's immediate subdirectories matching "cri-containerd-<runtime-id>.scope"
    // (containerd + systemd driver), keyed by the stripped id. The id is validated on LENGTH ONLY,
    // never on its characters, so a long enough non-hex id is accepted as-is. Never throws: an
    // unopenable dir yields an empty map, an error mid-iteration yields whatever matched so far.
    //
    // This is ONE of the six runtime x driver spellings above; the other five yield an empty map,
    // not an error. A widening cannot treat cgroupfs as meaning "bare id" -- see the trap above.
    //
    // For the supported systemd/containerd layout, the result INCLUDES THE POD SANDBOX (pause)
    // scope: containerd's sandbox setup uses the same cri-containerd-<id>.scope shape, so nothing in
    // the path distinguishes it. Its id is in none of the parsed kubelet status arrays, so
    // ReconcileContainers skips it downstream -- expected, not a fault.
    //
    // Containerd can also create a sandbox cgroup under cgroupfs, but this function does not match
    // that driver. Modern CRI-O generally does not create a sandbox cgroup at all.
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
