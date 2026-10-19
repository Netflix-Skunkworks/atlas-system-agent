#include <lib/logger/src/logger.h>
#include <lib/collectors/nvml/src/gpumetrics.h>

#include <thirdparty/spectator-cpp/spectator/registry.h>
#include <thirdparty/spectator-cpp/libs/writer/writer_wrapper/writer_test_helper.h>
#include <gtest/gtest.h>

using atlasagent::NvmlDeviceHandle;
using atlasagent::NvmlMemory;
using atlasagent::NvmlPerfState;
using atlasagent::NvmlUtilization;
using spectator::Config;
using spectator::MemoryWriter;
using spectator::Registry;
using spectator::WriterConfig;
using spectator::WriterTestHelper;
using spectator::WriterTypes;

namespace
{

class TestNvml
{
   public:
    bool get_count(unsigned int* count) noexcept
    {
        *count = device_count;
        return true;
    }

    bool get_by_index(unsigned int index, NvmlDeviceHandle* device) noexcept
    {
        *device = reinterpret_cast<NvmlDeviceHandle>(index + handle_offset);
        return true;
    }

    bool get_memory_info(NvmlDeviceHandle device, NvmlMemory* memory) noexcept
    {
        if (device)
        {
            memory->free = 1000;
            memory->used = 2000;
            memory->total = 3000;
        }
        else
        {
            memory->free = 100;
            memory->used = 2900;
            memory->total = 3000;
        }
        return true;
    }

    bool get_utilization_rates(NvmlDeviceHandle device, NvmlUtilization* utilization) noexcept
    {
        if (device)
        {
            utilization->memory = 25;
            utilization->gpu = 100;
        }
        else
        {
            utilization->memory = 0;
            utilization->gpu = 0;
        }
        return true;
    }

    unsigned int device_count{2};
    unsigned int handle_offset{0};
    atlasagent::NvmlRet sample_result{atlasagent::NvmlRet::ErrorNotSupported};
    std::vector<atlasagent::NvmlSample> sample_batch;
    std::vector<unsigned long long> cursors;

    atlasagent::NvmlRet get_gpu_utilization_samples(NvmlDeviceHandle, unsigned long long last_seen,
                                                    std::vector<atlasagent::NvmlSample>* samples)
    {
        cursors.push_back(last_seen);
        *samples = sample_batch;
        return sample_result;
    }

    bool get_performance_state(NvmlDeviceHandle device, NvmlPerfState* perf_state) noexcept
    {
        if (device)
        {
            *perf_state = 10;
        }
        else
        {
            *perf_state = 1;
        }
        return true;
    }

    bool get_temperature(NvmlDeviceHandle device, unsigned int* temperature) noexcept
    {
        if (device)
        {
            *temperature = 72;
        }
        else
        {
            *temperature = 42;
        }
        return true;
    }

    bool get_name(NvmlDeviceHandle device, std::string* name) noexcept
    {
        *name = "Tesla 1234";
        return true;
    }

    bool get_fan_speed(NvmlDeviceHandle device, unsigned int* speed) noexcept { return false; }
};

TEST(Gpu, Metrics)
{
    auto config = Config(WriterConfig(WriterTypes::Memory));
    auto r = Registry(config);

    atlasagent::GpuMetrics metrics(&r, std::make_unique<TestNvml>());
    metrics.gpu_metrics();

    auto memoryWriter = static_cast<MemoryWriter*>(WriterTestHelper::GetImpl());
    auto messages = memoryWriter->GetMessages();
    EXPECT_EQ(15, messages.size());
    EXPECT_EQ(messages.at(0), "g:gpu.count,provider=nvidia:2.000000\n");
    EXPECT_EQ(messages.at(1), "g:gpu.usedMemory,provider=nvidia,gpu=0:2900.000000\n");
    EXPECT_EQ(messages.at(2), "g:gpu.freeMemory,provider=nvidia,gpu=0:100.000000\n");
    EXPECT_EQ(messages.at(3), "g:gpu.totalMemory,provider=nvidia,gpu=0:3000.000000\n");
    EXPECT_EQ(messages.at(4), "g:gpu.utilization,provider=nvidia,gpu=0:0.000000\n");
    EXPECT_EQ(messages.at(5), "g:gpu.memoryActivity,provider=nvidia,gpu=0:0.000000\n");
    EXPECT_EQ(messages.at(6), "g:gpu.perfState,provider=nvidia,gpu=0:1.000000\n");
    EXPECT_EQ(messages.at(7), "d:gpu.temperature,provider=nvidia:42.000000\n");
    EXPECT_EQ(messages.at(8), "g:gpu.usedMemory,provider=nvidia,gpu=1:2000.000000\n");
    EXPECT_EQ(messages.at(9), "g:gpu.freeMemory,provider=nvidia,gpu=1:1000.000000\n");
    EXPECT_EQ(messages.at(10), "g:gpu.totalMemory,provider=nvidia,gpu=1:3000.000000\n");
    EXPECT_EQ(messages.at(11), "g:gpu.utilization,provider=nvidia,gpu=1:100.000000\n");
    EXPECT_EQ(messages.at(12), "g:gpu.memoryActivity,provider=nvidia,gpu=1:25.000000\n");
    EXPECT_EQ(messages.at(13), "g:gpu.perfState,provider=nvidia,gpu=1:10.000000\n");
    EXPECT_EQ(messages.at(14), "d:gpu.temperature,provider=nvidia:72.000000\n");
}

class GpuSampleMetrics : public testing::Test
{
   protected:
    Registry registry{Config(WriterConfig(WriterTypes::Memory))};
    TestNvml* nvml{new TestNvml};
    atlasagent::GpuMetrics<TestNvml> metrics{&registry, std::unique_ptr<TestNvml>(nvml)};

