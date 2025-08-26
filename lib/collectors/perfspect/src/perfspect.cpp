#include "perfspect.h"
#include <lib/logger/src/logger.h>

#include <boost/process.hpp>

#include <string>
#include <string_view>
#include <vector>
#include <algorithm>
#include <sstream>

bool valid_instance()
{
    // TODO: Implement actual validation logic
    return true;
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

        // Use individual argument constants with --live and --duration 60
        this->scriptProcess = std::make_unique<boost::process::child>(
            fullBinaryPath, PerfspectConstants::command, PerfspectConstants::eventfileFlag,
            PerfspectConstants::eventfilePath, PerfspectConstants::metricfileFlag, PerfspectConstants::metricfilePath,
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
    std::string line;
    int stdout_lines = 0;

    if (this->outStream == nullptr)
    {
        atlasagent::Logger()->debug("No output stream available for reading");
        return false;
    }

    while (std::getline(*this->outStream, line))
    {
        stdout_lines++;
        if (line.empty() == true)
        {
            continue;  // Skip empty lines
        }

        atlasagent::Logger()->info("Agent Read Output from Perfspect: {}", line);
        this->perfspectOutput.push_back(line);
    }

    return true;
}

bool Perfspect::sendMetricsAMD(const std::vector<std::string>& perfspectOutput)
{
    struct PerfspectData data{};
    for (unsigned int i = 0; i < 12; i++)
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
    }

    auto averageCpuFrequency = data.cpuFrequency / 12.0;
    auto averageCPI = data.cyclesPerInstruction / 12.0;
    auto averageGIPS = data.gips / 12.0;
    auto averageL2DataCacheMisses = data.l2DataCacheMisses / 12.0;
    auto averageL2CodeCacheMisses = data.l2CodeCacheMisses / 12.0;

    detail::perfspectGauge(this->registry_, PerfspectConstants::cpuFreqMetricName).Set(averageCpuFrequency);
    detail::perfspectGauge(this->registry_, PerfspectConstants::cpiMetricName).Set(averageCPI);
    detail::perfspectGauge(this->registry_, PerfspectConstants::gipsMetricName).Set(averageGIPS);
    detail::perfspectGauge(this->registry_, PerfspectConstants::l2DataCacheMissMetricName).Set(averageL2DataCacheMisses);
    detail::perfspectGauge(this->registry_, PerfspectConstants::l2CodeCacheMissMetricName).Set(averageL2CodeCacheMisses);

    auto totalInstructions = averageGIPS * 60 * 1'000'000'000;
    detail::perfspectCounter(this->registry_, PerfspectConstants::totalInstructionsMetricName).Increment(totalInstructions);
    atlasagent::Logger()->info("Total Instructions in last 60 seconds: {}", totalInstructions);

    auto totalL2DataCacheMisses = (averageL2DataCacheMisses / 1000.0) * totalInstructions;
    detail::perfspectCounter(this->registry_, PerfspectConstants::totalL2DataCacheMissesMetricName).Increment(totalL2DataCacheMisses);
    atlasagent::Logger()->info("Total L2 Data Cache Misses in last 60 seconds: {}", totalL2DataCacheMisses);

    auto totalL2CodeCacheMisses = (averageL2CodeCacheMisses / 1000.0) * totalInstructions;
    detail::perfspectCounter(this->registry_, PerfspectConstants::totalL2CodeCacheMissesMetricName).Increment(totalL2CodeCacheMisses);
    atlasagent::Logger()->info("Total L2 Code Cache Misses in last 60 seconds: {}", totalL2CodeCacheMisses);

    return true;
}

bool Perfspect::gather_metrics()
{
    atlasagent::Logger()->info("Checking for Perfspect metrics...");

    // If the script hasn't been started yet or the process is not running start it
    if (this->scriptStarted == false || this->scriptProcess->running() == false)
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

    // If we have over a minutes worth of output process it
    // if (this->perfspectOutput.size() > 13)
    // {
    //     if (false == this->sendMetricsAMD(this->perfspectOutput))
    //     {
    //         atlasagent::Logger()->info("Failed to send perfspect metrics");
    //         this->start_script();
    //         return false;
    //     }
    //     this->perfspectOutput.clear();  // Clear output after processing
    // }
    // else
    // {
    //     atlasagent::Logger()->info("Insufficient perfspect output lines: {}", this->perfspectOutput.size());
    //     this->start_script();
    //     return false;
    // }

    return true;  // Process still running, wait for next cycle
}