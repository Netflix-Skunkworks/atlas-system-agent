#include "../lib/cgroup.h"
#include "../lib/logger.h"
#include "measurement_utils.h"
#include "test_registry.h"
#include <gtest/gtest.h>

using namespace atlasagent;

// TODO: verify values
TEST(CGroup, ParseCpu) {
  TestRegistry registry;
  registry.SetWall(1000);
  CGroup cGroup{&registry, "./resources"};

  cGroup.cpu_stats();

  registry.SetWall(61000);
  cGroup.set_prefix("./resources2");
  cGroup.cpu_stats();

  registry.SetWall(121000);
  const auto& ms = registry.my_measurements();
  measurement_map map = measurements_to_map(ms, atlas::util::intern_str("proto"));
  EXPECT_EQ(5, map.size()) << "5 cpu metrics generated";
  EXPECT_DOUBLE_EQ(2, map["cgroup.cpu.usageTime|system"]);
  EXPECT_DOUBLE_EQ(1, map["cgroup.cpu.usageTime|user"]);
  EXPECT_DOUBLE_EQ(10.24, map["cgroup.cpu.processingCapacity"]);
  EXPECT_DOUBLE_EQ(1024, map["cgroup.cpu.shares"]);
  EXPECT_DOUBLE_EQ(0.5, map["cgroup.cpu.processingTime"]);
}

TEST(CGroup, ParseMemory) {
  TestRegistry registry;
  registry.SetWall(1000);
  CGroup cGroup{&registry, "./resources"};

  cGroup.memory_stats();

  registry.SetWall(61000);
  cGroup.memory_stats();

  const auto& ms = registry.my_measurements();
  for (const auto& m : ms) {
    Logger()->info("Got {}", m);
  }
  EXPECT_EQ(9 + 8, ms.size()) << "17 mem metrics generated";
}
