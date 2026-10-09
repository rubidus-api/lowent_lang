// low_closer.c — RFC-0142 §5-2: 새 표면 «이름은 열고, 점은 닫는다» 를 읽는다.
//
// ★★★ **이 단계는 선언을 읽지 않는다.** 쓰는 자리의 이름은 호출을 열고(`add a. b. .`), 점 하나가 가장 안쪽에 열린 것을
//   닫는다. 변수 `a` 는 곧바로 닫힌 호출 `a.` 이다. 그래서 식의 나무가 **글자만으로** 선다 — 인자 수(선언)는 나무를 세우는
//   재료가 아니라 나무를 세운 뒤의 검사가 된다.
//
//   하는 일은 토큰 열을 고쳐 쓰는 것 하나다: 새 표면의 토큰 열을, 뒤의 파서가 읽는 **괄호로 다 묶인** 토큰 열로 옮긴다.
//     add a. mul b. 2 . .        →   add a (mul b 2)          (값의 점 · 호출의 닫는 점을 지우고, 안쪽 호출을 괄호로 싼다)
//     if c. do … end .           →   if c do … end            (블록으로 끝난 것의 닫는 점을 지운다)
//     let v t f a. . else return 0 . .   →   let v t f a else return 0 .
//   뒤의 파서 · 검사 · 하강은 한 줄도 바뀌지 않는다(RFC-0141 의 `low_surface_lower` 와 같은 길이다 — 표면을 안쪽 꼴로 내린다).
//
//   글자만으로 정해지려면 **낱말 자리**(값이 아닌 자리)를 문법이 알아야 한다. 그것은 닫힌 어휘뿐이다:
//     · 문장 머리와 선언의 자리(`let <이름> <타입>` · `input <이름> <타입>` …)
//     · 기본 연산의 모양(`low_arity.h` 의 LOW_SHAPES · LOW_CLOSER_SHAPES — `cast <타입> <값>` · `field <값> <마디>*`)
//     · 타입 생성자(`slice` · `option` · `result` …). 인자를 받는 사용자 타입은 괄호로 싼다(`(box u64)`).
//   사용자 op 의 인자는 **전부 항**이다 — 타입을 넘길 때도(`max_of u64. a. b. .`).
#include "low_closer.h"
#include "low_arity.h"
#include "low_lex.h"
#include <setjmp.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

typedef struct {
    low_token_t  *T;
    proven_size_t n;
    unsigned char *del;      // 이 토큰을 지운다
    unsigned short *lp, *rp; // 이 토큰 앞에 `(` · 뒤에 `)` 를 몇 개 끼우나
    proven_size_t *mate;     // 괄호 · do/end 의 짝 (없으면 SIZE_MAX)
    bool          ext;       // `extern` 선언 안인가
    jmp_buf       jb;
    low_diag_t    err;
} cl_t;

#define NOPE ((proven_size_t)-1)

static bool cl_eq(const low_token_t *t, const char *s) {
    proven_size_t n = strlen(s);
    return t->lex.size == n && memcmp(t->lex.ptr, s, n) == 0;
}
static bool cl_in(const low_token_t *t, const char *const *set) {
    for (proven_size_t i = 0; set[i]; i++) if (cl_eq(t, set[i])) return true;
    return false;
}
[[noreturn]] static void cl_fail(cl_t *c, proven_size_t i, const char *code, const char *what) {
    const low_token_t *t = &c->T[i < c->n ? i : c->n - 1];
    c->err = (low_diag_t){ .sev = LOW_SEV_ERROR, .code = code, .line = t->line, .col = t->col };
    if (t->kind == LOW_TOK_EOF)
        snprintf(c->err.detail, sizeof c->err.detail, "%s — the file ends here (RFC-0142)", what);
    else
        snprintf(c->err.detail, sizeof c->err.detail, "%s — found `%.*s` (RFC-0142: a name opens, a stop closes; a variable is written `a.`)",
                 what, (int)(t->lex.size > 40 ? 40 : t->lex.size), (const char *)t->lex.ptr);
    longjmp(c->jb, 1);
}
static const low_token_t *cl_tk(cl_t *c, proven_size_t i) {
    if (i >= c->n || c->T[i].kind == LOW_TOK_EOF) cl_fail(c, c->n - 1, "E-DOT-MISSING", "the form is not closed");
    return &c->T[i];
}
static bool cl_id(cl_t *c, proven_size_t i) { return i < c->n && c->T[i].kind == LOW_TOK_IDENT; }
static bool cl_kw(cl_t *c, proven_size_t i, const char *w) { return cl_id(c, i) && c->T[i].kw != LOW_KW_NONE && cl_eq(&c->T[i], w); }
static bool cl_word(cl_t *c, proven_size_t i, const char *w) { return cl_id(c, i) && cl_eq(&c->T[i], w); }
static bool cl_dot(cl_t *c, proven_size_t i) { return i < c->n && c->T[i].kind == LOW_TOK_DOT; }
static proven_size_t cl_need_dot(cl_t *c, proven_size_t i, const char *what) {
    if (!cl_dot(c, i)) cl_fail(c, i, "E-DOT-MISSING", what);
    return i;
}
static proven_size_t cl_mate(cl_t *c, proven_size_t i) {
    if (i >= c->n || c->mate[i] == NOPE) cl_fail(c, i, "E-FORM-UNEXPECTED", "this `(` or `do` has no partner");
    return c->mate[i];
}
static void cl_wrap(cl_t *c, proven_size_t a, proven_size_t b) { c->lp[a]++; c->rp[b]++; }

// ── 닫힌 어휘 ────────────────────────────────────────────────────────────────────────────────────
static const char *const CL_T_ZERO[] = { "bool","u8","i8","u16","i16","u32","i32","u64","i64","usize","isize","f32","f64",
                                         "void","self","str","string","char","byte","bytes_view", NULL };
static const char *const CL_T_PRE1[] = { "slice","mut","owned","ref","mut_ref","option","segments","stack","set","unsafe_ptr","nonzero",
                                         "atomic","lock","rwlock","shared_read","view", NULL };
static const char *const CL_T_CTOR[] = { "result","array","vec","bitset","mask","bits","cap","region","fn","unsafe_fn", NULL };
static const char *const CL_QUAL[]   = { "mut","owned","ref","mut_ref", NULL };
static const char *const CL_GRADE[]  = { "assume","static","debug", NULL };
static const char *const CL_CLAUSE[] = { "input","using","output","effects","errors","tests","satisfies","access","parallel","reduce",
                                         "lowdoc","vector","priority","inplace","invalidates","absorbs","reference","why","strlen",
                                         "link","variadic","schedule","cap","state","on","layout","align","mmio","requires","ensures",
                                         "asm", NULL };
typedef struct { const char *name; const char *shape; } cl_shape_t;
static const cl_shape_t CL_SHAPES[] = {
#define X(n, w, a) { #n, (a) == 0 ? "" : (a) == 1 ? "V" : (a) == 2 ? "VV" : (a) == 3 ? "VVV" : (a) == 4 ? "VVVV" : "VVVVVV" },
    LOW_BUILTINS_CORE(X)
#undef X
#define X(n, sh) { #n, sh },
    LOW_SHAPES(X)
    LOW_NAMED_SHAPES(X)
    LOW_NAMED_SHAPES_KNOWN(X)
#undef X
};
// 파이프 단계: W = op 이름(낱말) · V = 값 · i = `into`. 그 뒤에 값이 더 올 수 있고(`filter gt 2`), `with <값>` 이 올 수 있다.
static const cl_shape_t CL_STAGES[] = {
    { "collect", "iV" }, { "count", "" }, { "sort", "" }, { "take", "V" }, { "skip", "V" }, { "filter", "W" }, { "map", "W" },
    { "any", "W" }, { "all", "W" }, { "enumerate", "W" }, { "fold", "VW" }, { "scan", "VW" }, { "zip", "VW" },
};
static const char *cl_lookup(const cl_shape_t *tab, proven_size_t n, const low_token_t *t) {
    for (proven_size_t i = 0; i < n; i++) if (cl_eq(t, tab[i].name)) return tab[i].shape;
    return NULL;
}
#define CL_N(a) (sizeof(a) / sizeof(*(a)))

static proven_size_t cl_term(cl_t *c, proven_size_t i, bool wrap, bool keep);
static proven_size_t cl_stmt(cl_t *c, proven_size_t i);
static proven_size_t cl_block(cl_t *c, proven_size_t i);
static proven_size_t cl_op_decl(cl_t *c, proven_size_t i);

// 타입 하나의 끝. top: 맨 위의 `(사용자타입 인자…)` 는 괄호를 벗긴다 — 뒤의 파서가 그 자리(절 · 칸 · `lit` · 별칭)에서는 괄호를 받지 않는다.
static proven_size_t cl_tyend(cl_t *c, proven_size_t i, bool top) {
    const low_token_t *t = cl_tk(c, i);
    if (t->kind == LOW_TOK_LPAREN) {
        proven_size_t j = cl_mate(c, i);
        const low_token_t *h = &c->T[i + 1];
        if (top && h->kind == LOW_TOK_IDENT && !cl_in(h, CL_T_ZERO) && !cl_in(h, CL_T_PRE1) && !cl_in(h, CL_T_CTOR)) { c->del[i] = 1; c->del[j] = 1; }
        return j + 1;
    }
    if (t->kind != LOW_TOK_IDENT) cl_fail(c, i, "E-FORM-UNEXPECTED", "a type belongs here");
    if (cl_in(t, CL_T_ZERO)) return i + 1;
    if (cl_in(t, CL_T_PRE1)) return cl_tyend(c, i + 1, false);
    if (cl_eq(t, "result")) return cl_tyend(c, cl_tyend(c, i + 1, false), false);
    if (cl_eq(t, "array") || cl_eq(t, "vec")) {
        proven_size_t j = cl_tyend(c, i + 1, false);
        cl_tk(c, j);
        return c->T[j].kind == LOW_TOK_LPAREN ? cl_mate(c, j) + 1 : j + 1;
    }
    if (cl_eq(t, "bitset") || cl_eq(t, "mask") || cl_eq(t, "bits")) return (i + 1 < c->n && c->T[i + 1].kind == LOW_TOK_NUMBER) ? i + 2 : i + 1;
    if (cl_eq(t, "cap") || cl_eq(t, "region")) return i + 2;
    return i + 1;                                        // 이름 하나 — 인자를 받는 사용자 타입은 괄호 안에 있다
}
static proven_size_t cl_tyq(cl_t *c, proven_size_t i) {  // 한정사 뒤의 맨 위 괄호도 벗긴다
    while (cl_id(c, i) && cl_in(&c->T[i], CL_QUAL)) i++;
    return cl_tyend(c, i, true);
}
// 타입으로 읽히면 읽고(괄호를 벗기고), 아니면 아무것도 하지 않는다. want != NOPE 면 그 자리에서 끝나야 한다.
static bool cl_ty_try(cl_t *c, proven_size_t i, proven_size_t want, bool qual) {
    jmp_buf saved; memcpy(saved, c->jb, sizeof saved);
    unsigned char *snap = (unsigned char *)malloc(c->n);
    if (!snap) return false;
    memcpy(snap, c->del, c->n);
    bool ok = false;
    if (setjmp(c->jb) == 0) {
        proven_size_t j = qual ? cl_tyq(c, i) : cl_tyend(c, i, true);
        ok = want == NOPE || j == want;
    }
    if (!ok) memcpy(c->del, snap, c->n);
    free(snap);
    memcpy(c->jb, saved, sizeof saved);
    return ok;
}

