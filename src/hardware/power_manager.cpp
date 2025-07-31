#include "hardware/power_manager.h"
#include "utils/logger.h"
#include "main.h" // For Config::Pins namespace
#include <thread>
#include <sstream>
#include <iomanip>

#ifdef ESP32_PLATFORM
#include <esp_sleep.h>
#include <esp_system.h>
#include <esp_task_wdt.h>
#include <driver/gpio.h>
#include <driver/rtc_io.h>
#include <rom/rtc.h>
#include <esp_pm.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#elif defined(RASPBERRY_PI_PLATFORM)
// Raspberry Pi power management includes
#include <linux/gpio.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <unistd.h>
#include <chrono>
#include <thread>
#else
// Generic development platform
#include <chrono>
#include <thread>
#endif

// Constructor
PowerManager::PowerManager(int powerPin)
    : powerPin_(powerPin), initialized_(false), emergencyMode_(false), accumulatedSleepTime_(0), accumulatedActiveTime_(0), watchdogHandle_(nullptr), powerHandle_(nullptr)
{
    // Initialize default configurations
    peripheralConfig_ = PeripheralConfig();
    powerConfig_ = PowerConfig();
    powerConfig_.maxSleepDuration = MAX_SLEEP_DURATION;
    powerConfig_.minSleepDuration = MIN_SLEEP_DURATION;
    powerConfig_.lowBatteryThreshold = DEFAULT_LOW_BATTERY;
    powerConfig_.criticalBatteryThreshold = DEFAULT_CRITICAL_BATTERY;
    powerConfig_.emergencyShutdownEnabled = true;
    powerConfig_.watchdogTimeoutMs = DEFAULT_WATCHDOG_TIMEOUT;

    bootTime_ = std::chrono::steady_clock::now();
    lastWakeTime_ = bootTime_;

    LOG_INFO("[PowerManager] Created with 5V control on GPIO " + std::to_string(powerPin_));
}

// Destructor
PowerManager::~PowerManager()
{
    if (initialized_)
    {
        LOG_INFO("[PowerManager] Shutting down power management system");

        // Disable watchdog
        if (watchdogHandle_)
        {
            disableWatchdog();
        }

        // Ensure peripherals are properly shut down
        disablePeripherals();

        // Platform cleanup
        platformCleanupWatchdog();
    }
}

// Initialize power management system
bool PowerManager::initialize()
{
    if (initialized_)
    {
        LOG_WARNING("[PowerManager] Already initialized");
        return true;
    }

    LOG_INFO("[PowerManager] Initializing power management system");

    try
    {
        // Platform-specific initialization
        if (!platformInitialize())
        {
            lastError_ = "Platform initialization failed";
            LOG_ERROR("[PowerManager] " + lastError_);
            return false;
        }

        // Initialize GPIO for 5V control
        // CRITICAL: Start with 5V disabled (HIGH = disabled)
#ifdef ESP32_PLATFORM
        gpio_config_t io_conf = {};
        io_conf.intr_type = GPIO_INTR_DISABLE;
        io_conf.mode = GPIO_MODE_OUTPUT;
        io_conf.pin_bit_mask = (1ULL << powerPin_);
        io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
        io_conf.pull_up_en = GPIO_PULLUP_ENABLE; // Pull high to keep disabled by default

        esp_err_t ret = gpio_config(&io_conf);
        if (ret != ESP_OK)
        {
            lastError_ = "Failed to configure 5V control GPIO";
            LOG_ERROR("[PowerManager] " + lastError_);
            return false;
        }

        // Set HIGH initially to keep 5V disabled
        gpio_set_level(static_cast<gpio_num_t>(powerPin_), 1);

        // Disable deep sleep hold initially
        gpio_deep_sleep_hold_dis();
        gpio_hold_dis(static_cast<gpio_num_t>(powerPin_));
#else
        // Development platform - just log the state
        LOG_DEBUG("[PowerManager] Development platform - GPIO" + std::to_string(powerPin_) +
                  " configured for 5V control (simulated)");
#endif

        // Get initial system information
        ResetReason resetReason = getResetReason();
        powerStats_.lastResetReason = resetReasonToString(resetReason);
        LOG_INFO("[PowerManager] System reset reason: " + powerStats_.lastResetReason);

        if (resetReason == ResetReason::DEEP_SLEEP)
        {
            WakeupCause wakeupCause = getWakeupCause();
            powerStats_.lastWakeupCause = wakeupCauseToString(wakeupCause);
            LOG_INFO("[PowerManager] Wakeup cause: " + powerStats_.lastWakeupCause);
            powerStats_.wakeupEvents++;
        }

        // Initialize watchdog if configured
        if (powerConfig_.watchdogTimeoutMs > 0)
        {
            if (!configureWatchdog(powerConfig_.watchdogTimeoutMs))
            {
                LOG_WARNING("[PowerManager] Failed to initialize watchdog timer");
            }
        }

        // Record boot time
        powerStats_.bootTime = std::chrono::duration_cast<std::chrono::seconds>(
                                   std::chrono::system_clock::now().time_since_epoch())
                                   .count();

        initialized_ = true;
        LOG_INFO("[PowerManager] Initialization complete");

        // Log initial memory status
        uint32_t freeHeap = getFreeHeap();
        uint32_t minFreeHeap = getMinFreeHeap();
        LOG_INFO("[PowerManager] Memory status - Free: " + std::to_string(freeHeap) +
                 " bytes, Min free: " + std::to_string(minFreeHeap) + " bytes");

        return true;
    }
    catch (const std::exception &e)
    {
        lastError_ = std::string("Exception during initialization: ") + e.what();
        LOG_ERROR("[PowerManager] " + lastError_);
        return false;
    }
}

