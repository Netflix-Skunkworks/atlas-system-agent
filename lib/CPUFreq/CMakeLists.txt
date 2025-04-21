add_library(cpufreq INTERFACE
  src/cpufreq.h
)

target_include_directories(cpufreq
  INTERFACE ${CMAKE_SOURCE_DIR}
)

target_link_libraries(cpufreq
  INTERFACE
    fmt::fmt
)