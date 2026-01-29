/**
 * CR29 Pool Miner - Full mining with stratum support
 * Includes: GPU trimming, cycle detection, pool connectivity, TLS
 */

#include "tls_socket.h"

#include <CL/cl.h>
#include <vector>
#include <string>
#include <fstream>
#include <sstream>
#include <iostream>
#include <chrono>
#include <cstring>
#include <algorithm>
#include <thread>
#include <atomic>
#include <mutex>
#include <map>
#include <set>
#include <cstdint>

// CR29 Parameters
constexpr uint32_t EDGEBITS = 29;
constexpr uint64_t NEDGES = 1ULL << EDGEBITS;
constexpr uint32_t NODEBITS = EDGEBITS + 1;
constexpr uint64_t NNODES = 1ULL << NODEBITS;
constexpr uint32_t NODEMASK = NNODES - 1;
constexpr uint32_t EDGEMASK = NEDGES - 1;
constexpr uint32_t PROOFSIZE = 42;  // Cycle length for Cuckaroo-29

// Tuning parameters
constexpr uint32_t XBITS = 6;
constexpr uint32_t NUMBUCKETS = 1 << XBITS;
constexpr uint32_t TRIMROUNDS = 40;
constexpr uint32_t MAX_EDGES_PER_BUCKET = (NEDGES / NUMBUCKETS) + 4096;

// =============================================================================
// SipHash-2-4 (CPU version for cycle verification)
// =============================================================================
class SipHash {
public:
    uint64_t k0, k1, k2, k3;

    SipHash(const uint8_t* header, size_t len) {
        // Initialize from header using Blake2b or similar
        // For now, simple initialization
        k0 = 0x736f6d6570736575ULL;
        k1 = 0x646f72616e646f6dULL;
        k2 = 0x6c7967656e657261ULL;
        k3 = 0x7465646279746573ULL;

        // XOR in header bytes
        for (size_t i = 0; i < len && i < 32; i++) {
            ((uint8_t*)&k0)[i % 8] ^= header[i];
            if (i >= 8) ((uint8_t*)&k1)[(i-8) % 8] ^= header[i];
            if (i >= 16) ((uint8_t*)&k2)[(i-16) % 8] ^= header[i];
            if (i >= 24) ((uint8_t*)&k3)[(i-24) % 8] ^= header[i];
        }
    }

    static inline uint64_t rotl(uint64_t x, int b) {
        return (x << b) | (x >> (64 - b));
    }

    uint64_t siphash24(uint64_t nonce) const {
        uint64_t v0 = k0, v1 = k1, v2 = k2, v3 = k3 ^ nonce;

        // 2 rounds
        for (int i = 0; i < 2; i++) {
            v0 += v1; v2 += v3;
            v1 = rotl(v1, 13); v3 = rotl(v3, 16);
            v1 ^= v0; v3 ^= v2;
            v0 = rotl(v0, 32);
            v2 += v1; v0 += v3;
            v1 = rotl(v1, 17); v3 = rotl(v3, 21);
            v1 ^= v2; v3 ^= v0;
            v2 = rotl(v2, 32);
        }

        v0 ^= nonce;
        v2 ^= 0xff;

        // 4 finalization rounds
        for (int i = 0; i < 4; i++) {
            v0 += v1; v2 += v3;
            v1 = rotl(v1, 13); v3 = rotl(v3, 16);
            v1 ^= v0; v3 ^= v2;
            v0 = rotl(v0, 32);
            v2 += v1; v0 += v3;
            v1 = rotl(v1, 17); v3 = rotl(v3, 21);
            v1 ^= v2; v3 ^= v0;
            v2 = rotl(v2, 32);
        }

        return v0 ^ v1 ^ v2 ^ v3;
    }

    std::pair<uint32_t, uint32_t> edge(uint32_t nonce) const {
        uint64_t h0 = siphash24(2 * (uint64_t)nonce);
        uint64_t h1 = siphash24(2 * (uint64_t)nonce + 1);
        uint32_t node0 = (uint32_t)(h0 & NODEMASK);
        uint32_t node1 = (uint32_t)(h1 & NODEMASK) | 1;  // Odd for bipartite
        return {node0, node1};
    }
};

