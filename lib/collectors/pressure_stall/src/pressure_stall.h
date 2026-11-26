#pragma once

#include <string>
#include <thirdparty/spectator-cpp/spectator/registry.h>

namespace atlasagent
{

class PressureStall
{
   public:
    explicit PressureStall(spectator::Registry* registry, std::string path_prefix = "/proc/pressure") noexcept;

    void set_prefix(std::string new_prefix) noexcept;

    void collect() noexcept;

   private:
    std::string path_prefix_;
    spectator::MonotonicCounter cpu_some_;
    spectator::MonotonicCounter io_some_;
    spectator::MonotonicCounter io_full_;
    spectator::MonotonicCounter memory_some_;
    spectator::MonotonicCounter memory_full_;
};
}  // namespace atlasagent
