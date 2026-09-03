#pragma once

#include <absl/container/flat_hash_map.h>

#include <filesystem>
#include <string>
#include <unordered_map>

namespace atlasagent
{

// Pod UID (canonical dashed form) -> cgroup path plus identity resolved from kubelet's local API.
struct PodInfo
{
    std::string uid;
    std::filesystem::path cgroup_path;
    std::string name;
    std::string pod_namespace;
    // Container id (bare hex) -> container name, from kubelet's
    // status.containerStatuses[] (see PodIdentity::containers). Empty when identity resolution
    // didn't resolve this uid this cycle, or the pod has no containers reported yet.
    std::unordered_map<std::string, std::string> containers;
    // Pod annotations/labels, from kubelet's metadata.annotations/metadata.labels (see
    // PodIdentity::annotations/labels). Empty when identity resolution didn't resolve this uid
    // this cycle, or the pod genuinely has none. Feeds ResolvePodTags's fallback chain.
    std::unordered_map<std::string, std::string> annotations;
    std::unordered_map<std::string, std::string> labels;
};
using PodInfoMap = absl::flat_hash_map<std::string, PodInfo>;

}  // namespace atlasagent
