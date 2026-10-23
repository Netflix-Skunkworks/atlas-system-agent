#include <lib/collectors/nvml/src/utilization_samples.h>
#include <gtest/gtest.h>

using namespace atlasagent;

namespace
{
NvmlSample Sample(unsigned long long timestamp, unsigned int utilization)
{
    NvmlSample sample{};
    sample.timeStamp = timestamp;
    sample.sampleValue.uiVal = utilization;
    return sample;
}

TEST(GpuUtilizationWindow, StartupHistoryIsExcluded)
{
    GpuUtilizationWindow window;
    window.Add({Sample(10, 100), Sample(20, 100)});
    EXPECT_EQ(20u, window.last_seen());
    EXPECT_FALSE(window.Publish());
    window.Add({Sample(30, 25), Sample(40, 75)});
    EXPECT_DOUBLE_EQ(50.0, *window.Publish());
}

TEST(GpuUtilizationWindow, SampleWeightedAcrossUnequalPollSizes)
{
    GpuUtilizationWindow window;
    window.Add({Sample(1, 0)});
    window.Add({Sample(2, 100)});
    window.Add({Sample(3, 0), Sample(4, 0), Sample(5, 0)});
    EXPECT_DOUBLE_EQ(25.0, *window.Publish());  // not (100 + 0) / 2
}

TEST(GpuUtilizationWindow, PublishResetsTotalsButPreservesCursor)
{
    GpuUtilizationWindow window;
    window.Add({Sample(1, 100)});
    window.Add({Sample(2, 20)});
    EXPECT_DOUBLE_EQ(20.0, *window.Publish());
    EXPECT_EQ(2u, window.last_seen());
    EXPECT_FALSE(window.Publish());
    window.Add({Sample(3, 80)});
    EXPECT_DOUBLE_EQ(80.0, *window.Publish());
}

TEST(GpuUtilizationWindow, EmptyPollDoesNotInsertZeroOrRepeatOldValue)
{
    GpuUtilizationWindow window;
    window.Add({});
    EXPECT_FALSE(window.Publish());
    window.Add({Sample(1, 60)});
    window.Add({});
    EXPECT_DOUBLE_EQ(60.0, *window.Publish());
    EXPECT_FALSE(window.Publish());
}

TEST(GpuUtilizationWindow, ZeroIsARealSample)
{
    GpuUtilizationWindow window;
    window.Add({Sample(1, 100)});
    window.Add({Sample(2, 0)});
    auto average = window.Publish();
    ASSERT_TRUE(average);
    EXPECT_DOUBLE_EQ(0.0, *average);
}

TEST(GpuUtilizationWindow, ExclusiveCursorAndUnsortedSamples)
{
    GpuUtilizationWindow window;
    window.Add({Sample(10, 0)});
    window.Add({Sample(30, 100), Sample(10, 0), Sample(20, 50), Sample(5, 0)});
    EXPECT_EQ(30u, window.last_seen());
    EXPECT_DOUBLE_EQ(75.0, *window.Publish());
    window.Add({Sample(20, 50), Sample(30, 100)});
    EXPECT_FALSE(window.Publish());
}

TEST(GpuUtilizationWindow, DeviceWindowsAreIndependent)
{
    GpuUtilizationWindow first;
    GpuUtilizationWindow second;
    first.Add({Sample(10, 0)});
    second.Add({Sample(100, 0)});
    first.Add({Sample(20, 25)});
    second.Add({Sample(200, 75)});
    EXPECT_DOUBLE_EQ(25.0, *first.Publish());
    EXPECT_DOUBLE_EQ(75.0, *second.Publish());
    EXPECT_EQ(20u, first.last_seen());
    EXPECT_EQ(200u, second.last_seen());
}

struct FakeSamples
{
    inline static int queries;
    inline static int reads;
    inline static NvmlRet result;
    inline static bool grows;
    inline static bool always_grows;
    inline static bool wrong_type;
    inline static bool empty;

    static void Reset()
    {
        queries = reads = 0;
        result = NvmlRet::Success;
        grows = always_grows = wrong_type = empty = false;
    }

