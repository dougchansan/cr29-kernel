/**
 * RDNA 4 Optimized Cuckaroo-29 Kernels
 * Specifically tuned for RX 9070 XT (gfx1201)
 *
 * Architecture specs:
 * - 32 Compute Units (16 WGPs)
 * - 64KB LDS per CU
 * - 16GB VRAM
 * - Wavefront size 32
 * - L1 cache: 32KB per shader array
 * - L2 cache: 4MB shared
 */

// RX 9070 XT specific tuning
#define RDNA4_CU_COUNT 32
#define RDNA4_LDS_SIZE 65536
#define RDNA4_WAVEFRONT 32
#define RDNA4_MAX_WORKGROUP 256

// Optimal bucketing for 32 CUs
// Use 32 buckets so each CU handles one bucket
#define XBITS_OPT 5
#define YBITS_OPT 5
#define NX_OPT (1 << XBITS_OPT)  // 32 buckets
#define NY_OPT (1 << YBITS_OPT)

// Memory layout optimized for RDNA 4 cache hierarchy
// L1 cache line: 128 bytes (32 dwords)
// Align data structures to cache lines
#define CACHE_LINE_SIZE 128
#define CACHE_LINE_DWORDS 32

// 2-bit counters: 16 counters per uint32
// For 2^30 nodes, need 2^30/16 = 64M counter words
// But we process in buckets, so each bucket needs 2^30/32/16 = 2M words
// That's 8MB per bucket - too large for LDS
// Use multiple passes with smaller counter arrays
#define COUNTERS_PER_PASS 16384  // 64KB / 4 = 16K dwords = 256K counters per pass
#define NODES_PER_PASS (COUNTERS_PER_PASS * 16)

/**
 * Optimized SipHash for RDNA 4
 * Uses vector operations where beneficial
 */
inline ulong siphash24_rdna4(const ulong4 keys, const ulong nonce) {
    ulong v0 = keys.s0;
    ulong v1 = keys.s1;
    ulong v2 = keys.s2;
    ulong v3 = keys.s3 ^ nonce;

    // Unrolled compression rounds
    // Round 1
    v0 += v1; v2 += v3;
    v1 = rotate(v1, 13UL); v3 = rotate(v3, 16UL);
    v1 ^= v0; v3 ^= v2;
    v0 = rotate(v0, 32UL);

    v2 += v1; v0 += v3;
    v1 = rotate(v1, 17UL); v3 = rotate(v3, 21UL);
    v1 ^= v2; v3 ^= v0;
    v2 = rotate(v2, 32UL);

    // Round 2
    v0 += v1; v2 += v3;
    v1 = rotate(v1, 13UL); v3 = rotate(v3, 16UL);
    v1 ^= v0; v3 ^= v2;
    v0 = rotate(v0, 32UL);

    v2 += v1; v0 += v3;
    v1 = rotate(v1, 17UL); v3 = rotate(v3, 21UL);
    v1 ^= v2; v3 ^= v0;
    v2 = rotate(v2, 32UL);

    v0 ^= nonce;
    v2 ^= 0xff;

    // Finalization (4 rounds)
    #pragma unroll
    for (int i = 0; i < 4; i++) {
        v0 += v1; v2 += v3;
        v1 = rotate(v1, 13UL); v3 = rotate(v3, 16UL);
        v1 ^= v0; v3 ^= v2;
        v0 = rotate(v0, 32UL);

        v2 += v1; v0 += v3;
        v1 = rotate(v1, 17UL); v3 = rotate(v3, 21UL);
        v1 ^= v2; v3 ^= v0;
        v2 = rotate(v2, 32UL);
    }

    return v0 ^ v1 ^ v2 ^ v3;
}

/**
 * Edge generation kernel - RDNA 4 optimized
 *
 * Uses wavefront-coalesced memory access patterns
 * Each wavefront (32 threads) generates 32 consecutive edges
 */
