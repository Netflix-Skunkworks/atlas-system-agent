#include "dcgm_stats.h"

#include <optional>

int list_field_values(unsigned int gpuId, dcgmFieldValue_v1 *values, int numValues, void *userdata)
{

    auto &field_val_map = *static_cast<std::map<int, std::vector<dcgmFieldValue_v1>> *>(userdata);

    // Store values in the map (nested map structure: {gpuId -> {fieldId -> value}})
    for (int i = 0; i < numValues; ++i)
    {
        field_val_map[gpuId].push_back(values[i]);
    }
    return 0;
}

ErrorCode GpuMetricsDCGM::Init()
{
    dcgmReturn_t result = dcgmInit();
    if (result != DCGM_ST_OK)
    {
        // return errorString(result)
        return ErrorCode::INITIALIZATION_FAILED;
    }

    // Why is he using MODE_AUTO (does not match documentation)
    result = dcgmStartEmbedded(DCGM_OPERATION_MODE_AUTO, &dcgmHandle);
    if (result != DCGM_ST_OK)
    {
        return ErrorCode::EMBEDDED_MODE_FAILED_TO_START;
    }

    return ErrorCode::SUCCESS;
}

ErrorCode GpuMetricsDCGM::GatherDeviceCount(int &deviceCount)
{
    unsigned int gpuIdList[DCGM_MAX_NUM_DEVICES];

    dcgmReturn_t result = dcgmGetAllSupportedDevices(dcgmHandle, gpuIdList, &deviceCount);
    if (result != DCGM_ST_OK)
    {
        std::cout << "Error fetching devices. Return: " << errorString(result) << std::endl;
        return ErrorCode::FAILED_TO_GATHER_NUMBER_OF_DEVICES;
    }

    return ErrorCode::SUCCESS;
}


ErrorCode GpuMetricsDCGM::SetWatchStats(dcgmFieldGrp_t &fieldGroupId)
{
    

    dcgmReturn_t result = dcgmFieldGroupCreate(dcgmHandle, 2, const_cast<short unsigned int*>(&fieldIds[0]), (char *)"interesting_fields", &fieldGroupId);

    if (result != DCGM_ST_OK)
    {
        return ErrorCode::FAILED_TO_CREATE_FIELD_GROUP;
    }

    long long loopIntervalUsec = 1000000; /* How long should we sleep after each loop iteration */
    result = dcgmWatchFields(dcgmHandle, DCGM_GROUP_ALL_GPUS, fieldGroupId, loopIntervalUsec, 3600.0, 3600);

    // Check result to see if DCGM operation was successful.
    if (result != DCGM_ST_OK)
    {
        return ErrorCode::FAILED_TO_WATCH_FIELDS;
    }

    return ErrorCode::SUCCESS;
}


ErrorCode GpuMetricsDCGM::GatherStatistics(std::map<int, std::vector<dcgmFieldValue_v1>> &field_value_map, dcgmFieldGrp_t &fieldGroupId)
{

    dcgmUpdateAllFields(dcgmHandle, 1);
    dcgmReturn_t result = dcgmGetLatestValues(dcgmHandle, DCGM_GROUP_ALL_GPUS, fieldGroupId, &list_field_values, &(field_value_map));
    if (result != DCGM_ST_OK)
    {
        return ErrorCode::FAILED_TO_GET_LATEST_VALUES;
    }

    return ErrorCode::SUCCESS;
}


void GpuMetricsDCGM::Cleanup()
{
    std::cout << "Cleaning up.\n";
    dcgmStopEmbedded(dcgmHandle);
    dcgmShutdown();
}


ErrorCode GpuMetricsDCGM::Driver(std::map<int, std::vector<dcgmFieldValue_v1>> &field_value_map)
{
    ErrorCode ec = this->Init();
    if (ErrorCode::SUCCESS != ec)
    {
        return ec;
    }

    int deviceCount{};
    ec = this->GatherDeviceCount(deviceCount);
    if (ErrorCode::SUCCESS != ec)
    {
        return ec;
    }

    if (0 == deviceCount)
    {
        return ErrorCode::NO_DEVICES_TO_PROFILE;
    }

    std::cout << "Number of devices:" << deviceCount << std::endl;

    dcgmFieldGrp_t fieldGroupId;
    ec = this->SetWatchStats(fieldGroupId);
    if (ErrorCode::SUCCESS != ec)
    {
        return ec;
    }

    ec = this->GatherStatistics(field_value_map, fieldGroupId);
    if (ErrorCode::SUCCESS != ec)
    {
        return ec;
    }

    return ec;
}