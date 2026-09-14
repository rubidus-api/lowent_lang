// low_cst.h — L1 point-closure CST (concrete syntax tree).
//
// The implementation commits to ONE concrete resolution of the rev.d point-closure
// surface (the residual §3.2 ambiguity is settled here, in code, per DECISION-0005):
//
//   module  = form*
//   form    = operand+ closer                 (head = first operand; rest = args, flat)
//   closer  = "."  |  ","  |  <trailing do…end supplies its own "end">
//   operand = access
//   access  = primary ( ("to"|"in") primary )*        (tight; forward/reverse, §12.D)
//   primary = ATOM | "(" form ")" | headed-block | bare-block
//   headed-block = ATOM "do" form* "end"      (ATOM heads a block form, end closes it)
//   bare-block   = "do" form* "end"
//   ATOM    = IDENT | NUMBER | STRING | HEREDOC | true|false|none|unit
//
// Nesting is EXPLICIT — via parens or do…end (no bare-dot arity bracketing; that is
// the middle-ground/full front-end, not the mini pure reader). A statement therefore
// carries exactly one closer (tail-merge): `let x add a b .` = let(x, add(a,b)), one
// dot; nested calls use parens: `let x add a (mul b c) .`. Flat operand runs are kept
// flat here; the evaluator (S1) interprets head+args with the vocabulary Γ.
#ifndef LOW_CST_H
#define LOW_CST_H

#include "proven/types.h"
#include "proven/array.h"
#include "low_token.h"
#include "low_diag.h"

typedef enum {
    LOW_CST_ATOM,    // leaf: .tok is the atom token
    LOW_CST_FORM,    // head + args: .tok = head token, kids = args, .closer set
    LOW_CST_BLOCK,   // do … end: kids = inner forms
    LOW_CST_GROUP,   // ( form ): kids[0] = the wrapped form
    LOW_CST_ACCESS,  // a to b in c …: kids = primaries, .ops = link operators (to/in)
    LOW_CST_ERROR,   // malformed span
} low_cst_kind_t;

typedef struct low_cst low_cst_t;
struct low_cst {
    low_cst_kind_t kind;
    low_token_t    tok;      // ATOM: the atom; FORM: head token; ACCESS/BLOCK/GROUP: first token
    low_cst_t    **kids;
    proven_size_t  nkids;
    low_kw_t      *ops;      // ACCESS only: nkids-1 link operators (LOW_KW_TO / LOW_KW_IN)
    low_tok_kind_t closer;   // FORM only: LOW_TOK_DOT | LOW_TOK_EOF(=end/implicit)
    // ★★★ 수식자는 **껍질을 벗기되 표시는 남긴다.** 전엔 껍질을 통째로 버려서 소비자가
    //   `export` 를 **볼 수 없었고**, 그래서 가시성이 **강제될 수 없었다** — `comptime` 과
    //   똑같은 병이다: **지어질 수 없게 만들어 놓고 "아직 못 한다" 고 말했다.**
    // ★★★★ **한정을 지우면서 어디를 가리켰는지도 지웠다** (알려진 결함 qualifier-erased-flat-lookup,
    //   2026-08-11 발견 → 2026-08-14 고침). `ck_narrow_qual` 이 `M.member` 를 bare `member` 로
    //   좁히는데(RFC-0011 — 뒤의 검증기들이 bare 를 본다), 그러면 **어느 모듈의 member 였는지**가
    //   사라진다. 이름이 겹치는 두 모듈이 한 단위에 있으면 뒤끝의 평평한 조회가 **남의 시그니처**를
    //   집었고, 그래서 **파일 순서만 바꿔도 같은 프로그램이 초록/빨강으로 갈렸다**(실측).
    //   ⇒ 좁히되 **가리킨 모듈을 여기 남긴다.** 비어 있으면 bare 호출이다(전과 같다).
    proven_u8str_view_t qual_mod;
    bool           is_export;   // `export <op>` 였는가 (모듈 밖에서 보인다)
    bool           is_unsafe;   // `unsafe <op>` 였는가 (unsafe 효과를 쓸 자격)
    // ★★★ **`extern` = 몸이 C 에 있다** (RFC-0063). 본문이 있으면 오류다 — 몸이 둘일 수는 없다.
    bool           is_extern;
    // ★★★ **`unsafe target <iset>`** (RFC-0040 D5) — target 전용 intrinsic 격리 블록. iset 이 비어 있지
    //   않으면 이 op 은 target-게이트다: 그 명령셋을 주는 target 에서만 컴파일된다(아니면 E-TARGET-INTRIN).
    //   `target` 은 **문맥 낱말**(task_group 처럼 — 어휘 0 증가). intrinsic 몸통은 아직 없다(스캐폴드·격리만).
    proven_u8str_view_t target_iset;
    bool           synth;    // ★ 정규화가 **만든** 괄호인가 (저자가 쓴 괄호와 구별한다).
                             //   이것이 있어야 `low_flat_kids` 가 **평평한 입력에 항등**임을
                             //   보장할 수 있다 — 저자의 괄호는 건드리지 않는다.
    proven_u32     line, col;
    // ★★★ **어느 파일에서 왔는가** (2026-08-06 · 단계 V).
    //
    //   한 컴파일 단위는 여러 파일이다(`use … from "…"` 가 딸려 온다). 그런데 각 파일은
    //   **따로 렉스·파싱되고 폼만 이어 붙는다** — 즉 **줄 번호가 파일마다 1 부터 다시 시작**
    //   하는데 진단은 줄만 실었다. 그래서 *"5:0 E-…"* 가 **어느 파일의 5 번 줄인지 알 수 없었다.**
    //   ⇒ 이 부족이 조사를 세 번 늦췄다(utf8/utf16 충돌 · pool/spsc · 단계 U 슬라이스 ③).
    //   ★ 정적 문자열(argv 의 경로)을 그대로 가리킨다 — 복사도 소유도 없다. NULL = 모름.
    const char    *file;
};