    static NvmlRet Get(NvmlDeviceHandle device, NvmlSamplingType type, unsigned long long last_seen,
                       NvmlValueType* value_type, unsigned int* count, NvmlSample* samples)
    {
        EXPECT_EQ(reinterpret_cast<NvmlDeviceHandle>(123), device);
        EXPECT_EQ(NvmlSamplingType::GpuUtilization, type);
        EXPECT_EQ(42u, last_seen);
        *value_type = wrong_type ? static_cast<NvmlValueType>(0) : NvmlValueType::UnsignedInt;
        if (samples == nullptr)
        {
            ++queries;
            *count = empty ? 0 : 2;
            return result;
        }
        ++reads;
        EXPECT_GE(*count, 3u);
        if (always_grows)
        {
            ++*count;
            return NvmlRet::ErrorInsufficientSize;
        }
        samples[0] = Sample(43, 20);
        samples[1] = Sample(44, 80);
        if (grows)
        {
            samples[2] = Sample(45, 50);
        }
        *count = grows ? 3 : 2;
        return result;
    }
};

class GpuSampleRead : public testing::Test
{
   protected:
    void SetUp() override { FakeSamples::Reset(); }
    NvmlRet Read()
    {
        return ReadGpuUtilizationSamples(&FakeSamples::Get, reinterpret_cast<NvmlDeviceHandle>(123), 42, &samples);
    }
    std::vector<NvmlSample> samples{Sample(0, 0)};
};

TEST_F(GpuSampleRead, ReadsTypedSamplesWithExclusiveTimestamp)
{
    EXPECT_EQ(NvmlRet::Success, Read());
    ASSERT_EQ(2u, samples.size());
    EXPECT_EQ(43u, samples[0].timeStamp);
    EXPECT_EQ(80u, samples[1].sampleValue.uiVal);
}

TEST_F(GpuSampleRead, NoNewSamplesIsNotAnError)
{
    FakeSamples::result = NvmlRet::ErrorNotFound;
    EXPECT_EQ(NvmlRet::Success, Read());
    EXPECT_TRUE(samples.empty());
    EXPECT_EQ(0, FakeSamples::reads);
}

TEST_F(GpuSampleRead, EmptyRing)
{
    FakeSamples::empty = true;
    EXPECT_EQ(NvmlRet::Success, Read());
    EXPECT_TRUE(samples.empty());
}

TEST_F(GpuSampleRead, MissingSymbol)
{
    EXPECT_EQ(NvmlRet::ErrorFunctionNotFound, ReadGpuUtilizationSamples(nullptr, nullptr, 0, &samples));
    EXPECT_TRUE(samples.empty());
}

TEST_F(GpuSampleRead, UnsupportedAndTransientErrors)
{
    for (auto error : {NvmlRet::ErrorNotSupported, NvmlRet::ErrorGpuIsLost, NvmlRet::ErrorUnknown})
    {
        FakeSamples::result = error;
        EXPECT_EQ(error, Read());
        EXPECT_TRUE(samples.empty());
    }
}

TEST_F(GpuSampleRead, ReadsSamplesAddedAfterSizeQuery)
{
    FakeSamples::grows = true;
    EXPECT_EQ(NvmlRet::Success, Read());
    EXPECT_EQ(1, FakeSamples::queries);
    EXPECT_EQ(1, FakeSamples::reads);
    ASSERT_EQ(3u, samples.size());
    EXPECT_EQ(45u, samples[2].timeStamp);
}

TEST_F(GpuSampleRead, StillTooSmallReturnsErrorForNextPoll)
{
    FakeSamples::always_grows = true;
    EXPECT_EQ(NvmlRet::ErrorInsufficientSize, Read());
    EXPECT_EQ(1, FakeSamples::queries);
    EXPECT_EQ(1, FakeSamples::reads);
    EXPECT_TRUE(samples.empty());
}

TEST_F(GpuSampleRead, RejectsUnexpectedValueType)
{
    FakeSamples::wrong_type = true;
    EXPECT_EQ(NvmlRet::ErrorInvalidArgument, Read());
    EXPECT_TRUE(samples.empty());
}
}  // namespace
