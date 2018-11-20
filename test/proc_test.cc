#include "../lib/logger.h"
#include "../lib/proc.h"
#include "measurement_utils.h"
#include <fmt/ostream.h>
#include <gtest/gtest.h>

using namespace atlasagent;

using spectator::Config;
using spectator::Registry;
using Measurements = std::vector<spectator::Measurement>;

TEST(Proc, ParseNetwork) {
  Registry registry(Config{});
  Proc proc{&registry, "./resources/proc"};

  proc.network_stats();
  EXPECT_TRUE(registry.Measurements().empty());

  proc.set_prefix("./resources/proc2");
  proc.network_stats();

  const auto& ms = registry.Measurements();
  auto map = measurements_to_map(ms, "iface");
  expect_value(&map, "net.iface.bytes|in|eth1", 1e3);
  expect_value(&map, "net.iface.errors|in|eth1", 1);
  expect_value(&map, "net.iface.packets|in|eth1", 1e3);
  expect_value(&map, "net.iface.bytes|out|eth1", 1e6);
  expect_value(&map, "net.iface.errors|out|eth1", 2);
  expect_value(&map, "net.iface.packets|out|eth1", 1e4);
  expect_value(&map, "net.iface.droppedPackets|out|eth1", 1);
  expect_value(&map, "net.iface.bytes|in|lo", 1e5);
  expect_value(&map, "net.iface.packets|in|lo", 1e7);
  expect_value(&map, "net.iface.bytes|out|lo", 1e9);
  expect_value(&map, "net.iface.packets|out|lo", 1e6);
  expect_value(&map, "net.iface.packets|out|eth0", 1e6);
  expect_value(&map, "net.iface.bytes|out|eth0", 1e8);
  expect_value(&map, "net.iface.collisions|eth0", 1);
  expect_value(&map, "net.iface.bytes|in|eth0", 1e5);
  expect_value(&map, "net.iface.droppedPackets|out|eth0", 1);
  expect_value(&map, "net.iface.errors|out|eth0", 2);
  expect_value(&map, "net.iface.packets|in|eth0", 100);

  EXPECT_TRUE(map.empty());  // checked all values
}

TEST(Proc, ParseSnmp) {
  Registry registry(Config{});
  Proc proc{&registry, "./resources/proc"};

  proc.snmp_stats();
  // only gauges
  auto initial = registry.Measurements();
  for (const auto& m : initial) {
    EXPECT_EQ(m.id->GetTags().at("statistic"), "gauge");
  }
  proc.set_prefix("./resources/proc2");
  proc.snmp_stats();

  auto ms = registry.Measurements();
  measurement_map values = measurements_to_map(ms, "proto");
  expect_value(&values, "net.tcp.connectionStates|closeWait|v4", 0);
  expect_value(&values, "net.tcp.connectionStates|closeWait|v6", 0);
  expect_value(&values, "net.tcp.connectionStates|close|v4", 0);
  expect_value(&values, "net.tcp.connectionStates|close|v6", 0);
  expect_value(&values, "net.tcp.connectionStates|closing|v4", 0);
  expect_value(&values, "net.tcp.connectionStates|closing|v6", 0);
  expect_value(&values, "net.tcp.connectionStates|established|v4", 27);
  expect_value(&values, "net.tcp.connectionStates|established|v6", 0);
  expect_value(&values, "net.tcp.connectionStates|finWait1|v4", 0);
  expect_value(&values, "net.tcp.connectionStates|finWait1|v6", 0);
  expect_value(&values, "net.tcp.connectionStates|finWait2|v4", 0);
  expect_value(&values, "net.tcp.connectionStates|finWait2|v6", 0);
  expect_value(&values, "net.tcp.connectionStates|lastAck|v4", 0);
  expect_value(&values, "net.tcp.connectionStates|lastAck|v6", 0);
  expect_value(&values, "net.tcp.connectionStates|listen|v4", 10);
  expect_value(&values, "net.tcp.connectionStates|listen|v6", 5);
  expect_value(&values, "net.tcp.connectionStates|synRecv|v4", 0);
  expect_value(&values, "net.tcp.connectionStates|synRecv|v6", 0);
  expect_value(&values, "net.tcp.connectionStates|synSent|v4", 0);
  expect_value(&values, "net.tcp.connectionStates|synSent|v6", 0);
  expect_value(&values, "net.tcp.connectionStates|timeWait|v4", 1);
  expect_value(&values, "net.tcp.connectionStates|timeWait|v6", 0);
  expect_value(&values, "net.tcp.currEstab", 27);

  expect_value(&values, "net.ip.datagrams|out", 20);
  expect_value(&values, "net.ip.discards|out", 3);
  expect_value(&values, "net.ip.datagrams|in", 100);
  expect_value(&values, "net.ip.discards|in", 1);

  expect_value(&values, "net.tcp.errors|attemptFails", 1);
  expect_value(&values, "net.tcp.errors|estabResets", 10);
  expect_value(&values, "net.tcp.errors|inErrs", 9);
  expect_value(&values, "net.tcp.errors|outRsts", 2);
  expect_value(&values, "net.tcp.errors|retransSegs", 20);
  expect_value(&values, "net.tcp.opens|active", 100);
  expect_value(&values, "net.tcp.opens|passive", 30);
  expect_value(&values, "net.tcp.segments|in", 1e+06);
  expect_value(&values, "net.tcp.segments|out", 1.1e+06);

  expect_value(&values, "net.udp.datagrams|in", 10000);
  expect_value(&values, "net.udp.datagrams|out", 1000);
  expect_value(&values, "net.udp.errors|inErrors", 1);
  EXPECT_TRUE(values.empty());
}

