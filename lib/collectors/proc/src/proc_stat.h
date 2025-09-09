
#include <vector>
#include <string>
#include <cstdlib>
#include <cstdint>

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
    // Default constructor
    CpuStatFields() 
        : user(0), nice(0), system(0), idle(0), iowait(0), irq(0), softirq(0), steal(0), guest(0), guest_nice(0), total(0)
    {
    }

    // Constructor that takes a vector of strings like: ["cpu0", "5209", "0", "5559", "263846", "286", "0", "5538", "0",
    // "0", "0"]
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
          guest_nice(0),
          total(0)
    {
        // Skip the first element (CPU name like "cpu0") and parse the numeric fields
        if (fields.size() >= 8)
        {  // Minimum required fields (user through softirq)
            user = std::strtoull(fields[1].c_str(), nullptr, 10);
            nice = std::strtoull(fields[2].c_str(), nullptr, 10);
            system = std::strtoull(fields[3].c_str(), nullptr, 10);
            idle = std::strtoull(fields[4].c_str(), nullptr, 10);
            iowait = std::strtoull(fields[5].c_str(), nullptr, 10);
            irq = std::strtoull(fields[6].c_str(), nullptr, 10);
            softirq = std::strtoull(fields[7].c_str(), nullptr, 10);

            // Optional fields for newer kernels
            if (fields.size() >= 9)
            {
                steal = std::strtoull(fields[8].c_str(), nullptr, 10);
            }
            if (fields.size() >= 10)
            {
                guest = std::strtoull(fields[9].c_str(), nullptr, 10);
            }
            if (fields.size() >= 11)
            {
                guest_nice = std::strtoull(fields[10].c_str(), nullptr, 10);
            }

            // Calculate total
            total = user + nice + system + idle + iowait + irq + softirq + steal + guest + guest_nice;
        }
    }

    Cpu_Gauge_Values computeGaugeValues(const CpuStatFields& prev) const
    {
        Cpu_Gauge_Values vals{0.0, 0.0, 0.0, 0.0, 0.0, 0.0};
        auto delta_total = total - prev.total;
        auto delta_user = user - prev.user;
        auto delta_system = system - prev.system;
        auto delta_stolen = steal - prev.steal;
        auto delta_nice = nice - prev.nice;
        auto delta_interrupt = (irq + softirq) - (prev.irq + prev.softirq);
        auto delta_wait = iowait > prev.iowait ? iowait - prev.iowait : 0;

        if (delta_total > 0)
        {
            vals.user = 100.0 * delta_user / delta_total;
            vals.system = 100.0 * delta_system / delta_total;
            vals.stolen = 100.0 * delta_stolen / delta_total;
            vals.nice = 100.0 * delta_nice / delta_total;
            vals.wait = 100.0 * delta_wait / delta_total;
            vals.interrupt = 100.0 * delta_interrupt / delta_total;
        }
        else
        {
            vals.user = vals.system = vals.stolen = vals.nice = vals.wait = vals.interrupt = 0.0;
        }
        return vals;
    }

   private:
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

    uint64_t total;  // computed as sum of all above fields
};

class CpuGauges
{
public:
    CpuGauges(Registry* registry, const char* name)
        : user_gauge(registry->CreateGauge(name, {{"id", "user"}})),        // id = "user"
          system_gauge(registry->CreateGauge(name, {{"id", "system"}})),    // id = "system"  
          stolen_gauge(registry->CreateGauge(name, {{"id", "stolen"}})),    // id = "stolen"
          nice_gauge(registry->CreateGauge(name, {{"id", "nice"}})),        // id = "nice"
          wait_gauge(registry->CreateGauge(name, {{"id", "wait"}})),        // id = "wait"
          interrupt_gauge(registry->CreateGauge(name, {{"id", "interrupt"}})) // id = "interrupt"
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
    Gauge user_gauge;
    Gauge system_gauge;
    Gauge stolen_gauge;
    Gauge nice_gauge;
    Gauge wait_gauge;
    Gauge interrupt_gauge;
};

class PeakCpuGauges
{
public:
    PeakCpuGauges(Registry* registry, const char* name)
        : peak_user_gauge(registry->CreateMaxGauge(name, {{"id", "user"}})),        // id = "user"
          peak_system_gauge(registry->CreateMaxGauge(name, {{"id", "system"}})),    // id = "system"  
          peak_stolen_gauge(registry->CreateMaxGauge(name, {{"id", "stolen"}})),    // id = "stolen"
          peak_nice_gauge(registry->CreateMaxGauge(name, {{"id", "nice"}})),        // id = "nice"
          peak_wait_gauge(registry->CreateMaxGauge(name, {{"id", "wait"}})),        // id = "wait"
          peak_interrupt_gauge(registry->CreateMaxGauge(name, {{"id", "interrupt"}})) // id = "interrupt"
    {
    }
    void update(const Cpu_Gauge_Values& vals)
    {
        peak_user_gauge.Set(vals.user);
        peak_system_gauge.Set(vals.system);
        peak_stolen_gauge.Set(vals.stolen);
        peak_nice_gauge.Set(vals.nice);
        peak_wait_gauge.Set(vals.wait);
        peak_interrupt_gauge.Set(vals.interrupt);
    }
private:
    MaxGauge peak_user_gauge;
    MaxGauge peak_system_gauge;
    MaxGauge peak_stolen_gauge;
    MaxGauge peak_nice_gauge;
    MaxGauge peak_wait_gauge;
    MaxGauge peak_interrupt_gauge;
};