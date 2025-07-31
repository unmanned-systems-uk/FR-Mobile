@echo off
setlocal enabledelayedexpansion

REM Forestry Research Device - Windows Build Script
REM Supports Visual Studio and MinGW compilers

set PROJECT_NAME=ForestryResearchDevice
set BUILD_DIR=build
set INSTALL_DIR=install
set BUILD_TYPE=Release
set ENABLE_TESTS=OFF
set VERBOSE=OFF
set CLEAN_FIRST=false
set COMMAND=build

REM Parse command line arguments
:parse_args
if "%~1"=="" goto end_parse
if /i "%~1"=="--debug" (
    set BUILD_TYPE=Debug
    shift
    goto parse_args
)
if /i "%~1"=="--release" (
    set BUILD_TYPE=Release
    shift
    goto parse_args
)
if /i "%~1"=="--tests" (
    set ENABLE_TESTS=ON
    shift
    goto parse_args
)
if /i "%~1"=="--no-tests" (
    set ENABLE_TESTS=OFF
    shift
    goto parse_args
)
if /i "%~1"=="--verbose" (
    set VERBOSE=ON
    shift
    goto parse_args
)
if /i "%~1"=="--clean-first" (
    set CLEAN_FIRST=true
    shift
    goto parse_args
)
if /i "%~1"=="build" (
    set COMMAND=build
    shift
    goto parse_args
)
if /i "%~1"=="clean" (
    set COMMAND=clean
    shift
    goto parse_args
)
if /i "%~1"=="test" (
    set COMMAND=test
    set ENABLE_TESTS=ON
    shift
    goto parse_args
)
if /i "%~1"=="install" (
    set COMMAND=install
    shift
    goto parse_args
)
if /i "%~1"=="help" (
    set COMMAND=help
    shift
    goto parse_args
)
echo [ERROR] Unknown option: %~1
goto show_usage

:end_parse

REM Show header
echo ============================================
echo     %PROJECT_NAME% Build Script (Windows)
echo ============================================

REM Execute command
if /i "%COMMAND%"=="help" goto show_usage
if /i "%COMMAND%"=="clean" goto clean_build
if /i "%COMMAND%"=="build" goto build_project
if /i "%COMMAND%"=="test" goto test_project
if /i "%COMMAND%"=="install" goto install_project

echo [ERROR] Unknown command: %COMMAND%
goto show_usage

:show_usage
echo Usage: %0 [OPTIONS] [COMMAND]
echo.
echo Commands:
echo   build     Configure and build the project (default)
echo   clean     Clean build directory
echo   test      Build and run tests
echo   install   Build and install the project
echo   help      Show this help message
echo.
echo Options:
echo   --debug           Build in Debug mode
echo   --release         Build in Release mode (default)
echo   --tests           Enable building tests
echo   --no-tests        Disable building tests
echo   --verbose         Enable verbose build output
echo   --clean-first     Clean before building
echo.
echo Examples:
echo   %0                        # Build in Release mode
echo   %0 --debug --tests        # Debug build with tests
echo   %0 clean                  # Clean build directory
echo   %0 test                   # Build and run tests
echo   %0 --clean-first build    # Clean then build
goto end

:clean_build
echo [INFO] Cleaning build directory...
if exist "%BUILD_DIR%" (
    rmdir /s /q "%BUILD_DIR%"
    echo [SUCCESS] Build directory cleaned
)
if exist "%INSTALL_DIR%" (
    rmdir /s /q "%INSTALL_DIR%"
    echo [SUCCESS] Install directory cleaned
)
if /i "%COMMAND%"=="clean" goto end
goto check_dependencies

:check_dependencies
echo [INFO] Checking build dependencies...

REM Check for CMake
cmake --version >nul 2>&1
if errorlevel 1 (
    echo [ERROR] CMake not found. Please install CMake 3.16 or later.
    goto end
)

REM Get CMake version
for /f "tokens=3" %%i in ('cmake --version 2^>nul') do (
    echo [INFO] Found CMake version: %%i
    goto cmake_found
)
:cmake_found

REM Check for Visual Studio
where cl >nul 2>&1
if not errorlevel 1 (
    echo [INFO] Found Visual Studio compiler (cl.exe)
    set GENERATOR="Visual Studio 16 2019"
    goto compiler_found
)

REM Check for MinGW
where g++ >nul 2>&1
if not errorlevel 1 (
    echo [INFO] Found MinGW compiler (g++.exe)
    set GENERATOR="MinGW Makefiles"
    goto compiler_found
)

echo [ERROR] No suitable C++ compiler found.
echo Please install Visual Studio 2019+ or MinGW-w64.
goto end

