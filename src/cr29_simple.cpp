/**
 * CR29 Simple Miner - Complete working implementation
 * Uses verified edge generation from debug tests
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

// Proof size for CR29
constexpr uint32_t PROOFSIZE = 42;

// CR29 Parameters
constexpr uint32_t EDGEBITS = 29;
constexpr uint64_t NEDGES = 1ULL << EDGEBITS;
constexpr uint32_t NODEBITS = EDGEBITS + 1;
constexpr uint64_t NNODES = 1ULL << NODEBITS;
constexpr uint32_t NODEMASK = NNODES - 1;

// Bucketing
constexpr uint32_t XBITS = 6;
constexpr uint32_t NUMBUCKETS = 1 << XBITS;
constexpr uint32_t TRIMROUNDS = 176;  // Full CR29 trim rounds

// Buffer sizing
constexpr uint32_t MAX_EDGES_PER_BUCKET = (NEDGES / NUMBUCKETS) + 4096;  // With padding
constexpr uint32_t COUNTER_WORDS = 65536;  // Per bucket

class SimpleMiner {
    cl_platform_id platform = nullptr;
    cl_device_id device = nullptr;
    cl_context context = nullptr;
    cl_command_queue queue = nullptr;
    cl_program program = nullptr;

    // Kernels
    cl_kernel generateKernel = nullptr;
    cl_kernel countKernel = nullptr;
    cl_kernel trimKernel = nullptr;
    cl_kernel consolidateKernel = nullptr;

    // Buffers
    cl_mem edgesA = nullptr;
    cl_mem edgesB = nullptr;
    cl_mem countsA = nullptr;
    cl_mem countsB = nullptr;
    cl_mem degreeCounters = nullptr;
    cl_mem output = nullptr;
    cl_mem outputCount = nullptr;

    bool initialized = false;

public:
    bool init(int deviceIndex) {
        cl_int err;

        // Find AMD platform
        cl_uint numPlatforms = 0;
        err = clGetPlatformIDs(0, nullptr, &numPlatforms);
        if (err != CL_SUCCESS || numPlatforms == 0) {
            std::cerr << "No OpenCL platforms found\n";
            return false;
        }

        std::vector<cl_platform_id> platforms(numPlatforms);
        clGetPlatformIDs(numPlatforms, platforms.data(), nullptr);

        std::cout << "Found " << numPlatforms << " platform(s)\n";

        for (size_t i = 0; i < platforms.size(); i++) {
            char vendor[256] = {0};
            char name[256] = {0};
            clGetPlatformInfo(platforms[i], CL_PLATFORM_VENDOR, 256, vendor, nullptr);
            clGetPlatformInfo(platforms[i], CL_PLATFORM_NAME, 256, name, nullptr);
            std::cout << "  Platform " << i << ": " << name << " (" << vendor << ")\n";

            if (strstr(vendor, "AMD") || strstr(vendor, "Advanced Micro")) {
                platform = platforms[i];
                std::cout << "  -> Selected\n";
            }
        }

        if (!platform) {
            std::cerr << "AMD platform not found\n";
            return false;
        }

        // Get GPU devices
        cl_uint numDevices = 0;
        clGetDeviceIDs(platform, CL_DEVICE_TYPE_GPU, 0, nullptr, &numDevices);
        std::vector<cl_device_id> devices(numDevices);
        clGetDeviceIDs(platform, CL_DEVICE_TYPE_GPU, numDevices, devices.data(), nullptr);

        std::cout << "Found " << numDevices << " GPU(s)\n";

        for (size_t i = 0; i < devices.size(); i++) {
            char name[256] = {0};
            clGetDeviceInfo(devices[i], CL_DEVICE_NAME, 256, name, nullptr);
            std::cout << "  GPU " << i << ": " << name << "\n";
        }

        if (deviceIndex >= (int)numDevices) {
            std::cerr << "Device index " << deviceIndex << " out of range\n";
            return false;
        }

        device = devices[deviceIndex];

        char deviceName[256] = {0};
        clGetDeviceInfo(device, CL_DEVICE_NAME, 256, deviceName, nullptr);
        std::cout << "\nUsing: " << deviceName << "\n";

        // Create context and queue
        context = clCreateContext(nullptr, 1, &device, nullptr, nullptr, &err);
        if (err != CL_SUCCESS) {
            std::cerr << "Failed to create context\n";
            return false;
        }

        queue = clCreateCommandQueueWithProperties(context, device, nullptr, &err);

        // Load and build kernel
        if (!buildKernels()) {
            return false;
        }

        // Allocate buffers
        if (!allocateBuffers()) {
            return false;
        }

        initialized = true;
        std::cout << "Initialization complete\n\n";
        return true;
    }

    bool buildKernels() {
        std::cout << "Loading kernel source...\n";

        std::ifstream file("src/cr29_simple.cl");
        if (!file.is_open()) {
            std::cerr << "Failed to open src/cr29_simple.cl\n";
            return false;
        }

        std::stringstream ss;
        ss << file.rdbuf();
        std::string source = ss.str();

        const char* src = source.c_str();
        size_t srcLen = source.length();

        cl_int err;
        program = clCreateProgramWithSource(context, 1, &src, &srcLen, &err);
        if (err != CL_SUCCESS) {
            std::cerr << "Failed to create program\n";
            return false;
        }

        std::cout << "Building kernels...\n";

        err = clBuildProgram(program, 1, &device, "-cl-std=CL2.0 -cl-mad-enable", nullptr, nullptr);
        if (err != CL_SUCCESS) {
            size_t logSize;
            clGetProgramBuildInfo(program, device, CL_PROGRAM_BUILD_LOG, 0, nullptr, &logSize);
            std::vector<char> log(logSize);
            clGetProgramBuildInfo(program, device, CL_PROGRAM_BUILD_LOG, logSize, log.data(), nullptr);
            std::cerr << "Build failed:\n" << log.data() << "\n";
            return false;
        }

        generateKernel = clCreateKernel(program, "GenerateEdges", &err);
        countKernel = clCreateKernel(program, "CountDegrees", &err);
        trimKernel = clCreateKernel(program, "TrimEdges", &err);
        consolidateKernel = clCreateKernel(program, "ConsolidateEdges", &err);

        if (!generateKernel || !countKernel || !trimKernel || !consolidateKernel) {
            std::cerr << "Failed to create kernels\n";
            return false;
        }

        std::cout << "Kernels built successfully\n";
        return true;
    }

    bool allocateBuffers() {
        cl_int err;

        size_t edgeBufferSize = (size_t)NUMBUCKETS * MAX_EDGES_PER_BUCKET * sizeof(uint64_t);
        size_t countBufferSize = NUMBUCKETS * sizeof(uint32_t);
        size_t counterBufferSize = (size_t)NUMBUCKETS * COUNTER_WORDS * sizeof(uint32_t);
        size_t outputBufferSize = 10 * 1024 * 1024 * sizeof(uint64_t);  // 10M edges max

        std::cout << "Allocating buffers:\n";
        std::cout << "  Edge buffers: " << (edgeBufferSize / 1024 / 1024) << " MB each\n";
        std::cout << "  Counter buffer: " << (counterBufferSize / 1024 / 1024) << " MB\n";

        edgesA = clCreateBuffer(context, CL_MEM_READ_WRITE, edgeBufferSize, nullptr, &err);
        edgesB = clCreateBuffer(context, CL_MEM_READ_WRITE, edgeBufferSize, nullptr, &err);
        countsA = clCreateBuffer(context, CL_MEM_READ_WRITE, countBufferSize, nullptr, &err);
        countsB = clCreateBuffer(context, CL_MEM_READ_WRITE, countBufferSize, nullptr, &err);
        degreeCounters = clCreateBuffer(context, CL_MEM_READ_WRITE, counterBufferSize, nullptr, &err);
        output = clCreateBuffer(context, CL_MEM_READ_WRITE, outputBufferSize, nullptr, &err);
        outputCount = clCreateBuffer(context, CL_MEM_READ_WRITE, sizeof(uint32_t), nullptr, &err);

        if (err != CL_SUCCESS) {
            std::cerr << "Failed to allocate buffers: " << err << "\n";
            return false;
        }

        return true;
    }

    struct SipKeys {
        uint64_t k0, k1, k2, k3;
    };

    uint32_t mine(const SipKeys& keys) {
        if (!initialized) {
            std::cerr << "Not initialized\n";
            return 0;
        }

        auto startTime = std::chrono::high_resolution_clock::now();

        cl_int err;
        cl_ulong4 sipkeys = {{keys.k0, keys.k1, keys.k2, keys.k3}};

        // Zero bucket counts
        std::vector<uint32_t> zeroCounts(NUMBUCKETS, 0);
        clEnqueueWriteBuffer(queue, countsA, CL_TRUE, 0,
                             NUMBUCKETS * sizeof(uint32_t), zeroCounts.data(), 0, nullptr, nullptr);

        std::cout << "Generating edges...\n";

        // Generate edges
        uint32_t edgeBits = EDGEBITS;
        uint32_t xbits = XBITS;
        uint32_t maxPerBucket = MAX_EDGES_PER_BUCKET;

        clSetKernelArg(generateKernel, 0, sizeof(cl_mem), &edgesA);
        clSetKernelArg(generateKernel, 1, sizeof(cl_mem), &countsA);
        clSetKernelArg(generateKernel, 2, sizeof(cl_ulong4), &sipkeys);
        clSetKernelArg(generateKernel, 3, sizeof(uint32_t), &edgeBits);
        clSetKernelArg(generateKernel, 4, sizeof(uint32_t), &xbits);
        clSetKernelArg(generateKernel, 5, sizeof(uint32_t), &maxPerBucket);

        size_t genGlobalSize = 1024 * 256;  // Many threads for edge generation
        size_t genLocalSize = 256;

        err = clEnqueueNDRangeKernel(queue, generateKernel, 1, nullptr,
                                      &genGlobalSize, &genLocalSize, 0, nullptr, nullptr);
        clFinish(queue);

        if (err != CL_SUCCESS) {
            std::cerr << "Edge generation failed: " << err << "\n";
            return 0;
        }

        // Read bucket counts
        std::vector<uint32_t> counts(NUMBUCKETS);
        clEnqueueReadBuffer(queue, countsA, CL_TRUE, 0,
                           NUMBUCKETS * sizeof(uint32_t), counts.data(), 0, nullptr, nullptr);

        uint64_t totalEdges = 0;
        for (auto c : counts) totalEdges += c;
        std::cout << "Generated " << totalEdges << " edges\n";

        auto genTime = std::chrono::high_resolution_clock::now();

        // Trimming rounds
        std::cout << "Trimming " << TRIMROUNDS << " rounds...\n";

        cl_mem* srcEdges = &edgesA;
        cl_mem* dstEdges = &edgesB;
        cl_mem* srcCounts = &countsA;
        cl_mem* dstCounts = &countsB;

        uint32_t nodeMask = NODEMASK;
        uint32_t counterWords = COUNTER_WORDS;

        for (uint32_t round = 0; round < TRIMROUNDS; round++) {
            // Zero destination counts
            clEnqueueWriteBuffer(queue, *dstCounts, CL_FALSE, 0,
                                NUMBUCKETS * sizeof(uint32_t), zeroCounts.data(), 0, nullptr, nullptr);

            // Set static kernel args once per round
            clSetKernelArg(countKernel, 0, sizeof(cl_mem), srcEdges);
            clSetKernelArg(countKernel, 1, sizeof(cl_mem), srcCounts);
            clSetKernelArg(countKernel, 2, sizeof(cl_mem), &degreeCounters);
            clSetKernelArg(countKernel, 4, sizeof(uint32_t), &maxPerBucket);
            clSetKernelArg(countKernel, 5, sizeof(uint32_t), &counterWords);
            clSetKernelArg(countKernel, 6, sizeof(uint32_t), &nodeMask);
            clSetKernelArg(countKernel, 7, sizeof(uint32_t), &round);

            clSetKernelArg(trimKernel, 0, sizeof(cl_mem), srcEdges);
            clSetKernelArg(trimKernel, 1, sizeof(cl_mem), dstEdges);
            clSetKernelArg(trimKernel, 2, sizeof(cl_mem), srcCounts);
            clSetKernelArg(trimKernel, 3, sizeof(cl_mem), dstCounts);
            clSetKernelArg(trimKernel, 4, sizeof(cl_mem), &degreeCounters);
            clSetKernelArg(trimKernel, 6, sizeof(uint32_t), &maxPerBucket);
            clSetKernelArg(trimKernel, 7, sizeof(uint32_t), &counterWords);
            clSetKernelArg(trimKernel, 8, sizeof(uint32_t), &nodeMask);
            clSetKernelArg(trimKernel, 9, sizeof(uint32_t), &round);

            size_t localSize = 256;
            size_t globalSize = 256;

            // Queue all buckets (count then trim for each)
            for (uint32_t bucket = 0; bucket < NUMBUCKETS; bucket++) {
                clSetKernelArg(countKernel, 3, sizeof(uint32_t), &bucket);
                clEnqueueNDRangeKernel(queue, countKernel, 1, nullptr,
                                       &globalSize, &localSize, 0, nullptr, nullptr);

                clSetKernelArg(trimKernel, 5, sizeof(uint32_t), &bucket);
                clEnqueueNDRangeKernel(queue, trimKernel, 1, nullptr,
                                       &globalSize, &localSize, 0, nullptr, nullptr);
            }

            clFinish(queue);

            // Swap buffers
            std::swap(srcEdges, dstEdges);
            std::swap(srcCounts, dstCounts);

            // Print progress every 20 rounds
            if ((round + 1) % 20 == 0) {
                clEnqueueReadBuffer(queue, *srcCounts, CL_TRUE, 0,
                                   NUMBUCKETS * sizeof(uint32_t), counts.data(), 0, nullptr, nullptr);
                totalEdges = 0;
                for (auto c : counts) totalEdges += c;
                std::cout << "  Round " << (round + 1) << ": " << totalEdges << " edges\n";
            }
        }

        auto trimTime = std::chrono::high_resolution_clock::now();

        // Consolidate remaining edges
        uint32_t zero = 0;
        clEnqueueWriteBuffer(queue, outputCount, CL_TRUE, 0, sizeof(uint32_t), &zero, 0, nullptr, nullptr);

        clSetKernelArg(consolidateKernel, 0, sizeof(cl_mem), srcEdges);
        clSetKernelArg(consolidateKernel, 1, sizeof(cl_mem), srcCounts);
        clSetKernelArg(consolidateKernel, 2, sizeof(cl_mem), &output);
        clSetKernelArg(consolidateKernel, 3, sizeof(cl_mem), &outputCount);
        uint32_t numBuckets = NUMBUCKETS;
        clSetKernelArg(consolidateKernel, 4, sizeof(uint32_t), &numBuckets);
        clSetKernelArg(consolidateKernel, 5, sizeof(uint32_t), &maxPerBucket);

        size_t consGlobalSize = NUMBUCKETS;
        clEnqueueNDRangeKernel(queue, consolidateKernel, 1, nullptr,
                               &consGlobalSize, nullptr, 0, nullptr, nullptr);
        clFinish(queue);

        uint32_t finalCount;
        clEnqueueReadBuffer(queue, outputCount, CL_TRUE, 0, sizeof(uint32_t), &finalCount, 0, nullptr, nullptr);

        auto endTime = std::chrono::high_resolution_clock::now();

        auto genDuration = std::chrono::duration_cast<std::chrono::milliseconds>(genTime - startTime);
        auto trimDuration = std::chrono::duration_cast<std::chrono::milliseconds>(trimTime - genTime);
        auto totalDuration = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime);

        std::cout << "\n=== Results ===\n";
        std::cout << "Final edges: " << finalCount << "\n";
        std::cout << "Generation: " << genDuration.count() << "ms\n";
        std::cout << "Trimming: " << trimDuration.count() << "ms\n";
        std::cout << "Total: " << totalDuration.count() << "ms\n";

        return finalCount;
    }

    // Read trimmed edges from GPU
    std::vector<uint64_t> readEdges(uint32_t count) {
        std::vector<uint64_t> edges(count);
        clEnqueueReadBuffer(queue, output, CL_TRUE, 0,
                           count * sizeof(uint64_t), edges.data(), 0, nullptr, nullptr);
        return edges;
    }

    void cleanup() {
        if (generateKernel) clReleaseKernel(generateKernel);
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

    ~SimpleMiner() {
        cleanup();
    }
};

/**
 * Simple cycle detection using path following
 * Finds 42-cycles in the trimmed edge graph
 */
