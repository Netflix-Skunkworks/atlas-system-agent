add_library(util
    src/util.cc
    src/util.h
)

target_include_directories(util
    PUBLIC ${CMAKE_SOURCE_DIR}
)

# Add any dependencies if needed
target_link_libraries(util
    fmt::fmt
    abseil::abseil
    spectator
)

# Add utils test executable
add_executable(utils_test
    test/utils_test.cc
)

target_link_libraries(utils_test
    util
    logger
    gtest::gtest
)

# Register the test with CTest
add_test(
    NAME utils_test
    COMMAND utils_test
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)