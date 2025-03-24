#include "dbus.h"
//#include "debug.h"
#include <iostream>

// Helper macros for common types
#define EXTRACT_STRING(iter, val) extract(iter, val, DBUS_TYPE_STRING)
#define EXTRACT_OBJPATH(iter, val) extract(iter, val, DBUS_TYPE_OBJECT_PATH)
#define EXTRACT_UINT32(iter, val) extract(iter, val, DBUS_TYPE_UINT32)

DBus::~DBus()
{
    std::cout << "Shutting down";
    dbus_shutdown();
}

void DBusErrorDeleter(DBusError* error)
{
    if (error != nullptr) 
    {
        dbus_error_free(error);
    }
    delete error;
}

std::unique_ptr<DBusError, decltype(&DBusErrorDeleter)> CreateDBusError() 
{
    auto error = std::unique_ptr<DBusError, decltype(&DBusErrorDeleter)>(new DBusError, DBusErrorDeleter);
    dbus_error_init(error.get());
    return error;
}

void DBusConnectionDeleter(DBusConnection* connection)
{
    if (connection != nullptr)
    {
        dbus_connection_close(connection);
        dbus_connection_unref(connection);
    }
}

std::unique_ptr<DBusConnection, decltype(&DBusConnectionDeleter)> CreateDBusConnection(DBusBusType bus_type)
{
    auto errorPtr = CreateDBusError();
    std::unique_ptr<DBusConnection, decltype(&DBusConnectionDeleter)> connection(dbus_bus_get_private(bus_type, errorPtr.get()), DBusConnectionDeleter);
    if (dbus_error_is_set(errorPtr.get()))
    {
        std::cerr << "Connection failed: " << errorPtr->message << std::endl;
        return std::unique_ptr<DBusConnection, decltype(&DBusConnectionDeleter)>(nullptr, DBusConnectionDeleter);
    }

    if (connection == nullptr)
    {
        std::cerr << "Failed to connect to D-Bus" << std::endl;
        return std::unique_ptr<DBusConnection, decltype(&DBusConnectionDeleter)>(nullptr, DBusConnectionDeleter);
    }

    return connection;

}

void DBusMessageDeleter(DBusMessage* msg) 
{
    if (msg != nullptr) 
    {
        dbus_message_unref(msg);
    }
}

std::unique_ptr<DBusMessage, decltype(&DBusMessageDeleter)> CreateDBusMessage(const char* destination, const char* path, const char* interface, const char* method) 
{
    DBusMessage* msg = dbus_message_new_method_call(destination, path, interface, method);    
    return std::unique_ptr<DBusMessage, decltype(&DBusMessageDeleter)>(msg, DBusMessageDeleter);
}


std::unique_ptr<DBusMessage, decltype(&DBusMessageDeleter)> SendMessageAndBlock(DBusConnection &connection, DBusMessage &message)
{
    auto errorPtr = CreateDBusError();
    std::unique_ptr<DBusMessage, decltype(&DBusMessageDeleter)> reply_ptr(dbus_connection_send_with_reply_and_block(&connection, &message, DBUS_TIMEOUT_USE_DEFAULT, errorPtr.get()), DBusMessageDeleter);

    if (dbus_error_is_set(errorPtr.get()))
    {
        std::cerr << "Error sending message: " << errorPtr->message << std::endl;
        return std::unique_ptr<DBusMessage, decltype(&DBusMessageDeleter)>(nullptr, DBusMessageDeleter);
    }

    if (reply_ptr == nullptr)
    {
        std::cerr << "Received null reply" << std::endl;
        return std::unique_ptr<DBusMessage, decltype(&DBusMessageDeleter)>(nullptr, DBusMessageDeleter);
    }

    if (dbus_message_get_type(reply_ptr.get()) != DBUS_MESSAGE_TYPE_METHOD_RETURN)
    {
        std::cerr << "Received unexpected reply type" << std::endl;
        return std::unique_ptr<DBusMessage, decltype(&DBusMessageDeleter)>(nullptr, DBusMessageDeleter);
    }

    return reply_ptr;
}


