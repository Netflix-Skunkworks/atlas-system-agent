#include "disk.h"
#include <lib/util/src/util.h>
#include <absl/strings/str_split.h>
#include <fstream>
#include <iostream>
#include <sstream>
#include <sys/statvfs.h>
#include <unordered_set>

namespace atlasagent {

using spectator::IdPtr;
using spectator::Registry;
using spectator::Tags;

template class Disk<atlasagent::TaggingRegistry>;
template class Disk<spectator::TestRegistry>;

inline std::unordered_set<std::string> get_nodev_filesystems(const std::string& prefix) {
  std::unordered_set<std::string> res;
  auto fp = open_file(prefix, "proc/filesystems");
  if (!fp) {
    return res;
  }
  char line[2048];
  while (fgets(line, sizeof line, fp) != nullptr) {
    static constexpr size_t PREFIX_LEN = 6;
    auto len = strlen(line);
    if (len < PREFIX_LEN + 1) {
      continue;
    }

    if (starts_with(line, "nodev\t")) {
      // remove the new line
      std::string fs{&line[PREFIX_LEN], len - PREFIX_LEN - 1};
      res.emplace(std::move(fs));
    }
  }
  return res;
}

// parse /proc/self/mountinfo
template <typename Reg>
std::vector<MountPoint> Disk<Reg>::get_mount_points() const noexcept {
  auto unwanted_filesystems = get_nodev_filesystems(path_prefix_);
  unwanted_filesystems.erase("tmpfs");
#if defined(TITUS_SYSTEM_SERVICE)
  // for titus we generate metrics for overlay fs
  // see overlay_stats()
  unwanted_filesystems.erase("overlay");
#endif

  auto file_name = fmt::format("{}/proc/self/mountinfo", path_prefix_);
  std::ifstream in(file_name);
  std::vector<MountPoint> res;

  if (!in) {
    Logger()->warn("Unable to open {}", file_name);
    return res;
  }

  std::string slash{"/"};
  while (in) {
    MountPoint mp;
    unsigned ignored;
    std::string root;
    char ch;
    in >> ignored;
    if (in.eof()) {
      break;
    }
    in >> ignored;
    in >> mp.device_major;
    in >> ch;  // ':';
    in >> mp.device_minor;
    in >> root;
    auto keep = root == slash;
    if (keep) {
      in >> mp.mount_point;  // relative to root, but we only concern ourselves with root = /
      in.ignore(std::numeric_limits<std::streamsize>::max(), '-');
      in >> mp.fs_type;
      in >> mp.device;
    }
    in.ignore(std::numeric_limits<std::streamsize>::max(), '\n');

    // add only if the filesystem is not in our unwanted blacklist
    if (keep && unwanted_filesystems.find(mp.fs_type) == unwanted_filesystems.end()) {
      res.push_back(std::move(mp));
    }
  }
  return res;
}

static constexpr int kMultipleDevice = 9;
static constexpr int kLoopDevice = 7;
static constexpr int kRamDevice = 1;

template <typename Reg>
std::vector<MountPoint> Disk<Reg>::filter_interesting_mount_points(
    const std::vector<MountPoint>& mount_points) const noexcept {
  std::vector<MountPoint> interesting;
  std::unordered_map<uint64_t, const MountPoint*> candidates;

  for (const auto& mp : mount_points) {
    if (mp.device_major == kLoopDevice || mp.device_major == kRamDevice) {
      continue;
    }

    if (starts_with(mp.mount_point.c_str(), "/dev") ||
        starts_with(mp.mount_point.c_str(), "/mnt/docker") ||
        starts_with(mp.mount_point.c_str(), "/mnt/kubelet") ||
        starts_with(mp.mount_point.c_str(), "/proc") ||
        starts_with(mp.mount_point.c_str(), "/run") ||
        starts_with(mp.mount_point.c_str(), "/sys") ||
        starts_with(mp.mount_point.c_str(), "/tmp/buildkit") ||
        starts_with(mp.mount_point.c_str(), "/var/lib")) {
      continue;
    }

    auto key = static_cast<uint64_t>(mp.device_major) << 32u | mp.device_minor;
    auto candidate = candidates[key];
    if (candidate != nullptr) {
      // keep the shortest path if more than one mount point refers to the same device
      // if both /foo and /foo/bar are mounted on /dev/xvda we just keep /foo
      auto mp_len = mp.mount_point.length();
      auto candidate_len = candidate->mount_point.length();
      if (mp_len < candidate_len) {
        candidates[key] = &mp;
      }
    } else {
      candidates[key] = &mp;
    }
  }
  interesting.reserve(candidates.size());
  for (const auto& kv : candidates) {
    interesting.emplace_back(*kv.second);
  }
  return interesting;
}

std::string get_id_from_mountpoint(const std::string& mp) {
  const auto& mp_len = mp.length();
  return mp_len == 1 ? std::string("root") : mp.substr(1);
}

std::string get_dev_from_device(const std::string& device) {
  if (device.substr(0, 5) == "/dev/") {
    return device.substr(5);
  }

  // should be very rare
  return device;
}

template <typename Reg>
void Disk<Reg>::stats_for_interesting_mps(
    std::function<void(Disk*, const MountPoint&)> stats_fn) noexcept {
  auto mount_points = filter_interesting_mount_points(get_mount_points());
  for (const auto& mp : mount_points) {
    stats_fn(this, mp);
  }
}

template <typename Reg>
void Disk<Reg>::disk_stats() noexcept {
  do_disk_stats(absl::Now());
}

template <typename Reg>
void Disk<Reg>::do_disk_stats(absl::Time start) noexcept {
  stats_for_interesting_mps([](Disk* disk, const MountPoint& mp) { disk->update_stats_for(mp); });

  diskio_stats(start);
  last_updated_ = absl::Now();
}

// parse /proc/diskstats
template <typename Reg>
std::vector<DiskIo> Disk<Reg>::get_disk_stats() const noexcept {
  std::vector<DiskIo> res;
  std::ostringstream os;
  os << path_prefix_ << "/proc/diskstats";
  std::string file_name = os.str();
  std::ifstream in(file_name);

  if (!in) {
    Logger()->warn("Unable to open {}", file_name);
    return res;
  }

  for (std::string line; std::getline(in, line);) {
    std::vector<std::string> fields =
        absl::StrSplit(line, absl::ByAnyChar(" \t"), absl::SkipWhitespace());
    if (fields.size() < 14) continue;
    int major = std::stoi(fields[0]);
    if (major < 0) {
      break;
    }

    DiskIo diskIo;
    diskIo.major = major;
    diskIo.minor = std::stoi(fields[1]);
    diskIo.device = std::move(fields[2]);
    diskIo.reads_completed = std::strtoul(fields[3].c_str(), nullptr, 10);
    diskIo.reads_merged = std::strtoul(fields[4].c_str(), nullptr, 10);
    diskIo.rsect = std::strtoul(fields[5].c_str(), nullptr, 10);
    diskIo.ms_reading = std::strtoul(fields[6].c_str(), nullptr, 10);
    diskIo.writes_completed = std::strtoul(fields[7].c_str(), nullptr, 10);
    diskIo.writes_merged = std::strtoul(fields[8].c_str(), nullptr, 10);
    diskIo.wsect = std::strtoul(fields[9].c_str(), nullptr, 10);
    diskIo.ms_writing = std::strtoul(fields[10].c_str(), nullptr, 10);
    diskIo.ios_in_progress = std::strtoul(fields[11].c_str(), nullptr, 10);
    diskIo.ms_doing_io = std::strtoul(fields[12].c_str(), nullptr, 10);
    diskIo.weighted_ms_doing_io = std::strtoul(fields[13].c_str(), nullptr, 10);

    res.push_back(diskIo);
  }
  return res;
}

inline IdPtr id_for(const char* name, const char* id, const std::string& dev) {
  return spectator::Id::of(name, {{"id", id}, {"dev", dev.c_str()}});
}

static constexpr const char* kRead = "read";
static constexpr const char* kWrite = "write";

template <typename Reg>
void Disk<Reg>::diskio_stats(absl::Time start) noexcept {
  const auto& stats = get_disk_stats();

  for (const auto& st : stats) {
    if (st.major == kLoopDevice || st.major == kRamDevice) {
      continue;  // ignore loop and ram devices
    }

    registry_->GetMonotonicCounter(id_for("disk.io.bytes", kRead, st.device))->Set(st.rsect * 512);
    registry_->GetMonotonicCounter(id_for("disk.io.bytes", kWrite, st.device))->Set(st.wsect * 512);

    if (st.major == kMultipleDevice) {
      continue;  // ignore multiple devices for disk.io.ops and disk.percentBusy - they do not provide timing stats
    }

    auto mono_read_id = id_for("disk.io.ops", kRead, st.device);
    auto mono_write_id = id_for("disk.io.ops", kWrite, st.device);
    auto busy_gauge_id = spectator::Id::of("disk.percentBusy", {{"dev", st.device}});

    std::shared_ptr<MonotonicTimer<Reg>> read_timer;
    std::shared_ptr<MonotonicTimer<Reg>> write_timer;

    auto it = monotonic_timers_.find(mono_read_id);

    if (it != monotonic_timers_.end()) {
      read_timer = it->second;
      write_timer = monotonic_timers_[mono_write_id];
    } else {
      read_timer.reset(new MonotonicTimer<Reg>(registry_, *mono_read_id));
      write_timer.reset(new MonotonicTimer<Reg>(registry_, *mono_write_id));

      monotonic_timers_[mono_read_id] = read_timer;
      monotonic_timers_[mono_write_id] = write_timer;
    }

    auto read_time = absl::Milliseconds(st.ms_reading);
    auto write_time = absl::Milliseconds(st.ms_writing);
    read_timer->update(read_time, st.reads_completed + st.reads_merged);
    write_timer->update(write_time, st.writes_completed + st.writes_merged);

    if (last_updated_ > absl::UnixEpoch()) {
      auto delta_t = start - last_updated_;
      auto delta_millis = absl::ToInt64Milliseconds(delta_t);
      auto last_time = last_ms_doing_io[st.device];
      if (st.ms_doing_io >= last_time) {
        auto delta_time_doing_io = st.ms_doing_io - last_time;
        registry_->GetGauge(busy_gauge_id)->Set(100.0 * delta_time_doing_io / delta_millis);
      }
    }

    last_ms_doing_io[st.device] = st.ms_doing_io;
  }
}

template <typename Reg>
void Disk<Reg>::titus_disk_stats() noexcept {
  stats_for_interesting_mps([](Disk* disk, const MountPoint& mp) { disk->update_stats_for(mp); });
}

template <typename Reg>
void Disk<Reg>::update_stats_for(const MountPoint& mp) noexcept {
  struct statvfs st;
  if (statvfs(mp.mount_point.c_str(), &st) != 0) {
    // do not generate warnings for tmpfs mount points. On some systems
    // we'll get a permission denied error (titusagents) and generate a lot
    // of noise in the logs
    if (mp.fs_type != "tmpfs") {
      Logger()->warn("Unable to statvfs({}) = {}", mp.mount_point, strerror(errno));
    }
    return;
  }

  auto id = get_id_from_mountpoint(mp.mount_point);
  Tags tags{{"id", id.c_str()}, {"dev", get_dev_from_device(mp.device).c_str()}};

  auto bytes_total = st.f_blocks * st.f_bsize;
  auto bytes_free = st.f_bfree * st.f_bsize;
  auto bytes_used = bytes_total - bytes_free;
  auto bytes_percent = 100.0 * bytes_used / bytes_total;
  registry_->GetGauge("disk.bytesFree", tags)->Set(bytes_free);
  registry_->GetGauge("disk.bytesUsed", tags)->Set(bytes_used);
  registry_->GetGauge("disk.bytesMax", tags)->Set(bytes_total);
  registry_->GetGauge("disk.bytesPercentUsed", tags)->Set(bytes_percent);

  if (st.f_files > 0) {
    auto inodes_free = st.f_ffree;
    auto inodes_used = st.f_files - st.f_ffree;
    auto inodes_percent = 100.0 * inodes_used / st.f_files;
    registry_->GetGauge("disk.inodesFree", tags)->Set(inodes_free);
    registry_->GetGauge("disk.inodesUsed", tags)->Set(inodes_used);
    registry_->GetGauge("disk.inodesPercentUsed", tags)->Set(inodes_percent);
  }
}

template <typename Reg>
void Disk<Reg>::set_prefix(const std::string& new_prefix) noexcept {
  path_prefix_ = new_prefix;
}

}  // namespace atlasagent
