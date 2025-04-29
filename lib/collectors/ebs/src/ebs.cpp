#include "ebs.h"

#include <lib/util/src/util.h>

#include <fcntl.h>
#include <filesystem>
#include <regex>
#include <sys/ioctl.h>
#include <unistd.h>

#include <iomanip> //remove with prints
#include <iostream>

struct EBSMetricConstants {
  // Operation types
  static constexpr auto ReadOp{"read"};
  static constexpr auto WriteOp{"write"};

  // Device types
  static constexpr auto Volume{"volume"};
  static constexpr auto Instance{"instance"};

  // Metric types
  static constexpr auto ebsOperations{"ebs.totalOperations"};
  static constexpr auto ebsBytes{"ebs.totalBytes"};
  static constexpr auto ebsTime{"ebs.totalTime"};
  static constexpr auto ebsIOPS{"ebs.PerformanceExceededIOPS"};
  static constexpr auto ebsTP{"ebs.PerformanceExceededTP"};
  static constexpr auto ebsQueueLength{"ebs.volumeQueueLength"};
  static constexpr auto ebsHistogram{"ebs.IOLatencyHistogram"};
};

template class EBSCollector<atlasagent::TaggingRegistry>;
//template class EBSCollector<spectator::TestRegistry>;

std::optional<std::vector<std::string>> ebs_parse_regex_config_file(const char* configFilePath) try {
  // Read the all the device paths in the config file
  std::optional<std::vector<std::string>> configContents = atlasagent::read_file(configFilePath);
  if (configContents.has_value() == false) {
    atlasagent::Logger()->error("Error reading config file {}", configFilePath);
    return std::nullopt;
  }

  // Skip empty files
  if (configContents.value().empty()) {
    atlasagent::Logger()->debug("Empty config file {}", configFilePath);
    return std::nullopt;
  }

  // Read all the device paths and assert the device exists
  std::vector<std::string> devicePaths{};
  for (const auto& device : configContents.value()) {
    if (std::filesystem::exists(device) == false) {
      atlasagent::Logger()->error("Device path: {} not valid in config file {}", device, configFilePath);
      return std::nullopt;
    }
    devicePaths.emplace_back(device); 
  }
  return devicePaths;
} catch (const std::exception& e) {
  atlasagent::Logger()->error("Exception: {} in parse_regex_config_file", e.what());
  return std::nullopt;
}

std::optional<std::unordered_set<std::string>> parse_ebs_config_directory(const char* directoryPath) try {
  // Check if the directory exists and is a directory
  if (std::filesystem::exists(directoryPath) == false || std::filesystem::is_directory(directoryPath) == false) {
    atlasagent::Logger()->error("Invalid service ebs config directory {}", directoryPath);
    return std::nullopt;
  }

  std::regex configFileExtPattern(EBSConstants::ConfigFileExtPattern);
  std::unordered_set<std::string> allDevices{};

  // Iterate through all files in the config directory, but do not process them if they do not match the service 
  // monitoring config regex pattern ".ebs-devices"
  for (const auto& file : std::filesystem::recursive_directory_iterator(directoryPath)) {
    if (std::regex_match(file.path().filename().string(), configFileExtPattern) == false) {
      continue;
    }

    // Read all the devices listed in the config file
    auto devicePaths = ebs_parse_regex_config_file(file.path().c_str());
    if (devicePaths.has_value() == false) {
      atlasagent::Logger()->error("Could not add devices from config file {}", file.path().c_str());
      continue;
    }
    
    // Insert each device path individually into the set
    for (const auto& devicePath : devicePaths.value()) {
      allDevices.insert(devicePath);
    }
  }

  // If no devices are to be monitored, log the error and return nullopt
  if (allDevices.empty()) {
    atlasagent::Logger()->info("No ebs regex patterns found in directory {}", directoryPath);
    return std::nullopt;
  }
  
  return allDevices;
} catch (const std::exception& e) {
  atlasagent::Logger()->error("Exception: {} in parse_service_monitor_config_directory", e.what());
  return std::nullopt;
}

