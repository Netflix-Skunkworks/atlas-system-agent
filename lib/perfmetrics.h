#pragma once

#include <atlas/meter/registry.h>
#include <fmt/format.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include "util.h"

// parse a file with contents like 0-3,5-7,9-23,38 into a vector
// of booleans indicating whether the given index is included
inline void parse_range(FILE* fp, std::vector<bool>* result) {
  if (fp == nullptr) return;

  int cpu, prev;
  char sep;

  prev = -1;
  for (;;) {
    auto n = fscanf(fp, "%d%c", &cpu, &sep);
    if (n <= 0) {
      break;
    }

    if (prev >= 0) {
      result->resize(static_cast<size_t>(cpu + 1));
      while (prev <= cpu) {
        result->at(prev) = true;
        ++prev;
      }
      prev = -1;
    } else {
      prev = cpu;
    }
  }
  if (prev >= 0) {
    result->resize(static_cast<size_t>(prev + 1));
    result->at(prev) = true;
  }
}

#ifdef __linux__
#include <linux/perf_event.h>
#include <linux/hw_breakpoint.h>
inline int perf_event_open(struct perf_event_attr* hw_event, pid_t pid, int cpu, int group_fd,
                           unsigned long flags) {
  return syscall(__NR_perf_event_open, hw_event, pid, cpu, group_fd, flags);
}

#endif
namespace atlasagent {

class PerfCounter {
 public:
  explicit PerfCounter(uint64_t config) : config_(config) {
#ifdef __linux__
    memset(&pea, 0, sizeof pea);
    pea.config = config;
    pea.type = PERF_TYPE_HARDWARE;
    pea.size = sizeof(perf_event_attr);
    pea.exclude_kernel = 1;
    pea.exclude_hv = 1;
#endif
  }

  bool open(const std::vector<bool>& online_cpus) {
    fds_.assign(online_cpus.size(), -1);
    for (auto i = 0u; i < online_cpus.size(); ++i) {
      if (online_cpus[i]) {
#ifdef __linux__
        fds_[i] = perf_event_open(&pea, -1, i, -1, 0);
        if (fds_[i] < 0) {
          if (errno == EACCES) {
            Logger()->warn(
                "Unable to collect performance events - Check "
                "/proc/sys/kernel/perf_event_paranoid");
            return false;
          } else if (errno == ENOENT) {
            Logger()->warn("This system does not allow access to hardware performance counters");
            return false;
          } else {
            Logger()->warn("Unable to perf_event_open CPU {}: {}({})", i, strerror(errno), errno);
          }
        }
#endif
      }
    }
    return true;
  }

  std::vector<uint64_t> read() {
    std::vector<uint64_t> res;
    res.assign(fds_.size(), 0);
    for (auto i = 0u; i < fds_.size(); ++i) {
      auto fd = fds_[i];
      uint64_t value;
      if (fd >= 0) {
        if (::read(fd, &value, sizeof(uint64_t)) == sizeof(uint64_t)) {
          res[i] = value;
        } else {
          Logger()->warn("Unable to read value from CPU {}", i);
        }
      }
      i++;
    }
    return res;
  }

  ~PerfCounter() {
    for (auto fd : fds_) {
      if (fd >= 0) {
        close(fd);
      }
    }
  }

  uint64_t Config() const { return config_; }