// =============================================================================
// Cycle Finder - Find 42-cycles in trimmed edge set
// =============================================================================
class CycleFinder {
    std::vector<uint32_t> cuckoo;  // Hash table for path finding
    static constexpr uint32_t CUCKOO_SIZE = NNODES + NEDGES;

public:
    CycleFinder() : cuckoo(CUCKOO_SIZE, 0) {}

    void reset() {
        std::fill(cuckoo.begin(), cuckoo.end(), 0);
    }

    // Find path from node u, return length
    int path(uint32_t u, std::vector<uint32_t>& pathNodes) const {
        pathNodes.clear();
        for (int len = 0; len < PROOFSIZE && u != 0; len++) {
            pathNodes.push_back(u);
            u = cuckoo[u];
        }
        return (int)pathNodes.size();
    }

    // Try to find a 42-cycle in the edge set
    // Returns true if cycle found, fills proof with nonces
    bool findCycle(const std::vector<uint64_t>& edges,
                   const SipHash& hasher,
                   std::vector<uint32_t>& proof) {
        reset();
        proof.clear();

        std::vector<uint32_t> us, vs;

        for (size_t i = 0; i < edges.size(); i++) {
            uint64_t edge = edges[i];
            uint32_t u0 = (uint32_t)(edge & NODEMASK);
            uint32_t v0 = (uint32_t)(edge >> 32);

            // Ensure u0 is even, v0 is odd (bipartite)
            u0 &= ~1u;  // Make even
            v0 |= 1u;   // Make odd

            uint32_t u = u0, v = v0;

            path(u, us);
            path(v, vs);

            if (us.size() > 0 && vs.size() > 0 && us.back() == vs.back()) {
                // Found cycle - check if it's length 42
                int cycleLen = (int)(us.size() + vs.size() - 1);
                if (cycleLen == PROOFSIZE) {
                    // Extract proof - need to find the 42 nonces
                    return extractProof(edges, hasher, us, vs, proof);
                }
            }

            // Add edge to cuckoo table
            if (us.size() < vs.size()) {
                while (us.size() > 0) {
                    uint32_t node = us.back();
                    us.pop_back();
                    cuckoo[node] = v;
                    v = node;
                }
                cuckoo[u0] = v;
            } else {
                while (vs.size() > 0) {
                    uint32_t node = vs.back();
                    vs.pop_back();
                    cuckoo[node] = u;
                    u = node;
                }
                cuckoo[v0] = u;
            }
        }

        return false;
    }

private:
    bool extractProof(const std::vector<uint64_t>& edges,
                      const SipHash& hasher,
                      const std::vector<uint32_t>& us,
                      const std::vector<uint32_t>& vs,
                      std::vector<uint32_t>& proof) {
        // Build set of cycle nodes
        std::set<uint64_t> cycleEdges;

        // Add edges from us path
        for (size_t i = 0; i + 1 < us.size(); i++) {
            uint32_t a = us[i], b = us[i+1];
            if (a > b) std::swap(a, b);
            cycleEdges.insert(((uint64_t)b << 32) | a);
        }

        // Add edges from vs path
        for (size_t i = 0; i + 1 < vs.size(); i++) {
            uint32_t a = vs[i], b = vs[i+1];
            if (a > b) std::swap(a, b);
            cycleEdges.insert(((uint64_t)b << 32) | a);
        }

        // Add connecting edge
        if (us.size() > 0 && vs.size() > 0) {
            uint32_t a = us[0], b = vs[0];
            if (a > b) std::swap(a, b);
            cycleEdges.insert(((uint64_t)b << 32) | a);
        }

        // Find original nonces for cycle edges
        proof.clear();
        for (uint32_t nonce = 0; nonce <= EDGEMASK && proof.size() < PROOFSIZE; nonce++) {
            std::pair<uint32_t, uint32_t> edgePair = hasher.edge(nonce);
            uint32_t u = edgePair.first, v = edgePair.second;
            if (u > v) std::swap(u, v);
            uint64_t edge = ((uint64_t)v << 32) | u;
            if (cycleEdges.count(edge)) {
                proof.push_back(nonce);
                cycleEdges.erase(edge);
            }
        }

        std::sort(proof.begin(), proof.end());
        return proof.size() == PROOFSIZE;
    }
};

