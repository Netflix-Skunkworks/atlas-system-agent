#pragma once

#include "tagger.h"
#include <lib/spectator/registry.h>

namespace atlasagent {

/// Wrap a spectator registry to be able to add some tags based on
/// metric names
template <typename Reg>
class base_tagging_registry {
 public:
  base_tagging_registry(Reg* registry, Tagger tagger)
      : registry_{registry}, tagger_{std::move(tagger)} {}
  base_tagging_registry(const base_tagging_registry&) = default;
  ~base_tagging_registry() = default;

  auto GetCounter(absl::string_view name, spectator::Tags tags = {}) {
    return registry_->GetCounter(tagger_.GetId(name, std::move(tags)));
  }
  auto GetCounter(const spectator::IdPtr& id) {
    return registry_->GetCounter(tagger_.GetId(id));
  }
  auto GetDistributionSummary(absl::string_view name, spectator::Tags tags = {}) {
    return registry_->GetDistributionSummary(tagger_.GetId(name, std::move(tags)));
  }
  auto GetDistributionSummary(const spectator::IdPtr& id) {
    return registry_->GetDistributionSummary(tagger_.GetId(id));
  }
  auto GetGauge(absl::string_view name, spectator::Tags tags = {}) {
    return registry_->GetGauge(tagger_.GetId(name, std::move(tags)));
  }
  auto GetGauge(const spectator::IdPtr& id) {
    return registry_->GetGauge(tagger_.GetId(id));
  }
  auto GetGaugeTTL(absl::string_view name, unsigned int ttl_seconds, spectator::Tags tags = {}) {
    return registry_->GetGaugeTTL(tagger_.GetId(name, std::move(tags)), ttl_seconds);
  }
  auto GetMaxGauge(absl::string_view name, spectator::Tags tags = {}) {
    return registry_->GetMaxGauge(tagger_.GetId(name, std::move(tags)));
  }
  auto GetMaxGauge(const spectator::IdPtr& id) {
    return registry_->GetMaxGauge(tagger_.GetId(id));
  }
  auto GetMonotonicCounter(absl::string_view name, spectator::Tags tags = {}) {
    return registry_->GetMonotonicCounter(tagger_.GetId(name, std::move(tags)));
  }
  auto GetMonotonicCounter(const spectator::IdPtr& id) {
    return registry_->GetMonotonicCounter(tagger_.GetId(id));
  }
  auto GetTimer(absl::string_view name, spectator::Tags tags = {}) {
    return registry_->GetTimer(tagger_.GetId(name, std::move(tags)));
  }
  auto GetTimer(const spectator::IdPtr& id) {
    return registry_->GetTimer(tagger_.GetId(id));
  }
  auto GetPercentileTimer(const spectator::IdPtr& id, absl::Duration min, absl::Duration max) {
    return registry_->GetPercentileTimer(id, min, max);
  }

  // types
  using counter_t = typename Reg::counter_t;
  using counter_ptr = std::shared_ptr<counter_t>;
  using monotonic_counter_t = typename Reg::monotonic_counter_t;
  using monotonic_counter_ptr = std::shared_ptr<monotonic_counter_t>;
  using gauge_t = typename Reg::gauge_t;
  using gauge_ptr = std::shared_ptr<gauge_t>;
  using max_gauge_t = typename Reg::max_gauge_t;
  using max_gauge_ptr = std::shared_ptr<max_gauge_t>;
  using dist_summary_t = typename Reg::dist_summary_t;
  using dist_summary_ptr = std::shared_ptr<dist_summary_t>;
  using timer_t = typename Reg::timer_t;
  using timer_ptr = std::shared_ptr<timer_t>;

 private:
  Reg* registry_;
  Tagger tagger_;
};

using TaggingRegistry = base_tagging_registry<spectator::Registry>;
}  // namespace atlasagent