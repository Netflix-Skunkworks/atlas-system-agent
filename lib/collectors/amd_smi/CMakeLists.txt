add_library(amd_smi_collector
    amd_smi.cpp
    amd_smi.h
    gpumetrics.cpp
    gpumetrics.h
)

target_include_directories(amd_smi_collector
    PUBLIC
        ${CMAKE_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/thirdparty/rocm-systems/projects/amdsmi/include
)

# 'amd_smi' is the shared-library target produced by thirdparty/amdsmi.
target_link_libraries(amd_smi_collector
    PUBLIC
        amd_smi
    PRIVATE
        fmt::fmt
        spdlog::spdlog
        spectator-registry
        logger
)
