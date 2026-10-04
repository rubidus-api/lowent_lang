/* low_iv.c — 구간 분석(interval/range analysis)과 SMT 질의.
 *
 * ★ `low_ir.c` 에서 **떼어 왔다** (2026-08-31, WO-0164 · X-0011). 뜻은 한 줄도 안 바꿨다 —
 *   옮기기만 했다. 증명은 `scripts/check-emit-identical.py`(코퍼스 342 단위 바이트 동일).
 *
 * ★★ 여기 사는 것: op-국소 전방 추상해석 → 기본블록 CFG + 고정점 + **분기 조건 narrowing**,
 *   그리고 남은 검사를 솔버에 묻는 자리(`low_smt.h`). *"이 인덱스가 범위 안임을 아는가"* 를
 *   답하는 층이고, **하강도 VM 도 이것을 부르지 않는다**(나가는 심볼 0).
 *
 * ★★★ 들여오는 43 개는 두 종류다 — **공용 어휘**(`veq`·`is_atom`·`ir_fail` 같은, 어느 조각을
 *   떼든 따라오는 낱말)와 **진짜 결합**(op 표 · 계약 진입). `low_ir_priv.h` 가 그 둘을
 *   나누어 적는다. 짧아야 하는 것은 **결합**이지 어휘가 아니다.
 */
#include "low_ir_priv.h"

#include "low_hwm.h"
#include "low_ir.h"
#include "low_ir_priv.h"
#include "low_arity.h"
#include <limits.h>
#include <stdarg.h>   // ★ 증명 운반 검사(cert_put)의 가변 인자
#include <math.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "low_hostfault.inc"
#include <dirent.h>     // ★ 디렉터리 순회 (RFC-0069 §6 — opendir/readdir/closedir)
#include "low_sha256.h"
#include "low_sha512.h"        // ★ SHA-256 **한 벌** — VM 은 컴파일, 백엔드는 문자열화
#include "proven_sys_random.h" // ★ 난수 리프 (RFC-0090 N3b) — OS 엔트로피 한 자리
#include "proven_sys_time.h"   // ★ 시계 리프 (RFC-0090 N1) — proven_c_lib 이 이미 준다
#include <sys/socket.h> // ★ 소켓 리프 (socketpair/send/recv — cap net)
#include <netinet/in.h> // ★ 네트워크 면 (sockaddr_in · TCP loopback)
#include <arpa/inet.h>  // ★ htons/htonl
#include <unistd.h>     // ★ close (소켓/파일 fd)
#include <sys/stat.h>   // ★ 파일 타입 질의 (RFC-0069 §6 — stat/S_ISDIR/S_ISREG)
#include <errno.h>      // ★ readdir 의 끝(NULL·errno==0) vs 오류(NULL·errno!=0) 를 가른다
#include <termios.h>    // ★ cap tty — raw 모드(에코·행버퍼 끄기)
#include <sys/ioctl.h>  // ★ cap tty — 화면 크기(TIOCGWINSZ)
#include <ucontext.h>   // ★★★ green thread — 태스크 중단(yield)을 위한 코루틴(VM 오라클 전용)
#include "low_token.h"
#include "low_diag.h"
#include "low_blake3.h"
#include "low_smt.h"    /* ★ REQ-0004: 손으로 넣던 관계 특수경우들을 **일반 절차**로 (후속 M) */

// ── RFC-0053: 구간 분석 (op-국소 전방 추상해석) ──────────────────────────────
// 각 정수 값에 구간 [lo, hi] 를 준다. 출처: 리터럴 · 선언 타입의 range · **requires 절**.
// 결과 구간이 선언 타입의 range 안에 있음이 증명되면 IR_POL_PROVEN 을 세워
// **오버플로 검사를 제거 가능**으로 표시한다(RFC-0053 E2/E3).
//
// 건전성 방향이 요점이다: "제거 가능"이라 말한 자리에서 실제로 넘치면 그것은
// **분석의 오류**다. VM 은 검사를 유지하고 그 경우 E-VM-ANALYSIS 로 고발한다 —
// 그래서 기존 퍼저가 곧 이 분석의 건전성 검증기가 된다(§7).
// ★★★★★ **`wide` — 이 상계는 진짜인가, 자른 것인가** (2026-08-17, PERF-0003).
//
//   도메인이 `proven_i64` 라 **u64 의 위쪽 절반을 못 담는다**. 그래서 `iv_ty(u64)` 는
//   `[0, INT64_MAX]` 를 주는데 그것은 **자른 값**이다(진짜 u64 는 2^63 을 넘을 수 있다).
//   그 자른 상계에 기대어 "넘치지 않는다" 고 결론하면 **불건전**하다 — 그래서 `iv_in` 은
//   64비트에서 아예 결론을 거부했다. 옳은 판단이었지만 **대가가 컸다**: 이 언어의 자연스러운
//   정수 타입이 `u64` 라, `requires le i 1000 .` 을 써도 `add i 1` 이 안 지워졌다(실증).
//   matmul 3/18 · sieve 1/11 · sched 7/16 · sort 0/22 가 **전부 그 한 줄** 때문이었다.
//
//   ⇒ 거부하는 대신 **가른다**: `wide = 1` 은 *"이 hi 는 타입에서 잘라 온 것이라 못 믿는다"*.
//     · `iv_ty` 가 **64비트 무부호**에만 켠다 — 켜는 자리는 **여기 하나**다(감사 가능).
//     · 산술·격자가 **전파**한다(하나라도 자른 것이면 결과도 자른 것).
//     · 계약·리터럴·분기로 **좁혀지면 꺼진다** — 그때의 hi 는 진짜 사실에서 왔다.
//   ★ 기본값 0(= 진짜)이 안전한 이유: 자른 값을 만드는 자리가 `iv_ty` **뿐**이기 때문이다.
//     다른 모든 `(iv_t){lo,hi}` 는 상수·계약·계산에서 오고, 그 hi 는 실제 값이다.
/* iv_t — low_ir_priv.h (계약→시험 절이 쓴다) */

 const iv_t IV_TOP = { INT64_MIN, INT64_MAX, 0 };
static const iv_t IV_LEN = { 0, INT64_MAX, 0 };   // 길이는 결코 음수가 아니다
static iv_t iv_of(proven_i64 v) { return (iv_t){ v, v }; }
 iv_t iv_ty(ityp_t t) {
    if (!t.known || t.flt || !t.bits) return IV_TOP;
    if (t.has_rng) return (iv_t){ t.rlo, t.rhi };   // ★ RFC-0055: 범위가 폭보다 정확하다
    // ★ 64비트 무부호의 hi 는 **자른 값**이다 — 그렇다고 표시한다(위 `wide` 주석).
    return (iv_t){ ity_lo(t.bits, t.sign),
                   t.bits >= 64 && !t.sign ? INT64_MAX : ity_hi(t.bits, t.sign),
                   (proven_u8)(t.bits >= 64 && !t.sign && !t.has_rng) };
}
static bool iv_in(iv_t v, ityp_t t) {
    if (!t.known || t.flt || !t.bits) return false;
    if (t.bits >= 64) {
        // ★★★ **자른 상계에 기대지 않았다면** 64비트도 결론할 수 있다 (2026-08-17).
        //   · `wide` 면 hi 가 타입에서 잘라 온 것이라 못 믿는다 ⇒ 거부(옛 동작).
        //   · 아니면 hi 는 계약·리터럴·계산에서 온 **진짜 값**이고 i64 안에 있다
        //     (`iv_add/sub/mul` 이 i64 오버플로를 이미 걸렀다).
        //       무부호: [0, 2^64−1] ⊇ [lo, hi] 는 `lo >= 0` 이면 성립한다.
        //       부호  : [INT64_MIN, INT64_MAX] ⊇ 표현 가능한 모든 구간 — 성립한다.
        if (v.wide) return false;
        return t.sign ? true : v.lo >= 0;
    }
    return v.lo >= ity_lo(t.bits, t.sign) && v.hi <= ity_hi(t.bits, t.sign);
}
static bool iv_add(iv_t a, iv_t b, iv_t *r) {   // 오버플로 없이 더할 수 있으면 true
    proven_i64 lo, hi;
    if (__builtin_add_overflow(a.lo, b.lo, &lo) || __builtin_add_overflow(a.hi, b.hi, &hi)) return false;
    *r = (iv_t){ lo, hi, (proven_u8)(a.wide || b.wide) }; return true;   // ★ 자름은 전염된다
}
static bool iv_sub(iv_t a, iv_t b, iv_t *r) {
    proven_i64 lo, hi;
    if (__builtin_sub_overflow(a.lo, b.hi, &lo) || __builtin_sub_overflow(a.hi, b.lo, &hi)) return false;
    *r = (iv_t){ lo, hi, (proven_u8)(a.wide || b.wide) }; return true;
}
static bool iv_mul(iv_t a, iv_t b, iv_t *r) {
    proven_i64 p[4];
    if (__builtin_mul_overflow(a.lo, b.lo, &p[0]) || __builtin_mul_overflow(a.lo, b.hi, &p[1]) ||
        __builtin_mul_overflow(a.hi, b.lo, &p[2]) || __builtin_mul_overflow(a.hi, b.hi, &p[3])) return false;
    proven_i64 lo = p[0], hi = p[0];
    for (int i = 1; i < 4; i++) { if (p[i] < lo) lo = p[i]; if (p[i] > hi) hi = p[i]; }
    *r = (iv_t){ lo, hi }; return true;
}

// `requires` 절이 구간을 좁힌다 — ★ 계약과 분석의 접점(RFC-0053 §6.2).
// 지원 형태: lt/le/gt/ge/ne  <local>  <int literal>
// ★★ 한 requires 삼중항(cmp · 이름 · 리터럴)을 시그니처 구간에 얹는다.
static void iv_sig_narrow(ir_ctx_t *c, iv_t *sig, proven_u8str_view_t op,
                          proven_u8str_view_t name, proven_u8str_view_t lit) {
    bool found; proven_size_t slot = ir_local_find(c, name, &found);
    proven_i64 n;
    if (!found || slot >= IR_MAXLOCALS || !ir_int_lit(lit, &n)) return;
    iv_t *v = &sig[slot];
    if      (veq(op, "lt") && n > INT64_MIN) { if (n - 1 < v->hi) v->hi = n - 1; }
    else if (veq(op, "le"))                  { if (n     < v->hi) v->hi = n;     }
    else if (veq(op, "gt") && n < INT64_MAX) { if (n + 1 > v->lo) v->lo = n + 1; }
    else if (veq(op, "ge"))                  { if (n     > v->lo) v->lo = n;     }
}
// ★★★ **명명 계약도 계약이다** — 그런데 구간 분석은 그것을 **못 보고 있었다.**
//   `satisfies N` 의 requires 는 IR 이 진입 검사로 **내지만**(앞 커밋), 분석의 시그니처(ptest)
//   에는 안 들어갔다. 그래서 **계약 오라클이 그 op 을 잘못 뽑았다**:
//   계약이 거부해야 할 입력(a=0)을 "허용" 이라 보고, 런타임이 트랩하자 **실패로 셌다.**
//   ⇒ 오라클이 자기가 못 보는 계약을 시험하고 있었다. 계약을 **한 곳에서** 본다.
 void iv_apply_named(ir_ctx_t *c, const low_parse_result_t *pr, const low_cst_t *f, iv_t *sig) {
    if (!pr) return;
    for (proven_size_t i = 0; i + 1 < f->nkids; i++) {
        if (!is_atom(f->kids[i]) || f->kids[i]->tok.kw != LOW_KW_SATISFIES) continue;
        for (proven_size_t r = i + 1; r < f->nkids && is_atom(f->kids[r]) &&
             f->kids[r]->tok.kw == LOW_KW_NONE; r++) {
            for (proven_size_t q = 0; q < pr->nforms; q++) {
                const low_cst_t *ct = pr->forms[q];
                if (ct->kind != LOW_CST_FORM || ct->nkids < 3 || !is_atom(ct->kids[0])) continue;
                if (!veq(ct->kids[0]->tok.lex, "contract") || !is_atom(ct->kids[1])) continue;
                if (!proven_u8str_view_eq(ct->kids[1]->tok.lex, f->kids[r]->tok.lex)) continue;
                const low_cst_t *blk = ct->kids[ct->nkids - 1];
                if (blk->kind != LOW_CST_BLOCK) continue;
                for (proven_size_t s = 0; s < blk->nkids; s++) {
                    const low_cst_t *rq = blk->kids[s];
                    if (rq->kind != LOW_CST_FORM || rq->nkids < 4 || !is_atom(rq->kids[0])) continue;
                    if (!veq(rq->kids[0]->tok.lex, "requires")) continue;
                    if (!is_atom(rq->kids[1]) || !is_atom(rq->kids[2]) || !is_atom(rq->kids[3])) continue;
                    iv_sig_narrow(c, sig, rq->kids[1]->tok.lex, rq->kids[2]->tok.lex,
                                  rq->kids[3]->tok.lex);
                }
            }
        }
    }
}
 void iv_apply_requires(ir_ctx_t *c, const low_cst_t *f, iv_t *sig) {
    // 절은 fn form 안의 **평평한 원자열**이다: … requires [grade] <cmp> <name> <lit> …
    for (proven_size_t i = 0; i + 3 < f->nkids; i++) {
        proven_size_t k; bool is_assume;
        if (!ir_requires_at(f, i, &k, &is_assume, NULL)) continue;
        // ★ assume 은 **검사되지 않는다** ⇒ 사실로 쓰지 않는다(검사 제거의 근거가 될 수 없다).
        if (is_assume) continue;
        proven_u8str_view_t op = f->kids[k]->tok.lex;
        bool found; proven_size_t slot = ir_local_find(c, f->kids[k + 1]->tok.lex, &found);
        proven_i64 n;
        if (!found || slot >= IR_MAXLOCALS || !ir_int_lit(f->kids[k + 2]->tok.lex, &n)) continue;
        iv_t *v = &sig[slot];
        // ★ 계약이 상계를 주면 그 hi 는 **진짜**다 — 자름 표시를 끈다(2026-08-17, PERF-0003).
        //   이것을 빠뜨렸더니 `requires le i 1000 .` 을 써도 `add i 1` 이 안 지워졌다:
        //   `iv_ty` 가 켠 표시가 그대로 남아 있었다. **계약을 읽고도 안 믿는 셈**이었다.
        if      (veq(op, "lt") && n > INT64_MIN) { if (n - 1 < v->hi) { v->hi = n - 1; v->wide = 0; } }
        else if (veq(op, "le"))                  { if (n     < v->hi) { v->hi = n;     v->wide = 0; } }
        else if (veq(op, "gt") && n < INT64_MAX) { if (n + 1 > v->lo) v->lo = n + 1; }
        else if (veq(op, "ge"))                  { if (n     > v->lo) v->lo = n;     }
    }
}
// ★★★★★ **길이의 상·하계도 계약이 말한다** (2026-08-19).
//
//   `iv_apply_requires` 는 `requires <cmp> <name> <lit>` 만 읽는다 — 왼쪽이 **원자**여야 한다.
//   그런데 길이 계약은 `requires le (len s) 16777216 .` 처럼 왼쪽이 **`(len s)` 그룹**이다.
//   그래서 그 절은 **통째로 건너뛰어졌고**, 계약을 쓴 판과 안 쓴 판의 제거율이 **똑같았다**
//   (실측 `bench_sieve`: 계약 판도 6/11 — 계약이 아무 일도 안 했다).
//
//   ★ 왜 이것이 필요한가: `sieve` 에 남은 검사 넷은 전부 **산술 오버플로**다
//     (`mul i i` · `add j i` · `add cnt 1`). 경계는 이미 다 지워졌다. 그 넷을 닫는 사실은
//     하나뿐이다 — *길이가 얼마나 큰가*. `len s ≤ 2^24` 를 알면 `i < n = len s` 에서
//     `i·i ≤ 2^48` 이 나오고, 그것은 u64 안이다.
//   ☞ 하한(`ge`/`gt`)도 같이 읽는다: 짝이 없으면 다음 사람이 *"상한만 되는군"* 을 배운다.
static void iv_apply_len_requires(ir_ctx_t *c, const low_cst_t *f, iv_t *lenv) {
    for (proven_size_t i = 0; i + 3 < f->nkids; i++) {
        proven_size_t k; bool is_assume;
        if (!ir_requires_at(f, i, &k, &is_assume, NULL)) continue;
        if (is_assume) continue;              // 검사되지 않는 것은 사실이 아니다
        proven_u8str_view_t op = f->kids[k]->tok.lex;
        const low_cst_t *L = f->kids[k + 1], *R = f->kids[k + 2];
        while (L && L->kind == LOW_CST_GROUP && L->nkids == 1) L = L->kids[0];   // 괄호는 장식이다
        if (!(L && L->kind == LOW_CST_FORM && L->nkids == 2 && is_atom(L->kids[0]) &&
              veq(L->kids[0]->tok.lex, "len") && is_atom(L->kids[1]))) continue;
        proven_i64 n;
        if (!is_atom(R) || !ir_int_lit(R->tok.lex, &n)) continue;   // 상수 아닌 우변은 lenge_p/q 의 일
        bool found; proven_size_t slot = ir_local_find(c, L->kids[1]->tok.lex, &found);
        if (!found || slot >= IR_MAXLOCALS) continue;
        iv_t *v = &lenv[slot];
        // ★ 계약이 준 상계는 **진짜**다 — 자름 표시를 끈다(PERF-0003 이 같은 자리에서 배운 것).
        if      (veq(op, "lt") && n > INT64_MIN) { if (n - 1 < v->hi) { v->hi = n - 1; v->wide = 0; } }
        else if (veq(op, "le"))                  { if (n     < v->hi) { v->hi = n;     v->wide = 0; } }
        else if (veq(op, "gt") && n < INT64_MAX) { if (n + 1 > v->lo) v->lo = n + 1; }
        else if (veq(op, "ge"))                  { if (n     > v->lo) v->lo = n;     }
    }
}

// ★★★ **배열 내용 술어를 심는다** — `requires elem_lt s N` ⇒ 슬라이스 s 의 원소 구간을 좁힌다.
//   (같은 평평한 원자열에서 `elem_*` 술어만 골라 읽는다. 스칼라 requires 와 같은 모양이다.)
static void iv_apply_elem_requires(ir_ctx_t *c, const low_cst_t *f, iv_t *elem) {
    for (proven_size_t i = 0; i + 3 < f->nkids; i++) {
        proven_size_t k; bool is_assume;
        if (!ir_requires_at(f, i, &k, &is_assume, NULL)) continue;
        if (is_assume) continue;
        proven_u8str_view_t op = f->kids[k]->tok.lex;
        int cmp = veq(op, "elem_lt") ? 0 : veq(op, "elem_le") ? 1 : veq(op, "elem_gt") ? 2 : veq(op, "elem_ge") ? 3 : -1;
        if (cmp < 0) continue;
        bool found; proven_size_t slot = ir_local_find(c, f->kids[k + 1]->tok.lex, &found);
        proven_i64 n;
        if (!found || slot >= IR_MAXLOCALS || !ir_int_lit(f->kids[k + 2]->tok.lex, &n)) continue;
        iv_t *v = &elem[slot];
        // ★★★★★ **좁힌 상계에서 `wide` 를 내린다** (2026-09-09, RFC-0111 §8-13 · WO-0191).
        //   `wide = 1` 은 *"이 hi 는 타입에서 잘라 온 것이라 못 믿는다"* 는 표시이고, `iv_in` 은
        //   그것을 보면 **무조건 거절**한다. 스칼라 `requires` 는 좁히면서 이미 내리는데
        //   (`iv_apply_requires` — 스무 줄 위) **원소 술어만 안 내렸다.** 그래서
        //   `requires elem_le a 10` 을 적어도 `add (index a i) 1` 이 안 증명됐다 —
        //   절은 읽히고 구간도 좁혀지는데 **그 구간을 아무도 못 믿게 표시돼 있었다.**
        //   ☞ 건전성: 계약이 준 상계는 잘라 온 것이 아니라 **진입에서 강제하는 약속**이다
        //     (`elem.check` 가 그 자리에서 훑는다) — 스칼라와 같은 논거다.
        if      (cmp == 0 && n > INT64_MIN) { if (n - 1 < v->hi) { v->hi = n - 1; v->wide = 0; } }
        else if (cmp == 1)                  { if (n     < v->hi) { v->hi = n;     v->wide = 0; } }
        else if (cmp == 2 && n < INT64_MAX) { if (n + 1 > v->lo) v->lo = n + 1; }
        else if (cmp == 3)                  { if (n     > v->lo) v->lo = n;     }
    }
}

// ── 구간 분석 v2 — 기본블록 CFG + 고정점 + **분기 조건 narrowing** ──────────
// 루프가 있는 코드에서 무언가를 증명하려면 세 가지가 함께 필요하다:
//   (1) 기본블록 CFG 와 고정점 반복      — 루프를 돌며 상태가 수렴할 때까지
//   (2) 뒤로 가는 간선에서의 **위드닝**  — 종료 보장(구간은 무한 도메인이다)
//   (3) 분기 조건에서의 **내로잉**       — 이게 없으면 위드닝이 전부 ⊤ 로 날려 버려
//                                          `while lt i 10` 이 i 를 전혀 묶지 못한다
// (3) 을 하려면 "이 스택 값이 어떤 술어인가"를 알아야 하므로, 추상 스택의 각 항목이
// 술어 출처(cmp op · 지역 슬롯 · 상수)를 함께 들고 다닌다.

#define IV_MAXBLK 256

typedef struct {
    iv_t v;
    proven_u8 pred;      // 0 = 술어 아님, 아니면 IRW_LT..IRW_NE
    proven_i32 slot;     // 술어 좌변의 지역 슬롯 (-1 = 없음)
    iv_t rhs;            // 술어 **우변의 구간** — 상수든 지역이든 똑같이 다룬다.
                         // 우변이 지역이면 그 지역의 현재 구간을 쓴다: `lt i n` 이고
                         // n ≤ 100 이면 i ≤ 99 로 좁힌다 — **계약이 루프 안까지 전달된다.**
    proven_i64 cst;      // (상수 피연산자 표시용)
    // ── RFC-0055 D5: **관계 사실** — 구간만으로는 인덱스 경계를 못 없앤다 ──────────
    // `index s i` 의 안전성은 `i < len(s)` 다. 그런데 len(s) 는 런타임 값이라
    // 구간 영역에서 ⊤ 다. 그래서 구간 옆에 **한 칸짜리 관계 영역**을 둔다:
    //   lenof : 이 스택 값이 `len <slot>` 자신인가 (아니면 -1)
    //   lenlt : 이 값이 `len <slot>` 보다 **작다**고 알려진가 (아니면 -1)
    // 이 둘이면 충분하다. 실제 코드가 정확히 그 모양이기 때문이다:
    //   while lt i (len g) . do  … index g i …  end
    proven_i32 lenof;
    proven_i32 lenlt;
    // ★★★ **엄격 비트** (2026-08-13, D3). `lenlt` 만 있으면 `<` 밖에 못 담는데, 실제 계약은
    //   `requires le n (len a) .` 처럼 **`≤` 로 쓰는 것이 더 흔하다**(용량·길이 계약의 자연형).
    //   그것을 담을 자리가 없어서 그 모양은 **아무 사실도 안 남겼다** — 즉 계약을 쓰고도
    //   본문 검사가 그대로 남았다(실측: `lt` 는 0, `le` 는 1).
    //   ⇒ 트윈 배열(`lenle[]`)을 만들지 **않는다**: 나를 곳이 여섯 군데라 이 저장소가 두 번
    //     당한 *"목록에서 한 줄 빠뜨림"* 을 세 번째로 부른다. `lerel`/`lestr` 가 이미 쓰는
    //     **비트 하나** 관례를 그대로 따른다.
    //   ★ 건전성: `lenstr == 0` 은 `k ≤ len s` 라는 **약한** 사실이고, 그것만으로는 `index`
    //     가 안전하지 않다(k = len s 가능). 그래서 **미지(unset) = 0(약함)** 으로 둔다 —
    //     실수로 안 실으면 검사가 *되살아날* 뿐 불건전해지지 않는다(실패 방향이 안전 쪽).
    proven_u8  lenstr;
    proven_i64 lenoff;    // ★ 여백(2026-08-16) — `이 값 + lenoff (<|≤) len(lenlt)`. 지역 쪽과 같은 뜻.
    proven_i32 lenpred;   // 술어의 좌변이 `len <slot>` 이면 그 슬롯 (아니면 -1)
    // ★★ R5 — **관계형 사실**. 구간은 변수를 **독립으로** 본다: `a ≤ b` 를 표현할 자리가 없다.
    //   `lenlt` 는 이미 그 특수 사례다(`i < len(s)`). 그러면 일반형도 같은 모양으로 둘 수 있다:
    //   술어의 **우변이 지역**이면 그 슬롯을 실어 보낸다 — 그래야 `le a b` 가 사실이 된다.
    proven_i32 rslot;     // 술어 우변의 지역 슬롯 (-1 = 지역이 아님)
    // ★★★ **행우선 인덱스를 증명하려면 비선형이 필요하다** (2026-07-30).
    //   `index a (add (mul i n) k)` 는 구간·한 칸 관계로는 **절대** 안 된다: i·n 은 곱이다.
    //   그런데 실제 코드가 쓰는 모양은 **하나**다 — 행우선 주소 계산. 그것만 정확히 증명한다:
    //     len(s) ≥ p·q  ∧  i < p  ∧  k < q  ⟹  i·q + k ≤ (p−1)q + (q−1) = p·q − 1 < len(s)
    //   ★ 성립 조건(하나라도 빠지면 **메모리 안전 구멍**이다):
    //     · p·q 가 넘치지 않았다 — 진입 `requires ge (len s) (mul p q)` 의 곱이 **트랩 곱**이고
    //       requires 검사는 **절대 제거되지 않으므로**, 본문에 도달했다면 넘치지 않았다.
    //     · i·q 와 +k 도 **트랩 연산**이다 — wrap/sat 이면 조용히 감싸므로 이 논증이 깨진다.
    //       그래서 아래에서 `IR_POL_WRAP|IR_POL_SAT` 이 붙은 곱·합은 **출처를 안 싣는다**.
    //     · 부호: 무부호이거나 i·k·q 가 음수가 아니어야 한다.
    // ★★★★ **아핀 출처** (2026-08-16, WO-0056 P2) — 이 값은 `지역 aff_s + aff_c` 다 (아니면 -1).
    //   벡터 루프의 표준형이 `while le (add i 4) n` 이라, 술어의 **좌변이 지역이 아니다**.
    //   그래서 `slot < 0` 이 되고 좁히기가 **아무것도 안 배웠다**. 한 칸짜리 아핀 출처면 충분하다:
    //   실제 코드가 정확히 그 모양(귀납 변수 + 상수 걸음)이기 때문이다 — `lenof`/`lenlt` 를
    //   둔 것과 같은 이유다.
    //   ★ **트랩 덧셈일 때만** 싣는다: wrap/sat 은 조용히 감기므로 `i + 4` 가 진짜 i+4 가 아니다.
    proven_i32 aff_s; proven_i64 aff_c;
    // ★ **우변의 아핀** — `while lt i (sub hi 1)` 처럼 우변이 임시값이면 `rslot = -1` 이라
    //   기존 추이가 안 걸린다. 종료값 규칙이 그 우변의 길이 사실을 물려받아야 하므로 함께 나른다.
    //   ☞ 이것만 단독으로 실었을 때는 소진이 0 이었다(2026-08-17 오전, 되돌림) — 사실이 루프
    //     **안**에서만 서고 쓰이는 데까지 못 갔기 때문이다. 종료값과 **함께** 와야 값을 한다.
    proven_i32 raff_s; proven_i64 raff_c;
    // ★★★★★ **차분 출처** — 이 값은 `지역 dif_l − 지역 dif_r` 다 (아니면 -1). `aff` 의 짝.
    //   ☞ `qsort` 의 바깥 루프 가드가 `gt (sub hi lo) 1` 이다. 좌변이 지역도 아니고
    //     `지역 ± 상수` 도 아니라 **술어가 통째로 버려졌다** — `aff_s` 가 `le (add i 4) n` 에
    //     대해 열어 준 것과 정확히 같은 종류의 구멍이다.
    //   ★ **트랩 무부호 뺄셈일 때만** 싣는다: 감기면 값이 진짜 차분이 아니고,
    //     부호형이면 `l − r > c` 에서 `r + c < l` 로 못 옮긴다(넘칠 수 있다).
    proven_i32 dif_l, dif_r;
    // ★ 이 술어의 **비교 타입이 무부호**인가. 위드닝이 무부호 지역의 하한 0 을 잃어버리기
    //   때문에(`loc.lo = INT64_MIN`) 곱 규칙이 `f ≥ 0` 을 구간에서 못 확인한다 — 그런데
    //   그 사실은 **타입에 있다.** 그래서 술어가 그것을 실어 보낸다.
    bool puns;
    // ★ **레인 수** — `vector.store` 의 `a` 는 0 이고 레인/폭은 **벡터 피연산자가 실어 온다**
    //   (IR 규약). 그래서 저장의 여백 규칙이 L 을 알려면 값을 따라 흘러야 한다.
    //   0 = 모른다 ⇒ **증명하지 않는다**(모르면 안 지운다).
    proven_u8 vlanes;
    proven_i32 prod_l, prod_r;    // 이 값은 `지역 prod_l * 지역 prod_r` 다 (트랩 곱) — 아니면 -1
    // ★★★★★ **나눗셈의 출처** (2026-08-19) — 무부호 항등식 하나가 두 검사를 닫는다:
    //     (x / c) · c ≤ x   그리고   x − (x / c) · c = x mod c ∈ [0, c−1]
    //   `bench_lru` 의 `q = prod/6 · key = prod − q·6 · key·100` 사슬이 이것 없이는 안 닫혔다.
    //   구간만으로는 못 한다: lo(prod)=0 이고 hi(q·6)=hi(prod) 라 뺄셈이 음수로 보인다.
    //   ⇒ **어디서 왔는지**를 나른다. `divof_*` = 이 값은 `지역 divof_s / divof_c` 다.
    //     `dmul_*`  = 이 값은 `(지역 dmul_s / dmul_c) · dmul_c` 다.
    //   ★ 트랩 연산·무부호일 때만 싣는다(감기면 항등식이 깨진다).
    proven_i32 divof_s; proven_i64 divof_c;
    proven_i32 dmul_s;  proven_i64 dmul_c;
    proven_i32 row_i, row_q, row_k;   // 이 값은 `i*q + k` 다 (트랩 곱·합) — 아니면 -1
    bool       nofail;    // ★ 이 값은 **실패할 수 없는 호출**의 결과다 (errors…when 이 전부 거짓)
    // ★ 경로별 **술어 기억** — 구간으로 표현할 수 없는 조건을 위한 것.
    //   `guard eq (index data 0) 1 else return error bad_version` 에서 else 가지의
    //   `ne (index data 0) 1` 은 **참임이 자명하다** — 같은 식이니까. 구간은 그것을 모른다.
    //   그래서 식마다 지문(eid)을 매기고, 분기가 그 지문의 진리값을 기억한다.
    proven_u64 eid;       // 이 값을 만든 식의 지문 (0 = 모름) — **빠른 조회용**
    proven_u64 deps;      // 그 식이 읽은 지역들의 비트마스크 (슬롯 0..63)
    bool       pneg;      // 이 값 = NOT(eid 가 가리키는 비교) 인가
    proven_size_t ifrom, ito;  // ★ 그 식을 계산한 **명령 범위**. 지문이 같아도 여기서
                               //   **구조를 실제로 대조한다** — 해시 충돌로 검사를 지우는 일이 없도록.
                               //   (네이티브는 검사를 진짜로 지운다. 근거가 해시일 수는 없다.)
} ivs_t;

// 지역마다 구간 + 관계 사실 + **길이 구간**.
//   loc[k]   : 지역 k 의 값 구간
//   lenlt[k] : "지역 k 는 len(지역 s) 보다 작다" (s = lenlt[k])
//   lenv[k]  : ★ **len(지역 k) 의 구간** — 슬라이스의 길이도 값이다. 분기 조건이 그것을 좁힌다.
//              (`guard ge len data . 4 .` 의 else 가지에서 len(data) ∈ [0,3] 이 된다.)
/* IV_MAXFACT — low_ir_priv.h */
/* iv_fact_t — low_ir_priv.h (계약→시험 절이 쓴다) */

/* ivstate_t — low_ir_priv.h (계약→시험 절이 쓴다) */


