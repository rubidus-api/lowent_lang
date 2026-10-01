// low_contract.c — MVP contract checks (S4): errors-closure + requires-resolve.
#include "low_hwm.h"
#include "low_contract.h"
#include <stdlib.h>

#include "low_token.h"
#include "low_diag.h"

#define CT_MAX 64

static bool veq(proven_u8str_view_t v, const char *s) { return proven_u8str_view_eq(v, proven_u8str_view_from_cstr(s)); }

// clause keyword = a boundary between an op's flat clauses
static bool is_clause_kw(proven_u8str_view_t v) { return low_is_clause_word(v); }  // ★ 하나의 어휘
// names known in a requires condition beyond params/declared ops
static bool is_known_pred(proven_u8str_view_t v) {
    static const char *k[] = { "ge","le","lt","gt","eq","ne","and","or","not","len","idx",
                               "add","sub","mul","div","mod","true","false","expr","deref",
                               // ★ 배열 내용 술어 (R5) — `requires elem_lt s N` (모든 원소 cmp N)
                               "elem_lt","elem_le","elem_gt","elem_ge",
                               "static","debug","assume" };  // contract grades (RFC-0008 §6.3)
    for (proven_size_t i = 0; i < sizeof k / sizeof k[0]; i++) if (veq(v, k[i])) return true;
    return false;
}

typedef struct {
    proven_array_t *diags;
    bool           *ok;
    const low_parse_result_t *pr;   // ★ `try` 가 부른 op 의 errors 절을 보러 간다
    const low_cst_t *self_enum;     // ★ 이 op 의 오류 enum 블록(절이 없을 때의 기준)
    bool            self_result;    // ★ 이 op 의 출력이 `result` 인가 (try 를 쓸 자격 — §6.5.8(2))
    const low_cst_t *cur_form;      // ★ 지금 보는 op — 진단에 **파일 이름**을 싣는다(결함 노트 #48)
    proven_u32      self_line;
    // per-op scratch
    proven_u8str_view_t declared[CT_MAX];  proven_size_t ndeclared;
    // ★★★★★ **여기 있던 고정 표를 없앴다** (2026-08-30, WO-0150 — 계측이 벼랑을 보여 줬다).
    //   `names` 는 *"이 계약이 쓸 수 있는 이름"* (제 파라미터 + 단위의 op 이름)이다.
    //   CT_MAX(64)로 고정돼 있었고, 넘치면 **판정을 통째로 접었다**(`names_truncated`) —
    //   조용히 틀린 고발 대신 **조용한 침묵**을 고른 완화였다. 그런데 실측하니 코퍼스의
    //   최고수위가 **정확히 64/64** 였다: 우리는 그 완화 **위에서 살고 있었다**.
    //   ⇒ 폼 수만큼 잡는다(op 하나에 이름 하나 — 정확한 상한). 그러면 잘림이 **없고**,
    //     "넘치면 어떻게 하나" 라는 물음도 함께 사라진다. *없애기가 후퇴보다 낫다.*
    proven_u8str_view_t *names;             proven_size_t nnames, namescap;
} ct_ctx_t;

static void ct_emit(ct_ctx_t *c, const char *code, const char *msg, proven_u32 line) {
    // ★ 한 단위가 여러 파일이고 줄 번호는 파일마다 1 부터 다시 시작한다 — 파일을 안 적으면
    //   읽는 사람이 어느 파일의 6 번 줄인지 모른다(결함 노트 #48, 2026-09-16).
    low_diag_t d = { .sev = LOW_SEV_ERROR, .code = code, .msg = msg, .line = line, .col = 0,
                     .file = low_cst_file_for_line(c->cur_form, line) };
    (void)proven_array_push(c->diags, &d);
    *c->ok = false;
}
static bool in_set(const proven_u8str_view_t *set, proven_size_t n, proven_u8str_view_t v) {
    for (proven_size_t i = 0; i < n; i++) if (proven_u8str_view_eq(set[i], v)) return true;
    return false;
}
static bool is_atom_word(const low_cst_t *nd, const char *s) {
    return nd->kind == LOW_CST_ATOM && veq(nd->tok.lex, s);
}

// ★★★ **`try` 로 흘러 들어오는 오류도 이 op 의 오류다** (RFC-0006 §8-2, 2026-08-01).
//
//   `errors` 절은 *"이 op 이 낼 수 있는 오류는 이것뿐"* 이라는 계약이다. 그런데 검사가
//   **본문이 직접 쓴 `return error X` 만** 보고 있었다 — `try p` 가 p 의 오류를 그대로
//   위로 올리는데, 그 오류는 아무도 대조하지 않았다.
//
//   실측(2026-08-01):
//       proc p output result u64 e1 . errors a1 . do return error a1 . end
//       proc q output result u64 e2 . errors b2 . do let v be u64 try p . return ok v . end
//       ⇒ `--check` 초록. 그리고 `q()` 는 **`err a1`** 을 냈다 — **다른 enum 의 변형**이,
//         자기 `errors` 절에 없는 이름이, 선언된 오류 타입 밖에서 나왔다.
//   ⇒ 절이 **거짓말**이었다. 계약이 거짓이면 그 위의 모든 것(진단·테스트 오라클·호출자의
//     망라 처리)이 거짓 전제 위에 선다.
//
//   규칙: **부른 op 의 선언된 오류는 부르는 op 의 선언된 오류의 부분집합이어야 한다.**
//   ☞ 이름 기준이면 충분하다 — 이 언어의 이름공간은 **평평**해서(E-NAME-DUP) 두 enum 이
//     같은 변형 이름을 가질 수 없다. 그래서 "이름 기준 vs 순서값 기준"(§8-5)이 한 답이다.
static bool ct_in_enum(const low_cst_t *blk, proven_u8str_view_t name);
static const low_cst_t *ct_err_enum(const low_parse_result_t *pr, const low_cst_t *f);
static proven_size_t ct_op_errors(const low_cst_t *g, proven_u8str_view_t *out, proven_size_t max) {
    proven_size_t n = 0;
    for (proven_size_t j = 2; j + 1 < g->nkids && n < max; j++) {
        if (g->kids[j]->kind != LOW_CST_ATOM || !veq(g->kids[j]->tok.lex, "errors")) continue;
        if (g->kids[j + 1]->kind != LOW_CST_ATOM) continue;
        proven_u8str_view_t en = g->kids[j + 1]->tok.lex;
        if (is_clause_kw(en) || veq(en, "none")) continue;
        out[n++] = en;
    }
    return n;
}
static const low_cst_t *ct_find_op(const low_parse_result_t *pr, proven_u8str_view_t name) {
    if (!pr) return NULL;
    for (proven_size_t i = 0; i < pr->nforms; i++) {
        const low_cst_t *g = pr->forms[i];
        if (g->kind != LOW_CST_FORM || g->nkids < 2 || g->kids[0]->kind != LOW_CST_ATOM) continue;
        if (g->kids[0]->tok.kw != LOW_KW_FN && g->kids[0]->tok.kw != LOW_KW_PROC) continue;
        if (g->kids[1]->kind == LOW_CST_ATOM && proven_u8str_view_eq(g->kids[1]->tok.lex, name)) return g;
    }
    return NULL;
}
// `try` 뒤에서 **불린 op 의 이름**을 집는다 — `try p` · `try p x` · `try (p x)` 셋 다.
static proven_u8str_view_t ct_try_callee(const low_cst_t *nd, proven_size_t j) {
    const low_cst_t *k = (j + 1 < nd->nkids) ? nd->kids[j + 1] : NULL;
    while (k && k->kind == LOW_CST_GROUP && k->nkids) k = k->kids[0];
    if (k && k->kind == LOW_CST_FORM && k->nkids && k->kids[0]->kind == LOW_CST_ATOM) k = k->kids[0];
    if (k && k->kind == LOW_CST_ATOM && k->tok.kind == LOW_TOK_IDENT && k->tok.kw == LOW_KW_NONE)
        return k->tok.lex;
    return (proven_u8str_view_t){ .ptr = NULL, .size = 0 };
}

