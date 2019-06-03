#pragma once

#include <spectator/registry.h>

namespace atlasagent {

class CGroup {
 public:
  using time_point = spectator::Registry::clock::time_point;

  explicit CGroup(spectator::Registry* registry, std::string path_prefix = "/sys/fs/cgroup",
                  std::chrono::seconds update_interval = std::chrono::seconds{60}) noexcept;
  void cpu_stats() noexcept;
  void memory_stats() noexcept;
  void set_prefix(std::string new_prefix) noexcept { path_prefix_ = std::move(new_prefix); }

 private:
  spectator::Registry* registry_;
  std::string path_prefix_;
  std::chrono::seconds update_interval_;
  spectator::Registry::clock::time_point last_updated_{};

  void cpu_processing_time() noexcept;
  void cpu_usage_time() noexcept;
  void cpu_shares(time_point now) noexcept;
  void cpu_throttle() noexcept;
  void kmem_stats() noexcept;

 protected:
  // for testing
  void do_cpu_stats(time_point now) noexcept;
};

}  // namespace atlasagent
