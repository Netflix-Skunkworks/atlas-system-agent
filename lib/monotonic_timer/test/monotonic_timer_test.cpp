#include <lib/monotonic_timer/src/monotonic_timer.h>
#include <gtest/gtest.h>
#include <thirdparty/spectator-cpp/spectator/registry.h>
#include <thirdparty/spectator-cpp/libs/writer/writer_wrapper/writer_test_helper.h>
namespace
{

using MonotonicTimer = atlasagent::MonotonicTimer;

TEST(MonotonicTimer, Record)
{
    auto config = Config(WriterConfig(WriterTypes::Memory));
    auto r = Registry(config);
    MonotonicTimer timer{&r, MeterId("test")};
    timer.update(absl::Milliseconds(1000), 10);

    auto memoryWriter = static_cast<MemoryWriter*>(WriterTestHelper::GetImpl());
    auto messages = memoryWriter->GetMessages();
    EXPECT_EQ(messages.size(), 0);

    timer.update(absl::Milliseconds(2000), 15);
    messages = memoryWriter->GetMessages();

    EXPECT_EQ(messages.size(), 2);
    EXPECT_EQ(messages.at(0), "c:test,statistic=totalTime:1.000000\n");
    EXPECT_EQ(messages.at(1), "c:test,statistic=count:5.000000\n");
}

TEST(MonotonicTimer, Overflow)
{
    auto config = Config(WriterConfig(WriterTypes::Memory));
    auto r = Registry(config);
    MonotonicTimer timer{&r, MeterId("test")};
    auto memoryWriter = static_cast<MemoryWriter*>(WriterTestHelper::GetImpl());

    timer.update(absl::Milliseconds(2000), 10);
    auto messages = memoryWriter->GetMessages();
    EXPECT_EQ(messages.size(), 0);

    timer.update(absl::Milliseconds(1000), 15);  // overflow time
    messages = memoryWriter->GetMessages();
    EXPECT_EQ(messages.size(), 0);

    timer.update(absl::Milliseconds(3000), 12);  // overflow count
    messages = memoryWriter->GetMessages();
    EXPECT_EQ(messages.size(), 0);

    timer.update(absl::Milliseconds(4000), 13);  // back to normal
    messages = memoryWriter->GetMessages();
    EXPECT_EQ(messages.size(), 2);
}
}  // namespace
