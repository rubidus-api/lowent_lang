// low_typecheck.c — MVP static type checking (S4): kind-level + integer width/sign.
#include "low_hwm.h"
#include "low_typecheck.h"

#include "low_token.h"
#include "low_diag.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

// TK_WRAPPED = result / option — **풀 수 있는(try 할 수 있는)** 것.
// `try` 는 "실패할 수 있다" 는 선언이다. 풀 것이 없는 값에 붙으면 그 선언은 거짓말이다.
// TK_VEC = `vec <elem> <lanes>` — SIMD 벡터. 레인 수와 원소 종류가 **타입의 일부**다.
//   지금까지 vec 은 TK_NAMED 로 떨어져서, 레인 수가 달라도·원소 종류가 달라도 통과했다.
//   VM 이 런타임에 잡긴 했지만(E-VM-TYPE), **타입 오류를 런타임까지 미루는 것**은
//   이 언어의 철학에 어긋난다. 정적으로 올린다.
// TK_MASK = `mask <lanes>` — 벡터 비교의 결과. bool 이 아니다(레인마다 하나씩이다).
//   지금까지 마스크는 아무 타입도 아니어서, `select` 에 벡터를 넘겨도·`any` 에 정수를
//   넘겨도 통과했다.
typedef enum { TK_UNKNOWN, TK_BOOL, TK_INT, TK_FLOAT, TK_SLICE, TK_NAMED, TK_UNIT,
               TK_WRAPPED, TK_VEC, TK_MASK } tk_t;

// bits==0 → width unknown (mixed expr / untyped). Integer literals carry their
// value (lit/val/neg) so declared-width fit is checked exactly; a literal's own
// width stays free (5 fits u8 and u64 alike).
typedef struct {
    tk_t        k;      // core kind (through the ref, when rk != 0)
    proven_u8   bits;
    bool        sign;
    bool        lit;
    proven_u64  val;
    bool        neg;
    proven_u8   rk;     // reference kind: 0 = value, 1 = ref (shared), 2 = mut_ref
    proven_u8   nominal;// 0 = plain uN/iN · 1 = usize/isize (RFC-0052 D11 — 구분 타입)
    double      fval;   // float literal 의 값 (lit && k == TK_FLOAT 일 때)
    bool        has_rng;// RFC-0055 D7: 선언된 **범위**를 가진 정수 (range [τ] lo hi)
    proven_i64  rlo, rhi;
    bool        is_mut; // ★ `mut` 로 선언됐는가 — 슬라이스 원소 쓰기의 전제
    proven_u8   lanes;  // TK_VEC / TK_MASK: 레인 수
    proven_u8   vflt;   // TK_VEC: 원소가 부동인가
    proven_u8   ebits;  // TK_SLICE: 원소 폭(비트). 0 = 미상
    bool        esign;  // TK_SLICE: 원소 부호
    bool        eflt;   // TK_SLICE: 원소가 부동인가
    // ★★★ **틀 인자는 타입의 일부다** (X-0009 · 2026-08-15). `gv u32 …` 와 `gv u8 …` 은
    //   지금까지 **둘 다 그냥 TK_NAMED** 였다 — 머리 이름도, 인자도 안 실려서 바인딩에서
    //   서로 통과했고, 같은 저장소를 다른 원소폭으로 읽으며 **조용히 틀린 값**을 냈다.
    //   ☞ 좁게 싣는다: 머리 이름(`nname`)과 **나머지 낱말의 해시**(`targ`). 비교는
    //     *머리가 같은데 인자가 다를 때*만 문다 — 투명 별칭(머리가 아예 다른 이름)은
    //     여전히 안 건드린다(SPEC-004 §89 가 *같은 타입*이라 했다).
    // ★★★★ **`option` 과 `result` 는 다른 것이다** (2026-08-15, RFC-0075 Stage 4 Task 2).
    //   둘 다 그냥 `TK_WRAPPED` 라, 검사기는 `option handle` 자리에 `result handle` 을 넣어도
    //   아무 말도 안 했다. 그 상태에서 라이브러리를 두 자리 → 세 자리로 옮기면 **컴파일러가
    //   길을 안내하지 못한다** — 손으로 옮기고 손으로 맞기를 바라는 셈이다.
    //   ⇒ 감싸개의 **종류**를 싣는다. 0 = 미상 · 1 = option · 2 = result.
    //   ★ 0(미상)은 늘 호환이다: 추론이 payload 를 아직 안 나르는 자리가 많고,
    //     모르면 안 무는 쪽이 옳다(거짓음성 < 거짓양성).
    proven_u8   wrap;
    proven_u8str_view_t nname;  // TK_NAMED: 머리 타입 이름
    proven_u32  targ;           // TK_NAMED: 틀 인자 낱말들의 해시(0 = 인자 없음/미상)
} ty_t;

// ★★★★ **용량이 조용히 검사를 멈추게 했다** (2026-09-09 · REQ-0010 · REQ-0011).
//   두 수가 «표가 차면 그만» 이었고, 그 뒤의 프로그램은 **검사를 아예 안 받았다**:
//     · `TC_MAXP` 가 8 이라 **아홉 번째 파라미터부터** 타입이 안 실렸다 ⇒ 그 이름을 쓰는
//       본문이 검사되지 않았다(`bool` 을 낸다고 적고 아홉 번째 u64 를 돌려줘도 초록).
//     · `TC_MAXSIG` 가 128 이라 op 이 129 개인 단위에서 **129 번째부터 몸통이 안 걸렸다**
//       (틀린 반환이 그대로 통과했다).
//   ⇒ 두 가지를 함께 고친다: **한계를 IR 과 맞추고**(파라미터 16 = `LOW_MAX_PARAMS`),
//     그래도 넘치면 **시끄럽게 거절한다**(`E-TYPE-LIMIT`).
//   ☞ *한계는 있어도 된다. 그러나 한계에 닿았을 때 «검사했다» 고 말하면 그것은 거짓말이다.*
//     (바로 아래 순환 탐지기가 같은 규율을 이미 적어 두었다: 거절이 정직한 답이다.)
#define TC_MAXP LOW_HDR_MAXP   /* ★ 수를 베끼지 않는다 — 원본은 low_cst.h 하나다(WO-0204). 그 위는 IR 이 arity 로 거절한다 */
/* ★ 256 → 512 (2026-09-18, X-0032). `lowget` 이 TLS·암호·인증서·신뢰 저장소를 한 단위로
 *   묶으면서 op 이 256 을 넘었다 — 진단이 그 자리에서 **거절**했고(초록이 아니라), 그것이
 *   이 수가 있는 까닭이다. 올리는 근거: 이 표는 `tc_sig_t`·`tc_struct_t`·`tnames` 셋을
 *   같은 수로 잡으므로 512 면 세 배열이 커지지만 전부 컴파일러 안의 자리이고, 프로그램이
 *   쓰는 메모리와는 무관하다. ☞ 한계는 있어도 된다. 닿았을 때 「검사했다」고 말하지만 않으면. */
#define TC_MAXSIG 512
#define TC_MAXENV 128

typedef struct {
    proven_u8str_view_t name;
    ty_t                params[TC_MAXP];
    proven_size_t       nparams;
    ty_t                ret;
    const low_cst_t    *body;
    // ★★★★★ **그리고 본문은 제 폼에서 파라미터 이름을 받아야 한다** (X-0008 · 2026-08-15).
    //   pass 2 가 폼을 **이름으로 다시 찾고 있었다** — 단위 안 **첫 일치**를. 그래서
    //   `hashmap.put` 의 파라미터 이름·개수가 `strmap.put` 의 타입에 붙어, 본문의
    //   `sm_write slots keys …` 가 **남의 시그니처로** 검사되며 E-TYPE-ARG 를 냈다.
    //   ⇒ **찾지 않는다.** pass 1 이 이미 그 폼을 손에 들고 있으니 그대로 들고 온다
    //   (조회를 고치는 것보다 조회를 **없애는** 것이 낫다 — 틀릴 자리가 사라진다).
    const low_cst_t    *form;
    // ★★★★ **같은 이름의 시그니처가 둘일 수 있다** (2026-08-15). `hashmap` 과 `strmap` 은
    //   `lookup`·`put`·`del`… 일곱을 겹치고 시그니처가 다르다. 이름만 보면 **남의 시그니처**로
    //   인자를 검사해 `E-TYPE-ARG` 가 났다 — 두 모듈이 한 단위에 공존할 수 없었다.
    //   ⇒ 같은 규율을 여기도: **한정이면 그 모듈, 아니면 제 모듈, 그래도 없으면 첫 일치.**
    proven_u8str_view_t mod;
} tc_sig_t;

typedef struct { proven_u8str_view_t name; ty_t ty; } tc_var_t;

// ★ 구조체 선언 — 타입체커가 이것을 **아예 몰랐다.** 그래서 `make` 리터럴이
//   필드 타입도, 누락도, 없는 필드도 검사받지 않았다:
//     struct p  x u8 . y u8 . end
//     make p do x 1 . end          → {x 1}          y 가 **아예 없다**
//     make p do x 1 . ghost 2 . end→ {x 1, ghost 2} 선언에 없는 필드가 들어간다
//     make p do x <i32> . … end    → {x -1}         u8 인데 -1
//   구조체 선언이 아무것도 강제하지 않았다. 선언은 검사되지 않으면 거짓말이 된다.
#define TC_MAXF 16
typedef struct {
    proven_u8str_view_t name;
    proven_u8str_view_t fname[TC_MAXF];
    ty_t                fty[TC_MAXF];
    // ★ 필드 선언 원문 — 타입 **낱말들**을 다시 읽으려고 든다(재귀 검사, 아래 pass 0b).
    //   `fty` 는 이미 해석된 타입이라 *"어느 struct 를 가리키는가"* 를 되물을 수 없다.
    const low_cst_t    *fform[TC_MAXF];
    proven_size_t       nf;
} tc_struct_t;

typedef struct {
    tc_sig_t        sigs[TC_MAXSIG];
    proven_size_t   nsigs;
    bool            cycle_overflow;   // ★ 순환 탐지 스택이 넘쳤다 — 그러면 판정을 안 낸다
    tc_struct_t     strs[TC_MAXSIG];
    proven_size_t   nstrs;
    // ★ 선언된 타입 이름들 — `type N is …` · `struct N` · `enum N`.
    //   이것이 없어서 **존재하지 않는 타입을 써도 통과했다.**
    //   (SPEC-MVP §8 예제 B 가 실제로 graph·node_id·bytes 를 선언 없이 썼다.)
    proven_u8str_view_t tnames[TC_MAXSIG];
    proven_size_t   ntnames;
    proven_array_t *diags;   // → out.diags (so pushes reach the returned array)
    bool           *ok;
    // ★★★★ **묻기만 하고 고발하지는 않는 추론** (RFC-0092, 2026-08-10).
    //   조건 자리(`if`/`guard`/`while`)의 타입을 알려고 `tc_infer_run` 을 불렀더니,
    //   그것이 **부수로 다른 진단까지 냈다** — 조건식은 그전까지 **한 번도 타입검사를
    //   받은 적이 없어서**, 갑자기 검사되자 잠자던 불평이 셋 튀어나왔다
    //   (`mvp.low` 의 `lt i (len xs)` 가 usize 대 u64 로 E-TYPE-NOMINAL).
    //   내 관심은 **"조건이 수인가"** 하나인데 검사 범위를 조용히 넓힌 것이다.
    //   ⇒ 추론하는 동안은 **입을 막고**, 내가 낼 진단만 낸다. 조건식 전반의 타입검사는
    //     그 자체로 별개 결정이고, 하려면 저장소 영향을 재고 따로 해야 한다.
    int             quiet;
    proven_u8str_view_t curmod;   // ★ 지금 검사 중인 op 이 속한 모듈(RFC-0075 S2)
    const low_cst_t    *curform;  // ★ 2026-09-14 — 지금 검사 중인 폼(진단에 그 파일을 싣는다)
} tc_ctx_t;

static bool veq(proven_u8str_view_t v, const char *s) { return proven_u8str_view_eq(v, proven_u8str_view_from_cstr(s)); }

static ty_t tk(tk_t k) { return (ty_t){ .k = k }; }
static ty_t tk_int(proven_u8 bits, bool sign) { return (ty_t){ .k = TK_INT, .bits = bits, .sign = sign }; }

// ★★★ **newtype 레지스트리** (RFC-0002 §6.3.1 — 명목 newtype). `newtype N core .` 는 투명 별칭과 달리
//   **자기 정체성**을 갖는다: core 의 int 로 해소하되 **새 nominal id**(≥2)를 찍어, u64/다른 newtype 과
//   섞이면 E-TYPE-NOMINAL 이 뜬다. 투명 `type` 별칭은 TK_NAMED 그대로(불투명·허용) — 안 건드린다.
static struct { proven_u8str_view_t name; proven_u8 bits; bool sign; bool flt; proven_u8 nominal; } g_nt[TC_MAXSIG];
// ★ 그 newtype 이 **값 타입으로** 쓰였는가(RFC-0104 §8-2 브랜드 판별 — 위 ty_of_word 주석).
static bool g_ntval[TC_MAXSIG];
// ★★★ **newtype 의 struct core** (RFC-0002 §6.3.1) — core 가 이름 타입(struct/enum/actor)일 때.
//   int/float 코어는 위 표가 명목 id 를 주지만, 이름 코어는 TK_NAMED 로 떨어져 **아무 구별도 없었다**:
//   `newtype spot pt .` 라 적어도 `pt` 를 `spot` 자리에 그냥 넣을 수 있었다 — 낱말이 **약속만 하고
//   아무것도 안 바꾸는 장식**(PRINCIPLES.md §0). 여기에도 명목 id 를 준다.
static struct { proven_u8str_view_t name; proven_u8 nominal; } g_nts[TC_MAXSIG];
// ★★★ **투명 별칭 레지스트리** (X-0010 · 2026-08-15). `type X Y … .` 은 명세가 *같은 타입*이라
//   했다(SPEC-004 §89). 그래서 이름으로 타입을 가르려면 **먼저 펴야** 한다 — 안 펴고 이름만
//   맞대면 `gvec` 과 `vecgen.vec` 이 다른 타입이 되어, 명세가 같다고 한 둘을 도구가 갈라 놓는다.
static struct { proven_u8str_view_t name, target; } g_alias[TC_MAXSIG];
static proven_size_t g_nalias;
// ★★★ **선언된 타입 이름**. 제네릭 틀의 본문에서 `t` 는 그냥 이름이다 — 선언된 적이 없다.
//   그런 이름까지 맞대면 틀 안에서 거짓양성이 쏟아진다. ⇒ **양쪽이 다 선언된 이름일 때만** 본다.
static proven_u8str_view_t g_tdecl[TC_MAXSIG * 2];
static proven_size_t g_ntdecl;
static proven_size_t g_nnts;
static proven_size_t g_nnt;

static ty_t ty_of_word(proven_u8str_view_t v) {
    if (veq(v, "bool")) return tk(TK_BOOL);
    if (veq(v, "u8"))  return tk_int(8, false);
    if (veq(v, "i8"))  return tk_int(8, true);
    if (veq(v, "u16")) return tk_int(16, false);
    if (veq(v, "i16")) return tk_int(16, true);
    if (veq(v, "u32")) return tk_int(32, false);
    if (veq(v, "i32")) return tk_int(32, true);
    if (veq(v, "u64")) return tk_int(64, false);
    if (veq(v, "i64")) return tk_int(64, true);
    if (veq(v, "usize")) { ty_t t = tk_int(64, false); t.nominal = 1; return t; }   // D11: u64 별칭 아님
    if (veq(v, "isize")) { ty_t t = tk_int(64, true);  t.nominal = 1; return t; }
    if (veq(v, "f32")) return (ty_t){ .k = TK_FLOAT, .bits = 32 };
    if (veq(v, "f64")) return (ty_t){ .k = TK_FLOAT, .bits = 64 };
    if (veq(v, "slice") || veq(v, "array")) return tk(TK_SLICE);
    // ★ `segments T` 의 값은 **백업 슬라이스와 서술자 슬라이스를 든 그릇**이다 —
    //   원소를 색인하는 것이 아니라 `seg` 로 조각(슬라이스)을 꺼낸다. 이름 있는 타입으로 둔다.
    if (veq(v, "segments")) return tk(TK_NAMED);
    if (veq(v, "result")) { ty_t w = tk(TK_WRAPPED); w.wrap = 2; return w; }   // 풀 수 있는 것
    if (veq(v, "option")) { ty_t w = tk(TK_WRAPPED); w.wrap = 1; return w; }
    for (proven_size_t i = 0; i < g_nnt; i++)                          // ★ newtype — 명목 int/float 로 해소
        if (proven_u8str_view_eq(g_nt[i].name, v)) {
            // ★★★ **값으로 쓰인 newtype 은 브랜드가 아니다** (RFC-0104 §8-2 · 소유자 결정
            //   2026-08-28). 이 함수는 **선언된 타입 자리**에서만 불린다 — 그래서 여기를
            //   지나갔다는 것은 곧 *"그 이름이 값의 타입으로 쓰였다"* 는 뜻이다.
            //   브랜드는 자료를 안 나르므로 이 문을 **한 번도 안 지난다**(`newtype pa u8 .` 은
            //   comptime 타입 인자로만 간다). 반대로 `newtype node_id u32 .` 는 키 값이라 지난다.
            g_ntval[i] = true;
            ty_t t = g_nt[i].flt ? (ty_t){ .k = TK_FLOAT, .bits = g_nt[i].bits }
                                 : tk_int(g_nt[i].bits, g_nt[i].sign);
            t.nominal = g_nt[i].nominal;
            return t;
        }
    for (proven_size_t i = 0; i < g_nnts; i++)        // ★ newtype 의 **이름 코어** — 명목 id 를 실은 TK_NAMED
        if (proven_u8str_view_eq(g_nts[i].name, v)) { ty_t t = tk(TK_NAMED); t.nominal = g_nts[i].nominal; return t; }
    return tk(TK_NAMED);
}

// ── RFC-0055 D7: `range [τ] lo hi` — 범위는 **기계 타입의 정련**이다 ───────────
// τ 를 적으면 표현이 못 박히고 [lo,hi] ⊆ τ 가 **컴파일타임에 강제**된다(E-TYPE-RANGE).
// τ 를 생략하면 가장 싼 폭이 유도된다(D1) — 두 형태는 같은 타입을 낳는다.
// 어느 쪽이든 결과는 **TK_INT** 다. 그래서 RFC-0052 의 ⊑ 규칙 전부가 range 값에도 걸린다.
bool low_num_is_float(proven_u8str_view_t v);
static bool num_value(proven_u8str_view_t v, proven_u64 *out, bool *neg);
static proven_i64 ty_lo(proven_u8 bits, bool sign) {
    if (!sign) return 0;
    return -(proven_i64)((proven_u64)1 << (bits - 1));
}
static proven_i64 ty_hi(proven_u8 bits, bool sign) {
    if (!sign) return (bits >= 64) ? INT64_MAX : (proven_i64)(((proven_u64)1 << bits) - 1);
    return (proven_i64)(((proven_u64)1 << (bits - 1)) - 1);
}
static bool rng_fits_ty(proven_i64 lo, proven_i64 hi, ty_t t) {
    return lo >= ty_lo(t.bits, t.sign) && hi <= ty_hi(t.bits, t.sign);
}
// 범위에서 가장 싼 폭을 유도한다(τ 생략 시).
static ty_t ty_derive_range(proven_i64 lo, proven_i64 hi) {
    bool sign = lo < 0;
    ty_t t = tk_int(64, sign);
    for (proven_u8 b = 8; b <= 64; b = (proven_u8)(b * 2)) {
        ty_t cand = tk_int(b, sign);
        if (rng_fits_ty(lo, hi, cand)) { t = cand; break; }
    }
    t.has_rng = true; t.rlo = lo; t.rhi = hi;
    return t;
}
static bool tc_int_lit(proven_u8str_view_t v, proven_i64 *out) {
    if (low_num_is_float(v)) return false;
    proven_u64 val; bool neg;
    if (!num_value(v, &val, &neg)) return false;
    if (neg) { if (val > (proven_u64)INT64_MAX + 1) return false; *out = -(proven_i64)val; }
    else     { if (val > (proven_u64)INT64_MAX) return false;     *out =  (proven_i64)val; }
    return true;
}
// `range` 로 시작하는 타입을 읽는다. 성공하면 소비한 낱말 수를 *used 에 넣는다.
// 오류(빈 범위 · τ 가 범위를 못 담음 · τ 가 부동)는 rerr 로 알린다 — 호출자가 진단을 낸다.
typedef enum { RNG_NONE, RNG_OK, RNG_EMPTY, RNG_OVERFLOW, RNG_KIND } rng_res_t;
static rng_res_t ty_of_range_at(const low_cst_t *f, proven_size_t i, proven_size_t end,
                                ty_t *out, proven_size_t *used) {
    if (i >= end || i >= f->nkids || f->kids[i]->kind != LOW_CST_ATOM) return RNG_NONE;
    if (!veq(f->kids[i]->tok.lex, "range")) return RNG_NONE;
    proven_size_t j = i + 1;
    ty_t base = { .k = TK_UNKNOWN };
    bool explicit_ty = false;
    if (j < end && j < f->nkids && f->kids[j]->kind == LOW_CST_ATOM) {
        ty_t w = ty_of_word(f->kids[j]->tok.lex);
        if (w.k == TK_INT || w.k == TK_FLOAT) { base = w; explicit_ty = true; j++; }   // ★ range τ lo hi
    }
    if (j + 1 >= end || j + 1 >= f->nkids ||
        f->kids[j]->kind != LOW_CST_ATOM || f->kids[j + 1]->kind != LOW_CST_ATOM) return RNG_NONE;
    proven_i64 lo, hi;
    if (!tc_int_lit(f->kids[j]->tok.lex, &lo) || !tc_int_lit(f->kids[j + 1]->tok.lex, &hi)) return RNG_NONE;
    *used = (j + 2) - i;
    if (lo > hi) { *out = ty_derive_range(0, 0); return RNG_EMPTY; }
    if (!explicit_ty) { *out = ty_derive_range(lo, hi); return RNG_OK; }
    if (base.k != TK_INT) { *out = ty_derive_range(lo, hi); return RNG_KIND; }   // range f32 … 은 없다
    base.has_rng = true; base.rlo = lo; base.rhi = hi;
    *out = base;
    return rng_fits_ty(lo, hi, base) ? RNG_OK : RNG_OVERFLOW;   // ★ [lo,hi] ⊆ τ 강제
}

