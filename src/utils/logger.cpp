// src/utils/logger.cpp

#include "utils/logger.h"
#include "interfaces.h"
#include <cstdio>
#include <cstdarg>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <filesystem>
#include <algorithm>

// Static member initialization
std::shared_ptr<Logger> Logger::globalLogger_ = nullptr;
std::mutex Logger::globalLoggerMutex_;

Logger::Logger(std::shared_ptr<ITimeManager> timeManager,
               std::shared_ptr<IDataStorage> storage)
    : timeManager_(timeManager), storage_(storage), consoleLogLevel_(LogLevel::DEBUG), fileLogLevel_(LogLevel::INFO), defaultDestination_(LogDestination::BOTH), logDirectory_("/logs"), maxFileSize_(1024 * 1024) // 1MB default
      ,
      maxBackupFiles_(5), asyncLogging_(true), shutdownRequested_(false), currentFileSize_(0), totalLogCount_(0)
{
}

Logger::~Logger()
{
    shutdown();
}

bool Logger::initialize(const std::string &logDirectory)
{
    logDirectory_ = logDirectory;

    try
    {
        // Create log directory if it doesn't exist
        if (storage_ && !storage_->fileExists(logDirectory_))
        {
            std::filesystem::create_directories(logDirectory_);
        }

        // Create initial log file
        if (!createNewLogFile())
        {
            printf("Logger: Failed to create initial log file\n");
            return false;
        }

        // Start async logging thread if enabled
        if (asyncLogging_)
        {
            shutdownRequested_.store(false);
            loggerThread_ = std::thread(&Logger::loggerThreadFunction, this);
        }

        // Log system startup
        logSystemStartup("Logger system initialized");

        printf("Logger: Initialized successfully\n");
        printf("Logger: Console level: %s, File level: %s\n",
               logLevelToString(consoleLogLevel_).c_str(),
               logLevelToString(fileLogLevel_).c_str());
        printf("Logger: Log directory: %s\n", logDirectory_.c_str());
        printf("Logger: Current log file: %s\n", currentLogFile_.c_str());

        return true;
    }
    catch (const std::exception &e)
    {
        printf("Logger: Initialization failed: %s\n", e.what());
        return false;
    }
}

void Logger::shutdown()
{
    if (shutdownRequested_.load())
    {
        return; // Already shutting down
    }

    logSystemShutdown();

    shutdownRequested_.store(true);

    // Wake up logger thread and wait for it to finish
    if (asyncLogging_ && loggerThread_.joinable())
    {
        queueCondition_.notify_all();
        loggerThread_.join();
    }

    // Flush and close file
    if (logFileStream_.is_open())
    {
        logFileStream_.flush();
        logFileStream_.close();
    }

    printf("Logger: Shutdown completed\n");
}

void Logger::setAsyncLogging(bool enabled)
{
    if (enabled == asyncLogging_)
    {
        return;
    }

    if (asyncLogging_ && loggerThread_.joinable())
    {
        // Stop current async thread
        shutdownRequested_.store(true);
        queueCondition_.notify_all();
        loggerThread_.join();
        shutdownRequested_.store(false);
    }

    asyncLogging_ = enabled;

    if (asyncLogging_)
    {
        // Start new async thread
        loggerThread_ = std::thread(&Logger::loggerThreadFunction, this);
    }
}

void Logger::debug(const std::string &component, const std::string &message,
                   LogDestination destination)
{
    LogEntry entry(LogLevel::DEBUG, component, message);
    entry.timestamp = getCurrentTimestamp();
    log(entry, destination);
}

void Logger::info(const std::string &component, const std::string &message,
                  LogDestination destination)
{
    LogEntry entry(LogLevel::INFO, component, message);
    entry.timestamp = getCurrentTimestamp();
    log(entry, destination);
}

void Logger::warning(const std::string &component, const std::string &message,
                     LogDestination destination)
{
    LogEntry entry(LogLevel::WARNING, component, message);
    entry.timestamp = getCurrentTimestamp();
    log(entry, destination);
}

void Logger::error(const std::string &component, const std::string &message,
                   LogDestination destination)
{
    LogEntry entry(LogLevel::ERROR, component, message);
    entry.timestamp = getCurrentTimestamp();
    log(entry, destination);
}

void Logger::critical(const std::string &component, const std::string &message,
                      LogDestination destination)
{
    LogEntry entry(LogLevel::CRITICAL, component, message);
    entry.timestamp = getCurrentTimestamp();
    log(entry, destination);
}

