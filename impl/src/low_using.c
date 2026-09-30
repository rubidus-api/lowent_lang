/* low_using.c — **객체마다 얼로케이터를 고른다**: `using` 절의 해석 (RFC-0112 D8 · WO-0214).
 *
 * ★★★★ 소유자 요청(2026-09-13): *"객체를 선언할 때, 기본 얼로케이터와 사용자 얼로케이터를 원하는
 *   대로 선택할 수 있는 문법과 구조가 있어야 하지 않을까. 따로 지정하지 않으면 상황에 맞춰 cap 또는
 *   effect로 제공되는 기본 얼로케이터를 디폴트로 쓰고."* · *"얼로케이터가 전역이면 안되고 매개변수로
 *   넘어가는게 안전한 구조가 아닐까요."*
 *
 * 두 쪽이 있다.
 *   · **받는 쪽** — op 머리의 `using <이름> <타입> .` 절. 그 op 이 깎아 쓰는 얼로케이터다. arity 에 **들지
 *     않는다**(부르는 쪽은 그 인자를 위치로 적지 않는다 — 한 개념 한 철자). `<타입>` 이 comptime 타입
 *     매개변수면 그 매개변수도 부르는 쪽이 적지 않는다(얼로케이터의 타입에서 온다).
 *   · **부르는 쪽** — 바인딩의 `using <출처>`(`let v using hb be T vecgen.open u32 16 .`), 아니면 기본값.
 *
 * 기본값 격자 — 위에서 처음 맞는 것(D8(4)):
 *   1. 바인딩의 `using <출처>`
 *   2. 이 op 의 `using` 절 이름(타입이 맞으면)
 *   3. 이 op 의 입력·바인딩 가운데 타입이 맞는 **유일한** 것
 *   4. 없으면 `E-ALLOC-NOSOURCE`, 둘 이상이면 `E-ALLOC-AMBIGUOUS`
 *   ★ 필드는 세지 않는다. **op 경계를 넘지 않는다** — 그것이 Odin/Jai 의 전역 컨텍스트와의 경계다.
 *
 * ★★ 왜 여기(나무 **뒤**, 단형화 **앞**)인가: 부르는 자리에 (타입 인자, 값 인자)를 **끼워 넣고**, 받는 쪽의
 *   `using` 절을 첫 실행 입력으로 **바꿔 적으면**, 그 뒤의 단형화·검사·하강은 오늘과 **같은 나무**를 본다.
 *   검사기에서 다시 쓰면 늦다 — 단형화가 먼저 돌아 인자 모자란 호출을 인스턴스로 찍어 버린다(검토단 F1).
 *   생략된 인자를 arity 정규화가 셀 수 없기 때문에 `using` 은 입력이 아니라 **절**이다(RFC-0112 §6 D8(2)).
 *
 * ★ 내장 `alloc_bytes capacity n`(뿌리 피연산자 생략)은 **열린 영역 블록 안에서만** 여기서 채운다(D8(7)):
 *   가장 안쪽 영역. 권한은 이름으로 적는다 — 뿌리 피연산자는 얼로케이터가 아니라 **권한**이다.
 */
#include "low_cst_priv.h"
#include "low_cst.h"
#include "low_ir.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#define US_MAXOPS   4096
#define US_MAXSCOPE 256

typedef struct {
    const low_cst_t    *form;       // 받는 op (원래 머리)
    proven_u8str_view_t name;       // op 이름(bare)
    proven_u8str_view_t mod;        // 선언 모듈
    proven_u8str_view_t uname;      // using 이름
    proven_u8str_view_t utype;      // using 타입 낱말
    proven_u8str_view_t trait;      // `requires <trait> <utype>` 의 trait (utype 이 매개변수일 때)
    int                 tpos;       // utype 이 **접히는 comptime 매개변수**면 그 차례, 아니면 -1
    proven_size_t       nt;         // 접히는 comptime 매개변수 수(utype 포함)
} us_callee_t;

typedef struct { proven_u8str_view_t name, type; bool is_using; } us_bind_t;

typedef struct {
    low_parser_t p;
    us_callee_t *cal; proven_size_t ncal;
    // 지금 걷는 op 의 자리
    const low_cst_t *op;
    proven_u8str_view_t curmod;
    us_bind_t bind[US_MAXSCOPE]; proven_size_t nbind;
    proven_u8str_view_t rgn[16]; proven_size_t nrgn;
} us_ctx_t;

static bool us_eq(proven_u8str_view_t a, const char *s) { return low_view_eq_cstr(a, s); }
static bool us_atom(const low_cst_t *n) { return n && n->kind == LOW_CST_ATOM; }
static proven_u8str_view_t us_bare(proven_u8str_view_t v) {
    for (proven_size_t i = v.size; i-- > 0; )
        if (v.ptr[i] == (proven_u8)'.') return (proven_u8str_view_t){ .ptr = v.ptr + i + 1, .size = v.size - i - 1 };
    return v;
}
static proven_u8str_view_t us_qual(proven_u8str_view_t v) {
    for (proven_size_t i = v.size; i-- > 0; )
        if (v.ptr[i] == (proven_u8)'.') return (proven_u8str_view_t){ .ptr = v.ptr, .size = i };
    return (proven_u8str_view_t){ 0 };
}
static low_cst_t *us_atom_like(us_ctx_t *c, const low_cst_t *model, proven_u8str_view_t lex) {
    low_token_t t = model->tok;
    t.kind = LOW_TOK_IDENT; t.kw = LOW_KW_NONE; t.lex = lex; t.aux = (proven_u8str_view_t){ 0 };
    return low_node(&c->p, LOW_CST_ATOM, t);
}
// ★★★ **고정 길이 입력 `array <타입> <개수>`** (정본 §6.2.6 (1) · 2026-09-14 · 차례는 RFC-0132 C12 로 2026-09-27 뒤집음 —
//   SIMD `vec <타입> <레인>` 과 같은 차례).
//   처리기는 `array` 를 `slice` 의 다른 이름으로 다뤘다 — 바로 뒤 낱말을 원소 타입으로 읽고 **길이는 버렸다.**
//   그래서 정본 모양 `array 4 u64` 는 원소를 모르는 **바이트 슬라이스**가 됐고(`4` 가 원소 자리), 틀린 차례
//   `array u64 4` 는 길이 검사 없는 `slice u64` 였다. 둘 다 조용히 통과했다.
//   ⇒ 입력 자리의 `array T N` 을 **`slice T` + 진입 계약 `requires eq (len <이름>) N .`** 으로 바꿔 적는다.
//     «길이는 타입의 일부» 가 진입 검사로 선다(계약이므로 부르는 쪽이 상수를 주면 번역 시점에도 걸린다).
//   그 밖의 자리(출력·지역·칸·틀린 차례)는 바꾸지 않고 두어 검사기가 `E-TYPE-ARRAY` 로 거절한다.
static bool us_is_int_lit(const low_cst_t *n) {
    if (!n || n->kind != LOW_CST_ATOM || n->tok.kind != LOW_TOK_NUMBER || !n->tok.lex.size) return false;
    for (proven_size_t i = 0; i < n->tok.lex.size; i++)
        if (n->tok.lex.ptr[i] < '0' || n->tok.lex.ptr[i] > '9') return false;
    return true;
}
static void us_arrays_one(us_ctx_t *c, low_cst_t *f) {
    if (!f || f->kind != LOW_CST_FORM || f->nkids < 2 || !us_atom(f->kids[0]) ||
        (f->kids[0]->tok.kw != LOW_KW_FN && f->kids[0]->tok.kw != LOW_KW_PROC)) return;
    low_op_header_t h = low_op_header(f);
    enum { UA_MAX = 16 };
    proven_size_t at[UA_MAX]; proven_u8str_view_t nm[UA_MAX]; proven_size_t na = 0;
    for (proven_size_t q = 0; q < h.np && na < UA_MAX; q++)
        for (proven_size_t z = h.p[q].ts; z + 2 < h.p[q].te && z + 2 < f->nkids; z++)
            if (us_atom(f->kids[z]) && us_eq(f->kids[z]->tok.lex, "array") &&
                us_atom(f->kids[z + 1]) && !us_is_int_lit(f->kids[z + 1]) && us_is_int_lit(f->kids[z + 2])) {
                at[na] = z; nm[na] = h.p[q].name; na++;
                break;
            }
    if (!na) return;
    // 넣을 자리 — 계약의 차례(정본 §6.4.1): `ensures`·`errors`·`tests`·`schedule` 앞, 없으면 몸 앞
    proven_size_t end = (f->kids[f->nkids - 1]->kind == LOW_CST_BLOCK) ? f->nkids - 1 : f->nkids;
    proven_size_t ins = end;
    for (proven_size_t i = 2; i < end; i++)
        if (us_atom(f->kids[i]) && (us_eq(f->kids[i]->tok.lex, "ensures") || us_eq(f->kids[i]->tok.lex, "errors") ||
                                    us_eq(f->kids[i]->tok.lex, "tests") || us_eq(f->kids[i]->tok.lex, "schedule"))) { ins = i; break; }
    proven_size_t n = f->nkids - na + 4 * na;
    low_cst_t **nk = (low_cst_t **)c->p.work.alloc_fn(c->p.work.ctx, sizeof(low_cst_t *) * n, alignof(low_cst_t *)).value.ptr;
    if (!nk) return;
    proven_size_t m = 0;
    for (proven_size_t i = 0; i <= f->nkids; i++) {
        if (i == ins)
            for (proven_size_t a = 0; a < na; a++) {
                const low_cst_t *model = f->kids[at[a]];
                low_cst_t *lenf = low_node(&c->p, LOW_CST_FORM, model->tok);
                low_cst_t *lk[2] = { us_atom_like(c, model, (proven_u8str_view_t){ .ptr = (const proven_u8 *)"len", .size = 3 }),
                                     us_atom_like(c, model, nm[a]) };
                lenf->closer = LOW_TOK_EOF;
                (void)low_refit(&c->p, lenf, lk, 2);
                low_cst_t *grp = low_node(&c->p, LOW_CST_GROUP, model->tok);
                (void)low_refit(&c->p, grp, &lenf, 1);
                nk[m++] = us_atom_like(c, model, (proven_u8str_view_t){ .ptr = (const proven_u8 *)"requires", .size = 8 });
                nk[m++] = us_atom_like(c, model, (proven_u8str_view_t){ .ptr = (const proven_u8 *)"eq", .size = 2 });
                nk[m++] = grp;
                nk[m++] = f->kids[at[a] + 2];          // 개수 리터럴을 계약으로 옮긴다(RFC-0132 C12: `array <타입> <개수>`)
            }
        if (i == f->nkids) break;
        bool drop = false, swap = false;
        for (proven_size_t a = 0; a < na; a++) { if (i == at[a] + 2) drop = true; if (i == at[a]) swap = true; }
        if (drop) continue;
        nk[m++] = swap ? us_atom_like(c, f->kids[i], (proven_u8str_view_t){ .ptr = (const proven_u8 *)"slice", .size = 5 })
                       : f->kids[i];
    }
    (void)low_refit(&c->p, f, nk, m);
}

static bool us_is_op(const low_cst_t *f) {
    return f && f->kind == LOW_CST_FORM && f->nkids >= 2 && us_atom(f->kids[0]) &&
           (f->kids[0]->tok.kw == LOW_KW_FN || f->kids[0]->tok.kw == LOW_KW_PROC);
}

// ── ① 받는 쪽을 모은다 ────────────────────────────────────────────────────────
static void us_collect_one(us_ctx_t *c, const low_cst_t *f, proven_u8str_view_t mod) {
    if (!us_is_op(f)) return;
    proven_size_t at = f->nkids;
    for (proven_size_t i = 2; i + 2 < f->nkids; i++)
        if (us_atom(f->kids[i]) && us_eq(f->kids[i]->tok.lex, "using")) {
            if (at != f->nkids) {
                low_pdiag(&c->p, "E-USING-DUP",
                          "an op draws from at most ONE allocator — this header has two `using` clauses. Take the "
                          "second one as an ordinary input (RFC-0112 D8)", f->kids[i]->tok.line, f->kids[i]->tok.col);
                return;
            }
            at = i;
        }
    if (at == f->nkids) return;
    if (!us_atom(f->kids[at + 1]) || !us_atom(f->kids[at + 2]) || low_is_clause_word(f->kids[at + 2]->tok.lex)) {
        low_pdiag(&c->p, "E-USING-FORM",
                  "a `using` clause names the allocator and its type: `using <name> <type> .` (RFC-0112 D8)",
                  f->kids[at]->tok.line, f->kids[at]->tok.col);
        return;
    }
    // ★ WO-0218 — `using` 의 자리(차례 4)는 **여기서** 본다. 검사기가 보는 머리는 이 패스가 `using` 을 `input` 으로
    //   바꿔 적은 뒤라서, `using` 을 comptime 입력 앞에 적어도 검사기는 못 본다(실측: vm_using.low 가 그랬고 초록이었다).
    for (proven_size_t i = 2; i < f->nkids; i++) {
        if (i == at || !us_atom(f->kids[i]) || !low_is_clause_word(f->kids[i]->tok.lex)) continue;
        proven_size_t e = i + 1;
        while (e < f->nkids && us_atom(f->kids[e]) && !low_is_clause_word(f->kids[e]->tok.lex)) e++;
        int r = us_eq(f->kids[i]->tok.lex, "input") ? low_input_rank(f, i, e) : low_clause_rank(f->kids[i]->tok.lex);
        if (r < 0) continue;
        if (i < at && r > 4) {
            low_pdiag(&c->p, "E-CLAUSE-ORDER",
                      "`using` comes after a clause that belongs behind it. `using` stands after the `comptime` and "
                      "capability/region inputs and before the data inputs, `output` and everything after them (WO-0217)",
                      f->kids[at]->tok.line, f->kids[at]->tok.col);
            break;
        }
        if (i > at && r < 4) {
            low_pdiag(&c->p, "E-CLAUSE-ORDER",
                      "`using` comes before a clause that belongs in front of it — a `comptime` input, a capability/region "
                      "input, `satisfies`/`lowdoc` or `vector`/`priority`. `using` stands after those and before the data inputs (WO-0217)",
                      f->kids[at]->tok.line, f->kids[at]->tok.col);
            break;
        }
    }
    if (c->ncal >= US_MAXOPS) return;
    us_callee_t *u = &c->cal[c->ncal++];
    *u = (us_callee_t){ .form = f, .name = us_bare(f->kids[1]->tok.lex), .mod = mod,
                        .uname = f->kids[at + 1]->tok.lex, .utype = f->kids[at + 2]->tok.lex, .tpos = -1 };
    low_op_header_t h = low_op_header(f);     // ★ 여기서는 using 이 **없는 척하지 않은** 머리 — np 에 타입 매개변수가 든다
    proven_size_t k = 0;
    for (proven_size_t q = 0; q < h.np; q++) {
        if (!h.p[q].is_comptime) continue;
        if (h.p[q].core < f->nkids && us_atom(f->kids[h.p[q].core]) && us_eq(f->kids[h.p[q].core]->tok.lex, "type")) {
            if (proven_u8str_view_eq(h.p[q].name, u->utype)) u->tpos = (int)k;
            k++;
        }
    }
    u->nt = k;
    // `requires <trait> <utype> .` — 매개변수의 경계
    for (proven_size_t i = 2; i + 2 < f->nkids; i++)
        if (us_atom(f->kids[i]) && us_eq(f->kids[i]->tok.lex, "requires") && us_atom(f->kids[i + 1]) &&
            us_atom(f->kids[i + 2]) && proven_u8str_view_eq(f->kids[i + 2]->tok.lex, u->utype))
            u->trait = us_bare(f->kids[i + 1]->tok.lex);
}

