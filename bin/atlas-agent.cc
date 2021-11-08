#include "config.h"
#ifdef __linux__
#include "contain/contain.h"
#endif
#include "aws.h"
#include "cgroup.h"
#include "cpufreq.h"
#include "disk.h"
#include "gpumetrics.h"
#include "logger.h"
#include "ntp.h"
#include "nvml.h"
#include "perfmetrics.h"
#include "proc.h"
#include "tagger.h"
#include "backward.hpp"
#include "spectator/registry.h"
#include <cinttypes>
#include <condition_variable>
#include <csignal>
#include <getopt.h>

using atlasagent::GetLogger;
using atlasagent::Logger;
using atlasagent::Nvml;

using atlasagent::TaggingRegistry;
using Aws = atlasagent::Aws<>;
using CGroup = atlasagent::CGroup<>;
using CpuFreq = atlasagent::CpuFreq<>;
using Disk = atlasagent::Disk<>;
using GpuMetrics = atlasagent::GpuMetrics<TaggingRegistry, Nvml>;
using Ntp = atlasagent::Ntp<>;
using PerfMetrics = atlasagent::PerfMetrics<>;
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
  return std::unique_ptr<GpuMetrics>();
}

#if defined(TITUS_AGENT) || defined(TITUS_SYSTEM_SERVICE)
static void gather_peak_titus_metrics(CGroup* cGroup) { cGroup->cpu_peak_stats(); }

static void gather_slow_titus_metrics(CGroup* cGroup, Proc* proc, Disk* disk, Aws* aws) {
  Logger()->info("Gathering titus metrics");
  aws->update_stats();
  cGroup->cpu_stats();
  cGroup->memory_stats();
  cGroup->memory_stats_std();
  cGroup->network_stats();
  disk->titus_disk_stats();
  proc->netstat_stats();
  proc->network_stats();
  proc->process_stats();
  proc->snmp_stats();
}
#else
static void gather_peak_system_metrics(Proc* proc) { proc->peak_cpu_stats(); }

static void gather_scaling_metrics(CpuFreq* cpufreq) { cpufreq->Stats(); }

static void gather_slow_system_metrics(Proc* proc, Disk* disk, Ntp* ntp, Aws* aws) {
  Logger()->info("Gathering system metrics");
  aws->update_stats();
  disk->disk_stats();
  ntp->update_stats();
  proc->arp_stats();
  proc->cpu_stats();
  proc->loadavg_stats();
  proc->memory_stats();
  proc->netstat_stats();
  proc->network_stats();
  proc->process_stats();
  proc->snmp_stats();
  proc->socket_stats();
  proc->vmstats();
}
#endif

struct terminator {
  terminator() noexcept {}

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
  struct sigaction sa;
  sa.sa_handler = &handle_signal;
  sa.sa_flags = SA_RESETHAND;  // remove the handler after the first signal
  sigfillset(&sa.sa_mask);
  sigaction(SIGINT, &sa, nullptr);
  sigaction(SIGTERM, &sa, nullptr);
}

