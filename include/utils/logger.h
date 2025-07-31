#pragma once

#include <string>
#include <memory>
#include <fstream>
#include <mutex>
#include <atomic>
#include <queue>
#include <thread>
#include <condition_variable>

// Forward declarations
class ITimeManager;
class IDataStorage;

/**
 * @brief Log levels for filtering and categorization
 */
enum class LogLevel
{
    DEBUG = 0,   ///< Detailed debug information
    INFO = 1,    ///< General information messages
    WARNING = 2, ///< Warning messages
    ERROR = 3,   ///< Error messages
    CRITICAL = 4 ///< Critical system errors
};

/**
 * @brief Log destinations for output control
 */
enum class LogDestination
{
    CONSOLE_ONLY = 1, ///< Output only to console/serial
    FILE_ONLY = 2,    ///< Output only to SD card file
    BOTH = 3          ///< Output to both console and file
};

/**
 * @brief Individual log entry structure
 */
struct LogEntry
{
    LogLevel level;
    std::string timestamp;
    std::string component; ///< Component/module name (e.g., "WiFiScanner", "BatteryMonitor")
    std::string message;
    std::string filename; ///< Source file name (optional)
    int lineNumber;       ///< Source line number (optional)

    LogEntry(LogLevel lvl, const std::string &comp, const std::string &msg,
             const std::string &file = "", int line = 0)
        : level(lvl), component(comp), message(msg), filename(file), lineNumber(line) {}
};

/**
 * @brief Comprehensive logging system for the forestry research device
 *
 * Features:
 * - Multiple log levels (DEBUG, INFO, WARNING, ERROR, CRITICAL)
 * - Dual output (console + SD card)
 * - Automatic file rotation by size and time
 * - Thread-safe operation
 * - Asynchronous logging to prevent blocking
 * - Configurable formatting
 * - Component-based filtering
 */
class Logger
{
public:
    /**
     * @brief Constructor
     * @param timeManager Time manager for timestamps
     * @param storage Storage manager for file operations
     */
    Logger(std::shared_ptr<ITimeManager> timeManager,
           std::shared_ptr<IDataStorage> storage);

    /**
     * @brief Destructor - ensures all logs are flushed
     */
    ~Logger();

    /**
     * @brief Initialize the logging system
     * @param logDirectory Directory for log files (default: "/logs")
     * @return true if initialization successful
     */
    bool initialize(const std::string &logDirectory = "/logs");

    /**
     * @brief Shutdown the logging system gracefully
     */
    void shutdown();

    // Configuration methods
    /**
     * @brief Set minimum log level for console output
     * @param level Minimum level to display on console
     */
    void setConsoleLogLevel(LogLevel level) { consoleLogLevel_ = level; }

    /**
     * @brief Set minimum log level for file output
     * @param level Minimum level to write to file
     */
    void setFileLogLevel(LogLevel level) { fileLogLevel_ = level; }

    /**
     * @brief Set default log destination
     * @param destination Where to output logs
     */
    void setLogDestination(LogDestination destination) { defaultDestination_ = destination; }

    /**
     * @brief Enable/disable asynchronous logging
     * @param enabled If true, logs are queued and written by background thread
     */
    void setAsyncLogging(bool enabled);

    /**
     * @brief Set maximum log file size before rotation
     * @param sizeBytes Maximum file size in bytes (default: 1MB)
     */
    void setMaxFileSize(size_t sizeBytes) { maxFileSize_ = sizeBytes; }

    /**
     * @brief Set maximum number of rotated log files to keep
     * @param count Number of backup files (default: 5)
     */
    void setMaxBackupFiles(int count) { maxBackupFiles_ = count; }

    // Main logging methods
    /**
     * @brief Log a debug message
     * @param component Component name
     * @param message Log message
     * @param destination Override default destination
     */
    void debug(const std::string &component, const std::string &message,
               LogDestination destination = LogDestination::BOTH);

    /**
     * @brief Log an info message
     * @param component Component name
     * @param message Log message
     * @param destination Override default destination
     */
    void info(const std::string &component, const std::string &message,
              LogDestination destination = LogDestination::BOTH);

    /**
     * @brief Log a warning message
     * @param component Component name
     * @param message Log message
     * @param destination Override default destination
     */
    void warning(const std::string &component, const std::string &message,
                 LogDestination destination = LogDestination::BOTH);

    /**
     * @brief Log an error message
     * @param component Component name
     * @param message Log message
     * @param destination Override default destination
     */
    void error(const std::string &component, const std::string &message,
               LogDestination destination = LogDestination::BOTH);

    /**
     * @brief Log a critical error message
     * @param component Component name
     * @param message Log message
     * @param destination Override default destination
     */
    void critical(const std::string &component, const std::string &message,
                  LogDestination destination = LogDestination::BOTH);

    /**
     * @brief Log a formatted message with printf-style formatting
     * @param level Log level
     * @param component Component name
     * @param format Printf-style format string
     * @param ... Format arguments
     */
    void logf(LogLevel level, const std::string &component, const char *format, ...);

