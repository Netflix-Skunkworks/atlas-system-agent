add_library(proc
    src/proc.cpp
    src/proc.h
)

target_include_directories(proc
    PUBLIC ${CMAKE_SOURCE_DIR}
)

# Add dependencies
target_link_libraries(proc
    fmt::fmt
    abseil::abseil
    spectator
)