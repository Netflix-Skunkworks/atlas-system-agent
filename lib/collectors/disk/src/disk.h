#pragma once
#include <thirdparty/spectator-cpp/spectator/registry.h>
#include <lib/monotonic_timer/src/monotonic_timer.h>
#include <string>
#include <sys/types.h>
#include <fmt/format.h>
#include <unordered_map>
#include <vector>
#include <unordered_set>
#include <absl/time/time.h>
#include <absl/time/clock.h>

namespace atlasagent
{
struct MountPoint
{
    unsigned device_major;
    unsigned device_minor;
    std::string mount_point;
    std::string device;
    std::string fs_type;
};

struct DiskIo
{
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

// Helper functions
std::unordered_set<std::string> get_nodev_filesystems(const std::string& prefix);
std::string get_id_from_mountpoint(const std::string& mp);
std::string get_dev_from_device(const std::string& device);

class Disk
{
   public:
    explicit Disk(Registry* registry, std::string path_prefix = "") noexcept
        : registry_(registry), path_prefix_(std::move(path_prefix))
    {
    }
    void titus_disk_stats() noexcept;
    void disk_stats() noexcept;
    void set_prefix(const std::string& new_prefix) noexcept;  // for testing
   private:
    Registry* registry_;
    std::string path_prefix_;
    absl::Time last_updated_{absl::UnixEpoch()};
    std::unordered_map<std::string, u_long> last_ms_doing_io{};
    std::unordered_map<MeterId, std::shared_ptr<MonotonicTimer>> monotonic_timers_{};

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

// for debugging
template <>
struct fmt::formatter<atlasagent::MountPoint> : formatter<std::string_view>
{
    static auto format(const atlasagent::MountPoint& mp, format_context& ctx) -> format_context::iterator
    {
        return fmt::format_to(ctx.out(), "MP{{dev#={}:{},mp={},dev={},type={}}}", mp.device_major, mp.device_minor,
                              mp.mount_point, mp.device, mp.fs_type);
    }
};
