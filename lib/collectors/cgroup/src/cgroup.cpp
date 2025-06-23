#include "cgroup.h"
#include <lib/util/src/util.h>
#include <cstdlib>
#include <map>
#include <unistd.h>

namespace atlasagent {


constexpr auto MICROS = 1000 * 1000.0;


void CGroup::network_stats() noexcept {
  auto megabits = std::getenv("TITUS_NUM_NETWORK_BANDWIDTH");

  if (megabits != nullptr) {
    auto n = strtol(megabits, nullptr, 10);
    if (n > 0) {
      auto bytes = n * 125000.0;  // 1 megabit = 1,000,000 bits / 8 = 125,000 bytes
      registry_.gauge("cgroup.net.bandwidthBytes").Set(bytes);
    }
  }
}

void CGroup::pressure_stall() noexcept {
  auto lines = read_lines_fields(path_prefix_, "cpu.pressure");

  if (lines.size() == 2) {
    auto usecs = std::strtoul(lines[0][4].substr(6).c_str(), nullptr, 10);
    registry_.monotonic_counter("sys.pressure.some", {{"id", "cpu"}}).Set(usecs / MICROS);

    usecs = std::strtoul(lines[1][4].substr(6).c_str(), nullptr, 10);
    registry_.monotonic_counter("sys.pressure.full", {{"id", "cpu"}}).Set(usecs / MICROS);
  }

  lines = read_lines_fields(path_prefix_, "io.pressure");
  if (lines.size() == 2) {
    auto usecs = std::strtoul(lines[0][4].substr(6).c_str(), nullptr, 10);
    registry_.monotonic_counter("sys.pressure.some", {{"id", "io"}}).Set(usecs / MICROS);

    usecs = std::strtoul(lines[1][4].substr(6).c_str(), nullptr, 10);
    registry_.monotonic_counter("sys.pressure.full", {{"id", "io"}}).Set(usecs / MICROS);
  }

  lines = read_lines_fields(path_prefix_, "memory.pressure");
  if (lines.size() == 2) {
    auto usecs = std::strtoul(lines[0][4].substr(6).c_str(), nullptr, 10);
    registry_.monotonic_counter("sys.pressure.some", {{"id", "memory"}}).Set(usecs / MICROS);

    usecs = std::strtoul(lines[1][4].substr(6).c_str(), nullptr, 10);
    registry_.monotonic_counter("sys.pressure.full", {{"id", "memory"}}).Set(usecs / MICROS);
  }
}


void CGroup::cpu_throttle_v2() noexcept {
  std::unordered_map<std::string, int64_t> stats;
  parse_kv_from_file(path_prefix_, "cpu.stat", &stats);

  static auto prev_throttled_time = static_cast<int64_t>(-1);
  auto cur_throttled_time = stats["throttled_usec"];
  if (prev_throttled_time >= 0) {
    auto seconds = (cur_throttled_time - prev_throttled_time) / MICROS;
    registry_.counter("cgroup.cpu.throttledTime").Increment(seconds);
  }
  prev_throttled_time = cur_throttled_time;

  registry_.monotonic_counter("cgroup.cpu.numThrottled").Set(stats["nr_throttled"]);
}


void CGroup::cpu_time_v2() noexcept {
  std::unordered_map<std::string, int64_t> stats;
  parse_kv_from_file(path_prefix_, "cpu.stat", &stats);


  static auto prev_proc_time = static_cast<int64_t>(-1);
  if (prev_proc_time >= 0) {
    auto secs = (stats["usage_usec"] - prev_proc_time) / MICROS;
    registry_.counter("cgroup.cpu.processingTime").Increment(secs);
  }
  prev_proc_time = stats["usage_usec"];

  static auto prev_sys_usage = static_cast<int64_t>(-1);
  if (prev_sys_usage >= 0) {
    auto secs = (stats["system_usec"] - prev_sys_usage) / MICROS;
    registry_.counter("cgroup.cpu.usageTime", {{"id", "system"}}).Increment(secs);
  }
  prev_sys_usage = stats["system_usec"];

  static auto prev_user_usage = static_cast<int64_t>(-1);
  if (prev_user_usage >= 0) {
    auto secs = (stats["user_usec"] - prev_user_usage) / MICROS;
    registry_.counter("cgroup.cpu.usageTime", {{"id", "user"}}).Increment(secs);
  }
  prev_user_usage = stats["user_usec"];
}

double CGroup::get_avail_cpu_time(double delta_t, double num_cpu) noexcept {
  auto cpu_max = read_num_vector_from_file(path_prefix_, "cpu.max");
  auto cfs_period = cpu_max[1];
  auto cfs_quota = cfs_period * num_cpu;
  return (delta_t / cfs_period) * cfs_quota;
}

double CGroup::get_num_cpu() noexcept {
  auto env_num_cpu = std::getenv("TITUS_NUM_CPU");
  auto num_cpu = 0.0;
  if (env_num_cpu != nullptr) {
    num_cpu = strtod(env_num_cpu, nullptr);
  }
  return num_cpu;
}

void CGroup::cpu_utilization_v2(absl::Time now) noexcept {
  static absl::Time last_updated;
  if (last_updated == absl::UnixEpoch()) {
    // ensure cgroup.cpu.processingCapacity has a consistent value after one sample
    last_updated = now - update_interval_;
  }
  auto delta_t = absl::ToDoubleSeconds(now - last_updated);
  last_updated = now;

  auto weight = read_num_from_file(path_prefix_, "cpu.weight");
  if (weight >= 0) {
    registry_.gauge("cgroup.cpu.weight").Set(weight);
  }

  auto num_cpu = get_num_cpu();
  auto avail_cpu_time = get_avail_cpu_time(delta_t, num_cpu);

  registry_.counter("cgroup.cpu.processingTime").Increment(delta_t * num_cpu);
  registry_.gauge("sys.cpu.numProcessors").Set(num_cpu);
  registry_.gauge("titus.cpu.requested").Set(num_cpu);

  std::unordered_map<std::string, int64_t> stats;
  parse_kv_from_file(path_prefix_, "cpu.stat", &stats);

  static auto prev_system_time = static_cast<int64_t>(-1);
  if (prev_system_time >= 0) {
    auto secs = (stats["system_usec"] - prev_system_time) / MICROS;
    registry_.gauge("sys.cpu.utilization", {{"id", "system"}}).Set((secs / avail_cpu_time) * 100);
  }
  prev_system_time = stats["system_usec"];

  static auto prev_user_time = static_cast<int64_t>(-1);
  if (prev_user_time >= 0) {
    auto secs = (stats["user_usec"] - prev_user_time) / MICROS;
    registry_.gauge("sys.cpu.utilization", {{"id", "user"}}).Set((secs / avail_cpu_time) * 100);
  }
  prev_user_time = stats["user_usec"];
}

void CGroup::cpu_peak_utilization_v2(absl::Time now) noexcept {
  static absl::Time last_updated;
  auto delta_t = absl::ToDoubleSeconds(now - last_updated);
  last_updated = now;

  auto num_cpu = get_num_cpu();
  auto avail_cpu_time = get_avail_cpu_time(delta_t, num_cpu);

  std::unordered_map<std::string, int64_t> stats;
  parse_kv_from_file(path_prefix_, "cpu.stat", &stats);

  static auto prev_system_time = static_cast<int64_t>(-1);
  if (prev_system_time >= 0) {
    auto secs = (stats["system_usec"] - prev_system_time) / MICROS;
    registry_.max_gauge("sys.cpu.peakUtilization", {{"id", "system"}}).Set((secs / avail_cpu_time) * 100);
  }
  prev_system_time = stats["system_usec"];

  static auto prev_user_time = static_cast<int64_t>(-1);
  if (prev_user_time >= 0) {
    auto secs = (stats["user_usec"] - prev_user_time) / MICROS;
    registry_.max_gauge("sys.cpu.peakUtilization", {{"id", "user"}}).Set((secs / avail_cpu_time) * 100);
  }
  prev_user_time = stats["user_usec"];
}

void CGroup::memory_stats_v2() noexcept {
  auto usage_bytes = read_num_from_file(path_prefix_, "memory.current");
  if (usage_bytes >= 0) {
    registry_.gauge("cgroup.mem.processUsage").Set(usage_bytes);
  }

  auto limit_bytes = read_num_from_file(path_prefix_, "memory.max");
  if (limit_bytes >= 0) {
    registry_.gauge("cgroup.mem.limit").Set(limit_bytes);
  }

  std::unordered_map<std::string, int64_t> events;
  parse_kv_from_file(path_prefix_, "memory.events", &events);
  auto mem_fail = events["max"];
  if (mem_fail >= 0) {
    registry_.monotonic_counter("cgroup.mem.failures").Set(mem_fail);
  }

  // kmem_stats not available for v2

  std::unordered_map<std::string, int64_t> stats;
  parse_kv_from_file(path_prefix_, "memory.stat", &stats);

  registry_.gauge("cgroup.mem.processUsage", {{"id", "cache"}}).Set(stats["file"]);

  registry_.gauge("cgroup.mem.processUsage", {{"id", "rss"}}).Set(stats["anon"]);

  registry_.gauge("cgroup.mem.processUsage", {{"id", "rss_huge"}}).Set(stats["anon_thp"]);

  registry_.gauge("cgroup.mem.processUsage", {{"id", "mapped_file"}}).Set(stats["file_mapped"]);

  registry_.monotonic_counter("cgroup.mem.pageFaults", {{"id", "minor"}}).Set(stats["pgfault"]);

  registry_.monotonic_counter("cgroup.mem.pageFaults", {{"id", "major"}}).Set(stats["pgmajfault"]);
}

void CGroup::memory_stats_std_v2() noexcept {
  auto mem_limit = read_num_from_file(path_prefix_, "memory.max");
  auto mem_usage = read_num_from_file(path_prefix_, "memory.current");
  auto memsw_limit = read_num_from_file(path_prefix_, "memory.swap.max");
  auto memsw_usage = read_num_from_file(path_prefix_, "memory.swap.current");

  std::unordered_map<std::string, int64_t> stats;
  parse_kv_from_file(path_prefix_, "memory.stat", &stats);


  auto cache = stats["file"];
  registry_.gauge("mem.cached").Set(cache);

  registry_.gauge("mem.shared").Set(stats["shmem"]);


  if (mem_limit >= 0 && mem_usage >= 0) {
    registry_.gauge("mem.availReal").Set(mem_limit - mem_usage + cache);
    registry_.gauge("mem.freeReal").Set(mem_limit - mem_usage);
    registry_.gauge("mem.totalReal").Set(mem_limit);  
  }

  if (memsw_limit >= 0 && memsw_usage >= 0) {
    registry_.gauge("mem.availSwap").Set(memsw_limit - memsw_usage);
    registry_.gauge("mem.totalSwap").Set(memsw_limit);
  }

  if (mem_limit >= 0 && mem_usage >= 0 && memsw_limit >= 0 && memsw_usage >= 0) {
    registry_.gauge("mem.totalFree").Set((mem_limit - mem_usage) + (memsw_limit - memsw_usage) + cache);
  }
}

void CGroup::do_cpu_stats(absl::Time now) noexcept {
  cpu_throttle_v2();
  cpu_time_v2();
  cpu_utilization_v2(now);
}

void CGroup::do_cpu_peak_stats(absl::Time now) noexcept {
  cpu_peak_utilization_v2(now);
}

}  // namespace atlasagent