 private:
  std::vector<int> fds_;
  uint64_t config_;
#ifdef __linux__
  perf_event_attr pea;
#endif
};

#ifndef __linux__
enum perf_hw_id {
  PERF_COUNT_HW_CPU_CYCLES,
  PERF_COUNT_HW_INSTRUCTIONS,
  PERF_COUNT_HW_CACHE_REFERENCES,
  PERF_COUNT_HW_CACHE_MISSES,
  PERF_COUNT_HW_BRANCH_INSTRUCTIONS,
  PERF_COUNT_HW_BRANCH_MISSES,
};

#endif

class PerfMetrics {
 public:
  PerfMetrics(atlas::meter::Registry* registry, std::string path_prefix)
      : registry_(registry), path_prefix_(std::move(path_prefix)) {
    auto is_perf_supported_name =
        fmt::format("{}/proc/sys/kernel/perf_event_paranoid", path_prefix_);
    if (access(is_perf_supported_name.c_str(), R_OK) != 0) {
      Logger()->warn("Perf is not supported on this system. The file {} is not accessible",
                     is_perf_supported_name);
      disabled_ = true;
      return;
    }

    update_online_cpus();
    // if we get EACCES for these then we don't even try the rest
    if (!cycles.open(online_cpus_) || !instructions.open(online_cpus_)) {
      disabled_ = true;
      return;
    }
    cache_refs.open(online_cpus_);
    cache_misses.open(online_cpus_);
    branch_insts.open(online_cpus_);
    branch_misses.open(online_cpus_);

    ipc_ds = registry_->ddistribution_summary("sys.cpu.instructionsPerCycle");
    cache_ds = registry_->ddistribution_summary("sys.cpu.cacheMissRate");
    branch_ds = registry_->ddistribution_summary("sys.cpu.branchMispredictionRate");
  }

  void update_online_cpus() {
    auto fp = open_file(path_prefix_, "sys/devices/system/cpu/online");
    auto num_cpus = static_cast<size_t>(sysconf(_SC_NPROCESSORS_ONLN));
    if (num_cpus > 0) {
      online_cpus_.reserve(num_cpus);
    }

    parse_range(fp, &online_cpus_);

    auto logger = Logger();
    if (logger->should_log(spdlog::level::debug)) {
      auto enabled = std::count(online_cpus_.begin(), online_cpus_.end(), true);
      logger->debug("Online CPUs: {}/{}", enabled, online_cpus_.size());
    }
  }

  const std::vector<bool>& get_online_cpus() const { return online_cpus_; }

  void collect() {
    if (disabled_) {
      return;
    }

    update_rate(instructions, cycles, ipc_ds.get(), "instructions per cycle");
    update_rate(cache_misses, cache_refs, cache_ds.get(), "cache miss rate");
    update_rate(branch_misses, branch_insts, branch_ds.get(), "branch misprediction rate");
  }

 private:
  bool disabled_ = false;
  atlas::meter::Registry* registry_;
  std::string path_prefix_;
  std::vector<bool> online_cpus_;
  PerfCounter cycles{PERF_COUNT_HW_CPU_CYCLES};
  PerfCounter instructions{PERF_COUNT_HW_INSTRUCTIONS};
  PerfCounter cache_refs{PERF_COUNT_HW_CACHE_REFERENCES};
  PerfCounter cache_misses{PERF_COUNT_HW_CACHE_MISSES};
  PerfCounter branch_insts{PERF_COUNT_HW_BRANCH_INSTRUCTIONS};
  PerfCounter branch_misses{PERF_COUNT_HW_BRANCH_MISSES};

  // instructions per cycle
  std::shared_ptr<atlas::meter::DDistributionSummary> ipc_ds;
  // cache miss rate
  std::shared_ptr<atlas::meter::DDistributionSummary> cache_ds;
  // branch miss rate
  std::shared_ptr<atlas::meter::DDistributionSummary> branch_ds;

  static void update_rate(PerfCounter& a, PerfCounter& b, atlas::meter::DDistributionSummary* ds,
                          const char* name) {
    auto a_values = a.read();
    auto b_values = b.read();
    assert(a_values.size() == b_values.size());

    // compute ipc for each core
    for (auto i = 0u; i < a_values.size(); ++i) {
      auto denominator = b_values[i];
      if (denominator == 0) continue;

      auto numerator = a_values[i];
      auto rate = static_cast<double>(numerator) / denominator;
      Logger()->trace("Updating {} with {}/{}={}", name, numerator, denominator, rate);
      ds->Record(rate);
    }
  }
};
}  // namespace atlasagent
