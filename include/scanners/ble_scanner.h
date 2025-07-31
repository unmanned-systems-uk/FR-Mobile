#pragma once

#include "interfaces.h"
#include <mutex>
#include <atomic>
#include <vector>
#include <functional>
#include <memory>

/**
 * @file ble_scanner.h
 * @brief BLE Scanner class for capturing Bluetooth Low Energy advertisements
 * @author Anthony Kirk
 * @date 2025-07-29
 * @version 1.0.0
 *
 * This class implements the IScanner interface to provide BLE scanning
 * functionality. It captures BLE advertisements from nearby devices and stores
 * them for analysis or transmission. Designed primarily for ESP32 but includes
 * development platform support.
 */

/**
 * @brief BLE Scanner class for capturing and processing BLE advertisements
 *
 * This class implements the IScanner interface to provide comprehensive BLE
 * scanning functionality. It captures BLE advertisements, processes device
 * information, and integrates with the logging and storage systems.
 *
 * Key features:
 * - Thread-safe scanning operations
 * - Configurable scan parameters (interval, window, RSSI threshold)
 * - Real-time result callbacks
 * - Automatic data persistence
 * - Platform abstraction (ESP32/development)
 * - Comprehensive error handling and logging
 */
class BLEScanner : public IScanner
{
public:
    /**
     * @brief Constructor
     * @param storage Shared pointer to data storage interface
     * @param timeManager Shared pointer to time management interface
     */
    BLEScanner(std::shared_ptr<IDataStorage> storage,
               std::shared_ptr<ITimeManager> timeManager);

    /**
     * @brief Destructor - automatically cleans up BLE resources
     */
    ~BLEScanner() override;

    // IScanner interface implementation
    /**
     * @brief Initialize the BLE scanner and Bluetooth stack
     * @return true if initialization successful, false otherwise
     */
    bool initialize() override;

    /**
     * @brief Start BLE advertisement scanning
     * @return true if scan started successfully, false otherwise
     */
    bool startScan() override;

    /**
     * @brief Stop BLE advertisement scanning
     * @return true if scan stopped successfully, false otherwise
     */
    bool stopScan() override;

    /**
     * @brief Get all BLE scan results collected so far
     * @return Vector of ProbeRequest structures containing BLE data
     */
    std::vector<ProbeRequest> getResults() override;

    /**
     * @brief Clean up all BLE resources and reset state
     */
    void cleanup() override;

    // BLE-specific configuration methods
    /**
     * @brief Set minimum RSSI threshold for filtering weak signals
     * @param minRssi Minimum RSSI value (typically -120 to 0 dBm)
     */
    void setMinRSSI(int minRssi) { minRssi_ = minRssi; }

    /**
     * @brief Configure BLE scan timing parameters
     * @param interval Scan interval in BLE time units (0.625ms each)
     * @param window Scan window in BLE time units (must be <= interval)
     */
    void setScanParams(uint16_t interval, uint16_t window);

    /**
     * @brief Set callback function for real-time advertisement processing
     * @param callback Function to call when a BLE advertisement is detected
     */
    void setResultCallback(std::function<void(const ProbeRequest &)> callback);

    /**
     * @brief Check if scanner is currently active
     * @return true if scanning is in progress
     */
    bool isScanning() const { return scanning_.load(); }

    /**
     * @brief Get number of BLE advertisements captured
     * @return Count of captured advertisements
     */
    size_t getResultCount() const;

    /**
     * @brief Clear all stored scan results
     */
    void clearResults();

    /**
     * @brief Get current scan configuration
     * @return Struct containing current scan parameters
     */
    struct ScanConfig
    {
        int minRssi;
        uint16_t interval;
        uint16_t window;
        bool scanning;
    };
    ScanConfig getScanConfig() const;

private:
    // Dependencies
    std::shared_ptr<IDataStorage> storage_;
    std::shared_ptr<ITimeManager> timeManager_;

    // Scan results and synchronization
    std::vector<ProbeRequest> scanResults_;
    mutable std::mutex resultsMutex_;
    std::atomic<bool> scanning_;

    // Callback for real-time processing
    std::function<void(const ProbeRequest &)> resultCallback_;

    // BLE scan configuration
    int minRssi_;
    uint16_t scanInterval_;
    uint16_t scanWindow_;

