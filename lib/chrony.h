#pragma once

#include <spectator/registry.h>

namespace atlasagent {
class Chrony {
 public:
  explicit Chrony(spectator::Registry* registry) noexcept;
  void update_stats() noexcept;

 private:
  std::shared_ptr<spectator::Gauge> sysTimeOffset_;
  std::shared_ptr<spectator::Gauge> sysLastRx_;

 protected:
  // for testing
  void tracking_stats(const std::string& tracking,
                      const std::vector<std::string>& sources) noexcept;
};
}  // namespace atlasagent
