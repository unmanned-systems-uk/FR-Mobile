/**
 * @file bq34z100_config_loader.cpp
 * @brief Implementation of TI BQ34Z100 Golden Image File Parser
 *
 * Cross-platform implementation supporting ESP32, Raspberry Pi, and STM32L476 platforms
 * with automatic platform detection and appropriate driver integration.
 *
 * @author Forestry Research Device Team
 * @date 2025-01-31
 * @version 1.0.0
 */

#include "hardware/bq34z100_config_loader.h"
#include "utils/logger.h"
#include "main.h"
#include <sstream>
#include <algorithm>
#include <cctype>
#include <iomanip>

#ifdef PLATFORM_RASPBERRY_PI
#include <sys/stat.h>
#include <chrono>
#include <thread>
#endif

namespace ForestryDevice
{
    namespace BatteryManagement
    {

        // ============================================================================
        // Platform-Specific I2C Interface Implementations
        // ============================================================================

#ifdef PLATFORM_ESP32
        /**
         * @brief ESP32-specific I2C interface implementation using ESP-IDF drivers
         */
        class ESP32I2CInterface : public I2CInterface
        {
        private:
            i2c_port_t i2c_port_;
            bool initialized_;

        public:
            ESP32I2CInterface() : i2c_port_(I2C_NUM_0), initialized_(false) {}

            bool initialize(int sda_pin = 21, int scl_pin = 22, uint32_t frequency = 100000) override
            {
                if (initialized_)
                    return true;

                i2c_config_t conf = {};
                conf.mode = I2C_MODE_MASTER;
                conf.sda_io_num = (gpio_num_t)sda_pin;
                conf.sda_pullup_en = GPIO_PULLUP_ENABLE;
                conf.scl_io_num = (gpio_num_t)scl_pin;
                conf.scl_pullup_en = GPIO_PULLUP_ENABLE;
                conf.master.clk_speed = frequency;
                conf.clk_flags = 0;

                esp_err_t err = i2c_param_config(i2c_port_, &conf);
                if (err != ESP_OK)
                {
                    LOG_ERROR("BQ34Z100_Config", "I2C param config failed: " + std::string(esp_err_to_name(err)));
                    return false;
                }

                err = i2c_driver_install(i2c_port_, conf.mode, 0, 0, 0);
                if (err != ESP_OK)
                {
                    LOG_ERROR("BQ34Z100_Config", "I2C driver install failed: " + std::string(esp_err_to_name(err)));
                    return false;
                }

                initialized_ = true;
                LOG_INFO("BQ34Z100_Config", "ESP32 I2C initialized (SDA:" + std::to_string(sda_pin) + 
                         ", SCL:" + std::to_string(scl_pin) + ", Freq:" + std::to_string(frequency) + ")");
                return true;
            }

            bool writeRegister(uint8_t device_addr, uint8_t reg_addr,
                               const uint8_t *data, size_t length) override
            {
                if (!initialized_)
                    return false;

                i2c_cmd_handle_t cmd = i2c_cmd_link_create();
                if (!cmd)
                    return false;

                i2c_master_start(cmd);
                i2c_master_write_byte(cmd, (device_addr << 1) | I2C_MASTER_WRITE, true);
                i2c_master_write_byte(cmd, reg_addr, true);

                if (length > 0 && data != nullptr)
                {
                    i2c_master_write(cmd, data, length, true);
                }

                i2c_master_stop(cmd);

                esp_err_t err = i2c_master_cmd_begin(i2c_port_, cmd, pdMS_TO_TICKS(1000));
                i2c_cmd_link_delete(cmd);

                if (err != ESP_OK)
                {
                    LOG_WARNING("BQ34Z100_Config", "I2C write failed: " + std::string(esp_err_to_name(err)));
                    return false;
                }

                return true;
            }

            bool readRegister(uint8_t device_addr, uint8_t reg_addr,
                              uint8_t *data, size_t length) override
            {
                if (!initialized_ || !data || length == 0)
                    return false;

                i2c_cmd_handle_t cmd = i2c_cmd_link_create();
                if (!cmd)
                    return false;

                // Write register address
                i2c_master_start(cmd);
                i2c_master_write_byte(cmd, (device_addr << 1) | I2C_MASTER_WRITE, true);
                i2c_master_write_byte(cmd, reg_addr, true);

                // Read data
                i2c_master_start(cmd);
                i2c_master_write_byte(cmd, (device_addr << 1) | I2C_MASTER_READ, true);

                if (length > 1)
                {
                    i2c_master_read(cmd, data, length - 1, I2C_MASTER_ACK);
                }
                i2c_master_read_byte(cmd, &data[length - 1], I2C_MASTER_NACK);

                i2c_master_stop(cmd);

                esp_err_t err = i2c_master_cmd_begin(i2c_port_, cmd, pdMS_TO_TICKS(1000));
                i2c_cmd_link_delete(cmd);

                return (err == ESP_OK);
            }

