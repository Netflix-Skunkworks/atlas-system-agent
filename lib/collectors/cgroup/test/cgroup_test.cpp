#include <lib/collectors/cgroup/src/cgroup.h>
#include <lib/util/src/util.h>
#include <gtest/gtest.h>
#include <thirdparty/spectator-cpp/spectator/registry.h>
#include <thirdparty/spectator-cpp/libs/writer/writer_wrapper/writer_test_helper.h>

#include <algorithm>
#include <string>
#include <unordered_map>
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

// utilization = (usage delta) / (delta_t * cpuCount), so the counter baseline
// (utilization_prev_*_time_) and the clock (utilization_last_updated_) MUST span the same window.
// CpuUtilizationV2 updates the two on either side of its unreadable-cpu.stat guard, so advancing
// the clock before that guard would leave the baseline behind, and the next successful tick would
// divide a two-interval usage delta by ONE interval of capacity.
//
// sample1 -> sample2 is a fixed 40s system / 20s user delta: 33.333333% / 16.666667% over the real
// 120s window, but 66.666667% / 33.333333% if delta_t were 60s -- exactly double, and exactly what
// CGroup.CpuUtilizationV2 above asserts for a genuine 60s window. So a regression here does not
// merely change a number, it reproduces a plausible-looking one.
TEST(CGroup, CpuUtilizationV2ClockDoesNotAdvanceOnUnreadableCpuStat)
{
    auto config = Config(WriterConfig(WriterTypes::Memory));
    Registry registry(config);
    CGroupTest cGroup{&registry, "lib/collectors/cgroup/test/resources/sample1"};
    setenv("TITUS_NUM_CPU", "1", 1);

    std::unordered_map<std::string, int64_t> stats;
    atlasagent::parse_kv_from_file(cGroup.path_prefix_, "cpu.stat", &stats);

    auto baseTime = absl::FromUnixSeconds(1000000000);
    auto cpuCount = cGroup.GetNumCpu();
    auto memoryWriter = static_cast<MemoryWriter*>(WriterTestHelper::GetImpl());

    // t=0: seeds the baselines. No utilization yet (prev_*_time_ is still -1).
    cGroup.CpuUtilizationV2(baseTime, cpuCount, stats, absl::Seconds(60));

    // t=60: cpu.stat unreadable, which parse_kv_from_file reports as an EMPTY map. The guard must
    // bail without publishing utilization AND without advancing the clock.
    memoryWriter->Clear();
    std::unordered_map<std::string, int64_t> unreadable;
    cGroup.CpuUtilizationV2(baseTime + absl::Seconds(60), cpuCount, unreadable, absl::Seconds(60));
    {
        auto skipped = memoryWriter->GetMessages();
        // Exactly the three pre-guard emissions: cgroup.cpu.weight (cpu.weight on disk is still
        // present) plus numProcessors/titus.cpu.requested (from cpuCount alone). Pinning the COUNT,
        // not just the needles below, is what makes a spurious extra emission on the bail-out path
        // visible.
        EXPECT_EQ(skipped.size(), 3);
        EXPECT_TRUE(std::any_of(skipped.begin(), skipped.end(),
                                 [](const std::string& m) { return m.find("sys.cpu.numProcessors") != std::string::npos; }));
        EXPECT_FALSE(std::any_of(skipped.begin(), skipped.end(),
                                  [](const std::string& m) { return m.find("sys.cpu.utilization") != std::string::npos; }));
    }

    // t=120: readable again. delta_t must span the FULL 120s, not the 60s since the skipped tick.
    cGroup.SetPrefix("lib/collectors/cgroup/test/resources/sample2");
    atlasagent::parse_kv_from_file(cGroup.path_prefix_, "cpu.stat", &stats);
    memoryWriter->Clear();
    cGroup.CpuUtilizationV2(baseTime + absl::Seconds(120), cpuCount, stats, absl::Seconds(60));

    auto messages = memoryWriter->GetMessages();
    auto has = [&messages](const std::string& needle) {
        return std::any_of(messages.begin(), messages.end(),
                            [&needle](const std::string& m) { return m.find(needle) != std::string::npos; });
    };
    // weight, numProcessors, titus.cpu.requested, utilization system, utilization user.
    EXPECT_EQ(messages.size(), 5);
    EXPECT_TRUE(has("g:sys.cpu.utilization,id=system:33.333333\n"));
    EXPECT_TRUE(has("g:sys.cpu.utilization,id=user:16.666667\n"));
    // What a clock advanced BEFORE the guard would produce -- see the doubling note above.
    EXPECT_FALSE(has("g:sys.cpu.utilization,id=system:66.666667\n"));
    EXPECT_FALSE(has("g:sys.cpu.utilization,id=user:33.333333\n"));
}

