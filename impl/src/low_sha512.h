// ★★★ SHA-512 (FIPS 180-4) — **한 벌만 쓴다.**
//
//   리프는 보통 두 벌로 산다: VM 이 도는 C 와, 방출되는 C. 짧으면 눈으로 대조할 수
//   있지만 SHA-512 는 그럴 길이가 아니다(라운드 상수만 80 개다). ⇒ 매크로 하나를
//   VM 이 컴파일하고, **같은 매크로를 문자열로 만들어** 방출한다. 어긋남이 문법적으로
//   불가능해진다. low_sha256.h 와 같은 자리, 같은 이유다.
//
// ☞ 왜 SHA-512 가 필요한가: Ed25519(RFC 8032)가 요구하고, `rsa_pss_rsae_sha512` 도 쓴다.
//   TLS 1.3 의 전사 해시는 SHA-256 이므로 이것은 **서명 쪽 전용**이다.
#ifndef LOW_SHA512_H
#define LOW_SHA512_H

#define LOW_SHA512_BODY                                                                   \
typedef struct { unsigned long long h[8]; unsigned char b[128]; unsigned long long n; }    \
    lw_sha5_t;                                                                             \
static unsigned long long lw_sha5_rr(unsigned long long x, int n) {                        \
    return (x >> n) | (x << (64 - n));                                                     \
}                                                                                          \
static const unsigned long long LW_SHA5_K[80] = {                                          \
 0x428a2f98d728ae22ULL,0x7137449123ef65cdULL,0xb5c0fbcfec4d3b2fULL,0xe9b5dba58189dbbcULL,  \
 0x3956c25bf348b538ULL,0x59f111f1b605d019ULL,0x923f82a4af194f9bULL,0xab1c5ed5da6d8118ULL,  \
 0xd807aa98a3030242ULL,0x12835b0145706fbeULL,0x243185be4ee4b28cULL,0x550c7dc3d5ffb4e2ULL,  \
 0x72be5d74f27b896fULL,0x80deb1fe3b1696b1ULL,0x9bdc06a725c71235ULL,0xc19bf174cf692694ULL,  \
 0xe49b69c19ef14ad2ULL,0xefbe4786384f25e3ULL,0x0fc19dc68b8cd5b5ULL,0x240ca1cc77ac9c65ULL,  \
 0x2de92c6f592b0275ULL,0x4a7484aa6ea6e483ULL,0x5cb0a9dcbd41fbd4ULL,0x76f988da831153b5ULL,  \
 0x983e5152ee66dfabULL,0xa831c66d2db43210ULL,0xb00327c898fb213fULL,0xbf597fc7beef0ee4ULL,  \
 0xc6e00bf33da88fc2ULL,0xd5a79147930aa725ULL,0x06ca6351e003826fULL,0x142929670a0e6e70ULL,  \
 0x27b70a8546d22ffcULL,0x2e1b21385c26c926ULL,0x4d2c6dfc5ac42aedULL,0x53380d139d95b3dfULL,  \
 0x650a73548baf63deULL,0x766a0abb3c77b2a8ULL,0x81c2c92e47edaee6ULL,0x92722c851482353bULL,  \
 0xa2bfe8a14cf10364ULL,0xa81a664bbc423001ULL,0xc24b8b70d0f89791ULL,0xc76c51a30654be30ULL,  \
 0xd192e819d6ef5218ULL,0xd69906245565a910ULL,0xf40e35855771202aULL,0x106aa07032bbd1b8ULL,  \
 0x19a4c116b8d2d0c8ULL,0x1e376c085141ab53ULL,0x2748774cdf8eeb99ULL,0x34b0bcb5e19b48a8ULL,  \
 0x391c0cb3c5c95a63ULL,0x4ed8aa4ae3418acbULL,0x5b9cca4f7763e373ULL,0x682e6ff3d6b2b8a3ULL,  \
 0x748f82ee5defb2fcULL,0x78a5636f43172f60ULL,0x84c87814a1f0ab72ULL,0x8cc702081a6439ecULL,  \
 0x90befffa23631e28ULL,0xa4506cebde82bde9ULL,0xbef9a3f7b2c67915ULL,0xc67178f2e372532bULL,  \
 0xca273eceea26619cULL,0xd186b8c721c0c207ULL,0xeada7dd6cde0eb1eULL,0xf57d4f7fee6ed178ULL,  \
 0x06f067aa72176fbaULL,0x0a637dc5a2c898a6ULL,0x113f9804bef90daeULL,0x1b710b35131c471bULL,  \
 0x28db77f523047d84ULL,0x32caab7b40c72493ULL,0x3c9ebe0a15c9bebcULL,0x431d67c49c100d4cULL,  \
 0x4cc5d4becb3e42b6ULL,0x597f299cfc657e2aULL,0x5fcb6fab3ad6faecULL,0x6c44198c4a475817ULL };\
