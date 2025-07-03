#pragma once
#include <thirdparty/spectator-cpp/spectator/registry.h>


#include <lib/util/src/util.h>
#include <absl/strings/str_split.h>
#include <sys/timex.h>
#include <absl/time/time.h>
#include <absl/time/clock.h>

struct TestClock {
  static absl::Time now() {
    static auto fixed = absl::Now();
    return fixed;
  }
};

namespace atlasagent {

namespace detail {
struct abseil_clock {
  static absl::Time now() { return absl::Now(); }
};
}  // namespace detail



template <typename Clock = detail::abseil_clock>
class Ntp {
 public:
  explicit Ntp(Registry* registry) noexcept;

  void update_stats() noexcept;

 private:
  Registry* registry_;
  Gauge lastSampleAge_;
  Gauge estimatedError_;
  Gauge unsynchronized_;

 protected:
  // for testing
  absl::Time lastSampleTime_;

  void ntp_stats(int err, timex* time);

  void chrony_stats(const std::string& tracking, const std::vector<std::string>& sources) noexcept;
};
}  // namespace atlasagent