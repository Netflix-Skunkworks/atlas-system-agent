#include "proc.h"
#include "util.h"
#include <cinttypes>
#include <cstring>

namespace atlasagent {

Proc::Proc(spectator::Registry* registry, std::string path_prefix) noexcept
    : registry_(registry), path_prefix_(std::move(path_prefix)) {}

static void discard_line(FILE* fp) {
  for (auto ch = getc_unlocked(fp); ch != EOF && ch != '\n'; ch = getc_unlocked(fp)) {
    // just keep reading until a newline is found
  }
}

using spectator::IdPtr;
using spectator::Registry;
using spectator::Tags;

static IdPtr id_for(Registry* registry, const char* name, const char* iface,
                    const char* idStr) noexcept {
  Tags tags{{"iface", iface}};
  if (idStr != nullptr) {
    tags.add("id", idStr);
  }
  return registry->CreateId(name, tags);
}

void Proc::handle_line(FILE* fp) noexcept {
  char iface[4096];
  int64_t bytes, packets, errs, drop, fifo, frame, compressed, multicast, colls, carrier;

  auto assigned =
      fscanf(fp,
             "%s %" PRId64 " %" PRId64 " %" PRId64 " %" PRId64 " %" PRId64 " %" PRId64 " %" PRId64
             " %" PRId64,
             iface, &bytes, &packets, &errs, &drop, &fifo, &frame, &compressed, &multicast);
  if (assigned > 0) {
    iface[strlen(iface) - 1] = '\0';  // strip trailing ':'

    registry_->GetMonotonicCounter(id_for(registry_, "net.iface.bytes", iface, "in"))->Set(bytes);
    registry_->GetMonotonicCounter(id_for(registry_, "net.iface.packets", iface, "in"))
        ->Set(packets);
    registry_->GetMonotonicCounter(id_for(registry_, "net.iface.errors", iface, "in"))
        ->Set(errs + fifo + frame);
    registry_->GetMonotonicCounter(id_for(registry_, "net.iface.droppedPackets", iface, "in"))
        ->Set(drop);
  }

  assigned = fscanf(fp,
                    " %" PRId64 " %" PRId64 " %" PRId64 " %" PRId64 " %" PRId64 " %" PRId64
                    " %" PRId64 " %" PRId64,
                    &bytes, &packets, &errs, &drop, &fifo, &colls, &carrier, &compressed);
  if (assigned > 0) {
    registry_->GetMonotonicCounter(id_for(registry_, "net.iface.bytes", iface, "out"))->Set(bytes);
    registry_->GetMonotonicCounter(id_for(registry_, "net.iface.packets", iface, "out"))
        ->Set(packets);
    registry_->GetMonotonicCounter(id_for(registry_, "net.iface.errors", iface, "out"))
        ->Set(errs + fifo);
    registry_->GetMonotonicCounter(id_for(registry_, "net.iface.droppedPackets", iface, "out"))
        ->Set(drop);
    registry_->GetMonotonicCounter(id_for(registry_, "net.iface.collisions", iface, nullptr))
        ->Set(colls);
  }
}

void Proc::network_stats() noexcept {
  auto fp = open_file(path_prefix_, "net/dev");
  if (fp == nullptr) {
    return;
  }
  discard_line(fp);
  discard_line(fp);

  while (!feof(fp)) {
    handle_line(fp);
  }
}

static constexpr const char* IP_STATS_LINE =
    "Ip: %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu "
    "%lu %lu";
static constexpr size_t IP_STATS_PREFIX_LEN = 4;
static constexpr const char* TCP_STATS_LINE =
    "Tcp: %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu";
static constexpr size_t TCP_STATS_PREFIX_LEN = 5;
static constexpr const char* UDP_STATS_LINE = "Udp: %lu %lu %lu %lu";
static constexpr size_t UDP_STATS_PREFIX_LEN = 5;
static constexpr const char* LOADAVG_LINE = "%lf %lf %lf";

static constexpr int kConnStates = 12;
void sum_tcp_states(FILE* fp, std::array<int, kConnStates>* connections) noexcept {
  char line[2048];
  // discard header
  if (fgets(line, sizeof line, fp) == nullptr) {
    return;
  }
  while (fgets(line, sizeof line, fp) != nullptr) {
    std::vector<std::string> fields;
    split(line, &fields);
    // all lines have at least 12 fields. Just being extra paranoid here:
    if (fields.size() < 4) {
      continue;
    }
    const char* st = fields[3].c_str();
    auto state = static_cast<int>(strtol(st, nullptr, 16));
    if (state < kConnStates) {
      ++(*connections)[state];
    } else {
      Logger()->info("Ignoring connection state {} for line: {}", state, line);
    }
  }
}

using gauge_ptr = std::shared_ptr<spectator::Gauge>;
inline std::shared_ptr<spectator::Gauge> tcpstate_gauge(Registry* registry, const char* state,
                                                        const char* protocol) {
  return registry->GetGauge(
      registry->CreateId("net.tcp.connectionStates", {{"id", state}, {"proto", protocol}}));
}

inline std::array<gauge_ptr, kConnStates> make_tcp_gauges(Registry* registry_,
                                                          const char* protocol) {
  return std::array<gauge_ptr, kConnStates>{gauge_ptr{nullptr},
                                            tcpstate_gauge(registry_, "established", protocol),
                                            tcpstate_gauge(registry_, "synSent", protocol),
                                            tcpstate_gauge(registry_, "synRecv", protocol),
                                            tcpstate_gauge(registry_, "finWait1", protocol),
                                            tcpstate_gauge(registry_, "finWait2", protocol),
                                            tcpstate_gauge(registry_, "timeWait", protocol),
                                            tcpstate_gauge(registry_, "close", protocol),
                                            tcpstate_gauge(registry_, "closeWait", protocol),
                                            tcpstate_gauge(registry_, "lastAck", protocol),
                                            tcpstate_gauge(registry_, "listen", protocol),
                                            tcpstate_gauge(registry_, "closing", protocol)};
}

inline void update_tcpstates_for_proto(const std::array<gauge_ptr, kConnStates>& gauges, FILE* fp) {
  std::array<int, kConnStates> connections{};
  if (fp != nullptr) {
    sum_tcp_states(fp, &connections);
    for (auto i = 1; i < kConnStates; ++i) {
      gauges[i]->Set(connections[i]);
    }
  }
}

void Proc::parse_tcp_connections() noexcept {
  static std::array<gauge_ptr, kConnStates> v4_states = make_tcp_gauges(registry_, "v4");
  static std::array<gauge_ptr, kConnStates> v6_states = make_tcp_gauges(registry_, "v6");

  update_tcpstates_for_proto(v4_states, open_file(path_prefix_, "net/tcp"));
  update_tcpstates_for_proto(v6_states, open_file(path_prefix_, "net/tcp6"));
}

// replicate what snmpd is doing
void Proc::snmp_stats() noexcept {
  auto fp = open_file(path_prefix_, "net/snmp");
  if (fp == nullptr) {
    return;
  }

  char line[1024];
  while (fgets(line, sizeof line, fp) != nullptr) {
    if (strncmp(line, IP_STATS_LINE, IP_STATS_PREFIX_LEN) == 0) {
      if (fgets(line, sizeof line, fp) != nullptr) {
        parse_ip_stats(line);
      }
    } else if (strncmp(line, TCP_STATS_LINE, TCP_STATS_PREFIX_LEN) == 0) {
      if (fgets(line, sizeof line, fp) != nullptr) {
        parse_tcp_stats(line);
      }
    } else if (strncmp(line, UDP_STATS_LINE, UDP_STATS_PREFIX_LEN) == 0) {
      if (fgets(line, sizeof line, fp) != nullptr) {
        parse_udp_stats(line);
      }
    }
  }

  parse_tcp_connections();
}

void Proc::parse_ip_stats(const char* buf) noexcept {
  static auto ipInReceivesCtr =
      registry_->GetMonotonicCounter(registry_->CreateId("net.ip.datagrams", Tags{{"id", "in"}}));
  static auto ipInDicardsCtr =
      registry_->GetMonotonicCounter(registry_->CreateId("net.ip.discards", Tags{{"id", "in"}}));
  static auto ipOutRequestsCtr =
      registry_->GetMonotonicCounter(registry_->CreateId("net.ip.datagrams", Tags{{"id", "out"}}));
  static auto ipOutDiscardsCtr =
      registry_->GetMonotonicCounter(registry_->CreateId("net.ip.discards", Tags{{"id", "out"}}));
  static auto ipReasmReqdsCtr = registry_->GetMonotonicCounter("net.ip.reasmReqds");

  u_long ipForwarding, ipDefaultTTL, ipInReceives, ipInHdrErrors, ipInAddrErrors, ipForwDatagrams,
      ipInUnknownProtos, ipInDiscards, ipInDelivers, ipOutRequests, ipOutDiscards, ipOutNoRoutes,
      ipReasmTimeout, ipReasmReqds, ipReasmOKs, ipReasmFails, ipFragOKs, ipFragFails, ipFragCreates;

  if (buf == nullptr) {
    return;
  }

  sscanf(buf, IP_STATS_LINE, &ipForwarding, &ipDefaultTTL, &ipInReceives, &ipInHdrErrors,
         &ipInAddrErrors, &ipForwDatagrams, &ipInUnknownProtos, &ipInDiscards, &ipInDelivers,
         &ipOutRequests, &ipOutDiscards, &ipOutNoRoutes, &ipReasmTimeout, &ipReasmReqds,
         &ipReasmOKs, &ipReasmFails, &ipFragOKs, &ipFragFails, &ipFragCreates);

  ipInReceivesCtr->Set(ipInReceives);
  ipInDicardsCtr->Set(ipInDiscards);
  ipOutRequestsCtr->Set(ipOutRequests);
  ipOutDiscardsCtr->Set(ipOutDiscards);
  ipReasmReqdsCtr->Set(ipReasmReqds);
}

void Proc::parse_tcp_stats(const char* buf) noexcept {
  static auto tcpInSegsCtr =
      registry_->GetMonotonicCounter(registry_->CreateId("net.tcp.segments", Tags{{"id", "in"}}));
  static auto tcpOutSegsCtr =
      registry_->GetMonotonicCounter(registry_->CreateId("net.tcp.segments", Tags{{"id", "out"}}));
  static auto tcpRetransSegsCtr = registry_->GetMonotonicCounter(
      registry_->CreateId("net.tcp.errors", Tags{{"id", "retransSegs"}}));
  static auto tcpInErrsCtr =
      registry_->GetMonotonicCounter(registry_->CreateId("net.tcp.errors", Tags{{"id", "inErrs"}}));
  static auto tcpOutRstsCtr = registry_->GetMonotonicCounter(
      registry_->CreateId("net.tcp.errors", Tags{{"id", "outRsts"}}));
  static auto tcpAttemptFailsCtr = registry_->GetMonotonicCounter(
      registry_->CreateId("net.tcp.errors", Tags{{"id", "attemptFails"}}));
  static auto tcpEstabResetsCtr = registry_->GetMonotonicCounter(
      registry_->CreateId("net.tcp.errors", Tags{{"id", "estabResets"}}));
  static auto tcpActiveOpensCtr =
      registry_->GetMonotonicCounter(registry_->CreateId("net.tcp.opens", Tags{{"id", "active"}}));
  static auto tcpPassiveOpensCtr =
      registry_->GetMonotonicCounter(registry_->CreateId("net.tcp.opens", Tags{{"id", "passive"}}));
  static auto tcpCurrEstabGauge = registry_->GetGauge("net.tcp.currEstab");

  if (buf == nullptr) {
    return;
  }

  u_long tcpRtoAlgorithm, tcpRtoMin, tcpRtoMax, tcpMaxConn, tcpActiveOpens, tcpPassiveOpens,
      tcpAttemptFails, tcpEstabResets, tcpCurrEstab, tcpInSegs, tcpOutSegs, tcpRetransSegs,
      tcpInErrs, tcpOutRsts;
  auto ret =
      sscanf(buf, TCP_STATS_LINE, &tcpRtoAlgorithm, &tcpRtoMin, &tcpRtoMax, &tcpMaxConn,
             &tcpActiveOpens, &tcpPassiveOpens, &tcpAttemptFails, &tcpEstabResets, &tcpCurrEstab,
             &tcpInSegs, &tcpOutSegs, &tcpRetransSegs, &tcpInErrs, &tcpOutRsts);
  tcpInSegsCtr->Set(tcpInSegs);
  tcpOutSegsCtr->Set(tcpOutSegs);
  tcpRetransSegsCtr->Set(tcpRetransSegs);
  tcpActiveOpensCtr->Set(tcpActiveOpens);
  tcpPassiveOpensCtr->Set(tcpPassiveOpens);
  tcpAttemptFailsCtr->Set(tcpAttemptFails);
  tcpEstabResetsCtr->Set(tcpEstabResets);
  tcpCurrEstabGauge->Set(tcpCurrEstab);

  if (ret > 12) {
    tcpInErrsCtr->Set(tcpInErrs);
  }
  if (ret > 13) {
    tcpOutRstsCtr->Set(tcpOutRsts);
  }
}

void Proc::parse_udp_stats(const char* buf) noexcept {
  static auto udpInDatagramsCtr =
      registry_->GetMonotonicCounter(registry_->CreateId("net.udp.datagrams", Tags{{"id", "in"}}));
  static auto udpOutDatagramsCtr =
      registry_->GetMonotonicCounter(registry_->CreateId("net.udp.datagrams", Tags{{"id", "out"}}));
  static auto udpInErrorsCtr = registry_->GetMonotonicCounter(
      registry_->CreateId("net.udp.errors", Tags{{"id", "inErrors"}}));

  if (buf == nullptr) {
    return;
  }

  u_long udpInDatagrams, udpNoPorts, udpInErrors, udpOutDatagrams;
  sscanf(buf, UDP_STATS_LINE, &udpInDatagrams, &udpNoPorts, &udpInErrors, &udpOutDatagrams);

  udpInDatagramsCtr->Set(udpInDatagrams);
  udpInErrorsCtr->Set(udpInErrors);
  udpOutDatagramsCtr->Set(udpOutDatagrams);
}

void Proc::parse_load_avg(const char* buf) noexcept {
  static auto loadAvg1Gauge = registry_->GetGauge("sys.load.1");
  static auto loadAvg5Gauge = registry_->GetGauge("sys.load.5");
  static auto loadAvg15Gauge = registry_->GetGauge("sys.load.15");

  double loadAvg1, loadAvg5, loadAvg15;
  sscanf(buf, LOADAVG_LINE, &loadAvg1, &loadAvg5, &loadAvg15);

  loadAvg1Gauge->Set(loadAvg1);
  loadAvg5Gauge->Set(loadAvg5);
  loadAvg15Gauge->Set(loadAvg15);
}

void Proc::loadavg_stats() noexcept {
  auto fp = open_file(path_prefix_, "loadavg");
  char line[1024];
  if (fp == nullptr) {
    return;
  }
  if (std::fgets(line, sizeof line, fp) != nullptr) {
    parse_load_avg(line);
  }
}

namespace proc {
int get_pid_from_sched(const char* sched_line) noexcept {
  auto parens = strchr(sched_line, '(');
  if (parens == nullptr) {
    return -1;
  }
  parens++;  // point to the first digit
  return atoi(parens);
}
}  // namespace proc

bool Proc::is_container() const noexcept {
  auto fp = open_file(path_prefix_, "1/sched");
  if (fp == nullptr) {
    return false;
  }
  char line[1024];
  bool error = std::fgets(line, sizeof line, fp) == nullptr;
  if (error) {
    return false;
  }

  return proc::get_pid_from_sched(line) != 1;
}

void Proc::set_prefix(const std::string& new_prefix) noexcept { path_prefix_ = new_prefix; }

namespace detail {
struct cpu_gauge_vals {
  double user;
  double system;
  double stolen;
  double nice;
  double wait;
  double interrupt;
};

template <typename G>
struct cpu_gauges {
  using gauge_ptr = std::shared_ptr<G>;
  using gauge_maker_t =
      std::function<gauge_ptr(Registry* registry, const char* name, const char* id)>;
  cpu_gauges(Registry* registry, const char* name, const gauge_maker_t& gauge_maker)
      : user_gauge(gauge_maker(registry, name, "user")),
        system_gauge(gauge_maker(registry, name, "system")),
        stolen_gauge(gauge_maker(registry, name, "stolen")),
        nice_gauge(gauge_maker(registry, name, "nice")),
        wait_gauge(gauge_maker(registry, name, "wait")),
        interrupt_gauge(gauge_maker(registry, name, "interrupt")) {}

