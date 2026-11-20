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
#define DY_MAXF   24      // 갖춘 타입 하나의 칸(편 뒤 · 구조체 칸은 그 칸들로 센다)
#define DY_MAXM   64      // 모듈
#define DY_MAXU   32      // 모듈 하나의 `use`

typedef struct {
    proven_u8str_view_t name, mod;
    proven_size_t form;                       // 최상위 폼 자리
    proven_size_t nimpl;
    proven_u8str_view_t imod[DY_MAXI], itext[DY_MAXI];   // 갖춘 타입의 모듈 · 주인 모듈에서 적는 이름(`rect` · `b.rect`)
    // 갖춘 타입의 칸을 **편** 잎: 납작한 이름(`p__x`) · 읽는 길(`p x`) · 타입 글 · 0 값 글 · 다시 짓는 글(`lit rect do … end .`)
    proven_size_t nf[DY_MAXI]; proven_u8str_view_t lflat[DY_MAXI][DY_MAXF], lpath[DY_MAXI][DY_MAXF], lty[DY_MAXI][DY_MAXF], lzero[DY_MAXI][DY_MAXF];
    proven_u8str_view_t rebuild[DY_MAXI];
    proven_u8str_view_t host; proven_size_t host_at;      // 표를 짓는 모듈 · 그 뒤에 넣을 폼 자리
    proven_size_t nuse; proven_u8str_view_t umod[DY_MAXU]; const low_cst_t *uat[DY_MAXU];   // `object T` 를 쓰는 모듈 · 그 첫 자리
    proven_size_t nop;
    struct { proven_u8str_view_t name, out; proven_size_t na; proven_u8str_view_t an[DY_MAXA], at[DY_MAXA];
             proven_size_t ne; proven_u8str_view_t eff[DY_MAXE]; } op[DY_MAXO];
    bool ok;
} dy_trait_t;

