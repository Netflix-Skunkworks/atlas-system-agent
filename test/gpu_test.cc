#include "../lib/logger.h"
#include "../lib/gpumetrics.h"
#include "test_registry.h"
#include "measurement_utils.h"
#include <gtest/gtest.h>

using namespace atlasagent;

using atlas::meter::Measurements;

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

  bool get_fan_speed(NvmlDeviceHandle device, unsigned int* speed) noexcept {
    return false;
  }
};

static void expect_dist_summary(const Measurements& ms, const char* name, double total, double count, double max,
                                double sq) {
  auto stat_ref = atlas::util::intern_str("statistic");
  auto num_tags_found = 0;
  for (const auto& m : ms) {
    const auto& cur_name = m.id->Name();
    if (strcmp(name, cur_name) != 0) continue;


    const auto& tags = m.id->GetTags();
    auto it = tags.at(stat_ref);
    if (it.length() == 0) {
      FAIL() << "Unable to find statistic tag for name=" << name;
    } else {
      auto stat = it.get();
      if (strcmp("totalAmount", stat) == 0) {
        EXPECT_DOUBLE_EQ(total/60.0, m.value) << "total does not match for " << name;
        num_tags_found++;
      } else if (strcmp("totalOfSquares", stat) == 0) {
        EXPECT_DOUBLE_EQ(sq/60.0, m.value) << "totalOfSquares does not match for " << name;
        num_tags_found++;
      } else if (strcmp("count", stat) == 0) {
        EXPECT_DOUBLE_EQ(count/60.0, m.value) << "count does not match for " << name;
        num_tags_found++;
      } else if (strcmp("max", stat) == 0) {
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
  TestRegistry registry;
  registry.SetWall(1000);
  TestNvml nvml;
  auto metrics = GpuMetrics<TestNvml>(&registry, &nvml);
  metrics.gpu_metrics();
  registry.SetWall(61000);
  const auto& ms = registry.AllMeasurements();
  EXPECT_EQ(13, ms.size());
  auto gpu_ref = atlas::util::intern_str("gpu");
  auto values = measurements_to_map(ms, gpu_ref);
  expect_value(values, "gpu.count", 2);
  expect_value(values, "gpu.memoryUsage|free|gpu-0", 100);
  expect_value(values, "gpu.memoryUsage|used|gpu-0", 2900);
  expect_value(values, "gpu.memoryUsage|free|gpu-1", 1000);
  expect_value(values, "gpu.memoryUsage|used|gpu-1", 2000);
  expect_value(values, "gpu.utilization|gpu-0", 0);
  expect_value(values, "gpu.utilization|gpu-1", 100);
  expect_value(values, "gpu.memoryActivity|gpu-0", 0);
  expect_value(values, "gpu.memoryActivity|gpu-1", 25);

  expect_dist_summary(ms, "gpu.temperature", 72 + 42, 2, 72, 72*72.0 + 42*42);
}