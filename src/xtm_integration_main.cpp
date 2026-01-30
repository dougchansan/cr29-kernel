/**
 * XTM Integration Test Main Executable
 * Connects to live Kryptex pool for real mining testing
 */

#include "xtm_integration_test.h"
#include <iostream>
#include <string>
#include <chrono>
#include <thread>

void printUsage() {
    std::cout << "XTM SHA3X Integration Test\n";
    std::cout << "==========================\n\n";
    std::cout << "This tool connects to the live Kryptex XTM pool for integration testing.\n";
    std::cout << "Wallet: 12LfqTi7aQKz9cpxU1AsRW7zNCRkKYdwsxVB1Qx47q3ZGS2DQUpMHDKoAdi2apbaFDdHzrjnDbe4jK1B4DbYo4titQH\n";
    std::cout << "Pool: xtm-c29-us.kryptex.network:8040 (TLS enabled)\n\n";
    std::cout << "Usage: xtm_integration_test [options]\n\n";
    std::cout << "Options:\n";
    std::cout << "  --duration <minutes>  Test duration in minutes (default: 10)\n";
    std::cout << "  --api-port <port>     API server port (default: 8080)\n";
    std::cout << "  --help                Show this help message\n\n";
    std::cout << "API Endpoints (during test):\n";
    std::cout << "  http://localhost:8080/stats          - Live statistics\n";
    std::cout << "  http://localhost:8080/control/stop   - Stop mining\n";
    std::cout << "  http://localhost:8080/               - Web interface\n\n";
    std::cout << "Example:\n";
    std::cout << "  xtm_integration_test --duration 15\n";
}

int main(int argc, char* argv[]) {
    std::cout << "========================================\n";
    std::cout << "XTM SHA3X Integration Test\n";
    std::cout << "Live Pool Testing with Kryptex Network\n";
    std::cout << "========================================\n\n";
    
    // Parse command line arguments
    int test_duration = 10; // default 10 minutes
    int api_port = 8080;    // default API port
    
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        
        if (arg == "--help" || arg == "-h") {
            printUsage();
            return 0;
        }
        else if (arg == "--duration" && i + 1 < argc) {
            try {
                test_duration = std::stoi(argv[++i]);
                if (test_duration < 1 || test_duration > 120) {
                    std::cerr << "Error: Duration must be between 1 and 120 minutes\n";
                    return 1;
                }
            } catch (const std::exception& e) {
                std::cerr << "Error: Invalid duration value\n";
                return 1;
            }
        }
        else if (arg == "--api-port" && i + 1 < argc) {
            try {
                api_port = std::stoi(argv[++i]);
                if (api_port < 1024 || api_port > 65535) {
                    std::cerr << "Error: API port must be between 1024 and 65535\n";
                    return 1;
                }
            } catch (const std::exception& e) {
                std::cerr << "Error: Invalid API port\n";
                return 1;
            }
        }
        else {
            std::cerr << "Error: Unknown argument: " << arg << "\n";
            printUsage();
            return 1;
        }
    }
    
    // Validate wallet configuration
    std::string wallet = "12LfqTi7aQKz9cpxU1AsRW7zNCRkKYdwsxVB1Qx47q3ZGS2DQUpMHDKoAdi2apbaFDdHzrjnDbe4jK1B4DbYo4titQH";
    std::string worker = "9070xt";
    
    std::cout << "🔍 Configuration:\n";
    std::cout << "  Pool: xtm-c29-us.kryptex.network:8040 (TLS enabled)\n";
    std::cout << "  Wallet: " << wallet.substr(0, 20) << "...\n";
    std::cout << "  Worker: " << worker << "\n";
    std::cout << "  Duration: " << test_duration << " minutes\n";
    std::cout << "  API Port: " << api_port << "\n\n";
    
    // Safety check
    std::cout << "⚠️  WARNING: This will connect to the LIVE Kryptex XTM pool!\n";
    std::cout << "⚠️  This will perform real mining with the configured wallet address.\n";
    std::cout << "⚠️  Ensure you have permission to mine to this wallet.\n\n";
    
    std::cout << "Do you want to continue? (yes/no): ";
    std::string response;
    std::getline(std::cin, response);
    
    if (response != "yes" && response != "y") {
        std::cout << "❌ Test aborted by user\n";
        return 0;
    }
    
    std::cout << "\n🚀 Starting integration test...\n";
    std::cout << "📊 Monitor progress at: http://localhost:" << api_port << "/stats\n";
    std::cout << "🌐 Web interface: http://localhost:" << api_port << "/\n\n";
    
    try {
        // Create and run integration test
        XTMIntegrationTest test;
        test.runIntegrationTest(test_duration);
        
        std::cout << "\n✅ Integration test completed successfully!\n";
        std::cout << "📄 Check 'xtm_integration_report.txt' for detailed results.\n";
        
        return 0;
        
    } catch (const std::exception& e) {
        std::cerr << "\n❌ Integration test failed: " << e.what() << "\n";
        return 1;
    }
}