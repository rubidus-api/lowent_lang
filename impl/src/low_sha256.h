// low_sha256.h — SHA-256 **한 벌**. (RFC-0090 N3c, 2026-08-11)
//
// ★★★★★ **왜 이렇게까지 하는가.** 이 저장소의 리프는 늘 **두 벌**로 존재한다: VM 이
//   실행하는 C 와, 방출된 C 안에 **자립적으로** 들어가는 C(`cc file.c -lm` 으로 서야 하므로
//   벤더 라이브러리를 부를 수 없다). 해시 둘(FNV-1a·CRC-32)은 짧아서 두 곳에 나란히 적고
//   눈으로 대조했다 — **SHA-256 은 그러기엔 길다**(라운드 상수 64 + 압축 함수).
//   두 곳에 복사하면 그것이 곧 **한쪽만 고쳐질 자리**이고, 이 저장소가 반복해서 겪은 병이다
//   (VM 만 고치고 네이티브를 안 고쳐 조용히 갈린 사고가 여럿 있다).
//
//   ⇒ 알고리즘을 **매크로 하나**로 둔다. VM 은 그것을 **컴파일**하고, C 백엔드는
//     **그것을 문자열화해 방출한다.** 두 벌이 아니라 **한 벌**이고, 갈릴 자리가 없다.
//     한쪽만 고치는 것이 **문법적으로 불가능**하다 — 그것이 이 파일의 값이다.
//
// ★ 문자열화(`#`)는 줄바꿈을 지운다 — 방출 C 가 한 줄이 되지만 **유효한 C** 다.
//   그래서 이 매크로 안에는 `//` 주석을 쓰지 않는다(한 줄이 되면 뒤가 전부 먹힌다).
// ★ 순수 계산이라 **프리스탠딩에서도 쓴다**(호스트 가드 바깥).
#ifndef LOW_SHA256_H
#define LOW_SHA256_H

#include <stddef.h>

#define LOW_SHA256_BODY                                                                            \
typedef struct { unsigned int h[8]; unsigned long long n; unsigned char b[64]; unsigned int c; }   \
    lw_sha_t;                                                                                      \
