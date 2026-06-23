#include <lib/collectors/service_monitor/src/cpu_rate_tracker.h>
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

TEST(ServiceMonitorTest, ParseProcPidStatRss)
{
    auto filepath{"testdata/resources2/service_monitor/valid-proc-pid-stat-info.txt"};
    auto fileContents = atlasagent::read_file(filepath);
    EXPECT_NE(std::nullopt, fileContents);
    auto rss = parse_rss(fileContents.value());
    EXPECT_EQ(2933, rss);
}

TEST(ServiceMonitorTest, ParseProcPidStatProcessTimes)
{
    auto filepath{"testdata/resources2/service_monitor/valid-proc-pid-stat-info.txt"};
    auto fileContents = atlasagent::read_file(filepath);
    EXPECT_NE(std::nullopt, fileContents);
    auto processTimes = parse_process_times(fileContents.value());
    EXPECT_NE(std::nullopt, processTimes);
    EXPECT_EQ(5, processTimes.value().uTime);
    EXPECT_EQ(6, processTimes.value().sTime);
}

// Demonstrates the /proc/[pid]/stat comm-parsing bug: field 2 (comm) is the process name in
// parentheses and may contain spaces (e.g. Firefox's "Web Content"). The current parser splits the
// whole line on spaces and uses fixed token indices, so a space in comm shifts every later field.
// These two tests assert the CORRECT values (utime=5, stime=6, rss=2933 — same fields as the
// no-space fixture) and therefore FAIL against the current code (which returns 0, 5, and 21966848)
// until parse_process_times / parse_rss are fixed to parse from the last ')'.
TEST(ServiceMonitorTest, ParseProcPidStatCommWithSpaceProcessTimes)
{
    auto filepath{"testdata/resources2/service_monitor/proc-pid-stat-comm-with-space.txt"};
    auto fileContents = atlasagent::read_file(filepath);
    EXPECT_NE(std::nullopt, fileContents);
    auto processTimes = parse_process_times(fileContents.value());
    EXPECT_NE(std::nullopt, processTimes);
    EXPECT_EQ(5, processTimes.value().uTime);
    EXPECT_EQ(6, processTimes.value().sTime);
}

TEST(ServiceMonitorTest, ParseProcPidStatCommWithSpaceRss)
{
    auto filepath{"testdata/resources2/service_monitor/proc-pid-stat-comm-with-space.txt"};
    auto fileContents = atlasagent::read_file(filepath);
    EXPECT_NE(std::nullopt, fileContents);
    auto rss = parse_rss(fileContents.value());
    EXPECT_EQ(2933, rss);
}

TEST(ServiceMonitorTest, ParseCgroupCpuStat)
{
    auto filepath{"testdata/resources2/service_monitor/valid-cpu-stat.txt"};
    auto fileContents = atlasagent::read_file(filepath);
    EXPECT_NE(std::nullopt, fileContents);
    auto usageUsec = parse_cgroup_cpu_stat(fileContents.value());
    EXPECT_NE(std::nullopt, usageUsec);
    EXPECT_EQ(123456789ULL, usageUsec.value());
}

// A cpu.stat without the usage_usec key (e.g. a cgroup-v1 host) yields nullopt, not a misleading 0
// that would later be treated as a real counter value.
TEST(ServiceMonitorTest, ParseCgroupCpuStatMissingKey)
{
    std::vector<std::string> lines{"user_usec 100", "system_usec 50", "nr_periods 0"};
    EXPECT_EQ(std::nullopt, parse_cgroup_cpu_stat(lines));
}

// A truncated "usage_usec" line with no value must return nullopt, NOT throw. Under the old code
// substr(prefix.size() + 1) threw std::out_of_range, which unwound out of the per-service loop and
// aborted metric collection for every remaining service that cycle.
TEST(ServiceMonitorTest, ParseCgroupCpuStatTruncatedKey)
{
    std::vector<std::string> lines{"usage_usec"};
    EXPECT_EQ(std::nullopt, parse_cgroup_cpu_stat(lines));
}

