#pragma once

#include "nvml.h"

#include <algorithm>
#include <cstdint>
#include <limits>
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

namespace detail
{

inline NvmlRet GetGpuUtilizationSampleCount(NvmlGetSamples get_samples, NvmlDeviceHandle device,
                                            unsigned long long last_seen, unsigned int* count)
{
    NvmlValueType value_type{};
    return get_samples(device, NvmlSamplingType::GpuUtilization, last_seen, &value_type, count, nullptr);
}

inline NvmlRet GetGpuUtilizationSamples(NvmlGetSamples get_samples, NvmlDeviceHandle device,
                                        unsigned long long last_seen, std::vector<NvmlSample>* buffer)
{
    auto count = static_cast<unsigned int>(buffer->size());
    NvmlValueType value_type{};
    auto result = get_samples(device, NvmlSamplingType::GpuUtilization, last_seen, &value_type, &count, buffer->data());
    if (result == NvmlRet::Success)
    {
        if (value_type != NvmlValueType::UnsignedInt || count > buffer->size())
        {
            return NvmlRet::ErrorInvalidArgument;
        }
        buffer->resize(count);
    }
    return result;
}

}  // namespace detail

// Leave room for samples added between the size query and retrieval. If even
// that is insufficient, the next poll retries with the same cursor.
inline NvmlRet ReadGpuUtilizationSamples(NvmlGetSamples get_samples, NvmlDeviceHandle device,
                                         unsigned long long last_seen, std::vector<NvmlSample>* samples)
{
    samples->clear();
    if (get_samples == nullptr)
    {
        return NvmlRet::ErrorFunctionNotFound;
    }

    unsigned int count = 0;
    auto result = detail::GetGpuUtilizationSampleCount(get_samples, device, last_seen, &count);
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

    constexpr unsigned int kHeadroom = 16;
    count += std::min(kHeadroom, std::numeric_limits<unsigned int>::max() - count);
    std::vector<NvmlSample> buffer(count);
    result = detail::GetGpuUtilizationSamples(get_samples, device, last_seen, &buffer);
    if (result == NvmlRet::ErrorNotFound)
    {
        return NvmlRet::Success;
    }
    if (result != NvmlRet::Success)
    {
        return result;
    }
    *samples = std::move(buffer);
    return NvmlRet::Success;
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