static proven_size_t cl_args(cl_t *c, proven_size_t j) {
    while (!cl_dot(c, j)) j = cl_term(c, j, true, false);
    return j;
}
// 닫는 점의 자리 j 에서 마무리한다: 점을 지우고(keep 이 아니면), 인자 자리의 호출이면 괄호로 싼다.
static proven_size_t cl_done(cl_t *c, proven_size_t i, proven_size_t j, bool wrap, bool keep) {
    cl_need_dot(c, j, "a stop `.` must close this call");
    if (!keep) c->del[j] = 1;
    if (wrap && !keep && j - 1 > i) cl_wrap(c, i, j - 1);
    return j + 1;
}
static void cl_island_inner(cl_t *c, proven_size_t j, proven_size_t e) {
    bool operand = true;
    while (j < e) {
        const low_token_t *x = &c->T[j];
        if (operand) {
            if (x->kind == LOW_TOK_LPAREN) { proven_size_t m = cl_mate(c, j); cl_island_inner(c, j + 1, m); j = m + 1; }
            else if (x->kind == LOW_TOK_OP) { j++; continue; }
            else j = cl_term(c, j, true, false);
            operand = false;
        } else { j++; operand = true; }
    }
}

// 항 하나. wrap: 인자 자리(뒤의 파서에게는 괄호로 싸서 넘긴다). keep: 닫는 점을 남긴다(호출 문장 — 그 점이 문장의 점이다).
static proven_size_t cl_term(cl_t *c, proven_size_t i, bool wrap, bool keep) {
    const low_token_t *t = cl_tk(c, i);
    if (t->kind == LOW_TOK_LPAREN) {
        proven_size_t j = cl_mate(c, i);
        proven_size_t k = cl_term(c, i + 1, false, false);
        if (k != j) cl_fail(c, k, "E-FORM-UNEXPECTED", "parentheses wrap exactly one term");
        return j + 1;
    }
    if (t->kind == LOW_TOK_NUMBER || t->kind == LOW_TOK_STRING || t->kind == LOW_TOK_CHAR || t->kind == LOW_TOK_TEXTLIT) return i + 1;
    if (t->kind != LOW_TOK_IDENT) cl_fail(c, i, "E-FORM-UNEXPECTED", "a value belongs here");
    if (t->kw != LOW_KW_NONE) {
        if (cl_eq(t, "true") || cl_eq(t, "false") || cl_eq(t, "none")) return i + 1;
        if (cl_eq(t, "lit")) {
            if (cl_kw(c, i + 1, "do")) cl_fail(c, i + 1, "E-FORM-UNEXPECTED", "`lit` is followed by a type");
            proven_size_t j = cl_tyend(c, i + 1, true);
            if (cl_kw(c, j, "do")) {
                proven_size_t e = cl_mate(c, j), k = j + 1;
                while (k < e) {
                    k++;                                 // 칸 이름(또는 자리 · `_`)
                    k = cl_term(c, k, false, false);
                    k = cl_need_dot(c, k, "a stop `.` must close this field") + 1;
                }
                return cl_done(c, i, e + 1, wrap, keep);
            }
            proven_size_t k = j;                         // 나열 — 제 점을 그대로 갖는다
            while (!cl_dot(c, k)) {
                if (cl_word(c, k, "_")) { k++; continue; }
                k = cl_term(c, k, true, false);
            }
            if (wrap) cl_wrap(c, i, k);
            return k + 1;
        }
        if (cl_eq(t, "expr")) {
            proven_size_t j = i + 1; bool operand = true;
            for (;;) {
                const low_token_t *x = cl_tk(c, j);
                if (operand) {
                    if (x->kind == LOW_TOK_LPAREN) { proven_size_t m = cl_mate(c, j); cl_island_inner(c, j + 1, m); j = m + 1; }
                    else if (x->kind == LOW_TOK_OP) { j++; continue; }
                    else j = cl_term(c, j, true, false);
                    operand = false;
                } else {
                    if (x->kind == LOW_TOK_DOT) break;
                    j++; operand = true;
                }
            }
            return cl_done(c, i, j, wrap, keep);
        }
        if (cl_eq(t, "send")) {
            proven_size_t j = cl_term(c, i + 1, true, false);
            if (!cl_id(c, j)) cl_fail(c, j, "E-FORM-UNEXPECTED", "`send <actor> <handler> <arguments> .` — the handler name belongs here");
            return cl_done(c, i, cl_args(c, j + 1), wrap, keep);
        }
        if (cl_eq(t, "spawn")) {
            if (cl_kw(c, i + 1, "actor")) {
                proven_size_t j = i + 2;
                while (!cl_dot(c, j)) { cl_tk(c, j); j++; }
                return cl_done(c, i, j, wrap, keep);
            }
            if (cl_kw(c, i + 1, "send")) return cl_done(c, i, cl_term(c, i + 1, false, false), wrap, keep);
            return cl_done(c, i, cl_args(c, i + 2), wrap, keep);
        }
        if (cl_eq(t, "try")) {
            proven_size_t j = cl_term(c, i + 1, true, false);
            while (!cl_dot(c, j)) { cl_tk(c, j); j++; }
            return cl_done(c, i, j, wrap, keep);
        }
        cl_fail(c, i, "E-FORM-UNEXPECTED", "a value belongs here, not a reserved word");
    }
    if (cl_dot(c, i + 1)) {                              // 곧바로 닫힌 이름 — 값(또는 인자 없는 호출)
        if (!keep) c->del[i + 1] = 1;
        return i + 2;
    }
    if (cl_eq(t, "pipe")) {
        proven_size_t j = cl_term(c, i + 1, true, false);
        if (!cl_kw(c, j, "do")) cl_fail(c, j, "E-FORM-UNEXPECTED", "`pipe <source> do <stages> end .`");
        proven_size_t e = cl_mate(c, j), k = j + 1;
        while (k < e) {
            const char *sh = cl_id(c, k) ? cl_lookup(CL_STAGES, CL_N(CL_STAGES), &c->T[k]) : NULL;
            if (!sh) cl_fail(c, k, "E-PIPE-STAGE", "not a pipe stage");
            k++;
            for (; *sh; sh++) {
                if (*sh == 'V') k = cl_term(c, k, true, false);
                else k++;
            }
            while (!cl_dot(c, k) && !cl_word(c, k, "with")) k = cl_term(c, k, true, false);
            if (cl_word(c, k, "with")) k = cl_term(c, k + 1, false, false);
            k = cl_need_dot(c, k, "a stop `.` must close this stage") + 1;
        }
        return cl_done(c, i, e + 1, wrap, keep);
    }
    if (cl_eq(t, "_")) return i + 1;
    if (cl_eq(t, "call_builtin")) return cl_done(c, i, cl_args(c, i + 2), wrap, keep);
    if (cl_eq(t, "alloc_bytes")) {
        proven_size_t j = i + 1;
        if (!cl_word(c, j, "capacity")) j = cl_term(c, j, true, false);
        if (!cl_word(c, j, "capacity")) cl_fail(c, j, "E-FORM-UNEXPECTED", "`alloc_bytes [<root>] capacity <n> .`");
        return cl_done(c, i, cl_term(c, j + 1, true, false), wrap, keep);
    }
    if (cl_eq(t, "pop")) {
        proven_size_t j = cl_term(c, i + 1, true, false);
        if (cl_word(c, j, "into")) j = cl_term(c, j + 1, true, false);
        return cl_done(c, i, j, wrap, keep);
    }
    if (cl_eq(t, "field")) {
        proven_size_t j = cl_term(c, i + 1, true, false);
        while (!cl_dot(c, j)) { cl_tk(c, j); j++; }
        return cl_done(c, i, j, wrap, keep);
    }
    if (cl_eq(t, "method")) {                            // method <값> <마디>* <op 이름>. <인자>* .
        proven_size_t j = cl_term(c, i + 1, true, false);
        while (!cl_dot(c, j + 1)) {
            const low_token_t *s = cl_tk(c, j);
            if (s->kind != LOW_TOK_IDENT && s->kind != LOW_TOK_NUMBER)
                cl_fail(c, j, "E-FORM-UNEXPECTED", "`method <value> <segments> <op name>. <arguments> .` — the op name is the first name closed by a stop");
            j++;
        }
        c->del[j + 1] = 1;                               // op 이름의 점
        c->T[j].aux = (proven_u8str_view_t){ .ptr = (const proven_byte_t *)".", .size = 1 };   // 서식기가 이 이름 뒤에 점을 다시 찍는다
        return cl_done(c, i, cl_args(c, j + 2), wrap, keep);
    }
    if (cl_eq(t, "stack_new")) {
        proven_size_t j = i + 1;
        while (!cl_dot(c, j)) { cl_tk(c, j); j++; }
        return cl_done(c, i, j, wrap, keep);
    }
    if (cl_eq(t, "view")) {
        proven_size_t j = i + 2;
        if (!cl_dot(c, j)) j = cl_term(c, j, true, false);
        return cl_done(c, i, j, wrap, keep);
    }
    // 모양에 낱말 자리(W)가 있는 머리만 표를 따른다. 인자가 전부 값인 머리는 사용자 op 과 꼴이 같다 — 표 없이 읽는다
    // (같은 철자의 사용자 op 이 인자 수가 달라도 여기서는 갈리지 않는다. 인자 수는 뒤의 검사가 본다).
    const char *sh = cl_lookup(CL_SHAPES, CL_N(CL_SHAPES), t);
    if (sh && !strchr(sh, 'W')) sh = NULL;
    if (sh) {
        proven_size_t j = i + 1;
        for (; *sh; sh++) {
            if (*sh == 'V') j = cl_term(c, j, true, false);
            else if (*sh == 'W') { cl_tk(c, j); j++; }
            else if (*sh == 'R') j = cl_args(c, j);
        }
        if (t->lex.size > 7 && memcmp(t->lex.ptr, "atomic_", 7) == 0 && cl_word(c, j, "order")) j += 2;
        return cl_done(c, i, j, wrap, keep);
    }
    if (t->lex.size > 7 && memcmp(t->lex.ptr, "atomic_", 7) == 0) {   // 원자 연산 — 끝에 `order <낱말>` 이 올 수 있다
        proven_size_t j = i + 1;
        while (!cl_dot(c, j)) {
            if (cl_word(c, j, "order") && !cl_dot(c, j + 1)) { j += 2; continue; }
            j = cl_term(c, j, true, false);
        }
        return cl_done(c, i, j, wrap, keep);
    }
    return cl_done(c, i, cl_args(c, i + 1), wrap, keep); // 사용자 op — 인자는 모두 항이다
}

