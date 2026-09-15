#include <lib/logger/src/logger.h>
#include <thirdparty/spectator-cpp/spectator/registry.h>
#include <thirdparty/spectator-cpp/libs/writer/writer_wrapper/writer_test_helper.h>
#include <lib/collectors/ethtool/src/ethtool.h>
#include <gtest/gtest.h>

#include <algorithm>

namespace
{

using atlasagent::Ethtool;
using atlasagent::Logger;

constexpr const char* kSysClassNet = "testdata/resources/sys/class/net";

class EthtoolTest : public Ethtool
{
   public:
    explicit EthtoolTest(Registry* registry, std::string path_prefix = kSysClassNet)
        : Ethtool{registry, {}, std::move(path_prefix)}
    {
    }

    void stats(const std::vector<std::string>& nic_stats, const char* iface) noexcept
    {
        Ethtool::ethtool_stats(nic_stats, iface);
    }

    std::vector<std::string> ifaces() noexcept { return Ethtool::enumerate_interfaces(); }
};

TEST(Ethtool, Stats)
{
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

TEST(Ethtool, StatsEmpty)
{
    auto config = Config(WriterConfig(WriterTypes::Memory));
    auto r = Registry(config);
    EthtoolTest ethtool{&r};

    ethtool.stats({}, "");
    auto memoryWriter = static_cast<MemoryWriter*>(WriterTestHelper::GetImpl());
    auto messages = memoryWriter->GetMessages();
    EXPECT_EQ(messages.size(), 0);
}

// The fixture models a k8s node: two hardware NICs (with a `device` directory) alongside the
// virtual interfaces that have none -- loopback, a docker bridge, and two pod veth peers whose
// names contain "eth" and are longer than the 15 characters ethtool's ioctl path accepts.
TEST(Ethtool, EnumerateInterfaces)
{
    auto config = Config(WriterConfig(WriterTypes::Memory));
    auto r = Registry(config);
    EthtoolTest ethtool{&r};

    // sorted here, not in the collector: directory order is arbitrary, and nothing downstream
    // cares which interface is polled first
    auto interfaces = ethtool.ifaces();
    std::sort(interfaces.begin(), interfaces.end());

    std::vector<std::string> expected{"eth0", "eth1"};
    EXPECT_EQ(interfaces, expected);
}

TEST(Ethtool, EnumerateInterfacesMissingDirectory)
{
    auto config = Config(WriterConfig(WriterTypes::Memory));
    auto r = Registry(config);
    EthtoolTest ethtool{&r, "testdata/resources/sys/class/does-not-exist"};

    EXPECT_TRUE(ethtool.ifaces().empty());
}
}  // namespace
