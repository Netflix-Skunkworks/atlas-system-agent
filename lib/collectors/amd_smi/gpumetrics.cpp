#include "gpumetrics.h"

#include <lib/logger/src/logger.h>

#include <limits>

namespace atlasagent
{

namespace
{

// AMD SMI exposes throughput as bytes-per-second; DCGM (Nvidia) exposes a
// cumulative counter. Multiply by the gather interval (60s) before
// Increment'ing so AMD and Nvidia counters are directly comparable.
constexpr int kBytesConversion = 60;

// Gate a whole-call read. Once the firmware has reported the call unsupported
// the SMI call is skipped entirely; on success on_ok runs; a permanent
// Unsupported latches the call off (logging once); a transient Error is retried
// next tick (the wrapper already logged it).
template <typename ReadFn, typename OnOk>
void Gather(uint32_t gpu_id, FirmwareSupport& support, const char* name, ReadFn&& read, OnOk&& on_ok) noexcept
{
    if (!support)
    {
        return;
    }
    switch (read())
    {
        case ReadStatus::Ok:
            on_ok();
            break;
        case ReadStatus::Unsupported:
            support.Unsupported("[gpu={}] {} not supported by firmware; disabling reads", gpu_id, name);
            break;
        case ReadStatus::Error:
            break;
    }
}

// Set a gauge from a firmware metrics field. The firmware uses the type's
// all-bits-set max value (UINT_MAX) to mean "not populated"; that latches the
// field off (logging once) instead of recording a bogus reading.
template <typename T>
void RecordScalar(uint32_t gpu_id, Gauge& meter, FirmwareSupport& support, const char* field, T value) noexcept
{
    if (value == std::numeric_limits<T>::max())
    {
        support.Unsupported("[gpu={}] {} not supported by firmware", gpu_id, field);
        return;
    }
    meter.Set(value);
}

uint64_t SumXgmiKb(uint32_t gpu_id, const char* label, FirmwareSupport& support,
                   const uint64_t (&arr)[AMDSMI_MAX_NUM_XGMI_LINKS]) noexcept
{
    uint64_t total = 0;
    for (size_t i = 0; i < AMDSMI_MAX_NUM_XGMI_LINKS; ++i)
    {
        if (arr[i] == UINT64_MAX)
        {
            support.Unsupported("[gpu={}] xgmi_{}_data_acc not supported by firmware", gpu_id, label);
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
        Logger()->info("No AMD GPUs detected. Agent will not collect AMD SMI metrics.");
        return std::nullopt;
    }
    Logger()->info("AMD GPU(s) detected. Agent will collect AMD SMI metrics.");
    return GpuMetricsAMD(registry, std::move(smi), count);
}

void GpuMetricsAMD::Collect(std::optional<GpuMetricsAMD>& self) noexcept
{
    if (self.has_value())
    {
        self->GPUMetrics();
    }
}

GpuMetricsAMD::GpuMetricsAMD(Registry* registry, std::unique_ptr<AmdSmi> smi, uint32_t count) noexcept
    : smi_{std::move(smi)},
      gpuCount_{registry->CreateGauge(
          "gpu.count", std::unordered_map<std::string, std::string>{{"provider", "amd"}})},
      temperature_{registry->CreateDistributionSummary(
          "gpu.temperature", std::unordered_map<std::string, std::string>{{"provider", "amd"}})},
      last_xgmi_(count),
      support_(count)
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

void GpuMetricsAMD::Collect(uint32_t gpu_id) noexcept
{
    Logger()->debug("[gpu={}] --- begin iteration ---", gpu_id);
    auto& support = support_[gpu_id];

    AmdSmiMemory memory;
    Gather(gpu_id, support.memoryCall, "gpu memory",
        [&] { return smi_->ReadMemory(gpu_id, memory); }, [&] { RecordMemory(gpu_id, memory); });

    AmdSmiThroughput pcie;
    Gather(
        gpu_id, support.pcieCall, "pcie throughput",
        [&] { return smi_->ReadPcieThroughput(gpu_id, pcie); },
        [&] { RecordPCIEThroughput(gpu_id, pcie); });

    amdsmi_gpu_metrics_t metrics;
    Gather(
        gpu_id, support.metricsCall, "gpu metrics",
        [&] { return smi_->ReadMetrics(gpu_id, metrics); },
        [&] {
            RecordTemperature(gpu_id, metrics);
            RecordActivity(gpu_id, metrics);
            RecordClocks(gpu_id, metrics);
            RecordPower(gpu_id, metrics);
            RecordXgmi(gpu_id, metrics);
        });

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
        support_[gpu_id].temperature.Unsupported("[gpu={}] temperature_edge not supported by firmware", gpu_id);
        return;
    }
    temperature_.Record(m.temperature_edge);
    Logger()->debug("[gpu={}] temperature={}C", gpu_id, m.temperature_edge);
}

void GpuMetricsAMD::RecordActivity(uint32_t gpu_id, const amdsmi_gpu_metrics_t& m) noexcept
{
    auto& support = support_[gpu_id];
    RecordScalar(gpu_id, meters_[gpu_id].utilization, support.gfxActivity, "average_gfx_activity",
                 m.average_gfx_activity);
    RecordScalar(gpu_id, meters_[gpu_id].memoryActivity, support.umcActivity,
                 "average_umc_activity", m.average_umc_activity);
    Logger()->debug("[gpu={}] activity gfx={}% mem={}%", gpu_id, m.average_gfx_activity,
                    m.average_umc_activity);
}

void GpuMetricsAMD::RecordClocks(uint32_t gpu_id, const amdsmi_gpu_metrics_t& m) noexcept
{
    auto& support = support_[gpu_id];
    RecordScalar(gpu_id, meters_[gpu_id].gfxClock, support.gfxClock, "current_gfxclk",
                 m.current_gfxclk);
    RecordScalar(gpu_id, meters_[gpu_id].memoryClock, support.memClock, "current_uclk",
                 m.current_uclk);
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
        support_[gpu_id].power.Unsupported("[gpu={}] socket power not supported by firmware",
                                           gpu_id);
        return;
    }
    meters_[gpu_id].power.Set(watts);
    Logger()->debug("[gpu={}] power={}W", gpu_id, watts);
}

void GpuMetricsAMD::RecordXgmi(uint32_t gpu_id, const amdsmi_gpu_metrics_t& m) noexcept
{
    auto& xgmi_support = support_[gpu_id].xgmi;
    auto curr_read = SumXgmiKb(gpu_id, "read", xgmi_support, m.xgmi_read_data_acc);
    auto curr_write = SumXgmiKb(gpu_id, "write", xgmi_support, m.xgmi_write_data_acc);
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
