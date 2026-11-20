// low_dyn.c — RFC-0159 첫 판: `object <트레이트>` 값과 `dyn` 부름 (2026-11-19 · W-20261119-1840)
//
// ★★★ **새 기계를 짓지 않는다.** 이미 검사되고 두 뒤끝이 오라클로 맞대는 것 — 구조체 · `if` · 보통 부름 — 으로 옮겨 적는다.
//   트레이트 T 를 `object` 로 쓰는 단위마다 처리기가 셋을 짓는다(2026-11-19 W-20261119-2322 부터 · 전엔 열거와 `match`):
//     struct T__object do dyn__tag u32 . v0__<칸> <타입> . … v1__<칸> … end .     ← 값: 갖춘 타입의 번호 + 갖춘 타입 **모두**의 칸(크기 = 합)
//     fn T__object.v<k> input dyn__v <갖춘 타입 k> . output T__object . …          ← 만들기: 제 칸에 값, 남의 칸에 0
//     proc T__object.<op> input dyn__o T__object . <나머지 입력> . output … . effects <트레이트의 효과> dyn . do
//       if eq field dyn__o. dyn__tag . 0 . do return <타입 0>.<op> lit <타입 0> do … end . <인자>… . . end . …   ← 번호로 고른다
//     end .
//   그리고 저자의 글을 옮겨 적는다: 타입 `object T` → `T__object` · 값 `object T v` → `T__object.v<k> v` · `dyn x. m. a .` → `T__object.m x a`.
//   ★ 왜 열거가 아닌가: 갈래에 값이 실린 열거는 바이트 배치가 없어(정본 §6.2.7 (2d)) 원소 나열에 못 담기고 빠른 경로도 못 나른다.
//     납작한 구조체는 둘 다 된다(목록에 담기 · 모든 op 이 빠른 경로). 값은 칸의 합만큼 크다 — 겹쳐 두기(가장 큰 것만큼)는 RFC-0159 §9-15.
//
// ★ 갖춘 타입의 집합은 **번역 단위 안에서 닫힌다** — 저자는 그 집합을 적지 않는다(다른 모듈이 `satisfies T` 로 늘린다 · 이 판은 같은 모듈만).
// ★ 첫 판의 테두리(RFC-0159 §5 · 그 밖은 진단으로 거절한다):
//   · 값을 **담는다**(복사) — 빌림이 아니다. 조각 · 배열 · 칸에 담길 수 있고 수명 규칙이 따로 없다. 크기는 갖춘 타입 모두의 칸의 합이다.
//   · 갖춘 타입의 칸은 낱말 하나의 크기 있는 수 · 부동 · `bool`(`E-DYN-FIELD`). `object` 값의 칸은 `field` 로 읽지 못한다(`E-DYN-FORM`).
//   · 트레이트의 op: 첫 입력이 `self`(값) · `self` 를 다른 자리에 쓰지 않는다 · 나머지 입력과 출력은 낱말 하나의 타입 · comptime · `via` 없음.
//   · `object T v` 의 v 는 타입을 적은 이름이거나 `lit <타입> …` 이다(타입은 짐작하지 않는다).
//   · `dyn` 의 받는 값은 `object T` 로 적은 이름이다. `dyn` 을 부르는 op 은 `proc … effects … dyn .` 이어야 한다(효과 원자 `dyn`).
#include "low_mem.h"
#include "low_fmt.h"
#include "low_cst.h"
#include "low_cst_priv.h"
#include "low_lex.h"
#include "low_closer.h"
#include <string.h>
#include <stdio.h>

#define DY_MAXT   32      // 한 단위에서 `object` 로 쓰는 트레이트
#define DY_MAXI   32      // 트레이트 하나를 갖춘 타입
#define DY_MAXO   16      // 트레이트의 op
#define DY_MAXA    8      // op 하나의 입력(self 제외)
#define DY_MAXE    8      // 효과 낱말
#define DY_MAXF   16      // 갖춘 타입 하나의 칸

typedef struct {
    proven_u8str_view_t name, mod;
    proven_size_t form;                       // 최상위 폼 자리
    proven_size_t nimpl; proven_u8str_view_t impl[DY_MAXI];
    proven_size_t nf[DY_MAXI]; proven_u8str_view_t fn_[DY_MAXI][DY_MAXF], ft[DY_MAXI][DY_MAXF];   // 갖춘 타입의 칸(이름 · 타입)
    proven_size_t nop;
    struct { proven_u8str_view_t name, out; proven_size_t na; proven_u8str_view_t an[DY_MAXA], at[DY_MAXA];
             proven_size_t ne; proven_u8str_view_t eff[DY_MAXE]; } op[DY_MAXO];
    bool ok;
} dy_trait_t;

typedef struct {
    low_parser_t p;
    proven_allocator_t nodes, work;
    low_parse_result_t *pr;
    proven_size_t nt; dy_trait_t t[DY_MAXT];
} dy_t;

