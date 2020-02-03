#include "../lib/logger.h"
#include "../lib/proc.h"
#include "measurement_utils.h"
#include <fmt/ostream.h>
#include <gtest/gtest.h>

using namespace atlasagent;

using spectator::GetConfiguration;
using spectator::Registry;
using Measurements = std::vector<spectator::Measurement>;

TEST(Proc, ParseNetwork) {
  Registry registry(GetConfiguration(), Logger());
  spectator::Tags extra{{"nf.test", "extra"}};
  Proc proc{&registry, extra, "./resources/proc"};

  proc.network_stats();
  EXPECT_TRUE(registry.Measurements().empty());

  proc.set_prefix("./resources/proc2");
  proc.network_stats();

  const auto& ms = registry.Measurements();
  auto map = measurements_to_map(ms, "iface");
  expect_value(&map, "net.iface.bytes|count|in|eth1|extra", 1e3);
  expect_value(&map, "net.iface.errors|count|in|eth1|extra", 1);
  expect_value(&map, "net.iface.packets|count|in|eth1|extra", 1e3);
  expect_value(&map, "net.iface.bytes|count|out|eth1|extra", 1e6);
  expect_value(&map, "net.iface.errors|count|out|eth1|extra", 2);
  expect_value(&map, "net.iface.packets|count|out|eth1|extra", 1e4);
  expect_value(&map, "net.iface.droppedPackets|count|out|eth1|extra", 1);
  expect_value(&map, "net.iface.bytes|count|in|lo|extra", 1e5);
  expect_value(&map, "net.iface.packets|count|in|lo|extra", 1e7);
  expect_value(&map, "net.iface.bytes|count|out|lo|extra", 1e9);
  expect_value(&map, "net.iface.packets|count|out|lo|extra", 1e6);
  expect_value(&map, "net.iface.packets|count|out|eth0|extra", 1e6);
  expect_value(&map, "net.iface.bytes|count|out|eth0|extra", 1e8);
  expect_value(&map, "net.iface.collisions|count|eth0|extra", 1);
  expect_value(&map, "net.iface.bytes|count|in|eth0|extra", 1e5);
  expect_value(&map, "net.iface.droppedPackets|count|out|eth0|extra", 1);
  expect_value(&map, "net.iface.errors|count|out|eth0|extra", 2);
  expect_value(&map, "net.iface.packets|count|in|eth0|extra", 100);

  EXPECT_TRUE(map.empty());  // checked all values
}

