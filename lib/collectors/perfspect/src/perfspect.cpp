#include "perfspect.h"
#include <lib/logger/src/logger.h>
#include <lib/util/src/util.h>

#include <boost/process.hpp>
#include <boost/asio.hpp>

#include <string>
#include <string_view>
#include <vector>
#include <fstream>
#include <filesystem>
#include <charconv>

Perfspect::Perfspect(Registry* registry, const std::pair<char, char>& instanceInfo)
    : registry_(registry),
      isAmd(instanceInfo.second == PerfspectConstants::amdSymbol),
      version(instanceInfo.first),
      cpuFrequencyGauge(registry->CreateGauge(PerfspectConstants::metricFrequency)),
      cyclesCounter(registry->CreateCounter(PerfspectConstants::metricCycles)),
      instructionsCounter(registry->CreateCounter(PerfspectConstants::metricInstructions)),
      l2CacheMissesCounter(registry->CreateCounter(PerfspectConstants::metricL2CacheMisses))
{
    atlasagent::Logger()->info("Perfspect initialized for instance type: {}:{}", instanceInfo.first,
                               instanceInfo.second);
};

std::optional<std::pair<char, char>> ParseProductName(const std::string& productName)
{
    if (productName.size() < 3)
    {
        atlasagent::Logger()->debug("Product name too short to parse: {}", productName);
        return std::nullopt;
    }

    char generation = productName[1];

    // Find the dot separator - it must be present in valid EC2 instance names
    auto dot_pos = productName.find('.');
    if (dot_pos == std::string::npos)
    {
        atlasagent::Logger()->debug("No dot separator found in product name: {}", productName);
        return std::nullopt;
    }

    // Find the first occurrence of a processor symbol after position 1 but before the dot
    auto processor_pos =
        productName.find_first_of(std::string{PerfspectConstants::amdSymbol, PerfspectConstants::intelSymbol}, 2);
    if (processor_pos == std::string::npos || processor_pos >= dot_pos)
    {
        atlasagent::Logger()->debug("Could not find processor id before dot in product name: {}", productName);
        return std::nullopt;
    }

    return std::make_pair(generation, productName[processor_pos]);
}

std::optional<std::pair<char, char>> Perfspect::IsValidInstance()
{
    if (atlasagent::is_file_present(PerfspectConstants::BinaryLocation) == false)
    {
        atlasagent::Logger()->debug("Perfspect binary not found at {}", PerfspectConstants::BinaryLocation);
        return std::nullopt;
    }

    std::ifstream file(PerfspectConstants::productNamePath);
    std::string product_name;
    if (!file.is_open() || !std::getline(file, product_name))
    {
        atlasagent::Logger()->error("Failed to read product name from DMI");
        return std::nullopt;
    }

    auto parsedProductName = ParseProductName(product_name);
    if (parsedProductName.has_value() == false)
    {
        atlasagent::Logger()->debug("Invalid product credentials for PerfSpect: {}", product_name);
        return std::nullopt;
    }

    char generation = parsedProductName.value().first;
    char processor = parsedProductName.value().second;

    int gen_value;
    auto result = std::from_chars(&generation, &generation + 1, gen_value);
    bool valid = (result.ec == std::errc()) && (gen_value >= PerfspectConstants::minGeneration) &&
                 (processor == PerfspectConstants::intelSymbol || processor == PerfspectConstants::amdSymbol);
    if (valid == false)
    {
        atlasagent::Logger()->debug("Invalid instance: {} (gen: {}, proc: {})", product_name, generation, processor);
        return std::nullopt;
    }
    return std::make_pair(generation, processor);
}

