#pragma once
#include <dbus/dbus.h>
#include <string>
#include <vector>
#include <optional>
#include <memory>
#include <iostream>

// Structure to hold unit information
struct UnitInfo
{
    std::string name;
    std::string description;
    std::string loadState;
    std::string activeState;
    std::string subState;
    std::string followingUnit;
    std::string objectPath;
    dbus_uint32_t jobId;
    std::string jobType;
    std::string jobObjectPath;
    pid_t pid{-1};
};

struct ProcessesInfo
{
    std::string controlGroupPath;
    pid_t pid;
    std::string binaryPath;
};

// Will rename this struct
struct DBusConstants
{
    static constexpr auto service = "org.freedesktop.systemd1";
    static constexpr auto path = "/org/freedesktop/systemd1";
    static constexpr auto interface = "org.freedesktop.systemd1.Manager";
    static constexpr auto MethodListUnits = "ListUnits";
    static constexpr auto MethodGetUnitProcesses = "GetUnitProcesses";
    
    
    static constexpr auto pathMainPID = "org/freedesktop/systemd1/unit/";
    static constexpr auto interfaceMainPID = "org.freedesktop.DBus.Properties";
    static constexpr auto MethodGet = "Get";
    
    static constexpr auto MainPid = "MainPID";
    static constexpr auto interfaceService = "org.freedesktop.systemd1.Service";
};




class DBus
{
    public:
    DBus() = default;
    ~DBus();
 
    std::optional<std::vector<UnitInfo>> GetAllServices();
};
