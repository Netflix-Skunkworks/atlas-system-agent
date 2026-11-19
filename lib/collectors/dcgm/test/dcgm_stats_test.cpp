#include "../src/dcgm_stats.cpp"
#include <lib/util/src/util.h>

#include <gtest/gtest.h>

TEST(DCGMTest, ParseLinesValidInput)
{
    std::string filePath{"testdata/resources2/dcgm/ValidInput1"};
    auto lines = atlasagent::read_file(filePath);
    std::map<int, std::vector<double>> dataMap;

    EXPECT_EQ(parse_lines(lines.value(), dataMap), true);
}

TEST(DCGMTest, ParseLinesInvalidInput1)
{
    std::string filePath{"testdata/resources2/dcgm/InvalidInput1"};
    auto lines = atlasagent::read_file(filePath);
    std::map<int, std::vector<double>> dataMap;

    EXPECT_EQ(parse_lines(lines.value(), dataMap), false);
}

TEST(DCGMTest, ParseLinesInvalidInput2)
{
    std::string filePath{"testdata/resources2/dcgm/InvalidInput2"};
    auto lines = atlasagent::read_file(filePath);
    std::map<int, std::vector<double>> dataMap;

    EXPECT_EQ(parse_lines(lines.value(), dataMap), false);
}

TEST(DCGMTest, ParseLinesEmptyLines)
{
    std::vector<std::string> lines{};
    std::map<int, std::vector<double>> dataMap;
    EXPECT_EQ(parse_lines(lines, dataMap), false);
}

// Reachability is a TCP connection to the host engine's port, so a plain listening socket stands in for
// nv-hostengine. Port 0 lets the kernel pick one, so the test cannot collide with a real host engine on 5555.
TEST(DCGMTest, HostEngineReachableOnlyWhileSomethingListens)
{
    using boost::asio::ip::tcp;
    boost::asio::io_context io;
    tcp::acceptor listener(io, tcp::endpoint(boost::asio::ip::address_v4::loopback(), 0));
    const int port = listener.local_endpoint().port();

    EXPECT_TRUE(is_hostengine_reachable("127.0.0.1", port));

    // Nothing listens on the port once the acceptor is closed, so the connection is refused
    listener.close();
    EXPECT_FALSE(is_hostengine_reachable("127.0.0.1", port));
}