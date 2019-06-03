#pragma once

#include "util.h"
#include <spectator/registry.h>
#include <sys/timex.h>

namespace atlasagent {

template <typename Clock = std::chrono::system_clock>
class Ntp {
 public:
  explicit Ntp(spectator::Registry* registry) noexcept
      : lastSampleAge_{registry->GetGauge("sys.time.lastSampleAge")},
        estimatedError_{registry->GetGauge("sys.time.estimatedError")},
        unsynchronized_{registry->GetGauge("sys.time.unsynchronized")} {}

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
  std::shared_ptr<spectator::Gauge> lastSampleAge_;
  std::shared_ptr<spectator::Gauge> estimatedError_;
  std::shared_ptr<spectator::Gauge> unsynchronized_;

 protected:
  // for testing
  typename Clock::time_point lastSampleTime_{};

  void ntp_stats(int err, timex* time) {
    if (err == -1) {
      atlasagent::Logger()->warn("Unable to ntp_gettime: {}", strerror(errno));
      return;
    }

    unsynchronized_->Set(err == TIME_ERROR);
    estimatedError_->Set(time->esterror / 1e6);
  }

  void chrony_stats(const std::string& tracking, const std::vector<std::string>& sources) noexcept {
    auto is_comma = [](int c) { return c == ','; };
    std::vector<std::string> fields;
    split(tracking.c_str(), is_comma, &fields);

    // get the last rx time for the current server
    std::string current_server = fields.size() > 1 ? fields[1] : "";
    for (const auto& source : sources) {
      std::vector<std::string> source_fields;
      split(source.c_str(), is_comma, &source_fields);
      if (source_fields.size() < 7) {
        continue;
      }
      const auto& server = source_fields[2];
      if (server == current_server) {
        try {
          auto last_rx = std::chrono::seconds{std::stoll(source_fields[6], nullptr)};
          lastSampleTime_ = Clock::now() - last_rx;
        } catch (const std::invalid_argument& e) {
          atlasagent::Logger()->error("Unable to parse {} as a number: {}", source_fields[6],
                                      e.what());
        }
      }
    }

    auto ageNanos =
        std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - lastSampleTime_);
    auto seconds = ageNanos.count() / 1e9;
    lastSampleAge_->Set(seconds);
  }
};
}  // namespace atlasagent
