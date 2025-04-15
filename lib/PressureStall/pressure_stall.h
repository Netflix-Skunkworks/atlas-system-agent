#pragma once

#include "absl/strings/str_split.h"
#include "../Tagging/tagging_registry.h"
#include "../Util/util.h"

namespace atlasagent {

using spectator::Id;
using spectator::IdPtr;
using spectator::Tags;

template <typename Reg = TaggingRegistry>
class PressureStall {
 public:
  explicit PressureStall(Reg* registry, std::string path_prefix = "/proc/pressure") noexcept
      : registry_(registry),
        path_prefix_(std::move(path_prefix)) {}

  void set_prefix(std::string new_prefix) noexcept { path_prefix_ = std::move(new_prefix); }

  void update_stats() noexcept {
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

 private:
  Reg* registry_;
  std::string path_prefix_;
  static constexpr double MICROS = 1000 * 1000.0;
};
}  // namespace atlasagent