static bool dy_atom(const low_cst_t *n) { return n && n->kind == LOW_CST_ATOM; }
static bool dy_is(const low_cst_t *n, const char *s) { return dy_atom(n) && n->tok.kw == LOW_KW_NONE && low_view_eq_cstr(n->tok.lex, s); }
static bool dy_eqv(proven_u8str_view_t a, proven_u8str_view_t b) { return proven_u8str_view_eq(a, b); }
static void dy_err(dy_t *c, const low_cst_t *at, const char *code, const char *msg) {
    low_pdiag(&c->p, code, msg, at ? at->tok.line : 0, at ? at->tok.col : 0);
}
static proven_u8str_view_t dy_cat(dy_t *c, proven_u8str_view_t a, const char *mid, proven_u8str_view_t b) {
    size_t ml = strlen(mid);
    proven_u8 *m = (proven_u8 *)c->nodes.alloc_fn(c->nodes.ctx, a.size + ml + b.size + 1, 1).value.ptr;
    if (!m) return a;
    memcpy(m, a.ptr, a.size); memcpy(m + a.size, mid, ml); if (b.size) memcpy(m + a.size + ml, b.ptr, b.size);
    m[a.size + ml + b.size] = 0;
    return (proven_u8str_view_t){ .ptr = m, .size = a.size + ml + b.size };
}
static const char *dy_dup(dy_t *c, const char *m) {   // 진단 문장은 진단이 사는 동안 살아야 한다(low_pdiag 는 가리키기만 한다)
    size_t n = strlen(m);
    char *d = (char *)c->nodes.alloc_fn(c->nodes.ctx, n + 1, 1).value.ptr;
    if (!d) return "RFC-0159";
    memcpy(d, m, n + 1); return d;
}
static low_cst_t *dy_word(dy_t *c, const low_cst_t *model, proven_u8str_view_t w) {
    low_token_t t = model->tok;
    t.kind = LOW_TOK_IDENT; t.kw = LOW_KW_NONE; t.lex = w; t.aux = (proven_u8str_view_t){ 0 };
    return low_node(&c->p, LOW_CST_ATOM, t);
}
static bool dy_clause(const low_cst_t *n) {
    return dy_is(n, "input") || dy_is(n, "output") || dy_is(n, "effects") || dy_is(n, "using") || dy_is(n, "errors") ||
           dy_is(n, "requires") || dy_is(n, "ensures") || dy_is(n, "link") || dy_is(n, "satisfies") || dy_is(n, "tests") ||
           dy_is(n, "access") || dy_is(n, "inplace");
}
static dy_trait_t *dy_find(dy_t *c, proven_u8str_view_t n) {
    for (proven_size_t i = 0; i < c->nt; i++) if (dy_eqv(c->t[i].name, n)) return &c->t[i];
    return NULL;
}
static const low_cst_t *dy_block(const low_cst_t *f) {
    return f && f->kind == LOW_CST_FORM && f->nkids && f->kids[f->nkids - 1]->kind == LOW_CST_BLOCK ? f->kids[f->nkids - 1] : NULL;
}
static const low_cst_t *dy_find_trait_form(const low_parse_result_t *pr, proven_u8str_view_t name, proven_size_t *at, proven_u8str_view_t *mod) {
    proven_u8str_view_t cur = { 0 };
    for (proven_size_t i = 0; i < pr->nforms; i++) {
        const low_cst_t *f = pr->forms[i];
        if (!f || f->kind != LOW_CST_FORM || f->nkids < 2) continue;
        if (dy_atom(f->kids[0]) && f->kids[0]->tok.kw == LOW_KW_MODULE && dy_atom(f->kids[1])) { cur = f->kids[1]->tok.lex; continue; }
        if (dy_atom(f->kids[0]) && f->kids[0]->tok.kw == LOW_KW_TRAIT && dy_atom(f->kids[1]) && dy_eqv(f->kids[1]->tok.lex, name)) {
            *at = i; *mod = cur; return f;
        }
    }
    return NULL;
}
// 트레이트 T 를 갖춘 구조체(같은 모듈) — 폼 차례대로. 두 단계가 같은 차례를 본다(갈래 번호가 그 차례다).
// ★ 2026-11-19 (W-20261119-2322 · 소유자 «dyn 목록에 담기부터») — `object T` 는 **납작한 구조체**다: 태그 하나 + 갖춘 타입 모두의 칸을
//   나란히(크기 = 칸의 합). 갈래에 값이 실린 열거는 바이트 배치가 없어(정본 §6.2.7 (2d)) 나열에 못 담기지만, 크기 있는 수 · bool 칸만의
//   구조체는 나열 · `for` · 빠른 경로가 이미 나른다. 그래서 갖춘 타입의 칸은 낱말 하나의 크기 있는 수 · 부동 · `bool` 이어야 한다 —
//   0 값을 적을 수 있어야 하기 때문이다(빈 조각 리터럴은 없다). 그 밖은 `E-DYN-FIELD`.
static const char *dy_zero_of(proven_u8str_view_t ty) {
    static const char *const ints[] = { "u8", "u16", "u32", "u64", "i8", "i16", "i32", "i64", "usize", "idiff" };
    for (size_t i = 0; i < sizeof ints / sizeof ints[0]; i++) if (low_view_eq_cstr(ty, ints[i])) return "0";
    if (low_view_eq_cstr(ty, "f32") || low_view_eq_cstr(ty, "f64")) return "0.0";
    if (low_view_eq_cstr(ty, "bool")) return "false";
    return NULL;
}
static void dy_collect_fields(dy_t *c, dy_trait_t *t, proven_size_t k, const low_cst_t *b) {
    t->nf[k] = 0;
    for (proven_size_t q = 0; b && q < b->nkids; q++) {
        const low_cst_t *s = b->kids[q];
        if (!s || s->kind != LOW_CST_FORM || !s->nkids || !dy_atom(s->kids[0]) || s->kids[0]->tok.kw != LOW_KW_NONE) continue;
        if (s->nkids != 2 || !dy_atom(s->kids[1]) || !dy_zero_of(s->kids[1]->tok.lex)) {
            char m[300]; low_bfmt(m, sizeof m, "a type held in `object {}` keeps its fields side by side in one record, so each field is a sized number, a float or "
                                  "`bool` in this version — `{}` is not (RFC-0159 §9-14)", proven_arg_str_view(t->name), proven_arg_str_view(s->kids[0]->tok.lex));
            dy_err(c, s->kids[0], "E-DYN-FIELD", dy_dup(c, m)); t->ok = false; return;
        }
        if (t->nf[k] >= DY_MAXF) { dy_err(c, s->kids[0], "E-DYN-FIELD", "a type held in `object` has too many fields for this version"); t->ok = false; return; }
        t->fn_[k][t->nf[k]] = s->kids[0]->tok.lex; t->ft[k][t->nf[k]] = s->kids[1]->tok.lex; t->nf[k]++;
    }
}
static void dy_collect_impl(dy_t *c, dy_trait_t *t) {
    proven_u8str_view_t cur = { 0 };
    t->nimpl = 0;
    for (proven_size_t i = 0; i < c->pr->nforms; i++) {
        const low_cst_t *f = c->pr->forms[i];
        if (!f || f->kind != LOW_CST_FORM || f->nkids < 2) continue;
        if (dy_atom(f->kids[0]) && f->kids[0]->tok.kw == LOW_KW_MODULE && dy_atom(f->kids[1])) { cur = f->kids[1]->tok.lex; continue; }
        if (!(dy_atom(f->kids[0]) && f->kids[0]->tok.kw == LOW_KW_STRUCT && dy_atom(f->kids[1]))) continue;
        const low_cst_t *b = dy_block(f);
        for (proven_size_t q = 0; b && q < b->nkids; q++) {
            const low_cst_t *s = b->kids[q];
            if (s && s->kind == LOW_CST_FORM && s->nkids == 2 && dy_atom(s->kids[0]) && s->kids[0]->tok.kw == LOW_KW_SATISFIES &&
                dy_atom(s->kids[1]) && dy_eqv(s->kids[1]->tok.lex, t->name)) {
                if (!dy_eqv(cur, t->mod)) { dy_err(c, f->kids[1], "E-DYN-MODULE",
                    "a type that satisfies a trait used with `object` must be in the trait's own module in this first version (RFC-0159 §5)"); t->ok = false; }
                else if (t->nimpl < DY_MAXI) { t->impl[t->nimpl] = f->kids[1]->tok.lex; dy_collect_fields(c, t, t->nimpl, b); t->nimpl++; }
            }
        }
    }
}
// ★ 2026-11-20 (W-20261120-1030 · RFC-0159 §9-12) — 입력과 출력의 타입은 **낱말 여럿**이어도 된다(`slice u8` · `mut slice u8` · `option u64`).
//   지은 글을 다시 읽으므로 낱말을 빈칸으로 이어 한 줄로 둔다. 개수를 받는 타입(`array` · `vec` · `matrix`)은 이 걸음에서 안쪽 차례(`array u8 3`)라
//   그대로 적으면 옛 차례로 거절된다 — 이 판은 받지 않는다. 괄호 · `self` · `comptime` 도 받지 않는다. 못 이으면 빈 뷰를 돌려준다.
static proven_u8str_view_t dy_type_text(dy_t *c, low_cst_t *const *k, proven_size_t a, proven_size_t b) {
    proven_u8str_view_t r = { 0 };
    if (a >= b) return r;
    for (proven_size_t i = a; i < b; i++) {
        if (!dy_atom(k[i]) || k[i]->tok.kind != LOW_TOK_IDENT) return (proven_u8str_view_t){ 0 };
        if (dy_is(k[i], "array") || dy_is(k[i], "vec") || dy_is(k[i], "matrix") || dy_is(k[i], "self") || dy_is(k[i], "comptime"))
            return (proven_u8str_view_t){ 0 };
        r = r.size ? dy_cat(c, r, " ", k[i]->tok.lex) : k[i]->tok.lex;
    }
    return r;
}
// 트레이트의 서명을 읽고 `dyn` 으로 부를 수 있는지 본다(RFC-0159 D3).
static void dy_read_sigs(dy_t *c, dy_trait_t *t, const low_cst_t *tf) {
    const low_cst_t *b = dy_block(tf);
    t->nop = 0;
    for (proven_size_t q = 0; b && q < b->nkids; q++) {
        const low_cst_t *s0 = b->kids[q];
        if (!s0 || s0->kind != LOW_CST_FORM || !s0->nkids || !dy_atom(s0->kids[0]) || dy_clause(s0->kids[0])) continue;
        // 서명 하나 = op 이름으로 여는 폼 + 뒤따라 절 낱말로 여는 폼들(`area input s self .` · `output u64 .` · `effects io .`) — 한 줄로 모은다
        low_cst_t *flat[64]; proven_size_t nflat = 0;
        for (proven_size_t z = 0; z < s0->nkids && nflat < 64; z++) flat[nflat++] = s0->kids[z];
        while (q + 1 < b->nkids && b->kids[q + 1] && b->kids[q + 1]->kind == LOW_CST_FORM && b->kids[q + 1]->nkids &&
               dy_clause(b->kids[q + 1]->kids[0])) {
            q++;
            for (proven_size_t z = 0; z < b->kids[q]->nkids && nflat < 64; z++) flat[nflat++] = b->kids[q]->kids[z];
        }
        low_cst_t sv = *s0; sv.kids = flat; sv.nkids = nflat;
        const low_cst_t *s = &sv;
        if (t->nop >= DY_MAXO) { dy_err(c, s->kids[0], "E-DYN-TRAIT", "a trait used with `object` has too many ops for this first version"); t->ok = false; return; }
        __typeof__(t->op[0]) *o = &t->op[t->nop];
        memset(o, 0, sizeof *o);
        o->name = s->kids[0]->tok.lex;
        o->out = (proven_u8str_view_t){ .ptr = (const proven_u8 *)"void", .size = 4 };
        proven_size_t k = 1; int nin = 0; bool bad = false; const char *why = NULL;
        while (k < s->nkids && !bad) {
            const low_cst_t *w = s->kids[k];
            if (dy_is(w, "input")) {
                proven_size_t e = k + 1; while (e < s->nkids && !dy_clause(s->kids[e])) e++;
                if (e - k < 3 || !dy_atom(s->kids[k + 1]) || !dy_atom(s->kids[k + 2])) { bad = true; why = "every input is `input <name> <type> .`"; break; }
                bool self_ = dy_is(s->kids[k + 2], "self");
                if (nin == 0 && !self_) { bad = true; why = "the first input must be `self` — the value the table is chosen by"; break; }
                if (nin == 0 && e - k != 3) { bad = true; why = "the first input is `input <name> self .` — `self` alone, the value the table is chosen by"; break; }
                if (nin > 0 && self_) { bad = true; why = "`self` may stand only in the first input — the size of the other value is not known"; break; }
                if (dy_is(s->kids[k + 1], "comptime")) { bad = true; why = "a `comptime` input cannot be chosen at run time"; break; }
                if (nin > 0) {
                    if (o->na >= DY_MAXA) { bad = true; why = "too many inputs for this first version"; break; }
                    proven_u8str_view_t ty = dy_type_text(c, s->kids, k + 2, e);
                    if (!ty.size) { bad = true; why = "an input's type is a run of words without a count — `slice u8` · `mut slice u8` · `option u64` (not `array` · `vec` · `matrix` in this version)"; break; }
                    o->an[o->na] = s->kids[k + 1]->tok.lex; o->at[o->na] = ty; o->na++;
                }
                nin++; k = e;
            } else if (dy_is(w, "output")) {
                proven_size_t e = k + 1; while (e < s->nkids && !dy_clause(s->kids[e])) e++;
                if (e - k < 2 || !dy_atom(s->kids[k + 1])) { bad = true; why = "the output is `output <type> .`"; break; }
                for (proven_size_t z = k + 1; z < e; z++)
                    if (dy_is(s->kids[z], "self")) { bad = true; why = "an op chosen at run time cannot give back `self` — its size is not known"; break; }
                if (bad) break;
                o->out = dy_type_text(c, s->kids, k + 1, e);
                if (!o->out.size) { bad = true; why = "the output's type is a run of words without a count — `u64` · `option u64` (not `array` · `vec` · `matrix` in this version)"; break; }
                k = e;
            } else if (dy_is(w, "effects")) {
                proven_size_t e = k + 1;
                for (; e < s->nkids && !dy_clause(s->kids[e]); e++) {
                    if (!dy_atom(s->kids[e])) { bad = true; break; }
                    if (dy_is(s->kids[e], "via")) { bad = true; why = "`via self` leaves the effects to the type, which is chosen at run time — not for `dyn` (RFC-0159 D3)"; break; }
                    if (dy_is(s->kids[e], "none")) continue;
                    if (o->ne < DY_MAXE) o->eff[o->ne++] = s->kids[e]->tok.lex;
                }
                k = e;
            } else { bad = true; why = "only `input` · `output` · `effects` in a signature used with `object` (this first version)"; }
        }
        if (!bad && nin == 0) { bad = true; why = "the first input must be `self` — the value the table is chosen by"; }
        if (bad) {
            char m[320]; low_bfmt(m, sizeof m, "trait `{}` cannot be used with `object`: op `{}` — {} (RFC-0159 D3)",
                                  proven_arg_str_view(t->name), proven_arg_str_view(o->name), proven_arg_cstr(why ? why : "unsupported signature"));
            dy_err(c, s->kids[0], "E-DYN-TRAIT", dy_dup(c, m));
            t->ok = false; return;
        }
        t->nop++;
    }
    if (!t->nop) { dy_err(c, tf->kids[1], "E-DYN-TRAIT", "a trait used with `object` must list at least one op"); t->ok = false; }
}

