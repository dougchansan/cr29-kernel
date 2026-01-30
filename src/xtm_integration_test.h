/**
 * XTM SHA3X Integration Testing
 * Live pool testing with Kryptex network
 * Wallet: 12LfqTi7aQKz9cpxU1AsRW7zNCRkKYdwsxVB1Qx47q3ZGS2DQUpMHDKoAdi2apbaFDdHzrjnDbe4jK1B4DbYo4titQH/9070xt
 * Pool: xtm-c29-us.kryptex.network:8040 (TLS enabled)
 */

#ifndef XTM_INTEGRATION_TEST_H
#define XTM_INTEGRATION_TEST_H

#include "sha3x_algo.h"
#include "sha3x_implementation.h"
#include "sha3x_cpu.h"
#include "sha3x_multi_gpu.h"
#include "sha3x_mining_api.h"
#include "sha3x_error_handling.h"
#include "tls_socket.h"

#include <iostream>
#include <string>
#include <chrono>
#include <thread>
#include <atomic>
#include <mutex>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <vector>

/**
 * XTM Pool Configuration for Kryptex Network
 */
struct XTMPoolConfig {
    std::string pool_host = "xtm-c29-us.kryptex.network";
    int pool_port = 8040;
    bool use_tls = true;
    std::string wallet_address = "12LfqTi7aQKz9cpxU1AsRW7zNCRkKYdwsxVB1Qx47q3ZGS2DQUpMHDKoAdi2apbaFDdHzrjnDbe4jK1B4DbYo4titQH";
    std::string worker_name = "9070xt";
    std::string password = "x";
    std::string algorithm = "sha3x";
    
    std::string toString() const {
        return pool_host + ":" + std::to_string(pool_port) + " (TLS: " + (use_tls ? "yes" : "no") + ")";
    }
};

/**
 * Live mining statistics from real pool
 */
struct LiveMiningStats {
    std::chrono::steady_clock::time_point start_time;
    std::atomic<uint64_t> shares_submitted{0};
    std::atomic<uint64_t> shares_accepted{0};
    std::atomic<uint64_t> shares_rejected{0};
    std::atomic<uint64_t> total_hashes{0};
    std::atomic<double> current_hashrate{0};
    std::atomic<double> average_hashrate{0};
    std::atomic<bool> pool_connected{false};
    std::atomic<bool> authenticated{false};
    std::string pool_difficulty;
    std::string last_job_id;
    std::chrono::steady_clock::time_point last_share_time;
    std::vector<std::string> share_history;
    std::mutex stats_mutex;
    
    std::string toJSON() const {
        std::lock_guard<std::mutex> lock(stats_mutex);
        
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::steady_clock::now() - start_time).count();
        
        std::ostringstream json;
        json << "{\n";
        json << "  \"elapsed_seconds\": " << elapsed << ",\n";
        json << "  \"shares_submitted\": " << shares_submitted.load() << ",\n";
        json << "  \"shares_accepted\": " << shares_accepted.load() << ",\n";
        json << "  \"shares_rejected\": " << shares_rejected.load() << ",\n";
        json << "  \"total_hashes\": " << total_hashes.load() << ",\n";
        json << "  \"current_hashrate\": " << std::fixed << std::setprecision(2) 
             << current_hashrate.load() << ",\n";
        json << "  \"average_hashrate\": " << std::fixed << std::setprecision(2) 
             << average_hashrate.load() << ",\n";
        json << "  \"pool_connected\": " << (pool_connected.load() ? "true" : "false") << ",\n";
        json << "  \"authenticated\": " << (authenticated.load() ? "true" : "false") << ",\n";
        json << "  \"pool_difficulty\": \"" << pool_difficulty << "\",\n";
        json << "  \"last_job_id\": \"" << last_job_id << "\",\n";
        json << "  \"acceptance_rate\": " << std::fixed << std::setprecision(1) 
             << (shares_submitted.load() > 0 ? (shares_accepted.load() * 100.0 / shares_submitted.load()) : 0) << "\n";
        json << "}";
        return json.str();
    }
};

/**
 * XTM Stratum Client for Kryptex Pool
 */
class XTMStratumClient {
private:
    TlsSocket tls_socket;
    XTMPoolConfig config;
    LiveMiningStats* stats;
    SHA3XErrorHandler* error_handler;
    
    std::atomic<bool> connected{false};
    std::atomic<bool> should_reconnect{false};
    std::atomic<int> message_id{1};
    
