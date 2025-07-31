// src/scanners/ble_scanner.cpp - Complete ESP32 Implementation
#ifdef ESP32_PLATFORM
#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_gap_ble_api.h"
#include "esp_bt_defs.h"
#include "nvs_flash.h"
#elif defined(RASPBERRY_PI_PLATFORM)
// Raspberry Pi BLE scanning includes
#include <bluetooth/bluetooth.h>
#include <bluetooth/hci.h>
#include <bluetooth/hci_lib.h>
#include <sys/ioctl.h>
#include <unistd.h>
#endif

#include "scanners/ble_scanner.h"
#include "utils/logger.h"
#include "main.h"    // For Utils class and ProbeRequest
#include <cstdio>    // For printf, snprintf
#include <cstring>   // For memcmp, memcpy
#include <algorithm> // For std::find

// Component name for logging
static const std::string COMPONENT_NAME = "BLEScanner";

// Global pointer for callback bridge (ESP32 requires C-style callbacks)
static BLEScanner *g_bleScannerInstance = nullptr;

#ifdef ESP32_PLATFORM
// ESP32 BLE GAP callback (C-style callback required by ESP-IDF)
static void esp32BleGapCallback(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param)
{
    if (!g_bleScannerInstance)
    {
        return;
    }

    try
    {
        // Handle scan result events (as in your original code)
        if (event == ESP_GAP_BLE_SCAN_RESULT_EVT)
        {
            if (param->scan_rst.search_evt == ESP_GAP_SEARCH_INQ_RES_EVT)
            {
                // Extract scan result data (exactly as in your original code)
                const uint8_t *address = param->scan_rst.bda;
                int rssi = param->scan_rst.rssi;
                const uint8_t *advData = param->scan_rst.ble_adv;
                size_t advDataLen = param->scan_rst.adv_data_len;

                // Process the BLE advertisement
                g_bleScannerInstance->processBLEResult(address, rssi, advData, advDataLen);
            }
        }
        // Handle scan parameter set completion
        else if (event == ESP_GAP_BLE_SCAN_PARAM_SET_COMPLETE_EVT)
        {
            // Scan parameters successfully set, can start scanning
            LOG_DEBUG(COMPONENT_NAME, "BLE scan parameters set successfully");
        }
        // Handle scan start completion
        else if (event == ESP_GAP_BLE_SCAN_START_COMPLETE_EVT)
        {
            if (param->scan_start_cmpl.status == ESP_BT_STATUS_SUCCESS)
            {
                LOG_DEBUG(COMPONENT_NAME, "BLE scanning started successfully");
            }
            else
            {
                LOG_ERROR(COMPONENT_NAME, "Failed to start BLE scanning");
            }
        }
        // Handle scan stop completion
        else if (event == ESP_GAP_BLE_SCAN_STOP_COMPLETE_EVT)
        {
            if (param->scan_stop_cmpl.status == ESP_BT_STATUS_SUCCESS)
            {
                LOG_DEBUG(COMPONENT_NAME, "BLE scanning stopped successfully");
            }
            else
            {
                LOG_ERROR(COMPONENT_NAME, "Failed to stop BLE scanning");
            }
        }
    }
    catch (const std::exception &e)
    {
        // Can't use LOG_ERROR here as we're in a C callback
        printf("Error in ESP32 BLE GAP callback: %s\n", e.what());
    }
}
#endif

// BLE Scanner Implementation
BLEScanner::BLEScanner(std::shared_ptr<IDataStorage> storage,
                       std::shared_ptr<ITimeManager> timeManager)
    : storage_(storage), timeManager_(timeManager), scanning_(false),
      minRssi_(DEFAULT_MIN_RSSI), scanInterval_(DEFAULT_SCAN_INTERVAL),
      scanWindow_(DEFAULT_SCAN_WINDOW), bleHandle_(nullptr)
{
    LOG_DEBUG(COMPONENT_NAME, "BLE Scanner instance created");

#ifdef ESP32_PLATFORM
    // Set global instance for callback bridge
    g_bleScannerInstance = this;
#endif
}

