#include "dcgm_stats.h"
#include <sstream>
#include <iostream>
#include <optional>
#include <iostream>
#include "util.h"

#include <gtest/gtest.h>

struct DataLine {
    std::string fieldName;
    int fieldId;
    double value;
    std::string valueInterpretation;

    // Constructor to initialize the struct
    DataLine(std::string name, int id, double val, std::string interpretation)
        : fieldName(name), fieldId(id), value(val), valueInterpretation(interpretation) {}
};



void ParseLines(std::vector<std::string> &lines, std::map<int, std::vector<DataLine>> &dataMap)
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

void PrintDataMap(std::map<int, std::vector<DataLine>> &dataMap)
{
    // Print the result (for debugging or verification)
    for (const auto& [gpuId, dataLines] : dataMap) {
        std::cout << "GPU ID: " << gpuId << std::endl;
        for (const auto& dataLine : dataLines) {
            std::cout << "  Field: " << dataLine.fieldName
                      << ", Field ID: " << dataLine.fieldId
                      << ", Value: " << dataLine.value
                      << ", Interpretation: " << dataLine.valueInterpretation << std::endl;
        }
    }
}

TEST(EverettB, EverettB) 
{
    
    // Path to the binary in your CMake build folder
    const char* binaryPath = "cmake-build/bin/dcgm_stats_main";

    auto lines = atlasagent::read_output_lines(binaryPath, 50000);

    std::map<int, std::vector<DataLine>> dataMap;

    ParseLines(lines, dataMap);

    std::cout << "Everett Checkoutput Below Here:" << std::endl;
    PrintDataMap(dataMap);

}