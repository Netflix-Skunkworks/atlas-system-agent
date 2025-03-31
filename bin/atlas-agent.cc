#ifdef __linux__
#include "../contain/contain.h"
#include <sys/vfs.h>
#endif
#include "../lib/aws.h"
#include "../lib/cgroup.h"
#include "../lib/cpufreq.h"
#include "../lib/dcgm_stats.h"
#include "../lib/disk.h"
#include "../lib/ethtool.h"
#include "../lib/gpumetrics.h"
#include "../lib/ntp.h"
#include "../lib/perfmetrics.h"
#include "../lib/pressure_stall.h"
#include "../lib/proc.h"
#include "../lib/service_monitor.h"
#include "../lib/util.h"
#include "backward.hpp"
#include <condition_variable>
#include <csignal>
#include <fmt/chrono.h>
#include <getopt.h>
#include <random>

using atlasagent::GetLogger;
using atlasagent::Logger;
using atlasagent::Nvml;

using atlasagent::TaggingRegistry;
using Aws = atlasagent::Aws<>;
using CGroup = atlasagent::CGroup<>;
using CpuFreq = atlasagent::CpuFreq<>;
using Disk = atlasagent::Disk<>;
using Ethtool = atlasagent::Ethtool<>;
using GpuMetrics = atlasagent::GpuMetrics<TaggingRegistry, Nvml>;
using Ntp = atlasagent::Ntp<>;
using PerfMetrics = atlasagent::PerfMetrics<>;
using PressureStall = atlasagent::PressureStall<>;
using Proc = atlasagent::Proc<>;

std::unique_ptr<GpuMetrics> init_gpu(TaggingRegistry* registry, std::unique_ptr<Nvml> lib) {
  if (lib) {
    try {
      lib->initialize();
      return std::make_unique<GpuMetrics>(registry, std::move(lib));
    } catch (atlasagent::NvmlException& e) {
      fprintf(stderr, "Will not collect GPU metrics: %s\n", e.what());
    }
  }
  return {};
}

#if defined(TITUS_SYSTEM_SERVICE)
static void gather_peak_titus_metrics(CGroup* cGroup) { cGroup->cpu_peak_stats(); }

static void gather_slow_titus_metrics(CGroup* cGroup, Proc* proc, Disk* disk, Aws* aws) {
  aws->update_stats();
  cGroup->cpu_stats();
  cGroup->memory_stats_v2();
  cGroup->memory_stats_std_v2();
  cGroup->network_stats();
  disk->titus_disk_stats();
  proc->netstat_stats();
  proc->network_stats();
  proc->process_stats();
  proc->snmp_stats();
  proc->uptime_stats();
}
#else
static void gather_peak_system_metrics(Proc* proc) { proc->peak_cpu_stats(); }

static void gather_scaling_metrics(CpuFreq* cpufreq) { cpufreq->Stats(); }

static void gather_slow_system_metrics(Proc* proc, Disk* disk, Ethtool* ethtool, Ntp* ntp,
                                       PressureStall* pressureStall, Aws* aws) {
  aws->update_stats();
  disk->disk_stats();
  ethtool->update_stats();
  ntp->update_stats();
  pressureStall->update_stats();
  proc->arp_stats();
  proc->cpu_stats();
  proc->loadavg_stats();
  proc->memory_stats();
  proc->netstat_stats();
  proc->network_stats();
  proc->process_stats();
  proc->snmp_stats();
  proc->socket_stats();
  proc->uptime_stats();
  proc->vmstats();
}
#endif

struct terminator {
  terminator() noexcept = default;

  // returns false if killed:
  template <class R, class P>
  bool wait_for(std::chrono::duration<R, P> const& time) {
    if (time.count() <= 0) {
      Logger()->warn("waiting for zero ticks!");
      return true;
    }
    std::unique_lock<std::mutex> lock(m);
    return !cv.wait_for(lock, time, [&] { return terminate; });
  }
  void kill() {
    std::unique_lock<std::mutex> lock(m);
    terminate = true;
    cv.notify_all();
  }

 private:
  std::condition_variable cv;
  std::mutex m;
  bool terminate = false;
};

terminator runner;

