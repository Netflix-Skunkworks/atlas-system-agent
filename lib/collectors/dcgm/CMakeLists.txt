add_library(dcgm
    src/dcgm_stats.cpp
    src/dcgm_stats.h
)

target_include_directories(dcgm
    PUBLIC ${CMAKE_SOURCE_DIR}
)

# Add dependencies
target_link_libraries(dcgm
    fmt::fmt
    abseil::abseil
    spectator
    tagging
)

# Add dcgm test executable
# add_executable(dcgm_stats_test
#     test/dcgm_stats_test.cpp
# )

# target_link_libraries(dcgm_stats_test
#     dcgm
#     util
#     logger
#     spectator
#     measurement_utils
#     gtest::gtest
# )

# # Register the test with CTest
# add_test(
#     NAME dcgm_stats_test
#     COMMAND dcgm_stats_test
#     WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
# )