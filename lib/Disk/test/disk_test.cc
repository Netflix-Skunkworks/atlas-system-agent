#include "../src/disk.h"
#include <lib/Logger/src/logger.h>
#include <lib/MeasurementUtils/src/measurement_utils.h>
#include <gtest/gtest.h>
#include <unordered_set>

// Add formatter for MountPoint
template <> struct fmt::formatter<atlasagent::MountPoint> : formatter<std::string> {
  auto format(const atlasagent::MountPoint& mp, format_context& ctx) const {
    return format_to(ctx.out(), "MP{{dev#={}:{}, mp={}, dev={}, type={}}}",
                     mp.device_major, mp.device_minor, mp.mount_point, mp.device, mp.fs_type);
  }
};

// Add the missing function declarations from disk.cc
namespace atlasagent {
  std::unordered_set<std::string> get_nodev_filesystems(const std::string& prefix);
  std::string get_id_from_mountpoint(const std::string& mp);
  std::string get_dev_from_device(const std::string& device);
}



namespace {
using Registry = spectator::TestRegistry;
using atlasagent::Disk;
using atlasagent::DiskIo;
using atlasagent::Logger;
using atlasagent::MountPoint;

class TestDisk : public atlasagent::Disk<Registry> {
 public:
  explicit TestDisk(Registry* registry) : Disk<Registry>(registry, "testdata/resources") {}

  std::vector<MountPoint> filter_interesting_mount_points(
      const std::vector<MountPoint>& mount_points) const noexcept {
    auto v = Disk<Registry>::filter_interesting_mount_points(mount_points);
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
  Registry registry;
  TestDisk disk(&registry);
  auto mount_points = disk.get_mount_points();
  EXPECT_EQ(mount_points.size(), 16);

  disk.set_prefix("testdata/resources2");
  mount_points = disk.get_mount_points();
  for (const auto& mp : mount_points) {
    Logger()->info("{}", mp);
  }
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

TEST(Disk, InterestingMountPoints) {
  Registry registry;
  TestDisk disk(&registry);

  auto interesting = disk.filter_interesting_mount_points(disk.get_mount_points());
  for (const auto& mp : interesting) {
    atlasagent::Logger()->info("{}\n", mp);
  }
  ASSERT_EQ(interesting.size(), 2);
  EXPECT_EQ(interesting[0].mount_point, "/");
  EXPECT_EQ(interesting[1].mount_point, "/mnt");

  // titus example
  disk.set_prefix("testdata/resources2");
  auto interesting2 = disk.filter_interesting_mount_points(disk.get_mount_points());
  for (const auto& mp : interesting2) {
    atlasagent::Logger()->info("{}\n", mp);
  }
}

TEST(Disk, UpdateTitusStats) {
  Registry registry;
  TestDisk disk(&registry);

  disk.titus_disk_stats();

  const auto& ms = my_measurements(&registry);
  for (const auto& m : ms) {
    Logger()->info("Got {}", m);
  }
}

TEST(Disk, UpdateDiskStats) {
  Registry registry;
  TestDisk disk(&registry);

  auto start = absl::Now();
  disk.do_disk_stats(start);
  disk.set_last_updated(start);

  auto initial = my_measurements(&registry);

  disk.set_prefix("testdata/resources2");
  disk.do_disk_stats(start + absl::Seconds(5));

  auto ms = my_measurements(&registry);
  auto values = measurements_to_map(ms, "dev");
  expect_value(&values, "disk.io.bytes|count|read|md0", 512 * 1e4);
  expect_value(&values, "disk.io.bytes|count|read|xvda", 512 * 1e4);
  expect_value(&values, "disk.io.bytes|count|write|md0", 1535488.0 + 1562112.0);
  expect_value(&values, "disk.io.bytes|count|write|xvda", 1404928.0);
  expect_value(&values, "disk.io.bytes|count|write|xvdb", 1535488.0);
  expect_value(&values, "disk.io.bytes|count|write|xvdc", 1562112.0);
  expect_value(&values, "disk.io.ops|count|read|xvda", 101);
  expect_value(&values, "disk.io.ops|count|write|xvda", 280);
  expect_value(&values, "disk.io.ops|count|write|xvdb", 79);
  expect_value(&values, "disk.io.ops|count|write|xvdc", 69);
  expect_value(&values, "disk.io.ops|totalTime|read|xvda", 1);
  expect_value(&values, "disk.io.ops|totalTime|write|xvda", 0.216);
  expect_value(&values, "disk.io.ops|totalTime|write|xvdb", 0.072);
  expect_value(&values, "disk.io.ops|totalTime|write|xvdc", 0.028);
  expect_value(&values, "disk.percentBusy|gauge|xvda", 40);
  expect_value(&values, "disk.percentBusy|gauge|xvdb", 90);
  expect_value(&values, "disk.percentBusy|gauge|xvdc", 96);

  // the following values are coming from statvfs
  for (const auto& pair : values) {
    std::cerr << fmt::format("{}={}\n", pair.first, pair.second);
  }
}

TEST(Disk, get_disk_stats) {
  Registry registry;
  TestDisk disk(&registry);

  const auto& s = disk.get_disk_stats();
  EXPECT_EQ(14, s.size());
}

TEST(Disk, diskio_stats) {
  Registry registry;
  TestDisk disk(&registry);

  auto start = absl::Now();
  disk.diskio_stats(start);
  auto initial = my_measurements(&registry);
  EXPECT_TRUE(initial.empty());

  disk.set_prefix("testdata/resources2");
  disk.diskio_stats(start + absl::Seconds(60));

  auto ms = my_measurements(&registry);
  auto values = measurements_to_map(ms, "dev");
  expect_value(&values, "disk.io.bytes|count|read|md0", 512 * 1e4);
  expect_value(&values, "disk.io.bytes|count|read|xvda", 512 * 1e4);
  expect_value(&values, "disk.io.bytes|count|write|md0", 1535488.0 + 1562112.0);
  expect_value(&values, "disk.io.bytes|count|write|xvda", 1404928.0);
  expect_value(&values, "disk.io.bytes|count|write|xvdb", 1535488.0);
  expect_value(&values, "disk.io.bytes|count|write|xvdc", 1562112.0);
  expect_value(&values, "disk.io.ops|count|read|xvda", 101);
  expect_value(&values, "disk.io.ops|count|write|xvda", 280);
  expect_value(&values, "disk.io.ops|count|write|xvdb", 79);
  expect_value(&values, "disk.io.ops|count|write|xvdc", 69);
  expect_value(&values, "disk.io.ops|totalTime|read|xvda", 1);
  expect_value(&values, "disk.io.ops|totalTime|write|xvda", 0.216);
  expect_value(&values, "disk.io.ops|totalTime|write|xvdb", 0.072);
  expect_value(&values, "disk.io.ops|totalTime|write|xvdc", 0.028);
  EXPECT_TRUE(values.empty());
}
}  // namespace
