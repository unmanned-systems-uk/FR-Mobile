#include "main.h"
#include "interfaces.h"
#include "scanners/wifi_scanner.h"
#include "scanners/ble_scanner.h"
#include "hardware/bq34z100_battery_monitor.h"
#include "hardware/power_manager.h"
#include "data/sdcard_manager.h"
#include "data/cellular_manager.h"
#include "data/rtc_time_manager.h"
#include "utils/logger.h"
#include "utils/utils.h"

#include <memory>
#include <chrono>
#include <thread>
#include <atomic>
#include <csignal>
#include <exception>

namespace ForestryDevice
{

    /**
     * @brief Main Application Class
     *
     * Orchestrates all system components with proper state management,
     * error handling, and resource cleanup.
     */
    class ForestryResearchApplication
    {
    private:
        // System state management
        SystemState currentState;
        std::atomic<bool> shouldShutdown{false};
        std::atomic<bool> emergencyStop{false};

        // Core component instances
        std::unique_ptr<WiFiScanner> wifiScanner;
        std::unique_ptr<BLEScanner> bleScanner;
        std::unique_ptr<BQ34Z100BatteryMonitor> batteryMonitor;
        std::unique_ptr<PowerManager> powerManager;
        std::unique_ptr<SDCardManager> sdCardManager;
        std::unique_ptr<CellularManager> cellularManager;
        std::unique_ptr<RTCTimeManager> timeManager;

        // System statistics
        struct SystemStats
        {
            size_t totalWiFiDevices = 0;
            size_t totalBLEDevices = 0;
            size_t filesCreated = 0;
            size_t uploadsCompleted = 0;
            std::chrono::steady_clock::time_point startTime;
            std::chrono::seconds totalRunTime{0};
        } stats;

        // Configuration
        Config::ScanConfig scanConfig;
        Config::PowerConfig powerConfig;
        Config::DataConfig dataConfig;

    public:
        ForestryResearchApplication() : currentState(SystemState::INITIALIZING)
        {
            stats.startTime = std::chrono::steady_clock::now();
            initializeConfiguration();
        }

        ~ForestryResearchApplication()
        {
            shutdown();
        }

        /**
         * @brief Main application entry point
         * @return Exit code (0 = success, non-zero = error)
         */
        int run()
        {
            try
            {
                LOG_INFO("ForestryResearchDevice", "=== Forestry Research Device Starting ===");
                LOG_INFO("ForestryResearchDevice", "Version: {}.{}.{}",
                         VERSION_MAJOR, VERSION_MINOR, VERSION_PATCH);

                // Install signal handlers for graceful shutdown
                installSignalHandlers();

                // Initialize all components
                if (!initializeSystem())
                {
                    LOG_ERROR("ForestryResearchDevice", "System initialization failed");
                    return -1;
                }

                // Run main operation loop
                runMainLoop();

                // Graceful shutdown
                shutdown();

                LOG_INFO("ForestryResearchDevice", "=== Application Shutdown Complete ===");
                printFinalStatistics();

                return 0;
            }
            catch (const std::exception &e)
            {
                LOG_CRITICAL("ForestryResearchDevice", "Unhandled exception: {}", e.what());
                return -2;
            }
            catch (...)
            {
                LOG_CRITICAL("ForestryResearchDevice", "Unknown exception occurred");
                return -3;
            }
        }

    private:
        /**
         * @brief Initialize system configuration
         */
        void initializeConfiguration()
        {
            // Scan configuration
            scanConfig.wifiChannels = {1, 6, 11}; // Common 2.4GHz channels
            scanConfig.wifiScanDuration = std::chrono::seconds(30);
            scanConfig.bleScanDuration = std::chrono::seconds(30);
            scanConfig.rssiThreshold = -80;
            scanConfig.enableRealTimeCallbacks = true;

            // Power configuration
            powerConfig.batteryCheckInterval = std::chrono::minutes(5);
            powerConfig.lowBatteryThreshold = 20.0f;      // 20%
            powerConfig.criticalBatteryThreshold = 10.0f; // 10%
            powerConfig.enableDeepSleep = true;
            powerConfig.sleepDuration = std::chrono::hours(1);

            // Data configuration
            dataConfig.maxFileSize = 10 * 1024 * 1024; // 10MB
            dataConfig.uploadInterval = std::chrono::hours(6);
            dataConfig.enableCompression = true;
            dataConfig.retainLocalCopy = true;
        }

