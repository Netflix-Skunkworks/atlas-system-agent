#include <lib/collectors/cgroup/src/cgroup.h>
#include <lib/util/src/util.h>
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

    // Expose protected members and methods for testing
    using CGroup::CpuPeakUtilizationV2;
    using CGroup::CpuProcessingCapacity;
    using CGroup::CpuThrottleV2;
    using CGroup::CpuTimeV2;
    using CGroup::CpuUtilizationV2;
    using CGroup::GetNumCpu;
    using CGroup::path_prefix_;
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

TEST(CGroup, CpuThrottleV2)
{
    auto config = Config(WriterConfig(WriterTypes::Memory));
    Registry registry(config);
    CGroupTest cGroup{&registry, "lib/collectors/cgroup/test/resources/sample1"};

    std::unordered_map<std::string, int64_t> stats;
    atlasagent::parse_kv_from_file(cGroup.path_prefix_, "cpu.stat", &stats);
    cGroup.CpuThrottleV2(stats);

    auto memoryWriter = static_cast<MemoryWriter*>(WriterTestHelper::GetImpl());
    auto messages = memoryWriter->GetMessages();
    EXPECT_EQ(messages.size(), 1);
    EXPECT_EQ(messages.at(0), "C:cgroup.cpu.numThrottled:0.000000\n");

    memoryWriter->Clear();

    // Second call to compute delta
    cGroup.SetPrefix("lib/collectors/cgroup/test/resources/sample2");
    atlasagent::parse_kv_from_file(cGroup.path_prefix_, "cpu.stat", &stats);
    cGroup.CpuThrottleV2(stats);
    messages = memoryWriter->GetMessages();
    EXPECT_EQ(messages.size(), 2);
    EXPECT_EQ(messages.at(0), "c:cgroup.cpu.throttledTime:6.000000\n");
    EXPECT_EQ(messages.at(1), "C:cgroup.cpu.numThrottled:5.000000\n");
}

TEST(CGroup, CpuUtilizationV2)
{
    auto config = Config(WriterConfig(WriterTypes::Memory));
    Registry registry(config);
    CGroupTest cGroup{&registry, "lib/collectors/cgroup/test/resources/sample1"};
    setenv("TITUS_NUM_CPU", "1", 1);

    std::unordered_map<std::string, int64_t> stats;
    atlasagent::parse_kv_from_file(cGroup.path_prefix_, "cpu.stat", &stats);

    // Use a fixed base time for consistent testing
    auto baseTime = absl::FromUnixSeconds(1000000000);  // Fixed timestamp
    auto cpuCount = cGroup.GetNumCpu();
    cGroup.CpuUtilizationV2(baseTime, cpuCount, stats, absl::Seconds(60));

    auto memoryWriter = static_cast<MemoryWriter*>(WriterTestHelper::GetImpl());
    auto messages = memoryWriter->GetMessages();
    EXPECT_EQ(messages.size(), 3);
    EXPECT_EQ(messages.at(0), "g:cgroup.cpu.weight:100.000000\n");
    EXPECT_EQ(messages.at(1), "g:sys.cpu.numProcessors:1.000000\n");
    EXPECT_EQ(messages.at(2), "g:titus.cpu.requested:1.000000\n");
    memoryWriter->Clear();

    // Second call after 60 seconds to compute utilization
    cGroup.SetPrefix("lib/collectors/cgroup/test/resources/sample2");
    atlasagent::parse_kv_from_file(cGroup.path_prefix_, "cpu.stat", &stats);
    cGroup.CpuUtilizationV2(baseTime + absl::Seconds(60), cpuCount, stats, absl::Seconds(60));

    messages = memoryWriter->GetMessages();
    EXPECT_EQ(messages.size(), 5);
    EXPECT_EQ(messages.at(0), "g:cgroup.cpu.weight:100.000000\n");
    EXPECT_EQ(messages.at(1), "g:sys.cpu.numProcessors:1.000000\n");
    EXPECT_EQ(messages.at(2), "g:titus.cpu.requested:1.000000\n");
    EXPECT_EQ(messages.at(3), "g:sys.cpu.utilization,id=system:66.666667\n");
    EXPECT_EQ(messages.at(4), "g:sys.cpu.utilization,id=user:33.333333\n");
}

TEST(CGroup, CpuTimeV2)
{
    auto config = Config(WriterConfig(WriterTypes::Memory));
    Registry registry(config);
    CGroupTest cGroup{&registry, "lib/collectors/cgroup/test/resources/sample1"};

    std::unordered_map<std::string, int64_t> stats;
    atlasagent::parse_kv_from_file(cGroup.path_prefix_, "cpu.stat", &stats);
    cGroup.CpuTimeV2(stats);

    auto memoryWriter = static_cast<MemoryWriter*>(WriterTestHelper::GetImpl());
    auto messages = memoryWriter->GetMessages();
    EXPECT_EQ(messages.size(), 0);

    // Second call after 60 seconds to compute utilization
    cGroup.SetPrefix("lib/collectors/cgroup/test/resources/sample2");
    atlasagent::parse_kv_from_file(cGroup.path_prefix_, "cpu.stat", &stats);
    cGroup.CpuTimeV2(stats);

    messages = memoryWriter->GetMessages();
    EXPECT_EQ(messages.size(), 3);
    EXPECT_EQ(messages.at(0), "c:cgroup.cpu.processingTime:60.000000\n");
    EXPECT_EQ(messages.at(1), "c:cgroup.cpu.usageTime,id=system:40.000000\n");
    EXPECT_EQ(messages.at(2), "c:cgroup.cpu.usageTime,id=user:20.000000\n");
}