typedef struct { proven_u8str_view_t name; proven_size_t last; proven_size_t nuse; proven_u8str_view_t use[DY_MAXU]; } dy_mod_t;
typedef struct {
    low_parser_t p;
    proven_allocator_t nodes, work;
    low_parse_result_t *pr;
    proven_size_t nt; dy_trait_t t[DY_MAXT];
    // ★ 2026-11-20 (W-20261120-1549 · 소유자 «쓰는 모듈이 불러온다») — 모듈 표: 폼마다 그 모듈 · 모듈마다 `use` 한 모듈과 마지막 폼
    proven_size_t nmod; dy_mod_t mods[DY_MAXM];
    proven_u8str_view_t *fmod;                // 폼 자리 → 모듈 이름(빈 뷰 = 모듈 밖)
    proven_u8str_view_t cur;                  // 옮겨 적는 폼의 모듈
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
    n = low_view_after_last(n, '.');             // `object a.shape` — 트레이트는 맨 이름으로 찾는다(한정자는 모듈을 가리킬 뿐)
    for (proven_size_t i = 0; i < c->nt; i++) if (dy_eqv(c->t[i].name, n)) return &c->t[i];
    return NULL;
}
static dy_mod_t *dy_mod(dy_t *c, proven_u8str_view_t m) {
    for (proven_size_t i = 0; i < c->nmod; i++) if (dy_eqv(c->mods[i].name, m)) return &c->mods[i];
    return NULL;
}
// 모듈 m 이 모듈 x 를 볼 수 있나 — 같은 모듈이거나 `use x .` 를 적었다(C 의 #include 처럼 · 서로 `use` 해도 된다)
static bool dy_sees(dy_t *c, proven_u8str_view_t m, proven_u8str_view_t x) {
    if (dy_eqv(m, x)) return true;
    const dy_mod_t *d = dy_mod(c, m);
    for (proven_size_t i = 0; d && i < d->nuse; i++) if (dy_eqv(d->use[i], x)) return true;
    return false;
}
static void dy_build_mods(dy_t *c) {
    c->nmod = 0;
    c->fmod = (proven_u8str_view_t *)c->work.alloc_fn(c->work.ctx, sizeof(proven_u8str_view_t) * (c->pr->nforms + 1), alignof(proven_u8str_view_t)).value.ptr;
    proven_u8str_view_t cur = { 0 };
    for (proven_size_t i = 0; i < c->pr->nforms; i++) {
        const low_cst_t *f = c->pr->forms[i];
        if (f && f->kind == LOW_CST_FORM && f->nkids >= 2 && dy_atom(f->kids[0]) && f->kids[0]->tok.kw == LOW_KW_MODULE && dy_atom(f->kids[1])) {
            cur = f->kids[1]->tok.lex;
            if (!dy_mod(c, cur) && c->nmod < DY_MAXM) { memset(&c->mods[c->nmod], 0, sizeof c->mods[0]); c->mods[c->nmod++].name = cur; }
        }
        if (c->fmod) c->fmod[i] = cur;
        dy_mod_t *d = cur.size ? dy_mod(c, cur) : NULL;
        if (d) d->last = i;
        if (d && f && f->kind == LOW_CST_FORM && f->nkids >= 2 && dy_atom(f->kids[0]) && f->kids[0]->tok.kw == LOW_KW_USE && dy_atom(f->kids[1]) && d->nuse < DY_MAXU)
            d->use[d->nuse++] = f->kids[1]->tok.lex;
    }
}
static proven_u8str_view_t dy_fmod(const dy_t *c, proven_size_t i) { return c->fmod && i < c->pr->nforms ? c->fmod[i] : (proven_u8str_view_t){ 0 }; }
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
// ── 타입 글 — 안쪽 차례의 나무를 사람의 차례로 다시 적는다 ───────────────────────────────────────────────────────────
// ★ 2026-11-20 (W-20261120-1549 · 소유자 «하지 않은 것들도 다 진행») — 이 걸음의 나무는 안쪽 차례다(`array u8 4` · `matrix u32 2 3 4`).
//   지은 글은 다시 읽히므로 사람의 차례(`array 4 u8` · `matrix 2 3 4 u32`)로 적어야 한다. 낱말 둘 이상의 원소는 괄호로 묶여 있다.
//   다른 모듈에서 온 타입 이름은 주인 모듈에서 보이게 한정한다(`pt` → `b.pt`). 못 적으면 빈 뷰.
static bool dy_decl_in(dy_t *c, proven_u8str_view_t name, proven_u8str_view_t m) {   // m 에 그 이름의 타입 선언이 있나
    for (proven_size_t i = 0; i < c->pr->nforms; i++) {
        const low_cst_t *f = c->pr->forms[i];
        if (!f || f->kind != LOW_CST_FORM || f->nkids < 2 || !dy_atom(f->kids[0]) || !dy_atom(f->kids[1]) || !dy_eqv(dy_fmod(c, i), m)) continue;
        low_kw_t k = f->kids[0]->tok.kw;
        if ((k == LOW_KW_STRUCT || k == LOW_KW_ENUM || k == LOW_KW_TYPE || k == LOW_KW_NEWTYPE || k == LOW_KW_TRAIT) && dy_eqv(f->kids[1]->tok.lex, name)) return true;
    }
    return false;
}
static proven_u8str_view_t dy_qual(dy_t *c, proven_u8str_view_t w, proven_u8str_view_t src, proven_u8str_view_t host) {
    if (!src.size || dy_eqv(src, host) || low_view_last(w, '.') != PROVEN_INDEX_NOT_FOUND || !dy_decl_in(c, w, src)) return w;
    return dy_cat(c, src, ".", w);
}
static proven_u8str_view_t dy_render(dy_t *c, low_cst_t *const *k, proven_size_t i, proven_size_t end, proven_u8str_view_t src, proven_u8str_view_t host, proven_size_t *next) {
    static const char *const PRE[] = { "slice", "mut", "owned", "ref", "mut_ref", "option", "view", "segments", "task", "chan", "stack", "set",
                                       "unsafe_ptr", "nonzero", "atomic", "lock", "rwlock", "shared_read", "object" };
    const proven_u8str_view_t NO = { 0 };
    if (i >= end) return NO;
    const low_cst_t *n = k[i];
    if (n->kind == LOW_CST_GROUP && n->nkids == 1 && n->kids[0]->kind == LOW_CST_FORM) {
        const low_cst_t *g = n->kids[0]; proven_size_t z = 0;
        proven_u8str_view_t r = dy_render(c, g->kids, 0, g->nkids, src, host, &z);
        if (!r.size || z != g->nkids) return NO;
        *next = i + 1; return r;
    }
    if (!dy_atom(n) || n->tok.kind != LOW_TOK_IDENT || dy_is(n, "self") || dy_is(n, "comptime")) return NO;
    if (dy_is(n, "array") || dy_is(n, "vec")) {
        proven_size_t j = 0; proven_u8str_view_t e = dy_render(c, k, i + 1, end, src, host, &j);
        if (!e.size || j >= end || !dy_atom(k[j])) return NO;
        *next = j + 1;
        return dy_cat(c, dy_cat(c, n->tok.lex, " ", k[j]->tok.lex), " ", e);
    }
    if (dy_is(n, "matrix")) {
        proven_size_t j = 0; proven_u8str_view_t e = dy_render(c, k, i + 1, end, src, host, &j);
        if (!e.size || j >= end || !dy_atom(k[j]) || k[j]->tok.kind != LOW_TOK_NUMBER) return NO;
        unsigned long long d = 0;
        for (proven_size_t q = 0; q < k[j]->tok.lex.size; q++) d = d * 10 + (unsigned)(k[j]->tok.lex.ptr[q] - '0');
        if (d < 2 || d > 8 || j + d >= end + 0 + 1) return NO;
        proven_u8str_view_t r = dy_cat(c, n->tok.lex, " ", k[j]->tok.lex);
        for (proven_size_t q = 1; q <= d; q++) { if (j + q >= end || !dy_atom(k[j + q])) return NO; r = dy_cat(c, r, " ", k[j + q]->tok.lex); }
        *next = j + 1 + (proven_size_t)d;
        return dy_cat(c, r, " ", e);
    }
    if (dy_is(n, "result")) {
        proven_size_t j = 0, m = 0;
        proven_u8str_view_t a = dy_render(c, k, i + 1, end, src, host, &j); if (!a.size) return NO;
        proven_u8str_view_t b = dy_render(c, k, j, end, src, host, &m); if (!b.size) return NO;
        *next = m; return dy_cat(c, dy_cat(c, n->tok.lex, " ", a), " ", b);
    }
    for (size_t q = 0; q < sizeof PRE / sizeof PRE[0]; q++)
        if (dy_is(n, PRE[q])) {
            proven_size_t j = 0; proven_u8str_view_t e = dy_render(c, k, i + 1, end, src, host, &j);
            if (!e.size) return NO;
            *next = j; return dy_cat(c, n->tok.lex, " ", e);
        }
    *next = i + 1;
    return dy_qual(c, n->tok.lex, src, host);
}
static proven_u8str_view_t dy_type_text(dy_t *c, low_cst_t *const *k, proven_size_t a, proven_size_t b, proven_u8str_view_t src, proven_u8str_view_t host) {
    proven_size_t n = 0;
    proven_u8str_view_t r = dy_render(c, k, a, b, src, host, &n);
    return n == b ? r : (proven_u8str_view_t){ 0 };
}

