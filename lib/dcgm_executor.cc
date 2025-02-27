
#include "dcgm_executor.h"
#include "dcgm_fields.h"
#include <iostream>
#include <fstream>
template <class Reg>
void DCGMExecutor<Reg>::ParseLines(std::vector<std::string> &lines, std::map<int, std::vector<DataLine>> &dataMap)
{
    // Loop through each line in your input
    for (const auto& line : lines) 
    {
        // Check if the line starts with "Data:"
        if (line.substr(0, 5) != "Data:") 
        {
            std::cout << "Skipping invalid line: " << line << std::endl;
            continue; // Skip the line if it doesn't start with "Data:"
        }

        // Remove the "Data:" prefix for further processing
        std::string dataPart = line.substr(5); // Everything after "Data:"
        
        // Example: Split the line by comma (assuming CSV format)
        size_t pos = 0;
        std::string token;
        std::vector<std::string> tokens;

        while ((pos = dataPart.find(',')) != std::string::npos) 
        {
            token = dataPart.substr(0, pos);
            tokens.push_back(token);
            dataPart.erase(0, pos + 1);
        }
        tokens.push_back(dataPart);  // The last token

        // Extract the fields from the tokens
        int gpuId = std::stoi(tokens[0]);
        std::string fieldName = tokens[1];
        int fieldId = std::stoi(tokens[2]);
        double value = std::stod(tokens[3]);
        std::string valueInterpretation = tokens[4];

        // Create a DataLine object from the extracted data
        DataLine dataLine(fieldName, fieldId, value, valueInterpretation);

        // Insert the DataLine into the map under the correct gpuId
        dataMap[gpuId].push_back(dataLine);
    }
}

template <class Reg>
void DCGMExecutor<Reg>::PrintDataMap(std::map<int, std::vector<DataLine>> &dataMap)
{

    // Create an ofstream to open the file for output
    std::ofstream outFile("/opt/output.txt");

    // Save the original standard output stream (optional)
    std::streambuf* orig_cout_stream = std::cout.rdbuf();

    // Redirect std::cout to the file
    std::cout.rdbuf(outFile.rdbuf());

    // Print the result (for debugging or verification)
    for (const auto& [gpuId, dataLines] : dataMap)
    {
        std::cout << "GPU ID: " << gpuId << std::endl;
        for (const auto& dataLine : dataLines) 
        {
            std::cout << "  Field: " << dataLine.fieldName
                      << ", Field ID: " << dataLine.fieldId
                      << ", Value: " << dataLine.value
                      << ", Interpretation: " << dataLine.valueInterpretation << std::endl;
        }
    }
    // Restore the original std::cout
    std::cout.rdbuf(orig_cout_stream);
}

template <class Reg>
void DCGMExecutor<Reg>::UpdateMetrics(std::map<int, std::vector<DataLine>> &dataMap)
{
    for (const auto& [gpuId, dataLines] : dataMap) 
    {
        
        std::cout << "Updating Registry Metrics for GPU ID: " << gpuId << std::endl;
        for (const auto& dataLine : dataLines)
        {
            switch (dataLine.fieldId)
            {
            case DCGM_FI_DEV_POWER_USAGE:
                detail::gauge(registry_, "gpu.dcgm.powerUsage", gpuId)->Set(dataLine.value);
                break;
            case DCGM_FI_DEV_GPU_TEMP:
                detail::gauge(registry_, "gpu.dcgm.deviceTemp", gpuId)->Set(dataLine.value);
                break;
            case DCGM_FI_PROF_SM_ACTIVE:
                detail::gauge(registry_, "gpu.dcgm.sm", gpuId)->Set(dataLine.value);
                break;
            case DCGM_FI_PROF_SM_OCCUPANCY:
                detail::gauge(registry_, "gpu.dcgm.sm", gpuId)->Set(dataLine.value);
                break;
            case DCGM_FI_PROF_PIPE_TENSOR_ACTIVE:
                detail::gauge(registry_, "gpu.dcgm.tensorCoresUtilization", gpuId)->Set(dataLine.value);
                break;
            case DCGM_FI_PROF_DRAM_ACTIVE:
                detail::gauge(registry_, "gpu.dcgm.memoryBandwidthUtilization", gpuId)->Set(dataLine.value);
                break;
            case DCGM_FI_PROF_PIPE_FP32_ACTIVE:
                detail::gauge(registry_, "gpu.dcgm.pipeUtilization", gpuId)->Set(dataLine.value);
                break;
            case DCGM_FI_PROF_PIPE_FP16_ACTIVE:
                detail::gauge(registry_, "gpu.dcgm.pipeUtilization", gpuId)->Set(dataLine.value);
                break;
            case DCGM_FI_PROF_PCIE_TX_BYTES:
                detail::gauge(registry_, "gpu.dcgm.pcie.bytes", gpuId)->Set(dataLine.value);
                break;
            case DCGM_FI_PROF_PCIE_RX_BYTES:
                detail::gauge(registry_, "gpu.dcgm.pcie.bytes", gpuId)->Set(dataLine.value);
                break;
            case DCGM_FI_PROF_NVLINK_RX_BYTES:
                detail::gauge(registry_, "gpu.dcgm.nvlink.bytes", gpuId)->Set(dataLine.value);
                break;
            case DCGM_FI_PROF_NVLINK_TX_BYTES:
                detail::gauge(registry_, "gpu.dcgm.nvlink.bytes", gpuId)->Set(dataLine.value);
                break;
            case DCGM_FI_PROF_GR_ENGINE_ACTIVE:
                detail::gauge(registry_, "gpu.dcgm.graphicsEngineActivity", gpuId)->Set(dataLine.value);
                break;
            default:
                std::cout << "Error Unknown field type.";
                break;
            }
                
        }
    }
}

template <class Reg>
bool DCGMExecutor<Reg>::DCGMMetrics()
{
    std::cout << "Calling Executor" << std::endl;

    auto lines = atlasagent::read_output_lines(DCGMExecutorConstants::ExternalBinaryPath);

    std::map<int, std::vector<DataLine>> dataMap;
    ParseLines(lines, dataMap);

    if (lines[0] != "Success")
    {
        return false;
    }

    return true;
}
template class DCGMExecutor<atlasagent::TaggingRegistry>;
