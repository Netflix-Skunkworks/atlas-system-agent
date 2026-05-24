#include "amd_smi.h"

#include <lib/logger/src/logger.h>

namespace atlasagent
{

namespace
{

const char* StatusString(amdsmi_status_t status) noexcept
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
        Logger()->error("amdsmi_init failed: {}", StatusString(ret));
        return;
    }
    initialized_ = true;

    uint32_t socket_count = 0;
    if (amdsmi_get_socket_handles(&socket_count, nullptr) != AMDSMI_STATUS_SUCCESS)
    {
        return;
    }

    std::vector<amdsmi_socket_handle> sockets(socket_count);
    if (amdsmi_get_socket_handles(&socket_count, sockets.data()) != AMDSMI_STATUS_SUCCESS)
    {
        return;
    }

    for (uint32_t i = 0; i < socket_count; ++i)
    {
        uint32_t device_count = 0;
        if (amdsmi_get_processor_handles(sockets[i], &device_count, nullptr) !=
            AMDSMI_STATUS_SUCCESS)
        {
            continue;
        }

        std::vector<amdsmi_processor_handle> dev_handles(device_count);
        if (amdsmi_get_processor_handles(sockets[i], &device_count, dev_handles.data()) !=
            AMDSMI_STATUS_SUCCESS)
        {
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

bool AmdSmi::ReadMemory(uint32_t gpu_id, AmdSmiMemory& out) noexcept
{
    if (gpu_id >= handles_.size())
    {
        return false;
    }
    auto handle = handles_[gpu_id];

    uint64_t total = 0;
    auto ret = amdsmi_get_gpu_memory_total(handle, AMDSMI_MEM_TYPE_VRAM, &total);
    if (ret != AMDSMI_STATUS_SUCCESS)
    {
        Logger()->error("[gpu={}] amdsmi_get_gpu_memory_total failed: {}", gpu_id,
                        StatusString(ret));
        return false;
    }

    uint64_t used = 0;
    ret = amdsmi_get_gpu_memory_usage(handle, AMDSMI_MEM_TYPE_VRAM, &used);
    if (ret != AMDSMI_STATUS_SUCCESS)
    {
        Logger()->error("[gpu={}] amdsmi_get_gpu_memory_usage failed: {}", gpu_id,
                        StatusString(ret));
        return false;
    }

    out.total = total;
    out.used = used;
    out.free = (total > used) ? (total - used) : 0;
    return true;
}

bool AmdSmi::ReadPcieThroughput(uint32_t gpu_id, AmdSmiThroughput& out) noexcept
{
    if (gpu_id >= handles_.size())
    {
        return false;
    }
    uint64_t sent = 0;
    uint64_t received = 0;
    uint64_t max_pkt_sz = 0;
    auto ret = amdsmi_get_gpu_pci_throughput(handles_[gpu_id], &sent, &received, &max_pkt_sz);
    if (ret != AMDSMI_STATUS_SUCCESS)
    {
        Logger()->error("[gpu={}] amdsmi_get_gpu_pci_throughput failed: {}", gpu_id,
                        StatusString(ret));
        return false;
    }
    out.out_bytes_per_sec = sent;
    out.in_bytes_per_sec = received;
    return true;
}

bool AmdSmi::ReadMetrics(uint32_t gpu_id, amdsmi_gpu_metrics_t& out) noexcept
{
    if (gpu_id >= handles_.size())
    {
        return false;
    }
    out = {};
    auto ret = amdsmi_get_gpu_metrics_info(handles_[gpu_id], &out);
    if (ret != AMDSMI_STATUS_SUCCESS)
    {
        Logger()->error("[gpu={}] amdsmi_get_gpu_metrics_info failed: {}", gpu_id,
                        StatusString(ret));
        return false;
    }
    return true;
}

}  // namespace atlasagent