    std::string current_job_id;
    std::vector<uint8_t> current_header;
    uint64_t current_target;
    std::mutex job_mutex;
    
    std::thread receive_thread;
    std::thread heartbeat_thread;
    
public:
    XTMStratumClient(const XTMPoolConfig& cfg, LiveMiningStats* mining_stats, SHA3XErrorHandler* err_handler)
        : config(cfg), stats(mining_stats), error_handler(err_handler) {}
    
    ~XTMStratumClient() {
        disconnect();
    }
    
    bool connect() {
        std::cout << "🔄 Connecting to XTM pool: " << config.toString() << "\n";
        
        if (!tls_socket.connect(config.pool_host, config.pool_port, config.use_tls)) {
            error_handler->reportError(ErrorSeverity::ERROR, ErrorCategory::CONNECTION,
                                     "Failed to connect to XTM pool",
                                     config.pool_host + ":" + std::to_string(config.pool_port));
            return false;
        }
        
        connected = true;
        stats->pool_connected = true;
        
        std::cout << "✅ Connected to XTM pool\n";
        
        // Start receive thread
        receive_thread = std::thread([this]() {
            receiveLoop();
        });
        
        // Send initial subscription
        if (!sendSubscription()) {
            disconnect();
            return false;
        }
        
        // Send authentication
        if (!sendAuthentication()) {
            disconnect();
            return false;
        }
        
        // Start heartbeat thread
        heartbeat_thread = std::thread([this]() {
            heartbeatLoop();
        });
        
        return true;
    }
    
    void disconnect() {
        connected = false;
        stats->pool_connected = false;
        
        if (receive_thread.joinable()) {
            receive_thread.join();
        }
        
        if (heartbeat_thread.joinable()) {
            heartbeat_thread.join();
        }
        
        tls_socket.close();
        std::cout << "⏹️  Disconnected from XTM pool\n";
    }
    
    bool isConnected() const {
        return connected && tls_socket.isValid();
    }
    
    bool getJob(std::string& job_id, std::vector<uint8_t>& header, uint64_t& target) {
        std::lock_guard<std::mutex> lock(job_mutex);
        
        if (current_job_id.empty()) {
            return false;
        }
        
        job_id = current_job_id;
        header = current_header;
        target = current_target;
        return true;
    }
    
    bool submitShare(const std::string& job_id, uint64_t nonce, const uint8_t* hash) {
        if (!connected) return false;
        
        std::stringstream ss;
        ss << "{\"id\":" << message_id++ << ",\"method\":\"mining.submit\",";
        ss << "\"params\":[\"" << config.wallet_address << "\",\"" << job_id << "\",\"";
        
        // Convert nonce to hex string (big-endian for XTM)
        for (int i = 7; i >= 0; i--) {
            ss << std::hex << std::setw(2) << std::setfill('0') 
               << (int)((nonce >> (i * 8)) & 0xFF);
        }
        
        ss << "\"]}\n";
        
        std::string message = ss.str();
        
        stats->shares_submitted++;
        stats->last_share_time = std::chrono::steady_clock::now();
        
        std::cout << "📤 Submitting share for job " << job_id << " with nonce 0x" 
                 << std::hex << nonce << std::dec << "\n";
        
        return sendMessage(message);
    }

private:
    bool sendSubscription() {
        std::stringstream ss;
        ss << "{\"id\":" << message_id++ << ",\"method\":\"mining.subscribe\",";
        ss << "\"params\":[\"sha3x-miner/1.0\",\"SHA3X\"]}\n";
        
        std::cout << "📨 Sending subscription...\n";
        return sendMessage(ss.str());
    }
    
    bool sendAuthentication() {
        std::stringstream ss;
        ss << "{\"id\":" << message_id++ << ",\"method\":\"mining.authorize\",";
        ss << "\"params\":[\"" << config.wallet_address << "." << config.worker_name << "\",\"" << config.password << "\"]}\n";
        
        std::cout << "🔑 Sending authentication...\n";
        return sendMessage(ss.str());
    }
    
    bool sendMessage(const std::string& message) {
        if (!tls_socket.isValid()) return false;
        
        int sent = tls_socket.sendData(message.c_str(), message.length());
        return sent == (int)message.length();
    }
    
