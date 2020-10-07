#include "../util.h"
#include "absl/strings/str_split.h"
#include <cinttypes>
#include <cstring>
#include <utility>

namespace atlasagent {

inline void discard_line(FILE* fp) {
  for (auto ch = getc_unlocked(fp); ch != EOF && ch != '\n'; ch = getc_unlocked(fp)) {
    // just keep reading until a newline is found
  }
}

using spectator::Id;
using spectator::IdPtr;
using spectator::Tags;

inline IdPtr id_for(const char* name, const char* iface, const char* idStr,
                    const spectator::Tags& extra) noexcept {
  Tags tags{extra};
  tags.add("iface", iface);
  if (idStr != nullptr) {
    tags.add("id", idStr);
  }
  return Id::of(name, tags);
}

template <typename Reg>
void Proc<Reg>::handle_line(FILE* fp) noexcept {
  char iface[4096];
  int64_t bytes, packets, errs, drop, fifo, frame, compressed, multicast, colls, carrier;

  auto assigned =
      fscanf(fp,
             "%s %" PRId64 " %" PRId64 " %" PRId64 " %" PRId64 " %" PRId64 " %" PRId64 " %" PRId64
             " %" PRId64,
             iface, &bytes, &packets, &errs, &drop, &fifo, &frame, &compressed, &multicast);
  if (assigned > 0) {
    iface[strlen(iface) - 1] = '\0';  // strip trailing ':'

    registry_->GetMonotonicCounter(id_for("net.iface.bytes", iface, "in", net_tags_))->Set(bytes);
    registry_->GetMonotonicCounter(id_for("net.iface.packets", iface, "in", net_tags_))
        ->Set(packets);
    registry_->GetMonotonicCounter(id_for("net.iface.errors", iface, "in", net_tags_))
        ->Set(errs + fifo + frame);
    registry_->GetMonotonicCounter(id_for("net.iface.droppedPackets", iface, "in", net_tags_))
        ->Set(drop);
  }

  assigned = fscanf(fp,
                    " %" PRId64 " %" PRId64 " %" PRId64 " %" PRId64 " %" PRId64 " %" PRId64
                    " %" PRId64 " %" PRId64,
                    &bytes, &packets, &errs, &drop, &fifo, &colls, &carrier, &compressed);
  if (assigned > 0) {
    registry_->GetMonotonicCounter(id_for("net.iface.bytes", iface, "out", net_tags_))->Set(bytes);
    registry_->GetMonotonicCounter(id_for("net.iface.packets", iface, "out", net_tags_))
        ->Set(packets);
    registry_->GetMonotonicCounter(id_for("net.iface.errors", iface, "out", net_tags_))
        ->Set(errs + fifo);
    registry_->GetMonotonicCounter(id_for("net.iface.droppedPackets", iface, "out", net_tags_))
        ->Set(drop);
    registry_->GetMonotonicCounter(id_for("net.iface.collisions", iface, nullptr, net_tags_))
        ->Set(colls);
  }
}

template <typename Reg>
void Proc<Reg>::network_stats() noexcept {
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
    std::vector<std::string> fields = absl::StrSplit(line, absl::ByAnyChar("\n\t "), absl::SkipEmpty());
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

inline IdPtr create_id(const char* name, const Tags& tags, Tags extra) {
  Tags all_tags{tags};
  all_tags.move_all(std::move(extra));
  return Id::of(name, all_tags);
}

template <typename Reg>
inline auto tcpstate_gauge(Reg* registry, const char* state, const char* protocol,
                           const Tags& extra) {
  return registry->GetGauge(
      create_id("net.tcp.connectionStates", {{"id", state}, {"proto", protocol}}, extra));
}

template <typename Reg>
inline auto make_tcp_gauges(Reg* registry_, const char* protocol, const Tags& extra)
    -> std::array<typename Reg::gauge_ptr, kConnStates> {
  return {typename Reg::gauge_ptr{nullptr},
          tcpstate_gauge<Reg>(registry_, "established", protocol, extra),
          tcpstate_gauge<Reg>(registry_, "synSent", protocol, extra),
          tcpstate_gauge<Reg>(registry_, "synRecv", protocol, extra),
          tcpstate_gauge<Reg>(registry_, "finWait1", protocol, extra),
          tcpstate_gauge<Reg>(registry_, "finWait2", protocol, extra),
          tcpstate_gauge<Reg>(registry_, "timeWait", protocol, extra),
          tcpstate_gauge<Reg>(registry_, "close", protocol, extra),
          tcpstate_gauge<Reg>(registry_, "closeWait", protocol, extra),
          tcpstate_gauge<Reg>(registry_, "lastAck", protocol, extra),
          tcpstate_gauge<Reg>(registry_, "listen", protocol, extra),
          tcpstate_gauge<Reg>(registry_, "closing", protocol, extra)};
}

template <typename Reg>
inline void update_tcpstates_for_proto(
    const std::array<typename Reg::gauge_ptr, kConnStates>& gauges, FILE* fp) {
  std::array<int, kConnStates> connections{};
  if (fp != nullptr) {
    sum_tcp_states(fp, &connections);
    for (auto i = 1; i < kConnStates; ++i) {
      gauges[i]->Set(connections[i]);
    }
  }
}

template <typename Reg>
void Proc<Reg>::parse_tcp_connections() noexcept {
  static std::array<typename Reg::gauge_ptr, kConnStates> v4_states =
      make_tcp_gauges(registry_, "v4", net_tags_);
  static std::array<typename Reg::gauge_ptr, kConnStates> v6_states =
      make_tcp_gauges(registry_, "v6", net_tags_);

  update_tcpstates_for_proto<Reg>(v4_states, open_file(path_prefix_, "net/tcp"));
  update_tcpstates_for_proto<Reg>(v6_states, open_file(path_prefix_, "net/tcp6"));
}

// replicate what snmpd is doing
template <typename Reg>
void Proc<Reg>::snmp_stats() noexcept {
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

template <typename Reg>
void Proc<Reg>::parse_ip_stats(const char* buf) noexcept {
  static auto ipInReceivesCtr =
      registry_->GetMonotonicCounter(create_id("net.ip.datagrams", Tags{{"id", "in"}}, net_tags_));
  static auto ipInDicardsCtr =
      registry_->GetMonotonicCounter(create_id("net.ip.discards", Tags{{"id", "in"}}, net_tags_));
  static auto ipOutRequestsCtr =
      registry_->GetMonotonicCounter(create_id("net.ip.datagrams", Tags{{"id", "out"}}, net_tags_));
  static auto ipOutDiscardsCtr =
      registry_->GetMonotonicCounter(create_id("net.ip.discards", Tags{{"id", "out"}}, net_tags_));
  static auto ipReasmReqdsCtr = registry_->GetMonotonicCounter("net.ip.reasmReqds", net_tags_);

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

template <typename Reg>
void Proc<Reg>::parse_tcp_stats(const char* buf) noexcept {
  static auto tcpInSegsCtr =
      registry_->GetMonotonicCounter(create_id("net.tcp.segments", Tags{{"id", "in"}}, net_tags_));
  static auto tcpOutSegsCtr =
      registry_->GetMonotonicCounter(create_id("net.tcp.segments", Tags{{"id", "out"}}, net_tags_));
  static auto tcpRetransSegsCtr = registry_->GetMonotonicCounter(
      create_id("net.tcp.errors", Tags{{"id", "retransSegs"}}, net_tags_));
  static auto tcpInErrsCtr = registry_->GetMonotonicCounter(
      create_id("net.tcp.errors", Tags{{"id", "inErrs"}}, net_tags_));
  static auto tcpOutRstsCtr = registry_->GetMonotonicCounter(
      create_id("net.tcp.errors", Tags{{"id", "outRsts"}}, net_tags_));
  static auto tcpAttemptFailsCtr = registry_->GetMonotonicCounter(
      create_id("net.tcp.errors", Tags{{"id", "attemptFails"}}, net_tags_));
  static auto tcpEstabResetsCtr = registry_->GetMonotonicCounter(
      create_id("net.tcp.errors", Tags{{"id", "estabResets"}}, net_tags_));
  static auto tcpActiveOpensCtr =
      registry_->GetMonotonicCounter(create_id("net.tcp.opens", Tags{{"id", "active"}}, net_tags_));
  static auto tcpPassiveOpensCtr = registry_->GetMonotonicCounter(
      create_id("net.tcp.opens", Tags{{"id", "passive"}}, net_tags_));
  static auto tcpCurrEstabGauge = registry_->GetGauge("net.tcp.currEstab", net_tags_);

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

template <typename Reg>
void Proc<Reg>::parse_udp_stats(const char* buf) noexcept {
  static auto udpInDatagramsCtr =
      registry_->GetMonotonicCounter(create_id("net.udp.datagrams", Tags{{"id", "in"}}, net_tags_));
  static auto udpOutDatagramsCtr = registry_->GetMonotonicCounter(
      create_id("net.udp.datagrams", Tags{{"id", "out"}}, net_tags_));
  static auto udpInErrorsCtr = registry_->GetMonotonicCounter(
      create_id("net.udp.errors", Tags{{"id", "inErrors"}}, net_tags_));

  if (buf == nullptr) {
    return;
  }

  u_long udpInDatagrams, udpNoPorts, udpInErrors, udpOutDatagrams;
  sscanf(buf, UDP_STATS_LINE, &udpInDatagrams, &udpNoPorts, &udpInErrors, &udpOutDatagrams);

  udpInDatagramsCtr->Set(udpInDatagrams);
  udpInErrorsCtr->Set(udpInErrors);
  udpOutDatagramsCtr->Set(udpOutDatagrams);
}

template <typename Reg>
void Proc<Reg>::parse_load_avg(const char* buf) noexcept {
  static auto loadAvg1Gauge = registry_->GetGauge("sys.load.1");
  static auto loadAvg5Gauge = registry_->GetGauge("sys.load.5");
  static auto loadAvg15Gauge = registry_->GetGauge("sys.load.15");

  double loadAvg1, loadAvg5, loadAvg15;
  sscanf(buf, LOADAVG_LINE, &loadAvg1, &loadAvg5, &loadAvg15);

  loadAvg1Gauge->Set(loadAvg1);
  loadAvg5Gauge->Set(loadAvg5);
  loadAvg15Gauge->Set(loadAvg15);
}

template <typename Reg>
void Proc<Reg>::loadavg_stats() noexcept {
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
inline int get_pid_from_sched(const char* sched_line) noexcept {
  auto parens = strchr(sched_line, '(');
  if (parens == nullptr) {
    return -1;
  }
  parens++;  // point to the first digit
  return atoi(parens);
}
}  // namespace proc

template <typename Reg>
bool Proc<Reg>::is_container() const noexcept {
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

template <typename Reg>
void Proc<Reg>::set_prefix(const std::string& new_prefix) noexcept {
  path_prefix_ = new_prefix;
}

namespace detail {
struct cpu_gauge_vals {
  double user;
  double system;
  double stolen;
  double nice;
  double wait;
  double interrupt;
};

template <typename Reg, typename G>
struct cpu_gauges {
  using gauge_ptr = std::shared_ptr<G>;
  using gauge_maker_t = std::function<gauge_ptr(Reg* registry, const char* name, const char* id)>;
  cpu_gauges(Reg* registry, const char* name, const gauge_maker_t& gauge_maker)
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

template <typename Reg>
struct cores_dist_summary {
  cores_dist_summary(Reg* registry, const char* name)
      : usage_ds(registry->GetDistributionSummary(name)) {}

  typename Reg::dist_summary_ptr usage_ds;

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

template <typename MonoCounter>
inline void set_if_present(const std::unordered_map<std::string, int64_t>& stats, const char* key,
                           MonoCounter* ctr) {
  auto it = stats.find(key);
  if (it != stats.end()) {
    ctr->Set(it->second);
  }
}

template <typename Reg>
void Proc<Reg>::vmstats() noexcept {
  static auto processes = registry_->GetMonotonicCounter("vmstat.procs.count");
  static auto procs_running = registry_->GetGauge("vmstat.procs", {{"id", "running"}});
  static auto procs_blocked = registry_->GetGauge("vmstat.procs", {{"id", "blocked"}});

  static auto page_in = registry_->GetMonotonicCounter("vmstat.paging", {{"id", "in"}});
  static auto page_out = registry_->GetMonotonicCounter("vmstat.paging", {{"id", "out"}});
  static auto swap_in = registry_->GetMonotonicCounter("vmstat.swapping", {{"id", "in"}});
  static auto swap_out = registry_->GetMonotonicCounter("vmstat.swapping", {{"id", "out"}});
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

template <typename Reg>
void Proc<Reg>::peak_cpu_stats() noexcept {
  static detail::cpu_gauges<Reg, typename Reg::max_gauge_t> peakUtilizationGauges{
      registry_, "sys.cpu.peakUtilization", [](Reg* r, const char* name, const char* id) {
        return r->GetMaxGauge(name, {{"id", id}});
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

template <typename Reg>
void Proc<Reg>::cpu_stats() noexcept {
  static detail::cpu_gauges<Reg, typename Reg::gauge_t> utilizationGauges{
      registry_, "sys.cpu.utilization", [](Reg* r, const char* name, const char* id) {
        return r->GetGauge(name, {{"id", id}});
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

template <typename Reg>
void Proc<Reg>::memory_stats() noexcept {
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

template <typename Reg>
void Proc<Reg>::netstat_stats() noexcept {
  static auto ect_ctr = registry_->GetMonotonicCounter(
      create_id("net.ip.ectPackets", Tags{{"id", "capable"}}, net_tags_));
  static auto noEct_ctr = registry_->GetMonotonicCounter(
      create_id("net.ip.ectPackets", Tags{{"id", "notCapable"}}, net_tags_));
  static auto congested_ctr = registry_->GetMonotonicCounter("net.ip.congestedPackets", net_tags_);

  auto fp = open_file(path_prefix_, "net/netstat");
  if (fp == nullptr) {
    return;
  }

  int64_t noEct = 0, ect = 0, congested = 0;
  char line[1024];
  while (fgets(line, sizeof line, fp) != nullptr) {
    if (starts_with(line, "IpExt:")) {
      // get header indexes
      std::vector<std::string> headers = absl::StrSplit(line, absl::ByAnyChar(" \t\n"), absl::SkipEmpty());
      if (fgets(line, sizeof line, fp) == nullptr) {
        Logger()->warn("Unable to parse {}/net/netstat", path_prefix_);
        return;
      }
      std::vector<std::string> values = absl::StrSplit(line, absl::ByAnyChar(" \t\n"), absl::SkipEmpty());;
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

template <typename Reg>
void Proc<Reg>::arp_stats() noexcept {
  static auto arpcache_size = registry_->GetGauge("net.arpCacheSize", net_tags_);
  auto fp = open_file(path_prefix_, "net/arp");
  if (fp == nullptr) {
    return;
  }

  // discard the header
  discard_line(fp);
  auto num_entries = 0;
  char line[1024];
  while (fgets(line, sizeof line, fp) != nullptr) {
    if (isdigit(line[0])) {
      num_entries++;
    }
  }
  arpcache_size->Set(num_entries);
}

static bool all_digits(const char* str) {
  assert(*str != '\0');

  for (; *str != '\0'; ++str) {
    auto c = *str;
    if (!isdigit(c)) return false;
  }
  return true;
}

int32_t count_tasks(const std::string& dirname) {
  DirHandle dh{dirname.c_str()};
  if (!dh) {
    return 0;
  }

  auto count = 0;
  for (;;) {
    auto entry = readdir(dh);
    if (entry == nullptr) break;

    if (all_digits(entry->d_name)) {
      ++count;
    }
  }
  return count;
}

template <typename Reg>
void Proc<Reg>::process_stats() noexcept {
  static auto cur_pids = registry_->GetGauge("sys.currentProcesses");
  static auto cur_threads = registry_->GetGauge("sys.currentThreads");

  DirHandle dir_handle{path_prefix_.c_str()};
  if (!dir_handle) {
    return;
  }

  auto pids = 0, tasks = 0;
  for (;;) {
    auto entry = readdir(dir_handle);
    if (entry == nullptr) break;
    if (all_digits(entry->d_name)) {
      ++pids;
      auto task_dir = fmt::format("{}/{}/task", path_prefix_, entry->d_name);
      tasks += count_tasks(task_dir);
    }
  }
  cur_pids->Set(pids);
  cur_threads->Set(tasks);
}

}  // namespace atlasagent
