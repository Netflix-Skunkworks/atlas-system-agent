#include "logger.h"
#include "measurement_utils.h"
#include "ethtool.h"
#include <gtest/gtest.h>

namespace {

using atlasagent::Logger;
using atlasagent::Ethtool;
using Registry = spectator::TestRegistry;
using spectator::Tags;

class EthtoolTest : public Ethtool<Registry> {
 public:
  explicit EthtoolTest(Registry* registry) : Ethtool{registry, Tags{}} {}

  void stats(const std::vector<std::string>& nic_stats, const char* iface) noexcept {
    Ethtool::ethtool_stats(nic_stats, iface);
  }

  std::vector<std::string> ifaces(const std::vector<std::string>& ip_links) {
    return Ethtool::enumerate_interfaces(ip_links);
  }
};

TEST(Ethtool, Stats) {
  Registry registry;
  EthtoolTest ethtool{&registry};

  std::vector<std::string> first_sample = {
      "NIC statistics:\n",
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
  auto ms = registry.Measurements();
  // one gauge, the rest mono counters
  EXPECT_EQ(ms.size(), 1);

  std::vector<std::string> second_sample = {
      "NIC statistics:\n",
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

  // we need two samples, because these are all monotonic counters
  ethtool.stats(second_sample, "eth0");
  ms = registry.Measurements();
  EXPECT_EQ(ms.size(), 6);

  auto map = measurements_to_map(ms, "");
  std::unordered_map<std::string, double> expected = {
      {"net.perf.bwAllowanceExceeded|count|in", 5},
      {"net.perf.bwAllowanceExceeded|count|out", 10},
      {"net.perf.conntrackAllowanceExceeded|count", 15},
      {"net.perf.conntrackAllowanceAvailable|gauge", 110},
      {"net.perf.linklocalAllowanceExceeded|count", 20},
      {"net.perf.ppsAllowanceExceeded|count", 25}};
  EXPECT_EQ(map, expected);
}

TEST(Ethtool, StatsEmpty) {
  Registry registry;
  EthtoolTest ethtool{&registry};

  ethtool.stats({}, "");
  auto ms = registry.Measurements();
  EXPECT_EQ(ms.size(), 0);

  ethtool.stats({}, "");
  ms = registry.Measurements();
  EXPECT_EQ(ms.size(), 0);
}

TEST(Ethtool, EnumerateInterfaces) {
  Registry registry;
  EthtoolTest ethtool{&registry};

  std::vector<std::string> ip_links = {
      "1: lo: <LOOPBACK,UP,LOWER_UP> mtu 65536 qdisc noqueue state UNKNOWN mode DEFAULT group default qlen 1000\n",
      "   link/loopback 00:00:00:00:00:00 brd 00:00:00:00:00:00\n",
      "2: eth0: <BROADCAST,MULTICAST,UP,LOWER_UP> mtu 9001 qdisc mq state UP mode DEFAULT group default qlen 1000\n",
      "   link/ether 0a:69:fb:fa:96:77 brd ff:ff:ff:ff:ff:ff\n",
      "   altname enp0s5\n",
      "   altname ens5\n",
      "3: eth1: <BROADCAST,MULTICAST,UP,LOWER_UP> mtu 9001 qdisc mq state UP mode DEFAULT group default qlen 1000\n",
      "   link/ether 0a:69:fb:fa:96:78 brd ff:ff:ff:ff:ff:ff\n",
      "   altname enp0s6\n",
      "   altname ens6\n"};

  std::vector<std::string> expected{"eth0", "eth1"};
  EXPECT_EQ(ethtool.ifaces(ip_links), expected);
}
}  // namespace
