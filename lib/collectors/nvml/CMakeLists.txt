add_library(nvml
    src/nvml.cpp
    src/gpumetrics.h
    src/nvml.h
    src/utilization_samples.h
)

target_include_directories(nvml
    PUBLIC ${CMAKE_SOURCE_DIR}
)

# Add dependencies
target_link_libraries(nvml
    fmt::fmt
    absl::time
    absl::synchronization
    spdlog::spdlog
    spectator-registry
    ${CMAKE_DL_LIBS}
)

#Add nvml test executable
add_executable(gpu_test
    test/gpu_test.cpp
)

target_link_libraries(gpu_test
    nvml
    util
    logger
    spectator-registry
    gtest::gtest
)

# Register the test with CTest
add_test(
    NAME gpu_test
    COMMAND gpu_test
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

# These tests need neither an NVIDIA driver nor a GPU.
add_executable(utilization_samples_test test/utilization_samples_test.cpp)
target_include_directories(utilization_samples_test PRIVATE ${CMAKE_SOURCE_DIR})
target_link_libraries(utilization_samples_test GTest::Main)
add_test(NAME utilization_samples_test COMMAND utilization_samples_test)

#-- test_nvml executable
add_executable(test_nvml "test/test_nvml.cpp")
target_link_libraries(test_nvml nvml logger)
