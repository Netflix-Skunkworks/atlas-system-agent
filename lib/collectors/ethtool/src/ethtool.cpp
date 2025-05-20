#include "ethtool.h"

namespace atlasagent {

template class Ethtool<atlasagent::TaggingRegistry>;
template class Ethtool<spectator::TestRegistry>;

template <typename Reg>
Ethtool<Reg>::Ethtool(Reg* registry, Tags net_tags) noexcept
    : registry_(registry), net_tags_{std::move(net_tags)} {}

template <typename Reg>
void Ethtool<Reg>::update_stats() noexcept {
  if (can_execute("ethtool")) {
    if (interfaces_.empty()) {
      auto ip_links = read_output_lines("ip link show");
      interfaces_ = enumerate_interfaces(ip_links);
    }

    for (auto iface : interfaces_) {
      auto nic_stats = read_output_lines(fmt::format("ethtool -S {}", iface).c_str());
      ethtool_stats(nic_stats, iface.c_str());
    }
  }
}

template <typename Reg>
std::vector<std::string> Ethtool<Reg>::enumerate_interfaces(const std::vector<std::string>& lines) {
  std::vector<std::string> result;
  std::size_t found;
  for (const auto& line : lines) {
    if (line[0] == ' ') {
      continue;
    }
    found = line.find("eth");
    if (found != std::string::npos) {
      std::vector<std::string> fields = absl::StrSplit(line, ' ');
      auto iface = fields[1].substr(0, fields[1].size() - 1);
      result.emplace_back(iface);
    }
  }
  return result;
}

template <typename Reg>
void Ethtool<Reg>::update_metric(const std::string& stat_line,
                                 typename Reg::monotonic_counter_ptr metric) {
  std::vector<std::string> stat_fields = absl::StrSplit(stat_line, ':');
  try {
    auto number = std::stoll(stat_fields[1]);
    metric->Set(number);
  } catch (const std::invalid_argument& e) {
    atlasagent::Logger()->error("Unable to parse {} as a number: {}", stat_fields[1], e.what());
  }
}

template <typename Reg>
void Ethtool<Reg>::ethtool_stats(const std::vector<std::string>& nic_stats,
                                 const char* iface) noexcept {
  std::size_t found;

  for (const auto& stat_line : nic_stats) {
    found = stat_line.find("bw_in_allowance_exceeded:");
    if (found != std::string::npos) {
      auto metric = registry_->GetMonotonicCounter(
          id_for("net.perf.bwAllowanceExceeded", iface, "in", net_tags_));
      update_metric(stat_line, metric);
      continue;
    }

    found = stat_line.find("bw_out_allowance_exceeded:");
    if (found != std::string::npos) {
      auto metric = registry_->GetMonotonicCounter(
          id_for("net.perf.bwAllowanceExceeded", iface, "out", net_tags_));
      update_metric(stat_line, metric);
      continue;
    }

    found = stat_line.find("conntrack_allowance_exceeded:");
    if (found != std::string::npos) {
      auto metric = registry_->GetMonotonicCounter(
          id_for("net.perf.conntrackAllowanceExceeded", iface, nullptr, net_tags_));
      update_metric(stat_line, metric);
      continue;
    }

    found = stat_line.find("conntrack_allowance_available:");
    if (found != std::string::npos) {
      auto metric = registry_->GetGauge(
          id_for("net.perf.conntrackAllowanceAvailable", iface, nullptr, net_tags_));
      std::vector<std::string> stat_fields = absl::StrSplit(stat_line, ':');
      try {
        auto number = std::stoll(stat_fields[1]);
        metric->Set(number);
      } catch (const std::invalid_argument& e) {
        atlasagent::Logger()->error("Unable to parse {} as a number: {}", stat_fields[1], e.what());
      }
      continue;
    }

    found = stat_line.find("linklocal_allowance_exceeded:");
    if (found != std::string::npos) {
      auto metric = registry_->GetMonotonicCounter(
          id_for("net.perf.linklocalAllowanceExceeded", iface, nullptr, net_tags_));
      update_metric(stat_line, metric);
      continue;
    }

    found = stat_line.find("pps_allowance_exceeded:");
    if (found != std::string::npos) {
      auto metric = registry_->GetMonotonicCounter(
          id_for("net.perf.ppsAllowanceExceeded", iface, nullptr, net_tags_));
      update_metric(stat_line, metric);
    }
  }
}

}  // namespace atlasagent
