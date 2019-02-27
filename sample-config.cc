#include <spectator/config.h>

std::unique_ptr<spectator::Config> GetSpectatorConfig() {
  return spectator::GetConfiguration();
}


