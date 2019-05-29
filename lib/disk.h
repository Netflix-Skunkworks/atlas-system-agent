#pragma once

#include <spectator/registry.h>
#include <vector>
#include <contain.h>
#include "../config.h"
#include "monotonic_timer.h"

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
  u_long reads_completed;
  u_long reads_merged;
  u_long rsect;
  u_long ms_reading;
  u_long writes_completed;
  u_long writes_merged;
  u_long wsect;
  u_long ms_writing;
  u_long ios_in_progress;
  u_long ms_doing_io;
  u_long weighted_ms_doing_io;
};

class Disk {
 public:
  explicit Disk(spectator::Registry* registry, std::string path_prefix = "") noexcept;
  void titus_disk_stats() noexcept;
  void disk_stats() noexcept;
  void set_prefix(const std::string& new_prefix) noexcept;  // for testing
 private:
  spectator::Registry* registry_;
  std::string path_prefix_;
  spectator::Registry::clock::time_point last_updated_{};
  std::unordered_map<std::string, u_long> last_ms_doing_io{};
  std::unordered_map<spectator::IdPtr, std::shared_ptr<MonotonicTimer>> monotonic_timers_{};

 protected:
  // protected for testing
  void do_disk_stats(spectator::Registry::clock::time_point start) noexcept;
  void stats_for_interesting_mps(std::function<void(Disk*, const MountPoint&)> stats_fn) noexcept;
  std::vector<MountPoint> filter_interesting_mount_points(
      const std::vector<MountPoint>& mount_points) const noexcept;
  std::vector<MountPoint> get_mount_points() const noexcept;
  std::vector<DiskIo> get_disk_stats() const noexcept;
  void update_titus_stats_for(const MountPoint& mp) noexcept;
  void update_stats_for(const MountPoint& mp, const char* prefix) noexcept;

  void update_gauge(const char* prefix, const char* name, const spectator::Tags& tags,
                    double value) noexcept;

  void diskio_stats(spectator::Registry::clock::time_point start) noexcept;
  void set_last_updated(spectator::Registry::clock::time_point updated) { last_updated_ = updated; }
};

}  // namespace atlasagent
