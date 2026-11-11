#include <iostream>
#include <fstream>

#include <absl/strings/str_split.h>
#include "dcgm_stats.h"
#include <lib/util/src/util.h>

#include <boost/asio.hpp>
#include <chrono>

using atlasagent::GetLogger;
using atlasagent::Logger;
using spectator::Registry;

// Why this exists: the DCGM systemd service is not the only way nv-hostengine gets started. It can also be launched
// directly or by something other than systemd, and on those hosts checking the service alone would leave DCGM
// metrics off even though dcgmi works. So this asks the host engine itself.
//
// nv-hostengine counts as reachable when it accepts a TCP connection on the address dcgmi uses. Connecting to it
// directly, rather than running dcgmi to find out, is deliberate: dcgmi does not fail fast. With no host engine it
// retries the connection for 5 seconds, which would stall the agent's single-threaded collection loop that long on
// every cycle. A connection to the local host is accepted or refused at once, and starts no process.
//
// This shows only that something is listening on the port. Whether it is a working host engine is settled by the
// dcgmi call in gather_metrics(), which logs its own failure.
//
// The address and port are parameters only so that a test can point this at a socket of its own.
inline bool is_hostengine_reachable(const char* address = DCGMConstants::HostEngineAddress,
                                    int port = DCGMConstants::HostEnginePort)
try
{
    // Stays an error unless the connection completes in the time allowed
    boost::system::error_code outcome = boost::asio::error::timed_out;

    boost::asio::io_context io;
    boost::asio::ip::tcp::socket socket(io);
    const boost::asio::ip::tcp::endpoint target(boost::asio::ip::make_address(address),
                                                static_cast<unsigned short>(port));

    // Started asynchronously and run for a bounded time, so that a host engine which has stopped answering cannot
    // hold up the collection loop. An attempt still pending when the time is up is abandoned as the socket closes.
    socket.async_connect(target, [&outcome](const boost::system::error_code& error) { outcome = error; });
    io.run_for(std::chrono::milliseconds(DCGMConstants::HostEngineConnectTimeoutMillis));
    return !outcome;
}
catch (const std::exception& e)
{
    atlasagent::Logger()->error("Exception thrown in is_hostengine_reachable: {}", e.what());
    return false;
}

std::optional<GpuMetricsDCGM> GpuMetricsDCGM::Create(Registry* registry)
{
    if (!atlasagent::is_file_present(DCGMConstants::dcgmiPath))
    {
        Logger()->info("DCGMI binary not present. Agent will not collect DCGM metrics.");
        return std::nullopt;
    }
    std::string serviceStatus = atlasagent::is_service_running(DCGMConstants::ServiceName) ? "ON" : "OFF";
    std::string hostEngineStatus = is_hostengine_reachable() ? "REACHABLE" : "UNREACHABLE";
    Logger()->info(
        "DCGMI binary present. Agent will collect DCGM metrics if service is ON or nv-hostengine is REACHABLE. "
        "DCGM service state: {}. nv-hostengine state: {}.",
        serviceStatus, hostEngineStatus);
    return std::optional<GpuMetricsDCGM>{std::in_place, registry};
}

void GpuMetricsDCGM::Collect(std::optional<GpuMetricsDCGM>& self)
{
    // Either signal is enough to collect: the DCGM service being active, or nv-hostengine accepting connections when
    // it is not run by that service.
    if (self.has_value() && (atlasagent::is_service_running(DCGMConstants::ServiceName) || is_hostengine_reachable()))
    {
        if (self->gather_metrics() == false)
        {
            Logger()->error("Failed to gather DCGM metrics");
        }
    }
}

