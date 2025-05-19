#include "perf_metrics.h"

namespace atlasagent {

template class PerfMetrics<atlasagent::TaggingRegistry>;
template class PerfMetrics<spectator::TestRegistry>;

template <typename Reg>
PerfMetrics<Reg>::PerfMetrics(Reg* registry, const std::string path_prefix)
    : registry_(registry), path_prefix_(std::move(path_prefix)) {
  static constexpr const char* kEnableEnvVar = "ATLAS_ENABLE_PMU_METRICS";
  auto enabled_var = std::getenv(kEnableEnvVar);
  if (enabled_var != nullptr && std::strcmp(enabled_var, "true") == 0) {
    Logger()->info("PMU metrics have been enabled using the env variable {}", kEnableEnvVar);
    disabled_ = false;
  } else {
    Logger()->debug("PMU metrics have not been enabled. Set {}=true to enable collection",
                    kEnableEnvVar);
    return;
  }

  auto is_perf_supported_name = fmt::format("{}/proc/sys/kernel/perf_event_paranoid", path_prefix_);
  if (access(is_perf_supported_name.c_str(), R_OK) != 0) {
    Logger()->warn("Perf is not supported on this system. The file {} is not accessible",
                   is_perf_supported_name);
    disabled_ = true;
    return;
  }

  // start collection
  if (!open_perf_counters_if_needed()) {
    return;
  }

  instructions_ds = registry_->GetDistributionSummary("sys.cpu.instructions");
  cycles_ds = registry_->GetDistributionSummary("sys.cpu.cycles");
  cache_ds = registry_->GetDistributionSummary("sys.cpu.cacheMissRate");
  branch_ds = registry_->GetDistributionSummary("sys.cpu.branchMispredictionRate");
}

template <typename Reg>
bool PerfMetrics<Reg>::open_perf_counters_if_needed() {
  auto new_online_cpus = get_online_cpus();
  if (new_online_cpus == online_cpus_) {
    Logger()->trace("Online CPUs have not changed. No need to reopen perf events.");
    return true;
  }
  online_cpus_ = std::move(new_online_cpus);

  Logger()->info("Online CPUs have changed. Reopening perf events.");
  // if we get EACCES for these then we don't even try the rest
  if (!instructions.open_events(online_cpus_)) {
    disabled_ = true;
    return false;
  }
  cycles.open_events(online_cpus_);
  cache_refs.open_events(online_cpus_);
  cache_misses.open_events(online_cpus_);
  branch_insts.open_events(online_cpus_);
  branch_misses.open_events(online_cpus_);

  return true;
}

template <typename Reg>
std::vector<bool> PerfMetrics<Reg>::get_online_cpus() {
  auto fp = open_file(path_prefix_, "sys/devices/system/cpu/online");
  auto num_cpus = static_cast<size_t>(sysconf(_SC_NPROCESSORS_ONLN));
  std::vector<bool> res;
  if (num_cpus > 0) {
    res.reserve(num_cpus);
  }

  parse_range(fp, &res);

  auto logger = Logger();
  if (logger->should_log(spdlog::level::debug)) {
    auto enabled = std::count(res.begin(), res.end(), true);
    logger->debug("Got online CPUs: {}/{}", enabled, res.size());
  }
  return res;
}

template <typename Reg>
void PerfMetrics<Reg>::collect() {
  if (disabled_) {
    return;
  }

  update_ds(instructions, instructions_ds.get(), "instructions");
  update_ds(cycles, cycles_ds.get(), "cycles");
  update_rate(cache_misses, cache_refs, cache_ds.get(), "cache miss rate");
  update_rate(branch_misses, branch_insts, branch_ds.get(), "branch misprediction rate");

  // refresh online CPUs and reopen perf counters so we can capture when CPUs are disabled
  // after we started running
  open_perf_counters_if_needed();
}

template <typename Reg>
void PerfMetrics<Reg>::update_ds(PerfCounter& a, typename Reg::dist_summary_t* ds,
                                 const char* name) {
  auto a_values = a.read_delta();
  // update our distribution summary with values from each CPU
  for (auto v : a_values) {
    Logger()->trace("Updating {} with {}", name, v);
    ds->Record(v);
  }
}

template <typename Reg>
void PerfMetrics<Reg>::update_rate(PerfCounter& a, PerfCounter& b, typename Reg::dist_summary_t* ds,
                                   const char* name) {
  auto a_values = a.read_delta();
  auto b_values = b.read_delta();
  assert(a_values.size() == b_values.size());

  // compute rate for each core
  for (auto i = 0u; i < a_values.size(); ++i) {
    auto denominator = b_values[i];
    if (denominator == 0) continue;

    auto numerator = a_values[i];
    auto rate = static_cast<double>(numerator) / denominator;
    Logger()->trace("Updating {} with {}/{}={}", name, numerator, denominator, rate);
    ds->Record(rate);
  }
}

}  // namespace atlasagent