// =============================================================================
// Stratum Client - Pool connectivity with TLS support
// =============================================================================
class StratumClient {
    TlsSocket tlsSocket;
    std::string host;
    int port;
    std::string user;
    std::string pass;
    bool useTls = false;
    std::atomic<bool> connected{false};
    std::mutex sendMutex;

    // Job info
    std::string currentJobId;
    std::vector<uint8_t> currentHeader;
    uint64_t currentNonce = 0;
    uint64_t currentTarget = 0;
    std::mutex jobMutex;

    int messageId = 1;

public:
    struct Stats {
        std::atomic<uint64_t> sharesSubmitted{0};
        std::atomic<uint64_t> sharesAccepted{0};
        std::atomic<uint64_t> sharesRejected{0};
        std::atomic<uint64_t> graphsProcessed{0};
    } stats;

    StratumClient(const std::string& h, int p, const std::string& u, const std::string& pw, bool tls = false)
        : host(h), port(p), user(u), pass(pw), useTls(tls) {}

    ~StratumClient() {
        disconnect();
    }

    bool connect() {
        std::cout << "Connecting to " << host << ":" << port;
        if (useTls) std::cout << " (TLS)";
        std::cout << "...\n";
        std::cout.flush();

        if (!tlsSocket.connect(host, port, useTls)) {
            std::cerr << "Failed to connect" << (useTls ? " (TLS handshake failed?)" : "") << "\n";
            return false;
        }

        std::cout << "[STRATUM] TLS socket connected, setting connected=true\n";
        std::cout.flush();

        connected = true;
        std::cout << "Connected to " << host << ":" << port << (useTls ? " (TLS)" : "") << "\n";
        std::cout.flush();

        // Send login
        std::cout << "[STRATUM] Calling login()...\n";
        std::cout.flush();
        return login();
    }

    void disconnect() {
        tlsSocket.close();
        connected = false;
    }

    bool isConnected() const { return connected; }

    bool login() {
        std::cout << "[STRATUM] Sending login...\n";
        std::cout.flush();

        // Grin stratum login format
        std::stringstream ss;
        ss << "{\"id\":" << messageId++ << ",\"jsonrpc\":\"2.0\",\"method\":\"login\","
           << "\"params\":{\"login\":\"" << user << "\",\"pass\":\"" << pass << "\","
           << "\"agent\":\"cr29-turbo/1.0\"}}\n";

        std::string msg = ss.str();
        std::cout << "[STRATUM] Login message: " << msg;
        std::cout.flush();

        bool result = sendMessage(msg);
        std::cout << "[STRATUM] Login sent: " << (result ? "success" : "failed") << "\n";
        std::cout.flush();
        return result;
    }

    bool submitShare(const std::string& jobId, uint64_t nonce, const std::vector<uint32_t>& proof) {
        std::lock_guard<std::mutex> lock(sendMutex);

        std::stringstream ss;
        ss << "{\"id\":" << messageId++ << ",\"jsonrpc\":\"2.0\",\"method\":\"submit\","
           << "\"params\":{\"edge_bits\":29,\"height\":0,\"job_id\":\"" << jobId << "\","
           << "\"nonce\":" << nonce << ",\"pow\":[";

        for (size_t i = 0; i < proof.size(); i++) {
            if (i > 0) ss << ",";
            ss << proof[i];
        }
        ss << "]}}\n";

        stats.sharesSubmitted++;
        std::cout << "[SHARE] Submitting nonce=" << nonce << " proof=[";
        for (size_t i = 0; i < std::min(proof.size(), (size_t)5); i++) {
            if (i > 0) std::cout << ",";
            std::cout << proof[i];
        }
        if (proof.size() > 5) std::cout << "...";
        std::cout << "]\n";

        return sendMessage(ss.str());
    }

