#include "pressure_stall.h"

namespace atlasagent
{

PressureStall::PressureStall(Registry* registry, const std::string path_prefix) noexcept
    : registry_(registry), path_prefix_(std::move(path_prefix))
{
}

void PressureStall::set_prefix(std::string new_prefix) noexcept { path_prefix_ = std::move(new_prefix); }

void PressureStall::update_stats() noexcept
{
    auto lines = read_lines_fields(path_prefix_, "cpu");
    if (lines.size() == 2)
    {
        auto usecs = std::strtoul(lines[0][4].substr(6).c_str(), nullptr, 10);
        registry_->CreateMonotonicCounter("sys.pressure.some", {{"id", "cpu"}}).Set(usecs / MICROS);
    }

    lines = read_lines_fields(path_prefix_, "io");
    if (lines.size() == 2)
    {
        auto usecs = std::strtoul(lines[0][4].substr(6).c_str(), nullptr, 10);
        registry_->CreateMonotonicCounter("sys.pressure.some", {{"id", "io"}}).Set(usecs / MICROS);

        usecs = std::strtoul(lines[1][4].substr(6).c_str(), nullptr, 10);
        registry_->CreateMonotonicCounter("sys.pressure.full", {{"id", "io"}}).Set(usecs / MICROS);
    }

    lines = read_lines_fields(path_prefix_, "memory");
    if (lines.size() == 2)
    {
        auto usecs = std::strtoul(lines[0][4].substr(6).c_str(), nullptr, 10);
        registry_->CreateMonotonicCounter("sys.pressure.some", {{"id", "memory"}}).Set(usecs / MICROS);

        usecs = std::strtoul(lines[1][4].substr(6).c_str(), nullptr, 10);
        registry_->CreateMonotonicCounter("sys.pressure.full", {{"id", "memory"}}).Set(usecs / MICROS);
    }
}

}  // namespace atlasagent
