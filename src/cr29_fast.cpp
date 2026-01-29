/**
 * CR29 Fast Miner - Optimized for RDNA 4
 * All buckets processed in parallel
 */

#include <CL/cl.h>
#include <vector>
#include <string>
#include <fstream>
#include <sstream>
#include <iostream>
#include <chrono>
#include <cstring>
#include <algorithm>
#include <unordered_map>
#include <unordered_set>

// CR29 Parameters
constexpr uint32_t EDGEBITS = 29;
constexpr uint64_t NEDGES = 1ULL << EDGEBITS;
constexpr uint32_t NODEBITS = EDGEBITS + 1;
constexpr uint64_t NNODES = 1ULL << NODEBITS;
constexpr uint32_t NODEMASK = NNODES - 1;
constexpr uint32_t EDGEMASK = NEDGES - 1;
constexpr uint32_t PROOFSIZE = 42;

// Bucketing - use more buckets for better parallelism
constexpr uint32_t XBITS = 6;
constexpr uint32_t NUMBUCKETS = 1 << XBITS;
constexpr uint32_t TRIMROUNDS = 176;

// Buffer sizing
constexpr uint32_t MAX_EDGES_PER_BUCKET = (NEDGES / NUMBUCKETS) + 8192;

class FastMiner {
    cl_platform_id platform = nullptr;
    cl_device_id device = nullptr;
    cl_context context = nullptr;
    cl_command_queue queue = nullptr;
    cl_program program = nullptr;

    cl_kernel seedKernel = nullptr;
    cl_kernel countKernel = nullptr;
    cl_kernel trimKernel = nullptr;
    cl_kernel consolidateKernel = nullptr;

    cl_mem edgesA = nullptr;
    cl_mem edgesB = nullptr;
    cl_mem countsA = nullptr;
    cl_mem countsB = nullptr;
    cl_mem degreeCounters = nullptr;
    cl_mem output = nullptr;
    cl_mem outputCount = nullptr;

    static constexpr uint32_t COUNTERS_PER_BUCKET = 65536;  // Per bucket, like simple version
    static constexpr uint32_t COUNTER_SIZE = NUMBUCKETS * COUNTERS_PER_BUCKET;  // Total counters

    bool initialized = false;

public:
    struct SipKeys {
        uint64_t k0, k1, k2, k3;
    };