class CycleDetector {
public:
    // Build adjacency list from packed edges
    std::unordered_map<uint32_t, std::vector<std::pair<uint32_t, uint32_t>>> adjList;

    void buildGraph(const std::vector<uint64_t>& edges) {
        adjList.clear();
        for (uint32_t i = 0; i < edges.size(); i++) {
            uint32_t node0 = edges[i] & 0xFFFFFFFF;
            uint32_t node1 = edges[i] >> 32;
            adjList[node0].push_back({node1, i});
            adjList[node1].push_back({node0, i});
        }
    }

    // Find 42-cycles using path following
    bool findCycle(std::vector<uint32_t>& proof) {
        proof.clear();

        // Try starting from each edge
        for (auto& [startNode, neighbors] : adjList) {
            if (neighbors.size() != 2) continue;  // Need exactly 2 neighbors for cycle

            // Path follow from this node
            std::vector<uint32_t> path;
            std::unordered_set<uint32_t> usedEdges;

            uint32_t current = startNode;
            uint32_t edgeIdx = neighbors[0].second;
            uint32_t next = neighbors[0].first;

            path.push_back(edgeIdx);
            usedEdges.insert(edgeIdx);

            while (path.size() < PROOFSIZE && next != startNode) {
                auto it = adjList.find(next);
                if (it == adjList.end()) break;

                auto& nextNeighbors = it->second;
                bool found = false;

                for (auto& [neighbor, idx] : nextNeighbors) {
                    if (usedEdges.find(idx) == usedEdges.end()) {
                        path.push_back(idx);
                        usedEdges.insert(idx);
                        current = next;
                        next = neighbor;
                        found = true;
                        break;
                    }
                }

                if (!found) break;
            }

            // Check if we found a 42-cycle
            if (path.size() == PROOFSIZE && next == startNode) {
                proof = path;
                std::sort(proof.begin(), proof.end());
                return true;
            }
        }

        return false;
    }
};

