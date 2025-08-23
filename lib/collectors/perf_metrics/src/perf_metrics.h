#pragma once

#include <thirdparty/spectator-cpp/spectator/registry.h>

#include <lib/util/src/util.h>
#include <fmt/format.h>
#include <unistd.h>
#include <sys/ioctl.h>

#ifndef __linux__
enum perf_hw_id
{
    PERF_COUNT_HW_CPU_CYCLES,
    PERF_COUNT_HW_INSTRUCTIONS,
    PERF_COUNT_HW_CACHE_REFERENCES,
    PERF_COUNT_HW_CACHE_MISSES,
    PERF_COUNT_HW_BRANCH_INSTRUCTIONS,
    PERF_COUNT_HW_BRANCH_MISSES,
};
#else
#include <linux/unistd.h>
#include <linux/perf_event.h>
#include <linux/hw_breakpoint.h>
inline int perf_event_open(struct perf_event_attr* hw_event, pid_t pid, int cpu, int group_fd, unsigned long flags)
{
    return syscall(__NR_perf_event_open, hw_event, pid, cpu, group_fd, flags);
}
#endif

// parse a file with contents like 0-3,5-7,9-23,38 into a vector
// of booleans indicating whether the given index is included
inline void parse_range(FILE* fp, std::vector<bool>* result)
{
    if (fp == nullptr) return;

    auto start_of_range = -1;
    for (;;)
    {
        int cpu;
        char sep;
        auto n = fscanf(fp, "%d%c", &cpu, &sep);
        if (n <= 0)
        {
            break;
        }

        result->resize(cpu + 1);
        if (start_of_range >= 0)
        {
            for (auto i = start_of_range; i <= cpu; ++i)
            {
                result->at(i) = true;
            }
        }

        if (sep == '-')
        {
            start_of_range = cpu;
        }
        else
        {
            result->at(cpu) = true;
            start_of_range = -1;
        }
    }
}

namespace atlasagent
{

struct perf_count
{
    uint64_t raw_value;
    uint64_t time_enabled;
    uint64_t time_running;

    uint64_t delta_from(const perf_count& prev)
    {
        if (time_running == 0)
        {
            return 0;
        }
        perf_count delta;
        delta.time_running = time_running - prev.time_running;
        delta.time_enabled = time_enabled - prev.time_enabled;
        delta.raw_value = raw_value - prev.raw_value;

        auto value = static_cast<uint64_t>(static_cast<double>(delta.raw_value) * delta.time_enabled /
                                           (delta.time_running + 0.5));
        return value;
    }
};

class PerfCounter
{
   public:
    explicit PerfCounter(uint64_t config) : config_(config), pid_{-1} {}

#ifdef __linux__
    void setup_perf_event(perf_event_attr* pea)
    {
        memset(pea, 0, sizeof(perf_event_attr));
        pea->config = config_;
        pea->exclude_guest = 1;
        pea->read_format = PERF_FORMAT_TOTAL_TIME_ENABLED | PERF_FORMAT_TOTAL_TIME_RUNNING;
        pea->type = PERF_TYPE_HARDWARE;
        pea->size = sizeof(perf_event_attr);
        pea->inherit = 1;
    }

    int perf_open(int cpu, unsigned long flags)
    {
        perf_event_attr pea;
        setup_perf_event(&pea);
        return perf_event_open(&pea, pid_, cpu, -1, flags);
    }
#endif  // __linux__

    void set_pid(int pid) { pid_ = pid; }

    bool open_events(const std::vector<bool>& online_cpus)
    {
        // ensure we don't leak fds
        close_events();

        fds_.assign(online_cpus.size(), -1);
#ifdef __linux__
        unsigned long flags = 0;
        for (auto i = 0u; i < online_cpus.size(); ++i)
        {
            if (online_cpus[i])
            {
                fds_[i] = perf_open(i, flags);
                if (fds_[i] < 0)
                {
                    if (errno == EACCES)
                    {
                        Logger()->warn(
                            "Unable to collect performance events - Check "
                            "/proc/sys/kernel/perf_event_paranoid");
                        return false;
                    }
                    else if (errno == ENOENT)
                    {
                        Logger()->warn("This system does not allow access to hardware performance counters");
                        return false;
                    }
                    else
                    {
                        Logger()->warn("Unable to perf_event_open CPU {}: {}({})", i, strerror(errno), errno);
                    }
                }
            }
        }
        prev_vals.resize(fds_.size());
#endif
        return true;
    }

    std::vector<uint64_t> read_delta()
    {
        std::vector<uint64_t> res;
        res.resize(fds_.size());

        // read new values
        std::vector<perf_count> new_vals;
        new_vals.resize(fds_.size());
        for (auto i = 0u; i < fds_.size(); ++i)
        {
            auto fd = fds_[i];
            if (fd >= 0)
            {
                memset(&new_vals[i], 0, sizeof(perf_count));
                if (::read(fd, &new_vals[i], sizeof(perf_count)) != sizeof(perf_count))
                {
                    Logger()->warn("Unable to read value from CPU {}", i);
                    return std::vector<uint64_t>();
                }
            }
        }

        // compute values
        for (auto i = 0u; i < fds_.size(); ++i)
        {
            auto value = new_vals[i].delta_from(prev_vals[i]);
            res[i] = value;
        }

        // remember the values just read
        prev_vals = std::move(new_vals);
        return res;
    }

    void close_events()
    {
        for (auto& fd : fds_)
        {
            if (fd >= 0)
            {
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
    std::vector<perf_count> prev_vals;
};

class PerfMetrics
{
   public:
    PerfMetrics(Registry* registry, const std::string path_prefix);

    bool open_perf_counters_if_needed();

    // https://www.kernel.org/doc/Documentation/cputopology.txt
    std::vector<bool> get_online_cpus();

    void collect();

   private:
    bool disabled_ = true;
    Registry* registry_;
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
    DistributionSummary instructions_ds_;

    // cycles
    DistributionSummary cycles_ds_;

    // cache miss rate
    DistributionSummary cache_ds_;

    // branch miss rate
    DistributionSummary branch_ds_;

    void update_ds(PerfCounter& a, const DistributionSummary& ds, const char* name);

    static void update_rate(PerfCounter& a, PerfCounter& b, const DistributionSummary& ds, const char* name);
};

}  // namespace atlasagent