// ★ X-0070 (2026-09-27) — «이 수 리터럴은 부동인가» 는 **여기 한 곳**에서 판정한다. low_check.c 가 제 사본을
//   두고 `.`·`e`·`E` 만 보아서 `0x1p4`(16진 부동)를 정수로, `0xe0`(16진 정수)를 부동으로 잘못 갈랐다 — 앞은 거짓
//   E-TYPE-MIX, 뒤는 `mul 2.0 0xe0` 이 검사를 지나 실행에서 E-VM-TYPE 로 멈췄다. 교훈 7: 같은 판정 두 벌은 갈린다.
bool low_num_is_float(proven_u8str_view_t v) {
    proven_size_t o = (v.size && (v.ptr[0] == '-' || v.ptr[0] == '+')) ? 1 : 0;  // skip sign
    if (v.size >= o + 2 && v.ptr[o] == '0' && (v.ptr[o + 1] == 'x' || v.ptr[o + 1] == 'X')) {
        for (proven_size_t i = o + 2; i < v.size; i++)  // hex float: '.' or 'p'/'P'
            if (v.ptr[i] == '.' || v.ptr[i] == 'p' || v.ptr[i] == 'P') return true;
        return false;
    }
    if (v.size >= o + 2 && v.ptr[o] == '0' && (v.ptr[o + 1] == 'b' || v.ptr[o + 1] == 'B'))
        return false;
    for (proven_size_t i = o; i < v.size; i++)
        if (v.ptr[i] == '.' || v.ptr[i] == 'e' || v.ptr[i] == 'E') return true;
    return false;
}
// plain integer literal → value (saturating at u64 max — still "does not fit u32")
static bool num_value(proven_u8str_view_t v, proven_u64 *out, bool *neg) {
    proven_size_t i = 0; *neg = false;
    if (v.size && (v.ptr[0] == '-' || v.ptr[0] == '+')) { *neg = (v.ptr[0] == '-'); i = 1; }
    proven_u64 val = 0, base = 10;
    if (i + 1 < v.size && v.ptr[i] == '0' && (v.ptr[i + 1] == 'x' || v.ptr[i + 1] == 'X')) { base = 16; i += 2; }
    else if (i + 1 < v.size && v.ptr[i] == '0' && (v.ptr[i + 1] == 'b' || v.ptr[i + 1] == 'B')) { base = 2; i += 2; }
    for (; i < v.size; i++) {
        char ch = (char)v.ptr[i];
        if (ch == '_') continue;
        proven_u64 d;
        if (ch >= '0' && ch <= '9') d = (proven_u64)(ch - '0');
        else if (base == 16 && ch >= 'a' && ch <= 'f') d = (proven_u64)(ch - 'a' + 10);
        else if (base == 16 && ch >= 'A' && ch <= 'F') d = (proven_u64)(ch - 'A' + 10);
        else return false;
        if (d >= base) return false;
        val = (val > (UINT64_MAX - d) / base) ? UINT64_MAX : val * base + d;
    }
    *out = val; return true;
}
static ty_t ty_of_number(proven_u8str_view_t v) {
    if (low_num_is_float(v)) {   // D3: 부동 리터럴도 comptime 무타입 — 값은 적합 검사용으로 들고 간다
        return (ty_t){ .k = TK_FLOAT, .lit = true, .fval = low_num_to_double(v) };
    }
    ty_t t = tk_int(0, false);
    t.lit = num_value(v, &t.val, &t.neg);
    return t;
}

// RFC-0052 D3: 부동 리터럴이 대상 폭의 표현 범위에 들어가는가.
// 언더플로(→ 0)는 IEEE 의 정상 동작이라 오류가 아니다. **오버플로(→ inf)만 오류**다 —
// 정수 리터럴이 폭을 넘으면 컴파일 오류인 것과 같은 규칙(조용한 inf 금지).
static bool flit_fits(ty_t lit, ty_t decl) {
    if (decl.bits != 32) return true;          // f64 는 double 그대로
    double v = lit.fval;
    if (!isfinite(v)) return true;             // 소스가 이미 inf/nan 이면 그대로 통과
    return isfinite((double)(float)v);
}
// does a literal value fit a declared integer width/signedness?
static bool lit_fits(ty_t lit, ty_t decl) {
    if (decl.k == TK_FLOAT) return flit_fits(lit, decl);
    if (!decl.bits) return true;
    if (!decl.sign) {
        if (lit.neg && lit.val != 0) return false;
        return decl.bits >= 64 || lit.val <= ((proven_u64)1 << decl.bits) - 1;
    }
    proven_u64 max = ((proven_u64)1 << (decl.bits - 1)) - 1;
    return lit.neg ? lit.val <= max + 1 : lit.val <= max;
}

// ── RFC-0052 D1/D2: 안전 확대 격자 ⊑ ─────────────────────────────────────────
// `docs/proofs/coq/NumericLattice.v` 의 `sub` 를 그대로 옮긴 것.
//   uN ⊑ uM (N ≤ M) · iN ⊑ iM (N ≤ M) · uN ⊑ iM (N < M) · iN ⋢ uM (언제나)
// ★ sub_preserves (Qed): τ ⊑ τ' → τ 의 모든 값이 τ' 에 표현 가능.
//   암묵 확대가 안전하다는 주장의 전부가 그 정리이고, 기계가 검사했다.
static bool ty_sub(ty_t t, ty_t u) {
    if (t.k != u.k) return false;
    if (t.k == TK_FLOAT) return t.bits <= u.bits;      // f32 ⊑ f64
    if (t.k != TK_INT) return true;
    if (t.nominal != u.nominal) return false;          // D11: usize ⋢ u64 (nominal 구분)
    if (!t.sign && !u.sign) return t.bits <= u.bits;   // uN ⊑ uM
    if (t.sign && u.sign)   return t.bits <= u.bits;   // iN ⊑ iM
    if (!t.sign && u.sign)  return t.bits <  u.bits;   // uN ⊑ iM — 엄격히 넓어야
    return false;                                      // iN ⋢ uM — 음수를 담을 수 없다
}
static bool ty_comparable(ty_t t, ty_t u) { return ty_sub(t, u) || ty_sub(u, t); }
// ★ join_is_an_operand (Qed): join 은 언제나 두 피연산자 중 하나 — 발명하지 않는다
static ty_t ty_join(ty_t t, ty_t u) { return ty_sub(t, u) ? u : t; }

// conservative: never flag when either side is unknown/named; then kind, then
// (integers/floats) literal fit / ⊑ 확대 판정 (D2: 암묵 변환은 ⊑ 를 따를 때만).
typedef enum { TC_OK, TC_KIND, TC_WIDTH, TC_SIGN, TC_NOMINAL, TC_RANGE, TC_LANES,
               TC_INSTANCE, TC_STRUCT, TC_WRAP } tc_reason_t;   // TC_WRAP: option ↔ result   // TC_STRUCT: 서로 다른 명명 타입(X-0010)   // ★ TC_INSTANCE: 같은 틀의 **다른 인스턴스**(X-0009)
// RFC-0055 D7: 리터럴은 선언된 **범위**에 들어야 한다 — 폭이 아니라 범위가 계약이다.
static bool lit_in_rng(ty_t lit, ty_t decl) {
    if (!decl.has_rng || !lit.lit) return true;
    if (lit.neg) return lit.val <= (proven_u64)INT64_MAX + 1 &&
                        -(proven_i64)lit.val >= decl.rlo && -(proven_i64)lit.val <= decl.rhi;
    return lit.val <= (proven_u64)INT64_MAX &&
           (proven_i64)lit.val >= decl.rlo && (proven_i64)lit.val <= decl.rhi;
}
// 별칭을 **고정점까지** 편다. 순환 별칭은 상한이 끊는다(순환 자체는 별개의 진단감이다).
static proven_u8str_view_t tc_canon_name(proven_u8str_view_t v) {
    for (int hop = 0; hop < 8; hop++) {
        bool moved = false;
        for (proven_size_t i = 0; i < g_nalias; i++)
            if (proven_u8str_view_eq(g_alias[i].name, v)) { v = g_alias[i].target; moved = true; break; }
        if (!moved) break;
    }
    return v;
}
static bool tc_is_declared_tname(proven_u8str_view_t v) {
    for (proven_size_t i = 0; i < g_ntdecl; i++)
        if (proven_u8str_view_eq(g_tdecl[i], v)) return true;
    return false;
}

// 단형화기가 지은 인스턴스 이름인가 — `twice#rect`·`gv#u32#allocs` 처럼 `#` 이 든 이름.
static bool tc_is_instance_name(proven_u8str_view_t v) {
    for (proven_size_t i = 0; i < v.size; i++) if (v.ptr[i] == (proven_byte_t)'#') return true;
    return false;
}

static tc_reason_t compat(ty_t decl, ty_t actual) {
    // ★★★ **이름 타입끼리도 newtype 이면 구별한다** (RFC-0002 §6.3.1 — struct core).
    //   ★ 범위를 좁게 둔다: **명목 id 를 가진 쪽이 있을 때만** 본다. 투명 `type X Y .` 별칭은
    //     명세가 *같은 타입*이라 했으므로(SPEC-004 §89) 오늘처럼 호환이어야 하고, 일반 struct 끼리의
    //     검사(`rc` 를 `pt` 자리에)는 **아직 없다** — 그건 이 항목과 별개의 더 넓은 틈이라 손대지 않는다.
    if (decl.k == TK_NAMED && actual.k == TK_NAMED && (decl.nominal || actual.nominal) &&
        decl.nominal != actual.nominal) return TC_NOMINAL;
    // ★★★ **같은 틀의 다른 인스턴스는 다른 타입이다** (X-0009 · 2026-08-15).
    //   `gv u32 …` 값을 `gv u8 …` 변수에 묶는 것이 통과하고 **3 대신 1** 을 냈다. 여기서 문다.
    //   ★ 범위를 좁게 둔다: **머리 이름이 같고**, 양쪽 다 인자를 갖고, 그 인자가 다를 때만.
    //     머리가 다르면 안 본다 — 투명 별칭(`type gvec vecgen.vec u8 .`)이 그 자리이고,
    //     명세가 *같은 타입*이라 했으므로 오늘처럼 호환이어야 한다(SPEC-004 §89).
    if (decl.k == TK_NAMED && actual.k == TK_NAMED &&
        decl.nname.size && actual.nname.size &&
        proven_u8str_view_eq(decl.nname, actual.nname) &&
        decl.targ && actual.targ && decl.targ != actual.targ) return TC_NOMINAL;
    // ★★★★ 그리고 **여기가 실제로 무는 자리다**: 단형화는 검사보다 **먼저** 돈다(main.c 의
    //   `low_mono`), 그래서 이 검사기가 보는 것은 `bx u32` 가 아니라 **인스턴스 이름** `bx#u32` 다.
    //   두 인스턴스는 서로 다른 구체 타입이므로 이름이 다르면 **다른 타입**이다.
    //   ★ `#` 이 든 이름만 본다 — 그것이 단형화기가 지은 이름의 표식이고, 사람이 쓴 이름
    //     (투명 별칭 포함)에는 `#` 이 없다. 그래서 별칭 호환은 건드리지 않는다.
    if (decl.k == TK_NAMED && actual.k == TK_NAMED &&
        tc_is_instance_name(decl.nname) && tc_is_instance_name(actual.nname) &&
        !proven_u8str_view_eq(decl.nname, actual.nname)) return TC_INSTANCE;
    // ★★★★★ **그리고 평범한 struct 끼리도 이름으로 가른다** (X-0010 · 2026-08-15).
    //   `meters` 를 `seconds` 자리에 넣는 것이 통과하고 **100 을 냈다** — 표현이 같다는 이유로
    //   단위가 섞였다. 이름은 타입의 일부다.
    //   ★ 두 겹의 문지기를 둔다: ① **투명 별칭을 편 뒤** 비교한다(명세가 *같은 타입*이라 한 둘을
    //     갈라 놓지 않으려고) ② **양쪽이 다 선언된 타입 이름일 때만** 본다(제네릭 틀 본문의
    //     타입 파라미터 `t` 는 선언된 적이 없다 — 그런 이름을 맞대면 틀 안에서 거짓양성이 난다).
    //   ☞ 별칭이 빌트인 코어로 이어지면 그 이름은 선언 목록에 남아 있어도 **핵이 TK_NAMED 가
    //     아니어서** 여기 안 온다. 모르면 안 무는 쪽이 옳다.
    if (decl.k == TK_NAMED && actual.k == TK_NAMED && decl.nname.size && actual.nname.size) {
        proven_u8str_view_t a = tc_canon_name(decl.nname), b = tc_canon_name(actual.nname);
        if (!proven_u8str_view_eq(a, b) && tc_is_declared_tname(a) && tc_is_declared_tname(b))
            return TC_STRUCT;
    }
    // ★★★★★ **`option` 자리에 `result` 를 넣지 않는다**(그 반대도). 둘 다 종류를 아는 때만 문다.
    if (decl.k == TK_WRAPPED && actual.k == TK_WRAPPED &&
        decl.wrap && actual.wrap && decl.wrap != actual.wrap) return TC_WRAP;
    if (decl.k == TK_UNKNOWN || actual.k == TK_UNKNOWN || decl.k == TK_NAMED || actual.k == TK_NAMED) return TC_OK;
    if (decl.k != actual.k) return TC_KIND;
    if (decl.k == TK_INT) {
        if (actual.lit) {                                                   // D3: 리터럴은 comptime 무타입
            if (!lit_fits(actual, decl)) return TC_WIDTH;
            return lit_in_rng(actual, decl) ? TC_OK : TC_RANGE;             // ★ D7: 범위도 강제
        }
        // ★ D7: 두 범위가 **서로소**면 모든 값이 계약을 어긴다 — 검사가 아니라 컴파일 오류다.
        if (decl.has_rng && actual.has_rng && (actual.rlo > decl.rhi || actual.rhi < decl.rlo)) return TC_RANGE;
        if (!decl.bits || !actual.bits) return TC_OK;
        if (decl.nominal != actual.nominal) return TC_NOMINAL;              // D11
        if (ty_sub(actual, decl)) return TC_OK;                             // ★ 값 보존 확대만 암묵
        return decl.sign != actual.sign ? TC_SIGN : TC_WIDTH;
    }
    if (decl.k == TK_FLOAT) {
        if (actual.lit) return flit_fits(actual, decl) ? TC_OK : TC_WIDTH;   // D3
        if (decl.nominal != actual.nominal) return TC_NOMINAL;   // ★ float newtype 도 명목 구별(RFC-0002 §6.3.1)
        if (!decl.bits || !actual.bits) return TC_OK;
        return actual.bits > decl.bits ? TC_WIDTH : TC_OK;   // f64 → f32 는 명시 round_to
    }
    if (decl.k == TK_VEC) {
        // ★ 벡터는 **정확히** 같아야 한다 — 레인 수도, 원소 종류도, 폭도.
        //   암묵 확대는 없다(레인이 다른 두 벡터는 아예 다른 것이다).
        if (decl.lanes != actual.lanes) return TC_LANES;
        if (decl.vflt != actual.vflt) return TC_KIND;
        if (decl.bits != actual.bits) return TC_WIDTH;
        return TC_OK;
    }
    return TC_OK;
}
static void tc_emit(tc_ctx_t *c, const char *code, const char *msg, proven_u32 line) {
    if (c->quiet) return;   // ★ 묻기만 하는 추론 중 — 위 tc_ctx_t.quiet 주석 참조
    low_diag_t d = { .sev = LOW_SEV_ERROR, .code = code, .msg = msg, .line = line, .col = 0,
                     .file = low_cst_file_for_line(c->curform, line) };
    (void)proven_array_push(c->diags, &d);
    *c->ok = false;
}
// ★★★ 2026-09-14 — **이름·타입을 대는 오류.** 문장은 그 진단의 `detail` 에 짓고 `msg` 는 NULL(렌더러는 low_diag_text).
#include <stdarg.h>
#include <stdio.h>
static void tc_emitf(tc_ctx_t *c, const char *code, proven_u32 line, const char *fmt, ...) {
    if (c->quiet) return;
    low_diag_t d = { .sev = LOW_SEV_ERROR, .code = code, .msg = NULL, .line = line, .col = 0,
                     .file = low_cst_file_for_line(c->curform, line) };
    va_list ap; va_start(ap, fmt); vsnprintf(d.detail, sizeof d.detail, fmt, ap); va_end(ap);
    (void)proven_array_push(c->diags, &d);
    *c->ok = false;
}
// 사람이 읽는 타입 이름. 모르면 NULL — **짐작한 이름을 대지 않는다.**
static const char *tc_ty_str(ty_t t, char *buf, size_t n) {
    const char *ref = t.rk == 1 ? "ref " : t.rk == 2 ? "mut_ref " : "";
    switch (t.k) {
        case TK_BOOL:  snprintf(buf, n, "%sbool", ref); return buf;
        case TK_INT:
            if (t.lit) { snprintf(buf, n, "an integer literal"); return buf; }
            if (t.nominal) snprintf(buf, n, "%s%s", ref, t.sign ? "isize" : "usize");
            else snprintf(buf, n, "%s%c%u", ref, t.sign ? 'i' : 'u', (unsigned)t.bits);
            return buf;
        case TK_FLOAT:
            if (t.lit || !t.bits) { snprintf(buf, n, "a float literal"); return buf; }
            snprintf(buf, n, "%sf%u", ref, (unsigned)t.bits); return buf;
        case TK_SLICE:
            if (!t.ebits) { snprintf(buf, n, "%sslice", ref); return buf; }
            snprintf(buf, n, "%sslice %c%u", ref, t.eflt ? 'f' : t.esign ? 'i' : 'u', (unsigned)t.ebits); return buf;
        case TK_NAMED:
            if (!t.nname.size) return NULL;
            snprintf(buf, n, "%s%.*s", ref, (int)t.nname.size, (const char *)t.nname.ptr); return buf;
        case TK_UNIT:    snprintf(buf, n, "void"); return buf;
        case TK_WRAPPED: snprintf(buf, n, "%s", t.wrap == 1 ? "option …" : t.wrap == 2 ? "result …" : "option/result"); return buf;
        case TK_VEC:     snprintf(buf, n, "vec"); return buf;
        case TK_MASK:    snprintf(buf, n, "mask"); return buf;
        default: return NULL;
    }
}
// 종류가 어긋난 자리(tc_flag 가 제 코드로 내는 갈래)에 **기대한 타입과 받은 타입**을 붙인다
static void tc_flag_types(tc_ctx_t *c, tc_reason_t r, const char *code, const char *msg,
                          ty_t want, ty_t got, proven_u32 line);

// ★★ **경고는 검사를 실패시키지 않는다** (2026-08-21). 이 층에는 `tc_emit` 하나뿐이었고
//   그것은 언제나 `LOW_SEV_ERROR` 다 — 그래서 `W-` 코드를 내면 **이름은 경고인데 행동은
//   에러**가 됐다(실측: `shared_read` 를 받아들이는지 보는 유닛 시험이 그 자리에서 깨졌다).
//   ⇒ 심각도를 이름과 맞춘다. *코드의 접두어가 곧 약속이다.*
static void tc_warn(tc_ctx_t *c, const char *code, const char *msg, proven_u32 line) {
    if (c->quiet) return;
    low_diag_t d = { .sev = LOW_SEV_WARNING, .code = code, .msg = msg, .line = line, .col = 0,
                     .file = low_cst_file_for_line(c->curform, line) };
    (void)proven_array_push(c->diags, &d);
}
// kind mismatches keep their site-specific code; width/sign get dedicated codes
static void tc_flag(tc_ctx_t *c, tc_reason_t r, const char *code, const char *msg, proven_u32 line) {
    if (r == TC_OK) return;
    if (r == TC_WIDTH)        tc_emit(c, "E-TYPE-WIDTH", "value does not fit the declared type (a literal out of range, or a narrowing — use narrow / narrow_wrap / narrow_sat / round_to)", line);
    else if (r == TC_SIGN)    tc_emit(c, "E-TYPE-SIGN", "sign mismatch: no value-preserving widening exists (widen both to a strictly wider signed type, or bitcast_sign)", line);
    else if (r == TC_NOMINAL) tc_emit(c, "E-TYPE-NOMINAL", "these are NOMINALLY DISTINCT types with the same representation — a `newtype` (or usize/isize) is not interchangeable with its base or with another newtype. Convert explicitly with `cast <type> <value>` (RFC-0002 §6.3.1)", line);
    else if (r == TC_RANGE)   tc_emit(c, "E-TYPE-RANGE", "the value is outside the declared range (no value can satisfy it — this is a lie, not a check)", line);
    else if (r == TC_INSTANCE) tc_emit(c, "E-TYPE-INSTANCE", "two instances of the same generic template are DIFFERENT concrete types — a container opened for one element type cannot be bound to a variable declared for another. Monomorphization makes each instance a distinct struct (visible as `name#arg` in `--ir`), and their layouts differ; binding across them reads the same storage at the wrong element width and yields a wrong value with no error at all. Declare the variable with the SAME instance, or convert explicitly (RFC-0021 · RFC-0084)", line);
    else if (r == TC_WRAP) tc_emit(c, "E-TYPE-WRAP", "`option` and `result` are not the same shape — one says a value may be ABSENT, the other says producing it may FAIL, and code that reads the wrong one silently treats a failure as an absence (or the reverse). That confusion is exactly what the three-place answer exists to prevent, so the checker must not paper over it here. Unwrap what you were given, or change the declaration to match", line);
    else if (r == TC_STRUCT) tc_emit(c, "E-TYPE-STRUCT", "these are two DIFFERENT named types — the same field layout does not make them the same type. `meters` and `seconds` may both hold one u64 and still mean different things, and a checker that only compares representation lets that confusion through silently. Transparent `type` aliases are expanded before this comparison, so an alias and its target remain interchangeable exactly as the spec says (SPEC-004 §89); what is rejected here is a genuinely different name. Use the declared type, or convert explicitly", line);
    else if (r == TC_LANES)   tc_emit(c, "E-TYPE-LANES", "vector lane counts differ — two vectors of different width are different types (there is no implicit widening between them)", line);
    else                      tc_emit(c, code, msg, line);
}
static void tc_flag_types(tc_ctx_t *c, tc_reason_t r, const char *code, const char *msg,
                          ty_t want, ty_t got, proven_u32 line) {
    if (r == TC_KIND) {
        char wb[96], gb[96];
        const char *ws = tc_ty_str(want, wb, sizeof wb), *gs = tc_ty_str(got, gb, sizeof gb);
        if (ws && gs) { tc_emitf(c, code, line, "%s — expected `%s`, found `%s`", msg, ws, gs); return; }
    }
    tc_flag(c, r, code, msg, line);
}
static const tc_sig_t *sig_find(tc_ctx_t *c, proven_u8str_view_t name) {
    // ★ 제 모듈의 시그니처를 **먼저** 본다(위 tc_sig_t.mod 주석) — 없을 때만 첫 일치.
    if (c->curmod.size)
        for (proven_size_t i = 0; i < c->nsigs; i++)
            if (proven_u8str_view_eq(c->sigs[i].name, name) && c->sigs[i].mod.size &&
                proven_u8str_view_eq(c->sigs[i].mod, c->curmod)) return &c->sigs[i];
    for (proven_size_t i = 0; i < c->nsigs; i++) if (proven_u8str_view_eq(c->sigs[i].name, name)) return &c->sigs[i];
    return NULL;
}
static ty_t env_find(const tc_var_t *env, proven_size_t n, proven_u8str_view_t name, bool *found) {
    for (proven_size_t i = n; i-- > 0; ) if (proven_u8str_view_eq(env[i].name, name)) { *found = true; return env[i].ty; }
    *found = false; return tk(TK_UNKNOWN);
}