// ★★★ 2026-09-25 (소유자 선택 ⓐ) — **부른 op 이 낼 수 있는 오류**: `errors` 절이 있으면 그 절,
//   **없으면 그 op 의 오류 enum 전체**. 전엔 절만 읽어서 절 없는 op 을 `try`·`return` 으로 넘기면
//   **아무것도 대조하지 않았다** — 절 없는 op 은 직접 `return error` 는 못 해도(E-ERR-UNDECLARED)
//   자기 아래에서 넘겨받은 실패는 올린다. 그 실패가 선언과 맞는지 아무도 안 봤다.
//   ☞ 절을 안 적는 것은 여전히 정당한 선택이다 — 다만 그 뜻(«이 타입의 오류는 뭐든 난다»)을
//     **부르는 쪽에서도 그대로** 읽는다. 절을 강제하지 않는다(그것은 다른 결정이다, ⓑ).
static proven_size_t ct_callee_errors(const ct_ctx_t *c, const low_cst_t *g,
                                      proven_u8str_view_t *out, proven_size_t max) {
    proven_size_t n = ct_op_errors(g, out, max);
    if (n) return n;
    const low_cst_t *eb = ct_err_enum(c->pr, g);
    if (!eb) return 0;                     // 오류 타입이 선언된 enum 이 아니다 — 모르면 안 본다
    for (proven_size_t v = 0; v < eb->nkids && n < max; v++) {
        const low_cst_t *vk = eb->kids[v];
        if (vk->kind == LOW_CST_FORM && vk->nkids && vk->kids[0]->kind == LOW_CST_ATOM) vk = vk->kids[0];
        if (vk->kind == LOW_CST_ATOM && vk->tok.kind == LOW_TOK_IDENT) out[n++] = vk->tok.lex;
    }
    return n;
}

