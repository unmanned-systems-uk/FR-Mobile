#pragma once

#include "interfaces.h"
#include <chrono>
#include <string>
#include <atomic>

/**
 * @file power_manager.h
 * @brief Power Manager for ESP32 sleep modes and peripheral control
 * @author Anthony Kirk
 * @date 2025-07-29
 * @version 1.0.0
 *
 * This class provides comprehensive power management for the ESP32 including
 * deep sleep modes, peripheral control, wakeup management, and power optimization.
 */

/**
 * @brief System reset reason enumeration
 */
enum class ResetReason
{
    POWER_ON = 0,      ///< Power-on reset
    EXTERNAL = 1,      ///< External reset pin
    SOFTWARE = 2,      ///< Software reset
    PANIC = 3,         ///< System panic reset
    INTERRUPT_WDT = 4, ///< Interrupt watchdog reset
    TASK_WDT = 5,      ///< Task watchdog reset
    OTHER_WDT = 6,     ///< Other watchdog reset
    DEEP_SLEEP = 7,    ///< Wake from deep sleep
    BROWNOUT = 8,      ///< Brownout reset
    SDIO = 9,          ///< SDIO reset
    UNKNOWN = 10       ///< Unknown reset reason
};

/**
 * @brief Deep sleep wakeup cause enumeration
 */
enum class WakeupCause
{
    UNDEFINED = 0, ///< Undefined or first boot
    ALL = 1,       ///< All wakeup sources
    EXT0 = 2,      ///< External signal on RTC_IO
    EXT1 = 3,      ///< External signal on RTC_CNTL
    TIMER = 4,     ///< Timer wakeup
    TOUCHPAD = 5,  ///< Touchpad wakeup
    ULP = 6,       ///< ULP program wakeup
    GPIO = 7,      ///< GPIO wakeup (light sleep only)
    UART = 8,      ///< UART wakeup (light sleep only)
    UNKNOWN = 9    ///< Unknown wakeup cause
};

/**
 * @brief Power consumption statistics and monitoring
 */
struct PowerStats
{
    uint64_t totalSleepTime = 0;       ///< Total time spent in sleep (microseconds)
    uint64_t totalActiveTime = 0;      ///< Total time spent active (microseconds)
    uint32_t sleepCycles = 0;          ///< Number of sleep cycles completed
    uint32_t wakeupEvents = 0;         ///< Number of wakeup events
    float averageSleepDuration = 0.0f; ///< Average sleep duration (seconds)
    float dutyCycle = 0.0f;            ///< Duty cycle percentage (active/total)
    std::string lastWakeupCause;       ///< Last wakeup cause description
    std::string lastResetReason;       ///< Last reset reason description
    uint64_t bootTime = 0;             ///< System boot timestamp
};

/**
 * @brief Peripheral power configuration
 */
struct PeripheralConfig
{
    bool wifi = false;       ///< WiFi radio power state
    bool bluetooth = false;  ///< Bluetooth radio power state
    bool cellular = false;   ///< Cellular module power state
    bool sdcard = false;     ///< SD card power state
    bool sensors = false;    ///< Sensor power state (I2C devices)
    bool external5V = false; ///< External 5V supply state
    bool rtc = true;         ///< RTC always enabled
    bool watchdog = true;    ///< Watchdog timer state
};

/**
 * @brief Power management configuration and limits
 */
struct PowerConfig
{
    uint64_t maxSleepDuration = 3600000000ULL; ///< Maximum sleep duration (1 hour)
    uint64_t minSleepDuration = 1000000ULL;    ///< Minimum sleep duration (1 second)
    float lowBatteryThreshold = 15.0f;         ///< Low battery threshold (%)
    float criticalBatteryThreshold = 5.0f;     ///< Critical battery threshold (%)
    bool emergencyShutdownEnabled = true;      ///< Emergency shutdown on critical battery
    uint32_t watchdogTimeoutMs = 300000;       ///< Watchdog timeout (5 minutes)
    int wakeupPin = -1;                        ///< GPIO pin for external wakeup (-1 = disabled)
    bool wakeupPinLevel = false;               ///< Wakeup pin active level (true = HIGH)
};

