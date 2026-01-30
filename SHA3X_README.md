# SHA3X Miner for XTM Coin

Optimized SHA3X mining implementation for XTM cryptocurrency, based on the cr29-kernel project structure. This miner replaces the Cuckaroo-29 algorithm with SHA3X while maintaining compatibility with the existing pool infrastructure and RDNA 4 optimizations.

## Overview

This implementation provides:
- **SHA3X Algorithm**: Full SHA3X implementation for XTM coin mining
- **GPU Optimization**: RDNA 4 optimized OpenCL kernels
- **Pool Compatibility**: Standard stratum protocol support
- **CPU Verification**: Reference implementation for solution validation
- **Modular Design**: Clean algorithm abstraction interface

## Architecture

### Algorithm Structure
```
src/
├── sha3x_algo.h              # Algorithm interface definition
├── sha3x_cpu.h               # CPU reference implementation
├── sha3x_implementation.h    # Concrete algorithm implementation
├── sha3x_kernel.cl           # OpenCL GPU kernels
└── sha3x_pool_miner.cpp      # Main pool miner application
```

### Key Components

1. **SHA3XAlgorithm Interface**: Modular abstraction for different PoW algorithms
2. **SHA3XCPU**: Deterministic CPU reference for verification and testing
3. **SHA3XGPUMiner**: OpenCL implementation with RDNA 4 optimizations
4. **SHA3XStratumClient**: Pool connectivity with XTM protocol support

## Build Instructions

### Prerequisites
- AMD ROCm 6.0+ or AMD GPU drivers with OpenCL 2.0
- CMake 3.20+
- C++17 compatible compiler
- OpenCL development libraries

### Build Process
```bash
# Clone repository
git clone https://github.com/dougchansan/cr29-kernel.git
cd cr29-kernel

# Create build directory
mkdir build && cd build

# Configure with CMake
cmake ..

# Build
make -j$(nproc)

# Install (optional)
sudo make install
```

### Build Outputs
- `sha3x_miner`: Main SHA3X mining executable
- `cr29_miner`: Legacy Cuckaroo miner (for reference)

## Usage

### Pool Mining
```bash
# Basic pool mining
./sha3x_miner -o pool.xtmcoin.com:3333 -u wallet_address.worker_name

# With TLS encryption
./sha3x_miner -o pool.xtmcoin.com:443 -u wallet_address.worker_name --tls

# Multiple GPUs
./sha3x_miner -o pool.xtmcoin.com:3333 -u wallet_address.worker_name -d 0,1,2

# Custom intensity
./sha3x_miner -o pool.xtmcoin.com:3333 -u wallet_address.worker_name --intensity 8
```

### Benchmark Mode
```bash
# Run benchmark
./sha3x_miner --benchmark

# Verbose benchmark
./sha3x_miner --benchmark --verbose

# Specific kernel variant
./sha3x_miner --benchmark --variant enhanced
```

### Command Line Options
```
Options:
  -o pool:port     Pool address (e.g., pool.xtmcoin.com:3333)
  -u username      Mining username/wallet address
  -p password      Mining password (default: x)
  -d device        GPU device index (default: 1)
  --tls            Enable TLS encryption
  --benchmark      Run benchmark only (no pool)
  --verbose        Verbose output
  --variant        Kernel variant: mining or enhanced (default: enhanced)
  --intensity      Work intensity multiplier (1-16)
  -h, --help       Show help message
```

## Performance

### RDNA 4 Optimizations
- **Wavefront Size 32**: Native RDNA wavefront utilization
- **Shared Memory**: Header caching for coalesced access
- **Memory Coalescing**: Optimized memory access patterns
- **Instruction Prefetch**: 32KB instruction cache utilization

### Expected Performance
- RX 9070 XT: ~50-100 MH/s (estimated)
- RX 7900 XTX: ~80-150 MH/s (estimated)
- RX 6800 XT: ~40-80 MH/s (estimated)

*Note: Actual performance depends on pool difficulty, kernel variant, and optimization settings.*

## Algorithm Details

### SHA3X Specification
- **Hash Function**: Keccak-f[1600] (SHA3-256)
- **Output Size**: 256 bits (32 bytes)
- **Input Format**: 80-byte header + 8-byte nonce
- **Domain Separation**: XTM-specific domain separation byte
- **Endianness**: Little-endian nonce, big-endian hash interpretation

