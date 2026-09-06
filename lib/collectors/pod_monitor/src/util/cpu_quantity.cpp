#include "cpu_quantity.h"

#include <charconv>
#include <cmath>

namespace atlasagent
{

std::optional<double> ParseCpuQuantity(std::string_view value) noexcept
{
    // Strip the millicpu suffix before parsing, so one numeric path serves both "1500m" and "0.5".
    const bool millicpu = value.ends_with('m');
    if (millicpu)
    {
        value.remove_suffix(1);
    }

    if (value.empty())
    {
        return std::nullopt;
    }

    double parsed = 0.0;
    const auto* const end = value.data() + value.size();
    auto [ptr, ec] = std::from_chars(value.data(), end, parsed);

    // ptr != end rejects trailing junk. from_chars stops at the first character it cannot use
    // rather than failing, so without this "0.5.1" would parse as 0.5 and "5x0m" as 5.
    if (ec != std::errc() || ptr != end)
    {
        return std::nullopt;
    }

    // from_chars's general format accepts "inf" and "nan", neither of which is a CPU quantity, and
    // a negative request is meaningless. Reject both rather than propagating them into a gauge.
    if (!std::isfinite(parsed) || parsed < 0.0)
    {
        return std::nullopt;
    }

    return millicpu ? parsed / 1000.0 : parsed;
}

}  // namespace atlasagent
