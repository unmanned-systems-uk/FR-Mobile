#include "data/cellular_manager.h"
#include "utils/logger.h"
#include "main.h"
#include <sstream>
#include <iomanip>
#include <regex>
#include <algorithm>
#include <chrono>

#ifdef ESP32_PLATFORM
#include <driver/uart.h>
#include <driver/gpio.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#elif defined(RASPBERRY_PI_PLATFORM)
// Raspberry Pi specific includes
#include <termios.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/gpio.h>
#include <thread>
#include <chrono>
#else
// Generic development platform
#include <thread>
#include <chrono>
#endif

// Constructor
CellularManager::CellularManager(int rxPin, int txPin, int netPin)
    : rxPin_(rxPin), txPin_(txPin), netPin_(netPin), baudRate_(DEFAULT_BAUD_RATE), connected_(false), httpServiceActive_(false), initialized_(false), maxRetries_(DEFAULT_MAX_RETRIES), commandTimeoutMs_(DEFAULT_COMMAND_TIMEOUT), serialHandle_(nullptr)
{
    // Initialize structures
    simInfo_ = SIMInfo();
    signalInfo_ = SignalInfo();
    stats_ = ConnectionStats();

    // Set default configuration
    apnName_ = DEFAULT_APN;
    httpEndpoint_ = DEFAULT_HTTP_ENDPOINT;

    LOG_INFO("[CellularManager] Created with SIM7600X on RX:" + std::to_string(rxPin_) +
             ", TX:" + std::to_string(txPin_) + ", NET:" + std::to_string(netPin_));
}

// Destructor
CellularManager::~CellularManager()
{
    if (initialized_)
    {
        if (connected_)
        {
            disconnect();
        }
        platformCleanupSerial();
    }
}

// Initialize SIM7600X module
bool CellularManager::initialize(uint32_t baudRate, uint32_t timeoutMs)
{
    if (initialized_)
    {
        LOG_WARNING("[CellularManager] Already initialized");
        return true;
    }

    LOG_INFO("[CellularManager] Initializing SIM7600X module");

    baudRate_ = baudRate;

    try
    {
        // Setup GPIO for NET pin monitoring
        if (!platformSetupGPIO())
        {
            lastError_ = "Failed to setup GPIO for NET pin";
            LOG_ERROR("[CellularManager] " + lastError_);
            return false;
        }

        // Initialize serial communication
        if (!platformInitializeSerial())
        {
            lastError_ = "Failed to initialize serial communication";
            LOG_ERROR("[CellularManager] " + lastError_);
            return false;
        }

        // Test basic AT communication
        if (!sendATCommand("", "OK", maxRetries_, timeoutMs))
        {
            lastError_ = "No response from modem - check power and connections";
            LOG_ERROR("[CellularManager] " + lastError_);
            return false;
        }

        // Disable echo
        sendATCommand("E0", "OK");

        // Get module information
        std::string response;
        if (sendATCommand("+CGMM", "OK"))
        {
            response = readSerialResponse(1000);
            LOG_INFO("[CellularManager] Module: " + response);
        }

        // Enable error reporting
        sendATCommand("+CMEE=2", "OK");

        // Configure network registration reporting
        sendATCommand("+CREG=2", "OK");
        sendATCommand("+CGREG=2", "OK");

        // Update SIM information
        updateSIMInfo();

        initialized_ = true;
        LOG_INFO("[CellularManager] Initialization complete");

        return true;
    }
    catch (const std::exception &e)
    {
        lastError_ = std::string("Exception during initialization: ") + e.what();
        LOG_ERROR("[CellularManager] " + lastError_);
        return false;
    }
}

// Connect to cellular network
bool CellularManager::connect()
{
    if (!initialized_)
    {
        LOG_ERROR("[CellularManager] Not initialized");
        return false;
    }

    if (connected_)
    {
        LOG_INFO("[CellularManager] Already connected");
        return true;
    }

    LOG_INFO("[CellularManager] Connecting to cellular network");

    std::lock_guard<std::mutex> lock(operationMutex_);

    // Check SIM ready
    if (!checkSIMReady())
    {
        stats_.failedConnections++;
        updateStats("connect", false);
        return false;
    }

    // Configure APN
    if (!configureAPN(apnName_, apnUsername_, apnPassword_))
    {
        LOG_WARNING("[CellularManager] Failed to configure APN, continuing anyway");
    }

    // Wait for network registration
    if (!checkNetworkRegistration())
    {
        lastError_ = "Failed to register on network";
        LOG_ERROR("[CellularManager] " + lastError_);
        stats_.failedConnections++;
        updateStats("connect", false);
        return false;
    }

    // Activate PDP context
    if (!activatePDPContext())
    {
        lastError_ = "Failed to activate data connection";
        LOG_ERROR("[CellularManager] " + lastError_);
        stats_.failedConnections++;
        updateStats("connect", false);
        return false;
    }

    // Update signal and connection info
    updateSignalInfo();

    connected_ = true;
    stats_.successfulConnections++;
    stats_.lastConnectionTime = std::chrono::duration_cast<std::chrono::seconds>(
                                    std::chrono::system_clock::now().time_since_epoch())
                                    .count();

    updateStats("connect", true);

    LOG_INFO("[CellularManager] Connected successfully");
    LOG_INFO("[CellularManager] Operator: " + signalInfo_.operatorName);
    LOG_INFO("[CellularManager] Network: " + signalInfo_.networkType);
    LOG_INFO("[CellularManager] Signal: " + std::to_string(signalInfo_.rssi) + " dBm");

    return true;
}

