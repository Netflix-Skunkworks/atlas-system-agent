add_library(ethtool INTERFACE
  src/ethtool.h
)

target_include_directories(ethtool
  INTERFACE ${CMAKE_SOURCE_DIR}
)

target_link_libraries(ethtool
  INTERFACE
    fmt::fmt
    measurement_utils
)