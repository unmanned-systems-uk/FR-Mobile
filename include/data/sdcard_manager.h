#pragma once

#include "interfaces.h"
#include <string>
#include <vector>
#include <fstream>
#include <mutex>
#include <memory>

/**
 * @file sdcard_manager.h
 * @brief SD Card Manager for file operations and data persistence
 * @author Anthony Kirk
 * @date 2025-07-29
 * @version 1.0.0
 *
 * This class provides comprehensive SD card management including file operations,
 * data logging, capacity monitoring, and error recovery. Designed for reliable
 * data persistence in field deployment scenarios.
 */

/**
 * @brief SD Card information and statistics structure
 */
struct SDCardInfo
{
    uint64_t totalSpace = 0;      ///< Total card capacity in bytes
    uint64_t usedSpace = 0;       ///< Used space in bytes
    uint64_t availableSpace = 0;  ///< Available space in bytes
    float capacityPercent = 0.0f; ///< Used capacity percentage
    std::string cardType;         ///< Card type (SD, SDHC, SDXC)
    std::string fileSystem;       ///< File system type (FAT32, exFAT)
    bool mounted = false;         ///< Mount status
    uint32_t sectorSize = 512;    ///< Sector size in bytes
    uint64_t sectorCount = 0;     ///< Total number of sectors
};

/**
 * @brief File operation statistics for monitoring and diagnostics
 */
struct FileOperationStats
{
    uint64_t filesCreated = 0;      ///< Number of files created
    uint64_t filesWritten = 0;      ///< Number of successful writes
    uint64_t filesRead = 0;         ///< Number of successful reads
    uint64_t bytesWritten = 0;      ///< Total bytes written
    uint64_t bytesRead = 0;         ///< Total bytes read
    uint64_t writeErrors = 0;       ///< Number of write errors
    uint64_t readErrors = 0;        ///< Number of read errors
    uint64_t lastOperationTime = 0; ///< Timestamp of last operation
};

/**
 * @brief SD Card Manager implementing comprehensive file operations and data management
 *
 * This class provides a complete interface for SD card operations optimized for
 * the forestry research device. Features include:
 *
 * - Reliable file operations with error recovery
 * - CSV data logging with proper formatting
 * - Capacity monitoring and low-space warnings
 * - Directory management and file organization
 * - Atomic write operations for data integrity
 * - Performance monitoring and statistics
 * - Thread-safe operations with proper locking
 * - Automatic mount/unmount handling
 */
class SDCardManager : public IDataStorage
{
public:
    /**
     * @brief Constructor
     * @param basePath Base path for file operations (default: "/sdcard")
     */
    explicit SDCardManager(const std::string &basePath = "/sdcard");

    /**
     * @brief Destructor - ensures proper cleanup and unmounting
     */
    ~SDCardManager() override;

    // IDataStorage interface implementation
    /**
     * @brief Write probe request data to specified file
     * @param data Probe request data to write
     * @param filename Target filename for the data
     * @return true if write successful, false otherwise
     */
    bool writeData(const ProbeRequest &data, const std::string &filename) override;

    /**
     * @brief Write asset information to dedicated file
     * @param data Asset information to write
     * @return true if write successful, false otherwise
     */
    bool writeAssetData(const AssetInfo &data) override;

    /**
     * @brief Read all lines from specified file
     * @param filename File to read
     * @return Vector of strings containing file lines
     */
    std::vector<std::string> readFile(const std::string &filename) override;

    /**
     * @brief Check if specified file exists
     * @param filename File to check
     * @return true if file exists, false otherwise
     */
    bool fileExists(const std::string &filename) override;

    /**
     * @brief Get size of specified file
     * @param filename File to check
     * @return File size in bytes, 0 if file doesn't exist
     */
    size_t getFileSize(const std::string &filename) override;

    /**
     * @brief Get remaining storage capacity as percentage
     * @return Remaining capacity percentage (0.0 - 100.0)
     */
    float getRemainingCapacityPercent() override;

