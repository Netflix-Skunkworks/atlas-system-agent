add_library(aws INTERFACE
    src/aws.h
)

target_include_directories(aws
    INTERFACE ${CMAKE_SOURCE_DIR}
)

# Add dependencies
target_link_libraries(aws
    INTERFACE
    fmt::fmt
    rapidjson
)

# Add aws test executable
add_executable(aws_test
    test/aws_test.cc
)

target_link_libraries(aws_test
    aws
    util
    spectator
    measurement_utils
    logger
    gtest::gtest
)

# Register the test with CTest
add_test(
    NAME aws_test
    COMMAND aws_test
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)