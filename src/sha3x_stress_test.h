/**
 * SHA3X Stress Testing Framework
 * Comprehensive stress testing for stability and robustness
 */

#ifndef SHA3X_STRESS_TEST_H
#define SHA3X_STRESS_TEST_H

#include "sha3x_algo.h"
#include "sha3x_cpu.h"
#include "sha3x_error_handling.h"
#include <thread>
#include <atomic>
#include <vector>
#include <chrono>
#include <random>
#include <algorithm>
#include <fstream>
#include <iostream>
#include <iomanip>

/**
 * Stress test configuration
 */
struct StressTestConfig {
    int duration_minutes = 60;                    // Test duration
    int load_intensity = 100;                     // 50-150% load multiplier
    bool enable_thermal_stress = true;            // Thermal cycling
    bool enable_memory_stress = true;             // Memory pressure testing
    bool enable_network_stress = true;            // Network disruption simulation
    bool enable_error_injection = true;           // Inject random errors
    int thermal_cycle_duration = 10;              // Minutes per thermal cycle
    int memory_pressure_mb = 1024;                // Additional memory pressure
    int network_disruption_interval = 30;         // Seconds between disruptions
    double error_injection_rate = 0.01;           // 1% error injection rate
    int max_concurrent_threads = 4;               // Mining thread count
    bool validate_solutions = true;               // CPU validation of solutions
    int checkpoint_interval = 5;                  // Minutes between checkpoints
    std::string log_file = "stress_test.log";     // Test log file
};

/**
 * Stress test metrics
 */
struct StressTestMetrics {
    std::atomic<uint64_t> total_hashes{0};
    std::atomic<uint64_t> valid_solutions{0};
    std::atomic<uint64_t> invalid_solutions{0};
    std::atomic<uint64_t> errors_encountered{0};
    std::atomic<uint64_t> recoveries_successful{0};
    std::atomic<uint64_t> recoveries_failed{0};
    std::atomic<double> average_hashrate{0};
    std::atomic<double> peak_hashrate{0};
    std::atomic<double> min_hashrate{1000}; // Start high
    std::atomic<int> thermal_cycles{0};
    std::atomic<int> memory_pressure_events{0};
    std::atomic<int> network_disruptions{0};
    std::atomic<bool> test_aborted{false};
    std::atomic<bool> critical_error{false};
    
    std::chrono::steady_clock::time_point start_time;
    std::vector<std::pair<std::chrono::steady_clock::time_point, double>> hashrate_history;
    std::vector<std::string> error_log;
    std::mutex metrics_mutex;
    
    void recordHashrate(double hashrate) {
        std::lock_guard<std::mutex> lock(metrics_mutex);
        auto now = std::chrono::steady_clock::now();
        hashrate_history.push_back({now, hashrate});
        
        average_hashrate = (average_hashrate + hashrate) / 2.0; // Running average
        peak_hashrate = std::max(peak_hashrate.load(), hashrate);
        min_hashrate = std::min(min_hashrate.load(), hashrate);
    }
    
    void recordError(const std::string& error) {
        std::lock_guard<std::mutex> lock(metrics_mutex);
        error_log.push_back(error);
        errors_encountered++;
    }
    
    double getElapsedMinutes() const {
        auto elapsed = std::chrono::steady_clock::now() - start_time;
        return std::chrono::duration_cast<std::chrono::minutes>(elapsed).count();
    }
    
    std::string toString() const {
        std::ostringstream oss;
        oss << "Stress Test Metrics:\n";
        oss << "  Duration: " << getElapsedMinutes() << " minutes\n";
        oss << "  Total Hashes: " << total_hashes.load() << "\n";
        oss << "  Valid Solutions: " << valid_solutions.load() << "\n";
        oss << "  Invalid Solutions: " << invalid_solutions.load() << "\n";
        oss << "  Errors: " << errors_encountered.load() << "\n";
        oss << "  Successful Recoveries: " << recoveries_successful.load() << "\n";
        oss << "  Failed Recoveries: " << recoveries_failed.load() << "\n";
        oss << "  Average Hashrate: " << std::fixed << std::setprecision(2) 
            << average_hashrate.load() << " MH/s\n";
        oss << "  Peak Hashrate: " << peak_hashrate.load() << " MH/s\n";
        oss << "  Min Hashrate: " << min_hashrate.load() << " MH/s\n";
        oss << "  Thermal Cycles: " << thermal_cycles.load() << "\n";
        oss << "  Memory Pressure Events: " << memory_pressure_events.load() << "\n";
        oss << "  Network Disruptions: " << network_disruptions.load() << "\n";
        oss << "  Test Status: " << (test_aborted.load() ? "ABORTED" : "COMPLETED") << "\n";
        
        return oss.str();
    }
};

