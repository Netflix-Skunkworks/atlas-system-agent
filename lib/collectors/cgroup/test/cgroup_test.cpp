#include <lib/collectors/cgroup/src/cgroup.h>
#include <gtest/gtest.h>
#include <thirdparty/spectator-cpp/spectator/registry.h>
#include <thirdparty/spectator-cpp/libs/writer/writer_wrapper/writer_test_helper.h>

#include <utility>

class CGroupTest : public atlasagent::CGroup
{
   public:
    explicit CGroupTest(Registry* registry, std::string path_prefix = "/sys/fs/cgroup") noexcept
        : CGroup(registry, std::move(path_prefix))
    {
    }

    // void cpu_stats(absl::Time now) { CGroup::do_cpu_stats(now); }
    // void cpu_peak_stats(absl::Time now) { CGroup::do_cpu_peak_stats(now); }
};

inline double megabits2bytes(int mbits) { return mbits * 125000; }

TEST(CGroup, Net)
{
    auto config = Config(WriterConfig(WriterTypes::Memory));
    auto r = Registry(config);
    CGroupTest cGroup{&r};

    unsetenv("TITUS_NUM_NETWORK_BANDWIDTH");
    cGroup.NetworkStats();

    auto memoryWriter = static_cast<MemoryWriter*>(WriterTestHelper::GetImpl());
    auto messages = memoryWriter->GetMessages();
    EXPECT_EQ(messages.size(), 0);

    setenv("TITUS_NUM_NETWORK_BANDWIDTH", "abc", 1);
    cGroup.NetworkStats();
    messages = memoryWriter->GetMessages();
    EXPECT_EQ(messages.size(), 0);

    setenv("TITUS_NUM_NETWORK_BANDWIDTH", "128", 1);
    cGroup.NetworkStats();
    messages = memoryWriter->GetMessages();
    EXPECT_EQ(messages.size(), 1);

    EXPECT_EQ(messages.at(0), "g:cgroup.net.bandwidthBytes:" + std::to_string(megabits2bytes(128)) + "\n");
}

TEST(CGroup, PressureStall)
{
    auto config = Config(WriterConfig(WriterTypes::Memory));
    Registry registry(config);

    CGroupTest cGroup{&registry, "lib/collectors/cgroup/test/resources/sample2"};

    cGroup.PressureStall();
    auto memoryWriter = static_cast<MemoryWriter*>(WriterTestHelper::GetImpl());
    auto messages = memoryWriter->GetMessages();

    EXPECT_EQ(messages.size(), 6);
    EXPECT_EQ(messages.at(0), "C:sys.pressure.some,id=cpu:2.000000\n");
    EXPECT_EQ(messages.at(1), "C:sys.pressure.full,id=cpu:1.500000\n");
    EXPECT_EQ(messages.at(2), "C:sys.pressure.some,id=io:2.000000\n");
    EXPECT_EQ(messages.at(3), "C:sys.pressure.full,id=io:1.500000\n");
    EXPECT_EQ(messages.at(4), "C:sys.pressure.some,id=memory:2.000000\n");
    EXPECT_EQ(messages.at(5), "C:sys.pressure.full,id=memory:1.500000\n");
}

// TEST(CGroup, ParseCpuV2)
// {
//     auto config = Config(WriterConfig(WriterTypes::Memory));
//     Registry registry(config);
//     CGroupTest cGroup{&registry, "lib/collectors/cgroup/test/resources/sample1", absl::Seconds(30)};
//     setenv("TITUS_NUM_CPU", "1", 1);
//     auto now = absl::Now();
//     cGroup.cpu_stats(now);
//     cGroup.cpu_peak_stats(now);

//     auto memoryWriter = static_cast<MemoryWriter*>(WriterTestHelper::GetImpl());
//     auto messages = memoryWriter->GetMessages();

//     EXPECT_EQ(messages.size(), 5);
//     // cpu_throttle_v2
//     EXPECT_EQ(messages.at(0), "C:cgroup.cpu.numThrottled:2.000000\n");

//     // cpu_utilization_v2
//     EXPECT_EQ(messages.at(1), "g:cgroup.cpu.weight:100.000000\n");
//     EXPECT_EQ(messages.at(2), "c:cgroup.cpu.processingCapacity:30.000000\n");
//     EXPECT_EQ(messages.at(3), "g:sys.cpu.numProcessors:1.000000\n");
//     EXPECT_EQ(messages.at(4), "g:titus.cpu.requested:1.000000\n");

