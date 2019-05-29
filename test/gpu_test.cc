#include "../lib/logger.h"
#include "../lib/gpumetrics.h"
#include "measurement_utils.h"
#include <gtest/gtest.h>
#include <spectator/memory.h>

using namespace atlasagent;

using spectator::GetConfiguration;
using spectator::Registry;
using Measurements = std::vector<spectator::Measurement>;

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

static void expect_dist_summary(const Measurements& ms, const char* name, double total,
                                double count, double max, double sq) {
  auto num_tags_found = 0;
  for (const auto& m : ms) {
    const auto& cur_name = m.id->Name();
    if (name != cur_name) continue;

    const auto& tags = m.id->GetTags();
    auto stat = tags.at("statistic");
    if (stat.empty()) {
      FAIL() << "Unable to find statistic tag for name=" << name;
    } else {
      if (stat == "totalAmount") {
        EXPECT_DOUBLE_EQ(total, m.value) << "total does not match for " << name;
        num_tags_found++;
      } else if (stat == "totalOfSquares") {
        EXPECT_DOUBLE_EQ(sq, m.value) << "totalOfSquares does not match for " << name;
        num_tags_found++;
      } else if (stat == "count") {
        EXPECT_DOUBLE_EQ(count, m.value) << "count does not match for " << name;
        num_tags_found++;
      } else if (stat == "max") {
        EXPECT_DOUBLE_EQ(max, m.value) << "max does not match for " << name;
        num_tags_found++;
      } else {
        FAIL() << "Unexpected value for statistic tag for name=" << name << "stat=" << stat;
      }
    }
  }
  EXPECT_EQ(4, num_tags_found) << "Expected 4 statistic tags for " << name;
}

TEST(Gpu, Metrics) {
  Registry registry(GetConfiguration(), Logger());
  auto metrics = GpuMetrics<TestNvml>(&registry, std::make_unique<TestNvml>());
  metrics.gpu_metrics();
  const auto& ms = registry.Measurements();
  EXPECT_EQ(17, ms.size());
  auto values = measurements_to_map(ms, "gpu");
  expect_value(&values, "gpu.count|gauge", 2);
  expect_value(&values, "gpu.usedMemory|gauge|gpu-0", 2900);
  expect_value(&values, "gpu.freeMemory|gauge|gpu-0", 100);
  expect_value(&values, "gpu.totalMemory|gauge|gpu-0", 3000);
  expect_value(&values, "gpu.usedMemory|gauge|gpu-1", 2000);
  expect_value(&values, "gpu.freeMemory|gauge|gpu-1", 1000);
  expect_value(&values, "gpu.totalMemory|gauge|gpu-1", 3000);
  expect_value(&values, "gpu.utilization|gauge|gpu-0", 0);
  expect_value(&values, "gpu.utilization|gauge|gpu-1", 100);
  expect_value(&values, "gpu.memoryActivity|gauge|gpu-0", 0);
  expect_value(&values, "gpu.memoryActivity|gauge|gpu-1", 25);

  expect_dist_summary(ms, "gpu.temperature", 72 + 42, 2, 72, 72 * 72.0 + 42 * 42);
}