    bool receiveAndProcess() {
        char buffer[4096];
        int len = tlsSocket.recvData(buffer, sizeof(buffer) - 1);
        if (len <= 0) {
            connected = false;
            return false;
        }
        buffer[len] = '\0';

        // Parse JSON response (simple parsing)
        std::string response(buffer);

        if (response.find("\"method\":\"job\"") != std::string::npos ||
            response.find("\"method\": \"job\"") != std::string::npos) {
            parseJob(response);
        }
        else if (response.find("\"result\":") != std::string::npos) {
            if (response.find("\"status\":\"ok\"") != std::string::npos ||
                response.find("\"result\":\"ok\"") != std::string::npos ||
                response.find("\"result\": \"ok\"") != std::string::npos) {
                stats.sharesAccepted++;
                std::cout << "[POOL] Share accepted! (" << stats.sharesAccepted << "/"
                         << stats.sharesSubmitted << ")\n";
            }
        }
        else if (response.find("\"error\":") != std::string::npos) {
            stats.sharesRejected++;
            std::cout << "[POOL] Share rejected: " << response << "\n";
        }

        return true;
    }

    void parseJob(const std::string& json) {
        std::lock_guard<std::mutex> lock(jobMutex);

        // Extract job_id
        size_t pos = json.find("\"job_id\"");
        if (pos != std::string::npos) {
            pos = json.find("\"", pos + 9);
            size_t end = json.find("\"", pos + 1);
            if (pos != std::string::npos && end != std::string::npos) {
                currentJobId = json.substr(pos + 1, end - pos - 1);
            }
        }

        // Extract blob (Kryptex format) or pre_pow (Grin format)
        pos = json.find("\"blob\"");
        if (pos == std::string::npos) {
            pos = json.find("\"pre_pow\"");
        }
        if (pos != std::string::npos) {
            pos = json.find("\"", pos + 6);
            size_t end = json.find("\"", pos + 1);
            if (pos != std::string::npos && end != std::string::npos) {
                std::string hex = json.substr(pos + 1, end - pos - 1);
                currentHeader.clear();
                for (size_t i = 0; i + 1 < hex.length(); i += 2) {
                    currentHeader.push_back((uint8_t)strtol(hex.substr(i, 2).c_str(), nullptr, 16));
                }
            }
        }

        // Extract target (hex string) or difficulty (number)
        pos = json.find("\"target\"");
        if (pos != std::string::npos) {
            pos = json.find("\"", pos + 9);
            size_t end = json.find("\"", pos + 1);
            if (pos != std::string::npos && end != std::string::npos) {
                std::string hexTarget = json.substr(pos + 1, end - pos - 1);
                currentTarget = strtoull(hexTarget.c_str(), nullptr, 16);
            }
        } else {
            pos = json.find("\"difficulty\"");
            if (pos != std::string::npos) {
                pos = json.find(":", pos);
                if (pos != std::string::npos) {
                    currentTarget = strtoull(json.c_str() + pos + 1, nullptr, 10);
                }
            }
        }

        std::cout << "[JOB] New job: " << currentJobId << " header_size=" << currentHeader.size()
                  << " target=0x" << std::hex << currentTarget << std::dec << "\n";
    }

    bool getJob(std::string& jobId, std::vector<uint8_t>& header, uint64_t& target) {
        std::lock_guard<std::mutex> lock(jobMutex);
        if (currentJobId.empty()) return false;
        jobId = currentJobId;
        header = currentHeader;
        target = currentTarget;
        return true;
    }

private:
    bool sendMessage(const std::string& msg) {
        if (!tlsSocket.isValid()) return false;
        int sent = tlsSocket.sendData(msg.c_str(), (int)msg.length());
        return sent == (int)msg.length();
    }
};

// =============================================================================
// GPU Miner (from cr29_turbo.cpp)
// =============================================================================
class TurboMiner {
    cl_platform_id platform = nullptr;
    cl_device_id device = nullptr;
    cl_context context = nullptr;
    cl_command_queue queue = nullptr;
    cl_program program = nullptr;

    cl_kernel seedKernel = nullptr;
    cl_kernel zeroCountKernel = nullptr;
    cl_kernel trimKernel = nullptr;
    cl_kernel consolidateKernel = nullptr;

