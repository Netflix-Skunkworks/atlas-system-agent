add_library(nvml
    src/nvml.cpp
    src/gpumetrics.h
    src/nvml.h
)

target_include_directories(nvml
    PUBLIC ${CMAKE_SOURCE_DIR}
)

# Add dependencies
target_link_libraries(nvml
    fmt::fmt
    absl::time
    spdlog::spdlog
    spectator
)

# Add nvml test executable
# add_executable(gpu_test
#     test/gpu_test.cpp
# )

# target_link_libraries(gpu_test
#     nvml
#     util
#     logger
#     spectator
#     measurement_utils
#     gtest::gtest
# )

# # Register the test with CTest
# add_test(
#     NAME gpu_test
#     COMMAND gpu_test
#     WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
# )

# #-- test_nvml executable
# add_executable(test_nvml "test/test_nvml.cpp")
# target_link_libraries(test_nvml nvml logger)