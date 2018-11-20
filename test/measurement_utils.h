#pragma once

#include <spectator/measurement.h>
#include <unordered_map>
#include <vector>

using measurement_map = std::unordered_map<std::string, double>;
measurement_map measurements_to_map(const std::vector<spectator::Measurement>& ms,
                                    const std::string& secondary);

// removes the entry after doing the comparison so we can later iterate over all measurements
// that were generated but have not been asserted
void expect_value(measurement_map* measurements, const char* name, double expected);
