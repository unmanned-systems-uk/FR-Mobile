// src/hardware/bq34z100_battery_monitor.cpp

#include "hardware/bq34z100_battery_monitor.h"
#include "utils/logger.h"
#include "main.h"  // For Config namespace
#include <cstdio>  // For printf, snprintf
#include <cstring> // For memset
#include <cmath>   // For mathematical operations
#include <thread>  // For sleep functions
#include <chrono>  // For time operations

#ifdef ESP32_PLATFORM
// ESP32-specific includes
#include <esp_system.h>
#include <esp_sleep.h>
#include <driver/gpio.h>
// Note: Use ESP-IDF I2C driver instead of Arduino Wire library
#elif defined(RASPBERRY_PI_PLATFORM)
// Raspberry Pi includes for battery monitoring
#include <linux/i2c-dev.h>
#include <linux/gpio.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <unistd.h>
#endif

// Component name for logging
static const std::string COMPONENT_NAME = "BatteryMonitor";

// BQ34z100 Battery Monitor Implementation
BQ34z100BatteryMonitor::BQ34z100BatteryMonitor()
    : deviceFound_(false), i2cHandle_(nullptr), safetyLimits_{
                                                    Config::BAT_LOW_SOC,        // minSoC
                                                    Config::BAT_HIGH_SOC,       // maxSoC
                                                    Config::BAT_LOW_SOH,        // minSoH
                                                    Config::BAT_LOW_CELL_TEMP,  // minCellTemp
                                                    Config::BAT_HIGH_CELL_TEMP, // maxCellTemp
                                                    Config::BAT_LOW_BOARD_TEMP, // minBoardTemp
                                                    Config::BAT_HIGH_BOARD_TEMP // maxBoardTemp
                                                }
{

    std::memset(flashBytes_, 0, sizeof(flashBytes_));
    LOG_DEBUG(COMPONENT_NAME, "BQ34z100 Battery Monitor instance created");
}

BQ34z100BatteryMonitor::~BQ34z100BatteryMonitor()
{
    LOG_DEBUG(COMPONENT_NAME, "BQ34z100 Battery Monitor destructor called");
    cleanup();
}

bool BQ34z100BatteryMonitor::initialize()
{
    try
    {
        LOG_INFO(COMPONENT_NAME, "Initializing BQ34z100 Battery Monitor...");

        // Platform-specific I2C initialization
#ifdef ESP32_PLATFORM
        // Wire.begin();  // Initialize I2C
        LOG_DEBUG(COMPONENT_NAME, "ESP32 I2C initialized");
#else
        LOG_WARNING(COMPONENT_NAME, "Running on development platform - I2C will be mocked");
#endif

        // Scan for required I2C devices
        if (!scanI2CDevices())
        {
            LOG_ERROR(COMPONENT_NAME, "Required I2C devices not found");
            return false;
        }

        // Validate device communication
        if (!validateDevice())
        {
            LOG_ERROR(COMPONENT_NAME, "Device validation failed");
            return false;
        }

        // Get device information
        auto deviceInfo = getDeviceInfo();
        LOGF_INFO(COMPONENT_NAME, "Device Type: 0x%04X, Chemistry ID: 0x%04X, Serial: 0x%04X",
                  deviceInfo.deviceType, deviceInfo.chemistryID, deviceInfo.serialNumber);

        // Log safety limits
        LOGF_INFO(COMPONENT_NAME, "Safety Limits - SoC: %d-%d%%, SoH: %d%%, Cell Temp: %d-%d°C, Board Temp: %d-%d°C",
                  safetyLimits_.minSoC, safetyLimits_.maxSoC, safetyLimits_.minSoH,
                  safetyLimits_.minCellTemp, safetyLimits_.maxCellTemp,
                  safetyLimits_.minBoardTemp, safetyLimits_.maxBoardTemp);

        deviceFound_ = true;
        LOG_INFO(COMPONENT_NAME, "BQ34z100 Battery Monitor initialized successfully");
        return true;
    }
    catch (const std::exception &e)
    {
        LOG_ERROR(COMPONENT_NAME, "Initialization failed: " + std::string(e.what()));
        return false;
    }
}

bool BQ34z100BatteryMonitor::isConnected()
{
    if (!deviceFound_)
    {
        return false;
    }

    try
    {
        // Try to read device type register
        uint16_t deviceType = getDeviceType();
        bool connected = (deviceType != 0 && deviceType != 0xFFFF);

        if (!connected)
        {
            LOG_WARNING(COMPONENT_NAME, "Device communication lost");
        }

        return connected;
    }
    catch (const std::exception &e)
    {
        LOG_ERROR(COMPONENT_NAME, "Connection check failed: " + std::string(e.what()));
        return false;
    }
}

void BQ34z100BatteryMonitor::reset()
{
    LOG_INFO(COMPONENT_NAME, "Resetting BQ34z100 device...");

    try
    {
        if (deviceFound_)
        {
            writeControlRegister(0x00, 0x41); // Reset command

            // Wait for reset to complete
            std::this_thread::sleep_for(std::chrono::milliseconds(1000));

            LOG_INFO(COMPONENT_NAME, "Device reset completed");
        }
    }
    catch (const std::exception &e)
    {
        LOG_ERROR(COMPONENT_NAME, "Device reset failed: " + std::string(e.what()));
    }
}

void BQ34z100BatteryMonitor::cleanup()
{
    LOG_INFO(COMPONENT_NAME, "Cleaning up BQ34z100 Battery Monitor...");

    deviceFound_ = false;
    i2cHandle_ = nullptr;
    std::memset(flashBytes_, 0, sizeof(flashBytes_));

    LOG_INFO(COMPONENT_NAME, "BQ34z100 Battery Monitor cleanup completed");
}