TEST(Proc, ParseSnmp) {
  Registry registry(GetConfiguration(), Logger());
  spectator::Tags extra{{"nf.test", "extra"}};
  Proc proc{&registry, extra, "./resources/proc"};

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
  expect_value(&values, "net.tcp.connectionStates|gauge|closeWait|v4|extra", 0);
  expect_value(&values, "net.tcp.connectionStates|gauge|closeWait|v6|extra", 0);
  expect_value(&values, "net.tcp.connectionStates|gauge|close|v4|extra", 0);
  expect_value(&values, "net.tcp.connectionStates|gauge|close|v6|extra", 0);
  expect_value(&values, "net.tcp.connectionStates|gauge|closing|v4|extra", 0);
  expect_value(&values, "net.tcp.connectionStates|gauge|closing|v6|extra", 0);
  expect_value(&values, "net.tcp.connectionStates|gauge|established|v4|extra", 27);
  expect_value(&values, "net.tcp.connectionStates|gauge|established|v6|extra", 0);
  expect_value(&values, "net.tcp.connectionStates|gauge|finWait1|v4|extra", 0);
  expect_value(&values, "net.tcp.connectionStates|gauge|finWait1|v6|extra", 0);
  expect_value(&values, "net.tcp.connectionStates|gauge|finWait2|v4|extra", 0);
  expect_value(&values, "net.tcp.connectionStates|gauge|finWait2|v6|extra", 0);
  expect_value(&values, "net.tcp.connectionStates|gauge|lastAck|v4|extra", 0);
  expect_value(&values, "net.tcp.connectionStates|gauge|lastAck|v6|extra", 0);
  expect_value(&values, "net.tcp.connectionStates|gauge|listen|v4|extra", 10);
  expect_value(&values, "net.tcp.connectionStates|gauge|listen|v6|extra", 5);
  expect_value(&values, "net.tcp.connectionStates|gauge|synRecv|v4|extra", 0);
  expect_value(&values, "net.tcp.connectionStates|gauge|synRecv|v6|extra", 0);
  expect_value(&values, "net.tcp.connectionStates|gauge|synSent|v4|extra", 0);
  expect_value(&values, "net.tcp.connectionStates|gauge|synSent|v6|extra", 0);
  expect_value(&values, "net.tcp.connectionStates|gauge|timeWait|v4|extra", 1);
  expect_value(&values, "net.tcp.connectionStates|gauge|timeWait|v6|extra", 0);
  expect_value(&values, "net.tcp.currEstab|gauge|extra", 27);

  expect_value(&values, "net.ip.datagrams|count|out|extra", 20);
  expect_value(&values, "net.ip.discards|count|out|extra", 3);
  expect_value(&values, "net.ip.datagrams|count|in|extra", 100);
  expect_value(&values, "net.ip.discards|count|in|extra", 1);

  expect_value(&values, "net.tcp.errors|count|attemptFails|extra", 1);
  expect_value(&values, "net.tcp.errors|count|estabResets|extra", 10);
  expect_value(&values, "net.tcp.errors|count|inErrs|extra", 9);
  expect_value(&values, "net.tcp.errors|count|outRsts|extra", 2);
  expect_value(&values, "net.tcp.errors|count|retransSegs|extra", 20);
  expect_value(&values, "net.tcp.opens|count|active|extra", 100);
  expect_value(&values, "net.tcp.opens|count|passive|extra", 30);
  expect_value(&values, "net.tcp.segments|count|in|extra", 1e+06);
  expect_value(&values, "net.tcp.segments|count|out|extra", 1.1e+06);

  expect_value(&values, "net.udp.datagrams|count|in|extra", 10000);
  expect_value(&values, "net.udp.datagrams|count|out|extra", 1000);
  expect_value(&values, "net.udp.errors|count|inErrors|extra", 1);
  EXPECT_TRUE(values.empty());
}

TEST(Proc, ParseLoadAvg) {
  Registry registry(GetConfiguration(), Logger());
  spectator::Tags extra{{"nf.test", "extra"}};
  Proc proc{&registry, extra, "./resources/proc"};
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
  Registry registry(GetConfiguration(), Logger());
  spectator::Tags extra{{"nf.test", "extra"}};
  Proc proc{&registry, extra, "./resources/proc"};

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
  Registry registry(GetConfiguration(), Logger());
  spectator::Tags extra{{"nf.test", "extra"}};
  Proc proc{&registry, extra, "./resources/proc"};
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
  Registry registry(GetConfiguration(), Logger());
  spectator::Tags extra{{"nf.test", "extra"}};
  Proc proc{&registry, extra, "./resources/proc"};
  proc.vmstats();
  auto ms = registry.Measurements();
  auto ms_map = measurements_to_map(ms, "proto");
  expect_value(&ms_map, "vmstat.procs|gauge|blocked", 1);
  expect_value(&ms_map, "vmstat.procs|gauge|running", 2);
  expect_value(&ms_map, "vmstat.fh.allocated|gauge", 2016);
  expect_value(&ms_map, "vmstat.fh.max|gauge", 12556616);
  EXPECT_TRUE(ms_map.empty());

  proc.set_prefix("./resources/proc2");
  proc.vmstats();
  auto ms2 = registry.Measurements();
  auto ms2_map = measurements_to_map(ms2, "proto");
  expect_value(&ms2_map, "vmstat.procs|gauge|blocked", 2);
  expect_value(&ms2_map, "vmstat.procs|gauge|running", 3);
  expect_value(&ms2_map, "vmstat.procs.count|count", 600);
  expect_value(&ms2_map, "vmstat.fh.allocated|gauge", 2017);
  expect_value(&ms2_map, "vmstat.fh.max|gauge", 12556616);
  expect_value(&ms2_map, "vmstat.paging|count|out", 256);
  EXPECT_TRUE(ms2_map.empty());
  // 0 values are not returned for counters
  //  "vmstat.swapping|in",  "vmstat.swapping|out", "vmstat.paging|in"
}

