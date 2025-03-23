#include "service_monitor.h"
#include "util.h"
#include <regex>


template class ServiceMonitor<atlasagent::TaggingRegistry>;


// Attempt to create a regex object with the provided pattern
// If no exception is thrown, it's a valid regex
bool is_valid_regex(const std::string& pattern) {
    try {
        std::regex regex(pattern);
        return true;  
    } catch (const std::regex_error& e) {
        return false;
    }
}

std::optional<std::vector<std::string>> parse_service_monitor_config(const char* configPath)
{
    std::optional<std::vector<std::string>> config = atlasagent::read_file(configPath);
    // If the reading the config fails or the config is empty return
    if (config.has_value() == false || config.value().empty() == true){
        return std::nullopt;
    }

    for (const auto& regex_pattern : config.value()) {
        if (is_valid_regex(regex_pattern) == false) {
            return std::nullopt;
        }
    }
    return config.value();
}


template <class Reg>
bool ServiceMonitor<Reg>::gather_metrics(){
    return true;
}
