#include "gpumetrics.h"

namespace atlasagent
{

std::optional<GpuMetricsAMD> GpuMetricsAMD::Create(Registry* registry) noexcept
{
    auto smi = std::make_unique<AmdSmi>();
    uint32_t count = 0;
    if (!smi->GetCount(count) || count == 0)
    {
        return std::nullopt;
    }
    return GpuMetricsAMD(registry, std::move(smi), count);
}

GpuMetricsAMD::GpuMetricsAMD(Registry* registry, std::unique_ptr<AmdSmi> smi,
                             uint32_t count) noexcept
    : registry_{registry}
    , smi_{std::move(smi)}
    , gpuCount_{registry->CreateGauge(
          "gpu.count", std::unordered_map<std::string, std::string>{{"provider", "amd"}})}
    , temperature_{registry->CreateDistributionSummary(
          "gpu.temperature", std::unordered_map<std::string, std::string>{{"provider", "amd"}})}
{
    meters_.reserve(count);
    for (uint32_t i = 0; i < count; ++i)
    {
        meters_.emplace_back(registry, i);
    }
}

void GpuMetricsAMD::GPUMetrics() noexcept
{
    uint32_t count = 0;
    if (!smi_->GetCount(count))
    {
        return;
    }
    gpuCount_.Set(count);

    for (uint32_t i = 0; i < count; ++i)
    {
        amdsmi_processor_handle handle;
        if (!smi_->GetHandle(i, handle))
        {
            continue;
        }

        GetMemoryMetrics(i, handle);
        GetActivityMetrics(i, handle);
        GetClockMetrics(i, handle);
        GetTemperatureMetric(handle);
        GetPowerMetric(i, handle);
        GetPcieMetrics(i, handle);
        GetXgmiMetrics(i, handle);
    }
}

void GpuMetricsAMD::GetMemoryMetrics(unsigned int i, amdsmi_processor_handle handle) noexcept
{
    AmdSmiMemory memory;
    if (!smi_->GetMemory(handle, memory))
    {
        return;
    }
    meters_[i].usedMemory.Set(memory.used);
    meters_[i].freeMemory.Set(memory.free);
    meters_[i].totalMemory.Set(memory.total);
}

void GpuMetricsAMD::GetActivityMetrics(unsigned int i, amdsmi_processor_handle handle) noexcept
{
    AmdSmiActivity activity;
    if (!smi_->GetActivity(handle, activity))
    {
        return;
    }
    meters_[i].utilization.Set(activity.gfx);
    meters_[i].memoryActivity.Set(activity.umc);
}

void GpuMetricsAMD::GetClockMetrics(unsigned int i, amdsmi_processor_handle handle) noexcept
{
    AmdSmiClocks clocks;
    if (!smi_->GetClocks(handle, clocks))
    {
        return;
    }
    meters_[i].gfxClock.Set(clocks.gfx_mhz);
    meters_[i].memoryClock.Set(clocks.mem_mhz);
}

void GpuMetricsAMD::GetTemperatureMetric(amdsmi_processor_handle handle) noexcept
{
    int64_t temperature = 0;
    if (!smi_->GetTemperature(handle, temperature))
    {
        return;
    }
    temperature_.Record(static_cast<double>(temperature));
}

void GpuMetricsAMD::GetPowerMetric(unsigned int i, amdsmi_processor_handle handle) noexcept
{
    uint64_t power = 0;
    if (!smi_->GetPower(handle, power))
    {
        return;
    }
    meters_[i].power.Set(power);
}

void GpuMetricsAMD::GetPcieMetrics(unsigned int i, amdsmi_processor_handle handle) noexcept
{
    AmdSmiThroughput pcie;
    if (!smi_->GetPcieThroughput(handle, pcie))
    {
        return;
    }
    meters_[i].pcieOut.Increment(pcie.out_bytes_per_sec * AmdSmiConstants::BytesConversion);
    meters_[i].pcieIn.Increment(pcie.in_bytes_per_sec * AmdSmiConstants::BytesConversion);
}

void GpuMetricsAMD::GetXgmiMetrics(unsigned int i, amdsmi_processor_handle handle) noexcept
{
    AmdSmiThroughput xgmi;
    if (!smi_->GetXgmiThroughput(handle, xgmi))
    {
        return;
    }
    meters_[i].xgmiOut.Increment(xgmi.out_bytes_per_sec * AmdSmiConstants::BytesConversion);
    meters_[i].xgmiIn.Increment(xgmi.in_bytes_per_sec * AmdSmiConstants::BytesConversion);
}

}  // namespace atlasagent
