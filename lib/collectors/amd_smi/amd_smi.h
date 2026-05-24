#pragma once

#include <amd_smi/amdsmi.h>

#include <cstdint>
#include <vector>

namespace atlasagent
{

struct AmdSmiMemory
{
    uint64_t total;
    uint64_t used;
    uint64_t free;
};

struct AmdSmiThroughput
{
    uint64_t out_bytes_per_sec;
    uint64_t in_bytes_per_sec;
};

// Thin wrapper over AMD SMI: initializes the library, discovers GPU handles,
// and offers two read methods. All sentinel checks, unit conversions, and
// delta-over-time bookkeeping live in the collector (gpumetrics.cpp), not here.
class AmdSmi
{
   public:
    AmdSmi();
    ~AmdSmi() noexcept;
    AmdSmi(const AmdSmi&) = delete;
    AmdSmi& operator=(const AmdSmi&) = delete;

    // Number of AMD GPUs discovered. Zero if SMI failed to initialize, no
    // GPUs are present, or the user lacks permission.
    uint32_t Count() const noexcept { return static_cast<uint32_t>(handles_.size()); }

    // VRAM totals/usage. Two SMI calls under the hood since AMD splits these
    // across two functions and they're not in the firmware metrics struct.
    bool ReadMemory(uint32_t gpu_id, AmdSmiMemory& out) noexcept;

    // PCIe traffic over a 1-second active measurement performed by the
    // amdgpu kernel driver. Returns NOT_SUPPORTED on ASICs whose driver
    // doesn't implement the get_pcie_usage callback (notably Navi 12 / V520
    // on AWS g4ad) and on virtualized hosts where the sysfs node isn't
    // exposed.
    bool ReadPcieThroughput(uint32_t gpu_id, AmdSmiThroughput& out) noexcept;

    // Snapshot of the firmware metrics table: temperature, activity, clocks,
    // power, xGMI accumulators, PCIe link state, etc. Caller must check
    // sentinel values (UINT_MAX) before using individual fields - the firmware
    // uses UINT_MAX to mean "not populated."
    bool ReadMetrics(uint32_t gpu_id, amdsmi_gpu_metrics_t& out) noexcept;

   private:
    bool initialized_{false};
    std::vector<amdsmi_processor_handle> handles_;
};

}  // namespace atlasagent
