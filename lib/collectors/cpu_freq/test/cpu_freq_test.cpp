#include <lib/collectors/cpu_freq/src/cpu_freq.h>
#include <lib/logger/src/logger.h>

#include <thirdparty/spectator-cpp/spectator/registry.h>
#include <thirdparty/spectator-cpp/libs/writer/writer_wrapper/writer_test_helper.h>

#include <gtest/gtest.h>
#include <set>

namespace {
using atlasagent::CpuFreq;

TEST(CpuFreq, Stats) {

  auto config = Config(WriterConfig(WriterTypes::Memory));
  auto r = Registry(config);

  CpuFreq cpuFreq{&r, "lib/collectors/cpu_freq/test/resources"};
  cpuFreq.Stats();

  auto memoryWriter = static_cast<MemoryWriter*>(WriterTestHelper::GetImpl());
  auto messages = memoryWriter->GetMessages();

  EXPECT_EQ(messages.size(), 12);

  std::multiset<std::string> expected = {
    "d:sys.minCoreFrequency:1200000.000000\n",
    "d:sys.maxCoreFrequency:3500000.000000\n",
    "d:sys.curCoreFrequency:1200188.000000\n",
    "d:sys.minCoreFrequency:1200000.000000\n",
    "d:sys.maxCoreFrequency:3500000.000000\n",
    "d:sys.curCoreFrequency:1200484.000000\n",
    "d:sys.minCoreFrequency:1200000.000000\n",
    "d:sys.maxCoreFrequency:3500000.000000\n",
    "d:sys.curCoreFrequency:2620000.000000\n",
    "d:sys.minCoreFrequency:1200000.000000\n",
    "d:sys.maxCoreFrequency:3500000.000000\n",
    "d:sys.curCoreFrequency:3000000.000000\n"
  };

  std::multiset<std::string> actual(messages.begin(), messages.end());

  EXPECT_EQ(actual, expected);
}

}  // namespace