// Generic extraction function with error handling
template<typename T>
bool extract(DBusMessageIter *iter, T &value, int expectedType) 
{
    if (dbus_message_iter_get_arg_type(iter) != expectedType) {
        std::cerr << "Expected type " << expectedType << ", got: " 
                  << dbus_message_iter_get_arg_type(iter) << std::endl;
        return false;
    }
    
    dbus_message_iter_get_basic(iter, &value);
    dbus_message_iter_next(iter);
    return true;
}

// String specialization
template<>
bool extract<std::string>(DBusMessageIter *iter, std::string &value, int expectedType) 
{
    if (dbus_message_iter_get_arg_type(iter) != expectedType) {
        std::cerr << "Expected type " << expectedType << ", got: " 
                  << dbus_message_iter_get_arg_type(iter) << std::endl;
        return false;
    }
    
    const char *str;
    dbus_message_iter_get_basic(iter, &str);
    value = str;
    dbus_message_iter_next(iter);
    return true;
}

bool extractUnitInfo(DBusMessageIter *structIter, UnitInfo &unit)
{
    return EXTRACT_STRING(structIter, unit.name) &&
           EXTRACT_STRING(structIter, unit.description) &&
           EXTRACT_STRING(structIter, unit.loadState) &&
           EXTRACT_STRING(structIter, unit.activeState) &&
           EXTRACT_STRING(structIter, unit.subState) &&
           EXTRACT_STRING(structIter, unit.followingUnit) &&
           EXTRACT_OBJPATH(structIter, unit.objectPath) &&
           EXTRACT_UINT32(structIter, unit.jobId) &&
           EXTRACT_STRING(structIter, unit.jobType) &&
           EXTRACT_OBJPATH(structIter, unit.jobObjectPath);   
}

std::optional<std::vector<UnitInfo>> ParseListUnitsReply(DBusMessage &reply)
{
    std::vector<UnitInfo> units;
    DBusMessageIter iter, arrayIter, structIter;
    if (!dbus_message_iter_init(&reply, &iter) || dbus_message_iter_get_arg_type(&iter) != DBUS_TYPE_ARRAY) 
    {
        std::cerr << "Invalid reply: " << (!dbus_message_iter_init(&reply, &iter) ? "no arguments" : "not an array") << std::endl;
        return std::nullopt;
    }

    // Process array elements
    for (dbus_message_iter_recurse(&iter, &arrayIter); dbus_message_iter_get_arg_type(&arrayIter) != DBUS_TYPE_INVALID; dbus_message_iter_next(&arrayIter)) 
    {
    
        if (dbus_message_iter_get_arg_type(&arrayIter) != DBUS_TYPE_STRUCT)
            continue;
    
        dbus_message_iter_recurse(&arrayIter, &structIter);
    
        UnitInfo unit;
        if (extractUnitInfo(&structIter, unit))
            units.push_back(unit);
    }
    return units;
}



std::optional<std::vector<UnitInfo>> DBus::GetAllServices()
{
    // Create a DBus Connection
    auto connection = CreateDBusConnection(DBUS_BUS_SYSTEM);
    if (connection == nullptr)
    {
        return std::nullopt;
    }

    // Create a DBus Message
    auto message = CreateDBusMessage(DBusConstants::service, DBusConstants::path, DBusConstants::interface, DBusConstants::MethodListUnits);
    if (message == nullptr)
    {
        return std::nullopt;
    }

    auto reply = SendMessageAndBlock(*connection, *message); 
    if (reply == nullptr)
    {
        return std::nullopt;
    }

    auto units = ParseListUnitsReply(*reply);

    if (units.has_value() == false)
    {
        return std::nullopt;
    }
    //PrintUnitMetrics(units.value());

    return units;
}