void Logger::logf(LogLevel level, const std::string &component, const char *format, ...)
{
    char buffer[1024];
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);

    LogEntry entry(level, component, std::string(buffer));
    entry.timestamp = getCurrentTimestamp();
    log(entry, defaultDestination_);
}

void Logger::logSystemStartup(const std::string &deviceInfo)
{
    std::string separator(80, '=');

    info("SYSTEM", separator);
    info("SYSTEM", "FORESTRY RESEARCH DEVICE STARTUP");
    info("SYSTEM", deviceInfo);
    info("SYSTEM", "Timestamp: " + getCurrentTimestamp());
    info("SYSTEM", separator);
}

void Logger::logSystemShutdown()
{
    std::string separator(80, '=');

    info("SYSTEM", separator);
    info("SYSTEM", "FORESTRY RESEARCH DEVICE SHUTDOWN");
    info("SYSTEM", "Total log entries: " + std::to_string(totalLogCount_.load()));
    info("SYSTEM", "Timestamp: " + getCurrentTimestamp());
    info("SYSTEM", separator);
}

void Logger::flush()
{
    if (asyncLogging_)
    {
        // For async logging, we need to wait for queue to empty
        std::unique_lock<std::mutex> lock(queueMutex_);
        while (!logQueue_.empty() && !shutdownRequested_.load())
        {
            queueCondition_.notify_all();
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }

    // Flush file stream
    std::lock_guard<std::mutex> fileLock(fileMutex_);
    if (logFileStream_.is_open())
    {
        logFileStream_.flush();
    }
}

void Logger::log(const LogEntry &entry, LogDestination destination)
{
    if (asyncLogging_)
    {
        // Add to queue for background processing
        std::lock_guard<std::mutex> lock(queueMutex_);
        logQueue_.push(entry);
        queueCondition_.notify_one();
    }
    else
    {
        // Process immediately
        if ((destination & LogDestination::CONSOLE_ONLY) &&
            entry.level >= consoleLogLevel_)
        {
            writeToConsole(entry);
        }

        if ((destination & LogDestination::FILE_ONLY) &&
            entry.level >= fileLogLevel_)
        {
            writeToFile(entry);
        }
    }

    totalLogCount_++;
}

std::string Logger::formatLogEntry(const LogEntry &entry, bool includeColors)
{
    std::ostringstream oss;

    if (includeColors)
    {
        oss << getColorForLevel(entry.level);
    }

    oss << "[" << entry.timestamp << "] "
        << "[" << std::setw(8) << logLevelToString(entry.level) << "] "
        << "[" << std::setw(12) << entry.component << "] "
        << entry.message;

    if (!entry.filename.empty() && entry.lineNumber > 0)
    {
        oss << " (" << entry.filename << ":" << entry.lineNumber << ")";
    }

    if (includeColors)
    {
        oss << "\033[0m"; // Reset color
    }

    return oss.str();
}

void Logger::writeToConsole(const LogEntry &entry)
{
    std::string formatted = formatLogEntry(entry, true); // Include colors for console
    printf("%s\n", formatted.c_str());
    fflush(stdout);
}

void Logger::writeToFile(const LogEntry &entry)
{
    std::lock_guard<std::mutex> lock(fileMutex_);

    if (!logFileStream_.is_open())
    {
        return;
    }

    std::string formatted = formatLogEntry(entry, false); // No colors for file
    logFileStream_ << formatted << std::endl;

    currentFileSize_ += formatted.length() + 1; // +1 for newline

    // Check if rotation is needed
    if (currentFileSize_ >= maxFileSize_)
    {
        rotateLogFileIfNeeded();
    }
}

void Logger::rotateLogFileIfNeeded()
{
    if (currentFileSize_ < maxFileSize_)
    {
        return;
    }

    logFileStream_.close();

    try
    {
        // Rotate existing backup files
        for (int i = maxBackupFiles_ - 1; i > 0; --i)
        {
            std::string oldFile = currentLogFile_ + "." + std::to_string(i);
            std::string newFile = currentLogFile_ + "." + std::to_string(i + 1);

            if (std::filesystem::exists(oldFile))
            {
                if (i == maxBackupFiles_ - 1)
                {
                    std::filesystem::remove(newFile); // Remove oldest backup
                }
                std::filesystem::rename(oldFile, newFile);
            }
        }

        // Move current log to .1
        std::string backupFile = currentLogFile_ + ".1";
        std::filesystem::rename(currentLogFile_, backupFile);

        // Create new log file
        createNewLogFile();
    }
    catch (const std::exception &e)
    {
        printf("Logger: Failed to rotate log file: %s\n", e.what());
        // Try to reopen current file
        logFileStream_.open(currentLogFile_, std::ios::app);
    }
}

bool Logger::createNewLogFile()
{
    std::string timestamp = getCurrentTimestamp();
    // Replace colons and spaces with underscores for filename
    std::replace(timestamp.begin(), timestamp.end(), ':', '_');
    std::replace(timestamp.begin(), timestamp.end(), ' ', '_');

    currentLogFile_ = logDirectory_ + "/forestry_log_" + timestamp + ".txt";
    currentFileSize_ = 0;

    std::lock_guard<std::mutex> lock(fileMutex_);
    logFileStream_.open(currentLogFile_, std::ios::app);

    if (!logFileStream_.is_open())
    {
        printf("Logger: Failed to create log file: %s\n", currentLogFile_.c_str());
        return false;
    }

    // Write file header
    logFileStream_ << "# Forestry Research Device Log File" << std::endl;
    logFileStream_ << "# Created: " << getCurrentTimestamp() << std::endl;
    logFileStream_ << "# Format: [Timestamp] [Level] [Component] Message" << std::endl;
    logFileStream_ << std::string(80, '=') << std::endl;

    return true;
}

void Logger::loggerThreadFunction()
{
    while (!shutdownRequested_.load())
    {
        std::unique_lock<std::mutex> lock(queueMutex_);

        // Wait for log entries or shutdown signal
        queueCondition_.wait(lock, [this]
                             { return !logQueue_.empty() || shutdownRequested_.load(); });

        // Process all queued entries
        while (!logQueue_.empty())
        {
            LogEntry entry = logQueue_.front();
            logQueue_.pop();
            lock.unlock();

            // Process the log entry
            if (entry.level >= consoleLogLevel_)
            {
                writeToConsole(entry);
            }

            if (entry.level >= fileLogLevel_)
            {
                writeToFile(entry);
            }

            lock.lock();
        }
    }

    // Process any remaining entries before shutdown
    std::lock_guard<std::mutex> lock(queueMutex_);
    while (!logQueue_.empty())
    {
        LogEntry entry = logQueue_.front();
        logQueue_.pop();

        if (entry.level >= consoleLogLevel_)
        {
            writeToConsole(entry);
        }

        if (entry.level >= fileLogLevel_)
        {
            writeToFile(entry);
        }
    }
}

std::string Logger::getCurrentTimestamp()
{
    if (timeManager_)
    {
        return timeManager_->getCurrentDateTime();
    }
    else
    {
        // Fallback to system time
        auto now = std::chrono::system_clock::now();
        auto time_t = std::chrono::system_clock::to_time_t(now);
        auto tm = *std::gmtime(&time_t);

        char buffer[30];
        std::strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &tm);
        return std::string(buffer);
    }
}

