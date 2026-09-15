#pragma once

#include <lib/collectors/pod_monitor/src/util/cgroup_pod_discovery.h>

#include <filesystem>
#include <initializer_list>
#include <string>
#include <string_view>

namespace atlasagent::pod_monitor_test
{

// Centralized fixture root used by tests whose runtime assertions detect a missing or mistyped path.
inline constexpr char kResources[] = "lib/collectors/pod_monitor/test/resources";

// The uid every single-pod fixture uses, in its canonical (dashed) spelling. Its
// cgroup-directory (underscore) spelling is kPod1Dir, private to this header's .cpp.
inline constexpr char kPod1Uid[] = "11111111-1111-1111-1111-111111111111";

[[nodiscard]] std::string ContainerId(char value);

[[nodiscard]] std::string Pod1SlicePath(std::string_view tree);

[[nodiscard]] ContainerCgroupMap ContainerScopes(
    const std::string& pod_path, std::initializer_list<std::string> container_ids);

}  // namespace atlasagent::pod_monitor_test