// ★★ **인스턴스가 어디서 만들어졌는가** (RFC-0084 §2.3) — 단형화의 오래된 병은 진단이
//   *"네가 쓰지도 않은 코드의 줄"* 을 가리키는 것이다(C++ 템플릿 오류가 악명 높은 이유).
//   틀의 줄은 여전히 옳다 — **모자란 것은 사용 자리**다. 그래서 둘 다 말한다.
#define LOW_MONO_MAXSITE 64
typedef struct {
    proven_u8str_view_t name;   // `vec#u32` 같은 인스턴스 이름
    proven_u32          line;   // 그것을 **부른/쓴** 줄
} low_mono_site_t;

typedef struct {
    low_cst_t   **forms;   // top-level forms
    proven_size_t nforms;
    proven_array_t diags;  // of low_diag_t
    bool ok;
    low_mono_site_t sites[LOW_MONO_MAXSITE];   // 단형화 유래(이름 → 사용 자리)
    proven_size_t   nsites;
    // ★★★★★ **버려진 틀** — 단형화는 *"틀 자신은 빠진다"* 로 폼 목록을 다시 쓴다.
    //   인스턴스가 하나라도 있으면 그 자리에 인스턴스가 들어가지만, **아무도
    //   부르지 않은 제네릭**은 아무것도 안 들어가 폼이 통째로 사라진다 — 그리고
    //   사라진 폼은 **검사도 안 받는다**(가림·이름·접근 규칙 전부). 안 보는 자리는
    //   통과하는 자리와 구별되지 않는다(2026-08-14 교훈의 재발).
    //   ⇒ 버린 틀을 여기 남겨, **타입을 몰라도 되는 검사**만 이것도 훑는다.
    //     (타입검사·IR 은 그대로 단형화된 목록만 본다 — 틀의 몸에는 `t` 처럼
    //      존재하지 않는 타입이 있어서, 그것들을 태우면 거짓 진단이 난다.)
    low_cst_t   **gforms;
    proven_size_t ngforms;
} low_parse_result_t;

// Parse the token stream into a CST. `node_alloc` should be arena-backed (nodes are
// bulk-freed); `work` (heap) backs growable temporaries, the diag array, and the
// top-level form list. Token views must outlive the result.
[[nodiscard]] low_parse_result_t low_parse(proven_allocator_t node_alloc,
                                           proven_allocator_t work,
                                           const proven_array_t *tokens);

// Print the CST as an indented tree (debug).
void low_cst_dump(const low_parse_result_t *pr);

// Re-emit the CST as canonical source (naked↔paren round-trip formatter).
void low_cst_fmt(const low_parse_result_t *pr);

// ★★★ **절(clause) 어휘는 하나다.** (RFC-0057 의 교훈을 어휘로 확장)
//
//   네 파일이 각자 목록을 갖고 있었고 — **갈렸다**:
//     low_check 12개 · low_typecheck 11 · low_contract 11 · low_ir 10
//   `satisfies` 를 셋이 몰랐다. 그래서 **앞 절의 스캔이 그것을 삼켰다**:
//
//       fn f … requires ge a 1 . satisfies t .   →   **E-REQ-UNDEF** (거짓 양성)
//       fn f satisfies t . … requires ge a 1 .   →   ok
//
//   ★ **절의 순서가 진단을 바꿨다.** 절은 **무순서**여야 한다(RFC-0008: spec = 무순서 요구사항 집합).
//   ⇒ 하나의 뜻에 네 개의 표현이 있으면, 그 넷은 **반드시 갈린다.** 하나로 둔다.
bool low_is_clause_word(proven_u8str_view_t v);

