/* ★★ low_fmt.h 의 ★ — 서식이 틀린 자리는 멈춘다. */
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "low_fmt.h"
#include "low_mem.h"

void low_bfmt_fail(const char *fmt, proven_err_t err) {
    fputs("lowentc: internal error — a format string does not match its arguments (proven error ", stderr);
    fprintf(stderr, "%d): \"%s\"\n", (int)err, fmt);
    abort();
}

/* low_fmt.h 의 low_fpf — 짧은 글은 스택 칸에서, 긴 글은 필요한 만큼 proven 힙에서 짓고, 같은 흐름에 한 번에 쓴다. */
int low_fpf_args(low_out_t *f, const char *fmt, const proven_arg_t *args, proven_size_t n) {
    char sb[1024];
    int need = low_bfmt_args(sb, sizeof sb, fmt, args, n);
    const char *p = sb; char *big = NULL;
    if ((size_t)need >= sizeof sb) {
        big = (char *)low_alloc((proven_size_t)need + 1);
        if (!big) return -1;
        need = low_bfmt_args(big, (size_t)need + 1, fmt, args, n);
        p = big;
    }
    // ★ 2026-11-26 (W-20261126-1117) — C 를 내는 자리는 정수를 `{}ll` 로 적는다. 가장 작은 i64 는 그러면 `-9223372036854775808ll` 인데,
    //   C 에서 그것은 «long long 에 안 드는 9223372036854775808ll 의 부호 뒤집기» 라 C 컴파일러가 «so large that it is unsigned» 를
    //   경고한다(값은 맞게 나왔다). 쓰는 자리 29 곳을 하나씩 고치는 대신 여기서 같은 값의 바른 C 철자로 바꾼다. 다른 글에는 이 토큰이 없다.
    static const char MINLL[] = "-9223372036854775808ll", FIXLL[] = "(-9223372036854775807ll-1)";
    char *fixed = NULL;
    if (need > 0 && strstr(p, MINLL)) {
        size_t cnt = 0; for (const char *q = p; (q = strstr(q, MINLL)); q += sizeof MINLL - 1) cnt++;
        size_t nn = (size_t)need + cnt * (sizeof FIXLL - sizeof MINLL);
        fixed = (char *)low_alloc((proven_size_t)nn + 1);
        if (!fixed) { if (big) low_free(big); return -1; }
        char *o = fixed; const char *q = p, *hit;
        while ((hit = strstr(q, MINLL))) { memcpy(o, q, (size_t)(hit - q)); o += hit - q; memcpy(o, FIXLL, sizeof FIXLL - 1); o += sizeof FIXLL - 1; q = hit + sizeof MINLL - 1; }
        size_t rest = (size_t)(p + need - q); memcpy(o, q, rest); o += rest; *o = 0;   // 남은 길이는 잰 길이에서 — 글 안의 0 바이트에 기대지 않는다
        p = fixed; need = (int)nn;
    }
    size_t w = need > 0 ? low_write(f, p, (size_t)need) : 0;
    if (fixed) low_free(fixed);
    if (big) low_free(big);
    return w == (size_t)need ? need : -1;
}