template <typename Reg>
EBSCollector<Reg>::EBSCollector(Reg* registry, const std::unordered_set<std::string>& config)
    : config{config},
      registry_{registry} {}

template <typename Reg>
bool EBSCollector<Reg>::query_stats_from_device(const std::string& device, nvme_get_amzn_stats_logpage& stats) try {
  nvme_admin_command admin_cmd = {};
  admin_cmd.opcode = NVMeCommands::GetLogPage;
  admin_cmd.addr = (uint64_t)&stats;
  admin_cmd.alen = sizeof(stats);
  admin_cmd.nsid = 1;
  admin_cmd.cdw10 = NVMeCommands::StatsLogPageId | (1024 << 16);

  int fd = open(device.c_str(), O_RDONLY);
  if (fd == -1) {
    std::error_code ec(errno, std::system_category());
    atlasagent::Logger()->error("Failed to open device {}: {}", device, ec.message());
    return false;
  }

  int ret = ioctl(fd, NVMeCommands::AdminCommand, &admin_cmd);
  close(fd);

  if (ret < 0) {
    std::error_code ec(errno, std::system_category());
    atlasagent::Logger()->error("Failed to call ioctl on device {}: {}", device, ec.message());
    return false;
  }

  if (stats._magic != NVMeCommands::StatsMagic) {
    atlasagent::Logger()->error("Not an EBS device: {}", device);
    return false;
  }

  return true;
}
catch (const std::exception& e) {
  atlasagent::Logger()->error("Exception: {} in parse_service_monitor_config_directory", e.what());
  return false;
}

template <class Reg>
void EBSCollector<Reg>::print_stats(const nvme_get_amzn_stats_logpage& stats, int sample_num) {

  std::cout << "Total Ops" << std::endl;
  std::cout << "  Read: " << stats.total_read_ops << std::endl;
  std::cout << "  Write: " << stats.total_write_ops << std::endl;

  std::cout << "Total Bytes" << std::endl;
  std::cout << "  Read: " << stats.total_read_bytes << std::endl;
  std::cout << "  Write: " << stats.total_write_bytes << std::endl;

  std::cout << "Total Time (us)" << std::endl;
  std::cout << "  Read: " << stats.total_read_time << std::endl;
  std::cout << "  Write: " << stats.total_write_time << std::endl;

  std::cout << "EBS Volume Performance Exceeded (us)" << std::endl;
  std::cout << "  IOPS: " << stats.ebs_volume_performance_exceeded_iops << std::endl;
  std::cout << "  Throughput: " << stats.ebs_volume_performance_exceeded_tp << std::endl;

  std::cout << "EC2 Instance EBS Performance Exceeded (us)" << std::endl;
  std::cout << "  IOPS: " << stats.ec2_instance_ebs_performance_exceeded_iops << std::endl;
  std::cout << "  Throughput: " << stats.ec2_instance_ebs_performance_exceeded_tp << std::endl;

  std::cout << "Queue Length (point in time): " << stats.volume_queue_length << std::endl;

  std::cout << "\nRead IO Latency Histogram (us)" << std::endl;
  print_histogram(stats.read_io_latency_histogram);

  std::cout << "\nWrite IO Latency Histogram (us)" << std::endl;
  print_histogram(stats.write_io_latency_histogram);
  std::cout << "\n\n";
}

template <typename Reg>
void EBSCollector<Reg>::print_histogram(const ebs_nvme_histogram& histogram) {
  std::cout << "Number of bins: " << histogram.num_bins << std::endl;
  std::cout << "=================================" << std::endl;
  std::cout << "Lower       Upper        IO Count" << std::endl;
  std::cout << "=================================" << std::endl;

  for (uint64_t i = 0; i < histogram.num_bins; i++) {
    std::cout << "[" << std::left << std::setw(8) << histogram.bins[i].lower << " - " << std::left
              << std::setw(8) << histogram.bins[i].upper << "] => " << histogram.bins[i].count
              << std::endl;
  }
}

