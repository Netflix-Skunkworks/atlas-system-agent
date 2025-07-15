add_library(disk
    src/disk.cpp
    src/disk.h
)

target_include_directories(disk
    PUBLIC ${CMAKE_SOURCE_DIR}
)

# Add dependencies
target_link_libraries(disk
    abseil::abseil
    fmt::fmt
    monotonic_timer
    spectator-registry
)

# Add disk test executable
add_executable(disk_test
    test/disk_test.cpp
)

target_link_libraries(disk_test
    disk
    util
    logger
    spectator-registry
    gtest::gtest
)

# Register the test with CTest
add_test(
    NAME disk_test
    COMMAND disk_test
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)