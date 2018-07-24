#include "measurement_utils.h"
#include <sstream>
#include <gtest/gtest.h>

using atlas::meter::Measurements;

measurement_map measurements_to_map(const Measurements& ms, atlas::util::StrRef secondary) {
  static auto id_ref = atlas::util::intern_str("id");
  measurement_map res;
  for (const auto& m : ms) {
    std::ostringstream os;
    const auto& tags = m.id->GetTags();
    os << m.id->Name();
    auto it = tags.at(id_ref);
    if (it.valid()) {
      os << "|" << it.get();
    }
    auto proto_it = tags.at(secondary);
    if (proto_it.valid()) {
      os << "|" << proto_it.get();
    }
    res[os.str()] = m.value;
  }
  return res;
}

void expect_value(const measurement_map& measurements, const char* name, double expected) {
  auto it = measurements.find(name);
  if (it != measurements.end()) {
    EXPECT_DOUBLE_EQ(it->second, expected) << "for " << name;
  } else {
    FAIL() << "Unable to find measurement for " << name;
  }
}