    bool init(int deviceIndex) {
        cl_int err;

        // Find AMD platform
        cl_uint numPlatforms = 0;
        clGetPlatformIDs(0, nullptr, &numPlatforms);
        std::vector<cl_platform_id> platforms(numPlatforms);
        clGetPlatformIDs(numPlatforms, platforms.data(), nullptr);

        for (auto& plat : platforms) {
            char vendor[256] = {0};
            clGetPlatformInfo(plat, CL_PLATFORM_VENDOR, 256, vendor, nullptr);
            if (strstr(vendor, "AMD") || strstr(vendor, "Advanced Micro")) {
                platform = plat;
                break;
            }
        }

        if (!platform) {
            std::cerr << "AMD platform not found\n";
            return false;
        }

        // Get GPU device
        cl_uint numDevices = 0;
        clGetDeviceIDs(platform, CL_DEVICE_TYPE_GPU, 0, nullptr, &numDevices);
        std::vector<cl_device_id> devices(numDevices);
        clGetDeviceIDs(platform, CL_DEVICE_TYPE_GPU, numDevices, devices.data(), nullptr);

        if (deviceIndex >= (int)numDevices) {
            std::cerr << "Device index out of range\n";
            return false;
        }

        device = devices[deviceIndex];

        char deviceName[256] = {0};
        clGetDeviceInfo(device, CL_DEVICE_NAME, 256, deviceName, nullptr);
        std::cout << "Using: " << deviceName << "\n";

        // Create context and queue
        context = clCreateContext(nullptr, 1, &device, nullptr, nullptr, &err);
        queue = clCreateCommandQueueWithProperties(context, device, nullptr, &err);

        // Load and build kernel
        std::ifstream file("src/cr29_fast.cl");
        if (!file.is_open()) {
            std::cerr << "Failed to open kernel file\n";
            return false;
        }

        std::stringstream ss;
        ss << file.rdbuf();
        std::string source = ss.str();

        const char* src = source.c_str();
        size_t srcLen = source.length();

        program = clCreateProgramWithSource(context, 1, &src, &srcLen, &err);
        err = clBuildProgram(program, 1, &device, "-cl-std=CL2.0 -cl-mad-enable -cl-fast-relaxed-math", nullptr, nullptr);

        if (err != CL_SUCCESS) {
            size_t logSize;
            clGetProgramBuildInfo(program, device, CL_PROGRAM_BUILD_LOG, 0, nullptr, &logSize);
            std::vector<char> log(logSize);
            clGetProgramBuildInfo(program, device, CL_PROGRAM_BUILD_LOG, logSize, log.data(), nullptr);
            std::cerr << "Build failed:\n" << log.data() << "\n";
            return false;
        }

        seedKernel = clCreateKernel(program, "Seed", &err);
        countKernel = clCreateKernel(program, "CountDegrees", &err);
        trimKernel = clCreateKernel(program, "Trim", &err);
        consolidateKernel = clCreateKernel(program, "Consolidate", &err);

        if (!seedKernel || !countKernel || !trimKernel || !consolidateKernel) {
            std::cerr << "Failed to create kernels\n";
            return false;
        }

        // Allocate buffers
        size_t edgeBufferSize = (size_t)NUMBUCKETS * MAX_EDGES_PER_BUCKET * sizeof(uint64_t);
        size_t countBufferSize = NUMBUCKETS * sizeof(uint32_t);
        size_t outputBufferSize = 10 * 1024 * 1024 * sizeof(uint64_t);

        std::cout << "Edge buffers: " << (edgeBufferSize / 1024 / 1024) << " MB each\n";

        edgesA = clCreateBuffer(context, CL_MEM_READ_WRITE, edgeBufferSize, nullptr, &err);
        edgesB = clCreateBuffer(context, CL_MEM_READ_WRITE, edgeBufferSize, nullptr, &err);
        countsA = clCreateBuffer(context, CL_MEM_READ_WRITE, countBufferSize, nullptr, &err);
        countsB = clCreateBuffer(context, CL_MEM_READ_WRITE, countBufferSize, nullptr, &err);
        degreeCounters = clCreateBuffer(context, CL_MEM_READ_WRITE, COUNTER_SIZE * sizeof(uint32_t), nullptr, &err);
        output = clCreateBuffer(context, CL_MEM_READ_WRITE, outputBufferSize, nullptr, &err);
        outputCount = clCreateBuffer(context, CL_MEM_READ_WRITE, sizeof(uint32_t), nullptr, &err);

        std::cout << "Counter buffer: " << (COUNTER_SIZE * sizeof(uint32_t) / 1024 / 1024) << " MB\n";

        if (err != CL_SUCCESS) {
            std::cerr << "Failed to allocate buffers\n";
            return false;
        }

        initialized = true;
        std::cout << "Initialization complete\n\n";
        return true;
    }