### Header Format (80 bytes)
```
[0-3]   Version (4 bytes)
[4-35]  Previous Block Hash (32 bytes)
[36-67] Merkle Root (32 bytes)
[68-71] Timestamp (4 bytes)
[72-75] Target Bits (4 bytes)
[76-79] Nonce (4 bytes) - set by miner
```

### Stratum Protocol
- **Subscribe**: `mining.subscribe` with miner agent
- **Notify**: `mining.notify` with job parameters
- **Submit**: `mining.submit` with nonce and work proof

## Testing

### Unit Tests
```bash
# Run CPU reference tests (if implemented)
./sha3x_miner --test-cpu

# Validate against known test vectors
./sha3x_miner --test-vectors
```

### Integration Testing
1. **CPU Verification**: GPU solutions verified against CPU reference
2. **Pool Connectivity**: Stratum protocol compliance
3. **Share Submission**: Valid share acceptance by pools
4. **Performance Benchmarking**: Hashrate measurement and optimization

## Configuration

### Kernel Variants
- **mining**: Basic single-kernel implementation
- **enhanced**: Multi-nonce processing with shared memory optimization

### Performance Tuning
```bash
# Higher intensity (more work per kernel)
./sha3x_miner -o pool:port -u wallet -p x --intensity 16

# Lower intensity (more frequent updates)
./sha3x_miner -o pool:port -u wallet -p x --intensity 2

# Specific workgroup size (advanced)
./sha3x_miner -o pool:port -u wallet -p x --workgroup-size 256
```

## Troubleshooting

### Common Issues

1. **OpenCL Not Found**
   ```
   Error: AMD OpenCL platform not found
   Solution: Install AMD ROCm or GPU drivers with OpenCL support
   ```

2. **Kernel Build Failed**
   ```
   Error: Build failed with compilation errors
   Solution: Check OpenCL version compatibility and driver updates
   ```

3. **No Shares Accepted**
   ```
   Error: Shares rejected by pool
   Solution: Verify pool address, username format, and algorithm compatibility
   ```

4. **Low Hashrate**
   ```
   Performance lower than expected
   Solution: Try different kernel variants and intensity settings
   ```

### Debug Mode
```bash
# Enable debug output
./sha3x_miner -o pool:port -u wallet --verbose --debug

# Test specific components
./sha3x_miner --test-kernels
./sha3x_miner --test-stratum
```

## Development

### Adding New Features
1. Extend `SHA3XAlgorithm` interface for new functionality
2. Implement in `SHA3XImplementation` class
3. Update OpenCL kernels if GPU changes needed
4. Add tests to `SHA3XTestVectors`

### Optimizing Performance
1. Profile kernel execution with AMD ROCm tools
2. Optimize memory access patterns
3. Tune workgroup sizes for specific GPUs
4. Experiment with instruction scheduling

## Known Limitations

- **Pool Protocol**: Currently supports basic stratum, extended protocols pending
- **Multi-GPU**: Device selection works, performance scaling needs optimization
- **Windows Support**: Primary development on Linux, Windows builds experimental
- **Algorithm Updates**: SHA3X spec may evolve, requiring kernel updates

## Contributing

1. Fork the repository
2. Create feature branch (`git checkout -b feature/sha3x-optimization`)
3. Commit changes with clear descriptions
4. Push to your fork
5. Submit pull request with performance benchmarks

## References

- [XTM Coin Specification](https://example.com/xtm-spec) *(placeholder)*
- [SHA3 Standard](https://nvlpubs.nist.gov/nistpubs/FIPS/NIST.FIPS.202.pdf)
- [AMD RDNA 4 ISA](https://www.amd.com/content/dam/amd/en/documents/radeon-tech-docs/instruction-set-architectures/rdna4-instruction-set-architecture.pdf)
- [OpenCL 2.0 Specification](https://www.khronos.org/registry/OpenCL/specs/opencl-2.0.pdf)

## License

This project builds upon the original cr29-kernel project. Please refer to the original license terms and ensure compliance with XTM coin mining requirements.

## Support

For issues and questions:
1. Check existing GitHub issues
2. Run with `--verbose` flag for detailed logging
3. Include system information and error messages in bug reports
4. Test with `--benchmark` mode to isolate performance issues