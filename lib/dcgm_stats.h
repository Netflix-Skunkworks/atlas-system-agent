#include "dcgm_agent.h"
#include "dcgm_structs.h"
#include "string.h"
#include "unistd.h"
#include <iostream>
#include <map>
#include <unordered_map>
#include <time.h>
#include <vector>
#include <optional>
#include <array>

enum class ErrorCode
{
    SUCCESS = 0,
    INITIALIZATION_FAILED = 1,
    EMBEDDED_MODE_FAILED_TO_START = 2,
    FAILED_TO_GATHER_NUMBER_OF_DEVICES = 3,
    FAILED_TO_CREATE_FIELD_GROUP = 4,
    FAILED_TO_WATCH_FIELDS = 5,
    FAILED_TO_GET_LATEST_VALUES = 6,
    NO_DEVICES_TO_PROFILE = 7,
};

class GpuMetricsDCGM
{
private:
    dcgmHandle_t dcgmHandle = (dcgmHandle_t) nullptr;
    static constexpr std::array<unsigned short, 13> fieldIds{
        DCGM_FI_DEV_POWER_USAGE,
        DCGM_FI_DEV_GPU_TEMP,
        DCGM_FI_PROF_SM_ACTIVE,
        DCGM_FI_PROF_SM_OCCUPANCY,
        DCGM_FI_PROF_PIPE_TENSOR_ACTIVE,
        DCGM_FI_PROF_DRAM_ACTIVE,
        DCGM_FI_PROF_PIPE_FP32_ACTIVE,
        DCGM_FI_PROF_PIPE_FP16_ACTIVE,
        DCGM_FI_PROF_PCIE_TX_BYTES,
        DCGM_FI_PROF_PCIE_RX_BYTES,
        DCGM_FI_PROF_NVLINK_RX_BYTES,
        DCGM_FI_PROF_NVLINK_TX_BYTES,
        DCGM_FI_PROF_GR_ENGINE_ACTIVE
    };
    ErrorCode Init();
    ErrorCode GatherDeviceCount(int &deviceCount);
    ErrorCode SetWatchStats(dcgmFieldGrp_t &fieldGroupId);
    ErrorCode GatherStatistics(std::map<int, std::vector<dcgmFieldValue_v1>> &field_value_map, dcgmFieldGrp_t &fieldGroupId);
    void Cleanup();

public:
    GpuMetricsDCGM() {};
    ~GpuMetricsDCGM() { this->Cleanup(); };

    // Abide by the C++ rule of 5
    GpuMetricsDCGM(const GpuMetricsDCGM &other) = delete;
    GpuMetricsDCGM &operator=(const GpuMetricsDCGM &other) = delete;
    GpuMetricsDCGM(GpuMetricsDCGM &&other) noexcept = delete;
    GpuMetricsDCGM &operator=(GpuMetricsDCGM &&other) noexcept = delete;

    ErrorCode Driver(std::map<int, std::vector<dcgmFieldValue_v1>> &field_value_map);
};