// ★ 여백 하나로 읽는다 — 사실이 없으면 **0**(아무것도 보장 못 한다).
static proven_i64 iv_lemargin(const ivstate_t *st, proven_size_t k, proven_i32 p) {
    if (k >= IR_MAXLOCALS || p < 0) return 0;
    if (st->lerel[k] == p) return st->leoff[k] + (st->lestr[k] ? 1 : 0);
    // ★★★ **한 홉**(2026-08-17). `var hi be u64 hi0 .` 같은 복사 때문에 관계가 **원본**을
    //   가리킨 채 남는다: `lo < hi0` 인데 쓰는 자리는 `hi` 다. 아래 STORE 가 복사에 대해
    //   `hi0 ≤ hi` 를 심어 두므로, `lo ≤ hi0 ≤ hi` 를 한 걸음으로 잇는다.
    //   ☞ 한 홉까지만 간다 — 두 홉이 필요한 모양은 실제 코드에 안 나왔다.
    proven_i32 q = st->lerel[k];
    if (q >= 0 && (proven_size_t)q < IR_MAXLOCALS && st->lerel[q] == p)
        return (st->leoff[k] + (st->lestr[k] ? 1 : 0)) +
               (st->leoff[q] + (st->lestr[q] ? 1 : 0));
    return 0;
}
static void iv_put_lerel(ivstate_t *st, proven_size_t k, proven_i32 p,
                         bool strict, proven_i64 off) {
    if (k >= IR_MAXLOCALS) return;
    if (p < 0) { st->lerel[k] = -1; st->lestr[k] = 0; st->leoff[k] = 0; return; }
    if (off < 0) return;                                   // 음의 여백은 사실이 아니다
    proven_i64 nk = off + (strict ? 1 : 0);
    if (st->lerel[k] == p && iv_lemargin(st, k, p) >= nk) return;
    st->lerel[k] = p; st->lestr[k] = (proven_u8)(strict ? 1 : 0); st->leoff[k] = off;
}
// ★★★ **0 은 없음이 아니다** (2026-08-17, 차등 스윕이 잡았다). `iv_lemargin` 은 *"사실이 없다"*
//   와 *"여백이 0 이다"* 를 **같은 0** 으로 돌려준다. 여백 0 도 훌륭한 사실(`y ≤ x`)이므로
//   `margin >= 0` 으로 물으면 **아무 사실도 없을 때 참**이 된다 — 그렇게 `sat_sub b a` 의
//   검사를 지워 VM 0 / 네이티브 −7 로 갈렸다. ⇒ **존재를 따로 묻는다.**
static bool iv_leknown(const ivstate_t *st, proven_size_t k, proven_i32 p) {
    if (k >= IR_MAXLOCALS || p < 0) return false;
    if (st->lerel[k] == p) return true;
    proven_i32 q = st->lerel[k];
    return q >= 0 && (proven_size_t)q < IR_MAXLOCALS && st->lerel[q] == p;
}
static proven_i64 iv_margin(const ivstate_t *st, proven_size_t k, proven_i32 s) {
    if (k >= IR_MAXLOCALS || s < 0 || st->lenlt[k] != s) return 0;
    return st->lenoff[k] + (st->lenstr[k] ? 1 : 0);
}
// ★ 길이 사실을 **싣는다**. 같은 슬라이스에 대한 기존 사실보다 **약하면 버린다** — 약한 사실로
//   강한 사실을 덮어쓰면 이미 증명된 것이 되살아난다(성능 회귀). 다른 슬라이스면 새 것이 이긴다.
static void iv_put_lenlt(ivstate_t *st, proven_size_t k, proven_i32 s,
                         bool strict, proven_i64 off) {
    if (k >= IR_MAXLOCALS) return;
    if (s < 0) { st->lenlt[k] = -1; st->lenstr[k] = 0; st->lenoff[k] = 0; return; }
    if (off < 0) return;   // ★ 음의 여백은 **사실이 아니다** — 0 으로 낮추면 거짓을 싣는다
                           //   (`i − 3 ≤ n` 은 `i ≤ n` 을 **뜻하지 않는다**).
    proven_i64 nk = off + (strict ? 1 : 0);
    if (st->lenlt[k] == s && iv_margin(st, k, s) >= nk) return;   // 이미 더 강하다
    st->lenlt[k] = s; st->lenstr[k] = (proven_u8)(strict ? 1 : 0); st->lenoff[k] = off;
}
// ★★★★ **슬라이스끼리의 길이 사실을 가드에서도 심는다** (2026-09-12, RFC-0111 §8-23 · WO-0201).
//   `lenlt[m]` 은 **변수 하나당 한 칸**이다. `if gt (add m 1) (len prev) … return` 다음에
//   `if gt (add m 1) (len cur) … return` 이 오면 뒤 가드가 앞 가드의 사실을 **덮어써서**,
//   두 가드가 똑같이 참인데 `prev` 쪽 색인만 증명이 안 됐다(dp 기본판 — 가드 순서를 바꾸면
//   검사가 `cur` 쪽으로 옮겨 갔다). 그런데 `m` 이 `len u` **그 자체**라면 그 사실은 사실
//   **두 슬라이스 사이**의 것이다: `len u (<|≤) len s`. 그것을 계약(`requires le (len u) (len s)`)이
//   이미 쓰는 **대상 슬라이스별 칸**(`slenle_t[s]`)에 싣는다 — 칸이 슬라이스마다 있으니 덮지 않는다.
//   ☞ 건전성: 좁히기가 그 경로에서 참이라고 한 사실만 싣고, 합류에서는 두 경로가 같을 때만
//     산다(IV_PUSH). 슬라이스를 다시 쓰면 기존 규칙이 지운다.
static void iv_put_slenle(ivstate_t *st, proven_i32 s, proven_i32 u, bool strict) {
    if (s < 0 || u < 0 || s == u || (proven_size_t)s >= IR_MAXLOCALS || (proven_size_t)u >= IR_MAXLOCALS) return;
    if (st->slenle_t[s] == u && st->slenle_str[s] && !strict) return;   // 이미 더 강하다
    st->slenle_t[s] = u; st->slenle_str[s] = (proven_u8)(strict ? 1 : 0);
    // 엄격이면 긴 쪽은 비어 있을 수 없다: len s > len u ≥ 0 ⇒ len s ≥ 1 (진입 쪽과 같은 규칙).
    if (strict && st->lenv[s].lo < 1) { st->lenv[s].lo = 1; st->lenv[s].wide = 0; }
}

static proven_u64 iv_mix(proven_u64 h, proven_u64 x) {
    h ^= x + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2);
    return h ? h : 1;
}
static void iv_fact_add(ivstate_t *st, proven_u64 eid, proven_u64 deps, bool truth,
                        proven_size_t ifrom, proven_size_t ito) {
    if (!eid) return;
    for (proven_u8 i = 0; i < st->nfacts; i++)
        if (st->facts[i].eid == eid) {
            st->facts[i].truth = truth; st->facts[i].deps = deps;
            st->facts[i].ifrom = ifrom; st->facts[i].ito = ito;
            return;
        }
    if (st->nfacts < IV_MAXFACT)
        st->facts[st->nfacts++] = (iv_fact_t){ eid, deps, truth, ifrom, ito };
}
// 비교 연산자의 정규형: ne = ¬eq · ge = ¬lt · le = ¬gt.
static low_irw_t iv_canon_cmp(low_irw_t w) {
    return w == IRW_NE ? IRW_EQ : w == IRW_GE ? IRW_LT : w == IRW_LE ? IRW_GT : w;
}
// ★ 두 명령 범위가 **같은 식**인가 — 구조를 실제로 대조한다(해시를 믿지 않는다).
//   네이티브는 검사를 진짜로 지운다. 근거가 해시 충돌 위에 설 수는 없다.
//   PROVEN 표시 비트는 표시 패스가 켜므로 비교에서 뺀다.
//   ★ 정규화는 **최상위 비교 연산자에만** 적용한다(진리값 극성은 pneg 가 들고 있다).
//     중첩된 비교까지 정규화하면 `and (lt a b) c` 와 `and (ge a b) c` 가 같아 보인다 — 불건전.
static bool iv_same_expr(const low_ir_def_t *d, proven_size_t a0, proven_size_t a1,
                         proven_size_t b0, proven_size_t b1) {
    if (a1 - a0 != b1 - b0) return false;
    if (a1 >= d->ncode || b1 >= d->ncode) return false;
    for (proven_size_t k = 0; k <= a1 - a0; k++) {
        const low_ir_ins_t *x = &d->code[a0 + k], *y = &d->code[b0 + k];
        bool top = (a0 + k == a1);                       // 최상위 = 술어 자신
        low_irw_t xw = top ? iv_canon_cmp(x->w) : x->w;
        low_irw_t yw = top ? iv_canon_cmp(y->w) : y->w;
        if (xw != yw) return false;
        if ((x->a & ~(proven_i64)IR_POL_PROVEN) != (y->a & ~(proven_i64)IR_POL_PROVEN)) return false;
    }
    return true;
}
static bool iv_fact_get(const ivstate_t *st, const low_ir_def_t *d, proven_u64 eid,
                        proven_size_t ifrom, proven_size_t ito, bool *truth) {
    if (!eid) return false;
    for (proven_u8 i = 0; i < st->nfacts; i++)
        if (st->facts[i].eid == eid &&
            iv_same_expr(d, st->facts[i].ifrom, st->facts[i].ito, ifrom, ito)) {
            *truth = st->facts[i].truth; return true;
        }
    return false;
}
// 지역이 바뀌면 그 지역을 읽은 사실은 **죽는다.**
static void iv_fact_kill(ivstate_t *st, proven_size_t slot) {
    proven_u64 bit = (slot < 64) ? ((proven_u64)1 << slot) : 0;
    if (!bit) { st->nfacts = 0; return; }          // 64 이상은 보수적으로 전부 버린다
    proven_u8 n = 0;
    for (proven_u8 i = 0; i < st->nfacts; i++)
        if (!(st->facts[i].deps & bit)) st->facts[n++] = st->facts[i];
    st->nfacts = n;
}

// ★★★★★ **한 지역이 바뀔 수 있게 되면 그 지역에 걸린 사실은 전부 죽는다** (2026-08-13).
//   전에는 이 무효화가 `IRW_STORE` **한 자리에 손으로 펼쳐져** 있었고, 그래서 *다른 창*으로
//   같은 지역이 바뀌는 경로는 **아무것도 안 죽였다.** 실증(소유자 질문에서 나왔다):
//       proc shrink input p mut ref slice u8 . do set p (subslice p 0 0) . end
//       … while lt i (len a) . do  let z be shrink (mut_ref a) .  index a i  …
//   호출자 본문에 STORE 가 **없으므로** 길이 사실이 살아남아 경계 검사가 지워지고,
//   VM 이 **E-VM-ANALYSIS**(범위 밖 읽기 = 분석 불건전)로 자기를 고발했다. 네이티브는 침묵한다.
//   ⇒ 무효화를 **한 함수로 모은다.** 창이 하나 더 생기면 그 창에서 이것을 부르면 되고,
//     필드가 늘면 여기 한 곳만 는다 — 이 저장소가 두 번 당한 *"목록에서 한 줄 빠뜨림"* 을
//     구조로 막는 것이 골든 증인 하나보다 낫다(RFC-0093 §1).
//   ★ 창 목록은 SPEC-004 §4.4a 가 정본이다: ① mut_ref 인자 ② 액터/태스크 공유
//     ③ volatile/MMIO ④ FFI. ②③④ 는 권한·효과로 이미 격리돼 있고 ① 만 열려 있었다.
// ★★★★★ **진입 프롤로그를 IR 모양으로 알아본다** (2026-08-18, spill 티켓 상환).
//
//   `input a slice u64 . .` 는 def 의 **맨 앞**에 자기-재대입을 낸다:
//       load.local sl · view.array · store.local sl      (파라미터마다 하나씩)
//   계약(`requires ge (len a) (mul n n)`)은 **그 store 다음의** a 를 말하므로, 이 store 에서
//   `lenge_p/q` 를 지우는 것은 보수적인 게 아니라 **틀린 것**이다.
//
//   그러나 **몸통의** store 는 다르다: 거기서 `a` 를 다른 슬라이스로 갈아끼우면 계약은 낡는다.
//   전에는 둘을 구별할 방법이 없어서 **정방향을 아예 안 지웠다** — 그것이 구멍이었다.
//   ⇒ 프롤로그는 **위치와 모양**으로 정해진다. def 선두의 `LOAD sl · VARRAY · STORE sl`
//     삼중주가 이어지는 동안이 프롤로그고, 첫 어긋남에서 끝난다. IR 에 새 필드도, 새 비트도
//     필요 없다 — **이미 있는 모양이 증거다**(그래서 def 해시도 안 바뀐다).
static proven_size_t ir_prologue_end(const low_ir_def_t *d) {
    proven_size_t i = 0;
    while (i + 2 < d->ncode &&
           d->code[i].w     == IRW_LOAD &&
           d->code[i + 1].w == IRW_VARRAY &&
           d->code[i + 2].w == IRW_STORE &&
           d->code[i].a     == d->code[i + 2].a)
        i += 3;
    return i;
}

// keep_lenge=true 는 **진입 프롤로그의 자기-재대입**에서만 쓴다 (위 설명).
static void iv_kill_local(ivstate_t *st, proven_size_t sl, bool keep_lenge) {
    if (sl >= IR_MAXLOCALS) return;
    st->loc[sl]      = IV_TOP;
    st->lenlt[sl]    = -1; st->lenstr[sl] = 0; st->lenoff[sl] = 0;
    st->lenofloc[sl] = -1;
    // ★★★★★ **길이에는 상한이 있다** (2026-08-27, WO-0131 — 소유자 결정).
    //   전에는 `IV_LEN`(hi = INT64_MAX)이었고, 그 hi 는 *"음수가 아니다"* 라는 **하한의
    //   자리 표시자**이지 상계가 아니었다(이 파일이 §7148 에서 그렇게 적는다).
    //   그래서 `while lt i (len a)` 관용구의 누산기가 반복마다 넘침 검사를 달았다.
    //   ⇒ 상한은 **주소공간에서 나온다**: `machine.max_slice_len`(목표 표, comptime 상수).
    //   ★ 이것은 **언어의 약속**이다 — "이 수를 넘는 슬라이스는 만들 수 없다".
    //     SPEC-004 §객체 크기에 명문화했다. 약속 없이 상계를 쓰는 것은 이 파일이
    //     경고하는 바로 그 잘못이다(*"정보가 없는 상계는 쓰지 않는다"*).
    st->lenv[sl].lo   = 0;
    st->lenv[sl].hi   = ir_tgt->max_slice_len;
    st->lenv[sl].wide = 0;
    st->elemv[sl]    = IV_TOP;
    // ★★★ **`lenge_p/q` 는 여기서 지우지 않는다** — 지웠다가 행우선 증명을 깨뜨렸다(실측).
    //   `input a slice u64 . .` 는 진입에 **자기-재대입**을 낸다: `load 0 · view.array · store 0`.
    //   그런데 계약(`requires ge (len a) (mul n n)`)은 **그 뒤의** a 를 말한다 — 진입 assert 자신이
    //   `len (load 0)` 을 그 store **다음에** 읽는다. 그러니 이 store 에서 계약 사실을 지우는 것은
    //   보수적인 게 아니라 **틀린 것**이다(사실은 애초에 view 이전 값에 대한 것이 아니었다).
    //   ☞ **그 구멍은 닫혔다**(2026-08-18): 프롤로그는 `ir_prologue_end` 가 **모양으로** 가려낸다.
    //     프롤로그가 아니면 정방향도 지운다 — 몸통에서 `a` 를 다른 슬라이스로 재대입하면
    //     `len a ≥ p·q` 는 그 자리에서 낡기 때문이다.
    if (!keep_lenge) { st->lenge_p[sl] = -1; st->lenge_q[sl] = -1;
                      st->slenle_t[sl] = -1; st->slenle_str[sl] = 0; }
    st->lerel[sl]    = -1; st->lestr[sl]   = 0; st->leoff[sl] = 0;
    st->vlanesloc[sl] = 0;
    st->divofloc_s[sl] = -1;
    st->prodloc_l[sl] = -1; st->prodloc_r[sl] = -1;
    st->affloc_s[sl] = -1;
    iv_fact_kill(st, sl);           // 그 지역을 읽은 술어 기억도 죽는다
    // ★ **양방향**이다 — 이 지역을 *가리키는* 사실도 죽어야 한다. 한쪽만 죽이면
    //   "i < len a" 에서 a 가 바뀌었는데 i 가 여전히 작다고 믿는다.
    for (proven_size_t k = 0; k < IR_MAXLOCALS; k++) {
        // ★★★★★ **자기-재대입은 길이 관계도 안 죽인다** (2026-09-09, RFC-0111 §8-14 · WO-0192).
        //   위 `lenge_p/q` 주석이 적은 그 사연이 **이 줄에도 그대로** 해당한다: 진입 프롤로그의
        //   `load p · view.array · store p` 는 같은 메모리를 폭만 붙여 자기 자신에게 다시 묶는
        //   것이고, 계약이 말하는 `a` 는 **그 정규화 다음의 a** 다(진입 assert 자신이 store
        //   뒤에서 `len (load 0)` 을 읽는다). 그런데 이 줄이 계약이 방금 준 `i < len a` 를
        //   지웠다 ⇒ 같은 프로그램이 **원소 폭만으로** 갈렸다(u8 은 증명되고 u64 는 안 됐다).
        //   ☞ *예외를 하나 만들 때는 같은 근거를 쓰는 줄을 전부 찾아야 한다 — 한 줄만 고치면
        //     그 예외는 절반만 존재한다.*
        if (!keep_lenge) {
            if (st->lenlt[k]    == (proven_i32)sl) { st->lenlt[k] = -1; st->lenstr[k] = 0; st->lenoff[k] = 0; }
            if (st->lenofloc[k] == (proven_i32)sl) st->lenofloc[k] = -1;
        }
        // ★ 반대 방향은 **지운다**: `p` 나 `q` 가 바뀌면 `len s ≥ p·q` 는 그 자리에서 낡는다.
        //   이쪽은 프롤로그와 무관하고(파라미터 자기-재대입이 아니다) 진짜 위험이다.
        if (st->lenge_p[k]  == (proven_i32)sl ||
            st->lenge_q[k]  == (proven_i32)sl) { st->lenge_p[k] = -1; st->lenge_q[k] = -1; }
        // ★ 길이 관계도 **양방향**이다: 짧은 쪽이 바뀌면 «len s ≤ len t» 는 그 자리에서 낡는다.
        if (!keep_lenge && st->slenle_t[k] == (proven_i32)sl) {
            st->slenle_t[k] = -1; st->slenle_str[k] = 0;
        }
        if (st->lerel[k]    == (proven_i32)sl) { st->lerel[k] = -1; st->lestr[k] = 0; st->leoff[k] = 0; }
        // ★★ 출처도 **양방향**이다 (2026-10-04): `row = i·n` 은 i 나 n 이 바뀌는 순간 낡고, `q = s / c` 는 s 가 바뀌는 순간 낡는다.
        //   나눗셈 출처는 이 줄이 없었다 — 원본이 바뀐 뒤에도 `q·c ≤ s` 를 믿을 수 있었다(이번에 곱 출처를 지역에 실으며 찾았다).
        if (st->prodloc_l[k] == (proven_i32)sl || st->prodloc_r[k] == (proven_i32)sl) { st->prodloc_l[k] = -1; st->prodloc_r[k] = -1; }
        if (st->divofloc_s[k] == (proven_i32)sl) st->divofloc_s[k] = -1;
        if (st->affloc_s[k] == (proven_i32)sl) st->affloc_s[k] = -1;
    }
}

static iv_t iv_meet(iv_t a, iv_t b) {
    // ★ 만남은 **좁힌다** — 더 작은 hi 를 낸 쪽이 그 hi 의 출처이므로 자름 여부도 그쪽을 따른다.
    iv_t r = { a.lo > b.lo ? a.lo : b.lo, a.hi < b.hi ? a.hi : b.hi,
               a.hi < b.hi ? a.wide : b.wide };
    if (r.lo > r.hi) r = a;   // 공집합 — 도달 불가지만 보수적으로 유지
    return r;
}
static iv_t iv_join2(iv_t a, iv_t b) {
    // ★ 합침은 **넓힌다** — 하나라도 자른 것이면 결과의 hi 도 믿을 수 없다.
    return (iv_t){ a.lo < b.lo ? a.lo : b.lo, a.hi > b.hi ? a.hi : b.hi,
                   (proven_u8)(a.wide || b.wide) };
}
// 위드닝: 넓어지는 쪽만 극단으로 보낸다(무한 상승 사슬 차단)
static iv_t iv_widen(iv_t old, iv_t nw) {
    // ★ 위드닝이 hi 를 극단으로 보내면 그것은 **더 이상 사실이 아니다** ⇒ 자른 것으로 표시한다.
    //   INT64_MAX 는 u64 의 진짜 상계가 아니다 — 그 자리에서 결론하면 불건전하다.
    bool up = nw.hi > old.hi;
    // ★★★ **0 에서 한 번 멈춘다** (2026-08-17). 내려가는 하한을 곧바로 INT64_MIN 으로 보내면
    //   무부호 지역의 *"음수가 될 수 없다"* 를 잃는다 — 곱 술어(부록 F.5)가 그 때문에 조용히
    //   안 먹었고, 계측해야 보였다. 두 상태의 lo 가 모두 ≥ 0 이면 **0** 으로 한 칸만 내린다:
    //   `[0, …]` 는 두 상태의 합류를 여전히 덮으므로 **건전하고**, 다음 라운드에 진짜로
    //   음수가 나오면 그때 INT64_MIN 으로 간다(사슬 유한 ⇒ 종료도 그대로).
    proven_i64 lo = old.lo;
    if (nw.lo < old.lo) lo = (old.lo >= 0 && nw.lo >= 0) ? 0 : INT64_MIN;
    return (iv_t){ lo,
                   up ? INT64_MAX : old.hi,
                   (proven_u8)(up ? 1 : (old.wide || nw.wide)) };
}
// 술어 `slot <op> cst` 가 참(taken=true)/거짓일 때 구간을 좁힌다.
// lenrhs ≥ 0 이면 우변이 `len <lenrhs>` 였다는 뜻 — 그러면 **관계 사실**도 함께 갱신한다.
static void iv_narrow_iv(iv_t *v, proven_u8 e, iv_t rhs) {
    switch (e) {
        // ★ **진짜 사실로 좁히면 자름 표시가 꺼진다** — 새 hi 의 출처가 우변이기 때문이다.
        //   우변 자신이 자른 것이면(`rhs.wide`) 그 성질을 물려받는다.
        case IRW_LT: if (rhs.hi > INT64_MIN && rhs.hi - 1 < v->hi) { v->hi = rhs.hi - 1; v->wide = rhs.wide; } break;
        case IRW_LE: if (rhs.hi < v->hi) { v->hi = rhs.hi; v->wide = rhs.wide; } break;
        case IRW_GT: if (rhs.lo < INT64_MAX && rhs.lo + 1 > v->lo) v->lo = rhs.lo + 1; break;
        case IRW_GE: if (rhs.lo > v->lo) v->lo = rhs.lo; break;
        case IRW_EQ: { iv_t m = { v->lo > rhs.lo ? v->lo : rhs.lo, v->hi < rhs.hi ? v->hi : rhs.hi };
                       if (m.lo <= m.hi) { *v = m; }
                       break; }
        default: break;
    }
    if (v->lo > v->hi) *v = (iv_t){ v->lo, v->lo };
}
static proven_u8 iv_negate(proven_u8 op) {
    return op == IRW_LT ? IRW_GE : op == IRW_GE ? IRW_LT
         : op == IRW_LE ? IRW_GT : op == IRW_GT ? IRW_LE
         : op == IRW_EQ ? IRW_NE : op == IRW_NE ? IRW_EQ : 0;
}
// ★ 술어의 좌변이 `len <slot>` 이면 **그 슬라이스의 길이 구간**을 좁힌다.
// ★★★★★ **차분 술어를 좁힌다** (2026-08-17). `l − r REL c` 는 **두 지역 사이의 사실**이다:
//   `l − r > c` ⟹ `r + (c+1) ≤ l`, 그리고 `l ≥ lo(r) + (c+1)`.
//   ☞ `qsort` 의 `gt (sub hi lo) 1` 이 참인 가지에서 `lo + 2 ≤ hi` 와 `hi ≥ 2` 를 준다 —
//     그것이 `sub hi lo` 와 `sub hi 1` 넷을 닫는다.
//   ★ c 는 구간일 수 있다. 보장되는 결론은 **가장 작은 c**(rhs.lo) 의 것이다.
//   ★ 음수 여백은 `iv_put_lerel` 이 거절하므로 `rhs.lo < 0` 이면 아무 말도 안 한다.
static void iv_narrow_dif(ivstate_t *st, proven_u8 pred, proven_i32 dl, proven_i32 dr,
                          iv_t rhs, bool taken) {
    if (dl < 0 || dr < 0 || (proven_size_t)dl >= IR_MAXLOCALS ||
        (proven_size_t)dr >= IR_MAXLOCALS || rhs.lo < 0) return;
    proven_i64 off;
    switch (pred) {
        case IRW_GT: if (!taken) return; off = rhs.lo + 1; break;   // l − r > c
        case IRW_GE: if (!taken) return; off = rhs.lo;     break;   // l − r ≥ c
        case IRW_LT: if (taken)  return; off = rhs.lo;     break;   // ¬(l − r < c) ⇒ ≥ c
        case IRW_LE: if (taken)  return; off = rhs.lo + 1; break;   // ¬(l − r ≤ c) ⇒ > c
        default: return;
    }
    if (off < 0) return;
    iv_put_lerel(st, (proven_size_t)dr, dl, false, off);
    // ★ 그리고 하한도 밀어 올린다 — `hi ≥ lo(lo) + off` 다.
    if (st->loc[dr].lo <= INT64_MAX - off) {
        proven_i64 nl = st->loc[dr].lo + off;
        if (nl > st->loc[dl].lo) st->loc[dl].lo = nl;
    }
}

// ★★★★★ **곱 술어를 좁힌다** (2026-08-17). `f · o REL n` 에서 **인자 하나**에 대한 사실을 낸다:
//   `o ≥ 1` 이면 `f ≤ f·o` 이므로 `f·o < n ⟹ f < n`. 제곱(`f == o`)이면 `f = 0` 도 안전하다
//   (`0 < n` 이므로 `f = 0 < n`). ⇒ 곱을 **계산하지 않고** 인자를 묶는다.
//   ★ **참인 쪽만** 말한다: `f·o ≥ n` 에서 `f ≥ n` 은 나오지 않는다(`f=2,o=9,n=10`).
//   ★ 음수는 배제한다 — `f ≥ 0` 이 아니면 `f ≤ f·o` 가 거짓이다.
static void iv_narrow_prod(ivstate_t *st, proven_u8 pred, proven_i32 pl, proven_i32 pr,
                           iv_t rhs, proven_i32 rslot, bool uns, bool taken) {
    if (pl < 0 || pr < 0 || (proven_size_t)pl >= IR_MAXLOCALS ||
        (proven_size_t)pr >= IR_MAXLOCALS) return;
    bool strict;
    switch (pred) {
        case IRW_LT: if (!taken) return; strict = true;  break;   // f·o < n
        case IRW_LE: if (!taken) return; strict = false; break;   // f·o ≤ n
        case IRW_GT: if (taken)  return; strict = false; break;   // ¬(f·o > n) ⇒ ≤ n
        case IRW_GE: if (taken)  return; strict = true;  break;   // ¬(f·o ≥ n) ⇒ < n
        default: return;
    }
    for (int side = 0; side < 2; side++) {
        proven_size_t f = (proven_size_t)(side ? pr : pl);
        proven_size_t o = (proven_size_t)(side ? pl : pr);
        // ★ `f ≥ 0` — 무부호면 **타입이 보장한다**. 구간만 보면 위드닝이 그것을 잃는다
        //   (실측: 둘째 라운드부터 `lo = INT64_MIN`) ⇒ 규칙이 조용히 안 먹었다.
        if (!uns && st->loc[f].lo < 0) continue;
        if (!(st->loc[o].lo >= 1 || f == o)) continue;          // o ≥ 1, 또는 제곱
        proven_i64 cap = strict ? rhs.hi - 1 : rhs.hi;
        if (rhs.hi > INT64_MIN && cap < st->loc[f].hi) {
            st->loc[f].hi = cap; st->loc[f].wide = rhs.wide;
        }
        if (rslot >= 0 && (proven_size_t)rslot < IR_MAXLOCALS && (proven_size_t)rslot != f)
            iv_put_lerel(st, f, rslot, strict, 0);
        if (f == o) break;                                      // 제곱은 한 번만
    }
}

