#pragma once

#include "interfaces.h"
#include <stdexcept>
#include <cstdint>
#include <memory>

/**
 * @file bq34z100_battery_monitor.h
 * @brief BQ34z100 Battery Monitor class for comprehensive battery management
 * @author Anthony Kirk
 * @date 2025-07-29
 * @version 1.0.0
 *
 * This class provides a complete interface to the Texas Instruments BQ34z100
 * battery fuel gauge IC. It implements safety monitoring, calibration,
 * configuration, and comprehensive battery status reporting.
 */

/**
 * @brief Enhanced battery status structure with all BQ34z100 parameters
 */
struct EnhancedBatteryStatus
{
    // Basic measurements
    float current = 0.0f;        ///< Current in mA (positive = charging, negative = discharging)
    float voltage = 0.0f;        ///< Battery voltage in mV
    float averageCurrent = 0.0f; ///< Average current in mA

    // Capacity information
    uint16_t remainingCapacity = 0;  ///< Remaining capacity in mAh
    uint16_t fullChargeCapacity = 0; ///< Full charge capacity in mAh

    // Temperature readings
    uint16_t cellTemperature = 0;  ///< Cell temperature in °C
    uint16_t boardTemperature = 0; ///< PCB temperature in °C

    // State information
    uint8_t stateOfCharge = 0;       ///< State of charge (0-100%)
    uint16_t stateOfHealth = 0;      ///< State of health (0-100%)
    uint16_t averageTimeToEmpty = 0; ///< Average time to empty in minutes
    uint16_t cycleCount = 0;         ///< Battery cycle count

    // Additional metrics
    uint16_t designCapacity = 0;     ///< Design capacity in mAh
    uint16_t designEnergy = 0;       ///< Design energy in mWh
    float instantaneousPower = 0.0f; ///< Instantaneous power in mW
};

/**
 * @brief BQ34z100 alarm and status flags structure
 */
struct BQ34z100AlarmStatus
{
    // Temperature alarms
    bool overTempCharge = false;    ///< Over-temperature in charge condition (OTC)
    bool overTempDischarge = false; ///< Over-temperature in discharge condition (OTD)

    // Voltage alarms
    bool batteryHigh = false; ///< Battery high voltage condition (BAT_HIGH)
    bool batteryLow = false;  ///< Battery low voltage condition (BATLOW)

    // Charging status
    bool chargeInhibit = false;      ///< Charge inhibit condition (CHG_INH)
    bool chargingDisallowed = false; ///< Charging disallowed (XCHG)
    bool fullCharge = false;         ///< Full charge detected (FC)
    bool charging = false;           ///< Fast charging allowed (CHG)

    // System status
    bool rest = false;                   ///< Device in rest/relax mode (REST)
    bool conditionFlag = false;          ///< Condition flag set (CF)
    bool remainingCapacityAlarm = false; ///< Remaining capacity alarm (RCA)
    bool endOfDischarge = false;         ///< End-of-discharge threshold reached (EOD)
    bool discharging = false;            ///< Discharging detected (DSG)
    bool hardwareAlarm = false;          ///< Hardware alarm/alert (ALARM)
};

/**
 * @brief BQ34z100 device information structure
 */
struct BQ34z100DeviceInfo
{
    uint16_t deviceType = 0;      ///< Device type identifier
    uint16_t chemistryID = 0;     ///< Battery chemistry ID
    uint16_t serialNumber = 0;    ///< Device serial number
    uint16_t firmwareVersion = 0; ///< Firmware version
    bool sealed = true;           ///< Device seal status
    bool fullAccess = false;      ///< Full access mode status
};

