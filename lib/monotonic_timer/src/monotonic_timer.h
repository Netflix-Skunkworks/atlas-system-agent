#pragma once

#include <lib/spectator/registry.h>

namespace atlasagent {
template <typename Reg>
class MonotonicTimer {
 public:
  MonotonicTimer(Reg* registry, const spectator::Id& id);

  void update(absl::Duration monotonic_time, int64_t monotonic_count);

 private:
  absl::Duration prev_time;
  int64_t prev_count{};
  typename Reg::counter_ptr count_;
  typename Reg::counter_ptr total_time_;
};

}  // namespace atlasagent
