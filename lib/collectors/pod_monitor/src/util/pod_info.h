#pragma once

#include <absl/container/flat_hash_map.h>

#include <filesystem>
#include <string>
#include <unordered_map>

namespace atlasagent
{

// Pod UID (canonical dashed form) -> cgroup path plus any identity fields joined from kubelet's
// local API. Identity fields remain empty when the fetch fails or has no entry for this UID.
struct PodInfo
{
    std::string uid;
    std::filesystem::path cgroup_path;
    std::string name;
    std::string pod_namespace;
    // containerID key produced by PodIdentity::containers -> container name. Empty if
    // identity resolution did not resolve this uid or parsed no usable container status entries.
    std::unordered_map<std::string, std::string> containers;
    // String-valued pod annotations/labels (see PodIdentity::annotations/labels). Empty if identity
    // is unresolved, the fields are absent or malformed, or they contain no string values. Feed
    // ResolvePodTags's fallback chain.
    std::unordered_map<std::string, std::string> annotations;
    std::unordered_map<std::string, std::string> labels;
    // Container NAME -> parsed declared CPU request in cores (see PodIdentity::cpu_requests). A
    // container is absent when it declares no request or its quantity is unparseable; absence makes
    // k8s.cpu.requested omit the gauge rather than publish a fabricated value.
    std::unordered_map<std::string, double> cpu_requests;
};
using PodInfoMap = absl::flat_hash_map<std::string, PodInfo>;

}  // namespace atlasagent
