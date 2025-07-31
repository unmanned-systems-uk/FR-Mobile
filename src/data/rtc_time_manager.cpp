#include "data/rtc_time_manager.h"
#include "utils/logger.h"
#include "main.h"
#include <sstream>
#include <iomanip>
#include <ctime>
#include <regex>

#ifdef ESP32_PLATFORM
#include <driver/i2c.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#elif defined(RASPBERRY_PI_PLATFORM)
// Raspberry Pi specific includes for I2C
#include <linux/i2c-dev.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <unistd.h>
#include <chrono>
#include <ctime>
#else
// Generic development platform
#include <chrono>
#include <ctime>
#endif

// DS1307 Register addresses
#define DS1307_ADDRESS 0x68
#define DS1307_REG_SECONDS 0x00
#define DS1307_REG_MINUTES 0x01
#define DS1307_REG_HOURS 0x02
#define DS1307_REG_DAY 0x03
#define DS1307_REG_DATE 0x04
#define DS1307_REG_MONTH 0x05
#define DS1307_REG_YEAR 0x06
#define DS1307_REG_CONTROL 0x07
#define DS1307_RAM_START 0x08
#define DS1307_RAM_END 0x3F

// Constructor
RTCTimeManager::RTCTimeManager(std::shared_ptr<IPowerManager> powerManager)
    : powerManager_(powerManager), initialized_(false), hasValidTime_(false), i2cInitialized_(false), lastSyncTime_(0), syncAttempts_(0), successfulSyncs_(0), totalDriftSeconds_(0)
{
    // Initialize sleep schedule with reasonable defaults
    sleepSchedule_.nightStartHour = 22; // 10 PM
    sleepSchedule_.nightEndHour = 6;    // 6 AM
    sleepSchedule_.nightSleepMinutes = 30;
    sleepSchedule_.daySleepMinutes = 5;
    sleepSchedule_.enabled = true;

    LOG_INFO("[RTCTimeManager] Created with DS1307 RTC");
}

// Destructor
RTCTimeManager::~RTCTimeManager()
{
    if (initialized_)
    {
        cleanup();
    }
}

// Initialize RTC
bool RTCTimeManager::initialize()
{
    if (initialized_)
    {
        LOG_WARNING("[RTCTimeManager] Already initialized");
        return true;
    }

    LOG_INFO("[RTCTimeManager] Initializing DS1307 RTC");

    try
    {
        // Ensure 5V power is enabled for RTC
        if (powerManager_)
        {
            if (!powerManager_->is5VSupplyEnabled())
            {
                LOG_INFO("[RTCTimeManager] Enabling 5V supply for RTC");
                powerManager_->set5VSupply(true);

                // Wait for power stabilization
                std::this_thread::sleep_for(
                    std::chrono::milliseconds(Config::Timing::POWER_STABILIZATION_MS));
            }
        }
        else
        {
            LOG_WARNING("[RTCTimeManager] No power manager - assuming 5V is enabled");
        }

        // Initialize I2C interface
        if (!initializeI2C())
        {
            lastError_ = "Failed to initialize I2C interface";
            LOG_ERROR("[RTCTimeManager] " + lastError_);
            return false;
        }

        // Test communication with RTC
        if (!isConnected())
        {
            lastError_ = "DS1307 RTC not detected on I2C bus";
            LOG_ERROR("[RTCTimeManager] " + lastError_);
            return false;
        }

        // Check if RTC is running
        uint8_t seconds = 0;
        if (!readRegister(DS1307_REG_SECONDS, seconds))
        {
            lastError_ = "Failed to read RTC seconds register";
            LOG_ERROR("[RTCTimeManager] " + lastError_);
            return false;
        }

        // Check CH (Clock Halt) bit
        if (seconds & 0x80)
        {
            LOG_WARNING("[RTCTimeManager] RTC oscillator was halted, starting it");

            // Clear CH bit to start oscillator
            seconds &= 0x7F;
            if (!writeRegister(DS1307_REG_SECONDS, seconds))
            {
                lastError_ = "Failed to start RTC oscillator";
                LOG_ERROR("[RTCTimeManager] " + lastError_);
                return false;
            }
        }

        // Configure control register (enable square wave output if needed)
        if (!writeRegister(DS1307_REG_CONTROL, 0x00))
        {
            LOG_WARNING("[RTCTimeManager] Failed to configure control register");
        }

        // Read current time to verify RTC is working
        std::string currentTime = getCurrentDateTime();
        if (!currentTime.empty())
        {
            hasValidTime_ = isValidTime(currentTime);
            if (hasValidTime_)
            {
                LOG_INFO("[RTCTimeManager] RTC time is valid: " + currentTime);
            }
            else
            {
                LOG_WARNING("[RTCTimeManager] RTC time is invalid: " + currentTime);
            }
        }

        // Load persisted data from EEPROM
        loadPersistedData();

        initialized_ = true;
        LOG_INFO("[RTCTimeManager] Initialization complete");

        return true;
    }
    catch (const std::exception &e)
    {
        lastError_ = std::string("Exception during initialization: ") + e.what();
        LOG_ERROR("[RTCTimeManager] " + lastError_);
        return false;
    }
}

