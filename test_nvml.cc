#include "lib/nvml.h"
#include "lib/logger.h"
#include <iostream>

using std::cout;
using atlasagent::Nvml;
using atlasagent::NvmlDeviceHandle;
using atlasagent::Logger;

int main() {
  atlasagent::UseConsoleLogger();
  auto logger = Logger();
  logger->info("Initializing nvml");
  try {
    Nvml nvml;
    logger->info("Done");

    unsigned int count;
    if (!nvml.get_count(&count)) {
      return 1;
    }

    logger->info("Device count = {}", count);
    for (auto i = 0u; i < count; ++i) {
      NvmlDeviceHandle handle;
      if (nvml.get_by_index(i, &handle)) {
        logger->info("Device {}: ", i);
        std::string name;
        if (nvml.get_name(handle, &name)) {
          logger->info("  Name={}", name);
        }
        atlasagent::NvmlMemory memory;
        if (nvml.get_memory_info(handle, &memory)) {
          logger->info("  Mem total={}, free={}, used={}", memory.total, memory.free, memory.used);
        }
        atlasagent::NvmlUtilization utilization;
        if (nvml.get_utilization_rates(handle, &utilization)) {
          logger->info("  gpu={} memory={}", utilization.gpu, utilization.memory);
        }
        atlasagent::NvmlPerfState state;
        if (nvml.get_performance_state(handle, &state)) {
          logger->info("  perf state={}/15", state);
        }
        unsigned int temp;
        if (nvml.get_temperature(handle, &temp)) {
          logger->info("  temperature={}", temp);
        }
        unsigned int speed;
        if (nvml.get_fan_speed(handle, &speed)) {
          logger->info("  fan speed={}", speed);
        }
      }
    }
  } catch (const atlasagent::NvmlException& exception) {
    Logger()->error("Caught NvmlException {}", exception.what());
  }
}