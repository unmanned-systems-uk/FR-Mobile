RTC Time Manager Implementation Features:
1. Power Management Integration

Automatically enables 5V power supply through PowerManager before RTC access
Waits for power stabilization before I2C communication
Graceful handling if no PowerManager is provided

2. DS1307 RTC Communication

Full I2C implementation with proper register access
BCD (Binary Coded Decimal) conversion for time values
Clock halt bit detection and automatic oscillator start
Batch read/write operations for efficiency

3. Time Synchronization

Network time synchronization with drift tracking
Supports ISO8601 format (YYYY-MM-DDTHH:MM:SS)
Calculates and tracks time drift between syncs
Persists sync statistics to DS1307's battery-backed RAM

4. Sleep Schedule Management

Configurable day/night sleep durations
Automatic detection of day/night based on current time
Returns sleep duration in microseconds for PowerManager

5. EEPROM Persistence

Uses DS1307's 56 bytes of battery-backed RAM
Stores sync statistics and drift information
Survives power cycles with battery backup

6. Time Validation

Validates time format and reasonable date ranges
Detects uninitialized RTC (default 2000-01-01)
Ensures year is within DS1307 range (2000-2099)

7. Comprehensive Logging

Follows your established logging patterns
Debug information for I2C operations
Sync statistics and drift reporting

Usage Example:
cpp// Create power manager and RTC manager
auto powerManager = std::make_shared<PowerManager>(15);
powerManager->initialize();

RTCTimeManager rtcManager(powerManager);
rtcManager.initialize();

// Get current time
std::string currentTime = rtcManager.getCurrentDateTime();
LOG_INFO("Current RTC time: " + currentTime);

// Sync with network time
std::string networkTime = "2025-07-29T14:30:00";  // From cellular network
rtcManager.setTimeFromNetwork(networkTime);

// Configure sleep schedule
SleepSchedule schedule;
schedule.nightStartHour = 22;  // 10 PM
schedule.nightEndHour = 6;     // 6 AM
schedule.nightSleepMinutes = 30;  // 30 minutes sleep at night
schedule.daySleepMinutes = 5;     // 5 minutes sleep during day
schedule.enabled = true;
rtcManager.setSleepSchedule(schedule);

// Get appropriate sleep duration based on current time
uint64_t sleepDuration = rtcManager.getNightSleepDuration(currentTime);
powerManager->enterDeepSleep(sleepDuration);

Key Implementation Details:
DS1307 Register Map:

0x00-0x06: Time/Date registers (seconds to year)
0x07: Control register
0x08-0x3F: 56 bytes of battery-backed RAM for user data

Time Format:

Uses 24-hour format
Years stored as offset from 2000 (0-99 = 2000-2099)
BCD encoding for all time values

Drift Tracking:

Calculates difference between RTC time and network time
Accumulates total drift over multiple syncs
Provides average drift statistics

Sleep Duration Calculation:

Automatically determines day/night based on hour
Returns microseconds for direct use with PowerManager
Handles schedules that cross midnight

Integration with Other Components:

PowerManager - Ensures 5V is enabled for RTC operation
CellularManager - Will provide network time for synchronization
Main Application - Uses sleep schedule for power optimization
SDCardManager - Can use RTC timestamps for file operations