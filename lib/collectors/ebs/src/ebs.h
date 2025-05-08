#include <lib/tagging/src/tagging_registry.h>
#include <lib/spectator/registry.h>

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
  "US_00000000_00000001",
  "US_00000001_00000002",
  "US_00000002_00000004",
  "US_00000004_00000008",
  "US_00000008_00000016",
  "US_00000016_00000032",
  "US_00000032_00000064",
  "US_00000064_00000128",
  "US_00000128_00000256",
  "US_00000256_00000512",
  "US_00000512_00001024",
  "US_00001024_00002048",
  "US_00002048_00004096",
  "US_00004096_00008192",
  "US_00008192_00016384",
  "US_00016384_00032768",
  "US_00032768_00065536",
  "US_00065536_00131072",
  "US_00131072_00262144",
  "US_00262144_00524288",
  "US_00524288_01048576",
  "US_01048576_02097152",
  "US_02097152_04194304",
  "US_04194304_08388608",
  "US_08388608_16777216",
  "US_16777216_33554432",
  "US_33554432_67108864",
  "US_67108864_MAX"
};

namespace detail {
  template <typename Reg>
  inline auto ebsGauge(Reg* registry, const std::string_view name, const std::string_view deviceName) {
    auto tags = spectator::Tags{{"dev", fmt::format("{}", deviceName)}};
    return registry->GetGauge(name, tags);
  }
  
  template <typename Reg>
  inline auto ebsMonocounter(Reg* registry, const std::string_view name, const std::string_view deviceName, const std::string_view id) {
    auto tags = spectator::Tags{{"dev", fmt::format("{}", deviceName)}};
    tags.add("id", id);
    return registry->GetMonotonicCounter(name, tags);
  }

  template <typename Reg>
  inline auto ebsHistogram(Reg* registry, const std::string_view name, const std::string_view deviceName, const std::string_view id, const std::string_view bin) {
    auto tags = spectator::Tags{{"dev", fmt::format("{}", deviceName)}};
    tags.add("id", id);
    tags.add("bin", bin);
    return registry->GetMonotonicCounter(name, tags);
  }
}  // namespace detail
  

template <typename Reg = atlasagent::TaggingRegistry>
class EBSCollector {
 private:
  // TODO: Change config to a vector to improve performance
  // PreReq: Break collect_system_metrics into more functions
  std::unordered_set<std::string> config;
  Reg* registry_;
  bool query_stats_from_device(const std::string& device, nvme_get_amzn_stats_logpage& stats);
  bool update_metrics(const std::string &devicePath, const nvme_get_amzn_stats_logpage &stats);
  bool handle_histogram(const ebs_nvme_histogram& histogram, const std::string& devicePath, const std::string& id);

 public:
 EBSCollector(Reg* registry, const std::unordered_set<std::string>& config);

  bool gather_metrics();
};

struct EBSConstants {
  static constexpr auto ConfigPath{"/etc/atlas-system-agent/conf.d"};
  static constexpr auto ConfigFileExtPattern = ".*\\.ebs-devices$";
};

std::optional<std::vector<std::string>> ebs_parse_regex_config_file(const char* configFilePath);
std::optional<std::unordered_set<std::string>> parse_ebs_config_directory(const char* directoryPath);