#include "../lib/disk.h"
#include "../lib/logger.h"
#include "measurement_utils.h"
#include <fmt/ostream.h>
#include <gtest/gtest.h>
#include <unordered_set>

namespace atlasagent {
std::string get_id_from_mountpoint(const std::string& mp);
std::string get_dev_from_device(const std::string& device);
std::unordered_set<std::string> get_nodev_filesystems(const std::string& prefix);
}  // namespace atlasagent

using namespace atlasagent;
using spectator::Config;
using spectator::Registry;
using spectator::Tags;

class TestDisk : public Disk {
 public:
  explicit TestDisk(spectator::Registry* registry) : Disk(registry, "./resources") {}

  std::vector<MountPoint> filter_interesting_mount_points(
      const std::vector<MountPoint>& mount_points) const noexcept {
    auto v = Disk::filter_interesting_mount_points(mount_points);
    std::sort(v.begin(), v.end(), [](const MountPoint& a, const MountPoint& b) {
      return a.mount_point < b.mount_point;
    });
    return v;
  }

  std::vector<MountPoint> get_mount_points() const noexcept { return Disk::get_mount_points(); }

  std::vector<DiskIo> get_disk_stats() const noexcept { return Disk::get_disk_stats(); }

  void update_titus_stats_for(const MountPoint& mp) noexcept { Disk::update_titus_stats_for(mp); }

  void diskio_stats() noexcept { Disk::diskio_stats(); }
};

TEST(Disk, NodevFS) {
  auto fs = atlasagent::get_nodev_filesystems("./resources");
  auto has_overlay = fs.find("overlay") != fs.end();
  EXPECT_TRUE(has_overlay);
  EXPECT_EQ(fs.size(), 26);
}

TEST(Disk, MountPoints) {
  Registry registry(Config{});
  TestDisk disk(&registry);
  auto mount_points = disk.get_mount_points();
  EXPECT_EQ(mount_points.size(), 7);

  disk.set_prefix("./resources2");
  mount_points = disk.get_mount_points();
  for (const auto& mp : mount_points) {
    Logger()->info("{}", mp);
  }
#ifdef TITUS_AGENT
  // titus does not ignore overlay
  EXPECT_EQ(mount_points.size(), 5);
#endif
}

TEST(Disk, id) {
  EXPECT_EQ(std::string("root"), get_id_from_mountpoint("/"));
  EXPECT_EQ(std::string("foo"), get_id_from_mountpoint("/foo"));
}

TEST(Disk, dev) {
  EXPECT_EQ(std::string("root"), get_dev_from_device("/dev/root"));
  EXPECT_EQ(std::string("/de"), get_dev_from_device("/de"));
  EXPECT_EQ(std::string("hda1"), get_dev_from_device("/dev/hda1"));
  EXPECT_EQ(std::string("foo"), get_dev_from_device("foo"));
}

TEST(Disk, InterestingMountPoints) {
  Registry registry(Config{});
  TestDisk disk(&registry);

  auto interesting = disk.filter_interesting_mount_points(disk.get_mount_points());
  for (const auto& mp : interesting) {
    std::cerr << mp << "\n";
  }
  ASSERT_EQ(interesting.size(), 2);
  EXPECT_EQ(interesting[0].mount_point, "/");
  EXPECT_EQ(interesting[1].mount_point, "/mnt");

  // titus example
  disk.set_prefix("./resources2");
  auto interesting2 = disk.filter_interesting_mount_points(disk.get_mount_points());
  for (const auto& mp : interesting2) {
    std::cerr << mp << "\n";
  }

#ifdef TITUS_AGENT
  ASSERT_EQ(interesting2.size(), 1);
  EXPECT_EQ(interesting2[0].mount_point, "/");
#endif
}

TEST(Disk, UpdateTitusStats) {
  Registry registry(Config{});
  TestDisk disk(&registry);

  disk.titus_disk_stats();

  const auto& ms = registry.Measurements();
  for (const auto& m : ms) {
    Logger()->info("Got {}", m);
  }
}

TEST(Disk, UpdateDiskStats) {
  Registry registry(Config{});
  TestDisk disk(&registry);

  disk.disk_stats();
  auto initial = registry.Measurements();

  disk.set_prefix("./resources2");
  disk.disk_stats();
  auto ms = registry.Measurements();
  auto values = measurements_to_map(ms, "dev");
  expect_value(&values, "disk.io.bytes|read|md0", 512 * 1e4);
  expect_value(&values, "disk.io.bytes|read|xvda", 512 * 1e4);
  expect_value(&values, "disk.io.bytes|write|md0", 1535488.0 + 1562112.0);
  expect_value(&values, "disk.io.bytes|write|xvda", 1404928.0);
  expect_value(&values, "disk.io.bytes|write|xvdb", 1535488.0);
  expect_value(&values, "disk.io.bytes|write|xvdc", 1562112.0);
  expect_value(&values, "disk.io.ops|read|md0", 100);
  expect_value(&values, "disk.io.ops|read|xvda", 100);
  expect_value(&values, "disk.io.ops|write|md0", 147);
  expect_value(&values, "disk.io.ops|write|xvda", 130);
  expect_value(&values, "disk.io.ops|write|xvdb", 69);
  expect_value(&values, "disk.io.ops|write|xvdc", 60);

  // the following values are coming from statvfs
  for (const auto& pair : values) {
    std::cerr << fmt::format("{}={}\n", pair.first, pair.second);
  }
}

TEST(Disk, get_disk_stats) {
  Registry registry(Config{});
  TestDisk disk(&registry);

  const auto& s = disk.get_disk_stats();
  EXPECT_EQ(14, s.size());
}

TEST(Disk, diskio_stats) {
  Registry registry(Config{});
  TestDisk disk(&registry);

  disk.diskio_stats();
  auto initial = registry.Measurements();
  EXPECT_TRUE(initial.empty());

  disk.set_prefix("./resources2");
  disk.diskio_stats();

  auto ms = registry.Measurements();
  auto values = measurements_to_map(ms, "dev");
  expect_value(&values, "disk.io.bytes|read|md0", 512 * 1e4);
  expect_value(&values, "disk.io.bytes|read|xvda", 512 * 1e4);
  expect_value(&values, "disk.io.bytes|write|md0", 1535488.0 + 1562112.0);
  expect_value(&values, "disk.io.bytes|write|xvda", 1404928.0);
  expect_value(&values, "disk.io.bytes|write|xvdb", 1535488.0);
  expect_value(&values, "disk.io.bytes|write|xvdc", 1562112.0);
  expect_value(&values, "disk.io.ops|read|md0", 100);
  expect_value(&values, "disk.io.ops|read|xvda", 100);
  expect_value(&values, "disk.io.ops|write|md0", 147);
  expect_value(&values, "disk.io.ops|write|xvda", 130);
  expect_value(&values, "disk.io.ops|write|xvdb", 69);
  expect_value(&values, "disk.io.ops|write|xvdc", 60);
  EXPECT_TRUE(values.empty());
}