TEST(CGroup, ProcessingTime)
{
    auto config = Config(WriterConfig(WriterTypes::Memory));
    Registry registry(config);
    CGroupTest cGroup{&registry, "lib/collectors/cgroup/test/resources/sample1"};
    setenv("TITUS_NUM_CPU", "1", 1);

    // Use a fixed base time for consistent testing
    auto baseTime = absl::FromUnixSeconds(1000000000);  // Fixed timestamp
    auto cpuCount = cGroup.GetNumCpu();
    cGroup.CpuProcessingCapacity(baseTime, cpuCount, absl::Seconds(5));

    auto memoryWriter = static_cast<MemoryWriter*>(WriterTestHelper::GetImpl());
    auto messages = memoryWriter->GetMessages();
    EXPECT_EQ(messages.size(), 1);
    EXPECT_EQ(messages.at(0), "c:cgroup.cpu.processingCapacity:5.000000\n");
    memoryWriter->Clear();

    cGroup.CpuProcessingCapacity(baseTime + absl::Seconds(30), cpuCount, absl::Seconds(5));

    messages = memoryWriter->GetMessages();
    EXPECT_EQ(messages.size(), 1);
    EXPECT_EQ(messages.at(0), "c:cgroup.cpu.processingCapacity:30.000000\n");
}

TEST(CGroup, CpuPeakUtilizationV2)
{
    auto config = Config(WriterConfig(WriterTypes::Memory));
    Registry registry(config);
    CGroupTest cGroup{&registry, "lib/collectors/cgroup/test/resources/sample1"};
    setenv("TITUS_NUM_CPU", "1", 1);

    std::unordered_map<std::string, int64_t> stats;
    atlasagent::parse_kv_from_file(cGroup.path_prefix_, "cpu.stat", &stats);
    auto baseTime = absl::FromUnixSeconds(1000000000);  // Fixed timestamp
    auto cpuCount = cGroup.GetNumCpu();

    cGroup.CpuPeakUtilizationV2(baseTime, stats, cpuCount);
    auto memoryWriter = static_cast<MemoryWriter*>(WriterTestHelper::GetImpl());
    auto messages = memoryWriter->GetMessages();
    EXPECT_EQ(messages.size(), 0);

    cGroup.SetPrefix("lib/collectors/cgroup/test/resources/sample2");
    atlasagent::parse_kv_from_file(cGroup.path_prefix_, "cpu.stat", &stats);
    cGroup.CpuPeakUtilizationV2(baseTime + absl::Seconds(60), stats, cpuCount);

    messages = memoryWriter->GetMessages();
    EXPECT_EQ(messages.size(), 2);
    EXPECT_EQ(messages.at(0), "m:sys.cpu.peakUtilization,id=system:66.666667\n");
    EXPECT_EQ(messages.at(1), "m:sys.cpu.peakUtilization,id=user:33.333333\n");
}

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

// Test case structure for invalid file tests
struct InvalidFileTestCase
{
    std::string filename;
    bool expectedResult;
    std::string description;
};

// Common test cases that apply to both io.stat and io.max (excluding type-specific ones)
std::vector<InvalidFileTestCase> GetCommonInvalidTestCases(const std::string& prefix)
{
    return {{prefix + ".duplicate_keys", false, "duplicate keys"},
            {prefix + ".empty", true, "empty file (no lines to process)"},
            {prefix + ".incomplete_lines", false, "incomplete lines"},
            {prefix + ".invalid_keys", false, "invalid key names"},
            {prefix + ".malformed_pairs", false, "malformed key=value pairs"},
            {prefix + ".missing_fields", false, "missing required fields"},
            {prefix + ".mixed_validity", false, "mixed valid/invalid lines"},
            {prefix + ".non_numeric", false, "non-numeric values"},
            {prefix + ".too_many_fields", false, "lines with too many fields"}};
}

