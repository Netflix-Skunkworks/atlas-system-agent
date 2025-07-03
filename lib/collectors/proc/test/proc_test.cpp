#include <lib/logger/src/logger.h>
#include <thirdparty/spectator-cpp/spectator/registry.h>
#include <thirdparty/spectator-cpp/libs/writer/writer_wrapper/writer_test_helper.h>
#include <lib/collectors/proc/src/proc.h>

#include <fmt/ostream.h>
#include <gtest/gtest.h>

namespace {

using atlasagent::Logger;

TEST(Proc, ParseNetwork) {
  // Registry registry;
  // spectator::Tags extra{{"nf.test", "extra"}};
  // Proc proc{&registry, extra, "testdata/resources/proc"};

  // proc.network_stats();
  // EXPECT_TRUE(my_measurements(&registry).empty());

  // proc.set_prefix("testdata/resources/proc2");
  // proc.network_stats();

  // const auto& ms = my_measurements(&registry);
  // auto map = measurements_to_map(ms, "iface");
  // expect_value(&map, "net.iface.bytes|count|in|eth1|extra", 1e3);
  // expect_value(&map, "net.iface.errors|count|in|eth1|extra", 1);
  // expect_value(&map, "net.iface.packets|count|in|eth1|extra", 1e3);
  // expect_value(&map, "net.iface.bytes|count|out|eth1|extra", 1e6);
  // expect_value(&map, "net.iface.errors|count|out|eth1|extra", 2);
  // expect_value(&map, "net.iface.packets|count|out|eth1|extra", 1e4);
  // expect_value(&map, "net.iface.droppedPackets|count|out|eth1|extra", 1);
  // expect_value(&map, "net.iface.bytes|count|in|lo|extra", 1e5);
  // expect_value(&map, "net.iface.packets|count|in|lo|extra", 1e7);
  // expect_value(&map, "net.iface.bytes|count|out|lo|extra", 1e9);
  // expect_value(&map, "net.iface.packets|count|out|lo|extra", 1e6);
  // expect_value(&map, "net.iface.packets|count|out|eth0|extra", 1e6);
  // expect_value(&map, "net.iface.bytes|count|out|eth0|extra", 1e8);
  // expect_value(&map, "net.iface.collisions|count|eth0|extra", 1);
  // expect_value(&map, "net.iface.bytes|count|in|eth0|extra", 1e5);
  // expect_value(&map, "net.iface.droppedPackets|count|out|eth0|extra", 1);
  // expect_value(&map, "net.iface.errors|count|out|eth0|extra", 2);
  // expect_value(&map, "net.iface.packets|count|in|eth0|extra", 100);

  // EXPECT_TRUE(map.empty());  // checked all values
}

TEST(Proc, ParseSnmp) {
  // Registry registry;
  // spectator::Tags extra{{"nf.test", "extra"}};
  // Proc proc{&registry, extra, "testdata/resources/proc"};

  // proc.snmp_stats();
  // // only gauges
  // auto initial = my_measurements(&registry);
  // for (const auto& m : initial) {
  //   EXPECT_EQ(m.id->GetTags().at("statistic"), "gauge");
  // }
  // proc.set_prefix("testdata/resources/proc2");
  // proc.snmp_stats();

  // auto ms = my_measurements(&registry);
  // measurement_map values = measurements_to_map(ms, "proto");

  // expect_value(&values, "net.tcp.connectionStates|gauge|closeWait|v4|extra", 0);
  // expect_value(&values, "net.tcp.connectionStates|gauge|closeWait|v6|extra", 0);
  // expect_value(&values, "net.tcp.connectionStates|gauge|close|v4|extra", 0);
  // expect_value(&values, "net.tcp.connectionStates|gauge|close|v6|extra", 0);
  // expect_value(&values, "net.tcp.connectionStates|gauge|closing|v4|extra", 0);
  // expect_value(&values, "net.tcp.connectionStates|gauge|closing|v6|extra", 0);
  // expect_value(&values, "net.tcp.connectionStates|gauge|established|v4|extra", 27);
  // expect_value(&values, "net.tcp.connectionStates|gauge|established|v6|extra", 0);
  // expect_value(&values, "net.tcp.connectionStates|gauge|finWait1|v4|extra", 0);
  // expect_value(&values, "net.tcp.connectionStates|gauge|finWait1|v6|extra", 0);
  // expect_value(&values, "net.tcp.connectionStates|gauge|finWait2|v4|extra", 0);
  // expect_value(&values, "net.tcp.connectionStates|gauge|finWait2|v6|extra", 0);
  // expect_value(&values, "net.tcp.connectionStates|gauge|lastAck|v4|extra", 0);
  // expect_value(&values, "net.tcp.connectionStates|gauge|lastAck|v6|extra", 0);
  // expect_value(&values, "net.tcp.connectionStates|gauge|listen|v4|extra", 10);
  // expect_value(&values, "net.tcp.connectionStates|gauge|listen|v6|extra", 5);
  // expect_value(&values, "net.tcp.connectionStates|gauge|synRecv|v4|extra", 0);
  // expect_value(&values, "net.tcp.connectionStates|gauge|synRecv|v6|extra", 0);
  // expect_value(&values, "net.tcp.connectionStates|gauge|synSent|v4|extra", 0);
  // expect_value(&values, "net.tcp.connectionStates|gauge|synSent|v6|extra", 0);
  // expect_value(&values, "net.tcp.connectionStates|gauge|timeWait|v4|extra", 1);
  // expect_value(&values, "net.tcp.connectionStates|gauge|timeWait|v6|extra", 0);
  // expect_value(&values, "net.tcp.currEstab|gauge|extra", 27);

  // expect_value(&values, "net.ip.datagrams|count|out|v4|extra", 20);
  // expect_value(&values, "net.ip.discards|count|out|v4|extra", 3);
  // expect_value(&values, "net.ip.datagrams|count|in|v4|extra", 100);
  // expect_value(&values, "net.ip.discards|count|in|v4|extra", 1);

  // expect_value(&values, "net.tcp.errors|count|attemptFails|extra", 1);
  // expect_value(&values, "net.tcp.errors|count|estabResets|extra", 10);
  // expect_value(&values, "net.tcp.errors|count|inErrs|extra", 9);
  // expect_value(&values, "net.tcp.errors|count|outRsts|extra", 2);
  // expect_value(&values, "net.tcp.errors|count|retransSegs|extra", 20);
  // expect_value(&values, "net.tcp.opens|count|active|extra", 100);
  // expect_value(&values, "net.tcp.opens|count|passive|extra", 30);
  // expect_value(&values, "net.tcp.segments|count|in|extra", 1e+06);
  // expect_value(&values, "net.tcp.segments|count|out|extra", 1.1e+06);

  // expect_value(&values, "net.udp.datagrams|count|in|v4|extra", 10000);
  // expect_value(&values, "net.udp.datagrams|count|out|v4|extra", 1000);
  // expect_value(&values, "net.udp.errors|count|inErrors|v4|extra", 1);

  // expect_value(&values, "net.ip.discards|count|in|v6|extra", 1.0);
  // expect_value(&values, "net.ip.discards|count|out|v6|extra", 2.0);
  // expect_value(&values, "net.ip.datagrams|count|in|v6|extra", 100.0);
  // expect_value(&values, "net.ip.datagrams|count|out|v6|extra", 1000.0);
  // expect_value(&values, "net.ip.reasmReqds|count|v6|extra", 42.0);
  // expect_value(&values, "net.ip.congestedPackets|count|v6|extra", 2.0);
  // expect_value(&values, "net.ip.ectPackets|count|capable|v6|extra", 42.0);
  // expect_value(&values, "net.ip.ectPackets|count|notCapable|v6|extra", 10.0);

  // expect_value(&values, "net.udp.datagrams|count|in|v6|extra", 10.0);
  // expect_value(&values, "net.udp.datagrams|count|out|v6|extra", 10.0);
  // expect_value(&values, "net.udp.errors|count|inErrors|v6|extra", 1.0);
  // for (const auto& m : values) {
  //   Logger()->info("Unexpected {} = {}", m.first, m.second);
  // }
  // EXPECT_TRUE(values.empty());
}

TEST(Proc, ParseLoadAvg) {
  // Registry registry;
  // spectator::Tags extra{{"nf.test", "extra"}};
  // Proc proc{&registry, extra, "testdata/resources/proc"};
  // proc.loadavg_stats();
  // const auto& ms = my_measurements(&registry);
  // EXPECT_EQ(3, ms.size()) << "3 metrics for loadavg";

  // for (const auto& m : ms) {
  //   Logger()->info("Got {}", m);
  // }
  auto config = Config(WriterConfig(WriterTypes::Memory));
  auto r = Registry(config);
  atlasagent::Proc proc{&r, {{"nf.test", "extra"}}, "testdata/resources/proc"};
  proc.loadavg_stats();

  auto memoryWriter = static_cast<MemoryWriter*>(WriterTestHelper::GetImpl());
  auto messages = memoryWriter->GetMessages();
  EXPECT_EQ(3, messages.size());
    for (const auto& m : messages) {
      Logger()->info("Got {}", m);
    }
}

TEST(Proc, ParsePidFromSched) {
  using atlasagent::proc::get_pid_from_sched;
  const char* container = "init (95352, #threads: 1)";
  const char* host = "systemd (1, #threads: 1)";

  EXPECT_EQ(95352, get_pid_from_sched(container));
  EXPECT_EQ(1, get_pid_from_sched(host));
}

TEST(Proc, IsContainer) {
  auto config = Config(WriterConfig(WriterTypes::Memory));
  auto r = Registry(config);
  atlasagent::Proc proc{&r, {{"nf.test", "extra"}}, "testdata/resources/proc"};

  EXPECT_TRUE(proc.is_container());
  proc.set_prefix("testdata/resources/proc-host");
  EXPECT_FALSE(proc.is_container());
}

TEST(Proc, CpuStats) {
  // Registry registry;
  // spectator::Tags extra{{"nf.test", "extra"}};
  // Proc proc{&registry, extra, "testdata/resources/proc"};
  // proc.cpu_stats();
  // proc.peak_cpu_stats();
  // const auto& ms = my_measurements(&registry);
  // EXPECT_EQ(1, ms.size());

  // proc.set_prefix("testdata/resources/proc2");
  // proc.cpu_stats();
  // proc.peak_cpu_stats();
  // const auto& ms2 = my_measurements(&registry);
  // EXPECT_EQ(17, ms2.size());
}

TEST(Proc, UptimeStats) {
  auto config = Config(WriterConfig(WriterTypes::Memory));
  auto r = Registry(config);
  atlasagent::Proc proc{&r, {{"nf.test", "extra"}}, "testdata/resources/proc"};
  proc.uptime_stats();

  auto memoryWriter = static_cast<MemoryWriter*>(WriterTestHelper::GetImpl());
  auto messages = memoryWriter->GetMessages();

  EXPECT_EQ(messages.size(), 1);
  EXPECT_EQ(messages.at(0), "g:sys.uptime:517407.000000\n");
}

TEST(Proc, VmStats) {
  // Registry registry;
  // spectator::Tags extra{{"nf.test", "extra"}};
  // Proc proc{&registry, extra, "testdata/resources/proc"};
  // proc.vmstats();
  // auto ms = my_measurements(&registry);
  // auto ms_map = measurements_to_map(ms, "proto");
  // expect_value(&ms_map, "vmstat.procs|gauge|blocked", 1);
  // expect_value(&ms_map, "vmstat.procs|gauge|running", 2);
  // expect_value(&ms_map, "vmstat.fh.allocated|gauge", 2016);
  // expect_value(&ms_map, "vmstat.fh.max|gauge", 12556616);
  // EXPECT_TRUE(ms_map.empty());

  // proc.set_prefix("testdata/resources/proc2");
  // proc.vmstats();
  // auto ms2 = my_measurements(&registry);
  // auto ms2_map = measurements_to_map(ms2, "proto");
  // expect_value(&ms2_map, "vmstat.procs|gauge|blocked", 2);
  // expect_value(&ms2_map, "vmstat.procs|gauge|running", 3);
  // expect_value(&ms2_map, "vmstat.procs.count|count", 600);
  // expect_value(&ms2_map, "vmstat.fh.allocated|gauge", 2017);
  // expect_value(&ms2_map, "vmstat.fh.max|gauge", 12556616);
  // expect_value(&ms2_map, "vmstat.paging|count|out", 256);
  // EXPECT_TRUE(ms2_map.empty());
  // 0 values are not returned for counters
  //  "vmstat.swapping|in",  "vmstat.swapping|out", "vmstat.paging|in"
}

TEST(Proc, MemoryStats) {
  auto config = Config(WriterConfig(WriterTypes::Memory));
  auto r = Registry(config);
  atlasagent::Proc proc{&r, {{"nf.test", "extra"}}, "testdata/resources/proc"};
  proc.memory_stats();

  auto memoryWriter = static_cast<MemoryWriter*>(WriterTestHelper::GetImpl());
  auto messages = memoryWriter->GetMessages();

  EXPECT_EQ(messages.size(), 9);
  EXPECT_EQ(messages.at(0), "g:mem.totalReal:128919773184.000000\n");
  EXPECT_EQ(messages.at(1), "g:mem.freeReal:9862373376.000000\n");
  EXPECT_EQ(messages.at(2), "g:mem.availReal:9786515456.000000\n");
  EXPECT_EQ(messages.at(3), "g:mem.buffer:99360768.000000\n");
  EXPECT_EQ(messages.at(4), "g:mem.cached:512413696.000000\n");
  EXPECT_EQ(messages.at(5), "g:mem.totalSwap:2048.000000\n");
  EXPECT_EQ(messages.at(6), "g:mem.availSwap:1024.000000\n");
  EXPECT_EQ(messages.at(7), "g:mem.shared:35807232.000000\n");
  EXPECT_EQ(messages.at(8), "g:mem.totalFree:9862374400.000000\n");
}

TEST(Proc, ParseNetstat) {
  // Registry registry;
  // spectator::Tags extra{{"nf.test", "extra"}};
  // Proc proc{&registry, extra, "testdata/resources/proc"};
  // proc.netstat_stats();
  // EXPECT_TRUE(my_measurements(&registry).empty());

  // proc.set_prefix("testdata/resources/proc2");
  // proc.netstat_stats();

  // const auto& ms = my_measurements(&registry);
  // measurement_map values = measurements_to_map(ms, "proto");
  // expect_value(&values, "net.ip.ectPackets|count|capable|v4|extra", 180.0);
  // expect_value(&values, "net.ip.ectPackets|count|notCapable|v4|extra", 60.0);
  // expect_value(&values, "net.ip.congestedPackets|count|v4|extra", 30);
  // EXPECT_TRUE(values.empty());
}

// TODO: Check this test
TEST(Proc, ParseSocketStats) {
  // auto config = Config(WriterConfig(WriterTypes::Memory));
  // auto r = Registry(config);
  // atlasagent::Proc proc{&r, {{"nf.test", "extra"}}, "testdata/resources/proc"};
  // proc.socket_stats();

  // auto memoryWriter = static_cast<MemoryWriter*>(WriterTestHelper::GetImpl());
  // auto messages = memoryWriter->GetMessages();

  // auto pagesize = static_cast<size_t>(sysconf(_SC_PAGESIZE));
  // auto expected = "g:net.tcp.memory:" + std::to_string(4519.0 * pagesize) + "\n";
  // EXPECT_EQ(messages.size(), 1);
  // EXPECT_EQ(messages.at(0), expected);
}

TEST(Proc, ArpStats) {
  auto config = Config(WriterConfig(WriterTypes::Memory));
  auto r = Registry(config);

  atlasagent::Proc proc{&r, {{"nf.test", "extra"}}, "testdata/resources/proc"};
  proc.arp_stats();

  auto memoryWriter = static_cast<MemoryWriter*>(WriterTestHelper::GetImpl());
  auto messages = memoryWriter->GetMessages();

  EXPECT_EQ(messages.size(), 1);
  EXPECT_EQ(messages.at(0), "g:net.arpCacheSize,nf.test=extra:6.000000\n");
}

TEST(Proc, ProcessStats) {
  auto config = Config(WriterConfig(WriterTypes::Memory));
  auto r = Registry(config);

  atlasagent::Proc proc{&r, {{"nf.test", "extra"}}, "testdata/resources/proc"};
  proc.process_stats();

  auto memoryWriter = static_cast<MemoryWriter*>(WriterTestHelper::GetImpl());
  auto messages = memoryWriter->GetMessages();

  EXPECT_EQ(messages.size(), 2);
  EXPECT_EQ(messages.at(0), "g:sys.currentProcesses:2.000000\n");
  EXPECT_EQ(messages.at(1), "g:sys.currentThreads:5.000000\n");
}

}  // namespace
