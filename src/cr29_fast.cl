/**
 * CR29 Fast Kernel - Optimized for RDNA 4 (gfx1201)
 * Uses fused operations and parallel bucket processing
 */

#define ROTL64(x, n) (((x) << (n)) | ((x) >> (64 - (n))))

// SipHash-2-4 optimized for RDNA 4
inline ulong siphash24(ulong4 keys, ulong nonce) {
    ulong v0 = keys.s0;
    ulong v1 = keys.s1;
    ulong v2 = keys.s2;
    ulong v3 = keys.s3 ^ nonce;

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

// Seeding kernel - generate edges and bucket by source node
// Uses all threads globally, bucketing by node0's high bits
__kernel void Seed(
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
    uint numBuckets = 1u << xbits;
    uint bucketMask = numBuckets - 1;

    for (uint nonce = gid; nonce <= edgeMask; nonce += stride) {
        ulong h0 = siphash24(sipkeys, 2 * (ulong)nonce);
        ulong h1 = siphash24(sipkeys, 2 * (ulong)nonce + 1);

        uint node0 = (uint)(h0 & nodeMask);
        uint node1 = (uint)(h1 & nodeMask) | 1;

        // Bucket by high bits of node0 (node0 has NODEBITS=30 bits)
        // For XBITS=6, we want the top 6 bits, so shift by 30-6=24
        uint bucket = (node0 >> 24) & bucketMask;

        // Get slot in bucket
        uint slot = atomic_inc(&counts[bucket]);

        if (slot < maxPerBucket) {
            edges[bucket * maxPerBucket + slot] = ((ulong)node1 << 32) | node0;
        }
    }
}

// Local memory counters for trimming (must fit in 64KB LDS)
// 16383 * 4 = 65532 bytes, leaving room for local variables
#define COUNTER_SIZE 16383

// Count degrees for a single bucket
// Uses per-bucket counter region
__kernel void CountDegrees(
    __global ulong* edges,
    __global uint* counts,
    __global uint* counters,       // Global counter array [numBuckets * countersPerBucket]
    uint numBuckets,
    uint maxPerBucket,
    uint countersPerBucket,        // Counters per bucket
    uint nodeMask,
    uint round
) {
    uint bucket = get_group_id(0);
    uint lid = get_local_id(0);
    uint groupSize = get_local_size(0);

    uint edgeCount = counts[bucket];
    __global ulong* bucketEdges = edges + bucket * maxPerBucket;
    __global uint* bucketCounters = counters + bucket * countersPerBucket;

    // Clear counters for this bucket
    for (uint i = lid; i < countersPerBucket; i += groupSize) {
        bucketCounters[i] = 0;
    }
    barrier(CLK_GLOBAL_MEM_FENCE);

    // Count degrees
    for (uint i = lid; i < edgeCount; i += groupSize) {
        ulong edge = bucketEdges[i];
        uint node = (round & 1) ? (uint)(edge >> 32) : (uint)edge;
        node &= nodeMask;

        uint idx = (node >> 4) % countersPerBucket;
        uint shift = (node & 0xF) * 2;

        atomic_add(&bucketCounters[idx], 1u << shift);
    }
}

// Trim edges with degree < 2
// Each workgroup processes one bucket
__kernel void Trim(
    __global ulong* srcEdges,
    __global ulong* dstEdges,
    __global uint* srcCounts,
    __global uint* dstCounts,
    __global uint* counters,       // Global counter array [numBuckets * countersPerBucket]
    uint bucket,
    uint maxPerBucket,
    uint countersPerBucket,
    uint nodeMask,
    uint round
) {
    uint lid = get_local_id(0);
    uint groupSize = get_local_size(0);

    uint srcCount = srcCounts[bucket];
    __global ulong* src = srcEdges + bucket * maxPerBucket;
    __global ulong* dst = dstEdges + bucket * maxPerBucket;
    __global uint* bucketCounters = counters + bucket * countersPerBucket;

    __local uint dstCount;
    if (lid == 0) dstCount = 0;
    barrier(CLK_LOCAL_MEM_FENCE);

    for (uint i = lid; i < srcCount; i += groupSize) {
        ulong edge = src[i];
        uint node = (round & 1) ? (uint)(edge >> 32) : (uint)edge;
        node &= nodeMask;

        uint idx = (node >> 4) % countersPerBucket;
        uint shift = (node & 0xF) * 2;
        uint deg = (bucketCounters[idx] >> shift) & 3;

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

// Consolidate edges for cycle detection
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
