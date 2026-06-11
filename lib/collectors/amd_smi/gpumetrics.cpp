#include "gpumetrics.h"

#include <lib/logger/src/logger.h>

namespace atlasagent
{

namespace
{

// AMD SMI exposes throughput as bytes-per-second; DCGM (Nvidia) exposes a
// cumulative counter. Multiply by the gather interval (60s) before
// Increment'ing so AMD and Nvidia counters are directly comparable.
constexpr int kBytesConversion = 60;

uint64_t SumXgmiKb(uint32_t gpu_id, const char* label, bool& supported,
                   const uint64_t (&arr)[AMDSMI_MAX_NUM_XGMI_LINKS]) noexcept
{
    uint64_t total = 0;
    for (size_t i = 0; i < AMDSMI_MAX_NUM_XGMI_LINKS; ++i)
    {
        if (arr[i] == UINT64_MAX)
        {
            if (supported)
            {
                Logger()->error("[gpu={}] xgmi_{}_data_acc not supported by firmware", gpu_id,
                                label);
                supported = false;
            }
            continue;
        }
        total += arr[i];
    }
    return total;
}

}  // namespace

std::optional<GpuMetricsAMD> GpuMetricsAMD::Create(Registry* registry) noexcept
{
    auto smi = std::make_unique<AmdSmi>();
    auto count = smi->Count();
    if (count == 0)
    {
        return std::nullopt;
    }
    return GpuMetricsAMD(registry, std::move(smi), count);
}

GpuMetricsAMD::GpuMetricsAMD(Registry* registry, std::unique_ptr<AmdSmi> smi,
                             uint32_t count) noexcept
    : smi_{std::move(smi)},
      gpuCount_{registry->CreateGauge(
          "gpu.count", std::unordered_map<std::string, std::string>{{"provider", "amd"}})},
      temperature_{registry->CreateDistributionSummary(
          "gpu.temperature", std::unordered_map<std::string, std::string>{{"provider", "amd"}})},
      last_xgmi_(count),
      field_support_(count)
{
    meters_.reserve(count);
    for (uint32_t i = 0; i < count; ++i)
    {
        meters_.emplace_back(registry, i);
    }
}

void GpuMetricsAMD::GPUMetrics() noexcept
{
    auto count = smi_->Count();
    gpuCount_.Set(count);
    for (uint32_t i = 0; i < count; ++i)
    {
        Collect(i);
    }
}

// TODO: If a metric is unsupported by firmware, we should stop trying to read it on subsequent
// iterations to avoid unnecessary overhead.
void GpuMetricsAMD::Collect(uint32_t gpu_id) noexcept
{
    Logger()->debug("[gpu={}] --- begin iteration ---", gpu_id);

    AmdSmiMemory memory;
    if (smi_->ReadMemory(gpu_id, memory))
    {
        RecordMemory(gpu_id, memory);
    }

    AmdSmiThroughput pcie;
    if (smi_->ReadPcieThroughput(gpu_id, pcie))
    {
        RecordPCIEThroughput(gpu_id, pcie);
    }

    amdsmi_gpu_metrics_t metrics;
    if (smi_->ReadMetrics(gpu_id, metrics))
    {
        RecordTemperature(gpu_id, metrics);
        RecordActivity(gpu_id, metrics);
        RecordClocks(gpu_id, metrics);
        RecordPower(gpu_id, metrics);
        RecordXgmi(gpu_id, metrics);
    }

    Logger()->debug("[gpu={}] --- end iteration ---", gpu_id);
}

void GpuMetricsAMD::RecordMemory(uint32_t gpu_id, const AmdSmiMemory& memory) noexcept
{
    meters_[gpu_id].usedMemory.Set(memory.used);
    meters_[gpu_id].freeMemory.Set(memory.free);
    meters_[gpu_id].totalMemory.Set(memory.total);
    Logger()->debug("[gpu={}] memory used={} free={} total={} bytes", gpu_id, memory.used,
                    memory.free, memory.total);
}

void GpuMetricsAMD::RecordPCIEThroughput(uint32_t gpu_id, const AmdSmiThroughput& pcie) noexcept
{
    meters_[gpu_id].pcieOut.Increment(pcie.out_bytes_per_sec * kBytesConversion);
    meters_[gpu_id].pcieIn.Increment(pcie.in_bytes_per_sec * kBytesConversion);
    Logger()->debug("[gpu={}] pcie out={} in={} bytes/sec", gpu_id, pcie.out_bytes_per_sec,
                    pcie.in_bytes_per_sec);
}

void GpuMetricsAMD::RecordTemperature(uint32_t gpu_id, const amdsmi_gpu_metrics_t& m) noexcept
{
    if (m.temperature_edge == UINT16_MAX)
    {
        if (field_support_[gpu_id].temperature)
        {
            Logger()->error("[gpu={}] temperature_edge not supported by firmware", gpu_id);
            field_support_[gpu_id].temperature = false;
        }
        return;
    }
    temperature_.Record(m.temperature_edge);
    Logger()->debug("[gpu={}] temperature={}C", gpu_id, m.temperature_edge);
}

void GpuMetricsAMD::RecordActivity(uint32_t gpu_id, const amdsmi_gpu_metrics_t& m) noexcept
{
    if (m.average_gfx_activity == UINT16_MAX)
    {
        if (field_support_[gpu_id].gfxActivity)
        {
            Logger()->error("[gpu={}] average_gfx_activity not supported by firmware", gpu_id);
            field_support_[gpu_id].gfxActivity = false;
        }
    }
    else
    {
        meters_[gpu_id].utilization.Set(m.average_gfx_activity);
    }
    if (m.average_umc_activity == UINT16_MAX)
    {
        if (field_support_[gpu_id].umcActivity)
        {
            Logger()->error("[gpu={}] average_umc_activity not supported by firmware", gpu_id);
            field_support_[gpu_id].umcActivity = false;
        }
    }
    else
    {
        meters_[gpu_id].memoryActivity.Set(m.average_umc_activity);
    }
    Logger()->debug("[gpu={}] activity gfx={}% mem={}%", gpu_id, m.average_gfx_activity,
                    m.average_umc_activity);
}

void GpuMetricsAMD::RecordClocks(uint32_t gpu_id, const amdsmi_gpu_metrics_t& m) noexcept
{
    if (m.current_gfxclk == UINT16_MAX)
    {
        if (field_support_[gpu_id].gfxClock)
        {
            Logger()->error("[gpu={}] current_gfxclk not supported by firmware", gpu_id);
            field_support_[gpu_id].gfxClock = false;
        }
    }
    else
    {
        meters_[gpu_id].gfxClock.Set(m.current_gfxclk);
    }
    if (m.current_uclk == UINT16_MAX)
    {
        if (field_support_[gpu_id].memClock)
        {
            Logger()->error("[gpu={}] current_uclk not supported by firmware", gpu_id);
            field_support_[gpu_id].memClock = false;
        }
    }
    else
    {
        meters_[gpu_id].memoryClock.Set(m.current_uclk);
    }
    Logger()->debug("[gpu={}] clock gfx={}MHz mem={}MHz", gpu_id, m.current_gfxclk,
                    m.current_uclk);
}

void GpuMetricsAMD::RecordPower(uint32_t gpu_id, const amdsmi_gpu_metrics_t& m) noexcept
{
    // MI300+ populates current_socket_power; Navi/MI200 and earlier populate
    // average_socket_power. Prefer current; fall back to average.
    uint16_t watts = UINT16_MAX;
    if (m.current_socket_power != UINT16_MAX)
    {
        watts = m.current_socket_power;
    }
    else if (m.average_socket_power != UINT16_MAX)
    {
        watts = m.average_socket_power;
    }
    else
    {
        if (field_support_[gpu_id].power)
        {
            Logger()->error("[gpu={}] socket power not supported by firmware", gpu_id);
            field_support_[gpu_id].power = false;
        }
        return;
    }
    meters_[gpu_id].power.Set(watts);
    Logger()->debug("[gpu={}] power={}W", gpu_id, watts);
}

void GpuMetricsAMD::RecordXgmi(uint32_t gpu_id, const amdsmi_gpu_metrics_t& m) noexcept
{
    auto curr_read = SumXgmiKb(gpu_id, "read", field_support_[gpu_id].xgmi, m.xgmi_read_data_acc);
    auto curr_write = SumXgmiKb(gpu_id, "write", field_support_[gpu_id].xgmi, m.xgmi_write_data_acc);
    auto now = std::chrono::steady_clock::now();

    auto& slot = last_xgmi_[gpu_id];
    if (!slot.has_value())
    {
        // First call for this GPU: record baseline, no rate yet.
        slot = XgmiSample{now, curr_read, curr_write};
        return;
    }

    auto dt = std::chrono::duration<double>(now - slot->timestamp).count();
    if (dt <= 0.0)
    {
        return;
    }

    auto read_delta = (curr_read >= slot->read_kb) ? (curr_read - slot->read_kb) : 0;
    auto write_delta = (curr_write >= slot->write_kb) ? (curr_write - slot->write_kb) : 0;
    auto in_bps = static_cast<uint64_t>((static_cast<double>(read_delta) * 1024.0) / dt);
    auto out_bps = static_cast<uint64_t>((static_cast<double>(write_delta) * 1024.0) / dt);

    meters_[gpu_id].xgmiIn.Increment(in_bps * kBytesConversion);
    meters_[gpu_id].xgmiOut.Increment(out_bps * kBytesConversion);
    Logger()->debug("[gpu={}] xgmi in={} out={} bytes/sec", gpu_id, in_bps, out_bps);

    *slot = XgmiSample{now, curr_read, curr_write};
}

}  // namespace atlasagent