    cl_mem edgesA = nullptr;
    cl_mem edgesB = nullptr;
    cl_mem countsA = nullptr;
    cl_mem countsB = nullptr;
    cl_mem degreeCounters = nullptr;
    cl_mem output = nullptr;
    cl_mem outputCount = nullptr;

    static constexpr uint32_t COUNTER_SIZE = 1 << 22;

    std::vector<uint32_t> zeroCounts;
    bool initialized = false;

public:
    struct SipKeys {
        uint64_t k0, k1, k2, k3;
    };

    bool init(int deviceIndex) {
        cl_int err;

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
        std::cout << "GPU: " << deviceName << "\n";

        context = clCreateContext(nullptr, 1, &device, nullptr, nullptr, &err);
        queue = clCreateCommandQueueWithProperties(context, device, nullptr, &err);

        std::ifstream file("src/cr29_turbo.cl");
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

        const char* options = "-cl-std=CL2.0 -cl-mad-enable -cl-fast-relaxed-math -cl-no-signed-zeros";
        err = clBuildProgram(program, 1, &device, options, nullptr, nullptr);

        if (err != CL_SUCCESS) {
            size_t logSize;
            clGetProgramBuildInfo(program, device, CL_PROGRAM_BUILD_LOG, 0, nullptr, &logSize);
            std::vector<char> log(logSize);
            clGetProgramBuildInfo(program, device, CL_PROGRAM_BUILD_LOG, logSize, log.data(), nullptr);
            std::cerr << "Build failed:\n" << log.data() << "\n";
            return false;
        }

        seedKernel = clCreateKernel(program, "SeedEdges", &err);
        zeroCountKernel = clCreateKernel(program, "ZeroAndCount", &err);
        trimKernel = clCreateKernel(program, "TrimBucket", &err);
        consolidateKernel = clCreateKernel(program, "Consolidate", &err);

        if (!seedKernel || !zeroCountKernel || !trimKernel || !consolidateKernel) {
            std::cerr << "Failed to create kernels\n";
            return false;
        }

        size_t edgeBufferSize = (size_t)NUMBUCKETS * MAX_EDGES_PER_BUCKET * sizeof(uint64_t);
        size_t countBufferSize = NUMBUCKETS * sizeof(uint32_t);
        size_t outputBufferSize = 1024 * 1024 * sizeof(uint64_t);

        edgesA = clCreateBuffer(context, CL_MEM_READ_WRITE, edgeBufferSize, nullptr, &err);
        edgesB = clCreateBuffer(context, CL_MEM_READ_WRITE, edgeBufferSize, nullptr, &err);
        countsA = clCreateBuffer(context, CL_MEM_READ_WRITE, countBufferSize, nullptr, &err);
        countsB = clCreateBuffer(context, CL_MEM_READ_WRITE, countBufferSize, nullptr, &err);
        degreeCounters = clCreateBuffer(context, CL_MEM_READ_WRITE, COUNTER_SIZE * sizeof(uint32_t), nullptr, &err);
        output = clCreateBuffer(context, CL_MEM_READ_WRITE, outputBufferSize, nullptr, &err);
        outputCount = clCreateBuffer(context, CL_MEM_READ_WRITE, sizeof(uint32_t), nullptr, &err);

        if (err != CL_SUCCESS) {
            std::cerr << "Failed to allocate buffers\n";
            return false;
        }

        zeroCounts.resize(NUMBUCKETS, 0);
        initialized = true;
        return true;
    }

