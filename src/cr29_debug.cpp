/**
 * CR29 Debug Miner - Simplified for debugging
 * Tests each kernel stage independently
 */

#include <CL/cl.h>
#include <vector>
#include <string>
#include <fstream>
#include <sstream>
#include <iostream>
#include <chrono>
#include <cstring>

// Simplified parameters for debugging
constexpr uint32_t EDGEBITS = 20;  // Reduced for testing (1M edges instead of 512M)
constexpr uint64_t NEDGES = 1ULL << EDGEBITS;
constexpr uint32_t NODEBITS = EDGEBITS + 1;
constexpr uint64_t NNODES = 1ULL << NODEBITS;

constexpr uint32_t XBITS = 5;
constexpr uint32_t NX = 1 << XBITS;  // 32 buckets

constexpr size_t WORKGROUP_SIZE = 256;

// Simplified SipHash test kernel
const char* testKernelSrc = R"(
#define ROTL64(x, n) (((x) << (n)) | ((x) >> (64 - (n))))

inline ulong siphash24(ulong4 keys, ulong nonce) {
    ulong v0 = keys.s0;
    ulong v1 = keys.s1;
    ulong v2 = keys.s2;
    ulong v3 = keys.s3 ^ nonce;

    // 2 compression rounds
    v0 += v1; v2 += v3;
    v1 = ROTL64(v1, 13); v3 = ROTL64(v3, 16);
    v1 ^= v0; v3 ^= v2;
    v0 = ROTL64(v0, 32);
    v2 += v1; v0 += v3;
    v1 = ROTL64(v1, 17); v3 = ROTL64(v3, 21);
    v1 ^= v2; v3 ^= v0;
    v2 = ROTL64(v2, 32);

    v0 += v1; v2 += v3;
    v1 = ROTL64(v1, 13); v3 = ROTL64(v3, 16);
    v1 ^= v0; v3 ^= v2;
    v0 = ROTL64(v0, 32);
    v2 += v1; v0 += v3;
    v1 = ROTL64(v1, 17); v3 = ROTL64(v3, 21);
    v1 ^= v2; v3 ^= v0;
    v2 = ROTL64(v2, 32);

    v0 ^= nonce;
    v2 ^= 0xff;

    // 4 finalization rounds
    for (int i = 0; i < 4; i++) {
        v0 += v1; v2 += v3;
        v1 = ROTL64(v1, 13); v3 = ROTL64(v3, 16);
        v1 ^= v0; v3 ^= v2;
        v0 = ROTL64(v0, 32);
        v2 += v1; v0 += v3;
        v1 = ROTL64(v1, 17); v3 = ROTL64(v3, 21);
        v1 ^= v2; v3 ^= v0;
        v2 = ROTL64(v2, 32);
    }

    return v0 ^ v1 ^ v2 ^ v3;
}

// Simple edge generation - just count how many edges we generate
__kernel void CountEdges(
    __global uint* edgeCount,
    ulong4 sipkeys,
    uint edgeBits,
    uint totalEdges
) {
    uint gid = get_global_id(0);
    uint stride = get_global_size(0);

    uint localCount = 0;
    uint nodeMask = (1u << (edgeBits + 1)) - 1;

    for (uint nonce = gid; nonce < totalEdges; nonce += stride) {
        ulong h0 = siphash24(sipkeys, 2 * (ulong)nonce);
        ulong h1 = siphash24(sipkeys, 2 * (ulong)nonce + 1);

        uint node0 = (uint)(h0 & nodeMask);
        uint node1 = (uint)(h1 & nodeMask) | 1;

        // Just count valid edges
        if (node0 != node1) {
            localCount++;
        }
    }

    atomic_add(edgeCount, localCount);
}

// Generate edges into buckets
__kernel void GenerateEdges(
    __global ulong* edges,
    __global uint* bucketCounts,
    ulong4 sipkeys,
    uint edgeBits,
    uint xbits,
    uint maxEdgesPerBucket
) {
    uint gid = get_global_id(0);
    uint stride = get_global_size(0);
    uint totalEdges = 1u << edgeBits;
    uint nodeMask = (1u << (edgeBits + 1)) - 1;
    uint numBuckets = 1u << xbits;
    uint bucketMask = numBuckets - 1;

    for (uint nonce = gid; nonce < totalEdges; nonce += stride) {
        ulong h0 = siphash24(sipkeys, 2 * (ulong)nonce);
        ulong h1 = siphash24(sipkeys, 2 * (ulong)nonce + 1);

        uint node0 = (uint)(h0 & nodeMask);
        uint node1 = (uint)(h1 & nodeMask) | 1;

        // Bucket by high bits of node0
        uint bucket = (node0 >> (edgeBits + 1 - xbits)) & bucketMask;

        // Get slot in bucket
        uint slot = atomic_inc(&bucketCounts[bucket]);

        if (slot < maxEdgesPerBucket) {
            // Pack edge: node0 in low 32 bits, node1 in high 32 bits
            ulong edge = ((ulong)node1 << 32) | node0;
            edges[bucket * maxEdgesPerBucket + slot] = edge;
        }
    }
}

