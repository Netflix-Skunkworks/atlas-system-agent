#include <gtest/gtest.h>
#include <fmt/ostream.h>

#include "../lib/cpufreq.h"
#include "../lib/logger.h"
#include "measurement_utils.h"

using atlasagent::CpuFreq;
using spectator::GetConfiguration;
using spectator::LogManager;
using spectator::Registry;

namespace {
TEST(CpuFreq, Stats) {
  auto logger = spectator::DefaultLogger();
  Registry registry(GetConfiguration(), logger);
  CpuFreq cpufreq{&registry, "./resources/cpufreq"};
  cpufreq.Stats();

  auto min_ds = registry.GetDistributionSummary("sys.minCoreFrequency");
  auto max_ds = registry.GetDistributionSummary("sys.maxCoreFrequency");
  auto cur_ds = registry.GetDistributionSummary("sys.curCoreFrequency");

  EXPECT_EQ(min_ds->Count(), 4);
  EXPECT_EQ(max_ds->Count(), 4);
  EXPECT_EQ(cur_ds->Count(), 4);

  EXPECT_EQ(min_ds->TotalAmount(), 4 * 1200000);
  EXPECT_EQ(max_ds->TotalAmount(), 4 * 3500000);
  EXPECT_EQ(cur_ds->TotalAmount(), 1200188 + 1200484 + 2620000 + 3000000);

  auto map = measurements_to_map(cur_ds->Measure(), "");
  auto it = map.find("sys.curCoreFrequency|max");
  if (it != map.end()) {
    EXPECT_EQ(it->second, 3000000);
  } else {
    FAIL() << "Unable to find max value";
  }
}

}  // namespace