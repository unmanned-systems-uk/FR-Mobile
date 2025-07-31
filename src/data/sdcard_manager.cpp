#include "data/sdcard_manager.h"
#include "utils/logger.h"
#include "main.h"
#include <algorithm>
#include <sstream>
#include <iomanip>
#include <chrono>
#include <filesystem>

#ifdef ESP32_PLATFORM
#include "esp_vfs_fat.h"
#include "driver/sdmmc_host.h"
#include "driver/sdspi_host.h"
#include "sdmmc_cmd.h"
#include "driver/spi_common.h"
#include <SD.h>
#include <SPI.h>
#elif defined(RASPBERRY_PI_PLATFORM)
// Raspberry Pi uses standard filesystem for SD card access
#include <fstream>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <dirent.h>
#include <filesystem>
#else
// Generic development platform
#include <fstream>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <dirent.h>
#endif

namespace fs = std::filesystem;

// Constructor
SDCardManager::SDCardManager(const std::string &basePath)
    : basePath_(basePath), initialized_(false), lowSpaceThreshold_(DEFAULT_LOW_SPACE_THRESHOLD), autoCleanupEnabled_(true), maxFileSize_(0) // No limit by default
      ,
      platformHandle_(nullptr)
{
    // Initialize statistics
    stats_ = FileOperationStats();
    cardInfo_ = SDCardInfo();

    // Ensure base path ends with separator
    if (!basePath_.empty() && basePath_.back() != '/')
    {
        basePath_ += '/';
    }

    LOG_INFO("[SDCardManager] Created with base path: " + basePath_);
}

// Destructor
SDCardManager::~SDCardManager()
{
    if (initialized_)
    {
        LOG_INFO("[SDCardManager] Shutting down SD card operations");
        unmount();
    }
}

// Initialize and mount SD card
bool SDCardManager::initialize(int maxRetries, int retryDelayMs)
{
    if (initialized_)
    {
        LOG_WARNING("[SDCardManager] Already initialized");
        return true;
    }

    LOG_INFO("[SDCardManager] Initializing SD card with " +
             std::to_string(maxRetries) + " retry attempts");

    int attempts = 0;
    while (attempts < maxRetries)
    {
        attempts++;

        if (platformMount())
        {
            // Get card information
            cardInfo_ = platformGetCardInfo();

            LOG_INFO("[SDCardManager] SD card mounted successfully");
            LOG_INFO("[SDCardManager] Card type: " + cardInfo_.cardType);
            LOG_INFO("[SDCardManager] Total space: " +
                     std::to_string(cardInfo_.totalSpace / (1024 * 1024)) + " MB");
            LOG_INFO("[SDCardManager] Available space: " +
                     std::to_string(cardInfo_.availableSpace / (1024 * 1024)) + " MB");

            // Create base directory if needed
            if (!basePath_.empty() && basePath_ != "/")
            {
                createDirectory(basePath_);
            }

            // Create data and log directories
            createDirectory(basePath_ + "data");
            createDirectory(basePath_ + "logs");

            initialized_ = true;

            // Check for low space
            checkAndHandleLowSpace();

            return true;
        }

        LOG_WARNING("[SDCardManager] Mount attempt " + std::to_string(attempts) +
                    " failed, retrying in " + std::to_string(retryDelayMs) + "ms");

        std::this_thread::sleep_for(std::chrono::milliseconds(retryDelayMs));
    }

    lastError_ = "Failed to mount SD card after " + std::to_string(maxRetries) + " attempts";
    LOG_ERROR("[SDCardManager] " + lastError_);
    return false;
}

// Unmount SD card
bool SDCardManager::unmount()
{
    if (!initialized_)
    {
        return true;
    }

    LOG_INFO("[SDCardManager] Unmounting SD card");

    if (platformUnmount())
    {
        initialized_ = false;
        cardInfo_.mounted = false;
        LOG_INFO("[SDCardManager] SD card unmounted successfully");
        return true;
    }

    LOG_ERROR("[SDCardManager] Failed to unmount SD card");
    return false;
}

