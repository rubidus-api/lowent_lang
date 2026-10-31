// low_typecheck.c — MVP static type checking (S4): kind-level + integer width/sign.
#include "low_hwm.h"
#include "low_typecheck.h"

#include "low_token.h"
#include "low_diag.h"
#include "low_ir.h"   // RFC-0149 L5 — `usize` 의 폭은 대상의 주소 폭(`low_ir_target()`)
#include "low_arity.h"   // RFC-0148 S5 — 빌트인 op 의 인자 수(표 하나)

#include <math.h>
bool low_ir_is_builtin_op(proven_u8str_view_t name);     /* low_ir.c — `call_builtin` 뒤의 이름공간에 있는 철자인가 */
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
    proven_u64  valhi;  // ★ RFC-0151 Q3 ⓧ — 정수 리터럴의 위 64 비트(값이 `u64` 를 넘을 때만 0 이 아니다 · 128 비트 자리만 받는다)
    proven_u8   rk;     // reference kind: 0 = value, 1 = ref (shared), 2 = mut_ref
    proven_u8   nominal;// 0 = plain uN/iN · 1 = usize/idiff (RFC-0052 D11 — 구분 타입)
    double      fval;   // float literal 의 값 (lit && k == TK_FLOAT 일 때)
    bool        has_rng;// RFC-0055 D7: 선언된 **범위**를 가진 정수 (range [τ] lo hi)
    proven_i64  rlo, rhi;
    bool        is_mut; // ★ `mut` 로 선언됐는가 — 슬라이스 원소 쓰기의 전제
    proven_u8   lanes;  // TK_VEC / TK_MASK: 레인 수
    proven_u8   vflt;   // TK_VEC: 원소가 부동인가
    proven_u8   ebits;  // TK_SLICE: 원소 폭(비트). 0 = 미상
    proven_u8   enom;   // TK_SLICE: 원소의 명목(1 = `usize` · `idiff`) — `slice usize` 와 `slice u64` 는 다른 타입이다(32 비트 대상에서는 원소의 크기도 다르다). RFC-0149 L4
    bool        esign;  // TK_SLICE: 원소 부호
    bool        eflt;   // TK_SLICE: 원소가 부동인가
    proven_u8   erow;   // TK_SLICE: 원소가 **줄**(`slice (array T M)`, RFC-0132 §13.10) — ebits 등은 줄 안 원소의 것. ★ RFC-0155 §8-6 — 수는 겹의 깊이:
                        //   1 = 줄 안이 원소 · 2 = 줄이 다시 줄의 배열 … `idx` · `for` 는 하나씩 줄인다(0 이 되면 원소의 슬라이스)
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
    // ★ X-0085 — 감싸개의 **알맹이 종류**(tk_t: 정수·부동·bool·슬라이스만, 0 = 미상). `option u64` 자리에 `option slice u8`
    //   이 들어가도 말이 없었다. 좁게 싣는다: 선언에 한 낱말로 적힌 알맹이만. 둘 다 알 때만 문다.
    proven_u8   pk;
    // ★★★ RFC-0149 L4 (2026-10-13 소유자 «깨져도 되니까 다 제대로») — 알맹이가 **정수 · 부동**이면 그 폭 · 부호 · 명목도 싣는다.
    //   `output option u64` 인 op 이 `usize` 를 `some` 으로 내도 서고, `some_value` 는 «모름» 을 내서 받는 쪽도 검사받지 않았다 —
    //   `usize` 와 `u64` 가 감싸개 속에서는 섞였다. pbits 0 = 미상(리터럴을 감쌌거나 선언이 한 낱말이 아니다) — 모르면 물지 않는다.
    proven_u8   pbits; bool psign; proven_u8 pnom;
    proven_u8str_view_t ename;  // ★ RFC-0148 S5 — `result T E` 의 오류 타입 이름(`error_value` 가 꺼낸다)
    proven_u8str_view_t pname;  // ★ RFC-0148 S5 — 알맹이의 이름(구조체 · 열거형). **꺼낼 때만** 쓴다 — 대조에는 쓰지 않는다(X-0085 의 «넓게 실었더니 빨개졌다»)
    proven_u8   pwrap, ipk;     // ★ RFC-0148 S5 — 알맹이가 또 감싼 것(pk == TK_WRAPPED)이면 그 감싸개의 종류와 **안쪽** 알맹이의 종류(폭은 pbits · psign · pnom 에)
    bool        bydecl;         // ★ RFC-0148 S5 — 모름이어도 S5 가 거절하지 않는다: 값의 타입이 **구성상** 받는 자리의 선언이다(`using` 패스가 선언대로 짓는 값)
                                //   이거나, 이 식이 이미 다른 진단으로 거절됐다(같은 자리를 두 번 말하지 않는다)
    proven_u8str_view_t nname;  // TK_NAMED: 머리 타입 이름
    proven_u32  targ;           // TK_NAMED: 틀 인자 낱말들의 해시(0 = 인자 없음/미상)
    bool        slit;           // ★ 직접 적은 문자열 · 텍스트 리터럴(TK_NAMED 로 나온다) — 수 · bool 자리에는 들지 않는다(2026-10-24)
    // ★ RFC-0154 §8-19 (가) — 환경의 **접힌 지역 `let`** 이 지닌 번역 시점 값(cfold 일 때만). 환경 칸에만 산다: env_find 가 지워서 돌려주므로
    //   식의 타입으로 번지지 않는다(번지면 `let m u64 add n. 1 .` 의 m 이 n 의 값을 지닌 것처럼 보인다).
    bool        cfold, cfu;
    proven_i64  cfv;
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
    proven_u8str_view_t mod;   // ★ RFC-0148 S4 — 선언한 모듈. 같은 이름의 구조체가 여러 모듈에 선다(`file.handle` · `pool.handle`)
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
// ★ RFC-0150 §8-28 — `subslice X …` · `view_array T X` 는 X 와 **같은 저장소**다(베끼지 않는다, 정본 §8.7.2 (4)).
//   쓰는 자리(`copy` · `store` 의 받는 쪽)는 그 뿌리 이름의 `mut` 을 본다 — 고칠 수 있음은 한 방향으로만 좁아진다(§8.8 (2)).
static const low_cst_t *tc_view_root(const low_cst_t *r) {
    for (int hops = 0; r && hops < 64; hops++) {
        while (r && r->kind == LOW_CST_GROUP && r->nkids == 1) r = r->kids[0];
        if (!r || r->kind != LOW_CST_FORM || r->nkids < 3 || r->kids[0]->kind != LOW_CST_ATOM) break;
        if (veq(r->kids[0]->tok.lex, "subslice")) r = r->kids[1];
        else if (veq(r->kids[0]->tok.lex, "view_array")) r = r->kids[2];
        else break;
    }
    return r;
}
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
static struct { proven_u8str_view_t name, target; proven_u8 nominal; } g_nts[TC_MAXSIG];
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
    if (veq(v, "u128")) return tk_int(128, false);   // ★ RFC-0151 Q1 ⓑ — 대상이 128 비트 정수를 가질 때만 선다(하강이 E-INT128-TARGET)
    if (veq(v, "i128")) return tk_int(128, true);
    if (veq(v, "usize")) { ty_t t = tk_int(low_ir_target()->addr_bits, false); t.nominal = 1; return t; }   // D11: u64 별칭 아님
    if (veq(v, "idiff")) { ty_t t = tk_int(low_ir_target()->addr_bits, true);  t.nominal = 1; return t; }
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
// ★ 2026-10-31 (W-20261031-0048) — `slice bool` 의 원소는 `bool` 이다. 원소 폭 칸(ebits)에 1 을 적어 둔다(1 비트 정수는 없다 — 수 원소와 섞이지 않는다).
//   전에는 0(모름)이라 `idx b. i.` 의 타입이 «모름» 이어서 `let x bool idx b. 1 . .` 이 E-TYPE-UNKNOWN 이었고, 배열 결과도 `bool` 을 받지 못했다(P4b).
static ty_t tc_elem_int(proven_u8 bits, bool sign) { return bits == 1 ? tk(TK_BOOL) : tk_int(bits, sign); }

// ── RFC-0055 D7: `range [τ] lo hi` — 범위는 **기계 타입의 정련**이다 ───────────
// τ 를 적으면 표현이 못 박히고 [lo,hi] ⊆ τ 가 **컴파일타임에 강제**된다(E-TYPE-RANGE).
// τ 를 생략하면 가장 싼 폭이 유도된다(D1) — 두 형태는 같은 타입을 낳는다.
// 어느 쪽이든 결과는 **TK_INT** 다. 그래서 RFC-0052 의 ⊑ 규칙 전부가 range 값에도 걸린다.
bool low_num_is_float(proven_u8str_view_t v);
static bool num_value(proven_u8str_view_t v, proven_u64 *out, bool *neg);
static proven_i64 ty_lo(proven_u8 bits, bool sign) {
    if (!sign) return 0;
    if (bits >= 64) return INT64_MIN;   // ★ 128 비트는 64 비트 칸에 다 안 든다 — 범위 셈에는 64 비트 끝을 쓴다(범위 타입은 64 비트까지)
    return -(proven_i64)((proven_u64)1 << (bits - 1));
}
static proven_i64 ty_hi(proven_u8 bits, bool sign) {
    if (!sign) return (bits >= 64) ? INT64_MAX : (proven_i64)(((proven_u64)1 << bits) - 1);
    if (bits >= 64) return INT64_MAX;
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
static bool num_value_w(proven_u8str_view_t v, proven_u64 *out, bool *neg, proven_u64 *hi);
static bool num_value(proven_u8str_view_t v, proven_u64 *out, bool *neg) { proven_u64 h; return num_value_w(v, out, neg, &h); }
// ★ RFC-0151 Q3 ⓧ — 값을 128 비트까지 센다. `u64` 를 넘으면 `*out` 은 `u64` 끝에서 멈추고(전처럼 «u32 에 안 맞는다») 위 비트는 `*hi` 에.
//   2^128 을 넘으면 `*hi` 도 끝에서 멈춘다(렉서가 이미 E-LIT-RANGE 로 거절했다).
static bool num_value_w(proven_u8str_view_t v, proven_u64 *out, bool *neg, proven_u64 *hi) {
    proven_size_t i = 0; *neg = false; *hi = 0;
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
#if defined(__SIZEOF_INT128__)
        if (*hi || val > (UINT64_MAX - d) / base) {   // 64 비트를 넘었다 — 128 비트로 이어 센다
            unsigned __int128 w = ((unsigned __int128)*hi << 64) | val;
            const unsigned __int128 MAXW = ~(unsigned __int128)0;
            w = (w > (MAXW - d) / base) ? MAXW : w * base + d;
            *hi = (proven_u64)(w >> 64); val = (proven_u64)w;
            continue;
        }
#endif
        val = (val > (UINT64_MAX - d) / base) ? UINT64_MAX : val * base + d;
    }
    *out = val;   // ★ 위 칸이 있으면 아래 칸은 정확한 아래 64 비트다 — 「`u64` 에 안 맞음」 은 `*hi` 가 말한다(lit_fits)
    return true;
}
// ★ RFC-0155 §8-6 (2026-10-30) — 줄 타입 마디(`array <원소> <m>` — 괄호는 벗긴다)의 겹 깊이와 맨 안의 원소 낱말. 줄이 아니면 0.
static proven_u8 tc_row_depth(const low_cst_t *rt, proven_u8str_view_t *leaf) {
    while (rt && rt->kind == LOW_CST_GROUP && rt->nkids == 1) rt = rt->kids[0];
    if (!rt || rt->kind != LOW_CST_FORM || rt->nkids != 3 || rt->kids[0]->kind != LOW_CST_ATOM || !veq(rt->kids[0]->tok.lex, "array")) return 0;
    if (rt->kids[1]->kind == LOW_CST_ATOM) { *leaf = rt->kids[1]->tok.lex; return 1; }
    proven_u8 d = tc_row_depth(rt->kids[1], leaf);
    return d && d < 200 ? (proven_u8)(d + 1) : 0;
}
static ty_t ty_of_number(proven_u8str_view_t v) {
    if (low_num_is_float(v)) {   // D3: 부동 리터럴도 comptime 무타입 — 값은 적합 검사용으로 들고 간다
        return (ty_t){ .k = TK_FLOAT, .lit = true, .fval = low_num_to_double(v) };
    }
    ty_t t = tk_int(0, false);
    t.lit = num_value_w(v, &t.val, &t.neg, &t.valhi);
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
    // ★ RFC-0151 Q3 ⓧ — `u64` 를 넘는 리터럴은 128 비트 자리에만 든다. 128 비트 자리: 부호 없음은 2^128 − 1 까지,
    //   부호 있음은 2^127 − 1(음수는 2^127)까지 — 위 64 비트(`valhi`)로 판정한다.
    if (lit.valhi && decl.bits != 128) return false;
    if (decl.bits == 128) {
        if (!decl.sign) return !(lit.neg && (lit.val || lit.valhi));
        if (lit.valhi < 0x8000000000000000ull) return true;
        return lit.neg && lit.valhi == 0x8000000000000000ull && lit.val == 0;   // −2^127
    }
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
               TC_INSTANCE, TC_STRUCT, TC_WRAP, TC_PAYLOAD, TC_UNKNOWN, TC_W128, TC_LITRANGE } tc_reason_t;   // TC_WRAP: option ↔ result   // TC_STRUCT: 서로 다른 명명 타입(X-0010)   // ★ TC_INSTANCE: 같은 틀의 **다른 인스턴스**(X-0009)
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