template <typename Reg>
bool EBSCollector<Reg>::handleHistogram(const ebs_nvme_histogram& histogram, const std::string& devicePath, const std::string& type) {
  if (histogram.num_bins > AtlasNamingConvention.size()) {
    atlasagent::Logger()->error("Histogram has more bins than expected: {} > {}", histogram.num_bins, AtlasNamingConvention.size());
    return false;
  }
  for (uint64_t i = 0; i < histogram.num_bins; i++) {
    detail::ebsHistogram(registry_, EBSMetricConstants::ebsHistogram, devicePath.c_str(), type.c_str(), AtlasNamingConvention.at(i).c_str())->Set(histogram.bins[i].count);
  }
  return true;
}

template <class Reg>
bool EBSCollector<Reg>::update_metrics(const std::string &devicePath, const nvme_get_amzn_stats_logpage &stats) {
  if (this->registry_ == nullptr) {
    return false;
  }
  
  detail::ebsMonocounter(registry_, EBSMetricConstants::ebsOperations, devicePath, EBSMetricConstants::ReadOp)->Set(stats.total_read_ops);
  detail::ebsMonocounter(registry_, EBSMetricConstants::ebsOperations, devicePath, EBSMetricConstants::WriteOp)->Set(stats.total_write_ops);
  
  detail::ebsMonocounter(registry_, EBSMetricConstants::ebsBytes, devicePath, EBSMetricConstants::ReadOp)->Set(stats.total_read_bytes);
  detail::ebsMonocounter(registry_, EBSMetricConstants::ebsBytes, devicePath, EBSMetricConstants::WriteOp)->Set(stats.total_write_bytes);

  detail::ebsMonocounter(registry_, EBSMetricConstants::ebsTime, devicePath, EBSMetricConstants::ReadOp)->Set(stats.total_read_time * .000001);
  detail::ebsMonocounter(registry_, EBSMetricConstants::ebsTime, devicePath, EBSMetricConstants::WriteOp)->Set(stats.total_write_time * .000001);
  
  detail::ebsMonocounter(registry_, EBSMetricConstants::ebsIOPS, devicePath, EBSMetricConstants::Volume)->Set(stats.ebs_volume_performance_exceeded_iops * .000001);
  detail::ebsMonocounter(registry_, EBSMetricConstants::ebsIOPS, devicePath, EBSMetricConstants::Instance)->Set(stats.ec2_instance_ebs_performance_exceeded_iops * .000001);

  detail::ebsMonocounter(registry_, EBSMetricConstants::ebsTP, devicePath, EBSMetricConstants::Volume)->Set(stats.ebs_volume_performance_exceeded_tp * .000001);
  detail::ebsMonocounter(registry_, EBSMetricConstants::ebsTP, devicePath, EBSMetricConstants::Instance)->Set(stats.ec2_instance_ebs_performance_exceeded_tp * .000001);

  detail::ebsGauge(registry_, EBSMetricConstants::ebsQueueLength, devicePath)->Set(stats.volume_queue_length);
  
  bool success {true};
  if (false == handleHistogram(stats.read_io_latency_histogram, devicePath, EBSMetricConstants::ReadOp)) {
    atlasagent::Logger()->error("Failed to handle read histogram for device {}", devicePath);
    success = false;
  }
  
  if (false == handleHistogram(stats.write_io_latency_histogram, devicePath, EBSMetricConstants::WriteOp)) {
    atlasagent::Logger()->error("Failed to handle write histogram for device {}", devicePath);
    success = false;
  }

  return success;
}

template <typename Reg>
bool EBSCollector<Reg>::gather_metrics() {
  bool success{true};
  // Iterate through all the devices in the config
  for (const auto& device : config) {
    // Gather statistics for each device
    nvme_get_amzn_stats_logpage stats {};
    if (false == query_stats_from_device(device, stats)) {
      atlasagent::Logger()->error("Failed to query stats from device {}", device);
      success = false;
      continue;
    }
    // Push the metrics to spectatorD
    if (update_metrics(device, stats) == false) {
      atlasagent::Logger()->error("Failed to update metrics for device {}", device);
      success = false;
    }
  }
  return success;
}