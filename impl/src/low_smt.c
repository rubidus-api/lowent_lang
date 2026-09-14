// low_smt.c — λ 를 추적하는 Fourier–Motzkin 소거. 자세한 설계 근거는 low_smt.h 를 볼 것.
//
// ★ 이 파일이 지키는 규율 하나: **넘치면 포기한다.** 포기 = 검사를 안 지운다 = 프로그램이
//   느려질 뿐 틀리지 않는다. 반대로 넘친 것을 잘라 계속 밀면 **없는 사실로 증명**하게 된다.
#include "low_smt.h"

#include <string.h>

void low_smt_init(low_smt_sys_t *s, proven_size_t nv) {
    memset(s, 0, sizeof(*s));
    s->nv = nv > LOW_SMT_MAXV ? LOW_SMT_MAXV : nv;
}

bool low_smt_add(low_smt_sys_t *s, const proven_i64 *a, proven_i64 c) {
    if (s->nc >= LOW_SMT_MAXC) return false;
    low_smt_row_t *r = &s->row[s->nc];
    for (proven_size_t i = 0; i < s->nv; i++) r->a[i] = a[i];
    r->c = c;
    s->nc++;
    return true;
}

// ── 넘침을 겁내는 산술 ────────────────────────────────────────────────────────
// ★★ 여기서 넘치면 **증명이 거짓이 된다**(모듈로 감싼 수로 모순을 만들어 낸다). 그래서 곱셈과
//   덧셈이 전부 검사된다. 넘치면 그 행을 **버리는 것이 아니라 절차 전체를 포기**한다 —
//   버리면 "약해진 계" 가 아니라 "다른 계" 가 되기 때문이다.
#define SMT_LIM 4000000000000000LL      /* 4e15 — i64 여유를 크게 남긴다 */

static bool sm_mul(proven_i64 x, proven_i64 y, proven_i64 *out) {
    if (x > SMT_LIM || x < -SMT_LIM || y > SMT_LIM || y < -SMT_LIM) return false;
    if (x != 0 && (y > SMT_LIM / (x < 0 ? -x : x) || y < -(SMT_LIM / (x < 0 ? -x : x))))
        return false;
    *out = x * y;
    return true;
}
static bool sm_add(proven_i64 x, proven_i64 y, proven_i64 *out) {
    if ((y > 0 && x > SMT_LIM - y) || (y < 0 && x < -SMT_LIM - y)) return false;
    *out = x + y;
    return true;
}

// 소거 도중의 행: 계수 + 상수 + **어디서 왔는지**(원래 제약에 대한 λ).
typedef struct {
    proven_i64 a[LOW_SMT_MAXV];
    proven_i64 c;
    proven_i64 lam[LOW_SMT_MAXC];
} smt_wrow_t;

static proven_i64 sm_gcd(proven_i64 x, proven_i64 y) {
    if (x < 0) x = -x;
    if (y < 0) y = -y;
    while (y) { proven_i64 t = x % y; x = y; y = t; }
    return x ? x : 1;
}

// ★ 행을 **약분한다.** 안 하면 소거를 몇 번 거치는 사이 계수가 곱해져 금세 넘친다.
//   약분은 부등식의 뜻을 바꾸지 않는다(양수로 나누므로) — λ 도 같은 수로 나눈다.
static void sm_reduce(smt_wrow_t *r, proven_size_t nv, proven_size_t nc) {
    proven_i64 g = 0;
    for (proven_size_t i = 0; i < nv; i++) g = sm_gcd(g, r->a[i]);
    g = sm_gcd(g, r->c);
    for (proven_size_t j = 0; j < nc; j++) g = sm_gcd(g, r->lam[j]);
    if (g <= 1) return;
    for (proven_size_t i = 0; i < nv; i++) r->a[i] /= g;
    r->c /= g;
    for (proven_size_t j = 0; j < nc; j++) r->lam[j] /= g;
}

bool low_smt_check(const low_smt_sys_t *s, const proven_i64 *lam) {
    // ★★★ **모순을 처음부터 다시 만든다** — 이 함수가 곧 검증기가 할 일의 C 판본이다.
    //   ① 계수는 음이 아니어야 한다(부등식에 음수를 곱하면 부호가 뒤집힌다).
    //   ② 변수 계수의 합이 **전부 0** 이어야 한다(변수가 남아 있으면 모순이 아니다).
    //   ③ 상수의 합이 **양수**여야 한다 ⇒ `(양수) ≤ 0` 이라는 거짓이 나온다.
    proven_i64 acc[LOW_SMT_MAXV];
    memset(acc, 0, sizeof(acc));
    proven_i64 cc = 0;
    bool any = false;
    for (proven_size_t j = 0; j < s->nc; j++) {
        if (lam[j] < 0) return false;                       // ①
        if (lam[j] == 0) continue;
        any = true;
        for (proven_size_t i = 0; i < s->nv; i++) {
            proven_i64 t;
            if (!sm_mul(lam[j], s->row[j].a[i], &t)) return false;
            if (!sm_add(acc[i], t, &acc[i])) return false;
        }
        proven_i64 t;
        if (!sm_mul(lam[j], s->row[j].c, &t)) return false;
        if (!sm_add(cc, t, &cc)) return false;
    }
    if (!any) return false;
    for (proven_size_t i = 0; i < s->nv; i++) if (acc[i] != 0) return false;   // ②
    return cc > 0;                                                              // ③
}

