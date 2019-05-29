#pragma once

#include <spectator/registry.h>

class MonotonicTimer {
 public:
  MonotonicTimer(spectator::Registry* registry, spectator::IdPtr id)
      : count_{registry->GetCounter(id->WithStat("count"))},
        total_time_{registry->GetCounter(id->WithStat("totalTime"))} {}

  void update(std::chrono::nanoseconds monotonic_time, int64_t monotonic_count) {
    if (prev_count > 0) {
      auto delta_count = monotonic_count - prev_count;
      if (delta_count > 0) {
        auto seconds = (monotonic_time - prev_time).count() / 1e9;
        if (seconds >= 0) {
          total_time_->Add(seconds);
          count_->Add(delta_count);
        }
      }
    }
    prev_time = monotonic_time;
    prev_count = monotonic_count;
  }

 private:
  std::chrono::nanoseconds prev_time;
  int64_t prev_count;
  std::shared_ptr<spectator::Counter> count_;
  std::shared_ptr<spectator::Counter> total_time_;
};