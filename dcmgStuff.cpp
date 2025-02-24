#include "dcgm_structs.h"
#include "string.h"
#include "unistd.h"
#include <iostream>
#include <map>
#include <unordered_map>
#include <time.h>
#include <vector>

void printFieldValue(const dcgmFieldValue_v1 &fieldValue)
{
    dcgm_field_meta_p fieldMeta = DcgmFieldGetById(fieldValue.fieldId);
    std::string fieldName = (fieldMeta ? fieldMeta->tag : "Unknown");
    std::cout << "Name: " << fieldName << std::endl;
    std::cout << "Field ID => " << fieldValue.fieldId << std::endl;
    std::cout << "Value => ";

    switch (fieldMeta ? fieldMeta->fieldType : DCGM_FI_UNKNOWN)
    {
    case DCGM_FT_DOUBLE:
        std::cout << fieldValue.value.dbl;
        break;
    case DCGM_FT_INT64:
        std::cout << fieldValue.value.i64;
        break;
    case DCGM_FT_STRING:
        std::cout << fieldValue.value.str;
        break;
    case DCGM_FT_TIMESTAMP:
        std::cout << fieldValue.value.i64;
        break;
    case DCGM_FT_BINARY:
        // Handle binary data if required
        std::cout << "(binary data)";
        break;
    default:
        std::cout << "Unknown field type.";
        break;
    }

    std::cout << std::endl;
}

int list_field_values(unsigned int gpuId, dcgmFieldValue_v1 *values, int numValues, void *userdata)
{

    auto &field_val_map = *static_cast<std::map<int, std::vector<dcgmFieldValue_v1> > *>(userdata);

    // Store values in the map (nested map structure: {gpuId -> {fieldId -> value}})
    for (int i = 0; i < numValues; ++i)
    {
        field_val_map[gpuId].push_back(values[i]);
    }
    return 0;
}

class GpuMetricsDCGM
{
private:
    dcgmHandle_t dcgmHandle = (dcgmHandle_t)NULL;

public:
    int Init();
    int GatherStatistics();
    void Cleanup();
};

int GpuMetricsDCGM::Init()
{
    dcgmReturn_t result = dcgmInit();
    if (result != DCGM_ST_OK)
    {
        std::cout << "Error initializing DCGM engine. Return: " << errorString(result) << std::endl;
        Cleanup();
        return -1;
    }

    // Why is he using MODE_AUTO (does not match documentation)
    result = dcgmStartEmbedded(DCGM_OPERATION_MODE_AUTO, &dcgmHandle);
    if (result != DCGM_ST_OK)
    {
        std::cout << "Error starting embedded DCGM engine. Return: " << errorString(result) << std::endl;
        Cleanup();
        return -1;
    }

    unsigned int gpuIdList[DCGM_MAX_NUM_DEVICES];

    int numberOfDevices;
    result = dcgmGetAllSupportedDevices(dcgmHandle, gpuIdList, &numberOfDevices);
    if (result != DCGM_ST_OK)
    {
        std::cout << "Error fetching devices. Return: " << errorString(result) << std::endl;
        Cleanup();
        return -1;
    }

    if (numberOfDevices == 0)
    {
        std::cout << "Error: No Supported GPUs.\n";
        Cleanup();
        return -1;
    }
    std::cout << "Current Number Of Devices: " << numberOfDevices << std::endl;
    return 1;
}