static tc_reason_t compat_known(ty_t decl, ty_t actual);
static proven_u8str_view_t g_unk_head;   // ★ RFC-0148 S5 — 마지막으로 «모름» 을 낸 식의 머리 낱말(진단이 무엇을 모르는지 말하게)
// ★★★ RFC-0148 S5 — **타입이 적힌 자리에 타입을 모르는 값이 오면 거절한다**(D2). 측정 단계: `LOWENT_S5=1` 일 때만 문다.
static bool tc_s5_on(void) { return true; }   // 2026-10-14 — 켰다(D2 «수가 0 이 된 뒤»): 코퍼스에서 정상 글의 «모름» 이 0 이 됐다
static bool tc_is_declared_tname(proven_u8str_view_t v);
static proven_u8str_view_t tc_canon_name(proven_u8str_view_t v);
static bool tc_decl_concrete(ty_t d) {
    // 선언된 이름 타입(구조체 · 열거 · 액터 · newtype)도 구체다 — 틀의 타입 인자(`t`)는 선언된 적이 없어 빠진다
    if (d.k == TK_NAMED) return d.nname.size && tc_is_declared_tname(tc_canon_name(d.nname));   // 별칭은 풀어서 — `type ten_bits bits 10 .` 은 이름 타입이 아니다
    return (d.k == TK_INT && (d.bits || d.nominal)) || d.k == TK_FLOAT || d.k == TK_BOOL || d.k == TK_SLICE || d.k == TK_WRAPPED ||
           d.k == TK_VEC || d.k == TK_MASK;
}
static tc_reason_t compat(ty_t decl, ty_t actual) {
    if (actual.k == TK_UNKNOWN && !actual.bydecl && tc_decl_concrete(decl) && tc_s5_on()) return TC_UNKNOWN;
    return compat_known(decl, actual);
}
// `newtype spot pt .` 의 값은 `lit pt do … end` 로 짓는다 — 리터럴(pt)이 newtype(spot)의 핵이면 그 자리에 들어간다
static bool tc_lit_builds(ty_t decl, ty_t actual) {
    if (!actual.lit || actual.k != TK_NAMED || decl.k != TK_NAMED || !decl.nname.size || !actual.nname.size) return false;
    for (proven_size_t q = 0; q < g_nnts; q++)
        if (proven_u8str_view_eq(g_nts[q].name, decl.nname) && proven_u8str_view_eq(g_nts[q].target, actual.nname)) return true;
    return false;
}
static proven_u8str_view_t g_sname[1024]; static proven_size_t g_nsname;   // 선언된 구조체 이름(검사 시작에 채운다)
static bool tc_is_struct_name(proven_u8str_view_t v) {
    proven_u8str_view_t b = v;
    for (proven_size_t i = 0; i < v.size; i++) if (v.ptr[i] == (proven_u8)'.') b = (proven_u8str_view_t){ v.ptr + i + 1, v.size - i - 1 };
    for (proven_size_t i = 0; i < g_nsname; i++) if (proven_u8str_view_eq(g_sname[i], v) || proven_u8str_view_eq(g_sname[i], b)) return true;
    return false;
}
static proven_u8str_view_t g_ename_t[1024]; static proven_size_t g_nename_t;   // 선언된 열거 이름(검사 시작에 채운다)
static proven_u8str_view_t g_aname_t[512]; static proven_size_t g_naname_t;   // 선언된 액터 이름(검사 시작에 채운다)
static bool tc_is_actor_name(proven_u8str_view_t v) {
    proven_u8str_view_t b = v;
    for (proven_size_t i = 0; i < v.size; i++) if (v.ptr[i] == (proven_u8)'.') b = (proven_u8str_view_t){ v.ptr + i + 1, v.size - i - 1 };
    for (proven_size_t i = 0; i < g_naname_t; i++) if (proven_u8str_view_eq(g_aname_t[i], v) || proven_u8str_view_eq(g_aname_t[i], b)) return true;
    return false;
}
static bool tc_is_enum_name(proven_u8str_view_t v) {
    proven_u8str_view_t b = v;
    for (proven_size_t i = 0; i < v.size; i++) if (v.ptr[i] == (proven_u8)'.') b = (proven_u8str_view_t){ v.ptr + i + 1, v.size - i - 1 };
    for (proven_size_t i = 0; i < g_nename_t; i++) if (proven_u8str_view_eq(g_ename_t[i], v) || proven_u8str_view_eq(g_ename_t[i], b)) return true;
    return false;
}
static bool tc_enum_has_payload(proven_u8str_view_t en);
static void tc_cast_enum_note(const void *nd, proven_u8str_view_t en);
static bool tc_is_tc(ty_t t) { return t.k == TK_NAMED && (veq(t.nname, "task") || veq(t.nname, "chan")); }
static tc_reason_t compat_known(ty_t decl, ty_t actual) {
    // ★ RFC-0148 S5 (소유자 2026-10-14 «핸들·채널에 타입을») — `task T` · `chan T` 는 정수가 아니고, 원소 타입이 타입의 일부다
    if (tc_is_tc(decl) || tc_is_tc(actual)) {
        if (tc_is_tc(decl) && tc_is_tc(actual)) {
            if (!proven_u8str_view_eq(decl.nname, actual.nname)) return TC_KIND;
            if (decl.pbits && actual.pbits) {
                if (decl.pnom != actual.pnom) return TC_NOMINAL;
                if (decl.pk != actual.pk || decl.pbits != actual.pbits) return TC_WIDTH;
                if (decl.psign != actual.psign) return TC_SIGN;
            }
            if ((decl.pbits && actual.pname.size) || (decl.pname.size && actual.pbits)) return TC_KIND;
            return TC_OK;
        }
        ty_t o = tc_is_tc(decl) ? actual : decl;
        // 수 가운데 핸들 자리에 들어가는 것은 리터럴 0 하나다 — «아직 흐름 · 채널을 받지 않은 핸들»(널, 정본 §10.3 (2c)).
        //   다른 수는 어느 흐름도 가리키지 않으므로 받지 않는다(`var h task u64 7 .`).
        if ((o.k == TK_INT && (!o.lit || o.neg || o.val != 0 || !tc_is_tc(decl))) || o.k == TK_FLOAT || o.k == TK_BOOL || o.k == TK_SLICE || o.k == TK_WRAPPED) return TC_KIND;
    }
    // ★★★ **이름 타입끼리도 newtype 이면 구별한다** (RFC-0002 §6.3.1 — struct core).
    //   ★ 범위를 좁게 둔다: **명목 id 를 가진 쪽이 있을 때만** 본다. 투명 `type X Y .` 별칭은
    //     명세가 *같은 타입*이라 했으므로(SPEC-004 §89) 오늘처럼 호환이어야 하고, 일반 struct 끼리의
    //     검사(`rc` 를 `pt` 자리에)는 **아직 없다** — 그건 이 항목과 별개의 더 넓은 틈이라 손대지 않는다.
    if (decl.k == TK_NAMED && actual.k == TK_NAMED && (decl.nominal || actual.nominal) &&
        decl.nominal != actual.nominal && !tc_lit_builds(decl, actual)) return TC_NOMINAL;   // 구조체 리터럴은 그 구조체를 핵으로 하는 newtype 을 짓는 길이다
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
        if (!proven_u8str_view_eq(a, b) && tc_is_declared_tname(a) && tc_is_declared_tname(b) && !tc_lit_builds(decl, actual))
            return TC_STRUCT;
    }
    // ★★★★★ **`option` 자리에 `result` 를 넣지 않는다**(그 반대도). 둘 다 종류를 아는 때만 문다.
    if (decl.k == TK_WRAPPED && actual.k == TK_WRAPPED &&
        decl.wrap && actual.wrap && decl.wrap != actual.wrap) return TC_WRAP;
    if (decl.k == TK_WRAPPED && actual.k == TK_WRAPPED && decl.wrap == actual.wrap &&
        decl.pk && actual.pk && decl.pk != actual.pk) return TC_PAYLOAD;
    if (decl.k == TK_WRAPPED && actual.k == TK_WRAPPED && decl.wrap == actual.wrap && decl.pk == actual.pk &&
        (decl.pk == TK_INT || decl.pk == TK_FLOAT) && decl.pbits && actual.pbits) {     // RFC-0149 L4 — 알맹이의 타입도 같아야 한다
        if (decl.pnom != actual.pnom) return TC_NOMINAL;
        if (decl.pbits != actual.pbits || decl.psign != actual.psign) return decl.psign != actual.psign ? TC_SIGN : TC_WIDTH;
    }
    // ★ RFC-0148 S5 — 선언된 구조체 · 열거형 값과 수 · 참거짓 · 슬라이스는 서로의 자리에 들어가지 않는다. 전에는 이름 타입이면 무엇이든 통과했다
    //   (`let q u8 ok_value r. .` 에 구조체가 섰다). 별칭이 수를 가리키면 이미 수로 풀렸다(ty_of_decl_r) — 남은 이름은 진짜 이름 타입이다.
    {
        bool dn = decl.k == TK_NAMED && decl.nname.size && tc_is_struct_name(tc_canon_name(decl.nname)) && !tc_is_tc(decl);
        bool an = actual.k == TK_NAMED && actual.nname.size && tc_is_struct_name(tc_canon_name(actual.nname)) && !tc_is_tc(actual);
        bool ds = decl.k == TK_INT || decl.k == TK_FLOAT || decl.k == TK_BOOL || decl.k == TK_SLICE;
        bool as = (actual.k == TK_INT && !actual.lit) || (actual.k == TK_FLOAT && !actual.lit) || actual.k == TK_BOOL || actual.k == TK_SLICE;
        if ((ds && an) || (dn && as)) return TC_KIND;
        // ★ 열거 값은 수가 아니다(소유자 2026-10-15 «열거 값을 수 자리에 쓰려면 cast»; 정본 §6.2.16 (5)) — 수 자리에 열거를,
        //   열거 자리에 수를(리터럴도) 두면 거절한다. 갈래의 번호가 필요하면 `cast <정수> <열거>` 로 적는다.
        bool de = decl.k == TK_NAMED && decl.nname.size && tc_is_enum_name(tc_canon_name(decl.nname));
        bool ae = actual.k == TK_NAMED && actual.nname.size && tc_is_enum_name(tc_canon_name(actual.nname));
        bool ass = actual.k == TK_INT || actual.k == TK_FLOAT || actual.k == TK_BOOL || actual.k == TK_SLICE;
        if ((ds && ae) || (de && ass)) return TC_KIND;
        // ★ 액터 자리에 들어가는 수는 리터럴 0 하나다 — «아직 띄우지 않은 액터»(소유자 2026-10-15; 정본 §10.2 (1d)). 다른 수는 받지 않는다.
        if (decl.k == TK_NAMED && decl.nname.size && tc_is_actor_name(tc_canon_name(decl.nname)) &&
            (actual.k == TK_INT || actual.k == TK_FLOAT || actual.k == TK_BOOL || actual.k == TK_SLICE) &&
            !(actual.k == TK_INT && actual.lit && !actual.neg && actual.val == 0)) return TC_KIND;
    }
    // ★★ 2026-10-24 (docs/known-defects/string-literal-passes-typed-place) — 문자열 리터럴은 `TK_NAMED` 로 나와서 바로 아래 줄이
    //   **어느 자리든** 받아 주었다: `output u64` 인 op 이 `return "wrong" .` 해도 서고 바이트 목록을 돌려줬다(정본 §6.2.17 — 문자열은
    //   `slice u8`). 이름 붙은 자리(`cstr` · 텍스트 타입 · 별칭)와 슬라이스 자리는 그대로 두고, 수 · bool · 벡터 · 감싸개 자리만 막는다.
    if (actual.slit && actual.rk == 0 && (decl.k == TK_INT || decl.k == TK_FLOAT || decl.k == TK_BOOL || decl.k == TK_VEC ||
                                         decl.k == TK_MASK || decl.k == TK_UNIT || decl.k == TK_WRAPPED)) return TC_KIND;
    if (decl.k == TK_UNKNOWN || actual.k == TK_UNKNOWN || decl.k == TK_NAMED || actual.k == TK_NAMED) return TC_OK;
    if (decl.k != actual.k) return TC_KIND;
    // ★ RFC-0155 §8-6 (2026-10-30) — 겹 깊이가 다른 줄의 슬라이스는 다른 타입이다(`array 2 array 3 array 4 u8` 을 `array 3 array 4 u8` 자리에).
    //   줄의 슬라이스를 원소의 슬라이스 자리에 둘 수도 없다 — `idx` 가 수 대신 줄을 낸다(반대도 같다). 원소를 모르는 슬라이스(`slice` · 틀)는 그대로 둔다.
    if (decl.k == TK_SLICE && actual.k == TK_SLICE && decl.ebits && actual.ebits && decl.erow != actual.erow) return TC_KIND;
    if (decl.k == TK_SLICE && decl.ebits && actual.ebits && !decl.eflt && !actual.eflt && !decl.erow && !actual.erow &&
        decl.ebits == actual.ebits && decl.enom != actual.enom) return TC_NOMINAL;   // RFC-0149 L4 — `slice usize` ≠ `slice u64`
    if (decl.k == TK_INT) {
        if (actual.lit) {                                                   // D3: 리터럴은 comptime 무타입
            if (actual.valhi && decl.bits != 128) return TC_LITRANGE;       // ★ RFC-0151 — 64 비트를 넘는 수는 128 비트 자리에만
            if (!lit_fits(actual, decl)) return TC_WIDTH;
            return lit_in_rng(actual, decl) ? TC_OK : TC_RANGE;             // ★ D7: 범위도 강제
        }
        // ★ D7: 두 범위가 **서로소**면 모든 값이 계약을 어긴다 — 검사가 아니라 컴파일 오류다.
        if (decl.has_rng && actual.has_rng && (actual.rlo > decl.rhi || actual.rhi < decl.rlo)) return TC_RANGE;
        if (!decl.bits || !actual.bits) return TC_OK;
        if (decl.nominal != actual.nominal) return TC_NOMINAL;   // D11
        // ★ RFC-0151 (2026-10-22) — 128 비트 자리에는 128 비트 값만 암묵으로 든다. 넓히기는 `widen u128 …` 로 적는다: 64 비트 칸의 값은
        //   위 절반을 들고 있지 않아서, 부호를 늘릴지 0 을 채울지를 넓히는 자리가 알아야 한다(원천의 부호가 그 자리에 실린다).
        if ((decl.bits == 128) != (actual.bits == 128)) return decl.bits == 128 && ty_sub(actual, decl) ? TC_W128 : (decl.sign != actual.sign ? TC_SIGN : TC_WIDTH);
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
        if (actual.lanes && decl.lanes != actual.lanes) return TC_LANES;   // 레인 0 = 받는 자리가 정한다(`load` · `splat`)
        if (!actual.lanes && !actual.bits) return TC_OK;                  // 레인도 원소도 받는 자리가 정한다(`load`)
        if (decl.vflt != actual.vflt) return TC_KIND;
        if (actual.bits && decl.bits != actual.bits) return TC_WIDTH;
        return TC_OK;
    }
    return TC_OK;
}
#include <stdarg.h>
#include <stdio.h>
static void tc_emit(tc_ctx_t *c, const char *code, const char *msg, proven_u32 line) {
    if (c->quiet) return;   // ★ 묻기만 하는 추론 중 — 위 tc_ctx_t.quiet 주석 참조
    low_diag_t d = { .sev = LOW_SEV_ERROR, .code = code, .msg = msg, .line = line, .col = 0,
                     .file = low_cst_file_for_line(c->curform, line) };
    (void)proven_array_push(c->diags, &d);
    *c->ok = false;
}
// ★★★ 2026-09-14 — **이름·타입을 대는 오류.** 문장은 그 진단의 `detail` 에 짓고 `msg` 는 NULL(렌더러는 low_diag_text).
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
            if (t.nominal) snprintf(buf, n, "%s%s", ref, t.sign ? "idiff" : "usize");
            else snprintf(buf, n, "%s%c%u", ref, t.sign ? 'i' : 'u', (unsigned)t.bits);
            return buf;
        case TK_FLOAT:
            if (t.lit || !t.bits) { snprintf(buf, n, "a float literal"); return buf; }
            snprintf(buf, n, "%sf%u", ref, (unsigned)t.bits); return buf;
        case TK_SLICE:
            if (!t.ebits) { snprintf(buf, n, "%sslice", ref); return buf; }
            if (t.erow) {   // ★ RFC-0155 §8-6 — 줄의 슬라이스는 겹을 말한다(줄 길이는 타입이 쥐지 않아 `…` 로 적는다)
                size_t o = (size_t)snprintf(buf, n, "%sslice", ref);
                for (unsigned d = 0; d < t.erow && o + 12 < n; d++) o += (size_t)snprintf(buf + o, n - o, " array …");
                if (o + 8 < n) snprintf(buf + o, n - o, " %c%u", t.eflt ? 'f' : t.esign ? 'i' : 'u', (unsigned)t.ebits);
                return buf;
            }
            if (t.enom && !t.eflt) { snprintf(buf, n, "%sslice %s", ref, t.esign ? "idiff" : "usize"); return buf; }
            snprintf(buf, n, "%sslice %c%u", ref, t.eflt ? 'f' : t.esign ? 'i' : 'u', (unsigned)t.ebits); return buf;
        case TK_NAMED:
            if (t.slit) { snprintf(buf, n, "a string literal"); return buf; }
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
// 이 op 의 입력 가운데 그 이름이 `cap <종류>` 로 선언됐으면 종류를 낸다 (RFC-0147 — 부름 자리의 `E-CAP-KIND`).
static bool tc_param_cap_kind(const low_cst_t *def, proven_u8str_view_t name, proven_u8str_view_t *kind) {
    if (!def || def->kind != LOW_CST_FORM) return false;
    low_op_header_t h = low_op_header(def);
    for (proven_size_t q = 0; q < h.np; q++) {
        if (!proven_u8str_view_eq(h.p[q].name, name)) continue;
        proven_size_t cw = h.p[q].core;
        if (cw + 1 < h.p[q].te && cw + 1 < def->nkids && def->kids[cw]->kind == LOW_CST_ATOM &&
            def->kids[cw + 1]->kind == LOW_CST_ATOM && veq(def->kids[cw]->tok.lex, "cap")) {
            *kind = def->kids[cw + 1]->tok.lex; return true;
        }
        return false;
    }
    return false;
}
// kind mismatches keep their site-specific code; width/sign get dedicated codes
static void tc_flag(tc_ctx_t *c, tc_reason_t r, const char *code, const char *msg, proven_u32 line) {
    if (r == TC_OK) return;
    if (r == TC_WIDTH)        tc_emit(c, "E-TYPE-WIDTH", "value does not fit the declared type (a literal out of range, or a narrowing — use narrow / narrow_wrap / narrow_sat / round_to)", line);
    else if (r == TC_LITRANGE) tc_emit(c, "E-LIT-RANGE", "this integer literal is wider than 64 bits, and only a 128-bit place (`u128` / `i128`) can hold it — "
                                       "the comptime precision is 128 bits, every other place is at most 64 (RFC-0052 D3 · RFC-0151)", line);
    else if (r == TC_W128)    tc_emit(c, "E-TYPE-WIDTH", "a 128-bit place takes a 128-bit value — widen it by name: `widen u128 <value> .` / `widen i128 <value> .` "
                                      "(a 64-bit value does not carry the upper half, and the widening is where its sign is known — RFC-0151)", line);
    else if (r == TC_SIGN)    tc_emit(c, "E-TYPE-SIGN", "sign mismatch: no value-preserving widening exists (widen both to a strictly wider signed type, or bitcast_sign)", line);
    else if (r == TC_NOMINAL) tc_emit(c, "E-TYPE-NOMINAL", "these are NOMINALLY DISTINCT types with the same representation — a `newtype` (or usize/idiff) is not interchangeable with its base or with another newtype. Convert explicitly with `cast <type> <value>` (RFC-0002 §6.3.1)", line);
    else if (r == TC_RANGE)   tc_emit(c, "E-TYPE-RANGE", "the value is outside the declared range (no value can satisfy it — this is a lie, not a check)", line);
    else if (r == TC_INSTANCE) tc_emit(c, "E-TYPE-INSTANCE", "two instances of the same generic template are DIFFERENT concrete types — a container opened for one element type cannot be bound to a variable declared for another. Monomorphization makes each instance a distinct struct (visible as `name#arg` in `--ir`), and their layouts differ; binding across them reads the same storage at the wrong element width and yields a wrong value with no error at all. Declare the variable with the SAME instance, or convert explicitly (RFC-0021 · RFC-0084)", line);
    else if (r == TC_WRAP) tc_emit(c, "E-TYPE-WRAP", "`option` and `result` are not the same shape — one says a value may be ABSENT, the other says producing it may FAIL, and code that reads the wrong one silently treats a failure as an absence (or the reverse). That confusion is exactly what the three-place answer exists to prevent, so the checker must not paper over it here. Unwrap what you were given, or change the declaration to match", line);
    else if (r == TC_PAYLOAD) tc_emit(c, "E-TYPE-WRAP", "the option/result holds a different kind of value than the declaration says — "
                                      "an integer, a float, a bool and a slice are not interchangeable inside it either, and taking the "
                                      "content out would read one as the other (X-0085). Declare the content type the value really has", line);
    else if (r == TC_STRUCT) tc_emit(c, "E-TYPE-STRUCT", "these are two DIFFERENT named types — the same field layout does not make them the same type. `meters` and `seconds` may both hold one u64 and still mean different things, and a checker that only compares representation lets that confusion through silently. Transparent `type` aliases are expanded before this comparison, so an alias and its target remain interchangeable exactly as the spec says (SPEC-004 §89); what is rejected here is a genuinely different name. Use the declared type, or convert explicitly", line);
    else if (r == TC_LANES)   tc_emit(c, "E-TYPE-LANES", "vector lane counts differ — two vectors of different width are different types (there is no implicit widening between them)", line);
    else if (r == TC_UNKNOWN && getenv("LOWENT_S5_HEAD")) tc_emitf(c, "E-TYPE-UNKNOWN", line, "HEAD[%.*s]", (int)g_unk_head.size, (const char *)g_unk_head.ptr);
    else if (r == TC_UNKNOWN) tc_emitf(c, "E-TYPE-UNKNOWN", line,
                                       "the checker does not know the type of this value (it comes from `%.*s`), and the place it goes to has a declared "
                                       "type — a value whose type is not known cannot be checked, so it is refused (RFC-0148 S5, canon 6.2 (2a)). "
                                       "Bind it through a name with a written type, convert it (`cast`), or report the word whose result type is missing",
                                       (int)(g_unk_head.size < 40 ? g_unk_head.size : 40), (const char *)g_unk_head.ptr);
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
// ★ X-0076 — 자격 붙은 부름(`files.open`)은 좁혀진 뒤 맨이름(`open`)이지만 가리킨 모듈을 `qual_mod` 에 남긴다. 그 모듈의
//   시그니처를 **먼저** 본다 — 안 그러면 같은 단위의 다른 모듈이 내보낸 `open` 을 집었다(growvec+trust).
// ★ RFC-0148 S5 (2026-10-14) — `use regex as rx .` 의 별칭. 모듈 이름은 좁혀져(`qual_mod`) 오는데 별칭은 그대로 와서
//   `rx.compile` 의 서명을 못 찾았다 — 결과가 «모름» 이었다(시험 글 · 매뉴얼에 110 자리).
static proven_u8str_view_t g_al_old[64], g_al_new[64];
static proven_size_t g_nal;
static const tc_sig_t *sig_find_alias(tc_ctx_t *c, proven_u8str_view_t v) {
    proven_size_t dot = v.size;
    for (proven_size_t i = 0; i < v.size; i++) if (v.ptr[i] == (proven_u8)'.') { if (dot != v.size) return NULL; dot = i; }
    if (dot == v.size || dot == 0 || dot + 1 >= v.size) return NULL;
    proven_u8str_view_t head = { v.ptr, dot }, rest = { v.ptr + dot + 1, v.size - dot - 1 };
    for (proven_size_t a = 0; a < g_nal; a++) {
        if (!proven_u8str_view_eq(head, g_al_new[a])) continue;
        for (proven_size_t i = 0; i < c->nsigs; i++)
            if (proven_u8str_view_eq(c->sigs[i].name, rest) && proven_u8str_view_eq(c->sigs[i].mod, g_al_old[a])) return &c->sigs[i];
    }
    return NULL;
}
// ★ RFC-0148 S5 (2026-10-14) — 열거형 변형의 알맹이 타입(`case rect w h` 의 `w` · `h`). 검사층에 이 표가 없어 갈래가 꺼낸 이름이 전부 «모름» 이었다.
typedef struct { proven_u8str_view_t en, vn; ty_t pt[8]; proven_u8 np; } tc_variant_t;
static bool g_tc_full;   // ★ RFC-0148 S5 — 아래 표가 찼다: 잘라 버리지 않고 `E-TYPE-LIMIT` 로 말한다(check-limits · RFC-0077 P1-5)
static tc_variant_t g_var[1024];
static proven_size_t g_nvar;
typedef struct { const tc_variant_t *v; proven_u8str_view_t fname; proven_u8 idx; } tc_varf_t;
static tc_varf_t g_varf[4096];
static bool tc_enum_has_payload(proven_u8str_view_t en) {
    proven_u8str_view_t b = en;
    for (proven_size_t i = 0; i < en.size; i++) if (en.ptr[i] == (proven_u8)'.') b = (proven_u8str_view_t){ en.ptr + i + 1, en.size - i - 1 };
    for (proven_size_t i = 0; i < g_nvar; i++)
        if ((proven_u8str_view_eq(g_var[i].en, en) || proven_u8str_view_eq(g_var[i].en, b)) && g_var[i].np) return true;
    return false;
}
static proven_size_t g_nvarf;
typedef struct { proven_u8str_view_t name, mod; ty_t ty;   // 모듈 맨 위의 `let` · `var` — 몸에서 이름으로 읽힌다
                 bool known, uns; proven_i64 v; low_cst_t *const *vk; proven_size_t vn; } tc_mconst_t;   // known: 번역 시점 값(RFC-0154 §8-19 (나))
static tc_mconst_t g_mc[512];
static proven_size_t g_nmc;
// ★★★ RFC-0154 §8-19 (가) · §8-21 (2026-10-30 · W-20261030-1440) — **검사층의 이름 표.** 번역층과 같은 접기(low_ctfe_value)를 부르되 이름은
//   이 층의 표에서 찾는다: 환경의 접힌 지역 `let` 을 먼저(가린 이름이 접히지 않은 것이면 접지 않는다), 그다음 모듈 상수(제 모듈 것을 먼저).
//   그래야 `comptime mul K. 100 .` 의 답을 받는 자리의 폭으로 잴 수 있다 — 하강층만 접으면 `u8` 자리에 600 이 들어간다(§8-20 의 결함이 이름으로 되살아난다).
typedef struct { const tc_var_t *env; proven_size_t nenv; proven_u8str_view_t curmod; } tc_ctfe_ud_t;
static bool tc_ctfe_find(const low_ctfe_names_t *nm, proven_u8str_view_t name, proven_i64 *out, bool *uns) {
    const tc_ctfe_ud_t *u = (const tc_ctfe_ud_t *)nm->ud;
    for (proven_size_t i = u->nenv; i-- > 0; )
        if (proven_u8str_view_eq(u->env[i].name, name)) {
            if (!u->env[i].ty.cfold) return false;
            *out = u->env[i].ty.cfv; *uns = u->env[i].ty.cfu; return true;
        }
    const tc_mconst_t *hit = NULL;
    for (proven_size_t q = 0; q < g_nmc; q++)
        if (proven_u8str_view_eq(g_mc[q].name, name)) { if (!hit || proven_u8str_view_eq(g_mc[q].mod, u->curmod)) hit = &g_mc[q]; }
    if (!hit || !hit->known) return false;
    *out = hit->v; *uns = hit->uns; return true;
}
typedef struct { proven_u8str_view_t name; bool isbool; } tc_opt_t;
static tc_opt_t g_opt[128];
static proven_size_t g_nopt;
static const tc_variant_t *variant_find(proven_u8str_view_t en, proven_u8str_view_t vn) {
    const tc_variant_t *only = NULL; proven_size_t hits = 0;
    for (proven_size_t i = 0; i < g_nvar; i++) {
        if (!proven_u8str_view_eq(g_var[i].vn, vn)) continue;
        if (en.size && proven_u8str_view_eq(g_var[i].en, en)) return &g_var[i];
        only = &g_var[i]; hits++;
    }
    return hits == 1 ? only : NULL;
}
// ★★ 2026-10-15 (RFC-0150 D4) — 칸 타입의 **모든 낱말**을 모은다, 괄호 안까지.
//   순환 검사는 칸 타입의 맨 낱말(`option node`)만 보고 괄호(`option (option node)`)는 건너뛰었다. 그래서 크기가 무한한
//   `option (option node)` 가 `--check` 와 `--emit-c` 를 지났고, `option (owned node)` · `option (ref node)` 도 같은 까닭으로
//   지났다 — 맨 꼴(`owned node` · `ref node`)은 거절되는데. 괄호 꼴은 머리가 FORM 의 `tok` 에 있다(kids 가 아니다).
//   ☞ *모양이 둘인 것을 하나만 보면, 다른 하나로 쓴 글은 검사 밖이다.*
static void tc_type_words(const low_cst_t *n, proven_u8str_view_t *out, proven_size_t *nout, proven_size_t cap, bool *over) {
    if (!n) return;
    if (n->kind == LOW_CST_ATOM) {
        if (*nout >= cap) { *over = true; return; }
        out[(*nout)++] = n->tok.lex;
        return;
    }
    if (n->kind == LOW_CST_FORM && n->tok.lex.size) {
        if (*nout >= cap) { *over = true; return; }
        out[(*nout)++] = n->tok.lex;
    }
    for (proven_size_t i = 0; i < n->nkids; i++) tc_type_words(n->kids[i], out, nout, cap, over);
}

static const tc_sig_t *sig_find_at(tc_ctx_t *c, const low_cst_t *at) {
    if (at && !at->qual_mod.size && g_nal) { const tc_sig_t *s = sig_find_alias(c, at->tok.lex); if (s) return s; }
    if (at && at->qual_mod.size)
        for (proven_size_t i = 0; i < c->nsigs; i++)
            if (proven_u8str_view_eq(c->sigs[i].name, at->tok.lex) && c->sigs[i].mod.size &&
                proven_u8str_view_eq(c->sigs[i].mod, at->qual_mod)) return &c->sigs[i];
    return at ? sig_find(c, at->tok.lex) : NULL;
}
static ty_t env_find(const tc_var_t *env, proven_size_t n, proven_u8str_view_t name, bool *found) {
    for (proven_size_t i = n; i-- > 0; ) if (proven_u8str_view_eq(env[i].name, name)) { *found = true; ty_t t = env[i].ty; t.cfold = false; return t; }
    *found = false; return tk(TK_UNKNOWN);
}
// ★ 받는 자리(`copy` · `swap` 의 슬라이스)가 읽기 전용인가: 뿌리 이름이 `mut` 아닌 슬라이스 지역이거나, 지역이 아닌 배열 모듈 상수
//   (RFC-0015 §6.3 — 번역 때 돈 `fn` 의 바이트가 읽기 전용 상수 풀에 있다). 모르면 false.
static bool tc_slice_ro(tc_ctx_t *c, const low_cst_t *place, const tc_var_t *env, proven_size_t nenv) {
    const low_cst_t *cr = tc_view_root(place);
    if (!cr || cr->kind != LOW_CST_ATOM) return false;
    bool fnd; ty_t d0 = env_find(env, nenv, cr->tok.lex, &fnd);
    if (fnd) return d0.k == TK_SLICE && !d0.is_mut;
    for (proven_size_t q = 0; q < g_nmc; q++)
        if (proven_u8str_view_eq(g_mc[q].name, cr->tok.lex) && g_mc[q].ty.k == TK_SLICE) { (void)c; return true; }
    return false;
}

// forward
static ty_t tc_infer(tc_ctx_t *c, const low_cst_t *nd, const tc_var_t *env, proven_size_t nenv);
static void tc_push_ctx(low_cst_t *const *k, proven_size_t start, proven_size_t n, ty_t decl, ty_t actual);   // RFC-0148 §8-11

// ── RFC-0052 D1: 이항 수치 연산의 피연산자 규칙 ──────────────────────────────
// 두 피연산자는 ⊑ 로 **비교 가능**해야 한다. 결과 = join(넓은 쪽).
// ★ join_sound (Qed): 비교 가능하면 join 이 둘 다를 손실 없이 담는다.
// 리터럴은 comptime 무타입이므로 상대 타입을 취한다(D3).
// RFC-0149 L4 — 감싼 값의 타입을 감싸개에 싣는다(리터럴 · 폭 미상은 «모름» 으로 남긴다).
static void tc_wrap_payload(ty_t *w, ty_t pv) {
    if (pv.k == TK_INT || pv.k == TK_FLOAT || pv.k == TK_BOOL || pv.k == TK_SLICE || pv.k == TK_WRAPPED) w->pk = (proven_u8)pv.k;
    if ((pv.k == TK_INT || pv.k == TK_FLOAT) && !pv.lit && pv.bits && !pv.rk) { w->pbits = pv.bits; w->psign = pv.sign; w->pnom = pv.nominal; }
    if (pv.k == TK_SLICE && pv.ebits && !pv.erow) { w->ebits = pv.ebits; w->esign = pv.esign; w->eflt = pv.eflt; w->enom = pv.enom; }
    if (pv.k == TK_WRAPPED) {   // 안쪽 감싸개의 종류와 알맹이(정수 · 부동 · 참거짓)를 싣는다
        w->pwrap = pv.wrap;
        if ((pv.pk == TK_INT || pv.pk == TK_FLOAT) && pv.pbits) { w->ipk = pv.pk; w->pbits = pv.pbits; w->psign = pv.psign; w->pnom = pv.pnom; }
        else if (pv.pk == TK_BOOL) w->ipk = TK_BOOL;
    }
}
// ★ 열거 값이 수를 받는 자리(비트 연산 · 시프트 · 첨자 · 좁히기 · 넓히기)에 왔는가 — 왔으면 말하고 참(소유자 2026-10-15; 정본 §6.2.16 (5))
static bool tc_enum_as_num(tc_ctx_t *c, ty_t t, const char *code, proven_u32 line) {
    if (!(t.k == TK_NAMED && t.nname.size && tc_is_enum_name(tc_canon_name(t.nname)))) return false;
    tc_emit(c, code, "an enum value is used where a number goes — an enum is not a number. "
            "Write `cast <integer> <value>` to take the variant's number (canon 6.2.16 (5))", line);
    return true;
}
static bool g_cmp_eq;    // tc_binop 을 부르는 쪽이 세운다 — 견줌이 `eq` · `ne` 인가(열거는 같음만 묻는다)
static bool g_cmp_ctx;   // 중위 `expr` 의 견줌 — 그 길은 견줌도 셈처럼 이어 재므로(is_cmp=false) 열거 판정에만 따로 알린다
static ty_t tc_binop(tc_ctx_t *c, ty_t a, ty_t b, bool is_cmp, proven_u32 line) {
    bool eqop = g_cmp_eq, cctx = g_cmp_ctx; g_cmp_eq = false; g_cmp_ctx = false;
    {
        // ★ 열거 값은 수가 아니다(소유자 2026-10-15; 정본 §6.2.16 (5)). 같은 열거의 두 값은 `eq` · `ne` 로 견줄 수 있고,
        //   그 밖(셈 · 크기 견줌 · 수와 섞기)은 갈래의 번호를 쓰는 일이므로 `cast <정수> <열거>` 로 적는다.
        bool ae = a.k == TK_NAMED && a.nname.size && tc_is_enum_name(tc_canon_name(a.nname));
        bool be = b.k == TK_NAMED && b.nname.size && tc_is_enum_name(tc_canon_name(b.nname));
        if (ae || be) {
            bool same = ae && be && proven_u8str_view_eq(tc_canon_name(a.nname), tc_canon_name(b.nname));
            bool other_unknown = (ae && b.k == TK_UNKNOWN) || (be && a.k == TK_UNKNOWN);
            bool cmpish = is_cmp || cctx;
            // 같은 열거의 두 값은 크기로도 견준다(소유자 2026-10-15 «열거끼리 크기 비교를 넣읍시다») — 번호가 선언 차례대로 오르므로 차례가 곧 번호다
            if (!(cmpish && (same || (eqop && other_unknown)))) {
                tc_emit(c, "E-TYPE-KIND",
                        cmpish && eqop
                        ? "an enum value is compared with something that is not the same enum — an enum is not a number. "
                          "Compare two values of the same enum, or write `cast <integer> <value>` to take the variant's number (canon 6.2.16 (5))"
                        : cmpish ? "an enum value is ordered against something that is not the same enum — an enum is not a number. "
                          "Order two values of the same enum, or write `cast <integer> <value>` to take the variant's number (canon 6.2.16 (5))"
                        : "an enum value is used in arithmetic — an enum is not a number. "
                          "Write `cast <integer> <value>` to take the variant's number; that conversion is explicit (canon 6.2.16 (5))", line);
                ty_t u = tk(TK_UNKNOWN); u.bydecl = true; return is_cmp ? tk(TK_BOOL) : u;
            }
            return is_cmp ? tk(TK_BOOL) : (ae ? a : b);   // 중위 견줌은 결과를 끝에서 bool 로 낸다
        }
    }
    if (a.k == TK_UNKNOWN || b.k == TK_UNKNOWN || a.k == TK_NAMED || b.k == TK_NAMED)
        return is_cmp ? tk(TK_BOOL) : tk(TK_UNKNOWN);          // 보수적: 모르면 넘어간다
    // 리터럴은 상대의 타입을 취한다(값 적합은 그쪽 경계에서 검사)
    if (a.lit && !b.lit) { if (!lit_fits(a, b)) tc_flag(c, (a.valhi && b.bits != 128) ? TC_LITRANGE : TC_WIDTH, NULL, NULL, line);
                           a = b; }
    else if (b.lit && !a.lit) { if (!lit_fits(b, a)) tc_flag(c, (b.valhi && a.bits != 128) ? TC_LITRANGE : TC_WIDTH, NULL, NULL, line);
                                b = a; }
    else if (a.lit && b.lit) return is_cmp ? tk(TK_BOOL) : a;   // comptime — 무타입 유지
    if (a.k != b.k) {                                           // D8: kind 변경은 암묵 아님
        tc_emit(c, "E-TYPE-MIX",
                "int and float do not mix: convert explicitly (float_of / round_to / trunc_to)", line);
        { ty_t u = tk(TK_UNKNOWN); u.bydecl = true; return is_cmp ? tk(TK_BOOL) : u; }   // 이미 말했다 — S5 가 다시 말하지 않는다
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
    if (a.k == TK_INT && (a.bits == 128) != (b.bits == 128)) {   // ★ RFC-0151 — 128 비트와 다른 폭은 섞지 않는다(넓히기를 적는다)
        tc_flag(c, TC_W128, NULL, NULL, line);
        { ty_t u = tk(TK_UNKNOWN); u.bydecl = true; return is_cmp ? tk(TK_BOOL) : u; }
    }
    if (!ty_comparable(a, b)) {
        tc_flag(c, a.nominal != b.nominal ? TC_NOMINAL
                : (a.k == TK_INT && a.sign != b.sign) ? TC_SIGN : TC_WIDTH, NULL, NULL, line);
        { ty_t u = tk(TK_UNKNOWN); u.bydecl = true; return is_cmp ? tk(TK_BOOL) : u; }   // 이미 말했다 — S5 가 다시 말하지 않는다
    }
    return is_cmp ? tk(TK_BOOL) : ty_join(a, b);                // ★ 발명하지 않는다
}

// expr island — **타입을 소거하지 않는다**(RFC-0052 §6.2). 중위 식을 좌결합 트리로 보고
// T-ARITH/T-CMP 를 재귀 적용한다. (이전 구현은 결과를 tk_int(0,false) 로 뭉개서
//  식 안으로 한 걸음만 들어가면 폭·부호가 소멸했다 — 뮤테이션 지표 54% silent 의 원인.)
// 원소 타입 → **타입 있는 슬라이스** ty_t (ebits/eflt/esign 를 실어). view_array·subslice 결과용.
static ty_t tc_slice_of_elem(ty_t e) {
    ty_t sl = tk(TK_SLICE);
    sl.ebits = e.k == TK_BOOL ? 1 : e.bits; sl.esign = e.sign; sl.eflt = (e.k == TK_FLOAT); sl.enom = e.nominal == 1 ? 1 : 0;
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
    bool have = false, cmp_seen = false, pend_cmp = false, pend_eq = false;
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
                    // ★ RFC-0148 S4 (2026-10-14) — 피연산자는 **소리 내어** 잰다. 전에는 타입만 묻고(quiet) 다른 길이 재 주기를 바랐는데,
                    //   어느 길도 재지 않아 `and lt add r. i. . 3 . true .` 의 `u64 + usize` 가 섰다.
                    ty_t o = tc_infer(c, k[q], env, nenv);
                    if (o.k == TK_INT || o.k == TK_FLOAT)
                        tc_emit(c, "E-TYPE-LOGICAL",
                                "`and`/`or`/`not` take bool, not a number — this language has no "
                                "truthiness (D12: no implicit int↔bool). Say what the test is: "
                                "`ne x 0` instead of `x`", k[q]->line);
                }
                return tk(TK_BOOL);
            }
            if (veq(v, "eq") || veq(v, "ne") || veq(v, "lt") || veq(v, "le") ||
                veq(v, "gt") || veq(v, "ge")) { cmp_seen = true; pend_cmp = true; pend_eq = veq(v, "eq") || veq(v, "ne"); continue; }
            if (v.size == 1 && (v.ptr[0] == '+' || v.ptr[0] == '-' || v.ptr[0] == '*' ||
                                v.ptr[0] == '/' || v.ptr[0] == '%')) continue;   // 중위 연산자
        }
        // ★ RFC-0148 S4 (2026-10-14) — 섬 안의 괄호는 **또 하나의 섬**이다. 전에는 묶음의 첫 낱말만 재서(`(x. * 100)` → `x.`)
        //   `expr (x. * 100) + 1` 의 나머지가 안 보였고, 묶음 안의 `(field c. n .)` 은 아예 안 보였다.
        const low_cst_t *gi = k[i];
        while (gi->kind == LOW_CST_GROUP && gi->nkids == 1) gi = gi->kids[0];
        bool sub = false;
        if (gi->kind == LOW_CST_FORM || gi->kind == LOW_CST_GROUP)
            for (proven_size_t q = 1; q < gi->nkids && !sub; q++) sub = tc_prec(gi->kids[q]) >= 0;
        ty_t t = sub ? tc_infer_expr(c, gi->kids, gi->nkids, env, nenv) : tc_infer(c, k[i], env, nenv);
        if (!have) { acc = t; have = true; }
        else { g_cmp_eq = pend_cmp && pend_eq; g_cmp_ctx = pend_cmp; acc = tc_binop(c, acc, t, false, k[i]->line); pend_cmp = false; }   // 좌결합 — 짝마다 규칙 적용
    }
    if (cmp_seen) return tk(TK_BOOL);
    return have ? acc : tk(TK_UNKNOWN);
}
// operand run: 1 → itself; expr → island; cast → target scalar; else → call
// ★★★ RFC-0148 S4 (2026-10-14) — **액터의 proc 에도 서명이 있다.** `send <액터> <proc> <인자…>` 는 검사층에 아예 없었다: 인자는 보지 않았고
//   결과는 «모름» 이어서 어느 선언에나 들어갔다(`let a u8 send t. total . .`). 액터의 처리기 이름은 op 표(`sigs`)에 넣지 않는다 —
//   넣으면 같은 이름의 모듈 op 을 가린다. 그래서 (액터, proc) 으로 찾는 표를 따로 둔다.
typedef struct {
    proven_u8str_view_t actor, mod, proc;
    ty_t                params[TC_MAXP];
    proven_size_t       nparams;
    ty_t                ret;
} tc_asig_t;
static tc_asig_t g_asig[TC_MAXSIG];
static proven_size_t g_nasig;
static proven_u8str_view_t tc_base_name(proven_u8str_view_t v, proven_u8str_view_t *pre) {
    proven_size_t dot = v.size;
    for (proven_size_t i = 0; i < v.size; i++) { if (v.ptr[i] == '#') break; if (v.ptr[i] == '.') dot = i; }
    if (pre) *pre = dot < v.size ? (proven_u8str_view_t){ v.ptr, dot } : (proven_u8str_view_t){ 0 };
    return dot < v.size ? (proven_u8str_view_t){ v.ptr + dot + 1, v.size - dot - 1 } : v;
}
// 액터 타입 이름(`allocs.bump_bytes` · `bump_bytes`)과 proc 이름으로 찾는다. 같은 이름의 액터가 여러 모듈에 있으면 한정자나 제 모듈이
// 가리는 것만 답한다 — 가릴 수 없으면 모른다고 답한다(틀린 서명으로 재느니 재지 않는다).
static const tc_asig_t *asig_find(tc_ctx_t *c, proven_u8str_view_t tname, proven_u8str_view_t proc) {
    proven_u8str_view_t pre, base = tc_base_name(tname, &pre);
    const tc_asig_t *one = NULL, *best = NULL; proven_size_t nact = 0;
    proven_u8str_view_t seen_mod = { 0 }; bool have_seen = false;
    for (proven_size_t i = 0; i < g_nasig; i++) {
        const tc_asig_t *a = &g_asig[i];
        if (!proven_u8str_view_eq(a->actor, base) && !proven_u8str_view_eq(a->actor, tname)) continue;
        if (!have_seen || !proven_u8str_view_eq(seen_mod, a->mod)) { nact++; seen_mod = a->mod; have_seen = true; }
        if (!proven_u8str_view_eq(a->proc, proc)) continue;
        one = a;
        if ((pre.size && proven_u8str_view_eq(a->mod, pre)) || (!pre.size && c->curmod.size && proven_u8str_view_eq(a->mod, c->curmod))) best = a;
    }
    return best ? best : (nact == 1 ? one : NULL);
}
static const tc_struct_t *struct_find(tc_ctx_t *c, proven_u8str_view_t name);   // 아래에 정의
static ty_t tc_infer_run_impl(tc_ctx_t *c, low_cst_t *const *k, proven_size_t start, proven_size_t n,
                         const tc_var_t *env, proven_size_t nenv);
