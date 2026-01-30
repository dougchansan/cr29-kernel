/**
 * SHA3X Mining Error Handling and Recovery System
 * Comprehensive error detection, handling, and automatic recovery
 */

#ifndef SHA3X_ERROR_HANDLING_H
#define SHA3X_ERROR_HANDLING_H

#include <string>
#include <vector>
#include <map>
#include <mutex>
#include <thread>
#include <atomic>
#include <chrono>
#include <queue>
#include <condition_variable>
#include <iostream>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <algorithm>

/**
 * Error severity levels
 */
enum class ErrorSeverity {
    INFO,        // Informational messages
    WARNING,     // Non-critical issues
    ERROR,       // Critical errors that may affect operation
    FATAL        // Fatal errors that stop mining
};

/**
 * Error categories
 */
enum class ErrorCategory {
    CONNECTION,      // Network/pool connection issues
    AUTHENTICATION,  // Stratum authentication failures
    GPU_HARDWARE,    // GPU hardware problems
    GPU_MEMORY,      // GPU memory issues
    OPENCL_RUNTIME,  // OpenCL runtime errors
    KERNEL_COMPILATION, // Kernel compilation failures
    SHARE_SUBMISSION,   // Share submission problems
    SHARE_VALIDATION,   // Share validation failures
    SYSTEM_RESOURCES,   // System resource issues
    POOL_PROTOCOL,      // Pool protocol violations
    PERFORMANCE_DEGRADATION // Performance issues
};

/**
 * Error information structure
 */
struct MiningError {
    ErrorSeverity severity;
    ErrorCategory category;
    std::string message;
    std::string details;
    std::chrono::system_clock::time_point timestamp;
    int error_code;
    std::string device_info;
    bool recovered;
    int recovery_attempts;
    
    std::string toString() const {
        std::ostringstream oss;
        oss << "[" << formatTimestamp(timestamp) << "] ";
        oss << severityToString(severity) << " - ";
        oss << categoryToString(category) << ": ";
        oss << message;
        if (!details.empty()) {
            oss << " (" << details << ")";
        }
        if (!device_info.empty()) {
            oss << " [Device: " << device_info << "]";
        }
        return oss.str();
    }
    
    std::string toJSON() const {
        std::ostringstream json;
        json << "{\n";
        json << "  \"timestamp\": \"" << formatTimestamp(timestamp) << "\",\n";
        json << "  \"severity\": \"" << severityToString(severity) << "\",\n";
        json << "  \"category\": \"" << categoryToString(category) << "\",\n";
        json << "  \"message\": \"" << escapeJSON(message) << "\",\n";
        json << "  \"details\": \"" << escapeJSON(details) << "\",\n";
        json << "  \"error_code\": " << error_code << ",\n";
        json << "  \"device_info\": \"" << escapeJSON(device_info) << "\",\n";
        json << "  \"recovered\": " << (recovered ? "true" : "false") << "\n";
        json << "}";
        return json.str();
    }
    
public:
    static std::string formatTimestamp(std::chrono::system_clock::time_point tp) {
        auto time_t = std::chrono::system_clock::to_time_t(tp);
        std::stringstream ss;
        ss << std::put_time(std::localtime(&time_t), "%Y-%m-%d %H:%M:%S");
        return ss.str();
    }
    
    static std::string severityToString(ErrorSeverity sev) {
        switch (sev) {
            case ErrorSeverity::INFO: return "INFO";
            case ErrorSeverity::WARNING: return "WARNING";
            case ErrorSeverity::ERROR: return "ERROR";
            case ErrorSeverity::FATAL: return "FATAL";
            default: return "UNKNOWN";
        }
    }
    
