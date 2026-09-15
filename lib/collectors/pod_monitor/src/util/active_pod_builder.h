#pragma once

#include <absl/container/flat_hash_map.h>

#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <unordered_map>

#include "cgroup_pod_discovery.h"
#include "pod_identity_client.h"

namespace atlasagent
{

// A container that is safe for the metric emitter to track. Its key in ActivePod::containers is the
// runtime container id used to join cgroups and kubelet status.
struct ActiveContainer
{
    std::filesystem::path cgroup_path;
    std::string name;
    std::optional<double> cpu_request;
};

using ActiveContainerMap = absl::flat_hash_map<std::string, ActiveContainer>;

// The complete admitted identity and non-empty container set for one pod. This is intentionally
// independent of the input snapshot: callers can apply it after the source maps have been discarded.
struct ActivePod
{
    std::string name;
    std::string pod_namespace;
    std::unordered_map<std::string, std::string> tags;
    ActiveContainerMap containers;
};

using ActivePodMap = absl::flat_hash_map<std::string, ActivePod>;

// Returns the cgroup-discovered pods and containers whose current kubelet identity can be confirmed.
// Entries that cannot be admitted are logged at debug level and are not retained. A pod must have a
// non-empty name and namespace and pass ResolvePodTags(); each container must have a matching runtime
// id and non-empty name. Existing tag fallback and nf.cluster semantics are preserved.
[[nodiscard]] ActivePodMap BuildActivePods(const CgroupSnapshot& cgroups, const PodIdentityMap& identities,
                                            const std::string& k8s_cluster) noexcept;

[[nodiscard]] std::size_t CountActiveContainers(const ActivePodMap& active_pods) noexcept;

}  // namespace atlasagent
