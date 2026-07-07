#pragma once

#include <optional>
#include <regex>
#include <sdbus-c++/sdbus-c++.h>
#include <string>
#include <unordered_map>
#include <vector>

// Define the Unit structure matching the D-Bus signature (ssssssouso)
using Unit = sdbus::Struct<std::string, std::string, std::string, std::string, std::string, std::string,
                           sdbus::ObjectPath, uint32_t, std::string, sdbus::ObjectPath>;

struct DBusConstants
{
    // Service and path constants
    static constexpr auto Service = "org.freedesktop.systemd1";
    static constexpr auto Path = "/org/freedesktop/systemd1";

    // Manager interface constants
    static constexpr auto Interface = "org.freedesktop.systemd1.Manager";
    static constexpr auto MethodListUnits = "ListUnits";
    static constexpr auto MethodGetUnit = "GetUnit";

    // Properties interface constants
    static constexpr auto PropertiesInterface = "org.freedesktop.DBus.Properties";
    static constexpr auto MethodGet = "Get";

    // Unit interface constants
    static constexpr auto UnitInterface = "org.freedesktop.systemd1.Unit";
    static constexpr auto PropertyActiveState = "ActiveState";
    static constexpr auto PropertySubState = "SubState";
    static constexpr auto PropertyControlGroup = "ControlGroup";

    // Service interface constants
    static constexpr auto ServiceInterface = "org.freedesktop.systemd1.Service";
    static constexpr auto PropertyMainPID = "MainPID";
};

using ExtraTags = std::unordered_map<std::string, std::string>;

struct ServiceMonitorUtilConstants
{
    static constexpr auto ProcPath{"/proc"};
    static constexpr auto StatPath{"stat"};
    static constexpr auto FdPath{"fd"};
    static constexpr auto ConfigFileExtPattern = ".*\\.systemd-unit$";
    static constexpr auto Active{"active"};
    static constexpr auto Running{"running"};
    // 0-based indices into the /proc/[pid]/stat tokens AFTER the comm field (i.e. starting at
    // field 3, state). comm (field 2) can contain spaces and ')', so it must be parsed separately;
    // man-page field F maps to index F-3: utime(14)->11, stime(15)->12, rss(24)->21.
    static constexpr auto UTimeIndex{11};
    static constexpr auto STimeIndex{12};
    static constexpr auto RssIndex{21};

    // cgroup-v2 paths and keys
    static constexpr auto CgroupBasePath{"/sys/fs/cgroup"};
    static constexpr auto CpuStatFile{"cpu.stat"};
    static constexpr auto MemoryCurrentFile{"memory.current"};
    static constexpr auto CgroupProcsFile{"cgroup.procs"};
    static constexpr auto CpuUsageKey{"usage_usec"};
};

struct ProcessTimes
{
    unsigned long uTime{};
    unsigned long sTime{};
};

struct ServiceProperties
{
    std::string name;
    std::string activeState;
    std::string subState;
    unsigned int mainPid;
    std::string controlGroup;
};

// DBus Functions
std::optional<std::vector<Unit>> list_all_units();
std::optional<ServiceProperties> get_service_properties(const std::string& serviceName);

// Config Parsing Functions
std::optional<std::vector<std::regex>> parse_service_monitor_config_directory(const char* directoryPath);

// Extra tag support: reads key=value lines from the service's .systemd-unit config file.
ExtraTags read_service_extra_tags(const std::string& serviceName);

// Process (per-PID) metric functions
std::optional<unsigned long> get_rss(const unsigned int& pid);
std::optional<unsigned int> get_number_fds(const unsigned int& pid);
std::optional<ProcessTimes> get_process_times(const unsigned int& pid);

// cgroup-v2 metric functions
// Returns nullopt when the usage_usec key is absent or unparseable (distinct from a real 0).
std::optional<unsigned long long> parse_cgroup_cpu_stat(const std::vector<std::string>& lines);
std::optional<unsigned long long> get_cgroup_cpu_usage(const std::string& cgroupPath);
std::optional<unsigned long long> get_cgroup_memory(const std::string& cgroupPath);
std::optional<std::vector<unsigned int>> get_cgroup_pids(const std::string& cgroupPath);
std::optional<unsigned long> get_total_fds(const std::string& cgroupPath);
