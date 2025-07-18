#include <lib/collectors/pressure_stall/src/pressure_stall.h>

#include <thirdparty/spectator-cpp/spectator/registry.h>
#include <thirdparty/spectator-cpp/libs/writer/writer_wrapper/writer_test_helper.h>

#include <gtest/gtest.h>



//TODD: Move this file to "testdata/resources/proc/pressure"
class PressureStallTest : public atlasagent::PressureStall {
 public:
  explicit PressureStallTest(Registry* registry, std::string path_prefix = "lib/collectors/pressure_stall/test/resources")
      : PressureStall{registry, std::move(path_prefix)} {}

  void stats() { PressureStall::update_stats(); }
};

TEST(PressureStallTest, UpdateStats) {
  auto config = Config(WriterConfig(WriterTypes::Memory));
  auto r = Registry(config);
  PressureStallTest pressure{&r};

  auto memoryWriter = static_cast<MemoryWriter*>(WriterTestHelper::GetImpl());
  pressure.stats();
  auto messages = memoryWriter->GetMessages();
  EXPECT_EQ(messages.size(), 5);

  EXPECT_EQ(messages.at(0), "C:sys.pressure.some,id=cpu:2.000000\n");
  EXPECT_EQ(messages.at(1), "C:sys.pressure.some,id=io:2.000000\n");
  EXPECT_EQ(messages.at(2), "C:sys.pressure.full,id=io:1.500000\n");
  EXPECT_EQ(messages.at(3), "C:sys.pressure.some,id=memory:2.000000\n");
  EXPECT_EQ(messages.at(4), "C:sys.pressure.full,id=memory:1.500000\n");
}