// Count degrees and trim
__kernel void TrimEdges(
    __global ulong* srcEdges,
    __global ulong* dstEdges,
    __global uint* srcCounts,
    __global uint* dstCounts,
    __global uint* counters,  // External counter buffer
    uint bucket,
    uint maxEdgesPerBucket,
    uint counterSize,
    uint round
) {
    uint lid = get_local_id(0);
    uint groupSize = get_local_size(0);

    uint srcCount = srcCounts[bucket];
    __global ulong* src = srcEdges + bucket * maxEdgesPerBucket;
    __global ulong* dst = dstEdges + bucket * maxEdgesPerBucket;
    __global uint* cnt = counters + bucket * counterSize;

    // Clear counters (2-bit packed)
    for (uint i = lid; i < counterSize; i += groupSize) {
        cnt[i] = 0;
    }
    barrier(CLK_GLOBAL_MEM_FENCE);

    // Count degrees
    for (uint i = lid; i < srcCount; i += groupSize) {
        ulong edge = src[i];
        uint node = (round & 1) ? (uint)(edge >> 32) : (uint)edge;

        uint idx = (node >> 4) % counterSize;
        uint shift = (node & 0xF) * 2;

        // Increment 2-bit counter
        atomic_add(&cnt[idx], 1u << shift);
    }
    barrier(CLK_GLOBAL_MEM_FENCE);

    // Copy edges with degree >= 2
    __local uint dstCount;
    if (lid == 0) dstCount = 0;
    barrier(CLK_LOCAL_MEM_FENCE);

    for (uint i = lid; i < srcCount; i += groupSize) {
        ulong edge = src[i];
        uint node = (round & 1) ? (uint)(edge >> 32) : (uint)edge;

        uint idx = (node >> 4) % counterSize;
        uint shift = (node & 0xF) * 2;
        uint deg = (cnt[idx] >> shift) & 3;

        if (deg >= 2) {
            uint slot = atomic_inc(&dstCount);
            dst[slot] = edge;
        }
    }
    barrier(CLK_LOCAL_MEM_FENCE);

    if (lid == 0) {
        dstCounts[bucket] = dstCount;
    }
}
)";

class DebugMiner {
    cl_platform_id platform = nullptr;
    cl_device_id device = nullptr;
    cl_context context = nullptr;
    cl_command_queue queue = nullptr;
    cl_program program = nullptr;

