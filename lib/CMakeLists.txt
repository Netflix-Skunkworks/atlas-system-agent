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
)
target_include_directories(spectator PRIVATE ${CMAKE_SOURCE_DIR})
target_link_libraries(spectator ${CONAN_LIBS})

#-- sysagent library
add_library(sysagent
    "aws.h"
    "cgroup.h"
    "config.h"
    "cpufreq.h"
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
target_include_directories(sysagent PRIVATE ${CMAKE_SOURCE_DIR})
target_link_libraries(sysagent contain spectator ${CONAN_LIBS})
