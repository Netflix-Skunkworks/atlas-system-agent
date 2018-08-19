#include "../lib/logger.h"
#include "../lib/proc.h"
#include "test_registry.h"
#include "measurement_utils.h"
#include <gtest/gtest.h>

using namespace atlasagent;

using atlas::meter::ManualClock;
using atlas::meter::Measurements;

static atlas::util::StrRef proto_ref() {
  static auto ref = atlas::util::intern_str("proto");
  return ref;
}

// TODO: verify values
TEST(Proc, ParseNetwork) {
  ManualClock clock;
  TestRegistry registry(&clock);
  registry.SetWall(1000);
  Proc proc{&registry, "./resources/proc"};

  proc.network_stats();

  registry.SetWall(61000);
  proc.network_stats();

  const auto& ms = registry.my_measurements();
  EXPECT_EQ(3 * 9, ms.size()) << "3 interfaces x 9 net metrics per iface generated";

  for (const auto& m : ms) {
    Logger()->info("Got {}", m);
  }
}

TEST(Proc, ParseSnmp) {
  ManualClock clock;
  TestRegistry registry(&clock);
  registry.SetWall(1000);
  Proc proc{&registry, "./resources/proc"};

  proc.snmp_stats();

  registry.SetWall(61000);
  proc.snmp_stats();

  const auto& ms = registry.my_measurements();
  EXPECT_EQ(22 + 5 + 10 + 3, ms.size())
      << "5 metrics for ip, 10 for tcp, 3 for udp + 22 for netstat.tcp";

  measurement_map values = measurements_to_map(ms, proto_ref());
  expect_value(values, "net.tcp.connectionStates|established|v4", 27.0);
  expect_value(values, "net.tcp.connectionStates|established|v6", 0.0);
  expect_value(values, "net.tcp.connectionStates|listen|v4", 10.0);
  expect_value(values, "net.tcp.connectionStates|listen|v6", 5.0);
  expect_value(values, "net.tcp.connectionStates|timeWait|v4", 1.0);
  expect_value(values, "net.tcp.connectionStates|timeWait|v6", 0.0);
}

TEST(Proc, ParseLoadAvg) {
  ManualClock clock;
  TestRegistry registry(&clock);
  registry.SetWall(1000);
  Proc proc{&registry, "./resources/proc"};
  proc.loadavg_stats();
  const auto& ms = registry.my_measurements();
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
  ManualClock clock;
  TestRegistry registry(&clock);
  Proc proc{&registry, "./resources/proc"};
  EXPECT_TRUE(proc.is_container());
  proc.set_prefix("./resources/proc-host");
  EXPECT_FALSE(proc.is_container());
}

bool is_gauge(const atlas::meter::IdPtr& id) {
  using atlas::util::intern_str;
  const auto& tags = id->GetTags();
  const auto dsType = intern_str("atlas.dstype");
  const auto gauge = intern_str("gauge");

  const auto& it = tags.at(dsType);
  return it == gauge;
}

TEST(Proc, CpuStats) {
  ManualClock clock;
  TestRegistry registry(&clock);
  Proc proc{&registry, "./resources/proc"};
  proc.cpu_stats();
  proc.peak_cpu_stats();
  registry.SetWall(60000);
  const auto& ms = registry.my_measurements();
  EXPECT_EQ(16, ms.size());

  for (const auto& m : ms) {
    if (is_gauge(m.id)) {
      EXPECT_TRUE(std::isnan(m.value)) << m;
    } else {
      EXPECT_DOUBLE_EQ(m.value, 0.0) << m;
    }
  }

  proc.set_prefix("./resources/proc2");
  proc.cpu_stats();
  proc.peak_cpu_stats();
  registry.SetWall(120000);
  const auto& ms2 = registry.my_measurements();
  EXPECT_EQ(16, ms2.size());
  for (const auto& m : ms2) {
    EXPECT_FALSE(std::isnan(m.value)) << m;
  }
}

TEST(Proc, VmStats) {
  ManualClock clock;
  TestRegistry registry(&clock);
  Proc proc{&registry, "./resources/proc"};
  proc.vmstats();
  registry.SetWall(60000);
  const auto& ms = registry.my_measurements();
  const auto ms_map = measurements_to_map(ms, proto_ref());
  EXPECT_EQ(4, ms.size());
  expect_value(ms_map, "vmstat.procs|blocked", 1);
  expect_value(ms_map, "vmstat.procs|running", 2);

  expect_value(ms_map, "vmstat.fh.allocated", 2016);
  expect_value(ms_map, "vmstat.fh.max", 12556616);

  proc.set_prefix("./resources/proc2");
  proc.vmstats();
  registry.SetWall(120000);
  const auto& ms2 = registry.my_measurements();
  const auto ms2_map = measurements_to_map(ms2, proto_ref());
  EXPECT_EQ(9, ms2.size());
  expect_value(ms2_map, "vmstat.procs|blocked", 2);
  expect_value(ms2_map, "vmstat.procs|running", 3);
  expect_value(ms2_map, "vmstat.procs.count", 10);
  expect_value(ms2_map, "vmstat.fh.allocated", 2017);
  expect_value(ms2_map, "vmstat.fh.max", 12556616);
  expect_value(ms2_map, "vmstat.swapping|in", 0);
  expect_value(ms2_map, "vmstat.swapping|out", 0);
  expect_value(ms2_map, "vmstat.paging|in", 0);
  expect_value(ms2_map, "vmstat.paging|out", 4.2666666666666667);
}

TEST(Proc, MemoryStats) {
  ManualClock clock;
  TestRegistry registry(&clock);
  Proc proc{&registry, "./resources/proc"};
  proc.memory_stats();
  const auto& ms = registry.my_measurements();
  const auto ms_map = measurements_to_map(ms, proto_ref());
  EXPECT_EQ(9, ms.size());
  expect_value(ms_map, "mem.freeReal", 1024.0 * 9631224);
  expect_value(ms_map, "mem.availReal", 1024.0 * 9557144);
  expect_value(ms_map, "mem.totalReal", 1024.0 * 125898216);
  expect_value(ms_map, "mem.totalSwap", 2 * 1024.0);
  expect_value(ms_map, "mem.availSwap", 1 * 1024.0);
  expect_value(ms_map, "mem.buffer", 97032 * 1024.0);
  expect_value(ms_map, "mem.cached", 500404 * 1024.0);
  expect_value(ms_map, "mem.shared", 34968 * 1024.0);
  expect_value(ms_map, "mem.totalFree", 1024.0 * 9631225);
}

TEST(Proc, ParseNetstat) {
  ManualClock clock;
  TestRegistry registry(&clock);
  registry.SetWall(1000);
  Proc proc{&registry, "./resources/proc"};
  proc.netstat_stats();

  registry.SetWall(61000);
  proc.set_prefix("./resources/proc2");
  proc.netstat_stats();

  registry.SetWall(121000);

  const auto& ms = registry.my_measurements();
  EXPECT_EQ(3, ms.size()) << "3 metrics for ipext";

  measurement_map values = measurements_to_map(ms, proto_ref());
  expect_value(values, "net.ipext.ectPackets|capable", 3.0);
  expect_value(values, "net.ipext.ectPackets|notCapable", 1.0);
  expect_value(values, "net.ipext.congestedPackets", 0.5);
}
