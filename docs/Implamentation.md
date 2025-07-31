/ Implementation Details

// SDCardManager Implementation
SDCardManager::SDCardManager() 
    : basePath_("/sdcard")
    , initialized_(false) {
}

SDCardManager::~SDCardManager() {
    if (initialized_) {
        unmountSDCard();
    }
}

bool SDCardManager::initialize() {
    printf("Initializing SD Card...\n");
    
    if (!mountSDCard()) {
        printf("Failed to mount SD card\n");
        return false;
    }
    
    // Create necessary directories
    ensureDirectoryExists(basePath_ + "/data");
    ensureDirectoryExists(basePath_ + "/logs");
    ensureDirectoryExists(basePath_ + "/assert");
    
    initialized_ = true;
    printf("SD Card initialized successfully\n");
    return true;
}

bool SDCardManager::writeData(const ProbeRequest& data, const std::string& filename) {
    if (!initialized_) {
        return false;
    }
    
    std::string fullPath = basePath_ + "/" + sanitizeFilename(filename);
    
    try {
        std::ofstream file(fullPath, std::ios::app);
        if (!file.is_open()) {
            printf("Failed to open file: %s\n", fullPath.c_str());
            return false;
        }
        
        // Write CSV format
        file << data.dataType << ","
             << data.timestamp << ","
             << data.source << ","
             << data.rssi << ","
             << data.packetLength << ","
             << data.macAddress << ","
             << data.payload << "\n";
        
        file.close();
        return true;
        
    } catch (const std::exception& e) {
        printf("Error writing to SD card: %s\n", e.what());
        return false;
    }
}

bool SDCardManager::writeAssetData(const AssetInfo& data) {
    if (!initialized_) {
        return false;
    }
    
    std::string filename = basePath_ + "/assert/assert_" + data.timeStamp + ".csv";
    filename = sanitizeFilename(filename);
    
    try {
        std::ofstream file(filename);
        if (!file.is_open()) {
            return false;
        }
        
        // Write header
        file << "assetId,locationName,forestName,latitude,longitude,"
             << "remainingBatteryCapacity,stateOfCharge,runtimeToEmpty,"
             << "batteryVoltage,batteryCurrent,SDCardCapacity,timeStamp\n";
        
        // Write data
        file << data.assetId << ","
             << data.locationName << ","
             << data.forestName << ","
             << data.latitude << ","
             << data.longitude << ","
             << data.remainingBatteryCapacity << ","
             << data.stateOfCharge << ","
             << data.runtimeToEmpty << ","
             << data.batteryVoltage << ","
             << data.batteryCurrent << ","
             << data.sdCardCapacity << ","
             << data.timeStamp << "\n";
        
        file.close();
        return true;
        
    } catch (const std::exception& e) {
        printf("Error writing asset data: %s\n", e.what());
        return false;
    }
}

std::vector<std::string> SDCardManager::readFile(const std::string& filename) {
    std::vector<std::string> lines;
    
    if (!initialized_) {
        return lines;
    }
    
    std::string fullPath = basePath_ + "/" + sanitizeFilename(filename);
    
    try {
        std::ifstream file(fullPath);
        if (!file.is_open()) {
            return lines;
        }
        
        std::string line;
        while (std::getline(file, line)) {
            lines.push_back(line);
        }
        
        file.close();
        
    } catch (const std::exception& e) {
        printf("Error reading file: %s\n", e.what());
    }
    
    return lines;
}

bool SDCardManager::fileExists(const std::string& filename) {
    std::string fullPath = basePath_ + "/" + sanitizeFilename(filename);
    return std::filesystem::exists(fullPath);
}

size_t SDCardManager::getFileSize(const std::string& filename) {
    std::string fullPath = basePath_ + "/" + sanitizeFilename(filename);
    
    try {
        return std::filesystem::file_size(fullPath);
    } catch (const std::exception& e) {
        return 0;
    }
}

