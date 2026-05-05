#include "amd_smi.h"

#include <lib/logger/src/logger.h>

namespace atlasagent
{

namespace
{
const char* status_string(amdsmi_status_t status) noexcept
{
    const char* err = nullptr;
    if (amdsmi_status_code_to_string(status, &err) == AMDSMI_STATUS_SUCCESS && err != nullptr)
    {
        return err;
    }
    return "unknown";
}
}  // namespace

AmdSmi::AmdSmi()
{
    auto ret = amdsmi_init(AMDSMI_INIT_AMD_GPUS);
    if (ret != AMDSMI_STATUS_SUCCESS)
    {
        Logger()->error("amdsmi_init failed: {}", status_string(ret));
        return;
    }
    initialized_ = true;

    uint32_t socket_count = 0;
    ret = amdsmi_get_socket_handles(&socket_count, nullptr);
    if (ret != AMDSMI_STATUS_SUCCESS)
    {
        Logger()->error("amdsmi_get_socket_handles failed: {}", status_string(ret));
        return;
    }

    std::vector<amdsmi_socket_handle> sockets(socket_count);
    ret = amdsmi_get_socket_handles(&socket_count, sockets.data());
    if (ret != AMDSMI_STATUS_SUCCESS)
    {
        Logger()->error("amdsmi_get_socket_handles failed: {}", status_string(ret));
        return;
    }

    for (uint32_t i = 0; i < socket_count; ++i)
    {
        uint32_t device_count = 0;
        ret = amdsmi_get_processor_handles(sockets[i], &device_count, nullptr);
        if (ret != AMDSMI_STATUS_SUCCESS)
        {
            Logger()->warn("amdsmi_get_processor_handles failed for socket {}: {}", i,
                           status_string(ret));
            continue;
        }

        std::vector<amdsmi_processor_handle> dev_handles(device_count);
        ret = amdsmi_get_processor_handles(sockets[i], &device_count, dev_handles.data());
        if (ret != AMDSMI_STATUS_SUCCESS)
        {
            Logger()->warn("amdsmi_get_processor_handles failed for socket {}: {}", i,
                           status_string(ret));
            continue;
        }

        for (uint32_t j = 0; j < device_count; ++j)
        {
            processor_type_t type = {};
            if (amdsmi_get_processor_type(dev_handles[j], &type) == AMDSMI_STATUS_SUCCESS &&
                type == AMDSMI_PROCESSOR_TYPE_AMD_GPU)
            {
                handles_.push_back(dev_handles[j]);
            }
        }
    }
}

AmdSmi::~AmdSmi() noexcept
{
    if (initialized_)
    {
        amdsmi_shut_down();
    }
}

bool AmdSmi::GetCount(uint32_t& count) noexcept
{
    if (!initialized_)
    {
        return false;
    }
    count = static_cast<uint32_t>(handles_.size());
    return true;
}

bool AmdSmi::GetHandle(uint32_t index, amdsmi_processor_handle& handle) noexcept
{
    if (!initialized_ || index >= handles_.size())
    {
        return false;
    }
    handle = handles_[index];
    return true;
}

bool AmdSmi::GetMemory(amdsmi_processor_handle handle, AmdSmiMemory& memory) noexcept
{
    uint64_t total = 0;
    auto ret = amdsmi_get_gpu_memory_total(handle, AMDSMI_MEM_TYPE_VRAM, &total);
    if (ret != AMDSMI_STATUS_SUCCESS)
    {
        return false;
    }

    uint64_t used = 0;
    ret = amdsmi_get_gpu_memory_usage(handle, AMDSMI_MEM_TYPE_VRAM, &used);
    if (ret != AMDSMI_STATUS_SUCCESS)
    {
        return false;
    }

    memory.total = total;
    memory.used = used;
    memory.free = (total > used) ? (total - used) : 0;
    return true;
}

bool AmdSmi::GetActivity(amdsmi_processor_handle handle, AmdSmiActivity& activity) noexcept
{
    amdsmi_engine_usage_t engine_usage = {};
    auto ret = amdsmi_get_gpu_activity(handle, &engine_usage);
    if (ret != AMDSMI_STATUS_SUCCESS)
    {
        return false;
    }

    activity.gfx = engine_usage.gfx_activity;
    activity.umc = engine_usage.umc_activity;
    return true;
}

bool AmdSmi::GetClocks(amdsmi_processor_handle handle, AmdSmiClocks& clocks) noexcept
{
    amdsmi_clk_info_t gfx_info = {};
    auto gfx_ret = amdsmi_get_clock_info(handle, AMDSMI_CLK_TYPE_GFX, &gfx_info);

    amdsmi_clk_info_t mem_info = {};
    auto mem_ret = amdsmi_get_clock_info(handle, AMDSMI_CLK_TYPE_MEM, &mem_info);

    if (gfx_ret != AMDSMI_STATUS_SUCCESS && mem_ret != AMDSMI_STATUS_SUCCESS)
    {
        return false;
    }

    clocks.gfx_mhz = (gfx_ret == AMDSMI_STATUS_SUCCESS) ? gfx_info.clk : 0;
    clocks.mem_mhz = (mem_ret == AMDSMI_STATUS_SUCCESS) ? mem_info.clk : 0;
    return true;
}

bool AmdSmi::GetTemperature(amdsmi_processor_handle handle, int64_t& temperature) noexcept
{   
    int64_t value = 0;
    auto ret = amdsmi_get_temp_metric(handle, AMDSMI_TEMPERATURE_TYPE_EDGE, AMDSMI_TEMP_CURRENT, &value);
    if (ret == AMDSMI_STATUS_SUCCESS)
    {
        temperature = value;
        return true;
    }

    ret = amdsmi_get_temp_metric(handle, AMDSMI_TEMPERATURE_TYPE_HOTSPOT, AMDSMI_TEMP_CURRENT,&value);
    if (ret == AMDSMI_STATUS_SUCCESS)
    {
        temperature = value;
        return true;
    }
    return false;
}

bool AmdSmi::GetPower(amdsmi_processor_handle handle, uint64_t& power_watts) noexcept
{
    amdsmi_power_info_t info = {};
    auto ret = amdsmi_get_power_info(handle, &info);
    if (ret != AMDSMI_STATUS_SUCCESS)
    {
        return false;
    }

    power_watts = info.socket_power;
    return true;
}

bool AmdSmi::GetPcieThroughput(amdsmi_processor_handle handle, AmdSmiThroughput& pcie) noexcept
{
    uint64_t sent = 0;
    uint64_t received = 0;
    uint64_t max_pkt_sz = 0;
    auto ret = amdsmi_get_gpu_pci_throughput(handle, &sent, &received, &max_pkt_sz);
    if (ret != AMDSMI_STATUS_SUCCESS)
    {
        return false;
    }

    pcie.out_bytes_per_sec = sent;
    pcie.in_bytes_per_sec = received;
    return true;
}

bool AmdSmi::GetXgmiThroughput(amdsmi_processor_handle handle, AmdSmiThroughput& xgmi) noexcept
{
    amdsmi_gpu_metrics_t metrics = {};
    auto ret = amdsmi_get_gpu_metrics_info(handle, &metrics);
    if (ret != AMDSMI_STATUS_SUCCESS)
    {
        return false;
    }

    uint64_t curr_read_kb = 0;
    uint64_t curr_write_kb = 0;
    for (size_t i = 0; i < AMDSMI_MAX_NUM_XGMI_LINKS; ++i)
    {
        curr_read_kb += metrics.xgmi_read_data_acc[i];
        curr_write_kb += metrics.xgmi_write_data_acc[i];
    }

    auto now = std::chrono::steady_clock::now();
    auto it = last_xgmi_.find(handle);
    if (it == last_xgmi_.end())
    {
        last_xgmi_[handle] = {now, curr_read_kb, curr_write_kb};
        return false;
    }

    auto dt = std::chrono::duration<double>(now - it->second.timestamp).count();
    if (dt <= 0.0)
    {
        return false;
    }

    uint64_t read_delta_kb = (curr_read_kb >= it->second.read_kb_total)
                                 ? (curr_read_kb - it->second.read_kb_total)
                                 : 0;
    uint64_t write_delta_kb = (curr_write_kb >= it->second.write_kb_total)
                                  ? (curr_write_kb - it->second.write_kb_total)
                                  : 0;

    xgmi.in_bytes_per_sec = static_cast<uint64_t>((static_cast<double>(read_delta_kb) * 1024.0) / dt);
    xgmi.out_bytes_per_sec = static_cast<uint64_t>((static_cast<double>(write_delta_kb) * 1024.0) / dt);

    it->second = {now, curr_read_kb, curr_write_kb};
    return true;
}

}  // namespace atlasagent
