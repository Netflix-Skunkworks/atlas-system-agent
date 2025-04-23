#include <lib/CGroup/src/cgroup.h>
#include <lib/MeasurementUtils/src/measurement_utils.h>
#include <gtest/gtest.h>

#include <utility>

namespace {
using Registry = spectator::TestRegistry;
using atlasagent::Logger;

class CGroupTest : public atlasagent::CGroup<Registry> {
 public:
  explicit CGroupTest(Registry* registry, std::string path_prefix = "/sys/fs/cgroup",
                      absl::Duration update_interval = absl::Seconds(60)) noexcept
      : CGroup(registry, std::move(path_prefix), update_interval) {}

  void cpu_stats(absl::Time now) { CGroup::do_cpu_stats(now); }
  void cpu_peak_stats(absl::Time now) { CGroup::do_cpu_peak_stats(now); }
};

inline long megabits2bytes(int mbits) { return mbits * 125000; }

TEST(CGroup, Net) {
  Registry registry;
  CGroupTest cGroup{&registry};

  unsetenv("TITUS_NUM_NETWORK_BANDWIDTH");
  cGroup.network_stats();
  auto ms = my_measurements(&registry);
  EXPECT_EQ(ms.size(), 0);

  setenv("TITUS_NUM_NETWORK_BANDWIDTH", "abc", 1);
  cGroup.network_stats();
  ms = my_measurements(&registry);
  EXPECT_EQ(ms.size(), 0);

  setenv("TITUS_NUM_NETWORK_BANDWIDTH", "128", 1);
  cGroup.network_stats();
  ms = my_measurements(&registry);
  auto map = measurements_to_map(ms, "");
  EXPECT_EQ(map["cgroup.net.bandwidthBytes|gauge"], megabits2bytes(128));
}

TEST(CGroup, PressureStall) {
  Registry registry;
  CGroupTest cGroup{&registry, "testdata/resources", absl::Seconds(30)};

  cGroup.pressure_stall();
  auto initial = my_measurements(&registry);
  auto initial_map = measurements_to_map(initial, "");
  EXPECT_EQ(initial_map.size(), 0);

  cGroup.set_prefix("testdata/resources2");
  cGroup.pressure_stall();
  const auto& ms = my_measurements(&registry);
  measurement_map map = measurements_to_map(ms, "");
  expect_value(&map, "sys.pressure.some|count|cpu", 1);
  expect_value(&map, "sys.pressure.some|count|io", 1);
  expect_value(&map, "sys.pressure.some|count|memory", 1);
  expect_value(&map, "sys.pressure.full|count|cpu", 0.5);
  expect_value(&map, "sys.pressure.full|count|io", 0.5);
  expect_value(&map, "sys.pressure.full|count|memory", 0.5);
  EXPECT_TRUE(map.empty());
}

TEST(CGroup, ParseCpuV2) {
  Registry registry;
  CGroupTest cGroup{&registry, "testdata/resources", absl::Seconds(30)};

  auto now = absl::Now();
  // pick 1 here, so as not to disturb the existing cpu metrics
  setenv("TITUS_NUM_CPU", "1", 1);
  cGroup.cpu_stats(now);
  cGroup.cpu_peak_stats(now);
  auto initial = my_measurements(&registry);
  auto initial_map = measurements_to_map(initial, "");
  expect_value(&initial_map, "cgroup.cpu.processingCapacity|count", 30);
  expect_value(&initial_map, "cgroup.cpu.weight|gauge", 100);
  expect_value(&initial_map, "sys.cpu.numProcessors|gauge", 1);
  EXPECT_EQ(initial_map.size(), 1);

  cGroup.set_prefix("testdata/resources2");
  cGroup.cpu_stats(now + absl::Seconds(5));
  cGroup.cpu_peak_stats(now + absl::Seconds(5));

  const auto& ms = my_measurements(&registry);
  measurement_map map = measurements_to_map(ms, "proto");
  expect_value(&map, "cgroup.cpu.numThrottled|count", 2);
  expect_value(&map, "cgroup.cpu.processingCapacity|count", 5.0);
  expect_value(&map, "cgroup.cpu.processingTime|count", 30);
  expect_value(&map, "cgroup.cpu.weight|gauge", 100);
  expect_value(&map, "cgroup.cpu.throttledTime|count", 1);
  expect_value(&map, "cgroup.cpu.usageTime|count|system", 120);
  expect_value(&map, "cgroup.cpu.usageTime|count|user", 60);
  expect_value(&map, "sys.cpu.numProcessors|gauge", 1);
  expect_value(&map, "sys.cpu.peakUtilization|max|system", 2400);
  expect_value(&map, "sys.cpu.peakUtilization|max|user", 1200);
  expect_value(&map, "sys.cpu.utilization|gauge|system", 2400);
  expect_value(&map, "sys.cpu.utilization|gauge|user", 1200);
  expect_value(&map, "titus.cpu.requested|gauge", 1);
  EXPECT_TRUE(map.empty());
}

TEST(CGroup, ParseMemoryV2) {
  Registry registry;
  CGroupTest cGroup{&registry, "testdata/resources"};

  cGroup.memory_stats_v2();
  cGroup.memory_stats_std_v2();
  auto initial = my_measurements(&registry);
  EXPECT_EQ(initial.size(), 14);

  cGroup.set_prefix("testdata/resources2");
  cGroup.memory_stats_v2();
  cGroup.memory_stats_std_v2();
  auto ms = my_measurements(&registry);
  auto values = measurements_to_map(ms, "");
  expect_value(&values, "cgroup.mem.failures|count", 2);
  expect_value(&values, "cgroup.mem.limit|gauge", 8589934592);
  expect_value(&values, "cgroup.mem.pageFaults|count|major", 10);
  expect_value(&values, "cgroup.mem.pageFaults|count|minor", 1000);
  expect_value(&values, "cgroup.mem.processUsage|gauge|cache", 11218944);
  expect_value(&values, "cgroup.mem.processUsage|gauge|mapped_file", 3);
  expect_value(&values, "cgroup.mem.processUsage|gauge|rss", 1);
  expect_value(&values, "cgroup.mem.processUsage|gauge|rss_huge", 2);
  expect_value(&values, "cgroup.mem.used|gauge", 7841374208);
  expect_value(&values, "mem.availReal|gauge", 759779328);
  expect_value(&values, "mem.availSwap|gauge", 536870912);
  expect_value(&values, "mem.cached|gauge", 11218944);
  expect_value(&values, "mem.freeReal|gauge", 748560384);
  expect_value(&values, "mem.shared|gauge", 135168);
  expect_value(&values, "mem.totalFree|gauge", 1296650240);
  expect_value(&values, "mem.totalReal|gauge", 8589934592);
  expect_value(&values, "mem.totalSwap|gauge", 536870912);
  EXPECT_TRUE(values.empty());
}
}  // namespace
