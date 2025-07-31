SD Card Manager Implementation Features:
1. Comprehensive File Operations

Thread-safe read/write operations with mutex protection
Atomic writes using temporary files for data integrity
Automatic file rotation when size limits are reached
CSV formatting for probe requests and asset data

2. Capacity Management

Real-time space monitoring and reporting
Automatic cleanup of old files when space is low
Configurable low-space threshold warnings
Support for different card types (SD, SDHC, SDXC)

3. Error Recovery

Retry mechanism for mount operations
Health check functionality
Automatic remount on errors
Comprehensive error logging

4. Directory Structure

Automatic creation of data and logs directories
Recursive directory listing
Path sanitization for cross-platform compatibility

5. Statistics and Monitoring

Track files created, read, written
Byte-level transfer statistics
Operation success/failure tracking
Last operation timestamp

6. Platform Abstraction

ESP32 implementation using Arduino SD library
Development platform using standard C++ filesystem
Proper SPI initialization for ESP32

7. Data File Management

Automatic CSV header creation
Proper escaping and formatting
Support for both probe data and asset information
File backup functionality

Integration with Other Components:
The SD Card Manager integrates seamlessly with:

RTC Time Manager - For accurate file timestamps
WiFi/BLE Scanners - To store probe request data
Battery Monitor - To log asset information
Main Application - For data persistence

Usage Example:
cpp// Initialize SD card
SDCardManager sdCard("/sdcard");
if (!sdCard.initialize(20, 200))  // 20 retries, 200ms delay
{
    LOG_ERROR("Failed to initialize SD card");
    return false;
}

// Check available space
float freePercent = sdCard.getRemainingCapacityPercent();
LOG_INFO("SD card has " + std::to_string(freePercent) + "% free space");

// Write probe request data
ProbeRequest probe;
probe.dataType = "wifi";
probe.timestamp = rtcManager.getCurrentDateTime();
probe.source = Config::DEVICE_ID;
probe.rssi = -65;
probe.packetLength = 128;
probe.macAddress = "AA:BB:CC:DD:EE:FF";
probe.payload = "test_ssid";

sdCard.writeData(probe, "wifi_data_20250729.csv");

// Write asset information
AssetInfo asset;
asset.assetId = Config::ASSET_ID;
asset.locationName = Config::LOCATION;
asset.forestName = Config::FOREST_NAME;
asset.latitude = std::to_string(Config::LATITUDE);
asset.longitude = std::to_string(Config::LONGITUDE);
asset.batteryVoltage = batteryStatus.voltage;
asset.stateOfCharge = batteryStatus.stateOfCharge;
asset.sdCardCapacity = freePercent;
asset.timeStamp = rtcManager.getCurrentDateTime();

sdCard.writeAssetData(asset);

// Enable automatic cleanup
sdCard.setLowSpaceThreshold(10.0f);  // Warn at 10% free
sdCard.setAutoCleanup(true);         // Auto-delete old files

// Set file rotation
sdCard.setMaxFileSize(10 * 1024 * 1024);  // 10MB max file size
Key Design Features:

Data Integrity: Atomic writes prevent data corruption during power loss
Automatic Management: Self-cleaning when space is low
Robust Error Handling: Retry mechanisms and error recovery
Performance Tracking: Detailed statistics for monitoring
Thread Safety: All operations are protected by mutexes