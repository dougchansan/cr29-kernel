@echo off
echo Building CR29 RDNA 4 Miner...
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

echo Compiling cr29_miner.cpp...

cl /EHsc /O2 /MD ^
    /I "%OPENCL_INCLUDE%" ^
    ..\src\cr29_miner.cpp ^
    /link OpenCL.lib ^
    /LIBPATH:"%OPENCL_LIB%" ^
    /out:cr29_miner.exe

if %errorlevel% neq 0 (
    echo Build failed!
    cd ..
    pause
    exit /b 1
)

:: Copy kernel files
echo Copying kernel files...
if not exist src mkdir src
copy ..\src\siphash.cl src\ >nul
copy ..\src\trimmer.cl src\ >nul

echo.
echo Build successful! Output: build\cr29_miner.exe
cd ..
pause