    void receiveLoop() {
        char buffer[4096];
        
        while (connected) {
            int bytes_read = tls_socket.recvData(buffer, sizeof(buffer) - 1);
            
            if (bytes_read <= 0) {
                if (connected) {
                    error_handler->reportError(ErrorSeverity::ERROR, ErrorCategory::CONNECTION,
                                             "Connection lost", "Receive failed");
                    should_reconnect = true;
                }
                break;
            }
            
            buffer[bytes_read] = '\0';
            std::string response(buffer);
            
            processResponse(response);
        }
    }
    
    void processResponse(const std::string& response) {
        std::cout << "📨 Received: " << response.substr(0, std::min(size_t(100), response.length())) 
                 << (response.length() > 100 ? "..." : "") << "\n";
        
        // Parse JSON response (simplified)
        if (response.find("\"method\":\"mining.notify\"") != std::string::npos) {
            parseMiningNotify(response);
        }
        else if (response.find("\"result\":true") != std::string::npos) {
            if (response.find("mining.authorize") != std::string::npos) {
                stats->authenticated = true;
                std::cout << "✅ Authentication successful\n";
            }
            else if (response.find("mining.submit") != std::string::npos) {
                stats->shares_accepted++;
                std::cout << "✅ Share accepted! (" << stats->shares_accepted.load() << "/"
                         << stats->shares_submitted.load() << ")\n";
            }
        }
        else if (response.find("\"error\"") != std::string::npos) {
            if (response.find("mining.submit") != std::string::npos) {
                stats->shares_rejected++;
                std::cout << "❌ Share rejected: " << response << "\n";
                error_handler->reportError(ErrorSeverity::WARNING, ErrorCategory::SHARE_SUBMISSION,
                                         "Share rejected", response);
            }
        }
    }
    
    void parseMiningNotify(const std::string& notify) {
        std::lock_guard<std::mutex> lock(job_mutex);
        
        // Extract job_id
        size_t job_pos = notify.find("\"job_id\"");
        if (job_pos != std::string::npos) {
            size_t quote_start = notify.find("\"", job_pos + 9);
            size_t quote_end = notify.find("\"", quote_start + 1);
            if (quote_start != std::string::npos && quote_end != std::string::npos) {
                current_job_id = notify.substr(quote_start + 1, quote_end - quote_start - 1);
                stats->last_job_id = current_job_id;
            }
        }
        
        // Extract header/blob
        size_t blob_pos = notify.find("\"blob\"");
        if (blob_pos == std::string::npos) {
            blob_pos = notify.find("\"header\"");
        }
        
        if (blob_pos != std::string::npos) {
            size_t quote_start = notify.find("\"", blob_pos + 6);
            size_t quote_end = notify.find("\"", quote_start + 1);
            if (quote_start != std::string::npos && quote_end != std::string::npos) {
                std::string hex_blob = notify.substr(quote_start + 1, quote_end - quote_start - 1);
                current_header.clear();
                for (size_t i = 0; i + 1 < hex_blob.length(); i += 2) {
                    current_header.push_back((uint8_t)strtol(hex_blob.substr(i, 2).c_str(), nullptr, 16));
                }
            }
        }
        
        // Extract target/difficulty
        size_t target_pos = notify.find("\"target\"");
        if (target_pos != std::string::npos) {
            size_t quote_start = notify.find("\"", target_pos + 9);
            size_t quote_end = notify.find("\"", quote_start + 1);
            if (quote_start != std::string::npos && quote_end != std::string::npos) {
                std::string hex_target = notify.substr(quote_start + 1, quote_end - quote_start - 1);
                current_target = strtoull(hex_target.c_str(), nullptr, 16);
                stats->pool_difficulty = hex_target;
            }
        }
        
        std::cout << "📝 New job: " << current_job_id << " (difficulty: 0x" 
                 << std::hex << current_target << std::dec << ")\n";
        
        // Log successful job reception
        error_handler->reportError(ErrorSeverity::INFO, ErrorCategory::POOL_PROTOCOL,
                                 "New job received", "Job ID: " + current_job_id);
    }
    
    void heartbeatLoop() {
        while (connected) {
            // Send keepalive every 60 seconds
            std::this_thread::sleep_for(std::chrono::seconds(60));
            
            if (connected) {
                std::stringstream ss;
                ss << "{\"id\":" << message_id++ << ",\"method\":\"mining.extranonce.subscribe\"}\\n";
                sendMessage(ss.str());
            }
        }
    }
};