/**
 * @brief BQ34z100 Battery Monitor class implementing comprehensive battery management
 *
 * This class provides a complete interface to the Texas Instruments BQ34z100
 * battery fuel gauge IC. Features include:
 *
 * - Real-time battery monitoring (voltage, current, temperature, capacity)
 * - State estimation (SoC, SoH, time to empty)
 * - Safety monitoring with configurable limits
 * - Calibration and configuration capabilities
 * - Flash memory access for advanced configuration
 * - Alarm and status monitoring
 * - Thread-safe operations
 * - Comprehensive error handling and logging
 */
class BQ34z100BatteryMonitor : public IHardwareInterface
{
public:
    /**
     * @brief Constructor
     */
    BQ34z100BatteryMonitor();

    /**
     * @brief Destructor - ensures proper cleanup
     */
    ~BQ34z100BatteryMonitor() override;

    // IHardwareInterface implementation
    /**
     * @brief Initialize the battery monitor and I2C communication
     * @return true if initialization successful, false otherwise
     */
    bool initialize() override;

    /**
     * @brief Check if battery monitor is connected and responding
     * @return true if device is connected and communicating
     */
    bool isConnected() override;

    /**
     * @brief Reset the battery monitor device
     */
    void reset() override;

    /**
     * @brief Clean up resources and reset state
     */
    void cleanup() override;

    // Battery monitoring methods
    /**
     * @brief Read complete battery status information
     * @return Enhanced battery status structure
     * @throws std::runtime_error if reading fails or safety limits exceeded
     */
    EnhancedBatteryStatus readBatteryStatus();

    /**
     * @brief Read alarm and status flags
     * @return Alarm status structure with all flags
     * @throws std::runtime_error if reading fails
     */
    BQ34z100AlarmStatus readAlarmStatus();

    /**
     * @brief Check if battery is within safe operating limits
     * @param status Battery status to validate
     * @return true if battery is safe to operate
     */
    bool isBatterySafe(const EnhancedBatteryStatus &status);

    /**
     * @brief Print comprehensive battery data to console/log
     * @param status Battery status to display
     */
    void printBatteryData(const EnhancedBatteryStatus &status);

    // Device information methods
    /**
     * @brief Get device type identifier
     * @return Device type code
     */
    uint16_t getDeviceType();

    /**
     * @brief Get battery chemistry ID
     * @return Chemistry identifier
     */
    uint16_t getChemistryID();

    /**
     * @brief Get device serial number
     * @return Serial number
     */
    uint16_t getSerialNumber();

    /**
     * @brief Get complete device information
     * @return Device info structure
     */
    BQ34z100DeviceInfo getDeviceInfo();

    /**
     * @brief Get current device status flags
     * @return Status register value
     */
    int16_t getDeviceStatus();

    // Calibration and configuration methods
    /**
     * @brief Calibrate voltage divider based on known voltage
     * @param currentVoltage Known accurate voltage in mV
     * @return true if calibration successful
     */
    bool calibrateVoltageDivider(uint16_t currentVoltage);

    /**
     * @brief Calibrate current shunt based on known current
     * @param current Known current in mA
     * @return true if calibration successful
     */
    bool calibrateCurrentShunt(int16_t current);

    /**
     * @brief Configure battery parameters for specific chemistry and capacity
     * @param chemistry Battery chemistry type (0=LiCoO2, 1=LiFePO4, etc.)
     * @param seriesCells Number of cells in series
     * @param cellCapacity Individual cell capacity in mAh
     * @param packVoltage Pack voltage in mV
     * @param current Typical current in mA
     * @return true if configuration successful
     */
    bool configureBattery(uint8_t chemistry, uint8_t seriesCells,
                          uint16_t cellCapacity, uint16_t packVoltage, uint16_t current);

    // Advanced features
    /**
     * @brief Enable impedance track algorithm
     * @return true if enabled successfully
     */
    bool enableImpedanceTrack();

    /**
     * @brief Enter calibration mode for advanced calibration
     * @return true if calibration mode entered successfully
     */
    bool enterCalibrationMode();

    /**
     * @brief Exit calibration mode
     * @return true if calibration mode exited successfully
     */
    bool exitCalibrationMode();

