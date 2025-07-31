# Complete Installation Guide for Forestry Research Device
## ESP32 Development Environment Setup on Windows 11

This guide will walk you through setting up the complete development environment for the Forestry Research Device project, including Python, ESP-IDF, drivers, and VS Code.

---

## Table of Contents
1. [System Requirements](#system-requirements)
2. [Python Installation](#python-installation)
3. [ESP-IDF Installation](#esp-idf-installation)
4. [USB Driver Installation](#usb-driver-installation)
5. [Project Setup](#project-setup)
6. [VS Code Configuration](#vs-code-configuration)
7. [Building and Flashing](#building-and-flashing)
8. [Troubleshooting](#troubleshooting)

---

## System Requirements

- **Operating System**: Windows 11 (64-bit)
- **RAM**: Minimum 4GB (8GB recommended)
- **Disk Space**: At least 4GB free space
- **Internet Connection**: Required for downloading tools
- **USB Port**: For connecting ESP32 device

---

## Step 1: Python Installation

### 1.1 Download Python
1. Visit [https://www.python.org/downloads/](https://www.python.org/downloads/)
2. Download **Python 3.12.10** (or latest 3.12.x)
   - Direct link: [Python 3.12.10 64-bit](https://www.python.org/ftp/python/3.12.10/python-3.12.10-amd64.exe)

### 1.2 Install Python
1. Run the installer
2. **IMPORTANT**: ✅ Check "Add python.exe to PATH" at the bottom of the first screen
3. Click "Customize installation"
4. Ensure these are checked:
   - ✅ pip
   - ✅ tcl/tk and IDLE
   - ✅ Python test suite
   - ✅ py launcher
5. Click "Next"
6. On Advanced Options:
   - ✅ Install for all users
   - ✅ Add Python to environment variables
   - ✅ Precompile standard library
7. Click "Install"

### 1.3 Verify Python Installation
Open Command Prompt (Win+R, type `cmd`, press Enter):
```cmd
python --version
pip --version
```

Expected output:
```
Python 3.12.10
pip 24.x.x from ...
```

---

## Step 2: ESP-IDF Installation

### 2.1 Download ESP-IDF Tools Installer
1. Visit [ESP-IDF Tools Installer](https://dl.espressif.com/dl/esp-idf/)
2. Download the **Offline Installer** (recommended):
   - `esp-idf-tools-setup-offline-5.3.1.exe` (or latest 5.3.x)

### 2.2 Run ESP-IDF Installer
1. Run the installer as Administrator
2. Select installation options:
   - ✅ Select "Download ESP-IDF"
   - Choose version: **v5.3.1** (or latest stable)
   - Installation directory: `C:\Espressif`
3. Select ESP32 targets:
   - ✅ ESP32
   - ✅ ESP32-S2 (optional)
   - ✅ ESP32-S3 (optional)
   - ✅ ESP32-C3 (optional)
4. Select tools to install:
   - ✅ CMake
   - ✅ Ninja  
   - ✅ OpenOCD
   - ✅ ESP-IDF Tools
5. Driver installation:
   - ✅ Install ESP32 drivers
6. Start menu shortcuts:
   - ✅ Create Start Menu shortcuts
7. Click "Install" and wait (10-15 minutes)

### 2.3 Verify ESP-IDF Installation
1. Open "ESP-IDF 5.3 CMD" from Start Menu
2. Run:
```cmd
idf.py --version
```

Expected output:
```
ESP-IDF v5.3.1
```

---

## Step 3: USB Driver Installation

### 3.1 Identify Your ESP32 Board
Common USB-to-Serial chips:
- **CP2102/CP2104**: Silicon Labs
- **CH340/CH341**: WCH
- **FT232**: FTDI

### 3.2 Install Drivers

#### For CP2102/CP2104 (most common):
1. Download from [Silicon Labs](https://www.silabs.com/developers/usb-to-uart-bridge-vcp-drivers)
2. Extract and run the installer
3. Restart computer

#### For CH340/CH341:
1. Download from [WCH](http://www.wch-ic.com/downloads/CH341SER_ZIP.html)
2. Extract and run `SETUP.EXE`
3. Restart computer

### 3.3 Verify Driver Installation
1. Connect ESP32 to USB
2. Open Device Manager (Win+X, select Device Manager)
3. Look under "Ports (COM & LPT)"
4. You should see "USB-SERIAL CH340 (COMx)" or similar
5. Note the COM port number (e.g., COM3)

---

## Step 4: Project Setup

### 4.1 Clone or Download Project
Option A - Using Git:
```cmd
cd C:\Users\%USERNAME%\Documents
git clone https://github.com/yourusername/ForestryResearchDevice.git
cd ForestryResearchDevice
```

Option B - Download ZIP:
1. Download project ZIP file
2. Extract to `C:\Users\%USERNAME%\Documents\ForestryResearchDevice`

### 4.2 Project Structure Verification
Ensure your project has this structure:
```
ForestryResearchDevice/
├── CMakeLists.txt          (4-line ESP-IDF version)
├── main/
│   └── CMakeLists.txt
├── include/
│   ├── interfaces.h
│   ├── main.h
│   └── ... (other headers)
├── src/
│   ├── main.cpp
│   └── ... (source files)
└── sdkconfig.defaults
```

### 4.3 First Build
1. Open "ESP-IDF 5.3 CMD" from Start Menu
2. Navigate to project:
```cmd
cd C:\Users\%USERNAME%\Documents\ForestryResearchDevice
```
3. Set target:
```cmd
idf.py set-target esp32
```
4. Build project:
```cmd
idf.py build
```

First build will take 10-20 minutes.

---

## Step 5: VS Code Configuration (Recommended)

### 5.1 Install VS Code
1. Download from [https://code.visualstudio.com/](https://code.visualstudio.com/)
2. Install with default options

### 5.2 Install ESP-IDF Extension
1. Open VS Code
2. Press `Ctrl+Shift+X` (Extensions)
3. Search for "ESP-IDF"
4. Install "Espressif IDF" extension
5. Restart VS Code

### 5.3 Configure ESP-IDF Extension
1. Press `Ctrl+Shift+P`
2. Type "ESP-IDF: Configure ESP-IDF Extension"
3. Select:
   - Use existing ESP-IDF: `C:\Espressif\frameworks\esp-idf-v5.3.1`
   - Python: `C:\Espressif\python_env\idf5.3_py3.12_env\Scripts\python.exe`
4. Click "Install" to verify

### 5.4 Open Project in VS Code
1. File → Open Folder
2. Select `ForestryResearchDevice` folder
3. VS Code will detect ESP-IDF project

---

## Step 6: Building and Flashing

### 6.1 Using Command Line

#### Build:
```cmd
idf.py build
```

#### Flash:
```cmd
idf.py -p COM3 flash
```
Replace `COM3` with your actual port.

#### Monitor:
```cmd
idf.py -p COM3 monitor
```
Exit monitor: `Ctrl+]`

#### Combined Flash & Monitor:
```cmd
idf.py -p COM3 flash monitor
```

### 6.2 Using VS Code
1. Click ESP-IDF icon in sidebar
2. Select your COM port
3. Click:
   - 🔨 Build
   - ⚡ Flash
   - 📟 Monitor

### 6.3 Create Convenience Scripts
Create these batch files in your project root:

**build.bat:**
```batch
@echo off
call C:\Espressif\frameworks\esp-idf-v5.3.1\export.bat
idf.py build
pause
```

**flash.bat:**
```batch
@echo off
call C:\Espressif\frameworks\esp-idf-v5.3.1\export.bat
set /p PORT="Enter COM port (e.g., COM3): "
idf.py -p %PORT% flash monitor
pause
```

**clean.bat:**
```batch
@echo off
call C:\Espressif\frameworks\esp-idf-v5.3.1\export.bat
idf.py fullclean
pause
```

---

## Step 7: Configuration

### 7.1 Project Configuration
Run menuconfig to adjust settings:
```cmd
idf.py menuconfig
```

Important settings:
- **Serial flasher config** → Default serial port
- **Component config** → ESP32-specific → CPU frequency
- **Component config** → FreeRTOS → Tick rate (Hz)

### 7.2 Update Device Configuration
Edit `include/main.h`:
```cpp
namespace Config
{
    const std::string ASSET_ID = "your-device-id";
    const std::string DEVICE_ID = "Device01";
    const std::string LOCATION = "Forest_Location_01";
    const double LATITUDE = 55.5;    // Your latitude
    const double LONGITUDE = -2.84;  // Your longitude
    // ... other settings
}
```

---

## Troubleshooting

### Issue: "idf.py not recognized"
**Solution**: Always use "ESP-IDF CMD" from Start Menu, or run:
```cmd
C:\Espressif\frameworks\esp-idf-v5.3.1\export.bat
```

### Issue: "No module named 'serial'"
**Solution**: Install pyserial:
```cmd
pip install pyserial
```

### Issue: "Failed to connect to ESP32"
**Solutions**:
1. Check USB cable (use data cable, not charge-only)
2. Install correct drivers
3. Try different USB port
4. Hold BOOT button while flashing starts

### Issue: "Permission denied on COM port"
**Solutions**:
1. Close any serial monitors
2. Unplug and replug ESP32
3. Use a different COM port

### Issue: Build errors with includes
**Solution**: Update includes from Arduino to ESP-IDF style:
```cpp
// Arduino style (OLD)
#include <WiFi.h>
#include <Wire.h>

// ESP-IDF style (NEW)
#include "esp_wifi.h"
#include "driver/i2c.h"
```

---

## Next Steps

1. **Test the Installation**:
   - Build the project
   - Flash to ESP32
   - Monitor output

2. **Configure Your Device**:
   - Update device IDs in `main.h`
   - Set correct GPS coordinates
   - Configure WiFi/cellular credentials

3. **Development Workflow**:
   - Make code changes
   - Run `idf.py build`
   - Flash and test
   - Use `idf.py monitor` for debugging

---

## Useful Links

- [ESP-IDF Documentation](https://docs.espressif.com/projects/esp-idf/en/stable/esp32/)
- [ESP32 Hardware Reference](https://www.espressif.com/en/products/socs/esp32)
- [FreeRTOS Documentation](https://www.freertos.org/Documentation/RTOS_book.html)
- [Project Repository](https://github.com/yourusername/ForestryResearchDevice)

---

## Support

If you encounter issues:
1. Check the [Troubleshooting](#troubleshooting) section
2. Review ESP-IDF [build errors](https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-guides/build-system.html)
3. Post in [ESP32 Forum](https://www.esp32.com/)
4. Create an issue in the project repository

---

*Last updated: November 2024*
*ESP-IDF Version: 5.3.1*
*Python Version: 3.12.x*