/**
 * Integration test for XTM SHA3X mining
 */
class XTMIntegrationTest {
private:
    XTMPoolConfig pool_config;
    LiveMiningStats mining_stats;
    SHA3XErrorHandler error_handler;
    SHA3XMiningAPI mining_api;
    
    std::unique_ptr<SHA3XAlgorithm> algorithm;
    SHA3XCPU cpu_ref;
    
    std::atomic<bool> test_running{false};
    std::thread mining_thread;
    std::thread stats_thread;
    
public:
    XTMIntegrationTest() : mining_api(8080) {
        algorithm = createSHA3XAlgorithm();
        
        // Set up error handler
        error_handler.startErrorProcessing();
        
        // Set up API
        MiningConfig api_config;
        api_config.pool_url = pool_config.pool_host + ":" + std::to_string(pool_config.pool_port);
        api_config.wallet_address = pool_config.wallet_address;
        api_config.worker_name = pool_config.worker_name;
        api_config.api_port = 8080;
        api_config.algorithm = pool_config.algorithm;
        
        mining_api.setConfig(api_config);
        mining_api.startServer();
        
        std::cout << "🚀 XTM Integration Test initialized\n";
        std::cout << "📍 Pool: " << pool_config.toString() << "\n";
        std::cout << "💰 Wallet: " << pool_config.wallet_address.substr(0, 20) << "...\n";
        std::cout << "🖥️  Worker: " << pool_config.worker_name << "\n";
        std::cout << "🌐 API: http://localhost:8080\n";
    }
    
    ~XTMIntegrationTest() {
        stopTest();
        error_handler.stopErrorProcessing();
        mining_api.stopServer();
    }
    
    /**
     * Run complete integration test
     */
    void runIntegrationTest(int test_duration_minutes = 10) {
        std::cout << "\n=== Starting XTM Integration Test ===\n";
        std::cout << "⏱️  Test duration: " << test_duration_minutes << " minutes\n\n";
        
        test_running = true;
        mining_stats.start_time = std::chrono::steady_clock::now();
        
        // Start mining thread
        mining_thread = std::thread([this]() {
            miningLoop();
        });
        
        // Start statistics thread
        stats_thread = std::thread([this]() {
            statsLoop();
        });
        
        // Run for specified duration
        std::this_thread::sleep_for(std::chrono::minutes(test_duration_minutes));
        
        // Stop test
        stopTest();
        
        // Generate test report
        generateTestReport();
    }

private:
    void miningLoop() {
        std::cout << "🔄 Starting mining loop...\n";
        
        // Create stratum client
        XTMStratumClient stratum_client(pool_config, &mining_stats, &error_handler);
        
        if (!stratum_client.connect()) {
            error_handler.reportError(ErrorSeverity::FATAL, ErrorCategory::CONNECTION,
                                    "Failed to connect to pool", "Initial connection failed");
            return;
        }
        
        mining_api.getStats().is_mining = true;
        
        // Mining loop
        uint64_t nonce = 0;
        int consecutive_failures = 0;
        
        while (test_running) {
            try {
                // Get current job from pool
                std::string job_id;
                std::vector<uint8_t> header;
                uint64_t target;
                
                if (!stratum_client.getJob(job_id, header, target)) {
                    std::cout << "⏳ Waiting for job from pool...\n";
                    std::this_thread::sleep_for(std::chrono::seconds(1));
                    continue;
                }
                
                // Create work unit
                SHA3XWork work;
                std::memcpy(work.header, header.data(), std::min(header.size(), (size_t)80));
                work.target = target;
                work.start_nonce = nonce;
                work.range = 0x100000; // 1M nonces per iteration
                work.intensity = 8;
                
                // Mine this work unit
                std::cout << "⛏️  Mining job " << job_id << " with target 0x" 
                         << std::hex << target << std::dec << "\n";
                
                // Simulate GPU mining (would use actual GPU miner)
                auto solutions = simulateGPUMining(work);
                
                // Submit solutions
                for (const auto& solution : solutions) {
                    if (stratum_client.submitShare(job_id, solution.nonce, solution.hash)) {
                        std::cout << "📤 Submitted solution for nonce 0x" << std::hex 
                                 << solution.nonce << std::dec << "\n";
                    }
                }
                
                // Update statistics
                mining_stats.total_hashes += work.range;
                nonce += work.range;
                
                // Reset failure counter on success
                consecutive_failures = 0;
                
            } catch (const std::exception& e) {
                error_handler.reportError(ErrorSeverity::ERROR, ErrorCategory::SYSTEM_RESOURCES,
                                        "Mining loop error", e.what());
                consecutive_failures++;
                
                if (consecutive_failures > 5) {
                    std::cout << "❌ Too many consecutive failures, stopping mining\n";
                    break;
                }
            }
            
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        
        mining_api.getStats().is_mining = false;
        std::cout << "⏹️  Mining loop stopped\n";
    }
    
    std::vector<SHA3XSolution> simulateGPUMining(const SHA3XWork& work) {
        std::vector<SHA3XSolution> solutions;
        
        // Simulate finding solutions (real implementation would use GPU)
        for (uint64_t n = 0; n < work.range; n += 100000) {
            uint64_t test_nonce = work.start_nonce + n;
            
            // CPU verification
            uint8_t hash[32];
            cpu_ref.sha3x_hash(work.header, 80, test_nonce, hash);
            
            if (cpu_ref.checkTarget(hash, work.target)) {
                SHA3XSolution solution;
                solution.nonce = test_nonce;
                std::memcpy(solution.hash, hash, 32);
                solutions.push_back(solution);
                
                std::cout << "🎯 Found solution! Nonce: 0x" << std::hex << test_nonce 
                         << std::dec << "\n";
            }
        }
        
        return solutions;
    }
    
    void statsLoop() {
        std::cout << "📊 Starting statistics monitoring...\n";
        
        auto last_stats_time = std::chrono::steady_clock::now();
        
        while (test_running) {
            auto now = std::chrono::steady_clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - last_stats_time).count();
            
            if (elapsed >= 30) { // Report every 30 seconds
                printLiveStats();
                last_stats_time = now;
            }
            
            // Update API stats
            updateAPIStats();
            
            std::this_thread::sleep_for(std::chrono::seconds(5));
        }
    }
    
