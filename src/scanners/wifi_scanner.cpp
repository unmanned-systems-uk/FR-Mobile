// src/scanners/wifi_scanner.cpp - Complete ESP32 Implementation
#ifdef ESP32_PLATFORM
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#elif defined(RASPBERRY_PI_PLATFORM)
// Raspberry Pi WiFi scanning includes
#include <iwlib.h>
#include <linux/wireless.h>
#include <sys/ioctl.h>
#include <unistd.h>
#endif

#include "scanners/wifi_scanner.h"
#include "utils/logger.h"
#include "main.h" // For Utils class and ProbeRequest
#include <cstring>

// Component name for logging
static const std::string COMPONENT_NAME = "WiFiScanner";

// Global pointer for callback bridge (ESP32 requires C-style callbacks)
static WiFiScanner *g_wifiScannerInstance = nullptr;

// ESP32-specific ignored MAC addresses (from your original code)
#ifdef ESP32_PLATFORM
static const uint8_t IGNORED_MACS[][6] = {
    {0xFC, 0xEC, 0xDA, 0x1A, 0xF5, 0x0A},
    {0x02, 0xEC, 0xDA, 0x1A, 0xF5, 0x0A},
    {0x06, 0xEC, 0xDA, 0x1A, 0xF5, 0x0A}};

static bool isHardwareIgnoredMAC(const uint8_t *mac)
{
    for (size_t i = 0; i < sizeof(IGNORED_MACS) / sizeof(IGNORED_MACS[0]); i++)
    {
        if (memcmp(mac, IGNORED_MACS[i], 6) == 0)
        {
            return true;
        }
    }
    return false;
}

// ESP32 promiscuous mode callback (C-style callback required by ESP-IDF)
static void esp32PromiscuousCallback(void *buf, wifi_promiscuous_pkt_type_t type)
{
    if (!g_wifiScannerInstance)
    {
        return;
    }

    try
    {
        wifi_promiscuous_pkt_t *packet = (wifi_promiscuous_pkt_t *)buf;

        // Check if this is a probe request (type 0x40) as in your original code
        if (type == WIFI_PKT_MGMT && packet->payload[0] == 0x40)
        {
            // Extract MAC address from offset 10 (as in your original code)
            const uint8_t *macAddress = packet->payload + 10;

            // Check hardware-level ignored MACs first
            if (!isHardwareIgnoredMAC(macAddress))
            {
                g_wifiScannerInstance->processProbeRequest(
                    packet->payload,
                    packet->rx_ctrl.sig_len,
                    packet->rx_ctrl.rssi);
            }
        }
    }
    catch (const std::exception &e)
    {
        // Can't use LOG_ERROR here as we're in a C callback
        printf("Error in ESP32 promiscuous callback: %s\n", e.what());
    }
}
#endif

// WiFi Scanner Implementation
WiFiScanner::WiFiScanner(std::shared_ptr<IDataStorage> storage,
                         std::shared_ptr<ITimeManager> timeManager)
    : storage_(storage), timeManager_(timeManager), scanning_(false), platformHandle_(nullptr)
{
    LOG_DEBUG(COMPONENT_NAME, "WiFi Scanner instance created");

#ifdef ESP32_PLATFORM
    // Set global instance for callback bridge
    g_wifiScannerInstance = this;
#endif
}

WiFiScanner::~WiFiScanner()
{
    LOG_DEBUG(COMPONENT_NAME, "WiFi Scanner destructor called");
    cleanup();

#ifdef ESP32_PLATFORM
    // Clear global instance
    if (g_wifiScannerInstance == this)
    {
        g_wifiScannerInstance = nullptr;
    }
#endif
}

bool WiFiScanner::initialize()
{
    try
    {
        LOG_INFO(COMPONENT_NAME, "Initializing WiFi Scanner...");

#ifdef ESP32_PLATFORM
        LOG_INFO(COMPONENT_NAME, "Initializing ESP32 WiFi stack...");

        // Initialize NVS (required for WiFi)
        esp_err_t ret = nvs_flash_init();
        if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
        {
            ESP_ERROR_CHECK(nvs_flash_erase());
            ret = nvs_flash_init();
        }
        ESP_ERROR_CHECK(ret);

        // Initialize network interface
        ESP_ERROR_CHECK(esp_netif_init());

        // Create default event loop
        ESP_ERROR_CHECK(esp_event_loop_create_default());

        // Initialize WiFi with default config
        wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
        ESP_ERROR_CHECK(esp_wifi_init(&cfg));

        // Set WiFi mode to station (as in your original code)
        ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));

        // Start WiFi
        ESP_ERROR_CHECK(esp_wifi_start());

        // Disconnect from any AP (as in your original code)
        esp_wifi_disconnect();

        // Set promiscuous mode callback
        ESP_ERROR_CHECK(esp_wifi_set_promiscuous_rx_cb(esp32PromiscuousCallback));

        LOG_INFO(COMPONENT_NAME, "ESP32 WiFi stack initialized successfully");

