#pragma once

#include "interfaces.h"
#include <string>
#include <vector>
#include <memory>
#include <chrono>
#include <mutex>
#include <atomic>

/**
 * @file cellular_manager.h
 * @brief Cellular Manager for SIM7600X network communication and data upload
 * @author Anthony Kirk
 * @date 2025-07-29
 * @version 1.0.0
 *
 * This class provides comprehensive cellular communication capabilities including
 * network connection management, HTTP operations, data upload, and network time
 * synchronization using the SIM7600X cellular module.
 */

/**
 * @brief Network signal information structure
 */
struct SignalInfo
{
    int rssi = -999;          ///< Received Signal Strength Indicator (dBm)
    int ber = 99;             ///< Bit Error Rate (0-7, 99=unknown)
    int rsrp = -999;          ///< Reference Signal Received Power (dBm)
    int rsrq = -999;          ///< Reference Signal Received Quality (dB)
    int sinr = -999;          ///< Signal to Interference plus Noise Ratio (dB)
    std::string networkType;  ///< Network type (2G, 3G, 4G, LTE)
    std::string operatorName; ///< Network operator name
    bool roaming = false;     ///< Roaming status
};

/**
 * @brief SIM card information structure
 */
struct SIMInfo
{
    std::string imei;         ///< International Mobile Equipment Identity
    std::string imsi;         ///< International Mobile Subscriber Identity
    std::string iccid;        ///< Integrated Circuit Card Identifier
    std::string phoneNumber;  ///< Phone number if available
    std::string operatorName; ///< SIM operator name
    bool pinRequired = false; ///< PIN requirement status
    bool ready = false;       ///< SIM ready status
};

/**
 * @brief Network connection statistics for monitoring
 */
struct ConnectionStats
{
    uint64_t bytesTransmitted = 0;       ///< Total bytes transmitted
    uint64_t bytesReceived = 0;          ///< Total bytes received
    uint32_t successfulConnections = 0;  ///< Number of successful connections
    uint32_t failedConnections = 0;      ///< Number of failed connections
    uint32_t httpRequestsSent = 0;       ///< Number of HTTP requests sent
    uint32_t httpRequestsSuccessful = 0; ///< Number of successful HTTP requests
    uint64_t totalConnectionTime = 0;    ///< Total time connected (seconds)
    uint64_t lastConnectionTime = 0;     ///< Timestamp of last connection
    std::string lastError;               ///< Last error message
};

/**
 * @brief HTTP response structure
 */
struct HTTPResponse
{
    int statusCode = 0;                        ///< HTTP status code (200, 404, etc.)
    std::string statusText;                    ///< HTTP status text
    std::string body;                          ///< Response body content
    std::string headers;                       ///< Response headers
    size_t contentLength = 0;                  ///< Content length in bytes
    bool success = false;                      ///< Overall success flag
    std::chrono::milliseconds responseTime{0}; ///< Response time
};

/**
 * @brief Cellular Manager implementing comprehensive network communication
 *
 * This class provides a complete interface for cellular communication using the
 * SIM7600X module. Features include:
 *
 * - Network connection management with automatic retry
 * - HTTP/HTTPS operations for data upload
 * - Network time synchronization (NITZ)
 * - Signal quality monitoring and reporting
 * - SIM card management and authentication
 * - Data transmission in configurable chunks
 * - Connection health monitoring and recovery
 * - Comprehensive error handling and logging
 * - Thread-safe operations with proper synchronization
 */
class CellularManager : public INetworkInterface
{
public:
    /**
     * @brief Constructor
     * @param rxPin UART RX pin for SIM7600X communication (default: 0)
     * @param txPin UART TX pin for SIM7600X communication (default: 4)
     * @param netPin Network status monitoring pin (default: 33)
     */
    CellularManager(int rxPin = 0, int txPin = 4, int netPin = 33);

    /**
     * @brief Destructor - ensures proper cleanup and disconnection
     */
    ~CellularManager() override;

