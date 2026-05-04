#include "gpumetrics.h"

namespace atlasagent
{

template <typename Lib>
void GpuMetricsAMD<Lib>::gpu_metrics() noexcept
{
    static auto gpuCountGauge = registry_->CreateGauge("gpu.count");
    static auto gpuTemperature = registry_->CreateDistributionSummary("gpu.temperature");

    uint32_t count = 0;
    if (!smi_->get_count(&count))
    {
        return;
    }
    gpuCountGauge.Set(count);

    for (uint32_t i = 0; i < count; ++i)
    {
        amdsmi_processor_handle handle;
        if (!smi_->get_handle(i, &handle))
        {
            continue;
        }

        AmdSmiMemory memory;
        if (smi_->get_memory(handle, &memory))
        {
            detail::gauge(registry_, "gpu.usedMemory", i).Set(memory.used);
            detail::gauge(registry_, "gpu.freeMemory", i).Set(memory.free);
            detail::gauge(registry_, "gpu.totalMemory", i).Set(memory.total);
        }

        AmdSmiActivity activity;
        if (smi_->get_activity(handle, &activity))
        {
            detail::gauge(registry_, "gpu.utilization", i).Set(activity.gfx);
            detail::gauge(registry_, "gpu.memoryActivity", i).Set(activity.umc);
        }

        AmdSmiClocks clocks;
        if (smi_->get_clocks(handle, &clocks))
        {
            detail::gauge(registry_, "gpu.clockFrequency", i, "gpu").Set(clocks.gfx_mhz);
            detail::gauge(registry_, "gpu.clockFrequency", i, "memory").Set(clocks.mem_mhz);
        }

        int64_t temperature = 0;
        if (smi_->get_temperature(handle, &temperature))
        {
            gpuTemperature.Record(static_cast<double>(temperature));
        }

        uint32_t power = 0;
        if (smi_->get_power(handle, &power))
        {
            detail::gauge(registry_, "gpu.power", i).Set(power);
        }

        AmdSmiThroughput pcie;
        if (smi_->get_pcie_throughput(handle, &pcie))
        {
            detail::counter(registry_, "gpu.amd.pcie.bytes", i, "out")
                .Increment(pcie.out_bytes_per_sec * AmdSmiConstants::BytesConversion);
            detail::counter(registry_, "gpu.amd.pcie.bytes", i, "in")
                .Increment(pcie.in_bytes_per_sec * AmdSmiConstants::BytesConversion);
        }

        AmdSmiThroughput xgmi;
        if (smi_->get_xgmi_throughput(handle, &xgmi))
        {
            detail::counter(registry_, "gpu.amd.xgmi.bytes", i, "out")
                .Increment(xgmi.out_bytes_per_sec * AmdSmiConstants::BytesConversion);
            detail::counter(registry_, "gpu.amd.xgmi.bytes", i, "in")
                .Increment(xgmi.in_bytes_per_sec * AmdSmiConstants::BytesConversion);
        }
    }
}

template class GpuMetricsAMD<AmdSmi>;

}  // namespace atlasagent
