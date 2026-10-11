// low_matrix.c — `matrix` 를 나무 위에서 `array` · `subslice` 로 옮겨 적는다 (RFC-0144 §6-2 M10–M12, 소유자 2026-10-11).
//
// 첫 걸음(2026-10-10)은 이 일을 표면 패스(`low_closer.c`)가 토큰 위에서 했다. 토큰에는 타입이 없어 모양을 **이름**에만 붙일 수
// 있었고, 그래서 구조체의 칸 · 식의 결과 · 크기를 받는 op 가 막혀 있었다. 이 패스는 파스 뒤의 나무에서 돈다:
//
//   · 선언을 먼저 다 읽는다 — 구조체의 칸마다의 타입, op 마다의 입력과 결과. 파일의 차례에 기대지 않는다.
//   · 이름마다 **적힌 타입**을 블록 단위로 들고 걷는다(이 언어의 묶기는 타입을 늘 적는다 — 추론이 없다).
//   · 식의 타입은 그것으로 따라간다: 이름 · `field x. f .` · `idx xs. i .`(구조체의 나열) · matrix 를 내는 op 의 호출.
//
// 내는 나무는 첫 걸음이 내던 토큰을 파서가 읽은 것과 같다(뒤의 선언 차례 · 계약 · 검사 · 하강이 그대로 받는다):
//
//     matrix u32 2 3 4                 →  array u32 12                  (입력이면 뒤의 걸음이 slice + 진입 계약으로)
//     lit matrix u32 2 3 4 e… .        →  lit array u32 12 e… .
//     idx m. r. c. .                   →  idx (subslice (subslice m (mul r 4) 12) 0 4) c
//     row m. r. .                      →  subslice (subslice m (mul r 4) 12) 0 4
//     dim m. 1 .                       →  4            flat m. .  →  m
//
// 크기가 번역 시점의 값 입력의 이름이면(`input comptime r u64 . … input m matrix u32 2 r c .`) 그 이름으로 식을 짓는다 —
// `mul r c`. 단형화가 이름을 수로 바꾼다. 그런 모양은 입력에만 선다(나열 리터럴과 결과는 제 길이를 수로 적어야 한다).
#include "low_cst.h"
#include "low_cst_priv.h"
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MX_MAXRANK 8
#define MX_MAXP    LOW_HDR_MAXP

typedef struct { unsigned k; const low_cst_t *elem; const low_cst_t *d[MX_MAXRANK]; } mx_shape_t;
typedef enum { MT_NONE, MT_MATRIX, MT_STRUCT, MT_LIST } mx_kind_t;
typedef struct { mx_kind_t kind; mx_shape_t sh; proven_u8str_view_t sname; } mx_ty_t;   // sname: 구조체 이름(MT_LIST 면 원소의)
typedef struct { proven_u8str_view_t sname, fname; mx_ty_t ty; } mx_field_t;
typedef struct {
    proven_u8str_view_t name; const low_cst_t *form; const char *file;
    unsigned np; proven_u8str_view_t pname[MX_MAXP]; bool pcomp[MX_MAXP]; mx_ty_t pty[MX_MAXP];
    mx_ty_t out; bool any;                               // any: 입력이나 결과에 matrix 가 있다
} mx_sig_t;
typedef struct { proven_u8str_view_t name; mx_ty_t ty; } mx_var_t;

typedef struct {
    low_parser_t p;
    void *tyc;                                           // 타입의 끝을 아는 쪽(low_using.c)
    mx_field_t *flds; proven_size_t nflds, cflds;
    proven_u8str_view_t *structs; proven_size_t nstructs, cstructs;
    mx_sig_t *sigs; proven_size_t nsigs, csigs;
    mx_var_t *env; proven_size_t nenv, cenv;
    const mx_sig_t *cur;                                 // 지금 걷는 op
    bool failed;
} mx_t;

// ── 작은 것들 ───────────────────────────────────────────────────────────────────────────────────────
static bool mx_atom(const low_cst_t *n) { return n && n->kind == LOW_CST_ATOM; }
static bool mx_is(const low_cst_t *n, const char *w) { return mx_atom(n) && n->tok.kind == LOW_TOK_IDENT && low_view_eq_cstr(n->tok.lex, w); }
static bool mx_name(const low_cst_t *n) { return mx_atom(n) && n->tok.kind == LOW_TOK_IDENT && n->tok.kw == LOW_KW_NONE; }
static proven_u8str_view_t mx_bare(proven_u8str_view_t v) { return low_view_after_last(v, '.'); }
static bool mx_veq(proven_u8str_view_t a, proven_u8str_view_t b) { return proven_u8str_view_eq(a, b); }
static bool mx_num(const low_cst_t *n, unsigned long long *out) {
    if (!mx_atom(n) || n->tok.kind != LOW_TOK_NUMBER || !n->tok.lex.size || n->tok.lex.size > 19) return false;
    unsigned long long v = 0;
    for (proven_size_t i = 0; i < n->tok.lex.size; i++) {
        if (n->tok.lex.ptr[i] < '0' || n->tok.lex.ptr[i] > '9') return false;
        v = v * 10 + (unsigned long long)(n->tok.lex.ptr[i] - '0');
    }
    *out = v;
    return true;
}
static const low_cst_t *mx_inner(const low_cst_t *n) {   // 꾸밈 괄호를 벗긴다
    while (n && n->kind == LOW_CST_GROUP && n->nkids == 1) n = n->kids[0];
    return n;
}
static void *mx_alloc(mx_t *c, proven_size_t bytes) {
    proven_result_mem_mut_t r = c->p.work.alloc_fn(c->p.work.ctx, bytes ? bytes : 1, alignof(max_align_t));
    return r.err == PROVEN_OK ? r.value.ptr : NULL;
}
static bool mx_grow(mx_t *c, void **arr, proven_size_t *cap, proven_size_t need, proven_size_t elem) {
    if (need <= *cap) return true;
    proven_size_t nc = *cap ? *cap * 2 : 32;
    while (nc < need) nc *= 2;
    void *n = mx_alloc(c, nc * elem);
    if (!n) return false;
    if (*cap) memcpy(n, *arr, *cap * elem);
    *arr = n; *cap = nc;
    return true;
}
static void mx_fail(mx_t *c, const low_cst_t *at, const char *code, const char *fmt, ...) __attribute__((format(printf, 4, 5)));
static void mx_fail(mx_t *c, const low_cst_t *at, const char *code, const char *fmt, ...) {
    low_diag_t d = { .sev = LOW_SEV_ERROR, .code = code, .line = at ? at->tok.line : 0, .col = at ? at->tok.col : 0, .file = at ? at->file : NULL };
    va_list ap; va_start(ap, fmt); vsnprintf(d.detail, sizeof d.detail, fmt, ap); va_end(ap);
    (void)proven_array_push(&c->p.out->diags, &d);
    c->p.out->ok = false; c->failed = true;
}

