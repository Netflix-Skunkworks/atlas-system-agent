#include <filesystem>

#include <absl/strings/str_split.h>
#include "service_monitor_utils.h"
#include <lib/util/src/util.h>

std::optional<std::vector<Unit>> list_all_units()
try
{
    auto connection = sdbus::createSystemBusConnection();
    auto proxy = sdbus::createProxy(*connection, sdbus::ServiceName{DBusConstants::Service},
                                    sdbus::ObjectPath{DBusConstants::Path});

    std::vector<Unit> units{};
    proxy->callMethod(sdbus::MethodName{DBusConstants::MethodListUnits})
        .onInterface(sdbus::InterfaceName{DBusConstants::Interface})
        .storeResultsTo(units);
    return units;
}
catch (const sdbus::Error& e)
{
    atlasagent::Logger()->error("D-Bus Exception: {} with message: {}", e.getName(), e.getMessage());
    return std::nullopt;
}
catch (const std::exception& e)
{
    atlasagent::Logger()->error("list_all_units exception: {}", e.what());
    return std::nullopt;
}

std::optional<ServiceProperties> get_service_properties(const std::string& serviceName)
try
{
    auto connection = sdbus::createSystemBusConnection();

    auto managerProxy = sdbus::createProxy(*connection, sdbus::ServiceName{DBusConstants::Service},
                                           sdbus::ObjectPath{DBusConstants::Path});

    sdbus::ObjectPath unitObjectPath;
    managerProxy->callMethod(DBusConstants::MethodGetUnit)
        .onInterface(DBusConstants::Interface)
        .withArguments(serviceName)
        .storeResultsTo(unitObjectPath);

    auto proxy = sdbus::createProxy(*connection, sdbus::ServiceName{DBusConstants::Service}, unitObjectPath);

    sdbus::Variant mainPidVariant;
    proxy->callMethod(DBusConstants::MethodGet)
        .onInterface(DBusConstants::PropertiesInterface)
        .withArguments(DBusConstants::ServiceInterface, DBusConstants::PropertyMainPID)
        .storeResultsTo(mainPidVariant);

    sdbus::Variant activeStateVariant;
    proxy->callMethod(DBusConstants::MethodGet)
        .onInterface(DBusConstants::PropertiesInterface)
        .withArguments(DBusConstants::UnitInterface, DBusConstants::PropertyActiveState)
        .storeResultsTo(activeStateVariant);

    sdbus::Variant subStateVariant;
    proxy->callMethod(DBusConstants::MethodGet)
        .onInterface(DBusConstants::PropertiesInterface)
        .withArguments(DBusConstants::UnitInterface, DBusConstants::PropertySubState)
        .storeResultsTo(subStateVariant);

    sdbus::Variant controlGroupVariant;
    proxy->callMethod(DBusConstants::MethodGet)
        .onInterface(DBusConstants::PropertiesInterface)
        .withArguments(DBusConstants::ServiceInterface, DBusConstants::PropertyControlGroup)
        .storeResultsTo(controlGroupVariant);

    return ServiceProperties{
        serviceName,
        activeStateVariant.get<std::string>(),
        subStateVariant.get<std::string>(),
        mainPidVariant.get<uint32_t>(),
        controlGroupVariant.get<std::string>(),
    };
}
catch (const sdbus::Error& e)
{
    atlasagent::Logger()->error("D-Bus Exception: {} with message: {}", e.getName(), e.getMessage());
    return std::nullopt;
}
catch (const std::exception& e)
{
    atlasagent::Logger()->error("get_service_properties exception: {}", e.what());
    return std::nullopt;
}