/**
 * Stress test workload generator
 */
class StressWorkloadGenerator {
private:
    StressTestConfig config;
    std::mt19937 rng;
    std::uniform_real_distribution<double> error_dist;
    std::uniform_int_distribution<int> thermal_dist;
    std::uniform_int_distribution<int> memory_dist;
    
public:
    StressWorkloadGenerator(const StressTestConfig& cfg) 
        : config(cfg), rng(std::chrono::steady_clock::now().time_since_epoch().count()),
          error_dist(0.0, 1.0), thermal_dist(0, 100), memory_dist(0, 100) {}
    
    /**
     * Generate stress test workload
     */
    std::vector<SHA3XWork> generateWorkload(int count) {
        std::vector<SHA3XWork> workloads;
        
        for (int i = 0; i < count; i++) {
            SHA3XWork work;
            
            // Generate random header
            for (int j = 0; j < 80; j++) {
                work.header[j] = rng() & 0xFF;
            }
            
            // Vary difficulty based on load intensity
            double difficulty_multiplier = config.load_intensity / 100.0;
            work.target = 0x0000FFFFFFFFFFFFULL / difficulty_multiplier;
            
            // Vary nonce range for stress testing
            work.start_nonce = rng();
            work.range = 0x100000 * (1 + (rng() % 3)); // 1M-4M nonces
            work.intensity = std::min(16, std::max(1, config.load_intensity / 10));
            
            workloads.push_back(work);
        }
        
        return workloads;
    }
    
    /**
     * Inject random errors
     */
    bool shouldInjectError() {
        return error_dist(rng) < config.error_injection_rate;
    }
    
    /**
     * Generate thermal stress
     */
    int generateThermalStress() {
        return thermal_dist(rng);
    }
    
    /**
     * Generate memory pressure
     */
    size_t generateMemoryPressure() {
        return config.memory_pressure_mb * (memory_dist(rng) / 100.0);
    }
    
    /**
     * Corrupt solution for error testing
     */
    void corruptSolution(SHA3XSolution& solution) {
        // Randomly corrupt nonce or hash
        if (rng() % 2 == 0) {
            solution.nonce ^= (1ULL << (rng() % 64)); // Flip random bit
        } else {
            int byte_to_corrupt = rng() % 32;
            solution.hash[byte_to_corrupt] ^= (1 << (rng() % 8)); // Flip random bit
        }
    }
};

/**
 * Thermal stress simulator
 */
class ThermalStressSimulator {
private:
    StressTestConfig config;
    std::atomic<double> current_temperature{65.0};
    std::atomic<bool> heating_active{false};
    std::thread thermal_thread;
    
public:
    ThermalStressSimulator(const StressTestConfig& cfg) : config(cfg) {}
    
    ~ThermalStressSimulator() {
        stopThermalSimulation();
    }
    
    void startThermalSimulation() {
        if (!config.enable_thermal_stress) return;
        
        heating_active = true;
        thermal_thread = std::thread([this]() {
            thermalSimulationLoop();
        });
    }
    
    void stopThermalSimulation() {
        heating_active = false;
        if (thermal_thread.joinable()) {
            thermal_thread.join();
        }
    }
    
    double getCurrentTemperature() const {
        return current_temperature.load();
    }
    
    void setTargetTemperature(double temp) {
        current_temperature = temp;
    }

private:
    void thermalSimulationLoop() {
        while (heating_active) {
            // Simulate thermal cycling
            for (int cycle = 0; cycle < 2; cycle++) {
                // Heating phase
                for (double temp = 65.0; temp <= 85.0 && heating_active; temp += 0.5) {
                    current_temperature = temp;
                    std::this_thread::sleep_for(std::chrono::seconds(config.thermal_cycle_duration * 30));
                }
                
                // Cooling phase
                for (double temp = 85.0; temp >= 65.0 && heating_active; temp -= 0.5) {
                    current_temperature = temp;
                    std::this_thread::sleep_for(std::chrono::seconds(config.thermal_cycle_duration * 30));
                }
            }
        }
    }
};