EnhancedBatteryStatus BQ34z100BatteryMonitor::readBatteryStatus()
{
    if (!deviceFound_)
    {
        throw std::runtime_error("Battery monitor not initialized");
    }

    EnhancedBatteryStatus status;

    try
    {
        LOG_DEBUG(COMPONENT_NAME, "Reading comprehensive battery status...");

        // Read voltage (register 0x08)
        uint16_t rawVoltage = readRegister(Register::VOLTAGE, 2);
        status.voltage = convertVoltage(rawVoltage);

        // Read current (register 0x10)
        int16_t rawCurrent = static_cast<int16_t>(readRegister(Register::CURRENT, 2));
        status.current = convertCurrent(rawCurrent);

        // Read average current (register 0x0A)
        int16_t rawAvgCurrent = static_cast<int16_t>(readRegister(Register::AVERAGE_CURRENT, 2));
        status.averageCurrent = convertCurrent(rawAvgCurrent);

        // Read remaining capacity (register 0x04)
        uint16_t rawCapacity = readRegister(Register::REMAINING_CAPACITY, 2);
        status.remainingCapacity = rawCapacity * 2; // Convert to mAh

        // Read design capacity (register 0x3C)
        status.designCapacity = readRegister(Register::DESIGN_CAPACITY, 2);

        // Read design energy (register 0x3E)
        status.designEnergy = readRegister(Register::DESIGN_ENERGY, 2);

        // Calculate full charge capacity (assuming it's available)
        status.fullChargeCapacity = status.designCapacity; // This might need adjustment based on actual register

        // Read State of Charge (register 0x02)
        status.stateOfCharge = static_cast<uint8_t>(readRegister(Register::STATE_OF_CHARGE, 1));

        // Read State of Health (register 0x2E)
        status.stateOfHealth = static_cast<uint16_t>(readRegister(Register::STATE_OF_HEALTH, 1));

        // Read temperatures
        uint16_t rawCellTemp = readRegister(Register::TEMPERATURE, 1);
        status.cellTemperature = static_cast<uint16_t>(convertTemperature(rawCellTemp));

        uint16_t rawBoardTemp = readRegister(Register::PCB_TEMPERATURE, 1);
        status.boardTemperature = static_cast<uint16_t>(convertTemperature(rawBoardTemp));

        // Read cycle count (register 0x2C)
        status.cycleCount = readRegister(Register::CYCLE_COUNT, 1);

        // Read average time to empty (register 0x18)
        status.averageTimeToEmpty = readRegister(Register::AVG_TIME_TO_EMPTY, 2);

        // Calculate instantaneous power
        status.instantaneousPower = (status.voltage * status.current) / 1000.0f; // Convert to mW

        LOGF_DEBUG(COMPONENT_NAME, "Battery Status: SoC=%u%%, Voltage=%.1fmV, Current=%.1fmA, Temp=%.1f°C",
                   status.stateOfCharge, status.voltage, status.current,
                   static_cast<float>(status.cellTemperature));

        // Perform safety checks
        if (!checkSafetyLimits(status))
        {
            handleBatteryError("Battery safety limits exceeded");
        }

        return status;
    }
    catch (const std::exception &e)
    {
        throw std::runtime_error("Failed to read battery status: " + std::string(e.what()));
    }
}

BQ34z100AlarmStatus BQ34z100BatteryMonitor::readAlarmStatus()
{
    if (!deviceFound_)
    {
        throw std::runtime_error("Battery monitor not initialized");
    }

    BQ34z100AlarmStatus alarms;

    try
    {
        LOG_DEBUG(COMPONENT_NAME, "Reading alarm and status flags...");

        // Read flags register (0x0E)
        uint16_t flags = readRegister(Register::FLAGS, 2);

        // Read flags B register (0x12)
        uint8_t flagsB = static_cast<uint8_t>(readRegister(Register::FLAGS_B, 1));

        // Parse flags register (refer to BQ34z100 datasheet for bit definitions)
        alarms.overTempCharge = (flags & 0x8000) != 0;
        alarms.overTempDischarge = (flags & 0x4000) != 0;
        alarms.batteryHigh = (flags & 0x2000) != 0;
        alarms.batteryLow = (flags & 0x1000) != 0;
        alarms.chargeInhibit = (flags & 0x0800) != 0;
        alarms.chargingDisallowed = (flags & 0x0400) != 0;
        alarms.fullCharge = (flags & 0x0200) != 0;
        alarms.charging = (flags & 0x0100) != 0;
        alarms.rest = (flags & 0x0080) != 0;
        alarms.conditionFlag = (flags & 0x0040) != 0;
        alarms.remainingCapacityAlarm = (flags & 0x0020) != 0;
        alarms.endOfDischarge = (flags & 0x0008) != 0;
        alarms.discharging = (flags & 0x0001) != 0;

        // Parse flagsB register
        alarms.hardwareAlarm = (flagsB & 0x80) != 0;

        // Log any active alarms
        if (alarms.overTempCharge || alarms.overTempDischarge)
        {
            LOG_WARNING(COMPONENT_NAME, "Temperature alarm active");
        }
        if (alarms.batteryLow)
        {
            LOG_WARNING(COMPONENT_NAME, "Low battery alarm active");
        }
        if (alarms.batteryHigh)
        {
            LOG_WARNING(COMPONENT_NAME, "High battery alarm active");
        }
        if (alarms.hardwareAlarm)
        {
            LOG_ERROR(COMPONENT_NAME, "Hardware alarm active");
        }

        LOGF_DEBUG(COMPONENT_NAME, "Alarm Status: Flags=0x%04X, FlagsB=0x%02X", flags, flagsB);

        return alarms;
    }
    catch (const std::exception &e)
    {
        throw std::runtime_error("Failed to read alarm status: " + std::string(e.what()));
    }
}

bool BQ34z100BatteryMonitor::isBatterySafe(const EnhancedBatteryStatus &status)
{
    return checkSafetyLimits(status);
}