// forward
static ty_t tc_infer(tc_ctx_t *c, const low_cst_t *nd, const tc_var_t *env, proven_size_t nenv);

// ── RFC-0052 D1: 이항 수치 연산의 피연산자 규칙 ──────────────────────────────
// 두 피연산자는 ⊑ 로 **비교 가능**해야 한다. 결과 = join(넓은 쪽).
// ★ join_sound (Qed): 비교 가능하면 join 이 둘 다를 손실 없이 담는다.
// 리터럴은 comptime 무타입이므로 상대 타입을 취한다(D3).
static ty_t tc_binop(tc_ctx_t *c, ty_t a, ty_t b, bool is_cmp, proven_u32 line) {
    if (a.k == TK_UNKNOWN || b.k == TK_UNKNOWN || a.k == TK_NAMED || b.k == TK_NAMED)
        return is_cmp ? tk(TK_BOOL) : tk(TK_UNKNOWN);          // 보수적: 모르면 넘어간다
    // 리터럴은 상대의 타입을 취한다(값 적합은 그쪽 경계에서 검사)
    if (a.lit && !b.lit) { if (!lit_fits(a, b)) tc_flag(c, TC_WIDTH, NULL, NULL, line);
                           a = b; }
    else if (b.lit && !a.lit) { if (!lit_fits(b, a)) tc_flag(c, TC_WIDTH, NULL, NULL, line);
                                b = a; }
    else if (a.lit && b.lit) return is_cmp ? tk(TK_BOOL) : a;   // comptime — 무타입 유지
    if (a.k != b.k) {                                           // D8: kind 변경은 암묵 아님
        tc_emit(c, "E-TYPE-MIX",
                "int and float do not mix: convert explicitly (float_of / round_to / trunc_to)", line);
        return is_cmp ? tk(TK_BOOL) : tk(TK_UNKNOWN);
    }
    // ★ 벡터의 이항 연산 — **레인 수와 원소 종류가 정확히 같아야** 한다.
    //   레인이 다른 두 벡터는 아예 다른 것이다. 암묵 확대는 없다.
    //   (지금까지 vec 은 TK_NAMED 로 떨어져 이 규칙이 통째로 없었다. VM 이 런타임에
    //    잡긴 했지만 — E-VM-TYPE — 타입 오류를 런타임까지 미루는 것은 이 언어가 아니다.)
    if (a.k == TK_VEC) {
        // ★ 비교는 **마스크**를 낸다 — bool 이 아니다. 레인마다 하나씩이다.
        ty_t res = a;
        if (is_cmp) { res = tk(TK_MASK); res.lanes = a.lanes; }
        if (a.lanes != b.lanes) { tc_flag(c, TC_LANES, NULL, NULL, line); return res; }
        if (a.vflt != b.vflt) {
            tc_emit(c, "E-TYPE-MIX",
                    "vector lane kinds differ: an integer vector and a float vector do not mix",
                    line);
            return res;
        }
        if (a.bits != b.bits) { tc_flag(c, TC_WIDTH, NULL, NULL, line); return res; }
        return res;
    }
    if (a.k != TK_INT && a.k != TK_FLOAT) return is_cmp ? tk(TK_BOOL) : a;
    if (!a.bits || !b.bits) return is_cmp ? tk(TK_BOOL) : (a.bits ? a : b);   // 폭 미상 — 보수적
    if (!ty_comparable(a, b)) {
        tc_flag(c, a.nominal != b.nominal ? TC_NOMINAL
                : (a.k == TK_INT && a.sign != b.sign) ? TC_SIGN : TC_WIDTH, NULL, NULL, line);
        return is_cmp ? tk(TK_BOOL) : tk(TK_UNKNOWN);
    }
    return is_cmp ? tk(TK_BOOL) : ty_join(a, b);                // ★ 발명하지 않는다
}

// expr island — **타입을 소거하지 않는다**(RFC-0052 §6.2). 중위 식을 좌결합 트리로 보고
// T-ARITH/T-CMP 를 재귀 적용한다. (이전 구현은 결과를 tk_int(0,false) 로 뭉개서
//  식 안으로 한 걸음만 들어가면 폭·부호가 소멸했다 — 뮤테이션 지표 54% silent 의 원인.)
// 원소 타입 → **타입 있는 슬라이스** ty_t (ebits/eflt/esign 를 실어). view_array·subslice 결과용.
static ty_t tc_slice_of_elem(ty_t e) {
    ty_t sl = tk(TK_SLICE);
    sl.ebits = e.bits; sl.esign = e.sign; sl.eflt = (e.k == TK_FLOAT);
    return sl;
}

