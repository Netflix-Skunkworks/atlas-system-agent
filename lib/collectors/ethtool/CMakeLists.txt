add_library(ethtool INTERFACE
	src/ethtool.h
)

target_include_directories(ethtool
	INTERFACE ${CMAKE_SOURCE_DIR}
)

target_link_libraries(ethtool
	INTERFACE
    fmt::fmt
    measurement_utils
)

# Add ethtool test executable
add_executable(ethtool_test
    test/ethtool_test.cpp
)

target_link_libraries(ethtool_test
    ethtool
    util
    spectator
    logger
    gtest::gtest
)

# Register the test with CTest
add_test(
    NAME ethtool_test
    COMMAND ethtool_test
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)