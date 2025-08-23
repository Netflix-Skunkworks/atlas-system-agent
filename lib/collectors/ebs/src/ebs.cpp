#include "ebs.h"

#include <lib/util/src/util.h>

#include <fcntl.h>
#include <filesystem>
#include <regex>
#include <sys/ioctl.h>
#include <unistd.h>

struct EBSMetricConstants
{
    // Operation types
    static constexpr auto ReadOp{"read"};
    static constexpr auto WriteOp{"write"};

    // Device types
    static constexpr auto Volume{"volume"};
    static constexpr auto Instance{"instance"};

    // Metric types
    static constexpr auto ebsOperations{"aws.ebs.totalOperations"};
    static constexpr auto ebsBytes{"aws.ebs.totalBytes"};
    static constexpr auto ebsTime{"aws.ebs.totalTime"};
    static constexpr auto ebsIOPS{"aws.ebs.perfExceededIOPS"};
    static constexpr auto ebsTP{"aws.ebs.perfExceededTput"};
    static constexpr auto ebsQueueLength{"aws.ebs.volumeQueueLength"};
    static constexpr auto ebsHistogram{"aws.ebs.ioLatencyHistogram"};

    // Conversion Constants
    static constexpr auto ebsMicrosecondsToSeconds{.000001};
};
using EBSMC = EBSMetricConstants;

std::optional<std::vector<std::string>> ebs_parse_regex_config_file(const char* configFilePath)
try
{
    // Read the all the device paths in the config file
    std::optional<std::vector<std::string>> configContents = atlasagent::read_file(configFilePath);
    if (configContents.has_value() == false)
    {
        atlasagent::Logger()->error("Error reading config file {}", configFilePath);
        return std::nullopt;
    }

    // Skip empty files
    if (configContents.value().empty())
    {
        atlasagent::Logger()->debug("Empty config file {}", configFilePath);
        return std::nullopt;
    }

    // Read all the device paths and assert the device exists
    std::vector<std::string> devicePaths{};
    for (const auto& device : configContents.value())
    {
        if (std::filesystem::exists(device) == false)
        {
            atlasagent::Logger()->error("Device path: {} not valid in config file {}", device, configFilePath);
            return std::nullopt;
        }
        devicePaths.emplace_back(device);
    }
    return devicePaths;
}
catch (const std::exception& e)
{
    atlasagent::Logger()->error("Exception: {} in parse_regex_config_file", e.what());
    return std::nullopt;
}

std::optional<std::unordered_set<std::string>> parse_ebs_config_directory(const char* directoryPath)
try
{
    // Check if the directory exists and is a directory
    if (std::filesystem::exists(directoryPath) == false || std::filesystem::is_directory(directoryPath) == false)
    {
        atlasagent::Logger()->error("Invalid service ebs config directory {}", directoryPath);
        return std::nullopt;
    }

    std::regex configFileExtPattern(EBSConstants::ConfigFileExtPattern);
    std::unordered_set<std::string> allDevices{};

    // Iterate through all files in the config directory, but do not process them if they do not match the service
    // monitoring config regex pattern ".ebs-devices"
    for (const auto& file : std::filesystem::recursive_directory_iterator(directoryPath))
    {
        if (std::regex_match(file.path().filename().string(), configFileExtPattern) == false)
        {
            continue;
        }

        // Read all the devices listed in the config file
        auto devicePaths = ebs_parse_regex_config_file(file.path().c_str());
        if (devicePaths.has_value() == false)
        {
            atlasagent::Logger()->error("Could not add devices from config file {}", file.path().c_str());
            continue;
        }

        // Insert each device path individually into the set
        for (const auto& devicePath : devicePaths.value())
        {
            allDevices.insert(devicePath);
        }
    }

    // If no devices are to be monitored, log the error and return nullopt
    if (allDevices.empty())
    {
        atlasagent::Logger()->info("No ebs regex patterns found in directory {}", directoryPath);
        return std::nullopt;
    }

    return allDevices;
}
catch (const std::exception& e)
{
    atlasagent::Logger()->error("Exception: {} in parse_service_monitor_config_directory", e.what());
    return std::nullopt;
}

EBSCollector::EBSCollector(Registry* registry, const std::unordered_set<std::string>& config)
    : config{config}, registry_{registry}
{
}