// ── 나무를 짓는다 ──────────────────────────────────────────────────────────────────────────────────
static low_cst_t *mx_word(mx_t *c, const low_cst_t *model, const char *w) {
    proven_size_t n = strlen(w);
    char *s = (char *)mx_alloc(c, n + 1);
    if (!s) return NULL;
    memcpy(s, w, n + 1);
    low_token_t t = model->tok;
    t.lex = (proven_u8str_view_t){ .ptr = (const proven_u8 *)s, .size = n };
    t.kind = (w[0] >= '0' && w[0] <= '9') ? LOW_TOK_NUMBER : LOW_TOK_IDENT;
    t.kw = t.kind == LOW_TOK_IDENT ? low_kw_lookup(t.lex) : LOW_KW_NONE;
    t.aux = (proven_u8str_view_t){ 0 };
    low_cst_t *a = low_node(&c->p, LOW_CST_ATOM, t);
    if (a) { a->synth = true; a->file = model->file; }
    return a;
}
static low_cst_t *mx_number(mx_t *c, const low_cst_t *model, unsigned long long v) {
    char b[32]; snprintf(b, sizeof b, "%llu", v);
    return mx_word(c, model, b);
}
static low_cst_t *mx_copy(mx_t *c, const low_cst_t *m) {   // 이름 · 수 하나를 베낀다(같은 마디를 두 자리에 걸지 않는다)
    low_cst_t *a = low_node(&c->p, m->kind, m->tok);
    if (!a) return NULL;
    a->file = m->file; a->qual_mod = m->qual_mod; a->closer = m->closer;
    if (m->nkids) {
        low_cst_t **kk = (low_cst_t **)mx_alloc(c, sizeof(low_cst_t *) * m->nkids);
        if (!kk) return NULL;
        for (proven_size_t i = 0; i < m->nkids; i++) kk[i] = mx_copy(c, m->kids[i]);
        (void)low_refit(&c->p, a, kk, m->nkids);
    }
    return a;
}
// (머리 인자…) — 괄호로 싼 폼 하나
static low_cst_t *mx_call(mx_t *c, const low_cst_t *model, const char *head, low_cst_t **args, proven_size_t n) {
    low_cst_t *kk[8];
    if (n + 1 > 8) return NULL;
    kk[0] = mx_word(c, model, head);
    for (proven_size_t i = 0; i < n; i++) kk[i + 1] = args[i];
    low_cst_t *f = low_node(&c->p, LOW_CST_FORM, kk[0] ? kk[0]->tok : model->tok);
    low_cst_t *g = low_node(&c->p, LOW_CST_GROUP, model->tok);
    if (!f || !g) return NULL;
    f->closer = LOW_TOK_EOF; f->file = model->file;
    (void)low_refit(&c->p, f, kk, n + 1);
    g->file = model->file;
    (void)low_refit(&c->p, g, &f, 1);
    return g;
}
// parent 의 자식 [s, e) 를 nk[0..n) 로 갈아 끼운다
static void mx_splice(mx_t *c, low_cst_t *parent, proven_size_t s, proven_size_t e, low_cst_t **nk, proven_size_t n) {
    proven_size_t total = parent->nkids - (e - s) + n;
    low_cst_t **kk = (low_cst_t **)mx_alloc(c, sizeof(low_cst_t *) * (total ? total : 1));
    if (!kk) return;
    proven_size_t w = 0;
    for (proven_size_t i = 0; i < s; i++) kk[w++] = parent->kids[i];
    for (proven_size_t i = 0; i < n; i++) kk[w++] = nk[i];
    for (proven_size_t i = e; i < parent->nkids; i++) kk[w++] = parent->kids[i];
    (void)low_refit(&c->p, parent, kk, total);
}

// ── 모양 ───────────────────────────────────────────────────────────────────────────────────────────
// k[i] 가 `matrix` 일 때 모양을 읽고 타입의 끝을 돌려준다(틀렸으면 0 — 꼴은 표면 패스가 이미 보았다).
static proven_size_t mx_shape_read(low_cst_t *const *k, proven_size_t i, proven_size_t end, mx_shape_t *sh) {
    unsigned long long kk = 0;
    if (i + 3 >= end + 1 || !mx_is(k[i], "matrix") || i + 2 >= end || !mx_atom(k[i + 1]) || !mx_num(k[i + 2], &kk) || kk < 2 || kk > MX_MAXRANK) return 0;
    if (i + 3 + kk > end) return 0;
    sh->k = (unsigned)kk; sh->elem = k[i + 1];
    for (unsigned q = 0; q < sh->k; q++) {
        const low_cst_t *d = k[i + 3 + q];
        unsigned long long v = 0;
        if (!(mx_num(d, &v) && v > 0) && !mx_name(d)) return 0;
        sh->d[q] = d;
    }
    return i + 3 + (proven_size_t)kk;
}
static bool mx_literal(const mx_shape_t *sh) {            // 크기가 모두 수인가
    unsigned long long v;
    for (unsigned q = 0; q < sh->k; q++) if (!mx_num(sh->d[q], &v)) return false;
    return true;
}
static unsigned long long mx_total(const mx_shape_t *sh, unsigned from) {   // 크기가 모두 수일 때의 곱
    unsigned long long t = 1, v = 0;
    for (unsigned q = from; q < sh->k; q++) { (void)mx_num(sh->d[q], &v); t *= v; }
    return t;
}
static bool mx_dim_eq(const low_cst_t *a, const low_cst_t *b) {
    unsigned long long x, y;
    bool nx = mx_num(a, &x), ny = mx_num(b, &y);
    if (nx || ny) return nx && ny && x == y;
    return mx_atom(a) && mx_atom(b) && mx_veq(a->tok.lex, b->tok.lex);
}
static bool mx_same(const mx_shape_t *a, const mx_shape_t *b) {
    if (a->k != b->k) return false;
    for (unsigned q = 0; q < a->k; q++) if (!mx_dim_eq(a->d[q], b->d[q])) return false;
    return true;
}
static void mx_shape_text(const mx_shape_t *sh, char *buf, size_t cap) {
    size_t w = 0;
    w += (size_t)snprintf(buf + w, cap - w, "%u", sh->k);
    for (unsigned q = 0; q < sh->k && w + 24 < cap; q++)
        w += (size_t)snprintf(buf + w, cap - w, " %.*s", (int)(sh->d[q]->tok.lex.size > 20 ? 20 : sh->d[q]->tok.lex.size), (const char *)sh->d[q]->tok.lex.ptr);
}
// 차원 from 부터 끝까지의 크기의 곱 — 수 하나이거나 `(mul a (mul b c))`
static low_cst_t *mx_prod(mx_t *c, const low_cst_t *model, const mx_shape_t *sh, unsigned from) {
    unsigned long long lit = 1, v = 0; low_cst_t *sym = NULL;
    for (unsigned q = sh->k; q-- > from; ) {
        if (mx_num(sh->d[q], &v)) { lit *= v; continue; }
        low_cst_t *nm = mx_copy(c, sh->d[q]);
        if (!sym) sym = nm;
        else { low_cst_t *a[2] = { nm, sym }; sym = mx_call(c, model, "mul", a, 2); }
    }
    if (!sym) return mx_number(c, model, lit);
    if (lit == 1) return sym;
    low_cst_t *a[2] = { mx_number(c, model, lit), sym };
    return mx_call(c, model, "mul", a, 2);
}

// ── 타입을 읽는다 ──────────────────────────────────────────────────────────────────────────────────
static bool mx_struct_known(const mx_t *c, proven_u8str_view_t name) {
    for (proven_size_t i = 0; i < c->nstructs; i++) if (mx_veq(c->structs[i], name)) return true;
    return false;
}
// k[s..e) 의 타입. matrix 이면 *at 에 `matrix` 의 자리, *end 에 그 끝을 적는다.
static mx_ty_t mx_ty_read(const mx_t *c, low_cst_t *const *k, proven_size_t s, proven_size_t e, proven_size_t *at, proven_size_t *end) {
    mx_ty_t t = { .kind = MT_NONE };
    while (s < e && (mx_is(k[s], "mut") || mx_is(k[s], "owned") || mx_is(k[s], "ref") || mx_is(k[s], "mut_ref"))) s++;
    if (s >= e) return t;
    if (mx_is(k[s], "matrix")) {
        proven_size_t z = mx_shape_read(k, s, e, &t.sh);
        if (z) { t.kind = MT_MATRIX; if (at) *at = s; if (end) *end = z; }
        return t;
    }
    if ((mx_is(k[s], "slice") || mx_is(k[s], "array")) && s + 1 < e && mx_name(k[s + 1]) && mx_struct_known(c, mx_bare(k[s + 1]->tok.lex))) {
        t.kind = MT_LIST; t.sname = mx_bare(k[s + 1]->tok.lex);
        return t;
    }
    if (mx_name(k[s]) && mx_struct_known(c, mx_bare(k[s]->tok.lex))) { t.kind = MT_STRUCT; t.sname = mx_bare(k[s]->tok.lex); }
    return t;
}