static proven_size_t cl_endstop(cl_t *c, proven_size_t j, const char *what) {   // 블록으로 끝난 것의 닫는 점
    cl_need_dot(c, j, what); c->del[j] = 1; return j + 1;
}
static proven_size_t cl_block(cl_t *c, proven_size_t i) {
    if (!cl_kw(c, i, "do")) cl_fail(c, i, "E-FORM-UNEXPECTED", "a block `do … end` belongs here");
    proven_size_t e = cl_mate(c, i), k = i + 1;
    if (cl_word(c, k, "asm") || c->T[k].kind == LOW_TOK_TEXTLIT) return e + 1;   // 기계어 몸 — 건드리지 않는다
    while (k < e) k = cl_stmt(c, k);
    if (k != e) cl_fail(c, k, "E-DOT-MISSING", "a statement runs past the end of its block");
    return e + 1;
}
// i = `else`. 실패 절과, 그 뒤에 오는 문장의 닫는 점까지.
static proven_size_t cl_fail_clause(cl_t *c, proven_size_t i) {
    proven_size_t j = i + 1;
    if (cl_kw(c, j, "do")) j = cl_block(c, j);
    else if (cl_word(c, j, "error") && cl_kw(c, j + 2, "do")) j = cl_block(c, j + 2);
    else {
        const low_token_t *t = cl_tk(c, j);
        if (cl_kw(c, j, "break") || cl_kw(c, j, "continue")) j++;
        else if (cl_kw(c, j, "return")) j = cl_dot(c, j + 1) ? j + 1 : cl_term(c, j + 1, false, false);
        else if (cl_eq(t, "panic")) j = cl_term(c, j + 1, false, false);
        else cl_fail(c, j, "E-FORM-UNEXPECTED", "after `else` comes a block or a leaving statement (`return` · `break` · `continue` · `panic`)");
        j = cl_need_dot(c, j, "a stop `.` must close the leaving statement") + 1;
    }
    return cl_endstop(c, j, "a stop `.` must close the statement after its `else` clause");
}

static proven_size_t cl_stmt(cl_t *c, proven_size_t i) {
    const low_token_t *t = cl_tk(c, i);
    if (t->kind != LOW_TOK_IDENT) cl_fail(c, i, "E-FORM-UNEXPECTED", "a statement belongs here");
    bool kw = t->kw != LOW_KW_NONE;
    if (kw && (cl_eq(t, "let") || cl_eq(t, "var"))) {
        proven_size_t j = i + 2;
        cl_tk(c, j);
        if (cl_kw(c, j, "use") || cl_kw(c, j, "keep")) { c->del[cl_need_dot(c, j + 2, "the allocator is a variable — write `use g.`")] = 1; j += 3; }
        if (!cl_kw(c, j, "lit")) j = cl_tyend(c, j, false);
        j = cl_term(c, j, false, false);
        if (cl_kw(c, j, "else")) return cl_fail_clause(c, j);
        return cl_need_dot(c, j, "a stop `.` must close the binding") + 1;
    }
    if (kw && cl_eq(t, "guard")) {
        proven_size_t j = cl_term(c, i + 1, false, false);
        if (!cl_kw(c, j, "else")) cl_fail(c, j, "E-FORM-UNEXPECTED", "`guard <condition> else …`");
        return cl_fail_clause(c, j);
    }
    if (kw && cl_eq(t, "set")) {
        proven_size_t j = cl_term(c, i + 1, true, false);
        j = cl_term(c, j, false, false);
        return cl_need_dot(c, j, "a stop `.` must close `set`") + 1;
    }
    if (kw && cl_eq(t, "return")) {
        proven_size_t j = cl_dot(c, i + 1) ? i + 1 : cl_term(c, i + 1, false, false);
        return cl_need_dot(c, j, "a stop `.` must close `return`") + 1;
    }
    if (kw && (cl_eq(t, "break") || cl_eq(t, "continue"))) return cl_need_dot(c, i + 1, "a stop `.` must close this statement") + 1;
    if (kw && cl_eq(t, "expect")) return cl_need_dot(c, cl_term(c, i + 1, false, false), "a stop `.` must close `expect`") + 1;
    if (kw && cl_eq(t, "drop")) {
        c->del[cl_need_dot(c, i + 2, "the dropped name is a variable — write `drop x. .`")] = 1;
        return cl_need_dot(c, i + 3, "a stop `.` must close `drop`") + 1;
    }
    if (kw && cl_eq(t, "if")) {
        proven_size_t j = i;
        for (;;) {
            j = cl_term(c, j + 1, false, false); j = cl_block(c, j);
            if (cl_kw(c, j, "else")) {
                if (cl_kw(c, j + 1, "if")) { j++; continue; }
                j = cl_block(c, j + 1);
            }
            break;
        }
        return cl_endstop(c, j, "a stop `.` must close `if … end`");
    }
    if (kw && cl_eq(t, "while")) return cl_endstop(c, cl_block(c, cl_term(c, i + 1, false, false)), "a stop `.` must close `while … end`");
    if (kw && (cl_eq(t, "for") || cl_eq(t, "repeat") || cl_eq(t, "range") || cl_eq(t, "cycle"))) {
        proven_size_t j = i + 2;                         // 뒤의 파서는 되풀이 머리의 식을 인자 수로 묶지 않는다 — 괄호로 싸서 넘긴다
        if (cl_eq(t, "for")) {
            if (cl_word(c, j, "mut")) j++;
            j = cl_term(c, j, true, false);
        } else {
            j = cl_tyend(c, j, false); j = cl_term(c, j, true, false);
            if (cl_eq(t, "range")) {
                j = cl_term(c, j, true, false);
                if (cl_kw(c, j, "step")) j = cl_term(c, j + 1, true, false);
            }
            if (cl_eq(t, "cycle")) {
                if (!cl_kw(c, j, "while")) cl_fail(c, j, "E-FORM-UNEXPECTED", "`cycle <name> <type> <start> while <condition> next <step> do … end .`");
                j = cl_term(c, j + 1, true, false);
                if (!cl_kw(c, j, "next")) cl_fail(c, j, "E-FORM-UNEXPECTED", "`cycle <name> <type> <start> while <condition> next <step> do … end .`");
                j = cl_term(c, j + 1, true, false);
            }
        }
        if (cl_kw(c, j, "if")) j = cl_term(c, j + 1, true, false);
        return cl_endstop(c, cl_block(c, j), "a stop `.` must close the loop");
    }
    if (kw && cl_eq(t, "match")) {
        proven_size_t j = cl_word(c, i + 1, "comptime") ? cl_term(c, i + 2, true, false) : cl_term(c, i + 1, false, false);
        if (!cl_kw(c, j, "do")) cl_fail(c, j, "E-FORM-UNEXPECTED", "`match <value> do <cases> end .`");
        proven_size_t e = cl_mate(c, j), k = j + 1;
        while (k < e) {
            if (cl_kw(c, k, "else")) { k = cl_block(c, k + 1); continue; }
            if (!cl_kw(c, k, "case")) cl_fail(c, k, "E-FORM-UNEXPECTED", "inside `match` comes `case <pattern> do … end .`");
            k++;
            while (!cl_kw(c, k, "do")) {                 // 갈래 — 낱말들이다(점이 없다)
                if (cl_dot(c, k)) cl_fail(c, k, "E-FORM-UNEXPECTED", "a case has one shape: `case <pattern> do … end .` (the one-statement body is gone, RFC-0142)");
                cl_tk(c, k); k++;
            }
            k = cl_endstop(c, cl_block(c, k), "a stop `.` must close `case … end`");
        }
        j = e + 1;
        if (cl_kw(c, j, "else")) j = cl_block(c, j + 1);
        return cl_endstop(c, j, "a stop `.` must close `match … end`");
    }
    if (cl_eq(t, "region") && cl_kw(c, i + 3, "do")) return cl_endstop(c, cl_block(c, i + 3), "a stop `.` must close `region … end`");
    if (cl_eq(t, "borrow") && cl_id(c, i + 1) && !cl_dot(c, i + 1) && !cl_dot(c, i + 2))
        return cl_endstop(c, cl_block(c, cl_term(c, i + 2, false, false)), "a stop `.` must close `borrow … end`");
    if (cl_eq(t, "task_group") && (cl_kw(c, i + 1, "do") || cl_kw(c, i + 2, "do")))
        return cl_endstop(c, cl_block(c, cl_kw(c, i + 1, "do") ? i + 1 : i + 2), "a stop `.` must close `task_group … end`");
    if (kw && (cl_eq(t, "fn") || cl_eq(t, "proc"))) return cl_op_decl(c, i);
    if (kw && (cl_eq(t, "else") || cl_eq(t, "end") || cl_eq(t, "do") || cl_eq(t, "case")))
        cl_fail(c, i, "E-FORM-UNEXPECTED", "a statement belongs here");
    if (!kw && cl_eq(t, "pipe") && !cl_dot(c, i + 1)) return cl_term(c, i, false, false);
    return cl_term(c, i, false, true);                   // 호출 문장 — 그 호출의 닫는 점이 문장의 점이다
}