// Disconnect from network
bool CellularManager::disconnect()
{
    if (!connected_)
    {
        return true;
    }

    LOG_INFO("[CellularManager] Disconnecting from cellular network");

    std::lock_guard<std::mutex> lock(operationMutex_);

    // Terminate HTTP service if active
    if (httpServiceActive_)
    {
        terminateHTTPService();
    }

    // Deactivate PDP context
    deactivatePDPContext();

    connected_ = false;

    // Update connection time statistics
    if (stats_.lastConnectionTime > 0)
    {
        uint64_t sessionDuration = std::chrono::duration_cast<std::chrono::seconds>(
                                       std::chrono::system_clock::now().time_since_epoch())
                                       .count() -
                                   stats_.lastConnectionTime;
        stats_.totalConnectionTime += sessionDuration;
    }

    LOG_INFO("[CellularManager] Disconnected successfully");
    return true;
}

// Check connection status
bool CellularManager::isConnected()
{
    if (!initialized_ || !connected_)
    {
        return false;
    }

    // Quick AT check
    return sendATCommand("", "OK", 1, 1000);
}

// Send data via HTTP
bool CellularManager::sendData(const std::string &data)
{
    if (!connected_)
    {
        LOG_ERROR("[CellularManager] Not connected");
        return false;
    }

    LOG_INFO("[CellularManager] Sending " + std::to_string(data.length()) + " bytes");

    std::lock_guard<std::mutex> lock(operationMutex_);

    // Setup HTTP service if needed
    if (!httpServiceActive_)
    {
        if (!setupHTTPService(httpEndpoint_))
        {
            return false;
        }
    }

    // Send HTTP request
    HTTPResponse response = sendHTTPRequest(data);

    if (response.success)
    {
        LOG_INFO("[CellularManager] Data sent successfully");
        updateStats("upload", true, data.length());
        return true;
    }
    else
    {
        LOG_ERROR("[CellularManager] Failed to send data: " + response.statusText);
        updateStats("upload", false);
        return false;
    }
}

