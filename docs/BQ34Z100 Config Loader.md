# BQ34Z100 Golden Image Configuration Integration Guide

## Overview

This guide demonstrates how to integrate the BQ34Z100 Golden Image Configuration Loader into your existing Forestry Research Device (ESP32) and BMS (STM32L476) projects.

## Project Integration

### 1. Forestry Research Device (ESP32) Integration

#### File Structure Updates
```
ForestryResearchDevice/
├── include/
│   ├── hardware/
│   │   ├── bq34z100_battery_monitor.h     # ✅ Existing
│   │   └── bq34z100_config_loader.h       # 🆕 New
│   └── ...
├── src/
│   ├── hardware/
│   │   ├── bq34z100_battery_monitor.cpp   # ✅ Existing  
│   │   └── bq34z100_config_loader.cpp     # 🆕 New
│   └── ...
├── data/                                  # 🆕 SD Card Data
│   ├── battery_configs/
│   │   ├── li_ion_2600mah.bq.fs          # TI BMS Golden Images
│   │   ├── li_ion_2600mah.df.fs
│   │   ├── lifepo4_3200mah.bq.fs
│   │   └── lifepo4_3200mah.df.fs
│   └── ...
```

#### CMakeLists.txt Updates
```cmake
# Add new source files
set(FORESTRY_SOURCES
    ${FORESTRY_SOURCES}
    src/hardware/bq34z100_config_loader.cpp
)

# Add include directories  
target_include_directories(ForestryResearchDevice PRIVATE
    include/hardware
)
```

#### ESP32 Main Application Integration
```cpp
// In main.cpp - Add to existing includes
#include "hardware/bq34z100_config_loader.h"

// In main() function, after SD card initialization
void setup_battery_configuration() {
    using namespace ForestryDevice::BatteryManagement;
    
    // Create configuration loader
    BQ34Z100ConfigLoader config_loader;
    
    // Initialize with existing I2C pins (from your context)
    if (!config_loader.initialize(21, 22, 100000)) {
        LOG_ERROR("Failed to initialize battery configuration loader");
        return;
    }
    
    // Integrate with existing battery monitor
    if (battery_monitor_) {
        config_loader.setBatteryMonitor(battery_monitor_.get());
    }
    
    // Load golden image based on battery type detected or configured
    std::string config_file = "/sdcard/battery_configs/li_ion_2600mah.bq.fs";
    
    LOG_INFO("Loading battery configuration: %s", config_file.c_str());
    
    LoaderStatus status = config_loader.loadGoldenImage(config_file, true);
    if (status == LoaderStatus::SUCCESS) {
        LOG_INFO("Battery configuration loaded successfully");
        
        // Get and log execution statistics
        const auto& stats = config_loader.getExecutionStats();
        LOG_INFO("Configuration stats: %zu commands, %zu writes, %zu reads, %dms execution time",
                 stats.total_commands, stats.write_commands, stats.read_commands, 
                 stats.estimated_time_ms);
    } else {
        LOG_ERROR("Failed to load battery configuration: %s", 
                  config_loader.getLastError().c_str());
    }
}

// In main() function, integrate into initialization sequence
int main() {
    // ... existing initialization code ...
    
    // Initialize SD card (already done in your project)
    if (!initializeSDCard()) {
        LOG_ERROR("SD card initialization failed");
        return -1;
    }
    
    // Initialize battery monitor (already done in your project) 
    if (!initializeBatteryMonitor()) {
        LOG_ERROR("Battery monitor initialization failed");
        return -1;
    }
    
    // 🆕 NEW: Setup battery configuration
    setup_battery_configuration();
    
    // ... rest of existing main loop ...
}
```

### 2. BMS Project (STM32L476) Integration

#### File Structure Updates
```
BMS_Project/
├── Core/
│   ├── Inc/
│   │   ├── bq34z100_config_loader.h      # 🆕 New
│   │   └── fatfs.h                        # ✅ Existing (CubeMX generated)
│   ├── Src/
│   │   ├── bq34z100_config_loader.cpp    # 🆕 New
│   │   ├── main.c                         # ✅ Existing (modify)
│   │   └── fatfs.c                        # 🆕 Add SD card support
│   └── ...
├── FATFS/                                 # 🆕 Add via CubeMX
│   ├── Target/
│   └── App/
├── battery_configs/                       # 🆕 SD Card Root
│   ├── production_battery.bq.fs
│   └── production_battery.df.fs
```

