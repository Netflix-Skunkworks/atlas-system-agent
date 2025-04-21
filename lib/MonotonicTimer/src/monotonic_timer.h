#pragma once

#include "../../spectator/registry.h"

namespace atlasagent {
template <typename Reg>
class MonotonicTimer {
 public:
  MonotonicTimer(Reg* registry, const spectator::Id& id)
      : count_{registry->GetCounter(id.WithStat("count"))},
        total_time_{registry->GetCounter(id.WithStat("totalTime"))} {}

  void update(absl::Duration monotonic_time, int64_t monotonic_count) {
    if (prev_count > 0) {
      auto delta_count = monotonic_count - prev_count;
      if (delta_count > 0) {
        auto seconds = absl::ToDoubleSeconds(monotonic_time - prev_time);
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
  absl::Duration prev_time;
  int64_t prev_count{};
  typename Reg::counter_ptr count_;
  typename Reg::counter_ptr total_time_;
};

}  // namespace atlasagent
