#pragma once

#include <thirdparty/spectator-cpp/spectator/registry.h>
#include <absl/container/flat_hash_map.h>

#include "pod_identity_client.h"

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

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

   protected:
    // For testing access
    static void ScanPodSliceDirectory(const std::filesystem::path& dir, std::string_view name_prefix,
                                       std::string_view name_suffix, PodCgroupMap* pods) noexcept;
    static std::optional<std::string_view> MatchPodSliceName(std::string_view name, std::string_view name_prefix,
                                                               std::string_view name_suffix) noexcept;
    static std::optional<std::string> NormalizePodUid(std::string_view raw_uid) noexcept;
    [[nodiscard]] static PodInfoMap JoinCgroupAndIdentity(const PodCgroupMap& cgroup_pods,
                                                           const std::optional<PodIdentityMap>& identities) noexcept;

   private:
    // Not read anywhere in this class today (identity_client_ is constructed from the
    // constructor's `registry` parameter directly); kept for parity with this codebase's other
    // collectors, which hold their own registry_ to record metrics (see e.g. aws.h).
    Registry* registry_;
    std::string path_prefix_;
    PodIdentityClient identity_client_;
};

}  // namespace atlasagent