    /**
     * @brief Get learned capacity status
     * @return Learned status flags
     */
    int8_t getLearnedStatus();

    /**
     * @brief Read current shunt resistance value
     * @return Shunt resistance in microohms
     */
    float readCurrentShunt();

    // Security and access control
    /**
     * @brief Check if device is sealed
     * @return true if device is sealed
     */
    bool isSealed();

    /**
     * @brief Unseal device for advanced operations
     * @return true if unsealing successful
     */
    bool unsealDevice();

    /**
     * @brief Enter full access mode for flash operations
     * @return true if full access mode entered
     */
    bool enterFullAccessMode();

    // Flash memory operations
    /**
     * @brief Read data from flash memory
     * @param subclass Flash subclass to read
     * @param offset Offset within subclass
     * @return true if read successful
     */
    bool readFlash(uint8_t subclass, uint8_t offset);

    /**
     * @brief Write data to flash memory
     * @param subclass Flash subclass to write
     * @param offset Offset within subclass
     * @return true if write successful
     */
    bool writeFlash(uint8_t subclass, uint8_t offset);

    /**
     * @brief Get flash data buffer
     * @return Pointer to 32-byte flash data buffer
     */
    const uint8_t *getFlashData() const { return flashBytes_; }

    // Safety configuration
    /**
     * @brief Set battery safety limits
     * @param minSoC Minimum state of charge (%)
     * @param maxSoC Maximum state of charge (%)
     * @param minSoH Minimum state of health (%)
     * @param minCellTemp Minimum cell temperature (°C)
     * @param maxCellTemp Maximum cell temperature (°C)
     * @param minBoardTemp Minimum board temperature (°C)
     * @param maxBoardTemp Maximum board temperature (°C)
     */
    void setSafetyLimits(int minSoC, int maxSoC, int minSoH,
                         int minCellTemp, int maxCellTemp,
                         int minBoardTemp, int maxBoardTemp);

    /**
     * @brief Get current safety limits
     * @return Safety limits structure
     */
    struct SafetyLimits
    {
        int minSoC, maxSoC, minSoH;
        int minCellTemp, maxCellTemp;
        int minBoardTemp, maxBoardTemp;
    };
    SafetyLimits getSafetyLimits() const;

private:
    // Hardware constants
    static constexpr uint8_t BQ34Z100_ADDRESS = 0x55; ///< I2C device address
    static constexpr float VOLTAGE_SCALE = 1.0f;      ///< Voltage scaling factor
    static constexpr float CURRENT_SCALE = 2.0f;      ///< Current scaling factor

    // Device state
    bool deviceFound_;          ///< Device detection flag
    void *i2cHandle_;           ///< Platform-specific I2C handle
    uint8_t flashBytes_[32];    ///< Flash memory buffer
    SafetyLimits safetyLimits_; ///< Current safety limits

    // Register addresses enumeration
    enum class Register : uint8_t
    {
        CONTROL = 0x00,            ///< Control register
        STATE_OF_CHARGE = 0x02,    ///< State of charge register
        REMAINING_CAPACITY = 0x04, ///< Remaining capacity register
        VOLTAGE = 0x08,            ///< Voltage register
        AVERAGE_CURRENT = 0x0A,    ///< Average current register
        TEMPERATURE = 0x0C,        ///< Temperature register
        FLAGS = 0x0E,              ///< Flags register
        CURRENT = 0x10,            ///< Current register
        FLAGS_B = 0x12,            ///< Flags B register
        AVG_TIME_TO_EMPTY = 0x18,  ///< Average time to empty register
        PCB_TEMPERATURE = 0x2A,    ///< PCB temperature register
        CYCLE_COUNT = 0x2C,        ///< Cycle count register
        STATE_OF_HEALTH = 0x2E,    ///< State of health register
        DESIGN_CAPACITY = 0x3C,    ///< Design capacity register
        DESIGN_ENERGY = 0x3E       ///< Design energy register
    };