// Write probe request data
bool SDCardManager::writeData(const ProbeRequest &data, const std::string &filename)
{
    if (!initialized_)
    {
        LOG_ERROR("[SDCardManager] Not initialized - cannot write data");
        return false;
    }

    std::lock_guard<std::mutex> lock(operationMutex_);

    try
    {
        // Build CSV line
        std::ostringstream line;
        line << data.dataType << ","
             << data.timestamp << ","
             << data.source << ","
             << data.rssi << ","
             << data.packetLength << ","
             << data.macAddress << ","
             << data.payload;

        // Get full path
        std::string fullPath = getFullPath("data/" + filename);

        // Check if file exists and create with header if not
        if (!fileExists("data/" + filename))
        {
            if (!createDataFile("data/" + filename))
            {
                LOG_ERROR("[SDCardManager] Failed to create data file: " + filename);
                updateStats("write", false);
                return false;
            }
        }

        // Check file size and rotate if needed
        if (maxFileSize_ > 0)
        {
            size_t currentSize = getFileSize("data/" + filename);
            if (currentSize > maxFileSize_)
            {
                LOG_INFO("[SDCardManager] File size limit reached, rotating: " + filename);

                // Create new filename with timestamp
                auto now = std::chrono::system_clock::now();
                auto time_t = std::chrono::system_clock::to_time_t(now);
                std::tm *tm = std::localtime(&time_t);

                std::ostringstream newName;
                newName << "data/" << filename << "."
                        << std::put_time(tm, "%Y%m%d_%H%M%S");

                // Rename current file
                std::string oldPath = getFullPath("data/" + filename);
                std::string newPath = getFullPath(newName.str());

#ifdef ESP32_PLATFORM
                SD.rename(oldPath.c_str(), newPath.c_str());
#else
                std::rename(oldPath.c_str(), newPath.c_str());
#endif

                // Create new file with header
                createDataFile("data/" + filename);
            }
        }

        // Write data
        if (writeToFile(fullPath, line.str() + "\n", true))
        {
            updateStats("write", true, line.str().length());

            // Check for low space after write
            checkAndHandleLowSpace();

            return true;
        }
        else
        {
            lastError_ = "Failed to write data to file";
            LOG_ERROR("[SDCardManager] " + lastError_);
            updateStats("write", false);
            return false;
        }
    }
    catch (const std::exception &e)
    {
        lastError_ = std::string("Exception writing data: ") + e.what();
        LOG_ERROR("[SDCardManager] " + lastError_);
        updateStats("write", false);
        return false;
    }
}

// Write asset data
bool SDCardManager::writeAssetData(const AssetInfo &data)
{
    if (!initialized_)
    {
        LOG_ERROR("[SDCardManager] Not initialized - cannot write asset data");
        return false;
    }

    std::lock_guard<std::mutex> lock(operationMutex_);

    try
    {
        // Build CSV line
        std::ostringstream line;
        line << data.assetId << ","
             << data.locationName << ","
             << data.forestName << ","
             << data.latitude << ","
             << data.longitude << ","
             << std::fixed << std::setprecision(2) << data.remainingBatteryCapacity << ","
             << data.stateOfCharge << ","
             << data.runtimeToEmpty << ","
             << std::fixed << std::setprecision(2) << data.batteryVoltage << ","
             << std::fixed << std::setprecision(2) << data.batteryCurrent << ","
             << std::fixed << std::setprecision(2) << data.sdCardCapacity << ","
             << data.timeStamp;

        std::string filename = "asset_data.csv";
        std::string fullPath = getFullPath("data/" + filename);

        // Create file with header if it doesn't exist
        if (!fileExists("data/" + filename))
        {
            std::string header = ASSET_CSV_HEADER;
            if (!writeToFile(fullPath, header + "\n", false))
            {
                LOG_ERROR("[SDCardManager] Failed to create asset data file");
                updateStats("write", false);
                return false;
            }
        }

        // Write data
        if (writeToFile(fullPath, line.str() + "\n", true))
        {
            LOG_DEBUG("[SDCardManager] Asset data written successfully");
            updateStats("write", true, line.str().length());
            return true;
        }
        else
        {
            LOG_ERROR("[SDCardManager] Failed to write asset data");
            updateStats("write", false);
            return false;
        }
    }
    catch (const std::exception &e)
    {
        lastError_ = std::string("Exception writing asset data: ") + e.what();
        LOG_ERROR("[SDCardManager] " + lastError_);
        updateStats("write", false);
        return false;
    }
}