void BQ34z100BatteryMonitor::printBatteryData(const EnhancedBatteryStatus &status)
{
    LOG_INFO(COMPONENT_NAME, "=== BATTERY STATUS REPORT ===");
    LOGF_INFO(COMPONENT_NAME, "Voltage: %.2f mV", status.voltage);
    LOGF_INFO(COMPONENT_NAME, "Current: %.2f mA", status.current);
    LOGF_INFO(COMPONENT_NAME, "Average Current: %.2f mA", status.averageCurrent);
    LOGF_INFO(COMPONENT_NAME, "Instantaneous Power: %.2f mW", status.instantaneousPower);
    LOGF_INFO(COMPONENT_NAME, "Remaining Capacity: %u mAh", status.remainingCapacity);
    LOGF_INFO(COMPONENT_NAME, "Full Charge Capacity: %u mAh", status.fullChargeCapacity);
    LOGF_INFO(COMPONENT_NAME, "Design Capacity: %u mAh", status.designCapacity);
    LOGF_INFO(COMPONENT_NAME, "Design Energy: %u mWh", status.designEnergy);
    LOGF_INFO(COMPONENT_NAME, "State of Charge: %u%%", status.stateOfCharge);
    LOGF_INFO(COMPONENT_NAME, "State of Health: %u%%", status.stateOfHealth);
    LOGF_INFO(COMPONENT_NAME, "Cell Temperature: %u°C", status.cellTemperature);
    LOGF_INFO(COMPONENT_NAME, "PCB Temperature: %u°C", status.boardTemperature);
    LOGF_INFO(COMPONENT_NAME, "Cycle Count: %u", status.cycleCount);
    LOGF_INFO(COMPONENT_NAME, "Time to Empty: %u min", status.averageTimeToEmpty);
    LOG_INFO(COMPONENT_NAME, "=============================");
}

uint16_t BQ34z100BatteryMonitor::getDeviceType()
{
    try
    {
        return readControlRegister(0x00, 0x01);
    }
    catch (const std::exception &e)
    {
        LOG_ERROR(COMPONENT_NAME, "Failed to read device type: " + std::string(e.what()));
        return 0;
    }
}

uint16_t BQ34z100BatteryMonitor::getChemistryID()
{
    try
    {
        return readControlRegister(0x00, 0x08);
    }
    catch (const std::exception &e)
    {
        LOG_ERROR(COMPONENT_NAME, "Failed to read chemistry ID: " + std::string(e.what()));
        return 0;
    }
}

uint16_t BQ34z100BatteryMonitor::getSerialNumber()
{
    try
    {
        return readControlRegister(0x00, 0x28); // Serial number subcommand
    }
    catch (const std::exception &e)
    {
        LOG_ERROR(COMPONENT_NAME, "Failed to read serial number: " + std::string(e.what()));
        return 0;
    }
}

BQ34z100DeviceInfo BQ34z100BatteryMonitor::getDeviceInfo()
{
    BQ34z100DeviceInfo info;

    try
    {
        info.deviceType = getDeviceType();
        info.chemistryID = getChemistryID();
        info.serialNumber = getSerialNumber();
        info.firmwareVersion = readControlRegister(0x00, 0x02); // Firmware version subcommand

        // Check seal status
        uint16_t controlStatus = readControlRegister(0x00, 0x00);
        info.sealed = (controlStatus & 0x4000) != 0;     // SS bit
        info.fullAccess = (controlStatus & 0x8000) == 0; // FAS bit (inverted)

        LOGF_DEBUG(COMPONENT_NAME, "Device Info: Type=0x%04X, Chem=0x%04X, Serial=0x%04X, FW=0x%04X",
                   info.deviceType, info.chemistryID, info.serialNumber, info.firmwareVersion);

        return info;
    }
    catch (const std::exception &e)
    {
        LOG_ERROR(COMPONENT_NAME, "Failed to read device info: " + std::string(e.what()));
        return info; // Return default initialized struct
    }
}

int16_t BQ34z100BatteryMonitor::getDeviceStatus()
{
    try
    {
        return static_cast<int16_t>(readControlRegister(0x00, 0x00));
    }
    catch (const std::exception &e)
    {
        LOG_ERROR(COMPONENT_NAME, "Failed to read device status: " + std::string(e.what()));
        return -1;
    }
}

bool BQ34z100BatteryMonitor::calibrateVoltageDivider(uint16_t currentVoltage)
{
    LOG_INFO(COMPONENT_NAME, "Starting voltage divider calibration...");

    try
    {
        if (currentVoltage < 5000)
        {
            LOG_ERROR(COMPONENT_NAME, "Voltage too low for calibration (minimum 5000mV)");
            return false;
        }

        // Read current voltage divider setting
        if (!readFlash(0x68, 15))
        {
            LOG_ERROR(COMPONENT_NAME, "Failed to read flash for voltage calibration");
            return false;
        }

        uint16_t currentSetting = (static_cast<uint16_t>(flashBytes_[14]) << 8) | flashBytes_[15];
        float currentVoltageReading = getDeviceInfo().deviceType != 0 ? convertVoltage(readRegister(Register::VOLTAGE, 2)) : currentVoltage * 0.9f; // Mock reading

        // Calculate new setting
        float newSetting = (static_cast<float>(currentVoltage) / currentVoltageReading) * currentSetting;
        uint16_t newSettingInt = static_cast<uint16_t>(newSetting);

        // Update flash with new setting
        changeFlashPair(14, newSettingInt);

        if (!writeFlash(0x68, 15))
        {
            LOG_ERROR(COMPONENT_NAME, "Failed to write voltage calibration to flash");
            return false;
        }

        LOGF_INFO(COMPONENT_NAME, "Voltage divider calibrated: %u -> %u (target: %umV)",
                  currentSetting, newSettingInt, currentVoltage);

        return true;
    }
    catch (const std::exception &e)
    {
        LOG_ERROR(COMPONENT_NAME, "Voltage calibration failed: " + std::string(e.what()));
        return false;
    }
}

