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
target_link_libraries(spectator
    abseil::abseil
    asio::asio
    Backward::Backward
    fmt::fmt
    spdlog::spdlog
)

#-- generator tools
add_executable(gen_valid_chars "tools/gen_valid_chars.cc")

#-- file generators, must exist where the outputs are referenced
add_custom_command(
    OUTPUT "spectator/valid_chars.inc"
    COMMAND "${CMAKE_BINARY_DIR}/bin/gen_valid_chars" > "${CMAKE_SOURCE_DIR}/lib/spectator/valid_chars.inc"
    DEPENDS gen_valid_chars
)

add_subdirectory(Util)
add_subdirectory(Tagging)
add_subdirectory(MeasurementUtils)
add_subdirectory(HttpClient)

#-- sysagent library
add_library(sysagent
    "AWS/aws.h"
    "CGroup/cgroup.h"
    "Config/config.h"
    "CPUFreq/cpufreq.h"
    "DCGM/dcgm_stats.h"
    "DCGM/dcgm_stats.cc"
    "Disk/disk.h"
    "Ethtool/ethtool.h"
    "Files/files.h"
    "NVML/gpumetrics.h"
    "internal/cgroup.inc"
    "internal/disk.inc"
    "internal/proc.inc"
    "Logger/logger.cc"
    "Logger/logger.h"
    "MonotonicTimer/monotonic_timer.h"
    "NTP/ntp.h"
    "NVML/nvml.cc"
    "NVML/nvml.h"
    "Perfmetrics/perfmetrics.h"
    "PressureStall/pressure_stall.h"
    "Proc/proc.h"
    "Config/sample_config.cc"
    "ServiceMonitor/service_monitor.cc"
    "ServiceMonitor/service_monitor.h"
    "ServiceMonitor/service_monitor_utils.cc"
    "ServiceMonitor/service_monitor_utils.h"
)
target_link_libraries(sysagent
    spectator
    abseil::abseil
    CURL::libcurl
    fmt::fmt
    rapidjson
    SDBusCpp::sdbus-c++
    spdlog::spdlog
    util
    tagging
    measurement_utils
    HttpClient
)