// ★★★ **섬의 우선순위는 한 표에서 온다**(정본 §6.3.2 · `ir_prec` 와 같은 수). 아래 갈림은
//   그 가운데 `and`·`or` 만 쓴다 — 나머지는 지금까지처럼 왼쪽부터 접는 고리가 본다.
static int tc_prec(const low_cst_t *nd) {
    if (!nd || nd->kind != LOW_CST_ATOM) return -1;
    if (nd->tok.kind == LOW_TOK_OP) {
        char ch = (char)nd->tok.lex.ptr[0];
        return (ch == '*' || ch == '/') ? 5 : 4;
    }
    proven_u8str_view_t v = nd->tok.lex;
    if (veq(v, "eq") || veq(v, "ne") || veq(v, "lt") || veq(v, "le") ||
        veq(v, "gt") || veq(v, "ge")) return 3;
    if (veq(v, "and")) return 2;
    if (veq(v, "or"))  return 1;
    return -1;
}
static ty_t tc_infer_expr(tc_ctx_t *c, low_cst_t *const *k, proven_size_t n, const tc_var_t *env, proven_size_t nenv);
// ★★★★ **`expr a lt b and b lt 10` 이 거절되고 있었다** (결함 노트 #2, 2026-09-16).
//
//   정본 §6.3.2 는 비교가 `and` 보다 **강하다**고 적고, 하강(`ir_prec`)도 그렇게 접는다 —
//   그래서 그 식은 **옳게 돈다**(`flat(3,5)=1` · `flat(3,50)=0`). 그런데 타입 검사만 섬을
//   평평한 낱말 줄로 보고, `and` 를 만나면 **줄에 있는 모든 낱말**을 그 피연산자로 여겼다.
//   `a`·`b`·`10` 은 수이므로 `E-TYPE-LOGICAL` 셋이 났다 — 도구가 자기 하강과 어긋난 것이다.
//   ⇒ `and`·`or` 자리에서 **갈라서** 양쪽을 따로 본다. 비교와 산술은 지금 고리가 그대로 본다
//     (그 자리에서 폭·부호를 재는 검사가 산다 — 갈라 버리면 그것이 사라진다).
static bool tc_expr_logic_split(tc_ctx_t *c, low_cst_t *const *k, proven_size_t n,
                                const tc_var_t *env, proven_size_t nenv, ty_t *out) {
    int lowest = 99; proven_size_t at = n;
    for (proven_size_t i = 0; i < n; i++) {
        int p = tc_prec(k[i]);
        if (p >= 0 && p <= lowest) { lowest = p; at = i; }   // 좌결합 ⇒ 오른쪽 것을 고른다
    }
    if (at == n || at == 0 || at + 1 >= n || lowest > 2) return false;
    ty_t L = tc_infer_expr(c, k, at, env, nenv);
    ty_t R = tc_infer_expr(c, k + at + 1, n - at - 1, env, nenv);
    for (int s = 0; s < 2; s++) {
        ty_t o = s ? R : L;
        if (o.k == TK_INT || o.k == TK_FLOAT)
            tc_emit(c, "E-TYPE-LOGICAL",
                    "`and`/`or` take bool on BOTH sides, not a number — this language has no "
                    "truthiness (D12: no implicit int↔bool). Say what the test is: `ne x 0` "
                    "instead of `x`", k[at]->line);
    }
    *out = tk(TK_BOOL);
    return true;
}
static ty_t tc_infer_expr(tc_ctx_t *c, low_cst_t *const *k, proven_size_t n, const tc_var_t *env, proven_size_t nenv) {
    { ty_t split_out; if (tc_expr_logic_split(c, k, n, env, nenv, &split_out)) return split_out; }
    ty_t acc = tk(TK_UNKNOWN);
    bool have = false, cmp_seen = false;
    for (proven_size_t i = 0; i < n; i++) {
        if (k[i]->kind == LOW_CST_ATOM) {
            proven_u8str_view_t v = k[i]->tok.lex;
            // ★★★★ **논리 op 은 결과만 bool 이고 피연산자는 아무도 안 봤다** (RFC-0092, 2026-08-10).
            //   `and 12 10` 이 통과했다 — 그리고 **논리**로 돌아 1 을 냈다(비트라면 8).
            //   즉 진리성(0 아니면 참)이 조용히 들어와 있었다. 그런데 같은 언어가
            //   `add (eq 1 1) 1` 은 거절하고 `cast bool 5` 도 거절한다(D12) — **한쪽만
            //   검사되고 있었다.** D12 는 "int↔bool 암묵 변환 없음" 이라고 이미 적었는데
            //   정작 논리 op 이 그 변환을 하고 있었다. ⇒ 피연산자도 bool 이라야 한다.
            if (veq(v, "and") || veq(v, "or") || veq(v, "not")) {
                for (proven_size_t q = 0; q < n; q++) {
                    if (q == i) continue;
                    if (k[q]->kind == LOW_CST_ATOM) {
                        proven_u8str_view_t w = k[q]->tok.lex;
                        if (veq(w, "and") || veq(w, "or") || veq(w, "not")) continue;
                    }
                    c->quiet++;   // ★ 타입만 묻는다(위 tc_ctx_t.quiet)
                    ty_t o = tc_infer(c, k[q], env, nenv);
                    c->quiet--;
                    if (o.k == TK_INT || o.k == TK_FLOAT)
                        tc_emit(c, "E-TYPE-LOGICAL",
                                "`and`/`or`/`not` take bool, not a number — this language has no "
                                "truthiness (D12: no implicit int↔bool). Say what the test is: "
                                "`ne x 0` instead of `x`", k[q]->line);
                }
                return tk(TK_BOOL);
            }
            if (veq(v, "eq") || veq(v, "ne") || veq(v, "lt") || veq(v, "le") ||
                veq(v, "gt") || veq(v, "ge")) { cmp_seen = true; continue; }
            if (v.size == 1 && (v.ptr[0] == '+' || v.ptr[0] == '-' || v.ptr[0] == '*' ||
                                v.ptr[0] == '/' || v.ptr[0] == '%')) continue;   // 중위 연산자
        }
        ty_t t = tc_infer(c, k[i], env, nenv);
        if (!have) { acc = t; have = true; }
        else acc = tc_binop(c, acc, t, false, k[i]->line);   // 좌결합 — 짝마다 규칙 적용
    }
    if (cmp_seen) return tk(TK_BOOL);
    return have ? acc : tk(TK_UNKNOWN);
}
// operand run: 1 → itself; expr → island; cast → target scalar; else → call
static ty_t tc_infer_run(tc_ctx_t *c, low_cst_t *const *k, proven_size_t start, proven_size_t n,
                         const tc_var_t *env, proven_size_t nenv) {
    if (n == 0) return tk(TK_UNIT);
    if (n == 1) return tc_infer(c, k[start], env, nenv);
    if (k[start]->kind == LOW_CST_ATOM && k[start]->tok.kw == LOW_KW_EXPR)
        return tc_infer_expr(c, k + start + 1, n - 1, env, nenv);
    if (k[start]->kind == LOW_CST_ATOM) {
        // a fresh borrow: `ref X` / `mut_ref X` (core type conservative, rk known)
        if (veq(k[start]->tok.lex, "ref") || veq(k[start]->tok.lex, "mut_ref")) {
            ty_t t = tk(TK_UNKNOWN);
            t.rk = veq(k[start]->tok.lex, "mut_ref") ? 2 : 1;
            return t;
        }
        // ★ 순수 내장 op 의 **반환 타입**. 이것이 없어서 `index d 0` 이 TK_UNKNOWN 이 되고,
        //   그 위의 `try` 가 보수적으로 통과했다(변이 지표가 그것을 잡아냈다).
        //   보수적으로: 확실히 아는 것만 적는다.
        {
            proven_u8str_view_t h2 = k[start]->tok.lex;
            if (veq(h2, "len") || veq(h2, "count") || veq(h2, "capacity")) {
                for (proven_size_t q = 1; q < n; q++) (void)tc_infer(c, k[start + q], env, nenv);
                return tk_int(64, false);
            }
            if (veq(h2, "index") && n >= 2) {             // ★ 원소 타입은 슬라이스가 정한다
                ty_t sq = tc_infer(c, k[start + 1], env, nenv);
                for (proven_size_t q = 2; q < n; q++) (void)tc_infer(c, k[start + q], env, nenv);
                if (sq.k == TK_SLICE && sq.ebits) {
                    if (sq.eflt) return (ty_t){ .k = TK_FLOAT, .bits = sq.ebits };
                    return tk_int(sq.ebits, sq.esign);
                }
                return tk_int(8, false);                 // 미상 — u8 로 보수적
            }
            // ★ `view_array <스칼라> <byteslice>` — 바이트 슬라이스를 **타입 배열**로 재해석한다.
            //   원소 타입을 슬라이스 ty_t 에 실어(ebits/eflt/esign) index·subslice·for-in·map 등
            //   **모든 소비 자리**가 float/부호를 되살린다. 구조체·미상 원소는 종전대로 무타입으로
            //   흘려보낸다 — SIMD 의 `var xs slice <T> be view_array …` load 핸들은 이 타입을 받는다.
            if (veq(h2, "view_array") && n >= 3 && k[start + 1]->kind == LOW_CST_ATOM) {
                ty_t e = ty_of_word(k[start + 1]->tok.lex);
                for (proven_size_t q = 2; q < n; q++) (void)tc_infer(c, k[start + q], env, nenv);
                if (e.k == TK_INT || e.k == TK_FLOAT) return tc_slice_of_elem(e);
            }
            if (veq(h2, "subslice")) {
                // ★ subslice 는 원소 타입을 **물려준다** — 슬라이스 X 의 원소가 곧 결과의 원소다.
                ty_t sx = (n >= 2) ? tc_infer(c, k[start + 1], env, nenv) : tk(TK_UNKNOWN);
                for (proven_size_t q = 1; q < n; q++) (void)tc_infer(c, k[start + q], env, nenv);
                if (sx.k == TK_SLICE && sx.ebits) return sx;         // 타입 있는 슬라이스는 그대로
                return tk(TK_SLICE);
            }
            // ★★★★★ **잎 낱말의 결과 타입** (2026-09-10, REQ-0014 · WO-0195).
            //   이 언어로 프로그램을 처음 짜 보던 사람이 이렇게 적었다:
            //       let e option u64 . be write_out k 1 ln .     rem write_out 은 option 을 안 낸다
            //   `--check` 는 **초록**이었고 프로그램은 **돌다가 죽었다**. 까닭은 여기다: 검사기가
            //   잎 낱말의 결과 타입을 모르면 `TK_UNKNOWN` 이 되고, 모르는 것은 **무엇과도 호환**이다.
            //   코퍼스를 돌연변이로 재니 선언 **829 자리 중 246 이 검사되지 않았다**
            //   (`scripts/check-decl-types.py`).
            //   ☞ *검사되지 않는 선언은 주석이다. 주석은 틀려도 아무도 안 깨운다.*
            //
            //   ★ 표에는 **실행으로 확인한 것만** 싣는다. 각 낱말을 실제로 돌려 갈랐다:
            //     option 이면 `is_some` 이 돌고, 아니면 그 자리에서 «is_some needs an option» 으로
            //     죽는다. 안 갈린 것은 **안 싣는다** — 여기 잘못 적으면 **멀쩡한 프로그램을
            //     거절**하고, 그것은 안 잡는 것보다 나쁘다.
            //   ★ 문맥이 결과를 정하는 낱말(`index`·`subslice`·`view_array`)은 위에서 따로 본다.
            {
                static const struct { const char *w; unsigned char kind; } LEAF[] = {
                    // ── option 을 낸다 (실패할 수 있다는 사실을 타입이 든다)
                    { "alloc_bytes", 1 }, { "read_in", 1 }, { "env_get", 1 },
                    { "file_open", 1 }, { "file_read", 1 }, { "file_write", 1 },
                    { "file_seek", 1 }, { "file_type", 1 }, { "link_type", 1 },
                    { "dir_open", 1 }, { "dir_read", 1 },
                    { "net_pair", 1 }, { "net_send", 1 }, { "net_recv", 1 },
                    { "net_listen", 1 }, { "net_port", 1 }, { "net_connect", 1 },
                    { "net_accept", 1 },
                    // ── 참거짓을 낸다 (닫기·파일시스템 변경은 됐나 안 됐나뿐이다)
                    { "file_close", 2 }, { "dir_close", 2 }, { "net_close", 2 }, { "same_slice", 2 },
                    { "dir_make", 2 }, { "path_remove", 2 }, { "path_rename", 2 },
                    // ── 정수를 낸다 (쓴 길이 · 해시 · 시계 · 난수 · 비트)
                    { "write_out", 3 }, { "crc32", 3 }, { "hash64", 3 },
                    { "sha256", 3 }, { "sha512", 3 }, { "blake3", 3 },
                    { "time_now", 3 }, { "time_local", 3 }, { "rng_next", 3 },
                    { "random_bytes", 3 }, { "argc", 3 },
                    { "bit_and", 3 }, { "bit_or", 3 }, { "bit_xor", 3 }, { "bit_not", 3 },
                    { "shl", 3 }, { "shr", 3 }, { "wshl", 3 }, { "wshr", 3 },
                    { "rotl", 3 }, { "rotr", 3 },
                    { "clmul_lo", 3 }, { "clmul_hi", 3 },   // ★ 캐리 없는 곱셈 (RFC-0119) — 폭은 피연산자가 말한다
                    // ★★ 암호 잎 — **낸 값은 처리한 바이트 수**다(option 도 참거짓도 아니다).
                    //   근거는 구현 자신이다: VM 의 이 자리들이 전부 `vmv_int` 를 쌓고, C 뒤끝은
                    //   `lw_int(...)` 로 싣는다. 표에 없어서 `let n u64 be aes_ctr …` 같은 선언이
                    //   **비틀어도 초록**이었다(돌연변이 측정 `check-decl-types`). RFC-0122 로 낱말이
                    //   둘 늘면서 그 자리가 하나 더 늘길래, 늘리는 대신 **줄였다**.
                    { "aes_ctr", 3 }, { "ghash", 3 },
                    { "aes_round", 3 }, { "aes_round_last", 3 },
                    { "chacha20", 3 }, { "poly1305", 3 },
                    { "sha384", 3 },
                    { "popcount", 3 }, { "leading_zeros", 3 }, { "trailing_zeros", 3 },
                    { "byte_swap", 3 },
                };
                // ★ RFC-0125 — 계산 잎은 `call_builtin <이름> …` 으로 온다. 머리만 보면
                //   이름을 못 보고, 그러면 선언 타입이 **다시 안 검사된다**(실측 175 → 181).
                //   ⇒ 머리가 `call_builtin` 이면 **다음 원자**를 이름으로 읽고 자리를 하나 민다.
                proven_u8str_view_t hleaf = h2;
                proven_size_t lskip = 1;
                if (veq(h2, "call_builtin") && n >= 2 && k[start + 1]->kind == LOW_CST_ATOM) {
                    hleaf = k[start + 1]->tok.lex; lskip = 2;
                }
                for (proven_size_t li = 0; li < sizeof LEAF / sizeof LEAF[0]; li++) {
                    if (!veq(hleaf, LEAF[li].w)) continue;
                    for (proven_size_t q = lskip; q < n; q++) (void)tc_infer(c, k[start + q], env, nenv);
                    if (LEAF[li].kind == 1) { ty_t w = tk(TK_WRAPPED); w.wrap = 1; return w; }
                    if (LEAF[li].kind == 2) return tk(TK_BOOL);
                    // ★ **폭은 안 말한다**(bits = 0 = 미상). 비트 낱말은 피연산자의 폭을
                    //   물려받으므로 여기서 64 라고 박으면 `u32` 자리를 **틀리게 거절**한다
                    //   (실측: `bit_not a` · `shl 1 5` 를 u32/u8 로 받는 자리 18 곳이 그렇게 걸렸다).
                    //   ⇒ 이 표가 잡으려는 것은 **종류**다: option 인가 · 참거짓인가 · 슬라이스인가.
                    //   폭은 이미 다른 규칙이 본다. 모르는 것을 아는 척하지 않는다.
                    return tk_int(0, false);
                }
            }
            if (veq(h2, "is_ok") || veq(h2, "is_error") || veq(h2, "is_some") ||
                veq(h2, "is_none") || veq(h2, "contains")) {
                for (proven_size_t q = 1; q < n; q++) (void)tc_infer(c, k[start + q], env, nenv);
                return tk(TK_BOOL);
            }
            // ★ `any <mask>` / `all <mask>` — 마스크를 요구한다(정수도 벡터도 아니다).
            if ((veq(h2, "any") || veq(h2, "all")) && n >= 2) {
                ty_t m = tc_infer_run(c, k, start + 1, n - 1, env, nenv);
                if (m.k != TK_UNKNOWN && m.k != TK_NAMED && m.k != TK_MASK)
                    tc_emit(c, "E-TYPE-MASK",
                            "`any`/`all` need a mask (the result of a lanewise comparison) — "
                            "this value is not a mask", k[start]->line);
                return tk(TK_BOOL);
            }
            // ★ `select <mask> <a> <b>` — 첫 인자는 마스크여야 하고, 레인 수가 맞아야 한다.
            if (veq(h2, "select") && n >= 4) {
                ty_t m = tc_infer(c, k[start + 1], env, nenv);
                ty_t va = tc_infer(c, k[start + 2], env, nenv);
                ty_t vb = tc_infer(c, k[start + 3], env, nenv);
                if (m.k != TK_UNKNOWN && m.k != TK_NAMED && m.k != TK_MASK)
                    tc_emit(c, "E-TYPE-MASK",
                            "`select` needs a mask as its first argument (the result of a lanewise "
                            "comparison)", k[start]->line);
                else if (m.k == TK_MASK && va.k == TK_VEC && m.lanes != va.lanes)
                    tc_emit(c, "E-TYPE-LANES",
                            "the mask and the vectors have different lane counts", k[start]->line);
                if (va.k == TK_VEC && vb.k == TK_VEC && va.lanes != vb.lanes)
                    tc_emit(c, "E-TYPE-LANES",
                            "`select` needs the two vectors to have the same lane count",
                            k[start]->line);
                return va;
            }
        }
        // ★ `ok <expr>` / `error <name>` — result 생성자. 안쪽을 **봐야** 한다.
        //   이것을 안 해서 `return ok try index d 0 .` 의 try 가 검사되지 않았다
        //   (try 분기는 try 가 **머리**일 때만 걸린다). 변이 지표가 그것을 잡아냈다.
        if (veq(k[start]->tok.lex, "ok") && n >= 2) {
            (void)tc_infer_run(c, k, start + 1, n - 1, env, nenv);   // payload 를 검사한다
            ty_t w = tk(TK_WRAPPED); w.wrap = 2; return w;           // ★ `ok` 는 result 다
        }
        if (veq(k[start]->tok.lex, "error") && n >= 2) { ty_t w = tk(TK_WRAPPED); w.wrap = 2; return w; }
        // ★ `try <expr>` — 풀 수 있는 것에만 붙는다.
        //   `try` 는 "이것은 실패할 수 있고, 실패하면 전파한다" 는 **선언**이다.
        //   result/option 이 아닌 값에 붙으면 아무 일도 하지 않는다 — 선언이 거짓말이 된다.
        //   (SPEC-MVP §8 의 대표 예제가 실제로 그랬다: read_u16_be 는 u16 을 돌려주는데
        //    본문이 `try read_u16_be …` 라고 적어 놨다. 아무도 확인하지 않았다.)
        if (veq(k[start]->tok.lex, "try") && n >= 2) {
            ty_t inner = tc_infer_run(c, k, start + 1, n - 1, env, nenv);
            if (inner.k == TK_INT || inner.k == TK_FLOAT || inner.k == TK_BOOL || inner.k == TK_SLICE)
                tc_emit(c, "E-TYPE-TRY",
                        "`try` needs a result/option — this value has nothing to unwrap, so the "
                        "`try` does nothing and the claim that it can fail is false", k[start]->line);
            // ★★★ **`try X else_none` / `else_error E` 는 채널 전환이다** — 미지가 아니다.
            //
            //   `try X` 만 있으면 그것은 **payload** 를 낸다(그 타입은 아직 안 쫓는다 — 보수적).
            //   그런데 `else_none` 이 붙으면 결과는 **option** 이고, `else_error` 면 **result** 다.
            //   ★ 그것을 몰라서 이런 코드가 **통과했다**:
            //
            //       fn f output u64 . …  do  return try (g 7) else_none .  end
            //       →  f() = **some 7**      (u64 라고 선언해 놓고 **option 을 냈다**)
            //
            //   **컴파일되고 실행되고 틀린 타입을 냈다** — 그리고 `--check` 는 초록불이었다.
            //   (제네릭을 시험하다 나왔는데, 제네릭과는 **아무 상관도 없는** 옛 구멍이었다.)
            for (proven_size_t q = start + 1; q < start + n; q++)
                if (k[q]->kind == LOW_CST_ATOM &&
                    (veq(k[q]->tok.lex, "else_none") || veq(k[q]->tok.lex, "else_error")))
                    return tk(TK_WRAPPED);   // 채널이 **바뀐다** — option / result 다
            return tk(TK_UNKNOWN);           // payload 타입은 아직 추적하지 않는다(보수적)
        }
        // explicit conversion: `cast <scalar> <expr>` (G2) yields the target type.
        // D12: bool 은 수치가 아니다 — cast 대상이 될 수 없다.
        if (veq(k[start]->tok.lex, "cast") && n >= 3 && k[start + 1]->kind == LOW_CST_ATOM) {
            /* ★★★★★ **한쪽만 막힌 규칙은 막혔다고 믿게 만든다** (2026-09-03, 소유자 결정 A).
             *
             *   그전에는 **대상 타입만** 봤다. 그래서 `cast bool <수>` 는 거절되고
             *   `cast u8 <bool>` 은 통과해 `true` 가 조용히 `1` 이 됐다 — 정본 §6.2.16 (4)
             *   가 막으려던 「어느 쪽이 1 인가」의 결정이 **반대 방향으로 새어 나갔다.**
             *   그 조항의 근거는 *"프로그램이 정할 일이지 언어가 정할 일이 아니다"* 이고,
             *   방향이 있는 규칙은 **양쪽을 다 재야** 그 방향이 참이다(§8.8 과 같은 규율).
             *   ⇒ 원천이 참거짓이어도 거절한다. 수를 오가려면 `if` 로 적는다.
             *   실측(2026-09-02): impl/tests 와 lib 어디에도 bool 을 캐스트하는 자리가
             *     없다 — 곧 이 조임의 비용은 0 이다. */
            ty_t src0 = { 0 };
            bool have_src = false;
            for (proven_size_t i = 2; i < n; i++) {
                ty_t ti = tc_infer(c, k[start + i], env, nenv);
                if (!have_src) { src0 = ti; have_src = true; }
            }
            ty_t tgt = ty_of_word(k[start + 1]->tok.lex);
            if (tgt.k == TK_BOOL)
                tc_emit(c, "E-TYPE-KIND", "bool is not numeric: it cannot be a cast target (use `if`)",
                        k[start + 1]->line);
            else if (have_src && src0.k == TK_BOOL)
                tc_emit(c, "E-TYPE-KIND",
                        "bool is not numeric: it cannot be a cast SOURCE either. Which side is `1` "
                        "is for the PROGRAM to say, not the language — write `if b . do 1 . else 0 .`",
                        k[start + 1]->line);
            return tgt;
        }
        // ★★★★★ **`widen` 은 표에 있는 관계로만 간다** (2026-09-01, 소유자 결정).
        //
        //   정본 §6.2.5 (4) 는 넓히기가 허용되는 관계를 표로 적고 *"여기 없는 조합은
        //   넓히기가 아니며, 적으면 **번역이 거부된다**"* 고 못박는다. 그런데 이 검사기는
        //   `widen` 을 **아예 안 보고** 있었다 — `cast` 는 바로 위에서 보면서.
        //   그래서 `i8 → u32` 도 `u32 → u8`(좁힘)도 `f64 → u32`(갈래 넘기)도 다 통과했다.
        //
        //   ★ 그중 하나는 트랩도 안 했다: `f64 7.9` → `u32` 가 **조용히 7** 을 냈다
        //     (VM·네이티브 둘 다). §6.2.5 (1) 이 *"값을 잃는 암묵적 변환이 없다"* 고 적은
        //     것과 정면으로 부딪히는 자리다 — 소수부가 사라지는데 아무도 말하지 않았다.
        //
        //   ⇒ 표의 네 규칙을 그대로 강제한다. 어기면 **번역이 거부된다**:
        //       uN → uM (N ≤ M) · iN → iM (N ≤ M) · uN → iM (N < M) · f32 → f64
        //     좁히려면 `narrow`(안 맞으면 트랩), 갈래를 넘거나 부호를 바꾸려면 `cast` 다 —
        //     둘 다 **소스에 적히므로** 읽는 사람이 값이 변할 수 있는 자리를 본다(P5).
        //   ☞ *«번역할 때 거부한다» 고 적어 놓고 실행할 때 트랩하면, 그 규범은 반만 참이다.*
        if (veq(k[start]->tok.lex, "widen") && n >= 3 && k[start + 1]->kind == LOW_CST_ATOM
            && !sig_find(c, k[start]->tok.lex)) {
            ty_t tgt = ty_of_word(k[start + 1]->tok.lex);
            ty_t src = tc_infer(c, k[start + 2], env, nenv);
            for (proven_size_t i = 3; i < n; i++) (void)tc_infer(c, k[start + i], env, nenv);
            // ★ 모르면 안 문다 — 폭이 안 실린 값(리터럴·혼합식)은 이 검사의 물음 밖이다.
            //   (거짓양성 하나가 거짓음성 여럿보다 비싸다: 사람이 게이트를 끄게 만든다.)
            bool known = (src.k == TK_INT || src.k == TK_FLOAT) && src.bits
                      && (tgt.k == TK_INT || tgt.k == TK_FLOAT) && tgt.bits;
            if (known) {
                if (src.k != tgt.k) {
                    tc_emit(c, "E-WIDEN-KIND",
                            "widening cannot cross the numeric KIND — integer and floating point "
                            "are different families, and going between them can LOSE the value "
                            "silently (7.9 would become 7). Say it with `cast`, so the place where "
                            "the value may change is written down (the standard, table in the "
                            "types clause)", k[start]->line);
                } else if (tgt.bits < src.bits) {
                    tc_emit(c, "E-WIDEN-NARROW",
                            "this is NARROWING, not widening — the target is smaller than the "
                            "source. `narrow` says so and traps when the value does not fit; "
                            "`widen` promises the value is unchanged", k[start]->line);
                } else if (src.k == TK_INT && src.sign && !tgt.sign) {
                    tc_emit(c, "E-WIDEN-SIGN",
                            "a signed integer cannot widen into an unsigned one — a negative value "
                            "has no place there. Use `cast` if that is what you mean",
                            k[start]->line);
                } else if (src.k == TK_INT && !src.sign && tgt.sign && tgt.bits <= src.bits) {
                    tc_emit(c, "E-WIDEN-SIGN",
                            "an unsigned integer widens into a signed one only when the target is "
                            "STRICTLY wider (uN into iM needs N < M) — at equal width the top of "
                            "the range does not fit", k[start]->line);
                }
            }
            return tgt;
        }
        // ── RFC-0052 D1: 전위 이항 수치 op — 피연산자 규칙이 여기에도 적용된다 ──
        {
            proven_u8str_view_t h = k[start]->tok.lex;
            bool arith = veq(h, "add") || veq(h, "sub") || veq(h, "mul") ||
                         veq(h, "div") || veq(h, "mod") || veq(h, "rem");
            bool cmp   = veq(h, "eq") || veq(h, "ne") || veq(h, "lt") ||
                         veq(h, "le") || veq(h, "gt") || veq(h, "ge");
            // ★★★★★ **초월 함수는 부동소수 전용이다** (정본 §6.3.9(1) · 결함 노트 #37, 2026-09-16).
            //   `pow a 2`(a 가 u64)가 `--check` 를 지나 **틀린 값**을 냈다 — `pow 5 2` 가 5 였고
            //   VM·네이티브가 같이 그랬다. 형제들(`sqrt`·`sin`·`exp`·`log`)은 실행 중에 "float-only"
            //   라고 멈추는데 `pow` 만 조용히 답을 냈다. 조용히 틀린 답이 가장 나쁜 갈래다.
            //   ⇒ 정수 피연산자를 **번역에서** 거절한다. 실행까지 갈 이유가 없다.
            {
                bool transc = veq(h, "pow") || veq(h, "sqrt") || veq(h, "sin") || veq(h, "cos") ||
                              veq(h, "exp") || veq(h, "log") || veq(h, "fmod");
                if (transc && n >= 2 && !sig_find(c, h)) {
                    for (proven_size_t q = 1; q < n; q++) {
                        c->quiet++;
                        ty_t o = tc_infer(c, k[start + q], env, nenv);
                        c->quiet--;
                        if (o.k == TK_INT) {
                            tc_emit(c, "E-TYPE-KIND",
                                    "this is a floating-point-only operation (canon §6.3.9) and it was "
                                    "given an INTEGER. `pow` used to answer anyway — and the answer was "
                                    "wrong (`pow 5 2` gave 5 on both backends), which is the worst kind "
                                    "of wrong: quiet. Convert first (`cast f64 n`), or use repeated "
                                    "multiplication for an integer power", k[start]->line);
                            break;
                        }
                    }
                }
            }
            if ((arith || cmp) && n == 3 && !sig_find(c, h)) {   // 사용자 op 가 가리지 않을 때만
                ty_t a = tc_infer(c, k[start + 1], env, nenv);
                ty_t b = tc_infer(c, k[start + 2], env, nenv);
                return tc_binop(c, a, b, cmp, k[start]->line);
            }
            // ★★★ **논리 op 은 두 경로로 들어온다** — `expr` 섬(중위)과 **여기**(전위 호출).
            //   섬 쪽만 고쳤더니 `and 1 1` 이 그대로 통과했다: 같은 규칙을 두 자리에
            //   걸어야 하는데 한 자리만 걸었다. **한 자리를 고치면 짝이 되는 자리를 늘 물어라**
            //   — 이 저장소가 이미 두 번 배운 것이다(mod 부호 · 슬라이스 원소 폭).
            bool logic = veq(h, "and") || veq(h, "or") || veq(h, "not");
            if (logic && n >= 2 && !sig_find(c, h)) {
                for (proven_size_t q = 1; q < n; q++) {
                    c->quiet++;   // ★ 타입만 묻는다(위 tc_ctx_t.quiet)
                    ty_t o = tc_infer(c, k[start + q], env, nenv);
                    c->quiet--;
                    if (o.k == TK_INT || o.k == TK_FLOAT)
                        tc_emit(c, "E-TYPE-LOGICAL",
                                "`and`/`or`/`not` take bool, not a number — this language has no "
                                "truthiness (D12: no implicit int↔bool). Say what the test is: "
                                "`ne x 0` instead of `x`", k[start + q]->line);
                }
                return tk(TK_BOOL);
            }
        }
        // ★★★ **감싸개를 벗기면 알맹이가 나온다** (RFC-0104 §8-2). 위에서 감싸개가 알맹이의
        //   이름을 실었으니 여기서 그것을 돌려준다. 안 그러면 `some_value h` 는 TK_UNKNOWN 이고,
        //   모르는 것은 물지 않으므로 **인자 검사가 조용히 빈다**.
        {
            proven_u8str_view_t uh = k[start]->tok.lex;
            if ((veq(uh, "some_value") || veq(uh, "ok_value")) && n == 2 && !sig_find(c, uh)) {
                ty_t w = tc_infer(c, k[start + 1], env, nenv);
                if (w.k == TK_WRAPPED && w.nname.size) {
                    ty_t pv = tk(TK_NAMED); pv.nname = w.nname; pv.targ = w.targ;
                    return pv;
                }
                return tk(TK_UNKNOWN);
            }
        }
        // call: head + args
        const tc_sig_t *s = sig_find(c, k[start]->tok.lex);
        if (s) {
            for (proven_size_t i = 0; i + 1 < n && i < s->nparams; i++) {
                ty_t at = tc_infer(c, k[start + 1 + i], env, nenv);
                tc_flag(c, compat(s->params[i], at),
                        "E-TYPE-ARG", "call argument type does not match parameter", k[start + 1 + i]->line);
            }
            return s->ret;
        }
    }
    return tk(TK_UNKNOWN);
}
static ty_t tc_infer(tc_ctx_t *c, const low_cst_t *nd, const tc_var_t *env, proven_size_t nenv) {
    if (!nd) return tk(TK_UNKNOWN);
    if (nd->kind == LOW_CST_ATOM) {
        switch (nd->tok.kind) {
            case LOW_TOK_NUMBER: return ty_of_number(nd->tok.lex);
            case LOW_TOK_STRING: case LOW_TOK_HEREDOC: return tk(TK_NAMED);
            default: break;
        }
        if (nd->tok.kw == LOW_KW_TRUE || nd->tok.kw == LOW_KW_FALSE) return tk(TK_BOOL);
        if (nd->tok.kw == LOW_KW_NONEVAL || nd->tok.kw == LOW_KW_UNIT) return tk(TK_UNKNOWN);
        bool f; ty_t t = env_find(env, nenv, nd->tok.lex, &f);
        if (f) return t;
        // ★★★ **인자 없는 호출도 타입이 있다** (X-0010 · 2026-08-15). `be dist .` 처럼 인자가
        //   하나도 없는 op 호출은 그냥 ATOM 이라, 환경에 없으면 **TK_UNKNOWN** 으로 떨어졌다 —
        //   즉 0인자 op 의 **반환 타입이 통째로 사라졌다**. 그래서 `var t seconds be dist .`
        //   (dist 는 meters 를 돌려준다)가 검사기의 눈에 *"모르는 것"* 이었다.
        //   ★ 환경이 **먼저**다: 같은 이름의 지역이 있으면 그것이 이 자리의 뜻이다.
        {
            const tc_sig_t *s0 = sig_find(c, nd->tok.lex);
            if (s0 && s0->nparams == 0) return s0->ret;
        }
        return tk(TK_UNKNOWN);
    }
    if (nd->kind == LOW_CST_GROUP) return nd->nkids ? tc_infer(c, nd->kids[0], env, nenv) : tk(TK_UNKNOWN);
    if (nd->kind == LOW_CST_ACCESS) return tk(TK_UNKNOWN);  // struct/slice element typing: future
    if (nd->kind == LOW_CST_FORM) return tc_infer_run(c, nd->kids, 0, nd->nkids, env, nenv);
    return tk(TK_UNKNOWN);
}

