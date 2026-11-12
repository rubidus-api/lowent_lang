/* ★★ low_fmt.h 의 ★ — 서식이 틀린 자리는 멈춘다. */
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
    size_t w = need > 0 ? low_write(f, p, (size_t)need) : 0;
    if (big) low_free(big);
    return w == (size_t)need ? need : -1;
}