// Read file
std::vector<std::string> SDCardManager::readFile(const std::string &filename)
{
    std::vector<std::string> lines;

    if (!initialized_)
    {
        LOG_ERROR("[SDCardManager] Not initialized - cannot read file");
        return lines;
    }

    std::lock_guard<std::mutex> lock(operationMutex_);

    try
    {
        std::string fullPath = getFullPath(filename);

#ifdef ESP32_PLATFORM
        File file = SD.open(fullPath.c_str(), FILE_READ);
        if (!file)
        {
            LOG_ERROR("[SDCardManager] Failed to open file: " + filename);
            updateStats("read", false);
            return lines;
        }

        size_t totalBytes = 0;
        while (file.available())
        {
            String line = file.readStringUntil('\n');
            lines.push_back(line.c_str());
            totalBytes += line.length();
        }
        file.close();

        updateStats("read", true, totalBytes);
#else
        std::ifstream file(fullPath);
        if (!file.is_open())
        {
            LOG_ERROR("[SDCardManager] Failed to open file: " + filename);
            updateStats("read", false);
            return lines;
        }

        std::string line;
        size_t totalBytes = 0;
        while (std::getline(file, line))
        {
            lines.push_back(line);
            totalBytes += line.length();
        }
        file.close();

        updateStats("read", true, totalBytes);
#endif

        LOG_DEBUG("[SDCardManager] Read " + std::to_string(lines.size()) +
                  " lines from " + filename);

        return lines;
    }
    catch (const std::exception &e)
    {
        lastError_ = std::string("Exception reading file: ") + e.what();
        LOG_ERROR("[SDCardManager] " + lastError_);
        updateStats("read", false);
        return lines;
    }
}

// Check if file exists
bool SDCardManager::fileExists(const std::string &filename)
{
    if (!initialized_)
    {
        return false;
    }

    std::string fullPath = getFullPath(filename);

#ifdef ESP32_PLATFORM
    return SD.exists(fullPath.c_str());
#else
    struct stat buffer;
    return (stat(fullPath.c_str(), &buffer) == 0);
#endif
}

// Get file size
size_t SDCardManager::getFileSize(const std::string &filename)
{
    if (!initialized_)
    {
        return 0;
    }

    std::string fullPath = getFullPath(filename);

#ifdef ESP32_PLATFORM
    File file = SD.open(fullPath.c_str(), FILE_READ);
    if (!file)
    {
        return 0;
    }

    size_t size = file.size();
    file.close();
    return size;
#else
    struct stat buffer;
    if (stat(fullPath.c_str(), &buffer) != 0)
    {
        return 0;
    }
    return buffer.st_size;
#endif
}

// Get remaining capacity percentage
float SDCardManager::getRemainingCapacityPercent()
{
    if (!initialized_)
    {
        return 0.0f;
    }

    cardInfo_ = platformGetCardInfo();

    if (cardInfo_.totalSpace == 0)
    {
        return 0.0f;
    }

    return (static_cast<float>(cardInfo_.availableSpace) /
            static_cast<float>(cardInfo_.totalSpace)) *
           100.0f;
}

// Get card information
SDCardInfo SDCardManager::getCardInfo()
{
    if (initialized_)
    {
        cardInfo_ = platformGetCardInfo();
    }

    return cardInfo_;
}

// Get operation statistics
FileOperationStats SDCardManager::getOperationStats() const
{
    std::lock_guard<std::mutex> lock(statsMutex_);
    return stats_;
}

