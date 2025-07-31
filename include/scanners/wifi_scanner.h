#pragma once

#include "interfaces.h"
#include <mutex>
#include <atomic>
#include <vector>
#include <functional>
#include <memory>

/**
 * @brief WiFi Scanner class for capturing probe requests in promiscuous mode
 *
 * This class implements the IScanner interface to provide WiFi scanning
 * functionality. It captures probe requests from nearby devices and stores
 * them for analysis or transmission.
 */
class WiFiScanner : public IScanner
{
public:
    /**
     * @brief Constructor
     * @param storage Shared pointer to data storage interface
     * @param timeManager Shared pointer to time management interface
     */
    WiFiScanner(std::shared_ptr<IDataStorage> storage,
                std::shared_ptr<ITimeManager> timeManager);

    /**
     * @brief Destructor - automatically cleans up resources
     */
    ~WiFiScanner() override;

    // IScanner interface implementation
    /**
     * @brief Initialize the WiFi scanner
     * @return true if initialization successful, false otherwise
     */
    bool initialize() override;

    /**
     * @brief Start WiFi scanning in promiscuous mode
     * @return true if scan started successfully, false otherwise
     */
    bool startScan() override;

    /**
     * @brief Stop WiFi scanning
     * @return true if scan stopped successfully, false otherwise
     */
    bool stopScan() override;

    /**
     * @brief Get all scan results collected so far
     * @return Vector of ProbeRequest structures
     */
    std::vector<ProbeRequest> getResults() override;

    /**
     * @brief Clean up all resources and reset state
     */
    void cleanup() override;

    /**
     * @brief Set callback function for real-time probe request processing
     * @param callback Function to call when a probe request is detected
     */
    void setProbeCallback(std::function<void(const ProbeRequest &)> callback);

    /**
     * @brief Check if scanner is currently active
     * @return true if scanning is in progress
     */
    bool isScanning() const { return scanning_.load(); }

    /**
     * @brief Get number of probe requests captured
     * @return Count of captured probe requests
     */
    size_t getResultCount() const;

    /**
     * @brief Clear all stored scan results
     */
    void clearResults();

private:
    // Dependencies
    std::shared_ptr<IDataStorage> storage_;
    std::shared_ptr<ITimeManager> timeManager_;

    // Scan results and synchronization
    std::vector<ProbeRequest> scanResults_;
    mutable std::mutex resultsMutex_;
    std::atomic<bool> scanning_;

    // Callback for real-time processing
    std::function<void(const ProbeRequest &)> probeCallback_;

    // Platform-specific implementation
    void *platformHandle_; ///< Platform-specific handle (e.g., ESP32 WiFi handle)

    /**
     * @brief Process a captured probe request packet
     * @param packet Raw packet data
     * @param length Length of packet data
     * @param rssi Received signal strength indicator
     */
    void processProbeRequest(const uint8_t *packet, size_t length, int rssi);

    /**
     * @brief Validate if packet is a valid probe request
     * @param packet Raw packet data
     * @param length Length of packet data
     * @return true if valid probe request
     */
    bool isValidProbeRequest(const uint8_t *packet, size_t length);

    /**
     * @brief Extract MAC address from packet
     * @param packet Raw packet data
     * @return MAC address as string (format: "xx:xx:xx:xx:xx:xx")
     */
    std::string extractMAC(const uint8_t *packet);

    /**
     * @brief Convert packet to hexadecimal string representation
     * @param packet Raw packet data
     * @param length Length of packet data
     * @return Hex string representation of packet
     */
    std::string packetToHexString(const uint8_t *packet, size_t length);

    /**
     * @brief Save probe request to storage and internal results
     * @param request Probe request to save
     */
    void saveProbeRequest(const ProbeRequest &request);

    // Platform-specific callbacks (static for C compatibility)
    /**
     * @brief Static callback for promiscuous mode packets (ESP32)
     * @param buf Packet buffer
     * @param type Packet type
     */
    static void promiscuousCallback(void *buf, int type);

    // Friend function for callback bridging
    friend void wifiScanCallback(WiFiScanner *scanner, void *buf, int type);

    // Constants
    static constexpr uint8_t PROBE_REQUEST_TYPE = 0x40; ///< 802.11 probe request frame type
    static constexpr size_t MIN_PACKET_SIZE = 24;       ///< Minimum valid packet size
    static constexpr size_t MAC_ADDRESS_OFFSET = 10;    ///< Offset of MAC address in packet
};

/**
 * @brief Global callback bridge function for WiFi scan results
 *
 * This function bridges the C-style callback required by the WiFi stack
 * to the C++ class method. It's used internally by the WiFiScanner.
 *
 * @param scanner Pointer to WiFiScanner instance
 * @param buf Packet buffer from WiFi stack
 * @param type Packet type identifier
 */
void wifiScanCallback(WiFiScanner *scanner, void *buf, int type);