//     // This test requires two iterations
//     memoryWriter->Clear();
//     cGroup.set_prefix("lib/collectors/cgroup/test/resources/sample2");
//     cGroup.cpu_stats(now + absl::Seconds(5));
//     cGroup.cpu_peak_stats(now + absl::Seconds(5));

//     messages = memoryWriter->GetMessages();
//     EXPECT_EQ(messages.size(), 13);

//     // cpu_throttle_v2
//     EXPECT_EQ(messages.at(0), "c:cgroup.cpu.throttledTime:1.000000\n");
//     EXPECT_EQ(messages.at(1), "C:cgroup.cpu.numThrottled:4.000000\n");

//     // cpu_time_v2
//     EXPECT_EQ(messages.at(2), "c:cgroup.cpu.processingTime:30.000000\n");
//     EXPECT_EQ(messages.at(3), "c:cgroup.cpu.usageTime,id=system:120.000000\n");
//     EXPECT_EQ(messages.at(4), "c:cgroup.cpu.usageTime,id=user:60.000000\n");

//     // cpu_utilization_v2
//     EXPECT_EQ(messages.at(5), "g:cgroup.cpu.weight:100.000000\n");
//     EXPECT_EQ(messages.at(6), "c:cgroup.cpu.processingCapacity:5.000000\n");
//     EXPECT_EQ(messages.at(7), "g:sys.cpu.numProcessors:1.000000\n");
//     EXPECT_EQ(messages.at(8), "g:titus.cpu.requested:1.000000\n");
//     EXPECT_EQ(messages.at(9), "g:sys.cpu.utilization,id=system:2400.000000\n");
//     EXPECT_EQ(messages.at(10), "g:sys.cpu.utilization,id=user:1200.000000\n");

//     // cpu_peak_utilization_v2
//     EXPECT_EQ(messages.at(11), "m:sys.cpu.peakUtilization,id=system:2400.000000\n");
//     EXPECT_EQ(messages.at(12), "m:sys.cpu.peakUtilization,id=user:1200.000000\n");
// }

TEST(CGroup, ParseMemoryV2)
{
    auto config = Config(WriterConfig(WriterTypes::Memory));
    Registry registry(config);
    CGroupTest cGroup{&registry, "lib/collectors/cgroup/test/resources/sample1"};

    cGroup.MemoryStatsV2();
    cGroup.MemoryStatsStdV2();

    auto memoryWriter = static_cast<MemoryWriter*>(WriterTestHelper::GetImpl());
    auto messages = memoryWriter->GetMessages();

    EXPECT_EQ(messages.size(), 17);

    // memory_stats_v2
    EXPECT_EQ(messages.at(0), "g:cgroup.mem.used:7841374208.000000\n");
    EXPECT_EQ(messages.at(1), "g:cgroup.mem.limit:8589934592.000000\n");
    EXPECT_EQ(messages.at(2), "C:cgroup.mem.failures:0.000000\n");

    EXPECT_EQ(messages.at(3), "g:cgroup.mem.processUsage,id=cache:11218944.000000\n");
    EXPECT_EQ(messages.at(4), "g:cgroup.mem.processUsage,id=rss:1.000000\n");
    EXPECT_EQ(messages.at(5), "g:cgroup.mem.processUsage,id=rss_huge:2.000000\n");
    EXPECT_EQ(messages.at(6), "g:cgroup.mem.processUsage,id=mapped_file:0.000000\n");

    EXPECT_EQ(messages.at(7), "C:cgroup.mem.pageFaults,id=minor:0.000000\n");
    EXPECT_EQ(messages.at(8), "C:cgroup.mem.pageFaults,id=major:0.000000\n");

    // memory_stats_std_v2
    EXPECT_EQ(messages.at(9), "g:mem.cached:11218944.000000\n");
    EXPECT_EQ(messages.at(10), "g:mem.shared:135168.000000\n");
    EXPECT_EQ(messages.at(11), "g:mem.availReal:759779328.000000\n");
    EXPECT_EQ(messages.at(12), "g:mem.freeReal:748560384.000000\n");
    EXPECT_EQ(messages.at(13), "g:mem.totalReal:8589934592.000000\n");
    EXPECT_EQ(messages.at(14), "g:mem.availSwap:536870912.000000\n");
    EXPECT_EQ(messages.at(15), "g:mem.totalSwap:536870912.000000\n");
    EXPECT_EQ(messages.at(16), "g:mem.totalFree:1296650240.000000\n");
}
