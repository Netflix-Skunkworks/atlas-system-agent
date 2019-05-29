#include "../lib/cgroup.h"
#include "../lib/logger.h"
#include "measurement_utils.h"
#include <fmt/ostream.h>
#include <gtest/gtest.h>

using namespace atlasagent;
using spectator::GetConfiguration;
using spectator::Registry;

// TODO: verify values
TEST(CGroup, ParseCpu) {
  Registry registry(GetConfiguration(), Logger());
  CGroup cGroup{&registry, "./resources"};

  cGroup.cpu_stats();
  auto initial = registry.Measurements();
  auto initial_map = measurements_to_map(initial, "");
  expect_value(&initial_map, "cgroup.cpu.processingCapacity|gauge", 10.24);
  expect_value(&initial_map, "cgroup.cpu.shares|gauge", 1024);
  EXPECT_TRUE(initial_map.empty());

  cGroup.set_prefix("./resources2");
  cGroup.cpu_stats();

  const auto& ms = registry.Measurements();
  measurement_map map = measurements_to_map(ms, "proto");
  expect_value(&map, "cgroup.cpu.usageTime|count|system", 120);
  expect_value(&map, "cgroup.cpu.usageTime|count|user", 60);
  expect_value(&map, "cgroup.cpu.processingCapacity|gauge", 10.24);
  expect_value(&map, "cgroup.cpu.shares|gauge", 1024);
  expect_value(&map, "cgroup.cpu.processingTime|count", 30);
  expect_value(&map, "cgroup.cpu.numThrottled|count", 2);
  expect_value(&map, "cgroup.cpu.throttledTime|count", 1);
  EXPECT_TRUE(map.empty());
}

TEST(CGroup, ParseMemory) {
  Registry registry(GetConfiguration(), Logger());
  CGroup cGroup{&registry, "./resources"};

  cGroup.memory_stats();
  auto initial = registry.Measurements();
  EXPECT_EQ(initial.size(), 12);  // 12 gauges

  cGroup.set_prefix("./resources2");
  cGroup.memory_stats();
  auto ms = registry.Measurements();
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