BLEScanner::~BLEScanner()
{
    LOG_DEBUG(COMPONENT_NAME, "BLE Scanner destructor called");
    cleanup();

#ifdef ESP32_PLATFORM
    // Clear global instance
    if (g_bleScannerInstance == this)
    {
        g_bleScannerInstance = nullptr;
    }
#endif
}

bool BLEScanner::initialize()
{
    try
    {
        LOG_INFO(COMPONENT_NAME, "Initializing BLE Scanner...");

#ifdef ESP32_PLATFORM
        LOG_INFO(COMPONENT_NAME, "Starting ESP32 Bluetooth stack...");

        // Initialize NVS (required for Bluetooth)
        esp_err_t ret = nvs_flash_init();
        if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
        {
            ESP_ERROR_CHECK(nvs_flash_erase());
            ret = nvs_flash_init();
        }
        ESP_ERROR_CHECK(ret);

        // Initialize Bluetooth controller (as in your original btStart())
        esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
        ret = esp_bt_controller_init(&bt_cfg);
        if (ret != ESP_OK)
        {
            LOG_ERROR(COMPONENT_NAME, "Failed to initialize BT controller: " + std::string(esp_err_to_name(ret)));
            return false;
        }

        ret = esp_bt_controller_enable(ESP_BT_MODE_BLE);
        if (ret != ESP_OK)
        {
            LOG_ERROR(COMPONENT_NAME, "Failed to enable BT controller: " + std::string(esp_err_to_name(ret)));
            return false;
        }

        // Initialize Bluedroid stack (as in your original code)
        ret = esp_bluedroid_init();
        if (ret != ESP_OK)
        {
            LOG_ERROR(COMPONENT_NAME, "Failed to initialize Bluedroid stack: " + std::string(esp_err_to_name(ret)));
            return false;
        }

        ret = esp_bluedroid_enable();
        if (ret != ESP_OK)
        {
            LOG_ERROR(COMPONENT_NAME, "Failed to enable Bluedroid stack: " + std::string(esp_err_to_name(ret)));
            return false;
        }

        // Register GAP callback (as in your original code)
        ret = esp_ble_gap_register_callback(esp32BleGapCallback);
        if (ret != ESP_OK)
        {
            LOG_ERROR(COMPONENT_NAME, "Failed to register BLE GAP callback: " + std::string(esp_err_to_name(ret)));
            return false;
        }

        // Set scan parameters (exactly as in your original code)
        esp_ble_scan_params_t scanParams = {
            .scan_type = BLE_SCAN_TYPE_ACTIVE,               // Same as your original
            .own_addr_type = BLE_ADDR_TYPE_PUBLIC,           // Same as your original
            .scan_filter_policy = BLE_SCAN_FILTER_ALLOW_ALL, // Same as your original
            .scan_interval = scanInterval_,                  // 0x50 from your original
            .scan_window = scanWindow_                       // 0x30 from your original
        };

        ret = esp_ble_gap_set_scan_params(&scanParams);
        if (ret != ESP_OK)
        {
            LOG_ERROR(COMPONENT_NAME, "Failed to set BLE scan parameters: " + std::string(esp_err_to_name(ret)));
            return false;
        }

        LOG_INFO(COMPONENT_NAME, "ESP32 BLE stack initialized successfully");
#else
        LOG_WARNING(COMPONENT_NAME, "Running on development platform - BLE scanning will be mocked");
#endif

        LOGF_INFO(COMPONENT_NAME, "BLE Scanner configuration: MinRSSI=%d, Interval=%u (%.1fms), Window=%u (%.1fms)",
                  minRssi_, scanInterval_, scanInterval_ * 0.625, scanWindow_, scanWindow_ * 0.625);

        LOG_INFO(COMPONENT_NAME, "BLE Scanner initialization successful");
        return true;
    }
    catch (const std::exception &e)
    {
        LOG_ERROR(COMPONENT_NAME, "Initialization failed: " + std::string(e.what()));
        return false;
    }
}