// Enable peripheral power supplies
void PowerManager::enablePeripherals()
{
    LOG_INFO("[PowerManager] Enabling peripheral power supplies");

    if (!initialized_)
    {
        LOG_ERROR("[PowerManager] Not initialized - cannot enable peripherals");
        return;
    }

    // Enable 5V supply first (needed for RTC, cellular, etc.)
    set5VSupply(true);

    // Wait for power stabilization
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));

    // Enable other peripherals based on configuration
    if (peripheralConfig_.wifi)
    {
        controlPeripheralPower("wifi", true);
    }

    if (peripheralConfig_.bluetooth)
    {
        controlPeripheralPower("bluetooth", true);
    }

    if (peripheralConfig_.cellular)
    {
        controlPeripheralPower("cellular", true);
    }

    if (peripheralConfig_.sdcard)
    {
        controlPeripheralPower("sdcard", true);
    }

    if (peripheralConfig_.sensors)
    {
        controlPeripheralPower("sensors", true);
    }

    LOG_INFO("[PowerManager] All configured peripherals enabled");
}

// Disable peripheral power supplies
void PowerManager::disablePeripherals()
{
    LOG_INFO("[PowerManager] Disabling peripheral power supplies");

    if (!initialized_)
    {
        LOG_WARNING("[PowerManager] Not initialized - peripherals may already be disabled");
        return;
    }

    // Disable peripherals in reverse order
    if (peripheralConfig_.sensors)
    {
        controlPeripheralPower("sensors", false);
    }

    if (peripheralConfig_.sdcard)
    {
        controlPeripheralPower("sdcard", false);
    }

    if (peripheralConfig_.cellular)
    {
        controlPeripheralPower("cellular", false);
    }

    if (peripheralConfig_.bluetooth)
    {
        controlPeripheralPower("bluetooth", false);
    }

    if (peripheralConfig_.wifi)
    {
        controlPeripheralPower("wifi", false);
    }

    // Disable 5V supply last (after all peripherals are off)
    set5VSupply(false);

    LOG_INFO("[PowerManager] All peripherals disabled");
}

// Enable/disable 5V external power supply
void PowerManager::set5VSupply(bool enabled)
{
    LOG_INFO("[PowerManager] " + std::string(enabled ? "Enabling" : "Disabling") + " 5V supply");

#ifdef ESP32_PLATFORM
    // Disable any existing hold on the pin
    gpio_hold_dis(static_cast<gpio_num_t>(powerPin_));
    gpio_deep_sleep_hold_dis();

    // Set the pin state (LOW = enabled, HIGH = disabled)
    gpio_set_level(static_cast<gpio_num_t>(powerPin_), enabled ? 0 : 1);

    // If we're enabling the 5V supply and might go to sleep, ensure it stays on
    if (enabled)
    {
        // Enable hold to maintain state during deep sleep
        gpio_hold_en(static_cast<gpio_num_t>(powerPin_));
        gpio_deep_sleep_hold_en();
    }
#else
    // Development platform - just track the state
    LOG_DEBUG("[PowerManager] Development platform - 5V supply " +
              std::string(enabled ? "enabled" : "disabled") + " (simulated)");
#endif

    peripheralConfig_.external5V = enabled;

    // Log the change
    if (enabled)
    {
        LOG_INFO("[PowerManager] 5V supply enabled - peripherals can now be powered");
    }
    else
    {
        LOG_INFO("[PowerManager] 5V supply disabled - all 5V peripherals are now off");
    }
}

// Check if 5V supply is currently enabled
bool PowerManager::is5VSupplyEnabled() const
{
#ifdef ESP32_PLATFORM
    // Read actual GPIO state (LOW = enabled, HIGH = disabled)
    int level = gpio_get_level(static_cast<gpio_num_t>(powerPin_));
    return (level == 0);
#else
    // Development platform - return tracked state
    return peripheralConfig_.external5V;
#endif
}

// Enter deep sleep mode
void PowerManager::enterSleep(uint64_t sleepTimeUs)
{
    // Use deep sleep by default
    enterDeepSleep(sleepTimeUs, WAKEUP_TIMER);
}

// Enter deep sleep mode with full power down
void PowerManager::enterDeepSleep(uint64_t sleepTimeUs, uint32_t wakeupSources)
{
    if (!initialized_)
    {
        LOG_ERROR("[PowerManager] Not initialized - cannot enter deep sleep");
        return;
    }

    // Validate and adjust sleep duration
    sleepTimeUs = validateSleepDuration(sleepTimeUs);

    LOG_INFO("[PowerManager] Entering deep sleep for " +
             std::to_string(sleepTimeUs / 1000000) + " seconds");

    // Update statistics
    lastSleepTime_ = std::chrono::steady_clock::now();
    auto activeDuration = std::chrono::duration_cast<std::chrono::microseconds>(
                              lastSleepTime_ - lastWakeTime_)
                              .count();
    accumulatedActiveTime_ += activeDuration;
    powerStats_.sleepCycles++;

    // Prepare system for sleep
    prepareSleep(true);

    // Configure wakeup sources
    if (wakeupSources & WAKEUP_TIMER)
    {
        configureWakeup(sleepTimeUs);
    }

    // Platform-specific deep sleep entry
    platformEnterDeepSleep(sleepTimeUs);

    // This code runs after waking up
    lastWakeTime_ = std::chrono::steady_clock::now();
    accumulatedSleepTime_ += sleepTimeUs;
    powerStats_.wakeupEvents++;

    // Restore system after sleep
    restoreFromSleep(true);

    // Update statistics
    updateDutyCycle();

    LOG_INFO("[PowerManager] Woke from deep sleep");
}

