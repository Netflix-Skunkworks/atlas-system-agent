#include <lib/collectors/disk/src/disk.h>
#include <lib/logger/src/logger.h>
#include <thirdparty/spectator-cpp/spectator/registry.h>
#include <thirdparty/spectator-cpp/libs/writer/writer_wrapper/writer_test_helper.h>
#include <gtest/gtest.h>
#include <unordered_set>

namespace {
using atlasagent::Disk;
using atlasagent::DiskIo;
using atlasagent::Logger;
using atlasagent::MountPoint;

class TestDisk : public atlasagent::Disk {
 public:
  explicit TestDisk(Registry* registry) : Disk(registry, "testdata/resources") {}

  std::vector<MountPoint> filter_interesting_mount_points(
      const std::vector<MountPoint>& mount_points) const noexcept {
    auto v = Disk::filter_interesting_mount_points(mount_points);
    std::sort(v.begin(), v.end(), [](const MountPoint& a, const MountPoint& b) {
      return a.mount_point < b.mount_point;
    });
    return v;
  }

  void set_last_updated(absl::Time now) { Disk::set_last_updated(now); }

  std::vector<MountPoint> get_mount_points() const noexcept { return Disk::get_mount_points(); }

  std::vector<DiskIo> get_disk_stats() const noexcept { return Disk::get_disk_stats(); }

  void update_titus_stats_for(const MountPoint& mp) noexcept { Disk::update_titus_stats_for(mp); }

  void diskio_stats(absl::Time start) noexcept { Disk::diskio_stats(start); }

