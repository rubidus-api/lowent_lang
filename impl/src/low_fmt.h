#ifndef LOW_FMT_H
#define LOW_FMT_H
/* ★★ 처리기의 글자 짓기 — proven_c_lib 의 포매터로 (2026-11-12 · W-20261112-0207 S2 · 소유자 «printf/sprintf/snprintf/fprintf 류 … 프로븐 쪽
 *    입출력 포매팅 함수로»).
 *
 *   low_bfmt(buf, cap, "…{}…", PROVEN_ARG((int)(x)), …)  — `snprintf(buf, cap, "…%d…", x)` 의 자리. 서식은 proven 의 `{}`.
 *     · 빌린 u8str(`proven_u8str_borrow`)에 `proven_u8str_append_fmt_trunc` 로 쓴다 — 넘치면 자르고, 늘 NUL 로 끝난다.
 *     · 돌려주는 값은 snprintf 와 같다: 잘리지 않았다면 썼을 바이트 수(NUL 빼고). `o += low_bfmt(...)` · `n >= cap` 가 그대로 선다.
 *     · cap 이 0 이면 아무것도 안 쓰고 필요한 바이트만 센다(snprintf(NULL, 0, …) 처럼).
 *     · 서식이 틀렸거나 인자 수가 안 맞으면 **멈춘다**(low_bfmt_fail). snprintf 는 그럴 때 반쯤 쓴 글을 냈고 proven 은 빈 글을 낸다 —
 *       조용히 빈 오류 메시지를 내느니 그 자리를 말하고 멈춘다. 서식은 모두 scripts/snprintf-to-proven.py 가 옮겼고, 그 대조 시험
 *       (scripts/snprintf-to-proven.py --harness)이 자리마다 C 의 snprintf 와 바이트를 견준다.
 *   low_sv(p, n)  — `%.*s` 의 (n, p) 한 쌍. printf 처럼 n 바이트 안의 첫 NUL 에서 멈추고, n 이 음수면 NUL 까지다.
 *
 *   C 와 다른 것(받아들였다 — docs/known-defects 가 아니라 VENDORED.md 에 적었다):
 *     · 버퍼가 모자라 자를 때와 `%.Ns` 의 N 에서 자를 때, proven 은 UTF-8 글자를 반으로 가르지 않는다(C 는 바이트에서 자른다).
 *     · `%s` 에 NULL — C 에서 정의되지 않은 동작(glibc 는 `(null)`), proven 은 아무것도 안 쓴다.
 *   ★ 이 머리글은 proven 의 `proven_u8str_fmt_internal` 을 매크로로 부른다 — fmt.h 가 «MINOR 판에서 바뀔 수 있다» 고 적은 것이다.
 *     다시 들여올 때 그 서명을 본다(VENDORED.md).
 */
#include <stddef.h>
#include <string.h>
#include "proven/fmt.h"

[[noreturn]] void low_bfmt_fail(const char *fmt, proven_err_t err);

static inline int low_bfmt_end_(proven_fmt_result_t r, const char *fmt) {
    if (!proven_is_ok(r.err) && r.err != PROVEN_ERR_OUT_OF_BOUNDS) low_bfmt_fail(fmt, r.err);
    return r.required > (proven_size_t)0x7fffffff ? 0x7fffffff : (int)r.required;
}

#define low_bfmt(buf, cap, fmt, ...) \
    low_bfmt_end_(proven_u8str_append_fmt_trunc( \
        ((proven_u8str_t[]){ proven_u8str_borrow((proven_byte_t *)(buf), (proven_size_t)(cap)) }), \
        fmt __VA_OPT__(,) __VA_ARGS__), fmt)

static inline proven_arg_t low_sv(const void *p, int n) {
    if (!p) return proven_arg_str_view((proven_u8str_view_t){ (const proven_u8 *)"", 0 });
    size_t k = n < 0 ? strlen((const char *)p) : strnlen((const char *)p, (size_t)n);
    return proven_arg_str_view((proven_u8str_view_t){ (const proven_u8 *)p, (proven_size_t)k });
}

