add_library(cpufreq INTERFACE
    src/cpufreq.h
)

target_include_directories(cpufreq
    INTERFACE ${CMAKE_SOURCE_DIR}
)

target_link_libraries(cpufreq
    INTERFACE
    fmt::fmt
    tagging
)

# Add cpufreq test executable
add_executable(cpufreq_test
    test/cpufreq_test.cpp
)

target_link_libraries(cpufreq_test
    cpufreq
    util
    spectator
    measurement_utils
    logger
    gtest::gtest
)

# Register the test with CTest
add_test(
    NAME cpufreq_test
    COMMAND cpufreq_test
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)