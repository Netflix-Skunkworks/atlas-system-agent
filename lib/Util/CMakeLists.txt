add_library(util
    src/util.cc
    src/util.h
)

target_include_directories(util
    PUBLIC ${CMAKE_SOURCE_DIR}
)

# Add any dependencies if needed
target_link_libraries(util
    fmt::fmt
    abseil::abseil
)