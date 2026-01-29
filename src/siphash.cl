/**
 * SipHash-2-4 Implementation for OpenCL
 * Optimized for AMD RDNA 4 (gfx1201)
 *
 * SipHash generates edges for the Cuckoo Cycle graph from nonces.
 */

#define ROTL64(x, n) (((x) << (n)) | ((x) >> (64 - (n))))

#define SIPROUND \
    v0 += v1; v2 += v3; \
    v1 = ROTL64(v1, 13); v3 = ROTL64(v3, 16); \
    v1 ^= v0; v3 ^= v2; \
    v0 = ROTL64(v0, 32); \
    v2 += v1; v0 += v3; \
    v1 = ROTL64(v1, 17); v3 = ROTL64(v3, 21); \
    v1 ^= v2; v3 ^= v0; \
    v2 = ROTL64(v2, 32);

/**
 * SipHash-2-4 core function
 * Returns 64-bit hash of the input nonce
 */
inline ulong siphash24(const ulong4 keys, const ulong nonce) {
    ulong v0 = keys.s0;
    ulong v1 = keys.s1;
    ulong v2 = keys.s2;
    ulong v3 = keys.s3;

    v3 ^= nonce;

    // 2 compression rounds
    SIPROUND
    SIPROUND

    v0 ^= nonce;
    v2 ^= 0xff;

    // 4 finalization rounds
    SIPROUND
    SIPROUND
    SIPROUND
    SIPROUND

    return v0 ^ v1 ^ v2 ^ v3;
}

/**
 * Generate a single edge from a nonce
 * Edge connects node0 (even bucket) to node1 (odd bucket)
 */
inline ulong2 make_edge(const ulong4 keys, const ulong nonce, const uint edgemask) {
    ulong hash = siphash24(keys, 2 * nonce);
    ulong hash1 = siphash24(keys, 2 * nonce + 1);

    ulong2 edge;
    edge.x = hash & edgemask;      // node0 (even)
    edge.y = (hash1 & edgemask) | 1; // node1 (odd)

    return edge;
}

/**
 * Vectorized edge generation - process 4 nonces at once
 * Optimized for RDNA 4 wavefront size 32
 */
inline void make_edges_x4(
    const ulong4 keys,
    const ulong base_nonce,
    const uint edgemask,
    ulong2* edges
) {
    #pragma unroll
    for (int i = 0; i < 4; i++) {
        edges[i] = make_edge(keys, base_nonce + i, edgemask);
    }
}
