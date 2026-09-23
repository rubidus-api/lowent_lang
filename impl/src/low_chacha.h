/* low_chacha.h — ChaCha20 (RFC 8439) 의 **한 구현**, 두 뒤끝이 나눠 쓴다.
 *
 * ★★★★ 왜 낱말인가 (RFC-0122, 소유자 지시 2026-09-24: «chacha 의 simd 낱말도 처리»)
 *   `lib/chacha.low` 는 이 언어로 쓴 ChaCha20 이고 실측 **89 MB/s** 다. 같은 기계의
 *   OpenSSL 은 ChaCha20-Poly1305 를 2059 MB/s 로 돈다 — 30 배다. 까닭은 알고리즘이
 *   아니라 **폭**이다: ChaCha 의 블록 스무 라운드는 서로 **독립인 블록 넷·여덟**을 한
 *   레지스터에 실으면 그대로 넷·여덟 배가 된다. 그 폭은 이 언어에 없다.
 *   ⇒ **계산만** 낱말로 내린다. AES 와 GHASH 를 내릴 때 세운 자리와 같다(X-0043 ⓑ).
 *
 * ★★★ 무엇을 안 내렸나 — **뜻은 언어에 남는다.**
 *   `lib/chacha.low` 의 `qround`·`rounds`·`keystream` 은 **지우지 않는다**. 그것이 이
 *   모듈이 말하는 뜻의 정의이고, 골든이 낱말과 그 정의를 **같은 입력으로 맞댄다**
 *   (`gmul128` 과 잎 `ghash` 가 선 자리와 같다). 짜임(AEAD 의 패딩·길이 블록·태그를
 *   먼저 검증하고 복호하는 차례)은 `lib/aead.low` 에 남는다 — 숨은 규율은 검사할 수 없다.
 *
 * ★★ 두 뒤끝이 **같은 글자**를 쓴다: VM 은 이 헤더를 컴파일해 넣고, C 방출기는 같은 글을
 *   문자열로 실어 보낸다(`LOW_CHACHA_C_SOURCE`). 나뉘어 있으면 언젠가 갈린다.
 *   ★ VM 은 **언제나 이 셈 판**이다(SIMD 를 못 돈다) ⇒ 오라클(VM ≡ 네이티브)이
 *     곧 SIMD 경로의 차등 시험이다.
 *
 * ★ 카운터 규약은 `aes_ctr` 와 **같은 모양**이다: 16 바이트 블록 하나가 상태의 뒤 넷이고
 *   (앞 4 바이트 = 블록 번호 리틀엔디언, 뒤 12 = nonce), 낱말이 **제자리에서** 올린다.
 *   그래서 나눠 불러도 이어진다. 넘침은 2^32 에서 **돈다**(RFC 8439 가 그렇게 말한다).
 */
#ifndef LOW_CHACHA_H
#define LOW_CHACHA_H

#define LOW_CHACHA_BODY                                                                        \
static unsigned int lw_cc_le32(const unsigned char *p) {                                       \
    return (unsigned int)p[0] | ((unsigned int)p[1] << 8)                                      \
         | ((unsigned int)p[2] << 16) | ((unsigned int)p[3] << 24);                            \
}                                                                                              \
static void lw_cc_put32(unsigned char *p, unsigned int x) {                                    \
    p[0] = (unsigned char)x;         p[1] = (unsigned char)(x >> 8);                           \
    p[2] = (unsigned char)(x >> 16); p[3] = (unsigned char)(x >> 24);                          \
}                                                                                              \
static unsigned int lw_cc_rotl(unsigned int x, int n) {                                        \
    return (unsigned int)((x << n) | (x >> (32 - n)));                                         \
}                                                                                              \
static void lw_chacha_block(const unsigned int *st, unsigned int *o) {                         \
    int i, r;                                                                                  \
    for (i = 0; i < 16; i++) o[i] = st[i];                                                     \
    for (r = 0; r < 10; r++) {                                                                 \
        static const int qi[8][4] = { {0,4,8,12},{1,5,9,13},{2,6,10,14},{3,7,11,15},           \
                                      {0,5,10,15},{1,6,11,12},{2,7,8,13},{3,4,9,14} };         \
        for (i = 0; i < 8; i++) {                                                              \
            int a = qi[i][0], b = qi[i][1], c = qi[i][2], d = qi[i][3];                         \
            o[a] += o[b]; o[d] = lw_cc_rotl(o[d] ^ o[a], 16);                                  \
            o[c] += o[d]; o[b] = lw_cc_rotl(o[b] ^ o[c], 12);                                  \
            o[a] += o[b]; o[d] = lw_cc_rotl(o[d] ^ o[a], 8);                                   \
            o[c] += o[d]; o[b] = lw_cc_rotl(o[b] ^ o[c], 7);                                   \
        }                                                                                      \
    }                                                                                          \
    for (i = 0; i < 16; i++) o[i] += st[i];                                                    \
}                                                                                              \
static int lw_chacha_state(const unsigned char *key, const unsigned char *ctr,                 \
                           unsigned int *st) {                                                 \
    int i;                                                                                     \
    st[0] = 0x61707865u; st[1] = 0x3320646eu; st[2] = 0x79622d32u; st[3] = 0x6b206574u;         \
    for (i = 0; i < 8; i++) st[4 + i] = lw_cc_le32(key + 4 * i);                               \
    for (i = 0; i < 4; i++) st[12 + i] = lw_cc_le32(ctr + 4 * i);                              \
    return 1;                                                                                  \
}                                                                                              \
static long long lw_chacha20(const void *keyp, unsigned long long klen,                        \
                             void *ctrp, unsigned long long clen,                              \
                             const void *srcp, unsigned long long slen,                        \
                             void *dstp, unsigned long long dlen) {                            \
    const unsigned char *key = (const unsigned char *)keyp;                                    \
    const unsigned char *src = (const unsigned char *)srcp;                                    \
    unsigned char *ctr = (unsigned char *)ctrp, *dst = (unsigned char *)dstp;                  \
    unsigned int st[16], o[16];                                                                \
    unsigned long long off;                                                                    \
    int i;                                                                                     \
    if (klen != 32 || clen != 16 || dlen < slen) return 0;                                     \
    lw_chacha_state(key, ctr, st);                                                             \
    for (off = 0; off < slen; off += 64) {                                                     \
        unsigned long long n = slen - off; unsigned char ks[64];                               \
        if (n > 64) n = 64;                                                                    \
        lw_chacha_block(st, o);                                                                \
        for (i = 0; i < 16; i++) lw_cc_put32(ks + 4 * i, o[i]);                                \
        for (i = 0; i < (int)n; i++) dst[off + i] = (unsigned char)(src[off + i] ^ ks[i]);     \
        st[12] = (unsigned int)(st[12] + 1u);                                                  \
    }                                                                                          \
    lw_cc_put32(ctr, st[12]);                                                                  \
    return (long long)slen;                                                                    \
}

/* ★ 가변 인자로 받는다 — 본문에 쉼표(표 초기화)가 있어서 한 인자 매크로로는 못 싣는다. */
#define LOW_CHACHA_STR2(...) #__VA_ARGS__
#define LOW_CHACHA_STR(...)  LOW_CHACHA_STR2(__VA_ARGS__)
#define LOW_CHACHA_C_SOURCE LOW_CHACHA_STR(LOW_CHACHA_BODY) "\n"

LOW_CHACHA_BODY

#endif
