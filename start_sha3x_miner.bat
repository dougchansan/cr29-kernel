@echo off
:: SHA3X Miner Launcher - Real Mining Implementation
:: Builds and runs the actual SHA3X miner for XTM
:: Configuration: Your Kryptex pool setup

title SHA3X Miner for XTM - Production Launcher

:: Set encoding for proper Unicode display
chcp 65001 >nul

:: Configuration - Your Kryptex XTM-SHA3X setup
set POOL_URL=xtm-sha3x.kryptex.network:7039
set WALLET_ADDRESS=12LfqTi7aQKz9cpxU1AsRW7zNCRkKYdwsxVB1Qx47q3ZGS2DQUpMHDKoAdi2apbaFDdHzrjnDbe4jK1B4DbYo4titQH
set WORKER_NAME=9070xt
set USE_TLS=false
set API_PORT=8080

echo.
echo ========================================
echo 🚀 SHA3X Miner for XTM - PRODUCTION READY 🚀
echo ========================================
echo 📍 Pool: %POOL_URL%
echo 💰 Wallet: %WALLET_ADDRESS:~0,20%... 
echo 🖥️  Worker: %WORKER_NAME%
echo 🔒 TLS: %USE_TLS%
echo ========================================
echo.

:: Check if we're in the right directory
if not exist "src\sha3x_pool_miner.cpp" (
    echo ❌ ERROR: Not in cr29-kernel directory!
    echo Please run this batch file from: C:\Users\douglaswhittingham\AppData\Roaming\npm\cr29-kernel\
    pause
    exit /b 1
)

:: Check for Visual Studio compiler
where cl >nul 2>&1
if %errorlevel% neq 0 (
    echo ❌ Visual Studio compiler (cl.exe) not found in PATH
    echo.
    echo 🔧 To build the real miner, you need:
    echo 1. Visual Studio 2022 or Build Tools
    echo 2. AMD GPU drivers with OpenCL support
    echo 3. Run this from Developer Command Prompt
    echo.
    echo 📝 For now, I'll show you the build commands and configuration
    pause
    goto :show_build_instructions
)

:: Check for AMD GPU
reg query "HKLM\SYSTEM\CurrentControlSet\Control\Class\{4d36e968-e325-11ce-bfc1-08002be10318}" /s | findstr /i "amd" >nul 2>&1
if %errorlevel% neq 0 (
    echo ⚠️  WARNING: No AMD GPU detected
    echo This miner is optimized for AMD RDNA 2/3/4 GPUs
    echo You can still build and test, but mining won't work
    echo.
    pause
)

:: Main menu
echo 🔧 What would you like to do?
echo.
echo [1] Build and run the miner (requires Visual Studio)
echo [2] Show build instructions only
echo [3] Run performance validation
echo [4] Run stress test
echo [5] Check system requirements
echo [6] Exit
echo.

set /p choice="Enter your choice (1-6): "

if "%choice%"=="1" goto :build_and_run
if "%choice%"=="2" goto :show_build_instructions
if "%choice%"=="3" goto :run_validation
if "%choice%"=="4" goto :run_stress_test
if "%choice%"=="5" goto :check_requirements
if "%choice%"=="6" goto :exit

echo ❌ Invalid choice!
goto :main_menu

:build_and_run
echo.
echo 🔨 Building SHA3X miner...
echo.

:: Create build directory
if not exist "build_real" mkdir build_real
cd build_real

:: Build the miner
echo Building main miner executable...
cl /EHsc /O2 /MD /nologo ^
    /I "..\src" ^
    /I "..\include" ^
    ..\src\sha3x_pool_miner.cpp ^
    /link ws2_32.lib OpenCL.lib ^
    /out:sha3x_miner.exe

if %errorlevel% neq 0 (
    echo ❌ Build failed!
    echo.
    echo 🔧 Common issues:
    echo - Missing OpenCL headers: Install AMD GPU drivers
    echo - Missing libraries: Check AMD APP SDK or ROCm installation
    echo - Compiler errors: Update Visual Studio
    pause
    goto :show_build_instructions
)

echo ✅ Build successful!
echo.
echo 🚀 Starting real SHA3X miner...
echo.

:: Copy kernel files
if not exist "src" mkdir src
copy "..\src\*.cl" "src\" >nul

echo 🎯 Configuration:
echo   Pool: %POOL_URL%
echo   Wallet: %WALLET_ADDRESS:~0,20%... 
echo   Worker: %WORKER_NAME%
echo   TLS: %USE_TLS%
echo   API Port: %API_PORT%
echo.
echo ⏱️  Starting mining... Press Ctrl+C to stop
echo.
echo ========================================