// ── 갖춘 타입의 칸 — 펴서 나란히 둔다 ─────────────────────────────────────────────────────────────────────────────
// ★ 2026-11-19 (W-20261119-2322) — `object T` 는 **납작한 구조체**다: 태그 하나 + 갖춘 타입 모두의 칸을 나란히(크기 = 칸의 합 + 태그 · 맞춤 여백).
//   남의 칸은 0 으로 채운다. ★ 2026-11-20 (W-20261120-1549) — 칸의 종류를 넓혔다:
//     크기 있는 수 · 부동 · `bool` → 그대로 · 0 은 `0` · `0.0` · `false`
//     구조체 → **편다**(`p pt` 는 `v0__p__x` · `v0__p__y`) — 구조체 안의 구조체는 나열에 못 담기므로(RFC-0132 T2b-3d)
//     `array <n> <수>` → 0 은 `lit array <n> <수> _ .`
//     `slice <타입>` → 0 은 빈 조각 `lit slice <타입> .`(시작점 · 길이 0 · 소유자 2026-11-20)
//   그 밖(열거 · `owned` · 참조 · 배열의 배열 …)은 `E-DYN-FIELD`.
static const char *dy_zero_of(proven_u8str_view_t ty) {
    static const char *const ints[] = { "u8", "u16", "u32", "u64", "i8", "i16", "i32", "i64", "usize", "idiff", "u128", "i128" };
    for (size_t i = 0; i < sizeof ints / sizeof ints[0]; i++) if (low_view_eq_cstr(ty, ints[i])) return "0";
    if (low_view_eq_cstr(ty, "f32") || low_view_eq_cstr(ty, "f64")) return "0.0";
    if (low_view_eq_cstr(ty, "bool")) return "false";
    return NULL;
}
static const low_cst_t *dy_struct_form(dy_t *c, proven_u8str_view_t name, proven_u8str_view_t src, proven_u8str_view_t *defmod) {
    proven_size_t dot = low_view_last(name, '.');
    proven_u8str_view_t m = src, b = name;
    if (dot != PROVEN_INDEX_NOT_FOUND) { m = (proven_u8str_view_t){ .ptr = name.ptr, .size = dot }; b = low_view_after_last(name, '.'); }
    for (proven_size_t i = 0; i < c->pr->nforms; i++) {
        const low_cst_t *f = c->pr->forms[i];
        if (f && f->kind == LOW_CST_FORM && f->nkids >= 3 && dy_atom(f->kids[0]) && f->kids[0]->tok.kw == LOW_KW_STRUCT && dy_atom(f->kids[1]) &&
            dy_eqv(f->kids[1]->tok.lex, b) && dy_eqv(dy_fmod(c, i), m)) { *defmod = m; return f; }
    }
    return NULL;
}
typedef struct { char b[2048]; size_t n; bool full; } dy_sb_t;   // 다시 짓는 글을 모으는 작은 버퍼
static void dy_sb(dy_sb_t *o, const char *s, size_t n) { if (o->n + n + 1 > sizeof o->b) { o->full = true; return; } memcpy(o->b + o->n, s, n); o->n += n; o->b[o->n] = 0; }
static void dy_sbs(dy_sb_t *o, const char *s) { dy_sb(o, s, strlen(s)); }
static void dy_sbv(dy_sb_t *o, proven_u8str_view_t v) { dy_sb(o, (const char *)v.ptr, v.size); }
static void dy_sbk(dy_sb_t *o, proven_size_t k) { char v[24]; low_bfmt(v, sizeof v, "{}", PROVEN_ARG((size_t)k)); dy_sbs(o, v); }
static bool dy_fields(dy_t *c, dy_trait_t *t, proven_size_t k, const low_cst_t *sf, proven_u8str_view_t src,
                      proven_u8str_view_t flat, proven_u8str_view_t path, int depth, dy_sb_t *rb) {
    const low_cst_t *b = dy_block(sf);
    proven_u8str_view_t tname = dy_qual(c, sf->kids[1]->tok.lex, src, t->host);
    dy_sbs(rb, "lit "); dy_sbv(rb, tname); dy_sbs(rb, " do");
    for (proven_size_t q = 0; b && q < b->nkids; q++) {
        const low_cst_t *s = b->kids[q];
        if (!s || s->kind != LOW_CST_FORM || !s->nkids || !dy_atom(s->kids[0]) || s->kids[0]->tok.kw != LOW_KW_NONE) continue;
        if (dy_is(s->kids[0], "layout") || dy_is(s->kids[0], "align")) continue;
        proven_u8str_view_t fname = s->kids[0]->tok.lex;
        proven_u8str_view_t fl = flat.size ? dy_cat(c, flat, "__", fname) : fname, fp = path.size ? dy_cat(c, path, " ", fname) : fname;
        const char *why = NULL;
        proven_u8str_view_t ty = { 0 }, zero = { 0 };
        proven_u8str_view_t defm = { 0 };
        const low_cst_t *nested = s->nkids == 2 && dy_atom(s->kids[1]) && !dy_zero_of(s->kids[1]->tok.lex) ? dy_struct_form(c, s->kids[1]->tok.lex, src, &defm) : NULL;
        if (s->nkids == 2 && dy_atom(s->kids[1]) && dy_zero_of(s->kids[1]->tok.lex)) {
            ty = s->kids[1]->tok.lex; zero = (proven_u8str_view_t){ .ptr = (const proven_byte_t *)dy_zero_of(ty), .size = strlen(dy_zero_of(ty)) };
        } else if (nested) {
            if (depth >= 4) why = "a struct field nested more than four deep";
            else {
                dy_sbs(rb, " "); dy_sbv(rb, fname); dy_sbs(rb, " ");
                if (!dy_fields(c, t, k, nested, defm, fl, fp, depth + 1, rb)) return false;
                dy_sbs(rb, " ."); continue;
            }
        } else if (s->nkids >= 3 && dy_is(s->kids[1], "slice")) {
            proven_u8str_view_t et = dy_type_text(c, s->kids, 2, s->nkids, src, t->host);
            if (!et.size) why = "a slice of a type this version cannot write back";
            else { ty = dy_cat(c, (proven_u8str_view_t){ .ptr = (const proven_byte_t *)"slice", .size = 5 }, " ", et);
                   zero = dy_cat(c, dy_cat(c, (proven_u8str_view_t){ .ptr = (const proven_byte_t *)"lit slice", .size = 9 }, " ", et), " ", (proven_u8str_view_t){ .ptr = (const proven_byte_t *)".", .size = 1 }); }
        } else if (s->nkids == 4 && dy_is(s->kids[1], "array") && dy_atom(s->kids[2]) && dy_zero_of(s->kids[2]->tok.lex) && dy_atom(s->kids[3]) && s->kids[3]->tok.kind == LOW_TOK_NUMBER) {
            ty = dy_cat(c, dy_cat(c, (proven_u8str_view_t){ .ptr = (const proven_byte_t *)"array", .size = 5 }, " ", s->kids[3]->tok.lex), " ", s->kids[2]->tok.lex);
            zero = dy_cat(c, dy_cat(c, (proven_u8str_view_t){ .ptr = (const proven_byte_t *)"lit", .size = 3 }, " ", ty), " ", (proven_u8str_view_t){ .ptr = (const proven_byte_t *)"_ .", .size = 3 });
        } else why = "not a sized number, float, `bool`, struct, `array <n> <number>` or `slice <type>`";
        if (why) {
            char m[360]; low_bfmt(m, sizeof m, "a type held in `object {}` keeps its fields side by side in one record, filling the other types' fields with zero — field `{}`: {} (RFC-0159 §9-14)",
                                  proven_arg_str_view(t->name), proven_arg_str_view(fname), proven_arg_cstr(why));
            dy_err(c, s->kids[0], "E-DYN-FIELD", dy_dup(c, m)); return false;
        }
        if (t->nf[k] >= DY_MAXF) { dy_err(c, s->kids[0], "E-DYN-FIELD", "a type held in `object` has too many fields for this version"); return false; }
        proven_size_t f = t->nf[k]++;
        t->lflat[k][f] = fl; t->lpath[k][f] = fp; t->lty[k][f] = ty; t->lzero[k][f] = zero;
        dy_sbs(rb, " "); dy_sbv(rb, fname); dy_sbs(rb, " field dyn__o. v"); dy_sbk(rb, k); dy_sbs(rb, "__"); dy_sbv(rb, fl); dy_sbs(rb, " . .");
    }
    dy_sbs(rb, " end .");
    return !rb->full;
}
static bool dy_is_satisfies_of(const low_cst_t *s, proven_u8str_view_t tname) {
    return s && s->kind == LOW_CST_FORM && s->nkids == 2 && dy_atom(s->kids[0]) && s->kids[0]->tok.kw == LOW_KW_SATISFIES && dy_atom(s->kids[1]) &&
           dy_eqv(low_view_after_last(s->kids[1]->tok.lex, '.'), tname);
}
// 트레이트 T 를 갖춘 구조체 — 단위 안의 **모든 모듈**에서, 폼 차례대로(번호가 그 차례다). 그리고 표를 지을 **주인 모듈**을 고른다:
//   ① 트레이트의 모듈이 갖춘 타입의 모듈을 모두 보면 거기(전과 같다 · 트레이트 바로 뒤)
//   ② 아니면 `object T` 를 쓰는 모듈 가운데 트레이트와 갖춘 타입의 모듈을 모두 보는 첫 모듈(그 모듈의 끝 · 소유자 «쓰는 모듈이 불러온다»)
//   ③ 그런 모듈이 없으면 `E-DYN-MODULE` — 무엇을 `use` 하라고 말한다. 주인이 아닌 쓰는 모듈은 주인 모듈을 `use` 해야 한다.
static void dy_collect_impl(dy_t *c, dy_trait_t *t) {
    t->nimpl = 0;
    for (proven_size_t i = 0; i < c->pr->nforms; i++) {
        const low_cst_t *f = c->pr->forms[i];
        if (!f || f->kind != LOW_CST_FORM || f->nkids < 2 || !(dy_atom(f->kids[0]) && f->kids[0]->tok.kw == LOW_KW_STRUCT && dy_atom(f->kids[1]))) continue;
        const low_cst_t *b = dy_block(f);
        for (proven_size_t q = 0; b && q < b->nkids; q++)
            if (dy_is_satisfies_of(b->kids[q], t->name) && t->nimpl < DY_MAXI) { t->imod[t->nimpl] = dy_fmod(c, i); t->itext[t->nimpl] = f->kids[1]->tok.lex; t->nimpl++; }
    }
    if (!t->nimpl) return;
    // 주인 모듈
    bool trait_sees = true;
    for (proven_size_t k = 0; k < t->nimpl; k++) if (!dy_sees(c, t->mod, t->imod[k])) trait_sees = false;
    t->host = (proven_u8str_view_t){ 0 };
    if (trait_sees) { t->host = t->mod; t->host_at = t->form; }
    else
        for (proven_size_t u = 0; u < t->nuse && !t->host.size; u++) {
            bool ok = dy_sees(c, t->umod[u], t->mod);
            for (proven_size_t k = 0; k < t->nimpl && ok; k++) if (!dy_sees(c, t->umod[u], t->imod[k])) ok = false;
            if (ok) { t->host = t->umod[u]; const dy_mod_t *d = dy_mod(c, t->host); t->host_at = d ? d->last : t->form; }
        }
    if (!t->host.size) {
        proven_u8str_view_t um = t->nuse ? t->umod[0] : t->mod, miss = { 0 }, mty = { 0 };
        for (proven_size_t k = 0; k < t->nimpl && !miss.size; k++) if (!dy_sees(c, um, t->imod[k])) { miss = t->imod[k]; mty = t->itext[k]; }
        if (!miss.size && !dy_sees(c, um, t->mod)) miss = t->mod;
        char m[400]; low_bfmt(m, sizeof m, "module `{}` uses `object {}`, so it must bring in every module whose types it may hold — write `use {} .` there "
                              "(`{}` satisfies `{}`) · the module that uses `object` imports the others, as C includes (RFC-0159 §9-12)",
                              proven_arg_str_view(um), proven_arg_str_view(t->name), proven_arg_str_view(miss), proven_arg_str_view(mty.size ? mty : t->name), proven_arg_str_view(t->name));
        dy_err(c, t->nuse ? t->uat[0] : NULL, "E-DYN-MODULE", dy_dup(c, m)); t->ok = false; return;
    }
    for (proven_size_t u = 0; u < t->nuse; u++)
        if (!dy_sees(c, t->umod[u], t->host)) {
            char m[360]; low_bfmt(m, sizeof m, "module `{}` uses `object {}`, whose table is built in module `{}` — write `use {} .` there (RFC-0159 §9-12)",
                                  proven_arg_str_view(t->umod[u]), proven_arg_str_view(t->name), proven_arg_str_view(t->host), proven_arg_str_view(t->host));
            dy_err(c, t->uat[u], "E-DYN-MODULE", dy_dup(c, m)); t->ok = false; return;
        }
    // 이름과 칸
    for (proven_size_t k = 0; k < t->nimpl; k++) {
        proven_u8str_view_t dm = { 0 };
        proven_u8str_view_t q = dy_eqv(t->imod[k], t->host) ? t->itext[k] : dy_cat(c, t->imod[k], ".", t->itext[k]);
        const low_cst_t *sf = dy_struct_form(c, q, t->host, &dm);
        if (!sf) sf = dy_struct_form(c, t->itext[k], t->imod[k], &dm);
        t->nf[k] = 0;
        dy_sb_t *rb = (dy_sb_t *)c->work.alloc_fn(c->work.ctx, sizeof(dy_sb_t), alignof(dy_sb_t)).value.ptr;
        if (!sf || !rb) { t->ok = false; return; }
        rb->n = 0; rb->full = false; rb->b[0] = 0;
        if (!dy_fields(c, t, k, sf, t->imod[k], (proven_u8str_view_t){ 0 }, (proven_u8str_view_t){ 0 }, 0, rb)) { t->ok = false; return; }
        t->itext[k] = q;
        char *keep = (char *)c->nodes.alloc_fn(c->nodes.ctx, rb->n + 1, 1).value.ptr;
        if (!keep) { t->ok = false; return; }
        memcpy(keep, rb->b, rb->n + 1);
        t->rebuild[k] = (proven_u8str_view_t){ .ptr = (const proven_byte_t *)keep, .size = rb->n };
        c->work.free_fn(c->work.ctx, rb);
    }
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
                    proven_u8str_view_t ty = dy_type_text(c, s->kids, k + 2, e, t->mod, t->host);
                    if (!ty.size) { bad = true; why = "an input's type must be one this version can write back — `u64` · `slice u8` · `mut slice u8` · `option u64` · `array 4 u8` (not `self` · `comptime`)"; break; }
                    o->an[o->na] = s->kids[k + 1]->tok.lex; o->at[o->na] = ty; o->na++;
                }
                nin++; k = e;
            } else if (dy_is(w, "output")) {
                proven_size_t e = k + 1; while (e < s->nkids && !dy_clause(s->kids[e])) e++;
                if (e - k < 2 || !dy_atom(s->kids[k + 1])) { bad = true; why = "the output is `output <type> .`"; break; }
                for (proven_size_t z = k + 1; z < e; z++)
                    if (dy_is(s->kids[z], "self")) { bad = true; why = "an op chosen at run time cannot give back `self` — its size is not known"; break; }
                if (bad) break;
                o->out = dy_type_text(c, s->kids, k + 1, e, t->mod, t->host);
                if (!o->out.size) { bad = true; why = "the output's type must be one this version can write back — `u64` · `option u64` (not `self`)"; break; }
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
// `object <트레이트>` 가 적힌 자리를 모은다 — 트레이트마다 쓰는 모듈과 그 첫 자리. 읽기(서명 · 갖춘 타입 · 주인 모듈)는 다 모은 뒤에 한다.
static void dy_scan_uses(dy_t *c, const low_cst_t *nd, proven_u8str_view_t m) {
    if (!nd || nd->kind == LOW_CST_ATOM) return;
    for (proven_size_t i = 0; i + 1 < nd->nkids; i++)
        if (dy_is(nd->kids[i], "object") && dy_atom(nd->kids[i + 1])) {
            dy_trait_t *t = dy_find(c, nd->kids[i + 1]->tok.lex);
            if (!t && c->nt < DY_MAXT) {
                t = &c->t[c->nt++]; memset(t, 0, sizeof *t);
                t->name = low_view_after_last(nd->kids[i + 1]->tok.lex, '.'); t->ok = true; t->uat[0] = nd->kids[i + 1];
            }
            if (!t) continue;
            bool seen = false;
            for (proven_size_t u = 0; u < t->nuse; u++) if (dy_eqv(t->umod[u], m)) seen = true;
            if (!seen && t->nuse < DY_MAXU) { t->umod[t->nuse] = m; t->uat[t->nuse] = nd->kids[i + 1]; t->nuse++; }
        }
    for (proven_size_t i = 0; i < nd->nkids; i++) dy_scan_uses(c, nd->kids[i], m);
}
// 모은 트레이트마다: 선언을 찾고 · 갖춘 타입과 주인 모듈을 정하고 · 서명을 읽는다(서명의 타입은 주인 모듈에서 적는다)
static void dy_resolve(dy_t *c) {
    dy_build_mods(c);
    for (proven_size_t i = 0; i < c->pr->nforms; i++) dy_scan_uses(c, c->pr->forms[i], dy_fmod(c, i));
    for (proven_size_t q = 0; q < c->nt; q++) {
        dy_trait_t *t = &c->t[q];
        const low_cst_t *tf = dy_find_trait_form(c->pr, t->name, &t->form, &t->mod);
        if (!tf) {
            char m[200]; low_bfmt(m, sizeof m, "`object {}` — `{}` is not a trait declared at the top of a module (RFC-0159)",
                                  proven_arg_str_view(t->name), proven_arg_str_view(t->name));
            dy_err(c, t->uat[0], "E-DYN-TRAIT", dy_dup(c, m)); t->ok = false; continue;
        }
        dy_collect_impl(c, t);
        if (!t->host.size) t->host = t->mod;              // 갖춘 타입이 없어도 서명의 진단은 먼저 낸다(E-DYN-TRAIT 가 E-DYN-NOIMPL 보다 앞)
        if (t->ok) dy_read_sigs(c, t, tf);
        if (t->ok && !t->nimpl) {
            char m[200]; low_bfmt(m, sizeof m, "`object {}` — no type in this unit says `satisfies {}`, so no value can be made", proven_arg_str_view(t->name), proven_arg_str_view(t->name));
            dy_err(c, t->uat[0], "E-DYN-NOIMPL", dy_dup(c, m)); t->ok = false; continue;
        }
    }
}
typedef struct { char *b; size_t n, cap; bool full; } dy_buf_t;
static void dy_put(dy_buf_t *o, const char *s, size_t n) { if (o->n + n + 1 > o->cap) { o->full = true; return; } memcpy(o->b + o->n, s, n); o->n += n; o->b[o->n] = 0; }
static void dy_ps(dy_buf_t *o, const char *s) { dy_put(o, s, strlen(s)); }
static void dy_pv(dy_buf_t *o, proven_u8str_view_t v) { dy_put(o, (const char *)v.ptr, v.size); }

static void dy_pk(dy_buf_t *o, proven_size_t k) { char v[24]; low_bfmt(v, sizeof v, "{}", PROVEN_ARG((size_t)k)); dy_ps(o, v); }
// 칸 이름: `v<k>__<칸>` · 태그 `dyn__tag`(u32 · 갖춘 타입의 번호)
static void dy_fld(dy_buf_t *o, proven_size_t k, proven_u8str_view_t f) { dy_ps(o, "v"); dy_pk(o, k); dy_ps(o, "__"); dy_pv(o, f); }
// ★ 주인 모듈 밖에서 쓰는 모듈이 있으면 지은 선언을 `export` 한다 — 그 모듈이 한정 이름(`app.shape__object`)으로 닿는다.
static void dy_gen_text(dy_t *c, const dy_trait_t *t, dy_buf_t *o) {
    // 내보내기는 주인 모듈 밖에서 쓰는 모듈이 있을 때만 — 아니면 내보내지 않은 갖춘 타입을 받는 op 이 W-EXPORT-HIDDEN 으로 운다
    bool ex = false;
    for (proven_size_t u = 0; u < t->nuse; u++) if (!dy_eqv(t->umod[u], t->host)) ex = true;
    const char *X = ex ? "export " : "";
    dy_ps(o, X); dy_ps(o, "struct "); dy_pv(o, t->name); dy_ps(o, "__object do\n  dyn__tag u32 .\n");
    for (proven_size_t k = 0; k < t->nimpl; k++)
        for (proven_size_t f = 0; f < t->nf[k]; f++) { dy_ps(o, "  "); dy_fld(o, k, t->lflat[k][f]); dy_ps(o, " "); dy_pv(o, t->lty[k][f]); dy_ps(o, " .\n"); }
    dy_ps(o, "end .\n");
    // 만들기 — 갖춘 타입마다 하나: 제 칸에 값을, 남의 칸에 0 을
    for (proven_size_t k = 0; k < t->nimpl; k++) {
        dy_ps(o, X); dy_ps(o, "fn "); dy_pv(o, t->name); dy_ps(o, "__object.v"); dy_pk(o, k); dy_ps(o, " input dyn__v "); dy_pv(o, t->itext[k]);
        dy_ps(o, " . output "); dy_pv(o, t->name); dy_ps(o, "__object . do\n  return lit "); dy_pv(o, t->name); dy_ps(o, "__object do dyn__tag "); dy_pk(o, k); dy_ps(o, " .");
        for (proven_size_t j = 0; j < t->nimpl; j++)
            for (proven_size_t f = 0; f < t->nf[j]; f++) {
                dy_ps(o, " "); dy_fld(o, j, t->lflat[j][f]);
                if (j == k) { dy_ps(o, " field dyn__v. "); dy_pv(o, t->lpath[j][f]); dy_ps(o, " . ."); }
                else { dy_ps(o, " "); dy_pv(o, t->lzero[j][f]); dy_ps(o, " ."); }
            }
        dy_ps(o, " end . .\nend .\n");
    }
    // 고르기 — 트레이트의 op 마다 하나: 태그로 갖춘 타입을 골라 그 값을 다시 짓고 그 타입의 op 을 부른다(마지막 갈래는 조건 없이)
    for (proven_size_t q = 0; q < t->nop; q++) {
        const __typeof__(t->op[0]) *op = &t->op[q];
        bool vd = low_view_eq_cstr(op->out, "void");
        dy_ps(o, X); dy_ps(o, "proc "); dy_pv(o, t->name); dy_ps(o, "__object."); dy_pv(o, op->name);
        dy_ps(o, " input dyn__o "); dy_pv(o, t->name); dy_ps(o, "__object .");
        for (proven_size_t a = 0; a < op->na; a++) { dy_ps(o, " input "); dy_pv(o, op->an[a]); dy_ps(o, " "); dy_pv(o, op->at[a]); dy_ps(o, " ."); }
        dy_ps(o, " output "); dy_pv(o, op->out); dy_ps(o, " . effects");
        for (proven_size_t e = 0; e < op->ne; e++) { dy_ps(o, " "); dy_pv(o, op->eff[e]); }
        dy_ps(o, " dyn . do\n");
        for (proven_size_t k = 0; k < t->nimpl; k++) {
            bool last = k + 1 == t->nimpl;
            if (!last) { dy_ps(o, "  if eq field dyn__o. dyn__tag . "); dy_pk(o, k); dy_ps(o, " . do\n"); }
            dy_ps(o, "    ");
            if (!vd) dy_ps(o, "return ");
            // `<타입>.<op>` — 맨 타입 이름으로 부른다(다른 모듈의 `b.rect` 도 `rect.area` 로 닿는다 · `b.rect.area` 꼴은 없다).
            //   `method` 로 부르면 행렬 단계가 op 이름을 못 찾아 `matrix` 입력을 거절한다.
            dy_pv(o, low_view_after_last(t->itext[k], '.')); dy_ps(o, "."); dy_pv(o, op->name); dy_ps(o, " "); dy_pv(o, t->rebuild[k]);
            for (proven_size_t a = 0; a < op->na; a++) { dy_ps(o, " "); dy_pv(o, op->an[a]); dy_ps(o, "."); }
            dy_ps(o, " .");                                   // 부름의 점
            if (!vd) dy_ps(o, " .");                          // `return` 의 점
            else if (!last) dy_ps(o, "\n    return .");
            dy_ps(o, last ? "\n" : "\n  end .\n");
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
    dy_resolve(c);
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
            if (!t->ok || t->host_at != i) continue;
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
            // ★ 2026-11-20 (W-20261120-1549) — 지은 글도 저자의 글이 거친 걸음을 같은 차례로 거친다: 안쪽 차례(`array 4 u8` → `array u8 4`) 다음에 표면 내리기
            if (ok) ok = low_order_lower(&sub, node_alloc, work, 0) && sub.diags.len == 0;
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
            // 타입 이름은 주인 모듈 밖에서 한정한다(`app.shape__object`) · 만들기와 고르는 op 은 `<타입>.<이름>` 꼴이라 맨 이름으로 닿는다
            proven_u8str_view_t gty = (t->host.size && c->cur.size && !dy_eqv(c->cur, t->host)) ? dy_cat(c, t->host, ".", gn) : gn;
            bool lit_el = i >= 2 && dy_atom(nd->kids[0]) && nd->kids[0]->tok.kw == LOW_KW_LIT && i == 2 &&   // `lit slice|array object T …` — 원소 타입
                          (dy_is(nd->kids[1], "slice") || dy_is(nd->kids[1], "array"));
            bool value = i + 2 < nd->nkids && !(bind && i < be) && !lit_el && nd->kids[i + 2]->kind != LOW_CST_BLOCK &&
                         !dy_clause(nd->kids[i + 2]) && !(dy_atom(nd->kids[i + 2]) && nd->kids[i + 2]->tok.kw == LOW_KW_BE);
            if (!value) {
                // ★ 2026-11-20 (W-20261120-1549 · 소유자 «컴퓨터가 효율적으로 처리할 수 있는 방향») — 첫 판은 C 경계와 액터에서 거절했다(E-DYN-FFI ·
                //   E-DYN-ACTOR). 이제 `object T` 는 크기가 정해진 평범한 구조체라 **다른 구조체와 같은 길**로 건넌다: C 에는 값으로(칸 차례 그대로 ·
                //   머리글에 실린다 · 큰 값은 C ABI 가 숨은 주소로 넘긴다), 액터에는 메시지 복사로. 바꾸는 일도 할당도 간접도 없다. 고르는 일(`dyn`)은 이쪽에서만 한다.
                out[no++] = dy_word(c, k, gty); i += 1; changed = true; continue;
            }
            proven_u8str_view_t vt = { 0 };
            if (!dy_value_type(sc, nd->kids[i + 2], &vt)) {
                dy_err(c, k, "E-DYN-LIT", "`object <trait> <value> .` — the value's type must be written: a name with a written type, or `lit <type> …` (the type is never guessed · RFC-0159)");
                out[no++] = k; continue;
            }
            proven_size_t vk = t->nimpl;
            {   // 값의 타입 — `rect`(제 모듈 안) · `b.rect`(한정) — 맨 이름이 같고, 한정자를 적었으면 모듈도 같아야 한다
                proven_u8str_view_t vb = low_view_after_last(vt, '.');
                proven_size_t vd = low_view_last(vt, '.');
                proven_u8str_view_t vm = vd == PROVEN_INDEX_NOT_FOUND ? c->cur : (proven_u8str_view_t){ .ptr = vt.ptr, .size = vd };
                for (proven_size_t q = 0; q < t->nimpl; q++)
                    if (dy_eqv(low_view_after_last(t->itext[q], '.'), vb) && (!vm.size || dy_eqv(t->imod[q], vm))) { vk = q; break; }
            }
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
// ★ 2026-11-20 (W-20261120-1549) — C 경계에서 들어오는 `object` 는 **태그를 문에서 검사한다**. 고르는 op 의 마지막 갈래는 조건 없이 받으므로,
//   C 가 없는 번호(예: 5)를 채워 넘기면 말없이 틀린 답이 났다(재 보았다). 분석과 고르는 op 이 «태그 < 갖춘 타입 수» 를 사실로 쓰는 만큼
//   그 사실을 경계에서 세운다(정본 교훈 1): C 가 부르는 op 의 입력에는 `requires`, 우리가 부르는 C 의 출력에는 `ensures` 를 처리기가 붙인다.
static low_cst_t *dy_tok(dy_t *c, const low_cst_t *model, const char *w, low_tok_kind_t kind) {
    low_token_t t = model->tok;
    t.kind = kind; t.kw = LOW_KW_NONE; t.lex = (proven_u8str_view_t){ .ptr = (const proven_byte_t *)w, .size = strlen(w) }; t.aux = (proven_u8str_view_t){ 0 };
    return low_node(&c->p, LOW_CST_ATOM, t);
}
static void dy_boundary_contract(dy_t *c, low_cst_t *f, const char *clause, proven_u8str_view_t name, const dy_trait_t *t) {
    char *num = (char *)c->nodes.alloc_fn(c->nodes.ctx, 24, 1).value.ptr;
    if (!num) return;
    low_bfmt(num, 24, "{}", PROVEN_ARG((size_t)t->nimpl));
    const low_cst_t *m = f->kids[0];
    low_cst_t *fk[3] = { dy_tok(c, m, "field", LOW_TOK_IDENT), dy_word(c, m, name), dy_tok(c, m, "dyn__tag", LOW_TOK_IDENT) };
    low_cst_t *fm = low_node(&c->p, LOW_CST_FORM, m->tok), *g = low_node(&c->p, LOW_CST_GROUP, m->tok);
    low_cst_t **fkc = (low_cst_t **)c->nodes.alloc_fn(c->nodes.ctx, sizeof fk, alignof(low_cst_t *)).value.ptr;
    if (!fm || !g || !fkc || !fk[0] || !fk[1] || !fk[2]) return;
    memcpy(fkc, fk, sizeof fk);
    fm->closer = LOW_TOK_EOF; fm->file = f->file; g->file = f->file;
    (void)low_refit(&c->p, fm, fkc, 3);
    (void)low_refit(&c->p, g, &fm, 1);
    low_cst_t *ins[4] = { dy_tok(c, m, clause, LOW_TOK_IDENT), dy_tok(c, m, "lt", LOW_TOK_IDENT), g, dy_tok(c, m, num, LOW_TOK_NUMBER) };
    // 절 차례: requires · ensures · errors · tests 다음 몸 — 넣을 자리는 그 뒤 절의 앞(없으면 몸 앞)
    proven_size_t at = f->nkids;
    for (proven_size_t i = 1; i < f->nkids; i++) {
        const low_cst_t *k = f->kids[i];
        if (k->kind == LOW_CST_BLOCK || dy_is(k, "errors") || dy_is(k, "tests") || (!strcmp(clause, "requires") && dy_is(k, "ensures"))) { at = i; break; }
    }
    low_cst_t **nk = (low_cst_t **)c->nodes.alloc_fn(c->nodes.ctx, sizeof(low_cst_t *) * (f->nkids + 4), alignof(low_cst_t *)).value.ptr;
    if (!nk) return;
    proven_size_t n = 0;
    for (proven_size_t i = 0; i < at; i++) nk[n++] = f->kids[i];
    for (proven_size_t i = 0; i < 4; i++) nk[n++] = ins[i];
    for (proven_size_t i = at; i < f->nkids; i++) nk[n++] = f->kids[i];
    (void)low_refit(&c->p, f, nk, n);
}
static void dy_rewrite_top(dy_t *c, low_cst_t *f, bool in_actor) {
    bool is_fn = false;
    dy_scope_t *sc = (dy_scope_t *)c->work.alloc_fn(c->work.ctx, sizeof(dy_scope_t), alignof(dy_scope_t)).value.ptr;
    if (!sc) return;
    memset(sc, 0, sizeof *sc);
    if (dy_is_opform(f, &is_fn)) {
        sc->is_fn = is_fn; sc->actor = in_actor;
        for (proven_size_t i = 0; i < f->nkids; i++)
            if (dy_is(f->kids[i], "link") || (dy_atom(f->kids[i]) && f->kids[i]->tok.kw == LOW_KW_EXTERN) || f->is_extern) sc->ffi = true;
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
    // C 경계의 `object` — 옮겨 적기 전에 자리를 적어 둔다(옮겨 적으면 `object T` 가 한 낱말이 된다)
    struct { proven_u8str_view_t name; const dy_trait_t *t; bool out; } bc[8]; proven_size_t nbc = 0;   // C 가 넘기는 `object` 입력
    if (sc->ffi) {
        // 방향: 몸이 이쪽에 있고 내보내면 C 가 우리를 부른다(입력이 C 에서 온다) · 몸이 C 에 있으면 우리가 C 를 부른다(출력이 C 에서 온다)
        bool body = f->nkids && f->kids[f->nkids - 1]->kind == LOW_CST_BLOCK;
        bool exp = f->is_export && body;
        for (proven_size_t i = 0; i + 2 < f->nkids && nbc < 8; i++) {
            if (exp && dy_is(f->kids[i], "input") && i + 3 < f->nkids && dy_atom(f->kids[i + 1]) && dy_is(f->kids[i + 2], "object") && dy_atom(f->kids[i + 3])) {
                const dy_trait_t *t = dy_find(c, f->kids[i + 3]->tok.lex); if (t && t->ok) bc[nbc++] = (__typeof__(bc[0])){ f->kids[i + 1]->tok.lex, t, false };
            }
            // 우리가 부르는 C 가 `object` 를 **돌려주는** 길은 닫는다: 출구 검사가 결과의 칸을 보지 못하고(W-CONTRACT-IGNORED),
            //   C 에서 구조체를 돌려받는 길 자체가 아직 `long long` 으로 낸다(docs/known-defects/ffi-import-struct-return.md). 둘이 풀리면 연다.
            if (f->is_extern && !body && dy_is(f->kids[i], "output") && dy_is(f->kids[i + 1], "object"))
                dy_err(c, f->kids[i + 1], "E-DYN-FFI", "a C function cannot give back `object <trait>` — its tag could not be checked on the way in, and a struct "
                       "returned from C is not built yet; let C fill a value Lowent made, or take the fields and build the `object` here (RFC-0159 §9-12)");
        }
    }
    dy_rewrite(c, f, sc);
    for (proven_size_t q = 0; q < nbc; q++) dy_boundary_contract(c, f, bc[q].out ? "ensures" : "requires", bc[q].name, bc[q].t);
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
    dy_resolve(c);
    pr->diags.len = nd0;
    // `dyn` 은 `object` 없이도 적힐 수 있다 — 그러면 받는 값을 못 풀어 E-DYN-RECV 가 말한다
    proven_size_t nd1 = pr->diags.len;
    for (proven_size_t i = 0; i < pr->nforms; i++) { c->cur = dy_fmod(c, i); dy_rewrite_top(c, pr->forms[i], false); }
    work.free_fn(work.ctx, c);
    return pr->diags.len == nd1;
}
