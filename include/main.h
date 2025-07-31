#pragma once

#include <string>
#include <vector>
#include <memory>
#include <chrono>
#include <cstdint>

// Configuration constants
namespace Config
{
    const std::string ASSET_ID = "9c3f2d54-3e77-4c8a-8e1d-0f5b8e3a7b10";
    const std::string DEVICE_ID = "Device23";
    const std::string LOCATION = "Device23";
    const std::string FOREST_NAME = "Device23";
    const double LATITUDE = 55.5;
    const double LONGITUDE = -2.84;

    const int SCAN_INTERVAL_MS = 10000;
    const int BLE_SCAN_TIME_MS = 60;
    const int WIFI_SCAN_TIME_MS = 600;
    const int LTE_TIME_MINUTES = 200;
    const int MAX_CHUNK_SIZE = 4096;
    const int FILENAME_LENGTH = 50;

    // Battery safety limits
    const int BAT_LOW_SOC = 10;         // 10% State of Charge
    const int BAT_HIGH_SOC = 101;       // 101% State of Charge
    const int BAT_LOW_SOH = 50;         // 50% State of Health
    const int BAT_LOW_BOARD_TEMP = 2;   // 2°C
    const int BAT_HIGH_BOARD_TEMP = 30; // 30°C
    const int BAT_LOW_CELL_TEMP = 4;    // 4°C
    const int BAT_HIGH_CELL_TEMP = 30;  // 30°C

    // Pin definitions - ESP32 GPIO assignments
    namespace Pins
    {
        // Power control
        constexpr int POWER_5V_ENABLE = 15; // 5V buck enable (LOW = on, HIGH = off)

        // SIM7600 cellular modem pins
        constexpr int CELL_DTR = 26; // Data Terminal Ready (sleep control)
        constexpr int CELL_CTS = 34; // Clear To Send (input)
        constexpr int CELL_RTS = 35; // Request To Send (output)
        constexpr int CELL_TXD = 0;  // UART TX to modem RX
        constexpr int CELL_RXD = 4;  // UART RX from modem TX
        constexpr int CELL_RST = 32; // Hardware reset (active low)
        constexpr int CELL_PWK = 27; // Power key (power on/off control)
        constexpr int CELL_RI = 25;  // Ring indicator (input)
        constexpr int CELL_NET = 33; // Network status LED (input)

        // I2C pins (for RTC and battery monitor)
        constexpr int I2C_SDA = 21; // Default ESP32 I2C data
        constexpr int I2C_SCL = 22; // Default ESP32 I2C clock

        // SD card SPI pins (if using SPI mode)
        constexpr int SD_MISO = 19;
        constexpr int SD_MOSI = 23;
        constexpr int SD_SCK = 18;
        constexpr int SD_CS = 5;
    }

    // Timing constants for hardware control
    namespace Timing
    {
        // SIM7600 specific timings
        constexpr uint32_t CELL_PWRKEY_PULSE_MS = 1200;      // Power key pulse duration
        constexpr uint32_t CELL_RESET_PULSE_MS = 100;        // Reset pulse duration
        constexpr uint32_t CELL_STARTUP_TIMEOUT_MS = 30000;  // Max time to wait for network
        constexpr uint32_t CELL_SHUTDOWN_TIMEOUT_MS = 10000; // Max time to wait for shutdown

        // General power sequencing
        constexpr uint32_t POWER_STABILIZATION_MS = 1000; // Time to wait after enabling 5V
        constexpr uint32_t I2C_RETRY_DELAY_MS = 100;      // Delay between I2C retries
    }
}

// Data structures
struct ProbeRequest
{
    std::string dataType;
    std::string timestamp;
    std::string source;
    int rssi;
    int packetLength;
    std::string macAddress;
    std::string payload;
};

struct BatteryStatus
{
    float voltage = 0.0f;
    float current = 0.0f;
    float remainingCapacity = 0.0f;
    int averageTimeToEmpty = 0;
    float boardTemperature = 0.0f;
    float cellTemperature = 0.0f;
    int stateOfCharge = 0;
    int stateOfHealth = 0;
    int cycleCount = 0;
    float averageCurrent = 0.0f;
};

struct AssetInfo
{
    std::string assetId;
    std::string locationName;
    std::string forestName;
    std::string latitude;
    std::string longitude;
    float remainingBatteryCapacity = 0.0f;
    int stateOfCharge = 0;
    int stateOfHealth = 0;
    int runtimeToEmpty = 0;
    int cycleCount = 0;
    float batteryVoltage = 0.0f;
    float batteryCurrent = 0.0f;
    float cellTemperature = 0.0f;
    float pcbTemperature = 0.0f;
    float sdCardCapacity = 0.0f;
    std::string timeStamp;
};

// Enums
enum class SystemState
{
    FIRST_BOOT,
    NORMAL_OPERATION,
    SLEEP_MODE,
    ERROR_STATE
};

enum class WakeupCause
{
    TIMER,
    EXTERNAL,
    UNKNOWN
};

// Forward declarations
class WiFiScanner;
class BLEScanner;
class BatteryMonitor;
class CellularManager;
class SDCardManager;
class RTCManager;
class PowerManager;
class DataLogger;