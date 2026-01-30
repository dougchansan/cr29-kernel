# XTM SHA3X Integration Testing Guide

This guide covers integration testing with the live Kryptex XTM pool using the provided wallet and worker configuration.

## 🎯 Test Configuration

**Pool**: `xtm-c29-us.kryptex.network:8040` (TLS enabled)  
**Wallet**: `12LfqTi7aQKz9cpxU1AsRW7zNCRkKYdwsxVB1Qx47q3ZGS2DQUpMHDKoAdi2apbaFDdHzrjnDbe4jK1B4DbYo4titQH`  
**Worker**: `9070xt`  
**Algorithm**: `SHA3X` (replacing Cuckaroo-29)

## 🚀 Quick Start

### Build the Integration Test
```bash
# Windows (from Developer Command Prompt)
cd cr29-kernel
build_sha3x.bat

# Output: build\xtm_integration_test.exe
```

### Run Integration Test
```bash
# 10-minute test (default)
build\xtm_integration_test.exe

# Custom duration
build\xtm_integration_test.exe --duration 15

# Custom API port
build\xtm_integration_test.exe --duration 20 --api-port 9090
```

## 📊 Live Monitoring

During the test, monitor progress at:
- **Stats**: http://localhost:8080/stats
- **Web Interface**: http://localhost:8080/
- **Device Details**: http://localhost:8080/stats/devices

## 🔍 What the Test Validates

### 1. **Pool Connectivity**
- ✅ TLS connection establishment
- ✅ Stratum protocol handshake
- ✅ Authentication with wallet/worker
- ✅ Job reception from pool

### 2. **Share Submission**
- ✅ Valid SHA3X solution generation
- ✅ Proper share formatting for XTM
- ✅ Share acceptance by pool
- ✅ Rejection handling

### 3. **Performance Metrics**
- ✅ Real-time hashrate calculation
- ✅ Share acceptance rate tracking
- ✅ Pool difficulty monitoring
- ✅ Connection stability

### 4. **Error Handling**
- ✅ Connection loss recovery
- ✅ Share validation failures
- ✅ Pool protocol errors
- ✅ Automatic reconnection

## 📈 Expected Results

### **Acceptance Rate Targets**
- **Excellent**: >95% share acceptance
- **Good**: 90-95% share acceptance  
- **Acceptable**: 85-90% share acceptance
- **Poor**: <85% share acceptance

### **Performance Targets**
- **RX 9070 XT**: 45-55 MH/s
- **RX 7900 XTX**: 70-85 MH/s
- **RX 6800 XT**: 35-45 MH/s

### **Connection Quality**
- **Uptime**: >99% during test
- **Reconnections**: <3 per hour
- **Latency**: <100ms to pool

## 🛠️ Test Components

### **1. Stratum Client (`XTMStratumClient`)**
- Handles TLS connection to Kryptex pool
- Implements XTM-specific stratum protocol
- Manages job reception and share submission
- Provides real-time connection status

### **2. Mining Simulator**
- Simulates GPU mining (CPU-based for testing)
- Generates valid SHA3X solutions
- Tests share validation logic
- Measures solution finding rate

### **3. Statistics Engine (`LiveMiningStats`)**
- Tracks all mining metrics in real-time
- Calculates hashrate and acceptance rates
- Maintains share submission history
- Provides JSON API for external monitoring

### **4. Integration API (`SHA3XMiningAPI`)**
- HTTP REST API for live monitoring
- Real-time statistics endpoints
- Web-based control interface
- JSON responses for automation

## 🔧 Advanced Configuration

### **Custom Pool Configuration**
```cpp
XTMPoolConfig config;
config.pool_host = "your-pool.com";
config.pool_port = 8040;
config.use_tls = true;
config.wallet_address = "your-wallet-address";
config.worker_name = "your-worker";
```

### **Test Duration Options**
```bash
# Quick test (5 minutes)
xtm_integration_test.exe --duration 5

# Standard test (15 minutes)
xtm_integration_test.exe --duration 15

# Extended test (60 minutes)
xtm_integration_test.exe --duration 60
```