/**
 * @brief Power Manager implementing comprehensive power control and optimization
 *
 * This class provides complete power management for the ESP32-based forestry
 * research device. Features include:
 *
 * - Deep sleep modes with configurable wakeup sources
 * - Peripheral power control and optimization
 * - Battery-aware power management with emergency procedures
 * - Watchdog timer configuration and monitoring
 * - Wake/sleep cycle statistics and analysis
 * - GPIO hold states for deep sleep persistence
 * - Reset and wakeup cause analysis
 * - Power consumption monitoring and optimization
 */
class PowerManager : public IPowerManager
{
public:
    /**
     * @brief Constructor
     * @param powerPin GPIO pin for 5V peripheral power control (default: 15)
     */
    explicit PowerManager(int powerPin = 15);

    /**
     * @brief Destructor - ensures safe shutdown
     */
    ~PowerManager() override;

    // IPowerManager interface implementation
    /**
     * @brief Enable peripheral power supplies
     */
    void enablePeripherals() override;

    /**
     * @brief Disable peripheral power supplies for power saving
     */
    void disablePeripherals() override;

    /**
     * @brief Enter deep sleep mode for specified duration
     * @param sleepTimeUs Sleep duration in microseconds
     */
    void enterSleep(uint64_t sleepTimeUs) override;

    /**
     * @brief Get the cause of the last wakeup event
     * @return Wakeup cause enumeration value
     */
    WakeupCause getWakeupCause() override;

    /**
     * @brief Configure timer wakeup for next sleep cycle
     * @param timeUs Wakeup time in microseconds from now
     */
    void configureWakeup(uint64_t timeUs) override;

    // Power management initialization and control
    /**
     * @brief Initialize power management system
     * @return true if initialization successful, false otherwise
     */
    bool initialize();

    /**
     * @brief Shutdown system safely with proper cleanup
     * @param emergency Whether this is an emergency shutdown
     */
    void shutdown(bool emergency = false);

    /**
     * @brief Restart the ESP32 system
     * @param reason Reason for restart (for logging)
     */
    void restart(const std::string &reason = "Manual restart");

    // Peripheral control operations
    /**
     * @brief Control individual peripheral power
     * @param config Peripheral configuration structure
     */
    void configurePeripherals(const PeripheralConfig &config);

    /**
     * @brief Get current peripheral power configuration
     * @return Current peripheral configuration
     */
    PeripheralConfig getPeripheralConfig() const { return peripheralConfig_; }

    /**
     * @brief Enable/disable 5V external power supply
     * @param enabled Whether to enable 5V supply
     */
    void set5VSupply(bool enabled);

    /**
     * @brief Check if 5V supply is currently enabled
     * @return true if 5V supply is on, false otherwise
     */
    bool is5VSupplyEnabled() const;

    // Sleep mode operations
    /**
     * @brief Enter light sleep mode (CPU halted, peripherals active)
     * @param sleepTimeUs Sleep duration in microseconds
     * @return true if sleep completed normally, false if interrupted
     */
    bool enterLightSleep(uint64_t sleepTimeUs);

    /**
     * @brief Enter deep sleep mode with full power down
     * @param sleepTimeUs Sleep duration in microseconds
     * @param wakeupSources Bitmask of enabled wakeup sources
     */
    void enterDeepSleep(uint64_t sleepTimeUs, uint32_t wakeupSources = 0x01); // Timer only by default

    /**
     * @brief Configure external GPIO wakeup
     * @param pin GPIO pin number for wakeup
     * @param level Logic level for wakeup (true = HIGH, false = LOW)
     * @return true if configuration successful, false otherwise
     */
    bool configureGPIOWakeup(int pin, bool level);

    /**
     * @brief Disable all wakeup sources
     */
    void disableAllWakeupSources();

    // Watchdog timer operations
    /**
     * @brief Configure task watchdog timer
     * @param timeoutMs Timeout in milliseconds (0 = disable)
     * @return true if configuration successful, false otherwise
     */
    bool configureWatchdog(uint32_t timeoutMs);

    /**
     * @brief Reset watchdog timer to prevent timeout
     */
    void resetWatchdog();

    /**
     * @brief Enable watchdog timer
     * @return true if enable successful, false otherwise
     */
    bool enableWatchdog();

    /**
     * @brief Disable watchdog timer
     * @return true if disable successful, false otherwise
     */
    bool disableWatchdog();

    // System information and diagnostics
    /**
     * @brief Get system reset reason
     * @return Reset reason enumeration value
     */
    ResetReason getResetReason();

