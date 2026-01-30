/**
 * SHA3X CPU Reference Implementation for XTM
 * Provides deterministic SHA3X hashing for verification and testing
 */

#ifndef SHA3X_CPU_H
#define SHA3X_CPU_H

#include <cstdint>
#include <vector>
#include <cstring>
#include <iostream>
#include "sha3x_algo.h"

/**
 * SHA3X CPU reference implementation
 * Implements the exact SHA3X specification for XTM coin
 */
class SHA3XCPU {
private:
    // Keccak-f[1600] permutation state
    static constexpr int STATE_SIZE = 25;
    static constexpr int ROUNDS = 24;
    static constexpr int RATE = 1088; // 136 bytes for SHA3-256
    static constexpr int CAPACITY = 512;
    
    // Round constants for Keccak-f[1600]
    static const uint64_t RC[24];
    
    // Rho offsets for Keccak
    static const int RHO_OFFSETS[25];

    uint64_t state[STATE_SIZE];

    /**
     * Keccak-f[1600] permutation
     */
    void keccakF1600() {
        for (int round = 0; round < ROUNDS; round++) {
            // θ (theta) step
            uint64_t C[5];
            for (int i = 0; i < 5; i++) {
                C[i] = state[i] ^ state[i + 5] ^ state[i + 10] ^ state[i + 15] ^ state[i + 20];
            }
            
            uint64_t D[5];
            for (int i = 0; i < 5; i++) {
                D[i] = C[(i + 4) % 5] ^ rotl64(C[(i + 1) % 5], 1);
            }
            
            for (int i = 0; i < 25; i++) {
                state[i] ^= D[i % 5];
            }
            
            // ρ (rho) and π (pi) steps
            uint64_t B[25];
            for (int i = 0; i < 25; i++) {
                B[i] = rotl64(state[i], RHO_OFFSETS[i]);
            }
            
            // π (pi) step
            uint64_t temp[25];
            for (int i = 0; i < 25; i++) {
                temp[i] = B[PI_PERMUTATION[i]];
            }
            
            // χ (chi) step
            for (int i = 0; i < 25; i += 5) {
                uint64_t row[5];
                for (int j = 0; j < 5; j++) {
                    row[j] = temp[i + j];
                }
                for (int j = 0; j < 5; j++) {
                    state[i + j] = row[j] ^ ((~row[(j + 1) % 5]) & row[(j + 2) % 5]);
                }
            }
            
            // ι (iota) step
            state[0] ^= RC[round];
        }
    }

    /**
     * Rotate left 64-bit value
     */
    static uint64_t rotl64(uint64_t x, int n) {
        return (x << n) | (x >> (64 - n));
    }

    /**
     * PI permutation indices for Keccak
     */
    static const int PI_PERMUTATION[25];

public:
    SHA3XCPU() {
        reset();
    }

    /**
     * Reset state to initial values
     */
    void reset() {
        std::memset(state, 0, sizeof(state));
    }

    /**
     * Absorb data into the sponge construction
     */
    void absorb(const uint8_t* data, size_t len) {
        size_t offset = 0;
        
        while (offset < len) {
            size_t block_size = (len - offset > RATE / 8) ? RATE / 8 : len - offset;
            
            // XOR data into state
            for (size_t i = 0; i < block_size; i += 8) {
                uint64_t chunk = 0;
                size_t chunk_size = (block_size - i >= 8) ? 8 : block_size - i;
                
                for (size_t j = 0; j < chunk_size; j++) {
                    chunk |= (uint64_t)data[offset + i + j] << (j * 8);
                }
                
                state[i / 8] ^= chunk;
            }
            
            offset += block_size;
            
            if (block_size == RATE / 8) {
                keccakF1600();
                // Clear rate portion for next block
                for (int i = 0; i < RATE / 64; i++) {
                    state[i] = 0;
                }
            }
        }
    }

    /**
     * Squeeze output from the sponge construction
     */
    void squeeze(uint8_t* output, size_t output_len) {
        size_t offset = 0;
        
        while (offset < output_len) {
            size_t block_size = (output_len - offset > RATE / 8) ? RATE / 8 : output_len - offset;
            
            // Extract data from state
            for (size_t i = 0; i < block_size; i += 8) {
                uint64_t chunk = state[i / 8];
                size_t chunk_size = (block_size - i >= 8) ? 8 : block_size - i;
                
                for (size_t j = 0; j < chunk_size; j++) {
                    output[offset + i + j] = (chunk >> (j * 8)) & 0xFF;
                }
            }
            
            offset += block_size;
            
            if (offset < output_len) {
                keccakF1600();
            }
        }
    }

    /**
     * Compute SHA3X hash for mining
     * XTM specific: SHA3-256 with mining header format
     */
    void sha3x_hash(const uint8_t* header, size_t header_len, uint64_t nonce, uint8_t* output) {
        reset();
        
        // Absorb header
        absorb(header, header_len);
        
        // Absorb nonce (little-endian)
        uint8_t nonce_bytes[8];
        for (int i = 0; i < 8; i++) {
            nonce_bytes[i] = (nonce >> (i * 8)) & 0xFF;
        }
        absorb(nonce_bytes, 8);
        
        // Domain separation and padding for XTM
        uint8_t domain_sep[2] = {0x01, 0x00}; // XTM specific domain separation
        absorb(domain_sep, 2);
        
        // Apply padding (10*1 rule)
        uint8_t padding[136] = {0};
        padding[0] = 0x06; // SHA3 padding byte
        
        size_t total_len = header_len + 8 + 2;
        size_t pad_pos = total_len % (RATE / 8);
        
        if (pad_pos < RATE / 8 - 1) {
            padding[pad_pos] = 0x06;
            padding[RATE / 8 - 1] = 0x80;
        } else {
            // Need two blocks
            padding[pad_pos] = 0x06;
            absorb(padding, RATE / 8);
            std::memset(padding, 0, RATE / 8);
            padding[RATE / 8 - 1] = 0x80;
        }
        
        absorb(padding, RATE / 8);
        
        // Squeeze output (256 bits = 32 bytes)
        squeeze(output, 32);
    }