        /**
         * @brief Install signal handlers for graceful shutdown
         */
        void installSignalHandlers()
        {
            std::signal(SIGINT, [](int)
                        {
                            LOG_INFO("ForestryResearchDevice", "Received SIGINT, initiating graceful shutdown...");
                            // Note: In a real implementation, we'd use a global instance pointer
                            // or a more sophisticated signal handling approach
                        });

            std::signal(SIGTERM, [](int)
                        { LOG_INFO("ForestryResearchDevice", "Received SIGTERM, initiating graceful shutdown..."); });
        }

        /**
         * @brief Initialize all system components
         * @return true if successful, false otherwise
         */
        bool initializeSystem()
        {
            try
            {
                currentState = SystemState::INITIALIZING;

                // Initialize time management first (needed for logging timestamps)
                LOG_INFO("ForestryResearchDevice", "Initializing time management...");
                timeManager = std::make_unique<RTCTimeManager>();
                if (!timeManager->initialize())
                {
                    LOG_WARNING("ForestryResearchDevice", "RTC initialization failed, using system time");
                }

                // Initialize power management
                LOG_INFO("ForestryResearchDevice", "Initializing power management...");
                powerManager = std::make_unique<PowerManager>();
                if (!powerManager->initialize())
                {
                    LOG_ERROR("ForestryResearchDevice", "Power manager initialization failed");
                    return false;
                }

                // Initialize battery monitoring
                LOG_INFO("ForestryResearchDevice", "Initializing battery monitor...");
                batteryMonitor = std::make_unique<BQ34Z100BatteryMonitor>();
                if (!batteryMonitor->initialize())
                {
                    LOG_WARNING("ForestryResearchDevice", "Battery monitor initialization failed");
                }

                // Initialize data storage
                LOG_INFO("ForestryResearchDevice", "Initializing SD card manager...");
                sdCardManager = std::make_unique<SDCardManager>();
                if (!sdCardManager->initialize())
                {
                    LOG_ERROR("ForestryResearchDevice", "SD card initialization failed");
                    return false;
                }

                // Initialize cellular connectivity
                LOG_INFO("ForestryResearchDevice", "Initializing cellular manager...");
                cellularManager = std::make_unique<CellularManager>();
                if (!cellularManager->initialize())
                {
                    LOG_WARNING("ForestryResearchDevice", "Cellular initialization failed, will retry later");
                }

                // Initialize scanners
                LOG_INFO("ForestryResearchDevice", "Initializing WiFi scanner...");
                wifiScanner = std::make_unique<WiFiScanner>();
                if (!wifiScanner->initialize())
                {
                    LOG_ERROR("ForestryResearchDevice", "WiFi scanner initialization failed");
                    return false;
                }

                LOG_INFO("ForestryResearchDevice", "Initializing BLE scanner...");
                bleScanner = std::make_unique<BLEScanner>();
                if (!bleScanner->initialize())
                {
                    LOG_ERROR("ForestryResearchDevice", "BLE scanner initialization failed");
                    return false;
                }

                // Set up real-time callbacks if enabled
                if (scanConfig.enableRealTimeCallbacks)
                {
                    setupRealTimeCallbacks();
                }

                currentState = SystemState::READY;
                LOG_INFO("ForestryResearchDevice", "System initialization complete");
                return true;
            }
            catch (const std::exception &e)
            {
                LOG_ERROR("ForestryResearchDevice", "Initialization exception: {}", e.what());
                return false;
            }
        }

