add_library(perfmetrics INTERFACE
	src/perfmetrics.h
)

target_include_directories(perfmetrics
	INTERFACE ${CMAKE_SOURCE_DIR}
)

target_link_libraries(perfmetrics
  	INTERFACE
    fmt::fmt
)

# Add perfmetrics test executable
add_executable(perfmetrics_test
    test/perfmetrics_test.cpp
)

target_link_libraries(perfmetrics_test
    perfmetrics
    util
    spectator
    measurement_utils
    logger
    gtest::gtest
)

# Register the test with CTest
add_test(
    NAME perfmetrics_test
    COMMAND perfmetrics_test
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)