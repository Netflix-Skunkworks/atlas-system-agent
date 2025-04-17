add_library(measurement_utils
    src/measurement_utils.cc
    src/measurement_utils.h
)

# Configure include paths to support #include <MeasurementUtils/...> syntax
target_include_directories(measurement_utils
    PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/..>
)

# Add dependencies - including abseil for hash functions
target_link_libraries(measurement_utils
    fmt::fmt
    abseil::abseil
    gtest::gtest
)