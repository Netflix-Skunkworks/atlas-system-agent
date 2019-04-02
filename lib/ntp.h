#pragma once

#include <spectator/registry.h>

namespace atlasagent {

class NTP {
 public:
  explicit NTP(spectator::Registry* registry) noexcept;
  void update_stats() noexcept;

 private:
  std::shared_ptr<spectator::Gauge> ntpTimeOffset_;
  std::shared_ptr<spectator::Gauge> ntpEstError_;
  std::shared_ptr<spectator::Gauge> ntpPrecision_;
  std::shared_ptr<spectator::Gauge> ntpUnsynchronized_;
};
}  // namespace atlasagent