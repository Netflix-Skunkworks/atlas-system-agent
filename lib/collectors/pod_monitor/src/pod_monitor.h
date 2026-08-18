#pragma once

#include <thirdparty/spectator-cpp/spectator/registry.h>
#include <absl/container/flat_hash_map.h>

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace atlasagent
{

// Pod UID (canonical dashed form) -> that pod's cgroup v2 directory.
using PodCgroupMap = absl::flat_hash_map<std::string, std::filesystem::path>;

class PodMonitor
{
   public:
    explicit PodMonitor(Registry* registry, std::string path_prefix = "/sys/fs/cgroup") noexcept
        : registry_(registry), path_prefix_(std::move(path_prefix))
    {
    }

    // Discovers every pod-level cgroup directory on this node, keyed by pod UID in
    // canonical (dashed) form. Detects the cgroup v2 driver (systemd vs cgroupfs) fresh
    // on each call, and walks at most two levels deep, so it only ever finds pod-aggregate
    // cgroups, never per-container leaves.
    [[nodiscard]] PodCgroupMap FindAllActivePods() const noexcept;

    void SetPrefix(std::string new_prefix) noexcept { path_prefix_ = std::move(new_prefix); }

   protected:
    // For testing access
    static void ScanPodSliceDirectory(const std::filesystem::path& dir, std::string_view name_prefix,
                                       std::string_view name_suffix, PodCgroupMap* pods) noexcept;
    static std::optional<std::string_view> MatchPodSliceName(std::string_view name, std::string_view name_prefix,
                                                               std::string_view name_suffix) noexcept;
    static std::optional<std::string> NormalizePodUid(std::string_view raw_uid) noexcept;

   private:
    // Not read by FindAllActivePods() itself; stored for methods added in a later increment
    // (e.g. per-pod CGroup construction/metric emission needs it).
    [[maybe_unused]] Registry* registry_;
    std::string path_prefix_;
};

}  // namespace atlasagent