    // INetworkInterface implementation
    /**
     * @brief Connect to cellular network with full initialization
     * @return true if connection successful, false otherwise
     */
    bool connect() override;

    /**
     * @brief Disconnect from cellular network and cleanup
     * @return true if disconnection successful, false otherwise
     */
    bool disconnect() override;

    /**
     * @brief Check if currently connected to network
     * @return true if connected, false otherwise
     */
    bool isConnected() override;

    /**
     * @brief Send data via HTTP POST to configured endpoint
     * @param data JSON data to send
     * @return true if transmission successful, false otherwise
     */
    bool sendData(const std::string &data) override;

    /**
     * @brief Get current network time from cellular provider
     * @return Network time string in format "YY/MM/DD,HH:MM:SS+ZZ" or error message
     */
    std::string getNetworkTime() override;

    // Cellular-specific operations
    /**
     * @brief Initialize SIM7600X module and serial communication
     * @param baudRate Serial communication baud rate (default: 115200)
     * @param timeoutMs Initialization timeout in milliseconds (default: 30000)
     * @return true if initialization successful, false otherwise
     */
    bool initialize(uint32_t baudRate = 115200, uint32_t timeoutMs = 30000);

    /**
     * @brief Get comprehensive SIM card information
     * @return SIM information structure
     */
    SIMInfo getSIMInfo();

    /**
     * @brief Get current signal quality and network information
     * @return Signal information structure
     */
    SignalInfo getSignalInfo();

    /**
     * @brief Get connection and transmission statistics
     * @return Statistics structure with performance data
     */
    ConnectionStats getConnectionStats() const;

    // HTTP operations
    /**
     * @brief Configure HTTP service with endpoint and parameters
     * @param url Target URL for HTTP requests
     * @param contentType Content type header (default: "application/json")
     * @return true if HTTP service setup successful, false otherwise
     */
    bool setupHTTPService(const std::string &url, const std::string &contentType = "application/json");

    /**
     * @brief Terminate HTTP service and cleanup
     * @return true if termination successful, false otherwise
     */
    bool terminateHTTPService();

    /**
     * @brief Send HTTP POST request with data
     * @param data Data to send in request body
     * @param moreData Whether more data will follow (for chunked transfer)
     * @return HTTP response structure with status and content
     */
    HTTPResponse sendHTTPRequest(const std::string &data, bool moreData = false);

    /**
     * @brief Send large file in chunks via HTTP
     * @param filename Local file to upload
     * @param storage Storage interface for file access
     * @param chunkSize Size of each chunk in bytes (default: 4096)
     * @return true if entire file uploaded successfully, false otherwise
     */
    bool sendDataInChunks(const std::string &filename,
                          std::shared_ptr<IDataStorage> storage,
                          size_t chunkSize = 4096);

    // Network time and synchronization
    /**
     * @brief Enable Network Identity and Time Zone (NITZ) updates
     * @return true if NITZ enabled successfully, false otherwise
     */
    bool enableNITZ();

    /**
     * @brief Enable Clock Time Zone Reporting (CTZR)
     * @return true if CTZR enabled successfully, false otherwise
     */
    bool enableCTZR();

    /**
     * @brief Validate network time format and content
     * @param timeString Time string to validate
     * @return true if time string is valid, false otherwise
     */
    bool isValidNetworkTime(const std::string &timeString);

    // Connection management
    /**
     * @brief Test network connectivity with ping or basic HTTP request
     * @param testUrl URL to test connectivity (default: uses configured endpoint)
     * @return true if network is reachable, false otherwise
     */
    bool testNetworkConnectivity(const std::string &testUrl = "");

    /**
     * @brief Check if SIM card is ready and network registered
     * @return true if SIM ready and registered, false otherwise
     */
    bool checkSIMReady();

