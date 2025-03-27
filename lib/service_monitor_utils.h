#pragma once

#include <optional>
#include <regex>
#include <sdbus-c++/sdbus-c++.h>
#include <unordered_set>
#include <vector>

// Define the Unit structure matching the D-Bus signature (ssssssouso)
using Unit =
    sdbus::Struct<std::string, std::string, std::string, std::string, std::string, std::string,
                  sdbus::ObjectPath, uint32_t, std::string, sdbus::ObjectPath>;

struct DBusConstants {
    // Service and path constants
    static constexpr auto service = "org.freedesktop.systemd1";
    static constexpr auto path = "/org/freedesktop/systemd1";
  
    // Manager interface constants
    static constexpr auto interface = "org.freedesktop.systemd1.Manager";
    static constexpr auto MethodListUnits = "ListUnits";
    static constexpr auto MethodGetUnit = "GetUnit";
  
    // Properties interface constants
    static constexpr auto propertiesInterface = "org.freedesktop.DBus.Properties";
    static constexpr auto MethodGet = "Get";
  
    // Unit interface constants
    static constexpr auto unitInterface = "org.freedesktop.systemd1.Unit";
    static constexpr auto PropertyActiveState = "ActiveState";
    static constexpr auto PropertyLoadState = "LoadState";
    static constexpr auto PropertySubState = "SubState";
  
    // Service interface constants
    static constexpr auto serviceInterface = "org.freedesktop.systemd1.Service";
    static constexpr auto PropertyMainPID = "MainPID";
  };

struct ServiceMonitorUtilConstants {
    static constexpr auto uTimeIndex{13};
    static constexpr auto sTimeIndex{14};
    static constexpr auto rssIndex{23};
    static constexpr auto ProcStatPath{"/proc/stat"};
    static constexpr auto CpuInfoPath{"/proc/cpuinfo"};
    static constexpr auto AggregateCpuIndex{0};
    static constexpr unsigned int AggregateCpuDataIndex{1};
    static constexpr auto ProcPath{"/proc"};
    static constexpr auto StatPath{"stat"};
    static constexpr auto FdPath{"fd"};
    static constexpr auto Processor{"processor"};
    static constexpr auto ConfigFileExtPattern = ".*\\.systemd-unit$";  // Fix the regex pattern
};


struct ProcessTimes {
    unsigned long uTime{};
    unsigned long sTime{};
};

struct ServiceProperties{
    std::string name;
    std::string activeState;
    std::string subState;
    unsigned int mainPid;
};

// DBus Functions
std::optional<std::vector<Unit>> list_all_units();
std::optional<ServiceProperties> get_service_properties(const std::string& serviceName);

// Config Parsing Functions
std::optional<std::vector<std::regex>> parse_service_monitor_config_directory(const char* directoryPath);

// Metrics Functions
std::optional<unsigned long> get_rss(pid_t pid);
std::optional<unsigned long long> get_total_cpu_time();
std::optional<unsigned int> get_number_fds(pid_t pid);
double calculate_cpu_usage(unsigned long long oldCpuTime, unsigned long long newCpuTime, ProcessTimes oldProcessTime, ProcessTimes newProcessTime, unsigned int numCores);
std::optional<unsigned int> get_cpu_cores();
std::unordered_map<pid_t, ProcessTimes> create_pid_map(const std::vector<ServiceProperties>& services);