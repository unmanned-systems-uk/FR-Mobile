# Forestry Research Device - C++ Migration Guide

## Project Structure

```
ForestryResearchDevice/
├── CMakeLists.txt                 # Main build configuration
├── README.md                      # Project documentation
├── .gitignore                     # Git ignore file
├── .clang-format                  # Code formatting rules
├── include/                       # Header files
│   ├── main.h                     # Main application header
│   ├── interfaces.h               # Abstract interfaces
│   ├── config.h                   # Configuration constants
│   └── platform/                  # Platform-specific headers
│       ├── esp32/
│       │   ├── esp32_wifi.h
│       │   ├── esp32_ble.h
│       │   └── esp32_power.h
│       └── linux/
│           └── linux_mock.h
├── src/                           # Source files
│   ├── main.cpp                   # Main application
│   ├── interfaces.cpp             # Interface implementations
│   ├── utils.cpp                  # Utility functions
│   ├── scanners/                  # Scanner implementations
│   │   ├── wifi_scanner.cpp
│   │   └── ble_scanner.cpp
│   ├── hardware/                  # Hardware interfaces
│   │   ├── battery_monitor.cpp
│   │   └── power_manager.cpp
│   ├── data/                      # Data management
│   │   ├── sdcard_manager.cpp
│   │   ├── cellular_manager.cpp
│   │   └── rtc_manager.cpp
│   └── platform/                  # Platform-specific code
│       ├── esp32/
│       │   ├── esp32_wifi.cpp
│       │   ├── esp32_ble.cpp
│       │   ├── esp32_i2c.cpp
│       │   └── esp32_power.cpp
│       └── linux/
│           └── linux_mock.cpp
├── tests/                         # Unit tests (optional)
│   ├── CMakeLists.txt
│   ├── test_main.cpp
│   ├── test_scanners.cpp
│   └── test_battery.cpp
├── docs/                          # Documentation
│   ├── Doxyfile
│   ├── API.md
│   └── ARCHITECTURE.md
├── scripts/                       # Build and utility scripts
│   ├── build.sh                   # Build script
│   ├── format.sh                  # Code formatting
│   └── install_deps.sh            # Dependency installation
└── platformio.ini                # PlatformIO config (for ESP32)
```

## Key Improvements Made

### 1. **Modern C++ Structure**
- Used C++17 features (smart pointers, auto, range-based loops)
- RAII pattern for resource management
- Exception-safe code with proper error handling
- Clear separation of concerns with interfaces

### 2. **Platform Abstraction**
- Abstract interfaces for hardware components
- Platform-specific implementations separated
- Easy to port between ESP32, Linux, Windows
- Conditional compilation with preprocessor directives

### 3. **Memory Management**
- Replaced raw pointers with `std::shared_ptr` and `std::unique_ptr`
- Automatic resource cleanup in destructors
- No manual memory management required

### 4. **Error Handling**
- Exceptions instead of error codes
- Comprehensive error logging
- Graceful degradation on component failures

### 5. **Thread Safety**
- Used `std::atomic` for flags
- `std::mutex` for protecting shared data
- Safe concurrent access to scan results

### 6. **Modular Design**
- Each component is a separate class
- Dependency injection for testability
- Easy to mock components for testing

## Build Instructions

### For Linux Development/Testing

```bash
# Install dependencies
sudo apt update
sudo apt install build-essential cmake git

# Clone and build
git clone <repository-url>
cd ForestryResearchDevice
mkdir build && cd build
cmake ..
make -j$(nproc)

# Run
./ForestryResearchDevice
```

### For ESP32 with PlatformIO

```bash
# Install PlatformIO
pip install platformio

# Build and upload
pio run
pio run --target upload

# Monitor serial output
pio device monitor
```

### For ESP32 with Arduino IDE

1. Copy the header files to your Arduino sketch folder
2. Install required libraries through Library Manager
3. Configure board settings for your ESP32
4. Compile and upload

## Configuration

Edit `include/config.h` to customize:

```cpp
namespace Config {
    const std::string DEVICE_ID = "Your_Device_ID";
    const std::string LOCATION = "Your_Location";
    const double LATITUDE = 55.5;
    const double LONGITUDE = -2.84;
    
    const int SCAN_INTERVAL_MS = 10000;      // 10 seconds
    const int WIFI_SCAN_TIME_MS = 600;       // 600ms
    const int BLE_SCAN_TIME_MS = 60;         // 60ms
    const int LTE_TIME_MINUTES = 200;        // 200 minutes
}
```

## Usage Examples

### Basic Usage

```cpp
#include "main.h"

int main() {
    ForestryResearchDevice device;
    
    if (!device.initialize()) {
        return -1;
    }
    
    device.run(); // Runs until shutdown
    return 0;
}
```

### Custom Configuration

```cpp
// Create device with custom settings
ForestryResearchDevice device;
device.setScanInterval(5000);        // 5 second intervals
device.setUploadInterval(120);       // Upload every 2 hours
device.enableNightSleep(true);       // Enable night time sleep
device.setDebugMode(true);          // Enable debug output
```

### Component Access

```cpp
// Access individual components
auto batteryStatus = device.getBatteryMonitor()->readBatteryStatus();
auto scanResults = device.getWiFiScanner()->getResults();
float sdCapacity = device.getStorage()->getRemainingCapacityPercent();
```

## Migration Benefits

1. **Maintainability**: Clear code structure, easier to debug and modify
2. **Portability**: Runs on multiple platforms (ESP32, Linux, Windows)
3. **Testability**: Each component can be unit tested independently
4. **Reliability**: Better error handling and resource management
5. **Performance**: Efficient memory usage and optimized scanning
6. **Extensibility**: Easy to add new features and sensors

## Platform-Specific Notes

### ESP32
- Uses ESP-IDF or Arduino framework
- Hardware-specific implementations for WiFi, BLE, I2C
- Deep sleep functionality preserved
- Watchdog timer support

### Linux
- Mock implementations for testing
- File-based storage instead of SD card
- Can simulate scanning operations
- Useful for algorithm development

### Windows
- Similar to Linux but with Windows-specific file paths
- Could integrate with Windows Bluetooth/WiFi APIs
- Good for data analysis and visualization tools

This structure provides a solid foundation for migrating your Arduino code to a more maintainable and portable C++ application while preserving all the original functionality.