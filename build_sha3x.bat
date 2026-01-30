@echo off
echo Building SHA3X Miner for XTM...
echo.

:: Check for Visual Studio
where cl >nul 2>&1
if %errorlevel% neq 0 (
    echo Visual Studio compiler not found in PATH
    echo Please run from Developer Command Prompt or install Visual Studio Build Tools
    pause
    exit /b 1
)

:: Create build directory
if not exist build mkdir build
cd build

:: Find OpenCL
set OPENCL_ROOT=C:\Windows\System32
if exist "%AMDAPPSDKROOT%" (
    set OPENCL_INCLUDE=%AMDAPPSDKROOT%\include
    set OPENCL_LIB=%AMDAPPSDKROOT%\lib\x86_64
) else (
    :: Use system OpenCL
    set OPENCL_INCLUDE=
    set OPENCL_LIB=
)

echo Compiling SHA3X test program...
cl /EHsc /O2 /MD ^
    /I "..\src" ^
    /I "%OPENCL_INCLUDE%" ^
    ..\src\test_sha3x.cpp ^
    /out:test_sha3x.exe

if %errorlevel% neq 0 (
    echo Test build failed!
) else (
    echo Test build successful!
)

echo.
echo Compiling XTM integration test...
cl /EHsc /O2 /MD ^
    /I "..\src" ^
    /I "%OPENCL_INCLUDE%" ^
    ..\src\xtm_integration_main.cpp ^
    /out:xtm_integration_test.exe

if %errorlevel% neq 0 (
    echo Integration test build failed!
) else (
    echo Integration test build successful!
)

if %errorlevel% neq 0 (
    echo Test build failed!
) else (
    echo Test build successful!
)

echo.
echo Compiling SHA3X pool miner...

cl /EHsc /O2 /MD ^
    /I "..\src" ^
    /I "%OPENCL_INCLUDE%" ^
    ..\src\sha3x_pool_miner.cpp ^
    /link OpenCL.lib ws2_32.lib ^
    /LIBPATH:"%OPENCL_LIB%" ^
    /out:sha3x_miner.exe

if %errorlevel% neq 0 (
    echo SHA3X miner build failed!
    cd ..
    pause
    exit /b 1
)

:: Copy kernel files
echo Copying kernel files...
if not exist src mkdir src
copy ..\src\sha3x_kernel.cl src\ >nul
copy ..\src\siphash.cl src\ >nul
copy ..\src\trimmer.cl src\ >nul

echo.
echo Build successful! Outputs:
echo   - build\sha3x_miner.exe (main miner)
echo   - build\test_sha3x.exe (test program)
echo   - build\xtm_integration_test.exe (integration test)
echo.
echo To run tests: build\test_sha3x.exe
echo To mine: build\sha3x_miner.exe -o pool:port -u wallet.worker
echo To test integration: build\xtm_integration_test.exe --duration 15

cd ..
pause