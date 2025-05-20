add_library(ethtool
    src/ethtool.h
    src/ethtool.cpp
)

target_include_directories(ethtool
    PUBLIC ${CMAKE_SOURCE_DIR}
)

target_link_libraries(ethtool
    abseil::abseil
    fmt::fmt
    measurement_utils
    tagging
)

add_executable(ethtool_test
    test/ethtool_test.cpp
)

target_link_libraries(ethtool_test
    ethtool
    gtest::gtest
    logger
    spectator
    util
)

add_test(
    NAME ethtool_test
    COMMAND ethtool_test
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)