add_library(ebs
    src/ebs.cpp
    src/ebs.h
)

target_include_directories(ebs
    PUBLIC ${CMAKE_SOURCE_DIR}
)

# Add dependencies
target_link_libraries(ebs
    fmt::fmt
    abseil::abseil
    spectator
    tagging
)