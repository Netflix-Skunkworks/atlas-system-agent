add_library(perf_metrics
    src/perf_metrics.h
    src/perf_metrics.cpp
)

target_include_directories(perf_metrics
    PUBLIC ${CMAKE_SOURCE_DIR}
)

target_link_libraries(perf_metrics
    PUBLIC
    fmt::fmt
    tagging
)

# add_executable(perf_metrics_test
#     test/perf_metrics_test.cpp
# )

# target_link_libraries(perf_metrics_test
#     perf_metrics
#     util
#     spectator
#     measurement_utils
#     logger
#     gtest::gtest
# )

# add_test(
#     NAME perf_metrics_test
#     COMMAND perf_metrics_test
#     WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
# )