int main() {
    std::cout << "CR29 Simple Miner for RDNA 4\n";
    std::cout << "============================\n";
    std::cout << "Edge bits: " << EDGEBITS << " (" << NEDGES << " edges)\n";
    std::cout << "Buckets: " << NUMBUCKETS << "\n";
    std::cout << "Trim rounds: " << TRIMROUNDS << "\n\n";

    SimpleMiner miner;

    // Use device 1 (RX 9070 XT, device 0 is integrated)
    if (!miner.init(1)) {
        std::cerr << "Initialization failed\n";
        return 1;
    }

    // Test with sample keys
    SimpleMiner::SipKeys keys = {
        0x0706050403020100ULL,
        0x0f0e0d0c0b0a0908ULL,
        0x0706050403020100ULL ^ 0x736f6d6570736575ULL,
        0x0f0e0d0c0b0a0908ULL ^ 0x646f72616e646f6dULL
    };

    uint32_t remaining = miner.mine(keys);

    if (remaining > 0) {
        std::cout << "\n" << remaining << " edges remain, searching for 42-cycles...\n";

        // Read edges from GPU
        auto edges = miner.readEdges(remaining);

        // Build graph and search for cycles
        CycleDetector detector;
        auto cycleStart = std::chrono::high_resolution_clock::now();

        detector.buildGraph(edges);

        std::vector<uint32_t> proof;
        if (detector.findCycle(proof)) {
            auto cycleEnd = std::chrono::high_resolution_clock::now();
            auto cycleDuration = std::chrono::duration_cast<std::chrono::milliseconds>(cycleEnd - cycleStart);

            std::cout << "\n*** FOUND 42-CYCLE! ***\n";
            std::cout << "Proof (" << proof.size() << " edges): ";
            for (size_t i = 0; i < std::min((size_t)10, proof.size()); i++) {
                std::cout << proof[i] << " ";
            }
            if (proof.size() > 10) std::cout << "...";
            std::cout << "\nCycle detection time: " << cycleDuration.count() << "ms\n";
        } else {
            auto cycleEnd = std::chrono::high_resolution_clock::now();
            auto cycleDuration = std::chrono::duration_cast<std::chrono::milliseconds>(cycleEnd - cycleStart);
            std::cout << "No 42-cycle found (this is expected for most nonces)\n";
            std::cout << "Cycle detection time: " << cycleDuration.count() << "ms\n";
        }
    } else {
        std::cout << "\nNo edges remaining after trimming\n";
    }

    return 0;
}
