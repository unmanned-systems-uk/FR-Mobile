#pragma once

#include "interfaces.h"
#include <string>
#include <chrono>
#include <mutex>

/**
 * @file rtc_time_manager.h
 * @brief Real-Time Clock Manager for DS1307 time management and synchronization
 * @author Anthony Kirk
 * @date 2025-07-29
 * @version 1.0.0
 *
 * This class provides comprehensive time management using the DS1307 RTC module
 * including network time synchronization, sleep scheduling, and EEPROM operations.
 */

/**
 * @brief Time components structure for date/time manipulation
 */
struct TimeComponents
{
    int year = 2024;        ///< Year (full year, e.g., 2024)
    int month = 1;          ///< Month (1-12)
    int day = 1;            ///< Day of month (1-31)
    int hour = 0;           ///< Hour (0-23)
    int minute = 0;         ///< Minute (0-59)
    int second = 0;         ///< Second (0-59)
    int dayOfWeek = 1;      ///< Day of week (1-7, 1=Sunday)
    int timeZoneOffset = 0; ///< Time zone offset in hours (-12 to +12)
};

/**
 * @brief Time synchronization status and statistics
 */
struct TimeSyncStatus
{
    bool rtcInitialized = false;    ///< RTC module initialization status
    bool networkTimeSynced = false; ///< Network time synchronization status
    std::string lastSyncTime;       ///< Timestamp of last successful sync
    std::string timeSource;         ///< Source of current time (RTC/Network/System)
    int syncAttempts = 0;           ///< Number of sync attempts
    int successfulSyncs = 0;        ///< Number of successful syncs
    std::string lastError;          ///< Last synchronization error
    uint64_t driftMilliseconds = 0; ///< Detected clock drift in milliseconds
};

/**
 * @brief Sleep schedule configuration for power management
 */
struct SleepSchedule
{
    int nightStartHour = 22;                   ///< Hour to start night sleep (0-23)
    int nightEndHour = 6;                      ///< Hour to end night sleep (0-23)
    bool nightSleepEnabled = true;             ///< Whether night sleep is enabled
    uint64_t nightSleepDurationUs = 0;         ///< Calculated night sleep duration in microseconds
    uint64_t normalScanIntervalUs = 600000000; ///< Normal scan interval (10 minutes)
    uint64_t nightScanIntervalUs = 3600000000; ///< Night scan interval (1 hour)
};

/**
 * @brief RTC Time Manager implementing comprehensive time management
 *
 * This class provides a complete interface for time management using the DS1307
 * RTC module. Features include:
 *
 * - DS1307 RTC module communication and control
 * - Network time synchronization with automatic drift correction
 * - Sleep schedule management for power optimization
 * - EEPROM operations for persistent time storage
 * - Time format conversion and validation
 * - Clock drift detection and compensation
 * - Thread-safe time operations
 * - Comprehensive error handling and recovery
 */
class RTCTimeManager : public ITimeManager
{
public:
    /**
     * @brief Constructor
     * @param i2cAddress DS1307 I2C address (default: 0x68)
     * @param eepromAddress DS1307 EEPROM I2C address (default: 0x50)
     */
    RTCTimeManager(uint8_t i2cAddress = 0x68, uint8_t eepromAddress = 0x50);

    /**
     * @brief Destructor - ensures proper cleanup
     */
    ~RTCTimeManager() override;

    // ITimeManager interface implementation
    /**
     * @brief Get current date and time as ISO 8601 string
     * @return Current date/time string in format "YYYY-MM-DDTHH:MM:SSZ"
     */
    std::string getCurrentDateTime() override;

    /**
     * @brief Set RTC time from network time string
     * @param networkTime Network time in format "YY/MM/DD,HH:MM:SS+ZZ"
     * @return true if time set successfully, false otherwise
     */
    bool setTimeFromNetwork(const std::string &networkTime) override;

    /**
     * @brief Validate network time string format and content
     * @param timeStr Time string to validate
     * @return true if format is valid, false otherwise
     */
    bool isValidTime(const std::string &timeStr) override;

    /**
     * @brief Calculate sleep duration for night time power saving
     * @param currentTime Current time string for calculation
     * @return Sleep duration in microseconds, 0 if not night time
     */
    uint64_t getNightSleepDuration(const std::string &currentTime) override;

    // RTC-specific operations
    /**
     * @brief Initialize DS1307 RTC module and I2C communication
     * @return true if initialization successful, false otherwise
     */
    bool initialize();

    /**
     * @brief Set RTC time using time components structure
     * @param time Time components to set
     * @return true if time set successfully, false otherwise
     */
    bool setTime(const TimeComponents &time);

    /**
     * @brief Get current time as time components structure
     * @return Current time components
     */
    TimeComponents getTime();

