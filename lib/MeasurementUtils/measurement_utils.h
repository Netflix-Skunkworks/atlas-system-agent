#pragma once

#include "../spectator/registry.h"
#include <unordered_map>
#include <vector>

using measurement_map = std::unordered_map<std::string, double>;
measurement_map measurements_to_map(const std::vector<spectator::Measurement>& ms,
                                    const std::string& secondary);

// removes the entry after doing the comparison so we can later iterate over all measurements
// that were generated but have not been asserted
void expect_value(measurement_map* measurements, const char* name, double expected);

inline std::vector<spectator::Measurement> my_measurements(spectator::TestRegistry* registry) {
  auto ms = registry->Measurements();
  std::vector<spectator::Measurement> result;
  std::copy_if(
      ms.begin(), ms.end(), std::back_inserter(result),
      [](const spectator::Measurement& m) { return m.id->Name().rfind("spectator.", 0) != 0; });
  return result;
}
