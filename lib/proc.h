#pragma once

#include "counters.h"
#include <atlas/atlas_client.h>
#include <atlas/meter/monotonic_counter.h>

namespace atlasagent {
class Proc {
 public:
  explicit Proc(atlas::meter::Registry* registry, std::string path_prefix = "/proc") noexcept;
  void network_stats() noexcept;
  void snmp_stats() noexcept;
  void loadavg_stats() noexcept;
  void cpu_stats() noexcept;
  void peak_cpu_stats() noexcept;
  void memory_stats() noexcept;
  void vmstats() noexcept;
  bool is_container() const noexcept;

  void set_prefix(const std::string& new_prefix) noexcept;  // for testing

 private:
  atlas::meter::Registry* registry_;
  std::string path_prefix_;
  Counters counters_;

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