// Create directory
bool SDCardManager::createDirectory(const std::string &path)
{
    if (!initialized_)
    {
        LOG_ERROR("[SDCardManager] Not initialized - cannot create directory");
        return false;
    }

    std::string fullPath = getFullPath(path);

#ifdef ESP32_PLATFORM
    // SD library creates parent directories automatically
    if (SD.mkdir(fullPath.c_str()))
    {
        LOG_DEBUG("[SDCardManager] Created directory: " + path);
        return true;
    }
    else if (SD.exists(fullPath.c_str()))
    {
        // Directory already exists
        return true;
    }
    else
    {
        LOG_ERROR("[SDCardManager] Failed to create directory: " + path);
        return false;
    }
#else
    // Create directory with parent directories
    try
    {
        fs::create_directories(fullPath);
        LOG_DEBUG("[SDCardManager] Created directory: " + path);
        return true;
    }
    catch (const std::exception &e)
    {
        LOG_ERROR("[SDCardManager] Failed to create directory: " +
                  path + " - " + e.what());
        return false;
    }
#endif
}

// List files in directory
std::vector<std::string> SDCardManager::listFiles(const std::string &directory, bool recursive)
{
    std::vector<std::string> files;

    if (!initialized_)
    {
        LOG_ERROR("[SDCardManager] Not initialized - cannot list files");
        return files;
    }

    std::string fullPath = getFullPath(directory);

#ifdef ESP32_PLATFORM
    File root = SD.open(fullPath.c_str());
    if (!root || !root.isDirectory())
    {
        LOG_ERROR("[SDCardManager] Failed to open directory: " + directory);
        return files;
    }

    File file = root.openNextFile();
    while (file)
    {
        std::string name = file.name();

        if (file.isDirectory() && recursive)
        {
            // Recursively list subdirectory
            auto subFiles = listFiles(directory + "/" + name, true);
            files.insert(files.end(), subFiles.begin(), subFiles.end());
        }
        else if (!file.isDirectory())
        {
            files.push_back(directory + "/" + name);
        }

        file = root.openNextFile();
    }
#else
    try
    {
        if (recursive)
        {
            for (const auto &entry : fs::recursive_directory_iterator(fullPath))
            {
                if (entry.is_regular_file())
                {
                    files.push_back(entry.path().string());
                }
            }
        }
        else
        {
            for (const auto &entry : fs::directory_iterator(fullPath))
            {
                if (entry.is_regular_file())
                {
                    files.push_back(entry.path().filename().string());
                }
            }
        }
    }
    catch (const std::exception &e)
    {
        LOG_ERROR("[SDCardManager] Failed to list directory: " +
                  directory + " - " + e.what());
    }
#endif

    LOG_DEBUG("[SDCardManager] Found " + std::to_string(files.size()) +
              " files in " + directory);

    return files;
}

// Delete file
bool SDCardManager::deleteFile(const std::string &filename)
{
    if (!initialized_)
    {
        LOG_ERROR("[SDCardManager] Not initialized - cannot delete file");
        return false;
    }

    std::string fullPath = getFullPath(filename);

#ifdef ESP32_PLATFORM
    if (SD.remove(fullPath.c_str()))
    {
        LOG_DEBUG("[SDCardManager] Deleted file: " + filename);
        return true;
    }
    else
    {
        LOG_ERROR("[SDCardManager] Failed to delete file: " + filename);
        return false;
    }
#else
    try
    {
        if (fs::remove(fullPath))
        {
            LOG_DEBUG("[SDCardManager] Deleted file: " + filename);
            return true;
        }
        else
        {
            LOG_WARNING("[SDCardManager] File not found: " + filename);
            return true; // Not an error if file doesn't exist
        }
    }
    catch (const std::exception &e)
    {
        LOG_ERROR("[SDCardManager] Failed to delete file: " +
                  filename + " - " + e.what());
        return false;
    }
#endif
}