            bool devicePresent(uint8_t device_addr) override
            {
                if (!initialized_)
                    return false;

                i2c_cmd_handle_t cmd = i2c_cmd_link_create();
                if (!cmd)
                    return false;

                i2c_master_start(cmd);
                i2c_master_write_byte(cmd, (device_addr << 1) | I2C_MASTER_WRITE, true);
                i2c_master_stop(cmd);

                esp_err_t err = i2c_master_cmd_begin(i2c_port_, cmd, pdMS_TO_TICKS(100));
                i2c_cmd_link_delete(cmd);

                return (err == ESP_OK);
            }
        };

        /**
         * @brief ESP32-specific file system interface using Arduino SD library
         */
        class ESP32FileSystemInterface : public FileSystemInterface
        {
        private:
            bool initialized_;

        public:
            ESP32FileSystemInterface() : initialized_(false) {}

            bool initialize() override
            {
                if (initialized_)
                    return true;

                if (!SD.begin())
                {
                    LOG_ERROR("BQ34Z100_Config", "SD card initialization failed");
                    return false;
                }

                initialized_ = true;
                LOG_INFO("BQ34Z100_Config", "ESP32 SD card initialized");
                return true;
            }

            bool fileExists(const std::string &filename) override
            {
                if (!initialized_)
                    return false;
                return SD.exists(filename.c_str());
            }

            bool readFile(const std::string &filename, std::string &content) override
            {
                if (!initialized_)
                    return false;

                File file = SD.open(filename.c_str(), FILE_READ);
                if (!file)
                {
                    LOG_ERROR("BQ34Z100_Config", "Failed to open file: " + filename);
                    return false;
                }

                content.clear();
                content.reserve(file.size());

                while (file.available())
                {
                    content += (char)file.read();
                }

                file.close();
                LOG_INFO("BQ34Z100_Config", "Read file: " + filename + " (" + std::to_string(content.length()) + " bytes)");
                return true;
            }

            size_t getFileSize(const std::string &filename) override
            {
                if (!initialized_)
                    return 0;

                File file = SD.open(filename.c_str(), FILE_READ);
                if (!file)
                    return 0;

                size_t size = file.size();
                file.close();
                return size;
            }
        };

#elif defined(PLATFORM_RASPBERRY_PI)
        /**
         * @brief Raspberry Pi-specific I2C interface implementation using Linux I2C
         */
        class RaspberryPiI2CInterface : public I2CInterface
        {
        private:
            int i2c_fd_;
            bool initialized_;
            std::string i2c_device_;

        public:
            RaspberryPiI2CInterface() : i2c_fd_(-1), initialized_(false), i2c_device_("/dev/i2c-1") {}

            bool initialize(int sda_pin = -1, int scl_pin = -1, uint32_t frequency = 100000) override
            {
                if (initialized_)
                    return true;

                i2c_fd_ = open(i2c_device_.c_str(), O_RDWR);
                if (i2c_fd_ < 0)
                {
                    LOG_ERROR("BQ34Z100_Config", "Failed to open I2C device: " + i2c_device_);
                    return false;
                }

                initialized_ = true;
                LOG_INFO("BQ34Z100_Config", "Raspberry Pi I2C initialized on device: " + i2c_device_);
                return true;
            }

            bool writeRegister(uint8_t device_addr, uint8_t reg_addr,
                               const uint8_t *data, size_t length) override
            {
                if (!initialized_ || i2c_fd_ < 0)
                    return false;

                if (ioctl(i2c_fd_, I2C_SLAVE, device_addr) < 0)
                {
                    LOG_ERROR("BQ34Z100_Config", "Failed to set I2C slave address");
                    return false;
                }

                std::vector<uint8_t> write_buffer;
                write_buffer.reserve(length + 1);
                write_buffer.push_back(reg_addr);

                if (data && length > 0)
                {
                    write_buffer.insert(write_buffer.end(), data, data + length);
                }

                ssize_t result = write(i2c_fd_, write_buffer.data(), write_buffer.size());
                return (result == (ssize_t)write_buffer.size());
            }