static void lw_sha5_block(lw_sha5_t *s, const unsigned char *p) {                           \
    unsigned long long w[80], a, b, c, d, e, f, g, h, t1, t2; int i;                        \
    for (i = 0; i < 16; i++) {                                                              \
        w[i] = 0;                                                                           \
        for (int j = 0; j < 8; j++) w[i] = (w[i] << 8) | p[i * 8 + j];                       \
    }                                                                                       \
    for (i = 16; i < 80; i++) {                                                             \
        unsigned long long s0 = lw_sha5_rr(w[i-15],1) ^ lw_sha5_rr(w[i-15],8) ^ (w[i-15]>>7);\
        unsigned long long s1 = lw_sha5_rr(w[i-2],19) ^ lw_sha5_rr(w[i-2],61) ^ (w[i-2]>>6); \
        w[i] = w[i-16] + s0 + w[i-7] + s1;                                                   \
    }                                                                                        \
    a=s->h[0];b=s->h[1];c=s->h[2];d=s->h[3];e=s->h[4];f=s->h[5];g=s->h[6];h=s->h[7];         \
    for (i = 0; i < 80; i++) {                                                               \
        unsigned long long S1 = lw_sha5_rr(e,14) ^ lw_sha5_rr(e,18) ^ lw_sha5_rr(e,41);      \
        unsigned long long ch = (e & f) ^ ((~e) & g);                                        \
        t1 = h + S1 + ch + LW_SHA5_K[i] + w[i];                                              \
        unsigned long long S0 = lw_sha5_rr(a,28) ^ lw_sha5_rr(a,34) ^ lw_sha5_rr(a,39);      \
        unsigned long long mj = (a & b) ^ (a & c) ^ (b & c);                                 \
        t2 = S0 + mj;                                                                        \
        h=g; g=f; f=e; e=d+t1; d=c; c=b; b=a; a=t1+t2;                                        \
    }                                                                                         \
    s->h[0]+=a;s->h[1]+=b;s->h[2]+=c;s->h[3]+=d;s->h[4]+=e;s->h[5]+=f;s->h[6]+=g;s->h[7]+=h;  \
}                                                                                             \
static long long lw_sha512(const void *src, unsigned long long n,                             \
                           unsigned char *dst, unsigned long long dn) {                       \
    if (dn < 64) return 0;                                                                    \
    lw_sha5_t s;                                                                              \
    s.h[0]=0x6a09e667f3bcc908ULL; s.h[1]=0xbb67ae8584caa73bULL;                               \
    s.h[2]=0x3c6ef372fe94f82bULL; s.h[3]=0xa54ff53a5f1d36f1ULL;                               \
    s.h[4]=0x510e527fade682d1ULL; s.h[5]=0x9b05688c2b3e6c1fULL;                               \
    s.h[6]=0x1f83d9abfb41bd6bULL; s.h[7]=0x5be0cd19137e2179ULL;                               \
    const unsigned char *p = (const unsigned char *)src;                                      \
    unsigned long long i = 0;                                                                 \
    while (n - i >= 128) { lw_sha5_block(&s, p + i); i += 128; }                              \
    unsigned char t[256]; unsigned long long r = n - i, m;                                    \
    for (m = 0; m < r; m++) t[m] = p[i + m];                                                  \
    t[r] = 0x80; m = r + 1;                                                                   \
    unsigned long long need = (r < 112) ? 128 : 256;                                          \
    while (m < need - 16) t[m++] = 0;                                                         \
    /* 길이는 **128비트 빅엔디언**이다 — 위 16 바이트는 0 (n < 2^61 이므로). */                \
    for (m = 0; m < 8; m++) t[need - 16 + m] = 0;                                             \
    unsigned long long bits = n * 8ULL;                                                       \
    for (m = 0; m < 8; m++) t[need - 8 + m] = (unsigned char)((bits >> (56 - 8 * m)) & 0xff);  \
    for (m = 0; m < need; m += 128) lw_sha5_block(&s, t + m);                                  \
    for (m = 0; m < 8; m++)                                                                    \
        for (unsigned long long j = 0; j < 8; j++)                                             \
            dst[m * 8 + j] = (unsigned char)((s.h[m] >> (56 - 8 * j)) & 0xff);                 \
    return 64;                                                                                 \
}

#define LOW_SHA512_STR2(...) #__VA_ARGS__
#define LOW_SHA512_STR(...)  LOW_SHA512_STR2(__VA_ARGS__)
#define LOW_SHA512_C_SOURCE  LOW_SHA512_STR(LOW_SHA512_BODY) "\n"

#endif
