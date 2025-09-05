#pragma once

#include <boost/process.hpp>
#include <boost/asio.hpp>

#include <string>
#include <memory>
#include <optional>

#include <thirdparty/spectator-cpp/spectator/registry.h>

struct PerfspectConstants
{
    static constexpr auto BinaryLocation{"/apps/nflx-perfspect/bin"};
    static constexpr auto BinaryName{"perfspect"};
    static constexpr auto command{"metrics"};
    static constexpr auto eventfileFlag{"--eventfile"};
    static constexpr auto eventfilePathAmd{"/apps/nflx-perfspect/etc/events-amd.txt"};
    static constexpr auto eventfilePathIntel{"/apps/nflx-perfspect/etc/events-intel.txt"};
    static constexpr auto metricfileFlag{"--metricfile"};
    static constexpr auto metricfilePathAmd{"/apps/nflx-perfspect/etc/metrics-amd.json"};
    static constexpr auto metricfilePathIntel{"/apps/nflx-perfspect/etc/metrics-intel.json"};
    static constexpr auto intervalFlag{"--interval"};
    static constexpr auto intervalValue{"5"};
    static constexpr auto liveFlag{"--live"};
    static constexpr auto metricFrequency{"cpu.perfspect.frequency"};
    static constexpr auto metricCycles{"cpu.perfspect.cycles"};
    static constexpr auto metricInstructions{"cpu.perfspect.instructions"};
    static constexpr auto metricL2CacheMisses{"cpu.perfspect.l2CacheMisses"};
    static constexpr auto interval{5};
    static constexpr auto minGeneration{7};
    static constexpr auto amdSymbol{'a'};
    static constexpr auto intelSymbol{'i'};
    static constexpr auto productNamePath{"/sys/devices/virtual/dmi/id/product_name"};
};

struct PerfspectData
{
    float cpuFrequency{};
    float cyclesPerSecond{};
    float instructionsPerSecond{};
    float l2CacheMissesPerSecond{};
};

struct ReadResult
{
    std::optional<std::string> data;
    boost::system::error_code error;
};

// Function to parse EC2 instance product name into generation and processor type
// Public for testing purposes
std::optional<std::pair<char, char>> ParseProductName(const std::string& productName);

class Perfspect
{
   public:
    Perfspect(Registry* registry, const std::pair<char, char>& instanceInfo);
    ~Perfspect() { CleanupProcess(); };

    // Abide by the C++ rule of 5
    Perfspect(const Perfspect& other) = delete;
    Perfspect& operator=(const Perfspect& other) = delete;
    Perfspect(Perfspect&& other) = delete;
    Perfspect& operator=(Perfspect&& other) = delete;

    bool GatherMetrics();
    static std::optional<std::pair<char, char>> IsValidInstance();

   private:
    void AsyncRead();
    bool CleanupProcess();
    void ExtractLine(const boost::system::error_code& ec, std::size_t bytes_transferred);
    ReadResult ReadOutput();
    void SendMetrics(const PerfspectData& data);
    bool StartScript();

    Registry* registry_;
    bool firstIteration{true};
    bool isAmd{false};
    char version;

    std::unique_ptr<boost::process::child> scriptProcess;

    // Asio components for async reading
    std::unique_ptr<boost::asio::io_context> ioContext;
    std::unique_ptr<boost::process::async_pipe> asyncPipe;
    std::unique_ptr<boost::asio::streambuf> buffer;
    std::string pendingLine;
    boost::system::error_code lastAsyncError;

    // Metrics
    Gauge cpuFrequencyGauge;
    Counter cyclesCounter;
    Counter instructionsCounter;
    Counter l2CacheMissesCounter;
};