// Enter light sleep mode
bool PowerManager::enterLightSleep(uint64_t sleepTimeUs)
{
    if (!initialized_)
    {
        LOG_ERROR("[PowerManager] Not initialized - cannot enter light sleep");
        return false;
    }

    // Validate and adjust sleep duration
    sleepTimeUs = validateSleepDuration(sleepTimeUs);

    LOG_INFO("[PowerManager] Entering light sleep for " +
             std::to_string(sleepTimeUs / 1000000) + " seconds");

    // Update statistics
    lastSleepTime_ = std::chrono::steady_clock::now();
    auto activeDuration = std::chrono::duration_cast<std::chrono::microseconds>(
                              lastSleepTime_ - lastWakeTime_)
                              .count();
    accumulatedActiveTime_ += activeDuration;

    // Prepare system for sleep
    prepareSleep(false);

    // Platform-specific light sleep entry
    bool normalWakeup = platformEnterLightSleep(sleepTimeUs);

    // This code runs after waking up
    lastWakeTime_ = std::chrono::steady_clock::now();
    auto actualSleepTime = std::chrono::duration_cast<std::chrono::microseconds>(
                               lastWakeTime_ - lastSleepTime_)
                               .count();
    accumulatedSleepTime_ += actualSleepTime;

    // Restore system after sleep
    restoreFromSleep(false);

    // Update statistics
    updateDutyCycle();

    if (normalWakeup)
    {
        LOG_INFO("[PowerManager] Woke from light sleep normally");
    }
    else
    {
        LOG_WARNING("[PowerManager] Light sleep interrupted");
    }

    return normalWakeup;
}

// Configure timer wakeup
void PowerManager::configureWakeup(uint64_t timeUs)
{
#ifdef ESP32_PLATFORM
    esp_err_t ret = esp_sleep_enable_timer_wakeup(timeUs);
    if (ret != ESP_OK)
    {
        LOG_ERROR("[PowerManager] Failed to configure timer wakeup");
    }
    else
    {
        LOG_DEBUG("[PowerManager] Timer wakeup configured for " +
                  std::to_string(timeUs / 1000000) + " seconds");
    }
#else
    LOG_DEBUG("[PowerManager] Development platform - timer wakeup configured for " +
              std::to_string(timeUs / 1000000) + " seconds (simulated)");
#endif
}

// Get wakeup cause
WakeupCause PowerManager::getWakeupCause()
{
    return platformGetWakeupCause();
}

// Get reset reason
ResetReason PowerManager::getResetReason()
{
    return platformGetResetReason();
}

// Configure peripheral power states
void PowerManager::configurePeripherals(const PeripheralConfig &config)
{
    LOG_INFO("[PowerManager] Configuring peripheral power states");

    // Update WiFi state
    if (config.wifi != peripheralConfig_.wifi)
    {
        controlPeripheralPower("wifi", config.wifi);
    }

    // Update Bluetooth state
    if (config.bluetooth != peripheralConfig_.bluetooth)
    {
        controlPeripheralPower("bluetooth", config.bluetooth);
    }

    // Update cellular state
    if (config.cellular != peripheralConfig_.cellular)
    {
        controlPeripheralPower("cellular", config.cellular);
    }

    // Update SD card state
    if (config.sdcard != peripheralConfig_.sdcard)
    {
        controlPeripheralPower("sdcard", config.sdcard);
    }

    // Update sensor state
    if (config.sensors != peripheralConfig_.sensors)
    {
        controlPeripheralPower("sensors", config.sensors);
    }

    // Update 5V supply state
    if (config.external5V != peripheralConfig_.external5V)
    {
        set5VSupply(config.external5V);
    }

    // Save new configuration
    peripheralConfig_ = config;

    LOG_INFO("[PowerManager] Peripheral configuration updated");
}

// Shutdown system safely
void PowerManager::shutdown(bool emergency)
{
    if (emergency)
    {
        LOG_CRITICAL("[PowerManager] Emergency shutdown initiated");
        executeEmergencyShutdown("Emergency shutdown requested");
    }
    else
    {
        LOG_INFO("[PowerManager] Normal shutdown initiated");

        // Disable all peripherals
        disablePeripherals();

        // Configure GPIO holds for shutdown
        configureGPIOHolds();

        // Enter deep sleep indefinitely
        LOG_INFO("[PowerManager] Entering indefinite deep sleep");
        enterDeepSleep(0xFFFFFFFFFFFFFFFFULL, 0); // No wakeup sources
    }
}

// Restart system
void PowerManager::restart(const std::string &reason)
{
    LOG_INFO("[PowerManager] System restart requested: " + reason);

    // Log the restart event
    logCriticalPowerEvent("System restart", reason);

    // Ensure logs are flushed
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // Platform-specific restart
    platformRestart();
}

// Configure watchdog timer
bool PowerManager::configureWatchdog(uint32_t timeoutMs)
{
    LOG_INFO("[PowerManager] Configuring watchdog timer with " +
             std::to_string(timeoutMs) + "ms timeout");

    powerConfig_.watchdogTimeoutMs = timeoutMs;

    if (timeoutMs == 0)
    {
        // Disable watchdog
        return disableWatchdog();
    }

    // Initialize watchdog with new timeout
    return platformInitializeWatchdog(timeoutMs);
}

// Reset watchdog timer
void PowerManager::resetWatchdog()
{
    if (watchdogHandle_ && powerConfig_.watchdogTimeoutMs > 0)
    {
        platformResetWatchdog();
    }
}

// Enable watchdog timer
bool PowerManager::enableWatchdog()
{
    if (powerConfig_.watchdogTimeoutMs == 0)
    {
        LOG_WARNING("[PowerManager] Cannot enable watchdog - no timeout configured");
        return false;
    }

    return platformInitializeWatchdog(powerConfig_.watchdogTimeoutMs);
}

// Disable watchdog timer
bool PowerManager::disableWatchdog()
{
    LOG_INFO("[PowerManager] Disabling watchdog timer");

#ifdef ESP32_PLATFORM
    esp_err_t ret = esp_task_wdt_deinit();
    if (ret == ESP_OK)
    {
        watchdogHandle_ = nullptr;
        return true;
    }
    else
    {
        LOG_ERROR("[PowerManager] Failed to disable watchdog");
        return false;
    }
#else
    watchdogHandle_ = nullptr;
    return true;
#endif
}