    void SetUp() override
    {
        nvml->sample_result = atlasagent::NvmlRet::Success;
        nvml->device_count = 1;
        Writer()->Clear();
    }
    static MemoryWriter* Writer() { return static_cast<MemoryWriter*>(WriterTestHelper::GetImpl()); }
    void Poll(unsigned long long timestamp, unsigned int value)
    {
        atlasagent::NvmlSample sample{};
        sample.timeStamp = timestamp;
        sample.sampleValue.uiVal = value;
        nvml->sample_batch = {sample};
        metrics.poll_samples();
    }
    std::vector<std::string> Averages()
    {
        std::vector<std::string> averages;
        for (const auto& message : Writer()->GetMessages())
        {
            if (message.find("g:gpu.utilizationAvg,") == 0)
            {
                averages.push_back(message);
            }
        }
        return averages;
    }
};

TEST_F(GpuSampleMetrics, PollsDoNotPublishAndMinuteMeanResets)
{
    Poll(10, 100);  // baseline, not counted
    Poll(20, 100);
    Poll(30, 0);
    EXPECT_TRUE(Writer()->GetMessages().empty());
    metrics.gpu_metrics();
    ASSERT_EQ(1u, Averages().size());
    EXPECT_EQ("g:gpu.utilizationAvg,provider=nvidia,gpu=0:50.000000\n", Averages()[0]);
    EXPECT_EQ((std::vector<unsigned long long>{0, 10, 20, 30}), nvml->cursors);
    Writer()->Clear();
    metrics.gpu_metrics();
    EXPECT_TRUE(Averages().empty());
    Poll(40, 25);
    metrics.gpu_metrics();
    ASSERT_EQ(1u, Averages().size());
    EXPECT_EQ("g:gpu.utilizationAvg,provider=nvidia,gpu=0:25.000000\n", Averages()[0]);
}

TEST_F(GpuSampleMetrics, UnsupportedIsDisabledOnceWithoutAffectingLegacyMetrics)
{
    nvml->sample_result = atlasagent::NvmlRet::ErrorNotSupported;
    metrics.poll_samples();
    metrics.poll_samples();
    EXPECT_EQ(1u, nvml->cursors.size());
    metrics.gpu_metrics();
    EXPECT_TRUE(Averages().empty());
    EXPECT_FALSE(Writer()->GetMessages().empty());
}

TEST_F(GpuSampleMetrics, TransientFailurePreservesCursorAndAccumulatedSamples)
{
    Poll(10, 100);
    Poll(20, 25);
    nvml->sample_result = atlasagent::NvmlRet::ErrorGpuIsLost;
    Poll(30, 100);
    nvml->sample_result = atlasagent::NvmlRet::Success;
    Poll(40, 75);
    metrics.gpu_metrics();
    EXPECT_EQ((std::vector<unsigned long long>{0, 10, 20, 20, 40}), nvml->cursors);
    ASSERT_EQ(1u, Averages().size());
    EXPECT_EQ("g:gpu.utilizationAvg,provider=nvidia,gpu=0:50.000000\n", Averages()[0]);
}

TEST_F(GpuSampleMetrics, ChangedDeviceHandleRebaselines)
{
    Poll(10, 0);
    Poll(20, 100);
    nvml->handle_offset = 100;
    Poll(30, 50);
    metrics.gpu_metrics();
    EXPECT_TRUE(Averages().empty());
    Poll(40, 25);
    metrics.gpu_metrics();
    ASSERT_EQ(1u, Averages().size());
    EXPECT_EQ("g:gpu.utilizationAvg,provider=nvidia,gpu=0:25.000000\n", Averages()[0]);
}

TEST_F(GpuSampleMetrics, RemovedDeviceDoesNotPublishOldSamples)
{
    Poll(10, 0);
    Poll(20, 100);
    nvml->device_count = 0;
    metrics.poll_samples();
    metrics.gpu_metrics();
    EXPECT_TRUE(Averages().empty());
    nvml->device_count = 1;
    Poll(30, 100);  // a returning device must establish a new baseline
    metrics.gpu_metrics();
    EXPECT_TRUE(Averages().empty());
}

TEST(GpuSamplingWorker, DisabledCollectorHasNoWorker)
{
    std::optional<atlasagent::GpuMetrics<TestNvml>> absent;
    auto worker = atlasagent::GpuMetrics<TestNvml>::StartPolling(absent);
    EXPECT_FALSE(worker.joinable());
}

TEST(GpuSamplingWorker, PollsWhileMinutePublisherIsIdleAndStopsPromptly)
{
    Registry registry{Config(WriterConfig(WriterTypes::Memory))};
    auto lib = std::make_unique<TestNvml>();
    auto* nvml = lib.get();
    nvml->sample_result = atlasagent::NvmlRet::Success;
    nvml->device_count = 1;
    std::optional<atlasagent::GpuMetrics<TestNvml>> collector{std::in_place, &registry, std::move(lib)};
    auto worker = atlasagent::GpuMetrics<TestNvml>::StartPolling(collector);
    std::this_thread::sleep_for(std::chrono::milliseconds(5200));
    auto start = std::chrono::steady_clock::now();
    worker = std::jthread{};  // request stop and join, interrupting the 5s wait
    EXPECT_LT(std::chrono::steady_clock::now() - start, std::chrono::seconds(1));
    EXPECT_GE(nvml->cursors.size(), 2u);  // initial baseline plus a background poll
}

}  // namespace