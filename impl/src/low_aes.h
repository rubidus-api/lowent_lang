/* low_aes.h — AES-128-CTR 과 GHASH 의 **한 구현**, 두 뒤끝이 나눠 쓴다.
 *
 * ★★★★ 왜 잎인가 (X-0043 ⓑ, 소유자 지시 2026-09-20)
 *   이 언어로 쓴 AES 는 실측 **0.6 MB/s**, GHASH 는 **4.1 MB/s** 였다(4 MiB, 네이티브 -O2).
 *   그래서 AES-128-GCM 전체가 0.5 MB/s 였고, 같은 기계의 OpenSSL 은 4825 MB/s 다.
 *   ChaCha20-Poly1305 로 도망칠 수는 있었지만(X-0043 ⓐ, 그것은 이미 했다), **서버가 AES 만
 *   받는 자리**가 남는다. 그 자리에서 이 언어는 못 쓸 물건이었다.
 *   ⇒ 「빌트인 op 증가 0」 규율을 여기서 한 번 연다. 소유자가 그렇게 정했다.
 *
 * ★★★ 무엇을 잎으로 내리고 무엇을 안 내렸나 — **구성은 남는다.**
 *   내린 것: 라운드 함수(AES)와 GF(2^128) 곱(GHASH). 둘 다 «기계가 잘하는 계산» 이다.
 *   안 내린 것: GCM 의 **짜임** — J0 를 만들고, 태그를 **먼저 검증하고 나서** 복호하고,
 *   길이 블록을 비트로 세는 규율은 `lib/gcm.low` 에 남는다. 그것이 골든이 재는 자리다.
 *   ☞ AEAD 통째로 내리면 그 규율이 C 안으로 숨는다. 숨은 규율은 검사할 수 없다.
 *
 * ★★ 두 뒤끝이 **같은 글자**를 쓴다: VM 은 이 헤더를 컴파일해 넣고, C 방출기는 같은 글을
 *   문자열로 실어 보낸다(`LOW_AES_C_SOURCE`). 나뉘어 있으면 언젠가 갈린다 —
 *   `low_sha512.h` 가 세운 모양 그대로다.
 */
#ifndef LOW_AES_H
#define LOW_AES_H

