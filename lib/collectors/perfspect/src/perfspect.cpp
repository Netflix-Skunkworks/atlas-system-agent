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
        
        this->scriptStarted = true;
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
    this->scriptStarted = false;
    this->perfspectOutput.clear();
}

bool Perfspect::read_output()
{
    if (this->outStream == nullptr)
    {
        atlasagent::Logger()->debug("No output stream available for reading");
        return false;
    }

    std::string line;
    while (std::getline(*this->outStream, line))
    {
        if (!line.empty())
        {
            atlasagent::Logger()->info("Agent Read Output from Perfspect: {}", line);
            this->perfspectOutput.push_back(line);
        }
    }

    return true;
}

bool Perfspect::sendMetricsAMD(const std::vector<std::string>& perfspectOutput)
{
	if (perfspectOutput.size() < 13)
	{
		atlasagent::Logger()->info("Not enough perfspect output collected: {}/13 lines", perfspectOutput.size());
		return false;
	}

    struct PerfspectData data{};
    for (unsigned int i = 1; i < 13; i++)
    {
        std::string line = perfspectOutput[i];

        std::vector<std::string> parts;
        std::string part;
        std::istringstream ss(line);
        while (std::getline(ss, part, ','))
        {
            parts.push_back(part);
        }

        data.cpuFrequency += std::stof(parts[4]);
        data.cyclesPerInstruction += std::stof(parts[5]);
        data.gips += std::stof(parts[6]);
        data.l2DataCacheMisses += std::stof(parts[8]);
        data.l2CodeCacheMisses += std::stof(parts[9]);
		atlasagent::Logger()->info("PerfspectLine {}: CPU Freq: {}, CPI: {}, GIPS: {}, L2 Data Cache Misses: {}, L2 Code Cache Misses: {}",
			i, parts[4], parts[5], parts[6], parts[8], parts[9]);
    }

    auto averageCpuFrequency = data.cpuFrequency / 12.0;
    auto averageCPI = data.cyclesPerInstruction / 12.0;
    auto averageGIPS = data.gips / 12.0;
    auto averageL2DataCacheMisses = data.l2DataCacheMisses / 12.0;
    auto averageL2CodeCacheMisses = data.l2CodeCacheMisses / 12.0;
	

    detail::perfspectGauge(this->registry_, PerfspectConstants::cpuFreqMetricName).Set(averageCpuFrequency);
    detail::perfspectGauge(this->registry_, PerfspectConstants::cpiMetricName).Set(averageCPI);
    detail::perfspectGauge(this->registry_, PerfspectConstants::l2DataCacheMissMetricName).Set(averageL2DataCacheMisses);
    detail::perfspectGauge(this->registry_, PerfspectConstants::l2CodeCacheMissMetricName).Set(averageL2CodeCacheMisses);

    auto totalInstructions = averageGIPS * 60 * 1'000'000'000;
    detail::perfspectCounter(this->registry_, PerfspectConstants::totalInstructionsMetricName).Increment(totalInstructions);

    auto totalL2DataCacheMisses = (averageL2DataCacheMisses / 1000.0) * totalInstructions;
    detail::perfspectCounter(this->registry_, PerfspectConstants::totalL2DataCacheMissesMetricName).Increment(totalL2DataCacheMisses);

    auto totalL2CodeCacheMisses = (averageL2CodeCacheMisses / 1000.0) * totalInstructions;
    detail::perfspectCounter(this->registry_, PerfspectConstants::totalL2CodeCacheMissesMetricName).Increment(totalL2CodeCacheMisses);

	atlasagent::Logger()->info("Avg CPU Freq: {}, Avg CPI: {}, Avg GIPS: {}, Avg L2 Data Cache Misses: {}, Avg L2 Code Cache Misses: {}",
		averageCpuFrequency, averageCPI, averageGIPS, averageL2DataCacheMisses, averageL2CodeCacheMisses);
	atlasagent::Logger()->info("Total Instructions: {}, Total L2 Data Cache Misses: {}, Total L2 Code Cache Misses: {}",
		totalInstructions, totalL2DataCacheMisses, totalL2CodeCacheMisses);

    return true;
}

bool Perfspect::gather_metrics()
{
    atlasagent::Logger()->info("Checking for Perfspect metrics...");

    // If the script hasn't been started start it now (required for first run)
    if (this->scriptStarted == false)
    {
        atlasagent::Logger()->info("Starting new 60-second perfspect collection cycle");
        return this->start_script();
    }
    
    // Read available output, restart script if reading fails
    if (this->read_output() == false)
    {
        atlasagent::Logger()->info("Failed to read perfspect output");
        this->start_script();
        return false;
    }

	// Send the metrics for the previous iteration and restart the script for the next iteration
	if (false == this->sendMetricsAMD(this->perfspectOutput))
	{
		atlasagent::Logger()->info("Failed to send perfspect metrics");
		this->start_script();
		return false;
	}

    return true;
}