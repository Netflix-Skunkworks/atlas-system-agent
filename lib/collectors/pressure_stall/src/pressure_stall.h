#pragma once

#include <absl/strings/str_split.h>
#include <lib/tagging/src/tagging_registry.h>
#include <lib/util/src/util.h>

namespace atlasagent {

using spectator::Id;
using spectator::IdPtr;
using spectator::Tags;

template <typename Reg = TaggingRegistry>
class PressureStall {
 public:
  explicit PressureStall(Reg* registry, std::string path_prefix = "/proc/pressure") noexcept;

  void set_prefix(std::string new_prefix) noexcept;

  void update_stats() noexcept;

 private:
  Reg* registry_;
  std::string path_prefix_;
  static constexpr double MICROS = 1000 * 1000.0;
};
}  // namespace atlasagent
