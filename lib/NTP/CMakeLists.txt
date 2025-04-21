add_library(ntp INTERFACE
  src/ntp.h
)

target_include_directories(ntp
  INTERFACE ${CMAKE_SOURCE_DIR}
)

target_link_libraries(ntp
  INTERFACE
    fmt::fmt
)