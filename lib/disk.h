#pragma once

#include <atlas/meter/registry.h>
#include <vector>
#include <atlas/meter/id.h>
#include <contain.h>
#include "../config.h"
#include "counters.h"

namespace atlasagent {
struct MountPoint {
  unsigned device_major;
  unsigned device_minor;
  std::string mount_point;
  std::string device;
  std::string fs_type;
};
std::ostream& operator<<(std::ostream& os, const MountPoint& mp);

struct DiskIo {
  int major;
  int minor;
  std::string device;
  u_long rio;
  u_long rmerge;
  u_long rsect;
  u_long ruse;
  u_long wio;
  u_long wmerge;
  u_long wsect;
  u_long wuse;
  u_long running;
  u_long use;
  u_long aveq;
};

class Disk {
 public:
  explicit Disk(atlas::meter::Registry* registry, std::string path_prefix = "") noexcept;
  void titus_disk_stats() noexcept;
  void disk_stats() noexcept;
  void set_prefix(const std::string& new_prefix) noexcept;  // for testing
 private:
  atlas::meter::Registry* registry_;
  std::string path_prefix_;
  Counters counters_;

 protected:
  // protected for testing
  void stats_for_interesting_mps(std::function<void(Disk*, const MountPoint&)> stats_fn) noexcept;
  std::vector<MountPoint> filter_interesting_mount_points(
      const std::vector<MountPoint>& mount_points) const noexcept;
  std::vector<MountPoint> get_mount_points() const noexcept;
  std::vector<DiskIo> get_disk_stats() const noexcept;
  void update_titus_stats_for(const MountPoint& mp) noexcept;
  void update_stats_for(const MountPoint& mp, const char* prefix) noexcept;
  void btrfs_stats(const MountPoint& mp) noexcept;
  void overlay_stats(const MountPoint& mp) noexcept;

  void update_gauge(const char* prefix, const char* name, const atlas::meter::Tags& tags,
                    double value) noexcept;

  void diskio_stats() noexcept;
};

}  // namespace atlasagent
