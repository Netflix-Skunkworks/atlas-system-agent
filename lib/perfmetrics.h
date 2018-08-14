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

  auto start_of_range = -1;
  for (;;) {
    int cpu;
    char sep;
    auto n = fscanf(fp, "%d%c", &cpu, &sep);
    if (n <= 0) {
      break;
    }

    result->resize(cpu + 1);
    if (start_of_range >= 0) {
      for (auto i = start_of_range; i <= cpu; ++i) {
        result->at(i) = true;
      }
    }

    if (sep == '-') {
      start_of_range = cpu;
    } else {
      result->at(cpu) = true;
      start_of_range = -1;
    }
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
  explicit PerfCounter(uint64_t config) : config_(config), pid_{-1} {
#ifdef __linux__
    memset(&pea, 0, sizeof pea);
    pea.config = config;
    pea.type = PERF_TYPE_HARDWARE;
    pea.size = sizeof(perf_event_attr);
#endif
  }

#ifdef __linux__
#ifdef TITUS_AGENT
  int perf_open(int cpu, unsigned long flags) {
    int tmperrno, ret;
    uid_t uid;

    uid = geteuid();
    if (seteuid(0))
      return -1;
    ret = perf_event_open(&pea, pid_, cpu, -1, flags);
    tmperrno = errno;
    /* If this call fails, the system is a potentially unsecure state, and we should bail */
    if (seteuid(uid))
	abort();

    errno = tmperrno;
    return ret;
  }
#else
  int perf_open(int cpu, unsigned long flags) {
    return perf_event_open(&pea, pid_, cpu, -1, flags);
  }
#endif
#endif

  void set_pid(int pid) { pid_ = pid; }

  bool open_events(const std::vector<bool>& online_cpus) {
    // ensure we don't leak fds
    close_events();

    fds_.assign(online_cpus.size(), -1);
#ifdef __linux__
#ifdef TITUS_AGENT
    unsigned long flags = PERF_FLAG_PID_CGROUP;
#else
    unsigned long flags = 0;
#endif
    for (auto i = 0u; i < online_cpus.size(); ++i) {
      if (online_cpus[i]) {
        fds_[i] = perf_open(i, flags);
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
      }
    }
#endif
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
    }
    return res;
  }

  void close_events() {
    for (auto& fd : fds_) {
      if (fd >= 0) {
        close(fd);
        fd = -1;
      }
    }
  }

  ~PerfCounter() { close_events(); }

  uint64_t Config() const { return config_; }

  int Pid() const { return pid_; }

 private:
  std::vector<int> fds_;
  uint64_t config_;
  int pid_;
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

    // start collection
    if (!open_perf_counters_if_needed()) {
      return;
    }

    instructions_ds = registry_->distribution_summary("sys.cpu.instructions");
    cycles_ds = registry_->distribution_summary("sys.cpu.cycles");
    cache_ds = registry_->ddistribution_summary("sys.cpu.cacheMissRate");
    branch_ds = registry_->ddistribution_summary("sys.cpu.branchMispredictionRate");
  }

  bool open_perf_counters_if_needed() {
#ifdef TITUS_AGENT
    if (pid_ < 0) {
      auto name = fmt::format("{}/{}", path_prefix_, "sys/fs/cgroup/perf_event");
      pid_.open(name.c_str());
      if (pid_ < 0) {
        Logger()->warn("Unable to start collection of perf counters for cgroup");
        return false;
      }
      Logger()->info("Opened cgroup file");
      cycles.set_pid(pid_);
      instructions.set_pid(pid_);
      cache_refs.set_pid(pid_);
      cache_misses.set_pid(pid_);
      branch_insts.set_pid(pid_);
      branch_misses.set_pid(pid_);
    }
#endif
    auto new_online_cpus = get_online_cpus();
    if (new_online_cpus == online_cpus_) {
      Logger()->trace("Online CPUs have not changed. No need to reopen perf events.");
      return true;
    }
    online_cpus_ = std::move(new_online_cpus);

    Logger()->info("Online CPUs have changed. Reopening perf events.");
    // if we get EACCES for these then we don't even try the rest
    if (!cycles.open_events(online_cpus_) || !instructions.open_events(online_cpus_)) {
      disabled_ = true;
      return false;
    }
    cache_refs.open_events(online_cpus_);
    cache_misses.open_events(online_cpus_);
    branch_insts.open_events(online_cpus_);
    branch_misses.open_events(online_cpus_);

    return true;
  }

  // https://www.kernel.org/doc/Documentation/cputopology.txt
  std::vector<bool> get_online_cpus() {
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

  void collect() {
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

 private:
  bool disabled_ = false;
  atlas::meter::Registry* registry_;
  std::string path_prefix_;
  std::vector<bool> online_cpus_;
  UnixFile pid_{-1};
  PerfCounter cycles{PERF_COUNT_HW_CPU_CYCLES};
  PerfCounter instructions{PERF_COUNT_HW_INSTRUCTIONS};
  PerfCounter cache_refs{PERF_COUNT_HW_CACHE_REFERENCES};
  PerfCounter cache_misses{PERF_COUNT_HW_CACHE_MISSES};
  PerfCounter branch_insts{PERF_COUNT_HW_BRANCH_INSTRUCTIONS};
  PerfCounter branch_misses{PERF_COUNT_HW_BRANCH_MISSES};

  // instructions
  std::shared_ptr<atlas::meter::DistributionSummary> instructions_ds;

  // cycles
  std::shared_ptr<atlas::meter::DistributionSummary> cycles_ds;

  // cache miss rate
  std::shared_ptr<atlas::meter::DDistributionSummary> cache_ds;
  // branch miss rate
  std::shared_ptr<atlas::meter::DDistributionSummary> branch_ds;

  static void update_ds(PerfCounter& a, atlas::meter::DistributionSummary* ds, const char* name) {
    auto a_values = a.read();
    // update our distribution summary with values from each CPU
    for (auto v : a_values) {
      Logger()->trace("Updating {} with {}", name, v);
      ds->Record(v);
    }
  }

  static void update_rate(PerfCounter& a, PerfCounter& b, atlas::meter::DDistributionSummary* ds,
                          const char* name) {
    auto a_values = a.read();
    auto b_values = b.read();
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
};
}  // namespace atlasagent
