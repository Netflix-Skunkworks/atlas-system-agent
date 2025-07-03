#include <lib/logger/src/logger.h>
#include <thirdparty/spectator-cpp/spectator/registry.h>
#include <thirdparty/spectator-cpp/libs/writer/writer_wrapper/writer_test_helper.h>
#include <lib/collectors/ethtool/src/ethtool.h>
#include <gtest/gtest.h>

namespace {

using atlasagent::Ethtool;
using atlasagent::Logger;

class EthtoolTest : public Ethtool {
 public:
  explicit EthtoolTest(Registry* registry) : Ethtool{registry} {}

  void stats(const std::vector<std::string>& nic_stats, const char* iface) noexcept {
    Ethtool::ethtool_stats(nic_stats, iface);
  }

  std::vector<std::string> ifaces(const std::vector<std::string>& ip_links) {
    return Ethtool::enumerate_interfaces(ip_links);
  }
};

TEST(Ethtool, Stats) {
  auto config = Config(WriterConfig(WriterTypes::Memory));
  auto r = Registry(config);
  EthtoolTest ethtool{&r};
  std::vector<std::string> first_sample = {"NIC statistics:\n",
                                           "    suspend: 0\n",
                                           "    resume: 0\n",
                                           "    bw_in_allowance_exceeded: 0\n",
                                           "    bw_out_allowance_exceeded: 0\n",
                                           "    pps_allowance_exceeded: 0\n",
                                           "    conntrack_allowance_exceeded: 0\n",
                                           "    conntrack_allowance_available: 100\n",
                                           "    linklocal_allowance_exceeded: 0\n",
                                           "    queue_0_tx_cnt: 368940\n",
                                           "    queue_0_tx_bytes: 126196057\n"};
  ethtool.stats(first_sample, "eth0");

  auto memoryWriter = static_cast<MemoryWriter*>(WriterTestHelper::GetImpl());
  auto messages = memoryWriter->GetMessages();

  EXPECT_EQ(messages.size(), 6);
  EXPECT_EQ(messages.at(0), "C:net.perf.bwAllowanceExceeded,id=in,iface=eth0:0.000000\n");
  EXPECT_EQ(messages.at(1), "C:net.perf.bwAllowanceExceeded,id=out,iface=eth0:0.000000\n");
  EXPECT_EQ(messages.at(2), "C:net.perf.ppsAllowanceExceeded,iface=eth0:0.000000\n");
  EXPECT_EQ(messages.at(3), "C:net.perf.conntrackAllowanceExceeded,iface=eth0:0.000000\n");
  EXPECT_EQ(messages.at(4), "g:net.perf.conntrackAllowanceAvailable,iface=eth0:100.000000\n");
  EXPECT_EQ(messages.at(5), "C:net.perf.linklocalAllowanceExceeded,iface=eth0:0.000000\n");

  memoryWriter->Clear();

  std::vector<std::string> second_sample = {"NIC statistics:\n",
                                            "    suspend: 0\n",
                                            "    resume: 0\n",
                                            "    bw_in_allowance_exceeded: 5\n",
                                            "    bw_out_allowance_exceeded: 10\n",
                                            "    conntrack_allowance_exceeded: 15\n",
                                            "    conntrack_allowance_available: 110\n",
                                            "    linklocal_allowance_exceeded: 20\n",
                                            "    pps_allowance_exceeded: 25\n",
                                            "    queue_0_tx_cnt: 368940\n",
                                            "    queue_0_tx_bytes: 126196057\n"};

  ethtool.stats(second_sample, "eth0");
  messages = memoryWriter->GetMessages();

  EXPECT_EQ(messages.size(), 6);
  EXPECT_EQ(messages.at(0), "C:net.perf.bwAllowanceExceeded,id=in,iface=eth0:5.000000\n");
  EXPECT_EQ(messages.at(1), "C:net.perf.bwAllowanceExceeded,id=out,iface=eth0:10.000000\n");
  EXPECT_EQ(messages.at(2), "C:net.perf.conntrackAllowanceExceeded,iface=eth0:15.000000\n");
  EXPECT_EQ(messages.at(3), "g:net.perf.conntrackAllowanceAvailable,iface=eth0:110.000000\n");
  EXPECT_EQ(messages.at(4), "C:net.perf.linklocalAllowanceExceeded,iface=eth0:20.000000\n");
  EXPECT_EQ(messages.at(5), "C:net.perf.ppsAllowanceExceeded,iface=eth0:25.000000\n");
}

TEST(Ethtool, StatsEmpty) {
  auto config = Config(WriterConfig(WriterTypes::Memory));
  auto r = Registry(config);
  EthtoolTest ethtool{&r};

  ethtool.stats({}, "");
  auto memoryWriter = static_cast<MemoryWriter*>(WriterTestHelper::GetImpl());
  auto messages = memoryWriter->GetMessages();
  EXPECT_EQ(messages.size(), 0);
}

TEST(Ethtool, EnumerateInterfaces) {
  auto config = Config(WriterConfig(WriterTypes::Memory));
  auto r = Registry(config);
  EthtoolTest ethtool{&r};
  std::vector<std::string> ip_links = {
      "1: lo: <LOOPBACK,UP,LOWER_UP> mtu 65536 qdisc noqueue state UNKNOWN mode DEFAULT group "
      "default qlen 1000\n",
      "   link/loopback 00:00:00:00:00:00 brd 00:00:00:00:00:00\n",
      "2: eth0: <BROADCAST,MULTICAST,UP,LOWER_UP> mtu 9001 qdisc mq state UP mode DEFAULT group "
      "default qlen 1000\n",
      "   link/ether 0a:69:fb:fa:96:77 brd ff:ff:ff:ff:ff:ff\n",
      "   altname enp0s5\n",
      "   altname ens5\n",
      "3: eth1: <BROADCAST,MULTICAST,UP,LOWER_UP> mtu 9001 qdisc mq state UP mode DEFAULT group "
      "default qlen 1000\n",
      "   link/ether 0a:69:fb:fa:96:78 brd ff:ff:ff:ff:ff:ff\n",
      "   altname enp0s6\n",
      "   altname ens6\n"};

  std::vector<std::string> expected{"eth0", "eth1"};
  EXPECT_EQ(ethtool.ifaces(ip_links), expected);
}
}  // namespace
