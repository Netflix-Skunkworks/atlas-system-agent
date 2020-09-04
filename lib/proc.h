#pragma once

#include <spectator/registry.h>

namespace atlasagent {
class Proc {
 public:
  Proc(spectator::Registry* registry, spectator::Tags net_tags,
       std::string path_prefix = "/proc") noexcept;
  void network_stats() noexcept;
  void arp_stats() noexcept;
  void snmp_stats() noexcept;
  void netstat_stats() noexcept;
  void loadavg_stats() noexcept;
  void cpu_stats() noexcept;
  void peak_cpu_stats() noexcept;
  void memory_stats() noexcept;
  void process_stats() noexcept;
  void vmstats() noexcept;
  bool is_container() const noexcept;

  void set_prefix(const std::string& new_prefix) noexcept;  // for testing

 private:
  spectator::Registry* registry_;
  const spectator::Tags net_tags_;
  std::string path_prefix_;

  void handle_line(FILE* fp) noexcept;
  void parse_ip_stats(const char* buf) noexcept;
  void parse_tcp_stats(const char* buf) noexcept;
  void parse_udp_stats(const char* buf) noexcept;
  void parse_load_avg(const char* buf) noexcept;
  void parse_tcp_connections() noexcept;
};

namespace proc {
int get_pid_from_sched(const char* sched_line) noexcept;
}  // namespace proc

}  // namespace atlasagent