bool low_smt_refute(const low_smt_sys_t *s, proven_i64 *lam) {
    if (!s->nc) return false;

    static smt_wrow_t cur[LOW_SMT_MAXROW], nxt[LOW_SMT_MAXROW];
    proven_size_t ncur = 0;

    for (proven_size_t j = 0; j < s->nc && ncur < LOW_SMT_MAXROW; j++) {
        smt_wrow_t *r = &cur[ncur++];
        memset(r, 0, sizeof(*r));
        for (proven_size_t i = 0; i < s->nv; i++) r->a[i] = s->row[j].a[i];
        r->c = s->row[j].c;
        r->lam[j] = 1;                       // 자기 자신에서 왔다
    }

    // ★★★ **변수를 하나씩 없앤다.** v 의 계수가 양수인 행(상계)과 음수인 행(하계)을 짝지어
    //   더하면 v 가 사라진다. 그 쌍이 없으면(한쪽만 있으면) 그 행들은 v 에 대해 아무 말도 못
    //   하므로 **버린다** — 이것이 FM 이 건전한 이유다(버려진 행은 모순에 기여할 수 없다).
    for (proven_size_t v = 0; v < s->nv; v++) {
        proven_size_t nn = 0;
        // 계수가 0 인 행은 그대로 통과
        for (proven_size_t k = 0; k < ncur; k++)
            if (cur[k].a[v] == 0) {
                if (nn >= LOW_SMT_MAXROW) return false;
                nxt[nn++] = cur[k];
            }
        for (proven_size_t p = 0; p < ncur; p++) {
            if (cur[p].a[v] <= 0) continue;
            for (proven_size_t q = 0; q < ncur; q++) {
                if (cur[q].a[v] >= 0) continue;
                if (nn >= LOW_SMT_MAXROW) return false;     // ★ 넘치면 **포기**한다
                proven_i64 wp = -cur[q].a[v];               // > 0
                proven_i64 wq =  cur[p].a[v];               // > 0
                smt_wrow_t *r = &nxt[nn];
                memset(r, 0, sizeof(*r));
                for (proven_size_t i = 0; i < s->nv; i++) {
                    proven_i64 t1, t2;
                    if (!sm_mul(wp, cur[p].a[i], &t1)) return false;
                    if (!sm_mul(wq, cur[q].a[i], &t2)) return false;
                    if (!sm_add(t1, t2, &r->a[i]))     return false;
                }
                {
                    proven_i64 t1, t2;
                    if (!sm_mul(wp, cur[p].c, &t1)) return false;
                    if (!sm_mul(wq, cur[q].c, &t2)) return false;
                    if (!sm_add(t1, t2, &r->c))     return false;
                }
                for (proven_size_t j = 0; j < s->nc; j++) {
                    proven_i64 t1, t2;
                    if (!sm_mul(wp, cur[p].lam[j], &t1)) return false;
                    if (!sm_mul(wq, cur[q].lam[j], &t2)) return false;
                    if (!sm_add(t1, t2, &r->lam[j]))     return false;
                }
                sm_reduce(r, s->nv, s->nc);
                nn++;
            }
        }
        memcpy(cur, nxt, sizeof(smt_wrow_t) * nn);
        ncur = nn;
        if (!ncur) return false;

        // ★ 변수를 없앨 때마다 **모순이 이미 나왔는지** 본다 — 끝까지 갈 필요가 없다.
        for (proven_size_t k = 0; k < ncur; k++) {
            bool zero = true;
            for (proven_size_t i = 0; i < s->nv; i++) if (cur[k].a[i] != 0) { zero = false; break; }
            if (zero && cur[k].c > 0) {
                for (proven_size_t j = 0; j < s->nc; j++) lam[j] = cur[k].lam[j];
                // ★★ **자기 검산**: 여기서 한 번 더 곱해 더해 본다. 이 줄이 없으면 소거의
                //   버그가 그대로 증명서가 되고, 그것을 잡는 것은 (있다면) 검증기뿐이다.
                return low_smt_check(s, lam);
            }
        }
    }
    return false;
}