// Cleanup
void RTCTimeManager::cleanup()
{
    LOG_INFO("[RTCTimeManager] Cleaning up");

    // Save any pending data
    savePersistedData();

    // Close I2C interface
    cleanupI2C();

    initialized_ = false;
    hasValidTime_ = false;
}

// Get current date/time
std::string RTCTimeManager::getCurrentDateTime()
{
    if (!initialized_)
    {
        LOG_ERROR("[RTCTimeManager] Not initialized");
        return "";
    }

    RTCDateTime dateTime;
    if (!readDateTime(dateTime))
    {
        LOG_ERROR("[RTCTimeManager] Failed to read date/time from RTC");
        return "";
    }

    // Format as ISO8601: YYYY-MM-DDTHH:MM:SS
    std::ostringstream oss;
    oss << std::setfill('0')
        << std::setw(4) << (2000 + dateTime.year) << "-"
        << std::setw(2) << static_cast<int>(dateTime.month) << "-"
        << std::setw(2) << static_cast<int>(dateTime.date) << "T"
        << std::setw(2) << static_cast<int>(dateTime.hours) << ":"
        << std::setw(2) << static_cast<int>(dateTime.minutes) << ":"
        << std::setw(2) << static_cast<int>(dateTime.seconds);

    return oss.str();
}

// Set time from network
bool RTCTimeManager::setTimeFromNetwork(const std::string &networkTime)
{
    if (!initialized_)
    {
        LOG_ERROR("[RTCTimeManager] Not initialized");
        return false;
    }

    LOG_INFO("[RTCTimeManager] Setting time from network: " + networkTime);

    // Parse network time (expected format: YYYY-MM-DD HH:MM:SS or ISO8601)
    RTCDateTime newTime;
    if (!parseTimeString(networkTime, newTime))
    {
        LOG_ERROR("[RTCTimeManager] Failed to parse network time: " + networkTime);
        return false;
    }

    // Get current RTC time for drift calculation
    RTCDateTime currentTime;
    bool hadValidTime = hasValidTime_ && readDateTime(currentTime);

    // Write new time to RTC
    if (!writeDateTime(newTime))
    {
        LOG_ERROR("[RTCTimeManager] Failed to write new time to RTC");
        return false;
    }

    // Calculate drift if we had valid time before
    if (hadValidTime)
    {
        int driftSeconds = calculateDrift(currentTime, newTime);
        totalDriftSeconds_ += std::abs(driftSeconds);

        LOG_INFO("[RTCTimeManager] Time drift: " + std::to_string(driftSeconds) +
                 " seconds (total accumulated: " + std::to_string(totalDriftSeconds_) + ")");
    }

    // Update sync statistics
    lastSyncTime_ = std::time(nullptr);
    syncAttempts_++;
    successfulSyncs_++;
    hasValidTime_ = true;

    // Save sync data to EEPROM
    savePersistedData();

    LOG_INFO("[RTCTimeManager] Time synchronized successfully");
    return true;
}