static void iv_narrow_len(ivstate_t *st, proven_u8 op, proven_i32 lslot, iv_t rhs, bool taken) {
    if (lslot < 0 || (proven_size_t)lslot >= IR_MAXLOCALS || !op) return;
    proven_u8 e = taken ? op : iv_negate(op);
    iv_narrow_iv(&st->lenv[lslot], e, rhs);
}
// ★★ R5 — **관계형 requires 를 진입에서 심는다.** `requires le a b .` 는 상수 범위가 아니어서
//   iv_apply_requires 가 통째로 버렸다(`ir_int_lit` 이 실패하면 `continue`). 그래서 계약을
//   써 놓아도 아무 일도 일어나지 않았다 — 도구는 "add a `requires`" 라고 조언하면서,
//   정작 그 requires 를 **읽지 않고 있었다.**
 void iv_apply_rel_requires(ir_ctx_t *c, const low_cst_t *f, ivstate_t *e0) {
    proven_i32 eqx[8], eqy[8]; int neq = 0;   // ★ `eq (len x) (len y)` 쌍 — 다 읽은 뒤 서로의 길이 사실을 나눈다(아래)
    for (proven_size_t i = 0; i + 3 < f->nkids; i++) {
        proven_size_t k; bool is_assume;
        if (!ir_requires_at(f, i, &k, &is_assume, NULL)) continue;
        if (is_assume) continue;                       // assume 은 검사되지 않는다 ⇒ 사실이 아니다
        proven_u8str_view_t op = f->kids[k]->tok.lex;
        // ★★★ **`requires ge (len s) (mul p q) .` 를 읽는다** (2026-07-30 · 행우선 규칙의 전제).
        //   피연산자가 **원자가 아니라 형(form)** 이라 아래 지역-대-지역 경로가 통째로 놓쳤다:
        //   도구가 *"requires 를 붙여 보라"* 고 조언하면서 정작 이 모양을 **안 읽고 있었다**.
        // ★★ `requires eq (len s) n .` 도 같은 사실을 준다 (2026-10-03). 같음은 «≥» 를 품는다 — 그런데 이 자리가
        //   `ge` 만 읽어서, 더 강한 계약을 쓴 프로그램이 **덜** 받았다(새로 짠 sched 가 `eq` 로 적어 ×2.7, `ge` 로
        //   고치면 ×1.13 — 같은 세션 실측). 거울 `eq n (len s)` 도 읽는다. «≤» 쪽 사실은 여기서 심지 않는다.
        if ((veq(op, "ge") || veq(op, "eq")) && k + 2 < f->nkids) {
            // ★ 괄호는 GROUP 으로 감싸인다(장식) — 벗기고 본다. 안 벗겨서 처음엔 못 읽었다.
            const low_cst_t *L = f->kids[k + 1], *R = f->kids[k + 2];
            while (L && (L->kind == LOW_CST_GROUP) && L->nkids == 1) L = L->kids[0];
            while (R && (R->kind == LOW_CST_GROUP) && R->nkids == 1) R = R->kids[0];
            if (veq(op, "eq") && R && R->kind == LOW_CST_FORM && R->nkids == 2 && is_atom(R->kids[0]) &&
                veq(R->kids[0]->tok.lex, "len")) { const low_cst_t *T = L; L = R; R = T; }
            // ★ **용량 형태** `ge (len s) cap` — 곱의 특수 사례로 담는다(q = -1 = 곱 없음).
            if (L && R && L->kind == LOW_CST_FORM && L->nkids == 2 && is_atom(R) &&
                is_atom(L->kids[0]) && veq(L->kids[0]->tok.lex, "len") && is_atom(L->kids[1])) {
                bool fs, fp;
                proven_size_t s = ir_local_find(c, L->kids[1]->tok.lex, &fs);
                proven_size_t pp = ir_local_find(c, R->tok.lex, &fp);
                if (fs && fp && s < IR_MAXLOCALS && pp < IR_MAXLOCALS) {
                    e0->lenge_p[s] = (proven_i32)pp; e0->lenge_q[s] = -1;
                    // ★ 2026-08-13 (D3): 용량 형태는 **길이 관계이기도 하다** — `len s ≥ p` 는
                    //   곧 `p ≤ len s`. 여기서 `continue` 하므로 아래 거울 읽기가 이 모양을
                    //   못 본다: 그러니 **여기서** 약한 사실도 함께 심는다.
                    iv_put_lenlt(e0, pp, (proven_i32)s, false, 0);
                    continue;
                }
            }
            if (!(L && R && L->kind == LOW_CST_FORM && L->nkids == 2 &&
                  R->kind == LOW_CST_FORM && R->nkids == 3)) { /* 아래 일반 경로로 */ }
            else
            if (is_atom(L->kids[0]) && veq(L->kids[0]->tok.lex, "len") && is_atom(L->kids[1]) &&
                is_atom(R->kids[0]) && veq(R->kids[0]->tok.lex, "mul") &&
                is_atom(R->kids[1]) && is_atom(R->kids[2])) {
                bool fs, fp, fq;
                proven_size_t s = ir_local_find(c, L->kids[1]->tok.lex, &fs);
                proven_size_t pp = ir_local_find(c, R->kids[1]->tok.lex, &fp);
                proven_size_t qq = ir_local_find(c, R->kids[2]->tok.lex, &fq);
                if (fs && fp && fq && s < IR_MAXLOCALS && pp < IR_MAXLOCALS && qq < IR_MAXLOCALS) {
                    e0->lenge_p[s] = (proven_i32)pp; e0->lenge_q[s] = (proven_i32)qq;
                    continue;
                }
            }
        }
        // ★★★★★ **길이끼리의 관계를 심는다** (2026-09-10, RFC-0111 §8-17 · WO-0194).
        //   `requires le (len b) (len prev) .` — 두 배열을 나란히 쓰는 코드가 실제로 적는 모양.
        //   진입 검사는 이제 이 절을 **세운다**(low_ir.c). 여기서 그 사실을 **쓴다**:
        //   나중에 `v ≤ len b` 인 값으로 `prev` 를 색인하면 `v < len prev` 가 따라 나온다.
        //   ☞ *강제되는 절만 사실이 된다 — 그리고 사실이 되는 절만 값을 한다.*
        {
            const low_cst_t *L2 = f->kids[k + 1], *R2 = f->kids[k + 2];
            while (L2 && L2->kind == LOW_CST_GROUP && L2->nkids == 1) L2 = L2->kids[0];
            while (R2 && R2->kind == LOW_CST_GROUP && R2->nkids == 1) R2 = R2->kids[0];
            bool lf = L2 && L2->kind == LOW_CST_FORM && L2->nkids == 2 && is_atom(L2->kids[0]) &&
                      veq(L2->kids[0]->tok.lex, "len") && is_atom(L2->kids[1]);
            bool rf = R2 && R2->kind == LOW_CST_FORM && R2->nkids == 2 && is_atom(R2->kids[0]) &&
                      veq(R2->kids[0]->tok.lex, "len") && is_atom(R2->kids[1]);
            if (lf && rf) {
                const low_cst_t *sm = NULL, *bg = NULL; bool strict2 = false; bool got = false;
                if (veq(op, "eq")) {
                    bool f1, f2;
                    proven_size_t s1 = ir_local_find(c, L2->kids[1]->tok.lex, &f1);
                    proven_size_t s2 = ir_local_find(c, R2->kids[1]->tok.lex, &f2);
                    if (f1 && f2 && s1 < IR_MAXLOCALS && s2 < IR_MAXLOCALS && s1 != s2 && neq < 8) {
                        eqx[neq] = (proven_i32)s1; eqy[neq] = (proven_i32)s2; neq++;
                        continue;
                    }
                }
                if      (veq(op, "lt")) { sm = L2; bg = R2; strict2 = true;  got = true; }
                else if (veq(op, "le")) { sm = L2; bg = R2; strict2 = false; got = true; }
                else if (veq(op, "gt")) { sm = R2; bg = L2; strict2 = true;  got = true; }
                else if (veq(op, "ge")) { sm = R2; bg = L2; strict2 = false; got = true; }
                if (got) {
                    bool f1, f2;
                    proven_size_t s1 = ir_local_find(c, sm->kids[1]->tok.lex, &f1);
                    proven_size_t s2 = ir_local_find(c, bg->kids[1]->tok.lex, &f2);
                    if (f1 && f2 && s1 < IR_MAXLOCALS && s2 < IR_MAXLOCALS && s1 != s2) {
                        e0->slenle_t[s2] = (proven_i32)s1;
                        e0->slenle_str[s2] = (proven_u8)(strict2 ? 1 : 0);
                        // ★ 엄격이면 **긴 쪽은 비어 있을 수 없다**: len(t) > len(s) ≥ 0 ⇒ len(t) ≥ 1.
                        //   `index cur 0` 같은 상수 색인이 이 한 줄로 닫힌다.
                        if (strict2 && e0->lenv[s2].lo < 1) { e0->lenv[s2].lo = 1; e0->lenv[s2].wide = 0; }
                        continue;
                    }
                }
            }
        }
        // ★★★★★ **`requires lt i (len s) .` 를 사실로 심는다** (RFC-0093, 2026-08-10).
        //   이것이 없어서 **계약이 `guard` 보다 느렸다.** 실측(같은 함수, 같은 조건):
        //       guard    lt i (len a) .  →  방출 C 가 **맨 접근** `s_.p[i_]`      (검사 제거됨)
        //       requires lt i (len a) .  →  `lw_panic("slice index out of bounds")` **남음**
        //   즉 이 언어가 내세우는 *"계약을 비용에서 자산으로"*(SPARK 의 핵심 통찰)가
        //   **바로 그 자리에서 뒤집혀 있었다**: 계약을 쓰면 검사가 하나 더 붙는다.
        //   ☞ 원인은 이 함수가 `ge (len s) …` 만 읽고 **`lt <이름> (len s)` 는 안 읽은 것**이다.
        //     그런데 그 모양은 색인 제거(RFC-0055 D5)가 요구하는 **바로 그 관계 사실**이고,
        //     도구의 진단문이 *"`requires` 를 붙여 보라"* 고 **권하는 모양**이기도 했다.
        //     **도구가 시킨 대로 써도 아무 일이 안 일어났다** — expr 섬에서 겪은 것과 같다.
        //   ★ 진입 requires 검사(ASSERT a=0)는 **여전히 제거하지 않는다**(위 IRW_ASSERT 주석):
        //     그래야 자기가 심은 사실로 자기를 정당화하는 순환이 안 생긴다. 즉 계약은
        //     **진입에서 한 번** 검사되고, 그 대가로 본문의 색인 검사가 **전부** 사라진다.
        {
            const low_cst_t *L = f->kids[k + 1], *R = f->kids[k + 2];
            while (L && L->kind == LOW_CST_GROUP && L->nkids == 1) L = L->kids[0];
            while (R && R->kind == LOW_CST_GROUP && R->nkids == 1) R = R->kids[0];
            // `lt i (len s)` 와 그 거울 `gt (len s) i` 를 같은 사실로 읽는다.
            //   ★★ 2026-08-13 (D3) — **거울 넷을 한자리에서 읽는다.** 전에는 `lt`/`gt` 만
            //     읽었고 `le n (len a)` 는 아래 원자-원자 경로에서 `(len a)` 가 FORM 이라
            //     **걸러졌다** — 즉 그 계약은 아무 사실도 안 남겼다(실측: 검사 1 남음).
            //     `ge (len a) n` 은 **용량 형태**로 위에서 먼저 잡혀 우연히 통했는데,
            //     그 거울인 `le n (len a)` 만 못 통한 것은 RFC-0093 이 적은
            //     *"이웃을 물어라"* 의 또 한 판이다. 네 모양을 **한 곳에서** 접는다.
            const low_cst_t *nm = NULL, *ln = NULL; bool strict = true;
            if (veq(op, "lt") && is_atom(L) && R && R->kind == LOW_CST_FORM) { nm = L; ln = R; }
            else if (veq(op, "gt") && is_atom(R) && L && L->kind == LOW_CST_FORM) { nm = R; ln = L; }
            else if (veq(op, "le") && is_atom(L) && R && R->kind == LOW_CST_FORM) { nm = L; ln = R; strict = false; }
            else if (veq(op, "ge") && is_atom(R) && L && L->kind == LOW_CST_FORM) { nm = R; ln = L; strict = false; }
            if (nm && ln && ln->nkids == 2 && is_atom(ln->kids[0]) &&
                veq(ln->kids[0]->tok.lex, "len") && is_atom(ln->kids[1])) {
                bool fi, fs;
                proven_size_t iv = ir_local_find(c, nm->tok.lex, &fi);
                proven_size_t sv = ir_local_find(c, ln->kids[1]->tok.lex, &fs);
                if (fi && fs && iv < IR_MAXLOCALS && sv < IR_MAXLOCALS) {
                    iv_put_lenlt(e0, iv, (proven_i32)sv, strict, 0);      // n (<|≤) len s
                    continue;
                }
            }
        }
        proven_i64 dummy;
        if (!is_atom(f->kids[k + 1]) || !is_atom(f->kids[k + 2])) continue;   // 원자만 아래 경로다
        if (ir_int_lit(f->kids[k + 2]->tok.lex, &dummy)) continue;   // 상수 우변은 구간이 맡는다
        bool fa, fb;
        proven_size_t a = ir_local_find(c, f->kids[k + 1]->tok.lex, &fa);
        proven_size_t b = ir_local_find(c, f->kids[k + 2]->tok.lex, &fb);
        if (!fa || !fb || a >= IR_MAXLOCALS || b >= IR_MAXLOCALS || a == b) continue;
        if      (veq(op, "lt")) iv_put_lerel(e0, a, (proven_i32)b, true,  0);
        else if (veq(op, "le")) iv_put_lerel(e0, a, (proven_i32)b, false, 0);
        else if (veq(op, "gt")) iv_put_lerel(e0, b, (proven_i32)a, true,  0);
        else if (veq(op, "ge")) iv_put_lerel(e0, b, (proven_i32)a, false, 0);
        else if (veq(op, "eq")) { iv_put_lerel(e0, a, (proven_i32)b, false, 0);
                                  iv_put_lerel(e0, b, (proven_i32)a, false, 0); }
    }
    // ★★ **같은 길이는 같은 사실을 갖는다** (2026-10-04). `requires eq (len cur) (len prev)` 와 `requires lt (len b) (len prev)`
    //   를 함께 적으면 `len b < len cur` 도 참이다 — 그런데 앞의 절은 아무 사실도 안 심었다(`eq` 는 수와 길이 사이만 읽었다).
    //   편집 거리 새 판의 `cur` 색인 셋이 그래서 검사로 남았다. 두 번 돌면 사슬(셋 이상)도 닫힌다.
    //   ☞ 건전성: 두 절 모두 진입에서 **강제된다**. 길이 칸은 하나라서 이미 사실이 있는 쪽은 덮지 않는다.
    for (int pass = 0; pass < 2; pass++)
        for (int q = 0; q < neq; q++)
            for (int dir = 0; dir < 2; dir++) {
                proven_i32 x = dir ? eqy[q] : eqx[q], y = dir ? eqx[q] : eqy[q];
                if (e0->slenle_t[y] < 0 && e0->slenle_t[x] >= 0 && e0->slenle_t[x] != y) {
                    e0->slenle_t[y] = e0->slenle_t[x]; e0->slenle_str[y] = e0->slenle_str[x];
                }
                if (e0->lenv[x].lo > e0->lenv[y].lo) { e0->lenv[y].lo = e0->lenv[x].lo; e0->lenv[y].wide = 0; }
                if (!e0->lenv[x].wide && e0->lenv[x].hi < e0->lenv[y].hi) e0->lenv[y].hi = e0->lenv[x].hi;
            }
    for (int q = 0; q < neq; q++) {   // 남는 칸이면 서로를 «≤» 로 적는다(`v < len x` 인 값으로 y 를 색인할 때)
        if (e0->slenle_t[eqx[q]] < 0) { e0->slenle_t[eqx[q]] = eqy[q]; e0->slenle_str[eqx[q]] = 0; }
        if (e0->slenle_t[eqy[q]] < 0) { e0->slenle_t[eqy[q]] = eqx[q]; e0->slenle_str[eqy[q]] = 0; }
    }
}
// ★★★★★ **관계 사실은 수치 상계로 **닫혀야** 쓸모가 있다** (2026-08-19, http 에서 잡혔다).
//
//   실측이 가르쳐 준 것: `bench_http` 의 도우미에 **정확히 맞는** 계약을 다 써 줘도
//     requires le start finish .   requires le finish (len src) .   requires le (len src) 16777216 .
//   `add start k` 의 검사가 **안 지워졌다**. 그런데 `requires le start 16777216 .` 을
//   손으로 한 줄 더 쓰면 지워졌다 — 즉 사람이 **사슬을 대신 이어 주고 있었다**.
//   ☞ 계약을 옳게 써도 안 되는데 *한 번 더* 쓰면 되는 도구는, 자기가 아는 것을 안 쓰는 것이다.
//
//   ★★★★★★ **2026-08-21(RFC-0095 S-B) — 그리고 여기서 방향이 반대인 결함이 나왔다.**
//     같은 사실인데 **어디서 왔느냐에 따라 도구가 다르게 답했다**(실측):
//         guard le j hi . else … .  + requires lt hi (len a) .  ⇒ `index a j` **지워진다**
//         requires le j hi .        + requires lt hi (len a) .  ⇒ **안 지워진다**
//     ☞ **계약으로 쓴 쪽이 진 것이다.** 그리고 도구의 진단문은 그 와중에도
//       *"a `range`/`requires` bound … INCLUDING a relational one"* 을 권하고 있었다.
//     원인: 본문 경로(`iv_narrow`)에는 **추이 한 걸음**이 있다 —
//       `i + c (<|≤) n` ∧ `n + m (<|≤) len(g)` ⟹ `i + (c+m) (<|≤) len(g)`.
//     진입 경로에는 그것이 **없었다**: `iv_apply_rel_requires` 는 `lerel` 만 심고,
//     이 폐포는 관계를 **수치 상계**로만 바꿨다. 그런데 `len a` 에는 수치 상계가 없다
//     (`IV_LEN.hi` 는 하한의 자리 표시자이지 상계가 아니다 — 이 파일이 이미 그렇게 적어 뒀다)
//     ⇒ 진입에서는 사슬이 **닿을 곳이 없었다.**
//   ⇒ 그래서 이 폐포에 **관계 ∘ 길이관계** 합성을 넣는다(아래 ②). 그러면 두 경로가 같아진다.
//     RFC-0093 이 이름 붙인 모양 그대로다 — *한 자리에서 배운 것을 이웃에 옮겨 적지 않았다.*
//
//   그래서 **사실이 심긴 자리마다** 닫는다(전이적 폐포):
//     · `loc[k] + leoff[k] (<|≤) loc[p]`      ⟹ hi[k] ≤ hi[p] − 여백
//     · `loc[k] + lenoff[k] (<|≤) lenv[s]`    ⟹ hi[k] ≤ lenv[s].hi − 여백
//   두 사실 모두 **검사되는** requires 에서만 온다(assume 은 위에서 걸러진다) ⇒ 진짜 사실이다.
//   합성은 순수 전이(`a ≤ b ∧ b ≤ B ⟹ a ≤ B`)라 새 규칙 id 가 필요 없다 — 증명서는 좁혀진
//   구간을 그대로 싣고, 검증기는 **추론**을 다시 한다(사실은 믿는다, 이 파일 머리말 참조).
//
//   ★ `wide`(타입에서 잘라 온 hi)는 **상계로 쓰지 않는다** — 그 hi 는 사실이 아니다.
//   ★ 반복은 슬롯 수만큼이면 어떤 사슬도 닫힌다(각 바퀴가 최소 한 칸을 확정한다).
static void iv_close_facts(ivstate_t *st) {
    for (proven_size_t round = 0; round < 8; round++) {
        bool moved = false;
        // ★★★★★ **관계 ∘ 길이관계** (RFC-0095 S-B) — `k + mk ≤ p` 이고 `p + mp ≤ len(s)` 이면
        //   `k + (mk + mp) ≤ len(s)`. 본문 경로가 이미 하는 걸음이고(`iv_narrow` ②),
        //   진입 경로에는 없었다. 여백은 **더한다** — 엄격은 여백 안에 이미 들어 있다
        //   (margin = off + strict), 그래서 결론은 `strict=false` 로 싣는다.
        for (proven_size_t k = 0; k < IR_MAXLOCALS; k++) {
            proven_i32 p = st->lerel[k];
            if (p < 0 || (proven_size_t)p >= IR_MAXLOCALS || (proven_size_t)p == k) continue;
            proven_i32 sl = st->lenlt[p];
            proven_i64 mk = st->leoff[k] + (st->lestr[k] ? 1 : 0);
            // ★★★★ §8-23 — `p` 가 `len u` **그 자체**면 `k + mk ≤ len u` 다. 그 사실은 `p` 의 한 칸
            //   (`lenlt[p]`, 뒤 가드가 덮어쓴 칸)보다 **넓게** 쓰인다: 슬라이스끼리의 사실
            //   (`len u < len s`)과 이어져 `u` 보다 긴 **모든** 슬라이스의 색인을 닫는다.
            //   ☞ `p` 의 칸이 여백 2 이상을 주면(벡터 적재의 `m + 4 ≤ len s`) 옛길을 둔다 —
            //     슬라이스끼리 사실이 주는 여백은 1 이 전부라 그 여백을 잃으면 안 된다.
            proven_i32 u = st->lenofloc[p];
            if (u >= 0 && (proven_size_t)u < IR_MAXLOCALS && (proven_size_t)u != k && mk >= 0 &&
                (sl < 0 || (proven_size_t)sl >= IR_MAXLOCALS || iv_margin(st, (proven_size_t)p, sl) <= 1)) {
                proven_i64 before_s = st->lenlt[k], before_m = iv_margin(st, k, u);
                iv_put_lenlt(st, k, u, false, mk);
                if (st->lenlt[k] != before_s || iv_margin(st, k, u) != before_m) moved = true;
                continue;
            }
            if (sl < 0 || (proven_size_t)sl >= IR_MAXLOCALS) continue;
            proven_i64 mp = iv_margin(st, (proven_size_t)p, sl);
            if (mk < 0 || mp < 0 || mk > INT64_MAX - mp) continue;
            proven_i64 before_s = st->lenlt[k], before_m = iv_margin(st, k, sl);
            iv_put_lenlt(st, k, sl, false, mk + mp);
            if (st->lenlt[k] != before_s || iv_margin(st, k, sl) != before_m) moved = true;
        }
        for (proven_size_t k = 0; k < IR_MAXLOCALS; k++) {
            proven_i64 cap = 0; bool have = false;
            proven_i32 p = st->lerel[k];
            if (p >= 0 && (proven_size_t)p < IR_MAXLOCALS && !st->loc[p].wide) {
                proven_i64 m = st->leoff[k] + (st->lestr[k] ? 1 : 0);
                if (st->loc[p].hi >= m) { cap = st->loc[p].hi - m; have = true; }
            }
            proven_i32 sl = st->lenlt[k];
            if (sl >= 0 && (proven_size_t)sl < IR_MAXLOCALS && !st->lenv[sl].wide) {
                proven_i64 m = st->lenoff[k] + (st->lenstr[k] ? 1 : 0);
                if (st->lenv[sl].hi >= m) {
                    proven_i64 c2 = st->lenv[sl].hi - m;
                    if (!have || c2 < cap) { cap = c2; have = true; }
                }
            }
            //   ★★ **상계처럼 생겼다고 사실인 것은 아니다** (2026-08-19, 이 패스가 처음
            //     낸 회귀가 가르쳤다): `IV_LEN` 의 hi 는 `INT64_MAX` 인데 그것은 *"길이는
            //     음수가 아니다"* 라는 하한의 **자리 표시자**이지 상계가 아니다. 그것을 사실로
            //     읽으면 `wide`(= 이 hi 는 타입에서 잘라 온 것이라 못 믿는다)를 **꺼 버린다**
            //     ⇒ vm_smt 의 두 홉 사슬 둘이 조용히 증명을 잃었다(3227 vs 3229, 래칫이 잡았다).
            //   ☞ 정보가 없는 상계는 **쓰지 않는다**. 래칫이 없었으면 못 봤다.
            if (!have || cap >= INT64_MAX) continue;
            if (st->loc[k].wide || st->loc[k].hi > cap) {
                if (cap < st->loc[k].lo) continue;      // 모순 — 넓히지도 좁히지도 않는다
                st->loc[k].hi = cap; st->loc[k].wide = 0; moved = true;
            }
        }
        if (!moved) break;
    }
}
// ★★★ **행우선 인덱스 규칙** (2026-07-30) — 구간·한 칸 관계로는 못 하는 **비선형** 한 조각.
//
//   증명 의무를 그대로 옮긴다:
//       len(s) ≥ p·q   (F1: 진입 requires — 그 곱은 **트랩 곱**이고 requires 검사는 제거되지 않는다
//                           ⇒ 본문에 도달했다면 p·q 는 넘치지 않았다)
//       i < p          (F2: 관계 사실, 엄격)
//       k < q          (F3: 관계 사실, 엄격)
//       주소 = i·q + k (트랩 곱·트랩 합으로 계산됐다 — wrap/sat 이면 출처를 안 싣는다)
//   ⟹ i ≤ p−1 이므로 i·q ≤ p·q − q, k ≤ q−1 이므로
//      i·q + k ≤ p·q − 1 < p·q ≤ len(s).  ∎  그리고 무부호(또는 음이 아님)이므로 ≥ 0.
//
//   ☞ 한 조각도 빼지 않는다: F1 이 없으면(계약을 안 썼으면) **증명하지 않는다**. 검사를 지우는
//     최적화의 실패는 느려지는 것이 아니라 **틀린 메모리 접근**이다.
static bool iv_row_major_ok(const ivstate_t *st, const ivs_t *idx, proven_i32 sslot) {
    if (sslot < 0 || (proven_size_t)sslot >= IR_MAXLOCALS) return false;
    // ★★ **상수 인덱스**: `len(s) ≥ cap` ∧ `cap ≥ c+1` ⟹ `c < len(s)`.
    //   `index cache_age 0` 같은 자리가 이것이다 — 계약이 `requires ge cap 1 .` 을 주면 닫힌다.
    //   ☞ 상수의 구간은 [c,c] 이므로 `idx->cst` 대신 구간을 쓴다(둘이 같다).
    if (st->lenge_q[sslot] < 0 && st->lenge_p[sslot] >= 0 && idx->slot == -2 &&
        (proven_size_t)st->lenge_p[sslot] < IR_MAXLOCALS &&
        idx->v.lo >= 0 && idx->v.lo == idx->v.hi &&
        st->loc[st->lenge_p[sslot]].lo > idx->v.hi)
        return true;
    // ★★ **용량 형태**: `len(s) ≥ cap` ∧ `i < cap` ⟹ `i < len(s)`. 곱이 없어 선형이지만,
    //   사실의 출처가 같으므로(계약) 같은 자리에서 답한다. lru·sched 가 이 모양이다.
    if (st->lenge_q[sslot] < 0 && st->lenge_p[sslot] >= 0 && idx->slot >= 0 &&
        (proven_size_t)idx->slot < IR_MAXLOCALS &&
        st->lerel[idx->slot] == st->lenge_p[sslot] && st->lestr[idx->slot] &&
        st->loc[idx->slot].lo >= 0)
        return true;
    // ★★★★★ **전이 한 걸음** (2026-09-10, RFC-0111 §8-17 · WO-0194):
    //   `v (<|≤) len(u)` 이고 `len(u) (<|≤) len(s)` 이면 `v < len(s)` 다 — 둘 중 **적어도
    //   하나가 엄격**해야 한다(둘 다 «≤» 면 결론은 «≤» 이고 그것은 색인에 모자란다).
    //   이 한 걸음이 편집 거리의 계약을 **크기 독립**으로 만든다: `le (len b) (len prev)` 만
    //   적으면 표 두 줄을 도는 색인이 전부 닫힌다(전에는 1500/1501 을 손으로 박아야 했다).
    //   ☞ 건전성: 두 사실 다 **진입에서 강제된다**(교훈 1 — 믿는 것은 강제되는 것뿐).
    if (idx->lenlt >= 0 && (proven_size_t)idx->lenlt < IR_MAXLOCALS && idx->v.lo >= 0 &&
        st->slenle_t[sslot] == idx->lenlt) {
        bool idx_strict = (idx->lenoff + (idx->lenstr ? 1 : 0)) >= 1;
        if (idx_strict || st->slenle_str[sslot]) return true;
    }
    // ★ 같은 걸음의 짝: 값이 **길이 그 자체**일 때(`let m be len b .` 뒤의 `index prev m`).
    //   `m = len(u)` 이고 `len(u) < len(s)` 이면 `m < len(s)` 다 — 여기서는 **엄격이 필요**하다
    //   (`≤` 면 `m = len s` 일 수 있고 그것은 색인 밖이다).
    if (idx->lenof >= 0 && (proven_size_t)idx->lenof < IR_MAXLOCALS && idx->v.lo >= 0 &&
        st->slenle_t[sslot] == idx->lenof && st->slenle_str[sslot])
        return true;
    if (idx->row_i < 0 || idx->row_q < 0 || idx->row_k < 0) return false;
    proven_i32 p = st->lenge_p[sslot], q = st->lenge_q[sslot];
    if (p < 0 || q < 0) return false;                       // F1 없음 ⇒ 증명 안 함
    if (idx->row_q != q) return false;                      // 곱한 것이 열 수(q)여야 한다
    if ((proven_size_t)idx->row_i >= IR_MAXLOCALS || (proven_size_t)idx->row_k >= IR_MAXLOCALS)
        return false;
    if (st->lerel[idx->row_i] != p || !st->lestr[idx->row_i]) return false;   // F2: i < p (엄격)
    if (st->lerel[idx->row_k] != q || !st->lestr[idx->row_k]) return false;   // F3: k < q (엄격)
    if (st->loc[idx->row_i].lo < 0 || st->loc[idx->row_k].lo < 0) return false;  // 음수 아님
    return true;
}
// ═══ SMT 백엔드 — 경계 증명을 **일반 절차**로 (REQ-0004 · 후속 M, 2026-08-05) ═══════════
//
// ★★★ 위의 `iv_row_major_ok` 를 보라: 상수 인덱스 · 용량 형태 · 행우선 — **손으로 넣은 특수
//   경우 셋**이다. 넷째가 필요해지면 넷째를 또 손으로 넣게 된다. 그 셋이 하는 일은 전부 같다:
//   *"가진 사실들을 선형으로 엮어 i < len(s) 를 이끌어낸다."* ⇒ 그 엮는 일을 절차에 맡긴다.
//
// ★★ 이 절차가 특수 경우들보다 **더** 하는 일 — 그것이 이 코드의 존재 이유다:
//     · **사슬**: `i < j` 와 `j < p` 와 `len ≥ p` 를 이으면 `i < len` 이다. `lerel` 은 한 칸만
//       보므로 그전에는 못 넘었다(두 홉이 필요하다).
//     · **구간과 관계의 혼합**: `i ≤ n−1`(구간) + `len ≥ n`(계약)처럼 출처가 다른 사실을 섞는다.
//     · 그리고 무엇을 썼는지 **Farkas 계수**로 적어 낸다 — 검증기가 곱해 더해 재현한다.
//
// ★ 못 하는 것도 그대로 적는다: 분리(∨)·비선형(행우선의 p·q)은 다루지 않는다. 행우선은
//   위 특수 경우가 계속 맡는다 — **선형 절차로 대체할 수 없는 진짜 비선형 조각**이기 때문이다.

#define IVS_LENBIT 4096              /* 변수 키: 슬롯 = 값, 슬롯+LENBIT = 그 슬라이스의 길이 */

typedef struct {
    proven_i32 key[LOW_SMT_MAXV];
    proven_size_t nv;
} ivs_map_t;

static proven_i32 ivs_var(ivs_map_t *m, proven_i32 key) {
    for (proven_size_t i = 0; i < m->nv; i++) if (m->key[i] == key) return (proven_i32)i;
    if (m->nv >= LOW_SMT_MAXV) return -1;                  // ★ 넘치면 **안 담는다**(포기 = 안전)
    m->key[m->nv] = key;
    return (proven_i32)m->nv++;
}

// 제약 하나를 담는다: coef 두 개짜리(대부분 그렇다) + 상수.
static bool ivs_c2(low_smt_sys_t *s, proven_i32 v1, proven_i64 c1,
                   proven_i32 v2, proven_i64 c2, proven_i64 k) {
    proven_i64 a[LOW_SMT_MAXV] = {0};
    if (v1 >= 0) a[v1] += c1;
    if (v2 >= 0) a[v2] += c2;
    return low_smt_add(s, a, k);
}

// ★★★ **i < len(s) 를 SMT 로 증명한다.** 성공하면 계(sys)와 계수(lam)를 돌려준다 —
//   그 둘이 곧 증명서다. 실패는 *"모른다"* 이지 *"거짓"* 이 아니므로 호출자는 검사를 남긴다.
// ★★★ **사실 수집은 한 벌이다** (2026-08-18). 색인 증명과 산술 증명이 같은 사실을 쓰는데
//   두 벌로 적으면 **반드시 갈린다**(이 저장소가 두 번 당한 모양). 그래서 씨앗 슬롯을 받아
//   관계 간선을 두 홉 따라가고, 담긴 변수마다 아는 사실을 전부 싣는 함수를 하나만 둔다.
//   목표(의 부정)는 부르는 쪽이 붙인다 — 그것만이 두 증명의 차이다.
static bool iv_smt_load(const ivstate_t *st, const proven_i32 *seed, proven_size_t nseed,
                        proven_i32 lenslot, low_smt_sys_t *sys, ivs_map_t *map) {
    map->nv = 0;
    // ── 관련 슬롯을 모은다 — 씨앗에서 출발해 관계 간선을 **두 홉**까지 따라간다.
    //   (더 멀리 가면 변수가 상한을 넘고, 그러면 어차피 포기한다.)
    proven_i32 want[LOW_SMT_MAXV];
    proven_size_t nw = 0;
    for (proven_size_t q = 0; q < nseed; q++) {
        if (seed[q] < 0 || (proven_size_t)seed[q] >= IR_MAXLOCALS) continue;
        bool dup = false;
        for (proven_size_t z = 0; z < nw; z++) if (want[z] == seed[q]) dup = true;
        if (!dup && nw < LOW_SMT_MAXV) { want[nw++] = seed[q]; LOW_HWM("ir:smt-vars", nw, LOW_SMT_MAXV); }
    }
    if (!nw) return false;
    for (proven_size_t hop = 0; hop < 2; hop++) {
        proven_size_t n0 = nw;
        for (proven_size_t k = 0; k < n0; k++) {
            proven_i32 sl = want[k];
            proven_i32 up = st->lerel[sl];
            if (up >= 0 && (proven_size_t)up < IR_MAXLOCALS && nw < LOW_SMT_MAXV) {
                LOW_HWM("ir:smt-vars", nw + 1, LOW_SMT_MAXV);
                bool dup = false;
                for (proven_size_t q = 0; q < nw; q++) if (want[q] == up) dup = true;
                if (!dup) want[nw++] = up;
            }
        }
    }
    // ★ 길이 쪽 사실이 가리키는 슬롯도 담는다 — `len(s) ≥ p` 의 p 가 그것이다.
    if (lenslot >= 0 && (proven_size_t)lenslot < IR_MAXLOCALS &&
        st->lenge_q[lenslot] < 0 && st->lenge_p[lenslot] >= 0 &&
        (proven_size_t)st->lenge_p[lenslot] < IR_MAXLOCALS && nw < LOW_SMT_MAXV) {
        bool dup = false;
        for (proven_size_t q = 0; q < nw; q++) if (want[q] == st->lenge_p[lenslot]) dup = true;
        if (!dup) want[nw++] = st->lenge_p[lenslot];
    }

    low_smt_init(sys, 0);
    // ★ 씨앗과 (있으면) 길이 변수를 **먼저** 잡는다 — 부르는 쪽이 그 번호를 기대한다.
    for (proven_size_t q = 0; q < nseed; q++)
        if (seed[q] >= 0 && (proven_size_t)seed[q] < IR_MAXLOCALS) (void)ivs_var(map, seed[q]);
    if (lenslot >= 0) (void)ivs_var(map, lenslot + IVS_LENBIT);
    for (proven_size_t k = 0; k < nw; k++) (void)ivs_var(map, want[k]);
    // ★ 담긴 지역이 가리키는 **길이** 변수도 담는다(`x < len t`) — 자리가 남는 만큼만.
    for (proven_size_t k = 0, n0 = map->nv; k < n0; k++) {
        proven_i32 key = map->key[k];
        if (key >= IVS_LENBIT) continue;
        proven_i32 lt = st->lenlt[key];
        if (lt >= 0 && (proven_size_t)lt < IR_MAXLOCALS) (void)ivs_var(map, lt + IVS_LENBIT);
        proven_i32 lo_ = st->lenofloc[key];
        if (lo_ >= 0 && (proven_size_t)lo_ < IR_MAXLOCALS) (void)ivs_var(map, lo_ + IVS_LENBIT);
    }
    sys->nv = map->nv;

    // ── 사실을 담는다 ─────────────────────────────────────────────────────────
    for (proven_size_t k = 0; k < map->nv; k++) {
        proven_i32 key = map->key[k];
        bool is_len = key >= IVS_LENBIT;
        proven_i32 sl = is_len ? key - IVS_LENBIT : key;
        if ((proven_size_t)sl >= IR_MAXLOCALS) continue;
        iv_t rng = is_len ? st->lenv[sl] : st->loc[sl];
        // 구간: 유한한 쪽만 담는다(⊤ 는 사실이 아니라 무지다).
        if (rng.lo > INT64_MIN) { if (!ivs_c2(sys, (proven_i32)k, -1, -1, 0,  rng.lo)) return false; }
        // ★ **자른 상한은 사실이 아니다**(`iv_t.wide`, 부록 C) — 담으면 없는 사실로 증명하게 된다.
        if (rng.hi < INT64_MAX && !rng.wide) { if (!ivs_c2(sys, (proven_i32)k, 1, -1, 0, -rng.hi)) return false; }
        if (is_len) continue;
        // 차분 제약  x_sl ≤ x_up (− 여백)
        proven_i32 up = st->lerel[sl];
        if (up >= 0 && (proven_size_t)up < IR_MAXLOCALS) {
            proven_i32 vu = -1;
            for (proven_size_t q = 0; q < map->nv; q++) if (map->key[q] == up) vu = (proven_i32)q;
            if (vu >= 0 && !ivs_c2(sys, (proven_i32)k, 1, vu, -1, iv_lemargin(st, sl, up))) return false;
        }
        // 길이 관계  x_sl + m ≤ len(t)
        proven_i32 lt = st->lenlt[sl];
        if (lt >= 0) {
            proven_i32 vl = -1;
            for (proven_size_t q = 0; q < map->nv; q++) if (map->key[q] == lt + IVS_LENBIT) vl = (proven_i32)q;
            proven_i64 mg = iv_margin(st, sl, lt);
            if (vl >= 0 && !ivs_c2(sys, (proven_i32)k, 1, vl, -1, mg)) return false;
        }
        // 등식  x_sl = len(t)
        proven_i32 lo_ = st->lenofloc[sl];
        if (lo_ >= 0) {
            proven_i32 vl = -1;
            for (proven_size_t q = 0; q < map->nv; q++) if (map->key[q] == lo_ + IVS_LENBIT) vl = (proven_i32)q;
            if (vl >= 0) {
                if (!ivs_c2(sys, (proven_i32)k, 1, vl, -1, 0)) return false;
                if (!ivs_c2(sys, (proven_i32)k, -1, vl, 1, 0)) return false;
            }
        }
    }
    // 계약  len(s) ≥ p   (곱이 없는 용량 형태만 — 곱은 비선형이라 특수 경우가 맡는다)
    if (lenslot >= 0 && (proven_size_t)lenslot < IR_MAXLOCALS &&
        st->lenge_q[lenslot] < 0 && st->lenge_p[lenslot] >= 0) {
        proven_i32 vp = -1, vL = -1;
        for (proven_size_t q = 0; q < map->nv; q++) {
            if (map->key[q] == st->lenge_p[lenslot]) vp = (proven_i32)q;
            if (map->key[q] == lenslot + IVS_LENBIT)  vL = (proven_i32)q;
        }
        if (vp >= 0 && vL >= 0 && !ivs_c2(sys, vp, 1, vL, -1, 0)) return false;
    }
    return true;
}

