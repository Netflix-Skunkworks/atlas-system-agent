#include "perfspect.h"
#include <lib/logger/src/logger.h>

#include <boost/process.hpp>

bool valid_instance() {}

bool Perfspect::start_script() {
  try {
    std::string fullBinaryPath = std::string(PerfspectConstants::BinaryLocation) + "/" + PerfspectConstants::BinaryName;
    this->scriptProcess = boost::process::child(fullBinaryPath,
                                                PerfspectConstants::arguments,
                                                boost::process::std_out > this->outStream);
    this->scriptStarted = true;
    return true;
  } catch (const boost::process::process_error& e) {
    atlasagent::Logger()->error("Failed to start script: {}", e.what());
    return false;
  }
}

void Perfspect::read_output(std::vector<std::string>& perfspectOutput) {
  if (!this->scriptStarted || !this->scriptProcess.running()) {
    this->scriptStarted = false;
    atlasagent::Logger()->error("Perfspect script process has died.");
    this->start_script();
    return false;
  }

  std::string line;
  while (std::getline(this->outStream, line)) {
    if (line.empty() == true) {
      continue;  // Skip empty lines
    }

    atlasagent::Logger()->info("Perfspect output: {}", line);
    perfspectOutput.push_back(line);
  }
}


bool Perfspect::gather_metrics() {
  if (this->scriptStarted == false) {
    this->start_script();
    return this->scriptStarted;
  }

  // For now, we will just log that the method was called.
  atlasagent::Logger()->info("Gathering Perfspect metrics...");

  std::vector<std::string> perfspectOutput;
  if (false == this->read_output(pefspectOutput)) {
    return false;
  }

  // Return true to indicate success
  return true;
}