// ══ 정규화된 op 헤더 (DECISION-0015, 두 번째 조각) ═══════════════════════════
//
// ★★★ **op 의 머리를 읽는 코드가 여섯 벌 있었다.** low_check(15) · low_ir(17) ·
//   low_typecheck(4) · low_contract(4) — 각자 `input` 절을 자르고, 각자 한정자를 건너뛰고,
//   각자 타입 낱말의 범위를 정했다. **그리고 각자 조금씩 다르게 틀렸다:**
//     · actor 핸들러의 `input` 절이 **통째로 버려졌다** — `--check` 는 초록불이었고,
//       실행하면 **산술을 탓했다**(교훈 5: 오진).
//     · 관계형 `requires` 를 **강제·구간사실·오라클 세 곳이 서로 다르게** 읽었다.
//     · `input a mut slice u8 .` 의 `mut` 을 어떤 스캐너는 건너뛰고 어떤 스캐너는 안 건너뛰었다.
//
//   ⇒ **한 번만 읽는다.** 아래 구조체가 op 의 머리에 대한 **유일한 답**이다.
//   (`low_op_summary()` 가 효과에 대해 한 것 · `low_is_clause_word()` 가 절 어휘에 대해 한 것 —
//    같은 처방의 일반화다.)
typedef struct {
    proven_u8str_view_t name;      // 파라미터 이름
    // ★ **타입의 경계는 어디인가** — 이것을 소비자마다 다르게 정하고 있었다.
    //   `mut` 은 **타입의 일부다**(SPEC-002: 접근 한정자, type-core 에만). `owned T` 도 타입이다.
    //   `comptime` 은 **타입 밖이다** — 파라미터의 속성이지 타입이 아니다.
    //   ★ low_typecheck 는 이름 **한 낱말만** 건너뛰어서 `comptime` 을 타입에 섞었고,
    //     low_ir 은 벗겼다. **같은 절을 두고 두 소비자가 다른 타입을 봤다.**
    proven_size_t       ts, te;    // 타입 낱말 [ts, te) — **`mut`/`owned` 포함**, `comptime` 제외
    proven_size_t       core;      // 타입 **알맹이**의 시작 — `mut`/`owned` 도 벗긴 자리
    bool                is_comptime;
    bool                is_mut;
    bool                is_owned;
    bool                is_uptr;   // ★ `unsafe_ptr T` — C 로 넘길 **생 포인터**(RFC-0066 가족). unsafe.
} low_param_t;

// ★★★ **파라미터 한도의 원본은 여기 하나다** (WO-0204, 2026-09-12). CST 가 가장 이른 층이라
//   여기서 정하고, IR(`LOW_MAX_PARAMS`)·타입검사(`TC_MAXP`)·방출 C(`LW_MAXP`)가 **이 수를 가리킨다**.
//   전에는 같은 16 이 네 곳에 따로 살았다 — 한 곳만 낡으면 나머지는 낡은 줄도 모른다(교훈 7).
//   8 → 16 → 32. 32 가 천장인 이유는 파라미터 종류표가 32 비트 마스크이기 때문이다(low_ir.h 참조).
#define LOW_HDR_MAXP 32

typedef struct {
    const low_cst_t    *form;      // op form 자신
    proven_u8str_view_t name;      // op 이름
    bool                is_calc;   // fn → 순수를 선언한다
    low_param_t         p[LOW_HDR_MAXP];
    proven_size_t       np;
    bool                too_many;  // ★ 넘치면 **말한다**. 조용히 자르지 않는다.
    proven_size_t       out_s, out_e;   // `output` 절의 낱말 범위 (없으면 0,0)
    proven_size_t       eff_s, eff_e;   // `effects` 절의 낱말 범위 (없으면 0,0)
    bool                is_variadic; // ★ `variadic` 절 — 씨의 가변인자 함수 호출(RFC-0063 §5)
    // ★★★★ RFC-0112 D8 — `using <이름> <타입> .` (없으면 비어 있다). `low_using()` 이 단형화 앞에서 이 절을
    //   첫 실행 입력으로 바꿔 적으므로, 그 뒤의 소비자는 이것을 보지 않는다.
    proven_u8str_view_t using_name, using_type;
    proven_size_t       np_call;   // 부르는 쪽이 **적는** 인자 수(`using` 이 대신 채우는 것을 뺀다)
    const low_cst_t    *body;      // `do … end` 본체 (없으면 NULL)
} low_op_header_t;

