cmake_minimum_required(VERSION 3.16)

# Project definition
project(ForestryResearchDevice 
    VERSION 1.0.0
    DESCRIPTION "C++ Forestry Research Device for WiFi/BLE Scanning"
    LANGUAGES CXX
)

# Set C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Build configuration
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# Platform detection
if(DEFINED ESP32_PLATFORM)
    set(TARGET_PLATFORM "ESP32")
    add_definitions(-DESP32_PLATFORM)
elseif(WIN32)
    set(TARGET_PLATFORM "Windows")
    add_definitions(-DWINDOWS_PLATFORM)
elseif(UNIX AND NOT APPLE)
    set(TARGET_PLATFORM "Linux")
    add_definitions(-DLINUX_PLATFORM)
elseif(APPLE)
    set(TARGET_PLATFORM "macOS")
    add_definitions(-DMACOS_PLATFORM)
else()
    set(TARGET_PLATFORM "Unknown")
    add_definitions(-DDEVELOPMENT_PLATFORM)
endif()

message(STATUS "Building for platform: ${TARGET_PLATFORM}")
message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")

# Compiler-specific flags
if(MSVC)
    add_compile_options(/W4 /WX- /permissive-)
    add_definitions(-D_CRT_SECURE_NO_WARNINGS)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
    if(CMAKE_BUILD_TYPE STREQUAL "Debug")
        add_compile_options(-g -O0 -fsanitize=address,undefined)
        add_link_options(-fsanitize=address,undefined)
    else()
        add_compile_options(-O3 -DNDEBUG)
    endif()
endif()

# Include directories
include_directories(
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src
)

# Define source files by component
set(SCANNER_SOURCES
    src/scanners/wifi_scanner.cpp
    src/scanners/ble_scanner.cpp
)

set(HARDWARE_SOURCES
    src/hardware/bq34z100_battery_monitor.cpp
    src/hardware/power_manager.cpp
)

set(DATA_SOURCES
    src/data/sdcard_manager.cpp
    src/data/cellular_manager.cpp
    src/data/rtc_time_manager.cpp
)

set(UTILS_SOURCES
    src/utils/logger.cpp
    src/utils/utils.cpp
)

# Platform-specific sources
if(TARGET_PLATFORM STREQUAL "ESP32")
    set(PLATFORM_SOURCES
        src/platform/esp32/esp32_wifi.cpp
        src/platform/esp32/esp32_ble.cpp
        src/platform/esp32/esp32_i2c.cpp
        src/platform/esp32/esp32_power.cpp
    )
else()
    set(PLATFORM_SOURCES
        src/platform/development/mock_implementations.cpp
    )
endif()

# All library sources
set(LIB_SOURCES
    ${SCANNER_SOURCES}
    ${HARDWARE_SOURCES}
    ${DATA_SOURCES}
    ${UTILS_SOURCES}
    ${PLATFORM_SOURCES}
)

# Create the main library
add_library(ForestryDeviceLib STATIC ${LIB_SOURCES})

# Set library properties
set_target_properties(ForestryDeviceLib PROPERTIES
    OUTPUT_NAME "forestry_device"
    VERSION ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR}
)

# Platform-specific linking
if(TARGET_PLATFORM STREQUAL "ESP32")
    # ESP32-specific libraries would be linked here
    # This would typically be handled by PlatformIO or ESP-IDF
    message(STATUS "ESP32 platform detected - ensure ESP-IDF environment is configured")
    
elseif(TARGET_PLATFORM STREQUAL "Linux")
    # Linux-specific libraries
    find_package(Threads REQUIRED)
    target_link_libraries(ForestryDeviceLib Threads::Threads)
    
    # Optional: BlueZ for Bluetooth (if available)
    find_library(BLUETOOTH_LIB bluetooth)
    if(BLUETOOTH_LIB)
        target_link_libraries(ForestryDeviceLib ${BLUETOOTH_LIB})
        add_definitions(-DBLUETOOTH_AVAILABLE)
        message(STATUS "BlueZ Bluetooth library found")
    endif()
    
elseif(TARGET_PLATFORM STREQUAL "Windows")
    # Windows-specific libraries
    target_link_libraries(ForestryDeviceLib ws2_32 iphlpapi)
    
endif()

# Main executable
add_executable(ForestryDevice src/main.cpp)
target_link_libraries(ForestryDevice ForestryDeviceLib)

# Set executable properties
set_target_properties(ForestryDevice PROPERTIES
    OUTPUT_NAME "forestry_device"
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)

# Test executable (optional, only built if BUILD_TESTS is ON)
option(BUILD_TESTS "Build test suite" ON)

if(BUILD_TESTS)
    add_executable(ForestryDeviceTests test_main.cpp)
    target_link_libraries(ForestryDeviceTests ForestryDeviceLib)
    
    set_target_properties(ForestryDeviceTests PROPERTIES
        OUTPUT_NAME "forestry_device_tests"
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
    )
    
    message(STATUS "Test suite enabled")
endif()

# Installation rules
if(NOT TARGET_PLATFORM STREQUAL "ESP32")
    install(TARGETS ForestryDevice ForestryDeviceLib
        RUNTIME DESTINATION bin
        LIBRARY DESTINATION lib
        ARCHIVE DESTINATION lib
    )
    
    install(DIRECTORY include/
        DESTINATION include/forestry_device
        FILES_MATCHING PATTERN "*.h"
    )
    
    if(BUILD_TESTS)
        install(TARGETS ForestryDeviceTests
            RUNTIME DESTINATION bin
        )
    endif()