// declared type of a var: `[mut] [ref|mut_ref] core …` (words up to `be`/form end)
// `rr` 가 널이 아니면 range 선언의 적합성 결과를 돌려준다(호출자가 진단을 낸다).
static ty_t ty_of_decl_r(const low_cst_t *f, proven_size_t start, proven_size_t end, rng_res_t *rr) {
    ty_t t = { .k = TK_UNKNOWN };
    if (rr) *rr = RNG_NONE;
    bool mut_seen = false;
    for (proven_size_t i = start; i < end; i++) {
        if (f->kids[i]->kind != LOW_CST_ATOM) break;
        proven_u8str_view_t w = f->kids[i]->tok.lex;
        if (veq(w, "mut")) { mut_seen = true; continue; }
        if (veq(w, "unsafe_ptr")) continue;   // ★ 생 포인터 한정자 — 알맹이는 그 뒤 구조체 타입이다
        if (veq(w, "ref"))     { t.rk = 1; continue; }
        if (veq(w, "mut_ref")) { t.rk = 2; continue; }
        // ★ `slice <T>` — **원소 타입이 타입의 일부다.**
        //   이것을 안 실어서 `index s i` 가 언제나 u8 이었다. slice u32 의 원소는 u32 다.
        if ((veq(w, "slice") || veq(w, "array")) && i + 1 < end && i + 1 < f->nkids &&
            f->kids[i + 1]->kind == LOW_CST_ATOM) {
            ty_t e = ty_of_word(f->kids[i + 1]->tok.lex);
            if (e.k == TK_INT || e.k == TK_FLOAT) {
                ty_t sl = tk(TK_SLICE);
                sl.ebits = e.bits; sl.esign = e.sign; sl.eflt = (e.k == TK_FLOAT);
                sl.rk = t.rk; sl.is_mut = mut_seen;
                return sl;
            }
        }
        // ★ `mask <lanes>` — 레인 수가 타입의 일부다.
        if (veq(w, "mask") && i + 1 < end && i + 1 < f->nkids &&
            f->kids[i + 1]->kind == LOW_CST_ATOM) {
            proven_i64 ln;
            if (tc_int_lit(f->kids[i + 1]->tok.lex, &ln) && ln > 0 && ln <= 64) {
                ty_t m = { .k = TK_MASK, .lanes = (proven_u8)ln };
                m.rk = t.rk; m.is_mut = mut_seen;
                return m;
            }
        }
        // ★ `vec <elem> <lanes>` — 레인 수와 원소 종류가 타입의 일부다.
        if (veq(w, "vec") && i + 2 < end && i + 2 < f->nkids &&
            f->kids[i + 1]->kind == LOW_CST_ATOM && f->kids[i + 2]->kind == LOW_CST_ATOM) {
            ty_t ew = ty_of_word(f->kids[i + 1]->tok.lex);
            proven_i64 ln;
            if ((ew.k == TK_INT || ew.k == TK_FLOAT) && tc_int_lit(f->kids[i + 2]->tok.lex, &ln) &&
                ln > 0 && ln <= 64) {
                ty_t v = { .k = TK_VEC, .bits = ew.bits, .sign = ew.sign,
                           .lanes = (proven_u8)ln, .vflt = (ew.k == TK_FLOAT) };
                v.rk = t.rk; v.is_mut = mut_seen;
                return v;
            }
        }
        ty_t core; proven_size_t used;
        rng_res_t r = ty_of_range_at(f, i, end, &core, &used);   // ★ RFC-0055 D7
        if (r != RNG_NONE) { if (rr) *rr = r; core.rk = t.rk; core.is_mut = mut_seen; return core; }
        core = ty_of_word(w);
        core.rk = t.rk;
        core.is_mut = mut_seen;
        if (core.k == TK_NAMED) {          // ★ 머리 이름과 틀 인자를 싣는다 (X-0009)
            core.nname = w;
            proven_u32 h = 2166136261u;    // FNV-1a — 인자 낱말들을 순서 그대로
            bool any = false;
            for (proven_size_t j = i + 1; j < end && j < f->nkids; j++) {
                if (f->kids[j]->kind != LOW_CST_ATOM) break;
                proven_u8str_view_t a = f->kids[j]->tok.lex;
                if (a.size == 0) continue;
                any = true;
                for (proven_size_t b = 0; b < a.size; b++) { h ^= (proven_u32)a.ptr[b]; h *= 16777619u; }
                h ^= 0x2eu; h *= 16777619u;    // 낱말 경계
            }
            core.targ = any ? (h ? h : 1u) : 0u;
        }
        // ★★★ **감싸개도 알맹이의 이름을 나른다** (RFC-0104 §8-2 · 2026-08-28).
        //   `option (handle pa)` 와 `option (handle pb)` 는 지금까지 **둘 다 그냥 TK_WRAPPED**
        //   였다. 그래서 `some_value ha` 를 **인라인으로** 쓰면 알맹이의 타입이 통째로 사라졌고
        //   (TK_UNKNOWN), 모르는 것은 안 무는 규율에 따라 **풀 A 의 핸들이 풀 B 로** 흘렀다.
        //   ☆ 바인딩을 거치면(`let hh handle pa . be some_value ha .`) 물었다 — 검사가 없던 것이
        //     아니라 **타입이 거기까지 안 갔다.**
        //
        //   ★★ 괄호 안의 모양을 **있는 그대로** 읽어야 한다(실측 2026-08-28): 단형화는
        //     `(pool.handle pa)` 를 **반만** 접는다 — 머리 토큰은 `pool.handle` 그대로이고
        //     **인자 자리**가 `handle#pa` 로 바뀐다(형제 쌍을 접는 규칙이 FORM 의 kids 에서
        //     돈 결과다). 그래서 *머리 이름 + 인자 해시* 로 읽으면 같은 타입이 두 철자를 갖고
        //     멀쩡한 프로그램이 빨개진다. **접힌 이름이 있으면 그것이 곧 그 타입의 이름이다.**
        if (core.k == TK_WRAPPED && i + 1 < end && i + 1 < f->nkids) {
            const low_cst_t *pl = f->kids[i + 1];
            if (pl->kind == LOW_CST_GROUP && pl->nkids) pl = pl->kids[0];
            proven_u8str_view_t phead = { 0 };
            const low_cst_t *const *pk = NULL; proven_size_t pn = 0;
            if (pl->kind == LOW_CST_ATOM) { phead = pl->tok.lex; }
            else if (pl->kind == LOW_CST_FORM) {
                phead = pl->tok.lex;
                pk = (const low_cst_t *const *)pl->kids; pn = pl->nkids;
            }
            // ★ **좁게 문다**: 알맹이가 *단형화된 인스턴스 이름*(`handle#pa` — `#` 이 그
            //   표식이다)일 때만 싣는다. 넓게 실었더니 `result (owned pending) io_error` 처럼
            //   알맹이가 여러 낱말인 자리에서 **머리 이름 + 인자 해시**가 선언 쪽과 안 맞아
            //   멀쩡한 프로그램이 빨개졌다(실측: emit.low · 골든 여덟 줄). 모르면 안 무는
            //   쪽이 옳다 — 여기서 필요한 것은 **브랜드가 다른 두 인스턴스**를 가르는 것뿐이다.
            //   ☞ 단형화는 `(pool.handle pa)` 를 **반만** 접는다: 머리 토큰은 `pool.handle` 인 채
            //     남고 **인자 자리**가 `handle#pa` 가 된다. 그래서 그 접힌 이름을 찾아 쓴다.
            proven_u8str_view_t folded = { 0 };
            if (pl->kind == LOW_CST_ATOM) {
                for (proven_size_t b = 0; b < phead.size; b++)
                    if (phead.ptr[b] == (proven_byte_t)'#') { folded = phead; break; }
            }
            for (proven_size_t j = 0; j < pn && !folded.size; j++) {
                if (!pk[j] || pk[j]->kind != LOW_CST_ATOM) continue;
                proven_u8str_view_t a = pk[j]->tok.lex;
                for (proven_size_t b = 0; b < a.size; b++)
                    if (a.ptr[b] == (proven_byte_t)'#') { folded = a; break; }
            }
            if (folded.size) { core.nname = folded; core.targ = 0u; }
        }
        return core;
    }
    return t;
}
static bool tname_known(tc_ctx_t *c, proven_u8str_view_t v);
// ★★★★★ **이름은 받았는데 뜻이 없는 낱말** (2026-08-21, `lib/http.low` 이 밟았다).
//
//   위 `ty_is_builtin_word` 의 목록에는 두 종류가 섞여 있다: **뜻이 있는 것**(`u64`·`slice`…)과
//   **이름만 받는 것**(`str`·`shared_read`…). 뒤엣것은 옆 주석이 스스로 적어 두었다 —
//   *"이름은 받는다. 의미는 아직 없고, 그것은 W-NOT-YET 로 말한다."*
//   ★ 그런데 **말하지 않고 있었다.** 그 결과:
//     `lib/http.low` 을 `input b str .` 로 쓰자 `--check` 는 **초록**인데 21 op 중 **16 개가
//     해석기로 떨어졌다**(~80배). 하강이 그 이름의 뜻을 모르기 때문이고, 아무도 안 울었다.
//     ☞ **조용한 80 배**다. 성능은 조용히 사라지고 답은 맞으므로 오라클도 못 본다.
//   ⇒ 뜻이 없으면 **말한다**. 그리고 사용자가 `type str slice u8 .` 로 **직접 선언했다면**
//     그때는 뜻이 있으므로 말하지 않는다(`lib/str.low` 이 그렇게 쓴다).
static bool tname_user_declared(tc_ctx_t *c, proven_u8str_view_t v);
static bool ty_is_name_only(proven_u8str_view_t v) {
    static const char *k[] = {
        // 뜻이 없다 — 하강이 이 이름들을 슬라이스/스칼라로 못 읽는다.
        "str", "string", "char", "byte", "bytes_view", "dyn",
        // SPEC-008 §74 의 공유 상태 타입 — 이름만 받는다고 위에 적혀 있다.
        "shared_read", "lock", "rwlock", "atomic",
        // ★★★ **문법 부록 A.9 의 낱말 넷** (결함 노트 #80 · RFC-0115 §8-10, 2026-09-17).
        //   `list`·`raw`·`addr`·`rng` 는 «쓸 수 있는 타입 낱말» 로 실려 있으나 **뜻을 정한
        //   조항이 정본에 없다** — `input x list u64 .` 가 경고 없이 통과했다. 하강은 이
        //   이름들을 슬라이스로도 스칼라로도 못 읽으므로 그 서명의 op 은 해석기로 떨어진다.
        //   ☞ `addr`·`raw` 는 **연산자로는** 뜻이 있다(`addr x` — 주소를 얻는다, unsafe 효과).
        //     여기서 막는 것은 **타입 자리**뿐이고, `cap rng` 는 위의 after_cap 이 빼 준다.
        "list", "raw", "addr", "rng",
        // ★★★ **권능의 종류 여섯** (RFC-0115 §8-26, 2026-09-17). `cap` 뒤에서만 뜻이 있는 낱말인데
        //   홀로 타입 자리에 서도 아무 말이 없었다. `rng` 만 말하고 다섯은 조용한 것이 가장 나쁘다 —
        //   읽는 사람이 그 차이에 뜻이 있다고 여긴다. 위의 after_cap 이 `cap <종류>` 를 빼 주므로
        //   여기 걸리는 것은 **홀로 선 것**뿐이다.
        "clock", "device", "file_system", "net", "tty",
    };
    for (proven_size_t i = 0; i < sizeof k / sizeof k[0]; i++) if (veq(v, k[i])) return true;
    return false;
}
// ★ 선언 자리의 타입 낱말이 **무언가를 가리키는지** 확인한다.
//   `input a no_such_type .` 이 조용히 통과했다 — 이름이 아무것도 안 가리키는데 아무도 안 봤다.
// 여러 낱말 타입의 머리인가(enum 갈래 검사가 IR 에 넘기는 모양)
static bool ty_is_head_word(proven_u8str_view_t w) {
    static const char *H[] = { "owned","ref","mut_ref","mut","slice","vec","mask","option","result","map","array","range","cap" };
    for (proven_size_t i = 0; i < sizeof H / sizeof H[0]; i++)
        if (proven_u8str_view_eq(w, proven_u8str_view_from_cstr(H[i]))) return true;
    return false;
}
static void tc_check_tnames(tc_ctx_t *c, const low_cst_t *f, proven_size_t from, proven_size_t to) {
    for (proven_size_t i = from; i < to && i < f->nkids; i++) {
        if (f->kids[i]->kind != LOW_CST_ATOM) continue;
        const low_token_t *t = &f->kids[i]->tok;
        if (t->kind != LOW_TOK_IDENT) continue;      // 숫자·기호는 타입 이름이 아니다
        if (t->kw != LOW_KW_NONE) continue;          // 키워드
        if (tname_known(c, t->lex)) {
            // ★ 아는 이름이어도 **뜻이 없으면** 말한다 — 위 문단 참조.
            //   `cap <kind>` 자리는 뺀다(`cap net` 의 `net` 은 타입이 아니라 종류다).
            bool after_cap = (i > from && f->kids[i-1]->kind == LOW_CST_ATOM &&
                proven_u8str_view_eq(f->kids[i-1]->tok.lex, proven_u8str_view_from_cstr("cap")));
            if (!after_cap && ty_is_name_only(t->lex) && !tname_user_declared(c, t->lex))
                tc_warn(c, "W-NOT-YET",
                        "this type NAME is accepted but carries NO MEANING yet: lowering cannot "
                        "read it, so every op in its signature falls to the interpreter (~80x). "
                        "The answer stays right, so no oracle will ever see this. Declare it "
                        "(`type str slice u8 .`) or write the underlying type",
                        t->line ? t->line : f->line);
            continue;
        }
        // ★★★ `cap <kind>` — **capability 타입**(RFC-0011 · RFC-0043 D1).
        //   `cap allocator` 의 `allocator` 는 **타입 이름이 아니라 capability 의 종류**다.
        //   그것을 몰라서 `input a cap allocator .` 이 **E-TYPE-UNDEF** 를 맞았다 —
        //   문법에 있는 형태를 *"네 프로그램이 틀렸다"* 고 거절했다(또 하나의 오진).
        if (i > from && f->kids[i-1]->kind == LOW_CST_ATOM &&
            proven_u8str_view_eq(f->kids[i-1]->tok.lex, proven_u8str_view_from_cstr("cap")))
            continue;
        tc_emitf(c, "E-TYPE-UNDEF", t->line ? t->line : f->line,
                 "`%.*s` is not a type — no `type` / `struct` / `enum` declares it, and it is not a built-in type",
                 (int)t->lex.size, (const char *)t->lex.ptr);
    }
}
// range 선언의 오류를 진단으로 낸다. **선언 자체가 거짓말이면 프로그램이 서지 않는다.**
static void tc_check_rng(tc_ctx_t *c, rng_res_t r, proven_u32 line) {
    if (r == RNG_EMPTY)
        tc_emit(c, "E-TYPE-RANGE", "empty range: the low bound is above the high bound", line);
    else if (r == RNG_OVERFLOW)
        tc_emit(c, "E-TYPE-RANGE", "the declared range does not fit the declared type (widen the type, or narrow the range)", line);
    else if (r == RNG_KIND)
        tc_emit(c, "E-TYPE-RANGE", "a range must refine an integer type (floats have no range type)", line);
}

// 한 절의 타입 낱말이 끝나는 자리 — **다음 절의 시작.**
// ★ requires/ensures/errors/tests/access/parallel 에서도 멈춰야 한다.
//   안 그러면 계약 절의 낱말(le · a · 200 …)까지 타입 이름으로 오인한다.
__attribute__((unused)) static proven_size_t tc_clause_end(const low_cst_t *f, proven_size_t from) {
    // ★ 하나의 어휘 — 목록을 각자 갖고 있었고, **갈렸다**(satisfies 를 셋이 몰랐다).
    proven_size_t i = from;
    while (i < f->nkids && !(f->kids[i]->kind == LOW_CST_ATOM &&
                             low_is_clause_word(f->kids[i]->tok.lex))) i++;
    return i;
}
// find the index of a keyword atom among a form's kids
static proven_size_t kw_index(const low_cst_t *f, low_kw_t kw) {
    for (proven_size_t i = 0; i < f->nkids; i++)
        if (f->kids[i]->kind == LOW_CST_ATOM && f->kids[i]->tok.kw == kw) return i;
    return f->nkids;
}

// 내장 타입 낱말 — 이것들은 선언이 필요 없다.
static bool ty_is_builtin_word(proven_u8str_view_t v) {
    static const char *k[] = {
        "bool","u8","i8","u16","i16","u32","i32","u64","i64","usize","isize","f32","f64",
        "slice","array","list","result","option","stack","set","region","ref","mut_ref",
        // ★ `segments T` — 흩어진 조각 여럿을 **한 값으로** (RFC-0104 §8-8, 서명 2026-08-29).
        //   타입 낱말 하나가 늘었다. 그 값은 백업 슬라이스와 (at, n) 서술자 슬라이스 두 칸이고,
        //   `view_segments`·`segs`·`seg` 로만 짓고 읽는다.
        "segments",
        "owned","raw","addr","mmio","bitset","cap","vec","mask","range","mut","void","unit",
        "unsafe_ptr",   // ★ 생 포인터 한정자(RFC-0066 가족) — mut/owned 처럼 타입 앞에 붙는 낱말
        "unsafe_fn",    // ★ 콜백 타입(RFC-0066) — 씨에 함수 포인터로. 값은 `unsafe_fn <op>`.
        "str","string","char","byte","bytes_view","dyn","fn","self",
        // ★ SPEC-008 §74 의 **공유 상태 타입**: "공유 상태는 shared_read t / lock t / rwlock t /
        //   atomic t / actor t 중 **하나로만**". 어휘에 없어서 "E-TYPE-UNDEF: 없는 타입" —
        //   즉 *네 프로그램이 틀렸다* 로 거절됐다. 명세에 있는 타입인데.
        //   이름은 받는다. **의미(level-3 규율)는 아직 없고, 그것은 W-NOT-YET 로 말한다.**
        "shared_read","lock","rwlock","atomic",
        // ★ SPEC-004 §19 — `cap k` 의 **종류**: file_system·net·clock·rng·device(RFC-0011).
        //   `input fs cap file_system .` (SPEC-006 §53) 가 **명세의 예제**인데
        //   `file_system` 을 타입으로 읽어 "E-TYPE-UNDEF: 없는 타입" 이라 했다 — 오진.
        "file_system","net","clock","rng","device",
        "tty",   // ★ 터미널 (2026-07-26) — raw 모드·키 입력·화면 크기
    };
    for (proven_size_t i = 0; i < sizeof k / sizeof k[0]; i++) if (veq(v, k[i])) return true;
    return false;
}
static bool tname_user_declared(tc_ctx_t *c, proven_u8str_view_t v) {
    for (proven_size_t i = 0; i < c->ntnames; i++)
        if (proven_u8str_view_eq(c->tnames[i], v)) return true;
    return false;
}
static bool tname_known(tc_ctx_t *c, proven_u8str_view_t v) {
    if (ty_is_builtin_word(v)) return true;
    for (proven_size_t i = 0; i < c->ntnames; i++)
        if (proven_u8str_view_eq(c->tnames[i], v)) return true;
    // ★ 모듈 한정 타입 `M.Type` (RFC-0011): 점 하나면 뒤 마디를 타입으로 본다(외부 모듈 타입 한정 참조).
    //   M 이 진짜 import 모듈인지·Type 이 export 됐는지는 가시성이 따로 강제; 여기선 타입 존재만.
    proven_size_t dot = v.size, nd = 0;
    for (proven_size_t i = 0; i < v.size; i++)
        if (v.ptr[i] == (proven_u8)'.') { if (dot == v.size) dot = i; nd++; }
    if (nd == 1 && dot > 0 && dot + 1 < v.size) {
        proven_u8str_view_t tail = { .ptr = v.ptr + dot + 1, .size = v.size - dot - 1 };
        if (ty_is_builtin_word(tail)) return true;
        for (proven_size_t i = 0; i < c->ntnames; i++)
            if (proven_u8str_view_eq(c->tnames[i], tail)) return true;
    }
    return false;
}
static const tc_struct_t *struct_find(tc_ctx_t *c, proven_u8str_view_t name) {
    for (proven_size_t i = 0; i < c->nstrs; i++)
        if (proven_u8str_view_eq(c->strs[i].name, name)) return &c->strs[i];
    return NULL;
}
// ★ `make T do <field> <value> . … end` — 선언과 대조한다.
static void tc_check_make(tc_ctx_t *c, const low_cst_t *form,
                          const tc_var_t *env, proven_size_t nenv) {
    if (!form || form->kind != LOW_CST_FORM || form->nkids < 2 ||
        form->kids[0]->kind != LOW_CST_ATOM ||
        form->kids[form->nkids - 1]->kind != LOW_CST_BLOCK) return;
    const tc_struct_t *st = struct_find(c, form->kids[0]->tok.lex);
    if (!st) return;                       // 모르는 타입 — 보수적으로 넘긴다
    const low_cst_t *blk = form->kids[form->nkids - 1];
    bool given[TC_MAXF] = { false };
    for (proven_size_t i = 0; i < blk->nkids; i++) {
        const low_cst_t *fld = blk->kids[i];
        if (fld->kind != LOW_CST_FORM || fld->nkids < 2 || fld->kids[0]->kind != LOW_CST_ATOM) continue;
        proven_u8str_view_t fn = fld->kids[0]->tok.lex;
        proven_size_t at = st->nf;
        for (proven_size_t q = 0; q < st->nf; q++)
            if (proven_u8str_view_eq(st->fname[q], fn)) { at = q; break; }
        if (at == st->nf) {                // ★ 선언에 없는 필드
            tc_emit(c, "E-TYPE-FIELD",
                    "this struct has no such field (the declaration does not mention it)", fld->line);
            continue;
        }
        given[at] = true;
        ty_t actual = tc_infer_run(c, fld->kids, 1, fld->nkids - 1, env, nenv);
        tc_flag(c, compat(st->fty[at], actual),
                "E-TYPE-FIELD", "the field's value does not match its declared type", fld->line);
    }
    for (proven_size_t q = 0; q < st->nf; q++)
        if (!given[q])                     // ★ 빠뜨린 필드
            tc_emit(c, "E-TYPE-FIELD",
                    "a declared field is missing from this `make` (every field must be given)",
                    form->line);
}
// 본문을 훑으며 make 리터럴을 찾는다.
static void tc_walk_makes(tc_ctx_t *c, const low_cst_t *nd,
                          const tc_var_t *env, proven_size_t nenv) {
    if (!nd) return;
    // ★★★ 2026-09-14 — **읽는 `field` 도 칸이 있어야 한다.** 쓰기(`set (field q x) v`)만 검사해서 `field q y` 가
    //   `--check` 를 통과하고 실행 중에야 `E-VM-FIELD` 로 멈췄다. 받는 값의 struct 를 아는 자리에서만 문다(보수적).
    for (proven_size_t j = 0; j + 2 < nd->nkids; j++) {
        if (nd->kids[j]->kind != LOW_CST_ATOM || !veq(nd->kids[j]->tok.lex, "field")) continue;
        const low_cst_t *rv = nd->kids[j + 1], *fnm = nd->kids[j + 2];
        if (rv->kind != LOW_CST_ATOM || fnm->kind != LOW_CST_ATOM || fnm->tok.kind != LOW_TOK_IDENT) continue;
        bool fnd; ty_t rt = env_find(env, nenv, rv->tok.lex, &fnd);
        if (!fnd || rt.k != TK_NAMED) continue;
        const tc_struct_t *st = struct_find(c, rt.nname);
        if (!st) continue;
        bool has = false;
        for (proven_size_t q = 0; q < st->nf; q++) if (proven_u8str_view_eq(st->fname[q], fnm->tok.lex)) has = true;
        if (has) continue;
        char fl[160]; size_t fo = 0; fl[0] = 0;
        for (proven_size_t q = 0; q < st->nf && fo + 1 < sizeof fl; q++) {
            int w = snprintf(fl + fo, sizeof fl - fo, "%s%.*s", q ? ", " : "", (int)st->fname[q].size, (const char *)st->fname[q].ptr);
            if (w < 0) break;
            fo += (size_t)w;
        }
        tc_emitf(c, "E-TYPE-FIELD", fnm->tok.line ? fnm->tok.line : nd->line,
                 "struct `%.*s` has no field `%.*s` — its fields are: %s",
                 (int)st->name.size, (const char *)st->name.ptr, (int)fnm->tok.lex.size, (const char *)fnm->tok.lex.ptr, fl);
    }
    for (proven_size_t j = 0; j + 1 < nd->nkids; j++) {
        if (nd->kids[j]->kind == LOW_CST_ATOM && veq(nd->kids[j]->tok.lex, "make")) {
            const low_cst_t *arg = nd->kids[j + 1];
            if (arg->kind == LOW_CST_GROUP && arg->nkids == 1) arg = arg->kids[0];
            tc_check_make(c, arg, env, nenv);
        }
    }
    for (proven_size_t j = 0; j < nd->nkids; j++) tc_walk_makes(c, nd->kids[j], env, nenv);
}

