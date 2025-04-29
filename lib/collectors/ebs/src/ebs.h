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
  "B_0000_0001",
  "B_0001_0002",
  "B_0002_0004",
  "B_0004_0008",
  "B_0008_0016",
  "B_0016_0032",
  "B_0032_0064",
  "B_0064_0128",
  "B_0128_0256",
  "B_0256_0512",
  "B_0512_1024",
  "KB_0001_0002",
  "KB_0002_0004",
  "KB_0004_0008",
  "KB_0008_0016",
  "KB_0016_0032",
  "KB_0032_0064",
  "KB_0064_0128",
  "KB_0128_0256",
  "KB_0256_0512",
  "KB_0512_1024",
  "MB_001_002",
  "MB_002_004",
  "MB_004_008",
  "MB_008_016",
  "MB_016_032",
  "MB_032_064",
  "MB_064_MAX"
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
  inline auto ebsHistogram(Reg* registry, const char* name, const char* deviceName, const char *id, const char *bin) {
    auto tags = spectator::Tags{{"dev", fmt::format("{}", deviceName)}};
    if (id != nullptr) {
      tags.add("id", id);
    }
    if (bin != nullptr) {
      tags.add("bin", bin);
    }
    return registry->GetMonotonicCounter(name, tags);
  }
}  // namespace detail
  

template <typename Reg = atlasagent::TaggingRegistry>
class EBSCollector {
 private:
  std::string device;
  // TODO: Change config to a vector to improve performance
  // PreReq: Break collect_system_metrics into more functions
  std::unordered_set<std::string> config;
  Reg* registry_;
  bool query_stats_from_device(const std::string& device, nvme_get_amzn_stats_logpage& stats);
  bool update_metrics(const std::string &devicePath, const nvme_get_amzn_stats_logpage &stats);

 public:
 EBSCollector(Reg* registry, std::unordered_set<std::string> config);

  void print_stats(const nvme_get_amzn_stats_logpage& stats, int sample_num = -1);

  void print_histogram(const ebs_nvme_histogram& histogram);

  bool handleHistogram(const ebs_nvme_histogram& histogram, const std::string& devicePath, const std::string& id);

  bool gather_metrics();
};

struct EBSConstants {
  static constexpr auto ConfigPath{"/etc/atlas-system-agent/conf.d"};
  static constexpr auto ConfigFileExtPattern = ".*\\.ebs-devices$";
};

std::optional<std::vector<std::string>> ebs_parse_regex_config_file(const char* configFilePath);
std::optional<std::unordered_set<std::string>> parse_ebs_config_directory(const char* directoryPath);