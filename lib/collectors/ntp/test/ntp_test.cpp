#include <lib/logger/src/logger.h>

#include <lib/collectors/ntp/src/ntp.h>
#include <gtest/gtest.h>

#include <thirdparty/spectator-cpp/spectator/registry.h>
#include <thirdparty/spectator-cpp/libs/writer/writer_wrapper/writer_test_helper.h>

namespace {
using atlasagent::Logger;
using atlasagent::Ntp;

class NtpTest : public Ntp<TestClock> {
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
  auto config = Config(WriterConfig(WriterTypes::Memory));
  auto r = Registry(config);
  NtpTest ntp{&r};

  std::string tracking =
      "A9FEA97B,169.254.169.123,4,1553630752.756016394,0.000042,-0.000048721,"
      "0.000203645,-3.485,-0.022,0.079,0.000577283,0.000112625,775.8,Normal\n";

  std::vector<std::string> sources = {
      "^,*,169.254.169.123,3,8,377,74,-0.000027989,-0.000076710,0.000319246\n",
      "^,-,10.229.0.50,2,10,340,7219,0.002353442,0.001586549,0.049785987\n",
      "^,-,172.16.1.2,2,10,337,1028,0.000021583,-0.000021316,0.049278442\n"};
  ntp.stats(tracking, sources);

  auto memoryWriter = static_cast<MemoryWriter*>(WriterTestHelper::GetImpl());
  auto messages = memoryWriter->GetMessages();

  EXPECT_EQ(messages.size(), 1);
  EXPECT_EQ(messages.at(0), "g:sys.time.lastSampleAge:74.000000\n");
}

TEST(Ntp, StatsEmpty) {
  auto config = Config(WriterConfig(WriterTypes::Memory));
  auto r = Registry(config);
  NtpTest ntp{&r};

  ntp.stats("", {});

  auto memoryWriter = static_cast<MemoryWriter*>(WriterTestHelper::GetImpl());
  auto messages = memoryWriter->GetMessages();

  // We always report
  EXPECT_EQ(messages.size(), 1);
  EXPECT_EQ(messages.at(0), "g:sys.time.lastSampleAge:0.000000\n");
}

TEST(Ntp, StatsInvalid) {
  auto config = Config(WriterConfig(WriterTypes::Memory));
  auto r = Registry(config);
  NtpTest ntp{&r};

  std::string tracking =
      "A9FEA97B,1.2.3.4,4,1.1,foo,-0.021,1,-2,-0.022,0.079,0.0005,0.0001,775.8,Normal\n";

  std::vector<std::string> sources = {
      "^,*,1.2.3.4,3,8,377,abc,-0.000027989,-0.000076710,0.000319246\n",
      "^,-,10.229.0.50,2,10,340,7219,0.002353442,0.001586549,0.049785987\n",
      "^,-,172.16.1.2,2,10,337,1028,0.000021583,-0.000021316,0.049278442\n"};

  ntp.stats(tracking, sources);

  auto memoryWriter = static_cast<MemoryWriter*>(WriterTestHelper::GetImpl());
  auto messages = memoryWriter->GetMessages();

  auto expected_message = "g:sys.time.lastSampleAge:" + std::to_string(get_default_sample_age(ntp)) + "\n"; 
  
  EXPECT_EQ(messages.size(), 1);
  EXPECT_EQ(messages.at(0), expected_message);
}

// ensure we deal properly when the server in tracking gets lost
// (maybe a race between the commands ntpc tracking; ntpc sources)
TEST(Ntp, NoSources) {
  auto config = Config(WriterConfig(WriterTypes::Memory));
  auto r = Registry(config);
  NtpTest ntp{&r};

  std::string tracking =
      "A9FEA97B,1.2.3.4,4,1.1,10,-0.021,1,-2,-0.022,0.079,0.0005,0.0001,775.8,Normal\n";

  std::vector<std::string> sources = {
      "^,*,1.2.3.5,3,8,377,abc,-0.000027989,-0.000076710,0.000319246\n",
      "^,-,10.229.0.50,2,10,340,7219,0.002353442,0.001586549,0.049785987\n",
      "^,-,172.16.1.2,2,10,337,1028,0.000021583,-0.000021316,0.049278442\n"};
  ntp.stats(tracking, sources);
  auto memoryWriter = static_cast<MemoryWriter*>(WriterTestHelper::GetImpl());
  auto messages = memoryWriter->GetMessages();
  
  auto expected_message = "g:sys.time.lastSampleAge:" + std::to_string(get_default_sample_age(ntp)) + "\n";

  EXPECT_EQ(messages.size(), 1);
  EXPECT_EQ(messages.at(0), expected_message);
}

TEST(Ntp, adjtime) {
  auto config = Config(WriterConfig(WriterTypes::Memory));
  auto r = Registry(config);
  NtpTest ntp{&r};

  struct timex t{};
  t.esterror = 100000;
  ntp.ntp(TIME_OK, &t);
  auto memoryWriter = static_cast<MemoryWriter*>(WriterTestHelper::GetImpl());
  auto messages = memoryWriter->GetMessages();
  EXPECT_EQ(messages.size(), 2);
  EXPECT_EQ(messages.at(0), "g:sys.time.unsynchronized:0.000000\n");
  EXPECT_EQ(messages.at(1), "g:sys.time.estimatedError:0.100000\n");
}

TEST(Ntp, adjtime_err) {
  auto config = Config(WriterConfig(WriterTypes::Memory));
  auto r = Registry(config);
  NtpTest ntp{&r};

  struct timex t{};
  t.esterror = 200000;
  ntp.ntp(TIME_ERROR, &t);
  auto memoryWriter = static_cast<MemoryWriter*>(WriterTestHelper::GetImpl());
  auto messages = memoryWriter->GetMessages();

  EXPECT_EQ(messages.size(), 1);
  EXPECT_EQ(messages.at(0), "g:sys.time.unsynchronized:1.000000\n");
}
}  // namespace
