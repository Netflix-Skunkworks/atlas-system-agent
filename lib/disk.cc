#include "disk.h"
#include "atlas-helpers.h"
#include "contain.h"
#include "logger.h"
#include "util.h"
#include <fstream>
#include <sstream>
#include <sys/statvfs.h>
#include <unordered_set>

namespace atlasagent {

using atlas::meter::IdPtr;
using atlas::meter::Registry;
using atlas::meter::Tags;

Disk::Disk(Registry* registry, std::string path_prefix, container_handle* container_handle) noexcept
    : registry_(registry),
      path_prefix_(std::move(path_prefix)),
      counters_(registry),
      container_handle_(container_handle) {
  if (container_handle_ != nullptr) {
    // to get rid of the error about container_handle_ not used on OSX
    Logger()->debug("Under containment");
  }
}

std::unordered_set<std::string> get_nodev_filesystems(const std::string& prefix) {
  std::unordered_set<std::string> res;
  auto fp = open_file(prefix, "/proc/filesystems");
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
std::vector<MountPoint> Disk::get_mount_points() const noexcept {
  auto unwanted_filesystems = get_nodev_filesystems(path_prefix_);
  unwanted_filesystems.erase("tmpfs");
#ifdef TITUS_AGENT
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

std::vector<MountPoint> Disk::filter_interesting_mount_points(
    const std::vector<MountPoint>& mount_points) const noexcept {
  std::vector<MountPoint> interesting;
  std::unordered_map<uint64_t, const MountPoint*> candidates;

  for (const auto& mp : mount_points) {
    if (starts_with(mp.mount_point.c_str(), "/sys") ||
        starts_with(mp.mount_point.c_str(), "/run") ||
        starts_with(mp.mount_point.c_str(), "/proc") ||
        starts_with(mp.mount_point.c_str(), "/dev")) {
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

void Disk::stats_for_interesting_mps(
    std::function<void(Disk*, const MountPoint&)> stats_fn) noexcept {
  auto mount_points = filter_interesting_mount_points(get_mount_points());
  for (const auto& mp : mount_points) {
    stats_fn(this, mp);
  }
}

void Disk::disk_stats() noexcept {
  stats_for_interesting_mps(
      [](Disk* disk, const MountPoint& mp) { disk->update_stats_for(mp, ""); });

  diskio_stats();
}

// parse /proc/diskstats
std::vector<DiskIo> Disk::get_disk_stats() const noexcept {
  std::vector<DiskIo> res;
  std::ostringstream os;
  os << path_prefix_ << "/proc/diskstats";
  std::string file_name = os.str();
  std::ifstream in(file_name);

  if (!in) {
    Logger()->warn("Unable to open {}", file_name);
    return res;
  }

  while (in) {
    int major = -1;
    in >> major;
    if (major < 0) {
      break;
    }

    DiskIo diskIo;
    diskIo.major = major;
    in >> diskIo.minor;
    in >> diskIo.device;
    in >> diskIo.rio;
    in >> diskIo.rmerge;
    in >> diskIo.rsect;
    in >> diskIo.ruse;
    in >> diskIo.wio;
    in >> diskIo.wmerge;
    in >> diskIo.wsect;
    in >> diskIo.wuse;
    in >> diskIo.running;
    in >> diskIo.use;
    in >> diskIo.aveq;

    res.push_back(diskIo);
  }
  return res;
}

static IdPtr id_for(Registry* registry, const char* name, const char* id, const std::string& dev) {
  Tags tags{{"id", id}, {"dev", dev.c_str()}};
  return registry->CreateId(name, tags);
}

static constexpr int kLoopDevice = 7;
static constexpr const char* kRead = "read";
static constexpr const char* kWrite = "write";

void Disk::diskio_stats() noexcept {
  const auto& stats = get_disk_stats();
  for (const auto& st : stats) {
    if (st.major == kLoopDevice) {
      continue;  // ignore loop devices
    }
    counters_.get(id_for(registry_, "disk.io.bytes", kRead, st.device))
        ->Set(static_cast<int64_t>(st.rsect) * 512);
    counters_.get(id_for(registry_, "disk.io.ops", kRead, st.device))
        ->Set(static_cast<int64_t>(st.rio));
    counters_.get(id_for(registry_, "disk.io.bytes", kWrite, st.device))
        ->Set(static_cast<int64_t>(st.wsect) * 512);
    counters_.get(id_for(registry_, "disk.io.ops", kWrite, st.device))
        ->Set(static_cast<int64_t>(st.wio));
  }
}

void Disk::titus_disk_stats() noexcept {
  stats_for_interesting_mps(
      [](Disk* disk, const MountPoint& mp) { disk->update_titus_stats_for(mp); });
}

void Disk::update_titus_stats_for(const MountPoint& mp) noexcept {
  update_stats_for(mp, "cgroup.");
}

void Disk::update_gauge(const char* prefix, const char* name, const Tags& tags,
                        double value) noexcept {
  std::string nameStr{prefix};
  nameStr += name;
  gauge(registry_, nameStr, tags)->Update(value);
}

void Disk::update_stats_for(const MountPoint& mp, const char* prefix) noexcept {
  struct statvfs st;
  if (statvfs(mp.mount_point.c_str(), &st) != 0) {
    Logger()->warn("Unable to statfs({}) = {}", mp.mount_point, strerror(errno));
    return;
  }

  auto id = get_id_from_mountpoint(mp.mount_point);
  Tags tags{{"id", id.c_str()}, {"dev", get_dev_from_device(mp.device).c_str()}};

  auto bytes_total = st.f_blocks * st.f_bsize;
  auto bytes_free = st.f_bfree * st.f_bsize;
  auto bytes_used = bytes_total - bytes_free;
  auto bytes_percent = 100.0 * bytes_used / bytes_total;
  update_gauge(prefix, "disk.bytesFree", tags, bytes_free);
  update_gauge(prefix, "disk.bytesUsed", tags, bytes_used);
  update_gauge(prefix, "disk.bytesMax", tags, bytes_total);
  update_gauge(prefix, "disk.bytesPercentUsed", tags, bytes_percent);

  if (st.f_files > 0) {
    auto inodes_free = st.f_ffree;
    auto inodes_used = st.f_files - st.f_ffree;
    auto inodes_percent = 100.0 * inodes_used / st.f_files;
    update_gauge(prefix, "disk.inodesFree", tags, inodes_free);
    update_gauge(prefix, "disk.inodesUsed", tags, inodes_used);
    update_gauge(prefix, "disk.inodesPercentUsed", tags, inodes_percent);
  }
}

// for debugging
std::ostream& operator<<(std::ostream& os, const MountPoint& mp) {
  os << "MP{dev#=" << mp.device_major << ':' << mp.device_minor << ",mp=" << mp.mount_point
     << ",dev=" << mp.device << ",type=" << mp.fs_type << '}';
  return os;
}

void Disk::set_prefix(const std::string& new_prefix) noexcept { path_prefix_ = new_prefix; }

}  // namespace atlasagent
