#include "perfspect.h"
#include <lib/logger/src/logger.h>
#include <lib/util/src/util.h>

#include <boost/process.hpp>

#include <string>
#include <string_view>
#include <vector>
#include <algorithm>
#include <sstream>
#include <fstream>


Perfspect::Perfspect(Registry* registry, std::pair<char, char> instanceInfo) 
    : registry_(registry), isAmd(instanceInfo.second == 'a'), version(instanceInfo.first)
{
    atlasagent::Logger()->info("Perfspect initialized for instance type: {}{}", instanceInfo.first, instanceInfo.second);
};

std::optional<std::pair<char, char>> Perfspect::is_valid_instance()
{
    if (false == atlasagent::is_file_present(PerfspectConstants::BinaryLocation))
    {
        atlasagent::Logger()->error("Perfspect binary not found at {}", PerfspectConstants::BinaryLocation);
        return std::nullopt;
    }

    std::ifstream file("/sys/devices/virtual/dmi/id/product_name");
    std::string product_name;
    if (!file.is_open() || !std::getline(file, product_name)) {
        atlasagent::Logger()->error("Failed to read product name from DMI");
        return std::nullopt;
    }

    size_t dot = product_name.find('.');
    if (dot == std::string::npos || dot < 3) {
        atlasagent::Logger()->error("Invalid product name format: {}", product_name);
        return std::nullopt;
    }

    char generation = product_name[1];
    char processor = product_name[2];
    
    bool valid = std::isdigit(generation) && (generation >= '7') && (processor == 'i' || processor == 'a');
    
    if (!valid) {
        atlasagent::Logger()->error("Invalid instance: {} (gen: {}, proc: {})", product_name, generation, processor);
        return std::nullopt;
    }
    return std::make_pair(generation, processor);
}


bool Perfspect::start_script()
{
    try
    {
        // Clean up any existing process first
        cleanup_process();

        std::string fullBinaryPath =
            std::string(PerfspectConstants::BinaryLocation) + "/" + PerfspectConstants::BinaryName;
        atlasagent::Logger()->info("Starting perfspect process for 60 seconds: {}", fullBinaryPath);

        // Create new streams
        this->outStream = std::make_unique<boost::process::ipstream>();
        this->errStream = std::make_unique<boost::process::ipstream>();

        // Select the appropriate event and metric file paths based on processor type
        const char* eventfilePath = this->isAmd ? PerfspectConstants::eventfilePathAmd : PerfspectConstants::eventfilePathIntel;
        const char* metricfilePath = this->isAmd ? PerfspectConstants::metricfilePathAmd : PerfspectConstants::metricfilePathIntel;

        // Use individual argument constants with --live and --duration 60
        this->scriptProcess = std::make_unique<boost::process::child>(
            fullBinaryPath, PerfspectConstants::command, PerfspectConstants::eventfileFlag,
            eventfilePath, PerfspectConstants::metricfileFlag, metricfilePath,
            PerfspectConstants::durationFlag, PerfspectConstants::durationValue, PerfspectConstants::liveFlag,
            boost::process::std_out > *this->outStream, boost::process::std_err > *this->errStream);
        
        atlasagent::Logger()->info("Perfspect process started successfully with PID: {}", this->scriptProcess->id());
        return true;
    }
    catch (const boost::process::process_error& e)
    {
        atlasagent::Logger()->error("Failed to start script: {}", e.what());
        cleanup_process();
        return false;
    }
    catch (const std::exception& e)
    {
        atlasagent::Logger()->error("Failed to start script (std::exception): {}", e.what());
        cleanup_process();
        return false;
    }
}

void Perfspect::cleanup_process()
{
    if (this->scriptProcess)
    {
        if (this->scriptProcess->running())
        {
            this->scriptProcess->terminate();
            this->scriptProcess->wait();
        }
        this->scriptProcess.reset();
    }
    this->outStream.reset();
    this->errStream.reset();
}

std::optional<std::string> Perfspect::readOutputNew()
{
    if (this->outStream == nullptr)
    {
        atlasagent::Logger()->debug("No output stream available for checking");
        return std::nullopt;
    }

    if (this->outStream->rdbuf()->in_avail() <= 0)
    {
        return std::nullopt; // No data available
    }

    std::string line;
    std::getline(*this->outStream, line);
    if (this->firstIteration == true)
    {
        std::getline(*this->outStream, line);
        this->firstIteration = false;
    }
    return line;
}

std::optional<PerfspectData> parsePerfspectLine(const std::string& line)
{
    std::istringstream ss(line);
    std::string item;
    std::vector<std::string> parts;

    while (std::getline(ss, item, ','))
    {
        parts.push_back(item);
    }

    if (parts.size() != 10)
    {
        return std::nullopt; // Invalid line format
    }

    PerfspectData data{};
    data.cpuFrequency = std::stof(parts[4]);
    data.cyclesPerSecond = std::stof(parts[5]);
    data.instructionsPerSecond = std::stof(parts[6]);
    data.l2CacheMissesPerSecond = std::stof(parts[7]);

    return data;
}

void Perfspect::SendMetrics(PerfspectData data)
{
    detail::perfspectGauge(this->registry_, "cpu.perfspect.frequency").Set(data.cpuFrequency);
    detail::perfspectCounter(this->registry_, "cpu.perfspect.totalCycles").Increment(data.cyclesPerSecond * 5);
    detail::perfspectCounter(this->registry_, "cpu.perfspect.totalInstructions").Increment(data.instructionsPerSecond * 5);
    detail::perfspectCounter(this->registry_, "cpu.perfspect.totalCacheMisses").Increment(data.l2CacheMissesPerSecond * 5);
}

bool Perfspect::gather_metrics()
{
    atlasagent::Logger()->info("Checking for Perfspect metrics...");

    // If the perfspect process has not been started or is no longer running start it
    if (this->scriptProcess == nullptr || this->scriptProcess->running() == false)
    {
        atlasagent::Logger()->info("No active perfspect process found, starting a new one");
        if (this->start_script() == false)
        {
            atlasagent::Logger()->error("Failed to start perfspect process");
            return false;
        }
        return true;
    }

    // Check for available output and read it if present
    auto availableOutput = this->readOutputNew();
    if (availableOutput.has_value() == false)
    {
        atlasagent::Logger()->info("No new perfspect output available yet");
        return false;
    }

    // Parse the output line into a struct of data we can report
    auto parsedDataOpt = parsePerfspectLine(availableOutput.value());
    if (parsedDataOpt.has_value() == false)
    {
        atlasagent::Logger()->error("Failed to parse perfspect output line: {}", availableOutput.value());
        return false;
    }

    SendMetrics(parsedDataOpt.value());
    return true;
}