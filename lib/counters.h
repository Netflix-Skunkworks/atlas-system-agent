#pragma once

#include <atlas/meter/registry.h>
#include <atlas/meter/monotonic_counter.h>

namespace atlasagent {
class Counters {
 public:
  explicit Counters(atlas::meter::Registry* registry) noexcept;
  atlas::meter::MonotonicCounter* get(atlas::meter::IdPtr id) noexcept;

 private:
  atlas::meter::Registry* registry_;
  std::unordered_map<atlas::meter::IdPtr, std::unique_ptr<atlas::meter::MonotonicCounter>> counters;
};

}  // namespace atlasagent