bool BLEScanner::startScan()
{
    if (scanning_.load())
    {
        LOG_WARNING(COMPONENT_NAME, "BLE scan already in progress");
        return false;
    }

    try
    {
        LOG_INFO(COMPONENT_NAME, "Starting BLE scan...");

        // Clear previous results
        {
            std::lock_guard<std::mutex> lock(resultsMutex_);
            size_t previousCount = scanResults_.size();
            scanResults_.clear();
            if (previousCount > 0)
            {
                LOGF_DEBUG(COMPONENT_NAME, "Cleared %zu previous scan results", previousCount);
            }
        }

        scanning_.store(true);

#ifdef ESP32_PLATFORM
        // Start BLE scanning (as in your original code)
        // Note: Duration of 0 means continuous scanning until stopped
        esp_err_t ret = esp_ble_gap_start_scanning(0);
        if (ret != ESP_OK)
        {
            LOG_ERROR(COMPONENT_NAME, "Failed to start BLE scanning: " + std::string(esp_err_to_name(ret)));
            scanning_.store(false);
            return false;
        }
        LOG_DEBUG(COMPONENT_NAME, "ESP32 BLE scanning started");
#else
        LOG_DEBUG(COMPONENT_NAME, "Mock BLE scan mode activated");
        // For development, simulate some BLE devices
        simulateBLEDevices();
#endif

        LOGF_INFO(COMPONENT_NAME, "BLE scan started successfully (RSSI threshold: %d dBm)", minRssi_);
        return true;
    }
    catch (const std::exception &e)
    {
        scanning_.store(false);
        LOG_ERROR(COMPONENT_NAME, "Failed to start BLE scan: " + std::string(e.what()));
        return false;
    }
}

bool BLEScanner::stopScan()
{
    if (!scanning_.load())
    {
        LOG_DEBUG(COMPONENT_NAME, "No active BLE scan to stop");
        return true;
    }

    try
    {
        LOG_INFO(COMPONENT_NAME, "Stopping BLE scan...");
        scanning_.store(false);

#ifdef ESP32_PLATFORM
        // Stop BLE scanning (as in your original code)
        esp_err_t ret = esp_ble_gap_stop_scanning();
        if (ret != ESP_OK)
        {
            LOG_WARNING(COMPONENT_NAME, "Failed to stop BLE scanning: " + std::string(esp_err_to_name(ret)));
        }
        LOG_DEBUG(COMPONENT_NAME, "ESP32 BLE scanning stopped");
#endif

        size_t resultCount = getResultCount();
        LOGF_INFO(COMPONENT_NAME, "BLE scan stopped - captured %zu advertisements", resultCount);

        return true;
    }
    catch (const std::exception &e)
    {
        LOG_ERROR(COMPONENT_NAME, "Failed to stop BLE scan: " + std::string(e.what()));
        return false;
    }
}

std::vector<ProbeRequest> BLEScanner::getResults()
{
    std::lock_guard<std::mutex> lock(resultsMutex_);
    LOGF_DEBUG(COMPONENT_NAME, "Returning %zu BLE scan results", scanResults_.size());
    return scanResults_;
}

void BLEScanner::cleanup()
{
    LOG_INFO(COMPONENT_NAME, "Cleaning up BLE Scanner...");

    // Stop scanning if active
    if (scanning_.load())
    {
        stopScan();
    }

#ifdef ESP32_PLATFORM
    // Platform-specific cleanup (reverse order of initialization)
    esp_ble_gap_register_callback(nullptr);
    esp_bluedroid_disable();
    esp_bluedroid_deinit();
    esp_bt_controller_disable();
    esp_bt_controller_deinit();
    LOG_DEBUG(COMPONENT_NAME, "ESP32 BLE stack cleanup completed");
#endif

    // Clear results
    {
        std::lock_guard<std::mutex> lock(resultsMutex_);
        size_t clearedCount = scanResults_.size();
        scanResults_.clear();
        if (clearedCount > 0)
        {
            LOGF_DEBUG(COMPONENT_NAME, "Cleared %zu stored results", clearedCount);
        }
    }

    LOG_INFO(COMPONENT_NAME, "BLE Scanner cleanup completed");
}