// ── 선언을 모은다 ──────────────────────────────────────────────────────────────────────────────────
static bool mx_is_op(const low_cst_t *f) {
    return f && f->kind == LOW_CST_FORM && f->nkids >= 2 && mx_atom(f->kids[0]) &&
           (f->kids[0]->tok.kw == LOW_KW_FN || f->kids[0]->tok.kw == LOW_KW_PROC) && mx_atom(f->kids[1]);
}
static bool mx_is_struct(const low_cst_t *f) {
    return f && f->kind == LOW_CST_FORM && f->nkids >= 3 && mx_atom(f->kids[0]) && f->kids[0]->tok.kw == LOW_KW_STRUCT &&
           mx_atom(f->kids[1]) && f->kids[f->nkids - 1]->kind == LOW_CST_BLOCK;
}
static void mx_collect_names(mx_t *c, const low_cst_t *n) {
    if (!n) return;
    if (mx_is_struct(n) && mx_grow(c, (void **)&c->structs, &c->cstructs, c->nstructs + 1, sizeof *c->structs))
        c->structs[c->nstructs++] = mx_bare(n->kids[1]->tok.lex);
    for (proven_size_t i = 0; i < n->nkids; i++) if (n->kids[i]->kind != LOW_CST_ATOM) mx_collect_names(c, n->kids[i]);
}
static void mx_collect(mx_t *c, const low_cst_t *n) {
    if (!n) return;
    if (mx_is_struct(n)) {
        const low_cst_t *blk = n->kids[n->nkids - 1];
        for (proven_size_t j = 0; j < blk->nkids; j++) {
            const low_cst_t *fl = blk->kids[j];
            if (fl->kind != LOW_CST_FORM || fl->nkids < 2 || !mx_name(fl->kids[0])) continue;
            mx_ty_t t = mx_ty_read(c, fl->kids, 1, fl->nkids, NULL, NULL);
            if (t.kind == MT_NONE || !mx_grow(c, (void **)&c->flds, &c->cflds, c->nflds + 1, sizeof *c->flds)) continue;
            c->flds[c->nflds++] = (mx_field_t){ .sname = mx_bare(n->kids[1]->tok.lex), .fname = fl->kids[0]->tok.lex, .ty = t };
        }
    }
    if (mx_is_op(n) && mx_grow(c, (void **)&c->sigs, &c->csigs, c->nsigs + 1, sizeof *c->sigs)) {
        mx_sig_t *sg = &c->sigs[c->nsigs];
        memset(sg, 0, sizeof *sg);
        low_op_header_t h = low_op_header(n);
        sg->name = mx_bare(n->kids[1]->tok.lex); sg->form = n; sg->file = n->file;
        sg->np = (unsigned)(h.np > MX_MAXP ? MX_MAXP : h.np);
        for (unsigned q = 0; q < sg->np; q++) {
            sg->pname[q] = h.p[q].name; sg->pcomp[q] = h.p[q].is_comptime;
            sg->pty[q] = mx_ty_read(c, n->kids, h.p[q].ts, h.p[q].te, NULL, NULL);
            if (sg->pty[q].kind == MT_MATRIX) sg->any = true;
        }
        if (h.out_e > h.out_s) {
            sg->out = mx_ty_read(c, n->kids, h.out_s, h.out_e, NULL, NULL);
            if (sg->out.kind == MT_NONE && h.out_e > h.out_s + 1)       // 이름 붙은 결과 — `output t matrix …`
                sg->out = mx_ty_read(c, n->kids, h.out_s + 1, h.out_e, NULL, NULL);
            if (sg->out.kind == MT_MATRIX) sg->any = true;
        }
        c->nsigs++;
    }
    for (proven_size_t i = 0; i < n->nkids; i++) if (n->kids[i]->kind != LOW_CST_ATOM) mx_collect(c, n->kids[i]);
}
static const mx_sig_t *mx_sig_of(const mx_t *c, const low_cst_t *form) {
    for (proven_size_t i = 0; i < c->nsigs; i++) if (c->sigs[i].form == form) return &c->sigs[i];
    return NULL;
}

// ── 이름의 타입 ────────────────────────────────────────────────────────────────────────────────────
static void mx_bind(mx_t *c, proven_u8str_view_t name, mx_ty_t ty) {
    if (!mx_grow(c, (void **)&c->env, &c->cenv, c->nenv + 1, sizeof *c->env)) return;
    c->env[c->nenv++] = (mx_var_t){ .name = name, .ty = ty };
}
static mx_ty_t mx_lookup(const mx_t *c, proven_u8str_view_t name) {
    for (proven_size_t i = c->nenv; i-- > 0; ) if (mx_veq(c->env[i].name, name)) return c->env[i].ty;
    return (mx_ty_t){ .kind = MT_NONE };
}
static bool mx_bound(const mx_t *c, proven_u8str_view_t name) {   // 지역 이름인가(타입이 무엇이든) — op 의 이름을 가린다
    for (proven_size_t i = c->nenv; i-- > 0; ) if (mx_veq(c->env[i].name, name)) return true;
    return false;
}
// 식의 타입 — 이름 · `field x f` · `idx xs i`(구조체의 나열) · matrix 를 내는 op 의 호출. 그 밖은 «모른다».
static mx_ty_t mx_ty_of(const mx_t *c, const low_cst_t *n) {
    mx_ty_t none = { .kind = MT_NONE };
    n = mx_inner(n);
    if (!n) return none;
    if (mx_name(n)) {
        if (mx_bound(c, n->tok.lex)) return mx_lookup(c, n->tok.lex);
        for (proven_size_t i = 0; i < c->nsigs; i++)     // 인자 없는 호출 `mk .` 은 이름 하나로 온다
            if (c->sigs[i].out.kind == MT_MATRIX && c->sigs[i].np == 0 && mx_veq(c->sigs[i].name, mx_bare(n->tok.lex)) &&
                (n->tok.lex.size != c->sigs[i].name.size || c->sigs[i].file == n->file)) return c->sigs[i].out;
        return none;
    }
    if (n->kind != LOW_CST_FORM || !n->nkids || !mx_atom(n->kids[0])) return none;
    if (mx_is(n->kids[0], "field") && n->nkids == 3 && mx_atom(n->kids[2])) {
        mx_ty_t x = mx_ty_of(c, n->kids[1]);
        if (x.kind != MT_STRUCT) return none;
        for (proven_size_t i = 0; i < c->nflds; i++)
            if (mx_veq(c->flds[i].sname, x.sname) && mx_veq(c->flds[i].fname, n->kids[2]->tok.lex)) return c->flds[i].ty;
        return none;
    }
    if (mx_is(n->kids[0], "idx") && n->nkids == 3) {
        mx_ty_t x = mx_ty_of(c, n->kids[1]);
        if (x.kind == MT_LIST) return (mx_ty_t){ .kind = MT_STRUCT, .sname = x.sname };
        return none;
    }
    if (mx_name(n->kids[0]))
        for (proven_size_t i = 0; i < c->nsigs; i++)
            if (c->sigs[i].out.kind == MT_MATRIX && mx_veq(c->sigs[i].name, mx_bare(n->kids[0]->tok.lex))) return c->sigs[i].out;
    return none;
}
static bool mx_is_matrix(const mx_t *c, const low_cst_t *n, mx_shape_t *sh) {
    mx_ty_t t = mx_ty_of(c, n);
    if (t.kind != MT_MATRIX) return false;
    if (sh) *sh = t.sh;
    return true;
}

// ── 옮겨 적는다 ────────────────────────────────────────────────────────────────────────────────────
#define MX_RANK_MSG "`idx <matrix>. i1 … ik .` takes one index per dimension — as many indexes as the type has dimensions. One row is `row m. i. .`; all the elements as one row are `flat m. .`"
#define MX_ROW_MSG  "`row <matrix>. i1 … .` takes one index for every dimension but the last and gives that row as a slice — a matrix of k dimensions takes k-1 indexes"

