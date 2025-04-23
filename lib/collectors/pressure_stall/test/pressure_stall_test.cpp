#include <lib/measurement_utils/src/measurement_utils.h>
#include <lib/collectors/pressure_stall/src/pressure_stall.h>
#include <gtest/gtest.h>

namespace {

using atlasagent::Logger;
using atlasagent::PressureStall;
using Registry = spectator::TestRegistry;
using spectator::Tags;

class PressureStallTest : public PressureStall<Registry> {
 public:
  explicit PressureStallTest(Registry* registry, std::string path_prefix = "/proc/pressure")
      : PressureStall{registry, std::move(path_prefix)} {}

  void stats() {
    PressureStall::update_stats();
  }
};

TEST(PressureStall, UpdateStats) {
  Registry registry;
  PressureStallTest pressure{&registry, "testdata/resources/proc/pressure"};

  pressure.stats();
  auto ms = registry.Measurements();
  EXPECT_EQ(ms.size(), 0);

  // we need two samples, because these are all monotonic counters
  pressure.set_prefix("testdata/resources/proc2/pressure");
  pressure.stats();
  ms = registry.Measurements();
  EXPECT_EQ(ms.size(), 5);

  auto map = measurements_to_map(ms, "");
  std::unordered_map<std::string, double> expected = {
      {"sys.pressure.some|count|cpu", 1},
      {"sys.pressure.some|count|io", 1},
      {"sys.pressure.some|count|memory", 1},
      {"sys.pressure.full|count|io", 0.5},
      {"sys.pressure.full|count|memory", 0.5}};
  EXPECT_EQ(map, expected);
}
}  // namespace
