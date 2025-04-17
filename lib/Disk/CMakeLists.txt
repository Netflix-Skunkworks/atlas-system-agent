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