std::string Logger::logLevelToString(LogLevel level)
{
    switch (level)
    {
    case LogLevel::DEBUG:
        return "DEBUG";
    case LogLevel::INFO:
        return "INFO";
    case LogLevel::WARNING:
        return "WARNING";
    case LogLevel::ERROR:
        return "ERROR";
    case LogLevel::CRITICAL:
        return "CRITICAL";
    default:
        return "UNKNOWN";
    }
}

std::string Logger::getColorForLevel(LogLevel level)
{
    switch (level)
    {
    case LogLevel::DEBUG:
        return "\033[36m"; // Cyan
    case LogLevel::INFO:
        return "\033[32m"; // Green
    case LogLevel::WARNING:
        return "\033[33m"; // Yellow
    case LogLevel::ERROR:
        return "\033[31m"; // Red
    case LogLevel::CRITICAL:
        return "\033[35m"; // Magenta
    default:
        return "\033[0m"; // Reset
    }
}

// Static methods for global logger
void Logger::setGlobalLogger(std::shared_ptr<Logger> logger)
{
    std::lock_guard<std::mutex> lock(globalLoggerMutex_);
    globalLogger_ = logger;
}

std::shared_ptr<Logger> Logger::getGlobalLogger()
{
    std::lock_guard<std::mutex> lock(globalLoggerMutex_);
    return globalLogger_;
}