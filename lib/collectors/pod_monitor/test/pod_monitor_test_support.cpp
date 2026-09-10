#include "pod_monitor_test_support.h"

namespace atlasagent::pod_monitor_test
{

namespace
{

// kPod1Uid's cgroup-directory (underscore) spelling, for the same uid the single-pod fixtures use.
constexpr char kPod1Dir[] = "kubepods-pod11111111_1111_1111_1111_111111111111.slice";

// Deliberately not exported: fixture paths outside this file are built from kResources directly or
// from Pod1SlicePath, so no test needs to join a resource path itself.
std::filesystem::path ResourcePath(const std::string_view relative)
{
    return std::filesystem::path(kResources) / std::string(relative);
}

}  // namespace

std::string ContainerId(const char value)
{
    return std::string(64, value);
}

std::string Pod1SlicePath(const std::string_view tree)
{
    return (ResourcePath(tree) / "kubepods.slice" / kPod1Dir).string();
}

ContainerCgroupMap ContainerScopes(const std::string& pod_path,
                                   const std::initializer_list<std::string> container_ids)
{
    ContainerCgroupMap result;
    for (const auto& container_id : container_ids)
    {
        result.emplace(container_id,
                       std::filesystem::path(pod_path) / ("cri-containerd-" + container_id + ".scope"));
    }
    return result;
}

}  // namespace atlasagent::pod_monitor_test
