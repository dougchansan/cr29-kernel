@echo off
setlocal

echo ========================================
echo CR29 Debug Build
echo ========================================
echo.

call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvarsall.bat" x64

cd /d "%~dp0"
if not exist build mkdir build

echo Compiling cr29_debug.cpp...

cl /EHsc /O2 /MD /W3 ^
    /I include ^
    /DCL_TARGET_OPENCL_VERSION=200 ^
    src\cr29_debug.cpp ^
    /Fe:build\cr29_debug.exe ^
    /link lib\OpenCL.lib

if %errorlevel% neq 0 (
    echo Build FAILED
    exit /b 1
)

echo.
echo Build SUCCESSFUL: build\cr29_debug.exe
endlocal
