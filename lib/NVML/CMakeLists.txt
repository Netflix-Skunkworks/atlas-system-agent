add_library(nvml
    src/nvml.cc
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
)