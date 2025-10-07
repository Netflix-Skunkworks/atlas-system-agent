#include "proc.h"
#include "proc_cpu.h"

#include <lib/util/src/util.h>
#include <absl/strings/str_split.h>
#include <absl/strings/str_join.h>
#include <absl/strings/numbers.h>
#include <cinttypes>
#include <cstring>
#include <utility>

namespace atlasagent
{

struct ProcStatConstants
{
    static constexpr unsigned int FirstProcessorIndex{1};
    static constexpr auto CpuPrefix = "cpu";
    static constexpr size_t ExpectedCpuFields = 11;
};

inline void discard_line(FILE* fp)
{
    for (auto ch = getc_unlocked(fp); ch != EOF && ch != '\n'; ch = getc_unlocked(fp))
    {
        // just keep reading until a newline is found
    }
}

void Proc::handle_line(FILE* fp) noexcept
{
    char iface[4096];
    int64_t bytes, packets, errs, drop, fifo, frame, compressed, multicast, colls, carrier;

    auto assigned =
        fscanf(fp, "%s %" PRId64 " %" PRId64 " %" PRId64 " %" PRId64 " %" PRId64 " %" PRId64 " %" PRId64 " %" PRId64,
               iface, &bytes, &packets, &errs, &drop, &fifo, &frame, &compressed, &multicast);
    if (assigned > 0)
    {
        iface[strlen(iface) - 1] = '\0';  // strip trailing ':'

        auto allTagsIn = this->net_tags_;
        allTagsIn.emplace("iface", iface);
        allTagsIn.emplace("id", "in");

        registry_->CreateMonotonicCounter("net.iface.bytes", allTagsIn).Set(bytes);
        registry_->CreateMonotonicCounter("net.iface.packets", allTagsIn).Set(packets);
        registry_->CreateMonotonicCounter("net.iface.errors", allTagsIn).Set(errs + fifo + frame);
        registry_->CreateMonotonicCounter("net.iface.droppedPackets", allTagsIn).Set(drop);
    }

    assigned =
        fscanf(fp, " %" PRId64 " %" PRId64 " %" PRId64 " %" PRId64 " %" PRId64 " %" PRId64 " %" PRId64 " %" PRId64,
               &bytes, &packets, &errs, &drop, &fifo, &colls, &carrier, &compressed);
    if (assigned > 0)
    {
        auto allTagsOut = this->net_tags_;

        allTagsOut.emplace("iface", iface);
        registry_->CreateMonotonicCounter("net.iface.collisions", allTagsOut).Set(colls);

        allTagsOut.emplace("id", "out");
        registry_->CreateMonotonicCounter("net.iface.bytes", allTagsOut).Set(bytes);
        registry_->CreateMonotonicCounter("net.iface.packets", allTagsOut).Set(packets);
        registry_->CreateMonotonicCounter("net.iface.errors", allTagsOut).Set(errs + fifo);
        registry_->CreateMonotonicCounter("net.iface.droppedPackets", allTagsOut).Set(drop);
    }
}

void Proc::network_stats() noexcept
{
    auto fp = open_file(path_prefix_, "net/dev");
    if (fp == nullptr)
    {
        return;
    }
    discard_line(fp);
    discard_line(fp);

    while (!feof(fp))
    {
        handle_line(fp);
    }
}

static constexpr const char* IP_STATS_LINE =
    "Ip: %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu "
    "%lu %lu";
static constexpr size_t IP_STATS_PREFIX_LEN = 4;
static constexpr const char* TCP_STATS_LINE = "Tcp: %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu";
static constexpr size_t TCP_STATS_PREFIX_LEN = 5;
static constexpr const char* UDP_STATS_LINE = "Udp: %lu %lu %lu %lu";
static constexpr size_t UDP_STATS_PREFIX_LEN = 5;
static constexpr const char* LOADAVG_LINE = "%lf %lf %lf";

static constexpr int kConnStates = 11;
void sum_tcp_states(FILE* fp, std::array<int, kConnStates>* connections) noexcept
{
    char line[2048];
    // discard header
    if (fgets(line, sizeof line, fp) == nullptr)
    {
        return;
    }
    while (fgets(line, sizeof line, fp) != nullptr)
    {
        std::vector<std::string> fields = absl::StrSplit(line, absl::ByAnyChar("\n\t "), absl::SkipEmpty());
        // all lines have at least 12 fields. Just being extra paranoid here:
        if (fields.size() < 4)
        {
            continue;
        }
        const char* st = fields[3].c_str();
        auto state = static_cast<int>(strtol(st, nullptr, 16));
        if (state > 0 && state <= kConnStates)
        {
            ++(*connections)[state - 1];
        }
        else
        {
            Logger()->info("Ignoring connection state {} for line: {}", state, line);
        }
    }
}

inline auto tcpstate_gauge(Registry* registry, const char* state, const char* protocol,
                           const std::unordered_map<std::string, std::string>& extra)
{
    std::unordered_map<std::string, std::string> tags{{"id", state}, {"proto", protocol}};
    tags.insert(extra.begin(), extra.end());
    return registry->CreateGauge("net.tcp.connectionStates", tags);
}

inline auto make_tcp_gauges(Registry* registry_, const char* protocol,
                            const std::unordered_map<std::string, std::string>& extra) -> std::array<Gauge, kConnStates>
{
    return {tcpstate_gauge(registry_, "established", protocol, extra),
            tcpstate_gauge(registry_, "synSent", protocol, extra),
            tcpstate_gauge(registry_, "synRecv", protocol, extra),
            tcpstate_gauge(registry_, "finWait1", protocol, extra),
            tcpstate_gauge(registry_, "finWait2", protocol, extra),
            tcpstate_gauge(registry_, "timeWait", protocol, extra),
            tcpstate_gauge(registry_, "close", protocol, extra),
            tcpstate_gauge(registry_, "closeWait", protocol, extra),
            tcpstate_gauge(registry_, "lastAck", protocol, extra),
            tcpstate_gauge(registry_, "listen", protocol, extra),
            tcpstate_gauge(registry_, "closing", protocol, extra)};
}

inline void update_tcpstates_for_proto(const std::array<Gauge, kConnStates>& gauges, FILE* fp)
{
    std::array<int, kConnStates> connections{};
    if (fp != nullptr)
    {
        sum_tcp_states(fp, &connections);
        for (auto i = 0; i < kConnStates; ++i)
        {
            gauges[i].Set(connections[i]);
        }
    }
}

void Proc::parse_tcp_connections() noexcept
{
    static std::array<Gauge, kConnStates> v4_states = make_tcp_gauges(registry_, "v4", net_tags_);
    static std::array<Gauge, kConnStates> v6_states = make_tcp_gauges(registry_, "v6", net_tags_);

    update_tcpstates_for_proto(v4_states, open_file(path_prefix_, "net/tcp"));
    update_tcpstates_for_proto(v6_states, open_file(path_prefix_, "net/tcp6"));
}

// replicate what snmpd is doing
void Proc::snmp_stats() noexcept
{
    auto fp = open_file(path_prefix_, "net/snmp");
    if (fp == nullptr)
    {
        return;
    }

    char line[1024];
    while (fgets(line, sizeof line, fp) != nullptr)
    {
        if (strncmp(line, IP_STATS_LINE, IP_STATS_PREFIX_LEN) == 0)
        {
            if (fgets(line, sizeof line, fp) != nullptr)
            {
                parse_ip_stats(line);
            }
        }
        else if (strncmp(line, TCP_STATS_LINE, TCP_STATS_PREFIX_LEN) == 0)
        {
            if (fgets(line, sizeof line, fp) != nullptr)
            {
                parse_tcp_stats(line);
            }
        }
        else if (strncmp(line, UDP_STATS_LINE, UDP_STATS_PREFIX_LEN) == 0)
        {
            if (fgets(line, sizeof line, fp) != nullptr)
            {
                parse_udp_stats(line);
            }
        }
    }

    parse_tcp_connections();

    std::unordered_map<std::string, int64_t> stats;
    parse_kv_from_file(path_prefix_, "net/snmp6", &stats);
    parse_ipv6_stats(stats);
    parse_udpv6_stats(stats);
}

void Proc::parse_ipv6_stats(const std::unordered_map<std::string, int64_t>& snmp_stats) noexcept
{
    auto inTags = std::unordered_map<std::string, std::string>{{"id", "in"}, {"proto", "v6"}};
    auto outTags = std::unordered_map<std::string, std::string>{{"id", "out"}, {"proto", "v6"}};
    auto protoTags = std::unordered_map<std::string, std::string>{{"proto", "v6"}};

    inTags.insert(net_tags_.begin(), net_tags_.end());
    outTags.insert(net_tags_.begin(), net_tags_.end());
    protoTags.insert(net_tags_.begin(), net_tags_.end());

    static auto ipInReceivesCtr = registry_->CreateMonotonicCounter("net.ip.datagrams", inTags);
    static auto ipInDicardsCtr = registry_->CreateMonotonicCounter("net.ip.discards", inTags);
    static auto ipOutRequestsCtr = registry_->CreateMonotonicCounter("net.ip.datagrams", outTags);
    static auto ipOutDiscardsCtr = registry_->CreateMonotonicCounter("net.ip.discards", outTags);
    static auto ipReasmReqdsCtr = registry_->CreateMonotonicCounter("net.ip.reasmReqds", protoTags);

    // the ipv4 metrics for these come from net/netstat but net/snmp6 include them

    auto capableTags = std::unordered_map<std::string, std::string>{{"id", "capable"}, {"proto", "v6"}};
    auto notCapableTags = std::unordered_map<std::string, std::string>{{"id", "notCapable"}, {"proto", "v6"}};
    capableTags.insert(net_tags_.begin(), net_tags_.end());
    notCapableTags.insert(net_tags_.begin(), net_tags_.end());

    static auto ect_ctr = registry_->CreateMonotonicCounter("net.ip.ectPackets", capableTags);
    static auto noEct_ctr = registry_->CreateMonotonicCounter("net.ip.ectPackets", notCapableTags);
    static auto congested_ctr = registry_->CreateMonotonicCounter("net.ip.congestedPackets", protoTags);

    auto in_receives = snmp_stats.find("Ip6InReceives");
    auto in_discards = snmp_stats.find("Ip6InDiscards");
    auto out_reqs = snmp_stats.find("Ip6OutRequests");
    auto out_discards = snmp_stats.find("Ip6OutDiscards");
    auto reassembly_reqd = snmp_stats.find("Ip6ReasmReqds");
    auto ectCapable0 = snmp_stats.find("Ip6InECT0Pkts");
    auto ectCapable1 = snmp_stats.find("Ip6InECT1Pkts");
    auto noEct = snmp_stats.find("Ip6InNoECTPkts");
    auto congested = snmp_stats.find("Ip6InCEPkts");

    if (in_receives != snmp_stats.end())
    {
        ipInReceivesCtr.Set(in_receives->second);
    }
    if (in_discards != snmp_stats.end())
    {
        ipInDicardsCtr.Set(in_discards->second);
    }
    if (out_reqs != snmp_stats.end())
    {
        ipOutRequestsCtr.Set(out_reqs->second);
    }
    if (out_discards != snmp_stats.end())
    {
        ipOutDiscardsCtr.Set(out_discards->second);
    }
    if (reassembly_reqd != snmp_stats.end())
    {
        ipReasmReqdsCtr.Set(reassembly_reqd->second);
    }
    int64_t ectCapable = 0;
    if (ectCapable0 != snmp_stats.end())
    {
        ectCapable += ectCapable0->second;
    }
    if (ectCapable1 != snmp_stats.end())
    {
        ectCapable += ectCapable1->second;
    }
    ect_ctr.Set(ectCapable);
    if (noEct != snmp_stats.end())
    {
        noEct_ctr.Set(noEct->second);
    }
    if (congested != snmp_stats.end())
    {
        congested_ctr.Set(congested->second);
    }
}

void Proc::parse_udpv6_stats(const std::unordered_map<std::string, int64_t>& snmp_stats) noexcept
{
    auto inTags = std::unordered_map<std::string, std::string>{{"id", "in"}, {"proto", "v6"}};
    auto outTags = std::unordered_map<std::string, std::string>{{"id", "out"}, {"proto", "v6"}};
    auto errorTags = std::unordered_map<std::string, std::string>{{"id", "inErrors"}, {"proto", "v6"}};
    inTags.insert(net_tags_.begin(), net_tags_.end());
    outTags.insert(net_tags_.begin(), net_tags_.end());
    errorTags.insert(net_tags_.begin(), net_tags_.end());

    static auto udpInDatagramsCtr = registry_->CreateMonotonicCounter("net.udp.datagrams", inTags);
    static auto udpOutDatagramsCtr = registry_->CreateMonotonicCounter("net.udp.datagrams", outTags);
    static auto udpInErrorsCtr = registry_->CreateMonotonicCounter("net.udp.errors", errorTags);

    auto in_datagrams = snmp_stats.find("Udp6InDatagrams");
    auto in_errors = snmp_stats.find("Udp6InErrors");
    auto out_datagrams = snmp_stats.find("Udp6OutDatagrams");

    if (in_datagrams != snmp_stats.end())
    {
        udpInDatagramsCtr.Set(in_datagrams->second);
    }
    if (in_errors != snmp_stats.end())
    {
        udpInErrorsCtr.Set(in_errors->second);
    }
    if (out_datagrams != snmp_stats.end())
    {
        udpOutDatagramsCtr.Set(out_datagrams->second);
    }
}

void Proc::parse_ip_stats(const char* buf) noexcept
{
    auto inTags = std::unordered_map<std::string, std::string>{{"id", "in"}, {"proto", "v4"}};
    auto outTags = std::unordered_map<std::string, std::string>{{"id", "out"}, {"proto", "v4"}};
    auto protoTags = std::unordered_map<std::string, std::string>{{"proto", "v4"}};
    inTags.insert(net_tags_.begin(), net_tags_.end());
    outTags.insert(net_tags_.begin(), net_tags_.end());
    protoTags.insert(net_tags_.begin(), net_tags_.end());

    static auto ipInReceivesCtr = registry_->CreateMonotonicCounter("net.ip.datagrams", inTags);
    static auto ipInDicardsCtr = registry_->CreateMonotonicCounter("net.ip.discards", inTags);
    static auto ipOutRequestsCtr = registry_->CreateMonotonicCounter("net.ip.datagrams", outTags);
    static auto ipOutDiscardsCtr = registry_->CreateMonotonicCounter("net.ip.discards", outTags);
    static auto ipReasmReqdsCtr = registry_->CreateMonotonicCounter("net.ip.reasmReqds", protoTags);

    u_long ipForwarding, ipDefaultTTL, ipInReceives, ipInHdrErrors, ipInAddrErrors, ipForwDatagrams, ipInUnknownProtos,
        ipInDiscards, ipInDelivers, ipOutRequests, ipOutDiscards, ipOutNoRoutes, ipReasmTimeout, ipReasmReqds,
        ipReasmOKs, ipReasmFails, ipFragOKs, ipFragFails, ipFragCreates;

    if (buf == nullptr)
    {
        return;
    }

    sscanf(buf, IP_STATS_LINE, &ipForwarding, &ipDefaultTTL, &ipInReceives, &ipInHdrErrors, &ipInAddrErrors,
           &ipForwDatagrams, &ipInUnknownProtos, &ipInDiscards, &ipInDelivers, &ipOutRequests, &ipOutDiscards,
           &ipOutNoRoutes, &ipReasmTimeout, &ipReasmReqds, &ipReasmOKs, &ipReasmFails, &ipFragOKs, &ipFragFails,
           &ipFragCreates);

    ipInReceivesCtr.Set(ipInReceives);
    ipInDicardsCtr.Set(ipInDiscards);
    ipOutRequestsCtr.Set(ipOutRequests);
    ipOutDiscardsCtr.Set(ipOutDiscards);
    ipReasmReqdsCtr.Set(ipReasmReqds);
}

void Proc::parse_tcp_stats(const char* buf) noexcept
{
    auto inTags = std::unordered_map<std::string, std::string>{{"id", "in"}};
    auto outTags = std::unordered_map<std::string, std::string>{{"id", "out"}};
    auto retransTags = std::unordered_map<std::string, std::string>{{"id", "retransSegs"}};
    auto inErrsTags = std::unordered_map<std::string, std::string>{{"id", "inErrs"}};
    auto outRstsTags = std::unordered_map<std::string, std::string>{{"id", "outRsts"}};
    auto attemptFailsTags = std::unordered_map<std::string, std::string>{{"id", "attemptFails"}};
    auto estabResetsTags = std::unordered_map<std::string, std::string>{{"id", "estabResets"}};
    auto activeOpensTags = std::unordered_map<std::string, std::string>{{"id", "active"}};
    auto passiveOpensTags = std::unordered_map<std::string, std::string>{{"id", "passive"}};

    inTags.insert(net_tags_.begin(), net_tags_.end());
    outTags.insert(net_tags_.begin(), net_tags_.end());
    retransTags.insert(net_tags_.begin(), net_tags_.end());
    inErrsTags.insert(net_tags_.begin(), net_tags_.end());
    outRstsTags.insert(net_tags_.begin(), net_tags_.end());
    attemptFailsTags.insert(net_tags_.begin(), net_tags_.end());
    estabResetsTags.insert(net_tags_.begin(), net_tags_.end());
    activeOpensTags.insert(net_tags_.begin(), net_tags_.end());
    passiveOpensTags.insert(net_tags_.begin(), net_tags_.end());

    static auto tcpInSegsCtr = registry_->CreateMonotonicCounter("net.tcp.segments", inTags);
    static auto tcpOutSegsCtr = registry_->CreateMonotonicCounter("net.tcp.segments", outTags);
    static auto tcpRetransSegsCtr = registry_->CreateMonotonicCounter("net.tcp.errors", retransTags);
    static auto tcpInErrsCtr = registry_->CreateMonotonicCounter("net.tcp.errors", inErrsTags);
    static auto tcpOutRstsCtr = registry_->CreateMonotonicCounter("net.tcp.errors", outRstsTags);
    static auto tcpAttemptFailsCtr = registry_->CreateMonotonicCounter("net.tcp.errors", attemptFailsTags);
    static auto tcpEstabResetsCtr = registry_->CreateMonotonicCounter("net.tcp.errors", estabResetsTags);
    static auto tcpActiveOpensCtr = registry_->CreateMonotonicCounter("net.tcp.opens", activeOpensTags);
    static auto tcpPassiveOpensCtr = registry_->CreateMonotonicCounter("net.tcp.opens", passiveOpensTags);
    static auto tcpCurrEstabGauge = registry_->CreateGauge("net.tcp.currEstab", net_tags_);

    if (buf == nullptr)
    {
        return;
    }

    u_long tcpRtoAlgorithm, tcpRtoMin, tcpRtoMax, tcpMaxConn, tcpActiveOpens, tcpPassiveOpens, tcpAttemptFails,
        tcpEstabResets, tcpCurrEstab, tcpInSegs, tcpOutSegs, tcpRetransSegs, tcpInErrs, tcpOutRsts;
    auto ret = sscanf(buf, TCP_STATS_LINE, &tcpRtoAlgorithm, &tcpRtoMin, &tcpRtoMax, &tcpMaxConn, &tcpActiveOpens,
                      &tcpPassiveOpens, &tcpAttemptFails, &tcpEstabResets, &tcpCurrEstab, &tcpInSegs, &tcpOutSegs,
                      &tcpRetransSegs, &tcpInErrs, &tcpOutRsts);
    tcpInSegsCtr.Set(tcpInSegs);
    tcpOutSegsCtr.Set(tcpOutSegs);
    tcpRetransSegsCtr.Set(tcpRetransSegs);
    tcpActiveOpensCtr.Set(tcpActiveOpens);
    tcpPassiveOpensCtr.Set(tcpPassiveOpens);
    tcpAttemptFailsCtr.Set(tcpAttemptFails);
    tcpEstabResetsCtr.Set(tcpEstabResets);
    tcpCurrEstabGauge.Set(tcpCurrEstab);

    if (ret > 12)
    {
        tcpInErrsCtr.Set(tcpInErrs);
    }
    if (ret > 13)
    {
        tcpOutRstsCtr.Set(tcpOutRsts);
    }
}

void Proc::parse_udp_stats(const char* buf) noexcept
{
    auto inTags = std::unordered_map<std::string, std::string>{{"id", "in"}, {"proto", "v4"}};
    auto outTags = std::unordered_map<std::string, std::string>{{"id", "out"}, {"proto", "v4"}};
    auto inErrorsTags = std::unordered_map<std::string, std::string>{{"id", "inErrors"}, {"proto", "v4"}};
    inTags.insert(net_tags_.begin(), net_tags_.end());
    outTags.insert(net_tags_.begin(), net_tags_.end());
    inErrorsTags.insert(net_tags_.begin(), net_tags_.end());

    static auto udpInDatagramsCtr = registry_->CreateMonotonicCounter("net.udp.datagrams", inTags);
    static auto udpOutDatagramsCtr = registry_->CreateMonotonicCounter("net.udp.datagrams", outTags);
    static auto udpInErrorsCtr = registry_->CreateMonotonicCounter("net.udp.errors", inErrorsTags);

    if (buf == nullptr)
    {
        return;
    }

    u_long udpInDatagrams, udpNoPorts, udpInErrors, udpOutDatagrams;
    sscanf(buf, UDP_STATS_LINE, &udpInDatagrams, &udpNoPorts, &udpInErrors, &udpOutDatagrams);

    udpInDatagramsCtr.Set(udpInDatagrams);
    udpInErrorsCtr.Set(udpInErrors);
    udpOutDatagramsCtr.Set(udpOutDatagrams);
}

void Proc::parse_load_avg(const char* buf) noexcept
{
    static auto loadAvg1Gauge = registry_->CreateGauge("sys.load.1");
    static auto loadAvg5Gauge = registry_->CreateGauge("sys.load.5");
    static auto loadAvg15Gauge = registry_->CreateGauge("sys.load.15");

    double loadAvg1, loadAvg5, loadAvg15;
    sscanf(buf, LOADAVG_LINE, &loadAvg1, &loadAvg5, &loadAvg15);

    loadAvg1Gauge.Set(loadAvg1);
    loadAvg5Gauge.Set(loadAvg5);
    loadAvg15Gauge.Set(loadAvg15);
}

void Proc::loadavg_stats() noexcept
{
    auto fp = open_file(path_prefix_, "loadavg");
    char line[1024];
    if (fp == nullptr)
    {
        return;
    }
    if (std::fgets(line, sizeof line, fp) != nullptr)
    {
        parse_load_avg(line);
    }
}

namespace proc
{
int get_pid_from_sched(const char* sched_line) noexcept
{
    auto parens = strchr(sched_line, '(');
    if (parens == nullptr)
    {
        return -1;
    }
    parens++;  // point to the first digit
    return atoi(parens);
}
}  // namespace proc

bool Proc::is_container() const noexcept
{
    auto fp = open_file(path_prefix_, "1/sched");
    if (fp == nullptr)
    {
        return false;
    }
    char line[1024];
    bool error = std::fgets(line, sizeof line, fp) == nullptr;
    if (error)
    {
        return false;
    }

    return proc::get_pid_from_sched(line) != 1;
}

void Proc::set_prefix(const std::string& new_prefix) noexcept { path_prefix_ = new_prefix; }

inline void set_if_present(const std::unordered_map<std::string, int64_t>& stats, const char* key,
                           const MonotonicCounter& ctr)
{
    auto it = stats.find(key);
    if (it != stats.end())
    {
        ctr.Set(it->second);
    }
}

void Proc::uptime_stats() noexcept
{
    static auto sys_uptime = registry_->CreateGauge("sys.uptime");
    // uptime values are in seconds, reported as doubles, but given how large they will be over
    // time, the 10ths of a second will not matter for the purpose of producing this metric
    auto uptime_seconds = read_num_vector_from_file(path_prefix_, "uptime");
    sys_uptime.Set(uptime_seconds[0]);
}

void Proc::vmstats() noexcept
{
    static auto processes = registry_->CreateMonotonicCounter("vmstat.procs.count");
    static auto procs_running = registry_->CreateGauge("vmstat.procs", {{"id", "running"}});
    static auto procs_blocked = registry_->CreateGauge("vmstat.procs", {{"id", "blocked"}});

    static auto page_in = registry_->CreateMonotonicCounter("vmstat.paging", {{"id", "in"}});
    static auto page_out = registry_->CreateMonotonicCounter("vmstat.paging", {{"id", "out"}});
    static auto swap_in = registry_->CreateMonotonicCounter("vmstat.swapping", {{"id", "in"}});
    static auto swap_out = registry_->CreateMonotonicCounter("vmstat.swapping", {{"id", "out"}});
    static auto fh_alloc = registry_->CreateGauge("vmstat.fh.allocated");
    static auto fh_max = registry_->CreateGauge("vmstat.fh.max");

    auto fp = open_file(path_prefix_, "stat");
    if (fp == nullptr)
    {
        return;
    }

    char line[2048];
    while (fgets(line, sizeof line, fp) != nullptr)
    {
        if (starts_with(line, "processes"))
        {
            u_long n;
            sscanf(line, "processes %lu", &n);
            processes.Set(n);
        }
        else if (starts_with(line, "procs_running"))
        {
            u_long n;
            sscanf(line, "procs_running %lu", &n);
            procs_running.Set(n);
        }
        else if (starts_with(line, "procs_blocked"))
        {
            u_long n;
            sscanf(line, "procs_blocked %lu", &n);
            procs_blocked.Set(n);
        }
    }

    std::unordered_map<std::string, int64_t> vmstats;
    parse_kv_from_file(path_prefix_, "vmstat", &vmstats);
    set_if_present(vmstats, "pgpgin", page_in);
    set_if_present(vmstats, "pgpgout", page_out);
    set_if_present(vmstats, "pswpin", swap_in);
    set_if_present(vmstats, "pswpout", swap_out);

    auto fh = open_file(path_prefix_, "sys/fs/file-nr");
    if (fgets(line, sizeof line, fh) != nullptr)
    {
        u_long alloc, used, max;
        if (sscanf(line, "%lu %lu %lu", &alloc, &used, &max) == 3)
        {
            fh_alloc.Set(alloc);
            fh_max.Set(max);
        }
    }
}

void Proc::PeakCpuStats(const std::vector<std::string>& aggregateLine) try
{
    static auto peakUtilizationGauges = CreatePeakCpuGauges(registry_, "sys.cpu.peakUtilization");
    static std::optional<CpuStatFields> previousAggregateStats;
    CpuStatFields currentStats(aggregateLine);
    if (previousAggregateStats.has_value())
    {
        auto computedVals = ComputeGaugeValues(previousAggregateStats.value(), currentStats);
        peakUtilizationGauges.update(computedVals);
    }
    previousAggregateStats = currentStats;
    return;
}
catch (const std::exception& ex)
{
    Logger()->error("Exception updating peak CPU stats: {}", ex.what());
    return;
}

void Proc::UpdateUtilizationGauges(const std::vector<std::string>& aggregateLine) try
{
    static auto utilizationGauges = CreateCpuGauges(registry_, "sys.cpu.utilization");
    static std::optional<CpuStatFields> previousAggregateStats;
    CpuStatFields currentStats(aggregateLine);
    if (previousAggregateStats.has_value())
    {
        auto computedVals = ComputeGaugeValues(previousAggregateStats.value(), currentStats);
        utilizationGauges.update(computedVals);
    }
    previousAggregateStats = currentStats;
    return;
}
catch (const std::exception& ex)
{
    Logger()->error("Exception updating utilization gauges: {}", ex.what());
    return;
}

void Proc::UpdateCoreUtilization(const std::vector<std::vector<std::string>>& cpuLines, const bool sixtySecondMetricsEnabled) try
{
    /*
    These metrics were previously recorded as a distribution summary, which behaved correctly
    when collected at 60-second intervals. However, when we introduced 5-second collection intervals,
    internal Netflix users were upset that the max usage values were no longer representative of the
    max core usage across the minute. The 5-second max values would capture brief CPU spikes that
    weren't indicative of sustained core utilization over a full minute.
    
    To address this issue, we now manually implement the internal representation of a distribution
    summary by updating its constituent counter fields directly. This approach allows us to compute
    the maximum average CPU usage across all cores over a 60-second window, rather than reporting
    the maximum usage over individual 5-second intervals.
    */
    
    static std::unordered_map<std::string, CpuStatFields> previousCpuStats;
    static std::unordered_map<std::string, double> previousCoreUsages;

    static auto counterCount = registry_->CreateCounter("sys.cpu.coreUtilization", {{"statistic", "count"}});
    static auto counterTotal = registry_->CreateCounter("sys.cpu.coreUtilization", {{"statistic", "totalAmount"}});
    static auto counterTotalSquares = registry_->CreateCounter("sys.cpu.coreUtilization", {{"statistic", "totalOfSquares"}});
    static auto gaugeMax = registry_->CreateMaxGauge("sys.cpu.coreUtilization", {{"statistic", "max"}});

    for (unsigned int i = ProcStatConstants::FirstProcessorIndex; i < cpuLines.size(); ++i)
    {
        const auto& fields = cpuLines[i];
        const auto& key = fields[0];
        CpuStatFields currentStats(fields);

        auto [it, inserted] = previousCpuStats.try_emplace(key, currentStats);
        if (inserted == false)
        {
            const auto& prevStats = it->second;
            auto computedVals = ComputeGaugeValues(prevStats, currentStats);
            auto usage = computedVals.user + computedVals.system + computedVals.stolen + computedVals.nice +
                         computedVals.wait + computedVals.interrupt + computedVals.guest;
            
            counterCount.Increment();
            counterTotal.Increment(usage);
            counterTotalSquares.Increment(usage * usage);
            previousCoreUsages[key] += usage;

            // Update the stored stats for next iteration
            it->second = currentStats;
        }
    }

    // If 60-second metrics are enabled, compute the max average usage across all cores over the minute
    // previousCoreUsages will never be empty this is just for the unit test
    if (sixtySecondMetricsEnabled && !previousCoreUsages.empty())
    {
        // Find the max usage in previousCoreUsages
        double maxUsage = 0.0;
        for (const auto& [key, usage] : previousCoreUsages)
        {
            maxUsage = std::max(maxUsage, usage);
        }

        // Divide the usage by 12 to get average over the minute
        double avgUsage = maxUsage / 12.0;

        // Set the gauge mean to the max average usage
        gaugeMax.Set(avgUsage);

        previousCoreUsages.clear();
    }

    return;
}
catch (const std::exception& ex)
{
    Logger()->error("Exception updating core utilization: {}", ex.what());
    return;
}

void Proc::UpdateNumProcs(const unsigned int numberProcessors)
{
    static auto num_procs = registry_->CreateGauge("sys.cpu.numProcessors");
    num_procs.Set(numberProcessors);
    return;
}

std::vector<std::vector<std::string>> Proc::ParseProcStatFile() try
{
    auto stat_data = read_lines_fields(this->path_prefix_, "stat");
    if (stat_data.empty()) return {};

    std::vector<std::vector<std::string>> cpu_lines;
    cpu_lines.reserve(stat_data.size());
    for (auto& fields : stat_data)
    {
        if (fields.empty()) continue;                          // skip blanks
        if (!starts_with(fields[0].c_str(), ProcStatConstants::CpuPrefix)) continue;  // non CPU line

        if (fields.size() != ProcStatConstants::ExpectedCpuFields)
        {
            Logger()->error("Malformed cpu line in /proc/stat: expected 11 fields, got {}: {}", fields.size(),
                            absl::StrJoin(fields, " "));
            return {};  // semantics: abort on first malformed line
        }
        cpu_lines.emplace_back(std::move(fields));
    }
    return cpu_lines;
}
catch (const std::exception& ex)
{
    Logger()->error("Exception reading /proc/stat: {}", ex.what());
    return {};
}

void Proc::CpuStats(const bool fiveSecondMetrics, const bool sixtySecondMetricsEnabled) noexcept
{
    auto cpuLines = ParseProcStatFile();
    if (cpuLines.empty())
    {
        return;
    }

    // If 60-second metrics are enabled, collect utilization metrics
    if (sixtySecondMetricsEnabled)
    {
        UpdateUtilizationGauges(cpuLines[0]);  // Pass the aggregate line (first line)
        UpdateNumProcs(cpuLines.size() - 1);   // Pass number of processors & subtract 1 for the aggregate "cpu" line
    }

    // If 5-second metrics are enabled, collect additional detailed metrics
    if (fiveSecondMetrics)
    {
        UpdateCoreUtilization(cpuLines, sixtySecondMetricsEnabled);
    }

    // Always collect peak stats (called every 1 second)
    PeakCpuStats(cpuLines[0]);
}

void Proc::memory_stats() noexcept
{
    static auto avail_real = registry_->CreateGauge("mem.availReal");
    static auto free_real = registry_->CreateGauge("mem.freeReal");
    static auto total_real = registry_->CreateGauge("mem.totalReal");
    static auto avail_swap = registry_->CreateGauge("mem.availSwap");
    static auto total_swap = registry_->CreateGauge("mem.totalSwap");
    static auto buffer = registry_->CreateGauge("mem.buffer");
    static auto cached = registry_->CreateGauge("mem.cached");
    static auto shared = registry_->CreateGauge("mem.shared");
    static auto total_free = registry_->CreateGauge("mem.totalFree");

    auto fp = open_file(path_prefix_, "meminfo");
    if (fp == nullptr)
    {
        return;
    }

    char line[1024];
    u_long total_free_bytes = 0;
    while (fgets(line, sizeof line, fp) != nullptr)
    {
        if (starts_with(line, "MemTotal:"))
        {
            u_long n;
            sscanf(line, "MemTotal: %lu", &n);
            total_real.Set(n * 1024.0);
        }
        else if (starts_with(line, "MemFree:"))
        {
            u_long n;
            sscanf(line, "MemFree: %lu", &n);
            free_real.Set(n * 1024.0);
            total_free_bytes += n;
        }
        else if (starts_with(line, "MemAvailable:"))
        {
            u_long n;
            sscanf(line, "MemAvailable: %lu", &n);
            avail_real.Set(n * 1024.0);
        }
        else if (starts_with(line, "SwapFree:"))
        {
            u_long n;
            sscanf(line, "SwapFree: %lu", &n);
            avail_swap.Set(n * 1024.0);
            total_free_bytes += n;
        }
        else if (starts_with(line, "SwapTotal:"))
        {
            u_long n;
            sscanf(line, "SwapTotal: %lu", &n);
            total_swap.Set(n * 1024.0);
        }
        else if (starts_with(line, "Buffers:"))
        {
            u_long n;
            sscanf(line, "Buffers: %lu", &n);
            buffer.Set(n * 1024.0);
        }
        else if (starts_with(line, "Cached:"))
        {
            u_long n;
            sscanf(line, "Cached: %lu", &n);
            cached.Set(n * 1024.0);
        }
        else if (starts_with(line, "Shmem:"))
        {
            u_long n;
            sscanf(line, "Shmem: %lu", &n);
            shared.Set(n * 1024.0);
        }
    }
    total_free.Set(total_free_bytes * 1024.0);
}

inline int64_t to_int64(const std::string& s)
{
    int64_t res;
    auto parsed = absl::SimpleAtoi(s, &res);
    return parsed ? res : 0;
}

void Proc::socket_stats() noexcept
{
    auto pagesize = static_cast<size_t>(sysconf(_SC_PAGESIZE));
    static auto tcp_memory = registry_->CreateGauge("net.tcp.memory");

    auto fp = open_file(path_prefix_, "net/sockstat");
    if (fp == nullptr)
    {
        return;
    }

    char line[1024];
    while (fgets(line, sizeof line, fp) != nullptr)
    {
        if (starts_with(line, "TCP:"))
        {
            std::vector<std::string> values = absl::StrSplit(line, absl::ByAnyChar(" \t\n"), absl::SkipEmpty());
            auto idx = 0u;
            for (const auto& value : values)
            {
                if (value == "mem")
                {
                    tcp_memory.Set(to_int64(values[idx + 1]) * pagesize);
                }
                ++idx;
            }
            break;
        }
    }
}

void Proc::netstat_stats() noexcept
{
    static auto capableTags = std::unordered_map<std::string, std::string>{{"id", "capable"}, {"proto", "v4"}};
    static auto notCapableTags = std::unordered_map<std::string, std::string>{{"id", "notCapable"}, {"proto", "v4"}};
    static auto protoTags = std::unordered_map<std::string, std::string>{{"proto", "v4"}};
    capableTags.insert(net_tags_.begin(), net_tags_.end());
    notCapableTags.insert(net_tags_.begin(), net_tags_.end());
    protoTags.insert(net_tags_.begin(), net_tags_.end());

    static auto ect_ctr = registry_->CreateMonotonicCounter("net.ip.ectPackets", capableTags);
    static auto noEct_ctr = registry_->CreateMonotonicCounter("net.ip.ectPackets", notCapableTags);
    static auto congested_ctr = registry_->CreateMonotonicCounter("net.ip.congestedPackets", protoTags);

    auto fp = open_file(path_prefix_, "net/netstat");
    if (fp == nullptr)
    {
        return;
    }

    int64_t noEct = 0, ect = 0, congested = 0;
    char line[1024];
    while (fgets(line, sizeof line, fp) != nullptr)
    {
        if (starts_with(line, "IpExt:"))
        {
            // get header indexes
            std::vector<std::string> headers = absl::StrSplit(line, absl::ByAnyChar(" \t\n"), absl::SkipEmpty());
            if (fgets(line, sizeof line, fp) == nullptr)
            {
                Logger()->warn("Unable to parse {}/net/netstat", path_prefix_);
                return;
            }
            std::vector<std::string> values = absl::StrSplit(line, absl::ByAnyChar(" \t\n"), absl::SkipEmpty());
            ;
            assert(values.size() == headers.size());
            auto idx = 0u;
            for (const auto& header : headers)
            {
                if (header == "InNoECTPkts")
                {
                    noEct = to_int64(values[idx]);
                }
                else if (header == "InECT1Pkts" || header == "InECT0Pkts")
                {
                    ect += to_int64(values[idx]);
                }
                else if (header == "InCEPkts")
                {
                    congested = to_int64(values[idx]);
                }
                ++idx;
            }
            break;
        }
    }

    // Set all the counters if we have data. We want to explicitly send a 0 value for congested to
    // distinguish known no congestion from no data
    if (ect > 0 || noEct > 0)
    {
        congested_ctr.Set(congested);
        ect_ctr.Set(ect);
        noEct_ctr.Set(noEct);
    }
}

void Proc::arp_stats() noexcept
{
    static auto arpcache_size = registry_->CreateGauge("net.arpCacheSize", net_tags_);
    auto fp = open_file(path_prefix_, "net/arp");
    if (fp == nullptr)
    {
        return;
    }

    // discard the header
    discard_line(fp);
    auto num_entries = 0;
    char line[1024];
    while (fgets(line, sizeof line, fp) != nullptr)
    {
        if (isdigit(line[0]))
        {
            num_entries++;
        }
    }
    arpcache_size.Set(num_entries);
}

static bool all_digits(const char* str)
{
    assert(*str != '\0');

    for (; *str != '\0'; ++str)
    {
        auto c = *str;
        if (!isdigit(c)) return false;
    }
    return true;
}

int32_t count_tasks(const std::string& dirname)
{
    DirHandle dh{dirname.c_str()};
    if (!dh)
    {
        return 0;
    }

    auto count = 0;
    for (;;)
    {
        auto entry = readdir(dh);
        if (entry == nullptr) break;

        if (all_digits(entry->d_name))
        {
            ++count;
        }
    }
    return count;
}

void Proc::process_stats() noexcept
{
    static auto cur_pids = registry_->CreateGauge("sys.currentProcesses");
    static auto cur_threads = registry_->CreateGauge("sys.currentThreads");

    DirHandle dir_handle{path_prefix_.c_str()};
    if (!dir_handle)
    {
        return;
    }

    auto pids = 0, tasks = 0;
    for (;;)
    {
        auto entry = readdir(dir_handle);
        if (entry == nullptr) break;
        if (all_digits(entry->d_name))
        {
            ++pids;
            auto task_dir = fmt::format("{}/{}/task", path_prefix_, entry->d_name);
            tasks += count_tasks(task_dir);
        }
    }
    cur_pids.Set(pids);
    cur_threads.Set(tasks);
}

}  // namespace atlasagent
