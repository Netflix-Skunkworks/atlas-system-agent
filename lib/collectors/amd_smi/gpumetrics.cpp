#include "gpumetrics.h"

#include <lib/logger/src/logger.h>

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

GpuMetricsAMD::GpuMetricsAMD(Registry* registry, std::unique_ptr<AmdSmi> smi, uint32_t count) noexcept
    : registry_{registry},
      smi_{std::move(smi)},
      gpuCount_{registry->CreateGauge("gpu.count", std::unordered_map<std::string, std::string>{{"provider", "amd"}})},
      temperature_{registry->CreateDistributionSummary(
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

        Logger()->debug("[gpu={}] --- begin iteration ---", i);
        GetMemoryMetrics(i, handle);
        GetActivityMetrics(i, handle);
        GetClockMetrics(i, handle);
        GetTemperatureMetric(i, handle);
        GetPowerMetric(i, handle);
        GetPcieMetrics(i, handle);
        GetXgmiMetrics(i, handle);
        Logger()->debug("[gpu={}] --- end iteration ---", i);
    }
}

void GpuMetricsAMD::GetMemoryMetrics(unsigned int i, amdsmi_processor_handle handle) noexcept
{
    AmdSmiMemory memory;
    if (!smi_->GetMemory(i, handle, memory))
    {
        return;
    }
    meters_[i].usedMemory.Set(memory.used);
    meters_[i].freeMemory.Set(memory.free);
    meters_[i].totalMemory.Set(memory.total);
    Logger()->debug("[gpu={}] memory used={} free={} total={} bytes", i, memory.used, memory.free, memory.total);
}

void GpuMetricsAMD::GetActivityMetrics(unsigned int i, amdsmi_processor_handle handle) noexcept
{
    AmdSmiActivity activity;
    if (!smi_->GetActivity(i, handle, activity))
    {
        return;
    }
    meters_[i].utilization.Set(activity.gfx);
    meters_[i].memoryActivity.Set(activity.umc);
    Logger()->debug("[gpu={}] activity gfx={}% umc={}%", i, activity.gfx, activity.umc);
}

void GpuMetricsAMD::GetClockMetrics(unsigned int i, amdsmi_processor_handle handle) noexcept
{
    AmdSmiClocks clocks;
    if (!smi_->GetClocks(i, handle, clocks))
    {
        return;
    }
    meters_[i].gfxClock.Set(clocks.gfx_mhz);
    meters_[i].memoryClock.Set(clocks.mem_mhz);
    Logger()->debug("[gpu={}] clock gfx={}MHz mem={}MHz", i, clocks.gfx_mhz, clocks.mem_mhz);
}

void GpuMetricsAMD::GetTemperatureMetric(unsigned int i, amdsmi_processor_handle handle) noexcept
{
    int64_t temperature = 0;
    if (!smi_->GetTemperature(i, handle, temperature))
    {
        return;
    }
    temperature_.Record(static_cast<double>(temperature));
    Logger()->debug("[gpu={}] temperature={}C", i, temperature);
}

void GpuMetricsAMD::GetPowerMetric(unsigned int i, amdsmi_processor_handle handle) noexcept
{
    uint64_t power = 0;
    if (!smi_->GetPower(i, handle, power))
    {
        return;
    }
    meters_[i].power.Set(power);
    Logger()->debug("[gpu={}] power={}W", i, power);
}

void GpuMetricsAMD::GetPcieMetrics(unsigned int i, amdsmi_processor_handle handle) noexcept
{
    AmdSmiThroughput pcie;
    if (!smi_->GetPcieThroughput(i, handle, pcie))
    {
        return;
    }
    meters_[i].pcieOut.Increment(pcie.out_bytes_per_sec * AmdSmiConstants::BytesConversion);
    meters_[i].pcieIn.Increment(pcie.in_bytes_per_sec * AmdSmiConstants::BytesConversion);
    Logger()->debug("[gpu={}] pcie out={} in={} bytes/sec", i, pcie.out_bytes_per_sec, pcie.in_bytes_per_sec);
}

void GpuMetricsAMD::GetXgmiMetrics(unsigned int i, amdsmi_processor_handle handle) noexcept
{
    AmdSmiThroughput xgmi;
    if (!smi_->GetXgmiThroughput(i, handle, xgmi))
    {
        return;
    }
    meters_[i].xgmiOut.Increment(xgmi.out_bytes_per_sec * AmdSmiConstants::BytesConversion);
    meters_[i].xgmiIn.Increment(xgmi.in_bytes_per_sec * AmdSmiConstants::BytesConversion);
    Logger()->debug("[gpu={}] xgmi out={} in={} bytes/sec", i, xgmi.out_bytes_per_sec, xgmi.in_bytes_per_sec);
}

}  // namespace atlasagent