// ★★★★★ **SMT 를 산술 검사에도 붙인다** (2026-08-18, REQ-0004 확장).
//   지금까지 이 백엔드는 **색인 경계에만** 걸려 있었다. 그런데 `check-erasure` 가 매번 찍는
//   *"남은 검사 N 개 = SMT 가 넘볼 수 있는 자리의 상한"* 은 **산술 검사**의 수다 —
//   즉 그 문장이 가리키는 자리에 정작 솔버가 안 닿고 있었다. op 별로 세어 보면
//   `add` 1798 · `mul` 647 · `sub` 191 이고, 특수 경우(부록 E·F)가 못 넘은 나머지가 여기 온다.
//
//   목표(의 부정)는 op 와 폭에 따라 셋이다:
//     · SUB(무부호): **하한만** 필요하다 — 상한은 `a − b ≤ a ≤ MAX` 로 공짜다(부록 E).
//       부정:  `a − b ≤ −1`  ⇔  `a − b + 1 ≤ 0`
//     · ADD(무부호, 폭 < 64): 부정은 `MAX + 1 − a − b ≤ 0`
//     · ADD(무부호, 폭 64): **MAX 가 도메인(i64)에 안 담긴다.** 그래서 계 안의 **증인 변수** V 를
//       쓴다: `a + b ≤ V` 를 보이면 된다(V 가 그 타입의 값이므로 `V ≤ MAX`).
//       부정: `V + 1 − a − b ≤ 0`. 후보를 차례로 시도하고 하나라도 반박되면 참이다.
//       ★ 증인 자격: **길이**는 언제나 ≥ 0 이고, **지역**은 `loc[V].lo ≥ 0` 일 때만 쓴다
//         (음수 지역과의 관계를 무부호 덧셈에 쓰면 뜻이 갈린다).
//   ★ 부호형은 아직 안 한다 — MIN 쪽까지 두 목표가 되고, 되돌아오는 자리가 아직 안 세어졌다.
static bool iv_smt_arith_ok(const ivstate_t *st, low_irw_t w, ityp_t t,
                            proven_i32 as, proven_i32 bs, bool b_const, proven_i64 bval,
                            low_smt_sys_t *sys, proven_i64 *lam, ivs_map_t *map) {
    if (t.sign || !t.bits || t.bits > 64) return false;
    if (w != IRW_ADD && w != IRW_SUB) return false;
    if (as < 0 || (proven_size_t)as >= IR_MAXLOCALS) return false;
    if (!b_const && (bs < 0 || (proven_size_t)bs >= IR_MAXLOCALS)) return false;
    if (b_const && bval < 0) return false;

    proven_i32 seed[2]; proven_size_t ns = 0;
    seed[ns++] = as;
    if (!b_const) seed[ns++] = bs;
    if (!iv_smt_load(st, seed, ns, -1, sys, map)) return false;

    proven_i32 va = -1, vb = -1;
    for (proven_size_t q = 0; q < map->nv; q++) {
        if (map->key[q] == as) va = (proven_i32)q;
        if (!b_const && map->key[q] == bs) vb = (proven_i32)q;
    }
    if (va < 0 || (!b_const && vb < 0)) return false;

    proven_size_t nc0 = sys->nc;          // ★ 목표를 갈아 끼우려면 되돌릴 자리를 안다
    proven_i64 g[LOW_SMT_MAXV];

    if (w == IRW_SUB) {
        for (proven_size_t i = 0; i < map->nv; i++) g[i] = 0;
        g[va] += 1;
        proven_i64 c = 1;
        if (b_const) { if (bval > INT64_MAX - 1) return false; c = 1 - bval; }
        else g[vb] -= 1;
        if (!low_smt_add(sys, g, c)) return false;
        return low_smt_refute(sys, lam);
    }

    // ADD
    if (t.bits < 64) {
        proven_i64 mx = ((proven_i64)1 << t.bits) - 1;
        for (proven_size_t i = 0; i < map->nv; i++) g[i] = 0;
        g[va] -= 1;
        proven_i64 c = mx + 1;
        if (b_const) c -= bval; else g[vb] -= 1;
        if (!low_smt_add(sys, g, c)) return false;
        return low_smt_refute(sys, lam);
    }

    // 폭 64 — 증인 변수를 차례로 시도한다.
    for (proven_size_t v = 0; v < map->nv; v++) {
        proven_i32 key = map->key[v];
        if (key < IVS_LENBIT) {                       // 지역이면 음수가 아니어야 한다
            if ((proven_size_t)key >= IR_MAXLOCALS || st->loc[key].lo < 0) continue;
        }
        sys->nc = nc0;                                // 앞 시도의 목표를 걷어낸다
        for (proven_size_t i = 0; i < map->nv; i++) g[i] = 0;
        g[v]  += 1;
        g[va] -= 1;
        proven_i64 c = 1;
        if (b_const) { if (bval > INT64_MAX - 1) continue; c = 1 - bval; }
        else g[vb] -= 1;
        if (!low_smt_add(sys, g, c)) { sys->nc = nc0; continue; }
        if (low_smt_refute(sys, lam)) return true;
    }
    sys->nc = nc0;
    return false;
}

static bool iv_smt_index_ok(const ivstate_t *st, proven_i32 islot, proven_i32 sslot,
                            low_smt_sys_t *sys, proven_i64 *lam, ivs_map_t *map) {
    if (islot < 0 || (proven_size_t)islot >= IR_MAXLOCALS) return false;
    if (sslot < 0 || (proven_size_t)sslot >= IR_MAXLOCALS) return false;
    proven_i32 seed[1] = { islot };
    if (!iv_smt_load(st, seed, 1, sslot, sys, map)) return false;
    proven_i32 vi = -1, vL = -1;
    for (proven_size_t q = 0; q < map->nv; q++) {
        if (map->key[q] == islot)              vi = (proven_i32)q;
        if (map->key[q] == sslot + IVS_LENBIT) vL = (proven_i32)q;
    }
    if (vi < 0 || vL < 0) return false;
    // ── 목표의 **부정**을 담는다:  ¬(i ≤ len−1)  ⇔  len − i ≤ 0 ────────────────
    if (!ivs_c2(sys, vL, 1, vi, -1, 0)) return false;
    return low_smt_refute(sys, lam);
}

static void iv_narrow(ivstate_t *st, proven_u8 op, proven_i32 slot, iv_t rhs, proven_i32 lenrhs,
                      proven_i32 rslot, bool taken, proven_i32 aff_s, proven_i64 aff_c,
                      proven_i32 raff_s, proven_i64 raff_c,
                      proven_i64 ivstep, proven_i32 ivfrom) {
    proven_u8 e0_ = op;
    if (!taken) {
        e0_ = op == IRW_LT ? IRW_GE : op == IRW_GE ? IRW_LT
            : op == IRW_LE ? IRW_GT : op == IRW_GT ? IRW_LE
            : op == IRW_EQ ? IRW_NE : op == IRW_NE ? IRW_EQ : 0;
    }
    // ★★★★★ **아핀 좌변의 길이 사실** (2026-08-16, P2). `i + c (<|≤) …` 에서 **c 칸의 여백**이
    //   나온다 — 벡터 로드가 정확히 그것을 묻는다. 좌변이 지역 자신(c = 0)인 경우는 아래 옛
    //   경로가 이미 다루므로 **c > 0 일 때만** 온다.
    //   ★ 건전성: `c < 0` 은 여기 못 온다(`iv_put_lenlt` 가 음의 여백을 버린다) — `i − 3 ≤ n`
    //     은 `i ≤ n` 을 뜻하지 않으므로 낮춰 싣는 것이 곧 거짓이다.
    if (op && aff_s >= 0 && aff_c > 0 && (proven_size_t)aff_s < IR_MAXLOCALS &&
        (e0_ == IRW_LT || e0_ == IRW_LE)) {
        //  ① 우변이 `len g` 자신:  i + c (<|≤) len(g)
        if (lenrhs >= 0) {
            iv_put_lenlt(st, (proven_size_t)aff_s, lenrhs, e0_ == IRW_LT, aff_c);
            // ★ §8-23 — i 가 `len u` 자신이면 `len u + c (<|≤) len g` (c ≥ 1) ⟹ `len u < len g`.
            iv_put_slenle(st, lenrhs, st->lenofloc[aff_s], true);
        }
        //  ② 우변이 지역 n 이고 n 에 길이 사실이 있다 — **추이 한 걸음**:
        //       i + c (<|≤) n   ∧   n + m (<|≤) len(g)   ⟹   i + (c+m) (<|≤) len(g)
        //     엄격은 **하나라도 있으면** 결론이 엄격이다(둘 다면 한 칸 더 남지만 안 챙긴다 —
        //     보수적인 쪽이라 건전하고, 챙기려면 규칙이 두 벌이 된다).
        if (rslot >= 0 && (proven_size_t)rslot < IR_MAXLOCALS && st->lenlt[rslot] >= 0)
            iv_put_lenlt(st, (proven_size_t)aff_s, st->lenlt[rslot],
                         (e0_ == IRW_LT) || st->lenstr[rslot], aff_c + st->lenoff[rslot]);
        //  ③ 우변이 지역 n 자신 — **차분 여백**:  i + c (<|≤) n.
        //    ★ 슬라이스 파라미터에는 이쪽이 **유일하게 살아남는 길**이다: 진입 프롤로그의
        //      자기-재대입이 길이 사실을 죽이므로 `len a ≥ n` 은 `lenge_p` 로만 남고, 그것과
        //      맞물리는 것이 바로 이 차분이다(`i + 4 ≤ n ≤ len a`).
        if (rslot >= 0 && rslot != aff_s)
            iv_put_lerel(st, (proven_size_t)aff_s, rslot, e0_ == IRW_LT, aff_c);
    }

    // ★★★★★ **루프 종료값** (2026-08-17, PERF-0004). 가드 `lt v X` 가 **거짓인 가지**가
    //   곧 루프를 벗어난 자리이고, `v` 가 걸음 c 짜리 귀납 변수(진입값 0)이면 거기서
    //   **v ≤ X + c − 1** 이다. 위 머리(`iv_find_indvars`)에 증명을 적어 두었다.
    //   ☞ `op == IRW_LT` 일 때만이다: `le v X` 의 거짓 가지는 `v > X` 라 상계가 안 나온다.
    //   ★ 그리고 **길이 사실을 함께 옮긴다** — X 가 `len(s)` 에 대해 여백 m 을 가지면
    //     벗어난 v 는 여백 `m − (c−1)` 을 갖는다. `qsort` 의 `set hi i` 가 그때 이어진다.
    if (op == IRW_LT && !taken && ivstep >= 1 &&
        slot >= 0 && (proven_size_t)slot < IR_MAXLOCALS) {
        proven_i64 bump = ivstep - 1;
        // ★★ 진입값이 **지역**이면(`var j be u64 lo .`) `lo ≤ X + c − 1` 을 **여기서** 확인한다.
        //   X = base + off 라 하면 `lo + mw ≤ base` 에서 `lo ≤ base − mw` 이므로
        //   요구는 `−mw ≤ off + c − 1`, 즉 **mw ≥ 1 − off − c** 다.
        //   `qsort` 는 `X = hi − 1`(off = −1) · c = 1 이라 `mw ≥ 1` 을 요구하고,
        //   머리의 `if ge lo0 hi0 . do return . end` 가 세운 `lo < hi` 가 정확히 그것이다.
        //   ★ 못 세우면 **아무 말도 안 한다** — 종료 상계를 틀리면 곧 범위 밖 접근이다.
        if (ivfrom >= 0) {
            proven_i32 base = (rslot >= 0) ? rslot : raff_s;
            proven_i64 off  = (rslot >= 0) ? 0 : raff_c;
            if (base < 0 || (proven_size_t)base >= IR_MAXLOCALS ||
                (proven_size_t)ivfrom >= IR_MAXLOCALS ||
                !iv_leknown(st, (proven_size_t)ivfrom, base) ||   // ★ 0 은 없음이 아니다
                iv_lemargin(st, (proven_size_t)ivfrom, base) < 1 - off - ivstep)
                return;   // 진입값을 못 묶는다 ⇒ 종료값도 못 말한다
        }
        // 구간 상계
        if (rhs.hi < INT64_MAX - bump) {
            proven_i64 cap = rhs.hi + bump;
            if (cap < st->loc[slot].hi) { st->loc[slot].hi = cap; st->loc[slot].wide = rhs.wide; }
        }
        // 길이 사실 — 우변이 지역이면 그 지역의 여백에서, 아핀이면 그 오프셋만큼 더 옮겨서.
        if (rslot >= 0 && (proven_size_t)rslot < IR_MAXLOCALS && st->lenlt[rslot] >= 0) {
            proven_i64 m = iv_margin(st, (proven_size_t)rslot, st->lenlt[rslot]);
            iv_put_lenlt(st, (proven_size_t)slot, st->lenlt[rslot], false, m - bump);
        } else if (raff_s >= 0 && (proven_size_t)raff_s < IR_MAXLOCALS &&
                   st->lenlt[raff_s] >= 0) {
            proven_i64 m = iv_margin(st, (proven_size_t)raff_s, st->lenlt[raff_s]);
            iv_put_lenlt(st, (proven_size_t)slot, st->lenlt[raff_s], false, m - raff_c - bump);
        }
    }
    // ★ 루프 **안**의 짝 — `i < (r + c)` 이고 `r + m ≤ len(g)` 이면 `i + (m − c) + [엄격이면 1]
    //   ≤ len(g)`. 종료값 규칙이 그 사실을 루프 밖으로 나르므로 둘은 **한 쌍**이다.
    if (op && slot >= 0 && (proven_size_t)slot < IR_MAXLOCALS &&
        raff_s >= 0 && (proven_size_t)raff_s < IR_MAXLOCALS &&
        (e0_ == IRW_LT || e0_ == IRW_LE) && st->lenlt[raff_s] >= 0) {
        proven_i64 m = iv_margin(st, (proven_size_t)raff_s, st->lenlt[raff_s]);
        iv_put_lenlt(st, (proven_size_t)slot, st->lenlt[raff_s], false,
                     m - raff_c + ((e0_ == IRW_LT) ? 1 : 0));
    } else if (op && slot >= 0 && (proven_size_t)slot < IR_MAXLOCALS &&
               raff_s >= 0 && (proven_size_t)raff_s < IR_MAXLOCALS &&
               (e0_ == IRW_LT || e0_ == IRW_LE) && st->lenofloc[raff_s] >= 0) {
        // ★ 우변의 지역이 **길이 그 자체**일 때(`let m be len b` 뒤의 `for j count (add m 1)`) — 여백 0 인 «≤» 와 같다
        //   (2026-10-04). 위 줄은 `lenlt` 만 보아 `j ≤ len b` 를 못 세웠고, 편집 거리 첫 줄 채우기의 검사가 남았다.
        iv_put_lenlt(st, (proven_size_t)slot, st->lenofloc[raff_s], false,
                     0 - raff_c + ((e0_ == IRW_LT) ? 1 : 0));
    } else if (op && slot >= 0 && (proven_size_t)slot < IR_MAXLOCALS &&
               rslot >= 0 && (proven_size_t)rslot < IR_MAXLOCALS && st->affloc_s[rslot] >= 0 &&
               (proven_size_t)st->affloc_s[rslot] < IR_MAXLOCALS && (e0_ == IRW_LT || e0_ == IRW_LE)) {
        // ★ 우변이 **덧셈 출처를 든 지역**일 때(`end = m + c` 를 담은 숨은 끝값) — 위 두 줄과 같은 셈을 그 출처로 한다(2026-10-04).
        proven_i32 b_ = st->affloc_s[rslot]; proven_i64 c_ = st->affloc_c[rslot];
        if (st->lenlt[b_] >= 0)
            iv_put_lenlt(st, (proven_size_t)slot, st->lenlt[b_], false,
                         iv_margin(st, (proven_size_t)b_, st->lenlt[b_]) - c_ + ((e0_ == IRW_LT) ? 1 : 0));
        else if (st->lenofloc[b_] >= 0)
            iv_put_lenlt(st, (proven_size_t)slot, st->lenofloc[b_], false, 0 - c_ + ((e0_ == IRW_LT) ? 1 : 0));
    }
    if (slot < 0 || (proven_size_t)slot >= IR_MAXLOCALS || !op) return;
    iv_t *v = &st->loc[slot];
    proven_u8 e = e0_;
    // ★ RFC-0055 D5: `lt i (len g)` 가 참인 가지에서 i 는 len(g) 보다 작다.
    //   거짓인 가지에서는 그 사실이 **반박된다** — 지운다(보수적이 아니라 정확하다).
    if (lenrhs >= 0) {
        // ★ 2026-08-13 (D3): `le i (len g)` 도 사실이다 — 다만 **약한** 사실(`≤`)이라
        //   그것만으로는 색인이 안전하지 않다. 엄격 비트로 구별해 담는다.
        if (e == IRW_LT)      iv_put_lenlt(st, (proven_size_t)slot, lenrhs, true,  0);
        else if (e == IRW_LE) iv_put_lenlt(st, (proven_size_t)slot, lenrhs, false, 0);
        // ★ §8-23 — 값이 `len u` 자신이면 같은 사실을 슬라이스끼리의 칸에도(덮어쓰기가 없는 칸).
        if (e == IRW_LT || e == IRW_LE) iv_put_slenle(st, lenrhs, st->lenofloc[slot], e == IRW_LT);
        // 거짓 가지에서는 반박된다 — 지운다. `ge` 는 `lt` 의 부정, `gt` 는 `le` 의 부정이다.
        else if (e == IRW_GE || e == IRW_GT) {
            if (st->lenlt[slot] == lenrhs) iv_put_lenlt(st, (proven_size_t)slot, -1, false, 0);
        }
    }
    // ★★ R5 — 우변이 **지역**이면 여기서 **차분 제약**이 생긴다. 구간은 이 사실을 담을 수 없다.
    //   `le a b` 가 참인 가지에서 a ≤ b 다. 거짓인 가지에서는 b < a 다 — **양쪽 다 사실이다.**
    if (rslot >= 0 && (proven_size_t)rslot < IR_MAXLOCALS && rslot != slot) {
        switch (e) {
            case IRW_LT: iv_put_lerel(st, (proven_size_t)slot, rslot, true,  0); break;
            case IRW_LE: iv_put_lerel(st, (proven_size_t)slot, rslot, false, 0); break;
            case IRW_GT: iv_put_lerel(st, (proven_size_t)rslot, slot, true,  0); break;
            case IRW_GE: iv_put_lerel(st, (proven_size_t)rslot, slot, false, 0); break;
            case IRW_EQ: iv_put_lerel(st, (proven_size_t)slot, rslot, false, 0);
                         iv_put_lerel(st, (proven_size_t)rslot, slot, false, 0); break;
            default: break;
        }
        // ★★★★★ **추이 한 걸음: `i < n` ∧ `n < len s` ⇒ `i < len s`** (2026-08-11).
        //   전에 한 번 넣었다가 **도달조차 안 해 걷어냈다** — 원인은 이 자리가 아니라
        //   `IRW_NOT` 이 술어를 나를 때 `rslot` 한 줄을 빠뜨린 것이었다(위 그 자리 주석).
        //   배관을 이은 뒤에야 이 걸음이 닿는다. ☞ **안 무는 코드를 남기지 않은 판단이 옳았다**:
        //   그때 남겼다면 "추이는 된다" 고 믿는 사람이 생겼을 것이고, 진짜 원인은 계속 숨었다.
        //   ★ 건전성: `i < n`(엄격) ∧ `n < len s`(엄격) ⇒ `i < len s`. `le` 는 `i ≤ n < len s`
        //     이므로 역시 성립한다. 반대 방향(`gt`/`ge`)은 rslot 쪽이 작은 값이라 거울로 적용한다.
        //   ★★ 2026-08-13 (D3) — **엄격은 하나만 있으면 된다**: 둘 중 **하나라도 엄격이면**
        //     결론이 엄격이고, **둘 다 비엄격이면 결론도 비엄격**이다(`i ≤ n ≤ len s` 로는
        //     `i = len s` 를 못 막는다 — 여기서 엄격이라고 적으면 그것이 곧 메모리 안전
        //     구멍이다). 예전 주석이 *"섞어도 늘 엄격"* 이라 적었던 것은 그때 `lenlt` 가
        //     **엄격밖에 담을 수 없었기 때문**이고, 비엄격이 들어온 지금은 참이 아니다.
        switch (e) {
            case IRW_LT: case IRW_LE:
                if (st->lenlt[rslot] >= 0 && st->lenlt[slot] < 0) {
                    // ★ 여백도 함께 온다: i ≤ n 이고 n + m ≤ len(s) 이면 i + m ≤ len(s).
                    st->lenoff[slot] = st->lenoff[rslot];
                    st->lenlt[slot]  = st->lenlt[rslot];
                    st->lenstr[slot] = (proven_u8)((e == IRW_LT) || st->lenstr[rslot]);
                }
                break;
            case IRW_GT: case IRW_GE:
                if (st->lenlt[slot] >= 0 && st->lenlt[rslot] < 0) {
                    st->lenoff[rslot] = st->lenoff[slot];
                    st->lenlt[rslot]  = st->lenlt[slot];
                    st->lenstr[rslot] = (proven_u8)((e == IRW_GT) || st->lenstr[slot]);
                }
                break;
            default: break;
        }
    }
    // x < rhs  ⇒  x ≤ hi(rhs) - 1     (우변이 지역이어도 상계만 있으면 좁혀진다)
    // x > rhs  ⇒  x ≥ lo(rhs) + 1
    switch (e) {
        // ★ **진짜 사실로 좁히면 자름 표시가 꺼진다** — 새 hi 의 출처가 우변이기 때문이다.
        //   우변 자신이 자른 것이면(`rhs.wide`) 그 성질을 물려받는다.
        case IRW_LT: if (rhs.hi > INT64_MIN && rhs.hi - 1 < v->hi) { v->hi = rhs.hi - 1; v->wide = rhs.wide; } break;
        case IRW_LE: if (rhs.hi < v->hi) { v->hi = rhs.hi; v->wide = rhs.wide; } break;
        case IRW_GT: if (rhs.lo < INT64_MAX && rhs.lo + 1 > v->lo) v->lo = rhs.lo + 1; break;
        case IRW_GE: if (rhs.lo > v->lo) v->lo = rhs.lo; break;
        case IRW_EQ: *v = iv_meet(*v, rhs); break;
        default: break;   // NE 는 구간으로 표현 불가
    }
    if (v->lo > v->hi) *v = (iv_t){ v->lo, v->lo };
}

// ══════════════════════════════════════════════════════════════════════════
// ★★★ **증명 운반 검사** (certificate + 작은 독립 검증기) — RFC-0086.
//
//   지금까지 "이 검사는 지워도 된다" 를 받아들이는 근거는 **이 분석기 수천 줄을 믿는 것**
//   뿐이었다. VM 의 자기 고발(E-VM-ANALYSIS)이 있지만 그것은 **그 입력이 실제로 그 자리를
//   때렸을 때만** 말한다 — 안 때리면 침묵한다(그리고 실제로 침묵했다: 행우선 규칙이
//   **검사되지 않는 계약**을 믿었을 때, 오라클이 그 자리를 때린 뒤에야 드러났다).
//
//   ⇒ 지울 때마다 **왜 지웠는지**를 함께 남긴다: 규칙 id + 그 규칙이 쓴 **수치 사실**.
//     그러면 `scripts/check-certificates.py`(수백 줄, 이 코드와 **독립**)가 규칙마다
//     **산술을 다시 해 본다.** 신뢰 대상이 *분석기 전체* 에서 *검증기* 로 줄어든다.
//
//   ★ 정직한 범위: 검증기는 **사실을 믿고 추론을 검사한다.** 즉 "i < len s 라는 사실이
//     어디서 왔나" 는 재도출하지 않는다(그건 분석 전체를 다시 하는 것이다). 잡는 것은
//     **규칙 오적용**(off-by-one · 폭/부호 착각 · 전제 빠짐)이고, 그것이 실제로 이 저장소에서
//     났던 결함의 종류다. 무엇을 못 잡는지 그 스크립트 머리에 적어 둔다.
// ══════════════════════════════════════════════════════════════════════════
/* IR_CERT_MAX — low_ir_priv.h */
/* ir_cert_t — low_ir_priv.h (계약→시험 절이 쓴다) */

 ir_cert_t g_cert[IR_CERT_MAX];
/* IR_CERT_EXT_MAX — low_ir_priv.h */
 proven_i64 g_cert_ext[IR_CERT_EXT_MAX];
static proven_size_t g_cert_ext_n = 0;
 proven_size_t g_ncert = 0;
 proven_size_t g_cert_drop = 0;      // ★ 넘친 것은 **센다**(조용히 자르지 않는다)

 void cert_reset(void) { g_ncert = 0; g_cert_drop = 0; g_cert_ext_n = 0; }

// ★ SMT 증명서: 계(nv·nc·행들) + 계수. 실을 자리가 없으면 **증명서를 안 낸다**(그리고 그것은
//   `# proven` 과 증명서 수의 차이로 드러난다 — 조용히 넘어가는 길이 없다).
static bool cert_put_smt(const low_ir_def_t *d, proven_size_t pc, low_irw_t w,
                         const low_smt_sys_t *s, const proven_i64 *lam) {
    proven_size_t need = 2 + s->nc * (s->nv + 1) + s->nc;
    if (g_ncert >= IR_CERT_MAX || g_cert_ext_n + need > IR_CERT_EXT_MAX) { g_cert_drop++; return false; }
    ir_cert_t *c = &g_cert[g_ncert++];
    c->def = d->name; c->pc = (proven_u32)pc; c->w = w; c->rule = "R-SMT-FARKAS";
    c->nf = 0;
    c->ext_at = (proven_u16)g_cert_ext_n;
    proven_size_t at = g_cert_ext_n;
    g_cert_ext[at++] = (proven_i64)s->nv;
    g_cert_ext[at++] = (proven_i64)s->nc;
    for (proven_size_t j = 0; j < s->nc; j++) {
        for (proven_size_t i = 0; i < s->nv; i++) g_cert_ext[at++] = s->row[j].a[i];
        g_cert_ext[at++] = s->row[j].c;
    }
    for (proven_size_t j = 0; j < s->nc; j++) g_cert_ext[at++] = lam[j];
    c->ext_n = (proven_u16)(at - g_cert_ext_n);
    g_cert_ext_n = at;
    return true;
}

static void cert_put(const low_ir_def_t *d, proven_size_t pc, low_irw_t w,
                     const char *rule, int nf, ...) {
    if (g_ncert >= IR_CERT_MAX) { g_cert_drop++; return; }
    ir_cert_t *c = &g_cert[g_ncert++];
    c->def = d->name; c->pc = (proven_u32)pc; c->w = w; c->rule = rule;
    c->ext_at = 0; c->ext_n = 0;
    c->nf = (proven_u8)(nf > 6 ? 6 : nf);
    va_list ap; va_start(ap, nf);
    for (int k = 0; k < c->nf; k++) c->f[k] = va_arg(ap, proven_i64);
    va_end(ap);
}

// 블록 하나를 통과시킨다. mark=true 면 증명된 자리에 IR_POL_PROVEN 을 세운다.
// 반환: 블록의 종결 명령 인덱스. 종결이 BRZ 면 술어 정보를 *pop 으로 돌려준다.
// ★ `iv_block` 은 이 표를 인자로 안 받는다(서명이 이미 여덟 개다) — def 마다 세워 두는
//   **한 개짜리 창**으로 넘긴다. 분석은 def 하나를 한 번에 도므로 재진입이 없다.
//   타입이 아니라 **배열 하나**만 넘긴다(귀납 변수 표는 여기보다 훨씬 아래에 선언된다).
static const proven_i64 *g_cmax = NULL;
// ★ 루프 표(머리·뒤 간선·바퀴 수) — 세는 변수 상계와 원소 상계가 **같은 표**를 읽는다.
typedef struct { proven_size_t h, b; proven_i64 trips; bool ok; } iv_loop_t;
static iv_loop_t g_lp[32];
static proven_size_t g_nlp;
// ★★★★★ **원소도 바퀴 수로 묶는다** (2026-09-11, RFC-0111 §8-18 · WO-0197).
//   세는 변수에는 이미 `cmax` 가 있다: *"이 루프는 최대 T 바퀴 도니 그 값은 걸음×T 이하다."*
//   그런데 **슬라이스에 쓰는 값**에는 그 논증이 없어서, 표에 되쓰는 코드(편집 거리)는
//   위드닝이 원소 구간을 ⊤ 로 보냈고 «값 + 1» 의 넘침 검사가 영원히 남았다.
//
//   ★ 여기서 쓰는 논증은 **추측하고 검증하는 것**이다(자기가 심은 사실로 자기를 정당화하지
//     않는다): 후보 상계 C 를 고르고, 고정점을 **C 로 자른 채** 다시 돌린 다음,
//     그 상태에서 **모든 쓰기가 C 를 안 넘는지** 확인한다. 넘으면 후보를 버린다.
//     넘지 않으면 그것은 **귀납 불변식**이다(진입에서 참 · 한 걸음이 보존).
//   ☞ *추측은 공짜여도 되지만, 검증은 공짜가 아니어야 한다.*
static proven_i64 g_emax[IR_MAXLOCALS];   // 0 = 없음
static bool       g_ecap_on;              // 이번 패스에서 상계를 물리는가
static bool       g_ecap_bad;             // 검증 실패 — 쓰기가 상계를 넘었다
static proven_i64 g_ewr_at[IR_MAXLOCALS]; // 이 슬라이스에 쓰는 첫 자리(0 = 안 씀)
static bool       g_ewr_out[IR_MAXLOCALS];// 루프 **밖**에서도 쓰는가(그러면 못 묶는다)