// Check if cellular network is available
bool PowerManager::isCellularNetworkAvailable()
{
#ifdef ESP32_PLATFORM
    if (!peripheralConfig_.cellular)
    {
        LOG_WARNING("[PowerManager] Cellular not powered - cannot check network");
        return false;
    }

    // Sample NET pin multiple times to detect blinking
    int highCount = 0;
    int lowCount = 0;

    for (int i = 0; i < 10; i++)
    {
        if (gpio_get_level(static_cast<gpio_num_t>(Config::Pins::CELL_NET)) == 1)
            highCount++;
        else
            lowCount++;

        vTaskDelay(100 / portTICK_PERIOD_MS);
    }

    // If we see both high and low states, it's blinking (network available)
    bool hasNetwork = (highCount > 0 && lowCount > 0);

    LOG_DEBUG("[PowerManager] Cellular network " +
              std::string(hasNetwork ? "available" : "not available") +
              " (high: " + std::to_string(highCount) +
              ", low: " + std::to_string(lowCount) + ")");

    return hasNetwork;
#else
    // Development platform - simulate network available
    return peripheralConfig_.cellular;
#endif
}

// Get power statistics
PowerStats PowerManager::getPowerStats() const
{
    PowerStats stats = powerStats_;

    // Calculate current session stats
    auto now = std::chrono::steady_clock::now();
    auto sessionDuration = std::chrono::duration_cast<std::chrono::microseconds>(
                               now - bootTime_)
                               .count();

    // Update totals
    stats.totalActiveTime = accumulatedActiveTime_;
    stats.totalSleepTime = accumulatedSleepTime_;

    // Calculate averages
    if (stats.sleepCycles > 0)
    {
        stats.averageSleepDuration = static_cast<float>(stats.totalSleepTime) /
                                     static_cast<float>(stats.sleepCycles) / 1000000.0f;
    }

    // Calculate duty cycle
    uint64_t totalTime = stats.totalActiveTime + stats.totalSleepTime;
    if (totalTime > 0)
    {
        stats.dutyCycle = (static_cast<float>(stats.totalActiveTime) /
                           static_cast<float>(totalTime)) *
                          100.0f;
    }

    return stats;
}

// Get system uptime
uint64_t PowerManager::getUptimeMs()
{
    auto now = std::chrono::steady_clock::now();
    auto uptime = std::chrono::duration_cast<std::chrono::milliseconds>(
                      now - bootTime_)
                      .count();
    return static_cast<uint64_t>(uptime);
}

// Get free heap memory
uint32_t PowerManager::getFreeHeap()
{
    uint32_t freeHeap = 0;
    uint32_t minFreeHeap = 0;
    platformGetMemoryInfo(&freeHeap, &minFreeHeap);
    return freeHeap;
}

// Get minimum free heap
uint32_t PowerManager::getMinFreeHeap()
{
    uint32_t freeHeap = 0;
    uint32_t minFreeHeap = 0;
    platformGetMemoryInfo(&freeHeap, &minFreeHeap);
    return minFreeHeap;
}

// Check battery level and apply policies
bool PowerManager::checkBatteryLevel(float batteryPercent)
{
    LOG_DEBUG("[PowerManager] Checking battery level: " +
              std::to_string(batteryPercent) + "%");

    // Critical battery check
    if (batteryPercent <= powerConfig_.criticalBatteryThreshold)
    {
        LOG_CRITICAL("[PowerManager] Critical battery level detected: " +
                     std::to_string(batteryPercent) + "%");

        if (powerConfig_.emergencyShutdownEnabled)
        {
            enterEmergencyMode("Critical battery level");
            return false; // System should shut down
        }
    }
    // Low battery check
    else if (batteryPercent <= powerConfig_.lowBatteryThreshold)
    {
        LOG_WARNING("[PowerManager] Low battery level: " +
                    std::to_string(batteryPercent) + "%");

        // Apply power saving measures
        PeripheralConfig lowPowerConfig = peripheralConfig_;
        lowPowerConfig.wifi = false;
        lowPowerConfig.bluetooth = false;
        configurePeripherals(lowPowerConfig);
    }

    return true; // Continue operation
}

// Enter emergency mode
void PowerManager::enterEmergencyMode(const std::string &reason)
{
    if (emergencyMode_)
    {
        LOG_WARNING("[PowerManager] Already in emergency mode");
        return;
    }

    LOG_CRITICAL("[PowerManager] Entering emergency mode: " + reason);
    emergencyMode_ = true;

    // Log the emergency event
    logCriticalPowerEvent("Emergency mode activated", reason);

    // Disable all non-essential peripherals
    PeripheralConfig emergencyConfig;
    emergencyConfig.external5V = false; // Turn off 5V to save power
    emergencyConfig.wifi = false;
    emergencyConfig.bluetooth = false;
    emergencyConfig.cellular = false;
    emergencyConfig.sdcard = false;
    emergencyConfig.sensors = false;
    emergencyConfig.rtc = true;      // Keep RTC if possible
    emergencyConfig.watchdog = true; // Keep watchdog active

    configurePeripherals(emergencyConfig);

    // Enter long sleep
    LOG_CRITICAL("[PowerManager] Entering emergency sleep for 1 hour");
    enterDeepSleep(EMERGENCY_SLEEP_DURATION, WAKEUP_TIMER);
}