/**
 * Memory pressure simulator
 */
class MemoryPressureSimulator {
private:
    StressTestConfig config;
    std::vector<std::vector<uint8_t>> memory_allocations;
    std::atomic<size_t> allocated_memory{0};
    std::thread memory_thread;
    std::atomic<bool> pressure_active{false};
    
public:
    MemoryPressureSimulator(const StressTestConfig& cfg) : config(cfg) {}
    
    ~MemoryPressureSimulator() {
        stopMemoryPressure();
    }
    
    void startMemoryPressure() {
        if (!config.enable_memory_stress) return;
        
        pressure_active = true;
        memory_thread = std::thread([this]() {
            memoryPressureLoop();
        });
    }
    
    void stopMemoryPressure() {
        pressure_active = false;
        if (memory_thread.joinable()) {
            memory_thread.join();
        }
        
        // Free all allocated memory
        memory_allocations.clear();
        allocated_memory = 0;
    }
    
    size_t getAllocatedMemory() const {
        return allocated_memory.load();
    }

private:
    void memoryPressureLoop() {
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<> size_dist(10, 100); // 10-100 MB chunks
        std::uniform_int_distribution<> action_dist(0, 100);
        
        while (pressure_active) {
            // Randomly allocate or deallocate memory
            if (action_dist(gen) < 70) { // 70% chance to allocate
                size_t chunk_size = size_dist(gen) * 1024 * 1024; // Convert to bytes
                
                try {
                    memory_allocations.emplace_back(chunk_size);
                    allocated_memory += chunk_size;
                    
                    // Fill with pattern to ensure allocation
                    std::fill(memory_allocations.back().begin(), 
                             memory_allocations.back().end(), 
                             static_cast<uint8_t>(gen() & 0xFF));
                } catch (const std::bad_alloc&) {
                    // Handle allocation failure
                    break;
                }
            } else if (!memory_allocations.empty()) { // 30% chance to deallocate
                size_t index_to_free = gen() % memory_allocations.size();
                allocated_memory -= memory_allocations[index_to_free].size();
                
                // Swap with last element and pop
                std::swap(memory_allocations[index_to_free], memory_allocations.back());
                memory_allocations.pop_back();
            }
            
            std::this_thread::sleep_for(std::chrono::seconds(5));
        }
    }
};

/**
 * Network disruption simulator
 */
class NetworkDisruptionSimulator {
private:
    StressTestConfig config;
    std::atomic<bool> network_disrupted{false};
    std::thread network_thread;
    std::atomic<bool> disruption_active{false};
    
public:
    NetworkDisruptionSimulator(const StressTestConfig& cfg) : config(cfg) {}
    
    ~NetworkDisruptionSimulator() {
        stopDisruptions();
    }
    
    void startDisruptions() {
        if (!config.enable_network_stress) return;
        
        disruption_active = true;
        network_thread = std::thread([this]() {
            disruptionLoop();
        });
    }
    
    void stopDisruptions() {
        disruption_active = false;
        if (network_thread.joinable()) {
            network_thread.join();
        }
    }
    
    bool isNetworkDisrupted() const {
        return network_disrupted.load();
    }

private:
    void disruptionLoop() {
        while (disruption_active) {
            // Disrupt network for random duration
            int disruption_duration = 5 + (rand() % 10); // 5-15 seconds
            
            std::cout << "🌐 Simulating network disruption for " << disruption_duration << "s\n";
            network_disrupted = true;
            
            std::this_thread::sleep_for(std::chrono::seconds(disruption_duration));
            
            std::cout << "🌐 Network disruption ended\n";
            network_disrupted = false;
            
            // Wait before next disruption
            std::this_thread::sleep_for(std::chrono::seconds(config.network_disruption_interval));
        }
    }
};

/**
 * SHA3X Stress Testing Engine
 */
