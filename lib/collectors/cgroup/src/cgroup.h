#pragma once

#include <thirdparty/spectator-cpp/spectator/registry.h>
#include "absl/time/clock.h"

namespace atlasagent {

class CGroup {
 public:
  explicit CGroup(Registry* registry, std::string path_prefix = "/sys/fs/cgroup",
                  absl::Duration update_interval = absl::Seconds(60)) noexcept
      : registry_(registry),
        path_prefix_(std::move(path_prefix)),
        update_interval_{update_interval} {}

  void cpu_stats() noexcept { do_cpu_stats(absl::Now()); }
  void cpu_peak_stats() noexcept { do_cpu_peak_stats(absl::Now()); }
  void memory_stats_v2() noexcept;
  void memory_stats_std_v2() noexcept;
  void network_stats() noexcept;
  void pressure_stall() noexcept;
  void set_prefix(std::string new_prefix) noexcept { path_prefix_ = std::move(new_prefix); }

 private:
  Registry* registry_;
  std::string path_prefix_;
  absl::Duration update_interval_;

  void cpu_throttle_v2() noexcept;
  void cpu_time_v2() noexcept;
  void cpu_utilization_v2(absl::Time now) noexcept;
  void cpu_peak_utilization_v2(absl::Time now) noexcept;
  double get_avail_cpu_time(double delta_t, double num_cpu) noexcept;
  double get_num_cpu() noexcept;

 protected:
  // for testing
  void do_cpu_stats(absl::Time now) noexcept;
  void do_cpu_peak_stats(absl::Time now) noexcept;
};

}  // namespace atlasagent