// Configure GPIO holds for deep sleep
void PowerManager::configureGPIOHolds()
{
#ifdef ESP32_PLATFORM
    // Configure hold for 5V control pin if it's enabled
    if (is5VSupplyEnabled())
    {
        gpio_hold_en(static_cast<gpio_num_t>(powerPin_));
        gpio_deep_sleep_hold_en();
        LOG_DEBUG("[PowerManager] GPIO hold enabled for 5V supply pin");
    }
#else
    LOG_DEBUG("[PowerManager] Development platform - GPIO holds configured (simulated)");
#endif
}

// Enable GPIO hold for specific pin
void PowerManager::enableGPIOHold(int pin, bool state)
{
#ifdef ESP32_PLATFORM
    gpio_set_level(static_cast<gpio_num_t>(pin), state ? 1 : 0);
    gpio_hold_en(static_cast<gpio_num_t>(pin));
    LOG_DEBUG("[PowerManager] GPIO" + std::to_string(pin) + " hold enabled at " +
              std::string(state ? "HIGH" : "LOW"));
#else
    LOG_DEBUG("[PowerManager] Development platform - GPIO" + std::to_string(pin) +
              " hold enabled (simulated)");
#endif
}

// Disable all GPIO holds
void PowerManager::disableAllGPIOHolds()
{
#ifdef ESP32_PLATFORM
    gpio_deep_sleep_hold_dis();
    // Disable hold on power pin
    gpio_hold_dis(static_cast<gpio_num_t>(powerPin_));
    LOG_DEBUG("[PowerManager] All GPIO holds disabled");
#else
    LOG_DEBUG("[PowerManager] Development platform - GPIO holds disabled (simulated)");
#endif
}

// Perform health check
bool PowerManager::performHealthCheck()
{
    LOG_DEBUG("[PowerManager] Performing system health check");

    bool healthy = true;

    // Check memory
    uint32_t freeHeap = getFreeHeap();
    if (freeHeap < 10000) // Less than 10KB free
    {
        LOG_ERROR("[PowerManager] Low memory detected: " +
                  std::to_string(freeHeap) + " bytes free");
        healthy = false;
    }

    // Check 5V supply consistency
    bool expected5V = peripheralConfig_.external5V;
    bool actual5V = is5VSupplyEnabled();
    if (expected5V != actual5V)
    {
        LOG_ERROR("[PowerManager] 5V supply state mismatch - expected: " +
                  std::string(expected5V ? "ON" : "OFF") + ", actual: " +
                  std::string(actual5V ? "ON" : "OFF"));
        healthy = false;
    }

    // Check emergency mode
    if (emergencyMode_)
    {
        LOG_WARNING("[PowerManager] System is in emergency mode");
        healthy = false;
    }

    if (healthy)
    {
        LOG_INFO("[PowerManager] System health check passed");
    }
    else
    {
        LOG_WARNING("[PowerManager] System health check detected issues");
    }

    return healthy;
}

// Private helper methods

// Platform-specific peripheral power control
bool PowerManager::controlPeripheralPower(const std::string &peripheral, bool enabled)
{
    LOG_DEBUG("[PowerManager] " + std::string(enabled ? "Enabling" : "Disabling") +
              " " + peripheral);

#ifdef ESP32_PLATFORM
    if (peripheral == "wifi")
    {
        if (enabled)
        {
            esp_err_t ret = esp_wifi_start();
            return (ret == ESP_OK);
        }
        else
        {
            esp_err_t ret = esp_wifi_stop();
            return (ret == ESP_OK);
        }
    }
    else if (peripheral == "bluetooth")
    {
        // Bluetooth control would go here
        return true;
    }
    else if (peripheral == "cellular")
    {
        return controlCellularPower(enabled);
    }
    // Other peripherals...
#endif

    // Update configuration
    if (peripheral == "wifi")
        peripheralConfig_.wifi = enabled;
    else if (peripheral == "bluetooth")
        peripheralConfig_.bluetooth = enabled;
    else if (peripheral == "cellular")
        peripheralConfig_.cellular = enabled;
    else if (peripheral == "sdcard")
        peripheralConfig_.sdcard = enabled;
    else if (peripheral == "sensors")
        peripheralConfig_.sensors = enabled;

    return true;
}

