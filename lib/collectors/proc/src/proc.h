#pragma once

#include <thirdparty/spectator-cpp/spectator/registry.h>

namespace atlasagent
{

class Proc
{
   public:
    Proc(Registry* registry, std::unordered_map<std::string, std::string> net_tags,
         std::string path_prefix = "/proc") noexcept
        : registry_(registry), net_tags_{std::move(net_tags)}, path_prefix_(std::move(path_prefix))
    {
    }
    // 60-second "slow" proc metrics for each agent flavor. These are the production entry points;
    // the individual per-metric collectors below are internal.
    void CollectSystem() noexcept;
    void CollectTitus() noexcept;
    void CollectK8s() noexcept;

    // Peak CPU metrics run every second (1s/5s/60s cadence), separate from the slow set above.
    void CpuStats(const bool fiveSecondMetrics, const bool sixtySecondMetricsEnabled) noexcept;
    void set_prefix(const std::string& new_prefix) noexcept;  // for testing

   protected:
    // Not part of the public API — production calls these only via CollectSystem/CollectTitus. They are
    // protected (rather than private) so the unit tests can exercise each in isolation through a TestProc
    // subclass, matching the convention used by Disk (see disk.h).
    void network_stats() noexcept;
    void arp_stats() noexcept;
    void snmp_stats() noexcept;
    void netstat_stats() noexcept;
    void loadavg_stats() noexcept;
    void memory_stats() noexcept;
    void process_stats() noexcept;
    void socket_stats() noexcept;
    void uptime_stats() noexcept;
    void vmstats() noexcept;
    [[nodiscard]] bool is_container() const noexcept;
    std::vector<std::vector<std::string>> ParseProcStatFile();

   private:
    void PeakCpuStats(const std::vector<std::string>& aggregateLine);
    void UpdateUtilizationGauges(const std::vector<std::string>& aggregateLine);
    void UpdateCoreUtilization(const std::vector<std::vector<std::string>>& cpu_lines, const bool sixtySecondMetricsEnabled);
    void UpdateNumProcs(const unsigned int numberProcessors);

    void handle_line(FILE* fp) noexcept;
    void parse_ip_stats(const char* buf) noexcept;
    void parse_tcp_stats(const char* buf) noexcept;
    void parse_udp_stats(const char* buf) noexcept;
    void parse_ipv6_stats(const std::unordered_map<std::string, int64_t>& snmp_stats) noexcept;
    void parse_udpv6_stats(const std::unordered_map<std::string, int64_t>& snmp_stats) noexcept;
    void parse_load_avg(const char* buf) noexcept;
    void parse_tcp_connections() noexcept;

    Registry* registry_;
    const std::unordered_map<std::string, std::string> net_tags_;
    std::string path_prefix_;
};

namespace proc
{
int get_pid_from_sched(const char* sched_line) noexcept;
}  // namespace proc

}  // namespace atlasagent