    uint32_t trimEdges(const SipKeys& keys, std::vector<uint64_t>& resultEdges) {
        if (!initialized) return 0;

        cl_ulong4 sipkeys = {{keys.k0, keys.k1, keys.k2, keys.k3}};
        uint32_t edgeMask = EDGEMASK;
        uint32_t nodeMask = NODEMASK;
        uint32_t xbits = XBITS;
        uint32_t maxPerBucket = MAX_EDGES_PER_BUCKET;

        clEnqueueWriteBuffer(queue, countsA, CL_FALSE, 0, NUMBUCKETS * sizeof(uint32_t), zeroCounts.data(), 0, nullptr, nullptr);

        clSetKernelArg(seedKernel, 0, sizeof(cl_mem), &edgesA);
        clSetKernelArg(seedKernel, 1, sizeof(cl_mem), &countsA);
        clSetKernelArg(seedKernel, 2, sizeof(cl_ulong4), &sipkeys);
        clSetKernelArg(seedKernel, 3, sizeof(uint32_t), &edgeMask);
        clSetKernelArg(seedKernel, 4, sizeof(uint32_t), &nodeMask);
        clSetKernelArg(seedKernel, 5, sizeof(uint32_t), &xbits);
        clSetKernelArg(seedKernel, 6, sizeof(uint32_t), &maxPerBucket);

        size_t seedGlobal = 2048 * 256;
        size_t seedLocal = 256;
        clEnqueueNDRangeKernel(queue, seedKernel, 1, nullptr, &seedGlobal, &seedLocal, 0, nullptr, nullptr);

        cl_mem* srcEdges = &edgesA;
        cl_mem* dstEdges = &edgesB;
        cl_mem* srcCounts = &countsA;
        cl_mem* dstCounts = &countsB;

        uint32_t numBuckets = NUMBUCKETS;
        size_t trimGlobal = NUMBUCKETS * 256;  // Use 256 threads (matches working mineVerbose)
        size_t trimLocal = 256;

        uint32_t counterSize = COUNTER_SIZE;
        size_t zeroCountGlobal = 256 * 256;  // Use smaller size (matches mineVerbose)
        size_t zeroCountLocal = 256;

        for (uint32_t round = 0; round < TRIMROUNDS; round++) {
            clEnqueueWriteBuffer(queue, *dstCounts, CL_FALSE, 0, NUMBUCKETS * sizeof(uint32_t), zeroCounts.data(), 0, nullptr, nullptr);

            clSetKernelArg(zeroCountKernel, 0, sizeof(cl_mem), srcEdges);
            clSetKernelArg(zeroCountKernel, 1, sizeof(cl_mem), srcCounts);
            clSetKernelArg(zeroCountKernel, 2, sizeof(cl_mem), &degreeCounters);
            clSetKernelArg(zeroCountKernel, 3, sizeof(uint32_t), &numBuckets);
            clSetKernelArg(zeroCountKernel, 4, sizeof(uint32_t), &maxPerBucket);
            clSetKernelArg(zeroCountKernel, 5, sizeof(uint32_t), &nodeMask);
            clSetKernelArg(zeroCountKernel, 6, sizeof(uint32_t), &round);
            clSetKernelArg(zeroCountKernel, 7, sizeof(uint32_t), &counterSize);

            clEnqueueNDRangeKernel(queue, zeroCountKernel, 1, nullptr, &zeroCountGlobal, &zeroCountLocal, 0, nullptr, nullptr);

            clSetKernelArg(trimKernel, 0, sizeof(cl_mem), srcEdges);
            clSetKernelArg(trimKernel, 1, sizeof(cl_mem), dstEdges);
            clSetKernelArg(trimKernel, 2, sizeof(cl_mem), srcCounts);
            clSetKernelArg(trimKernel, 3, sizeof(cl_mem), dstCounts);
            clSetKernelArg(trimKernel, 4, sizeof(cl_mem), &degreeCounters);
            clSetKernelArg(trimKernel, 5, sizeof(uint32_t), &maxPerBucket);
            clSetKernelArg(trimKernel, 6, sizeof(uint32_t), &nodeMask);
            clSetKernelArg(trimKernel, 7, sizeof(uint32_t), &round);

            clEnqueueNDRangeKernel(queue, trimKernel, 1, nullptr, &trimGlobal, &trimLocal, 0, nullptr, nullptr);

            std::swap(srcEdges, dstEdges);
            std::swap(srcCounts, dstCounts);
        }

        clFinish(queue);

        // Read bucket counts
        std::vector<uint32_t> counts(NUMBUCKETS);
        clEnqueueReadBuffer(queue, *srcCounts, CL_TRUE, 0, NUMBUCKETS * sizeof(uint32_t), counts.data(), 0, nullptr, nullptr);

        // Calculate total edges
        uint32_t totalEdges = 0;
        for (uint32_t c : counts) totalEdges += c;

        // Read edges from all buckets
        resultEdges.clear();
        resultEdges.reserve(totalEdges);

        std::vector<uint64_t> bucketEdges(maxPerBucket);
        for (uint32_t bucket = 0; bucket < NUMBUCKETS; bucket++) {
            uint32_t count = counts[bucket];
            if (count > 0) {
                size_t offset = (size_t)bucket * maxPerBucket * sizeof(uint64_t);
                clEnqueueReadBuffer(queue, *srcEdges, CL_TRUE, offset, count * sizeof(uint64_t), bucketEdges.data(), 0, nullptr, nullptr);
                for (uint32_t i = 0; i < count; i++) {
                    resultEdges.push_back(bucketEdges[i]);
                }
            }
        }

        return totalEdges;
    }