// op form 하나에서 헤더를 읽는다. **여기가 유일한 답이다.**
low_op_header_t low_op_header(const low_cst_t *opform);
// ★★★★ WO-0217 — 머리 절의 차례. 입력이 아닌 절은 `low_clause_rank`, 입력은 `low_input_rank`(절의 [at, end)).
int low_clause_rank(proven_u8str_view_t word);
int low_input_rank(const low_cst_t *opform, proven_size_t at, proven_size_t end);

// ★★★★ RFC-0112 D8 — `using` 을 푼다: 부르는 자리에 얼로케이터 인자를 끼우고, 받는 쪽 절을 입력으로 바꾼다.
//   나무(`low_nest`) **뒤**, 단형화(`low_mono`) **앞**에 한 번 돈다.
void low_using(low_parse_result_t *pr, proven_allocator_t node_alloc, proven_allocator_t work);

// ★★★ arity 단일화 — 평평한 원자 열에 괄호를 박아 **진짜 나무**를 세운다(CST → CST).
//   파서는 무지한 채로 둔다(구조=문자열). 나무는 파스 **다음에** 선다.
//   불규칙한 머리를 만나면 **그 자리는 평평하게 남긴다** — 틀린 나무보다 없는 나무가 낫다.
//   `nested`/`gave_up` 으로 **얼마나 세웠고 얼마나 포기했는지** 정직하게 돌려준다.
// ★ 나무를 **원래의 평평한 열**로 되편다. 평평한 입력에는 **항등**이다.
//   나무 이전의 눈을 가진 스캐너를 위한 **단 하나의 창** — 다리이지 목적지가 아니다.
//   (이 창을 쓰는 자리 수가 곧 남은 빚이다.)
proven_size_t low_flat_kids(const low_cst_t *f, const low_cst_t **out, proven_size_t max);
// ★ 저자의 괄호까지 펴는 판 — 빌림 스캐너(E-EXCL)처럼 괄호에 뜻이 없는 소비자만 쓴다.
proven_size_t low_flat_kids_deep(const low_cst_t *f, const low_cst_t **out, proven_size_t max);

// ★ 마지막 정규화가 **무엇 때문에 포기했는지** — 이름과 횟수. 줄이려면 알아야 한다.
void low_nest_report(char *buf, proven_size_t cap);
// ★ 보고는 **파일**에 대해 한다 — 링크된 의존의 폼은 세지 않는다(나무는 단위 전체에 선다).
void low_nest_set_report_limit(proven_size_t n);

// ★★★ **제네릭 = comptime 타입 파라미터 + 단형화** (RFC-0021).
//   호출 자리마다 구체 op(`pick#rect`)을 만들고 본문의 타입 파라미터를 치환한다(CST → CST).
//   그러면 뒷단(검사·타입·IR)은 **제네릭을 아예 모른다** — 간접 호출 0, 계약 그대로 전파.
// ★ 이 op 이 **제네릭 틀**인가 (comptime 타입 파라미터, 또는 타입 자리에 쓰인 comptime 값 파라미터).
//   틀은 IR 로 낮추지 않는다 — 호출은 전부 인스턴스로 재작성되고, 틀의 몸은 지워진 파라미터를 참조한다.
bool low_is_generic_template(const low_cst_t *f);

void low_mono(low_parse_result_t *pr, proven_allocator_t node_alloc, proven_allocator_t work,
              proven_size_t *ninst, proven_size_t *nsub);

void low_nest(low_parse_result_t *pr, proven_allocator_t node_alloc, proven_allocator_t work,
              proven_size_t *nested, proven_size_t *gave_up);

// ★ 2026-09-14 — 폼이 덮는 마지막 줄(진단에 파일을 실을 때, 그 줄이 이 폼 안인지 본다)
static inline proven_u32 low_cst_last_line(const low_cst_t *n) {
    proven_u32 m = n ? n->line : 0;
    while (n) {
        if (n->line > m) m = n->line;
        if (n->kind == LOW_CST_ATOM || n->nkids == 0) { if (n->tok.line > m) m = n->tok.line; break; }
        n = n->kids[n->nkids - 1];
    }
    return m;
}
// 줄이 이 폼 안이면 그 폼의 파일, 아니면 NULL — **짐작해서 붙이지 않는다**
static inline const char *low_cst_file_for_line(const low_cst_t *f, proven_u32 line) {
    if (!f || !f->file || !line) return NULL;
    return (line >= f->line && line <= low_cst_last_line(f)) ? f->file : NULL;
}

#endif // LOW_CST_H
