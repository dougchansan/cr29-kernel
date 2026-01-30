/**
 * SHA3X Pool Miner for XTM
 * Integrates SHA3X algorithm with existing pool infrastructure
 * Based on cr29_pool_miner.cpp structure
 */

#include "tls_socket.h"
#include "sha3x_algo.h"
#include "sha3x_implementation.h"
#include "sha3x_cpu.h"

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

// =============================================================================
// SHA3X GPU Miner - OpenCL implementation
// =============================================================================
class SHA3XGPUMiner {
private:
    cl_platform_id platform = nullptr;
    cl_device_id device = nullptr;
    cl_context context = nullptr;
    cl_command_queue queue = nullptr;
    cl_program program = nullptr;

    // Kernels
    cl_kernel mining_kernel = nullptr;
    cl_kernel enhanced_kernel = nullptr;
    cl_kernel full_hash_kernel = nullptr;

    // Buffers
    cl_mem header_buffer = nullptr;
    cl_mem found_nonces_buffer = nullptr;
    cl_mem found_count_buffer = nullptr;

    bool initialized = false;
    std::string kernel_variant = "enhanced"; // "mining" or "enhanced"

public:
    SHA3XGPUMiner() = default;
    
    ~SHA3XGPUMiner() {
        cleanup();
    }

    bool init(int deviceIndex, const std::string& variant = "enhanced") {
        cl_int err;
        kernel_variant = variant;

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

        // Get GPU devices
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
        std::cout << "GPU: " << deviceName << " (SHA3X mining)\n";

        // Create context and queue
        context = clCreateContext(nullptr, 1, &device, nullptr, nullptr, &err);
        queue = clCreateCommandQueueWithProperties(context, device, nullptr, &err);

        // Load kernel source
        std::string kernel_source;
        if (variant == "enhanced") {
            kernel_source = loadKernelSource("src/sha3x_kernel.cl");
        } else {
            kernel_source = loadKernelSource("src/sha3x_kernel.cl");
        }

        if (kernel_source.empty()) {
            std::cerr << "Failed to load kernel source\n";
            return false;
        }

        // Build program
        const char* src = kernel_source.c_str();
        size_t srcLen = kernel_source.length();

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

        // Create kernels
        mining_kernel = clCreateKernel(program, "sha3x_hash_mining", &err);
        enhanced_kernel = clCreateKernel(program, "sha3x_hash_enhanced", &err);
        full_hash_kernel = clCreateKernel(program, "sha3x_hash_full", &err);

        if (!mining_kernel || !enhanced_kernel || !full_hash_kernel) {
            std::cerr << "Failed to create kernels\n";
            return false;
        }

        // Allocate buffers
        header_buffer = clCreateBuffer(context, CL_MEM_READ_ONLY, 80, nullptr, &err);
        found_nonces_buffer = clCreateBuffer(context, CL_MEM_READ_WRITE, 256 * sizeof(uint64_t), nullptr, &err);
        found_count_buffer = clCreateBuffer(context, CL_MEM_READ_WRITE, sizeof(uint32_t), nullptr, &err);

        if (err != CL_SUCCESS) {
            std::cerr << "Failed to allocate buffers\n";
            return false;
        }

        initialized = true;
        return true;
    }

