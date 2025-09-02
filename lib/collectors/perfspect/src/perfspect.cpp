#include "perfspect.h"
#include <lib/logger/src/logger.h>
#include <lib/util/src/util.h>

#include <boost/process.hpp>
#include <boost/asio.hpp>

#include <string>
#include <string_view>
#include <vector>
#include <algorithm>
#include <sstream>
#include <fstream>
#include <chrono>


Perfspect::Perfspect(Registry* registry, std::pair<char, char> instanceInfo) 
    : registry_(registry), isAmd(instanceInfo.second == 'a'), version(instanceInfo.first)
{
    atlasagent::Logger()->info("Perfspect initialized for instance type: {}{}", instanceInfo.first, instanceInfo.second);
};

std::optional<std::pair<char, char>> Perfspect::IsValidInstance()
{
    if (!atlasagent::is_file_present(PerfspectConstants::BinaryLocation))
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

bool Perfspect::StartScript() try
{
    // Clean up any existing process first
    CleanupProcess();

    std::string fullBinaryPath = std::string(PerfspectConstants::BinaryLocation) + "/" + PerfspectConstants::BinaryName;
    
    // Create async pipe for stdout
    this->asyncPipe = std::make_unique<boost::process::async_pipe>(this->ioContext);

    // Select the appropriate event and metric file paths based on processor type
    const char* eventfilePath = this->isAmd ? PerfspectConstants::eventfilePathAmd : PerfspectConstants::eventfilePathIntel;
    const char* metricfilePath = this->isAmd ? PerfspectConstants::metricfilePathAmd : PerfspectConstants::metricfilePathIntel;

    this->scriptProcess = std::make_unique<boost::process::child>(
        fullBinaryPath, PerfspectConstants::command, PerfspectConstants::eventfileFlag,
        eventfilePath, PerfspectConstants::metricfileFlag, metricfilePath,
        PerfspectConstants::intervalFlag, PerfspectConstants::intervalValue, PerfspectConstants::liveFlag,
        boost::process::std_out > *this->asyncPipe, 
        boost::process::std_err > boost::process::null);
        
    // Start async reading
    AsyncRead();
        
    atlasagent::Logger()->info("Perfspect process started successfully with PID: {}", this->scriptProcess->id());
    return true;
}
catch (const std::exception& e)
{
    atlasagent::Logger()->error("Failed to start script: {}", e.what());
    CleanupProcess();
    return false;
}

void Perfspect::CleanupProcess()
{
    if (this->asyncPipe)
    {
        this->asyncPipe->close();
        this->asyncPipe.reset();
    }
    
    if (this->scriptProcess)
    {
        if (this->scriptProcess->running())
        {
            this->scriptProcess->terminate();
            this->scriptProcess->wait();
        }
        this->scriptProcess.reset();
    }
    
    // Reset async state
    this->hasNewLine = false;
    this->pendingLine.clear();
}

void Perfspect::ExtractLine(const boost::system::error_code& ec, std::size_t bytes_transferred)
{
    if (ec.failed())
    {
        atlasagent::Logger()->debug("Async read failed: {}", ec.message());
        return;
    }
    
    // Extract the line from the buffer
    std::istream is(&this->buffer);
    std::getline(is, this->pendingLine);
    if (this->firstIteration)
    {
        std::cout << "first" << pendingLine << std::endl;
        std::getline(is, this->pendingLine);
        this->firstIteration = false;
    }

    std::cout << pendingLine << std::endl;
    
    // Mark that we have a new line ready
    this->hasNewLine = true;
        
    // Continue reading the next line
    if (this->asyncPipe)
    {
        AsyncRead();
    }
}

void Perfspect::AsyncRead()
{
    boost::asio::async_read_until(*this->asyncPipe, this->buffer, '\n',
        [this](const boost::system::error_code& ec, std::size_t bytes_transferred) {
            this->ExtractLine(ec, bytes_transferred);
        });
}

std::optional<std::string> Perfspect::ReadOutput()
{
    // Process any pending async operations
    this->ioContext.poll();
    
    if (this->hasNewLine == false)
    {
        return std::nullopt;
    }

    std::string result = this->pendingLine;
    
    // Reset state after reading
    this->hasNewLine = false;
    this->pendingLine.clear();
    
    return result;
}

std::optional<PerfspectData> ParsePerfspectLine(const std::string& line)
{
    std::istringstream ss(line);
    std::string item;
    std::vector<std::string> parts;

    while (std::getline(ss, item, ','))
    {
        parts.push_back(item);
    }

    if (parts.size() != 8)
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
    atlasagent::Logger()->info("Sending Perfspect metrics:");
    atlasagent::Logger()->info("Cpu Frequency: {}", data.cpuFrequency);
    atlasagent::Logger()->info("Cycles Per Second: {} * 5 = {}", data.cyclesPerSecond, data.cyclesPerSecond * 5);
    atlasagent::Logger()->info("Instructions Per Second: {} * 5 = {}", data.instructionsPerSecond, data.instructionsPerSecond * 5);
    atlasagent::Logger()->info("L2 Cache Misses Per Second: {} * 5 = {}", data.l2CacheMissesPerSecond, data.l2CacheMissesPerSecond * 5);
    
    
    detail::perfspectGauge(this->registry_, "cpu.perfspect.frequency").Set(data.cpuFrequency);
    detail::perfspectCounter(this->registry_, "cpu.perfspect.totalCycles").Increment(data.cyclesPerSecond * 5);
    detail::perfspectCounter(this->registry_, "cpu.perfspect.totalInstructions").Increment(data.instructionsPerSecond * 5);
    detail::perfspectCounter(this->registry_, "cpu.perfspect.totalCacheMisses").Increment(data.l2CacheMissesPerSecond * 5);
}

bool Perfspect::GatherMetrics()
{
    atlasagent::Logger()->info("Checking for Perfspect metrics...");

    // If the perfspect process has not been started or is no longer running start it
    if (!this->scriptProcess || this->scriptProcess->running() == false)
    {
        atlasagent::Logger()->info("No active perfspect process found, starting a new one");
        if (this->StartScript() == false)
        {
            atlasagent::Logger()->error("Failed to start perfspect process");
            return false;
        }
        return true;
    }

    // Check for available output and read it if present
    auto availableOutput = this->ReadOutput();
    if (availableOutput.has_value() == false)
    {
        atlasagent::Logger()->info("No new perfspect output available yet");
        return false;
    }

    // Parse the output line into a struct of data we can report
    auto parsedDataOpt = ParsePerfspectLine(availableOutput.value());
    if (parsedDataOpt.has_value() == false)
    {
        atlasagent::Logger()->error("Failed to parse perfspect output line: {}", availableOutput.value());
        return false;
    }

    SendMetrics(parsedDataOpt.value());
    return true;
}