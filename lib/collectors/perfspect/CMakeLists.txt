add_library(perfspect
    src/perfspect.cpp
    src/perfspect.h
)

target_include_directories(perfspect
    PUBLIC ${CMAKE_SOURCE_DIR}
)

target_link_libraries(perfspect
    spectator-registry
    Boost::boost
    logger
)

add_executable(perfspect_example
    example/perf-main.cpp
)

target_link_libraries(perfspect_example
    perfspect
    spectator-registry
    util
)