// Create data file with header
bool SDCardManager::createDataFile(const std::string &filename, const std::string &customHeader)
{
    if (!initialized_)
    {
        LOG_ERROR("[SDCardManager] Not initialized - cannot create file");
        return false;
    }

    std::string header = customHeader.empty() ? DEFAULT_CSV_HEADER : customHeader;
    std::string fullPath = getFullPath(filename);

    if (writeToFile(fullPath, header + "\n", false))
    {
        LOG_INFO("[SDCardManager] Created data file: " + filename);
        updateStats("create", true);
        return true;
    }
    else
    {
        LOG_ERROR("[SDCardManager] Failed to create data file: " + filename);
        updateStats("create", false);
        return false;
    }
}

// Write file atomically
bool SDCardManager::writeFileAtomic(const std::string &filename, const std::string &data)
{
    if (!initialized_)
    {
        LOG_ERROR("[SDCardManager] Not initialized - cannot write file");
        return false;
    }

    std::lock_guard<std::mutex> lock(operationMutex_);

    // Write to temporary file first
    std::string tempFile = filename + ".tmp";
    std::string fullTempPath = getFullPath(tempFile);
    std::string fullPath = getFullPath(filename);

    // Write data to temp file
    if (!writeToFile(fullTempPath, data, false))
    {
        LOG_ERROR("[SDCardManager] Failed to write temporary file");
        return false;
    }

    // Delete original file if it exists
    if (fileExists(filename))
    {
        deleteFile(filename);
    }

    // Rename temp file to final name
#ifdef ESP32_PLATFORM
    if (SD.rename(fullTempPath.c_str(), fullPath.c_str()))
    {
        LOG_DEBUG("[SDCardManager] Atomic write completed: " + filename);
        return true;
    }
    else
    {
        LOG_ERROR("[SDCardManager] Failed to rename temporary file");
        deleteFile(tempFile); // Clean up temp file
        return false;
    }
#else
    try
    {
        fs::rename(fullTempPath, fullPath);
        LOG_DEBUG("[SDCardManager] Atomic write completed: " + filename);
        return true;
    }
    catch (const std::exception &e)
    {
        LOG_ERROR("[SDCardManager] Failed to rename temporary file: " + e.what());
        deleteFile(tempFile); // Clean up temp file
        return false;
    }
#endif
}

// Create backup
bool SDCardManager::createBackup(const std::string &filename, const std::string &backupSuffix)
{
    if (!initialized_ || !fileExists(filename))
    {
        LOG_ERROR("[SDCardManager] Cannot backup - file doesn't exist: " + filename);
        return false;
    }

    std::string backupName = filename + backupSuffix;
    std::string content = readFromFile(getFullPath(filename));

    if (content.empty())
    {
        LOG_ERROR("[SDCardManager] Failed to read file for backup: " + filename);
        return false;
    }

    if (writeToFile(getFullPath(backupName), content, false))
    {
        LOG_INFO("[SDCardManager] Created backup: " + backupName);
        return true;
    }
    else
    {
        LOG_ERROR("[SDCardManager] Failed to create backup: " + backupName);
        return false;
    }
}

// Check if sufficient space available
bool SDCardManager::hasSufficientSpace(size_t requiredBytes)
{
    if (!initialized_)
    {
        return false;
    }

    cardInfo_ = platformGetCardInfo();
    return (cardInfo_.availableSpace >= requiredBytes);
}

// Get free space
uint64_t SDCardManager::getFreeSpace()
{
    if (!initialized_)
    {
        return 0;
    }

    cardInfo_ = platformGetCardInfo();
    return cardInfo_.availableSpace;
}

// Get used space
uint64_t SDCardManager::getUsedSpace()
{
    if (!initialized_)
    {
        return 0;
    }

    cardInfo_ = platformGetCardInfo();
    return cardInfo_.usedSpace;
}