// 색인 n 개로 차원을 앞에서부터 벗긴다: x → (subslice (subslice x (mul i s) 남은길이) 0 s) …  n 은 k-1 이하.
static low_cst_t *mx_peel(mx_t *c, const low_cst_t *model, const mx_shape_t *sh, low_cst_t *x, low_cst_t *const *ix, unsigned n) {
    low_cst_t *cur = x;
    for (unsigned q = 0; q < n; q++) {
        low_cst_t *m[2] = { ix[q], mx_prod(c, model, sh, q + 1) };
        low_cst_t *a1[3] = { cur, mx_call(c, model, "mul", m, 2), mx_prod(c, model, sh, q) };
        low_cst_t *a2[3] = { mx_call(c, model, "subslice", a1, 3), mx_number(c, model, 0), mx_prod(c, model, sh, q + 1) };
        cur = mx_call(c, model, "subslice", a2, 3);
    }
    return cur;
}
// 머리와 인자로 된 호출 하나(괄호 안이든 문장의 맨 위든). matrix 의 꼴이면 out[0..*nout) 에 바꿔 적을 마디를 낸다:
//   *nout == 1 이면 마디 하나(값), 그보다 많으면 머리부터의 평평한 호출. 손대지 않으면 false.
static bool mx_lower_call(mx_t *c, low_cst_t *head, low_cst_t *const *args, proven_size_t na, const bool *am, const mx_shape_t *as,
                          low_cst_t **out, proven_size_t *nout) {
    if (!mx_atom(head) || !na || !am[0]) return false;
    mx_shape_t sh = as[0];
    bool is_idx = mx_is(head, "idx"), is_row = mx_is(head, "row");
    if (is_idx || is_row) {
        unsigned want = is_idx ? sh.k : sh.k - 1;
        if (na - 1 != want) { mx_fail(c, head, "E-MATRIX-INDEX", "%s", is_idx ? MX_RANK_MSG : MX_ROW_MSG); return false; }
        low_cst_t *cur = mx_peel(c, head, &sh, args[0], args + 1, sh.k - 1);
        if (!cur) return false;
        if (is_idx) { out[0] = head; out[1] = cur; out[2] = args[sh.k]; *nout = 3; }
        else {                                           // 줄 하나 — 벗긴 것이 곧 값이다(괄호를 벗겨 평평한 호출로 낸다)
            const low_cst_t *f = mx_inner(cur);
            for (proven_size_t i = 0; i < f->nkids; i++) out[i] = f->kids[i];
            *nout = f->nkids;
        }
        return true;
    }
    if (mx_is(head, "dim")) {
        unsigned long long j = 0;
        if (na != 2 || !mx_num(args[1], &j) || j >= sh.k) {
            mx_fail(c, head, "E-MATRIX-INDEX", "`dim <matrix>. <k> .` gives the size of dimension k — k is a plain decimal literal, counted from 0, below the number of dimensions");
            return false;
        }
        out[0] = mx_copy(c, sh.d[j]); *nout = 1;
        return out[0] != NULL;
    }
    if (mx_is(head, "flat")) {
        if (na != 1) { mx_fail(c, head, "E-MATRIX-USE", "`flat <matrix>. .` takes the matrix and nothing else — it is the same elements seen as one row"); return false; }
        out[0] = args[0]; *nout = 1;
        return true;
    }
    if (mx_is(head, "len")) {
        mx_fail(c, head, "E-MATRIX-USE", "`len` of a `matrix` could mean the first size or the element count, so it is not offered — `dim m. 0 .` is one size, `len flat m. . .` is the element count");
        return false;
    }
    return false;
}
// 부르는 자리의 matrix 인자와 받는 쪽의 입력을 견준다. 같은 이름의 op 가운데 하나와 맞으면 통과다.
static void mx_check_call(mx_t *c, const low_cst_t *head, low_cst_t *const *args, proven_size_t na, const bool *am, const mx_shape_t *as) {
    if (!mx_name(head)) {
        for (proven_size_t a = 0; a < na && a < MX_MAXP; a++)
            if (am[a]) {
                mx_fail(c, mx_inner(args[a])->kind == LOW_CST_ATOM ? mx_inner(args[a]) : head, "E-MATRIX-USE",
                        "a `matrix` stands whole only in these places: `idx m. i. j. .` (one index per dimension), `row m. i. .`, `dim m. 0 .`, `flat m. .`, an argument of an op whose input has the same shape, and the `return` of an op whose output has the same shape");
                return;
            }
        return;
    }
    bool any = false;
    for (proven_size_t a = 0; a < na && a < MX_MAXP; a++) any = any || am[a];
    proven_u8str_view_t nm = mx_bare(head->tok.lex);
    bool known = false, fits = false; const mx_sig_t *first = NULL;
    for (proven_size_t g = 0; g < c->nsigs && !fits; g++) {
        const mx_sig_t *sg = &c->sigs[g];
        if (!mx_veq(sg->name, nm)) continue;
        if (!known) first = sg;
        known = true;
        bool same = true;
        for (unsigned a = 0; a < MX_MAXP && same; a++) {
            bool ca = a < na && am[a], sa = a < sg->np && sg->pty[a].kind == MT_MATRIX;
            if (ca != sa) { same = false; break; }
            if (!ca) continue;
            const mx_shape_t *ps = &sg->pty[a].sh;
            if (ps->k != as[a].k) { same = false; break; }
            for (unsigned q = 0; q < ps->k && same; q++) {
                const low_cst_t *want = ps->d[q];
                if (mx_name(want))                       // 받는 쪽의 크기가 이름이면 부르는 쪽이 그 자리에 적은 값으로 읽는다
                    for (unsigned z = 0; z < sg->np && z < na; z++)
                        if (sg->pcomp[z] && mx_veq(sg->pname[z], want->tok.lex)) { want = mx_inner(args[z]); break; }
                if (!mx_dim_eq(want, as[a].d[q])) same = false;
            }
        }
        fits = same;
    }
    if (fits || (!known && !any) || (known && !any && !first->any)) return;
    if (known && !any) {                                 // matrix 를 받는 자리에 matrix 가 아닌 것을 주었다
        bool takes = false;
        for (unsigned a = 0; a < first->np; a++) if (first->pty[a].kind == MT_MATRIX) takes = true;
        if (!takes) return;
    }
    if (!known)
        mx_fail(c, head, "E-MATRIX-SHAPE", "a `matrix` is passed whole to `%.*s`, which declares no `matrix` input — read one element (`idx m. i. j. .`), one row (`row m. i. .`) or all elements as one row (`flat m. .`)",
                (int)(nm.size > 40 ? 40 : nm.size), (const char *)nm.ptr);
    else
        mx_fail(c, head, "E-MATRIX-SHAPE", "the call of `%.*s` and its inputs disagree about a `matrix` — the number of dimensions and every size must be the same (`matrix u32 2 3 4` is not `matrix u32 2 4 3`, nor `array u32 12`)",
                (int)(nm.size > 40 ? 40 : nm.size), (const char *)nm.ptr);
}

static void mx_stmt(mx_t *c, low_cst_t *f);
static void mx_block(mx_t *c, low_cst_t *blk);
static void mx_run(mx_t *c, low_cst_t *parent, proven_size_t s, proven_size_t *e, bool whole_ok);

