#include "../lib/cgroup.h"
#include "../lib/logger.h"
#include "measurement_utils.h"
#include <fmt/ostream.h>
#include <gtest/gtest.h>

using namespace atlasagent;
using spectator::Config;
using spectator::Registry;

// TODO: verify values
TEST(CGroup, ParseCpu) {
  Registry registry(Config{}, Logger());
  CGroup cGroup{&registry, "./resources"};

  cGroup.cpu_stats();
  auto initial = registry.Measurements();
  auto initial_map = measurements_to_map(initial, "");
  expect_value(&initial_map, "cgroup.cpu.processingCapacity", 10.24);
  expect_value(&initial_map, "cgroup.cpu.shares", 1024);
  EXPECT_TRUE(initial_map.empty());

  cGroup.set_prefix("./resources2");
  cGroup.cpu_stats();

  const auto& ms = registry.Measurements();
  measurement_map map = measurements_to_map(ms, "proto");
  expect_value(&map, "cgroup.cpu.usageTime|system", 120);
  expect_value(&map, "cgroup.cpu.usageTime|user", 60);
  expect_value(&map, "cgroup.cpu.processingCapacity", 10.24);
  expect_value(&map, "cgroup.cpu.shares", 1024);
  expect_value(&map, "cgroup.cpu.processingTime", 30);
  expect_value(&map, "cgroup.cpu.numThrottled", 2);
  expect_value(&map, "cgroup.cpu.throttledTime", 1);
  EXPECT_TRUE(map.empty());
}

TEST(CGroup, ParseMemory) {
  Registry registry(Config{}, Logger());
  CGroup cGroup{&registry, "./resources"};

  cGroup.memory_stats();
  auto initial = registry.Measurements();
  EXPECT_EQ(initial.size(), 12);  // 12 gauges

  cGroup.set_prefix("./resources2");
  cGroup.memory_stats();
  auto ms = registry.Measurements();
  auto values = measurements_to_map(ms, "");
  expect_value(&values, "cgroup.kmem.tcpUsed", 0);
  expect_value(&values, "cgroup.mem.processUsage|mapped_file", 3);
  expect_value(&values, "cgroup.mem.used", 917504);
  expect_value(&values, "cgroup.mem.pageFaults|minor", 1000);
  expect_value(&values, "cgroup.mem.pageFaults|major", 10);
  expect_value(&values, "cgroup.kmem.limit", 9223372036854771712.0);
  expect_value(&values, "cgroup.mem.processUsage|cache", 951968);
  expect_value(&values, "cgroup.kmem.used", 14528144.0);
  expect_value(&values, "cgroup.kmem.maxUsed", 14598144.0);
  expect_value(&values, "cgroup.mem.limit", 9223372036854771712.0);
  expect_value(&values, "cgroup.mem.processUsage|rss_huge", 2);
  expect_value(&values, "cgroup.mem.processUsage|rss", 1);
  expect_value(&values, "cgroup.kmem.tcpMaxUsed", 0);
  expect_value(&values, "cgroup.kmem.tcpLimit", 9223372036854771712.0);
  expect_value(&values, "cgroup.kmem.failures", 4);
  expect_value(&values, "cgroup.mem.failures", 2);
  EXPECT_TRUE(values.empty());
}
