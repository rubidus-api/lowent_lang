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
// ★★★ **하강이 이름으로 읽는 특수형의 모양.** `low_arity.h` 의 두 표(LOW_BUILTINS_CORE · LOW_SHAPES)에 없지만 인자 꼴이 하나로
//   정해진 것들이다. 낱말 자리(W)를 가진 머리는 모두 닫힌 표에 있어야 글자만으로 읽힌다. 사용자 op 의 인자는 전부 항이라 표가
//   필요 없다. (`scripts/closer-migrate.py` 가 이 표를 읽는다 — 사본을 두지 않는다.)
//   ☞ 이 이름들은 `low_arity.h` 의 어휘 표에도, 부록 D 의 표에도 없다 — 하강이 이름으로만 안다. 그 틈은 이 편의 일이 아니다(RFC-0142 §8).
#define LOW_CLOSER_SHAPES(X)                                                   \
    X(some, "V")  X(is_none, "V")  X(arg, "VV")  X(write_out, "VVV")           \
    X(chrecv, "V")  X(chsend, "VV")  X(time_sleep, "VV")  X(time_now, "V")     \
    X(time_local, "V")  X(await, "V")  X(channel, "W")  X(drain, "V")          \
    X(read_in, "VVV")  X(reactor_new, "VVV")  X(r_read, "VVVV")                \
    X(r_write, "VVVV")  X(unsafe_fn, "W")  X(env_get, "VV")                    \
    X(random_bytes, "VV")  X(copy, "VV")  X(isa, "VW")  X(tty_size, "V")       \
    X(tty_read, "VV")  X(tty_raw, "VV")  X(shuffle, "VR")                      \
    X(elem_le, "VV")  X(elem_lt, "VV")  X(elem_ge, "VV")  X(elem_gt, "VV")     \
    X(elem_eq, "VV")  X(elem_ne, "VV")

typedef struct { const char *name; const char *shape; } cl_shape_t;
static const cl_shape_t CL_SHAPES[] = {
#define X(n, w, a) { #n, (a) == 0 ? "" : (a) == 1 ? "V" : (a) == 2 ? "VV" : (a) == 3 ? "VVV" : (a) == 4 ? "VVVV" : "VVVVVV" },
    LOW_BUILTINS_CORE(X)
#undef X
#define X(n, sh) { #n, sh },
    LOW_SHAPES(X)
    LOW_CLOSER_SHAPES(X)
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
    const char *sh = cl_lookup(CL_SHAPES, CL_N(CL_SHAPES), t);
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
    if (kw && cl_eq(t, "trait") && cl_kw(c, j + 2, "do")) return cl_endstop(c, cl_mate(c, j + 2) + 1, "a stop `.` must close the declaration (`trait … end .`)");
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
