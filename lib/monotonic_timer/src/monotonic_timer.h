#pragma once

#include <thirdparty/spectator-cpp/spectator/registry.h>
#include <absl/time/time.h>

namespace atlasagent
{

class MonotonicTimer
{
   public:
    MonotonicTimer(spectator::Registry* registry, const spectator::MeterId& id);

    void update(absl::Duration monotonic_time, int64_t monotonic_count);

   private:
    absl::Duration prev_time;
    int64_t prev_count{};
    spectator::Counter count_;
    spectator::Counter total_time_;
};

}  // namespace atlasagent