// Check if time string is valid
bool RTCTimeManager::isValidTime(const std::string &timeStr)
{
    // Check format (ISO8601: YYYY-MM-DDTHH:MM:SS)
    std::regex isoRegex(R"((\d{4})-(\d{2})-(\d{2})T(\d{2}):(\d{2}):(\d{2}))");
    std::smatch match;

    if (!std::regex_match(timeStr, match, isoRegex))
    {
        return false;
    }

    // Extract components
    int year = std::stoi(match[1].str());
    int month = std::stoi(match[2].str());
    int day = std::stoi(match[3].str());
    int hour = std::stoi(match[4].str());
    int minute = std::stoi(match[5].str());
    int second = std::stoi(match[6].str());

    // Validate ranges
    if (year < 2000 || year > 2099)
        return false; // DS1307 handles 2000-2099
    if (month < 1 || month > 12)
        return false;
    if (day < 1 || day > 31)
        return false; // Simple check
    if (hour < 0 || hour > 23)
        return false;
    if (minute < 0 || minute > 59)
        return false;
    if (second < 0 || second > 59)
        return false;

    // Check if time is reasonable (not default 2000-01-01)
    if (year == 2000 && month == 1 && day == 1)
    {
        LOG_WARNING("[RTCTimeManager] Time appears to be default/unset");
        return false;
    }

    return true;
}

// Get night sleep duration
uint64_t RTCTimeManager::getNightSleepDuration(const std::string &currentTime)
{
    if (!sleepSchedule_.enabled)
    {
        // Return default sleep duration if schedule is disabled
        return static_cast<uint64_t>(sleepSchedule_.daySleepMinutes) * 60 * 1000000ULL;
    }

    // Parse current time to get hour
    std::regex timeRegex(R"(\d{4}-\d{2}-\d{2}T(\d{2}):(\d{2}):(\d{2}))");
    std::smatch match;

    if (!std::regex_match(currentTime, match, timeRegex))
    {
        LOG_WARNING("[RTCTimeManager] Failed to parse time for sleep calculation");
        return static_cast<uint64_t>(sleepSchedule_.daySleepMinutes) * 60 * 1000000ULL;
    }

    int currentHour = std::stoi(match[1].str());

    // Determine if it's night time
    bool isNight = false;
    if (sleepSchedule_.nightStartHour > sleepSchedule_.nightEndHour)
    {
        // Night period crosses midnight (e.g., 22:00 to 06:00)
        isNight = (currentHour >= sleepSchedule_.nightStartHour ||
                   currentHour < sleepSchedule_.nightEndHour);
    }
    else
    {
        // Night period within same day
        isNight = (currentHour >= sleepSchedule_.nightStartHour &&
                   currentHour < sleepSchedule_.nightEndHour);
    }

    // Return appropriate sleep duration in microseconds
    uint32_t sleepMinutes = isNight ? sleepSchedule_.nightSleepMinutes : sleepSchedule_.daySleepMinutes;

    LOG_DEBUG("[RTCTimeManager] " + std::string(isNight ? "Night" : "Day") +
              " time detected, sleep duration: " + std::to_string(sleepMinutes) + " minutes");

    return static_cast<uint64_t>(sleepMinutes) * 60 * 1000000ULL;
}

// Read date/time from RTC
bool RTCTimeManager::readDateTime(RTCDateTime &dateTime)
{
    uint8_t data[7];

    // Read all time registers in one operation
    if (!readRegisters(DS1307_REG_SECONDS, data, 7))
    {
        LOG_ERROR("[RTCTimeManager] Failed to read time registers");
        return false;
    }

    // Convert BCD to decimal and populate structure
    dateTime.seconds = bcdToDec(data[0] & 0x7F); // Mask CH bit
    dateTime.minutes = bcdToDec(data[1]);
    dateTime.hours = bcdToDec(data[2] & 0x3F); // 24-hour mode
    dateTime.dayOfWeek = bcdToDec(data[3]);
    dateTime.date = bcdToDec(data[4]);
    dateTime.month = bcdToDec(data[5]);
    dateTime.year = bcdToDec(data[6]);

    return true;
}

// Write date/time to RTC
bool RTCTimeManager::writeDateTime(const RTCDateTime &dateTime)
{
    uint8_t data[7];

    // Convert decimal to BCD
    data[0] = decToBcd(dateTime.seconds); // CH bit = 0 (oscillator enabled)
    data[1] = decToBcd(dateTime.minutes);
    data[2] = decToBcd(dateTime.hours); // 24-hour mode
    data[3] = decToBcd(dateTime.dayOfWeek);
    data[4] = decToBcd(dateTime.date);
    data[5] = decToBcd(dateTime.month);
    data[6] = decToBcd(dateTime.year);

    // Write all time registers in one operation
    if (!writeRegisters(DS1307_REG_SECONDS, data, 7))
    {
        LOG_ERROR("[RTCTimeManager] Failed to write time registers");
        return false;
    }

    LOG_DEBUG("[RTCTimeManager] Time written successfully");
    return true;
}

