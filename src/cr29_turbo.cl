/**
 * CR29 Turbo Kernel - Maximum performance for RDNA 4
 * Fused operations, LDS optimization, parallel bucket processing
 */

#define ROTL64(x, n) (((x) << (n)) | ((x) >> (64 - (n))))

// Unrolled SipHash for maximum throughput
inline ulong siphash24_fast(ulong v0, ulong v1, ulong v2, ulong v3, ulong nonce) {
    v3 ^= nonce;

    // Round 1
    v0 += v1; v2 += v3; v1 = ROTL64(v1, 13); v3 = ROTL64(v3, 16);
    v1 ^= v0; v3 ^= v2; v0 = ROTL64(v0, 32);
    v2 += v1; v0 += v3; v1 = ROTL64(v1, 17); v3 = ROTL64(v3, 21);
    v1 ^= v2; v3 ^= v0; v2 = ROTL64(v2, 32);

    // Round 2
    v0 += v1; v2 += v3; v1 = ROTL64(v1, 13); v3 = ROTL64(v3, 16);
    v1 ^= v0; v3 ^= v2; v0 = ROTL64(v0, 32);
    v2 += v1; v0 += v3; v1 = ROTL64(v1, 17); v3 = ROTL64(v3, 21);
    v1 ^= v2; v3 ^= v0; v2 = ROTL64(v2, 32);

    v0 ^= nonce;
    v2 ^= 0xff;

    // Finalization rounds 1-4
    #pragma unroll 4
    for (int i = 0; i < 4; i++) {
        v0 += v1; v2 += v3; v1 = ROTL64(v1, 13); v3 = ROTL64(v3, 16);
        v1 ^= v0; v3 ^= v2; v0 = ROTL64(v0, 32);
        v2 += v1; v0 += v3; v1 = ROTL64(v1, 17); v3 = ROTL64(v3, 21);
        v1 ^= v2; v3 ^= v0; v2 = ROTL64(v2, 32);
    }

    return v0 ^ v1 ^ v2 ^ v3;
}

// Generate edges
__kernel void SeedEdges(
    __global ulong* edges,
    __global uint* counts,
    ulong4 sipkeys,
    uint edgeMask,
    uint nodeMask,
    uint xbits,
    uint maxPerBucket
) {
    uint gid = get_global_id(0);
    uint stride = get_global_size(0);
    uint bucketShift = 30 - xbits;
    uint bucketMask = (1u << xbits) - 1;

    ulong v0 = sipkeys.s0;
    ulong v1 = sipkeys.s1;
    ulong v2 = sipkeys.s2;
    ulong v3 = sipkeys.s3;

    for (uint nonce = gid; nonce <= edgeMask; nonce += stride) {
        ulong h0 = siphash24_fast(v0, v1, v2, v3, 2 * (ulong)nonce);
        ulong h1 = siphash24_fast(v0, v1, v2, v3, 2 * (ulong)nonce + 1);

        uint node0 = (uint)(h0 & nodeMask);
        uint node1 = (uint)(h1 & nodeMask) | 1;

        uint bucket = (node0 >> bucketShift) & bucketMask;
        uint slot = atomic_inc(&counts[bucket]);

        if (slot < maxPerBucket) {
            edges[bucket * maxPerBucket + slot] = ((ulong)node1 << 32) | node0;
        }
    }
}

// Global counter array size
#define GLOBAL_COUNTERS (1 << 22)  // 4M counters = 16MB

// Fused zero + count kernel - processes all edges with coalesced access
__kernel void ZeroAndCount(
    __global ulong* edges,
    __global uint* counts,
    __global uint* counters,
    uint numBuckets,
    uint maxPerBucket,
    uint nodeMask,
    uint round,
    uint counterSize
) {
    uint gid = get_global_id(0);
    uint stride = get_global_size(0);

    // Zero counters (all threads participate) - use vectorized writes
    __global uint4* counters4 = (__global uint4*)counters;
    uint counterSize4 = counterSize >> 2;
    uint4 zero4 = (uint4)(0, 0, 0, 0);
    for (uint i = gid; i < counterSize4; i += stride) {
        counters4[i] = zero4;
    }

    barrier(CLK_GLOBAL_MEM_FENCE);

    // Count degrees - process edges with better memory access pattern
    uint selectNode = round & 1;
    for (uint bucket = 0; bucket < numBuckets; bucket++) {
        uint edgeCount = counts[bucket];
        __global ulong* bucketEdges = edges + bucket * maxPerBucket;

        for (uint i = gid; i < edgeCount; i += stride) {
            ulong edge = bucketEdges[i];
            uint node = selectNode ? (uint)(edge >> 32) : (uint)edge;
            node &= nodeMask;

            uint h = node ^ (node >> 16);
            uint counterIdx = (h >> 4) & (GLOBAL_COUNTERS - 1);
            uint shift = (node & 0xF) * 2;
            atomic_add(&counters[counterIdx], 1u << shift);
        }
    }
}

// Phase 2: Trim edges using global counters
__kernel void TrimBucket(
    __global ulong* srcEdges,
    __global ulong* dstEdges,
    __global uint* srcCounts,
    __global uint* dstCounts,
    __global uint* counters,
    uint maxPerBucket,
    uint nodeMask,
    uint round
) {
    uint bucket = get_group_id(0);
    uint lid = get_local_id(0);
    uint groupSize = get_local_size(0);

    __local uint outCount;
    if (lid == 0) outCount = 0;
    barrier(CLK_LOCAL_MEM_FENCE);

    uint srcCount = srcCounts[bucket];
    __global ulong* src = srcEdges + bucket * maxPerBucket;
    __global ulong* dst = dstEdges + bucket * maxPerBucket;
    uint selectNode = round & 1;

    for (uint i = lid; i < srcCount; i += groupSize) {
        ulong edge = src[i];
        uint node = selectNode ? (uint)(edge >> 32) : (uint)edge;
        node &= nodeMask;

        // Same hash as counting
        uint h = node ^ (node >> 16);
        uint counterIdx = (h >> 4) & (GLOBAL_COUNTERS - 1);
        uint shift = (node & 0xF) * 2;
        uint deg = (counters[counterIdx] >> shift) & 3;

        if (deg >= 2) {
            uint slot = atomic_inc(&outCount);
            dst[slot] = edge;
        }
    }

    barrier(CLK_LOCAL_MEM_FENCE);

    if (lid == 0) {
        dstCounts[bucket] = outCount;
    }
}

// Consolidate all buckets
__kernel void Consolidate(
    __global ulong* edges,
    __global uint* counts,
    __global ulong* output,
    __global uint* totalCount,
    uint maxPerBucket
) {
    uint bucket = get_group_id(0);
    uint lid = get_local_id(0);

    __local uint offset;
    if (lid == 0) {
        offset = atomic_add(totalCount, counts[bucket]);
    }
    barrier(CLK_LOCAL_MEM_FENCE);

    uint count = counts[bucket];
    __global ulong* src = edges + bucket * maxPerBucket;

    for (uint i = lid; i < count; i += get_local_size(0)) {
        output[offset + i] = src[i];
    }
}