    static std::string categoryToString(ErrorCategory cat) {
        switch (cat) {
            case ErrorCategory::CONNECTION: return "CONNECTION";
            case ErrorCategory::AUTHENTICATION: return "AUTHENTICATION";
            case ErrorCategory::GPU_HARDWARE: return "GPU_HARDWARE";
            case ErrorCategory::GPU_MEMORY: return "GPU_MEMORY";
            case ErrorCategory::OPENCL_RUNTIME: return "OPENCL_RUNTIME";
            case ErrorCategory::KERNEL_COMPILATION: return "KERNEL_COMPILATION";
            case ErrorCategory::SHARE_SUBMISSION: return "SHARE_SUBMISSION";
            case ErrorCategory::SHARE_VALIDATION: return "SHARE_VALIDATION";
            case ErrorCategory::SYSTEM_RESOURCES: return "SYSTEM_RESOURCES";
            case ErrorCategory::POOL_PROTOCOL: return "POOL_PROTOCOL";
            case ErrorCategory::PERFORMANCE_DEGRADATION: return "PERFORMANCE_DEGRADATION";
            default: return "UNKNOWN";
        }
    }
    
    static std::string escapeJSON(const std::string& str) {
        std::string escaped;
        for (char c : str) {
            switch (c) {
                case '"': escaped += "\\\""; break;
                case '\\': escaped += "\\\\"; break;
                case '\n': escaped += "\\n"; break;
                case '\r': escaped += "\\r"; break;
                case '\t': escaped += "\\t"; break;
                default: escaped += c;
            }
        }
        return escaped;
    }
    
private:
};

/**
 * Recovery action interface
 */
class RecoveryAction {
public:
    virtual ~RecoveryAction() = default;
    virtual bool execute() = 0;
    virtual std::string getDescription() const = 0;
    virtual int getPriority() const = 0; // Higher priority actions execute first
};

/**
 * Connection recovery actions
 */
class ConnectionRecoveryAction : public RecoveryAction {
private:
    std::string& connection_state;
    int max_retries;
    int retry_delay_ms;
    
public:
    ConnectionRecoveryAction(std::string& state, int retries = 3, int delay = 5000)
        : connection_state(state), max_retries(retries), retry_delay_ms(delay) {}
    
    bool execute() override {
        for (int attempt = 1; attempt <= max_retries; attempt++) {
            std::cout << "Connection recovery attempt " << attempt << "/" << max_retries << "\n";
            
            // Simulate connection recovery
            if (simulateConnectionRecovery()) {
                connection_state = "connected";
                std::cout << "✅ Connection recovered successfully\n";
                return true;
            }
            
            if (attempt < max_retries) {
                std::cout << "⏳ Waiting " << retry_delay_ms << "ms before retry...\n";
                std::this_thread::sleep_for(std::chrono::milliseconds(retry_delay_ms));
            }
        }
        
        std::cout << "❌ Connection recovery failed after " << max_retries << " attempts\n";
        return false;
    }
    
    std::string getDescription() const override {
        return "Reconnect to mining pool with " + std::to_string(max_retries) + " retries";
    }
    
    int getPriority() const override { return 10; }
    
private:
    bool simulateConnectionRecovery() {
        // Simulate 70% success rate
        return (rand() % 100) < 70;
    }
};

/**
 * GPU recovery actions
 */
class GPURecoveryAction : public RecoveryAction {
private:
    int device_index;
    std::string& device_state;
    
public:
    GPURecoveryAction(int device, std::string& state)
        : device_index(device), device_state(state) {}
    
    bool execute() override {
        std::cout << "Attempting GPU " << device_index << " recovery...\n";
        
        // Step 1: Reset GPU state
        if (!resetGPUState()) {
            return false;
        }
        
        // Step 2: Reinitialize OpenCL context
        if (!reinitializeOpenCL()) {
            return false;
        }
        
        // Step 3: Reload and recompile kernels
        if (!reloadKernels()) {
            return false;
        }
        
        device_state = "recovered";
        std::cout << "✅ GPU " << device_index << " recovered successfully\n";
        return true;
    }
    
    std::string getDescription() const override {
        return "Recover GPU " + std::to_string(device_index) + " (reset, reinitialize, reload)";
    }
    
    int getPriority() const override { return 8; }
    
private:
    bool resetGPUState() {
        std::cout << "  Resetting GPU " << device_index << " state...\n";
        // Simulate GPU reset
        std::this_thread::sleep_for(std::chrono::seconds(2));
        return true;
    }
    
