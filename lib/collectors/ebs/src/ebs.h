#include <iostream>
#include <string>
#include <cstring>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <vector>
#include <cstdint>
#include <stdexcept>
#include <iomanip>

#include <lib/tagging/src/tagging_registry.h>
#include <lib/spectator/registry.h>

// Constants for NVMe commands
#define NVME_GET_LOG_PAGE 0x02
#define NVME_IOCTL_ADMIN_CMD 0xC0484E41

#define AMZN_NVME_STATS_LOGPAGE_ID 0xD0
#define AMZN_NVME_STATS_MAGIC 0x3C23B510

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


template <typename Reg = atlasagent::TaggingRegistry>
class EBSCollector {
 private:
  std::string device;

  void nvme_ioctl(nvme_admin_command& admin_cmd);

  nvme_get_amzn_stats_logpage query_stats_from_device();

 public:
  EBSCollector(const std::string& dev) : device(dev) {}

  static void print_stats(const nvme_get_amzn_stats_logpage& stats, int sample_num = -1);

  static void print_histogram(const ebs_nvme_histogram& histogram);

  bool gather_metrics();
};
