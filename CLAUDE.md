# Project Context and Todo List

## Project Overview
Forest Research Firmware v4 - Multi-platform IoT device firmware for environmental monitoring.

## Build Targets
- ESP32 (current primary target)
- Raspberry Pi (future support)
- Other embedded Linux platforms (future)

## Platform-Specific Implementation Notes

### ESP32
- Uses ESP-IDF drivers for UART, I2C, SPI
- Built-in cellular module via UART
- SD card via SPI

### Raspberry Pi (Future)
- Will use built-in WiFi module
- USB-attached SIM7600 cellular modem
- Different GPIO mappings and drivers

## Todo List

### Cross-Platform Compatibility
- [x] Make project fully cross-platform compatible (basic structure)
- [x] Add proper platform abstractions for:
  - [x] GPIO operations
  - [x] UART/Serial communication  
  - [x] I2C communication
  - [x] SPI communication
  - [x] WiFi interfaces
  - [x] Cellular modem interfaces
- [ ] Create platform-specific configuration files
- [ ] Abstract hardware pin mappings per platform

### Code Quality
- [x] Replace Arduino functions with ESP-IDF equivalents
- [x] Ensure all ESP32 drivers are within #ifdef ESP32_PLATFORM
- [x] Add placeholders for other target hardware (Raspberry Pi, etc.)

## Important Commands
- Build: `idf.py build`
- Flash: `idf.py flash`
- Monitor: `idf.py monitor`

## Recent Changes
- Replaced HardwareSerial with ESP-IDF UART driver in cellular_manager.cpp
- Replaced Wire library with ESP-IDF I2C driver in rtc_time_manager.cpp
- All Arduino functions have been replaced with ESP-IDF equivalents
- Added proper platform conditionals (#ifdef ESP32_PLATFORM) to all source files
- Added Raspberry Pi platform placeholders with appropriate includes:
  - Linux GPIO, I2C, UART, and networking libraries
  - Bluetooth and WiFi scanning libraries
  - USB serial device handling for cellular modem
- All ESP32-specific drivers are now properly isolated within platform ifdefs