TEST(CGroup, InvalidIOStats)
{
    auto testCases = GetCommonInvalidTestCases("io.stat");
    // Add io.stat specific test case
    testCases.push_back({"io.stat.negative_values", false, "negative values"});

    // Create a simple device map for testing
    std::unordered_map<std::string, std::string> deviceMap = {{"8:0", "sda"}, {"8:1", "sda1"}, {"259:0", "nvme0n1"}};

    for (const auto& testCase : testCases)
    {
        auto lines = atlasagent::read_lines_fields("lib/collectors/cgroup/test/resources/invalid_tests/io.stat/",
                                                   testCase.filename.c_str());
        auto result = atlasagent::ParseIOLines(lines, deviceMap);

        if (testCase.expectedResult)
        {
            // For empty files, success means returning an empty map
            if (testCase.filename == "io.stat.empty")
            {
                EXPECT_TRUE(result.empty()) << "No data should be parsed from empty file";
            }
            else
            {
                EXPECT_FALSE(result.empty()) << "ParseIOLines should return non-empty map for " << testCase.description;
            }
        }
        else
        {
            EXPECT_TRUE(result.empty()) << "ParseIOLines should return empty map for " << testCase.description;
        }
    }
}

TEST(CGroup, InvalidIOMaxStats)
{
    auto testCases = GetCommonInvalidTestCases("io.max");
    // Add io.max specific test case
    testCases.push_back({"io.max.negative_values", false, "negative values"});

    for (const auto& testCase : testCases)
    {
        auto lines = atlasagent::read_lines_fields("lib/collectors/cgroup/test/resources/invalid_tests/io.max/",
                                                   testCase.filename.c_str());
        auto result = atlasagent::ParseIOThrottleLines(lines);

        if (testCase.expectedResult)
        {
            // For empty files, success means returning an empty map
            if (testCase.filename == "io.max.empty")
            {
                EXPECT_TRUE(result.empty()) << "No data should be parsed from empty file";
            }
            else
            {
                EXPECT_FALSE(result.empty())
                    << "ParseIOThrottleLines should return non-empty map for " << testCase.description;
            }
        }
        else
        {
            EXPECT_TRUE(result.empty()) << "ParseIOThrottleLines should return empty map for " << testCase.description;
        }
    }
}

TEST(CGroup, IOStats)
{
    auto config = Config(WriterConfig(WriterTypes::Memory));
    Registry registry(config);
    CGroupTest cGroup{&registry, "lib/collectors/cgroup/test/resources/sample1"};

    auto memoryWriter = static_cast<MemoryWriter*>(WriterTestHelper::GetImpl());

    cGroup.IOStats();
    auto messages = memoryWriter->GetMessages();
    EXPECT_TRUE(messages.empty());
    cGroup.SetPrefix("lib/collectors/cgroup/test/resources/sample2");
    cGroup.IOStats();
    messages = memoryWriter->GetMessages();

    auto expectedMessages = std::vector<std::string>{"c:disk.io.bytes,id=read,dev=unknown:2000.000000\n",
                                                     "c:disk.io.bytes,id=write,dev=unknown:2000.000000\n",
                                                     "c:disk.io.ops,statistic=count,id=read,dev=unknown:2000.000000\n",
                                                     "c:disk.io.ops,statistic=count,id=write,dev=unknown:2000.000000\n",

                                                     "c:disk.io.bytes,id=read,dev=unknown:2000.000000\n",
                                                     "c:disk.io.bytes,id=write,dev=unknown:2000.000000\n",
                                                     "c:disk.io.ops,statistic=count,id=read,dev=unknown:2000.000000\n",
                                                     "c:disk.io.ops,statistic=count,id=write,dev=unknown:2000.000000\n",
                                                     "d:cgroup.disk.io.throttleActivityOperations,id=write,dev=unknown:40.000000\n",

                                                     "c:disk.io.bytes,id=read,dev=unknown:500.000000\n",
                                                     "c:disk.io.bytes,id=write,dev=unknown:500.000000\n",
                                                     "c:disk.io.ops,statistic=count,id=read,dev=unknown:500.000000\n",
                                                     "c:disk.io.ops,statistic=count,id=write,dev=unknown:500.000000\n",
                                                     "d:cgroup.disk.io.throttleActivityBytes,id=read,dev=unknown:10.000000\n",
                                                     "d:cgroup.disk.io.throttleActivityBytes,id=write,dev=unknown:10.000000\n",
                                                     "d:cgroup.disk.io.throttleActivityOperations,id=read,dev=unknown:10.000000\n",
                                                     "d:cgroup.disk.io.throttleActivityOperations,id=write,dev=unknown:10.000000\n",

                                                     "c:disk.io.bytes,id=read,dev=unknown:500.000000\n",
                                                     "c:disk.io.bytes,id=write,dev=unknown:500.000000\n",
                                                     "c:disk.io.ops,statistic=count,id=read,dev=unknown:500.000000\n",
                                                     "c:disk.io.ops,statistic=count,id=write,dev=unknown:500.000000\n",
                                                     "d:cgroup.disk.io.throttleActivityOperations,id=read,dev=unknown:10.000000\n",
                                                     "d:cgroup.disk.io.throttleActivityOperations,id=write,dev=unknown:10.000000\n"};
    EXPECT_EQ(messages.size(), expectedMessages.size());

    // Convert to sets to handle the unordered nature of unordered_map iteration
    std::set<std::string> messageSet(messages.begin(), messages.end());
    std::set<std::string> expectedSet(expectedMessages.begin(), expectedMessages.end());

    EXPECT_EQ(messageSet, expectedSet);
}