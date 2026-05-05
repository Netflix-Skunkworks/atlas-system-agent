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
    return GpuMetricsAMD(registry, std::move(smi));
}

GpuMetricsAMD::GpuMetricsAMD(Registry* registry, std::unique_ptr<AmdSmi> smi) noexcept
    : registry_{registry}, smi_{std::move(smi)}
{
}

void GpuMetricsAMD::GPUMetrics() noexcept
{
    static auto gpuCountGauge = registry_->CreateGauge("gpu.count");

    uint32_t count = 0;
    if (!smi_->GetCount(count))
    {
        return;
    }
    gpuCountGauge.Set(count);

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
    detail::gauge(registry_, "gpu.usedMemory", i).Set(memory.used);
    detail::gauge(registry_, "gpu.freeMemory", i).Set(memory.free);
    detail::gauge(registry_, "gpu.totalMemory", i).Set(memory.total);
}

void GpuMetricsAMD::GetActivityMetrics(unsigned int i, amdsmi_processor_handle handle) noexcept
{
    AmdSmiActivity activity;
    if (!smi_->GetActivity(handle, activity))
    {
        return;
    }
    detail::gauge(registry_, "gpu.utilization", i).Set(activity.gfx);
    detail::gauge(registry_, "gpu.memoryActivity", i).Set(activity.umc);
}

void GpuMetricsAMD::GetClockMetrics(unsigned int i, amdsmi_processor_handle handle) noexcept
{
    AmdSmiClocks clocks;
    if (!smi_->GetClocks(handle, clocks))
    {
        return;
    }
    detail::gauge(registry_, "gpu.clockFrequency", i, "gpu").Set(clocks.gfx_mhz);
    detail::gauge(registry_, "gpu.clockFrequency", i, "memory").Set(clocks.mem_mhz);
}

void GpuMetricsAMD::GetTemperatureMetric(amdsmi_processor_handle handle) noexcept
{
    static auto gpuTemperature = registry_->CreateDistributionSummary("gpu.temperature");

    int64_t temperature = 0;
    if (!smi_->GetTemperature(handle, temperature))
    {
        return;
    }
    gpuTemperature.Record(static_cast<double>(temperature));
}

void GpuMetricsAMD::GetPowerMetric(unsigned int i, amdsmi_processor_handle handle) noexcept
{
    uint64_t power = 0;
    if (!smi_->GetPower(handle, power))
    {
        return;
    }
    detail::gauge(registry_, "gpu.power", i).Set(power);
}

void GpuMetricsAMD::GetPcieMetrics(unsigned int i, amdsmi_processor_handle handle) noexcept
{
    AmdSmiThroughput pcie;
    if (!smi_->GetPcieThroughput(handle, pcie))
    {
        return;
    }
    detail::counter(registry_, "gpu.amd.pcie.bytes", i, "out")
        .Increment(pcie.out_bytes_per_sec * AmdSmiConstants::BytesConversion);
    detail::counter(registry_, "gpu.amd.pcie.bytes", i, "in")
        .Increment(pcie.in_bytes_per_sec * AmdSmiConstants::BytesConversion);
}

void GpuMetricsAMD::GetXgmiMetrics(unsigned int i, amdsmi_processor_handle handle) noexcept
{
    AmdSmiThroughput xgmi;
    if (!smi_->GetXgmiThroughput(handle, xgmi))
    {
        return;
    }
    detail::counter(registry_, "gpu.amd.xgmi.bytes", i, "out")
        .Increment(xgmi.out_bytes_per_sec * AmdSmiConstants::BytesConversion);
    detail::counter(registry_, "gpu.amd.xgmi.bytes", i, "in")
        .Increment(xgmi.in_bytes_per_sec * AmdSmiConstants::BytesConversion);
}

}  // namespace atlasagent
