add_library(pod_monitor
    src/pod_monitor.h
    src/pod_monitor.cpp
    src/util/pod_identity_client.h
    src/util/pod_identity_client.cpp
    src/util/pod_info.h
    src/util/cgroup_pod_discovery.h
    src/util/cgroup_pod_discovery.cpp
    src/util/pod_tag_resolver.h
    src/util/pod_tag_resolver.cpp
    src/util/tracked_pod_registry.h
    src/util/tracked_pod_registry.cpp
)

target_include_directories(pod_monitor
    PUBLIC ${CMAKE_SOURCE_DIR}
)

target_link_libraries(pod_monitor
    abseil::abseil
    cgroup
    fmt::fmt
    spectator-registry
    HttpClient
    rapidjson
    util
)

add_executable(pod_monitor_test
    test/pod_monitor_test.cpp
)

target_link_libraries(pod_monitor_test
    pod_monitor
    gtest::gtest
    logger
    spectator-registry
)

add_test(
    NAME pod_monitor_test
    COMMAND pod_monitor_test
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

# Standalone debug tool for manually exercising FindActivePodInfo() (cgroup discovery
# plus a live call to kubelet's own local API for identity resolution) on a real node;
# not part of the atlas_system_agent binary and not run by ctest.
add_executable(find-activepods
    bin/find-activepods.cpp
)

target_link_libraries(find-activepods
    pod_monitor
    fmt::fmt
    spectator-registry
)

# Standalone debug tool: a trimmed copy of AtlasAgent/src/k8s-agent.cpp's polling loop that
# constructs only PodMonitor and stops after two 60-second cadence intervals, for exercising
# PodMonitor's real cadence wiring by hand without the rest of k8s-agent.cpp's collectors; not
# part of the atlas_system_agent binary and not run by ctest.
add_executable(mock-agent
    bin/mock-agent.cpp
)

target_link_libraries(mock-agent
    pod_monitor
    fmt::fmt
    spectator-registry
)
