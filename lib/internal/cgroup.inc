#include "../util.h"
#include <cstdlib>
#include <map>
#include <unistd.h>

namespace atlasagent {

using spectator::Id;
using spectator::Tags;

constexpr auto MICROS = 1000 * 1000.0;
constexpr auto NANOS = 1000 * 1000 * 1000.0;

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
void CGroup<Reg>::cpu_shares_v1(absl::Time now) noexcept {
  static absl::Time last_updated;

  auto shares = read_num_from_file(path_prefix_, "cpu/cpu.shares");
  if (shares >= 0) {
    registry_->GetGauge("cgroup.cpu.shares")->Set(shares);
  }

  // use an environment variable to set the processing capacity
  auto num_cpu = std::getenv("TITUS_NUM_CPU");
  auto n = 0.0;
  if (num_cpu != nullptr) {
    n = strtod(num_cpu, nullptr);
    if (n <= 0) {
      Logger()->info("Unable to fetch processing capacity from env var. [{}]", num_cpu);
    } else {
      registry_->GetGauge("titus.cpu.requested")->Set(n);
    }
  }

  if (n <= 0) {
    auto cfs_quota = read_num_from_file(path_prefix_, "cpuacct/cpu.cfs_quota_us");
    auto cfs_period = read_num_from_file(path_prefix_, "cpuacct/cpu.cfs_period_us");
    n = cfs_quota / cfs_period;
  }

  if (n > 0) {
    if (last_updated == absl::UnixEpoch()) {
      last_updated = now - update_interval_;
    }
    auto delta_t = absl::ToDoubleSeconds(now - last_updated);
    last_updated = now;
    registry_->GetCounter("cgroup.cpu.processingCapacity")->Add(delta_t * n);
  }
}

template <typename Reg>
void CGroup<Reg>::cpu_shares_v2(absl::Time now) noexcept {
  static absl::Time last_updated;

  auto weight = read_num_from_file(path_prefix_, "cpu.weight");
  if (weight >= 0) {
    registry_->GetGauge("cgroup.cpu.weight")->Set(weight);
  }

  // use an environment variable to set the processing capacity
  auto num_cpu = std::getenv("TITUS_NUM_CPU");
  auto n = 0.0;
  if (num_cpu != nullptr) {
    n = strtod(num_cpu, nullptr);
    if (n <= 0) {
      Logger()->info("Unable to fetch processing capacity from env var. [{}]", num_cpu);
    } else {
      registry_->GetGauge("titus.cpu.requested")->Set(n);
    }
  }

  if (n <= 0) {
    auto vec = read_num_vector_from_file(path_prefix_, "cpu.max");
    auto cfs_quota = vec[0];
    auto cfs_period = vec[1];
    n = cfs_quota / cfs_period;
  }

  if (n > 0) {
    if (last_updated == absl::UnixEpoch()) {
      last_updated = now - update_interval_;
    }
    auto delta_t = absl::ToDoubleSeconds(now - last_updated);
    last_updated = now;
    registry_->GetCounter("cgroup.cpu.processingCapacity")->Add(delta_t * n);
  }
}

template <typename Reg>
void CGroup<Reg>::cpu_processing_time_v1() noexcept {
  auto time_nanos = read_num_from_file(path_prefix_, "cpuacct/cpuacct.usage");

  static auto counter = registry_->GetCounter("cgroup.cpu.processingTime");
  static int64_t prev = 0;
  if (prev != 0) {
    counter->Add((time_nanos - prev) / NANOS);
  }
  prev = time_nanos;
}

template <typename Reg>
void CGroup<Reg>::cpu_processing_time_v2() noexcept {
  std::unordered_map<std::string, int64_t> stats;
  parse_kv_from_file(path_prefix_, "cpu.stat", &stats);

  static auto counter = registry_->GetCounter("cgroup.cpu.processingTime");
  static int64_t prev = 0;
  if (prev != 0) {
    counter->Add((stats["usage_usec"] - prev) / MICROS);
  }
  prev = stats["usage_usec"];
}

template <typename Reg>
void CGroup<Reg>::cpu_usage_time_v1() noexcept {
  std::unordered_map<std::string, int64_t> stats;
  // cpustat.acct values are reported in USER_HZ (usually, 100 per second)
  parse_kv_from_file(path_prefix_, "cpuacct/cpuacct.stat", &stats);

  static auto system_usage = registry_->GetCounter("cgroup.cpu.usageTime", {{"id", "system"}});
  static auto prev_sys_usage = static_cast<int64_t>(-1);
  if (prev_sys_usage >= 0) {
    auto secs = (stats["system"] - prev_sys_usage) / user_hz_;
    system_usage->Add(secs);
  }
  prev_sys_usage = stats["system"];

  static auto user_usage = registry_->GetCounter("cgroup.cpu.usageTime", {{"id", "user"}});
  static auto prev_user_usage = static_cast<int64_t>(-1);
  if (prev_user_usage >= 0) {
    auto secs = (stats["user"] - prev_user_usage) / user_hz_;
    user_usage->Add(secs);
  }
  prev_user_usage = stats["user"];
}

template <typename Reg>
void CGroup<Reg>::cpu_usage_time_v2() noexcept {
  std::unordered_map<std::string, int64_t> stats;
  parse_kv_from_file(path_prefix_, "cpu.stat", &stats);

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
void CGroup<Reg>::cpu_utilization_v1(absl::Time now) noexcept {
  static absl::Time last_updated;
  auto delta_t = absl::ToDoubleSeconds(now - last_updated);
  last_updated = now;

  static auto num_procs = registry_->GetGauge("sys.cpu.numProcessors");
  auto cfs_quota = read_num_from_file(path_prefix_, "cpuacct/cpu.cfs_quota_us");
  auto cfs_period = read_num_from_file(path_prefix_, "cpuacct/cpu.cfs_period_us");
  auto avail_cpu_time = (delta_t / cfs_period) * cfs_quota;
  num_procs->Set(cfs_quota / cfs_period);

  static auto cpu_system = registry_->GetGauge("sys.cpu.utilization", {{"id", "system"}});
  static auto prev_system_time = static_cast<int64_t>(-1);
  auto system_time = read_num_from_file(path_prefix_, "cpuacct/cpuacct.usage_sys");
  if (prev_system_time >= 0) {
    auto secs = (system_time - prev_system_time) / NANOS;
    cpu_system->Set((secs / avail_cpu_time) * 100);
  }
  prev_system_time = system_time;

  static auto cpu_user = registry_->GetGauge("sys.cpu.utilization", {{"id", "user"}});
  static auto prev_user_time = static_cast<int64_t>(-1);
  auto user_time = read_num_from_file(path_prefix_, "cpuacct/cpuacct.usage_user");
  if (prev_user_time >= 0) {
    auto secs = (user_time - prev_user_time) / NANOS;
    cpu_user->Set((secs / avail_cpu_time) * 100);
  }
  prev_user_time = user_time;
}

template <typename Reg>
void CGroup<Reg>::cpu_utilization_v2(absl::Time now) noexcept {
  static absl::Time last_updated;
  auto delta_t = absl::ToDoubleSeconds(now - last_updated);
  last_updated = now;

  static auto num_procs = registry_->GetGauge("sys.cpu.numProcessors");
  auto vec = read_num_vector_from_file(path_prefix_, "cpu.max");
  auto cfs_quota = vec[0];
  auto cfs_period = vec[1];
  auto avail_cpu_time = (delta_t / cfs_period) * cfs_quota;
  num_procs->Set(cfs_quota / cfs_period);

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
void CGroup<Reg>::cpu_peak_utilization_v1(absl::Time now) noexcept {
  static absl::Time last_updated;
  auto delta_t = absl::ToDoubleSeconds(now - last_updated);
  last_updated = now;

  auto cfs_quota = read_num_from_file(path_prefix_, "cpuacct/cpu.cfs_quota_us");
  auto cfs_period = read_num_from_file(path_prefix_, "cpuacct/cpu.cfs_period_us");
  auto avail_cpu_time = (delta_t / cfs_period) * cfs_quota;

  static auto cpu_system = registry_->GetMaxGauge("sys.cpu.peakUtilization", {{"id", "system"}});
  static auto prev_system_time = static_cast<int64_t>(-1);
  auto system_time = read_num_from_file(path_prefix_, "cpuacct/cpuacct.usage_sys");
  if (prev_system_time >= 0) {
    auto secs = (system_time - prev_system_time) / NANOS;
    cpu_system->Set((secs / avail_cpu_time) * 100);
  }
  prev_system_time = system_time;

  static auto cpu_user = registry_->GetMaxGauge("sys.cpu.peakUtilization", {{"id", "user"}});
  static auto prev_user_time = static_cast<int64_t>(-1);
  auto user_time = read_num_from_file(path_prefix_, "cpuacct/cpuacct.usage_user");
  if (prev_user_time >= 0) {
    auto secs = (user_time - prev_user_time) / NANOS;
    cpu_user->Set((secs / avail_cpu_time) * 100);
  }
  prev_user_time = user_time;
}

template <typename Reg>
void CGroup<Reg>::cpu_peak_utilization_v2(absl::Time now) noexcept {
  static absl::Time last_updated;
  auto delta_t = absl::ToDoubleSeconds(now - last_updated);
  last_updated = now;

  auto vec = read_num_vector_from_file(path_prefix_, "cpu.max");
  auto cfs_quota = vec[0];
  auto cfs_period = vec[1];
  auto avail_cpu_time = (delta_t / cfs_period) * cfs_quota;

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
void CGroup<Reg>::kmem_stats_v1() noexcept {
  static auto kmem_fail_cnt = registry_->GetMonotonicCounter("cgroup.kmem.failures");
  static auto tcp_fail_cnt = registry_->GetMonotonicCounter("cgroup.kmem.tcpFailures");

  // userspace and kernel memory are all counted against the same
  // counters in v2, so there are no independent kmem metrics.

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
void CGroup<Reg>::memory_stats_v1() noexcept {
  auto usage_bytes = read_num_from_file(path_prefix_, "memory/memory.usage_in_bytes");
  if (usage_bytes >= 0) {
    registry_->GetGauge("cgroup.mem.used")->Set(usage_bytes);
  }

  auto limit_bytes = read_num_from_file(path_prefix_, "memory/memory.limit_in_bytes");
  if (limit_bytes >= 0) {
    registry_->GetGauge("cgroup.mem.limit")->Set(limit_bytes);
  }

  static auto mem_fail_cnt = registry_->GetMonotonicCounter("cgroup.mem.failures");
  auto mem_fail = read_num_from_file(path_prefix_, "memory/memory.failcnt");
  if (mem_fail >= 0) {
    mem_fail_cnt->Set(mem_fail);
  }

  kmem_stats_v1();

  std::unordered_map<std::string, int64_t> stats;
  parse_kv_from_file(path_prefix_, "memory/memory.stat", &stats);

  static auto usage_cache_gauge = registry_->GetGauge("cgroup.mem.processUsage", {{"id", "cache"}});
  usage_cache_gauge->Set(stats["total_cache"]);

  static auto usage_rss_gauge = registry_->GetGauge("cgroup.mem.processUsage", {{"id", "rss"}});
  usage_rss_gauge->Set(stats["total_rss"]);

  static auto usage_rss_huge_gauge = registry_->GetGauge("cgroup.mem.processUsage", {{"id", "rss_huge"}});
  usage_rss_huge_gauge->Set(stats["total_rss_huge"]);

  static auto usage_mapped_file_gauge = registry_->GetGauge("cgroup.mem.processUsage", {{"id", "mapped_file"}});
  usage_mapped_file_gauge->Set(stats["total_mapped_file"]);

  static auto minor_page_faults = registry_->GetMonotonicCounter("cgroup.mem.pageFaults", {{"id", "minor"}});
  minor_page_faults->Set(stats["total_pgfault"]);

  static auto major_page_faults = registry_->GetMonotonicCounter("cgroup.mem.pageFaults", {{"id", "major"}});
  major_page_faults->Set(stats["total_pgmajfault"]);
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
void CGroup<Reg>::memory_stats_std_v1() noexcept {
  auto mem_limit = read_num_from_file(path_prefix_, "memory/memory.limit_in_bytes");
  auto mem_usage = read_num_from_file(path_prefix_, "memory/memory.usage_in_bytes");
  auto memsw_limit = read_num_from_file(path_prefix_, "memory/memory.memsw.limit_in_bytes");
  auto memsw_usage = read_num_from_file(path_prefix_, "memory/memory.memsw.usage_in_bytes");

  std::unordered_map<std::string, int64_t> stats;
  parse_kv_from_file(path_prefix_, "memory/memory.stat", &stats);

  static auto cached = registry_->GetGauge("mem.cached");
  auto cache = stats["total_cache"];
  cached->Set(cache);

  static auto shared = registry_->GetGauge("mem.shared");
  auto shmem = stats["total_shmem"];
  shared->Set(shmem);

  auto swap = stats["total_swap"];

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
  if (memsw_limit >= 0 && mem_limit >= 0) {
    avail_swap->Set(memsw_limit - mem_limit - swap);
    total_swap->Set(memsw_limit - mem_limit);
  }

  static auto total_free = registry_->GetGauge("mem.totalFree");
  if (memsw_limit >= 0 && memsw_usage >= 0) {
    total_free->Set(memsw_limit - memsw_usage);
  }
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
void CGroup<Reg>::cpu_throttle_v1() noexcept {
  std::unordered_map<std::string, int64_t> stats;
  parse_kv_from_file(path_prefix_, "cpuacct/cpu.stat", &stats);

  static auto throttled_time = registry_->GetCounter("cgroup.cpu.throttledTime");
  static auto prev_throttled_time = static_cast<int64_t>(-1);
  auto cur_throttled_time = stats["throttled_time"];
  if (prev_throttled_time >= 0) {
    auto seconds = (cur_throttled_time - prev_throttled_time) / NANOS;
    throttled_time->Add(seconds);
  }
  prev_throttled_time = cur_throttled_time;

  static auto nr_throttled = registry_->GetMonotonicCounter("cgroup.cpu.numThrottled");
  nr_throttled->Set(stats["nr_throttled"]);
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
void CGroup<Reg>::do_cpu_stats(absl::Time now, bool is_cgroup2) noexcept {
  if (is_cgroup2) {
    cpu_processing_time_v2();
    cpu_shares_v2(now);
    cpu_throttle_v2();
    cpu_usage_time_v2();
    cpu_utilization_v2(now);
  } else {
    cpu_processing_time_v1();
    cpu_shares_v1(now);
    cpu_throttle_v1();
    cpu_usage_time_v1();
    cpu_utilization_v1(now);
  }
}

template <typename Reg>
void CGroup<Reg>::do_cpu_peak_stats(absl::Time now, bool is_cgroup2) noexcept {
  if (is_cgroup2) {
    cpu_peak_utilization_v2(now);
  } else {
    cpu_peak_utilization_v1(now);
  }
}

}  // namespace atlasagent
