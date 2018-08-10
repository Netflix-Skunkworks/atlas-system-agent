#include "cgroup.h"
#include "atlas-helpers.h"
#include "logger.h"
#include "util.h"
#include <atlas/meter/id.h>
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

constexpr auto NANOS = 1000 * 1000 * 1000.0;
void CGroup::cpu_processing_time() noexcept {
  using atlas::meter::Tags;
  static int64_t prev = 0;
  static std::shared_ptr<atlas::meter::DCounter> counter = nullptr;

  auto time_nanos = read_num_from_file(path_prefix_, "cpuacct/cpuacct.usage");
  if (prev == 0) {
    counter = registry_->dcounter(
        registry_->CreateId("cgroup.cpu.processingTime", Tags{{"statistic", "count"}}));
  } else {
    counter->Add((time_nanos - prev) / NANOS);
  }
  prev = time_nanos;
}

void CGroup::cpu_usage_time() noexcept {
  using atlas::meter::Tags;
  static auto prev_user_usage = static_cast<int64_t>(-1);
  static auto prev_sys_usage = static_cast<int64_t>(-1);
  static auto user_usage =
      registry_->dcounter(registry_->CreateId("cgroup.cpu.usageTime", Tags{{"id", "user"}}));
  static auto system_usage =
      registry_->dcounter(registry_->CreateId("cgroup.cpu.usageTime", Tags{{"id", "system"}}));

  std::unordered_map<std::string, int64_t> stats;
  parse_kv_from_file(path_prefix_, "cpuacct/cpuacct.stat", &stats);
  // the values are reported in hertz (100 per second)
  for (const auto& kv : stats) {
    if (kv.first == "user") {
      if (prev_user_usage >= 0) {
        auto secs = (kv.second - prev_user_usage) / 100.0;
        user_usage->Add(secs);
      }
      prev_user_usage = kv.second;
    } else if (kv.first == "system") {
      if (prev_sys_usage >= 0) {
        auto secs = (kv.second - prev_sys_usage) / 100.0;
        system_usage->Add(secs);
      }
      prev_sys_usage = kv.second;
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

void CGroup::cpu_throttle() noexcept {
  static auto nr_throttled = monotonic_counter(registry_, "cgroup.cpu.numThrottled");
  static auto throttled_time_ctr = registry_->dcounter(
      registry_->CreateId("cgroup.cpu.throttledTime", atlas::meter::kEmptyTags));
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

void CGroup::cpu_stats() noexcept {
  cpu_processing_time();
  cpu_usage_time();
  cpu_shares();
  cpu_throttle();
}

CGroup::CGroup(atlas::meter::Registry* registry, std::string path_prefix) noexcept
    : registry_(registry), path_prefix_(std::move(path_prefix)) {}

}  // namespace atlasagent