void BLEScanner::setScanParams(uint16_t interval, uint16_t window)
{
    if (scanning_.load())
    {
        LOG_WARNING(COMPONENT_NAME, "Cannot change scan parameters while scanning is active");
        return;
    }

    if (window > interval)
    {
        LOG_ERROR(COMPONENT_NAME, "Scan window cannot be greater than scan interval");
        return;
    }

    scanInterval_ = interval;
    scanWindow_ = window;

    LOGF_INFO(COMPONENT_NAME, "Scan parameters updated: interval=%u (%.1fms), window=%u (%.1fms)",
              interval, interval * 0.625, window, window * 0.625);

#ifdef ESP32_PLATFORM
    // Update ESP32 scan parameters if already initialized
    if (!scanning_.load())
    {
        esp_ble_scan_params_t scanParams = {
            .scan_type = BLE_SCAN_TYPE_ACTIVE,
            .own_addr_type = BLE_ADDR_TYPE_PUBLIC,
            .scan_filter_policy = BLE_SCAN_FILTER_ALLOW_ALL,
            .scan_interval = scanInterval_,
            .scan_window = scanWindow_};

        esp_err_t ret = esp_ble_gap_set_scan_params(&scanParams);
        if (ret != ESP_OK)
        {
            LOG_WARNING(COMPONENT_NAME, "Failed to update ESP32 scan parameters: " + std::string(esp_err_to_name(ret)));
        }
    }
#endif
}

void BLEScanner::setResultCallback(std::function<void(const ProbeRequest &)> callback)
{
    resultCallback_ = callback;
    if (callback)
    {
        LOG_DEBUG(COMPONENT_NAME, "BLE result callback registered");
    }
    else
    {
        LOG_DEBUG(COMPONENT_NAME, "BLE result callback cleared");
    }
}

size_t BLEScanner::getResultCount() const
{
    std::lock_guard<std::mutex> lock(resultsMutex_);
    return scanResults_.size();
}

void BLEScanner::clearResults()
{
    std::lock_guard<std::mutex> lock(resultsMutex_);
    size_t clearedCount = scanResults_.size();
    scanResults_.clear();
    LOGF_DEBUG(COMPONENT_NAME, "Manually cleared %zu BLE scan results", clearedCount);
}

BLEScanner::ScanConfig BLEScanner::getScanConfig() const
{
    return {
        .minRssi = minRssi_,
        .interval = scanInterval_,
        .window = scanWindow_,
        .scanning = scanning_.load()};
}