bool Perfspect::StartScript() try
{
    // Clean up any existing process first
    if (CleanupProcess() == false)
    {
        atlasagent::Logger()->error("Failed to clean up previous Perfspect process.");
        return false;
    }

    std::filesystem::path fullBinaryPath =
        std::filesystem::path(PerfspectConstants::BinaryLocation) / PerfspectConstants::BinaryName;

    // Create io_context, async pipe, and buffer for stdout
    this->ioContext = std::make_unique<boost::asio::io_context>();
    this->asyncPipe = std::make_unique<boost::process::async_pipe>(*this->ioContext);
    this->buffer = std::make_unique<boost::asio::streambuf>();

    // Select the appropriate event and metric file paths based on processor type
    const char* eventfilePath =
        this->isAmd ? PerfspectConstants::eventfilePathAmd : PerfspectConstants::eventfilePathIntel;
    const char* metricfilePath =
        this->isAmd ? PerfspectConstants::metricfilePathAmd : PerfspectConstants::metricfilePathIntel;

    this->scriptProcess = std::make_unique<boost::process::child>(
        fullBinaryPath.string(), PerfspectConstants::command, PerfspectConstants::eventfileFlag, eventfilePath,
        PerfspectConstants::metricfileFlag, metricfilePath, PerfspectConstants::intervalFlag,
        PerfspectConstants::intervalValue, PerfspectConstants::liveFlag, boost::process::std_out > *this->asyncPipe,
        boost::process::std_err > boost::process::null);

    // Start async reading
    AsyncRead();

    atlasagent::Logger()->info("Perfspect process started successfully with PID: {}", this->scriptProcess->id());
    return true;
}
catch (const std::exception& e)
{
    atlasagent::Logger()->error("Failed to start PerfSpect: {}", e.what());
    CleanupProcess();
    return false;
}

bool Perfspect::CleanupProcess() try
{
    if (this->scriptProcess && this->scriptProcess->running())
    {
        std::error_code ec;
        this->scriptProcess->terminate(ec);
        if (ec)
        {
            atlasagent::Logger()->debug("Failed to terminate PerfSpect process: {}", ec.message());
            return false;
        }

        this->scriptProcess->wait(ec);
        if (ec)
        {
            atlasagent::Logger()->debug("Failed to wait for PerfSpect process termination: {}", ec.message());
            return false;
        }
    }

    this->scriptProcess.reset();
    this->ioContext.reset();
    this->asyncPipe.reset();
    this->buffer.reset();
    this->pendingLine.clear();
    this->firstIteration = true;
    return true;
}
catch (const std::exception& e)
{
    atlasagent::Logger()->error("Failed to cleanup child process", e.what());
    return false;
}

void Perfspect::ExtractLine(const boost::system::error_code& ec, std::size_t bytes_transferred)
{
    this->lastAsyncError = ec;
    if (this->lastAsyncError.failed())
    {
        atlasagent::Logger()->error("Async read failed: {}", lastAsyncError.message());
        return;
    }

    std::istream is(this->buffer.get());
    std::string line;
    std::getline(is, line);
    if (this->firstIteration)  // skip the first line which is the header line from perfspect output
    {
        this->firstIteration = false;
        atlasagent::Logger()->debug("Skipping header line: {}", line);
        AsyncRead();
        return;
    }

    // Assert no bytes remain in buffer after single line extraction
    // PerfSpect is only emitting at 5 second intervals
    if (this->buffer->in_avail() != 0)
    {
        atlasagent::Logger()->error("Buffer still has {} bytes available after line extraction, expected 0",
                                    this->buffer->in_avail());
    }

    this->pendingLine = std::move(line);
    atlasagent::Logger()->debug("Extracted line: {}", this->pendingLine);
    AsyncRead();
}

void Perfspect::AsyncRead()
{
    // This function initiates an asynchronous read from the PerfSpect process output.
    // The I/O behavior is expected to follow a specific pattern:
    // 1. Initial Output: PerfSpect first emits a two-line burst containing a header
    //    followed by the first data line ("header\ndata\n"). We handle this by
    //    skipping the header and processing the data line.
    // 2. Subsequent Output: After initialization, PerfSpect is configured to emit a single
    //    data line every 5 seconds.
    //
    // IMPORTANT: This implementation is not robust against data bursts. If multiple lines
    // arrive in quick succession, `ioContext->poll()` will execute all ready handlers
    // sequentially within the same call. This will cause the `pendingLine` member to be
    // overwritten, resulting in the loss of all but the last metric from the burst.
    // The code's correctness fundamentally relies on the assumption that PerfSpect
    // only emits a single line of data per interval.
    boost::asio::async_read_until(*this->asyncPipe, *this->buffer, '\n',
                                  [this](const boost::system::error_code& ec, std::size_t bytes_transferred)
                                  { this->ExtractLine(ec, bytes_transferred); });
}

