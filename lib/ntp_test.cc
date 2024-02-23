#include "logger.h"
#include "measurement_utils.h"
#include "ntp.h"
#include <gtest/gtest.h>

namespace {
using atlasagent::Logger;
using atlasagent::Ntp;
using Registry = spectator::TestRegistry;

struct TestClock {
  static absl::Time now() {
    static auto fixed = absl::Now();
    return fixed;
  }
};

class NtpTest : public Ntp<Registry, TestClock> {
 public:
  explicit NtpTest(Registry* registry) : Ntp{registry} {}
  void stats(const std::string& tracking, const std::vector<std::string>& sources) noexcept {
    Ntp::chrony_stats(tracking, sources);
  }
  void ntp(int err, timex* time) { Ntp::ntp_stats(err, time); }
  [[nodiscard]] absl::Time lastSample() const { return lastSampleTime_; }
};

double get_default_sample_age(const NtpTest& ntp) {
  return absl::ToDoubleSeconds(TestClock::now() - ntp.lastSample());
}

TEST(Ntp, Stats) {
  Registry registry;
  NtpTest ntp{&registry};

  std::string tracking =
      "A9FEA97B,169.254.169.123,4,1553630752.756016394,0.000042,-0.000048721,"
      "0.000203645,-3.485,-0.022,0.079,0.000577283,0.000112625,775.8,Normal\n";

  std::vector<std::string> sources = {
      "^,*,169.254.169.123,3,8,377,74,-0.000027989,-0.000076710,0.000319246\n",
      "^,-,10.229.0.50,2,10,340,7219,0.002353442,0.001586549,0.049785987\n",
      "^,-,172.16.1.2,2,10,337,1028,0.000021583,-0.000021316,0.049278442\n"};
  ntp.stats(tracking, sources);

  auto ms = registry.Measurements();
  auto map = measurements_to_map(ms, "");
  std::unordered_map<std::string, double> expected = {{"sys.time.lastSampleAge|gauge", 74}};
  EXPECT_EQ(map, expected);
}

TEST(Ntp, StatsEmpty) {
  Registry registry;
  NtpTest ntp{&registry};
  ntp.stats("", {});

  auto ms = registry.Measurements();
  EXPECT_EQ(ms.size(), 1);  // we always report
}

TEST(Ntp, StatsInvalid) {
  Registry registry;
  NtpTest ntp{&registry};

  std::string tracking =
      "A9FEA97B,1.2.3.4,4,1.1,foo,-0.021,1,-2,-0.022,0.079,0.0005,0.0001,775.8,Normal\n";

  std::vector<std::string> sources = {
      "^,*,1.2.3.4,3,8,377,abc,-0.000027989,-0.000076710,0.000319246\n",
      "^,-,10.229.0.50,2,10,340,7219,0.002353442,0.001586549,0.049785987\n",
      "^,-,172.16.1.2,2,10,337,1028,0.000021583,-0.000021316,0.049278442\n"};
  ntp.stats(tracking, sources);

  auto ms = registry.Measurements();
  auto map = measurements_to_map(ms, "");

  std::unordered_map<std::string, double> expected = {
      {"sys.time.lastSampleAge|gauge", get_default_sample_age(ntp)}};
  EXPECT_EQ(expected, map);
}

// ensure we deal properly when the server in tracking gets lost
// (maybe a race between the commands ntpc tracking; ntpc sources)
TEST(Ntp, NoSources) {
  Registry registry;
  NtpTest ntp{&registry};

  std::string tracking =
      "A9FEA97B,1.2.3.4,4,1.1,10,-0.021,1,-2,-0.022,0.079,0.0005,0.0001,775.8,Normal\n";

  std::vector<std::string> sources = {
      "^,*,1.2.3.5,3,8,377,abc,-0.000027989,-0.000076710,0.000319246\n",
      "^,-,10.229.0.50,2,10,340,7219,0.002353442,0.001586549,0.049785987\n",
      "^,-,172.16.1.2,2,10,337,1028,0.000021583,-0.000021316,0.049278442\n"};
  ntp.stats(tracking, sources);

  auto ms = registry.Measurements();
  auto map = measurements_to_map(ms, "");
  std::unordered_map<std::string, double> expected = {
      {"sys.time.lastSampleAge|gauge", get_default_sample_age(ntp)}};
  EXPECT_EQ(map, expected);
}

TEST(Ntp, adjtime) {
  Registry registry;
  NtpTest ntp{&registry};

  struct timex t {};
  t.esterror = 100000;
  ntp.ntp(TIME_OK, &t);

  auto ms = registry.Measurements();
  auto map = measurements_to_map(ms, "");
  std::unordered_map<std::string, double> expected = {{"sys.time.unsynchronized|gauge", 0},
                                                      {"sys.time.estimatedError|gauge", 0.1}};
  EXPECT_EQ(map, expected);
}

TEST(Ntp, adjtime_err) {
  Registry registry;
  NtpTest ntp{&registry};

  struct timex t {};
  t.esterror = 200000;
  ntp.ntp(TIME_ERROR, &t);

  auto ms = registry.Measurements();
  auto map = measurements_to_map(ms, "");
  std::unordered_map<std::string, double> expected = {{"sys.time.unsynchronized|gauge", 1},
                                                      {"sys.time.estimatedError|gauge", 0.2}};
  EXPECT_EQ(map, expected);
}
}  // namespace
