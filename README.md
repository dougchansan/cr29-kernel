# CR29 OpenCL Kernel for AMD RDNA 4 (gfx1201)

Optimized Cuckaroo-29 mining kernel for AMD RX 9070 XT and other RDNA 4 GPUs.

## Algorithm Overview

Cuckaroo-29 is a memory-hard proof-of-work based on the Cuckoo Cycle algorithm:

1. **Edge Generation**: Generate 2^29 edges using SipHash-2-4
2. **Edge Trimming**: ~176 rounds to eliminate edges not in cycles
3. **Cycle Detection**: Find 42-cycles in the remaining graph
4. **Proof Recovery**: Recover nonces for valid cycles

## RDNA 4 Optimizations

- **Wavefront Size 32**: Native RDNA wavefront (vs 64 for GCN)
- **4-bit Cache Control**: New GLC/SLC/DLC/TH bits for L1/L2 optimization
- **Fine-grained Memory Sync**: Better wait instruction granularity
- **32KB Instruction Prefetch**: Improved kernel execution

## Build Requirements

- AMD ROCm 6.0+ or AMD GPU drivers with OpenCL 2.0
- CMake 3.20+

## References

- [tromp/cuckoo](https://github.com/tromp/cuckoo) - Reference implementation
- [AMD RDNA 4 ISA](https://www.amd.com/content/dam/amd/en/documents/radeon-tech-docs/instruction-set-architectures/rdna4-instruction-set-architecture.pdf)
- [Chips and Cheese RDNA 4 Analysis](https://chipsandcheese.com/p/examining-amds-rdna-4-changes-in-llvm)