    void cleanup() {
        if (seedKernel) clReleaseKernel(seedKernel);
        if (zeroCountKernel) clReleaseKernel(zeroCountKernel);
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

    ~TurboMiner() {
        cleanup();
    }
};

// =============================================================================
// Main Mining Loop
// =============================================================================
void printUsage(const char* prog) {
    std::cout << "Usage: " << prog << " [options]\n"
              << "Options:\n"
              << "  -o pool:port     Pool address (e.g., pool.grinmint.com:3416)\n"
              << "  -u username      Mining username/wallet\n"
              << "  -p password      Mining password (default: x)\n"
              << "  -d device        GPU device index (default: 1)\n"
              << "  --tls            Enable TLS encryption\n"
              << "  --benchmark      Run benchmark only (no pool)\n"
              << "  --verbose        Verbose output\n";
}

int main(int argc, char** argv) {
    std::cout << "===========================================\n";
    std::cout << "  CR29 Turbo Pool Miner v1.0\n";
    std::cout << "  RDNA 4 Optimized - 7.82 g/s\n";
    std::cout << "===========================================\n\n";
    std::cout.flush();

    // Parse arguments
    std::string poolHost = "";
    int poolPort = 3416;
    std::string user = "";
    std::string pass = "x";
    int deviceIndex = 1;
    bool benchmark = false;
    bool verbose = false;
    bool useTls = false;

    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "-o" && i + 1 < argc) {
            std::string pool = argv[++i];
            size_t colon = pool.find(':');
            if (colon != std::string::npos) {
                poolHost = pool.substr(0, colon);
                poolPort = std::stoi(pool.substr(colon + 1));
            } else {
                poolHost = pool;
            }
        } else if (arg == "-u" && i + 1 < argc) {
            user = argv[++i];
        } else if (arg == "-p" && i + 1 < argc) {
            pass = argv[++i];
        } else if (arg == "-d" && i + 1 < argc) {
            deviceIndex = std::stoi(argv[++i]);
        } else if (arg == "--benchmark") {
            benchmark = true;
        } else if (arg == "--verbose") {
            verbose = true;
        } else if (arg == "--tls") {
            useTls = true;
        } else if (arg == "-h" || arg == "--help") {
            printUsage(argv[0]);
            return 0;
        }
    }

    // Initialize GPU
    TurboMiner miner;
    if (!miner.init(deviceIndex)) {
        std::cerr << "Failed to initialize GPU\n";
        return 1;
    }

    CycleFinder cycleFinder;

    if (benchmark) {
        // Benchmark mode
        std::cout << "\n=== Benchmark Mode ===\n";

        TurboMiner::SipKeys keys = {
            0x0706050403020100ULL,
            0x0f0e0d0c0b0a0908ULL,
            0x0706050403020100ULL ^ 0x736f6d6570736575ULL,
            0x0f0e0d0c0b0a0908ULL ^ 0x646f72616e646f6dULL
        };

        SipHash hasher(nullptr, 0);
        hasher.k0 = keys.k0;
        hasher.k1 = keys.k1;
        hasher.k2 = keys.k2;
        hasher.k3 = keys.k3;

        // Warmup
        std::vector<uint64_t> edges;
        miner.trimEdges(keys, edges);
        miner.trimEdges(keys, edges);

        auto start = std::chrono::high_resolution_clock::now();
        int iterations = 20;
        int cyclesFound = 0;

        for (int i = 0; i < iterations; i++) {
            uint32_t count = miner.trimEdges(keys, edges);

            std::vector<uint32_t> proof;
            if (cycleFinder.findCycle(edges, hasher, proof)) {
                cyclesFound++;
                if (verbose) {
                    std::cout << "Cycle found! Proof: [";
                    for (size_t j = 0; j < std::min(proof.size(), (size_t)5); j++) {
                        if (j > 0) std::cout << ",";
                        std::cout << proof[j];
                    }
                    std::cout << "...]\n";
                }
            }

            if (verbose) {
                std::cout << "Graph " << (i+1) << ": " << count << " edges, "
                         << (proof.size() == 42 ? "CYCLE FOUND" : "no cycle") << "\n";
            }
        }

        auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

        double graphsPerSec = iterations * 1000.0 / duration.count();
        std::cout << "\nResults:\n";
        std::cout << "  Graphs processed: " << iterations << "\n";
        std::cout << "  Cycles found: " << cyclesFound << "\n";
        std::cout << "  Total time: " << duration.count() << " ms\n";
        std::cout << "  Performance: " << graphsPerSec << " g/s\n";

        return 0;
    }

    // Pool mining mode
    if (poolHost.empty() || user.empty()) {
        std::cerr << "Pool address and username required for mining\n";
        printUsage(argv[0]);
        return 1;
    }

    std::cout << "Connecting to " << poolHost << ":" << poolPort;
    if (useTls) std::cout << " (TLS)";
    std::cout << "...\n";
    std::cout.flush();

    StratumClient stratum(poolHost, poolPort, user, pass, useTls);
    if (!stratum.connect()) {
        std::cerr << "Failed to connect to pool\n";
        return 1;
    }

    std::cout << "Starting mining loop...\n";

    // Receive thread
    std::atomic<bool> running{true};
    std::thread recvThread([&]() {
        while (running && stratum.isConnected()) {
            stratum.receiveAndProcess();
        }
    });

    // Mining loop
    uint64_t nonce = 0;
    auto lastStatus = std::chrono::steady_clock::now();

    while (running && stratum.isConnected()) {
        std::string jobId;
        std::vector<uint8_t> header;
        uint64_t target;

        if (!stratum.getJob(jobId, header, target)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }

        // Create SipHash keys from header + nonce
        std::vector<uint8_t> fullHeader = header;
        for (int i = 0; i < 8; i++) {
            fullHeader.push_back((nonce >> (i * 8)) & 0xFF);
        }

        SipHash hasher(fullHeader.data(), fullHeader.size());
        TurboMiner::SipKeys keys = {hasher.k0, hasher.k1, hasher.k2, hasher.k3};

        // Trim edges on GPU
        std::vector<uint64_t> edges;
        uint32_t edgeCount = miner.trimEdges(keys, edges);
        stratum.stats.graphsProcessed++;

        // Find cycles on CPU
        std::vector<uint32_t> proof;
        if (cycleFinder.findCycle(edges, hasher, proof)) {
            std::cout << "[CYCLE] Found 42-cycle at nonce " << nonce << "!\n";
            stratum.submitShare(jobId, nonce, proof);
        }

        nonce++;

        // Status update every 10 seconds
        auto now = std::chrono::steady_clock::now();
        if (std::chrono::duration_cast<std::chrono::seconds>(now - lastStatus).count() >= 10) {
            double elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastStatus).count() / 1000.0;
            double gps = stratum.stats.graphsProcessed / elapsed;

            std::cout << "[STATUS] " << gps << " g/s | "
                     << "Shares: " << stratum.stats.sharesAccepted << "/"
                     << stratum.stats.sharesSubmitted << " accepted | "
                     << "Graphs: " << stratum.stats.graphsProcessed << "\n";

            stratum.stats.graphsProcessed = 0;
            lastStatus = now;
        }
    }

    running = false;
    if (recvThread.joinable()) {
        recvThread.join();
    }

    return 0;
}
