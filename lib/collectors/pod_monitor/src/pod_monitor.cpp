#include "pod_monitor.h"

#include <lib/logger/src/logger.h>

#include <fmt/format.h>

#include <cstdlib>
#include <memory>
#include <utility>

namespace atlasagent
{

namespace
{

std::string ResolveK8sClusterEnv() noexcept
{
    const auto* value = std::getenv("K8S_CLUSTER");
    return value != nullptr ? std::string(value) : std::string();
}

std::string DescribeCgroupError(const CgroupDiscoveryError& error)
{
    auto description = fmt::format("kind={}", ToString(error.kind));
    if (!error.path.empty())
    {
        description += fmt::format(" path={}", error.path.string());
    }
    if (error.cause)
    {
        description += fmt::format(" cause={}", error.cause.message());
    }
    return description;
}

}  // namespace

PodMonitor::PodMonitor(Registry* registry, std::string path_prefix, std::string kubelet_url) noexcept
    : PodMonitor(registry, std::make_unique<CgroupPodDiscovery>(std::move(path_prefix)),
                 std::make_unique<PodIdentityClient>(registry, std::move(kubelet_url)), ResolveK8sClusterEnv())
{
}

PodMonitor::PodMonitor(Registry* registry, std::unique_ptr<PodCgroupSource> cgroup_source,
                       std::unique_ptr<PodIdentitySource> identity_source, std::string k8s_cluster) noexcept
    : cgroup_source_(std::move(cgroup_source)),
      identity_source_(std::move(identity_source)),
      k8s_cluster_(std::move(k8s_cluster)),
      tracked_registry_(registry)
{
}

PodRefreshResult PodMonitor::Refresh() noexcept
{
    if (!cgroup_source_)
    {
        tracked_registry_.Suspend();
        Logger()->warn("Pod monitor suspended: no cgroup source configured");
        return PodRefreshResult{PodMonitorState::kCgroupUnavailable,
                                CgroupDiscoveryError{CgroupDiscoveryErrorKind::kUnavailableSource, {}, {}},
                                std::nullopt, {}};
    }

    auto cgroups = cgroup_source_->Discover();
    if (!cgroups.has_value())
    {
        tracked_registry_.Suspend();
        Logger()->warn("Pod monitor suspended: cgroup discovery failed ({})", DescribeCgroupError(cgroups.error()));
        return PodRefreshResult{PodMonitorState::kCgroupUnavailable, cgroups.error(), std::nullopt, {}};
    }

    if (!identity_source_)
    {
        tracked_registry_.Suspend();
        PodIdentityError error{PodIdentityErrorKind::UnavailableSource};
        Logger()->warn("Pod monitor suspended: no identity source configured");
        return PodRefreshResult{PodMonitorState::kIdentityUnavailable, std::nullopt, error, {}};
    }

    auto identities = identity_source_->FetchPodIdentities();
    if (!identities.has_value())
    {
        tracked_registry_.Suspend();
        Logger()->warn("Pod monitor suspended: identity fetch failed ({})", ToString(identities.error().kind));
        return PodRefreshResult{PodMonitorState::kIdentityUnavailable, std::nullopt, identities.error(), {}};
    }

    auto active_pods = BuildActivePods(*cgroups, *identities, k8s_cluster_);
    const auto admitted_pods = active_pods.size();
    const auto admitted_containers = CountActiveContainers(active_pods);
    tracked_registry_.Reconcile(active_pods);
    Logger()->debug("Pod monitor refreshed: cgroup_pods={} identity_pods={} admitted_pods={} admitted_containers={}",
                    cgroups->size(), identities->size(), admitted_pods, admitted_containers);
    return PodRefreshResult{PodMonitorState::kActive, std::nullopt, std::nullopt, std::move(active_pods)};
}

}  // namespace atlasagent