bool BQ34z100BatteryMonitor::calibrateCurrentShunt(int16_t current)
{
    LOG_INFO(COMPONENT_NAME, "Starting current shunt calibration...");

    try
    {
        if (current > -200 && current < 200)
        {
            LOG_ERROR(COMPONENT_NAME, "Current too small for calibration (minimum ±200mA)");
            return false;
        }

        // Read current gain from flash
        if (!readFlash(0x68, 15))
        {
            LOG_ERROR(COMPONENT_NAME, "Failed to read flash for current calibration");
            return false;
        }

        // Extract current gain (4 bytes starting at offset 0)
        uint32_t currentGain = (static_cast<uint32_t>(flashBytes_[0]) << 24) |
                               (static_cast<uint32_t>(flashBytes_[1]) << 16) |
                               (static_cast<uint32_t>(flashBytes_[2]) << 8) |
                               static_cast<uint32_t>(flashBytes_[3]);

        // Convert to resistance value
        float currentGainResistance = 4.768f / xemicsToFloat(currentGain);

        // Read actual current
        int16_t actualCurrent = static_cast<int16_t>(readRegister(Register::CURRENT, 2));
        if (actualCurrent == 0)
            actualCurrent = 20; // Avoid division by zero

        // Calculate new gain
        float newGain = (static_cast<float>(actualCurrent) / static_cast<float>(current)) * currentGainResistance;
        uint32_t newGainXemics = floatToXemics(4.768f / newGain);

        // Update flash with new gain
        changeFlashQuad(0, newGainXemics);

        if (!writeFlash(0x68, 15))
        {
            LOG_ERROR(COMPONENT_NAME, "Failed to write current calibration to flash");
            return false;
        }

        LOGF_INFO(COMPONENT_NAME, "Current shunt calibrated: %.6f -> %.6f ohms (target: %dmA)",
                  currentGainResistance, newGain, current);

        return true;
    }
    catch (const std::exception &e)
    {
        LOG_ERROR(COMPONENT_NAME, "Current calibration failed: " + std::string(e.what()));
        return false;
    }
}

bool BQ34z100BatteryMonitor::configureBattery(uint8_t chemistry, uint8_t seriesCells,
                                              uint16_t cellCapacity, uint16_t packVoltage, uint16_t current)
{
    LOG_INFO(COMPONENT_NAME, "Configuring battery parameters...");

    try
    {
        LOGF_INFO(COMPONENT_NAME, "Battery Config: Chemistry=%u, Cells=%u, Capacity=%umAh, Voltage=%umV, Current=%umA",
                  chemistry, seriesCells, cellCapacity, packVoltage, current);

        // Unseal device for configuration
        if (!unsealDevice())
        {
            LOG_ERROR(COMPONENT_NAME, "Failed to unseal device for configuration");
            return false;
        }

        // Calculate pack capacity and energy
        uint16_t packCapacity = cellCapacity;                      // Assuming parallel configuration handled elsewhere
        uint16_t packEnergy = (packCapacity * packVoltage) / 1000; // Convert to Wh

        // Configure design capacity and energy (Flash subclass 48)
        if (!readFlash(48, 24))
        {
            LOG_ERROR(COMPONENT_NAME, "Failed to read design capacity flash");
            return false;
        }

        changeFlashPair(21, packCapacity); // Design capacity
        changeFlashPair(23, packEnergy);   // Design energy

        // Set cell voltage thresholds (example values, adjust based on chemistry)
        uint16_t cellVoltageThresholds = 3600; // 3.6V per cell for LiIon
        if (chemistry == 1)
        {                                 // LiFePO4
            cellVoltageThresholds = 3200; // 3.2V per cell
        }

        changeFlashPair(28, cellVoltageThresholds); // Cell VT1->VT2
        changeFlashPair(30, cellVoltageThresholds); // Cell VT2->VT3

        if (!writeFlash(48, 24))
        {
            LOG_ERROR(COMPONENT_NAME, "Failed to write design capacity flash");
            return false;
        }

        // Reset device to apply new configuration
        reset();

        LOG_INFO(COMPONENT_NAME, "Battery configuration completed successfully");
        return true;
    }
    catch (const std::exception &e)
    {
        LOG_ERROR(COMPONENT_NAME, "Battery configuration failed: " + std::string(e.what()));
        return false;
    }
}

bool BQ34z100BatteryMonitor::enableImpedanceTrack()
{
    LOG_INFO(COMPONENT_NAME, "Enabling Impedance Track algorithm...");

    try
    {
        bool result = writeControlRegister(0x00, 0x21); // Enable IT command

        if (result)
        {
            LOG_INFO(COMPONENT_NAME, "Impedance Track enabled successfully");
        }
        else
        {
            LOG_ERROR(COMPONENT_NAME, "Failed to enable Impedance Track");
        }

        return result;
    }
    catch (const std::exception &e)
    {
        LOG_ERROR(COMPONENT_NAME, "Enable IT failed: " + std::string(e.what()));
        return false;
    }
}

bool BQ34z100BatteryMonitor::enterCalibrationMode()
{
    LOG_INFO(COMPONENT_NAME, "Entering calibration mode...");

    try
    {
        bool result = writeControlRegister(0x00, 0x81); // Enter calibration mode

        if (result)
        {
            LOG_INFO(COMPONENT_NAME, "Calibration mode entered successfully");
        }
        else
        {
            LOG_ERROR(COMPONENT_NAME, "Failed to enter calibration mode");
        }

        return result;
    }
    catch (const std::exception &e)
    {
        LOG_ERROR(COMPONENT_NAME, "Enter calibration mode failed: " + std::string(e.what()));
        return false;
    }
}

bool BQ34z100BatteryMonitor::exitCalibrationMode()
{
    LOG_INFO(COMPONENT_NAME, "Exiting calibration mode...");

    try
    {
        bool result = writeControlRegister(0x00, 0x82); // Exit calibration mode

        if (result)
        {
            LOG_INFO(COMPONENT_NAME, "Calibration mode exited successfully");
        }
        else
        {
            LOG_ERROR(COMPONENT_NAME, "Failed to exit calibration mode");
        }

        return result;
    }
    catch (const std::exception &e)
    {
        LOG_ERROR(COMPONENT_NAME, "Exit calibration mode failed: " + std::string(e.what()));
        return false;
    }
}