// ── ② 받는 쪽 머리를 바꿔 적는다: `using n T .` → 마지막 comptime 입력 뒤의 `input n T .` ──
static void us_rewrite_header(us_ctx_t *c, const us_callee_t *u) {
    low_cst_t *f = (low_cst_t *)u->form;
    proven_size_t at = f->nkids;
    for (proven_size_t i = 2; i + 2 < f->nkids; i++)
        if (us_atom(f->kids[i]) && us_eq(f->kids[i]->tok.lex, "using")) { at = i; break; }
    if (at == f->nkids) return;
    // ★ WO-0217 — 넣는 자리: 마지막 comptime·권한·영역 입력 절의 끝(없으면 이름 바로 뒤). 곧 데이터 입력의 앞이다 —
    //   `using` 이 선 자리(차례 4)와 같다. comptime 뒤에만 넣으면 권한 입력보다 앞에 서서 차례를 어긴다.
    //   ★ 2026-09-15 — `satisfies`·`lowdoc`·`vector` 도 그 앞에 서므로(차례 0~1) 그 끝까지 본다.
    proven_size_t ins = 2;
    for (proven_size_t i = 2; i < f->nkids; i++) {
        if (i == at || !us_atom(f->kids[i]) || !low_is_clause_word(f->kids[i]->tok.lex)) continue;
        proven_size_t e = i + 1;
        while (e < f->nkids && us_atom(f->kids[e]) && !low_is_clause_word(f->kids[e]->tok.lex)) e++;
        int r = us_eq(f->kids[i]->tok.lex, "input") ? low_input_rank(f, i, e) : low_clause_rank(f->kids[i]->tok.lex);
        if (r >= 0 && r <= 3 && e > ins) ins = e;
    }
    proven_size_t n = f->nkids;
    low_cst_t **nk = (low_cst_t **)c->p.work.alloc_fn(c->p.work.ctx, sizeof(low_cst_t *) * (n + 1), alignof(low_cst_t *)).value.ptr;
    if (!nk) return;
    proven_size_t m = 0;
    low_cst_t *kw = us_atom_like(c, f->kids[at], proven_u8str_view_from_cstr("input"));
    for (proven_size_t i = 0; i < n; i++) {
        if (i == ins) { nk[m++] = kw; nk[m++] = f->kids[at + 1]; nk[m++] = f->kids[at + 2]; }
        if (i >= at && i <= at + 2) continue;
        nk[m++] = f->kids[i];
    }
    if (ins >= n) { nk[m++] = kw; nk[m++] = f->kids[at + 1]; nk[m++] = f->kids[at + 2]; }
    (void)low_refit(&c->p, f, nk, m);
}

// ── ③ 부르는 쪽 ────────────────────────────────────────────────────────────────
static const us_callee_t *us_find_callee(us_ctx_t *c, proven_u8str_view_t head) {
    proven_u8str_view_t b = us_bare(head), q = us_qual(head);
    for (proven_size_t i = 0; i < c->ncal; i++) {
        if (!proven_u8str_view_eq(c->cal[i].name, b)) continue;
        if (q.size ? proven_u8str_view_eq(c->cal[i].mod, q) : proven_u8str_view_eq(c->cal[i].mod, c->curmod))
            return &c->cal[i];
    }
    return NULL;
}
// 그 타입이 trait 을 갖춘 actor 인가 · 또는 이 op 의 같은 경계를 가진 comptime 매개변수인가
static bool us_type_satisfies(us_ctx_t *c, proven_u8str_view_t ty, proven_u8str_view_t trait) {
    const low_parse_result_t *pr = c->p.out;
    proven_u8str_view_t b = us_bare(ty);
    for (proven_size_t i = 0; i < pr->nforms; i++) {
        const low_cst_t *f = pr->forms[i];
        if (!f || f->kind != LOW_CST_FORM || f->nkids < 3 || !us_atom(f->kids[0]) ||
            (f->kids[0]->tok.kw != LOW_KW_ACTOR && f->kids[0]->tok.kw != LOW_KW_STRUCT) ||
            !us_atom(f->kids[1]) || !proven_u8str_view_eq(f->kids[1]->tok.lex, b)) continue;
        const low_cst_t *blk = f->kids[f->nkids - 1];
        if (blk->kind != LOW_CST_BLOCK) continue;
        for (proven_size_t j = 0; j < blk->nkids; j++) {
            const low_cst_t *s = blk->kids[j];
            if (s->kind == LOW_CST_FORM && s->nkids >= 2 && us_atom(s->kids[0]) && us_eq(s->kids[0]->tok.lex, "satisfies"))
                for (proven_size_t z = 1; z < s->nkids; z++)
                    if (us_atom(s->kids[z]) && proven_u8str_view_eq(us_bare(s->kids[z]->tok.lex), trait)) return true;
        }
    }
    // 이 op 의 comptime 타입 매개변수이고 같은 trait 경계를 가졌는가
    const low_cst_t *op = c->op;
    for (proven_size_t i = 2; i + 2 < op->nkids; i++)
        if (us_atom(op->kids[i]) && us_eq(op->kids[i]->tok.lex, "requires") && us_atom(op->kids[i + 1]) &&
            us_atom(op->kids[i + 2]) && proven_u8str_view_eq(op->kids[i + 2]->tok.lex, ty) &&
            proven_u8str_view_eq(us_bare(op->kids[i + 1]->tok.lex), trait)) return true;
    return false;
}
static bool us_type_fits(us_ctx_t *c, const us_callee_t *u, proven_u8str_view_t ty) {
    if (!ty.size) return false;
    if (u->tpos < 0) return proven_u8str_view_eq(us_bare(ty), us_bare(u->utype));
    return u->trait.size && us_type_satisfies(c, ty, u->trait);
}
// 바인딩 폼의 타입 첫 낱말(한정자 건너뜀)
static proven_u8str_view_t us_type_word(const low_cst_t *f, proven_size_t from) {
    for (proven_size_t q = from; q < f->nkids && us_atom(f->kids[q]); q++) {
        proven_u8str_view_t w = f->kids[q]->tok.lex;
        if (us_eq(w, "mut") || us_eq(w, "owned") || us_eq(w, "comptime")) continue;
        if (f->kids[q]->tok.kw == LOW_KW_BE || us_eq(w, "using")) break;
        return w;
    }
    return (proven_u8str_view_t){ 0 };
}
static void us_push_bind(us_ctx_t *c, proven_u8str_view_t name, proven_u8str_view_t type, bool is_using) {
    if (c->nbind < US_MAXSCOPE) c->bind[c->nbind++] = (us_bind_t){ .name = name, .type = type, .is_using = is_using };
}
static proven_u8str_view_t us_bind_type(us_ctx_t *c, proven_u8str_view_t name) {
    for (proven_size_t i = c->nbind; i-- > 0; )
        if (proven_u8str_view_eq(c->bind[i].name, name)) return c->bind[i].type;
    return (proven_u8str_view_t){ 0 };
}

// 호출 폼 하나에 인자를 끼운다. `explicit_src` 가 있으면 그것, 없으면 격자.
static bool us_fill_call(us_ctx_t *c, low_cst_t *call, const us_callee_t *u, const low_cst_t *explicit_src) {
    proven_u8str_view_t src = { 0 }, srctype = { 0 };
    if (explicit_src) {
        src = explicit_src->tok.lex;
        srctype = us_bind_type(c, src);
        if (!srctype.size) {
            low_pdiag(&c->p, "E-ALLOC-NOSOURCE",
                      "`using` names something that is not an input or a binding of THIS op. The allocator an "
                      "object draws from must be visible in the op that creates it — a default never crosses the "
                      "op boundary (RFC-0112 D1(4))", explicit_src->tok.line, explicit_src->tok.col);
            return false;
        }
    } else {
        // 2. 이 op 의 using 이름
        for (proven_size_t i = 0; i < c->nbind; i++)
            if (c->bind[i].is_using && us_type_fits(c, u, c->bind[i].type)) { src = c->bind[i].name; srctype = c->bind[i].type; break; }
        // 3. 유일한 후보
        if (!src.size) {
            proven_size_t hits = 0;
            for (proven_size_t i = 0; i < c->nbind; i++)
                if (us_type_fits(c, u, c->bind[i].type)) {
                    bool dup = false;                       // 같은 이름이 가려진 경우는 하나로 센다
                    for (proven_size_t j = i + 1; j < c->nbind; j++)
                        if (proven_u8str_view_eq(c->bind[j].name, c->bind[i].name)) dup = true;
                    if (dup) continue;
                    hits++; src = c->bind[i].name; srctype = c->bind[i].type;
                }
            if (hits > 1) {
                low_pdiag(&c->p, "E-ALLOC-AMBIGUOUS",
                          "this call draws from an allocator, and MORE THAN ONE value of a fitting type is in scope — "
                          "the tool will not guess which one you meant. Say it on the binding: "
                          "`let <name> <type> using <allocator> be …` (RFC-0112 D8(4))", call->kids[0]->tok.line, call->kids[0]->tok.col);
                return false;
            }
            if (!hits) {
                low_pdiag(&c->p, "E-ALLOC-NOSOURCE",
                          "this call draws from an allocator, but NO value of a fitting type is visible in this op — "
                          "no input, no binding, no `using` clause. There is no global allocator: take one as an "
                          "input, create one here, or declare `using <name> <type> .` on this op (RFC-0112 D8(4))",
                          call->kids[0]->tok.line, call->kids[0]->tok.col);
                return false;
            }
        }
    }
    // 끼운다: 타입 인자(매개변수일 때) · 값 인자(접히는 comptime 뒤)
    proven_size_t n = call->nkids;
    low_cst_t **nk = (low_cst_t **)c->p.work.alloc_fn(c->p.work.ctx, sizeof(low_cst_t *) * (n + 2), alignof(low_cst_t *)).value.ptr;
    if (!nk) return false;
    proven_size_t m = 0;
    proven_size_t type_at = u->tpos >= 0 ? 1 + (proven_size_t)u->tpos : (proven_size_t)-1;
    proven_size_t val_at = 1 + u->nt;         // 끼운 뒤의 자리
    for (proven_size_t i = 0; i <= n; i++) {
        if (m == type_at) nk[m++] = us_atom_like(c, call->kids[0], srctype);
        if (m == val_at) nk[m++] = us_atom_like(c, call->kids[0], src);
        if (i < n) nk[m++] = call->kids[i];
    }
    (void)low_refit(&c->p, call, nk, m);
    return true;
}

static void us_walk(us_ctx_t *c, low_cst_t *nd);

// `alloc_bytes capacity n` — 뿌리 피연산자를 채운다(D8(7))
static void us_fill_root(us_ctx_t *c, low_cst_t *nd) {
    for (proven_size_t j = 0; j + 1 < nd->nkids; j++) {
        if (!us_atom(nd->kids[j]) || !us_eq(nd->kids[j]->tok.lex, "alloc_bytes")) continue;
        if (!us_atom(nd->kids[j + 1]) || !us_eq(nd->kids[j + 1]->tok.lex, "capacity")) continue;
        // ★ 영역 블록만 채운다. 권한(`cap allocator`·`cap heap`)은 **이름으로 적어야** 한다 — 권한 하나를 서명에서
        //   골라 쥐여 주면 «이름 없이 뿌리에 닿는 길» 이 하나 생긴다(RFC-0043 D1 · 검토단 Q6 «위치 cap 하나만»).
        //   영역은 다르다: 블록이 **어휘로** 열려 있고 그 수명 안에서만 깎으므로 가장 안쪽이 곧 뜻이다.
        if (!c->nrgn) continue;
        proven_u8str_view_t src = c->rgn[c->nrgn - 1];
        proven_size_t n = nd->nkids, m = 0;
        low_cst_t **nk = (low_cst_t **)c->p.work.alloc_fn(c->p.work.ctx, sizeof(low_cst_t *) * (n + 1), alignof(low_cst_t *)).value.ptr;
        if (!nk) return;
        for (proven_size_t i = 0; i < n; i++) {
            nk[m++] = nd->kids[i];
            if (i == j) nk[m++] = us_atom_like(c, nd->kids[j], src);
        }
        (void)low_refit(&c->p, nd, nk, m);
        j++;
    }
}

static void us_walk_block(us_ctx_t *c, low_cst_t *blk) {
    proven_size_t save = c->nbind;
    for (proven_size_t i = 0; i < blk->nkids; i++) us_walk(c, blk->kids[i]);
    c->nbind = save;
}