#### CubeMX Configuration Requirements

**1. Enable FatFS Middleware:**
- Middleware → FATFS → Enable
- Set SD Card mode
- Configure SPI or SDIO for SD card interface

**2. I2C Configuration:**
- Enable I2C1 or I2C2 
- Configure pins and timing
- Enable I2C interrupts if desired

**3. Generated Code Integration:**
```c
// In main.c - Add to USER CODE includes
/* USER CODE BEGIN Includes */
#ifdef __cplusplus
extern "C" {
#endif
#include "bq34z100_config_loader.h"
#ifdef __cplusplus
}
#endif
/* USER CODE END Includes */

// In main() function
int main(void) {
    /* USER CODE BEGIN 1 */
    /* USER CODE END 1 */

    HAL_Init();
    SystemClock_Config();
    
    // Initialize peripherals (CubeMX generated)
    MX_GPIO_Init();
    MX_I2C1_Init();
    MX_FATFS_Init();
    MX_SPI1_Init(); // or SDIO if using SDIO mode
    
    /* USER CODE BEGIN 2 */
    
    // Setup battery configuration
    setup_battery_configuration_stm32();
    
    /* USER CODE END 2 */
    
    // Main loop
    while (1) {
        /* USER CODE BEGIN 3 */
        /* USER CODE END 3 */
    }
}

// Add this function in USER CODE section
/* USER CODE BEGIN 4 */
void setup_battery_configuration_stm32(void) {
    // Create configuration loader (C++ object in C code)
    ForestryDevice::BatteryManagement::BQ34Z100ConfigLoader* loader = 
        new ForestryDevice::BatteryManagement::BQ34Z100ConfigLoader();
    
    // Initialize loader
    if (!loader->initialize()) {
        // Handle error - could use UART printf or LED indication
        Error_Handler();
        return;
    }
    
    // Load production configuration
    ForestryDevice::BatteryManagement::LoaderStatus status = 
        loader->loadGoldenImage("production_battery.bq.fs", true);
    
    if (status == ForestryDevice::BatteryManagement::LoaderStatus::SUCCESS) {
        // Configuration loaded successfully
        // Could toggle LED or send status via UART
        HAL_GPIO_WritePin(GPIOA, GPIO_PIN_5, GPIO_PIN_SET); // Turn on success LED
    } else {
        // Handle configuration error
        Error_Handler();
    }
    
    // Clean up
    delete loader;
}
/* USER CODE END 4 */
```

## Configuration File Management

### SD Card Structure
```
/sdcard/                              # ESP32 mount point
└── battery_configs/
    ├── li_ion_2600mah.bq.fs         # Configuration file
    ├── li_ion_2600mah.df.fs         # Data Flash file
    ├── lifepo4_3200mah.bq.fs        # Alternative chemistry
    ├── lifepo4_3200mah.df.fs
    ├── custom_profile.bq.fs          # Custom configurations
    └── test_config.bq.fs
```

### Golden Image File Format

The BQ34Z100 Configuration Loader supports TI Battery Management Studio generated files:

**Supported File Types:**
- `.bq.fs` - Main configuration files with device setup commands
- `.df.fs` - Data Flash programming files

**Command Format Examples:**
```
; Comment: Battery configuration for Li-Ion 2600mAh
W: AA 00 14 04                        ; Unseal command
X: 100                                ; Wait 100ms
W: AA 3E 52                          ; Set flash class
W: AA 3F 00                          ; Set flash block
W: AA 40 0A 28 0B B8                 ; Write flash data
C: AA 40 0A                          ; Verify data
X: 500                               ; Wait for flash write
```

## Complete Feature Set

### 1. Cross-Platform Support

**ESP32 Implementation:**
- ESP-IDF I2C driver integration
- Arduino SD card library support
- ESP logging system integration
- FreeRTOS task delay support

**STM32L476 Implementation:**
- HAL I2C driver integration
- FatFS file system support
- HAL delay functions
- Custom logging (UART/SWO configurable)

**Development Platform:**
- File system simulation
- I2C operation logging
- Standard library threading

### 2. Golden Image File Processing

