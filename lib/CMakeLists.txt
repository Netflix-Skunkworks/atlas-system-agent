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


#-- sysagent library
add_library(sysagent
    "aws.h"
    "cgroup.h"
    "config.h"
    "cpufreq.h"
    "dcgm_stats.h"
    "dcgm_stats.cc"
    "disk.h"
    "ethtool.h"
    "files.h"
    "gpumetrics.h"
    "http_client.h"
    "internal/cgroup.inc"
    "internal/disk.inc"
    "internal/http_client.inc"
    "internal/proc.inc"
    "log_entry.h"
    "logger.cc"
    "logger.h"
    "monotonic_timer.h"
    "ntp.h"
    "nvml.cc"
    "nvml.h"
    "perfmetrics.h"
    "pressure_stall.h"
    "proc.h"
    "sample_config.cc"
    "tagger.h"
    "tagging_registry.h"
    "util.cc"
    "util.h"
)
target_link_libraries(sysagent
    contain
    spectator
    abseil::abseil
    CURL::libcurl
    fmt::fmt
    rapidjson
    spdlog::spdlog
)