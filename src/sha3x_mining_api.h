/**
 * SHA3X Mining API and Control Interface
 * HTTP REST API for monitoring and controlling the miner
 */

#ifndef SHA3X_MINING_API_H
#define SHA3X_MINING_API_H

#include <string>
#include <vector>
#include <map>
#include <mutex>
#include <thread>
#include <atomic>
#include <chrono>
#include <sstream>
#include <iostream>
#include <iomanip>
#include <functional>
#include <ctime>

// Simple HTTP server components (placeholder for actual HTTP library)
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#endif

/**
 * Mining statistics for API reporting
 */
struct MiningStats {
    std::atomic<double> current_hashrate{0};
    std::atomic<double> average_hashrate{0};
    std::atomic<uint64_t> total_hashes{0};
    std::atomic<uint64_t> total_shares{0};
    std::atomic<uint64_t> accepted_shares{0};
    std::atomic<uint64_t> rejected_shares{0};
    std::atomic<double> uptime_seconds{0};
    std::atomic<bool> is_mining{false};
    std::string pool_url;
    std::string wallet_address;
    std::string algorithm;
    
    // Device-specific stats
    std::map<int, double> device_hashrates;
    std::map<int, double> device_temperatures;
    std::map<int, double> device_power;
    std::map<int, int> device_fan_speeds;
    
    mutable std::mutex stats_mutex;
    
    std::string toJSON() const {
        std::lock_guard<std::mutex> lock(stats_mutex);
        
        std::ostringstream json;
        json << "{\n";
        json << "  \"current_hashrate\": " << std::fixed << std::setprecision(2) << current_hashrate.load() << ",\n";
        json << "  \"average_hashrate\": " << std::fixed << std::setprecision(2) << average_hashrate.load() << ",\n";
        json << "  \"total_hashes\": " << total_hashes.load() << ",\n";
        json << "  \"total_shares\": " << total_shares.load() << ",\n";
        json << "  \"accepted_shares\": " << accepted_shares.load() << ",\n";
        json << "  \"rejected_shares\": " << rejected_shares.load() << ",\n";
        json << "  \"uptime_seconds\": " << std::fixed << std::setprecision(0) << uptime_seconds.load() << ",\n";
        json << "  \"is_mining\": " << (is_mining.load() ? "true" : "false") << ",\n";
        json << "  \"pool_url\": \"" << pool_url << "\",\n";
        json << "  \"wallet_address\": \"" << wallet_address << "\",\n";
        json << "  \"algorithm\": \"" << algorithm << "\",\n";
        
        json << "  \"devices\": [\n";
        bool first_device = true;
        for (const auto& [device_id, hashrate] : device_hashrates) {
            if (!first_device) json << ",\n";
            first_device = false;
            
            json << "    {\n";
            json << "      \"device_id\": " << device_id << ",\n";
            json << "      \"hashrate\": " << std::fixed << std::setprecision(2) << hashrate << ",\n";
            json << "      \"temperature\": " << std::fixed << std::setprecision(1) << device_temperatures.at(device_id) << ",\n";
            json << "      \"power\": " << std::fixed << std::setprecision(1) << device_power.at(device_id) << ",\n";
            json << "      \"fan_speed\": " << device_fan_speeds.at(device_id) << "\n";
            json << "    }";
        }
        json << "\n  ]\n";
        json << "}";
        
        return json.str();
    }
};

/**
 * Mining configuration parameters
 */
struct MiningConfig {
    std::string pool_url;
    std::string wallet_address;
    std::string worker_name;
    std::string password;
    bool use_tls = false;
    int api_port = 8080;
    std::string algorithm = "sha3x";
    int intensity = 8;
    std::vector<int> selected_devices;
    bool auto_tune = true;
    int benchmark_duration = 30;
};

/**
 * Simple HTTP response
 */
struct HttpResponse {
    int status_code;
    std::string content_type;
    std::string body;
    
    HttpResponse(int code = 200, const std::string& type = "application/json", const std::string& body_text = "")
        : status_code(code), content_type(type), body(body_text) {}
    
    std::string toString() const {
        std::ostringstream response;
        response << "HTTP/1.1 " << status_code << " " << getStatusText() << "\r\n";
        response << "Content-Type: " << content_type << "\r\n";
        response << "Content-Length: " << body.length() << "\r\n";
        response << "Access-Control-Allow-Origin: *\r\n";
        response << "\r\n";
        response << body;
        return response.str();
    }
    
private:
    std::string getStatusText() const {
        switch (status_code) {
            case 200: return "OK";
            case 400: return "Bad Request";
            case 404: return "Not Found";
            case 500: return "Internal Server Error";
            default: return "Unknown";
        }
    }
};

