/**
 * SHA3X Test Program
 * Validates CPU reference implementation and basic functionality
 */

#include "sha3x_cpu.h"
#include "sha3x_implementation.h"
#include <iostream>
#include <iomanip>
#include <chrono>

void printHash(const uint8_t* hash, const std::string& label) {
    std::cout << label << ": ";
    for (int i = 0; i < 32; i++) {
        std::cout << std::hex << std::setw(2) << std::setfill('0') << (int)hash[i];
    }
    std::cout << std::dec << "\n";
}

bool testBasicHashing() {
    std::cout << "=== Testing Basic SHA3X Hashing ===\n";
    
    SHA3XCPU cpu;
    uint8_t header[80];
    uint8_t hash[32];
    
    // Test 1: Simple header pattern
    for (int i = 0; i < 80; i++) {
        header[i] = i & 0xFF;
    }
    
    uint64_t nonce = 0x123456789ABCDEF0ULL;
    
    cpu.sha3x_hash(header, 80, nonce, hash);
    printHash(hash, "Hash result");
    
    // Test consistency - same input should produce same output
    uint8_t hash2[32];
    cpu.sha3x_hash(header, 80, nonce, hash2);
    
    bool consistent = (memcmp(hash, hash2, 32) == 0);
    std::cout << "Consistency test: " << (consistent ? "PASSED" : "FAILED") << "\n";
    
    return consistent;
}

bool testTargetChecking() {
    std::cout << "\n=== Testing Target Checking ===\n";
    
    SHA3XCPU cpu;
    uint8_t hash[32];
    
    // Test hash that should meet easy target
    memset(hash, 0x00, 32); // All zeros - very low hash
    uint64_t easy_target = 0x0000FFFFFFFFFFFFULL;
    
    bool meets_easy = cpu.checkTarget(hash, easy_target);
    std::cout << "Easy target test: " << (meets_easy ? "PASSED" : "FAILED") << "\n";
    
    // Test hash that should NOT meet hard target
    memset(hash, 0xFF, 32); // All ones - very high hash
    uint64_t hard_target = 0x00000000FFFFFFFFULL;
    
    bool meets_hard = cpu.checkTarget(hash, hard_target);
    std::cout << "Hard target test: " << (!meets_hard ? "PASSED" : "FAILED") << "\n";
    
    return meets_easy && !meets_hard;
}

bool testAlgorithmImplementation() {
    std::cout << "\n=== Testing Algorithm Implementation ===\n";
    
    auto algo = createSHA3XAlgorithm();
    
    // Test job parsing (simplified)
    std::string mock_job = R"({"id":1,"method":"mining.notify","params":["job123","prevhash","coinbase1","coinbase2",[],"version","nbits","ntime",true]})";
    
    SHA3XWork work;
    bool parsed = algo->parseJob(mock_job, work);
    std::cout << "Job parsing: " << (parsed ? "PASSED" : "FAILED") << "\n";
    
    if (parsed) {
        std::cout << "Header built: ";
        for (int i = 0; i < 16; i++) {
            std::cout << std::hex << std::setw(2) << std::setfill('0') << (int)work.header[i];
        }
        std::cout << std::dec << "...\n";
    }
    
    return parsed;
}

bool testPerformance() {
    std::cout << "\n=== Testing Performance ===\n";
    
    SHA3XCPU cpu;
    uint8_t header[80];
    uint8_t hash[32];
    
    // Initialize header
    for (int i = 0; i < 80; i++) {
        header[i] = rand() & 0xFF;
    }
    
    const int iterations = 10000;
    auto start = std::chrono::high_resolution_clock::now();
    
    for (int i = 0; i < iterations; i++) {
        uint64_t nonce = i;
        cpu.sha3x_hash(header, 80, nonce, hash);
    }
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    
    double hashes_per_sec = (iterations * 1000.0) / duration.count();
    std::cout << "CPU hash rate: " << (hashes_per_sec / 1000.0) << " KH/s\n";
    std::cout << "Time for " << iterations << " hashes: " << duration.count() << " ms\n";
    
    return true;
}

bool testSolutionVerification() {
    std::cout << "\n=== Testing Solution Verification ===\n";
    
    SHA3XCPU cpu;
    SHA3XWork work;
    SHA3XSolution solution;
    
    // Create test work
    for (int i = 0; i < 80; i++) {
        work.header[i] = i & 0xFF;
    }
    work.target = 0x0000FFFFFFFFFFFFULL; // Easy target
    
    // Create solution with known nonce
    solution.nonce = 0x42;
    cpu.sha3x_hash(work.header, 80, solution.nonce, solution.hash);
    
    bool verified = cpu.verifySolution(work, solution);
    std::cout << "Solution verification: " << (verified ? "PASSED" : "FAILED") << "\n";
    
    // Test with wrong hash
    solution.hash[0] ^= 0xFF; // Corrupt the hash
    bool should_fail = !cpu.verifySolution(work, solution);
    std::cout << "Corrupted solution rejection: " << (should_fail ? "PASSED" : "FAILED") << "\n";
    
    return verified && should_fail;
}

int main() {
    std::cout << "SHA3X Implementation Test Suite\n";
    std::cout << "==============================\n\n";
    
    bool all_passed = true;
    
    all_passed &= testBasicHashing();
    all_passed &= testTargetChecking();
    all_passed &= testAlgorithmImplementation();
    all_passed &= testPerformance();
    all_passed &= testSolutionVerification();
    
    std::cout << "\n==============================\n";
    std::cout << "Overall result: " << (all_passed ? "ALL TESTS PASSED" : "SOME TESTS FAILED") << "\n";
    
    return all_passed ? 0 : 1;
}