    /**
     * @brief Get comprehensive time synchronization status
     * @return Time sync status with statistics and error information
     */
    TimeSyncStatus getSyncStatus() const;

    // Clock control operations
    /**
     * @brief Start the RTC oscillator
     * @return true if start successful, false otherwise
     */
    bool startClock();

    /**
     * @brief Stop the RTC oscillator
     * @return true if stop successful, false otherwise
     */
    bool stopClock();

    /**
     * @brief Check if RTC oscillator is running
     * @return true if clock is running, false if stopped
     */
    bool isClockRunning();

    // Square wave output control
    /**
     * @brief Configure square wave output pin
     * @param frequency Square wave frequency (1Hz, 4096Hz, 8192Hz, 32768Hz)
     * @param enabled Whether to enable square wave output
     * @return true if configuration successful, false otherwise
     */
    bool configureSquareWave(int frequency, bool enabled = true);

    /**
     * @brief Disable square wave output
     * @return true if disable successful, false otherwise
     */
    bool disableSquareWave();

    // EEPROM operations
    /**
     * @brief Save current time to EEPROM for power-loss recovery
     * @return true if save successful, false otherwise
     */
    bool saveTimeToEEPROM();

    /**
     * @brief Restore time from EEPROM after power loss
     * @return true if restore successful, false otherwise
     */
    bool setTimeFromEEPROM();

    /**
     * @brief Read data from DS1307 EEPROM
     * @param address EEPROM address to read (0-255)
     * @param buffer Buffer to store read data
     * @param length Number of bytes to read
     * @return Number of bytes actually read
     */
    size_t readEEPROM(uint8_t address, uint8_t *buffer, size_t length);

    /**
     * @brief Write data to DS1307 EEPROM
     * @param address EEPROM address to write (0-247, 248-255 reserved for time)
     * @param buffer Data to write
     * @param length Number of bytes to write
     * @return true if write successful, false otherwise
     */
    bool writeEEPROM(uint8_t address, const uint8_t *buffer, size_t length);

    // Time synchronization and drift management
    /**
     * @brief Synchronize RTC with network time and calculate drift
     * @param networkTime Network time string from cellular module
     * @return true if synchronization successful, false otherwise
     */
    bool synchronizeWithNetwork(const std::string &networkTime);

    /**
     * @brief Detect and measure clock drift against system time
     * @return Detected drift in milliseconds (positive = RTC fast, negative = RTC slow)
     */
    int64_t measureClockDrift();

    /**
     * @brief Apply drift correction to RTC time
     * @param driftMs Drift correction in milliseconds
     * @return true if correction applied successfully, false otherwise
     */
    bool applyDriftCorrection(int64_t driftMs);

    // Sleep schedule management
    /**
     * @brief Configure sleep schedule for power management
     * @param schedule Sleep schedule configuration
     */
    void setSleepSchedule(const SleepSchedule &schedule);

    /**
     * @brief Get current sleep schedule configuration
     * @return Current sleep schedule
     */
    SleepSchedule getSleepSchedule() const { return sleepSchedule_; }

    /**
     * @brief Check if current time is within night sleep period
     * @param currentTime Time to check (optional, uses current time if empty)
     * @return true if night time, false otherwise
     */
    bool isNightTime(const std::string &currentTime = "");

    /**
     * @brief Calculate next wakeup time based on schedule
     * @return Next wakeup time as ISO string
     */
    std::string getNextWakeupTime();

    // Time format conversion utilities
    /**
     * @brief Parse network time string to time components
     * @param networkTime Network time string "YY/MM/DD,HH:MM:SS+ZZ"
     * @return Parsed time components
     */
    TimeComponents parseNetworkTime(const std::string &networkTime);

    /**
     * @brief Parse ISO 8601 date/time string to time components
     * @param isoTime ISO time string "YYYY-MM-DDTHH:MM:SSZ"
     * @return Parsed time components
     */
    TimeComponents parseISOTime(const std::string &isoTime);

    /**
     * @brief Convert time components to ISO 8601 string
     * @param time Time components to convert
     * @return ISO 8601 formatted string
     */
    std::string formatISOTime(const TimeComponents &time);

    /**
     * @brief Convert month name to number
     * @param monthName Month name (Jan, Feb, etc.)
     * @return Month number (1-12), 0 if invalid
     */
    uint8_t monthNameToNumber(const std::string &monthName);

    // Validation and utility functions
    /**
     * @brief Validate time components for correctness
     * @param time Time components to validate
     * @return true if all components are valid, false otherwise
     */
    bool validateTimeComponents(const TimeComponents &time);

    /**
     * @brief Get system uptime in seconds
     * @return Uptime in seconds since last boot
     */
    uint64_t getUptimeSeconds();