            bool readRegister(uint8_t device_addr, uint8_t reg_addr,
                              uint8_t *data, size_t length) override
            {
                if (!initialized_ || i2c_fd_ < 0 || !data || length == 0)
                    return false;

                if (ioctl(i2c_fd_, I2C_SLAVE, device_addr) < 0)
                {
                    LOG_ERROR("BQ34Z100_Config", "Failed to set I2C slave address");
                    return false;
                }

                // Write register address
                if (write(i2c_fd_, &reg_addr, 1) != 1)
                    return false;

                // Read data
                ssize_t result = read(i2c_fd_, data, length);
                return (result == (ssize_t)length);
            }

            bool devicePresent(uint8_t device_addr) override
            {
                if (!initialized_ || i2c_fd_ < 0)
                    return false;

                if (ioctl(i2c_fd_, I2C_SLAVE, device_addr) < 0)
                    return false;

                // Try to read a single byte
                uint8_t dummy;
                ssize_t result = read(i2c_fd_, &dummy, 1);
                return (result >= 0);
            }
        };

        /**
         * @brief Raspberry Pi-specific file system interface using standard filesystem
         */
        class RaspberryPiFileSystemInterface : public FileSystemInterface
        {
        private:
            bool initialized_;
            std::string base_path_;

        public:
            RaspberryPiFileSystemInterface() : initialized_(false), base_path_("/media/usb0") {}

            bool initialize() override
            {
                if (initialized_)
                    return true;

                // Check if mount point exists
                struct stat st;
                if (stat(base_path_.c_str(), &st) != 0)
                {
                    LOG_WARNING("BQ34Z100_Config", "Mount point not found, using /tmp instead");
                    base_path_ = "/tmp";
                }

                initialized_ = true;
                LOG_INFO("BQ34Z100_Config", "Raspberry Pi filesystem initialized at: " + base_path_);
                return true;
            }

            bool fileExists(const std::string &filename) override
            {
                if (!initialized_)
                    return false;

                std::string full_path = base_path_ + "/" + filename;
                struct stat st;
                return (stat(full_path.c_str(), &st) == 0);
            }

            bool readFile(const std::string &filename, std::string &content) override
            {
                if (!initialized_)
                    return false;

                std::string full_path = base_path_ + "/" + filename;
                std::ifstream file(full_path, std::ios::binary);
                if (!file.is_open())
                {
                    LOG_ERROR("BQ34Z100_Config", "Failed to open file: " + full_path);
                    return false;
                }

                content.assign((std::istreambuf_iterator<char>(file)),
                               std::istreambuf_iterator<char>());

                LOG_INFO("BQ34Z100_Config", "Read file: " + filename + " (" + std::to_string(content.length()) + " bytes)");
                return true;
            }

            size_t getFileSize(const std::string &filename) override
            {
                if (!initialized_)
                    return 0;

                std::string full_path = base_path_ + "/" + filename;
                struct stat st;
                if (stat(full_path.c_str(), &st) != 0)
                    return 0;

                return st.st_size;
            }
        };

#elif defined(PLATFORM_STM32)
        /**
         * @brief STM32L476-specific I2C interface implementation using HAL drivers
         */
        class STM32I2CInterface : public I2CInterface
        {
        private:
            I2C_HandleTypeDef *hi2c_;
            bool initialized_;

        public:
            STM32I2CInterface() : hi2c_(nullptr), initialized_(false) {}

            bool initialize(int sda_pin = -1, int scl_pin = -1, uint32_t frequency = 100000) override
            {
                // STM32 I2C is typically initialized in main.c via CubeMX
                // We'll assume hi2c1 is available and configured
                extern I2C_HandleTypeDef hi2c1;
                hi2c_ = &hi2c1;
                initialized_ = true;
                return true;
            }

            bool writeRegister(uint8_t device_addr, uint8_t reg_addr,
                               const uint8_t *data, size_t length) override
            {
                if (!initialized_ || !hi2c_)
                    return false;

                // Combine register address and data into single buffer
                std::vector<uint8_t> write_buffer;
                write_buffer.reserve(length + 1);
                write_buffer.push_back(reg_addr);

                if (data && length > 0)
                {
                    write_buffer.insert(write_buffer.end(), data, data + length);
                }

                HAL_StatusTypeDef status = HAL_I2C_Master_Transmit(
                    hi2c_,
                    device_addr << 1,
                    write_buffer.data(),
                    write_buffer.size(),
                    1000);

                return (status == HAL_OK);
            }