// walk body: any `error NAME` / `err NAME` must have NAME in the declared error set
static void ct_walk_errors_in(ct_ctx_t *c, const low_cst_t *nd, bool consumed);
static void ct_walk_errors(ct_ctx_t *c, const low_cst_t *nd) { ct_walk_errors_in(c, nd, false); }
static void ct_walk_errors_in(ct_ctx_t *c, const low_cst_t *nd, bool consumed) {
    if (!nd) return;
    // ★ MM5/MM3b — **`case error e` 는 반환이 아니라 패턴**(error 케이스를 매칭하고 값을 e 에 묶는다,
    //   RFC-0081). case 폼의 **직접 자식**(패턴 부분)에서는 `error NAME` 을 반환으로 보지 않는다 —
    //   진짜 반환(`return error X`)은 케이스의 do-블록(자식) 안에 있고, 그건 아래 재귀가 훑는다.
    bool is_case = nd->kind == LOW_CST_FORM && nd->nkids >= 1 &&
                   nd->kids[0]->kind == LOW_CST_ATOM && nd->kids[0]->tok.kw == LOW_KW_CASE;
    if (!is_case)
        for (proven_size_t j = 0; j + 1 < nd->nkids; j++) {
            if ((is_atom_word(nd->kids[j], "error") || is_atom_word(nd->kids[j], "err")) &&
                nd->kids[j + 1]->kind == LOW_CST_ATOM) {
                proven_u8str_view_t name = nd->kids[j + 1]->tok.lex;
                if (!in_set(c->declared, c->ndeclared, name))
                    ct_emit(c, "E-ERR-UNDECLARED", "op returns an error not in its errors clause", nd->kids[j]->line);
            }
        }
    // ★ `try <op>` — 부른 op 의 선언된 오류가 **내 절 안에** 있어야 한다.
    for (proven_size_t j = 0; j + 1 < nd->nkids; j++) {
        if (!is_atom_word(nd->kids[j], "try")) continue;
        // ★★★★★ **`try` 를 쓰는 op 은 자기도 그 오류를 돌려줄 수 있어야 한다** (정본 §6.5.8(2) ·
        //   결함 노트 #43, 2026-09-16). `output u8` 인 op 안의 `try` 가 통과했고, 실패하면 VM 이
        //   **오류 값을 u8 자리로** 돌려줬다(`plus(250) = err too_big`). 네이티브는 아예 짓지
        //   못했다(C 컴파일러가 «returning lw_r but long long expected»). 두 뒤끝이 갈리는 자리다.
        // ★ 꼬리를 붙인 `try`(`else_none` · `else_error`)는 **채널을 바꾼다**(§6.5.8(4)) —
        //   실패가 위로 가지 않으므로 이 op 이 `result` 일 필요가 없다.
        // ★ `let r be (result …) try … .` — 값이 **result 이름에 담기면** 실패는 밖으로 나가지 않는다.
        // ★ 이 폼 자신이 «소비하는» 자리인가 — `--flat`(정규화 끈 대조 스위치)에서는 `try` 가
        //   소비 op 과 **같은 폼의 형제**로 온다(나무에서는 자식이다). 두 모양을 같게 본다.
        bool head_consumer = false;   // 이 폼 안에 **소비하는 낱말**이 있는가(§6.5.8(3))
        for (proven_size_t z = 0; z < nd->nkids; z++)
            if (nd->kids[z]->kind == LOW_CST_ATOM &&
                (is_atom_word(nd->kids[z], "is_ok") || is_atom_word(nd->kids[z], "is_error") ||
                 is_atom_word(nd->kids[z], "is_some") || is_atom_word(nd->kids[z], "is_none") ||
                 is_atom_word(nd->kids[z], "ok_value") || is_atom_word(nd->kids[z], "error_value") ||
                 is_atom_word(nd->kids[z], "some_value") || is_atom_word(nd->kids[z], "value_or")))
                { head_consumer = true; break; }
        bool self_bind_result = false;
        if (nd->nkids >= 3 && nd->kids[0]->kind == LOW_CST_ATOM &&
            (nd->kids[0]->tok.kw == LOW_KW_LET || nd->kids[0]->tok.kw == LOW_KW_VAR))
            for (proven_size_t z = 2; z < nd->nkids; z++) {
                if (nd->kids[z]->kind != LOW_CST_ATOM) continue;
                if (nd->kids[z]->tok.kw == LOW_KW_BE) break;
                if (veq(nd->kids[z]->tok.lex, "result") || veq(nd->kids[z]->tok.lex, "option"))
                    { self_bind_result = true; break; }
            }
        bool bound_to_result = consumed || head_consumer || self_bind_result;
        bool has_tail = false;
        for (proven_size_t z = j + 1; z < nd->nkids; z++)
            if (nd->kids[z]->kind == LOW_CST_ATOM &&
                (is_atom_word(nd->kids[z], "else_none") || is_atom_word(nd->kids[z], "else_error") ||
                 is_atom_word(nd->kids[z], "map_error")))
                { has_tail = true; break; }
        if (!c->self_result && !has_tail && !bound_to_result) {
            ct_emit(c, "E-TRY-NORESULT",
                    "`try` may only be used in an op that can RETURN that error (§6.5.8(2)): this op's "
                    "output is not a `result`, so there is nowhere for the failure to go. It used to "
                    "pass, and the VM then returned the ERROR VALUE in the plain output slot while the "
                    "native build would not compile at all. Declare `output result <T> <E> .` (and the "
                    "matching `errors` clause), or handle the failure here (`guard is_ok …` / "
                    "`value_or` / a `case error` arm)", nd->kids[j]->line);
            continue;
        }
        proven_u8str_view_t callee = ct_try_callee(nd, j);
        if (!callee.size) continue;
        const low_cst_t *g = ct_find_op(c->pr, callee);
        if (!g) continue;                       // 모르는 이름(내장·외부) — 보수적으로 넘어간다
        proven_u8str_view_t es[CT_MAX];
        proven_size_t ne = ct_callee_errors(c, g, es, CT_MAX);
        // ★ 기준은 **두 단계**다 (실측으로 배웠다 — 첫 판은 fixture 넷을 거짓 양성으로 잡았다):
        //     `errors` 절이 있으면  → 그 절이 기준이다(더 좁은 주장을 했으므로 지켜야 한다)
        //     절이 없으면          → **오류 enum 자체**가 기준이다(주장은 타입뿐이다)
        //   절을 안 적는 것은 *"이 타입의 오류는 뭐든 난다"* 는 정당한 선택이다 — 그걸 오류라
        //   부르면 `errors` 절이 사실상 강제가 되고, 그것은 이 RFC 가 정한 바가 아니다.
        for (proven_size_t e = 0; e < ne; e++)
            if (c->ndeclared ? !in_set(c->declared, c->ndeclared, es[e])
                             : (c->self_enum && !ct_in_enum(c->self_enum, es[e])))
                ct_emit(c, "E-ERR-UNDECLARED",
                        "`try` propagates an error this op did not declare. The callee's `errors` "
                        "clause names an error that is NOT in this op's own `errors` clause, so the "
                        "clause claims less than the op can actually produce — and the caller budgets "
                        "for that claim. Add it here, or handle it (`else_error` / a `case error` arm) "
                        "so it never leaves. (RFC-0006 §6: errors is a CLOSURE, not a hint)",
                        nd->kids[j]->line);
    }
    // ★★★ 2026-09-25 — **`return <op> …` 도 부른 op 의 실패를 그대로 올린다** — `try` 와 같은 닫힘이다.
    //   `try` 만 대조해서, `output result u64 b_error` 인 op 이 `return a w .` 로 **a_error 의 변형**을
    //   내보내도 `--check` 초록이었다(known-defects/bare-return-under-result.md «형제 증상»).
    //   기준은 `try` 와 **똑같다** — 같은 `ct_callee_errors` 를 쓴다.
    if (c->self_result)
        for (proven_size_t j = 0; j + 1 < nd->nkids; j++) {
            if (nd->kids[j]->kind != LOW_CST_ATOM || nd->kids[j]->tok.kw != LOW_KW_RETURN) continue;
            proven_u8str_view_t callee = ct_try_callee(nd, j);
            if (!callee.size) continue;
            const low_cst_t *g = ct_find_op(c->pr, callee);
            if (!g || !ct_err_enum(c->pr, g)) continue;   // 모르는 op · result 가 아닌 op — 보수적으로 넘어간다
            proven_u8str_view_t es[CT_MAX];
            proven_size_t ne = ct_callee_errors(c, g, es, CT_MAX);
            for (proven_size_t e = 0; e < ne; e++)
                if (c->ndeclared ? !in_set(c->declared, c->ndeclared, es[e])
                                 : (c->self_enum && !ct_in_enum(c->self_enum, es[e]))) {
                    ct_emit(c, "E-ERR-UNDECLARED",
                            "`return <op>` hands on that op's failures as this op's own, but the callee "
                            "can fail with an error that is NOT in this op's `errors` clause (or error "
                            "enum) — the declaration promises failures this op cannot keep to. Convert "
                            "it (`try … map_error` / a `case error` arm), or declare it here. "
                            "(RFC-0006 §6: errors is a CLOSURE, not a hint)",
                            nd->kids[j]->line);
                    break;
                }
        }
    // ★ `let r be (result …) …` 아래로는 «담긴다» 는 사실을 물려준다 — 그 자리의 실패는
    //   밖으로 나가지 않는다(§6.5.8(3) 의 «묻는 것·꺼내는 것» 으로 이어진다).
    bool child_consumed = consumed;
    // ★ 소비하는 낱말이 이 폼에 있으면 **블록이 아닌** 자식에게만 물려준다 — 블록 안은 다른
    //   문장이고, 거기서 새는 실패까지 덮으면 안 된다. (`--flat` 은 `try` 를 형제로 둔다.)
    bool form_consumer = false;
    for (proven_size_t z = 0; z < nd->nkids; z++)
        if (nd->kids[z]->kind == LOW_CST_ATOM &&
            (is_atom_word(nd->kids[z], "is_ok") || is_atom_word(nd->kids[z], "is_error") ||
             is_atom_word(nd->kids[z], "is_some") || is_atom_word(nd->kids[z], "is_none") ||
             is_atom_word(nd->kids[z], "ok_value") || is_atom_word(nd->kids[z], "error_value") ||
             is_atom_word(nd->kids[z], "some_value") || is_atom_word(nd->kids[z], "value_or")))
            { form_consumer = true; break; }
    if (nd->kind == LOW_CST_FORM && nd->nkids >= 3 && nd->kids[0]->kind == LOW_CST_ATOM &&
        (nd->kids[0]->tok.kw == LOW_KW_LET || nd->kids[0]->tok.kw == LOW_KW_VAR))
        for (proven_size_t z = 2; z < nd->nkids; z++) {
            if (nd->kids[z]->kind != LOW_CST_ATOM) continue;
            if (nd->kids[z]->tok.kw == LOW_KW_BE) break;
            if (veq(nd->kids[z]->tok.lex, "result") || veq(nd->kids[z]->tok.lex, "option"))
                { child_consumed = true; break; }
        }
    for (proven_size_t j = 0; j < nd->nkids; j++)
        ct_walk_errors_in(c, nd->kids[j],
                          child_consumed || (form_consumer && nd->kids[j]->kind != LOW_CST_BLOCK));
}