// Get network time
std::string CellularManager::getNetworkTime()
{
    if (!initialized_)
    {
        return "";
    }

    std::lock_guard<std::mutex> lock(operationMutex_);

    // Request network time
    if (!sendATCommand("+CCLK?", "OK"))
    {
        LOG_ERROR("[CellularManager] Failed to get network time");
        return "";
    }

    std::string response = readSerialResponse(2000);

    // Parse time: +CCLK: "YY/MM/DD,HH:MM:SS+ZZ"
    std::regex timeRegex(R"(\+CCLK:\s*"([^"]+)") ");
        std::smatch match;

    if (std::regex_search(response, match, timeRegex))
    {
        std::string timeStr = match[1];
        LOG_DEBUG("[CellularManager] Network time: " + timeStr);
        return timeStr;
    }

    return "";
}

// Get SIM information
SIMInfo CellularManager::getSIMInfo()
{
    if (initialized_)
    {
        updateSIMInfo();
    }
    return simInfo_;
}

// Get signal information
SignalInfo CellularManager::getSignalInfo()
{
    if (initialized_)
    {
        updateSignalInfo();
    }
    return signalInfo_;
}

// Get connection statistics
ConnectionStats CellularManager::getConnectionStats() const
{
    std::lock_guard<std::mutex> lock(statsMutex_);
    return stats_;
}

// Setup HTTP service
bool CellularManager::setupHTTPService(const std::string &url, const std::string &contentType)
{
    LOG_INFO("[CellularManager] Setting up HTTP service");

    // Terminate any existing session
    terminateHTTPService();

    // Initialize HTTP
    if (!sendATCommand("+HTTPINIT", "OK"))
    {
        LOG_ERROR("[CellularManager] Failed to initialize HTTP");
        return false;
    }

    // Set HTTP parameters
    std::string urlCmd = "+HTTPPARA=\"URL\",\"" + url + "\"";
    if (!sendATCommand(urlCmd, "OK"))
    {
        LOG_ERROR("[CellularManager] Failed to set URL");
        terminateHTTPService();
        return false;
    }

    // Set content type
    std::string contentCmd = "+HTTPPARA=\"CONTENT\",\"" + contentType + "\"";
    if (!sendATCommand(contentCmd, "OK"))
    {
        LOG_ERROR("[CellularManager] Failed to set content type");
        terminateHTTPService();
        return false;
    }

    // Set user agent
    std::string userAgent = "ForestryDevice/" + std::string(Config::DEVICE_ID);
    std::string uaCmd = "+HTTPPARA=\"USERDATA\",\"" + userAgent + "\"";
    sendATCommand(uaCmd, "OK"); // Optional parameter

    httpServiceActive_ = true;
    httpEndpoint_ = url;

    LOG_INFO("[CellularManager] HTTP service ready");
    return true;
}

// Terminate HTTP service
bool CellularManager::terminateHTTPService()
{
    if (!httpServiceActive_)
    {
        return true;
    }

    LOG_DEBUG("[CellularManager] Terminating HTTP service");

    sendATCommand("+HTTPTERM", "OK");
    httpServiceActive_ = false;

    return true;
}

// Send HTTP request
HTTPResponse CellularManager::sendHTTPRequest(const std::string &data, bool moreData)
{
    HTTPResponse response;
    auto startTime = std::chrono::steady_clock::now();

    if (!httpServiceActive_)
    {
        response.statusText = "HTTP service not active";
        return response;
    }

    // Set data length and timeout
    std::string dataCmd = "+HTTPDATA=" + std::to_string(data.length()) + ",10000";
    if (!sendATCommand(dataCmd, "DOWNLOAD"))
    {
        response.statusText = "Failed to initiate data transfer";
        return response;
    }

    // Send actual data
    clearSerialBuffer();
#ifdef ESP32_PLATFORM
    int written = uart_write_bytes(UART_NUM_1, data.c_str(), data.length());
    if (written != static_cast<int>(data.length()))
    {
        response.statusText = "Failed to write all data";
        return response;
    }
#endif

    // Wait for data acceptance
    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    // Execute HTTP POST
    if (!sendATCommandPost("+HTTPACTION=1", "+HTTPACTION:"))
    {
        response.statusText = "HTTP POST failed";
        return response;
    }

    // Parse HTTP action response
    std::string actionResponse = readSerialResponse(5000);
    std::regex statusRegex(R"(\+HTTPACTION:\s*\d+,(\d+),(\d+))");
    std::smatch match;

    if (std::regex_search(actionResponse, match, statusRegex))
    {
        response.statusCode = std::stoi(match[1]);
        response.contentLength = std::stoi(match[2]);

        // Read response data if available
        if (response.contentLength > 0)
        {
            std::string readCmd = "+HTTPREAD";
            if (sendATCommand(readCmd, "+HTTPREAD:"))
            {
                response.body = readSerialResponse(5000);
            }
        }

        response.success = (response.statusCode >= 200 && response.statusCode < 300);
        response.statusText = (response.success ? "Success" : "HTTP Error " + std::to_string(response.statusCode));
    }
    else
    {
        response.statusText = "Failed to parse HTTP response";
    }

    response.responseTime = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - startTime);

    // Update statistics
    stats_.httpRequestsSent++;
    if (response.success)
    {
        stats_.httpRequestsSuccessful++;
        stats_.bytesTransmitted += data.length();
        if (response.contentLength > 0)
        {
            stats_.bytesReceived += response.contentLength;
        }
    }

    return response;
}

// Send data in chunks
bool CellularManager::sendDataInChunks(const std::string &filename,
                                       std::shared_ptr<IDataStorage> storage,
                                       size_t chunkSize)
{
    if (!connected_ || !storage)
    {
        LOG_ERROR("[CellularManager] Not connected or invalid storage");
        return false;
    }

    LOG_INFO("[CellularManager] Sending file in chunks: " + filename);

    // Read file data
    std::vector<std::string> lines = storage->readFile(filename);
    if (lines.empty())
    {
        LOG_ERROR("[CellularManager] File is empty or cannot be read");
        return false;
    }

    // Build data from lines
    std::string fullData;
    for (const auto &line : lines)
    {
        fullData += line + "\n";
    }

    // Send in chunks
    size_t totalSize = fullData.length();
    size_t sent = 0;
    int chunkNum = 0;

    while (sent < totalSize)
    {
        size_t currentChunkSize = std::min(chunkSize, totalSize - sent);
        std::string chunk = fullData.substr(sent, currentChunkSize);

        chunkNum++;
        LOG_DEBUG("[CellularManager] Sending chunk " + std::to_string(chunkNum) +
                  " (" + std::to_string(currentChunkSize) + " bytes)");

        bool moreData = (sent + currentChunkSize < totalSize);
        HTTPResponse response = sendHTTPRequest(chunk, moreData);

        if (!response.success)
        {
            LOG_ERROR("[CellularManager] Failed to send chunk " +
                      std::to_string(chunkNum));
            return false;
        }

        sent += currentChunkSize;
    }

    LOG_INFO("[CellularManager] File sent successfully in " +
             std::to_string(chunkNum) + " chunks");
    return true;
}

// Enable NITZ
bool CellularManager::enableNITZ()
{
    LOG_INFO("[CellularManager] Enabling Network Identity and Time Zone");
    return sendATCommand("+CTZU=1", "OK");
}

// Enable CTZR
bool CellularManager::enableCTZR()
{
    LOG_INFO("[CellularManager] Enabling Clock Time Zone Reporting");
    return sendATCommand("+CTZR=1", "OK");
}

// Validate network time
bool CellularManager::isValidNetworkTime(const std::string &timeString)
{
    // Expected format: "YY/MM/DD,HH:MM:SS+ZZ"
    std::regex timeRegex(R"(^\d{2}/\d{2}/\d{2},\d{2}:\d{2}:\d{2}[+-]\d{2}$)");
    return std::regex_match(timeString, timeRegex);
}

// Test network connectivity
bool CellularManager::testNetworkConnectivity(const std::string &testUrl)
{
    if (!connected_)
    {
        return false;
    }

    LOG_DEBUG("[CellularManager] Testing network connectivity");

    // Use ping test
    std::string pingCmd = "+CIPPING=\"8.8.8.8\"";
    if (sendATCommand(pingCmd, "+CIPPING:"))
    {
        std::string response = readSerialResponse(5000);
        return (response.find("ms") != std::string::npos);
    }

    return false;
}

// Check SIM ready
bool CellularManager::checkSIMReady()
{
    LOG_DEBUG("[CellularManager] Checking SIM status");

    if (!sendATCommand("+CPIN?", "READY"))
    {
        std::string response = readSerialResponse(1000);

        if (response.find("SIM PIN") != std::string::npos)
        {
            lastError_ = "SIM PIN required";
            simInfo_.pinRequired = true;
        }
        else if (response.find("SIM PUK") != std::string::npos)
        {
            lastError_ = "SIM PUK required";
        }
        else
        {
            lastError_ = "SIM not ready: " + response;
        }

        LOG_ERROR("[CellularManager] " + lastError_);
        simInfo_.ready = false;
        return false;
    }

    simInfo_.ready = true;
    simInfo_.pinRequired = false;
    return true;
}

// Monitor network status
bool CellularManager::monitorNetworkStatus(uint32_t timeoutMs)
{
    LOG_INFO("[CellularManager] Monitoring network status on NET pin");

#ifdef ESP32_PLATFORM
    auto startTime = std::chrono::steady_clock::now();
    int flashCount = 0;
    bool lastState = false;

    while (std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now() - startTime)
               .count() < timeoutMs)
    {
        bool currentState = gpio_get_level(static_cast<gpio_num_t>(netPin_));

        if (currentState != lastState)
        {
            flashCount++;
            lastState = currentState;

            if (flashCount >= NET_PIN_TEST_FLASHES * 2)
            {
                LOG_INFO("[CellularManager] Network signal detected (LED flashing)");
                return true;
            }
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    LOG_WARNING("[CellularManager] No network signal detected on NET pin");
    return false;
#else
    // Development platform
    return true;
#endif
}

// Configure APN
bool CellularManager::configureAPN(const std::string &apn,
                                   const std::string &username,
                                   const std::string &password)
{
    LOG_INFO("[CellularManager] Configuring APN: " + apn);

    apnName_ = apn;
    apnUsername_ = username;
    apnPassword_ = password;

    // Set APN
    std::string apnCmd = "+CGDCONT=1,\"IP\",\"" + apn + "\"";
    if (!sendATCommand(apnCmd, "OK"))
    {
        LOG_ERROR("[CellularManager] Failed to set APN");
        return false;
    }

    // Set authentication if provided
    if (!username.empty() || !password.empty())
    {
        std::string authCmd = "+CGAUTH=1,1,\"" + username + "\",\"" + password + "\"";
        sendATCommand(authCmd, "OK"); // Optional, may not be supported
    }

    return true;
}

// Perform health check
bool CellularManager::performHealthCheck()
{
    LOG_INFO("[CellularManager] Performing module health check");

    bool healthy = true;

    // Check basic AT response
    if (!sendATCommand("", "OK", 1, 1000))
    {
        LOG_ERROR("[CellularManager] No AT response");
        healthy = false;
    }

    // Check SIM status
    if (!checkSIMReady())
    {
        LOG_ERROR("[CellularManager] SIM not ready");
        healthy = false;
    }

    // Check signal quality
    updateSignalInfo();
    if (signalInfo_.rssi < -110 || signalInfo_.rssi == -999)
    {
        LOG_WARNING("[CellularManager] Poor or no signal");
        healthy = false;
    }

    if (healthy)
    {
        LOG_INFO("[CellularManager] Health check passed");
    }
    else
    {
        LOG_WARNING("[CellularManager] Health check failed");
    }

    return healthy;
}

// Reset module
bool CellularManager::resetModule(bool hardReset)
{
    LOG_WARNING("[CellularManager] Resetting module (" +
                std::string(hardReset ? "hard" : "soft") + " reset)");

    if (hardReset)
    {
        // Hardware reset via RST pin
#ifdef ESP32_PLATFORM
        gpio_set_level(static_cast<gpio_num_t>(Config::Pins::CELL_RST), 0);
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        gpio_set_level(static_cast<gpio_num_t>(Config::Pins::CELL_RST), 1);
#endif
        std::this_thread::sleep_for(std::chrono::seconds(5));
    }
    else
    {
        // Software reset
        sendATCommand("+CFUN=1,1", "OK");
        std::this_thread::sleep_for(std::chrono::seconds(10));
    }

    // Reinitialize
    initialized_ = false;
    connected_ = false;
    httpServiceActive_ = false;

    return initialize(baudRate_);
}

// Create JSON payload
std::string CellularManager::createJSONPayload(const AssetInfo &asset,
                                               const std::vector<std::string> &dataLines)
{
    std::ostringstream json;
    json << "{\n";

    // Asset information
    json << "  \"assetId\": \"" << escapeJSONString(asset.assetId) << "\",\n";
    json << "  \"locationName\": \"" << escapeJSONString(asset.locationName) << "\",\n";
    json << "  \"forestName\": \"" << escapeJSONString(asset.forestName) << "\",\n";
    json << "  \"latitude\": " << asset.latitude << ",\n";
    json << "  \"longitude\": " << asset.longitude << ",\n";
    json << "  \"batteryCapacity\": " << asset.remainingBatteryCapacity << ",\n";
    json << "  \"stateOfCharge\": " << asset.stateOfCharge << ",\n";
    json << "  \"runtimeToEmpty\": " << asset.runtimeToEmpty << ",\n";
    json << "  \"batteryVoltage\": " << asset.batteryVoltage << ",\n";
    json << "  \"batteryCurrent\": " << asset.batteryCurrent << ",\n";
    json << "  \"cellTemperature\": " << asset.cellTemperature << ",\n";
    json << "  \"pcbTemperature\": " << asset.pcbTemperature << ",\n";
    json << "  \"sdCardCapacity\": " << asset.sdCardCapacity << ",\n";
    json << "  \"timestamp\": \"" << escapeJSONString(asset.timeStamp) << "\",\n";

    // Scan data
    json << "  \"scanData\": [\n";

    for (size_t i = 0; i < dataLines.size(); ++i)
    {
        json << "    \"" << escapeJSONString(dataLines[i]) << "\"";
        if (i < dataLines.size() - 1)
        {
            json << ",";
        }
        json << "\n";
    }

    json << "  ]\n";
    json << "}";

    return json.str();
}

// Escape JSON string
std::string CellularManager::escapeJSONString(const std::string &input)
{
    std::string output;
    output.reserve(input.length() * 2);

    for (char c : input)
    {
        switch (c)
        {
        case '"':
            output += "\\\"";
            break;
        case '\\':
            output += "\\\\";
            break;
        case '\b':
            output += "\\b";
            break;
        case '\f':
            output += "\\f";
            break;
        case '\n':
            output += "\\n";
            break;
        case '\r':
            output += "\\r";
            break;
        case '\t':
            output += "\\t";
            break;
        default:
            if (c >= 0x20 && c <= 0x7E)
            {
                output += c;
            }
            else
            {
                // Unicode escape for non-printable
                char buf[7];
                snprintf(buf, sizeof(buf), "\\u%04x", static_cast<unsigned char>(c));
                output += buf;
            }
            break;
        }
    }

    return output;
}

// Private implementation methods

// Send AT command
bool CellularManager::sendATCommand(const std::string &command,
                                    const std::string &expectedResponse,
                                    int maxAttempts,
                                    uint32_t timeoutMs)
{
    if (!serialHandle_)
    {
        return false;
    }

    if (timeoutMs == 0)
    {
        timeoutMs = commandTimeoutMs_;
    }

    std::string fullCommand = "AT" + command;

    for (int attempt = 0; attempt < maxAttempts; ++attempt)
    {
        if (attempt > 0)
        {
            LOG_DEBUG("[CellularManager] Retry attempt " + std::to_string(attempt + 1));
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
        }

        LOG_DEBUG("[CellularManager] TX: " + fullCommand);

        clearSerialBuffer();

#ifdef ESP32_PLATFORM
        std::string cmdWithNewline = fullCommand + "\r\n";
        uart_write_bytes(UART_NUM_1, cmdWithNewline.c_str(), cmdWithNewline.length());
#else
        // Development platform simulation
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
#endif

        std::string response = readSerialResponse(timeoutMs);
        LOG_DEBUG("[CellularManager] RX: " + response);

        if (response.find(expectedResponse) != std::string::npos)
        {
            return true;
        }

        if (response.find("ERROR") != std::string::npos)
        {
            lastError_ = "AT command error: " + response;
            LOG_ERROR("[CellularManager] " + lastError_);
            return false;
        }
    }

    return false;
}

// Send AT command for POST operations
bool CellularManager::sendATCommandPost(const std::string &command,
                                        const std::string &expectedResponse,
                                        int maxAttempts,
                                        uint32_t timeoutMs)
{
    // POST commands need longer timeout
    return sendATCommand(command, expectedResponse, maxAttempts,
                         timeoutMs == 0 ? DEFAULT_HTTP_TIMEOUT : timeoutMs);
}

// Read serial response
std::string CellularManager::readSerialResponse(uint32_t timeoutMs)
{
    std::string response;

#ifdef ESP32_PLATFORM
    auto startTime = std::chrono::steady_clock::now();
    uint8_t data[256];

    while (std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now() - startTime)
               .count() < timeoutMs)
    {
        int len = uart_read_bytes(UART_NUM_1, data, sizeof(data), 10 / portTICK_PERIOD_MS);
        if (len > 0)
        {
            response.append(reinterpret_cast<char*>(data), len);
        }

        // Check for complete response
        if (response.find("OK\r\n") != std::string::npos ||
            response.find("ERROR") != std::string::npos ||
            response.find("DOWNLOAD") != std::string::npos ||
            response.find("+HTTPACTION:") != std::string::npos)
        {
            break;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
#else
    // Development platform - return simulated responses
    if (timeoutMs > 0)
    {
        response = "OK\r\n";
    }
#endif

    return response;
}

// Clear serial buffer
void CellularManager::clearSerialBuffer()
{
#ifdef ESP32_PLATFORM
    uart_flush_input(UART_NUM_1);
#endif
}

// Activate PDP context
bool CellularManager::activatePDPContext()
{
    LOG_DEBUG("[CellularManager] Activating PDP context");

    // Check current state
    if (sendATCommand("+CGACT?", "+CGACT: 1,1"))
    {
        LOG_DEBUG("[CellularManager] PDP context already active");
        return true;
    }

    // Activate context
    if (!sendATCommand("+CGACT=1,1", "OK", 3, 10000))
    {
        LOG_ERROR("[CellularManager] Failed to activate PDP context");
        return false;
    }

    LOG_INFO("[CellularManager] PDP context activated");
    return true;
}

// Deactivate PDP context
bool CellularManager::deactivatePDPContext()
{
    LOG_DEBUG("[CellularManager] Deactivating PDP context");
    return sendATCommand("+CGACT=0,1", "OK");
}

// Check network registration
bool CellularManager::checkNetworkRegistration()
{
    LOG_INFO("[CellularManager] Checking network registration");

    int attempts = 0;
    const int maxAttempts = 60; // 60 seconds maximum

    while (attempts < maxAttempts)
    {
        if (sendATCommand("+CREG?", "OK"))
        {
            std::string response = readSerialResponse(2000);

            // Parse registration status
            std::regex regexPattern(R"(\+CREG:\s*\d+,(\d+))");
            std::smatch match;

            if (std::regex_search(response, match, regexPattern))
            {
                int status = std::stoi(match[1]);

                if (status == 1 || status == 5) // 1=home, 5=roaming
                {
                    LOG_INFO("[CellularManager] Registered on network" +
                             std::string(status == 5 ? " (roaming)" : ""));
                    signalInfo_.roaming = (status == 5);
                    return true;
                }
                else if (status == 2)
                {
                    LOG_DEBUG("[CellularManager] Still searching for network...");
                }
                else if (status == 3)
                {
                    LOG_ERROR("[CellularManager] Registration denied");
                    return false;
                }
            }
        }

        std::this_thread::sleep_for(std::chrono::seconds(1));
        attempts++;
    }

    LOG_ERROR("[CellularManager] Network registration timeout");
    return false;
}

// Update statistics
void CellularManager::updateStats(const std::string &operation, bool success, size_t bytes)
{
    std::lock_guard<std::mutex> lock(statsMutex_);

    if (operation == "connect")
    {
        if (!success)
        {
            stats_.lastError = lastError_;
        }
    }
    else if (operation == "upload")
    {
        if (success)
        {
            stats_.bytesTransmitted += bytes;
        }
        else
        {
            stats_.lastError = lastError_;
        }
    }
}

// Update signal information
void CellularManager::updateSignalInfo()
{
    // Get signal quality
    if (sendATCommand("+CSQ", "OK"))
    {
        std::string response = readSerialResponse(2000);
        std::regex csqRegex(R"(\+CSQ:\s*(\d+),(\d+))");
        std::smatch match;

        if (std::regex_search(response, match, csqRegex))
        {
            int rssi = std::stoi(match[1]);
            int ber = std::stoi(match[2]);

            if (rssi != 99)
            {
                signalInfo_.rssi = -113 + (rssi * 2);
            }
            signalInfo_.ber = ber;
        }
    }

    // Get extended signal info for LTE
    if (sendATCommand("+CPSI?", "OK"))
    {
        std::string response = readSerialResponse(2000);

        if (response.find("LTE") != std::string::npos)
        {
            signalInfo_.networkType = "LTE";

            // Parse RSRP, RSRQ, SINR if available
            std::regex lteRegex(R"(RSRP:(-?\d+).*RSRQ:(-?\d+).*SINR:(-?\d+))");
            std::smatch match;

            if (std::regex_search(response, match, lteRegex))
            {
                signalInfo_.rsrp = std::stoi(match[1]);
                signalInfo_.rsrq = std::stoi(match[2]);
                signalInfo_.sinr = std::stoi(match[3]);
            }
        }
        else if (response.find("WCDMA") != std::string::npos)
        {
            signalInfo_.networkType = "3G";
        }
        else if (response.find("GSM") != std::string::npos)
        {
            signalInfo_.networkType = "2G";
        }
    }

    // Get operator name
    if (sendATCommand("+COPS?", "OK"))
    {
        std::string response = readSerialResponse(2000);
        std::regex opsRegex(R"(\+COPS:\s*\d+,\d+,"([^"]+)") ");
            std::smatch match;

        if (std::regex_search(response, match, opsRegex))
        {
            signalInfo_.operatorName = match[1];
        }
    }
}

// Update SIM information
void CellularManager::updateSIMInfo()
{
    // Get IMEI
    if (sendATCommand("+CGSN", "OK"))
    {
        std::string response = readSerialResponse(2000);
        std::regex imeiRegex(R"((\d{15}))");
        std::smatch match;

        if (std::regex_search(response, match, imeiRegex))
        {
            simInfo_.imei = match[0];
        }
    }

    // Get IMSI
    if (sendATCommand("+CIMI", "OK"))
    {
        std::string response = readSerialResponse(2000);
        std::regex imsiRegex(R"((\d{15}))");
        std::smatch match;

        if (std::regex_search(response, match, imsiRegex))
        {
            simInfo_.imsi = match[0];
        }
    }

    // Get ICCID
    if (sendATCommand("+CCID", "OK"))
    {
        std::string response = readSerialResponse(2000);
        std::regex iccidRegex(R"((\d{19,20}))");
        std::smatch match;

        if (std::regex_search(response, match, iccidRegex))
        {
            simInfo_.iccid = match[0];
        }
    }

    // Get operator from SIM
    if (sendATCommand("+CSPN?", "OK"))
    {
        std::string response = readSerialResponse(2000);
        std::regex spnRegex(R"(\+CSPN:\s*"([^"]+)") ");
            std::smatch match;

        if (std::regex_search(response, match, spnRegex))
        {
            simInfo_.operatorName = match[1];
        }
    }
}

// Platform-specific serial initialization
bool CellularManager::platformInitializeSerial()
{
#ifdef ESP32_PLATFORM
    // Configure UART parameters
    uart_config_t uart_config = {
        .baud_rate = static_cast<int>(baudRate_),
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .rx_flow_ctrl_thresh = 0,
        .source_clk = UART_SCLK_DEFAULT,
    };

    // Configure UART driver
    esp_err_t ret = uart_driver_install(UART_NUM_1, SERIAL_BUFFER_SIZE * 2, 
                                        SERIAL_BUFFER_SIZE * 2, 0, NULL, 0);
    if (ret != ESP_OK) {
        LOG_ERROR("[CellularManager] Failed to install UART driver");
        return false;
    }

    ret = uart_param_config(UART_NUM_1, &uart_config);
    if (ret != ESP_OK) {
        LOG_ERROR("[CellularManager] Failed to configure UART parameters");
        uart_driver_delete(UART_NUM_1);
        return false;
    }

    ret = uart_set_pin(UART_NUM_1, txPin_, rxPin_, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    if (ret != ESP_OK) {
        LOG_ERROR("[CellularManager] Failed to set UART pins");
        uart_driver_delete(UART_NUM_1);
        return false;
    }

    // Set read timeout
    uart_set_rx_timeout(UART_NUM_1, 10); // 10 TOUT (unit: time of sending one symbol)

    serialHandle_ = (void*)1; // Non-null to indicate initialized

    LOG_DEBUG("[CellularManager] Serial initialized on RX:" +
              std::to_string(rxPin_) + ", TX:" + std::to_string(txPin_) +
              " at " + std::to_string(baudRate_) + " baud");
    return true;
#elif defined(RASPBERRY_PI_PLATFORM)
    // Raspberry Pi: Open USB serial device for SIM7600
    // Typical USB serial devices: /dev/ttyUSB0, /dev/ttyUSB1, etc.
    const char* device = "/dev/ttyUSB0"; // TODO: Make configurable
    
    int fd = open(device, O_RDWR | O_NOCTTY | O_NDELAY);
    if (fd == -1) {
        LOG_ERROR("[CellularManager] Failed to open USB serial device: " + std::string(device));
        return false;
    }
    
    // Configure serial port
    struct termios options;
    tcgetattr(fd, &options);
    
    // Set baud rate
    cfsetispeed(&options, B115200); // TODO: Use baudRate_ parameter
    cfsetospeed(&options, B115200);
    
    // Set 8N1
    options.c_cflag &= ~PARENB;
    options.c_cflag &= ~CSTOPB;
    options.c_cflag &= ~CSIZE;
    options.c_cflag |= CS8;
    
    // Enable receiver and local mode
    options.c_cflag |= (CLOCAL | CREAD);
    
    // Apply settings
    tcsetattr(fd, TCSANOW, &options);
    
    serialHandle_ = reinterpret_cast<void*>(fd);
    LOG_DEBUG("[CellularManager] Serial initialized on " + std::string(device) +
              " at " + std::to_string(baudRate_) + " baud");
    return true;
#else
    serialHandle_ = (void *)1; // Dummy handle
    LOG_DEBUG("[CellularManager] Development platform - serial initialized (simulated)");
    return true;
#endif
}

// Platform cleanup serial
void CellularManager::platformCleanupSerial()
{
#ifdef ESP32_PLATFORM
    if (serialHandle_)
    {
        uart_driver_delete(UART_NUM_1);
        serialHandle_ = nullptr;
    }
#else
    serialHandle_ = nullptr;
#endif
}

// Platform setup GPIO
bool CellularManager::platformSetupGPIO()
{
#ifdef ESP32_PLATFORM
    // Configure NET pin as input
    gpio_config_t io_conf = {};
    io_conf.intr_type = GPIO_INTR_DISABLE;
    io_conf.mode = GPIO_MODE_INPUT;
    io_conf.pin_bit_mask = (1ULL << netPin_);
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io_conf.pull_up_en = GPIO_PULLUP_DISABLE;

    esp_err_t ret = gpio_config(&io_conf);
    if (ret != ESP_OK)
    {
        LOG_ERROR("[CellularManager] Failed to configure NET pin GPIO");
        return false;
    }

    LOG_DEBUG("[CellularManager] NET pin GPIO" + std::to_string(netPin_) + " configured");
    return true;
#else
    return true;
#endif
}

// Count lines in file
int CellularManager::countLinesInFile(const std::string &filename,
                                      std::shared_ptr<IDataStorage> storage)
{
    if (!storage)
    {
        return -1;
    }

    std::vector<std::string> lines = storage->readFile(filename);
    return static_cast<int>(lines.size());
}