        /**
         * @brief Setup real-time data processing callbacks
         */
        void setupRealTimeCallbacks()
        {
            // WiFi device detection callback
            wifiScanner->setRealTimeCallback([this](const ProbeRequest &device)
                                             {
            stats.totalWiFiDevices++;
            LOG_DEBUG("WiFiScanner", "Real-time WiFi device: {} ({})", 
                     device.macAddress, device.rssi);
            
            // Immediate processing for high-value targets
            if (device.rssi > -50) { // Very close device
                LOG_INFO("WiFiScanner", "High-strength WiFi signal detected: {} at {} dBm", 
                        device.macAddress, device.rssi);
            } });

            // BLE device detection callback
            bleScanner->setRealTimeCallback([this](const ProbeRequest &device)
                                            {
            stats.totalBLEDevices++;
            LOG_DEBUG("BLEScanner", "Real-time BLE device: {} ({})", 
                     device.macAddress, device.rssi);
            
            // Log interesting BLE devices
            if (!device.deviceName.empty()) {
                LOG_INFO("BLEScanner", "Named BLE device: '{}' [{}]", 
                        device.deviceName, device.macAddress);
            } });
        }

        /**
         * @brief Main operational loop
         */
        void runMainLoop()
        {
            currentState = SystemState::RUNNING;
            LOG_INFO("ForestryResearchDevice", "Entering main operation loop");

            auto lastBatteryCheck = std::chrono::steady_clock::now();
            auto lastDataUpload = std::chrono::steady_clock::now();
            auto cycleStartTime = std::chrono::steady_clock::now();

            while (!shouldShutdown.load() && !emergencyStop.load())
            {
                try
                {
                    auto cycleStart = std::chrono::steady_clock::now();

                    // Check battery status periodically
                    if (std::chrono::steady_clock::now() - lastBatteryCheck >= powerConfig.batteryCheckInterval)
                    {
                        if (!checkBatteryStatus())
                        {
                            break; // Emergency stop due to low battery
                        }
                        lastBatteryCheck = std::chrono::steady_clock::now();
                    }

                    // Perform scanning cycle
                    performScanningCycle();

                    // Check if we should upload data
                    if (std::chrono::steady_clock::now() - lastDataUpload >= dataConfig.uploadInterval)
                    {
                        uploadCollectedData();
                        lastDataUpload = std::chrono::steady_clock::now();
                    }

                    // Log cycle statistics
                    auto cycleEnd = std::chrono::steady_clock::now();
                    auto cycleDuration = std::chrono::duration_cast<std::chrono::seconds>(
                        cycleEnd - cycleStart);

                    LOG_INFO("ForestryResearchDevice",
                             "Cycle complete - WiFi: {}, BLE: {}, Duration: {}s",
                             wifiScanner->getResultCount(),
                             bleScanner->getResultCount(),
                             cycleDuration.count());

                    // Brief pause between cycles
                    std::this_thread::sleep_for(std::chrono::seconds(5));
                }
                catch (const std::exception &e)
                {
                    LOG_ERROR("ForestryResearchDevice", "Main loop exception: {}", e.what());
                    std::this_thread::sleep_for(std::chrono::seconds(10)); // Recovery pause
                }
            }

            if (emergencyStop.load())
            {
                LOG_CRITICAL("ForestryResearchDevice", "Emergency stop triggered!");
                currentState = SystemState::EMERGENCY;
            }
            else
            {
                LOG_INFO("ForestryResearchDevice", "Main loop shutdown requested");
                currentState = SystemState::SHUTDOWN;
            }
        }

