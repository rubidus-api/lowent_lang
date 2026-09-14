/* low_hostfault.inc — 호스트 잎의 실패를 **결정적으로 주입한다** (RFC-0075 S5 · 2026-08-15)
 *
 * ★★★ 왜 있어야 하나. `file_read` 가 짧게 읽는 경우, 쓰기가 실패하는 경우, 여는 데 실패하는
 *   경우는 이 저장소에서 **한 번도 실행된 적이 없는 경로**였다. 라이브러리(`lib/file.low`·
 *   `lib/out.low`)는 그 경로를 다루는 코드를 갖고 있는데, 그 코드가 옳은지 아무도 못 봤다.
 *   실패를 **불러올 수 없으면** 실패 처리는 주장일 뿐이다.
 *
 * ★★★ 왜 한 파일인가. 잎은 **두 곳**에 구현돼 있다 — VM(`low_ir.c`)과 방출 C(`low_cbe.c`).
 *   주입 규칙을 두 벌로 두면 갈리고, 갈리면 VM ≡ native 오라클이 **주입 아래서만** 깨진다.
 *   그래서 이 파일이 유일한 원본이다: VM 은 그대로 `#include` 하고, 방출기는
 *   `scripts/gen-hostfault-blob.py` 가 만든 문자열로 **같은 바이트**를 찍는다.
 *   (그 블롭이 이 파일에서 다시 나오는지는 게이트가 매번 확인한다 — 유니코드 표와 같은 규율.)
 *
 * ★★ 기본은 **꺼짐**이다. 환경변수가 없으면 이 코드는 잎마다 정수 하나를 올릴 뿐이고,
 *   프로그램의 답은 한 바이트도 달라지지 않는다.
 *
 * 문법:
 *
 *     LOW_HOST_FAULT="<leaf>:<what>[@<nth>][=<k>][,…]"
 *
 *     leaf  = open | read | write | close | out          (file · 표준출력)
 *           | connect | accept | send | recv               (net)
 *           | random | env                                  (그 밖의 잎)
 *     what  = err    잎이 **none 을 답한다**(트랩이 아니다 — 실패는 값이다, RFC-0043 D5)
 *           | short  옮기는 바이트를 **k 개로 자른다**(기본 1). 부분 읽기/쓰기.
 *     @nth  = 1부터 세는 **호출 번호**. 없으면 **매번**.
 *     =k    = short 의 바이트 수.
 *
 *   보기:
 *     LOW_HOST_FAULT="read:short@2=3"   두 번째 읽기만 3 바이트로 자른다
 *     LOW_HOST_FAULT="write:err@1"      첫 쓰기가 실패한다
 *     LOW_HOST_FAULT="open:err"         모든 open 이 실패한다
 *
 * ☞ 안 지은 것(정직히): 지연·순서 뒤섞기·부분 close · errno 값 고르기 ·
 *   `listen`/`port`/`net_close`·`dir_*`·`path_*` 잎. 같은 틀에 남아 있다.
 *
 * ★★★★ **잎 이름은 정확히 일치시킨다.** 처음 판은 **첫 글자**로 갈랐다(`'r'` → read). 그 판에
 *   `recv` 를 더했다면 **`read` 로 읽혔을 것**이다 — 요청한 잎과 다른 잎을 조용히 주입하는,
 *   시험 장치가 낼 수 있는 가장 나쁜 종류의 오류다(시험이 거짓말을 하면 그 위의 전부가 거짓말이다).
 *   ⇒ 표를 두고 **온 이름**을 맞댄다. 새 잎을 더하는 값도 그만큼 싸진다: 표에 한 줄이다.
 */

#define LW_HF_NLEAF 11

static int  lw_hf_ready;
static const char *lw_hf_spec;
static unsigned long lw_hf_calls[LW_HF_NLEAF];

static const char *const lw_hf_leaves[LW_HF_NLEAF] = {
    "open", "read", "write", "close", "out",
    "connect", "accept", "send", "recv",
    "random", "env"
};

static int lw_hf_leaf_index(const char *leaf) {
    int i;
    for (i = 0; i < LW_HF_NLEAF; i++)
        if (strcmp(lw_hf_leaves[i], leaf) == 0) return i;
    return LW_HF_NLEAF - 1;   /* 닿지 않는다 — 부르는 쪽이 표에 있는 이름만 준다 */
}

/* 이 잎의 이번 호출이 주입 대상인가.
 *   돌려주는 값: 0 = 그대로 · 1 = 실패(none) · 2 = 잘라 쓰기(*kout 바이트)
 * ★ 호출 수는 **주입 여부와 무관하게** 올린다 — 그래야 `@3` 이 뜻을 갖는다. */
static int lw_hf_probe(const char *leaf, unsigned long *kout) {
    const char *p;
    int idx;
    unsigned long nth;
    if (!lw_hf_ready) { lw_hf_ready = 1; lw_hf_spec = getenv("LOW_HOST_FAULT"); }
    idx = lw_hf_leaf_index(leaf);
    nth = ++lw_hf_calls[idx];
    if (!lw_hf_spec || !*lw_hf_spec) return 0;
    for (p = lw_hf_spec; *p; ) {
        const char *name = p;
        size_t nlen = 0;
        int what = 0;
        unsigned long want_nth = 0, k = 1;
        while (p[nlen] && p[nlen] != ':' && p[nlen] != ',') nlen++;
        p = name + nlen;
        if (*p != ':') { while (*p && *p != ',') p++; if (*p) p++; continue; }
        p++;
        if (p[0] == 'e') { what = 1; while (*p && *p != '@' && *p != '=' && *p != ',') p++; }
        else if (p[0] == 's') { what = 2; while (*p && *p != '@' && *p != '=' && *p != ',') p++; }
        else { while (*p && *p != ',') p++; if (*p) p++; continue; }
        if (*p == '@') { p++; want_nth = strtoul(p, (char **)&p, 10); }
        if (*p == '=') { p++; k = strtoul(p, (char **)&p, 10); }
        while (*p && *p != ',') p++;
        if (*p == ',') p++;
        if (nlen && strncmp(name, leaf, nlen) == 0 && leaf[nlen] == '\0' &&
            (want_nth == 0 || want_nth == nth)) {
            if (what == 2 && kout) *kout = k;
            return what;
        }
    }
    return 0;
}

/* 옮길 바이트 수를 자른다 — `short` 의 실물. */
static size_t lw_hf_cap(size_t want, unsigned long k) {
    return ((unsigned long)want > k) ? (size_t)k : want;
}
