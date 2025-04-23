#include <lib/Perfmetrics/src/perfmetrics.h>
#include <gtest/gtest.h>

namespace {
using Registry = spectator::TestRegistry;
using PerfMetrics = atlasagent::PerfMetrics<Registry>;

TEST(PerfMetrics, OnlineCpus) {
  Registry registry;
  PerfMetrics p{&registry, "testdata/resources"};

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
  auto fp = atlasagent::open_file("testdata", "resources/range-simple.txt");
  std::vector<bool> range;
  parse_range(fp, &range);

  // 0-63
  std::vector<bool> expected;
  expected.assign(64, true);
  EXPECT_EQ(expected, range);
}

TEST(PerfMetrics, ParseRangeCommas) {
  auto fp = atlasagent::open_file("testdata", "resources/range-commas.txt");
  std::vector<bool> range;
  parse_range(fp, &range);

  // 1,3,5
  std::vector<bool> expected{false, true, false, true, false, true};
  EXPECT_EQ(expected, range);
}

}  // namespace