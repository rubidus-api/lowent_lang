// low_blake3.c — BLAKE3-256, one-shot (see low_blake3.h).
#include "low_blake3.h"

#include <string.h>

#define B3_CHUNK 1024u
#define B3_BLOCK 64u

// flags
#define B3_CHUNK_START 1u
#define B3_CHUNK_END   2u
#define B3_PARENT      4u
#define B3_ROOT        8u

static const proven_u32 B3_IV[8] = {
    0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au,
    0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u,
};
static const proven_u8 B3_SIGMA[16] = { 2, 6, 3, 10, 7, 0, 4, 13, 1, 11, 12, 5, 9, 14, 15, 8 };

static proven_u32 rotr(proven_u32 x, unsigned n) { return (x >> n) | (x << (32 - n)); }

static void b3_g(proven_u32 *v, unsigned a, unsigned b, unsigned c, unsigned d,
                 proven_u32 x, proven_u32 y) {
    v[a] += v[b] + x; v[d] = rotr(v[d] ^ v[a], 16);
    v[c] += v[d];     v[b] = rotr(v[b] ^ v[c], 12);
    v[a] += v[b] + y; v[d] = rotr(v[d] ^ v[a], 8);
    v[c] += v[d];     v[b] = rotr(v[b] ^ v[c], 7);
}

// compress one 64-byte block; writes the 8-word chaining value (v[i] ^ v[i+8])
static void b3_compress(const proven_u32 h[8], const proven_u32 m_in[16],
                        proven_u64 counter, proven_u32 block_len, proven_u32 flags,
                        proven_u32 out[8]) {
    proven_u32 v[16], m[16], tmp[16];
    memcpy(v, h, 32);
    memcpy(v + 8, B3_IV, 16);
    v[12] = (proven_u32)counter;
    v[13] = (proven_u32)(counter >> 32);
    v[14] = block_len;
    v[15] = flags;
    memcpy(m, m_in, 64);
    for (unsigned r = 0; r < 7; r++) {
        b3_g(v, 0, 4,  8, 12, m[0],  m[1]);
        b3_g(v, 1, 5,  9, 13, m[2],  m[3]);
        b3_g(v, 2, 6, 10, 14, m[4],  m[5]);
        b3_g(v, 3, 7, 11, 15, m[6],  m[7]);
        b3_g(v, 0, 5, 10, 15, m[8],  m[9]);
        b3_g(v, 1, 6, 11, 12, m[10], m[11]);
        b3_g(v, 2, 7,  8, 13, m[12], m[13]);
        b3_g(v, 3, 4,  9, 14, m[14], m[15]);
        if (r + 1 < 7) {
            for (unsigned i = 0; i < 16; i++) tmp[i] = m[B3_SIGMA[i]];
            memcpy(m, tmp, 64);
        }
    }
    for (unsigned i = 0; i < 8; i++) out[i] = v[i] ^ v[i + 8];
}

static void b3_words_from_bytes(const proven_byte_t *p, proven_size_t n, proven_u32 m[16]) {
    proven_byte_t buf[B3_BLOCK] = { 0 };
    memcpy(buf, p, n);
    for (unsigned i = 0; i < 16; i++)
        m[i] = (proven_u32)buf[4 * i] | ((proven_u32)buf[4 * i + 1] << 8) |
               ((proven_u32)buf[4 * i + 2] << 16) | ((proven_u32)buf[4 * i + 3] << 24);
}

// compress a whole chunk (≤ 1 KiB) into its chaining value
static void b3_chunk_cv(const proven_byte_t *p, proven_size_t n, proven_u64 chunk_idx,
                        bool root, proven_u32 cv[8]) {
    memcpy(cv, B3_IV, 32);
    proven_size_t nblocks = n ? (n + B3_BLOCK - 1) / B3_BLOCK : 1;  // empty chunk = one empty block
    for (proven_size_t b = 0; b < nblocks; b++) {
        proven_size_t off = b * B3_BLOCK;
        proven_size_t blen = (b + 1 < nblocks) ? B3_BLOCK : n - off;
        proven_u32 m[16], flags = 0;
        b3_words_from_bytes(p + off, blen, m);
        if (b == 0) flags |= B3_CHUNK_START;
        if (b + 1 == nblocks) flags |= B3_CHUNK_END | (root ? B3_ROOT : 0);
        proven_u32 next[8];
        b3_compress(cv, m, chunk_idx, (proven_u32)blen, flags, next);
        memcpy(cv, next, 32);
    }
}

static void b3_parent(const proven_u32 l[8], const proven_u32 r[8], bool root, proven_u32 out[8]) {
    proven_u32 m[16];
    memcpy(m, l, 32);
    memcpy(m + 8, r, 32);
    b3_compress(B3_IV, m, 0, B3_BLOCK, B3_PARENT | (root ? B3_ROOT : 0), out);
}

void low_blake3_256(const proven_byte_t *data, proven_size_t len, proven_u8 out[32]) {
    proven_size_t nchunks = len ? (len + B3_CHUNK - 1) / B3_CHUNK : 1;
    proven_u32 stack[54][8];    // supports > 2^54 KiB — far beyond any input here
    proven_size_t nstack = 0;
    proven_u32 cv[8];

    for (proven_size_t i = 0; i < nchunks; i++) {
        proven_size_t off = i * B3_CHUNK;
        proven_size_t n = (i + 1 < nchunks) ? B3_CHUNK : len - off;
        b3_chunk_cv(data + off, n, (proven_u64)i, nchunks == 1, cv);
        if (i + 1 == nchunks) break;      // keep the last cv unpushed for the fold
        proven_u64 total = (proven_u64)i + 1;
        while ((total & 1u) == 0 && nstack > 0) {
            proven_u32 merged[8];
            b3_parent(stack[nstack - 1], cv, false, merged);
            nstack--;
            memcpy(cv, merged, 32);
            total >>= 1;
        }
        memcpy(stack[nstack++], cv, 32);
    }
    while (nstack > 0) {
        proven_u32 merged[8];
        b3_parent(stack[nstack - 1], cv, nstack == 1, merged);
        nstack--;
        memcpy(cv, merged, 32);
    }
    for (unsigned i = 0; i < 8; i++) {
        out[4 * i]     = (proven_u8)(cv[i]);
        out[4 * i + 1] = (proven_u8)(cv[i] >> 8);
        out[4 * i + 2] = (proven_u8)(cv[i] >> 16);
        out[4 * i + 3] = (proven_u8)(cv[i] >> 24);
    }
}