// ── E-CONTRACT-DEAD — `requires` 가 이미 배제한 오류는 **도달할 수 없다** ─────────
//
// 사용자 결정(2026-07-12): **안정성 기반** 죽은-경로 검사.
//   조건의 입력이 op 실행 중에 **변할 수 없음이 증명되면**, 진입에서 배제한 조건은
//   본문 어디서도 참이 될 수 없다 ⇒ 같은 조건의 `errors … when` 은 **죽은 경로**다.
//   그러나 입력이 **변할 수 있으면**(volatile · atomic · shared · mut_ref) 진입 검사가
//   사용 시점을 덮지 못한다 ⇒ 둘 다 정당하다. 중복이 아니다.
//
// 이것이 이 언어가 두 기제를 **모두** 갖는 이유를 정확히 가른다:
//   requires = "진입 시점의 사실"        errors  = "그 지점에서 실제로 일어난 일"
//   불변 입력이면 앞의 것이 뒤의 것을 덮는다. 변하는 입력이면 못 덮는다.
//
// 보수적으로 판정한다 — 안정성을 **증명하지 못하면** 오류를 내지 않는다(거짓 양성 0).

static bool ct_complement(proven_u8str_view_t a, proven_u8str_view_t b) {
    return (veq(a, "ge") && veq(b, "lt")) || (veq(a, "lt") && veq(b, "ge"))
        || (veq(a, "gt") && veq(b, "le")) || (veq(a, "le") && veq(b, "gt"))
        || (veq(a, "eq") && veq(b, "ne")) || (veq(a, "ne") && veq(b, "eq"));
}
// 파라미터가 op 실행 동안 **변할 수 없는가** — mut / mut_ref 는 변할 수 있다.
static bool ct_param_immutable(const low_cst_t *f, proven_u8str_view_t name) {
    // ★ 헤더가 `mut` 을 이미 벗겨 놨다 (DECISION-0015) — 여기서 다시 찾지 않는다.
    low_op_header_t h = low_op_header(f);
    for (proven_size_t q = 0; q < h.np; q++) {
        if (!proven_u8str_view_eq(h.p[q].name, name)) continue;
        if (h.p[q].is_mut) return false;                            // ★ 변할 수 있다
        for (proven_size_t k = h.p[q].ts; k < h.p[q].te && f->kids[k]->kind == LOW_CST_ATOM; k++)
            if (veq(f->kids[k]->tok.lex, "mut_ref")) return false;  // ★ 변할 수 있다
        return true;
    }
    return false;   // 파라미터가 아니다 — 알 수 없으므로 보수적
}
// 조건의 모든 이름이 **불변 파라미터 · 순수 내장 · 순수 fn** 이어야 안정이다.
// 그 밖의 것(proc · volatile/atomic 읽기 · 모르는 이름)은 **불안정**으로 본다.
static bool ct_stable(const low_cst_t *f, const low_cst_t *const *k, proven_size_t from,
                      proven_size_t to, const proven_u8str_view_t *calcs, proven_size_t ncalc) {
    for (proven_size_t i = from; i < to; i++) {
        if (k[i]->kind != LOW_CST_ATOM) return false;
        const low_token_t *t = &k[i]->tok;
        if (t->kind != LOW_TOK_IDENT) continue;             // 숫자·기호는 안정
        if (t->kw != LOW_KW_NONE) continue;                 // 키워드
        if (is_known_pred(t->lex)) continue;                // 순수 내장
        if (ct_param_immutable(f, t->lex)) continue;        // 불변 파라미터
        if (in_set(calcs, ncalc, t->lex)) continue;         // 순수 fn (effects none 강제)
        return false;                                       // ★ 증명 못 함 → 불안정
    }
    return true;
}
// 절의 끝 = 다음 절 키워드
// ★ 절의 끝: **다음 절 낱말** 또는 **본체 블록**. 원자만 훑으면 **괄호에서 멈춘다** —
//   조건에는 괄호가 들어간다(`lt (len data) 4`).
static proven_size_t ct_clause_end(const low_cst_t *f, proven_size_t from) {
    proven_size_t i = from;
    while (i < f->nkids && f->kids[i]->kind != LOW_CST_BLOCK &&
           !(f->kids[i]->kind == LOW_CST_ATOM && is_clause_kw(f->kids[i]->tok.lex))) i++;
    return i;
}
static bool ct_seq_eq(const low_cst_t *const *k, proven_size_t a0, proven_size_t a1,
                      proven_size_t b0, proven_size_t b1) {
    if (a1 - a0 != b1 - b0) return false;
    for (proven_size_t i = 0; i < a1 - a0; i++) {
        if (k[a0 + i]->kind != LOW_CST_ATOM || k[b0 + i]->kind != LOW_CST_ATOM) return false;
        if (!proven_u8str_view_eq(k[a0 + i]->tok.lex, k[b0 + i]->tok.lex)) return false;
    }
    return true;
}

