#include "counters.h"

namespace atlasagent {

using atlas::meter::IdPtr;
using atlas::meter::MonotonicCounter;
using atlas::meter::Registry;

Counters::Counters(Registry* registry) noexcept : registry_(registry) {}

MonotonicCounter* Counters::get(IdPtr id) noexcept {
  auto it = counters.find(id);
  if (it != counters.end()) {
    return (*it).second.get();
  }

  auto ctr_pointer = std::make_unique<MonotonicCounter>(registry_, id);
  auto ctr = ctr_pointer.get();
  counters.insert(it, std::make_pair(id, std::move(ctr_pointer)));
  return ctr;
}

}  // namespace atlasagent
