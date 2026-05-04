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

    bool get_count(uint32_t* count) noexcept;
    bool get_handle(uint32_t index, amdsmi_processor_handle* handle) noexcept;
    bool get_memory(amdsmi_processor_handle handle, AmdSmiMemory* memory) noexcept;
    bool get_activity(amdsmi_processor_handle handle, AmdSmiActivity* activity) noexcept;
    bool get_clocks(amdsmi_processor_handle handle, AmdSmiClocks* clocks) noexcept;
    bool get_temperature(amdsmi_processor_handle handle, int64_t* temperature) noexcept;
    bool get_power(amdsmi_processor_handle handle, uint32_t* power_watts) noexcept;
    bool get_pcie_throughput(amdsmi_processor_handle handle, AmdSmiThroughput* pcie) noexcept;
    bool get_xgmi_throughput(amdsmi_processor_handle handle, AmdSmiThroughput* xgmi) noexcept;

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
