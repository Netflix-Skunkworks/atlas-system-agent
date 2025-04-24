#include "cgroup.h"
#include <lib/util/src/util.h>
#include <cstdlib>
#include <map>
#include <unistd.h>

namespace atlasagent {

using spectator::Id;
using spectator::Tags;

constexpr auto MICROS = 1000 * 1000.0;

template class CGroup<atlasagent::TaggingRegistry>;
template class CGroup<spectator::TestRegistry>;

template <typename Reg>
void CGroup<Reg>::network_stats() noexcept {
  auto megabits = std::getenv("TITUS_NUM_NETWORK_BANDWIDTH");

  if (megabits != nullptr) {
    auto n = strtol(megabits, nullptr, 10);
    if (n > 0) {
      auto bytes = n * 125000.0;  // 1 megabit = 1,000,000 bits / 8 = 125,000 bytes
      registry_->GetGauge("cgroup.net.bandwidthBytes")->Set(bytes);
    }
  }
}

template <typename Reg>
void CGroup<Reg>::pressure_stall() noexcept {
  auto lines = read_lines_fields(path_prefix_, "cpu.pressure");

  if (lines.size() == 2) {
    auto some = registry_->GetMonotonicCounter(Id::of("sys.pressure.some", Tags{{"id", "cpu"}}));
    auto usecs = std::strtoul(lines[0][4].substr(6).c_str(), nullptr, 10);
    some->Set(usecs / MICROS);

    auto full = registry_->GetMonotonicCounter(Id::of("sys.pressure.full", Tags{{"id", "cpu"}}));
    usecs = std::strtoul(lines[1][4].substr(6).c_str(), nullptr, 10);
    full->Set(usecs / MICROS);
  }

  lines = read_lines_fields(path_prefix_, "io.pressure");
  if (lines.size() == 2) {
    auto some = registry_->GetMonotonicCounter(Id::of("sys.pressure.some", Tags{{"id", "io"}}));
    auto usecs = std::strtoul(lines[0][4].substr(6).c_str(), nullptr, 10);
    some->Set(usecs / MICROS);

    auto full = registry_->GetMonotonicCounter(Id::of("sys.pressure.full", Tags{{"id", "io"}}));
    usecs = std::strtoul(lines[1][4].substr(6).c_str(), nullptr, 10);
    full->Set(usecs / MICROS);
  }

  lines = read_lines_fields(path_prefix_, "memory.pressure");
  if (lines.size() == 2) {
    auto some = registry_->GetMonotonicCounter(Id::of("sys.pressure.some", Tags{{"id", "memory"}}));
    auto usecs = std::strtoul(lines[0][4].substr(6).c_str(), nullptr, 10);
    some->Set(usecs / MICROS);

    auto full = registry_->GetMonotonicCounter(Id::of("sys.pressure.full", Tags{{"id", "memory"}}));
    usecs = std::strtoul(lines[1][4].substr(6).c_str(), nullptr, 10);
    full->Set(usecs / MICROS);
  }
}

template <typename Reg>
void CGroup<Reg>::cpu_throttle_v2() noexcept {
  std::unordered_map<std::string, int64_t> stats;
  parse_kv_from_file(path_prefix_, "cpu.stat", &stats);

  static auto throttled_time = registry_->GetCounter("cgroup.cpu.throttledTime");
  static auto prev_throttled_time = static_cast<int64_t>(-1);
  auto cur_throttled_time = stats["throttled_usec"];
  if (prev_throttled_time >= 0) {
    auto seconds = (cur_throttled_time - prev_throttled_time) / MICROS;
    throttled_time->Add(seconds);
  }
  prev_throttled_time = cur_throttled_time;

  static auto nr_throttled = registry_->GetMonotonicCounter("cgroup.cpu.numThrottled");
  nr_throttled->Set(stats["nr_throttled"]);
}

template <typename Reg>
void CGroup<Reg>::cpu_time_v2() noexcept {
  std::unordered_map<std::string, int64_t> stats;
  parse_kv_from_file(path_prefix_, "cpu.stat", &stats);

  static auto proc_time = registry_->GetCounter("cgroup.cpu.processingTime");
  static auto prev_proc_time = static_cast<int64_t>(-1);
  if (prev_proc_time >= 0) {
    auto secs = (stats["usage_usec"] - prev_proc_time) / MICROS;
    proc_time->Add(secs);
  }
  prev_proc_time = stats["usage_usec"];

  static auto system_usage = registry_->GetCounter("cgroup.cpu.usageTime", {{"id", "system"}});
  static auto prev_sys_usage = static_cast<int64_t>(-1);
  if (prev_sys_usage >= 0) {
    auto secs = (stats["system_usec"] - prev_sys_usage) / MICROS;
    system_usage->Add(secs);
  }
  prev_sys_usage = stats["system_usec"];

  static auto user_usage = registry_->GetCounter("cgroup.cpu.usageTime", {{"id", "user"}});
  static auto prev_user_usage = static_cast<int64_t>(-1);
  if (prev_user_usage >= 0) {
    auto secs = (stats["user_usec"] - prev_user_usage) / MICROS;
    user_usage->Add(secs);
  }
  prev_user_usage = stats["user_usec"];
}

template <typename Reg>
double CGroup<Reg>::get_avail_cpu_time(double delta_t, double num_cpu) noexcept {
  auto cpu_max = read_num_vector_from_file(path_prefix_, "cpu.max");
  auto cfs_period = cpu_max[1];
  auto cfs_quota = cfs_period * num_cpu;
  return (delta_t / cfs_period) * cfs_quota;
}

template <typename Reg>
double CGroup<Reg>::get_num_cpu() noexcept {
  auto env_num_cpu = std::getenv("TITUS_NUM_CPU");
  auto num_cpu = 0.0;
  if (env_num_cpu != nullptr) {
    num_cpu = strtod(env_num_cpu, nullptr);
  }
  return num_cpu;
}

template <typename Reg>
void CGroup<Reg>::cpu_utilization_v2(absl::Time now) noexcept {
  static absl::Time last_updated;
  if (last_updated == absl::UnixEpoch()) {
    // ensure cgroup.cpu.processingCapacity has a consistent value after one sample
    last_updated = now - update_interval_;
  }
  auto delta_t = absl::ToDoubleSeconds(now - last_updated);
  last_updated = now;

  auto weight = read_num_from_file(path_prefix_, "cpu.weight");
  if (weight >= 0) {
    registry_->GetGauge("cgroup.cpu.weight")->Set(weight);
  }

  auto num_cpu = get_num_cpu();
  auto avail_cpu_time = get_avail_cpu_time(delta_t, num_cpu);

  registry_->GetCounter("cgroup.cpu.processingCapacity")->Add(delta_t * num_cpu);
  registry_->GetGauge("sys.cpu.numProcessors")->Set(num_cpu);
  registry_->GetGauge("titus.cpu.requested")->Set(num_cpu);

  std::unordered_map<std::string, int64_t> stats;
  parse_kv_from_file(path_prefix_, "cpu.stat", &stats);

  static auto cpu_system = registry_->GetGauge("sys.cpu.utilization", {{"id", "system"}});
  static auto prev_system_time = static_cast<int64_t>(-1);
  if (prev_system_time >= 0) {
    auto secs = (stats["system_usec"] - prev_system_time) / MICROS;
    cpu_system->Set((secs / avail_cpu_time) * 100);
  }
  prev_system_time = stats["system_usec"];

  static auto cpu_user = registry_->GetGauge("sys.cpu.utilization", {{"id", "user"}});
  static auto prev_user_time = static_cast<int64_t>(-1);
  if (prev_user_time >= 0) {
    auto secs = (stats["user_usec"] - prev_user_time) / MICROS;
    cpu_user->Set((secs / avail_cpu_time) * 100);
  }
  prev_user_time = stats["user_usec"];
}

template <typename Reg>
void CGroup<Reg>::cpu_peak_utilization_v2(absl::Time now) noexcept {
  static absl::Time last_updated;
  auto delta_t = absl::ToDoubleSeconds(now - last_updated);
  last_updated = now;

  auto num_cpu = get_num_cpu();
  auto avail_cpu_time = get_avail_cpu_time(delta_t, num_cpu);

  std::unordered_map<std::string, int64_t> stats;
  parse_kv_from_file(path_prefix_, "cpu.stat", &stats);

  static auto cpu_system = registry_->GetMaxGauge("sys.cpu.peakUtilization", {{"id", "system"}});
  static auto prev_system_time = static_cast<int64_t>(-1);
  if (prev_system_time >= 0) {
    auto secs = (stats["system_usec"] - prev_system_time) / MICROS;
    cpu_system->Set((secs / avail_cpu_time) * 100);
  }
  prev_system_time = stats["system_usec"];

  static auto cpu_user = registry_->GetMaxGauge("sys.cpu.peakUtilization", {{"id", "user"}});
  static auto prev_user_time = static_cast<int64_t>(-1);
  if (prev_user_time >= 0) {
    auto secs = (stats["user_usec"] - prev_user_time) / MICROS;
    cpu_user->Set((secs / avail_cpu_time) * 100);
  }
  prev_user_time = stats["user_usec"];
}

template <typename Reg>
void CGroup<Reg>::memory_stats_v2() noexcept {
  auto usage_bytes = read_num_from_file(path_prefix_, "memory.current");
  if (usage_bytes >= 0) {
    registry_->GetGauge("cgroup.mem.used")->Set(usage_bytes);
  }

  auto limit_bytes = read_num_from_file(path_prefix_, "memory.max");
  if (limit_bytes >= 0) {
    registry_->GetGauge("cgroup.mem.limit")->Set(limit_bytes);
  }

  static auto mem_fail_cnt = registry_->GetMonotonicCounter("cgroup.mem.failures");
  std::unordered_map<std::string, int64_t> events;
  parse_kv_from_file(path_prefix_, "memory.events", &events);
  auto mem_fail = events["max"];
  if (mem_fail >= 0) {
    mem_fail_cnt->Set(mem_fail);
  }

  // kmem_stats not available for v2

  std::unordered_map<std::string, int64_t> stats;
  parse_kv_from_file(path_prefix_, "memory.stat", &stats);

  static auto usage_cache_gauge = registry_->GetGauge("cgroup.mem.processUsage", {{"id", "cache"}});
  usage_cache_gauge->Set(stats["file"]);

  static auto usage_rss_gauge = registry_->GetGauge("cgroup.mem.processUsage", {{"id", "rss"}});
  usage_rss_gauge->Set(stats["anon"]);

  static auto usage_rss_huge_gauge = registry_->GetGauge("cgroup.mem.processUsage", {{"id", "rss_huge"}});
  usage_rss_huge_gauge->Set(stats["anon_thp"]);

  static auto usage_mapped_file_gauge = registry_->GetGauge("cgroup.mem.processUsage", {{"id", "mapped_file"}});
  usage_mapped_file_gauge->Set(stats["file_mapped"]);

  static auto minor_page_faults = registry_->GetMonotonicCounter("cgroup.mem.pageFaults", {{"id", "minor"}});
  minor_page_faults->Set(stats["pgfault"]);

  static auto major_page_faults = registry_->GetMonotonicCounter("cgroup.mem.pageFaults", {{"id", "major"}});
  major_page_faults->Set(stats["pgmajfault"]);
}

template <typename Reg>
void CGroup<Reg>::memory_stats_std_v2() noexcept {
  auto mem_limit = read_num_from_file(path_prefix_, "memory.max");
  auto mem_usage = read_num_from_file(path_prefix_, "memory.current");
  auto memsw_limit = read_num_from_file(path_prefix_, "memory.swap.max");
  auto memsw_usage = read_num_from_file(path_prefix_, "memory.swap.current");

  std::unordered_map<std::string, int64_t> stats;
  parse_kv_from_file(path_prefix_, "memory.stat", &stats);

  static auto cached = registry_->GetGauge("mem.cached");
  auto cache = stats["file"];
  cached->Set(cache);

  static auto shared = registry_->GetGauge("mem.shared");
  auto shmem = stats["shmem"];
  shared->Set(shmem);

  static auto avail_real = registry_->GetGauge("mem.availReal");
  static auto free_real = registry_->GetGauge("mem.freeReal");
  static auto total_real = registry_->GetGauge("mem.totalReal");
  if (mem_limit >= 0 && mem_usage >= 0) {
    avail_real->Set(mem_limit - mem_usage + cache);
    free_real->Set(mem_limit - mem_usage);
    total_real->Set(mem_limit);
  }

  static auto avail_swap = registry_->GetGauge("mem.availSwap");
  static auto total_swap = registry_->GetGauge("mem.totalSwap");
  if (memsw_limit >= 0 && memsw_usage >= 0) {
    avail_swap->Set(memsw_limit - memsw_usage);
    total_swap->Set(memsw_limit);
  }

  static auto total_free = registry_->GetGauge("mem.totalFree");
  if (mem_limit >= 0 && mem_usage >= 0 && memsw_limit >= 0 && memsw_usage >= 0) {
    total_free->Set((mem_limit - mem_usage) + (memsw_limit - memsw_usage) + cache);
  }
}

template <typename Reg>
void CGroup<Reg>::do_cpu_stats(absl::Time now) noexcept {
  cpu_throttle_v2();
  cpu_time_v2();
  cpu_utilization_v2(now);
}

template <typename Reg>
void CGroup<Reg>::do_cpu_peak_stats(absl::Time now) noexcept {
  cpu_peak_utilization_v2(now);
}

}  // namespace atlasagent