**File Parsing Features:**
- Automatic file type detection (.bq.fs vs .df.fs)
- Comment preservation and logging
- Error-tolerant parsing with line-by-line validation
- Comprehensive statistics generation

**Command Support:**
- `W:` Write register commands with multi-byte data
- `C:` Read/check register commands with verification
- `X:` Delay commands with millisecond precision
- `;` Comment commands for documentation

**Advanced Parsing:**
- Hex string to byte conversion
- Device address and register validation
- Data length verification
- Execution time estimation

### 3. Device Communication

**I2C Interface Abstraction:**
- Platform-specific driver integration
- Configurable pins and frequency (ESP32)
- Device presence detection
- Automatic retry logic with timeouts

**BQ34Z100 Register Support:**
- Control and status registers
- Battery measurement registers
- Flash programming registers
- Sealed/unsealed state detection

**Communication Features:**
- Write operation verification
- Read-back confirmation
- Device health checking
- Error recovery mechanisms

### 4. Flash Programming Safety

**Device State Management:**
- Automatic unseal sequence detection
- ROM mode entry verification
- Flash programming state tracking
- Post-programming validation

**Safety Features:**
- Write verification for critical operations
- Command execution rollback on failure
- Device state restoration
- Comprehensive error reporting

### 5. Integration Capabilities

**Battery Monitor Integration:**
- Seamless integration with existing BQ34Z100BatteryMonitor
- Shared I2C resource management
- Configuration validation against monitor settings
- Status synchronization

**System Integration:**
- SD card dependency management
- Power management coordination
- Logging system integration
- Error handling consistency

### 6. Statistics and Monitoring

**Execution Statistics:**
```cpp
struct GoldenImageInfo {
    std::string filename;           // Source file name
    std::string file_type;          // .bq.fs or .df.fs
    size_t total_commands;          // Total commands parsed
    size_t write_commands;          // Number of write operations
    size_t read_commands;           // Number of read operations
    size_t delay_commands;          // Number of delay operations
    size_t comment_lines;           // Documentation lines
    uint32_t estimated_time_ms;     // Execution time estimate
    bool has_unseal_sequence;       // Contains device unseal
    bool has_rom_mode;             // Contains ROM mode entry
    bool has_flash_programming;     // Contains flash operations
};
```

**Runtime Monitoring:**
- Real-time progress reporting
- Command execution tracking
- Error logging with context
- Performance metrics

### 7. Error Handling and Recovery

**Comprehensive Error Types:**
```cpp
enum class LoaderStatus : uint8_t {
    SUCCESS = 0,                    // Operation completed successfully
    FILE_NOT_FOUND,                // Golden image file not found
    FILE_READ_ERROR,               // Error reading file content
    PARSE_ERROR,                   // Error parsing .fs file format
    I2C_ERROR,                     // I2C communication failure
    DEVICE_NOT_FOUND,              // BQ34Z100 device not responding
    UNSEAL_FAILED,                 // Failed to unseal device
    VERIFICATION_FAILED,           // Post-programming verification failed
    TIMEOUT,                       // Operation timeout
    INVALID_PARAMETER,             // Invalid input parameter
    PLATFORM_ERROR                 // Platform-specific error
};
```

**Error Recovery:**
- Automatic retry for transient failures
- Detailed error context reporting
- Safe device state restoration
- Graceful degradation options

## Usage Examples

### Basic Configuration Loading
```cpp
#include "hardware/bq34z100_config_loader.h"

using namespace ForestryDevice::BatteryManagement;

void loadBatteryConfiguration() {
    // Create loader instance
    BQ34Z100ConfigLoader loader;
    
    // Initialize with custom I2C settings
    if (!loader.initialize(21, 22, 100000)) {
        LOG_ERROR("Failed to initialize configuration loader");
        return;
    }
    
    // Load golden image
    LoaderStatus status = loader.loadGoldenImage("/sdcard/battery_configs/li_ion_2600mah.bq.fs");
    
    switch (status) {
        case LoaderStatus::SUCCESS:
            LOG_INFO("Battery configuration loaded successfully");
            break;
        case LoaderStatus::FILE_NOT_FOUND:
            LOG_ERROR("Configuration file not found");
            break;
        case LoaderStatus::DEVICE_NOT_FOUND:
            LOG_ERROR("BQ34Z100 device not responding");
            break;
        default:
            LOG_ERROR("Configuration failed: %s", loader.getLastError().c_str());
            break;
    }
}
```