            bool readRegister(uint8_t device_addr, uint8_t reg_addr,
                              uint8_t *data, size_t length) override
            {
                if (!initialized_ || !hi2c_ || !data || length == 0)
                    return false;

                // Write register address
                HAL_StatusTypeDef status = HAL_I2C_Master_Transmit(
                    hi2c_,
                    device_addr << 1,
                    &reg_addr,
                    1,
                    1000);

                if (status != HAL_OK)
                    return false;

                // Read data
                status = HAL_I2C_Master_Receive(
                    hi2c_,
                    device_addr << 1,
                    data,
                    length,
                    1000);

                return (status == HAL_OK);
            }

            bool devicePresent(uint8_t device_addr) override
            {
                if (!initialized_ || !hi2c_)
                    return false;

                HAL_StatusTypeDef status = HAL_I2C_IsDeviceReady(
                    hi2c_,
                    device_addr << 1,
                    3,
                    100);

                return (status == HAL_OK);
            }
        };

        /**
         * @brief STM32L476-specific file system interface using FatFS
         */
        class STM32FileSystemInterface : public FileSystemInterface
        {
        private:
            bool initialized_;

        public:
            STM32FileSystemInterface() : initialized_(false) {}

            bool initialize() override
            {
                if (initialized_)
                    return true;

                // Mount SD card using FatFS
                FRESULT res = f_mount(&SDFatFS, SDPath, 1);
                if (res != FR_OK)
                {
                    return false;
                }

                initialized_ = true;
                return true;
            }

            bool fileExists(const std::string &filename) override
            {
                if (!initialized_)
                    return false;

                FILINFO fno;
                FRESULT res = f_stat(filename.c_str(), &fno);
                return (res == FR_OK);
            }

            bool readFile(const std::string &filename, std::string &content) override
            {
                if (!initialized_)
                    return false;

                FIL file;
                FRESULT res = f_open(&file, filename.c_str(), FA_READ);
                if (res != FR_OK)
                    return false;

                DWORD file_size = f_size(&file);
                content.clear();
                content.reserve(file_size);

                char buffer[512];
                UINT bytes_read;

                while (f_read(&file, buffer, sizeof(buffer), &bytes_read) == FR_OK && bytes_read > 0)
                {
                    content.append(buffer, bytes_read);
                }

                f_close(&file);
                return true;
            }

            size_t getFileSize(const std::string &filename) override
            {
                if (!initialized_)
                    return 0;

                FILINFO fno;
                FRESULT res = f_stat(filename.c_str(), &fno);
                return (res == FR_OK) ? fno.fsize : 0;
            }
        };

#else // PLATFORM_DEVELOPMENT
        /**
         * @brief Development platform I2C interface (simulation)
         */
        class DevelopmentI2CInterface : public I2CInterface
        {
        private:
            bool initialized_;

        public:
            DevelopmentI2CInterface() : initialized_(false) {}

            bool initialize(int sda_pin = -1, int scl_pin = -1, uint32_t frequency = 100000) override
            {
                initialized_ = true;
                LOG_INFO("BQ34Z100_Config", "Development I2C interface initialized");
                return true;
            }

            bool writeRegister(uint8_t device_addr, uint8_t reg_addr,
                               const uint8_t *data, size_t length) override
            {
                if (!initialized_)
                    return false;

                std::stringstream ss;
                ss << "I2C Write: Device 0x" << std::hex << (int)device_addr
                   << " Reg 0x" << (int)reg_addr << " Data: ";

                if (data && length > 0)
                {
                    for (size_t i = 0; i < length; i++)
                    {
                        ss << std::hex << std::setw(2) << std::setfill('0') << (int)data[i] << " ";
                    }
                }
                LOG_DEBUG("BQ34Z100_Config", ss.str());

                return true; // Simulate successful write
            }

            bool readRegister(uint8_t device_addr, uint8_t reg_addr,
                              uint8_t *data, size_t length) override
            {
                if (!initialized_ || !data || length == 0)
                    return false;

                // Simulate read by filling with dummy data
                for (size_t i = 0; i < length; i++)
                {
                    data[i] = 0x00; // Simulate empty/default values
                }

                std::stringstream ss;
                ss << "I2C Read: Device 0x" << std::hex << (int)device_addr
                   << " Reg 0x" << (int)reg_addr << " Length: " << std::dec << length;
                LOG_DEBUG("BQ34Z100_Config", ss.str());

                return true;
            }