// ★ `errors` 절이 거는 이름은 **선언된 enum 변형**이어야 한다.
//   지금까지 본문 쪽만 봤다(E-ERR-UNDECLARED: 선언 안 한 오류를 반환). 절 자체는 안 봤다 —
//   `errors ghost .` 처럼 enum 에 없는 이름을 걸어도 조용히 통과했다.
// 이름이 **특정 enum** 의 변형인가. (enum 을 못 찾으면 NULL 을 돌려준다 — 그 경우 검사하지 않는다:
//  `output result u8 small .` 처럼 **enum 없이 오류 이름을 바로 쓰는 형태**도 언어가 허용한다.)
static const low_cst_t *ct_enum_of(const low_parse_result_t *pr, proven_u8str_view_t ename) {
    for (proven_size_t i = 0; i < pr->nforms; i++) {
        const low_cst_t *f = pr->forms[i];
        if (f->kind != LOW_CST_FORM || f->nkids < 3 || f->kids[0]->kind != LOW_CST_ATOM) continue;
        if (f->kids[0]->tok.kw != LOW_KW_ENUM) continue;
        if (f->kids[1]->kind != LOW_CST_ATOM) continue;
        if (!proven_u8str_view_eq(f->kids[1]->tok.lex, ename)) continue;
        const low_cst_t *blk = f->kids[f->nkids - 1];
        return (blk->kind == LOW_CST_BLOCK) ? blk : NULL;
    }
    return NULL;
}
static bool ct_in_enum(const low_cst_t *blk, proven_u8str_view_t name) {
    for (proven_size_t j = 0; j < blk->nkids; j++) {
        const low_cst_t *v = blk->kids[j];
        if (v->kind == LOW_CST_ATOM && proven_u8str_view_eq(v->tok.lex, name)) return true;
        if (v->kind == LOW_CST_FORM && v->nkids > 0 && v->kids[0]->kind == LOW_CST_ATOM &&
            proven_u8str_view_eq(v->kids[0]->tok.lex, name)) return true;
    }
    return false;
}
// op 의 `output result <t> <e> .` 에서 오류 타입 이름 e 를 찾는다.
static const low_cst_t *ct_err_enum(const low_parse_result_t *pr, const low_cst_t *f) {
    for (proven_size_t j = 2; j + 2 < f->nkids; j++) {
        if (f->kids[j]->kind != LOW_CST_ATOM || !veq(f->kids[j]->tok.lex, "output")) continue;
        if (f->kids[j + 1]->kind != LOW_CST_ATOM || !veq(f->kids[j + 1]->tok.lex, "result")) return NULL;
        // result <payload> <error> — 오류 이름은 payload 다음 원자
        proven_size_t q = j + 3;
        if (q >= f->nkids || f->kids[q]->kind != LOW_CST_ATOM) return NULL;
        return ct_enum_of(pr, f->kids[q]->tok.lex);
    }
    return NULL;
}

// ★ 이 이름이 **선언된 trait** 인가 — `requires <trait> <type>` 는 **경계**이지 술어가 아니다.
static bool ct_is_trait(const low_parse_result_t *pr, proven_u8str_view_t nm) {
    for (proven_size_t i = 0; i < pr->nforms; i++) {
        const low_cst_t *f = pr->forms[i];
        if (f->kind != LOW_CST_FORM || f->nkids < 2 || f->kids[0]->kind != LOW_CST_ATOM) continue;
        if (f->kids[0]->tok.kw != LOW_KW_TRAIT || f->kids[1]->kind != LOW_CST_ATOM) continue;
        if (proven_u8str_view_eq(f->kids[1]->tok.lex, nm)) return true;
    }
    return false;
}


// ── RFC-0104 §8-3 — **지금 판정할 수 있는 계약은 지금 판정한다** ────────────────────
//
//   실측(2026-08-28): `requires le n 64 .` 인 op 을 `budget 100` 으로 부르면 `--check` 는
//   **초록**이고, 실행해서야 `E-VM-CONTRACT` 로 터졌다. 그런데 그 위반은 **소스에 다 적혀
//   있다** — 상수 하나와 상수 하나다. 실행까지 미룰 이유가 없다.
//   ★ 그리고 이것이 §8-3 이 요구한 *"overflow 구성은 compile-time 거절"* 의 실체다:
//     bit 예산은 리터럴로 적히고, 예산을 넘긴 구성은 **부르는 자리에서** 거절되어야 한다.
//   ☞ 좁게 문다: **양쪽이 다 상수로 접힐 때만**. 하나라도 모르면 아무 말도 안 한다
//     (거짓 고발보다 침묵이 낫다 — 런타임 검사가 그대로 남아 있다).
typedef struct { proven_u8str_view_t name; proven_i64 val; bool known; } ct_bind_t;