:compiler_found
goto configure_build

:configure_build
echo [INFO] Configuring build...
echo [INFO] Build type: %BUILD_TYPE%

REM Create build directory
if not exist "%BUILD_DIR%" mkdir "%BUILD_DIR%"
cd "%BUILD_DIR%"

REM Configure with CMake
set CMAKE_ARGS=-DCMAKE_BUILD_TYPE=%BUILD_TYPE% -DCMAKE_INSTALL_PREFIX=../%INSTALL_DIR%

if defined GENERATOR (
    set CMAKE_ARGS=%CMAKE_ARGS% -G %GENERATOR%
)

if /i "%ENABLE_TESTS%"=="ON" (
    set CMAKE_ARGS=%CMAKE_ARGS% -DBUILD_TESTS=ON
    echo [INFO] Tests enabled
)

if /i "%VERBOSE%"=="ON" (
    set CMAKE_ARGS=%CMAKE_ARGS% -DCMAKE_VERBOSE_MAKEFILE=ON
)

REM Run CMake configuration
cmake %CMAKE_ARGS% ..
if errorlevel 1 (
    echo [ERROR] CMake configuration failed
    cd ..
    goto end
)

echo [SUCCESS] Build configured successfully
cd ..
goto build_main

:build_main
echo [INFO] Building project...
cd "%BUILD_DIR%"

REM Determine number of parallel jobs (use all available cores)
set JOBS=%NUMBER_OF_PROCESSORS%
echo [INFO] Using %JOBS% parallel jobs

REM Build the project
cmake --build . --config %BUILD_TYPE% --parallel %JOBS%
if errorlevel 1 (
    echo [ERROR] Build failed
    cd ..
    goto end
)

echo [SUCCESS] Build completed successfully
cd ..
goto build_complete

:build_complete
if /i "%COMMAND%"=="build" goto print_summary
if /i "%COMMAND%"=="test" goto run_tests
if /i "%COMMAND%"=="install" goto install_main
goto print_summary

:build_project
if /i "%CLEAN_FIRST%"=="true" call :clean_build
call :check_dependencies
goto build_complete

:test_project
if /i "%CLEAN_FIRST%"=="true" call :clean_build
call :check_dependencies
goto run_tests

:install_project
if /i "%CLEAN_FIRST%"=="true" call :clean_build
call :check_dependencies
goto install_main

:run_tests
echo [INFO] Running tests...

REM Check if test executable exists
if exist "%BUILD_DIR%\bin\forestry_device_tests.exe" (
    cd "%BUILD_DIR%"
    bin\forestry_device_tests.exe
    if errorlevel 1 (
        echo [ERROR] Tests failed
        cd ..
        goto end
    )
    echo [SUCCESS] Tests completed
    cd ..
) else if exist "%BUILD_DIR%\%BUILD_TYPE%\forestry_device_tests.exe" (
    cd "%BUILD_DIR%"
    %BUILD_TYPE%\forestry_device_tests.exe
    if errorlevel 1 (
        echo [ERROR] Tests failed
        cd ..
        goto end
    )
    echo [SUCCESS] Tests completed
    cd ..
) else (
    echo [WARNING] Test executable not found. Tests may not be enabled.
)
goto print_summary

:install_main
echo [INFO] Installing project...
cd "%BUILD_DIR%"
cmake --build . --target install --config %BUILD_TYPE%
if errorlevel 1 (
    echo [ERROR] Installation failed
    cd ..
    goto end
)
cd ..
echo [SUCCESS] Project installed to %INSTALL_DIR%
goto print_summary

:print_summary
echo.
echo ============================================
echo          Build Summary
echo ============================================
echo Project: %PROJECT_NAME%
echo Platform: Windows
echo Build Type: %BUILD_TYPE%
echo Tests: %ENABLE_TESTS%
echo Build Directory: %BUILD_DIR%

if exist "%BUILD_DIR%\bin" (
    echo.
    echo Built Executables:
    dir /b "%BUILD_DIR%\bin\*.exe" 2>nul | findstr /r ".*" >nul && (
        for %%f in ("%BUILD_DIR%\bin\*.exe") do echo   %%f
    )
)

if exist "%BUILD_DIR%\%BUILD_TYPE%" (
    dir /b "%BUILD_DIR%\%BUILD_TYPE%\*.exe" 2>nul | findstr /r ".*" >nul && (
        echo.
        echo Built Executables (%BUILD_TYPE%):
        for %%f in ("%BUILD_DIR%\%BUILD_TYPE%\*.exe") do echo   %%f
    )
)

echo ============================================

echo [SUCCESS] Build script completed successfully!
goto end

:end
pause
exit /b 0