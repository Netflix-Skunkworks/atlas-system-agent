#include <lib/collectors/service_monitor/src/service_monitor_utils.cpp>
#include <lib/util/src/util.h>

#include <gtest/gtest.h>

TEST(ServiceMonitorTest, ParseValidConfig)
{
    auto filepath{"testdata/resources2/service_monitor/regex_directory/valid_regex.systemd-unit"};
    auto config = parse_regex_config_file(filepath);
    EXPECT_NE(std::nullopt, config);
    EXPECT_EQ(12, config.value().size());
}

TEST(ServiceMonitorTest, ParseRegexDirectory)
{
    auto directory{"testdata/resources2/service_monitor/regex_directory"};
    auto config = parse_service_monitor_config_directory(directory);
    EXPECT_NE(std::nullopt, config);
    EXPECT_EQ(20, config.value().size());
}

TEST(ServiceMonitorTest, ParseNumberOfCores)
{
    auto filepath{"testdata/resources2/service_monitor/valid-cpu-info.txt"};
    auto fileContents = atlasagent::read_file(filepath);
    EXPECT_NE(std::nullopt, fileContents);
    auto coreCount = parse_cores(fileContents.value()[0]);
    EXPECT_EQ(96, coreCount);
}

TEST(ServiceMonitorTest, ParseProcStat)
{
    auto filepath{"testdata/resources2/service_monitor/valid-proc-stat-info.txt"};
    auto fileContents = atlasagent::read_file(filepath);
    EXPECT_NE(std::nullopt, fileContents);
    auto cpuTime = parse_cpu_time(fileContents.value());
    EXPECT_EQ(46255609, cpuTime);
}

TEST(ServiceMonitorTest, ParseProcPidStat)
{
    auto filepath{"testdata/resources2/service_monitor/valid-proc-pid-stat-info.txt"};
    auto fileContents = atlasagent::read_file(filepath);
    EXPECT_NE(std::nullopt, fileContents);
    auto processTimes = parse_process_times(fileContents.value());
    auto rss = parse_rss(fileContents.value());
    EXPECT_EQ(5, processTimes.value().uTime);
    EXPECT_EQ(6, processTimes.value().sTime);
    EXPECT_EQ(2933, rss);
}

TEST(ServiceMonitorTest, GetCgroupCpuUsec)
{
    auto usec = get_cgroup_cpu_usec("testdata/resources2/service_monitor", "/cgroup_cpu_stat");
    ASSERT_TRUE(usec.has_value());
    EXPECT_EQ(1234567890ULL, usec.value());
}

TEST(ServiceMonitorTest, GetCgroupCpuUsecHandlesEmptyPath)
{
    EXPECT_FALSE(get_cgroup_cpu_usec("testdata/resources2/service_monitor", "").has_value());
}

TEST(ServiceMonitorTest, GetCgroupCpuUsecHandlesMissingFile)
{
    EXPECT_FALSE(
        get_cgroup_cpu_usec("testdata/resources2/service_monitor", "/does_not_exist").has_value());
}

TEST(ServiceMonitorTest, CalculateCgroupCpuUsageOneCoreBusy)
{
    // 1 full core for 60s = 60_000_000 usec consumed
    EXPECT_DOUBLE_EQ(100.0, calculate_cgroup_cpu_usage(0ULL, 60'000'000ULL, 60.0));
}

TEST(ServiceMonitorTest, CalculateCgroupCpuUsageMultipleCoresBusy)
{
    // 4 cores busy for 60s = 240_000_000 usec consumed
    EXPECT_DOUBLE_EQ(400.0, calculate_cgroup_cpu_usage(0ULL, 240'000'000ULL, 60.0));
}

TEST(ServiceMonitorTest, CalculateCgroupCpuUsageZeroInterval)
{
    EXPECT_DOUBLE_EQ(0.0, calculate_cgroup_cpu_usage(0ULL, 1'000'000ULL, 0.0));
}

TEST(ServiceMonitorTest, CalculateCgroupCpuUsageHandlesCounterReset)
{
    // Defensive: if usage_usec ever drops between samples (e.g. cgroup recreated), report 0
    // rather than wrapping into a huge value.
    EXPECT_DOUBLE_EQ(0.0, calculate_cgroup_cpu_usage(500ULL, 100ULL, 60.0));
}
