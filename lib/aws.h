#pragma once

#include <spectator/http_client.h>
#include <spectator/registry.h>
#include "logger.h"

namespace atlasagent {
class Aws {
 public:
  explicit Aws(spectator::Registry* registry) noexcept;

  void update_stats() noexcept;

 private:
  spectator::Registry* registry_;

  std::string metadata_url_;
  std::string creds_url_;

  spectator::HttpClient http_client_;

 protected:
  void update_stats_from(std::chrono::system_clock::time_point now,
                         const std::string& json) noexcept;
};

}  // namespace atlasagent