// Same defect and fix in CpuPeakUtilizationV2, where the consequence is worse: peakUtilization is a
// MaxGauge, so one inflated sample latches as the reported peak for the whole publishing interval
// instead of being averaged away.
TEST(CGroup, CpuPeakUtilizationV2ClockDoesNotAdvanceOnUnreadableCpuStat)
{
    auto config = Config(WriterConfig(WriterTypes::Memory));
    Registry registry(config);
    CGroupTest cGroup{&registry, "lib/collectors/cgroup/test/resources/sample1"};
    setenv("TITUS_NUM_CPU", "1", 1);

    std::unordered_map<std::string, int64_t> stats;
    atlasagent::parse_kv_from_file(cGroup.path_prefix_, "cpu.stat", &stats);

    auto baseTime = absl::FromUnixSeconds(1000000000);
    auto cpuCount = cGroup.GetNumCpu();
    auto memoryWriter = static_cast<MemoryWriter*>(WriterTestHelper::GetImpl());

    cGroup.CpuPeakUtilizationV2(baseTime, stats, cpuCount);

    std::unordered_map<std::string, int64_t> unreadable;
    cGroup.CpuPeakUtilizationV2(baseTime + absl::Seconds(60), unreadable, cpuCount);

    cGroup.SetPrefix("lib/collectors/cgroup/test/resources/sample2");
    atlasagent::parse_kv_from_file(cGroup.path_prefix_, "cpu.stat", &stats);
    memoryWriter->Clear();
    cGroup.CpuPeakUtilizationV2(baseTime + absl::Seconds(120), stats, cpuCount);

    auto messages = memoryWriter->GetMessages();
    auto has = [&messages](const std::string& needle) {
        return std::any_of(messages.begin(), messages.end(),
                            [&needle](const std::string& m) { return m.find(needle) != std::string::npos; });
    };
    // Peak emits only its two lines -- it reads no cpu.weight and no cpuCount gauges.
    EXPECT_EQ(messages.size(), 2);
    EXPECT_TRUE(has("sys.cpu.peakUtilization,id=system:33.333333\n"));
    EXPECT_TRUE(has("sys.cpu.peakUtilization,id=user:16.666667\n"));
    EXPECT_FALSE(has("sys.cpu.peakUtilization,id=system:66.666667\n"));
    EXPECT_FALSE(has("sys.cpu.peakUtilization,id=user:33.333333\n"));
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

// Regression test for the refactor that turned CpuTimeV2's (and its siblings') `static` delta
// locals into per-instance members: two CGroup instances must keep independent prev_* baselines.
// Against the old shared statics this fails twice -- step 2 would emit a 60/40/20 delta instead of
// nothing (A's step-1 call already primed the shared static), and step 3 would compute zero instead
// of 60/40/20 (B's step-2 call having clobbered the baseline with sample2's values).
TEST(CGroup, TwoInstancesIndependentCpuTimeState)
{
    auto config = Config(WriterConfig(WriterTypes::Memory));
    Registry registry(config);

    CGroupTest cGroupA{&registry, "lib/collectors/cgroup/test/resources/sample1"};
    CGroupTest cGroupB{&registry, "lib/collectors/cgroup/test/resources/sample1"};

    std::unordered_map<std::string, int64_t> stats1;
    atlasagent::parse_kv_from_file("lib/collectors/cgroup/test/resources/sample1", "cpu.stat", &stats1);
    std::unordered_map<std::string, int64_t> stats2;
    atlasagent::parse_kv_from_file("lib/collectors/cgroup/test/resources/sample2", "cpu.stat", &stats2);

    auto memoryWriter = static_cast<MemoryWriter*>(WriterTestHelper::GetImpl());

    // Step 1: instance A's first-ever call (baseline from sample1) -- no delta yet.
    cGroupA.CpuTimeV2(stats1);
    auto messages = memoryWriter->GetMessages();
    EXPECT_EQ(messages.size(), 0);
    memoryWriter->Clear();

    // Step 2: B's first-ever call (baseline from sample2) -- also no delta, so B's prev_* state
    // starts independent of what A just recorded.
    cGroupB.CpuTimeV2(stats2);
    messages = memoryWriter->GetMessages();
    EXPECT_EQ(messages.size(), 0);
    memoryWriter->Clear();

    // Step 3: A's second call reproduces the known-good delta from the single-instance CpuTimeV2
    // test above (60/40/20), unaffected by B's call in between.
    cGroupA.CpuTimeV2(stats2);
    messages = memoryWriter->GetMessages();
    EXPECT_EQ(messages.size(), 3);
    EXPECT_EQ(messages.at(0), "c:cgroup.cpu.processingTime:60.000000\n");
    EXPECT_EQ(messages.at(1), "c:cgroup.cpu.usageTime,id=system:40.000000\n");
    EXPECT_EQ(messages.at(2), "c:cgroup.cpu.usageTime,id=user:20.000000\n");
    memoryWriter->Clear();

    // Step 4: B's second call against its own sample2 baseline -- delta is zero by design.
    // spectator::Counter::Increment() (counter.h) writes only when delta > 0, so a genuinely zero
    // delta on all three counters means no messages at all, not three zero-valued ones.
    cGroupB.CpuTimeV2(stats2);
    messages = memoryWriter->GetMessages();
    EXPECT_EQ(messages.size(), 0);
}

// The "max" -> 0 parsing pitfall QuotaCpuCount() exists to avoid: sample1/sample2's cpu.max
// ("max 100000") is the unlimited case and must return kUnlimited, not a limited quota of 0.
TEST(CGroup, QuotaCpuCountUnlimitedReturnsUnlimited)
{
    auto config = Config(WriterConfig(WriterTypes::Memory));
    Registry registry(config);
    CGroupTest cGroup{&registry, "lib/collectors/cgroup/test/resources/sample1"};

    EXPECT_EQ(cGroup.QuotaCpuCount().state, atlasagent::CpuQuotaState::kUnlimited);

    cGroup.SetPrefix("lib/collectors/cgroup/test/resources/sample2");
    EXPECT_EQ(cGroup.QuotaCpuCount().state, atlasagent::CpuQuotaState::kUnlimited);
}

// Pins down the numeric-quota branch (quota/period), which had no fixture or test in the repo
// before this one.
TEST(CGroup, QuotaCpuCountNumericQuotaReturnsQuotaOverPeriod)
{
    auto config = Config(WriterConfig(WriterTypes::Memory));
    Registry registry(config);
    CGroupTest cGroup{&registry, "lib/collectors/cgroup/test/resources/sample_cpu_quota"};

    auto result = cGroup.QuotaCpuCount();
    EXPECT_EQ(result.state, atlasagent::CpuQuotaState::kLimited);
    EXPECT_DOUBLE_EQ(result.cores, 0.5);
}

TEST(CGroup, QuotaCpuCountMissingFileReturnsUnreadable)
{
    auto config = Config(WriterConfig(WriterTypes::Memory));
    Registry registry(config);
    CGroupTest cGroup{&registry, "lib/collectors/cgroup/test/resources/does_not_exist"};

    EXPECT_EQ(cGroup.QuotaCpuCount().state, atlasagent::CpuQuotaState::kUnreadable);
}

TEST(CGroup, QuotaCpuCountMalformedQuotaReturnsUnreadable)
{
    auto config = Config(WriterConfig(WriterTypes::Memory));
    Registry registry(config);
    CGroupTest cGroup{&registry, "lib/collectors/cgroup/test/resources/sample_cpu_quota_malformed"};

    EXPECT_EQ(cGroup.QuotaCpuCount().state, atlasagent::CpuQuotaState::kUnreadable);
}

TEST(CGroup, QuotaCpuCountPartialFileReturnsUnreadable)
{
    auto config = Config(WriterConfig(WriterTypes::Memory));
    Registry registry(config);
    CGroupTest cGroup{&registry, "lib/collectors/cgroup/test/resources/sample_cpu_quota_partial"};

    EXPECT_EQ(cGroup.QuotaCpuCount().state, atlasagent::CpuQuotaState::kUnreadable);
}

TEST(CGroup, QuotaCpuCountNonPositivePeriodReturnsUnreadable)
{
    auto config = Config(WriterConfig(WriterTypes::Memory));
    Registry registry(config);
    CGroupTest cGroup{&registry, "lib/collectors/cgroup/test/resources/sample_cpu_quota_nonpositive"};

    EXPECT_EQ(cGroup.QuotaCpuCount().state, atlasagent::CpuQuotaState::kUnreadable);
}

TEST(CGroup, CpuWeight)
{
    auto config = Config(WriterConfig(WriterTypes::Memory));
    Registry registry(config);
    CGroupTest cGroup{&registry, "lib/collectors/cgroup/test/resources/sample1"};

    cGroup.CpuWeight();

    auto memoryWriter = static_cast<MemoryWriter*>(WriterTestHelper::GetImpl());
    auto messages = memoryWriter->GetMessages();
    EXPECT_EQ(messages.size(), 1);
    EXPECT_EQ(messages.at(0), "g:cgroup.cpu.weight:100.000000\n");
}

TEST(CGroup, SetExtraTagsMergesWithLocalTags)
{
    auto config = Config(WriterConfig(WriterTypes::Memory));
    Registry registry(config);
    CGroupTest cGroup{&registry, "lib/collectors/cgroup/test/resources/sample1"};

    // "id" deliberately collides with CpuTimeV2's own "id" tag on the usageTime series, so this
    // also exercises local_tags winning on collision; "pod" never collides.
    cGroup.SetExtraTags({{"pod", "my-pod"}, {"id", "OVERRIDE_ME"}});

    std::unordered_map<std::string, int64_t> stats;
    atlasagent::parse_kv_from_file(cGroup.path_prefix_, "cpu.stat", &stats);
    cGroup.CpuTimeV2(stats);  // first call: no prev reading yet, nothing emitted

    cGroup.SetPrefix("lib/collectors/cgroup/test/resources/sample2");
    atlasagent::parse_kv_from_file(cGroup.path_prefix_, "cpu.stat", &stats);
    cGroup.CpuTimeV2(stats);

    auto memoryWriter = static_cast<MemoryWriter*>(WriterTestHelper::GetImpl());
    auto messages = memoryWriter->GetMessages();
    ASSERT_EQ(messages.size(), 3);

    // processingTime has no local tags, so extra_tags_ passes through untouched. Not asserting the
    // full line on purpose -- multi-tag order follows hash-bucket iteration, not insertion order.
    EXPECT_NE(messages.at(0).find("cgroup.cpu.processingTime"), std::string::npos);
    EXPECT_NE(messages.at(0).find("pod=my-pod"), std::string::npos);
    EXPECT_NE(messages.at(0).find("id=OVERRIDE_ME"), std::string::npos);

    // usageTime,id=system: local "id=system" must win over extra_tags_'s "id=OVERRIDE_ME", while
    // "pod" (no collision) still comes through.
    EXPECT_NE(messages.at(1).find("cgroup.cpu.usageTime"), std::string::npos);
    EXPECT_NE(messages.at(1).find("pod=my-pod"), std::string::npos);
    EXPECT_NE(messages.at(1).find("id=system"), std::string::npos);
    EXPECT_EQ(messages.at(1).find("OVERRIDE_ME"), std::string::npos);

    // usageTime,id=user: same, but for the "user" series.
    EXPECT_NE(messages.at(2).find("cgroup.cpu.usageTime"), std::string::npos);
    EXPECT_NE(messages.at(2).find("pod=my-pod"), std::string::npos);
    EXPECT_NE(messages.at(2).find("id=user"), std::string::npos);
    EXPECT_EQ(messages.at(2).find("OVERRIDE_ME"), std::string::npos);
}

// PodCpuStats() delegates straight to CpuStats() (see cgroup.cpp for why), so a pod's sys.cpu.* /
// titus.cpu.* lines have the same shape Titus already emits per-container, disambiguated per pod by
// SetExtraTags()/MergeTags(). These tests pin down that sys.cpu.utilization / sys.cpu.numProcessors
// / titus.cpu.requested DO appear (gated by sixtySecondMetricsEnabled, via CpuUtilizationV2), that
// sys.cpu.peakUtilization appears on every call regardless of either flag (CpuStats() calls
// CpuPeakUtilizationV2 unconditionally), and that the lines carry the pod's tag -- replacing the old
// (incorrect) assertions that these series could never appear from PodCpuStats().

TEST(CGroup, PodCpuStatsBothCadencesEnabled)
{
    auto config = Config(WriterConfig(WriterTypes::Memory));
    Registry registry(config);
    CGroupTest cGroup{&registry, "lib/collectors/cgroup/test/resources/sample1"};
    setenv("TITUS_NUM_CPU", "1", 1);
    cGroup.SetExtraTags({{"nf.node", "test-pod"}});

    cGroup.PodCpuStats(/*fiveSecondMetricsEnabled=*/true, /*sixtySecondMetricsEnabled=*/true);

    auto memoryWriter = static_cast<MemoryWriter*>(WriterTestHelper::GetImpl());
    auto messages = memoryWriter->GetMessages();
    // First-ever call: CpuThrottleV2's numThrottled (1) + CpuUtilizationV2's CpuWeight /
    // sys.cpu.numProcessors / titus.cpu.requested (3) + CpuProcessingCapacity's counter (1) = 5.
    // sys.cpu.utilization, CpuTimeV2 and the unconditional CpuPeakUtilizationV2 all need a prior
    // reading, so none of them fire yet.
    EXPECT_EQ(messages.size(), 5);

    // numProcessors / titus.cpu.requested are present on this very first call (unlike delta-based
    // sys.cpu.utilization) and already carry the pod's tag.
    int otherMetricCount = 0;  // the messages this loop does not assert on
    for (const auto& msg : messages)
    {
        if (msg.find("sys.cpu.numProcessors") != std::string::npos || msg.find("titus.cpu.requested") != std::string::npos)
        {
            EXPECT_NE(msg.find("nf.node=test-pod"), std::string::npos);
        }
        else
        {
            ++otherMetricCount;
        }
    }
    EXPECT_EQ(otherMetricCount, 3);  // numThrottled, weight, processingCapacity
    memoryWriter->Clear();

    // Second call, against a different sample so the delta-tracked sub-metrics fire too.
    cGroup.SetPrefix("lib/collectors/cgroup/test/resources/sample2");
    cGroup.PodCpuStats(true, true);
    messages = memoryWriter->GetMessages();
    // CpuThrottleV2 (2) + CpuUtilizationV2's weight/numProcessors/titus.requested/utilization
    // system+user (5) + CpuTimeV2 (3) + CpuProcessingCapacity (1) + CpuPeakUtilizationV2 (2, now
    // that it has a prior reading) = 13.
    EXPECT_EQ(messages.size(), 13);

    auto countContaining = [&messages](const std::string& needle)
    {
        int count = 0;
        for (const auto& msg : messages)
        {
            if (msg.find(needle) != std::string::npos) ++count;
        }
        return count;
    };

    // The metrics PodCpuStats() was previously asserted never to emit, now present per pod.
    EXPECT_EQ(countContaining("sys.cpu.utilization"), 2);      // id=system, id=user
    EXPECT_EQ(countContaining("sys.cpu.numProcessors"), 1);
    EXPECT_EQ(countContaining("titus.cpu.requested"), 1);
    EXPECT_EQ(countContaining("sys.cpu.peakUtilization"), 2);  // id=system, id=user

    // And every sys.cpu.* / titus.cpu.* line carries the pod's disambiguating tag.
    for (const auto& msg : messages)
    {
        if (msg.find("sys.cpu") != std::string::npos || msg.find("titus.cpu") != std::string::npos)
        {
            EXPECT_NE(msg.find("nf.node=test-pod"), std::string::npos);
        }
    }
}

TEST(CGroup, PodCpuStatsOnlySixtySecondCadence)
{
    auto config = Config(WriterConfig(WriterTypes::Memory));
    Registry registry(config);
    CGroupTest cGroup{&registry, "lib/collectors/cgroup/test/resources/sample1"};
    setenv("TITUS_NUM_CPU", "1", 1);
    cGroup.SetExtraTags({{"nf.node", "test-pod"}});

    cGroup.PodCpuStats(false, true);
    auto memoryWriter = static_cast<MemoryWriter*>(WriterTestHelper::GetImpl());
    auto messages = memoryWriter->GetMessages();
    // CpuThrottleV2's numThrottled (1) + CpuUtilizationV2's weight/numProcessors/titus.requested
    // (3); CpuTimeV2/CpuProcessingCapacity never run (fiveSecondMetricsEnabled=false); the
    // always-called CpuPeakUtilizationV2 emits nothing on its first-ever call -- 4 total.
    EXPECT_EQ(messages.size(), 4);
    memoryWriter->Clear();

    cGroup.SetPrefix("lib/collectors/cgroup/test/resources/sample2");
    cGroup.PodCpuStats(false, true);
    messages = memoryWriter->GetMessages();
    // CpuThrottleV2 (2) + CpuUtilizationV2's weight/numProcessors/titus.requested/utilization
    // system+user (5) + CpuPeakUtilizationV2's system+user (2, now it has a prior reading) -- 9.
    EXPECT_EQ(messages.size(), 9);

    auto countContaining = [&messages](const std::string& needle)
    {
        int count = 0;
        for (const auto& msg : messages)
        {
            if (msg.find(needle) != std::string::npos) ++count;
        }
        return count;
    };

    // The 60-second-cadence sys.cpu.*/titus.cpu.* group is present...
    EXPECT_EQ(countContaining("sys.cpu.numProcessors"), 1);
    EXPECT_EQ(countContaining("titus.cpu.requested"), 1);
    EXPECT_EQ(countContaining("sys.cpu.utilization"), 2);
    // ...and so is the unconditional peak metric...
    EXPECT_EQ(countContaining("sys.cpu.peakUtilization"), 2);
    // ...while the 5-second-cadence group is correctly still absent.
    for (const auto& msg : messages)
    {
        EXPECT_EQ(msg.find("cgroup.cpu.processingTime"), std::string::npos);
        EXPECT_EQ(msg.find("cgroup.cpu.processingCapacity"), std::string::npos);
        EXPECT_EQ(msg.find("cgroup.cpu.usageTime"), std::string::npos);
    }

    bool sawTaggedSysCpuLine = false;
    for (const auto& msg : messages)
    {
        if (msg.find("sys.cpu") != std::string::npos && msg.find("nf.node=test-pod") != std::string::npos)
        {
            sawTaggedSysCpuLine = true;
        }
    }
    EXPECT_TRUE(sawTaggedSysCpuLine);
}

TEST(CGroup, PodCpuStatsOnlyFiveSecondCadence)
{
    auto config = Config(WriterConfig(WriterTypes::Memory));
    Registry registry(config);
    CGroupTest cGroup{&registry, "lib/collectors/cgroup/test/resources/sample1"};
    setenv("TITUS_NUM_CPU", "1", 1);
    cGroup.SetExtraTags({{"nf.node", "test-pod"}});

    cGroup.PodCpuStats(true, false);
    auto memoryWriter = static_cast<MemoryWriter*>(WriterTestHelper::GetImpl());
    auto messages = memoryWriter->GetMessages();
    // sixtySecondMetricsEnabled=false skips CpuThrottleV2 and all of CpuUtilizationV2 (so no
    // numProcessors/titus.cpu.requested/sys.cpu.utilization/cgroup.cpu.weight at all); CpuTimeV2
    // and the always-called CpuPeakUtilizationV2 have no prior reading yet, leaving only
    // CpuProcessingCapacity's counter -- 1 total.
    EXPECT_EQ(messages.size(), 1);
    memoryWriter->Clear();

    cGroup.SetPrefix("lib/collectors/cgroup/test/resources/sample2");
    cGroup.PodCpuStats(true, false);
    messages = memoryWriter->GetMessages();
    // CpuTimeV2's three usage/processing deltas + CpuProcessingCapacity's one +
    // CpuPeakUtilizationV2's system+user (now it has a prior reading) -- 6 total.
    EXPECT_EQ(messages.size(), 6);

    // The metrics gated behind sixtySecondMetricsEnabled are correctly still absent...
    for (const auto& msg : messages)
    {
        EXPECT_EQ(msg.find("sys.cpu.numProcessors"), std::string::npos);
        EXPECT_EQ(msg.find("titus.cpu.requested"), std::string::npos);
        EXPECT_EQ(msg.find("sys.cpu.utilization"), std::string::npos);
        EXPECT_EQ(msg.find("cgroup.cpu.weight"), std::string::npos);
        EXPECT_EQ(msg.find("cgroup.cpu.throttledTime"), std::string::npos);
        EXPECT_EQ(msg.find("cgroup.cpu.numThrottled"), std::string::npos);
    }

    // ...but sys.cpu.peakUtilization is still present and tagged, because CpuPeakUtilizationV2
    // is called outside of any cadence gate -- the key behavior this test pins down.
    int peakCount = 0;
    bool sawTaggedPeakLine = false;
    for (const auto& msg : messages)
    {
        if (msg.find("sys.cpu.peakUtilization") != std::string::npos)
        {
            ++peakCount;
            if (msg.find("nf.node=test-pod") != std::string::npos) sawTaggedPeakLine = true;
        }
    }
    EXPECT_EQ(peakCount, 2);
    EXPECT_TRUE(sawTaggedPeakLine);
}

TEST(CGroup, PodCpuStatsNeitherCadenceEnabled)
{
    auto config = Config(WriterConfig(WriterTypes::Memory));
    Registry registry(config);
    CGroupTest cGroup{&registry, "lib/collectors/cgroup/test/resources/sample1"};
    setenv("TITUS_NUM_CPU", "1", 1);
    cGroup.SetExtraTags({{"nf.node", "test-pod"}});

    cGroup.PodCpuStats(false, false);
    auto memoryWriter = static_cast<MemoryWriter*>(WriterTestHelper::GetImpl());
    auto messages = memoryWriter->GetMessages();
    // Every cadence-gated sub-metric is skipped, and the always-called CpuPeakUtilizationV2
    // emits nothing on its first-ever call -- 0 total.
    EXPECT_EQ(messages.size(), 0);

    cGroup.SetPrefix("lib/collectors/cgroup/test/resources/sample2");
    cGroup.PodCpuStats(false, false);
    messages = memoryWriter->GetMessages();
    // The crux of the fix: CpuPeakUtilizationV2 runs even with BOTH cadence flags off, and now has
    // a prior reading to diff against -- sys.cpu.peakUtilization fires on every single call.
    EXPECT_EQ(messages.size(), 2);
    EXPECT_NE(messages.at(0).find("sys.cpu.peakUtilization"), std::string::npos);
    EXPECT_NE(messages.at(0).find("nf.node=test-pod"), std::string::npos);
    EXPECT_NE(messages.at(1).find("sys.cpu.peakUtilization"), std::string::npos);
    EXPECT_NE(messages.at(1).find("nf.node=test-pod"), std::string::npos);

    // No other cadence-gated or 60s-only metric leaks in.
    for (const auto& msg : messages)
    {
        EXPECT_EQ(msg.find("sys.cpu.numProcessors"), std::string::npos);
        EXPECT_EQ(msg.find("titus.cpu.requested"), std::string::npos);
        EXPECT_EQ(msg.find("sys.cpu.utilization"), std::string::npos);
        EXPECT_EQ(msg.find("cgroup.cpu"), std::string::npos);
    }
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

// Regression test for MemoryStatsStdV2()'s tag-parity bug: all 8 of its CreateGauge call sites used
// to pass no tag argument at all (not even MergeTags({})), even though it reads genuinely
// per-cgroup values under path_prefix_ (memory.max/current/swap.max/swap.current/memory.stat) just
// as MemoryStatsV2() does above it. For a pod-scoped caller (PodMonitor) that meant every tracked
// pod's mem.* series colliding under identical untagged names. Substring checks only: the tag is
// the point here, not the numbers (ParseMemoryV2 above already pins those).
TEST(CGroup, MemoryStatsStdV2EmitsWithPodTags)
{
    auto config = Config(WriterConfig(WriterTypes::Memory));
    Registry registry(config);
    CGroupTest cGroup{&registry, "lib/collectors/cgroup/test/resources/sample1"};
    cGroup.SetExtraTags({{"nf.node", "test-pod"}});

    cGroup.MemoryStatsStdV2();

    auto memoryWriter = static_cast<MemoryWriter*>(WriterTestHelper::GetImpl());
    auto messages = memoryWriter->GetMessages();
    ASSERT_EQ(messages.size(), 8);

    for (const auto& msg : messages)
    {
        EXPECT_NE(msg.find("nf.node=test-pod"), std::string::npos) << "message missing pod tag: " << msg;
    }
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
            {prefix + ".non_numeric", false, "non-numeric values"}};
}

TEST(CGroup, InvalidIOStats)
{
    auto testCases = GetCommonInvalidTestCases("io.stat");
    // Add io.stat specific test cases
    testCases.push_back({"io.stat.negative_values", false, "negative values"});
    // Extra fields (e.g. from io.cost) are now tolerated, so too_many_fields is valid for io.stat
    testCases.push_back({"io.stat.too_many_fields", true, "extra extension fields are tolerated"});

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
    // Add io.max specific test cases
    testCases.push_back({"io.max.negative_values", false, "negative values"});
    testCases.push_back({"io.max.too_many_fields", false, "lines with too many fields"});

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

TEST(CGroup, IOStatWithCostFields)
{
    std::unordered_map<std::string, std::string> deviceMap = {{"259:0", "nvme0n1"}};
    auto lines = atlasagent::read_lines_fields("lib/collectors/cgroup/test/resources/sample_io_cost/", "io.stat");
    auto result = atlasagent::ParseIOLines(lines, deviceMap);

    ASSERT_FALSE(result.empty()) << "io.stat with cost fields should parse successfully";
    ASSERT_NE(result.find("259:0"), result.end()) << "device 259:0 should be present";

    const auto& entry = result.at("259:0");
    ASSERT_TRUE(entry.rBytes.has_value());
    EXPECT_EQ(*entry.rBytes, 40016384.0);
    ASSERT_TRUE(entry.wBytes.has_value());
    EXPECT_EQ(*entry.wBytes, 3842195456.0);
    ASSERT_TRUE(entry.rOperations.has_value());
    EXPECT_EQ(*entry.rOperations, 1074.0);
    ASSERT_TRUE(entry.wOperations.has_value());
    EXPECT_EQ(*entry.wOperations, 34821.0);
    ASSERT_TRUE(entry.dBytes.has_value());
    EXPECT_EQ(*entry.dBytes, 0.0);
    ASSERT_TRUE(entry.dOperations.has_value());
    EXPECT_EQ(*entry.dOperations, 0.0);
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
