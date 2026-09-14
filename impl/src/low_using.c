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
 *   · **부르는 쪽** — 바인딩의 `using <출처>`(`let v T using hb be vecgen.open u32 16 .`), 아니면 기본값.
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
#include <string.h>

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
// ★★★ **고정 길이 입력 `array <개수> <타입>`** (정본 §6.2.6 (1) · 2026-09-14).
//   처리기는 `array` 를 `slice` 의 다른 이름으로 다뤘다 — 바로 뒤 낱말을 원소 타입으로 읽고 **길이는 버렸다.**
//   그래서 정본 모양 `array 4 u64` 는 원소를 모르는 **바이트 슬라이스**가 됐고(`4` 가 원소 자리), 틀린 차례
//   `array u64 4` 는 길이 검사 없는 `slice u64` 였다. 둘 다 조용히 통과했다.
//   ⇒ 입력 자리의 `array N T` 를 **`slice T` + 진입 계약 `requires eq (len <이름>) N .`** 으로 바꿔 적는다.
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
                us_is_int_lit(f->kids[z + 1]) && us_atom(f->kids[z + 2]) && !us_is_int_lit(f->kids[z + 2])) {
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
                nk[m++] = f->kids[at[a] + 1];          // 개수 리터럴을 계약으로 옮긴다
            }
        if (i == f->nkids) break;
        bool drop = false, swap = false;
        for (proven_size_t a = 0; a < na; a++) { if (i == at[a] + 1) drop = true; if (i == at[a]) swap = true; }
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
        if (i < at && r > 5) {
            low_pdiag(&c->p, "E-CLAUSE-ORDER",
                      "`using` comes after a clause that belongs behind it. `using` stands after the `comptime` and "
                      "capability/region inputs and before the data inputs and everything after them (WO-0217)",
                      f->kids[at]->tok.line, f->kids[at]->tok.col);
            break;
        }
        if (i > at && r < 5) {
            low_pdiag(&c->p, "E-CLAUSE-ORDER",
                      "`using` comes before a clause that belongs in front of it — a `comptime` input, a capability/region "
                      "input, `output`, `lowdoc` or `vector`/`priority`. `using` stands after those and before the data inputs (WO-0217)",
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
    //   ★ 2026-09-15 — `output`·`satisfies`·`lowdoc`·`vector` 도 그 앞에 서므로(차례 0~2) 그 끝까지 본다.
    proven_size_t ins = 2;
    for (proven_size_t i = 2; i < f->nkids; i++) {
        if (i == at || !us_atom(f->kids[i]) || !low_is_clause_word(f->kids[i]->tok.lex)) continue;
        proven_size_t e = i + 1;
        while (e < f->nkids && us_atom(f->kids[e]) && !low_is_clause_word(f->kids[e]->tok.lex)) e++;
        int r = us_eq(f->kids[i]->tok.lex, "input") ? low_input_rank(f, i, e) : low_clause_rank(f->kids[i]->tok.lex);
        if (r >= 0 && r <= 4 && e > ins) ins = e;
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
        if (explicit_src && !u) {
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
