#pragma once

#include <lib/tagging/src/tagging_registry.h>
#include <lib/util/src/util.h>
#include <absl/strings/str_split.h>
#include <sys/timex.h>

namespace atlasagent {

namespace detail {
struct abseil_clock {
  static absl::Time now() { return absl::Now(); }
};
}  // namespace detail

template <typename Reg = TaggingRegistry, typename Clock = detail::abseil_clock>
class Ntp {
 public:
  explicit Ntp(Reg* registry) noexcept
      : lastSampleAge_{registry->GetGauge("sys.time.lastSampleAge")},
        estimatedError_{registry->GetGauge("sys.time.estimatedError")},
        unsynchronized_{registry->GetGauge("sys.time.unsynchronized")},
        lastSampleTime_{Clock::now()} {}

  void update_stats() noexcept {
    if (can_execute("chronyc")) {
      auto tracking_csv = read_output_string("chronyc -c tracking");
      auto sources_csv = read_output_lines("chronyc -c sources");
      chrony_stats(tracking_csv, sources_csv);
    }

    struct timex time {};

    auto err = ntp_adjtime(&time);
    ntp_stats(err, &time);
  }

 private:
  typename Reg::gauge_ptr lastSampleAge_;
  typename Reg::gauge_ptr estimatedError_;
  typename Reg::gauge_ptr unsynchronized_;

 protected:
  // for testing
  absl::Time lastSampleTime_;

  void ntp_stats(int err, timex* time) {
    if (err == -1) {
      atlasagent::Logger()->warn("Unable to ntp_gettime: {}", strerror(errno));
      return;
    }

    unsynchronized_->Set(err == TIME_ERROR);
    if (err != TIME_ERROR) {
      estimatedError_->Set(time->esterror / 1e6);
    }
  }

  void chrony_stats(const std::string& tracking, const std::vector<std::string>& sources) noexcept {
    std::vector<std::string> fields = absl::StrSplit(tracking, ',');

    // get the last rx time for the current server
    std::string current_server = fields.size() > 1 ? fields[1] : "";
    for (const auto& source : sources) {
      std::vector<std::string> source_fields = absl::StrSplit(source, ',');
      if (source_fields.size() < 7) {
        continue;
      }
      const auto& server = source_fields[2];
      if (server == current_server) {
        try {
          auto last_rx = absl::Seconds(std::stoll(source_fields[6], nullptr));
          lastSampleTime_ = Clock::now() - last_rx;
        } catch (const std::invalid_argument& e) {
          atlasagent::Logger()->error("Unable to parse {} as a number: {}", source_fields[6],
                                      e.what());
        }
      }
    }

    lastSampleAge_->Set(absl::ToDoubleSeconds(Clock::now() - lastSampleTime_));
  }
};
}  // namespace atlasagent