static void us_walk(us_ctx_t *c, low_cst_t *nd) {
    if (!nd || nd->kind == LOW_CST_ATOM) return;
    if (nd->kind == LOW_CST_BLOCK) { us_walk_block(c, nd); return; }
    // region 블록 — 이름을 쌓고 몸을 걷는다
    if (nd->kind == LOW_CST_FORM && nd->nkids >= 3 && us_atom(nd->kids[0]) && us_eq(nd->kids[0]->tok.lex, "region") &&
        us_atom(nd->kids[1]) && c->nrgn < 16) {
        c->rgn[c->nrgn++] = nd->kids[1]->tok.lex;
        for (proven_size_t i = 2; i < nd->nkids; i++) us_walk(c, nd->kids[i]);
        c->nrgn--;
        return;
    }
    if (nd->kind == LOW_CST_FORM) us_fill_root(c, nd);
    // 바인딩: `let|var <이름> <타입…> [using <출처>] be <식>`
    if (nd->kind == LOW_CST_FORM && nd->nkids >= 3 && us_atom(nd->kids[0]) &&
        (nd->kids[0]->tok.kw == LOW_KW_LET || nd->kids[0]->tok.kw == LOW_KW_VAR) && us_atom(nd->kids[1])) {
        proven_size_t be = nd->nkids, us = nd->nkids;
        for (proven_size_t i = 2; i < nd->nkids; i++) {
            if (us_atom(nd->kids[i]) && nd->kids[i]->tok.kw == LOW_KW_BE) { be = i; break; }
            if (us_atom(nd->kids[i]) && us_eq(nd->kids[i]->tok.lex, "using")) us = i;
        }
        const low_cst_t *explicit_src = NULL;
        bool explicit_src_told = false;                   // 이미 E-LIT-USING 으로 말했다
        bool keep = us < be && nd->kids[us]->is_keep;     // RFC-0135 D12 — 블록 끝에 돌려주지 않는다
        if (us < be) {
            if (us + 2 != be || !us_atom(nd->kids[us + 1])) {
                low_pdiag(&c->p, "E-USING-FORM",
                          "a binding names its allocator right before `be`: `let <name> <type> using <allocator> be …` "
                          "(RFC-0112 D8(3))", nd->kids[us]->tok.line, nd->kids[us]->tok.col);
            } else explicit_src = nd->kids[us + 1];
            // `using <출처>` 두 낱말을 폼에서 뺀다 — 뒤의 모든 소비자는 오늘의 바인딩을 본다
            proven_size_t n = nd->nkids, m = 0;
            low_cst_t **nk = (low_cst_t **)c->p.work.alloc_fn(c->p.work.ctx, sizeof(low_cst_t *) * n, alignof(low_cst_t *)).value.ptr;
            if (nk) {
                for (proven_size_t i = 0; i < n; i++) if (i != us && i != us + 1) nk[m++] = nd->kids[i];
                (void)low_refit(&c->p, nd, nk, m);
                be -= 2;
            }
        }
        // 초기식의 머리 호출
        low_cst_t *init = be + 1 < nd->nkids ? nd->kids[be + 1] : NULL;
        low_cst_t *call = init;
        while (call && call->kind == LOW_CST_GROUP && call->nkids) call = call->kids[0];
        const us_callee_t *u = NULL;
        if (call && call->kind == LOW_CST_FORM && call->nkids && us_atom(call->kids[0])) u = us_find_callee(c, call->kids[0]->tok.lex);
        else if (call && us_atom(call) && call == init && be + 1 < nd->nkids) {
            // ★ 인자 0 인 호출은 나무가 폼으로 안 묶는다(`be two_takes .`) — 얼로케이터를 끼울 자리를 만들려고
            //   폼 한 겹으로 싼다. 뜻은 같다(괄호 한 겹).
            u = us_find_callee(c, call->tok.lex);
            if (u) {
                low_cst_t *fm = low_node(&c->p, LOW_CST_FORM, call->tok);
                low_cst_t **one = (low_cst_t **)c->p.work.alloc_fn(c->p.work.ctx, sizeof(low_cst_t *), alignof(low_cst_t *)).value.ptr;
                if (fm && one) {
                    one[0] = call;
                    (void)low_refit(&c->p, fm, one, 1);
                    nd->kids[be + 1] = fm;
                    init = call = fm;
                } else u = NULL;
            }
        }
        // ★★ RFC-0132 §13.7 — 나열을 고른 할당자에서 받는다. 값을 [using, (send <출처> reserve <바이트>), <나열>] 로
        //   바꾼다(정규화가 만든 폼 — `synth`). 뒤의 모든 검사(효과 · send 대조)는 평범한 `send` 를 본다.
        {
            const low_cst_t *lg = init;
            while (lg && lg->kind == LOW_CST_GROUP && lg->nkids == 1) lg = lg->kids[0];
            bool is_list = lg && lg->kind == LOW_CST_FORM && lg->nkids >= 3 && us_atom(lg->kids[0]) && lg->kids[0]->tok.kw == LOW_KW_LIT &&
                           us_atom(lg->kids[1]) && us_atom(lg->kids[2]) &&
                           (us_eq(lg->kids[1]->tok.lex, "array") || us_eq(lg->kids[1]->tok.lex, "slice") ||
                            (us_eq(lg->kids[1]->tok.lex, "vec") && lg->kids[lg->nkids - 1]->kind != LOW_CST_BLOCK));
            // ★ RFC-0135 S2a — `lit <구조체> do … end` 도 받는다: 바이트 수는 `size_of <구조체>`(번역 시점에 접힌다)
            const low_cst_t *sl = NULL;
            if (!is_list && lg && lg->kind == LOW_CST_FORM && lg->nkids == 2 && us_atom(lg->kids[0]) && lg->kids[0]->tok.kw == LOW_KW_LIT) {
                sl = lg->kids[1];
                while (sl && sl->kind == LOW_CST_GROUP && sl->nkids == 1) sl = sl->kids[0];
                if (!(sl && sl->kind == LOW_CST_FORM && sl->nkids == 2 && us_atom(sl->kids[0]) && sl->kids[1]->kind == LOW_CST_BLOCK)) sl = NULL;
            }
            if (keep && !(is_list || sl))
                low_pdiag(&c->p, "E-USING-FORM", "`keep` says bytes built from an allocator are not given back at the end of the "
                          "block — it goes on a list or struct literal: `var t using al keep be mut slice u8 lit … . else … .` "
                          "(RFC-0135 D12)", explicit_src ? explicit_src->tok.line : nd->kids[0]->tok.line,
                          explicit_src ? explicit_src->tok.col : nd->kids[0]->tok.col);
            if (explicit_src && (is_list || sl)) {
                bool opt = be > 2 && us_atom(nd->kids[2]) && us_eq(nd->kids[2]->tok.lex, "option");
                if ((is_list && us_eq(lg->kids[1]->tok.lex, "vec")) || !opt) {
                    explicit_src_told = true;
                    low_pdiag(&c->p, "E-LIT-USING", is_list && us_eq(lg->kids[1]->tok.lex, "vec")
                              ? "a SIMD value lives in lanes, not in bytes an allocator hands out — `using` does not apply to `lit vec`"
                              : sl ? "a struct built in an allocator's bytes can fail to get them — say what happens then: "
                                "`var q using al be pt lit pt do … end . else return … .`, or keep the option: "
                                "`let qo using al be option pt lit pt do … end .` (RFC-0135 §4.2)"
                              : "a list built from an allocator can fail to get its bytes — say what happens then: "
                                "`var buf using al be mut slice u8 lit array u8 16 _ . . else return … .`, or keep the option: "
                                "`let bo using al be option mut slice u8 lit array u8 16 _ . .` (RFC-0135 §4.2)",
                              explicit_src->tok.line, explicit_src->tok.col);
                } else {
                    proven_u8str_view_t ty = is_list ? lg->kids[2]->tok.lex : (proven_u8str_view_t){ 0 };
                    unsigned esz = us_eq(ty, "u16") || us_eq(ty, "i16") ? 2 : us_eq(ty, "u32") || us_eq(ty, "i32") || us_eq(ty, "f32") ? 4 :
                                   us_eq(ty, "u64") || us_eq(ty, "i64") || us_eq(ty, "f64") || us_eq(ty, "usize") || us_eq(ty, "isize") ? 8 : 1;
                    unsigned long long cnt = 0;
                    if (!is_list) cnt = 0;
                    else if (us_eq(lg->kids[1]->tok.lex, "array")) {
                        if (lg->nkids > 3 && us_atom(lg->kids[3])) cnt = strtoull((const char *)lg->kids[3]->tok.lex.ptr, NULL, 0);
                    } else for (proven_size_t q = 3; q < lg->nkids; q++) if (!(us_atom(lg->kids[q]) && us_eq(lg->kids[q]->tok.lex, "_"))) cnt++;
                    char *num = (char *)c->p.node_alloc.alloc_fn(c->p.node_alloc.ctx, 24, 1).value.ptr;
                    low_cst_t **sk = (low_cst_t **)c->p.work.alloc_fn(c->p.work.ctx, sizeof(low_cst_t *) * 4, alignof(low_cst_t *)).value.ptr;
                    low_cst_t **uk = (low_cst_t **)c->p.work.alloc_fn(c->p.work.ctx, sizeof(low_cst_t *) * 3, alignof(low_cst_t *)).value.ptr;
                    low_cst_t **gk = (low_cst_t **)c->p.work.alloc_fn(c->p.work.ctx, sizeof(low_cst_t *), alignof(low_cst_t *)).value.ptr;
                    low_cst_t *sf = low_node(&c->p, LOW_CST_FORM, explicit_src->tok), *sg = low_node(&c->p, LOW_CST_GROUP, explicit_src->tok);
                    low_cst_t *uf = low_node(&c->p, LOW_CST_FORM, explicit_src->tok);
                    if (num && sk && uk && gk && sf && sg && uf) {
                        snprintf(num, 24, "%llu", cnt * esz);
                        sk[0] = us_atom_like(c, explicit_src, (proven_u8str_view_t){ .ptr = (const proven_u8 *)"send", .size = 4 });
                        sk[1] = (low_cst_t *)explicit_src;
                        sk[2] = us_atom_like(c, explicit_src, (proven_u8str_view_t){ .ptr = (const proven_u8 *)"reserve", .size = 7 });
                        sk[3] = us_atom_like(c, explicit_src, (proven_u8str_view_t){ .ptr = (const proven_u8 *)num, .size = strlen(num) });
                        // ★ RFC-0132 T2b-3d — 원소가 구조체인 나열: 바이트 수는 (mul <개수> (size_of <구조체>))
                        bool elem_struct = is_list && !(us_eq(ty, "u8") || us_eq(ty, "i8") || us_eq(ty, "u16") || us_eq(ty, "i16") ||
                                           us_eq(ty, "u32") || us_eq(ty, "i32") || us_eq(ty, "u64") || us_eq(ty, "i64") || us_eq(ty, "f32") ||
                                           us_eq(ty, "f64") || us_eq(ty, "usize") || us_eq(ty, "isize") || us_eq(ty, "bool"));
                        if (elem_struct && sk[3]) {
                            char *cn = (char *)c->p.node_alloc.alloc_fn(c->p.node_alloc.ctx, 24, 1).value.ptr;
                            low_cst_t **mk = (low_cst_t **)c->p.work.alloc_fn(c->p.work.ctx, sizeof(low_cst_t *) * 3, alignof(low_cst_t *)).value.ptr;
                            low_cst_t **zk = (low_cst_t **)c->p.work.alloc_fn(c->p.work.ctx, sizeof(low_cst_t *) * 2, alignof(low_cst_t *)).value.ptr;
                            low_cst_t **g1 = (low_cst_t **)c->p.work.alloc_fn(c->p.work.ctx, sizeof(low_cst_t *), alignof(low_cst_t *)).value.ptr;
                            low_cst_t **g2 = (low_cst_t **)c->p.work.alloc_fn(c->p.work.ctx, sizeof(low_cst_t *), alignof(low_cst_t *)).value.ptr;
                            low_cst_t *mf = low_node(&c->p, LOW_CST_FORM, explicit_src->tok), *mg = low_node(&c->p, LOW_CST_GROUP, explicit_src->tok);
                            low_cst_t *zf = low_node(&c->p, LOW_CST_FORM, explicit_src->tok), *zg = low_node(&c->p, LOW_CST_GROUP, explicit_src->tok);
                            low_cst_t *m0 = us_atom_like(c, explicit_src, (proven_u8str_view_t){ .ptr = (const proven_u8 *)"mul", .size = 3 });
                            low_cst_t *z0 = us_atom_like(c, explicit_src, (proven_u8str_view_t){ .ptr = (const proven_u8 *)"size_of", .size = 7 });
                            low_cst_t *z1 = us_atom_like(c, lg->kids[2], ty);
                            if (cn && mk && zk && g1 && g2 && mf && mg && zf && zg && m0 && z0 && z1) {
                                snprintf(cn, 24, "%llu", cnt);
                                low_cst_t *m1 = us_atom_like(c, explicit_src, (proven_u8str_view_t){ .ptr = (const proven_u8 *)cn, .size = strlen(cn) });
                                if (m1) {
                                    m1->tok.kind = LOW_TOK_NUMBER;
                                    zk[0] = z0; zk[1] = z1; (void)low_refit(&c->p, zf, zk, 2); zf->synth = true;
                                    g1[0] = zf; (void)low_refit(&c->p, zg, g1, 1); zg->synth = true;
                                    mk[0] = m0; mk[1] = m1; mk[2] = zg; (void)low_refit(&c->p, mf, mk, 3); mf->synth = true;
                                    g2[0] = mf; (void)low_refit(&c->p, mg, g2, 1); mg->synth = true;
                                    sk[3] = mg;
                                }
                            }
                        }
                        if (sl && sk[3]) {                      // (size_of <구조체>)
                            low_cst_t **zk = (low_cst_t **)c->p.work.alloc_fn(c->p.work.ctx, sizeof(low_cst_t *) * 2, alignof(low_cst_t *)).value.ptr;
                            low_cst_t **zg = (low_cst_t **)c->p.work.alloc_fn(c->p.work.ctx, sizeof(low_cst_t *), alignof(low_cst_t *)).value.ptr;
                            low_cst_t *zf = low_node(&c->p, LOW_CST_FORM, explicit_src->tok), *zgr = low_node(&c->p, LOW_CST_GROUP, explicit_src->tok);
                            low_cst_t *z0 = us_atom_like(c, explicit_src, (proven_u8str_view_t){ .ptr = (const proven_u8 *)"size_of", .size = 7 });
                            low_cst_t *z1 = us_atom_like(c, sl->kids[0], sl->kids[0]->tok.lex);
                            if (zk && zg && zf && zgr && z0 && z1) {
                                zk[0] = z0; zk[1] = z1; (void)low_refit(&c->p, zf, zk, 2); zf->synth = true;
                                zg[0] = zf; (void)low_refit(&c->p, zgr, zg, 1); zgr->synth = true;
                                sk[3] = zgr;
                            } else sk[3] = NULL;
                        }
                        if (sk[0] && sk[2] && sk[3]) {
                            if (!sl && sk[3]->kind == LOW_CST_ATOM) sk[3]->tok.kind = LOW_TOK_NUMBER;
                            sk[0]->tok.kw = LOW_KW_SEND;          // 파서가 `send` 에 붙이는 예약어 표시 그대로
                            (void)low_refit(&c->p, sf, sk, 4); sf->synth = true;
                            gk[0] = sf; (void)low_refit(&c->p, sg, gk, 1); sg->synth = true;
                            uk[0] = us_atom_like(c, explicit_src, (proven_u8str_view_t){ .ptr = (const proven_u8 *)"using", .size = 5 });
                            if (uk[0]) {
                                uk[0]->synth = true;
                                uk[1] = sg; uk[2] = init;
                                proven_size_t nuk = 3;
                                // ★★ RFC-0135 S2 (D11) — 낱낱이 돌려주는 할당기(`freeing_allocator`)면 **블록을 나갈 때 돌려준다**:
                                //   [send <출처> release (some_value <이름>)] 를 넷째 자식으로 붙인다. 하강이 그 바인딩의 블록 끝 ·
                                //   `return` · `break`/`continue` 에 넣는다(받지 못했으면 건너뛴다). 수명 검사는 그 바이트를 블록에 묶는다.
                                proven_u8str_view_t aty = us_bind_type(c, explicit_src->tok.lex);
                                if (!keep && aty.size && us_type_satisfies(c, aty, (proven_u8str_view_t){ .ptr = (const proven_u8 *)"freeing_allocator", .size = 17 })) {
                                    low_cst_t **rk = (low_cst_t **)c->p.work.alloc_fn(c->p.work.ctx, sizeof(low_cst_t *) * 4, alignof(low_cst_t *)).value.ptr;
                                    low_cst_t **vk = (low_cst_t **)c->p.work.alloc_fn(c->p.work.ctx, sizeof(low_cst_t *) * 2, alignof(low_cst_t *)).value.ptr;
                                    low_cst_t **gk2 = (low_cst_t **)c->p.work.alloc_fn(c->p.work.ctx, sizeof(low_cst_t *), alignof(low_cst_t *)).value.ptr;
                                    low_cst_t *rf = low_node(&c->p, LOW_CST_FORM, explicit_src->tok), *vf = low_node(&c->p, LOW_CST_FORM, explicit_src->tok);
                                    low_cst_t *vg = low_node(&c->p, LOW_CST_GROUP, explicit_src->tok);
                                    low_cst_t *s0 = us_atom_like(c, explicit_src, (proven_u8str_view_t){ .ptr = (const proven_u8 *)"send", .size = 4 });
                                    low_cst_t *s1 = us_atom_like(c, explicit_src, explicit_src->tok.lex);
                                    low_cst_t *s2 = us_atom_like(c, explicit_src, (proven_u8str_view_t){ .ptr = (const proven_u8 *)"release", .size = 7 });
                                    low_cst_t *v0 = us_atom_like(c, explicit_src, (proven_u8str_view_t){ .ptr = (const proven_u8 *)"some_value", .size = 10 });
                                    low_cst_t *v1 = us_atom_like(c, nd->kids[1], nd->kids[1]->tok.lex);
                                    if (rk && vk && gk2 && rf && vf && vg && s0 && s1 && s2 && v0 && v1) {
                                        s0->tok.kw = LOW_KW_SEND;
                                        vk[0] = v0; vk[1] = v1; (void)low_refit(&c->p, vf, vk, 2); vf->synth = true;
                                        gk2[0] = vf; (void)low_refit(&c->p, vg, gk2, 1); vg->synth = true;
                                        rk[0] = s0; rk[1] = s1; rk[2] = s2; rk[3] = vg; (void)low_refit(&c->p, rf, rk, 4); rf->synth = true;
                                        low_cst_t **uk4 = (low_cst_t **)c->p.work.alloc_fn(c->p.work.ctx, sizeof(low_cst_t *) * 4, alignof(low_cst_t *)).value.ptr;
                                        if (uk4) { uk4[0] = uk[0]; uk4[1] = uk[1]; uk4[2] = uk[2]; uk4[3] = rf; uk = uk4; nuk = 4; }
                                    }
                                }
                                (void)low_refit(&c->p, uf, uk, nuk); uf->synth = true;
                                us_walk(c, init);
                                nd->kids[be + 1] = uf;
                                us_push_bind(c, nd->kids[1]->tok.lex, us_type_word(nd, 2), false);
                                return;
                            }
                        }
                    }
                }
            }
        }
        if (be + 1 < nd->nkids && us_atom(nd->kids[be + 1]) && us_eq(nd->kids[be + 1]->tok.lex, "option"))
            explicit_src_told = true;                     // `be option lit …` 을 선언 패스가 이미 말했다(E-LIT-USING)
        if (explicit_src && !u && !explicit_src_told) {
            low_pdiag(&c->p, "E-ALLOC-USING-UNUSED",
                      "this binding says which allocator to use, but the call it initialises does not draw from one "
                      "(no `using` clause on that op). An object that already carries its allocator — like "
                      "`vecgen.append` on a vector — does not take the caller's choice (RFC-0112 D8(5))",
                      explicit_src->tok.line, explicit_src->tok.col);
        }
        // 식 안쪽을 먼저 걷는다(안쪽 호출도 기본값을 받는다) — 머리 호출은 아래서 따로
        for (proven_size_t i = be + 1; i < nd->nkids; i++) {
            if (nd->kids[i] == init && u && call) {
                for (proven_size_t z = 1; z < call->nkids; z++) us_walk(c, call->kids[z]);
                (void)us_fill_call(c, call, u, explicit_src);
            } else us_walk(c, nd->kids[i]);
        }
        us_push_bind(c, nd->kids[1]->tok.lex, us_type_word(nd, 2), false);
        return;
    }
    // ★ 인자 0 인 using 호출이 식 자리에 원자로 서 있으면(`return two_takes .`) 폼 한 겹으로 싸서 채운다.
    if (nd->kind == LOW_CST_FORM || nd->kind == LOW_CST_GROUP) {
        for (proven_size_t i = (nd->kind == LOW_CST_FORM ? 1 : 0); i < nd->nkids; i++) {
            low_cst_t *a = nd->kids[i];
            if (!us_atom(a) || a->tok.kw != LOW_KW_NONE || us_bind_type(c, a->tok.lex).size) continue;
            const us_callee_t *u0 = us_find_callee(c, a->tok.lex);
            if (!u0) continue;
            low_op_header_t hh = low_op_header(u0->form);
            if (hh.np_call != 0) continue;
            low_cst_t *fm = low_node(&c->p, LOW_CST_FORM, a->tok);
            low_cst_t **one = (low_cst_t **)c->p.work.alloc_fn(c->p.work.ctx, sizeof(low_cst_t *), alignof(low_cst_t *)).value.ptr;
            if (!fm || !one) continue;
            one[0] = a;
            (void)low_refit(&c->p, fm, one, 1);
            // 괄호 한 겹 = GROUP(FORM) — 저자가 `(take)` 로 적은 것과 같은 나무
            low_cst_t *gp = low_node(&c->p, LOW_CST_GROUP, a->tok);
            low_cst_t **one2 = (low_cst_t **)c->p.work.alloc_fn(c->p.work.ctx, sizeof(low_cst_t *), alignof(low_cst_t *)).value.ptr;
            if (!gp || !one2) continue;
            one2[0] = fm;
            (void)low_refit(&c->p, gp, one2, 1);
            nd->kids[i] = gp;          // ★ 채우는 것은 아래 되풀이가 이 폼을 만날 때 한 번만 한다
        }
    }
    // 일반 호출 폼
    if (nd->kind == LOW_CST_FORM && nd->nkids && us_atom(nd->kids[0]) && nd->kids[0]->tok.kw == LOW_KW_NONE) {
        const us_callee_t *u = us_find_callee(c, nd->kids[0]->tok.lex);
        for (proven_size_t i = 1; i < nd->nkids; i++) us_walk(c, nd->kids[i]);
        if (u) (void)us_fill_call(c, nd, u, NULL);
        return;
    }
    for (proven_size_t i = 0; i < nd->nkids; i++) us_walk(c, nd->kids[i]);
}