void BLEScanner::processBLEResult(const uint8_t *address, int rssi,
                                  const uint8_t *advData, size_t advDataLen)
{
    if (!scanning_.load())
    {
        LOG_DEBUG(COMPONENT_NAME, "Ignoring BLE advertisement - scanner not active");
        return;
    }

    if (!isValidAdvertisement(address, rssi, advData, advDataLen))
    {
        LOGF_DEBUG(COMPONENT_NAME, "Invalid BLE advertisement (RSSI: %d, Length: %zu)", rssi, advDataLen);
        return;
    }

    try
    {
        ProbeRequest request;
        request.dataType = "BLE";
        request.timestamp = timeManager_ ? timeManager_->getCurrentDateTime() : Utils::getCurrentTimestamp();
        request.source = "ble";
        request.rssi = rssi;
        request.packetLength = static_cast<int>(advDataLen);
        request.macAddress = formatMACAddress(address);
        request.payload = formatPayload(advData, advDataLen);

        // Extract device name if available
        std::string deviceName = extractDeviceName(advData, advDataLen);
        if (!deviceName.empty())
        {
            // Add device name to payload for easier analysis
            request.payload += " [Name: " + deviceName + "]";
        }

        // Extract service UUIDs if available
        auto serviceUUIDs = extractServiceUUIDs(advData, advDataLen);
        if (!serviceUUIDs.empty())
        {
            request.payload += " [Services: ";
            for (size_t i = 0; i < serviceUUIDs.size(); ++i)
            {
                if (i > 0)
                    request.payload += ",";
                request.payload += serviceUUIDs[i];
            }
            request.payload += "]";
        }

        // Check if MAC should be ignored (using Utils class)
        if (Utils::isIgnoredMAC(request.macAddress))
        {
            LOGF_DEBUG(COMPONENT_NAME, "Ignoring BLE advertisement from filtered MAC: %s",
                       request.macAddress.c_str());
            return;
        }

        // Save the BLE result
        saveBLEResult(request);

        // Call user callback if registered
        if (resultCallback_)
        {
            try
            {
                resultCallback_(request);
            }
            catch (const std::exception &e)
            {
                LOG_ERROR(COMPONENT_NAME, "Error in BLE result callback: " + std::string(e.what()));
            }
        }

#ifdef DEBUG_BLE_SCAN
        LOGF_DEBUG(COMPONENT_NAME, "BLE Advertisement: MAC=%s, RSSI=%d, Length=%d, Name=%s",
                   request.macAddress.c_str(), request.rssi, request.packetLength,
                   deviceName.empty() ? "Unknown" : deviceName.c_str());
#endif
    }
    catch (const std::exception &e)
    {
        LOG_ERROR(COMPONENT_NAME, "Error processing BLE advertisement: " + std::string(e.what()));
    }
}

bool BLEScanner::isValidAdvertisement(const uint8_t *address, int rssi,
                                      const uint8_t *advData, size_t advDataLen)
{
    if (!address)
    {
        LOG_DEBUG(COMPONENT_NAME, "Null address in BLE advertisement");
        return false;
    }

    // Apply RSSI filter (as in your original MIN_RSSI check)
    if (rssi < minRssi_)
    {
        LOGF_DEBUG(COMPONENT_NAME, "BLE advertisement below RSSI threshold: %d < %d", rssi, minRssi_);
        return false;
    }

    if (advDataLen == 0 || advDataLen > MAX_ADV_DATA_LENGTH)
    {
        LOGF_DEBUG(COMPONENT_NAME, "Invalid BLE advertisement data length: %zu", advDataLen);
        return false;
    }

    if (!advData)
    {
        LOG_DEBUG(COMPONENT_NAME, "Null advertisement data");
        return false;
    }

    return true;
}

std::string BLEScanner::formatMACAddress(const uint8_t *address)
{
    if (!address)
    {
        LOG_ERROR(COMPONENT_NAME, "Cannot format null MAC address");
        return "";
    }

    try
    {
        // Format MAC address exactly as in your original code
        char macStr[18];
        std::snprintf(macStr, sizeof(macStr), "%02x:%02x:%02x:%02x:%02x:%02x",
                      address[0], address[1], address[2],
                      address[3], address[4], address[5]);
        return std::string(macStr);
    }
    catch (const std::exception &e)
    {
        LOG_ERROR(COMPONENT_NAME, "Error formatting MAC address: " + std::string(e.what()));
        return "";
    }
}

std::string BLEScanner::formatPayload(const uint8_t *data, size_t length)
{
    if (!data || length == 0)
    {
        LOG_WARNING(COMPONENT_NAME, "Cannot format null/empty BLE advertisement data");
        return "";
    }

    try
    {
        // Create hex string exactly as in your original code
        std::string result;
        result.reserve(length * 3);

        for (size_t i = 0; i < length; ++i)
        {
            char hex[4];
            std::snprintf(hex, sizeof(hex), "%02x ", data[i]);
            result += hex;
        }

        // Remove trailing space
        if (!result.empty() && result.back() == ' ')
        {
            result.pop_back();
        }

        return result;
    }
    catch (const std::exception &e)
    {
        LOG_ERROR(COMPONENT_NAME, "Error formatting BLE payload: " + std::string(e.what()));
        return "";
    }
}