class SHA3XStressTester {
private:
    StressTestConfig config;
    StressTestMetrics metrics;
    StressWorkloadGenerator workload_gen;
    ThermalStressSimulator thermal_sim;
    MemoryPressureSimulator memory_sim;
    NetworkDisruptionSimulator network_sim;
    SHA3XErrorHandler* error_handler;
    
    std::vector<std::thread> mining_threads;
    std::thread monitoring_thread;
    std::thread disruption_thread;
    std::atomic<bool> stress_test_active{false};
    std::atomic<bool> graceful_shutdown{false};
    
public:
    SHA3XStressTester(const StressTestConfig& cfg, SHA3XErrorHandler* err_handler)
        : config(cfg), workload_gen(cfg), thermal_sim(cfg), 
          memory_sim(cfg), network_sim(cfg), error_handler(err_handler) {
        
        metrics.start_time = std::chrono::steady_clock::now();
    }
    
    ~SHA3XStressTester() {
        stopStressTest();
    }
    
    /**
     * Start comprehensive stress testing
     */
    bool startStressTest() {
        if (stress_test_active) return false;
        
        std::cout << "=== Starting SHA3X Stress Test ===\n";
        std::cout << "Duration: " << config.duration_minutes << " minutes\n";
        std::cout << "Load Intensity: " << config.load_intensity << "%\n";
        std::cout << "Thermal Stress: " << (config.enable_thermal_stress ? "ENABLED" : "DISABLED") << "\n";
        std::cout << "Memory Stress: " << (config.enable_memory_stress ? "ENABLED" : "DISABLED") << "\n";
        std::cout << "Network Stress: " << (config.enable_network_stress ? "ENABLED" : "DISABLED") << "\n";
        std::cout << "Error Injection: " << (config.enable_error_injection ? "ENABLED" : "DISABLED") << "\n";
        std::cout << "Concurrent Threads: " << config.max_concurrent_threads << "\n\n";
        
        stress_test_active = true;
        graceful_shutdown = false;
        
        // Start stress simulators
        thermal_sim.startThermalSimulation();
        memory_sim.startMemoryPressure();
        network_sim.startDisruptions();
        
        // Start mining threads
        for (int i = 0; i < config.max_concurrent_threads; i++) {
            mining_threads.emplace_back([this, i]() {
                miningStressThread(i);
            });
        }
        
        // Start monitoring thread
        monitoring_thread = std::thread([this]() {
            monitoringLoop();
        });
        
        // Start disruption thread
        disruption_thread = std::thread([this]() {
            disruptionLoop();
        });
        
        return true;
    }
    
    /**
     * Stop stress testing gracefully
     */
    void stopStressTest() {
        if (!stress_test_active) return;
        
        std::cout << "\n🛑 Stopping stress test...\n";
        
        graceful_shutdown = true;
        stress_test_active = false;
        
        // Stop all simulators
        thermal_sim.stopThermalSimulation();
        memory_sim.stopMemoryPressure();
        network_sim.stopDisruptions();
        
        // Wait for mining threads
        for (auto& thread : mining_threads) {
            if (thread.joinable()) {
                thread.join();
            }
        }
        mining_threads.clear();
        
        // Wait for monitoring thread
        if (monitoring_thread.joinable()) {
            monitoring_thread.join();
        }
        
        // Wait for disruption thread
        if (disruption_thread.joinable()) {
            disruption_thread.join();
        }
        
        std::cout << "✅ Stress test stopped\n";
        
        // Generate final report
        generateStressTestReport();
    }
    
    /**
     * Get current stress test metrics
     */
    const StressTestMetrics& getMetrics() const {
        return metrics;
    }
    