static proven_size_t iv_block(ir_ctx_t *c, low_ir_def_t *d, proven_size_t b0, proven_size_t bend,
                              ivstate_t *st, ivs_t *pred_out, bool mark,
                              proven_size_t *proven, proven_size_t *total) {
    ivs_t stk[64]; proven_size_t sp = 0;
    pred_out->pred = 0; pred_out->slot = -1; pred_out->lenpred = -1; pred_out->lenlt = -1;
    pred_out->rslot = -1;
    pred_out->eid = 0; pred_out->deps = 0; pred_out->pneg = false;
    pred_out->ifrom = 0; pred_out->ito = 0;
    pred_out->divof_s = -1; pred_out->dmul_s = -1;
    proven_size_t i = b0;
    for (; i < bend; i++) {
        low_ir_ins_t *in = &d->code[i];
        if (in->w == IRW_BR || in->w == IRW_BRZ || in->w == IRW_RET) break;
        int ar = ir_word_arity(c->out, in->w, in->a);
        if (in->w == IRW_STORE) {
            if (sp) {
                proven_size_t sl = (proven_size_t)in->a;
                if (sl < IR_MAXLOCALS) {
                    // ★★★ **죽이고 나서 싣는다** (2026-08-13). 예전엔 이 자리가 무효화를
                    //   **손으로 펼쳐** 갖고 있었고, 그래서 같은 무효화가 필요한 *다른 창*
                    //   (`mut_ref` 인자)이 열렸을 때 **아무것도 안 죽었다.** 이제 창마다
                    //   `iv_kill_local` 을 부르면 되고, 필드가 늘어도 한 곳만 는다.
                    proven_i32 src_slot = stk[sp - 1].slot;
                    iv_t   src_elem = (src_slot >= 0 && (proven_size_t)src_slot < IR_MAXLOCALS)
                                      ? st->elemv[src_slot] : IV_TOP;
                    // ★★★★★ **진입 정규화가 방금 받은 원소 사실을 지웠다** (2026-09-09, WO-0191).
                    //   `slice u64` 파라미터의 프롤로그는 `load p · view.array · store p` 다 —
                    //   같은 메모리를 **폭만 붙여** 자기 자신에게 다시 묶는 것이다. 그런데 그
                    //   중간값은 슬롯이 없어서(`slot = -1`) 위 줄이 `IV_TOP` 을 싣고, 그래서
                    //   `requires elem_le s 200` 이 **자기 op 의 첫 세 낱말에서 죽었다**.
                    //   ⇒ u8 슬라이스는 정규화가 없어 같은 프로그램이 **원소 폭만으로 갈렸다**
                    //     (u8 은 증명되고 u64 는 안 되던 것).
                    //   ☞ 건전성: 되살리는 것은 **자기 자신에게 다시 묶는 프롤로그**뿐이고,
                    //     그 값은 같은 메모리다 — 몸통의 재대입은 그대로 죽인다.
                    //   ☞ *정규화는 뜻을 안 바꾸어야 한다. 사실을 지우는 정규화는 뜻을 바꾼다.*
                    bool pro_rebind = (i < ir_prologue_end(d)) && src_slot < 0;
                    iv_t keep_elem = st->elemv[sl];
                    // ★ 길이 구간도 같다 (2026-10-04) — 계약 `lt (len b) (len prev)` 가 준 «len prev ≥ 1» 이 `slice u64` 의
                    //   프롤로그에서 지워져, `set (idx prev 0) …` 의 검사가 u64 에서만 남았다(같은 병의 셋째 자리).
                    iv_t keep_lenv = st->lenv[sl];
                    // ★★★★★ **증가는 하한 관계를 죽이지 않는다** (2026-08-22, RFC-0053 §8).
                    //   `x + m ≤ v` 가 참인데 `v = v + c`(c ≥ 0)를 하면 그 부등식은 **여전히
                    //   참이다** — v 는 커지기만 했다. 그런데 무효화가 그것을 통째로 지웠다.
                    //   ☞ 실측이 자리를 짚었다: `qsort` 의 `sub i lo`(= `i ≥ lo` 를 요구)와
                    //     탐침 `iv2`(조건부 증가)에서 같은 모양이 재현된다. 그리고 그 사실을
                    //     **심는 코드는 이미 있었다**(*복사는 같음이다*, 아래) — 지우는 쪽만
                    //     한 걸음 크게 지우고 있었다. **새 사실이 아니라 안 지우는 것이다.**
                    //   ★ 건전성: 되살리는 것은 **이 슬롯을 가리키는 `lerel` 뿐**이다.
                    //     · `lenge_p/q`(len s ≥ p·q)는 p 가 커지면 **깨진다** ⇒ 안 살린다.
                    //     · 술어 기억(`fact`)도 값이 바뀌면 거짓일 수 있다 ⇒ 안 살린다.
                    //     · 이 슬롯 **자신의** 상계 관계(`lerel[sl]`)도 커졌으니 죽는다.
                    //     · 여백·엄격은 그대로 나른다(`x + m ≤ v_old ≤ v_new`).
                    bool inc_self = stk[sp - 1].aff_s == (proven_i32)sl && stk[sp - 1].aff_c >= 0;
                    proven_i32 keep_p[IR_MAXLOCALS]; proven_u8 keep_s[IR_MAXLOCALS];
                    proven_i64 keep_o[IR_MAXLOCALS];
                    if (inc_self)
                        for (proven_size_t k2 = 0; k2 < IR_MAXLOCALS; k2++) {
                            keep_p[k2] = (st->lerel[k2] == (proven_i32)sl) ? st->lerel[k2] : -1;
                            keep_s[k2] = st->lestr[k2]; keep_o[k2] = st->leoff[k2];
                        }
                    // ★ 진입 프롤로그의 자기-재대입만 계약 사실을 지키고, 몸통의 재대입은 지운다.
                    iv_kill_local(st, sl, i < ir_prologue_end(d));
                    if (inc_self)
                        for (proven_size_t k2 = 0; k2 < IR_MAXLOCALS; k2++)
                            if (keep_p[k2] >= 0 && k2 != sl) {
                                st->lerel[k2] = keep_p[k2];
                                st->lestr[k2] = keep_s[k2]; st->leoff[k2] = keep_o[k2];
                            }
                    st->loc[sl] = stk[sp - 1].v;
                    st->lenlt[sl] = stk[sp - 1].lenlt;   // 사실은 값을 따라간다 (`var j be i`)
                    st->lenstr[sl] = stk[sp - 1].lenstr; // ★ 엄격/비엄격도 함께 따라간다
                    st->lenoff[sl] = stk[sp - 1].lenoff; // ★ 여백도 — 셋은 한 사실의 세 칸이다
                    st->vlanesloc[sl] = stk[sp - 1].vlanes;   // ★ 레인 수도 값을 따라간다
                    st->divofloc_s[sl] = stk[sp - 1].divof_s;  // ★ 나눗셈 출처도
                    st->divofloc_c[sl] = stk[sp - 1].divof_c;
                    // ★ 자기 자신을 가리키는 출처는 싣지 않는다 — `set s (div s 6)` 뒤의 s 는 «s / 6» 이 아니다.
                    if (st->divofloc_s[sl] == (proven_i32)sl) st->divofloc_s[sl] = -1;
                    // ★★ 곱의 출처도 값을 따라간다 (2026-10-04) — `let row be mul i n` 뒤의 `add row k` 가 행우선 규칙에 닿는다.
                    //   전에는 곱을 이름에 담는 순간 출처가 사라져 matmul 을 식을 펴서 적어야 했다.
                    st->prodloc_l[sl] = stk[sp - 1].prod_l; st->prodloc_r[sl] = stk[sp - 1].prod_r;
                    if (st->prodloc_l[sl] == (proven_i32)sl || st->prodloc_r[sl] == (proven_i32)sl ||
                        st->prodloc_l[sl] < 0 || st->prodloc_r[sl] < 0) { st->prodloc_l[sl] = -1; st->prodloc_r[sl] = -1; }
                    // ★ 덧셈 출처도 (2026-10-04) — `for j count (add m 1)` 의 숨은 끝값이 «m + 1» 임을 좁히기가 알아야 한다.
                    st->affloc_s[sl] = stk[sp - 1].aff_s; st->affloc_c[sl] = stk[sp - 1].aff_c;
                    if (st->affloc_s[sl] == (proven_i32)sl || st->affloc_c[sl] == 0) st->affloc_s[sl] = -1;
                    st->lenofloc[sl] = stk[sp - 1].lenof;   // ★ `let n be len s` — 그 사실도 값을 따라간다
                    // ★ 슬라이스를 재바인딩하면 원소 구간도 **출처를 따라간다**(`let t be s`).
                    st->elemv[sl] = pro_rebind ? keep_elem : src_elem;
                    if (pro_rebind) st->lenv[sl] = keep_lenv;
                    // ★★★ **사실은 값을 따라간다** (2026-07-30) — `var lru_idx be j` 에서 `j < cap` 이면
                    //   `lru_idx < cap` 이다. 이 한 줄이 없어서 인덱스를 **다른 이름에 담는 순간**
                    //   경계 증명이 죽었다(lru 벤치의 남은 검사 다섯 중 넷이 그것이었다).
                    //   `lenlt` 는 이미 값을 따라가고 있었다 — 차분 제약만 빠져 있었다.
                    //   ☞ 건전성: 출처나 대상이 바뀌면 위 kill 루프가 양방향으로 죽인다.
                    {
                        proven_i32 src = stk[sp - 1].slot;
                        if (src >= 0 && (proven_size_t)src < IR_MAXLOCALS && (proven_size_t)src != sl) {
                            st->lerel[sl] = st->lerel[src]; st->lestr[sl] = st->lestr[src];
                            st->leoff[sl] = st->leoff[src];
                            // ★★ **복사는 같음이다** — `var hi be u64 hi0 .` 뒤로 두 슬롯의 값이
                            //   같다. 원본 쪽에 아직 관계가 없을 때만 `src ≤ sl` 을 심는다
                            //   (있는 사실을 덮으면 그것이 더 큰 손해다). 위 한 홉이 이것을 쓴다.
                            if (src < IR_MAXLOCALS && st->lerel[src] < 0)
                                iv_put_lerel(st, src, (proven_i32)sl, false, 0);
                            // ★★ **같음은 두 방향이다** (2026-10-03). 위는 `src ≤ sl` 만 심었다. 그런데 `for i count τ n .` 은 끝을
                            //   숨은 지역에 얼려 두고(`endv ← n`) 그것과 견준다 — `i < endv` 에서 `i < n` 으로 가려면 `endv ≤ n` 이
                            //   있어야 한다. 그것이 없어서 새 `for` 머리를 쓰면 행 우선 첨자 증명이 통째로 죽었다(matmul 15/18 → 3/15).
                            //   ⇒ 대상에 아직 상계 관계가 없으면 `sl ≤ src` 도 심는다. 둘 중 하나가 바뀌면 kill 이 양쪽을 지운다.
                            if (st->lerel[sl] < 0)
                                iv_put_lerel(st, (proven_i32)sl, src, false, 0);
                        } else if (stk[sp - 1].aff_s >= 0 && stk[sp - 1].aff_c >= 0 &&
                                   (proven_size_t)stk[sp - 1].aff_s < IR_MAXLOCALS &&
                                   (proven_size_t)stk[sp - 1].aff_s != sl) {
                            // ★★★ **차분 제약도 아핀을 따라간다** (2026-08-17, PERF-0004).
                            //   길이 사실은 이미 따라갔는데(`lenlt`, 2026-08-17 오전) **차분만
                            //   빠져 있었다** — 한 사실의 두 계열인데 한쪽만 나른 것이다.
                            //   `set lo (add i 1)` 에서 `i + m ≤ p` 면 `lo + (m − 1) ≤ p` 다.
                            //   ☞ 이 한 줄이 `qsort` 의 바깥 루프를 닫는다: 앞줄
                            //     `sub hi (add i 1)` 이 `i + 1 ≤ hi`(m = 1)를 증언했으므로
                            //     c = 1 에서 `lo ≤ hi` 가 서고, 그것이 22:`sub hi lo` 다.
                            proven_size_t a = (proven_size_t)stk[sp - 1].aff_s;
                            proven_i32 pp = st->lerel[a];
                            if (pp >= 0) {
                                proven_i64 m = iv_lemargin(st, a, pp) - stk[sp - 1].aff_c;
                                if (m >= 0) iv_put_lerel(st, sl, pp, false, m);
                            }
                        }
                    }
                }
                sp--;
            }
            continue;
        }
        if (in->w == IRW_SPOP_INTO) { sp = 0; continue; }
        if (ar < 0) { sp = 0; continue; }
        // ★★★ **지정 초기화자로 바꿨다** (2026-08-13). 전에는 위치 초기화자였고 주석이
        //   *"필드를 더하면 여기도 늘려야 한다 — 안 늘리면 뒤 필드로 밀린다"* 고 **경고하고
        //   있었다.** 경고가 필요한 코드는 언젠가 그 경고를 못 본 사람을 만난다(이 파일이
        //   같은 부류로 두 번 당했다). 이제 필드를 더해도 **밀리지 않고**, 안 적은 필드는
        //   0 이 된다 — `lenstr` 의 미지값이 0(약함)인 것도 그래서 안전하다.
        ivs_t zero_ivs = { .v = IV_TOP, .rhs = IV_TOP,
                           .slot = -1, .lenof = -1, .lenlt = -1, .lenpred = -1, .rslot = -1,
                           .aff_s = -1, .aff_c = 0, .raff_s = -1, .raff_c = 0,
                           .dif_l = -1, .dif_r = -1, .puns = false, .vlanes = 0,
                           .prod_l = -1, .prod_r = -1, .divof_s = -1, .dmul_s = -1,
                           .row_i = -1, .row_q = -1, .row_k = -1 };
        ivs_t ops[3] = { zero_ivs, zero_ivs, zero_ivs };
        if ((proven_size_t)ar > sp) { sp = 0; continue; }
        for (int k = ar - 1; k >= 0; k--) { ivs_t v = stk[--sp]; if (k < 3) ops[k] = v; }
        ivs_t res = zero_ivs;
        // ★★★ **레인 수는 값을 따라 흐른다** (2026-08-17, SIMD-0001).
        //   `vector.store` 의 IR 은 `a = 0` 이고 레인/폭을 **벡터 피연산자가 실어 온다**.
        //   그래서 저장의 여백 규칙이 L 을 알려면 `load → add → store` 사슬을 건너와야 한다.
        //   ☞ 산술 케이스 안에 두었더니 **안 닿았다**(디버그로 확인: L=0). 레인별 연산이
        //     그 case 의 조건들(폭 표시·부호)을 다 통과하지 않기 때문이다. ⇒ **일반 자리**에서
        //     한 번에 흘린다: 단항은 그대로, 이항은 **둘이 같을 때만**.
        //   ★ 건전성: 레인 수는 `splat`/`load` 에서만 생기고 거기서는 정확하다. 레인별 연산이
        //     두 벡터의 레인 수가 같기를 요구하는 것은 **종류 시뮬레이터가 이미 강제**한다
        //     (`st.vn` 일치). 다르면 여기서 0(모름)이 되고, 모르면 **증명하지 않는다**.
        if (ar == 1) res.vlanes = ops[0].vlanes;
        else if (ar >= 2 && ops[0].vlanes && ops[0].vlanes == ops[1].vlanes)
            res.vlanes = ops[0].vlanes;
        // ★ 식 지문 + **명령 범위**. 지문은 조회용이고, 확정은 구조 비교가 한다.
        {
            proven_u64 h = iv_mix(iv_mix(1469598103934665603ULL, (proven_u64)in->w),
                                  (proven_u64)(in->a & ~(proven_i64)IR_POL_PROVEN));
            proven_u64 dp = 0;
            proven_size_t from = i;
            for (int q = 0; q < ar && q < 3; q++) {
                h = iv_mix(h, ops[q].eid); dp |= ops[q].deps;
                if (ops[q].eid && ops[q].ifrom < from) from = ops[q].ifrom;
            }
            if (in->w == IRW_LOAD) {
                h = iv_mix(h, 0x10ADULL);
                if ((proven_size_t)in->a < 64) dp |= ((proven_u64)1 << in->a);
            }
            res.eid = h; res.deps = dp; res.ifrom = from; res.ito = i;
        }
        switch (in->w) {
            case IRW_CONST: res.v = iv_of(in->a); res.cst = in->a; res.slot = -2; break;   // -2 = 상수 피연산자
            case IRW_LOAD:
                res.v = ((proven_size_t)in->a < IR_MAXLOCALS) ? st->loc[in->a] : IV_TOP;
                // ★★★★★ **세는 변수의 상계를 여기서 물린다** (2026-08-19). `cmax` 는 def
                //   **전체에서** 참인 불변식이므로(위 `iv_counter_bounds` 의 증명) 값이 식에
                //   들어오는 **이 한 자리**에서 물리면 된다. 위드닝이 [0, MAX] 로 벌려 놓은
                //   것을 여기서 되좁힌다 — 위드닝은 *모르는 것*이지 *사실*이 아니다.
                if ((proven_size_t)in->a < IR_MAXLOCALS && g_cmax && g_cmax[in->a] > 0) {
                    if (res.v.hi > g_cmax[in->a]) { res.v.hi = g_cmax[in->a]; res.v.wide = 0; }
                    if (res.v.lo < 0) res.v.lo = 0;
                }
                res.slot = (proven_i32)in->a;
                res.lenlt = ((proven_size_t)in->a < IR_MAXLOCALS) ? st->lenlt[in->a] : -1;
                res.lenstr = ((proven_size_t)in->a < IR_MAXLOCALS) ? st->lenstr[in->a] : 0;
                res.lenoff = ((proven_size_t)in->a < IR_MAXLOCALS) ? st->lenoff[in->a] : 0;
                res.aff_s  = (proven_i32)in->a; res.aff_c = 0;   // ★ 지역 자신 = 아핀(오프셋 0)
                res.vlanes = ((proven_size_t)in->a < IR_MAXLOCALS) ? st->vlanesloc[in->a] : 0;
                res.lenof = ((proven_size_t)in->a < IR_MAXLOCALS) ? st->lenofloc[in->a] : -1;
                if ((proven_size_t)in->a < IR_MAXLOCALS) {
                    res.divof_s = st->divofloc_s[in->a]; res.divof_c = st->divofloc_c[in->a];
                    res.prod_l = st->prodloc_l[in->a]; res.prod_r = st->prodloc_r[in->a];
                }
                break;
            // ★★★★★ **배타 수정 참조를 넘기는 순간 그 지역은 남의 것이 된다** (2026-08-13, D0a).
            //   `mut_ref a` 를 만들면 그 뒤로 `a` 가 **내 본문의 STORE 없이** 바뀔 수 있다 —
            //   피호출자가 `set p (…)` 로 서술자를 통째로 갈아끼울 수 있기 때문이다(실증:
            //   경계 검사가 지워진 채 E-VM-ANALYSIS). ⇒ **빌려주는 자리에서 사실을 죽인다.**
            //   ★ 읽기 참조(`ref`)는 죽이지 않는다 — 그것으로는 바꿀 수 없다(권한이 능력을 가른다).
            //   ★ 만드는 지점에서 죽이는 것이 호출 지점에서 죽이는 것보다 **좁고 안전하다**:
            //     참조가 어디로 흘러가 언제 쓰이는지 좇을 필요가 없다.
            case IRW_MREF:
                if ((proven_size_t)in->a < IR_MAXLOCALS) iv_kill_local(st, (proven_size_t)in->a, false);
                res.slot = -1;
                break;
        case IRW_LEN:
                // ★ 이 값은 `len <slot>` **자신**이다. 이름이 있고, **구간도 있다.**
                res.lenof = ops[0].slot;
                if (ops[0].slot >= 0 && (proven_size_t)ops[0].slot < IR_MAXLOCALS)
                    res.v = st->lenv[ops[0].slot];
                break;
            case IRW_VLOAD: {
                // ★★★★★ **벡터 로드의 경계 검사** (2026-08-16, WO-0056 P2).
                //   `index` 에는 2026-07 부터 있던 제거 규칙이 **벡터 경로에는 아예 없었다** —
                //   세지도 않았으므로 `--ir` 의 제거율에도 안 잡혔다(**안 세는 것은 안 보인다**).
                //   실측: simd 내적에서 이 검사 둘이 시간의 **절반**이었다(0.719 → 0.348 ms).
                //
                //   근거는 스칼라와 **같은 관계 사실**이고, 다른 것은 **여백**뿐이다:
                //       i ≥ 0  ∧  i + L ≤ len(s)      (L = 레인 수)
                //   여백이 그 자리를 정확히 담는다 — `while le (add i 4) n` 이 `i` 에 여백 4 를
                //   싣고, `requires ge (len a) n` 이 `n ≤ len(a)` 를 심어 추이로 이어진다.
                //   ops[0] = 슬라이스/배열 · ops[1] = 원소 인덱스.
                int lanes_ = (int)((in->a >> 8) & 0xff);
                res.vlanes = (proven_u8)lanes_;      // ★ 값을 따라 흐른다(아래 VSTORE 가 읽는다)
                if (ops[0].slot >= 0 && lanes_ > 0) {
                    proven_i32 sl_ = ops[0].slot, ix_ = ops[1].slot;
                    //  ① 길이 사실이 곧장 서는 경우:  i + L ≤ len(s)
                    bool ok_ = (ops[1].lenlt == sl_ &&
                                ops[1].lenoff + (ops[1].lenstr ? 1 : 0) >= lanes_);
                    //  ② **용량 형태** — 슬라이스 파라미터에서 실제로 살아남는 길:
                    //       len(s) ≥ p  (진입 계약, lenge_p)  ∧  i + L ≤ p   ⟹  i + L ≤ len(s).
                    //     `iv_row_major_ok` 의 용량 규칙과 **같은 논증**이고, 다른 것은
                    //     엄격(margin 1) 자리에 **레인 수**가 온다는 것뿐이다.
                    if (!ok_ && ix_ >= 0 && (proven_size_t)ix_ < IR_MAXLOCALS &&
                        (proven_size_t)sl_ < IR_MAXLOCALS &&
                        st->lenge_q[sl_] < 0 && st->lenge_p[sl_] >= 0)
                        ok_ = iv_lemargin(st, (proven_size_t)ix_, st->lenge_p[sl_]) >= lanes_;
                    if (mark) (*total)++;
                    if (mark && ok_ && ops[1].v.lo >= 0) {
                        in->a |= IR_POL_PROVEN; (*proven)++;
                        cert_put(d, i, IRW_VLOAD, "R-VLOAD-MARGIN", 3,
                                 (proven_i64)ops[1].v.lo, (proven_i64)ops[1].v.hi,
                                 (proven_i64)lanes_);
                    }
                }
                break;
            }
            case IRW_VSTORE: {
                // ★★★★★ **쓰기도 짝으로 증명한다** (2026-08-17, SIMD-0001).
                //   읽기(`IRW_VLOAD`)만 증명하고 쓰기를 안 하면, **쓰는 커널은 증명의 값을
                //   하나도 못 받는다** — 그리고 벡터 커널은 대개 읽고 **쓴다**.
                //   근거는 읽기와 **같은 여백 규칙**(RFC-0053 부록 B): i ≥ 0 ∧ i + L ≤ len(s).
                //   ops[0] = 슬라이스 · ops[1] = 인덱스 · ops[2] = 벡터(L 을 실어 온다).
                //   ★ L 을 모르면(`vlanes == 0`) **증명하지 않는다.** 쓰기에서 경계를 잘못
                //     지우면 남의 바이트를 밟는다 — 모를 때는 지우지 않는 쪽이 유일한 답이다.
                int vln_ = (int)ops[2].vlanes;
                if (ops[0].slot >= 0 && vln_ > 0) {
                    proven_i32 sl_ = ops[0].slot, ix_ = ops[1].slot;
                    bool ok_ = (ops[1].lenlt == sl_ &&
                                ops[1].lenoff + (ops[1].lenstr ? 1 : 0) >= vln_);
                    if (!ok_ && ix_ >= 0 && (proven_size_t)ix_ < IR_MAXLOCALS &&
                        (proven_size_t)sl_ < IR_MAXLOCALS &&
                        st->lenge_q[sl_] < 0 && st->lenge_p[sl_] >= 0)
                        ok_ = iv_lemargin(st, (proven_size_t)ix_, st->lenge_p[sl_]) >= vln_;
                    if (mark) (*total)++;
                    if (mark && ok_ && ops[1].v.lo >= 0) {
                        in->a |= IR_POL_PROVEN; (*proven)++;
                        cert_put(d, i, IRW_VSTORE, "R-VSTORE-MARGIN", 3,
                                 (proven_i64)ops[1].v.lo, (proven_i64)ops[1].v.hi,
                                 (proven_i64)vln_);
                    }
                }
                break;
            }
            case IRW_SWAP: {
                // ★★ `swap s i j` 의 경계 검사 — 두 색인을 **`index` 와 같은 규칙**으로 따로 증명한다
                //   (2026-10-03). 전에는 이 op 에 증명 자리가 없어서, 분석이 `i < len(s)` 를 알아도
                //   C 출력이 늘 두 색인을 다시 쟀다 — 새로 짠 퀵정렬이 옛 판(손으로 쓴 맞바꾸기)보다
                //   ×1.9 느렸던 까닭의 하나다. 둘 다 서야 지운다. 증명서는 `R-IDX-LENLT` 하나에
                //   두 색인 구간을 합쳐 싣는다(검증기가 보는 것은 하한 ≥ 0 이고, 합친 하한이 그것이다).
                //   ops[0] = 슬라이스 · ops[1] = i · ops[2] = j. 원소는 자리만 바뀌므로 원소 구간은 그대로다.
                if (ops[0].slot >= 0) {
                    if (mark) (*total)++;
                    bool ok_ = true;
                    for (int q_ = 1; q_ <= 2; q_++) {
                        const ivs_t *x_ = &ops[q_];
                        bool lt_ = x_->lenlt == ops[0].slot && x_->lenoff + (x_->lenstr ? 1 : 0) >= 1 && x_->v.lo >= 0;
                        bool lo_ = x_->v.lo == x_->v.hi && !x_->v.wide && x_->v.lo >= 0 &&
                                   (proven_size_t)ops[0].slot < IR_MAXLOCALS &&
                                   !st->lenv[ops[0].slot].wide && st->lenv[ops[0].slot].lo > x_->v.hi;
                        if (!lt_ && !lo_) ok_ = false;
                    }
                    if (mark && ok_) {
                        in->a |= IR_POL_PROVEN; (*proven)++;
                        cert_put(d, i, IRW_SWAP, "R-IDX-LENLT", 2,
                                 (proven_i64)(ops[1].v.lo < ops[2].v.lo ? ops[1].v.lo : ops[2].v.lo),
                                 (proven_i64)(ops[1].v.hi > ops[2].v.hi ? ops[1].v.hi : ops[2].v.hi));
                    }
                }
                break;
            }
            case IRW_ISTORE: {
                // ★ 쓰기의 경계 검사도 같은 근거로 제거된다 — `idx_no_check` (Qed).
                //   읽기와 **같은 관계 사실**(i < len(s))이면 충분하다.
                //   ops[0] = 슬라이스, ops[1] = 인덱스, ops[2] = 값.
                if (ops[0].slot >= 0) {
                    if (mark) (*total)++;
                    if (mark && ((ops[1].lenlt == ops[0].slot && ops[1].lenoff + (ops[1].lenstr ? 1 : 0) >= 1 && ops[1].v.lo >= 0) ||
                                 iv_row_major_ok(st, &ops[1], ops[0].slot))) {
                        in->a |= IR_POL_PROVEN; (*proven)++;
                        if (ops[1].lenlt == ops[0].slot && ops[1].lenoff + (ops[1].lenstr ? 1 : 0) >= 1 && ops[1].v.lo >= 0)
                            cert_put(d, i, IRW_ISTORE, "R-IDX-LENLT", 2,
                                     (proven_i64)ops[1].v.lo, (proven_i64)ops[1].v.hi);
                        else
                            cert_put(d, i, IRW_ISTORE, "R-IDX-ROWMAJOR", 2,
                                     (proven_i64)ops[1].v.lo, (proven_i64)ops[1].v.hi);
                    } else if (mark && ops[1].v.lo == ops[1].v.hi && !ops[1].v.wide &&
                               ops[1].v.lo >= 0 &&
                               (proven_size_t)ops[0].slot < IR_MAXLOCALS &&
                               !st->lenv[ops[0].slot].wide &&
                               st->lenv[ops[0].slot].lo > ops[1].v.hi) {
                        // ★★★★★ **쓰기도 같은 규칙이다** (2026-08-27, WO-0130).
                        //   읽기(`IRW_INDEX`)에 `R-IDX-LENLO` 를 달고 쓰기를 안 달면
                        //   **읽기는 되고 쓰기는 안 되는 비대칭**이 된다 — 이 파일이 이미
                        //   한 번 앓은 병이다(`set (index s i) (index s j)` 맞바꾸기, 2026-07-26).
                        //   *규칙이 같으면 두 자리에 같이 적어야 한다.*
                        in->a |= IR_POL_PROVEN; (*proven)++;
                        cert_put(d, i, IRW_ISTORE, "R-IDX-LENLO", 2,
                                 (proven_i64)ops[1].v.lo, (proven_i64)st->lenv[ops[0].slot].lo);
                    } else if (mark && g_smt_on && ops[1].v.lo >= 0) {
                        // ★★★ **마지막 수단으로 SMT** — 특수 경우들이 못 넘은 자리만 온다.
                        //   ☞ 하한(i ≥ 0)은 구간이 준다: 위 규칙들과 **같은 전제**를 쓴다.
                        low_smt_sys_t sy; proven_i64 lm[LOW_SMT_MAXC]; ivs_map_t mp;
                        if (iv_smt_index_ok(st, ops[1].slot, ops[0].slot, &sy, lm, &mp) &&
                            cert_put_smt(d, i, IRW_ISTORE, &sy, lm)) {
                            in->a |= IR_POL_PROVEN; (*proven)++;
                            smt_emit_query(d, i, &sy);
                        }
                    }
                    // ★★★ **건전성**: 슬라이스에 값을 쓰면 원소 구간은 **그 값을 포함하도록 넓힌다.**
                    //   써 넣은 값이 선언 경계 안이면 구간 불변(부분집합과의 합), 밖이면 넓어져
                    //   사실이 정확히 약해진다 — 그러면 뒤의 `index` 가 낡은 경계를 못 쓴다.
                    if ((proven_size_t)ops[0].slot < IR_MAXLOCALS)
                    {
                        proven_size_t sl_ = (proven_size_t)ops[0].slot;
                        // ★ 관찰: 이 슬라이스가 **어디서** 쓰이는가(루프 안인가 밖인가).
                        if (!g_ewr_at[sl_]) g_ewr_at[sl_] = (proven_i64)(i + 1);
                        {
                            bool inloop = false;
                            for (proven_size_t q_ = 0; q_ < g_nlp; q_++)
                                if (i >= g_lp[q_].h && i <= g_lp[q_].b) { inloop = true; break; }
                            if (!inloop) g_ewr_out[sl_] = true;
                        }
                        // ★ 검증: 상계를 물린 패스에서 **쓰는 값이 상계를 넘으면** 그 후보는 틀렸다.
                        // ★★★ **검증은 「한 걸음」 을 본다** — 상계 그 자체가 아니라.
                        //   상계 C 를 가정하고 「쓰는 값 ≤ C」 를 물으면 **영원히 못 닫는다**:
                        //   쓰는 값이 `원소 + 1` 이면 그것은 C + 1 이다(실측: 정확히 1 넘쳤다).
                        //   ⇒ 물어야 할 것은 **한 걸음의 크기**다: 쓰는 값이 «지금 원소 상계 + c»
                        //     안인가. 그것이 참이면 T 바퀴 뒤의 값은 **진입 + c·T** 이고, 그것이
                        //     바로 위에서 고른 C 다. 세는 변수 상계가 쓰는 논증과 **같은 모양**이다.
                        //   ☞ *귀납으로 못 닫는 사실은 바깥 논증으로 닫고, 그 논증의 전제를 검사한다.*
                        if (g_ecap_on && g_emax[sl_] > 0) {
                            // ★★★ 걸음은 **풀 전체**에 대고 잰다. 한 슬라이스만 보면 «다른 표에서
                            //   베껴 오는 줄»(`prev[t] ← cur[t]`)이 한 걸음에 수만을 뛰는 것으로
                            //   보인다 — 그런데 그 줄은 **풀의 최대를 안 올린다**. 그래서 묶이는
                            //   슬라이스들을 한 덩이로 보고 그 덩이의 최대가 한 걸음에 c 이상
                            //   안 자라는지 묻는다. (실측: 이것 없이는 정확히 그 줄에서 걸렸다.)
                            proven_i64 pool_ = 0;
                            for (proven_size_t z_ = 0; z_ < IR_MAXLOCALS; z_++)
                                if (g_emax[z_] > 0 && st->elemv[z_].hi > pool_) pool_ = st->elemv[z_].hi;
                            if (ops[2].v.lo < 0 ||
                                (pool_ < INT64_MAX - 1 && ops[2].v.hi > pool_ + 1))
                                g_ecap_bad = true;
                        }
                    }
                        st->elemv[ops[0].slot] = iv_join2(st->elemv[ops[0].slot], ops[2].v);
                        // ★ 자르기 — 후보 상계 안으로. 검증이 통과해야 이 자름이 사실이 된다.
                        if (g_ecap_on && g_emax[ops[0].slot] > 0) {
                            if (st->elemv[ops[0].slot].hi > g_emax[ops[0].slot]) {
                                st->elemv[ops[0].slot].hi = g_emax[ops[0].slot];
                                st->elemv[ops[0].slot].wide = 0;
                            }
                            if (st->elemv[ops[0].slot].lo < 0) st->elemv[ops[0].slot].lo = 0;
                        }
                }
                break;
            }
            case IRW_INDEX: {
                // ★★ RFC-0055 D5 — 경계 검사 제거. ★ `idx_no_check` (Qed).
                //   근거는 **관계 사실**이다: i < len(g) 이고 i ≥ 0 이면 index g i 는 안전하다.
                //   구간만으로는 절대 못 한다(len 이 런타임 값이라 ⊤ 다).
                if (ops[0].slot >= 0) {
                    if (mark) (*total)++;
                    if (mark && ((ops[1].lenlt == ops[0].slot && ops[1].lenoff + (ops[1].lenstr ? 1 : 0) >= 1 && ops[1].v.lo >= 0) ||
                                 iv_row_major_ok(st, &ops[1], ops[0].slot))) {
                        in->a |= IR_POL_PROVEN; (*proven)++;
                        // ★ 증명서: 어느 규칙으로 지웠나 + 그 규칙이 쓴 수.
                        //   R-IDX-LENLT  = 관계 사실 i < len(s) 그리고 i ≥ 0
                        //   R-IDX-ROWMAJOR = 계약 len(s) ≥ p·q 와 i<p · k<q (비선형)
                        if (ops[1].lenlt == ops[0].slot && ops[1].lenoff + (ops[1].lenstr ? 1 : 0) >= 1 && ops[1].v.lo >= 0)
                            cert_put(d, i, IRW_INDEX, "R-IDX-LENLT", 2,
                                     (proven_i64)ops[1].v.lo, (proven_i64)ops[1].v.hi);
                        else
                            cert_put(d, i, IRW_INDEX, "R-IDX-ROWMAJOR", 2,
                                     (proven_i64)ops[1].v.lo, (proven_i64)ops[1].v.hi);
                    } else if (mark && ops[1].v.lo == ops[1].v.hi && !ops[1].v.wide &&
                               ops[1].v.lo >= 0 &&
                               (proven_size_t)ops[0].slot < IR_MAXLOCALS &&
                               !st->lenv[ops[0].slot].wide &&
                               st->lenv[ops[0].slot].lo > ops[1].v.hi) {
                        // ★★★★★ **상수 색인도 길이 사실에 닿아야 한다** (2026-08-27, WO-0129).
                        //   위 규칙은 색인의 **슬롯에 실린 관계 사실**(`lenlt`)을 본다.
                        //   상수는 슬롯이 없어서 그 기제에 **닿지 못했다.** 그래서 같은
                        //   프로그램이 색인의 **철자**만으로 갈렸다:
                        //       guard ge (len s) 1 .  index s 0    →  0 / 1  (안 지워짐)
                        //       guard ge (len s) 1 .  index s i    →  1 / 1  (지워짐, i=0)
                        //   ⇒ 상수에는 **길이의 하한**이 곧 답이다: len(s).lo > C 이면 C < len(s).
                        //   규모: 상수 색인이 코퍼스에 750 곳(lib/ 457) — 지역 색인 417 곳과
                        //   맞먹는다. **관용구의 절반이 분석에서 빠져 있었다.**
                        in->a |= IR_POL_PROVEN; (*proven)++;
                        cert_put(d, i, IRW_INDEX, "R-IDX-LENLO", 2,
                                 (proven_i64)ops[1].v.lo, (proven_i64)st->lenv[ops[0].slot].lo);
                    } else if (mark && g_smt_on && ops[1].v.lo >= 0) {
                        // ★★★ **마지막 수단으로 SMT** (REQ-0004 · 후속 M).
                        //   특수 경우 셋이 전부 못 넘은 자리에서만 부른다 — 순서가 중요하다:
                        //   기존 규칙이 먼저여야 증명서의 종류가 바뀌지 않고(회귀 없음),
                        //   **SMT 가 실제로 몇 개를 더 지웠는지**가 그 자체로 측정된다.
                        low_smt_sys_t sy; proven_i64 lm[LOW_SMT_MAXC]; ivs_map_t mp;
                        if (iv_smt_index_ok(st, ops[1].slot, ops[0].slot, &sy, lm, &mp) &&
                            cert_put_smt(d, i, IRW_INDEX, &sy, lm)) {
                            in->a |= IR_POL_PROVEN; (*proven)++;
                            smt_emit_query(d, i, &sy);
                        }
                    }
                    // ★★★ **배열 내용 사실**: 슬라이스 s 의 모든 원소가 구간 안이면, `index s i` 의
                    //   값도 그 구간이다 — ⊤ 대신 이걸 낸다(그전엔 원소값이 늘 미지였다).
                    if ((proven_size_t)ops[0].slot < IR_MAXLOCALS) res.v = st->elemv[ops[0].slot];
                }
                break;
            }
            case IRW_ASSERT: {
                // ★ 출구 ensures 검사(a = 1)만 제거 대상이다. 진입 requires(a = 0)는 **절대**
                //   제거하지 않는다 — 분석이 requires 를 사실로 심었으므로, 그 검사를 지우면
                //   자기가 심은 사실로 자기를 정당화하는 **순환**이 된다(그러면 계약이 거짓말이 된다).
                if (!(in->a & 3)) break;      // 진입 requires(a=0)는 **절대** 제거하지 않는다
                if (mark) (*total)++;
                // ★ 먼저 **술어 기억**을 본다 — 같은 식이 이 경로에서 참임이 이미 밝혀졌는가.
                //   `guard eq X 1 else return error E` 의 else 가지에서 `ne X 1` 은 자명하다.
                {
                    bool tv;
                    if (iv_fact_get(st, d, ops[0].eid, ops[0].ifrom, ops[0].ito, &tv) &&
                        (tv != ops[0].pneg)) {
                        if (mark) {
                            in->a |= IR_POL_PROVEN; (*proven)++;
                            // R-ASSERT-FACT: 이 경로에서 **같은 식이 이미 참**임이 기록돼 있다.
                            //   ★ 산술이 아니라 **경로 사실**이다 — 검증기가 재도출할 수 없다.
                            //   그래도 증명서를 낸다: 그러면 재고가 **완전**해지고, 검증기가
                            //   "재도출 불가"를 **세어서 말할 수 있다.** 빈칸으로 두면 그 31 자리가
                            //   무엇인지 아무도 모른다 — 세어지지 않는 것은 관리되지 않는다.
                            // ★★ **사실의 출처와 극성을 함께 싣는다**(2026-07-31). 사실 자체는
                            //   재도출할 수 없지만, **그 사실로부터의 추론 단계**는 검사할 수 있다:
                            //     · 사실이 기록된 구간(ifrom..ito)이 이 자리보다 **앞**이어야 한다
                            //     · 기록된 참/거짓이 요구하는 극성과 **맞아야** 한다(tv != pneg)
                            //   ⇒ 재고 전용이던 규칙이 **부분 검증**으로 올라간다. 사실을 못 따져도
                            //     **사실을 쓰는 방법**은 따질 수 있다 — 그것이 이 검증기의 일이다.
                            cert_put(d, i, IRW_ASSERT, "R-ASSERT-FACT", 5,
                                     (proven_i64)ops[0].eid, (proven_i64)ops[0].ifrom,
                                     (proven_i64)ops[0].ito, (proven_i64)(tv ? 1 : 0),
                                     (proven_i64)(ops[0].pneg ? 1 : 0));
                        }
                        break;
                    }
                }
                if (ops[0].pred &&
                    ((ops[0].slot >= 0 && (proven_size_t)ops[0].slot < IR_MAXLOCALS) ||
                     (ops[0].lenpred >= 0 && (proven_size_t)ops[0].lenpred < IR_MAXLOCALS))) {
                    iv_t v = ops[0].slot >= 0 ? st->loc[ops[0].slot] : st->lenv[ops[0].lenpred];   // ★ 지역이 있으면 지역(옛 차례 그대로)
                    iv_t r = ops[0].rhs;
                    bool ok = false;
                    switch (ops[0].pred) {
                        case IRW_LT: ok = v.hi <  r.lo; break;
                        case IRW_LE: ok = v.hi <= r.lo; break;
                        case IRW_GT: ok = v.lo >  r.hi; break;
                        case IRW_GE: ok = v.lo >= r.hi; break;
                        case IRW_EQ: ok = v.lo == v.hi && r.lo == r.hi && v.lo == r.lo; break;
                        case IRW_NE: ok = v.hi < r.lo || v.lo > r.hi; break;
                        default: break;
                    }
                    if (ok && mark) {
                        in->a |= IR_POL_PROVEN; (*proven)++;
                        // ★ R-ASSERT-CMP-*: **완전히 재도출 가능한** 산술이다.
                        //   비교 낱말을 **규칙 이름에 담는다** — 숫자 코드로 넘기면 검증기가
                        //   그 코드표를 또 한 벌 갖게 되고, 두 벌은 갈린다.
                        //   ★ 여기 `in->a == in->a &&` 가 붙어 있었다(2026-08-16 제거) —
                        //     늘 참이라 아무 일도 안 하면서 `-Wtautological-compare` 를 울렸고,
                        //     읽는 사람에게는 *"a 에 무슨 조건이 있나?"* 를 묻게 했다.
                        //     **아무 일도 안 하는 조건은 뜻이 없는 게 아니라 뜻을 흐린다.**
                        const char *rn =
                            ops[0].pred == IRW_LT ? "R-ASSERT-CMP-LT" :
                            ops[0].pred == IRW_LE ? "R-ASSERT-CMP-LE" :
                            ops[0].pred == IRW_GT ? "R-ASSERT-CMP-GT" :
                            ops[0].pred == IRW_GE ? "R-ASSERT-CMP-GE" :
                            ops[0].pred == IRW_EQ ? "R-ASSERT-CMP-EQ" :
                            ops[0].pred == IRW_NE ? "R-ASSERT-CMP-NE" : "R-ASSERT-CMP-?";
                        cert_put(d, i, IRW_ASSERT, rn, 4,
                                 (proven_i64)v.lo, (proven_i64)v.hi,
                                 (proven_i64)r.lo, (proven_i64)r.hi);
                    }
                }
                break;
            }
            case IRW_NOT:
                // ★ `guard COND else …` 는 COND 뒤에 **not** 을 넣고 brz 한다.
                //   not 이 술어 정보를 버리면 **guard 의 분기 내로잉이 통째로 죽는다** —
                //   실제로 죽어 있었다. 술어를 **뒤집어서** 그대로 나른다.
                if (ops[0].pred) {
                    res.pred    = iv_negate(ops[0].pred);
                    res.slot    = ops[0].slot;
                    res.rhs     = ops[0].rhs;
                    res.lenpred = ops[0].lenpred;
                    res.lenlt   = ops[0].lenlt;
                    res.lenstr  = ops[0].lenstr;
                    // ★★★★★ **`rslot` 한 줄이 빠져 있었다** (2026-08-11).
                    //   바로 위 주석이 *"not 이 술어 정보를 버리면 분기 내로잉이 통째로
                    //   죽는다 — 실제로 죽어 있었다"* 고 적어 두고, **다섯 줄을 나르면서
                    //   여섯째를 빠뜨렸다.** 같은 결함의 두 번째 판이다.
                    //   ⇒ 결과: `guard lt i n` 처럼 **우변이 지역**인 비교에서 R5 차분 제약이
                    //     **한 번도 안 심겼다**(실측: `lerel` 심은 횟수 0). 그 위에 얹히는
                    //     추이(`i < n < len a`)도 당연히 안 됐고, 그래서 **계약이 루프에서
                    //     값을 못 냈다**(RFC-0093 §4-1 이 "못 한 것" 으로 적어 둔 그것).
                    //   ☞ 찾는 법이 기록될 값어치가 있다: 비교는 `res.rslot=1` 을 실었는데
                    //     분기에서 집힌 술어는 `rslot=-1` 이고 `pred` 가 15→18 로 **뒤집혀**
                    //     있었다. 뒤집힌 것이 곧 `not` 을 가리켰다.
                    res.rslot   = ops[0].rslot;
                }
                res.eid   = ops[0].eid;         // ★ 같은 식이다 — 다만 뒤집혔다
                res.deps  = ops[0].deps;
                res.pneg  = !ops[0].pneg;
                res.ifrom = ops[0].ifrom;       // 명령 범위는 **피연산자의 것** (not 은 뺀다)
                res.ito   = ops[0].ito;
                res.v = (iv_t){ 0, 1 };
                break;
            case IRW_LT: case IRW_LE: case IRW_GT: case IRW_GE: case IRW_EQ: case IRW_NE:
                // 좌변이 지역이면 술어로 기록한다. 우변은 **구간**으로 들고 간다 —
                // 상수든 다른 지역이든 상관없다(`lt i n` 에서 n ≤ 100 이면 i ≤ 99).
                if (ops[0].slot >= 0) {
                    res.pred = (proven_u8)in->w; res.slot = ops[0].slot; res.rhs = ops[1].v;
                    // ★ 좌변 지역이 `len g` 를 담았으면(`let n be len s`) 술어는 **g 의 길이**에 관한 것이기도 하다 (2026-10-04).
                    //   전에는 `guard ge n 2` 가 n 만 좁히고 «len s ≥ 2» 를 안 세워 `set (idx s 1) 0` 의 검사가 남았다(체 새 판).
                    res.lenpred = ops[0].lenof;
                    res.lenlt = ops[1].lenof;   // ★ 우변이 `len g` 였으면 그 이름을 술어에 실어 보낸다
                    res.rslot = ops[1].slot;    // ★ R5: 우변이 **지역**이면 관계 사실이 된다
                    res.aff_s = ops[0].aff_s; res.aff_c = ops[0].aff_c;
                    res.raff_s = ops[1].aff_s; res.raff_c = ops[1].aff_c;
                } else if (ops[0].aff_s >= 0) {
                    // ★★★★ **좌변이 `지역 + 상수`** (2026-08-16, P2) — `le (add i 4) n`.
                    //   전에는 여기서 술어가 통째로 버려졌다(좌변이 슬롯이 아니므로). 그 한 줄이
                    //   벡터 루프의 경계 검사를 전부 살려 두고 있었다.
                    res.pred = (proven_u8)in->w; res.slot = -1; res.rhs = ops[1].v;
                    res.lenlt = ops[1].lenof; res.rslot = ops[1].slot;
                    res.aff_s = ops[0].aff_s; res.aff_c = ops[0].aff_c;
                    res.raff_s = ops[1].aff_s; res.raff_c = ops[1].aff_c;
                } else if (ops[0].prod_l >= 0 && ops[0].prod_r >= 0) {
                    // ★★★★★ **좌변이 곱** (2026-08-17) — `while lt (mul i i) n`.
                    //   `i·i < n` 이면 `i < n` 이다. 이 저장소는 이것을 오래
                    //   *"비선형이라 못 한다"* 고 적어 두었는데, 곱을 **계산할** 필요가 없다:
                    //   `f ≤ f·o`(다른 인자가 ≥ 1)면 `f ≤ f·o < n` 으로 끝난다.
                    //   ★ **참인 쪽만** 말한다 — `i·i ≥ n` 은 `i ≥ n` 을 **뜻하지 않는다**.
                    //     그래서 `res.slot` 을 쓰면 안 된다(그건 양쪽 가지를 다 좁힌다).
                    res.pred = (proven_u8)in->w; res.slot = -1; res.rhs = ops[1].v;
                    res.rslot = ops[1].slot;
                    res.prod_l = ops[0].prod_l; res.prod_r = ops[0].prod_r;
                    res.puns = !(in->a & IR_TY_SIGNED);
                } else if (ops[0].dif_l >= 0) {
                    // ★★★★★ **좌변이 두 지역의 차분** (2026-08-17) — `gt (sub hi lo) 1`.
                    //   여기까지 오면 좌변이 슬롯도 아핀도 아니라 술어가 버려졌다.
                    res.pred = (proven_u8)in->w; res.slot = -1; res.rhs = ops[1].v;
                    res.dif_l = ops[0].dif_l; res.dif_r = ops[0].dif_r;
                } else if (ops[0].lenof >= 0) {
                    // ★ 좌변이 `len g` 다 — 그러면 이 술어는 **g 의 길이**에 관한 것이다.
                    res.pred = (proven_u8)in->w; res.lenpred = ops[0].lenof; res.rhs = ops[1].v;
                }
                // ★ 비교 연산자를 **정규화**한다. ne = ¬eq · ge = ¬lt · le = ¬gt 이므로,
                //   `eq X 1` 과 `ne X 1` 은 **같은 식의 두 진리값**이다. 지문이 같아야 한다.
                //   (이것을 안 하면 guard 가 심은 사실과 when 조건이 서로를 못 알아본다.)
                {
                    low_irw_t canon = in->w; bool neg = false;
                    switch (in->w) {
                        case IRW_NE: canon = IRW_EQ; neg = true; break;
                        case IRW_GE: canon = IRW_LT; neg = true; break;
                        case IRW_LE: canon = IRW_GT; neg = true; break;
                        default: break;
                    }
                    proven_u64 h = iv_mix(iv_mix(1469598103934665603ULL, (proven_u64)canon),
                                          (proven_u64)in->a);
                    h = iv_mix(h, ops[0].eid);
                    h = iv_mix(h, ops[1].eid);
                    res.eid  = h;
                    res.deps = ops[0].deps | ops[1].deps;
                    res.pneg = neg;
                    // 명령 범위는 이미 위에서 잡혔다(ifrom = 피연산자들의 최소, ito = i).
                }
                res.v = (iv_t){ 0, 1 };
                break;
            case IRW_DIV: case IRW_MOD: {
                if (!(in->a & IR_TY_KNOWN) || (in->a & IR_TY_FLT) || (in->a & IR_POL_NZ)) break;
                ityp_t t = { true, (proven_u8)(in->a & 0xff), (in->a & IR_TY_SIGNED) != 0, false, false, 0, 0 };
                if (mark) (*total)++;
                bool nz = ops[1].v.lo > 0 || ops[1].v.hi < 0;
                bool no_mn = !t.sign || ops[0].v.lo > ity_lo(t.bits, true) ||
                             ops[1].v.lo > -1 || ops[1].v.hi < -1;
                if (nz && no_mn && mark) {
                    in->a |= IR_POL_PROVEN; (*proven)++;
                    // R-DIV-NZ: 제수 구간이 0 을 **포함하지 않고**, 부호 나눗셈이면
                    //   MIN/-1 짝도 불가능하다(`div_signed_failure_is_only_min_neg1`, Qed).
                    cert_put(d, i, in->w, "R-DIV-NZ", 6,
                             (proven_i64)ops[0].v.lo, (proven_i64)ops[0].v.hi,
                             (proven_i64)ops[1].v.lo, (proven_i64)ops[1].v.hi,
                             (proven_i64)t.bits, (proven_i64)(t.sign ? 1 : 0));
                }
                res.v = iv_ty(t);
                // ★★★★★ **양수 상수로 나누면 결과가 좁아진다** (2026-08-19).
                //   여태 `div`·`mod` 의 결과는 **타입 전체**였다. 그래서 `q = x / 6` 다음의
                //   `mul q 6` 이 못 지워졌다 — x 의 상계를 알고 있는데도.
                //   · `x / c` (c > 0, x ≥ 0) ⇒ [lo/c, hi/c]
                //   · `x % c` (c > 0, x ≥ 0) ⇒ [0, min(hi, c−1)]
                //   ★ 부호형은 안 건드린다: 잘림 방향이 부호에 달려 있고(C 는 0 쪽으로,
                //     이 언어의 `mod` 는 **유클리드**다) 그 차이가 정확히 이 저장소가 한 번
                //     당한 자리다(`mod` 가 세 곳에서 `rem` 을 내던 결함). 무부호만 말한다.
                if (ops[1].v.lo == ops[1].v.hi && ops[1].v.lo > 0 &&
                    ops[0].v.lo >= 0 && !t.sign) {
                    proven_i64 cdiv = ops[1].v.lo;
                    // ★ 출처를 싣는다 — 아래 SUB 가 `x − (x/c)·c` 를 알아보는 근거다.
                    if (in->w == IRW_DIV && ops[0].slot >= 0 &&
                        !(in->a & (IR_POL_WRAP | IR_POL_SAT | 0x40000))) {
                        res.divof_s = ops[0].slot; res.divof_c = cdiv;
                    }
                    if (in->w == IRW_DIV) {
                        res.v.lo = ops[0].v.lo / cdiv;
                        if (ops[0].v.hi < res.v.hi * cdiv || ops[0].v.hi / cdiv < res.v.hi)
                            res.v.hi = ops[0].v.hi / cdiv;
                        res.v.wide = ops[0].v.wide;
                    } else {                       // MOD
                        res.v.lo = 0;
                        proven_i64 hm = cdiv - 1;
                        if (ops[0].v.hi < hm) hm = ops[0].v.hi;
                        if (hm < res.v.hi) { res.v.hi = hm; res.v.wide = 0; }
                    }
                }
                // ★ `mod i (len g)` 는 **안전한 인덱스**다 — 0 ≤ 결과 < len(g).
                //   ★ `mod_is_a_safe_index` (Qed, NumericLattice.v). len(g) = 0 이면 0 나눗셈으로
                //   먼저 트랩하므로, 값이 나온 경로에서는 len(g) > 0 이다.
                if (in->w == IRW_MOD && ops[1].lenof >= 0 && (!t.sign || ops[0].v.lo >= 0)) {
                    res.lenlt = ops[1].lenof;
                    res.lenstr = 1;      // ★ 나머지는 **엄격하게** 작다 (0 ≤ r < len g)
                    if (res.v.lo < 0) res.v.lo = 0;
                }
                break;
            }
            case IRW_SHL: case IRW_SHR: {
                // ★★ RFC-0064 §5 — **폭 이상 시프트는 comptime 오류다** (E-SHIFT-RANGE).
                //   런타임 형제 E-VM-SHIFT 를 앞당긴다: 시프트 양이 **모든 경로에서** 타입 폭 이상
                //   (또는 음수)임을 구간이 증명하면, 트랩까지 갈 것 없이 **컴파일 오류**로 거절한다 —
                //   C 의 UB 를 오류로. 구간은 건전한 과근사라 `lo ≥ 폭` 이면 도달 가능한 값이 전부 폭 이상이다.
                //   (감싸기를 원하면 이름으로 말한다: `wrap_shl`/`wrap_shr` = IRW_WSHL/WSHR — 여기 없다.)
                if ((in->a & IR_TY_KNOWN) && !(in->a & IR_TY_FLT)) {
                    proven_i64 bits = in->a & 0xff;
                    if (bits > 0 && mark && (ops[1].v.lo >= bits || ops[1].v.hi < 0))
                        ir_fail(c, "E-SHIFT-RANGE",
                                "the shift amount is provably NOT smaller than the type's width (or is "
                                "negative) — in C this is UNDEFINED BEHAVIOUR, so here it is a COMPILE "
                                "error, not a runtime trap. If you meant the masking wrap-around, say it "
                                "by name: `wrap_shl` / `wrap_shr`.",
                                c->def_form ? c->def_form->line : 0);
                }
                break;
            }
            case IRW_CAST: {
                proven_u8 nb = (proven_u8)((in->a & 0xff) * 8);
                // ★★★★★ **넓히기는 값을 바꾸지 않는다 — 그런데 구간을 버리고 있었다**
                //   (2026-08-23, RFC-0095 §8-7). 64 비트로 넓히는 자리에서 통째로 `break` 라
                //   `widen u64 v` 의 결과가 **⊤(모름)** 이 됐다. 그래서 `v : u8`(정의상 [0,255])을
                //   넓히기만 해도 그 사실이 사라졌고, 뒤따르는 덧셈·좁히기가 전부 미증명이 됐다.
                //   ☞ 실측이 자리를 짚었다: `narrow u8 (widen u64 v)` 가 u8 파라미터에서도
                //     검사를 남겼다 — 슬라이스와 **무관한** 자리였다(처음엔 슬라이스 탓인 줄 알았다).
                //   ★ 건전성: 넓히기는 표현만 커지고 값은 그대로다. 다만 **부호가 섞이면** 아니다 —
                //     음수를 무부호로 넓히면 값이 뒤바뀐다(−1 → 2^64−1). 그래서 조건은 둘 중 하나:
                //     ① 대상이 부호형이다(좁은 타입의 어떤 값도 i64 에 그대로 들어간다) 또는
                //     ② 원본 구간이 **음수를 포함하지 않는다**(그러면 무부호로도 값이 같다).
                if (nb >= 64 && !(in->a & (IR_POL_WRAP | IR_POL_SAT | IR_POL_CHK))) {
                    bool tsign = (in->a & IR_SGN_BIT) != 0;
                    if (tsign || ops[0].v.lo >= 0) res.v = ops[0].v;
                    break;
                }
                if (!nb || nb >= 64 || (in->a & (IR_POL_WRAP | IR_POL_SAT | IR_POL_CHK))) break;
                // ★ 대상 부호는 런타임 판정과 **같은 부호**로 모델링해야 PROVEN 소거가 어긋나지 않는다
                //   (widen 이 IR_SGN_BIT 를 실으면 부호 범위 — Finding: widen i16 −1).
                ityp_t t = { true, nb, (in->a & IR_SGN_BIT) != 0, false, false, 0, 0 };
                if (mark) (*total)++;
                if (iv_in(ops[0].v, t)) {
                    if (mark) {
                        in->a |= IR_POL_PROVEN; (*proven)++;
                        // R-NARROW-FITS: 값 구간이 **목표 타입 범위 안**이다 ⇒ 절단이 없다
                        //   (`narrow_ok_iff`, Qed — narrow 는 fits 일 때만 성공한다).
                        cert_put(d, i, in->w, "R-NARROW-FITS", 4,
                                 (proven_i64)ops[0].v.lo, (proven_i64)ops[0].v.hi,
                                 (proven_i64)t.bits, (proven_i64)(t.sign ? 1 : 0));
                    }
                    res.v = ops[0].v;
                }
                else res.v = iv_ty(t);
                break;
            }
            case IRW_CALL: {
                // ★ D3 op 경계 전파: 인자의 구간이 파라미터 범위에 포함됨이 증명되면
                //   진입 검사를 제거한다(호출 지점마다 독립적으로).
                const low_ir_def_t *cal = &c->out->defs[IR_CALL_IDX(in->a)];
                bool any = false, all = true;
                for (proven_size_t k2 = 0; k2 < cal->nparams && k2 < 3 && k2 < 8; k2++) {
                    if (k2 >= LOW_MAX_PARAMS) break;
                    if (!cal->prng[k2].has_rng) continue;
                    any = true;
                    if (!(ops[k2].v.lo >= cal->prng[k2].rlo && ops[k2].v.hi <= cal->prng[k2].rhi))
                        all = false;
                }
                if (any) {
                    if (mark) (*total)++;
                    if (all && mark) {
                        in->a |= IR_POL_PROVEN; (*proven)++;
                        // ★★ R-CALL-ARGRANGE: 인자의 구간이 파라미터 선언 범위 안이다.
                        //   전에는 **첫 인자만** 실었다(자리 6개로 묶은 대가) — 그래서 검증기가
                        //   "재도출 불가" 로 셀 수밖에 없었다. 이제 **인자마다 증명서를 하나씩**
                        //   낸다: 각 줄이 (구간, 범위) 넷이므로 자리에 맞고, **완전히 재도출된다.**
                        //   ☞ 한 줄에 다 담으려 한 것이 문제였다. **줄을 늘리면 되는 것**이었다.
                        for (proven_size_t k3 = 0; k3 < cal->nparams && k3 < 3 && k3 < 8; k3++) {
                            if (k3 >= LOW_MAX_PARAMS || !cal->prng[k3].has_rng) continue;
                            cert_put(d, i, IRW_CALL, "R-CALL-ARGRANGE", 4,
                                     (proven_i64)ops[k3].v.lo, (proven_i64)ops[k3].v.hi,
                                     (proven_i64)cal->prng[k3].rlo, (proven_i64)cal->prng[k3].rhi);
                        }
                    }
                }
                // ★★ op 경계 전파의 **나머지 반쪽**: requires 는 계약을 callee 안으로 나르고,
                //   ensures 는 그것을 **호출자에게 되돌려 준다.** callee 가 출구에서 강제하므로
                //   호출자는 그것을 사실로 써도 된다(RFC-0053 §6.6 — 사실로 쓰려면 강제해야 한다).
                if (cal->eret.has_rng) res.v = (iv_t){ cal->eret.rlo, cal->eret.rhi };

                // ★★★ **세 번째 쌍대 — `errors E when C .` 도 호출자에게 넘어간다.**
                //   호출자가 **모든 when 조건이 거짓임을 증명하면** 그 호출은 **실패할 수 없다.**
                //   ⇒ `try` 의 오류 가지는 **죽은 코드**다. 검사 하나가 사라진다.
                //
                //   ★ 이것이 **`guard` 가 값을 하는 자리다.** `guard`/`if` 는 **같은 CFG** 를 만들어
                //     분석에 아무 차이도 없다(실측). 그러나 **호출자는 피호출자의 CFG 를 못 본다 —
                //     시그니처만 본다.** `errors … when` 이 그 시그니처이고,
                //     `guard ¬C . else return error E .` 가 그것이 **정직함의 증명**이다.
                //     ⇒ guard 는 **CFG 밖으로** 사실을 실어 나른다. 그것이 if 에 없는 것이다.
                res.nofail = false;
                if (cal->newhen && cal->newhen == cal->nerrv) {
                    bool all_false = true;
                    for (proven_u8 wi = 0; wi < cal->newhen; wi++) {
                        proven_u8 cw = cal->ewhen[wi].cmp;
                        proven_size_t pi = (proven_size_t)cal->ewhen[wi].p;
                        if (!cw || pi >= 3 || pi >= cal->nparams) { all_false = false; break; }
                        iv_t av = ops[pi].v; proven_i64 nn = cal->ewhen[wi].n;
                        bool f_ = false;                       // 조건이 **언제나 거짓**인가
                        switch (cw) {
                            case IRW_LT: f_ = av.lo >= nn;     break;   // ¬(a < n)  ⇔  a ≥ n
                            case IRW_LE: f_ = av.lo >  nn;     break;
                            case IRW_GT: f_ = av.hi <= nn;     break;
                            case IRW_GE: f_ = av.hi <  nn;     break;
                            case IRW_EQ: f_ = av.hi <  nn || av.lo > nn; break;
                            case IRW_NE: f_ = av.lo == nn && av.hi == nn; break;
                            default: break;
                        }
                        if (!f_) { all_false = false; break; }
                    }
                    res.nofail = all_false;
                }
                break;
            }
            case IRW_TRY: case IRW_ELSE_NONE: {
                // ★★★ `try E` 는 오류면 **일찍 반환한다**. `try E else_none` 은 오류를 **버린다**.
                //   둘 다 **오류 가지**를 갖는다 — 그것이 검사다.
                //   피호출자의 `errors … when` 이 **전부 거짓임이 증명되면 그 가지는 죽었다.**
                if (mark) (*total)++;
                if (ops[0].nofail) {
                    if (mark) {
                        in->a |= IR_POL_PROVEN; (*proven)++;
                        // R-TRY-NOFAIL: 피호출자의 `errors … when` 이 **전부 거짓임이 증명됐다**.
                        //   ★ 그 증명은 피호출자 쪽 분석의 산물이라 여기서 재도출할 수 없다 —
                        //     재고에만 올린다(위와 같은 이유).
                        //   ★ 추적을 위해 **어느 호출의 오류 가지**인지는 남긴다(재도출은 못 한다).
                        cert_put(d, i, in->w, "R-TRY-NOFAIL", 1, (proven_i64)ops[0].eid);
                    }
                    res.v = ops[0].v;
                    res.nofail = true;
                }
                break;
            }
            case IRW_ADD: case IRW_SUB: case IRW_MUL: {
                // ★★★ **행우선 인덱스의 출처를 나른다** (2026-07-30 — 아래 §행우선 규칙이 쓴다).
                //   `mul i q` = 두 지역의 곱 · `add (i*q) k` = 행우선 주소.
                //   ☞ **트랩 연산일 때만** 싣는다: wrap/sat 은 조용히 감싸므로 `i·q ≤ (p−1)q` 논증이
                //     깨진다 — 그러면 경계 제거가 곧 **메모리 안전 구멍**이다. chk_*(값으로 답하는 것)
                //     도 제외한다.
                if (!(in->a & (IR_POL_WRAP | IR_POL_SAT | 0x40000)) &&
                    (!(in->a & IR_TY_SIGNED) || (ops[0].v.lo >= 0 && ops[1].v.lo >= 0))) {
                    // ★★ **`(x/c)·c` 를 알아본다** — 아래 SUB 의 항등식이 쓸 출처다.
                    //   양쪽 순서를 다 본다(`mul q 6` · `mul 6 q`).
                    if (in->w == IRW_MUL && !(in->a & IR_TY_SIGNED)) {
                        if (ops[0].divof_s >= 0 && ops[1].v.lo == ops[1].v.hi &&
                            ops[1].v.lo == ops[0].divof_c) {
                            res.dmul_s = ops[0].divof_s; res.dmul_c = ops[0].divof_c;
                        } else if (ops[1].divof_s >= 0 && ops[0].v.lo == ops[0].v.hi &&
                                   ops[0].v.lo == ops[1].divof_c) {
                            res.dmul_s = ops[1].divof_s; res.dmul_c = ops[1].divof_c;
                        }
                    }
                    if (in->w == IRW_MUL && ops[0].slot >= 0 && ops[1].slot >= 0) {
                        res.prod_l = ops[0].slot; res.prod_r = ops[1].slot;
                    } else if (in->w == IRW_ADD && ops[0].prod_l >= 0 && ops[1].slot >= 0) {
                        res.row_i = ops[0].prod_l; res.row_q = ops[0].prod_r; res.row_k = ops[1].slot;
                    }
                    // ★★★★ **아핀 출처**: `지역 + 상수` · `지역 − 상수` (2026-08-16, P2).
                    //   벡터 루프의 표준형 `le (add i 4) n` 이 여기서 사실이 된다. wrap/sat 은
                    //   위 조건이 이미 걸러 냈으므로 **트랩 연산**이고, 그래서 값이 진짜 i±c 다.
                    // ★ 정수일 때만 — 부동 레인에는 여백이라는 개념이 없고, 그 사실이
                    //   정수 규칙(색인·벡터 로드)에 섞이면 뜻이 갈린다.
                    if (!(in->a & IR_TY_KNOWN) || (in->a & IR_TY_FLT)) { /* 아핀 안 싣는다 */ }
                    else if (in->w == IRW_ADD && ops[0].aff_s >= 0 && ops[1].slot == -2)
                        { res.aff_s = ops[0].aff_s; res.aff_c = ops[0].aff_c + ops[1].cst; }
                    else if (in->w == IRW_ADD && ops[1].aff_s >= 0 && ops[0].slot == -2)
                        { res.aff_s = ops[1].aff_s; res.aff_c = ops[1].aff_c + ops[0].cst; }
                    else if (in->w == IRW_SUB && ops[0].aff_s >= 0 && ops[1].slot == -2)
                        { res.aff_s = ops[0].aff_s; res.aff_c = ops[0].aff_c - ops[1].cst; }

                    // ★★★★★ **길이 사실도 아핀을 따라간다** (2026-08-17).
                    //   `qsort` 가 `if gt hi0 (len s) . do return . end` 로 `hi0 ≤ len s` 를
                    //   세워 두는데, 그 뒤 색인은 언제나 `index s (sub hi 1)` 이다. 값이
                    //   **뺄셈을 지나면서** 사실이 죽어 `bench_sort` 는 계약을 붙여도 **0/22**
                    //   였다(2026-07-29 부터 열린 항목).
                    //   ⇒ 값이 `loc[k] + c` 이고 `loc[k] + m ≤ len(s)` 이면
                    //     **그 값 + (m − c) ≤ len(s)** 다. c 가 음수면 여백이 **늘어난다** —
                    //     `hi − 1` 이 정확히 그 경우이고, 여백 0 이 1 이 되어 색인이 안전해진다.
                    //   ★ 건전성: 위 조건들이 이미 트랩 연산·정수·비음수를 보장한다. 여백이
                    //     음수가 되면 싣지 않는다(`iv_put_lenlt` 와 같은 규율).
                    if (res.aff_s >= 0 && (proven_size_t)res.aff_s < IR_MAXLOCALS) {
                        proven_i32 ls = st->lenlt[res.aff_s];
                        if (ls >= 0) {
                            proven_i64 m = iv_margin(st, (proven_size_t)res.aff_s, ls) - res.aff_c;
                            if (m >= 0) {
                                res.lenlt = ls;
                                res.lenstr = 0;          // 여백으로만 말한다(엄격은 여백 +1 이다)
                                res.lenoff = m;
                            }
                        }
                    }
                }
                if (!(in->a & IR_TY_KNOWN) || (in->a & IR_TY_FLT)) break;
                ityp_t t = { true, (proven_u8)(in->a & 0xff), (in->a & IR_TY_SIGNED) != 0, false, false, 0, 0 };
                if (mark) (*total)++;
                iv_t r;
                bool ok = in->w == IRW_ADD ? iv_add(ops[0].v, ops[1].v, &r)
                        : in->w == IRW_SUB ? iv_sub(ops[0].v, ops[1].v, &r)
                        :                    iv_mul(ops[0].v, ops[1].v, &r);
                // ★★ R5 — **차분 제약을 여기서 쓴다.** `sub x y` 에서 y ≤ x 를 알면 x − y ≥ 0.
                //   구간만 보면 x,y ∈ [0,255] 이므로 x − y ∈ [−255,255] 다 → u8 언더플로 검사가
                //   **절대** 안 지워졌다. `requires le a b .` 를 써도 소용없었다: 그 사실이
                //   **어디에서도 학습되지 않았기 때문이다**(상수 범위가 아니면 통째로 버려졌다).
                // ★★★ **무부호 뺄셈은 구간이 필요 없다** (2026-08-17, PERF-0004 후속).
                //   `y ≤ x` 를 알고 타입이 무부호면 `x − y ∈ [0, x] ⊆ [0, MAX]` 다 — **x 가
                //   그 타입의 값이라는 사실만으로** 결론이 난다. 구간을 볼 필요가 없다.
                //   ☞ 이것이 `qsort` 의 뺄셈 일곱을 막고 있었다: 두 피연산자가 상계 없는 u64 라
                //     `iv_t.wide`(부록 C)가 켜져 `iv_in` 이 64비트 결론을 거부했다. 거부는
                //     **hi 를 잘라 왔기 때문**인데, 여기서 쓰는 근거는 hi 가 아니라 **차분**이다.
                //     *같은 보수성이 다른 근거를 막고 있었다.*
                //   ★ 부호형에는 안 쓴다(`x − y` 가 MIN 을 넘을 수 있다).
                //   ★★ 그리고 **감기는 뺄셈은 여기 오면 안 된다**: `_wrap`/`_sat` 은 언더플로가
                //     **정의된 동작**이라 지울 검사가 없고, PROVEN 을 박으면 뒤끝이 **평범한
                //     뺄셈**을 낸다 — 차등 스윕이 `sat_sub b a` 에서 VM 0 / 네이티브 −7 로 잡았다.
                bool diff_rel = false, diff_int = false;
                bool sub_trap = (in->w == IRW_SUB && !t.sign &&
                                 !(in->a & (IR_POL_WRAP | IR_POL_SAT | IR_POL_CHK)));
                if (sub_trap &&
                    ops[0].slot >= 0 && (proven_size_t)ops[0].slot < IR_MAXLOCALS) {
                    proven_i32 y = -1; proven_i64 yoff = 0;
                    if (ops[1].slot >= 0 && (proven_size_t)ops[1].slot < IR_MAXLOCALS) {
                        y = ops[1].slot; yoff = 0;
                    } else if (ops[1].aff_s >= 0 && ops[1].aff_c >= 0 &&
                               (proven_size_t)ops[1].aff_s < IR_MAXLOCALS) {
                        y = ops[1].aff_s; yoff = ops[1].aff_c;
                    }
                    if (y >= 0 && iv_leknown(st, (proven_size_t)y, ops[0].slot) &&
                        iv_lemargin(st, (proven_size_t)y, ops[0].slot) >= yoff)
                        diff_rel = true;
                }
                // ★ 그리고 **구간으로 언더플로가 없다고 보이는 경우**도 같은 결론이다:
                //   `x.lo ≥ y.hi` 면 `x − y ≥ 0` 이고, 위 논리로 상계는 볼 필요가 없다.
                //   `sub hi 1`(우변이 **상수**)이 정확히 이 모양이고, `qsort` 안에 넷 있었다.
                //   ☞ lo 쪽은 `wide` 와 무관하다 — 자른 것은 hi 다.
                if (sub_trap && ops[0].v.lo >= ops[1].v.hi)
                    diff_int = true;
                // ★★★ **덧셈의 짝** (2026-08-17). `x + c ≤ p` 이고 `p` 가 그 타입의 값이면
                //   `x + c` 는 넘칠 수 없다 — 무부호 뺄셈과 **같은 논거**(상한을 볼 필요가 없다).
                //   ☞ `qsort` 의 `add i 1` 다섯이 이 모양이다: 앞줄 `sub hi (add i 1)` 이
                //     `i + 1 ≤ hi` 를 증언한다.
                //   ★ 폭을 64 로 묶는다: 더 좁은 타입에서는 `p` 가 그 타입의 값이라는 보장이
                //     없다(u8 덧셈인데 p 가 u64 지역이면 255 로 못 묶는다).
                //   ★ 그리고 `p` 가 음수가 아니어야 한다 — 부호 지역과의 관계를 무부호 덧셈에
                //     쓰면 뜻이 갈린다.
                // ★★★★★ **곱의 상계는 계약이 이미 갖고 있다** (2026-08-18).
                //   `matmul` 의 안쪽 루프가 `mul i n` 을 매 바퀴 **오버플로 검사와 함께** 돈다
                //   (명령 수 축이 잰 ×3.21 의 큰 몫이 이것이다). 그런데 계약이
                //   `requires ge (len a) (mul n n)` 이라고 이미 말했다:
                //       x ≤ p · y ≤ q · p·q ≤ len s   ⇒   x·y ≤ len s ≤ MAX
                //   ★ 곱을 **계산하지 않는다** — 부록 F.5 의 술어와 같은 논거다.
                //   ★★ `len s` 는 size_t 이므로 u64 안이다. 그래서 폭을 64 로 묶는다
                //     (더 좁은 타입에서는 `len s` 가 그 타입의 값이라는 보장이 없다).
                bool mul_cap = false;
                if (in->w == IRW_MUL && !t.sign && t.bits == 64 &&
                    !(in->a & (IR_POL_WRAP | IR_POL_SAT | IR_POL_CHK)) &&
                    ops[0].slot >= 0 && ops[1].slot >= 0 &&
                    (proven_size_t)ops[0].slot < IR_MAXLOCALS &&
                    (proven_size_t)ops[1].slot < IR_MAXLOCALS) {
                    proven_i32 x = ops[0].slot, y = ops[1].slot;
                    for (proven_size_t sl = 0; sl < IR_MAXLOCALS && !mul_cap; sl++) {
                        proven_i32 pp = st->lenge_p[sl], qq = st->lenge_q[sl];
                        if (pp < 0 || qq < 0) continue;
                        // `x ≤ pp` 는 **같은 슬롯**이거나 관계가 서 있을 때다(여백 ≥ 0).
                        #define IV_LE_OR_SAME(a, b) ((a) == (b) || iv_leknown(st, (proven_size_t)(a), (b)))
                        if ((IV_LE_OR_SAME(x, pp) && IV_LE_OR_SAME(y, qq)) ||
                            (IV_LE_OR_SAME(x, qq) && IV_LE_OR_SAME(y, pp)))
                            mul_cap = true;
                        #undef IV_LE_OR_SAME
                    }
                }
                // ★★★★★ **행우선 색인의 상계도 계약이 갖고 있다** (2026-08-18).
                //   `matmul` 의 `add (mul i n) k` 는 색인 **경계**가 이미 증명된다
                //   (`iv_row_major_ok`: `i<p ∧ k<q ∧ p·q ≤ len s` ⟹ `i·q+k < len s`).
                //   그 사실은 **오버플로**도 함께 말한다: `i·q+k < len s ≤ MAX`.
                //   ⇒ 같은 판정을 **산술 검사에도** 쓴다. 새 사실이 아니라 **이미 있는 사실의
                //     두 번째 쓰임**이다.
                //   ★ 왜 중요한가(실측): 검사가 **하나라도 남으면** GCC 가 그 루프를 통째로
                //     다르게 굴린다 — matmul 안쪽 루프에서 전부 raw 면 5.08 ms(C 5.03), 하나라도
                //     섞이면 13.8~23.3 ms 다. **부분 증명은 계곡이고, 목적지는 전부다.**
                bool row_cap = false;
                if (in->w == IRW_ADD && !t.sign && t.bits == 64 &&
                    !(in->a & (IR_POL_WRAP | IR_POL_SAT | 0x40000)) &&
                    res.row_i >= 0 && res.row_q >= 0 && res.row_k >= 0)
                    for (proven_size_t sl = 0; sl < IR_MAXLOCALS && !row_cap; sl++)
                        if (st->lenge_p[sl] >= 0 && st->lenge_q[sl] >= 0 &&
                            iv_row_major_ok(st, &res, (proven_i32)sl))
                            row_cap = true;
                bool add_rel = false, add_len = false;
                if (in->w == IRW_ADD && !t.sign && t.bits == 64 &&
                    !(in->a & (IR_POL_WRAP | IR_POL_SAT | IR_POL_CHK)) &&
                    ops[0].slot >= 0 && (proven_size_t)ops[0].slot < IR_MAXLOCALS &&
                    ops[1].v.lo == ops[1].v.hi && ops[1].v.lo >= 0) {
                    proven_i32 pp = st->lerel[ops[0].slot];
                    if (pp >= 0 && (proven_size_t)pp < IR_MAXLOCALS &&
                        st->loc[pp].lo >= 0 &&
                        iv_leknown(st, (proven_size_t)ops[0].slot, pp) &&
                        iv_lemargin(st, (proven_size_t)ops[0].slot, pp) >= ops[1].v.lo)
                        add_rel = true;
                    // ★★★ **길이 사실도 같은 논거를 준다** (2026-08-17). `x + c ≤ len s` 이고
                    //   `len s` 는 **언제나 그 타입의 값**이다(슬라이스 길이는 size_t) ⇒ 넘칠 수 없다.
                    //   ☞ 코퍼스에 남은 검사를 op 별로 세 보니 `add` 가 **1798** 로 압도적이었고,
                    //     그중 대부분의 상계는 지역이 아니라 **`len s`** 다(`i < len s` 뒤의
                    //     `add i 1`). 관계 쪽만 보던 규칙이 그 절반을 못 보고 있었다.
                    //     *무엇이 남았는지 세어 보면 다음 규칙이 스스로 드러난다.*
                    if (!add_rel) {
                        add_len = true;
                        proven_i32 ls = st->lenlt[ops[0].slot];
                        if (ls >= 0 && (proven_size_t)ls < IR_MAXLOCALS &&
                            iv_margin(st, (proven_size_t)ops[0].slot, ls) >= ops[1].v.lo)
                            add_rel = true;
                        else add_len = false;
                    }
                }
                // ★ 그리고 **차분 출처를 싣는다** — 이 값이 `l − r` 임을 술어가 알아야 한다.
                if (sub_trap && ops[0].slot >= 0 && ops[1].slot >= 0 &&
                    ops[0].slot != ops[1].slot) {
                    res.dif_l = ops[0].slot; res.dif_r = ops[1].slot;
                }
                if (ok && (diff_rel || diff_int)) { if (r.lo < 0) r.lo = 0; }
                if (ok && in->w == IRW_SUB &&
                    ops[0].slot >= 0 && (proven_size_t)ops[0].slot < IR_MAXLOCALS &&
                    ops[1].slot >= 0 && (proven_size_t)ops[1].slot < IR_MAXLOCALS &&
                    st->lerel[ops[1].slot] == ops[0].slot) {
                    proven_i64 lo = st->lestr[ops[1].slot] ? 1 : 0;
                    if (r.lo < lo) r.lo = lo;
                }
                // ★★★★★ **나머지 항등식** (2026-08-19) — `x − (x/c)·c = x mod c ∈ [0, c−1]`.
                //   무부호에서 `(x/c)·c ≤ x` 이므로 이 뺄셈은 **밑돌 수 없다**. 구간만으로는
                //   못 본다(lo(x)=0 이고 hi((x/c)·c)=hi(x) 라 음수로 보인다) — **출처**가 근거다.
                //   ☞ 그리고 결과를 [0, c−1] 로 좁힌다: 그것이 뒤따르는 `key·100` 을 닫는다
                //     (`bench_lru` 의 사슬이 이 둘로 끝난다).
                bool modid = false;
                if (in->w == IRW_SUB && !t.sign &&
                    !(in->a & (IR_POL_WRAP | IR_POL_SAT | 0x40000)) &&
                    ops[0].slot >= 0 && ops[1].dmul_s == ops[0].slot && ops[1].dmul_c > 0) {
                    modid = true;
                    r.lo = 0;
                    if (r.hi > ops[1].dmul_c - 1) { r.hi = ops[1].dmul_c - 1; r.wide = 0; }
                }
                if (diff_int || diff_rel || add_rel || mul_cap || row_cap || modid || (ok && iv_in(r, t))) {
                    if (mark) {
                        in->a |= IR_POL_PROVEN; (*proven)++;
                        // R-ARITH-RANGE: 두 피연산자 구간을 이 연산으로 합친 결과가
                        //   **선언 타입 범위 안**이다 ⇒ 넘칠 수 없다(`radd_no_check`, Qed).
                        //   ★ 검증기가 **산술을 다시 한다** — lo/hi 를 직접 계산해 견준다.
                        //     `sub` 의 차분 제약으로 lo 를 끌어올린 경우는 재도출이 불가능하므로
                        //     규칙 이름을 갈라 적는다(R-SUB-DIFF) — 그래야 검증기가 거짓말을 안 한다.
                        // ★★★ **근거가 다르면 이름을 갈라야 한다** (2026-08-17, 검증기가 잡았다).
                        //   무부호 뺄셈의 새 근거는 *"x − y ≤ x ≤ MAX"* 라 **상한 산술을 안 본다.**
                        //   그런데 `R-SUB-DIFF` 의 검증기는 *상한을 다시 계산*한다 — 같은 이름에
                        //   다른 근거를 실었더니 vm_regex 두 자리에서 **즉시 거짓이 됐다**
                        //   (`상한 2^64−1 이 타입 상한을 넘는다`). 검증기가 옳았다.
                        //   ⇒ 셋으로 가른다: 구간이 언더플로를 보이면 **완전 검증 가능**,
                        //     관계 사실에 기대면 하한만 미검증, 옛 경로는 그대로.
                        bool diff_used = (in->w == IRW_SUB &&
                            ops[0].slot >= 0 && (proven_size_t)ops[0].slot < IR_MAXLOCALS &&
                            ops[1].slot >= 0 && (proven_size_t)ops[1].slot < IR_MAXLOCALS &&
                            st->lerel[ops[1].slot] == ops[0].slot);
                        const char *rule = diff_int ? "R-SUB-NOUNDER"
                                         : diff_rel ? "R-SUB-DIFF-REL"
                                         : add_len  ? "R-ADD-LENLT"
                                         : add_rel  ? "R-ADD-LEREL"
                                         : mul_cap  ? "R-MUL-CAP"
                                         : row_cap  ? "R-ROW-CAP"
                                         : modid    ? "R-MOD-IDENT"
                                         : diff_used ? "R-SUB-DIFF" : "R-ARITH-RANGE";
                        cert_put(d, i, in->w, rule, 6,
                                 (proven_i64)ops[0].v.lo, (proven_i64)ops[0].v.hi,
                                 (proven_i64)ops[1].v.lo, (proven_i64)ops[1].v.hi,
                                 (proven_i64)t.bits, (proven_i64)(t.sign ? 1 : 0));
                    }
                    res.v = (ok && iv_in(r, t)) ? r : iv_ty(t);   // 상한을 못 담으면 모른다고 둔다
                } else {
                    res.v = iv_ty(t);
                    // ★★★ **마지막 수단으로 SMT** — 특수 경우(부록 E·F)가 못 넘은 자리만 온다.
                    //   색인에 걸려 있던 것과 **같은 사실 수집기**를 쓴다(두 벌이면 갈린다).
                    if (mark && g_smt_on && !t.sign &&
                        (in->w == IRW_ADD || in->w == IRW_SUB) &&
                        !(in->a & (IR_POL_WRAP | IR_POL_SAT | IR_POL_CHK))) {
                        low_smt_sys_t sy; proven_i64 lm[LOW_SMT_MAXC]; ivs_map_t mp;
                        bool bc = (ops[1].slot < 0 && ops[1].v.lo == ops[1].v.hi);
                        if (iv_smt_arith_ok(st, in->w, t, ops[0].slot,
                                            bc ? -1 : ops[1].slot, bc, ops[1].v.lo,
                                            &sy, lm, &mp) &&
                            cert_put_smt(d, i, in->w, &sy, lm)) {
                            in->a |= IR_POL_PROVEN; (*proven)++;
                            smt_emit_query(d, i, &sy);
                        }
                    }
                }
                // ★★★ **안 터진 뺄셈은 증언이다** (2026-08-17, PERF-0004).
                //   무부호 트랩 뺄셈 `sub x y` 가 이 자리를 **지나갔다면** 그 순간 `y ≤ x` 였다 —
                //   아니었으면 트랩했을 것이다. 지워진 경우(PROVEN)도 마찬가지다: 지운 근거가
                //   곧 `y ≤ x` 다. 그러니 **어느 쪽이든** 그 뒤로는 사실이다.
                //   ☞ 지금까지 이 방향이 **비어 있었다**: 차분 제약을 *쓰기만* 하고
                //     *배우지는* 않았다. 그래서 `qsort` 가 막혔다 — `set hi i` 앞줄의
                //     `sub hi (add i 1)` 이 `i < hi` 를 이미 증언하고 있었는데 아무도 안 들었다.
                //   ★ 우변이 임시값이면 **아핀 출처**로 받는다(`sub hi (add i 1)` ⇒ `i + 1 ≤ hi`).
                if (in->w == IRW_SUB && !t.sign &&
                    !(in->a & (IR_POL_WRAP | IR_POL_SAT | 0x40000)) &&
                    ops[0].slot >= 0 && (proven_size_t)ops[0].slot < IR_MAXLOCALS) {
                    proven_i32 k = -1; proven_i64 koff = 0;
                    if (ops[1].slot >= 0 && (proven_size_t)ops[1].slot < IR_MAXLOCALS) {
                        k = ops[1].slot; koff = 0;
                    } else if (ops[1].aff_s >= 0 && ops[1].aff_c >= 0 &&
                               (proven_size_t)ops[1].aff_s < IR_MAXLOCALS) {
                        k = ops[1].aff_s; koff = ops[1].aff_c;
                    }
                    if (k >= 0 && k != ops[0].slot) {
                        iv_put_lerel(st, (proven_size_t)k, ops[0].slot, false, koff);
                        // ★★ 그리고 **길이 사실까지 곧바로 내려 쓴다**. 합성(`k ≤ x ≤ len s`)은
                        //   읽는 자리에서는 되는데, `set hi i` 같은 **복사 저장**은 *실려 있는*
                        //   사실만 나른다 — 그래서 루프를 한 바퀴 돌면 사라졌다.
                        //   여기서 재료가 다 있을 때 실어 두면 복사가 그것을 물려받는다.
                        proven_i32 sl2 = st->lenlt[ops[0].slot];
                        if (sl2 >= 0) {
                            proven_i64 m = iv_margin(st, (proven_size_t)ops[0].slot, sl2);
                            iv_put_lenlt(st, (proven_size_t)k, sl2, false, m + koff);
                        }
                    }
                }
                break;
            }
            default: break;
        }
        // ★★★★ **넘치면 조용히 안 밀어 넣는 것이 아니라, 분석을 포기한다** (RFC-0077 P1-5 감사,
        //   2026-08-14). 이 스택은 **검사를 지우는** 분석(구간)의 것이다. 안 밀어 넣으면 뒤의
        //   읽기가 **남의 값**을 보고, 그 어긋난 값 하나가 *"지워도 되는 검사"* 판정을 뒤집을 수
        //   있다 — 조용히, 그리고 나쁜 쪽으로. ⇒ 넘치면 **모르는 것으로 물러난다**(검사는 남는다).
        //   그리고 **센다**: 조용한 후퇴는 후퇴가 아니라 사라짐이다.
        if (sp < 64) stk[sp++] = res;
        else { g_iv_stack_full++; return bend; }
    }
    if (i < bend && d->code[i].w == IRW_BRZ && sp) *pred_out = stk[sp - 1];
    return i;
}