// `lit matrix t k d… e… .` 를 `lit array t N e… .` 로. 모양을 *sh 에 적는다. matrix 리터럴이 아니면 false.
static bool mx_lit(mx_t *c, low_cst_t *f, mx_shape_t *sh) {
    if (!f || f->kind != LOW_CST_FORM || f->nkids < 2 || !mx_atom(f->kids[0]) || f->kids[0]->tok.kw != LOW_KW_LIT || !mx_is(f->kids[1], "matrix")) return false;
    proven_size_t e = mx_shape_read(f->kids, 1, f->nkids, sh);
    if (!e) { mx_fail(c, f->kids[1], "E-MATRIX-TYPE", "`lit matrix <element type> <number of dimensions> <one size per dimension> <elements>… .`"); return false; }
    if (!mx_literal(sh)) {
        mx_fail(c, f->kids[1], "E-MATRIX-TYPE", "a `lit matrix` writes every size as a number — a size that is a translation-time input stands only in an op's `input`");
        return false;
    }
    unsigned long long total = mx_total(sh, 0), have = f->nkids - e;
    bool fill = have && mx_is(f->kids[f->nkids - 1], "_");
    if (fill ? have - 1 >= total + 1 || have - 1 > total : have != total) {
        mx_fail(c, f->kids[0], "E-MATRIX-SHAPE", "a `lit matrix` lists exactly as many elements as its sizes multiply to, in row-major order — or fewer, followed by `_` to fill the rest with zero");
        return false;
    }
    low_cst_t *nk[3] = { mx_word(c, f->kids[1], "array"), f->kids[2], mx_number(c, f->kids[1], total) };
    mx_splice(c, f, 1, e, nk, 3);
    return true;
}
// 식 하나(괄호로 싼 폼이거나 잎). 안쪽부터 옮겨 적는다.
static void mx_expr(mx_t *c, low_cst_t **slot) {
    low_cst_t *n = *slot;
    if (!n || n->kind == LOW_CST_ATOM) return;
    if (n->kind == LOW_CST_BLOCK) { mx_block(c, n); return; }
    low_cst_t *f = n;
    while (f->kind == LOW_CST_GROUP && f->nkids == 1) f = f->kids[0];
    if (f->kind != LOW_CST_FORM || !f->nkids) { for (proven_size_t i = 0; i < f->nkids; i++) mx_expr(c, &f->kids[i]); return; }
    if (mx_atom(f->kids[0]) && f->kids[0]->tok.kw != LOW_KW_NONE && f->kids[0]->tok.kw != LOW_KW_LIT) { mx_stmt(c, f); return; }
    proven_size_t before = f->nkids, e = f->nkids;
    mx_run(c, f, 0, &e, false);
    if (before > 1 && f->nkids == 1) *slot = f->kids[0];   // `dim m. 1 .` → 4 · `flat m. .` → m: 폼이 값 하나가 됐다
}
// 구조체 리터럴의 몸 — 칸이 matrix 이면 그 값은 같은 모양의 `lit matrix`
static void mx_struct_lit(mx_t *c, proven_u8str_view_t sname, low_cst_t *blk) {
    for (proven_size_t j = 0; j < blk->nkids; j++) {
        low_cst_t *fl = blk->kids[j];
        if (fl->kind != LOW_CST_FORM || fl->nkids < 2 || !mx_atom(fl->kids[0])) continue;
        const mx_field_t *fd = NULL;
        for (proven_size_t i = 0; i < c->nflds; i++)
            if (mx_veq(c->flds[i].sname, sname) && mx_veq(c->flds[i].fname, fl->kids[0]->tok.lex)) fd = &c->flds[i];
        low_cst_t *v = (low_cst_t *)mx_inner(fl->kids[1]);
        mx_shape_t sh;
        if (fd && fd->ty.kind == MT_MATRIX && fl->nkids == 2 && mx_lit(c, v, &sh)) {
            if (!mx_same(&sh, &fd->ty.sh))
                mx_fail(c, v->kids[0], "E-MATRIX-SHAPE", "the `lit matrix` has a different shape from the field it fills — the number of dimensions and every size must agree");
            continue;
        }
        proven_size_t e = fl->nkids;
        mx_run(c, fl, 1, &e, false);
    }
}
// parent 의 자식 [s, *e) 가 값 하나다: 잎 하나이거나, 머리와 인자들의 평평한 호출. 옮겨 적으면 *e 가 바뀐다.
static void mx_run(mx_t *c, low_cst_t *parent, proven_size_t s, proven_size_t *e, bool whole_ok) {
    if (s >= *e) return;
    // 구조체 리터럴 `lit <이름> do … end` 은 칸마다 본다
    if (mx_atom(parent->kids[s]) && parent->kids[s]->tok.kw == LOW_KW_LIT && s + 1 < *e && parent->kids[s + 1]->kind == LOW_CST_FORM &&
        parent->kids[s + 1]->nkids >= 2 && mx_name(parent->kids[s + 1]->kids[0]) &&
        parent->kids[s + 1]->kids[parent->kids[s + 1]->nkids - 1]->kind == LOW_CST_BLOCK) {
        low_cst_t *sl = parent->kids[s + 1];
        mx_struct_lit(c, mx_bare(sl->kids[0]->tok.lex), sl->kids[sl->nkids - 1]);
        return;
    }
    if (parent->kind == LOW_CST_FORM && s == 0 && mx_name(parent->kids[0]) && *e >= 2 && parent->kids[*e - 1]->kind == LOW_CST_BLOCK &&
        mx_struct_known(c, mx_bare(parent->kids[0]->tok.lex))) {   // 괄호 안의 `<이름> do … end`
        mx_struct_lit(c, mx_bare(parent->kids[0]->tok.lex), parent->kids[*e - 1]);
        return;
    }
    // 인자가 matrix 인가는 **옮겨 적기 전에** 본다 — `total flat t. . .` 의 인자는 matrix 가 아니라 그것을 편 줄이다
    bool am[MX_MAXP] = { false }; mx_shape_t as[MX_MAXP];
    for (proven_size_t a = 0; s + 1 + a < *e && a < MX_MAXP; a++) am[a] = mx_is_matrix(c, parent->kids[s + 1 + a], &as[a]);
    for (proven_size_t i = s + 1; i < *e; i++) mx_expr(c, &parent->kids[i]);
    if (c->failed) return;
    if (*e - s == 1) {
        mx_expr(c, &parent->kids[s]);
        if (!whole_ok && mx_is_matrix(c, parent->kids[s], NULL) && mx_inner(parent->kids[s])->kind == LOW_CST_ATOM)
            mx_fail(c, mx_inner(parent->kids[s]), "E-MATRIX-USE",
                    "a `matrix` stands whole only in these places: `idx m. i. j. .` (one index per dimension), `row m. i. .`, `dim m. 0 .`, `flat m. .`, an argument of an op whose input has the same shape, and the `return` of an op whose output has the same shape");
        return;
    }
    low_cst_t *head = parent->kids[s];
    low_cst_t *out[16]; proven_size_t nout = 0;
    if (mx_lower_call(c, head, parent->kids + s + 1, *e - s - 1, am, as, out, &nout)) {
        mx_splice(c, parent, s, *e, out, nout);
        *e = s + nout;
        return;
    }
    if (c->failed) return;
    if (mx_atom(head)) mx_check_call(c, head, parent->kids + s + 1, *e - s - 1, am, as);
}
// 값의 머리가 op 의 호출(또는 맨 이름)이면 그 이름의 마디를 돌려준다
static const low_cst_t *mx_value_head(const low_cst_t *parent, proven_size_t s, proven_size_t e) {
    if (s >= e) return NULL;
    const low_cst_t *n = parent->kids[s];
    if (e - s == 1) { n = mx_inner(n); if (n && n->kind == LOW_CST_FORM && n->nkids) n = n->kids[0]; }
    return mx_name(n) ? n : NULL;
}
// 그 이름의 op 가 내는 matrix 의 모양. bare: 인자도 한정도 없는 이름 — 제 파일의 op 만 본다(지역 이름일 수 있다).
static bool mx_callee_out(const mx_t *c, const low_cst_t *head, bool bare, bool *known, bool *all_mx, mx_shape_t *first) {
    proven_u8str_view_t nm = mx_bare(head->tok.lex);
    bool k = false, all = true, got = false;
    for (proven_size_t g = 0; g < c->nsigs; g++) {
        const mx_sig_t *sg = &c->sigs[g];
        if (!mx_veq(sg->name, nm)) continue;
        if (bare && nm.size == head->tok.lex.size && sg->file != head->file) continue;
        k = true;
        if (sg->out.kind == MT_MATRIX) { if (!got) { *first = sg->out.sh; got = true; } }
        else all = false;
    }
    *known = k; *all_mx = k && all;
    return got;
}
static bool mx_callee_gives(const mx_t *c, const low_cst_t *head, const mx_shape_t *want) {
    proven_u8str_view_t nm = mx_bare(head->tok.lex);
    for (proven_size_t g = 0; g < c->nsigs; g++)
        if (mx_veq(c->sigs[g].name, nm) && c->sigs[g].out.kind == MT_MATRIX && mx_same(&c->sigs[g].out.sh, want)) return true;
    return false;
}
// matrix 타입의 낱말 [at, end) 를 `array t N` 으로
static bool mx_type_to_array(mx_t *c, low_cst_t *parent, proven_size_t at, proven_size_t end, const mx_shape_t *sh, const char *where) {
    if (!mx_literal(sh)) {
        mx_fail(c, parent->kids[at], "E-MATRIX-TYPE", "%s writes every size of a `matrix` as a number — a size that is a translation-time input stands only in an op's `input`", where);
        return false;
    }
    low_cst_t *nk[3] = { mx_word(c, parent->kids[at], "array"), parent->kids[at + 1], mx_number(c, parent->kids[at], mx_total(sh, 0)) };
    mx_splice(c, parent, at, end, nk, 3);
    return true;
}