__attribute__((reqd_work_group_size(256, 1, 1)))
__kernel void SeedA_RDNA4(
    __global ulong* restrict edges,
    __global uint* restrict bucketCounts,
    const ulong4 sipkeys,
    const uint edgeBits
) {
    const uint lid = get_local_id(0);
    const uint gid = get_global_id(0);
    const uint groupId = get_group_id(0);
    const uint numGroups = get_num_groups(0);

    const uint edgeMask = (1u << edgeBits) - 1;
    const uint nodeMask = (1u << (edgeBits + 1)) - 1;

    // LDS for local bucket counts
    __local uint localCounts[NX_OPT];
    __local uint localBase[NX_OPT];

    // Initialize local counts
    if (lid < NX_OPT) {
        localCounts[lid] = 0;
    }
    barrier(CLK_LOCAL_MEM_FENCE);

    // Each thread processes multiple nonces
    const ulong totalEdges = 1UL << edgeBits;
    const ulong edgesPerGroup = totalEdges / numGroups;
    const ulong baseNonce = groupId * edgesPerGroup;

    // Process edges in wavefront-aligned chunks
    for (uint i = lid; i < edgesPerGroup; i += 256) {
        ulong nonce = baseNonce + i;

        // Generate edge endpoints
        ulong h0 = siphash24_rdna4(sipkeys, 2 * nonce);
        ulong h1 = siphash24_rdna4(sipkeys, 2 * nonce + 1);

        uint node0 = h0 & nodeMask;
        uint node1 = (h1 & nodeMask) | 1;  // Ensure odd

        // Determine bucket from high bits of node0
        uint bucket = node0 >> (edgeBits + 1 - XBITS_OPT);

        // Pack edge: low 32 bits = node0, high 32 bits = node1
        ulong edge = ((ulong)node1 << 32) | node0;

        // Atomic increment local counter
        uint slot = atomic_inc(&localCounts[bucket]);

        // We'll compute global positions after the barrier
    }

    barrier(CLK_LOCAL_MEM_FENCE);

    // Get global base offsets for each bucket
    if (lid < NX_OPT) {
        localBase[lid] = atomic_add(&bucketCounts[groupId * NX_OPT + lid], localCounts[lid]);
    }
    barrier(CLK_LOCAL_MEM_FENCE);

    // Second pass: actually store edges
    // Reset local counts
    if (lid < NX_OPT) {
        localCounts[lid] = 0;
    }
    barrier(CLK_LOCAL_MEM_FENCE);

    for (uint i = lid; i < edgesPerGroup; i += 256) {
        ulong nonce = baseNonce + i;

        ulong h0 = siphash24_rdna4(sipkeys, 2 * nonce);
        ulong h1 = siphash24_rdna4(sipkeys, 2 * nonce + 1);

        uint node0 = h0 & nodeMask;
        uint node1 = (h1 & nodeMask) | 1;

        uint bucket = node0 >> (edgeBits + 1 - XBITS_OPT);
        ulong edge = ((ulong)node1 << 32) | node0;

        uint localSlot = atomic_inc(&localCounts[bucket]);
        uint globalSlot = localBase[bucket] + localSlot;

        // Store to global memory with coalesced access
        edges[bucket * (totalEdges / NX_OPT) + globalSlot] = edge;
    }
}

/**
 * Trimming round kernel - RDNA 4 optimized
 *
 * Uses LDS for 2-bit counters
 * Processes nodes in passes to fit counters in LDS
 */