// `return <값>` 의 값을 op 의 `output` 에 맞댄다 — 문장 `return` 과 guard 의 `else return` 이 같은 규칙을 쓴다.
static void tc_check_return(tc_ctx_t *c, low_cst_t *const *kids, proven_size_t from, proven_size_t n,
                            const tc_var_t *env, proven_size_t nenv, ty_t ret, proven_u32 line) {
    ty_t actual = tc_infer_run(c, kids, from, n, env, nenv);
    tc_flag_types(c, compat(ret, actual),
            "E-TYPE-RETURN", "the returned value does not match the op's `output`", ret, actual, line);
}

static void tc_check_body(tc_ctx_t *c, const low_cst_t *blk, tc_var_t *env, proven_size_t *nenv, ty_t ret) {
    if (!blk) return;
    for (proven_size_t i = 0; i < blk->nkids; i++) {
        const low_cst_t *f = blk->kids[i];
        if (f->kind != LOW_CST_FORM || f->nkids == 0 || f->kids[0]->kind != LOW_CST_ATOM) continue;
        low_kw_t kw = f->kids[0]->tok.kw;
        tc_walk_makes(c, f, env, *nenv);      // ★ 이 문장 안의 make 리터럴을 검사한다
        // ★ `let` 은 타입체크를 **통째로 빠져나가고 있었다.** `var` 만 검사됐다.
        //   `let v u8 be <i32>` 도, `let v bool be <u8>` 조차도 조용히 통과했다.
        //   같은 선언인데 한쪽만 검사한 것은 그냥 빠뜨린 것이다. 같은 규칙을 건다.
        if ((kw == LOW_KW_VAR || kw == LOW_KW_LET) && f->nkids >= 3) {
            proven_size_t be = kw_index(f, LOW_KW_BE);
            rng_res_t rr;
            ty_t declared = ty_of_decl_r(f, 2, be < f->nkids ? be : f->nkids, &rr);
            tc_check_rng(c, rr, f->line);   // ★ RFC-0055 D7
            ty_t actual = (be + 1 < f->nkids)
                            ? tc_infer_run(c, f->kids, be + 1, f->nkids - be - 1, env, *nenv)
                            : tk(TK_UNKNOWN);
            // ★★ **split binding** — `var v vec u32 4 . be <expr> .` 에서 타입의 닫는 `.` 가
            //   var 폼을 **먼저 닫아 버려서**, `be <expr> .` 이 **별개의 문장**이 된다.
            //   IR 은 이것을 알고 처리한다(low_ir.c: "split binding: 타입은 f 에 있다").
            //   그런데 **타입체커는 몰랐다** — 그래서 그런 선언의 **초기화식이 아예 검사되지
            //   않았다.** vec 선언은 전부 이 모양이다. 뒤 문장을 이어 본다.
            if (be >= f->nkids && i + 1 < blk->nkids) {
                const low_cst_t *nx = blk->kids[i + 1];
                if (nx->kind == LOW_CST_FORM && nx->nkids >= 2 &&
                    nx->kids[0]->kind == LOW_CST_ATOM && nx->kids[0]->tok.kw == LOW_KW_BE)
                    actual = tc_infer_run(c, nx->kids, 1, nx->nkids - 1, env, *nenv);
            }
            if (declared.rk == 0)   // ref initializers are conservative (value = a borrow)
                tc_flag_types(c, compat(declared, actual),
                        (kw == LOW_KW_LET) ? "E-TYPE-LET" : "E-TYPE-VAR",
                        "the initializer's type does not match the declared type", declared, actual, f->line);
            if (*nenv < TC_MAXENV) { env[*nenv].name = f->kids[1]->tok.lex; env[(*nenv)++].ty = declared; }
        } else if (kw == LOW_KW_RETURN) {
            tc_check_return(c, f->kids, 1, f->nkids - 1, env, *nenv, ret, f->line);
        } else if (kw == LOW_KW_FOR && f->nkids >= 3 && f->kids[1]->kind == LOW_CST_ATOM) {
            // CST: [for, <var>, <seq…>, BLOCK] — 파서가 `in` 마커를 떨어뜨린다(RFC-0049).
            // ★ `for x in <seq> . do … end` — 대상은 **슬라이스**여야 하고,
            //   루프 변수 x 는 그 **원소 타입**을 갖는다.
            //   지금까지 둘 다 안 봤다: `for x in <정수>` 가 통과하고 런타임에서야 잡혔다
            //   (E-VM-TYPE: len needs a slice). 타입 오류를 런타임까지 미루는 것은 이 언어가 아니다.
            //   그리고 x 는 환경에 **없어서**, 본문에서 x 를 어떻게 쓰든 검사되지 않았다.
            proven_size_t bi = f->nkids;
            for (proven_size_t j = 2; j < f->nkids; j++)
                if (f->kids[j]->kind == LOW_CST_BLOCK) { bi = j; break; }
            ty_t seq = (bi > 2) ? tc_infer_run(c, f->kids, 2, bi - 2, env, *nenv) : tk(TK_UNKNOWN);
            if (seq.k != TK_UNKNOWN && seq.k != TK_NAMED && seq.k != TK_SLICE)
                tc_emit(c, "E-TYPE-ITER",
                        "`for … in` needs a slice — this value cannot be iterated", f->line);
            if (*nenv < TC_MAXENV) {
                // ★ 루프 변수는 **슬라이스의 원소 타입**을 갖는다.
                //   (u8 고정이었다 — `slice T` 가 원소 타입을 나르게 되면서 고칠 수 있게 됐다.)
                ty_t ev = tk_int(8, false);
                if (seq.k == TK_SLICE && seq.ebits)
                    ev = seq.eflt ? (ty_t){ .k = TK_FLOAT, .bits = seq.ebits }
                                  : tk_int(seq.ebits, seq.esign);
                env[*nenv].name = f->kids[1]->tok.lex;
                env[(*nenv)++].ty = ev;
            }
            for (proven_size_t j = 0; j < f->nkids; j++)
                if (f->kids[j]->kind == LOW_CST_BLOCK) tc_check_body(c, f->kids[j], env, nenv, ret);
        } else if (kw == LOW_KW_SET && f->nkids >= 3 && f->kids[1]->kind == LOW_CST_GROUP) {
            // ★ `set (index s i) v .` — s 는 **mut** 로 선언돼 있어야 한다.
            //   가변성은 정적으로 강제된다(런타임 슬라이스 값에는 mut 표시가 없다 — 포인터가 const 다).
            const low_cst_t *g = f->kids[1];
            if (g->nkids == 1 && g->kids[0]->kind == LOW_CST_FORM && g->kids[0]->nkids == 3 &&
                g->kids[0]->kids[0]->kind == LOW_CST_ATOM &&
                veq(g->kids[0]->kids[0]->tok.lex, "index") &&
                g->kids[0]->kids[1]->kind == LOW_CST_ATOM) {
                proven_u8str_view_t sn = g->kids[0]->kids[1]->tok.lex;
                bool fnd; ty_t st2 = env_find(env, *nenv, sn, &fnd);
                if (fnd && !st2.is_mut)
                    tc_emit(c, "E-TYPE-MUT",
                            "writing an element of a slice that is not declared `mut` "
                            "(a shared slice is read-only)", f->line);
            }
            // ★ `set (field q x) v .` — 필드가 존재해야 하고, 값의 타입이 맞아야 한다.
            if (g->nkids == 1 && g->kids[0]->kind == LOW_CST_FORM && g->kids[0]->nkids == 3 &&
                g->kids[0]->kids[0]->kind == LOW_CST_ATOM &&
                veq(g->kids[0]->kids[0]->tok.lex, "field") &&
                g->kids[0]->kids[1]->kind == LOW_CST_ATOM &&
                g->kids[0]->kids[2]->kind == LOW_CST_ATOM) {
                proven_u8str_view_t rn = g->kids[0]->kids[1]->tok.lex;
                proven_u8str_view_t fn = g->kids[0]->kids[2]->tok.lex;
                bool fnd; ty_t rt = env_find(env, *nenv, rn, &fnd);
                (void)rt;
                // ★★★ **정수 세그먼트는 필드 이름이 아니다 — 슬라이스 첨자다.**
                //   `field` 가 다단이 되면서 `set (field s 0) 11 .` 이 legal 이 됐는데
                //   (예전 `set s.0 11 .` 이 하던 일), 이 검사는 **선언된 구조체에서 이름을
                //   찾다가** 못 찾고 "가리키는 것이 없다" 고 고발했다. 읽기 쪽은 이미
                //   정수를 INDEX 로 내리고 있었다 — **쓰기 쪽만 그 규칙을 안 배웠다.**
                bool all_digit = fn.size > 0;
                for (proven_size_t z = 0; z < fn.size; z++)
                    if (fn.ptr[z] < '0' || fn.ptr[z] > '9') { all_digit = false; break; }
                if (all_digit) continue;
                // 레코드의 구조체 타입을 이름으로 찾을 수 없으므로(현재 ty_t 에 이름이 없다),
                // **모든 선언된 구조체**에서 그 필드를 찾는다 — 보수적이지만 없는 필드는 잡는다.
                bool any_has = false; ty_t fty = tk(TK_UNKNOWN);
                for (proven_size_t q = 0; q < c->nstrs; q++)
                    for (proven_size_t v = 0; v < c->strs[q].nf; v++)
                        if (proven_u8str_view_eq(c->strs[q].fname[v], fn)) {
                            any_has = true; fty = c->strs[q].fty[v];
                        }
                if (c->nstrs && !any_has)
                    tc_emit(c, "E-TYPE-FIELD",
                            "no declared struct has this field (the name points at nothing)", f->line);
                else if (any_has) {
                    ty_t actual = tc_infer_run(c, f->kids, 2, f->nkids - 2, env, *nenv);
                    tc_flag(c, compat(fty, actual),
                            "E-TYPE-FIELD", "the value does not match the field's declared type",
                            f->line);
                }
            }
        } else if (kw == LOW_KW_SET) {
            // `set <place> <form> .` (RFC-0049); tolerate a legacy `to` marker atom
            proven_size_t to = kw_index(f, LOW_KW_TO);
            bool found; ty_t vt = env_find(env, *nenv, f->kids[1]->tok.lex, &found);
            ty_t actual = (to + 1 < f->nkids) ? tc_infer_run(c, f->kids, to + 1, f->nkids - to - 1, env, *nenv)
                                              : tc_infer_run(c, f->kids, 2, f->nkids - 2, env, *nenv);
            if (found && vt.rk == 1 && actual.rk == 0) {
                // G7 write-through needs exclusivity: shared `ref` places are read-only
                tc_emit(c, "E-TYPE-REF", "write through a shared ref (declare mut_ref)", f->line);
            } else if (found) {
                ty_t core = vt; core.rk = 0;   // rk==2: check against the referent type
                tc_flag(c, compat(core, actual),
                        "E-TYPE-SET", "assigned value type does not match variable", f->line);
            }
        } else {
            // ★ `store <dst> <idx> <vec>`·`store_masked <dst> <idx> <vec> <mask>` (RFC-0040) — dst 는 **mut** 로
            //   선언돼 있어야 한다(읽기 전용 슬라이스에 벡터를 쓸 수 없다). set (index) 의 mut 강제와 같은 규율.
            if (f->nkids >= 4 && f->kids[0]->kind == LOW_CST_ATOM &&
                (veq(f->kids[0]->tok.lex, "store") || veq(f->kids[0]->tok.lex, "store_masked")) &&
                f->kids[1]->kind == LOW_CST_ATOM) {
                bool fnd; ty_t dt = env_find(env, *nenv, f->kids[1]->tok.lex, &fnd);
                if (fnd && !dt.is_mut)
                    tc_emit(c, "E-TYPE-MUT",
                            "`store`/`store_masked` writes a vector into a slice that is not declared `mut` "
                            "(a shared slice is read-only)", f->line);
            }
            // ★★★★ **조건 자리는 타입검사를 한 번도 받은 적이 없다** (RFC-0092, 2026-08-10).
            //   `if 5 .` 도 `guard 5 .` 도 **초록**이었다 — 이 검사기에 `if`/`guard`/`while`
            //   갈래가 아예 없었다(문장 종류 표에 `let`·`var`·`return`·`for`·`set` 뿐).
            //   그 침묵이 곧 C 식 진리성을 들여놓은 자리다. bool 이 타입 목록에 있는데
            //   조건이 bool 을 요구하지 않으면 그 타입은 **장식**이다.
            if (f->kids[0]->kind == LOW_CST_ATOM &&
                (kw == LOW_KW_IF || kw == LOW_KW_GUARD || kw == LOW_KW_WHILE ||
                 kw == LOW_KW_EXPECT)) {   // ★ 2026-09-27 — `expect 1 .` 이 통과해 0 아닌 수를 참으로 읽었다(X-0061 과 같은 병)
                // ★ 조건이 어디서 끝나는가 — 세 모양을 **다** 봐야 한다.
                //   `if` 는 BLOCK 으로, 어떤 꼴은 `do`/`else` **아톰**으로 끝나는데,
                //   `guard` 의 else 는 **중첩 FORM**(머리가 `else`)이다. 처음엔 앞의 둘만
                //   봤고, 그래서 **`guard 5` 가 그대로 통과했다** — `if 5` 는 잡히는데
                //   `guard 5` 는 안 잡히는 반쪽 게이트였다. 셋을 다 센다.
                proven_size_t cend = f->nkids;
                for (proven_size_t j = 1; j < f->nkids; j++) {
                    const low_cst_t *kj = f->kids[j];
                    bool stop = (kj->kind == LOW_CST_BLOCK);
                    if (!stop && kj->kind == LOW_CST_ATOM &&
                        (kj->tok.kw == LOW_KW_DO || kj->tok.kw == LOW_KW_ELSE)) stop = true;
                    if (!stop && kj->kind == LOW_CST_FORM && kj->nkids &&
                        kj->kids[0]->kind == LOW_CST_ATOM &&
                        (kj->kids[0]->tok.kw == LOW_KW_DO || kj->kids[0]->tok.kw == LOW_KW_ELSE))
                        stop = true;
                    if (stop) { cend = j; break; }
                }
                if (cend > 1) {
                    c->quiet++;   // ★ 타입만 묻는다 — 딸린 진단은 내 관심 밖이다
                    ty_t ct = tc_infer_run(c, f->kids, 1, cend - 1, env, *nenv);
                    c->quiet--;
                    if (ct.k == TK_INT || ct.k == TK_FLOAT)
                        tc_emit(c, "E-TYPE-COND",
                                "a condition must be bool, not a number — this language has no "
                                "truthiness (D12: no implicit int↔bool). Say what the test is: "
                                "`ne x 0`", f->line);
                }
            }
            // recurse into nested blocks (if/while/for bodies); also type the call itself
            (void)tc_infer(c, f, env, *nenv);
            for (proven_size_t j = 0; j < f->nkids; j++)
                if (f->kids[j]->kind == LOW_CST_BLOCK) tc_check_body(c, f->kids[j], env, nenv, ret);
            // ★★★ 2026-09-25 — **guard 의 else 는 BLOCK 이 아니라 FORM 이다**(머리가 `else`, low_cst.c ①).
            //   위 줄은 BLOCK 자식만 내려가서 guard 의 else 가 **통째로 검사 밖**이었다:
            //   `result` op 의 `guard … else return 0 .` 이 `--check` 초록이고 실행하면 죽었고
            //   (known-defects/bare-return-under-result.md), `else do let z u8 be 300 . … end` 조차 통과했다.
            //   IR(ir_guard)이 읽는 두 모양을 그대로 본다: 마지막이 BLOCK 이면 블록, 아니면 `else return …` 한 줄.
            if (kw == LOW_KW_GUARD && f->nkids >= 2) {
                const low_cst_t *ef = f->kids[f->nkids - 1];
                if (ef->kind == LOW_CST_FORM && ef->nkids >= 2 && ef->kids[0]->kind == LOW_CST_ATOM &&
                    ef->kids[0]->tok.kw == LOW_KW_ELSE) {
                    const low_cst_t *last = ef->kids[ef->nkids - 1];
                    if (last->kind == LOW_CST_BLOCK)
                        tc_check_body(c, last, env, nenv, ret);
                    else if (ef->kids[1]->kind == LOW_CST_ATOM && ef->kids[1]->tok.kw == LOW_KW_RETURN)
                        tc_check_return(c, ef->kids, 2, ef->nkids - 2, env, *nenv, ret, ef->line);
                }
            }
        }
    }
}