  gauge_ptr user_gauge;
  gauge_ptr system_gauge;
  gauge_ptr stolen_gauge;
  gauge_ptr nice_gauge;
  gauge_ptr wait_gauge;
  gauge_ptr interrupt_gauge;

  void update(const cpu_gauge_vals& vals) {
    user_gauge->Set(vals.user);
    system_gauge->Set(vals.system);
    stolen_gauge->Set(vals.stolen);
    nice_gauge->Set(vals.nice);
    wait_gauge->Set(vals.wait);
    interrupt_gauge->Set(vals.interrupt);
  }
};

struct cores_dist_summary {
  cores_dist_summary(Registry* registry, const char* name)
      : usage_ds(registry->GetDistributionSummary(name)) {}

  std::shared_ptr<spectator::DistributionSummary> usage_ds;

  void update(const cpu_gauge_vals& vals) {
    auto usage = vals.user + vals.system + vals.stolen + vals.nice + vals.wait + vals.interrupt;
    usage_ds->Record(usage);
  }
};

struct stat_vals {
  static constexpr const char* CPU_STATS_LINE = " %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu";
  u_long user{0}, nice{0}, system{0}, idle{0}, iowait{0}, irq{0}, softirq{0}, steal{0}, guest{0},
      guest_nice{0};
  double total{NAN};

