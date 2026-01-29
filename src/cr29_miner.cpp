/**
 * CR29 OpenCL Miner for RDNA 4
 * Host code for managing GPU kernel execution
 */

#include <CL/cl.h>
#include <vector>
#include <string>
#include <fstream>
#include <sstream>
#include <iostream>
#include <chrono>
#include <cstring>

// Graph parameters
constexpr uint32_t EDGEBITS = 29;
constexpr uint64_t NEDGES = 1ULL << EDGEBITS;
constexpr uint32_t NODEBITS = EDGEBITS + 1;
constexpr uint64_t NNODES = 1ULL << NODEBITS;
constexpr uint32_t PROOFSIZE = 42;

// Trimming parameters
constexpr uint32_t XBITS = 6;
constexpr uint32_t NX = 1 << XBITS;
constexpr uint32_t TRIMROUNDS = 176;

// RDNA 4 optimal workgroup size
constexpr size_t WORKGROUP_SIZE = 256;

struct SipKeys {
    uint64_t k0, k1, k2, k3;
};

class CR29Miner {
private:
    cl_platform_id platform;
    cl_device_id device;
    cl_context context;
    cl_command_queue queue;
    cl_program program;

    // Kernels
    cl_kernel seedA_kernel;
    cl_kernel round_kernel;
    cl_kernel tail_kernel;

    // Buffers
    cl_mem bufferA;
    cl_mem bufferB;
    cl_mem indexesA;
    cl_mem indexesB;
    cl_mem output;
    cl_mem outputCount;

    size_t bufferSize;
    size_t indexSize;

    bool initialized = false;

public:
    CR29Miner() = default;

    ~CR29Miner() {
        cleanup();
    }

    bool init(int deviceIndex = 0) {
        cl_int err;

        // Get platform
        cl_uint numPlatforms;
        clGetPlatformIDs(0, nullptr, &numPlatforms);
        std::vector<cl_platform_id> platforms(numPlatforms);
        clGetPlatformIDs(numPlatforms, platforms.data(), nullptr);

        // Find AMD platform
        for (auto& plat : platforms) {
            char vendor[256];
            clGetPlatformInfo(plat, CL_PLATFORM_VENDOR, 256, vendor, nullptr);
            if (strstr(vendor, "AMD") || strstr(vendor, "Advanced Micro Devices")) {
                platform = plat;
                break;
            }
        }

        if (!platform) {
            std::cerr << "AMD OpenCL platform not found\n";
            return false;
        }

        // Get GPU devices
        cl_uint numDevices;
        clGetDeviceIDs(platform, CL_DEVICE_TYPE_GPU, 0, nullptr, &numDevices);

        if (deviceIndex >= numDevices) {
            std::cerr << "Device index " << deviceIndex << " out of range\n";
            return false;
        }

        std::vector<cl_device_id> devices(numDevices);
        clGetDeviceIDs(platform, CL_DEVICE_TYPE_GPU, numDevices, devices.data(), nullptr);
        device = devices[deviceIndex];

        // Print device info
        char deviceName[256];
        clGetDeviceInfo(device, CL_DEVICE_NAME, 256, deviceName, nullptr);
        std::cout << "Using device: " << deviceName << std::endl;

        // Check for gfx1201 (RDNA 4)
        char version[256];
        clGetDeviceInfo(device, CL_DEVICE_VERSION, 256, version, nullptr);
        std::cout << "OpenCL version: " << version << std::endl;

        // Create context and queue
        context = clCreateContext(nullptr, 1, &device, nullptr, nullptr, &err);
        if (err != CL_SUCCESS) {
            std::cerr << "Failed to create context: " << err << std::endl;
            return false;
        }

        // Use out-of-order queue for better RDNA 4 utilization
        cl_queue_properties props[] = {
            CL_QUEUE_PROPERTIES, CL_QUEUE_OUT_OF_ORDER_EXEC_MODE_ENABLE,
            0
        };
        queue = clCreateCommandQueueWithProperties(context, device, props, &err);
        if (err != CL_SUCCESS) {
            // Fallback to in-order queue
            queue = clCreateCommandQueueWithProperties(context, device, nullptr, &err);
        }

        // Load and build kernels
        if (!buildKernels()) {
            return false;
        }

        // Allocate buffers
        if (!allocateBuffers()) {
            return false;
        }

        initialized = true;
        return true;
    }

