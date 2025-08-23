#include "perfspect.h"
#include <lib/logger/src/logger.h>

#include <boost/process.hpp>

#include <string>
#include <string_view>
#include <vector>
#include <algorithm>
#include <sstream>

bool valid_instance() {
  // TODO: Implement actual validation logic
  return true;
}

bool Perfspect::start_script() {
  try {
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
        PerfspectConstants::eventfilePath, PerfspectConstants::metricfileFlag,
        PerfspectConstants::metricfilePath, PerfspectConstants::durationFlag,
        PerfspectConstants::durationValue, PerfspectConstants::liveFlag,
        boost::process::std_out > *this->outStream, boost::process::std_err > *this->errStream);
    this->scriptStarted = true;
    atlasagent::Logger()->info("Perfspect process started successfully with PID: {}",
                               this->scriptProcess->id());
    return true;
  } catch (const boost::process::process_error& e) {
    atlasagent::Logger()->error("Failed to start script: {}", e.what());
    cleanup_process();
    return false;
  } catch (const std::exception& e) {
    atlasagent::Logger()->error("Failed to start script (std::exception): {}", e.what());
    cleanup_process();
    return false;
  }
}

void Perfspect::cleanup_process() {
  if (this->scriptProcess) {
    if (this->scriptProcess->running()) {
      this->scriptProcess->terminate();
      this->scriptProcess->wait();
    }
    this->scriptProcess.reset();
  }
  this->outStream.reset();
  this->errStream.reset();
  this->scriptStarted = false;
}

bool Perfspect::read_output(std::vector<std::string>& perfspectOutput) {
  std::string line;
  int stdout_lines = 0;

  if (this->outStream == nullptr) {
    atlasagent::Logger()->debug("No output stream available for reading");
    return false;
  }

  while (std::getline(*this->outStream, line)) {
    stdout_lines++;
    if (line.empty() == true) {
      continue;  // Skip empty lines
    }

    atlasagent::Logger()->info("Perfspect output: {}", line);
    perfspectOutput.push_back(line);
  }

  return true;
}

void printArray(std::array<float, 5> arr) {
  std::string x = "";
  for (const auto& val : arr) {
    x += std::to_string(val) + " ";
  }
  std::cout << x << std::endl;
}

bool Perfspect::sendMetricsAMD(const std::vector<std::string>& perfspectOutput) {
  if (perfspectOutput.size() != 13) {
    atlasagent::Logger()->info("Unexpected perfspect output size: {}", perfspectOutput.size());
    return false;
  }

  std::array<float, 5> metrics{0, 0, 0, 0, 0};
  std::array<std::string, 4> metricNames{
      "perfspect.cpu.frequency", "perfspect.cpu.cyclesPerInstruction",
      "perfspect.cpu.l2DataCacheMisses", "perfspect.cpu.l2CodeCacheMisses"};
  for (unsigned int i = 1; i < perfspectOutput.size(); i++) {
    std::string data = perfspectOutput[i];

    // Split the CSV line manually (C++17 compatible)
    std::vector<std::string> parts;
    std::string part;
    std::istringstream ss(data);
    while (std::getline(ss, part, ',')) {
      parts.push_back(part);
    }

    if (parts.size() < 10) {
      atlasagent::Logger()->warn("Insufficient CSV fields in perfspect output: {}", data);
      continue;
    }

    auto cpuFreq = std::stof(parts[4]);
    auto cpi = std::stof(parts[5]);
    auto gips = std::stof(parts[6]);
    auto dataCacheMiss = std::stof(parts[8]);
    auto codeCacheMiss = std::stof(parts[9]);
    metrics[0] += cpuFreq;
    metrics[1] += cpi;
    metrics[2] += dataCacheMiss;
    metrics[3] += codeCacheMiss;
    metrics[4] += gips;
    printArray(metrics);
  }

  for (unsigned int i = 0; i < 4; i++) {
    auto finalmetric = metrics[i] / perfspectOutput.size();
    detail::perfspectGauge(this->registry_, metricNames.at(i)).Set(finalmetric);
  }

  // Calculate average GIPS (Giga Instructions Per Second)
  auto avgGips = metrics[4] / perfspectOutput.size();
  // Calculate total instructions over 60 seconds
  auto totalInstructions = avgGips * 60 * 1'000'000'000;
  detail::perfspectCounter(this->registry_, "perfspect.cpu.totalInstructions").Increment(totalInstructions);
  atlasagent::Logger()->info("Total Instructions in last 60 seconds: {}", totalInstructions);

  // Calculate total L2 Data Cache misses for the entire minute
  auto avgL2DataMissRate = metrics[2] / perfspectOutput.size();
  auto totalL2DataMisses = (avgL2DataMissRate / 1000.0) * totalInstructions;
  detail::perfspectCounter(this->registry_, "perfspect.cpu.totalL2DataCacheMisses").Increment(totalL2DataMisses);
  atlasagent::Logger()->info("Total L2 Data Cache Misses in last 60 seconds: {}", totalL2DataMisses);


  auto avgL2CodeMissRate = metrics[3] / perfspectOutput.size();
  auto totalL2CodeMisses = (avgL2CodeMissRate / 1000.0) * totalInstructions;
  detail::perfspectCounter(this->registry_, "perfspect.cpu.totalL2CodeCacheMisses").Increment(totalL2CodeMisses);
  atlasagent::Logger()->info("Total L2 Code Cache Misses in last 60 seconds: {}", totalL2CodeMisses);
  
  return true;
}

bool Perfspect::process_completed() {
  if (this->scriptProcess == nullptr) {
    atlasagent::Logger()->info("No script process available");
    return false;
  }

  if (this->scriptProcess->running() == true) {
    atlasagent::Logger()->info("Previous perfspect process still running");
    return false;
  }

  int exit_code = this->scriptProcess->exit_code();
  if (exit_code != 0) {
    atlasagent::Logger()->info("Previous perfspect process failed with exit code: {}", exit_code);
    return false;
  }
  return true;
}

bool Perfspect::gather_metrics() {
  atlasagent::Logger()->info("Gathering Perfspect metrics...");

  std::vector<std::string> perfspectOutput;
  if (this->scriptStarted == false) {
    atlasagent::Logger()->info("Starting new 60-second perfspect collection cycle");
    return this->start_script();
  }

  if (this->process_completed() == false) {
    atlasagent::Logger()->info("Previous perfspect process failed to complete successfully");
    this->start_script();
    return false;
  }

  this->read_output(perfspectOutput);
  if (false == this->sendMetricsAMD(perfspectOutput)) {
    atlasagent::Logger()->info("Failed to send perfspect metrics");
    this->start_script();
    return false;
  }
  this->start_script();

  return true;  // Process still running, wait for next cycle
}