  static stat_vals parse(const char* line) {
    stat_vals result;
    auto ret = sscanf(line, CPU_STATS_LINE, &result.user, &result.nice, &result.system,
                      &result.idle, &result.iowait, &result.irq, &result.softirq, &result.steal,
                      &result.guest, &result.guest_nice);
    if (ret < 7) {
      Logger()->info("Unable to parse cpu stats from '{}' - only {} fields were read", line, ret);
      return result;
    }
    result.total = static_cast<double>(result.user) + result.nice + result.system + result.idle +
                   result.iowait + result.irq + result.softirq;
    if (ret > 7) {
      result.total += result.steal + result.guest + result.guest_nice;
    } else {
      result.steal = result.guest = result.guest_nice = 0;
    }
    return result;
  }

  bool has_been_updated() const noexcept { return !std::isnan(total); }

  stat_vals() = default;

  cpu_gauge_vals compute_vals(const stat_vals& prev) const noexcept {
    cpu_gauge_vals vals{};
    auto delta_total = total - prev.total;
    auto delta_user = user - prev.user;
    auto delta_system = system - prev.system;
    auto delta_stolen = steal - prev.steal;
    auto delta_nice = nice - prev.nice;
    auto delta_interrupt = (irq + softirq) - (prev.irq + prev.softirq);
    auto delta_wait = iowait > prev.iowait ? iowait - prev.iowait : 0;

    if (delta_total > 0) {
      vals.user = 100.0 * delta_user / delta_total;
      vals.system = 100.0 * delta_system / delta_total;
      vals.stolen = 100.0 * delta_stolen / delta_total;
      vals.nice = 100.0 * delta_nice / delta_total;
      vals.wait = 100.0 * delta_wait / delta_total;
      vals.interrupt = 100.0 * delta_interrupt / delta_total;
    } else {
      vals.user = vals.system = vals.stolen = vals.nice = vals.wait = vals.interrupt = 0.0;
    }
    return vals;
  }
};

}  // namespace detail

inline void set_if_present(const std::unordered_map<std::string, int64_t>& stats, const char* key,
                           spectator::MonotonicCounter* ctr) {
  auto it = stats.find(key);
  if (it != stats.end()) {
    ctr->Set(it->second);
  }
}

void Proc::vmstats() noexcept {
  static auto processes = registry_->GetMonotonicCounter("vmstat.procs.count");
  static auto procs_running =
      registry_->GetGauge(registry_->CreateId("vmstat.procs", {{"id", "running"}}));
  static auto procs_blocked =
      registry_->GetGauge(registry_->CreateId("vmstat.procs", {{"id", "blocked"}}));

  static auto page_in =
      registry_->GetMonotonicCounter(registry_->CreateId("vmstat.paging", Tags{{"id", "in"}}));
  static auto page_out =
      registry_->GetMonotonicCounter(registry_->CreateId("vmstat.paging", Tags{{"id", "out"}}));
  static auto swap_in =
      registry_->GetMonotonicCounter(registry_->CreateId("vmstat.swapping", Tags{{"id", "in"}}));
  static auto swap_out =
      registry_->GetMonotonicCounter(registry_->CreateId("vmstat.swapping", Tags{{"id", "out"}}));

  static auto fh_alloc = registry_->GetGauge("vmstat.fh.allocated");
  static auto fh_max = registry_->GetGauge("vmstat.fh.max");

  auto fp = open_file(path_prefix_, "stat");
  if (fp == nullptr) {
    return;
  }

  char line[2048];
  while (fgets(line, sizeof line, fp) != nullptr) {
    if (starts_with(line, "processes")) {
      u_long n;
      sscanf(line, "processes %lu", &n);
      processes->Set(n);
    } else if (starts_with(line, "procs_running")) {
      u_long n;
      sscanf(line, "procs_running %lu", &n);
      procs_running->Set(n);
    } else if (starts_with(line, "procs_blocked")) {
      u_long n;
      sscanf(line, "procs_blocked %lu", &n);
      procs_blocked->Set(n);
    }
  }

  std::unordered_map<std::string, int64_t> vmstats;
  parse_kv_from_file(path_prefix_, "vmstat", &vmstats);
  set_if_present(vmstats, "pgpgin", page_in.get());
  set_if_present(vmstats, "pgpgout", page_out.get());
  set_if_present(vmstats, "pswpin", swap_in.get());
  set_if_present(vmstats, "pswpout", swap_out.get());

  auto fh = open_file(path_prefix_, "sys/fs/file-nr");
  if (fgets(line, sizeof line, fh) != nullptr) {
    u_long alloc, used, max;
    if (sscanf(line, "%lu %lu %lu", &alloc, &used, &max) == 3) {
      fh_alloc->Set(alloc);
      fh_max->Set(max);
    }
  }
}

void Proc::peak_cpu_stats() noexcept {
  static detail::cpu_gauges<spectator::MaxGauge> peakUtilizationGauges{
      registry_, "sys.cpu.peakUtilization", [](Registry* r, const char* name, const char* id) {
        return r->GetMaxGauge(r->CreateId(name, Tags{{"id", id}}));
      }};
  static detail::stat_vals prev;

  auto fp = open_file(path_prefix_, "stat");
  if (fp == nullptr) {
    return;
  }
  char line[1024];
  auto ret = fgets(line, sizeof line, fp);
  if (ret == nullptr) {
    return;
  }
  detail::stat_vals vals = detail::stat_vals::parse(line + 3);  // 'cpu'
  if (prev.has_been_updated()) {
    auto gauge_vals = vals.compute_vals(prev);
    peakUtilizationGauges.update(gauge_vals);
  }
  prev = vals;
}

void Proc::cpu_stats() noexcept {
  static detail::cpu_gauges<spectator::Gauge> utilizationGauges{
      registry_, "sys.cpu.utilization", [](Registry* r, const char* name, const char* id) {
        return r->GetGauge(r->CreateId(name, Tags{{"id", id}}));
      }};

  static detail::cores_dist_summary coresDistSummary{registry_, "sys.cpu.coreUtilization"};
  static detail::stat_vals prev_vals;
  static std::unordered_map<int, detail::stat_vals> prev_cpu_vals;

  auto fp = open_file(path_prefix_, "stat");
  if (fp == nullptr) {
    return;
  }
  char line[1024];
  auto ret = fgets(line, sizeof line, fp);
  if (ret == nullptr) {
    return;
  }
  detail::stat_vals vals = detail::stat_vals::parse(line + 3);  // 'cpu'
  if (prev_vals.has_been_updated()) {
    auto gauge_vals = vals.compute_vals(prev_vals);
    utilizationGauges.update(gauge_vals);
  }
  prev_vals = vals;

  // get the per-cpu metrics
  while (fgets(line, sizeof line, fp) != nullptr) {
    if (strncmp(line, "cpu", 3) != 0) {
      break;
    }
    int cpu_num;
    sscanf(line, "cpu%d ", &cpu_num);
    char* p = line + 4;
    while (*p != ' ') {
      ++p;
    }
    detail::stat_vals per_cpu_vals = detail::stat_vals::parse(p);
    auto it = prev_cpu_vals.find(cpu_num);
    if (it != prev_cpu_vals.end()) {
      auto& prev = it->second;
      auto computed_vals = per_cpu_vals.compute_vals(prev);
      coresDistSummary.update(computed_vals);
    }
    prev_cpu_vals[cpu_num] = per_cpu_vals;
  }
}

void Proc::memory_stats() noexcept {
  static auto avail_real = registry_->GetGauge("mem.availReal");
  static auto free_real = registry_->GetGauge("mem.freeReal");
  static auto total_real = registry_->GetGauge("mem.totalReal");
  static auto avail_swap = registry_->GetGauge("mem.availSwap");
  static auto total_swap = registry_->GetGauge("mem.totalSwap");
  static auto buffer = registry_->GetGauge("mem.buffer");
  static auto cached = registry_->GetGauge("mem.cached");
  static auto shared = registry_->GetGauge("mem.shared");
  static auto total_free = registry_->GetGauge("mem.totalFree");

  auto fp = open_file(path_prefix_, "meminfo");
  if (fp == nullptr) {
    return;
  }

  char line[1024];
  u_long total_free_bytes = 0;
  while (fgets(line, sizeof line, fp) != nullptr) {
    if (starts_with(line, "MemTotal:")) {
      u_long n;
      sscanf(line, "MemTotal: %lu", &n);
      total_real->Set(n * 1024.0);
    } else if (starts_with(line, "MemFree:")) {
      u_long n;
      sscanf(line, "MemFree: %lu", &n);
      free_real->Set(n * 1024.0);
      total_free_bytes += n;
    } else if (starts_with(line, "MemAvailable:")) {
      u_long n;
      sscanf(line, "MemAvailable: %lu", &n);
      avail_real->Set(n * 1024.0);
    } else if (starts_with(line, "SwapFree:")) {
      u_long n;
      sscanf(line, "SwapFree: %lu", &n);
      avail_swap->Set(n * 1024.0);
      total_free_bytes += n;
    } else if (starts_with(line, "SwapTotal:")) {
      u_long n;
      sscanf(line, "SwapTotal: %lu", &n);
      total_swap->Set(n * 1024.0);
    } else if (starts_with(line, "Buffers:")) {
      u_long n;
      sscanf(line, "Buffers: %lu", &n);
      buffer->Set(n * 1024.0);
    } else if (starts_with(line, "Cached:")) {
      u_long n;
      sscanf(line, "Cached: %lu", &n);
      cached->Set(n * 1024.0);
    } else if (starts_with(line, "Shmem:")) {
      u_long n;
      sscanf(line, "Shmem: %lu", &n);
      shared->Set(n * 1024.0);
    }
  }
  total_free->Set(total_free_bytes * 1024.0);
}

int64_t to_int64(const std::string& s) {
  int64_t res;
  sscanf(s.c_str(), "%" PRId64, &res);
  return res;
}

void Proc::netstat_stats() noexcept {
  static auto ect_ctr = registry_->GetMonotonicCounter(
      registry_->CreateId("net.ip.ectPackets", Tags{{"id", "capable"}}));
  static auto noEct_ctr = registry_->GetMonotonicCounter(
      registry_->CreateId("net.ip.ectPackets", Tags{{"id", "notCapable"}}));
  static auto congested_ctr = registry_->GetMonotonicCounter("net.ip.congestedPackets");

  auto fp = open_file(path_prefix_, "net/netstat");
  if (fp == nullptr) {
    return;
  }

  int64_t noEct = 0, ect = 0, congested = 0;
  char line[1024];
  while (fgets(line, sizeof line, fp) != nullptr) {
    if (starts_with(line, "IpExt:")) {
      // get header indexes
      std::vector<std::string> headers;
      split(line, &headers);
      if (fgets(line, sizeof line, fp) == nullptr) {
        Logger()->warn("Unable to parse {}/net/netstat", path_prefix_);
        return;
      }
      std::vector<std::string> values;
      values.reserve(headers.size());
      split(line, &values);
      assert(values.size() == headers.size());
      auto idx = 0u;
      for (const auto& header : headers) {
        if (header == "InNoECTPkts") {
          noEct = to_int64(values[idx]);
        } else if (header == "InECT1Pkts" || header == "InECT0Pkts") {
          ect += to_int64(values[idx]);
        } else if (header == "InCEPkts") {
          congested = to_int64(values[idx]);
        }
        ++idx;
      }
      break;
    }
  }

  // Set all the counters if we have data. We want to explicitly send a 0 value for congested to
  // distinguish known no congestion from no data
  if (ect > 0 || noEct > 0) {
    congested_ctr->Set(congested);
    ect_ctr->Set(ect);
    noEct_ctr->Set(noEct);
  }
}

void Proc::arp_stats() noexcept {
  static auto arpcache_size = registry_->GetGauge("net.arpCache");
  auto fp = open_file(path_prefix_, "net/arp");
  if (fp == nullptr) {
    return;
  }

  // discard the header
  discard_line(fp);
  auto num_entries = 0;
  while (!feof(fp)) {
    discard_line(fp);
    num_entries++;
  }
  arpcache_size->Set(num_entries);
}

}  // namespace atlasagent
