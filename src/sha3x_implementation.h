/**
 * SHA3X Algorithm Implementation for XTM
 * Concrete implementation of the SHA3X algorithm interface
 */

#ifndef SHA3X_IMPLEMENTATION_H
#define SHA3X_IMPLEMENTATION_H

#include "sha3x_algo.h"
#include "sha3x_cpu.h"
#include <sstream>
#include <iomanip>
#include <ctime>

/**
 * SHA3X Algorithm Implementation for XTM Mining
 */
class SHA3XImplementation : public SHA3XAlgorithm {
private:
    SHA3XCPU cpu_ref;  // CPU reference for verification
    
    /**
     * Parse hex string to bytes
     */
    std::vector<uint8_t> hexToBytes(const std::string& hex) {
        std::vector<uint8_t> bytes;
        for (size_t i = 0; i + 1 < hex.length(); i += 2) {
            bytes.push_back((uint8_t)strtol(hex.substr(i, 2).c_str(), nullptr, 16));
        }
        return bytes;
    }
    
    /**
     * Convert bytes to hex string
     */
    std::string bytesToHex(const uint8_t* bytes, size_t len) {
        std::stringstream ss;
        for (size_t i = 0; i < len; i++) {
            ss << std::hex << std::setw(2) << std::setfill('0') << (int)bytes[i];
        }
        return ss.str();
    }

public:
    SHA3XImplementation() = default;
    
    /**
     * Parse stratum job JSON into work structure
     * Expected format (XTM specific):
     * {
     *   "id": 1,
     *   "method": "mining.notify",
     *   "params": [
     *     "job_id",
     *     "prevhash",
     *     "coinbase1",
     *     "coinbase2",
     *     ["merkle_branch"],
     *     "version",
     *     "nbits",
     *     "ntime",
     *     "clean_jobs"
     *   ]
     * }
     */
    bool parseJob(const std::string& job_json, SHA3XWork& work) override {
        try {
            // Simple JSON parsing (replace with actual JSON library)
            // For now, extract key fields using string manipulation
            
            std::string job_id;
            std::string prev_hash;
            std::string coinbase1;
            std::string coinbase2;
            std::string version;
            std::string nbits;
            std::string ntime;
            
            // Extract job_id
            size_t pos = job_json.find("\"params\"");
            if (pos == std::string::npos) return false;
            
            pos = job_json.find("\"", pos + 8);
            if (pos == std::string::npos) return false;
            
            size_t end = job_json.find("\"", pos + 1);
            if (end == std::string::npos) return false;
            
            job_id = job_json.substr(pos + 1, end - pos - 1);
            
            // Extract other fields (simplified - would need proper JSON parsing)
            // For now, create a synthetic header for testing
            
            // Build header (80 bytes standard Bitcoin-like format)
            std::memset(work.header, 0, SHA3X_HEADER_SIZE);
            
            // Version (4 bytes)
            uint32_t version_val = 0x20000000; // Default version
            for (int i = 0; i < 4; i++) {
                work.header[i] = (version_val >> (i * 8)) & 0xFF;
            }
            
            // Previous block hash (32 bytes) - use job_id as placeholder
            std::vector<uint8_t> prev_bytes = hexToBytes(job_id);
            if (prev_bytes.size() >= 32) {
                std::memcpy(work.header + 4, prev_bytes.data(), 32);
            }
            
            // Merkle root (32 bytes) - placeholder
            std::memcpy(work.header + 36, prev_bytes.data(), std::min(prev_bytes.size(), (size_t)32));
            
            // Timestamp (4 bytes)
            uint32_t timestamp = (uint32_t)time(nullptr);
            for (int i = 0; i < 4; i++) {
                work.header[68 + i] = (timestamp >> (i * 8)) & 0xFF;
            }
            
            // Target bits (4 bytes)
            uint32_t target_bits = 0x1d00ffff; // Default difficulty
            for (int i = 0; i < 4; i++) {
                work.header[72 + i] = (target_bits >> (i * 8)) & 0xFF;
            }
            
            // Nonce (4 bytes) - will be set by miner
            for (int i = 0; i < 4; i++) {
                work.header[76 + i] = 0;
            }
            
            // Set default target (adjust based on nbits if available)
            work.target = 0x0000FFFFFFFFFFFFULL;
            work.start_nonce = 0;
            work.range = 0xFFFFFFFFFFFFULL; // Full 48-bit nonce space
            work.intensity = 1;
            
            return true;
            
        } catch (const std::exception& e) {
            std::cerr << "Error parsing job JSON: " << e.what() << std::endl;
            return false;
        }
    }
    
    /**
     * Build work item from job data
     */
    void buildWork(const SHA3XWork& job, SHA3XWork& work) override {
        work = job;
        // Additional work building logic can be added here
        // For example, adjusting intensity based on GPU capabilities
    }
    
    /**
     * CPU verification of GPU-found solution
     */
    bool verifySolution(const SHA3XWork& work, const SHA3XSolution& solution) override {
        return cpu_ref.verifySolution(work, solution);
    }
    
    /**
     * Check if hash meets target difficulty
     */
    bool checkTarget(const uint8_t hash[SHA3X_HASH_SIZE], uint64_t target) override {
        return cpu_ref.checkTarget(hash, target);
    }
    
    /**
     * Get algorithm name for stratum protocol
     */
    std::string getAlgorithmName() const override {
        return "sha3x";
    }
    
    /**
     * Get required memory size for GPU buffers
     */
    size_t getRequiredMemory() const override {
        // SHA3X needs much less memory than Cuckaroo
        // Mainly for input/output buffers and some working memory
        return 64 * 1024 * 1024; // 64MB should be sufficient
    }
    
    /**
     * Compute hash using CPU reference (for testing)
     */
    void computeHashCPU(const uint8_t header[SHA3X_HEADER_SIZE], uint64_t nonce, uint8_t output[SHA3X_HASH_SIZE]) {
        cpu_ref.sha3x_hash(header, SHA3X_HEADER_SIZE, nonce, output);
    }
};

/**
 * Factory function implementation
 */
std::unique_ptr<SHA3XAlgorithm> createSHA3XAlgorithm() {
    return std::make_unique<SHA3XImplementation>();
}

#endif // SHA3X_IMPLEMENTATION_H