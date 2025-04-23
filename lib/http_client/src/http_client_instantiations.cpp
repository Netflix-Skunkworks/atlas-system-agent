#include "http_client.h"
#include <lib/tagging/src/tagging_registry.h>
#include <lib/spectator/registry.h>

// Include the template implementations
#include "http_client.cpp"

// Explicitly instantiate the template classes for the specific types used in the application
namespace atlasagent {
    // Instantiate for SpectatordRegistry
    template class HttpClient<spectator::SpectatordRegistry>;
    
    // Instantiate for base_tagging_registry<SpectatordRegistry> (TaggingRegistry)
    template class HttpClient<base_tagging_registry<spectator::SpectatordRegistry>>;
} // namespace atlasagent