#define LOW_AES_BODY                                                                           \
static const unsigned char lw_aes_sbox[256] = {                                                \
0x63,0x7c,0x77,0x7b,0xf2,0x6b,0x6f,0xc5,0x30,0x01,0x67,0x2b,0xfe,0xd7,0xab,0x76,               \
0xca,0x82,0xc9,0x7d,0xfa,0x59,0x47,0xf0,0xad,0xd4,0xa2,0xaf,0x9c,0xa4,0x72,0xc0,               \
0xb7,0xfd,0x93,0x26,0x36,0x3f,0xf7,0xcc,0x34,0xa5,0xe5,0xf1,0x71,0xd8,0x31,0x15,               \
0x04,0xc7,0x23,0xc3,0x18,0x96,0x05,0x9a,0x07,0x12,0x80,0xe2,0xeb,0x27,0xb2,0x75,               \
0x09,0x83,0x2c,0x1a,0x1b,0x6e,0x5a,0xa0,0x52,0x3b,0xd6,0xb3,0x29,0xe3,0x2f,0x84,               \
0x53,0xd1,0x00,0xed,0x20,0xfc,0xb1,0x5b,0x6a,0xcb,0xbe,0x39,0x4a,0x4c,0x58,0xcf,               \
0xd0,0xef,0xaa,0xfb,0x43,0x4d,0x33,0x85,0x45,0xf9,0x02,0x7f,0x50,0x3c,0x9f,0xa8,               \
0x51,0xa3,0x40,0x8f,0x92,0x9d,0x38,0xf5,0xbc,0xb6,0xda,0x21,0x10,0xff,0xf3,0xd2,               \
0xcd,0x0c,0x13,0xec,0x5f,0x97,0x44,0x17,0xc4,0xa7,0x7e,0x3d,0x64,0x5d,0x19,0x73,               \
0x60,0x81,0x4f,0xdc,0x22,0x2a,0x90,0x88,0x46,0xee,0xb8,0x14,0xde,0x5e,0x0b,0xdb,               \
0xe0,0x32,0x3a,0x0a,0x49,0x06,0x24,0x5c,0xc2,0xd3,0xac,0x62,0x91,0x95,0xe4,0x79,               \
0xe7,0xc8,0x37,0x6d,0x8d,0xd5,0x4e,0xa9,0x6c,0x56,0xf4,0xea,0x65,0x7a,0xae,0x08,               \
0xba,0x78,0x25,0x2e,0x1c,0xa6,0xb4,0xc6,0xe8,0xdd,0x74,0x1f,0x4b,0xbd,0x8b,0x8a,               \
0x70,0x3e,0xb5,0x66,0x48,0x03,0xf6,0x0e,0x61,0x35,0x57,0xb9,0x86,0xc1,0x1d,0x9e,               \
0xe1,0xf8,0x98,0x11,0x69,0xd9,0x8e,0x94,0x9b,0x1e,0x87,0xe9,0xce,0x55,0x28,0xdf,               \
0x8c,0xa1,0x89,0x0d,0xbf,0xe6,0x42,0x68,0x41,0x99,0x2d,0x0f,0xb0,0x54,0xbb,0x16 };             \
static unsigned char lw_aes_xt(unsigned char a) {                                              \
    return (unsigned char)((a << 1) ^ ((a & 0x80) ? 0x1b : 0));                                 \
}                                                                                               \
/* AES-128 키 일정: 16 바이트 키 → 11 라운드 키(176 바이트). */                                \
static void lw_aes_expand(const unsigned char *k, unsigned char *rk) {                          \
    unsigned char rcon = 1; int i;                                                              \
    for (i = 0; i < 16; i++) rk[i] = k[i];                                                       \
    for (i = 1; i < 11; i++) {                                                                   \
        unsigned char *p = rk + i * 16, *q = p - 16;                                              \
        unsigned char t0 = q[13], t1 = q[14], t2 = q[15], t3 = q[12];                             \
        p[0] = (unsigned char)(q[0] ^ lw_aes_sbox[t0] ^ rcon);                                    \
        p[1] = (unsigned char)(q[1] ^ lw_aes_sbox[t1]);                                           \
        p[2] = (unsigned char)(q[2] ^ lw_aes_sbox[t2]);                                           \
        p[3] = (unsigned char)(q[3] ^ lw_aes_sbox[t3]);                                           \
        { int j; for (j = 4; j < 16; j++) p[j] = (unsigned char)(q[j] ^ p[j - 4]); }              \
        rcon = lw_aes_xt(rcon);                                                                   \
    }                                                                                              \
}                                                                                                  \
/* 한 블록 암호화(제자리). 열 우선 상태가 아니라 **바이트 순서 그대로** 쓴다 —                 \
   `lib/aes.low` 이 그렇게 쓰고 있었고, 두 구현이 같은 답을 내야 한다. */                       \
static void lw_aes_block(const unsigned char *rk, unsigned char *s) {                            \
    int r, i;                                                                                     \
    for (i = 0; i < 16; i++) s[i] ^= rk[i];                                                        \
    for (r = 1; r <= 10; r++) {                                                                    \
        unsigned char t[16];                                                                        \
        for (i = 0; i < 16; i++) t[i] = lw_aes_sbox[s[i]];                                           \
        { unsigned char a;                                                                           \
          a = t[1];  t[1]  = t[5];  t[5]  = t[9];  t[9]  = t[13]; t[13] = a;                          \
          a = t[2];  t[2]  = t[10]; t[10] = a; a = t[6]; t[6] = t[14]; t[14] = a;                     \
          a = t[15]; t[15] = t[11]; t[11] = t[7];  t[7]  = t[3];  t[3]  = a; }                        \
        if (r != 10) {                                                                                \
            for (i = 0; i < 16; i += 4) {                                                              \
                unsigned char a0 = t[i], a1 = t[i+1], a2 = t[i+2], a3 = t[i+3];                         \
                unsigned char x = (unsigned char)(a0 ^ a1 ^ a2 ^ a3);                                   \
                t[i]   = (unsigned char)(a0 ^ x ^ lw_aes_xt((unsigned char)(a0 ^ a1)));                 \
                t[i+1] = (unsigned char)(a1 ^ x ^ lw_aes_xt((unsigned char)(a1 ^ a2)));                 \
                t[i+2] = (unsigned char)(a2 ^ x ^ lw_aes_xt((unsigned char)(a2 ^ a3)));                 \
                t[i+3] = (unsigned char)(a3 ^ x ^ lw_aes_xt((unsigned char)(a3 ^ a0)));                 \
            }                                                                                            \
        }                                                                                                 \
        for (i = 0; i < 16; i++) s[i] = (unsigned char)(t[i] ^ rk[r * 16 + i]);                            \
    }                                                                                                       \
}                                                                                                            \
/* (key16, ctr16 mut, src, dst) → 처리한 바이트 수. 0 = 거절(길이가 안 맞는다).                 \
   카운터는 **마지막 4 바이트를 빅엔디언으로** 올린다(GCM 규약). */                             \