static void handle_signal(int signal) {
  const char* name;
  switch (signal) {
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

static void init_signals() {
  struct sigaction sa {};
  sa.sa_handler = &handle_signal;
  sa.sa_flags = SA_RESETHAND;  // remove the handler after the first signal
  sigfillset(&sa.sa_mask);
  sigaction(SIGINT, &sa, nullptr);
  sigaction(SIGTERM, &sa, nullptr);
}

long initial_polling_delay() {
  std::random_device rdev;
  std::mt19937 generator(rdev());

  // calculate previous step boundary using integer arithmetic, to determine start second
  auto now = std::chrono::system_clock::now();
  auto now_epoch = now.time_since_epoch();
  auto epoch = std::chrono::duration_cast<std::chrono::seconds>(now_epoch);
  long step_boundary = epoch.count() / 60 * 60;
  long start_second = epoch.count() - step_boundary;

  Logger()->debug("epoch={} step_boundary={} start_second={}", epoch.count(), step_boundary,
                  start_second);

  if (start_second < 10) {
    std::uniform_int_distribution<long> start_delay_dist(10 - start_second, 50 - start_second);
    return start_delay_dist(generator);
  } else if (start_second > 50) {
    auto next_min = 60 - start_second;
    std::uniform_int_distribution<long> start_delay_dist(10, 50);
    return next_min + start_delay_dist(generator);
  } else {
    return 0;
  }
}

#if defined(TITUS_SYSTEM_SERVICE)
void collect_titus_metrics(TaggingRegistry* registry, std::unique_ptr<Nvml> nvidia_lib,
                           spectator::Tags net_tags) {
  using std::chrono::duration_cast;
  using std::chrono::milliseconds;
  using std::chrono::seconds;
  using std::chrono::system_clock;

  Aws aws{registry};
  CGroup cGroup{registry};
  Disk disk{registry, ""};
  PerfMetrics perf_metrics{registry, ""};
  Proc proc{registry, std::move(net_tags)};

  auto gpu = init_gpu(registry, std::move(nvidia_lib));

  // initial polling delay, to prevent publishing too close to a minute boundary
  auto delay = initial_polling_delay();
  Logger()->info("Initial polling delay is {}s", delay);
  if (delay > 0) {
    runner.wait_for(seconds(delay));
  }

  // the first call to this gather function takes ~100ms, so it must be
  // done before we start calculating times to wait for peak metrics
  gather_slow_titus_metrics(&cGroup, &proc, &disk, &aws);
  Logger()->info("Published slow Titus metrics (first iteration)");

  auto now = system_clock::now();
  auto next_run = now;
  auto next_slow_run = now + seconds(60);
  std::chrono::nanoseconds time_to_sleep;

  do {
    auto start = system_clock::now();
    gather_peak_titus_metrics(&cGroup);

    if (start >= next_slow_run) {
      gather_slow_titus_metrics(&cGroup, &proc, &disk, &aws);
      perf_metrics.collect();
      if (gpu) {
        gpu->gpu_metrics();
      }
      auto elapsed = duration_cast<milliseconds>(system_clock::now() - start);
      Logger()->info("Published Titus metrics (delay={})", elapsed);
      next_slow_run += seconds(60);
    }

    next_run += seconds(1);
    time_to_sleep = next_run - system_clock::now();
  } while (runner.wait_for(time_to_sleep));
}
#else
void collect_system_metrics(TaggingRegistry* registry, std::unique_ptr<atlasagent::Nvml> nvidia_lib,
                            const spectator::Tags& net_tags, const int& max_monitored_services) {
  using std::chrono::duration_cast;
  using std::chrono::milliseconds;
  using std::chrono::seconds;
  using std::chrono::system_clock;

  Aws aws{registry};
  CpuFreq cpufreq{registry};
  Disk disk{registry, ""};
  Ethtool ethtool{registry, net_tags};
  Ntp ntp{registry};
  PerfMetrics perf_metrics{registry, ""};
  PressureStall pressureStall{registry};
  Proc proc{registry, net_tags};

  auto gpu = init_gpu(registry, std::move(nvidia_lib));

  std::optional<GpuMetricsDCGM<TaggingRegistry> > gpuDCGM{std::nullopt};
  if (atlasagent::is_file_present(DCGMConstants::dcgmiPath)) {
    gpuDCGM.emplace(registry);
  }

  /* To Do: DCGM & ServiceMonitor have Dynamic metric collection. During each iteration we have to
  check if these optionals have a set value. lets improve how we handle this */
  std::optional<ServiceMonitor<TaggingRegistry> > serviceMetrics{};
  std::optional<std::vector<std::regex> > serviceConfig{
      parse_service_monitor_config_directory(ServiceMonitorConstants::ConfigPath)};
  if (serviceConfig.has_value()) {
    serviceMetrics.emplace(registry, serviceConfig.value(), max_monitored_services);
  }

  if (gpuDCGM.has_value()) {
    std::string serviceStatus =
        atlasagent::is_service_running(DCGMConstants::ServiceName) ? "ON" : "OFF";
    Logger()->info(
        "DCGMI binary present. Agent will collect DCGM metrics if service is ON. DCGM service "
        "state: {}.",
        serviceStatus);
  } else {
    Logger()->info("DCGMI binary not present. Agent will not collect DCGM metrics.");
  }

  // initial polling delay, to prevent publishing too close to a minute boundary
  auto delay = initial_polling_delay();
  Logger()->info("Initial polling delay is {}s", delay);
  if (delay > 0) {
    runner.wait_for(seconds(delay));
  }

  // the first call to this gather function takes ~100ms, so it must be
  // done before we start calculating times to wait for peak metrics
  gather_slow_system_metrics(&proc, &disk, &ethtool, &ntp, &pressureStall, &aws);
  Logger()->info("Published slow system metrics (first iteration)");

  auto now = system_clock::now();
  auto next_run = now;
  auto next_slow_run = now + seconds(60);
  std::chrono::nanoseconds time_to_sleep;

  do {
    auto start = system_clock::now();
    gather_peak_system_metrics(&proc);
    gather_scaling_metrics(&cpufreq);

    if (start >= next_slow_run) {
      gather_slow_system_metrics(&proc, &disk, &ethtool, &ntp, &pressureStall, &aws);
      perf_metrics.collect();
      if (gpu) {
        gpu->gpu_metrics();
      }

      if (gpuDCGM.has_value() && atlasagent::is_service_running(DCGMConstants::ServiceName)) {
        if (gpuDCGM.value().gather_metrics() == false) {
          Logger()->error("Failed to gather DCGM metrics");
        }
      }

      if (serviceMetrics.has_value()) {
        if (serviceMetrics.value().gather_metrics() == false) {
          Logger()->error("Failed to gather Service metrics");
        }
      }

      auto elapsed = duration_cast<milliseconds>(system_clock::now() - start);
      Logger()->debug("Published system metrics (delay={})", elapsed);
      next_slow_run += seconds(60);
    }

    next_run += seconds(1);
    time_to_sleep = next_run - system_clock::now();
  } while (runner.wait_for(time_to_sleep));
}
#endif

struct agent_options {
  spectator::Tags network_tags;
  std::string cfg_file;
  bool verbose;
  unsigned int max_monitored_services{0};
};

static constexpr const char* const kDefaultCfgFile = "/etc/default/atlas-agent.json";

static void usage(const char* progname) {
  fprintf(stderr,
          "Usage: %s [-c cfg_file] [-s monitored-service-threshold][-v] [-t extra-network-tags]\n"
          "\t-c\tUse cfg_file as the configuration file. Default %s\n"
          "\t-v\tBe very verbose\n"
          "\t-t tags\tAdd extra tags to the network metrics.\n"
          "\t\tExpects a string of the form key=val,key2=val2\n",
          progname, kDefaultCfgFile);
  exit(EXIT_FAILURE);
}

static int parse_options(int& argc, char* const argv[], agent_options* result) {
  result->verbose = std::getenv("VERBOSE_AGENT") != nullptr;  // default for backwards compat

  int ch;
  while ((ch = getopt(argc, argv, "c:vt:")) != -1) {
    switch (ch) {
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
        result->max_monitored_services = std::stoi(optarg);
        if (result->max_monitored_services < 0) {
          fprintf(stderr, "Invalid value for -s: %s\n", optarg);
          usage(argv[0]);
        }
      case '?':
      default:
        usage(argv[0]);
    }
  }
  if (result->cfg_file.empty()) {
    result->cfg_file = kDefaultCfgFile;
  }
  return optind;
}

int main(int argc, char* const argv[]) {
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
  auto cfg = spectator::Config{"unix:/run/spectatord/spectatord.unix", std::move(common_tags)};

#if defined(TITUS_SYSTEM_SERVICE)
  auto titus_host = std::getenv("TITUS_HOST_EC2_INSTANCE_ID");
  if (titus_host != nullptr && titus_host[0] != '\0') {
    cfg.common_tags["titus.host"] = titus_host;
  }
#endif

  auto spectator_logger = GetLogger("spectator");
  auto logger = Logger();
  if (options.verbose) {
    spectator_logger->set_level(spdlog::level::debug);
    logger->set_level(spdlog::level::debug);
  }

  std::unique_ptr<Nvml> nvidia_lib;
  try {
    nvidia_lib = std::make_unique<Nvml>();
    logger->info("Will attempt to collect GPU metrics");
  } catch (atlasagent::NvmlException& e) {
    logger->info("Will not collect GPU metrics: {}", e.what());
  }

  atlasagent::HttpClient<>::GlobalInit();
  auto maybe_tagger = atlasagent::Tagger::FromConfigFile(options.cfg_file.c_str());
  if (!maybe_tagger) {
    logger->warn("Unable to load Tagger from config file {}. Ignoring", options.cfg_file);
  }
  spectator::Registry spectator_registry{cfg, spectator_logger};
  TaggingRegistry registry{&spectator_registry, maybe_tagger.value_or(atlasagent::Tagger::Nop())};
#if defined(TITUS_SYSTEM_SERVICE)
  Logger()->info("Start gathering Titus system metrics");
  collect_titus_metrics(&registry, std::move(nvidia_lib), options.network_tags);
#else
  Logger()->info("Start gathering EC2 system metrics");
  collect_system_metrics(&registry, std::move(nvidia_lib), options.network_tags,
                         options.max_monitored_services);
#endif
  logger->info("Shutting down spectator registry");
  atlasagent::HttpClient<>::GlobalShutdown();
  return 0;
}