TEST(Proc, ParseLoadAvg) {
  Registry registry(Config{});
  Proc proc{&registry, "./resources/proc"};
  proc.loadavg_stats();
  const auto& ms = registry.Measurements();
  EXPECT_EQ(3, ms.size()) << "3 metrics for loadavg";

  for (const auto& m : ms) {
    Logger()->info("Got {}", m);
  }
}

TEST(Proc, ParsePidFromSched) {
  const char* container = "init (95352, #threads: 1)";
  const char* host = "systemd (1, #threads: 1)";

  EXPECT_EQ(95352, proc::get_pid_from_sched(container));
  EXPECT_EQ(1, proc::get_pid_from_sched(host));
}

TEST(Proc, IsContainer) {
  Registry registry(Config{});
  Proc proc{&registry, "./resources/proc"};
  EXPECT_TRUE(proc.is_container());
  proc.set_prefix("./resources/proc-host");
  EXPECT_FALSE(proc.is_container());
}

bool is_gauge(const spectator::IdPtr& id) {
  const auto& tags = id->GetTags();
  const auto& stat = tags.at("statistic");
  return stat == "gauge";
}

TEST(Proc, CpuStats) {
  Registry registry(Config{});
  Proc proc{&registry, "./resources/proc"};
  proc.cpu_stats();
  proc.peak_cpu_stats();
  const auto& ms = registry.Measurements();
  EXPECT_TRUE(ms.empty());

  proc.set_prefix("./resources/proc2");
  proc.cpu_stats();
  proc.peak_cpu_stats();
  const auto& ms2 = registry.Measurements();
  EXPECT_EQ(16, ms2.size());
}

TEST(Proc, VmStats) {
  Registry registry(Config{});
  Proc proc{&registry, "./resources/proc"};
  proc.vmstats();
  auto ms = registry.Measurements();
  auto ms_map = measurements_to_map(ms, "proto");
  expect_value(&ms_map, "vmstat.procs|blocked", 1);
  expect_value(&ms_map, "vmstat.procs|running", 2);
  expect_value(&ms_map, "vmstat.fh.allocated", 2016);
  expect_value(&ms_map, "vmstat.fh.max", 12556616);
  EXPECT_TRUE(ms_map.empty());

  proc.set_prefix("./resources/proc2");
  proc.vmstats();
  auto ms2 = registry.Measurements();
  auto ms2_map = measurements_to_map(ms2, "proto");
  expect_value(&ms2_map, "vmstat.procs|blocked", 2);
  expect_value(&ms2_map, "vmstat.procs|running", 3);
  expect_value(&ms2_map, "vmstat.procs.count", 600);
  expect_value(&ms2_map, "vmstat.fh.allocated", 2017);
  expect_value(&ms2_map, "vmstat.fh.max", 12556616);
  expect_value(&ms2_map, "vmstat.paging|out", 256);
  EXPECT_TRUE(ms2_map.empty());
  // 0 values are not returned for counters
  //  "vmstat.swapping|in",  "vmstat.swapping|out", "vmstat.paging|in"
}

TEST(Proc, MemoryStats) {
  Registry registry(Config{});
  Proc proc{&registry, "./resources/proc"};
  proc.memory_stats();
  auto ms = registry.Measurements();
  auto ms_map = measurements_to_map(ms, "proto");
  expect_value(&ms_map, "mem.freeReal", 1024.0 * 9631224);
  expect_value(&ms_map, "mem.availReal", 1024.0 * 9557144);
  expect_value(&ms_map, "mem.totalReal", 1024.0 * 125898216);
  expect_value(&ms_map, "mem.totalSwap", 2 * 1024.0);
  expect_value(&ms_map, "mem.availSwap", 1 * 1024.0);
  expect_value(&ms_map, "mem.buffer", 97032 * 1024.0);
  expect_value(&ms_map, "mem.cached", 500404 * 1024.0);
  expect_value(&ms_map, "mem.shared", 34968 * 1024.0);
  expect_value(&ms_map, "mem.totalFree", 1024.0 * 9631225);
  EXPECT_TRUE(ms_map.empty());
}

TEST(Proc, ParseNetstat) {
  Registry registry(Config{});
  Proc proc{&registry, "./resources/proc"};
  proc.netstat_stats();
  EXPECT_TRUE(registry.Measurements().empty());

  proc.set_prefix("./resources/proc2");
  proc.netstat_stats();

  const auto& ms = registry.Measurements();
  measurement_map values = measurements_to_map(ms, "proto");
  expect_value(&values, "net.ip.ectPackets|capable", 180.0);
  expect_value(&values, "net.ip.ectPackets|notCapable", 60.0);
  expect_value(&values, "net.ip.congestedPackets", 30);
  EXPECT_TRUE(values.empty());
}