std::optional<std::vector<std::regex>> parse_regex_config_file(const char* configFilePath)
try
{
    std::optional<std::vector<std::string>> stringPatterns = atlasagent::read_file(configFilePath);
    if (stringPatterns.has_value() == false)
    {
        atlasagent::Logger()->error("Error reading config file {}", configFilePath);
        return std::nullopt;
    }

    if (stringPatterns.value().empty())
    {
        atlasagent::Logger()->debug("Empty config file {}", configFilePath);
        return std::nullopt;
    }

    std::vector<std::regex> regexPatterns{};
    for (const auto& regex_pattern : stringPatterns.value())
    {
        if (regex_pattern.empty() || regex_pattern.find('=') != std::string::npos)
        {
            continue;
        }
        try
        {
            regexPatterns.emplace_back(regex_pattern);
        }
        catch (const std::regex_error& e)
        {
            atlasagent::Logger()->error("Exception: {}, for regex:{}, in config file {}", e.what(), regex_pattern,
                                        configFilePath);
            return std::nullopt;
        }
    }
    return regexPatterns;
}
catch (const std::exception& e)
{
    atlasagent::Logger()->error("Exception: {} in parse_regex_config_file", e.what());
    return std::nullopt;
}

std::optional<std::vector<std::regex>> parse_service_monitor_config_directory(const char* directoryPath)
try
{
    if (std::filesystem::exists(directoryPath) == false || std::filesystem::is_directory(directoryPath) == false)
    {
        atlasagent::Logger()->error("Invalid service monitor config directory {}", directoryPath);
        return std::nullopt;
    }

    std::regex configFileExtPattern(ServiceMonitorUtilConstants::ConfigFileExtPattern);
    std::vector<std::regex> allRegexPatterns{};

    for (const auto& file : std::filesystem::recursive_directory_iterator(directoryPath))
    {
        if (std::regex_match(file.path().filename().string(), configFileExtPattern) == false)
        {
            continue;
        }

        auto regexExpressions = parse_regex_config_file(file.path().c_str());
        if (regexExpressions.has_value() == false)
        {
            atlasagent::Logger()->error("Could not add regex expressions from file {}", file.path().c_str());
            continue;
        }
        allRegexPatterns.insert(allRegexPatterns.end(), regexExpressions.value().begin(),
                                regexExpressions.value().end());
    }

    if (allRegexPatterns.empty())
    {
        atlasagent::Logger()->info("No service monitor regex patterns found in directory {}", directoryPath);
        return std::nullopt;
    }

    return allRegexPatterns;
}
catch (const std::exception& e)
{
    atlasagent::Logger()->error("Exception: {} in parse_service_monitor_config_directory", e.what());
    return std::nullopt;
}

// ── Extra tags ──────────────────────────────────────────────────────────────

ExtraTags read_service_extra_tags(const std::string& serviceName)
{
    // Derive the config filename from the service name: strip ".service", look for the matching
    // .systemd-unit file in conf.d/. Lines containing '=' are treated as key=value tags; lines
    // without '=' are regex patterns (handled by parse_regex_config_file) and skipped here.
    auto name = serviceName;
    const std::string suffix = ".service";
    if (name.size() > suffix.size() && name.compare(name.size() - suffix.size(), suffix.size(), suffix) == 0)
    {
        name.erase(name.size() - suffix.size());
    }

    auto configPath = std::filesystem::path(ServiceMonitorConstants::ConfigPath) / (name + ".systemd-unit");

    ExtraTags tags;
    auto lines = atlasagent::read_file(configPath.string().c_str());
    if (!lines)
    {
        return tags;
    }

    for (const auto& line : lines.value())
    {
        if (line.empty())
        {
            continue;
        }
        auto pos = line.find('=');
        if (pos == std::string::npos || pos == 0)
        {
            continue;
        }
        tags.emplace(line.substr(0, pos), line.substr(pos + 1));
    }
    return tags;
}

// ── Process (per-PID) helpers ────────────────────────────────────────────────