static void us_walk_op(us_ctx_t *c, low_cst_t *f) {
    if (!us_is_op(f)) return;
    c->op = f; c->nbind = 0; c->nrgn = 0;
    low_op_header_t h = low_op_header(f);
    for (proven_size_t q = 0; q < h.np; q++) {
        proven_u8str_view_t ty = { 0 };
        for (proven_size_t z = h.p[q].core; z < h.p[q].te && z < f->nkids; z++)
            if (us_atom(f->kids[z])) { ty = f->kids[z]->tok.lex; break; }
        us_push_bind(c, h.p[q].name, ty, false);
    }
    for (proven_size_t i = 2; i + 2 < f->nkids; i++)        // 이 op 자신의 using 절(아직 안 바꿔 적었다)
        if (us_atom(f->kids[i]) && us_eq(f->kids[i]->tok.lex, "using") && us_atom(f->kids[i + 1]) && us_atom(f->kids[i + 2]))
            us_push_bind(c, f->kids[i + 1]->tok.lex, f->kids[i + 2]->tok.lex, true);
    if (h.body) us_walk(c, (low_cst_t *)h.body);
}

void low_using(low_parse_result_t *pr, proven_allocator_t node_alloc, proven_allocator_t work) {
    us_ctx_t *c = (us_ctx_t *)work.alloc_fn(work.ctx, sizeof(us_ctx_t), alignof(us_ctx_t)).value.ptr;
    if (!c) return;
    memset(c, 0, sizeof *c);
    c->p = (low_parser_t){ .node_alloc = node_alloc, .work = work, .out = pr };
    c->cal = (us_callee_t *)work.alloc_fn(work.ctx, sizeof(us_callee_t) * US_MAXOPS, alignof(us_callee_t)).value.ptr;
    if (!c->cal) return;
    // ⓪ 고정 길이 입력(`array N T`)을 슬라이스 + 진입 계약으로 바꿔 적는다 — 아래 모든 소비자가 바뀐 머리를 본다
    for (proven_size_t i = 0; i < pr->nforms; i++) {
        low_cst_t *f = pr->forms[i];
        us_arrays_one(c, f);
        if (f && f->kind == LOW_CST_FORM && f->nkids >= 3 && us_atom(f->kids[0]) && f->kids[0]->tok.kw == LOW_KW_ACTOR &&
            f->kids[f->nkids - 1]->kind == LOW_CST_BLOCK) {
            low_cst_t *blk = f->kids[f->nkids - 1];
            for (proven_size_t j = 0; j < blk->nkids; j++) us_arrays_one(c, blk->kids[j]);
        }
    }
    // ① 모은다
    proven_u8str_view_t mod = { 0 };
    bool any_using = false;
    for (proven_size_t i = 0; i < pr->nforms; i++) {
        const low_cst_t *f = pr->forms[i];
        if (f && f->kind == LOW_CST_FORM && f->nkids >= 2 && us_atom(f->kids[0]) && f->kids[0]->tok.kw == LOW_KW_MODULE &&
            us_atom(f->kids[1])) { mod = f->kids[1]->tok.lex; continue; }
        us_collect_one(c, f, mod);
        if (f && f->kind == LOW_CST_FORM && f->nkids >= 3 && us_atom(f->kids[0]) && f->kids[0]->tok.kw == LOW_KW_ACTOR) {
            const low_cst_t *blk = f->kids[f->nkids - 1];
            if (blk->kind == LOW_CST_BLOCK)
                for (proven_size_t j = 0; j < blk->nkids; j++) us_collect_one(c, blk->kids[j], mod);
        }
    }
    any_using = c->ncal > 0;
    // ② 부르는 쪽을 걷는다(받는 쪽 머리를 바꾸기 **전에** — 자기 using 이름을 봐야 한다)
    mod = (proven_u8str_view_t){ 0 };
    for (proven_size_t i = 0; i < pr->nforms; i++) {
        low_cst_t *f = pr->forms[i];
        if (f && f->kind == LOW_CST_FORM && f->nkids >= 2 && us_atom(f->kids[0]) && f->kids[0]->tok.kw == LOW_KW_MODULE &&
            us_atom(f->kids[1])) { mod = f->kids[1]->tok.lex; continue; }
        c->curmod = mod;
        if (any_using || true) us_walk_op(c, f);
        if (f && f->kind == LOW_CST_FORM && f->nkids >= 3 && us_atom(f->kids[0]) && f->kids[0]->tok.kw == LOW_KW_ACTOR) {
            low_cst_t *blk = f->kids[f->nkids - 1];
            if (blk->kind == LOW_CST_BLOCK)
                for (proven_size_t j = 0; j < blk->nkids; j++) us_walk_op(c, blk->kids[j]);
        }
    }
    // ③ 받는 쪽 머리를 바꿔 적는다
    for (proven_size_t i = 0; i < c->ncal; i++) us_rewrite_header(c, &c->cal[i]);
}

// ═══════════════════════════════════════════════════════════════════════════════════════════
// ★★★ **선언의 타입은 값 앞에 선다** — `var <이름> be <타입> <값> .` (RFC-0132 T1 · TV3 · 2026-09-28).
//   옛 모양 `var <이름> <타입> be <값> .` 의 타입을 소비자 모두가 이름 바로 뒤에서 읽는다. 그래서 새 모양은 여기서
//   **타입만 `be` 앞으로 옮겨** 옛 안쪽 모양으로 바꾼다 — 뒤의 모든 소비자(검사·타입·하강·서식 대조)는 바뀌지 않는다.
//   타입의 끝은 **타입 문법의 인자 수**로 안다: 내장 낱말은 정해진 수, 괄호 묶음은 하나, 사용자 타입은
//   선언이 받는 `input comptime <x> type` 의 수(제네릭·브랜드). 그래서 이 패스는 **링크 뒤·묶기 전**, 모든
//   파일의 타입 선언이 보일 때 돈다(`--flat` 과 나무 모드 둘 다).
//   strict 이면 옛 모양은 `E-LET-OLDFORM`, 타입 없는 묶기는 `E-LET-NOTYPE`(X-0074 — `let x be 300 .` 이 폭 검사를
//   빠져나갔다).
// ═══════════════════════════════════════════════════════════════════════════════════════════
typedef struct { proven_u8str_view_t name, mod; proven_size_t arity; } dt_tname_t;
typedef struct {
    low_parser_t p;
    dt_tname_t *tn; proven_size_t ntn, ctn;
    proven_u8str_view_t tp[32]; proven_size_t ntp;     // 지금 걷는 op/구조체의 comptime 타입 매개변수
    bool strict;
    bool migrate;                                       // `--fmt`: 옛 모양을 새 모양(표면)으로 옮긴다
    proven_u8str_view_t curmod;                         // 지금 걷는 폼의 모듈
    struct { proven_u8str_view_t mod, alias, target; } al[512]; proven_size_t nal;   // `use <t> … as <a>`
} dt_ctx_t;

static bool dt_in(proven_u8str_view_t v, const char *const *set) {
    for (proven_size_t i = 0; set[i]; i++) if (us_eq(v, set[i])) return true;
    return false;
}
static const char *const DT_ZERO[] = { "bool","u8","i8","u16","i16","u32","i32","u64","i64","usize","isize","f32","f64",
                                       "void","self","str","string","char","byte","bytes_view", NULL };
static const char *const DT_PRE1[] = { "slice","mut","owned","ref","mut_ref","option","segments","stack","set","unsafe_ptr","nonzero",
                                       "atomic","lock","rwlock","shared_read","view", NULL };