float SDCardManager::getRemainingCapacityPercent() {
    try {
        auto space = std::filesystem::space(basePath_);
        float used = static_cast<float>(space.capacity - space.available);
        float total = static_cast<float>(space.capacity);
        return 100.0f * (1.0f - (used / total));
    } catch (const std::exception& e) {
        return 0.0f;
    }
}

// CellularManager Implementation
CellularManager::CellularManager()
    : connected_(false)
    , serialHandle_(nullptr)
    , apiUrl_("https://uk-610246-forestryresearchapi-app-dev-01.azurewebsites.net/api/DeviceData") {
}

CellularManager::~CellularManager() {
    disconnect();
}

bool CellularManager::connect() {
    printf("Connecting to cellular network...\n");
    
    try {
        // Initialize serial communication
        #ifdef ESP32_PLATFORM
        // Serial2.begin(115200, SERIAL_8N1, RX_PIN, TX_PIN);
        // Serial2.setRxBufferSize(1024);
        #endif
        
        // Test basic AT communication
        if (!sendATCommand("AT", "OK", 10)) {
            printf("Failed basic AT communication\n");
            return false;
        }
        
        // Check SIM status
        if (!sendATCommand("AT+CPIN?", "+CPIN: READY", 10)) {
            printf("SIM not ready\n");
            return false;
        }
        
        // Check network registration
        if (!sendATCommand("AT+CREG?", "+CREG: 0,1", 10)) {
            printf("Not registered to network\n");
            return false;
        }
        
        // Configure APN
        if (!configureAPN()) {
            printf("Failed to configure APN\n");
            return false;
        }
        
        // Activate PDP context
        if (!activatePDPContext()) {
            printf("Failed to activate PDP context\n");
            return false;
        }
        
        connected_ = true;
        printf("Cellular connection established\n");
        return true;
        
    } catch (const std::exception& e) {
        printf("Cellular connection failed: %s\n", e.what());
        return false;
    }
}

bool CellularManager::sendData(const std::string& data) {
    if (!connected_) {
        printf("Not connected to cellular network\n");
        return false;
    }
    
    return sendHTTPData(data);
}

std::string CellularManager::getNetworkTime() {
    if (!sendATCommand("AT", "OK", 10)) {
        return "Error: Unable to fetch network time";
    }
    
    std::string response;
    
    #ifdef ESP32_PLATFORM
    // Send CCLK command and parse response
    // Serial2.println("AT+CCLK?");
    // 
    // auto startTime = std::chrono::steady_clock::now();
    // const auto timeout = std::chrono::seconds(5);
    // 
    // while (std::chrono::steady_clock::now() - startTime < timeout) {
    //     if (Serial2.available()) {
    //         char c = Serial2.read();
    //         response += c;
    //         
    //         if (response.find("+CCLK:") != std::string::npos &&
    //             response.find("OK") != std::string::npos) {
    //             break;
    //         }
    //     }
    // }
    #endif
    
    // Parse time from response
    size_t timeStart = response.find("+CCLK: \"") + 8;
    size_t timeEnd = response.find("\"", timeStart);
    
    if (timeStart != std::string::npos && timeEnd != std::string::npos) {
        return response.substr(timeStart, timeEnd - timeStart);
    }
    
    return "Error: Unable to fetch network time";
}

bool CellularManager::sendATCommand(const std::string& command, 
                                   const std::string& expectedResponse, 
                                   int maxAttempts, int timeoutMs) {
    for (int attempt = 0; attempt < maxAttempts; ++attempt) {
        #ifdef ESP32_PLATFORM
        // Serial2.println(command.c_str());
        #endif
        
        auto startTime = std::chrono::steady_clock::now();
        auto timeout = std::chrono::milliseconds(timeoutMs);
        std::string response;
        
        while (std::chrono::steady_clock::now() - startTime < timeout) {
            #ifdef ESP32_PLATFORM
            // if (Serial2.available()) {
            //     char c = Serial2.read();
            //     response += c;
            // }
            #endif
            
            if (response.find(expectedResponse) != std::string::npos) {
                printf("AT Response: %s\n", response.c_str());
                return true;
            }
        }
        
        printf("AT Command failed (attempt %d): %s\n", attempt + 1, response.c_str());
    }
    
    return false;
}

