add_library(measurement_utils
    src/measurement_utils.cpp
    src/measurement_utils.h
)


target_include_directories(measurement_utils
    PUBLIC ${CMAKE_SOURCE_DIR}
    )

# Add dependencies - including abseil for hash functions
target_link_libraries(measurement_utils
    fmt::fmt
    abseil::abseil
    gtest::gtest
    spectator
)