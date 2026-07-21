// Entry point for the Atlas agent. This file holds the pieces common to both
// build flavors — option parsing, signal handling, and the helpers shared by
// the collector loops. The per-flavor collection logic lives in
// system-agent.cpp / titus-agent.cpp (only one is compiled per build, selected
// by TITUS_SYSTEM_SERVICE); the shared declarations live in atlas-agent.h.

#include "atlas-agent.h"

#include <lib/http_client/src/http_client.h>
#include <lib/util/src/util.h>

#include "backward.hpp"

#include <cassert>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <getopt.h>
#include <random>
#include <utility>

terminator runner;

static void handle_signal(int signal)
{
    const char* name;
    switch (signal)
    {
        case SIGINT:
            name = "SIGINT";
            break;
        case SIGTERM:
            name = "SIGTERM";
            break;
        default:
            name = "Unknown";
    }

    Logger()->info("Caught {}, cleaning up", name);
    runner.kill();
}

static void init_signals()
{
    struct sigaction sa
    {
    };
    sa.sa_handler = &handle_signal;
    sa.sa_flags = SA_RESETHAND;  // remove the handler after the first signal
    sigfillset(&sa.sa_mask);
    sigaction(SIGINT, &sa, nullptr);
    sigaction(SIGTERM, &sa, nullptr);
}

long initial_polling_delay()
{
    std::random_device rdev;
    std::mt19937 generator(rdev());

    // calculate previous step boundary using integer arithmetic, to determine start second
    auto now = std::chrono::system_clock::now();
    auto now_epoch = now.time_since_epoch();
    auto epoch = std::chrono::duration_cast<std::chrono::seconds>(now_epoch);
    long step_boundary = epoch.count() / 60 * 60;
    long start_second = epoch.count() - step_boundary;

    Logger()->debug("epoch={} step_boundary={} start_second={}", epoch.count(), step_boundary, start_second);

    if (start_second < 10)
    {
        std::uniform_int_distribution<long> start_delay_dist(10 - start_second, 50 - start_second);
        return start_delay_dist(generator);
    }
    else if (start_second > 50)
    {
        auto next_min = 60 - start_second;
        std::uniform_int_distribution<long> start_delay_dist(10, 50);
        return next_min + start_delay_dist(generator);
    }
    else
    {
        return 0;
    }
}

struct agent_options
{
    std::unordered_map<std::string, std::string> network_tags;
    std::string cfg_file;
    bool verbose;
    // 0 means "not set" -> the ServiceMonitor constructor falls back to DefaultMonitoredServices.
    unsigned int max_monitored_services{0};
};

static constexpr const char* const kDefaultCfgFile = "/etc/default/atlas-agent.json";

static void usage(const char* progname)
{
    fprintf(stderr,
            "Usage: %s [-c cfg_file] [-s monitored-service-threshold][-v] [-t extra-network-tags]\n"
            "\t-c\tUse cfg_file as the configuration file. Default %s\n"
            "\t-s\tSet the maximum number of monitored services. Default is 10\n"
            "\t-v\tBe very verbose\n"
            "\t-t tags\tAdd extra tags to the network metrics.\n"
            "\t\tExpects a string of the form key=val,key2=val2\n",
            progname, kDefaultCfgFile);
    exit(EXIT_FAILURE);
}

static int parse_options(int& argc, char* const argv[], agent_options* result)
try
{
    result->verbose = std::getenv("VERBOSE_AGENT") != nullptr;  // default for backwards compat

    int ch;
    while ((ch = getopt(argc, argv, "c:vt:s:")) != -1)
    {
        switch (ch)
        {
            case 'c':
                result->cfg_file = optarg;
                break;
            case 'v':
                result->verbose = true;
                break;
            case 't':
                result->network_tags = atlasagent::parse_tags(optarg);
                break;
            case 's':
            {
                // Parse as signed first so a negative value fails the <= 0 check instead of wrapping
                // to a huge unsigned. A non-numeric/overflowing optarg throws std::stoi, which the
                // function-level handler below catches. usage() exits on failure.
                int value = std::stoi(optarg);
                if (value <= 0)
                {
                    fprintf(stderr, "Invalid value for -s: %s\n", optarg);
                    usage(argv[0]);
                }
                result->max_monitored_services = static_cast<unsigned int>(value);
                break;
            }
            case '?':
            default:
                usage(argv[0]);
        }
    }
    if (result->cfg_file.empty())
    {
        result->cfg_file = kDefaultCfgFile;
    }
    return optind;
}
catch (const std::exception& e)
{
    fprintf(stderr, "Invalid command-line options: %s\n", e.what());
    usage(argv[0]);
    return optind;  // unreachable: usage() calls exit()
}

int main(int argc, char* const argv[])
{
    agent_options options{};

    auto idx = parse_options(argc, argv, &options);
    assert(idx >= 0);
    argc -= idx;
    argv += idx;

#if defined(TITUS_SYSTEM_SERVICE)
    const char* process = argc > 1 ? argv[1] : "atlas-titus-agent";
#else
    const char* process = argc > 1 ? argv[1] : "atlas-system-agent";
#endif

    init_signals();
    backward::SignalHandling sh;

    std::unordered_map<std::string, std::string> common_tags{{"xatlas.process", process}};
#if defined(TITUS_SYSTEM_SERVICE)
    auto titus_host = std::getenv("TITUS_HOST_EC2_INSTANCE_ID");
    if (titus_host != nullptr && titus_host[0] != '\0')
    {
        common_tags["titus.host"] = titus_host;
    }
#endif

    auto logger = Logger();
    if (options.verbose)
    {
        logger->set_level(spdlog::level::debug);
        Logger::GetLogger()->set_level(spdlog::level::debug);
    }

    atlasagent::HttpClient::GlobalInit();

    Config config(WriterConfig(WriterTypes::Unix), common_tags);
    Registry registry(config);
#if defined(TITUS_SYSTEM_SERVICE)
    Logger()->info("Start gathering Titus system metrics");
    collect_titus_metrics(&registry, options.network_tags, options.max_monitored_services);
#else
    Logger()->info("Start gathering EC2 system metrics");
    collect_system_metrics(&registry, options.network_tags, options.max_monitored_services);
#endif
    logger->info("Shutting down spectator registry");
    atlasagent::HttpClient::GlobalShutdown();
    return 0;
}
