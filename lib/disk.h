#pragma once

#include "monotonic_timer.h"
#include "spectator/registry.h"
#include <string>
#include <sys/types.h>
#include <unordered_map>
#include <vector>

namespace atlasagent {
struct MountPoint {
  unsigned device_major;
  unsigned device_minor;
  std::string mount_point;
  std::string device;
  std::string fs_type;
};

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

template <typename Reg = spectator::Registry>
class Disk {
 public:
  explicit Disk(Reg* registry, std::string path_prefix = "") noexcept
      : registry_(registry), path_prefix_(std::move(path_prefix)) {}
  void titus_disk_stats() noexcept;
  void disk_stats() noexcept;
  void set_prefix(const std::string& new_prefix) noexcept;  // for testing
 private:
  Reg* registry_;
  std::string path_prefix_;
  absl::Time last_updated_{absl::UnixEpoch()};
  std::unordered_map<std::string, u_long> last_ms_doing_io{};
  std::unordered_map<spectator::IdPtr, std::shared_ptr<MonotonicTimer<Reg>>> monotonic_timers_{};

 protected:
  // protected for testing
  void do_disk_stats(absl::Time start) noexcept;
  void stats_for_interesting_mps(std::function<void(Disk*, const MountPoint&)> stats_fn) noexcept;
  [[nodiscard]] std::vector<MountPoint> filter_interesting_mount_points(
      const std::vector<MountPoint>& mount_points) const noexcept;
  [[nodiscard]] std::vector<MountPoint> get_mount_points() const noexcept;
  [[nodiscard]] std::vector<DiskIo> get_disk_stats() const noexcept;
  void update_titus_stats_for(const MountPoint& mp) noexcept;
  void update_stats_for(const MountPoint& mp) noexcept;

  void diskio_stats(absl::Time start) noexcept;
  void set_last_updated(absl::Time updated) { last_updated_ = updated; }
};

}  // namespace atlasagent

#include "internal/disk.inc"