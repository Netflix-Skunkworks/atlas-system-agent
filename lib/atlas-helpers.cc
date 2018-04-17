#include "atlas-helpers.h"

using atlas::meter::Gauge;
using atlas::meter::MonotonicCounter;
using atlas::meter::Registry;
using atlas::meter::Tags;

std::shared_ptr<Gauge<double>> gauge(Registry* registry, std::string name, const Tags& tags) {
  return registry->gauge(registry->CreateId(std::move(name), tags));
}

std::unique_ptr<MonotonicCounter> monotonic_counter(Registry* registry, const char* name,
                                                    const Tags& tags) {
  return std::make_unique<MonotonicCounter>(registry, registry->CreateId(name, tags));
}