            bool devicePresent(uint8_t device_addr) override
            {
                std::stringstream ss;
                ss << "I2C Device Check: 0x" << std::hex << (int)device_addr << " - Simulated Present";
                LOG_DEBUG("BQ34Z100_Config", ss.str());
                return true; // Simulate device present
            }
        };

        /**
         * @brief Development platform file system interface
         */
        class DevelopmentFileSystemInterface : public FileSystemInterface
        {
        private:
            bool initialized_;

        public:
            DevelopmentFileSystemInterface() : initialized_(false) {}

            bool initialize() override
            {
                initialized_ = true;
                LOG_INFO("BQ34Z100_Config", "Development file system initialized");
                return true;
            }

            bool fileExists(const std::string &filename) override
            {
                if (!initialized_)
                    return false;

                std::ifstream file(filename);
                return file.good();
            }

            bool readFile(const std::string &filename, std::string &content) override
            {
                if (!initialized_)
                    return false;

                std::ifstream file(filename);
                if (!file.is_open())
                {
                    LOG_ERROR("BQ34Z100_Config", "Failed to open file: " + filename);
                    return false;
                }

                content.assign((std::istreambuf_iterator<char>(file)),
                               std::istreambuf_iterator<char>());

                LOG_INFO("BQ34Z100_Config", "Read file: " + filename + " (" + std::to_string(content.length()) + " bytes)");
                return true;
            }

            size_t getFileSize(const std::string &filename) override
            {
                if (!initialized_)
                    return 0;

                std::ifstream file(filename, std::ios::binary | std::ios::ate);
                if (!file.is_open())
                    return 0;

                return file.tellg();
            }
        };

#endif

        // ============================================================================
        // Main BQ34Z100ConfigLoader Implementation
        // ============================================================================

        BQ34Z100ConfigLoader::BQ34Z100ConfigLoader(std::shared_ptr<I2CInterface> i2c_interface,
                                                   std::shared_ptr<FileSystemInterface> fs_interface)
            : i2c_interface_(i2c_interface), fs_interface_(fs_interface), battery_monitor_(nullptr), initialized_(false)
        {
            if (!i2c_interface_)
            {
                i2c_interface_ = createPlatformI2C();
            }

            if (!fs_interface_)
            {
                fs_interface_ = createPlatformFileSystem();
            }
        }

        BQ34Z100ConfigLoader::~BQ34Z100ConfigLoader()
        {
            // Cleanup handled by smart pointers
        }

        bool BQ34Z100ConfigLoader::initialize(int i2c_sda_pin, int i2c_scl_pin, uint32_t i2c_frequency)
        {
            if (initialized_)
                return true;

            // Initialize file system
            if (!fs_interface_->initialize())
            {
                setLastError("Failed to initialize file system");
                return false;
            }

            // Initialize I2C interface
            if (!i2c_interface_->initialize(i2c_sda_pin, i2c_scl_pin, i2c_frequency))
            {
                setLastError("Failed to initialize I2C interface");
                return false;
            }

            initialized_ = true;
            logMessage(1, "BQ34Z100 Configuration Loader initialized successfully");
            return true;
        }

        LoaderStatus BQ34Z100ConfigLoader::loadGoldenImage(const std::string &filename,
                                                           bool verify_after_programming)
        {
            if (!initialized_)
            {
                setLastError("Configuration loader not initialized");
                return LoaderStatus::PLATFORM_ERROR;
            }

            // Reset execution statistics
            execution_stats_ = GoldenImageInfo();
            execution_stats_.filename = filename;

            // Check if file exists
            if (!fs_interface_->fileExists(filename))
            {
                setLastError("Golden image file not found: " + filename);
                return LoaderStatus::FILE_NOT_FOUND;
            }

            // Parse the golden image file
            std::vector<FSCommand> commands;
            LoaderStatus parse_status = parseGoldenImage(filename, commands);
            if (parse_status != LoaderStatus::SUCCESS)
            {
                return parse_status;
            }

            logMessage(1, "Parsed " + std::to_string(commands.size()) + " commands from " + filename);

            // Verify device connection before programming
            if (!verifyDeviceConnection())
            {
                setLastError("BQ34Z100 device not responding");
                return LoaderStatus::DEVICE_NOT_FOUND;
            }

            // Execute the commands
            LoaderStatus exec_status = executeCommands(commands, verify_after_programming);
            if (exec_status != LoaderStatus::SUCCESS)
            {
                return exec_status;
            }

            logMessage(1, "Golden image loaded successfully: " + filename);
            return LoaderStatus::SUCCESS;
        }