### Advanced Configuration with Validation
```cpp
void advancedConfigurationSetup() {
    BQ34Z100ConfigLoader loader;
    
    // Initialize
    if (!loader.initialize()) {
        return;
    }
    
    // Verify device connection first
    if (!loader.verifyDeviceConnection()) {
        LOG_ERROR("BQ34Z100 device not found");
        return;
    }
    
    // Check device state
    if (!loader.isDeviceUnsealed()) {
        LOG_WARN("Device may be sealed - some operations might fail");
    }
    
    // Get file information before loading
    GoldenImageInfo info;
    std::string config_file = "/sdcard/battery_configs/custom_profile.bq.fs";
    
    if (loader.getGoldenImageInfo(config_file, info) == LoaderStatus::SUCCESS) {
        LOG_INFO("Configuration file info:");
        LOG_INFO("  File: %s", info.filename.c_str());
        LOG_INFO("  Type: %s", info.file_type.c_str());
        LOG_INFO("  Commands: %zu total, %zu writes, %zu reads", 
                 info.total_commands, info.write_commands, info.read_commands);
        LOG_INFO("  Estimated time: %d ms", info.estimated_time_ms);
        LOG_INFO("  Has unseal sequence: %s", info.has_unseal_sequence ? "Yes" : "No");
        LOG_INFO("  Has flash programming: %s", info.has_flash_programming ? "Yes" : "No");
    }
    
    // Load configuration with verification
    LoaderStatus status = loader.loadGoldenImage(config_file, true);
    if (status == LoaderStatus::SUCCESS) {
        const auto& stats = loader.getExecutionStats();
        LOG_INFO("Configuration completed in %d ms", stats.estimated_time_ms);
    }
}
```

### Integration with Existing Battery Monitor
```cpp
class BatteryManager {
private:
    std::unique_ptr<BQ34Z100BatteryMonitor> monitor_;
    std::unique_ptr<BQ34Z100ConfigLoader> config_loader_;
    
public:
    bool initialize() {
        // Initialize battery monitor
        monitor_ = std::make_unique<BQ34Z100BatteryMonitor>();
        if (!monitor_->initialize()) {
            return false;
        }
        
        // Initialize configuration loader
        config_loader_ = std::make_unique<BQ34Z100ConfigLoader>();
        if (!config_loader_->initialize()) {
            return false;
        }
        
        // Link them together
        config_loader_->setBatteryMonitor(monitor_.get());
        
        return true;
    }
    
    bool loadConfiguration(const std::string& battery_type) {
        std::string config_file = "/sdcard/battery_configs/" + battery_type + ".bq.fs";
        
        LoaderStatus status = config_loader_->loadGoldenImage(config_file);
        if (status == LoaderStatus::SUCCESS) {
            // Force battery monitor to refresh after configuration
            monitor_->refreshAllReadings();
            return true;
        }
        
        LOG_ERROR("Failed to load battery configuration: %s", 
                  config_loader_->getLastError().c_str());
        return false;
    }
};
```

### STM32 Production Integration
```c
// STM32 main.c integration
#include "bq34z100_config_loader.h"

// Global variables
ForestryDevice::BatteryManagement::BQ34Z100ConfigLoader* g_config_loader = nullptr;

void battery_config_init(void) {
    // Create loader instance
    g_config_loader = new ForestryDevice::BatteryManagement::BQ34Z100ConfigLoader();
    
    // Initialize
    if (!g_config_loader->initialize()) {
        Error_Handler();
        return;
    }
    
    // Load production configuration
    auto status = g_config_loader->loadGoldenImage("production_config.bq.fs", true);
    
    if (status == ForestryDevice::BatteryManagement::LoaderStatus::SUCCESS) {
        // Signal success (LED, UART, etc.)
        HAL_GPIO_WritePin(CONFIG_SUCCESS_LED_GPIO_Port, CONFIG_SUCCESS_LED_Pin, GPIO_PIN_SET);
    } else {
        // Signal failure
        HAL_GPIO_WritePin(CONFIG_ERROR_LED_GPIO_Port, CONFIG_ERROR_LED_Pin, GPIO_PIN_SET);
        
        // Optional: Send error via UART
        const char* error = g_config_loader->getLastError().c_str();
        HAL_UART_Transmit(&huart1, (uint8_t*)error, strlen(error), 1000);
    }
}

void battery_config_deinit(void) {
    if (g_config_loader) {
        delete g_config_loader;
        g_config_loader = nullptr;
    }
}
```

