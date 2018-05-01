#include "cgroup.h"
#include "atlas-helpers.h"
#include "logger.h"
#include "util.h"
#include <map>

namespace atlasagent {

void CGroup::cpu_shares() noexcept {
  auto shares = read_num_from_file(path_prefix_, "cpu/cpu.shares");
  if (shares >= 0) {
    gauge(registry_, "cgroup.cpu.shares")->Update(static_cast<double>(shares));
  }

  // attempt to use an environment variable to set the processing capacity
  // falling back to shares if needed
  char* num_cpu = std::getenv("TITUS_NUM_CPU");
  double n = 0.0;
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
    gauge(registry_, "cgroup.cpu.processingCapacity")->Update(n);
  }
}

constexpr int64_t NANOS = 1000 * 1000 * 1000ll;
void CGroup::cpu_processing_time() noexcept {
  static auto counter = monotonic_counter(registry_, "cgroup.cpu.processingTime");

  auto time_nanos = read_num_from_file(path_prefix_, "cpuacct/cpuacct.usage");
  if (time_nanos >= 0) {
    counter->Set(time_nanos / NANOS);
  }
}

void CGroup::cpu_usage_time() noexcept {
  using atlas::meter::Tags;
  static auto user_usage =
      monotonic_counter(registry_, "cgroup.cpu.usageTime", Tags{{"id", "user"}});
  static auto system_usage =
      monotonic_counter(registry_, "cgroup.cpu.usageTime", Tags{{"id", "system"}});
  std::unordered_map<std::string, int64_t> stats;
  parse_kv_from_file(path_prefix_, "cpuacct/cpuacct.stat", &stats);
  for (const auto& kv : stats) {
    // round up since monotonic counter takes an int64_t not a double
    auto time_in_seconds = (kv.second + 50) / 100;
    if (kv.first == "user") {
      user_usage->Set(time_in_seconds);
    } else if (kv.first == "system") {
      system_usage->Set(time_in_seconds);
    }
  }
}

using atlas::meter::Tags;

void CGroup::kmem_stats() noexcept {
  static auto kmem_fail_cnt = monotonic_counter(registry_, "cgroup.kmem.failures");
  static auto tcp_fail_cnt = monotonic_counter(registry_, "cgroup.kmem.tcpFailures");

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
    gauge(registry_, "cgroup.kmem.used")->Update(usage_bytes);
  }
  auto limit_bytes = read_num_from_file(path_prefix_, "memory/memory.kmem.limit_in_bytes");
  if (limit_bytes >= 0) {
    gauge(registry_, "cgroup.kmem.limit")->Update(limit_bytes);
  }
  auto max_usage_bytes = read_num_from_file(path_prefix_, "memory/memory.kmem.max_usage_in_bytes");
  if (max_usage_bytes >= 0) {
    gauge(registry_, "cgroup.kmem.maxUsed")->Update(max_usage_bytes);
  }

  auto tcp_usage_bytes = read_num_from_file(path_prefix_, "memory/memory.kmem.tcp.usage_in_bytes");
  if (tcp_usage_bytes >= 0) {
    gauge(registry_, "cgroup.kmem.tcpUsed")->Update(tcp_usage_bytes);
  }
  auto tcp_limit_bytes = read_num_from_file(path_prefix_, "memory/memory.kmem.tcp.limit_in_bytes");
  if (tcp_limit_bytes >= 0) {
    gauge(registry_, "cgroup.kmem.tcpLimit")->Update(tcp_limit_bytes);
  }
  auto tcp_max_usage_bytes =
      read_num_from_file(path_prefix_, "memory/memory.kmem.tcp.max_usage_in_bytes");
  if (max_usage_bytes >= 0) {
    gauge(registry_, "cgroup.kmem.tcpMaxUsed")->Update(tcp_max_usage_bytes);
  }
}

void CGroup::memory_stats() noexcept {
  static auto mem_fail_cnt = monotonic_counter(registry_, "cgroup.mem.failures");
  static auto usage_cache_gauge =
      gauge(registry_, "cgroup.mem.processUsage", Tags{{"id", "cache"}});
  static auto usage_rss_gauge = gauge(registry_, "cgroup.mem.processUsage", Tags{{"id", "rss"}});
  static auto usage_rss_huge_gauge =
      gauge(registry_, "cgroup.mem.processUsage", Tags{{"id", "rss_huge"}});
  static auto usage_mapped_file_gauge =
      gauge(registry_, "cgroup.mem.processUsage", Tags{{"id", "mapped_file"}});
  static auto minor_page_faults =
      monotonic_counter(registry_, "cgroup.mem.pageFaults", Tags{{"id", "minor"}});
  static auto major_page_faults =
      monotonic_counter(registry_, "cgroup.mem.pageFaults", Tags{{"id", "major"}});

  auto usage_bytes = read_num_from_file(path_prefix_, "memory/memory.usage_in_bytes");
  if (usage_bytes >= 0) {
    gauge(registry_, "cgroup.mem.used")->Update(usage_bytes);
  }
  auto limit_bytes = read_num_from_file(path_prefix_, "memory/memory.limit_in_bytes");
  if (limit_bytes >= 0) {
    gauge(registry_, "cgroup.mem.limit")->Update(limit_bytes);
  }
  auto mem_fail = read_num_from_file(path_prefix_, "memory/memory.failcnt");
  if (mem_fail >= 0) {
    mem_fail_cnt->Set(mem_fail);
  }

  kmem_stats();

  std::unordered_map<std::string, int64_t> stats;
  parse_kv_from_file(path_prefix_, "memory/memory.stat", &stats);

  usage_cache_gauge->Update(stats["total_cache"]);
  usage_rss_gauge->Update(stats["total_rss"]);
  usage_rss_huge_gauge->Update(stats["total_rss_huge"]);
  usage_mapped_file_gauge->Update(stats["total_mapped_file"]);
  minor_page_faults->Set(stats["total_pgfault"]);
  major_page_faults->Set(stats["total_pgmajfault"]);
}

void CGroup::cpu_stats() noexcept {
  cpu_processing_time();
  cpu_usage_time();
  cpu_shares();
}

CGroup::CGroup(atlas::meter::Registry* registry, std::string path_prefix) noexcept
    : registry_(registry), path_prefix_(std::move(path_prefix)) {}

}  // namespace atlasagent