static void mx_let(mx_t *c, low_cst_t *f) {
    proven_size_t b = 0, n = f->nkids;
    for (proven_size_t i = 2; i < n; i++) if (mx_atom(f->kids[i]) && f->kids[i]->tok.kw == LOW_KW_BE) { b = i; break; }
    if (!b || !mx_atom(f->kids[1])) { for (proven_size_t i = 1; i < n; i++) mx_expr(c, &f->kids[i]); return; }
    proven_size_t ve = n;
    if (ve > b + 1 && f->kids[ve - 1]->kind == LOW_CST_FORM && f->kids[ve - 1]->nkids && mx_atom(f->kids[ve - 1]->kids[0]) &&
        f->kids[ve - 1]->kids[0]->tok.kw == LOW_KW_ELSE) ve--;
    proven_size_t ts = b + 1, te = ts, at = 0;
    mx_ty_t decl = mx_ty_read(c, f->kids, ts, ve, &at, &te);
    if (decl.kind != MT_MATRIX) {
        proven_size_t z = low_tyctx_type_end(c->tyc, f->kids, ts, ve);
        te = (z == (proven_size_t)-1 || z >= ve) ? ts : z;   // 끝까지가 타입이면 값이 없다 — 그것은 타입이 아니라 값이다
        if (te > ts && decl.kind == MT_NONE) decl = mx_ty_read(c, f->kids, ts, te, NULL, NULL);
        if (te == ts) decl.kind = MT_NONE;
    }
    proven_size_t vs = te;
    mx_shape_t lsh; bool has_lit = false;
    low_cst_t *litf = NULL;
    if (ve - vs == 1) {                                  // 나열 리터럴은 파서가 괄호로 싸서 준다
        litf = (low_cst_t *)mx_inner(f->kids[vs]);
        if (!litf || litf->kind != LOW_CST_FORM || !litf->nkids) litf = NULL;
    }
    if (litf && litf->nkids >= 2 && mx_atom(litf->kids[0]) && litf->kids[0]->tok.kw == LOW_KW_LIT && mx_is(litf->kids[1], "matrix"))
        has_lit = mx_lit(c, litf, &lsh);
    if (c->failed) return;
    mx_ty_t bound = decl;
    if (decl.kind == MT_MATRIX) {
        const low_cst_t *vh = mx_value_head(f, vs, ve);
        if (has_lit) {
            if (!mx_same(&decl.sh, &lsh)) { mx_fail(c, litf->kids[0], "E-MATRIX-SHAPE", "the `lit matrix` has a different shape from the type written before it — the number of dimensions and every size must agree"); return; }
            mx_splice(c, f, at, te, NULL, 0);            // 뒤의 걸음은 나열 리터럴의 타입을 리터럴에서 읽는다
        } else {
            if (litf && mx_atom(litf->kids[0]) && litf->kids[0]->tok.kw == LOW_KW_LIT) {
                mx_fail(c, litf->kids[0], "E-MATRIX-SHAPE", "this name is a `matrix`, so its value is a `lit matrix` of the same shape (or an op that returns that shape)"); return;
            }
            if (vh && ve - vs == 1 && mx_atom(f->kids[vs]) && mx_lookup(c, vh->tok.lex).kind == MT_MATRIX) {
                mx_fail(c, vh, "E-MATRIX-USE", "a `matrix` is not copied by binding it to another name — pass it to an op, or build the new one with `lit matrix … _ .` and fill it"); return;
            }
            if (!vh) { mx_fail(c, f->kids[vs < ve ? vs : b], "E-MATRIX-SHAPE", "the value of a `matrix` name is a `lit matrix` of the same shape, or a call of an op whose output is that shape"); return; }
            if (!mx_callee_gives(c, vh, &decl.sh)) {
                bool known, all; mx_shape_t fs; char want[48] = "", got[48] = "";
                bool has = mx_callee_out(c, vh, false, &known, &all, &fs);
                mx_shape_text(&decl.sh, got, sizeof got);
                if (has) mx_shape_text(&fs, want, sizeof want);
                mx_fail(c, vh, "E-MATRIX-SHAPE", "this `matrix` name has shape `%s`, but its value `%.*s` %s%s%s — the shapes must be the same", got,
                        (int)(vh->tok.lex.size > 40 ? 40 : vh->tok.lex.size), (const char *)vh->tok.lex.ptr,
                        !known ? "is neither a `matrix` name here nor an op of this program" : has ? "returns shape `" : "does not return a `matrix`",
                        has ? want : "", has ? "`" : "");
                return;
            }
            proven_size_t e2 = ve;
            if (ve - vs > 1) { mx_run(c, f, vs, &e2, true); }
            else mx_expr(c, &f->kids[vs]);
            if (c->failed) return;
            if (!mx_type_to_array(c, f, at, te, &decl.sh, "a binding")) return;
        }
    } else if (decl.kind == MT_NONE && ve - vs >= 2 && mx_atom(f->kids[vs]) && f->kids[vs]->tok.kw == LOW_KW_LIT &&
               f->kids[vs + 1]->kind == LOW_CST_FORM && f->kids[vs + 1]->nkids && mx_name(f->kids[vs + 1]->kids[0]) &&
               mx_struct_known(c, mx_bare(f->kids[vs + 1]->kids[0]->tok.lex))) {
        // 타입을 안 적은 구조체 리터럴 — `var z lit board do … end .`: 이름의 타입은 그 구조체다
        bound = (mx_ty_t){ .kind = MT_STRUCT, .sname = mx_bare(f->kids[vs + 1]->kids[0]->tok.lex) };
        proven_size_t e2 = ve;
        mx_run(c, f, vs, &e2, false);
    } else if (decl.kind == MT_NONE && litf && litf->nkids >= 3 && mx_atom(litf->kids[0]) && litf->kids[0]->tok.kw == LOW_KW_LIT &&
               (mx_is(litf->kids[1], "array") || mx_is(litf->kids[1], "slice")) && mx_name(litf->kids[2]) &&
               mx_struct_known(c, mx_bare(litf->kids[2]->tok.lex))) {
        // 타입을 안 적은 구조체의 나열 — `let bs lit array board 2 a. b. .`: 이름의 타입은 그 구조체의 줄이다
        bound = (mx_ty_t){ .kind = MT_LIST, .sname = mx_bare(litf->kids[2]->tok.lex) };
        mx_expr(c, &f->kids[vs]);
    } else if (has_lit) {
        bound = (mx_ty_t){ .kind = MT_MATRIX, .sh = lsh };
        if (te > ts) { mx_fail(c, f->kids[ts], "E-MATRIX-SHAPE", "the value is a `lit matrix`, and the type written before it is not a `matrix` of that shape"); return; }
    } else {
        const low_cst_t *vh = mx_value_head(f, vs, ve);
        bool known = false, all = false; mx_shape_t fs;
        if (vh && !mx_bound(c, vh->tok.lex) && mx_callee_out(c, vh, ve - vs == 1 && mx_atom(f->kids[vs]), &known, &all, &fs) && all) {
            char shs[48]; mx_shape_text(&fs, shs, sizeof shs);
            mx_fail(c, vh, "E-MATRIX-SHAPE", "`%.*s` returns a `matrix` (shape `%s`), and this place is not a `matrix` of that shape — bind it to a name declared with that shape, or return it from an op whose `output` is that shape",
                    (int)(vh->tok.lex.size > 40 ? 40 : vh->tok.lex.size), (const char *)vh->tok.lex.ptr, shs);
            return;
        }
        proven_size_t e2 = ve;
        mx_run(c, f, vs, &e2, false);
    }
    if (f->nkids && f->kids[f->nkids - 1]->kind == LOW_CST_FORM && f->kids[f->nkids - 1]->nkids && mx_atom(f->kids[f->nkids - 1]->kids[0]) &&
        f->kids[f->nkids - 1]->kids[0]->tok.kw == LOW_KW_ELSE) mx_stmt(c, f->kids[f->nkids - 1]);
    mx_bind(c, f->kids[1]->tok.lex, bound);
}
static void mx_return(mx_t *c, low_cst_t *f) {
    proven_size_t s = 1, e = f->nkids;
    bool mo = c->cur && c->cur->out.kind == MT_MATRIX;
    if (s >= e) return;
    const low_cst_t *vh = mx_value_head(f, s, e);
    mx_shape_t vsh;
    bool single = e - s == 1;
    const low_cst_t *v0 = single ? mx_inner(f->kids[s]) : NULL;
    bool name_mx = v0 && mx_name(v0) && mx_lookup(c, v0->tok.lex).kind == MT_MATRIX && mx_is_matrix(c, v0, &vsh);
    bool field_mx = v0 && v0->kind == LOW_CST_FORM && v0->nkids && mx_is(v0->kids[0], "field") && mx_is_matrix(c, v0, &vsh);
    const low_cst_t *litn = single ? mx_inner(f->kids[s]) : f->kids[s];
    bool is_lit = litn && ((litn->kind == LOW_CST_FORM && litn->nkids && mx_atom(litn->kids[0]) && litn->kids[0]->tok.kw == LOW_KW_LIT) || (mx_atom(litn) && litn->tok.kw == LOW_KW_LIT));
    if (name_mx || field_mx) {
        if (!mo || !mx_same(&c->cur->out.sh, &vsh)) mx_fail(c, mx_inner(f->kids[s])->kind == LOW_CST_ATOM ? mx_inner(f->kids[s]) : f->kids[0], "E-MATRIX-SHAPE", "this `return` gives a `matrix`, and the op's `output` does not declare the same shape");
        return;
    }
    if (mo && !is_lit) {
        if (!vh) { mx_fail(c, f->kids[0], "E-MATRIX-SHAPE", "this op's `output` is a `matrix`, so `return` gives a `matrix` name of that shape or a call of an op that returns that shape"); return; }
        if (!mx_callee_gives(c, vh, &c->cur->out.sh)) {
            bool known, all; mx_shape_t fs; char want[48] = "", got[48] = "";
            bool has = mx_callee_out(c, vh, false, &known, &all, &fs);
            mx_shape_text(&c->cur->out.sh, got, sizeof got);
            if (has) mx_shape_text(&fs, want, sizeof want);
            mx_fail(c, vh, "E-MATRIX-SHAPE", "this op's `output` is a `matrix` of shape `%s`, but its value `%.*s` %s%s%s — the shapes must be the same", got,
                    (int)(vh->tok.lex.size > 40 ? 40 : vh->tok.lex.size), (const char *)vh->tok.lex.ptr,
                    !known ? "is neither a `matrix` name here nor an op of this program" : has ? "returns shape `" : "does not return a `matrix`",
                    has ? want : "", has ? "`" : "");
            return;
        }
        mx_run(c, f, s, &e, true);
        return;
    }
    if (!mo && vh && !mx_bound(c, vh->tok.lex)) {
        bool known = false, all = false; mx_shape_t fs;
        if (mx_callee_out(c, vh, single && mx_atom(f->kids[s]), &known, &all, &fs) && all) {
            char shs[48]; mx_shape_text(&fs, shs, sizeof shs);
            mx_fail(c, vh, "E-MATRIX-SHAPE", "`%.*s` returns a `matrix` (shape `%s`), and this place is not a `matrix` of that shape — bind it to a name declared with that shape, or return it from an op whose `output` is that shape",
                    (int)(vh->tok.lex.size > 40 ? 40 : vh->tok.lex.size), (const char *)vh->tok.lex.ptr, shs);
            return;
        }
    }
    mx_run(c, f, s, &e, false);
}
static proven_size_t mx_run_end(const low_cst_t *f, proven_size_t s) {   // 값이 끝나는 자리 — 블록이나 실패 절 앞
    proven_size_t e = s;
    while (e < f->nkids && f->kids[e]->kind != LOW_CST_BLOCK &&
           !(f->kids[e]->kind == LOW_CST_FORM && f->kids[e]->nkids && mx_atom(f->kids[e]->kids[0]) && f->kids[e]->kids[0]->tok.kw != LOW_KW_NONE &&
             f->kids[e]->kids[0]->tok.kw != LOW_KW_LIT) &&
           !(mx_atom(f->kids[e]) && (f->kids[e]->tok.kw == LOW_KW_ELSE || f->kids[e]->tok.kw == LOW_KW_DO))) e++;
    return e;
}
static void mx_op(mx_t *c, low_cst_t *f);
static void mx_stmt(mx_t *c, low_cst_t *f) {
    if (!f || c->failed) return;
    if (f->kind != LOW_CST_FORM || !f->nkids) { mx_expr(c, &f); return; }
    if (mx_is_op(f)) { mx_op(c, f); return; }
    if (mx_is_struct(f)) return;                         // 칸은 이미 옮겨 적었다
    low_cst_t *h = f->kids[0];
    low_kw_t kw = mx_atom(h) ? h->tok.kw : LOW_KW_NONE;
    if (kw == LOW_KW_LET || kw == LOW_KW_VAR) { mx_let(c, f); return; }
    if (kw == LOW_KW_RETURN) { mx_return(c, f); return; }
    proven_size_t mark = c->nenv, s = 1;
    if (kw == LOW_KW_FOR) {
        if (f->nkids >= 2 && mx_atom(f->kids[1])) mx_bind(c, f->kids[1]->tok.lex, (mx_ty_t){ .kind = MT_NONE });
        for (proven_size_t i = 2; i < f->nkids; i++) {
            if (f->kids[i]->kind == LOW_CST_BLOCK) continue;
            if (mx_inner(f->kids[i])->kind == LOW_CST_ATOM && mx_is_matrix(c, f->kids[i], NULL)) {
                mx_fail(c, mx_inner(f->kids[i]), "E-MATRIX-USE", "`for` does not walk a `matrix` — count the dimensions (`repeat r u64 dim m. 0 . do …`), walk one row (`for x row m. r. . do …`) or all the elements as one row (`for x flat m. . do …`)");
                return;
            }
        }
        s = 2;
    } else if (kw == LOW_KW_SET) {
        if (f->nkids >= 2) {
            if (mx_inner(f->kids[1])->kind == LOW_CST_ATOM && mx_is_matrix(c, f->kids[1], NULL)) {
                mx_fail(c, mx_inner(f->kids[1]), "E-MATRIX-USE", "`set` writes one element of a `matrix` — `set idx m. i. j. . v .`; a whole matrix is not assigned");
                return;
            }
            mx_expr(c, &f->kids[1]);
        }
        s = 2;
    } else if (kw == LOW_KW_NONE && mx_atom(h)) s = 0;   // 문장으로 선 호출
    proven_size_t e = mx_run_end(f, s);
    if (kw == LOW_KW_FOR) { for (proven_size_t i = s; i < e; i++) mx_expr(c, &f->kids[i]); }
    else if (e > s) { proven_size_t e2 = e; mx_run(c, f, s, &e2, false); e = e2; }
    for (proven_size_t i = e; i < f->nkids && !c->failed; i++) {
        low_cst_t *k = f->kids[i];
        if (k->kind == LOW_CST_BLOCK) mx_block(c, k);
        else if (k->kind == LOW_CST_FORM) mx_stmt(c, k);
        else if (k->kind == LOW_CST_GROUP) mx_expr(c, &f->kids[i]);
    }
    c->nenv = mark;
}
static void mx_block(mx_t *c, low_cst_t *blk) {
    proven_size_t mark = c->nenv;
    for (proven_size_t i = 0; i < blk->nkids && !c->failed; i++) {
        if (blk->kids[i]->kind == LOW_CST_FORM) mx_stmt(c, blk->kids[i]);
        else mx_expr(c, &blk->kids[i]);
    }
    c->nenv = mark;
}
// op 하나 — 머리의 matrix 타입을 옮겨 적고, 입력과 이름 붙은 결과를 묶은 채로 몸을 걷는다
static void mx_op(mx_t *c, low_cst_t *f) {
    const mx_sig_t *sg = mx_sig_of(c, f), *save = c->cur;
    proven_size_t mark = c->nenv, tmark = low_tyctx_enter(c->tyc, f);
    low_op_header_t h = low_op_header(f);
    if (sg) {
        for (unsigned q = 0; q < sg->np; q++) mx_bind(c, sg->pname[q], sg->pty[q]);
        if (sg->out.kind == MT_MATRIX && h.out_e > h.out_s + 1 && mx_name(f->kids[h.out_s]) && !mx_is(f->kids[h.out_s], "matrix"))
            mx_bind(c, f->kids[h.out_s]->tok.lex, sg->out);
    }
    c->cur = sg;
    for (proven_size_t i = 2; i < f->nkids && !c->failed; i++)           // 계약 안의 `dim m. 0 .` · `idx …` — 아래에서 짓는 계약보다 먼저
        if (f->kids[i]->kind == LOW_CST_GROUP) mx_expr(c, &f->kids[i]);
    // 뒤에서부터 옮겨 적는다 — 앞의 자리가 밀리지 않는다
    struct { proven_size_t s, e; int q; } sp[MX_MAXP + 1]; unsigned nsp = 0;
    for (proven_size_t q = 0; q < h.np && q < MX_MAXP; q++) { sp[nsp].s = h.p[q].ts; sp[nsp].e = h.p[q].te; sp[nsp].q = (int)q; nsp++; }
    if (h.out_e > h.out_s) { sp[nsp].s = h.out_s; sp[nsp].e = h.out_e; sp[nsp].q = -1; nsp++; }
    for (unsigned a = 0; a < nsp; a++)
        for (unsigned b = a + 1; b < nsp; b++)
            if (sp[b].s > sp[a].s) { __typeof__(sp[0]) t = sp[a]; sp[a] = sp[b]; sp[b] = t; }
    for (unsigned a = 0; a < nsp && !c->failed; a++) {
        proven_size_t at = 0, end = 0;
        mx_ty_t t = mx_ty_read(c, f->kids, sp[a].s, sp[a].e, &at, &end);
        if (t.kind != MT_MATRIX && sp[a].q < 0 && sp[a].e > sp[a].s + 1) t = mx_ty_read(c, f->kids, sp[a].s + 1, sp[a].e, &at, &end);
        if (t.kind != MT_MATRIX) continue;
        if (mx_literal(&t.sh) || sp[a].q < 0) { (void)mx_type_to_array(c, f, at, end, &t.sh, "an op's `output`"); continue; }
        // 크기가 이름인 입력 — `slice t`. 길이의 진입 계약은 짓지 않는다: 이 자리에 올 수 있는 것은 부르는 쪽에서 모양을 견준 matrix
        //   뿐이고(다른 것은 `E-MATRIX-SHAPE`), 원소 읽기는 `subslice` 가 실제 길이로 검사한다. 계약으로 적으면 단형화 뒤에
        //   `eq (len m) (mul 3 4)` 가 되는데 진입 검사는 그 식을 짓지 못한다(`W-CONTRACT-IGNORED`).
        low_cst_t *nk[2] = { mx_word(c, f->kids[at], "slice"), f->kids[at + 1] };
        mx_splice(c, f, at, end, nk, 2);
    }
    for (proven_size_t i = 2; i < f->nkids && !c->failed; i++)
        if (f->kids[i]->kind == LOW_CST_BLOCK) mx_block(c, f->kids[i]);
    c->cur = save; c->nenv = mark;
    low_tyctx_leave(c->tyc, tmark);
}
// 구조체의 칸 — `g matrix u32 2 3 4 .` → `g array u32 12 .`
static void mx_struct_fields(mx_t *c, low_cst_t *n) {
    if (!n) return;
    if (mx_is_struct(n)) {
        low_cst_t *blk = n->kids[n->nkids - 1];
        for (proven_size_t j = 0; j < blk->nkids && !c->failed; j++) {
            low_cst_t *fl = blk->kids[j];
            if (fl->kind != LOW_CST_FORM || fl->nkids < 2) continue;
            proven_size_t at = 0, end = 0;
            mx_ty_t t = mx_ty_read(c, fl->kids, 1, fl->nkids, &at, &end);
            if (t.kind == MT_MATRIX) (void)mx_type_to_array(c, fl, at, end, &t.sh, "a struct field");
        }
        return;
    }
    for (proven_size_t i = 0; i < n->nkids; i++) if (n->kids[i]->kind != LOW_CST_ATOM) mx_struct_fields(c, n->kids[i]);
}
// 남은 `matrix` 타입 — 받아 주는 자리가 아닌 곳에 적혔다
static void mx_sweep(mx_t *c, const low_cst_t *n) {
    if (!n || c->failed) return;
    unsigned long long k;
    for (proven_size_t i = 0; i + 2 < n->nkids; i++)
        if (mx_is(n->kids[i], "matrix") && mx_atom(n->kids[i + 1]) && mx_num(n->kids[i + 2], &k)) {
            mx_fail(c, n->kids[i], "E-MATRIX-USE", "a `matrix` type is written where a NAME is made — `let` / `var`, an op's `input`, an op's `output`, a struct field. It is not an element type, a type argument or an alias, and a `lit matrix` is the value of a binding or of a struct field");
            return;
        }
    for (proven_size_t i = 0; i < n->nkids; i++) if (n->kids[i]->kind != LOW_CST_ATOM) mx_sweep(c, n->kids[i]);
}

