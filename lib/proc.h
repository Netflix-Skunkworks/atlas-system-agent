#pragma once

#include "tagging_registry.h"

namespace atlasagent {
template <typename Reg = TaggingRegistry>
class Proc {
 public:
  Proc(Reg* registry, spectator::Tags net_tags, std::string path_prefix = "/proc") noexcept
      : registry_(registry), net_tags_{std::move(net_tags)}, path_prefix_(std::move(path_prefix)) {}
  void network_stats() noexcept;
  void arp_stats() noexcept;
  void snmp_stats() noexcept;
  void netstat_stats() noexcept;
  void loadavg_stats() noexcept;
  void cpu_stats() noexcept;
  void peak_cpu_stats() noexcept;
  void memory_stats() noexcept;
  void process_stats() noexcept;
  void socket_stats() noexcept;
  void vmstats() noexcept;
  [[nodiscard]] bool is_container() const noexcept;

  void set_prefix(const std::string& new_prefix) noexcept;  // for testing

 private:
  Reg* registry_;
  const spectator::Tags net_tags_;
  std::string path_prefix_;

  void handle_line(FILE* fp) noexcept;
  void parse_ip_stats(const char* buf) noexcept;
  void parse_tcp_stats(const char* buf) noexcept;
  void parse_udp_stats(const char* buf) noexcept;
  void parse_ipv6_stats(const std::unordered_map<std::string, int64_t>& snmp_stats) noexcept;
  void parse_udpv6_stats(const std::unordered_map<std::string, int64_t>& snmp_stats) noexcept;
  void parse_load_avg(const char* buf) noexcept;
  void parse_tcp_connections() noexcept;
};

namespace proc {
int get_pid_from_sched(const char* sched_line) noexcept;
}  // namespace proc

}  // namespace atlasagent

#include "internal/proc.inc"