// RTCTimeManager Implementation
RTCTimeManager::RTCTimeManager()
    : rtcHandle_(nullptr)
    , initialized_(false) {
}

std::string RTCTimeManager::getCurrentDateTime() {
    if (!initialized_) {
        return "";
    }
    
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    auto tm = *std::gmtime(&time_t);
    
    char buffer[30];
    std::strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%SZ", &tm);
    
    return std::string(buffer);
}

bool RTCTimeManager::setTimeFromNetwork(const std::string& networkTime) {
    printf("Setting RTC from network time: %s\n", networkTime.c_str());
    
    if (!isValidTime(networkTime)) {
        printf("Invalid network time format\n");
        return false;
    }
    
    try {
        TimeComponents time = parseNetworkTime(networkTime);
        
        #ifdef ESP32_PLATFORM
        // Platform-specific RTC setting code would go here
        // DS1307.setTime(timeArray);
        #endif
        
        printf("RTC time updated successfully\n");
        return true;
        
    } catch (const std::exception& e) {
        printf("Failed to set RTC time: %s\n", e.what());
        return false;
    }
}

bool RTCTimeManager::isValidTime(const std::string& timeStr) {
    // Check format: "YY/MM/DD,HH:MM:SS+ZZ"
    if (timeStr.length() != 20) {
        return false;
    }
    
    // Check delimiters
    return (timeStr[2] == '/' && timeStr[5] == '/' && timeStr[8] == ',' &&
            timeStr[11] == ':' && timeStr[14] == ':' && timeStr[17] == '+');
}

uint64_t RTCTimeManager::getNightSleepDuration(const std::string& currentTime) {
    if (!isValidTime(currentTime)) {
        return 0;
    }
    
    TimeComponents time = parseNetworkTime(currentTime);
    
    // Check if it's night time (22:00 - 06:00)
    if (time.hour >= 22 || time.hour <= 6) {
        return calculateSleepDuration(time.hour, time.minute);
    }
    
    return 0; // Not night time
}

// PowerManager Implementation  
PowerManager::PowerManager()
    : initialized_(false)
    , lastWakeupCause_(WakeupCause::UNKNOWN) {
}

bool PowerManager::initialize() {
    printf("Initializing Power Management\n");
    
    // Configure watchdog
    configureWatchdog(300000); // 5 minutes
    
    // Analyze wakeup cause
    lastWakeupCause_ = getWakeupCause();
    printf("Wakeup cause: %s\n", getWakeupCauseString().c_str());
    printf("Reset reason: %s\n", getResetReasonString().c_str());
    
    initialized_ = true;
    return true;
}

void PowerManager::enablePeripherals() {
    printf("Enabling 5V peripheral power\n");
    enable5V();
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
}

void PowerManager::disablePeripherals() {
    printf("Disabling 5V peripheral power\n");
    disable5V();
}

void PowerManager::enterSleep(uint64_t sleepTimeUs) {
    printf("Entering deep sleep for %llu seconds\n", sleepTimeUs / 1000000);
    
    disablePeripherals();
    configureWakeup(sleepTimeUs);
    
    #ifdef ESP32_PLATFORM
    // esp_deep_sleep_start();
    #endif
}

WakeupCause PowerManager::getWakeupCause() {
    #ifdef ESP32_PLATFORM
    // esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();
    // switch (cause) {
    //     case ESP_SLEEP_WAKEUP_TIMER:
    //         return WakeupCause::TIMER;
    //     case ESP_SLEEP_WAKEUP_EXT0:
    //     case ESP_SLEEP_WAKEUP_EXT1:
    //         return WakeupCause::EXTERNAL;
    //     default:
    //         return WakeupCause::UNKNOWN;
    // }
    #endif
    
    return WakeupCause::UNKNOWN;