// 낱말 `matrix` 가 하나도 없으면 할 일이 없다 — 거의 모든 프로그램이 그렇다
static bool mx_mentions(const low_cst_t *n) {
    if (!n) return false;
    if (mx_is(n, "matrix")) return true;
    for (proven_size_t i = 0; i < n->nkids; i++) if (mx_mentions(n->kids[i])) return true;
    return false;
}
bool low_matrix_lower(low_parse_result_t *pr, proven_allocator_t node_alloc, proven_allocator_t work) {
    bool any = false;
    for (proven_size_t i = 0; i < pr->nforms && !any; i++) any = mx_mentions(pr->forms[i]);
    if (!any) return true;
    mx_t *c = (mx_t *)work.alloc_fn(work.ctx, sizeof(mx_t), alignof(mx_t)).value.ptr;
    if (!c) return true;
    memset(c, 0, sizeof *c);
    c->p = (low_parser_t){ .node_alloc = node_alloc, .work = work, .out = pr };
    c->tyc = low_tyctx_open(pr, node_alloc, work);
    if (!c->tyc) return true;
    for (proven_size_t i = 0; i < pr->nforms; i++) mx_collect_names(c, pr->forms[i]);
    for (proven_size_t i = 0; i < pr->nforms; i++) mx_collect(c, pr->forms[i]);
    for (proven_size_t i = 0; i < pr->nforms && !c->failed; i++) mx_struct_fields(c, pr->forms[i]);
    for (proven_size_t i = 0; i < pr->nforms && !c->failed; i++) {
        low_tyctx_form(c->tyc, pr->forms[i]);
        c->nenv = 0; c->cur = NULL;
        mx_stmt(c, pr->forms[i]);
    }
    for (proven_size_t i = 0; i < pr->nforms && !c->failed; i++) mx_sweep(c, pr->forms[i]);
    return !c->failed;
}
