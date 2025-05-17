add_library(perfmetrics
    src/perfmetrics.h
    src/perfmetrics.cpp
)

target_include_directories(perfmetrics
    PUBLIC ${CMAKE_SOURCE_DIR}
)

target_link_libraries(perfmetrics
    PUBLIC
    fmt::fmt
    tagging
)

# Add perfmetrics test executable
add_executable(perfmetrics_test
    test/perfmetrics_test.cpp
)

target_link_libraries(perfmetrics_test
    perfmetrics
    util
    spectator
    measurement_utils
    logger
    gtest::gtest
)

# Register the test with CTest
add_test(
    NAME perfmetrics_test
    COMMAND perfmetrics_test
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)