bool EBSCollector::query_stats_from_device(const std::string& device, nvme_get_amzn_stats_logpage& stats)
try
{
    nvme_admin_command admin_cmd = {};
    admin_cmd.opcode = NVMeCommands::GetLogPage;
    admin_cmd.addr = (uint64_t)&stats;
    admin_cmd.alen = sizeof(stats);
    admin_cmd.nsid = 1;
    admin_cmd.cdw10 = NVMeCommands::StatsLogPageId | (1024 << 16);

    int fd = open(device.c_str(), O_RDONLY);
    if (fd == -1)
    {
        std::error_code ec(errno, std::system_category());
        atlasagent::Logger()->error("Failed to open device {}: {}", device, ec.message());
        return false;
    }

    int ret = ioctl(fd, NVMeCommands::AdminCommand, &admin_cmd);
    close(fd);

    if (ret < 0)
    {
        std::error_code ec(errno, std::system_category());
        atlasagent::Logger()->error("Failed to call ioctl on device {}: {}", device, ec.message());
        return false;
    }

    if (stats._magic != NVMeCommands::StatsMagic)
    {
        atlasagent::Logger()->error("Not an EBS device: {}", device);
        return false;
    }

    return true;
}
catch (const std::exception& e)
{
    atlasagent::Logger()->error("Exception: {} in parse_service_monitor_config_directory", e.what());
    return false;
}

bool EBSCollector::handle_histogram(const ebs_nvme_histogram& histogram, const std::string& devicePath,
                                    const std::string& type)
{
    if (histogram.num_bins > AtlasNamingConvention.size())
    {
        atlasagent::Logger()->error("Histogram has more bins than expected: {} > {}", histogram.num_bins,
                                    AtlasNamingConvention.size());
        return false;
    }
    for (uint64_t i = 0; i < histogram.num_bins; i++)
    {
        ebsHistogram(registry_, EBSMC::ebsHistogram, devicePath, type, AtlasNamingConvention.at(i))
            .Set(histogram.bins[i].count);
    }
    return true;
}

bool EBSCollector::update_metrics(const std::string& devicePath, const nvme_get_amzn_stats_logpage& stats)
{
    if (this->registry_ == nullptr)
    {
        return false;
    }

    ebsMonocounter(registry_, EBSMC::ebsOperations, devicePath, EBSMC::ReadOp).Set(stats.total_read_ops);
    ebsMonocounter(registry_, EBSMC::ebsOperations, devicePath, EBSMC::WriteOp).Set(stats.total_write_ops);

    ebsMonocounter(registry_, EBSMC::ebsBytes, devicePath, EBSMC::ReadOp).Set(stats.total_read_bytes);
    ebsMonocounter(registry_, EBSMC::ebsBytes, devicePath, EBSMC::WriteOp).Set(stats.total_write_bytes);

    ebsMonocounter(registry_, EBSMC::ebsTime, devicePath, EBSMC::ReadOp)
        .Set(stats.total_read_time * EBSMC::ebsMicrosecondsToSeconds);
    ebsMonocounter(registry_, EBSMC::ebsTime, devicePath, EBSMC::WriteOp)
        .Set(stats.total_write_time * EBSMC::ebsMicrosecondsToSeconds);

    ebsMonocounter(registry_, EBSMC::ebsIOPS, devicePath, EBSMC::Volume)
        .Set(stats.ebs_volume_performance_exceeded_iops * EBSMC::ebsMicrosecondsToSeconds);
    ebsMonocounter(registry_, EBSMC::ebsIOPS, devicePath, EBSMC::Instance)
        .Set(stats.ec2_instance_ebs_performance_exceeded_iops * EBSMC::ebsMicrosecondsToSeconds);

    ebsMonocounter(registry_, EBSMC::ebsTP, devicePath, EBSMC::Volume)
        .Set(stats.ebs_volume_performance_exceeded_tp * EBSMC::ebsMicrosecondsToSeconds);
    ebsMonocounter(registry_, EBSMC::ebsTP, devicePath, EBSMC::Instance)
        .Set(stats.ec2_instance_ebs_performance_exceeded_tp * EBSMC::ebsMicrosecondsToSeconds);

    ebsGauge(registry_, EBSMC::ebsQueueLength, devicePath).Set(stats.volume_queue_length);

    bool success{true};
    if (false == handle_histogram(stats.read_io_latency_histogram, devicePath, EBSMC::ReadOp))
    {
        atlasagent::Logger()->error("Failed to handle read histogram for device {}", devicePath);
        success = false;
    }

    if (false == handle_histogram(stats.write_io_latency_histogram, devicePath, EBSMC::WriteOp))
    {
        atlasagent::Logger()->error("Failed to handle write histogram for device {}", devicePath);
        success = false;
    }

    return success;
}

bool EBSCollector::gather_metrics()
{
    bool success{true};
    // Iterate through all the devices in the config
    for (const auto& device : config)
    {
        // Gather statistics for each device
        nvme_get_amzn_stats_logpage stats{};
        if (false == query_stats_from_device(device, stats))
        {
            atlasagent::Logger()->error("Failed to query stats from device {}", device);
            success = false;
            continue;
        }
        // Push the metrics to spectatorD
        if (update_metrics(device, stats) == false)
        {
            atlasagent::Logger()->error("Failed to update metrics for device {}", device);
            success = false;
        }
    }
    return success;
}