  void do_disk_stats(absl::Time start) noexcept { Disk::do_disk_stats(start); }
};

TEST(Disk, NodevFS) {
  auto fs = atlasagent::get_nodev_filesystems("testdata/resources");
  auto has_overlay = fs.find("overlay") != fs.end();
  EXPECT_TRUE(has_overlay);
  EXPECT_EQ(fs.size(), 26);
}

TEST(Disk, MountPoints) {
  auto config = Config(WriterConfig(WriterTypes::Memory));
  auto r = Registry(config);
  TestDisk disk(&r);

  auto mount_points = disk.get_mount_points();
  EXPECT_EQ(mount_points.size(), 16);

  EXPECT_EQ(fmt::format("{}", mount_points.at(0)), "MP{dev#=0:19,mp=/run,dev=tmpfs,type=tmpfs}");
  EXPECT_EQ(fmt::format("{}", mount_points.at(1)), "MP{dev#=202:0,mp=/,dev=/dev/xvda,type=ext4}");
  EXPECT_EQ(fmt::format("{}", mount_points.at(2)), "MP{dev#=0:21,mp=/dev/shm,dev=tmpfs,type=tmpfs}");
  EXPECT_EQ(fmt::format("{}", mount_points.at(3)), "MP{dev#=0:22,mp=/run/lock,dev=tmpfs,type=tmpfs}");
  EXPECT_EQ(fmt::format("{}", mount_points.at(4)), "MP{dev#=0:23,mp=/sys/fs/cgroup,dev=tmpfs,type=tmpfs}");
  EXPECT_EQ(fmt::format("{}", mount_points.at(5)), "MP{dev#=9:0,mp=/mnt,dev=/dev/md0,type=xfs}");
  EXPECT_EQ(fmt::format("{}", mount_points.at(6)), "MP{dev#=0:40,mp=/run/user/0,dev=tmpfs,type=tmpfs}");
  EXPECT_EQ(fmt::format("{}", mount_points.at(7)), "MP{dev#=7:0,mp=/snap/core/10185,dev=/dev/loop0,type=squashfs}");
  EXPECT_EQ(fmt::format("{}", mount_points.at(8)), "MP{dev#=7:1,mp=/snap/core18/1885,dev=/dev/loop1,type=squashfs}");
  EXPECT_EQ(fmt::format("{}", mount_points.at(9)), "MP{dev#=7:2,mp=/snap/lxd/16922,dev=/dev/loop2,type=squashfs}");
  EXPECT_EQ(fmt::format("{}", mount_points.at(10)), "MP{dev#=7:3,mp=/snap/amazon-ssm-agent/2012,dev=/dev/loop3,type=squashfs}");
  EXPECT_EQ(fmt::format("{}", mount_points.at(11)), "MP{dev#=7:4,mp=/snap/core18/2246,dev=/dev/loop4,type=squashfs}");
  EXPECT_EQ(fmt::format("{}", mount_points.at(12)), "MP{dev#=7:5,mp=/snap/core/11993,dev=/dev/loop5,type=squashfs}");
  EXPECT_EQ(fmt::format("{}", mount_points.at(13)), "MP{dev#=7:6,mp=/snap/core20/1242,dev=/dev/loop6,type=squashfs}");
  EXPECT_EQ(fmt::format("{}", mount_points.at(14)), "MP{dev#=7:7,mp=/snap/amazon-ssm-agent/4046,dev=/dev/loop7,type=squashfs}");
  EXPECT_EQ(fmt::format("{}", mount_points.at(15)), "MP{dev#=7:8,mp=/snap/lxd/21835,dev=/dev/loop8,type=squashfs}");
}

TEST(Disk, id) {
  using atlasagent::get_id_from_mountpoint;
  EXPECT_EQ(std::string("root"), get_id_from_mountpoint("/"));
  EXPECT_EQ(std::string("foo"), get_id_from_mountpoint("/foo"));
}

TEST(Disk, dev) {
  using atlasagent::get_dev_from_device;
  EXPECT_EQ(std::string("root"), get_dev_from_device("/dev/root"));
  EXPECT_EQ(std::string("/de"), get_dev_from_device("/de"));
  EXPECT_EQ(std::string("hda1"), get_dev_from_device("/dev/hda1"));
  EXPECT_EQ(std::string("foo"), get_dev_from_device("foo"));
}

// TODO: Broken test with Titus example
TEST(Disk, InterestingMountPoints) {
  auto config = Config(WriterConfig(WriterTypes::Memory));
  auto r = Registry(config);
  TestDisk disk(&r);

  auto interesting = disk.filter_interesting_mount_points(disk.get_mount_points());
  
  ASSERT_EQ(interesting.size(), 2);
  EXPECT_EQ(interesting[0].mount_point, "/");
  EXPECT_EQ(interesting[1].mount_point, "/mnt");
  EXPECT_EQ(fmt::format("{}", interesting.at(0)), "MP{dev#=202:0,mp=/,dev=/dev/xvda,type=ext4}");
  EXPECT_EQ(fmt::format("{}", interesting.at(1)), "MP{dev#=9:0,mp=/mnt,dev=/dev/md0,type=xfs}");

  // titus example (Broken Test)
  // Somehow this test is broken, testdata/resources2 is equal to testdata/resources
  disk.set_prefix("testdata/resources2");
  auto interesting2 = disk.filter_interesting_mount_points(disk.get_mount_points());
  EXPECT_EQ(fmt::format("{}", interesting2.at(0)), "MP{dev#=202:0,mp=/,dev=/dev/xvda,type=ext4}");
  EXPECT_EQ(fmt::format("{}", interesting2.at(1)), "MP{dev#=9:0,mp=/mnt,dev=/dev/md0,type=xfs}");
}

// Another broken test none of the values are being checked
TEST(Disk, UpdateTitusStats) {
  auto config = Config(WriterConfig(WriterTypes::Memory));
  auto r = Registry(config);
  TestDisk disk(&r);

  disk.titus_disk_stats();

  auto memoryWriter = static_cast<MemoryWriter*>(WriterTestHelper::GetImpl());
  auto messages = memoryWriter->GetMessages();
}

// TODO: Test could be improved, not all values are being checked, only the values 
// that were originally present in the spectator-cpp 2.0 migration
TEST(Disk, UpdateDiskStats) {
  auto config = Config(WriterConfig(WriterTypes::Memory));
  auto r = Registry(config);
  TestDisk disk(&r);
  
  auto start = absl::Now();
  disk.do_disk_stats(start);
  disk.set_last_updated(start);

  auto memoryWriter = static_cast<MemoryWriter*>(WriterTestHelper::GetImpl());
  auto messages = memoryWriter->GetMessages();

  memoryWriter->Clear();
  disk.set_prefix("testdata/resources2");
  disk.do_disk_stats(start + absl::Seconds(5));
  messages = memoryWriter->GetMessages();
  
  std::unordered_set<std::string> values = {
    "C:disk.io.bytes,dev=xvda,id=read:445441024.000000\n",
    "C:disk.io.bytes,dev=xvda,id=write:344576000.000000\n",
    "c:disk.io.ops,statistic=totalTime,id=read,dev=xvda:1.000000\n",   
    "c:disk.io.ops,statistic=count,id=read,dev=xvda:101.000000\n",
    "c:disk.io.ops,statistic=totalTime,id=write,dev=xvda:0.216000\n",
    "c:disk.io.ops,statistic=count,id=write,dev=xvda:280.000000\n",
    "g:disk.percentBusy,dev=xvda:40.000000\n",
    "C:disk.io.bytes,dev=xvdb,id=write:194535936.000000\n",
    "c:disk.io.ops,statistic=totalTime,id=write,dev=xvdb:0.072000\n",
    "c:disk.io.ops,statistic=count,id=write,dev=xvdb:79.000000\n",
    "g:disk.percentBusy,dev=xvdb:90.000000\n",
    "C:disk.io.bytes,dev=xvdc,id=read:6931968.000000\n",
    "C:disk.io.bytes,dev=xvdc,id=write:201859584.000000\n",
    "c:disk.io.ops,statistic=totalTime,id=write,dev=xvdc:0.028000\n",
    "c:disk.io.ops,statistic=count,id=write,dev=xvdc:69.000000\n",
    "g:disk.percentBusy,dev=xvdc:96.000000\n",
    "C:disk.io.bytes,dev=md0,id=read:12092416.000000\n",
    "C:disk.io.bytes,dev=md0,id=write:396387328.000000\n" };

    for (const auto& v : values) {
      EXPECT_TRUE(std::find(messages.begin(), messages.end(), v) != messages.end());
    }
}

// TODO: Another broken test, not all values are being checked
TEST(Disk, get_disk_stats) {
  auto config = Config(WriterConfig(WriterTypes::Memory));
  auto r = Registry(config);
  TestDisk disk(&r);

  const auto& s = disk.get_disk_stats();
  EXPECT_EQ(14, s.size());
}

TEST(Disk, diskio_stats) {
  auto config = Config(WriterConfig(WriterTypes::Memory));
  auto r = Registry(config);
  TestDisk disk(&r);
  
  auto start = absl::Now();
  disk.diskio_stats(start);
  
  auto memoryWriter = static_cast<MemoryWriter*>(WriterTestHelper::GetImpl());
  auto messages = memoryWriter->GetMessages();
  
  EXPECT_EQ(8, messages.size());
  EXPECT_EQ(messages.at(0), "C:disk.io.bytes,dev=xvda,id=read:440321024.000000\n");
  EXPECT_EQ(messages.at(1), "C:disk.io.bytes,dev=xvda,id=write:343171072.000000\n");
  EXPECT_EQ(messages.at(2), "C:disk.io.bytes,dev=xvdb,id=read:6858240.000000\n");
  EXPECT_EQ(messages.at(3), "C:disk.io.bytes,dev=xvdb,id=write:193000448.000000\n");
  EXPECT_EQ(messages.at(4), "C:disk.io.bytes,dev=xvdc,id=read:6931968.000000\n");
  EXPECT_EQ(messages.at(5), "C:disk.io.bytes,dev=xvdc,id=write:200297472.000000\n");
  EXPECT_EQ(messages.at(6), "C:disk.io.bytes,dev=md0,id=read:6972416.000000\n");
  EXPECT_EQ(messages.at(7), "C:disk.io.bytes,dev=md0,id=write:393289728.000000\n");

  // Two iterations are requied to test the monotonic timers
  memoryWriter->Clear();
  disk.set_prefix("testdata/resources2");
  disk.diskio_stats(start + absl::Seconds(60));
  messages = memoryWriter->GetMessages();
  
  EXPECT_EQ(16, messages.size());
  EXPECT_EQ(messages.at(0), "C:disk.io.bytes,dev=xvda,id=read:445441024.000000\n");
  EXPECT_EQ(messages.at(1), "C:disk.io.bytes,dev=xvda,id=write:344576000.000000\n");
  EXPECT_EQ(messages.at(2), "c:disk.io.ops,statistic=totalTime,id=read,dev=xvda:1.000000\n");
  EXPECT_EQ(messages.at(3), "c:disk.io.ops,statistic=count,id=read,dev=xvda:101.000000\n");
  EXPECT_EQ(messages.at(4), "c:disk.io.ops,statistic=totalTime,id=write,dev=xvda:0.216000\n");
  EXPECT_EQ(messages.at(5), "c:disk.io.ops,statistic=count,id=write,dev=xvda:280.000000\n");
  EXPECT_EQ(messages.at(6), "C:disk.io.bytes,dev=xvdb,id=read:6858240.000000\n");
  EXPECT_EQ(messages.at(7), "C:disk.io.bytes,dev=xvdb,id=write:194535936.000000\n");
  EXPECT_EQ(messages.at(8), "c:disk.io.ops,statistic=totalTime,id=write,dev=xvdb:0.072000\n");
  EXPECT_EQ(messages.at(9), "c:disk.io.ops,statistic=count,id=write,dev=xvdb:79.000000\n");
  EXPECT_EQ(messages.at(10), "C:disk.io.bytes,dev=xvdc,id=read:6931968.000000\n");
  EXPECT_EQ(messages.at(11), "C:disk.io.bytes,dev=xvdc,id=write:201859584.000000\n");
  EXPECT_EQ(messages.at(12), "c:disk.io.ops,statistic=totalTime,id=write,dev=xvdc:0.028000\n");
  EXPECT_EQ(messages.at(13), "c:disk.io.ops,statistic=count,id=write,dev=xvdc:69.000000\n");
  EXPECT_EQ(messages.at(14), "C:disk.io.bytes,dev=md0,id=read:12092416.000000\n");
  EXPECT_EQ(messages.at(15), "C:disk.io.bytes,dev=md0,id=write:396387328.000000\n");
}
}  // namespace