static unsigned int lw_sha_rr(unsigned int x, int k) { return (x >> k) | (x << (32 - k)); }        \
static void lw_sha_block(lw_sha_t *s, const unsigned char *p) {                                    \
    static const unsigned int K[64] = {                                                            \
        0x428a2f98u,0x71374491u,0xb5c0fbcfu,0xe9b5dba5u,0x3956c25bu,0x59f111f1u,0x923f82a4u,       \
        0xab1c5ed5u,0xd807aa98u,0x12835b01u,0x243185beu,0x550c7dc3u,0x72be5d74u,0x80deb1feu,       \
        0x9bdc06a7u,0xc19bf174u,0xe49b69c1u,0xefbe4786u,0x0fc19dc6u,0x240ca1ccu,0x2de92c6fu,       \
        0x4a7484aau,0x5cb0a9dcu,0x76f988dau,0x983e5152u,0xa831c66du,0xb00327c8u,0xbf597fc7u,       \
        0xc6e00bf3u,0xd5a79147u,0x06ca6351u,0x14292967u,0x27b70a85u,0x2e1b2138u,0x4d2c6dfcu,       \
        0x53380d13u,0x650a7354u,0x766a0abbu,0x81c2c92eu,0x92722c85u,0xa2bfe8a1u,0xa81a664bu,       \
        0xc24b8b70u,0xc76c51a3u,0xd192e819u,0xd6990624u,0xf40e3585u,0x106aa070u,0x19a4c116u,       \
        0x1e376c08u,0x2748774cu,0x34b0bcb5u,0x391c0cb3u,0x4ed8aa4au,0x5b9cca4fu,0x682e6ff3u,       \
        0x748f82eeu,0x78a5636fu,0x84c87814u,0x8cc70208u,0x90befffau,0xa4506cebu,0xbef9a3f7u,       \
        0xc67178f2u };                                                                             \
    unsigned int w[64], a, b_, c, d, e, f, g, h2, t1, t2; int i;                                   \
    for (i = 0; i < 16; i++)                                                                       \
        w[i] = ((unsigned int)p[i*4] << 24) | ((unsigned int)p[i*4+1] << 16) |                      \
               ((unsigned int)p[i*4+2] << 8) | (unsigned int)p[i*4+3];                             \
    for (i = 16; i < 64; i++) {                                                                    \
        unsigned int s0 = lw_sha_rr(w[i-15],7) ^ lw_sha_rr(w[i-15],18) ^ (w[i-15] >> 3);           \
        unsigned int s1 = lw_sha_rr(w[i-2],17) ^ lw_sha_rr(w[i-2],19) ^ (w[i-2] >> 10);            \
        w[i] = w[i-16] + s0 + w[i-7] + s1;                                                         \
    }                                                                                              \
    a=s->h[0]; b_=s->h[1]; c=s->h[2]; d=s->h[3];                                                   \
    e=s->h[4]; f=s->h[5]; g=s->h[6]; h2=s->h[7];                                                   \
    for (i = 0; i < 64; i++) {                                                                     \
        unsigned int S1 = lw_sha_rr(e,6) ^ lw_sha_rr(e,11) ^ lw_sha_rr(e,25);                      \
        unsigned int ch = (e & f) ^ ((~e) & g);                                                    \
        unsigned int S0, mj;                                                                       \
        t1 = h2 + S1 + ch + K[i] + w[i];                                                           \
        S0 = lw_sha_rr(a,2) ^ lw_sha_rr(a,13) ^ lw_sha_rr(a,22);                                   \
        mj = (a & b_) ^ (a & c) ^ (b_ & c);                                                        \
        t2 = S0 + mj;                                                                              \
        h2=g; g=f; f=e; e=d+t1; d=c; c=b_; b_=a; a=t1+t2;                                          \
    }                                                                                              \
    s->h[0]+=a; s->h[1]+=b_; s->h[2]+=c; s->h[3]+=d;                                               \
    s->h[4]+=e; s->h[5]+=f; s->h[6]+=g; s->h[7]+=h2;                                               \
}                                                                                                  \
static long long lw_sha256(const unsigned char *p, size_t n, unsigned char *out, size_t outn) {    \
    lw_sha_t s; size_t i; unsigned long long bits;                                                 \
    if (outn < 32) return 0;                                                                       \
    s.h[0]=0x6a09e667u; s.h[1]=0xbb67ae85u; s.h[2]=0x3c6ef372u; s.h[3]=0xa54ff53au;                \
    s.h[4]=0x510e527fu; s.h[5]=0x9b05688cu; s.h[6]=0x1f83d9abu; s.h[7]=0x5be0cd19u;                \
    s.n = 0; s.c = 0;                                                                              \
    for (i = 0; i < n; i++) {                                                                      \
        s.b[s.c++] = p[i]; s.n++;                                                                   \
        if (s.c == 64) { lw_sha_block(&s, s.b); s.c = 0; }                                          \
    }                                                                                              \
    bits = s.n * 8ull;                                                                             \
    s.b[s.c++] = 0x80u;                                                                            \
    if (s.c > 56) { while (s.c < 64) s.b[s.c++] = 0; lw_sha_block(&s, s.b); s.c = 0; }             \
    while (s.c < 56) s.b[s.c++] = 0;                                                               \
    for (i = 0; i < 8; i++) s.b[56+i] = (unsigned char)((bits >> (56 - 8*i)) & 0xffu);             \
    lw_sha_block(&s, s.b);                                                                          \
    for (i = 0; i < 8; i++) {                                                                      \
        out[i*4]   = (unsigned char)((s.h[i] >> 24) & 0xffu);                                       \
        out[i*4+1] = (unsigned char)((s.h[i] >> 16) & 0xffu);                                       \
        out[i*4+2] = (unsigned char)((s.h[i] >> 8) & 0xffu);                                        \
        out[i*4+3] = (unsigned char)(s.h[i] & 0xffu);                                               \
    }                                                                                              \
    return 32;                                                                                      \
}

// ★ 가변 인자여야 한다 — 본문에 **최상위 쉼표**(배열 초기화·선언)가 있어서, 인자 하나짜리
//   문자열화 매크로는 "인자 75 개" 라고 거절한다(실측). `__VA_ARGS__` 가 그것을 삼킨다.
#define LOW_SHA256_STR2(...) #__VA_ARGS__
#define LOW_SHA256_STR(...)  LOW_SHA256_STR2(__VA_ARGS__)
// 방출 C 가 그대로 받는 원문 — **위 매크로와 같은 글자**다(문자열화이므로 다를 수가 없다).
#define LOW_SHA256_C_SOURCE LOW_SHA256_STR(LOW_SHA256_BODY) "\n"

#endif // LOW_SHA256_H