    /**
     * Mine SHA3X with given work parameters
     */
    std::vector<SHA3XSolution> mine(const SHA3XWork& work, uint64_t& hashes_processed) {
        std::vector<SHA3XSolution> solutions;
        
        if (!initialized) return solutions;

        cl_int err;
        
        // Upload header to GPU
        err = clEnqueueWriteBuffer(queue, header_buffer, CL_TRUE, 0, 80, work.header, 0, nullptr, nullptr);
        
        // Reset found counter
        uint32_t zero = 0;
        err = clEnqueueFillBuffer(queue, found_count_buffer, &zero, sizeof(zero), 0, sizeof(uint32_t), 0, nullptr, nullptr);
        
        // Set kernel arguments
        cl_kernel active_kernel = (kernel_variant == "enhanced") ? enhanced_kernel : mining_kernel;
        
        err = clSetKernelArg(active_kernel, 0, sizeof(cl_mem), &header_buffer);
        err = clSetKernelArg(active_kernel, 1, sizeof(uint64_t), &work.start_nonce);
        err = clSetKernelArg(active_kernel, 2, sizeof(uint64_t), &work.target);
        err = clSetKernelArg(active_kernel, 3, sizeof(cl_mem), &found_nonces_buffer);
        err = clSetKernelArg(active_kernel, 4, sizeof(cl_mem), &found_count_buffer);
        
        if (kernel_variant == "enhanced") {
            // Add shared memory argument
            err = clSetKernelArg(active_kernel, 5, 80, nullptr); // Shared header buffer
        }
        
        // Launch kernel
        size_t global_size = 16384 * 256; // ~4M work items
        size_t local_size = 256;
        
        auto start_time = std::chrono::high_resolution_clock::now();
        
        err = clEnqueueNDRangeKernel(queue, active_kernel, 1, nullptr, &global_size, &local_size, 0, nullptr, nullptr);
        
        if (err != CL_SUCCESS) {
            std::cerr << "Kernel execution failed: " << err << "\n";
            return solutions;
        }
        
        clFinish(queue);
        
        auto end_time = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
        
        // Calculate hashes processed
        hashes_processed = global_size * ((kernel_variant == "enhanced") ? 32 : 1);
        
        // Read found solutions
        uint32_t found_count;
        err = clEnqueueReadBuffer(queue, found_count_buffer, CL_TRUE, 0, sizeof(uint32_t), &found_count, 0, nullptr, nullptr);
        
        if (found_count > 0) {
            std::vector<uint64_t> found_nonces(256);
            err = clEnqueueReadBuffer(queue, found_nonces_buffer, CL_TRUE, 0, found_count * sizeof(uint64_t), found_nonces.data(), 0, nullptr, nullptr);
            
            // Create solutions
            for (uint32_t i = 0; i < found_count; i++) {
                SHA3XSolution solution;
                solution.nonce = found_nonces[i];
                
                // Compute full hash for verification
                cl_mem nonce_buffer = clCreateBuffer(context, CL_MEM_READ_ONLY, sizeof(uint64_t), nullptr, &err);
                cl_mem hash_buffer = clCreateBuffer(context, CL_MEM_WRITE_ONLY, 32, nullptr, &err);
                
                err = clEnqueueWriteBuffer(queue, nonce_buffer, CL_TRUE, 0, sizeof(uint64_t), &found_nonces[i], 0, nullptr, nullptr);
                
                err = clSetKernelArg(full_hash_kernel, 0, sizeof(cl_mem), &header_buffer);
                err = clSetKernelArg(full_hash_kernel, 1, sizeof(uint64_t), &found_nonces[i]);
                err = clSetKernelArg(full_hash_kernel, 2, sizeof(cl_mem), &hash_buffer);
                
                size_t full_size = 1;
                err = clEnqueueNDRangeKernel(queue, full_hash_kernel, 1, nullptr, &full_size, nullptr, 0, nullptr, nullptr);
                
                clEnqueueReadBuffer(queue, hash_buffer, CL_TRUE, 0, 32, solution.hash, 0, nullptr, nullptr);
                
                clReleaseMemObject(nonce_buffer);
                clReleaseMemObject(hash_buffer);
                
                solutions.push_back(solution);
            }
        }
        
        return solutions;
    }

private:
    std::string loadKernelSource(const std::string& filename) {
        std::ifstream file(filename);
        if (!file.is_open()) {
            return "";
        }
        std::stringstream ss;
        ss << file.rdbuf();
        return ss.str();
    }
    
    void cleanup() {
        if (mining_kernel) clReleaseKernel(mining_kernel);
        if (enhanced_kernel) clReleaseKernel(enhanced_kernel);
        if (full_hash_kernel) clReleaseKernel(full_hash_kernel);
        if (program) clReleaseProgram(program);
        if (header_buffer) clReleaseMemObject(header_buffer);
        if (found_nonces_buffer) clReleaseMemObject(found_nonces_buffer);
        if (found_count_buffer) clReleaseMemObject(found_count_buffer);
        if (queue) clReleaseCommandQueue(queue);
        if (context) clReleaseContext(context);
    }
};

// =============================================================================
// Stratum Client (adapted from CR29)
// =============================================================================
class SHA3XStratumClient {
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
    uint64_t currentTarget = 0;
    std::mutex jobMutex;

    int messageId = 1;

public:
    struct Stats {
        std::atomic<uint64_t> sharesSubmitted{0};
        std::atomic<uint64_t> sharesAccepted{0};
        std::atomic<uint64_t> sharesRejected{0};
        std::atomic<uint64_t> hashesProcessed{0};
    } stats;

    SHA3XStratumClient(const std::string& h, int p, const std::string& u, const std::string& pw, bool tls = false)
        : host(h), port(p), user(u), pass(pw), useTls(tls) {}

    ~SHA3XStratumClient() {
        disconnect();
    }

