#pragma once

#include "tagging_registry.h"

namespace atlasagent {

template <typename Reg = TaggingRegistry>
class CGroup {
 public:
  explicit CGroup(Reg* registry, std::string path_prefix = "/sys/fs/cgroup",
                  absl::Duration update_interval = absl::Seconds(60)) noexcept
      : registry_(registry),
        path_prefix_(std::move(path_prefix)),
        update_interval_{update_interval} {}

  void cpu_stats(bool is_cgroup2) noexcept { do_cpu_stats(absl::Now(), is_cgroup2); }
  void cpu_peak_stats(bool is_cgroup2) noexcept { do_cpu_peak_stats(absl::Now(), is_cgroup2); }
  void memory_stats_v1() noexcept;
  void memory_stats_v2() noexcept;
  void memory_stats_std_v1() noexcept;
  void memory_stats_std_v2() noexcept;
  void network_stats() noexcept;
  void set_prefix(std::string new_prefix) noexcept { path_prefix_ = std::move(new_prefix); }

 private:
  Reg* registry_;
  std::string path_prefix_;
  absl::Duration update_interval_;
  double user_hz_{sysconf(_SC_CLK_TCK) * 1.0};

  void cpu_processing_time_v1() noexcept;
  void cpu_processing_time_v2() noexcept;
  void cpu_shares_v1(absl::Time now) noexcept;
  void cpu_shares_v2(absl::Time now) noexcept;
  void cpu_throttle_v1() noexcept;
  void cpu_throttle_v2() noexcept;
  void cpu_usage_time_v1() noexcept;
  void cpu_usage_time_v2() noexcept;
  void cpu_utilization_v1(absl::Time now) noexcept;
  void cpu_utilization_v2(absl::Time now) noexcept;
  void cpu_peak_utilization_v1(absl::Time now) noexcept;
  void cpu_peak_utilization_v2(absl::Time now) noexcept;
  void kmem_stats_v1() noexcept;

 protected:
  // for testing
  void do_cpu_stats(absl::Time now, bool is_cgroup2) noexcept;
  void do_cpu_peak_stats(absl::Time now, bool is_cgroup2) noexcept;
};

}  // namespace atlasagent

#include "internal/cgroup.inc"
