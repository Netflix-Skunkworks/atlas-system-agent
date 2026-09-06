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
    // Container id (bare hex) -> container name (see PodIdentity::containers). Empty if identity
    // resolution didn't resolve this uid this cycle, or the pod has no containers reported yet.
    std::unordered_map<std::string, std::string> containers;
    // Pod annotations/labels (see PodIdentity::annotations/labels). Empty if unresolved this
    // cycle, or the pod genuinely has none. Feed ResolvePodTags's fallback chain.
    std::unordered_map<std::string, std::string> annotations;
    std::unordered_map<std::string, std::string> labels;
    // Container NAME -> declared CPU request in cores (see PodIdentity::cpu_requests). A container
    // is absent when it declares no request, which is NOT the same as a request of zero -- the
    // absence is what makes k8s.cpu.requested omit the gauge rather than publish a wrong value.
    std::unordered_map<std::string, double> cpu_requests;
};
using PodInfoMap = absl::flat_hash_map<std::string, PodInfo>;

}  // namespace atlasagent