std::string BLEScanner::extractDeviceName(const uint8_t *advData, size_t advDataLen)
{
    if (!advData || advDataLen == 0)
    {
        return "";
    }

    try
    {
        size_t pos = 0;
        while (pos < advDataLen)
        {
            if (pos + 1 >= advDataLen)
                break;

            uint8_t length = advData[pos];
            if (length == 0 || pos + length >= advDataLen)
                break;

            uint8_t type = advData[pos + 1];

            // Check for complete or shortened local name
            if (type == BLE_AD_TYPE_COMPLETE_NAME || type == BLE_AD_TYPE_SHORTENED_NAME)
            {
                if (length > 1)
                {
                    size_t nameLen = length - 1; // Subtract 1 for the type byte
                    std::string name(reinterpret_cast<const char *>(&advData[pos + 2]), nameLen);
                    LOGF_DEBUG(COMPONENT_NAME, "Extracted device name: %s", name.c_str());
                    return name;
                }
            }

            pos += length + 1;
        }
    }
    catch (const std::exception &e)
    {
        LOG_ERROR(COMPONENT_NAME, "Error extracting device name: " + std::string(e.what()));
    }

    return "";
}

std::vector<std::string> BLEScanner::extractServiceUUIDs(const uint8_t *advData, size_t advDataLen)
{
    std::vector<std::string> uuids;

    if (!advData || advDataLen == 0)
    {
        return uuids;
    }

    try
    {
        size_t pos = 0;
        while (pos < advDataLen)
        {
            if (pos + 1 >= advDataLen)
                break;

            uint8_t length = advData[pos];
            if (length == 0 || pos + length >= advDataLen)
                break;

            uint8_t type = advData[pos + 1];

            // Check for 16-bit service UUIDs
            if (type == BLE_AD_TYPE_16BIT_SERVICE_UUID && length >= 3)
            {
                for (size_t i = 2; i < length; i += 2)
                {
                    if (pos + i + 1 < advDataLen)
                    {
                        uint16_t uuid = (advData[pos + i + 1] << 8) | advData[pos + i];
                        char uuidStr[8];
                        std::snprintf(uuidStr, sizeof(uuidStr), "%04x", uuid);
                        uuids.push_back(std::string(uuidStr));
                    }
                }
            }
            // Check for 128-bit service UUIDs
            else if (type == BLE_AD_TYPE_128BIT_SERVICE_UUID && length >= 17)
            {
                // Format 128-bit UUID as string
                char uuidStr[37]; // 32 hex chars + 4 hyphens + null terminator
                const uint8_t *uuidBytes = &advData[pos + 2];
                std::snprintf(uuidStr, sizeof(uuidStr),
                              "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
                              uuidBytes[15], uuidBytes[14], uuidBytes[13], uuidBytes[12],
                              uuidBytes[11], uuidBytes[10], uuidBytes[9], uuidBytes[8],
                              uuidBytes[7], uuidBytes[6], uuidBytes[5], uuidBytes[4],
                              uuidBytes[3], uuidBytes[2], uuidBytes[1], uuidBytes[0]);
                uuids.push_back(std::string(uuidStr));
            }

            pos += length + 1;
        }

        if (!uuids.empty())
        {
            LOGF_DEBUG(COMPONENT_NAME, "Extracted %zu service UUIDs", uuids.size());
        }
    }
    catch (const std::exception &e)
    {
        LOG_ERROR(COMPONENT_NAME, "Error extracting service UUIDs: " + std::string(e.what()));
    }

    return uuids;
}

