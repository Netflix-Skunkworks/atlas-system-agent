#include <gtest/gtest.h>
#include "test_registry.h"
#include "../lib/perfmetrics.h"

using atlas::meter::ManualClock;
using atlasagent::PerfMetrics;

TEST(PerfMetrics, OnlineCpus) {
  ManualClock clock;
  TestRegistry registry{&clock};
  PerfMetrics p{&registry, "./resources"};

  // 0-3,5-7,10-15,23
  std::vector<bool> expected;
  expected.resize(24);
  for (auto i = 0; i <= 3; i++) expected[i] = true;
  for (auto i = 5; i <= 7; i++) expected[i] = true;
  for (auto i = 10; i <= 15; i++) expected[i] = true;
  expected[23] = true;

  EXPECT_EQ(expected, p.get_online_cpus());
}

TEST(PerfMetrics, ParseRange) {
  auto fp = atlasagent::open_file(".", "resources/range-simple.txt");
  std::vector<bool> range;
  parse_range(fp, &range);

  // 0-63
  std::vector<bool> expected;
  expected.assign(64, true);
  EXPECT_EQ(expected, range);
}

TEST(PerfMetrics, ParseRangeCommas) {
  auto fp = atlasagent::open_file(".", "resources/range-commas.txt");
  std::vector<bool> range;
  parse_range(fp, &range);

  // 1,3,5
  std::vector<bool> expected{false, true, false, true, false, true};
  EXPECT_EQ(expected, range);
}