/**
 * SHA3X Mining API Server
 */
class SHA3XMiningAPI {
private:
    int port;
    std::atomic<bool> server_running{false};
    std::thread server_thread;
    
    MiningStats mining_stats;
    MiningConfig mining_config;
    
    // API endpoints
    std::map<std::string, std::function<HttpResponse(const std::string&)>> endpoints;
    
public:
    SHA3XMiningAPI(int api_port = 8080) : port(api_port) {
        setupEndpoints();
    }
    
    ~SHA3XMiningAPI() {
        stopServer();
    }
    
    /**
     * Start the API server
     */
    bool startServer() {
        if (server_running) return false;
        
        server_running = true;
        server_thread = std::thread([this]() {
            serverLoop();
        });
        
        std::cout << "✅ Mining API server started on port " << port << "\n";
        std::cout << "📊 Access mining stats at: http://localhost:" << port << "/stats\n";
        std::cout << "🎮 Control miner at: http://localhost:" << port << "/control\n";
        
        return true;
    }
    
    /**
     * Stop the API server
     */
    void stopServer() {
        server_running = false;
        
        if (server_thread.joinable()) {
            server_thread.join();
        }
        
        std::cout << "⏹️  Mining API server stopped\n";
    }
    
    /**
     * Update mining statistics
     */
    void updateStats(const MiningStats& new_stats) {
        // Update individual atomic fields
        mining_stats.current_hashrate = new_stats.current_hashrate.load();
        mining_stats.average_hashrate = new_stats.average_hashrate.load();
        mining_stats.total_hashes = new_stats.total_hashes.load();
        mining_stats.total_shares = new_stats.total_shares.load();
        mining_stats.accepted_shares = new_stats.accepted_shares.load();
        mining_stats.rejected_shares = new_stats.rejected_shares.load();
        mining_stats.uptime_seconds = new_stats.uptime_seconds.load();
        mining_stats.is_mining = new_stats.is_mining.load();
        
        // Update non-atomic fields with mutex protection
        {
            std::lock_guard<std::mutex> lock(mining_stats.stats_mutex);
            mining_stats.pool_url = new_stats.pool_url;
            mining_stats.wallet_address = new_stats.wallet_address;
            mining_stats.algorithm = new_stats.algorithm;
            mining_stats.device_hashrates = new_stats.device_hashrates;
            mining_stats.device_temperatures = new_stats.device_temperatures;
            mining_stats.device_power = new_stats.device_power;
            mining_stats.device_fan_speeds = new_stats.device_fan_speeds;
        }
    }
    
    /**
     * Get current mining statistics
     */
    MiningStats& getStats() {
        return mining_stats;
    }
    
    /**
     * Set mining configuration
     */
    void setConfig(const MiningConfig& config) {
        mining_config = config;
        mining_stats.pool_url = config.pool_url;
        mining_stats.wallet_address = config.wallet_address;
        mining_stats.algorithm = config.algorithm;
    }

private:
    void setupEndpoints() {
        // Statistics endpoints
        endpoints["/stats"] = [this](const std::string& body) {
            return HttpResponse(200, "application/json", mining_stats.toJSON());
        };
        
        endpoints["/stats/summary"] = [this](const std::string& body) {
            return getStatsSummary();
        };
        
        endpoints["/stats/devices"] = [this](const std::string& body) {
            return getDeviceStats();
        };
        
        // Control endpoints
        endpoints["/control/start"] = [this](const std::string& body) {
            return controlStartMining();
        };
        
        endpoints["/control/stop"] = [this](const std::string& body) {
            return controlStopMining();
        };
        
        endpoints["/control/restart"] = [this](const std::string& body) {
            return controlRestartMining();
        };
        
        endpoints["/control/intensity"] = [this](const std::string& body) {
            return controlSetIntensity(body);
        };
        
        // Configuration endpoints
        endpoints["/config"] = [this](const std::string& body) {
            return getConfig();
        };
        
        endpoints["/config/update"] = [this](const std::string& body) {
            return updateConfig(body);
        };
        
        // Utility endpoints
        endpoints["/health"] = [this](const std::string& body) {
            return getHealthStatus();
        };
        
        endpoints["/"] = [this](const std::string& body) {
            return getWelcomePage();
        };
    }
    