void BLEScanner::saveBLEResult(const ProbeRequest &request)
{
    try
    {
        // Add to internal results
        {
            std::lock_guard<std::mutex> lock(resultsMutex_);
            scanResults_.push_back(request);

            LOGF_DEBUG(COMPONENT_NAME, "Stored BLE advertisement #%zu from MAC: %s (RSSI: %d)",
                       scanResults_.size(), request.macAddress.c_str(), request.rssi);
        }

        // Save to storage if available (similar to your original sdcardWrite approach)
        if (storage_)
        {
            // Use filename similar to your original approach
            std::string filename = request.timestamp;
            // Replace colons with underscores (as in your original code)
            std::replace(filename.begin(), filename.end(), ':', '_');
            filename += ".csv";

            if (storage_->writeData(request, filename))
            {
                LOG_DEBUG(COMPONENT_NAME, "BLE advertisement written to storage: " + filename);
            }
            else
            {
                LOG_ERROR(COMPONENT_NAME, "Failed to write BLE advertisement to storage");
            }
        }
        else
        {
            LOG_WARNING(COMPONENT_NAME, "No storage available - BLE advertisement not persisted");
        }
    }
    catch (const std::exception &e)
    {
        LOG_ERROR(COMPONENT_NAME, "Error saving BLE advertisement: " + std::string(e.what()));
    }
}

#ifndef ESP32_PLATFORM
// Mock implementation for development platforms
void BLEScanner::simulateBLEDevices()
{
    LOG_DEBUG(COMPONENT_NAME, "Simulating BLE devices for development");

    // Simulate some BLE devices similar to real advertisements
    struct MockBLEDevice
    {
        std::string mac;
        int rssi;
        std::string name;
        std::vector<uint8_t> advData;
    };

    std::vector<MockBLEDevice> devices = {
        {"aa:bb:cc:dd:ee:01", -45, "iPhone 12", {0x02, 0x01, 0x06, 0x0A, 0x09, 'i', 'P', 'h', 'o', 'n', 'e', ' ', '1', '2'}},
        {"aa:bb:cc:dd:ee:02", -67, "Samsung Galaxy", {0x02, 0x01, 0x06, 0x0D, 0x09, 'S', 'a', 'm', 's', 'u', 'n', 'g', ' ', 'G', 'a', 'l', 'a', 'x', 'y'}},
        {"aa:bb:cc:dd:ee:03", -82, "Fitbit Versa", {0x02, 0x01, 0x06, 0x0C, 0x09, 'F', 'i', 't', 'b', 'i', 't', ' ', 'V', 'e', 'r', 's', 'a'}},
        {"aa:bb:cc:dd:ee:04", -91, "", {0x02, 0x01, 0x06}}, // Device without name
    };

    for (const auto &device : devices)
    {
        if (device.rssi >= minRssi_)
        {
            // Convert MAC string to bytes
            uint8_t macBytes[6];
            std::sscanf(device.mac.c_str(), "%hhx:%hhx:%hhx:%hhx:%hhx:%hhx",
                        &macBytes[0], &macBytes[1], &macBytes[2],
                        &macBytes[3], &macBytes[4], &macBytes[5]);

            // Process the simulated device
            processBLEResult(macBytes, device.rssi, device.advData.data(), device.advData.size());
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }
}
#endif

// Static callback for BLE GAP events (kept for interface compatibility)
void BLEScanner::bleGapCallback(int event, void *param)
{
    // This static method is kept for interface compatibility
    // The actual callback is handled by esp32BleGapCallback
    LOG_DEBUG(COMPONENT_NAME, "Static BLE GAP callback called (should not be used)");
}

// Global callback bridge function
void bleScanResultHandler(BLEScanner *scanner, const uint8_t *address,
                          int rssi, const uint8_t *advData, size_t advDataLen)
{
    if (!scanner)
    {
        LOG_ERROR(COMPONENT_NAME, "Null scanner pointer in BLE callback");
        return;
    }

    try
    {
        scanner->processBLEResult(address, rssi, advData, advDataLen);
        LOGF_DEBUG(COMPONENT_NAME, "BLE scan result processed (RSSI: %d, Length: %zu)", rssi, advDataLen);
    }
    catch (const std::exception &e)
    {
        LOG_ERROR(COMPONENT_NAME, "Error in BLE scan result handler: " + std::string(e.what()));
    }
}