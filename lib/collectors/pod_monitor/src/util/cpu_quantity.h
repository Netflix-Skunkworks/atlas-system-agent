#pragma once

#include <optional>
#include <string_view>

namespace atlasagent
{

// Parses the CPU subset of a Kubernetes resource Quantity into a count of cores:
// "500m" -> 0.5, "100m" -> 0.1, "1500m" -> 1.5, "2" -> 2.0, "0.5" -> 0.5.
//
// Deliberately NOT the general Quantity grammar -- no binary/decimal SI suffixes (Ki/Mi/Gi/k/M/G)
// and no exponent form, because a CPU quantity is only ever millicpu or decimal cores. Feeding it
// a memory quantity returns nullopt rather than a wrong number.
//
// Returns nullopt for anything it cannot represent exactly: empty input, a bare "m", a negative
// value, a leading '+', a non-finite value, and -- importantly -- any input not FULLY consumed, so
// "0.5.1" and "5x0m" are rejected rather than silently truncated to 0.5 and 5.
//
// noexcept and allocation-free: every caller on the identity-resolution path is noexcept, so this
// uses std::from_chars rather than std::stod (which would terminate the process on bad input) or
// std::strtod (which silently returns 0 for garbage).
[[nodiscard]] std::optional<double> ParseCpuQuantity(std::string_view value) noexcept;

}  // namespace atlasagent
