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

// Regression test for the CGroup refactor that turned CpuThrottleV2/CpuTimeV2/etc.'s hidden
// `static` delta-tracking locals into per-instance members. Two independent CGroup instances
// must track their own prev_* baselines without clobbering each other's -- which is exactly
// what the old shared statics did not guarantee. This test fails against the pre-refactor code
// (shared statics): step 2 below would spuriously emit a 60/40/20s delta instead of nothing
// (because instance A's call in step 1 already primed the shared static), and step 3 would
// compute a zero delta instead of the expected 60/40/20 (because instance B's call in step 2
// would have clobbered the shared static baseline with sample2's own values).
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

    // Step 2: instance B's first-ever call (baseline from sample2). B must also see no delta,
    // proving B's prev_* state starts independent of whatever A just recorded.
    cGroupB.CpuTimeV2(stats2);
    messages = memoryWriter->GetMessages();
    EXPECT_EQ(messages.size(), 0);
    memoryWriter->Clear();

    // Step 3: instance A's second call must reproduce the exact known-good delta from the
    // single-instance CpuTimeV2 test above (60/40/20), unaffected by B's call in between.
    cGroupA.CpuTimeV2(stats2);
    messages = memoryWriter->GetMessages();
    EXPECT_EQ(messages.size(), 3);
    EXPECT_EQ(messages.at(0), "c:cgroup.cpu.processingTime:60.000000\n");
    EXPECT_EQ(messages.at(1), "c:cgroup.cpu.usageTime,id=system:40.000000\n");
    EXPECT_EQ(messages.at(2), "c:cgroup.cpu.usageTime,id=user:20.000000\n");
    memoryWriter->Clear();

    // Step 4: instance B's second call, still against its own sample2 baseline -- delta is
    // zero by design, but the point is that it is unaffected by A's calls in between.
    cGroupB.CpuTimeV2(stats2);
    messages = memoryWriter->GetMessages();
    EXPECT_EQ(messages.size(), 3);
    EXPECT_EQ(messages.at(0), "c:cgroup.cpu.processingTime:0.000000\n");
    EXPECT_EQ(messages.at(1), "c:cgroup.cpu.usageTime,id=system:0.000000\n");
    EXPECT_EQ(messages.at(2), "c:cgroup.cpu.usageTime,id=user:0.000000\n");
}

// Regression coverage for the exact "max" -> 0 parsing pitfall QuotaCpuCount() is designed to
// avoid: sample1/sample2's cpu.max ("max 100000") is the unlimited-quota case, which must come
// back as nullopt rather than silently being parsed as a quota of 0 (which read_num_vector_from_
// file()'s strtoul-based parsing would do, since strtoul("max", ...) == 0).
TEST(CGroup, QuotaCpuCountUnlimitedReturnsNullopt)
{
    auto config = Config(WriterConfig(WriterTypes::Memory));
    Registry registry(config);
    CGroupTest cGroup{&registry, "lib/collectors/cgroup/test/resources/sample1"};

    EXPECT_FALSE(cGroup.QuotaCpuCount().has_value());

    cGroup.SetPrefix("lib/collectors/cgroup/test/resources/sample2");
    EXPECT_FALSE(cGroup.QuotaCpuCount().has_value());
}

// Pins down the numeric-quota branch (quota/period), which -- unlike the unlimited case above --
// had no fixture or test anywhere in the repo prior to this test.
TEST(CGroup, QuotaCpuCountNumericQuotaReturnsQuotaOverPeriod)
{
    auto config = Config(WriterConfig(WriterTypes::Memory));
    Registry registry(config);
    CGroupTest cGroup{&registry, "lib/collectors/cgroup/test/resources/sample_cpu_quota"};

    auto result = cGroup.QuotaCpuCount();
    ASSERT_TRUE(result.has_value());
    EXPECT_DOUBLE_EQ(*result, 0.5);
}

