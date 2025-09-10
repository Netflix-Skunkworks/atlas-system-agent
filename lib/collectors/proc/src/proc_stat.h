
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
          guest_nice(0),
          total(0)
    {
        // Skip the first element (CPU name like "cpu0") and parse the numeric fields
        if (fields.size() >= 8)
        {  // Minimum required fields (user through softirq)
            auto result1 = std::from_chars(fields[1].data(), fields[1].data() + fields[1].size(), user);
            auto result2 = std::from_chars(fields[2].data(), fields[2].data() + fields[2].size(), nice);
            auto result3 = std::from_chars(fields[3].data(), fields[3].data() + fields[3].size(), system);
            auto result4 = std::from_chars(fields[4].data(), fields[4].data() + fields[4].size(), idle);
            auto result5 = std::from_chars(fields[5].data(), fields[5].data() + fields[5].size(), iowait);
            auto result6 = std::from_chars(fields[6].data(), fields[6].data() + fields[6].size(), irq);
            auto result7 = std::from_chars(fields[7].data(), fields[7].data() + fields[7].size(), softirq);

            // Check for parsing errors in required fields
            if (result1.ec != std::errc{} || result2.ec != std::errc{} || result3.ec != std::errc{} || 
                result4.ec != std::errc{} || result5.ec != std::errc{} || result6.ec != std::errc{} || 
                result7.ec != std::errc{})
            {
                // Reset to zero on parse error
                user = nice = system = idle = iowait = irq = softirq = steal = guest = guest_nice = total = 0;
                return;
            }

            // Optional fields for newer kernels
            if (fields.size() >= 9)
            {
                auto result8 = std::from_chars(fields[8].data(), fields[8].data() + fields[8].size(), steal);
                if (result8.ec != std::errc{}) steal = 0;
            }
            if (fields.size() >= 10)
            {
                auto result9 = std::from_chars(fields[9].data(), fields[9].data() + fields[9].size(), guest);
                if (result9.ec != std::errc{}) guest = 0;
            }
            if (fields.size() >= 11)
            {
                auto result10 = std::from_chars(fields[10].data(), fields[10].data() + fields[10].size(), guest_nice);
                if (result10.ec != std::errc{}) guest_nice = 0;
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
        registry, name, [](Registry* reg, const char* n, const std::unordered_map<std::string, std::string>& tags)
        { return reg->CreateGauge(n, tags); });
}

inline CpuGaugesTemplate<MaxGauge> CreatePeakCpuGauges(Registry* registry, const char* name)
{
    return CpuGaugesTemplate<MaxGauge>(
        registry, name, [](Registry* reg, const char* n, const std::unordered_map<std::string, std::string>& tags)
        { return reg->CreateMaxGauge(n, tags); });
}