int GpuMetricsDCGM::GatherStatistics()
{

    dcgmReturn_t result = dcgmInit();

    dcgmFieldGrp_t fieldGroupId;

    unsigned short fieldIds[14];
    fieldIds[0] = DCGM_FI_DEV_POWER_USAGE;
    fieldIds[1] = DCGM_FI_DEV_GPU_TEMP;
    fieldIds[2] = DCGM_FI_PROF_SM_ACTIVE;
    fieldIds[3] = DCGM_FI_PROF_SM_OCCUPANCY;
    fieldIds[4] = DCGM_FI_PROF_PIPE_TENSOR_ACTIVE;
    fieldIds[5] = DCGM_FI_PROF_DRAM_ACTIVE;
    //fieldIds[5] = DCGM_FI_PROF_PIPE_FP64_ACTIVE;
    fieldIds[12] = DCGM_FI_PROF_PIPE_FP32_ACTIVE;
    fieldIds[11] = DCGM_FI_PROF_PIPE_FP16_ACTIVE;
    fieldIds[6] = DCGM_FI_PROF_PCIE_TX_BYTES;
    fieldIds[7] = DCGM_FI_PROF_PCIE_RX_BYTES;
    fieldIds[8] = DCGM_FI_PROF_NVLINK_RX_BYTES;
    fieldIds[9] = DCGM_FI_PROF_NVLINK_TX_BYTES;
    fieldIds[10] = DCGM_FI_PROF_GR_ENGINE_ACTIVE;

    result = dcgmFieldGroupCreate(dcgmHandle,13, &fieldIds[0], (char *)"interesting_fields", &fieldGroupId);
    if (result != DCGM_ST_OK)
    {
        std::cout << "Error creating field group. Result: " << errorString(result) << std::endl;
        Cleanup();
        return -1;
    }

    long long loopIntervalUsec = 1000000; /* How long should we sleep after each loop iteration */
    result = dcgmWatchFields(dcgmHandle, DCGM_GROUP_ALL_GPUS, fieldGroupId, loopIntervalUsec, 3600.0, 3600);

    if (result == DCGM_ST_BADPARAM)
    {
        std::cout << "BADDD Result: " << errorString(result) << std::endl;
        Cleanup();
        return -1;
    }

    // Check result to see if DCGM operation was successful.
    if (result != DCGM_ST_OK)
    {
        std::cout << "hey" << (result) << std::endl;
        std::cout << "Error setting watches. Result: " << errorString(result) << std::endl;
        Cleanup();
        return -1;
    }

    dcgmUpdateAllFields(dcgmHandle, 1);

    std::map<int, std::vector<dcgmFieldValue_v1> > field_val_map;
    result = dcgmGetLatestValues(dcgmHandle, DCGM_GROUP_ALL_GPUS, fieldGroupId, &list_field_values, &field_val_map);
    if (result != DCGM_ST_OK)
    {
        std::cout << "Error getValues information:" << errorString(result) << std::endl;
        Cleanup();
        return -1;
    }

    // Iterate over each GPU in the map and print the field values
    for (const auto &gpuPair : field_val_map)
    {
        unsigned int gpuId = gpuPair.first;
        const std::vector<dcgmFieldValue_v1> &fieldValues = gpuPair.second;

        std::cout << "GPU " << gpuId << " field values:" << std::endl;

        // Iterate over each field value for this GPU and print it
        for (const auto &fieldValue : fieldValues)
        {
            printFieldValue(fieldValue);  // Call printFieldValue for each fieldValue
        }

        std::cout << std::endl;  // Add a newline for each GPU
    }

    Cleanup();
    return 0;
}

void GpuMetricsDCGM::Cleanup()
{
    std::cout << "Cleaning up.\n";
    dcgmStopEmbedded(dcgmHandle);
    dcgmShutdown();
}

int main(int argc, char **argv)
{
    dcgmHandle_t dcgmHandle = (dcgmHandle_t)NULL;
    dcgmReturn_t result = dcgmInit();
    if (result != DCGM_ST_OK)
    {
        std::cout << "Error initializing DCGM engine. Return: " << errorString(result) << std::endl;
        return -1;
    }

    // Why is he using MODE_AUTO (does not match documentation)
    result = dcgmStartEmbedded(DCGM_OPERATION_MODE_AUTO, &dcgmHandle);
    if (result != DCGM_ST_OK)
    {
        std::cout << "Error starting embedded DCGM engine. Return: " << errorString(result) << std::endl;
        return -1;
    }

    dcgmStopEmbedded(dcgmHandle);
    dcgmShutdown();


    // GpuMetricsDCGM x;
    // x.Init();
    // x.GatherStatistics();
    // return 0;
}