static proven_size_t cl_clause(cl_t *c, proven_size_t i) {
    const low_token_t *t = cl_tk(c, i);
    if (t->kind != LOW_TOK_IDENT) cl_fail(c, i, "E-FORM-UNEXPECTED", "a clause belongs here");
    if (cl_eq(t, "requires") || cl_eq(t, "ensures")) {
        proven_size_t j = i + 1;
        while (cl_id(c, j) && cl_in(&c->T[j], CL_GRADE) && !cl_dot(c, j + 1)) j++;
        return cl_need_dot(c, cl_term(c, j, false, false), "a stop `.` must close the clause") + 1;
    }
    if (cl_eq(t, "input")) {
        proven_size_t j = i + 1;
        if (cl_word(c, j, "comptime")) j++;
        if (!cl_word(c, j + 1, "type") && !cl_dot(c, j + 1)) (void)cl_ty_try(c, j + 1, NOPE, true);
    }
    if (cl_eq(t, "using") && !cl_dot(c, i + 2)) (void)cl_ty_try(c, i + 2, NOPE, true);
    if (cl_eq(t, "output") && !cl_dot(c, i + 1)) {
        proven_size_t d = i + 1;
        while (d < c->n && !cl_dot(c, d) && c->T[d].kind != LOW_TOK_EOF) d++;
        if (!cl_ty_try(c, i + 1, d, true)) (void)cl_ty_try(c, i + 2, d, true);
    }
    if (cl_eq(t, "asm")) {
        proven_size_t j = i;
        while (!cl_kw(c, j, "do") && !cl_kw(c, j, "end")) { cl_tk(c, j); j++; }
        return j;
    }
    proven_size_t j = i + 1;
    for (;;) {
        const low_token_t *k = cl_tk(c, j);
        if (k->kind == LOW_TOK_LPAREN) { j = cl_mate(c, j) + 1; continue; }
        if (k->kind == LOW_TOK_DOT) return j + 1;
        if (cl_kw(c, j, "do") || cl_kw(c, j, "end")) cl_fail(c, i, "E-DOT-MISSING", "a stop `.` must close this clause");
        j++;
    }
}
static void cl_fields(cl_t *c, proven_size_t k, proven_size_t e) {
    while (k < e) {
        proven_size_t d = k;
        while (d < e && !cl_dot(c, d)) d++;
        if (cl_id(c, k) && !cl_eq(&c->T[k], "comptime") && !cl_eq(&c->T[k], "layout") && !cl_eq(&c->T[k], "align") && !cl_eq(&c->T[k], "mmio") && d > k + 1)
            (void)cl_ty_try(c, k + 1, d, true);
        k = d + 1;
    }
}
static proven_size_t cl_op_decl(cl_t *c, proven_size_t i) {
    proven_size_t j = i + 2;
    if (cl_kw(c, j, "do") && c->ext && cl_id(c, j + 1) && cl_in(&c->T[j + 1], CL_CLAUSE)) {   // extern — 블록 안에 절
        proven_size_t e = cl_mate(c, j), k = j + 1;
        while (k < e) k = cl_clause(c, k);
        return cl_endstop(c, e + 1, "a stop `.` must close the declaration");
    }
    while (!cl_kw(c, j, "do")) j = cl_clause(c, j);
    return cl_endstop(c, cl_block(c, j), "a stop `.` must close the declaration (`fn … end .`)");
}
static proven_size_t cl_top(cl_t *c, proven_size_t i) {
    c->ext = false;
    proven_size_t j = i;
    while (cl_kw(c, j, "export") || cl_kw(c, j, "extern") || cl_kw(c, j, "unsafe")) {
        if (cl_kw(c, j, "extern")) c->ext = true;
        j++;
        if (cl_word(c, j, "target")) j += 2;
    }
    const low_token_t *t = cl_tk(c, j);
    if (t->kind != LOW_TOK_IDENT) cl_fail(c, j, "E-TOPLEVEL", "a declaration belongs here");
    bool kw = t->kw != LOW_KW_NONE;
    if (cl_eq(t, "module") || cl_eq(t, "use") || cl_eq(t, "package") || cl_eq(t, "build")) {
        while (!cl_dot(c, j)) { cl_tk(c, j); j++; }
        return j + 1;
    }
    if (kw && cl_eq(t, "def")) {
        const low_token_t *k = cl_tk(c, j + 1);
        if (cl_eq(k, "type") || cl_eq(k, "newtype")) {
            (void)cl_ty_try(c, j + 3, NOPE, false);
            while (!cl_dot(c, j)) { cl_tk(c, j); j++; }
            return j + 1;
        }
        if (!cl_kw(c, j + 3, "do")) cl_fail(c, j + 3, "E-FORM-UNEXPECTED", "`def struct <name> do … end .`");
        proven_size_t e = cl_mate(c, j + 3);
        if (cl_eq(k, "struct")) cl_fields(c, j + 4, e);
        return cl_endstop(c, e + 1, "a stop `.` must close the declaration (`def struct … end .`)");
    }
    if (kw && (cl_eq(t, "fn") || cl_eq(t, "proc"))) return cl_op_decl(c, j);
    if (kw && cl_eq(t, "trait") && cl_kw(c, j + 2, "do")) {
        // 서명 = op 이름 + 절들. 절은 제 점으로 닫히고 그 안의 식도 제 점으로 닫히므로, 절의 점 다음에 오는 «절 낱말이 아닌 이름» 이
        // 곧 다음 서명의 이름이다 — 글자만으로 갈린다(1.7 에서는 갈리지 않던 자리다).
        proven_size_t e = cl_mate(c, j + 2), k = j + 3;
        while (k < e) {
            if (cl_id(c, k) && cl_in(&c->T[k], CL_CLAUSE)) k = cl_clause(c, k);
            else if (cl_id(c, k)) k++;
            else cl_fail(c, k, "E-FORM-UNEXPECTED", "inside `trait` come signatures: `<op name> <clauses>`");
        }
        return cl_endstop(c, e + 1, "a stop `.` must close the declaration (`trait … end .`)");
    }
    if (kw && cl_eq(t, "contract") && cl_kw(c, j + 2, "do")) {
        proven_size_t e = cl_mate(c, j + 2), k = j + 3;
        while (k < e) k = cl_clause(c, k);
        return cl_endstop(c, e + 1, "a stop `.` must close the declaration (`contract … end .`)");
    }
    if (kw && cl_eq(t, "actor") && cl_kw(c, j + 2, "do")) {
        proven_size_t e = cl_mate(c, j + 2), k = j + 3;
        while (k < e) {
            if (cl_kw(c, k, "state") && cl_kw(c, k + 1, "do")) {
                proven_size_t m = cl_mate(c, k + 1);
                cl_fields(c, k + 2, m);
                k = cl_endstop(c, m + 1, "a stop `.` must close `state … end`");
            } else if (cl_kw(c, k, "fn") || cl_kw(c, k, "proc")) k = cl_op_decl(c, k);
            else if (cl_kw(c, k, "export") || cl_kw(c, k, "unsafe")) k++;
            else k = cl_clause(c, k);
        }
        return cl_endstop(c, e + 1, "a stop `.` must close the declaration (`actor … end .`)");
    }
    if (kw && cl_eq(t, "test")) {
        proven_size_t k = j + 2;
        if (cl_word(c, k, "schedule")) { while (!cl_dot(c, k)) { cl_tk(c, k); k++; } k++; }
        return cl_endstop(c, cl_block(c, k), "a stop `.` must close the declaration (`test … end .`)");
    }
    if (kw && (cl_eq(t, "let") || cl_eq(t, "var"))) return cl_stmt(c, j);
    cl_fail(c, j, "E-TOPLEVEL", "this word does not start a declaration");
}

bool low_closer_lower(proven_allocator_t heap, proven_array_t *tokens, proven_array_t *diags) {
    cl_t c = { .T = (low_token_t *)tokens->data, .n = tokens->len };
    if (c.n == 0) return true;
    c.del = (unsigned char *)calloc(c.n, 1);
    c.lp = (unsigned short *)calloc(c.n, sizeof *c.lp);
    c.rp = (unsigned short *)calloc(c.n, sizeof *c.rp);
    c.mate = (proven_size_t *)malloc(c.n * sizeof *c.mate);
    proven_size_t *st = (proven_size_t *)malloc(c.n * sizeof *st);
    bool ok = false;
    if (c.del && c.lp && c.rp && c.mate && st) {
        proven_size_t np = 0, nd = 0;                    // 괄호 · do/end 의 짝 — 두 벌의 더미를 한 배열의 양끝에 둔다
        for (proven_size_t i = 0; i < c.n; i++) c.mate[i] = NOPE;
        for (proven_size_t i = 0; i < c.n; i++) {
            const low_token_t *t = &c.T[i];
            if (t->kind == LOW_TOK_LPAREN) st[np++] = i;
            else if (t->kind == LOW_TOK_RPAREN) { if (np) { proven_size_t j = st[--np]; c.mate[j] = i; c.mate[i] = j; } }
            else if (cl_kw(&c, i, "do")) st[c.n - 1 - nd++] = i;
            else if (cl_kw(&c, i, "end")) { if (nd) { proven_size_t j = st[c.n - nd--]; c.mate[j] = i; } }
        }
        if (setjmp(c.jb) == 0) {
            proven_size_t i = 0;
            while (i < c.n && c.T[i].kind != LOW_TOK_EOF) i = cl_top(&c, i);
            ok = true;
        } else {
            (void)proven_array_push(diags, &c.err);
        }
    }
    if (ok) {
        proven_size_t extra = 0;
        for (proven_size_t i = 0; i < c.n; i++) extra += c.lp[i] + c.rp[i];
        proven_array_t out = PROVEN_ARRAY_INIT(heap, low_token_t, c.n + extra + 1).value;
        for (proven_size_t i = 0; i < c.n; i++) {
            low_token_t t = c.T[i];
            for (unsigned q = 0; q < c.lp[i]; q++) {
                low_token_t p = { .kind = LOW_TOK_LPAREN, .lex = { .ptr = (const proven_byte_t *)"(", .size = 1 }, .line = t.line, .col = t.col };
                (void)proven_array_push(&out, &p);
            }
            if (!c.del[i]) (void)proven_array_push(&out, &t);
            for (unsigned q = 0; q < c.rp[i]; q++) {
                low_token_t p = { .kind = LOW_TOK_RPAREN, .lex = { .ptr = (const proven_byte_t *)")", .size = 1 }, .line = t.line, .col = t.col };
                (void)proven_array_push(&out, &p);
            }
        }
        proven_array_destroy(tokens);
        *tokens = out;
    }
    free(c.del); free(c.lp); free(c.rp); free(c.mate); free(st);
    return ok;
}

