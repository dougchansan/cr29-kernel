/**
 * Edge Trimming Kernels for Cuckaroo-29
 * Optimized for AMD RDNA 4 (gfx1201)
 *
 * The trimmer progressively eliminates edges that cannot be part of 42-cycles.
 * An edge can only be in a cycle if both its nodes have degree >= 2.
 */

// Graph parameters for Cuckaroo-29
#define EDGEBITS 29
#define NEDGES ((ulong)1 << EDGEBITS)
#define EDGEMASK (NEDGES - 1)
#define NODEBITS (EDGEBITS + 1)
#define NNODES ((ulong)1 << NODEBITS)
#define NODEMASK (NNODES - 1)

// Bucketing parameters - tuned for RDNA 4 memory hierarchy
#define XBITS 6
#define YBITS 6
#define NX (1 << XBITS)
#define NY (1 << YBITS)
#define XMASK (NX - 1)
#define YMASK (NY - 1)

// RDNA 4 specific: Wavefront size 32
#define WAVEFRONT_SIZE 32

// Local memory for counting (fits in LDS)
// 16383 * 4 = 65532 bytes, leaving room for other locals
#define COUNTERWORDS 16383

/**
 * Seed Kernel A: Generate edges and bucket by source node
 *
 * Each workgroup processes a range of nonces, generating edges and
 * storing them in buckets based on the high bits of node0.
 */
__kernel void SeedA(
    __global ulong* buffer,       // Output edge buffer
    __global uint* indexes,       // Per-bucket edge counts
    const ulong4 sipkeys,         // SipHash keys from block header
    const uint startNonce,        // Starting nonce for this kernel
    const uint noncesPerGroup     // Nonces to process per workgroup
) {
    const uint lid = get_local_id(0);
    const uint gid = get_group_id(0);
    const uint group_size = get_local_size(0);

    // Local counters for bucket sizes
    __local uint localCounts[NX];

    // Initialize local counts
    if (lid < NX) {
        localCounts[lid] = 0;
    }
    barrier(CLK_LOCAL_MEM_FENCE);

    // Process nonces assigned to this workgroup
    const uint base = startNonce + gid * noncesPerGroup;

    for (uint i = lid; i < noncesPerGroup; i += group_size) {
        ulong nonce = base + i;
        if (nonce >= NEDGES) break;

        // Generate edge using SipHash
        ulong hash0 = siphash24(sipkeys, 2 * nonce);
        ulong hash1 = siphash24(sipkeys, 2 * nonce + 1);

        uint node0 = hash0 & NODEMASK;
        uint node1 = (hash1 & NODEMASK) | 1;

        // Bucket by high bits of node0
        uint bucket = (node0 >> (NODEBITS - XBITS)) & XMASK;

        // Atomic increment local counter
        uint slot = atomic_inc(&localCounts[bucket]);

        // Store edge (packed: node0 in low 32 bits, node1 in high 32 bits)
        ulong edge = ((ulong)node1 << 32) | node0;

        // Calculate global buffer position
        uint globalSlot = atomic_inc(&indexes[gid * NX + bucket]);
        buffer[gid * NX * noncesPerGroup + bucket * noncesPerGroup + slot] = edge;
    }

    barrier(CLK_LOCAL_MEM_FENCE);
}

/**
 * Trimming Round Kernel
 *
 * Each round identifies nodes with degree < 2 and removes their edges.
 * Uses 2-bit counters packed into 32-bit words.
 *
 * RDNA 4 optimizations:
 * - Use wavefront-aligned memory access
 * - Leverage LDS for counter arrays
 * - Fine-grained memory barriers
 */
__kernel void Round(
    __global ulong* src,          // Source edge buffer
    __global ulong* dst,          // Destination edge buffer
    __global uint* srcIdx,        // Source bucket sizes
    __global uint* dstIdx,        // Destination bucket sizes
    const uint round              // Current round number
) {
    const uint lid = get_local_id(0);
    const uint gid = get_group_id(0);
    const uint group_size = get_local_size(0);

    // 2-bit counters in local memory (4 counters per byte, 16 per uint)
    __local uint counters[COUNTERWORDS];

    // Initialize counters
    for (uint i = lid; i < COUNTERWORDS; i += group_size) {
        counters[i] = 0;
    }
    barrier(CLK_LOCAL_MEM_FENCE);

    // First pass: count node degrees
    uint edgeCount = srcIdx[gid];
    __global ulong* bucket = src + gid * (NEDGES / NX);

    for (uint i = lid; i < edgeCount; i += group_size) {
        ulong edge = bucket[i];
        uint node = (round & 1) ? (uint)(edge >> 32) : (uint)edge;

        // Map node to counter position
        uint counterIdx = (node >> 4) % COUNTERWORDS;
        uint shift = (node & 0xF) * 2;

        // Atomic increment 2-bit counter (saturates at 3)
        uint mask = 3u << shift;
        uint old;
        do {
            old = counters[counterIdx];
            uint cnt = (old >> shift) & 3;
            if (cnt >= 2) break; // Already saturated
            uint newval = (old & ~mask) | ((cnt + 1) << shift);
            old = atomic_cmpxchg(&counters[counterIdx], old, newval);
        } while (old != counters[counterIdx]);
    }

    barrier(CLK_LOCAL_MEM_FENCE);

    // Second pass: copy edges where both nodes have degree >= 2
    __global ulong* outBucket = dst + gid * (NEDGES / NX);
    __local uint outCount;

    if (lid == 0) outCount = 0;
    barrier(CLK_LOCAL_MEM_FENCE);

    for (uint i = lid; i < edgeCount; i += group_size) {
        ulong edge = bucket[i];
        uint node = (round & 1) ? (uint)(edge >> 32) : (uint)edge;

        uint counterIdx = (node >> 4) % COUNTERWORDS;
        uint shift = (node & 0xF) * 2;
        uint cnt = (counters[counterIdx] >> shift) & 3;

        if (cnt >= 2) {
            uint slot = atomic_inc(&outCount);
            outBucket[slot] = edge;
        }
    }

    barrier(CLK_LOCAL_MEM_FENCE);

    if (lid == 0) {
        dstIdx[gid] = outCount;
    }
}

/**
 * Tail Kernel: Final edge consolidation
 *
 * After trimming rounds, consolidate remaining edges for CPU cycle detection.
 */
__kernel void Tail(
    __global ulong* edges,        // Edge buffer
    __global uint* indexes,       // Per-bucket edge counts
    __global ulong* output,       // Consolidated output
    __global uint* outputCount    // Total edge count
) {
    const uint gid = get_group_id(0);
    const uint lid = get_local_id(0);

    __local uint baseOffset;

    if (lid == 0) {
        baseOffset = atomic_add(outputCount, indexes[gid]);
    }
    barrier(CLK_LOCAL_MEM_FENCE);

    uint count = indexes[gid];
    __global ulong* src = edges + gid * (NEDGES / NX);

    for (uint i = lid; i < count; i += get_local_size(0)) {
        output[baseOffset + i] = src[i];
    }
}
