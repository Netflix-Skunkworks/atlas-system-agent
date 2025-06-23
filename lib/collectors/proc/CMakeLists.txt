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

# Add proc test executable
add_executable(proc_test
    test/proc_test.cpp
)

target_link_libraries(proc_test
    proc
    logger
    util
    gtest::gtest
)

# Register the test with CTest
add_test(
    NAME proc_test
    COMMAND proc_test
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)