// ── ⓐ 짓기 — 표면을 내린 직후(선언 차례 패스 앞) ───────────────────────────────────────────────────────────────
static void dy_scan_uses(dy_t *c, const low_cst_t *nd) {
    if (!nd || nd->kind == LOW_CST_ATOM) return;
    for (proven_size_t i = 0; i + 1 < nd->nkids; i++)
        if (dy_is(nd->kids[i], "object") && dy_atom(nd->kids[i + 1]) && !dy_find(c, nd->kids[i + 1]->tok.lex) && c->nt < DY_MAXT) {
            dy_trait_t *t = &c->t[c->nt]; memset(t, 0, sizeof *t);
            t->name = nd->kids[i + 1]->tok.lex; t->ok = true;
            const low_cst_t *tf = dy_find_trait_form(c->pr, t->name, &t->form, &t->mod);
            if (!tf) {
                char m[200]; low_bfmt(m, sizeof m, "`object {}` — `{}` is not a trait declared at the top of a module (RFC-0159)",
                                      proven_arg_str_view(t->name), proven_arg_str_view(t->name));
                dy_err(c, nd->kids[i + 1], "E-DYN-TRAIT", dy_dup(c, m));
                t->ok = false;
            }
            c->nt++;
            if (tf) { dy_read_sigs(c, t, tf); if (t->ok) dy_collect_impl(c, t); }
            if (tf && t->ok && !t->nimpl) {
                char m[200]; low_bfmt(m, sizeof m, "`object {}` — no type in this unit says `satisfies {}`, so no value can be made", proven_arg_str_view(t->name), proven_arg_str_view(t->name));
                dy_err(c, nd->kids[i + 1], "E-DYN-NOIMPL", dy_dup(c, m)); t->ok = false;
            }
        }
    for (proven_size_t i = 0; i < nd->nkids; i++) dy_scan_uses(c, nd->kids[i]);
}
typedef struct { char *b; size_t n, cap; bool full; } dy_buf_t;
static void dy_put(dy_buf_t *o, const char *s, size_t n) { if (o->n + n + 1 > o->cap) { o->full = true; return; } memcpy(o->b + o->n, s, n); o->n += n; o->b[o->n] = 0; }
static void dy_ps(dy_buf_t *o, const char *s) { dy_put(o, s, strlen(s)); }
static void dy_pv(dy_buf_t *o, proven_u8str_view_t v) { dy_put(o, (const char *)v.ptr, v.size); }

