/* ★★ low_fmt.h 의 ★ — 서식이 틀린 자리는 멈춘다. */
#include <stdio.h>
#include <stdlib.h>
#include "low_fmt.h"

void low_bfmt_fail(const char *fmt, proven_err_t err) {
    fputs("lowentc: internal error — a format string does not match its arguments (proven error ", stderr);
    fprintf(stderr, "%d): \"%s\"\n", (int)err, fmt);
    abort();
}