#else
        LOG_WARNING(COMPONENT_NAME, "Running on development platform - WiFi scanning will be mocked");
#endif

        LOG_INFO(COMPONENT_NAME, "WiFi Scanner initialization successful");
        return true;
    }
    catch (const std::exception &e)
    {
        LOG_ERROR(COMPONENT_NAME, "Initialization failed: " + std::string(e.what()));
        return false;
    }
}

bool WiFiScanner::startScan()
{
    if (scanning_.load())
    {
        LOG_WARNING(COMPONENT_NAME, "Scan already in progress");
        return false;
    }

    try
    {
        LOG_INFO(COMPONENT_NAME, "Starting WiFi scan...");

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
        // Enable promiscuous mode (as in your original code)
        esp_err_t result = esp_wifi_set_promiscuous(true);
        if (result != ESP_OK)
        {
            LOG_ERROR(COMPONENT_NAME, "Failed to enable promiscuous mode: " + std::string(esp_err_to_name(result)));
            scanning_.store(false);
            return false;
        }
        LOG_DEBUG(COMPONENT_NAME, "ESP32 promiscuous mode enabled");
#else
        LOG_DEBUG(COMPONENT_NAME, "Mock scan mode activated");
        // Simulate some WiFi devices for development
        simulateWiFiDevices();
#endif

        LOG_INFO(COMPONENT_NAME, "WiFi scan started successfully");
        return true;
    }
    catch (const std::exception &e)
    {
        scanning_.store(false);
        LOG_ERROR(COMPONENT_NAME, "Failed to start scan: " + std::string(e.what()));
        return false;
    }
}

bool WiFiScanner::stopScan()
{
    if (!scanning_.load())
    {
        LOG_DEBUG(COMPONENT_NAME, "No active scan to stop");
        return true;
    }

    try
    {
        LOG_INFO(COMPONENT_NAME, "Stopping WiFi scan...");
        scanning_.store(false);

#ifdef ESP32_PLATFORM
        // Disable promiscuous mode (as in your original code)
        esp_err_t result = esp_wifi_set_promiscuous(false);
        if (result != ESP_OK)
        {
            LOG_WARNING(COMPONENT_NAME, "Failed to disable promiscuous mode: " + std::string(esp_err_to_name(result)));
        }
        LOG_DEBUG(COMPONENT_NAME, "ESP32 promiscuous mode disabled");
#endif

        size_t resultCount = getResultCount();
        LOGF_INFO(COMPONENT_NAME, "WiFi scan stopped - captured %zu probe requests", resultCount);

        return true;
    }
    catch (const std::exception &e)
    {
        LOG_ERROR(COMPONENT_NAME, "Failed to stop scan: " + std::string(e.what()));
        return false;
    }
}

std::vector<ProbeRequest> WiFiScanner::getResults()
{
    std::lock_guard<std::mutex> lock(resultsMutex_);
    LOGF_DEBUG(COMPONENT_NAME, "Returning %zu scan results", scanResults_.size());
    return scanResults_;
}

void WiFiScanner::cleanup()
{
    LOG_INFO(COMPONENT_NAME, "Cleaning up WiFi Scanner...");

    // Stop scanning if active
    if (scanning_.load())
    {
        stopScan();
    }

#ifdef ESP32_PLATFORM
    // Platform-specific cleanup
    esp_wifi_set_promiscuous_rx_cb(nullptr);
    esp_wifi_stop();
    esp_wifi_deinit();
    LOG_DEBUG(COMPONENT_NAME, "ESP32 WiFi cleanup completed");
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

    LOG_INFO(COMPONENT_NAME, "WiFi Scanner cleanup completed");
}

