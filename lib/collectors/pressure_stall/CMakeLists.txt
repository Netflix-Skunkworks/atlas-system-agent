add_library(pressurestall
    src/pressure_stall.h
    src/pressure_stall.cpp
)

target_include_directories(pressurestall
    PUBLIC ${CMAKE_SOURCE_DIR}
)

target_link_libraries(pressurestall
    abseil::abseil
    fmt::fmt
    tagging
)

# Add pressure stall test executable
add_executable(pressure_stall_test
    test/pressure_stall_test.cpp
)

target_link_libraries(pressure_stall_test
    pressurestall
    util
    logger
    measurement_utils
    spectator
    abseil::abseil
    gtest::gtest
)

# Register the test with CTest
add_test(
    NAME pressure_stall_test
    COMMAND pressure_stall_test
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)