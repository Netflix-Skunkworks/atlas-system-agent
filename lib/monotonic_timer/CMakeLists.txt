add_library(monotonic_timer
    src/monotonic_timer.h
    src/monotonic_timer.cpp
)

target_include_directories(monotonic_timer
    PUBLIC ${CMAKE_SOURCE_DIR}
)

target_link_libraries(monotonic_timer
    PUBLIC
    fmt::fmt
    spectator
    tagging
)

# Add monotonic_timer test executable
add_executable(monotonic_timer_test
    test/monotonic_timer_test.cpp
)

target_link_libraries(monotonic_timer_test
    monotonic_timer
    util
    logger
    spectator
    measurement_utils
    gtest::gtest
)

# Register the test with CTest
add_test(
    NAME monotonic_timer_test
    COMMAND monotonic_timer_test
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)