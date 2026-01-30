# 🚀 SHA3X Miner Setup Guide - Get Mining NOW!

## 📍 Where You Are
**Directory**: `C:\Users\douglaswhittingham\AppData\Roaming\npm\cr29-kernel\`
**Ready Files**: Complete SHA3X miner source code, build scripts, and configuration
**Your Config**: Kryptex XTM-SHA3X pool with your exact wallet and worker

## 🎯 Your Goal: Real Mining

You want to mine XTM with:
- **Pool**: `xtm-sha3x.kryptex.network:7039` (XTM-SHA3X pool)
- **Wallet**: `12LfqTi7aQKz9cpxU1AsRW7zNCRkKYdwsxVB1Qx47q3ZGS2DQUpMHDKoAdi2apbaFDdHzrjnDbe4jK1B4DbYo4titQH`
- **Worker**: `9070xt`
- **Target**: 45-55 MH/s on RX 9070 XT

## 🔧 Step-by-Step Setup

### **Step 1: Install Prerequisites**

**A. Visual Studio 2022 (CRITICAL)**
```
Download: https://visualstudio.microsoft.com/downloads/
Install: Visual Studio 2022 Community (FREE)
Select: Desktop development with C++ workload
```

**B. AMD GPU Drivers**
```
Download: https://www.amd.com/en/support
Install: Latest Adrenalin drivers for your GPU
Verify: Device Manager shows AMD GPU with no warnings
```

**C. AMD APP SDK (Optional but recommended)**
```
Download: https://github.com/GPUOpen-LibrariesAndSDKs/OCL-SDK/releases
Install: AMD APP SDK for OpenCL support
```

### **Step 2: Set Up Build Environment**

**A. Open Developer Command Prompt**
```
Start Menu → "Developer Command Prompt for VS 2022"
Navigate to: cd C:\Users\douglaswhittingham\AppData\Roaming\npm\cr29-kernel
```

**B. Verify Everything is Ready**
```cmd
cl /?                    :: Should show Visual Studio compiler info
where OpenCL.lib         :: Should find OpenCL library
dir src\*.cpp            :: Should see our source files
```

### **Step 3: Build the Real Miner**

**Option A: Use the comprehensive launcher**
```cmd
start_sha3x_miner.bat
```
Choose option 1 to build and run.

**Option B: Manual build**
```cmd
mkdir build_real
cd build_real

cl /EHsc /O2 /MD /nologo ^
    /I "..\src" ^
    /I "..\include" ^
    ..\src\sha3x_pool_miner.cpp ^
    /link ws2_32.lib OpenCL.lib ^
    /out:sha3x_miner.exe

mkdir src
copy "..\src\*.cl" "src\"
```

### **Step 4: Configure for Your Setup**

**Create your config file**: `my_config.json`
```json
{
  "mining": {
    "algorithm": "sha3x",
    "intensity": 8,
    "workgroup_size": 256,
    "kernel_variant": "enhanced"
  },
  "pool": {
    "url": "xtm-c29-us.kryptex.network:8040",
    "use_tls": true,
    "timeout": 30
  },
  "wallet": {
    "address": "12LfqTi7aQKz9cpxU1AsRW7zNCRkKYdwsxVB1Qx47q3ZGS2DQUpMHDKoAdi2apbaFDdHzrjnDbe4jK1B4DbYo4titQH",
    "worker": "9070xt",
    "password": "x"
  },
  "devices": {
    "selection": "auto",
    "intensity_per_device": {}
  },
  "monitoring": {
    "api_enabled": true,
    "api_port": 8080,
    "log_level": "info"
  }
}
```

### **Step 5: Start Mining**

**Command line (simple):**
```cmd
sha3x_miner.exe -o xtm-sha3x.kryptex.network:7039 -u 12LfqTi7aQKz9cpxU1AsRW7zNCRkKYdwsxVB1Qx47q3ZGS2DQUpMHDKoAdi2apbaFDdHzrjnDbe4jK1B4DbYo4titQH.9070xt -p x --api-port 8080
```

**Command line (full options):**
```cmd
sha3x_miner.exe ^
    -o xtm-sha3x.kryptex.network:7039 ^
    -u 12LfqTi7aQKz9cpxU1AsRW7zNCRkKYdwsxVB1Qx47q3ZGS2DQUpMHDKoAdi2apbaFDdHzrjnDbe4jK1B4DbYo4titQH.9070xt ^
    -p x ^
    --api-port 8080 ^
    --intensity 8 ^
    --verbose
```

**Command line (full options):**
```cmd
sha3x_miner.exe ^
    -o xtm-c29-us.kryptex.network:8040 ^
    -u 12LfqTi7aQKz9cpxU1AsRW7zNCRkKYdwsxVB1Qx47q3ZGS2DQUpMHDKoAdi2apbaFDdHzrjnDbe4jK1B4DbYo4titQH.9070xt ^
    -p x ^
    --tls on ^
    --api-port 8080 ^
    --intensity 8 ^
    --workgroup-size 256 ^
    --verbose
```

## 📊 Expected Results

**Performance Targets:**
- **RX 9070 XT**: 45-55 MH/s
- **RX 7900 XTX**: 70-85 MH/s  
- **RX 6800 XT**: 35-45 MH/s

**Quality Metrics:**
- **Share Acceptance**: >90% (target: >95%)
- **Pool Connection**: 99%+ uptime
- **Temperature**: <85°C under load

## 🔍 Monitoring

**Web Interface**: `http://localhost:8080/`
**API Stats**: `http://localhost:8080/stats`
**Control API**: Start/stop mining, adjust settings

## 🚨 Troubleshooting

**Build Errors:**
- `CL/cl.h not found` → Install AMD GPU drivers
- `cl.exe not found` → Run from Developer Command Prompt
- `OpenCL.lib not found` → Install AMD APP SDK

**Runtime Issues:**
- `GPU not detected` → Check AMD drivers
- `Low hashrate` → Run performance validation
- `High temperature` → Reduce intensity, improve cooling

**Performance Issues:**
- Run: `sha3x_test_suite.exe --validate-perf`
- Check: `demo_results.txt` for baseline
- Monitor: API stats for real-time metrics

## 🎯 Next Steps

1. **Build it**: Run `start_sha3x_miner.bat` and choose option 1
2. **Test it**: Run performance validation
3. **Monitor it**: Check web interface at localhost:8080
4. **Optimize it**: Use auto-tuning and performance tuning
5. **Scale it**: Add more GPUs, optimize settings

## 📚 Quick Commands

```batch
# Build everything
start_sha3x_miner.bat

# Quick start (if already built)
quick_start.bat

# Performance validation
sha3x_test_suite.exe --validate-perf --duration 30

# Stress testing
sha3x_test_suite.exe --stress-test --duration 60 --thermal-stress

# Check system
start_sha3x_miner.bat & choose option 5
```

## 💡 Pro Tips

1. **Start with validation** - Ensure your system is ready
2. **Monitor closely** - Watch temperatures and hashrate
3. **Test incrementally** - Start with one GPU, add more
4. **Document settings** - Save successful configurations
5. **Stay updated** - Check for miner updates regularly

**You're ready to mine XTM!** 🚀

Just run `start_sha3x_miner.bat` and follow the prompts to build and start mining with your exact configuration.