static void dy_pk(dy_buf_t *o, proven_size_t k) { char v[24]; low_bfmt(v, sizeof v, "{}", PROVEN_ARG((size_t)k)); dy_ps(o, v); }
// 칸 이름: `v<k>__<칸>` · 태그 `dyn__tag`(u32 · 갖춘 타입의 번호)
static void dy_fld(dy_buf_t *o, proven_size_t k, proven_u8str_view_t f) { dy_ps(o, "v"); dy_pk(o, k); dy_ps(o, "__"); dy_pv(o, f); }
// 갖춘 타입 k 의 값을 납작한 칸에서 다시 짓는다: `lit <타입 k> do a field dyn__o. v<k>__a . . … end .`
static void dy_rebuild(dy_buf_t *o, const dy_trait_t *t, proven_size_t k) {
    dy_ps(o, "lit "); dy_pv(o, t->impl[k]); dy_ps(o, " do");
    for (proven_size_t f = 0; f < t->nf[k]; f++) { dy_ps(o, " "); dy_pv(o, t->fn_[k][f]); dy_ps(o, " field dyn__o. "); dy_fld(o, k, t->fn_[k][f]); dy_ps(o, " . ."); }
    dy_ps(o, " end .");
}
static void dy_gen_text(dy_t *c, const dy_trait_t *t, dy_buf_t *o) {
    dy_ps(o, "struct "); dy_pv(o, t->name); dy_ps(o, "__object do\n  dyn__tag u32 .\n");
    for (proven_size_t k = 0; k < t->nimpl; k++)
        for (proven_size_t f = 0; f < t->nf[k]; f++) { dy_ps(o, "  "); dy_fld(o, k, t->fn_[k][f]); dy_ps(o, " "); dy_pv(o, t->ft[k][f]); dy_ps(o, " .\n"); }
    dy_ps(o, "end .\n");
    // 만들기 — 갖춘 타입마다 하나: 제 칸에 값을, 남의 칸에 0 을
    for (proven_size_t k = 0; k < t->nimpl; k++) {
        dy_ps(o, "fn "); dy_pv(o, t->name); dy_ps(o, "__object.v"); dy_pk(o, k); dy_ps(o, " input dyn__v "); dy_pv(o, t->impl[k]);
        dy_ps(o, " . output "); dy_pv(o, t->name); dy_ps(o, "__object . do\n  return lit "); dy_pv(o, t->name); dy_ps(o, "__object do dyn__tag "); dy_pk(o, k); dy_ps(o, " .");
        for (proven_size_t j = 0; j < t->nimpl; j++)
            for (proven_size_t f = 0; f < t->nf[j]; f++) {
                dy_ps(o, " "); dy_fld(o, j, t->fn_[j][f]);
                if (j == k) { dy_ps(o, " field dyn__v. "); dy_pv(o, t->fn_[j][f]); dy_ps(o, " . ."); }
                else { dy_ps(o, " "); dy_ps(o, dy_zero_of(t->ft[j][f])); dy_ps(o, " ."); }
            }
        dy_ps(o, " end . .\nend .\n");
    }
    // 고르기 — 트레이트의 op 마다 하나: 태그로 갖춘 타입을 골라 그 값을 다시 짓고 그 타입의 op 을 부른다(마지막 갈래는 조건 없이)
    for (proven_size_t q = 0; q < t->nop; q++) {
        const __typeof__(t->op[0]) *op = &t->op[q];
        bool vd = low_view_eq_cstr(op->out, "void");
        dy_ps(o, "proc "); dy_pv(o, t->name); dy_ps(o, "__object."); dy_pv(o, op->name);
        dy_ps(o, " input dyn__o "); dy_pv(o, t->name); dy_ps(o, "__object .");
        for (proven_size_t a = 0; a < op->na; a++) { dy_ps(o, " input "); dy_pv(o, op->an[a]); dy_ps(o, " "); dy_pv(o, op->at[a]); dy_ps(o, " ."); }
        dy_ps(o, " output "); dy_pv(o, op->out); dy_ps(o, " . effects");
        for (proven_size_t e = 0; e < op->ne; e++) { dy_ps(o, " "); dy_pv(o, op->eff[e]); }
        dy_ps(o, " dyn . do\n");
        for (proven_size_t k = 0; k < t->nimpl; k++) {
            bool last = k + 1 == t->nimpl;
            if (!last) { dy_ps(o, "  if eq field dyn__o. dyn__tag . "); dy_pk(o, k); dy_ps(o, " . do "); } else dy_ps(o, "  ");
            if (!vd) dy_ps(o, "return ");
            dy_pv(o, t->impl[k]); dy_ps(o, "."); dy_pv(o, op->name); dy_ps(o, " "); dy_rebuild(o, t, k);
            for (proven_size_t a = 0; a < op->na; a++) { dy_ps(o, " "); dy_pv(o, op->an[a]); dy_ps(o, "."); }
            dy_ps(o, " .");                                   // 부름의 점
            if (!vd) dy_ps(o, " .");                          // `return` 의 점
            else if (!last) dy_ps(o, " return .");
            dy_ps(o, last ? "\n" : " end .\n");
        }
        dy_ps(o, "end .\n");
    }
    (void)c;
}