    cl_kernel countKernel = nullptr;
    cl_kernel genKernel = nullptr;
    cl_kernel trimKernel = nullptr;

public:
    bool init(int deviceIndex) {
        cl_int err;

        // Get AMD platform
        cl_uint numPlatforms = 0;
        err = clGetPlatformIDs(0, nullptr, &numPlatforms);
        if (err != CL_SUCCESS || numPlatforms == 0) {
            std::cerr << "No OpenCL platforms found (err=" << err << ", count=" << numPlatforms << ")\n";
            return false;
        }

        std::cout << "Found " << numPlatforms << " OpenCL platform(s)\n";
        std::vector<cl_platform_id> platforms(numPlatforms);
        clGetPlatformIDs(numPlatforms, platforms.data(), nullptr);

        for (size_t i = 0; i < platforms.size(); i++) {
            char vendor[256] = {0};
            char name[256] = {0};
            clGetPlatformInfo(platforms[i], CL_PLATFORM_VENDOR, 256, vendor, nullptr);
            clGetPlatformInfo(platforms[i], CL_PLATFORM_NAME, 256, name, nullptr);
            std::cout << "  Platform " << i << ": " << name << " (" << vendor << ")\n";

            // Check for AMD - match "AMD" or "Advanced Micro"
            if (strstr(vendor, "AMD") || strstr(vendor, "Advanced Micro")) {
                platform = platforms[i];
                std::cout << "  -> Selected AMD platform\n";
            }
        }

        if (!platform) {
            std::cerr << "AMD platform not found\n";
            return false;
        }

        // Get device
        cl_uint numDevices;
        clGetDeviceIDs(platform, CL_DEVICE_TYPE_GPU, 0, nullptr, &numDevices);
        std::vector<cl_device_id> devices(numDevices);
        clGetDeviceIDs(platform, CL_DEVICE_TYPE_GPU, numDevices, devices.data(), nullptr);

        if (deviceIndex >= (int)numDevices) {
            std::cerr << "Invalid device index\n";
            return false;
        }

        device = devices[deviceIndex];

        char name[256];
        clGetDeviceInfo(device, CL_DEVICE_NAME, 256, name, nullptr);
        std::cout << "Using device: " << name << "\n";

        // Create context and queue
        context = clCreateContext(nullptr, 1, &device, nullptr, nullptr, &err);
        queue = clCreateCommandQueueWithProperties(context, device, nullptr, &err);

        // Build program
        const char* src = testKernelSrc;
        size_t srcLen = strlen(src);
        program = clCreateProgramWithSource(context, 1, &src, &srcLen, &err);

        err = clBuildProgram(program, 1, &device, "-cl-std=CL2.0", nullptr, nullptr);
        if (err != CL_SUCCESS) {
            size_t logSize;
            clGetProgramBuildInfo(program, device, CL_PROGRAM_BUILD_LOG, 0, nullptr, &logSize);
            std::vector<char> log(logSize);
            clGetProgramBuildInfo(program, device, CL_PROGRAM_BUILD_LOG, logSize, log.data(), nullptr);
            std::cerr << "Build error:\n" << log.data() << "\n";
            return false;
        }

        countKernel = clCreateKernel(program, "CountEdges", &err);
        genKernel = clCreateKernel(program, "GenerateEdges", &err);
        trimKernel = clCreateKernel(program, "TrimEdges", &err);

        if (!countKernel || !genKernel || !trimKernel) {
            std::cerr << "Failed to create kernels\n";
            return false;
        }

        std::cout << "Kernels compiled successfully!\n";
        return true;
    }

    void testEdgeCount() {
        std::cout << "\n=== Test 1: Edge Count ===\n";
        std::cout << "Generating " << NEDGES << " edges...\n";

        cl_int err;

        // Create counter buffer
        cl_mem countBuf = clCreateBuffer(context, CL_MEM_READ_WRITE, sizeof(uint32_t), nullptr, &err);
        uint32_t zero = 0;
        clEnqueueWriteBuffer(queue, countBuf, CL_TRUE, 0, sizeof(uint32_t), &zero, 0, nullptr, nullptr);

        // Set up SipHash keys
        cl_ulong4 sipkeys = {{
            0x0706050403020100ULL,
            0x0f0e0d0c0b0a0908ULL,
            0x0706050403020100ULL ^ 0x736f6d6570736575ULL,
            0x0f0e0d0c0b0a0908ULL ^ 0x646f72616e646f6dULL
        }};

        uint32_t edgeBits = EDGEBITS;
        uint32_t totalEdges = NEDGES;

        clSetKernelArg(countKernel, 0, sizeof(cl_mem), &countBuf);
        clSetKernelArg(countKernel, 1, sizeof(cl_ulong4), &sipkeys);
        clSetKernelArg(countKernel, 2, sizeof(uint32_t), &edgeBits);
        clSetKernelArg(countKernel, 3, sizeof(uint32_t), &totalEdges);

        size_t globalSize = 65536;
        size_t localSize = 256;

        auto start = std::chrono::high_resolution_clock::now();
        err = clEnqueueNDRangeKernel(queue, countKernel, 1, nullptr, &globalSize, &localSize, 0, nullptr, nullptr);
        clFinish(queue);
        auto end = std::chrono::high_resolution_clock::now();

        if (err != CL_SUCCESS) {
            std::cerr << "CountEdges kernel failed: " << err << "\n";
            return;
        }

        uint32_t count;
        clEnqueueReadBuffer(queue, countBuf, CL_TRUE, 0, sizeof(uint32_t), &count, 0, nullptr, nullptr);

        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
        std::cout << "Generated edges: " << count << " / " << NEDGES << "\n";
        std::cout << "Time: " << duration.count() << "ms\n";

        clReleaseMemObject(countBuf);
    }

