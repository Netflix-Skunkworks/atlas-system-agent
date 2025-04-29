#include <lib/collectors/ebs/src/ebs.h>
#include <gtest/gtest.h>
#include <unordered_set>

TEST(ebs, a) {
    // Create a test registry and config set
    spectator::TestRegistry registry;
    std::unordered_set<std::string> config{"/dev/nvme1"};
    
    // Initialize the collector with the registry and device config
    EBSCollector<spectator::TestRegistry> collector(&registry, config);
    
    // Test gathering metrics
    collector.gather_metrics();
}