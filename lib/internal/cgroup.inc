#include "../util.h"
#include <cstdlib>
#include <map>
#include <unistd.h>

namespace atlasagent {

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
void CGroup<Reg>::cpu_shares(absl::Time now) noexcept {
  static absl::Time last_updated;
  auto shares = read_num_from_file(path_prefix_, "cpu/cpu.shares");
  // attempt to use an environment variable to set the processing capacity
  // falling back to shares if needed
  auto num_cpu = std::getenv("TITUS_NUM_CPU");
  auto n = 0.0;
  if (num_cpu != nullptr) {
    n = strtod(num_cpu, nullptr);
    if (n <= 0) {
      Logger()->info("Unable to fetch processing capacity from env var. [{}]", num_cpu);
    }
  }
  if (n <= 0) {
    n = shares / 100.0;
  }
  if (n > 0) {
    if (last_updated == absl::UnixEpoch()) {
      last_updated = now - update_interval_;
    }
    auto delta_t = absl::ToDoubleSeconds(now - last_updated);
    last_updated = now;
    registry_->GetCounter("cgroup.cpu.processingCapacity")->Add(delta_t * n);
  }
  if (shares >= 0) {
    registry_->GetGauge("cgroup.cpu.shares")->Set(shares);
  }
}

constexpr auto NANOS = 1000 * 1000 * 1000.0;
template <typename Reg>
void CGroup<Reg>::cpu_processing_time() noexcept {
  static int64_t prev = 0;
  static auto counter = registry_->GetCounter("cgroup.cpu.processingTime");

  auto time_nanos = read_num_from_file(path_prefix_, "cpuacct/cpuacct.usage");
  if (prev != 0) {
    counter->Add((time_nanos - prev) / NANOS);
  }
  prev = time_nanos;
}

template <typename Reg>
void CGroup<Reg>::cpu_usage_time() noexcept {
  static auto prev_user_usage = static_cast<int64_t>(-1);
  static auto prev_sys_usage = static_cast<int64_t>(-1);
  static auto user_usage = registry_->GetCounter("cgroup.cpu.usageTime", {{"id", "user"}});
  static auto system_usage = registry_->GetCounter("cgroup.cpu.usageTime", {{"id", "system"}});

  std::unordered_map<std::string, int64_t> stats;
  parse_kv_from_file(path_prefix_, "cpuacct/cpuacct.stat", &stats);
  // cpustat.acct values are reported in USER_HZ (usually, 100 per second)
  for (const auto& kv : stats) {
    if (kv.first == "user") {
      if (prev_user_usage >= 0) {
        auto secs = (kv.second - prev_user_usage) / user_hz_;
        user_usage->Add(secs);
      }
      prev_user_usage = kv.second;
    } else if (kv.first == "system") {
      if (prev_sys_usage >= 0) {
        auto secs = (kv.second - prev_sys_usage) / user_hz_;
        system_usage->Add(secs);
      }
      prev_sys_usage = kv.second;
    }
  }
}

template <typename Reg>
void CGroup<Reg>::cpu_utilization(absl::Time now) noexcept {
  static absl::Time last_updated;

  auto delta_t = absl::ToDoubleSeconds(now - last_updated);
  last_updated = now;

  static auto num_procs = registry_->GetGauge("sys.cpu.numProcessors");

  auto cfs_quota = read_num_from_file(path_prefix_, "cpuacct/cpu.cfs_quota_us");
  auto cfs_period = read_num_from_file(path_prefix_, "cpuacct/cpu.cfs_period_us");

  num_procs->Set(cfs_quota / cfs_period);

  static auto cpu_user = registry_->GetGauge("sys.cpu.utilization", {{"id", "user"}});
  static auto cpu_system = registry_->GetGauge("sys.cpu.utilization", {{"id", "system"}});

  static auto prev_user_time = static_cast<int64_t>(-1);
  static auto prev_system_time = static_cast<int64_t>(-1);

  auto system_time = read_num_from_file(path_prefix_, "cpuacct/cpuacct.usage_sys");
  auto user_time = read_num_from_file(path_prefix_, "cpuacct/cpuacct.usage_user");
  auto avail_cpu_time = (delta_t / cfs_period) * cfs_quota;

  if (prev_system_time >= 0) {
    auto secs = (system_time - prev_system_time) / NANOS;
    cpu_system->Set((secs / avail_cpu_time) * 100);
  }
  prev_system_time = system_time;

  if (prev_user_time >= 0) {
    auto secs = (user_time - prev_user_time) / NANOS;
    cpu_user->Set((secs / avail_cpu_time) * 100);
  }
  prev_user_time = user_time;
}

template <typename Reg>
void CGroup<Reg>::cpu_peak_utilization(absl::Time now) noexcept {
  static absl::Time last_updated;

  auto delta_t = absl::ToDoubleSeconds(now - last_updated);
  last_updated = now;

  auto cfs_quota = read_num_from_file(path_prefix_, "cpuacct/cpu.cfs_quota_us");
  auto cfs_period = read_num_from_file(path_prefix_, "cpuacct/cpu.cfs_period_us");

  static auto cpu_user = registry_->GetMaxGauge("sys.cpu.peakUtilization", {{"id", "user"}});
  static auto cpu_system = registry_->GetMaxGauge("sys.cpu.peakUtilization", {{"id", "system"}});

  static auto prev_user_time = static_cast<int64_t>(-1);
  static auto prev_system_time = static_cast<int64_t>(-1);

  auto system_time = read_num_from_file(path_prefix_, "cpuacct/cpuacct.usage_sys");
  auto user_time = read_num_from_file(path_prefix_, "cpuacct/cpuacct.usage_user");
  auto avail_cpu_time = (delta_t / cfs_period) * cfs_quota;

  if (prev_system_time >= 0) {
    auto secs = (system_time - prev_system_time) / NANOS;
    cpu_system->Set((secs / avail_cpu_time) * 100);
  }
  prev_system_time = system_time;

  if (prev_user_time >= 0) {
    auto secs = (user_time - prev_user_time) / NANOS;
    cpu_user->Set((secs / avail_cpu_time) * 100);
  }
  prev_user_time = user_time;
}

template <typename Reg>
void CGroup<Reg>::kmem_stats() noexcept {
  static auto kmem_fail_cnt = registry_->GetMonotonicCounter("cgroup.kmem.failures");
  static auto tcp_fail_cnt = registry_->GetMonotonicCounter("cgroup.kmem.tcpFailures");

  auto mem_fail = read_num_from_file(path_prefix_, "memory/memory.kmem.failcnt");
  if (mem_fail >= 0) {
    kmem_fail_cnt->Set(mem_fail);
  }
  auto tcp_mem_fail = read_num_from_file(path_prefix_, "memory/memory.kmem.tcp.failcnt");
  if (tcp_mem_fail >= 0) {
    tcp_fail_cnt->Set(tcp_mem_fail);
  }

  auto usage_bytes = read_num_from_file(path_prefix_, "memory/memory.kmem.usage_in_bytes");
  if (usage_bytes >= 0) {
    registry_->GetGauge("cgroup.kmem.used")->Set(usage_bytes);
  }
  auto limit_bytes = read_num_from_file(path_prefix_, "memory/memory.kmem.limit_in_bytes");
  if (limit_bytes >= 0) {
    registry_->GetGauge("cgroup.kmem.limit")->Set(limit_bytes);
  }
  auto max_usage_bytes = read_num_from_file(path_prefix_, "memory/memory.kmem.max_usage_in_bytes");
  if (max_usage_bytes >= 0) {
    registry_->GetGauge("cgroup.kmem.maxUsed")->Set(max_usage_bytes);
  }

  auto tcp_usage_bytes = read_num_from_file(path_prefix_, "memory/memory.kmem.tcp.usage_in_bytes");
  if (tcp_usage_bytes >= 0) {
    registry_->GetGauge("cgroup.kmem.tcpUsed")->Set(tcp_usage_bytes);
  }
  auto tcp_limit_bytes = read_num_from_file(path_prefix_, "memory/memory.kmem.tcp.limit_in_bytes");
  if (tcp_limit_bytes >= 0) {
    registry_->GetGauge("cgroup.kmem.tcpLimit")->Set(tcp_limit_bytes);
  }
  auto tcp_max_usage_bytes =
      read_num_from_file(path_prefix_, "memory/memory.kmem.tcp.max_usage_in_bytes");
  if (max_usage_bytes >= 0) {
    registry_->GetGauge("cgroup.kmem.tcpMaxUsed")->Set(tcp_max_usage_bytes);
  }
}

template <typename Reg>
void CGroup<Reg>::memory_stats() noexcept {
  static auto mem_fail_cnt = registry_->GetMonotonicCounter("cgroup.mem.failures");
  static auto usage_cache_gauge = registry_->GetGauge("cgroup.mem.processUsage", {{"id", "cache"}});
  static auto usage_rss_gauge = registry_->GetGauge("cgroup.mem.processUsage", {{"id", "rss"}});
  static auto usage_rss_huge_gauge =
      registry_->GetGauge("cgroup.mem.processUsage", {{"id", "rss_huge"}});
  static auto usage_mapped_file_gauge =
      registry_->GetGauge("cgroup.mem.processUsage", {{"id", "mapped_file"}});
  static auto minor_page_faults =
      registry_->GetMonotonicCounter("cgroup.mem.pageFaults", {{"id", "minor"}});
  static auto major_page_faults =
      registry_->GetMonotonicCounter("cgroup.mem.pageFaults", {{"id", "major"}});

  auto usage_bytes = read_num_from_file(path_prefix_, "memory/memory.usage_in_bytes");
  if (usage_bytes >= 0) {
    registry_->GetGauge("cgroup.mem.used")->Set(usage_bytes);
  }
  auto limit_bytes = read_num_from_file(path_prefix_, "memory/memory.limit_in_bytes");
  if (limit_bytes >= 0) {
    registry_->GetGauge("cgroup.mem.limit")->Set(limit_bytes);
  }
  auto mem_fail = read_num_from_file(path_prefix_, "memory/memory.failcnt");
  if (mem_fail >= 0) {
    mem_fail_cnt->Set(mem_fail);
  }

  kmem_stats();

  std::unordered_map<std::string, int64_t> stats;
  parse_kv_from_file(path_prefix_, "memory/memory.stat", &stats);

  usage_cache_gauge->Set(stats["total_cache"]);
  usage_rss_gauge->Set(stats["total_rss"]);
  usage_rss_huge_gauge->Set(stats["total_rss_huge"]);
  usage_mapped_file_gauge->Set(stats["total_mapped_file"]);
  minor_page_faults->Set(stats["total_pgfault"]);
  major_page_faults->Set(stats["total_pgmajfault"]);
}

template <typename Reg>
void CGroup<Reg>::memory_stats_std() noexcept {
  static auto avail_real = registry_->GetGauge("mem.availReal");
  static auto free_real = registry_->GetGauge("mem.freeReal");
  static auto total_real = registry_->GetGauge("mem.totalReal");
  static auto avail_swap = registry_->GetGauge("mem.availSwap");
  static auto total_swap = registry_->GetGauge("mem.totalSwap");
  static auto cached = registry_->GetGauge("mem.cached");
  static auto shared = registry_->GetGauge("mem.shared");
  static auto total_free = registry_->GetGauge("mem.totalFree");

  auto mem_limit = read_num_from_file(path_prefix_, "memory/memory.limit_in_bytes");
  auto mem_usage = read_num_from_file(path_prefix_, "memory/memory.usage_in_bytes");
  auto memsw_limit = read_num_from_file(path_prefix_, "memory/memory.memsw.limit_in_bytes");
  auto memsw_usage = read_num_from_file(path_prefix_, "memory/memory.memsw.usage_in_bytes");

  std::unordered_map<std::string, int64_t> stats;
  parse_kv_from_file(path_prefix_, "memory/memory.stat", &stats);

  cached->Set(stats["total_cache"]);
  shared->Set(stats["total_shmem"]);

  if (mem_limit >= 0 && mem_usage >= 0) {
    avail_real->Set(mem_limit - mem_usage + stats["total_cache"]);
    free_real->Set(mem_limit - mem_usage);
    total_real->Set(mem_limit);
  }
  if (memsw_limit >= 0 && mem_limit >= 0) {
    avail_swap->Set(memsw_limit - mem_limit - stats["total_swap"]);
    total_swap->Set(memsw_limit - mem_limit);
  }
  if (memsw_limit >= 0 && memsw_usage >= 0) {
    total_free->Set(memsw_limit - memsw_usage);
  }
}

template <typename Reg>
void CGroup<Reg>::cpu_throttle() noexcept {
  static auto nr_throttled = registry_->GetMonotonicCounter("cgroup.cpu.numThrottled");
  static auto throttled_time_ctr = registry_->GetCounter("cgroup.cpu.throttledTime");
  static auto prev_throttled_time = static_cast<int64_t>(-1);

  std::unordered_map<std::string, int64_t> stats;
  parse_kv_from_file(path_prefix_, "cpuacct/cpu.stat", &stats);

  nr_throttled->Set(stats["nr_throttled"]);
  auto throttled_time = stats["throttled_time"];
  if (prev_throttled_time >= 0) {
    auto seconds = (throttled_time - prev_throttled_time) / 1e9;
    throttled_time_ctr->Add(seconds);
  }
  prev_throttled_time = throttled_time;
}

template <typename Reg>
void CGroup<Reg>::do_cpu_stats(absl::Time now) noexcept {
  cpu_processing_time();
  cpu_shares(now);
  cpu_throttle();
  cpu_usage_time();
  cpu_utilization(now);
}

template <typename Reg>
void CGroup<Reg>::do_cpu_peak_stats(absl::Time now) noexcept {
  cpu_peak_utilization(now);
}

}  // namespace atlasagent