    void printLiveStats() {
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::steady_clock::now() - mining_stats.start_time).count();
        
        std::cout << "\n=== Live Mining Stats ===\n";
        std::cout << "⏱️  Runtime: " << (elapsed / 60) << "m " << (elapsed % 60) << "s\n";
        std::cout << "💰 Shares: " << mining_stats.shares_accepted.load() << " accepted, "
                 << mining_stats.shares_rejected.load() << " rejected, "
                 << mining_stats.shares_submitted.load() << " total\n";
        
        if (mining_stats.shares_submitted.load() > 0) {
            double acceptance_rate = (mining_stats.shares_accepted.load() * 100.0) / mining_stats.shares_submitted.load();
            std::cout << "📈 Acceptance rate: " << std::fixed << std::setprecision(1) 
                     << acceptance_rate << "%\n";
        }
        
        std::cout << "🔄 Total hashes: " << mining_stats.total_hashes.load() << "\n";
        
        if (elapsed > 0) {
            double avg_hashrate = (mining_stats.total_hashes.load() / (double)elapsed) / 1e6; // MH/s
            mining_stats.average_hashrate = avg_hashrate;
            std::cout << "⚡ Average hashrate: " << std::fixed << std::setprecision(2) 
                     << avg_hashrate << " MH/s\n";
        }
        
