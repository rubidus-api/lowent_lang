/* ★★★ **남의 자리에 앉아 보는 시험** — 구판 헤더로 지은 C 가 신판 오브젝트와 링크되는가.
 *      (RFC-0089 §8-1 ③ · 후속 G)
 *
 *   ☞ 이 파일이 include 하는 것은 **커밋된 기준선**(`docs/abi-surface.h`)이다 — 방금 방출한
 *     헤더가 아니다. 그것이 이 시험의 전부다: *"어제 우리 헤더를 믿고 컴파일한 사람이,
 *     오늘 우리가 낸 오브젝트와 링크해서 여전히 도는가."*
 *
 *   ★ 그래서 이 파일은 **일부러 다시 만들지 않는다.** 기준선이 바뀌면 이 소비자가
 *     컴파일에 실패하거나 링크에 실패하거나 답이 달라진다 — 셋 다 게이트가 본다.
 *
 *   ★★ 두 방향을 한 번에 민다:
 *     · **나가는 쪽**(`export`) — 여기서 부른다. 심볼이 없어지거나 시그니처가 바뀌면 링크가 깨진다.
 *     · **들어오는 쪽**(`extern`) — 여기서 **정의한다**. 헤더가 약속한 프로토타입과 어긋나면
 *       `cc` 가 그 자리에서 거절한다(그게 헤더가 존재하는 이유다).
 *
 *   ☞ 정직: 이 소비자는 **진짜 제3자가 아니다** — 우리가 둔 대역이다. 그래서 §8-1 ③ 은
 *     "밖에서 온 증거" 가 아니라 "밖에서 왔다면 겪었을 일" 을 잰다. 그 한계는 RFC 에 적혀 있다.
 */
#include "abi-surface.h"
#include <stdio.h>
#include <string.h>

/* ── 들어오는 쪽: 헤더가 선언한 대로 **C 가 정의한다** ─────────────────────────── */
static long long sum_i8 (const int8_t   *p, size_t n) { long long s = 0; for (size_t i = 0; i < n; i++) s += p[i]; return s; }
long long abi_c_i8 (const int8_t   *p, size_t n) { return sum_i8(p, n); }
long long abi_c_i16(const int16_t  *p, size_t n) { long long s = 0; for (size_t i = 0; i < n; i++) s += p[i]; return s; }
long long abi_c_i32(const int32_t  *p, size_t n) { long long s = 0; for (size_t i = 0; i < n; i++) s += p[i]; return s; }
long long abi_c_i64(const int64_t  *p, size_t n) { long long s = 0; for (size_t i = 0; i < n; i++) s += p[i]; return s; }
long long abi_c_u8 (const uint8_t  *p, size_t n) { long long s = 0; for (size_t i = 0; i < n; i++) s += p[i]; return s; }
long long abi_c_u16(const uint16_t *p, size_t n) { long long s = 0; for (size_t i = 0; i < n; i++) s += p[i]; return s; }
long long abi_c_u32(const uint32_t *p, size_t n) { long long s = 0; for (size_t i = 0; i < n; i++) s += p[i]; return s; }
long long abi_c_u64(const uint64_t *p, size_t n) { long long s = 0; for (size_t i = 0; i < n; i++) s += (long long)p[i]; return s; }
double    abi_c_scalars(long long a, double b)   { return (double)a + b; }
long long abi_c_void(void)                       { return 11; }
const char *abi_c_cstr(void)                     { return "cstr"; }
long long abi_c_pt(struct lw_sty_0 p)            { return p.f0 + p.f1; }

int main(void) {
    /* ── 나가는 쪽: 구판 헤더의 선언을 믿고 부른다 ───────────────────────────── */
    const unsigned char buf[5] = { 1, 2, 3, 4, 5 };

    long long a = abi_ret_int(41);                    /* 42  */
    double    b = abi_ret_flt(1.5);                   /* 3.0 */
    long long c = abi_no_params();                    /* 7   */
    long long d = abi_slice_param(buf, sizeof buf);   /* 5   */
    double    e = abi_many(2, 0.5, buf, sizeof buf);  /* 7.5 */
    long long f = abi_clamp255(1000);                 /* 255 */

    /* 들어오는 쪽 정의도 한 번씩 불러 본다 — 링크가 아니라 **표현**이 맞는지 본다. */
    const int32_t w[3] = { 100, -1, -2 };
    long long g = abi_c_i32(w, 3);                    /* 97  */
    double    h = abi_c_scalars(3, 0.25);             /* 3.25 */
    struct lw_sty_0 pt = { 20, 22 };
    long long i = abi_c_pt(pt);                       /* 42  */
    long long j = (long long)strlen(abi_c_cstr());    /* 4   */

    printf("%lld %.2f %lld %lld %.2f %lld %lld %.2f %lld %lld\n", a, b, c, d, e, f, g, h, i, j);
    return 0;
}