// ════════════════════════════════════════════════════════════════════════════════════════════════
// ★★★ **거꾸로 — 서식기가 찍은 글을 새 표면으로 올린다** (`--fmt`).
//
//   서식기는 안쪽 나무를 걸어 «호출마다 괄호로 싼» 글을 찍는다(RFC-0046 R5 — 괄호 = 렌더링 계층). 그 글에서는 호출의 끝이
//   괄호로 정해지므로, 새 표면으로 올리는 데에도 선언이 필요 없다: 괄호를 벗기고 닫는 점을 찍고(`(add a b)` → `add a. b. .`),
//   값 자리의 이름에 점을 붙이고, 블록으로 끝난 것 뒤에 점을 찍는다. 위의 내림(`low_closer_lower`)의 정확한 역이다 —
//   같은 닫힌 어휘를 읽는다. 줄바꿈과 들여쓰기는 서식기가 찍은 그대로 둔다(글자를 끼우고 지울 뿐이다).
//
//   `method` 의 op 이름은 글자로는 마디와 갈리지 않으므로, 내림이 그 토큰에 표시(aux = ".")를 남기고 서식기가 그 이름 뒤에
//   점을 붙여 찍는다. 여기서는 «이름에 붙은 점» 을 그 표시로 읽는다.
typedef struct {
    low_token_t  *T;
    proven_size_t n;
    unsigned char *del, *dot;         // 지운다 · 뒤에 붙은 점 하나
    unsigned short *close, *lp, *rp;  // 뒤에 ` .` 몇 개 · 앞에 `(` · 뒤에 `)`
    proven_size_t *mate;
    bool ext;
    proven_size_t cur;                // 마지막으로 본 자리(못 올렸을 때 어디였는지 말한다)
    jmp_buf jb;
} up_t;

static bool up_id(up_t *u, proven_size_t i) { return i < u->n && u->T[i].kind == LOW_TOK_IDENT; }
static bool up_kw(up_t *u, proven_size_t i, const char *w) { return up_id(u, i) && u->T[i].kw != LOW_KW_NONE && cl_eq(&u->T[i], w); }
static bool up_word(up_t *u, proven_size_t i, const char *w) { return up_id(u, i) && cl_eq(&u->T[i], w); }
static bool up_dot(up_t *u, proven_size_t i) { return i < u->n && u->T[i].kind == LOW_TOK_DOT; }
[[noreturn]] static void up_fail(up_t *u) { longjmp(u->jb, 1); }
static const low_token_t *up_tk(up_t *u, proven_size_t i) { if (i < u->n) u->cur = i; if (i >= u->n || u->T[i].kind == LOW_TOK_EOF) up_fail(u); return &u->T[i]; }
static proven_size_t up_mate(up_t *u, proven_size_t i) { if (i >= u->n || u->mate[i] == NOPE) up_fail(u); return u->mate[i]; }
static bool up_stop(up_t *u, proven_size_t i) {
    if (i >= u->n) return true;
    const low_token_t *t = &u->T[i];
    if (t->kind == LOW_TOK_DOT || t->kind == LOW_TOK_RPAREN || t->kind == LOW_TOK_EOF) return true;
    return t->kind == LOW_TOK_IDENT && t->kw != LOW_KW_NONE &&
           (cl_eq(t, "do") || cl_eq(t, "else") || cl_eq(t, "end") || cl_eq(t, "if") || cl_eq(t, "step") || cl_eq(t, "while") || cl_eq(t, "next"));
}
// 이름에 붙은 점(서식기가 `method` 의 op 이름 뒤에 찍은 표시)인가
static bool up_glued(up_t *u, proven_size_t j) {
    return j > 0 && up_dot(u, j) && u->T[j - 1].kind == LOW_TOK_IDENT && u->T[j - 1].line == u->T[j].line &&
           u->T[j - 1].col + u->T[j - 1].lex.size == u->T[j].col;
}
static bool up_island_op(const low_token_t *t) {
    static const char *const W[] = { "eq","ne","lt","le","gt","ge","and","or", NULL };
    return t->kind == LOW_TOK_OP || (t->kind == LOW_TOK_IDENT && cl_in(t, W));
}
static proven_size_t up_item(up_t *u, proven_size_t i);
static void up_call(up_t *u, proven_size_t h, proven_size_t e);
static proven_size_t up_stmt(up_t *u, proven_size_t i);
static proven_size_t up_block(up_t *u, proven_size_t i);
static proven_size_t up_op_decl(up_t *u, proven_size_t i);

