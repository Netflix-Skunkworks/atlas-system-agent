#include "pressure_stall.h"

namespace atlasagent {

template class PressureStall<atlasagent::TaggingRegistry>;
template class PressureStall<spectator::TestRegistry>;

template <typename Reg>
PressureStall<Reg>::PressureStall(Reg* registry, const std::string path_prefix) noexcept
  : registry_(registry), path_prefix_(std::move(path_prefix)) {}

template <typename Reg>
void PressureStall<Reg>::set_prefix(std::string new_prefix) noexcept {
  path_prefix_ = std::move(new_prefix);
}

template <typename Reg>
void PressureStall<Reg>::update_stats() noexcept {
  auto lines = read_lines_fields(path_prefix_, "cpu");
  if (lines.size() == 2) {
    auto some = registry_->GetMonotonicCounter(Id::of("sys.pressure.some", Tags{{"id", "cpu"}}));
    auto usecs = std::strtoul(lines[0][4].substr(6).c_str(), nullptr, 10);
    some->Set(usecs / MICROS);
  }

  lines = read_lines_fields(path_prefix_, "io");
  if (lines.size() == 2) {
    auto some = registry_->GetMonotonicCounter(Id::of("sys.pressure.some", Tags{{"id", "io"}}));
    auto usecs = std::strtoul(lines[0][4].substr(6).c_str(), nullptr, 10);
    some->Set(usecs / MICROS);

    auto full = registry_->GetMonotonicCounter(Id::of("sys.pressure.full", Tags{{"id", "io"}}));
    usecs = std::strtoul(lines[1][4].substr(6).c_str(), nullptr, 10);
    full->Set(usecs / MICROS);
  }

  lines = read_lines_fields(path_prefix_, "memory");
  if (lines.size() == 2) {
    auto some = registry_->GetMonotonicCounter(Id::of("sys.pressure.some", Tags{{"id", "memory"}}));
    auto usecs = std::strtoul(lines[0][4].substr(6).c_str(), nullptr, 10);
    some->Set(usecs / MICROS);

    auto full = registry_->GetMonotonicCounter(Id::of("sys.pressure.full", Tags{{"id", "memory"}}));
    usecs = std::strtoul(lines[1][4].substr(6).c_str(), nullptr, 10);
    full->Set(usecs / MICROS);
  }
}

}  // namespace atlasagent
