#include "../lib/disk.h"
#include "../lib/logger.h"
#include "test_registry.h"
#include <gtest/gtest.h>

namespace atlasagent {
std::string get_id_from_mountpoint(const std::string& mp);
std::string get_dev_from_device(const std::string& device);
}

using namespace atlasagent;
using atlas::meter::Tags;

class TestDisk : public Disk {
 public:
  explicit TestDisk(atlas::meter::Registry* registry)
      : Disk(registry, "./resources") {}

  std::vector<MountPoint> filter_interesting_mount_points(const std::vector<MountPoint>& mount_points) const noexcept {
    auto v = Disk::filter_interesting_mount_points(mount_points);
    std::sort(v.begin(), v.end(), [](const MountPoint& a, const MountPoint& b) {
      return a.mount_point < b.mount_point;
    });
    return v;
  }

  std::vector<MountPoint> get_mount_points() const noexcept {
    return Disk::get_mount_points();
  }

  std::vector<DiskIo> get_disk_stats() const noexcept {
    return Disk::get_disk_stats();
  }

  void update_titus_stats_for(const MountPoint& mp) noexcept {
    Disk::update_titus_stats_for(mp);
  }

  void diskio_stats() noexcept {
    Disk::diskio_stats();
  }
};

TEST(Disk, MountPoints) {
  TestRegistry registry;
  TestDisk disk(&registry);
  auto mount_points = disk.get_mount_points();
  EXPECT_EQ(mount_points.size(), 34);
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
  TestRegistry registry;
  TestDisk disk(&registry);

  auto interesting = disk.filter_interesting_mount_points(disk.get_mount_points());
  for (const auto& mp: interesting) {
    std::cerr << mp << "\n";
  }
  ASSERT_EQ(interesting.size(), 5);
  EXPECT_EQ(interesting[0].mount_point, "/");
  EXPECT_EQ(interesting[1].mount_point, "/mnt");
  EXPECT_EQ(interesting[2].mount_point, "/run");
  EXPECT_EQ(interesting[3].mount_point, "/run/lock");
  EXPECT_EQ(interesting[4].mount_point, "/run/user/0");

  // titus example
  disk.set_prefix("./resources2");
  auto interesting2 = disk.filter_interesting_mount_points(disk.get_mount_points());
  for (const auto& mp: interesting2) {
    std::cerr << mp << "\n";
  }

  ASSERT_EQ(interesting2.size(), 3);
  EXPECT_EQ(interesting2[0].mount_point, "/");
  // xvda/xvdb are not particularly useful for titus - should we special case titus?
  EXPECT_EQ(interesting2[1].device, "/dev/xvdb");
  EXPECT_EQ(interesting2[2].device, "/dev/xvda");
}

TEST(Disk, UpdateTitusStats) {
  TestRegistry registry;
  TestDisk disk(&registry);

  disk.titus_disk_stats();

  const auto& ms = registry.AllMeasurements();
  for (const auto& m : ms) {
    Logger()->info("Got {}", m);
  }
}

TEST(Disk, UpdateDiskStats) {
  TestRegistry registry;
  TestDisk disk(&registry);

  disk.disk_stats();

  const auto& ms = registry.AllMeasurements();
  for (const auto& m : ms) {
    Logger()->info("Got {}", m);
  }
}

TEST(Disk, get_disk_stats) {
  TestRegistry registry;
  TestDisk disk(&registry);

  const auto& s = disk.get_disk_stats();
  EXPECT_EQ(12, s.size());
}

TEST(Disk, diskio_stats) {
  TestRegistry registry;
  TestDisk disk(&registry);

  disk.diskio_stats();
  const auto& first = registry.AllMeasurements();
  EXPECT_EQ(first.size(), 0);

  disk.set_prefix("./resources2");
  registry.SetWall(60000);
  disk.diskio_stats();

  registry.SetWall(120000);
  const auto& ms = registry.AllMeasurements();
  for (const auto& m : ms) {
    Logger()->info("Got {}", m);
  }
}
