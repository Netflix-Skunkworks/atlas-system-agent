add_library(monotonic_timer INTERFACE
  src/monotonic_timer.h
)

target_include_directories(monotonic_timer
  INTERFACE ${CMAKE_SOURCE_DIR}
)

target_link_libraries(monotonic_timer
  INTERFACE
    fmt::fmt
)