        std::cout << "🌐 Pool connected: " << (mining_stats.pool_connected.load() ? "yes" : "no") << "\n";
        std::cout << "🔑 Authenticated: " << (mining_stats.authenticated.load() ? "yes" : "no") << "\n";
        std::cout << "🎯 Last job: " << mining_stats.last_job_id << "\n";
        std::cout << "📊 Difficulty: " << mining_stats.pool_difficulty << "\n";
    }
    
    void updateAPIStats() {
        auto& api_stats = mining_api.getStats();
        
        api_stats.current_hashrate = mining_stats.current_hashrate.load();
        api_stats.average_hashrate = mining_stats.average_hashrate.load();
        api_stats.total_hashes = mining_stats.total_hashes.load();
        api_stats.total_shares = mining_stats.shares_submitted.load();
        api_stats.accepted_shares = mining_stats.shares_accepted.load();
        api_stats.rejected_shares = mining_stats.shares_rejected.load();
        api_stats.is_mining = true;
        
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::steady_clock::now() - mining_stats.start_time).count();
        api_stats.uptime_seconds = elapsed;
    }
    
    void stopTest() {
        test_running = false;
        
        if (mining_thread.joinable()) {
            mining_thread.join();
        }
        
        if (stats_thread.joinable()) {
            stats_thread.join();
        }
    }
    
    void generateTestReport() {
        std::cout << "\n=== XTM Integration Test Report ===\n";
        
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::steady_clock::now() - mining_stats.start_time).count();
        
        std::cout << "⏱️  Total test duration: " << (elapsed / 60) << "m " << (elapsed % 60) << "s\n";
        std::cout << "💰 Total shares submitted: " << mining_stats.shares_submitted.load() << "\n";
        std::cout << "✅ Shares accepted: " << mining_stats.shares_accepted.load() << "\n";
        std::cout << "❌ Shares rejected: " << mining_stats.shares_rejected.load() << "\n";
        std::cout << "🔄 Total hashes computed: " << mining_stats.total_hashes.load() << "\n";
        
        if (mining_stats.shares_submitted.load() > 0) {
            double acceptance_rate = (mining_stats.shares_accepted.load() * 100.0) / mining_stats.shares_submitted.load();
            std::cout << "📈 Overall acceptance rate: " << std::fixed << std::setprecision(1) 
                     << acceptance_rate << "%\n";
            
            if (acceptance_rate > 90) {
                std::cout << "✅ EXCELLENT: High share acceptance rate\n";
            } else if (acceptance_rate > 80) {
                std::cout << "✅ GOOD: Acceptable share acceptance rate\n";
            } else {
                std::cout << "⚠️  WARNING: Low share acceptance rate\n";
            }
        }
        
        // Save detailed report
        saveDetailedReport();
        
        std::cout << "\n📄 Test completed. Check 'xtm_integration_report.txt' for details.\n";
    }
    
    void saveDetailedReport() {
        std::ofstream report("xtm_integration_report.txt");
        if (!report.is_open()) return;
        
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::steady_clock::now() - mining_stats.start_time).count();
        
        report << "XTM SHA3X Integration Test Report\n";
        report << "=================================\n\n";
        report << "Test Date: " << __DATE__ << " " << __TIME__ << "\n";
        report << "Pool: " << pool_config.toString() << "\n";
        report << "Wallet: " << pool_config.wallet_address.substr(0, 20) << "...\n";
        report << "Worker: " << pool_config.worker_name << "\n\n";
        
        report << "Test Duration: " << (elapsed / 60) << " minutes " << (elapsed % 60) << " seconds\n";
        report << "Total Shares: " << mining_stats.shares_submitted.load() << "\n";
        report << "Accepted Shares: " << mining_stats.shares_accepted.load() << "\n";
        report << "Rejected Shares: " << mining_stats.shares_rejected.load() << "\n";
        report << "Acceptance Rate: " << std::fixed << std::setprecision(1) 
               << (mining_stats.shares_submitted.load() > 0 ? 
                   (mining_stats.shares_accepted.load() * 100.0 / mining_stats.shares_submitted.load()) : 0) 
               << "%\n\n";
        
        report << "Total Hashes: " << mining_stats.total_hashes.load() << "\n";
        report << "Average Hashrate: " << std::fixed << std::setprecision(2) 
               << mining_stats.average_hashrate.load() << " MH/s\n\n";
        
        report << "Final Statistics (JSON):\n";
        report << mining_stats.toJSON() << "\n";
        
        report.close();
    }
};

/**
 * Main integration test function
 */
void runXTMIntegrationTest() {
    std::cout << "========================================\n";
    std::cout << "XTM SHA3X Integration Test\n";
    std::cout << "Pool: xtm-c29-us.kryptex.network:8040\n";
    std::cout << "Wallet: 12LfqTi7aQKz9cpxU1AsRW7zNCRkKYdwsxVB1Qx47q3ZGS2DQUpMHDKoAdi2apbaFDdHzrjnDbe4jK1B4DbYo4titQH\n";
    std::cout << "Worker: 9070xt\n";
    std::cout << "========================================\n\n";
    
    std::cout << "⚠️  IMPORTANT: This will connect to the LIVE Kryptex pool and mine XTM.\n";
    std::cout << "⚠️  Ensure you have the correct wallet address configured.\n";
    std::cout << "⚠️  Press Enter to continue or Ctrl+C to abort...\n";
    
    std::cin.get();
    
    XTMIntegrationTest test;
    test.runIntegrationTest(10); // 10 minute test
}

#endif // XTM_INTEGRATION_TEST_H