static long long lw_aes_ctr(const void *keyp, unsigned long long klen,                            \
                            void *ctrp, unsigned long long clen,                                   \
                            const void *srcp, unsigned long long slen,                              \
                            void *dstp, unsigned long long dlen) {                                   \
    const unsigned char *key = (const unsigned char *)keyp;                                           \
    unsigned char *ctr = (unsigned char *)ctrp;                                                        \
    const unsigned char *src = (const unsigned char *)srcp;                                             \
    unsigned char *dst = (unsigned char *)dstp;                                                          \
    unsigned char rk[176], ks[16];                                                                        \
    unsigned long long off;                                                                                \
    if (klen != 16 || clen != 16 || dlen < slen) return 0;                                                  \
    lw_aes_expand(key, rk);                                                                                  \
    for (off = 0; off < slen; off += 16) {                                                                    \
        unsigned long long n = slen - off, i; unsigned int c;                                                  \
        if (n > 16) n = 16;                                                                                     \
        for (i = 0; i < 16; i++) ks[i] = ctr[i];                                                                 \
        lw_aes_block(rk, ks);                                                                                     \
        for (i = 0; i < n; i++) dst[off + i] = (unsigned char)(src[off + i] ^ ks[i]);                              \
        c = (unsigned int)ctr[15] + 1u; ctr[15] = (unsigned char)c;                                                 \
        if (c >> 8) { c = (unsigned int)ctr[14] + 1u; ctr[14] = (unsigned char)c;                                    \
        if (c >> 8) { c = (unsigned int)ctr[13] + 1u; ctr[13] = (unsigned char)c;                                     \
        if (c >> 8) { ctr[12] = (unsigned char)(ctr[12] + 1); } } }                                                    \
    }                                                                                                                   \
    return (long long)slen;                                                                                              \
}                                                                                                                         \
/* ★★★ **AES 한 라운드** (RFC-0119 §10, 2026-09-23) — FIPS-197 의 라운드 하나.                 \
   `last` 면 MixColumns 를 건너뛴다(마지막 라운드). 상태 16 바이트를 제자리에서 고친다.         \
   ☞ 이 셈은 **S-box 표를 읽는다** — 상수시간이 아니다. 기계 명령(`aesenc`)은 표를 안 읽으므로 \
     그 경로는 타이밍 성질이 다르고, 그 사실은 정본과 문서가 적는다. */                          \
static long long lw_aes_round(void *sp2, unsigned long long slen2,                              \
                              const void *rkp, unsigned long long rklen, int last) {             \
    unsigned char *s = (unsigned char *)sp2;                                                      \
    const unsigned char *rk = (const unsigned char *)rkp;                                          \
    unsigned char t[16]; int i;                                                                     \
    if (slen2 < 16 || rklen < 16) return 0;                                                          \
    for (i = 0; i < 16; i++) t[i] = lw_aes_sbox[s[i]];                                               \
    { unsigned char a;                                                                                \
      a = t[1];  t[1]  = t[5];  t[5]  = t[9];  t[9]  = t[13]; t[13] = a;                               \
      a = t[2];  t[2]  = t[10]; t[10] = a; a = t[6]; t[6] = t[14]; t[14] = a;                          \
      a = t[15]; t[15] = t[11]; t[11] = t[7];  t[7]  = t[3];  t[3]  = a; }                             \
    if (!last) {                                                                                       \
        for (i = 0; i < 16; i += 4) {                                                                   \
            unsigned char a0 = t[i], a1 = t[i+1], a2 = t[i+2], a3 = t[i+3];                              \
            unsigned char x = (unsigned char)(a0 ^ a1 ^ a2 ^ a3);                                         \
            t[i]   = (unsigned char)(a0 ^ x ^ lw_aes_xt((unsigned char)(a0 ^ a1)));                       \
            t[i+1] = (unsigned char)(a1 ^ x ^ lw_aes_xt((unsigned char)(a1 ^ a2)));                        \
            t[i+2] = (unsigned char)(a2 ^ x ^ lw_aes_xt((unsigned char)(a2 ^ a3)));                         \
            t[i+3] = (unsigned char)(a3 ^ x ^ lw_aes_xt((unsigned char)(a3 ^ a0)));                          \
        }                                                                                                     \
    }                                                                                                          \
    for (i = 0; i < 16; i++) s[i] = (unsigned char)(t[i] ^ rk[i]);                                              \
    return 16;                                                                                                   \
}                                                                                                                  \
/* (h16, z16 mut, data) → 먹인 바이트 수. 마지막 조각은 0 으로 채운다(GCM 규약).                                        \
   z ^= 블록 ; z = z·h  in GF(2^128), 축약 다항식 x^128 + x^7 + x^2 + x + 1.                                \
   ★★★ X-0044 (2026-09-21) — **64 비트 낱말 둘 · 분기 없이 가림(mask)으로** 곱한다.                                  \
   전에는 바이트 16 칸을 비트마다 `if (z 의 비트) acc ^= v` 로 돌았다. 그것은 **데이터에 따라                                    \
   분기**하므로 상수시간도 아니었고(«표를 안 쓰니 상수시간 쪽» 이라는 판단이 틀렸다) 느렸다.                                             \
   이제 비트는 0/전부-1 가림이 되어 흐름이 데이터와 무관하고, 4 MiB 에 783 → 62 ms (-O2) 다.                                  \
   ☞ 4비트 표(45 배)는 **안 쓴다** — 키에 따라 표를 읽어 캐시 타이밍이 샌다.                                                 \
   ☞ 이것으로 GCM 이 상수시간이 되지는 않는다: AES 라운드(`lw_aes_block`)는 S-box 표를 읽는다. */                             \