// 타입 자리 [a, b): 한정사 뒤에 낱말이 둘 이상이고 머리가 생성자가 아니면 «인자를 받는 사용자 타입» 이다 — 괄호로 싼다.
static void up_type_span(up_t *u, proven_size_t a, proven_size_t b) {
    while (a < b && up_id(u, a) && cl_in(&u->T[a], CL_QUAL)) a++;
    if (b <= a + 1 || !up_id(u, a)) return;
    const low_token_t *h = &u->T[a];
    if (cl_in(h, CL_T_ZERO) || cl_in(h, CL_T_PRE1) || cl_in(h, CL_T_CTOR)) return;
    u->lp[a]++; u->rp[b - 1]++;
}
static proven_size_t up_tyend(up_t *u, proven_size_t i) {     // 글자만으로 끝나는 타입(바인딩 · 되풀이의 자리)
    const low_token_t *t = up_tk(u, i);
    if (t->kind == LOW_TOK_LPAREN) return up_mate(u, i) + 1;
    if (t->kind != LOW_TOK_IDENT) up_fail(u);
    if (cl_in(t, CL_T_ZERO)) return i + 1;
    if (cl_in(t, CL_T_PRE1)) return up_tyend(u, i + 1);
    if (cl_eq(t, "result")) return up_tyend(u, up_tyend(u, i + 1));
    if (cl_eq(t, "array") || cl_eq(t, "vec")) { proven_size_t j = up_tyend(u, i + 1); up_tk(u, j); return u->T[j].kind == LOW_TOK_LPAREN ? up_mate(u, j) + 1 : j + 1; }
    if (cl_eq(t, "bitset") || cl_eq(t, "mask") || cl_eq(t, "bits")) return (i + 1 < u->n && u->T[i + 1].kind == LOW_TOK_NUMBER) ? i + 2 : i + 1;
    if (cl_eq(t, "cap") || cl_eq(t, "region")) return i + 2;
    return i + 1;
}
static void up_island(up_t *u, proven_size_t j, proven_size_t e) {
    bool operand = true;
    while (j < e) {
        const low_token_t *x = &u->T[j];
        if (operand) {
            if (x->kind == LOW_TOK_LPAREN) {
                proven_size_t m = up_mate(u, j);
                if (j + 2 < m && up_island_op(&u->T[u->T[j + 1].kind == LOW_TOK_LPAREN ? up_mate(u, j + 1) + 1 : j + 2])) up_island(u, j + 1, m);
                else up_call(u, j + 1, m);               // 괄호로 싼 호출 — 섬 안에서는 괄호를 남긴다
                j = m + 1;
            }
            else if (x->kind == LOW_TOK_OP) { j++; continue; }
            else j = up_item(u, j);
            operand = false;
        } else { j++; operand = true; }
    }
}
static proven_size_t up_top(up_t *u, proven_size_t i);
// 서식기는 타입 인자를 받는 구조체 값을 `(lit <이름>) <타입 인자>… do … end` 로 찍는다. 그 꼴이면 읽고 끝 자리를 돌려준다(아니면 NOPE).
static proven_size_t up_generic_lit(up_t *u, proven_size_t i) {
    if (!(i + 3 < u->n && u->T[i].kind == LOW_TOK_LPAREN && up_kw(u, i + 1, "lit") && u->mate[i] == i + 3)) return NOPE;
    proven_size_t j = i + 4;
    while (j < u->n && !up_kw(u, j, "do")) {
        if (u->T[j].kind == LOW_TOK_LPAREN) { j = up_mate(u, j) + 1; continue; }
        if (u->T[j].kind != LOW_TOK_IDENT || u->T[j].kw != LOW_KW_NONE) return NOPE;
        j++;
    }
    if (j >= u->n || j == i + 4) return NOPE;
    u->del[i] = 1; u->del[i + 3] = 1;
    u->lp[i + 2]++; u->rp[j - 1]++;                      // `lit (nest (nest u32)) do …`
    proven_size_t m = up_mate(u, j), k = j + 1;
    while (k < m) { k++; k = up_top(u, k); if (!up_dot(u, k)) up_fail(u); k++; }
    u->close[m]++;
    return m + 1;
}
// 값 자리의 한 마디: 리터럴 · 이름(→ `a.`) · 괄호로 싼 호출(→ 괄호를 벗기고 닫는 점).
static proven_size_t up_item(up_t *u, proven_size_t i) {
    const low_token_t *t = up_tk(u, i);
    if (t->kind == LOW_TOK_LPAREN) {
        proven_size_t g = up_generic_lit(u, i);
        if (g != NOPE) return g;
        proven_size_t j = up_mate(u, i);
        if (j == i + 1) up_fail(u);
        u->del[i] = 1; u->del[j] = 1;
        up_call(u, i + 1, j);
        // 서식기는 원자 연산의 `order <낱말>` 을 괄호 밖에 찍는다 — 그 낱말까지가 호출이다(닫는 점을 그 뒤로 옮긴다).
        if (up_id(u, i + 1) && u->T[i + 1].lex.size > 7 && memcmp(u->T[i + 1].lex.ptr, "atomic_", 7) == 0 &&
            up_word(u, j + 1, "order") && up_id(u, j + 2) && u->close[j - 1]) {
            u->close[j - 1]--; u->close[j + 2]++;
            return j + 3;
        }
        return j + 1;
    }
    if (t->kind == LOW_TOK_NUMBER || t->kind == LOW_TOK_STRING || t->kind == LOW_TOK_CHAR || t->kind == LOW_TOK_TEXTLIT) return i + 1;
    if (t->kind != LOW_TOK_IDENT) up_fail(u);
    if (t->kw != LOW_KW_NONE) {
        if (cl_eq(t, "true") || cl_eq(t, "false") || cl_eq(t, "none")) return i + 1;
        up_fail(u);
    }
    if (cl_eq(t, "_")) return i + 1;
    u->dot[i] = 1;
    return i + 1;
}
static proven_size_t up_open_end(up_t *u, proven_size_t i);
// 인자 자리의 한 마디 — 서식기가 괄호 없이 찍는 값(`lit … do … end` · 섬 · `send` …)도 받는다. 그것은 제 끝까지가 한 항이다.
static proven_size_t up_arg(up_t *u, proven_size_t j, proven_size_t e) {
    const low_token_t *t = up_tk(u, j);
    bool open = t->kind == LOW_TOK_IDENT && ((t->kw != LOW_KW_NONE && (cl_eq(t, "lit") || cl_eq(t, "expr") || cl_eq(t, "send") || cl_eq(t, "spawn") || cl_eq(t, "try"))) ||
                                             (t->kw == LOW_KW_NONE && cl_eq(t, "pipe") && j + 1 < e));
    if (!open) return up_item(u, j);
    proven_size_t x = up_open_end(u, j);
    if (x > e) x = e;
    up_call(u, j, x);
    return x;
}
static void up_items(up_t *u, proven_size_t j, proven_size_t e) { while (j < e) j = up_arg(u, j, e); }
// 값이 시작하는 자리 i 에서 괄호 없이 적힌 값의 끝(다음 토큰의 번호)을 찾는다.
static proven_size_t up_open_end(up_t *u, proven_size_t i) {
    const low_token_t *t = up_tk(u, i);
    if (t->kind == LOW_TOK_IDENT && t->kw != LOW_KW_NONE && cl_eq(t, "lit")) {
        proven_size_t j = i + 1;
        while (j < u->n && !up_kw(u, j, "do") && !up_dot(u, j) && u->T[j].kind != LOW_TOK_RPAREN && u->T[j].kind != LOW_TOK_EOF) {
            if (u->T[j].kind == LOW_TOK_LPAREN) j = up_mate(u, j);
            j++;
        }
        if (up_kw(u, j, "do")) return up_mate(u, j) + 1;
        return up_dot(u, j) ? j + 1 : j;                 // 나열 — 제 점까지
    }
    proven_size_t j = i;
    while (j < u->n) {
        if (u->T[j].kind == LOW_TOK_LPAREN) { j = up_mate(u, j) + 1; continue; }
        if (up_kw(u, j, "do") && up_word(u, i, "pipe") && u->T[i].kw == LOW_KW_NONE) { j = up_mate(u, j) + 1; break; }
        if (up_glued(u, j)) { j++; continue; }
        if (up_stop(u, j)) break;
        j++;
    }
    return j;
}
// 맨 위의 값(문장 · 절 · 칸의 값). 괄호 없이 적힌 호출도 받는다.
static proven_size_t up_top(up_t *u, proven_size_t i) {
    const low_token_t *t = up_tk(u, i);
    if (t->kind == LOW_TOK_LPAREN) return up_item(u, i);
    proven_size_t e = up_open_end(u, i);
    if (e == i + 1) return up_item(u, i);
    up_call(u, i, e);
    return e;
}
// 호출 하나: 머리 h, 그 범위 [h, e). 끝에 닫는 점을 찍는다(인자가 없으면 값의 점).
static void up_call(up_t *u, proven_size_t h, proven_size_t e) {
    const low_token_t *t = up_tk(u, h);
    if (e <= h) up_fail(u);
    if (e == h + 1) { (void)up_item(u, h); return; }     // `(x)` — 값 하나
    if (t->kind != LOW_TOK_IDENT) {                      // `(1 …)` 같은 것은 호출이 아니다
        if (t->kind == LOW_TOK_LPAREN && up_mate(u, h) == e - 1) { (void)up_item(u, h); return; }
        up_fail(u);
    }
    proven_size_t last = e - 1;
    if (t->kw != LOW_KW_NONE) {
        if (cl_eq(t, "lit")) {
            proven_size_t j = h + 1;
            while (j < e && !up_kw(u, j, "do") && !up_dot(u, j)) { if (u->T[j].kind == LOW_TOK_LPAREN) j = up_mate(u, j); j++; }
            if (j < e && up_kw(u, j, "do")) {
                up_type_span(u, h + 1, j);
                proven_size_t m = up_mate(u, j), k = j + 1;
                while (k < m) {
                    k++;
                    k = up_top(u, k);
                    if (!up_dot(u, k)) up_fail(u);
                    k++;
                }
                u->close[m]++;
                return;
            }
            proven_size_t k = up_tyend(u, h + 1);         // 나열 — 닫힌 어휘의 타입이다
            while (k < e && !up_dot(u, k)) k = up_item(u, k);
            if (!(k < e && up_dot(u, k))) u->close[last]++;   // 괄호가 닫던 나열 — 제 점을 찍는다
            return;
        }
        if (cl_eq(t, "expr")) { up_island(u, h + 1, e); u->close[last]++; return; }
        if (cl_eq(t, "send")) { proven_size_t j = up_item(u, h + 1); up_items(u, j + 1, e); u->close[last]++; return; }
        if (cl_eq(t, "spawn")) {
            if (up_kw(u, h + 1, "actor")) { u->close[last]++; return; }
            if (up_kw(u, h + 1, "send")) { up_call(u, h + 1, e); u->close[last]++; return; }
            up_items(u, h + 2, e); u->close[last]++; return;
        }
        if (cl_eq(t, "try")) { (void)up_item(u, h + 1); u->close[last]++; return; }
        if (cl_eq(t, "true") || cl_eq(t, "false") || cl_eq(t, "none")) up_fail(u);
        up_fail(u);
    }
    if (cl_eq(t, "pipe")) {
        proven_size_t j = up_item(u, h + 1);
        if (!up_kw(u, j, "do")) up_fail(u);
        proven_size_t m = up_mate(u, j), k = j + 1;
        while (k < m) {
            const char *sh = up_id(u, k) ? cl_lookup(CL_STAGES, CL_N(CL_STAGES), &u->T[k]) : NULL;
            if (!sh) up_fail(u);
            k++;
            for (; *sh; sh++) { if (*sh == 'V') k = up_item(u, k); else k++; }
            while (!up_dot(u, k) && !up_word(u, k, "with")) k = up_item(u, k);
            if (up_word(u, k, "with")) k = up_top(u, k + 1);
            if (!up_dot(u, k)) up_fail(u);
            k++;
        }
        u->close[m]++;
        return;
    }
    if (cl_eq(t, "call_builtin")) { up_items(u, h + 2, e); u->close[last]++; return; }
    if (cl_eq(t, "alloc_bytes")) {
        proven_size_t j = h + 1;
        if (!up_word(u, j, "capacity")) j = up_item(u, j);
        if (!up_word(u, j, "capacity")) up_fail(u);
        (void)up_item(u, j + 1); u->close[last]++; return;
    }
    if (cl_eq(t, "pop")) {
        proven_size_t j = up_item(u, h + 1);
        if (up_word(u, j, "into")) (void)up_item(u, j + 1);
        u->close[last]++; return;
    }
    if (cl_eq(t, "field")) { (void)up_item(u, h + 1); u->close[last]++; return; }
    if (cl_eq(t, "method")) {                            // 서식기가 op 이름 뒤에 점을 붙여 찍었다
        proven_size_t j = up_item(u, h + 1);
        while (j < e && !up_dot(u, j)) j++;
        if (j >= e) up_fail(u);
        up_items(u, j + 1, e);
        u->close[last]++; return;
    }
    if (cl_eq(t, "stack_new")) { u->close[last]++; return; }
    if (cl_eq(t, "view")) { if (h + 2 < e) (void)up_item(u, h + 2); u->close[last]++; return; }
    const char *sh = cl_lookup(CL_SHAPES, CL_N(CL_SHAPES), t);
    if (sh && !strchr(sh, 'W')) sh = NULL;
    if (sh) {
        proven_size_t j = h + 1;
        for (; *sh && j < e; sh++) {
            if (*sh == 'V') j = up_item(u, j);
            else if (*sh == 'W') j++;
            else if (*sh == 'R') { up_items(u, j, e); j = e; }
        }
        u->close[last]++; return;
    }
    proven_size_t j = h + 1;
    while (j < e) {
        if (t->lex.size > 7 && memcmp(t->lex.ptr, "atomic_", 7) == 0 && up_word(u, j, "order")) { j += 2; continue; }
        j = up_arg(u, j, e);
    }
    u->close[last]++;
}