    bool reinitializeOpenCL() {
        std::cout << "  Reinitializing OpenCL for GPU " << device_index << "...\n";
        // Simulate OpenCL reinitialization
        std::this_thread::sleep_for(std::chrono::seconds(1));
        return true;
    }
    
    bool reloadKernels() {
        std::cout << "  Reloading kernels for GPU " << device_index << "...\n";
        // Simulate kernel reloading
        std::this_thread::sleep_for(std::chrono::seconds(3));
        return true;
    }
};

/**
 * Performance recovery actions
 */
class PerformanceRecoveryAction : public RecoveryAction {
private:
    double target_hashrate;
    double current_hashrate;
    
public:
    PerformanceRecoveryAction(double target, double current)
        : target_hashrate(target), current_hashrate(current) {}
    
    bool execute() override {
        std::cout << "Performance recovery: target " << target_hashrate 
                 << " MH/s, current " << current_hashrate << " MH/s\n";
        
        // Step 1: Analyze performance degradation
        double degradation_percent = ((target_hashrate - current_hashrate) / target_hashrate) * 100;
        std::cout << "  Performance degradation: " << std::fixed << std::setprecision(1) 
                 << degradation_percent << "%\n";
        
        // Step 2: Apply performance optimizations
        if (degradation_percent > 20) {
            return applyMajorOptimizations();
        } else if (degradation_percent > 10) {
            return applyMinorOptimizations();
        } else {
            std::cout << "  Minor degradation (<10%), monitoring...\n";
            return true;
        }
    }
    
    std::string getDescription() const override {
        return "Recover performance (target: " + std::to_string(target_hashrate) + " MH/s)";
    }
    
    int getPriority() const override { return 5; }
    
private:
    bool applyMajorOptimizations() {
        std::cout << "  Applying major performance optimizations...\n";
        
        // Optimize kernel parameters
        std::cout << "    Optimizing kernel parameters...\n";
        std::this_thread::sleep_for(std::chrono::seconds(2));
        
        // Adjust memory access patterns
        std::cout << "    Adjusting memory access patterns...\n";
        std::this_thread::sleep_for(std::chrono::seconds(1));
        
        // Re-tune work distribution
        std::cout << "    Re-tuning work distribution...\n";
        std::this_thread::sleep_for(std::chrono::seconds(2));
        
        return true;
    }
    
    bool applyMinorOptimizations() {
        std::cout << "  Applying minor performance optimizations...\n";
        
        // Fine-tune kernel launch parameters
        std::cout << "    Fine-tuning kernel launch parameters...\n";
        std::this_thread::sleep_for(std::chrono::seconds(1));
        
        return true;
    }
};

/**
 * Comprehensive error handling and recovery system
 */
class SHA3XErrorHandler {
private:
    std::queue<MiningError> error_queue;
    std::mutex error_mutex;
    std::condition_variable error_cv;
    
    std::thread error_processing_thread;
    std::atomic<bool> processing_active{false};
    
    std::vector<std::unique_ptr<RecoveryAction>> recovery_actions;
    std::map<ErrorCategory, int> error_counts;
    std::map<ErrorCategory, std::chrono::steady_clock::time_point> last_error_time;
    
    std::ofstream error_log_file;
    std::mutex log_mutex;
    
    // Error thresholds for automatic recovery
    static constexpr int MAX_CONNECTION_ERRORS = 5;
    static constexpr int MAX_GPU_ERRORS = 3;
    static constexpr int MAX_SHARE_ERRORS = 10;
    static constexpr auto ERROR_WINDOW = std::chrono::minutes(5);
    
public:
    SHA3XErrorHandler() {
        // Open error log file
        error_log_file.open("sha3x_error_log.txt", std::ios::app);
        if (!error_log_file.is_open()) {
            std::cerr << "Warning: Could not open error log file\n";
        }
    }
    
    ~SHA3XErrorHandler() {
        stopErrorProcessing();
        if (error_log_file.is_open()) {
            error_log_file.close();
        }
    }
    
    /**
     * Start error processing thread
     */
    void startErrorProcessing() {
        if (processing_active) return;
        
        processing_active = true;
        error_processing_thread = std::thread([this]() {
            errorProcessingLoop();
        });
        
        std::cout << "✅ Error handling system started\n";
    }
    
