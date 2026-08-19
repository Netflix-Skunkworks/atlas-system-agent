#include "pod_monitor.h"

#include <lib/logger/src/logger.h>

#include <fmt/format.h>

#include <cctype>
#include <system_error>
#include <unistd.h>
#include <utility>

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

PodInfoMap PodMonitor::JoinCgroupAndIdentity(const PodCgroupMap& cgroup_pods,
                                              const std::optional<PodIdentityMap>& identities) noexcept
{
    PodInfoMap result;
    result.reserve(cgroup_pods.size());
    for (const auto& [uid, cgroup_path] : cgroup_pods)
    {
        PodInfo info{uid, cgroup_path, "", ""};
        if (identities.has_value())
        {
            auto it = identities->find(uid);
            if (it != identities->end())
            {
                info.name = it->second.name;
                info.pod_namespace = it->second.pod_namespace;
            }
        }
        result.emplace(uid, std::move(info));
    }
    return result;
}

PodInfoMap PodMonitor::FindAllActivePods2() const noexcept
{
    auto cgroup_pods = FindAllActivePods();
    auto identities = identity_client_.FetchPodIdentities();
    return JoinCgroupAndIdentity(cgroup_pods, identities);
}

std::unordered_map<std::string, std::string> PodMonitor::BuildPodTags(const PodInfo& info) noexcept
{
    return {
        {"nf.node", info.name},
        {"nf.platform", "k8s"},
        {"k8s.namespace.name", info.pod_namespace},
    };
}

double PodMonitor::ResolveCpuCountForPod(const CGroup& cgroup) noexcept
{
    if (auto quota = cgroup.QuotaCpuCount(); quota.has_value())
    {
        return *quota;
    }
    return static_cast<double>(sysconf(_SC_NPROCESSORS_ONLN));
}

void PodMonitor::RefreshTrackedPods() noexcept
{
    auto discovered = FindAllActivePods2();

    // Pass 1: evict every tracked pod no longer discovered. Erasing the map entry destroys its
    // TrackedPod, which destroys its owned CGroup, freeing every one of its delta-tracking
    // baselines in one step -- there is no separate "reset". Unlike std::unordered_map,
    // absl::flat_hash_map's single-iterator erase() returns void (not the next iterator), so
    // the iterator must be advanced with post-increment before the (now invalidated) copy is
    // erased -- see raw_hash_set.h's own erase() doc comment for this exact idiom.
    for (auto it = tracked_pods_.begin(); it != tracked_pods_.end();)
    {
        if (discovered.find(it->first) == discovered.end())
        {
            tracked_pods_.erase(it++);
        }
        else
        {
            ++it;
        }
    }

    // Pass 2 (fully separate from pass 1): absl::flat_hash_map gives no iterator/reference
    // stability across insertion/erasure, so every try_emplace iterator below is only used
    // within the same loop iteration it was returned in.
    for (const auto& [uid, info] : discovered)
    {
        auto [it, inserted] =
            tracked_pods_.try_emplace(uid, registry_, info.cgroup_path.string(), info.name, info.pod_namespace);
        if (inserted)
        {
            it->second.cgroup.SetExtraTags(BuildPodTags(info));
        }
        // `info.name`/`info.pod_namespace` are only ever both populated together, from a
        // successful identity lookup for this exact uid (see JoinCgroupAndIdentity) -- a
        // nullopt/omitted identity this cycle collapses both to "". Treat that blank pair as
        // "identity unknown this cycle", not as "this pod's identity became blank": self-heal
        // only when the fresh info actually carries a resolved identity, so a transient
        // apiserver/exec-credential failure (FetchPodIdentities() returning nullopt, or simply
        // omitting this uid) never wipes out an already-correctly-tagged pod's name/namespace.
        else if (!info.name.empty() && !info.pod_namespace.empty() &&
                 (it->second.name != info.name || it->second.pod_namespace != info.pod_namespace))
        {
            it->second.name = info.name;
            it->second.pod_namespace = info.pod_namespace;
            it->second.cgroup.SetExtraTags(BuildPodTags(info));
        }

        // Re-resolve every refresh cycle (not only at first insertion): a pod's cgroup
        // directory can appear slightly before kubelet/the CPU manager writes its real cpu.max
        // quota (cpu.max still reads "max" at that instant), and an in-place vertical resize can
        // change an already-tracked pod's quota later in its lifetime. Recomputing here means
        // both cases converge to the correct value on the next refresh instead of sticking with
        // whatever was resolved (possibly the whole node's CPU count) the first time this pod
        // was seen.
        it->second.cgroup.SetCpuCountOverride(ResolveCpuCountForPod(it->second.cgroup));
    }
}

void PodMonitor::CollectCpuStats(const bool fiveSecondMetricsEnabled, const bool sixtySecondMetricsEnabled) noexcept
{
    for (auto& entry : tracked_pods_)
    {
        atlasagent::Logger()->debug("Collecting CPU stats for pod {}/{} (uid={})", entry.second.pod_namespace, entry.second.name, entry.first);
        entry.second.cgroup.PodCpuStats(fiveSecondMetricsEnabled, sixtySecondMetricsEnabled);
    }
}

void PodMonitor::CollectIOStats() noexcept
{
    for (auto& entry : tracked_pods_)
    {
        atlasagent::Logger()->debug("Collecting IO stats for pod {}/{} (uid={})", entry.second.pod_namespace, entry.second.name, entry.first);
        entry.second.cgroup.IOStats();
    }
}

void PodMonitor::CollectMemoryStats() noexcept
{
    RefreshTrackedPods();
    for (auto& entry : tracked_pods_)
    {
        atlasagent::Logger()->debug("Collecting memory stats for pod {}/{} (uid={})", entry.second.pod_namespace, entry.second.name, entry.first);
        entry.second.cgroup.MemoryStatsV2();
        entry.second.cgroup.MemoryStatsStdV2();
    }
}

}  // namespace atlasagent