bool low_dyn_generate(low_parse_result_t *pr, proven_allocator_t node_alloc, proven_allocator_t work, proven_size_t *nforms0) {
    dy_t *c = (dy_t *)work.alloc_fn(work.ctx, sizeof(dy_t), alignof(dy_t)).value.ptr;
    if (!c) return true;
    proven_size_t nd0 = pr->diags.len;
    memset(c, 0, sizeof *c);
    c->p = (low_parser_t){ .node_alloc = node_alloc, .work = work, .out = pr };
    c->nodes = node_alloc; c->work = work; c->pr = pr;
    for (proven_size_t i = 0; i < pr->nforms; i++) dy_scan_uses(c, pr->forms[i]);
    if (!c->nt) { work.free_fn(work.ctx, c); return pr->diags.len == nd0; }
    // 이름이 이미 쓰였으면 짓지 않는다 — 저자의 `T__object` 를 덮어쓰지 않는다
    for (proven_size_t k = 0; k < c->nt; k++) {
        dy_trait_t *t = &c->t[k]; if (!t->ok) continue;
        proven_u8str_view_t gn = dy_cat(c, t->name, "__object", (proven_u8str_view_t){ 0 });
        for (proven_size_t i = 0; i < pr->nforms; i++) {
            const low_cst_t *f = pr->forms[i];
            if (f && f->kind == LOW_CST_FORM && f->nkids >= 2 && dy_atom(f->kids[1]) && dy_eqv(f->kids[1]->tok.lex, gn)) {
                dy_err(c, f->kids[1], "E-DYN-NAME", "this name is the one the processor gives the values of `object <trait>` — choose another (RFC-0159)");
                t->ok = false; break;
            }
        }
    }
    proven_array_t nf = PROVEN_ARRAY_INIT(work, low_cst_t *, pr->nforms + 8 * c->nt).value;
    proven_size_t n0 = nforms0 ? *nforms0 : 0, add0 = 0;
    for (proven_size_t i = 0; i < pr->nforms; i++) {
        (void)PROVEN_ARRAY_PUSH(&nf, low_cst_t *, pr->forms[i]);
        for (proven_size_t k = 0; k < c->nt; k++) {
            dy_trait_t *t = &c->t[k];
            if (!t->ok || t->form != i) continue;
            proven_size_t cap = 4096 + 512 * t->nimpl * (t->nop + 1);
            char *txt = (char *)node_alloc.alloc_fn(node_alloc.ctx, cap, 1).value.ptr;   // 토큰이 이 글을 가리킨다 — 마디 아레나에 둔다
            if (!txt) continue;
            dy_buf_t o = { txt, 0, cap, false }; txt[0] = 0;
            dy_gen_text(c, t, &o);
            if (o.full) { dy_err(c, pr->forms[i]->kids[1], "E-DYN-TRAIT", "internal: the generated table did not fit"); continue; }
            low_lex_result_t lx = low_lex(work, (proven_u8str_view_t){ .ptr = (const proven_byte_t *)txt, .size = o.n });
            bool ok = lx.ok && lx.diags.len == 0 && low_closer_lower(work, &lx.tokens, &lx.diags) && lx.diags.len == 0;
            low_parse_result_t sub = { 0 };
            if (ok) { sub = low_parse(node_alloc, work, &lx.tokens); ok = sub.ok && sub.diags.len == 0; }
            if (ok) low_surface_lower(&sub, node_alloc, work);
            if (!ok) {
                const low_diag_t *d0 = lx.diags.len ? (const low_diag_t *)lx.diags.data : (sub.diags.len ? (const low_diag_t *)sub.diags.data : NULL);
                char m[320]; low_bfmt(m, sizeof m, "internal: the processor could not read the table it built for `object {}` ({}) — this is a compiler bug",
                                      proven_arg_str_view(t->name), proven_arg_cstr(d0 ? d0->code : "?"));
                dy_err(c, pr->forms[i]->kids[1], "E-IR-SURFACE", dy_dup(c, m));   // 내부 실패 — 처리기의 결함이다
            } else {
                for (proven_size_t q = 0; q < sub.nforms; q++) { (void)PROVEN_ARRAY_PUSH(&nf, low_cst_t *, sub.forms[q]); if (i < n0) add0++; }
            }
            proven_array_destroy(&lx.tokens); proven_array_destroy(&lx.diags);
            if (sub.diags.data) proven_array_destroy(&sub.diags);
        }
    }
    pr->forms = (low_cst_t **)nf.data;
    pr->nforms = nf.len;
    if (nforms0) *nforms0 += add0;
    work.free_fn(work.ctx, c);
    return pr->diags.len == nd0;   // ★ 거절했으면 뒤따르는 진단은 헛것이다 — 부르는 쪽이 폼을 비운다
}

// ── ⓑ 옮겨 적기 — 선언 차례 패스 뒤(`let a <타입…> be <값…>`) ─────────────────────────────────────────────────────
typedef struct { proven_u8str_view_t name, ty; bool obj, seq; } dy_decl_t;   // seq: 원소가 `object <ty>` 인 조각 · 배열
typedef struct { dy_decl_t d[256]; proven_size_t n; bool has_dyn_eff; bool is_fn; bool ffi; bool actor; const low_cst_t *head; } dy_scope_t;