        LoaderStatus BQ34Z100ConfigLoader::parseGoldenImage(const std::string &filename,
                                                            std::vector<FSCommand> &commands)
        {
            commands.clear();

            // Determine file type
            if (filename.find(".bq.fs") != std::string::npos)
            {
                execution_stats_.file_type = ".bq.fs";
            }
            else if (filename.find(".df.fs") != std::string::npos)
            {
                execution_stats_.file_type = ".df.fs";
            }
            else
            {
                setLastError("Unknown file type (expected .bq.fs or .df.fs): " + filename);
                return LoaderStatus::PARSE_ERROR;
            }

            // Read file content
            std::string content;
            if (!fs_interface_->readFile(filename, content))
            {
                setLastError("Failed to read file: " + filename);
                return LoaderStatus::FILE_READ_ERROR;
            }

            // Parse line by line
            std::istringstream stream(content);
            std::string line;
            size_t line_number = 0;

            while (std::getline(stream, line))
            {
                line_number++;

                // Skip empty lines
                if (line.empty())
                    continue;

                FSCommand command;
                if (parseFSLine(line, command))
                {
                    commands.push_back(command);
                    updateExecutionStats(command);
                }
                else
                {
                    logMessage(2, "Warning: Failed to parse line " + std::to_string(line_number) + ": " + line);
                }
            }

            execution_stats_.total_commands = commands.size();
            logMessage(1, "Parsed " + std::to_string(commands.size()) + " commands from " + filename);

            return LoaderStatus::SUCCESS;
        }

        LoaderStatus BQ34Z100ConfigLoader::executeCommands(const std::vector<FSCommand> &commands,
                                                           bool verify_writes)
        {
            logMessage(1, "Executing " + std::to_string(commands.size()) + " commands");

            for (size_t i = 0; i < commands.size(); i++)
            {
                const FSCommand &cmd = commands[i];

                if (!executeSingleCommand(cmd, verify_writes))
                {
                    setLastError("Failed to execute command " + std::to_string(i + 1) +
                                 " of " + std::to_string(commands.size()));
                    return LoaderStatus::I2C_ERROR;
                }

                // Log progress every 100 commands or on important commands
                if ((i + 1) % 100 == 0 || cmd.type == FSCommandType::DELAY)
                {
                    logMessage(1, "Progress: " + std::to_string(i + 1) + "/" + std::to_string(commands.size()) + " commands completed");
                }
            }

            logMessage(1, "All commands executed successfully");
            return LoaderStatus::SUCCESS;
        }

        LoaderStatus BQ34Z100ConfigLoader::getGoldenImageInfo(const std::string &filename,
                                                              GoldenImageInfo &info)
        {
            std::vector<FSCommand> commands;
            LoaderStatus status = parseGoldenImage(filename, commands);

            if (status == LoaderStatus::SUCCESS)
            {
                info = execution_stats_;
            }

            return status;
        }

        bool BQ34Z100ConfigLoader::verifyDeviceConnection(uint8_t device_address)
        {
            if (!initialized_)
                return false;

            // Check if device is present on I2C bus
            if (!i2c_interface_->devicePresent(device_address))
            {
                logMessage(3, "BQ34Z100 device not present at address 0x" +
                                  std::to_string(device_address));
                return false;
            }

            // Try to read control status register
            uint8_t status_data[2];
            if (!i2c_interface_->readRegister(device_address,
                                              BQ34Z100Registers::CONTROL_STATUS,
                                              status_data, sizeof(status_data)))
            {
                logMessage(3, "Failed to read control status register");
                return false;
            }

            logMessage(1, "BQ34Z100 device verified at address 0x" + std::to_string(device_address));
            return true;
        }

        bool BQ34Z100ConfigLoader::isDeviceUnsealed(uint8_t device_address)
        {
            if (!initialized_)
                return false;

            // Read control status to check sealed/unsealed state
            uint8_t status_data[2];
            if (!i2c_interface_->readRegister(device_address,
                                              BQ34Z100Registers::CONTROL_STATUS,
                                              status_data, sizeof(status_data)))
            {
                return false;
            }

            uint16_t control_status = (status_data[1] << 8) | status_data[0];

            // Check if device is unsealed (bit varies by device, this is simplified)
            // In practice, you would check specific status bits per BQ34Z100 datasheet
            logMessage(1, "Device control status: 0x" + std::to_string(control_status));

            return true; // Simplified - assume unsealed for now
        }

