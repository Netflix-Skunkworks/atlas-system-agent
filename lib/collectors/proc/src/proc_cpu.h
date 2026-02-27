#pragma once
#include <vector>
#include <string>
#include <cstdlib>
#include <cstdint>
#include <unordered_map>
#include <charconv>

#include <thirdparty/spectator-cpp/spectator/registry.h>

struct Cpu_Gauge_Values
{
    double user;
    double system;
    double stolen;
    double nice;
    double wait;
    double interrupt;
};

class CpuStatFields
{
   public:
    CpuStatFields(const std::vector<std::string>& fields)
        : user(0),
          nice(0),
          system(0),
          idle(0),
          iowait(0),
          irq(0),
          softirq(0),
          steal(0),
          guest(0),
          guest_nice(0)
    {
        uint64_t* values[] = {&user, &nice, &system, &idle, &iowait, &irq, &softirq, &steal, &guest, &guest_nice};

        // Parse all fields in a loop
        for (int i = 0; i < 10; ++i)
        {
            if (auto result =
                    std::from_chars(fields[i + 1].data(), fields[i + 1].data() + fields[i + 1].size(), *values[i]);
                result.ec != std::errc{})
            {
                // Reset all on any parse error
                user = nice = system = idle = iowait = irq = softirq = steal = guest = guest_nice = 0;
                throw std::invalid_argument("Invalid CPU stat field: " + fields[i + 1]);
            }
        }
    }

    uint64_t user;
    uint64_t nice;
    uint64_t system;
    uint64_t idle;
    uint64_t iowait;
    uint64_t irq;
    uint64_t softirq;
    uint64_t steal;
    uint64_t guest;
    uint64_t guest_nice;
};

inline Cpu_Gauge_Values ComputeGaugeValues(const CpuStatFields& prev, const CpuStatFields& current)
{
    Cpu_Gauge_Values vals{0.0, 0.0, 0.0, 0.0, 0.0, 0.0};
    auto delta_user = current.user - prev.user;
    auto delta_system = current.system - prev.system;
    auto delta_stolen = current.steal - prev.steal;
    auto delta_nice = current.nice - prev.nice;
    auto delta_idle = current.idle - prev.idle;
    auto delta_interrupt = (current.irq + current.softirq) - (prev.irq + prev.softirq);

    // iowait is non-monotonic on SMP systems and can decrease between readings. Clamp to 0
    // so the decrease doesn't shrink delta_total and inflate all percentages above 100%.
    auto delta_wait = current.iowait > prev.iowait ? current.iowait - prev.iowait : 0;

    // delta_idle is included so each percentage reflects a fraction of total CPU time,
    // not just busy time. guest/guest_nice are excluded because /proc/stat already accounts
    // for them within user/nice — adding them separately would double-count that time.
    auto delta_total = delta_user + delta_nice + delta_system + delta_idle
                     + delta_wait + delta_interrupt + delta_stolen;

    if (delta_total > 0)
    {
        vals.user = 100.0 * delta_user / delta_total;
        vals.system = 100.0 * delta_system / delta_total;
        vals.stolen = 100.0 * delta_stolen / delta_total;
        vals.nice = 100.0 * delta_nice / delta_total;
        vals.wait = 100.0 * delta_wait / delta_total;
        vals.interrupt = 100.0 * delta_interrupt / delta_total;
    }
    return vals;
}

template <typename GaugeType>
class CpuGaugesTemplate
{
   public:
    template <typename CreateGaugeFn>
    CpuGaugesTemplate(Registry* registry, const char* name, CreateGaugeFn createGauge)
        : user_gauge(createGauge(registry, name, {{"id", "user"}})),
          system_gauge(createGauge(registry, name, {{"id", "system"}})),
          stolen_gauge(createGauge(registry, name, {{"id", "stolen"}})),
          nice_gauge(createGauge(registry, name, {{"id", "nice"}})),
          wait_gauge(createGauge(registry, name, {{"id", "wait"}})),
          interrupt_gauge(createGauge(registry, name, {{"id", "interrupt"}}))
    {
    }

    void update(const Cpu_Gauge_Values& vals)
    {
        user_gauge.Set(vals.user);
        system_gauge.Set(vals.system);
        stolen_gauge.Set(vals.stolen);
        nice_gauge.Set(vals.nice);
        wait_gauge.Set(vals.wait);
        interrupt_gauge.Set(vals.interrupt);
    }

   private:
    GaugeType user_gauge;
    GaugeType system_gauge;
    GaugeType stolen_gauge;
    GaugeType nice_gauge;
    GaugeType wait_gauge;
    GaugeType interrupt_gauge;
};

// Factory functions for easy construction
inline CpuGaugesTemplate<Gauge> CreateCpuGauges(Registry* registry, const char* name)
{
    return CpuGaugesTemplate<Gauge>(
        registry, name,
        [](Registry* reg, const char* n, const std::unordered_map<std::string, std::string>& tags)
        { return reg->CreateGauge(n, tags); });
}

inline CpuGaugesTemplate<MaxGauge> CreatePeakCpuGauges(Registry* registry, const char* name)
{
    return CpuGaugesTemplate<MaxGauge>(
        registry, name,
        [](Registry* reg, const char* n, const std::unordered_map<std::string, std::string>& tags)
        { return reg->CreateMaxGauge(n, tags); });
}