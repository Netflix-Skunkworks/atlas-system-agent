#pragma once

#include "nvml.h"

#include <algorithm>
#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

namespace atlasagent
{

// NVML ABI declarations. Keep the complete value union: nvmlSample_t has an
// eight-byte value even when GPU utilization uses only its unsigned-int member.
union NvmlValue
{
    double dVal;
    int siVal;
    unsigned int uiVal;
    unsigned long ulVal;
    unsigned long long ullVal;
    long long sllVal;
    unsigned short usVal;
};

struct NvmlSample
{
    unsigned long long timeStamp;  // CPU timestamp, microseconds
    NvmlValue sampleValue;
};

enum class NvmlSamplingType : int
{
    GpuUtilization = 1  // NVML_GPU_UTILIZATION_SAMPLES
};

enum class NvmlValueType : int
{
    UnsignedInt = 1  // NVML_VALUE_TYPE_UNSIGNED_INT
};

using NvmlGetSamples = NvmlRet (*)(NvmlDeviceHandle, NvmlSamplingType, unsigned long long, NvmlValueType*,
                                   unsigned int*, NvmlSample*);

// The ring may grow between the size query and retrieval. Retry insufficient
// capacity a bounded number of times, without advancing the caller's cursor.
inline NvmlRet ReadGpuUtilizationSamples(NvmlGetSamples get_samples, NvmlDeviceHandle device,
                                         unsigned long long last_seen, std::vector<NvmlSample>* samples)
{
    samples->clear();
    if (get_samples == nullptr)
    {
        return NvmlRet::ErrorFunctionNotFound;
    }

    for (int attempt = 0; attempt < 3; ++attempt)
    {
        unsigned int count = 0;
        NvmlValueType value_type{};
        auto result = get_samples(device, NvmlSamplingType::GpuUtilization, last_seen, &value_type, &count, nullptr);
        if (result == NvmlRet::ErrorNotFound)
        {
            return NvmlRet::Success;  // no new data, not a zero-utilization sample
        }
        if (result != NvmlRet::Success && result != NvmlRet::ErrorInsufficientSize)
        {
            return result;
        }
        if (count == 0)
        {
            return NvmlRet::Success;
        }

        std::vector<NvmlSample> buffer(count);
        result = get_samples(device, NvmlSamplingType::GpuUtilization, last_seen, &value_type, &count, buffer.data());
        if (result == NvmlRet::ErrorInsufficientSize)
        {
            continue;
        }
        if (result == NvmlRet::ErrorNotFound)
        {
            return NvmlRet::Success;
        }
        if (result != NvmlRet::Success)
        {
            return result;
        }
        if (value_type != NvmlValueType::UnsignedInt || count > buffer.size())
        {
            return NvmlRet::ErrorInvalidArgument;
        }
        buffer.resize(count);
        *samples = std::move(buffer);
        return NvmlRet::Success;
    }
    return NvmlRet::ErrorInsufficientSize;
}

// One accumulator per GPU. Polls contribute individual samples, not equally
// weighted poll averages. Publishing resets totals but retains the NVML cursor.
class GpuUtilizationWindow
{
   public:
    unsigned long long last_seen() const noexcept { return last_seen_; }

    void Add(const std::vector<NvmlSample>& samples) noexcept
    {
        auto newest = last_seen_;
        for (const auto& sample : samples)
        {
            newest = std::max(newest, sample.timeStamp);
            if (initialized_ && sample.timeStamp > last_seen_)
            {
                total_ += sample.sampleValue.uiVal;
                ++count_;
            }
        }
        // First successful poll only establishes a baseline, excluding history
        // retained in the driver before this collection interval started.
        initialized_ = true;
        last_seen_ = newest;
    }

    std::optional<double> Publish() noexcept
    {
        std::optional<double> average;
        if (count_ != 0)
        {
            average = static_cast<double>(total_) / count_;
        }
        total_ = 0;
        count_ = 0;
        return average;
    }

   private:
    bool initialized_{false};
    unsigned long long last_seen_{0};
    uint64_t total_{0};
    uint64_t count_{0};
};

}  // namespace atlasagent
