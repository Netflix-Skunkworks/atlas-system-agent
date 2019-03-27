#include "../lib/chrony.h"
#include "../lib/logger.h"
#include "measurement_utils.h"
#include <gtest/gtest.h>

using atlasagent::Chrony;
using atlasagent::Logger;

struct TestClock : std::chrono::system_clock {
  using base_type = std::chrono::system_clock;
  static time_point now() {
    static auto fixed = base_type::now();
    return fixed;
  }
};

class CT : public Chrony<TestClock> {
 public:
  CT(spectator::Registry* registry) : Chrony{registry} {}
  void stats(const std::string& tracking, const std::vector<std::string>& sources) noexcept {
    Chrony::tracking_stats(tracking, sources);
  }

  TestClock::time_point lastSample() const { return lastSampleTime_; }
};

double get_default_sample_age(const CT& chrony) {
  auto nanos =
      std::chrono::duration_cast<std::chrono::nanoseconds>(TestClock::now() - chrony.lastSample())
          .count();
  return nanos / 1e9;
}

TEST(Chrony, Stats) {
  spectator::Registry registry{spectator::GetConfiguration(), Logger()};
  CT chrony{&registry};

  std::string tracking =
      "A9FEA97B,169.254.169.123,4,1553630752.756016394,0.000042,-0.000048721,"
      "0.000203645,-3.485,-0.022,0.079,0.000577283,0.000112625,775.8,Normal\n";

  std::vector<std::string> sources = {
      "^,*,169.254.169.123,3,8,377,74,-0.000027989,-0.000076710,0.000319246\n",
      "^,-,10.229.0.50,2,10,340,7219,0.002353442,0.001586549,0.049785987\n",
      "^,-,172.16.1.2,2,10,337,1028,0.000021583,-0.000021316,0.049278442\n"};
  chrony.stats(tracking, sources);

  auto ms = registry.Measurements();
  auto map = measurements_to_map(ms, "");
  std::unordered_map<std::string, double> expected = {{"sys.time.offset", 0.000042},
                                                      {"sys.time.lastSampleAge", 74}};
  EXPECT_EQ(map, expected);
}

TEST(Chrony, StatsEmpty) {
  spectator::Registry registry{spectator::GetConfiguration(), Logger()};
  CT chrony{&registry};
  chrony.stats("", {});

  auto ms = registry.Measurements();
  EXPECT_TRUE(ms.empty());
}

TEST(Chrony, StatsInvalid) {
  spectator::Registry registry{spectator::GetConfiguration(), Logger()};
  CT chrony{&registry};

  std::string tracking =
      "A9FEA97B,1.2.3.4,4,1.1,foo,-0.021,1,-2,-0.022,0.079,0.0005,0.0001,775.8,Normal\n";

  std::vector<std::string> sources = {
      "^,*,1.2.3.4,3,8,377,abc,-0.000027989,-0.000076710,0.000319246\n",
      "^,-,10.229.0.50,2,10,340,7219,0.002353442,0.001586549,0.049785987\n",
      "^,-,172.16.1.2,2,10,337,1028,0.000021583,-0.000021316,0.049278442\n"};
  chrony.stats(tracking, sources);

  auto ms = registry.Measurements();
  auto map = measurements_to_map(ms, "");

  std::unordered_map<std::string, double> expected = {
      {"sys.time.lastSampleAge", get_default_sample_age(chrony)}};
  EXPECT_EQ(expected, map);
}

// ensure we deal properly when the server in tracking gets lost
// (maybe a race between the commands chronyc tracking; chronyc sources)
TEST(Chrony, NoSources) {
  spectator::Registry registry{spectator::GetConfiguration(), Logger()};
  CT chrony{&registry};

  std::string tracking =
      "A9FEA97B,1.2.3.4,4,1.1,10,-0.021,1,-2,-0.022,0.079,0.0005,0.0001,775.8,Normal\n";

  std::vector<std::string> sources = {
      "^,*,1.2.3.5,3,8,377,abc,-0.000027989,-0.000076710,0.000319246\n",
      "^,-,10.229.0.50,2,10,340,7219,0.002353442,0.001586549,0.049785987\n",
      "^,-,172.16.1.2,2,10,337,1028,0.000021583,-0.000021316,0.049278442\n"};
  chrony.stats(tracking, sources);

  auto ms = registry.Measurements();
  auto map = measurements_to_map(ms, "");
  std::unordered_map<std::string, double> expected = {
      {"sys.time.offset", 10}, {"sys.time.lastSampleAge", get_default_sample_age(chrony)}};
  EXPECT_EQ(map, expected);
}
