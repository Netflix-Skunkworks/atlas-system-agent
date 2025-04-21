add_library(perfmetrics INTERFACE
  src/perfmetrics.h
)

target_include_directories(perfmetrics
  INTERFACE ${CMAKE_SOURCE_DIR}
)

target_link_libraries(perfmetrics
  INTERFACE
    fmt::fmt
)