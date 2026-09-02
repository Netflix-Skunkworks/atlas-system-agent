#include "pod_monitor.h"

#include <lib/logger/src/logger.h>

#include <fmt/format.h>

#include <cctype>
#include <cstdlib>
#include <system_error>
#include <unistd.h>
#include <utility>

namespace atlasagent
{

namespace
{

// A key present in `values` with a non-empty value; nullopt otherwise (missing, or present but
// empty -- both treated as "not set" per the fallback-chain wording).
std::optional<std::string> NonEmptyValue(const std::unordered_map<std::string, std::string>& values,
                                          std::string_view key) noexcept
{
    auto it = values.find(std::string(key));
    if (it != values.end() && !it->second.empty())
    {
        return it->second;
    }
    return std::nullopt;
}

std::string ResolveK8sClusterEnv() noexcept
{
    const auto* value = std::getenv("K8S_CLUSTER");
    return value != nullptr ? std::string(value) : std::string();
}

}  // namespace

PodMonitor::PodMonitor(Registry* registry, std::string path_prefix, std::string kubelet_url) noexcept
    : registry_(registry), path_prefix_(std::move(path_prefix)),
      identity_client_(registry, std::move(kubelet_url)), k8s_cluster_(ResolveK8sClusterEnv())
{
}

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

ContainerCgroupMap PodMonitor::FindContainersInPod(const std::filesystem::path& pod_cgroup_dir) noexcept
{
    ContainerCgroupMap containers;

    // Lighter validation than NormalizePodUid's strict UUID check -- a runtime-assigned
    // container id is an arbitrary hex string, not a UUID, so just require a plausible
    // non-trivial length rather than an exact one.
    constexpr size_t kMinContainerIdLength = 12;

    std::error_code ec;
    std::filesystem::directory_iterator it{pod_cgroup_dir, ec};
    if (ec)
    {
        return containers;
    }

    const std::filesystem::directory_iterator end{};
    while (it != end)
    {
        const auto& entry = *it;

        std::error_code is_dir_ec;
        if (entry.is_directory(is_dir_ec) && !is_dir_ec)
        {
            auto name = entry.path().filename().string();
            if (auto matched = MatchPodSliceName(name, "cri-containerd-", ".scope");
                matched && !matched->empty() && matched->size() >= kMinContainerIdLength)
            {
                containers.emplace(std::string(*matched), entry.path());
            }
        }

        it.increment(ec);
        if (ec)
        {
            break;
        }
    }

    return containers;
}

PodInfoMap PodMonitor::JoinCgroupAndIdentity(const PodCgroupMap& cgroup_pods,
                                              const std::optional<PodIdentityMap>& identities) noexcept
{
    PodInfoMap result;
    result.reserve(cgroup_pods.size());
    for (const auto& [uid, cgroup_path] : cgroup_pods)
    {
        PodInfo info{uid, cgroup_path, "", "", {}, {}, {}};
        if (identities.has_value())
        {
            auto it = identities->find(uid);
            if (it != identities->end())
            {
                info.name = it->second.name;
                info.pod_namespace = it->second.pod_namespace;
                info.containers = it->second.containers;
                info.annotations = it->second.annotations;
                info.labels = it->second.labels;
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

std::optional<std::unordered_map<std::string, std::string>> PodMonitor::ResolvePodTags(
    const std::unordered_map<std::string, std::string>& annotations,
    const std::unordered_map<std::string, std::string>& labels, const std::string& pod_name,
    const std::string& k8s_cluster) noexcept
{
    auto primary_app = NonEmptyValue(annotations, PodTagKeys::kAnnotationApp);
    auto primary_stack = NonEmptyValue(annotations, PodTagKeys::kAnnotationStack);
    auto primary_detail = NonEmptyValue(annotations, PodTagKeys::kAnnotationDetail);

    auto nf_app = primary_app;
    if (!nf_app.has_value())
    {
        for (auto key : {PodTagKeys::kLabelAppName, PodTagKeys::kLabelK8sApp, PodTagKeys::kLabelApp})
        {
            if (auto value = NonEmptyValue(labels, key); value.has_value())
            {
                nf_app = std::move(value);
                break;
            }
        }
    }

    auto nf_stack = primary_stack;
    if (!nf_stack.has_value())
    {
        nf_stack = NonEmptyValue(labels, PodTagKeys::kLabelAppInstance);
    }

    auto nf_detail = primary_detail;
    if (!nf_detail.has_value())
    {
        nf_detail = NonEmptyValue(labels, PodTagKeys::kLabelAppComponent);
    }

    if (!nf_app.has_value() && !nf_stack.has_value() && !nf_detail.has_value())
    {
        // Gating: none of the three identity-bearing keys resolved (annotation or label
        // fallback) -- no metrics for any container in this pod. nf.node/nf.process are
        // deliberately NOT part of this check -- they're always structurally available once a
        // pod's identity resolves at all, so including them would make Gating vacuous.
        return std::nullopt;
    }

    // nf.cluster is gated on the *primary* netflix.com/app annotation specifically -- NOT on
    // whatever nf_app ended up resolving to. A pod whose nf.app only resolved via a label
    // fallback must not get an nf.cluster tag; this mirrors the Netflix K8s-native observability
    // design's own `where resource.attributes["netflix.app"] != nil` guards, which all key off
    // the primary attribute, not the transform's own output.
    std::optional<std::string> nf_cluster;
    if (primary_app.has_value())
    {
        std::string cluster = *primary_app;
        if (primary_stack.has_value())
        {
            cluster += "-";
            cluster += *primary_stack;
        }
        if (primary_detail.has_value())
        {
            cluster += "-";
            cluster += *primary_detail;
        }
        nf_cluster = std::move(cluster);
    }

    std::unordered_map<std::string, std::string> tags;
    if (nf_app.has_value())
    {
        tags.emplace("nf.app", *nf_app);
    }
    if (nf_stack.has_value())
    {
        tags.emplace("nf.stack", *nf_stack);
    }
    if (nf_detail.has_value())
    {
        tags.emplace("nf.detail", *nf_detail);
    }
    if (nf_cluster.has_value())
    {
        tags.emplace("nf.cluster", *nf_cluster);
    }
    if (!pod_name.empty())
    {
        tags.emplace("nf.node", pod_name);
    }
    if (!k8s_cluster.empty())
    {
        tags.emplace("k8s.cluster.name", k8s_cluster);
    }
    tags.emplace("nf.platform", "k8s");

    return tags;
}

double PodMonitor::ResolveCpuCountForPod(const CGroup& cgroup) noexcept
{
    if (auto quota = cgroup.QuotaCpuCount(); quota.has_value())
    {
        return *quota;
    }
    return static_cast<double>(sysconf(_SC_NPROCESSORS_ONLN));
}

void PodMonitor::EvictUntrackedPods(const PodInfoMap& discovered) noexcept
{
    // Unlike std::unordered_map, absl::flat_hash_map's single-iterator erase() returns void (not
    // the next iterator), so the iterator must be advanced with post-increment before the (now
    // invalidated) copy is erased -- see raw_hash_set.h's own erase() doc comment for this exact
    // idiom.
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
}

TrackedPod& PodMonitor::UpsertPodIdentity(const std::string& uid, const PodInfo& info) noexcept
{
    auto [it, inserted] = tracked_pods_.try_emplace(uid, info.name, info.pod_namespace);
    if (!inserted && !info.name.empty() && !info.pod_namespace.empty() &&
        (it->second.name != info.name || it->second.pod_namespace != info.pod_namespace))
    {
        it->second.name = info.name;
        it->second.pod_namespace = info.pod_namespace;
    }
    return it->second;
}

void PodMonitor::EvictUntrackedContainers(TrackedPod& pod, const ContainerCgroupMap& discovered_containers) noexcept
{
    for (auto cit = pod.containers.begin(); cit != pod.containers.end();)
    {
        if (discovered_containers.find(cit->first) == discovered_containers.end())
        {
            pod.containers.erase(cit++);
        }
        else
        {
            ++cit;
        }
    }
}

void PodMonitor::ReconcileContainers(TrackedPod& pod, const PodInfo& info,
                                      const ContainerCgroupMap& discovered_containers,
                                      const std::unordered_map<std::string, std::string>& pod_tags) noexcept
{
    for (const auto& [container_id, container_cgroup_path] : discovered_containers)
    {
        auto container_name_it = info.containers.find(container_id);
        if (container_name_it == info.containers.end())
        {
            // Not ready yet -- expected transient race: a container's cgroup scope can appear
            // slightly before kubelet reports it in containerStatuses (or vice versa). Skip this
            // cycle without evicting it if it was already tracked.
            continue;
        }
        const std::string& container_name = container_name_it->second;

        auto container_tags = pod_tags;
        container_tags["nf.process"] = container_name;

        auto [cit, container_inserted] =
            pod.containers.try_emplace(container_id, registry_, container_cgroup_path.string(), container_id, container_name);
        if (!container_inserted)
        {
            cit->second.container_name = container_name;
        }
        cit->second.cgroup.SetExtraTags(std::move(container_tags));

        // Re-resolve every refresh cycle (not only at first insertion) -- a container's cgroup
        // directory can appear slightly before cpu.max is set to its real quota, and an in-place
        // resize can change it later.
        cit->second.cgroup.SetCpuCountOverride(ResolveCpuCountForPod(cit->second.cgroup));
    }
}

void PodMonitor::RefreshTrackedPods() noexcept
{
    auto discovered = FindAllActivePods2();
    EvictUntrackedPods(discovered);

    for (const auto& [uid, info] : discovered)
    {
        TrackedPod& pod = UpsertPodIdentity(uid, info);

        // Resolve this pod's tags ONCE -- annotations/labels are pod-level, not per-container,
        // so every container in this pod shares the same nf.app/nf.stack/nf.detail/nf.cluster.
        // This is also the single Gating decision for every container below (see
        // ResolvePodTags's own doc comment for why nf.node/nf.process are excluded from it).
        auto pod_tags = ResolvePodTags(info.annotations, info.labels, pod.name, k8s_cluster_);
        auto discovered_containers = FindContainersInPod(info.cgroup_path);
        EvictUntrackedContainers(pod, discovered_containers);

        if (!pod_tags.has_value())
        {
            // Gating: this pod's identity didn't resolve -- no metrics for ANY of its
            // containers. Evict everything still tracked (a pod can lose its resolved identity
            // across a relabel/rollout, the same way a single container losing its tags could
            // under the superseded per-container design).
            pod.containers.clear();
            continue;
        }

        if (!pod.pod_namespace.empty())
        {
            (*pod_tags)["k8s.namespace.name"] = pod.pod_namespace;
        }

        ReconcileContainers(pod, info, discovered_containers, *pod_tags);
    }
}

void PodMonitor::CollectCpuStats(const bool fiveSecondMetricsEnabled, const bool sixtySecondMetricsEnabled) noexcept
{
    for (auto& pod_entry : tracked_pods_)
    {
        for (auto& container_entry : pod_entry.second.containers)
        {
            atlasagent::Logger()->debug("Collecting CPU stats for pod {}/{} (uid={}) container {} ({})",
                                         pod_entry.second.pod_namespace, pod_entry.second.name, pod_entry.first,
                                         container_entry.first, container_entry.second.container_name);
            container_entry.second.cgroup.PodCpuStats(fiveSecondMetricsEnabled, sixtySecondMetricsEnabled);
        }
    }
}

void PodMonitor::CollectIOStats() noexcept
{
    for (auto& pod_entry : tracked_pods_)
    {
        for (auto& container_entry : pod_entry.second.containers)
        {
            atlasagent::Logger()->debug("Collecting IO stats for pod {}/{} (uid={}) container {} ({})",
                                         pod_entry.second.pod_namespace, pod_entry.second.name, pod_entry.first,
                                         container_entry.first, container_entry.second.container_name);
            container_entry.second.cgroup.IOStats();
        }
    }
}

void PodMonitor::CollectMemoryStats() noexcept
{
    RefreshTrackedPods();
    for (auto& pod_entry : tracked_pods_)
    {
        for (auto& container_entry : pod_entry.second.containers)
        {
            atlasagent::Logger()->debug("Collecting memory stats for pod {}/{} (uid={}) container {} ({})",
                                         pod_entry.second.pod_namespace, pod_entry.second.name, pod_entry.first,
                                         container_entry.first, container_entry.second.container_name);
            container_entry.second.cgroup.MemoryStatsV2();
            container_entry.second.cgroup.MemoryStatsStdV2();
        }
    }
}

}  // namespace atlasagent