// ★ 이름은 **모듈과 함께** 맞춘다(check-lib-pairs 가 잡았다: `trust` 의 `files.handle` 이 `pool` 의 브랜드 타입 `handle`
//   (인자 하나)로 읽혀 다음 낱말을 삼켰다). `m.t` 는 모듈 m 의 t 만, 맨 `t` 는 **자기 모듈 것만**.
// ★★ 되짚기(«없으면 아무 모듈의 것») 를 좁혔다(2026-09-28, 코드 검토). 전엔 **링크 차례상 처음** 것을 집어, 같은
//   이름이 두 모듈에 있으면 차례에 따라 남의 제네릭 `box`(인자 하나)가 값의 다음 낱말을 타입 인자로 삼켰다.
//   남의 모듈 타입을 맨이름으로 쓰는 것은 어차피 오류(E-VISIBILITY — `m.t` 로 쓴다)라, 되짚기는 **어느 오류를
//   내는가** 만 정한다. 그래서 그 이름을 선언한 모듈이 **하나뿐일 때만** 집는다(그러면 뒤에서 E-VISIBILITY 가
//   제 원인을 말한다 — 매뉴얼 ch21 mistake_bare). 둘 이상이면 집지 않는다 — 링크 차례에 기대지 않는다.
static bool dt_tname(dt_ctx_t *c, proven_u8str_view_t v, proven_size_t *arity) {
    proven_u8str_view_t b = us_bare(v), q = us_qual(v);
    for (proven_size_t i = 0; !q.size && i < c->ntp; i++) if (proven_u8str_view_eq(c->tp[i], b)) { *arity = 0; return true; }
    if (q.size) {
        proven_u8str_view_t qb = us_bare(q);                         // `a.b.t` 면 마지막 모듈 마디
        for (proven_size_t i = 0; i < c->nal; i++)                   // 가져오기 별칭(`use geom … as g`)을 푼다
            if (proven_u8str_view_eq(c->al[i].mod, c->curmod) && proven_u8str_view_eq(c->al[i].alias, qb)) { qb = c->al[i].target; break; }
        for (proven_size_t i = 0; i < c->ntn; i++)
            if (proven_u8str_view_eq(c->tn[i].name, b) && proven_u8str_view_eq(c->tn[i].mod, qb)) { *arity = c->tn[i].arity; return true; }
        return false;
    }
    for (proven_size_t i = 0; i < c->ntn; i++)
        if (proven_u8str_view_eq(c->tn[i].name, b) && proven_u8str_view_eq(c->tn[i].mod, c->curmod)) { *arity = c->tn[i].arity; return true; }
    proven_size_t hit = (proven_size_t)-1, nhit = 0;
    for (proven_size_t i = 0; i < c->ntn; i++)
        if (proven_u8str_view_eq(c->tn[i].name, b)) {
            bool same = hit != (proven_size_t)-1 && proven_u8str_view_eq(c->tn[hit].mod, c->tn[i].mod);
            if (!same) nhit++;
            hit = i;
        }
    if (nhit == 1) { *arity = c->tn[hit].arity; return true; }
    return false;
}
// kids[i..end) 에서 타입 하나가 끝나는 자리. 타입이 아니면 (proven_size_t)-1.
// head: 머리 자리(`be` 바로 뒤)면 아는 타입 이름만 타입이다. 타입 인자 자리(`result u64 <여기>`)에서는 모르는 이름도
//   타입으로 받는다 — 거기에는 타입밖에 올 수 없다(내장 오류 타입 `mailbox_full` 같은 것).
static proven_size_t dt_type_end_h(dt_ctx_t *c, low_cst_t *const *k, proven_size_t i, proven_size_t end, bool head);
static proven_size_t dt_type_end(dt_ctx_t *c, low_cst_t *const *k, proven_size_t i, proven_size_t end) {
    return dt_type_end_h(c, k, i, end, false);
}
static proven_size_t dt_type_end_h(dt_ctx_t *c, low_cst_t *const *k, proven_size_t i, proven_size_t end, bool head) {
    const proven_size_t NO = (proven_size_t)-1;
    if (i >= end) return NO;
    const low_cst_t *t = k[i];
    if (t->kind == LOW_CST_GROUP) return i + 1;                      // 괄호로 싼 타입(TV4)
    if (!us_atom(t) || t->tok.kind != LOW_TOK_IDENT) return NO;
    proven_u8str_view_t w = t->tok.lex; proven_size_t a = 0;
    if (dt_in(w, DT_ZERO)) return i + 1;
    if (dt_in(w, DT_PRE1)) return dt_type_end(c, k, i + 1, end);
    if (us_eq(w, "result")) { proven_size_t j = dt_type_end(c, k, i + 1, end); return j == NO ? NO : dt_type_end(c, k, j, end); }
    if (us_eq(w, "array")) {                                         // array <타입> <길이>(C12)
        proven_size_t j = dt_type_end(c, k, i + 1, end);
        return (j != NO && j < end && us_is_int_lit(k[j])) ? j + 1 : NO;
    }
    if (us_eq(w, "vec")) {                                           // vec <타입> <레인 수 | scalable 같은 낱말>
        proven_size_t j = dt_type_end(c, k, i + 1, end);
        return (j != NO && j < end && us_atom(k[j])) ? j + 1 : NO;
    }
    if (us_eq(w, "bitset") || us_eq(w, "mask"))                     // 크기는 정수 리터럴일 때만(없이 쓰는 곳이 있다)
        return (i + 1 < end && us_is_int_lit(k[i + 1])) ? i + 2 : i + 1;
    if (us_eq(w, "cap"))
        return (i + 1 < end && us_atom(k[i + 1])) ? i + 2 : NO;
    if (dt_tname(c, w, &a)) {
        proven_size_t j = i + 1;
        for (proven_size_t q = 0; q < a; q++) { j = dt_type_end(c, k, j, end); if (j == NO) return NO; }
        return j;
    }
    return head ? NO : i + 1;
}
static void dt_collect(dt_ctx_t *c, const low_cst_t *f) {
    if (!f || f->kind != LOW_CST_FORM || f->nkids < 2 || !us_atom(f->kids[0]) || !us_atom(f->kids[1])) return;
    low_kw_t kw = f->kids[0]->tok.kw;
    if (kw != LOW_KW_STRUCT && kw != LOW_KW_ENUM && kw != LOW_KW_TYPE && kw != LOW_KW_NEWTYPE && kw != LOW_KW_ACTOR) return;
    proven_size_t ar = 0;
    const low_cst_t *blk = f->kids[f->nkids - 1];
    if (kw == LOW_KW_STRUCT && blk->kind == LOW_CST_BLOCK)
        for (proven_size_t j = 0; j < blk->nkids; j++) {
            const low_cst_t *m = blk->kids[j];
            for (proven_size_t q = 0; m->kind == LOW_CST_FORM && q + 2 < m->nkids; q++)
                if (us_atom(m->kids[q]) && us_eq(m->kids[q]->tok.lex, "comptime") && us_atom(m->kids[q + 2]) &&
                    us_eq(m->kids[q + 2]->tok.lex, "type")) ar++;
        }
    if (c->ntn == c->ctn) {
        proven_size_t nc = c->ctn ? c->ctn * 2 : 256;
        dt_tname_t *nt = (dt_tname_t *)c->p.work.alloc_fn(c->p.work.ctx, sizeof(dt_tname_t) * nc, alignof(dt_tname_t)).value.ptr;
        if (!nt) return;
        if (c->ntn) memcpy(nt, c->tn, sizeof(dt_tname_t) * c->ntn);
        c->tn = nt; c->ctn = nc;
    }
    c->tn[c->ntn].name = us_bare(f->kids[1]->tok.lex); c->tn[c->ntn].mod = c->curmod; c->tn[c->ntn].arity = ar; c->ntn++;
}
static void dt_diag(dt_ctx_t *c, const low_cst_t *at, const char *code, const char *msg) {
    low_diag_t d = { .sev = LOW_SEV_ERROR, .code = code, .msg = msg, .line = at->tok.line, .col = at->tok.col, .file = at->file };
    (void)proven_array_push(&c->p.out->diags, &d);
    c->p.out->ok = false;
}
static void dt_decl_core(dt_ctx_t *c, low_cst_t *f);
// ★ RFC-0135 S1 — 끝 자식이 바인딩 `else` 면 떼어 두고 타입·값을 가른 뒤 다시 붙인다(값의 꼬리로 읽히지 않게).
static void dt_decl(dt_ctx_t *c, low_cst_t *f) {
    low_cst_t *els = NULL;
    if (f->nkids >= 3 && f->kids[f->nkids - 1]->kind == LOW_CST_FORM && f->kids[f->nkids - 1]->nkids &&
        us_atom(f->kids[f->nkids - 1]->kids[0]) && f->kids[f->nkids - 1]->kids[0]->tok.kw == LOW_KW_ELSE) {
        els = f->kids[f->nkids - 1];
        f->nkids--;
    }
    dt_decl_core(c, f);
    if (!els) return;
    low_cst_t **nk = (low_cst_t **)c->p.work.alloc_fn(c->p.work.ctx, sizeof(low_cst_t *) * (f->nkids + 1), alignof(low_cst_t *)).value.ptr;
    if (!nk) return;
    for (proven_size_t i = 0; i < f->nkids; i++) nk[i] = f->kids[i];
    nk[f->nkids] = els;
    (void)low_refit(&c->p, f, nk, f->nkids + 1);
}
static void dt_decl_core(dt_ctx_t *c, low_cst_t *f) {
    proven_size_t b = 0;
    for (proven_size_t i = 2; i < f->nkids; i++) if (us_atom(f->kids[i]) && f->kids[i]->tok.kw == LOW_KW_BE) { b = i; break; }
    if (!b) return;
    // 이름과 `be` 사이: 비었거나 `using <이름>` 뿐이면 새 모양(또는 타입 없음), 타입이 있으면 옛 모양
    bool typeless_mid = (b == 2) ||
        (b == 4 && us_atom(f->kids[2]) && us_eq(f->kids[2]->tok.lex, "using")) ||
        (b == 5 && us_atom(f->kids[2]) && us_eq(f->kids[2]->tok.lex, "using") && us_atom(f->kids[4]) && us_eq(f->kids[4]->tok.lex, "keep"));   // RFC-0135 D12
    if (!typeless_mid && c->migrate) {
        // ★ `--fmt` 옮김(2026-09-28, 코드 검토): `var i u64 be 0 .` → `var i be u64 0 .`. 타입 뒤 점과 `using <x>` 는
        //   떼어 `be` 앞에 둔다. 타입의 끝이 문법으로 딱 떨어지지 않으면 괄호로 싼다(TV4 — migrate-decl-order.py 와 같다).
        low_cst_t *ty[64]; proven_size_t nty = 0; low_cst_t *us = NULL, *ux = NULL, *kp = NULL;
        for (proven_size_t i = 2; i < b; i++) {
            low_cst_t *k = f->kids[i];
            if (us_atom(k) && k->tok.kind == LOW_TOK_DOT) continue;
            if (us_atom(k) && us_eq(k->tok.lex, "using") && i + 1 < b) {
                us = k; ux = f->kids[++i];
                if (i + 1 < b && us_atom(f->kids[i + 1]) && us_eq(f->kids[i + 1]->tok.lex, "keep")) kp = f->kids[++i];   // RFC-0135 D12
                continue;
            }
            if (nty < 64) ty[nty++] = k;
        }
        if (!nty || nty == 64) return;
        low_cst_t *tnode = ty[0];
        if (nty > 1 || dt_type_end_h(c, ty, 0, nty, true) != nty) {
            if (!(nty == 1 && ty[0]->kind == LOW_CST_GROUP)) {
                low_cst_t *g = (low_cst_t *)c->p.node_alloc.alloc_fn(c->p.node_alloc.ctx, sizeof(low_cst_t), alignof(low_cst_t)).value.ptr;
                if (!g) return;
                *g = *ty[0];
                g->kind = LOW_CST_FORM; g->closer = LOW_TOK_EOF; g->synth = false;
                (void)low_refit(&c->p, g, ty, nty);
                if (nty == 1 || dt_type_end_h(c, ty, 0, nty, true) == nty) tnode = NULL;   // 딱 떨어지면 괄호 없이
                else tnode = g;
            }
        } else tnode = NULL;
        proven_size_t n = f->nkids;
        low_cst_t **nk = (low_cst_t **)c->p.work.alloc_fn(c->p.work.ctx, sizeof(low_cst_t *) * (n + 2), alignof(low_cst_t *)).value.ptr;
        if (!nk) return;
        proven_size_t m = 0;
        nk[m++] = f->kids[0]; nk[m++] = f->kids[1];
        if (us) { nk[m++] = us; nk[m++] = ux; if (kp) nk[m++] = kp; }
        nk[m++] = f->kids[b];
        if (tnode) nk[m++] = tnode; else for (proven_size_t i = 0; i < nty; i++) nk[m++] = ty[i];
        for (proven_size_t i = b + 1; i < n; i++) nk[m++] = f->kids[i];
        (void)low_refit(&c->p, f, nk, m);
        return;
    }
    if (c->migrate) {
        // 새 모양이면 그대로 찍는다. 타입이 없으면 서식기는 타입을 알 수 없다 — 알리고 실패로 끝낸다.
        const low_cst_t *h1 = b + 1 < f->nkids ? f->kids[b + 1] : NULL;
        if (h1 && h1->kind == LOW_CST_GROUP && h1->nkids == 1 && h1->kids[0]->kind == LOW_CST_FORM && h1->kids[0]->nkids)
            h1 = h1->kids[0]->kids[0];
        bool lit_head = h1 && us_atom(h1) && h1->tok.kw == LOW_KW_LIT;
        if (!lit_head && dt_type_end_h(c, f->kids, b + 1, f->nkids, true) == (proven_size_t)-1)
            dt_diag(c, f->kids[1], "E-LET-NOTYPE",
                      "a binding must show its type in front of the value — `let x be u64 300 .`. `--fmt` cannot "
                      "guess it: write the type, then format again");
        return;
    }
    if (!typeless_mid) {
        if (c->strict)
            dt_diag(c, f->kids[1], "E-LET-OLDFORM",
                      "a binding writes its type AFTER `be`, in front of the value: `var i be u64 0 .` "
                      "(RFC-0132). The type between the name and `be` is the old form — move it: "
                      "`var <name> be <type> <value> .`");
        return;
    }
    // ★ RFC-0132 L1 — `be lit <타입> …` 는 `lit` 가 타입을 보인다(§13.1): 따로 달지 않는다. 안쪽 모양에는 그 타입을
    //   선언 타입으로 복사해 넣는다(`let p be lit pt do … end .` → [let, p, pt, be, lit, pt do … end]).
    //   `lit pt do … end` 는 파서가 `pt do … end` 를 머리 붙은 블록 폼 하나로 이미 묶어 둔다.
    // `--fmt` 는 값을 괄호로 싸서 찍는다 — `be (lit pt do … end)` 도 같다.
    // ★★ RFC-0132 §13.7 (2026-09-30, 소유자 «넣습니다») — **고른 할당자에서 받는 나열**: `var bo using al be option
    //   lit array T N … .`. 값은 할당자가 준 바이트에 지은 나열이고, 할당이 실패할 수 있으므로 타입은 `option` 이다.
    //   안쪽 모양의 선언 타입은 `option mut slice T`. 값(나열)은 그대로 두고, using 패스가 할당과 채우기로 바꾼다.
    if (b + 3 == f->nkids && us_atom(f->kids[b + 1]) && us_eq(f->kids[b + 1]->tok.lex, "option") &&
        f->kids[b + 2]->kind == LOW_CST_GROUP && f->kids[b + 2]->nkids == 1 && f->kids[b + 2]->kids[0]->kind == LOW_CST_FORM) {
        const low_cst_t *lf = f->kids[b + 2]->kids[0];
        if (lf->nkids >= 3 && us_atom(lf->kids[0]) && lf->kids[0]->tok.kw == LOW_KW_LIT && us_atom(lf->kids[1]) && us_atom(lf->kids[2]) &&
            us_eq(lf->kids[1]->tok.lex, "vec") && lf->kids[lf->nkids - 1]->kind != LOW_CST_BLOCK) {
            dt_diag(c, f->kids[1], "E-LIT-USING", "a SIMD value lives in lanes, not in bytes an allocator hands out — "
                    "`option lit vec …` has no meaning (RFC-0132 §13.7)");
            return;
        }
        if (lf->nkids >= 3 && us_atom(lf->kids[0]) && lf->kids[0]->tok.kw == LOW_KW_LIT && us_atom(lf->kids[1]) && us_atom(lf->kids[2]) &&
            (us_eq(lf->kids[1]->tok.lex, "array") || us_eq(lf->kids[1]->tok.lex, "slice"))) {
            // ★ RFC-0135 D6 — 이 철자(`be option lit …`)는 §4.1·§4.2 의 모양과 같은 일을 하는 둘째 철자라 없앴다.
            dt_diag(c, f->kids[1], "E-LIT-USING", "`be option lit …` is gone (RFC-0135 D6) — take the list from the allocator with "
                    "`var buf using al be mut slice T lit array T N … . else return … .`, or keep the option with its whole type: "
                    "`let bo using al be option mut slice T lit array T N … .`");
            return;
        }
    }
    low_cst_t *const *vk = NULL; proven_size_t vn = 0;
    if (b + 2 < f->nkids && us_atom(f->kids[b + 1]) && f->kids[b + 1]->tok.kw == LOW_KW_LIT) {
        vk = f->kids + b + 2; vn = f->nkids - (b + 2);
    } else if (b + 1 < f->nkids && f->kids[b + 1]->kind == LOW_CST_GROUP && f->kids[b + 1]->nkids == 1) {
        const low_cst_t *in = f->kids[b + 1]->kids[0];
        if (in->kind == LOW_CST_FORM && in->nkids >= 2 && us_atom(in->kids[0]) && in->kids[0]->tok.kw == LOW_KW_LIT) {
            vk = in->kids + 1; vn = in->nkids - 1;
        }
    }
    if (vk) {
        low_cst_t *const *lk = NULL; proven_size_t ln = 0;
        low_cst_t *lbuf[16];
        // 블록은 타입의 **마지막 낱말**에 붙는다: `lit token b do … end` 는 [token, (b do … end)] 로 온다.
        proven_size_t j = 0;
        while (j < vn && us_atom(vk[j]) && j < 15) j++;
        const low_cst_t *v = j < vn ? vk[j] : NULL;
        if (v && v->kind == LOW_CST_FORM && v->nkids >= 2 && v->kids[v->nkids - 1]->kind == LOW_CST_BLOCK &&
            j + (v->nkids - 1) <= 16) {
            for (proven_size_t q = 0; q < j; q++) lbuf[ln++] = vk[q];
            for (proven_size_t q = 0; q + 1 < v->nkids; q++) lbuf[ln++] = v->kids[q];
            lk = lbuf;
        } else {
            proven_size_t le = dt_type_end_h(c, vk, 0, vn, true);
            if (le != (proven_size_t)-1) { lk = vk; ln = le; }
        }
        if (c->migrate) return;                        // 새 모양 그대로 찍는다
        if (ln) {
            proven_size_t n = f->nkids;
            low_cst_t **nk = (low_cst_t **)c->p.work.alloc_fn(c->p.work.ctx, sizeof(low_cst_t *) * (n + ln + 1), alignof(low_cst_t *)).value.ptr;
            if (!nk) return;
            proven_size_t m = 0;
            nk[m++] = f->kids[0]; nk[m++] = f->kids[1];
            // ★ RFC-0132 T2b-1 — `let` 에 묶은 상수 배열 리터럴은 읽기 전용 자리를 **보는 슬라이스**다(§13.2 ⓑ):
            //   선언 타입 `array T N` 을 `slice T` 로 적는다(입력 자리의 `array` 를 using 패스가 `slice` 로 적는 것과 같다 —
            //   길이는 리터럴 자신이 정한다). `var` 배열은 아래 T2b-2 의 틀 안 자리(ⓐ)다.
            bool as_slice = f->kids[0]->tok.kw == LOW_KW_LET && ln == 3 && us_atom(lk[0]) && us_eq(lk[0]->tok.lex, "array");
            // ★ RFC-0132 T2b-2 — `var` 에 묶은 나열(배열이든 슬라이스든)은 **쓸 수 있는 틀 안 자리**(§13.2 ⓐ)다:
            //   선언 타입을 `mut slice T` 로 적는다(`set (index buf i) v` 가 선다). 길이는 나열이 정한다.
            bool as_mslice = f->kids[0]->tok.kw == LOW_KW_VAR && us_atom(lk[0]) &&
                             ((ln == 3 && us_eq(lk[0]->tok.lex, "array")) || (ln == 2 && us_eq(lk[0]->tok.lex, "slice")));
            if (as_slice) ln = 2;
            if (as_mslice) {
                low_cst_t *mt = (low_cst_t *)c->p.node_alloc.alloc_fn(c->p.node_alloc.ctx, sizeof(low_cst_t), alignof(low_cst_t)).value.ptr;
                if (!mt) return;
                *mt = *lk[0]; mt->tok.lex = (proven_u8str_view_t){ .ptr = (const proven_u8 *)"mut", .size = 3 };
                nk[m++] = mt;
                ln = 2;
            }
            for (proven_size_t i = 0; i < ln; i++) {          // 타입 낱말은 **복사본**으로 — 나무에서 한 노드가 두 자리에 서지 않게
                low_cst_t *cp = (low_cst_t *)c->p.node_alloc.alloc_fn(c->p.node_alloc.ctx, sizeof(low_cst_t), alignof(low_cst_t)).value.ptr;
                if (!cp) return;
                *cp = *lk[i];
                if ((as_slice || as_mslice) && i == 0) cp->tok.lex = (proven_u8str_view_t){ .ptr = (const proven_u8 *)"slice", .size = 5 };
                nk[m++] = cp;
            }
            for (proven_size_t i = 2; i < n; i++) nk[m++] = f->kids[i];
            (void)low_refit(&c->p, f, nk, m);
            return;
        }
    }
    proven_size_t te = dt_type_end_h(c, f->kids, b + 1, f->nkids, true);
    // ★ 머리의 괄호 묶음 **뒤에 아무것도 없으면** 그 묶음은 타입이 아니라 값일 수 있다 — `let x be (add a 1) .`.
    //   전엔 묶음을 늘 타입으로 보아 «값이 없다»(E-LET-NOVALUE, 앞 점 소수 힌트까지)로 잘못 말했다(2026-09-28, 코드 검토).
    //   묶음 속이 타입 문법으로 딱 떨어질 때만 타입으로 두고(그러면 정말 값이 없는 것), 아니면 타입이 없는 것이다.
    if (te == f->nkids && te == b + 2 && f->kids[b + 1]->kind == LOW_CST_GROUP) {
        const low_cst_t *g = f->kids[b + 1];
        const low_cst_t *in = (g->nkids == 1 && g->kids[0]->kind == LOW_CST_FORM) ? g->kids[0] : g;
        // 묶음 속 첫 낱말이 타입 머리면 타입이다(`(result u64)` 처럼 괄호 안에서만 쓰는 짧은 모양도 있다).
        proven_size_t ar0 = 0;
        const low_cst_t *h0 = in->nkids ? in->kids[0] : NULL;
        bool thead = h0 && us_atom(h0) && h0->tok.kind == LOW_TOK_IDENT &&
            (dt_in(h0->tok.lex, DT_ZERO) || dt_in(h0->tok.lex, DT_PRE1) || us_eq(h0->tok.lex, "result") ||
             us_eq(h0->tok.lex, "array") || us_eq(h0->tok.lex, "vec") || us_eq(h0->tok.lex, "bitset") ||
             us_eq(h0->tok.lex, "mask") || us_eq(h0->tok.lex, "cap") || dt_tname(c, h0->tok.lex, &ar0));
        if (!thead) te = (proven_size_t)-1;
    }
    if (te == (proven_size_t)-1) {
        if (c->strict)
            dt_diag(c, f->kids[1], "E-LET-NOTYPE",
                      "a binding must show its type in front of the value — `let x be u64 300 .`, not "
                      "`let x be 300 .`. The type is never guessed (RFC-0132): a guessed type let a bare "
                      "literal skip the width check (X-0074)");
        return;
    }
    // 옮긴다: [kw, name, 타입…, (using x), be, 값…]
    proven_size_t n = f->nkids;
    low_cst_t **nk = (low_cst_t **)c->p.work.alloc_fn(c->p.work.ctx, sizeof(low_cst_t *) * n, alignof(low_cst_t *)).value.ptr;
    if (!nk) return;
    proven_size_t m = 0;
    nk[m++] = f->kids[0]; nk[m++] = f->kids[1];
    // ★ 괄호로 싼 머리 타입(`be (owned shard.token grid) …` · `be (result u64) …`)은 **풀어서** 옛 안쪽 모양으로 넘긴다.
    //   소비자들은 이름 뒤 낱말들에서 `owned`·`mut` 같은 한정어를 읽는다 — 묶음째 넘기면 그것을 못 보고 소유 검사가
    //   빠졌다(실측: E-OWN-MOVED 가 사라졌다). 안쪽 묶음(`option (pool.block_pool pf)`)은 원래대로 둔다.
    const low_cst_t *g0 = f->kids[b + 1];
    if (te == b + 2 && g0->kind == LOW_CST_GROUP && g0->nkids == 1 && g0->kids[0]->kind == LOW_CST_FORM) {
        const low_cst_t *in = g0->kids[0];
        low_cst_t **nk2 = (low_cst_t **)c->p.work.alloc_fn(c->p.work.ctx, sizeof(low_cst_t *) * (n + in->nkids),
                                                           alignof(low_cst_t *)).value.ptr;
        if (!nk2) return;
        nk = nk2;
        m = 0; nk[m++] = f->kids[0]; nk[m++] = f->kids[1];
        for (proven_size_t i = 0; i < in->nkids; i++) nk[m++] = in->kids[i];
    } else
    for (proven_size_t i = b + 1; i < te; i++) nk[m++] = f->kids[i];
    for (proven_size_t i = 2; i <= b; i++) nk[m++] = f->kids[i];
    for (proven_size_t i = te; i < n; i++) nk[m++] = f->kids[i];
    (void)low_refit(&c->p, f, nk, m);
}
static void dt_walk(dt_ctx_t *c, low_cst_t *nd) {
    if (!nd) return;
    if (nd->kind == LOW_CST_FORM && nd->nkids >= 3 && us_atom(nd->kids[0]) &&
        (nd->kids[0]->tok.kw == LOW_KW_LET || nd->kids[0]->tok.kw == LOW_KW_VAR)) dt_decl(c, nd);
    proven_size_t save = c->ntp;
    if (nd->kind == LOW_CST_FORM && nd->nkids >= 2 && us_atom(nd->kids[0]) &&
        (nd->kids[0]->tok.kw == LOW_KW_FN || nd->kids[0]->tok.kw == LOW_KW_PROC || nd->kids[0]->tok.kw == LOW_KW_STRUCT))
        for (proven_size_t q = 0; q + 2 < nd->nkids && c->ntp < 32; q++)
            if (us_atom(nd->kids[q]) && us_eq(nd->kids[q]->tok.lex, "comptime") && us_atom(nd->kids[q + 1]) &&
                us_atom(nd->kids[q + 2]) && us_eq(nd->kids[q + 2]->tok.lex, "type"))
                c->tp[c->ntp++] = nd->kids[q + 1]->tok.lex;
    for (proven_size_t i = 0; i < nd->nkids; i++) dt_walk(c, nd->kids[i]);
    c->ntp = save;
}
static void dt_run(low_parse_result_t *pr, proven_allocator_t node_alloc, proven_allocator_t work, bool strict, bool migrate);
void low_decl_order(low_parse_result_t *pr, proven_allocator_t node_alloc, proven_allocator_t work, bool strict) {
    dt_run(pr, node_alloc, work, strict, false);
}
// `--fmt` 용 — 옛 모양을 새 모양으로 **표면에서** 옮긴다(안쪽 모양으로 바꾸는 low_decl_order 의 반대 방향).
void low_decl_migrate(low_parse_result_t *pr, proven_allocator_t node_alloc, proven_allocator_t work) {
    dt_run(pr, node_alloc, work, false, true);
}
static void dt_run(low_parse_result_t *pr, proven_allocator_t node_alloc, proven_allocator_t work, bool strict, bool migrate) {
    dt_ctx_t *c = (dt_ctx_t *)work.alloc_fn(work.ctx, sizeof(dt_ctx_t), alignof(dt_ctx_t)).value.ptr;
    if (!c) return;
    memset(c, 0, sizeof *c);
    c->p = (low_parser_t){ .node_alloc = node_alloc, .work = work, .out = pr };
    c->strict = strict; c->migrate = migrate;
    for (proven_size_t i = 0; i < pr->nforms; i++) {
        const low_cst_t *f = pr->forms[i];
        if (f && f->kind == LOW_CST_FORM && f->nkids >= 2 && us_atom(f->kids[0]) && f->kids[0]->tok.kw == LOW_KW_MODULE &&
            us_atom(f->kids[1])) c->curmod = f->kids[1]->tok.lex;
        if (f && f->kind == LOW_CST_FORM && f->nkids >= 4 && us_atom(f->kids[0]) && f->kids[0]->tok.kw == LOW_KW_USE &&
            us_atom(f->kids[1]) && c->nal < 512) {
            for (proven_size_t q = 2; q + 1 < f->nkids; q++)
                if (us_atom(f->kids[q]) && us_eq(f->kids[q]->tok.lex, "as") && us_atom(f->kids[q + 1])) {
                    c->al[c->nal].mod = c->curmod; c->al[c->nal].alias = f->kids[q + 1]->tok.lex;
                    c->al[c->nal].target = us_bare(f->kids[1]->tok.lex); c->nal++;
                    break;
                }
        }
        dt_collect(c, f);
        if (f && f->kind == LOW_CST_FORM && f->nkids >= 3 && us_atom(f->kids[0]) && f->kids[0]->tok.kw == LOW_KW_ACTOR &&
            f->kids[f->nkids - 1]->kind == LOW_CST_BLOCK) {
            const low_cst_t *blk = f->kids[f->nkids - 1];
            for (proven_size_t j = 0; j < blk->nkids; j++) dt_collect(c, blk->kids[j]);
        }
    }
    c->curmod = (proven_u8str_view_t){ 0 };
    for (proven_size_t i = 0; i < pr->nforms; i++) {
        const low_cst_t *f = pr->forms[i];
        if (f && f->kind == LOW_CST_FORM && f->nkids >= 2 && us_atom(f->kids[0]) && f->kids[0]->tok.kw == LOW_KW_MODULE &&
            us_atom(f->kids[1])) c->curmod = f->kids[1]->tok.lex;
        dt_walk(c, pr->forms[i]);
    }
}