    /**
     * @brief Get comprehensive power statistics
     * @return Power statistics structure with usage data
     */
    PowerStats getPowerStats() const;

    /**
     * @brief Get system uptime in milliseconds
     * @return Uptime since last reset/boot
     */
    uint64_t getUptimeMs();

    /**
     * @brief Get free heap memory in bytes
     * @return Available heap memory
     */
    uint32_t getFreeHeap();

    /**
     * @brief Get minimum free heap since boot
     * @return Minimum heap that was available
     */
    uint32_t getMinFreeHeap();

    // Battery-aware power management
    /**
     * @brief Set power configuration including battery thresholds
     * @param config Power configuration structure
     */
    void setPowerConfig(const PowerConfig &config);

    /**
     * @brief Get current power configuration
     * @return Current power configuration
     */
    PowerConfig getPowerConfig() const { return powerConfig_; }

    /**
     * @brief Check battery level and apply power management policies
     * @param batteryPercent Current battery level (0-100%)
     * @return true if system should continue operation, false if shutdown needed
     */
    bool checkBatteryLevel(float batteryPercent);

    /**
     * @brief Enter emergency low power mode
     * @param reason Reason for emergency mode
     */
    void enterEmergencyMode(const std::string &reason);

    // GPIO hold and persistence
    /**
     * @brief Configure GPIO holds for deep sleep persistence
     */
    void configureGPIOHolds();

    /**
     * @brief Enable GPIO hold for specific pin
     * @param pin GPIO pin number
     * @param state State to maintain during sleep
     */
    void enableGPIOHold(int pin, bool state);

    /**
     * @brief Disable all GPIO holds
     */
    void disableAllGPIOHolds();

    // Error handling and recovery
    /**
     * @brief Get last error message for diagnostics
     * @return String describing the last error that occurred
     */
    std::string getLastError() const { return lastError_; }

    /**
     * @brief Perform power system health check
     * @return true if power system appears healthy, false if issues detected
     */
    bool performHealthCheck();

    // Utility functions
    /**
     * @brief Convert reset reason to human-readable string
     * @param reason Reset reason enumeration
     * @return Descriptive string for reset reason
     */
    static std::string resetReasonToString(ResetReason reason);

    /**
     * @brief Convert wakeup cause to human-readable string
     * @param cause Wakeup cause enumeration
     * @return Descriptive string for wakeup cause
     */
    static std::string wakeupCauseToString(WakeupCause cause);

private:
    // Hardware configuration
    int powerPin_;     ///< GPIO pin for 5V power control
    bool initialized_; ///< Initialization status

    // Power state tracking
    PeripheralConfig peripheralConfig_; ///< Current peripheral configuration
    PowerConfig powerConfig_;           ///< Current power configuration
    PowerStats powerStats_;             ///< Power usage statistics
    std::atomic<bool> emergencyMode_;   ///< Emergency mode flag

    // Timing and statistics
    std::chrono::steady_clock::time_point bootTime_;      ///< System boot time
    std::chrono::steady_clock::time_point lastSleepTime_; ///< Last sleep entry time
    std::chrono::steady_clock::time_point lastWakeTime_;  ///< Last wake time
    uint64_t accumulatedSleepTime_;                       ///< Total accumulated sleep time
    uint64_t accumulatedActiveTime_;                      ///< Total accumulated active time

    // Error handling
    std::string lastError_; ///< Last error message

    // Platform-specific handles
    void *watchdogHandle_; ///< Platform-specific watchdog handle
    void *powerHandle_;    ///< Platform-specific power management handle

    // Internal power control methods
    /**
     * @brief Platform-specific peripheral power control
     * @param peripheral Peripheral identifier
     * @param enabled Whether to enable the peripheral
     * @return true if control successful, false otherwise
     */
    bool controlPeripheralPower(const std::string &peripheral, bool enabled);

    /**
     * @brief Update power usage statistics
     * @param operation Type of operation performed
     * @param duration Duration of operation in microseconds
     */
    void updatePowerStats(const std::string &operation, uint64_t duration);

    /**
     * @brief Calculate and update duty cycle statistics
     */
    void updateDutyCycle();