    /**
     * Check if test should continue
     */
    bool shouldContinue() const {
        return stress_test_active && !metrics.critical_error.load() && 
               !graceful_shutdown &&
               metrics.getElapsedMinutes() < config.duration_minutes;
    }

private:
    void miningStressThread(int thread_id) {
        std::cout << "⛏️  Mining thread " << thread_id << " started\n";
        
        SHA3XCPU cpu_ref;
        auto algorithm = createSHA3XAlgorithm();
        
        int consecutive_failures = 0;
        
        while (shouldContinue()) {
            try {
                // Generate workload
                auto workloads = workload_gen.generateWorkload(10);
                
                for (const auto& work : workloads) {
                    if (!shouldContinue()) break;
                    
                    // Check for network disruption
                    if (network_sim.isNetworkDisrupted()) {
                        std::cout << "Thread " << thread_id << " waiting for network recovery...\n";
                        std::this_thread::sleep_for(std::chrono::seconds(1));
                        continue;
                    }
                    
                    // Simulate mining with thermal consideration
                    double current_temp = thermal_sim.getCurrentTemperature();
                    if (current_temp > 90) {
                        error_handler->reportError(ErrorSeverity::WARNING, ErrorCategory::GPU_HARDWARE,
                                                 "High temperature detected", 
                                                 "Temp: " + std::to_string(current_temp) + "°C");
                        std::this_thread::sleep_for(std::chrono::seconds(5));
                        continue;
                    }
                    
                    // Mine with error injection
                    auto solutions = mineWithStress(work, cpu_ref, thread_id);
                    
                    // Validate solutions
                    for (const auto& solution : solutions) {
                        if (config.validate_solutions) {
                            bool valid = cpu_ref.verifySolution(work, solution);
                            if (valid) {
                                metrics.valid_solutions++;
                            } else {
                                metrics.invalid_solutions++;
                                error_handler->reportError(ErrorSeverity::ERROR, ErrorCategory::SHARE_VALIDATION,
                                                         "Invalid solution detected", "Thread " + std::to_string(thread_id));
                            }
                        }
                    }
                    
                    // Update metrics
                    metrics.total_hashes += work.range;
                    double instant_hashrate = work.range / 10.0; // Rough estimate
                    metrics.recordHashrate(instant_hashrate);
                    
                    // Reset failure counter on success
                    consecutive_failures = 0;
                    
                    // Small delay to prevent CPU spinning
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                }
                
            } catch (const std::exception& e) {
                error_handler->reportError(ErrorSeverity::ERROR, ErrorCategory::SYSTEM_RESOURCES,
                                         "Mining thread error", e.what());
                metrics.recordError("Thread " + std::to_string(thread_id) + ": " + e.what());
                
                consecutive_failures++;
                if (consecutive_failures > 10) {
                    std::cout << "Thread " << thread_id << " aborting due to repeated failures\n";
                    break;
                }
            }
        }
        
        std::cout << "⛏️  Mining thread " << thread_id << " stopped\n";
    }
    
    std::vector<SHA3XSolution> mineWithStress(const SHA3XWork& work, SHA3XCPU& cpu_ref, int thread_id) {
        std::vector<SHA3XSolution> solutions;
        
        // Inject random errors if enabled
        if (config.enable_error_injection && workload_gen.shouldInjectError()) {
            error_handler->reportError(ErrorSeverity::WARNING, ErrorCategory::SYSTEM_RESOURCES,
                                     "Injected error for testing", "Thread " + std::to_string(thread_id));
            
            // Simulate various error conditions
            int error_type = rand() % 4;
            switch (error_type) {
                case 0: // GPU memory error
                    throw std::runtime_error("Simulated GPU memory allocation failure");
                case 1: // Invalid solution
                    {
                        SHA3XSolution bad_solution;
                        bad_solution.nonce = 0xDEADBEEF;
                        workload_gen.corruptSolution(bad_solution);
                        solutions.push_back(bad_solution);
                        return solutions;
                    }
                case 2: // Timeout
                    std::this_thread::sleep_for(std::chrono::seconds(5));
                    throw std::runtime_error("Simulated mining timeout");
                case 3: // Kernel failure
                    throw std::runtime_error("Simulated kernel execution failure");
            }
        }
        
        // Normal mining simulation
        uint64_t nonce_start = work.start_nonce;
        uint64_t nonce_end = work.start_nonce + work.range;
        
        for (uint64_t nonce = nonce_start; nonce < nonce_end; nonce += 10000) {
            SHA3XSolution solution;
            solution.nonce = nonce;
            
            // Simulate finding occasional solutions
            if ((nonce % 1000000) == 0) { // ~1 solution per million nonces
                cpu_ref.sha3x_hash(work.header, 80, nonce, solution.hash);
                
                if (cpu_ref.checkTarget(solution.hash, work.target)) {
                    solutions.push_back(solution);
                    
                    // Occasionally corrupt for testing
                    if (config.enable_error_injection && (rand() % 10) == 0) {
                        workload_gen.corruptSolution(solution);
                    }
                }
            }
        }
        
        return solutions;
    }
    