ReadResult Perfspect::ReadOutput()
{
    // If there are no completed async reads, this will return immediately without blocking
    this->ioContext->poll();
    if (this->lastAsyncError.failed() || this->pendingLine.empty() == true)
    {
        return {std::nullopt, this->lastAsyncError};
    }

    ReadResult result{std::move(this->pendingLine), this->lastAsyncError};
    return result;
}

std::optional<PerfspectData> ParsePerfspectLine(const std::string& line) try
{
    std::vector<std::string_view> parts;
    std::string_view sv(line);
    size_t start = 0;
    size_t pos = 0;

    // Split the csv data at each comma
    while ((pos = sv.find(',', start)) != std::string_view::npos)
    {
        parts.emplace_back(sv.substr(start, pos - start));
        start = pos + 1;
    }
    parts.emplace_back(sv.substr(start));

    // PerfSpect is expected to emit exactly 8 comma-delimited metrics.
    // If this count changes, it indicates the nflx-perfspect binary has been updated
    // and our parsing logic may no longer be compatible with the new output format.
    if (parts.size() != 8)
    {
        return std::nullopt;
    }

    PerfspectData data{};

    auto res1 = std::from_chars(parts[4].data(), parts[4].data() + parts[4].size(), data.cpuFrequency);
    auto res2 = std::from_chars(parts[5].data(), parts[5].data() + parts[5].size(), data.cyclesPerSecond);
    auto res3 = std::from_chars(parts[6].data(), parts[6].data() + parts[6].size(), data.instructionsPerSecond);
    auto res4 = std::from_chars(parts[7].data(), parts[7].data() + parts[7].size(), data.l2CacheMissesPerSecond);

    if (res1.ec != std::errc() || res2.ec != std::errc() || res3.ec != std::errc() || res4.ec != std::errc())
    {
        return std::nullopt;
    }

    return data;
}
catch (const std::exception& e)
{
    atlasagent::Logger()->error("Exception parsing PerfSpect line: {}", e.what());
    return std::nullopt;
}

void Perfspect::SendMetrics(const PerfspectData& data)
{
    atlasagent::Logger()->debug(
        "Sending: Frequency: {}, Cycles/sec: {} * 5 = {}, Instructions/sec: {} * 5 = {}, L2 Cache Misses/sec: {} * 5 = "
        "{}",
        data.cpuFrequency, data.cyclesPerSecond, data.cyclesPerSecond * 5, data.instructionsPerSecond,
        data.instructionsPerSecond * 5, data.l2CacheMissesPerSecond, data.l2CacheMissesPerSecond * 5);

    this->cpuFrequencyGauge.Set(data.cpuFrequency);
    this->cyclesCounter.Increment(data.cyclesPerSecond * PerfspectConstants::interval);
    this->instructionsCounter.Increment(data.instructionsPerSecond * PerfspectConstants::interval);
    this->l2CacheMissesCounter.Increment(data.l2CacheMissesPerSecond * PerfspectConstants::interval);
}

bool Perfspect::GatherMetrics()
{
    // If the perfspect process has not been started or is no longer running start it
    if (!this->scriptProcess || this->scriptProcess->running() == false)
    {
        atlasagent::Logger()->debug("No active PerfSpect process found, starting a new one");
        this->StartScript();
        return true;
    }

    atlasagent::Logger()->debug("Checking for PerfSpect metrics...");
    auto result = this->ReadOutput();

    // If there was an error reading, restart the PerfSpect process we cant handle a corrupted stream
    if (result.error.failed())
    {
        atlasagent::Logger()->error("Failed to read PerfSpect output: {}", result.error.message());
        bool restartSuccess = this->StartScript();
        atlasagent::Logger()->debug("Restarting PerfSpect process: {}", restartSuccess ? "Success" : "Failed");
        return false;
    }

    // PerfSpect requires 4-7 seconds to startup
    // We may try to GatherMetrics and read from the stream 
    // before it has produced any output, so we need to handle that case gracefully
    if (result.data.has_value() == false)
    {
        atlasagent::Logger()->debug("No new perfspect output available yet");
        return true;
    }

    // Parse the output line into a struct of data we can report
    auto parsedDataOpt = ParsePerfspectLine(result.data.value());
    if (parsedDataOpt.has_value() == false)
    {
        atlasagent::Logger()->error("Failed to parse perfspect output line: {}", result.data.value());
        return false;
    }

    SendMetrics(parsedDataOpt.value());
    return true;
}