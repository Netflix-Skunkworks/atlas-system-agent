#include "config.h"
#include "contain/contain.h"
#include "lib/aws.h"
#include "lib/cgroup.h"
#include "lib/disk.h"
#include "lib/gpumetrics.h"
#include "lib/logger.h"
#include "lib/ntp.h"
#include "lib/nvml.h"
#include "lib/perfmetrics.h"
#include "lib/proc.h"
#include <spectator/registry.h>
#include <cinttypes>
#include <condition_variable>
#include <csignal>
#include <spectator/memory.h>

using atlasagent::Aws;
using atlasagent::CGroup;
using atlasagent::Disk;
using atlasagent::GetLogger;
using atlasagent::GpuMetrics;
using atlasagent::Logger;
using atlasagent::Ntp;
using atlasagent::Nvml;
using atlasagent::PerfMetrics;
using atlasagent::Proc;

std::unique_ptr<spectator::Config> GetSpectatorConfig();

std::unique_ptr<GpuMetrics<Nvml>> init_gpu(spectator::Registry* registry) {
  auto gpu = std::unique_ptr<GpuMetrics<Nvml> >(nullptr);
  try {
    gpu.reset(new GpuMetrics<Nvml>(registry, std::make_unique<Nvml>()));
  } catch (const atlasagent::NvmlException& e) {
    auto errmsg = fmt::format("Unable to start collection of GPU metrics: {}", e.what());
    Logger()->debug(errmsg);
  } catch (...) {
    Logger()->debug("Unable to start collection of GPU metrics");
  }
  return gpu;
}

#ifdef TITUS_AGENT
static void gather_titus_metrics(CGroup* cGroup, Proc* proc, Disk* disk, Aws* aws) {
  Logger()->info("Gathering titus metrics");
  cGroup->cpu_stats();
  cGroup->memory_stats();
  disk->titus_disk_stats();
  proc->network_stats();
  proc->snmp_stats();
  proc->netstat_stats();
  aws->update_stats();
}
#else
static void gather_peak_system_metrics(Proc* proc) { proc->peak_cpu_stats(); }

static void gather_slow_system_metrics(Proc* proc, Disk* disk, Ntp<>* ntp, Aws* aws) {
  Logger()->info("Gathering system metrics");
  proc->cpu_stats();
  proc->network_stats();
  proc->arp_stats();
  proc->snmp_stats();
  proc->netstat_stats();
  proc->loadavg_stats();
  proc->memory_stats();
  proc->vmstats();
  disk->disk_stats();
  ntp->update_stats();
  aws->update_stats();
}
#endif

struct terminator {
  terminator() noexcept {}

  // returns false if killed:
  template <class R, class P>
  bool wait_for(std::chrono::duration<R, P> const& time) {
    if (time.count() <= 0) {
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

#ifdef TITUS_AGENT
void collect_titus_metrics(spectator::Registry* registry) {
  using std::chrono::milliseconds;
  using std::chrono::seconds;
  using std::chrono::system_clock;

  Aws aws{registry};
  CGroup cGroup{registry};
  Proc proc{registry};
  Disk disk{registry, ""};
  PerfMetrics perf_metrics{registry, ""};

  auto gpu = init_gpu(registry);

  // collect all metrics except perf at startup
  gather_titus_metrics(&cGroup, &proc, &disk, &aws);
  auto next_run = system_clock::now();
  std::chrono::nanoseconds time_to_sleep = seconds(60);
  while (runner.wait_for(time_to_sleep)) {
    gather_titus_metrics(&cGroup, &proc, &disk, &aws);
    perf_metrics.collect();
    if (gpu) {
      gpu->gpu_metrics();
    }
    next_run += seconds(60);
    time_to_sleep = next_run - system_clock::now();
    if (time_to_sleep.count() > 0) {
      Logger()->info("Sleeping {} milliseconds",
                     std::chrono::duration_cast<milliseconds>(time_to_sleep).count());
    }
  }
}
#else
void collect_system_metrics(spectator::Registry* registry) {
  using std::chrono::seconds;
  using std::chrono::system_clock;
  Proc proc{registry};
  Disk disk{registry, ""};
  Ntp<> ntp{registry};
  Aws aws{registry};

  auto gpu = init_gpu(registry);

  PerfMetrics perf_metrics{registry, ""};
  auto now = system_clock::now();
  auto next_slow_run = now + seconds(60);
  auto next_run = now;
  std::chrono::nanoseconds time_to_sleep;
  gather_slow_system_metrics(&proc, &disk, &ntp, &aws);
  do {
    gather_peak_system_metrics(&proc);
    if (system_clock::now() >= next_slow_run) {
      gather_slow_system_metrics(&proc, &disk, &ntp, &aws);
      perf_metrics.collect();
      next_slow_run += seconds(60);
      if (gpu) {
        gpu->gpu_metrics();
      }
    }
    next_run += seconds(1);
    time_to_sleep = next_run - system_clock::now();
  } while (runner.wait_for(time_to_sleep));
}
#endif

int main(int argc, const char* argv[]) {
#ifdef TITUS_AGENT
  container_handle c;
  if (maybe_reexec(argv)) {
    return 1;
  }
  if (maybe_contain(&c) != 0) {
    return 1;
  }
  const char* process = argc > 1 ? argv[1] : "atlas-titus-agent";
#else
  const char* process = argc > 1 ? argv[1] : "atlas-system-agent";
#endif

  init_signals();
  auto cfg = GetSpectatorConfig();
  cfg->common_tags["xatlas.process"] = process;

#ifdef TITUS_AGENT
  auto titus_host = std::getenv("EC2_INSTANCE_ID");
  if (titus_host != nullptr) {
    cfg->common_tags["titus.host"] = titus_host;
  }
#endif

  auto spectator_logger = GetLogger("spectator");
  auto logger = Logger();
  if (std::getenv("VERBOSE_AGENT") != nullptr) {
    spectator_logger->set_level(spdlog::level::debug);
    logger->set_level(spdlog::level::debug);
  }
  spectator::Registry registry{std::move(cfg), spectator_logger};
  registry.Start();
#ifdef TITUS_AGENT
  collect_titus_metrics(&registry);
#else
  collect_system_metrics(&registry);
#endif
  logger->info("Shutting down spectator registry");
  registry.Stop();
  return 0;
}
