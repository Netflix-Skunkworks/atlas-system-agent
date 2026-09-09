#include "cgroup_pod_discovery.h"

#include <fmt/format.h>

#include <algorithm>
#include <cctype>
#include <system_error>
#include <utility>

namespace atlasagent
{

namespace
{

using PodCgroupMap = absl::flat_hash_map<std::string, std::filesystem::path>;

bool IsMissing(const std::error_code& ec) noexcept
{
    return ec == std::make_error_code(std::errc::no_such_file_or_directory);
}

enum class ScanErrorKind
{
    kMissing,
    kUnreadable,
};

struct ScanError
{
    ScanErrorKind kind;
    std::filesystem::path path;
    std::error_code cause;
};

using ScanResult = std::expected<void, ScanError>;

ScanError MakeScanError(const std::filesystem::path& path, const std::error_code& cause) noexcept
{
    return ScanError{IsMissing(cause) ? ScanErrorKind::kMissing : ScanErrorKind::kUnreadable, path, cause};
}

CgroupDiscoveryError MakeDiscoveryScanError(const ScanError& error) noexcept
{
    return CgroupDiscoveryError{CgroupDiscoveryErrorKind::kUnreadableScan, error.path, error.cause};
}

std::optional<std::string_view> MatchPodSliceNameImpl(std::string_view name, std::string_view name_prefix,
                                                       std::string_view name_suffix) noexcept
{
    if (name.size() <= name_prefix.size() + name_suffix.size())
    {
        return std::nullopt;
    }
    if (!name.starts_with(name_prefix) || !name.ends_with(name_suffix))
    {
        return std::nullopt;
    }
    return name.substr(name_prefix.size(), name.size() - name_prefix.size() - name_suffix.size());
}

std::optional<std::string> NormalizePodUidImpl(std::string_view raw_uid) noexcept
{
    constexpr size_t kUidLength = 36;
    constexpr size_t kSeparatorPositions[] = {8, 13, 18, 23};
    if (raw_uid.size() != kUidLength)
    {
        return std::nullopt;
    }

    std::string uid{raw_uid};
    for (size_t i = 0; i < uid.size(); ++i)
    {
        bool is_separator_position = std::ranges::contains(kSeparatorPositions, i);
        auto ch = static_cast<unsigned char>(uid[i]);
        if (is_separator_position)
        {
            if (ch != '-' && ch != '_')
            {
                return std::nullopt;
            }
            uid[i] = '-';
        }
        else if (!std::isxdigit(ch) || std::isupper(ch))
        {
            return std::nullopt;
        }
    }
    return uid;
}

ScanResult ScanPodSliceDirectoryImpl(const std::filesystem::path& dir, std::string_view name_prefix,
                                     std::string_view name_suffix, PodCgroupMap& pods) noexcept
{
    std::error_code ec;
    std::filesystem::directory_iterator it{dir, ec};
    if (ec)
    {
        return std::unexpected(MakeScanError(dir, ec));
    }

    const std::filesystem::directory_iterator end{};
    while (it != end)
    {
        const auto& entry = *it;
        std::error_code is_dir_ec;
        if (entry.is_directory(is_dir_ec) && !is_dir_ec)
        {
            auto name = entry.path().filename().string();
            if (auto matched = MatchPodSliceNameImpl(name, name_prefix, name_suffix))
            {
                if (auto uid = NormalizePodUidImpl(*matched))
                {
                    pods.emplace(std::move(*uid), entry.path());
                }
            }
        }
        else if (is_dir_ec && !IsMissing(is_dir_ec))
        {
            return std::unexpected(MakeScanError(entry.path(), is_dir_ec));
        }

        it.increment(ec);
        if (ec)
        {
            return std::unexpected(MakeScanError(dir, ec));
        }
    }
    return {};
}

ScanResult ScanContainersInPodImpl(const std::filesystem::path& pod_cgroup_dir,
                                   ContainerCgroupMap& containers) noexcept
{
    constexpr size_t kMinContainerIdLength = 12;
    std::error_code ec;
    std::filesystem::directory_iterator it{pod_cgroup_dir, ec};
    if (ec)
    {
        return std::unexpected(MakeScanError(pod_cgroup_dir, ec));
    }

    const std::filesystem::directory_iterator end{};
    while (it != end)
    {
        const auto& entry = *it;
        std::error_code is_dir_ec;
        if (entry.is_directory(is_dir_ec) && !is_dir_ec)
        {
            auto name = entry.path().filename().string();
            if (auto matched = MatchPodSliceNameImpl(name, "cri-containerd-", ".scope");
                matched && matched->size() >= kMinContainerIdLength)
            {
                containers.emplace(std::string(*matched), entry.path());
            }
        }
        else if (is_dir_ec && !IsMissing(is_dir_ec))
        {
            return std::unexpected(MakeScanError(entry.path(), is_dir_ec));
        }

        it.increment(ec);
        if (ec)
        {
            return std::unexpected(MakeScanError(pod_cgroup_dir, ec));
        }
    }
    return {};
}

std::expected<bool, std::error_code> InspectRoot(const std::filesystem::path& root) noexcept
{
    std::error_code ec;
    if (std::filesystem::is_directory(root, ec))
    {
        return true;
    }
    if (IsMissing(ec))
    {
        return false;
    }
    if (ec)
    {
        return std::unexpected(ec);
    }
    return std::unexpected(std::make_error_code(std::errc::not_a_directory));
}

}  // namespace

std::optional<std::string_view> CgroupPodDiscovery::MatchPodSliceName(
    std::string_view name, std::string_view name_prefix, std::string_view name_suffix) noexcept
{
    return MatchPodSliceNameImpl(name, name_prefix, name_suffix);
}

std::optional<std::string> CgroupPodDiscovery::NormalizePodUid(std::string_view raw_uid) noexcept
{
    return NormalizePodUidImpl(raw_uid);
}

// The supported systemd layout and the cgroupfs layout Discover() detects but rejects:
//
//   systemd driver                                cgroupfs driver
//   kubepods.slice/                               kubepods/
//   |-- kubepods-pod<uid>.slice/                  |-- pod<uid>/
//   |   `-- cri-containerd-<id>.scope             |   `-- <id>       <-- unsupported
//   `-- kubepods-burstable.slice/                 `-- burstable/
//       `-- kubepods-burstable-pod<uid>.slice/        `-- pod<uid>/
//           `-- cri-containerd-<id>.scope                 `-- <id>   <-- unsupported
//
// BestEffort is shaped exactly like Burstable. Guaranteed-QoS pods have no tier directory, so the
// first systemd-root scan covers them directly. Container scope spelling is runtime-specific; the
// header's matrix documents why only containerd with the systemd driver is accepted.
//
// The UID separator also differs by driver: systemd underscores
// (kubepods-pod11111111_1111_1111_1111_111111111111.slice), cgroupfs dashes
// (pod11111111-1111-1111-1111-111111111111). NormalizePodUid accepts either and normalizes to
// dashes. Discover() does not parse cgroupfs pod directories because it rejects that layout before
// scanning; accepting both separators keeps the parsing helper explicit and independently tested.
CgroupDiscoveryResult CgroupPodDiscovery::Discover() const noexcept
{
    const std::filesystem::path systemd_root = std::filesystem::path(path_prefix_) / "kubepods.slice";
    const std::filesystem::path cgroupfs_root = std::filesystem::path(path_prefix_) / "kubepods";
    const auto systemd_present = InspectRoot(systemd_root);
    if (!systemd_present.has_value())
    {
        return std::unexpected(CgroupDiscoveryError{CgroupDiscoveryErrorKind::kUnreadableRoot,
                                                     systemd_root, systemd_present.error()});
    }

    if (*systemd_present)
    {
        PodCgroupMap pods;
        auto root_scan = ScanPodSliceDirectoryImpl(systemd_root, "kubepods-pod", ".slice", pods);
        if (!root_scan.has_value())
        {
            return std::unexpected(MakeDiscoveryScanError(root_scan.error()));
        }

        for (const auto* qos : {"burstable", "besteffort"})
        {
            auto qos_dir = systemd_root / fmt::format("kubepods-{}.slice", qos);
            auto qos_scan =
                ScanPodSliceDirectoryImpl(qos_dir, fmt::format("kubepods-{}-pod", qos), ".slice", pods);
            // Missing QoS directories are expected; the systemd layout only creates them when used.
            if (!qos_scan.has_value() && qos_scan.error().kind == ScanErrorKind::kUnreadable)
            {
                return std::unexpected(MakeDiscoveryScanError(qos_scan.error()));
            }
        }

        CgroupSnapshot snapshot;
        snapshot.reserve(pods.size());
        for (auto& [uid, pod_path] : pods)
        {
            ContainerCgroupMap containers;
            auto container_scan = ScanContainersInPodImpl(pod_path, containers);
            if (!container_scan.has_value() && container_scan.error().kind == ScanErrorKind::kMissing)
            {
                // Pod removal between the root scan and this per-pod scan is normal lifecycle
                // churn. Omitting the known-stale entry evicts its previously tracked state without
                // suspending unrelated pods.
                continue;
            }
            if (!container_scan.has_value())
            {
                return std::unexpected(MakeDiscoveryScanError(container_scan.error()));
            }
            snapshot.emplace(std::move(uid), CgroupPod{std::move(pod_path), std::move(containers)});
        }

        // Distinguish a genuinely empty hierarchy from the root disappearing while it was being
        // scanned. The latter is a failed observation and must not evict all tracked state as if
        // the node authoritatively had no pods.
        auto final_root_check = InspectRoot(systemd_root);
        if (!final_root_check.has_value())
        {
            return std::unexpected(CgroupDiscoveryError{CgroupDiscoveryErrorKind::kUnreadableScan,
                                                         systemd_root, final_root_check.error()});
        }
        if (!*final_root_check)
        {
            return std::unexpected(CgroupDiscoveryError{
                CgroupDiscoveryErrorKind::kUnreadableScan, systemd_root,
                std::make_error_code(std::errc::no_such_file_or_directory)});
        }

        if (snapshot.empty())
        {
            // Do not let an empty, stale systemd root mask an active cgroupfs layout. With no
            // systemd pods there is no positive evidence for the supported driver, so the presence
            // of the alternative root is treated conservatively as unsupported.
            auto cgroupfs_present = InspectRoot(cgroupfs_root);
            if (!cgroupfs_present.has_value())
            {
                return std::unexpected(CgroupDiscoveryError{CgroupDiscoveryErrorKind::kUnreadableRoot,
                                                             cgroupfs_root, cgroupfs_present.error()});
            }
            if (*cgroupfs_present)
            {
                return std::unexpected(CgroupDiscoveryError{
                    CgroupDiscoveryErrorKind::kUnsupportedCgroupfsLayout, cgroupfs_root, {}});
            }
        }
        return snapshot;
    }

    auto cgroupfs_present = InspectRoot(cgroupfs_root);
    if (!cgroupfs_present.has_value())
    {
        return std::unexpected(CgroupDiscoveryError{CgroupDiscoveryErrorKind::kUnreadableRoot,
                                                     cgroupfs_root, cgroupfs_present.error()});
    }
    if (*cgroupfs_present)
    {
        return std::unexpected(CgroupDiscoveryError{
            CgroupDiscoveryErrorKind::kUnsupportedCgroupfsLayout, cgroupfs_root, {}});
    }
    return std::unexpected(CgroupDiscoveryError{
        CgroupDiscoveryErrorKind::kMissingRoot, std::filesystem::path(path_prefix_),
        std::make_error_code(std::errc::no_such_file_or_directory)});
}

std::string_view ToString(CgroupDiscoveryErrorKind error) noexcept
{
    switch (error)
    {
        case CgroupDiscoveryErrorKind::kUnavailableSource:
            return "unavailable-source";
        case CgroupDiscoveryErrorKind::kMissingRoot:
            return "missing-root";
        case CgroupDiscoveryErrorKind::kUnreadableRoot:
            return "unreadable-root";
        case CgroupDiscoveryErrorKind::kUnreadableScan:
            return "unreadable-scan";
        case CgroupDiscoveryErrorKind::kUnsupportedCgroupfsLayout:
            return "unsupported-cgroupfs-layout";
    }
    return "unknown";
}

}  // namespace atlasagent
