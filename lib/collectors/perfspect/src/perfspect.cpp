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
#include <filesystem>


Perfspect::Perfspect(Registry* registry, const std::pair<char, char> &instanceInfo) 
    : registry_(registry), isAmd(instanceInfo.second == 'a'), version(instanceInfo.first)
{
    atlasagent::Logger()->debug("Perfspect initialized for instance type: {}{}", instanceInfo.first, instanceInfo.second);
};

std::optional<std::pair<char, char>> Perfspect::IsValidInstance()
{
    if (atlasagent::is_file_present(PerfspectConstants::BinaryLocation) == false)
    {
        atlasagent::Logger()->debug("Perfspect binary not found at {}", PerfspectConstants::BinaryLocation);
        return std::nullopt;
    }

    std::ifstream file("/sys/devices/virtual/dmi/id/product_name");
    std::string product_name;
    if (!file.is_open() || !std::getline(file, product_name))
    {
        atlasagent::Logger()->error("Failed to read product name from DMI");
        return std::nullopt;
    }

    size_t dot = product_name.find('.');
    if (dot == std::string::npos || dot < 3) 
    {
        atlasagent::Logger()->debug("Invalid product name format: {}", product_name);
        return std::nullopt;
    }

    char generation = product_name[1];
    char processor = product_name[2];
    
    bool valid = std::isdigit(generation) && (generation >= '7') && (processor == 'i' || processor == 'a');
    
    if (valid == false) 
    {
        atlasagent::Logger()->debug("Invalid instance: {} (gen: {}, proc: {})", product_name, generation, processor);
        return std::nullopt;
    }
    return std::make_pair(generation, processor);
}

bool Perfspect::StartScript() try
{
    // Clean up any existing process first
    CleanupProcess();

    std::filesystem::path fullBinaryPath = std::filesystem::path(PerfspectConstants::BinaryLocation) / PerfspectConstants::BinaryName;
    
    // Create async pipe for stdout
    this->asyncPipe = std::make_unique<boost::process::async_pipe>(this->ioContext);

    // Select the appropriate event and metric file paths based on processor type
    const char* eventfilePath = this->isAmd ? PerfspectConstants::eventfilePathAmd : PerfspectConstants::eventfilePathIntel;
    const char* metricfilePath = this->isAmd ? PerfspectConstants::metricfilePathAmd : PerfspectConstants::metricfilePathIntel;

    this->scriptProcess = std::make_unique<boost::process::child>(
        fullBinaryPath.string(), PerfspectConstants::command, PerfspectConstants::eventfileFlag,
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
    atlasagent::Logger()->error("Failed to start PerfSpect: {}", e.what());
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
    
    this->pendingLine.clear();
}

void Perfspect::ExtractLine(const boost::system::error_code& ec, std::size_t bytes_transferred)
{
    this->lastAsyncError = ec;
    if (this->lastAsyncError.failed())
    {
        atlasagent::Logger()->error("Async read failed: {}", lastAsyncError.message());
        return;
    }
    
    // Extract the line from the buffer
    std::istream is(&this->buffer);
    std::getline(is, this->pendingLine);
    if (this->firstIteration)
    {
        atlasagent::Logger()->debug("First iteration skipping: {}", this->pendingLine);
        std::getline(is, this->pendingLine);
        this->firstIteration = false;
    }
    
    atlasagent::Logger()->debug("Extracted Line: {}", this->pendingLine);
        
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

ReadResult Perfspect::ReadOutput()
{
    // Process any pending async operations
    this->ioContext.poll();

    if (this->lastAsyncError.failed() || this->pendingLine.empty() == true)
    {
        return {std::nullopt, this->lastAsyncError};
    }

    ReadResult result {this->pendingLine, this->lastAsyncError};
    
    this->pendingLine.clear();
    
    return result;
}

std::optional<PerfspectData> ParsePerfspectLine(const std::string& line) try
{
    std::vector<std::string_view> parts;
    std::string_view sv(line);
    
    size_t start = 0;
    size_t pos = 0;
    
    while ((pos = sv.find(',', start)) != std::string_view::npos) {
        parts.emplace_back(sv.substr(start, pos - start));
        start = pos + 1;
    }
    parts.emplace_back(sv.substr(start));
    
    if (parts.size() != 8) {
        return std::nullopt;
    }
    
    PerfspectData data{};
    data.cpuFrequency = std::stof(std::string(parts[4]));
    data.cyclesPerSecond = std::stof(std::string(parts[5]));
    data.instructionsPerSecond = std::stof(std::string(parts[6]));
    data.l2CacheMissesPerSecond = std::stof(std::string(parts[7]));
    
    return data;
}
catch (const std::exception& e)
{
    atlasagent::Logger()->error("Exception parsing PerfSpect line: {}", e.what());
    return std::nullopt;
}

void Perfspect::SendMetrics(const PerfspectData &data)
{
    atlasagent::Logger()->info("Sending: Frequency: {}, Cycles/sec: {} * 5 = {}, Instructions/sec: {} * 5 = {}, L2 Cache Misses/sec: {} * 5 = {}", 
        data.cpuFrequency, data.cyclesPerSecond, data.cyclesPerSecond * 5, 
        data.instructionsPerSecond, data.instructionsPerSecond * 5,
        data.l2CacheMissesPerSecond, data.l2CacheMissesPerSecond * 5);
    
    
    detail::perfspectGauge(this->registry_, PerfspectConstants::metricFrequency).Set(data.cpuFrequency);
    detail::perfspectCounter(this->registry_, PerfspectConstants::metricCycles).Increment(data.cyclesPerSecond * PerfspectConstants::interval);
    detail::perfspectCounter(this->registry_, PerfspectConstants::metricInstructions).Increment(data.instructionsPerSecond * PerfspectConstants::interval);
    detail::perfspectCounter(this->registry_, PerfspectConstants::metricL2CacheMisses).Increment(data.l2CacheMissesPerSecond * PerfspectConstants::interval);
}

bool Perfspect::GatherMetrics()
{
    // If the perfspect process has not been started or is no longer running start it
    if (!this->scriptProcess || this->scriptProcess->running() == false)
    {
        atlasagent::Logger()->debug("No active PerfSpect process found, starting a new one");
        this->StartScript();
        return true;
    }

    atlasagent::Logger()->debug("Checking for PerfSpect metrics...");
    auto result = this->ReadOutput();
    
    // If there was an error reading, restart the PerfSpect process
    if (result.error.failed())
    {
        atlasagent::Logger()->error("Failed to read PerfSpect output: {}", result.error.message());
        bool restartSuccess = this->StartScript();
        atlasagent::Logger()->debug("Restarting PerfSpect process: {}", restartSuccess ? "Success" : "Failed");
        return false;
    }

    // If no data was returned, continue waiting for more output
    if (result.data.has_value() == false)
    {
        atlasagent::Logger()->debug("No new perfspect output available yet");
        return true;
    }
    
    // Parse the output line into a struct of data we can report
    auto parsedDataOpt = ParsePerfspectLine(result.data.value());
    if (parsedDataOpt.has_value() == false)
    {
        atlasagent::Logger()->error("Failed to parse perfspect output line: {}", result.data.value());
        return false;
    }

    SendMetrics(parsedDataOpt.value());
    return true;
}