void WiFiScanner::setProbeCallback(std::function<void(const ProbeRequest &)> callback)
{
    probeCallback_ = callback;
    if (callback)
    {
        LOG_DEBUG(COMPONENT_NAME, "Probe request callback registered");
    }
    else
    {
        LOG_DEBUG(COMPONENT_NAME, "Probe request callback cleared");
    }
}

size_t WiFiScanner::getResultCount() const
{
    std::lock_guard<std::mutex> lock(resultsMutex_);
    return scanResults_.size();
}

void WiFiScanner::clearResults()
{
    std::lock_guard<std::mutex> lock(resultsMutex_);
    size_t clearedCount = scanResults_.size();
    scanResults_.clear();
    LOGF_DEBUG(COMPONENT_NAME, "Manually cleared %zu scan results", clearedCount);
}

void WiFiScanner::processProbeRequest(const uint8_t *packet, size_t length, int rssi)
{
    if (!scanning_.load())
    {
        LOG_DEBUG(COMPONENT_NAME, "Ignoring probe request - scanner not active");
        return;
    }

    if (!isValidProbeRequest(packet, length))
    {
        LOGF_DEBUG(COMPONENT_NAME, "Invalid probe request packet (length: %zu, type: 0x%02x)",
                   length, packet ? packet[0] : 0);
        return;
    }

    try
    {
        ProbeRequest request;
        request.dataType = "Wi-Fi";
        request.timestamp = timeManager_ ? timeManager_->getCurrentDateTime() : Utils::getCurrentTimestamp();
        request.source = "wifi";
        request.rssi = rssi;
        request.packetLength = static_cast<int>(length);
        request.macAddress = extractMAC(packet);
        request.payload = packetToHexString(packet, length);

        // Check if MAC should be ignored (using Utils class as well)
        if (Utils::isIgnoredMAC(request.macAddress))
        {
            LOGF_DEBUG(COMPONENT_NAME, "Ignoring probe request from filtered MAC: %s",
                       request.macAddress.c_str());
            return;
        }

        // Save the probe request
        saveProbeRequest(request);

        // Call user callback if registered
        if (probeCallback_)
        {
            try
            {
                probeCallback_(request);
            }
            catch (const std::exception &e)
            {
                LOG_ERROR(COMPONENT_NAME, "Error in probe callback: " + std::string(e.what()));
            }
        }

#ifdef DEBUG_WIFI_SCAN
        LOGF_DEBUG(COMPONENT_NAME, "Probe Request: MAC=%s, RSSI=%d, Length=%d, Time=%s",
                   request.macAddress.c_str(), request.rssi, request.packetLength,
                   request.timestamp.c_str());
#endif
    }
    catch (const std::exception &e)
    {
        LOG_ERROR(COMPONENT_NAME, "Error processing probe request: " + std::string(e.what()));
    }
}

bool WiFiScanner::isValidProbeRequest(const uint8_t *packet, size_t length)
{
    if (!packet)
    {
        LOG_DEBUG(COMPONENT_NAME, "Null packet received");
        return false;
    }

    if (length < MIN_PACKET_SIZE)
    {
        LOGF_DEBUG(COMPONENT_NAME, "Packet too small: %zu bytes (minimum: %zu)",
                   length, MIN_PACKET_SIZE);
        return false;
    }

    // Check for probe request type (0x40 as in your original code)
    if (packet[0] != PROBE_REQUEST_TYPE)
    {
        LOGF_DEBUG(COMPONENT_NAME, "Wrong packet type: 0x%02x (expected: 0x%02x)",
                   packet[0], PROBE_REQUEST_TYPE);
        return false;
    }

    return true;
}

std::string WiFiScanner::extractMAC(const uint8_t *packet)
{
    if (!packet)
    {
        LOG_ERROR(COMPONENT_NAME, "Cannot extract MAC from null packet");
        return "";
    }

    try
    {
        // Extract MAC from offset 10 (as in your original code)
        char macStr[18];
        std::snprintf(macStr, sizeof(macStr), "%02x:%02x:%02x:%02x:%02x:%02x",
                      packet[10], packet[11], packet[12],
                      packet[13], packet[14], packet[15]);
        return std::string(macStr);
    }
    catch (const std::exception &e)
    {
        LOG_ERROR(COMPONENT_NAME, "Error extracting MAC address: " + std::string(e.what()));
        return "";
    }
}