static proven_size_t up_block(up_t *u, proven_size_t i) {
    if (!up_kw(u, i, "do")) up_fail(u);
    proven_size_t e = up_mate(u, i), k = i + 1;
    if (up_word(u, k, "asm") || u->T[k].kind == LOW_TOK_TEXTLIT) return e + 1;
    while (k < e) k = up_stmt(u, k);
    if (k != e) up_fail(u);
    return e + 1;
}
// i = `else`. 실패 절을 읽고, 그것을 품은 문장의 닫는 점을 찍는다.
static proven_size_t up_fail_clause(up_t *u, proven_size_t i) {
    proven_size_t j = i + 1;
    if (up_kw(u, j, "do")) { j = up_block(u, j); u->close[j - 1]++; return j; }
    if (up_word(u, j, "error") && up_kw(u, j + 2, "do")) { j = up_block(u, j + 2); u->close[j - 1]++; return j; }
    if (up_kw(u, j, "break") || up_kw(u, j, "continue")) j++;
    else if (up_kw(u, j, "return")) j = up_dot(u, j + 1) ? j + 1 : up_top(u, j + 1);
    else if (up_word(u, j, "panic")) j = up_top(u, j + 1);
    else up_fail(u);
    if (!up_dot(u, j)) up_fail(u);
    u->close[j]++;
    return j + 1;
}
static proven_size_t up_stmt(up_t *u, proven_size_t i) {
    const low_token_t *t = up_tk(u, i);
    if (t->kind != LOW_TOK_IDENT) up_fail(u);
    bool kw = t->kw != LOW_KW_NONE;
    if (kw && (cl_eq(t, "let") || cl_eq(t, "var"))) {
        proven_size_t j = i + 2;
        if (up_kw(u, j, "use") || up_kw(u, j, "keep")) { u->dot[j + 1] = 1; j += 2; }
        // 값이 `lit` 로 시작하면 타입을 적지 않는다(서식기는 그 값도 괄호로 싸서 찍는다)
        if (!up_kw(u, j, "lit") && !(j < u->n && u->T[j].kind == LOW_TOK_LPAREN && up_kw(u, j + 1, "lit"))) j = up_tyend(u, j);
        if ((up_dot(u, j) || up_kw(u, j, "else")) && u->T[j - 1].kind == LOW_TOK_RPAREN && !(up_kw(u, i + 2, "use") || up_kw(u, i + 2, "keep") ? false : u->mate[j - 1] == i + 2 && up_kw(u, i + 3, "lit"))) {
            // 서식기는 `view <타입>` 으로 끝나는 타입 뒤의 값을 `(view <타입> <값>)` 으로 한데 묶어 찍는다(`view` 가 연산의 이름이기도
            // 하다). 그 괄호를 벗기면 타입과 값이 제자리로 간다.
            proven_size_t m = up_mate(u, j - 1);
            if (!up_word(u, m + 1, "view") || m + 3 >= j - 1) up_fail(u);
            u->del[m] = 1; u->del[j - 1] = 1;
            if (up_item(u, m + 3) != j - 1) up_fail(u);
        } else
        j = up_top(u, j);
        if (up_kw(u, j, "else")) return up_fail_clause(u, j);
        if (!up_dot(u, j)) up_fail(u);
        return j + 1;
    }
    if (kw && cl_eq(t, "guard")) {
        proven_size_t j = up_top(u, i + 1);
        if (!up_kw(u, j, "else")) up_fail(u);
        return up_fail_clause(u, j);
    }
    if (kw && cl_eq(t, "set")) {
        proven_size_t j = up_item(u, i + 1);
        j = up_top(u, j);
        if (!up_dot(u, j)) up_fail(u);
        return j + 1;
    }
    if (kw && cl_eq(t, "return")) {
        proven_size_t j = up_dot(u, i + 1) ? i + 1 : up_top(u, i + 1);
        if (!up_dot(u, j)) up_fail(u);
        return j + 1;
    }
    if (kw && (cl_eq(t, "break") || cl_eq(t, "continue"))) { if (!up_dot(u, i + 1)) up_fail(u); return i + 2; }
    if (kw && cl_eq(t, "expect")) { proven_size_t j = up_top(u, i + 1); if (!up_dot(u, j)) up_fail(u); return j + 1; }
    if (kw && cl_eq(t, "drop")) { u->dot[i + 1] = 1; if (!up_dot(u, i + 2)) up_fail(u); return i + 3; }
    if (kw && cl_eq(t, "if")) {
        proven_size_t j = i;
        for (;;) {
            j = up_top(u, j + 1); j = up_block(u, j);
            if (up_kw(u, j, "else")) {
                if (up_kw(u, j + 1, "if")) { j++; continue; }
                j = up_block(u, j + 1);
            }
            break;
        }
        u->close[j - 1]++;
        return j;
    }
    if (kw && cl_eq(t, "while")) { proven_size_t j = up_block(u, up_top(u, i + 1)); u->close[j - 1]++; return j; }
    if (kw && (cl_eq(t, "for") || cl_eq(t, "repeat") || cl_eq(t, "range") || cl_eq(t, "cycle"))) {
        proven_size_t j = i + 2;
        if (cl_eq(t, "for")) { if (up_word(u, j, "mut")) j++; j = up_item(u, j); }
        else {
            j = up_tyend(u, j); j = up_item(u, j);
            if (cl_eq(t, "range")) { j = up_item(u, j); if (up_kw(u, j, "step")) j = up_item(u, j + 1); }
            if (cl_eq(t, "cycle")) { j = up_item(u, j + 1); j = up_item(u, j + 1); }
        }
        if (up_kw(u, j, "if")) j = up_item(u, j + 1);
        j = up_block(u, j); u->close[j - 1]++;
        return j;
    }
    if (kw && cl_eq(t, "match")) {
        proven_size_t j = up_word(u, i + 1, "comptime") ? up_item(u, i + 2) : up_top(u, i + 1);
        if (!up_kw(u, j, "do")) up_fail(u);
        proven_size_t e = up_mate(u, j), k = j + 1;
        while (k < e) {
            if (up_kw(u, k, "else")) {
                k = up_block(u, k + 1);
                if (up_dot(u, k)) { u->del[k] = 1; k++; }   // 서식기는 블록 안의 `else` 를 폼으로 닫아 찍는다 — 새 표면에는 그 점이 없다
                continue;
            }
            k++;
            while (k < e && !up_kw(u, k, "do")) { if (up_dot(u, k)) up_fail(u); k++; }
            k = up_block(u, k); u->close[k - 1]++;
        }
        j = e + 1;
        if (up_kw(u, j, "else")) j = up_block(u, j + 1);
        u->close[j - 1]++;
        return j;
    }
    if (cl_eq(t, "region") && up_kw(u, i + 3, "do")) { proven_size_t j = up_block(u, i + 3); u->close[j - 1]++; return j; }
    if (cl_eq(t, "borrow") && up_id(u, i + 1) && !up_dot(u, i + 1) && !up_dot(u, i + 2)) {
        proven_size_t j = up_block(u, up_top(u, i + 2)); u->close[j - 1]++; return j;
    }
    if (cl_eq(t, "task_group") && (up_kw(u, i + 1, "do") || up_kw(u, i + 2, "do"))) {
        proven_size_t j = up_block(u, up_kw(u, i + 1, "do") ? i + 1 : i + 2); u->close[j - 1]++; return j;
    }
    if (kw && (cl_eq(t, "fn") || cl_eq(t, "proc"))) return up_op_decl(u, i);
    if (kw && !cl_eq(t, "send") && !cl_eq(t, "spawn") && !cl_eq(t, "try") && !cl_eq(t, "lit") && !cl_eq(t, "expr")) up_fail(u);
    // 호출 문장 — 문장의 점이 그 호출의 닫는 점이다(따로 찍지 않는다).
    proven_size_t e = up_open_end(u, i);
    if (!kw && cl_eq(t, "pipe") && e > i + 1) { up_call(u, i, e); return e; }
    if (!up_dot(u, e)) up_fail(u);
    if (e > i + 1) { up_call(u, i, e); u->close[e - 1]--; }
    return e + 1;
}
static proven_size_t up_clause(up_t *u, proven_size_t i) {
    const low_token_t *t = up_tk(u, i);
    if (t->kind != LOW_TOK_IDENT) up_fail(u);
    if (cl_eq(t, "requires") || cl_eq(t, "ensures")) {
        proven_size_t j = i + 1;
        while (up_id(u, j) && cl_in(&u->T[j], CL_GRADE) && !up_dot(u, j + 1)) j++;
        j = up_top(u, j);
        if (!up_dot(u, j)) up_fail(u);
        return j + 1;
    }
    proven_size_t d = i + 1;
    if (cl_eq(t, "asm")) { while (!up_kw(u, d, "do") && !up_kw(u, d, "end")) { up_tk(u, d); d++; } return d; }
    for (;;) {
        const low_token_t *k = up_tk(u, d);
        if (k->kind == LOW_TOK_LPAREN) { d = up_mate(u, d) + 1; continue; }
        if (k->kind == LOW_TOK_DOT) break;
        if (up_kw(u, d, "do") || up_kw(u, d, "end")) up_fail(u);
        d++;
    }
    if (cl_eq(t, "input")) {
        proven_size_t j = i + 1;
        if (up_word(u, j, "comptime")) j++;
        if (!up_word(u, j + 1, "type")) up_type_span(u, j + 1, d);
    } else if (cl_eq(t, "using")) up_type_span(u, i + 2, d);
    else if (cl_eq(t, "output") && d > i + 1) {
        // output [<이름>] <타입> — 첫 낱말이 글자만으로 끝나는 타입이면 이름이 없다
        proven_size_t a = i + 1;
        jmp_buf saved; memcpy(saved, u->jb, sizeof saved);
        bool whole = false;
        if (setjmp(u->jb) == 0) { proven_size_t q = a; while (up_id(u, q) && cl_in(&u->T[q], CL_QUAL)) q++; whole = up_tyend(u, q) == d; }
        memcpy(u->jb, saved, sizeof saved);
        if (!whole) {
            const low_token_t *h = &u->T[a];
            bool tyhead = h->kind == LOW_TOK_IDENT && (cl_in(h, CL_T_ZERO) || cl_in(h, CL_T_PRE1) || cl_in(h, CL_T_CTOR) || cl_in(h, CL_QUAL));
            if (tyhead) up_type_span(u, a, d);
            else {                                       // 이름이 있는가 — 둘째부터가 글자만으로 끝나는 타입이면 그렇다
                bool named = false;
                memcpy(saved, u->jb, sizeof saved);
                if (setjmp(u->jb) == 0) { proven_size_t q = a + 1; while (up_id(u, q) && cl_in(&u->T[q], CL_QUAL)) q++; named = d > a + 1 && up_tyend(u, q) == d; }
                memcpy(u->jb, saved, sizeof saved);
                if (named) { /* 이름 + 타입 하나 — 싸지 않는다 */ }
                else up_type_span(u, a, d);
            }
        }
    }
    return d + 1;
}
static void up_fields(up_t *u, proven_size_t k, proven_size_t e) {
    while (k < e) {
        proven_size_t d = k;
        while (d < e && !up_dot(u, d)) d++;
        if (up_id(u, k) && !cl_eq(&u->T[k], "comptime") && !cl_eq(&u->T[k], "layout") && !cl_eq(&u->T[k], "align") && !cl_eq(&u->T[k], "mmio") && d > k + 1)
            up_type_span(u, k + 1, d);
        k = d + 1;
    }
}
static proven_size_t up_op_decl(up_t *u, proven_size_t i) {
    proven_size_t j = i + 2;
    if (up_kw(u, j, "do") && u->ext && up_id(u, j + 1) && cl_in(&u->T[j + 1], CL_CLAUSE)) {
        proven_size_t e = up_mate(u, j), k = j + 1;
        while (k < e) k = up_clause(u, k);
        u->close[e]++;
        return e + 1;
    }
    while (!up_kw(u, j, "do")) j = up_clause(u, j);
    j = up_block(u, j); u->close[j - 1]++;
    return j;
}
static proven_size_t up_topform(up_t *u, proven_size_t i) {
    u->ext = false;
    proven_size_t j = i;
    for (;;) {
        if (up_kw(u, j, "export") || up_kw(u, j, "unsafe")) j++;
        else if (up_kw(u, j, "extern")) { u->ext = true; j++; }
        else if (up_word(u, j, "target") && up_id(u, j + 1) && (up_kw(u, j + 2, "fn") || up_kw(u, j + 2, "proc") || up_kw(u, j + 2, "extern") || up_kw(u, j + 2, "unsafe") || up_kw(u, j + 2, "export"))) j += 2;
        else break;
    }
    const low_token_t *t = up_tk(u, j);
    if (t->kind != LOW_TOK_IDENT) up_fail(u);
    bool kw = t->kw != LOW_KW_NONE;
    if (cl_eq(t, "module") || cl_eq(t, "use") || cl_eq(t, "package") || cl_eq(t, "build")) { while (!up_dot(u, j)) { up_tk(u, j); j++; } return j + 1; }
    if (kw && cl_eq(t, "def")) {
        const low_token_t *k = up_tk(u, j + 1);
        if (cl_eq(k, "type") || cl_eq(k, "newtype")) {
            proven_size_t d = j + 3;
            while (!up_dot(u, d)) { up_tk(u, d); if (u->T[d].kind == LOW_TOK_LPAREN) d = up_mate(u, d); d++; }
            up_type_span(u, j + 3, d);
            return d + 1;
        }
        if (!up_kw(u, j + 3, "do")) up_fail(u);
        proven_size_t e = up_mate(u, j + 3);
        if (cl_eq(k, "struct")) up_fields(u, j + 4, e);
        u->close[e]++;
        return e + 1;
    }
    if (kw && (cl_eq(t, "fn") || cl_eq(t, "proc"))) return up_op_decl(u, j);
    if (kw && (cl_eq(t, "trait") || cl_eq(t, "contract")) && up_kw(u, j + 2, "do")) {
        proven_size_t e = up_mate(u, j + 2), k = j + 3;
        while (k < e) {
            if (up_id(u, k) && cl_in(&u->T[k], CL_CLAUSE)) k = up_clause(u, k);
            else if (cl_eq(t, "trait") && up_id(u, k)) k++;
            else up_fail(u);
        }
        u->close[e]++;
        return e + 1;
    }
    if (kw && cl_eq(t, "actor") && up_kw(u, j + 2, "do")) {
        proven_size_t e = up_mate(u, j + 2), k = j + 3;
        while (k < e) {
            if (up_kw(u, k, "state") && up_kw(u, k + 1, "do")) { proven_size_t m = up_mate(u, k + 1); up_fields(u, k + 2, m); u->close[m]++; k = m + 1; }
            else if (up_kw(u, k, "fn") || up_kw(u, k, "proc")) k = up_op_decl(u, k);
            else if (up_kw(u, k, "export") || up_kw(u, k, "unsafe")) k++;
            else k = up_clause(u, k);
        }
        u->close[e]++;
        return e + 1;
    }
    if (kw && cl_eq(t, "test")) {
        proven_size_t k = j + 2;
        if (up_word(u, k, "schedule")) { while (!up_dot(u, k)) { up_tk(u, k); k++; } k++; }
        k = up_block(u, k); u->close[k - 1]++;
        return k;
    }
    if (kw && (cl_eq(t, "let") || cl_eq(t, "var"))) return up_stmt(u, j);
    up_fail(u);
}