    /**
     * @brief Monitor network status using NET pin signal
     * @param timeoutMs Maximum time to wait for stable signal (default: 10000)
     * @return true if network status is stable, false if timeout or error
     */
    bool monitorNetworkStatus(uint32_t timeoutMs = 10000);

    // Configuration and settings
    /**
     * @brief Configure Access Point Name (APN) for data connection
     * @param apn APN string (default: "everywhere")
     * @param username APN username (optional)
     * @param password APN password (optional)
     * @return true if APN configuration successful, false otherwise
     */
    bool configureAPN(const std::string &apn = "everywhere",
                      const std::string &username = "",
                      const std::string &password = "");

    /**
     * @brief Set HTTP endpoint URL for data transmission
     * @param url Target URL for HTTP requests
     */
    void setHTTPEndpoint(const std::string &url) { httpEndpoint_ = url; }

    /**
     * @brief Set maximum retry attempts for failed operations
     * @param maxRetries Maximum number of retry attempts (default: 3)
     */
    void setMaxRetries(int maxRetries) { maxRetries_ = maxRetries; }

    /**
     * @brief Set timeout for AT command responses
     * @param timeoutMs Timeout in milliseconds (default: 5000)
     */
    void setCommandTimeout(uint32_t timeoutMs) { commandTimeoutMs_ = timeoutMs; }

    // Error handling and diagnostics
    /**
     * @brief Get last error message for diagnostics
     * @return String describing the last error that occurred
     */
    std::string getLastError() const { return lastError_; }

    /**
     * @brief Perform comprehensive module health check
     * @return true if module appears healthy, false if issues detected
     */
    bool performHealthCheck();

    /**
     * @brief Reset SIM7600X module (hardware or software reset)
     * @param hardReset Whether to perform hardware reset (default: false)
     * @return true if reset successful, false otherwise
     */
    bool resetModule(bool hardReset = false);

private:
    // Hardware configuration
    int rxPin_;         ///< UART RX pin number
    int txPin_;         ///< UART TX pin number
    int netPin_;        ///< Network status monitoring pin
    uint32_t baudRate_; ///< Serial communication baud rate

    // Connection state
    std::atomic<bool> connected_;         ///< Connection status flag
    std::atomic<bool> httpServiceActive_; ///< HTTP service status flag
    bool initialized_;                    ///< Initialization status

    // Configuration
    std::string apnName_;       ///< Access Point Name
    std::string apnUsername_;   ///< APN username
    std::string apnPassword_;   ///< APN password
    std::string httpEndpoint_;  ///< HTTP endpoint URL
    int maxRetries_;            ///< Maximum retry attempts
    uint32_t commandTimeoutMs_; ///< AT command timeout

    // State tracking
    SIMInfo simInfo_;       ///< Current SIM information
    SignalInfo signalInfo_; ///< Current signal information
    ConnectionStats stats_; ///< Connection statistics
    std::string lastError_; ///< Last error message

    // Thread safety
    mutable std::mutex operationMutex_; ///< Mutex for thread-safe operations
    mutable std::mutex statsMutex_;     ///< Mutex for statistics updates

    // Platform-specific handle
    void *serialHandle_; ///< Platform-specific serial handle

    // AT command operations
    /**
     * @brief Send AT command and wait for expected response
     * @param command AT command to send (without "AT" prefix)
     * @param expectedResponse Expected response string
     * @param maxAttempts Maximum number of attempts (default: 3)
     * @param timeoutMs Timeout per attempt in milliseconds
     * @return true if expected response received, false otherwise
     */
    bool sendATCommand(const std::string &command,
                       const std::string &expectedResponse,
                       int maxAttempts = 3,
                       uint32_t timeoutMs = 0); // 0 = use default timeout