// Clean up old files
int SDCardManager::cleanupOldFiles(float targetFreePercent, const std::string &deletePattern)
{
    if (!initialized_)
    {
        LOG_ERROR("[SDCardManager] Not initialized - cannot cleanup files");
        return 0;
    }

    LOG_INFO("[SDCardManager] Starting cleanup to achieve " +
             std::to_string(targetFreePercent) + "% free space");

    int deletedCount = 0;

    // Get current free space percentage
    float currentFreePercent = getRemainingCapacityPercent();

    if (currentFreePercent >= targetFreePercent)
    {
        LOG_INFO("[SDCardManager] Sufficient space available, no cleanup needed");
        return 0;
    }

    // Get list of data files
    auto files = listFiles("data", false);

    // Filter by pattern and sort by modification time (oldest first)
    std::vector<std::pair<std::string, uint64_t>> fileList;

    for (const auto &file : files)
    {
        // Simple pattern matching (ends with pattern)
        if (file.find(deletePattern.substr(1)) != std::string::npos)
        {
            uint64_t modTime = 0; // Would need platform-specific implementation
            fileList.push_back({file, modTime});
        }
    }

    // Sort by modification time (oldest first)
    std::sort(fileList.begin(), fileList.end(),
              [](const auto &a, const auto &b)
              { return a.second < b.second; });

    // Delete files until target is reached
    for (const auto &filePair : fileList)
    {
        if (getRemainingCapacityPercent() >= targetFreePercent)
        {
            break;
        }

        if (deleteFile(filePair.first))
        {
            deletedCount++;
            LOG_INFO("[SDCardManager] Deleted old file: " + filePair.first);
        }
    }

    LOG_INFO("[SDCardManager] Cleanup complete - deleted " +
             std::to_string(deletedCount) + " files");

    return deletedCount;
}

// Perform health check
bool SDCardManager::performHealthCheck()
{
    if (!initialized_)
    {
        LOG_ERROR("[SDCardManager] Not initialized - cannot perform health check");
        return false;
    }

    LOG_DEBUG("[SDCardManager] Performing health check");

    bool healthy = true;

    // Test write operation
    std::string testFile = "test_health_check.tmp";
    std::string testData = "SD card health check test";

    if (!writeToFile(getFullPath(testFile), testData, false))
    {
        LOG_ERROR("[SDCardManager] Health check failed - cannot write");
        healthy = false;
    }
    else
    {
        // Test read operation
        std::string readData = readFromFile(getFullPath(testFile));
        if (readData != testData)
        {
            LOG_ERROR("[SDCardManager] Health check failed - read mismatch");
            healthy = false;
        }

        // Clean up test file
        deleteFile(testFile);
    }

    // Check card info
    cardInfo_ = platformGetCardInfo();
    if (!cardInfo_.mounted)
    {
        LOG_ERROR("[SDCardManager] Health check failed - card not mounted");
        healthy = false;
    }

    if (healthy)
    {
        LOG_INFO("[SDCardManager] Health check passed");
    }
    else
    {
        LOG_WARNING("[SDCardManager] Health check detected issues");
    }

    return healthy;
}

// Private helper methods

// Write to file
bool SDCardManager::writeToFile(const std::string &fullPath, const std::string &data, bool append)
{
#ifdef ESP32_PLATFORM
    File file = SD.open(fullPath.c_str(), append ? FILE_APPEND : FILE_WRITE);
    if (!file)
    {
        lastError_ = "Failed to open file for writing: " + fullPath;
        return false;
    }

    size_t written = file.print(data.c_str());
    file.close();

    return (written == data.length());
#else
    std::ios_base::openmode mode = std::ios::out;
    if (append)
    {
        mode |= std::ios::app;
    }

    std::ofstream file(fullPath, mode);
    if (!file.is_open())
    {
        lastError_ = "Failed to open file for writing: " + fullPath;
        return false;
    }

    file << data;
    file.close();

    return file.good();
#endif
}