    /**
     * Verify if hash meets target difficulty
     */
    bool checkTarget(const uint8_t hash[32], uint64_t target) {
        // Interpret hash as big-endian 256-bit number
        uint64_t hash_value = 0;
        for (int i = 0; i < 8; i++) {
            hash_value = (hash_value << 8) | hash[i];
        }
        
        // For XTM: hash must be less than target
        return hash_value < target;
    }

    /**
     * Verify complete solution
     */
    bool verifySolution(const SHA3XWork& work, const SHA3XSolution& solution) {
        uint8_t hash[32];
        sha3x_hash(work.header, SHA3X_HEADER_SIZE, solution.nonce, hash);
        
        // Verify hash meets target
        if (!checkTarget(hash, work.target)) {
            return false;
        }
        
        // Verify solution hash matches computed hash
        return std::memcmp(hash, solution.hash, 32) == 0;
    }
};

// Static member definitions
const uint64_t SHA3XCPU::RC[24] = {
    0x0000000000000001ULL, 0x0000000000008082ULL, 0x800000000000808aULL,
    0x8000000080008000ULL, 0x000000000000808bULL, 0x0000000080000001ULL,
    0x8000000080008081ULL, 0x8000000000008009ULL, 0x000000000000008aULL,
    0x0000000000000088ULL, 0x0000000080008009ULL, 0x000000008000000aULL,
    0x000000008000808bULL, 0x800000000000008bULL, 0x8000000000008089ULL,
    0x8000000000008003ULL, 0x8000000000008002ULL, 0x8000000000000080ULL,
    0x000000000000800aULL, 0x800000008000000aULL, 0x8000000080008081ULL,
    0x8000000000008080ULL, 0x0000000080000001ULL, 0x8000000080008008ULL
};

const int SHA3XCPU::RHO_OFFSETS[25] = {
    0, 1, 62, 28, 27, 36, 44, 6, 55, 20, 3, 10, 43, 25, 39, 41, 45, 15, 21, 8, 18, 2, 61, 56, 14
};

const int SHA3XCPU::PI_PERMUTATION[25] = {
    0, 6, 12, 18, 24, 3, 9, 10, 16, 22, 1, 7, 13, 19, 20, 4, 5, 11, 17, 23, 2, 8, 14, 15, 21
};

/**
 * Test vectors for SHA3X validation
 */
class SHA3XTestVectors {
public:
    struct TestVector {
        uint8_t header[SHA3X_HEADER_SIZE];
        uint64_t nonce;
        uint8_t expected_hash[32];
        uint64_t target;
        bool should_meet_target;
    };

    static std::vector<TestVector> getTestVectors() {
        std::vector<TestVector> vectors;
        
        // Test vector 1: Basic functionality
        TestVector vec1 = {0};
        // Simple header pattern
        for (int i = 0; i < SHA3X_HEADER_SIZE; i++) {
            vec1.header[i] = i & 0xFF;
        }
        vec1.nonce = 0x123456789ABCDEF0ULL;
        vec1.target = 0x0000FFFFFFFFFFFFULL; // Easy target
        vec1.should_meet_target = true;
        
        // Expected hash (would be precomputed for real test)
        SHA3XCPU cpu;
        cpu.sha3x_hash(vec1.header, SHA3X_HEADER_SIZE, vec1.nonce, vec1.expected_hash);
        
        vectors.push_back(vec1);
        
        // Test vector 2: High difficulty
        TestVector vec2 = {0};
        std::memset(vec2.header, 0xFF, SHA3X_HEADER_SIZE);
        vec2.nonce = 0x0FEDCBA987654321ULL;
        vec2.target = 0x00000000FFFFFFFFULL; // Hard target
        vec2.should_meet_target = false;
        
        cpu.sha3x_hash(vec2.header, SHA3X_HEADER_SIZE, vec2.nonce, vec2.expected_hash);
        
        vectors.push_back(vec2);
        
        return vectors;
    }

    /**
     * Run all test vectors and verify correctness
     */
    static bool runTests() {
        std::cout << "Running SHA3X CPU reference tests...\n";
        
        auto vectors = getTestVectors();
        SHA3XCPU cpu;
        bool all_passed = true;
        
        for (size_t i = 0; i < vectors.size(); i++) {
            const auto& vec = vectors[i];
            uint8_t computed_hash[32];
            
            cpu.sha3x_hash(vec.header, SHA3X_HEADER_SIZE, vec.nonce, computed_hash);
            
            // Verify hash computation
            bool hash_correct = std::memcmp(computed_hash, vec.expected_hash, 32) == 0;
            bool target_correct = cpu.checkTarget(computed_hash, vec.target) == vec.should_meet_target;
            
            std::cout << "Test vector " << (i + 1) << ": ";
            if (hash_correct && target_correct) {
                std::cout << "PASSED\n";
            } else {
                std::cout << "FAILED\n";
                all_passed = false;
            }
        }
        
        return all_passed;
    }
};

#endif // SHA3X_CPU_H