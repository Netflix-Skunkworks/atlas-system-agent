#include "config.h"
#include "contain/contain.h"
#include "lib/cgroup.h"
#include "lib/disk.h"
#include "lib/gpumetrics.h"
#include "lib/logger.h"
#include "lib/nvml.h"
#include "lib/proc.h"
#include <atlas/atlas_client.h>
#include <cinttypes>
#include <condition_variable>
#include <csignal>

using atlasagent::CGroup;
using atlasagent::Disk;
using atlasagent::GpuMetrics;
using atlasagent::Logger;
using atlasagent::Nvml;
using atlasagent::Proc;

#ifdef TITUS_AGENT
static void gather_titus_metrics(CGroup* cGroup, Proc* proc, Disk* disk) {
  Logger()->info("Gathering fast titus metrics");

  cGroup->cpu_stats();
  cGroup->memory_stats();
  disk->titus_disk_stats();
}
#else
static void gather_fast_system_metrics(Proc* proc, Disk* disk) {
  Logger()->info("Gathering fast system metrics");

  proc->loadavg_stats();
  proc->cpu_stats();
  proc->memory_stats();
  proc->vmstats();
  disk->disk_stats();
}
#endif
static void gather_slow_system_metrics(Proc* proc) {
  Logger()->info("Gathering slow system metrics");
  proc->network_stats();
  proc->snmp_stats();
}

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
void collect_titus_metrics(container_handle* c) {
  const auto& clock = atlas_registry.clock();
  CGroup cGroup{&atlas_registry};
  Proc proc{&atlas_registry};
  Disk disk{&atlas_registry, "", c};

  int64_t time_to_sleep;
  do {
    auto now = clock.WallTime();
    auto next_run = now + 60 * 1000L;
    gather_titus_metrics(&cGroup, &proc, &disk);
    time_to_sleep = next_run - clock.WallTime();
    if (time_to_sleep > 0) {
      Logger()->info("Sleeping {} milliseconds", time_to_sleep);
    }
  } while (runner.wait_for(std::chrono::milliseconds(time_to_sleep)));
}
#else
void collect_system_metrics() {
  const auto& clock = atlas_registry.clock();
  Proc proc{&atlas_registry};
  Disk disk{&atlas_registry, "", nullptr};

  auto gpu = std::unique_ptr<GpuMetrics<Nvml>>(nullptr);
  try {
    Nvml nvml;
    gpu.reset(new GpuMetrics<Nvml>(&atlas_registry, &nvml));
  } catch (...) {
    Logger()->debug("Unable to start collection of GPU metrics");
  }
  int64_t time_to_sleep;
  int64_t next_slow_run = clock.WallTime();
  do {
    auto now = clock.WallTime();
    auto next_run = now + 10 * 1000L;
    gather_fast_system_metrics(&proc, &disk);
    if (now >= next_slow_run) {
      gather_slow_system_metrics(&proc);
      next_slow_run += 60 * 1000L;
    }
    if (gpu) {
      gpu->gpu_metrics();
    }
    time_to_sleep = next_run - clock.WallTime();
    if (time_to_sleep > 0) {
      Logger()->info("Sleeping {} milliseconds", time_to_sleep);
    }
  } while (runner.wait_for(std::chrono::milliseconds(time_to_sleep)));
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

  atlas::UseConsoleLogger(spdlog::level::info);
  init_signals();
  atlasagent::UseConsoleLogger();
  atlas::Init();
  atlas::SetNotifyAlertServer(false);
  atlas::AddCommonTag("xatlas.process", process);

#ifdef TITUS_AGENT
  auto titus_host = std::getenv("EC2_INSTANCE_ID");
  if (titus_host != nullptr) {
    atlas::AddCommonTag("titus.host", titus_host);
  }
  collect_titus_metrics(&c);
#else
  collect_system_metrics();
#endif
  Logger()->info("Shutting down atlas");
  atlas::Shutdown();
  return 0;
}