static std::optional<std::vector<std::string>> get_proc_fields(const unsigned int& pid)
{
    std::filesystem::path procPath = std::filesystem::path(ServiceMonitorUtilConstants::ProcPath) /
                                     std::to_string(pid) / ServiceMonitorUtilConstants::StatPath;
    return atlasagent::read_file(procPath.string().c_str());
}

// Tokenize the fields of /proc/[pid]/stat that follow the comm field. comm (field 2) is the
// process name wrapped in parentheses and may itself contain spaces and ')', so splitting the
// whole line on spaces would shift every later field. Per proc(5), parse from the LAST ')': the
// returned tokens start at field 3 (state) at index 0, so man-page field F is at index F-3.
static std::optional<std::vector<std::string>> tokenize_post_comm(const std::string& statLine)
{
    auto rparen = statLine.rfind(')');
    if (rparen == std::string::npos)
    {
        atlasagent::Logger()->error("Malformed proc stat line, no ')' found: {}", statLine);
        return std::nullopt;
    }
    std::vector<std::string> tokens = absl::StrSplit(statLine.substr(rparen + 1), ' ', absl::SkipWhitespace());
    return tokens;
}

std::optional<ProcessTimes> parse_process_times(const std::vector<std::string>& pidStats)
try
{
    auto statTokens = tokenize_post_comm(pidStats.at(0));
    if (statTokens.has_value() == false)
    {
        return std::nullopt;
    }

    if (statTokens->size() <= ServiceMonitorUtilConstants::STimeIndex)
    {
        atlasagent::Logger()->error("Not enough tokens in proc stat file. Expected at least {}, got {}",
                                    ServiceMonitorUtilConstants::STimeIndex + 1, statTokens->size());
        return std::nullopt;
    }

    unsigned long uTime = std::stoul((*statTokens)[ServiceMonitorUtilConstants::UTimeIndex]);
    unsigned long sTime = std::stoul((*statTokens)[ServiceMonitorUtilConstants::STimeIndex]);
    return ProcessTimes{uTime, sTime};
}
catch (const std::exception& e)
{
    atlasagent::Logger()->error("Exception: {} in parse_process_times", e.what());
    return std::nullopt;
}

std::optional<ProcessTimes> get_process_times(const unsigned int& pid)
{
    auto pidStats = get_proc_fields(pid);
    if (pidStats.has_value() == false)
    {
        return std::nullopt;
    }
    return parse_process_times(pidStats.value());
}

std::optional<unsigned long> parse_rss(const std::vector<std::string>& pidStats)
try
{
    auto statTokens = tokenize_post_comm(pidStats.at(0));
    if (statTokens.has_value() == false)
    {
        return std::nullopt;
    }
    if (statTokens->size() <= ServiceMonitorUtilConstants::RssIndex)
    {
        atlasagent::Logger()->error("Not enough tokens in proc stat file. Expected at least {}, got {}",
                                    ServiceMonitorUtilConstants::RssIndex + 1, statTokens->size());
        return std::nullopt;
    }

    return std::stoul((*statTokens)[ServiceMonitorUtilConstants::RssIndex]);
}
catch (const std::exception& e)
{
    atlasagent::Logger()->error("Exception: {} in parse_rss", e.what());
    return std::nullopt;
}

std::optional<unsigned long> get_rss(const unsigned int& pid)
{
    auto pidStats = get_proc_fields(pid);
    if (pidStats.has_value() == false)
    {
        return std::nullopt;
    }
    return parse_rss(pidStats.value());
}

std::optional<unsigned int> get_number_fds(const unsigned int& pid)
try
{
    auto path = std::filesystem::path(ServiceMonitorUtilConstants::ProcPath) / std::to_string(pid) /
                ServiceMonitorUtilConstants::FdPath;

    int fd_count = 0;
    for (const auto& entry : std::filesystem::directory_iterator(path))
    {
        if (entry.is_symlink())
        {
            ++fd_count;
        }
    }
    return fd_count;
}
catch (const std::exception& e)
{
    atlasagent::Logger()->error("get_number_fds exception: {} ", e.what());
    return std::nullopt;
}

