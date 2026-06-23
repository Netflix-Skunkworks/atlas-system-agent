#pragma once

#include <chrono>
#include <optional>

// Turns a monotonically increasing CPU-time counter (microseconds) into a utilization percentage
// against wall-clock time, where 100% == one core fully busy. One tracker owns one counter.
//
// It also owns the Identity the counter belongs to -- the main PID for the process scope, the
// control-group path for the service scope. Bundling the identity with the baseline is what makes a
// restart safe: when the PID or cgroup changes, update() sees a new identity and treats this sample
// as a fresh baseline instead of subtracting two unrelated process generations' counters.
//
// update() advances the baseline (counter, identity, timestamp) as a side effect every time it is
// called, and returns the percentage for the interval just closed -- or nullopt when there is
// nothing meaningful to report: the first sample, an identity change (restart), a counter that went
// backward (a reset), or no elapsed wall time. Wall time is measured from the stored timestamp, so
// a service skipped for several cycles yields the correct average over the whole gap, not a spike.
//
// Callers must only call update() with a counter they actually read this cycle. A failed read means
// "no sample" -- skip the call entirely and the prior baseline is preserved untouched.
//
// This type is intentionally free of any systemd / D-Bus / spectator dependency so it can be unit
// tested in isolation; see test/service_monitor_test.cpp.
template <typename Identity>
class CpuRateTracker
{
   public:
    std::optional<double> update(const Identity& id, unsigned long long counterUs,
                                 std::chrono::steady_clock::time_point now)
    {
        std::optional<double> pct;
        if (valid_ && id == id_ && counterUs >= counterUs_)
        {
            const auto wallUs = std::chrono::duration_cast<std::chrono::microseconds>(now - at_).count();
            if (wallUs > 0)
            {
                pct = 100.0 * static_cast<double>(counterUs - counterUs_) / static_cast<double>(wallUs);
            }
        }

        id_ = id;
        counterUs_ = counterUs;
        at_ = now;
        valid_ = true;
        return pct;
    }

   private:
    Identity id_{};
    unsigned long long counterUs_{0};
    std::chrono::steady_clock::time_point at_{};
    bool valid_{false};  // false until the first update(); the first sample reports no percentage
};
