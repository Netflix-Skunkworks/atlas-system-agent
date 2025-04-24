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
add_custom_command(
    OUTPUT "spectator/valid_chars.inc"
    COMMAND "${CMAKE_BINARY_DIR}/bin/gen_valid_chars" > "${CMAKE_SOURCE_DIR}/lib/spectator/valid_chars.inc"
    DEPENDS gen_valid_chars
)

target_link_libraries(spectator
    abseil::abseil
    asio::asio
    Backward::Backward
    fmt::fmt
    spdlog::spdlog
)

add_subdirectory(collectors)
add_subdirectory(files)
add_subdirectory(http_client)
add_subdirectory(logger)
add_subdirectory(measurement_utils)
add_subdirectory(monotonic_timer)
add_subdirectory(tagging)
add_subdirectory(util)