// Control SIM7600 cellular modem power
bool PowerManager::controlCellularPower(bool enabled)
{
#ifdef ESP32_PLATFORM
    if (!is5VSupplyEnabled())
    {
        LOG_ERROR("[PowerManager] Cannot control cellular - 5V supply is off");
        return false;
    }

    if (enabled)
    {
        LOG_INFO("[PowerManager] Powering on SIM7600 cellular modem");

        // Configure control pins
        gpio_set_direction(static_cast<gpio_num_t>(Config::Pins::CELL_PWK), GPIO_MODE_OUTPUT);
        gpio_set_direction(static_cast<gpio_num_t>(Config::Pins::CELL_RST), GPIO_MODE_OUTPUT);
        gpio_set_direction(static_cast<gpio_num_t>(Config::Pins::CELL_DTR), GPIO_MODE_OUTPUT);
        gpio_set_direction(static_cast<gpio_num_t>(Config::Pins::CELL_NET), GPIO_MODE_INPUT);

        // Ensure reset is high (not in reset)
        gpio_set_level(static_cast<gpio_num_t>(Config::Pins::CELL_RST), 1);

        // Set DTR low to enable normal operation (high = sleep mode)
        gpio_set_level(static_cast<gpio_num_t>(Config::Pins::CELL_DTR), 0);

        // Pulse PWRKEY low for 1.2 seconds to power on
        gpio_set_level(static_cast<gpio_num_t>(Config::Pins::CELL_PWK), 1);
        vTaskDelay(100 / portTICK_PERIOD_MS);
        gpio_set_level(static_cast<gpio_num_t>(Config::Pins::CELL_PWK), 0);
        vTaskDelay(Config::Timing::CELL_PWRKEY_PULSE_MS / portTICK_PERIOD_MS);
        gpio_set_level(static_cast<gpio_num_t>(Config::Pins::CELL_PWK), 1);

        // Wait for modem to start and check NET pin
        LOG_INFO("[PowerManager] Waiting for cellular network signal...");
        uint32_t startTime = xTaskGetTickCount();
        bool networkFound = false;

        while ((xTaskGetTickCount() - startTime) < (Config::Timing::CELL_STARTUP_TIMEOUT_MS / portTICK_PERIOD_MS))
        {
            // Check NET pin - it should start blinking when network is found
            // We'll sample it multiple times to detect blinking
            int highCount = 0;
            int lowCount = 0;

            for (int i = 0; i < 10; i++)
            {
                if (gpio_get_level(static_cast<gpio_num_t>(Config::Pins::CELL_NET)) == 1)
                    highCount++;
                else
                    lowCount++;

                vTaskDelay(100 / portTICK_PERIOD_MS);
            }

            // If we see both high and low states, it's blinking
            if (highCount > 0 && lowCount > 0)
            {
                networkFound = true;
                break;
            }

            resetWatchdog(); // Keep watchdog happy during long wait
        }

        if (networkFound)
        {
            LOG_INFO("[PowerManager] Cellular modem powered on and network detected");
            return true;
        }
        else
        {
            LOG_WARNING("[PowerManager] Cellular modem powered on but no network signal detected");
            return true; // Modem is on, even if no network yet
        }
    }
    else
    {
        LOG_INFO("[PowerManager] Powering off SIM7600 cellular modem");

        // Pulse PWRKEY low for 1.2 seconds to power off
        gpio_set_level(static_cast<gpio_num_t>(Config::Pins::CELL_PWK), 0);
        vTaskDelay(Config::Timing::CELL_PWRKEY_PULSE_MS / portTICK_PERIOD_MS);
        gpio_set_level(static_cast<gpio_num_t>(Config::Pins::CELL_PWK), 1);

        // Wait a bit for shutdown
        vTaskDelay(2000 / portTICK_PERIOD_MS);

        // Set DTR high to ensure low power mode
        gpio_set_level(static_cast<gpio_num_t>(Config::Pins::CELL_DTR), 1);

        LOG_INFO("[PowerManager] Cellular modem powered off");
        return true;
    }
#else
    LOG_DEBUG("[PowerManager] Development platform - cellular " +
              std::string(enabled ? "enabled" : "disabled") + " (simulated)");
    return true;
#endif
}

// Put SIM7600 into sleep mode (low power but faster wake)
bool PowerManager::setCellularSleepMode(bool sleep)
{
#ifdef ESP32_PLATFORM
    if (!peripheralConfig_.cellular)
    {
        LOG_WARNING("[PowerManager] Cellular not powered - cannot set sleep mode");
        return false;
    }

    // DTR high = sleep mode, DTR low = normal operation
    gpio_set_level(static_cast<gpio_num_t>(Config::Pins::CELL_DTR), sleep ? 1 : 0);

    LOG_INFO("[PowerManager] Cellular modem " +
             std::string(sleep ? "entering sleep mode" : "waking from sleep"));

    if (!sleep)
    {
        // Give modem time to wake up
        vTaskDelay(500 / portTICK_PERIOD_MS);
    }

    return true;
#else
    LOG_DEBUG("[PowerManager] Development platform - cellular sleep mode " +
              std::string(sleep ? "enabled" : "disabled") + " (simulated)");
    return true;
#endif
}

// Reset SIM7600 cellular modem
bool PowerManager::resetCellularModem()
{
#ifdef ESP32_PLATFORM
    LOG_WARNING("[PowerManager] Performing hardware reset of cellular modem");

    // Pulse reset pin low
    gpio_set_level(static_cast<gpio_num_t>(Config::Pins::CELL_RST), 0);
    vTaskDelay(Config::Timing::CELL_RESET_PULSE_MS / portTICK_PERIOD_MS);
    gpio_set_level(static_cast<gpio_num_t>(Config::Pins::CELL_RST), 1);

    // Wait for modem to restart
    vTaskDelay(5000 / portTICK_PERIOD_MS);

    LOG_INFO("[PowerManager] Cellular modem reset complete");
    return true;
#else
    LOG_DEBUG("[PowerManager] Development platform - cellular reset (simulated)");
    return true;
#endif
}

// Validate sleep duration
uint64_t PowerManager::validateSleepDuration(uint64_t sleepTimeUs)
{
    if (sleepTimeUs < powerConfig_.minSleepDuration)
    {
        LOG_WARNING("[PowerManager] Sleep duration too short, adjusting to minimum");
        return powerConfig_.minSleepDuration;
    }
    else if (sleepTimeUs > powerConfig_.maxSleepDuration)
    {
        LOG_WARNING("[PowerManager] Sleep duration too long, adjusting to maximum");
        return powerConfig_.maxSleepDuration;
    }

    return sleepTimeUs;
}

// Prepare system for sleep
void PowerManager::prepareSleep(bool deepSleep)
{
    LOG_DEBUG("[PowerManager] Preparing for " +
              std::string(deepSleep ? "deep" : "light") + " sleep");

    // Reset watchdog to prevent timeout during preparation
    resetWatchdog();

    // Configure GPIO states for sleep
    if (deepSleep)
    {
        platformConfigureGPIOForSleep();
    }

    // Log sleep entry
    updatePowerStats("sleep_entry", 0);
}

// Restore system after sleep
void PowerManager::restoreFromSleep(bool fromDeepSleep)
{
    LOG_DEBUG("[PowerManager] Restoring from " +
              std::string(fromDeepSleep ? "deep" : "light") + " sleep");

    // Re-enable watchdog
    if (powerConfig_.watchdogTimeoutMs > 0)
    {
        resetWatchdog();
    }

    // Get wakeup cause
    if (fromDeepSleep)
    {
        WakeupCause cause = getWakeupCause();
        powerStats_.lastWakeupCause = wakeupCauseToString(cause);
        LOG_INFO("[PowerManager] Wakeup cause: " + powerStats_.lastWakeupCause);
    }

    // Log wakeup event
    updatePowerStats("wakeup", 0);
}

