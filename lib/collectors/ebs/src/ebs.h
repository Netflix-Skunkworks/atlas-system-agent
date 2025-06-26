#pragma once
#include <thirdparty/spectator-cpp/spectator/registry.h>

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_set>
#include <vector>

// Constants for NVMe commands
struct NVMeCommands {
  static constexpr auto GetLogPage = 0x02;
  static constexpr auto AdminCommand = 0xC0484E41;
  static constexpr auto StatsLogPageId = 0xD0;
  static constexpr auto StatsMagic = 0x3C23B510;
};

// Structures
#pragma pack(push, 1)
struct nvme_admin_command {
  uint8_t opcode;
  uint8_t flags;
  uint16_t cid;
  uint32_t nsid;
  uint64_t _reserved0;
  uint64_t mptr;
  uint64_t addr;
  uint32_t mlen;
  uint32_t alen;
  uint32_t cdw10;
  uint32_t cdw11;
  uint32_t cdw12;
  uint32_t cdw13;
  uint32_t cdw14;
  uint32_t cdw15;
  uint64_t _reserved1;
};

struct nvme_histogram_bin {
  uint64_t lower;
  uint64_t upper;
  uint32_t count;
  uint32_t _reserved0;
};

struct ebs_nvme_histogram {
  uint64_t num_bins;
  nvme_histogram_bin bins[64];
};

struct nvme_get_amzn_stats_logpage {
  uint32_t _magic;
  char _reserved0[4];
  uint64_t total_read_ops;
  uint64_t total_write_ops;
  uint64_t total_read_bytes;
  uint64_t total_write_bytes;
  uint64_t total_read_time;
  uint64_t total_write_time;
  uint64_t ebs_volume_performance_exceeded_iops;
  uint64_t ebs_volume_performance_exceeded_tp;
  uint64_t ec2_instance_ebs_performance_exceeded_iops;
  uint64_t ec2_instance_ebs_performance_exceeded_tp;
  uint64_t volume_queue_length;
  char _reserved1[416];
  ebs_nvme_histogram read_io_latency_histogram;
  ebs_nvme_histogram write_io_latency_histogram;
  char _reserved2[496];
};
#pragma pack(pop)

static const std::vector<std::string> AtlasNamingConvention = {
  "us_00000000_00000001",
  "us_00000001_00000002",
  "us_00000002_00000004",
  "us_00000004_00000008",
  "us_00000008_00000016",
  "us_00000016_00000032",
  "us_00000032_00000064",
  "us_00000064_00000128",
  "us_00000128_00000256",
  "us_00000256_00000512",
  "us_00000512_00001024",
  "us_00001024_00002048",
  "us_00002048_00004096",
  "us_00004096_00008192",
  "us_00008192_00016384",
  "us_00016384_00032768",
  "us_00032768_00065536",
  "us_00065536_00131072",
  "us_00131072_00262144",
  "us_00262144_00524288",
  "us_00524288_01048576",
  "us_01048576_02097152",
  "us_02097152_04194304",
  "us_04194304_08388608",
  "us_08388608_16777216",
  "us_16777216_33554432",
  "us_33554432_67108864",
  "us_67108864_MAX"
};


template <typename Reg>
inline auto ebsGauge(Reg* registry, const std::string_view name, const std::string_view deviceName) {
  std::unordered_map<std::string, std::string> tags = {{"dev", fmt::format("{}", deviceName)}};
  return registry->gauge(std::string(name), tags);
}

template <typename Reg>
inline auto ebsMonocounter(Reg* registry, const std::string_view name, const std::string_view deviceName, const std::string_view id) {
  std::unordered_map<std::string, std::string> tags = {{"dev", fmt::format("{}", deviceName)}, {"id", fmt::format("{}", id)}};
  return registry->monotonic_counter(std::string(name), tags);
}

template <typename Reg>
inline auto ebsHistogram(Reg* registry, const std::string_view name, const std::string_view deviceName, const std::string_view id, const std::string_view bin) {
  std::unordered_map<std::string, std::string> tags = {{"dev", fmt::format("{}", deviceName)}, {"id", fmt::format("{}", id)}, {"bin", fmt::format("{}", bin)}};
  return registry->monotonic_counter(std::string(name), tags);
}
  
class EBSCollector {
 private:
  // TODO: Change config to a vector to improve performance
  // PreReq: Break collect_system_metrics into more functions
  std::unordered_set<std::string> config;
  Registry registry_;
  bool query_stats_from_device(const std::string& device, nvme_get_amzn_stats_logpage& stats);
  bool update_metrics(const std::string &devicePath, const nvme_get_amzn_stats_logpage &stats);
  bool handle_histogram(const ebs_nvme_histogram& histogram, const std::string& devicePath, const std::string& id);

 public:
 EBSCollector(Registry registry, const std::unordered_set<std::string>& config);

  bool gather_metrics();
};

struct EBSConstants {
  static constexpr auto ConfigPath{"/etc/atlas-system-agent/conf.d"};
  static constexpr auto ConfigFileExtPattern = ".*\\.ebs-devices$";
};

std::optional<std::vector<std::string>> ebs_parse_regex_config_file(const char* configFilePath);
std::optional<std::unordered_set<std::string>> parse_ebs_config_directory(const char* directoryPath);