// ★★★★★ **귀납 변수 — 루프를 벗어난 뒤에도 아는 것을 안다** (2026-08-17, PERF-0004).
//
//   ☞ 짓기 전에 **비어 있는 칸이 이것 하나임을 쟀다**: 종료값을 사람이 엄격 가드로 주면
//     (`if ge i hi . do return 0 . end`) 중첩 루프 + 루프 재대입(`set hi i`) 이 있는 프로그램의
//     색인 검사가 **둘 다 사라진다**. 나머지 기계는 이미 다 있었다.
//
//   알아보는 모양은 **하나**다 — 실제 코드의 관용구:
//       var v be u64 0 .              ← 진입값 상수 0
//       while lt v X . do … set v (add v c) . end     ← 몸통에서 대입이 **하나**뿐, 걸음 c ≥ 1
//   이때 그 가드가 **거짓인 가지**(루프를 벗어난 자리)에서 **v ≤ X + c − 1** 이다.
//
//   **건전성** — 벗어나는 순간 `v ≥ X` 이고, `v` 가 온 곳은 둘뿐이다:
//     · 한 번도 안 돌았다 ⇒ `v = 0`, 그리고 `0 ≤ X + c − 1` (u64 라 X ≥ 0, c ≥ 1)
//     · 돌았다 ⇒ 마지막 몸통이 `v_prev < X` 에서 돌았고 `v = v_prev + c ≤ X − 1 + c`
//   두 경우 모두 성립한다. ∎
//
//   ★ **셋 중 하나라도 못 세우면 아무 말도 안 한다.** 종료 상계를 틀리면 곧 범위 밖 접근이다.
//     그래서 `mut_ref`(다른 창으로 바뀔 수 있다)가 걸린 슬롯은 통째로 제외한다.
//   ★★ 진입값을 **0 으로 좁혔다** — 일반 `k` 는 `k ≤ X + c − 1` 을 따로 증명해야 하고,
//     실제 관용구는 거의 전부 `var i be u64 0` 이다. *안 나오는 것을 위해 규칙을 늘리지 않는다.*
typedef struct {
    proven_i64 step[IR_MAXLOCALS];     // 0 = 귀납 변수가 아니다
    proven_i32 from[IR_MAXLOCALS];     // 진입값이 지역이면 그 슬롯(-1 = 상수 0)
    proven_size_t stepat[IR_MAXLOCALS];// 걸음 STORE 의 위치 — 어느 루프 안인지 묻는 데 쓴다
    proven_i64 cmax[IR_MAXLOCALS];     // 0 = 모름. >0 이면 이 지역은 **def 전체에서** ≤ cmax
} iv_indvar_t;