// Update power statistics
void PowerManager::updatePowerStats(const std::string &operation, uint64_t duration)
{
    if (operation == "sleep_entry")
    {
        LOG_DEBUG("[PowerManager] Sleep cycle #" + std::to_string(powerStats_.sleepCycles + 1));
    }
    else if (operation == "wakeup")
    {
        powerStats_.wakeupEvents++;
        updateDutyCycle();
    }
}

// Update duty cycle calculation
void PowerManager::updateDutyCycle()
{
    uint64_t totalTime = accumulatedActiveTime_ + accumulatedSleepTime_;
    if (totalTime > 0)
    {
        powerStats_.dutyCycle = (static_cast<float>(accumulatedActiveTime_) /
                                 static_cast<float>(totalTime)) *
                                100.0f;

        LOG_DEBUG("[PowerManager] Duty cycle: " +
                  std::to_string(powerStats_.dutyCycle) + "%");
    }
}

// Log critical power event
void PowerManager::logCriticalPowerEvent(const std::string &event, const std::string &data)
{
    LOG_CRITICAL("[PowerManager] CRITICAL EVENT: " + event +
                 (data.empty() ? "" : " - " + data));

    // Ensure log is written immediately
    // Logger should handle this internally, but we can add a small delay
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
}

// Execute emergency shutdown
void PowerManager::executeEmergencyShutdown(const std::string &reason)
{
    LOG_CRITICAL("[PowerManager] EMERGENCY SHUTDOWN: " + reason);

    // Immediately disable all peripherals
    disablePeripherals();

    // Log final message
    logCriticalPowerEvent("Emergency shutdown executed", reason);

    // Small delay to ensure logs are written
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // Enter indefinite deep sleep
    platformEnterDeepSleep(0xFFFFFFFFFFFFFFFFULL);
}

// Platform-specific implementations

// Platform initialization
bool PowerManager::platformInitialize()
{
#ifdef ESP32_PLATFORM
    // Initialize ESP32 power management
    esp_pm_config_esp32_t pm_config = {
        .max_freq_mhz = 240,
        .min_freq_mhz = 10,
        .light_sleep_enable = true};

    esp_err_t ret = esp_pm_configure(&pm_config);
    if (ret != ESP_OK)
    {
        LOG_ERROR("[PowerManager] Failed to configure ESP32 power management");
        return false;
    }

    LOG_DEBUG("[PowerManager] ESP32 power management configured");
    return true;
#else
    LOG_DEBUG("[PowerManager] Development platform initialized");
    return true;
#endif
}

// Platform deep sleep
void PowerManager::platformEnterDeepSleep(uint64_t sleepTimeUs)
{
#ifdef ESP32_PLATFORM
    // ESP32 deep sleep
    esp_deep_sleep(sleepTimeUs);
    // This function does not return on ESP32
#else
    // Development platform - simulate sleep
    LOG_DEBUG("[PowerManager] Development platform - simulating deep sleep for " +
              std::to_string(sleepTimeUs / 1000000) + " seconds");
    std::this_thread::sleep_for(std::chrono::microseconds(sleepTimeUs));
#endif
}

// Platform light sleep
bool PowerManager::platformEnterLightSleep(uint64_t sleepTimeUs)
{
#ifdef ESP32_PLATFORM
    // Configure timer wakeup
    esp_sleep_enable_timer_wakeup(sleepTimeUs);

    // Enter light sleep
    esp_err_t ret = esp_light_sleep_start();

    // Check if we woke up normally
    esp_sleep_wakeup_cause_t wakeup_reason = esp_sleep_get_wakeup_cause();

    return (ret == ESP_OK && wakeup_reason == ESP_SLEEP_WAKEUP_TIMER);
#else
    // Development platform - simulate sleep
    LOG_DEBUG("[PowerManager] Development platform - simulating light sleep for " +
              std::to_string(sleepTimeUs / 1000000) + " seconds");
    std::this_thread::sleep_for(std::chrono::microseconds(sleepTimeUs));
    return true;
#endif
}

// Platform GPIO configuration for sleep
void PowerManager::platformConfigureGPIOForSleep()
{
#ifdef ESP32_PLATFORM
    // Isolate GPIO12 to prevent current leakage (ESP32 specific)
    rtc_gpio_isolate(GPIO_NUM_12);

    // Configure other GPIOs as needed
    LOG_DEBUG("[PowerManager] GPIOs configured for deep sleep");
#else
    LOG_DEBUG("[PowerManager] Development platform - GPIOs configured for sleep (simulated)");
#endif
}

// Platform restart
void PowerManager::platformRestart()
{
#ifdef ESP32_PLATFORM
    esp_restart();
    // This function does not return
#else
    LOG_INFO("[PowerManager] Development platform - system restart (simulated)");
    // In development, we just exit
    std::exit(0);
#endif
}

// Platform watchdog initialization
bool PowerManager::platformInitializeWatchdog(uint32_t timeoutMs)
{
#ifdef ESP32_PLATFORM
    // Convert milliseconds to seconds (ESP32 uses seconds)
    uint32_t timeoutSec = (timeoutMs + 999) / 1000; // Round up

    // Initialize task watchdog
    esp_err_t ret = esp_task_wdt_init(timeoutSec, true); // true = panic on timeout
    if (ret != ESP_OK)
    {
        LOG_ERROR("[PowerManager] Failed to initialize watchdog");
        return false;
    }

    // Add current task to watchdog
    ret = esp_task_wdt_add(NULL); // NULL = current task
    if (ret != ESP_OK)
    {
        LOG_ERROR("[PowerManager] Failed to add task to watchdog");
        return false;
    }

    watchdogHandle_ = (void *)1; // Non-null to indicate initialized
    LOG_INFO("[PowerManager] Watchdog initialized with " +
             std::to_string(timeoutSec) + " second timeout");
    return true;
#else
    watchdogHandle_ = (void *)1; // Non-null to indicate initialized
    LOG_DEBUG("[PowerManager] Development platform - watchdog initialized (simulated)");
    return true;
#endif
}

