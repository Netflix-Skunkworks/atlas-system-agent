#include <lib/collectors/cpu_freq/src/cpu_freq.h>
#include <lib/logger/src/logger.h>

#include <thirdparty/spectator-cpp/spectator/registry.h>
#include <thirdparty/spectator-cpp/libs/writer/writer_wrapper/writer_test_helper.h>

#include <gtest/gtest.h>

namespace {
using atlasagent::CpuFreq;

TEST(CpuFreq, Stats) {

  auto config = Config(WriterConfig(WriterTypes::Memory));
  auto r = Registry(config);

  CpuFreq cpuFreq{&r, "testdata/resources/cpufreq"};
  cpuFreq.Stats();

  auto memoryWriter = static_cast<MemoryWriter*>(WriterTestHelper::GetImpl());
  auto messages = memoryWriter->GetMessages();

  EXPECT_EQ(messages.size(), 12);
  EXPECT_EQ(messages.at(0), "d:sys.minCoreFrequency:1200000\n");
  EXPECT_EQ(messages.at(1), "d:sys.maxCoreFrequency:3500000\n");
  EXPECT_EQ(messages.at(2), "d:sys.curCoreFrequency:1200188\n");
  EXPECT_EQ(messages.at(3), "d:sys.minCoreFrequency:1200000\n");
  EXPECT_EQ(messages.at(4), "d:sys.maxCoreFrequency:3500000\n");
  EXPECT_EQ(messages.at(5), "d:sys.curCoreFrequency:3000000\n");
  EXPECT_EQ(messages.at(6), "d:sys.minCoreFrequency:1200000\n");
  EXPECT_EQ(messages.at(7), "d:sys.maxCoreFrequency:3500000\n");
  EXPECT_EQ(messages.at(8), "d:sys.curCoreFrequency:2620000\n");
  EXPECT_EQ(messages.at(9), "d:sys.minCoreFrequency:1200000\n");
  EXPECT_EQ(messages.at(10), "d:sys.maxCoreFrequency:3500000\n");
  EXPECT_EQ(messages.at(11), "d:sys.curCoreFrequency:1200484\n");
}

}  // namespace