    /**
     * @brief Send AT command for HTTP POST operations with extended timeout
     * @param command AT command to send
     * @param expectedResponse Expected response string
     * @param maxAttempts Maximum number of attempts (default: 1)
     * @param timeoutMs Timeout in milliseconds (default: 30000)
     * @return true if expected response received, false otherwise
     */
    bool sendATCommandPost(const std::string &command,
                           const std::string &expectedResponse,
                           int maxAttempts = 1,
                           uint32_t timeoutMs = 30000);

    /**
     * @brief Read response from serial port with timeout
     * @param timeoutMs Maximum time to wait for response
     * @return Response string, empty if timeout
     */
    std::string readSerialResponse(uint32_t timeoutMs);

    /**
     * @brief Clear serial input buffer
     */
    void clearSerialBuffer();

    // Network operations
    /**
     * @brief Configure and activate PDP context for data connection
     * @return true if PDP context activated successfully, false otherwise
     */
    bool activatePDPContext();

    /**
     * @brief Deactivate PDP context
     * @return true if deactivation successful, false otherwise
     */
    bool deactivatePDPContext();

    /**
     * @brief Check network registration status
     * @return true if registered to network, false otherwise
     */
    bool checkNetworkRegistration();

    // Data formatting and transmission
    /**
     * @brief Create JSON payload for asset data and scan results
     * @param asset Asset information structure
     * @param dataLines Vector of CSV data lines
     * @return Formatted JSON string for transmission
     */
    std::string createJSONPayload(const AssetInfo &asset,
                                  const std::vector<std::string> &dataLines);

    /**
     * @brief Escape JSON string for safe transmission
     * @param input String to escape
     * @return Escaped string safe for JSON
     */
    std::string escapeJSONString(const std::string &input);

    /**
     * @brief Count lines in specified file
     * @param filename File to count lines in
     * @param storage Storage interface for file access
     * @return Number of lines in file, -1 if error
     */
    int countLinesInFile(const std::string &filename,
                         std::shared_ptr<IDataStorage> storage);

    // Statistics and monitoring
    /**
     * @brief Update connection statistics
     * @param operation Type of operation performed
     * @param success Whether operation was successful
     * @param bytes Number of bytes involved (default: 0)
     */
    void updateStats(const std::string &operation, bool success, size_t bytes = 0);

    /**
     * @brief Update signal information from module
     */
    void updateSignalInfo();

    /**
     * @brief Update SIM information from module
     */
    void updateSIMInfo();

    // Platform-specific implementations
    /**
     * @brief Platform-specific serial port initialization
     * @return true if initialization successful, false otherwise
     */
    bool platformInitializeSerial();

    /**
     * @brief Platform-specific serial port cleanup
     */
    void platformCleanupSerial();

    /**
     * @brief Platform-specific GPIO configuration for NET pin
     * @return true if GPIO setup successful, false otherwise
     */
    bool platformSetupGPIO();

    // Constants
    static constexpr uint32_t DEFAULT_BAUD_RATE = 115200;        ///< Default baud rate
    static constexpr uint32_t DEFAULT_COMMAND_TIMEOUT = 5000;    ///< Default AT command timeout
    static constexpr uint32_t DEFAULT_HTTP_TIMEOUT = 30000;      ///< Default HTTP timeout
    static constexpr int DEFAULT_MAX_RETRIES = 3;                ///< Default maximum retries
    static constexpr size_t SERIAL_BUFFER_SIZE = 1024;           ///< Serial buffer size
    static constexpr size_t MAX_HTTP_CHUNK_SIZE = 4096;          ///< Maximum HTTP chunk size
    static constexpr uint32_t NET_PIN_FLASH_DELAY_US = 50000000; ///< NET pin flash delay (50ms)
    static constexpr int NET_PIN_TEST_FLASHES = 2;               ///< Number of flashes to test
    static constexpr const char *DEFAULT_APN = "everywhere";     ///< Default APN name
    static constexpr const char *DEFAULT_HTTP_ENDPOINT =         ///< Default API endpoint
        "https://uk-610246-forestryresearchapi-app-dev-01.azurewebsites.net/api/DeviceData";
};