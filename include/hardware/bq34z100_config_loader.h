/**
 * @file bq34z100_config_loader.h
 * @brief TI BQ34Z100 Golden Image File Parser and Configuration Loader
 *
 * This module provides cross-platform functionality to parse and execute
 * TI Battery Management Studio generated .bq.fs and .df.fs files for
 * programming BQ34Z100 battery fuel gauge configurations.
 *
 * Supports ESP32 (Forestry Research Device), Raspberry Pi, and STM32L476 (BMS) platforms
 * with automatic platform detection and appropriate I2C/file system drivers.
 *
 * @author Forestry Research Device Team
 * @date 2025-01-31
 * @version 1.0.0
 */

#ifndef BQ34Z100_CONFIG_LOADER_H
#define BQ34Z100_CONFIG_LOADER_H

#include <stdint.h>
#include <stdbool.h>
#include <string>
#include <vector>
#include <memory>

// Platform detection (aligned with project standards)
#ifdef ESP32_PLATFORM
#include "driver/i2c.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include <SD.h>
#include <SPI.h>
#define PLATFORM_ESP32
#elif defined(RASPBERRY_PI_PLATFORM)
// Raspberry Pi includes
#include <linux/i2c-dev.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <unistd.h>
#include <fstream>
#define PLATFORM_RASPBERRY_PI
#elif defined(STM32L476xx)
#include "stm32l4xx_hal.h"
#include "fatfs.h"
#define PLATFORM_STM32
#else
#define PLATFORM_DEVELOPMENT
#include <iostream>
#include <fstream>
#endif

// Forward declarations
class BQ34Z100BatteryMonitor;

namespace ForestryDevice
{
    namespace BatteryManagement
    {

        /**
         * @brief BQ34Z100 I2C register addresses
         */
        namespace BQ34Z100Registers
        {
            constexpr uint8_t CONTROL_STATUS = 0x00;
            constexpr uint8_t ACCUMULATED_CAPACITY = 0x02;
            constexpr uint8_t TEMPERATURE = 0x08;
            constexpr uint8_t VOLTAGE = 0x09;
            constexpr uint8_t BATTERY_STATUS = 0x0A;
            constexpr uint8_t CURRENT = 0x0C;
            constexpr uint8_t REMAINING_CAPACITY = 0x10;
            constexpr uint8_t FULL_CHARGE_CAPACITY = 0x12;
            constexpr uint8_t STATE_OF_CHARGE = 0x2C;
            constexpr uint8_t STATE_OF_HEALTH = 0x2E;

            // Flash access registers
            constexpr uint8_t FLASH_CONTROL = 0x61;
            constexpr uint8_t FLASH_CLASS = 0x3E;
            constexpr uint8_t FLASH_BLOCK = 0x3F;
            constexpr uint8_t FLASH_DATA_START = 0x40;
            constexpr uint8_t FLASH_CHECKSUM = 0x60;
            constexpr uint8_t FLASH_BLOCK_DATA_CONTROL = 0x61;
        }

        /**
         * @brief TI .fs file command types parsed from golden image files
         */
        enum class FSCommandType : uint8_t
        {
            WRITE_REGISTER = 'W', // Write data to register
            READ_REGISTER = 'C',  // Check/read register value
            DELAY = 'X',          // Execute delay in milliseconds
            COMMENT = ';',        // Comment line (ignored)
            UNKNOWN = 0xFF
        };

        /**
         * @brief Structure representing a single .fs file command
         */
        struct FSCommand
        {
            FSCommandType type;        // Command type (W, C, X, ;)
            uint8_t device_address;    // I2C device address (0xAA, 0x16)
            uint8_t register_address;  // Target register address
            std::vector<uint8_t> data; // Data payload for write commands
            uint8_t expected_value;    // Expected value for check commands
            uint32_t delay_ms;         // Delay duration for X commands
            std::string comment;       // Comment text for documentation

            FSCommand() : type(FSCommandType::UNKNOWN), device_address(0),
                          register_address(0), expected_value(0), delay_ms(0) {}
        };

        /**
         * @brief Golden image file metadata and statistics
         */
        struct GoldenImageInfo
        {
            std::string filename;       // Source filename
            std::string file_type;      // ".bq.fs" or ".df.fs"
            size_t total_commands;      // Total number of commands
            size_t write_commands;      // Number of write operations
            size_t read_commands;       // Number of read/check operations
            size_t delay_commands;      // Number of delay operations
            size_t comment_lines;       // Number of comment lines
            uint32_t estimated_time_ms; // Estimated execution time
            bool has_unseal_sequence;   // Contains device unseal commands
            bool has_rom_mode;          // Contains ROM mode entry
            bool has_flash_programming; // Contains flash data programming

            GoldenImageInfo() : total_commands(0), write_commands(0), read_commands(0),
                                delay_commands(0), comment_lines(0), estimated_time_ms(0),
                                has_unseal_sequence(false), has_rom_mode(false),
                                has_flash_programming(false) {}
        };