    uint32_t mine(const SipKeys& keys) {
        if (!initialized) return 0;

        auto startTime = std::chrono::high_resolution_clock::now();

        cl_int err;
        cl_ulong4 sipkeys = {{keys.k0, keys.k1, keys.k2, keys.k3}};
        uint32_t edgeMask = EDGEMASK;
        uint32_t nodeMask = NODEMASK;
        uint32_t xbits = XBITS;
        uint32_t maxPerBucket = MAX_EDGES_PER_BUCKET;

        // Zero counts before seeding
        std::vector<uint32_t> zeroCounts(NUMBUCKETS, 0);
        clEnqueueWriteBuffer(queue, countsA, CL_TRUE, 0, NUMBUCKETS * sizeof(uint32_t), zeroCounts.data(), 0, nullptr, nullptr);

        // Seed - generate edges
        clSetKernelArg(seedKernel, 0, sizeof(cl_mem), &edgesA);
        clSetKernelArg(seedKernel, 1, sizeof(cl_mem), &countsA);
        clSetKernelArg(seedKernel, 2, sizeof(cl_ulong4), &sipkeys);
        clSetKernelArg(seedKernel, 3, sizeof(uint32_t), &edgeMask);
        clSetKernelArg(seedKernel, 4, sizeof(uint32_t), &nodeMask);
        clSetKernelArg(seedKernel, 5, sizeof(uint32_t), &xbits);
        clSetKernelArg(seedKernel, 6, sizeof(uint32_t), &maxPerBucket);

        // Launch many workgroups for edge generation
        size_t seedGlobal = 256 * 1024;  // Many threads for edge gen
        size_t seedLocal = 256;

        err = clEnqueueNDRangeKernel(queue, seedKernel, 1, nullptr, &seedGlobal, &seedLocal, 0, nullptr, nullptr);
        clFinish(queue);

        // Check seed counts
        std::vector<uint32_t> seedCounts(NUMBUCKETS);
        clEnqueueReadBuffer(queue, countsA, CL_TRUE, 0, NUMBUCKETS * sizeof(uint32_t), seedCounts.data(), 0, nullptr, nullptr);
        uint64_t totalSeeded = 0;
        for (auto c : seedCounts) totalSeeded += c;

        auto seedTime = std::chrono::high_resolution_clock::now();
        auto seedDuration = std::chrono::duration_cast<std::chrono::milliseconds>(seedTime - startTime);
        std::cout << "Seed: " << seedDuration.count() << "ms (" << totalSeeded << " edges)\n";

        // Trimming rounds
        cl_mem* srcEdges = &edgesA;
        cl_mem* dstEdges = &edgesB;
        cl_mem* srcCounts = &countsA;
        cl_mem* dstCounts = &countsB;

        uint32_t numBuckets = NUMBUCKETS;
        uint32_t counterSize = COUNTER_SIZE;

        std::cout << "Trimming " << TRIMROUNDS << " rounds...\n";

        std::vector<uint32_t> zeroCounts(NUMBUCKETS, 0);
        uint32_t countersPerBucket = COUNTERS_PER_BUCKET;

        for (uint32_t round = 0; round < TRIMROUNDS; round++) {
            // Zero destination counts
            clEnqueueWriteBuffer(queue, *dstCounts, CL_FALSE, 0, NUMBUCKETS * sizeof(uint32_t), zeroCounts.data(), 0, nullptr, nullptr);

            // Count degrees for all buckets in parallel
            clSetKernelArg(countKernel, 0, sizeof(cl_mem), srcEdges);
            clSetKernelArg(countKernel, 1, sizeof(cl_mem), srcCounts);
            clSetKernelArg(countKernel, 2, sizeof(cl_mem), &degreeCounters);
            clSetKernelArg(countKernel, 3, sizeof(uint32_t), &numBuckets);
            clSetKernelArg(countKernel, 4, sizeof(uint32_t), &maxPerBucket);
            clSetKernelArg(countKernel, 5, sizeof(uint32_t), &countersPerBucket);
            clSetKernelArg(countKernel, 6, sizeof(uint32_t), &nodeMask);
            clSetKernelArg(countKernel, 7, sizeof(uint32_t), &round);

            // One workgroup per bucket for counting
            size_t countGlobal = NUMBUCKETS * 256;
            size_t countLocal = 256;
            clEnqueueNDRangeKernel(queue, countKernel, 1, nullptr, &countGlobal, &countLocal, 0, nullptr, nullptr);

            // Trim each bucket (also in parallel)
            clSetKernelArg(trimKernel, 0, sizeof(cl_mem), srcEdges);
            clSetKernelArg(trimKernel, 1, sizeof(cl_mem), dstEdges);
            clSetKernelArg(trimKernel, 2, sizeof(cl_mem), srcCounts);
            clSetKernelArg(trimKernel, 3, sizeof(cl_mem), dstCounts);
            clSetKernelArg(trimKernel, 4, sizeof(cl_mem), &degreeCounters);
            clSetKernelArg(trimKernel, 6, sizeof(uint32_t), &maxPerBucket);
            clSetKernelArg(trimKernel, 7, sizeof(uint32_t), &countersPerBucket);
            clSetKernelArg(trimKernel, 8, sizeof(uint32_t), &nodeMask);
            clSetKernelArg(trimKernel, 9, sizeof(uint32_t), &round);

            for (uint32_t bucket = 0; bucket < NUMBUCKETS; bucket++) {
                clSetKernelArg(trimKernel, 5, sizeof(uint32_t), &bucket);
                size_t trimGlobal = 256;
                size_t trimLocal = 256;
                clEnqueueNDRangeKernel(queue, trimKernel, 1, nullptr, &trimGlobal, &trimLocal, 0, nullptr, nullptr);
            }

            clFinish(queue);

            // Swap buffers
            std::swap(srcEdges, dstEdges);
            std::swap(srcCounts, dstCounts);

            // Progress every 44 rounds
            if ((round + 1) % 44 == 0) {
                std::vector<uint32_t> counts(NUMBUCKETS);
                clEnqueueReadBuffer(queue, *srcCounts, CL_TRUE, 0, NUMBUCKETS * sizeof(uint32_t), counts.data(), 0, nullptr, nullptr);
                uint64_t total = 0;
                for (auto c : counts) total += c;
                std::cout << "  Round " << (round + 1) << ": " << total << " edges\n";
            }
        }

        auto trimTime = std::chrono::high_resolution_clock::now();
        auto trimDuration = std::chrono::duration_cast<std::chrono::milliseconds>(trimTime - seedTime);
        std::cout << "Trim: " << trimDuration.count() << "ms\n";

        // Consolidate
        uint32_t zero = 0;
        clEnqueueWriteBuffer(queue, outputCount, CL_TRUE, 0, sizeof(uint32_t), &zero, 0, nullptr, nullptr);

        clSetKernelArg(consolidateKernel, 0, sizeof(cl_mem), srcEdges);
        clSetKernelArg(consolidateKernel, 1, sizeof(cl_mem), srcCounts);
        clSetKernelArg(consolidateKernel, 2, sizeof(cl_mem), &output);
        clSetKernelArg(consolidateKernel, 3, sizeof(cl_mem), &outputCount);
        clSetKernelArg(consolidateKernel, 4, sizeof(uint32_t), &maxPerBucket);

        size_t consGlobal = NUMBUCKETS * 64;
        size_t consLocal = 64;

        clEnqueueNDRangeKernel(queue, consolidateKernel, 1, nullptr, &consGlobal, &consLocal, 0, nullptr, nullptr);
        clFinish(queue);

        uint32_t finalCount;
        clEnqueueReadBuffer(queue, outputCount, CL_TRUE, 0, sizeof(uint32_t), &finalCount, 0, nullptr, nullptr);

        auto endTime = std::chrono::high_resolution_clock::now();
        auto totalDuration = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime);