        // ============================================================================
        // Private Implementation Methods
        // ============================================================================

        std::shared_ptr<I2CInterface> BQ34Z100ConfigLoader::createPlatformI2C()
        {
#ifdef PLATFORM_ESP32
            return std::make_shared<ESP32I2CInterface>();
#elif defined(PLATFORM_RASPBERRY_PI)
            return std::make_shared<RaspberryPiI2CInterface>();
#elif defined(PLATFORM_STM32)
            return std::make_shared<STM32I2CInterface>();
#else
            return std::make_shared<DevelopmentI2CInterface>();
#endif
        }

        std::shared_ptr<FileSystemInterface> BQ34Z100ConfigLoader::createPlatformFileSystem()
        {
#ifdef PLATFORM_ESP32
            return std::make_shared<ESP32FileSystemInterface>();
#elif defined(PLATFORM_RASPBERRY_PI)
            return std::make_shared<RaspberryPiFileSystemInterface>();
#elif defined(PLATFORM_STM32)
            return std::make_shared<STM32FileSystemInterface>();
#else
            return std::make_shared<DevelopmentFileSystemInterface>();
#endif
        }

        bool BQ34Z100ConfigLoader::parseFSLine(const std::string &line, FSCommand &command)
        {
            // Remove leading/trailing whitespace
            std::string trimmed = line;
            trimmed.erase(0, trimmed.find_first_not_of(" \t\r\n"));
            trimmed.erase(trimmed.find_last_not_of(" \t\r\n") + 1);

            if (trimmed.empty())
                return false;

            // Check command type
            char cmd_char = trimmed[0];
            command.type = static_cast<FSCommandType>(cmd_char);

            switch (command.type)
            {
            case FSCommandType::COMMENT:
                command.comment = trimmed.substr(1);
                return true;

            case FSCommandType::DELAY:
                // Format: X: <delay_ms>
                if (trimmed.length() > 3 && trimmed[1] == ':')
                {
                    try
                    {
                        command.delay_ms = std::stoul(trimmed.substr(2));
                        return true;
                    }
                    catch (...)
                    {
                        return false;
                    }
                }
                return false;

            case FSCommandType::WRITE_REGISTER:
            case FSCommandType::READ_REGISTER:
                // Format: W: <device_addr> <reg_addr> <data...>
                // Format: C: <device_addr> <reg_addr> <expected_value>
                if (trimmed.length() > 3 && trimmed[1] == ':')
                {
                    std::istringstream iss(trimmed.substr(2));
                    std::string hex_token;
                    std::vector<std::string> tokens;

                    while (iss >> hex_token)
                    {
                        tokens.push_back(hex_token);
                    }

                    if (tokens.size() >= 2)
                    {
                        try
                        {
                            command.device_address = std::stoul(tokens[0], nullptr, 16);
                            command.register_address = std::stoul(tokens[1], nullptr, 16);

                            if (command.type == FSCommandType::WRITE_REGISTER && tokens.size() > 2)
                            {
                                // Parse data bytes for write command
                                for (size_t i = 2; i < tokens.size(); i++)
                                {
                                    uint8_t byte_val = std::stoul(tokens[i], nullptr, 16);
                                    command.data.push_back(byte_val);
                                }
                            }
                            else if (command.type == FSCommandType::READ_REGISTER && tokens.size() > 2)
                            {
                                // Parse expected value for read command
                                command.expected_value = std::stoul(tokens[2], nullptr, 16);
                            }

                            return true;
                        }
                        catch (...)
                        {
                            return false;
                        }
                    }
                }
                return false;

            default:
                command.type = FSCommandType::UNKNOWN;
                return false;
            }
        }