        /**
         * @brief Check battery status and handle low battery conditions
         * @return false if emergency stop is needed
         */
        bool checkBatteryStatus()
        {
            if (!batteryMonitor)
                return true;

            try
            {
                auto status = batteryMonitor->getBatteryStatus();

                LOG_INFO("BatteryMonitor",
                         "Battery Status - Level: {:.1f}%, Voltage: {:.2f}V, Current: {:.0f}mA",
                         status.stateOfCharge, status.voltage, status.current);

                // Check for critical battery level
                if (status.stateOfCharge < powerConfig.criticalBatteryThreshold)
                {
                    LOG_CRITICAL("ForestryResearchDevice",
                                 "Critical battery level: {:.1f}% - Initiating emergency shutdown",
                                 status.stateOfCharge);

                    // Save current data immediately
                    saveEmergencyData();

                    // Trigger deep sleep or shutdown
                    if (powerManager && powerConfig.enableDeepSleep)
                    {
                        LOG_INFO("ForestryResearchDevice", "Entering deep sleep mode");
                        powerManager->enterDeepSleep(powerConfig.sleepDuration);
                    }

                    emergencyStop.store(true);
                    return false;
                }
                else if (status.stateOfCharge < powerConfig.lowBatteryThreshold)
                {
                    LOG_WARNING("ForestryResearchDevice",
                                "Low battery level: {:.1f}% - Consider recharging",
                                status.stateOfCharge);
                }

                return true;
            }
            catch (const std::exception &e)
            {
                LOG_ERROR("ForestryResearchDevice", "Battery check failed: {}", e.what());
                return true; // Continue operation if battery check fails
            }
        }

        /**
         * @brief Perform a complete scanning cycle
         */
        void performScanningCycle()
        {
            LOG_INFO("ForestryResearchDevice", "Starting scanning cycle");

            // Clear previous results
            wifiScanner->clearResults();
            bleScanner->clearResults();

            // Start both scanners concurrently
            LOG_INFO("ForestryResearchDevice", "Starting WiFi and BLE scans");

            // WiFi scan
            auto wifiThread = std::thread([this]()
                                          {
            try {
                wifiScanner->startScan(scanConfig.wifiChannels, scanConfig.wifiScanDuration);
                while (wifiScanner->isScanning()) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                }
            } catch (const std::exception& e) {
                LOG_ERROR("WiFiScanner", "WiFi scan error: {}", e.what());
            } });

            // BLE scan
            auto bleThread = std::thread([this]()
                                         {
            try {
                bleScanner->startScan(scanConfig.bleScanDuration);
                while (bleScanner->isScanning()) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                }
            } catch (const std::exception& e) {
                LOG_ERROR("BLEScanner", "BLE scan error: {}", e.what());
            } });

            // Wait for both scans to complete
            if (wifiThread.joinable())
                wifiThread.join();
            if (bleThread.joinable())
                bleThread.join();

            // Collect and save results
            saveScannedData();

