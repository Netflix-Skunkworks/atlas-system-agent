#include "proc.h"
#include "atlas-helpers.h"
#include "util.h"
#include <cinttypes>
#include <cstring>

namespace atlasagent {

Proc::Proc(atlas::meter::Registry* registry, std::string path_prefix) noexcept
    : registry_(registry), path_prefix_(std::move(path_prefix)), counters_(registry) {}

static void discard_line(FILE* fp) {
  for (auto ch = getc_unlocked(fp); ch != EOF && ch != '\n'; ch = getc_unlocked(fp)) {
    // just keep reading until a newline is found
  }
}

using atlas::meter::IdPtr;
using atlas::meter::kEmptyTags;
using atlas::meter::MonotonicCounter;
using atlas::meter::Registry;
using atlas::meter::Tags;

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

    counters_.get(id_for(registry_, "net.iface.bytes", iface, "in"))->Set(bytes);
    counters_.get(id_for(registry_, "net.iface.packets", iface, "in"))->Set(packets);
    counters_.get(id_for(registry_, "net.iface.errors", iface, "in"))->Set(errs + fifo + frame);
    counters_.get(id_for(registry_, "net.iface.droppedPackets", iface, "in"))->Set(drop);
  }

  assigned = fscanf(fp,
                    " %" PRId64 " %" PRId64 " %" PRId64 " %" PRId64 " %" PRId64 " %" PRId64
                    " %" PRId64 " %" PRId64,
                    &bytes, &packets, &errs, &drop, &fifo, &colls, &carrier, &compressed);
  if (assigned > 0) {
    counters_.get(id_for(registry_, "net.iface.bytes", iface, "out"))->Set(bytes);
    counters_.get(id_for(registry_, "net.iface.packets", iface, "out"))->Set(packets);
    counters_.get(id_for(registry_, "net.iface.errors", iface, "out"))->Set(errs + fifo);
    counters_.get(id_for(registry_, "net.iface.droppedPackets", iface, "out"))->Set(drop);
    counters_.get(id_for(registry_, "net.iface.collissions", iface, nullptr))->Set(colls);
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

using gauge_ptr = std::shared_ptr<atlas::meter::Gauge<double>>;
inline gauge_ptr tcpstate_gauge(Registry* registry, const char* state, const char* protocol) {
  return registry->gauge(
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
      gauges[i]->Update(connections[i]);
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
  static MonotonicCounter ipInReceivesCtr(
      registry_, registry_->CreateId("net.ip.datagrams", Tags{{"id", "in"}}));
  static MonotonicCounter ipInDicardsCtr(
      registry_, registry_->CreateId("net.ip.discards", Tags{{"id", "in"}}));
  static MonotonicCounter ipOutRequestsCtr(
      registry_, registry_->CreateId("net.ip.datagrams", Tags{{"id", "out"}}));
  static MonotonicCounter ipOutDiscardsCtr(
      registry_, registry_->CreateId("net.ip.discards", Tags{{"id", "out"}}));
  static MonotonicCounter ipReasmReqdsCtr(registry_,
                                          registry_->CreateId("net.ip.reasmReqds", kEmptyTags));

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

  ipInReceivesCtr.Set(static_cast<int64_t>(ipInReceives));
  ipInDicardsCtr.Set(static_cast<int64_t>(ipInDiscards));
  ipOutRequestsCtr.Set(static_cast<int64_t>(ipOutRequests));
  ipOutDiscardsCtr.Set(static_cast<int64_t>(ipOutDiscards));
  ipReasmReqdsCtr.Set(static_cast<int64_t>(ipReasmReqds));
}

void Proc::parse_tcp_stats(const char* buf) noexcept {
  static MonotonicCounter tcpInSegsCtr(registry_,
                                       registry_->CreateId("net.tcp.segments", Tags{{"id", "in"}}));
  static MonotonicCounter tcpOutSegsCtr(
      registry_, registry_->CreateId("net.tcp.segments", Tags{{"id", "out"}}));
  static MonotonicCounter tcpRetransSegsCtr(
      registry_, registry_->CreateId("net.tcp.errors", Tags{{"id", "retransSegs"}}));
  static MonotonicCounter tcpInErrsCtr(
      registry_, registry_->CreateId("net.tcp.errors", Tags{{"id", "inErrs"}}));
  static MonotonicCounter tcpOutRstsCtr(
      registry_, registry_->CreateId("net.tcp.errors", Tags{{"id", "outRsts"}}));
  static MonotonicCounter tcpAttemptFailsCtr(
      registry_, registry_->CreateId("net.tcp.errors", Tags{{"id", "attemptFails"}}));
  static MonotonicCounter tcpEstabResetsCtr(
      registry_, registry_->CreateId("net.tcp.errors", Tags{{"id", "estabResets"}}));
  static MonotonicCounter tcpActiveOpensCtr(
      registry_, registry_->CreateId("net.tcp.opens", Tags{{"id", "active"}}));
  static MonotonicCounter tcpPassiveOpensCtr(
      registry_, registry_->CreateId("net.tcp.opens", Tags{{"id", "passive"}}));
  static auto tcpCurrEstabGauge =
      registry_->gauge(registry_->CreateId("net.tcp.currEstab", Tags{}));

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
  tcpInSegsCtr.Set(static_cast<int64_t>(tcpInSegs));
  tcpOutSegsCtr.Set(static_cast<int64_t>(tcpOutSegs));
  tcpRetransSegsCtr.Set(static_cast<int64_t>(tcpRetransSegs));
  tcpActiveOpensCtr.Set(static_cast<int64_t>(tcpActiveOpens));
  tcpPassiveOpensCtr.Set(static_cast<int64_t>(tcpPassiveOpens));
  tcpAttemptFailsCtr.Set(static_cast<int64_t>(tcpAttemptFails));
  tcpEstabResetsCtr.Set(static_cast<int64_t>(tcpEstabResets));
  tcpCurrEstabGauge->Update(tcpCurrEstab);

  if (ret > 12) {
    tcpInErrsCtr.Set(static_cast<int64_t>(tcpInErrs));
  }
  if (ret > 13) {
    tcpOutRstsCtr.Set(static_cast<int64_t>(tcpOutRsts));
  }
}

void Proc::parse_udp_stats(const char* buf) noexcept {
  static MonotonicCounter udpInDatagramsCtr(
      registry_, registry_->CreateId("net.udp.datagrams", Tags{{"id", "in"}}));
  static MonotonicCounter udpOutDatagramsCtr(
      registry_, registry_->CreateId("net.udp.datagrams", Tags{{"id", "out"}}));
  static MonotonicCounter udpInErrorsCtr(
      registry_, registry_->CreateId("net.udp.errors", Tags{{"id", "inErrors"}}));

  if (buf == nullptr) {
    return;
  }

  u_long udpInDatagrams, udpNoPorts, udpInErrors, udpOutDatagrams;
  sscanf(buf, UDP_STATS_LINE, &udpInDatagrams, &udpNoPorts, &udpInErrors, &udpOutDatagrams);

  udpInDatagramsCtr.Set(static_cast<int64_t>(udpInDatagrams));
  udpInErrorsCtr.Set(static_cast<int64_t>(udpInErrors));
  udpOutDatagramsCtr.Set(static_cast<int64_t>(udpOutDatagrams));
}

void Proc::parse_load_avg(const char* buf) noexcept {
  static auto loadAvg1Gauge = registry_->gauge(registry_->CreateId("sys.load.1", Tags{}));
  static auto loadAvg5Gauge = registry_->gauge(registry_->CreateId("sys.load.5", Tags{}));
  static auto loadAvg15Gauge = registry_->gauge(registry_->CreateId("sys.load.15", Tags{}));

  double loadAvg1, loadAvg5, loadAvg15;
  sscanf(buf, LOADAVG_LINE, &loadAvg1, &loadAvg5, &loadAvg15);

  loadAvg1Gauge->Update(loadAvg1);
  loadAvg5Gauge->Update(loadAvg5);
  loadAvg15Gauge->Update(loadAvg15);
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

struct cpu_gauges {
  using gauge_ptr = std::shared_ptr<atlas::meter::Gauge<double>>;
  using gauge_maker_t =
      std::function<gauge_ptr(Registry* registry, const char* name, const char* id)>;
  cpu_gauges(Registry* registry, const char* name, const gauge_maker_t& gauge_maker)
      : user_gauge(gauge_maker(registry, name, "user")),
        system_gauge(gauge_maker(registry, name, "system")),
        stolen_gauge(gauge_maker(registry, name, "stolen")),
        nice_gauge(gauge_maker(registry, name, "nice")),
        wait_gauge(gauge_maker(registry, name, "wait")),
        interrupt_gauge(gauge_maker(registry, name, "interrupt")) {}

  std::shared_ptr<atlas::meter::Gauge<double>> user_gauge;
  std::shared_ptr<atlas::meter::Gauge<double>> system_gauge;
  std::shared_ptr<atlas::meter::Gauge<double>> stolen_gauge;
  std::shared_ptr<atlas::meter::Gauge<double>> nice_gauge;
  std::shared_ptr<atlas::meter::Gauge<double>> wait_gauge;
  std::shared_ptr<atlas::meter::Gauge<double>> interrupt_gauge;

  void update(const cpu_gauge_vals& vals) {
    user_gauge->Update(vals.user);
    system_gauge->Update(vals.system);
    stolen_gauge->Update(vals.stolen);
    nice_gauge->Update(vals.nice);
    wait_gauge->Update(vals.wait);
    interrupt_gauge->Update(vals.interrupt);
  }
};

struct cores_dist_summary {
  cores_dist_summary(Registry* registry, const char* name)
      : usage_ds(registry->ddistribution_summary(name)) {}

  std::shared_ptr<atlas::meter::DDistributionSummary> usage_ds;

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

  bool is_init() const noexcept { return !std::isnan(total); }

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
                           MonotonicCounter* ctr) {
  auto it = stats.find(key);
  if (it != stats.end()) {
    ctr->Set(it->second);
  }
}

void Proc::vmstats() noexcept {
  static MonotonicCounter processes{registry_,
                                    registry_->CreateId("vmstat.procs.count", kEmptyTags)};
  static auto procs_running =
      registry_->gauge(registry_->CreateId("vmstat.procs", {{"id", "running"}}));
  static auto procs_blocked =
      registry_->gauge(registry_->CreateId("vmstat.procs", {{"id", "blocked"}}));

  static MonotonicCounter page_in{registry_,
                                  registry_->CreateId("vmstat.paging", Tags{{"id", "in"}})};
  static MonotonicCounter page_out{registry_,
                                   registry_->CreateId("vmstat.paging", Tags{{"id", "out"}})};
  static MonotonicCounter swap_in{registry_,
                                  registry_->CreateId("vmstat.swapping", Tags{{"id", "in"}})};
  static MonotonicCounter swap_out{registry_,
                                   registry_->CreateId("vmstat.swapping", Tags{{"id", "out"}})};

  static auto fh_alloc = registry_->gauge(registry_->CreateId("vmstat.fh.allocated", kEmptyTags));
  static auto fh_max = registry_->gauge(registry_->CreateId("vmstat.fh.max", kEmptyTags));

  auto fp = open_file(path_prefix_, "stat");
  if (fp == nullptr) {
    return;
  }

  char line[2048];
  while (fgets(line, sizeof line, fp) != nullptr) {
    if (starts_with(line, "processes")) {
      u_long n;
      sscanf(line, "processes %lu", &n);
      processes.Set(n);
    } else if (starts_with(line, "procs_running")) {
      u_long n;
      sscanf(line, "procs_running %lu", &n);
      procs_running->Update(n);
    } else if (starts_with(line, "procs_blocked")) {
      u_long n;
      sscanf(line, "procs_blocked %lu", &n);
      procs_blocked->Update(n);
    }
  }

  std::unordered_map<std::string, int64_t> vmstats;
  parse_kv_from_file(path_prefix_, "vmstat", &vmstats);
  set_if_present(vmstats, "pgpgin", &page_in);
  set_if_present(vmstats, "pgpgout", &page_out);
  set_if_present(vmstats, "pswpin", &swap_in);
  set_if_present(vmstats, "pswpout", &swap_out);

  auto fh = open_file(path_prefix_, "sys/fs/file-nr");
  if (fgets(line, sizeof line, fh) != nullptr) {
    u_long alloc, used, max;
    if (sscanf(line, "%lu %lu %lu", &alloc, &used, &max) == 3) {
      fh_alloc->Update(alloc);
      fh_max->Update(max);
    }
  }
}

void Proc::cpu_stats() noexcept {
  static detail::cpu_gauges utilizationGauges{
      registry_, "sys.cpu.utilization", [](Registry* r, const char* name, const char* id) {
        return r->gauge(r->CreateId(name, Tags{{"id", id}}));
      }};
  static detail::cpu_gauges peakUtilizationGauges{
      registry_, "sys.cpu.peakUtilization", [](Registry* r, const char* name, const char* id) {
        return r->max_gauge(r->CreateId(name, Tags{{"id", id}}));
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
  if (prev_vals.is_init()) {
    auto gauge_vals = vals.compute_vals(prev_vals);
    utilizationGauges.update(gauge_vals);
    peakUtilizationGauges.update(gauge_vals);
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
  static auto avail_real = registry_->gauge(registry_->CreateId("mem.availReal", kEmptyTags));
  static auto free_real = registry_->gauge(registry_->CreateId("mem.freeReal", kEmptyTags));
  static auto total_real = registry_->gauge(registry_->CreateId("mem.totalReal", kEmptyTags));
  static auto avail_swap = registry_->gauge(registry_->CreateId("mem.availSwap", kEmptyTags));
  static auto total_swap = registry_->gauge(registry_->CreateId("mem.totalSwap", kEmptyTags));
  static auto buffer = registry_->gauge(registry_->CreateId("mem.buffer", kEmptyTags));
  static auto cached = registry_->gauge(registry_->CreateId("mem.cached", kEmptyTags));
  static auto shared = registry_->gauge(registry_->CreateId("mem.shared", kEmptyTags));
  static auto total_free = registry_->gauge(registry_->CreateId("mem.totalFree", kEmptyTags));

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
      total_real->Update(n * 1024.0);
    } else if (starts_with(line, "MemFree:")) {
      u_long n;
      sscanf(line, "MemFree: %lu", &n);
      free_real->Update(n * 1024.0);
      total_free_bytes += n;
    } else if (starts_with(line, "MemAvailable:")) {
      u_long n;
      sscanf(line, "MemAvailable: %lu", &n);
      avail_real->Update(n * 1024.0);
    } else if (starts_with(line, "SwapFree:")) {
      u_long n;
      sscanf(line, "SwapFree: %lu", &n);
      avail_swap->Update(n * 1024.0);
      total_free_bytes += n;
    } else if (starts_with(line, "SwapTotal:")) {
      u_long n;
      sscanf(line, "SwapTotal: %lu", &n);
      total_swap->Update(n * 1024.0);
    } else if (starts_with(line, "Buffers:")) {
      u_long n;
      sscanf(line, "Buffers: %lu", &n);
      buffer->Update(n * 1024.0);
    } else if (starts_with(line, "Cached:")) {
      u_long n;
      sscanf(line, "Cached: %lu", &n);
      cached->Update(n * 1024.0);
    } else if (starts_with(line, "Shmem:")) {
      u_long n;
      sscanf(line, "Shmem: %lu", &n);
      shared->Update(n * 1024.0);
    }
  }
  total_free->Update(total_free_bytes * 1024.0);
}

}  // namespace atlasagent
