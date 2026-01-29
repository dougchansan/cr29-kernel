/**
 * CR29 Mean Trimmer - Global degree counting
 * Uses bitmap for degree tracking across all nodes
 */

#define ROTL64(x, n) (((x) << (n)) | ((x) >> (64 - (n))))

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

// Generate edges into buckets
__kernel void GenerateEdges(
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
    uint bucketMask = (1u << xbits) - 1;

    for (uint nonce = gid; nonce <= edgeMask; nonce += stride) {
        ulong h0 = siphash24(sipkeys, 2 * (ulong)nonce);
        ulong h1 = siphash24(sipkeys, 2 * (ulong)nonce + 1);

        uint node0 = (uint)(h0 & nodeMask);
        uint node1 = (uint)(h1 & nodeMask) | 1;

        // Bucket by node0's top bits
        uint bucket = (node0 >> 24) & bucketMask;

        uint slot = atomic_inc(&counts[bucket]);
        if (slot < maxPerBucket) {
            edges[bucket * maxPerBucket + slot] = ((ulong)node1 << 32) | node0;
        }
    }
}

// Count degrees globally using atomic bitmap
// Each bit pair represents a 2-bit counter
__kernel void CountDegrees(
    __global ulong* edges,
    __global uint* counts,
    __global uint* degrees,    // Global degree counters (2-bit each)
    uint numBuckets,
    uint maxPerBucket,
    uint degreeSize,           // Size of degree array
    uint round
) {
    uint gid = get_global_id(0);
    uint stride = get_global_size(0);

    uint totalEdges = 0;
    for (uint b = 0; b < numBuckets; b++) {
        totalEdges += counts[b];
    }

    // Process edges in parallel
    uint edgeIdx = gid;
    while (edgeIdx < totalEdges) {
        // Find which bucket and slot this edge is in
        uint bucket = 0;
        uint offset = edgeIdx;
        for (uint b = 0; b < numBuckets; b++) {
            if (offset < counts[b]) {
                bucket = b;
                break;
            }
            offset -= counts[b];
        }

        ulong edge = edges[bucket * maxPerBucket + offset];
        uint node = (round & 1) ? (uint)(edge >> 32) : (uint)edge;

        // Increment 2-bit counter
        uint idx = (node >> 4) % degreeSize;
        uint shift = (node & 0xF) * 2;
        atomic_add(&degrees[idx], 1u << shift);

        edgeIdx += stride;
    }
}

// Trim edges with degree < 2
__kernel void TrimEdges(
    __global ulong* srcEdges,
    __global ulong* dstEdges,
    __global uint* srcCounts,
    __global uint* dstCounts,
    __global uint* degrees,
    uint bucket,
    uint maxPerBucket,
    uint degreeSize,
    uint round
) {
    uint lid = get_local_id(0);
    uint groupSize = get_local_size(0);

    uint srcCount = srcCounts[bucket];
    __global ulong* src = srcEdges + bucket * maxPerBucket;
    __global ulong* dst = dstEdges + bucket * maxPerBucket;

    __local uint dstCount;
    if (lid == 0) dstCount = 0;
    barrier(CLK_LOCAL_MEM_FENCE);

    for (uint i = lid; i < srcCount; i += groupSize) {
        ulong edge = src[i];
        uint node = (round & 1) ? (uint)(edge >> 32) : (uint)edge;

        uint idx = (node >> 4) % degreeSize;
        uint shift = (node & 0xF) * 2;
        uint deg = (degrees[idx] >> shift) & 3;

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

// Consolidate
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
