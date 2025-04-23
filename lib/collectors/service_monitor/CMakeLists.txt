add_library(service_monitor
    src/service_monitor.cpp
    src/service_monitor.h
    src/service_monitor_utils.cpp
    src/service_monitor_utils.h
)

target_include_directories(service_monitor
    PUBLIC ${CMAKE_SOURCE_DIR}
)

# Add dependencies
target_link_libraries(service_monitor
    SDBusCpp::sdbus-c++
    fmt::fmt
    abseil::abseil
    spectator
)

# Add service monitor test executable
add_executable(service_monitor_test
    test/service_monitor_test.cpp
)

target_link_libraries(service_monitor_test
    service_monitor
    util
    logger
    gtest::gtest
)

# Register the test with CTest
add_test(
    NAME service_monitor_test
    COMMAND service_monitor_test
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)