// Read from file
std::string SDCardManager::readFromFile(const std::string &fullPath)
{
#ifdef ESP32_PLATFORM
    File file = SD.open(fullPath.c_str(), FILE_READ);
    if (!file)
    {
        return "";
    }

    String content = file.readString();
    file.close();

    return std::string(content.c_str());
#else
    std::ifstream file(fullPath);
    if (!file.is_open())
    {
        return "";
    }

    std::stringstream buffer;
    buffer << file.rdbuf();
    file.close();

    return buffer.str();
#endif
}

// Get full path
std::string SDCardManager::getFullPath(const std::string &relativePath)
{
    if (relativePath.empty() || relativePath[0] == '/')
    {
        return relativePath;
    }

    return basePath_ + relativePath;
}

// Sanitize filename
std::string SDCardManager::sanitizeFilename(const std::string &filename)
{
    std::string sanitized = filename;

    // Replace invalid characters
    std::replace(sanitized.begin(), sanitized.end(), ':', '_');
    std::replace(sanitized.begin(), sanitized.end(), '*', '_');
    std::replace(sanitized.begin(), sanitized.end(), '?', '_');
    std::replace(sanitized.begin(), sanitized.end(), '"', '_');
    std::replace(sanitized.begin(), sanitized.end(), '<', '_');
    std::replace(sanitized.begin(), sanitized.end(), '>', '_');
    std::replace(sanitized.begin(), sanitized.end(), '|', '_');

    // Truncate if too long
    if (sanitized.length() > MAX_FILENAME_LENGTH)
    {
        sanitized = sanitized.substr(0, MAX_FILENAME_LENGTH);
    }

    return sanitized;
}

// Update statistics
void SDCardManager::updateStats(const std::string &operation, bool success, size_t bytes)
{
    std::lock_guard<std::mutex> lock(statsMutex_);

    if (operation == "write")
    {
        if (success)
        {
            stats_.filesWritten++;
            stats_.bytesWritten += bytes;
        }
        else
        {
            stats_.writeErrors++;
        }
    }
    else if (operation == "read")
    {
        if (success)
        {
            stats_.filesRead++;
            stats_.bytesRead += bytes;
        }
        else
        {
            stats_.readErrors++;
        }
    }
    else if (operation == "create")
    {
        if (success)
        {
            stats_.filesCreated++;
        }
    }

    stats_.lastOperationTime = std::chrono::duration_cast<std::chrono::seconds>(
                                   std::chrono::system_clock::now().time_since_epoch())
                                   .count();
}

// Check and handle low space
void SDCardManager::checkAndHandleLowSpace()
{
    float freePercent = getRemainingCapacityPercent();

    if (freePercent < lowSpaceThreshold_)
    {
        LOG_WARNING("[SDCardManager] Low space warning - " +
                    std::to_string(freePercent) + "% free");

        if (autoCleanupEnabled_)
        {
            LOG_INFO("[SDCardManager] Triggering automatic cleanup");
            cleanupOldFiles(lowSpaceThreshold_ * 2); // Try to free up to 2x threshold
        }
    }
}

// Platform-specific implementations

// Platform mount
bool SDCardManager::platformMount()
{
#ifdef ESP32_PLATFORM
    // Initialize SPI for SD card
    SPI.begin(Config::Pins::SD_SCK, Config::Pins::SD_MISO,
              Config::Pins::SD_MOSI, Config::Pins::SD_CS);

    // Mount SD card
    if (!SD.begin(Config::Pins::SD_CS))
    {
        lastError_ = "SD card mount failed - check card insertion and wiring";
        return false;
    }

    platformHandle_ = (void *)1; // Non-null to indicate mounted
    return true;
#else
    // Development platform - check if directory exists
    struct stat info;
    if (stat(basePath_.c_str(), &info) != 0)
    {
        // Try to create base directory
        try
        {
            fs::create_directories(basePath_);
            LOG_DEBUG("[SDCardManager] Created base directory: " + basePath_);
        }
        catch (const std::exception &e)
        {
            lastError_ = "Failed to create base directory: " + std::string(e.what());
            return false;
        }
    }

    platformHandle_ = (void *)1; // Non-null to indicate mounted
    return true;
#endif
}

