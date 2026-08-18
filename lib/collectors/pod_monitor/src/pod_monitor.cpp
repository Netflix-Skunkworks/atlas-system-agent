#include "pod_monitor.h"

#include <fmt/format.h>

#include <cctype>
#include <system_error>

namespace atlasagent
{

std::optional<std::string_view> PodMonitor::MatchPodSliceName(std::string_view name, std::string_view name_prefix,
                                                                std::string_view name_suffix) noexcept
{
    if (name.size() <= name_prefix.size() + name_suffix.size())
    {
        return std::nullopt;
    }

    if (name.substr(0, name_prefix.size()) != name_prefix)
    {
        return std::nullopt;
    }

    if (name.substr(name.size() - name_suffix.size()) != name_suffix)
    {
        return std::nullopt;
    }

    return name.substr(name_prefix.size(), name.size() - name_prefix.size() - name_suffix.size());
}

std::optional<std::string> PodMonitor::NormalizePodUid(std::string_view raw_uid) noexcept
{
    constexpr size_t kUidLength = 36;
    constexpr int kSeparatorPositions[] = {8, 13, 18, 23};

    if (raw_uid.size() != kUidLength)
    {
        return std::nullopt;
    }

    std::string uid{raw_uid};

    for (size_t i = 0; i < uid.size(); ++i)
    {
        bool is_separator_position = false;
        for (int sep : kSeparatorPositions)
        {
            if (static_cast<int>(i) == sep)
            {
                is_separator_position = true;
                break;
            }
        }

        auto ch = static_cast<unsigned char>(uid[i]);

        if (is_separator_position)
        {
            if (ch != '-' && ch != '_')
            {
                return std::nullopt;
            }
            uid[i] = '-';
        }
        else
        {
            if (!std::isxdigit(ch) || std::isupper(ch))
            {
                return std::nullopt;
            }
        }
    }

    return uid;
}

void PodMonitor::ScanPodSliceDirectory(const std::filesystem::path& dir, std::string_view name_prefix,
                                        std::string_view name_suffix, PodCgroupMap* pods) noexcept
{
    std::error_code ec;
    std::filesystem::directory_iterator it{dir, ec};
    if (ec)
    {
        return;
    }

    const std::filesystem::directory_iterator end{};
    while (it != end)
    {
        const auto& entry = *it;

        std::error_code is_dir_ec;
        if (entry.is_directory(is_dir_ec) && !is_dir_ec)
        {
            auto name = entry.path().filename().string();
            if (auto matched = MatchPodSliceName(name, name_prefix, name_suffix))
            {
                if (auto uid = NormalizePodUid(*matched))
                {
                    pods->emplace(std::move(*uid), entry.path());
                }
            }
        }

        it.increment(ec);
        if (ec)
        {
            break;
        }
    }
}

PodCgroupMap PodMonitor::FindAllActivePods() const noexcept
{
    PodCgroupMap pods;
    std::error_code ec;

    std::filesystem::path systemd_root = std::filesystem::path(path_prefix_) / "kubepods.slice";
    if (std::filesystem::is_directory(systemd_root, ec))
    {
        ScanPodSliceDirectory(systemd_root, "kubepods-pod", ".slice", &pods);

        for (const auto* qos : {"burstable", "besteffort"})
        {
            auto qos_dir = systemd_root / fmt::format("kubepods-{}.slice", qos);
            ScanPodSliceDirectory(qos_dir, fmt::format("kubepods-{}-pod", qos), ".slice", &pods);
        }

        return pods;
    }

    std::filesystem::path cgroupfs_root = std::filesystem::path(path_prefix_) / "kubepods";
    if (std::filesystem::is_directory(cgroupfs_root, ec))
    {
        ScanPodSliceDirectory(cgroupfs_root, "pod", "", &pods);

        for (const auto* qos : {"burstable", "besteffort"})
        {
            ScanPodSliceDirectory(cgroupfs_root / qos, "pod", "", &pods);
        }
    }

    return pods;
}

}  // namespace atlasagent