    bool buildKernels() {
        cl_int err;

        // Load kernel sources
        std::string siphashSrc = loadFile("src/siphash.cl");
        std::string trimmerSrc = loadFile("src/trimmer.cl");

        if (siphashSrc.empty() || trimmerSrc.empty()) {
            std::cerr << "Failed to load kernel sources\n";
            return false;
        }

        std::string fullSource = siphashSrc + "\n" + trimmerSrc;
        const char* src = fullSource.c_str();
        size_t srcLen = fullSource.length();

        program = clCreateProgramWithSource(context, 1, &src, &srcLen, &err);
        if (err != CL_SUCCESS) {
            std::cerr << "Failed to create program: " << err << std::endl;
            return false;
        }

        // RDNA 4 specific compiler options
        const char* options =
            "-cl-std=CL2.0 "
            "-cl-mad-enable "
            "-cl-fast-relaxed-math "
            "-cl-no-signed-zeros ";

        err = clBuildProgram(program, 1, &device, options, nullptr, nullptr);
        if (err != CL_SUCCESS) {
            size_t logSize;
            clGetProgramBuildInfo(program, device, CL_PROGRAM_BUILD_LOG, 0, nullptr, &logSize);
            std::vector<char> log(logSize);
            clGetProgramBuildInfo(program, device, CL_PROGRAM_BUILD_LOG, logSize, log.data(), nullptr);
            std::cerr << "Build failed:\n" << log.data() << std::endl;
            return false;
        }

        // Create kernels
        seedA_kernel = clCreateKernel(program, "SeedA", &err);
        round_kernel = clCreateKernel(program, "Round", &err);
        tail_kernel = clCreateKernel(program, "Tail", &err);

        if (!seedA_kernel || !round_kernel || !tail_kernel) {
            std::cerr << "Failed to create kernels\n";
            return false;
        }

        return true;
    }

    bool allocateBuffers() {
        cl_int err;

        // Calculate buffer sizes
        // Each bucket can hold up to NEDGES/NX edges
        bufferSize = NEDGES * sizeof(uint64_t) / 2; // Start with half, will trim
        indexSize = NX * sizeof(uint32_t);

        std::cout << "Allocating " << (bufferSize / 1024 / 1024) << " MB per buffer\n";

        bufferA = clCreateBuffer(context, CL_MEM_READ_WRITE, bufferSize, nullptr, &err);
        bufferB = clCreateBuffer(context, CL_MEM_READ_WRITE, bufferSize, nullptr, &err);
        indexesA = clCreateBuffer(context, CL_MEM_READ_WRITE, indexSize, nullptr, &err);
        indexesB = clCreateBuffer(context, CL_MEM_READ_WRITE, indexSize, nullptr, &err);
        output = clCreateBuffer(context, CL_MEM_READ_WRITE, 1024 * 1024 * sizeof(uint64_t), nullptr, &err);
        outputCount = clCreateBuffer(context, CL_MEM_READ_WRITE, sizeof(uint32_t), nullptr, &err);

        if (err != CL_SUCCESS) {
            std::cerr << "Failed to allocate buffers: " << err << std::endl;
            return false;
        }

        return true;
    }

