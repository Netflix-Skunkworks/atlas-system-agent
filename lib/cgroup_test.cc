#include "cgroup.h"
#include "measurement_utils.h"
#include <gtest/gtest.h>

#include <utility>

namespace {

using Registry = spectator::TestRegistry;

class CGroupTest : public atlasagent::CGroup<Registry> {
 public:
  explicit CGroupTest(Registry* registry, std::string path_prefix = "/sys/fs/cgroup",
                      absl::Duration update_interval = absl::Seconds(60)) noexcept
      : CGroup(registry, std::move(path_prefix), update_interval) {}

  void do_cpu_stats(absl::Time now) { CGroup::do_cpu_stats(now); }
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

TEST(CGroup, ParseCpu) {
  Registry registry;
  CGroupTest cGroup{&registry, "testdata/resources", absl::Seconds(30)};

  auto now = absl::Now();
  cGroup.do_cpu_stats(now);
  auto initial = my_measurements(&registry);
  auto initial_map = measurements_to_map(initial, "");
  expect_value(&initial_map, "cgroup.cpu.processingCapacity|count", 10.24 * 30.0);
  expect_value(&initial_map, "cgroup.cpu.shares|gauge", 1024);
  EXPECT_TRUE(initial_map.empty());

  cGroup.set_prefix("testdata/resources2");
  cGroup.do_cpu_stats(now + absl::Seconds(5));

  const auto& ms = my_measurements(&registry);
  measurement_map map = measurements_to_map(ms, "proto");
  expect_value(&map, "cgroup.cpu.usageTime|count|system", 120);
  expect_value(&map, "cgroup.cpu.usageTime|count|user", 60);
  expect_value(&map, "cgroup.cpu.processingCapacity|count", 10.24 * 5.0);
  expect_value(&map, "cgroup.cpu.shares|gauge", 1024);
  expect_value(&map, "cgroup.cpu.processingTime|count", 30);
  expect_value(&map, "cgroup.cpu.numThrottled|count", 2);
  expect_value(&map, "cgroup.cpu.throttledTime|count", 1);
  EXPECT_TRUE(map.empty());
}

TEST(CGroup, ParseMemory) {
  Registry registry;
  CGroupTest cGroup{&registry, "testdata/resources"};

  cGroup.memory_stats();
  auto initial = my_measurements(&registry);
  EXPECT_EQ(initial.size(), 12);  // 12 gauges

  cGroup.set_prefix("testdata/resources2");
  cGroup.memory_stats();
  auto ms = my_measurements(&registry);
  auto values = measurements_to_map(ms, "");
  expect_value(&values, "cgroup.kmem.tcpUsed|gauge", 0);
  expect_value(&values, "cgroup.mem.processUsage|gauge|mapped_file", 3);
  expect_value(&values, "cgroup.mem.used|gauge", 917504);
  expect_value(&values, "cgroup.mem.pageFaults|count|minor", 1000);
  expect_value(&values, "cgroup.mem.pageFaults|count|major", 10);
  expect_value(&values, "cgroup.kmem.limit|gauge", 9223372036854771712.0);
  expect_value(&values, "cgroup.mem.processUsage|gauge|cache", 951968);
  expect_value(&values, "cgroup.kmem.used|gauge", 14528144.0);
  expect_value(&values, "cgroup.kmem.maxUsed|gauge", 14598144.0);
  expect_value(&values, "cgroup.mem.limit|gauge", 9223372036854771712.0);
  expect_value(&values, "cgroup.mem.processUsage|gauge|rss_huge", 2);
  expect_value(&values, "cgroup.mem.processUsage|gauge|rss", 1);
  expect_value(&values, "cgroup.kmem.tcpMaxUsed|gauge", 0);
  expect_value(&values, "cgroup.kmem.tcpLimit|gauge", 9223372036854771712.0);
  expect_value(&values, "cgroup.kmem.failures|count", 4);
  expect_value(&values, "cgroup.mem.failures|count", 2);
  EXPECT_TRUE(values.empty());
}
}  // namespace
