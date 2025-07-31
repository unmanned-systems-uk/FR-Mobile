#pragma once

#include "main.h"
#include <functional>

// Base scanner interface
class IScanner
{
public:
    virtual ~IScanner() = default;
    virtual bool initialize() = 0;
    virtual bool startScan() = 0;
    virtual bool stopScan() = 0;
    virtual std::vector<ProbeRequest> getResults() = 0;
    virtual void cleanup() = 0;
};

// Base hardware interface
class IHardwareInterface
{
public:
    virtual ~IHardwareInterface() = default;
    virtual bool initialize() = 0;
    virtual bool isConnected() = 0;
    virtual void reset() = 0;
    virtual void cleanup() = 0;
};

// Timer callback interface
class ITimerCallback
{
public:
    virtual ~ITimerCallback() = default;
    virtual void onTimerExpired() = 0;
};

// Data storage interface
class IDataStorage
{
public:
    virtual ~IDataStorage() = default;
    virtual bool writeData(const ProbeRequest &data, const std::string &filename) = 0;
    virtual bool writeAssetData(const AssetInfo &data) = 0;
    virtual std::vector<std::string> readFile(const std::string &filename) = 0;
    virtual bool fileExists(const std::string &filename) = 0;
    virtual size_t getFileSize(const std::string &filename) = 0;
    virtual float getRemainingCapacityPercent() = 0;
};

// Network interface
class INetworkInterface
{
public:
    virtual ~INetworkInterface() = default;
    virtual bool connect() = 0;
    virtual bool disconnect() = 0;
    virtual bool isConnected() = 0;
    virtual bool sendData(const std::string &data) = 0;
    virtual std::string getNetworkTime() = 0;
};

// Power management interface
class IPowerManager
{
public:
    virtual ~IPowerManager() = default;
    virtual void enablePeripherals() = 0;
    virtual void disablePeripherals() = 0;
    virtual void enterSleep(uint64_t sleepTimeUs) = 0;
    virtual WakeupCause getWakeupCause() = 0;
    virtual void configureWakeup(uint64_t timeUs) = 0;
};

// Time management interface
class ITimeManager
{
public:
    virtual ~ITimeManager() = default;
    virtual std::string getCurrentDateTime() = 0;
    virtual bool setTimeFromNetwork(const std::string &networkTime) = 0;
    virtual bool isValidTime(const std::string &timeStr) = 0;
    virtual uint64_t getNightSleepDuration(const std::string &currentTime) = 0;
};

// Utility functions
class Utils
{
public:
    static std::string escapeJSONString(const std::string &input);
    static long convertToTotalMinutes(const std::string &dateTimeStr);
    static bool isTimeDifferenceGreaterThan(const std::string &filename, int minutes);
    static std::vector<std::string> split(const std::string &str, char delimiter);
    static std::string trim(const std::string &str);
    static bool isIgnoredMAC(const std::string &mac);
    static std::string getCurrentTimestamp();

private:
    static const std::vector<std::string> IGNORED_MACS;
};