static void iv_find_indvars(const low_ir_def_t *d, iv_indvar_t *iv) {
    proven_i32 nstore[IR_MAXLOCALS];
    bool init0[IR_MAXLOCALS], stepok[IR_MAXLOCALS], tainted[IR_MAXLOCALS];
    for (proven_size_t k = 0; k < IR_MAXLOCALS; k++) {
        iv->step[k] = 0; iv->from[k] = -1; iv->stepat[k] = 0; iv->cmax[k] = 0; nstore[k] = 0;
        init0[k] = false; stepok[k] = false; tainted[k] = false;
    }
    for (proven_size_t i = 0; i < d->ncode; i++) {
        const low_ir_ins_t *in = &d->code[i];
        // ★ 다른 창으로 바뀔 수 있으면 귀납 변수가 아니다(SPEC-004 §4.4a 의 그 창들).
        if ((in->w == IRW_REF || in->w == IRW_MREF) &&
            (proven_size_t)in->a < IR_MAXLOCALS) tainted[in->a] = true;
        if (in->w != IRW_STORE || (proven_size_t)in->a >= IR_MAXLOCALS) continue;
        proven_size_t v = (proven_size_t)in->a;
        nstore[v]++;
        // 모양 ①ㄱ 초기화:  CONST 0 · STORE v
        if (i >= 1 && d->code[i-1].w == IRW_CONST && d->code[i-1].a == 0) {
            if (init0[v]) { tainted[v] = true; continue; }
            init0[v] = true; iv->from[v] = -1; continue;
        }
        // 모양 ①ㄴ 초기화:  LOAD w · STORE v   — `var j be u64 lo .` 의 모양.
        //   ★ 이때는 `w ≤ X + c − 1` 을 **쓰는 자리에서** 따로 확인한다(여기서는 못 한다).
        //     `qsort` 가 정확히 이 모양이라(`var j be u64 lo`) 상수 0 만 받으면 못 닿는다.
        if (i >= 1 && d->code[i-1].w == IRW_LOAD &&
            (proven_size_t)d->code[i-1].a < IR_MAXLOCALS &&
            (proven_size_t)d->code[i-1].a != v) {
            if (init0[v]) { tainted[v] = true; continue; }
            init0[v] = true; iv->from[v] = (proven_i32)d->code[i-1].a; continue;
        }
        // 모양 ② 걸음:  LOAD v · CONST c · ADD · STORE v   (트랩 덧셈만)
        if (i >= 3 && d->code[i-1].w == IRW_ADD &&
            !(d->code[i-1].a & (IR_POL_WRAP | IR_POL_SAT | 0x40000)) &&
            d->code[i-2].w == IRW_CONST && d->code[i-2].a >= 1 &&
            d->code[i-3].w == IRW_LOAD && (proven_size_t)d->code[i-3].a == v) {
            if (stepok[v]) { tainted[v] = true; continue; }   // 걸음이 둘이면 모르는 것이다
            stepok[v] = true; iv->step[v] = d->code[i-2].a; iv->stepat[v] = i; continue;
        }
        tainted[v] = true;   // 그 밖의 대입 — 귀납 변수가 아니다
    }
    for (proven_size_t k = 0; k < IR_MAXLOCALS; k++)
        if (tainted[k] || !init0[k] || !stepok[k] || nstore[k] != 2) iv->step[k] = 0;
}

