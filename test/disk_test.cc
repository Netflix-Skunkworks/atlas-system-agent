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
using spectator::GetConfiguration;
using spectator::Registry;
using spectator::Tags;
using time_point = spectator::Registry::clock::time_point;

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

  void set_last_updated(time_point now) { Disk::set_last_updated(now); }

  std::vector<MountPoint> get_mount_points() const noexcept { return Disk::get_mount_points(); }

  std::vector<DiskIo> get_disk_stats() const noexcept { return Disk::get_disk_stats(); }

  void update_titus_stats_for(const MountPoint& mp) noexcept { Disk::update_titus_stats_for(mp); }

  void diskio_stats(time_point start) noexcept { Disk::diskio_stats(start); }

  void do_disk_stats(time_point start) noexcept { Disk::do_disk_stats(start); }
};

TEST(Disk, NodevFS) {
  auto fs = atlasagent::get_nodev_filesystems("./resources");
  auto has_overlay = fs.find("overlay") != fs.end();
  EXPECT_TRUE(has_overlay);
  EXPECT_EQ(fs.size(), 26);
}

TEST(Disk, MountPoints) {
  Registry registry(GetConfiguration(), Logger());
  TestDisk disk(&registry);
  auto mount_points = disk.get_mount_points();
  EXPECT_EQ(mount_points.size(), 7);

  disk.set_prefix("./resources2");
  mount_points = disk.get_mount_points();
  for (const auto& mp : mount_points) {
    Logger()->info("{}", mp);
  }
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
  Registry registry(GetConfiguration(), Logger());
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
}

TEST(Disk, UpdateTitusStats) {
  Registry registry(GetConfiguration(), Logger());
  TestDisk disk(&registry);

  disk.titus_disk_stats();

  const auto& ms = registry.Measurements();
  for (const auto& m : ms) {
    Logger()->info("Got {}", m);
  }
}

TEST(Disk, UpdateDiskStats) {
  Registry registry(GetConfiguration(), Logger());
  TestDisk disk(&registry);

  auto start = Registry::clock::now();
  disk.do_disk_stats(start);
  disk.set_last_updated(start);

  auto initial = registry.Measurements();

  disk.set_prefix("./resources2");
  disk.do_disk_stats(start + std::chrono::seconds(5));

  auto ms = registry.Measurements();
  auto values = measurements_to_map(ms, "dev");
  expect_value(&values, "disk.io.bytes|count|read|md0", 512 * 1e4);
  expect_value(&values, "disk.io.bytes|count|read|xvda", 512 * 1e4);
  expect_value(&values, "disk.io.bytes|count|write|md0", 1535488.0 + 1562112.0);
  expect_value(&values, "disk.io.bytes|count|write|xvda", 1404928.0);
  expect_value(&values, "disk.io.bytes|count|write|xvdb", 1535488.0);
  expect_value(&values, "disk.io.bytes|count|write|xvdc", 1562112.0);
  expect_value(&values, "disk.io.ops|count|read|md0", 102);
  expect_value(&values, "disk.io.ops|count|read|xvda", 101);
  expect_value(&values, "disk.io.ops|count|write|md0", 147);
  expect_value(&values, "disk.io.ops|count|write|xvda", 280);
  expect_value(&values, "disk.io.ops|count|write|xvdb", 79);
  expect_value(&values, "disk.io.ops|count|write|xvdc", 69);
  expect_value(&values, "disk.io.ops|totalTime|read|md0", 1);
  expect_value(&values, "disk.io.ops|totalTime|read|xvda", 1);
  expect_value(&values, "disk.io.ops|totalTime|write|md0", 0.5);
  expect_value(&values, "disk.io.ops|totalTime|write|xvda", 0.216);
  expect_value(&values, "disk.io.ops|totalTime|write|xvdb", 0.072);
  expect_value(&values, "disk.io.ops|totalTime|write|xvdc", 0.028);
  expect_value(&values, "disk.percentBusy|gauge|md0", 80);
  expect_value(&values, "disk.percentBusy|gauge|xvda", 40);
  expect_value(&values, "disk.percentBusy|gauge|xvdb", 90);
  expect_value(&values, "disk.percentBusy|gauge|xvdc", 96);

  // the following values are coming from statvfs
  for (const auto& pair : values) {
    std::cerr << fmt::format("{}={}\n", pair.first, pair.second);
  }
}

TEST(Disk, get_disk_stats) {
  Registry registry(GetConfiguration(), Logger());
  TestDisk disk(&registry);

  const auto& s = disk.get_disk_stats();
  EXPECT_EQ(14, s.size());
}

TEST(Disk, diskio_stats) {
  Registry registry(GetConfiguration(), Logger());
  TestDisk disk(&registry);

  auto start = spectator::Registry::clock::now();
  disk.diskio_stats(start);
  auto initial = registry.Measurements();
  EXPECT_TRUE(initial.empty());

  disk.set_prefix("./resources2");
  disk.diskio_stats(start + std::chrono::seconds{60});

  auto ms = registry.Measurements();
  auto values = measurements_to_map(ms, "dev");
  expect_value(&values, "disk.io.bytes|count|read|md0", 512 * 1e4);
  expect_value(&values, "disk.io.bytes|count|read|xvda", 512 * 1e4);
  expect_value(&values, "disk.io.bytes|count|write|md0", 1535488.0 + 1562112.0);
  expect_value(&values, "disk.io.bytes|count|write|xvda", 1404928.0);
  expect_value(&values, "disk.io.bytes|count|write|xvdb", 1535488.0);
  expect_value(&values, "disk.io.bytes|count|write|xvdc", 1562112.0);
  expect_value(&values, "disk.io.ops|count|read|md0", 102);
  expect_value(&values, "disk.io.ops|count|read|xvda", 101);
  expect_value(&values, "disk.io.ops|count|write|md0", 147);
  expect_value(&values, "disk.io.ops|count|write|xvda", 280);
  expect_value(&values, "disk.io.ops|count|write|xvdb", 79);
  expect_value(&values, "disk.io.ops|count|write|xvdc", 69);
  expect_value(&values, "disk.io.ops|totalTime|read|md0", 1);
  expect_value(&values, "disk.io.ops|totalTime|read|xvda", 1);
  expect_value(&values, "disk.io.ops|totalTime|write|md0", 0.5);
  expect_value(&values, "disk.io.ops|totalTime|write|xvda", 0.216);
  expect_value(&values, "disk.io.ops|totalTime|write|xvdb", 0.072);
  expect_value(&values, "disk.io.ops|totalTime|write|xvdc", 0.028);
  EXPECT_TRUE(values.empty());
}