    // SD Card specific operations
    /**
     * @brief Initialize and mount the SD card
     * @param maxRetries Maximum number of mount attempts (default: 20)
     * @param retryDelayMs Delay between retry attempts in ms (default: 200)
     * @return true if initialization successful, false otherwise
     */
    bool initialize(int maxRetries = 20, int retryDelayMs = 200);

    /**
     * @brief Safely unmount the SD card
     * @return true if unmount successful, false otherwise
     */
    bool unmount();

    /**
     * @brief Get comprehensive SD card information
     * @return SD card info structure with capacity, type, and status
     */
    SDCardInfo getCardInfo();

    /**
     * @brief Get file operation statistics for monitoring
     * @return Statistics structure with operation counts and performance data
     */
    FileOperationStats getOperationStats() const;

    // File and directory management
    /**
     * @brief Create directory structure if it doesn't exist
     * @param path Directory path to create
     * @return true if directory exists or was created successfully
     */
    bool createDirectory(const std::string &path);

    /**
     * @brief List all files in specified directory
     * @param directory Directory to list (default: root)
     * @param recursive Include subdirectories (default: false)
     * @return Vector of filenames found
     */
    std::vector<std::string> listFiles(const std::string &directory = "", bool recursive = false);

    /**
     * @brief Delete specified file
     * @param filename File to delete
     * @return true if file was deleted or didn't exist, false on error
     */
    bool deleteFile(const std::string &filename);

    /**
     * @brief Create new data file with CSV header
     * @param filename File to create
     * @param customHeader Optional custom header (uses default if empty)
     * @return true if file created successfully, false otherwise
     */
    bool createDataFile(const std::string &filename, const std::string &customHeader = "");

    // Data integrity and backup operations
    /**
     * @brief Write data with atomic operation (temporary file + rename)
     * @param filename Target filename
     * @param data Data to write
     * @return true if atomic write successful, false otherwise
     */
    bool writeFileAtomic(const std::string &filename, const std::string &data);

    /**
     * @brief Create backup copy of specified file
     * @param filename Original file to backup
     * @param backupSuffix Suffix for backup file (default: ".bak")
     * @return true if backup created successfully, false otherwise
     */
    bool createBackup(const std::string &filename, const std::string &backupSuffix = ".bak");

    /**
     * @brief Verify file integrity using checksum
     * @param filename File to verify
     * @return true if file is valid, false if corrupted or missing
     */
    bool verifyFileIntegrity(const std::string &filename);

    // Capacity and maintenance operations
    /**
     * @brief Check if SD card has sufficient free space
     * @param requiredBytes Minimum required free space in bytes
     * @return true if sufficient space available, false otherwise
     */
    bool hasSufficientSpace(size_t requiredBytes);

    /**
     * @brief Get free space in bytes
     * @return Available free space in bytes
     */
    uint64_t getFreeSpace();

    /**
     * @brief Get used space in bytes
     * @return Used space in bytes
     */
    uint64_t getUsedSpace();

    /**
     * @brief Clean up old files to free space
     * @param targetFreePercent Target free space percentage (default: 20%)
     * @param deletePattern File pattern to delete (default: oldest data files)
     * @return Number of files deleted
     */
    int cleanupOldFiles(float targetFreePercent = 20.0f, const std::string &deletePattern = "data_*.csv");

    // Error handling and recovery
    /**
     * @brief Check SD card health and perform basic diagnostics
     * @return true if card appears healthy, false if issues detected
     */
    bool performHealthCheck();

    /**
     * @brief Attempt to recover from SD card errors
     * @return true if recovery successful, false otherwise
     */
    bool attemptErrorRecovery();

    /**
     * @brief Get last error message for diagnostics
     * @return String describing the last error that occurred
     */
    std::string getLastError() const { return lastError_; }