    void serverLoop() {
#ifdef _WIN32
        WSADATA wsaData;
        WSAStartup(MAKEWORD(2, 2), &wsaData);
#endif
        
        int server_fd = socket(AF_INET, SOCK_STREAM, 0);
        if (server_fd < 0) {
            std::cerr << "Failed to create socket\n";
            return;
        }
        
        int opt = 1;
#ifdef _WIN32
        setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, (char*)&opt, sizeof(opt));
#else
        setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
#endif
        
        struct sockaddr_in address;
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = INADDR_ANY;
        address.sin_port = htons(port);
        
        if (bind(server_fd, (struct sockaddr*)&address, sizeof(address)) < 0) {
            std::cerr << "Failed to bind to port " << port << "\n";
#ifdef _WIN32
            closesocket(server_fd);
#else
            ::close(server_fd);
#endif
            return;
        }
        
        if (listen(server_fd, 3) < 0) {
            std::cerr << "Failed to listen on socket\n";
#ifdef _WIN32
            closesocket(server_fd);
#else
            ::close(server_fd);
#endif
            return;
        }
        
        std::cout << "🌐 API server listening on port " << port << "\n";
        
        while (server_running) {
            struct sockaddr_in client_addr;
            socklen_t client_len = sizeof(client_addr);
            
            int client_fd = accept(server_fd, (struct sockaddr*)&client_addr, &client_len);
            if (client_fd < 0) {
                if (server_running) {
                    std::cerr << "Failed to accept connection\n";
                }
                continue;
            }
            
            // Handle client request
            handleClientRequest(client_fd);
            
#ifdef _WIN32
            closesocket(client_fd);
#else
            ::close(client_fd);
#endif
        }
        
#ifdef _WIN32
        closesocket(server_fd);
        WSACleanup();
#else
        ::close(server_fd);
#endif
    }
    
    void handleClientRequest(int client_fd) {
        char buffer[4096] = {0};
        int bytes_read = recv(client_fd, buffer, sizeof(buffer) - 1, 0);
        
        if (bytes_read <= 0) return;
        
        std::string request(buffer);
        
        // Parse HTTP request (simplified)
        std::string method, path, version;
        std::istringstream request_stream(request);
        request_stream >> method >> path >> version;
        
        // Handle the request
        HttpResponse response;
        
        if (endpoints.find(path) != endpoints.end()) {
            // Extract body if present
            std::string body;
            size_t body_start = request.find("\r\n\r\n");
            if (body_start != std::string::npos) {
                body = request.substr(body_start + 4);
            }
            
            response = endpoints[path](body);
        } else {
            response = HttpResponse(404, "application/json", R"({"error": "Endpoint not found"})");
        }
        
        std::string response_str = response.toString();
        send(client_fd, response_str.c_str(), response_str.length(), 0);
    }
    
    // API endpoint implementations
    HttpResponse getStatsSummary() {
        std::ostringstream json;
        json << "{\n";
        json << "  \"status\": \"success\",\n";
        json << "  \"data\": {\n";
        json << "    \"current_hashrate\": " << std::fixed << std::setprecision(2) 
             << mining_stats.current_hashrate.load() << ",\n";
        json << "    \"total_shares\": " << mining_stats.total_shares.load() << ",\n";
        json << "    \"accepted_shares\": " << mining_stats.accepted_shares.load() << ",\n";
        json << "    \"rejected_shares\": " << mining_stats.rejected_shares.load() << ",\n";
        json << "    \"uptime\": " << std::fixed << std::setprecision(0) 
             << mining_stats.uptime_seconds.load() << ",\n";
        json << "    \"is_mining\": " << (mining_stats.is_mining.load() ? "true" : "false") << "\n";
        json << "  }\n";
        json << "}";
        
        return HttpResponse(200, "application/json", json.str());
    }
    
    HttpResponse getDeviceStats() {
        std::lock_guard<std::mutex> lock(mining_stats.stats_mutex);
        
        std::ostringstream json;
        json << "{\n";
        json << "  \"status\": \"success\",\n";
        json << "  \"devices\": [\n";
        
        bool first = true;
        for (const auto& [device_id, hashrate] : mining_stats.device_hashrates) {
            if (!first) json << ",\n";
            first = false;
            
            json << "    {\n";
            json << "      \"id\": " << device_id << ",\n";
            json << "      \"hashrate\": " << std::fixed << std::setprecision(2) << hashrate << ",\n";
            json << "      \"temperature\": " << std::fixed << std::setprecision(1) 
                 << mining_stats.device_temperatures.at(device_id) << ",\n";
            json << "      \"power\": " << std::fixed << std::setprecision(1) 
                 << mining_stats.device_power.at(device_id) << ",\n";
            json << "      \"fan_speed\": " << mining_stats.device_fan_speeds.at(device_id) << "\n";
            json << "    }";
        }
        
        json << "\n  ]\n";
        json << "}";
        
        return HttpResponse(200, "application/json", json.str());
    }
    
    HttpResponse controlStartMining() {
        mining_stats.is_mining = true;
        return HttpResponse(200, "application/json", R"({"status": "success", "message": "Mining started"})");
    }
    
    HttpResponse controlStopMining() {
        mining_stats.is_mining = false;
        return HttpResponse(200, "application/json", R"({"status": "success", "message": "Mining stopped"})");
    }
    
    HttpResponse controlRestartMining() {
        mining_stats.is_mining = false;
        std::this_thread::sleep_for(std::chrono::seconds(1));
        mining_stats.is_mining = true;
        return HttpResponse(200, "application/json", R"({"status": "success", "message": "Mining restarted"})");
    }
    
    HttpResponse controlSetIntensity(const std::string& body) {
        // Parse intensity from body (simple JSON parsing)
        size_t intensity_pos = body.find("\"intensity\"");
        if (intensity_pos == std::string::npos) {
            return HttpResponse(400, "application/json", R"({"error": "Invalid intensity format"})");
        }
        
        size_t colon_pos = body.find(":", intensity_pos);
        if (colon_pos == std::string::npos) {
            return HttpResponse(400, "application/json", R"({"error": "Invalid intensity format"})");
        }
        
        size_t value_start = body.find_first_of("0123456789", colon_pos);
        if (value_start == std::string::npos) {
            return HttpResponse(400, "application/json", R"({"error": "Invalid intensity format"})");
        }
        
        size_t value_end = body.find_first_not_of("0123456789", value_start);
        std::string intensity_str = body.substr(value_start, value_end - value_start);
        
        try {
            int intensity = std::stoi(intensity_str);
            if (intensity < 1 || intensity > 16) {
                return HttpResponse(400, "application/json", R"({"error": "Intensity must be between 1 and 16"})");
            }
            
            mining_config.intensity = intensity;
            return HttpResponse(200, "application/json", R"({"status": "success", "intensity": )" + std::to_string(intensity) + "}");
            
        } catch (const std::exception& e) {
            return HttpResponse(400, "application/json", R"({"error": "Invalid intensity value"})");
        }
    }
    
    HttpResponse getConfig() {
        std::ostringstream json;
        json << "{\n";
        json << "  \"status\": \"success\",\n";
        json << "  \"config\": {\n";
        json << "    \"pool_url\": \"" << mining_config.pool_url << "\",\n";
        json << "    \"wallet_address\": \"" << mining_config.wallet_address << "\",\n";
        json << "    \"worker_name\": \"" << mining_config.worker_name << "\",\n";
        json << "    \"algorithm\": \"" << mining_config.algorithm << "\",\n";
        json << "    \"intensity\": " << mining_config.intensity << ",\n";
        json << "    \"auto_tune\": " << (mining_config.auto_tune ? "true" : "false") << ",\n";
        json << "    \"use_tls\": " << (mining_config.use_tls ? "true" : "false") << "\n";
        json << "  }\n";
        json << "}";
        
        return HttpResponse(200, "application/json", json.str());
    }
    
    HttpResponse updateConfig(const std::string& body) {
        // Simple config update (would need proper JSON parsing in production)
        return HttpResponse(200, "application/json", R"({"status": "success", "message": "Configuration updated"})");
    }
    
    HttpResponse getHealthStatus() {
        std::ostringstream json;
        json << "{\n";
        json << "  \"status\": \"healthy\",\n";
        json << "  \"timestamp\": \"" << getCurrentTimestamp() << "\",\n";
        json << "  \"uptime\": " << std::fixed << std::setprecision(0) << mining_stats.uptime_seconds.load() << ",\n";
        json << "  \"mining_active\": " << (mining_stats.is_mining.load() ? "true" : "false") << ",\n";
        json << "  \"api_version\": \"1.0\"\n";
        json << "}";
        
        return HttpResponse(200, "application/json", json.str());
    }
    
    HttpResponse getWelcomePage() {
        std::string html = R"(<!DOCTYPE html>
<html>
<head>
    <title>SHA3X Mining API</title>
    <style>
        body { font-family: Arial, sans-serif; margin: 40px; }
        .endpoint { background: #f0f0f0; padding: 10px; margin: 10px 0; border-radius: 5px; }
        .method { color: #0066cc; font-weight: bold; }
        .path { color: #009900; font-family: monospace; }
    </style>
</head>
<body>
    <h1>SHA3X Mining API</h1>
    <p>Welcome to the SHA3X mining control API</p>
    
    <h2>Available Endpoints</h2>
    
    <div class="endpoint">
        <span class="method">GET</span> <span class="path">/stats</span> - Get detailed mining statistics
    </div>
    
    <div class="endpoint">
        <span class="method">GET</span> <span class="path">/stats/summary</span> - Get mining summary
    </div>
    
    <div class="endpoint">
        <span class="method">GET</span> <span class="path">/stats/devices</span> - Get device-specific statistics
    </div>
    
    <div class="endpoint">
        <span class="method">POST</span> <span class="path">/control/start</span> - Start mining
    </div>
    
    <div class="endpoint">
        <span class="method">POST</span> <span class="path">/control/stop</span> - Stop mining
    </div>
    
    <div class="endpoint">
        <span class="method">POST</span> <span class="path">/control/intensity</span> - Set mining intensity
    </div>
    
    <div class="endpoint">
        <span class="method">GET</span> <span class="path">/config</span> - Get current configuration
    </div>
    
    <div class="endpoint">
        <span class="method">GET</span> <span class="path">/health</span> - Get health status
    </div>
    
    <h2>Example Usage</h2>
    <pre>
# Get mining statistics
curl http://localhost:8080/stats

# Start mining
curl -X POST http://localhost:8080/control/start

# Set intensity to 12
curl -X POST http://localhost:8080/control/intensity \\
  -H "Content-Type: application/json" \\
  -d '{"intensity": 12}'
    </pre>
</body>
</html>)";
        
        return HttpResponse(200, "text/html", html);
    }
    
    std::string getCurrentTimestamp() const {
        auto now = std::chrono::system_clock::now();
        auto time_t = std::chrono::system_clock::to_time_t(now);
        
        std::stringstream ss;
        ss << std::put_time(std::localtime(&time_t), "%Y-%m-%d %H:%M:%S");
        return ss.str();
    }
};

/**
 * Enhanced miner with API integration
 */
class SHA3XMinerWithAPI {
private:
    SHA3XMiningAPI api;
    MiningStats stats;
    MiningConfig config;
    std::thread stats_update_thread;
    std::atomic<bool> running{false};
    
public:
    SHA3XMinerWithAPI(int api_port = 8080) : api(api_port) {}
    
    ~SHA3XMinerWithAPI() {
        stop();
    }
    
    bool start(const MiningConfig& mining_config) {
        config = mining_config;
        
        // Start API server
        if (!api.startServer()) {
            std::cerr << "Failed to start API server\n";
            return false;
        }
        
        // Set initial configuration
        api.setConfig(config);
        
        // Start stats update thread
        running = true;
        stats_update_thread = std::thread([this]() {
            statsUpdateLoop();
        });
        
        std::cout << "🚀 SHA3X Miner with API started successfully\n";
        std::cout << "🌐 API available at: http://localhost:" << config.api_port << "\n";
        
        return true;
    }
    
    void stop() {
        running = false;
        
        if (stats_update_thread.joinable()) {
            stats_update_thread.join();
        }
        
        api.stopServer();
        
        std::cout << "⏹️  SHA3X Miner with API stopped\n";
    }
    
    MiningStats& getStats() {
        return api.getStats();
    }
    
    SHA3XMiningAPI& getAPI() {
        return api;
    }

private:
    void statsUpdateLoop() {
        auto start_time = std::chrono::steady_clock::now();
        
        while (running) {
            auto now = std::chrono::steady_clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - start_time).count();
            
            // Update uptime
            stats.uptime_seconds = elapsed;
            
            // Simulate mining stats (would be from actual miner)
            if (stats.is_mining) {
                stats.current_hashrate = 45.2 + (rand() % 100) / 10.0; // 45.2-55.2 MH/s
                stats.average_hashrate = 48.5;
                stats.total_hashes += 1000000; // Simulate hashes
                
                // Randomly find shares
                if (rand() % 100 < 5) { // 5% chance per update
                    stats.total_shares++;
                    if (rand() % 100 < 95) { // 95% acceptance rate
                        stats.accepted_shares++;
                    } else {
                        stats.rejected_shares++;
                    }
                }
            }
            
            // Update API stats
            api.updateStats(stats);
            
            std::this_thread::sleep_for(std::chrono::seconds(5));
        }
    }
};

#endif // SHA3X_MINING_API_H