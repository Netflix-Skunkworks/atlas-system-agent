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
add_subdirectory(Disk)
add_subdirectory(NVML)
add_subdirectory(AWS)
add_subdirectory(CPUFreq)
add_subdirectory(Ethtool)
add_subdirectory(Files)
add_subdirectory(Logger)
add_subdirectory(MonotonicTimer)
add_subdirectory(NTP)
add_subdirectory(Perfmetrics)
add_subdirectory(PressureStall)