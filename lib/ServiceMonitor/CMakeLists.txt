add_library(service_monitor
    src/service_monitor.cc
    src/service_monitor.h
    src/service_monitor_utils.cc
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
)