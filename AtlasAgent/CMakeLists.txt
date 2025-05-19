#-- atlas_system_agent executable
add_executable(atlas_system_agent "src/atlas-agent.cpp")

target_include_directories(atlas_system_agent
    PUBLIC ${CMAKE_SOURCE_DIR}
)


target_link_libraries(atlas_system_agent 
    Backward::Backward
    fmt::fmt
    abseil::abseil
    aws
    cgroup
    cpufreq
    nvml
    proc
    disk
    ebs
    ethtool
    ntp
    perf_metrics
    pressurestall
    util
    dcgm
    service_monitor
    HttpClient
    logger
)

# required to allow running on older systems, such as bionic
target_link_options(atlas_system_agent PRIVATE "-static-libstdc++")
