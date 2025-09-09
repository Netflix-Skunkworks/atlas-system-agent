#include <lib/logger/src/logger.h>
#include <thirdparty/spectator-cpp/spectator/registry.h>
#include <thirdparty/spectator-cpp/libs/writer/writer_wrapper/writer_test_helper.h>
#include <lib/collectors/proc/src/proc.h>

#include <fmt/ostream.h>
#include <gtest/gtest.h>
#include <iostream>
#include <iomanip>
#include <algorithm>


namespace
{

using atlasagent::Logger;
/*
TEST(Proc, ParseNetwork)
{
    auto config = Config(WriterConfig(WriterTypes::Memory));
    auto r = Registry(config);
    std::unordered_map<std::string, std::string> extra_tags{{"nf.test", "extra"}};
    atlasagent::Proc proc{&r, extra_tags, "testdata/resources/proc"};

    auto memoryWriter = static_cast<MemoryWriter*>(WriterTestHelper::GetImpl());
    proc.network_stats();
    auto messages = memoryWriter->GetMessages();

    EXPECT_EQ(messages.size(), 27);
    EXPECT_EQ(messages.at(0), "C:net.iface.bytes,id=in,iface=eth1,nf.test=extra:3241362.000000\n");
    EXPECT_EQ(messages.at(1), "C:net.iface.packets,id=in,iface=eth1,nf.test=extra:51892.000000\n");
    EXPECT_EQ(messages.at(2), "C:net.iface.errors,id=in,iface=eth1,nf.test=extra:0.000000\n");
    EXPECT_EQ(messages.at(3), "C:net.iface.droppedPackets,id=in,iface=eth1,nf.test=extra:0.000000\n");
    EXPECT_EQ(messages.at(4), "C:net.iface.collisions,iface=eth1,nf.test=extra:0.000000\n");
    EXPECT_EQ(messages.at(5), "C:net.iface.bytes,id=out,iface=eth1,nf.test=extra:1634822.000000\n");
    EXPECT_EQ(messages.at(6), "C:net.iface.packets,id=out,iface=eth1,nf.test=extra:35418.000000\n");
    EXPECT_EQ(messages.at(7), "C:net.iface.errors,id=out,iface=eth1,nf.test=extra:0.000000\n");
    EXPECT_EQ(messages.at(8), "C:net.iface.droppedPackets,id=out,iface=eth1,nf.test=extra:0.000000\n");

    EXPECT_EQ(messages.at(9), "C:net.iface.bytes,id=in,iface=lo,nf.test=extra:141078250858.000000\n");
    EXPECT_EQ(messages.at(10), "C:net.iface.packets,id=in,iface=lo,nf.test=extra:43006074.000000\n");
    EXPECT_EQ(messages.at(11), "C:net.iface.errors,id=in,iface=lo,nf.test=extra:0.000000\n");
    EXPECT_EQ(messages.at(12), "C:net.iface.droppedPackets,id=in,iface=lo,nf.test=extra:0.000000\n");
    EXPECT_EQ(messages.at(13), "C:net.iface.collisions,iface=lo,nf.test=extra:0.000000\n");
    EXPECT_EQ(messages.at(14), "C:net.iface.bytes,id=out,iface=lo,nf.test=extra:141078250858.000000\n");
    EXPECT_EQ(messages.at(15), "C:net.iface.packets,id=out,iface=lo,nf.test=extra:43006074.000000\n");
    EXPECT_EQ(messages.at(16), "C:net.iface.errors,id=out,iface=lo,nf.test=extra:0.000000\n");
    EXPECT_EQ(messages.at(17), "C:net.iface.droppedPackets,id=out,iface=lo,nf.test=extra:0.000000\n");

    EXPECT_EQ(messages.at(18), "C:net.iface.bytes,id=in,iface=eth0,nf.test=extra:3437349965.000000\n");
    EXPECT_EQ(messages.at(19), "C:net.iface.packets,id=in,iface=eth0,nf.test=extra:7809881.000000\n");
    EXPECT_EQ(messages.at(20), "C:net.iface.errors,id=in,iface=eth0,nf.test=extra:0.000000\n");
    EXPECT_EQ(messages.at(21), "C:net.iface.droppedPackets,id=in,iface=eth0,nf.test=extra:0.000000\n");
    EXPECT_EQ(messages.at(22), "C:net.iface.collisions,iface=eth0,nf.test=extra:0.000000\n");
    EXPECT_EQ(messages.at(23), "C:net.iface.bytes,id=out,iface=eth0,nf.test=extra:9368243963.000000\n");
    EXPECT_EQ(messages.at(24), "C:net.iface.packets,id=out,iface=eth0,nf.test=extra:12878559.000000\n");
    EXPECT_EQ(messages.at(25), "C:net.iface.errors,id=out,iface=eth0,nf.test=extra:0.000000\n");
    EXPECT_EQ(messages.at(26), "C:net.iface.droppedPackets,id=out,iface=eth0,nf.test=extra:0.000000\n");

    memoryWriter->Clear();
    proc.set_prefix("testdata/resources/proc2");
    proc.network_stats();

    messages = memoryWriter->GetMessages();
    EXPECT_EQ(messages.size(), 27);
    EXPECT_EQ(messages.at(0), "C:net.iface.bytes,id=in,iface=eth1,nf.test=extra:3242362.000000\n");
    EXPECT_EQ(messages.at(1), "C:net.iface.packets,id=in,iface=eth1,nf.test=extra:52892.000000\n");
    EXPECT_EQ(messages.at(2), "C:net.iface.errors,id=in,iface=eth1,nf.test=extra:1.000000\n");
    EXPECT_EQ(messages.at(3), "C:net.iface.droppedPackets,id=in,iface=eth1,nf.test=extra:0.000000\n");
    EXPECT_EQ(messages.at(4), "C:net.iface.collisions,iface=eth1,nf.test=extra:0.000000\n");
    EXPECT_EQ(messages.at(5), "C:net.iface.bytes,id=out,iface=eth1,nf.test=extra:2634822.000000\n");
    EXPECT_EQ(messages.at(6), "C:net.iface.packets,id=out,iface=eth1,nf.test=extra:45418.000000\n");
    EXPECT_EQ(messages.at(7), "C:net.iface.errors,id=out,iface=eth1,nf.test=extra:2.000000\n");
    EXPECT_EQ(messages.at(8), "C:net.iface.droppedPackets,id=out,iface=eth1,nf.test=extra:1.000000\n");

    EXPECT_EQ(messages.at(9), "C:net.iface.bytes,id=in,iface=lo,nf.test=extra:141078350858.000000\n");
    EXPECT_EQ(messages.at(10), "C:net.iface.packets,id=in,iface=lo,nf.test=extra:53006074.000000\n");
    EXPECT_EQ(messages.at(11), "C:net.iface.errors,id=in,iface=lo,nf.test=extra:0.000000\n");
    EXPECT_EQ(messages.at(12), "C:net.iface.droppedPackets,id=in,iface=lo,nf.test=extra:0.000000\n");
    EXPECT_EQ(messages.at(13), "C:net.iface.collisions,iface=lo,nf.test=extra:0.000000\n");
    EXPECT_EQ(messages.at(14), "C:net.iface.bytes,id=out,iface=lo,nf.test=extra:142078250858.000000\n");
    EXPECT_EQ(messages.at(15), "C:net.iface.packets,id=out,iface=lo,nf.test=extra:44006074.000000\n");
    EXPECT_EQ(messages.at(16), "C:net.iface.errors,id=out,iface=lo,nf.test=extra:0.000000\n");
    EXPECT_EQ(messages.at(17), "C:net.iface.droppedPackets,id=out,iface=lo,nf.test=extra:0.000000\n");

    EXPECT_EQ(messages.at(18), "C:net.iface.bytes,id=in,iface=eth0,nf.test=extra:3437449965.000000\n");
    EXPECT_EQ(messages.at(19), "C:net.iface.packets,id=in,iface=eth0,nf.test=extra:7809981.000000\n");
    EXPECT_EQ(messages.at(20), "C:net.iface.errors,id=in,iface=eth0,nf.test=extra:0.000000\n");
    EXPECT_EQ(messages.at(21), "C:net.iface.droppedPackets,id=in,iface=eth0,nf.test=extra:0.000000\n");
    EXPECT_EQ(messages.at(22), "C:net.iface.collisions,iface=eth0,nf.test=extra:1.000000\n");
    EXPECT_EQ(messages.at(23), "C:net.iface.bytes,id=out,iface=eth0,nf.test=extra:9468243963.000000\n");
    EXPECT_EQ(messages.at(24), "C:net.iface.packets,id=out,iface=eth0,nf.test=extra:13878559.000000\n");
    EXPECT_EQ(messages.at(25), "C:net.iface.errors,id=out,iface=eth0,nf.test=extra:2.000000\n");
    EXPECT_EQ(messages.at(26), "C:net.iface.droppedPackets,id=out,iface=eth0,nf.test=extra:1.000000\n");
}

TEST(Proc, ParseSnmp)
{
    auto config = Config(WriterConfig(WriterTypes::Memory));
    auto r = Registry(config);
    std::unordered_map<std::string, std::string> extra_tags{{"nf.test", "extra"}};
    atlasagent::Proc proc{&r, extra_tags, "testdata/resources/proc"};

    proc.snmp_stats();
    auto memoryWriter = static_cast<MemoryWriter*>(WriterTestHelper::GetImpl());
    auto messages = memoryWriter->GetMessages();

    EXPECT_EQ(messages.size(), 51);
    EXPECT_EQ(messages.at(0), "C:net.ip.datagrams,nf.test=extra,proto=v4,id=in:7000374.000000\n");
    EXPECT_EQ(messages.at(1), "C:net.ip.discards,nf.test=extra,proto=v4,id=in:0.000000\n");
    EXPECT_EQ(messages.at(2), "C:net.ip.datagrams,nf.test=extra,proto=v4,id=out:7231213.000000\n");
    EXPECT_EQ(messages.at(3), "C:net.ip.discards,nf.test=extra,proto=v4,id=out:0.000000\n");
    EXPECT_EQ(messages.at(4), "C:net.ip.reasmReqds,nf.test=extra,proto=v4:0.000000\n");
    EXPECT_EQ(messages.at(5), "C:net.tcp.segments,nf.test=extra,id=in:6864744.000000\n");
    EXPECT_EQ(messages.at(6), "C:net.tcp.segments,nf.test=extra,id=out:7180910.000000\n");
    EXPECT_EQ(messages.at(7), "C:net.tcp.errors,nf.test=extra,id=retransSegs:1147.000000\n");
    EXPECT_EQ(messages.at(8), "C:net.tcp.opens,nf.test=extra,id=active:272201.000000\n");
    EXPECT_EQ(messages.at(9), "C:net.tcp.opens,nf.test=extra,id=passive:265950.000000\n");
    EXPECT_EQ(messages.at(10), "C:net.tcp.errors,nf.test=extra,id=attemptFails:9.000000\n");
    EXPECT_EQ(messages.at(11), "C:net.tcp.errors,nf.test=extra,id=estabResets:3239.000000\n");
    EXPECT_EQ(messages.at(12), "g:net.tcp.currEstab,nf.test=extra:27.000000\n");
    EXPECT_EQ(messages.at(13), "C:net.tcp.errors,nf.test=extra,id=inErrs:1.000000\n");
    EXPECT_EQ(messages.at(14), "C:net.tcp.errors,nf.test=extra,id=outRsts:84368.000000\n");
    EXPECT_EQ(messages.at(15), "C:net.udp.datagrams,nf.test=extra,proto=v4,id=in:135618.000000\n");
    EXPECT_EQ(messages.at(16), "C:net.udp.errors,nf.test=extra,proto=v4,id=inErrors:0.000000\n");
    EXPECT_EQ(messages.at(17), "C:net.udp.datagrams,nf.test=extra,proto=v4,id=out:135742.000000\n");
    EXPECT_EQ(messages.at(18), "g:net.tcp.connectionStates,nf.test=extra,proto=v4,id=established:27.000000\n");
    EXPECT_EQ(messages.at(19), "g:net.tcp.connectionStates,nf.test=extra,proto=v4,id=synSent:0.000000\n");
    EXPECT_EQ(messages.at(20), "g:net.tcp.connectionStates,nf.test=extra,proto=v4,id=synRecv:0.000000\n");
    EXPECT_EQ(messages.at(21), "g:net.tcp.connectionStates,nf.test=extra,proto=v4,id=finWait1:0.000000\n");
    EXPECT_EQ(messages.at(22), "g:net.tcp.connectionStates,nf.test=extra,proto=v4,id=finWait2:0.000000\n");
    EXPECT_EQ(messages.at(23), "g:net.tcp.connectionStates,nf.test=extra,proto=v4,id=timeWait:1.000000\n");
    EXPECT_EQ(messages.at(24), "g:net.tcp.connectionStates,nf.test=extra,proto=v4,id=close:0.000000\n");
    EXPECT_EQ(messages.at(25), "g:net.tcp.connectionStates,nf.test=extra,proto=v4,id=closeWait:0.000000\n");
    EXPECT_EQ(messages.at(26), "g:net.tcp.connectionStates,nf.test=extra,proto=v4,id=lastAck:0.000000\n");
    EXPECT_EQ(messages.at(27), "g:net.tcp.connectionStates,nf.test=extra,proto=v4,id=listen:10.000000\n");
    EXPECT_EQ(messages.at(28), "g:net.tcp.connectionStates,nf.test=extra,proto=v4,id=closing:0.000000\n");
    EXPECT_EQ(messages.at(29), "g:net.tcp.connectionStates,nf.test=extra,proto=v6,id=established:0.000000\n");
    EXPECT_EQ(messages.at(30), "g:net.tcp.connectionStates,nf.test=extra,proto=v6,id=synSent:0.000000\n");
    EXPECT_EQ(messages.at(31), "g:net.tcp.connectionStates,nf.test=extra,proto=v6,id=synRecv:0.000000\n");
    EXPECT_EQ(messages.at(32), "g:net.tcp.connectionStates,nf.test=extra,proto=v6,id=finWait1:0.000000\n");
    EXPECT_EQ(messages.at(33), "g:net.tcp.connectionStates,nf.test=extra,proto=v6,id=finWait2:0.000000\n");
    EXPECT_EQ(messages.at(34), "g:net.tcp.connectionStates,nf.test=extra,proto=v6,id=timeWait:0.000000\n");
    EXPECT_EQ(messages.at(35), "g:net.tcp.connectionStates,nf.test=extra,proto=v6,id=close:0.000000\n");
    EXPECT_EQ(messages.at(36), "g:net.tcp.connectionStates,nf.test=extra,proto=v6,id=closeWait:0.000000\n");
    EXPECT_EQ(messages.at(37), "g:net.tcp.connectionStates,nf.test=extra,proto=v6,id=lastAck:0.000000\n");
    EXPECT_EQ(messages.at(38), "g:net.tcp.connectionStates,nf.test=extra,proto=v6,id=listen:5.000000\n");
    EXPECT_EQ(messages.at(39), "g:net.tcp.connectionStates,nf.test=extra,proto=v6,id=closing:0.000000\n");
    EXPECT_EQ(messages.at(40), "C:net.ip.datagrams,nf.test=extra,proto=v6,id=in:26635.000000\n");
    EXPECT_EQ(messages.at(41), "C:net.ip.discards,nf.test=extra,proto=v6,id=in:0.000000\n");
    EXPECT_EQ(messages.at(42), "C:net.ip.datagrams,nf.test=extra,proto=v6,id=out:27662.000000\n");
    EXPECT_EQ(messages.at(43), "C:net.ip.discards,nf.test=extra,proto=v6,id=out:0.000000\n");
    EXPECT_EQ(messages.at(44), "C:net.ip.reasmReqds,nf.test=extra,proto=v6:0.000000\n");
    EXPECT_EQ(messages.at(45), "C:net.ip.ectPackets,nf.test=extra,proto=v6,id=capable:0.000000\n");
    EXPECT_EQ(messages.at(46), "C:net.ip.ectPackets,nf.test=extra,proto=v6,id=notCapable:26635.000000\n");
    EXPECT_EQ(messages.at(47), "C:net.ip.congestedPackets,nf.test=extra,proto=v6:0.000000\n");
    EXPECT_EQ(messages.at(48), "C:net.udp.datagrams,nf.test=extra,proto=v6,id=in:14.000000\n");
    EXPECT_EQ(messages.at(49), "C:net.udp.errors,nf.test=extra,proto=v6,id=inErrors:0.000000\n");
    EXPECT_EQ(messages.at(50), "C:net.udp.datagrams,nf.test=extra,proto=v6,id=out:14.000000\n");

    memoryWriter->Clear();
    proc.set_prefix("testdata/resources/proc2");
    proc.snmp_stats();
    messages = memoryWriter->GetMessages();

    EXPECT_EQ(messages.size(), 51);
    EXPECT_EQ(messages.at(0), "C:net.ip.datagrams,nf.test=extra,proto=v4,id=in:7000474.000000\n");
    EXPECT_EQ(messages.at(1), "C:net.ip.discards,nf.test=extra,proto=v4,id=in:1.000000\n");
    EXPECT_EQ(messages.at(2), "C:net.ip.datagrams,nf.test=extra,proto=v4,id=out:7231233.000000\n");
    EXPECT_EQ(messages.at(3), "C:net.ip.discards,nf.test=extra,proto=v4,id=out:3.000000\n");
    EXPECT_EQ(messages.at(4), "C:net.ip.reasmReqds,nf.test=extra,proto=v4:0.000000\n");
    EXPECT_EQ(messages.at(5), "C:net.tcp.segments,nf.test=extra,id=in:7864744.000000\n");
    EXPECT_EQ(messages.at(6), "C:net.tcp.segments,nf.test=extra,id=out:8280910.000000\n");
    EXPECT_EQ(messages.at(7), "C:net.tcp.errors,nf.test=extra,id=retransSegs:1167.000000\n");
    EXPECT_EQ(messages.at(8), "C:net.tcp.opens,nf.test=extra,id=active:272301.000000\n");
    EXPECT_EQ(messages.at(9), "C:net.tcp.opens,nf.test=extra,id=passive:265980.000000\n");
    EXPECT_EQ(messages.at(10), "C:net.tcp.errors,nf.test=extra,id=attemptFails:10.000000\n");
    EXPECT_EQ(messages.at(11), "C:net.tcp.errors,nf.test=extra,id=estabResets:3249.000000\n");
    EXPECT_EQ(messages.at(12), "g:net.tcp.currEstab,nf.test=extra:27.000000\n");
    EXPECT_EQ(messages.at(13), "C:net.tcp.errors,nf.test=extra,id=inErrs:10.000000\n");
    EXPECT_EQ(messages.at(14), "C:net.tcp.errors,nf.test=extra,id=outRsts:84370.000000\n");
    EXPECT_EQ(messages.at(15), "C:net.udp.datagrams,nf.test=extra,proto=v4,id=in:145618.000000\n");
    EXPECT_EQ(messages.at(16), "C:net.udp.errors,nf.test=extra,proto=v4,id=inErrors:1.000000\n");
    EXPECT_EQ(messages.at(17), "C:net.udp.datagrams,nf.test=extra,proto=v4,id=out:136742.000000\n");
    EXPECT_EQ(messages.at(18), "g:net.tcp.connectionStates,nf.test=extra,proto=v4,id=established:27.000000\n");
    EXPECT_EQ(messages.at(19), "g:net.tcp.connectionStates,nf.test=extra,proto=v4,id=synSent:0.000000\n");
    EXPECT_EQ(messages.at(20), "g:net.tcp.connectionStates,nf.test=extra,proto=v4,id=synRecv:0.000000\n");
    EXPECT_EQ(messages.at(21), "g:net.tcp.connectionStates,nf.test=extra,proto=v4,id=finWait1:0.000000\n");
    EXPECT_EQ(messages.at(22), "g:net.tcp.connectionStates,nf.test=extra,proto=v4,id=finWait2:0.000000\n");
    EXPECT_EQ(messages.at(23), "g:net.tcp.connectionStates,nf.test=extra,proto=v4,id=timeWait:1.000000\n");
    EXPECT_EQ(messages.at(24), "g:net.tcp.connectionStates,nf.test=extra,proto=v4,id=close:0.000000\n");
    EXPECT_EQ(messages.at(25), "g:net.tcp.connectionStates,nf.test=extra,proto=v4,id=closeWait:0.000000\n");
    EXPECT_EQ(messages.at(26), "g:net.tcp.connectionStates,nf.test=extra,proto=v4,id=lastAck:0.000000\n");
    EXPECT_EQ(messages.at(27), "g:net.tcp.connectionStates,nf.test=extra,proto=v4,id=listen:10.000000\n");
    EXPECT_EQ(messages.at(28), "g:net.tcp.connectionStates,nf.test=extra,proto=v4,id=closing:0.000000\n");
    EXPECT_EQ(messages.at(29), "g:net.tcp.connectionStates,nf.test=extra,proto=v6,id=established:0.000000\n");
    EXPECT_EQ(messages.at(30), "g:net.tcp.connectionStates,nf.test=extra,proto=v6,id=synSent:0.000000\n");
    EXPECT_EQ(messages.at(31), "g:net.tcp.connectionStates,nf.test=extra,proto=v6,id=synRecv:0.000000\n");
    EXPECT_EQ(messages.at(32), "g:net.tcp.connectionStates,nf.test=extra,proto=v6,id=finWait1:0.000000\n");
    EXPECT_EQ(messages.at(33), "g:net.tcp.connectionStates,nf.test=extra,proto=v6,id=finWait2:0.000000\n");
    EXPECT_EQ(messages.at(34), "g:net.tcp.connectionStates,nf.test=extra,proto=v6,id=timeWait:0.000000\n");
    EXPECT_EQ(messages.at(35), "g:net.tcp.connectionStates,nf.test=extra,proto=v6,id=close:0.000000\n");
    EXPECT_EQ(messages.at(36), "g:net.tcp.connectionStates,nf.test=extra,proto=v6,id=closeWait:0.000000\n");
    EXPECT_EQ(messages.at(37), "g:net.tcp.connectionStates,nf.test=extra,proto=v6,id=lastAck:0.000000\n");
    EXPECT_EQ(messages.at(38), "g:net.tcp.connectionStates,nf.test=extra,proto=v6,id=listen:5.000000\n");
    EXPECT_EQ(messages.at(39), "g:net.tcp.connectionStates,nf.test=extra,proto=v6,id=closing:0.000000\n");
    EXPECT_EQ(messages.at(40), "C:net.ip.datagrams,nf.test=extra,proto=v6,id=in:26735.000000\n");
    EXPECT_EQ(messages.at(41), "C:net.ip.discards,nf.test=extra,proto=v6,id=in:1.000000\n");
    EXPECT_EQ(messages.at(42), "C:net.ip.datagrams,nf.test=extra,proto=v6,id=out:28662.000000\n");
    EXPECT_EQ(messages.at(43), "C:net.ip.discards,nf.test=extra,proto=v6,id=out:2.000000\n");
    EXPECT_EQ(messages.at(44), "C:net.ip.reasmReqds,nf.test=extra,proto=v6:42.000000\n");
    EXPECT_EQ(messages.at(45), "C:net.ip.ectPackets,nf.test=extra,proto=v6,id=capable:42.000000\n");
    EXPECT_EQ(messages.at(46), "C:net.ip.ectPackets,nf.test=extra,proto=v6,id=notCapable:26645.000000\n");
    EXPECT_EQ(messages.at(47), "C:net.ip.congestedPackets,nf.test=extra,proto=v6:2.000000\n");
    EXPECT_EQ(messages.at(48), "C:net.udp.datagrams,nf.test=extra,proto=v6,id=in:24.000000\n");
    EXPECT_EQ(messages.at(49), "C:net.udp.errors,nf.test=extra,proto=v6,id=inErrors:1.000000\n");
    EXPECT_EQ(messages.at(50), "C:net.udp.datagrams,nf.test=extra,proto=v6,id=out:24.000000\n");
}

// TODO: This is a broken test, it has no validation
TEST(Proc, ParseLoadAvg)
{
    auto config = Config(WriterConfig(WriterTypes::Memory));
    auto r = Registry(config);
    atlasagent::Proc proc{&r, {{"nf.test", "extra"}}, "testdata/resources/proc"};
    proc.loadavg_stats();

    auto memoryWriter = static_cast<MemoryWriter*>(WriterTestHelper::GetImpl());
    auto messages = memoryWriter->GetMessages();
    EXPECT_EQ(3, messages.size());
    for (const auto& m : messages)
    {
        Logger()->info("Got {}", m);
    }
}

TEST(Proc, ParsePidFromSched)
{
    using atlasagent::proc::get_pid_from_sched;
    const char* container = "init (95352, #threads: 1)";
    const char* host = "systemd (1, #threads: 1)";

    EXPECT_EQ(95352, get_pid_from_sched(container));
    EXPECT_EQ(1, get_pid_from_sched(host));
}

TEST(Proc, IsContainer)
{
    auto config = Config(WriterConfig(WriterTypes::Memory));
    auto r = Registry(config);
    atlasagent::Proc proc{&r, {{"nf.test", "extra"}}, "testdata/resources/proc"};

    EXPECT_TRUE(proc.is_container());
    proc.set_prefix("testdata/resources/proc-host");
    EXPECT_FALSE(proc.is_container());
}
*/
TEST(Proc, CpuStats)
{
    auto config = Config(WriterConfig(WriterTypes::Memory));
    auto r = Registry(config);
    atlasagent::Proc proc{&r, {{"nf.test", "extra"}}, "testdata/resources/proc"};

    auto memoryWriter = static_cast<MemoryWriter*>(WriterTestHelper::GetImpl());

    proc.cpu_stats_new();
    proc.peak_cpu_stats_new();
    auto messages = memoryWriter->GetMessages();
    EXPECT_EQ(messages.size(), 1);
    EXPECT_EQ(messages.at(0), "g:sys.cpu.numProcessors:2.000000\n");

    memoryWriter->Clear();
    proc.set_prefix("testdata/resources/proc2");
    proc.cpu_stats_new();
    proc.peak_cpu_stats_new();
    messages = memoryWriter->GetMessages();

    EXPECT_EQ(15, messages.size());
    EXPECT_EQ(messages.at(0), "g:sys.cpu.utilization,id=user:7857889221593.500000\n");
    EXPECT_EQ(messages.at(1), "g:sys.cpu.utilization,id=system:0.024642\n");
    EXPECT_EQ(messages.at(2), "g:sys.cpu.utilization,id=stolen:0.001503\n");
    EXPECT_EQ(messages.at(3), "g:sys.cpu.utilization,id=nice:0.000510\n");
    EXPECT_EQ(messages.at(4), "g:sys.cpu.utilization,id=wait:0.001324\n");
    EXPECT_EQ(messages.at(5), "g:sys.cpu.utilization,id=interrupt:7857889221593.793945\n");
    EXPECT_EQ(messages.at(6), "d:sys.cpu.coreUtilization:254183268679903.093750\n");
    EXPECT_EQ(messages.at(7), "d:sys.cpu.coreUtilization:338348793412414.750000\n");
    EXPECT_EQ(messages.at(8), "g:sys.cpu.numProcessors:8.000000\n");
    EXPECT_EQ(messages.at(9), "m:sys.cpu.peakUtilization,id=user:7857889221593.500000\n");
    EXPECT_EQ(messages.at(10), "m:sys.cpu.peakUtilization,id=system:0.024642\n");
    EXPECT_EQ(messages.at(11), "m:sys.cpu.peakUtilization,id=stolen:0.001503\n");
    EXPECT_EQ(messages.at(12), "m:sys.cpu.peakUtilization,id=nice:0.000510\n");
    EXPECT_EQ(messages.at(13), "m:sys.cpu.peakUtilization,id=wait:0.001324\n");
    EXPECT_EQ(messages.at(14), "m:sys.cpu.peakUtilization,id=interrupt:7857889221593.793945\n");
}
/*
TEST(Proc, UptimeStats)
{
    auto config = Config(WriterConfig(WriterTypes::Memory));
    auto r = Registry(config);
    atlasagent::Proc proc{&r, {{"nf.test", "extra"}}, "testdata/resources/proc"};
    proc.uptime_stats();

    auto memoryWriter = static_cast<MemoryWriter*>(WriterTestHelper::GetImpl());
    auto messages = memoryWriter->GetMessages();

    EXPECT_EQ(messages.size(), 1);
    EXPECT_EQ(messages.at(0), "g:sys.uptime:517407.000000\n");
}

TEST(Proc, VmStats)
{
    auto config = Config(WriterConfig(WriterTypes::Memory));
    auto r = Registry(config);
    atlasagent::Proc proc{&r, {{"nf.test", "extra"}}, "testdata/resources/proc"};
    proc.vmstats();
    auto memoryWriter = static_cast<MemoryWriter*>(WriterTestHelper::GetImpl());
    auto messages = memoryWriter->GetMessages();

    EXPECT_EQ(messages.size(), 9);
    EXPECT_EQ(messages.at(0), "C:vmstat.procs.count:67395.000000\n");
    EXPECT_EQ(messages.at(1), "g:vmstat.procs,id=running:2.000000\n");
    EXPECT_EQ(messages.at(2), "g:vmstat.procs,id=blocked:1.000000\n");
    EXPECT_EQ(messages.at(3), "C:vmstat.paging,id=in:459380.000000\n");
    EXPECT_EQ(messages.at(4), "C:vmstat.paging,id=out:939162.000000\n");
    EXPECT_EQ(messages.at(5), "C:vmstat.swapping,id=in:0.000000\n");
    EXPECT_EQ(messages.at(6), "C:vmstat.swapping,id=out:0.000000\n");
    EXPECT_EQ(messages.at(7), "g:vmstat.fh.allocated:2016.000000\n");
    EXPECT_EQ(messages.at(8), "g:vmstat.fh.max:12556616.000000\n");

    memoryWriter->Clear();
    proc.set_prefix("testdata/resources/proc2");
    proc.vmstats();
    messages = memoryWriter->GetMessages();

    EXPECT_EQ(messages.size(), 9);
    EXPECT_EQ(messages.at(0), "C:vmstat.procs.count:67995.000000\n");
    EXPECT_EQ(messages.at(1), "g:vmstat.procs,id=running:3.000000\n");
    EXPECT_EQ(messages.at(2), "g:vmstat.procs,id=blocked:2.000000\n");
    EXPECT_EQ(messages.at(3), "C:vmstat.paging,id=in:459380.000000\n");
    EXPECT_EQ(messages.at(4), "C:vmstat.paging,id=out:939418.000000\n");
    EXPECT_EQ(messages.at(5), "C:vmstat.swapping,id=in:0.000000\n");
    EXPECT_EQ(messages.at(6), "C:vmstat.swapping,id=out:0.000000\n");
    EXPECT_EQ(messages.at(7), "g:vmstat.fh.allocated:2017.000000\n");
    EXPECT_EQ(messages.at(8), "g:vmstat.fh.max:12556616.000000\n");
}

TEST(Proc, MemoryStats)
{
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

TEST(Proc, ParseNetstat)
{
    auto config = Config(WriterConfig(WriterTypes::Memory));
    auto r = Registry(config);
    atlasagent::Proc proc{&r, {{"nf.test", "extra"}}, "testdata/resources/proc"};
    proc.netstat_stats();

    auto memoryWriter = static_cast<MemoryWriter*>(WriterTestHelper::GetImpl());
    auto messages = memoryWriter->GetMessages();

    EXPECT_EQ(messages.size(), 3);
    EXPECT_EQ(messages.at(0), "C:net.ip.congestedPackets,nf.test=extra,proto=v4:0.000000\n");
    EXPECT_EQ(messages.at(1), "C:net.ip.ectPackets,nf.test=extra,proto=v4,id=capable:122266.000000\n");
    EXPECT_EQ(messages.at(2), "C:net.ip.ectPackets,nf.test=extra,proto=v4,id=notCapable:380016.000000\n");

    memoryWriter->Clear();
    proc.set_prefix("testdata/resources/proc2");
    proc.netstat_stats();
    messages = memoryWriter->GetMessages();

    EXPECT_EQ(messages.size(), 3);
    EXPECT_EQ(messages.at(0), "C:net.ip.congestedPackets,nf.test=extra,proto=v4:30.000000\n");
    EXPECT_EQ(messages.at(1), "C:net.ip.ectPackets,nf.test=extra,proto=v4,id=capable:122446.000000\n");
    EXPECT_EQ(messages.at(2), "C:net.ip.ectPackets,nf.test=extra,proto=v4,id=notCapable:380076.000000\n");
}

TEST(Proc, ParseSocketStats)
{
    auto config = Config(WriterConfig(WriterTypes::Memory));
    auto r = Registry(config);
    atlasagent::Proc proc{&r, {{"nf.test", "extra"}}, "testdata/resources/proc"};
    proc.socket_stats();

    auto memoryWriter = static_cast<MemoryWriter*>(WriterTestHelper::GetImpl());
    auto messages = memoryWriter->GetMessages();

    auto pagesize = static_cast<size_t>(sysconf(_SC_PAGESIZE));
    auto expected = "g:net.tcp.memory:" + std::to_string(4519.0 * pagesize) + "\n";
    EXPECT_EQ(messages.size(), 1);
    EXPECT_EQ(messages.at(0), expected);
}

TEST(Proc, ArpStats)
{
    auto config = Config(WriterConfig(WriterTypes::Memory));
    auto r = Registry(config);

    atlasagent::Proc proc{&r, {{"nf.test", "extra"}}, "testdata/resources/proc"};
    proc.arp_stats();

    auto memoryWriter = static_cast<MemoryWriter*>(WriterTestHelper::GetImpl());
    auto messages = memoryWriter->GetMessages();

    EXPECT_EQ(messages.size(), 1);
    EXPECT_EQ(messages.at(0), "g:net.arpCacheSize,nf.test=extra:6.000000\n");
}

TEST(Proc, ProcessStats)
{
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
*/


// TEST(Proc, CpuStats2)
// {
//     auto config = Config(WriterConfig(WriterTypes::Memory));
//     auto r = Registry(config);
//     atlasagent::Proc proc{&r, {{"nf.test", "extra"}}, "/home/ebadeaux/cpumetrics/atlas-system-agent/lib/collectors/proc/data/old/"};

//     proc.cpu_stats_new();
//     proc.set_prefix("/home/ebadeaux/cpumetrics/atlas-system-agent/lib/collectors/proc/data/new/");
//     proc.cpu_stats_new();
    
//     auto memoryWriter = static_cast<MemoryWriter*>(WriterTestHelper::GetImpl());
//     auto oldMessages = memoryWriter->GetMessages();
//     std::sort(oldMessages.begin(), oldMessages.end());


//     memoryWriter->Clear();
//     proc.set_prefix("/home/ebadeaux/cpumetrics/atlas-system-agent/lib/collectors/proc/data/old/");
//     proc.cpu_stats();
//     proc.set_prefix("/home/ebadeaux/cpumetrics/atlas-system-agent/lib/collectors/proc/data/new/");
//     proc.cpu_stats();

    
//     auto newMessages = memoryWriter->GetMessages();
//     std::sort(newMessages.begin(), newMessages.end());
    
//     // Print messages side by side for comparison
//     std::cout << "\n=== CPU STATS COMPARISON ===" << std::endl;
//     std::cout << std::left << std::setw(50) << "cpu_stats_new() Output" << " | " << "cpu_stats() Output" << std::endl;
//     std::cout << std::string(50, '-') << " | " << std::string(50, '-') << std::endl;
    
//     size_t maxSize = std::max(oldMessages.size(), newMessages.size());
//     for (size_t i = 0; i < maxSize; ++i) {
//         std::string oldMsg = (i < oldMessages.size()) ? oldMessages[i] : "";
//         std::string newMsg = (i < newMessages.size()) ? newMessages[i] : "";
        
//         // Remove newlines for cleaner output
//         if (!oldMsg.empty() && oldMsg.back() == '\n') oldMsg.pop_back();
//         if (!newMsg.empty() && newMsg.back() == '\n') newMsg.pop_back();
        
//         std::cout << std::left << std::setw(50) << oldMsg << " | " << newMsg << std::endl;
//     }
//     std::cout << "==============================" << std::endl;
    
//     EXPECT_EQ(newMessages, oldMessages);
// }


// TEST(Proc, PeakCpuStats2)
// {
//     auto config = Config(WriterConfig(WriterTypes::Memory));
//     auto r = Registry(config);
//     atlasagent::Proc proc{&r, {{"nf.test", "extra"}}, "/home/ebadeaux/cpumetrics/atlas-system-agent/lib/collectors/proc/data/old/"};

//     proc.peak_cpu_stats_new();
//     proc.set_prefix("/home/ebadeaux/cpumetrics/atlas-system-agent/lib/collectors/proc/data/new/");
//     proc.peak_cpu_stats_new();
    
//     auto memoryWriter = static_cast<MemoryWriter*>(WriterTestHelper::GetImpl());
//     auto oldMessages = memoryWriter->GetMessages();
//     std::sort(oldMessages.begin(), oldMessages.end());

//     memoryWriter->Clear();
//     proc.set_prefix("/home/ebadeaux/cpumetrics/atlas-system-agent/lib/collectors/proc/data/old/");
//     proc.peak_cpu_stats();
//     proc.set_prefix("/home/ebadeaux/cpumetrics/atlas-system-agent/lib/collectors/proc/data/new/");
//     proc.peak_cpu_stats();  

//     auto newMessages = memoryWriter->GetMessages();
//     std::sort(newMessages.begin(), newMessages.end());

//     // Print messages side by side for comparison
//     std::cout << "\n=== PEAK CPU STATS COMPARISON ===" << std::endl;
//     std::cout << std::left << std::setw(50) << "peak_cpu_stats_new() Output" << " | " << "peak_cpu_stats() Output" << std::endl;
//     std::cout << std::string(50, '-') << " | " << std::string(50, '-') << std::endl;    
//     size_t maxSize = std::max(oldMessages.size(), newMessages.size());
//     for (size_t i = 0; i < maxSize; ++i) {
//         std::string oldMsg = (i < oldMessages.size()) ? oldMessages[i] : "";
//         std::string newMsg = (i < newMessages.size()) ? newMessages[i] : "";
//         // Remove newlines for cleaner output
//         if (!oldMsg.empty() && oldMsg.back() == '\n') oldMsg.pop_back();
//         if (!newMsg.empty() && newMsg.back() == '\n') newMsg.pop_back();
//         std::cout << std::left << std::setw(50) << oldMsg << " | " << newMsg << std::endl;
//     }
//     std::cout << "==============================" << std::endl;
// }




}  // namespace
