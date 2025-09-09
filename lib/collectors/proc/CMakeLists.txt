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
    spectator-registry
)

# Add proc test executable
add_executable(proc_test
    test/proc_test.cpp
)

add_executable(proc_example
    test/proc_example.cpp
)

target_link_libraries(proc_example
    proc
    logger
    spectator-registry
    util
)

target_link_libraries(proc_test
    proc
    logger
    spectator-registry
    util
    gtest::gtest
)

# Register the test with CTest
add_test(
    NAME proc_test
    COMMAND proc_test
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)