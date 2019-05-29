#include "measurement_utils.h"
#include <gtest/gtest.h>
#include <sstream>

using spectator::Measurement;

measurement_map measurements_to_map(const std::vector<Measurement>& ms,
                                    const std::string& secondary) {
  measurement_map res;
  for (const auto& m : ms) {
    std::ostringstream os;
    const auto& tags = m.id->GetTags();
    auto st = tags.at("statistic");
    os << m.id->Name() << "|" << st;
    auto it = tags.at("id");
    if (!it.empty()) {
      os << "|" << it;
    }
    auto proto_it = tags.at(secondary);
    if (!proto_it.empty()) {
      os << "|" << proto_it;
    }
    res[os.str()] = m.value;
  }
  return res;
}

void expect_value(measurement_map* measurements, const char* name, double expected) {
  auto it = measurements->find(name);
  if (it != measurements->end()) {
    EXPECT_DOUBLE_EQ(it->second, expected) << "for " << name;
    measurements->erase(it);
  } else {
    FAIL() << "Unable to find measurement for " << name;
  }
}