    /**
     * Stop error processing thread
     */
    void stopErrorProcessing() {
        processing_active = false;
        error_cv.notify_all();
        
        if (error_processing_thread.joinable()) {
            error_processing_thread.join();
        }
        
        std::cout << "⏹️  Error handling system stopped\n";
    }
    
    /**
     * Report an error for processing
     */
    void reportError(const MiningError& error) {
        {
            std::lock_guard<std::mutex> lock(error_mutex);
            error_queue.push(error);
            
            // Update error counts
            error_counts[error.category]++;
            last_error_time[error.category] = std::chrono::steady_clock::now();
        }
        
        error_cv.notify_one();
        
        // Log error immediately
        logError(error);
        
        // Print error if severe
        if (error.severity >= ErrorSeverity::ERROR) {
            std::cerr << "❌ " << error.toString() << "\n";
        }
    }
    
    /**
     * Report error with convenience method
     */
    void reportError(ErrorSeverity severity, ErrorCategory category, 
                    const std::string& message, const std::string& details = "",
                    int error_code = 0, const std::string& device_info = "") {
        
        MiningError error;
        error.severity = severity;
        error.category = category;
        error.message = message;
        error.details = details;
        error.timestamp = std::chrono::system_clock::now();
        error.error_code = error_code;
        error.device_info = device_info;
        error.recovered = false;
        error.recovery_attempts = 0;
        
        reportError(error);
    }
    
    /**
     * Get recent errors for API/reporting
     */
    std::vector<MiningError> getRecentErrors(int count = 50) const {
        std::lock_guard<std::mutex> lock(const_cast<std::mutex&>(error_mutex));
        
        std::vector<MiningError> recent_errors;
        std::queue<MiningError> temp_queue = error_queue;
        
        while (!temp_queue.empty() && recent_errors.size() < count) {
            recent_errors.push_back(temp_queue.front());
            temp_queue.pop();
        }
        
        return recent_errors;
    }
    
    /**
     * Get error statistics
     */
    std::map<ErrorCategory, int> getErrorStatistics() const {
        std::lock_guard<std::mutex> lock(const_cast<std::mutex&>(error_mutex));
        return error_counts;
    }
    
    /**
     * Check if automatic recovery should be triggered
     */
    bool shouldTriggerRecovery(ErrorCategory category) const {
        auto now = std::chrono::steady_clock::now();
        auto it = last_error_time.find(category);
        
        if (it == last_error_time.end()) {
            return false;
        }
        
        // Check if errors are recent
        if (now - it->second > ERROR_WINDOW) {
            return false;
        }
        
        // Check error count against thresholds
        int error_count = error_counts.at(category);
        
        switch (category) {
            case ErrorCategory::CONNECTION:
                return error_count >= MAX_CONNECTION_ERRORS;
            case ErrorCategory::GPU_HARDWARE:
            case ErrorCategory::GPU_MEMORY:
            case ErrorCategory::OPENCL_RUNTIME:
                return error_count >= MAX_GPU_ERRORS;
            case ErrorCategory::SHARE_SUBMISSION:
            case ErrorCategory::SHARE_VALIDATION:
                return error_count >= MAX_SHARE_ERRORS;
            default:
                return false;
        }
    }
    
    /**
     * Reset error count for category
     */
    void resetErrorCount(ErrorCategory category) {
        std::lock_guard<std::mutex> lock(error_mutex);
        error_counts[category] = 0;
    }

private:
    void errorProcessingLoop() {
        while (processing_active) {
            std::unique_lock<std::mutex> lock(error_mutex);
            error_cv.wait(lock, [this] { return !error_queue.empty() || !processing_active; });
            
            if (!processing_active) break;
            
            while (!error_queue.empty()) {
                MiningError error = error_queue.front();
                error_queue.pop();
                lock.unlock();
                
                // Process error and determine recovery actions
                processError(error);
                
                lock.lock();
            }
        }
    }
    
