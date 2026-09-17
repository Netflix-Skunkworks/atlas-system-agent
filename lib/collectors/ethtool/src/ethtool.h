#pragma once

#include <thirdparty/spectator-cpp/spectator/registry.h>
#include <lib/util/src/util.h>

#include <string>
#include <unordered_map>
#include <vector>

namespace atlasagent
{

class Ethtool
{
   public:
    explicit Ethtool(Registry* registry, std::unordered_map<std::string, std::string> net_tags = {},
                     std::string path_prefix = "/sys/class/net") noexcept;

    virtual ~Ethtool() = default;

    void collect() noexcept;

   protected:
    std::vector<std::string> enumerate_interfaces() noexcept;

    void ethtool_stats(const std::vector<std::string>& nic_stats, const std::string& iface) noexcept;

    // Seam for tests: an override exercises the full collection path without the real binary
    // installed. It also owns the availability check, so a test never needs one.
    virtual std::vector<std::string> run_ethtool(const std::string& iface);

   private:
    Registry* registry_;
    std::unordered_map<std::string, std::string> net_tags_;
    std::string path_prefix_;
};
}  // namespace atlasagent
