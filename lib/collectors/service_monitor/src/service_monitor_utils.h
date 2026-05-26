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

    // Service interface constants
    static constexpr auto ServiceInterface = "org.freedesktop.systemd1.Service";
    static constexpr auto PropertyMainPID = "MainPID";
    static constexpr auto PropertyControlGroup = "ControlGroup";
};

struct ServiceMonitorUtilConstants
{
    static constexpr auto UTimeIndex{13};
    static constexpr auto STimeIndex{14};
    static constexpr auto RssIndex{23};
    static constexpr auto ProcStatPath{"/proc/stat"};
    static constexpr auto CpuInfoPath{"/sys/devices/system/cpu/possible"};
    static constexpr auto AggregateCpuIndex{0};
    static constexpr unsigned int AggregateCpuDataIndex{1};
    static constexpr auto ProcPath{"/proc"};
    static constexpr auto StatPath{"stat"};
    static constexpr auto FdPath{"fd"};
    static constexpr auto ConfigFileExtPattern = ".*\\.systemd-unit$";
    static constexpr auto DefaultCoreCount{1};
    static constexpr auto Active{"active"};
    static constexpr auto Running{"running"};
    static constexpr auto CgroupRoot{"/sys/fs/cgroup"};
    static constexpr auto CgroupCpuStatFile{"cpu.stat"};
    static constexpr auto CgroupCpuUsageUsecKey{"usage_usec"};
    static constexpr unsigned long long MicrosPerSecond{1000000ULL};
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

// Metrics Functions
std::optional<unsigned long> get_rss(const unsigned int& pid);
std::optional<unsigned long long> get_total_cpu_time();
std::optional<unsigned int> get_number_fds(const unsigned int& pid);
double calculate_cpu_usage(const unsigned long long& oldCpuTime, const unsigned long long& newCpuTime,
                           const ProcessTimes& oldProcessTime, const ProcessTimes& newProcessTime,
                           const unsigned int& numCores);
std::optional<unsigned int> get_cpu_cores();
std::unordered_map<unsigned int, ProcessTimes> create_pid_map(const std::vector<ServiceProperties>& services);

// Cgroup CPU helpers. The cgroup CPU sampler reads `usage_usec` from cpu.stat in the unit's cgroup,
// which sums CPU time across every process and thread under that cgroup — not just the main PID.
std::optional<unsigned long long> get_cgroup_cpu_usec(const std::string& cgroupRoot,
                                                     const std::string& cgroupRelPath);
double calculate_cgroup_cpu_usage(unsigned long long oldUsec, unsigned long long newUsec,
                                  double intervalSeconds);