        /**
         * @brief Configuration loader execution status
         */
        enum class LoaderStatus : uint8_t
        {
            SUCCESS = 0,         // Operation completed successfully
            FILE_NOT_FOUND,      // Golden image file not found
            FILE_READ_ERROR,     // Error reading file content
            PARSE_ERROR,         // Error parsing .fs file format
            I2C_ERROR,           // I2C communication failure
            DEVICE_NOT_FOUND,    // BQ34Z100 device not responding
            UNSEAL_FAILED,       // Failed to unseal device
            VERIFICATION_FAILED, // Post-programming verification failed
            TIMEOUT,             // Operation timeout
            INVALID_PARAMETER,   // Invalid input parameter
            PLATFORM_ERROR       // Platform-specific error
        };

        /**
         * @brief Platform-specific I2C interface abstraction
         */
        class I2CInterface
        {
        public:
            virtual ~I2CInterface() = default;

            /**
             * @brief Initialize I2C interface
             * @param sda_pin SDA pin (ESP32 only, ignored on STM32)
             * @param scl_pin SCL pin (ESP32 only, ignored on STM32)
             * @param frequency I2C frequency in Hz
             * @return true if initialization successful
             */
            virtual bool initialize(int sda_pin = -1, int scl_pin = -1, uint32_t frequency = 100000) = 0;

            /**
             * @brief Write data to I2C device
             * @param device_addr I2C device address
             * @param reg_addr Register address
             * @param data Data buffer to write
             * @param length Number of bytes to write
             * @return true if write successful
             */
            virtual bool writeRegister(uint8_t device_addr, uint8_t reg_addr,
                                       const uint8_t *data, size_t length) = 0;

            /**
             * @brief Read data from I2C device
             * @param device_addr I2C device address
             * @param reg_addr Register address
             * @param data Buffer to store read data
             * @param length Number of bytes to read
             * @return true if read successful
             */
            virtual bool readRegister(uint8_t device_addr, uint8_t reg_addr,
                                      uint8_t *data, size_t length) = 0;

            /**
             * @brief Check if device is present on I2C bus
             * @param device_addr I2C device address to check
             * @return true if device responds
             */
            virtual bool devicePresent(uint8_t device_addr) = 0;
        };

        /**
         * @brief Platform-specific file system interface abstraction
         */
        class FileSystemInterface
        {
        public:
            virtual ~FileSystemInterface() = default;

            /**
             * @brief Initialize file system
             * @return true if initialization successful
             */
            virtual bool initialize() = 0;

            /**
             * @brief Check if file exists
             * @param filename Path to file
             * @return true if file exists
             */
            virtual bool fileExists(const std::string &filename) = 0;

            /**
             * @brief Read entire file content
             * @param filename Path to file
             * @param content Output string to store file content
             * @return true if read successful
             */
            virtual bool readFile(const std::string &filename, std::string &content) = 0;

            /**
             * @brief Get file size in bytes
             * @param filename Path to file
             * @return File size, or 0 if error
             */
            virtual size_t getFileSize(const std::string &filename) = 0;
        };

        /**
         * @brief Main BQ34Z100 configuration loader class
         *
         * This class provides the primary interface for loading and executing
         * TI Battery Management Studio generated golden image files (.bq.fs and .df.fs)
         * on BQ34Z100 battery fuel gauge devices.
         *
         * Key features:
         * - Cross-platform support (ESP32, STM32L476, development)
         * - Automatic .fs file parsing and validation
         * - Safe device programming with verification
         * - Comprehensive error handling and reporting
         * - Integration with existing battery monitor classes
         *
         * @example Basic usage:
         * @code
         * BQ34Z100ConfigLoader loader;
         * if (loader.initialize()) {
         *     auto status = loader.loadGoldenImage("/sdcard/battery_config.bq.fs");
         *     if (status == LoaderStatus::SUCCESS) {
         *         LOG_INFO("Battery configuration loaded successfully");
         *     }
         * }
         * @endcode
         */
        class BQ34Z100ConfigLoader
        {
        public:
            /**
             * @brief Constructor with optional custom interfaces
             * @param i2c_interface Custom I2C interface (nullptr for platform default)
             * @param fs_interface Custom file system interface (nullptr for platform default)
             */
            explicit BQ34Z100ConfigLoader(std::shared_ptr<I2CInterface> i2c_interface = nullptr,
                                          std::shared_ptr<FileSystemInterface> fs_interface = nullptr);

            /**
             * @brief Destructor
             */
            ~BQ34Z100ConfigLoader();

            // Disable copy constructor and assignment operator
            BQ34Z100ConfigLoader(const BQ34Z100ConfigLoader &) = delete;
            BQ34Z100ConfigLoader &operator=(const BQ34Z100ConfigLoader &) = delete;

            /**
             * @brief Initialize the configuration loader
             * @param i2c_sda_pin I2C SDA pin (ESP32 only, default: GPIO 21)
             * @param i2c_scl_pin I2C SCL pin (ESP32 only, default: GPIO 22)
             * @param i2c_frequency I2C frequency in Hz (default: 100kHz)
             * @return true if initialization successful
             */
            bool initialize(int i2c_sda_pin = 21, int i2c_scl_pin = 22, uint32_t i2c_frequency = 100000);