// A non-numeric usage_usec value must return nullopt, NOT throw (std::stoull threw before).
TEST(ServiceMonitorTest, ParseCgroupCpuStatMalformedValue)
{
    std::vector<std::string> lines{"usage_usec notanumber"};
    EXPECT_EQ(std::nullopt, parse_cgroup_cpu_stat(lines));
}

TEST(ServiceMonitorTest, ParseCgroupMemory)
{
    auto filepath{"testdata/resources2/service_monitor/valid-memory-current.txt"};
    auto fileContents = atlasagent::read_file(filepath);
    EXPECT_NE(std::nullopt, fileContents);
    EXPECT_FALSE(fileContents.value().empty());
    auto memory = std::stoull(fileContents.value().at(0));
    EXPECT_EQ(52428800ULL, memory);
}

TEST(ServiceMonitorTest, ParseCgroupPids)
{
    auto filepath{"testdata/resources2/service_monitor/valid-cgroup-procs.txt"};
    auto fileContents = atlasagent::read_file(filepath);
    EXPECT_NE(std::nullopt, fileContents);

    std::vector<unsigned int> pids{};
    for (const auto& line : fileContents.value())
    {
        if (!line.empty())
        {
            pids.emplace_back(std::stoul(line));
        }
    }
    EXPECT_EQ(3u, pids.size());
    EXPECT_EQ(1001u, pids[0]);
    EXPECT_EQ(1002u, pids[1]);
    EXPECT_EQ(1003u, pids[2]);
}

// ── CpuRateTracker ────────────────────────────────────────────────────────────
// Pure CPU-utilization state machine, no systemd / D-Bus / spectator dependency. Synthetic,
// monotonic timestamps keep these deterministic. The convention is 100% == one core fully busy, so
// a CPU-time delta in microseconds equal to the wall-clock delta in microseconds is exactly 100%.

namespace
{
std::chrono::steady_clock::time_point at_seconds(long long s)
{
    return std::chrono::steady_clock::time_point(std::chrono::seconds(s));
}
}  // namespace

// The first sample has no prior baseline to subtract from, so it reports nothing while still arming
// the tracker for the next call.
TEST(CpuRateTrackerTest, FirstSampleReturnsNullopt)
{
    CpuRateTracker<unsigned int> tracker;
    EXPECT_EQ(std::nullopt, tracker.update(100, 5'000'000, at_seconds(0)));
}

// One core fully busy: 1s of CPU time consumed over 1s of wall time == 100%.
TEST(CpuRateTrackerTest, OneCoreFullyBusyIsOneHundredPercent)
{
    CpuRateTracker<unsigned int> tracker;
    tracker.update(100, 0, at_seconds(0));
    auto pct = tracker.update(100, 1'000'000, at_seconds(1));
    ASSERT_TRUE(pct.has_value());
    EXPECT_DOUBLE_EQ(100.0, *pct);
}