std::string WiFiScanner::packetToHexString(const uint8_t *packet, size_t length)
{
    if (!packet || length == 0)
    {
        LOG_WARNING(COMPONENT_NAME, "Cannot convert null/empty packet to hex string");
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
            std::snprintf(hex, sizeof(hex), "%02x ", packet[i]);
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
        LOG_ERROR(COMPONENT_NAME, "Error converting packet to hex: " + std::string(e.what()));
        return "";
    }
}

void WiFiScanner::saveProbeRequest(const ProbeRequest &request)
{
    try
    {
        // Add to internal results
        {
            std::lock_guard<std::mutex> lock(resultsMutex_);
            scanResults_.push_back(request);

            LOGF_DEBUG(COMPONENT_NAME, "Stored probe request #%zu from MAC: %s (RSSI: %d)",
                       scanResults_.size(), request.macAddress.c_str(), request.rssi);
        }

        // Save to storage if available
        if (storage_)
        {
            // Use filename similar to your original approach
            std::string filename = request.timestamp;
            // Replace colons with underscores (as in your original code)
            std::replace(filename.begin(), filename.end(), ':', '_');
            filename += ".csv";

            if (storage_->writeData(request, filename))
            {
                LOG_DEBUG(COMPONENT_NAME, "Probe request written to storage: " + filename);
            }
            else
            {
                LOG_ERROR(COMPONENT_NAME, "Failed to write probe request to storage");
            }
        }
        else
        {
            LOG_WARNING(COMPONENT_NAME, "No storage available - probe request not persisted");
        }
    }
    catch (const std::exception &e)
    {
        LOG_ERROR(COMPONENT_NAME, "Error saving probe request: " + std::string(e.what()));
    }
}

#ifndef ESP32_PLATFORM
// Mock implementation for development platforms
void WiFiScanner::simulateWiFiDevices()
{
    LOG_DEBUG(COMPONENT_NAME, "Simulating WiFi devices for development");

    // Simulate some probe requests similar to real devices
    struct MockProbeRequest
    {
        std::string mac;
        int rssi;
        std::vector<uint8_t> packet;
    };

    std::vector<MockProbeRequest> mockDevices = {
        {"aa:bb:cc:dd:ee:01", -45, {0x40, 0x00, 0x3c, 0x01, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0x01, 0x50, 0x51, 0x52, 0x53, 0x54, 0x55, 0x60, 0x61}},
        {"bb:cc:dd:ee:ff:02", -67, {0x40, 0x00, 0x3c, 0x01, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xbb, 0xcc, 0xdd, 0xee, 0xff, 0x02, 0x60, 0x61, 0x62, 0x63, 0x64, 0x65, 0x70, 0x71}},
        {"cc:dd:ee:ff:00:03", -82, {0x40, 0x00, 0x3c, 0x01, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xcc, 0xdd, 0xee, 0xff, 0x00, 0x03, 0x70, 0x71, 0x72, 0x73, 0x74, 0x75, 0x80, 0x81}}};

    for (const auto &device : mockDevices)
    {
        processProbeRequest(device.packet.data(), device.packet.size(), device.rssi);
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}
#endif

// Static callback for promiscuous mode (not needed in current implementation)
void WiFiScanner::promiscuousCallback(void *buf, int type)
{
    // This static method is kept for interface compatibility
    // The actual callback is handled by esp32PromiscuousCallback
    LOG_DEBUG(COMPONENT_NAME, "Static promiscuous callback called (should not be used)");
}

// Global callback bridge function
void wifiScanCallback(WiFiScanner *scanner, void *buf, int type)
{
    if (!scanner)
    {
        LOG_ERROR(COMPONENT_NAME, "Null scanner pointer in callback");
        return;
    }

    try
    {
#ifdef ESP32_PLATFORM
        wifi_promiscuous_pkt_t *packet = (wifi_promiscuous_pkt_t *)buf;
        if (type == WIFI_PKT_MGMT)
        {
            scanner->processProbeRequest(packet->payload, packet->rx_ctrl.sig_len, packet->rx_ctrl.rssi);
        }
        LOGF_DEBUG(COMPONENT_NAME, "WiFi scan callback triggered (type: %d)", type);
#else
        // Mock implementation for development
        LOG_DEBUG(COMPONENT_NAME, "Mock WiFi scan callback triggered");
#endif
    }
    catch (const std::exception &e)
    {
        LOG_ERROR(COMPONENT_NAME, "Error in WiFi scan callback: " + std::string(e.what()));
    }
}