TEST(Proc, MemoryStats) {
  Registry registry(GetConfiguration(), Logger());
  spectator::Tags extra{{"nf.test", "extra"}};
  Proc proc{&registry, extra, "./resources/proc"};
  proc.memory_stats();
  auto ms = registry.Measurements();
  auto ms_map = measurements_to_map(ms, "proto");
  expect_value(&ms_map, "mem.freeReal|gauge", 1024.0 * 9631224);
  expect_value(&ms_map, "mem.availReal|gauge", 1024.0 * 9557144);
  expect_value(&ms_map, "mem.totalReal|gauge", 1024.0 * 125898216);
  expect_value(&ms_map, "mem.totalSwap|gauge", 2 * 1024.0);
  expect_value(&ms_map, "mem.availSwap|gauge", 1 * 1024.0);
  expect_value(&ms_map, "mem.buffer|gauge", 97032 * 1024.0);
  expect_value(&ms_map, "mem.cached|gauge", 500404 * 1024.0);
  expect_value(&ms_map, "mem.shared|gauge", 34968 * 1024.0);
  expect_value(&ms_map, "mem.totalFree|gauge", 1024.0 * 9631225);
  EXPECT_TRUE(ms_map.empty());
}

TEST(Proc, ParseNetstat) {
  Registry registry(GetConfiguration(), Logger());
  spectator::Tags extra{{"nf.test", "extra"}};
  Proc proc{&registry, extra, "./resources/proc"};
  proc.netstat_stats();
  EXPECT_TRUE(registry.Measurements().empty());

  proc.set_prefix("./resources/proc2");
  proc.netstat_stats();

  const auto& ms = registry.Measurements();
  measurement_map values = measurements_to_map(ms, "proto");
  expect_value(&values, "net.ip.ectPackets|count|capable|extra", 180.0);
  expect_value(&values, "net.ip.ectPackets|count|notCapable|extra", 60.0);
  expect_value(&values, "net.ip.congestedPackets|count|extra", 30);
  EXPECT_TRUE(values.empty());
}

TEST(Proc, ArpStats) {
  Registry registry(GetConfiguration(), Logger());
  spectator::Tags extra{{"nf.test", "extra"}};
  Proc proc{&registry, extra, "./resources/proc"};
  proc.arp_stats();
  const auto ms = registry.Measurements();
  EXPECT_EQ(ms.size(), 1);
  const auto& m = ms.front();
  EXPECT_EQ(m.id->Name(), "net.arpCacheSize");
  EXPECT_EQ(m.id->GetTags().at("nf.test"), "extra");
  EXPECT_DOUBLE_EQ(m.value, 6);
}

TEST(Proc, ProcessStats) {
  Registry registry(GetConfiguration(), Logger());
  spectator::Tags extra{{"nf.test", "extra"}};
  Proc proc{&registry, extra, "./resources/proc"};
  proc.process_stats();

  const auto& ms = registry.Measurements();
  auto map = measurements_to_map(ms, "");
  expect_value(&map, "sys.currentProcesses|gauge", 2.0);
  expect_value(&map, "sys.currentThreads|gauge", 1.0 + 4.0);
}
