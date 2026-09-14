/* ★ 진짜 C 코드 — Lowent 가 이것을 부르고, 이것이 Lowent 를 부른다. */
#include <stddef.h>
#include <stdint.h>
#include <stdarg.h>
#include <stdlib.h>

/* ★★★ **cstr 를 돌려준다** (RFC-0068 S4 · C1) — C 는 아직 **문자열에선 섬**이다. 이것이 그 다리다:
   C 가 널종단 char* 를 주고, Lowent 가 `str_from_cstr` 로 길이를 **스캔**해 str 로 만든다.
   문자열 리터럴은 정적 저장이라 수명이 프로그램 전체다(뷰가 안전하다). 길이는 13. */
const char *lw_c_greeting(void) { return "hello, lowent"; }
/* ★ 빈 문자열도 정직하게 — strlen 0 → len 0 (자르지 않는다, none 도 아니다). */
const char *lw_c_empty(void) { return ""; }
/* ★★★ **C 가 cstr 를 받는다** (RFC-0068 S4 · C4) — str_buf 가 봉인한 널종단 버퍼를 Lowent 가
   `char*` 하나로 넘긴다(길이 없이). 여기선 진짜 libc `strlen` 을 그대로 부른다. */
#include <string.h>
long long lw_c_slen(const char *s) { return (long long)strlen(s); }
/* ★ 첫 바이트를 돌려준다 — 포인터가 진짜 그 바이트를 가리키는지(오프셋 안 밀렸는지) 확인용. */
long long lw_c_first(const char *s) { return (long long)(unsigned char)s[0]; }

/* ★ getenv 격 — 진짜 libc 를 부른다(RFC 가 든 예). 값은 환경 의존이라 골든은 위 고정
   문자열로 재고, 이것은 "실제 char* 반환 libc 함수가 그대로 다리를 탄다"는 시연이다. */
const char *lw_c_getenv(const unsigned char *name, size_t n) {
    static char buf[256];
    size_t m = n < sizeof buf - 1 ? n : sizeof buf - 1;
    for (size_t i = 0; i < m; i++) buf[i] = (char)name[i];
    buf[m] = 0;
    return getenv(buf);
}
long long lw_c_add(long long a, long long b) { return a + b; }
long long lw_c_sum(const unsigned char *p, size_t n) {
    long long t = 0;
    for (size_t i = 0; i < n; i++) t += p[i];
    return t;
}
/* ★ f64 를 받고 f64 를 돌려준다 — extern 의 반환이 예전엔 언제나 long long 으로
   선언·수신돼 경계에서 타입이 어긋났다(UB). 이제 double 로 오간다. */
double lw_c_scale2(double x) { return x * 2.0; }

/* ★ **넓은 슬라이스** — `slice u32` 가 예전엔 `const unsigned char *` 로 넘어왔다(폭 무시).
   이제 `const uint32_t *` 로, 길이는 **원소 수**로 정직하게 온다. */
long long lw_c_sum32(const uint32_t *p, size_t n) {
    long long t = 0;
    for (size_t i = 0; i < n; i++) t += p[i];
    return t;
}

/* ★ **구조체 by-pointer** (`unsafe_ptr`) — C 가 Lowent 구조체의 바이트를 **읽고 쓴다**.
   C 는 자기 struct 정의로 캐스팅한다(레이아웃 일치는 unsafe — 이름이 그렇게 말한다). */
struct pt { long long x, y; };
long long lw_c_ptsum(void *raw)  { struct pt *p = raw; return p->x + p->y; }   /* 읽기 */
long long lw_c_ptbump(void *raw) { struct pt *p = raw; p->x += 10; return p->x; } /* 쓰기 */

/* ★ **구조체 by-value** — C 가 struct 를 **값으로** 받는다(SysV 분류는 컴파일러가). C 는 자기
   struct pt 로 받고, 생성된 C 는 struct lw_sty_<idx> 로 넘긴다 — 레이아웃이 같아 ABI 로 링크된다. */
long long lw_c_area(struct pt p) { return p.x * p.y; }

/* ★ **콜백** — 씨가 Lowent 함수를 **함수 포인터로 부른다**(`unsafe_fn`). f 는 export extern
   op 의 경계 래퍼(계약 검사 포함). 여기선 f(x)+1 을 돌려준다. */
long long lw_c_apply(long long (*f)(long long), long long x) { return f(x) + 1; }

/* ★ **조합** — 콜백 + 가변인자를 한 함수에. f 를 각 가변 인자에 적용해 더한다.
   (가변인자 호출 경로가 콜백 인자를 int 로 오취급하던 조합 버그를 잡은 자리 — 교훈 6.) */
long long lw_c_apply_sum(long long (*f)(long long), long long n, ...) {
    va_list ap; va_start(ap, n);
    long long s = 0;
    for (long long i = 0; i < n; i++) s += f(va_arg(ap, long long));
    va_end(ap);
    return s;
}

/* ★ **가변인자 C 함수** — n 개의 정수 가변 인자를 더한다(`int sum(int n, ...)` 모양). */
long long lw_c_vsum(long long n, ...) {
    va_list ap; va_start(ap, n);
    long long s = 0;
    for (long long i = 0; i < n; i++) s += va_arg(ap, long long);
    va_end(ap);
    return s;
}