static bool ct_lit(proven_u8str_view_t v, proven_i64 *out) {
    if (!v.size) return false;
    proven_i64 n = 0;
    for (proven_size_t i = 0; i < v.size; i++) {
        if (v.ptr[i] < (proven_byte_t)'0' || v.ptr[i] > (proven_byte_t)'9') return false;
        if (n > (INT64_MAX - 9) / 10) return false;
        n = n * 10 + (v.ptr[i] - (proven_byte_t)'0');
    }
    *out = n;
    return true;
}
// 피연산자를 상수로 접는다: 리터럴 · 상수로 묶인 파라미터 · 그 둘의 add/sub/mul.
static bool ct_fold(const low_cst_t *nd, const ct_bind_t *b, proven_size_t nb, proven_i64 *out) {
    if (!nd) return false;
    while (nd->kind == LOW_CST_GROUP && nd->nkids == 1) nd = nd->kids[0];
    if (nd->kind == LOW_CST_ATOM) {
        if (ct_lit(nd->tok.lex, out)) return true;
        for (proven_size_t i = 0; i < nb; i++)
            if (b[i].known && proven_u8str_view_eq(b[i].name, nd->tok.lex)) { *out = b[i].val; return true; }
        return false;
    }
    if (nd->kind != LOW_CST_FORM) return false;
    // ★ 폼은 **머리를 kid 로도 갖는다**: `(add a b)` 는 tok=add 이고 kids=[add, a, b] 다(실측).
    //   그래서 피연산자 자리는 nkids 에 따라 다르다 — 둘 다 받는다.
    proven_size_t o0, o1;
    if (nd->nkids == 2) { o0 = 0; o1 = 1; }
    else if (nd->nkids == 3 && nd->kids[0] && nd->kids[0]->kind == LOW_CST_ATOM &&
             proven_u8str_view_eq(nd->kids[0]->tok.lex, nd->tok.lex)) { o0 = 1; o1 = 2; }
    else return false;
    proven_i64 x, y;
    if (!ct_fold(nd->kids[o0], b, nb, &x) || !ct_fold(nd->kids[o1], b, nb, &y)) return false;
    proven_u8str_view_t op = nd->tok.lex;
    if (veq(op, "add")) { if ((y > 0 && x > INT64_MAX - y) || (y < 0 && x < INT64_MIN - y)) return false; *out = x + y; return true; }
    if (veq(op, "sub")) { *out = x - y; return true; }
    if (veq(op, "mul")) { if (x != 0 && (x > INT64_MAX / (y ? y : 1))) return false; *out = x * y; return true; }
    return false;
}
static bool ct_cmp_eval(proven_u8str_view_t op, proven_i64 a, proven_i64 bv, bool *ok) {
    *ok = true;
    if (veq(op, "lt")) return a <  bv;
    if (veq(op, "le")) return a <= bv;
    if (veq(op, "gt")) return a >  bv;
    if (veq(op, "ge")) return a >= bv;
    if (veq(op, "eq")) return a == bv;
    if (veq(op, "ne")) return a != bv;
    *ok = false;
    return false;
}


// 본문 나무를 훑어 **호출 자리마다** callee 의 `requires` 를 상수로 판정한다.
static void ct_static_calls(ct_ctx_t *c, const low_cst_t *nd) {
    if (!nd) return;
    // ★ 호출은 **평평한 형제 열**이다: `return budget 100 .` 은 머리가 `return` 이고
    //   호출 이름은 **kid 원자**다. 머리 토큰만 보면 아무 호출도 안 잡힌다(실측).
    for (proven_size_t i = 0; i < nd->nkids; i++) {
        const low_cst_t *a = nd->kids[i];
        if (!a || a->kind != LOW_CST_ATOM || a->tok.kind != LOW_TOK_IDENT ||
            a->tok.kw != LOW_KW_NONE) continue;
        const low_cst_t *g = ct_find_op(c->pr, a->tok.lex);
        if (!g) continue;
        low_op_header_t h = low_op_header(g);
        if (!h.np) continue;
        ct_bind_t bind[LOW_HDR_MAXP]; proven_size_t nb = 0;
        for (proven_size_t q = 0; q < h.np && nb < LOW_HDR_MAXP; q++) {
            const low_cst_t *ar = (i + 1 + q < nd->nkids) ? nd->kids[i + 1 + q] : NULL;
            proven_i64 v = 0;
            bool known = ar && ar->kind == LOW_CST_ATOM && ct_lit(ar->tok.lex, &v);
            bind[nb].name = h.p[q].name; bind[nb].val = v; bind[nb].known = known;
            nb++;
        }
        for (proven_size_t j = 0; j + 3 < g->nkids; j++) {
            if (!is_atom_word(g->kids[j], "requires")) continue;
            if (!g->kids[j + 1] || g->kids[j + 1]->kind != LOW_CST_ATOM) continue;
            proven_u8str_view_t op = g->kids[j + 1]->tok.lex;
            proven_i64 lv, rv;
            if (!ct_fold(g->kids[j + 2], bind, nb, &lv)) continue;
            if (!ct_fold(g->kids[j + 3], bind, nb, &rv)) continue;
            bool ok = false;
            bool holds = ct_cmp_eval(op, lv, rv, &ok);
            if (!ok || holds) continue;
            ct_emit(c, "E-CONTRACT-IMPOSSIBLE",
                    "this call breaks the callee's `requires`, and BOTH SIDES ARE CONSTANTS — so it "
                    "can be decided here, now. It used to compile green and trap at run time "
                    "(E-VM-CONTRACT): a bit budget that does not fit (`slot + shard + generation > "
                    "word`) shipped as a runnable program. A contract that can be decided at compile "
                    "time IS decided at compile time (RFC-0104 §8-3)", a->tok.line);
        }
    }
    for (proven_size_t i = 0; i < nd->nkids; i++) ct_static_calls(c, nd->kids[i]);
}

