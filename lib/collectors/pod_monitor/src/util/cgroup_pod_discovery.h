#pragma once

#include <absl/container/flat_hash_map.h>

#include <filesystem>
#include <expected>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

namespace atlasagent
{

// Runtime container-id suffix extracted from the cgroup directory name -> that container's cgroup
// v2 scope directory, one level below its pod's own cgroup directory. The current matcher strips
// the systemd/containerd prefix and suffix but validates only the id's minimum length, not its
// contents.
using ContainerCgroupMap = absl::flat_hash_map<std::string, std::filesystem::path>;

// One complete cgroup observation for a pod. The container map is discovered from the pod path in
// the same source operation as the pod itself, so callers do not need to rescan each pod after the
// pod-level scan has completed.
struct CgroupPod
{
    std::filesystem::path cgroup_path;
    ContainerCgroupMap containers;
};

// Pod UID (canonical dashed form) -> the pod's cgroup path and its discovered container scopes.
using CgroupSnapshot = absl::flat_hash_map<std::string, CgroupPod>;

enum class CgroupDiscoveryErrorKind
{
    // PodMonitor was constructed without a cgroup source implementation.
    kUnavailableSource,
    // Neither the supported systemd root nor the cgroupfs root exists.
    kMissingRoot,
    // A cgroup root exists, but cannot be inspected as a directory.
    kUnreadableRoot,
    // A supported root was found, but a directory scan failed or a discovered pod's container
    // directory was present but could not be inspected. A pod that disappears during discovery is
    // normal lifecycle churn and is omitted from the successful snapshot.
    kUnreadableScan,
    // The cgroupfs layout was found. Pod directories are discoverable there, but container matching
    // is intentionally unsupported because the runtime spelling is ambiguous.
    kUnsupportedCgroupfsLayout,
};

struct CgroupDiscoveryError
{
    CgroupDiscoveryErrorKind kind;
    // The root, directory, or entry whose inspection failed. Empty only for a missing injected
    // source, which has no filesystem path.
    std::filesystem::path path;
    // The filesystem cause when one exists. Configuration/layout errors leave this unset.
    std::error_code cause;
};

using CgroupDiscoveryResult = std::expected<CgroupSnapshot, CgroupDiscoveryError>;

[[nodiscard]] std::string_view ToString(CgroupDiscoveryErrorKind error) noexcept;

// Narrow source boundary for obtaining a complete pod/container cgroup snapshot. Identity and
// Kubernetes HTTP concerns are intentionally outside this interface.
class PodCgroupSource
{
   public:
    virtual ~PodCgroupSource() = default;

    [[nodiscard]] virtual CgroupDiscoveryResult Discover() const noexcept = 0;
};

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
// Pod-directory names can be parsed under both drivers, but complete container discovery handles
// only one row. Discover() therefore reports cgroupfs explicitly as unsupported instead of returning
// pods with silently empty container maps.
//
// Before widening: CRI-O also creates a sibling crio-conmon-<id>[.scope] cgroup per container for
// its monitor process. That is NOT a container, and any matcher loose enough to accept crio-<id>
// accepts it too -- exclude it explicitly, do not rely on the downstream kubelet id->name filter.
//
// Deployment constraint: container metrics require containerd with the systemd driver. Re-verify
// that constraint before enabling this collector on CRI-O or cgroupfs nodes.
class CgroupPodDiscovery : public PodCgroupSource
{
   public:
    explicit CgroupPodDiscovery(std::string path_prefix = "/sys/fs/cgroup") noexcept
        : path_prefix_(std::move(path_prefix))
    {
    }

    // Discovers a complete systemd+containerd snapshot. Success means every discovered pod has a
    // container map from the same source operation. cgroupfs is reported explicitly as unsupported,
    // while missing roots and failed scans are returned distinctly with their failing path and
    // filesystem cause. The result includes containerd's pod-sandbox scope; BuildActivePods ignores
    // it because kubelet status has no matching container identity.
    [[nodiscard]] CgroupDiscoveryResult Discover() const noexcept override;

   protected:
    // For testing access
    static std::optional<std::string_view> MatchPodSliceName(std::string_view name, std::string_view name_prefix,
                                                               std::string_view name_suffix) noexcept;
    static std::optional<std::string> NormalizePodUid(std::string_view raw_uid) noexcept;

   private:
    std::string path_prefix_;
};

}  // namespace atlasagent
