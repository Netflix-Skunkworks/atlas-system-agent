add_library(disk
    src/disk.cc
    src/disk.h
)

target_include_directories(disk
    PUBLIC ${CMAKE_SOURCE_DIR}
)

# Add dependencies
target_link_libraries(disk
    fmt::fmt
    abseil::abseil
    spectator
    tagging
)

# Add disk test executable
add_executable(disk_test
    test/disk_test.cc
)

target_link_libraries(disk_test
    disk
    util
    logger
    measurement_utils
    gtest::gtest
)

# Register the test with CTest
add_test(
    NAME disk_test
    COMMAND disk_test
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)