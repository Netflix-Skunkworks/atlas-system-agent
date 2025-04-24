#include <iostream>
#include <fstream>

#include "absl/strings/str_split.h"
#include "dcgm_stats.h"
#include <lib/util/src/util.h>

using atlasagent::GetLogger;
using atlasagent::Logger;

template class GpuMetricsDCGM<atlasagent::TaggingRegistry>;
template class GpuMetricsDCGM<spectator::TestRegistry>;

bool parse_lines(const std::vector<std::string>& lines,
                 std::map<int, std::vector<double>>& dataMap) try {
  if (lines.size() < DCGMConstants::RequiredLines) {
    return false;
  }

  for (unsigned int i = DCGMConstants::DataStartLineIndex; i < lines.size(); i++) {
    auto line = lines.at(i);
    std::vector<std::string> tokens = absl::StrSplit(line, ' ', absl::SkipWhitespace());

    if (tokens.size() != DCGMConstants::ExpectedCountOfTokens) {
      return false;
    }

    auto gpuId = std::stoi(tokens.at(DCGMConstants::GPUIdTokenIndex));
    for (unsigned int j = DCGMConstants::DataStartTokenIndex; j < tokens.size(); j++) {
      dataMap[gpuId].push_back(std::stod(tokens[j]));
    }

    if (dataMap[gpuId].size() != DCGMConstants::ExpectedCountOfProfileValues) {
      return false;
    }
  }
  return true;
} catch (const std::exception& e) {
  atlasagent::Logger()->error("Exception thrown in ParseLines: {}", e.what());
  return false;
}

inline std::vector<std::string> execute_dcgmi() try {
  static const auto command =
      std::string(DCGMConstants::dcgmiPath) + " " + std::string(DCGMConstants::dcgmiArgs);
  return atlasagent::read_output_lines(command.data(), 5000);
} catch (const std::exception& e) {
  atlasagent::Logger()->error("Exception thrown in ExecuteDCGMI: {}", e.what());
  return std::vector<std::string>();
}

template <class Reg>
bool GpuMetricsDCGM<Reg>::update_metrics(std::map<int, std::vector<double>>& dataMap) {
  if (this->registry_ == nullptr) {
    return false;
  }
  for (const auto& [gpuId, data] : dataMap) {
    for (unsigned int i = 0; i < data.size(); i++) {
      double value = data.at(i);
      switch (i) {
        case 0:
          detail::gauge(registry_, "gpu.dcgm.graphicsEngineActivity", gpuId)->Set(value * DCGMConstants::PercentileConversion);
          break;
        case 1:
          detail::gauge(registry_, "gpu.dcgm.sm", gpuId, "activity")->Set(value * DCGMConstants::PercentileConversion);
          break;
        case 2:
          detail::gauge(registry_, "gpu.dcgm.sm", gpuId, "occupancy")->Set(value * DCGMConstants::PercentileConversion);
          break;
        case 3:
          detail::gauge(registry_, "gpu.dcgm.tensorCoresUtilization", gpuId)->Set(value * DCGMConstants::PercentileConversion);
          break;
        case 4:
          detail::gauge(registry_, "gpu.dcgm.memoryBandwidthUtilization", gpuId)->Set(value * DCGMConstants::PercentileConversion);
          break;
        case 5:
          detail::gauge(registry_, "gpu.dcgm.pipeUtilization", gpuId, "fp32")->Set(value * DCGMConstants::PercentileConversion);
          break;
        case 6:
          detail::gauge(registry_, "gpu.dcgm.pipeUtilization", gpuId, "fp16")->Set(value * DCGMConstants::PercentileConversion);
          break;
        case 7:
          detail::counter(registry_, "gpu.dcgm.pcie.bytes", gpuId, "out")->Add(value * DCGMConstants::BytesConversion);
          break;
        case 8:
          detail::counter(registry_, "gpu.dcgm.pcie.bytes", gpuId, "in")->Add(value * DCGMConstants::BytesConversion);
          break;
        case 9:
          detail::counter(registry_, "gpu.dcgm.nvlink.bytes", gpuId, "out")->Add(value * DCGMConstants::BytesConversion);
          break;
        case 10:
          detail::counter(registry_, "gpu.dcgm.nvlink.bytes", gpuId, "in")->Add(value * DCGMConstants::BytesConversion);
          break;
        default:
          Logger()->error("Unhandled field type provided in update_metrics.");
          break;
      }
    }
  }
  return true;
}

template <class Reg>
bool GpuMetricsDCGM<Reg>::gather_metrics() {
  Logger()->debug("Attempting to gather DCGM metrics");

  auto lines = execute_dcgmi();

  std::map<int, std::vector<double>> dataMap;

  if (false == parse_lines(lines, dataMap)) {
    Logger()->error("Failure to parse DCGMI output");
    return false;
  }

  if (false == update_metrics(dataMap)) {
    Logger()->error("Failure to update DCGMI metrics in registry");
    return false;
  }

  return true;
}