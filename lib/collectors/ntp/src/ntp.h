#pragma once

#include <lib/tagging/src/tagging_registry.h>
#include <lib/util/src/util.h>
#include <absl/strings/str_split.h>
#include <sys/timex.h>


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



template <typename Reg = TaggingRegistry, typename Clock = detail::abseil_clock>
class Ntp {
 public:
  explicit Ntp(Reg* registry) noexcept;
    

  void update_stats() noexcept;

 private:
  typename Reg::gauge_ptr lastSampleAge_;
  typename Reg::gauge_ptr estimatedError_;
  typename Reg::gauge_ptr unsynchronized_;

 protected:
  // for testing
  absl::Time lastSampleTime_;

  void ntp_stats(int err, timex* time);

  void chrony_stats(const std::string& tracking, const std::vector<std::string>& sources) noexcept;
};
}  // namespace atlasagent