    // Control subcommands enumeration
    enum class ControlCommand : uint16_t
    {
        CONTROL_STATUS = 0x0000, ///< Control status
        DEVICE_TYPE = 0x0001,    ///< Device type
        RESET_DATA = 0x0005,     ///< Reset data
        CHEMISTRY_ID = 0x0008,   ///< Chemistry ID
        ENABLE_IT = 0x0021,      ///< Enable impedance track
        RESET = 0x0041,          ///< Reset device
        ENTER_CAL = 0x0081,      ///< Enter calibration mode
        EXIT_CAL = 0x0082        ///< Exit calibration mode
    };

    // Internal register access methods
    /**
     * @brief Read register value
     * @param reg Register to read
     * @param length Number of bytes to read (1 or 2)
     * @return Register value
     */
    uint16_t readRegister(Register reg, uint8_t length = 1);

    /**
     * @brief Write register value
     * @param address Register address
     * @param value Value to write
     * @return true if write successful
     */
    bool writeRegister(uint8_t address, uint8_t value);

    /**
     * @brief Read control register
     * @param subcommand1 First subcommand byte
     * @param subcommand2 Second subcommand byte
     * @return Control register value
     */
    uint16_t readControlRegister(uint8_t subcommand1, uint8_t subcommand2);

    /**
     * @brief Write control register
     * @param subcommand1 First subcommand byte
     * @param subcommand2 Second subcommand byte
     * @return true if write successful
     */
    bool writeControlRegister(uint8_t subcommand1, uint8_t subcommand2);

    // Data conversion methods
    /**
     * @brief Convert raw voltage value to millivolts
     * @param rawValue Raw register value
     * @return Voltage in mV
     */
    float convertVoltage(uint16_t rawValue);

    /**
     * @brief Convert raw current value to milliamps
     * @param rawValue Raw register value
     * @return Current in mA
     */
    float convertCurrent(int16_t rawValue);

    /**
     * @brief Convert raw temperature value to Celsius
     * @param rawValue Raw register value
     * @return Temperature in °C
     */
    float convertTemperature(uint16_t rawValue);

    // Flash memory utilities
    /**
     * @brief Calculate checksum for flash data
     * @param subclass Flash subclass
     * @param offset Offset within subclass
     */
    void calculateChecksum(uint16_t subclass, uint8_t offset);

    /**
     * @brief Change flash data pair (16-bit value)
     * @param index Index in flash buffer
     * @param value New value
     */
    void changeFlashPair(uint8_t index, int value);

    /**
     * @brief Change flash data quad (32-bit value)
     * @param index Index in flash buffer
     * @param value New value
     */
    void changeFlashQuad(uint8_t index, uint32_t value);

    // Utility functions for Xemics floating point format
    /**
     * @brief Convert float to Xemics format
     * @param value Float value to convert
     * @return Xemics format value
     */
    uint32_t floatToXemics(float value);

    /**
     * @brief Convert Xemics format to float
     * @param value Xemics format value
     * @return Float value
     */
    float xemicsToFloat(uint32_t value);

    // Device discovery and validation
    /**
     * @brief Scan I2C bus for required devices
     * @return true if all required devices found
     */
    bool scanI2CDevices();

    /**
     * @brief Validate device communication
     * @return true if device responds correctly
     */
    bool validateDevice();

    // Safety monitoring
    /**
     * @brief Check battery safety limits
     * @param status Battery status to check
     * @return true if within safe limits
     */
    bool checkSafetyLimits(const EnhancedBatteryStatus &status);

    /**
     * @brief Handle battery safety violation
     * @param error Error description
     */
    void handleBatteryError(const std::string &error);

    /**
     * @brief Enter emergency sleep mode for safety
     */
    void enterEmergencySleep();
};