    // Sleep preparation and recovery
    /**
     * @brief Prepare system for sleep mode
     * @param deepSleep Whether preparing for deep sleep (true) or light sleep (false)
     */
    void prepareSleep(bool deepSleep);

    /**
     * @brief Restore system after waking from sleep
     * @param fromDeepSleep Whether waking from deep sleep
     */
    void restoreFromSleep(bool fromDeepSleep);

    /**
     * @brief Validate sleep duration against configuration limits
     * @param sleepTimeUs Requested sleep duration
     * @return Validated sleep duration within limits
     */
    uint64_t validateSleepDuration(uint64_t sleepTimeUs);

    // Platform-specific implementations
    /**
     * @brief Platform-specific power management initialization
     * @return true if initialization successful, false otherwise
     */
    bool platformInitialize();

    /**
     * @brief Platform-specific deep sleep entry
     * @param sleepTimeUs Sleep duration in microseconds
     */
    void platformEnterDeepSleep(uint64_t sleepTimeUs);

    /**
     * @brief Platform-specific light sleep entry
     * @param sleepTimeUs Sleep duration in microseconds
     * @return true if sleep completed normally, false if interrupted
     */
    bool platformEnterLightSleep(uint64_t sleepTimeUs);

    /**
     * @brief Platform-specific GPIO configuration for deep sleep
     */
    void platformConfigureGPIOForSleep();

    /**
     * @brief Platform-specific system restart
     */
    void platformRestart();

    // Watchdog implementations
    /**
     * @brief Platform-specific watchdog initialization
     * @param timeoutMs Timeout in milliseconds
     * @return true if initialization successful, false otherwise
     */
    bool platformInitializeWatchdog(uint32_t timeoutMs);

    /**
     * @brief Platform-specific watchdog reset
     */
    void platformResetWatchdog();

    /**
     * @brief Platform-specific watchdog cleanup
     */
    void platformCleanupWatchdog();

    // System information retrieval
    /**
     * @brief Platform-specific reset reason detection
     * @return Reset reason enumeration
     */
    ResetReason platformGetResetReason();

    /**
     * @brief Platform-specific wakeup cause detection
     * @return Wakeup cause enumeration
     */
    WakeupCause platformGetWakeupCause();

    /**
     * @brief Platform-specific memory information
     * @param freeHeap Pointer to store free heap size
     * @param minFreeHeap Pointer to store minimum free heap size
     */
    void platformGetMemoryInfo(uint32_t *freeHeap, uint32_t *minFreeHeap);

    // Emergency procedures
    /**
     * @brief Execute emergency shutdown sequence
     * @param reason Reason for emergency shutdown
     */
    void executeEmergencyShutdown(const std::string &reason);

    /**
     * @brief Log critical power event
     * @param event Description of power event
     * @param data Additional data about the event
     */
    void logCriticalPowerEvent(const std::string &event, const std::string &data = "");

    // Constants and configuration
    static constexpr int DEFAULT_POWER_PIN = 15;                        ///< Default 5V power pin
    static constexpr uint32_t DEFAULT_WATCHDOG_TIMEOUT = 300000;        ///< Default watchdog timeout (5min)
    static constexpr uint64_t MIN_SLEEP_DURATION = 1000000ULL;          ///< Minimum sleep (1 second)
    static constexpr uint64_t MAX_SLEEP_DURATION = 3600000000ULL;       ///< Maximum sleep (1 hour)
    static constexpr float DEFAULT_LOW_BATTERY = 15.0f;                 ///< Default low battery threshold
    static constexpr float DEFAULT_CRITICAL_BATTERY = 5.0f;             ///< Default critical battery threshold
    static constexpr uint64_t EMERGENCY_SLEEP_DURATION = 3600000000ULL; ///< Emergency sleep duration (1 hour)

    // ESP32-specific wakeup source bits (for ESP32 platform)
    static constexpr uint32_t WAKEUP_TIMER = 0x01;    ///< Timer wakeup bit
    static constexpr uint32_t WAKEUP_EXT0 = 0x02;     ///< EXT0 wakeup bit
    static constexpr uint32_t WAKEUP_EXT1 = 0x04;     ///< EXT1 wakeup bit
    static constexpr uint32_t WAKEUP_TOUCHPAD = 0x08; ///< Touchpad wakeup bit
    static constexpr uint32_t WAKEUP_ULP = 0x10;      ///< ULP wakeup bit
};