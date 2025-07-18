add_library(cpu_freq
    src/cpu_freq.h
    src/cpu_freq.cpp
)

target_include_directories(cpu_freq
    PUBLIC ${CMAKE_SOURCE_DIR}
)

target_link_libraries(cpu_freq
    abseil::abseil
    fmt::fmt
    spectator-registry
)

add_executable(cpu_freq_test
    test/cpu_freq_test.cpp
)

target_link_libraries(cpu_freq_test
    cpu_freq
    gtest::gtest
    logger
    spectator-registry
    util    
)

add_test(
    NAME cpu_freq_test
    COMMAND cpu_freq_test
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)