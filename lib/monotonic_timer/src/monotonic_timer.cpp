
#include "monotonic_timer.h"


namespace atlasagent {
    

template <typename Reg>
MonotonicTimer<Reg>::MonotonicTimer(Registry* registry, const spectator::Id& id)
    : count_{registry->GetCounter(id.WithStat("count"))},
      total_time_{registry->GetCounter(id.WithStat("totalTime"))} {}

template <typename Reg>
void MonotonicTimer<Reg>::update(absl::Duration monotonic_time, int64_t monotonic_count) {
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

}  // namespace atlasagent