// Half a core: 0.5s of CPU over 1s of wall == 50%.
TEST(CpuRateTrackerTest, HalfCoreIsFiftyPercent)
{
    CpuRateTracker<unsigned int> tracker;
    tracker.update(100, 0, at_seconds(0));
    auto pct = tracker.update(100, 500'000, at_seconds(1));
    ASSERT_TRUE(pct.has_value());
    EXPECT_DOUBLE_EQ(50.0, *pct);
}

// Multi-threaded work legitimately exceeds 100%: 4s of CPU over 2s of wall == 200% (two cores).
TEST(CpuRateTrackerTest, MultipleCoresExceedOneHundredPercent)
{
    CpuRateTracker<unsigned int> tracker;
    tracker.update(100, 0, at_seconds(0));
    auto pct = tracker.update(100, 4'000'000, at_seconds(2));
    ASSERT_TRUE(pct.has_value());
    EXPECT_DOUBLE_EQ(200.0, *pct);
}

// An idle interval (counter unchanged) reports a real 0%, not nullopt: the guard is
// counterUs >= prev, so an equal counter is a valid zero-usage sample rather than a suppressed one.
TEST(CpuRateTrackerTest, IdleIntervalReportsZero)
{
    CpuRateTracker<unsigned int> tracker;
    tracker.update(100, 5'000'000, at_seconds(0));
    auto pct = tracker.update(100, 5'000'000, at_seconds(1));
    ASSERT_TRUE(pct.has_value());
    EXPECT_DOUBLE_EQ(0.0, *pct);
}

// A restart that changes the main PID is an identity change: the cross-generation sample is
// suppressed (even though the new counter is lower), and the tracker rebaselines on the new PID so
// the next same-PID interval computes correctly.
TEST(CpuRateTrackerTest, IdentityChangeReturnsNulloptAndRebaselines)
{
    CpuRateTracker<unsigned int> tracker;
    tracker.update(100, 9'000'000, at_seconds(0));
    EXPECT_EQ(std::nullopt, tracker.update(200, 1'000'000, at_seconds(1)));  // new PID -> no delta
    auto pct = tracker.update(200, 2'000'000, at_seconds(2));               // off the PID-200 baseline
    ASSERT_TRUE(pct.has_value());
    EXPECT_DOUBLE_EQ(100.0, *pct);
}

// Same identity but the counter went backward (a cgroup recreated at the same path, or PID reuse
// where the new process has less accumulated CPU) is a reset: suppressed, then rebaselined.
TEST(CpuRateTrackerTest, CounterResetReturnsNulloptAndRebaselines)
{
    CpuRateTracker<unsigned int> tracker;
    tracker.update(100, 9'000'000, at_seconds(0));
    EXPECT_EQ(std::nullopt, tracker.update(100, 1'000'000, at_seconds(1)));  // counter dropped
    auto pct = tracker.update(100, 2'000'000, at_seconds(2));               // off the reset baseline
    ASSERT_TRUE(pct.has_value());
    EXPECT_DOUBLE_EQ(100.0, *pct);
}

// No elapsed wall time cannot yield a rate; the divide-by-zero is guarded and reported as no sample.
TEST(CpuRateTrackerTest, ZeroWallTimeReturnsNullopt)
{
    CpuRateTracker<unsigned int> tracker;
    tracker.update(100, 0, at_seconds(5));
    EXPECT_EQ(std::nullopt, tracker.update(100, 1'000'000, at_seconds(5)));
}

// When a service is skipped for several cycles the wall window is measured from the stored baseline
// timestamp, so the rate is the true average over the whole gap (5s CPU / 10s wall == 50%), not a
// one-cycle spike.
TEST(CpuRateTrackerTest, MultiCycleGapAveragesOverWholeGap)
{
    CpuRateTracker<unsigned int> tracker;
    tracker.update(100, 0, at_seconds(0));
    auto pct = tracker.update(100, 5'000'000, at_seconds(10));
    ASSERT_TRUE(pct.has_value());
    EXPECT_DOUBLE_EQ(50.0, *pct);
}

// The cgroup scope keys on the control-group path (a std::string identity): it computes a rate for a
// stable path and treats a changed path as an identity change.
TEST(CpuRateTrackerTest, StringIdentityComputesRateAndDetectsCgroupChange)
{
    CpuRateTracker<std::string> tracker;
    tracker.update("/system.slice/foo.service", 0, at_seconds(0));
    auto pct = tracker.update("/system.slice/foo.service", 2'000'000, at_seconds(1));
    ASSERT_TRUE(pct.has_value());
    EXPECT_DOUBLE_EQ(200.0, *pct);
    EXPECT_EQ(std::nullopt, tracker.update("/system.slice/bar.service", 3'000'000, at_seconds(2)));
}
