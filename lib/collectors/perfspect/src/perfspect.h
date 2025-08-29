#pragma once

#include <boost/process.hpp>

#include <vector>
#include <string>
#include <memory>
#include <optional>

#include <thirdparty/spectator-cpp/spectator/registry.h>

struct PerfspectConstants
{
    static constexpr auto BinaryLocation{"/apps/nflx-perfspect/bin"};
    static constexpr auto BinaryName{"perfspect"};

    // Individual argument constants
    static constexpr auto command{"metrics"};
    static constexpr auto eventfileFlag{"--eventfile"};
    static constexpr auto eventfilePathAmd{"/apps/nflx-perfspect/etc/events-amd.txt"};
    static constexpr auto eventfilePathIntel{"/apps/nflx-perfspect/etc/events-intel.txt"};
    static constexpr auto metricfileFlag{"--metricfile"};
    static constexpr auto metricfilePathAmd{"/apps/nflx-perfspect/etc/metrics-amd.json"};
    static constexpr auto metricfilePathIntel{"/apps/nflx-perfspect/etc/metrics-intel.json"};
    static constexpr auto durationFlag{"--duration"};
    static constexpr auto durationValue{"60"};
    static constexpr auto liveFlag{"--live"};

    static constexpr auto cpuFreqMetricName{"cpu.perfspect.frequency"};
    static constexpr auto cpiMetricName{"cpu.perfspect.cyclesPerInstruction"};
    
    static constexpr auto totalInstructionsMetricName{"cpu.perfspect.totalInstructions"};
    
    static constexpr auto totalL2CacheMissesMetricName{"cpu.perfspect.totalL2CacheMisses"};
    
    static constexpr auto l2CacheMissRateMetricName{"cpu.perfspect.l2CacheMissRatePti"};

    static constexpr auto CodeMetricId{"code"};
    static constexpr auto DataMetricId{"data"};

    static constexpr auto FiveSecondIntervals{12}; // 12 intervals of 5 seconds each for a total of 60 seconds

};

struct PerfspectData
{
    float cpuFrequency{0.0};
    float cyclesPerInstruction{0.0};
    float l2DataCacheMisses{0.0}; // Instructions per 1000 instructions
    float l2CodeCacheMisses{0.0}; // Instructions per 1000 instructions
    float gips{0.0};  // Giga Instructions Per Second
};

bool valid_instance();

namespace detail
{
inline auto perfspectGauge(Registry* registry, const std::string_view name, const char* id = nullptr)
{
    return id == nullptr 
        ? registry->CreateGauge(std::string(name))
        : registry->CreateGauge(std::string(name), {{"id", id}});
}
inline auto perfspectCounter(Registry* registry, const std::string_view name, const char* id = nullptr)
{
    return id == nullptr 
        ? registry->CreateCounter(std::string(name))
        : registry->CreateCounter(std::string(name), {{"id", id}});
}

}  // namespace detail

class Perfspect
{
   public:
    Perfspect(Registry* registry, std::pair<char, char> instanceInfo);
    ~Perfspect() { cleanup_process(); };

    // Abide by the C++ rule of 5
    Perfspect(const Perfspect& other) = delete;
    Perfspect& operator=(const Perfspect& other) = delete;
    Perfspect(Perfspect&& other) = delete;
    Perfspect& operator=(Perfspect&& other) = delete;

    bool gather_metrics();
    static std::optional<std::pair<char, char>> is_valid_instance();

   private:
    bool process_completed();
    bool start_script();
    bool read_output();
    void cleanup_process();
    bool sendMetricsAMD(const std::vector<std::string>& perfspectOutput);

    Registry* registry_;
    bool scriptStarted{false};
    std::vector<std::string> perfspectOutput;
    bool isAmd{false};
    char version;

    std::unique_ptr<boost::process::child> scriptProcess;
    std::unique_ptr<boost::process::ipstream> outStream;  // Pipe for reading stdout
    std::unique_ptr<boost::process::ipstream> errStream;  // Pipe for reading stderr
};