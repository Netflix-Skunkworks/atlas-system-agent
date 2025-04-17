add_library(dcgm
    src/dcgm_stats.cc
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
)