            LOG_INFO("ForestryResearchDevice",
                     "Scanning cycle complete - WiFi: {} devices, BLE: {} devices",
                     wifiScanner->getResultCount(), bleScanner->getResultCount());
        }

        /**
         * @brief Save scanned data to local storage
         */
        void saveScannedData()
        {
            if (!sdCardManager)
                return;

            try
            {
                // Get current timestamp
                auto timestamp = timeManager ? timeManager->getCurrentTime() : std::chrono::system_clock::now();

                // Create filename with timestamp
                auto filename = Utils::formatTimestamp(timestamp, "scan_data_%Y%m%d_%H%M%S.csv");

                // Collect all scan results
                auto wifiResults = wifiScanner->getResults();
                auto bleResults = bleScanner->getResults();

                // Save to CSV file
                if (sdCardManager->saveDataToCSV(filename, wifiResults, bleResults))
                {
                    stats.filesCreated++;
                    LOG_INFO("ForestryResearchDevice", "Scan data saved to: {}", filename);
                }
                else
                {
                    LOG_ERROR("ForestryResearchDevice", "Failed to save scan data");
                }
            }
            catch (const std::exception &e)
            {
                LOG_ERROR("ForestryResearchDevice", "Save data exception: {}", e.what());
            }
        }

        /**
         * @brief Upload collected data via cellular connection
         */
        void uploadCollectedData()
        {
            if (!cellularManager || !sdCardManager)
                return;

            try
            {
                LOG_INFO("ForestryResearchDevice", "Starting data upload");

                // Check cellular connection
                if (!cellularManager->isConnected())
                {
                    LOG_INFO("CellularManager", "Attempting to connect to network");
                    if (!cellularManager->connect())
                    {
                        LOG_WARNING("ForestryResearchDevice", "Cellular connection failed, upload postponed");
                        return;
                    }
                }

                // Get list of files to upload
                auto files = sdCardManager->getDataFiles();
                LOG_INFO("ForestryResearchDevice", "Found {} files to upload", files.size());

                size_t uploadedCount = 0;
                for (const auto &filename : files)
                {
                    try
                    {
                        if (cellularManager->uploadFile(filename))
                        {
                            uploadedCount++;
                            stats.uploadsCompleted++;

                            // Optionally delete uploaded file if not retaining local copy
                            if (!dataConfig.retainLocalCopy)
                            {
                                sdCardManager->deleteFile(filename);
                            }

                            LOG_INFO("ForestryResearchDevice", "Successfully uploaded: {}", filename);
                        }
                        else
                        {
                            LOG_WARNING("ForestryResearchDevice", "Failed to upload: {}", filename);
                        }
                    }
                    catch (const std::exception &e)
                    {
                        LOG_ERROR("ForestryResearchDevice", "Upload error for {}: {}", filename, e.what());
                    }

                    // Brief pause between uploads
                    std::this_thread::sleep_for(std::chrono::seconds(1));
                }

                LOG_INFO("ForestryResearchDevice", "Upload session complete - {}/{} files uploaded",
                         uploadedCount, files.size());
            }
            catch (const std::exception &e)
            {
                LOG_ERROR("ForestryResearchDevice", "Upload session exception: {}", e.what());
            }
        }

        /**
         * @brief Save critical data during emergency shutdown
         */
        void saveEmergencyData()
        {
            try
            {
                LOG_WARNING("ForestryResearchDevice", "Saving emergency data");

                if (sdCardManager)
                {
                    // Save current scan results if any
                    auto wifiResults = wifiScanner ? wifiScanner->getResults() : std::vector<ProbeRequest>{};
                    auto bleResults = bleScanner ? bleScanner->getResults() : std::vector<ProbeRequest>{};

                    if (!wifiResults.empty() || !bleResults.empty())
                    {
                        auto timestamp = std::chrono::system_clock::now();
                        auto filename = Utils::formatTimestamp(timestamp, "emergency_data_%Y%m%d_%H%M%S.csv");

                        if (sdCardManager->saveDataToCSV(filename, wifiResults, bleResults))
                        {
                            LOG_INFO("ForestryResearchDevice", "Emergency data saved to: {}", filename);
                        }
                    }

                    // Save system state and statistics
                    saveSystemState();
                }
            }
            catch (const std::exception &e)
            {
                LOG_ERROR("ForestryResearchDevice", "Emergency save failed: {}", e.what());
            }
        }

        /**
         * @brief Save current system state and statistics
         */
        void saveSystemState()
        {
            try
            {
                if (!sdCardManager)
                    return;

                auto timestamp = std::chrono::system_clock::now();
                auto filename = Utils::formatTimestamp(timestamp, "system_state_%Y%m%d_%H%M%S.json");

                // Calculate runtime
                auto now = std::chrono::steady_clock::now();
                auto runtime = std::chrono::duration_cast<std::chrono::seconds>(now - stats.startTime);

                // Create system state JSON (simplified representation)
                std::ostringstream stateJson;
                stateJson << "{\n";
                stateJson << "  \"timestamp\": \"" << Utils::formatTimestamp(timestamp) << "\",\n";
                stateJson << "  \"runtime_seconds\": " << runtime.count() << ",\n";
                stateJson << "  \"total_wifi_devices\": " << stats.totalWiFiDevices << ",\n";
                stateJson << "  \"total_ble_devices\": " << stats.totalBLEDevices << ",\n";
                stateJson << "  \"files_created\": " << stats.filesCreated << ",\n";
                stateJson << "  \"uploads_completed\": " << stats.uploadsCompleted << ",\n";
                stateJson << "  \"system_state\": \"" << systemStateToString(currentState) << "\"\n";
                stateJson << "}\n";

                if (sdCardManager->writeFile(filename, stateJson.str()))
                {
                    LOG_INFO("ForestryResearchDevice", "System state saved to: {}", filename);
                }
            }
            catch (const std::exception &e)
            {
                LOG_ERROR("ForestryResearchDevice", "Save system state failed: {}", e.what());
            }
        }

        /**
         * @brief Graceful system shutdown
         */
        void shutdown()
        {
            LOG_INFO("ForestryResearchDevice", "Initiating system shutdown");
            currentState = SystemState::SHUTDOWN;

            // Stop all scanning operations
            if (wifiScanner && wifiScanner->isScanning())
            {
                wifiScanner->stopScan();
            }
            if (bleScanner && bleScanner->isScanning())
            {
                bleScanner->stopScan();
            }

            // Save final system state
            saveSystemState();

            // Upload any remaining data
            if (cellularManager && cellularManager->isConnected())
            {
                LOG_INFO("ForestryResearchDevice", "Final data upload attempt");
                uploadCollectedData();
            }

            // Cleanup components in reverse order
            cellularManager.reset();
            sdCardManager.reset();
            batteryMonitor.reset();
            bleScanner.reset();
            wifiScanner.reset();
            powerManager.reset();
            timeManager.reset();

            LOG_INFO("ForestryResearchDevice", "System shutdown complete");
        }

        /**
         * @brief Print final system statistics
         */
        void printFinalStatistics()
        {
            auto endTime = std::chrono::steady_clock::now();
            auto totalRuntime = std::chrono::duration_cast<std::chrono::seconds>(
                endTime - stats.startTime);

            LOG_INFO("ForestryResearchDevice", "=== Final Statistics ===");
            LOG_INFO("ForestryResearchDevice", "Total Runtime: {} seconds ({} minutes)",
                     totalRuntime.count(), totalRuntime.count() / 60);
            LOG_INFO("ForestryResearchDevice", "WiFi Devices Detected: {}", stats.totalWiFiDevices);
            LOG_INFO("ForestryResearchDevice", "BLE Devices Detected: {}", stats.totalBLEDevices);
            LOG_INFO("ForestryResearchDevice", "Files Created: {}", stats.filesCreated);
            LOG_INFO("ForestryResearchDevice", "Uploads Completed: {}", stats.uploadsCompleted);
            LOG_INFO("ForestryResearchDevice", "=======================");
        }

        /**
         * @brief Convert system state enum to string
         */
        std::string systemStateToString(SystemState state)
        {
            switch (state)
            {
            case SystemState::INITIALIZING:
                return "INITIALIZING";
            case SystemState::READY:
                return "READY";
            case SystemState::RUNNING:
                return "RUNNING";
            case SystemState::EMERGENCY:
                return "EMERGENCY";
            case SystemState::SHUTDOWN:
                return "SHUTDOWN";
            default:
                return "UNKNOWN";
            }
        }
    };

} // namespace ForestryDevice

/**
 * @brief Application entry point
 */
int main()
{
    try
    {
        // Initialize global logger first
        Logger::initialize(LogLevel::INFO, LogLevel::DEBUG);

        // Create and run the application
        ForestryDevice::ForestryResearchApplication app;
        return app.run();
    }
    catch (const std::exception &e)
    {
        // Fallback error handling if logger isn't available
        std::cerr << "Fatal error: " << e.what() << std::endl;
        return -1;
    }
    catch (...)
    {
        std::cerr << "Unknown fatal error occurred" << std::endl;
        return -2;
    }
}