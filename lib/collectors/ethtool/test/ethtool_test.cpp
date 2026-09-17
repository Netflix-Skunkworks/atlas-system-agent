#include <thirdparty/spectator-cpp/libs/utils/include/util.h>
#include <thirdparty/spectator-cpp/libs/writer/writer_wrapper/writer_test_helper.h>
#include <thirdparty/spectator-cpp/spectator/registry.h>
#include <lib/collectors/ethtool/src/ethtool.h>
#include <gtest/gtest.h>

#include <algorithm>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace
{

using atlasagent::Ethtool;

constexpr const char* kSysClassNet = "testdata/resources/sys/class/net";

// Tags reach the writer in unordered_map iteration order, which is a hash detail rather than
// anything the protocol fixes. ParseProtocolLine re-emits them sorted by key, so expectations can
// be written as literal lines. An unparseable message is returned as-is, to fail with a readable
// diff rather than throwing.
std::string normalized(const std::string& message)
{
    const auto parsed = ParseProtocolLine(message);
    return parsed.has_value() ? parsed->to_string() : message;
}

// Overrides the ethtool invocation so collect() can be driven end to end without the binary
// installed: every interface enumerated from the fixture is recorded, and canned output is
// returned in place of the real statistics.
class TestableEthtool : public Ethtool
{
   public:
    using Output = std::unordered_map<std::string, std::vector<std::string>>;

    explicit TestableEthtool(Registry* registry, std::unordered_map<std::string, std::string> net_tags = {},
                             std::string path_prefix = kSysClassNet, Output output = {})
        : Ethtool{registry, std::move(net_tags), std::move(path_prefix)}, output_{std::move(output)}
    {
    }

    void stats(const std::vector<std::string>& nic_stats, const std::string& iface) noexcept
    {
        Ethtool::ethtool_stats(nic_stats, iface);
    }

    std::vector<std::string> ifaces() noexcept { return Ethtool::enumerate_interfaces(); }

    const std::vector<std::string>& polled() const { return polled_; }

   protected:
    std::vector<std::string> run_ethtool(const std::string& iface) override
    {
        polled_.push_back(iface);
        const auto entry = output_.find(iface);
        return entry == output_.end() ? std::vector<std::string>{} : entry->second;
    }

   private:
    Output output_;
    std::vector<std::string> polled_;
};

class EthtoolTest : public testing::Test
{
   protected:
    void SetUp() override { writer()->Clear(); }

    static MemoryWriter* writer() { return static_cast<MemoryWriter*>(WriterTestHelper::GetImpl()); }

    Config config_{WriterConfig(WriterTypes::Memory)};
    Registry registry_{config_};
};

TEST_F(EthtoolTest, Stats)
{
    TestableEthtool ethtool{&registry_};
    const std::vector<std::string> first_sample = {"NIC statistics:",
                                                   "    suspend: 0",
                                                   "    resume: 0",
                                                   "    bw_in_allowance_exceeded: 0",
                                                   "    bw_out_allowance_exceeded: 0",
                                                   "    pps_allowance_exceeded: 0",
                                                   "    conntrack_allowance_exceeded: 0",
                                                   "    conntrack_allowance_available: 100",
                                                   "    linklocal_allowance_exceeded: 0",
                                                   "    queue_0_tx_cnt: 368940",
                                                   "    queue_0_tx_bytes: 126196057"};
    ethtool.stats(first_sample, "eth0");

    auto messages = writer()->GetMessages();

    ASSERT_EQ(messages.size(), 6);
    EXPECT_EQ(normalized(messages.at(0)), "C:net.perf.bwAllowanceExceeded,id=in,iface=eth0:0.000000\n");
    EXPECT_EQ(normalized(messages.at(1)), "C:net.perf.bwAllowanceExceeded,id=out,iface=eth0:0.000000\n");
    EXPECT_EQ(normalized(messages.at(2)), "C:net.perf.ppsAllowanceExceeded,iface=eth0:0.000000\n");
    EXPECT_EQ(normalized(messages.at(3)), "C:net.perf.conntrackAllowanceExceeded,iface=eth0:0.000000\n");
    EXPECT_EQ(normalized(messages.at(4)), "g:net.perf.conntrackAllowanceAvailable,iface=eth0:100.000000\n");
    EXPECT_EQ(normalized(messages.at(5)), "C:net.perf.linklocalAllowanceExceeded,iface=eth0:0.000000\n");

    writer()->Clear();

    const std::vector<std::string> second_sample = {"NIC statistics:",
                                                    "    suspend: 0",
                                                    "    resume: 0",
                                                    "    bw_in_allowance_exceeded: 5",
                                                    "    bw_out_allowance_exceeded: 10",
                                                    "    conntrack_allowance_exceeded: 15",
                                                    "    conntrack_allowance_available: 110",
                                                    "    linklocal_allowance_exceeded: 20",
                                                    "    pps_allowance_exceeded: 25",
                                                    "    queue_0_tx_cnt: 368940",
                                                    "    queue_0_tx_bytes: 126196057"};

    ethtool.stats(second_sample, "eth0");
    messages = writer()->GetMessages();

    ASSERT_EQ(messages.size(), 6);
    EXPECT_EQ(normalized(messages.at(0)), "C:net.perf.bwAllowanceExceeded,id=in,iface=eth0:5.000000\n");
    EXPECT_EQ(normalized(messages.at(1)), "C:net.perf.bwAllowanceExceeded,id=out,iface=eth0:10.000000\n");
    EXPECT_EQ(normalized(messages.at(2)), "C:net.perf.conntrackAllowanceExceeded,iface=eth0:15.000000\n");
    EXPECT_EQ(normalized(messages.at(3)), "g:net.perf.conntrackAllowanceAvailable,iface=eth0:110.000000\n");
    EXPECT_EQ(normalized(messages.at(4)), "C:net.perf.linklocalAllowanceExceeded,iface=eth0:20.000000\n");
    EXPECT_EQ(normalized(messages.at(5)), "C:net.perf.ppsAllowanceExceeded,iface=eth0:25.000000\n");
}

TEST_F(EthtoolTest, StatsEmpty)
{
    TestableEthtool ethtool{&registry_};

    ethtool.stats({}, "");

    EXPECT_TRUE(writer()->GetMessages().empty());
}

TEST_F(EthtoolTest, StatsIgnoresMalformedAndNonExactFields)
{
    TestableEthtool ethtool{&registry_};

    ethtool.stats({"bw_in_allowance_exceeded: not-a-number", "conntrack_allowance_available: ",
                   "queue_0_bw_in_allowance_exceeded: 7", "prefixpps_allowance_exceeded: 8"},
                  "eth0");

    EXPECT_TRUE(writer()->GetMessages().empty());
}

TEST_F(EthtoolTest, StatsPreservesNetworkTags)
{
    TestableEthtool ethtool{&registry_, {{"nf.app", "test-app"}, {"nf.region", "us-east-1"}}};

    ethtool.stats({"bw_in_allowance_exceeded: 3"}, "eth0");

    const auto messages = writer()->GetMessages();
    ASSERT_EQ(messages.size(), 1);
    EXPECT_EQ(normalized(messages.at(0)),
              "C:net.perf.bwAllowanceExceeded,id=in,iface=eth0,nf.app=test-app,nf.region=us-east-1:3.000000\n");
}

// The fixture models a k8s node: two hardware NICs (with a `device` directory) alongside the
// virtual interfaces that have none -- loopback, a docker bridge, and two pod veth peers whose
// names contain "eth" and are longer than the 15 characters ethtool's ioctl path accepts.
TEST_F(EthtoolTest, EnumerateInterfaces)
{
    TestableEthtool ethtool{&registry_};

    // Sorted here, not in the collector: directory order is arbitrary, and nothing downstream
    // cares which interface is polled first.
    auto interfaces = ethtool.ifaces();
    std::sort(interfaces.begin(), interfaces.end());

    const std::vector<std::string> expected{"eth0", "eth1"};
    EXPECT_EQ(interfaces, expected);
}

TEST_F(EthtoolTest, EnumerateInterfacesMissingDirectory)
{
    TestableEthtool ethtool{&registry_, {}, "testdata/resources/sys/class/does-not-exist"};

    EXPECT_TRUE(ethtool.ifaces().empty());
}

// Covers the wiring collect() owns and nothing else does: every enumerated interface is polled,
// and each one's output is tagged with the interface it came from rather than the last one seen.
TEST_F(EthtoolTest, CollectsEveryEnumeratedInterface)
{
    TestableEthtool ethtool{&registry_,
                            {},
                            kSysClassNet,
                            {{"eth0", {"bw_in_allowance_exceeded: 1"}}, {"eth1", {"pps_allowance_exceeded: 2"}}}};

    ethtool.collect();

    auto polled = ethtool.polled();
    std::sort(polled.begin(), polled.end());
    const std::vector<std::string> expected{"eth0", "eth1"};
    EXPECT_EQ(polled, expected);

    std::vector<std::string> messages;
    for (const auto& message : writer()->GetMessages())
    {
        messages.push_back(normalized(message));
    }
    std::sort(messages.begin(), messages.end());

    ASSERT_EQ(messages.size(), 2);
    EXPECT_EQ(messages.at(0), "C:net.perf.bwAllowanceExceeded,id=in,iface=eth0:1.000000\n");
    EXPECT_EQ(messages.at(1), "C:net.perf.ppsAllowanceExceeded,iface=eth1:2.000000\n");
}
}  // namespace
