#include "chrony.h"
#include "util.h"
#include <cstdio>

namespace atlasagent {

Chrony::Chrony(spectator::Registry* registry) noexcept
    : sysTimeOffset_{registry->GetGauge("sys.time.offset")},
      sysLastRx_{registry->GetGauge("sys.time.lastSampleAge")} {}

void Chrony::tracking_stats(const std::string& tracking,
                            const std::vector<std::string>& sources) noexcept {
  auto is_comma = [](int c) { return c == ','; };
  if (tracking.empty()) {
    return;
  }

  std::vector<std::string> fields;
  split(tracking.c_str(), is_comma, &fields);
  if (fields.size() < 5) {
    return;
  }

  try {
    auto system_time = std::stod(fields[4], nullptr);
    sysTimeOffset_->Set(system_time);
  } catch (const std::invalid_argument& e) {
    Logger()->error("Unable to parse {} as a double: {}", fields[4], e.what());
  }

  if (sources.empty()) {
    return;
  }

  // get the last rx time for the current server
  auto& current_server = fields[1];
  for (const auto& source : sources) {
    std::vector<std::string> source_fields;
    split(source.c_str(), is_comma, &source_fields);
    if (source_fields.size() < 7) {
      continue;
    }
    const auto& server = source_fields[2];
    if (server == current_server) {
      try {
        auto last_rx = std::stod(source_fields[6], nullptr);
        sysLastRx_->Set(last_rx);
      } catch (const std::invalid_argument& e) {
        Logger()->error("Unable to parse {} as a double: {}", source_fields[6], e.what());
      }
    }
  }
}

void Chrony::update_stats() noexcept {
  auto tracking_csv = read_output_string("chronyc -c tracking");
  auto sources_csv = read_output_lines("chronyc -c sources");
  tracking_stats(tracking_csv, sources_csv);
}

}  // namespace atlasagent
