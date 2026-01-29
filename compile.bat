@echo off
setlocal

echo ========================================
echo CR29 RDNA4 Kernel Builder
echo ========================================
echo.

:: Setup Visual Studio environment
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvarsall.bat" x64

if %errorlevel% neq 0 (
    echo Failed to setup Visual Studio environment
    exit /b 1
)

:: Create build directory
cd /d "%~dp0"
if not exist build mkdir build
if not exist lib mkdir lib
if not exist include\CL mkdir include\CL

:: Download OpenCL headers if needed
if not exist include\CL\cl.h (
    echo Downloading OpenCL headers...
    curl -sL -o include\CL\cl.h https://raw.githubusercontent.com/KhronosGroup/OpenCL-Headers/main/CL/cl.h
    curl -sL -o include\CL\cl_platform.h https://raw.githubusercontent.com/KhronosGroup/OpenCL-Headers/main/CL/cl_platform.h
    curl -sL -o include\CL\cl_version.h https://raw.githubusercontent.com/KhronosGroup/OpenCL-Headers/main/CL/cl_version.h
    curl -sL -o include\CL\cl_ext.h https://raw.githubusercontent.com/KhronosGroup/OpenCL-Headers/main/CL/cl_ext.h
)

:: Create OpenCL import library from DLL if needed
if not exist lib\OpenCL.lib (
    echo Creating OpenCL import library...

    :: Create a .def file for the DLL exports we need
    echo LIBRARY OpenCL > lib\OpenCL.def
    echo EXPORTS >> lib\OpenCL.def
    echo     clGetPlatformIDs >> lib\OpenCL.def
    echo     clGetPlatformInfo >> lib\OpenCL.def
    echo     clGetDeviceIDs >> lib\OpenCL.def
    echo     clGetDeviceInfo >> lib\OpenCL.def
    echo     clCreateContext >> lib\OpenCL.def
    echo     clCreateCommandQueueWithProperties >> lib\OpenCL.def
    echo     clCreateProgramWithSource >> lib\OpenCL.def
    echo     clBuildProgram >> lib\OpenCL.def
    echo     clGetProgramBuildInfo >> lib\OpenCL.def
    echo     clCreateKernel >> lib\OpenCL.def
    echo     clSetKernelArg >> lib\OpenCL.def
    echo     clCreateBuffer >> lib\OpenCL.def
    echo     clEnqueueNDRangeKernel >> lib\OpenCL.def
    echo     clEnqueueReadBuffer >> lib\OpenCL.def
    echo     clEnqueueFillBuffer >> lib\OpenCL.def
    echo     clReleaseKernel >> lib\OpenCL.def
    echo     clReleaseProgram >> lib\OpenCL.def
    echo     clReleaseMemObject >> lib\OpenCL.def
    echo     clReleaseCommandQueue >> lib\OpenCL.def
    echo     clReleaseContext >> lib\OpenCL.def
    echo     clFinish >> lib\OpenCL.def
    echo     clEnqueueWriteBuffer >> lib\OpenCL.def

    :: Create import library
    lib /def:lib\OpenCL.def /out:lib\OpenCL.lib /machine:x64
)

echo.
echo Compiling cr29_miner.cpp...
echo.

cl /EHsc /O2 /MD /W3 ^
    /I include ^
    /DCL_TARGET_OPENCL_VERSION=200 ^
    src\cr29_miner.cpp ^
    /Fe:build\cr29_miner.exe ^
    /link lib\OpenCL.lib

if %errorlevel% neq 0 (
    echo.
    echo Build FAILED
    exit /b 1
)

:: Copy kernel files
echo.
echo Copying kernel files...
if not exist build\src mkdir build\src
copy src\*.cl build\src\ >nul

echo.
echo ========================================
echo Build SUCCESSFUL
echo Output: build\cr29_miner.exe
echo ========================================

endlocal