/* ★ P3 (W-20261112-0207 · 소유자 «스택의 배열 버퍼 쓰는 부분도 잘 검토해서 proven 쪽으로») — 스택 칸에 글을 손으로 베끼던 자리
 *   (`memcpy(nb, v.ptr, n); nb[n] = 0;` · `a` `.` `b` 를 이어 붙이기)를 빌린 u8str 로 한다. 칸은 그대로 스택에 있고, 쓰기만 proven 이
 *   경계를 지킨다.
 *
 *   low_bcopy(buf, cap, p, n) — n 바이트를 NUL 로 끝나게 베낀다. 넘치면 **바이트에서** 자른다(`proven_u8str_append_partial` — 전의 손
 *     복사와 같은 자리에서 자른다). 돌려주는 값은 베낀 바이트 수. cap 이 0 이면 아무것도 안 쓴다.
 *   low_qname(buf, cap, a, b, out) — `a.b` 를 짓는다(자격 이름). cap 에 NUL 몫까지 들어가야 하고, 안 들어가면 false 이고 아무것도
 *     안 쓴다. 지은 글은 *out 에 보기로 준다. */
static inline proven_size_t low_bcopy(void *buf, size_t cap, const void *p, size_t n) {
    if (!buf || !cap) return 0;
    proven_u8str_t s = proven_u8str_borrow((proven_byte_t *)buf, (proven_size_t)cap);
    if (!n || !p) return 0;
    proven_result_size_t r = proven_u8str_append_partial(&s, (proven_u8str_view_t){ (const proven_u8 *)p, (proven_size_t)n });
    return r.value;
}
static inline bool low_qname(void *buf, size_t cap, proven_u8str_view_t a, proven_u8str_view_t b, proven_u8str_view_t *out) {
    if (!buf || a.size + 1 + b.size >= cap) return false;
    proven_u8str_t s = proven_u8str_borrow((proven_byte_t *)buf, (proven_size_t)cap);
    if (!proven_is_ok(proven_u8str_append(&s, a)) || !proven_is_ok(proven_u8str_append(&s, (proven_u8str_view_t){ (const proven_u8 *)".", 1 })) ||
        !proven_is_ok(proven_u8str_append(&s, b))) return false;
    *out = proven_u8str_as_view(&s);
    return true;
}

/* 진단 한 줄을 고정 칸(`detail[]`)에 짓는 가변 인자 함수(tc_emitf · mx_fail)가 쓴다: 인자 배열을 그대로 넘긴다. */
#define LOW_FMT_ARGS(...) ((const proven_arg_t[]){ proven_arg_none() __VA_OPT__(,) __VA_ARGS__ }), \
    (sizeof((proven_arg_t[]){ proven_arg_none() __VA_OPT__(,) __VA_ARGS__ }) / sizeof(proven_arg_t))
static inline int low_bfmt_args(char *buf, size_t cap, const char *fmt, const proven_arg_t *args, proven_size_t n) {
    proven_u8str_t s = proven_u8str_borrow((proven_byte_t *)buf, (proven_size_t)cap);
    return low_bfmt_end_(proven_u8str_fmt_internal((proven_allocator_t){0}, &s, true, fmt, (proven_allocator_t){0}, args, n), fmt);
}

/* ★ P4 단계 A (W-20261112-0207 · 소유자 «printf/sprintf/snprintf/fprintf 류 … 프로븐 쪽 입출력 포매팅 함수로») — 흐름(FILE *)에 쓰는 자리.
 *   low_fpf(f, "…{}…", PROVEN_ARG(...), …)  — `fprintf(f, "…%d…", x)` 의 자리.   low_pf(…) — `printf(…)` 의 자리(stdout).
 *     · proven 이 글을 짓는다: 1 KiB 스택 칸에, 넘치면 proven 힙 칸에(잘리지 않는다 — fprintf 처럼 길이 한도가 없다).
 *     · 지은 바이트는 **같은 FILE * 에 fwrite** 한다. 흐름은 이 단계에서 그대로다 — VM 이 내는 프로그램 출력 · 자식 프로세스 ·
 *       exit 경로와의 순서가 안 바뀐다(흐름을 proven 쪽으로 바꾸는 것은 단계 B).
 *     · 돌려주는 값은 fprintf 처럼 쓴 바이트 수, 쓰기가 모자라면 -1. 서식이 틀리면 low_bfmt 처럼 멈춘다.
 *   서식은 scripts/snprintf-to-proven.py 가 옮겼다(fprintf · printf 도 같은 캐스트 규칙). */
#include <stdio.h>
int low_fpf_args(FILE *f, const char *fmt, const proven_arg_t *args, proven_size_t n);
#define low_fpf(f, fmt, ...) low_fpf_args((f), fmt, LOW_FMT_ARGS(__VA_ARGS__))
#define low_pf(fmt, ...) low_fpf_args(stdout, fmt, LOW_FMT_ARGS(__VA_ARGS__))
#endif
