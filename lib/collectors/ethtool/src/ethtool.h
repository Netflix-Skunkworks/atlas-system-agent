#pragma once

#include <absl/strings/str_split.h>
#include <thirdparty/spectator-cpp/spectator/registry.h>
#include <lib/util/src/util.h>

namespace atlasagent {


class Ethtool {
 public:
  explicit Ethtool(Registry* registry, std::unordered_map<std::string, std::string> net_tags = {}) noexcept;

  void update_stats() noexcept;

 private:
  Registry* registry_;
  //const spectator::Tags net_tags_;
  std::unordered_map<std::string, std::string> net_tags_;
  std::vector<std::string> interfaces_;

 protected:
  std::vector<std::string> enumerate_interfaces(const std::vector<std::string>& lines);

  void update_metric(const std::string& stat_line, MonotonicCounter metric);

  void ethtool_stats(const std::vector<std::string>& nic_stats, const char* iface) noexcept;
};
}  // namespace atlasagent