/* ══ RFC-0135 S1 — 바인딩 `else` ══════════════════════════════════════════════════════════════════════════
 * `let n be T v . else S` 는 값 v 가 option/result 일 때 알맹이를 꺼내 n 에 묶고, 비었으면 S(반드시 벗어난다)로 간다.
 * 모든 뒤따르는 소비자(검사 · 타입 · 하강 · 두 뒤끝)가 이미 아는 모양으로 **펼친다**:
 *     let $n [using a] be <$wrapof|option> T v .        ← 숨은 임시(타입은 아래 풀이가 채운다)
 *     guard $has $n . else S                             ← guard 의 규칙(E-GUARD-FALLTHROUGH)을 그대로 받는다
 *     let|var n be T $take $n .                          ← 알맹이
 * 풀이(`low_bind_else`, 단형화 뒤)가 v 의 타입(부르는 op 의 출력 · 지역·매개변수의 선언 · send 처리기 · `alloc_bytes`)을 보고
 * `$wrapof` → option|result(+오류 타입), `$has` → is_some|is_ok, `$take` → some_value|ok_value 로 바꾼다. 알 수 없으면 거절한다.
 * 할당기 나열(`using` + `lit`)은 펼칠 때 곧바로 `option` 이다(RFC-0132 §13.7 · RFC-0135 §4.3). */
