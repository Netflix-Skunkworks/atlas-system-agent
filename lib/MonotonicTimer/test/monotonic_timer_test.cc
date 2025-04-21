#include <lib/MeasurementUtils/src/measurement_utils.h>
#include "../src/monotonic_timer.h"
#include <gtest/gtest.h>

namespace {
using Registry = spectator::TestRegistry;
using MonotonicTimer = atlasagent::MonotonicTimer<Registry>;

TEST(MonotonicTimer, NoActivity) {
  Registry registry;
  MonotonicTimer timer{&registry, *spectator::Id::of("test")};

  EXPECT_EQ(registry.Measurements().size(), 0);
}

TEST(MonotonicTimer, Record) {
  Registry registry;
  MonotonicTimer timer{&registry, *spectator::Id::of("test")};

  timer.update(absl::Milliseconds(1000), 10);
  EXPECT_EQ(registry.Measurements().size(), 0);
  timer.update(absl::Milliseconds(2000), 15);
  auto ms = registry.Measurements();
  auto map = measurements_to_map(ms, "");
  std::map<std::string, double> expected_map{{"test|count", 5}, {"test|totalTime", 1}};
  for (const auto& kv : expected_map) {
    EXPECT_DOUBLE_EQ(kv.second, map[kv.first]) << kv.first;
  }
}

TEST(MonotonicTimer, Overflow) {
  Registry registry;
  MonotonicTimer timer{&registry, *spectator::Id::of("test")};

  timer.update(absl::Milliseconds(2000), 10);
  EXPECT_EQ(my_measurements(&registry).size(), 0);
  timer.update(absl::Milliseconds(1000), 15);  // overflow time
  EXPECT_EQ(my_measurements(&registry).size(), 0);
  timer.update(absl::Milliseconds(3000), 12);  // overflow count
  EXPECT_EQ(my_measurements(&registry).size(), 0);
  timer.update(absl::Milliseconds(4000), 13);  // back to normal
  EXPECT_EQ(my_measurements(&registry).size(), 2);
}
}  // namespace
