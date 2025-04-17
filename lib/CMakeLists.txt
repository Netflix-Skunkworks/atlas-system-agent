#-- spectator-cpp library
add_library(spectator
    "spectator/logger.cc"
    "spectator/publisher.cc"
    "spectator/config.h"
    "spectator/id.h"
    "spectator/logger.h"
    "spectator/measurement.h"
    "spectator/meter_type.h"
    "spectator/publisher.h"
    "spectator/registry.h"
    "spectator/stateful_meters.h"
    "spectator/stateless_meters.h"
    "spectator/valid_chars.inc"
)

#-- generator tools
add_executable(gen_valid_chars "tools/gen_valid_chars.cc")

#-- file generators, must exist where the outputs are referenced
set(VALID_CHARS_OUTPUT "${CMAKE_CURRENT_SOURCE_DIR}/spectator/valid_chars.inc")
add_custom_command(
    OUTPUT ${VALID_CHARS_OUTPUT}
    COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_CURRENT_SOURCE_DIR}/spectator"
    COMMAND "$<TARGET_FILE:gen_valid_chars>" > ${VALID_CHARS_OUTPUT}
    DEPENDS gen_valid_chars
    COMMENT "Generating valid_chars.inc"
    VERBATIM
)

# Create a custom target to guarantee the file gets created
add_custom_target(generate_valid_chars ALL
    DEPENDS ${VALID_CHARS_OUTPUT}
)

# Make spectator depend on the file generation
add_dependencies(spectator generate_valid_chars)

target_link_libraries(spectator
    abseil::abseil
    asio::asio
    Backward::Backward
    fmt::fmt
    spdlog::spdlog
)

add_subdirectory(Util)
add_subdirectory(Tagging)
add_subdirectory(MeasurementUtils)
add_subdirectory(HttpClient)
add_subdirectory(CGroup)
add_subdirectory(ServiceMonitor)
add_subdirectory(DCGM)
add_subdirectory(Proc)

#-- sysagent library
add_library(sysagent
    "AWS/aws.h"
    "Config/config.h"
    "CPUFreq/cpufreq.h"
    "Disk/disk.h"
    "Ethtool/ethtool.h"
    "Files/files.h"
    "NVML/gpumetrics.h"
    "internal/disk.inc"
    "Logger/logger.cc"
    "Logger/logger.h"
    "MonotonicTimer/monotonic_timer.h"
    "NTP/ntp.h"
    "NVML/nvml.cc"
    "NVML/nvml.h"
    "Perfmetrics/perfmetrics.h"
    "PressureStall/pressure_stall.h"
    "Config/sample_config.cc"
)
target_link_libraries(sysagent
    spectator
    abseil::abseil
    CURL::libcurl
    fmt::fmt
    rapidjson
    spdlog::spdlog
    util
    tagging
    measurement_utils
    HttpClient
    cgroup
    service_monitor
    dcgm
    proc
)