// 서식기가 찍은 글(text, n 바이트)을 새 표면으로 올려 `out` 에 쓴다. 올리지 못하면 false — 그때는 아무것도 쓰지 않는다.
bool low_closer_raise_text(proven_allocator_t heap, const char *text, proven_size_t n, FILE *out) {
    low_lex_result_t lx = low_lex(heap, (proven_u8str_view_t){ .ptr = (const proven_byte_t *)text, .size = n });
    up_t u = { .T = (low_token_t *)lx.tokens.data, .n = lx.tokens.len };
    bool ok = false;
    proven_size_t *ls = NULL, *st = NULL, nl = 0;
    u.del = (unsigned char *)calloc(u.n + 1, 1); u.dot = (unsigned char *)calloc(u.n + 1, 1);
    u.close = (unsigned short *)calloc(u.n + 1, sizeof *u.close);
    u.lp = (unsigned short *)calloc(u.n + 1, sizeof *u.lp); u.rp = (unsigned short *)calloc(u.n + 1, sizeof *u.rp);
    u.mate = (proven_size_t *)malloc((u.n + 1) * sizeof *u.mate);
    st = (proven_size_t *)malloc((u.n + 1) * sizeof *st);
    ls = (proven_size_t *)malloc((n + 2) * sizeof *ls);
    if (lx.ok && u.n && u.del && u.dot && u.close && u.lp && u.rp && u.mate && st && ls) {
        proven_size_t np = 0, nd = 0;
        for (proven_size_t i = 0; i < u.n; i++) u.mate[i] = NOPE;
        for (proven_size_t i = 0; i < u.n; i++) {
            const low_token_t *t = &u.T[i];
            if (t->kind == LOW_TOK_LPAREN) st[np++] = i;
            else if (t->kind == LOW_TOK_RPAREN) { if (np) { proven_size_t j = st[--np]; u.mate[j] = i; u.mate[i] = j; } }
            else if (up_kw(&u, i, "do")) st[u.n - 1 - nd++] = i;
            else if (up_kw(&u, i, "end")) { if (nd) { proven_size_t j = st[u.n - nd--]; u.mate[j] = i; } }
        }
        if (setjmp(u.jb) == 0) {
            proven_size_t i = 0;
            while (i < u.n && u.T[i].kind != LOW_TOK_EOF) i = up_topform(&u, i);
            ok = true;
        } else if (u.cur < u.n) {
            fprintf(stderr, "lowentc: --fmt: the printed form could not be raised near its line %u, `%.*s`\n", (unsigned)u.T[u.cur].line,
                    (int)(u.T[u.cur].lex.size > 30 ? 30 : u.T[u.cur].lex.size), (const char *)u.T[u.cur].lex.ptr);
        }
    }
    if (ok) {
        ls[nl++] = 0;
        for (proven_size_t q = 0; q < n; q++) if (text[q] == '\n') ls[nl++] = q + 1;
        proven_size_t at = 0;
        for (proven_size_t i = 0; i < u.n && u.T[i].kind != LOW_TOK_EOF; i++) {
            const low_token_t *t = &u.T[i];
            proven_size_t off = ls[t->line - 1] + t->col - 1;
            // 이 토큰의 끝 = 다음 토큰의 처음에서 빈칸을 거슬러 올라간 자리
            proven_size_t nxt = (i + 1 < u.n && u.T[i + 1].kind != LOW_TOK_EOF) ? ls[u.T[i + 1].line - 1] + u.T[i + 1].col - 1 : n;
            proven_size_t end = nxt;
            while (end > off && (text[end - 1] == ' ' || text[end - 1] == '\n' || text[end - 1] == '\t' || text[end - 1] == '\r')) end--;
            if (t->kind == LOW_TOK_IDENT || t->kind == LOW_TOK_NUMBER) { proven_size_t e2 = off + t->lex.size; if (e2 < end) end = e2; }
            if (t->kind == LOW_TOK_LPAREN || t->kind == LOW_TOK_RPAREN || t->kind == LOW_TOK_DOT) end = off + 1;
            fwrite(text + at, 1, off - at, out);
            for (unsigned q = 0; q < u.lp[i]; q++) fputc('(', out);
            if (!u.del[i]) fwrite(text + off, 1, end - off, out);
            for (unsigned q = 0; q < u.rp[i]; q++) fputc(')', out);
            if (u.dot[i]) fputc('.', out);
            if (t->kind == LOW_TOK_TEXTLIT && u.close[i]) {      // 닫는 꼬리표의 줄에는 아무것도 못 온다 — 다음 토큰 앞에 찍는다
                fwrite(text + end, 1, nxt - end, out); end = nxt;
                for (unsigned q = 0; q < u.close[i]; q++) fputs(". ", out);
            } else
            for (unsigned q = 0; q < u.close[i]; q++) fputs(" .", out);
            at = end;
        }
        fwrite(text + at, 1, n - at, out);
    }
    free(u.del); free(u.dot); free(u.close); free(u.lp); free(u.rp); free(u.mate); free(st); free(ls);
    proven_array_destroy(&lx.diags); proven_array_destroy(&lx.tokens);
    return ok;
}
