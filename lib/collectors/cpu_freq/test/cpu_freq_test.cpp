#include <lib/collectors/cpu_freq/src/cpu_freq.h>
#include <lib/logger/src/logger.h>

#include <thirdparty/spectator-cpp/spectator/registry.h>
#include <thirdparty/spectator-cpp/libs/writer/writer_wrapper/writer_test_helper.h>

#include <gtest/gtest.h>
#include <filesystem>
#include <set>

using spectator::Config;
using spectator::MemoryWriter;
using spectator::Registry;
using spectator::WriterConfig;
using spectator::WriterTestHelper;
using spectator::WriterTypes;

namespace
{
using atlasagent::CpuFreq;

TEST(CpuFreq, Stats)
{
    auto config = Config(WriterConfig(WriterTypes::Memory));
    auto r = Registry(config);

    CpuFreq cpuFreq{&r, "lib/collectors/cpu_freq/test/resources/valid"};
    cpuFreq.Stats();

    auto memoryWriter = static_cast<MemoryWriter*>(WriterTestHelper::GetImpl());
    auto messages = memoryWriter->GetMessages();

    EXPECT_EQ(messages.size(), 12);

    std::multiset<std::string> expected = {
        "d:sys.minCoreFrequency:1200000.000000\n", "d:sys.maxCoreFrequency:3500000.000000\n",
        "d:sys.curCoreFrequency:1200188.000000\n", "d:sys.minCoreFrequency:1200000.000000\n",
        "d:sys.maxCoreFrequency:3500000.000000\n", "d:sys.curCoreFrequency:1200484.000000\n",
        "d:sys.minCoreFrequency:1200000.000000\n", "d:sys.maxCoreFrequency:3500000.000000\n",
        "d:sys.curCoreFrequency:2620000.000000\n", "d:sys.minCoreFrequency:1200000.000000\n",
        "d:sys.maxCoreFrequency:3500000.000000\n", "d:sys.curCoreFrequency:3000000.000000\n"};

    std::multiset<std::string> actual(messages.begin(), messages.end());

    EXPECT_EQ(actual, expected);
}

TEST(CpuFreq, MissingDirectoryRecordsNothing)
{
    auto config = Config(WriterConfig(WriterTypes::Memory));
    auto r = Registry(config);
    auto memoryWriter = static_cast<MemoryWriter*>(WriterTestHelper::GetImpl());
    memoryWriter->Clear();

    // hosts without a cpufreq driver (common on VMs) have no cpufreq directory
    const CpuFreq cpuFreq{&r, "lib/collectors/cpu_freq/test/resources/does-not-exist"};
    cpuFreq.Stats();

    EXPECT_TRUE(memoryWriter->GetMessages().empty());
}

TEST(CpuFreq, InvalidContentsRecordNothing)
{
    auto config = Config(WriterConfig(WriterTypes::Memory));
    auto r = Registry(config);
    auto memoryWriter = static_cast<MemoryWriter*>(WriterTestHelper::GetImpl());
    memoryWriter->Clear();

    // The directory exists, but no policy is complete. Each one fails a different check, and the
    // other files in it are valid, so recording any metric before all three reads succeed shows up
    // as a message:
    //   policy0: scaling_min_freq is not a number
    //   policy1: scaling_max_freq is empty
    //   policy2: scaling_cur_freq is missing
    //   policy3: scaling_cur_freq is negative
    //   not_a_policy: a plain file, not a directory
    const std::string path = "lib/collectors/cpu_freq/test/resources/invalid";
    ASSERT_TRUE(std::filesystem::is_directory(path)) << "fixture missing, the test would pass vacuously";

    const CpuFreq cpuFreq{&r, path};
    cpuFreq.Stats();

    EXPECT_TRUE(memoryWriter->GetMessages().empty());
}

}  // namespace
