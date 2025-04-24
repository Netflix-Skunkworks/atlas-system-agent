add_library(pressurestall INTERFACE
    src/pressure_stall.h
)

target_include_directories(pressurestall
    INTERFACE ${CMAKE_SOURCE_DIR}
)

target_link_libraries(pressurestall
    INTERFACE
    fmt::fmt
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