static bool be_is_else(const low_cst_t *n) {
    return n && n->kind == LOW_CST_FORM && n->nkids && us_atom(n->kids[0]) && n->kids[0]->tok.kw == LOW_KW_ELSE;
}
static low_cst_t *be_copy(us_ctx_t *c, const low_cst_t *m) {
    low_cst_t *cp = (low_cst_t *)c->p.node_alloc.alloc_fn(c->p.node_alloc.ctx, sizeof(low_cst_t), alignof(low_cst_t)).value.ptr;
    if (cp) *cp = *m;
    return cp;
}
static low_cst_t *be_word(us_ctx_t *c, const low_cst_t *model, const char *w, low_kw_t kw) {
    low_cst_t *a = us_atom_like(c, model, (proven_u8str_view_t){ .ptr = (const proven_u8 *)w, .size = strlen(w) });
    if (a) { a->tok.kw = kw; a->synth = true; }
    return a;
}
static low_cst_t *be_form(us_ctx_t *c, const low_cst_t *model, low_cst_t **k, proven_size_t n) {
    low_cst_t *f = low_node(&c->p, LOW_CST_FORM, model->tok);
    if (!f) return NULL;
    low_cst_t **kk = (low_cst_t **)c->p.work.alloc_fn(c->p.work.ctx, sizeof(low_cst_t *) * (n ? n : 1), alignof(low_cst_t *)).value.ptr;
    if (!kk) return NULL;
    for (proven_size_t i = 0; i < n; i++) kk[i] = k[i];
    (void)low_refit(&c->p, f, kk, n);
    f->synth = true; f->closer = model->closer;
    return f;
}
static void be_expand_block(us_ctx_t *c, low_cst_t *blk) {
    bool any = false;
    for (proven_size_t i = 0; i < blk->nkids; i++) {
        const low_cst_t *f = blk->kids[i];
        if (f->kind == LOW_CST_FORM && f->nkids >= 4 && us_atom(f->kids[0]) &&
            (f->kids[0]->tok.kw == LOW_KW_LET || f->kids[0]->tok.kw == LOW_KW_VAR) && be_is_else(f->kids[f->nkids - 1])) any = true;
    }
    if (any) {
        proven_size_t cap = blk->nkids * 3;
        low_cst_t **nk = (low_cst_t **)c->p.work.alloc_fn(c->p.work.ctx, sizeof(low_cst_t *) * cap, alignof(low_cst_t *)).value.ptr;
        if (!nk) return;
        proven_size_t m = 0;
        for (proven_size_t i = 0; i < blk->nkids; i++) {
            low_cst_t *f = blk->kids[i];
            bool is_bind = f->kind == LOW_CST_FORM && f->nkids >= 4 && us_atom(f->kids[0]) &&
                           (f->kids[0]->tok.kw == LOW_KW_LET || f->kids[0]->tok.kw == LOW_KW_VAR) && us_atom(f->kids[1]) &&
                           be_is_else(f->kids[f->nkids - 1]);
            proven_size_t be = 0, us = 0;
            if (is_bind) for (proven_size_t q = 2; q + 1 < f->nkids; q++) {
                if (us_atom(f->kids[q]) && f->kids[q]->tok.kw == LOW_KW_BE) { be = q; break; }
                if (us_atom(f->kids[q]) && us_eq(f->kids[q]->tok.lex, "using")) us = q;
            }
            proven_size_t tend = us ? us : be;
            if (!is_bind || !be || tend <= 2 || be + 1 >= f->nkids - 1) {
                if (is_bind) low_pdiag(&c->p, "E-BIND-ELSE", "a binding with `else` needs a type and a value: `let n be u64 find xs 3 . else return 0 .` "
                                       "(RFC-0135 §4.2)", f->kids[0]->tok.line, f->kids[0]->tok.col);
                nk[m++] = f; continue;
            }
            low_cst_t *els = f->kids[f->nkids - 1];
            const low_cst_t *nm = f->kids[1];
            char *tn = (char *)c->p.node_alloc.alloc_fn(c->p.node_alloc.ctx, nm->tok.lex.size + 2, 1).value.ptr;
            if (!tn) { nk[m++] = f; continue; }
            tn[0] = '$'; memcpy(tn + 1, nm->tok.lex.ptr, nm->tok.lex.size); tn[nm->tok.lex.size + 1] = 0;
            // 할당기 나열이면 임시의 타입은 곧바로 option(using 패스가 `option` 을 보고 할당 · 채우기로 바꾼다)
            bool using_lit = false;
            if (us && be + 2 == f->nkids - 1) {
                const low_cst_t *v = f->kids[be + 1];
                while (v && v->kind == LOW_CST_GROUP && v->nkids == 1) v = v->kids[0];
                using_lit = v && v->kind == LOW_CST_FORM && v->nkids >= 3 && us_atom(v->kids[0]) && v->kids[0]->tok.kw == LOW_KW_LIT &&
                            us_atom(v->kids[1]) && (us_eq(v->kids[1]->tok.lex, "array") || us_eq(v->kids[1]->tok.lex, "slice"));
            }
            // ★ RFC-0135 S2a (D9) — 구조체도 할당기 바이트에 짓는다: `var q using al be pt lit pt do … end . else …`
            bool using_struct = false;
            if (us && !using_lit && (be + 2 == f->nkids - 1 || be + 3 == f->nkids - 1)) {
                // 값은 한 폼(`(lit pt do … end)`)이거나, 아직 묶이기 전의 두 낱말(`lit` · `pt do … end`)이다
                const low_cst_t *v = f->kids[be + 1], *sv = NULL;
                while (v && v->kind == LOW_CST_GROUP && v->nkids == 1) v = v->kids[0];
                if (be + 2 == f->nkids - 1)
                    sv = v && v->kind == LOW_CST_FORM && v->nkids == 2 && us_atom(v->kids[0]) && v->kids[0]->tok.kw == LOW_KW_LIT ? v->kids[1] : NULL;
                else if (v && us_atom(v) && v->tok.kw == LOW_KW_LIT) sv = f->kids[be + 2];
                while (sv && sv->kind == LOW_CST_GROUP && sv->nkids == 1) sv = sv->kids[0];
                if (sv && sv->kind == LOW_CST_FORM && sv->nkids == 2 && us_atom(sv->kids[0]) && sv->kids[1]->kind == LOW_CST_BLOCK) {
                    using_lit = using_struct = true;
                    bool ok = tend == 3 && us_atom(f->kids[2]) && proven_u8str_view_eq(f->kids[2]->tok.lex, sv->kids[0]->tok.lex);
                    if (!ok) low_pdiag(&c->p, "E-BIND-ELSE", "a struct built in an allocator's bytes binds as that struct — write "
                                       "`be <struct> lit <struct> do … end . else …` (RFC-0135 §4.5)",
                                       f->kids[0]->tok.line, f->kids[0]->tok.col);
                }
            }
            if (using_lit && !using_struct) {           // 바인딩 타입은 `[mut] slice <나열의 원소 타입>` 이어야 한다
                const low_cst_t *v = f->kids[be + 1];
                while (v && v->kind == LOW_CST_GROUP && v->nkids == 1) v = v->kids[0];
                proven_size_t q = 2;
                while (q < tend && us_atom(f->kids[q]) && us_eq(f->kids[q]->tok.lex, "mut")) q++;
                bool ok = tend - q == 2 && us_atom(f->kids[q]) && us_eq(f->kids[q]->tok.lex, "slice") && us_atom(f->kids[q + 1]) &&
                          proven_u8str_view_eq(f->kids[q + 1]->tok.lex, v->kids[2]->tok.lex);
                if (!ok) low_pdiag(&c->p, "E-BIND-ELSE", "a list taken from an allocator is a slice of its element type — write "
                                   "`be mut slice <element type> lit array <element type> … . else …` (RFC-0135 §4.2)",
                                   f->kids[0]->tok.line, f->kids[0]->tok.col);
            }
            low_cst_t *tk[256]; proven_size_t tnk = 0;
            if (f->nkids + 4 > 256) { nk[m++] = f; continue; }
            // ① 임시: [let, $n, $wrapof|option, T…, (using x), be, v…]
            tk[tnk++] = be_word(c, f->kids[0], "let", LOW_KW_LET);
            tk[tnk++] = be_word(c, nm, tn, LOW_KW_NONE);
            tk[tnk++] = be_word(c, nm, using_lit ? "option" : "$wrapof", LOW_KW_NONE);
            for (proven_size_t q = 2; q < tend; q++) tk[tnk++] = be_copy(c, f->kids[q]);
            for (proven_size_t q = tend; q + 1 < f->nkids; q++) tk[tnk++] = f->kids[q];   // using x · be · 값(옮긴다 — 한 자리에만)
            low_cst_t *tmp = be_form(c, f, tk, tnk);
            // ② guard $has $n . else S
            low_cst_t *gk[4] = { be_word(c, f->kids[0], "guard", LOW_KW_GUARD), be_word(c, nm, "$has", LOW_KW_NONE),
                                 be_word(c, nm, tn, LOW_KW_NONE), els };
            low_cst_t *gd = be_form(c, f, gk, 4);
            // ③ [kw, n, T…, be, $take, $n]
            low_cst_t *fk[256]; proven_size_t fnk = 0;
            fk[fnk++] = f->kids[0]; fk[fnk++] = f->kids[1];
            for (proven_size_t q = 2; q < tend; q++) fk[fnk++] = f->kids[q];
            fk[fnk++] = be_copy(c, f->kids[be]);
            fk[fnk++] = be_word(c, nm, "$take", LOW_KW_NONE);
            fk[fnk++] = be_word(c, nm, tn, LOW_KW_NONE);
            low_cst_t *fin = be_form(c, f, fk, fnk);
            if (!tmp || !gd || !fin) { nk[m++] = f; continue; }
            fin->synth = false;          // 저자가 쓴 이름의 바인딩 — 검사는 보통 바인딩처럼 한다
            nk[m++] = tmp; nk[m++] = gd; nk[m++] = fin;
        }
        (void)low_refit(&c->p, blk, nk, m);
    }
    for (proven_size_t i = 0; i < blk->nkids; i++) {
        low_cst_t *k = blk->kids[i];
        for (proven_size_t j = 0; j < k->nkids; j++) if (k->kids[j]->kind == LOW_CST_BLOCK) be_expand_block(c, k->kids[j]);
            else if (k->kids[j]->kind == LOW_CST_FORM)
                for (proven_size_t z = 0; z < k->kids[j]->nkids; z++) if (k->kids[j]->kids[z]->kind == LOW_CST_BLOCK) be_expand_block(c, k->kids[j]->kids[z]);
    }
}
static void be_expand_any(us_ctx_t *c, low_cst_t *nd) {
    if (!nd || nd->kind == LOW_CST_ATOM) return;
    if (nd->kind == LOW_CST_BLOCK) { be_expand_block(c, nd); return; }
    for (proven_size_t i = 0; i < nd->nkids; i++) be_expand_any(c, nd->kids[i]);
}
// ★ RFC-0135 D12 — `let|var <이름> … using <할당기> keep be …` 에서 `keep` 을 빼고 `using` 원자에 표시한다.
//   `keep` 은 `using <할당기>` 바로 뒤에만 뜻이 있다 — 다른 자리면 말한다.
static void be_strip_keep(us_ctx_t *c, low_cst_t *nd) {
    if (!nd || nd->kind == LOW_CST_ATOM) return;
    if (nd->kind == LOW_CST_FORM && nd->nkids >= 4 && us_atom(nd->kids[0]) &&
        (nd->kids[0]->tok.kw == LOW_KW_LET || nd->kids[0]->tok.kw == LOW_KW_VAR)) {
        proven_size_t be = nd->nkids;
        for (proven_size_t q = 2; q < nd->nkids; q++) if (us_atom(nd->kids[q]) && nd->kids[q]->tok.kw == LOW_KW_BE) { be = q; break; }
        for (proven_size_t q = 2; q < be; q++) {
            if (!us_atom(nd->kids[q]) || !us_eq(nd->kids[q]->tok.lex, "keep")) continue;
            if (q >= 4 && us_atom(nd->kids[q - 2]) && us_eq(nd->kids[q - 2]->tok.lex, "using") && q + 1 == be)
                nd->kids[q - 2]->is_keep = true;
            else
                low_pdiag(&c->p, "E-USING-FORM", "`keep` goes right after the allocator, before `be`: "
                          "`var t using al keep be mut slice u8 lit … . else … .` (RFC-0135 D12)",
                          nd->kids[q]->tok.line, nd->kids[q]->tok.col);
            for (proven_size_t z = q; z + 1 < nd->nkids; z++) nd->kids[z] = nd->kids[z + 1];   // 뒤의 패스는 `keep` 을 보지 않는다
            nd->nkids--;
            break;
        }
    }
    for (proven_size_t i = 0; i < nd->nkids; i++) be_strip_keep(c, nd->kids[i]);
}
void low_bind_keep_strip(low_parse_result_t *pr, proven_allocator_t node_alloc, proven_allocator_t work) {
    us_ctx_t *c = (us_ctx_t *)work.alloc_fn(work.ctx, sizeof(us_ctx_t), alignof(us_ctx_t)).value.ptr;
    if (!c) return;
    memset(c, 0, sizeof *c);
    c->p = (low_parser_t){ .node_alloc = node_alloc, .work = work, .out = pr };
    for (proven_size_t i = 0; i < pr->nforms; i++) be_strip_keep(c, pr->forms[i]);
}
void low_bind_else_expand(low_parse_result_t *pr, proven_allocator_t node_alloc, proven_allocator_t work) {
    us_ctx_t *c = (us_ctx_t *)work.alloc_fn(work.ctx, sizeof(us_ctx_t), alignof(us_ctx_t)).value.ptr;
    if (!c) return;
    memset(c, 0, sizeof *c);
    c->p = (low_parser_t){ .node_alloc = node_alloc, .work = work, .out = pr };
    for (proven_size_t i = 0; i < pr->nforms; i++) be_expand_any(c, pr->forms[i]);
}