        bool BQ34Z100ConfigLoader::executeSingleCommand(const FSCommand &command, bool verify_write)
        {
            switch (command.type)
            {
            case FSCommandType::COMMENT:
                // Comments are ignored during execution
                return true;

            case FSCommandType::DELAY:
                // Execute delay
                logMessage(0, "Executing delay: " + std::to_string(command.delay_ms) + "ms");
#ifdef PLATFORM_ESP32
                vTaskDelay(pdMS_TO_TICKS(command.delay_ms));
#elif defined(PLATFORM_RASPBERRY_PI)
                std::this_thread::sleep_for(std::chrono::milliseconds(command.delay_ms));
#elif defined(PLATFORM_STM32)
                HAL_Delay(command.delay_ms);
#else
                std::this_thread::sleep_for(std::chrono::milliseconds(command.delay_ms));
#endif
                return true;

            case FSCommandType::WRITE_REGISTER:
                // Execute write operation
                if (!i2c_interface_->writeRegister(command.device_address,
                                                   command.register_address,
                                                   command.data.data(),
                                                   command.data.size()))
                {
                    setLastError("I2C write failed");
                    return false;
                }

                // Verify write if requested
                if (verify_write && !command.data.empty())
                {
                    return verifyWrite(command.device_address,
                                       command.register_address,
                                       command.data.data(),
                                       command.data.size());
                }
                return true;

            case FSCommandType::READ_REGISTER:
                // Execute read operation and verify value
                uint8_t read_value;
                if (!i2c_interface_->readRegister(command.device_address,
                                                  command.register_address,
                                                  &read_value, 1))
                {
                    setLastError("I2C read failed");
                    return false;
                }

                if (read_value != command.expected_value)
                {
                    setLastError("Read verification failed - expected: 0x" +
                                 std::to_string(command.expected_value) +
                                 ", got: 0x" + std::to_string(read_value));
                    return false;
                }
                return true;

            default:
                setLastError("Unknown command type");
                return false;
            }
        }

        bool BQ34Z100ConfigLoader::verifyWrite(uint8_t device_addr, uint8_t reg_addr,
                                               const uint8_t *expected_data, size_t length)
        {
            if (!expected_data || length == 0)
                return true;

            std::vector<uint8_t> read_data(length);
            if (!i2c_interface_->readRegister(device_addr, reg_addr,
                                              read_data.data(), length))
            {
                return false;
            }

            for (size_t i = 0; i < length; i++)
            {
                if (read_data[i] != expected_data[i])
                {
                    setLastError("Write verification failed at byte " + std::to_string(i));
                    return false;
                }
            }

            return true;
        }

        bool BQ34Z100ConfigLoader::hexStringToBytes(const std::string &hex_string,
                                                    std::vector<uint8_t> &bytes)
        {
            bytes.clear();

            for (size_t i = 0; i < hex_string.length(); i += 2)
            {
                if (i + 1 >= hex_string.length())
                    return false;

                try
                {
                    std::string byte_str = hex_string.substr(i, 2);
                    uint8_t byte_val = std::stoul(byte_str, nullptr, 16);
                    bytes.push_back(byte_val);
                }
                catch (...)
                {
                    return false;
                }
            }

            return true;
        }

        void BQ34Z100ConfigLoader::updateExecutionStats(const FSCommand &command)
        {
            switch (command.type)
            {
            case FSCommandType::WRITE_REGISTER:
                execution_stats_.write_commands++;

                // Check for special sequences
                if (command.device_address == 0xAA && command.register_address == 0x00)
                {
                    if (command.data.size() >= 2 &&
                        command.data[0] == 0x14 && command.data[1] == 0x04)
                    {
                        execution_stats_.has_unseal_sequence = true;
                    }
                }

                if (command.device_address == 0xAA && command.register_address == 0x00 &&
                    command.data.size() >= 2 &&
                    command.data[0] == 0x00 && command.data[1] == 0x0F)
                {
                    execution_stats_.has_rom_mode = true;
                }

                if (command.device_address == 0x16)
                {
                    execution_stats_.has_flash_programming = true;
                }
                break;

            case FSCommandType::READ_REGISTER:
                execution_stats_.read_commands++;
                break;

            case FSCommandType::DELAY:
                execution_stats_.delay_commands++;
                execution_stats_.estimated_time_ms += command.delay_ms;
                break;

            case FSCommandType::COMMENT:
                execution_stats_.comment_lines++;
                break;

            default:
                break;
            }
        }

        void BQ34Z100ConfigLoader::setLastError(const std::string &error)
        {
            last_error_ = error;
            logMessage(3, "Error: " + error);
        }

        void BQ34Z100ConfigLoader::logMessage(int level, const std::string &message)
        {
            // Use the project's unified logger system
            switch (level)
            {
            case 0:
                LOG_DEBUG("BQ34Z100_Config", message);
                break;
            case 1:
                LOG_INFO("BQ34Z100_Config", message);
                break;
            case 2:
                LOG_WARNING("BQ34Z100_Config", message);
                break;
            case 3:
                LOG_ERROR("BQ34Z100_Config", message);
                break;
            default:
                LOG_INFO("BQ34Z100_Config", message);
                break;
            }
        }

    } // namespace BatteryManagement
} // namespace ForestryDevice