// ★★★★★ **세는 변수는 루프가 도는 횟수만큼만 큰다** (2026-08-19).
//
//   실측이 이 규칙을 지목했다. `sieve` 의 계약 판에서 남은 검사 넷 중 **`add cnt 1` 하나가
//   격차 전부**였다(감싸면 ×1.46 → ×0.99). `lru` 도 `add total_hits 1` 이 절반이었다
//   (×1.76 → ×1.54). 나머지 셋(`mul i i` 등)은 **값이 0** 이었다 — 재고 나서 골랐다.
//
//   왜 구간이 이것을 못 보나: `cnt` 는 루프 머리에서 **위드닝**돼 [0, MAX] 가 된다. 가드가
//   좁혀 주는 것은 **가드 변수**(`k`)뿐이고 `cnt` 는 가드에 안 나온다. 그런데 사람은 안다 —
//   *한 바퀴에 한 번 는다면 바퀴 수만큼만 큰다.*
//
//   ⇒ **루프의 반복 상계**를 세고, 그 루프 안에서 도는 카운터에 곱한다.
//     · 루프 = **뒤로 가는 분기**(`BR h`, h ≤ 자기 위치). 몸통 = [h, b].
//     · 가드는 `while` 이 내는 **정규형** 하나만 본다:
//         h: LOAD v · (LOAD X | CONST X) · LT/LE · BRZ end
//       그리고 `v` 는 걸음 ≥ 1 인 귀납 변수여야 한다.
//     · 반복 ≤ ⌈(hi(X) + (LE?1:0) − lo(v 진입)) / 걸음⌉.
//     · 카운터 `c`(진입 0 · 걸음 s ≥ 1 · 대입 정확히 둘)의 걸음 STORE 를 감싸는 **모든** 루프의
//       반복 수를 곱한다 ⇒ `c ≤ s · Π반복`.
//
//   ★★★ **건전성의 핵심은 "모든"이다.** 감싸는 뒤로가기 분기 중 **하나라도** 정규형이 아니면
//     아무 말도 안 한다. 하나를 놓치면 그 루프의 반복 수만큼 곱이 모자라고, 모자란 상계는
//     보수적인 게 아니라 **틀린 것**이다 — 그리고 틀린 상계로 지운 검사는 조용한 오버플로다.
//   ★★ 곱은 **포화**시킨다. 넘치면 상계가 아니라 쓰레기다 ⇒ 모른다고 말한다.
//   ☞ `break` 는 반복을 줄이기만 하고, `continue` 는 걸음을 건너뛸 뿐이다 — 둘 다 상계를
//     깨지 않는다. 걸음 STORE 가 `if` 안에 있어도 마찬가지다(한 바퀴에 **많아야** 한 번).
// ★★★★★ **루프 표는 하나여야 한다** (2026-09-11, RFC-0111 §8-18 · WO-0197).
//   세는 변수의 상계(`iv_counter_bounds`)와 **원소의 상계**(`iv_elem_bounds`)는 같은 논증을
//   쓴다: *"이 루프는 최대 T 바퀴 돈다."* 표를 둘로 만들면 두 답이 갈린다(교훈 7).
//   ⇒ 여기 한 번 채우고 둘 다 읽는다.
static void iv_counter_bounds(const low_ir_def_t *d, iv_indvar_t *iv, const ivstate_t *e0) {
    iv_loop_t *lp = g_lp;
    proven_size_t nlp = 0;
    for (proven_size_t i = 0; i < d->ncode && nlp < 32; i++) {
        if (d->code[i].w != IRW_BR || d->code[i].a < 0) continue;
        proven_size_t h = (proven_size_t)d->code[i].a;
        if (h > i) continue;                       // 앞으로 가는 분기는 루프가 아니다
        lp[nlp].h = h; lp[nlp].b = i; lp[nlp].trips = 0; lp[nlp].ok = false;
        // 정규형 가드인가
        if (h + 3 < d->ncode &&
            d->code[h].w == IRW_LOAD && (proven_size_t)d->code[h].a < IR_MAXLOCALS &&
            (d->code[h+1].w == IRW_LOAD || d->code[h+1].w == IRW_CONST) &&
            (d->code[h+2].w == IRW_LT || d->code[h+2].w == IRW_LE) &&
            d->code[h+3].w == IRW_BRZ) {
            proven_size_t v = (proven_size_t)d->code[h].a;
            proven_i64 hiX = INT64_MAX;
            if (d->code[h+1].w == IRW_CONST) hiX = d->code[h+1].a;
            else if ((proven_size_t)d->code[h+1].a < IR_MAXLOCALS) {
                proven_size_t X = (proven_size_t)d->code[h+1].a;
                hiX = e0->loc[X].hi;
                // ★★ **`let n be u64 len s .` 은 계약의 길이 상계를 물려받는다.**
                //   진입 구간(`e0->loc`)은 파라미터만 안다. 그런데 실제 코드는 길이를 지역에
                //   담아 놓고 그것으로 돈다(`sieve` 가 정확히 그 모양이라 처음엔 안 걸렸다).
                //   ⇒ X 의 대입이 **오직 하나**이고 그것이 `len s` 면 hi 를 그대로 물려받는다.
                //   ★ 대입이 둘 이상이면 아무 말도 안 한다 — 뒤에서 다른 값이 들어올 수 있다.
                proven_i32 nx = 0; proven_i64 lenhi = INT64_MAX;
                for (proven_size_t q = 0; q < d->ncode; q++) {
                    if (d->code[q].w != IRW_STORE || (proven_size_t)d->code[q].a != X) continue;
                    nx++;
                    if (q >= 2 && d->code[q-1].w == IRW_LEN && d->code[q-2].w == IRW_LOAD &&
                        (proven_size_t)d->code[q-2].a < IR_MAXLOCALS)
                        lenhi = e0->lenv[d->code[q-2].a].hi;
                }
                if (nx == 1 && lenhi < hiX) hiX = lenhi;
            }
            // ★★ 가드 변수는 **귀납 변수 표에 없어도 된다.** 그 표는 진입값이 상수 0 인 것만
            //   받는데(그 규칙이 사는 이유는 따로 있다), `var k be u64 2 .` 같은 흔한 모양이
            //   거기서 빠진다 — 실제로 `sieve` 의 세는 루프가 그것이라 처음엔 아무것도 안 걸렸다.
            //   ⇒ 반복 상계에는 **시작값이 필요 없다**: 무부호라 v ≥ 0 이고 한 바퀴에 ≥1 늘며
            //     `v < X` 인 동안만 도므로 **반복 ≤ hi(X) + 1** 이다. 시작이 2 든 0 이든 상관없다.
            //   ★ 대신 몸통에서 v 가 **오직 그 한 번만** 바뀌는지 본다. 다른 대입이 하나라도
            //     있으면 v 가 되돌 수 있고, 그러면 이 논증이 통째로 깨진다.
            proven_i32 nst = 0; bool inc = false;
            for (proven_size_t q = h; q <= i && q < d->ncode; q++) {
                if (d->code[q].w != IRW_STORE || (proven_size_t)d->code[q].a != v) continue;
                nst++;
                if (q >= 3 && d->code[q-1].w == IRW_ADD &&
                    !(d->code[q-1].a & (IR_POL_WRAP | IR_POL_SAT | 0x40000)) &&
                    d->code[q-2].w == IRW_CONST && d->code[q-2].a >= 1 &&
                    d->code[q-3].w == IRW_LOAD && (proven_size_t)d->code[q-3].a == v)
                    inc = true;
            }
            if (nst == 1 && inc && hiX >= 0 && hiX < (INT64_MAX >> 4)) {
                lp[nlp].trips = hiX + (d->code[h+2].w == IRW_LE ? 1 : 0) + 1;
                lp[nlp].ok = true;
            }
        }
        nlp++;
    }
    g_nlp = nlp;
    for (proven_size_t cslot = 0; cslot < IR_MAXLOCALS; cslot++) {
        if (iv->step[cslot] < 1 || iv->from[cslot] != -1) continue;   // 진입 0 인 카운터만
        proven_size_t at = iv->stepat[cslot];
        if (!at) continue;
        proven_i64 prod = 1; bool ok = true;
        for (proven_size_t q = 0; q < nlp; q++) {
            if (at < lp[q].h || at > lp[q].b) continue;   // 이 걸음을 안 감싼다
            if (!lp[q].ok) { ok = false; break; }         // ★ 하나라도 모르면 아무 말도 안 한다
            if (lp[q].trips <= 0 || prod > (INT64_MAX >> 4) / lp[q].trips) { ok = false; break; }
            prod *= lp[q].trips;
        }
        if (!ok || prod == 1) continue;                   // 루프 밖의 "카운터" 는 여기 일이 아니다
        if (iv->step[cslot] > (INT64_MAX >> 4) / prod) continue;
        iv->cmax[cslot] = iv->step[cslot] * prod;
    }
}

 void ir_interval(ir_ctx_t *c, const low_cst_t *f, low_ir_def_t *d,
                        proven_size_t *proven, proven_size_t *total) {
    proven_size_t n = d->ncode;
    if (!n || n > 4096) return;

    // ★ 귀납 변수 표 — 이 def 을 한 번 훑어 만든다(위 `iv_find_indvars` 의 증명).
    static iv_indvar_t indv;
    iv_find_indvars(d, &indv);

    // 1) 리더(블록 시작) 표시
    static bool leader[4096];
    memset(leader, 0, sizeof(bool) * n);
    leader[0] = true;
    for (proven_size_t i = 0; i < n; i++) {
        low_irw_t w = d->code[i].w;
        if (w == IRW_BR || w == IRW_BRZ) {
            proven_size_t t = (proven_size_t)d->code[i].a;
            if (t < n) leader[t] = true;
            if (i + 1 < n) leader[i + 1] = true;
        } else if (w == IRW_RET && i + 1 < n) leader[i + 1] = true;
    }

    // 2) 진입 상태 — 타입 range + requires (assume 제외; RFC-0053 §6.6)
    static ivstate_t ent[4096];
    static bool seen[4096];
    memset(seen, 0, sizeof(bool) * n);
    ivstate_t e0;
    for (proven_size_t i = 0; i < IR_MAXLOCALS; i++) {
        e0.loc[i] = IV_TOP; e0.lenlt[i] = -1; e0.lenstr[i] = 0; e0.lenoff[i] = 0;
        e0.lenofloc[i] = -1;
        e0.lenge_p[i] = -1; e0.lenge_q[i] = -1;
        e0.slenle_t[i] = -1; e0.slenle_str[i] = 0;
        e0.divofloc_s[i] = -1; e0.divofloc_c[i] = 0;
        e0.prodloc_l[i] = -1; e0.prodloc_r[i] = -1;
        e0.affloc_s[i] = -1; e0.affloc_c[i] = 0;
        // ★★★★★ **길이에는 상한이 있다** (2026-08-27, WO-0131 — 소유자 결정).
        //   `IV_LEN` 의 hi 는 INT64_MAX 였고 그것은 *"음수가 아니다"* 라는 **하한의
        //   자리 표시자**이지 상계가 아니었다(§7148 이 그렇게 경고한다).
        //   ⇒ 상한은 **주소공간에서 나온다**: `machine.max_slice_len`(목표 표).
        //   ★ 이것은 **언어의 약속**이다 — SPEC-004 에 명문화했다. 약속 없이
        //     상계를 쓰는 것이 이 파일이 경고하는 바로 그 잘못이다.
        e0.lenv[i].lo = 0; e0.lenv[i].hi = ir_tgt->max_slice_len; e0.lenv[i].wide = 0;
        e0.lerel[i] = -1; e0.lestr[i] = 0; e0.leoff[i] = 0; e0.vlanesloc[i] = 0;
        e0.elemv[i] = IV_TOP;                    // ★ 원소 구간 — 기본 미지(⊤)
    }
    e0.nfacts = 0;
    for (proven_size_t i = 0; i < c->nlocals && i < IR_MAXLOCALS; i++) e0.loc[i] = iv_ty(c->locals[i].ty);
    iv_apply_requires(c, f, e0.loc);
    iv_apply_rel_requires(c, f, &e0);        // ★ R5: 변수 **사이의** 관계도 사실이다
    iv_apply_len_requires(c, f, e0.lenv);    // ★ 길이의 상·하계 (`requires le (len s) N`)
    iv_close_facts(&e0);               // ★ R5 — 관계 사실을 **수치 상계로 닫는다**(전이)
    // ★ 슬라이스 파라미터의 **원소 타입 범위**를 먼저 시드한다(u8 → [0,255]). 그래야 `elem_lt s N`
    //   하나만 있어도 하한(0)이 타입에서 온다 — index 값이 [0, N-1] 로 묶여 검사가 없어진다.
    for (proven_size_t q = 0; q < d->nparams && q < IR_MAXLOCALS; q++) {
        if (!((d->param_slice >> q) & 1u) || d->param_selem[q]) continue;   // 구조체 슬라이스 제외
        proven_u8 eb = d->param_ebits[q];
        if (eb == 8)  e0.elemv[q] = (iv_t){ 0, 255 };
        else if (eb == 16) e0.elemv[q] = (iv_t){ 0, 65535 };
        else if (eb == 32) e0.elemv[q] = (iv_t){ 0, 4294967295LL };
        else if (eb == 108) e0.elemv[q] = (iv_t){ -128, 127 };   // i8
        // ★ i16/i32/i64(116/132/164)는 구간을 안 박는다 — `index (slice iN)` 이 아직 부호 확장을 안 하고
        //   무부호로 읽으므로(별개 미결), 부호 범위를 가정하면 분석이 불건전해진다. 보수적(IV_TOP) 유지.
    }
    iv_apply_elem_requires(c, f, e0.elemv);
    // ★★★ **세는 변수의 상계** — 계약이 진입 구간을 다 심은 **뒤에** 센다(루프 상계가
    //   가드 우변의 hi 를 읽으므로, `requires le n N` 이 먼저 들어와 있어야 한다).
    iv_counter_bounds(d, &indv, &e0);
    g_cmax = indv.cmax;
    // ★ 계산된 진입 원소 구간을 def 에 되쓴다 — 계약 오라클의 길이 축이 이 구간 안의 내용을
    //   채워야 정직하다(elem 술어를 어기는 바이트로 트랩시키고 프로그램을 고발하지 않도록).
    for (proven_size_t q = 0; q < d->nparams && q < LOW_MAX_PARAMS; q++) {
        if (!((d->param_slice >> q) & 1u) || d->param_selem[q]) continue;
        if (e0.elemv[q].lo > IV_TOP.lo || e0.elemv[q].hi < IV_TOP.hi) {
            d->param_elo[q] = e0.elemv[q].lo;
            d->param_ehi[q] = e0.elemv[q].hi;
            d->param_elem_rng |= 1u << q;
        }
    }
    ent[0] = e0; seen[0] = true;

    // ★★★★★ **원소 상계 — 추측하고 검증한다** (2026-09-11, RFC-0111 §8-18 · WO-0197).
    //   고정점을 **세 번까지** 돈다:
    //     ⓐ 그냥 돈다 — 어느 슬라이스가 **루프 안에서만** 쓰이는지 관찰한다.
    //     ⓑ 후보 상계(진입 원소 상계 + 걸음×바퀴수)를 물리고 다시 돈다.
    //     ⓒ 쓰기가 상계를 넘었으면(검증 실패) 후보를 버리고 ⓐ 의 답으로 돌아간다.
    //   ☞ *추측은 공짜여도 되지만, 검증은 공짜가 아니어야 한다.*
    for (proven_size_t q = 0; q < IR_MAXLOCALS; q++) {
        g_emax[q] = 0; g_ewr_at[q] = 0; g_ewr_out[q] = false;
    }
    g_ecap_on = false; g_ecap_bad = false;
    for (int pass = 0; pass < 3; pass++) {
    if (pass) { memset(seen, 0, sizeof(bool) * n); ent[0] = e0; seen[0] = true; }

    // 3) 고정점 — 뒤로 가는 간선에서 위드닝
    for (int round = 0; round < 24; round++) {
        bool changed = false;
        for (proven_size_t b = 0; b < n; b++) {
            if (!leader[b] || !seen[b]) continue;
            proven_size_t bend = b + 1;
            while (bend < n && !leader[bend]) bend++;
            ivstate_t st = ent[b];
            ivs_t pr;
            proven_size_t term = iv_block(c, d, b, bend, &st, &pr, false, proven, total);

            #define IV_PUSH(TGT, S) do {                                                   \
                proven_size_t _t = (TGT);                                                  \
                if (_t < n) {                                                              \
                    if (!seen[_t]) { ent[_t] = (S); seen[_t] = true; changed = true; }     \
                    else {                                                                 \
                        for (proven_size_t _i = 0; _i < IR_MAXLOCALS; _i++) {              \
                            iv_t _o = ent[_t].loc[_i];                                     \
                            iv_t _j = iv_join2(_o, (S).loc[_i]);                           \
                            iv_t _w = (_t <= b) ? iv_widen(_o, _j) : _j; /* 뒤로 = 위드닝 */ \
                            if (_w.lo != _o.lo || _w.hi != _o.hi) { ent[_t].loc[_i] = _w; changed = true; } \
                            /* ★ 관계 사실은 두 경로가 같을 때만 살아남는다(합류=meet). */  \
                            if (ent[_t].lenlt[_i] != (S).lenlt[_i] && ent[_t].lenlt[_i] != -1) { \
                                ent[_t].lenlt[_i] = -1; ent[_t].lenstr[_i] = 0;            \
                                ent[_t].lenoff[_i] = 0; changed = true;                    \
                            } else if (ent[_t].lenlt[_i] >= 0 &&                           \
                                       ent[_t].lenstr[_i] && !(S).lenstr[_i]) {            \
                                /* ★ 같은 길이 사실인데 한 경로가 **약하다** — 약한 쪽이 참이다. */ \
                                ent[_t].lenstr[_i] = 0; changed = true;                    \
                            }                                                              \
                            /* ★ 여백도 **작은 쪽**이 참이다 — 한 경로에서만 4칸 남으면 4칸이 아니다. */ \
                            if (ent[_t].lenlt[_i] >= 0 &&                                  \
                                ent[_t].lenoff[_i] > (S).lenoff[_i]) {                     \
                                ent[_t].lenoff[_i] = (S).lenoff[_i]; changed = true;       \
                            }                                                              \
                            if (ent[_t].lenofloc[_i] != (S).lenofloc[_i] && ent[_t].lenofloc[_i] != -1) { \
                                ent[_t].lenofloc[_i] = -1; changed = true;                 \
                            }                                                              \
                            /* ★ 출처(곱 · 나눗셈)도 두 경로가 같을 때만 산다 (2026-10-04). */ \
                            if (ent[_t].prodloc_l[_i] != -1 && (ent[_t].prodloc_l[_i] != (S).prodloc_l[_i] || \
                                                                ent[_t].prodloc_r[_i] != (S).prodloc_r[_i])) { \
                                ent[_t].prodloc_l[_i] = -1; ent[_t].prodloc_r[_i] = -1; changed = true; \
                            }                                                              \
                            if (ent[_t].affloc_s[_i] != -1 && (ent[_t].affloc_s[_i] != (S).affloc_s[_i] || \
                                                               ent[_t].affloc_c[_i] != (S).affloc_c[_i])) { \
                                ent[_t].affloc_s[_i] = -1; changed = true;                 \
                            }                                                              \
                            if (ent[_t].divofloc_s[_i] != -1 && (ent[_t].divofloc_s[_i] != (S).divofloc_s[_i] || \
                                                                 ent[_t].divofloc_c[_i] != (S).divofloc_c[_i])) { \
                                ent[_t].divofloc_s[_i] = -1; changed = true;               \
                            }                                                              \
                            /* ★ §8-23 — 슬라이스끼리의 길이 사실도 이제 본문에서 심는다(가드).  \
                               그러니 합류에서 **두 경로가 같을 때만** 산다. 엄격은 둘 다일 때만. */ \
                            if (ent[_t].slenle_t[_i] != (S).slenle_t[_i] && ent[_t].slenle_t[_i] != -1) { \
                                ent[_t].slenle_t[_i] = -1; ent[_t].slenle_str[_i] = 0; changed = true; \
                            } else if (ent[_t].slenle_t[_i] >= 0 &&                        \
                                       ent[_t].slenle_str[_i] && !(S).slenle_str[_i]) {    \
                                ent[_t].slenle_str[_i] = 0; changed = true;                \
                            }                                                              \
                            /* ★ R5: 차분 제약도 마찬가지 — 한 경로에서만 참이면 사실이 아니다. */ \
                            /* ★★★ **구문이 다르다고 사실이 없는 게 아니다** (2026-08-17).      \
                               `var hi be u64 hi0 .` 뒤로 앞 간선은 `lo ≤ hi0` 를, 뒤 간선은     \
                               `lo ≤ hi` 를 들고 온다 — **같은 결론인데 다른 이름**이라 합류가    \
                               통째로 버렸다. 그래서 `qsort` 의 22:`sub hi lo` 가 영원히 안       \
                               지워졌다. ⇒ 이름이 다르면 **상대에게 같은 결론을 묻는다**          \
                               (한 홉 포함). 둘 다 증명하면 **약한 쪽 여백**으로 남긴다. */      \
                            if (ent[_t].lerel[_i] >= 0 &&                                  \
                                ent[_t].lerel[_i] != (S).lerel[_i]) {                      \
                                proven_i64 _sm = iv_lemargin(&(S), _i, ent[_t].lerel[_i]); \
                                proven_i64 _om = ent[_t].leoff[_i] + (ent[_t].lestr[_i] ? 1 : 0); \
                                if (_sm <= 0) { ent[_t].lerel[_i] = -1; ent[_t].lestr[_i] = 0; \
                                                ent[_t].leoff[_i] = 0; changed = true; }   \
                                else if (_sm < _om) { ent[_t].lestr[_i] = 0;               \
                                                      ent[_t].leoff[_i] = _sm; changed = true; } \
                            } else if (ent[_t].lerel[_i] >= 0 &&                           \
                                       ent[_t].lestr[_i] && !(S).lestr[_i]) {              \
                                ent[_t].lestr[_i] = 0; changed = true;                     \
                            }                                                              \
                            /* ★ 레인 수는 두 경로가 같을 때만 산다 — 다르면 모르는 것이다. */ \
                            if (ent[_t].vlanesloc[_i] != (S).vlanesloc[_i] &&              \
                                ent[_t].vlanesloc[_i] != 0) {                              \
                                ent[_t].vlanesloc[_i] = 0; changed = true;                 \
                            }                                                              \
                            /* ★ 차분의 여백도 **작은 쪽**이 참이다. */                    \
                            if (ent[_t].lerel[_i] >= 0 &&                                  \
                                ent[_t].leoff[_i] > (S).leoff[_i]) {                       \
                                ent[_t].leoff[_i] = (S).leoff[_i]; changed = true;         \
                            }                                                              \
                            { iv_t _l0 = ent[_t].lenv[_i];                                 \
                              iv_t _lj = iv_join2(_l0, (S).lenv[_i]);                      \
                              iv_t _lw = (_t <= b) ? iv_widen(_l0, _lj) : _lj;             \
                              if (_lw.lo != _l0.lo || _lw.hi != _l0.hi) {                  \
                                  ent[_t].lenv[_i] = _lw; changed = true; } }              \
                            /* ★ 원소 구간(배열 내용)도 합류에서 join(+뒤로 가는 간선 위드닝). */ \
                            { iv_t _e0 = ent[_t].elemv[_i];                                \
                              iv_t _ej = iv_join2(_e0, (S).elemv[_i]);                     \
                              iv_t _ew = (_t <= b) ? iv_widen(_e0, _ej) : _ej;             \
                              /* ★ 후보 상계로 되좁힌다 — 위드닝은 *모르는 것*이지 *사실*이 아니다. \
                                 이 자름이 사실이 되는 것은 **검증이 통과할 때**뿐이다(ISTORE 쪽). */ \
                              if (g_ecap_on && g_emax[_i] > 0) {                           \
                                  if (_ew.hi > g_emax[_i]) { _ew.hi = g_emax[_i]; _ew.wide = 0; } \
                                  if (_ew.lo < 0) _ew.lo = 0;                              \
                              }                                                            \
                              if (_ew.lo != _e0.lo || _ew.hi != _e0.hi) {                  \
                                  ent[_t].elemv[_i] = _ew; changed = true; } }             \
                        }                                                                  \
                        /* ★ 술어 기억도 두 경로가 **일치할 때만** 살아남는다. */          \
                        { proven_u8 _n = 0;                                                \
                          for (proven_u8 _q = 0; _q < ent[_t].nfacts; _q++) {              \
                              bool _tv;                                                    \
                              if (iv_fact_get(&(S), d, ent[_t].facts[_q].eid,               \
                                              ent[_t].facts[_q].ifrom,                      \
                                              ent[_t].facts[_q].ito, &_tv) &&               \
                                  _tv == ent[_t].facts[_q].truth)                          \
                                  ent[_t].facts[_n++] = ent[_t].facts[_q];                 \
                          }                                                                \
                          if (_n != ent[_t].nfacts) { ent[_t].nfacts = _n; changed = true; } } \
                    }                                                                      \
                }                                                                          \
            } while (0)

            if (term >= bend || term >= n) { IV_PUSH(bend, st); continue; }
            low_ir_ins_t *tm = &d->code[term];
            if (tm->w == IRW_RET) continue;
            if (tm->w == IRW_BR) { IV_PUSH((proven_size_t)tm->a, st); continue; }
            if (tm->w == IRW_BRZ) {
                ivstate_t tk = st, ft = st;                     // ★ 분기 조건 내로잉
                { bool _ok = pr.slot >= 0 && (proven_size_t)pr.slot < IR_MAXLOCALS;
                  proven_i64 _step = _ok ? indv.step[pr.slot] : 0;
                  proven_i32 _from = _ok ? indv.from[pr.slot] : -1;
                  iv_narrow(&tk, pr.pred, pr.slot, pr.rhs, pr.lenlt, pr.rslot, false,
                            pr.aff_s, pr.aff_c, pr.raff_s, pr.raff_c, _step, _from);   // 점프 = 조건 거짓
                  iv_narrow(&ft, pr.pred, pr.slot, pr.rhs, pr.lenlt, pr.rslot, true,
                            pr.aff_s, pr.aff_c, pr.raff_s, pr.raff_c, _step, _from); }  // 낙하 = 조건 참
                iv_narrow_len(&tk, pr.pred, pr.lenpred, pr.rhs, false);      // ★ 슬라이스 길이도
                iv_narrow_len(&ft, pr.pred, pr.lenpred, pr.rhs, true);
                iv_narrow_dif(&tk, pr.pred, pr.dif_l, pr.dif_r, pr.rhs, false);  // ★ 두 지역의 차분도
                iv_narrow_dif(&ft, pr.pred, pr.dif_l, pr.dif_r, pr.rhs, true);
                iv_narrow_prod(&tk, pr.pred, pr.prod_l, pr.prod_r, pr.rhs, pr.rslot, pr.puns, false);  // ★ 곱의 인자도
                iv_narrow_prod(&ft, pr.pred, pr.prod_l, pr.prod_r, pr.rhs, pr.rslot, pr.puns, true);
                // ★ 술어 기억: 이 경로에서 그 **식**이 참인가 거짓인가.
                //   BRZ 는 조건이 거짓일 때 점프한다. pneg 를 풀어서 원래 식의 진리값으로 적는다.
                iv_fact_add(&tk, pr.eid, pr.deps, false != pr.pneg, pr.ifrom, pr.ito);
                iv_fact_add(&ft, pr.eid, pr.deps, true  != pr.pneg, pr.ifrom, pr.ito);
                // ★★★★★ **RFC-0095 S-B** — 본문 사실도 진입 사실과 **같은 폐포**를 지난다.
                //   위의 좁히기들이 이 경로의 관계(`lerel`·`lenlt`)를 막 심었다. 그것을
                //   수치 상계로 닫지 않으면 `guard le j hi` 는 `requires le j hi` 와
                //   **다른 답**을 낸다(그것이 RFC-0095 §2 실측 C 다).
                iv_close_facts(&tk);
                iv_close_facts(&ft);
                IV_PUSH((proven_size_t)tm->a, tk);
                IV_PUSH(term + 1, ft);
                continue;
            }
            IV_PUSH(bend, st);
            #undef IV_PUSH
        }
        if (!changed) break;
    }

    // ── 원소 상계: 관찰 → 추측 → 검증 ─────────────────────────────────────
    if (pass == 0) {
        bool any = false;
        for (proven_size_t s = 0; s < IR_MAXLOCALS; s++) {
            if (!g_ewr_at[s] || g_ewr_out[s]) continue;        // 루프 밖에서도 쓰면 못 묶는다
            if (e0.elemv[s].hi >= IV_TOP.hi || e0.elemv[s].lo < 0) continue;  // 진입 상계가 없으면 밑동이 없다
            proven_size_t at = (proven_size_t)g_ewr_at[s] - 1;
            proven_i64 prod = 1; bool ok = true;
            for (proven_size_t q = 0; q < g_nlp; q++) {
                if (at < g_lp[q].h || at > g_lp[q].b) continue;
                if (!g_lp[q].ok) { ok = false; break; }          // ★ 하나라도 모르면 아무 말도 안 한다
                if (g_lp[q].trips <= 0 || prod > (INT64_MAX >> 8) / g_lp[q].trips) { ok = false; break; }
                prod *= g_lp[q].trips;
            }
            if (!ok || prod <= 1) continue;
            // ★ 걸음은 **1 로 추측한다**(표를 되쓰는 관용구가 거의 전부 «값 + 1» 이다).
            //   틀리면 검증이 잡는다 — 그것이 이 설계의 값이다.
            if (e0.elemv[s].hi > (INT64_MAX >> 4) - prod) continue;
            g_emax[s] = e0.elemv[s].hi + prod;   // 임시 — 아래에서 풀 공통값으로 맞춘다
            any = true;
        }
        if (!any) break;                                        // 후보가 없으면 한 바퀴로 끝
        // ★ 풀 공통 상계로 맞춘다: 밑동 = 진입 최대 · 늘어남 = 쓰기 자리들의 **합**
        //   (최대가 아니라 합이다 — 모든 쓰기가 각자 몫만큼 풀의 최대를 올릴 수 있다).
        {
            proven_i64 base_ = 0, grow_ = 0; bool ok2_ = true;
            for (proven_size_t s = 0; s < IR_MAXLOCALS; s++) {
                if (!g_emax[s]) continue;
                if (e0.elemv[s].hi > base_) base_ = e0.elemv[s].hi;
                proven_i64 g_ = g_emax[s] - e0.elemv[s].hi;
                if (grow_ > (INT64_MAX >> 4) - g_) { ok2_ = false; break; }
                grow_ += g_;
            }
            if (!ok2_ || base_ > (INT64_MAX >> 4) - grow_) {
                for (proven_size_t s = 0; s < IR_MAXLOCALS; s++) g_emax[s] = 0;
                break;
            }
            for (proven_size_t s = 0; s < IR_MAXLOCALS; s++)
                if (g_emax[s]) g_emax[s] = base_ + grow_;
        }
        g_ecap_on = true; g_ecap_bad = false;
        continue;
    }
    if (pass == 1) {
        if (!g_ecap_bad) break;                                 // 검증 통과 — 이 답을 쓴다
        // 검증 실패: 후보를 버리고 한 바퀴 더(상계 없이)
        for (proven_size_t s = 0; s < IR_MAXLOCALS; s++) g_emax[s] = 0;
        g_ecap_on = false;
        continue;
    }
    break;
    }

    // 4) 표시 패스 — 수렴한 진입 상태로 다시 훑으며 증명된 자리에 PROVEN
    for (proven_size_t b = 0; b < n; b++) {
        if (!leader[b] || !seen[b]) continue;
        proven_size_t bend = b + 1;
        while (bend < n && !leader[bend]) bend++;
        ivstate_t st = ent[b];
        ivs_t pr;
        (void)iv_block(c, d, b, bend, &st, &pr, true, proven, total);
    }
}


// ★★★ **하나의 뜻에 두 표현이 있으면 반드시 갈린다**(교훈 7) — 그리고 여기 그 둘이 있다:
//   **피연산자 목록**과 **템플릿의 `{name}`**. 둘 다 "이 asm 이 어떤 값을 만지는가" 를 말한다.
//   대조하지 않으면: 안 묶인 `{x}` 는 어셈블러가 토하거나(운이 좋으면) **틀린 레지스터를 읽고**,
//   안 쓰인 피연산자는 레지스터를 **공연히 태운다**. 그래서 **양방향으로 대조한다**
//   (아래 `ir_asm_tmpl_check`).
//
// ★ 절의 **알맹이**(타깃 이름 + 피연산자)를 읽는다. `asm` 이 op 의 절로 왔든(RFC-0041 D2)
//   몸 안의 **문장**으로 왔든(RFC-0042 D11) 여기 하나가 읽는다 — 두 벌이면 갈린다(교훈 7).
//   반환: false = 진단을 냈다(호출자는 그대로 돌아간다).
/* ir_asm_clause — ir_asm_stmt 와 한 덩어리라 low_ir.c 로 함께 되돌렸다 */


// ★★ 양방향 대조 — 템플릿의 `{name}` ↔ 피연산자. (절이든 문장이든 같은 대조다.)
/* ir_asm_tmpl_check — ir_asm_stmt 와 한 덩어리라 low_ir.c 로 함께 되돌렸다 */
