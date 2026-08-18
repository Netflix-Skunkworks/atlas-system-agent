add_library(pod_monitor
    src/pod_monitor.h
    src/pod_monitor.cpp
)

target_include_directories(pod_monitor
    PUBLIC ${CMAKE_SOURCE_DIR}
)

target_link_libraries(pod_monitor
    abseil::abseil
    fmt::fmt
    spectator-registry
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

# Standalone debug tool for manually exercising FindAllActivePods() on a real node;
# not part of the atlas_system_agent binary and not run by ctest.
add_executable(pod_monitor_example
    bin/example.cpp
)

target_link_libraries(pod_monitor_example
    pod_monitor
    fmt::fmt
    spectator-registry
)
