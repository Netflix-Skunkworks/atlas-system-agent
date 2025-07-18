#include <lib/logger/src/logger.h>
#include <lib/collectors/nvml/src/gpumetrics.h>

#include <thirdparty/spectator-cpp/spectator/registry.h>
#include <thirdparty/spectator-cpp/libs/writer/writer_wrapper/writer_test_helper.h>
#include <gtest/gtest.h>

using atlasagent::NvmlDeviceHandle;
using atlasagent::NvmlMemory;
using atlasagent::NvmlPerfState;
using atlasagent::NvmlUtilization;

namespace {

class TestNvml {
 public:
  bool get_count(unsigned int* count) noexcept {
    *count = 2;
    return true;
  }

  bool get_by_index(unsigned int index, NvmlDeviceHandle* device) noexcept {
    *device = reinterpret_cast<NvmlDeviceHandle>(index);
    return true;
  }

  bool get_memory_info(NvmlDeviceHandle device, NvmlMemory* memory) noexcept {
    if (device) {
      memory->free = 1000;
      memory->used = 2000;
      memory->total = 3000;
    } else {
      memory->free = 100;
      memory->used = 2900;
      memory->total = 3000;
    }
    return true;
  }

  bool get_utilization_rates(NvmlDeviceHandle device, NvmlUtilization* utilization) noexcept {
    if (device) {
      utilization->memory = 25;
      utilization->gpu = 100;
    } else {
      utilization->memory = 0;
      utilization->gpu = 0;
    }
    return true;
  }

  bool get_performance_state(NvmlDeviceHandle device, NvmlPerfState* perf_state) noexcept {
    if (device) {
      *perf_state = 10;
    } else {
      *perf_state = 1;
    }
    return true;
  }

  bool get_temperature(NvmlDeviceHandle device, unsigned int* temperature) noexcept {
    if (device) {
      *temperature = 72;
    } else {
      *temperature = 42;
    }
    return true;
  }

  bool get_name(NvmlDeviceHandle device, std::string* name) noexcept {
    *name = "Tesla 1234";
    return true;
  }

  bool get_fan_speed(NvmlDeviceHandle device, unsigned int* speed) noexcept { return false; }
};

TEST(Gpu, Metrics) {

  auto config = Config(WriterConfig(WriterTypes::Memory));
  auto r = Registry(config);

  atlasagent::GpuMetrics metrics(&r, std::make_unique<TestNvml>());
  metrics.gpu_metrics();

  auto memoryWriter = static_cast<MemoryWriter*>(WriterTestHelper::GetImpl());
  auto messages = memoryWriter->GetMessages();
  EXPECT_EQ(15, messages.size());
  EXPECT_EQ(messages.at(0), "g:gpu.count:2.000000\n");
  EXPECT_EQ(messages.at(1), "g:gpu.usedMemory,gpu=0:2900.000000\n");
  EXPECT_EQ(messages.at(2), "g:gpu.freeMemory,gpu=0:100.000000\n");
  EXPECT_EQ(messages.at(3), "g:gpu.totalMemory,gpu=0:3000.000000\n");
  EXPECT_EQ(messages.at(4), "g:gpu.utilization,gpu=0:0.000000\n");
  EXPECT_EQ(messages.at(5), "g:gpu.memoryActivity,gpu=0:0.000000\n");
  EXPECT_EQ(messages.at(6), "g:gpu.perfState,gpu=0:1.000000\n");
  EXPECT_EQ(messages.at(7), "d:gpu.temperature:42.000000\n");
  EXPECT_EQ(messages.at(8), "g:gpu.usedMemory,gpu=1:2000.000000\n");
  EXPECT_EQ(messages.at(9), "g:gpu.freeMemory,gpu=1:1000.000000\n");
  EXPECT_EQ(messages.at(10), "g:gpu.totalMemory,gpu=1:3000.000000\n");
  EXPECT_EQ(messages.at(11), "g:gpu.utilization,gpu=1:100.000000\n");
  EXPECT_EQ(messages.at(12), "g:gpu.memoryActivity,gpu=1:25.000000\n");
  EXPECT_EQ(messages.at(13), "g:gpu.perfState,gpu=1:10.000000\n");
  EXPECT_EQ(messages.at(14), "d:gpu.temperature:72.000000\n");
}

}  // namespace