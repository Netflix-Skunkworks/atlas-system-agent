#include "ntp.h"

namespace atlasagent {

template class Ntp<atlasagent::TaggingRegistry>;
template class Ntp<spectator::TestRegistry>;
template class atlasagent::Ntp<spectator::TestRegistry, TestClock>;

template <typename Reg, typename Clock>
Ntp<Reg, Clock>::Ntp(Reg* registry) noexcept

    : lastSampleAge_{registry->GetGauge("sys.time.lastSampleAge")},
      estimatedError_{registry->GetGauge("sys.time.estimatedError")},
      unsynchronized_{registry->GetGauge("sys.time.unsynchronized")},
      lastSampleTime_{Clock::now()} {}

template <typename Reg, typename Clock>
void Ntp<Reg, Clock>::update_stats() noexcept {
  if (can_execute("chronyc")) {
    auto tracking_csv = read_output_string("chronyc -c tracking");
    auto sources_csv = read_output_lines("chronyc -c sources");
    chrony_stats(tracking_csv, sources_csv);
  }

  struct timex time {};

  auto err = ntp_adjtime(&time);
  ntp_stats(err, &time);
}

template <typename Reg, typename Clock>
void Ntp<Reg, Clock>::ntp_stats(int err, timex* time) {
  if (err == -1) {
    atlasagent::Logger()->warn("Unable to ntp_gettime: {}", strerror(errno));
    return;
  }

  unsynchronized_->Set(err == TIME_ERROR);
  if (err != TIME_ERROR) {
    estimatedError_->Set(time->esterror / 1e6);
  }
}

template <typename Reg, typename Clock>
void Ntp<Reg, Clock>::chrony_stats(const std::string& tracking,
                                   const std::vector<std::string>& sources) noexcept {
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

}  // namespace atlasagent