:: Run the real miner
sha3x_miner.exe ^
    -o %POOL_URL% ^
    -u %WALLET_ADDRESS%.%WORKER_NAME% ^
    -p x ^
    --tls %USE_TLS% ^
    --api-port %API_PORT% ^
    --verbose

cd ..
goto :exit

:show_build_instructions
echo.
echo 📋 MANUAL BUILD INSTRUCTIONS
echo ============================
echo.
echo 🔧 Prerequisites:
echo 1. Visual Studio 2022 or Build Tools for Visual Studio
echo 2. AMD GPU drivers (Adrenalin 23.40+ or ROCm 6.0+)
echo 3. Run from Developer Command Prompt
echo.
echo 📝 Build Commands:
echo.
echo # Create build directory
echo mkdir build_real
echo cd build_real
echo.
echo # Build the miner
echo cl /EHsc /O2 /MD /nologo ^
echo     /I "..\src" ^
echo     /I "..\include" ^
echo     ..\src\sha3x_pool_miner.cpp ^
echo     /link ws2_32.lib OpenCL.lib ^
echo     /out:sha3x_miner.exe
echo.
echo # Copy kernel files
echo mkdir src
echo copy "..\src\*.cl" "src\" 
echo.
echo # Run the miner
echo sha3x_miner.exe -o %POOL_URL% -u %WALLET_ADDRESS%.%WORKER_NAME% -p x --tls %USE_TLS% --api-port %API_PORT%
echo.
echo 🔍 Troubleshooting:
echo - If OpenCL.lib not found: Install AMD GPU drivers
echo - If cl.exe not found: Run from Developer Command Prompt
echo - If build fails: Check Visual Studio installation
echo.
echo 📚 Documentation: See COMPLETE_DOCUMENTATION.md
echo.
pause
goto :exit

:run_validation
echo.
echo 🔍 Running Performance Validation...
echo.
if exist "sha3x_test_suite.exe" (
    sha3x_test_suite.exe --validate-perf --duration 5 --verbose
) else (
    echo ⚠️  Test suite not built. Building now...
    cd build_real 2>nul || mkdir build_real && cd build_real
    cl /EHsc /O2 /MD /nologo ^
        /I "..\src" ^
        ..\src\sha3x_test_suite.cpp ^
        /out:sha3x_test_suite.exe
    if %errorlevel% equ 0 (
        sha3x_test_suite.exe --validate-perf --duration 5 --verbose
    ) else (
        echo ❌ Could not build test suite
    )
    cd ..
)
pause
goto :exit

:run_stress_test
echo.
echo 🔥 Running Stress Test...
echo.
if exist "sha3x_test_suite.exe" (
    sha3x_test_suite.exe --stress-test --duration 10 --thermal-stress --verbose
) else (
    echo ⚠️  Test suite not built. Run option 3 first to build it.
)
pause
goto :exit

:check_requirements
echo.
echo 🔍 System Requirements Check
echo =============================
echo.
echo ✅ Checking for AMD GPU...
reg query "HKLM\SYSTEM\CurrentControlSet\Control\Class\{4d36e968-e325-11ce-bfc1-08002be10318}" /s 2>nul | findstr /i "amd" >nul
if %errorlevel% equ 0 (
    echo ✅ AMD GPU detected
) else (
    echo ❌ No AMD GPU detected
)

echo.
echo ✅ Checking for Visual Studio...
where cl >nul 2>&1
if %errorlevel% equ 0 (
    echo ✅ Visual Studio compiler found
) else (
    echo ❌ Visual Studio compiler not found
)

echo.
echo ✅ Checking for Python (for demos)...
python --version >nul 2>&1
if %errorlevel% equ 0 (
    echo ✅ Python found
    python --version
) else (
    echo ❌ Python not found
)

echo.
echo 📋 System Status:
echo - Windows Version: %OS% %PROCESSOR_ARCHITECTURE%
echo - Current Directory: %CD%
echo - GPU Drivers: Check Device Manager
echo - OpenCL Support: Check AMD drivers
echo.
pause
goto :exit

:exit
echo.
echo 👋 Thank you for using SHA3X Miner!
echo 📚 For complete documentation, see: COMPLETE_DOCUMENTATION.md
echo 🌐 GitHub: https://github.com/dougchansan/cr29-kernel
echo.
echo 🎯 Next Steps:
echo 1. Install Visual Studio 2022 and AMD GPU drivers
echo 2. Build the miner using the commands above
echo 3. Run performance validation to ensure optimal settings
echo 4. Start mining with your configuration
echo.
if "%choice%"=="1" (
    echo 💡 Build completed! Check build_real\sha3x_miner.exe
) else (
    echo 💡 Follow the build instructions to create the real miner
)
pause