// Platform unmount
bool SDCardManager::platformUnmount()
{
#ifdef ESP32_PLATFORM
    SD.end();
    platformHandle_ = nullptr;
    return true;
#else
    platformHandle_ = nullptr;
    return true;
#endif
}

// Platform get card info
SDCardInfo SDCardManager::platformGetCardInfo()
{
    SDCardInfo info;

#ifdef ESP32_PLATFORM
    if (!platformHandle_)
    {
        return info;
    }

    info.mounted = true;

    // Get card type
    uint8_t cardType = SD.cardType();
    switch (cardType)
    {
    case CARD_MMC:
        info.cardType = "MMC";
        break;
    case CARD_SD:
        info.cardType = "SDSC";
        break;
    case CARD_SDHC:
        info.cardType = "SDHC";
        break;
    case CARD_UNKNOWN:
    default:
        info.cardType = "Unknown";
        break;
    }

    // Get card size and usage
    info.totalSpace = SD.totalBytes();
    info.usedSpace = SD.usedBytes();
    info.availableSpace = info.totalSpace - info.usedSpace;

    if (info.totalSpace > 0)
    {
        info.capacityPercent = (static_cast<float>(info.usedSpace) /
                                static_cast<float>(info.totalSpace)) *
                               100.0f;
    }

    // File system type (ESP32 SD library uses FAT)
    info.fileSystem = "FAT32";

    // Sector information
    info.sectorSize = 512; // Standard SD sector size
    info.sectorCount = info.totalSpace / info.sectorSize;

#else
    // Development platform - use statvfs for file system info
    if (!platformHandle_)
    {
        return info;
    }

    struct statvfs stat;
    if (statvfs(basePath_.c_str(), &stat) == 0)
    {
        info.mounted = true;
        info.cardType = "Virtual";
        info.fileSystem = "Native";

        info.totalSpace = stat.f_blocks * stat.f_frsize;
        info.availableSpace = stat.f_bavail * stat.f_frsize;
        info.usedSpace = info.totalSpace - info.availableSpace;

        if (info.totalSpace > 0)
        {
            info.capacityPercent = (static_cast<float>(info.usedSpace) /
                                    static_cast<float>(info.totalSpace)) *
                                   100.0f;
        }

        info.sectorSize = stat.f_frsize;
        info.sectorCount = stat.f_blocks;
    }
#endif

    return info;
}

// Verify file integrity (basic implementation)
bool SDCardManager::verifyFileIntegrity(const std::string &filename)
{
    if (!initialized_ || !fileExists(filename))
    {
        return false;
    }

    // Basic check - try to read the file
    auto lines = readFile(filename);

    // Check if we got any data
    if (lines.empty())
    {
        LOG_WARNING("[SDCardManager] File appears to be empty: " + filename);
        return false;
    }

    // For CSV files, check header
    if (filename.find(".csv") != std::string::npos && !lines.empty())
    {
        // Check if first line looks like a header (contains commas)
        if (lines[0].find(',') == std::string::npos)
        {
            LOG_WARNING("[SDCardManager] CSV file missing proper header: " + filename);
            return false;
        }
    }

    LOG_DEBUG("[SDCardManager] File integrity check passed: " + filename);
    return true;
}

// Attempt error recovery
bool SDCardManager::attemptErrorRecovery()
{
    LOG_WARNING("[SDCardManager] Attempting error recovery");

    // First, try to unmount and remount
    if (initialized_)
    {
        unmount();
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    }

    // Try to reinitialize
    if (initialize(5, 500)) // Fewer retries for recovery
    {
        LOG_INFO("[SDCardManager] Error recovery successful - card remounted");

        // Perform health check
        if (performHealthCheck())
        {
            LOG_INFO("[SDCardManager] Card appears healthy after recovery");
            return true;
        }
        else
        {
            LOG_WARNING("[SDCardManager] Card mounted but health check failed");
            return false;
        }
    }

    LOG_ERROR("[SDCardManager] Error recovery failed");
    return false;
}