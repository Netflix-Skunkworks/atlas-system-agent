add_library(cgroup
    src/cgroup.cpp
    src/cgroup.h
)

target_include_directories(cgroup
    PUBLIC ${CMAKE_SOURCE_DIR}
)

# Add dependencies
target_link_libraries(cgroup
    fmt::fmt
    abseil::abseil
    registry
)

# Add cgroup test executable
add_executable(cgroup_test
    test/cgroup_test.cpp
)

target_link_libraries(cgroup_test
    cgroup
    util
    registry
    logger
    gtest::gtest
)

# Register the test with CTest
add_test(
    NAME cgroup_test
    COMMAND cgroup_test
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)