// ── RFC-0104 §8-2 · `E-BRAND-REUSED` — **한 브랜드 = 한 저장소 생성 자리** ──────────────
//
//   소유자 결정(2026-08-28): 브랜드는 **값으로 한 번도 안 쓰이는 `newtype`** 이고,
//   저장소를 **생성하는 자리**는 *comptime 타입 인자가 출력에만 나타나는 호출* 이다.
//     `init pa … → option (block_pool pa)`   pa 가 **출력에만** → 생성 자리
//     `take pa p → option (handle pa)`       pa 가 입력(`block_pool pa`)에도 → 아니다
//   ⇒ 같은 브랜드로 두 번 생성하면 그 브랜드는 저장소 **둘**을 덮는다. 그것을 여기서 문다.
//
//   ★ 언어는 `pool` 을 **모른다**(리프 규칙). 위 두 문장 어디에도 라이브러리 이름이 없다.
//   ★ 단형화가 먼저 돌아 호출 이름이 `init#pa` 이므로, 브랜드는 **이름에서** 읽는다.
static bool tc_name_has_brand(proven_u8str_view_t nm, proven_u8str_view_t brand) {
    if (!nm.size || !brand.size) return false;
    for (proven_size_t i = 0; i + 1 < nm.size; i++) {
        if (nm.ptr[i] != (proven_byte_t)'#') continue;
        proven_size_t at = i + 1, e = at;
        while (e < nm.size && nm.ptr[e] != (proven_byte_t)'#') e++;
        if (e - at == brand.size) {
            bool same = true;
            for (proven_size_t b = 0; b < brand.size; b++)
                if (nm.ptr[at + b] != brand.ptr[b]) { same = false; break; }
            if (same) return true;
        }
    }
    return false;
}
// 시그니처 이름에 실린 브랜드 후보(첫 `#` 뒤의 낱말들)를 하나씩 준다.
typedef struct { proven_u8str_view_t part[4]; proven_size_t n; } tc_brands_t;
static tc_brands_t tc_brands_of(proven_u8str_view_t nm) {
    tc_brands_t out = { .n = 0 };
    proven_size_t i = 0;
    while (i < nm.size && nm.ptr[i] != (proven_byte_t)'#') i++;
    while (i < nm.size && out.n < 4) {
        proven_size_t at = ++i, e = at;
        while (e < nm.size && nm.ptr[e] != (proven_byte_t)'#') e++;
        if (e > at) out.part[out.n++] = (proven_u8str_view_t){ .ptr = nm.ptr + at, .size = e - at };
        i = e;
    }
    return out;
}
static bool tc_is_brand_newtype(proven_u8str_view_t v) {
    for (proven_size_t i = 0; i < g_nnt; i++)
        if (proven_u8str_view_eq(g_nt[i].name, v)) {
            return !g_ntval[i];
        }
    return false;   // newtype 이 아니면 브랜드가 아니다(구조체 코어 newtype 도 여기서 뺀다)
}
// 브랜드 `b` 가 이 폼의 [s,e) 낱말들 안에 인스턴스 이름으로 실려 있는가.
static bool tc_node_has_brand(const low_cst_t *k, proven_u8str_view_t b, proven_size_t depth) {
    if (!k || depth > 8) return false;
    if (tc_name_has_brand(k->tok.lex, b)) return true;
    for (proven_size_t j = 0; j < k->nkids; j++)
        if (tc_node_has_brand(k->kids[j], b, depth + 1)) return true;
    return false;
}
static bool tc_span_has_brand(const low_cst_t *f, proven_size_t st, proven_size_t en, proven_u8str_view_t b) {
    if (!f) return false;
    for (proven_size_t i = st; i < en && i < f->nkids; i++)
        if (tc_node_has_brand(f->kids[i], b, 0)) return true;
    return false;
}
// 이 호출이 브랜드 `b` 의 **생성 자리**인가 — 출력엔 있고 입력엔 없다.
//   ★ 타입은 **선언 낱말에서** 읽는다: `ret` 의 이름칸은 감싸개 안쪽까지 늘 나르지는 않는다
//     (`option (block_pool#pa)` 처럼 알맹이가 괄호 안에 있는 자리). 헤더의 출력 구간을 그대로
//     보는 편이 짧고, 틀릴 자리가 적다.
static bool tc_is_generative(const tc_sig_t *sg, proven_u8str_view_t b) {
    for (proven_size_t i = 0; i < sg->nparams; i++)
        if (tc_name_has_brand(sg->params[i].nname, b)) return false;
    if (!sg->form) return false;
    low_op_header_t h = low_op_header(sg->form);
    for (proven_size_t i = 0; i < h.np; i++)                            // 입력에 있으면 생성이 아니다
        if (tc_span_has_brand(sg->form, h.p[i].ts, h.p[i].te, b)) return false;
    // ★ 출력 구간은 **다음 절이 시작될 때까지** 본다. `low_op_header` 의 `out_e` 는 낱말
    //   하나에서 멈추기도 한다(`output option (block_pool#pa) .` 에서 알맹이가 괄호 안이면
    //   그 괄호는 구간 밖이다) — 그래서 여기서 절 낱말을 만날 때까지 이어 읽는다.
    //   그 위 `p[i].ts/te` 는 파라미터마다 경계가 분명하므로 그대로 쓴다.
    proven_size_t oe = h.out_e;
    while (oe < sg->form->nkids) {
        const low_cst_t *k = sg->form->kids[oe];
        if (k && k->kind == LOW_CST_ATOM &&
            (low_is_clause_word(k->tok.lex) || k->tok.kw == LOW_KW_DO)) break;
        if (k && (k->kind == LOW_CST_BLOCK)) break;
        oe++;
    }
    return tc_span_has_brand(sg->form, h.out_s, oe, b);
}
static void tc_brand_walk(tc_ctx_t *c, const low_cst_t *nd,
                          proven_u8str_view_t *seen, proven_u32 *seenline, proven_size_t *nseen) {
    if (!nd) return;
    if (nd->kind == LOW_CST_ATOM || nd->kind == LOW_CST_FORM) {
        proven_u8str_view_t nm = nd->tok.lex;
        bool inst = false;
        for (proven_size_t i = 0; i < nm.size; i++) if (nm.ptr[i] == (proven_byte_t)'#') inst = true;
        if (inst) {
            const tc_sig_t *sg = NULL;
            for (proven_size_t i = 0; i < c->nsigs; i++)
                if (proven_u8str_view_eq(c->sigs[i].name, nm)) { sg = &c->sigs[i]; break; }
            if (sg) {
                tc_brands_t bs = tc_brands_of(nm);
                for (proven_size_t q = 0; q < bs.n; q++) {
                    proven_u8str_view_t b = bs.part[q];
                    if (!tc_is_brand_newtype(b)) continue;
                    if (!tc_is_generative(sg, b)) continue;
                    // ★ **한 자리는 한 번만 센다.** 같은 호출이 나무에서 두 모양으로 보인다
                    //   (FORM 의 머리 토큰과 그 안의 원자). 그리고 **선언은 호출이 아니다** —
                    //   op 이 선언된 줄은 뺀다. ☞ 정직히: 그래서 *선언과 같은 줄의 호출*과
                    //   *한 줄에 놓인 두 생성*은 못 본다. 줄이 아니라 자리로 세는 것은 후속이다.
                    if (sg->form && nd->tok.line == sg->form->tok.line) continue;
                    bool dup = false;
                    for (proven_size_t z = 0; z < *nseen; z++)
                        if (proven_u8str_view_eq(seen[z], b)) {
                            if (seenline[z] == nd->tok.line) { dup = false; goto brand_done; }
                            dup = true; break;
                        }
                    if (dup) {
                        // 같은 줄을 두 모양(FORM 머리·원자)으로 보므로 **한 번만** 운다.
                        bool told = false;
                        for (proven_size_t z = 0; z < *nseen; z++)
                            if (proven_u8str_view_eq(seen[z], b) && seenline[z] == nd->tok.line) told = true;
                        if (told) goto brand_done;
                        if (*nseen < TC_MAXSIG) { seen[*nseen] = b; seenline[*nseen] = nd->tok.line; (*nseen)++; }
                        tc_emit(c, "E-BRAND-REUSED",
                                "this brand already opened a storage somewhere else — a brand names ONE storage "
                                "instance, and a second creation site makes it name two. Then two different "
                                "storages share a type, and handles from one type-check against the other: the "
                                "very confusion the brand exists to refuse. Declare a second `newtype` and use it "
                                "here (a brand carries no data, so an extra one costs nothing at run time) "
                                "(RFC-0104 §5.11 · §8-2)", nd->tok.line);
                    } else if (*nseen < TC_MAXSIG) {
                        seen[*nseen] = b; seenline[*nseen] = nd->tok.line; (*nseen)++;
                    }
                    brand_done: ;
                }
            }
        }
    }
    // ★ **선언은 호출이 아니다.** `proc init#pa …` 의 머리 이름을 세면 그 op 의 **첫 호출**이
    //   벌써 두 번째가 되어, 옳은 프로그램이 빨개진다(실측 2026-08-28).
    //   ☞ 정의 낱말의 자리는 모양마다 다르다(`export proc f …`). 그래서 **모양을 외우지 않고**
    //     시그니처 표에 묻는다: *이 폼이 어떤 op 의 폼인가?* 맞으면 그 이름 원자 하나를 뺀다.
    proven_size_t skip = (proven_size_t)-1;
    if (nd->kind == LOW_CST_FORM) {
        proven_u8str_view_t own = { 0 };
        for (proven_size_t i = 0; i < c->nsigs; i++)
            if (c->sigs[i].form == nd) { own = c->sigs[i].name; break; }
        if (own.size)
            for (proven_size_t i = 0; i < nd->nkids; i++)
                if (nd->kids[i] && nd->kids[i]->kind == LOW_CST_ATOM &&
                    proven_u8str_view_eq(nd->kids[i]->tok.lex, own)) { skip = i; break; }
    }
    for (proven_size_t i = 0; i < nd->nkids; i++) {
        if (i == skip) continue;
        tc_brand_walk(c, nd->kids[i], seen, seenline, nseen);
    }
}

