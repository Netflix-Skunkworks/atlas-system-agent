add_library(files INTERFACE
  src/files.h
)

target_include_directories(files
  INTERFACE ${CMAKE_SOURCE_DIR}
)

target_link_libraries(files
  INTERFACE
    fmt::fmt
)