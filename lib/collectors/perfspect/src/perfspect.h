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
};

struct PerfspectData
{
    float cpuFrequency{};
    float cyclesPerSecond{};
    float instructionsPerSecond{};
    float l2CacheMissesPerSecond{};
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
    bool start_script();
    void cleanup_process();

    void SendMetrics(PerfspectData data);
    
    std::optional<std::string> readOutputNew();

    Registry* registry_;
    bool firstIteration{true};
    bool isAmd{false};
    char version;

    std::unique_ptr<boost::process::child> scriptProcess;
    std::unique_ptr<boost::process::ipstream> outStream;  // Pipe for reading stdout
    std::unique_ptr<boost::process::ipstream> errStream;  // Pipe for reading stderr
};