### Custom Platform Implementation
```cpp
// Custom I2C interface for specialized hardware
class CustomI2CInterface : public I2CInterface {
private:
    // Your custom I2C driver here
    
public:
    bool initialize(int sda_pin, int scl_pin, uint32_t frequency) override {
        // Initialize your custom I2C hardware
        return true;
    }
    
    bool writeRegister(uint8_t device_addr, uint8_t reg_addr,
                       const uint8_t* data, size_t length) override {
        // Implement your custom I2C write
        return true;
    }
    
    bool readRegister(uint8_t device_addr, uint8_t reg_addr,
                      uint8_t* data, size_t length) override {
        // Implement your custom I2C read
        return true;
    }
    
    bool devicePresent(uint8_t device_addr) override {
        // Implement device detection
        return true;
    }
};

// Use custom interface
void useCustomInterface() {
    auto custom_i2c = std::make_shared<CustomI2CInterface>();
    
    BQ34Z100ConfigLoader loader(custom_i2c);
    if (loader.initialize()) {
        loader.loadGoldenImage("config.bq.fs");
    }
}
```

## Best Practices

### 1. Configuration File Management
- Store multiple battery chemistry configurations
- Use descriptive filenames (chemistry_capacity_voltage.bq.fs)
- Keep backup configurations for recovery
- Validate files before deployment

### 2. Error Handling
- Always check return status from operations
- Log detailed error information for debugging
- Implement fallback configurations for critical systems
- Monitor device state before and after programming

### 3. Performance Optimization
- Cache frequently used configurations
- Minimize I2C transaction overhead
- Use appropriate delays between operations
- Monitor programming time for production limits

### 4. System Integration
- Coordinate with power management systems
- Ensure I2C bus sharing compatibility
- Integrate with existing logging frameworks
- Maintain configuration version tracking

### 5. Testing and Validation
- Test with known good golden images
- Verify device functionality after programming
- Implement automated configuration validation
- Monitor long-term configuration stability

## Troubleshooting

### Common Issues

**File Not Found:**
- Verify SD card mounting
- Check file path and naming
- Ensure file system permissions

**I2C Communication Errors:**
- Verify wiring and pull-up resistors
- Check I2C address (0x55 for BQ34Z100)
- Monitor bus for conflicts
- Validate timing and frequency settings

**Device Not Responding:**
- Check device power supply
- Verify I2C address configuration
- Test basic communication first
- Check for device reset requirements

**Programming Failures:**
- Ensure device is unsealed
- Verify golden image file integrity
- Check for flash memory conflicts
- Monitor programming sequence timing

### Debug Features

**Logging Levels:**
- DEBUG: Detailed operation traces
- INFO: Major operation status
- WARN: Non-fatal issues
- ERROR: Critical failures

**Statistics Monitoring:**
```cpp
const auto& stats = loader.getExecutionStats();
LOG_INFO("Execution Statistics:");
LOG_INFO("  Total commands: %zu", stats.total_commands);
LOG_INFO("  Write operations: %zu", stats.write_commands);
LOG_INFO("  Read operations: %zu", stats.read_commands);
LOG_INFO("  Execution time: %d ms", stats.estimated_time_ms);
LOG_INFO("  Has unseal sequence: %s", stats.has_unseal_sequence ? "Yes" : "No");
```

## Integration with Your System

The BQ34Z100 Configuration Loader integrates seamlessly with your existing Forestry Research Device and BMS systems:

**PowerManager Integration:**
- Coordinate BQ34Z100 power sequencing
- Manage I2C bus power states
- Control device reset timing

**RTCTimeManager Integration:**
- Timestamp configuration operations
- Log configuration events
- Schedule periodic reconfiguration

**SDCardManager Integration:**
- Access golden image files
- Log configuration results
- Store backup configurations

**Main Application Integration:**
- Startup configuration loading
- Runtime reconfiguration support
- Error recovery mechanisms

The configuration loader provides a robust, production-ready solution for managing BQ34Z100 battery fuel gauge configurations across multiple platforms while maintaining consistency with your existing system architecture.