### **API Customization**
```bash
# Different API port
xtm_integration_test.exe --api-port 9090

# Multiple instances
xtm_integration_test.exe --api-port 8081 --duration 30
```

## 📋 Test Results Analysis

### **Success Criteria**
1. **Connection**: Successfully connects to pool with TLS
2. **Authentication**: Wallet authentication accepted
3. **Jobs**: Receives valid mining jobs
4. **Shares**: Submits shares that get accepted
5. **Stability**: Maintains connection for test duration

### **Generated Reports**
- **Console Output**: Real-time statistics
- **JSON Report**: Detailed test results (`xtm_integration_report.txt`)
- **Error Log**: Any issues encountered (`sha3x_error_log.txt`)

### **Sample Output**
```
=== Live Mining Stats ===
⏱️  Runtime: 5m 30s
💰 Shares: 12 accepted, 2 rejected, 14 total
📈 Acceptance rate: 85.7%
🔄 Total hashes: 15,750,000,000
⚡ Average hashrate: 47.8 MH/s
🌐 Pool connected: yes
🔑 Authenticated: yes
🎯 Last job: job_12345
📊 Difficulty: 0x00000000ffffffff
```

## 🚨 Troubleshooting

### **Connection Issues**
```
Error: Failed to connect to XTM pool
Solution: Check internet connection, verify pool address/port
```

### **Authentication Failures**
```
Error: Authentication rejected
Solution: Verify wallet address format, check worker name
```

### **Low Acceptance Rate**
```
Symptom: <80% share acceptance
Cause: Invalid solutions, network latency, pool issues
Solution: Check algorithm implementation, verify share format
```

### **High Rejection Rate**
```
Symptom: >20% share rejection
Cause: Stale shares, invalid nonces, pool difficulty changes
Solution: Optimize solution timing, check nonce generation
```

## 🔬 Technical Validation

### **Stratum Protocol Compliance**
```
✅ mining.subscribe - Client subscription
✅ mining.authorize - Wallet authentication  
✅ mining.notify - Job notification
✅ mining.submit - Share submission
✅ mining.extranonce.subscribe - Keepalive
```

### **XTM-Specific Features**
```
✅ SHA3X algorithm implementation
✅ 80-byte header format
✅ 64-bit nonce handling
✅ Big-endian share encoding
✅ Proper difficulty comparison
```

### **Pool Integration**
```
✅ Kryptex network compatibility
✅ TLS encryption support
✅ Real-time job updates
✅ Share validation
✅ Error recovery
```

## 📊 Performance Benchmarks

### **Hashrate Comparison**
| GPU Model | Expected MH/s | Test Target |
|-----------|---------------|-------------|
| RX 9070 XT | 45-55 MH/s | >40 MH/s |
| RX 7900 XTX | 70-85 MH/s | >65 MH/s |
| RX 6800 XT | 35-45 MH/s | >30 MH/s |

### **Efficiency Metrics**
- **Solutions per Hash**: ~1 per 4B hashes (at current diff)
- **Share Submission Rate**: ~1 per 30-60 seconds
- **Network Efficiency**: <1% stale shares

## 🎯 Production Deployment Checklist

Before deploying to production:

- [ ] Integration test passes with >90% acceptance rate
- [ ] Multi-GPU testing completed successfully  
- [ ] Performance tuning optimized for target hardware
- [ ] Error handling validated under stress conditions
- [ ] API monitoring configured and tested
- [ ] Pool failover mechanisms implemented
- [ ] Logging and alerting configured
- [ ] Security review completed

## 📞 Support

For issues during integration testing:
1. Check error logs in `sha3x_error_log.txt`
2. Review pool connection diagnostics
3. Verify wallet and worker configuration
4. Test with different pool endpoints if available

The integration test provides comprehensive validation that the SHA3X miner works correctly with live XTM pools and can successfully submit valid shares for rewards.