TEST(CGroup, QuotaCpuCountMissingFileReturnsNullopt)
{
    auto config = Config(WriterConfig(WriterTypes::Memory));
    Registry registry(config);
    CGroupTest cGroup{&registry, "lib/collectors/cgroup/test/resources/does_not_exist"};

    EXPECT_FALSE(cGroup.QuotaCpuCount().has_value());
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

    // processingTime has no local tags at all, so extra_tags_ passes through untouched. Note:
    // we deliberately do NOT assert a full line here, since the relative order multiple tags
    // serialize in is driven by hash-bucket iteration, not insertion order.
    EXPECT_NE(messages.at(0).find("cgroup.cpu.processingTime"), std::string::npos);
    EXPECT_NE(messages.at(0).find("pod=my-pod"), std::string::npos);
    EXPECT_NE(messages.at(0).find("id=OVERRIDE_ME"), std::string::npos);

    // usageTime,id=system: local "id=system" must win over extra_tags_'s "id=OVERRIDE_ME",
    // while "pod" (no local collision) still comes through from extra_tags_.
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

// PodCpuStats() now delegates straight to CpuStats() (see cgroup.cpp), so a pod's sys.cpu.* /
// titus.cpu.* output is byte-for-byte the same shape Titus already emits per-container -- the
// only difference is that a pod's CGroup instance has SetExtraTags() applied, so MergeTags()
// disambiguates which pod each line belongs to. These tests pin down that sys.cpu.utilization /
// sys.cpu.numProcessors / titus.cpu.requested DO appear (gated by sixtySecondMetricsEnabled,
// since they come from CpuUtilizationV2), that sys.cpu.peakUtilization DOES appear on every call
// regardless of either cadence flag (CpuStats() calls CpuPeakUtilizationV2 unconditionally), and
// that the emitted lines carry the pod's tag -- replacing the old (incorrect) assertions that
// these series could never appear from PodCpuStats().

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
    // First-ever call: CpuThrottleV2 emits only numThrottled (1); CpuUtilizationV2 emits
    // CpuWeight + sys.cpu.numProcessors + titus.cpu.requested (3) -- sys.cpu.utilization itself
    // needs a prior reading, so it doesn't fire yet; CpuTimeV2 emits nothing yet (1st call);
    // CpuProcessingCapacity always emits one counter (1); CpuPeakUtilizationV2 (unconditional)
    // emits nothing on its first-ever call -- 5 total.
    EXPECT_EQ(messages.size(), 5);

    // sys.cpu.numProcessors / titus.cpu.requested are already present on the very first call
    // (unlike the delta-based sys.cpu.utilization), and already carry the pod's tag.
    int otherMetricCount = 0;  // messages not asserted on in this loop (numThrottled/weight/processingCapacity)
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
    // system+user (5) + CpuTimeV2's three deltas (3) + CpuProcessingCapacity (1) +
    // CpuPeakUtilizationV2's system+user (2, now that it has a prior reading) -- 13 total.
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

    // The node/Titus-scoped metrics PodCpuStats() used to never emit are now present, per pod,
    // exactly as Titus already emits them per-container via CpuStats().
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
    // sixtySecondMetricsEnabled=false skips CpuThrottleV2 AND all of CpuUtilizationV2 (so no
    // sys.cpu.numProcessors/titus.cpu.requested/sys.cpu.utilization/cgroup.cpu.weight at all).
    // CpuTimeV2 emits nothing on its first call; CpuProcessingCapacity always emits one counter;
    // the always-called CpuPeakUtilizationV2 emits nothing on its first-ever call -- 1 total.
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
    // Even with BOTH cadence flags off, CpuPeakUtilizationV2 still runs unconditionally, and now
    // has a prior reading to diff against -- this is the crux of the fix: sys.cpu.peakUtilization
    // is not gated behind either cadence flag, it fires on every single call.
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

// Regression test for MemoryStatsStdV2()'s tag-parity bug: all 8 of its CreateGauge call sites
// used to pass zero tag arguments at all (not even MergeTags({})), even though it reads
// genuinely per-cgroup values (memory.max/memory.current/memory.swap.max/memory.swap.current/
// memory.stat, all under path_prefix_) exactly like MemoryStatsV2() does just above it. That
// meant a pod-scoped caller (PodMonitor) calling this today would have every tracked pod's
// mem.* series collide under identical untagged names. This test pins down the fix: once
// SetExtraTags() has given this CGroup instance a disambiguating tag, every one of the 8
// emitted mem.* messages carries it -- mirroring the substring-check style of
// SetExtraTagsMergesWithLocalTags/PodCpuStatsBothCadencesEnabled above, since (as in those
// tests) the exact numeric values aren't the point here.
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