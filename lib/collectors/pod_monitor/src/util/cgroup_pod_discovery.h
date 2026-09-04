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
class CgroupPodDiscovery
{
   public:
    explicit CgroupPodDiscovery(std::string path_prefix = "/sys/fs/cgroup") noexcept : path_prefix_(std::move(path_prefix))
    {
    }

    // Discovers every pod-level cgroup directory on this node, keyed by pod UID in
    // canonical (dashed) form. Detects the cgroup v2 driver (systemd vs cgroupfs) fresh
    // on each call, and walks at most two levels deep, so it only ever finds pod-aggregate
    // cgroups, never per-container leaves.
    [[nodiscard]] PodCgroupMap FindActivePodCgroups() const noexcept;

    void SetPrefix(std::string new_prefix) noexcept { path_prefix_ = std::move(new_prefix); }

    // Lists pod_cgroup_dir's immediate subdirectories and matches container scope names shaped
    // "cri-containerd-<hex-id>.scope" (containerd/CRI convention), keyed by the stripped id. The
    // id is only loosely validated (a plausible-length hex string, not a strict UUID like a
    // pod's own uid). Never throws; returns an empty map if pod_cgroup_dir itself can't be
    // opened, or whatever was already matched if an error interrupts iteration partway through.
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
