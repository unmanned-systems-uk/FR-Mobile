Key Features of the PowerManager Implementation:
1. 5V Power Control

Proper GPIO15 control with LOW = enabled, HIGH = disabled
GPIO hold functionality to maintain state during deep sleep
Safe initialization starting with 5V disabled
Proper sequencing with 1-second delay for power stabilization

2. Sleep Mode Management

Deep sleep with configurable wakeup sources
Light sleep for lower latency wakeup
Sleep duration validation and limits
Proper statistics tracking for duty cycle monitoring

3. Peripheral Power Management

Individual control for WiFi, Bluetooth, cellular, SD card, and sensors
Proper shutdown sequencing (peripherals first, then 5V)
Power-up sequencing (5V first, then peripherals)

4. Battery-Aware Power Management

Low battery threshold triggers power saving
Critical battery threshold triggers emergency mode
Emergency shutdown procedures with logging

5. Watchdog Timer Support

Configurable timeout with automatic reset
Integration with sleep modes
Proper cleanup on shutdown

6. Comprehensive Logging

Follows your established logging patterns
Critical events are logged immediately
Detailed diagnostic information

7. Platform Abstraction

Full ESP32 implementation with proper ESP-IDF calls
Development platform simulation for testing
Clean separation of platform-specific code

8. Safety Features

Health check functionality
Emergency mode for critical situations
Proper GPIO hold configuration for deep sleep
Memory monitoring

Integration Points:
The PowerManager is now ready to be used by other components:

RTC Time Manager can call enablePeripherals() or set5VSupply(true) to power up the RTC
Cellular Manager can use the same methods to control modem power
Main Application can use sleep modes for power optimization
Battery Monitor can call checkBatteryLevel() to trigger power policies

Usage Example:
cpp// Initialize power manager
PowerManager powerManager(15);  // GPIO 15 for 5V control
powerManager.initialize();

// Enable 5V for RTC and cellular
powerManager.set5VSupply(true);

// Or enable all configured peripherals
powerManager.enablePeripherals();

// Enter deep sleep for 10 minutes
powerManager.enterDeepSleep(600000000ULL);  // 600 seconds in microseconds

// Check battery and apply policies
powerManager.checkBatteryLevel(batteryPercent);