#include "dcgm_stats.h"
#include <sstream>
#include <iostream>
#include <optional>
#include <iostream>
#include <gtest/gtest.h>


void PrintValues(const std::map<int, std::vector<dcgmFieldValue_v1>> &field_val_map)
{
    // Iterate over each GPU in the map and print the field values
    for (const auto &gpuPair : field_val_map)
    {
        unsigned int gpuId = gpuPair.first;
        const std::vector<dcgmFieldValue_v1> &fieldValues = gpuPair.second;

        std::cout << "GPU " << gpuId << " field values:" << std::endl;

        // Iterate over each field value for this GPU and print it
        for (const auto &fieldValue : fieldValues)
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

            std::cout << std::endl; // Add a newline after each field value
        }

        std::cout << std::endl; // Add a newline after each GPU's field values
    }
}

void PrintErrorCode(ErrorCode code)
{
    switch (code)
    {
    case ErrorCode::SUCCESS:
        std::cout << "Operation succeeded!" << std::endl;
        break;
    case ErrorCode::INITIALIZATION_FAILED:
        std::cout << "Error initializing DCGM engine. Return: "; //<< errorString(result) << std::endl;
        break;
    case ErrorCode::EMBEDDED_MODE_FAILED_TO_START:
        std::cout << "Error starting embedded DCGM engine. Return: "; // << errorString(result) << std::endl;
        break;
    case ErrorCode::FAILED_TO_GATHER_NUMBER_OF_DEVICES:
        std::cout << "Error fetching devices. Return: "; //<< errorString(result) << std::endl;
        break;
    case ErrorCode::NO_DEVICES_TO_PROFILE:
        std::cout << "Device count on machine is 0. No devices to profile" << std::endl;
        break;
    case ErrorCode::FAILED_TO_CREATE_FIELD_GROUP:
        std::cout << "Failed to create a field group" << std::endl;
        break;
    case ErrorCode::FAILED_TO_WATCH_FIELDS:
        std::cout << "Error setting watches. Result: " << std::endl;
        break;
    default:
        std::cout << "Unknown error code.\n";
        break;
    }
}


TEST(CpuFreq, Stats) 
{
    GpuMetricsDCGM gpuMetrics{};
    std::map<int, std::vector<dcgmFieldValue_v1>> field_value_map{};
    ErrorCode ec = gpuMetrics.Driver(field_value_map);
    if (ErrorCode::SUCCESS != ec)
    {
        PrintErrorCode(ec);
    }

    PrintValues(field_value_map);  
}
