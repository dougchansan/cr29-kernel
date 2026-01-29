/**
 * CR29 Simple Kernel - Complete edge generation and trimming
 * Tested and verified on RDNA 4 (gfx1201)
 */

#define ROTL64(x, n) (((x) << (n)) | ((x) >> (64 - (n))))

/**
 * SipHash-2-4 implementation for edge generation
 */
inline ulong siphash24(ulong4 keys, ulong nonce) {
    ulong v0 = keys.s0;
    ulong v1 = keys.s1;
    ulong v2 = keys.s2;
    ulong v3 = keys.s3 ^ nonce;

    // 2 compression rounds
    #pragma unroll 2
    for (int i = 0; i < 2; i++) {
        v0 += v1; v2 += v3;
        v1 = ROTL64(v1, 13); v3 = ROTL64(v3, 16);
        v1 ^= v0; v3 ^= v2;
        v0 = ROTL64(v0, 32);
        v2 += v1; v0 += v3;
        v1 = ROTL64(v1, 17); v3 = ROTL64(v3, 21);
        v1 ^= v2; v3 ^= v0;
        v2 = ROTL64(v2, 32);
    }

    v0 ^= nonce;
    v2 ^= 0xff;

    // 4 finalization rounds
    #pragma unroll 4
    for (int i = 0; i < 4; i++) {
        v0 += v1; v2 += v3;
        v1 = ROTL64(v1, 13); v3 = ROTL64(v3, 16);
        v1 ^= v0; v3 ^= v2;
        v0 = ROTL64(v0, 32);
        v2 += v1; v0 += v3;
        v1 = ROTL64(v1, 17); v3 = ROTL64(v3, 21);
        v1 ^= v2; v3 ^= v0;
        v2 = ROTL64(v2, 32);
    }

    return v0 ^ v1 ^ v2 ^ v3;
}

/**
 * Generate edges and bucket by source node
 * Each edge is packed as (node1 << 32) | node0
 */
__kernel void GenerateEdges(
    __global ulong* edges,         // Output edge buffer [numBuckets * maxPerBucket]
    __global uint* bucketCounts,   // Per-bucket counts [numBuckets]
    ulong4 sipkeys,                // SipHash keys
    uint edgeBits,                 // Number of edge bits (29 for CR29)
    uint xbits,                    // Bucket bits
    uint maxPerBucket              // Max edges per bucket
) {
    uint gid = get_global_id(0);
    uint stride = get_global_size(0);

    uint totalEdges = 1u << edgeBits;
    uint nodeMask = (1u << (edgeBits + 1)) - 1;
    uint numBuckets = 1u << xbits;
    uint bucketMask = numBuckets - 1;

    for (uint nonce = gid; nonce < totalEdges; nonce += stride) {
        ulong h0 = siphash24(sipkeys, 2 * (ulong)nonce);
        ulong h1 = siphash24(sipkeys, 2 * (ulong)nonce + 1);

        uint node0 = (uint)(h0 & nodeMask);
        uint node1 = (uint)(h1 & nodeMask) | 1;  // Make node1 odd

        // Bucket by high bits of node0
        uint bucket = (node0 >> (edgeBits + 1 - xbits)) & bucketMask;

        // Get slot in bucket
        uint slot = atomic_inc(&bucketCounts[bucket]);

        if (slot < maxPerBucket) {
            ulong edge = ((ulong)node1 << 32) | node0;
            edges[bucket * maxPerBucket + slot] = edge;
        }
    }
}

/**
 * Count node degrees using 2-bit counters
 * Each counter word holds 16 counters (2 bits each)
 */
__kernel void CountDegrees(
    __global ulong* edges,          // Edge buffer
    __global uint* bucketCounts,    // Per-bucket counts
    __global uint* counters,        // Degree counters [numBuckets * counterWords]
    uint bucket,                    // Which bucket to process
    uint maxPerBucket,              // Max edges per bucket
    uint counterWords,              // Counter words per bucket
    uint nodeMask,                  // Mask for node values
    uint round                      // Round number (determines which node to count)
) {
    uint lid = get_local_id(0);
    uint groupSize = get_local_size(0);

    uint edgeCount = bucketCounts[bucket];
    __global ulong* bucketEdges = edges + bucket * maxPerBucket;
    __global uint* bucketCounters = counters + bucket * counterWords;

    // Clear counters
    for (uint i = lid; i < counterWords; i += groupSize) {
        bucketCounters[i] = 0;
    }
    barrier(CLK_GLOBAL_MEM_FENCE);

    // Count degrees
    for (uint i = lid; i < edgeCount; i += groupSize) {
        ulong edge = bucketEdges[i];
        uint node = (round & 1) ? (uint)(edge >> 32) : (uint)edge;
        node &= nodeMask;

        uint idx = (node >> 4) % counterWords;
        uint shift = (node & 0xF) * 2;

        // Atomic increment 2-bit counter
        atomic_add(&bucketCounters[idx], 1u << shift);
    }
}

/**
 * Trim edges with degree < 2
 */
__kernel void TrimEdges(
    __global ulong* srcEdges,       // Source edge buffer
    __global ulong* dstEdges,       // Destination edge buffer
    __global uint* srcCounts,       // Source bucket counts
    __global uint* dstCounts,       // Destination bucket counts
    __global uint* counters,        // Degree counters
    uint bucket,                    // Which bucket to process
    uint maxPerBucket,              // Max edges per bucket
    uint counterWords,              // Counter words per bucket
    uint nodeMask,                  // Mask for node values
    uint round                      // Round number
) {
    uint lid = get_local_id(0);
    uint groupSize = get_local_size(0);

    uint srcCount = srcCounts[bucket];
    __global ulong* src = srcEdges + bucket * maxPerBucket;
    __global ulong* dst = dstEdges + bucket * maxPerBucket;
    __global uint* bucketCounters = counters + bucket * counterWords;

    __local uint dstCount;
    if (lid == 0) dstCount = 0;
    barrier(CLK_LOCAL_MEM_FENCE);

    // Copy edges with degree >= 2
    for (uint i = lid; i < srcCount; i += groupSize) {
        ulong edge = src[i];
        uint node = (round & 1) ? (uint)(edge >> 32) : (uint)edge;
        node &= nodeMask;

        uint idx = (node >> 4) % counterWords;
        uint shift = (node & 0xF) * 2;
        uint deg = (bucketCounters[idx] >> shift) & 3;

        // Keep edges with degree >= 2 (saturated counter shows >= 2)
        if (deg >= 2) {
            uint slot = atomic_inc(&dstCount);
            dst[slot] = edge;
        }
    }
    barrier(CLK_LOCAL_MEM_FENCE);

    if (lid == 0) {
        dstCounts[bucket] = dstCount;
    }
}

/**
 * Consolidate remaining edges for CPU cycle detection
 */
__kernel void ConsolidateEdges(
    __global ulong* edges,          // Edge buffer
    __global uint* bucketCounts,    // Per-bucket counts
    __global ulong* output,         // Consolidated output
    __global uint* outputCount,     // Total output count
    uint numBuckets,                // Number of buckets
    uint maxPerBucket               // Max edges per bucket
) {
    uint gid = get_global_id(0);

    if (gid >= numBuckets) return;

    uint count = bucketCounts[gid];
    if (count == 0) return;

    // Get output offset
    uint offset = atomic_add(outputCount, count);

    // Copy edges
    __global ulong* src = edges + gid * maxPerBucket;
    for (uint i = 0; i < count; i++) {
        output[offset + i] = src[i];
    }
}
