add_library(pressurestall INTERFACE
  src/pressure_stall.h
)

target_include_directories(pressurestall
  INTERFACE ${CMAKE_SOURCE_DIR}
)

target_link_libraries(pressurestall
  INTERFACE
    fmt::fmt
)