static const dy_decl_t *dy_lookup(const dy_scope_t *sc, proven_u8str_view_t n);
// `slice object T` · `array object T N`(안쪽 차례) · `lit slice|array object T …` — 원소가 `object T` 인 줄이면 T 를 돌려준다
static const low_cst_t *dy_seq_trait(dy_t *c, low_cst_t *const *k, proven_size_t i, proven_size_t n) {
    if (i < n && dy_atom(k[i]) && k[i]->tok.kw == LOW_KW_LIT) i++;
    if (i + 2 < n + 0 && (dy_is(k[i], "slice") || dy_is(k[i], "array")) && dy_is(k[i + 1], "object") && dy_atom(k[i + 2]) && dy_find(c, k[i + 2]->tok.lex))
        return k[i + 2];
    return NULL;
}
static void dy_collect_decls(dy_t *c, const low_cst_t *nd, dy_scope_t *sc) {
    if (!nd || nd->kind == LOW_CST_ATOM) return;
    // ★ 2026-11-19 (W-20261119-2322) — 원소가 `object T` 인 줄을 적은 이름, 그리고 그 줄을 도는 `for x <줄>.` 의 x
    if (nd->kind == LOW_CST_FORM && nd->nkids >= 3 && dy_atom(nd->kids[0]) && dy_atom(nd->kids[1]) && sc->n < 256) {
        low_kw_t k0 = nd->kids[0]->tok.kw;
        if (k0 == LOW_KW_LET || k0 == LOW_KW_VAR) {
            const low_cst_t *tr = dy_seq_trait(c, nd->kids, 2, nd->nkids);
            if (!tr && nd->nkids > 2 && nd->kids[2]->kind == LOW_CST_FORM) tr = dy_seq_trait(c, nd->kids[2]->kids, 0, nd->kids[2]->nkids);
            if (!tr) for (proven_size_t q = 2; q < nd->nkids && !tr; q++)   // `var ys lit array object T N …` — 값이 `be` 뒤에 있다
                if (dy_atom(nd->kids[q]) && nd->kids[q]->tok.kw == LOW_KW_BE && q + 1 < nd->nkids) {
                    const low_cst_t *v = nd->kids[q + 1];
                    tr = v->kind == LOW_CST_FORM ? dy_seq_trait(c, v->kids, 0, v->nkids) : dy_seq_trait(c, nd->kids, q + 1, nd->nkids);
                }
            if (tr) { sc->d[sc->n++] = (dy_decl_t){ nd->kids[1]->tok.lex, tr->tok.lex, false, true }; }
        }
        if (k0 == LOW_KW_FOR && dy_atom(nd->kids[2])) {
            const dy_decl_t *d = dy_lookup(sc, nd->kids[2]->tok.lex);
            if (d && d->seq) sc->d[sc->n++] = (dy_decl_t){ nd->kids[1]->tok.lex, d->ty, true, false };
        }
    }
    if (nd->kind == LOW_CST_FORM && nd->nkids >= 4 && dy_atom(nd->kids[0]) &&
        (nd->kids[0]->tok.kw == LOW_KW_LET || nd->kids[0]->tok.kw == LOW_KW_VAR) && dy_atom(nd->kids[1]) && sc->n < 256) {
        if (dy_is(nd->kids[2], "object") && dy_atom(nd->kids[3]) && dy_find(c, nd->kids[3]->tok.lex))
            sc->d[sc->n++] = (dy_decl_t){ nd->kids[1]->tok.lex, nd->kids[3]->tok.lex, true, false };
        else if (dy_atom(nd->kids[2]) && nd->nkids > 3 && dy_atom(nd->kids[3]) && nd->kids[3]->tok.kw == LOW_KW_BE)
            sc->d[sc->n++] = (dy_decl_t){ nd->kids[1]->tok.lex, nd->kids[2]->tok.lex, false, false };
    }
    // op 머리의 `input <이름> <타입>`
    for (proven_size_t i = 0; i + 2 < nd->nkids && sc->n < 256; i++)
        if (dy_is(nd->kids[i], "input") && dy_atom(nd->kids[i + 1]) && dy_atom(nd->kids[i + 2])) {
            const low_cst_t *tr = dy_seq_trait(c, nd->kids, i + 2, nd->nkids);
            if (tr) sc->d[sc->n++] = (dy_decl_t){ nd->kids[i + 1]->tok.lex, tr->tok.lex, false, true };
            else if (dy_is(nd->kids[i + 2], "object") && i + 3 < nd->nkids && dy_atom(nd->kids[i + 3]) && dy_find(c, nd->kids[i + 3]->tok.lex))
                sc->d[sc->n++] = (dy_decl_t){ nd->kids[i + 1]->tok.lex, nd->kids[i + 3]->tok.lex, true, false };
            else if (i + 3 >= nd->nkids || dy_clause(nd->kids[i + 3]) || nd->kids[i + 3]->kind == LOW_CST_BLOCK)
                sc->d[sc->n++] = (dy_decl_t){ nd->kids[i + 1]->tok.lex, nd->kids[i + 2]->tok.lex, false, false };
        }
    for (proven_size_t i = 0; i < nd->nkids; i++)
        if (nd->kids[i]->kind != LOW_CST_ATOM && !(nd->kind == LOW_CST_FORM && i == 0)) dy_collect_decls(c, nd->kids[i], sc);
}
static const dy_decl_t *dy_lookup(const dy_scope_t *sc, proven_u8str_view_t n) {
    for (proven_size_t i = sc->n; i-- > 0; ) if (dy_eqv(sc->d[i].name, n)) return &sc->d[i];
    return NULL;
}
// 값의 타입(짐작하지 않는다): 타입을 적은 이름 · `lit <타입> …`
static bool dy_value_type(const dy_scope_t *sc, const low_cst_t *v, proven_u8str_view_t *ty) {
    while (v && v->kind == LOW_CST_GROUP && v->nkids == 1) v = v->kids[0];
    if (dy_atom(v)) { const dy_decl_t *d = dy_lookup(sc, v->tok.lex); if (d && !d->obj) { *ty = d->ty; return true; } return false; }
    if (v && v->kind == LOW_CST_FORM && v->nkids >= 2 && dy_atom(v->kids[0]) && v->kids[0]->tok.kw == LOW_KW_LIT) {
        const low_cst_t *tn = v->kids[1];
        if (tn->kind == LOW_CST_FORM && tn->nkids && dy_atom(tn->kids[0])) tn = tn->kids[0];   // `lit rect do … end` — 머리가 타입
        if (dy_atom(tn)) { *ty = tn->tok.lex; return true; }
    }
    return false;
}
static void dy_rewrite(dy_t *c, low_cst_t *nd, dy_scope_t *sc) {
    if (!nd || nd->kind == LOW_CST_ATOM) return;
    for (proven_size_t i = 0; i < nd->nkids; i++) dy_rewrite(c, nd->kids[i], sc);   // 안쪽부터
    // ★ 2026-11-19 (W-20261119-2322) — `object` 는 납작한 구조체지만 그 칸은 처리기의 것이다: `field o. v0__w .` 로 읽으면 원래의 타입을
    //   되찾는 길이 생긴다(정본 §6.11.5 (5) «되찾는 길은 없다»). 그래서 `object` 로 적은 이름의 칸은 읽지도 쓰지도 못한다.
    for (proven_size_t i = 0; i + 1 < nd->nkids; i++) {   // 이 걸음에서는 `return field a v` 처럼 낱말이 아직 한 줄에 있다
        if (!dy_is(nd->kids[i], "field") || !dy_atom(nd->kids[i + 1])) continue;
        const dy_decl_t *d = dy_lookup(sc, nd->kids[i + 1]->tok.lex);
        if (d && (d->obj || d->seq)) dy_err(c, nd->kids[i], "E-DYN-FORM", "a value of `object <trait>` has no fields to read — what it holds is reached only through `dyn` (RFC-0159 · canon 6.11.5 (5))");
    }
    // 안쪽 차례 패스가 낱말 둘의 타입을 괄호로 묶었다(`array (object T) N`) — 옮겨 적으면 낱말 하나(`T__object`)이므로 괄호를 벗긴다
    for (proven_size_t i = 0; i < nd->nkids; i++) {
        low_cst_t *g = nd->kids[i];
        if (g && g->kind == LOW_CST_GROUP && g->nkids == 1 && g->kids[0]->kind == LOW_CST_FORM && g->kids[0]->nkids == 1 && dy_atom(g->kids[0]->kids[0])) {
            proven_u8str_view_t w = g->kids[0]->kids[0]->tok.lex;
            for (proven_size_t q = 0; q < c->nt; q++)
                if (c->t[q].ok && w.size == c->t[q].name.size + 8 && memcmp(w.ptr, c->t[q].name.ptr, c->t[q].name.size) == 0 &&
                    memcmp(w.ptr + c->t[q].name.size, "__object", 8) == 0) { nd->kids[i] = g->kids[0]->kids[0]; break; }
        }
    }
    bool bind = nd->kind == LOW_CST_FORM && nd->nkids >= 2 && dy_atom(nd->kids[0]) && (nd->kids[0]->tok.kw == LOW_KW_LET || nd->kids[0]->tok.kw == LOW_KW_VAR);
    proven_size_t be = nd->nkids;
    if (bind) for (proven_size_t i = 2; i < nd->nkids; i++) if (dy_atom(nd->kids[i]) && nd->kids[i]->tok.kw == LOW_KW_BE) { be = i; break; }
    low_cst_t *out[512]; proven_size_t no = 0; bool changed = false;
    for (proven_size_t i = 0; i < nd->nkids && no < 510; i++) {
        low_cst_t *k = nd->kids[i];
        // dyn <받는 값> <op 이름> <인자…>
        bool eff_word = false;                        // `effects … dyn .` 의 `dyn` 은 효과 낱말이다 — 부름이 아니다
        if (dy_atom(k) && k->tok.kw == LOW_KW_DYN)
            for (proven_size_t b = i; b-- > 0; ) {
                if (!dy_atom(nd->kids[b])) break;
                if (dy_is(nd->kids[b], "effects")) { eff_word = true; break; }
                if (dy_clause(nd->kids[b])) break;
            }
        // 선언 낱말 바로 뒤의 `dyn` 은 이름 자리다(`module dyn .`) — 예약어를 이름으로 쓴 것은 뒤의 E-NAME-KEYWORD 가 말한다
        if (dy_atom(k) && k->tok.kw == LOW_KW_DYN && i > 0 && dy_atom(nd->kids[i - 1])) {
            low_kw_t pk = nd->kids[i - 1]->tok.kw;
            if (pk == LOW_KW_MODULE || pk == LOW_KW_FN || pk == LOW_KW_PROC || pk == LOW_KW_STRUCT || pk == LOW_KW_ENUM ||
                pk == LOW_KW_TRAIT || pk == LOW_KW_ACTOR || pk == LOW_KW_TYPE || pk == LOW_KW_NEWTYPE ||
                pk == LOW_KW_LET || pk == LOW_KW_VAR || pk == LOW_KW_USE) eff_word = true;
        }
        if (dy_atom(k) && k->tok.kw == LOW_KW_DYN && !eff_word) {
            if (i + 2 >= nd->nkids || !dy_atom(nd->kids[i + 1]) || !dy_atom(nd->kids[i + 2])) {
                dy_err(c, k, "E-DYN-RECV", "`dyn <value> <op name>. <arguments> .` — the value is a name written with type `object <trait>` in this first version"); out[no++] = k; continue;
            }
            const dy_decl_t *d = dy_lookup(sc, nd->kids[i + 1]->tok.lex);
            dy_trait_t *t = d && d->obj ? dy_find(c, d->ty) : NULL;
            if (!t) { dy_err(c, nd->kids[i + 1], "E-DYN-RECV", "`dyn` chooses the op by the value's type at run time — the value must be a name written with type `object <trait>` (RFC-0159)"); out[no++] = k; continue; }
            if (!t->ok) { out[no++] = k; continue; }
            bool has = false;
            for (proven_size_t q = 0; q < t->nop; q++) if (dy_eqv(t->op[q].name, nd->kids[i + 2]->tok.lex)) has = true;
            if (!has) {
                char m[240]; low_bfmt(m, sizeof m, "`dyn` — trait `{}` has no op `{}` (only the trait's ops can be chosen at run time)", proven_arg_str_view(t->name), proven_arg_str_view(nd->kids[i + 2]->tok.lex));
                dy_err(c, nd->kids[i + 2], "E-DYN-OP", dy_dup(c, m)); out[no++] = k; continue;
            }
            if (sc->is_fn || !sc->has_dyn_eff)
                dy_err(c, k, "E-DYN-EFFECT", sc->is_fn
                    ? "a `fn` cannot call `dyn` — the op is chosen at run time, which is the effect `dyn`: make it `proc … effects dyn .` (RFC-0159)"
                    : "this op calls `dyn` but its effects line does not say `dyn` — write `effects … dyn .` so the caller sees that an op is chosen at run time (RFC-0159)");
            proven_u8str_view_t on = dy_cat(c, dy_cat(c, t->name, "__object.", (proven_u8str_view_t){ 0 }), "", nd->kids[i + 2]->tok.lex);
            out[no++] = dy_word(c, nd->kids[i + 2], on);
            out[no++] = nd->kids[i + 1];
            i += 2; changed = true; continue;
        }
        // object <트레이트> [<값>]
        if (dy_is(k, "object") && i + 1 < nd->nkids && dy_atom(nd->kids[i + 1]) && dy_find(c, nd->kids[i + 1]->tok.lex)) {
            dy_trait_t *t = dy_find(c, nd->kids[i + 1]->tok.lex);
            proven_u8str_view_t gn = dy_cat(c, t->name, "__object", (proven_u8str_view_t){ 0 });
            bool lit_el = i >= 2 && dy_atom(nd->kids[0]) && nd->kids[0]->tok.kw == LOW_KW_LIT && i == 2 &&   // `lit slice|array object T …` — 원소 타입
                          (dy_is(nd->kids[1], "slice") || dy_is(nd->kids[1], "array"));
            bool value = i + 2 < nd->nkids && !(bind && i < be) && !lit_el && nd->kids[i + 2]->kind != LOW_CST_BLOCK &&
                         !dy_clause(nd->kids[i + 2]) && !(dy_atom(nd->kids[i + 2]) && nd->kids[i + 2]->tok.kw == LOW_KW_BE);
            if (!value) {
                // ★ 첫 판의 테두리(소유자 2026-11-19 «추천대로» · RFC-0159 D11 · D12) — C 와 주고받는 op 과 액터의 처리기는 `object` 를 받지도 내지도 않는다
                if (sc->ffi) dy_err(c, k, "E-DYN-FFI", "an op that crosses to C (`link` · `extern`) cannot take or give `object <trait>` — its layout is not part of the C ABI (RFC-0159 D11)");
                else if (sc->actor) dy_err(c, k, "E-DYN-ACTOR", "an actor's handler cannot take or give `object <trait>` in this first version (RFC-0159 D12)");
                out[no++] = dy_word(c, k, gn); i += 1; changed = true; continue;
            }
            proven_u8str_view_t vt = { 0 };
            if (!dy_value_type(sc, nd->kids[i + 2], &vt)) {
                dy_err(c, k, "E-DYN-LIT", "`object <trait> <value> .` — the value's type must be written: a name with a written type, or `lit <type> …` (the type is never guessed · RFC-0159)");
                out[no++] = k; continue;
            }
            proven_size_t vk = t->nimpl;
            for (proven_size_t q = 0; q < t->nimpl; q++) if (dy_eqv(t->impl[q], vt)) { vk = q; break; }
            if (vk == t->nimpl) {
                char m[240]; low_bfmt(m, sizeof m, "`object {}` — type `{}` does not say `satisfies {}`", proven_arg_str_view(t->name), proven_arg_str_view(vt), proven_arg_str_view(t->name));
                dy_err(c, k, "E-DYN-NOTSAT", dy_dup(c, m)); out[no++] = k; continue;
            }
            char vb[24]; low_bfmt(vb, sizeof vb, ".v{}", PROVEN_ARG((size_t)vk));
            out[no++] = dy_word(c, k, dy_cat(c, gn, vb, (proven_u8str_view_t){ 0 }));
            out[no++] = nd->kids[i + 2];
            i += 2; changed = true; continue;
        }
        out[no++] = k;
    }
    if (changed) (void)low_refit(&c->p, nd, out, no);
}
static bool dy_is_opform(const low_cst_t *f, bool *is_fn) {
    if (!f || f->kind != LOW_CST_FORM || f->nkids < 3) return false;
    for (proven_size_t i = 0; i < 2 && i < f->nkids; i++)
        if (dy_atom(f->kids[i]) && (f->kids[i]->tok.kw == LOW_KW_FN || f->kids[i]->tok.kw == LOW_KW_PROC)) { *is_fn = f->kids[i]->tok.kw == LOW_KW_FN; return true; }
    return false;
}
static void dy_rewrite_top(dy_t *c, low_cst_t *f, bool in_actor) {
    bool is_fn = false;
    dy_scope_t *sc = (dy_scope_t *)c->work.alloc_fn(c->work.ctx, sizeof(dy_scope_t), alignof(dy_scope_t)).value.ptr;
    if (!sc) return;
    memset(sc, 0, sizeof *sc);
    if (dy_is_opform(f, &is_fn)) {
        sc->is_fn = is_fn; sc->actor = in_actor;
        for (proven_size_t i = 0; i < f->nkids; i++)
            if (dy_is(f->kids[i], "link") || (dy_atom(f->kids[i]) && f->kids[i]->tok.kw == LOW_KW_EXTERN)) sc->ffi = true;
        for (proven_size_t i = 0; i < f->nkids; i++)
            if (dy_is(f->kids[i], "effects"))
                for (proven_size_t e = i + 1; e < f->nkids && dy_atom(f->kids[e]) && !dy_clause(f->kids[e]); e++)
                    if (f->kids[e]->tok.kw == LOW_KW_DYN || dy_is(f->kids[e], "dyn")) sc->has_dyn_eff = true;
        dy_collect_decls(c, f, sc);
    } else if (f && f->kind == LOW_CST_FORM) {
        for (proven_size_t i = 0; i < f->nkids; i++) {          // actor · module 블록 안의 op
            const low_cst_t *b = f->kids[i];
            bool act = dy_atom(f->kids[0]) && f->kids[0]->tok.kw == LOW_KW_ACTOR;
            if (b && b->kind == LOW_CST_BLOCK) for (proven_size_t q = 0; q < b->nkids; q++) dy_rewrite_top(c, b->kids[q], act || in_actor);
        }
        c->work.free_fn(c->work.ctx, sc);
        // 구조체 칸 · 타입 선언의 `object T` 는 타입 자리다 — 아래 dy_rewrite 가 «값이 아님» 으로 가른다
        dy_scope_t z; memset(&z, 0, sizeof z); dy_rewrite(c, f, &z);
        return;
    }
    dy_rewrite(c, f, sc);
    c->work.free_fn(c->work.ctx, sc);
}
bool low_dyn_rewrite(low_parse_result_t *pr, proven_allocator_t node_alloc, proven_allocator_t work) {
    dy_t *c = (dy_t *)work.alloc_fn(work.ctx, sizeof(dy_t), alignof(dy_t)).value.ptr;
    if (!c) return true;
    memset(c, 0, sizeof *c);
    c->p = (low_parser_t){ .node_alloc = node_alloc, .work = work, .out = pr };
    c->nodes = node_alloc; c->work = work; c->pr = pr;
    // ⓐ 와 같은 표를 다시 세운다(진단은 ⓐ 가 이미 했다 — 여기서는 말하지 않는다)
    proven_size_t nd0 = pr->diags.len;
    for (proven_size_t i = 0; i < pr->nforms; i++) dy_scan_uses(c, pr->forms[i]);
    pr->diags.len = nd0;
    // `dyn` 은 `object` 없이도 적힐 수 있다 — 그러면 받는 값을 못 풀어 E-DYN-RECV 가 말한다
    proven_size_t nd1 = pr->diags.len;
    for (proven_size_t i = 0; i < pr->nforms; i++) dy_rewrite_top(c, pr->forms[i], false);
    work.free_fn(work.ctx, c);
    return pr->diags.len == nd1;
}