        std::cout << "\nTotal: " << totalDuration.count() << "ms ("
                  << (1000.0 / totalDuration.count()) << " g/s)\n";
        std::cout << "Final edges: " << finalCount << "\n";

        return finalCount;
    }

    std::vector<uint64_t> readEdges(uint32_t count) {
        std::vector<uint64_t> edges(count);
        clEnqueueReadBuffer(queue, output, CL_TRUE, 0, count * sizeof(uint64_t), edges.data(), 0, nullptr, nullptr);
        return edges;
    }

    void cleanup() {
        if (seedKernel) clReleaseKernel(seedKernel);
        if (countKernel) clReleaseKernel(countKernel);
        if (trimKernel) clReleaseKernel(trimKernel);
        if (consolidateKernel) clReleaseKernel(consolidateKernel);
        if (program) clReleaseProgram(program);
        if (edgesA) clReleaseMemObject(edgesA);
        if (edgesB) clReleaseMemObject(edgesB);
        if (countsA) clReleaseMemObject(countsA);
        if (countsB) clReleaseMemObject(countsB);
        if (degreeCounters) clReleaseMemObject(degreeCounters);
        if (output) clReleaseMemObject(output);
        if (outputCount) clReleaseMemObject(outputCount);
        if (queue) clReleaseCommandQueue(queue);
        if (context) clReleaseContext(context);
    }

    ~FastMiner() {
        cleanup();
    }
};

int main() {
    std::cout << "CR29 Fast Miner for RDNA 4\n";
    std::cout << "==========================\n\n";

    FastMiner miner;

    if (!miner.init(1)) {
        return 1;
    }

    // Test keys
    FastMiner::SipKeys keys = {
        0x0706050403020100ULL,
        0x0f0e0d0c0b0a0908ULL,
        0x0706050403020100ULL ^ 0x736f6d6570736575ULL,
        0x0f0e0d0c0b0a0908ULL ^ 0x646f72616e646f6dULL
    };

    // Run multiple iterations to measure performance
    std::cout << "\n=== Performance Test ===\n";
    for (int i = 0; i < 3; i++) {
        std::cout << "\nIteration " << (i + 1) << ":\n";
        miner.mine(keys);
    }

    return 0;
}