    /**
     * @brief Get timestamp for file naming (safe for filesystem)
     * @return Timestamp string with underscores instead of colons
     */
    std::string getFilenameTimestamp();

    // Error handling and diagnostics
    /**
     * @brief Get last error message for diagnostics
     * @return String describing the last error that occurred
     */
    std::string getLastError() const { return lastError_; }

    /**
     * @brief Perform RTC health check and diagnostics
     * @return true if RTC appears healthy, false if issues detected
     */
    bool performHealthCheck();

private:
    // Hardware configuration
    uint8_t rtcAddress_;    ///< DS1307 I2C address
    uint8_t eepromAddress_; ///< DS1307 EEPROM I2C address
    bool initialized_;      ///< Initialization status

    // Time synchronization state
    TimeSyncStatus syncStatus_;                          ///< Current sync status
    SleepSchedule sleepSchedule_;                        ///< Current sleep schedule
    std::chrono::steady_clock::time_point lastSyncTime_; ///< Last sync timestamp
    int64_t accumulatedDrift_;                           ///< Accumulated drift in milliseconds

    // Thread safety and error handling
    mutable std::mutex timeMutex_; ///< Mutex for thread-safe operations
    std::string lastError_;        ///< Last error message

    // Platform-specific handle
    void *i2cHandle_; ///< Platform-specific I2C handle

    // Internal DS1307 operations
    /**
     * @brief Read register from DS1307
     * @param reg Register address to read
     * @param buffer Buffer to store read data
     * @param length Number of bytes to read
     * @return Number of bytes actually read
     */
    size_t readRegister(uint8_t reg, uint8_t *buffer, size_t length);

    /**
     * @brief Write register to DS1307
     * @param reg Register address to write
     * @param buffer Data to write
     * @param length Number of bytes to write
     * @return true if write successful, false otherwise
     */
    bool writeRegister(uint8_t reg, const uint8_t *buffer, size_t length);

    /**
     * @brief Convert binary value to BCD (Binary Coded Decimal)
     * @param value Binary value to convert
     * @return BCD encoded value
     */
    uint8_t binaryToBCD(uint8_t value);

    /**
     * @brief Convert BCD value to binary
     * @param value BCD value to convert
     * @return Binary decoded value
     */
    uint8_t bcdToBinary(uint8_t value);

    // Time calculation utilities
    /**
     * @brief Calculate day of week from date
     * @param year Full year (e.g., 2024)
     * @param month Month (1-12)
     * @param day Day of month (1-31)
     * @return Day of week (1-7, 1=Sunday)
     */
    int calculateDayOfWeek(int year, int month, int day);

    /**
     * @brief Check if year is a leap year
     * @param year Year to check
     * @return true if leap year, false otherwise
     */
    bool isLeapYear(int year);

    /**
     * @brief Get number of days in specified month
     * @param month Month (1-12)
     * @param year Year (for leap year calculation)
     * @return Number of days in month
     */
    int getDaysInMonth(int month, int year);

    // Statistics and monitoring
    /**
     * @brief Update synchronization statistics
     * @param success Whether sync was successful
     * @param source Source of time sync (Network/EEPROM/Manual)
     */
    void updateSyncStats(bool success, const std::string &source);

    /**
     * @brief Log time synchronization event
     * @param event Description of sync event
     * @param success Whether event was successful
     */
    void logSyncEvent(const std::string &event, bool success);

    // Platform-specific implementations
    /**
     * @brief Platform-specific I2C initialization
     * @return true if initialization successful, false otherwise
     */
    bool platformInitializeI2C();

    /**
     * @brief Platform-specific I2C cleanup
     */
    void platformCleanupI2C();

    // Constants
    static constexpr uint8_t DS1307_DEFAULT_ADDRESS = 0x68;          ///< Default DS1307 I2C address
    static constexpr uint8_t DS1307_EEPROM_ADDRESS = 0x50;           ///< DS1307 EEPROM I2C address
    static constexpr uint8_t DS1307_SECONDS_REG = 0x00;              ///< Seconds register
    static constexpr uint8_t DS1307_CONTROL_REG = 0x07;              ///< Control register
    static constexpr uint8_t DS1307_RAM_START = 0x08;                ///< RAM start address
    static constexpr uint8_t DS1307_CLOCK_HALT_BIT = 0x80;           ///< Clock halt bit
    static constexpr uint8_t EEPROM_TIME_BACKUP_ADDR = 248;          ///< EEPROM address for time backup
    static constexpr int DEFAULT_NIGHT_START_HOUR = 22;              ///< Default night start (10 PM)
    static constexpr int DEFAULT_NIGHT_END_HOUR = 6;                 ///< Default night end (6 AM)
    static constexpr uint64_t MICROSECONDS_PER_HOUR = 3600000000ULL; ///< Microseconds per hour
};