__attribute__((reqd_work_group_size(256, 1, 1)))
__kernel void TrimRound_RDNA4(
    __global ulong* restrict srcEdges,
    __global ulong* restrict dstEdges,
    __global uint* restrict srcCounts,
    __global uint* restrict dstCounts,
    const uint bucket,
    const uint round
) {
    const uint lid = get_local_id(0);

    // 2-bit counters in LDS (16 counters per uint)
    __local uint counters[COUNTERS_PER_PASS];

    const uint srcCount = srcCounts[bucket];
    __global ulong* src = srcEdges + bucket * (1 << 24);  // Max edges per bucket
    __global ulong* dst = dstEdges + bucket * (1 << 24);

    __local uint dstCount;
    if (lid == 0) dstCount = 0;

    // Process in passes due to LDS size limit
    const uint nodeShift = (round & 1) ? 32 : 0;  // Alternate between node0 and node1

    uint passCount = ((1 << 30) + NODES_PER_PASS - 1) / NODES_PER_PASS;

    for (uint pass = 0; pass < passCount; pass++) {
        uint nodeBase = pass * NODES_PER_PASS;

        // Clear counters
        for (uint i = lid; i < COUNTERS_PER_PASS; i += 256) {
            counters[i] = 0;
        }
        barrier(CLK_LOCAL_MEM_FENCE);

        // First pass: count degrees
        for (uint i = lid; i < srcCount; i += 256) {
            ulong edge = src[i];
            uint node = (uint)(edge >> nodeShift) & 0x3FFFFFFF;

            // Check if node is in this pass's range
            if (node >= nodeBase && node < nodeBase + NODES_PER_PASS) {
                uint localNode = node - nodeBase;
                uint counterIdx = localNode >> 4;
                uint shift = (localNode & 0xF) * 2;

                // Increment 2-bit counter (saturate at 2)
                uint mask = 3u << shift;
                uint old = atomic_or(&counters[counterIdx], 0);  // Read current
                uint cnt = (old >> shift) & 3;
                if (cnt < 2) {
                    atomic_add(&counters[counterIdx], 1u << shift);
                }
            }
        }
        barrier(CLK_LOCAL_MEM_FENCE);

        // Second pass: copy edges with degree >= 2
        for (uint i = lid; i < srcCount; i += 256) {
            ulong edge = src[i];
            uint node = (uint)(edge >> nodeShift) & 0x3FFFFFFF;

            if (node >= nodeBase && node < nodeBase + NODES_PER_PASS) {
                uint localNode = node - nodeBase;
                uint counterIdx = localNode >> 4;
                uint shift = (localNode & 0xF) * 2;

                uint cnt = (counters[counterIdx] >> shift) & 3;
                if (cnt >= 2) {
                    uint slot = atomic_inc(&dstCount);
                    dst[slot] = edge;
                }
            }
        }
        barrier(CLK_LOCAL_MEM_FENCE);
    }

    if (lid == 0) {
        dstCounts[bucket] = dstCount;
    }
}

/**
 * Recovery kernel - find original nonces for cycle edges
 */
__attribute__((reqd_work_group_size(256, 1, 1)))
__kernel void Recovery_RDNA4(
    __global const uint* restrict cycleEdges,  // 42 edges to recover
    __global uint* restrict nonces,            // Output: recovered nonces
    const ulong4 sipkeys,
    const uint edgeBits
) {
    const uint gid = get_global_id(0);
    const ulong totalEdges = 1UL << edgeBits;
    const uint nodeMask = (1u << (edgeBits + 1)) - 1;

    // Each thread checks a range of nonces
    const ulong noncesPerThread = totalEdges / get_global_size(0);
    const ulong baseNonce = gid * noncesPerThread;

    for (ulong n = 0; n < noncesPerThread; n++) {
        ulong nonce = baseNonce + n;

        ulong h0 = siphash24_rdna4(sipkeys, 2 * nonce);
        ulong h1 = siphash24_rdna4(sipkeys, 2 * nonce + 1);

        uint node0 = h0 & nodeMask;
        uint node1 = (h1 & nodeMask) | 1;

        // Check against all 42 cycle edges
        #pragma unroll 1
        for (int i = 0; i < 42; i++) {
            uint cycleNode0 = cycleEdges[i * 2];
            uint cycleNode1 = cycleEdges[i * 2 + 1];

            if (node0 == cycleNode0 && node1 == cycleNode1) {
                nonces[i] = (uint)nonce;
            }
        }
    }
}