bool parse_lines(const std::vector<std::string>& lines, std::map<int, std::vector<double>>& dataMap)
try
{
    if (lines.size() < DCGMConstants::RequiredLines)
    {
        return false;
    }

    for (unsigned int i = DCGMConstants::DataStartLineIndex; i < lines.size(); i++)
    {
        auto line = lines.at(i);
        std::vector<std::string> tokens = absl::StrSplit(line, ' ', absl::SkipWhitespace());

        if (tokens.size() != DCGMConstants::ExpectedCountOfTokens)
        {
            return false;
        }

        auto gpuId = std::stoi(tokens.at(DCGMConstants::GPUIdTokenIndex));
        for (unsigned int j = DCGMConstants::DataStartTokenIndex; j < tokens.size(); j++)
        {
            dataMap[gpuId].push_back(std::stod(tokens[j]));
        }

        if (dataMap[gpuId].size() != DCGMConstants::ExpectedCountOfProfileValues)
        {
            return false;
        }
    }
    return true;
}
catch (const std::exception& e)
{
    atlasagent::Logger()->error("Exception thrown in ParseLines: {}", e.what());
    return false;
}

inline std::vector<std::string> execute_dcgmi()
try
{
    static const auto command = std::string(DCGMConstants::dcgmiPath) + " " + std::string(DCGMConstants::dcgmiArgs);
    return atlasagent::read_output_lines(command.data(), 5000);
}
catch (const std::exception& e)
{
    atlasagent::Logger()->error("Exception thrown in ExecuteDCGMI: {}", e.what());
    return std::vector<std::string>();
}

bool GpuMetricsDCGM::update_metrics(std::map<int, std::vector<double>>& dataMap)
{
    if (this->registry_ == nullptr)
    {
        return false;
    }
    for (const auto& [gpuId, data] : dataMap)
    {
        for (unsigned int i = 0; i < data.size(); i++)
        {
            double value = data.at(i);
            switch (i)
            {
                case 0:
                    detail::gauge(registry_, "gpu.dcgm.graphicsEngineActivity", gpuId)
                        .Set(value * DCGMConstants::PercentileConversion);
                    break;
                case 1:
                    detail::gauge(registry_, "gpu.dcgm.sm", gpuId, "activity")
                        .Set(value * DCGMConstants::PercentileConversion);
                    break;
                case 2:
                    detail::gauge(registry_, "gpu.dcgm.sm", gpuId, "occupancy")
                        .Set(value * DCGMConstants::PercentileConversion);
                    break;
                case 3:
                    detail::gauge(registry_, "gpu.dcgm.tensorCoresUtilization", gpuId)
                        .Set(value * DCGMConstants::PercentileConversion);
                    break;
                case 4:
                    detail::gauge(registry_, "gpu.dcgm.memoryBandwidthUtilization", gpuId)
                        .Set(value * DCGMConstants::PercentileConversion);
                    break;
                case 5:
                    detail::gauge(registry_, "gpu.dcgm.pipeUtilization", gpuId, "fp32")
                        .Set(value * DCGMConstants::PercentileConversion);
                    break;
                case 6:
                    detail::gauge(registry_, "gpu.dcgm.pipeUtilization", gpuId, "fp16")
                        .Set(value * DCGMConstants::PercentileConversion);
                    break;
                case 7:
                    detail::counter(registry_, "gpu.dcgm.pcie.bytes", gpuId, "out")
                        .Increment(value * DCGMConstants::BytesConversion);
                    break;
                case 8:
                    detail::counter(registry_, "gpu.dcgm.pcie.bytes", gpuId, "in")
                        .Increment(value * DCGMConstants::BytesConversion);
                    break;
                case 9:
                    detail::counter(registry_, "gpu.dcgm.nvlink.bytes", gpuId, "out")
                        .Increment(value * DCGMConstants::BytesConversion);
                    break;
                case 10:
                    detail::counter(registry_, "gpu.dcgm.nvlink.bytes", gpuId, "in")
                        .Increment(value * DCGMConstants::BytesConversion);
                    break;
                default:
                    Logger()->error("Unhandled field type provided in update_metrics.");
                    break;
            }
        }
    }
    return true;
}

bool GpuMetricsDCGM::gather_metrics()
{
    Logger()->debug("Attempting to gather DCGM metrics");

    auto lines = execute_dcgmi();

    std::map<int, std::vector<double>> dataMap;

    if (false == parse_lines(lines, dataMap))
    {
        Logger()->error("Failure to parse DCGMI output");
        return false;
    }

    if (false == update_metrics(dataMap))
    {
        Logger()->error("Failure to update DCGMI metrics in registry");
        return false;
    }

    return true;
}