// I2C operations
bool RTCTimeManager::initializeI2C()
{
#ifdef ESP32_PLATFORM
    // Configure I2C parameters
    i2c_config_t conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = Config::Pins::I2C_SDA,
        .scl_io_num = Config::Pins::I2C_SCL,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master = {
            .clk_speed = 100000  // 100kHz for DS1307
        },
        .clk_flags = 0
    };

    esp_err_t ret = i2c_param_config(I2C_NUM_0, &conf);
    if (ret != ESP_OK) {
        LOG_ERROR("[RTCTimeManager] Failed to configure I2C parameters");
        return false;
    }

    ret = i2c_driver_install(I2C_NUM_0, conf.mode, 0, 0, 0);
    if (ret != ESP_OK) {
        LOG_ERROR("[RTCTimeManager] Failed to install I2C driver");
        return false;
    }

    i2cInitialized_ = true;
    LOG_DEBUG("[RTCTimeManager] I2C initialized on SDA:" +
              std::to_string(Config::Pins::I2C_SDA) +
              ", SCL:" + std::to_string(Config::Pins::I2C_SCL));
    return true;
#else
    // Development platform
    i2cInitialized_ = true;
    LOG_DEBUG("[RTCTimeManager] Development platform - I2C initialized (simulated)");
    return true;
#endif
}

// Cleanup I2C
void RTCTimeManager::cleanupI2C()
{
#ifdef ESP32_PLATFORM
    if (i2cInitialized_)
    {
        i2c_driver_delete(I2C_NUM_0);
        i2cInitialized_ = false;
    }
#else
    i2cInitialized_ = false;
#endif
}

// Check if RTC is connected
bool RTCTimeManager::isConnected()
{
#ifdef ESP32_PLATFORM
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (DS1307_ADDRESS << 1) | I2C_MASTER_WRITE, true);
    i2c_master_stop(cmd);
    esp_err_t ret = i2c_master_cmd_begin(I2C_NUM_0, cmd, 1000 / portTICK_PERIOD_MS);
    i2c_cmd_link_delete(cmd);
    return (ret == ESP_OK);
#else
    // Development platform - simulate connected
    return true;
#endif
}

// Read single register
bool RTCTimeManager::readRegister(uint8_t reg, uint8_t &value)
{
#ifdef ESP32_PLATFORM
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (DS1307_ADDRESS << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(cmd, reg, true);
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (DS1307_ADDRESS << 1) | I2C_MASTER_READ, true);
    i2c_master_read_byte(cmd, &value, I2C_MASTER_NACK);
    i2c_master_stop(cmd);
    esp_err_t ret = i2c_master_cmd_begin(I2C_NUM_0, cmd, 1000 / portTICK_PERIOD_MS);
    i2c_cmd_link_delete(cmd);
    return (ret == ESP_OK);
#else
    // Development platform - return simulated value
    value = 0x00;
    return true;
#endif
}

