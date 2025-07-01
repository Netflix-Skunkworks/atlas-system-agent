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
    return 8;
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

struct GpuTestingConstants {
  static constexpr auto expectedMessage1 = "g:gpu.count:2.000000\n";
  static constexpr auto expectedMessage2 = "g:gpu.usedMemory,gpu=0:2900.000000\n";
  static constexpr auto expectedMessage3 = "g:gpu.freeMemory,gpu=0:100.000000\n";
  static constexpr auto expectedMessage4 = "g:gpu.totalMemory,gpu=0:3000.000000\n";
  static constexpr auto expectedMessage5 = "g:gpu.utilization,gpu=0:0.000000\n";
  static constexpr auto expectedMessage6 = "g:gpu.memoryActivity,gpu=0:0.000000\n";
  static constexpr auto expectedMessage7 = "g:gpu.perfState,gpu=0:-1.000000\n";
  static constexpr auto expectedMessage8 = "d:gpu.temperature:42\n";
  static constexpr auto expectedMessage9 = "g:gpu.usedMemory,gpu=1:2000.000000\n";
  static constexpr auto expectedMessage10 = "g:gpu.freeMemory,gpu=1:1000.000000\n";
  static constexpr auto expectedMessage11= "g:gpu.totalMemory,gpu=1:3000.000000\n";
  static constexpr auto expectedMessage12 = "g:gpu.utilization,gpu=1:100.000000\n";
  static constexpr auto expectedMessage13 = "g:gpu.memoryActivity,gpu=1:25.000000\n";
  static constexpr auto expectedMessage14 = "g:gpu.perfState,gpu=1:-1.000000\n";
  static constexpr auto expectedMessage15 = "d:gpu.temperature:72\n";
};

TEST(Gpu, Metrics) {

  auto config = Config(WriterConfig(WriterTypes::Memory));
  auto r = Registry(config);

  atlasagent::GpuMetrics metrics(&r, std::make_unique<TestNvml>());
  metrics.gpu_metrics();

  auto memoryWriter = static_cast<MemoryWriter*>(WriterTestHelper::GetImpl());
  auto messages = memoryWriter->GetMessages();
  EXPECT_EQ(15, messages.size());
  for (const auto& msg : messages) {
    std::cout << msg << std::endl;
  }
  EXPECT_EQ(messages.at(0), GpuTestingConstants::expectedMessage1);
  EXPECT_EQ(messages.at(1), GpuTestingConstants::expectedMessage2);
  EXPECT_EQ(messages.at(2), GpuTestingConstants::expectedMessage3);
  EXPECT_EQ(messages.at(3), GpuTestingConstants::expectedMessage4);
  EXPECT_EQ(messages.at(4), GpuTestingConstants::expectedMessage5);
  EXPECT_EQ(messages.at(5), GpuTestingConstants::expectedMessage6);
  EXPECT_EQ(messages.at(6), GpuTestingConstants::expectedMessage7);
  EXPECT_EQ(messages.at(7), GpuTestingConstants::expectedMessage8);
  EXPECT_EQ(messages.at(8), GpuTestingConstants::expectedMessage9);
  EXPECT_EQ(messages.at(9), GpuTestingConstants::expectedMessage10);
  EXPECT_EQ(messages.at(10), GpuTestingConstants::expectedMessage11);
  EXPECT_EQ(messages.at(11), GpuTestingConstants::expectedMessage12);
  EXPECT_EQ(messages.at(12), GpuTestingConstants::expectedMessage13);
  EXPECT_EQ(messages.at(13), GpuTestingConstants::expectedMessage14);
  EXPECT_EQ(messages.at(14), GpuTestingConstants::expectedMessage15);

}

}  // namespace