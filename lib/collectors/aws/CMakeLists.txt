add_library(aws
    src/aws.h
    src/aws.cpp
)

target_include_directories(aws
    PUBLIC ${CMAKE_SOURCE_DIR}
)

# Add dependencies
target_link_libraries(aws
    PUBLIC
    abseil::abseil
    fmt::fmt
    rapidjson
    spectator-registry
    HttpClient
    logger
)

# Add aws test executable
add_executable(aws_test
    test/aws_test.cpp
)

target_link_libraries(aws_test
    aws
    util
    spectator-registry
    logger
    gtest::gtest
)

# Register the test with CTest
add_test(
    NAME aws_test
    COMMAND aws_test
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)