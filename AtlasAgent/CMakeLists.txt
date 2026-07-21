#-- atlas_system_agent executable
# The entry point is shared; the per-flavor collection logic lives in a separate
# source selected by AGENT_FLAVOR (see the top-level CMakeLists.txt), so only one
# collector implementation is ever compiled into the binary.
if(AGENT_FLAVOR STREQUAL "titus")
    set(ATLAS_AGENT_SOURCES src/atlas-agent.cpp src/titus-agent.cpp)
elseif(AGENT_FLAVOR STREQUAL "k8s")
    set(ATLAS_AGENT_SOURCES src/atlas-agent.cpp src/k8s-agent.cpp)
else()
    set(ATLAS_AGENT_SOURCES src/atlas-agent.cpp src/system-agent.cpp)
endif()

add_executable(atlas_system_agent ${ATLAS_AGENT_SOURCES})

target_include_directories(atlas_system_agent
    PUBLIC ${CMAKE_SOURCE_DIR}
)


target_link_libraries(atlas_system_agent 
    Backward::Backward
    fmt::fmt
    abseil::abseil
    amd_smi_collector
    aws
    cgroup
    cpu_freq
    nvml
    proc
    disk
    ebs
    ethtool
    ntp
    perf_metrics
    perfspect
    pressure_stall
    util
    dcgm
    service_monitor
    HttpClient
    logger
    spectator-registry
)

# required to allow running on older systems, such as bionic
target_link_options(atlas_system_agent PRIVATE "-static-libstdc++")
