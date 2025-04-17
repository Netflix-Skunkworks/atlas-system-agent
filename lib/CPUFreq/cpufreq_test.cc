#include "cpufreq.h"
#include "../Logger/logger.h"
#include <lib/MeasurementUtils/src/measurement_utils.h>


#include <gtest/gtest.h>

namespace {
using atlasagent::CpuFreq;
using Registry = spectator::TestRegistry;

TEST(CpuFreq, Stats) {
  Registry registry;
  CpuFreq<Registry> cpufreq{&registry, "testdata/resources/cpufreq"};
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

  std::vector<spectator::Measurement> measures;
  cur_ds->Measure(&measures);
  auto map = measurements_to_map(measures, "");
  auto it = map.find("sys.curCoreFrequency|max");
  if (it != map.end()) {
    EXPECT_EQ(it->second, 3000000);
  } else {
    FAIL() << "Unable to find max value";
  }
}

}  // namespace
