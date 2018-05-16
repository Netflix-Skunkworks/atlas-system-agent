#include "../lib/disk.h"
#include "../lib/logger.h"
#include "test_registry.h"
#include <gtest/gtest.h>
#include <unordered_set>

namespace atlasagent {
std::string get_id_from_mountpoint(const std::string& mp);
std::string get_dev_from_device(const std::string& device);
std::unordered_set<std::string> get_nodev_filesystems(const std::string& prefix);
}  // namespace atlasagent

using namespace atlasagent;
using atlas::meter::ManualClock;
using atlas::meter::Tags;

class TestDisk : public Disk {
 public:
  explicit TestDisk(atlas::meter::Registry* registry) : Disk(registry, "./resources") {}

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
  ManualClock clock;
  TestRegistry registry(&clock);
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
#else
  EXPECT_EQ(mount_points.size(), 4);
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
  ManualClock clock;
  TestRegistry registry(&clock);
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
#else
  ASSERT_EQ(interesting2.size(), 0);
#endif
}

TEST(Disk, UpdateTitusStats) {
  ManualClock clock;
  TestRegistry registry(&clock);
  TestDisk disk(&registry);

  disk.titus_disk_stats();

  const auto& ms = registry.my_measurements();
  for (const auto& m : ms) {
    Logger()->info("Got {}", m);
  }
}

TEST(Disk, UpdateDiskStats) {
  ManualClock clock;
  TestRegistry registry(&clock);
  TestDisk disk(&registry);

  disk.disk_stats();

  const auto& ms = registry.my_measurements();
  for (const auto& m : ms) {
    Logger()->info("Got {}", m);
  }
}

TEST(Disk, get_disk_stats) {
  ManualClock clock;
  TestRegistry registry(&clock);
  TestDisk disk(&registry);

  const auto& s = disk.get_disk_stats();
  EXPECT_EQ(12, s.size());
}

TEST(Disk, diskio_stats) {
  ManualClock clock;
  TestRegistry registry(&clock);
  TestDisk disk(&registry);

  disk.diskio_stats();
  const auto& first = registry.my_measurements();
  EXPECT_EQ(first.size(), 0);

  disk.set_prefix("./resources2");
  registry.SetWall(60000);
  disk.diskio_stats();

  registry.SetWall(120000);
  const auto& ms = registry.my_measurements();
  for (const auto& m : ms) {
    Logger()->info("Got {}", m);
  }
}