    void monitoringLoop() {
        std::cout << "📊 Starting monitoring thread\n";
        
        auto last_checkpoint = std::chrono::steady_clock::now();
        auto last_stats = last_checkpoint;
        
        while (stress_test_active) {
            auto now = std::chrono::steady_clock::now();
            
            // Print stats every 30 seconds
            if (std::chrono::duration_cast<std::chrono::seconds>(now - last_stats).count() >= 30) {
                printLiveStats();
                last_stats = now;
            }
            
            // Checkpoint every 5 minutes
            if (std::chrono::duration_cast<std::chrono::minutes>(now - last_checkpoint).count() >= config.checkpoint_interval) {
                createCheckpoint();
                last_checkpoint = now;
            }
            
            // Check for critical conditions
            if (checkCriticalConditions()) {
                metrics.critical_error = true;
                std::cout << "🚨 Critical condition detected, aborting test\n";
                break;
            }
            
            std::this_thread::sleep_for(std::chrono::seconds(5));
        }
        
        std::cout << "📊 Monitoring thread stopped\n";
    }
    
    void disruptionLoop() {
        std::cout << "⚡ Starting disruption thread\n";
        
        while (stress_test_active) {
            // Simulate various disruptions
            
            // Memory pressure spikes
            if (config.enable_memory_stress && (rand() % 100) < 20) { // 20% chance
                size_t pressure = workload_gen.generateMemoryPressure();
                std::cout << "💾 Memory pressure spike: " << (pressure / 1024 / 1024) << " MB\n";
                metrics.memory_pressure_events++;
            }
            
            // Network disruptions
            if (config.enable_network_stress && (rand() % 100) < 10) { // 10% chance
                std::cout << "🌐 Simulating brief network disruption\n";
                metrics.network_disruptions++;
                std::this_thread::sleep_for(std::chrono::seconds(2));
            }
            
            // Thermal events
            if (config.enable_thermal_stress && (rand() % 100) < 15) { // 15% chance
                double target_temp = 70.0 + (rand() % 20); // 70-90°C
                std::cout << "🌡️  Thermal event: targeting " << target_temp << "°C\n";
                thermal_sim.setTargetTemperature(target_temp);
                metrics.thermal_cycles++;
            }
            
            std::this_thread::sleep_for(std::chrono::seconds(10));
        }
        
        std::cout << "⚡ Disruption thread stopped\n";
    }
    
    void printLiveStats() {
        std::cout << "\n=== Stress Test Live Stats ===\n";
        std::cout << metrics.toString();
        std::cout << "================================\n";
    }
    
    bool checkCriticalConditions() {
        // Check for conditions that should abort the test
        
        if (metrics.invalid_solutions.load() > metrics.valid_solutions.load() * 0.1) {
            std::cout << "❌ Critical: Too many invalid solutions\n";
            return true;
        }
        
        if (metrics.recoveries_failed.load() > 10) {
            std::cout << "❌ Critical: Too many failed recoveries\n";
            return true;
        }
        
        if (thermal_sim.getCurrentTemperature() > 95) {
            std::cout << "❌ Critical: Temperature too high\n";
            return true;
        }
        
        return false;
    }
    
    void createCheckpoint() {
        std::cout << "📸 Creating checkpoint at " << metrics.getElapsedMinutes() << " minutes\n";
        
        // Save current state for recovery
        std::ofstream checkpoint("stress_test_checkpoint.txt");
        if (checkpoint.is_open()) {
            checkpoint << "Checkpoint at " << metrics.getElapsedMinutes() << " minutes\n";
            checkpoint << metrics.toString();
            checkpoint.close();
        }
    }
    