    void processError(const MiningError& error) {
        // Determine if recovery is needed
        if (shouldTriggerRecovery(error.category)) {
            std::cout << "🔄 Triggering automatic recovery for " 
                     << error.categoryToString(error.category) << " errors\n";
            
            executeRecoveryActions(error.category);
            resetErrorCount(error.category);
        }
        
        // Generate recovery actions based on error
        generateRecoveryActions(error);
    }
    
    void executeRecoveryActions(ErrorCategory category) {
        std::vector<std::unique_ptr<RecoveryAction>> actions;
        
        // Create appropriate recovery actions based on category
        switch (category) {
            case ErrorCategory::CONNECTION:
                {
                    std::string conn_state = "connection_state";
                    actions.push_back(std::make_unique<ConnectionRecoveryAction>(conn_state));
                }
                break;
                
            case ErrorCategory::GPU_HARDWARE:
            case ErrorCategory::GPU_MEMORY:
            case ErrorCategory::OPENCL_RUNTIME:
                // Add GPU recovery for each device
                {
                    std::string gpu_state = "gpu_state";
                    actions.push_back(std::make_unique<GPURecoveryAction>(0, gpu_state));
                }
                break;
                
            case ErrorCategory::PERFORMANCE_DEGRADATION:
                actions.push_back(std::make_unique<PerformanceRecoveryAction>(50.0, 35.0));
                break;
                
            default:
                std::cout << "No specific recovery actions for category: " 
                         << MiningError::categoryToString(category) << "\n";
                return;
        }
        
        // Sort by priority (higher priority first)
        std::sort(actions.begin(), actions.end(), 
                 [](const auto& a, const auto& b) {
                     return a->getPriority() > b->getPriority();
                 });
        
        // Execute recovery actions
        for (const auto& action : actions) {
            std::cout << "Executing recovery: " << action->getDescription() << "\n";
            
            if (action->execute()) {
                std::cout << "✅ Recovery action completed successfully\n";
            } else {
                std::cout << "❌ Recovery action failed\n";
                // Continue with next action even if this one failed
            }
        }
    }
    
    void generateRecoveryActions(const MiningError& error) {
        // Add specific recovery actions to the queue based on error details
        // This is where you'd implement custom recovery logic
        
        if (error.category == ErrorCategory::PERFORMANCE_DEGRADATION) {
            // Parse performance metrics from error details and create recovery action
            double current_hashrate = 35.0; // Would parse from error.details
            double target_hashrate = 50.0;  // Would get from config/history
            
            recovery_actions.push_back(std::make_unique<PerformanceRecoveryAction>(
                target_hashrate, current_hashrate));
        }
    }
    
    void logError(const MiningError& error) {
        std::lock_guard<std::mutex> lock(log_mutex);
        
        if (error_log_file.is_open()) {
            error_log_file << error.toString() << "\n";
            error_log_file.flush();
        }
        
        // Also log to console for severe errors
        if (error.severity >= ErrorSeverity::ERROR) {
            std::cerr << "📝 Logged error: " << error.toString() << "\n";
        }
    }
};

/**
 * Health monitoring system
 */
class MiningHealthMonitor {
private:
    SHA3XErrorHandler* error_handler;
    std::thread health_thread;
    std::atomic<bool> monitoring_active{false};
    
    // Health thresholds
    static constexpr double MAX_GPU_TEMPERATURE = 85.0;
    static constexpr double MAX_POWER_CONSUMPTION = 300.0;
    static constexpr double MIN_ACCEPTABLE_HASHRATE = 30.0; // 70% of target
    static constexpr int MAX_STALE_SHARE_PERCENTAGE = 5;
    
public:
    MiningHealthMonitor(SHA3XErrorHandler* handler) : error_handler(handler) {}
    
    ~MiningHealthMonitor() {
        stopMonitoring();
    }
    
    void startMonitoring() {
        if (monitoring_active) return;
        
        monitoring_active = true;
        health_thread = std::thread([this]() {
            healthMonitoringLoop();
        });
        
        std::cout << "✅ Health monitoring started\n";
    }
    
    void stopMonitoring() {
        monitoring_active = false;
        
        if (health_thread.joinable()) {
            health_thread.join();
        }
        
        std::cout << "⏹️  Health monitoring stopped\n";
    }
    
