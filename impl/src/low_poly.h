/* low_poly.h — Poly1305 (RFC 8439 §2.5) 의 블록 되풀이, 두 뒤끝이 나눠 쓴다.
 *
 * ★★★★ 왜 낱말인가 (RFC-0122, 2026-09-24)
 *   ChaCha20 에 폭을 주고 나니(2,439 MB/s) ChaCha20-Poly1305 전체의 **86%** 가 이쪽이 됐다
 *   — 언어로 쓴 Poly1305 가 331 MB/s 다. 막는 것은 알고리즘이 아니라 **경계 검사와 트랩**이다:
 *   26비트 조각 다섯을 u64 위에서 곱하는 자리마다 이 언어는 넘침을 본다. 그 값이 곱셈 25 번
 *   마다 붙는다.
 *   ⇒ **계산만** 내린다. 짜임(AAD·암호문·길이 블록의 패딩 차례, 태그를 먼저 검증하고 복호하는
 *     순서)은 `lib/aead.low` 에 남는다 — 숨은 규율은 검사할 수 없다.
 *
 * ★★★ 뜻의 정의는 `lib/poly.low` 의 `block` 이다. 지우지 않는다 — 골든이 낱말과 그 정의를
 *   **같은 상태·같은 바이트**로 맞댄다. 그래서 이 파일의 셈은 그 글을 **그대로** 옮긴 것이다
 *   (26비트 조각 다섯 · 5 로 접는 mod 2^130-5 · 같은 자리올림 차례).
 *
 * ★★ 상태의 자리도 `lib/poly.low` 와 **같다**: `st[0..5]` = h, `st[5..10]` = r(clamp 된 조각).
 *   그래서 `setup` 과 `emit` 은 언어에 남고, 이 낱말은 **가운데 되풀이만** 판다.
 *
 * ★ 마지막 블록이 16 바이트가 안 되면 0 으로 채우고 2^128 비트를 더한다 — ChaCha20-Poly1305 는
 *   AAD 와 암호문을 **16 의 배수로 0 패딩해서** 먹이므로 그것이 맞는 규약이다(RFC 8439 §2.8).
 *
 * ★ 기계 명령이 없다 ⇒ `--hw` 와 무관하다. VM 과 방출 C 가 **같은 함수**를 부른다.
 */
#ifndef LOW_POLY_H
#define LOW_POLY_H

/* ★★★★ **조각을 다섯에서 셋으로** (X-0054, 2026-09-24). 26 비트 조각 다섯이면 곱셈이 **25 번**,
 *   44 비트 조각 셋이면 **아홉 번**이다 — 128 비트 중간값을 들 수 있으면 그렇다. 실측 1,353 MB/s
 *   가 막힌 자리가 그 스물다섯이었다.
 *   ☞ `unsigned __int128` 은 표준 C 가 아니라 **확장**이다. 없는 기계(32 비트 `cortex_m` 등)에서는
 *     조각 다섯 판이 선다 — 답은 같고, 갈림은 **전처리기 한 줄**이다. 두 판을 한 매크로 안에서
 *     `#if` 로 가를 수는 없다(문자열화하면 지시문이 한 줄로 뭉개진다): 그래서 **매크로를 둘로**
 *     두고 부르는 자리에서 고른다.
 *   ★ 상태의 자리(26 비트 다섯)는 **안 바뀐다** — `lib/poly.low` 의 `setup`·`emit` 이 그 자리를
 *     읽기 때문이다. 44 비트는 이 함수 **안에서만** 산다(`lw_pl_to44`·`lw_pl_to26`, 부름당 한 번).
 */
