@echo off
setlocal

echo CR29 Pool Miner Build
echo =====================

call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvarsall.bat" x64

cd /d "%~dp0"
if not exist build mkdir build

cl /EHsc /O2 /MD /W3 /std:c++17 ^
    /I include ^
    /DCL_TARGET_OPENCL_VERSION=200 ^
    src\cr29_pool_miner.cpp ^
    /Fe:build\cr29_pool_miner.exe ^
    /link lib\OpenCL.lib ws2_32.lib

if %errorlevel% neq 0 (
    echo Build FAILED
    exit /b 1
)

echo Build SUCCESSFUL
echo.
echo Usage: build\cr29_pool_miner.exe -o pool:port -u wallet -p password
echo        build\cr29_pool_miner.exe --benchmark --verbose
endlocal