    /**
     * Run the full trimming pipeline
     * Returns the number of remaining edges after trimming
     */
    uint32_t trim(const SipKeys& keys) {
        if (!initialized) {
            std::cerr << "Miner not initialized\n";
            return 0;
        }

        cl_int err;
        auto start = std::chrono::high_resolution_clock::now();

        // Pack SipHash keys
        cl_ulong4 sipkeys = {{keys.k0, keys.k1, keys.k2, keys.k3}};

        // Zero indexes
        uint32_t zero = 0;
        clEnqueueFillBuffer(queue, indexesA, &zero, sizeof(zero), 0, indexSize, 0, nullptr, nullptr);
        clEnqueueFillBuffer(queue, indexesB, &zero, sizeof(zero), 0, indexSize, 0, nullptr, nullptr);

        // Seed phase - generate initial edges
        uint32_t noncesPerGroup = NEDGES / NX;
        clSetKernelArg(seedA_kernel, 0, sizeof(cl_mem), &bufferA);
        clSetKernelArg(seedA_kernel, 1, sizeof(cl_mem), &indexesA);
        clSetKernelArg(seedA_kernel, 2, sizeof(cl_ulong4), &sipkeys);
        clSetKernelArg(seedA_kernel, 3, sizeof(uint32_t), &zero);
        clSetKernelArg(seedA_kernel, 4, sizeof(uint32_t), &noncesPerGroup);

        size_t globalSize = NX * WORKGROUP_SIZE;
        size_t localSize = WORKGROUP_SIZE;

        err = clEnqueueNDRangeKernel(queue, seedA_kernel, 1, nullptr,
                                      &globalSize, &localSize, 0, nullptr, nullptr);
        if (err != CL_SUCCESS) {
            std::cerr << "SeedA kernel failed: " << err << std::endl;
            return 0;
        }

        // Trimming rounds
        cl_mem* src = &bufferA;
        cl_mem* dst = &bufferB;
        cl_mem* srcIdx = &indexesA;
        cl_mem* dstIdx = &indexesB;

        for (uint32_t round = 0; round < TRIMROUNDS; round++) {
            clSetKernelArg(round_kernel, 0, sizeof(cl_mem), src);
            clSetKernelArg(round_kernel, 1, sizeof(cl_mem), dst);
            clSetKernelArg(round_kernel, 2, sizeof(cl_mem), srcIdx);
            clSetKernelArg(round_kernel, 3, sizeof(cl_mem), dstIdx);
            clSetKernelArg(round_kernel, 4, sizeof(uint32_t), &round);

            err = clEnqueueNDRangeKernel(queue, round_kernel, 1, nullptr,
                                          &globalSize, &localSize, 0, nullptr, nullptr);

            // Swap buffers
            std::swap(src, dst);
            std::swap(srcIdx, dstIdx);

            // Reset destination indexes
            clEnqueueFillBuffer(queue, *dstIdx, &zero, sizeof(zero), 0, indexSize, 0, nullptr, nullptr);
        }

        // Tail - consolidate results
        clEnqueueFillBuffer(queue, outputCount, &zero, sizeof(zero), 0, sizeof(uint32_t), 0, nullptr, nullptr);

        clSetKernelArg(tail_kernel, 0, sizeof(cl_mem), src);
        clSetKernelArg(tail_kernel, 1, sizeof(cl_mem), srcIdx);
        clSetKernelArg(tail_kernel, 2, sizeof(cl_mem), &output);
        clSetKernelArg(tail_kernel, 3, sizeof(cl_mem), &outputCount);

        err = clEnqueueNDRangeKernel(queue, tail_kernel, 1, nullptr,
                                      &globalSize, &localSize, 0, nullptr, nullptr);

        // Read result count
        uint32_t count;
        clEnqueueReadBuffer(queue, outputCount, CL_TRUE, 0, sizeof(uint32_t), &count, 0, nullptr, nullptr);

        auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

        std::cout << "Trimming complete: " << count << " edges in " << duration.count() << "ms\n";

        return count;
    }

    void cleanup() {
        if (seedA_kernel) clReleaseKernel(seedA_kernel);
        if (round_kernel) clReleaseKernel(round_kernel);
        if (tail_kernel) clReleaseKernel(tail_kernel);
        if (program) clReleaseProgram(program);
        if (bufferA) clReleaseMemObject(bufferA);
        if (bufferB) clReleaseMemObject(bufferB);
        if (indexesA) clReleaseMemObject(indexesA);
        if (indexesB) clReleaseMemObject(indexesB);
        if (output) clReleaseMemObject(output);
        if (outputCount) clReleaseMemObject(outputCount);
        if (queue) clReleaseCommandQueue(queue);
        if (context) clReleaseContext(context);
    }

private:
    std::string loadFile(const std::string& path) {
        std::ifstream file(path);
        if (!file.is_open()) {
            return "";
        }
        std::stringstream ss;
        ss << file.rdbuf();
        return ss.str();
    }
};

// Test main
int main(int argc, char* argv[]) {
    std::cout << "CR29 OpenCL Miner for RDNA 4\n";
    std::cout << "============================\n\n";

    CR29Miner miner;

    // Device 1 = RX 9070 XT (Device 0 is integrated GPU)
    if (!miner.init(1)) {
        std::cerr << "Failed to initialize miner\n";
        return 1;
    }

    // Test with dummy keys
    SipKeys keys = {
        0x0706050403020100ULL,
        0x0f0e0d0c0b0a0908ULL,
        0x0706050403020100ULL ^ 0x736f6d6570736575ULL,
        0x0f0e0d0c0b0a0908ULL ^ 0x646f72616e646f6dULL
    };

    // Run trimming
    uint32_t remaining = miner.trim(keys);

    std::cout << "\nRemaining edges after trimming: " << remaining << std::endl;

    return 0;
}
