#include "nvml.h"
#include "../../Config/config.h"
#include "../../Logger/logger.h"
#include <dlfcn.h>

namespace atlasagent {

struct sym_handle {
  const char* symbol;
  void* handle;
};

const char* to_string(NvmlRet ret) noexcept {
  switch (ret) {
    case NvmlRet::Success:
      return "The operation was successful";
    case NvmlRet::ErrorUninitialized:
      return "Library not initialized properly";
    case NvmlRet::ErrorInvalidArgument:
      return "A supplied argument is invalid";
    case NvmlRet::ErrorNotSupported:
      return "not supported";
    case NvmlRet::ErrorNoPermission:
      return "The current user does not have permission for operation";
    case NvmlRet::ErrorAlreadyInitialized:
      return "Deprecated error code (5)";
    case NvmlRet::ErrorNotFound:
      return "A query to find an object was unsuccessful";
    case NvmlRet::ErrorInsufficientSize:
      return "An input argument is not large enough";
    case NvmlRet::ErrorInsufficientPower:
      return "A device's external power cables are not properly attached";
    case NvmlRet::ErrorDriverNotLoaded:
      return "NVIDIA driver is not loaded";
    case NvmlRet::ErrorTimeout:
      return "User provided timeout has passed";
    case NvmlRet::ErrorIrqIssue:
      return "NVIDIA Kernel detected an interrupt issue with a GPU";
    case NvmlRet::ErrorLibraryNotFound:
      return "NVML Shared Library couldn't be found or loaded";
    case NvmlRet::ErrorFunctionNotFound:
      return "Local version of NVML doesn't implement this function";
    case NvmlRet::ErrorCorruptedInforom:
      return "infoROM is corrupted";
    case NvmlRet::ErrorGpuIsLost:
      return "The GPU has become inaccessible";
    case NvmlRet::ErrorUnknown:
      return "An interval driver error has occured";
  }
  return "Unknown error";
}

enum NvmlIndex {
  Init,
  Shutdown,
  DeviceGetCount,
  DeviceGetHandleByIndex,
  DeviceGetName,
  DeviceGetPciInfo,
  DeviceGetFanSpeed,
  DeviceGetTemperature,
  DeviceGetUtilizationRates,
  DeviceGetMemoryInfo,
  DeviceGetPerformanceState
};

std::array<sym_handle, 11> nvml_symtab = {{
    {"nvmlInit", nullptr},
    {"nvmlShutdown", nullptr},
    {"nvmlDeviceGetCount", nullptr},
    {"nvmlDeviceGetHandleByIndex", nullptr},
    {"nvmlDeviceGetName", nullptr},
    {"nvmlDeviceGetPciInfo", nullptr},
    {"nvmlDeviceGetFanSpeed", nullptr},
    {"nvmlDeviceGetTemperature", nullptr},
    {"nvmlDeviceGetUtilizationRates", nullptr},
    {"nvmlDeviceGetMemoryInfo", nullptr},
    {"nvmlDeviceGetPerformanceState", nullptr},
}};

NvmlRet Nvml::resolve_symbols() {
  if (nvml_dso_ != nullptr) {
    return NvmlRet::Success;
  }

  const char* library = getenv("NVIDIA_LIBRARY");
  if (library == nullptr) {
    library = kNvidiaLib;
  }

  if ((nvml_dso_ = dlopen(library, RTLD_NOW)) == nullptr) {
    return NvmlRet::ErrorLibraryNotFound;
  }

  fprintf(stderr, "Successfully opened NVIDIA NVML library: %s\n", library);
  return NvmlRet::Success;
}

Nvml::Nvml() {
  auto load = resolve_symbols();
  if (load != NvmlRet::Success) {
    throw NvmlException(load);
  }
}

void Nvml::initialize() {
  void* func;
  for (auto& sh : nvml_symtab) {
    sh.handle = dlsym(nvml_dso_, sh.symbol);
  }

  if ((func = nvml_symtab[NvmlIndex::Init].handle) == nullptr) {
    throw NvmlException("init function not found");
  }

  auto init = reinterpret_cast<NvmlRet (*)()>(func);
  auto res = init();
  if (res != NvmlRet::Success) {
    throw NvmlException(res);
  }
  fprintf(stderr, "Successfully initialized NVIDIA library\n");
}

Nvml::~Nvml() noexcept {
  void* func;
  if ((func = nvml_symtab[NvmlIndex::Shutdown].handle) != nullptr) {
    auto shutdown = reinterpret_cast<NvmlRet (*)()>(func);
    auto ret = shutdown();
    if (ret != NvmlRet::Success) {
      Logger()->info("Unable to shutdown nvml: {}", to_string(ret));
    }
  }
}

static bool to_bool(const char* name, NvmlRet code) {
  if (code == NvmlRet::Success) {
    return true;
  }

  Logger()->warn("Error calling {}: {}", name, to_string(code));
  return false;
}

bool Nvml::get_count(unsigned int* count) noexcept {
  constexpr const char* kFuncName = "get_count";
  void* func;
  if ((func = nvml_symtab[NvmlIndex::DeviceGetCount].handle) != nullptr) {
    auto nvml_get_count = reinterpret_cast<NvmlRet (*)(unsigned int*)>(func);
    return to_bool(kFuncName, nvml_get_count(count));
  }
  return to_bool(kFuncName, NvmlRet::ErrorFunctionNotFound);
}

bool Nvml::get_by_index(unsigned int index, NvmlDeviceHandle* device) noexcept {
  constexpr const char* kFuncName = "get_by_index";
  void* func;
  if ((func = nvml_symtab[NvmlIndex::DeviceGetHandleByIndex].handle) != nullptr) {
    auto nvml_get_by_idx = reinterpret_cast<NvmlRet (*)(unsigned int, NvmlDeviceHandle*)>(func);
    return to_bool(kFuncName, nvml_get_by_idx(index, device));
  }
  return to_bool(kFuncName, NvmlRet::ErrorFunctionNotFound);
}

bool Nvml::get_memory_info(NvmlDeviceHandle device, NvmlMemory* memory) noexcept {
  constexpr const char* kFuncName = "get_memory_info";
  void* func;
  if ((func = nvml_symtab[NvmlIndex::DeviceGetMemoryInfo].handle) != nullptr) {
    auto nvml_get_mem = reinterpret_cast<NvmlRet (*)(NvmlDeviceHandle, NvmlMemory*)>(func);
    return to_bool(kFuncName, nvml_get_mem(device, memory));
  }
  return to_bool(kFuncName, NvmlRet::ErrorFunctionNotFound);
}

bool Nvml::get_utilization_rates(NvmlDeviceHandle device, NvmlUtilization* utilization) noexcept {
  constexpr const char* kFuncName = "get_utilization_rates";
  void* func;
  if ((func = nvml_symtab[NvmlIndex::DeviceGetUtilizationRates].handle) != nullptr) {
    auto nvml_get_rates = reinterpret_cast<NvmlRet (*)(NvmlDeviceHandle, NvmlUtilization*)>(func);
    return to_bool(kFuncName, nvml_get_rates(device, utilization));
  }
  return to_bool(kFuncName, NvmlRet::ErrorFunctionNotFound);
}

bool Nvml::get_performance_state(NvmlDeviceHandle device, NvmlPerfState* perf_state) noexcept {
  constexpr const char* kFuncName = "get_performance_state";
  void* func;
  if ((func = nvml_symtab[NvmlIndex::DeviceGetPerformanceState].handle) != nullptr) {
    auto nvml_get_perf = reinterpret_cast<NvmlRet (*)(NvmlDeviceHandle, NvmlPerfState*)>(func);
    return to_bool(kFuncName, nvml_get_perf(device, perf_state));
  }
  return to_bool(kFuncName, NvmlRet::ErrorFunctionNotFound);
}

bool Nvml::get_temperature(NvmlDeviceHandle device, unsigned int* temperature) noexcept {
  constexpr const char* kFuncName = "get_temperature";
  constexpr int kTempGPU = 0;
  void* func;
  if ((func = nvml_symtab[NvmlIndex::DeviceGetTemperature].handle) != nullptr) {
    auto nvml_get_temp = reinterpret_cast<NvmlRet (*)(NvmlDeviceHandle, int, unsigned int*)>(func);
    return to_bool(kFuncName, nvml_get_temp(device, kTempGPU, temperature));
  }
  return to_bool(kFuncName, NvmlRet::ErrorFunctionNotFound);
}

bool Nvml::get_name(NvmlDeviceHandle device, std::string* name) noexcept {
  constexpr const char* kFuncName = "get_name";
  constexpr int kNameSize = 64;
  char name_buf[kNameSize];
  void* func;
  if ((func = nvml_symtab[NvmlIndex::DeviceGetName].handle) != nullptr) {
    auto nvml_get_name = reinterpret_cast<NvmlRet (*)(NvmlDeviceHandle, char*, unsigned int)>(func);
    auto ret = nvml_get_name(device, &name_buf[0], kNameSize);
    if (ret == NvmlRet::Success) {
      name->assign(&name_buf[0]);
    }
    return to_bool(kFuncName, ret);
  }
  return to_bool(kFuncName, NvmlRet::ErrorFunctionNotFound);
}

bool Nvml::get_fan_speed(NvmlDeviceHandle device, unsigned int* speed) noexcept {
  constexpr const char* kFuncName = "get_fan_speed";
  void* func;
  if ((func = nvml_symtab[NvmlIndex::DeviceGetFanSpeed].handle) != nullptr) {
    auto nvml_get_fan = reinterpret_cast<NvmlRet (*)(NvmlDeviceHandle, unsigned int*)>(func);
    return to_bool(kFuncName, nvml_get_fan(device, speed));
  }
  return to_bool(kFuncName, NvmlRet::ErrorFunctionNotFound);
}

NvmlException::NvmlException(int error) : NvmlException(to_string(static_cast<NvmlRet>(error))) {}

}  // namespace atlasagent