    // Configuration and settings
    /**
     * @brief Set low space warning threshold
     * @param thresholdPercent Percentage below which to trigger warnings (default: 10%)
     */
    void setLowSpaceThreshold(float thresholdPercent) { lowSpaceThreshold_ = thresholdPercent; }

    /**
     * @brief Enable/disable automatic cleanup when space is low
     * @param enabled Whether to automatically clean up old files
     */
    void setAutoCleanup(bool enabled) { autoCleanupEnabled_ = enabled; }

    /**
     * @brief Set maximum file size before rotation/splitting
     * @param maxSizeBytes Maximum file size in bytes (0 = no limit)
     */
    void setMaxFileSize(size_t maxSizeBytes) { maxFileSize_ = maxSizeBytes; }

private:
    // Configuration
    std::string basePath_;    ///< Base path for all operations
    bool initialized_;        ///< Initialization status
    float lowSpaceThreshold_; ///< Low space warning threshold (%)
    bool autoCleanupEnabled_; ///< Automatic cleanup enabled flag
    size_t maxFileSize_;      ///< Maximum file size before rotation

    // State tracking
    SDCardInfo cardInfo_;               ///< Current card information
    FileOperationStats stats_;          ///< Operation statistics
    std::string lastError_;             ///< Last error message
    mutable std::mutex operationMutex_; ///< Thread safety for operations
    mutable std::mutex statsMutex_;     ///< Thread safety for statistics

    // Platform-specific handle
    void *platformHandle_; ///< Platform-specific SD card handle

    // Internal file operations
    /**
     * @brief Internal method to write string data to file
     * @param fullPath Complete file path
     * @param data Data to write
     * @param append Whether to append (true) or overwrite (false)
     * @return true if write successful, false otherwise
     */
    bool writeToFile(const std::string &fullPath, const std::string &data, bool append = true);

    /**
     * @brief Internal method to read entire file content
     * @param fullPath Complete file path
     * @return File content as string, empty if error
     */
    std::string readFromFile(const std::string &fullPath);

    /**
     * @brief Convert relative path to absolute path
     * @param relativePath Path relative to base path
     * @return Complete absolute path
     */
    std::string getFullPath(const std::string &relativePath);

    /**
     * @brief Sanitize filename for cross-platform compatibility
     * @param filename Original filename
     * @return Sanitized filename safe for file system
     */
    std::string sanitizeFilename(const std::string &filename);

    // Statistics and monitoring
    /**
     * @brief Update operation statistics
     * @param operation Type of operation performed
     * @param success Whether operation was successful
     * @param bytes Number of bytes involved in operation
     */
    void updateStats(const std::string &operation, bool success, size_t bytes = 0);

    /**
     * @brief Check for low space and trigger cleanup if enabled
     */
    void checkAndHandleLowSpace();

    // Platform-specific implementations
    /**
     * @brief Platform-specific SD card mounting
     * @return true if mount successful, false otherwise
     */
    bool platformMount();

    /**
     * @brief Platform-specific SD card unmounting
     * @return true if unmount successful, false otherwise
     */
    bool platformUnmount();

    /**
     * @brief Platform-specific card information retrieval
     * @return Updated card information structure
     */
    SDCardInfo platformGetCardInfo();

    // Constants
    static constexpr size_t DEFAULT_SECTOR_SIZE = 512;          ///< Default sector size
    static constexpr float DEFAULT_LOW_SPACE_THRESHOLD = 10.0f; ///< Default low space threshold
    static constexpr size_t MAX_FILENAME_LENGTH = 255;          ///< Maximum filename length
    static constexpr const char *DEFAULT_CSV_HEADER =           ///< Default CSV header
        "dataType,timestamp,source,rssi,packetLength,macAddress,payload";
    static constexpr const char *ASSET_CSV_HEADER = ///< Asset data CSV header
        "assetId,locationName,forestName,latitude,longitude,"
        "remainingBatteryCapacity,stateOfCharge,runtimeToEmpty,"
        "batteryVoltage,batteryCurrent,SDCardCapacity,timeStamp";
};