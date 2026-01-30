/**
 * SHA3X Algorithm Interface for XTM Mining
 * Provides modular algorithm abstraction for different PoW implementations
 */

#ifndef SHA3X_ALGO_H
#define SHA3X_ALGO_H

#include <vector>
#include <cstdint>
#include <string>
#include <memory>

// SHA3X Parameters for XTM
constexpr uint32_t SHA3X_HASH_SIZE = 32;          // 256-bit output
constexpr uint32_t SHA3X_NONCE_SIZE = 8;          // 64-bit nonce
constexpr uint32_t SHA3X_HEADER_SIZE = 80;        // Standard header size
constexpr uint32_t SHA3X_WORKGROUP_SIZE = 256;    // RDNA 4 optimal

/**
 * Work structure passed to GPU kernels
 */
struct SHA3XWork {
    uint8_t header[SHA3X_HEADER_SIZE];    // Block header data
    uint64_t target;                      // Difficulty target
    uint64_t start_nonce;                 // Starting nonce
    uint64_t range;                       // Nonce range to scan
    uint32_t intensity;                   // Work intensity multiplier
};

/**
 * Found solution structure
 */
struct SHA3XSolution {
    uint64_t nonce;                       // Winning nonce
    uint8_t hash[SHA3X_HASH_SIZE];        // Resulting hash
    uint32_t extra_nonce;                 // Additional nonce if needed
};

/**
 * Mining statistics
 */
struct SHA3XStats {
    uint64_t hashes_processed;            // Total hash count
    uint64_t solutions_found;             // Valid solutions found
    uint64_t shares_submitted;            // Shares sent to pool
    uint64_t shares_accepted;             // Shares accepted by pool
    double hashrate;                      // Current hashrate (H/s)
    double elapsed_time;                  // Time elapsed (seconds)
};

/**
 * SHA3X Algorithm Interface
 * Mirrors existing cuckaroo structure for easy integration
 */
class SHA3XAlgorithm {
public:
    virtual ~SHA3XAlgorithm() = default;

    /**
     * Parse stratum job JSON into work structure
     */
    virtual bool parseJob(const std::string& job_json, SHA3XWork& work) = 0;

    /**
     * Build work item from job data
     */
    virtual void buildWork(const SHA3XWork& job, SHA3XWork& work) = 0;

    /**
     * CPU verification of GPU-found solution
     */
    virtual bool verifySolution(const SHA3XWork& work, const SHA3XSolution& solution) = 0;

    /**
     * Check if hash meets target difficulty
     */
    virtual bool checkTarget(const uint8_t hash[SHA3X_HASH_SIZE], uint64_t target) = 0;

    /**
     * Get algorithm name for stratum protocol
     */
    virtual std::string getAlgorithmName() const = 0;

    /**
     * Get required memory size for GPU buffers
     */
    virtual size_t getRequiredMemory() const = 0;
};

/**
 * Factory function to create SHA3X algorithm instance
 */
std::unique_ptr<SHA3XAlgorithm> createSHA3XAlgorithm();

#endif // SHA3X_ALGO_H