    void testEdgeGeneration() {
        std::cout << "\n=== Test 2: Edge Generation with Buckets ===\n";

        cl_int err;
        uint32_t maxEdgesPerBucket = NEDGES / NX + 1024;  // Extra padding

        std::cout << "Buckets: " << NX << "\n";
        std::cout << "Max edges per bucket: " << maxEdgesPerBucket << "\n";

        // Allocate buffers
        size_t edgeBufSize = (size_t)NX * maxEdgesPerBucket * sizeof(uint64_t);
        size_t countBufSize = NX * sizeof(uint32_t);

        std::cout << "Edge buffer size: " << (edgeBufSize / 1024 / 1024) << " MB\n";

        cl_mem edgeBuf = clCreateBuffer(context, CL_MEM_READ_WRITE, edgeBufSize, nullptr, &err);
        cl_mem countBuf = clCreateBuffer(context, CL_MEM_READ_WRITE, countBufSize, nullptr, &err);

        if (err != CL_SUCCESS) {
            std::cerr << "Failed to allocate buffers: " << err << "\n";
            return;
        }

        // Zero counts
        std::vector<uint32_t> zeroCounts(NX, 0);
        clEnqueueWriteBuffer(queue, countBuf, CL_TRUE, 0, countBufSize, zeroCounts.data(), 0, nullptr, nullptr);

        // Set up kernel args
        cl_ulong4 sipkeys = {{
            0x0706050403020100ULL,
            0x0f0e0d0c0b0a0908ULL,
            0x0706050403020100ULL ^ 0x736f6d6570736575ULL,
            0x0f0e0d0c0b0a0908ULL ^ 0x646f72616e646f6dULL
        }};

        uint32_t edgeBits = EDGEBITS;
        uint32_t xbits = XBITS;

        clSetKernelArg(genKernel, 0, sizeof(cl_mem), &edgeBuf);
        clSetKernelArg(genKernel, 1, sizeof(cl_mem), &countBuf);
        clSetKernelArg(genKernel, 2, sizeof(cl_ulong4), &sipkeys);
        clSetKernelArg(genKernel, 3, sizeof(uint32_t), &edgeBits);
        clSetKernelArg(genKernel, 4, sizeof(uint32_t), &xbits);
        clSetKernelArg(genKernel, 5, sizeof(uint32_t), &maxEdgesPerBucket);

        size_t globalSize = 65536;
        size_t localSize = 256;

        auto start = std::chrono::high_resolution_clock::now();
        err = clEnqueueNDRangeKernel(queue, genKernel, 1, nullptr, &globalSize, &localSize, 0, nullptr, nullptr);
        clFinish(queue);
        auto end = std::chrono::high_resolution_clock::now();

        if (err != CL_SUCCESS) {
            std::cerr << "GenerateEdges kernel failed: " << err << "\n";
            return;
        }

        // Read bucket counts
        std::vector<uint32_t> counts(NX);
        clEnqueueReadBuffer(queue, countBuf, CL_TRUE, 0, countBufSize, counts.data(), 0, nullptr, nullptr);

        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

        uint64_t totalEdges = 0;
        std::cout << "Bucket counts:\n";
        for (int i = 0; i < NX; i++) {
            totalEdges += counts[i];
            if (i < 8 || i >= NX - 2) {
                std::cout << "  Bucket " << i << ": " << counts[i] << "\n";
            } else if (i == 8) {
                std::cout << "  ...\n";
            }
        }
        std::cout << "Total edges: " << totalEdges << " / " << NEDGES << "\n";
        std::cout << "Time: " << duration.count() << "ms\n";

        clReleaseMemObject(edgeBuf);
        clReleaseMemObject(countBuf);
    }

    void cleanup() {
        if (countKernel) clReleaseKernel(countKernel);
        if (genKernel) clReleaseKernel(genKernel);
        if (trimKernel) clReleaseKernel(trimKernel);
        if (program) clReleaseProgram(program);
        if (queue) clReleaseCommandQueue(queue);
        if (context) clReleaseContext(context);
    }
};

int main() {
    std::cout << "CR29 RDNA4 Kernel Debug Test\n";
    std::cout << "============================\n";
    std::cout << "Edge bits: " << EDGEBITS << " (" << NEDGES << " edges)\n";
    std::cout << "Bucket bits: " << XBITS << " (" << NX << " buckets)\n\n";

    DebugMiner miner;

    // Device 1 = RX 9070 XT
    if (!miner.init(1)) {
        return 1;
    }

    miner.testEdgeCount();
    miner.testEdgeGeneration();

    miner.cleanup();
    return 0;
}