#define LOW_POLY_BODY_44                                                                       \
static unsigned long long lw_pl_le64(const unsigned char *p) {                                 \
    return (unsigned long long)p[0]        | ((unsigned long long)p[1] << 8)                   \
         | ((unsigned long long)p[2] << 16) | ((unsigned long long)p[3] << 24)                 \
         | ((unsigned long long)p[4] << 32) | ((unsigned long long)p[5] << 40)                 \
         | ((unsigned long long)p[6] << 48) | ((unsigned long long)p[7] << 56);                \
}                                                                                              \
static void lw_pl_to44(const unsigned long long *q, unsigned long long *L) {                   \
    unsigned long long a = q[0] | (q[1] << 26), b = q[2] | (q[3] << 26), m;                    \
    L[0] = a & 0xfffffffffffull;                                                               \
    m = (a >> 44) | (b << 8);                                                                  \
    L[1] = m & 0xfffffffffffull;                                                               \
    L[2] = (m >> 44) | (q[4] << 16);                                                           \
}                                                                                              \
static void lw_pl_to26(const unsigned long long *L, unsigned long long *q) {                   \
    q[0] = L[0] & 0x3ffffffull;                                                                \
    q[1] = ((L[0] >> 26) | (L[1] << 18)) & 0x3ffffffull;                                       \
    q[2] = (L[1] >> 8) & 0x3ffffffull;                                                         \
    q[3] = ((L[1] >> 34) | (L[2] << 10)) & 0x3ffffffull;                                       \
    q[4] = L[2] >> 16;                                                                         \
}                                                                                              \
static long long lw_poly1305(void *stp, unsigned long long stn,                                \
                             const void *datap, unsigned long long dlen) {                     \
    unsigned long long *st = (unsigned long long *)stp;                                        \
    const unsigned char *data = (const unsigned char *)datap;                                  \
    unsigned long long H[3], R[3], h0, h1, h2, r0, r1, r2, s1, s2, c, off;                     \
    if (stn < 10) return 0;                                                                    \
    lw_pl_to44(st, H); lw_pl_to44(st + 5, R);                                                  \
    h0 = H[0]; h1 = H[1]; h2 = H[2];                                                           \
    r0 = R[0]; r1 = R[1]; r2 = R[2];                                                           \
    s1 = r1 * 20ull; s2 = r2 * 20ull;                                                          \
    for (off = 0; off < dlen; off += 16) {                                                     \
        unsigned char blk[16]; const unsigned char *b = data + off;                            \
        unsigned long long i, n = dlen - off, t0, t1;                                          \
        unsigned __int128 d0, d1, d2;                                                          \
        if (n < 16) { for (i = 0; i < 16; i++) blk[i] = (unsigned char)(i < n ? b[i] : 0);     \
                      b = blk; }                                                               \
        t0 = lw_pl_le64(b); t1 = lw_pl_le64(b + 8);                                            \
        h0 += t0 & 0xfffffffffffull;                                                           \
        h1 += ((t0 >> 44) | (t1 << 20)) & 0xfffffffffffull;                                    \
        h2 += ((t1 >> 24) & 0x3ffffffffffull) | 0x10000000000ull;                              \
        d0 = (unsigned __int128)h0 * r0 + (unsigned __int128)h1 * s2                           \
           + (unsigned __int128)h2 * s1;                                                       \
        d1 = (unsigned __int128)h0 * r1 + (unsigned __int128)h1 * r0                           \
           + (unsigned __int128)h2 * s2;                                                       \
        d2 = (unsigned __int128)h0 * r2 + (unsigned __int128)h1 * r1                           \
           + (unsigned __int128)h2 * r0;                                                       \
        c = (unsigned long long)(d0 >> 44); h0 = (unsigned long long)d0 & 0xfffffffffffull;    \
        d1 += c;                                                                               \
        c = (unsigned long long)(d1 >> 44); h1 = (unsigned long long)d1 & 0xfffffffffffull;    \
        d2 += c;                                                                               \
        c = (unsigned long long)(d2 >> 42); h2 = (unsigned long long)d2 & 0x3ffffffffffull;    \
        h0 += c * 5ull; c = h0 >> 44; h0 &= 0xfffffffffffull; h1 += c;                         \
    }                                                                                          \
    H[0] = h0; H[1] = h1; H[2] = h2;                                                           \
    lw_pl_to26(H, st);                                                                         \
    return (long long)dlen;                                                                    \
}