low_contract_result_t low_contract(proven_allocator_t work, const low_parse_result_t *pr) {
    low_contract_result_t out = { .ok = true };
    proven_result_array_t da = PROVEN_ARRAY_INIT(work, low_diag_t, 8);
    if (da.err != PROVEN_OK) { out.ok = false; return out; }
    out.diags = da.value;
    ct_ctx_t c = { .diags = &out.diags, .ok = &out.ok, .pr = pr };

    // gather all declared op names first (predicate ops usable in requires)
    // ★ 이름 표 셋을 **폼 수만큼** 잡는다(위 주석). 실패하면 아무것도 안 하고 나간다 —
    //   *못 잰 검사를 초록으로 내지 않는다.*
    proven_size_t ncap = pr->nforms + 32;   // 폼 수 + 한 op 의 파라미터 여유
    proven_result_mem_mut_t rn = work.alloc_fn(work.ctx, ncap * sizeof(proven_u8str_view_t), alignof(proven_u8str_view_t));
    proven_result_mem_mut_t ro = work.alloc_fn(work.ctx, ncap * sizeof(proven_u8str_view_t), alignof(proven_u8str_view_t));
    proven_result_mem_mut_t rc = work.alloc_fn(work.ctx, ncap * sizeof(proven_u8str_view_t), alignof(proven_u8str_view_t));
    if (rn.err != PROVEN_OK || ro.err != PROVEN_OK || rc.err != PROVEN_OK) return out;
    c.names = (proven_u8str_view_t *)rn.value.ptr; c.namescap = ncap;
    proven_u8str_view_t *opnames = (proven_u8str_view_t *)ro.value.ptr;
    proven_u8str_view_t *calcbuf = (proven_u8str_view_t *)rc.value.ptr;
    proven_size_t nop = 0;
    for (proven_size_t i = 0; i < pr->nforms; i++) {
        const low_cst_t *f = pr->forms[i];
        if (f->kind == LOW_CST_FORM && f->nkids >= 2 && f->kids[0]->kind == LOW_CST_ATOM &&
            (f->kids[0]->tok.kw == LOW_KW_FN || f->kids[0]->tok.kw == LOW_KW_PROC))
            opnames[nop++] = f->kids[1]->tok.lex;
    }
    LOW_HWM("contract:opnames", nop, ncap);

    for (proven_size_t i = 0; i < pr->nforms; i++) {
        const low_cst_t *f = pr->forms[i];
        if (f->kind != LOW_CST_FORM || f->nkids < 2 || f->kids[0]->kind != LOW_CST_ATOM) continue;
        low_kw_t kw = f->kids[0]->tok.kw;
        if (kw != LOW_KW_FN && kw != LOW_KW_PROC) continue;
        const low_cst_t *body = (f->kids[f->nkids - 1]->kind == LOW_CST_BLOCK) ? f->kids[f->nkids - 1] : NULL;

        // collect declared errors + params (input NAME), seed names with params + op names
        c.ndeclared = 0; c.nnames = 0;
        // ★★★★★ **순서가 뜻이다 — 자기 매개변수를 먼저 넣는다** (2026-08-11).
        //   전엔 **단위의 모든 op 이름**을 먼저 채우고 그 다음에 이 op 의 매개변수를 넣었다.
        //   표는 고정 크기(CT_MAX)이고 **조용히 잘린다** ⇒ 단위가 커지면 op 이름이 표를 다
        //   먹고 **자기 매개변수가 한 개도 못 들어갔다.** 그러면 `requires ge nslots 1 .` 의
        //   `nslots` 가 *"정의되지 않은 이름"* 이 된다 — **자기가 선언한 파라미터인데.**
        //   ☞ 실측: `lib/hash.low` 은 혼자면 초록인데 `term`/`regex` 와 한 단위에 놓으면
        //     E-REQ-UNDEF 였다. 그 둘은 `unicode`+`utf8`+`fmt` 를 **전이적으로** 끌고 와
        //     단위가 70~80 op 이 된다. `unicode`(37 op) 하나와는 초록이었다 — 임계 아래라서다.
        //     즉 **내 계약의 합법성이 "남이 몇 개를 선언했는가" 에 달려 있었다**(오늘 세 번째
        //     같은 모양 — 지역 이름·가림 통에 이어).
        //   ⇒ 자기 것부터 넣는다. 잘림이 나도 **자기 파라미터는 언제나 표에 있다.**
        {
            low_op_header_t h = low_op_header(f);
            for (proven_size_t q = 0; q < h.np && c.nnames < c.namescap; q++)
                c.names[c.nnames++] = h.p[q].name;
        }
        for (proven_size_t k = 0; k < nop && c.nnames < c.namescap; k++) c.names[c.nnames++] = opnames[k];
        // ★ 그래도 잘렸다면 **모른다고 말한다**: 표가 넘친 상태에서 *"정의되지 않았다"* 는
        //   판정은 근거가 없다. 잘림을 기억해 두고 아래에서 그 판정을 **내리지 않는다**
        //   (조용히 틀린 고발보다 조용한 침묵이 낫다 — 그리고 위 순서 덕에 자기 파라미터는
        //    이미 들어가 있으므로, 침묵하는 경우는 *다른 op 이름*을 가리킬 때뿐이다).
        // ★ 이제 표가 정확한 상한만큼 있으므로 **잘림은 일어나지 않는다.**
        //   그래도 변수는 남긴다 — 잘렸다면 판정을 접는다는 **약속 자체는 옳다**.
        bool names_truncated = (c.nnames >= c.namescap);
        for (proven_size_t j = 2; j + 1 < f->nkids; j++) {
            if (f->kids[j]->kind != LOW_CST_ATOM) continue;
            proven_u8str_view_t w = f->kids[j]->tok.lex;
            // ★ `input` 의 이름 수집은 **헤더가 한다**(아래) — 여기서 `kids[j+1]` 을 이름으로
            //   잡으면 `input comptime n u8 .` 의 이름이 **`comptime`** 이 된다. 실제로 그랬다.
            if (veq(w, "errors")) {
                // ★★★ **`errors <변형> [<조건>] .`** — 절 하나에 **오류 하나** (2026-07-14).
                //   변형은 `kids[j+1]` 하나뿐이고, 그 뒤는 **전부 조건**이다.
                //   전엔 절이 variadic 이라(`errors a b .` = 변형 둘) `when` 표지가 **필요했다.**
                for (proven_size_t e = j + 1; e < j + 2 && e < f->nkids &&
                     f->kids[e]->kind == LOW_CST_ATOM &&
                     !is_clause_kw(f->kids[e]->tok.lex) && c.ndeclared < CT_MAX; e++) {
                    proven_u8str_view_t en = f->kids[e]->tok.lex;
                    if (veq(en, "none")) continue;
                    // ★ 출력이 **선언된 enum** 을 가리킬 때만, 그 변형이어야 한다.
                    //   (enum 없이 오류 이름을 바로 쓰는 형태도 언어가 허용한다 — 그건 검사 안 한다.)
                    const low_cst_t *eb = ct_err_enum(pr, f);
                    if (eb && !ct_in_enum(eb, en))
                        ct_emit(&c, "E-ERR-UNDEF",
                                "the `errors` clause names something that the op's declared error "
                                "enum does not contain", f->line);
                    c.declared[c.ndeclared++] = en;
                }
            }
        }

        // errors-closure over the body
        c.cur_form = f;
        c.self_enum = ct_err_enum(pr, f);
        // 출력이 `result` 인가 — 머리에서 `output` 뒤 첫 낱말을 본다.
        c.self_result = false;
        for (proven_size_t z = 2; z + 1 < f->nkids; z++)
            if (f->kids[z]->kind == LOW_CST_ATOM && veq(f->kids[z]->tok.lex, "output") &&
                f->kids[z + 1]->kind == LOW_CST_ATOM &&
                veq(f->kids[z + 1]->tok.lex, "result")) { c.self_result = true; break; }
        ct_walk_errors(&c, body);
        // ★ 그리고 **지금 판정할 수 있는 계약은 지금 판정한다**(위 ct_static_calls 주석).
        ct_static_calls(&c, body);

        // ★ E-CONTRACT-DEAD: requires 가 배제한 조건을 errors…when 이 다시 건다
        //   (조건의 입력이 **불변임이 증명될 때만**. volatile/atomic/shared/mut_ref 는 통과시킨다.)
        {
            // 순수 fn 이름 모으기 (effects none 이 강제되므로 순수하다)
            proven_u8str_view_t *calcs = calcbuf; proven_size_t ncalc = 0;
            for (proven_size_t q = 0; q < pr->nforms; q++) {
                const low_cst_t *g = pr->forms[q];
                if (g->kind == LOW_CST_FORM && g->nkids >= 2 && g->kids[0]->kind == LOW_CST_ATOM &&
                    g->kids[0]->tok.kw == LOW_KW_FN)
                    calcs[ncalc++] = g->kids[1]->tok.lex;
            }
            LOW_HWM("contract:calcs", ncalc, ncap);
            for (proven_size_t j = 2; j + 1 < f->nkids; j++) {
                if (!is_atom_word(f->kids[j], "requires")) continue;
                proven_size_t rs = j + 1;
                while (rs < f->nkids && f->kids[rs]->kind == LOW_CST_ATOM &&
                       (veq(f->kids[rs]->tok.lex, "static") || veq(f->kids[rs]->tok.lex, "debug") ||
                        veq(f->kids[rs]->tok.lex, "assume"))) rs++;
                if (rs >= f->nkids || f->kids[rs]->kind != LOW_CST_ATOM) continue;
                proven_size_t re = ct_clause_end(f, rs);
                if (re <= rs + 1) continue;
                proven_u8str_view_t rcmp = f->kids[rs]->tok.lex;

                for (proven_size_t e = 2; e + 1 < f->nkids; e++) {
                    if (!is_atom_word(f->kids[e], "errors")) continue;
                    proven_size_t ee = ct_clause_end(f, e + 1);
                    {
                        proven_size_t ws = e + 2;                 // ★ 변형 뒤가 곧 조건이다
                        proven_size_t we = ee;
                        if (ws >= ee || f->kids[ws]->kind != LOW_CST_ATOM) continue;
                        proven_u8str_view_t wcmp = f->kids[ws]->tok.lex;
                        if (!ct_complement(rcmp, wcmp)) continue;
                        if (!ct_seq_eq((const low_cst_t *const *)f->kids, rs + 1, re, ws + 1, we)) continue;
                        // ★ 같은 조건의 부정이다. 이제 **안정성**을 판정한다.
                        if (!ct_stable(f, (const low_cst_t *const *)f->kids, rs + 1, re, calcs, ncalc))
                            continue;   // 변할 수 있다 → 둘 다 정당하다. 중복이 아니다.
                        ct_emit(&c, "E-CONTRACT-DEAD",
                                "this declared error can never occur: `requires` already excludes the "
                                "condition, and the condition's inputs cannot change during the op "
                                "(remove the `requires`, or remove the error — not both)",
                                f->kids[e]->tok.line ? f->kids[e]->tok.line : f->line);
                    }
                }
            }
        }

        // (ct_is_trait 는 아래에 정의)
        // ★ 계약 절의 **이름은 무언가를 가리켜야 한다** — requires · ensures · errors…when 전부.
        //
        //   requires 만 검사하고 있었다. `ensures le qqq 200 .` 는 **조용히 통과했다** —
        //   그런데 구간 분석은 ensures 로 **결과 범위를 유도한다**(RFC-0053 §6.6 dual).
        //   이름이 아무것도 안 가리키면 그 절은 **아무 일도 하지 않는데**, 쓴 사람은 검사됐다고
        //   믿는다. 정확히 "검사되지 않는 중복" 이다. errors…when 의 조건도 같다 —
        //   그 조건이 **오류 지점의 검사로 낮춰진다.**
        static const char *CT_CLAUSES[2] = { "requires", "ensures" };
        (void)0;
        static const char *CT_CODES[2]   = { "E-REQ-UNDEF", "E-ENS-UNDEF" };
        static const char *CT_MSGS[2]    = {
            "requires references an undefined name",
            "ensures references an undefined name — an ensures that names nothing checks nothing, "
            "and the interval analysis DERIVES the result range from it (`ret` is the result)",
        };
        for (proven_size_t ci = 0; ci < 2; ci++)
        for (proven_size_t j = 2; j + 1 < f->nkids; j++) {
            if (!is_atom_word(f->kids[j], CT_CLAUSES[ci])) continue;
            // ★★★ **`requires <trait> <type> .` 는 런타임 술어가 아니라 경계(bound)다** (RFC-0021 §6.3).
            //   그것은 컴파일타임에 **타입이 trait 을 충족하는지**를 요구하는 말이고,
            //   `low_check` 의 ck_trait_bound() 가 검사한다. 여기서 이름을 찾으면 **오진**이다.
            if (ci == 0 && j + 1 < f->nkids && f->kids[j+1]->kind == LOW_CST_ATOM &&
                ct_is_trait(pr, f->kids[j+1]->tok.lex)) continue;
            for (proven_size_t r = j + 1; r < f->nkids && f->kids[r]->kind == LOW_CST_ATOM &&
                 !is_clause_kw(f->kids[r]->tok.lex); r++) {
                const low_token_t *t = &f->kids[r]->tok;
                if (t->kind != LOW_TOK_IDENT) continue;            // skip numbers/ops-symbols
                if (t->kw != LOW_KW_NONE) continue;                // skip keywords
                if (is_known_pred(t->lex)) continue;
                if (ci == 1 && veq(t->lex, "ret")) continue;       // ensures 의 결과 이름
                // ★★★ **계약은 구조체의 필드를 말할 수 있어야 한다** — `requires ge s.w 1 .`
                //   그런데 이름표에는 **파라미터 이름만** 있어서 `s.w` 를 *"정의되지 않은 이름"*
                //   이라 했다 — **오진**이다. `s` 는 정의돼 있고 `w` 는 그 필드다.
                //   (그리고 `errors` 는 같은 모양을 **이미 강제하고 있었다** — 갈려 있었다.)
                {
                    proven_u8str_view_t base = t->lex;
                    for (proven_size_t z = 0; z < t->lex.size; z++)
                        if (t->lex.ptr[z] == (proven_u8)'.') { base.size = z; break; }
                    if (base.size != t->lex.size && in_set(c.names, c.nnames, base)) continue;
                }
                if (!in_set(c.names, c.nnames, t->lex) && !names_truncated)
                    ct_emit(&c, CT_CODES[ci], CT_MSGS[ci], t->line ? t->line : f->line);
            }
        }
    }
    return out;
}
