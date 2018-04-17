#pragma once

#include <atlas/interpreter/interpreter.h>
#include <atlas/meter/manual_clock.h>
#include <atlas/meter/subscription_registry.h>

class TestRegistry : public atlas::meter::SubscriptionRegistry {
 public:
  TestRegistry()
      : atlas::meter::SubscriptionRegistry(
            std::make_unique<atlas::interpreter::Interpreter>(
                std::make_unique<atlas::interpreter::ClientVocabulary>()),
            &manual_clock) {
    // some monitors check for a previous update with if (last_updated > 0) ...
    // so we need to have a value greater than 0 by default
    SetWall(1);

  }
  atlas::meter::Measurements AllMeasurements() const {
    return GetMeasurements(atlas::util::kMainFrequencyMillis);
  }

  void SetWall(int64_t millis) { manual_clock.SetWall(millis); }

 private:
  atlas::meter::ManualClock manual_clock;
};