    /**
     * Report health metrics for monitoring
     */
    void reportHealthMetrics(double gpu_temperature, double power_consumption, 
                           double current_hashrate, double target_hashrate,
                           int accepted_shares, int total_shares) {
        
        // Check GPU temperature
        if (gpu_temperature > MAX_GPU_TEMPERATURE) {
            error_handler->reportError(ErrorSeverity::WARNING, ErrorCategory::GPU_HARDWARE,
                                     "GPU temperature critical", 
                                     "Temperature: " + std::to_string(gpu_temperature) + "°C");
        }
        
        // Check power consumption
        if (power_consumption > MAX_POWER_CONSUMPTION) {
            error_handler->reportError(ErrorSeverity::WARNING, ErrorCategory::SYSTEM_RESOURCES,
                                     "Power consumption too high",
                                     "Power: " + std::to_string(power_consumption) + "W");
        }
        
        // Check hashrate performance
        if (current_hashrate < target_hashrate * (MIN_ACCEPTABLE_HASHRATE / 100.0)) {
            error_handler->reportError(ErrorSeverity::ERROR, ErrorCategory::PERFORMANCE_DEGRADATION,
                                     "Hashrate below acceptable threshold",
                                     "Current: " + std::to_string(current_hashrate) + 
                                     " MH/s, Target: " + std::to_string(target_hashrate) + " MH/s");
        }
        
        // Check share acceptance rate
        if (total_shares > 0) {
            double stale_percentage = ((total_shares - accepted_shares) / (double)total_shares) * 100;
            if (stale_percentage > MAX_STALE_SHARE_PERCENTAGE) {
                error_handler->reportError(ErrorSeverity::WARNING, ErrorCategory::SHARE_SUBMISSION,
                                         "High stale share percentage",
                                         "Stale: " + std::to_string(stale_percentage) + "%");
            }
        }
    }

private:
    void healthMonitoringLoop() {
        while (monitoring_active) {
            // Perform health checks every 30 seconds
            performHealthChecks();
            
            std::this_thread::sleep_for(std::chrono::seconds(30));
        }
    }
    
    void performHealthChecks() {
        // Check system resources
        checkSystemResources();
        
        // Check GPU health (would integrate with AMD ADL/NVML)
        checkGPUHealth();
        
        // Check memory usage
        checkMemoryUsage();
        
        // Check network connectivity
        checkNetworkConnectivity();
    }
    
    void checkSystemResources() {
        // Check available system memory
        // Check CPU usage
        // Check disk space
        
        // Simulate resource check
        if (rand() % 100 < 2) { // 2% chance of resource issue
            error_handler->reportError(ErrorSeverity::WARNING, ErrorCategory::SYSTEM_RESOURCES,
                                     "System resources low", "Memory usage > 90%");
        }
    }
    
    void checkGPUHealth() {
        // Check GPU temperature, fan speed, power
        // Would integrate with actual GPU monitoring APIs
        
        double temperature = 65 + (rand() % 20); // Simulate 65-85°C
        double power = 150 + (rand() % 100);     // Simulate 150-250W
        
        // Simulate health metrics reporting
        if (temperature > 80) {
            error_handler->reportError(ErrorSeverity::WARNING, ErrorCategory::GPU_HARDWARE,
                                     "GPU temperature high", 
                                     "Temperature: " + std::to_string(temperature) + "°C");
        }
        
        if (power > 200) {
            error_handler->reportError(ErrorSeverity::WARNING, ErrorCategory::SYSTEM_RESOURCES,
                                     "High power consumption",
                                     "Power: " + std::to_string(power) + "W");
        }
    }
    
    void checkMemoryUsage() {
        // Check for memory leaks or excessive usage
        if (rand() % 1000 < 1) { // 0.1% chance
            error_handler->reportError(ErrorSeverity::ERROR, ErrorCategory::GPU_MEMORY,
                                     "Potential memory leak detected", 
                                     "Memory usage growing over time");
        }
    }
    