    bool connect() {
        std::cout << "Connecting to " << host << ":" << port;
        if (useTls) std::cout << " (TLS)";
        std::cout << "...\n";

        if (!tlsSocket.connect(host, port, useTls)) {
            std::cerr << "Failed to connect" << (useTls ? " (TLS handshake failed?)" : "") << "\n";
            return false;
        }

        connected = true;
        std::cout << "Connected to " << host << ":" << port << (useTls ? " (TLS)" : "") << "\n";

        return login();
    }

    void disconnect() {
        tlsSocket.close();
        connected = false;
    }

    bool isConnected() const { return connected; }

    bool login() {
        // XTM stratum login format
        std::stringstream ss;
        ss << "{\"id\":" << messageId++ << ",\"jsonrpc\":\"2.0\",\"method\":\"mining.subscribe\","
           << "\"params\":[\"sha3x-miner/1.0\",\"\"]}\n";

        return sendMessage(ss.str());
    }

    bool submitShare(const std::string& jobId, uint64_t nonce, const uint8_t* hash) {
        std::lock_guard<std::mutex> lock(sendMutex);

        std::stringstream ss;
        ss << "{\"id\":" << messageId++ << ",\"jsonrpc\":\"2.0\",\"method\":\"mining.submit\","
           << "\"params\":[\"" << user << "\",\"" << jobId << "\",\"";

        // Convert nonce to hex string
        for (int i = 7; i >= 0; i--) {
            ss << std::hex << std::setw(2) << std::setfill('0') << (int)((nonce >> (i * 8)) & 0xFF);
        }

        ss << "\"]}\n";

        stats.sharesSubmitted++;
        std::cout << "[SHARE] Submitting nonce=0x" << std::hex << nonce << std::dec << "\n";

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

        std::string response(buffer);

        if (response.find("\"method\":\"mining.notify\"") != std::string::npos) {
            parseJob(response);
        }
        else if (response.find("\"result\":true") != std::string::npos) {
            stats.sharesAccepted++;
            std::cout << "[POOL] Share accepted! (" << stats.sharesAccepted << "/"
                     << stats.sharesSubmitted << ")\n";
        }
        else if (response.find("\"error\"") != std::string::npos) {
            stats.sharesRejected++;
            std::cout << "[POOL] Share rejected: " << response << "\n";
        }

        return true;
    }

    void parseJob(const std::string& json) {
        std::lock_guard<std::mutex> lock(jobMutex);

        // Simple JSON parsing for XTM stratum
        size_t pos = json.find("\"params\"");
        if (pos == std::string::npos) return;

        // Extract job_id
        pos = json.find("\"", pos + 8);
        if (pos != std::string::npos) {
            size_t end = json.find("\"", pos + 1);
            if (end != std::string::npos) {
                currentJobId = json.substr(pos + 1, end - pos - 1);
            }
        }

        // Extract header/blob
        pos = json.find("\"", end + 1);
        if (pos != std::string::npos) {
            size_t end = json.find("\"", pos + 1);
            if (end != std::string::npos) {
                std::string hex = json.substr(pos + 1, end - pos - 1);
                currentHeader.clear();
                for (size_t i = 0; i + 1 < hex.length(); i += 2) {
                    currentHeader.push_back((uint8_t)strtol(hex.substr(i, 2).c_str(), nullptr, 16));
                }
            }
        }

        // Extract target
        pos = json.find("\"target\"");
        if (pos != std::string::npos) {
            pos = json.find("\"", pos + 9);
            size_t end = json.find("\"", pos + 1);
            if (pos != std::string::npos && end != std::string::npos) {
                std::string hexTarget = json.substr(pos + 1, end - pos - 1);
                currentTarget = strtoull(hexTarget.c_str(), nullptr, 16);
            }
        }

        std::cout << "[JOB] New job: " << currentJobId << " target=0x" << std::hex << currentTarget << std::dec << "\n";
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
// Main Mining Loop
// =============================================================================
void printUsage(const char* prog) {
    std::cout << "Usage: " << prog << " [options]\n"
              << "Options:\n"
              << "  -o pool:port     Pool address (e.g., pool.xtmcoin.com:3333)\n"
              << "  -u username      Mining username/wallet\n"
              << "  -p password      Mining password (default: x)\n"
              << "  -d device        GPU device index (default: 1)\n"
              << "  --tls            Enable TLS encryption\n"
              << "  --benchmark      Run benchmark only (no pool)\n"
              << "  --verbose        Verbose output\n"
              << "  --variant        Kernel variant: mining or enhanced (default: enhanced)\n";
}

int main(int argc, char** argv) {
    std::cout << "===========================================\n";
    std::cout << "  SHA3X Pool Miner v1.0 for XTM\n";
    std::cout << "  RDNA 4 Optimized - GPU Mining\n";
    std::cout << "===========================================\n\n";

    // Parse arguments
    std::string poolHost = "";
    int poolPort = 3333;
    std::string user = "";
    std::string pass = "x";
    int deviceIndex = 1;
    bool benchmark = false;
    bool verbose = false;
    bool useTls = false;
    std::string variant = "enhanced";

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
        } else if (arg == "--variant" && i + 1 < argc) {
            variant = argv[++i];
        } else if (arg == "-h" || arg == "--help") {
            printUsage(argv[0]);
            return 0;
        }
    }

