#pragma once

#include <amd_smi/amdsmi.h>

#include <chrono>
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace atlasagent
{

struct AmdSmiMemory
{
    uint64_t total;
    uint64_t used;
    uint64_t free;
};

struct AmdSmiActivity
{
    uint32_t gfx;
    uint32_t umc;
};

struct AmdSmiClocks
{
    uint32_t gfx_mhz;
    uint32_t mem_mhz;
};

struct AmdSmiThroughput
{
    uint64_t out_bytes_per_sec;
    uint64_t in_bytes_per_sec;
};

class AmdSmi
{
   public:
    AmdSmi();
    ~AmdSmi() noexcept;
    AmdSmi(const AmdSmi&) = delete;
    AmdSmi& operator=(const AmdSmi&) = delete;

    // All Get* methods log failures internally (with gpu_id context where
    // applicable) and return true on success, false on failure. Callers
    // should just check the bool and continue/return; do not log again.
    bool GetCount(uint32_t& count) noexcept;
    bool GetHandle(uint32_t gpu_id, amdsmi_processor_handle& handle) noexcept;
    bool GetMemory(uint32_t gpu_id, amdsmi_processor_handle handle, AmdSmiMemory& memory) noexcept;
    bool GetActivity(uint32_t gpu_id, amdsmi_processor_handle handle, AmdSmiActivity& activity) noexcept;
    bool GetClocks(uint32_t gpu_id, amdsmi_processor_handle handle, AmdSmiClocks& clocks) noexcept;
    bool GetTemperature(uint32_t gpu_id, amdsmi_processor_handle handle, int64_t& temperature) noexcept;
    bool GetPower(uint32_t gpu_id, amdsmi_processor_handle handle, uint64_t& power_watts) noexcept;
    bool GetPcieThroughput(uint32_t gpu_id, amdsmi_processor_handle handle, AmdSmiThroughput& pcie) noexcept;
    bool GetXgmiThroughput(uint32_t gpu_id, amdsmi_processor_handle handle, AmdSmiThroughput& xgmi) noexcept;

   private:
    struct XgmiSample
    {
        std::chrono::steady_clock::time_point timestamp;
        uint64_t read_kb_total;
        uint64_t write_kb_total;
    };

    bool initialized_{false};
    std::vector<amdsmi_processor_handle> handles_;
    std::unordered_map<amdsmi_processor_handle, XgmiSample> last_xgmi_;
};

}  // namespace atlasagent
