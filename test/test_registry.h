#pragma once

#include <fmt/ostream.h>
#include <atlas/meter/measurement.h>
#include <atlas/meter/atlas_registry.h>
#include <atlas/meter/manual_clock.h>

class TestRegistry : public atlas::meter::AtlasRegistry {
 public:
  TestRegistry() : atlas::meter::AtlasRegistry(atlas::util::kMainFrequencyMillis, &manual_clock) {
    // some monitors check for a previous update with if (last_updated > 0) ...
    // so we need to have a value greater than 0 by default
    SetWall(1);
  }

  void SetWall(int64_t millis) { manual_clock.SetWall(millis); }

  // non atlas. measurements only
  atlas::meter::Measurements my_measurements() {
    atlas::meter::Measurements res;
    auto ms = measurements();
    std::copy_if(ms.begin(), ms.end(), std::back_inserter(res),
                 [](const atlas::meter::Measurement& m) {
                   const auto& name = m.id->Name();
                   return strncmp("atlas.", name, 6) != 0;
                 });
    return res;
  }

 private:
  atlas::meter::ManualClock manual_clock;
};