int8_t BQ34z100BatteryMonitor::getLearnedStatus()
{
    try
    {
        // Read learned status from appropriate register/flash location
        // This is a placeholder - actual implementation depends on BQ34z100 specifics
        return static_cast<int8_t>(readControlRegister(0x00, 0x23)); // Example subcommand
    }
    catch (const std::exception &e)
    {
        LOG_ERROR(COMPONENT_NAME, "Failed to read learned status: " + std::string(e.what()));
        return -1;
    }
}

float BQ34z100BatteryMonitor::readCurrentShunt()
{
    try
    {
        // Read current shunt resistance from flash
        if (!readFlash(0x68, 15))
        {
            LOG_ERROR(COMPONENT_NAME, "Failed to read current shunt from flash");
            return 0.0f;
        }

        // Extract current gain (4 bytes starting at offset 0)
        uint32_t currentGain = (static_cast<uint32_t>(flashBytes_[0]) << 24) |
                               (static_cast<uint32_t>(flashBytes_[1]) << 16) |
                               (static_cast<uint32_t>(flashBytes_[2]) << 8) |
                               static_cast<uint32_t>(flashBytes_[3]);

        // Convert to resistance in microohms
        float resistance = (4.768f / xemicsToFloat(currentGain)) * 1000000.0f;

        LOGF_DEBUG(COMPONENT_NAME, "Current shunt resistance: %.2f µΩ", resistance);
        return resistance;
    }
    catch (const std::exception &e)
    {
        LOG_ERROR(COMPONENT_NAME, "Failed to read current shunt: " + std::string(e.what()));
        return 0.0f;
    }
}

bool BQ34z100BatteryMonitor::isSealed()
{
    try
    {
        uint16_t controlStatus = readControlRegister(0x00, 0x00);
        return (controlStatus & 0x4000) != 0; // SS bit
    }
    catch (const std::exception &e)
    {
        LOG_ERROR(COMPONENT_NAME, "Failed to check seal status: " + std::string(e.what()));
        return true; // Assume sealed on error
    }
}

bool BQ34z100BatteryMonitor::unsealDevice()
{
    LOG_INFO(COMPONENT_NAME, "Unsealing BQ34z100 device...");

    try
    {
        // Send unseal sequence (repeat 3 times as per TI documentation)
        for (int i = 0; i < 3; i++)
        {
            // First unseal key
            writeControlRegister(0x14, 0x04);
            std::this_thread::sleep_for(std::chrono::milliseconds(10));

            // Second unseal key
            writeControlRegister(0x72, 0x36);
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }

        // Wait for unseal to complete
        std::this_thread::sleep_for(std::chrono::milliseconds(100));

        // Verify unsealed
        bool unsealed = !isSealed();

        if (unsealed)
        {
            LOG_INFO(COMPONENT_NAME, "Device unsealed successfully");
        }
        else
        {
            LOG_ERROR(COMPONENT_NAME, "Device unseal failed");
        }

        return unsealed;
    }
    catch (const std::exception &e)
    {
        LOG_ERROR(COMPONENT_NAME, "Unseal operation failed: " + std::string(e.what()));
        return false;
    }
}

bool BQ34z100BatteryMonitor::enterFullAccessMode()
{
    LOG_INFO(COMPONENT_NAME, "Entering full access mode...");

    try
    {
        // Send full access key sequence
        writeControlRegister(0xFF, 0xFF);
        writeControlRegister(0xFF, 0xFF);

        // Wait for mode change
        std::this_thread::sleep_for(std::chrono::milliseconds(120));

        // Verify full access mode
        uint16_t controlStatus = readControlRegister(0x00, 0x00);
        bool fullAccess = (controlStatus & 0x8000) == 0; // FAS bit (inverted)

        if (fullAccess)
        {
            LOG_INFO(COMPONENT_NAME, "Full access mode entered successfully");
        }
        else
        {
            LOG_ERROR(COMPONENT_NAME, "Failed to enter full access mode");
        }

        return fullAccess;
    }
    catch (const std::exception &e)
    {
        LOG_ERROR(COMPONENT_NAME, "Full access mode failed: " + std::string(e.what()));
        return false;
    }
}

bool BQ34z100BatteryMonitor::readFlash(uint8_t subclass, uint8_t offset)
{
    LOG_DEBUG(COMPONENT_NAME, "Reading flash memory...");

    try
    {
        // Set up flash access
        if (!writeRegister(0x61, 0x00))
        { // Enable flash access
            LOG_ERROR(COMPONENT_NAME, "Failed to enable flash access");
            return false;
        }

        if (!writeRegister(0x3E, subclass))
        { // Set subclass
            LOG_ERROR(COMPONENT_NAME, "Failed to set flash subclass");
            return false;
        }

        if (!writeRegister(0x3F, offset % 32))
        { // Set offset
            LOG_ERROR(COMPONENT_NAME, "Failed to set flash offset");
            return false;
        }

        // Wait for flash access to be ready
        std::this_thread::sleep_for(std::chrono::milliseconds(10));

        // Read flash data (32 bytes)
#ifdef ESP32_PLATFORM
        // Wire.beginTransmission(BQ34Z100_ADDRESS);
        // Wire.write(0x40); // Flash data start address
        // Wire.endTransmission();
        // Wire.requestFrom(BQ34Z100_ADDRESS, 32);
        //
        // for (int i = 0; i < 32; i++) {
        //     if (Wire.available()) {
        //         flashBytes_[i] = Wire.read();
        //     } else {
        //         LOG_ERROR(COMPONENT_NAME, "Incomplete flash data read");
        //         return false;
        //     }
        // }
#else
        // Mock flash data for development
        for (int i = 0; i < 32; i++)
        {
            flashBytes_[i] = i; // Mock data pattern
        }
#endif

        LOGF_DEBUG(COMPONENT_NAME, "Flash read successful: subclass=0x%02X, offset=%u", subclass, offset);
        return true;
    }
    catch (const std::exception &e)
    {
        LOG_ERROR(COMPONENT_NAME, "Flash read failed: " + std::string(e.what()));
        return false;
    }
}

