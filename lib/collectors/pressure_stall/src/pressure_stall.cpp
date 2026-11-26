#include "pressure_stall.h"

#include <absl/strings/numbers.h>
#include <lib/util/src/util.h>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

using spectator::Registry;

namespace atlasagent
{

namespace
{

constexpr double kMicrosPerSecond = 1'000'000.0;

// Sets counter to the cumulative stall time, in seconds, from the fields of one PSI line:
//   "some avg10=0.00 avg60=0.00 avg300=0.00 total=<microseconds>"
// A malformed line leaves the counter untouched.
void set_total_seconds(spectator::MonotonicCounter& counter, const std::vector<std::string>& fields)
{
    constexpr std::size_t kTotalField = 4;
    constexpr std::string_view kTotal = "total=";
    std::uint64_t usecs{};
    if (fields.size() <= kTotalField || !fields[kTotalField].starts_with(kTotal) ||
        !absl::SimpleAtoi(absl::string_view{fields[kTotalField]}.substr(kTotal.size()), &usecs))
    {
        return;
    }

    counter.Set(static_cast<double>(usecs) / kMicrosPerSecond);
}

// Reads one /proc/pressure file: line 1 ("some") sets some, line 2 ("full") sets full when given
void collect_file(const std::string& prefix, const char* name, spectator::MonotonicCounter& some,
                  spectator::MonotonicCounter* full = nullptr)
{
    if (const auto lines = read_lines_fields(prefix, name); lines.size() == 2)
    {
        set_total_seconds(some, lines[0]);
        if (full != nullptr)
        {
            set_total_seconds(*full, lines[1]);
        }
    }
}

}  // namespace

PressureStall::PressureStall(Registry* registry, std::string path_prefix) noexcept
    : path_prefix_(std::move(path_prefix)),
      cpu_some_{registry->CreateMonotonicCounter("sys.pressure.some", {{"id", "cpu"}})},
      io_some_{registry->CreateMonotonicCounter("sys.pressure.some", {{"id", "io"}})},
      io_full_{registry->CreateMonotonicCounter("sys.pressure.full", {{"id", "io"}})},
      memory_some_{registry->CreateMonotonicCounter("sys.pressure.some", {{"id", "memory"}})},
      memory_full_{registry->CreateMonotonicCounter("sys.pressure.full", {{"id", "memory"}})}
{
}

void PressureStall::set_prefix(std::string new_prefix) noexcept { path_prefix_ = std::move(new_prefix); }

void PressureStall::collect() noexcept
{
    // /proc/pressure is not available on RHEL/Rocky. Use the error_code overload: the throwing one
    // would terminate the agent from this noexcept function on errors such as EACCES.
    if (std::error_code ec; !std::filesystem::exists(path_prefix_, ec))
    {
        return;
    }

    collect_file(path_prefix_, "cpu", cpu_some_);  // CPU "full" is undefined system-wide (always 0)
    collect_file(path_prefix_, "io", io_some_, &io_full_);
    collect_file(path_prefix_, "memory", memory_some_, &memory_full_);
}

}  // namespace atlasagent