    // Initialize GPU
    SHA3XGPUMiner gpu_miner;
    if (!gpu_miner.init(deviceIndex, variant)) {
        std::cerr << "Failed to initialize GPU\n";
        return 1;
    }

    // Initialize algorithm
    auto algorithm = createSHA3XAlgorithm();
    SHA3XCPU cpu_ref;

    if (benchmark) {
        // Benchmark mode
        std::cout << "\n=== Benchmark Mode ===\n";

        SHA3XWork work;
        std::memset(work.header, 0xAA, SHA3X_HEADER_SIZE);
        work.target = 0x0000FFFFFFFFFFFFULL;
        work.start_nonce = 0;
        work.range = 0x1000000; // 16M nonces
        work.intensity = 1;

        // Warmup
        uint64_t dummy_hashes;
        gpu_miner.mine(work, dummy_hashes);

        auto start = std::chrono::high_resolution_clock::now();
        int iterations = 10;
        uint64_t total_hashes = 0;
        int solutions_found = 0;

        for (int i = 0; i < iterations; i++) {
            work.start_nonce = i * work.range;
            uint64_t hashes = 0;
            auto solutions = gpu_miner.mine(work, hashes);
            
            total_hashes += hashes;
            solutions_found += solutions.size();
            
            if (verbose && !solutions.empty()) {
                std::cout << "Iteration " << (i+1) << ": Found " << solutions.size() << " solutions\n";
            }
        }

        auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

        double hash_rate = (total_hashes * 1000.0) / duration.count();
        std::cout << "\nResults:\n";
        std::cout << "  Total hashes: " << total_hashes << "\n";
        std::cout << "  Solutions found: " << solutions_found << "\n";
        std::cout << "  Total time: " << duration.count() << " ms\n";
        std::cout << "  Hash rate: " << (hash_rate / 1000000.0) << " MH/s\n";

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

    SHA3XStratumClient stratum(poolHost, poolPort, user, pass, useTls);
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
    double total_hash_rate = 0;
    int status_count = 0;

    while (running && stratum.isConnected()) {
        std::string jobId;
        std::vector<uint8_t> header;
        uint64_t target;

        if (!stratum.getJob(jobId, header, target)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }

        // Prepare work
        SHA3XWork work;
        std::memcpy(work.header, header.data(), std::min(header.size(), (size_t)SHA3X_HEADER_SIZE));
        work.target = target;
        work.start_nonce = nonce;
        work.range = 0x1000000; // 16M nonces per batch
        work.intensity = 1;

        // Mine on GPU
        uint64_t hashes_processed = 0;
        auto solutions = gpu_miner.mine(work, hashes_processed);
        stratum.stats.hashesProcessed += hashes_processed;

        // Verify and submit solutions
        for (const auto& solution : solutions) {
            // CPU verification before submission
            if (algorithm->verifySolution(work, solution)) {
                std::cout << "[SOLUTION] Found valid solution at nonce 0x" << std::hex << solution.nonce << std::dec << "!\n";
                stratum.submitShare(jobId, solution.nonce, solution.hash);
            } else {
                std::cout << "[WARNING] GPU solution failed CPU verification\n";
            }
        }

        nonce += work.range;

        // Status update every 10 seconds
        auto now = std::chrono::steady_clock::now();
        if (std::chrono::duration_cast<std::chrono::seconds>(now - lastStatus).count() >= 10) {
            double elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastStatus).count() / 1000.0;
            double hash_rate = stratum.stats.hashesProcessed / elapsed;
            total_hash_rate += hash_rate;
            status_count++;

            std::cout << "[STATUS] " << (hash_rate / 1000000.0) << " MH/s | "
                     << "Shares: " << stratum.stats.sharesAccepted << "/"
                     << stratum.stats.sharesSubmitted << " accepted | "
                     << "Avg: " << (total_hash_rate / status_count / 1000000.0) << " MH/s\n";

            stratum.stats.hashesProcessed = 0;
            lastStatus = now;
        }
    }

    running = false;
    if (recvThread.joinable()) {
        recvThread.join();
    }

    return 0;
}