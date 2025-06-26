#pragma once

#include <absl/strings/str_split.h>
#include <lib/util/src/util.h>
#include <thirdparty/spectator-cpp/spectator/registry.h>


namespace atlasagent {


class PressureStall {
 public:
  explicit PressureStall(Registry registry, std::string path_prefix = "/proc/pressure") noexcept;

  void set_prefix(std::string new_prefix) noexcept;

  void update_stats() noexcept;

 private:
  Registry registry_;
  std::string path_prefix_;
  static constexpr double MICROS = 1000 * 1000.0;
};
}  // namespace atlasagent
