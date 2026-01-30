@echo off
:: SHA3X Miner Quick Start - Real Mining Implementation
:: Simple launcher for the actual SHA3X miner
:: Your configuration: Kryptex XTM pool with your wallet

title SHA3X Miner - Quick Start
echo.
echo ========================================
echo 🚀 SHA3X Miner for XTM - QUICK START 🚀
echo ========================================
echo 📍 Pool: xtm-sha3x.kryptex.network:7039
echo 💰 Wallet: 12LfqTi7aQKz9cpxU1AsRW7zNCRkKYdwsxVB1Qx47q3ZGS2DQUpMHDKoAdi2apbaFDdHzrjnDbe4jK1B4DbYo4titQH
echo 🖥️  Worker: 9070xt
echo ========================================
echo.

:: Check if miner executable exists
if exist "build_real\sha3x_miner.exe" (
    echo ✅ Real miner found! Starting mining...
    echo.
    cd build_real
    echo 🎯 Starting mining with your configuration...
    echo.
    sha3x_miner.exe -o xtm-sha3x.kryptex.network:7039 -u 12LfqTi7aQKz9cpxU1AsRW7zNCRkKYdwsxVB1Qx47q3ZGS2DQUpMHDKoAdi2apbaFDdHzrjnDbe4jK1B4DbYo4titQH.9070xt -p x --api-port 8080 --verbose
    cd ..
) else (
    echo ❌ Real miner not found!
    echo.
    echo 🔧 To build the real miner:
    echo 1. Install Visual Studio 2022
    echo 2. Install AMD GPU drivers
    echo 3. Run from Developer Command Prompt
    echo 4. Use: start_sha3x_miner.bat (full launcher)
    echo.
    echo 💡 For now, you can test with the Python demo:
    echo    PYTHONIOENCODING=utf-8 python sha3x_demo.py
)

echo.
echo 👋 Mining session completed!
echo 📊 Check results in: demo_results.txt
echo 🌐 Monitor at: http://localhost:8080/
pause