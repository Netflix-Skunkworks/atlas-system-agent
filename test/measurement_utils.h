#pragma once

#include <atlas/meter/measurement.h>
#include <unordered_map>

using measurement_map = std::unordered_map<std::string, double>;
measurement_map measurements_to_map(const atlas::meter::Measurements& ms, atlas::util::StrRef);
void expect_value(const measurement_map& measurements, const char* name, double expected);
