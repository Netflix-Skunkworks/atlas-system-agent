add_library(cgroup
    src/cgroup.cpp
    src/cgroup.h
)

target_include_directories(cgroup
    PUBLIC ${CMAKE_SOURCE_DIR}
)

# Add dependencies
target_link_libraries(cgroup
    fmt::fmt
    abseil::abseil

)