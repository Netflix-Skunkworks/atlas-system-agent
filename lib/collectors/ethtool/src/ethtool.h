#pragma once

#include <absl/strings/str_split.h>
#include <lib/tagging/src/tagging_registry.h>
#include <lib/util/src/util.h>

namespace atlasagent {

using spectator::Id;
using spectator::IdPtr;
using spectator::Tags;

template <typename Reg = TaggingRegistry>
class Ethtool {
 public:
  explicit Ethtool(Reg* registry, Tags net_tags) noexcept;

  void update_stats() noexcept;

 private:
  Reg* registry_;
  const spectator::Tags net_tags_;
  std::vector<std::string> interfaces_;

 protected:
  std::vector<std::string> enumerate_interfaces(const std::vector<std::string>& lines);

  void update_metric(const std::string& stat_line, typename Reg::monotonic_counter_ptr metric);

  void ethtool_stats(const std::vector<std::string>& nic_stats, const char* iface) noexcept;
};
}  // namespace atlasagent