low_typecheck_result_t low_typecheck(proven_allocator_t work, const low_parse_result_t *pr) {
    low_typecheck_result_t out = { .ok = true };
    proven_result_array_t da = PROVEN_ARRAY_INIT(work, low_diag_t, 8);
    if (da.err != PROVEN_OK) { out.ok = false; return out; }
    out.diags = da.value;

    tc_ctx_t c = { .nsigs = 0, .nstrs = 0, .diags = &out.diags, .ok = &out.ok };
    g_nnt = 0; g_nnts = 0;   // ★ newtype 레지스트리 — 컴파일 단위마다 새로
    for (proven_size_t z = 0; z < TC_MAXSIG; z++) g_ntval[z] = false;

    // ★ pass 0a: 선언된 **타입 이름**을 모은다 — `type N is …` · `struct N` · `enum N`.
    //   이것이 없어서 존재하지 않는 타입을 써도 통과했다.
    g_nalias = 0; g_ntdecl = 0;   // ★ 정적 레지스트리는 **채우기 직전에** 비운다(low_doc 이 두 번 부른다)
    for (proven_size_t i = 0; i < pr->nforms && c.ntnames < TC_MAXSIG; i++) {
        const low_cst_t *f = pr->forms[i]; c.curform = f;
        if (f->kind != LOW_CST_FORM || f->nkids < 2 || f->kids[0]->kind != LOW_CST_ATOM) continue;
        low_kw_t kw = f->kids[0]->tok.kw;
        // ★★★ **액터도 타입이다.** 그런데 이 표에 없었다 ⇒ `input x coop .`(액터 인스턴스)이
        //   **E-TYPE-UNDEF** 를 맞았다. *"선언되지 않은 타입"* — **선언돼 있다. `actor` 로.**
        //   ★ "액터도 타입이다" 라는 **같은 규칙이 세 곳**에 있었고(충족 검사 · 경계 검사 ·
        //     타입 이름표) **셋 다 따로 고쳐야 했다.** 교훈 7 이 오늘만 네 번째다.
        if (kw != LOW_KW_TYPE && kw != LOW_KW_NEWTYPE && kw != LOW_KW_STRUCT && kw != LOW_KW_ENUM &&
            kw != LOW_KW_ACTOR) continue;
        if (f->kids[1]->kind != LOW_CST_ATOM) continue;
        c.tnames[c.ntnames++] = f->kids[1]->tok.lex;
        // ★ X-0010: 이름으로 타입을 가르려면 **선언된 이름**과 **투명 별칭**을 알아야 한다.
        if (g_ntdecl < TC_MAXSIG * 2) { g_tdecl[g_ntdecl++] = f->kids[1]->tok.lex; LOW_HWM("tc:type-decls", g_ntdecl, TC_MAXSIG * 2); }
        if (kw == LOW_KW_TYPE && f->nkids >= 3 && f->kids[2]->kind == LOW_CST_ATOM &&
            g_nalias < TC_MAXSIG) {
            g_alias[g_nalias].name   = f->kids[1]->tok.lex;
            g_alias[g_nalias].target = f->kids[2]->tok.lex;   // 머리 낱말이 곧 그 타입의 정체다
            g_nalias++;
        }

        // ★★★ **newtype 은 자기 정체성이다** (RFC-0002 §6.3.1). core 를 int 로 해소하고 **새 nominal**(≥2)을
        //   찍어 레지스트리에 넣는다 — 그러면 ty_of_word 가 `node_id` 를 명목 int 로 돌려주고, u64·다른
        //   newtype 과 섞이면 E-TYPE-NOMINAL 이 뜬다. (float core 는 nominal 을 안 보므로 int 만 강제한다.)
        if (kw == LOW_KW_NEWTYPE && f->nkids >= 3 && f->kids[2]->kind == LOW_CST_ATOM && g_nnt < TC_MAXSIG) {
            ty_t core = ty_of_word(f->kids[2]->tok.lex);
            if (core.k == TK_NAMED && g_nnts < TC_MAXSIG) {   // ★ **struct/enum/actor 코어**도 명목 정체성을 준다
                g_nts[g_nnts].name = f->kids[1]->tok.lex;
                g_nts[g_nnts].nominal = (proven_u8)(2 + TC_MAXSIG + g_nnts);   // int 계열과 겹치지 않는 대역
                g_nnts++;
            }
            if (core.k == TK_INT || core.k == TK_FLOAT) {   // ★ int·float 코어 둘 다 명목 정체성을 준다
                g_nt[g_nnt].name = f->kids[1]->tok.lex;
                g_nt[g_nnt].bits = core.bits; g_nt[g_nnt].sign = core.sign; g_nt[g_nnt].flt = (core.k == TK_FLOAT);
                g_nt[g_nnt].nominal = (proven_u8)(2 + g_nnt);   // 0=plain · 1=usize/isize · ≥2=newtype
                g_nnt++;
            }
        }
        // enum 의 변형 이름도 타입 자리에 오지는 않지만, `errors` 등에서 쓰이므로 기록하지 않는다.

        // ★★ **문법에 없는 철자를 받아 주지 않는다.** (SPEC-002 §211-213)
        //   구조체는 `struct N 필드* end`, enum 은 `enum N 변형* end` 로만 선언된다.
        //   `type N is T .` 의 T 는 **타입**이고, `type X is Y` 는 **투명 별칭**이다(SPEC-004 §89).
        //
        //   그런데 `type N is struct . field x u8 . . .` 이 조용히 통과하고 있었다. 그리고
        //   도구가 **레이아웃 없는 두 번째 종류의 구조체**를 발명했다: make/field 는 되는데
        //   `view` 도 안 되고 경계로 넘길 수도 없는 것 — 이 언어에 그런 타입은 **없다.**
        //   ⇒ 같은 것을 말하는 두 가지 철자가 서로 **다르게 행동한다.** 정확히 P1 위반이다.
        if (kw == LOW_KW_TYPE && f->nkids >= 3 && f->kids[2]->kind == LOW_CST_ATOM &&
            (f->kids[2]->tok.kw == LOW_KW_STRUCT || f->kids[2]->tok.kw == LOW_KW_ENUM))
            tc_emit(&c, "E-TYPE-DECL",
                   "a struct/enum is declared with `struct N … end` / `enum N … end`, not with "
                   "`type N is struct …` — `type X is Y` is a TRANSPARENT ALIAS to an existing "
                   "type (SPEC-002 §211-213, SPEC-004 §89). The spelling you used built a "
                   "layout-less thing that cannot be `view`ed and cannot cross the program "
                   "boundary: this language has no such type.",
                   f->kids[0]->tok.line);
        // ★★★ 2026-09-15 소유자 결정 — **`type N be T` 는 거절한다.** 코퍼스는 91 곳 모두 `type N T .` 로 쓰는데
        //   처리기는 `be` 를 끼운 꼴도 받고 있었다(같은 뜻의 철자가 둘). `be` 는 `let`/`var` 가 값을 묶는 낱말이다.
        if ((kw == LOW_KW_TYPE || kw == LOW_KW_NEWTYPE) && f->nkids >= 3 && f->kids[2]->kind == LOW_CST_ATOM &&
            f->kids[2]->tok.kw == LOW_KW_BE)
            tc_emit(&c, "E-TYPE-DECL",
                   "a type alias is `type N T .` and a newtype is `newtype N T .` — without `be`. `be` binds a VALUE "
                   "(`let x u8 be 1 .`); a type declaration names a TYPE. One meaning, one spelling",
                   f->kids[0]->tok.line);
    }

    // ★ pass 0b: `type N is …` 별칭의 **순환**을 잡는다.
    //   `type a is b . type b is a .` 이 조용히 통과했다 — 이름이 자기 자신으로 돌아온다.
    //   해석하려 들면 무한히 돈다. 선언이 아무것도 뜻하지 않는다.
    {
        proven_u8str_view_t an[TC_MAXSIG], ar[TC_MAXSIG];   // 별칭 → 대상
        proven_size_t na = 0;
        for (proven_size_t i = 0; i < pr->nforms && na < TC_MAXSIG; i++) {
            const low_cst_t *f = pr->forms[i]; c.curform = f;
            if (f->kind != LOW_CST_FORM || f->nkids < 3 || f->kids[0]->kind != LOW_CST_ATOM) continue;
            if (f->kids[0]->tok.kw != LOW_KW_TYPE) continue;
            if (f->kids[1]->kind != LOW_CST_ATOM || f->kids[2]->kind != LOW_CST_ATOM) continue;
            // ★ `type N <core> …` — `is` 는 **없앴다**(장식이었다). 대상은 kids[2] 다.
            an[na] = f->kids[1]->tok.lex; ar[na] = f->kids[2]->tok.lex; na++;
        }
        for (proven_size_t i = 0; i < na; i++) {
            proven_u8str_view_t cur = ar[i];
            for (proven_size_t hop = 0; hop <= na; hop++) {
                if (proven_u8str_view_eq(cur, an[i])) {
                    tc_emit(&c, "E-TYPE-CYCLE",
                            "this `type` alias resolves back to itself — the declaration names "
                            "nothing (resolving it would not terminate)",
                            pr->forms[0]->line);
                    break;
                }
                bool moved = false;
                for (proven_size_t j = 0; j < na; j++)
                    if (proven_u8str_view_eq(an[j], cur)) { cur = ar[j]; moved = true; break; }
                if (!moved) break;             // 내장 타입에 닿았다 — 순환 아님
            }
        }
    }

    // ★ pass 0: 구조체 선언을 모은다. 타입체커가 이것을 **몰라서** make 리터럴이
    //   필드 타입도·누락도·없는 필드도 검사받지 않고 있었다.
    for (proven_size_t i = 0; i < pr->nforms && c.nstrs < TC_MAXSIG; i++) {
        const low_cst_t *f = pr->forms[i]; c.curform = f;
        if (f->kind != LOW_CST_FORM || f->nkids < 3 || f->kids[0]->kind != LOW_CST_ATOM) continue;
        if (f->kids[0]->tok.kw != LOW_KW_STRUCT) continue;
        if (f->kids[1]->kind != LOW_CST_ATOM) continue;
        const low_cst_t *blk = f->kids[f->nkids - 1];
        if (blk->kind != LOW_CST_BLOCK) continue;
        tc_struct_t *st = &c.strs[c.nstrs++];
        st->name = f->kids[1]->tok.lex; st->nf = 0;
        // ★★ 한계를 넘는 필드는 **조용히 잘렸다** — `--check` 는 초록불이었고, 레이아웃이
        //   짧게 나와 `encode` 가 바이트를 덜 썼다. 임의의 배열 크기가 조용히 자르는 병
        //   (`prng[8]` 과 같은 자리). **거절이 정직한 답이다.**
        {
            proven_size_t nfld = 0;
            for (proven_size_t j = 0; j < blk->nkids; j++) {
                const low_cst_t *fd = blk->kids[j];
                if (fd->kind != LOW_CST_FORM || fd->nkids < 2 || fd->kids[0]->kind != LOW_CST_ATOM)
                    continue;
                if (fd->kids[0]->tok.kw == LOW_KW_SATISFIES) continue;   // ★ 주장이지 필드가 아니다
                if (veq(fd->kids[0]->tok.lex, "layout") || veq(fd->kids[0]->tok.lex, "align"))
                    continue;
                nfld++;
            }
            if (nfld > TC_MAXF)
                tc_emit(&c, "E-TYPE-LIMIT",
                        "this struct has more fields than the tool can carry (16) — the extra "
                        "fields used to be DROPPED SILENTLY: the layout came out short and "
                        "`encode` wrote too few bytes. Refusing is the honest answer",
                        f->kids[0]->tok.line);
        }
        for (proven_size_t j = 0; j < blk->nkids && st->nf < TC_MAXF; j++) {
            const low_cst_t *fd = blk->kids[j];
            if (fd->kind != LOW_CST_FORM || fd->nkids < 2 || fd->kids[0]->kind != LOW_CST_ATOM) continue;
            // `layout packed .` · `align n .` 은 **의사 필드**다 — 값이 아니라 배치 속성이다.
            // (IR 도 그렇게 다룬다. 타입체커만 그것을 몰라서 필드로 오인했다.)
            if (fd->kids[0]->tok.kw == LOW_KW_SATISFIES) continue;   // ★ 주장이지 필드가 아니다
            if (veq(fd->kids[0]->tok.lex, "layout")) {
                // ★ layout 어휘는 닫혀 있다: packed | native
                if (fd->nkids < 2 || fd->kids[1]->kind != LOW_CST_ATOM ||
                    !(veq(fd->kids[1]->tok.lex, "packed") || veq(fd->kids[1]->tok.lex, "native")))
                    tc_emit(&c, "E-TYPE-LAYOUT",
                            "unknown struct layout (the vocabulary is closed: `packed` | `native`)",
                            fd->line);
                continue;
            }
            if (veq(fd->kids[0]->tok.lex, "align")) {
                // ★ align 은 **2의 거듭제곱**이어야 한다. 아니면 하드웨어가 그것을 지킬 수 없다.
                // ★★★ **타깃에게 물어본 정렬도 받는다** (RFC-0104 §8-7): `align machine.cache_line .`
                //   그 수는 **타깃 표**가 알고 이 층은 모른다 — 값 검사는 IR 이 한다(거기가 표를 든다).
                //   ☞ 같은 규칙이 **두 자리**에 있다는 것 자체가 이 저장소가 반복해 배운 병이다.
                //     여기서는 *"이 자리에 무엇이 올 수 있나"* 만 보고, *"그 값이 옳은가"* 는 한 곳에서 본다.
                const low_cst_t *av_nd = (fd->nkids > 1) ? fd->kids[1] : NULL;
                if (av_nd && av_nd->kind == LOW_CST_ATOM && veq(av_nd->tok.lex, "comptime") && fd->nkids > 2)
                    av_nd = fd->kids[2];
                bool from_machine = av_nd && av_nd->kind == LOW_CST_ATOM &&
                                    av_nd->tok.lex.size > 8 &&
                                    proven_u8str_view_eq((proven_u8str_view_t){ .ptr = av_nd->tok.lex.ptr, .size = 8 },
                                                         proven_u8str_view_from_cstr("machine."));
                proven_i64 av;
                if (!from_machine &&
                    (fd->nkids < 2 || fd->kids[1]->kind != LOW_CST_ATOM ||
                    !tc_int_lit(fd->kids[1]->tok.lex, &av) || av <= 0 || (av & (av - 1)) != 0))
                    tc_emit(&c, "E-TYPE-ALIGN",
                            "`align` must be a power of two (1, 2, 4, 8, …) — no hardware can honour "
                            "anything else", fd->line);
                continue;
            }
            st->fname[st->nf] = fd->kids[0]->tok.lex;
            st->fform[st->nf] = fd;
            rng_res_t rr;
            st->fty[st->nf] = ty_of_decl_r(fd, 1, fd->nkids, &rr);
            tc_check_rng(&c, rr, fd->line);
            st->nf++;
        }
    }

    // ── pass 0a″: **enum 갈래의 모양** (2026-09-14) ──
    // ★★★ 갈래를 점으로 닫지 않고 줄마다 적으면(`red` · `green` · `blue`) **한 갈래로 이어졌다.** 개행은 닫개가
    //   아니므로(RFC-0103) 파서는 그것을 «칸 `green`, 타입 `blue` 를 가진 갈래 `red`» 로 읽었고, 칸 타입이 실제
    //   타입인지 **아무도 보지 않아** 조용히 통과했다(`color.green` 까지). 매뉴얼 9장과 정본 §6.2.7 의 예제가
    //   바로 그 모양이었다. ⇒ 두 가지를 본다:
    //   ① 갈래 이름보다 **뒤 줄에서 같은(또는 얕은) 들여쓰기로** 시작하는 낱말 = 점이 빠진 다음 갈래(`E-ENUM-DOT`)
    //   ② 칸은 `<이름> <타입>` 짝이고, 그 타입은 **있는 타입**이어야 한다(`E-TYPE-UNDEF` · 짝이 안 맞으면 `E-ENUM-FIELD`)
    for (proven_size_t i = 0; i < pr->nforms; i++) {
        const low_cst_t *f = pr->forms[i]; c.curform = f;
        if (f->kind != LOW_CST_FORM || f->nkids < 3 || f->kids[0]->kind != LOW_CST_ATOM) continue;
        if (f->kids[0]->tok.kw != LOW_KW_ENUM) continue;
        const low_cst_t *blk = f->kids[f->nkids - 1];
        if (blk->kind != LOW_CST_BLOCK) continue;
        for (proven_size_t j = 0; j < blk->nkids; j++) {
            const low_cst_t *vf = blk->kids[j];
            if (vf->kind != LOW_CST_FORM || vf->nkids < 1 || vf->kids[0]->kind != LOW_CST_ATOM) continue;
            if (vf->kids[0]->tok.kw == LOW_KW_SATISFIES) continue;
            const low_token_t *nt = &vf->kids[0]->tok;
            bool told = false;
            for (proven_size_t t = 1; t < vf->nkids; t++) {
                const low_cst_t *k = vf->kids[t];
                if (k->kind != LOW_CST_ATOM) continue;
                if (k->tok.line > nt->line && k->tok.col <= nt->col) {
                    tc_emit(&c, "E-ENUM-DOT",
                            "this enum variant is not closed with `.`, so the NEXT line was read as part of it — "
                            "a newline does not close a form (RFC-0103). Close every variant: `red .` · "
                            "`green .`. Without the dot `red green blue` is ONE variant `red` whose payload "
                            "field `green` has a type `blue`",
                            k->tok.line);
                    told = true;
                    break;
                }
            }
            if (told) continue;
            // 여러 낱말 타입(`slice u8` 따위)은 IR 이 더 정확한 말(`E-ENUM-PAYLOAD`)을 한다 — 여기선 한 낱말 짝만 본다
            bool multi = false;
            for (proven_size_t t = 1; t < vf->nkids; t++)
                if (vf->kids[t]->kind != LOW_CST_ATOM || ty_is_head_word(vf->kids[t]->tok.lex)) multi = true;
            if (multi) continue;
            if ((vf->nkids - 1) % 2) {
                tc_emit(&c, "E-ENUM-FIELD",
                        "an enum variant's payload is `<field> <type>` pairs, and this one has a word left "
                        "over — a field without a type, or a missing `.` after the variant",
                        vf->kids[vf->nkids - 1]->tok.line ? vf->kids[vf->nkids - 1]->tok.line : vf->line);
                continue;
            }
            for (proven_size_t t = 2; t < vf->nkids; t += 2) tc_check_tnames(&c, vf, t, t + 1);
        }
    }

    // ── pass 0b: **재귀 struct 필드** — 크기가 정해지지 않는다 (RFC-0093, 2026-08-10) ──
    // ★★★★★ `type` 별칭에는 이 검사가 **이미 있었다**(바로 위 E-TYPE-CYCLE, `type a b . type b a .`).
    //   그런데 **struct 필드에는 없었다.** 그래서:
    //       struct node . v u32 . kid node . end   →  `--check` **초록**
    //   인데 그 struct 의 `size_of` 는 `E-IR-UNDEF`, `make` 는 `E-IR-UNSUP` 였다 —
    //   **선언은 받아 주고 쓰려 하면 없는** 타입이다(bool 이 그랬던 것과 같은 부류).
    //   ☞ **한 검사기에서 배운 것을 이웃 자리에 옮겨 적지 않으면 같은 구멍이 다시 난다.**
    //     이번 단계에서만 이 패턴이 셋이다: `let`/`var`(탈출 검사) · 별칭/struct(순환) ·
    //     섬/전위(논리 op).
    // ★ 진단은 **길을 알려 준다**: 이 언어에 포인터 나무는 없고, 나무는 **색인(arena)** 으로 짓는다.
    //   `hashmap`·`pool`·`vec` 이 이미 그 규율이고, `newtype nid u32 .` 로 색인에 이름도 준다.
    for (proven_size_t si = 0; si < c.nstrs; si++) {
        proven_size_t stack[TC_MAXSIG]; proven_size_t sp = 0;
        bool seen[TC_MAXSIG];
        for (proven_size_t z = 0; z < TC_MAXSIG; z++) seen[z] = false;
        stack[sp++] = si; seen[si] = true; LOW_HWM("tc:cycle-stack", sp, TC_MAXSIG);
        // ★★★★★ **두 질문을 갈라 답한다** (2026-08-28, RFC-0104 §8-1 · WO-0133).
        //   전에는 셋을 한 진단으로 답했다:
        //     struct node . next node .     end   — 값 순환      = **레이아웃 오류**
        //     struct node . next ref node . end   — 간접 순환    = **수명 정책**
        //     a↔b 값 상호재귀                      — 값 순환
        //   앞의 것은 크기가 무한해서 **어떤 정책으로도 못 고친다**. 뒤의 것은 크기가
        //   유한하고(참조는 한 칸) *무엇이 그 참조를 살아 있게 하는가* 가 안 답해졌을 뿐이다.
        //   같은 이름으로 거절하면 사용자가 *"재귀 자료구조는 이 언어에서 안 된다"* 고 읽는다.
        //   실제로는 **하나는 영영 안 되고 하나는 아직 안 될 뿐**이다.
        //   ⇒ 경로에 `ref`/`mut_ref` 가 한 번이라도 끼면 **간접 순환**이다.
        bool cyc = false;        // 값 순환 — 레이아웃이 없다
        bool indir = false;      // 간접 순환 — 수명 정책이 없다
        bool via_ref[TC_MAXSIG];
        for (proven_size_t z = 0; z < TC_MAXSIG; z++) via_ref[z] = false;
        while (sp && !cyc) {
            proven_size_t cur = stack[--sp];
            bool cur_ref = via_ref[cur];
            for (proven_size_t j = 0; j < c.strs[cur].nf && !cyc; j++) {
                const low_cst_t *fd = c.strs[cur].fform[j];
                if (!fd) continue;
                // 이 필드의 타입 낱말에 `ref`/`mut_ref` 가 있으면 **간접 간선**이다.
                bool fref = false;
                for (proven_size_t w = 1; w < fd->nkids; w++)
                    if (fd->kids[w]->kind == LOW_CST_ATOM &&
                        (proven_u8str_view_eq(fd->kids[w]->tok.lex, proven_u8str_view_from_cstr("ref")) ||
                         proven_u8str_view_eq(fd->kids[w]->tok.lex, proven_u8str_view_from_cstr("mut_ref"))))
                        { fref = true; break; }
                // 타입 낱말은 여럿일 수 있다(`option node` · `ref node` · `array node 4`).
                // 그 **어느 자리에든** struct 이름이 있으면 그 struct 를 품는다는 뜻이다.
                for (proven_size_t w = 1; w < fd->nkids && !cyc; w++) {
                    if (fd->kids[w]->kind != LOW_CST_ATOM) continue;
                    for (proven_size_t k = 0; k < c.nstrs; k++) {
                        if (!proven_u8str_view_eq(c.strs[k].name, fd->kids[w]->tok.lex)) continue;
                        if (k == si) {
                            if (cur_ref || fref) indir = true;   // 경로에 참조가 있었다
                            else cyc = true;                     // 값만으로 돌아왔다
                            break;
                        }
                        if (!seen[k]) {
                            seen[k] = true;
                            via_ref[k] = cur_ref || fref;
                            // ★★★ 여기서 잘리면 **순환을 못 본다** — `E-STRUCT-CYCLE` 이
                            //   조용히 안 나고, 크기가 무한한 타입이 통과한다.
                            //   ⇒ 자르지 않고 **거절한다**(검사가 줄어드는 쪽으로 틀리지 않는다).
                            if (sp >= TC_MAXSIG) { c.cycle_overflow = true; break; }
                            stack[sp++] = k; LOW_HWM("tc:cycle-stack", sp, TC_MAXSIG);
                        }
                        break;
                    }
                }
            }
        }
        if (!cyc && indir)
            tc_emit(&c, "E-STRUCT-REF-UNMANAGED",
                    "this struct refers to itself THROUGH A REFERENCE, so its size IS finite — the "
                    "problem is not layout but LIFETIME: nothing here says what keeps that reference "
                    "alive. Lowent has no managed pointer graph yet, so an unowned back-edge cannot "
                    "be accepted. Build the graph with INDICES into a slice (an arena) the way "
                    "`hashmap`/`pool`/`vec` do, and name the index with `newtype node_id u32 .` "
                    "(RFC-0104 §5.1: a value cycle is a layout error and can never work; an "
                    "indirect cycle is a policy question and may be answered later)",
                    c.strs[si].name.size ? pr->forms[0]->line : 0);
        if (cyc)
            tc_emit(&c, "E-STRUCT-CYCLE",
                    "this struct contains itself (directly or through another struct), so its size "
                    "is not finite — the declaration cannot describe any layout. There are no "
                    "pointer trees in this language: build trees with INDICES into a slice (an "
                    "arena), the way `hashmap`/`pool`/`vec` already do, and give the index a name "
                    "with `newtype node_id u32 .`",
                    c.strs[si].name.size ? pr->forms[0]->line : 0);
    }

    // ★★★ **넘쳤으면 말한다.** 순환 탐지 스택이 찼다는 것은 *"순환이 없다"* 를
    //   말할 근거가 없다는 뜻이다 — 조용한 초록은 여기서 가장 나쁜 답이다.
    if (c.cycle_overflow)
        tc_emit(&c, "E-TYPE-CYCLE-LIMIT",
                "the struct graph is larger than the cycle detector can walk, so this unit's "
                "\"no value cycle\" answer would have no basis. Refusing is the honest answer: "
                "split the unit, or report this limit with the unit that triggered it", 0);

    // pass 1: collect signatures from input/output clauses
    proven_u8str_view_t tc_curmod = { 0 };   // ★ 자리가 곧 모듈 소속이다(RFC-0075 S2)
    for (proven_size_t i = 0; i < pr->nforms; i++) {
        const low_cst_t *f = pr->forms[i]; c.curform = f;
        if (f->kind != LOW_CST_FORM || f->nkids < 2 || f->kids[0]->kind != LOW_CST_ATOM) continue;
        low_kw_t kw = f->kids[0]->tok.kw;
        if (kw == LOW_KW_MODULE && f->kids[1]->kind == LOW_CST_ATOM) { tc_curmod = f->kids[1]->tok.lex; continue; }
        if (kw != LOW_KW_FN && kw != LOW_KW_PROC) continue;
        // ★★★ **표가 차면 멈추지 않고 거절한다** (REQ-0010). 전에는 이 루프의 조건이
        //   `c.nsigs < TC_MAXSIG` 였다 — 그러면 129 번째 op 부터는 시그니처가 없고,
        //   pass 2 가 `c.nsigs` 만 도니 **그 몸통들은 검사를 아예 안 받았다.**
        //   초록불은 «검사했다» 는 뜻이어야 한다.
        if (c.nsigs >= TC_MAXSIG) {
            tc_emit(&c, "E-TYPE-LIMIT",
                    "this compilation unit declares more ops than the type checker's signature "
                    "table holds, so the ops after that point would go UNCHECKED. Refusing is the "
                    "honest answer: split the unit (or raise TC_MAXSIG and say why). A green light "
                    "must mean «checked», never «ran out of room»",
                    f->line);
            break;
        }
        // ★★★ **op 의 머리는 한 번만 읽는다** (DECISION-0015).
        //   전엔 이 자리가 타입을 **`j + 2`** 에서 읽었다 — **이름 한 낱말만** 건너뛴 것이다.
        //   그래서 `input s mut slice u8 .` 의 `mut` 도, `input h owned T .` 의 `owned` 도
        //   **타입 낱말에 섞여 들어갔다.** 다른 소비자들은 그것을 벗겼다 — **같은 절을 두고
        //   두 소비자가 다른 타입을 봤다.** 이제 `low_op_header()` 가 벗긴다.
        low_op_header_t h = low_op_header(f);
        tc_sig_t *s = &c.sigs[c.nsigs++];
        *s = (tc_sig_t){ .name = h.name, .ret = tk(TK_UNKNOWN), .body = h.body, .mod = tc_curmod,
                         .form = f };
        // ★★★ **파라미터도 같은 규율** (REQ-0011): 아홉 번째부터 타입이 안 실려서
        //   그 이름을 쓰는 본문이 검사되지 않았다. 이제 한계는 IR 과 같고(16), 넘으면 거절한다
        //   — IR 은 17 번째에서 arity 로 거절하므로 그 사이가 **구멍**이었다.
        if (h.np > TC_MAXP)
            tc_emit(&c, "E-TYPE-LIMIT",
                    "this op declares more inputs than the type checker binds, so the inputs past "
                    "that point (and every use of them in the body) would go UNCHECKED. Refusing "
                    "is the honest answer",
                    f->line);
        for (proven_size_t q = 0; q < h.np && s->nparams < TC_MAXP; q++) {
            rng_res_t rr;                                   // ★ RFC-0055 D7: 시그니처의 range
            s->params[s->nparams++] = ty_of_decl_r(f, h.p[q].ts, h.p[q].te, &rr);
            tc_check_rng(&c, rr, f->line);
            tc_check_tnames(&c, f, h.p[q].ts, h.p[q].te);   // ★ 타입 이름이 존재하는가
        }
        if (h.out_e > h.out_s) {
            rng_res_t rr;
            s->ret = ty_of_decl_r(f, h.out_s, h.out_e, &rr);
            tc_check_rng(&c, rr, f->line);
            tc_check_tnames(&c, f, h.out_s, h.out_e);
        }
    }

    // pass 2: check each op body (bind params from its input clauses, then walk)
    for (proven_size_t i = 0; i < c.nsigs; i++) {
        tc_var_t env[TC_MAXENV];
        proven_size_t nenv = 0;
        // ★ 파라미터 이름은 **헤더가 준다** (DECISION-0015) — 전엔 `kids[j+1]` 을 잡아서
        //   `input comptime n u8 .` 의 이름이 **`comptime`** 이 됐다.
        // ★★ 그리고 그 헤더는 **이 시그니처를 낳은 폼**의 것이다(X-0008) — 이름으로 다시
        //   찾지 않는다. 찾으면 같은 이름의 남의 op 을 집는다.
        if (c.sigs[i].form) {
            low_op_header_t hh = low_op_header(c.sigs[i].form);
            for (proven_size_t q = 0; q < hh.np && nenv < TC_MAXENV && q < c.sigs[i].nparams; q++) {
                env[nenv].name = hh.p[q].name;
                { env[nenv++].ty = c.sigs[i].params[q]; LOW_HWM("tc:env", nenv, TC_MAXENV); }
            }
        }
        c.curmod = c.sigs[i].mod;   // ★ 이 본문은 이 모듈의 코드다 — 맨이름은 제 이웃을 먼저 본다
        c.curform = c.sigs[i].form;
        tc_check_body(&c, c.sigs[i].body, env, &nenv, c.sigs[i].ret);
    }

    // pass 3: ★ 2026-09-27 (X-0071 실측) — **`test` 블록의 몸은 타입 검사를 한 번도 받지 않았다.** 위 두 패스가
    //   `fn`/`proc` 만 돌아서, 시험 안의 `let x u8 be 300 .` 도 `expect 1 .` 도 `check: ok` 였다. 시험은 입력이 없는
    //   몸이므로 빈 환경에서 같은 규칙으로 걷는다.
    {
        proven_u8str_view_t tmod = { 0 };
        for (proven_size_t i = 0; i < pr->nforms; i++) {
            const low_cst_t *f = pr->forms[i];
            if (f->kind != LOW_CST_FORM || f->nkids < 2 || f->kids[0]->kind != LOW_CST_ATOM) continue;
            low_kw_t kw = f->kids[0]->tok.kw;
            if (kw == LOW_KW_MODULE && f->kids[1]->kind == LOW_CST_ATOM) { tmod = f->kids[1]->tok.lex; continue; }
            if (kw != LOW_KW_TEST) continue;
            const low_cst_t *body = f->kids[f->nkids - 1];
            if (body->kind != LOW_CST_BLOCK) continue;
            tc_var_t env[TC_MAXENV];
            proven_size_t nenv = 0;
            c.curmod = tmod; c.curform = f;
            tc_check_body(&c, body, env, &nenv, tk(TK_UNKNOWN));
        }
    }

    // ── pass 2b: **actor 핸들러의 몸** (WO-0219 · WO-0213 spill) ──
    //   ★★★ 핸들러 몸은 **타입검사를 한 번도 받지 않았다.** pass 1·2 가 최상위 `fn`/`proc` 만 돌았기 때문이다 —
    //   `let v bool be add n 1 .` 이 actor 안에서는 초록이었다(check-decl-types 가 lib/alloc.low 의 핸들러 자리를
    //   «비틀어도 초록» 으로 세던 까닭). 얼로케이터가 전부 actor 가 된 뒤로(RFC-0112 D6) 이 구멍은 모든 할당의 자리다.
    //   ⇒ 상태 칸과 입력을 환경에 묶고 같은 몸 검사를 돌린다. 핸들러는 시그니처 표에 넣지 않는다 — `send` 로만
    //     불리므로, 넣으면 같은 이름의 모듈 op 을 가린다.
    {
        proven_u8str_view_t amod = { 0 };
        for (proven_size_t i = 0; i < pr->nforms; i++) {
            const low_cst_t *f = pr->forms[i]; c.curform = f;
            if (f->kind != LOW_CST_FORM || f->nkids < 3 || f->kids[0]->kind != LOW_CST_ATOM) continue;
            if (f->kids[0]->tok.kw == LOW_KW_MODULE && f->kids[1]->kind == LOW_CST_ATOM) { amod = f->kids[1]->tok.lex; continue; }
            if (f->kids[0]->tok.kw != LOW_KW_ACTOR || f->kids[f->nkids - 1]->kind != LOW_CST_BLOCK) continue;
            const low_cst_t *blk = f->kids[f->nkids - 1];
            tc_var_t senv[TC_MAXENV];
            proven_size_t ns = 0;
            for (proven_size_t j = 0; j < blk->nkids; j++) {
                const low_cst_t *st = blk->kids[j];
                if (st->kind != LOW_CST_FORM || st->nkids < 2 || st->kids[0]->kind != LOW_CST_ATOM ||
                    st->kids[0]->tok.kw != LOW_KW_STATE || st->kids[st->nkids - 1]->kind != LOW_CST_BLOCK) continue;
                const low_cst_t *sb = st->kids[st->nkids - 1];
                // ★ 표가 차면 **말한다**(REQ-0010 — 초록은 «검사했다» 여야 한다). 칸이 많은 actor 의 몸을 반만 묶고 검사하지 않는다.
                if (sb->nkids > TC_MAXENV)
                    tc_emit(&c, "E-TYPE-LIMIT",
                            "this actor has more state fields than the type checker binds, so its handler bodies "
                            "would be checked against a partial environment. Refusing is the honest answer",
                            st->line);
                for (proven_size_t q = 0; q < sb->nkids && ns < TC_MAXENV; q++) {
                    const low_cst_t *fld = sb->kids[q];
                    if (fld->kind != LOW_CST_FORM || fld->nkids < 2 || fld->kids[0]->kind != LOW_CST_ATOM) continue;
                    rng_res_t rr;
                    senv[ns].name = fld->kids[0]->tok.lex;
                    senv[ns++].ty = ty_of_decl_r(fld, 1, fld->nkids, &rr);
                }
            }
            for (proven_size_t j = 0; j < blk->nkids; j++) {
                const low_cst_t *op = blk->kids[j];
                if (op->kind != LOW_CST_FORM || op->nkids < 3 || op->kids[0]->kind != LOW_CST_ATOM) continue;
                if (op->kids[0]->tok.kw != LOW_KW_PROC && op->kids[0]->tok.kw != LOW_KW_FN) continue;
                low_op_header_t hh = low_op_header(op);
                if (!hh.body) continue;
                tc_var_t env[TC_MAXENV];
                proven_size_t nenv = 0;
                for (proven_size_t q = 0; q < ns && nenv < TC_MAXENV; q++) env[nenv++] = senv[q];
                if (ns + hh.np > TC_MAXENV)
                    tc_emit(&c, "E-TYPE-LIMIT",
                            "this handler's state fields and inputs together exceed the type checker's "
                            "environment, so the names past that point would go UNCHECKED. Refusing is the "
                            "honest answer", op->line);
                for (proven_size_t q = 0; q < hh.np && nenv < TC_MAXENV; q++) {
                    rng_res_t rr;
                    env[nenv].name = hh.p[q].name;
                    env[nenv++].ty = ty_of_decl_r(op, hh.p[q].ts, hh.p[q].te, &rr);
                    tc_check_rng(&c, rr, op->line);
                    tc_check_tnames(&c, op, hh.p[q].ts, hh.p[q].te);
                }
                ty_t ret = tk(TK_UNKNOWN);
                if (hh.out_e > hh.out_s) {
                    rng_res_t rr;
                    ret = ty_of_decl_r(op, hh.out_s, hh.out_e, &rr);
                    tc_check_rng(&c, rr, op->line);
                    tc_check_tnames(&c, op, hh.out_s, hh.out_e);
                }
                c.curmod = amod;
                tc_check_body(&c, hh.body, env, &nenv, ret);
            }
        }
    }

    // ── pass 3: 브랜드 재사용 (RFC-0104 §8-2 · 위 tc_brand_walk 주석) ──
    //   ★ 마지막에 돈다: 시그니처가 다 모인 뒤라야 *"출력에만 있는가"* 를 물을 수 있다.
    {
        proven_u8str_view_t seen[TC_MAXSIG]; proven_u32 seenline[TC_MAXSIG]; proven_size_t nseen = 0;
        for (proven_size_t i = 0; i < pr->nforms; i++)
            tc_brand_walk(&c, pr->forms[i], seen, seenline, &nseen);
    }

    return out;
}