#define LOW_POLY_BODY_26                                                                       \
static unsigned long long lw_pl_le64(const unsigned char *p) {                                 \
    return (unsigned long long)p[0]        | ((unsigned long long)p[1] << 8)                   \
         | ((unsigned long long)p[2] << 16) | ((unsigned long long)p[3] << 24)                 \
         | ((unsigned long long)p[4] << 32) | ((unsigned long long)p[5] << 40)                 \
         | ((unsigned long long)p[6] << 48) | ((unsigned long long)p[7] << 56);                \
}                                                                                              \
static long long lw_poly1305(void *stp, unsigned long long stn,                                \
                             const void *datap, unsigned long long dlen) {                     \
    unsigned long long *st = (unsigned long long *)stp;                                        \
    const unsigned char *data = (const unsigned char *)datap;                                  \
    unsigned long long h0, h1, h2, h3, h4, r0, r1, r2, r3, r4, s1, s2, s3, s4, off;            \
    if (stn < 10) return 0;                                                                    \
    h0 = st[0]; h1 = st[1]; h2 = st[2]; h3 = st[3]; h4 = st[4];                                \
    r0 = st[5]; r1 = st[6]; r2 = st[7]; r3 = st[8]; r4 = st[9];                                \
    s1 = r1 * 5ull; s2 = r2 * 5ull; s3 = r3 * 5ull; s4 = r4 * 5ull;                            \
    for (off = 0; off < dlen; off += 16) {                                                     \
        unsigned char blk[16]; const unsigned char *b;                                         \
        unsigned long long i, n = dlen - off, lo, hi, d0, d1, d2, d3, d4, c;                   \
        if (n >= 16) { b = data + off; }                                                       \
        else { for (i = 0; i < 16; i++) blk[i] = (unsigned char)(i < n ? data[off + i] : 0);   \
               b = blk; }                                                                      \
        lo = lw_pl_le64(b); hi = lw_pl_le64(b + 8);                                            \
        h0 += lo & 0x3ffffffull;                                                               \
        h1 += (lo >> 26) & 0x3ffffffull;                                                       \
        h2 += ((lo >> 52) & 0xfffull) | ((hi << 12) & 0x3fff000ull);                           \
        h3 += (hi >> 14) & 0x3ffffffull;                                                       \
        h4 += ((hi >> 40) & 0x3ffffffull) + 0x1000000ull;                                      \
        d0 = h0 * r0 + h1 * s4 + h2 * s3 + h3 * s2 + h4 * s1;                                  \
        d1 = h0 * r1 + h1 * r0 + h2 * s4 + h3 * s3 + h4 * s2;                                  \
        d2 = h0 * r2 + h1 * r1 + h2 * r0 + h3 * s4 + h4 * s3;                                  \
        d3 = h0 * r3 + h1 * r2 + h2 * r1 + h3 * r0 + h4 * s4;                                  \
        d4 = h0 * r4 + h1 * r3 + h2 * r2 + h3 * r1 + h4 * r0;                                  \
        c = d0 >> 26; h0 = d0 & 0x3ffffffull;                                                  \
        d1 += c; c = d1 >> 26; h1 = d1 & 0x3ffffffull;                                         \
        d2 += c; c = d2 >> 26; h2 = d2 & 0x3ffffffull;                                         \
        d3 += c; c = d3 >> 26; h3 = d3 & 0x3ffffffull;                                         \
        d4 += c; c = d4 >> 26; h4 = d4 & 0x3ffffffull;                                         \
        h0 += c * 5ull; c = h0 >> 26; h0 &= 0x3ffffffull; h1 += c;                             \
    }                                                                                          \
    st[0] = h0; st[1] = h1; st[2] = h2; st[3] = h3; st[4] = h4;                                \
    return (long long)dlen;                                                                    \
}

#define LOW_POLY_STR2(...) #__VA_ARGS__
#define LOW_POLY_STR(...)  LOW_POLY_STR2(__VA_ARGS__)
#define LOW_POLY_C_SOURCE    LOW_POLY_STR(LOW_POLY_BODY_44) "\n"
#define LOW_POLY_C_SOURCE_26 LOW_POLY_STR(LOW_POLY_BODY_26) "\n"

#if defined(__SIZEOF_INT128__)
LOW_POLY_BODY_44
#else
LOW_POLY_BODY_26
#endif


#endif