// Platform watchdog reset
void PowerManager::platformResetWatchdog()
{
#ifdef ESP32_PLATFORM
    esp_task_wdt_reset();
#else
    // Development platform - no action needed
#endif
}

// Platform watchdog cleanup
void PowerManager::platformCleanupWatchdog()
{
#ifdef ESP32_PLATFORM
    if (watchdogHandle_)
    {
        esp_task_wdt_delete(NULL); // Remove current task
        esp_task_wdt_deinit();
    }
#endif
    watchdogHandle_ = nullptr;
}

// Platform reset reason
ResetReason PowerManager::platformGetResetReason()
{
#ifdef ESP32_PLATFORM
    esp_reset_reason_t reason = esp_reset_reason();

    switch (reason)
    {
    case ESP_RST_POWERON:
        return ResetReason::POWER_ON;
    case ESP_RST_EXT:
        return ResetReason::EXTERNAL;
    case ESP_RST_SW:
        return ResetReason::SOFTWARE;
    case ESP_RST_PANIC:
        return ResetReason::PANIC;
    case ESP_RST_INT_WDT:
        return ResetReason::INTERRUPT_WDT;
    case ESP_RST_TASK_WDT:
        return ResetReason::TASK_WDT;
    case ESP_RST_WDT:
        return ResetReason::OTHER_WDT;
    case ESP_RST_DEEPSLEEP:
        return ResetReason::DEEP_SLEEP;
    case ESP_RST_BROWNOUT:
        return ResetReason::BROWNOUT;
    case ESP_RST_SDIO:
        return ResetReason::SDIO;
    default:
        return ResetReason::UNKNOWN;
    }
#else
    // Development platform - return power on
    return ResetReason::POWER_ON;
#endif
}

// Platform wakeup cause
WakeupCause PowerManager::platformGetWakeupCause()
{
#ifdef ESP32_PLATFORM
    esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();

    switch (cause)
    {
    case ESP_SLEEP_WAKEUP_UNDEFINED:
        return WakeupCause::UNDEFINED;
    case ESP_SLEEP_WAKEUP_ALL:
        return WakeupCause::ALL;
    case ESP_SLEEP_WAKEUP_EXT0:
        return WakeupCause::EXT0;
    case ESP_SLEEP_WAKEUP_EXT1:
        return WakeupCause::EXT1;
    case ESP_SLEEP_WAKEUP_TIMER:
        return WakeupCause::TIMER;
    case ESP_SLEEP_WAKEUP_TOUCHPAD:
        return WakeupCause::TOUCHPAD;
    case ESP_SLEEP_WAKEUP_ULP:
        return WakeupCause::ULP;
    case ESP_SLEEP_WAKEUP_GPIO:
        return WakeupCause::GPIO;
    case ESP_SLEEP_WAKEUP_UART:
        return WakeupCause::UART;
    default:
        return WakeupCause::UNKNOWN;
    }
#else
    // Development platform - return timer wakeup
    return WakeupCause::TIMER;
#endif
}

// Platform memory info
void PowerManager::platformGetMemoryInfo(uint32_t *freeHeap, uint32_t *minFreeHeap)
{
#ifdef ESP32_PLATFORM
    if (freeHeap)
    {
        *freeHeap = esp_get_free_heap_size();
    }

    if (minFreeHeap)
    {
        *minFreeHeap = esp_get_minimum_free_heap_size();
    }
#else
    // Development platform - return simulated values
    if (freeHeap)
    {
        *freeHeap = 100000; // 100KB
    }

    if (minFreeHeap)
    {
        *minFreeHeap = 80000; // 80KB
    }
#endif
}

// Static string conversion methods

// Convert reset reason to string
std::string PowerManager::resetReasonToString(ResetReason reason)
{
    switch (reason)
    {
    case ResetReason::POWER_ON:
        return "Power-on reset";
    case ResetReason::EXTERNAL:
        return "External reset pin";
    case ResetReason::SOFTWARE:
        return "Software reset";
    case ResetReason::PANIC:
        return "System panic";
    case ResetReason::INTERRUPT_WDT:
        return "Interrupt watchdog";
    case ResetReason::TASK_WDT:
        return "Task watchdog";
    case ResetReason::OTHER_WDT:
        return "Other watchdog";
    case ResetReason::DEEP_SLEEP:
        return "Wake from deep sleep";
    case ResetReason::BROWNOUT:
        return "Brownout reset";
    case ResetReason::SDIO:
        return "SDIO reset";
    case ResetReason::UNKNOWN:
    default:
        return "Unknown reset";
    }
}

// Convert wakeup cause to string
std::string PowerManager::wakeupCauseToString(WakeupCause cause)
{
    switch (cause)
    {
    case WakeupCause::UNDEFINED:
        return "Undefined/first boot";
    case WakeupCause::ALL:
        return "All wakeup sources";
    case WakeupCause::EXT0:
        return "External signal (EXT0)";
    case WakeupCause::EXT1:
        return "External signal (EXT1)";
    case WakeupCause::TIMER:
        return "Timer wakeup";
    case WakeupCause::TOUCHPAD:
        return "Touchpad wakeup";
    case WakeupCause::ULP:
        return "ULP program";
    case WakeupCause::GPIO:
        return "GPIO wakeup";
    case WakeupCause::UART:
        return "UART wakeup";
    case WakeupCause::UNKNOWN:
    default:
        return "Unknown wakeup";
    }
}