            /**
             * @brief Load and execute a golden image file
             * @param filename Path to .bq.fs or .df.fs file
             * @param verify_after_programming Perform verification after programming (default: true)
             * @return LoaderStatus indicating success or failure reason
             */
            LoaderStatus loadGoldenImage(const std::string &filename, bool verify_after_programming = true);

            /**
             * @brief Parse a golden image file without executing
             * @param filename Path to .bq.fs or .df.fs file
             * @param commands Output vector to store parsed commands
             * @return LoaderStatus indicating success or failure reason
             */
            LoaderStatus parseGoldenImage(const std::string &filename, std::vector<FSCommand> &commands);

            /**
             * @brief Execute a list of pre-parsed commands
             * @param commands Vector of FSCommand objects to execute
             * @param verify_writes Verify each write operation (default: true)
             * @return LoaderStatus indicating success or failure reason
             */
            LoaderStatus executeCommands(const std::vector<FSCommand> &commands, bool verify_writes = true);

            /**
             * @brief Get information about a golden image file
             * @param filename Path to .bq.fs or .df.fs file
             * @param info Output structure to store file information
             * @return LoaderStatus indicating success or failure reason
             */
            LoaderStatus getGoldenImageInfo(const std::string &filename, GoldenImageInfo &info);

            /**
             * @brief Verify device communication and basic functionality
             * @param device_address I2C address of BQ34Z100 (default: 0xAA >> 1)
             * @return true if device is responding correctly
             */
            bool verifyDeviceConnection(uint8_t device_address = 0x55);

            /**
             * @brief Check if device is sealed/unsealed
             * @param device_address I2C address of BQ34Z100 (default: 0xAA >> 1)
             * @return true if device is unsealed (programmable)
             */
            bool isDeviceUnsealed(uint8_t device_address = 0x55);

            /**
             * @brief Get detailed status of last operation
             * @return String describing last operation status
             */
            std::string getLastError() const { return last_error_; }

            /**
             * @brief Get execution statistics from last operation
             * @return Reference to execution statistics
             */
            const GoldenImageInfo &getExecutionStats() const { return execution_stats_; }

            /**
             * @brief Set battery monitor for integration with existing systems
             * @param monitor Pointer to existing BQ34Z100BatteryMonitor instance
             */
            void setBatteryMonitor(BQ34Z100BatteryMonitor *monitor) { battery_monitor_ = monitor; }

        private:
            // Platform-specific interface instances
            std::shared_ptr<I2CInterface> i2c_interface_;
            std::shared_ptr<FileSystemInterface> fs_interface_;

            // Integration with existing battery monitor
            BQ34Z100BatteryMonitor *battery_monitor_;

            // Internal state
            bool initialized_;
            std::string last_error_;
            GoldenImageInfo execution_stats_;

            // Internal methods
            /**
             * @brief Create platform-specific I2C interface
             * @return Shared pointer to platform I2C interface
             */
            std::shared_ptr<I2CInterface> createPlatformI2C();

            /**
             * @brief Create platform-specific file system interface
             * @return Shared pointer to platform file system interface
             */
            std::shared_ptr<FileSystemInterface> createPlatformFileSystem();

            /**
             * @brief Parse a single line from .fs file
             * @param line Text line from file
             * @param command Output FSCommand structure
             * @return true if parsing successful
             */
            bool parseFSLine(const std::string &line, FSCommand &command);

            /**
             * @brief Execute a single FSCommand
             * @param command Command to execute
             * @param verify_write Verify write operations
             * @return true if execution successful
             */
            bool executeSingleCommand(const FSCommand &command, bool verify_write = true);

            /**
             * @brief Verify a write operation by reading back
             * @param device_addr I2C device address
             * @param reg_addr Register address
             * @param expected_data Expected data
             * @param length Data length
             * @return true if verification successful
             */
            bool verifyWrite(uint8_t device_addr, uint8_t reg_addr,
                             const uint8_t *expected_data, size_t length);

            /**
             * @brief Convert hex string to byte array
             * @param hex_string Input hex string (e.g., "AA BB CC")
             * @param bytes Output byte vector
             * @return true if conversion successful
             */
            bool hexStringToBytes(const std::string &hex_string, std::vector<uint8_t> &bytes);

            /**
             * @brief Update execution statistics
             * @param command Command being processed
             */
            void updateExecutionStats(const FSCommand &command);

            /**
             * @brief Set last error message with optional formatting
             * @param error Error message
             */
            void setLastError(const std::string &error);

            /**
             * @brief Log message to platform-specific logging system
             * @param level Log level (0=DEBUG, 1=INFO, 2=WARN, 3=ERROR)
             * @param message Message to log
             */
            void logMessage(int level, const std::string &message);
        };

    } // namespace BatteryManagement
} // namespace ForestryDevice

#endif // BQ34Z100_CONFIG_LOADER_H