    void checkNetworkConnectivity() {
        // Check pool connectivity
        if (rand() % 100 < 1) { // 1% chance
            error_handler->reportError(ErrorSeverity::WARNING, ErrorCategory::CONNECTION,
                                     "Intermittent network connectivity", 
                                     "Packet loss detected");
        }
    }
};

/**
 * Complete error handling and recovery demo
 */
class SHA3XErrorHandlingDemo {
public:
    static void runDemo() {
        std::cout << "=== SHA3X Error Handling and Recovery Demo ===\n\n";
        
        // Create error handler
        SHA3XErrorHandler error_handler;
        error_handler.startErrorProcessing();
        
        // Create health monitor
        MiningHealthMonitor health_monitor(&error_handler);
        health_monitor.startMonitoring();
        
        // Simulate various error scenarios
        simulateErrorScenarios(error_handler);
        
        // Let recovery system work
        std::cout << "\n⏳ Letting recovery system work...\n";
        std::this_thread::sleep_for(std::chrono::seconds(10));
        
        // Show error statistics
        showErrorStatistics(error_handler);
        
        // Cleanup
        health_monitor.stopMonitoring();
        error_handler.stopErrorProcessing();
        
        std::cout << "\n✅ Error handling demo completed\n";
    }

private:
    static void simulateErrorScenarios(SHA3XErrorHandler& handler) {
        std::cout << "🧪 Simulating error scenarios...\n\n";
        
        // Scenario 1: Connection issues
        std::cout << "Scenario 1: Connection issues\n";
        handler.reportError(ErrorSeverity::WARNING, ErrorCategory::CONNECTION,
                          "Connection timeout", "Pool not responding for 30 seconds");
        
        std::this_thread::sleep_for(std::chrono::seconds(1));
        
        handler.reportError(ErrorSeverity::ERROR, ErrorCategory::CONNECTION,
                          "Connection lost", "Socket error: Connection reset by peer");
        
        std::this_thread::sleep_for(std::chrono::seconds(1));
        
        // Scenario 2: GPU issues
        std::cout << "\nScenario 2: GPU hardware issues\n";
        handler.reportError(ErrorSeverity::ERROR, ErrorCategory::GPU_HARDWARE,
                          "GPU memory error", "CL_OUT_OF_RESOURCES on device 0");
        
        std::this_thread::sleep_for(std::chrono::seconds(1));
        
        handler.reportError(ErrorSeverity::WARNING, ErrorCategory::GPU_MEMORY,
                          "High memory usage", "GPU memory usage > 90%");
        
        std::this_thread::sleep_for(std::chrono::seconds(1));
        
        // Scenario 3: Performance degradation
        std::cout << "\nScenario 3: Performance degradation\n";
        handler.reportError(ErrorSeverity::WARNING, ErrorCategory::PERFORMANCE_DEGRADATION,
                          "Hashrate dropping", "Current: 35 MH/s, Target: 50 MH/s");
        
        std::this_thread::sleep_for(std::chrono::seconds(1));
        
        // Scenario 4: Share validation issues
        std::cout << "\nScenario 4: Share validation issues\n";
        handler.reportError(ErrorSeverity::ERROR, ErrorCategory::SHARE_VALIDATION,
                          "Invalid share", "Share does not meet target difficulty");
        
        std::this_thread::sleep_for(std::chrono::seconds(1));
        
        handler.reportError(ErrorSeverity::WARNING, ErrorCategory::SHARE_SUBMISSION,
                          "High stale share rate", "Stale shares: 8%");
    }
    
    static void showErrorStatistics(SHA3XErrorHandler& handler) {
        std::cout << "\n📊 Error Statistics:\n";
        
        auto stats = handler.getErrorStatistics();
        for (const auto& [category, count] : stats) {
            std::cout << "  " << MiningError::categoryToString(category) << ": " << count << " errors\n";
        }
        
        auto recent_errors = handler.getRecentErrors(10);
        if (!recent_errors.empty()) {
            std::cout << "\n📋 Recent Errors:\n";
            for (const auto& error : recent_errors) {
                std::cout << "  " << error.toString() << "\n";
            }
        }
    }
};

#endif // SHA3X_ERROR_HANDLING_H