/* 풀이: v 의 타입의 첫 낱말(option/result)과 result 의 오류 타입 낱말. */
static const low_cst_t *br_op_form(const low_parse_result_t *pr, proven_u8str_view_t name) {
    proven_u8str_view_t bare = us_bare(name);
    const low_cst_t *hit = NULL;
    for (proven_size_t i = 0; i < pr->nforms; i++) {
        const low_cst_t *f = pr->forms[i];
        if (f->kind != LOW_CST_FORM || f->nkids < 3 || !us_atom(f->kids[0]) || !us_atom(f->kids[1])) continue;
        low_kw_t k = f->kids[0]->tok.kw;
        if (k != LOW_KW_FN && k != LOW_KW_PROC) continue;
        if (proven_u8str_view_eq(f->kids[1]->tok.lex, name)) return f;
        if (!hit && proven_u8str_view_eq(us_bare(f->kids[1]->tok.lex), bare)) hit = f;
    }
    return hit;
}
typedef struct { low_cst_t *const *w; proven_size_t n; } br_words_t;
static br_words_t br_out_words(const low_cst_t *opf) {
    low_op_header_t h = low_op_header(opf);
    br_words_t r = { 0 };
    if (h.out_e > h.out_s) { r.w = opf->kids + h.out_s; r.n = h.out_e - h.out_s; }
    return r;
}
static br_words_t br_local_words(const low_cst_t *op, const low_cst_t *body, proven_u8str_view_t nm) {
    br_words_t r = { 0 };
    low_op_header_t h = low_op_header(op);
    for (proven_size_t q = 0; q < h.np; q++)
        if (proven_u8str_view_eq(h.p[q].name, nm)) { r.w = op->kids + h.p[q].core; r.n = h.p[q].te - h.p[q].core; return r; }
    // 몸의 바인딩(안쪽 모양: [kw, 이름, 타입…, (using x), be, …])
    const low_cst_t *stack[256]; proven_size_t sp = 0;
    if (body) stack[sp++] = body;
    while (sp) {
        const low_cst_t *n = stack[--sp];
        if (n->kind == LOW_CST_FORM && n->nkids >= 3 && us_atom(n->kids[0]) &&
            (n->kids[0]->tok.kw == LOW_KW_LET || n->kids[0]->tok.kw == LOW_KW_VAR) && us_atom(n->kids[1]) &&
            proven_u8str_view_eq(n->kids[1]->tok.lex, nm)) {
            proven_size_t e = 2;
            while (e < n->nkids && !(us_atom(n->kids[e]) && (n->kids[e]->tok.kw == LOW_KW_BE || us_eq(n->kids[e]->tok.lex, "using")))) e++;
            proven_size_t s = 2;
            while (s < e && us_atom(n->kids[s]) && (us_eq(n->kids[s]->tok.lex, "mut") || us_eq(n->kids[s]->tok.lex, "owned"))) s++;
            r.w = n->kids + s; r.n = e - s; return r;
        }
        for (proven_size_t i = 0; i < n->nkids && sp < 256; i++) if (n->kids[i]->kind != LOW_CST_ATOM) stack[sp++] = n->kids[i];
    }
    return r;
}
// 타입 낱말 줄의 종류: 1 = option · 2 = result(오류 타입 낱말을 *err 에) · -1 = 둘 다 아님 · 0 = 모른다
static int br_words_kind(br_words_t w, const low_cst_t **err) {
    if (!w.n) return 0;
    const low_cst_t *w0 = w.w[0];
    while (w0 && w0->kind == LOW_CST_GROUP && w0->nkids) w0 = w0->kids[0];
    if (w0 && w0->kind == LOW_CST_FORM && w0->nkids) {                  // `(result u64 e)` 처럼 괄호로 싼 타입
        if (us_atom(w0->kids[0]) && us_eq(w0->kids[0]->tok.lex, "result") && w0->nkids >= 3) { *err = w0->kids[w0->nkids - 1]; return 2; }
        w0 = w0->kids[0];
    }
    if (!w0 || !us_atom(w0)) return 0;
    if (us_eq(w0->tok.lex, "option")) return 1;
    if (us_eq(w0->tok.lex, "result")) { *err = w.w[w.n - 1]; return 2; }
    return -1;
}
static int br_value_kind(const low_parse_result_t *pr, const low_cst_t *op, const low_cst_t *body,
                         low_cst_t *const *v, proven_size_t nv, const low_cst_t **err, br_words_t *wout) {
    if (!nv) return 0;
    const low_cst_t *h = v[0];
    low_cst_t *const *args = v + 1; proven_size_t na = nv - 1;
    while (nv == 1 && h && h->kind == LOW_CST_GROUP && h->nkids == 1) h = h->kids[0];
    if (nv == 1 && h && h->kind == LOW_CST_FORM && h->nkids) { args = h->kids + 1; na = h->nkids - 1; h = h->kids[0]; }
    if (!h || !us_atom(h)) return 0;
    if (na == 0 && h->tok.kind == LOW_TOK_IDENT) {                       // 이름 하나 — 매개변수·지역의 선언
        br_words_t lw = br_local_words(op, body, h->tok.lex);
        int k = br_words_kind(lw, err);
        if (k) { *wout = lw; return k; }
    }
    if (h->tok.kind == LOW_TOK_NUMBER || h->tok.kind == LOW_TOK_STRING) return -1;
    if (us_eq(h->tok.lex, "alloc_bytes")) return 1;                      // option mut slice u8
    if (us_eq(h->tok.lex, "send") && na >= 2 && us_atom(args[0]) && us_atom(args[1])) {   // send <actor> <처리기> …
        br_words_t at = br_local_words(op, body, args[0]->tok.lex);
        if (!at.n || !us_atom(at.w[0])) return 0;
        proven_u8str_view_t an = us_bare(at.w[0]->tok.lex);
        for (proven_size_t i = 0; i < pr->nforms; i++) {
            const low_cst_t *f = pr->forms[i];
            if (f->kind != LOW_CST_FORM || f->nkids < 3 || !us_atom(f->kids[0]) || f->kids[0]->tok.kw != LOW_KW_ACTOR ||
                !us_atom(f->kids[1]) || !proven_u8str_view_eq(us_bare(f->kids[1]->tok.lex), an)) continue;
            const low_cst_t *blk = f->kids[f->nkids - 1];
            for (proven_size_t j = 0; blk->kind == LOW_CST_BLOCK && j < blk->nkids; j++) {
                const low_cst_t *hf = blk->kids[j];
                if (hf->kind == LOW_CST_FORM && hf->nkids >= 3 && us_atom(hf->kids[1]) &&
                    proven_u8str_view_eq(hf->kids[1]->tok.lex, args[1]->tok.lex)) { *wout = br_out_words(hf); return br_words_kind(*wout, err); }
            }
        }
        return 0;
    }
    const low_cst_t *opf = br_op_form(pr, h->tok.lex);
    if (opf) { *wout = br_out_words(opf); return br_words_kind(*wout, err); }
    // 내장 op 가운데 비어 있을 수 있는 값을 내는 것은 위의 것뿐이다 — 나머지 내장은 option/result 가 아니다
    if (low_ir_is_builtin_name(h->tok.lex) && !us_eq(h->tok.lex, "try_view") && !us_eq(h->tok.lex, "pop")) return -1;
    return 0;
}
// 타입 낱말 줄을 펼쳐 낱말 목록으로(괄호 한 겹은 벗긴다 · 앞의 `mut` 는 뺀다 — `mut slice` 를 `slice` 로 받아도 된다)
static proven_size_t br_flat(low_cst_t *const *w, proven_size_t n, proven_u8str_view_t *out, proven_size_t cap) {
    proven_size_t m = 0;
    for (proven_size_t i = 0; i < n && m < cap; i++) {
        const low_cst_t *x = w[i];
        if (x->kind == LOW_CST_GROUP || x->kind == LOW_CST_FORM) { m += br_flat(x->kids, x->nkids, out + m, cap - m); continue; }
        if (us_atom(x)) out[m++] = us_bare(x->tok.lex);
    }
    return m;
}
// 값 타입 W(option P / result P E)의 알맹이 P 와 바인딩 타입 T 가 같은가. 모르면 참(묻지 않는다).
static bool br_payload_ok(br_words_t w, int kind, low_cst_t *const *t, proven_size_t nt) {
    if (!w.n) return true;
    proven_u8str_view_t a[64], b[64];
    proven_size_t na = br_flat(w.w, w.n, a, 64), nb = br_flat(t, nt, b, 64);
    if (na < 2) return true;
    proven_size_t s0 = 1, e0 = kind == 2 ? na - 1 : na;               // [option|result] P… [E]
    while (s0 < e0 && low_view_eq_cstr(a[s0], "mut")) s0++;
    proven_size_t s1 = 0;
    while (s1 < nb && low_view_eq_cstr(b[s1], "mut")) s1++;
    if (e0 - s0 != nb - s1) return false;
    for (proven_size_t i = 0; i < e0 - s0; i++) if (!proven_u8str_view_eq(a[s0 + i], b[s1 + i])) return false;
    return true;
}
typedef struct { proven_u8str_view_t name; int kind; } br_tmp_t;   // kind 1 = option · 2 = result
// ★ 바인딩 else 가 옮겨 적는 오류 타입 낱말은 **부른 op 의 모듈**에서 온다. 그 모듈이 부르는 쪽과 다르면 맨이름으로 옮기면
//   부르는 쪽이 남의 이름을 맨이름으로 쓰는 꼴이 된다(E-VISIBILITY — 벤치 탐침 crypto_probe 가 드러냈다). 선언한 모듈을 찾아
//   `<모듈>.<이름>` 으로 적는다 — 사람이 적을 철자와 같다.
static proven_u8str_view_t g_br_curmod;
static bool br_holds(const low_cst_t *f, const low_cst_t *x) {   // 머리(몸 밖)에 그 노드가 있나 · actor 면 처리기 머리까지
    if (!f) return false;
    if (f == x) return true;
    if (f->kind == LOW_CST_ATOM) return false;
    for (proven_size_t i = 0; i < f->nkids; i++) {
        const low_cst_t *k = f->kids[i];
        if (k->kind == LOW_CST_BLOCK && i + 1 == f->nkids && f->kind == LOW_CST_FORM && us_atom(f->kids[0]) &&
            f->kids[0]->tok.kw != LOW_KW_ACTOR) continue;                 // op 몸은 보지 않는다
        if (br_holds(k, x)) return true;
    }
    return false;
}
static const low_cst_t *br_qualify(us_ctx_t *c, const low_cst_t *err) {
    if (!us_atom(err)) return err;
    proven_u8str_view_t w = err->tok.lex;
    for (proven_size_t i = 0; i < w.size; i++) if (w.ptr[i] == '.') return err;
    proven_u8str_view_t mod = { 0 }, decl = { 0 };
    // ① 이 낱말은 부른 op 의 머리(출력 절)에서 온 노드다 — 그 폼의 모듈이 곧 답이다
    for (proven_size_t i = 0; i < c->p.out->nforms; i++) {
        const low_cst_t *f = c->p.out->forms[i];
        if (!f || f->kind != LOW_CST_FORM || f->nkids < 2 || !us_atom(f->kids[0])) continue;
        if (f->kids[0]->tok.kw == LOW_KW_MODULE && us_atom(f->kids[1])) { mod = f->kids[1]->tok.lex; continue; }
        if (br_holds(f, err)) {
            if (!mod.size || proven_u8str_view_eq(mod, g_br_curmod)) return err;
            decl = mod;
            goto qualify;
        }
    }
    // ② 못 찾으면 그 이름을 선언한 모듈(제 모듈에 있으면 맨이름 그대로)
    mod = (proven_u8str_view_t){ 0 };
    for (proven_size_t i = 0; i < c->p.out->nforms; i++) {
        const low_cst_t *f = c->p.out->forms[i];
        if (!f || f->kind != LOW_CST_FORM || f->nkids < 2 || !us_atom(f->kids[0]) || !us_atom(f->kids[1])) continue;
        low_kw_t k = f->kids[0]->tok.kw;
        if (k == LOW_KW_MODULE) { mod = f->kids[1]->tok.lex; continue; }
        if ((k == LOW_KW_ENUM || k == LOW_KW_STRUCT || us_eq(f->kids[0]->tok.lex, "type") || us_eq(f->kids[0]->tok.lex, "newtype")) &&
            proven_u8str_view_eq(f->kids[1]->tok.lex, w)) { decl = mod; if (proven_u8str_view_eq(mod, g_br_curmod)) return err; }
    }
    if (!decl.size || proven_u8str_view_eq(decl, g_br_curmod)) return err;
qualify:;
    char *q = (char *)c->p.node_alloc.alloc_fn(c->p.node_alloc.ctx, decl.size + w.size + 2, 1).value.ptr;
    if (!q) return err;
    memcpy(q, decl.ptr, decl.size); q[decl.size] = '.'; memcpy(q + decl.size + 1, w.ptr, w.size); q[decl.size + w.size + 1] = 0;
    low_cst_t *a = us_atom_like(c, err, (proven_u8str_view_t){ .ptr = (const proven_u8 *)q, .size = decl.size + w.size + 1 });
    return a ? a : err;
}
static void br_walk(us_ctx_t *c, const low_cst_t *op, low_cst_t *body, low_cst_t *nd, br_tmp_t *t, proven_size_t *nt) {
    if (!nd || nd->kind == LOW_CST_ATOM) return;
    if (nd->kind == LOW_CST_FORM && nd->nkids >= 4 && us_atom(nd->kids[0]) && nd->kids[0]->tok.kw == LOW_KW_LET &&
        us_atom(nd->kids[1]) && nd->kids[1]->tok.lex.size > 1 && nd->kids[1]->tok.lex.ptr[0] == '$' && us_atom(nd->kids[2])) {
        int kind = 0;
        if (us_eq(nd->kids[2]->tok.lex, "option")) kind = 1;
        else if (us_eq(nd->kids[2]->tok.lex, "$wrapof")) {
            proven_size_t be = 0;
            for (proven_size_t q = 3; q < nd->nkids; q++) if (us_atom(nd->kids[q]) && nd->kids[q]->tok.kw == LOW_KW_BE) { be = q; break; }
            const low_cst_t *err = NULL; br_words_t vw = { 0 };
            kind = be ? br_value_kind(c->p.out, op, body, nd->kids + be + 1, nd->nkids - be - 1, &err, &vw) : 0;
            proven_size_t tend = 3;
            while (tend < nd->nkids && !(us_atom(nd->kids[tend]) && (nd->kids[tend]->tok.kw == LOW_KW_BE || us_eq(nd->kids[tend]->tok.lex, "using")))) tend++;
            if (kind > 0 && !br_payload_ok(vw, kind, nd->kids + 3, tend - 3))
                low_pdiag(&c->p, "E-BIND-ELSE", "the value holds a different type than this binding takes — the type written after `be` "
                          "is what comes OUT of the option/result, and it must be that content's type (RFC-0135 §4.2)",
                          nd->kids[0]->tok.line, nd->kids[0]->tok.col);
            if (kind <= 0) {
                low_pdiag(&c->p, "E-BIND-ELSE", kind < 0
                    ? "`else` on a binding takes a value that can be empty — an `option` or a `result`. This value is neither: "
                      "bind it plainly, without `else` (RFC-0135 §4.2)"
                    : "cannot tell here whether this value is an `option` or a `result` — `else` works on calls, names and "
                      "`send`; bind the value with its type first (`let r be option u64 … .`) and `guard` it (RFC-0135 §4.2)",
                    nd->kids[0]->tok.line, nd->kids[0]->tok.col);
                kind = 1;
            }
            nd->kids[2]->tok.lex = kind == 2 ? (proven_u8str_view_t){ .ptr = (const proven_u8 *)"result", .size = 6 }
                                             : (proven_u8str_view_t){ .ptr = (const proven_u8 *)"option", .size = 6 };
            if (kind == 2 && err) err = br_qualify(c, err);              // ★ 남의 모듈의 오류 타입이면 `<모듈>.<이름>` 으로 적는다
            if (kind == 2 && err) {                                      // 오류 타입 낱말을 T 뒤(using/be 앞)에 끼운다
                proven_size_t at = 3;
                while (at < nd->nkids && !(us_atom(nd->kids[at]) && (nd->kids[at]->tok.kw == LOW_KW_BE || us_eq(nd->kids[at]->tok.lex, "using")))) at++;
                low_cst_t **nk = (low_cst_t **)c->p.work.alloc_fn(c->p.work.ctx, sizeof(low_cst_t *) * (nd->nkids + 1), alignof(low_cst_t *)).value.ptr;
                if (nk) {
                    proven_size_t m = 0;
                    for (proven_size_t q = 0; q < nd->nkids; q++) { if (q == at) nk[m++] = be_copy(c, err); nk[m++] = nd->kids[q]; }
                    (void)low_refit(&c->p, nd, nk, m);
                }
            }
        }
        if (kind && *nt < 256) { t[*nt].name = nd->kids[1]->tok.lex; t[*nt].kind = kind; (*nt)++; }
    }
    for (proven_size_t i = 0; i < nd->nkids; i++) {
        low_cst_t *k = nd->kids[i];
        if (us_atom(k) && (us_eq(k->tok.lex, "$has") || us_eq(k->tok.lex, "$take")) && i + 1 < nd->nkids && us_atom(nd->kids[i + 1])) {
            int kind = 1;
            for (proven_size_t z = *nt; z-- > 0; ) if (proven_u8str_view_eq(t[z].name, nd->kids[i + 1]->tok.lex)) { kind = t[z].kind; break; }
            bool has = us_eq(k->tok.lex, "$has");
            const char *w = has ? (kind == 2 ? "is_ok" : "is_some") : (kind == 2 ? "ok_value" : "some_value");
            k->tok.lex = (proven_u8str_view_t){ .ptr = (const proven_u8 *)w, .size = strlen(w) };
            continue;
        }
        br_walk(c, op, body, k, t, nt);
    }
}
void low_bind_else(low_parse_result_t *pr, proven_allocator_t node_alloc, proven_allocator_t work) {
    us_ctx_t *c = (us_ctx_t *)work.alloc_fn(work.ctx, sizeof(us_ctx_t), alignof(us_ctx_t)).value.ptr;
    if (!c) return;
    memset(c, 0, sizeof *c);
    c->p = (low_parser_t){ .node_alloc = node_alloc, .work = work, .out = pr };
    static br_tmp_t t[256];
    g_br_curmod = (proven_u8str_view_t){ 0 };
    for (proven_size_t i = 0; i < pr->nforms; i++) {
        low_cst_t *f = pr->forms[i];
        if (f && f->kind == LOW_CST_FORM && f->nkids >= 2 && us_atom(f->kids[0]) && f->kids[0]->tok.kw == LOW_KW_MODULE &&
            us_atom(f->kids[1])) { g_br_curmod = f->kids[1]->tok.lex; continue; }
        if (!f || f->kind != LOW_CST_FORM || f->nkids < 3 || !us_atom(f->kids[0])) continue;
        low_kw_t k = f->kids[0]->tok.kw;
        if (k == LOW_KW_FN || k == LOW_KW_PROC) {
            proven_size_t nt = 0;
            low_cst_t *body = f->kids[f->nkids - 1];
            if (body->kind == LOW_CST_BLOCK) br_walk(c, f, body, body, t, &nt);
        } else if (k == LOW_KW_ACTOR && f->kids[f->nkids - 1]->kind == LOW_CST_BLOCK) {
            low_cst_t *blk = f->kids[f->nkids - 1];
            for (proven_size_t j = 0; j < blk->nkids; j++) {
                low_cst_t *hf = blk->kids[j];
                if (hf->kind == LOW_CST_FORM && hf->nkids >= 3 && hf->kids[hf->nkids - 1]->kind == LOW_CST_BLOCK) {
                    proven_size_t nt = 0; br_walk(c, hf, hf->kids[hf->nkids - 1], hf->kids[hf->nkids - 1], t, &nt);
                }
            }
        }
    }
}
