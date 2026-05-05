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

    bool GetCount(uint32_t& count) noexcept;
    bool GetHandle(uint32_t index, amdsmi_processor_handle& handle) noexcept;
    bool GetMemory(amdsmi_processor_handle handle, AmdSmiMemory& memory) noexcept;
    bool GetActivity(amdsmi_processor_handle handle, AmdSmiActivity& activity) noexcept;
    bool GetClocks(amdsmi_processor_handle handle, AmdSmiClocks& clocks) noexcept;
    bool GetTemperature(amdsmi_processor_handle handle, int64_t& temperature) noexcept;
    bool GetPower(amdsmi_processor_handle handle, uint64_t& power_watts) noexcept;
    bool GetPcieThroughput(amdsmi_processor_handle handle, AmdSmiThroughput& pcie) noexcept;
    bool GetXgmiThroughput(amdsmi_processor_handle handle, AmdSmiThroughput& xgmi) noexcept;

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