    /**
     * @brief Log system startup information
     * @param deviceInfo Device information to log
     */
    void logSystemStartup(const std::string &deviceInfo);

    /**
     * @brief Log system shutdown information
     */
    void logSystemShutdown();

    /**
     * @brief Force flush all pending log entries
     */
    void flush();

    /**
     * @brief Get current log file path
     * @return Path to current log file
     */
    std::string getCurrentLogFile() const { return currentLogFile_; }

    /**
     * @brief Get total number of log entries written
     * @return Total log entry count
     */
    size_t getTotalLogCount() const { return totalLogCount_.load(); }

    // Static convenience methods for global logger
    /**
     * @brief Set global logger instance
     * @param logger Shared pointer to logger instance
     */
    static void setGlobalLogger(std::shared_ptr<Logger> logger);

    /**
     * @brief Get global logger instance
     * @return Shared pointer to global logger (may be null)
     */
    static std::shared_ptr<Logger> getGlobalLogger();

private:
    // Dependencies
    std::shared_ptr<ITimeManager> timeManager_;
    std::shared_ptr<IDataStorage> storage_;

    // Configuration
    LogLevel consoleLogLevel_;
    LogLevel fileLogLevel_;
    LogDestination defaultDestination_;
    std::string logDirectory_;
    std::string currentLogFile_;
    size_t maxFileSize_;
    int maxBackupFiles_;

    // File management
    std::ofstream logFileStream_;
    mutable std::mutex fileMutex_;
    size_t currentFileSize_;

    // Asynchronous logging
    bool asyncLogging_;
    std::atomic<bool> shutdownRequested_;
    std::queue<LogEntry> logQueue_;
    std::mutex queueMutex_;
    std::condition_variable queueCondition_;
    std::thread loggerThread_;

    // Statistics
    std::atomic<size_t> totalLogCount_;

    // Internal methods
    /**
     * @brief Core logging method
     * @param entry Log entry to process
     * @param destination Where to output the log
     */
    void log(const LogEntry &entry, LogDestination destination);

    /**
     * @brief Format log entry for output
     * @param entry Log entry to format
     * @param includeColors Whether to include ANSI color codes
     * @return Formatted log string
     */
    std::string formatLogEntry(const LogEntry &entry, bool includeColors = false);

    /**
     * @brief Write log entry to console
     * @param entry Log entry to write
     */
    void writeToConsole(const LogEntry &entry);

    /**
     * @brief Write log entry to file
     * @param entry Log entry to write
     */
    void writeToFile(const LogEntry &entry);

    /**
     * @brief Check if log file needs rotation and perform if necessary
     */
    void rotateLogFileIfNeeded();

    /**
     * @brief Create new log file with timestamp
     * @return true if file created successfully
     */
    bool createNewLogFile();

    /**
     * @brief Background thread function for asynchronous logging
     */
    void loggerThreadFunction();

    /**
     * @brief Get timestamp string from time manager
     * @return Current timestamp string
     */
    std::string getCurrentTimestamp();

    /**
     * @brief Convert log level to string
     * @param level Log level to convert
     * @return String representation of log level
     */
    static std::string logLevelToString(LogLevel level);

    /**
     * @brief Get ANSI color code for log level
     * @param level Log level
     * @return ANSI color code string
     */
    static std::string getColorForLevel(LogLevel level);

    // Global logger instance
    static std::shared_ptr<Logger> globalLogger_;
    static std::mutex globalLoggerMutex_;
};

// Convenience macros for easier logging
#define LOG_DEBUG(component, message)            \
    if (auto logger = Logger::getGlobalLogger()) \
    {                                            \
        logger->debug(component, message);       \
    }

#define LOG_INFO(component, message)             \
    if (auto logger = Logger::getGlobalLogger()) \
    {                                            \
        logger->info(component, message);        \
    }

#define LOG_WARNING(component, message)          \
    if (auto logger = Logger::getGlobalLogger()) \
    {                                            \
        logger->warning(component, message);     \
    }

#define LOG_ERROR(component, message)            \
    if (auto logger = Logger::getGlobalLogger()) \
    {                                            \
        logger->error(component, message);       \
    }

#define LOG_CRITICAL(component, message)         \
    if (auto logger = Logger::getGlobalLogger()) \
    {                                            \
        logger->critical(component, message);    \
    }

// Formatted logging macros
#define LOGF_DEBUG(component, format, ...)                               \
    if (auto logger = Logger::getGlobalLogger())                         \
    {                                                                    \
        logger->logf(LogLevel::DEBUG, component, format, ##__VA_ARGS__); \
    }

#define LOGF_INFO(component, format, ...)                               \
    if (auto logger = Logger::getGlobalLogger())                        \
    {                                                                   \
        logger->logf(LogLevel::INFO, component, format, ##__VA_ARGS__); \
    }

#define LOGF_ERROR(component, format, ...)                               \
    if (auto logger = Logger::getGlobalLogger())                         \
    {                                                                    \
        logger->logf(LogLevel::ERROR, component, format, ##__VA_ARGS__); \
    }