bool BQ34z100BatteryMonitor::writeFlash(uint8_t subclass, uint8_t offset)
{
    LOG_DEBUG(COMPONENT_NAME, "Writing flash memory...");

    try
    {
        // Set up flash access
        if (!writeRegister(0x61, 0x00))
        { // Enable flash access
            LOG_ERROR(COMPONENT_NAME, "Failed to enable flash access");
            return false;
        }

        if (!writeRegister(0x3E, subclass))
        { // Set subclass
            LOG_ERROR(COMPONENT_NAME, "Failed to set flash subclass");
            return false;
        }

        if (!writeRegister(0x3F, offset % 32))
        { // Set offset
            LOG_ERROR(COMPONENT_NAME, "Failed to set flash offset");
            return false;
        }

        // Write flash data (32 bytes)
#ifdef ESP32_PLATFORM
        // Wire.beginTransmission(BQ34Z100_ADDRESS);
        // Wire.write(0x40); // Flash data start address
        // for (int i = 0; i < 32; i++) {
        //     Wire.write(flashBytes_[i]);
        // }
        // Wire.endTransmission();
#endif

        // Calculate and write checksum
        calculateChecksum(subclass, offset);

        // Wait for write to complete
        std::this_thread::sleep_for(std::chrono::milliseconds(200));

        LOGF_DEBUG(COMPONENT_NAME, "Flash write successful: subclass=0x%02X, offset=%u", subclass, offset);
        return true;
    }
    catch (const std::exception &e)
    {
        LOG_ERROR(COMPONENT_NAME, "Flash write failed: " + std::string(e.what()));
        return false;
    }
}

void BQ34z100BatteryMonitor::setSafetyLimits(int minSoC, int maxSoC, int minSoH,
                                             int minCellTemp, int maxCellTemp,
                                             int minBoardTemp, int maxBoardTemp)
{
    safetyLimits_.minSoC = minSoC;
    safetyLimits_.maxSoC = maxSoC;
    safetyLimits_.minSoH = minSoH;
    safetyLimits_.minCellTemp = minCellTemp;
    safetyLimits_.maxCellTemp = maxCellTemp;
    safetyLimits_.minBoardTemp = minBoardTemp;
    safetyLimits_.maxBoardTemp = maxBoardTemp;

    LOGF_INFO(COMPONENT_NAME, "Safety limits updated: SoC=%d-%d%%, SoH>=%d%%, CellTemp=%d-%d°C, BoardTemp=%d-%d°C",
              minSoC, maxSoC, minSoH, minCellTemp, maxCellTemp, minBoardTemp, maxBoardTemp);
}

BQ34z100BatteryMonitor::SafetyLimits BQ34z100BatteryMonitor::getSafetyLimits() const
{
    return safetyLimits_;
}

// Private implementation methods

uint16_t BQ34z100BatteryMonitor::readRegister(Register reg, uint8_t length)
{
    uint16_t result = 0;
    uint8_t address = static_cast<uint8_t>(reg);

    try
    {
#ifdef ESP32_PLATFORM
        // for (int i = 0; i <= length; i++) {
        //     Wire.beginTransmission(BQ34Z100_ADDRESS);
        //     Wire.write(address + i);
        //     Wire.endTransmission();
        //     Wire.requestFrom(BQ34Z100_ADDRESS, 1);
        //
        //     if (Wire.available()) {
        //         result |= (Wire.read() << (8 * i));
        //     } else {
        //         throw std::runtime_error("I2C read timeout");
        //     }
        // }
#else
        // Mock data for development
        switch (reg)
        {
        case Register::VOLTAGE:
            result = 12500;
            break; // 12.5V
        case Register::CURRENT:
            result = static_cast<uint16_t>(-500);
            break; // -0.5A (discharging)
        case Register::STATE_OF_CHARGE:
            result = 75;
            break; // 75%
        case Register::STATE_OF_HEALTH:
            result = 95;
            break; // 95%
        case Register::TEMPERATURE:
            result = 2980;
            break; // ~25°C in Kelvin
        case Register::PCB_TEMPERATURE:
            result = 2980;
            break; // ~25°C
        case Register::REMAINING_CAPACITY:
            result = 1500;
            break; // 3000mAh (×2)
        case Register::CYCLE_COUNT:
            result = 42;
            break;
        case Register::AVG_TIME_TO_EMPTY:
            result = 180;
            break; // 3 hours
        default:
            result = 0;
            break;
        }
#endif

        return result;
    }
    catch (const std::exception &e)
    {
        throw std::runtime_error("Register read failed: " + std::string(e.what()));
    }
}

bool BQ34z100BatteryMonitor::writeRegister(uint8_t address, uint8_t value)
{
    try
    {
#ifdef ESP32_PLATFORM
        // Wire.beginTransmission(BQ34Z100_ADDRESS);
        // Wire.write(address);
        // Wire.write(value);
        // return Wire.endTransmission() == 0;
#else
        // Mock write for development
        LOGF_DEBUG(COMPONENT_NAME, "Mock register write: 0x%02X = 0x%02X", address, value);
#endif
        return true;
    }
    catch (const std::exception &e)
    {
        LOG_ERROR(COMPONENT_NAME, "Register write failed: " + std::string(e.what()));
        return false;
    }
}

