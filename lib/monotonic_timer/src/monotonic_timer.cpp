
#include "monotonic_timer.h"

namespace atlasagent
{

MonotonicTimer::MonotonicTimer(Registry* registry, const MeterId& id)
    : count_{registry->CreateCounter(id.WithStat("count"))},
      total_time_{registry->CreateCounter(id.WithStat("totalTime"))}
{
}

void MonotonicTimer::update(absl::Duration monotonic_time, int64_t monotonic_count)
{
    if (prev_count > 0)
    {
        auto delta_count = monotonic_count - prev_count;
        if (delta_count > 0)
        {
            auto seconds = absl::ToDoubleSeconds(monotonic_time - prev_time);
            if (seconds >= 0)
            {
                total_time_.Increment(seconds);
                count_.Increment(delta_count);
            }
        }
    }
    prev_time = monotonic_time;
    prev_count = monotonic_count;
}

}  // namespace atlasagent