// Write single register
bool RTCTimeManager::writeRegister(uint8_t reg, uint8_t value)
{
#ifdef ESP32_PLATFORM
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (DS1307_ADDRESS << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(cmd, reg, true);
    i2c_master_write_byte(cmd, value, true);
    i2c_master_stop(cmd);
    esp_err_t ret = i2c_master_cmd_begin(I2C_NUM_0, cmd, 1000 / portTICK_PERIOD_MS);
    i2c_cmd_link_delete(cmd);
    return (ret == ESP_OK);
#else
    // Development platform - simulate success
    return true;
#endif
}

// Read multiple registers
bool RTCTimeManager::readRegisters(uint8_t startReg, uint8_t *data, uint8_t count)
{
#ifdef ESP32_PLATFORM
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (DS1307_ADDRESS << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(cmd, startReg, true);
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (DS1307_ADDRESS << 1) | I2C_MASTER_READ, true);
    if (count > 1) {
        i2c_master_read(cmd, data, count - 1, I2C_MASTER_ACK);
    }
    i2c_master_read_byte(cmd, data + count - 1, I2C_MASTER_NACK);
    i2c_master_stop(cmd);
    esp_err_t ret = i2c_master_cmd_begin(I2C_NUM_0, cmd, 1000 / portTICK_PERIOD_MS);
    i2c_cmd_link_delete(cmd);
    return (ret == ESP_OK);
#else
    // Development platform - return current system time
    auto now = std::chrono::system_clock::now();
    std::time_t time = std::chrono::system_clock::to_time_t(now);
    std::tm *tm = std::localtime(&time);

    if (startReg == DS1307_REG_SECONDS && count >= 7)
    {
        data[0] = decToBcd(tm->tm_sec);
        data[1] = decToBcd(tm->tm_min);
        data[2] = decToBcd(tm->tm_hour);
        data[3] = decToBcd(tm->tm_wday);
        data[4] = decToBcd(tm->tm_mday);
        data[5] = decToBcd(tm->tm_mon + 1);
        data[6] = decToBcd(tm->tm_year - 100); // Years since 2000
    }
    return true;
#endif
}

// Write multiple registers
bool RTCTimeManager::writeRegisters(uint8_t startReg, const uint8_t *data, uint8_t count)
{
#ifdef ESP32_PLATFORM
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (DS1307_ADDRESS << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(cmd, startReg, true);
    i2c_master_write(cmd, data, count, true);
    i2c_master_stop(cmd);
    esp_err_t ret = i2c_master_cmd_begin(I2C_NUM_0, cmd, 1000 / portTICK_PERIOD_MS);
    i2c_cmd_link_delete(cmd);
    return (ret == ESP_OK);
#else
    // Development platform - simulate success
    return true;
#endif
}

// EEPROM operations for persistent data
bool RTCTimeManager::readEEPROM(uint8_t address, uint8_t *data, uint8_t length)
{
    if (address + length > 56) // DS1307 has 56 bytes of RAM (0x08-0x3F)
    {
        LOG_ERROR("[RTCTimeManager] EEPROM read exceeds available space");
        return false;
    }

    return readRegisters(DS1307_RAM_START + address, data, length);
}

bool RTCTimeManager::writeEEPROM(uint8_t address, const uint8_t *data, uint8_t length)
{
    if (address + length > 56) // DS1307 has 56 bytes of RAM
    {
        LOG_ERROR("[RTCTimeManager] EEPROM write exceeds available space");
        return false;
    }

    return writeRegisters(DS1307_RAM_START + address, data, length);
}

// Load persisted data from EEPROM
void RTCTimeManager::loadPersistedData()
{
    LOG_DEBUG("[RTCTimeManager] Loading persisted data from EEPROM");

    // Read sync statistics (stored at EEPROM address 0)
    uint8_t data[12];
    if (readEEPROM(0, data, sizeof(data)))
    {
        // Check magic number to verify valid data
        if (data[0] == 0xA5 && data[1] == 0x5A)
        {
            syncAttempts_ = (data[2] << 8) | data[3];
            successfulSyncs_ = (data[4] << 8) | data[5];
            totalDriftSeconds_ = (data[6] << 24) | (data[7] << 16) |
                                 (data[8] << 8) | data[9];

            // Last two bytes reserved for future use

            LOG_INFO("[RTCTimeManager] Loaded sync stats - Attempts: " +
                     std::to_string(syncAttempts_) +
                     ", Successful: " + std::to_string(successfulSyncs_) +
                     ", Total drift: " + std::to_string(totalDriftSeconds_) + "s");
        }
        else
        {
            LOG_DEBUG("[RTCTimeManager] No valid persisted data found");
        }
    }
}

// Save persistent data to EEPROM
void RTCTimeManager::savePersistedData()
{
    LOG_DEBUG("[RTCTimeManager] Saving persistent data to EEPROM");

    // Prepare sync statistics data
    uint8_t data[12];
    data[0] = 0xA5; // Magic number
    data[1] = 0x5A;
    data[2] = (syncAttempts_ >> 8) & 0xFF;
    data[3] = syncAttempts_ & 0xFF;
    data[4] = (successfulSyncs_ >> 8) & 0xFF;
    data[5] = successfulSyncs_ & 0xFF;
    data[6] = (totalDriftSeconds_ >> 24) & 0xFF;
    data[7] = (totalDriftSeconds_ >> 16) & 0xFF;
    data[8] = (totalDriftSeconds_ >> 8) & 0xFF;
    data[9] = totalDriftSeconds_ & 0xFF;
    data[10] = 0; // Reserved
    data[11] = 0; // Reserved

    if (!writeEEPROM(0, data, sizeof(data)))
    {
        LOG_ERROR("[RTCTimeManager] Failed to save persistent data");
    }
}

// Helper functions
uint8_t RTCTimeManager::bcdToDec(uint8_t bcd)
{
    return ((bcd >> 4) * 10) + (bcd & 0x0F);
}

uint8_t RTCTimeManager::decToBcd(uint8_t dec)
{
    return ((dec / 10) << 4) | (dec % 10);
}

// Parse time string
bool RTCTimeManager::parseTimeString(const std::string &timeStr, RTCDateTime &dateTime)
{
    // Try ISO8601 format first: YYYY-MM-DDTHH:MM:SS
    std::regex isoRegex(R"((\d{4})-(\d{2})-(\d{2})T(\d{2}):(\d{2}):(\d{2}))");
    std::smatch match;

    if (std::regex_match(timeStr, match, isoRegex))
    {
        int year = std::stoi(match[1].str());
        dateTime.year = year - 2000; // DS1307 stores years since 2000
        dateTime.month = std::stoi(match[2].str());
        dateTime.date = std::stoi(match[3].str());
        dateTime.hours = std::stoi(match[4].str());
        dateTime.minutes = std::stoi(match[5].str());
        dateTime.seconds = std::stoi(match[6].str());

        // Calculate day of week (simplified - doesn't account for leap years perfectly)
        dateTime.dayOfWeek = calculateDayOfWeek(year, dateTime.month, dateTime.date);

        return true;
    }

    // Try alternate format: YYYY-MM-DD HH:MM:SS
    std::regex altRegex(R"((\d{4})-(\d{2})-(\d{2}) (\d{2}):(\d{2}):(\d{2}))");
    if (std::regex_match(timeStr, match, altRegex))
    {
        int year = std::stoi(match[1].str());
        dateTime.year = year - 2000;
        dateTime.month = std::stoi(match[2].str());
        dateTime.date = std::stoi(match[3].str());
        dateTime.hours = std::stoi(match[4].str());
        dateTime.minutes = std::stoi(match[5].str());
        dateTime.seconds = std::stoi(match[6].str());

        dateTime.dayOfWeek = calculateDayOfWeek(year, dateTime.month, dateTime.date);

        return true;
    }

    return false;
}

// Calculate day of week
uint8_t RTCTimeManager::calculateDayOfWeek(int year, int month, int day)
{
    // Zeller's congruence (simplified)
    if (month < 3)
    {
        month += 12;
        year--;
    }

    int k = year % 100;
    int j = year / 100;

    int h = (day + ((13 * (month + 1)) / 5) + k + (k / 4) + (j / 4) - (2 * j)) % 7;

    // Convert to 1-7 where 1 = Sunday
    return ((h + 5) % 7) + 1;
}

// Calculate drift between two times
int RTCTimeManager::calculateDrift(const RTCDateTime &rtcTime, const RTCDateTime &actualTime)
{
    // Convert both times to seconds since epoch (simplified)
    int rtcSeconds = rtcTime.seconds + (rtcTime.minutes * 60) + (rtcTime.hours * 3600) +
                     (rtcTime.date * 86400); // Simplified - doesn't account for months/years

    int actualSeconds = actualTime.seconds + (actualTime.minutes * 60) +
                        (actualTime.hours * 3600) + (actualTime.date * 86400);

    return actualSeconds - rtcSeconds;
}

// Get sync statistics
RTCSyncStats RTCTimeManager::getSyncStats() const
{
    RTCSyncStats stats;
    stats.lastSyncTime = lastSyncTime_;
    stats.syncAttempts = syncAttempts_;
    stats.successfulSyncs = successfulSyncs_;
    stats.failedSyncs = syncAttempts_ - successfulSyncs_;
    stats.totalDriftSeconds = totalDriftSeconds_;

    if (successfulSyncs_ > 0)
    {
        stats.averageDriftSeconds = static_cast<float>(totalDriftSeconds_) /
                                    static_cast<float>(successfulSyncs_);
    }

    return stats;
}

// Set sleep schedule
void RTCTimeManager::setSleepSchedule(const SleepSchedule &schedule)
{
    sleepSchedule_ = schedule;
    LOG_INFO("[RTCTimeManager] Sleep schedule updated - Night: " +
             std::to_string(schedule.nightStartHour) + ":00 to " +
             std::to_string(schedule.nightEndHour) + ":00, " +
             "Night sleep: " + std::to_string(schedule.nightSleepMinutes) + " min, " +
             "Day sleep: " + std::to_string(schedule.daySleepMinutes) + " min");
}