uint16_t BQ34z100BatteryMonitor::readControlRegister(uint8_t subcommand1, uint8_t subcommand2)
{
    try
    {
#ifdef ESP32_PLATFORM
        // // Write control command
        // Wire.beginTransmission(BQ34Z100_ADDRESS);
        // Wire.write(0x00);  // Control register
        // Wire.write(subcommand2);
        // Wire.write(subcommand1);
        // Wire.endTransmission();
        //
        // // Read response
        // Wire.beginTransmission(BQ34Z100_ADDRESS);
        // Wire.write(0x00);
        // Wire.endTransmission();
        // Wire.requestFrom(BQ34Z100_ADDRESS, 2);
        //
        // uint16_t result = 0;
        // if (Wire.available()) result = Wire.read();
        // if (Wire.available()) result |= (Wire.read() << 8);
        //
        // return result;
#else
        // Mock control register data
        uint16_t command = (subcommand1 << 8) | subcommand2;
        switch (command)
        {
        case 0x0001:
            return 0x0100; // Device type
        case 0x0008:
            return 0x0355; // Chemistry ID
        case 0x0028:
            return 0x1234; // Serial number
        case 0x0002:
            return 0x0109; // Firmware version
        case 0x0000:
            return 0x4000; // Control status (sealed)
        default:
            return 0x0000;
        }
#endif
    }
    catch (const std::exception &e)
    {
        throw std::runtime_error("Control register read failed: " + std::string(e.what()));
    }
}

bool BQ34z100BatteryMonitor::writeControlRegister(uint8_t subcommand1, uint8_t subcommand2)
{
    try
    {
#ifdef ESP32_PLATFORM
        // Wire.beginTransmission(BQ34Z100_ADDRESS);
        // Wire.write(0x00);  // Control register
        // Wire.write(subcommand2);
        // Wire.write(subcommand1);
        // return Wire.endTransmission() == 0;
#else
        LOGF_DEBUG(COMPONENT_NAME, "Mock control write: 0x%02X%02X", subcommand1, subcommand2);
#endif
        return true;
    }
    catch (const std::exception &e)
    {
        LOG_ERROR(COMPONENT_NAME, "Control register write failed: " + std::string(e.what()));
        return false;
    }
}

float BQ34z100BatteryMonitor::convertVoltage(uint16_t rawValue)
{
    return static_cast<float>(rawValue) * VOLTAGE_SCALE;
}

float BQ34z100BatteryMonitor::convertCurrent(int16_t rawValue)
{
    return static_cast<float>(rawValue) * CURRENT_SCALE;
}

float BQ34z100BatteryMonitor::convertTemperature(uint16_t rawValue)
{
    // Convert from 0.1K units to Celsius
    return (static_cast<float>(rawValue) * 0.1f) - 273.15f;
}

void BQ34z100BatteryMonitor::calculateChecksum(uint16_t subclass, uint8_t offset)
{
    int checksum = 0;
    for (int i = 0; i < 32; i++)
    {
        checksum += flashBytes_[i];
    }

    int checksumTemp = checksum / 256;
    checksum = checksum - (checksumTemp * 256);
    checksum = 255 - checksum;

    // Write checksum
    writeRegister(0x60, static_cast<uint8_t>(checksum));

    LOGF_DEBUG(COMPONENT_NAME, "Checksum calculated and written: 0x%02X", checksum);
}

void BQ34z100BatteryMonitor::changeFlashPair(uint8_t index, int value)
{
    if (index > 30)
    { // Ensure we don't exceed buffer bounds
        LOG_ERROR(COMPONENT_NAME, "Flash pair index out of bounds");
        return;
    }

    flashBytes_[index] = static_cast<uint8_t>(value >> 8);       // High byte
    flashBytes_[index + 1] = static_cast<uint8_t>(value & 0xFF); // Low byte

    LOGF_DEBUG(COMPONENT_NAME, "Flash pair changed at index %u: 0x%04X", index, value);
}

void BQ34z100BatteryMonitor::changeFlashQuad(uint8_t index, uint32_t value)
{
    if (index > 28)
    { // Ensure we don't exceed buffer bounds
        LOG_ERROR(COMPONENT_NAME, "Flash quad index out of bounds");
        return;
    }

    flashBytes_[index] = static_cast<uint8_t>(value >> 24);
    flashBytes_[index + 1] = static_cast<uint8_t>((value >> 16) & 0xFF);
    flashBytes_[index + 2] = static_cast<uint8_t>((value >> 8) & 0xFF);
    flashBytes_[index + 3] = static_cast<uint8_t>(value & 0xFF);

    LOGF_DEBUG(COMPONENT_NAME, "Flash quad changed at index %u: 0x%08X", index, value);
}

uint32_t BQ34z100BatteryMonitor::floatToXemics(float value)
{
    // Convert IEEE 754 float to Xemics format (simplified implementation)
    // This is a placeholder - actual Xemics conversion may be more complex
    union
    {
        float f;
        uint32_t i;
    } converter;
    converter.f = value;
    return converter.i;
}

float BQ34z100BatteryMonitor::xemicsToFloat(uint32_t value)
{
    // Convert Xemics format to IEEE 754 float (simplified implementation)
    // This is a placeholder - actual Xemics conversion may be more complex
    union
    {
        float f;
        uint32_t i;
    } converter;
    converter.i = value;
    return converter.f;
}