// ── cgroup-v2 helpers ────────────────────────────────────────────────────────

std::optional<unsigned long long> parse_cgroup_cpu_stat(const std::vector<std::string>& lines)
try
{
    for (const auto& line : lines)
    {
        // cpu.stat lines are "<key> <value>". Match the key exactly (a prefix test would also
        // match e.g. "usage_useconds") and require a value token before parsing it.
        std::vector<std::string> tokens = absl::StrSplit(line, ' ', absl::SkipWhitespace());
        if (tokens.size() >= 2 && tokens[0] == ServiceMonitorUtilConstants::CpuUsageKey)
        {
            return std::stoull(tokens[1]);
        }
    }
    return std::nullopt;
}
catch (const std::exception& e)
{
    atlasagent::Logger()->error("Exception: {} in parse_cgroup_cpu_stat", e.what());
    return std::nullopt;
}

static std::filesystem::path cgroup_path(const std::string& cgroupPath, const char* file)
{
    // cgroupPath from D-Bus starts with '/', e.g. "/system.slice/nginx.service"
    auto relative = cgroupPath.starts_with('/') ? cgroupPath.substr(1) : cgroupPath;
    return std::filesystem::path(ServiceMonitorUtilConstants::CgroupBasePath) / relative / file;
}

std::optional<unsigned long long> get_cgroup_cpu_usage(const std::string& cgroupPath)
{
    auto path = cgroup_path(cgroupPath, ServiceMonitorUtilConstants::CpuStatFile);
    auto lines = atlasagent::read_file(path.string().c_str());
    if (lines.has_value() == false || lines.value().empty())
    {
        atlasagent::Logger()->error("Error reading {}", path.string());
        return std::nullopt;
    }
    return parse_cgroup_cpu_stat(lines.value());
}

std::optional<unsigned long long> get_cgroup_memory(const std::string& cgroupPath)
try
{
    auto path = cgroup_path(cgroupPath, ServiceMonitorUtilConstants::MemoryCurrentFile);
    auto lines = atlasagent::read_file(path.string().c_str());
    if (lines.has_value() == false || lines.value().empty())
    {
        atlasagent::Logger()->error("Error reading {}", path.string());
        return std::nullopt;
    }
    return std::stoull(lines.value().at(0));
}
catch (const std::exception& e)
{
    atlasagent::Logger()->error("Exception: {} in get_cgroup_memory", e.what());
    return std::nullopt;
}

std::optional<std::vector<unsigned int>> get_cgroup_pids(const std::string& cgroupPath)
try
{
    auto path = cgroup_path(cgroupPath, ServiceMonitorUtilConstants::CgroupProcsFile);
    auto lines = atlasagent::read_file(path.string().c_str());
    if (lines.has_value() == false)
    {
        atlasagent::Logger()->error("Error reading {}", path.string());
        return std::nullopt;
    }

    std::vector<unsigned int> pids{};
    for (const auto& line : lines.value())
    {
        if (line.empty())
        {
            continue;
        }
        pids.emplace_back(std::stoul(line));
    }
    return pids;
}
catch (const std::exception& e)
{
    atlasagent::Logger()->error("Exception: {} in get_cgroup_pids", e.what());
    return std::nullopt;
}

std::optional<unsigned long> get_total_fds(const std::string& cgroupPath)
{
    auto pids = get_cgroup_pids(cgroupPath);
    if (pids.has_value() == false)
    {
        return std::nullopt;
    }

    unsigned long total = 0;
    for (const auto& pid : pids.value())
    {
        auto fds = get_number_fds(pid);
        if (fds.has_value())
        {
            total += fds.value();
        }
        else
        {
            atlasagent::Logger()->debug("Could not get fd count for pid {}, skipping", pid);
        }
    }
    return total;
}