static bool tc_bin2(proven_u8str_view_t v) {
    return veq(v, "add") || veq(v, "sub") || veq(v, "mul") || veq(v, "div") || veq(v, "mod") ||
           veq(v, "eq") || veq(v, "ne") || veq(v, "lt") || veq(v, "le") || veq(v, "gt") || veq(v, "ge");
}
static ty_t tc_binop(tc_ctx_t *c, ty_t a, ty_t b, bool is_cmp, proven_u32 line);
// 납작한 줄 하나를 인자 수 2 의 산술 · 비교로 읽는다 — 그 밖의 낱말은 한 마디로 잰다
static ty_t tc_prefix2(tc_ctx_t *c, low_cst_t *const *k, proven_size_t *pos, proven_size_t end, const tc_var_t *env, proven_size_t nenv) {
    if (*pos >= end) return tk(TK_UNKNOWN);
    const low_cst_t *h = k[(*pos)++];
    if (h->kind == LOW_CST_ATOM && tc_bin2(h->tok.lex)) {
        ty_t a = tc_prefix2(c, k, pos, end, env, nenv), b = tc_prefix2(c, k, pos, end, env, nenv);
        bool cmp = veq(h->tok.lex, "eq") || veq(h->tok.lex, "ne") || veq(h->tok.lex, "lt") || veq(h->tok.lex, "le") ||
                   veq(h->tok.lex, "gt") || veq(h->tok.lex, "ge");
        g_cmp_eq = veq(h->tok.lex, "eq") || veq(h->tok.lex, "ne");
        return tc_binop(c, a, b, cmp, h->line);
    }
    return tc_infer(c, h, env, nenv);
}
// 감싼 것의 알맹이 — `some_value` · `ok_value` · `try` 가 꺼내는 것(RFC-0148 S5). 모르면 «모름».
static ty_t tc_payload_of(ty_t w) {
    if (w.k != TK_WRAPPED) return tk(TK_UNKNOWN);
    if (w.pbits && (w.pk == TK_INT || w.pk == TK_FLOAT)) { ty_t pv = w.pk == TK_INT ? tk_int(w.pbits, w.psign) : (ty_t){ .k = TK_FLOAT, .bits = w.pbits }; pv.nominal = w.pnom; return pv; }
    if (w.pk == TK_BOOL) return tk(TK_BOOL);
    if (w.pk == TK_SLICE) { ty_t sl = tk(TK_SLICE); sl.ebits = w.ebits; sl.esign = w.esign; sl.eflt = w.eflt; sl.enom = w.enom; return sl; }
    if (w.pk == TK_WRAPPED) {
        ty_t in = tk(TK_WRAPPED); in.wrap = w.pwrap; in.pk = w.ipk;
        if (w.ipk == TK_INT || w.ipk == TK_FLOAT) { in.pbits = w.pbits; in.psign = w.psign; in.pnom = w.pnom; }
        return in;
    }
    if (w.nname.size) { ty_t pv = tk(TK_NAMED); pv.nname = w.nname; pv.targ = w.targ; return pv; }
    if (w.pname.size) { ty_t pv = tk(TK_NAMED); pv.nname = w.pname; return pv; }
    return tk(TK_UNKNOWN);
}
static void tc_unk_mark(const low_cst_t *nd);
// 빌트인 op 의 인자 수(표 하나 — low_arity.h). 모르면 -1
static int tc_builtin_arity(proven_u8str_view_t v) {
#define X(nm, w, a) if (veq(v, #nm)) return (a);
    LOW_BUILTINS_CORE(X)
#undef X
    return -1;
}
static ty_t tc_infer_run(tc_ctx_t *c, low_cst_t *const *k, proven_size_t start, proven_size_t n,
                         const tc_var_t *env, proven_size_t nenv) {
    ty_t t = tc_infer_run_impl(c, k, start, n, env, nenv);
    if (t.k == TK_UNKNOWN && n) {
        tc_unk_mark(k[start]);
        // 머리 낱말이 op 도, 지역 이름도, 빌트인 연산도 아니면 그 이름을 이름 검사(또는 하강층)가 거절한다
        //   (`E-NAME-…` · `E-BUILTIN-BARE`) — S5 가 같은 자리에 «모름» 을 덧붙이지 않는다
        if (k[start]->kind == LOW_CST_ATOM && k[start]->tok.kind == LOW_TOK_IDENT && k[start]->tok.kw == LOW_KW_NONE && n >= 2 &&
            !sig_find_at(c, k[start]) && !low_ir_is_builtin_op(k[start]->tok.lex)) {
            bool fnd = false; (void)env_find(env, nenv, k[start]->tok.lex, &fnd);
            if (!fnd && tc_builtin_arity(k[start]->tok.lex) < 0) t.bydecl = true;
        }
        // 인자 수가 틀린 빌트인 op 은 하강층이 `E-IR-ARITY` 로 말한다 — S5 가 앞서 «모름» 이라 하지 않는다
        if (k[start]->kind == LOW_CST_ATOM && !sig_find(c, k[start]->tok.lex)) {
            int ar = tc_builtin_arity(k[start]->tok.lex);
            if (ar >= 0 && (proven_size_t)ar != n - 1) t.bydecl = true;
        }
    }
    return t;
}
static ty_t tc_infer_run_impl(tc_ctx_t *c, low_cst_t *const *k, proven_size_t start, proven_size_t n,
                         const tc_var_t *env, proven_size_t nenv) {
    if (n == 0) return tk(TK_UNIT);
    if (n == 1) return tc_infer(c, k[start], env, nenv);
    // ★ RFC-0148 S4 (2026-10-14) — 원자 연산의 선택 절 `order <이름>`(RFC-0018 §6.1)은 정규화 뒤 폼 **바깥**에 남는다:
    //   `atomic_load c. 0 order relaxed .` = [폼, order, relaxed]. 그 꼴을 몰라 앞의 폼을 아예 재지 않았다(`let x u8 …` 가 섰다).
    if (n == 3 && k[start]->kind != LOW_CST_ATOM && k[start + 1]->kind == LOW_CST_ATOM && veq(k[start + 1]->tok.lex, "order") &&
        k[start + 2]->kind == LOW_CST_ATOM)
        return tc_infer(c, k[start], env, nenv);
    // ★ RFC-0148 S5 — 납작한 줄의 산술 · 비교(`add 14 add 30 45`): 인자 수 2 로 차례로 읽는다(하강층이 인자 수 표로 하는 것과 같다).
    //   정규화된 나무에서는 폼으로 묶여 오지만, 옛 표면을 읽는 단위 시험의 생성기는 납작한 줄을 낸다.
    if (n > 3 && k[start]->kind == LOW_CST_ATOM && tc_bin2(k[start]->tok.lex) && !sig_find(c, k[start]->tok.lex)) {
        proven_size_t pos = start;
        ty_t t = tc_prefix2(c, k, &pos, start + n, env, nenv);
        if (pos == start + n) return t;
    }
    // [lit, (<구조체> 몸)] 꼴의 줄(파이프 `with` 의 값 따위)
    if (n == 2 && k[start]->kind == LOW_CST_ATOM && k[start]->tok.kw == LOW_KW_LIT) {
        const low_cst_t *r = k[start + 1];
        while (r->kind == LOW_CST_GROUP && r->nkids == 1) r = r->kids[0];
        if (r->kind == LOW_CST_FORM && r->nkids >= 2 && r->kids[0]->kind == LOW_CST_ATOM && r->kids[r->nkids - 1]->kind == LOW_CST_BLOCK) {
            ty_t t = ty_of_word(r->kids[0]->tok.lex);
            if (t.k == TK_NAMED) { t.nname = r->kids[0]->tok.lex; t.lit = true; return t; }
        }
    }
    // 낱말 줄로 온 구조체 리터럴 [lit, <구조체>, 몸] — 폼으로 온 것과 같다(RFC-0148 S5)
    if (n >= 3 && k[start]->kind == LOW_CST_ATOM && k[start]->tok.kw == LOW_KW_LIT && k[start + 1]->kind == LOW_CST_ATOM &&
        k[start + n - 1]->kind == LOW_CST_BLOCK && !veq(k[start + 1]->tok.lex, "array") && !veq(k[start + 1]->tok.lex, "slice") &&
        !veq(k[start + 1]->tok.lex, "vec")) {
        ty_t t = ty_of_word(k[start + 1]->tok.lex);
        if (t.k == TK_NAMED) { t.nname = k[start + 1]->tok.lex; return t; }
    }
    if (k[start]->kind == LOW_CST_ATOM && k[start]->tok.kw == LOW_KW_EXPR)
        return tc_infer_expr(c, k + start + 1, n - 1, env, nenv);
    if (k[start]->kind == LOW_CST_ATOM) {
        if (k[start]->tok.kw == LOW_KW_SEND && n >= 3 && k[start + 2]->kind == LOW_CST_ATOM) {
            ty_t tt = tc_infer(c, k[start + 1], env, nenv);
            const tc_asig_t *as = (tt.k == TK_NAMED && tt.nname.size) ? asig_find(c, tc_canon_name(tt.nname), k[start + 2]->tok.lex) : NULL;
            for (proven_size_t i = 3; i < n; i++) {
                ty_t at = tc_infer(c, k[start + i], env, nenv);
                if (as && i - 3 < as->nparams)
                    tc_flag(c, compat(as->params[i - 3], at), "E-TYPE-ARG", "the argument sent to the actor's proc does not match its parameter",
                            k[start + i]->line ? k[start + i]->line : k[start]->tok.line);
            }
            if (as) return as->ret;
            // 액터는 아는데 그 처리기가 없으면 하강층이 `E-IR-UNDEF` 로 말한다 — S5 가 «모름» 을 덧붙이지 않는다
            { ty_t u = tk(TK_UNKNOWN); if (tt.k == TK_NAMED && tt.nname.size) u.bydecl = true; return u; }
        }
        // a fresh borrow: `ref X` / `mut_ref X` (core type conservative, rk known)
        if (veq(k[start]->tok.lex, "ref") || veq(k[start]->tok.lex, "mut_ref")) {
            // ★ RFC-0148 S5 — 빌린 것의 타입은 빌린 값의 타입이다(참조 표시만 붙는다). 전에는 핵이 «모름» 이라 `mut_ref u64` 자리에 무엇이든 섰다.
            ty_t t = n >= 2 ? tc_infer_run(c, k, start + 1, n - 1, env, nenv) : tk(TK_UNKNOWN);
            if (t.k == TK_INT || t.k == TK_FLOAT) { t.lit = false; t.has_rng = false; }
            t.rk = veq(k[start]->tok.lex, "mut_ref") ? 2 : 1;
            return t;
        }
        // ★ 순수 내장 op 의 **반환 타입**. 이것이 없어서 `index d 0` 이 TK_UNKNOWN 이 되고,
        //   그 위의 `try` 가 보수적으로 통과했다(변이 지표가 그것을 잡아냈다).
        //   보수적으로: 확실히 아는 것만 적는다.
        {
            proven_u8str_view_t h2 = k[start]->tok.lex;
            if (veq(h2, "len") || veq(h2, "bitset_count") || veq(h2, "stack_count") || veq(h2, "capacity")) {
                for (proven_size_t q = 1; q < n; q++) {
                    ty_t at = tc_infer(c, k[start + q], env, nenv);
                    // RFC-0149 L4 — `alloc_bytes … capacity <n>` 의 `n` 은 바이트 수다: `usize`.
                    if (veq(h2, "capacity") && at.k == TK_INT && !at.lit && at.bits && at.nominal != 1) {
                        tc_emit(c, "E-TYPE-NOMINAL", "a capacity is a `usize` — this one has a fixed-width integer type. Declare it `usize`, or write "
                                "`cast usize <value> .`", k[start]->tok.line);
                    }
                }
                { ty_t ut = tk_int(low_ir_target()->addr_bits, false); ut.nominal = 1; return ut; }   // RFC-0149 — `usize`
            }
            // RFC-0149 L4 — 크기 · 정렬 · 조각 수 · 레인 수도 `usize` 다(전에는 타입이 «모름» 이어서 어느 선언에나 들어갔다).
            if (!sig_find(c, h2) && (veq(h2, "size_of") || veq(h2, "align_of") || veq(h2, "native_lanes"))) {
                ty_t ut = tk_int(low_ir_target()->addr_bits, false); ut.nominal = 1; return ut;
            }
            // ★ RFC-0132 T2b-3b — `field <레코드> <칸>` 이 **타입 있는 슬라이스 칸**(배열 칸 · `slice u32` 칸)이면 그 타입을 준다.
            //   없으면 그 위의 `index` 가 원소를 u8 로 보았다(`array f64 2` 칸의 원소가 u8 로 읽혔다). 좁게: 타입 있는 슬라이스만.
            // ★★★ RFC-0148 S4 — **칸에서 읽은 값의 타입은 그 칸의 선언이다** — 어느 칸이든, 어느 식의 칸이든(`field field a. b . c .`).
            //   전에는 타입 있는 슬라이스 칸만 답했다: `let n u8 field q. total . .`(`total u64`)이 섰다. 하강층은 S2 부터 알고 있었다.
            // ★ RFC-0148 S4 (2026-10-14) — `method <받는 값> <이름> <인자…>` 는 받는 값의 타입에 딸린 op `<타입>.<이름>` 을 부른다(하강층이
            //   그렇게 푼다). 검사층은 이 낱말을 몰라 결과가 «모름» 이었다. 받는 값이 이름 하나이고 그 타입의 op 이 있을 때만 답한다.
            if (veq(h2, "method") && n >= 3 && !sig_find(c, h2) && k[start + 2]->kind == LOW_CST_ATOM) {
                // 받는 값 뒤로 낱말 원자가 이어지는 동안이 칸 경로 + 이름이고 마지막이 이름이다(하강층과 같은 규칙) · 받는 값이 폼이면 사슬이다
                ty_t rt = tc_infer(c, k[start + 1], env, nenv);
                proven_size_t ne = start + 2;
                while (ne + 1 < start + n && k[ne + 1]->kind == LOW_CST_ATOM && k[ne + 1]->tok.kind == LOW_TOK_IDENT && k[ne + 1]->tok.kw == LOW_KW_NONE) {
                    bool fnd = false; (void)env_find(env, nenv, k[ne + 1]->tok.lex, &fnd);
                    if (fnd) break;                                   // 지역 이름이면 인자다
                    const tc_struct_t *st = rt.k == TK_NAMED && rt.nname.size ? struct_find(c, rt.nname) : NULL;
                    if (!st) break;
                    bool got = false;
                    for (proven_size_t z = 0; z < st->nf; z++)
                        if (proven_u8str_view_eq(st->fname[z], k[ne]->tok.lex)) { rt = st->fty[z]; got = true; break; }
                    if (!got) break;
                    ne++;
                }
                for (proven_size_t q = ne + 1; q < start + n; q++) (void)tc_infer(c, k[q], env, nenv);
                if (ne != start + 2) {   // 칸 경로를 지났다 — 이름은 ne 의 것
                    if (rt.k == TK_NAMED && rt.nname.size) {
                        proven_u8str_view_t tb = tc_base_name(rt.nname, NULL), mn = k[ne]->tok.lex; char nb[256];
                        if (tb.size + 1 + mn.size < sizeof nb) {
                            memcpy(nb, tb.ptr, tb.size); nb[tb.size] = '.'; memcpy(nb + tb.size + 1, mn.ptr, mn.size);
                            const tc_sig_t *ms = sig_find(c, (proven_u8str_view_t){ (const proven_byte_t *)nb, tb.size + 1 + mn.size });
                            if (ms) return ms->ret;
                        }
                    }
                    return tk(TK_UNKNOWN);
                }
                if (rt.k == TK_NAMED && rt.nname.size) {
                    proven_u8str_view_t tb = tc_base_name(rt.nname, NULL), mn = k[start + 2]->tok.lex;
                    char nb[256];
                    if (tb.size + 1 + mn.size < sizeof nb) {
                        memcpy(nb, tb.ptr, tb.size); nb[tb.size] = '.'; memcpy(nb + tb.size + 1, mn.ptr, mn.size);
                        const tc_sig_t *ms = sig_find(c, (proven_u8str_view_t){ (const proven_byte_t *)nb, tb.size + 1 + mn.size });
                        if (ms) return ms->ret;
                    }
                    ty_t u = tk(TK_UNKNOWN); u.bydecl = true; return u;   // 그 타입에 그 메서드가 없다 — `E-METHOD-UNDEF` 가 말한다
                }
                return tk(TK_UNKNOWN);
            }
            //   `read_volatile <묶음> <레지스터>` 도 같은 칸을 읽는다(§7.6 — 다른 것은 합치거나 없애지 않는다는 것뿐이다).
            // 여러 단 `field r. a b .` — 칸을 차례로 따라간다(RFC-0148 S5)
            if (veq(h2, "field") && n >= 4 && !sig_find(c, h2)) {
                ty_t rt = tc_infer(c, k[start + 1], env, nenv);
                proven_size_t q0 = 2;
                // 값이 폼으로 묶이지 않고 낱말 줄로 왔으면(`field some_value some lit pt2 do … end . . px .`) 마지막 낱말이 칸이다
                if (k[start + 1]->kind == LOW_CST_ATOM && k[start + 1]->tok.kind == LOW_TOK_IDENT && rt.k == TK_UNKNOWN) {
                    bool fnd = false; (void)env_find(env, nenv, k[start + 1]->tok.lex, &fnd);
                    if (!fnd) { rt = tc_infer_run(c, k, start + 1, n - 2, env, nenv); q0 = n - 1; }
                }
                for (proven_size_t q = q0; q < n; q++) {
                    if (k[start + q]->kind != LOW_CST_ATOM) return tk(TK_UNKNOWN);
                    if (rt.k == TK_SLICE && k[start + q]->tok.kind == LOW_TOK_NUMBER) {   // 숫자 칸 = 첨자
                        if (rt.erow) { rt.erow--; continue; }
                        if (rt.ebits) { ty_t ev = rt.eflt ? (ty_t){ .k = TK_FLOAT, .bits = rt.ebits } : tc_elem_int(rt.ebits, rt.esign); ev.nominal = rt.enom; rt = ev; continue; }
                        if (rt.nname.size) { ty_t ev = tk(TK_NAMED); ev.nname = rt.nname; rt = ev; continue; }
                        return tk(TK_UNKNOWN);
                    }
                    const tc_struct_t *st = rt.k == TK_NAMED && rt.nname.size ? struct_find(c, rt.nname) : NULL;
                    if (!st && rt.k == TK_NAMED && rt.nname.size) st = struct_find(c, tc_canon_name(rt.nname));
                    if (!st) return tk(TK_UNKNOWN);
                    bool got = false;
                    for (proven_size_t z = 0; z < st->nf; z++)
                        if (proven_u8str_view_eq(st->fname[z], k[start + q]->tok.lex)) { rt = st->fty[z]; rt.is_mut = false; got = true; break; }
                    if (!got) return tk(TK_UNKNOWN);
                }
                return rt;
            }
            if ((veq(h2, "field") || veq(h2, "read_volatile")) && n == 3 && k[start + 2]->kind == LOW_CST_ATOM && !sig_find(c, h2)) {
                ty_t rt = tc_infer(c, k[start + 1], env, nenv);
                // 숫자 칸은 슬라이스의 첨자다(`field s. 0 .` ≡ `idx s. 0 .`)
                if (rt.k == TK_SLICE && k[start + 2]->tok.kind == LOW_TOK_NUMBER) {
                    if (rt.erow) { rt.erow--; return rt; }
                    if (rt.ebits) { if (rt.eflt) return (ty_t){ .k = TK_FLOAT, .bits = rt.ebits }; ty_t ev = tc_elem_int(rt.ebits, rt.esign); ev.nominal = rt.enom; return ev; }
                    if (rt.nname.size) { ty_t ev = tk(TK_NAMED); ev.nname = rt.nname; return ev; }
                    return tk(TK_UNKNOWN);
                }
                const tc_struct_t *st = rt.k == TK_NAMED && rt.nname.size ? struct_find(c, rt.nname) : NULL;
                if (!st && rt.k == TK_NAMED && rt.nname.size) st = struct_find(c, tc_canon_name(rt.nname));   // 투명 별칭(`type p2 pt .`)
                if (!st && rt.k == TK_NAMED && rt.nname.size)   // `newtype spot pt .` — 이름은 다르고 칸은 핵의 것이다
                    for (proven_size_t q = 0; q < g_nnts && !st; q++) if (proven_u8str_view_eq(g_nts[q].name, rt.nname)) st = struct_find(c, g_nts[q].target);
                if (st) for (proven_size_t q = 0; q < st->nf; q++)
                    if (proven_u8str_view_eq(st->fname[q], k[start + 2]->tok.lex)) { ty_t ft = st->fty[q]; ft.is_mut = false; return ft; }
            }
            // ★★★ RFC-0149 L4 (2026-10-13) — **비트 연산의 결과에도 타입이 있다.** 전에는 «폭 모르는 정수» 여서 `usize` 와 `u64` 가
            //   `bit_and` · `shr` 를 한 번 거치면 섞였고, 그 결과는 어느 선언에나 들어갔다. 두 피연산자의 타입이 같으면 그 타입,
            //   한쪽이 리터럴이면 다른 쪽의 타입, `usize` · `idiff` 와 고정 폭이 만나면 셈과 같이 거절한다. 폭만 다른 고정 폭끼리도
            //   이제 셈과 같다(아래 · RFC-0148 S4). 시프트의 결과는 왼쪽 피연산자의 타입이다.
            if (!sig_find(c, h2) && n == 3 && (veq(h2, "bit_and") || veq(h2, "bit_or") || veq(h2, "bit_xor"))) {
                ty_t a = tc_infer(c, k[start + 1], env, nenv), b = tc_infer(c, k[start + 2], env, nenv);
                if (tc_enum_as_num(c, a, "E-TYPE-KIND", k[start]->line) | tc_enum_as_num(c, b, "E-TYPE-KIND", k[start]->line)) { ty_t u = tk(TK_UNKNOWN); u.bydecl = true; return u; }
                if (a.k == TK_INT && b.k == TK_INT) {
                    tc_push_ctx(k, start + 1, 1, b, a);   // RFC-0148 §8-11
                    tc_push_ctx(k, start + 2, 1, a, b);
                    if (a.lit && !b.lit) { return b; }
                    if (b.lit && !a.lit) { return a; }
                    // RFC-0148 S4 — 한쪽의 폭을 모르면(무타입 리터럴을 민 `shl 1 n.` 따위) 아는 쪽의 타입이다 — 하강층의 join 과 같다
                    if (!a.bits && b.bits && !b.lit) { b.has_rng = false; return b; }
                    if (!b.bits && a.bits && !a.lit) { a.has_rng = false; return a; }
                    if (!a.lit && !b.lit && a.bits && b.bits) {
                        if (a.nominal != b.nominal && (a.nominal == 1 || b.nominal == 1)) {
                            return tc_binop(c, a, b, false, k[start]->line);
                        }
                        if (a.bits == b.bits && a.sign == b.sign && a.nominal == b.nominal) { a.has_rng = false; return a; }
                        // 폭이나 부호만 다르면 **셈과 같은 규칙**이다(정본 §6.3.5 (1″) · 2026-10-14 소유자 «덧셈과 같은 규칙»): 넓히기로 비교할 수
                        //   있으면 넓은 쪽, 없으면 거절. 전에는 «폭 모름» 이라 `u64` 와 `i8` 을 섞어도, 그 결과를 `u8` 에 담아도 섰다.
                        a.has_rng = false; b.has_rng = false;
                        return tc_binop(c, a, b, false, k[start]->line);
                    }
                }
                return tk_int(0, false);
            }
            if (!sig_find(c, h2) && n == 3 && (veq(h2, "shl") || veq(h2, "shr") || veq(h2, "wrap_shl") || veq(h2, "wrap_shr") ||
                                               veq(h2, "rotl") || veq(h2, "rotr"))) {
                ty_t a = tc_infer(c, k[start + 1], env, nenv), sb = tc_infer(c, k[start + 2], env, nenv);
                if (tc_enum_as_num(c, a, "E-TYPE-KIND", k[start]->line) | tc_enum_as_num(c, sb, "E-TYPE-KIND", k[start]->line)) { ty_t u = tk(TK_UNKNOWN); u.bydecl = true; return u; }
                if (a.k == TK_INT && !a.lit && a.bits) { a.has_rng = false; return a; }
                return tk_int(0, false);
            }
            // ★★★ RFC-0148 S4 — 시그니처 표가 이미 답한 낱말들: `leading_zeros` · `trailing_zeros` · `byte_swap` 의 결과는 **`a` 의 타입**이고
            //   `bit_cast T a` 의 결과는 `T` 다(§6.3.8). 검사층은 앞의 셋을 «폭 모르는 정수» 로, `bit_cast` 는 «모름» 으로 답하고 있었다.
            //   `count_ones` 도 같다(2026-10-14 소유자 «둘 다 피연산자 타입» — 셋 다 «몇 개» 를 세지만 결과는 `a` 의 폭이다).
            if (!sig_find(c, h2) && n == 2 && (veq(h2, "leading_zeros") || veq(h2, "trailing_zeros") || veq(h2, "byte_swap") || veq(h2, "count_ones"))) {
                ty_t a = tc_infer(c, k[start + 1], env, nenv);
                if (tc_enum_as_num(c, a, "E-TYPE-KIND", k[start]->line)) { ty_t u = tk(TK_UNKNOWN); u.bydecl = true; return u; }
                if (a.k == TK_INT && !a.lit && a.bits) { a.has_rng = false; return a; }
                return tk_int(0, false);
            }
            if (!sig_find(c, h2) && n == 3 && veq(h2, "bit_cast") && k[start + 1]->kind == LOW_CST_ATOM) {
                ty_t tgt = ty_of_word(k[start + 1]->tok.lex);
                (void)tc_infer(c, k[start + 2], env, nenv);
                if ((tgt.k == TK_INT || tgt.k == TK_FLOAT) && tgt.bits) return tgt;
                if (tgt.k == TK_BOOL) return tgt;   // 덫 표현이 있는 대상은 배치 검사가 따로 거절한다(E-TYPE-BITCAST)
                return tk(TK_UNKNOWN);
            }
            // ★★★ RFC-0148 S5 (2026-10-14) — 결과 타입이 피연산자에서 나오는 낱말들(검사층이 «모름» 으로 답하던 것).
            //   `value_or w d` — `w` 의 알맹이(모르면 `d` 의 타입) · `deref p` — 가리키는 값 · `ref x` / `mut_ref x` — `x` 를 빌린 것 ·
            //   `view_array T b` — 원소가 `T` 인 슬라이스 · `load s i` / `load_masked` · `splat v` — 원소가 정해진 벡터(레인 수는 받는 자리가 정한다).
            // `pipe <원천> do … <종결자> end` 의 값은 종결자가 낸 값이다(정본 §6.12 (2b)): `count` · `collect into` → `u64`,
            //   `any` · `all` → `bool`, `fold <초기값> <op>` → 그 op 의 출력.
            const low_cst_t *pbl = NULL;
            if (!sig_find(c, h2) && veq(h2, "pipe") && n >= 2) {
                if (k[start + n - 1]->kind == LOW_CST_BLOCK) pbl = k[start + n - 1];
                else if (n == 2 && k[start + 1]->kind == LOW_CST_FORM && k[start + 1]->nkids &&
                         k[start + 1]->kids[k[start + 1]->nkids - 1]->kind == LOW_CST_BLOCK) pbl = k[start + 1]->kids[k[start + 1]->nkids - 1];
            }
            if (pbl) {
                if (k[start + n - 1]->kind == LOW_CST_BLOCK) (void)tc_infer(c, k[start + 1], env, nenv);
                else if (k[start + 1]->nkids >= 2) (void)tc_infer(c, k[start + 1]->kids[0], env, nenv);
                const low_cst_t *bl = pbl;
                const low_cst_t *tf = bl->nkids ? bl->kids[bl->nkids - 1] : NULL;
                if (tf && tf->kind == LOW_CST_FORM && tf->nkids && tf->kids[0]->kind == LOW_CST_ATOM) {
                    proven_u8str_view_t tw = tf->kids[0]->tok.lex;
                    if (veq(tw, "count") || veq(tw, "collect")) return tk_int(64, false);
                    if (veq(tw, "any") || veq(tw, "all")) return tk(TK_BOOL);
                    if (veq(tw, "fold") && tf->nkids >= 3 && tf->kids[2]->kind == LOW_CST_ATOM) {
                        const tc_sig_t *fs = sig_find_at(c, tf->kids[2]);
                        if (fs) return fs->ret;
                    }
                }
                if (tf && tf->kind == LOW_CST_ATOM && veq(tf->tok.lex, "count")) return tk_int(64, false);
                return tk(TK_UNKNOWN);
            }
            // `include_text "<경로>"` — 번역 때 읽는 파일의 바이트: 문자열 리터럴과 같은 읽기 전용 `slice u8`(RFC-0015 §6.4 · 2026-10-31)
            if (!sig_find(c, h2) && n == 2 && veq(h2, "include_text")) { ty_t st = tk(TK_NAMED); st.slit = true; return st; }
            // `config <이름>` — 번역 시점의 상수(빌드 선택지): `bool` 선택지는 bool, `int` · `choice` 는 무타입 정수 상수다(D3)
            if (!sig_find(c, h2) && n == 2 && veq(h2, "config") && k[start + 1]->kind == LOW_CST_ATOM) {
                proven_u8str_view_t on = k[start + 1]->tok.lex;
                for (proven_size_t q = 0; q < g_nopt; q++)
                    if (proven_u8str_view_eq(g_opt[q].name, on)) {
                        if (g_opt[q].isbool) return tk(TK_BOOL);
                        ty_t t = tk_int(0, false); t.lit = true; t.val = 0;
                        // ★ RFC-0154 §8-21 (2026-10-30) — 고른 **값**을 싣는다: 받는 자리가 리터럴처럼 잰다. 전에는 0 을 실어 `hz` 가 1000 이어도
                        //   `u8` 자리를 지나갔다.
                        proven_i64 ov;
                        if (low_ir_option_int(on, &ov)) { if (ov < 0) { t.neg = true; t.val = (proven_u64)0 - (proven_u64)ov; } else t.val = (proven_u64)ov; }
                        return t;
                    }
                return tk(TK_UNKNOWN);
            }
            // `isa <값> <갈래>` — 참거짓 · `encode <짜임> <값>` — 바이트(`slice u8`) · `try_view T b` — `option` · `seg ss i` — 조각(원소는 조각 뷰의 것)
            if (!sig_find(c, h2) && n == 3 && veq(h2, "isa")) { (void)tc_infer(c, k[start + 1], env, nenv); return tk(TK_BOOL); }
            if (!sig_find(c, h2) && n == 3 && veq(h2, "encode")) { (void)tc_infer(c, k[start + 2], env, nenv); return tc_slice_of_elem(tk_int(8, false)); }
            if (!sig_find(c, h2) && n == 3 && veq(h2, "try_view")) {
                (void)tc_infer(c, k[start + 2], env, nenv);
                ty_t w = tk(TK_WRAPPED); w.wrap = 1;
                if (k[start + 1]->kind == LOW_CST_ATOM) { w.nname = k[start + 1]->tok.lex; }
                return w;
            }
            // `nonzero_of a` — `option nonzero T`(표): 알맹이는 `a` 의 타입이다
            if (!sig_find(c, h2) && n == 2 && veq(h2, "nonzero_of")) {
                ty_t a0 = tc_infer(c, k[start + 1], env, nenv);
                ty_t w = tk(TK_WRAPPED); w.wrap = 1; tc_wrap_payload(&w, a0); return w;
            }
            // `payload <값> <갈래> <칸>` — 그 갈래의 그 칸의 타입
            if (!sig_find(c, h2) && n == 4 && veq(h2, "payload") && k[start + 2]->kind == LOW_CST_ATOM && k[start + 3]->kind == LOW_CST_ATOM) {
                ty_t sv = tc_infer(c, k[start + 1], env, nenv);
                proven_u8str_view_t en = sv.k == TK_NAMED ? tc_base_name(sv.nname, NULL) : (proven_u8str_view_t){ 0 };
                const tc_variant_t *vv = variant_find(en, tc_base_name(k[start + 2]->tok.lex, NULL));
                if (vv) {
                    // 칸 이름의 자리를 찾는다 — 선언의 짝(칸 타입)을 다시 읽는다
                    for (proven_size_t q = 0; q < g_nvarf; q++)
                        if (g_varf[q].v == vv && proven_u8str_view_eq(g_varf[q].fname, k[start + 3]->tok.lex) && g_varf[q].idx < vv->np) return vv->pt[g_varf[q].idx];
                }
                return tk(TK_UNKNOWN);
            }
            // `div_nz a b` — `a` 의 타입(표) · `pop s` — 쌓개 원소의 option · `str_from_cstr` — 바이트 슬라이스
            if (!sig_find(c, h2) && n == 3 && veq(h2, "div_nz")) {
                ty_t a0 = tc_infer(c, k[start + 1], env, nenv); (void)tc_infer(c, k[start + 2], env, nenv);
                if ((a0.k == TK_INT) && a0.bits && !a0.lit) { a0.has_rng = false; a0.is_mut = false; return a0; }
                return a0.lit ? a0 : tk(TK_UNKNOWN);
            }
            if (!sig_find(c, h2) && n == 2 && veq(h2, "pop")) {
                ty_t sv = tc_infer(c, k[start + 1], env, nenv);
                ty_t w = tk(TK_WRAPPED); w.wrap = 1;
                if (sv.k == TK_NAMED && sv.ebits) { ty_t ev = sv.eflt ? (ty_t){ .k = TK_FLOAT, .bits = sv.ebits } : tc_elem_int(sv.ebits, sv.esign); ev.nominal = sv.enom; tc_wrap_payload(&w, ev); }
                return w;
            }
            if (!sig_find(c, h2) && n >= 2 && veq(h2, "str_from_cstr")) {
                for (proven_size_t q = 1; q < n; q++) (void)tc_infer(c, k[start + q], env, nenv);
                return tc_slice_of_elem(tk_int(8, false));
            }
            // ★★★ RFC-0148 S5 (소유자 2026-10-14 «핸들·채널에 타입을») — 흐름과 채널의 타입:
            //   `spawn <op> …` → `task <그 op 의 출력>` · `await h` → 그 출력 · `channel T` → `chan T` · `chrecv ch` → `T` ·
            //   `chsend ch v` → `v` 는 `T` 여야 하고 결과는 `u64`(0) · `try spawn send …` → `result u64 mailbox_full`.
            if (!sig_find(c, h2) && n >= 2 && veq(h2, "spawn") && k[start + 1]->kind == LOW_CST_ATOM &&
                !veq(k[start + 1]->tok.lex, "actor") && k[start + 1]->tok.kw != LOW_KW_SEND && !veq(k[start + 1]->tok.lex, "send")) {
                const tc_sig_t *ss = sig_find_at(c, k[start + 1]);
                if (ss) for (proven_size_t q = 2; q < n && q - 2 < ss->nparams; q++) {
                    ty_t at = tc_infer(c, k[start + q], env, nenv);
                    tc_flag(c, compat(ss->params[q - 2], at), "E-TYPE-ARG", "the argument given to the spawned op does not match its parameter", k[start]->line);
                }
                ty_t t = tk(TK_NAMED); t.nname = (proven_u8str_view_t){ (const proven_byte_t *)"task", 4 };
                if (ss) {
                    ty_t r = ss->ret;
                    if ((r.k == TK_INT || r.k == TK_FLOAT) && r.bits) { t.pk = (proven_u8)r.k; t.pbits = r.bits; t.psign = r.sign; t.pnom = r.nominal; }
                    else if (r.k == TK_BOOL) t.pk = TK_BOOL;
                    else if (r.k == TK_NAMED && r.nname.size) t.pname = r.nname;
                }
                return t;
            }
            if (!sig_find(c, h2) && n == 2 && (veq(h2, "await") || veq(h2, "chrecv"))) {
                ty_t h = tc_infer(c, k[start + 1], env, nenv);
                bool want_task = veq(h2, "await");
                if (h.k != TK_UNKNOWN && !(tc_is_tc(h) && veq(h.nname, want_task ? "task" : "chan")))
                    tc_emit(c, "E-TYPE-KIND", want_task ? "`await` takes a `task T` — the handle `spawn` gives (RFC-0148 S5)"
                                                        : "`chrecv` takes a `chan T` — the channel `channel T` makes (RFC-0148 S5)", k[start]->line);
                if (tc_is_tc(h)) {
                    if (h.pbits) { ty_t e = h.pk == TK_FLOAT ? (ty_t){ .k = TK_FLOAT, .bits = h.pbits } : tk_int(h.pbits, h.psign); e.nominal = h.pnom; return e; }
                    if (h.pk == TK_BOOL) return tk(TK_BOOL);
                    if (h.pname.size) { ty_t e = tk(TK_NAMED); e.nname = h.pname; return e; }
                }
                return tk(TK_UNKNOWN);
            }
            if (!sig_find(c, h2) && n >= 1 && veq(h2, "channel")) {
                ty_t t = tk(TK_NAMED); t.nname = (proven_u8str_view_t){ (const proven_byte_t *)"chan", 4 };
                if (n >= 2 && k[start + 1]->kind == LOW_CST_ATOM) {
                    ty_t e = ty_of_word(tc_canon_name(k[start + 1]->tok.lex));
                    if ((e.k == TK_INT || e.k == TK_FLOAT) && e.bits) { t.pk = (proven_u8)e.k; t.pbits = e.bits; t.psign = e.sign; t.pnom = e.nominal; }
                    else if (e.k == TK_BOOL) t.pk = TK_BOOL;
                    else if (e.k == TK_NAMED) t.pname = k[start + 1]->tok.lex;
                }
                return t;
            }
            if (!sig_find(c, h2) && n == 3 && veq(h2, "chsend")) {
                ty_t h = tc_infer(c, k[start + 1], env, nenv), v = tc_infer(c, k[start + 2], env, nenv);
                if (h.k != TK_UNKNOWN && !(tc_is_tc(h) && veq(h.nname, "chan")))
                    tc_emit(c, "E-TYPE-KIND", "`chsend` takes a `chan T` — the channel `channel T` makes (RFC-0148 S5)", k[start]->line);
                if (tc_is_tc(h) && h.pbits) {
                    ty_t e = h.pk == TK_FLOAT ? (ty_t){ .k = TK_FLOAT, .bits = h.pbits } : tk_int(h.pbits, h.psign); e.nominal = h.pnom;
                    tc_flag(c, compat(e, v), "E-TYPE-ARG", "the value sent does not have the channel's element type", k[start]->line);
                }
                return tk_int(64, false);
            }
            // `error_value r` — 오류 열거형 · `spawn actor X` — 그 액터 · `view T b` — 그 짜임 · `view_segments b d` — `segments u8` · `cstr_of s` — `cstr`
            if (!sig_find(c, h2) && n == 2 && veq(h2, "error_value")) {
                ty_t r = tc_infer(c, k[start + 1], env, nenv);
                if (r.k == TK_WRAPPED && r.ename.size) { ty_t e = tk(TK_NAMED); e.nname = r.ename; return e; }
                return tk(TK_UNKNOWN);
            }
            if (!sig_find(c, h2) && n == 3 && veq(h2, "spawn") && k[start + 1]->kind == LOW_CST_ATOM && veq(k[start + 1]->tok.lex, "actor") &&
                k[start + 2]->kind == LOW_CST_ATOM) { ty_t a = tk(TK_NAMED); a.nname = k[start + 2]->tok.lex; return a; }
            if (!sig_find(c, h2) && n == 3 && veq(h2, "view") && k[start + 1]->kind == LOW_CST_ATOM) {
                (void)tc_infer(c, k[start + 2], env, nenv);
                ty_t t = tk(TK_NAMED); t.nname = k[start + 1]->tok.lex; return t;
            }
            if (!sig_find(c, h2) && n == 3 && veq(h2, "view_segments")) {
                ty_t b0 = tc_infer(c, k[start + 1], env, nenv); (void)tc_infer(c, k[start + 2], env, nenv);
                ty_t t = tk(TK_NAMED); t.nname = (proven_u8str_view_t){ (const proven_byte_t *)"segments", 8 };
                if (b0.k == TK_SLICE && b0.ebits) { t.ebits = b0.ebits; t.esign = b0.esign; t.eflt = b0.eflt; t.enom = b0.enom; }
                return t;
            }
            if (!sig_find(c, h2) && n == 2 && veq(h2, "cstr_of")) {
                (void)tc_infer(c, k[start + 1], env, nenv);
                proven_u8str_view_t cw = { (const proven_byte_t *)"cstr", 4 };
                ty_t t = ty_of_word(cw); if (t.k == TK_NAMED) t.nname = cw; return t;   // 선언과 같은 길로(명목 정체성까지)
            }
            // `comptime <식>` — 그 식의 타입이다(번역 시점에 값이 나오는지는 하강층이 본다 — E-COMPTIME-NONCONST)
            if (!sig_find(c, h2) && n >= 2 && veq(h2, "comptime")) {
                ty_t t = tc_infer_run(c, k, start + 1, n - 1, env, nenv);
                // ★★★ RFC-0154 §8-20 (2026-10-30) — 접은 **값**을 리터럴처럼 들고 간다: 받는 자리가 `return 300 .` 과 같이 폭을 잰다(E-TYPE-WIDTH).
                //   전에는 첫 리터럴의 값만 들고 가서 `u8` 자리에 `comptime add 200 100 .` 의 300 이 들었다.
                // ★★★ RFC-0154 §8-21 (2026-10-30 · W-20261030-1440) — 이름을 접게 되자(§8-19 (가)) 답의 타입이 이름의 선언 타입(`u64` …)이 됐다.
                //   그대로 두면 `t.lit` 이 아니라 폭을 재지 않는다. 접은 답은 **상수**다 — 무타입 리터럴로 바꿔 받는 자리가 잰다(§8-20 과 같은 규칙).
                proven_i64 cv; bool cu;
                tc_ctfe_ud_t ud = { env, nenv, c->curmod };
                const low_ctfe_names_t nm = { tc_ctfe_find, &ud };
                if (t.k == TK_INT && n == 2 && low_ctfe_value(k[start + 1], &nm, &cv, &cu)) {
                    t = tk_int(0, false); t.lit = true;
                    t.valhi = 0;
                    if (cu || cv >= 0) { t.val = (proven_u64)cv; t.neg = false; }
                    else { t.val = (proven_u64)0 - (proven_u64)cv; t.neg = true; }
                }
                return t;
            }
            if (!sig_find(c, h2) && n == 3 && veq(h2, "value_or")) {
                ty_t w = tc_infer(c, k[start + 1], env, nenv), d = tc_infer(c, k[start + 2], env, nenv);
                if (w.k == TK_WRAPPED && w.pbits && (w.pk == TK_INT || w.pk == TK_FLOAT)) {
                    ty_t pv = w.pk == TK_INT ? tk_int(w.pbits, w.psign) : (ty_t){ .k = TK_FLOAT, .bits = w.pbits };
                    pv.nominal = w.pnom;
                    if (d.k == TK_INT || d.k == TK_FLOAT) tc_flag(c, compat(pv, d), "E-TYPE-ARG", "the fallback of `value_or` does not have the option's content type", k[start]->line);
                    return pv;
                }
                if (w.k == TK_WRAPPED && w.pk == TK_BOOL) return tk(TK_BOOL);
                if (w.k == TK_WRAPPED && w.pk == TK_SLICE) { ty_t sl = tk(TK_SLICE); sl.ebits = w.ebits; sl.esign = w.esign; sl.eflt = w.eflt; sl.enom = w.enom; return sl; }
                if (w.k == TK_WRAPPED && w.nname.size) { ty_t pv = tk(TK_NAMED); pv.nname = w.nname; pv.targ = w.targ; return pv; }
                if (!d.lit && d.k != TK_UNKNOWN) { d.is_mut = false; return d; }
                return tk(TK_UNKNOWN);
            }
            if (!sig_find(c, h2) && n == 2 && veq(h2, "deref")) {
                ty_t p = tc_infer(c, k[start + 1], env, nenv);
                if (p.rk) { p.rk = 0; return p; }
                return tk(TK_UNKNOWN);
            }
            if (!sig_find(c, h2) && n == 3 && veq(h2, "view_array") && k[start + 1]->kind != LOW_CST_ATOM) {   // ★ RFC-0155 §5.3 — 줄 보기
                ty_t sx = tc_infer(c, k[start + 2], env, nenv);
                proven_u8str_view_t lf = { 0 }; proven_u8 dp = tc_row_depth(k[start + 1], &lf);
                if (dp) {
                    ty_t e = ty_of_word(lf);
                    if (e.k == TK_INT || e.k == TK_FLOAT) { ty_t sl = tc_slice_of_elem(e); sl.erow = dp; sl.is_mut = sx.is_mut; return sl; }
                }
                return tk(TK_UNKNOWN);
            }
            if (!sig_find(c, h2) && n == 3 && veq(h2, "view_array") && k[start + 1]->kind == LOW_CST_ATOM) {
                (void)tc_infer(c, k[start + 2], env, nenv);
                ty_t e = ty_of_word(k[start + 1]->tok.lex);
                if (e.k == TK_INT || e.k == TK_FLOAT) return tc_slice_of_elem(e);
                { ty_t sl = tk(TK_SLICE); sl.nname = k[start + 1]->tok.lex; return sl; }
            }
            if (!sig_find(c, h2) && n >= 3 && (veq(h2, "load") || veq(h2, "load_masked"))) {
                ty_t sq = tc_infer(c, k[start + 1], env, nenv);
                for (proven_size_t q = 2; q < n; q++) (void)tc_infer(c, k[start + q], env, nenv);
                // 레인 수와 원소는 받는 자리(`vec T N`)가 정한다 — 바이트 슬라이스에서 `vec u32 4` 를 읽는 것이 보통이다(SIMD 픽스처)
                if (sq.k == TK_SLICE || sq.k == TK_NAMED) return (ty_t){ .k = TK_VEC, .bits = 0, .lanes = 0 };   // 글 리터럴도 바이트다
                return tk(TK_UNKNOWN);
            }
            if (!sig_find(c, h2) && n == 2 && veq(h2, "splat")) {
                ty_t v = tc_infer(c, k[start + 1], env, nenv);
                if ((v.k == TK_INT || v.k == TK_FLOAT) && v.bits && !v.lit) return (ty_t){ .k = TK_VEC, .bits = v.bits, .sign = v.sign, .lanes = 0, .vflt = v.k == TK_FLOAT };
                if (v.lit) return (ty_t){ .k = TK_VEC, .lanes = 0, .vflt = v.k == TK_FLOAT };   // 원소 폭은 받는 자리가 정한다
                return tk(TK_UNKNOWN);
            }
            if (!sig_find(c, h2) && n == 3 && (veq(h2, "lane_avg") || veq(h2, "lane_rotate"))) {
                ty_t v = tc_infer(c, k[start + 1], env, nenv); (void)tc_infer(c, k[start + 2], env, nenv);
                if (v.k == TK_VEC) return v;
                return tk(TK_UNKNOWN);
            }
            if (!sig_find(c, h2) && n == 2 && (veq(h2, "lane_reverse") || veq(h2, "lane_rotate"))) {
                ty_t v = tc_infer(c, k[start + 1], env, nenv);
                if (v.k == TK_VEC) return v;
                return tk(TK_UNKNOWN);
            }
            if (!sig_find(c, h2) && n == 2 && veq(h2, "bit_not")) {
                ty_t a = tc_infer(c, k[start + 1], env, nenv);
                if (a.k == TK_INT && !a.lit && a.bits) { a.has_rng = false; return a; }
                return tk_int(0, false);
            }
            // ★ RFC-0054 §8-8 (2026-10-25 소유자 «다 추천대로») — 그 기계가 잠금 없이 못 하는 폭의 원자 연산은 번역 때 거절한다.
            //   전엔 Cortex-M 에서 링크가 이름 없는 `__atomic_*` 기호로 멈췄다(베어메탈에는 원자 라이브러리가 없다). 같은 사실을 까닭과 함께 먼저 말한다.
            if (!sig_find(c, h2) && n >= 3 && (veq(h2, "atomic_load") || veq(h2, "atomic_store") || veq(h2, "atomic_cas") || veq(h2, "atomic_swap") ||
                                               veq(h2, "atomic_add") || veq(h2, "atomic_sub") || veq(h2, "atomic_and") || veq(h2, "atomic_or") || veq(h2, "atomic_xor"))) {
                ty_t sq0 = tc_infer(c, k[start + 1], env, nenv);
                const bool rmw = !(veq(h2, "atomic_load") || veq(h2, "atomic_store"));
                const unsigned lim = rmw ? low_ir_target()->atom_rmw : low_ir_target()->atom_ldst;
                if (sq0.k == TK_SLICE && sq0.ebits && sq0.ebits / 8u > lim) {
                    tc_emitf(c, "E-ATOMIC-TARGET", k[start]->tok.line,   // ★ detail 에 베낀다(msg 가 지역 칸을 가리키면 뒤에 깨진다)
                             "this machine (`%s`) does a %s atomic only up to %u bytes without a lock, and this element is %u bytes — "
                             "there is no atomic library to call on it, so the build would stop at the link. Use a narrower element, or a lock",
                             low_ir_target()->name, rmw ? "read-modify-write" : "load or store", lim, (unsigned)(sq0.ebits / 8u));
                }
                if (veq(h2, "atomic_store")) { for (proven_size_t q = 2; q < n; q++) (void)tc_infer(c, k[start + q], env, nenv); return tk(TK_UNKNOWN); }
            }
            // ★ `atomic_load s i …` — 읽은 값의 타입은 그 슬라이스의 원소다(전에는 «모름»).
            // ★ RFC-0148 S4 (2026-10-14 소유자 «둘 다 피연산자 타입») — 값을 내는 원자 연산(`atomic_cas` · `atomic_swap` · `atomic_add` …)의 결과도
            //   그 칸의 원소 타입이다(정본 §7.5 (1a)). `atomic_cas` 는 바꿨으면 1, 아니면 0 을 그 타입으로 낸다.
            if (!sig_find(c, h2) && n >= 3 && (veq(h2, "atomic_cas") || veq(h2, "atomic_swap") || veq(h2, "atomic_add") || veq(h2, "atomic_sub") ||
                                               veq(h2, "atomic_and") || veq(h2, "atomic_or") || veq(h2, "atomic_xor"))) {
                ty_t sq = tc_infer(c, k[start + 1], env, nenv);
                ty_t it = tc_infer(c, k[start + 2], env, nenv);
                for (proven_size_t q = 3; q < n; q++) (void)tc_infer(c, k[start + q], env, nenv);
                if (it.k == TK_INT && !it.lit && it.bits && it.nominal != 1)
                    tc_emit(c, "E-TYPE-NOMINAL", "the index of an atomic operation is a `usize` — declare it `usize`, or write `cast usize <value> .`", k[start]->tok.line);
                if (sq.k == TK_SLICE && sq.ebits && !sq.eflt && !sq.erow) { ty_t ev = tc_elem_int(sq.ebits, sq.esign); ev.nominal = sq.enom; return ev; }
                return tk(TK_UNKNOWN);
            }
            if (!sig_find(c, h2) && veq(h2, "atomic_load") && n >= 3) {
                ty_t sq = tc_infer(c, k[start + 1], env, nenv);
                ty_t it = tc_infer(c, k[start + 2], env, nenv);
                if (it.k == TK_INT && !it.lit && it.bits && it.nominal != 1) {
                    tc_emit(c, "E-TYPE-NOMINAL", "the index of an atomic operation is a `usize` — declare it `usize`, or write `cast usize <value> .`", k[start]->tok.line);
                }
                if (sq.k == TK_SLICE && sq.ebits && !sq.eflt && !sq.erow) { ty_t ev = tc_elem_int(sq.ebits, sq.esign); ev.nominal = sq.enom; return ev; }
                return tk(TK_UNKNOWN);
            }
            if (veq(h2, "idx") && n >= 2) {             // ★ 원소 타입은 슬라이스가 정한다
                ty_t sq = tc_infer(c, k[start + 1], env, nenv);
                for (proven_size_t q = 2; q < n; q++) {
                    ty_t it = tc_infer(c, k[start + q], env, nenv);
                    (void)tc_enum_as_num(c, it, "E-TYPE-KIND", k[start + q]->line ? k[start + q]->line : k[start]->tok.line);
                    // ★ RFC-0149 — 첨자는 `usize` 다. 옮기는 동안은 다른 정수도 받고 센다.
                    if (it.k == TK_INT && !it.lit && it.bits && it.nominal != 1) {
                        tc_emit(c, "E-TYPE-NOMINAL", "an index or a bound is a `usize` — this one has a fixed-width integer type. Declare it `usize`, "
                                "or convert where it is used: `cast usize <value> .` (the conversion stops when the value does not fit the "
                                "target's `usize`; canon 6.2.6 (3a))", k[start + q]->line ? k[start + q]->line : k[start]->tok.line);
                    }
                }
                if (sq.k == TK_SLICE && sq.erow) {           // §13.10 — 줄의 배열의 원소는 그 줄을 보는 슬라이스
                    ty_t rv = sq; rv.erow--;
                    return rv;
                }
                if (sq.k == TK_SLICE && sq.ebits) {
                    if (sq.eflt) return (ty_t){ .k = TK_FLOAT, .bits = sq.ebits };
                    ty_t ev = tc_elem_int(sq.ebits, sq.esign); ev.nominal = sq.enom;
                    return ev;
                }
                // ★ RFC-0148 S4 (2026-10-14) — 원소를 모르는 슬라이스(`slice bool` · `slice <묶음>` · 틀의 `slice t`)의 원소를 `u8` 이라
                //   답하고 있었다. 조건 자리를 검사하자 `eq idx t. i. . true .`(`slice bool`)가 «정수와 bool» 로 거절됐다 — 지어낸 답이다.
                //   모르면 모른다고 답한다. 글 리터럴(`idx "abc" 0 .`)만 바이트다.
                if (sq.k == TK_SLICE && sq.nname.size && n == 3) { ty_t ev = tk(TK_NAMED); ev.nname = sq.nname; return ev; }   // 구조체 원소
                if (sq.k == TK_SLICE || sq.k == TK_UNKNOWN || sq.k == TK_WRAPPED) return tk(TK_UNKNOWN);
                return tk_int(8, false);
            }
            // ★ `view_array <스칼라> <byteslice>` — 바이트 슬라이스를 **타입 배열**로 재해석한다.
            //   원소 타입을 슬라이스 ty_t 에 실어(ebits/eflt/esign) index·subslice·for-in·map 등
            //   **모든 소비 자리**가 float/부호를 되살린다. 구조체·미상 원소는 종전대로 무타입으로
            //   흘려보낸다 — SIMD 의 `var xs be slice <T> view_array …` load 핸들은 이 타입을 받는다.
            if (veq(h2, "view_array") && n >= 3 && k[start + 1]->kind != LOW_CST_ATOM) {   // ★ RFC-0155 §5.3 — 줄 보기: 원소가 줄이다
                ty_t sx = tc_infer(c, k[start + 2], env, nenv);
                proven_u8str_view_t lf = { 0 }; proven_u8 dp = tc_row_depth(k[start + 1], &lf);
                if (dp) {
                    ty_t e = ty_of_word(lf);
                    if (e.k == TK_INT || e.k == TK_FLOAT) { ty_t sl = tc_slice_of_elem(e); sl.erow = dp; sl.is_mut = sx.is_mut; return sl; }
                }
            }
            if (veq(h2, "view_array") && n >= 3 && k[start + 1]->kind == LOW_CST_ATOM) {
                ty_t e = ty_of_word(k[start + 1]->tok.lex);
                for (proven_size_t q = 2; q < n; q++) (void)tc_infer(c, k[start + q], env, nenv);
                if (e.k == TK_INT || e.k == TK_FLOAT) return tc_slice_of_elem(e);
            }
            if (veq(h2, "subslice")) {
                // ★ subslice 는 원소 타입을 **물려준다** — 슬라이스 X 의 원소가 곧 결과의 원소다.
                ty_t sx = (n >= 2) ? tc_infer(c, k[start + 1], env, nenv) : tk(TK_UNKNOWN);
                for (proven_size_t q = 2; q < n; q++) {
                    ty_t it = tc_infer(c, k[start + q], env, nenv);
                    if (it.k == TK_INT && !it.lit && it.bits && it.nominal != 1) {   // RFC-0149 — 경계도 `usize`
                        tc_emit(c, "E-TYPE-NOMINAL", "an index or a bound is a `usize` — this one has a fixed-width integer type. Declare it `usize`, "
                                "or convert where it is used: `cast usize <value> .` (the conversion stops when the value does not fit the "
                                "target's `usize`; canon 6.2.6 (3a))", k[start + q]->line ? k[start + q]->line : k[start]->tok.line);
                    }
                }
                if (sx.k == TK_SLICE && sx.ebits) return sx;         // 타입 있는 슬라이스는 그대로
                return tk(TK_SLICE);
            }
            // ★★★★★ **잎 낱말의 결과 타입** (2026-09-10, REQ-0014 · WO-0195).
            //   이 언어로 프로그램을 처음 짜 보던 사람이 이렇게 적었다:
            //       let e be option u64 write_out k 1 ln .     rem write_out 은 option 을 안 낸다
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
            // ★★ RFC-0149 L4 뒤(2026-10-14) — **`alloc_bytes <뿌리> capacity <n>` 의 `n` 은 `usize` 다.** 위의 `capacity` 갈래는 그 낱말이 폼의 머리일
            //   때만 서는데, 이 꼴에서는 `capacity` 가 `alloc_bytes` 의 인자 사이에 선 표지다 — 그래서 흔한 철자에서 규칙이 한 번도 서지 않았다
            //   (릴리스 노트의 표를 처리기에 넣어 보다 찾았다). 표지 바로 뒤의 인자를 본다.
            if (veq(h2, "alloc_bytes"))
                for (proven_size_t q = 1; q + 1 < n; q++)
                    if (k[start + q]->kind == LOW_CST_ATOM && veq(k[start + q]->tok.lex, "capacity")) {
                        ty_t at = tc_infer(c, k[start + q + 1], env, nenv);
                        if (at.k == TK_INT && !at.lit && at.bits && at.nominal != 1)
                            tc_emit(c, "E-TYPE-NOMINAL", "a capacity is a `usize` — this one has a fixed-width integer type. Declare it `usize`, or write "
                                    "`cast usize <value> .`", k[start]->tok.line);
                        break;
                    }
            {
                static const struct { const char *w; unsigned char kind; } LEAF[] = {
                    // ── option 을 낸다 (실패할 수 있다는 사실을 타입이 든다)
                    { "alloc_bytes", 1 }, { "@read_in", 4 }, { "@env_get", 1 },
                    { "@file_open", 1 }, { "@file_read", 4 }, { "@file_write", 4 },
                    { "@file_seek", 1 }, { "@file_type", 1 }, { "@link_type", 1 },
                    { "@dir_open", 1 }, { "@dir_read", 4 },
                    { "@net_pair", 1 }, { "@net_send", 4 }, { "@net_recv", 4 },
                    { "@net_listen", 1 }, { "@net_port", 1 }, { "@net_connect", 1 },
                    { "@net_accept", 1 },
                    { "@proc_spawn", 1 }, { "@proc_read", 4 }, { "@proc_poll", 1 }, { "@proc_wait", 1 },   // ★ RFC-0136
                    // ★ RFC-0148 S4 (2026-10-14) — 라이브러리 감싸개(`args.get` · `reactor.*` · `tty.*`)가 이미 적은 타입 그대로다.
                    //   8 = `option slice u8` · 9 = `option u64`. 읽고 쓴 바이트 수는 4(`option usize`)다(정본 6.2.6 (3‴)).
                    { "@arg", 8 }, { "@reactor_new", 9 }, { "@r_read", 4 }, { "@r_write", 4 },
                    { "@tty_read", 4 }, { "@tty_size", 9 }, { "@tty_raw", 2 },
                    { "@time_sleep", 6 }, { "@net_resolve", 9 }, { "@sum_seq", 10 }, { "@sum_neumaier", 10 },
                    // ── 참거짓을 낸다 (닫기·파일시스템 변경은 됐나 안 됐나뿐이다)
                    { "@file_close", 2 }, { "@dir_close", 2 }, { "@net_close", 2 }, { "same_slice", 2 },
                    { "@proc_kill", 2 },
                    { "@dir_make", 2 }, { "@path_remove", 2 }, { "@path_rename", 2 },
                    // ── 정수를 낸다 (쓴 길이 · 해시 · 시계 · 난수 · 비트)
                    { "@write_out", 5 }, { "crc32", 7 }, { "hash_bytes", 6 },
                    { "sha256", 5 }, { "sha512", 5 },
                    { "@time_now", 3 }, { "@time_local", 3 }, { "rng_next", 6 },
                    { "@random_bytes", 3 }, { "argc", 3 },
                    { "bit_and", 3 }, { "bit_or", 3 }, { "bit_xor", 3 }, { "bit_not", 3 },
                    { "shl", 3 }, { "shr", 3 }, { "wshl", 3 }, { "wshr", 3 },
                    { "rotl", 3 }, { "rotr", 3 },
                    { "clmul_lo", 6 }, { "clmul_hi", 6 }, { "mul_hi", 6 },   // ★ 캐리 없는 곱셈 (RFC-0119) — 폭은 피연산자가 말한다
                    // ★★ 암호 잎 — **낸 값은 처리한 바이트 수**다(option 도 참거짓도 아니다).
                    //   근거는 구현 자신이다: VM 의 이 자리들이 전부 `vmv_int` 를 쌓고, C 뒤끝은
                    //   `lw_int(...)` 로 싣는다. 표에 없어서 `let n be u64 aes_ctr …` 같은 선언이
                    //   **비틀어도 초록**이었다(돌연변이 측정 `check-decl-types`). RFC-0122 로 낱말이
                    //   둘 늘면서 그 자리가 하나 더 늘길래, 늘리는 대신 **줄였다**.
                    { "aes_ctr", 5 }, { "ghash", 5 },
                    { "aes_round", 5 }, { "aes_round_last", 5 },
                    { "chacha20", 5 }, { "poly1305", 5 }, { "aes_gcm", 5 }, { "chacha_poly", 5 },
                    { "sha384", 5 },
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
                // ★ 맨 머리가 빌트인 연산과 같은 철자이면 그것은 **저자의 op** 이다(이름공간이 따로다) — 이 표가 답하지 않는다.
                bool own = lskip == 1 && low_ir_is_builtin_op(h2);
                // `call_builtin` 없이 맨 이름으로 부른 잎(`sha256 s. o.`)은 하강층이 `E-BUILTIN-BARE` 로 거절한다 — S5 는 물러선다
                if (own && !sig_find(c, h2)) {
                    for (proven_size_t q = 1; q < n; q++) (void)tc_infer(c, k[start + q], env, nenv);
                    ty_t u = tk(TK_UNKNOWN); u.bydecl = true; return u;
                }
                for (proven_size_t li = 0; li < sizeof LEAF / sizeof LEAF[0] && !own; li++) {
                    if (!veq(hleaf, LEAF[li].w)) continue;
                    // ★ RFC-0151 (2026-10-22) — 곱의 말(`mul_hi` · `clmul_lo` · `clmul_hi`)은 `u64` 둘을 받는다. 전에는 받는 타입을 안 봐서
                    //   `i64` 를 주면 검사가 `u64` 를, 하강이 `i64` 를 답해 «compiler defect»(E-IR-TYPE-SPLIT)가 났다.
                    const bool wordmul = veq(hleaf, "mul_hi") || veq(hleaf, "clmul_lo") || veq(hleaf, "clmul_hi");
                    for (proven_size_t q = lskip; q < n; q++) {
                        ty_t at = tc_infer(c, k[start + q], env, nenv);
                        if (wordmul && at.k == TK_INT && !at.lit && at.bits && (at.bits != 64 || at.sign || at.nominal))
                            tc_emit(c, "E-TYPE-ARG", "this word multiplies two `u64` — the product's halves are defined on unsigned 64-bit words. "
                                    "Give it `u64` (`widen u64 …` for a narrower unsigned value, `cast u64 …` otherwise)", k[start]->tok.line);
                    }
                    if (LEAF[li].kind == 1) { ty_t w = tk(TK_WRAPPED); w.wrap = 1; return w; }
                    // ★ RFC-0149 L4 — 읽고 쓴 **바이트 수**는 `usize` 다(4 = `option usize` · 5 = `usize`). 길이를 세는 수가 `u64` 로 나오면
                    //   받는 쪽이 매번 `cast` 를 적어야 하고, 좁은 대상에서는 버퍼의 길이와 다른 폭이 된다.
                    if (LEAF[li].kind == 4) { ty_t w = tk(TK_WRAPPED); w.wrap = 1; w.pk = (proven_u8)TK_INT; w.pbits = low_ir_target()->addr_bits; w.psign = false; w.pnom = 1; return w; }
                    if (LEAF[li].kind == 10) return (ty_t){ .k = TK_FLOAT, .bits = 64 };   // `math.sum_seq` · `sum_neumaier` 의 감싸개가 `f64` 를 낸다
                    if (LEAF[li].kind == 8) { ty_t w = tk(TK_WRAPPED); w.wrap = 1; w.pk = (proven_u8)TK_SLICE; w.ebits = 8; return w; }
                    if (LEAF[li].kind == 9) { ty_t w = tk(TK_WRAPPED); w.wrap = 1; w.pk = (proven_u8)TK_INT; w.pbits = 64; return w; }
                    if (LEAF[li].kind == 5) { ty_t ut = tk_int(low_ir_target()->addr_bits, false); ut.nominal = 1; return ut; }
                    if (LEAF[li].kind == 6) return tk_int(64, false);   // RFC-0148 D5 — 해시 값 · 난수 · 캐리 없는 곱의 말: `u64`
                    if (LEAF[li].kind == 7) return tk_int(32, false);   // RFC-0148 D5 — `crc32` 의 값은 32 비트다
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
                veq(h2, "is_none") || veq(h2, "bitset_contains")) {
                for (proven_size_t q = 1; q < n; q++) (void)tc_infer(c, k[start + q], env, nenv);
                return tk(TK_BOOL);
            }
            // ★ `any <mask>` / `all <mask>` — 마스크를 요구한다(정수도 벡터도 아니다).
            if ((veq(h2, "lane_any") || veq(h2, "lane_all")) && n >= 2) {
                ty_t m = tc_infer_run(c, k, start + 1, n - 1, env, nenv);
                if (m.k != TK_UNKNOWN && m.k != TK_NAMED && m.k != TK_MASK)
                    tc_emit(c, "E-TYPE-MASK",
                            "`lane_any`/`lane_all` need a mask (the result of a lanewise comparison) — "
                            "this value is not a mask", k[start]->line);
                return tk(TK_BOOL);
            }
            // ★ X-0081 — `reduce_add|mul|min|max <벡터>` 는 원소 하나를 낸다. 인자를 **추론해야** 그 안의 벡터 식
            //   (`reduce_add (add v w)`)의 레인 대조가 돈다 — 모르는 머리로 넘기면 안쪽을 아무도 보지 않았다.
            if ((veq(h2, "reduce_add") || veq(h2, "reduce_mul") || veq(h2, "reduce_min") || veq(h2, "reduce_max")) && n >= 2) {
                ty_t v = tc_infer_run(c, k, start + 1, n - 1, env, nenv);
                if (v.k == TK_VEC) {
                    ty_t e = tk(v.vflt ? TK_FLOAT : TK_INT);
                    e.bits = v.bits; e.sign = v.sign;
                    return e;
                }
                return tk(TK_UNKNOWN);
            }
            // ★ `select <mask> <a> <b>` — 첫 인자는 마스크여야 하고, 레인 수가 맞아야 한다.
            if (veq(h2, "lane_select") && n >= 4) {
                ty_t m = tc_infer(c, k[start + 1], env, nenv);
                ty_t va = tc_infer(c, k[start + 2], env, nenv);
                ty_t vb = tc_infer(c, k[start + 3], env, nenv);
                if (m.k != TK_UNKNOWN && m.k != TK_NAMED && m.k != TK_MASK)
                    tc_emit(c, "E-TYPE-MASK",
                            "`lane_select` needs a mask as its first argument (the result of a lanewise "
                            "comparison)", k[start]->line);
                else if (m.k == TK_MASK && va.k == TK_VEC && m.lanes != va.lanes)
                    tc_emit(c, "E-TYPE-LANES",
                            "the mask and the vectors have different lane counts", k[start]->line);
                if (va.k == TK_VEC && vb.k == TK_VEC && va.lanes != vb.lanes)
                    tc_emit(c, "E-TYPE-LANES",
                            "`lane_select` needs the two vectors to have the same lane count",
                            k[start]->line);
                return va;
            }
        }
        // ★ `ok <expr>` / `error <name>` — result 생성자. 안쪽을 **봐야** 한다.
        //   이것을 안 해서 `return ok try index d 0 .` 의 try 가 검사되지 않았다
        //   (try 분기는 try 가 **머리**일 때만 걸린다). 변이 지표가 그것을 잡아냈다.
        if (veq(k[start]->tok.lex, "ok") && n >= 2) {
            ty_t pv = tc_infer_run(c, k, start + 1, n - 1, env, nenv);   // payload 를 검사한다
            ty_t w = tk(TK_WRAPPED); w.wrap = 2;                         // ★ `ok` 는 result 다
            tc_wrap_payload(&w, pv);
            if (pv.k == TK_NAMED && pv.nname.size) w.pname = pv.nname;
            return w;
        }
        if (veq(k[start]->tok.lex, "some") && n >= 2 && !sig_find(c, k[start]->tok.lex)) {   // RFC-0149 L4 — `some` 은 알맹이를 지닌 option 이다
            ty_t pv = tc_infer_run(c, k, start + 1, n - 1, env, nenv);
            ty_t w = tk(TK_WRAPPED); w.wrap = 1;
            tc_wrap_payload(&w, pv);
            if (pv.k == TK_NAMED) { w.nname = pv.nname; w.targ = pv.targ; w.pname = pv.nname; }
            return w;
        }
        if (veq(k[start]->tok.lex, "error") && n >= 2) { ty_t w = tk(TK_WRAPPED); w.wrap = 2; return w; }
        // ★ `try <expr>` — 풀 수 있는 것에만 붙는다.
        //   `try` 는 "이것은 실패할 수 있고, 실패하면 전파한다" 는 **선언**이다.
        //   result/option 이 아닌 값에 붙으면 아무 일도 하지 않는다 — 선언이 거짓말이 된다.
        //   (SPEC-MVP §8 의 대표 예제가 실제로 그랬다: read_u16_be 는 u16 을 돌려주는데
        //    본문이 `try read_u16_be …` 라고 적어 놨다. 아무도 확인하지 않았다.)
        if (veq(k[start]->tok.lex, "try") && n >= 2) {
            // ★ RFC-0148 S4 (2026-10-14) — 잴 것은 `else_none` · `else_error` **앞까지**다. 전에는 그 낱말까지 한 줄로 재서
            //   [폼, else_none] 이 «모르는 꼴» 이 되고 앞의 폼(`mean s.`)은 아예 재지 않았다.
            proven_size_t te = start + n;
            for (proven_size_t q = start + 1; q < start + n; q++)
                if (k[q]->kind == LOW_CST_ATOM && (veq(k[q]->tok.lex, "else_none") || veq(k[q]->tok.lex, "else_error") || veq(k[q]->tok.lex, "map_error"))) { te = q; break; }
            const low_cst_t *sf = te - start - 1 == 1 ? k[start + 1] : NULL;   // [try, (spawn send …)] 꼴
            while (sf && sf->kind == LOW_CST_GROUP && sf->nkids == 1) sf = sf->kids[0];
            if (sf && sf->kind == LOW_CST_FORM && sf->nkids >= 3 && sf->kids[0]->kind == LOW_CST_ATOM && veq(sf->kids[0]->tok.lex, "spawn") &&
                sf->kids[1]->kind == LOW_CST_ATOM && (sf->kids[1]->tok.kw == LOW_KW_SEND || veq(sf->kids[1]->tok.lex, "send"))) {
                for (proven_size_t q = 2; q < sf->nkids; q++) if (sf->kids[q]->kind != LOW_CST_ATOM) (void)tc_infer(c, sf->kids[q], env, nenv);
                ty_t w = tk(TK_WRAPPED); w.wrap = 2; w.pk = TK_INT; w.pbits = 64; return w;
            }
            if (te - start - 1 >= 3 && k[start + 1]->kind == LOW_CST_ATOM && veq(k[start + 1]->tok.lex, "spawn") &&
                k[start + 2]->kind == LOW_CST_ATOM && (k[start + 2]->tok.kw == LOW_KW_SEND || veq(k[start + 2]->tok.lex, "send"))) {
                for (proven_size_t q = start + 3; q < te; q++) (void)tc_infer(c, k[q], env, nenv);
                ty_t w = tk(TK_WRAPPED); w.wrap = 2; w.pk = TK_INT; w.pbits = 64; return w;   // 넣지 못하면 `mailbox_full` 을 값으로
            }
            ty_t inner = tc_infer_run(c, k, start + 1, te - start - 1, env, nenv);
            for (proven_size_t q = te + 1; q < start + n; q++) (void)tc_infer(c, k[q], env, nenv);   // else_error 의 값
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
            if (te < start + n && veq(k[te]->tok.lex, "map_error")) return tc_payload_of(inner);   // 오류만 바꿔 전파한다 — 값은 알맹이다
            if (te < start + n) {   // 채널이 **바뀐다** — option / result 다. 알맹이는 안의 것 그대로다
                ty_t w = tk(TK_WRAPPED); w.wrap = veq(k[te]->tok.lex, "else_none") ? 1 : 2;
                if (inner.k == TK_WRAPPED) {
                    w.pk = inner.pk; w.pbits = inner.pbits; w.psign = inner.psign; w.pnom = inner.pnom;
                    w.ebits = inner.ebits; w.esign = inner.esign; w.eflt = inner.eflt; w.enom = inner.enom;
                    w.nname = inner.nname; w.targ = inner.targ;
                }
                return w;
            }
            return tc_payload_of(inner);     // RFC-0148 S5 — 알맹이를 꺼낸다(모르면 «모름»)
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
            /* ★★★ 2026-10-10 (소유자 «bool을 숫자 u8 0과 1로 바꿀 수 있어야 합니다 … 수를 bool로 바꾸는 것은 eq n 0 또는
             *   ne n 0 으로») — 위 «결정 A»(2026-09-03)의 한쪽을 되돌린다: **참거짓에서 정수로**는 `cast` 로 간다.
             *   `true` 는 1, `false` 는 0 이고 어느 정수 타입에도 들어가므로 트랩하지 않는다. 반대쪽(정수 → 참거짓)은 여전히
             *   없다 — 무엇을 묻는지(`ne n. 0 .`)를 적는다. 부동소수점 타입은 대상이 아니다(0/1 을 실수로 받을 까닭이 없다). */
            if (tgt.k == TK_BOOL)
                tc_emit(c, "E-TYPE-KIND", "bool is not numeric: it cannot be a cast target — write the question: `ne n. 0 .` (or `eq n. 0 .`)",
                        k[start + 1]->line);
            else if (have_src && src0.k == TK_BOOL && tgt.k != TK_INT)
                tc_emit(c, "E-TYPE-KIND",
                        "a bool casts to an INTEGER type only (`cast u8 b. .` gives 1 for true and 0 for false) — "
                        "not to a floating-point type",
                        k[start + 1]->line);
            // ★ 열거 → 정수(소유자 2026-10-15 «열거 값을 수 자리에 쓰려면 cast»; 정본 §6.2.16 (5)) — 갈래의 번호(정본 §6.2.7 (2c))를 낸다.
            //   값을 지닌 갈래가 있는 열거는 번호 하나로 값이 다 말해지지 않으므로 받지 않는다.
            else if (have_src && src0.k == TK_NAMED && src0.nname.size && tc_is_enum_name(tc_canon_name(src0.nname))) {
                if (tgt.k != TK_INT)
                    tc_emit(c, "E-TYPE-KIND", "an enum value casts to an INTEGER type only — the variant's number (canon 6.2.7 (2c))",
                            k[start + 1]->line);
                else if (tc_enum_has_payload(tc_canon_name(src0.nname)))
                    tc_emit(c, "E-TYPE-KIND",
                            "this enum has a variant that carries values, so a number alone does not say what the value is — "
                            "`cast` takes the number only of an enum whose variants carry nothing. Ask with `isa` / `match` (canon 6.2.16 (5b))",
                            k[start + 1]->line);
                else tc_cast_enum_note(k[start], tc_canon_name(src0.nname));
            }
            // ★ 정수 → 열거(소유자 2026-10-15 «수->열거도 필요 … 파일에 enum 을 적거나 다시 읽어올 때») — 그 번호를 가진 갈래를 낸다.
            //   어느 갈래의 번호도 아니면 멈춘다(§6.2.16 (5a)). 원천은 정수여야 하고, 값을 지닌 갈래가 있는 열거는 대상이 될 수 없다.
            if (k[start + 1]->kind == LOW_CST_ATOM && tc_is_enum_name(tc_canon_name(k[start + 1]->tok.lex))) {
                proven_u8str_view_t en = tc_canon_name(k[start + 1]->tok.lex);
                if (tc_enum_has_payload(en)) {
                    tc_emit(c, "E-TYPE-KIND", "a number does not cast to an enum whose variants carry values — the number does not say "
                            "what the value holds. Build the variant with `<enum>.<variant> …` (canon 6.2.16 (5b))", k[start + 1]->line);
                    ty_t u = tk(TK_UNKNOWN); u.bydecl = true; return u;
                }
                if (have_src && src0.k != TK_INT && src0.k != TK_UNKNOWN) {
                    tc_emit(c, "E-TYPE-KIND", "only an integer casts to an enum — it gives the variant with that number (canon 6.2.16 (5a))",
                            k[start + 1]->line);
                    ty_t u = tk(TK_UNKNOWN); u.bydecl = true; return u;
                }
                ty_t et = tk(TK_NAMED); et.nname = k[start + 1]->tok.lex;
                return et;
            }
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
        // ★★★ RFC-0149 §6.7 — `narrow` 도 방향을 말한다: `usize` · `idiff` 가 끼면 `cast` 다. 멈추지 않는 셋(`narrow_try` ·
        //   `narrow_sat` · `narrow_wrap`)은 «안 들어갈 때 무엇을 할지» 를 이름이 고르므로 그대로 선다(소유자 «다 추천대로»).
        //   원천은 여기서 **소리 내어** 잰다(RFC-0148 S4 · 2026-10-14) — 전에는 묻기만 했고(quiet) 아래의 길도 원천을 재지 않아,
        //   `narrow u8 add r. i. . .` 의 `u64 + usize` 가 섰다.
        if (veq(k[start]->tok.lex, "narrow") && n >= 3 && k[start + 1]->kind == LOW_CST_ATOM && !sig_find_at(c, k[start])) {
            ty_t ntgt = ty_of_word(k[start + 1]->tok.lex);
            ty_t nsrc = tc_infer(c, k[start + 2], env, nenv);
            (void)tc_enum_as_num(c, nsrc, "E-TYPE-KIND", k[start]->line);
            if ((nsrc.k == TK_INT && nsrc.nominal == 1) || (ntgt.k == TK_INT && ntgt.nominal == 1))
                tc_emit(c, "E-TYPE-NOMINAL",
                        "`usize` / `idiff` convert with `cast` only — their width is the implementation's (16 bits up to the widest "
                        "integer), so whether this NARROWS at all depends on the target. Write `cast <type> <value>` (it stops when "
                        "the value does not fit), or pick what happens instead: `narrow_try` / `narrow_sat` / `narrow_wrap`",
                        k[start]->line);
            // ★★★ RFC-0148 S4 — **좁힌 값의 타입은 적힌 대상 타입이다.** 전에는 여기서 답 없이 흘러 «모름» 이 됐다: `narrow u8 x.` 를
            //   어느 선언에나 넣을 수 있었다(하강층은 `u8` 로 알고 있었다 — 두 층을 맞대자 321 자리에서 보였다).
            if ((ntgt.k == TK_INT || ntgt.k == TK_FLOAT) && ntgt.bits) return ntgt;   // `narrow f32` 도 대상 타입이다
        }
        // RFC-0148 S4 — 멈추지 않는 좁히기 셋도 같다: `narrow_sat` · `narrow_wrap` 은 `T`, `narrow_try` 는 `option T` (시그니처 표 · §6.3.6).
        if (n >= 3 && k[start + 1]->kind == LOW_CST_ATOM && !sig_find_at(c, k[start]) &&
            (veq(k[start]->tok.lex, "narrow_sat") || veq(k[start]->tok.lex, "narrow_wrap") || veq(k[start]->tok.lex, "narrow_try"))) {
            ty_t ntgt = ty_of_word(k[start + 1]->tok.lex);
            for (proven_size_t i = 2; i < n; i++) (void)tc_enum_as_num(c, tc_infer(c, k[start + i], env, nenv), "E-TYPE-KIND", k[start]->line);
            if (ntgt.k == TK_INT && ntgt.bits) {
                if (!veq(k[start]->tok.lex, "narrow_try")) return ntgt;
                ty_t w = tk(TK_WRAPPED); w.wrap = 1; tc_wrap_payload(&w, ntgt);
                return w;
            }
        }
        if (veq(k[start]->tok.lex, "widen") && n >= 3 && k[start + 1]->kind == LOW_CST_ATOM
            && !sig_find_at(c, k[start])) {
            ty_t tgt = ty_of_word(k[start + 1]->tok.lex);
            ty_t src = tc_infer(c, k[start + 2], env, nenv);
            for (proven_size_t i = 3; i < n; i++) (void)tc_infer(c, k[start + i], env, nenv);
            if (tc_enum_as_num(c, src, "E-WIDEN-KIND", k[start]->line)) return tgt;
            // ★★★ RFC-0149 §6.7 (2026-10-13 소유자 «usize 와 관련된 형변환은 꼭 cast를 쓰게 합시다») — `usize` · `idiff` 는 넓히기의
            //   표 밖이다. 그 폭은 구현이 정하므로(16 비트부터 가장 넓은 정수까지) «넓히는가 좁히는가» 가 대상마다 다르다 —
            //   방향을 말하는 낱말로는 적을 수 없다. `cast` 는 방향을 묻지 않고 안 들어가면 멈춘다.
            if ((src.k == TK_INT && src.nominal == 1) || (tgt.k == TK_INT && tgt.nominal == 1)) {
                tc_emit(c, "E-TYPE-NOMINAL",
                        "`usize` / `idiff` convert with `cast` only — their width is the implementation's (16 bits up to the widest "
                        "integer), so whether this WIDENS or NARROWS depends on the target, and `widen` promises a direction. "
                        "Write `cast <type> <value>`: it asks no direction and stops when the value does not fit", k[start]->line);
                return tgt;
            }
            // ★ 모르면 안 문다 — 폭이 안 실린 값(리터럴·혼합식)은 이 검사의 물음 밖이다.
            //   (거짓양성 하나가 거짓음성 여럿보다 비싸다: 사람이 게이트를 끄게 만든다.)
            bool known = (src.k == TK_INT || src.k == TK_FLOAT) && src.bits
                      && (tgt.k == TK_INT || tgt.k == TK_FLOAT) && tgt.bits;
            // ★ 2026-10-10 — 참거짓은 넓히기의 표에 없다(§6.2.5 (4)). 그런데 «모르면 안 문다» 에 걸려 `widen u8 <bool>` 이
            //   말없이 통과해 1 을 냈다. 참거짓 → 정수의 철자는 `cast` 하나다(§6.2.16 (4)).
            if (src.k == TK_BOOL)
                tc_emit(c, "E-WIDEN-KIND",
                        "a bool is not in the widening table — turning true/false into 1/0 crosses a KIND, "
                        "and that is spelt `cast`: `cast u8 b. .`", k[start]->line);
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
                bool transc = veq(h, "@pow") || veq(h, "@sqrt") || veq(h, "@sin") || veq(h, "@cos") ||
                              veq(h, "@exp") || veq(h, "@log") || veq(h, "@fmod");
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
            // ★★★ RFC-0148 S4 (2026-10-14) — 시그니처 표가 이미 답한 낱말들: 부동소수점 함수(`sqrt` … `fmod`)와 `abs` 의 결과는 **`a` 의 타입**,
            //   `min` · `max` 는 «같은 부호의 두 정수, 또는 두 부동» 중 하나다 — 셈과 같은 피연산자 규칙(넓은 쪽). 검사층은 셋 다 «모름» 으로 답해
            //   `return min x. y. .` 를 `u8` 로 내도, `f64` 의 제곱근을 `f32` 로 내도 섰다. 피연산자는 여기서 한 번 잰다.
            {
                bool ffn = veq(h, "@pow") || veq(h, "@sqrt") || veq(h, "@sin") || veq(h, "@cos") || veq(h, "@exp") || veq(h, "@log") ||
                           veq(h, "@fmod") || veq(h, "@floor") || veq(h, "@ceil") || veq(h, "@round");
                if ((ffn || veq(h, "abs")) && n >= 2 && !sig_find(c, h)) {
                    ty_t a0 = tc_infer(c, k[start + 1], env, nenv);
                    for (proven_size_t q = 2; q < n; q++) (void)tc_infer(c, k[start + q], env, nenv);
                    if ((a0.k == TK_FLOAT || (!ffn && a0.k == TK_INT)) && a0.bits && !a0.lit) { a0.has_rng = false; a0.is_mut = false; return a0; }
                    if (a0.k == TK_FLOAT && a0.lit) return (ty_t){ .k = TK_FLOAT, .lit = true, .fval = 0 };   // 리터럴의 함수는 무타입 부동 상수 — 받는 자리가 폭을 정한다
                    return tk(TK_UNKNOWN);
                }
                if ((veq(h, "min") || veq(h, "max")) && n == 3 && !sig_find(c, h)) {
                    ty_t a0 = tc_infer(c, k[start + 1], env, nenv), b0 = tc_infer(c, k[start + 2], env, nenv);
                    a0.has_rng = false; b0.has_rng = false; a0.is_mut = false; b0.is_mut = false;
                    // 같은 부호의 두 정수만(시그니처 표 · 소유자 2026-10-14 «같은 부호만») — 셈과 달리 값을 지키는 넓히기로 건너지 않는다
                    if (a0.k == TK_INT && b0.k == TK_INT && !a0.lit && !b0.lit && a0.bits && b0.bits && a0.sign != b0.sign) {
                        tc_emit(c, "E-TYPE-SIGN", "`min` / `max` take two integers of the SAME sign — one signed and one unsigned operand has no "
                                "single answer type here. Convert one side first (`cast`)", k[start]->line);
                        ty_t u = tk(TK_UNKNOWN); u.bydecl = true; return u;
                    }
                    return tc_binop(c, a0, b0, false, k[start]->line);
                }
            }
            // ★★★ **`neg` 는 부호 있는 타입과 부동소수의 것이다** (RFC-0052 D4 — «무부호 neg → 정적 E-TYPE-SIGN». 2026-10-08 에 지음).
            //   결정은 2026-08 에 났는데 지어지지 않았고, 정본으로 옮겨지며 문장도 빠졌다. 그 사이 `neg` 를 `u8` 7 에 대면
            //   18446744073709551609 — **그 타입에 없는 값** — 이 나왔다. 결과의 타입도 «모름» 이어서 아무 자리에나 들어갔다.
            if (veq(h, "neg") && n == 2 && !sig_find(c, h)) {
                ty_t o = tc_infer(c, k[start + 1], env, nenv);
                if (o.k == TK_INT && !o.lit && !o.sign)
                    tc_emit(c, "E-TYPE-SIGN", "`neg` needs a SIGNED integer or a float — an unsigned type has no negative "
                            "values, so the result could not be a value of that type. Widen to a signed type first "
                            "(`neg (widen i16 x)`), or write the subtraction you mean (`wrap_sub 0 x`)", k[start]->line);
                return o;
            }
            // ★★★ RFC-0148 S4 — **넘침 처분을 적은 셈도 셈이다**: `wrap_*` · `sat_*` 의 결과는 `add` 와 같은 타입이고 `chk_*` 는 그
            //   `option` 이다(시그니처 표 · §6.3.6 «맞는 두 정수 → 둘 중 넓은 쪽»). 전에는 «모름» 이어서 `wrap_sub` 의 결과가 어느
            //   선언에나 들어갔고, 피연산자가 맞는지도 보지 않았다.
            {
                bool wr = veq(h, "wrap_add") || veq(h, "wrap_sub") || veq(h, "wrap_mul") ||
                          veq(h, "sat_add") || veq(h, "sat_sub") || veq(h, "sat_mul");
                bool ch = veq(h, "chk_add") || veq(h, "chk_sub") || veq(h, "chk_mul");
                if ((wr || ch) && n == 3 && !sig_find(c, h)) {
                    ty_t a = tc_infer(c, k[start + 1], env, nenv);
                    ty_t b = tc_infer(c, k[start + 2], env, nenv);
                    tc_push_ctx(k, start + 1, 1, b, a);   // RFC-0148 §8-11
                    tc_push_ctx(k, start + 2, 1, a, b);
                    ty_t r = tc_binop(c, a, b, false, k[start]->line);
                    if (!ch) return r;
                    ty_t w = tk(TK_WRAPPED); w.wrap = 1;
                    if (r.k == TK_INT && !r.lit && r.bits) tc_wrap_payload(&w, r);
                    return w;
                }
            }
            if ((arith || cmp) && n == 3 && !sig_find(c, h)) {   // 사용자 op 가 가리지 않을 때만
                ty_t a = tc_infer(c, k[start + 1], env, nenv);
                ty_t b = tc_infer(c, k[start + 2], env, nenv);
                // RFC-0149 L2 — 굳은 `usize` 와 고정 폭 정수가 셈 · 견줌에서 만났다: 고정 폭 쪽의 식을 댄다(도구가 `cast usize` 로 감싼다)
                g_cmp_eq = veq(h, "eq") || veq(h, "ne");
                // RFC-0148 §8-11 — 한쪽이 무타입이면 다른 쪽의 타입을 그 식까지 내려 보낸다(`bit_and h. sub shl 1 n. . 1 . .`)
                tc_push_ctx(k, start + 1, 1, b, a);
                tc_push_ctx(k, start + 2, 1, a, b);
                return tc_binop(c, a, b, cmp, k[start]->line);
            }
            // ★★★ **논리 op 은 두 경로로 들어온다** — `expr` 섬(중위)과 **여기**(전위 호출).
            //   섬 쪽만 고쳤더니 `and 1 1` 이 그대로 통과했다: 같은 규칙을 두 자리에
            //   걸어야 하는데 한 자리만 걸었다. **한 자리를 고치면 짝이 되는 자리를 늘 물어라**
            //   — 이 저장소가 이미 두 번 배운 것이다(mod 부호 · 슬라이스 원소 폭).
            bool logic = veq(h, "and") || veq(h, "or") || veq(h, "not");
            if (logic && n >= 2 && !sig_find(c, h)) {
                for (proven_size_t q = 1; q < n; q++) {
                    ty_t o = tc_infer(c, k[start + q], env, nenv);   // ★ 소리 내어 잰다 — 섬 쪽(위)과 같은 까닭(RFC-0148 S4)
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
            if ((veq(uh, "some_value") || veq(uh, "ok_value")) && n >= 2 && !sig_find(c, uh)) {
                ty_t w = n == 2 ? tc_infer(c, k[start + 1], env, nenv) : tc_infer_run(c, k, start + 1, n - 1, env, nenv);   // 안쪽이 낱말 줄로 올 수 있다
                if (w.k == TK_WRAPPED && w.nname.size) {
                    ty_t pv = tk(TK_NAMED); pv.nname = w.nname; pv.targ = w.targ;
                    return pv;
                }
                if (w.k == TK_WRAPPED && w.pbits && (w.pk == TK_INT || w.pk == TK_FLOAT)) {   // RFC-0149 L4 — 정수 · 부동 알맹이는 제 타입으로 나온다
                    ty_t pv = w.pk == TK_INT ? tk_int(w.pbits, w.psign) : (ty_t){ .k = TK_FLOAT, .bits = w.pbits };
                    pv.nominal = w.pnom;
                    return pv;
                }
                if (w.k == TK_WRAPPED && w.pk == TK_BOOL) return tk(TK_BOOL);
                if (w.k == TK_WRAPPED && w.pk == TK_WRAPPED) return tc_payload_of(w);   // 감싼 것 안의 감싼 것
                if (w.k == TK_WRAPPED && w.pname.size) { ty_t pv = tk(TK_NAMED); pv.nname = w.pname; return pv; }
                if (k[start]->synth) { ty_t u = tk(TK_UNKNOWN); u.bydecl = true; return u; }   // `using` 패스가 지은 꺼냄 — 값은 선언대로 지어진다
                if (w.k == TK_WRAPPED && w.pk == TK_SLICE && w.ebits) {   // 알맹이 슬라이스는 제 원소와 함께 나온다
                    ty_t sl = tk(TK_SLICE); sl.ebits = w.ebits; sl.esign = w.esign; sl.eflt = w.eflt; sl.enom = w.enom;
                    return sl;
                }
                return tk(TK_UNKNOWN);
            }
        }
        // call: head + args
        const tc_sig_t *s = sig_find_at(c, k[start]);
        if (s) {
            // ★ 인자가 파라미터보다 **모자라면** 짝이 어긋나 있다 — 짝지어 대 보면 엉뚱한 자리를 탓한다(권한을 빠뜨린 부름의 `"x"` 가
            //   `u64` 자리에 대어졌다). 그 부름은 인자 수를 보는 검사(`E-CAP-MISSING` · 수 검사)가 거절한다(2026-10-24).
            //   남는 쪽은 C 의 가변 인자(`printf`)라 앞의 고정 파라미터는 그대로 대 본다.
            const bool aligned = n - 1 >= s->nparams;
            for (proven_size_t i = 0; aligned && i + 1 < n && i < s->nparams; i++) {
                ty_t at = tc_infer(c, k[start + 1 + i], env, nenv);
                tc_reason_t ar = compat(s->params[i], at);
                tc_push_ctx(k, start + 1 + i, 1, s->params[i], at);   // RFC-0148 §8-11
                // ★ RFC-0147 (2026-10-12) — 권한 자리에 **다른 종류의 권한**을 댔으면 그 말로 한다(`E-CAP-KIND`). 권한을 받는
                //   빌트인 연산이 라이브러리 뒤로 가면서, 프로그램이 이 실수를 하는 자리가 잎이 아니라 감싸개의 부름이 됐다.
                //   전엔 «이름만 다른 같은 표현의 타입» (`E-TYPE-NOMINAL`)이라고 했다 — 맞는 거절에 틀린 설명이었다.
                proven_u8str_view_t ck = { 0 };
                if (ar == TC_NOMINAL && k[start + 1 + i]->kind == LOW_CST_ATOM &&
                    tc_param_cap_kind(c->curform, k[start + 1 + i]->tok.lex, &ck)) {
                    tc_emit(c, "E-CAP-KIND", "a capability of ANOTHER KIND is handed to this op: the parameter here is declared "
                            "`cap <kind>` with a different kind. One capability never stands in for another — each names a "
                            "different right, and holding one says nothing about the other (RFC-0011)", k[start + 1 + i]->line);
                } else
                tc_flag(c, ar, "E-TYPE-ARG", "call argument type does not match parameter", k[start + 1 + i]->line);
            }
            return s->ret;
        }
        // ★★★ RFC-0148 S4 — **모르는 머리의 피연산자도 본다.** 검사층이 답을 모르는 낱말(잎 낱말 · `cstr_of` …)이면 그 안쪽 식은 아무도
        //   보지 않았다: `call_builtin hash_bytes subslice d. i. j. . .` 의 `subslice` 가 `u64` 첨자를 받아도 말이 없었다. 결과는 여전히
        //   «모름» 이지만 안쪽의 규칙은 선다. 블록(`do … end`)은 문장이라 여기서 보지 않는다.
        //   ★ 머리가 **문장의 낱말**(`if` · `guard` · `while` …)이면 건드리지 않는다 — 조건 자리의 타입 검사는 따로 정할 일이다
        //     (RFC-0092 의 입 막은 추론 · 위 `tc_ctx_t.quiet`). 처음엔 가르지 않아 조건 속의 섞임이 한꺼번에 울었다(시험 글 6).
        if (k[start]->tok.kw == LOW_KW_NONE || veq(k[start]->tok.lex, "call_builtin"))
            for (proven_size_t i = 1; i < n; i++)
                if (k[start + i]->kind == LOW_CST_GROUP || k[start + i]->kind == LOW_CST_FORM || k[start + i]->kind == LOW_CST_ATOM)
                    (void)tc_infer(c, k[start + i], env, nenv);
    }
    return tk(TK_UNKNOWN);
}
static ty_t tc_infer_impl(tc_ctx_t *c, const low_cst_t *nd, const tc_var_t *env, proven_size_t nenv) {
    if (!nd) return tk(TK_UNKNOWN);
    if (nd->kind == LOW_CST_ATOM) {
        switch (nd->tok.kind) {
            case LOW_TOK_NUMBER: return ty_of_number(nd->tok.lex);
            case LOW_TOK_STRING: case LOW_TOK_TEXTLIT: { ty_t st = tk(TK_NAMED); st.slit = true; return st; }
            default: break;
        }
        if (nd->tok.kw == LOW_KW_TRUE || nd->tok.kw == LOW_KW_FALSE) return tk(TK_BOOL);
        // ★ RFC-0148 S5 (2026-10-14) — `none` 은 알맹이를 모르는 **option** 이다(받는 자리의 `option T` 와 맞는다 — 알맹이 0 은 늘 호환).
        //   전에는 «모름» 이라 `result` 를 내는 op 이 `return none .` 해도 섰다.
        if (nd->tok.kw == LOW_KW_NONEVAL) { ty_t w = tk(TK_WRAPPED); w.wrap = 1; return w; }
        if (nd->tok.kw == LOW_KW_UNIT) return tk(TK_UNIT);
        // 문자 리터럴은 정수 상수다(정본 §6.1.4 (23)) — 숫자 리터럴처럼 받는 자리의 타입을 취한다
        if (nd->tok.kind == LOW_TOK_CHAR) {
            ty_t t = tk_int(0, false); t.lit = true;
            t.val = nd->tok.lex.size == 1 ? (proven_u64)nd->tok.lex.ptr[0] : 0;
            return t;
        }
        // 맨 `ok` — 알맹이 없는 result(`output result void e` 의 성공)
        if (nd->tok.kw == LOW_KW_NONE && veq(nd->tok.lex, "ok") && !sig_find_at(c, nd)) {
            bool f0; (void)env_find(env, nenv, nd->tok.lex, &f0);
            if (!f0) { ty_t w = tk(TK_WRAPPED); w.wrap = 2; return w; }
        }
        bool f; ty_t t = env_find(env, nenv, nd->tok.lex, &f);
        if (f) return t;
        // ★★★ **인자 없는 호출도 타입이 있다** (X-0010 · 2026-08-15). `be dist .` 처럼 인자가
        //   하나도 없는 op 호출은 그냥 ATOM 이라, 환경에 없으면 **TK_UNKNOWN** 으로 떨어졌다 —
        //   즉 0인자 op 의 **반환 타입이 통째로 사라졌다**. 그래서 `var t be seconds dist .`
        //   (dist 는 meters 를 돌려준다)가 검사기의 눈에 *"모르는 것"* 이었다.
        //   ★ 환경이 **먼저**다: 같은 이름의 지역이 있으면 그것이 이 자리의 뜻이다.
        {
            const tc_sig_t *s0 = sig_find_at(c, nd);
            if (s0 && s0->nparams == 0) return s0->ret;
        }
        // ★ RFC-0148 S5 — 모듈 맨 위의 상수(`let LIMIT u8 10 .`)는 제 선언의 타입이다(제 모듈 것을 먼저 본다)
        {
            const tc_mconst_t *hit = NULL;
            for (proven_size_t q = 0; q < g_nmc; q++)
                if (proven_u8str_view_eq(g_mc[q].name, nd->tok.lex)) { if (!hit || proven_u8str_view_eq(g_mc[q].mod, c->curmod)) hit = &g_mc[q]; }
            if (hit) return hit->ty;
        }
        // ★ 값을 지니지 않은 갈래의 이름(`red.` · `color.red`)은 제 열거의 값이다(2026-10-15 — 열거끼리 견줌 · `cast`). 같은 이름의 갈래가
        //   여러 열거에 있으면 한정하지 않은 이름은 정하지 않는다.
        if (nd->tok.kind == LOW_TOK_IDENT && nd->tok.kw == LOW_KW_NONE) {
            proven_u8str_view_t vn = nd->tok.lex, en = { 0 };
            for (proven_size_t q = vn.size; q > 0; q--)
                if (vn.ptr[q - 1] == (proven_byte_t)'.') { en = (proven_u8str_view_t){ vn.ptr, q - 1 }; vn = (proven_u8str_view_t){ vn.ptr + q, vn.size - q }; break; }
            if (en.size) { proven_u8str_view_t eb = en; for (proven_size_t q = en.size; q > 0; q--) if (en.ptr[q - 1] == (proven_byte_t)'.') { eb = (proven_u8str_view_t){ en.ptr + q, en.size - q }; break; } en = eb; }
            const tc_variant_t *vv = variant_find(en, vn);
            if (vv && !vv->np && (!en.size || proven_u8str_view_eq(vv->en, en))) { ty_t et = tk(TK_NAMED); et.nname = vv->en; return et; }
        }
        // ★ RFC-0148 S5 — 지역도 op 도 상수도 아닌 이름은 하강층이 `E-IR-UNDEF` 로 거절한다 — S5 가 «모름» 을 덧붙이지 않는다
        if (nd->tok.kind == LOW_TOK_IDENT && nd->tok.kw == LOW_KW_NONE && !low_ir_is_builtin_op(nd->tok.lex) &&
            !(nd->tok.lex.size > 8 && memcmp(nd->tok.lex.ptr, "machine.", 8) == 0) && !sig_find_at(c, nd)) {
            ty_t u = tk(TK_UNKNOWN); u.bydecl = true; return u;
        }
        // `machine.<이름>` 은 번역 시점에 정해지는 상수다(정본 §5 (4a)) — 무타입 정수 상수(D3), `machine.ptr_caps` 만 참거짓
        if (nd->tok.lex.size > 8 && memcmp(nd->tok.lex.ptr, "machine.", 8) == 0) {
            proven_u8str_view_t mv = nd->tok.lex;   // 참인지 묻는 것들(low_ir.c 의 표와 같다)
            if (veq(mv, "machine.ptr_caps") || veq(mv, "machine.big_endian") || veq(mv, "machine.little_endian") || veq(mv, "machine.has_fpu") ||
                veq(mv, "machine.no_heap") || veq(mv, "machine.no_float") || (mv.size > 11 && memcmp(mv.ptr, "machine.is_", 11) == 0)) return tk(TK_BOOL);
            ty_t t = tk_int(0, false); t.lit = true; t.val = 0; return t;
        }
        return tk(TK_UNKNOWN);
    }
    // ★ RFC-0148 S5 (2026-10-14) — 묶음 안이 낱말 줄이면(`(some_value some lit pt2 do … end .)`) 줄 전체가 값이다. 전에는 첫 낱말만 쟀다.
    if (nd->kind == LOW_CST_GROUP) return nd->nkids == 1 ? tc_infer(c, nd->kids[0], env, nenv)
                                        : nd->nkids ? tc_infer_run(c, (low_cst_t *const *)nd->kids, 0, nd->nkids, env, nenv) : tk(TK_UNKNOWN);
    if (nd->kind == LOW_CST_ACCESS) return tk(TK_UNKNOWN);  // struct/slice element typing: future
    // ★★ RFC-0132 §13.7 — using 패스가 만든 [using, (send …), <나열>]: send 와 나열 원소를 재고, 값의 타입은 선언이 말한다.
    if (nd->kind == LOW_CST_FORM && (nd->nkids == 3 || nd->nkids == 4) && nd->kids[0]->kind == LOW_CST_ATOM && nd->kids[0]->synth &&
        veq(nd->kids[0]->tok.lex, "using")) {
        (void)tc_infer(c, nd->kids[1], env, nenv);
        (void)tc_infer(c, nd->kids[2], env, nenv);
        ty_t t = tk(TK_UNKNOWN); t.bydecl = true; return t;
    }
    // ★★ RFC-0132 T2b-2 — 나열 리터럴의 **실행 중 원소**도 원소 타입에 들어가야 한다. 상수 원소는 검사층
    //   (`ck_lit_walk`)이 이미 폭을 잰다. 틀 안 자리에 쓰는 원소가 말없이 잘리면(`u8` 칸에 1000) X-0074 의 병이다.
    if (nd->kind == LOW_CST_FORM && nd->nkids >= 3 && nd->kids[0]->kind == LOW_CST_ATOM && nd->kids[0]->tok.kw == LOW_KW_LIT &&
        nd->kids[1]->kind == LOW_CST_ATOM && nd->kids[2]->kind == LOW_CST_ATOM &&
        (veq(nd->kids[1]->tok.lex, "array") || veq(nd->kids[1]->tok.lex, "slice") ||
         (veq(nd->kids[1]->tok.lex, "vec") && nd->kids[nd->nkids - 1]->kind != LOW_CST_BLOCK))) {
        ty_t et = ty_of_word(nd->kids[2]->tok.lex);
        if (et.k == TK_NAMED) et.nname = nd->kids[2]->tok.lex;   // T2b-3d — 구조체 원소는 이름으로 맞댄다(E-TYPE-STRUCT)
        proven_size_t e0 = veq(nd->kids[1]->tok.lex, "slice") ? 3 : 4;
        for (proven_size_t q = e0; q < nd->nkids && et.k != TK_UNKNOWN; q++) {
            const low_cst_t *e = nd->kids[q];
            if (e->kind == LOW_CST_BLOCK) {                  // 칸 골라 채우기(T2b-3) — 줄마다 `<번호> <값> .`, 값만 잰다
                for (proven_size_t r = 0; r < e->nkids; r++) {
                    const low_cst_t *cf = e->kids[r];
                    if (cf->kind != LOW_CST_FORM || cf->nkids < 2) continue;
                    if (cf->nkids == 2 && cf->kids[1]->kind == LOW_CST_ATOM && (cf->kids[1]->tok.kind == LOW_TOK_NUMBER ||
                        cf->kids[1]->tok.kw == LOW_KW_TRUE || cf->kids[1]->tok.kw == LOW_KW_FALSE)) continue;
                    ty_t at = cf->nkids == 2 ? tc_infer(c, cf->kids[1], env, nenv) : tc_infer_run(c, cf->kids, 1, cf->nkids - 1, env, nenv);
                    tc_flag(c, compat(et, at), "E-TYPE-SET", "a filled cell's value does not match the list's element type — "
                            "every value is checked against the type written after `lit array`", cf->kids[0]->tok.line);
                }
                continue;
            }
            if (e->kind == LOW_CST_ATOM && (e->tok.kind == LOW_TOK_NUMBER || e->tok.kw == LOW_KW_TRUE ||
                                             e->tok.kw == LOW_KW_FALSE || veq(e->tok.lex, "_"))) continue;
            ty_t at = tc_infer(c, e, env, nenv);
            tc_flag(c, compat(et, at), "E-TYPE-SET", "a list element does not match the list's element type — "
                    "every element is checked against the type written after `lit array`/`lit slice`", e->tok.line ? e->tok.line : nd->line);
        }
        // ★ T2b-3 — `lit vec T N …` 의 타입은 `vec T N` 이다(레인 수·원소 종류가 타입의 일부 — 묶는 자리와 정확히 맞아야 한다)
        if (veq(nd->kids[1]->tok.lex, "vec") && nd->nkids >= 4 && nd->kids[3]->kind == LOW_CST_ATOM && (et.k == TK_INT || et.k == TK_FLOAT)) {
            proven_i64 ln;
            if (tc_int_lit(nd->kids[3]->tok.lex, &ln) && ln > 0 && ln <= 64)
                return (ty_t){ .k = TK_VEC, .bits = et.bits, .sign = et.sign, .lanes = (proven_u8)ln, .vflt = (et.k == TK_FLOAT) };
        }
        // ★ RFC-0148 S5 — `lit array T N …` · `lit slice T …` 의 타입은 원소가 `T` 인 슬라이스다(구조체 원소면 종류만 안다)
        if (veq(nd->kids[1]->tok.lex, "array") || veq(nd->kids[1]->tok.lex, "slice")) {
            if (et.k == TK_INT || et.k == TK_FLOAT || et.k == TK_BOOL) { ty_t sl = tc_slice_of_elem(et); sl.is_mut = true; return sl; }
            ty_t sl = tk(TK_SLICE); sl.is_mut = true; if (et.k == TK_NAMED) sl.nname = et.nname; return sl;
        }
        return tk(TK_UNKNOWN);
    }
    // ★ RFC-0148 S5 — `lit <구조체> do … end` 의 타입은 그 구조체다(칸의 값은 `tc_walk_makes` 가 잰다). 나무에서는 [lit, (<구조체> 몸)] 꼴이다.
    if (nd->kind == LOW_CST_FORM && nd->nkids == 2 && nd->kids[0]->kind == LOW_CST_ATOM && nd->kids[0]->tok.kw == LOW_KW_LIT) {
        const low_cst_t *r = nd->kids[1];
        while (r->kind == LOW_CST_GROUP && r->nkids == 1) r = r->kids[0];
        if (r->kind == LOW_CST_FORM && r->nkids >= 2 && r->kids[0]->kind == LOW_CST_ATOM && r->kids[r->nkids - 1]->kind == LOW_CST_BLOCK) {
            ty_t t = ty_of_word(r->kids[0]->tok.lex);
            if (t.k == TK_NAMED) { t.nname = r->kids[0]->tok.lex; t.lit = true; return t; }
        }
    }
    if (nd->kind == LOW_CST_FORM && nd->nkids >= 3 && nd->kids[0]->kind == LOW_CST_ATOM && nd->kids[0]->tok.kw == LOW_KW_LIT &&
        nd->kids[1]->kind == LOW_CST_ATOM && nd->kids[nd->nkids - 1]->kind == LOW_CST_BLOCK &&
        !veq(nd->kids[1]->tok.lex, "array") && !veq(nd->kids[1]->tok.lex, "slice") && !veq(nd->kids[1]->tok.lex, "vec")) {
        ty_t t = ty_of_word(nd->kids[1]->tok.lex);
        if (t.k == TK_NAMED) { t.nname = nd->kids[1]->tok.lex; return t; }
    }
    // `lit array (array T M) N …` — 줄의 배열(§13.10): 원소가 줄인 슬라이스. 사람이 적는 철자는 `lit slice (array T M) <줄>… .` 이고 `low_rows_lower` 가 이 꼴로 옮긴다
    if (nd->kind == LOW_CST_FORM && nd->nkids >= 3 && nd->kids[0]->kind == LOW_CST_ATOM && nd->kids[0]->tok.kw == LOW_KW_LIT &&
        nd->kids[1]->kind == LOW_CST_ATOM && veq(nd->kids[1]->tok.lex, "array") && nd->kids[2]->kind != LOW_CST_ATOM) {
        for (proven_size_t q = 3; q < nd->nkids; q++) if (nd->kids[q]->kind != LOW_CST_ATOM) (void)tc_infer(c, nd->kids[q], env, nenv);
        proven_u8str_view_t lf = { 0 }; proven_u8 dp = tc_row_depth(nd->kids[2], &lf);
        if (dp) {
            ty_t e = ty_of_word(lf);
            if (e.k == TK_INT || e.k == TK_FLOAT) { ty_t sl = tc_slice_of_elem(e); sl.erow = dp; sl.is_mut = true; return sl; }
        }
        ty_t sl = tk(TK_SLICE); sl.is_mut = true; return sl;
    }
    if (nd->kind == LOW_CST_FORM) return tc_infer_run(c, nd->kids, 0, nd->nkids, env, nenv);
    return tk(TK_UNKNOWN);
}

// ★★★ RFC-0148 S4-0 — 마디마다 낸 답을 적는다(재는 도구 · `LOWENT_LIST_TYPES=1`). 같은 마디를 두 번 물으면 **아는 답이 남는다**
//   (조건 자리의 입 막은 추론과 본 검사가 같은 마디를 본다).
#define TC_NOTE_CAP (1u << 18)
static struct { const void *nd; low_tcnote_t t; bool pushed; } *g_note;
static int g_note_on = -1;
static bool g_note_live;    // 이 단위의 검사가 답을 적었는가 — 하강층은 그때만 읽는다
// ★★★ RFC-0148 S4-2 (2026-10-14 소유자 «검사기 답이 권위») — 답은 **늘** 적는다. 하강층이 그것을 권위로 읽는다(`low_ir.c` 의 `ir_value`).
//   `LOWENT_LIST_TYPES=1` 은 이제 두 답을 한 줄로 **내는 것**만 켠다(`low_tc_list_types`).
bool low_tc_notes_on(void) {
    if (g_note_on < 0) g_note_on = (g_note = calloc(TC_NOTE_CAP, sizeof *g_note)) ? 1 : 0;
    return g_note_on == 1;
}
bool low_tc_notes_live(void) { return g_note_live && low_tc_notes_on(); }
// 하강이 한 단위를 끝내면 답을 닫는다 — 검사 없이 하강하는 다음 단위가 옛 마디의 답을 읽지 않게(마디의 주소는 다시 쓰인다).
void low_tc_notes_close(void) { g_note_live = false; }
bool low_tc_list_types(void) { static int on = -1; if (on < 0) on = getenv("LOWENT_LIST_TYPES") ? 1 : 0; return on == 1; }
// ★ 열거 → 수 `cast` 의 원천 열거(소유자 2026-10-15) — 하강층이 갈래의 번호를 내려면 원천이 어느 열거인지 알아야 한다(값은 단위 전체의
//   갈래 순번이다). 검사층이 `cast` 마디에 적고 하강층이 읽는다. 다 차면 하강층은 원천의 선언 타입으로 찾는다.
static struct { const void *nd; proven_u8str_view_t en; } g_castenum[1024]; static proven_size_t g_ncastenum;
static void tc_cast_enum_note(const void *nd, proven_u8str_view_t en) { if (g_ncastenum >= 1024) { g_tc_full = true; return; } g_castenum[g_ncastenum++] = (typeof(g_castenum[0])){ nd, en }; }   // 다 차면 말한다(E-TYPE-LIMIT) — 빠진 자리의 번호가 틀린다
bool low_tc_cast_enum(const void *node, proven_u8str_view_t *ename) {
    if (!g_note_live) return false;
    for (proven_size_t i = 0; i < g_ncastenum; i++) if (g_castenum[i].nd == node) { *ename = g_castenum[i].en; return true; }
    return false;
}
static void tc_notes_reset(void) {
    g_ncastenum = 0;
    if (low_tc_notes_on()) memset(g_note, 0, TC_NOTE_CAP * sizeof *g_note);   // ★ 한 프로세스가 여러 단위를 검사한다(단위 시험) — 옛 마디의 주소가 새 마디와 겹친다
    g_note_live = true;
}
static proven_u32 tc_note_slot(const void *nd) {
    proven_u64 h = (proven_u64)(uintptr_t)nd * 0x9E3779B97F4A7C15ull;
    proven_u32 i = (proven_u32)(h >> 40) & (TC_NOTE_CAP - 1);
    for (proven_u32 n = 0; n < TC_NOTE_CAP; n++, i = (i + 1) & (TC_NOTE_CAP - 1))
        if (!g_note[i].nd || g_note[i].nd == nd) return i;
    return TC_NOTE_CAP;
}
static proven_u8 tc_note_kind(tk_t k) {
    switch (k) {
        case TK_UNKNOWN: return 0; case TK_INT: return 1; case TK_FLOAT: return 2; case TK_BOOL: return 3;
        case TK_SLICE: return 4; case TK_WRAPPED: return 5; case TK_NAMED: return 6; default: return 7;
    }
}
static void tc_note(const low_cst_t *nd, ty_t t) {
    proven_u32 i = tc_note_slot(nd);
    if (i == TC_NOTE_CAP) return;
    if (g_note[i].nd && g_note[i].t.k && !tc_note_kind(t.k)) return;
    // RFC-0148 §8-11 — 받는 자리가 내려 준 타입(아래 tc_push_ctx)을 다시 추론한 «무타입» 이 덮지 않는다
    if (g_note[i].nd && g_note[i].pushed && t.k == TK_INT && (t.lit || !t.bits)) return;
    g_note[i].nd = nd;
    g_note[i].t = (low_tcnote_t){ .k = tc_note_kind(t.k), .bits = t.bits, .nominal = t.nominal, .sign = t.sign, .lit = t.lit,
        .wrap = t.wrap, .pk = t.pk ? tc_note_kind((tk_t)t.pk) : 0, .pbits = t.pbits, .pnom = t.pnom, .psign = t.psign,
        .ebits = t.ebits, .enom = t.enom, .esign = t.esign, .eflt = t.eflt };
}
bool low_tc_note_of(const void *node, low_tcnote_t *out) {
    if (!low_tc_notes_on()) return false;
    proven_u32 i = tc_note_slot(node);
    if (i == TC_NOTE_CAP || !g_note[i].nd) return false;
    *out = g_note[i].t;
    return true;
}
static void tc_unk_mark(const low_cst_t *nd) {
    while (nd && nd->kind == LOW_CST_GROUP && nd->nkids) nd = nd->kids[0];
    if (!nd) return;
    if (nd->kind == LOW_CST_ATOM) g_unk_head = nd->tok.lex;
    else if (nd->nkids && nd->kids[0]->kind == LOW_CST_ATOM) g_unk_head = nd->kids[0]->tok.lex;
}
// ★★★ RFC-0148 §8-11 (2026-10-22 · 소유자 «추천대로») — **받는 자리의 타입을 무타입 리터럴까지 내려 보낸다.**
//   리터럴은 상대가 타입을 준다(D3). 그런데 리터럴끼리의 셈과 왼쪽이 리터럴인 밀기(`shl 1 n.`)는 받는 자리에 닿을 때까지
//   타입이 없었고, 하강층은 그 사이를 64 비트로 셈했다 — `u8` 을 내는 op 이 `return shl 1 n. .` 로 256 을, `add 200 100` 으로
//   300 을 냈다(그 타입에 없는 값). 이제 받는 자리(반환 · 묶기 · 셈의 상대편 · 호출 인자 · `set`)가 정수 타입을 알면 그 타입을
//   무타입인 식의 마디마다 적는다. 하강층은 그 답을 따르므로(S4-2) 셈이 그 폭에서 일어나고, 넘치면 멈춘다(`E-VM-OVERFLOW` ·
//   `E-VM-SHIFT`). 밀기는 왼쪽만 받는다 — 오른쪽은 미는 칸 수다.
static bool tc_lit_int_atom(const low_cst_t *nd) {
    low_tcnote_t t;
    return nd->kind == LOW_CST_ATOM && nd->tok.kind == LOW_TOK_NUMBER && low_tc_note_of(nd, &t) && t.k == 1 && t.lit;
}
static bool tc_push_shift(proven_u8str_view_t w) { return veq(w, "shl") || veq(w, "shr") || veq(w, "rotl") || veq(w, "rotr"); }
static bool tc_push_arith(proven_u8str_view_t w) {
    return veq(w, "add") || veq(w, "sub") || veq(w, "mul") || veq(w, "div") || veq(w, "mod") || veq(w, "rem") ||
           veq(w, "bit_and") || veq(w, "bit_or") || veq(w, "bit_xor") || veq(w, "bit_not") || veq(w, "neg") ||
           veq(w, "min") || veq(w, "max") || veq(w, "wrap_add") || veq(w, "wrap_sub") || veq(w, "wrap_mul") ||
           veq(w, "sat_add") || veq(w, "sat_sub") || veq(w, "sat_mul");
}
// **리터럴로만 된 식**인가 — 정수 리터럴, 피연산자가 모두 그런 셈, 왼쪽이 그런 밀기(오른쪽은 미는 칸 수라 따지지 않는다).
static bool tc_litexpr_node(const low_cst_t *nd, int depth);
static bool tc_litexpr_run(low_cst_t *const *k, proven_size_t start, proven_size_t n, int depth) {
    if (!n || depth > 48) return false;
    if (n == 1) return tc_litexpr_node(k[start], depth + 1);
    const low_cst_t *h = k[start];
    if (h->kind != LOW_CST_ATOM) return false;
    if (tc_push_shift(h->tok.lex)) return n == 3 && tc_litexpr_node(k[start + 1], depth + 1);
    if (!tc_push_arith(h->tok.lex)) return false;
    for (proven_size_t j = 1; j < n; j++) if (!tc_litexpr_node(k[start + j], depth + 1)) return false;
    return true;
}
static bool tc_litexpr_node(const low_cst_t *nd, int depth) {
    if (!nd || depth > 48) return false;
    while (nd->kind == LOW_CST_GROUP && nd->nkids == 1) nd = nd->kids[0];
    if (nd->kind == LOW_CST_ATOM) return tc_lit_int_atom(nd);
    if (nd->kind == LOW_CST_FORM || nd->kind == LOW_CST_GROUP) return tc_litexpr_run(nd->kids, 0, nd->nkids, depth + 1);
    return false;
}
static void tc_push_note(const low_cst_t *nd, ty_t T) {
    tc_note(nd, T);
    proven_u32 i = tc_note_slot(nd);
    if (i < TC_NOTE_CAP && g_note[i].nd == nd) g_note[i].pushed = true;
}
static void tc_push_node(const low_cst_t *nd, ty_t T, int depth);
static void tc_push_run(low_cst_t *const *k, proven_size_t start, proven_size_t n, ty_t T, int depth) {
    if (!n || depth > 48) return;
    if (n == 1) { tc_push_node(k[start], T, depth + 1); return; }
    const low_cst_t *h = k[start];
    if (h->kind != LOW_CST_ATOM) return;
    if (tc_push_shift(h->tok.lex)) { if (n >= 2) tc_push_node(k[start + 1], T, depth + 1); return; }
    if (tc_push_arith(h->tok.lex)) for (proven_size_t j = 1; j < n; j++) tc_push_node(k[start + j], T, depth + 1);
}
static void tc_push_node(const low_cst_t *nd, ty_t T, int depth) {
    if (!nd || depth > 48 || !tc_litexpr_node(nd, 0)) return;   // 이름 · 이미 타입이 있는 식은 제 타입이 이긴다
    const low_cst_t *in = nd;
    while (in->kind == LOW_CST_GROUP && in->nkids == 1) in = in->kids[0];
    if (in->kind == LOW_CST_FORM || in->kind == LOW_CST_GROUP) tc_push_run(in->kids, 0, in->nkids, T, depth);
    tc_push_note(in, T);
    if (nd != in) tc_push_note(nd, T);
}
static void tc_push_ctx(low_cst_t *const *k, proven_size_t start, proven_size_t n, ty_t decl, ty_t actual) {
    if (!low_tc_notes_on() || decl.k != TK_INT || !decl.bits || decl.rk) return;
    if (actual.k != TK_INT || !tc_litexpr_run(k, start, n, 0)) return;
    ty_t T = decl; T.lit = false; T.has_rng = false; T.is_mut = false;
    tc_push_run(k, start, n, T, 0);
}
static ty_t tc_infer(tc_ctx_t *c, const low_cst_t *nd, const tc_var_t *env, proven_size_t nenv) {
    ty_t t = tc_infer_impl(c, nd, env, nenv);
    if (nd && low_tc_notes_on()) tc_note(nd, t);
    if (t.k == TK_UNKNOWN) tc_unk_mark(nd);
    return t;
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
        // ★ RFC-0148 S4 (2026-10-14) — `owned` 도 한정자다(소유는 소유 검사가 본다). 전에는 `owned` 를 **타입 이름**으로 읽어
        //   `input h owned handle .` 의 타입이 «`owned` 라는 이름» 이 됐다 — `field h. fd .` 가 칸을 못 찾았고 구조체 대조도 비었다.
        if (veq(w, "owned")) continue;
        if (veq(w, "view")) continue;   // ★ RFC-0150 §8-32 — 창의 알맹이는 뒤의 구조체다(창인지는 검사층 `ck_window_check` 가 본다)
        if (veq(w, "ref"))     { t.rk = 1; continue; }
        if (veq(w, "mut_ref")) { t.rk = 2; continue; }
        // ★ `slice <T>` — **원소 타입이 타입의 일부다.**
        //   이것을 안 실어서 `index s i` 가 언제나 u8 이었다. slice u32 의 원소는 u32 다.
        // ★ 머리의 절 범위(`low_op_header`)는 첫 괄호 앞에서 끝난다 — 줄 타입 `(array T M)` 은 범위 바로 뒤에 있을 수 있다
        // ★ RFC-0155 §8-3 — 배열 안의 배열(안쪽 꼴 `array (array T M) N`)의 칸도 줄의 슬라이스로 읽힌다(줄 수는 칸이 쥔다)
        if ((veq(w, "slice") || veq(w, "array")) && i + 1 <= end && i + 1 < f->nkids && f->kids[i + 1]->kind != LOW_CST_ATOM) {   // §13.10 줄의 슬라이스
            proven_u8str_view_t lf = { 0 }; proven_u8 dp = tc_row_depth(f->kids[i + 1], &lf);
            if (dp) {
                ty_t e = ty_of_word(lf);
                if (e.k == TK_INT || e.k == TK_FLOAT) {
                    ty_t sl = tk(TK_SLICE);
                    sl.ebits = e.bits; sl.esign = e.sign; sl.eflt = (e.k == TK_FLOAT); sl.enom = e.nominal == 1 ? 1 : 0; sl.erow = dp;
                    sl.rk = t.rk; sl.is_mut = mut_seen;
                    return sl;
                }
            }
        }
        if ((veq(w, "slice") || veq(w, "array")) && i + 1 < end && i + 1 < f->nkids &&
            f->kids[i + 1]->kind == LOW_CST_ATOM) {
            ty_t e = ty_of_word(f->kids[i + 1]->tok.lex);
            if (e.k == TK_NAMED && veq(w, "slice")) {   // RFC-0148 S5 — 구조체 원소의 이름을 싣는다(`for q ps.` 의 `q` · `idx ps. i.` 가 그 구조체다)
                proven_u8str_view_t cn = tc_canon_name(f->kids[i + 1]->tok.lex); ty_t ce = ty_of_word(cn);
                if (ce.k == TK_INT || ce.k == TK_FLOAT) e = ce;
                else { ty_t sl = tk(TK_SLICE); sl.nname = f->kids[i + 1]->tok.lex; sl.rk = t.rk; sl.is_mut = mut_seen; return sl; }
            }
            if (e.k == TK_INT || e.k == TK_FLOAT || e.k == TK_BOOL) {   // `slice bool` — 원소 폭 칸 1(tc_elem_int)
                ty_t sl = tc_slice_of_elem(e);
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
            if ((ew.k == TK_INT || ew.k == TK_FLOAT) && !tc_int_lit(f->kids[i + 2]->tok.lex, &ln)) {   // 레인 수가 이름(틀의 `L`)이면 레인은 모른다
                ty_t v = { .k = TK_VEC, .bits = ew.bits, .sign = ew.sign, .lanes = 0, .vflt = (ew.k == TK_FLOAT) };
                v.rk = t.rk; v.is_mut = mut_seen;
                return v;
            }
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
        // ★ RFC-0148 S5 — 투명 별칭이 수 · 참거짓을 가리키면(`type meters u64 .`) 그 타입이다(SPEC-004 §89 «같은 타입»). 전에는 이름 타입으로
        //   남아 셈과 대조가 모두 «모름» 이었다.
        if (core.k == TK_NAMED && !core.nominal) {
            proven_u8str_view_t cn = tc_canon_name(w);
            if (!proven_u8str_view_eq(cn, w)) { ty_t ct = ty_of_word(cn); if (ct.k == TK_INT || ct.k == TK_FLOAT || ct.k == TK_BOOL) core = ct; }
        }
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
            // ★ RFC-0148 S4 (2026-10-14) — `segments T` 는 조각들의 뷰다. 꺼내면 나오는 원소(`T`)를 e* 칸에 싣는다 — 하강층의 그림자가 싣는 것과 같다.
            if ((veq(w, "segments") || veq(w, "stack")) && i + 1 < end && i + 1 < f->nkids && f->kids[i + 1]->kind == LOW_CST_ATOM) {
                ty_t e = ty_of_word(tc_canon_name(f->kids[i + 1]->tok.lex));
                if ((e.k == TK_INT || e.k == TK_FLOAT) && e.bits) { core.ebits = e.bits; core.esign = e.sign; core.eflt = (e.k == TK_FLOAT); core.enom = e.nominal == 1 ? 1 : 0; }
            }
            // `task T` · `chan T` — 결과 · 원소의 타입은 **알맹이 칸**(pk · pbits)에 싣는다. e* 칸은 «꺼내면 나오는 것» 이라 하강층이 그 값 자체의
            //   폭으로 읽는다 — 핸들 번호가 `u8` 폭 검사를 받게 된다.
            if ((veq(w, "task") || veq(w, "chan")) && i + 1 < end && i + 1 < f->nkids && f->kids[i + 1]->kind == LOW_CST_ATOM) {
                ty_t e = ty_of_word(tc_canon_name(f->kids[i + 1]->tok.lex));
                if ((e.k == TK_INT || e.k == TK_FLOAT) && e.bits) { core.pk = (proven_u8)e.k; core.pbits = e.bits; core.psign = e.sign; core.pnom = e.nominal; }
                else if (e.k == TK_BOOL) core.pk = (proven_u8)TK_BOOL;
                else if (e.k == TK_NAMED) core.pname = f->kids[i + 1]->tok.lex;
                core.targ = 0u;   // 원소는 알맹이 칸으로 맞댄다(아래 compat)
            }
        }
        // ★★★ **감싸개도 알맹이의 이름을 나른다** (RFC-0104 §8-2 · 2026-08-28).
        //   `option (handle pa)` 와 `option (handle pb)` 는 지금까지 **둘 다 그냥 TK_WRAPPED**
        //   였다. 그래서 `some_value ha` 를 **인라인으로** 쓰면 알맹이의 타입이 통째로 사라졌고
        //   (TK_UNKNOWN), 모르는 것은 안 무는 규율에 따라 **풀 A 의 핸들이 풀 B 로** 흘렀다.
        //   ☆ 바인딩을 거치면(`let hh be (handle pa) some_value ha .`) 물었다 — 검사가 없던 것이
        //     아니라 **타입이 거기까지 안 갔다.**
        //
        //   ★★ 괄호 안의 모양을 **있는 그대로** 읽어야 한다(실측 2026-08-28): 단형화는
        //     `(pool.handle pa)` 를 **반만** 접는다 — 머리 토큰은 `pool.handle` 그대로이고
        //     **인자 자리**가 `handle#pa` 로 바뀐다(형제 쌍을 접는 규칙이 FORM 의 kids 에서
        //     돈 결과다). 그래서 *머리 이름 + 인자 해시* 로 읽으면 같은 타입이 두 철자를 갖고
        //     멀쩡한 프로그램이 빨개진다. **접힌 이름이 있으면 그것이 곧 그 타입의 이름이다.**
        if (core.k == TK_WRAPPED && i + 1 < f->nkids && (i + 1 < end || (i + 1 == end && f->kids[i + 1]->kind != LOW_CST_ATOM))) {   // 머리의 절 범위는 첫 괄호 앞에서 끝난다 — 바로 뒤 괄호가 알맹이다
            {   // X-0085 — 알맹이의 종류(한 낱말 · 앞의 mut 는 건너뛴다)
                proven_size_t q = i + 1;
                while (q < end && q < f->nkids && f->kids[q]->kind == LOW_CST_ATOM && veq(f->kids[q]->tok.lex, "mut")) q++;
                // ★ RFC-0148 S4 (2026-10-14) — 알맹이가 또 감싼 것이면(`result (option usize) e` · `result option u64 e`) 그 종류도 싣는다.
                //   전에는 «모름» 이라 `ok_value` 로 꺼낸 `option` 을 `u64` 이름에 담아도 섰다.
                if (q <= end && q < f->nkids && f->kids[q]->kind != LOW_CST_ATOM) {
                    const low_cst_t *pg = f->kids[q];
                    while (pg->kind == LOW_CST_GROUP && pg->nkids == 1) pg = pg->kids[0];
                    const low_cst_t *ph = pg->kind == LOW_CST_FORM && pg->nkids ? pg->kids[0] : pg->kind == LOW_CST_GROUP && pg->nkids ? pg->kids[0] : NULL;
                    if (ph && ph->kind == LOW_CST_ATOM && (veq(ph->tok.lex, "option") || veq(ph->tok.lex, "result"))) {
                        core.pk = (proven_u8)TK_WRAPPED; core.pwrap = veq(ph->tok.lex, "option") ? 1 : 2;
                        if (pg->nkids >= 2 && pg->kids[1]->kind == LOW_CST_ATOM) {
                            ty_t it = ty_of_word(pg->kids[1]->tok.lex);
                            if ((it.k == TK_INT || it.k == TK_FLOAT) && it.bits) { core.ipk = (proven_u8)it.k; core.pbits = it.bits; core.psign = it.sign; core.pnom = it.nominal; }
                            else if (it.k == TK_BOOL) core.ipk = (proven_u8)TK_BOOL;
                        }
                    }
                }
                if (core.wrap == 2) {   // `result T E` — 알맹이 다음 낱말이 오류 타입이다(알맹이가 한 낱말 · 괄호 하나일 때)
                    proven_size_t eq0 = q;
                    if (eq0 < f->nkids && f->kids[eq0]->kind == LOW_CST_ATOM) {
                        ty_t pw = ty_of_word(f->kids[eq0]->tok.lex);
                        eq0 += (pw.k == TK_SLICE || pw.k == TK_WRAPPED) ? 2 : 1;
                    } else eq0 += 1;
                    if (eq0 < f->nkids && f->kids[eq0]->kind == LOW_CST_ATOM && f->kids[eq0]->tok.kind == LOW_TOK_IDENT) core.ename = f->kids[eq0]->tok.lex;
                }
                if (q + 1 < end && q + 1 < f->nkids && f->kids[q]->kind == LOW_CST_ATOM && veq(f->kids[q]->tok.lex, "nonzero") &&
                    f->kids[q + 1]->kind == LOW_CST_ATOM) q++;   // `option nonzero T` — 알맹이는 0 아닌 `T` 다(값의 타입은 `T`)
                if (q < end && q < f->nkids && f->kids[q]->kind == LOW_CST_ATOM) {
                    ty_t pt = ty_of_word(f->kids[q]->tok.lex);
                    if (pt.k == TK_INT || pt.k == TK_FLOAT || pt.k == TK_BOOL || pt.k == TK_SLICE || pt.k == TK_WRAPPED) core.pk = (proven_u8)pt.k;
                    if (pt.k == TK_WRAPPED && q + 1 < end && q + 1 < f->nkids && f->kids[q + 1]->kind == LOW_CST_ATOM) {   // `result option u64 e`
                        core.pwrap = veq(f->kids[q]->tok.lex, "option") ? 1 : 2;
                        ty_t it = ty_of_word(f->kids[q + 1]->tok.lex);
                        if ((it.k == TK_INT || it.k == TK_FLOAT) && it.bits) { core.ipk = (proven_u8)it.k; core.pbits = it.bits; core.psign = it.sign; core.pnom = it.nominal; }
                        else if (it.k == TK_BOOL) core.ipk = (proven_u8)TK_BOOL;
                    }
                    if ((pt.k == TK_INT || pt.k == TK_FLOAT) && pt.bits) { core.pbits = pt.bits; core.psign = pt.sign; core.pnom = pt.nominal; }
                    // ★ RFC-0148 S4 — 알맹이가 `slice T` 면 그 원소도 싣는다(e* 칸 · 감싸개에서는 알맹이 슬라이스의 원소). 전에는 `option slice u8` 의
                    //   원소를 잃어, `some_value` 로 꺼낸 슬라이스의 `idx` 가 «모름» 이었다.
                    if (pt.k == TK_SLICE && q + 1 < end && q + 1 < f->nkids && f->kids[q + 1]->kind == LOW_CST_ATOM) {
                        ty_t e = ty_of_word(f->kids[q + 1]->tok.lex);
                        if ((e.k == TK_INT || e.k == TK_FLOAT) && e.bits) { core.ebits = e.bits; core.esign = e.sign; core.eflt = (e.k == TK_FLOAT); core.enom = e.nominal == 1 ? 1 : 0; }
                    }
                }
            }
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
            else if (pl->kind == LOW_CST_ATOM && !core.pk) { ty_t pw = ty_of_word(phead); if (pw.k == TK_NAMED) core.pname = phead; }
            else if (pl->kind == LOW_CST_FORM && pl->nkids && pl->kids[0]->kind == LOW_CST_ATOM && !core.pk) {
                ty_t pw = ty_of_word(pl->kids[0]->tok.lex); if (pw.k == TK_NAMED) core.pname = pl->kids[0]->tok.lex;   // `(owned pending)` 의 머리는 한정자
                if (veq(pl->kids[0]->tok.lex, "owned") && pl->nkids >= 2 && pl->kids[1]->kind == LOW_CST_ATOM) core.pname = pl->kids[1]->tok.lex;
            }
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
        "clock", "device", "file_system", "net", "tty", "process",
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
                        "(`type id u64 .`) or write the underlying type",
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
                 "`%.*s` is not a type — no `type` / `struct` / `enum` declares it, and it is not a built-in type%s",
                 (int)t->lex.size, (const char *)t->lex.ptr,
                 // RFC-0149 §6.9 (2026-10-13) — 옛 이름을 쓴 글에는 새 이름을 일러 준다.
                 proven_u8str_view_eq(t->lex, proven_u8str_view_from_cstr("isize"))
                     ? " (the signed size type is spelt `idiff` — the difference of two positions)" : "");
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
        "bool","u8","i8","u16","i16","u32","i32","u64","i64","u128","i128","usize","idiff","f32","f64",
        "slice","array","list","result","option","stack","set","region","ref","mut_ref",
        // ★ `segments T` — 흩어진 조각 여럿을 **한 값으로** (RFC-0104 §8-8, 서명 2026-08-29).
        //   타입 낱말 하나가 늘었다. 그 값은 백업 슬라이스와 (at, n) 서술자 슬라이스 두 칸이고,
        //   `view_segments`·`segs`·`seg` 로만 짓고 읽는다.
        "segments",
        // ★ `task T` · `chan T` — 흐름의 핸들과 채널(RFC-0148 S5 · 소유자 2026-10-14 «핸들·채널에 타입을»). 값은 u64 칸이고, 타입이 결과 · 원소를 싣는다.
        "task", "chan",
        // ★ `view T` · `mut view T` — 바이트 위의 창(RFC-0150 §8-32 · 소유자 2026-10-21). 알맹이 구조체 앞의 한정어다.
        "view",
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
        "process",   // ★ 프로그램 띄우기 (RFC-0136)
        "matrix",    // ★ RFC-0144 — 표면 패스가 `array` 로 옮겨 적으므로 여기에는 서식 때만 온다. 타입 낱말의 표는 이것을 안다
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
    // ★★★ RFC-0148 S4 (2026-10-14) — **이름이 같은 구조체가 여러 모듈에 있다.** 전에는 첫 일치를 집어, `file.close` 의 `field h. fd .` 가
    //   `pool.handle`(칸 `fd` 가 없다)에서 찾아져 «모름» 이 됐다. 한정자가 가리키는 모듈 → 지금 모듈 → 하나뿐인 것 → (예전처럼) 첫 일치.
    for (proven_size_t i = 0; i < c->nstrs; i++)
        if (proven_u8str_view_eq(c->strs[i].name, name)) {
            // 한정 없이 적힌 이름 — 같은 이름이 또 있으면 아래에서 가린다
            bool more = false;
            for (proven_size_t j = i + 1; j < c->nstrs && !more; j++) more = proven_u8str_view_eq(c->strs[j].name, name);
            if (!more) return &c->strs[i];
            for (proven_size_t j = i; j < c->nstrs; j++)
                if (proven_u8str_view_eq(c->strs[j].name, name) && c->curmod.size && proven_u8str_view_eq(c->strs[j].mod, c->curmod)) return &c->strs[j];
            return &c->strs[i];
        }
    proven_u8str_view_t pre, base = tc_base_name(name, &pre);
    if (pre.size) {
        const tc_struct_t *one = NULL; proven_size_t nb = 0;
        for (proven_size_t i = 0; i < c->nstrs; i++)
            if (proven_u8str_view_eq(c->strs[i].name, base)) {
                if (proven_u8str_view_eq(c->strs[i].mod, pre)) return &c->strs[i];
                one = &c->strs[i]; nb++;
            }
        if (nb == 1) return one;
    }
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
                    "a declared field is missing from this `lit` (every field must be given)",
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
        if (nd->kids[j]->kind == LOW_CST_ATOM && veq(nd->kids[j]->tok.lex, "lit")) {
            const low_cst_t *arg = nd->kids[j + 1];
            if (arg->kind == LOW_CST_GROUP && arg->nkids == 1) arg = arg->kids[0];
            tc_check_make(c, arg, env, nenv);
        }
    }
    // ★ RFC-0148 S5 (2026-10-14) — 문장 몸(블록)으로는 미리 내려가지 않는다: 그 안의 문장은 몸을 걸을 때 **제 환경으로** 다시 본다.
    //   미리 내려가면 몸 안에서 묶인 이름(`if … do let bk … . return some lit (handle b) do blk bk. … end`)이 아직 없어 «모름» 이었다.
    bool stmt_head = nd->kind == LOW_CST_FORM && nd->nkids && nd->kids[0]->kind == LOW_CST_ATOM &&
        (nd->kids[0]->tok.kw == LOW_KW_IF || nd->kids[0]->tok.kw == LOW_KW_WHILE || nd->kids[0]->tok.kw == LOW_KW_FOR ||
         nd->kids[0]->tok.kw == LOW_KW_MATCH || nd->kids[0]->tok.kw == LOW_KW_CASE || nd->kids[0]->tok.kw == LOW_KW_ELSE ||
         nd->kids[0]->tok.kw == LOW_KW_GUARD || veq(nd->kids[0]->tok.lex, "repeat") || veq(nd->kids[0]->tok.lex, "task_group"));
    for (proven_size_t j = 0; j < nd->nkids; j++) {
        if (stmt_head && (nd->kids[j]->kind == LOW_CST_BLOCK ||
                          (nd->kids[j]->kind == LOW_CST_FORM && nd->kids[j]->nkids && nd->kids[j]->kids[0]->kind == LOW_CST_ATOM &&
                           (nd->kids[j]->kids[0]->tok.kw == LOW_KW_IF || nd->kids[j]->kids[0]->tok.kw == LOW_KW_ELSE)))) continue;
        tc_walk_makes(c, nd->kids[j], env, nenv);
    }
}

// ★★ RFC-0121 §6.1 — `with` 문맥의 타입은 단계 op 의 **마지막 매개변수** 타입과 맞아야 한다. 문맥은 값으로 건너가므로
//   맞지 않으면 바이트가 다른 자리로 읽힌다 — 호출 인자와 같은 규칙(compat)을 건다.
static void tc_walk_pipes(tc_ctx_t *c, const low_cst_t *nd, const tc_var_t *env, proven_size_t nenv) {
    if (!nd) return;
    if (nd->kind == LOW_CST_FORM && nd->nkids >= 4 && nd->kids[0]->kind == LOW_CST_ATOM) {
        proven_u8str_view_t w = nd->kids[0]->tok.lex;
        proven_size_t opk = (veq(w, "filter") || veq(w, "map") || veq(w, "any") || veq(w, "all") || veq(w, "enumerate")) ? 1 :
                            (veq(w, "scan") || veq(w, "fold") || veq(w, "zip")) ? 2 : 0;
        if (opk && opk + 2 < nd->nkids && nd->kids[opk]->kind == LOW_CST_ATOM && nd->kids[opk + 1]->kind == LOW_CST_ATOM &&
            veq(nd->kids[opk + 1]->tok.lex, "with")) {
            const tc_sig_t *s = sig_find(c, nd->kids[opk]->tok.lex);
            if (s && s->nparams) {
                ty_t want = s->params[s->nparams - 1];
                ty_t got = tc_infer_run(c, nd->kids, opk + 2, nd->nkids - opk - 2, env, nenv);
                tc_flag_types(c, compat(want, got), "E-PIPE-CONTEXT-ARG",
                              "the `with` context does not match the type of the stage op's last parameter (the context's place, "
                              "RFC-0121 §6.1)", want, got, nd->kids[0]->tok.line);
            }
        }
    }
    for (proven_size_t j = 0; j < nd->nkids; j++) tc_walk_pipes(c, nd->kids[j], env, nenv);
}

// `return <값>` 의 값을 op 의 `output` 에 맞댄다 — 문장 `return` 과 guard 의 `else return` 이 같은 규칙을 쓴다.
static void tc_check_return(tc_ctx_t *c, low_cst_t *const *kids, proven_size_t from, proven_size_t n,
                            const tc_var_t *env, proven_size_t nenv, ty_t ret, proven_u32 line) {
    ty_t actual = tc_infer_run(c, kids, from, n, env, nenv);
    tc_flag_types(c, compat(ret, actual),
            "E-TYPE-RETURN", "the returned value does not match the op's `output`", ret, actual, line);
    tc_push_ctx(kids, from, n, ret, actual);   // RFC-0148 §8-11
}

static void tc_check_body(tc_ctx_t *c, const low_cst_t *blk, tc_var_t *env, proven_size_t *nenv, ty_t ret) {
    if (!blk) return;
    for (proven_size_t i = 0; i < blk->nkids; i++) {
        const low_cst_t *f = blk->kids[i];
        if (f->kind != LOW_CST_FORM || f->nkids == 0 || f->kids[0]->kind != LOW_CST_ATOM) continue;
        low_kw_t kw = f->kids[0]->tok.kw;
        tc_walk_makes(c, f, env, *nenv);      // ★ 이 문장 안의 make 리터럴을 검사한다
        tc_walk_pipes(c, f, env, *nenv);      // ★ RFC-0121 — `with` 문맥의 타입
        // ★ `let` 은 타입체크를 **통째로 빠져나가고 있었다.** `var` 만 검사됐다.
        //   `let v be u8 <i32>` 도, `let v be bool <u8>` 조차도 조용히 통과했다.
        //   같은 선언인데 한쪽만 검사한 것은 그냥 빠뜨린 것이다. 같은 규칙을 건다.
        if ((kw == LOW_KW_VAR || kw == LOW_KW_LET) && f->nkids >= 3) {
            proven_size_t be = kw_index(f, LOW_KW_BE);
            rng_res_t rr;
            ty_t declared = ty_of_decl_r(f, 2, be < f->nkids ? be : f->nkids, &rr);
            tc_check_rng(c, rr, f->line);   // ★ RFC-0055 D7
            ty_t actual = (be + 1 < f->nkids)
                            ? tc_infer_run(c, f->kids, be + 1, f->nkids - be - 1, env, *nenv)
                            : tk(TK_UNKNOWN);
            // ★★ **split binding** — `var v be vec u32 4 <expr> .` 에서 타입의 닫는 `.` 가
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
            // `--flat` 은 `use <할당기>.` · `keep` 묶음을 풀지 않는다 — 하강층이 `E-IR-UNSUP` 으로 거절하므로 여기서 타입을 맞대지 않는다
            bool unresolved_use = false;   // [let, $b, option …, using, hb, …] — 풀리지 않은 채 남은 `using` 낱말
            for (proven_size_t q = 2; q < f->nkids && !unresolved_use; q++)
                if (f->kids[q]->kind == LOW_CST_ATOM && (veq(f->kids[q]->tok.lex, "using") || veq(f->kids[q]->tok.lex, "use") || veq(f->kids[q]->tok.lex, "keep")))
                    unresolved_use = true;
            if (declared.rk == 0 && !unresolved_use)   // ref initializers are conservative (value = a borrow)
                tc_flag_types(c, compat(declared, actual),
                        (kw == LOW_KW_LET) ? "E-TYPE-LET" : "E-TYPE-VAR",
                        // ★ RFC-0135 D3 — 값이 option/result 인데 알맹이 타입으로 묶었다: 고칠 길을 말한다
                        (actual.k == TK_WRAPPED && declared.k != TK_WRAPPED && declared.k != TK_UNKNOWN)
                          ? "the value can be empty (an option/result) but the binding takes its content — say what happens when "
                            "it is empty: add `else return … .` (or `else panic \"…\" .`), or declare the binding `option …`/`result …` "
                            "and check it later (RFC-0135 §4.2)"
                          : "the initializer's type does not match the declared type", declared, actual, f->line);
            if (declared.rk == 0 && !unresolved_use && be + 1 < f->nkids)
                tc_push_ctx(f->kids, be + 1, f->nkids - be - 1, declared, actual);   // RFC-0148 §8-11
            // ★★★ **읽기 전용 줄을 쓸 수 있는 이름으로 다시 묶지 못한다** (2026-10-11 · RFC-0144 의 `row` 를 짓다가 찾았다).
            //   `let w mut slice u32 g. .` · `let w mut slice u32 subslice g. 0 4 . .` 가 `g` 가 `let` 이거나 공유 입력이어도
            //   통과했고, `set idx w. 0 . 99 .` 가 부른 쪽의 `let` 나열을 바꿨다(VM · 네이티브 모두 99). `set` 의 자리는 뿌리까지
            //   따라가 보는데(위) 묶기는 보지 않았다. 값이 이름이거나 그 이름의 `subslice` 이면 같은 자리다 — 뿌리가 `mut` 이어야 한다.
            //   `idx` 는 따라가지 않는다: 원소가 다시 슬라이스인 줄에서는 원소의 `mut` 이 따로 정한다.
            if (declared.k == TK_SLICE && declared.is_mut && be + 2 == f->nkids) {
                const low_cst_t *r = f->kids[be + 1];
                for (int hops = 0; r && hops < 64; hops++) {
                    while (r && r->kind == LOW_CST_GROUP && r->nkids == 1) r = r->kids[0];
                    if (!r || r->kind != LOW_CST_FORM || r->nkids < 3 || r->kids[0]->kind != LOW_CST_ATOM) break;
                    // ★ RFC-0150 §8-28 (소유자 2026-10-17 «다 추천대로») — `view_array <T> <조각>` 도 같은 자리다(베끼지 않는다,
                    //   정본 §8.7.2 (4)). 그런데 따라가지 않아서 `let v mut slice u8 view_array u8 xs. .` 가 읽기 전용 `xs` 를 썼다.
                    if (veq(r->kids[0]->tok.lex, "subslice")) r = r->kids[1];
                    else if (veq(r->kids[0]->tok.lex, "view_array")) r = r->kids[2];
                    else break;
                }
                if (r && r->kind == LOW_CST_ATOM && r->tok.kind == LOW_TOK_IDENT) {
                    bool fnd; ty_t st2 = env_find(env, *nenv, r->tok.lex, &fnd);
                    if (fnd && st2.k == TK_SLICE && !st2.is_mut)
                        tc_emit(c, "E-TYPE-MUT",
                                "binding a `mut` slice to a slice that is not declared `mut` — it is the same storage, and the new name "
                                "would write what the old one may only read. Declare the source `var` / `mut`, or bind a plain `slice`", f->line);
                }
            }
            // ★ RFC-0154 §8-19 (가) — 접힌 지역 `let` 은 값을 지닌다(하강층과 같은 규칙 low_ctfe_let_value). `var` 는 지니지 않는다.
            declared.cfold = false;
            if (kw == LOW_KW_LET && be + 1 < f->nkids && declared.k == TK_INT) {
                tc_ctfe_ud_t ud = { env, *nenv, c->curmod };
                const low_ctfe_names_t nm = { tc_ctfe_find, &ud };
                proven_i64 v; bool u;
                if (low_ctfe_let_value(f->kids + be + 1, f->nkids - be - 1, &nm, &v, &u)) {
                    declared.cfold = true; declared.cfv = v; declared.cfu = !declared.sign;
                }
            }
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
            // ★ RFC-0132 P2 — `where c .` 는 머리의 끝 절이다(조건은 몸 첫머리에서, 셈 이름이 보인 채로 잰다)
            proven_size_t wi = bi;
            for (proven_size_t j = 2; j < bi; j++)
                if (f->kids[j]->kind == LOW_CST_ATOM && f->kids[j]->tok.kw == LOW_KW_NONE && veq(f->kids[j]->tok.lex, "where")) { wi = j; break; }
            // ★★ RFC-0132 P2 — 점화식 `be τ v . while c . next e .`: 이름의 타입은 τ, 시작과 다음 값은 τ 에 들어가야 한다
            if (wi > 4 && f->kids[2]->kind == LOW_CST_ATOM && f->kids[2]->tok.kw == LOW_KW_BE && f->kids[3]->kind == LOW_CST_ATOM) {
                ty_t tau = ty_of_word(f->kids[3]->tok.lex);
                proven_size_t wh = wi, nx = wi;
                for (proven_size_t j = 4; j < wi; j++) {
                    if (f->kids[j]->kind == LOW_CST_ATOM && f->kids[j]->tok.kw == LOW_KW_WHILE && wh == wi) wh = j;
                    else if (f->kids[j]->kind == LOW_CST_ATOM && veq(f->kids[j]->tok.lex, "next") && wh < wi) { nx = j; break; }
                }
                if (wh < wi && nx < wi && wh > 4) {
                    ty_t sv = tc_infer_run(c, f->kids, 4, wh - 4, env, *nenv);
                    tc_flag(c, compat(tau, sv), "E-TYPE-WIDTH", "a recurrence's start does not fit its declared type", f->line);
                    if (*nenv < TC_MAXENV) { env[*nenv].name = f->kids[1]->tok.lex; env[(*nenv)++].ty = tau; }
                    ty_t cv = tc_infer_run(c, f->kids, wh + 1, nx - wh - 1, env, *nenv);
                    if (cv.k != TK_UNKNOWN && cv.k != TK_BOOL && cv.k != TK_NAMED)
                        tc_emit(c, "E-TYPE-COND", "the `while` of a recurrence is a condition (`bool`)", f->line);
                    ty_t nv = tc_infer_run(c, f->kids, nx + 1, wi - nx - 1, env, *nenv);
                    tc_flag(c, compat(tau, nv), "E-TYPE-WIDTH", "a recurrence's next value does not fit its declared type", f->line);
                }
                if (wi < bi) (void)tc_infer_run(c, f->kids, wi + 1, bi - wi - 1, env, *nenv);
                for (proven_size_t j = 0; j < f->nkids; j++)
                    if (f->kids[j]->kind == LOW_CST_BLOCK) tc_check_body(c, f->kids[j], env, nenv, ret);
                goto for_done;
            }
            // ★★ RFC-0132 P1 — `count τ n .` · `range τ a b [step k] .` : 셈의 타입 τ 는 늘 적고(N5), 끝은 τ 에 들어가야 한다(넓히기만).
            if (bi > 3 && f->kids[2]->kind == LOW_CST_ATOM && f->kids[2]->tok.kw == LOW_KW_NONE &&
                (veq(f->kids[2]->tok.lex, "count") || veq(f->kids[2]->tok.lex, "range")) && f->kids[3]->kind == LOW_CST_ATOM &&
                f->kids[3]->tok.kw == LOW_KW_NONE && !veq(f->kids[3]->tok.lex, "where")) {   // RFC-0150 Q3 — `for x count if c do` 의 `count` 는 원천이다(거르기는 안에서 `where` 다)
                ty_t tau = ty_of_word(f->kids[3]->tok.lex);
                if (tau.k != TK_INT) {
                    tc_emit(c, "E-FOR-STEP", tau.k == TK_FLOAT
                            ? "a counted loop counts in an integer type — a float has no next value to step to (RFC-0132 §8.2)"
                            : "`count`/`range` need the counting type first: `repeat i u64 n` · `range i u64 3 10` (RFC-0132 §8.2)", f->line);
                } else {
                    for (proven_size_t q = 4; q < wi; q++) {
                        if (f->kids[q]->kind == LOW_CST_ATOM && veq(f->kids[q]->tok.lex, "step")) {
                            if (q + 1 < bi && f->kids[q + 1]->kind == LOW_CST_ATOM && f->kids[q + 1]->tok.kind == LOW_TOK_NUMBER) {
                                proven_i64 sv; if (tc_int_lit(f->kids[q + 1]->tok.lex, &sv) && sv == 0)
                                    tc_emit(c, "E-FOR-STEP", "`step 0` never moves — the loop would not end (RFC-0132 §8.2)", f->line);
                            }
                            break;                                   // step 은 셈의 타입과 따로(부호 있는 64 비트 거리)
                        }
                        ty_t et = tc_infer(c, f->kids[q], env, *nenv);
                        tc_flag(c, compat(tau, et), "E-TYPE-WIDTH", "a loop bound does not fit the counting type", f->line);
                    }
                }
                if (*nenv < TC_MAXENV) { env[*nenv].name = f->kids[1]->tok.lex; env[(*nenv)++].ty = tau.k == TK_INT ? tau : tk_int(64, false); }
                if (wi < bi) (void)tc_infer_run(c, f->kids, wi + 1, bi - wi - 1, env, *nenv);
                for (proven_size_t j = 0; j < f->nkids; j++)
                    if (f->kids[j]->kind == LOW_CST_BLOCK) tc_check_body(c, f->kids[j], env, nenv, ret);
                goto for_done;
            }
            bool mut_src = bi > 3 && f->kids[2]->kind == LOW_CST_ATOM && veq(f->kids[2]->tok.lex, "mut");   // for x mut buf
            ty_t seq = (wi > 2) ? tc_infer_run(c, f->kids, mut_src ? 3 : 2, wi - (mut_src ? 3 : 2), env, *nenv) : tk(TK_UNKNOWN);
            if (mut_src && seq.k == TK_SLICE && !seq.is_mut && f->kids[3]->kind == LOW_CST_ATOM)
                tc_emit(c, "E-TYPE-MUT", "`for x mut <slice>` writes the slice's elements — the slice must be declared `mut` "
                        "(a `let` list, a string or a shared parameter is read-only)", f->line);
            if (seq.k != TK_UNKNOWN && seq.k != TK_NAMED && seq.k != TK_SLICE)
                tc_emit(c, "E-TYPE-ITER",
                        "`for … in` needs a slice — this value cannot be iterated", f->line);
            if (*nenv < TC_MAXENV) {
                // ★ 루프 변수는 **슬라이스의 원소 타입**을 갖는다.
                //   (u8 고정이었다 — `slice T` 가 원소 타입을 나르게 되면서 고칠 수 있게 됐다.)
                ty_t ev = tk_int(8, false);
                // ★ RFC-0148 S5 — 원소를 모르는 슬라이스에서 `u8` 을 지어내지 않는다(`idx` 와 같은 병). 구조체 원소면 그 구조체다.
                if (seq.k == TK_SLICE && !seq.ebits && !seq.erow) {
                    if (seq.nname.size) { ev = tk(TK_NAMED); ev.nname = seq.nname; } else ev = tk(TK_UNKNOWN);
                }
                if (seq.k == TK_UNKNOWN || seq.k == TK_WRAPPED) ev = tk(TK_UNKNOWN);
                if (seq.k == TK_SLICE && seq.erow) { ev = seq; ev.erow--; }   // §13.10 — 줄마다 도는 변수는 줄 슬라이스
                else if (seq.k == TK_SLICE && seq.ebits)
                    ev = seq.eflt ? (ty_t){ .k = TK_FLOAT, .bits = seq.ebits }
                                  : tc_elem_int(seq.ebits, seq.esign);
                if (seq.k == TK_SLICE && seq.ebits && !seq.eflt) ev.nominal = seq.enom;
                env[*nenv].name = f->kids[1]->tok.lex;
                env[(*nenv)++].ty = ev;
            }
            if (wi < bi) (void)tc_infer_run(c, f->kids, wi + 1, bi - wi - 1, env, *nenv);   // `where` 조건
            for (proven_size_t j = 0; j < f->nkids; j++)
                if (f->kids[j]->kind == LOW_CST_BLOCK) tc_check_body(c, f->kids[j], env, nenv, ret);
            for_done:;
        } else if (kw == LOW_KW_NONE && f->nkids == 3 && f->kids[0]->kind == LOW_CST_ATOM && veq(f->kids[0]->tok.lex, "copy") &&
                   !sig_find_at(c, f->kids[0])) {
            // ★ RFC-0132 P3 — `copy <받는 쪽> <주는 쪽> .`: 둘 다 슬라이스, 원소 타입이 같고, 받는 쪽은 `mut` 이어야 한다
            ty_t dt = tc_infer(c, f->kids[1], env, *nenv), st3 = tc_infer(c, f->kids[2], env, *nenv);
            if ((dt.k != TK_UNKNOWN && dt.k != TK_SLICE && dt.k != TK_NAMED) || (st3.k != TK_UNKNOWN && st3.k != TK_SLICE && st3.k != TK_NAMED))
                tc_emit(c, "E-TYPE-SET", "`copy` copies a slice's contents into another slice — both sides are slices", f->line);
            else if (dt.k == TK_SLICE && st3.k == TK_SLICE && dt.ebits && st3.ebits &&
                     (dt.ebits != st3.ebits || dt.eflt != st3.eflt || dt.esign != st3.esign))
                tc_emit(c, "E-TYPE-SET", "`copy` needs two slices of the same element type", f->line);
            // ★ RFC-0150 §8-28 — 받는 쪽이 `subslice X …` · `view_array T X` 이면 X 의 자리다. 맨 이름만 보던 탓에
            //   `copy subslice xs. 0 1 . ys. .` 가 읽기 전용 `xs` 에 썼다.
            if (tc_slice_ro(c, f->kids[1], env, *nenv))
                tc_emit(c, "E-TYPE-MUT", "`copy` writes the receiving slice — it must be declared `mut` (a `let` list, a string, a "
                        "shared parameter or a `comptime` array is read-only)", f->line);
        } else if (kw == LOW_KW_NONE && f->nkids == 4 && f->kids[0]->kind == LOW_CST_ATOM && veq(f->kids[0]->tok.lex, "swap") &&
                   !sig_find_at(c, f->kids[0])) {
            // ★ 2026-10-30 (RFC-0015 §6.3 을 짓다 찾았다) — `swap s i j .` 도 슬라이스에 쓴다. 검사가 없어서 `let` 상수 나열을 맞바꿨고
            //   VM(2)과 네이티브(1)가 갈렸다(상수 나열은 읽기 전용 바이트다). `copy` 와 같은 규칙이다.
            for (proven_size_t j = 1; j < 4; j++) (void)tc_infer(c, f->kids[j], env, *nenv);
            if (tc_slice_ro(c, f->kids[1], env, *nenv))
                tc_emit(c, "E-TYPE-MUT", "`swap` writes the slice — it must be declared `mut` (a `let` list, a string, a "
                        "shared parameter or a `comptime` array is read-only)", f->line);
        } else if (kw == LOW_KW_SET && f->nkids >= 3 && f->kids[1]->kind == LOW_CST_GROUP) {
            // ★ `set (index s i) v .` — s 는 **mut** 로 선언돼 있어야 한다.
            //   가변성은 정적으로 강제된다(런타임 슬라이스 값에는 mut 표시가 없다 — 포인터가 const 다).
            const low_cst_t *g = f->kids[1];
            // ★★★ RFC-0148 S4 (2026-10-14) — **쓰는 값은 어떤 자리에 쓰든 한 번 검사된다.** 아래 갈래들은 값을 조용히(`quiet`) 재거나
            //   (`idx` 의 원소를 알 때), 아예 안 쟀다(원소를 모를 때 · 칸 이름이 없을 때). 그래서 `set idx st. i. . idx rk. add r. i. . . .` 의
            //   값 안에서 `u64` 와 `usize` 를 더해도 섰다(`aes.add_round_key`). 갈래가 값을 재지 않았으면 끝에서 잰다.
            bool vdone = false;
            // ★★★ RFC-0149 L2 (2026-10-13) — **쓰는 자리의 첨자도 첨자다.** `set (idx s i) v .` 의 `i` 는 어디서도 추론되지 않아서,
            //   옮길 자리를 세는 집계(`LENU64 idx`)가 읽는 자리만 세고 있었다. 자리를 한 번 추론해 첨자를 같은 길로 보낸다.
            if (g->nkids == 1 && g->kids[0]->kind == LOW_CST_FORM && g->kids[0]->nkids >= 3 &&
                g->kids[0]->kids[0]->kind == LOW_CST_ATOM && veq(g->kids[0]->kids[0]->tok.lex, "idx")) {
                ty_t et = tc_infer(c, g->kids[0], env, *nenv);
                // ★★★ RFC-0149 L4 — **쓰는 값도 원소의 타입이어야 한다.** `set (idx s i) v .` 의 `v` 는 원소 타입과 견주어지지 않아서
                //   `usize` 를 `u64` 칸에, `u64` 를 `u8` 칸에 적어도 섰다(뒤의 것은 실행 중에 멈추거나 잘렸다). 정수 · 부동 원소만 본다.
                if ((et.k == TK_INT || et.k == TK_FLOAT) && et.bits && f->nkids >= 3) {
                    ty_t vt = tc_infer_run(c, f->kids, 2, f->nkids - 2, env, *nenv);
                    vdone = true;
                    et.lit = false; et.has_rng = false;
                    if (vt.k == TK_INT || vt.k == TK_FLOAT || (vt.k == TK_NAMED && vt.nname.size && tc_is_enum_name(tc_canon_name(vt.nname)))) {
                        tc_reason_t vr = compat(et, vt);
                        if (vr != TC_OK) tc_flag(c, vr, "E-TYPE-SET", "the value written does not have the element's type", f->line);
                    }
                }
            }
            if (g->nkids == 1 && g->kids[0]->kind == LOW_CST_FORM && g->kids[0]->nkids == 3 &&
                g->kids[0]->kids[0]->kind == LOW_CST_ATOM &&
                veq(g->kids[0]->kids[0]->tok.lex, "idx") &&
                g->kids[0]->kids[1]->kind == LOW_CST_ATOM) {
                proven_u8str_view_t sn = g->kids[0]->kids[1]->tok.lex;
                bool fnd; ty_t st2 = env_find(env, *nenv, sn, &fnd);
                if (fnd && !st2.is_mut)
                    tc_emit(c, "E-TYPE-MUT",
                            "writing an element of a slice that is not declared `mut` "
                            "(a shared slice is read-only)", f->line);
                // ★ RFC-0132 §13.10 — 줄의 배열에서 한 줄을 통째로 바꾸는 `set (index g r) <줄>` 은 없다(소유자 결정 «줄 보기»):
                //   칸을 하나씩 쓰거나 `copy` 로 줄을 베낀다. 실행 중에 멈추는 대신 여기서 말한다.
                if (fnd && st2.k == TK_SLICE && st2.erow)
                    tc_emit(c, "E-TYPE-SET",
                            "a whole row is not replaced with `set (idx … )` — write its cells (`set (idx (idx g r) c) v .`), "
                            "or copy a row into it with `copy` (RFC-0132 §13.10)", f->line);
            }
            // ★★★ **자리의 뿌리까지 내려가 본다** (2026-10-10 · RFC-0144 를 짓다가 찾았다). 위의 검사는 `set (idx <이름> …)` 만
            //   보았다 ⇒ `set (idx (subslice x …) i) v` · `set (idx (idx g r) c) v` 는 x · g 가 `let` 이거나 공유 입력이어도
            //   통과했다(읽기 전용 자리에 쓴다). `idx` 와 `subslice` 는 받은 자리를 그대로 가리키므로, 받는 쪽을 따라 이름까지 간다.
            if (g->nkids == 1 && g->kids[0]->kind == LOW_CST_FORM && g->kids[0]->nkids >= 2 &&
                g->kids[0]->kids[0]->kind == LOW_CST_ATOM && veq(g->kids[0]->kids[0]->tok.lex, "idx") &&
                g->kids[0]->kids[1]->kind != LOW_CST_ATOM) {
                const low_cst_t *r = g->kids[0]->kids[1];
                for (int hops = 0; r && hops < 64; hops++) {
                    while (r && r->kind == LOW_CST_GROUP && r->nkids == 1) r = r->kids[0];
                    if (!r || r->kind != LOW_CST_FORM || r->nkids < 2 || r->kids[0]->kind != LOW_CST_ATOM) break;
                    if (veq(r->kids[0]->tok.lex, "idx") || veq(r->kids[0]->tok.lex, "subslice")) r = r->kids[1];
                    else if (veq(r->kids[0]->tok.lex, "view_array") && r->nkids >= 3) r = r->kids[2];   // RFC-0150 §8-28
                    else break;
                }
                if (r && r->kind == LOW_CST_ATOM && r->tok.kind == LOW_TOK_IDENT) {
                    bool fnd; ty_t st2 = env_find(env, *nenv, r->tok.lex, &fnd);
                    if (fnd && st2.k == TK_SLICE && !st2.is_mut)
                        tc_emit(c, "E-TYPE-MUT",
                                "writing an element of a slice that is not declared `mut` "
                                "(a shared slice is read-only) — the place is reached through `idx` / `subslice`, and it is the same storage", f->line);
                }
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
                // ★★★ **정수 세그먼트는 필드 이름이 아니다 — 슬라이스 첨자다.**
                //   `field` 가 다단이 되면서 `set (field s 0) 11 .` 이 legal 이 됐는데
                //   (예전 `set s.0 11 .` 이 하던 일), 이 검사는 **선언된 구조체에서 이름을
                //   찾다가** 못 찾고 "가리키는 것이 없다" 고 고발했다. 읽기 쪽은 이미
                //   정수를 INDEX 로 내리고 있었다 — **쓰기 쪽만 그 규칙을 안 배웠다.**
                bool all_digit = fn.size > 0;
                for (proven_size_t z = 0; z < fn.size; z++)
                    if (fn.ptr[z] < '0' || fn.ptr[z] > '9') { all_digit = false; break; }
                if (all_digit) { (void)tc_infer_run(c, f->kids, 2, f->nkids - 2, env, *nenv); continue; }
                // 레코드의 구조체 타입을 이름으로 찾을 수 없으므로(현재 ty_t 에 이름이 없다),
                // **모든 선언된 구조체**에서 그 필드를 찾는다 — 보수적이지만 없는 필드는 잡는다.
                // ★★★ RFC-0148 S4 (2026-10-14) — 이제 `ty_t` 에 이름이 있다: **그 레코드의 구조체**에서 칸을 찾는다. 이름을 모를 때만
                //   옛 길(모든 구조체)로 가되, 같은 이름의 칸이 구조체마다 타입이 다르면 타입은 맞대지 않는다 — 옛 길은 **마지막에
                //   선언된** 구조체의 칸을 집었다(`mapgen.table` 의 `n u64` 에 쓰는 글이 `vecgen.vec` 의 `n usize` 로 재어졌다).
                bool any_has = false, amb = false; ty_t fty = tk(TK_UNKNOWN);
                const tc_struct_t *rst = (fnd && rt.k == TK_NAMED && rt.nname.size) ? struct_find(c, rt.nname) : NULL;
                for (proven_size_t q = 0; q < c->nstrs; q++) {
                    if (rst && rst != &c->strs[q]) continue;
                    for (proven_size_t v = 0; v < c->strs[q].nf; v++)
                        if (proven_u8str_view_eq(c->strs[q].fname[v], fn)) {
                            ty_t ft = c->strs[q].fty[v];
                            if (any_has && (ft.k != fty.k || ft.bits != fty.bits || ft.sign != fty.sign || ft.nominal != fty.nominal)) amb = true;
                            any_has = true; fty = ft;
                        }
                }
                if (amb) fty = tk(TK_UNKNOWN);
                if (c->nstrs && !any_has)
                    tc_emit(c, "E-TYPE-FIELD",
                            "no declared struct has this field (the name points at nothing)", f->line);
                else if (any_has) {
                    ty_t actual = tc_infer_run(c, f->kids, 2, f->nkids - 2, env, *nenv);
                    vdone = true;
                    tc_flag(c, compat(fty, actual),
                            "E-TYPE-FIELD", "the value does not match the field's declared type",
                            f->line);
                }
            }
            if (!vdone) (void)tc_infer_run(c, f->kids, 2, f->nkids - 2, env, *nenv);
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
                if (to + 1 < f->nkids) tc_push_ctx(f->kids, to + 1, f->nkids - to - 1, core, actual);   // RFC-0148 §8-11
                else tc_push_ctx(f->kids, 2, f->nkids - 2, core, actual);
            }
        } else {
            // ★ `store <dst> <idx> <vec>`·`store_masked <dst> <idx> <vec> <mask>` (RFC-0040) — dst 는 **mut** 로
            //   선언돼 있어야 한다(읽기 전용 슬라이스에 벡터를 쓸 수 없다). set (index) 의 mut 강제와 같은 규율.
            if (f->nkids >= 4 && f->kids[0]->kind == LOW_CST_ATOM &&
                (veq(f->kids[0]->tok.lex, "store") || veq(f->kids[0]->tok.lex, "store_masked")) &&
                tc_view_root(f->kids[1]) && tc_view_root(f->kids[1])->kind == LOW_CST_ATOM) {   // RFC-0150 §8-28 — 뷰를 거쳐도 뿌리를 본다
                bool fnd; ty_t dt = env_find(env, *nenv, tc_view_root(f->kids[1])->tok.lex, &fnd);
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
                    // ★★★ RFC-0148 §8-14 (2026-10-14 소유자 «셋 다 권고대로») — **조건 자리의 식도 타입 검사를 받는다.** RFC-0092 는 여기서
                    //   타입만 묻고 딸린 진단은 막아 두었다(`quiet`): `while lt i. n. .` 에서 `usize` 와 `u64` 가 섞여도, 조건 속의
                    //   `idx` 가 `u64` 첨자를 받아도 말이 없었다. 조건은 식이다 — 다른 자리의 식과 같은 규칙을 받는다.
                    ty_t ct = tc_infer_run(c, f->kids, 1, cend - 1, env, *nenv);
                    if (ct.k == TK_INT || ct.k == TK_FLOAT)
                        tc_emit(c, "E-TYPE-COND",
                                "a condition must be bool, not a number — this language has no "
                                "truthiness (D12: no implicit int↔bool). Say what the test is: "
                                "`ne x 0`", f->line);
                }
            }
            // ★★★ RFC-0148 S5 (2026-10-14) — **갈래가 꺼낸 이름에도 타입이 있다.** `match s. do case rect w h do … end . end .` 의 `w` · `h` 는
            //   그 변형의 알맹이 타입이고, `option` 의 `some v` · `result` 의 `ok v` 는 알맹이, `error e` 는 오류 열거형이다. 전에는 환경에 없어
            //   몸 안의 셈과 반환이 모두 «모름» 이었다(하강층은 S2 부터 알았다).
            if (kw == LOW_KW_MATCH && f->nkids >= 3 && f->kids[f->nkids - 1]->kind == LOW_CST_BLOCK) {
                ty_t st = tc_infer_run(c, f->kids, 1, f->nkids - 2, env, *nenv);
                const low_cst_t *mb = f->kids[f->nkids - 1];
                for (proven_size_t q = 0; q < mb->nkids; q++) {
                    const low_cst_t *cf = mb->kids[q];
                    if (cf->kind != LOW_CST_FORM || cf->nkids < 2 || cf->kids[0]->kind != LOW_CST_ATOM || cf->kids[0]->tok.kw != LOW_KW_CASE) {
                        if (cf->kind == LOW_CST_FORM) { low_cst_t *one[1] = { (low_cst_t *)cf }; low_cst_t b1 = *cf; b1.kind = LOW_CST_BLOCK; b1.kids = one; b1.nkids = 1; tc_check_body(c, &b1, env, nenv, ret); }
                        continue;
                    }
                    proven_size_t save = *nenv, pos = 1;
                    // 갈래 하나에 `or` 로 이은 꼴이 여럿일 수 있다 — 첫 꼴의 이름만 묶는다(나머지 꼴도 같은 이름을 같은 자리에 둔다)
                    if (cf->kids[1]->kind == LOW_CST_ATOM) {
                        proven_u8str_view_t vn = cf->kids[1]->tok.lex;
                        pos = 2;
                        ty_t bt[8]; proven_size_t nb = 0;
                        if (st.k == TK_WRAPPED && (veq(vn, "some") || veq(vn, "ok"))) {
                            ty_t pv = tc_payload_of(st);
                            // 겹친 꼴 `case ok (some x)` — 안쪽 꼴의 이름은 알맹이의 알맹이다
                            if (cf->nkids > 2 && cf->kids[2]->kind != LOW_CST_ATOM && cf->kids[2]->kind != LOW_CST_BLOCK) {
                                const low_cst_t *ig = cf->kids[2];
                                while (ig->kind == LOW_CST_GROUP && ig->nkids == 1) ig = ig->kids[0];
                                if (ig->nkids >= 2 && ig->kids[1]->kind == LOW_CST_ATOM && *nenv < TC_MAXENV) {
                                    env[*nenv].name = ig->kids[1]->tok.lex; env[(*nenv)++].ty = tc_payload_of(pv);
                                }
                            }
                            bt[nb++] = pv;
                        } else if (st.k == TK_WRAPPED && veq(vn, "error") && st.ename.size) {
                            ty_t ev = tk(TK_NAMED); ev.nname = st.ename; bt[nb++] = ev;
                        } else if (st.k == TK_NAMED && st.nname.size && struct_find(c, st.nname) &&
                                   proven_u8str_view_eq(tc_base_name(st.nname, NULL), tc_base_name(vn, NULL))) {
                            // 구조체 분해 `case point x a y b` — 칸 이름 다음이 묶는 이름이다(`_` 는 묶지 않는다)
                            const tc_struct_t *sd = struct_find(c, st.nname);
                            for (proven_size_t t = 2; t + 1 < cf->nkids && cf->kids[t]->kind == LOW_CST_ATOM && cf->kids[t + 1]->kind == LOW_CST_ATOM; t += 2) {
                                if (veq(cf->kids[t + 1]->tok.lex, "_") || *nenv >= TC_MAXENV) continue;
                                ty_t ft = tk(TK_UNKNOWN);
                                for (proven_size_t z = 0; z < sd->nf; z++) if (proven_u8str_view_eq(sd->fname[z], cf->kids[t]->tok.lex)) { ft = sd->fty[z]; ft.is_mut = false; }
                                env[*nenv].name = cf->kids[t + 1]->tok.lex; env[(*nenv)++].ty = ft;
                            }
                            for (proven_size_t t = 1; t < cf->nkids; t++)
                                if (cf->kids[t]->kind == LOW_CST_BLOCK) tc_check_body(c, cf->kids[t], env, nenv, ret);
                            *nenv = save;
                            continue;
                        } else {
                            proven_u8str_view_t en = st.k == TK_NAMED ? tc_base_name(st.nname, NULL) : (proven_u8str_view_t){ 0 };
                            proven_u8str_view_t vb = tc_base_name(vn, NULL);
                            const tc_variant_t *vv = variant_find(en, vb);
                            if (vv) for (proven_size_t t = 0; t < vv->np && nb < 8; t++) bt[nb++] = vv->pt[t];
                        }
                        for (proven_size_t t = 2, bi = 0; t < cf->nkids && cf->kids[t]->kind == LOW_CST_ATOM && !veq(cf->kids[t]->tok.lex, "or"); t++, bi++) {
                            if (*nenv >= TC_MAXENV) break;
                            env[*nenv].name = cf->kids[t]->tok.lex;
                            env[(*nenv)++].ty = bi < nb ? bt[bi] : tk(TK_UNKNOWN);
                        }
                    }
                    (void)pos;
                    for (proven_size_t t = 1; t < cf->nkids; t++)
                        if (cf->kids[t]->kind == LOW_CST_BLOCK) tc_check_body(c, cf->kids[t], env, nenv, ret);
                    *nenv = save;
                }
                continue;
            }
            // ★ RFC-0148 S5 — `while pop <쌓개> into <이름> . do … end` 의 이름은 쌓개의 원소 타입이다(몸에서 읽힌다)
            if (kw == LOW_KW_WHILE) {
                // 평평한 꼴 [while, pop, <쌓개>, into, <이름>, 몸]
                if (f->nkids >= 6 && f->kids[1]->kind == LOW_CST_ATOM && veq(f->kids[1]->tok.lex, "pop") && f->kids[3]->kind == LOW_CST_ATOM &&
                    veq(f->kids[3]->tok.lex, "into") && f->kids[4]->kind == LOW_CST_ATOM && *nenv < TC_MAXENV) {
                    ty_t sv = tc_infer(c, f->kids[2], env, *nenv);
                    ty_t ev = tk(TK_UNKNOWN);
                    if (sv.k == TK_NAMED && sv.ebits) { ev = sv.eflt ? (ty_t){ .k = TK_FLOAT, .bits = sv.ebits } : tc_elem_int(sv.ebits, sv.esign); ev.nominal = sv.enom; }
                    env[*nenv].name = f->kids[4]->tok.lex; env[(*nenv)++].ty = ev;
                }
                for (proven_size_t j = 1; j + 1 < f->nkids; j++) {
                    const low_cst_t *pf = f->kids[j];
                    while (pf->kind == LOW_CST_GROUP && pf->nkids == 1) pf = pf->kids[0];
                    if (pf->kind != LOW_CST_FORM || pf->nkids < 4 || pf->kids[0]->kind != LOW_CST_ATOM || !veq(pf->kids[0]->tok.lex, "pop")) continue;
                    for (proven_size_t q = 2; q + 1 < pf->nkids; q++)
                        if (pf->kids[q]->kind == LOW_CST_ATOM && veq(pf->kids[q]->tok.lex, "into") && pf->kids[q + 1]->kind == LOW_CST_ATOM && *nenv < TC_MAXENV) {
                            ty_t sv = tc_infer(c, pf->kids[1], env, *nenv);
                            ty_t ev = tk(TK_UNKNOWN);
                            if (sv.k == TK_NAMED && sv.ebits) { ev = sv.eflt ? (ty_t){ .k = TK_FLOAT, .bits = sv.ebits } : tc_elem_int(sv.ebits, sv.esign); ev.nominal = sv.enom; }
                            env[*nenv].name = pf->kids[q + 1]->tok.lex; env[(*nenv)++].ty = ev;
                        }
                }
            }
            // recurse into nested blocks (if/while/for bodies); also type the call itself
            (void)tc_infer(c, f, env, *nenv);
            for (proven_size_t j = 0; j < f->nkids; j++)
                if (f->kids[j]->kind == LOW_CST_BLOCK) tc_check_body(c, f->kids[j], env, nenv, ret);
            // ★★★ RFC-0148 S4 (2026-10-14) — **`else if` 는 블록이 아니라 안쪽 `if` 폼이다**(low_cst.c — `else` 뒤 `if` 는 문장 하나로 붙는다).
            //   위 줄은 블록 자식만 내려가서, `if … end else if … end else …` 의 **둘째 갈래부터 통째로** 검사 밖이었다 — 조건도, 몸도,
            //   맨 끝 `else` 의 몸도(`hash_legacy` 의 md5 · sha1 라운드 셋이 그 자리에 있었다). 안쪽 폼을 문장 하나짜리 블록으로 본다.
            if (kw == LOW_KW_IF)
                for (proven_size_t j = 1; j < f->nkids; j++) {
                    const low_cst_t *ef = f->kids[j];
                    if (ef->kind != LOW_CST_FORM || !ef->nkids || ef->kids[0]->kind != LOW_CST_ATOM || ef->kids[0]->tok.kw != LOW_KW_IF) continue;
                    low_cst_t *one[1] = { (low_cst_t *)ef };
                    low_cst_t blk = *ef; blk.kind = LOW_CST_BLOCK; blk.kids = one; blk.nkids = 1;
                    tc_check_body(c, &blk, env, nenv, ret);
                }
            // ★★★ 2026-09-25 — **guard 의 else 는 BLOCK 이 아니라 FORM 이다**(머리가 `else`, low_cst.c ①).
            //   위 줄은 BLOCK 자식만 내려가서 guard 의 else 가 **통째로 검사 밖**이었다:
            //   `result` op 의 `guard … else return 0 .` 이 `--check` 초록이고 실행하면 죽었고
            //   (known-defects/bare-return-under-result.md), `else do let z be u8 300 . … end` 조차 통과했다.
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
    tc_notes_reset();
    low_ir_options_preload(pr);   // ★ RFC-0154 §8-21
    low_ctfe_calls_prepare(work, pr);   // ★ RFC-0015 §4.1 B — `comptime` 자리의 `fn` 을 먼저 돌려 답을 잰다(노트를 비운 뒤라 미리 짓기가 옛 노트를 읽지 않는다) — `config <이름>` 의 값을 잰다(아래 · `comptime` 접기)
    g_nvar = 0; g_nvarf = 0; g_nopt = 0; g_nmc = 0; g_nsname = 0; g_nename_t = 0; g_naname_t = 0; g_tc_full = false;
    for (proven_size_t i = 0; i < pr->nforms; i++) {
        const low_cst_t *f = pr->forms[i];
        if (f->kind == LOW_CST_FORM && f->nkids >= 2 && f->kids[0]->kind == LOW_CST_ATOM && f->kids[0]->tok.kw == LOW_KW_STRUCT &&
            f->kids[1]->kind == LOW_CST_ATOM) { if (g_nsname >= 1024) { g_tc_full = true; break; } g_sname[g_nsname++] = f->kids[1]->tok.lex; }
        if (f->kind == LOW_CST_FORM && f->nkids >= 2 && f->kids[0]->kind == LOW_CST_ATOM && f->kids[0]->tok.kw == LOW_KW_ENUM &&
            f->kids[1]->kind == LOW_CST_ATOM) { if (g_nename_t >= 1024) { g_tc_full = true; break; } g_ename_t[g_nename_t++] = f->kids[1]->tok.lex; }
        if (f->kind == LOW_CST_FORM && f->nkids >= 2 && f->kids[0]->kind == LOW_CST_ATOM && f->kids[0]->tok.kw == LOW_KW_ACTOR &&
            f->kids[1]->kind == LOW_CST_ATOM) { if (g_naname_t >= 512) { g_tc_full = true; break; } g_aname_t[g_naname_t++] = f->kids[1]->tok.lex; }
    }
    {
        proven_u8str_view_t cm = { 0 };
        for (proven_size_t i = 0; i < pr->nforms; i++) {
            const low_cst_t *f = pr->forms[i];
            if (f->kind != LOW_CST_FORM || f->nkids < 3 || f->kids[0]->kind != LOW_CST_ATOM) continue;
            if (f->kids[0]->tok.kw == LOW_KW_MODULE && f->kids[1]->kind == LOW_CST_ATOM) { cm = f->kids[1]->tok.lex; continue; }
            if ((f->kids[0]->tok.kw != LOW_KW_LET && f->kids[0]->tok.kw != LOW_KW_VAR) || f->kids[1]->kind != LOW_CST_ATOM) continue;
            proven_size_t be = kw_index(f, LOW_KW_BE);
            ty_t d = ty_of_decl_r(f, 2, be < f->nkids ? be : f->nkids, NULL);
            if (d.k == TK_UNKNOWN) continue;
            d.is_mut = false;
            if (g_nmc >= 512) { g_tc_full = true; break; }
            const bool is_let = f->kids[0]->tok.kw == LOW_KW_LET && be + 1 < f->nkids;
            g_mc[g_nmc++] = (tc_mconst_t){ f->kids[1]->tok.lex, cm, d, .uns = d.k == TK_INT && !d.sign,
                                           .vk = is_let ? f->kids + be + 1 : NULL, .vn = is_let ? f->nkids - be - 1 : 0 };
        }
        // ★ RFC-0154 §8-19 (나) — 모듈 상수의 값: 리터럴이거나 `comptime <식>`(다른 모듈 상수를 읽어도 된다 · 순서는 묻지 않는다 — 하강층과 같다).
        //   한 바퀴에 하나도 못 접으면 남은 것은 값을 모르는 채 둔다(하강층이 E-CONST-NOTCOMPTIME 으로 거절한다).
        for (bool prog = true; prog; ) {
            prog = false;
            for (proven_size_t q = 0; q < g_nmc; q++) {
                tc_mconst_t *m = &g_mc[q];
                if (m->known || !m->vk) continue;
                const bool lit1 = m->vn == 1 && m->vk[0]->kind == LOW_CST_ATOM && m->vk[0]->tok.kind == LOW_TOK_NUMBER;
                const bool ct2 = m->vn == 2 && m->vk[0]->kind == LOW_CST_ATOM && veq(m->vk[0]->tok.lex, "comptime");
                if (!lit1 && !ct2) { m->vk = NULL; continue; }
                tc_ctfe_ud_t ud = { NULL, 0, m->mod };
                const low_ctfe_names_t nm = { tc_ctfe_find, &ud };
                proven_i64 v; bool u;
                if (low_ctfe_let_value(m->vk, m->vn, &nm, &v, &u)) { m->known = true; m->v = v; prog = true; }
            }
        }
    }
    for (proven_size_t i = 0; i < pr->nforms; i++) {   // `build option <이름> <bool|int|choice> …`
        const low_cst_t *f = pr->forms[i];
        if (f->kind != LOW_CST_FORM || f->nkids < 4 || f->kids[0]->kind != LOW_CST_ATOM || !veq(f->kids[0]->tok.lex, "build") ||
            f->kids[1]->kind != LOW_CST_ATOM || !veq(f->kids[1]->tok.lex, "option") || f->kids[2]->kind != LOW_CST_ATOM || f->kids[3]->kind != LOW_CST_ATOM) continue;
        if (g_nopt >= 128) { g_tc_full = true; break; }
        g_opt[g_nopt++] = (tc_opt_t){ f->kids[2]->tok.lex, veq(f->kids[3]->tok.lex, "bool") };
    }
    for (proven_size_t i = 0; i < pr->nforms; i++) {
        const low_cst_t *f = pr->forms[i];
        if (f->kind != LOW_CST_FORM || f->nkids < 3 || f->kids[0]->kind != LOW_CST_ATOM || f->kids[0]->tok.kw != LOW_KW_ENUM ||
            f->kids[1]->kind != LOW_CST_ATOM) continue;
        const low_cst_t *blk = f->kids[f->nkids - 1];
        if (blk->kind != LOW_CST_BLOCK) continue;
        for (proven_size_t j = 0; j < blk->nkids; j++) {
            const low_cst_t *vf = blk->kids[j];
            if (vf->kind != LOW_CST_FORM || !vf->nkids || vf->kids[0]->kind != LOW_CST_ATOM) continue;
            if (g_nvar >= 1024) { g_tc_full = true; break; }
            tc_variant_t *v = &g_var[g_nvar++];
            v->en = f->kids[1]->tok.lex; v->vn = vf->kids[0]->tok.lex; v->np = 0;
            if (vf->nkids > 2 + 2 * 8) g_tc_full = true;   // 알맹이 칸이 여덟을 넘는다
            for (proven_size_t t = 2; t < vf->nkids && v->np < 8; t += 2) {
                if (vf->kids[t]->kind != LOW_CST_ATOM) { v->pt[v->np++] = tk(TK_UNKNOWN); continue; }
                ty_t pt = ty_of_word(vf->kids[t]->tok.lex);
                if (pt.k == TK_NAMED) pt.nname = vf->kids[t]->tok.lex;
                if (vf->kids[t - 1]->kind == LOW_CST_ATOM) {
                    if (g_nvarf >= 4096) g_tc_full = true;
                    else g_varf[g_nvarf++] = (tc_varf_t){ v, vf->kids[t - 1]->tok.lex, v->np };
                }
                v->pt[v->np++] = pt;
            }
        }
    }
    g_nal = 0;
    for (proven_size_t i = 0; i < pr->nforms; i++) {
        const low_cst_t *f = pr->forms[i];
        if (f->kind != LOW_CST_FORM || f->nkids < 3 || f->kids[0]->kind != LOW_CST_ATOM || f->kids[0]->tok.kw != LOW_KW_USE ||
            f->kids[1]->kind != LOW_CST_ATOM) continue;
        for (proven_size_t j = 2; j + 1 < f->nkids; j++)
            if (f->kids[j]->kind == LOW_CST_ATOM && veq(f->kids[j]->tok.lex, "as") && f->kids[j + 1]->kind == LOW_CST_ATOM) {
                if (g_nal >= 64) { g_tc_full = true; break; }
                g_al_old[g_nal] = f->kids[1]->tok.lex; g_al_new[g_nal++] = f->kids[j + 1]->tok.lex; break;
            }
        // `use <이름> from "<길>" .` — 이름이 그 파일의 모듈 이름과 다르면 별칭이다(모듈 이름 = 파일 이름)
        if (g_nal >= 64) g_tc_full = true;
        if (g_nal < 64 && f->nkids >= 4 && f->kids[2]->kind == LOW_CST_ATOM && veq(f->kids[2]->tok.lex, "from") && f->kids[3]->kind == LOW_CST_ATOM &&
            f->kids[3]->tok.kind == LOW_TOK_STRING) {
            proven_u8str_view_t pth = f->kids[3]->tok.lex; proven_size_t b0 = 0, e0 = pth.size;
            for (proven_size_t q = 0; q < pth.size; q++) if (pth.ptr[q] == (proven_u8)'/') b0 = q + 1;
            if (e0 > b0 + 4 && memcmp(pth.ptr + e0 - 4, ".low", 4) == 0) e0 -= 4;
            proven_u8str_view_t mn = { pth.ptr + b0, e0 - b0 };
            if (!proven_u8str_view_eq(mn, f->kids[1]->tok.lex)) { g_al_old[g_nal] = mn; g_al_new[g_nal++] = f->kids[1]->tok.lex; }
        }
    }
    low_typecheck_result_t out = { .ok = true };
    proven_result_array_t da = PROVEN_ARRAY_INIT(work, low_diag_t, 8);
    if (da.err != PROVEN_OK) { out.ok = false; return out; }
    out.diags = da.value;

    tc_ctx_t c = { .nsigs = 0, .nstrs = 0, .diags = &out.diags, .ok = &out.ok };
    if (g_tc_full) tc_emit(&c, "E-TYPE-LIMIT", "the checker's tables for enum variants, module constants, build options, struct names or module aliases are full — "
                            "this unit is refused rather than checked in part (RFC-0148 S5)", 1);
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
                g_nts[g_nnts].target = f->kids[2]->tok.lex;   // RFC-0148 S5 — 칸을 읽을 때 핵의 구조체를 찾는다
                g_nts[g_nnts].nominal = (proven_u8)(2 + TC_MAXSIG + g_nnts);   // int 계열과 겹치지 않는 대역
                g_nnts++;
            }
            if (core.k == TK_INT || core.k == TK_FLOAT) {   // ★ int·float 코어 둘 다 명목 정체성을 준다
                g_nt[g_nnt].name = f->kids[1]->tok.lex;
                g_nt[g_nnt].bits = core.bits; g_nt[g_nnt].sign = core.sign; g_nt[g_nnt].flt = (core.k == TK_FLOAT);
                g_nt[g_nnt].nominal = (proven_u8)(2 + g_nnt);   // 0=plain · 1=usize/idiff · ≥2=newtype
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
                   "(`let x u8 1 .`); a type declaration names a TYPE. One meaning, one spelling",
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
    proven_u8str_view_t smod = { 0 };
    for (proven_size_t i = 0; i < pr->nforms && c.nstrs < TC_MAXSIG; i++) {
        const low_cst_t *f = pr->forms[i]; c.curform = f;
        if (f->kind == LOW_CST_FORM && f->nkids >= 2 && f->kids[0]->kind == LOW_CST_ATOM && f->kids[0]->tok.kw == LOW_KW_MODULE &&
            f->kids[1]->kind == LOW_CST_ATOM) smod = f->kids[1]->tok.lex;
        if (f->kind != LOW_CST_FORM || f->nkids < 3 || f->kids[0]->kind != LOW_CST_ATOM) continue;
        if (f->kids[0]->tok.kw != LOW_KW_STRUCT) continue;
        if (f->kids[1]->kind != LOW_CST_ATOM) continue;
        const low_cst_t *blk = f->kids[f->nkids - 1];
        if (blk->kind != LOW_CST_BLOCK) continue;
        tc_struct_t *st = &c.strs[c.nstrs++];
        st->name = f->kids[1]->tok.lex; st->nf = 0; st->mod = smod;
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
            // ★ 갈래의 번호(소유자 2026-10-15 «가에 11 로 하면 나는 자동으로 12» · 철자 `ga 11 .`) — 이름 뒤의 정수 하나.
            //   값을 지닌 갈래가 있는 열거에는 적지 않고, 적힌 번호는 앞 갈래의 번호보다 커야 한다(정본 §6.2.7 (2c)). 검사는 아래 pass 에서 열거마다 한 번.
            if (vf->nkids == 2 && vf->kids[1]->kind == LOW_CST_ATOM && vf->kids[1]->tok.kind == LOW_TOK_NUMBER) continue;
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
        // 번호: 적지 않은 갈래는 앞 번호 + 1, 첫 갈래는 1(소유자 «열거는 무조건 1부터»). 적힌 번호는 정수이고 앞보다 커야 한다.
        {
            bool payload = false, numbered = false;
            for (proven_size_t j = 0; j < blk->nkids; j++) {
                const low_cst_t *vf = blk->kids[j];
                if (vf->kind != LOW_CST_FORM || vf->nkids < 1 || vf->kids[0]->kind != LOW_CST_ATOM || vf->kids[0]->tok.kw == LOW_KW_SATISFIES) continue;
                if (vf->nkids == 2 && vf->kids[1]->kind == LOW_CST_ATOM && vf->kids[1]->tok.kind == LOW_TOK_NUMBER) numbered = true;
                else if (vf->nkids >= 3) payload = true;
            }
            bool have = false; proven_i64 prev = 0;
            for (proven_size_t j = 0; j < blk->nkids && numbered; j++) {
                const low_cst_t *vf = blk->kids[j];
                if (vf->kind != LOW_CST_FORM || vf->nkids < 1 || vf->kids[0]->kind != LOW_CST_ATOM || vf->kids[0]->tok.kw == LOW_KW_SATISFIES) continue;
                proven_i64 num = have ? prev + 1 : 1;
                if (vf->nkids == 2 && vf->kids[1]->kind == LOW_CST_ATOM && vf->kids[1]->tok.kind == LOW_TOK_NUMBER) {
                    proven_u64 u = 0; bool neg = false;
                    proven_u32 ln = vf->kids[1]->tok.line ? vf->kids[1]->tok.line : vf->line;
                    if (payload) {
                        tc_emit(&c, "E-ENUM-NUMBER", "a variant number is written only in an enum whose variants carry nothing — "
                                "a number does not say what a value-carrying variant holds (canon 6.2.7 (2c))", ln);
                        break;
                    }
                    if (low_num_is_float(vf->kids[1]->tok.lex) || !num_value(vf->kids[1]->tok.lex, &u, &neg) ||
                        (!neg && u > (proven_u64)INT64_MAX) || (neg && u > (proven_u64)INT64_MAX)) {
                        tc_emit(&c, "E-ENUM-NUMBER", "a variant number is an integer (canon 6.2.7 (2c))", ln);
                        break;
                    }
                    proven_i64 v = neg ? -(proven_i64)u : (proven_i64)u;
                    if (have && v <= prev) {
                        tc_emit(&c, "E-ENUM-NUMBER", "variant numbers rise in declaration order — this number is not larger than the "
                                "variant before it (written or counted on from the one before). Declaration order is number order, "
                                "so `lt` / `ge` between variants and the numbers `cast` reads agree (canon 6.2.7 (2c))", ln);
                        break;
                    }
                    num = v;
                }
                prev = num; have = true;
            }
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
    // ★ RFC-0150 §8-22 (2026-10-16) — **별칭을 거친 자기 참조.** `type kid node .` 과 칸 `k option kid .` ·
    //   `type kid option (option node) .` 과 칸 `k kid .` · `type kid ref node .` 과 칸 `k kid .` 가 모두 초록이었다 — 아래 걸음이
    //   구조체 이름만 맞대고 별칭을 풀지 않았다. 정본 §6.2.7 (3) «다른 타입을 거쳐서든» 이 이미 답을 정했다.
    //   ⇒ 칸의 타입 낱말에 별칭(`type` · `newtype`)의 이름이 있으면 그 별칭의 낱말을 덧붙인다(한 별칭은 한 번만 — 별칭끼리의
    //     순환은 위의 E-TYPE-CYCLE 이 본다).
    const low_cst_t *alf[TC_MAXSIG]; proven_size_t nal = 0;
    for (proven_size_t i = 0; i < pr->nforms && nal < TC_MAXSIG; i++) {
        const low_cst_t *f = pr->forms[i];
        if (f->kind != LOW_CST_FORM || f->nkids < 3 || f->kids[0]->kind != LOW_CST_ATOM || f->kids[1]->kind != LOW_CST_ATOM) continue;
        if (f->kids[0]->tok.kw != LOW_KW_TYPE && f->kids[0]->tok.kw != LOW_KW_NEWTYPE) continue;
        alf[nal++] = f;
    }
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
                proven_u8str_view_t tw[64]; proven_size_t ntw = 0; bool tw_over = false;
                for (proven_size_t w = 1; w < fd->nkids; w++) tc_type_words(fd->kids[w], tw, &ntw, 64, &tw_over);
                {   // 별칭을 푼다(위 §8-22) — 덧붙인 낱말도 다시 본다(별칭이 별칭을 가리킬 수 있다)
                    bool used[TC_MAXSIG];
                    for (proven_size_t z = 0; z < nal; z++) used[z] = false;
                    for (proven_size_t w = 0; w < ntw && !tw_over; w++)
                        for (proven_size_t a2 = 0; a2 < nal; a2++) {
                            if (used[a2] || !proven_u8str_view_eq(alf[a2]->kids[1]->tok.lex, tw[w])) continue;
                            used[a2] = true;
                            for (proven_size_t q = 2; q < alf[a2]->nkids; q++) tc_type_words(alf[a2]->kids[q], tw, &ntw, 64, &tw_over);
                            break;
                        }
                }
                if (tw_over) { c.cycle_overflow = true; break; }     // 다 못 모았으면 «순환 없다» 의 근거가 없다
                // 이 필드의 타입 낱말에 `ref`/`mut_ref` 가 있으면 **간접 간선**이다.
                bool fref = false;
                for (proven_size_t w = 0; w < ntw; w++)
                    if (proven_u8str_view_eq(tw[w], proven_u8str_view_from_cstr("ref")) ||
                        proven_u8str_view_eq(tw[w], proven_u8str_view_from_cstr("mut_ref")))
                        { fref = true; break; }
                // 타입 낱말은 여럿일 수 있다(`option node` · `ref node` · `array node 4` · `option (option node)`).
                // 그 **어느 자리에든** struct 이름이 있으면 그 struct 를 품는다는 뜻이다.
                // ★★ RFC-0150 §8-12 (2026-10-24) — **스스로를 품는 마디** `left option (owned node) .`: 이 칸은 값이 아니라 할당기 바이트에
                //   지은 마디를 가리키는 **주소 한 칸**이다(없으면 0). 그래서 크기가 유한하다 — 순환이 아니다. 낱말이 꼭 이 셋일 때만
                //   (`option` · `owned` · 자기 이름) 이고, 다른 구조체를 거쳐 돌아오는 소유 순환은 여전히 E-STRUCT-CYCLE 이다.
                //   괄호 꼴(`option (owned node)`)은 묶음의 머리 낱말이 한 번 더 실린다 — [option][owned][owned][node].
                proven_u8str_view_t ow_ = proven_u8str_view_from_cstr("owned");
                bool fbox = cur == si && proven_u8str_view_eq(tw[0], proven_u8str_view_from_cstr("option")) &&
                            ((ntw == 3 && proven_u8str_view_eq(tw[1], ow_) && proven_u8str_view_eq(tw[2], c.strs[si].name)) ||
                             (ntw == 4 && proven_u8str_view_eq(tw[1], ow_) && proven_u8str_view_eq(tw[2], ow_) && proven_u8str_view_eq(tw[3], c.strs[si].name)));
                if (fbox) continue;
                for (proven_size_t w = 0; w < ntw && !cyc; w++) {
                    for (proven_size_t k = 0; k < c.nstrs; k++) {
                        if (!proven_u8str_view_eq(c.strs[k].name, tw[w])) continue;
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
    // RFC-0148 S4 — 액터의 proc 서명을 모은다(위 `tc_asig_t`). 표가 차면 그 뒤의 `send` 는 «모름» 으로 남는다 — 재지 않을 뿐 틀리게 재지 않는다.
    g_nasig = 0;
    {
        proven_u8str_view_t amod = { 0 };
        for (proven_size_t i = 0; i < pr->nforms; i++) {
            const low_cst_t *f = pr->forms[i];
            if (f->kind != LOW_CST_FORM || f->nkids < 3 || f->kids[0]->kind != LOW_CST_ATOM) continue;
            if (f->kids[0]->tok.kw == LOW_KW_MODULE && f->kids[1]->kind == LOW_CST_ATOM) { amod = f->kids[1]->tok.lex; continue; }
            if (f->kids[0]->tok.kw != LOW_KW_ACTOR || f->kids[1]->kind != LOW_CST_ATOM || f->kids[f->nkids - 1]->kind != LOW_CST_BLOCK) continue;
            const low_cst_t *blk = f->kids[f->nkids - 1];
            for (proven_size_t j = 0; j < blk->nkids && g_nasig < TC_MAXSIG; j++) {
                const low_cst_t *op = blk->kids[j];
                if (op->kind != LOW_CST_FORM || op->nkids < 3 || op->kids[0]->kind != LOW_CST_ATOM) continue;
                if (op->kids[0]->tok.kw != LOW_KW_PROC && op->kids[0]->tok.kw != LOW_KW_FN) continue;
                low_op_header_t hh = low_op_header(op);
                tc_asig_t *a = &g_asig[g_nasig++];
                *a = (tc_asig_t){ .actor = f->kids[1]->tok.lex, .mod = amod, .proc = hh.name, .ret = tk(TK_UNKNOWN) };
                for (proven_size_t q = 0; q < hh.np && a->nparams < TC_MAXP; q++) {
                    rng_res_t rr;
                    a->params[a->nparams++] = ty_of_decl_r(op, hh.p[q].ts, hh.p[q].te, &rr);
                }
                if (hh.out_e > hh.out_s) { rng_res_t rr; a->ret = ty_of_decl_r(op, hh.out_s, hh.out_e, &rr); }
            }
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
        // ★★★ RFC-0148 S4 (2026-10-14) — **계약 절의 조건도 식이다.** `requires` · `ensures` · `errors <변형>` 뒤의 조건은 어디서도
        //   타입 검사를 받지 않아 `requires lt add r. i. . 3 .`(u64 + usize)이 섰다. 본문과 같은 이름(파라미터)으로 잰다.
        //   `ensures` 의 `result` 같은 이름은 모르는 이름이라 «모름» 으로 지나간다(모르는 것은 물지 않는다).
        if (c.sigs[i].form) {
            const low_cst_t *of = c.sigs[i].form;
            for (proven_size_t j = 2; j < of->nkids; j++) {
                const low_cst_t *kj = of->kids[j];
                if (kj->kind != LOW_CST_ATOM || !(veq(kj->tok.lex, "requires") || veq(kj->tok.lex, "ensures") || veq(kj->tok.lex, "errors"))) continue;
                proven_size_t vs = j + 1, e = j + 1;
                while (e < of->nkids && of->kids[e]->kind != LOW_CST_BLOCK &&
                       !(of->kids[e]->kind == LOW_CST_ATOM && low_is_clause_word(of->kids[e]->tok.lex))) e++;
                if (vs < e && of->kids[vs]->kind == LOW_CST_ATOM &&
                    (veq(of->kids[vs]->tok.lex, "static") || veq(of->kids[vs]->tok.lex, "debug") || veq(of->kids[vs]->tok.lex, "assume"))) vs++;
                if (veq(kj->tok.lex, "errors") && vs < e && of->kids[vs]->kind == LOW_CST_ATOM) vs++;   // 변형 이름
                // `ensures` 는 결과를 `ret` 으로 가리킨다(정본 §6.4 (…)) — 그 이름은 op 의 출력 타입이다
                proven_size_t nsave = nenv;
                if (veq(kj->tok.lex, "ensures")) {
                    if (nenv >= TC_MAXENV) tc_emit(&c, "E-TYPE-LIMIT", "too many names in scope to check this `ensures` (the checker's table is full) — refused, not skipped", of->line);
                    else { env[nenv].name = (proven_u8str_view_t){ (const proven_byte_t *)"ret", 3 }; env[nenv++].ty = c.sigs[i].ret; }
                }
                if (vs < e) (void)tc_infer_run(&c, of->kids, vs, e - vs, env, nenv);
                nenv = nsave;
            }
        }
        tc_check_body(&c, c.sigs[i].body, env, &nenv, c.sigs[i].ret);
    }

    // pass 3: ★ 2026-09-27 (X-0071 실측) — **`test` 블록의 몸은 타입 검사를 한 번도 받지 않았다.** 위 두 패스가
    //   `fn`/`proc` 만 돌아서, 시험 안의 `let x be u8 300 .` 도 `expect 1 .` 도 `check: ok` 였다. 시험은 입력이 없는
    //   몸이므로 빈 환경에서 같은 규칙으로 걷는다.
    {
        proven_u8str_view_t tmod = { 0 };
        for (proven_size_t i = 0; i < pr->nforms; i++) {
            const low_cst_t *f = pr->forms[i];
            if (f->kind != LOW_CST_FORM || f->nkids < 2 || f->kids[0]->kind != LOW_CST_ATOM) continue;
            low_kw_t kw = f->kids[0]->tok.kw;
            if (kw == LOW_KW_MODULE && f->kids[1]->kind == LOW_CST_ATOM) { tmod = f->kids[1]->tok.lex; continue; }
            // ★★★ RFC-0148 S4 (2026-10-14) — **모듈 맨 위의 `let` 도 선언이다.** 어느 패스도 그것을 걷지 않아 `let BIG u8 300 .` 도,
            //   `let LIMIT slice u8 10 .` 도 `check: ok` 였다. 빈 환경에서 문장 하나짜리 몸으로 같은 규칙을 건다.
            if (kw == LOW_KW_LET || kw == LOW_KW_VAR) {
                low_cst_t *one[1] = { (low_cst_t *)f };
                low_cst_t blk = *f; blk.kind = LOW_CST_BLOCK; blk.kids = one; blk.nkids = 1;
                tc_var_t env[TC_MAXENV];
                proven_size_t nenv = 0;
                c.curmod = tmod; c.curform = f;
                tc_check_body(&c, &blk, env, &nenv, tk(TK_UNKNOWN));
                continue;
            }
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
    //   `let v be bool add n 1 .` 이 actor 안에서는 초록이었다(check-decl-types 가 lib/alloc.low 의 핸들러 자리를
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