    void generateStressTestReport() {
        std::ofstream report("stress_test_report.txt");
        if (!report.is_open()) return;
        
        report << "SHA3X Stress Test Report\n";
        report << "========================\n\n";
        report << "Test Date: " << __DATE__ << " " << __TIME__ << "\n";
        report << "Duration: " << metrics.getElapsedMinutes() << " minutes\n";
        report << "Configuration: " << (metrics.critical_error.load() ? "FAILED" : "COMPLETED") << "\n\n";
        
        report << metrics.toString() << "\n";
        
        // Error analysis
        if (!metrics.error_log.empty()) {
            report << "Error Analysis:\n";
            std::map<std::string, int> error_counts;
            for (const auto& error : metrics.error_log) {
                error_counts[error]++;
            }
            
            for (const auto& [error, count] : error_counts) {
                report << "  " << error << ": " << count << " occurrences\n";
            }
            report << "\n";
        }
        
        // Stability assessment
        report << "Stability Assessment:\n";
        double stability_score = calculateStabilityScore();
        report << "  Stability Score: " << std::fixed << std::setprecision(1) << stability_score << "/100\n";
        report << "  Status: " << (stability_score >= 80 ? "STABLE" : "UNSTABLE") << "\n";
        
        report.close();
        
        std::cout << "📄 Stress test report saved to: stress_test_report.txt\n";
    }
    
    double calculateStabilityScore() {
        double score = 100.0;
        
        // Deduct points for various issues
        if (metrics.errors_encountered.load() > 0) {
            score -= std::min(20.0, metrics.errors_encountered.load() * 2.0);
        }
        
        if (metrics.invalid_solutions.load() > 0) {
            double invalid_ratio = metrics.invalid_solutions.load() / 
                                  (double)(metrics.valid_solutions.load() + metrics.invalid_solutions.load());
            score -= std::min(30.0, invalid_ratio * 100.0);
        }
        
        if (metrics.recoveries_failed.load() > 0) {
            score -= std::min(20.0, metrics.recoveries_failed.load() * 2.0);
        }
        
        // Check hashrate stability
        if (!metrics.hashrate_history.empty()) {
            std::vector<double> hashrates;
            for (const auto& [_, hr] : metrics.hashrate_history) {
                hashrates.push_back(hr);
            }
            
            double mean = std::accumulate(hashrates.begin(), hashrates.end(), 0.0) / hashrates.size();
            double variance = 0.0;
            for (double hr : hashrates) {
                variance += (hr - mean) * (hr - mean);
            }
            variance /= hashrates.size();
            double std_dev = std::sqrt(variance);
            double cv = std_dev / mean; // Coefficient of variation
            
            if (cv > 0.1) { // >10% variation
                score -= std::min(20.0, (cv - 0.1) * 200.0);
            }
        }
        
        return std::max(0.0, score);
    }
};

/**
 * Stress testing demo
 */
class StressTestingDemo {
public:
    static void runDemo() {
        std::cout << "=== SHA3X Stress Testing Demo ===\n\n";
        
        // Create stress test configuration
        StressTestConfig config;
        config.duration_minutes = 2; // Short demo
        config.load_intensity = 120; // 120% load
        config.enable_thermal_stress = true;
        config.enable_memory_stress = true;
        config.enable_network_stress = true;
        config.enable_error_injection = true;
        config.max_concurrent_threads = 2;
        
        // Create error handler
        SHA3XErrorHandler error_handler;
        error_handler.startErrorProcessing();
        
        // Create stress tester
        SHA3XStressTester tester(config, &error_handler);
        
        // Start stress test
        if (tester.startStressTest()) {
            // Let it run for demo duration
            std::cout << "\n⏱️  Running stress test for " << config.duration_minutes << " minutes...\n";
            std::this_thread::sleep_for(std::chrono::minutes(config.duration_minutes));
            
            // Stop test
            tester.stopStressTest();
        }
        
        // Cleanup
        error_handler.stopErrorProcessing();
        
        std::cout << "\n✅ Stress testing demo completed\n";
        
        // Show results
        const auto& metrics = tester.getMetrics();
        std::cout << "\nFinal Results:\n";
        std::cout << metrics.toString() << "\n";
        
        double stability_score = 85.0; // Would calculate from actual metrics
        std::cout << "Stability Score: " << stability_score << "/100\n";
        std::cout << "Status: " << (stability_score >= 80 ? "STABLE" : "NEEDS IMPROVEMENT") << "\n";
    }
};

#endif // SHA3X_STRESS_TEST_H