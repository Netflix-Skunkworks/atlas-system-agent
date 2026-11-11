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
        *count = 2;
        return true;
    }

    bool get_by_index(unsigned int index, NvmlDeviceHandle* device) noexcept
    {
        *device = reinterpret_cast<NvmlDeviceHandle>(index);
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

// Stands in for an NVML library that cannot be loaded until the NVIDIA driver shows up, so the retry logic can
// be tested without a GPU or a driver: construction fails while failures_left is positive.
//
// GpuMetrics::Create() constructs the library object itself (std::make_unique<Lib>()), so a test never holds
// one. That is why both counters are static: the test sets failures_left before calling Create(), and every
// construction Create() attempts bumps load_attempts, whether or not it then throws.
class LateNvml : public TestNvml
{
   public:
    static inline int failures_left = 0;
    static inline int load_attempts = 0;

    LateNvml()
    {
        ++load_attempts;
        if (failures_left > 0)
        {
            --failures_left;
            // The exception type the real Nvml throws when the library cannot be loaded. Create() catches it
            // and returns an empty optional, so it never reaches the test
            throw atlasagent::NvmlException("NVIDIA driver is not loaded");
        }
    }

    // Create() calls this after construction; the fake has nothing to initialize
    void initialize() {}
};

using LateGpuMetrics = atlasagent::GpuMetrics<LateNvml>;

// The case RetryCreate() exists for: the driver is loaded after the agent has started, so the startup Create()
// fails and a later retry has to bring the collector up.
TEST(Gpu, RetryCreateLoadsOnceTheLibraryIsAvailable)
{
    auto config = Config(WriterConfig(WriterTypes::Memory));
    auto r = Registry(config);

    // The load fails at startup and on the first retry, then succeeds
    LateNvml::failures_left = 2;
    LateNvml::load_attempts = 0;

    auto gpu = LateGpuMetrics::Create(&r);
    EXPECT_FALSE(gpu.has_value());

    unsigned int retries_left = 3;
    LateGpuMetrics::RetryCreate(gpu, &r, retries_left);
    EXPECT_FALSE(gpu.has_value());
    EXPECT_EQ(retries_left, 2u);

    LateGpuMetrics::RetryCreate(gpu, &r, retries_left);
    EXPECT_TRUE(gpu.has_value());
    EXPECT_EQ(retries_left, 1u);

    // With a collector in place, further calls neither load again nor spend a retry
    LateGpuMetrics::RetryCreate(gpu, &r, retries_left);
    EXPECT_TRUE(gpu.has_value());
    EXPECT_EQ(retries_left, 1u);
    EXPECT_EQ(LateNvml::load_attempts, 3);
}

// A host where the driver never arrives, e.g. one without an NVIDIA GPU: the retries must stop at the limit
// rather than attempt a load on every cycle forever.
TEST(Gpu, RetryCreateGivesUpOnceRetriesAreSpent)
{
    auto config = Config(WriterConfig(WriterTypes::Memory));
    auto r = Registry(config);

    // The load never succeeds
    LateNvml::failures_left = 100;
    LateNvml::load_attempts = 0;

    auto gpu = LateGpuMetrics::Create(&r);
    unsigned int retries_left = 3;
    for (int cycle = 0; cycle < 10; ++cycle)
    {
        LateGpuMetrics::RetryCreate(gpu, &r, retries_left);
    }

    EXPECT_FALSE(gpu.has_value());
    EXPECT_EQ(retries_left, 0u);
    // The startup attempt plus the three retries
    EXPECT_EQ(LateNvml::load_attempts, 4);
}

}  // namespace