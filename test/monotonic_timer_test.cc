
#include <gtest/gtest.h>
#include "../lib/monotonic_timer.h"
#include "measurement_utils.h"

TEST(MonotonicTimer, NoActivity) {
  spectator::Registry registry{spectator::GetConfiguration(), spectator::DefaultLogger()};
  MonotonicTimer timer{&registry, registry.CreateId("test", spectator::Tags{})};

  EXPECT_EQ(registry.Measurements().size(), 0);
};

TEST(MonotonicTimer, Record) {
  spectator::Registry registry{spectator::GetConfiguration(), spectator::DefaultLogger()};
  MonotonicTimer timer{&registry, registry.CreateId("test", spectator::Tags{})};

  using millis = std::chrono::milliseconds;
  timer.update(millis{1000}, 10);
  EXPECT_EQ(registry.Measurements().size(), 0);
  timer.update(millis{2000}, 15);
  auto ms = registry.Measurements();
  auto map = measurements_to_map(ms, "");
  std::map<std::string, double> expected_map{{"test|count", 5}, {"test|totalTime", 1}};
  for (const auto& kv : expected_map) {
    EXPECT_DOUBLE_EQ(kv.second, map[kv.first]) << kv.first;
  }
}

TEST(MonotonicTimer, Overflow) {
  spectator::Registry registry{spectator::GetConfiguration(), spectator::DefaultLogger()};
  MonotonicTimer timer{&registry, registry.CreateId("test", spectator::Tags{})};

  using millis = std::chrono::milliseconds;
  timer.update(millis{2000}, 10);
  EXPECT_EQ(registry.Measurements().size(), 0);
  timer.update(millis{1000}, 15);  // overflow time
  EXPECT_EQ(registry.Measurements().size(), 0);
  timer.update(millis{3000}, 12);  // overflow count
  EXPECT_EQ(registry.Measurements().size(), 0);

  timer.update(millis{4000}, 13);  // back to normal
  EXPECT_EQ(registry.Measurements().size(), 2);
}