    // Platform-specific implementation
    void *bleHandle_; ///< Platform-specific BLE handle (e.g., ESP32 BLE stack)

    /**
     * @brief Process a captured BLE advertisement
     * @param address Device MAC address (6 bytes)
     * @param rssi Received signal strength indicator
     * @param advData Advertisement data payload
     * @param advDataLen Length of advertisement data
     */
    void processBLEResult(const uint8_t *address, int rssi,
                          const uint8_t *advData, size_t advDataLen);

    /**
     * @brief Format MAC address bytes to string representation
     * @param address 6-byte MAC address
     * @return MAC address as string (format: "xx:xx:xx:xx:xx:xx")
     */
    std::string formatMACAddress(const uint8_t *address);

    /**
     * @brief Convert advertisement data to hexadecimal string
     * @param data Advertisement data bytes
     * @param length Length of data
     * @return Hex string representation of data
     */
    std::string formatPayload(const uint8_t *data, size_t length);

    /**
     * @brief Extract device name from BLE advertisement data
     * @param advData Advertisement data
     * @param advDataLen Length of advertisement data
     * @return Device name if found, empty string otherwise
     */
    std::string extractDeviceName(const uint8_t *advData, size_t advDataLen);

    /**
     * @brief Parse advertisement data for service UUIDs
     * @param advData Advertisement data
     * @param advDataLen Length of advertisement data
     * @return Vector of service UUID strings
     */
    std::vector<std::string> extractServiceUUIDs(const uint8_t *advData, size_t advDataLen);

    /**
     * @brief Validate BLE advertisement data
     * @param address Device address
     * @param rssi Signal strength
     * @param advData Advertisement data
     * @param advDataLen Data length
     * @return true if advertisement is valid and should be processed
     */
    bool isValidAdvertisement(const uint8_t *address, int rssi,
                              const uint8_t *advData, size_t advDataLen);

    /**
     * @brief Save BLE result to storage and internal cache
     * @param request BLE scan result to save
     */
    void saveBLEResult(const ProbeRequest &request);

    // Platform-specific callbacks (static for C compatibility)
    /**
     * @brief Static callback for BLE GAP events (ESP32)
     * @param event BLE event type
     * @param param Event parameters
     */
    static void bleGapCallback(int event, void *param);

    // Friend function for callback bridging
    friend void bleScanResultHandler(BLEScanner *scanner, const uint8_t *address,
                                     int rssi, const uint8_t *advData, size_t advDataLen);

    // Constants
    static constexpr int DEFAULT_MIN_RSSI = -120;           ///< Default minimum RSSI threshold
    static constexpr uint16_t DEFAULT_SCAN_INTERVAL = 0x50; ///< Default scan interval (50ms)
    static constexpr uint16_t DEFAULT_SCAN_WINDOW = 0x30;   ///< Default scan window (30ms)
    static constexpr size_t MAX_ADV_DATA_LENGTH = 31;       ///< Maximum BLE advertisement length
    static constexpr size_t MAC_ADDRESS_LENGTH = 6;         ///< MAC address length in bytes

    // BLE Advertisement Data Type constants
    static constexpr uint8_t BLE_AD_TYPE_COMPLETE_NAME = 0x09;       ///< Complete local name
    static constexpr uint8_t BLE_AD_TYPE_SHORTENED_NAME = 0x08;      ///< Shortened local name
    static constexpr uint8_t BLE_AD_TYPE_16BIT_SERVICE_UUID = 0x03;  ///< 16-bit service UUID
    static constexpr uint8_t BLE_AD_TYPE_128BIT_SERVICE_UUID = 0x07; ///< 128-bit service UUID
};

/**
 * @brief Global callback bridge function for BLE scan results
 *
 * This function bridges the C-style callback required by the BLE stack
 * to the C++ class method. It's used internally by the BLEScanner.
 *
 * @param scanner Pointer to BLEScanner instance
 * @param address Device MAC address
 * @param rssi Received signal strength
 * @param advData Advertisement data
 * @param advDataLen Advertisement data length
 */
void bleScanResultHandler(BLEScanner *scanner, const uint8_t *address,
                          int rssi, const uint8_t *advData, size_t advDataLen);