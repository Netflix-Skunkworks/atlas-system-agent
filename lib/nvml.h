#pragma once

#include <memory>
#include <stdexcept>

namespace atlasagent {
typedef void* NvmlDeviceHandle;  // opaque handle to a device

typedef int NvmlPerfState;  // performance state 0-15, 0 = fastest state

class NvmlException : public std::runtime_error {
 public:
  explicit NvmlException(const char* error) : runtime_error(error) {}
  explicit NvmlException(int error);
};

struct NvmlMemory {
  unsigned long long total;
  unsigned long long free;
  unsigned long long used;
};

struct NvmlUtilization {
  unsigned int gpu;
  unsigned int memory;
};

class Nvml {
 public:
  Nvml();
  ~Nvml() noexcept;
  Nvml(const Nvml& other) = delete;

  bool get_count(unsigned int* count) noexcept;
  bool get_by_index(unsigned int index, NvmlDeviceHandle* device) noexcept;

  bool get_memory_info(NvmlDeviceHandle device, NvmlMemory* memory) noexcept;
  bool get_utilization_rates(NvmlDeviceHandle device, NvmlUtilization* utilization) noexcept;
  bool get_performance_state(NvmlDeviceHandle device, NvmlPerfState* perf_state) noexcept;
  bool get_temperature(NvmlDeviceHandle device, unsigned int* temperature) noexcept;
  bool get_name(NvmlDeviceHandle device, std::string* name) noexcept;
  bool get_fan_speed(NvmlDeviceHandle device, unsigned int* speed) noexcept;
};

}  // namespace atlasagent