endif()

# Custom targets for development
add_custom_target(clean-all
    COMMAND ${CMAKE_COMMAND} -E remove_directory ${CMAKE_BINARY_DIR}
    COMMENT "Cleaning all build files"
)

# Code formatting (if clang-format is available)
find_program(CLANG_FORMAT clang-format)
if(CLANG_FORMAT)
    file(GLOB_RECURSE ALL_SOURCE_FILES
        ${CMAKE_SOURCE_DIR}/src/*.cpp
        ${CMAKE_SOURCE_DIR}/src/*.h
        ${CMAKE_SOURCE_DIR}/include/*.h
    )
    
    add_custom_target(format
        COMMAND ${CLANG_FORMAT} -i ${ALL_SOURCE_FILES}
        COMMENT "Formatting source code with clang-format"
    )
    
    message(STATUS "Code formatting target available: make format")
endif()

# Documentation generation (if Doxygen is available)
find_package(Doxygen QUIET)
if(DOXYGEN_FOUND)
    set(DOXYGEN_IN ${CMAKE_SOURCE_DIR}/Doxyfile.in)
    set(DOXYGEN_OUT ${CMAKE_BINARY_DIR}/Doxyfile)
    
    if(EXISTS ${DOXYGEN_IN})
        configure_file(${DOXYGEN_IN} ${DOXYGEN_OUT} @ONLY)
        
        add_custom_target(docs
            COMMAND ${DOXYGEN_EXECUTABLE} ${DOXYGEN_OUT}
            WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
            COMMENT "Generating API documentation with Doxygen"
            VERBATIM
        )
        
        message(STATUS "Documentation target available: make docs")
    endif()
endif()

# Print build summary
message(STATUS "=== Build Configuration Summary ===")
message(STATUS "Project: ${PROJECT_NAME} v${PROJECT_VERSION}")
message(STATUS "Platform: ${TARGET_PLATFORM}")
message(STATUS "Build Type: ${CMAKE_BUILD_TYPE}")
message(STATUS "C++ Standard: ${CMAKE_CXX_STANDARD}")
message(STATUS "Build Tests: ${BUILD_TESTS}")
message(STATUS "Install Prefix: ${CMAKE_INSTALL_PREFIX}")

# Detailed source file information
message(STATUS "Source Files by Component:")
message(STATUS "  Scanners: ${SCANNER_SOURCES}")
message(STATUS "  Hardware: ${HARDWARE_SOURCES}")
message(STATUS "  Data: ${DATA_SOURCES}")
message(STATUS "  Utils: ${UTILS_SOURCES}")
message(STATUS "  Platform: ${PLATFORM_SOURCES}")
message(STATUS "================================")

# Build targets summary
message(STATUS "Available Targets:")
message(STATUS "  ForestryDevice     - Main application")
message(STATUS "  ForestryDeviceLib  - Static library")
if(BUILD_TESTS)
    message(STATUS "  ForestryDeviceTests- Test suite")
endif()
if(CLANG_FORMAT)
    message(STATUS "  format             - Code formatting")
endif()
if(DOXYGEN_FOUND)
    message(STATUS "  docs               - Generate documentation")
endif()
message(STATUS "  clean-all          - Clean all build files")
message(STATUS "================================")

# Development helper information
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    message(STATUS "Debug build configuration:")
    message(STATUS "  - Address sanitizer enabled")
    message(STATUS "  - Debug symbols included")
    message(STATUS "  - Optimizations disabled")
endif()

# Platform-specific build notes
if(TARGET_PLATFORM STREQUAL "ESP32")
    message(STATUS "ESP32 Build Notes:")
    message(STATUS "  - Ensure ESP-IDF environment is configured")
    message(STATUS "  - Use 'idf.py build' for ESP32 compilation")
    message(STATUS "  - This CMakeLists.txt provides structure reference")
    
elseif(TARGET_PLATFORM STREQUAL "Linux")
    message(STATUS "Linux Build Notes:")
    message(STATUS "  - Install build dependencies: sudo apt install build-essential cmake")
    message(STATUS "  - Optional: install libbluetooth-dev for Bluetooth support")
    message(STATUS "  - Build: mkdir build && cd build && cmake .. && make")
    
elseif(TARGET_PLATFORM STREQUAL "Windows")
    message(STATUS "Windows Build Notes:")
    message(STATUS "  - Use Visual Studio 2019+ or MinGW-w64")
    message(STATUS "  - Build: mkdir build && cd build && cmake .. && cmake --build .")
    message(STATUS "  - Ensure Windows SDK is installed")
    
endif()

# Quick start instructions
message(STATUS "Quick Start:")
message(STATUS "  mkdir build && cd build")
message(STATUS "  cmake ..")
message(STATUS "  make -j$(nproc)  # Linux/macOS")
message(STATUS "  cmake --build . --config Release  # Windows")
message(STATUS "  ./bin/forestry_device  # Run main application")
if(BUILD_TESTS)
    message(STATUS "  ./bin/forestry_device_tests  # Run test suite")
endif()
message(STATUS "================================")