static unsigned long long lw_gh_be64(const unsigned char *p) {                                       \
    unsigned long long x = 0; int i;                                                                 \
    for (i = 0; i < 8; i++) x = (x << 8) | p[i];                                                     \
    return x;                                                                                        \
}                                                                                                    \
static void lw_gh_put64(unsigned char *p, unsigned long long x) {                                    \
    int i;                                                                                           \
    for (i = 7; i >= 0; i--) { p[i] = (unsigned char)x; x >>= 8; }                                   \
}                                                                                                    \
static long long lw_ghash(const void *hp, unsigned long long hlen,                                   \
                          void *zp, unsigned long long zlen,                                         \
                          const void *datap, unsigned long long dlen) {                              \
    const unsigned char *h = (const unsigned char *)hp;                                              \
    unsigned char *z = (unsigned char *)zp;                                                          \
    const unsigned char *data = (const unsigned char *)datap;                                        \
    unsigned long long off, h0, h1, z0, z1;                                                          \
    if (hlen != 16 || zlen != 16) return 0;                                                          \
    h0 = lw_gh_be64(h); h1 = lw_gh_be64(h + 8);                                                      \
    z0 = lw_gh_be64(z); z1 = lw_gh_be64(z + 8);                                                      \
    for (off = 0; off < dlen; off += 16) {                                                           \
        unsigned char blk[16]; unsigned long long i, n = dlen - off;                                 \
        unsigned long long v0 = h0, v1 = h1, a0 = 0, a1 = 0, w, mk, lsb; int b;                      \
        if (n > 16) n = 16;                                                                          \
        for (i = 0; i < 16; i++) blk[i] = (unsigned char)(i < n ? data[off + i] : 0);                \
        z0 ^= lw_gh_be64(blk); z1 ^= lw_gh_be64(blk + 8);                                            \
        for (b = 0; b < 128; b++) {                                                                  \
            w = b < 64 ? z0 : z1;                                                                    \
            mk = 0ull - ((w >> (63 - (b & 63))) & 1ull);                                             \
            a0 ^= v0 & mk; a1 ^= v1 & mk;                                                            \
            lsb = 0ull - (v1 & 1ull);                                                                \
            v1 = (v1 >> 1) | (v0 << 63);                                                             \
            v0 = (v0 >> 1) ^ (0xe100000000000000ull & lsb);                                          \
        }                                                                                            \
        z0 = a0; z1 = a1;                                                                            \
    }                                                                                                \
    lw_gh_put64(z, z0); lw_gh_put64(z + 8, z1);                                                      \
    return (long long)dlen;                                                                          \
}

/* ★ 가변 인자로 받는다 — 본문에 쉼표(표 초기화)가 있어서 한 인자 매크로로는 못 싣는다.
   `low_sha256.h` 가 같은 까닭으로 같은 모양이다. */
#define LOW_AES_STR2(...) #__VA_ARGS__
#define LOW_AES_STR(...)  LOW_AES_STR2(__VA_ARGS__)
#define LOW_AES_C_SOURCE LOW_AES_STR(LOW_AES_BODY) "\n"

LOW_AES_BODY

#endif
