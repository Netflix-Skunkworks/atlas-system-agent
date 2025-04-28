#include "ebs.h"

void EbsStatsCollector::nvme_ioctl(nvme_admin_command& admin_cmd) {
  int fd = open(device.c_str(), O_RDONLY);
  if (fd < 0) {
    throw std::runtime_error("Failed to open device: " + device);
  }

  int ret = ioctl(fd, NVME_IOCTL_ADMIN_CMD, &admin_cmd);
  close(fd);

  if (ret < 0) {
    throw std::runtime_error("Failed to issue nvme cmd: " + std::string(strerror(errno)));
  }
}

nvme_get_amzn_stats_logpage EbsStatsCollector::query_stats_from_device() {
  nvme_get_amzn_stats_logpage stats;
  memset(&stats, 0, sizeof(stats));

  nvme_admin_command admin_cmd = {};
  admin_cmd.opcode = NVME_GET_LOG_PAGE;
  admin_cmd.addr = (uint64_t)&stats;
  admin_cmd.alen = sizeof(stats);
  admin_cmd.nsid = 1;
  admin_cmd.cdw10 = AMZN_NVME_STATS_LOGPAGE_ID | (1024 << 16);

  nvme_ioctl(admin_cmd);

  if (stats._magic != AMZN_NVME_STATS_MAGIC) {
    throw std::runtime_error("Not an EBS device: " + device);
  }

  return stats;
}

std::vector<nvme_get_amzn_stats_logpage> EbsStatsCollector::collect_stats(int count = 1,
                                                                          int interval_ms = 0) {
  std::vector<nvme_get_amzn_stats_logpage> stats_vector;

  for (int i = 0; i < count; ++i) {
    stats_vector.push_back(query_stats_from_device());

    if (i < count - 1 && interval_ms > 0) {
      usleep(interval_ms * 1000);
    }
  }

  return stats_vector;
}

void EbsStatsCollector::print_stats(const nvme_get_amzn_stats_logpage& stats, int sample_num = -1) {
  if (sample_num >= 0) {
    std::cout << "\n===== Sample " << sample_num + 1 << " =====" << std::endl;
  }

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
}

void EbsStatsCollector::print_histogram(const ebs_nvme_histogram& histogram) {
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

void EbsStatsCollector::driver(const std::string& device) {
    // if (argc < 2) {
    //     std::cerr << "Usage: " << argv[0] << " <device>" << std::endl;
    //     return 1;
    // }

    // std::string device = argv[1];

    try {
        EbsStatsCollector collector(device);
        // Collect 3 samples, 1 second apart
        std::vector<nvme_get_amzn_stats_logpage> stats = collector.collect_stats(3, 1000);
        
        std::cout << "Collected " << stats.size() << " stats samples" << std::endl;
        
        // Print all collected samples
        for (size_t i = 0; i < stats.size(); i++) {
            EbsStatsCollector::print_stats(stats[i], i);
        }
        
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return;
    }
}