#if defined(TITUS_AGENT) || defined(TITUS_SYSTEM_SERVICE)
void collect_titus_metrics(TaggingRegistry* registry, std::unique_ptr<Nvml> nvidia_lib,
                           spectator::Tags net_tags) {
  using std::chrono::seconds;
  using std::chrono::system_clock;

  Aws aws{registry};
  CGroup cGroup{registry};
  Disk disk{registry, ""};
#ifndef TITUS_SYSTEM_SERVICE
  PerfMetrics perf_metrics{registry, ""};
#endif
  Proc proc{registry, std::move(net_tags)};

  auto gpu = init_gpu(registry, std::move(nvidia_lib));

  // the first call to this gather function takes >1 second, so it must
  // be done before we start calculating times to wait for peak metrics
  gather_slow_titus_metrics(&cGroup, &proc, &disk, &aws);

  auto now = system_clock::now();
  auto next_run = now;
  auto next_slow_run = now + seconds(60);
  std::chrono::nanoseconds time_to_sleep;

  do {
    gather_peak_titus_metrics(&cGroup);
    if (system_clock::now() >= next_slow_run) {
      gather_slow_titus_metrics(&cGroup, &proc, &disk, &aws);
#ifndef TITUS_SYSTEM_SERVICE
      perf_metrics.collect();
#endif
      if (gpu) {
        gpu->gpu_metrics();
      }
      next_slow_run += seconds(60);
    }
    next_run += seconds(1);
    time_to_sleep = next_run - system_clock::now();
  } while (runner.wait_for(time_to_sleep));
}
#else
void collect_system_metrics(TaggingRegistry* registry, std::unique_ptr<atlasagent::Nvml> nvidia_lib,
                            spectator::Tags net_tags) {
  using std::chrono::seconds;
  using std::chrono::system_clock;

  Aws aws{registry};
  CpuFreq cpufreq{registry};
  Disk disk{registry, ""};
  Ntp ntp{registry};
  PerfMetrics perf_metrics{registry, ""};
  Proc proc{registry, std::move(net_tags)};

  auto gpu = init_gpu(registry, std::move(nvidia_lib));

  // the first call to this gather function takes >1 second, so it must
  // be done before we start calculating times to wait for peak metrics
  gather_slow_system_metrics(&proc, &disk, &ntp, &aws);

  auto now = system_clock::now();
  auto next_run = now;
  auto next_slow_run = now + seconds(60);
  std::chrono::nanoseconds time_to_sleep;

  do {
    gather_peak_system_metrics(&proc);
    gather_scaling_metrics(&cpufreq);
    if (system_clock::now() >= next_slow_run) {
      gather_slow_system_metrics(&proc, &disk, &ntp, &aws);
      perf_metrics.collect();
      if (gpu) {
        gpu->gpu_metrics();
      }
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
};

static constexpr const char* const kDefaultCfgFile = "/etc/default/atlas-agent.json";

static void usage(const char* progname) {
  fprintf(stderr,
          "Usage: %s [-c cfg_file] [-v] [-t extra-network-tags]\n"
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

#ifdef TITUS_AGENT
  container_handle c;
  if (maybe_reexec(argv)) {
    return 1;
  }
#endif

  auto idx = parse_options(argc, argv, &options);
  assert(idx >= 0);
  argc -= idx;
  argv += idx;

  std::unique_ptr<Nvml> nvidia_lib;
  try {
    nvidia_lib = std::make_unique<Nvml>();
    fprintf(stderr, "Will attempt to collect GPU metrics\n");
  } catch (atlasagent::NvmlException& e) {
    fprintf(stderr, "Will not collect GPU metrics: %s\n", e.what());
  }

#ifdef TITUS_AGENT
  if (maybe_contain(&c) != 0) {
    return 1;
  }
#endif

#if defined(TITUS_AGENT) || defined(TITUS_SYSTEM_SERVICE)
  const char* process = argc > 1 ? argv[1] : "atlas-titus-agent";
#else
  const char* process = argc > 1 ? argv[1] : "atlas-system-agent";
#endif

  init_signals();
  backward::SignalHandling sh;
  std::unordered_map<std::string, std::string> common_tags{{"xatlas.process", process}};
  auto cfg = spectator::Config{"unix:/run/spectatord/spectatord.unix", std::move(common_tags)};

#if defined(TITUS_AGENT) || defined(TITUS_SYSTEM_SERVICE)
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
  atlasagent::HttpClient<>::GlobalInit();
  auto maybe_tagger = atlasagent::Tagger::FromConfigFile(options.cfg_file.c_str());
  if (!maybe_tagger) {
    logger->warn("Unable to load Tagger from config file {}. Ignoring", options.cfg_file);
  }
  spectator::Registry spectator_registry{cfg, spectator_logger};
  TaggingRegistry registry{&spectator_registry, maybe_tagger.value_or(atlasagent::Tagger::Nop())};
#if defined(TITUS_AGENT) || defined(TITUS_SYSTEM_SERVICE)
  collect_titus_metrics(&registry, std::move(nvidia_lib), std::move(options.network_tags));
#else
  collect_system_metrics(&registry, std::move(nvidia_lib), std::move(options.network_tags));
#endif
  logger->info("Shutting down spectator registry");
  atlasagent::HttpClient<>::GlobalShutdown();
  return 0;
}