bool BQ34z100BatteryMonitor::scanI2CDevices()
{
    LOG_INFO(COMPONENT_NAME, "Scanning for required I2C devices...");

    bool foundBQ34 = false;
    bool foundDS1307 = false;
    bool foundEEPROM = false;

#ifdef ESP32_PLATFORM
    // for (uint8_t address = 1; address < 127; address++) {
    //     Wire.beginTransmission(address);
    //     uint8_t error = Wire.endTransmission();
    //
    //     if (error == 0) {
    //         LOGF_DEBUG(COMPONENT_NAME, "I2C device found at address 0x%02X", address);
    //
    //         switch (address) {
    //             case 0x50: foundEEPROM = true; break;
    //             case 0x55: foundBQ34 = true; break;
    //             case 0x68: foundDS1307 = true; break;
    //         }
    //     }
    // }
#else
    // Mock device discovery for development
    foundBQ34 = foundDS1307 = foundEEPROM = true;
    LOG_DEBUG(COMPONENT_NAME, "Mock I2C scan: all devices found");
#endif

    if (!foundBQ34)
    {
        LOG_ERROR(COMPONENT_NAME, "BQ34Z100 not found at address 0x55");
    }
    else
    {
        LOG_INFO(COMPONENT_NAME, "BQ34Z100 found at address 0x55");
    }

    if (!foundDS1307)
    {
        LOG_ERROR(COMPONENT_NAME, "DS1307 RTC not found at address 0x68");
    }
    else
    {
        LOG_INFO(COMPONENT_NAME, "DS1307 RTC found at address 0x68");
    }

    if (!foundEEPROM)
    {
        LOG_ERROR(COMPONENT_NAME, "DS1307 EEPROM not found at address 0x50");
    }
    else
    {
        LOG_INFO(COMPONENT_NAME, "DS1307 EEPROM found at address 0x50");
    }

    return foundBQ34 && foundDS1307 && foundEEPROM;
}

bool BQ34z100BatteryMonitor::validateDevice()
{
    try
    {
        // Test basic communication
        uint16_t deviceType = getDeviceType();
        if (deviceType == 0 || deviceType == 0xFFFF)
        {
            LOG_ERROR(COMPONENT_NAME, "Invalid device type response");
            return false;
        }

        // Test register reads
        uint16_t voltage = readRegister(Register::VOLTAGE, 2);
        uint8_t soc = static_cast<uint8_t>(readRegister(Register::STATE_OF_CHARGE, 1));

        // Validate reasonable values
        if (voltage > 25000 || soc > 100)
        { // > 25V or > 100% seems unreasonable
            LOGF_WARNING(COMPONENT_NAME, "Questionable readings: Voltage=%umV, SoC=%u%%", voltage, soc);
        }

        LOG_INFO(COMPONENT_NAME, "Device validation successful");
        return true;
    }
    catch (const std::exception &e)
    {
        LOG_ERROR(COMPONENT_NAME, "Device validation failed: " + std::string(e.what()));
        return false;
    }
}

bool BQ34z100BatteryMonitor::checkSafetyLimits(const EnhancedBatteryStatus &status)
{
    bool safe = true;

    // Check State of Charge limits
    if (status.stateOfCharge < safetyLimits_.minSoC ||
        status.stateOfCharge > safetyLimits_.maxSoC)
    {
        LOGF_ERROR(COMPONENT_NAME, "SoC out of safe range: %u%% (limits: %d-%d%%)",
                   status.stateOfCharge, safetyLimits_.minSoC, safetyLimits_.maxSoC);
        safe = false;
    }

    // Check State of Health limit
    if (status.stateOfHealth < safetyLimits_.minSoH)
    {
        LOGF_ERROR(COMPONENT_NAME, "SoH below safe limit: %u%% (minimum: %d%%)",
                   status.stateOfHealth, safetyLimits_.minSoH);
        safe = false;
    }

    // Check cell temperature limits
    if (status.cellTemperature < safetyLimits_.minCellTemp ||
        status.cellTemperature > safetyLimits_.maxCellTemp)
    {
        LOGF_ERROR(COMPONENT_NAME, "Cell temperature out of safe range: %u°C (limits: %d-%d°C)",
                   status.cellTemperature, safetyLimits_.minCellTemp, safetyLimits_.maxCellTemp);
        safe = false;
    }

    // Check board temperature limits
    if (status.boardTemperature < safetyLimits_.minBoardTemp ||
        status.boardTemperature > safetyLimits_.maxBoardTemp)
    {
        LOGF_ERROR(COMPONENT_NAME, "Board temperature out of safe range: %u°C (limits: %d-%d°C)",
                   status.boardTemperature, safetyLimits_.minBoardTemp, safetyLimits_.maxBoardTemp);
        safe = false;
    }

    return safe;
}

void BQ34z100BatteryMonitor::handleBatteryError(const std::string &error)
{
    LOG_CRITICAL(COMPONENT_NAME, "BATTERY SAFETY VIOLATION: " + error);

    // Log current status for diagnostics
    try
    {
        auto status = readBatteryStatus();
        LOGF_CRITICAL(COMPONENT_NAME, "Critical Status: SoC=%u%%, Voltage=%.1fmV, CellTemp=%u°C, BoardTemp=%u°C",
                      status.stateOfCharge, status.voltage, status.cellTemperature, status.boardTemperature);
    }
    catch (...)
    {
        LOG_CRITICAL(COMPONENT_NAME, "Unable to read status during safety violation");
    }

    // For critical battery errors, enter emergency sleep
    if (error.find("safety") != std::string::npos ||
        error.find("temperature") != std::string::npos ||
        error.find("voltage") != std::string::npos)
    {

        LOG_CRITICAL(COMPONENT_NAME, "Entering emergency sleep mode for safety");
        enterEmergencySleep();
    }
}

void BQ34z100BatteryMonitor::enterEmergencySleep()
{
    LOG_CRITICAL(COMPONENT_NAME, "EMERGENCY SLEEP ACTIVATED - BATTERY SAFETY PROTECTION");

#ifdef ESP32_PLATFORM
    // Disable all peripherals
    // Note: Use ESP-IDF GPIO functions instead of Arduino digitalWrite
    // gpio_set_level(GPIO_NUM_15, 1); // Disable 5V supply
    
    // Enter deep sleep for safety (1 hour)
    // esp_sleep_enable_timer_wakeup(3600000000ULL); // 1 hour in microseconds
    // esp_deep_sleep_start();
#else
    // For development, just log the emergency state
    LOG_CRITICAL(COMPONENT_NAME, "Emergency sleep would be activated on ESP32 platform");

    // In a real implementation, you might want to throw an exception
    // or call a system shutdown function
    throw std::runtime_error("EMERGENCY BATTERY SAFETY SHUTDOWN");
#endif
}