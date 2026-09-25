// run_tests.c — S0 front-end smoke tests (lexer + point-closure CST).
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "proven/heap.h"
#include "proven/arena.h"
#include "low_lex.h"
#include "low_cst.h"
#include "low_check.h"
#include "low_typecheck.h"
#include "low_contract.h"
#include "low_region.h"
#include "low_ir.h"
#include "low_blake3.h"
#include "low_smt.h"

static int g_fail = 0;
static void check(bool cond, const char *name) {
    printf("  [%s] %s\n", cond ? "PASS" : "FAIL", name);
    if (!cond) g_fail++;
}

// ── V1 fuzzer: deterministic program generator (MVP core: ints/refs/loops) ────
// Property: a program that passes ALL four static checks must run on the
// verifying VM without any E-VM-* soundness diagnostic (dangling / EXCL /
// readonly / type). Any hit = a static-checker false negative.
static proven_u64 fz_state;
static proven_u32 fz_rnd(proven_u32 n) {
    fz_state = fz_state * 6364136223846793005ull + 1442695040888963407ull;
    return (proven_u32)((fz_state >> 33) % n);
}
typedef struct {
    char buf[6144]; proven_size_t len;
    int nv;                 // int locals v0..
    bool is_counter[48];    // loop counters: never a set-target or borrow target
    int nb; bool bmut[12];  // borrows r0.. (targets are int locals)
    int nr;                 // record locals q0.. (make fzr)
    int ns;                 // slice locals s0.. (encode outputs)
    int nw;                 // vector locals w0..
} fz_t;
static void fz_str(fz_t *g, char *out, int minlen) {   // random short identifier-ish string
    (void)g;
    int n = minlen + (int)fz_rnd(5);
    int i = 0;
    for (; i < n && i < 10; i++) out[i] = (char)('a' + fz_rnd(26));
    out[i] = 0;
}
static int fz_pick_var(fz_t *g, bool allow_counter) {
    for (int tries = 0; tries < 8; tries++) {
        int cand = (int)fz_rnd((proven_u32)g->nv);
        if (allow_counter || !g->is_counter[cand]) return cand;
    }
    return -1;
}
static void fz_put(fz_t *g, const char *s) {
    proven_size_t n = strlen(s);
    if (g->len + n + 1 < sizeof g->buf) { memcpy(g->buf + g->len, s, n); g->len += n; g->buf[g->len] = 0; }
}
static void fz_putf(fz_t *g, const char *fmt, int a, int b) {
    char tmp[96];
    snprintf(tmp, sizeof tmp, fmt, a, b);
    fz_put(g, tmp);
}
static void fz_iexpr(fz_t *g, int depth) {
    proven_u32 k = fz_rnd(g->nv > 0 ? 4 : 2);
    if (depth <= 0 && k >= 2) k = fz_rnd(2) ? 0 : (g->nv ? 2 : 0);
    switch (k) {
        case 0: fz_putf(g, "%d", (int)fz_rnd(50), 0); break;
        case 1: fz_putf(g, "%d", (int)fz_rnd(50), 0); break;
        case 2: fz_putf(g, "v%d", (int)fz_rnd((proven_u32)g->nv), 0); break;
        default:
            fz_put(g, fz_rnd(2) ? "add " : "mul ");
            fz_iexpr(g, depth - 1); fz_put(g, " ");
            fz_iexpr(g, depth - 1);
            break;
    }
}
static void fz_stmts(fz_t *g, int budget, int nest);
static void fz_stmt(fz_t *g, int nest) {
    proven_u32 k = fz_rnd(13);
    if ((k == 3 || k == 4 || k == 12) && g->nb == 0) k = 0;   // needs a borrow
    if (k == 1 && nest > 0) k = 0;                 // borrows only at top level: always executed
    if ((k == 5 || k == 6) && nest >= 2) k = 0;    // cap nesting
    if (k >= 8 && nest > 0) k = 0;                 // feature templates only at top level
    if (k == 10 && g->nr == 0) k = 9;              // encode needs a record
    if (g->nv >= 40) k = 2;                         // stay under the IR locals cap
    switch (k) {
        case 0: case 7:   // new int var
            fz_putf(g, "var v%d u64 be ", g->nv, 0);
            fz_iexpr(g, 2); fz_put(g, " . ");
            g->nv++;
            break;
        case 1: {         // new borrow of a random (non-counter) int var
            int t = fz_pick_var(g, false);
            if (t < 0 || g->nb >= 12) { fz_putf(g, "var v%d u64 be 1 . ", g->nv++, 0); return; }
            g->bmut[g->nb] = fz_rnd(2);
            fz_putf(g, g->bmut[g->nb] ? "var r%d mut_ref u64 . be mut_ref v%d . "
                                      : "var r%d ref u64 . be ref v%d . ",
                    g->nb, t);
            g->nb++;
            break;
        }
        case 2: {         // owner write (never a loop counter)
            int t = fz_pick_var(g, false);
            if (t < 0) { fz_putf(g, "var v%d u64 be 1 . ", g->nv++, 0); return; }
            fz_putf(g, "set v%d ", t, 0);
            fz_iexpr(g, 1); fz_put(g, " . ");
            break;
        }
        case 3: {         // write-through a mut borrow (if any)
            int m = -1;
            for (int i = 0; i < g->nb; i++) if (g->bmut[i]) m = i;
            if (m < 0) { fz_putf(g, "var v%d u64 be 1 . ", g->nv++, 0); return; }
            fz_putf(g, "set r%d ", m, 0);
            fz_iexpr(g, 1); fz_put(g, " . ");
            break;
        }
        case 4:           // read through a borrow into a new var
            fz_putf(g, "var v%d u64 be deref r%d . ", g->nv, (int)fz_rnd((proven_u32)g->nb));
            g->nv++;
            break;
        case 5:           // if block
            fz_put(g, "if lt ");
            fz_iexpr(g, 1); fz_put(g, " ");
            fz_iexpr(g, 1); fz_put(g, " . do ");
            fz_stmts(g, 1 + (int)fz_rnd(2), nest + 1);
            fz_put(g, "end ");
            break;
        case 6: {         // bounded while: dedicated counter, never reset elsewhere
            int c = g->nv;
            if (c >= 48) { fz_putf(g, "var v%d u64 be 1 . ", g->nv++, 0); return; }
            g->is_counter[c] = true;
            g->nv++;
            fz_putf(g, "var v%d u64 be 0 . while lt v%d ", c, c);
            fz_putf(g, "%d . do ", 2 + (int)fz_rnd(3), 0);
            fz_stmts(g, 1 + (int)fz_rnd(2), nest + 1);
            fz_putf(g, "set v%d expr v%d + 1 . . end ", c, c);
            break;
        }
        case 8: {         // string literal: len / in-bounds index
            char s[12]; fz_str(g, s, 3);
            if (fz_rnd(2)) { fz_putf(g, "var v%d u64 be len \"", g->nv, 0); fz_put(g, s); fz_put(g, "\" . "); }
            else {
                fz_putf(g, "var v%d u64 be index \"", g->nv, 0); fz_put(g, s);
                fz_putf(g, "\" %d . ", (int)fz_rnd((proven_u32)strlen(s)), 0);
            }
            g->nv++;
            break;
        }
        case 9:           // record literal + field read
            fz_putf(g, "var q%d u64 . be make fzr do f0 ", g->nr, 0);
            fz_iexpr(g, 1); fz_put(g, " . f1 ");
            fz_iexpr(g, 1); fz_put(g, " . end . ");
            fz_putf(g, "var v%d u64 be q%d.f0 . ", g->nv, g->nr);
            g->nv++; g->nr++;
            break;
        case 10: {        // encode → view → field read (layout round-trip inline)
            int q = (int)fz_rnd((proven_u32)g->nr);
            fz_putf(g, "var s%d u64 . be encode fzw q%d . ", g->ns, q);
            fz_putf(g, "var v%d u64 be field (view fzw s%d) f", g->nv, g->ns);
            fz_putf(g, "%d . ", (int)fz_rnd(2), 0);
            g->nv++; g->ns++;
            break;
        }
        case 11: {        // vector from a string literal: load + reduce
            char s[12]; fz_str(g, s, 4);
            fz_putf(g, "var w%d vec u8 4 . be load \"", g->nw, 0); fz_put(g, s); fz_put(g, "\" 0 . ");
            fz_putf(g, "var v%d u64 be reduce_add w%d . ", g->nv, g->nw);
            g->nv++; g->nw++;
            break;
        }
        case 12: {        // D3: launder a borrow through fzpass (result aliases it)
            int m = -1;
            for (int i = 0; i < g->nb; i++) if (g->bmut[i]) m = i;
            if (m < 0 || g->nb >= 12) { fz_putf(g, "var v%d u64 be 1 . ", g->nv++, 0); return; }
            g->bmut[g->nb] = true;
            fz_putf(g, "var r%d mut_ref u64 . be fzpass r%d . ", g->nb, m);
            g->nb++;
            break;
        }
        default: break;
    }
}
static void fz_stmts(fz_t *g, int budget, int nest) {
    for (int i = 0; i < budget; i++) fz_stmt(g, nest);
}
static void fz_gen(fz_t *g, proven_u64 seed) {
    memset(g, 0, sizeof *g);
    fz_state = seed * 2654435761u + 12345u;
    fz_put(g, "struct fzr do f0 u64 . f1 u64 . end ");
    fz_put(g, "struct fzw do layout packed . f0 u32 big . f1 u16 . end ");
    fz_put(g, "fn fzpass input a mut_ref u64 . . output mut_ref u64 . . "
              " do return a . end ");
    fz_put(g, "fn fmain output u64 .  do ");
    fz_put(g, "var v0 u64 be 3 . ");
    g->nv = 1;
    fz_stmts(g, 4 + (int)fz_rnd(6), 0);
    fz_putf(g, "return v%d . end", (int)fz_rnd((proven_u32)g->nv), 0);
}

static proven_size_t count_kind(const low_lex_result_t *lex, low_tok_kind_t k) {
    proven_size_t n = 0;
    for (proven_size_t i = 0; i < lex->tokens.len; i++)
        if (PROVEN_ARRAY_GET(&lex->tokens, low_token_t, i)->kind == k) n++;
    return n;
}
static const low_token_t *tok(const low_lex_result_t *lex, proven_size_t i) {
    return PROVEN_ARRAY_GET(&lex->tokens, low_token_t, i);
}
static bool lex_is(const low_token_t *t, const char *s) {
    return proven_u8str_view_eq(t->lex, proven_u8str_view_from_cstr(s));
}

// ── 의미 엔트로피 지표: 1-edit silent acceptance rate ─────────────────────────
// Coding-theory framing (PRINCIPLES.md §0): a language detects authoring errors iff
// valid programs sit FAR APART — one token edit should land on a REJECTED
// program, not on a silently different valid one (C's `=` vs `==` is the
// canonical failure: edit distance 1, both valid, undetected).
// This is mutation testing used as a *measurement*, not as a test: we generate
// a valid program, apply exactly one single-token mutation, and ask whether the
// static checker rejects it. The rate is a tracked project metric — RFC-0052 is
// expected to move the TYPE classes sharply, and nothing else.
#define MP_MAXV 6
static const char *MP_TY[9] = { "u8", "u16", "u32", "u64", "i8", "i16", "i32", "i64", "f64" };
#define MP_FLT 8   // kind-change target
typedef struct {
    int  base;                  // program-wide scalar type (index into MP_TY)
    int  nv;
    int  vt[MP_MAXV];           // per-var declared type (baseline: all == base)
    int  lit[MP_MAXV];          // initialiser literals
    int  op[MP_MAXV];           // 0 add, 1 sub, 2 mul
    int  ret;                   // which var is returned
    bool ref_shared;            // mutated: declare the borrow `ref` instead of `mut_ref`
} mp_t;

static void mp_emit(const mp_t *m, char *out, proven_size_t cap) {
    proven_size_t o = 0;
    #define MPUT(...) o += (proven_size_t)snprintf(out + o, cap - o, __VA_ARGS__)
    MPUT("fn mmain output %s .  do ", MP_TY[m->base]);
    for (int i = 0; i < m->nv; i++)
        MPUT("var v%d %s be %d . ", i, MP_TY[m->vt[i]], m->lit[i]);
    // one borrow + write-through: the positive control (a strong checker must
    // reject `ref` + write — E-TYPE-REF)
    MPUT("var r %s %s . be mut_ref v0 . ", m->ref_shared ? "ref" : "mut_ref", MP_TY[m->vt[0]]);
    MPUT("set r %d . ", m->lit[0]);
    for (int i = 1; i < m->nv; i++)
        MPUT("set v%d expr v%d %s v%d . ", i, i,
             m->op[i] == 0 ? "+" : m->op[i] == 1 ? "-" : "*", i - 1);
    MPUT("return v%d . end", m->ret);
    #undef MPUT
}
static void mp_gen(mp_t *m, proven_u64 seed) {
    memset(m, 0, sizeof *m);
    fz_state = seed * 6364136223846793005ull + 1442695040888963407ull;
    m->base = (int)fz_rnd(8);
    m->nv = 3 + (int)fz_rnd(MP_MAXV - 2);
    for (int i = 0; i < m->nv; i++) {
        m->vt[i] = m->base;                    // baseline: one type per program
        m->lit[i] = 1 + (int)fz_rnd(30);       // small — fits every width, signed or not
        m->op[i] = (int)fz_rnd(3);
    }
    m->ret = (int)fz_rnd((proven_u32)m->nv);
}
// mutation classes. WIDTH/SIGN/KIND are what a numeric type system *for*;
// OP and LIT are negative controls (no type system can catch a value change —
// those belong to contracts/tests), REF is the positive control.
typedef enum { MC_WIDTH, MC_SIGN, MC_KIND, MC_REF, MC_OP, MC_LIT, MC_N_CLASS } mp_class_t;
static const char *MP_CLASS_NAME[MC_N_CLASS] = {
    "type width  (u8→u32)", "type sign   (u32→i32)", "type kind   (int→f64)",
    "ref kind    (mut_ref→ref)", "operator    (+→-)   [control]", "literal     (n→n+1) [control]"
};
// apply exactly one edit; returns false when this program admits no such edit
static bool mp_mutate(mp_t *m, mp_class_t c, int which) {
    int i = which % (m->nv ? m->nv : 1);
    switch (c) {
        case MC_WIDTH: {                       // same sign, different width
            int sign_base = (m->vt[i] >= 4) ? 4 : 0;
            int w = m->vt[i] - sign_base;
            m->vt[i] = sign_base + ((w + 1 + (which / 2) % 3) % 4);
            return m->vt[i] != m->base;
        }
        case MC_SIGN:                          // same width, opposite sign
            m->vt[i] = (m->vt[i] + 4) % 8;
            return true;
        case MC_KIND:                          // int → float: a kind change
            m->vt[i] = MP_FLT;
            return true;
        case MC_REF:
            if (m->ref_shared) return false;
            m->ref_shared = true;
            return true;
        case MC_OP:
            m->op[i] = (m->op[i] + 1) % 3;
            return i >= 1;                     // op[0] is unused
        case MC_LIT:
            m->lit[i] += 1;
            return true;
        default: return false;
    }
}

// ── V3-lite: bounded exhaustive model check of the agreement theorem ──────────
// (docs/proofs/lambda-lowent-core-agreement.md) — the abstract static judgment
// (S1/S2 interval-EXCL) and the abstract dynamic ledger (D1–D5 borrow stack)
// are reimplemented here INDEPENDENTLY of low_region.c / low_ir.c, and the
// implication "static-green ⇒ dyn-clean" is checked over EVERY event sequence
// within the bounds (2 locals, ≤4 tags, length ≤ MC_N). shr-writes are excluded
// from the space (the type checker forbids them — Corollary A').
// ★ 봉투는 **밖에서 넓힐 수 있다**: `-DMC_N=7` 로 다시 지으면 더 깊이 훑는다.
//   기본값이 6 인 이유는 `make test` 가 **매번** 도는 게이트이기 때문이다 — 그 자리에서
//   몇 분을 쓰면 사람이 게이트를 안 돌린다. **돌지 않는 게이트는 없는 게이트다.**
//   ⇒ 깊은 훑기는 **옵트인**이고, 잰 결과는 문서에 적는다(16장).
//
//   ★★ 그런데 **재 보니 기본을 올릴 수 있었다**(2026-07-31 실측, `make test` 전체 시간):
//         MC_N=6   920,918 시퀀스        4.3 초   ← 예전 기본
//         MC_N=7  10,490,024             5.0 초   ← **지금 기본**(봉투 11배 · 0.7 초)
//         MC_N=8 119,892,870            13.4 초   ← 옵트인 `make OPT="-O2 -DMC_N=8" test`
//         MC_N=9 1,366,571,808          99   초   ← 옵트인 (13억 시퀀스 · 위반 0)
//      전부 **위반 0**. 봉투를 11배 넓히는 값이 0.7 초라면 **안 넓힐 이유가 없다.**
//      ⇒ *"비싸서 못 한다"* 고 적기 전에 **재 봐야 한다** — 이 저장소에서 세 번째로 만나는 교훈이다.
#ifndef MC_N
#define MC_N      7
#endif
#define MC_LOCALS 2
#define MC_TAGS   4
typedef struct { proven_u8 kind, a, b; } mc_ev_t;   // 0 create(x,mut) 1 use(tag,wr) 2 own(x,wr)
static long long mc_total, mc_green, mc_viol, mc_conserv;

static void mc_check(const mc_ev_t *ev, int n) {
    proven_u8 ttgt[MC_TAGS], tmut[MC_TAGS];
    int tc[MC_TAGS], tl[MC_TAGS], nt = 0;
    for (int i = 0; i < n; i++) {
        if (ev[i].kind == 0) { ttgt[nt] = ev[i].a; tmut[nt] = ev[i].b; tc[nt] = tl[nt] = i; nt++; }
        else if (ev[i].kind == 1) { if (i > tl[ev[i].a]) tl[ev[i].a] = i; }
    }
    bool green = true;   // S1: overlapping borrows on one local, either mutable
    for (int a2 = 0; a2 < nt && green; a2++)
        for (int b2 = a2 + 1; b2 < nt && green; b2++)
            if (ttgt[a2] == ttgt[b2] && (tmut[a2] || tmut[b2]) && tc[a2] <= tl[b2] && tc[b2] <= tl[a2])
                green = false;
    for (int i = 0; i < n && green; i++)   // S2: owner access inside a live interval
        if (ev[i].kind == 2)
            for (int t = 0; t < nt && green; t++)
                if (ttgt[t] == ev[i].a && (tmut[t] || ev[i].b) && tc[t] < i && i <= tl[t])
                    green = false;
    // dynamic ledger (D1–D5)
    proven_u8 stk[MC_LOCALS][MC_TAGS];
    int sn[MC_LOCALS] = { 0 };
    bool clean = true;
    int born = 0;
    for (int i = 0; i < n && clean; i++) {
        switch (ev[i].kind) {
            case 0: { int x = ev[i].a; stk[x][sn[x]++] = (proven_u8)born++; break; }
            case 1: {
                int t = ev[i].a, x = ttgt[t], pos = -1;
                for (int k = sn[x]; k-- > 0; ) if (stk[x][k] == t) { pos = k; break; }
                if (pos < 0) { clean = false; break; }
                if (ev[i].b) sn[x] = pos + 1;                        // write: pop above
                else {                                               // read: drop muts above
                    int w = pos + 1;
                    for (int k = pos + 1; k < sn[x]; k++) if (!tmut[stk[x][k]]) stk[x][w++] = stk[x][k];
                    sn[x] = w;
                }
                break;
            }
            case 2: {
                int x = ev[i].a;
                if (ev[i].b) sn[x] = 0;                              // owner write: kill all
                else {                                               // owner read: kill muts
                    int w = 0;
                    for (int k = 0; k < sn[x]; k++) if (!tmut[stk[x][k]]) stk[x][w++] = stk[x][k];
                    sn[x] = w;
                }
                break;
            }
        }
    }
    mc_total++;
    if (green) { mc_green++; if (!clean) mc_viol++; }
    else if (clean) mc_conserv++;
}
static void mc_enum(mc_ev_t *ev, int depth, int ntags, const proven_u8 *tmut2) {
    if (depth > 0) mc_check(ev, depth);
    if (depth == MC_N) return;
    if (ntags < MC_TAGS) {
        proven_u8 muts[MC_TAGS];
        memcpy(muts, tmut2, MC_TAGS);
        for (int x = 0; x < MC_LOCALS; x++)
            for (int m = 0; m < 2; m++) {
                ev[depth] = (mc_ev_t){ 0, (proven_u8)x, (proven_u8)m };
                muts[ntags] = (proven_u8)m;
                mc_enum(ev, depth + 1, ntags + 1, muts);
            }
    }
    for (int t = 0; t < ntags; t++) {
        ev[depth] = (mc_ev_t){ 1, (proven_u8)t, 0 };
        mc_enum(ev, depth + 1, ntags, tmut2);
        if (tmut2[t]) {   // writes only through mut borrows (Corollary A')
            ev[depth] = (mc_ev_t){ 1, (proven_u8)t, 1 };
            mc_enum(ev, depth + 1, ntags, tmut2);
        }
    }
    for (int x = 0; x < MC_LOCALS; x++)
        for (int w = 0; w < 2; w++) {
            ev[depth] = (mc_ev_t){ 2, (proven_u8)x, (proven_u8)w };
            mc_enum(ev, depth + 1, ntags, tmut2);
        }
}

// ── V3-lite part 2: Lemma B (loop span) — prefix + loop[body], unrolled k=1..3 ──
// Static: S1/S2 on the single-occurrence view + S2L (a borrow crossing into the
// loop conflicts with any owner access inside, order-free). Dynamic: the body is
// replayed k times; body-created tags get FRESH instances per iteration (the
// generator/VM behavior). Checks every combination within the bounds.
#define MC2_P 2
#define MC2_B 3
static long long mc2_total, mc2_green, mc2_viol;

static bool mc2_static(const mc_ev_t *ev, int np, int nb) {
    int n = np + nb, nt = 0;
    proven_u8 ttgt[MC_TAGS], tmut[MC_TAGS];
    int tc[MC_TAGS], tl[MC_TAGS];
    for (int i = 0; i < n; i++) {
        if (ev[i].kind == 0) { ttgt[nt] = ev[i].a; tmut[nt] = ev[i].b; tc[nt] = tl[nt] = i; nt++; }
        else if (ev[i].kind == 1) { if (i > tl[ev[i].a]) tl[ev[i].a] = i; }
    }
    for (int a2 = 0; a2 < nt; a2++)
        for (int b2 = a2 + 1; b2 < nt; b2++)
            if (ttgt[a2] == ttgt[b2] && (tmut[a2] || tmut[b2]) && tc[a2] <= tl[b2] && tc[b2] <= tl[a2])
                return false;
    for (int i = 0; i < n; i++) {
        if (ev[i].kind != 2) continue;
        for (int t = 0; t < nt; t++) {
            if (ttgt[t] != ev[i].a || !(tmut[t] || ev[i].b)) continue;
            bool crossing = i >= np && tc[t] < np && tl[t] >= np;   // S2L: borrow spans into the loop
            bool inside = tc[t] < i && i <= tl[t];
            if (crossing || inside) return false;
        }
    }
    return true;
}
static bool mc2_dyn(const mc_ev_t *ev, int np, int nb, int k) {
    proven_u8 ttgt[MC_TAGS];
    proven_u8 tmut[MC_TAGS];
    int nt = 0;
    for (int i = 0; i < np + nb; i++)
        if (ev[i].kind == 0) { ttgt[nt] = ev[i].a; tmut[nt] = ev[i].b; nt++; }
    int inst[MC_TAGS];                         // static tag → current dynamic instance
    proven_u8 imut[32];
    int stk[MC_LOCALS][32], sn[MC_LOCALS] = { 0 }, born = 0;
    for (int t = 0; t < MC_TAGS; t++) inst[t] = -1;
    int steps[MC2_P + MC2_B * 3][2];          // (event idx) replay plan — prefix + k회 전개
    int nsteps = 0;
    for (int i = 0; i < np; i++) steps[nsteps][0] = i, steps[nsteps++][1] = 0;
    for (int it = 0; it < k; it++)
        for (int i = np; i < np + nb; i++) steps[nsteps][0] = i, steps[nsteps++][1] = it;
    int tseen = 0;
    int first_of[MC_TAGS];                     // static tag order → tag id at creation
    for (int i = 0, c = 0; i < np + nb; i++) if (ev[i].kind == 0) first_of[c++] = i;
    (void)first_of; (void)tseen;
    for (int s = 0; s < nsteps; s++) {
        const mc_ev_t *e = &ev[steps[s][0]];
        switch (e->kind) {
            case 0: {   // fresh instance per (re)execution
                int t = 0;
                for (int i = 0; i < steps[s][0]; i++) if (ev[i].kind == 0) t++;
                if (born >= 32 || sn[e->a] >= 32) return true;   // bound guard: skip oversized
                inst[t] = born;
                imut[born] = e->b;
                stk[e->a][sn[e->a]++] = born++;
                break;
            }
            case 1: {
                int t = e->a;
                if (inst[t] < 0) return true;   // use before create in replay — skip (not a real program)
                int x = ttgt[t], pos = -1;
                for (int q = sn[x]; q-- > 0; ) if (stk[x][q] == inst[t]) { pos = q; break; }
                if (pos < 0) return false;      // ⚡
                if (e->b) sn[x] = pos + 1;
                else {
                    int w = pos + 1;
                    for (int q = pos + 1; q < sn[x]; q++) if (!imut[stk[x][q]]) stk[x][w++] = stk[x][q];
                    sn[x] = w;
                }
                break;
            }
            case 2: {
                int x = e->a;
                if (e->b) sn[x] = 0;
                else {
                    int w = 0;
                    for (int q = 0; q < sn[x]; q++) if (!imut[stk[x][q]]) stk[x][w++] = stk[x][q];
                    sn[x] = w;
                }
                break;
            }
        }
    }
    (void)tmut;
    return true;
}
static void mc2_run(mc_ev_t *ev, int depth, int np, int nb, int ntags, const proven_u8 *tmut2) {
    if (depth == np + nb) {
        mc2_total++;
        if (mc2_static(ev, np, nb)) {
            mc2_green++;
            for (int k = 1; k <= 3; k++)
                if (!mc2_dyn(ev, np, nb, k)) { mc2_viol++; return; }
        }
        return;
    }
    if (ntags < MC_TAGS) {
        proven_u8 muts[MC_TAGS];
        memcpy(muts, tmut2, MC_TAGS);
        for (int x = 0; x < MC_LOCALS; x++)
            for (int m = 0; m < 2; m++) {
                ev[depth] = (mc_ev_t){ 0, (proven_u8)x, (proven_u8)m };
                muts[ntags] = (proven_u8)m;
                mc2_run(ev, depth + 1, np, nb, ntags + 1, muts);
            }
    }
    for (int t = 0; t < ntags; t++) {
        ev[depth] = (mc_ev_t){ 1, (proven_u8)t, 0 };
        mc2_run(ev, depth + 1, np, nb, ntags, tmut2);
        if (tmut2[t]) {   // writes only through mut borrows (Corollary A')
            ev[depth] = (mc_ev_t){ 1, (proven_u8)t, 1 };
            mc2_run(ev, depth + 1, np, nb, ntags, tmut2);
        }
    }
    for (int x = 0; x < MC_LOCALS; x++)
        for (int w = 0; w < 2; w++) {
            ev[depth] = (mc_ev_t){ 2, (proven_u8)x, (proven_u8)w };
            mc2_run(ev, depth + 1, np, nb, ntags, tmut2);
        }
}

// ── V3-lite part 3: path-sensitive rules (D4 fix) — exhaustive over if-shapes ──
// Program shape: prefix (≤2 events) ; if { armA (≤2) } else { armB (≤2) }.
// Static: S1'/S2' with path compatibility (sibling arms never conflict).
// Dynamic: BOTH executions (arm A taken, arm B taken) must be ⚡-free.
#define MC3_MAX 2
static long long mc3_total, mc3_green, mc3_viol;

static bool mc3_run_path(const mc_ev_t *ev, const int *arm_of, int n, int taken) {
    proven_u8 ttgt[MC_TAGS], tmut[MC_TAGS];
    int nt = 0;
    for (int i = 0; i < n; i++)
        if (ev[i].kind == 0) { ttgt[nt] = ev[i].a; tmut[nt] = ev[i].b; nt++; }
    int stk[MC_LOCALS][MC_TAGS], sn[MC_LOCALS] = { 0 };
    for (int i = 0, t = 0; i < n; i++) {
        if (arm_of[i] >= 0 && arm_of[i] != taken) { if (ev[i].kind == 0) t++; continue; }
        switch (ev[i].kind) {
            case 0: stk[ev[i].a][sn[ev[i].a]++] = t++; break;
            case 1: {
                int tg = ev[i].a, x = ttgt[tg], pos = -1;
                for (int k = sn[x]; k-- > 0; ) if (stk[x][k] == tg) { pos = k; break; }
                if (pos < 0) return false;                        // ⚡ (or unreachable use)
                if (ev[i].b) sn[x] = pos + 1;
                else { int w = pos + 1;
                       for (int k = pos + 1; k < sn[x]; k++) if (!tmut[stk[x][k]]) stk[x][w++] = stk[x][k];
                       sn[x] = w; }
                break;
            }
            case 2: {
                int x = ev[i].a;
                if (ev[i].b) sn[x] = 0;
                else { int w = 0;
                       for (int k = 0; k < sn[x]; k++) if (!tmut[stk[x][k]]) stk[x][w++] = stk[x][k];
                       sn[x] = w; }
                break;
            }
        }
    }
    return true;
}
// a use whose tag was created in the *other* arm can never execute — such shapes
// are not real programs (the binding would be out of scope); skip them.
static bool mc3_wellformed(const mc_ev_t *ev, const int *arm_of, int n) {
    int arm_of_tag[MC_TAGS], nt = 0;
    for (int i = 0; i < n; i++) {
        if (ev[i].kind == 0) { arm_of_tag[nt++] = arm_of[i]; continue; }
        if (ev[i].kind == 1) {
            int t = ev[i].a;
            if (t >= nt) return false;                      // use before create
            int at = arm_of_tag[t];
            if (at >= 0 && at != arm_of[i]) return false;   // cross-arm use
        }
    }
    return true;
}
static void mc3_check(const mc_ev_t *ev, const int *arm_of, int n) {
    proven_u8 ttgt[MC_TAGS], tmut[MC_TAGS];
    int tc[MC_TAGS], tl[MC_TAGS], tarm[MC_TAGS], nt = 0;
    for (int i = 0; i < n; i++) {
        if (ev[i].kind == 0) { ttgt[nt] = ev[i].a; tmut[nt] = ev[i].b; tarm[nt] = arm_of[i]; tc[nt] = tl[nt] = i; nt++; }
        else if (ev[i].kind == 1) { if (i > tl[ev[i].a]) tl[ev[i].a] = i; }
    }
    #define MC3_COMPAT(x, y) ((x) < 0 || (y) < 0 || (x) == (y))
    bool green = true;
    for (int a2 = 0; a2 < nt && green; a2++)          // S1': older use at/after newer create
        for (int b2 = a2 + 1; b2 < nt && green; b2++) {
            if (ttgt[a2] != ttgt[b2] || !(tmut[a2] || tmut[b2])) continue;
            for (int i = 0; i < n && green; i++)
                if (ev[i].kind == 1 && ev[i].a == a2 && i >= tc[b2] && MC3_COMPAT(arm_of[i], tarm[b2]))
                    green = false;
        }
    for (int i = 0; i < n && green; i++) {           // S2': owner access before a co-occurring use
        if (ev[i].kind != 2) continue;
        for (int t = 0; t < nt && green; t++) {
            if (ttgt[t] != ev[i].a || !(tmut[t] || ev[i].b) || tc[t] >= i) continue;
            for (int u = i; u < n && green; u++)
                if (ev[u].kind == 1 && ev[u].a == t && MC3_COMPAT(arm_of[u], arm_of[i]))
                    green = false;
        }
    }
    #undef MC3_COMPAT
    mc3_total++;
    if (!green) return;
    mc3_green++;
    if (!mc3_run_path(ev, arm_of, n, 0) || !mc3_run_path(ev, arm_of, n, 1)) mc3_viol++;
}
static void mc3_enum(mc_ev_t *ev, int *arm_of, int depth, int n, const int *arms,
                     int ntags, const proven_u8 *tmut2) {
    if (depth == n) {
        if (mc3_wellformed(ev, arm_of, n)) mc3_check(ev, arm_of, n);
        return;
    }
    arm_of[depth] = arms[depth];
    if (ntags < MC_TAGS) {
        proven_u8 muts[MC_TAGS];
        memcpy(muts, tmut2, MC_TAGS);
        for (int x = 0; x < MC_LOCALS; x++)
            for (int m = 0; m < 2; m++) {
                ev[depth] = (mc_ev_t){ 0, (proven_u8)x, (proven_u8)m };
                muts[ntags] = (proven_u8)m;
                mc3_enum(ev, arm_of, depth + 1, n, arms, ntags + 1, muts);
            }
    }
    for (int t = 0; t < ntags; t++) {
        ev[depth] = (mc_ev_t){ 1, (proven_u8)t, 0 };
        mc3_enum(ev, arm_of, depth + 1, n, arms, ntags, tmut2);
        if (tmut2[t]) {
            ev[depth] = (mc_ev_t){ 1, (proven_u8)t, 1 };
            mc3_enum(ev, arm_of, depth + 1, n, arms, ntags, tmut2);
        }
    }
    for (int x = 0; x < MC_LOCALS; x++)
        for (int w = 0; w < 2; w++) {
            ev[depth] = (mc_ev_t){ 2, (proven_u8)x, (proven_u8)w };
            mc3_enum(ev, arm_of, depth + 1, n, arms, ntags, tmut2);
        }
}

int main(void) {
    proven_allocator_t heap = proven_heap_allocator();
    void *amem = malloc(1u << 20);
    proven_arena_t arena = proven_arena_create((proven_mem_mut_t){ .ptr = amem, .size = 1u << 20 });
    proven_allocator_t nodes = proven_arena_as_allocator(&arena);

    #define LEX(s) low_lex(heap, (proven_u8str_view_t){ .ptr = (const proven_byte_t *)(s), .size = strlen(s) })

    printf("lexer:\n");
    {
        low_lex_result_t l = LEX("let x 5 .");
        check(l.ok, "let x 5 . lexes clean");
        check(count_kind(&l, LOW_TOK_DOT) == 1, "one DOT closer");
        check(count_kind(&l, LOW_TOK_NUMBER) == 1, "one NUMBER");
        check(lex_is(tok(&l, 0), "let") && tok(&l, 0)->kw == LOW_KW_LET, "let is keyword");
        proven_array_destroy(&l.tokens); proven_array_destroy(&l.diags);
    }
    {
        low_lex_result_t l = LEX("net.http.get  1_000_000 0xFF 0b1010 2.5e-3");
        check(l.ok && lex_is(tok(&l, 0), "net.http.get"), "qualified glued-dot name");
        check(count_kind(&l, LOW_TOK_NUMBER) == 4, "dec/hex/bin/float = 4 NUMBERs");
        check(count_kind(&l, LOW_TOK_DOT) == 0, "no isolated DOT (all glued)");
        proven_array_destroy(&l.tokens); proven_array_destroy(&l.diags);
    }
    {
        low_lex_result_t l = LEX("a .. b");
        check(!l.ok && l.diags.len == 1, "'..' is a lexical error");
        proven_array_destroy(&l.tokens); proven_array_destroy(&l.diags);
    }
    {
        low_lex_result_t l = LEX("let s text END\nhi \"q\" {x}\nEND .");
        check(l.ok && count_kind(&l, LOW_TOK_HEREDOC) == 1, "heredoc body captured");
        check(count_kind(&l, LOW_TOK_DOT) == 1, "trailing closer after END");
        proven_array_destroy(&l.tokens); proven_array_destroy(&l.diags);
    }

    printf("parser:\n");
    #define PARSE(s) ({ low_lex_result_t _l = LEX(s); \
        proven_arena_reset(&arena); \
        low_parse_result_t _p = low_parse(nodes, heap, &_l.tokens); \
        proven_array_destroy(&_l.tokens); proven_array_destroy(&_l.diags); _p; })

    {
        // pure reader: value 'add a b' stays FLAT at L1 → let(x, add, a, b); the
        // evaluator brackets it with Γ. Parens bracket explicitly.
        low_parse_result_t p = PARSE("let x add a b .");
        check(p.ok && p.nforms == 1, "one top form");
        check(p.nforms == 1 && p.forms[0]->nkids == 5, "let/x/add/a/b flat (evaluator brackets)");
        proven_array_destroy(&p.diags); if (p.forms) heap.free_fn(heap.ctx, p.forms);
    }
    {
        low_parse_result_t p = PARSE("let x (add a b) .");
        low_cst_t *val = (p.nforms && p.forms[0]->nkids == 3) ? p.forms[0]->kids[2] : NULL;
        check(val && val->kind == LOW_CST_GROUP, "parens bracket value: let/x/(group) = 3 kids");
        proven_array_destroy(&p.diags); if (p.forms) heap.free_fn(heap.ctx, p.forms);
    }
    {
        // ★★ 중위 접근(`a to b` / `b in a`)은 **없앴다** — `field a b` 와 **같은 뜻**이었다.
        //   한 뜻에 네 철자가 있었고(전위·정방향·역방향·붙임 점), **이미 갈려 있었다**:
        //   붙임 점만 **인덱스를 못 했다**. 이제 붙임 점이 인덱스도 하고(`arr.3`),
        //   `to`/`in` 은 어휘에서 사라졌다.
        low_parse_result_t p = PARSE("let px win.pos.x .");
        low_cst_t *f = p.nforms ? p.forms[0] : NULL;
        check(p.ok && f && f->nkids == 3 && f->kids[2]->kind == LOW_CST_ATOM,
              "glued access `win.pos.x` is ONE identifier token (no infix operator)");
        proven_array_destroy(&p.diags); if (p.forms) heap.free_fn(heap.ctx, p.forms);
    }
    {
        low_parse_result_t p = PARSE("let e arr.3 .");
        low_cst_t *f = p.nforms ? p.forms[0] : NULL;
        check(p.ok && f && f->nkids == 3 && f->kids[2]->kind == LOW_CST_ATOM,
              "glued INDEX `arr.3` lexes as one token — it used to be `arr` + a CLOSING dot + `3`");
        proven_array_destroy(&p.diags); if (p.forms) heap.free_fn(heap.ctx, p.forms);
    }
    // ★ 여기 R3(쉼표) 시험 둘이 있었다 — `print a, mul b c, d .` 가 인자 셋으로 갈리는가.
    //   RFC-0103(2026-08-27)에서 `,` 를 없앴으므로 함께 없앤다. 대신 **없앤 낱말이 오류로
    //   말하는지**를 묻는다: 조용히 무시되면 그것이 함정이다(`;` 와 같은 처방).
    {
        low_lex_result_t l = LEX("print a, b .");
        bool said = false;
        for (proven_size_t i = 0; i < l.diags.len; i++) {
            const low_diag_t *d = (const low_diag_t *)proven_array_get(&l.diags, i);
            if (d->code && strcmp(d->code, "E-VOCAB-REMOVED") == 0) said = true;
        }
        check(said, "`,` (R3) is REMOVED and the lexer SAYS SO — a word that parses but does nothing is a silent trap");
        proven_array_destroy(&l.diags); proven_array_destroy(&l.tokens);
    }
    {
        low_parse_result_t p = PARSE("proc f a b do return a . end");
        low_cst_t *f = p.nforms ? p.forms[0] : NULL;
        low_cst_t *blk = (f && f->nkids) ? f->kids[f->nkids - 1] : NULL;
        check(blk && blk->kind == LOW_CST_BLOCK, "op body block binds to op, not last param");
        proven_array_destroy(&p.diags); if (p.forms) heap.free_fn(heap.ctx, p.forms);
    }
    {
        low_parse_result_t p = PARSE("let w record do x 1 . end");
        low_cst_t *val = (p.nforms && p.forms[0]->nkids == 3) ? p.forms[0]->kids[2] : NULL;
        check(val && val->kind == LOW_CST_FORM && val->closer == LOW_TOK_EOF, "headed record block, end closer");
        proven_array_destroy(&p.diags); if (p.forms) heap.free_fn(heap.ctx, p.forms);
    }
    {
        low_parse_result_t p = PARSE("for i range 0 10 do print i . end");
        low_cst_t *f = p.nforms ? p.forms[0] : NULL;
        bool acc = false;
        if (f) for (proven_size_t i = 0; i < f->nkids; i++) if (f->kids[i]->kind == LOW_CST_ACCESS) acc = true;
        check(f && f->nkids == 6, "for: [for,i,range,0,10,block] (in-marker dropped)");
        check(!acc, "for-marker 'in' is NOT reverse-access");
        check(f && f->kids[f->nkids - 1]->kind == LOW_CST_BLOCK, "for body is a block");
        proven_array_destroy(&p.diags); if (p.forms) heap.free_fn(heap.ctx, p.forms);
    }
    {
        low_parse_result_t p = PARSE("if lt a b do print x . end else do print y . end");
        low_cst_t *f = p.nforms ? p.forms[0] : NULL;
        int blocks = 0;
        if (f) for (proven_size_t i = 0; i < f->nkids; i++) if (f->kids[i]->kind == LOW_CST_BLOCK) blocks++;
        check(f && blocks == 2, "if/else = two blocks");
        proven_array_destroy(&p.diags); if (p.forms) heap.free_fn(heap.ctx, p.forms);
    }
    {
        low_parse_result_t p = PARSE("let r add (mul a b) (sub c d) .");
        low_cst_t *f = p.nforms ? p.forms[0] : NULL;
        check(f && f->nkids == 5 && f->kids[3]->kind == LOW_CST_GROUP && f->kids[4]->kind == LOW_CST_GROUP,
              "nested parens: let/r/add/(grp)/(grp)");
        proven_array_destroy(&p.diags); if (p.forms) heap.free_fn(heap.ctx, p.forms);
    }
    { low_lex_result_t l = LEX("");  low_parse_result_t p = { .ok = true };
      proven_arena_reset(&arena); p = low_parse(nodes, heap, &l.tokens);
      check(p.ok && p.nforms == 0, "empty input parses to zero forms");
      proven_array_destroy(&l.tokens); proven_array_destroy(&l.diags);
      proven_array_destroy(&p.diags); if (p.forms) heap.free_fn(heap.ctx, p.forms); }

    // ── S3: MVP front-end grows additively (same parser, +schema rows) ──
    {
        low_parse_result_t p = PARSE("fn f input x u32 . . output bool . do return true . end");
        low_cst_t *f = p.nforms ? p.forms[0] : NULL;
        check(p.ok && f && f->kids[0]->tok.kw == LOW_KW_FN, "MVP fn parses, no errors");
        check(f && f->kids[f->nkids - 1]->kind == LOW_CST_BLOCK, "fn body binds to fn (not last clause)");
        proven_array_destroy(&p.diags); if (p.forms) heap.free_fn(heap.ctx, p.forms);
    }
    {
        // ★ `make` 는 **값**이지 문장이 아니다 ⇒ 일반 form 으로 읽는다:
        //   ATOM(make) · FORM(point, BLOCK)   ← `point do … end` 가 머리 붙은 블록
        //   그래서 **괄호 안팎이 같은 나무**다. 전엔 `make` 가 블록-문장 머리 목록에 있어서
        //   괄호 안에서만 `FORM(make, point, BLOCK)` 이 됐고, IR 이 두 모양을 다 받도록
        //   **기워져** 있었다 — 뒷단의 반창고는 대개 **앞단의 병**이다.
        low_parse_result_t p = PARSE("make point do x 1 . y 2 . end");
        low_cst_t *f = p.nforms ? p.forms[0] : NULL;
        check(p.ok && f && f->nkids == 2 && f->kids[0]->tok.kw == LOW_KW_MAKE &&
              f->kids[1]->kind == LOW_CST_FORM &&
              f->kids[1]->kids[f->kids[1]->nkids - 1]->kind == LOW_CST_BLOCK,
              "MVP make aggregate parses — ONE shape, inside parens and out");
        proven_array_destroy(&p.diags); if (p.forms) heap.free_fn(heap.ctx, p.forms);
    }
    {
        low_parse_result_t p = PARSE("while lt i n . do set i (add i 1) . end");
        low_cst_t *f = p.nforms ? p.forms[0] : NULL;
        check(p.ok && f && f->kids[0]->tok.kw == LOW_KW_WHILE &&
              f->kids[f->nkids - 1]->kind == LOW_CST_BLOCK, "MVP while binds body (cond '.' skipped)");
        proven_array_destroy(&p.diags); if (p.forms) heap.free_fn(heap.ctx, p.forms);
    }
    {
        // normative struct/enum: `do … end` (2026-09-15 owner decision — a block declaration opens with `do`)
        low_parse_result_t p = PARSE("struct header do version u8 . flags u8 . end");
        low_cst_t *f = p.nforms ? p.forms[0] : NULL;
        check(p.ok && f && f->kids[0]->tok.kw == LOW_KW_STRUCT &&
              f->kids[f->nkids - 1]->kind == LOW_CST_BLOCK, "MVP struct `do … end` parses (normative)");
        proven_array_destroy(&p.diags); if (p.forms) heap.free_fn(heap.ctx, p.forms);
    }
    {
        low_parse_result_t p = PARSE("enum parse_error do too_short . bad_version . end");
        low_cst_t *f = p.nforms ? p.forms[0] : NULL;
        check(p.ok && f && f->kids[0]->tok.kw == LOW_KW_ENUM &&
              f->kids[f->nkids - 1]->kind == LOW_CST_BLOCK, "MVP enum `do … end` parses (normative)");
        proven_array_destroy(&p.diags); if (p.forms) heap.free_fn(heap.ctx, p.forms);
    }
    {
        // ★ 2026-09-15 — the other spellings are refused: `struct N .` and a bare line break (a newline closes nothing)
        low_parse_result_t p = PARSE("struct header . version u8 . end");
        bool nd1 = false; for (proven_size_t z = 0; z < p.diags.len; z++) if (!strcmp(PROVEN_ARRAY_GET(&p.diags, low_diag_t, z)->code, "E-STMT-NODO")) nd1 = true;
        check(!p.ok && nd1, "block declaration without `do` → E-STMT-NODO (struct N .)");
        proven_array_destroy(&p.diags); if (p.forms) heap.free_fn(heap.ctx, p.forms);
        low_parse_result_t q = PARSE("trait shape\n area input s self .\n output u64 . end");
        bool nd2 = false; for (proven_size_t z = 0; z < q.diags.len; z++) if (!strcmp(PROVEN_ARRAY_GET(&q.diags, low_diag_t, z)->code, "E-STMT-NODO")) nd2 = true;
        check(!q.ok && nd2, "block declaration without `do` → E-STMT-NODO (trait N newline)");
        proven_array_destroy(&q.diags); if (q.forms) heap.free_fn(heap.ctx, q.forms);
        low_parse_result_t r3 = PARSE("trait shape do fn area input s self . output u64 . end");
        bool ts = false; for (proven_size_t z = 0; z < r3.diags.len; z++) if (!strcmp(PROVEN_ARRAY_GET(&r3.diags, low_diag_t, z)->code, "E-TRAIT-SIG")) ts = true;
        check(!r3.ok && ts, "trait signature with `fn` → E-TRAIT-SIG (the effects line decides, not a keyword)");
        proven_array_destroy(&r3.diags); if (r3.forms) heap.free_fn(heap.ctx, r3.forms);
    }
    {
        // for-iterable may be a '.'-closed form before 'do' (SPEC-015 §17.2)
        low_parse_result_t p = PARSE("for child children_of g node . do push work child . end");
        low_cst_t *f = p.nforms ? p.forms[0] : NULL;
        check(p.ok && f && f->kids[0]->tok.kw == LOW_KW_FOR &&
              f->kids[f->nkids - 1]->kind == LOW_CST_BLOCK, "MVP for with closed-form iterable parses");
        proven_array_destroy(&p.diags); if (p.forms) heap.free_fn(heap.ctx, p.forms);
    }
    {
        low_parse_result_t p = PARSE("struct pt do x i32 . y i32 . end");
        check(p.ok && p.nforms == 1 && p.forms[0]->kids[0]->tok.kw == LOW_KW_STRUCT, "MVP struct parses do");
        proven_array_destroy(&p.diags); if (p.forms) heap.free_fn(heap.ctx, p.forms);
    }

    // ── ★★ 옛 트리워킹 인터프리터의 테스트 75개가 여기 있었다. 지웠다.
    //   그것들이 테스트하던 것은 **명세에 없는 두 번째 언어**였다:
    //     RUN("let x 10 . expr x - 3 .")            ← `be` 가 없다
    //     RUN("proc d x do … end let m (map …) .") ← map · list · concat · to_string
    //   SPEC-002 어휘 목록에 하나도 없는 낱말들이다. 초록불 75개가 **아무도 안 쓰는 언어**가
    //   도는지를 확인하고 있었다 — 그리고 그 언어를 살려 두느라 `fn`·`as`·`fail`·`give`·`unit`
    //   여섯 낱말이 어휘에 남아 있었다.
    //   ★ **테스트 수가 줄어드는 것이 정직한 결과다.** 늘 초록이던 75개는 아무것도 지키지
    //     않고 있었다. 진짜 언어는 골든(312) · 차등 퍼저 · 계약 오라클이 지킨다.

    // ── S4: MVP static effect discipline ──
    printf("check:\n");
    // 경고 개수 — "조용히 무시" 와 "고지" 를 구별해서 검사한다.
    #define CHECK_WARNS(s) ({ low_lex_result_t _l = LEX(s); proven_arena_reset(&arena); \
        low_parse_result_t _p = low_parse(nodes, heap, &_l.tokens); \
        low_check_result_t _c = low_check(heap, &_p); int _n = 0; \
        for (proven_size_t _i = 0; _i < _c.diags.len; _i++) { \
            const low_diag_t *_d = (const low_diag_t *)proven_array_get(&_c.diags, _i); \
            if (_d->sev == LOW_SEV_WARNING) _n++; } \
        proven_array_destroy(&_c.diags); if (_p.forms) heap.free_fn(heap.ctx, _p.forms); \
        proven_array_destroy(&_p.diags); proven_array_destroy(&_l.tokens); proven_array_destroy(&_l.diags); _n; })
    #define CHECK(s) ({ low_lex_result_t _l = LEX(s); proven_arena_reset(&arena); \
        low_parse_result_t _p = low_parse(nodes, heap, &_l.tokens); \
        low_check_result_t _c = low_check(heap, &_p); bool _ok = _c.ok; \
        proven_array_destroy(&_c.diags); if (_p.forms) heap.free_fn(heap.ctx, _p.forms); \
        proven_array_destroy(&_p.diags); proven_array_destroy(&_l.tokens); proven_array_destroy(&_l.diags); _ok; })

    check(CHECK("fn f  do return 1 . end") == true,  "pure fn with no effects: ok");
    // ★★★ **이 시험은 죽은 표에 기대고 있었다** (2026-08-31, 소유자 결정으로 표를 지웠다).
    //   `print` 는 op 이 아닌데(`E-IR-UNDEF`) 효과 추론표에 **없는 이름 일곱**이 io 로 적혀
    //   있어서 이 줄이 그 표 덕에 초록이었다. 표를 지우니 빨개졌다 — **시험이 잡던 것은
    //   언어가 아니라 그 표였다.** 실제 op 으로 다시 쓴다.
    //   ☞ *죽은 코드를 지울 때 무엇이 빨개지는지 보면, 그 코드가 무엇을 떠받치고 있었는지
    //     알 수 있다. 시험이 빨개지면 시험 쪽을 먼저 의심한다.*
    check(CHECK("fn f input k cap io . do var n u64 be write_out k \"x\" . return . end") == false,
          "fn doing io (write_out) → rejected");
    // ★ 2026-07-24: `effects io` 는 이제 그것을 **인가하는 cap** 을 요구한다(E-EFFECT-NO-CAP,
    //   RFC-0007 §6.7) — io 는 손에 쥐여 주는 **권리**이지 주변 권능이 아니다. 그리고 `print`
    //   는 op 이 아니다(정의 없는 이름 → E-IR-UNDEF); 실제 출력은 `write_out <cap> <바이트>` 다.
    //   이 테스트는 그 둘이 착지하기 전에 쓰여 **낡아 있었다**(그 사이 유닛 게이트가 빨간불이었다).
    check(CHECK("proc g input k cap io . effects io . do var n u64 be write_out k \"x\" . return . end") == true,
          "proc doing io: ok (declared + cap io that authorizes it)");
    check(CHECK("proc p input k cap io . effects io . do var n u64 be write_out k \"x\" . return . end  fn c input k cap io . do p k . end") == false,
          "fn calling a proc → rejected (transitive effect)");
    check(CHECK("fn a  do return add 1 2 . end") == true, "fn calling a pure builtin: ok");

    // type checking (kind-level)
    #define TYCK(s) ({ low_lex_result_t _l = LEX(s); proven_arena_reset(&arena); \
        low_parse_result_t _p = low_parse(nodes, heap, &_l.tokens); \
        low_typecheck_result_t _t = low_typecheck(heap, &_p); bool _ok = _t.ok; \
        proven_array_destroy(&_t.diags); if (_p.forms) heap.free_fn(heap.ctx, _p.forms); \
        proven_array_destroy(&_p.diags); proven_array_destroy(&_l.tokens); proven_array_destroy(&_l.diags); _ok; })

    check(TYCK("fn f input x u32 . output u32 . do return x . end") == true, "return matches output type: ok");
    check(TYCK("fn f input x u32 . output bool . do return x . end") == false, "return u32 vs output bool → rejected");
    check(TYCK("fn f output u32 . do var i u32 be true . return i . end") == false, "var u32 := bool → rejected");
    check(TYCK("fn f output u32 . do var i u32 be 5 . return i . end") == true, "var u32 := int literal: ok");
    check(TYCK("fn g input p u32 . output u32 . do return p . end  fn f output u32 . do return g true . end") == false,
          "call arg bool vs param u32 → rejected");
    check(TYCK("fn f input x f64 . output bool . do return lt x 3 . end") == true, "compare → bool matches output: ok");

    // type checking (integer width / signedness)
    check(TYCK("fn f output u8 . do var i u8 be 200 . return i . end") == true,  "u8 := 200 literal fits: ok");
    check(TYCK("fn f output u8 . do var i u8 be 300 . return i . end") == false, "u8 := 300 literal → E-TYPE-WIDTH");
    check(TYCK("fn f output u8 . do var i u8 be 5 . set i 300 . return i . end") == false,
          "set u8 := 300 (markerless set, RFC-0049) → E-TYPE-WIDTH");
    check(TYCK("fn f input x u8 . output u32 . do return x . end") == true,  "u8 → u32 implicit widening: ok");
    check(TYCK("fn f input x u32 . output u8 . do return x . end") == false, "u32 → u8 implicit narrowing → E-TYPE-WIDTH");
    check(TYCK("fn f input x u32 . output u8 . do return cast u8 x . end") == true, "narrowing via explicit cast: ok");
    check(TYCK("fn f input x i32 . output u32 . do return x . end") == false, "i32 → u32 sign mismatch → E-TYPE-SIGN");
    // RFC-0052 D11: usize/isize are DISTINCT types, not u64/i64 aliases — a 32-bit
    // target would silently break code that mixes them.
    check(TYCK("fn f input x u64 . output usize . do return x . end") == false,
          "u64 → usize is rejected: usize is a distinct nominal type (D11)");
    check(TYCK("fn f input x usize . output usize . do return x . end") == true,
          "usize → usize: ok");

    // ── RFC-0052 D1/D2: 안전 확대 격자 ⊑ (Coq: NumericLattice.v) ──
    check(TYCK("fn f input a u8 . input b u16 . output u16 . do return add a b . end") == true,
          "⊑: u8 + u16 → u16 (value-preserving widening — join is an operand)");
    check(TYCK("fn f input a u8 . input b i16 . output i16 . do return add a b . end") == true,
          "⊑: u8 + i16 → i16 (unsigned into strictly wider signed is safe)");
    check(TYCK("fn f input a u8 . input b i8 . output i16 . do return add a b . end") == false,
          "⊑: u8 + i8 → E-TYPE-SIGN (neither contains the other; no invented join)");
    check(TYCK("fn f input a u32 . input b i32 . output i32 . do return add a b . end") == false,
          "⊑: u32 + i32 → E-TYPE-SIGN (Coq: u32_i32_incomparable)");
    check(TYCK("fn f input a u8 . input b f64 . output f64 . do return add a b . end") == false,
          "D8: int + float → E-TYPE-MIX (kind change is never implicit)");
    check(TYCK("fn f input a f32 . input b f64 . output f64 . do return add a b . end") == true,
          "⊑: f32 + f64 → f64 (float widening is value-preserving)");
    check(TYCK("fn f input a u8 . input b u16 . output u16 . do return expr a + b . . end") == true,
          "⊑ inside `expr`: the island no longer erases width/sign");
    check(TYCK("fn f input a u8 . input b i8 . output i16 . do return expr a + b . . end") == false,
          "⊑ inside `expr`: incomparable operands are caught (was silent before)");
    check(TYCK("fn f input a u32 . input b u8 . output u32 . do return lt a b . end") == false,
          "compare returns bool, not u32 (kind mismatch at the boundary)");
    check(TYCK("fn f input a u8 . output u8 . do return add a 3 . end") == true,
          "D3: an untyped literal adopts the other operand's type");
    check(TYCK("fn f input a u8 . output u8 . do return add a 300 . end") == false,
          "D3: a literal that does not fit the operand type → E-TYPE-WIDTH");
    check(TYCK("fn f input x f64 . output f32 . do return x . end") == false, "f64 → f32 narrowing → E-TYPE-WIDTH");
    check(TYCK("fn f output u16 . do var i u16 be 0xFFFF . return i . end") == true, "u16 := 0xFFFF hex fits: ok");
    check(TYCK("fn f output i16 . do var i i16 be 0xFFFF . return i . end") == false, "i16 := 0xFFFF → E-TYPE-WIDTH");

    // type checking (references: shared ref is read-only, G7 write-through typed)
    check(TYCK("fn f input n u64 . output u64 . do var x u64 be n . "
               "var s ref u64 . be ref x . set s 9 . return x . end") == false,
          "set through shared ref → E-TYPE-REF (statics now catch it)");
    check(TYCK("fn f input n u64 . output u64 . do var x u64 be n . "
               "var r mut_ref u64 . be mut_ref x . set r 9 . return x . end") == true,
          "set through mut_ref: ok");
    check(TYCK("fn f input n u64 . output u64 . do var x u64 be n . "
               "var r mut_ref u64 . be mut_ref x . set r true . return x . end") == false,
          "write-through value type vs referent → E-TYPE-SET");

    // contract checking (requires / errors)
    #define CTCK(s) ({ low_lex_result_t _l = LEX(s); proven_arena_reset(&arena); \
        low_parse_result_t _p = low_parse(nodes, heap, &_l.tokens); \
        low_contract_result_t _t = low_contract(heap, &_p); bool _ok = _t.ok; \
        proven_array_destroy(&_t.diags); if (_p.forms) heap.free_fn(heap.ctx, _p.forms); \
        proven_array_destroy(&_p.diags); proven_array_destroy(&_l.tokens); proven_array_destroy(&_l.diags); _ok; })

    // ★ 계약 절의 이름은 무언가를 가리켜야 한다 — requires 만 검사하고 **ensures 는 안 했다.**
    check(CTCK("fn f input a u8 . output u8 .  ensures le qqq 200 . "
               " do return a . end") == false,
          "contract: ★ an `ensures` naming nothing checks NOTHING — and the interval analysis "
          "DERIVES the result range from it (requires was checked; ensures was not)");
    check(CTCK("fn f input a u8 . output u8 .  ensures le ret 200 . "
               " do return a . end") == true,
          "contract: `ret` is the result name — a legitimate ensures still passes");

    check(CTCK("fn f input d slice u8 . errors too.short . do return error too.short . end") == true,
          "returns a declared error: ok");
    check(CTCK("fn f input d slice u8 . errors too.short . do return error too.long . end") == false,
          "returns an UNdeclared error → rejected");
    check(CTCK("fn f input d slice u8 . do return error boom . end") == false,
          "error with no errors clause → rejected");
    check(CTCK("fn f input xs slice u32 . requires ge len xs 1 . do return ok . end") == true,
          "requires references a param: ok");
    check(CTCK("fn f input xs slice u32 . requires ge len ys 1 . do return ok . end") == false,
          "requires references undefined name → rejected");

    // region / escape checking
    #define RGCK(s) ({ low_lex_result_t _l = LEX(s); proven_arena_reset(&arena); \
        low_parse_result_t _p = low_parse(nodes, heap, &_l.tokens); \
        low_region_result_t _t = low_region(heap, &_p); bool _ok = _t.ok; \
        proven_array_destroy(&_t.diags); if (_p.forms) heap.free_fn(heap.ctx, _p.forms); \
        proven_array_destroy(&_p.diags); proven_array_destroy(&_l.tokens); proven_array_destroy(&_l.diags); _ok; })

    check(RGCK("fn f input p ref u32 . output ref u32 . do return ref p . end") == true,
          "return ref to a param: ok (referent outlives op)");
    check(RGCK("fn f output ref u32 . do var l u32 be 5 . return ref l . end") == false,
          "return ref to a local → E-ESCAPE");
    check(RGCK("fn f output ref u32 . do var t u32 be 1 . return ok (mut_ref t) . end") == false,
          "nested ref-to-local in return → E-ESCAPE");
    check(RGCK("fn f output u32 . do var x u32 be 5 . return x . end") == true,
          "return a local by value (not by ref): ok");

    // EXCL exclusivity (readers-XOR-writer, last-use liveness v2 — NLL-style)
    check(RGCK("fn f output u32 . do var x u32 be 5 . "
               "var a mut_ref u32 be mut_ref x . var b mut_ref u32 be mut_ref x . "
               "set a 1 . set b 2 . return x . end") == false,
          "two overlapping (used) mut_ref of one local → E-EXCL");
    check(RGCK("fn f output u32 . do var x u32 be 5 . "
               "var a mut_ref u32 be mut_ref x . var b mut_ref u32 be mut_ref x . return x . end") == true,
          "two UNUSED mut_ref: intervals don't overlap → ok (liveness precision)");
    check(RGCK("fn f output u32 . do var x u32 be 5 . "
               "var a ref u32 be ref x . var b ref u32 be ref x . return x . end") == true,
          "two shared refs of one local: ok (readers)");
    check(RGCK("fn f output u32 . do var x u32 be 5 . "
               "var a mut_ref u32 be mut_ref x . var r ref u32 be ref x . set a 1 . return x . end") == false,
          "shared ref created inside a mut_ref's live interval → E-EXCL");
    check(RGCK("fn f output u32 . do var x u32 be 5 . var y u32 be 6 . "
               "var a mut_ref u32 be mut_ref x . var b mut_ref u32 be mut_ref y . "
               "set a 1 . set b 2 . return x . end") == true,
          "mut_ref of two different locals: ok");
    check(RGCK("fn f output u32 . do var x u32 be 5 . "
               "if true do var a mut_ref u32 be mut_ref x . set a 1 . end "
               "var b mut_ref u32 be mut_ref x . set b 2 . return x . end") == true,
          "sequential borrows in/after a block: ok (intervals disjoint)");
    check(RGCK("fn f output u32 . do var x u32 be 5 . "
               "var a mut_ref u32 be mut_ref x . var y u32 be x . set a 1 . return x . end") == false,
          "owner read inside a mut borrow's live interval → E-EXCL (v2)");
    check(RGCK("fn f output u32 . do var x u32 be 5 . "
               "var a mut_ref u32 be mut_ref x . set a 1 . "
               "while lt x 9 . do set x expr x + 1 . . set a 3 . end return x . end") == false,
          "loop wrap-around: owner access + crossing borrow in one while body → E-EXCL");
    check(RGCK("proc f output u32 . do var x u32 be 5 . g mut_ref x mut_ref x . return x . end") == false,
          "two mut_ref args to one call → E-EXCL");
    check(RGCK("proc f output u32 . do var x u32 be 5 . g ref x ref x . return x . end") == true,
          "two shared-ref args to one call: ok");

    // op-boundary borrows (D3): a reference-typed result aliases the argument borrow
    check(RGCK("fn pass input a mut_ref u32 . . output mut_ref u32 . . do return a . end "
               "fn f output u32 . do var x u32 be 1 . "
               "var r mut_ref u32 . be mut_ref x . "
               "var t mut_ref u32 . be pass r . "
               "set x 9 . set t 5 . return x . end") == false,
          "laundered borrow: owner write while the call result is live → E-EXCL (D3 fixed)");
    check(RGCK("fn pass input a mut_ref u32 . . output mut_ref u32 . . do return a . end "
               "fn f output u32 . do var x u32 be 1 . "
               "var r mut_ref u32 . be mut_ref x . "
               "var t mut_ref u32 . be pass r . "
               "set t 5 . return x . end") == true,
          "laundered borrow used correctly (no owner access while live): ok");
    check(RGCK("fn pass input a mut_ref u32 . . output mut_ref u32 . . do return a . end "
               "fn f output u32 . do var x u32 be 1 . "
               "var v u32 be pass 5 . set x 9 . return v . end") == true,
          "non-reference bindings are unaffected by the alias rule");

    // path sensitivity (D4): mutually exclusive if-arms never conflict
    check(RGCK("fn f input c u32 . output u32 . do var x u32 be 1 . "
               "var r mut_ref u32 . be mut_ref x . "
               "if gt c 0 . do set x 9 . end else do set r 5 . end return x . end") == true,
          "path-sensitive: owner write in one arm, borrow use in the other → ok (D4 fixed)");
    check(RGCK("fn f input c u32 . output u32 . do var x u32 be 1 . "
               "var r mut_ref u32 . be mut_ref x . "
               "if gt c 0 . do set x 9 . set r 5 . end else do var y u32 be 0 . end return x . end") == false,
          "path-sensitive: owner write + borrow use in the SAME arm → E-EXCL");
    check(RGCK("fn f input c u32 . output u32 . do var x u32 be 1 . "
               "var r mut_ref u32 . be mut_ref x . "
               "while lt c 3 . do set x 9 . set r 5 . end return x . end") == false,
          "path sensitivity does not weaken the loop rule (crossing borrow → E-EXCL)");
    // else-if arms are traversed (previously invisible → false negative)
    check(RGCK("fn f input c u32 . output u32 . do var x u32 be 1 . "
               "var r mut_ref u32 . be mut_ref x . "
               "if gt c 5 . do var z u32 be 0 . end "
               "else if gt c 2 . do set x 9 . set r 5 . end "
               "return x . end") == false,
          "else-if body is analyzed: owner+borrow inside it → E-EXCL (false negative fixed)");

    // ── S5: BLAKE3 content addressing ──
    {
        proven_u8 h[32]; char hex[65];
        low_blake3_256((const proven_byte_t *)"", 0, h);
        for (int i = 0; i < 32; i++) sprintf(hex + 2 * i, "%02x", h[i]);
        check(strcmp(hex, "af1349b9f5f9a1a6a0404dea36dcc9499bcb25c9adc112b7cc9a93cae41f3262") == 0,
              "blake3: official empty-input vector");
        static proven_byte_t b3buf[1025];
        for (int i = 0; i < 1025; i++) b3buf[i] = (proven_byte_t)(i * 251 % 256);
        low_blake3_256(b3buf, 1025, h);
        for (int i = 0; i < 32; i++) sprintf(hex + 2 * i, "%02x", h[i]);
        check(strcmp(hex, "a72b987a880217dbf608b63561c8804324b816d6ac9ed1fa66fad090208aa081") == 0,
              "blake3: cross-chunk (1025B) vector vs reference impl");
    }

    // ── S5: stack IR lowering + VM execution + hash identity ──
    {
        const char *src =
            "fn fact input n u64 . output u64 .  do "
            "var acc u64 be 1 . var i u64 be 1 . "
            "while le i n . do set acc mul acc i . set i expr i + 1 . . end "
            "return acc . end "
            "fn fib input n u64 . output u64 .  do "
            "if lt n 2 . do return n . end "
            "return expr (fib (expr n - 1 .)) + (fib (expr n - 2 .)) . . end";
        low_lex_result_t l = LEX(src); proven_arena_reset(&arena);
        low_parse_result_t p = low_parse(nodes, heap, &l.tokens);
        low_ir_t ir = low_ir_build(heap, &p);
        check(ir.ok && ir.ndefs == 2 && ir.defs[0].lowered && ir.defs[1].lowered,
              "S5: fact+fib lower to stack IR");
        proven_i64 a5[1] = { 5 }, a10[1] = { 10 };
        low_ir_run_result_t r = low_ir_run(&ir, proven_u8str_view_from_cstr("fact"), a5, 1, heap, &ir.diags);
        check(r.ok && r.value == 120, "S5 VM: fact(5) = 120 (while/set/expr island)");
        r = low_ir_run(&ir, proven_u8str_view_from_cstr("fib"), a10, 1, heap, &ir.diags);
        check(r.ok && r.value == 55, "S5 VM: fib(10) = 55 (recursive call)");
        r = low_ir_run(&ir, proven_u8str_view_from_cstr("fact"), a10, 0, heap, &ir.diags);
        check(!r.ok, "S5 VM: wrong argument count → E-VM-ARITY");
        check(memcmp(ir.defs[0].iface_hash, ir.defs[1].iface_hash, 32) == 0,
              "S5 hash: same signature → same iface-hash");
        check(memcmp(ir.defs[0].def_hash, ir.defs[1].def_hash, 32) != 0,
              "S5 hash: different body → different def-hash");
        low_ir_free(heap, &ir);
        if (p.forms) heap.free_fn(heap.ctx, p.forms);
        proven_array_destroy(&p.diags); proven_array_destroy(&l.tokens); proven_array_destroy(&l.diags);
    }
    {
        // structural identity: names erased (de Bruijn) → identical defs dedup by hash
        const char *src =
            "fn double input x u64 . output u64 . do return expr x * 2 . . end "
            "fn twice  input y u64 . output u64 . do return expr y * 2 . . end "
            "fn plus2  input x u64 . output u64 . do return expr x + 2 . . end "
            "fn tenth  input x u64 . output u64 . do return div 10 x . end";
        low_lex_result_t l = LEX(src); proven_arena_reset(&arena);
        low_parse_result_t p = low_parse(nodes, heap, &l.tokens);
        low_ir_t ir = low_ir_build(heap, &p);
        check(ir.ok && ir.ndefs == 4, "S5: dedup fixture lowers");
        check(memcmp(ir.defs[0].def_hash, ir.defs[1].def_hash, 32) == 0,
              "S5 hash: structurally identical defs → same def-hash (dedup, name-free)");
        check(memcmp(ir.defs[0].def_hash, ir.defs[2].def_hash, 32) != 0,
              "S5 hash: body edit → def-hash changes");
        check(memcmp(ir.defs[0].iface_hash, ir.defs[2].iface_hash, 32) == 0,
              "S5 hash: body edit keeps iface-hash (relink-only boundary)");
        proven_i64 z[1] = { 0 };
        low_ir_run_result_t r = low_ir_run(&ir, proven_u8str_view_from_cstr("tenth"), z, 1, heap, &ir.diags);
        check(!r.ok, "S5 VM: divide by zero → E-VM-DIV0");
        low_ir_free(heap, &ir);
        if (p.forms) heap.free_fn(heap.ctx, p.forms);
        proven_array_destroy(&p.diags); proven_array_destroy(&l.tokens); proven_array_destroy(&l.diags);
    }

    // ── S5b: SCC fixed-point Merkle def-hash ──
    {
        // a callee body edit must propagate into the caller's def-hash (Merkle)
        const char *src =
            "fn hlp1 input x u64 . output u64 . do return expr x + 1 . . end "
            "fn cal1 input x u64 . output u64 . do return hlp1 x . end "
            "fn hlp2 input x u64 . output u64 . do return expr x + 2 . . end "
            "fn cal2 input x u64 . output u64 . do return hlp2 x . end";
        low_lex_result_t l = LEX(src); proven_arena_reset(&arena);
        low_parse_result_t p = low_parse(nodes, heap, &l.tokens);
        low_ir_t ir = low_ir_build(heap, &p);
        check(ir.ok && ir.ndefs == 4, "S5b: Merkle fixture lowers");
        check(memcmp(ir.defs[1].def_hash, ir.defs[3].def_hash, 32) != 0,
              "S5b Merkle: callee body edit changes the CALLER def-hash");
        check(memcmp(ir.defs[1].iface_hash, ir.defs[3].iface_hash, 32) == 0,
              "S5b Merkle: caller iface-hash unaffected");
        low_ir_free(heap, &ir);
        if (p.forms) heap.free_fn(heap.ctx, p.forms);
        proven_array_destroy(&p.diags); proven_array_destroy(&l.tokens); proven_array_destroy(&l.diags);
    }
    {
        // mutual recursion = one SCC: runs on the VM, hashes deterministic across builds
        const char *src =
            "fn is_even input n u64 . output bool .  do "
            "if eq n 0 . do return true . end return is_odd expr n - 1 . . end "
            "fn is_odd input n u64 . output bool .  do "
            "if eq n 0 . do return false . end return is_even expr n - 1 . . end";
        proven_u8 h1[32], h2[32];
        low_lex_result_t l = LEX(src); proven_arena_reset(&arena);
        low_parse_result_t p = low_parse(nodes, heap, &l.tokens);
        low_ir_t ir = low_ir_build(heap, &p);
        check(ir.ok && ir.ndefs == 2, "S5b SCC: mutual recursion lowers");
        proven_i64 ten[1] = { 10 };
        low_ir_run_result_t r = low_ir_run(&ir, proven_u8str_view_from_cstr("is_even"), ten, 1, heap, &ir.diags);
        check(r.ok && r.value == 1, "S5b VM: is_even(10) via mutual recursion");
        r = low_ir_run(&ir, proven_u8str_view_from_cstr("is_odd"), ten, 1, heap, &ir.diags);
        check(r.ok && r.value == 0, "S5b VM: is_odd(10) via mutual recursion");
        check(memcmp(ir.defs[0].def_hash, ir.defs[1].def_hash, 32) != 0,
              "S5b SCC: group members keep distinct def-hashes");
        memcpy(h1, ir.defs[0].def_hash, 32);
        low_ir_free(heap, &ir);
        low_ir_t ir2 = low_ir_build(heap, &p);
        memcpy(h2, ir2.defs[0].def_hash, 32);
        check(memcmp(h1, h2, 32) == 0, "S5b SCC: def-hash deterministic across builds");
        low_ir_free(heap, &ir2);
        if (p.forms) heap.free_fn(heap.ctx, p.forms);
        proven_array_destroy(&p.diags); proven_array_destroy(&l.tokens); proven_array_destroy(&l.diags);
    }

    // ── S5b: values — guard/error/ok/try/make/slice on the VM ──
    {
        const char *src =
            "type bytes slice u8 . . "
            "enum e2 do neg end "
            "struct pairr do a u64 . b u64 . end "
            "fn chk input n u64 . output result u64 e2 .  do "
            "guard ge n 1 . else return error neg . . "
            "return ok n . end "
            "fn mk input n u64 . output pairr .  do "
            "return make pairr do a n . b mul n 2 . end end "
            "fn use_try input n u64 . output u64 .  do "
            "let v u64 be try chk n . . return v . end "
            "fn first input d bytes . output u64 . do return index d 0 . end";
        low_lex_result_t l = LEX(src); proven_arena_reset(&arena);
        low_parse_result_t p = low_parse(nodes, heap, &l.tokens);
        low_ir_t ir = low_ir_build(heap, &p);
        check(ir.ok && ir.ndefs == 4, "S5b: values fixture lowers (guard/make/try/slice)");
        proven_i64 five[1] = { 5 }, zero[1] = { 0 }, seven[1] = { 7 }, three[1] = { 3 };
        low_ir_run_result_t r = low_ir_run(&ir, proven_u8str_view_from_cstr("chk"), five, 1, heap, &ir.diags);
        check(r.ok && strcmp(r.text, "ok 5") == 0, "S5b VM: guard pass → ok 5");
        r = low_ir_run(&ir, proven_u8str_view_from_cstr("chk"), zero, 1, heap, &ir.diags);
        check(r.ok && strcmp(r.text, "err neg") == 0, "S5b VM: guard diverge → err neg");
        r = low_ir_run(&ir, proven_u8str_view_from_cstr("mk"), three, 1, heap, &ir.diags);
        check(r.ok && strcmp(r.text, "{a 3, b 6}") == 0, "S5b VM: make record literal renders");
        r = low_ir_run(&ir, proven_u8str_view_from_cstr("use_try"), zero, 1, heap, &ir.diags);
        check(r.ok && strcmp(r.text, "err neg") == 0, "S5b VM: try propagates err early-return");
        r = low_ir_run(&ir, proven_u8str_view_from_cstr("use_try"), seven, 1, heap, &ir.diags);
        check(r.ok && r.value == 7, "S5b VM: try unwraps ok payload");
        proven_i64 bytes2[2] = { 7, 9 };
        r = low_ir_run(&ir, proven_u8str_view_from_cstr("first"), bytes2, 2, heap, &ir.diags);
        check(r.ok && r.value == 7, "S5b VM: CLI ints → byte slice param; index works");
        low_ir_free(heap, &ir);
        if (p.forms) heap.free_fn(heap.ctx, p.forms);
        proven_array_destroy(&p.diags); proven_array_destroy(&l.tokens); proven_array_destroy(&l.diags);
    }

    // ── S5c: containers — stack (pop-into), bitset (poly add/contains/count), for ──
    {
        const char *src =
            "type bytes slice u8 . . "
            "proc drain input n u64 . output u64 . effects none . do "
            "var work stack u64 . be stack_new r capacity 8 . . "
            "var i u64 be 0 . "
            "while lt i n . do push work i . set i expr i + 1 . . end "
            "var total u64 be 0 . "
            "while pop work into v . do set total expr total + v . . end "
            "return total . end "
            "proc bset input a u64 . input b u64 . output u64 . effects none . do "
            "var s bitset be bitset_new 64 . . "
            "add s a . add s b . add s a . "
            "guard contains s a . else return 99 . . "
            "return count s . end "
            "fn sum_bytes input xs bytes . output u64 .  do "
            "var t u64 be 0 . "
            "for x xs do set t expr t + x . . end "
            "return t . end";
        low_lex_result_t l = LEX(src); proven_arena_reset(&arena);
        low_parse_result_t p = low_parse(nodes, heap, &l.tokens);
        low_ir_t ir = low_ir_build(heap, &p);
        check(ir.ok && ir.ndefs == 3, "S5c: container fixture lowers (stack/bitset/for)");
        proven_i64 five[1] = { 5 };
        low_ir_run_result_t r = low_ir_run(&ir, proven_u8str_view_from_cstr("drain"), five, 1, heap, &ir.diags);
        check(r.ok && r.value == 10, "S5c VM: stack_new/push + while pop-into drains 0..4 → 10");
        proven_i64 ab[2] = { 3, 7 };
        r = low_ir_run(&ir, proven_u8str_view_from_cstr("bset"), ab, 2, heap, &ir.diags);
        check(r.ok && r.value == 2, "S5c VM: bitset add dedups; contains guards; count = 2");
        proven_i64 aa[2] = { 5, 5 };
        r = low_ir_run(&ir, proven_u8str_view_from_cstr("bset"), aa, 2, heap, &ir.diags);
        check(r.ok && r.value == 1, "S5c VM: bitset add is idempotent (count = 1)");
        proven_i64 b3[3] = { 1, 2, 3 };
        r = low_ir_run(&ir, proven_u8str_view_from_cstr("sum_bytes"), b3, 3, heap, &ir.diags);
        check(r.ok && r.value == 6, "S5c VM: for-in iterates a slice (sum = 6)");
        low_ir_free(heap, &ir);
        if (p.forms) heap.free_fn(heap.ctx, p.forms);
        proven_array_destroy(&p.diags); proven_array_destroy(&l.tokens); proven_array_destroy(&l.diags);
    }

    // ── S-impl-4 V1: dynamic verifier — runtime references (RFC-0017) ──
    {
        #define HASDIAG(diags, wanted) ({ bool _found = false; \
            for (proven_size_t _i = 0; _i < (diags).len; _i++) \
                if (strcmp(PROVEN_ARRAY_GET(&(diags), low_diag_t, _i)->code, (wanted)) == 0) _found = true; \
            _found; })
        const char *src =
            "fn bump input p mut_ref u32 . . output u32 .  do "
            "set p expr (deref p) + 1 . . return deref p . end "
            "fn bump42 input n u64 . output u64 .  do "
            "var x u64 be n . "
            "var r mut_ref u64 . be mut_ref x . "
            "bump r . "
            "return x . end "
            "fn ro_write input n u64 . output u64 .  do "
            "var x u64 be n . "
            "var s ref u64 . be ref x . "
            "set s 9 . return x . end "
            "fn bad output ref u64 . .  do "
            "var l u64 be 5 . return ref l . end "
            "fn use_bad output u64 .  do "
            "var r ref u64 . be bad . return deref r . end";
        low_lex_result_t l = LEX(src); proven_arena_reset(&arena);
        low_parse_result_t p = low_parse(nodes, heap, &l.tokens);
        low_ir_t ir = low_ir_build(heap, &p);
        check(ir.ok && ir.ndefs == 5, "V1: reference fixture lowers");
        proven_i64 a41[1] = { 41 }, a5[1] = { 5 };
        low_ir_run_result_t r = low_ir_run(&ir, proven_u8str_view_from_cstr("bump42"), a41, 1, heap, &ir.diags);
        check(r.ok && r.value == 42, "V1 VM: cross-frame mut_ref write-through (bump42 = 42)");
        r = low_ir_run(&ir, proven_u8str_view_from_cstr("ro_write"), a5, 1, heap, &ir.diags);
        check(!r.ok && HASDIAG(ir.diags, "E-VM-READONLY"), "V1 VM: write through shared ref → E-VM-READONLY");
        r = low_ir_run(&ir, proven_u8str_view_from_cstr("use_bad"), NULL, 0, heap, &ir.diags);
        check(!r.ok && HASDIAG(ir.diags, "E-VM-DANGLING"), "V1 VM: use-after-return → E-VM-DANGLING");
        // static ≡ dynamic: the same dangling program is rejected statically too
        low_region_result_t rr = low_region(heap, &p);
        check(!rr.ok, "V1: dangling twin also rejected statically (E-ESCAPE) — static≡dynamic");
        proven_array_destroy(&rr.diags);
        low_ir_free(heap, &ir);
        if (p.forms) heap.free_fn(heap.ctx, p.forms);
        proven_array_destroy(&p.diags); proven_array_destroy(&l.tokens); proven_array_destroy(&l.diags);
    }

    // ── V1 dynamic EXCL (borrow-stack-lite) + the static gap it found ──
    {
        const char *src =
            "fn two_mut output u64 .  do "
            "var x u64 be 0 . "
            "var a mut_ref u64 . be mut_ref x . "
            "var b mut_ref u64 . be mut_ref x . "
            "set a 1 . set b 2 . return x . end "
            "fn owner_read output u64 .  do "
            "var x u64 be 7 . "
            "var r mut_ref u64 . be mut_ref x . "
            "var y u64 be x . "
            "set r 5 . return x . end";
        low_lex_result_t l = LEX(src); proven_arena_reset(&arena);
        low_parse_result_t p = low_parse(nodes, heap, &l.tokens);
        low_ir_t ir = low_ir_build(heap, &p);
        check(ir.ok && ir.ndefs == 2, "V1 EXCL: fixture lowers");
        low_ir_run_result_t r = low_ir_run(&ir, proven_u8str_view_from_cstr("two_mut"), NULL, 0, heap, &ir.diags);
        check(!r.ok && HASDIAG(ir.diags, "E-VM-EXCL"), "V1 EXCL: second mut_ref invalidates the first → E-VM-EXCL");
        // static EXCL now catches split bindings too (gap found BY the dynamic verifier)
        low_region_result_t rr = low_region(heap, &p);
        bool static_excl = false;
        for (proven_size_t i = 0; i < rr.diags.len; i++)
            if (strcmp(PROVEN_ARRAY_GET(&rr.diags, low_diag_t, i)->code, "E-EXCL") == 0) static_excl = true;
        check(static_excl, "V1 EXCL: split-binding two_mut also rejected statically (gap fixed)");
        proven_array_destroy(&rr.diags);
        r = low_ir_run(&ir, proven_u8str_view_from_cstr("owner_read"), NULL, 0, heap, &ir.diags);
        check(!r.ok && HASDIAG(ir.diags, "E-VM-EXCL"),
              "V1 EXCL: owner read invalidates unique borrow → E-VM-EXCL (twin of static v2)");
        low_ir_free(heap, &ir);
        if (p.forms) heap.free_fn(heap.ctx, p.forms);
        proven_array_destroy(&p.diags); proven_array_destroy(&l.tokens); proven_array_destroy(&l.diags);
    }
    {
        // green paths stay green under the borrow stack: sequential reborrow
        const char *src =
            "fn seq output u64 .  do "
            "var x u64 be 1 . "
            "var a mut_ref u64 . be mut_ref x . "
            "set a 2 . "
            "var y u64 be x . "
            "var b mut_ref u64 . be mut_ref x . "
            "set b expr y + 2 . . "
            "return x . end";
        low_lex_result_t l = LEX(src); proven_arena_reset(&arena);
        low_parse_result_t p = low_parse(nodes, heap, &l.tokens);
        low_ir_t ir = low_ir_build(heap, &p);
        low_ir_run_result_t r = low_ir_run(&ir, proven_u8str_view_from_cstr("seq"), NULL, 0, heap, &ir.diags);
        check(r.ok && r.value == 4, "V1 EXCL: sequential borrows (use → owner read → reborrow) stay green");
        low_ir_free(heap, &ir);
        if (p.forms) heap.free_fn(heap.ctx, p.forms);
        proven_array_destroy(&p.diags); proven_array_destroy(&l.tokens); proven_array_destroy(&l.diags);
    }

    // ── structs first-class: access surfaces + hash invariance ──
    {
        const char *src =
            "struct pairr do a u64 . b u64 . end "
            "fn mk2 output pairr .  do return make pairr do a 3 . b 9 . end end "
            "fn geta output u64 .  do var p pairr be mk2 . return field p a . end "
            "fn getb output u64 .  do var p pairr be mk2 . return field p b . end "
            "fn getg output u64 .  do var p pairr be mk2 . return (field p a) . end "
            "fn getin output u64 .  do var p pairr be mk2 . return (field p b) . end";
        low_lex_result_t l = LEX(src); proven_arena_reset(&arena);
        low_parse_result_t p = low_parse(nodes, heap, &l.tokens);
        low_ir_t ir = low_ir_build(heap, &p);
        check(ir.ok && ir.ndefs == 5, "struct: access fixture lowers");
        low_ir_run_result_t r = low_ir_run(&ir, proven_u8str_view_from_cstr("geta"), NULL, 0, heap, &ir.diags);
        check(r.ok && r.value == 3, "struct: prefix `field p a` reads a record field");
        r = low_ir_run(&ir, proven_u8str_view_from_cstr("getb"), NULL, 0, heap, &ir.diags);
        check(r.ok && r.value == 9, "struct: `field p b` reads the other field");
        r = low_ir_run(&ir, proven_u8str_view_from_cstr("getg"), NULL, 0, heap, &ir.diags);
        check(r.ok && r.value == 3, "struct: parenthesised `(field p a)` is the same form");
        r = low_ir_run(&ir, proven_u8str_view_from_cstr("getin"), NULL, 0, heap, &ir.diags);
        check(r.ok && r.value == 9, "struct: parenthesised `(field p b)` too");
        /* ★★★ The four spellings became ONE (2026-08-25). This block used to
           assert that `field p a`, `p to b`, `p.a` and `b in p` all normalize to the
           same IR — four surfaces, one meaning. Three of them are gone now
           (`to`/`in` by RFC-0049, the glued dot by owner decision), so what is left
           to check is that the ONE surviving surface is stable under parentheses. */
        check(memcmp(ir.defs[1].def_hash, ir.defs[3].def_hash, 32) == 0,
              "struct hash: `field p a` ≡ `(field p a)` (parens do not change the IR)");
        check(memcmp(ir.defs[2].def_hash, ir.defs[4].def_hash, 32) == 0,
              "struct hash: `field p b` ≡ `(field p b)`");
        check(memcmp(ir.defs[1].def_hash, ir.defs[2].def_hash, 32) != 0,
              "struct hash: different field name → different def-hash");
        low_ir_free(heap, &ir);
        if (p.forms) heap.free_fn(heap.ctx, p.forms);
        proven_array_destroy(&p.diags); proven_array_destroy(&l.tokens); proven_array_destroy(&l.diags);
    }

    // ── view/try_view (RFC-0025 §6.6): packed layout + be fields, zero-copy ──
    {
        const char *src =
            "type bytes slice u8 . . "
            "struct wire_header do layout packed . magic u32 big . length u16 big . kind u8 . end "
            "fn hdr_kind input b bytes . output u64 .  do "
            "var o wire_header . be try_view wire_header b . . "
            "guard is_some o . else return 999 . . "
            "var h wire_header . be some_value o . "
            "return field h kind . end "
            "fn hdr_sum input b bytes . output u64 .  do "
            "var h wire_header . be view wire_header b . . "
            "return expr (field h length) * 1000 + (field h kind) . . end";
        low_lex_result_t l = LEX(src); proven_arena_reset(&arena);
        low_parse_result_t p = low_parse(nodes, heap, &l.tokens);
        low_ir_t ir = low_ir_build(heap, &p);
        check(ir.ok && ir.ndefs == 2 && ir.nstructs == 1 && ir.structs[0].packed &&
              ir.structs[0].total == 7, "view: packed layout computed (4+2+1 = 7 bytes)");
        proven_i64 wire[7] = { 222, 173, 190, 239, 1, 2, 7 };
        low_ir_run_result_t r = low_ir_run(&ir, proven_u8str_view_from_cstr("hdr_kind"), wire, 7, heap, &ir.diags);
        check(r.ok && r.value == 7, "view: try_view some-path reads kind through the view");
        proven_i64 shrt[2] = { 1, 2 };
        r = low_ir_run(&ir, proven_u8str_view_from_cstr("hdr_kind"), shrt, 2, heap, &ir.diags);
        check(r.ok && r.value == 999, "view: try_view none-path (slice too short) → guard diverges");
        r = low_ir_run(&ir, proven_u8str_view_from_cstr("hdr_sum"), wire, 7, heap, &ir.diags);
        check(r.ok && r.value == 258007, "view: be u16 decode via infix/glued access (0x0102 → 258)");
        r = low_ir_run(&ir, proven_u8str_view_from_cstr("hdr_sum"), shrt, 2, heap, &ir.diags);
        check(!r.ok && HASDIAG(ir.diags, "E-VM-VIEW"), "view: length contract violation panics (E-VM-VIEW)");
        low_ir_free(heap, &ir);
        if (p.forms) heap.free_fn(heap.ctx, p.forms);
        proven_array_destroy(&p.diags); proven_array_destroy(&l.tokens); proven_array_destroy(&l.diags);
    }
    {
        // encode (RFC-0025 §6.7 왕복): make → wire bytes → view → same fields
        const char *src =
            "struct wire_header do layout packed . magic u32 big . length u16 big . kind u8 . end "
            "fn mk_hdr output wire_header .  do "
            "return make wire_header do magic 3735928559 . length 258 . kind 7 . end end "
            "fn rt output u64 .  do "
            "var h wire_header . be view wire_header encode wire_header mk_hdr . . "
            "return expr (field h magic) + (field h length) + (field h kind) . . end "
            "fn blen output u64 .  do return len encode wire_header mk_hdr . . end";
        low_lex_result_t l = LEX(src); proven_arena_reset(&arena);
        low_parse_result_t p = low_parse(nodes, heap, &l.tokens);
        low_ir_t ir = low_ir_build(heap, &p);
        check(ir.ok && ir.ndefs == 3, "encode: round-trip fixture lowers");
        low_ir_run_result_t r = low_ir_run(&ir, proven_u8str_view_from_cstr("rt"), NULL, 0, heap, &ir.diags);
        check(r.ok && r.value == 3735928559ll + 258 + 7, "encode→view round-trip preserves every field");
        r = low_ir_run(&ir, proven_u8str_view_from_cstr("blen"), NULL, 0, heap, &ir.diags);
        check(r.ok && r.value == 7, "encode: wire length = packed layout total (7)");
        low_ir_free(heap, &ir);
        if (p.forms) heap.free_fn(heap.ctx, p.forms);
        proven_array_destroy(&p.diags); proven_array_destroy(&l.tokens); proven_array_destroy(&l.diags);
    }

    // ── native (non-packed) layout: aligned offsets + tail padding ──
    {
        const char *src =
            "struct mixed do flag u8 . big u32 . small u16 . end "
            "fn mk output mixed .  do "
            "return make mixed do flag 1 . big 305419896 . small 4660 . end end "
            "fn rt output u64 .  do "
            "var h mixed . be view mixed encode mixed mk . . "
            "return expr (field h flag) + (field h big) + (field h small) . . end "
            "fn blen output u64 .  do return len encode mixed mk . . end";
        low_lex_result_t l = LEX(src); proven_arena_reset(&arena);
        low_parse_result_t p = low_parse(nodes, heap, &l.tokens);
        low_ir_t ir = low_ir_build(heap, &p);
        // u8@0, u32 aligned @4, u16 @8, total padded to align 4 → 12
        check(ir.ok && ir.nstructs == 1 && !ir.structs[0].packed &&
              ir.structs[0].f[0].off == 0 && ir.structs[0].f[1].off == 4 &&
              ir.structs[0].f[2].off == 8 && ir.structs[0].total == 12,
              "layout: native offsets are size-aligned; total padded (1,4,2 → 0,4,8 / 12)");
        low_ir_run_result_t r = low_ir_run(&ir, proven_u8str_view_from_cstr("rt"), NULL, 0, heap, &ir.diags);
        check(r.ok && r.value == 1 + 305419896 + 4660, "layout: native round-trip preserves fields");
        r = low_ir_run(&ir, proven_u8str_view_from_cstr("blen"), NULL, 0, heap, &ir.diags);
        check(r.ok && r.value == 12, "layout: native wire length includes padding");
        low_ir_free(heap, &ir);
        if (p.forms) heap.free_fn(heap.ctx, p.forms);
        proven_array_destroy(&p.diags); proven_array_destroy(&l.tokens); proven_array_destroy(&l.diags);
    }

    // ── view_array + strings + view-EXCL ──
    {
        const char *src =
            "type bytes slice u8 . . "
            "fn va_sum input b bytes . output u64 .  do "
            "var t u64 be 0 . for x view_array u16 b . do set t expr t + x . . end return t . end "
            "fn va_at input b bytes . output u64 .  do "
            "return index (view_array u32 b) 1 . end "
            "fn slen output u64 .  do return len \"hello\" . end "
            "fn ssum output u64 .  do "
            "var t u64 be 0 . for ch \"abc\" do set t expr t + ch . . end return t . end";
        low_lex_result_t l = LEX(src); proven_arena_reset(&arena);
        low_parse_result_t p = low_parse(nodes, heap, &l.tokens);
        low_ir_t ir = low_ir_build(heap, &p);
        check(ir.ok && ir.ndefs == 4, "view_array/strings: fixture lowers");
        proven_i64 w6[6] = { 1, 0, 2, 0, 3, 0 };
        low_ir_run_result_t r = low_ir_run(&ir, proven_u8str_view_from_cstr("va_sum"), w6, 6, heap, &ir.diags);
        check(r.ok && r.value == 6, "view_array: u16 le elements iterate via for-in (sum 6)");
        proven_i64 w8[8] = { 1, 0, 0, 0, 42, 0, 0, 0 };
        r = low_ir_run(&ir, proven_u8str_view_from_cstr("va_at"), w8, 8, heap, &ir.diags);
        check(r.ok && r.value == 42, "view_array: u32 index decodes one element");
        proven_i64 w5[5] = { 1, 2, 3, 4, 5 };
        r = low_ir_run(&ir, proven_u8str_view_from_cstr("va_sum"), w5, 5, heap, &ir.diags);
        check(!r.ok && HASDIAG(ir.diags, "E-VM-VIEW"), "view_array: non-multiple length panics");
        r = low_ir_run(&ir, proven_u8str_view_from_cstr("slen"), NULL, 0, heap, &ir.diags);
        check(r.ok && r.value == 5, "strings: literal is a byte slice (len \"hello\" = 5)");
        r = low_ir_run(&ir, proven_u8str_view_from_cstr("ssum"), NULL, 0, heap, &ir.diags);
        check(r.ok && r.value == 294, "strings: for-in over literal bytes ('a'+'b'+'c')");
        low_ir_free(heap, &ir);
        if (p.forms) heap.free_fn(heap.ctx, p.forms);
        proven_array_destroy(&p.diags); proven_array_destroy(&l.tokens); proven_array_destroy(&l.diags);
    }
    {
        // view-EXCL: rebinding the viewed slice while the view is live → static E-EXCL
        const char *src =
            "struct pairr do a u32 . b u32 . end "
            "fn mkp output pairr .  do return make pairr do a 5 . b 6 . end end "
            "fn stale output u64 .  do "
            "var s u64 . be encode pairr mkp . . "
            "var h pairr . be view pairr s . "
            "set s encode pairr mkp . . "
            "return field h a . end "
            "fn fine output u64 .  do "
            "var s u64 . be encode pairr mkp . . "
            "var h pairr . be view pairr s . "
            "var v u64 be field h a . "
            "set s encode pairr mkp . . "
            "return v . end";
        low_lex_result_t l = LEX(src); proven_arena_reset(&arena);
        low_parse_result_t p = low_parse(nodes, heap, &l.tokens);
        low_region_result_t rr = low_region(heap, &p);
        bool has_excl = false;
        for (proven_size_t i = 0; i < rr.diags.len; i++)
            if (strcmp(PROVEN_ARRAY_GET(&rr.diags, low_diag_t, i)->code, "E-EXCL") == 0) has_excl = true;
        check(!rr.ok && has_excl, "view-EXCL: owner rebind inside a live view's interval → E-EXCL");
        proven_array_destroy(&rr.diags);
        // `fine` alone must be green: check the fixture minus the stale op
        low_ir_t ir = low_ir_build(heap, &p);
        low_ir_run_result_t r = low_ir_run(&ir, proven_u8str_view_from_cstr("fine"), NULL, 0, heap, &ir.diags);
        check(r.ok && r.value == 5, "view-EXCL: read-then-rebind (view dead) stays green and runs");
        low_ir_free(heap, &ir);
        if (p.forms) heap.free_fn(heap.ctx, p.forms);
        proven_array_destroy(&p.diags); proven_array_destroy(&l.tokens); proven_array_destroy(&l.diags);
    }

    // ── portable SIMD (RFC-0040): comptime lanes, arith/compare lift, reductions ──
    {
        const char *src =
            "type bytes slice u8 . . "
            "fn dot4 input b bytes . output u64 .  do "
            "var xs u64 . be view_array u32 b . "
            "var va vec u32 4 . be load xs 0 . "
            "var vb vec u32 4 . be load xs 4 . "
            "return reduce_add mul va vb . end "
            "fn clampsum input b bytes . output u64 .  do "
            "var xs u64 . be view_array u32 b . "
            "var v vec u32 4 . be load xs 0 . "
            "var lim vec u32 4 . be splat 10 . "
            "var m u64 . be gt v lim . "
            "var cc vec u32 4 . be select m lim v . "
            "return reduce_add cc . end "
            "fn over input b bytes . output u64 .  do "
            "var xs u64 . be view_array u32 b . "
            "var v vec u32 4 . be load xs 0 . "
            "var lim vec u32 4 . be splat 10 . "
            "return any gt v lim . . end";
        low_lex_result_t l = LEX(src); proven_arena_reset(&arena);
        low_parse_result_t p = low_parse(nodes, heap, &l.tokens);
        low_ir_t ir = low_ir_build(heap, &p);
        check(ir.ok && ir.ndefs == 3, "vec: SIMD fixture lowers (comptime lanes from binding types)");
        proven_i64 w32[32] = { 1,0,0,0, 2,0,0,0, 3,0,0,0, 4,0,0,0, 5,0,0,0, 6,0,0,0, 7,0,0,0, 8,0,0,0 };
        low_ir_run_result_t r = low_ir_run(&ir, proven_u8str_view_from_cstr("dot4"), w32, 32, heap, &ir.diags);
        check(r.ok && r.value == 70, "vec: lanewise mul + reduce_add = dot product (70)");
        proven_i64 w16[16] = { 3,0,0,0, 12,0,0,0, 7,0,0,0, 20,0,0,0 };
        r = low_ir_run(&ir, proven_u8str_view_from_cstr("clampsum"), w16, 16, heap, &ir.diags);
        check(r.ok && r.value == 30, "vec: compare→mask + select clamps lanes (3+10+7+10)");
        r = low_ir_run(&ir, proven_u8str_view_from_cstr("over"), w16, 16, heap, &ir.diags);
        check(r.ok && r.value == 1, "vec: any(mask) over threshold → true");
        proven_i64 lo16[16] = { 1,0,0,0, 2,0,0,0, 3,0,0,0, 4,0,0,0 };
        r = low_ir_run(&ir, proven_u8str_view_from_cstr("over"), lo16, 16, heap, &ir.diags);
        check(r.ok && r.value == 0, "vec: any(mask) under threshold → false");
        proven_i64 w8b[8] = { 1,0,0,0, 2,0,0,0 };
        r = low_ir_run(&ir, proven_u8str_view_from_cstr("dot4"), w8b, 8, heap, &ir.diags);
        check(!r.ok && HASDIAG(ir.diags, "E-VM-BOUNDS"), "vec: load past the array bounds panics");
        low_ir_free(heap, &ir);
        if (p.forms) heap.free_fn(heap.ctx, p.forms);
        proven_array_destroy(&p.diags); proven_array_destroy(&l.tokens); proven_array_destroy(&l.diags);
    }

    // ── float core: IEEE arith, compares, runtime cast (G2) ──
    {
        const char *src =
            "fn favg input a u64 . input b u64 . output f64 .  do "
            "return expr ((cast f64 a) + (cast f64 b)) / 2.0 . end "
            "fn fdiv0 output f64 .  do return expr 1.0 / 0.0 . . end "
            "fn ftrunc output u64 .  do return cast u32 7.9 . end "
            "fn fmix output u64 .  do return expr 1 + 2.5 . . end";
        low_lex_result_t l = LEX(src); proven_arena_reset(&arena);
        low_parse_result_t p = low_parse(nodes, heap, &l.tokens);
        low_ir_t ir = low_ir_build(heap, &p);
        check(ir.ok && ir.ndefs == 4, "float: fixture lowers (literals + cast)");
        proven_i64 ab[2] = { 3, 4 };
        low_ir_run_result_t r = low_ir_run(&ir, proven_u8str_view_from_cstr("favg"), ab, 2, heap, &ir.diags);
        check(r.ok && strcmp(r.text, "3.5") == 0, "float: int→f64 cast + IEEE arith (favg(3,4) = 3.5)");
        r = low_ir_run(&ir, proven_u8str_view_from_cstr("fdiv0"), NULL, 0, heap, &ir.diags);
        check(r.ok && strcmp(r.text, "inf") == 0, "float: division by zero is IEEE inf (no trap)");
        r = low_ir_run(&ir, proven_u8str_view_from_cstr("ftrunc"), NULL, 0, heap, &ir.diags);
        check(r.ok && r.value == 7, "float: cast u32 truncates (7.9 → 7)");
        r = low_ir_run(&ir, proven_u8str_view_from_cstr("fmix"), NULL, 0, heap, &ir.diags);
        check(!r.ok && HASDIAG(ir.diags, "E-VM-TYPE"), "float: int+float mixing rejected (no implicit coercion)");
        low_ir_free(heap, &ir);
        if (p.forms) heap.free_fn(heap.ctx, p.forms);
        proven_array_destroy(&p.diags); proven_array_destroy(&l.tokens); proven_array_destroy(&l.diags);
    }

    // ── numeric builtins: sqrt/abs/floor/ceil, fmod/min/max (tag-dispatched) ──
    {
        const char *src =
            "fn hyp input a u64 . input b u64 . output f64 .  do "
            "  return sqrt expr ((cast f64 a) * (cast f64 a)) + ((cast f64 b) * (cast f64 b)) . . end "
            "fn iclamp input x u64 . output u64 .  do return min (max x 3) 9 . end "
            "fn fwrap output f64 .  do return fmod 7.5 2.0 . end "
            "fn ffloor output f64 .  do return floor 2.7 . end "
            "fn iabs input x u64 . output u64 .  do return abs neg x . end "
            "fn mixed output f64 .  do return min 1 2.0 . end";
        low_lex_result_t l = LEX(src); proven_arena_reset(&arena);
        low_parse_result_t p = low_parse(nodes, heap, &l.tokens);
        low_ir_t ir = low_ir_build(heap, &p);
        check(ir.ok, "numeric builtins: lower with fixed arity");
        proven_i64 ab[2] = { 3, 4 };
        low_ir_run_result_t r = low_ir_run(&ir, proven_u8str_view_from_cstr("hyp"), ab, 2, heap, &ir.diags);
        // ★ `5.0`, not `5` — a f64 that prints as an int is the tool blurring the type.
        //   여섯 개의 기대값이 그 모호함을 **굳혀 두고 있었다.**
        check(r.ok && strcmp(r.text, "5.0") == 0, "numeric: sqrt lifts ints to float (hyp(3,4) = 5.0 — and it prints as a FLOAT)");
        proven_i64 one = 1, twelve = 12, five = 5;
        r = low_ir_run(&ir, proven_u8str_view_from_cstr("iclamp"), &one, 1, heap, &ir.diags);
        check(r.ok && r.value == 3, "numeric: min/max stay integral on ints (clamp low)");
        r = low_ir_run(&ir, proven_u8str_view_from_cstr("iclamp"), &twelve, 1, heap, &ir.diags);
        check(r.ok && r.value == 9, "numeric: min/max stay integral on ints (clamp high)");
        r = low_ir_run(&ir, proven_u8str_view_from_cstr("iabs"), &five, 1, heap, &ir.diags);
        check(r.ok && r.value == 5, "numeric: abs of a negated int is integral");
        r = low_ir_run(&ir, proven_u8str_view_from_cstr("fwrap"), NULL, 0, heap, &ir.diags);
        check(r.ok && strcmp(r.text, "1.5") == 0, "numeric: fmod 7.5 2.0 = 1.5");
        r = low_ir_run(&ir, proven_u8str_view_from_cstr("ffloor"), NULL, 0, heap, &ir.diags);
        check(r.ok && strcmp(r.text, "2.0") == 0, "numeric: floor 2.7 = 2.0 — a float, and it says so");
        r = low_ir_run(&ir, proven_u8str_view_from_cstr("mixed"), NULL, 0, heap, &ir.diags);
        check(!r.ok && HASDIAG(ir.diags, "E-VM-TYPE"),
              "numeric: min of an int and a float is rejected (no implicit coercion)");
        low_ir_free(heap, &ir);
        if (p.forms) heap.free_fn(heap.ctx, p.forms);
        proven_array_destroy(&p.diags); proven_array_destroy(&l.tokens); proven_array_destroy(&l.diags);
    }

    // ── float layouts + float SIMD lanes (RFC-0025 §6.6 / RFC-0040) ──
    {
        const char *src =
            "type bytes slice u8 . . "
            "struct sample do layout packed . temp f32 big . scale f64 . tag u8 . end "
            "fn mk output sample .  do "
            "  return make sample do temp 1.5 . scale 2.25 . tag 3 . end end "
            "fn rt output f64 .  do "
            "  var h sample . be view sample encode sample mk . . "
            "  return expr (field h temp) + (field h scale) . . end "
            "fn wire output u64 .  do return len encode sample mk . . end "
            "fn dot input b bytes . output f64 .  do "
            "  var xs u64 . be view_array f32 b . "
            "  var va vec f32 4 . be load xs 0 . "
            "  var vb vec f32 4 . be load xs 4 . "
            "  return reduce_add mul va vb . end";
        low_lex_result_t l = LEX(src); proven_arena_reset(&arena);
        low_parse_result_t p = low_parse(nodes, heap, &l.tokens);
        low_ir_t ir = low_ir_build(heap, &p);
        check(ir.ok, "float layout: f32/f64 struct fields lower");
        low_ir_run_result_t r = low_ir_run(&ir, proven_u8str_view_from_cstr("rt"), NULL, 0, heap, &ir.diags);
        check(r.ok && strcmp(r.text, "3.75") == 0,
              "float layout: encode→view→field round-trips f32 be + f64 (1.5 + 2.25)");
        r = low_ir_run(&ir, proven_u8str_view_from_cstr("wire"), NULL, 0, heap, &ir.diags);
        check(r.ok && r.value == 13, "float layout: packed wire length = 4 + 8 + 1");
        proven_i64 fb[32];   // [1,2,3,4] · [5,6,7,8] as little-endian f32 bytes
        {
            float xs[8] = { 1, 2, 3, 4, 5, 6, 7, 8 };
            proven_u8 raw[32];
            memcpy(raw, xs, 32);
            for (int i = 0; i < 32; i++) fb[i] = raw[i];
        }
        r = low_ir_run(&ir, proven_u8str_view_from_cstr("dot"), fb, 32, heap, &ir.diags);
        check(r.ok && strcmp(r.text, "70.0") == 0,
              "float SIMD: f32 lanes — view_array + load + lanewise mul + reduce_add = 70.0");
        low_ir_free(heap, &ir);
        if (p.forms) heap.free_fn(heap.ctx, p.forms);
        proven_array_destroy(&p.diags); proven_array_destroy(&l.tokens); proven_array_destroy(&l.diags);
    }

    // ── `align n` — alignment contract, enforced at the view boundary (RFC-0051 §5.1) ──
    {
        const char *src =
            "type bytes slice u8 . . "
            "struct reg do align 16 . ctrl u32 . stat u16 . end "
            "struct nat do ctrl u32 . stat u16 . end "
            "fn mk output reg .  do return make reg do ctrl 7 . stat 2 . end . end "
            "fn mkn output nat .  do return make nat do ctrl 7 . stat 2 . end . end "
            "fn wide output u64 .  do return len encode reg mk . . end "
            "fn narrow output u64 .  do return len encode nat mkn . . end "
            "fn tv_at input off u64 . input b bytes . output u64 .  do "
            "  var o reg . be try_view reg (subslice b off (len b)) . . "
            "  guard is_some o . else return 0 . . "
            "  var h reg . be some_value o . "
            "  return expr (field h ctrl) + 1000 . . end";
        low_lex_result_t l = LEX(src); proven_arena_reset(&arena);
        low_parse_result_t p = low_parse(nodes, heap, &l.tokens);
        low_ir_t ir = low_ir_build(heap, &p);
        check(ir.ok, "align: `align n .` clause lowers");
        low_ir_run_result_t r = low_ir_run(&ir, proven_u8str_view_from_cstr("narrow"), NULL, 0, heap, &ir.diags);
        check(r.ok && r.value == 8, "align: natural layout of u32+u16 is 8 bytes");
        r = low_ir_run(&ir, proven_u8str_view_from_cstr("wide"), NULL, 0, heap, &ir.diags);
        check(r.ok && r.value == 16, "align: `align 16` raises the trailing pad (8 → 16)");
        proven_i64 a2[25] = { 0, 7 };           // off=0 + 24 bytes: aligned base
        for (int i = 2; i < 25; i++) a2[i] = 0;
        r = low_ir_run(&ir, proven_u8str_view_from_cstr("tv_at"), a2, 25, heap, &ir.diags);
        check(r.ok && r.value == 1007, "align: try_view at an aligned base succeeds");
        a2[0] = 4;                              // off=4: 4 % 16 ≠ 0 → contract violated
        r = low_ir_run(&ir, proven_u8str_view_from_cstr("tv_at"), a2, 25, heap, &ir.diags);
        check(r.ok && r.value == 0, "align: try_view at a mis-aligned base returns none (contract)");
        low_ir_free(heap, &ir);
        if (p.forms) heap.free_fn(heap.ctx, p.forms);
        proven_array_destroy(&p.diags); proven_array_destroy(&l.tokens); proven_array_destroy(&l.diags);
    }

    // ── layout round-trip fuzz: random struct layouts/values, independent oracle ──
    {
        int viol = 0, first_bad = -1;
        for (int seed = 1; seed <= 200; seed++) {
            fz_state = (proven_u64)seed * 40503u + 7u;
            char src[2048]; proven_size_t sl = 0;
            #define FPUT(...) sl += (proven_size_t)snprintf(src + sl, sizeof src - sl, __VA_ARGS__)
            int nf = 1 + (int)fz_rnd(8);
            bool packed = fz_rnd(2);
            proven_u8 sizes[8]; bool bes[8], flts[8]; proven_u64 vals[8];
            FPUT("struct s do ");
            if (packed) FPUT("layout packed . ");
            proven_u64 expect_sum = 0;
            unsigned off = 0, maxal = 1;
            for (int i = 0; i < nf; i++) {
                static const proven_u8 szs[6] = { 1, 2, 4, 8, 4, 8 };
                static const char *tys[6] = { "u8", "u16", "u32", "u64", "f32", "f64" };
                int t = (int)fz_rnd(6);
                sizes[i] = szs[t]; bes[i] = fz_rnd(2); flts[i] = t >= 4;
                proven_u64 mask = sizes[i] >= 8 ? ~0ull : ((1ull << (8 * sizes[i])) - 1);
                // float fields carry small integers — exactly representable in f32,
                // so `cast u64` recovers them and the sum oracle still holds
                vals[i] = flts[i] ? fz_rnd(1000)
                                  : ((fz_state * 2862933555777941757ull + (proven_u64)i) & mask);
                expect_sum += vals[i];   // u64 wrap matches the VM's wrap semantics
                FPUT("f%d %s%s . ", i, tys[t], bes[i] ? " big" : " little");
                if (!packed) {           // independent oracle for the layout rules
                    if (off % sizes[i]) off += sizes[i] - off % sizes[i];
                    if (sizes[i] > maxal) maxal = sizes[i];
                }
                off += sizes[i];
            }
            if (!packed && off % maxal) off += maxal - off % maxal;
            FPUT("end fn mk output s .  do return make s do ");
            for (int i = 0; i < nf; i++) FPUT("f%d %llu . ", i, (unsigned long long)vals[i]);
            FPUT("end . end fn rt output u64 .  do "
                 "var h s . be view s encode s mk . . return ");
            #define FTERM(i) do { if (flts[i]) FPUT("(cast u64 (field h f%d))", i); else FPUT("(field h f%d)", i); } while (0)
            for (int i = 0; i < nf - 1; i++) { FPUT("add "); FTERM(i); FPUT(" ("); }
            FTERM(nf - 1);
            for (int i = 0; i < nf - 1; i++) FPUT(")");
            #undef FTERM
            FPUT(" . end fn blen output u64 .  do return len encode s mk . . end");
            #undef FPUT

            low_lex_result_t l = LEX(src); proven_arena_reset(&arena);
            low_parse_result_t p = low_parse(nodes, heap, &l.tokens);
            low_ir_t ir = low_ir_build(heap, &p);
            bool ok = ir.ok && p.ok;
            if (ok) {
                low_ir_run_result_t r = low_ir_run(&ir, proven_u8str_view_from_cstr("rt"), NULL, 0, heap, &ir.diags);
                ok = r.ok && (proven_u64)r.value == expect_sum;
                if (ok) {
                    r = low_ir_run(&ir, proven_u8str_view_from_cstr("blen"), NULL, 0, heap, &ir.diags);
                    ok = r.ok && r.value == (proven_i64)off;
                }
            }
            if (!ok) {
                viol++;
                if (first_bad < 0) { first_bad = seed; fprintf(stderr, "\nlayout fuzz violation, seed %d:\n%s\n", seed, src); }
            }
            low_ir_free(heap, &ir);
            if (p.forms) heap.free_fn(heap.ctx, p.forms);
            proven_array_destroy(&p.diags); proven_array_destroy(&l.tokens); proven_array_destroy(&l.diags);
        }
        printf("  (layout fuzz: 200 seeds — %d violations)\n", viol);
        check(viol == 0, "layout fuzz: encode→view round-trip + wire length hold (packed/native, be/le)");
    }

    // ── V1 fuzz: statically-green programs must run with 0 soundness violations ──
    {
        int green = 0, viol = 0, genbug = 0, first_bad = -1;
        for (int seed = 1; seed <= 400; seed++) {
            fz_t g;
            fz_gen(&g, (proven_u64)seed);
            low_lex_result_t l = LEX(g.buf); proven_arena_reset(&arena);
            low_parse_result_t p = low_parse(nodes, heap, &l.tokens);
            bool stat_ok = false;
            if (p.ok && !l.diags.len) {
                low_check_result_t c1 = low_check(heap, &p);
                low_typecheck_result_t c2 = low_typecheck(heap, &p);
                low_contract_result_t c3 = low_contract(heap, &p);
                low_region_result_t c4 = low_region(heap, &p);
                stat_ok = c1.ok && c2.ok && c3.ok && c4.ok;
                proven_array_destroy(&c1.diags); proven_array_destroy(&c2.diags);
                proven_array_destroy(&c3.diags); proven_array_destroy(&c4.diags);
            } else {
                genbug++;
                if (first_bad < 0) { first_bad = seed; fprintf(stderr, "\nfuzz generator bug, seed %d:\n%s\n", seed, g.buf); }
            }
            if (stat_ok) {
                green++;
                low_ir_t ir = low_ir_build(heap, &p);
                if (ir.ok) {
                    low_ir_run_result_t r = low_ir_run(&ir, proven_u8str_view_from_cstr("fmain"),
                                                       NULL, 0, heap, &ir.diags);
                    // 건전성 위반 = 정적 검사가 **놓친** 것. 오버플로·div0 트랩은
                    // 계약 실패이지 건전성 위반이 아니다(정적 검사가 약속한 적 없다).
                    // E-VM-ANALYSIS 는 구간 분석(RFC-0053)이 "제거 가능"이라 한 자리가
                    // 실제로 넘쳤다는 뜻 — **분석의 거짓음성**이므로 위반이다(§7).
                    bool unsound = false;
                    for (proven_size_t di = 0; di < ir.diags.len; di++) {
                        const low_diag_t *d = PROVEN_ARRAY_GET(&ir.diags, low_diag_t, di);
                        if (strcmp(d->code, "E-VM-EXCL") == 0 || strcmp(d->code, "E-VM-DANGLING") == 0 ||
                            strcmp(d->code, "E-VM-READONLY") == 0 || strcmp(d->code, "E-VM-ANALYSIS") == 0)
                            unsound = true;
                    }
                    if (!r.ok && unsound) {
                        viol++;
                        if (first_bad < 0) {
                            first_bad = seed;
                            fprintf(stderr, "\nfuzz SOUNDNESS violation, seed %d:\n%s\n", seed, g.buf);
                            for (proven_size_t di = 0; di < ir.diags.len; di++) {
                                const low_diag_t *d = PROVEN_ARRAY_GET(&ir.diags, low_diag_t, di);
                                fprintf(stderr, "  %s: %s\n", d->code, low_diag_text(d));
                            }
                        }
                    }
                }
                low_ir_free(heap, &ir);
            }
            if (p.forms) heap.free_fn(heap.ctx, p.forms);
            proven_array_destroy(&p.diags); proven_array_destroy(&l.tokens); proven_array_destroy(&l.diags);
        }
        printf("  (fuzz: 400 seeds — %d statically green, %d violations, %d generator bugs)\n",
               green, viol, genbug);
        check(genbug == 0, "V1 fuzz: generator emits well-formed programs (400 seeds)");
        check(viol == 0,
              "V1 fuzz: 0 soundness violations — incl. E-VM-ANALYSIS, so the fuzzer also "
              "validates RFC-0053's interval analysis (a false removal would show up here)");
        check(green >= 50, "V1 fuzz: healthy statically-green fraction");
    }

    // ── RFC-0053: 구간 분석 — 계약이 오버플로 검사를 제거한다 ──
    {
        const char *src =
            // 계약 없음 → 증명 불가(검사 잔존)
            "fn bare input a u8 . output u8 .  do return add a 1 . end "
            // ★ requires 가 구간을 좁힌다 → 검사 제거
            "fn proven input a u8 . output u8 . requires le a 200 .  do "
            "  return add a 1 . end "
            // 좁혀도 여전히 넘칠 수 있다 → 검사 잔존(보수적이되 건전)
            "fn tight input a u8 . output u8 . requires le a 254 .  do "
            "  return mul a 2 . end";
        low_lex_result_t l = LEX(src); proven_arena_reset(&arena);
        low_parse_result_t p = low_parse(nodes, heap, &l.tokens);
        low_ir_t ir = low_ir_build(heap, &p);
        check(ir.ok, "RFC-0053: interval analysis runs");
        check(ir.checks_total == 3, "RFC-0053: three overflow-check sites are seen");
        check(ir.checks_proven == 1,
              "RFC-0053: `requires le a 200` PROVES `add a 1` cannot overflow — the check is removed");
        proven_i64 p200 = 200, p255 = 255;
        low_ir_run_result_t pr0 = low_ir_run(&ir, proven_u8str_view_from_cstr("proven"), &p200, 1, heap, &ir.diags);
        check(pr0.ok && pr0.value == 201, "RFC-0053: the proven op still computes correctly");
        pr0 = low_ir_run(&ir, proven_u8str_view_from_cstr("bare"), &p255, 1, heap, &ir.diags);
        check(!pr0.ok && HASDIAG(ir.diags, "E-VM-OVERFLOW"),
              "RFC-0053: the unproven op keeps its check (sound: no false removal)");
        low_ir_free(heap, &ir);
        if (p.forms) heap.free_fn(heap.ctx, p.forms);
        proven_array_destroy(&p.diags); proven_array_destroy(&l.tokens); proven_array_destroy(&l.diags);
    }

    // ── RFC-0055: range 타입 — 계약이 시그니처에 실려 **op 경계를 넘는다** ──
    {
        const char *src =
            // range 0 100 → 폭은 u8 로 유도된다. [0,100]*2 = [0,200] ⊆ u8 ⇒ 검사 제거
            "fn scale input a range 0 100 . output u8 .  do return mul a 2 . end "
            // ★ 호출자가 증명한다: x ∈ [0,50] ⊆ [0,100] ⇒ **진입 검사 제거**(계약이 공짜)
            "fn good input x range 0 50 . output u8 .  do return scale x . end "
            // 증명 못 한다: y ∈ [0,255] ⊄ [0,100] ⇒ 검사 유지
            "fn bad input y u8 . output u8 .  do return scale y . end";
        low_lex_result_t l = LEX(src); proven_arena_reset(&arena);
        low_parse_result_t p = low_parse(nodes, heap, &l.tokens);
        low_ir_t ir = low_ir_build(heap, &p);
        check(ir.ok, "range: fixture lowers (width derived from the range)");
        check(ir.checks_total == 3 && ir.checks_proven == 2,
              "range: the arithmetic AND the proven call site are discharged; "
              "the unproven call keeps its check (op-boundary propagation)");
        proven_i64 v50 = 50, v200 = 200;
        low_ir_run_result_t r = low_ir_run(&ir, proven_u8str_view_from_cstr("good"), &v50, 1, heap, &ir.diags);
        check(r.ok && r.value == 100, "range: the proven call runs with NO entry check");
        r = low_ir_run(&ir, proven_u8str_view_from_cstr("bad"), &v50, 1, heap, &ir.diags);
        check(r.ok && r.value == 100, "range: the unproven call runs when the contract holds do");
        r = low_ir_run(&ir, proven_u8str_view_from_cstr("bad"), &v200, 1, heap, &ir.diags);
        check(!r.ok && HASDIAG(ir.diags, "E-VM-CONTRACT"),
              "range: a violated parameter range traps at the call (the fact is enforced)");
        low_ir_free(heap, &ir);
        if (p.forms) heap.free_fn(heap.ctx, p.forms);
        proven_array_destroy(&p.diags); proven_array_destroy(&l.tokens); proven_array_destroy(&l.diags);
    }

    // ── RFC-0055 D7: `range τ lo hi` — 타입을 적으면 **표현이 못 박히고 강제된다** ──
    // 두 축을 나눈다: 표현(τ)은 **정적으로 강제**, 범위는 증명하거나 검사한다.
    {
        // τ 를 적으면 폭 유도(u8)를 덮어쓴다 ⇒ [0,100]*1000 = [0,100000] 이 u32 안에 든다.
        // 유도형(range 0 100 → u8)이었다면 이 곱은 **트랩**했을 것이다. 표현은 프로그래머의 것이다.
        const char *src =
            "fn wide input a range u32 0 100 . output u32 .  do return mul a 1000 . end "
            "fn drive input x range u32 0 100 . output u32 .  do return wide x . end";
        low_lex_result_t l = LEX(src); proven_arena_reset(&arena);
        low_parse_result_t p = low_parse(nodes, heap, &l.tokens);
        low_ir_t ir = low_ir_build(heap, &p);
        check(ir.ok, "range τ: `range u32 0 100` lowers (the declared type pins the representation)");
        check(ir.checks_total == 2 && ir.checks_proven == 2,
              "range τ: both the arithmetic and the call are discharged (100000 fits u32 — "
              "under the derived width u8 this same code would have trapped)");
        proven_i64 v100 = 100;
        low_ir_run_result_t r = low_ir_run(&ir, proven_u8str_view_from_cstr("drive"), &v100, 1, heap, &ir.diags);
        check(r.ok && r.value == 100000, "range τ: the pinned representation carries the value (no trap)");
        low_ir_free(heap, &ir);
        if (p.forms) heap.free_fn(heap.ctx, p.forms);
        proven_array_destroy(&p.diags); proven_array_destroy(&l.tokens); proven_array_destroy(&l.diags);
    }
    // 선언이 거짓말이면 프로그램이 서지 않는다 (★ twf / E-TYPE-RANGE — Coq: twf_repr).
    check(TYCK("fn f input a range u8 0 300 . output u8 .  do return a . end") == false,
          "range τ: `range u8 0 300` → E-TYPE-RANGE (u8 cannot hold 300 — the declaration is a lie)");
    check(TYCK("fn f input a range u8 -1 10 . output u8 .  do return a . end") == false,
          "range τ: `range u8 -1 10` → E-TYPE-RANGE (sign is enforced too)");
    check(TYCK("fn f input a range i16 -50 150 . output i16 .  do return a . end") == true,
          "range τ: `range i16 -50 150` is well-formed (a signed range in a signed type)");
    check(TYCK("fn f input a range 10 0 . output u8 .  do return a . end") == false,
          "range τ: an empty range (lo > hi) → E-TYPE-RANGE");
    check(TYCK("fn f input a range f32 0 10 . output f32 .  do return a . end") == false,
          "range τ: a range must refine an INTEGER type (no float ranges)");
    check(TYCK("fn f output u8 .  do var i range 0 9 be 20 . return i . end") == false,
          "range τ: a literal outside its own declared range → E-TYPE-RANGE (the range is the contract, not do the width)");
    check(TYCK("fn f output u8 .  do var i range 0 9 be 7 . return i . end") == true,
          "range τ: a literal inside the declared range is fine");
    // ★ 이것이 사용자가 물은 구멍이었다: range 파라미터가 TK_NAMED 로 떨어져 **타입이 안 걸렸다.**
    check(TYCK("fn g input a range u8 0 100 . output u8 .  do return a . end "
               "fn f input z f64 . output u8 .  do return g z . end") == false,
          "range τ: passing an f64 into a range parameter is a TYPE error "
          "(before D7 the range type was TK_NAMED and this was silently accepted)");
    check(TYCK("fn g input a range u8 0 100 . output u8 .  do return a . end "
               "fn f input z i32 . output u8 .  do return g z . end") == false,
          "range τ: i32 → range u8 is a sign/width error (the ⊑ lattice applies to range types too)");
    // ★ 서로소 범위 = 어떤 값도 만족시킬 수 없다 ⇒ 검사가 아니라 오류다 (Coq: rdisj_no_value).
    check(TYCK("fn g input a range u8 0 100 . output u8 .  do return a . end "
               "fn f input z range u8 200 255 . output u8 .  do return g z . end") == false,
          "range τ: disjoint ranges → E-TYPE-RANGE (no value can satisfy it — a lie, not a check)");
    check(TYCK("fn g input a range u8 0 100 . output u8 .  do return a . end "
               "fn f input z range u8 0 50 . output u8 .  do return g z . end") == true,
          "range τ: [0,50] ⊆ [0,100] with the same representation — implicit and check-free");

    // ── ★★ RFC-0009 DET-1 — `parallel <s> split .` 의 **Bernstein 조건을 검사한다** ──
    // LowentPar.v 가 증명한 것: 태스크들이 서로의 읽기/쓰기 집합을 건드리지 않으면
    // **병렬 결과는 순차와 비트 동일하다**(det1_par_eq_seq, Qed). 그리고 겹치면 순서가
    // 결과를 바꾼다(overlap_is_nondeterministic, Qed).
    // ⇒ 컴파일러가 할 일은 하나다: **그 조건이 성립하는지 검사하는 것.**
    // ★ 그리고 DET-1 덕분에 **순차 실행이 곧 올바른 구현**이다 — 검사부터 넣고 실행은
    //   나중에 붙여도 의미가 바뀌지 않는다. 증명이 사 준 자유다.
    check(CHECK("proc dbl input s mut slice u8 . . output u64 . effects none . parallel s split . do "
                "  var i u64 be 0 . "
                "  while lt i (len s) . do set (index s i) expr (index s i) * 2 . . "
                "    set i expr i + 1 . . end "
                "  return len s . end") == true,
          "parallel: ★ an independent loop passes the Bernstein check (each iteration touches only "
          "its OWN element) — the compiler reports W-PAR-OK and cites the Coq theorem");
    check(CHECK("proc blur input s mut slice u8 . . output u64 . effects none . parallel s split . do "
                "  var i u64 be 1 . "
                "  while lt i (len s) . do set (index s i) expr (index s i) + (index s 0) . . "
                "    set i expr i + 1 . . end "
                "  return len s . end") == false,
          "parallel: ★ reading ANOTHER index → E-PAR-READ (rd ∩ wr ≠ ∅ — a cross-iteration "
          "dependence; DET-1's premise fails and the parallel result would NOT match sequential)");
    check(CHECK("proc sum input s mut slice u8 . . output u64 . effects none . parallel s split . do "
                "  var acc u64 be 0 . var i u64 be 0 . "
                "  while lt i (len s) . do set acc expr acc + (index s i) . . "
                "    set i expr i + 1 . . end "
                "  return acc . end") == false,
          "parallel: ★ writing an accumulator that lives across iterations → E-PAR-CARRY "
          "(a loop-carried dependence — a reduction must be declared, and its tree fixed: DET-3)");
    check(CHECK("proc f input s mut slice u8 . . output u64 . effects none . do "
                "  var acc u64 be 0 . var i u64 be 0 . "
                "  while lt i (len s) . do set acc expr acc + (index s i) . . "
                "    set i expr i + 1 . . end "
                "  return acc . end") == true,
          "parallel: without the `parallel` clause the same loop is fine — the check only bites "
          "where the programmer ASKED for a split (no over-rejection)");

    // ── ★★ DET-3 — reduction 의 트리를 고정해야 하는가? **연산이 결합적이냐**에 달렸다 ──
    // ★ assoc_shape_free (Qed)       결합적이면 트리 모양이 결과를 **안 바꾼다** → 자유롭게 쪼갠다
    // ★ nonassoc_shape_matters (Qed) 결합적이지 않으면 모양이 결과를 **바꾼다** → 쪼갤 수 없다
    // E-PAR-CARRY 가 "reduction 을 선언하라" 고 말했는데 **그 문법이 없었다.**
    // 도구가 있지도 않은 것을 시키고 있었다. 만들었다 — 그리고 DET-3 을 그 위에 걸었다.
    check(CHECK("proc isum input s mut slice u8 . . output u64 . effects none . "
                " parallel s split . reduce acc add . do "
                "  var acc u64 be 0 . var i u64 be 0 . "
                "  while lt i (len s) . do set acc expr acc + (index s i) . . "
                "    set i expr i + 1 . . end return acc . end") == true,
          "reduce: ★ an INTEGER `add` reduction may be split — add is associative, so the tree "
          "shape cannot change the answer (assoc_shape_free, Qed)");
    check(CHECK("proc fsum input s mut slice u8 . . output f64 . effects none . "
                " parallel s split . reduce acc add . do "
                "  var acc f64 be 0.0 . var i u64 be 0 . "
                "  while lt i (len s) . do set acc expr acc + (index s i) . . "
                "    set i expr i + 1 . . end return acc . end") == false,
          "reduce: ★★ a FLOAT reduction is REJECTED → E-PAR-FLOAT. Float addition is not "
          "associative, so splitting changes the tree and the answer would depend on the SCHEDULE "
          "(nonassoc_shape_matters, Qed). Determinism is part of the meaning, not a detail");
    check(CHECK("proc bad input s mut slice u8 . . output u64 . effects none . "
                " parallel s split . reduce acc sub . do "
                "  var acc u64 be 0 . var i u64 be 0 . "
                "  while lt i (len s) . do set acc expr acc - (index s i) . . "
                "    set i expr i + 1 . . end return acc . end") == false,
          "reduce: ★ a NON-ASSOCIATIVE operator (`sub`) is rejected → E-PAR-ASSOC (the theorem "
          "names exactly this: the tree shape decides the answer)");

    // ── ★ 슬라이스 **원소 쓰기** — 커널·level-1 병렬(R2)의 전제 ──
    // 지금까지 `set` 은 지역 이름만 받았다. 그래서 **커널이 결과를 쓸 데가 없었고**,
    // R2 의 의미론은 증명돼 있는데(LowentPar.v) 그것을 실을 그릇이 없었다.
    // 장소는 괄호로 감싼 form 이다: `set (index s i) v .` (점-클로저에서 중첩은 괄호가 만든다).
    {
        const char *src =
            "proc dbl input s mut slice u8 . . output u64 . effects none . do "
            "  var i u64 be 0 . "
            "  while lt i (len s) . do "
            "    set (index s i) expr (index s i) * 2 . . "
            "    set i expr i + 1 . . end "
            "  var acc u64 be 0 . var j u64 be 0 . "
            "  while lt j (len s) . do set acc expr acc + (index s j) . . set j expr j + 1 . . end "
            "  return acc . end "
            "fn poke input i u64 . input s mut slice u8 . . output u64 .  do "
            "  set (index s i) 9 . return len s . end";
        low_lex_result_t l = LEX(src); proven_arena_reset(&arena);
        low_parse_result_t p = low_parse(nodes, heap, &l.tokens);
        low_ir_t ir = low_ir_build(heap, &p);
        check(ir.ok, "slice write: `set (index s i) v .` lowers (the S5 core had no place form)");
        proven_i64 a3[3] = { 1, 2, 3 };
        low_ir_run_result_t r = low_ir_run(&ir, proven_u8str_view_from_cstr("dbl"), a3, 3, heap, &ir.diags);
        check(r.ok && r.value == 12,
              "slice write: ★ the kernel shape works — double every element in place, then sum "
              "(2+4+6 = 12). This is what R2's proven semantics needed a vessel for");
        // ★ 읽기와 **같은 관계 사실**이 쓰기의 경계 검사도 없앤다 (idx_no_check, Qed).
        check(ir.checks_proven >= 3,
              "slice write: the loop guard `lt i (len s)` discharges the WRITE's bounds check too "
              "— the same relational fact, the same Coq theorem");
        proven_i64 bad[4] = { 9, 1, 2, 3 };
        r = low_ir_run(&ir, proven_u8str_view_from_cstr("poke"), bad, 4, heap, &ir.diags);
        check(!r.ok && HASDIAG(ir.diags, "E-VM-BOUNDS"),
              "slice write: an unguarded out-of-bounds write traps (the check was kept)");
        // 소진 검사 — 제거된 쓰기 검사가 실제로 틀린 적이 있는가.
        int accused = 0;
        for (proven_i64 len = 0; len <= 10; len++) {
            proven_i64 args[10];
            for (proven_i64 k = 0; k < len; k++) args[k] = 1 + k;
            (void)low_ir_run(&ir, proven_u8str_view_from_cstr("dbl"), args, (proven_size_t)len, heap, &ir.diags);
        }
        if (HASDIAG(ir.diags, "E-VM-ANALYSIS")) accused++;
        check(accused == 0,
              "slice write: no false elimination on any length 0..10 (a wrong removal would be an "
              "out-of-bounds WRITE in native code — worse than a read)");
        low_ir_free(heap, &ir);
        if (p.forms) heap.free_fn(heap.ctx, p.forms);
        proven_array_destroy(&p.diags); proven_array_destroy(&l.tokens); proven_array_destroy(&l.diags);
    }
    // ★ 가변성은 **정적으로** 강제된다 — 런타임 슬라이스 값에는 mut 표시가 없다(포인터가 const).
    check(TYCK("fn f input s slice u8 . output u64 .  do "
               "  set (index s 0) 9 . return len s . end") == false,
          "slice write: ★ writing an element of a NON-mut slice → E-TYPE-MUT (a shared slice is "
          "read-only; mutability is enforced statically because the runtime value has no mut flag)");
    check(TYCK("proc f input s mut slice u8 . . output u64 . effects none . do "
               "  set (index s 0) 9 . return len s . end") == true,
          "slice write: a `mut` slice accepts the write");

    // ── ★ effect 어휘는 **닫혀 있다** — 오타가 op 을 조용히 순수로 만든다 ──
    // effect_of_word 가 모르는 낱말을 **조용히 none 으로 삼켰다.**
    // `effects io` 를 `effects lo` 로 오타 내면 op 이 **순수로 선언된다.** 선언이 뜻을 바꾼다.
    check(CHECK("fn f output u8 . effects no_such_effect . do return 1 . end") == false,
          "effects: ★ an unknown effect word → E-EFFECT-UNDEF (the vocabulary is closed; a typo "
          "here used to silently declare the op PURE)");
    // ★ 2026-07-24: 위(§`proc doing io`)와 **같은 낡음**이 여기 한 번 더 있었다 — `effects io`
    //   는 그것을 인가하는 `cap` 을 요구하고(E-EFFECT-NO-CAP), `print` 는 op 이 아니다.
    check(CHECK("proc g input k cap io . effects io . do var n u64 be write_out k \"x\" . return . end") == true,
          "effects: a real effect word still passes");
    check(CHECK("fn f output u8 .  do return 1 . end") == true,
          "effects: `none` is a real effect word");

    // ── ★ `errors` 절이 거는 이름은 선언된 enum 변형이어야 한다 ──
    // 지금까지 **본문 쪽만** 봤다(선언 안 한 오류를 반환하면 E-ERR-UNDECLARED).
    // 절 자체는 안 봤다 — enum 에 없는 이름을 걸어도 조용히 통과했다.
    check(CTCK("enum e do a end "
               "fn f output result u8 e . .  errors ghost . "
               "do return ok 1 . end") == false,
          "errors clause: ★ a name the declared error enum does not contain → E-ERR-UNDEF");
    check(CTCK("enum e do a end "
               "fn f output result u8 e . .  errors a . "
               "do return ok 1 . end") == true,
          "errors clause: a real variant passes");
    check(CTCK("fn f input d slice u8 . errors bare_name . "
               "do return error bare_name . end") == true,
          "errors clause: the enum-less form (an error name with no enum) is still allowed — "
          "the check only bites when the output names a DECLARED enum (no over-rejection)");

    // ── ★★ enum 값 + `match` — S5 코어에 **없던 것** (언어 완결성) ──
    // enum 값의 표현: **변형의 인덱스**(정수). 런타임 태그가 없다 — 비용 가시.
    // match 는 `eq` + 분기 사슬로 낮아진다. if 사슬과 코드가 같다.
    // ★ 그런데 match 가 if 사슬보다 나은 이유가 하나 있다: **완전성**.
    //   나중에 enum 에 변형을 추가하면 **컴파일러가 빠진 곳을 전부 찾아 준다.**
    //   그 검사가 없으면 match 는 그냥 if 사슬이고, 새 변형이 조용히 빠진다.
    {
        const char *src =
            "enum color do\n red .\n green .\n blue .\nend\n"
            "fn code input c color . output u8 .  do "
            "  match c do "
            "    case red . do return 10 . end "
            "    case green . do return 20 . end "
            "    case blue . do return 30 . end "
            "  end end "
            "fn pick input n u8 . output color .  do "
            "  if lt n 5 . do return red . end return blue . end";
        low_lex_result_t l = LEX(src); proven_arena_reset(&arena);
        low_parse_result_t p = low_parse(nodes, heap, &l.tokens);
        low_ir_t ir = low_ir_build(heap, &p);
        check(ir.ok, "match: enum values and `match` lower (they were outside the S5 core)");
        for (proven_i64 v = 0; v <= 2; v++) {
            low_ir_run_result_t r = low_ir_run(&ir, proven_u8str_view_from_cstr("code"), &v, 1, heap, &ir.diags);
            check(r.ok && r.value == 10 * (v + 1),
                  "match: each variant takes its own arm (the value IS the variant index — no "
                  "runtime tag, cost visible)");
        }
        proven_i64 one = 1, nine = 9;
        low_ir_run_result_t r = low_ir_run(&ir, proven_u8str_view_from_cstr("pick"), &one, 1, heap, &ir.diags);
        check(r.ok && r.value == 0, "match: an enum value in VALUE position works (red = 0)");
        r = low_ir_run(&ir, proven_u8str_view_from_cstr("pick"), &nine, 1, heap, &ir.diags);
        check(r.ok && r.value == 2, "match: blue = 2");
        low_ir_free(heap, &ir);
        if (p.forms) heap.free_fn(heap.ctx, p.forms);
        proven_array_destroy(&p.diags); proven_array_destroy(&l.tokens); proven_array_destroy(&l.diags);
    }
    // ★★ 완전성 — 이것이 match 의 값어치다.
    check(CHECK("enum e do\n a .\n b .\nend\n"
                "fn f input c e . output u8 .  do "
                "  match c do case a . do return 1 . end end end end") == false,
          "match: ★★ a missing variant → E-MATCH-INEXHAUSTIVE. THIS is what a `match` buys over an "
          "if-chain: add a variant to the enum later and the compiler finds every place that must "
          "change");
    check(CHECK("enum e do\n a .\n b .\nend\n"
                "fn f input c e . output u8 .  do "
                "  match c do case a . do return 1 . end case ghost . do return 2 . end "
                "  case b . do return 3 . end end end end") == false,
          "match: ★ a `case` naming a variant the enum does not contain → E-MATCH-UNDEF");
    check(CHECK("enum e do\n a .\n b .\nend\n"
                "fn f input c e . output u8 .  do "
                "  match c do case a . do return 1 . end case b . do return 2 . end end end end") == true,
          "match: an exhaustive match passes (no false positive)");

    // ── ★★ `slice T` 의 **원소 타입** — 지금까지 조용히 바이트로 취급됐다 ──
    // `slice u32` 가 컴파일되고 실행되고 **틀린 값을 냈다**:
    //     len(slice u32, 원소 4개) = 16   ← 바이트 수 (4 여야 한다)
    //     index(s, 1)              = 0    ← 바이트 1번 (원소 1 = 2 여야 한다)
    // **미구현보다 나쁘다.** 미구현은 거부라도 하지만, 이것은 **거짓말을 실행했다.**
    // 기계장치는 이미 있었다(IRW_VARRAY — view_array 가 쓰던 것). 진입에서 씌운다.
    {
        const char *src =
            "fn count input s slice u32 . output u64 .  do return len s . end "
            "fn at input i u64 . input s slice u32 . output u32 .  do "
            "  return index s i . end "
            "proc dbl input s mut slice u32 . . output u32 . effects none . do "
            "  var i u64 be 0 . "
            "  while lt i (len s) . do set (index s i) expr (index s i) * 2 . . "
            "    set i expr i + 1 . . end "
            "  return index s 1 . end";
        low_lex_result_t l = LEX(src); proven_arena_reset(&arena);
        low_parse_result_t p = low_parse(nodes, heap, &l.tokens);
        low_ir_t ir = low_ir_build(heap, &p);
        check(ir.ok, "slice T: a typed slice lowers");
        // u32 [1,2,3,4] = 16 bytes, little-endian
        proven_i64 b[16] = { 1,0,0,0, 2,0,0,0, 3,0,0,0, 4,0,0,0 };
        low_ir_run_result_t r = low_ir_run(&ir, proven_u8str_view_from_cstr("count"), b, 16, heap, &ir.diags);
        check(r.ok && r.value == 4,
              "slice T: ★ `len` counts ELEMENTS, not bytes (it used to say 16)");
        proven_i64 a1[17] = { 1, 1,0,0,0, 2,0,0,0, 3,0,0,0, 4,0,0,0 };
        r = low_ir_run(&ir, proven_u8str_view_from_cstr("at"), a1, 17, heap, &ir.diags);
        check(r.ok && r.value == 2,
              "slice T: ★ `index s 1` reads ELEMENT 1 (=2), not byte 1 (=0)");
        r = low_ir_run(&ir, proven_u8str_view_from_cstr("dbl"), b, 16, heap, &ir.diags);
        check(r.ok && r.value == 4,
              "slice T: ★ writing an element ENCODES it at the right width (element 1: 2*2 = 4)");
        low_ir_free(heap, &ir);
        if (p.forms) heap.free_fn(heap.ctx, p.forms);
        proven_array_destroy(&p.diags); proven_array_destroy(&l.tokens); proven_array_destroy(&l.diags);
    }
    // ★ 그리고 타입체커가 원소 타입을 안다 — `index` 의 결과는 슬라이스가 정한다.
    check(TYCK("fn f input s slice u32 . output u8 .  do "
               "  var x u8 be index s 0 . . return x . end") == false,
          "slice T: ★ a u32 element into a u8 is a narrowing → E-TYPE-WIDTH (before this, `index` "
          "was ALWAYS u8 no matter what the slice held)");
    check(TYCK("fn f input s slice u32 . output u32 .  do "
               "  var x u32 be index s 0 . . return x . end") == true,
          "slice T: the element type flows out of the slice type");

    // ── ★ 마스크는 **진짜 타입**이다 — bool 도 정수도 아니다 (레인마다 하나씩) ──
    // 벡터 비교의 결과가 마스크인데, 지금까지 마스크는 **아무 타입도 아니었다.**
    // vm_vec 픽스처가 실제로 `var m u64 . be gt v lim .` 라고 적어 놨다 — **마스크를 정수로**.
    // 그래서 select 에 아무거나 넘겨도·any 에 정수를 넘겨도 통과했다.
    check(TYCK("type bytes slice u8 . . "
               "fn f input b bytes . output bool .  do "
               "  var n u64 be 5 . return any n . end") == false,
          "mask: ★ `any` on an integer → E-TYPE-MASK (a mask is the result of a lanewise compare, "
          "not a number)");
    // ★ 2026-07-24: `var xs u64 . be view_array u32 b .` 였다 — 초기식은 **슬라이스**인데 선언은
    //   스칼라라 이제 E-TYPE-VAR 다(타입검사가 자랐고 픽스처가 안 따라가 유닛 게이트가 빨간불이었다).
    check(TYCK("type bytes slice u8 . . "
               "fn f input b bytes . output bool .  do "
               "  var xs slice u32 . be view_array u32 b . "
               "  var v vec u32 4 . be load xs 0 . "
               "  var lim vec u32 4 . be splat 10 . "
               "  var m mask 4 . be gt v lim . "
               "  return any m . end") == true,
          "mask: a real `mask 4` from a lanewise compare passes (and vm_vec now declares it that "
          "way — it used to say `u64`)");
    // ★ 여기도 `xs` 를 고친다 — 안 고치면 E-TYPE-VAR 로 거부돼 **정작 재려던 E-TYPE-MASK 를
    //   안 재고도 통과한다**(맞는 답, 틀린 이유 — 이 프로젝트가 가장 싫어하는 초록불이다).
    check(TYCK("type bytes slice u8 . . "
               "fn f input b bytes . output u64 .  do "
               "  var xs slice u32 . be view_array u32 b . "
               "  var v vec u32 4 . be load xs 0 . "
               "  var lim vec u32 4 . be splat 10 . "
               "  var cc vec u32 4 . be select v lim v . "
               "  return reduce_add cc . end") == false,
          "mask: ★ `select` whose first argument is a VECTOR, not a mask → E-TYPE-MASK");

    // ── ★ `for x <seq>` — 대상은 슬라이스여야 하고, 루프 변수는 환경에 있어야 한다 ──
    // 지금까지 둘 다 안 봤다. `for x <정수>` 가 통과하고 **런타임에서야** 잡혔다
    // (E-VM-TYPE: len needs a slice). 타입 오류를 런타임까지 미루는 것은 이 언어가 아니다.
    // 그리고 x 가 환경에 **없어서**, 본문에서 x 를 어떻게 쓰든 검사되지 않았다.
    check(TYCK("fn f input n u64 . output u64 .  do "
               "  var acc u64 be 0 . "
               "  for x n . do set acc expr acc + x . . end "
               "  return acc . end") == false,
          "for: ★ iterating a NON-slice → E-TYPE-ITER (before this it passed --check and only the "
          "VM caught it, at run time)");
    check(TYCK("fn f input s slice u8 . output u64 .  do "
               "  var acc u64 be 0 . "
               "  for x s . do set acc expr acc + x . . end "
               "  return acc . end") == true,
          "for: iterating a slice is fine, and the loop variable is now bound in the environment");
    // ★ 루프 변수는 **슬라이스의 원소 타입**을 갖는다(u8 고정이었다).
    check(TYCK("fn f input s slice u32 . output u8 .  do "
               "  var acc u8 be 0 . for x s . do set acc x . end return acc . end") == false,
          "for: ★ the loop variable carries the slice's ELEMENT type — a u32 element into a u8 is a "
          "narrowing (it used to be hard-coded u8 no matter what the slice held)");
    check(TYCK("fn f input s slice u32 . output u32 .  do "
               "  var acc u32 be 0 . for x s . do set acc x . end return acc . end") == true,
          "for: the element type flows into the loop variable");

    // ★★ 문법에 없는 철자 — 구조체는 `struct N … end` 로만 선언된다(SPEC-002 §211-213).
    //   `type N is struct …` 이 조용히 통과하면서 **레이아웃 없는 두 번째 구조체**를 만들었다.
    check(TYCK("type pt struct . field x u8 . field y u8 . . "
               "fn f output u8 .  do return 0 . end") == false,
          "type: ★ `type N is struct …` is NOT the grammar — a struct is `struct N do … end`. "
          "The bad spelling built a layout-less thing that cannot be viewed and cannot cross "
          "the boundary; this language has no such type (E-TYPE-DECL)");
    check(TYCK("type c enum . variant red . "
               "fn f output u8 .  do return 0 . end") == false,
          "type: an enum is `enum N do … end`, not `type N is enum …`");
    check(TYCK("type pct be u8 . fn f output u8 . do return 0 . end") == false &&
          TYCK("newtype pid be u32 . fn f output u8 . do return 0 . end") == false &&
          TYCK("type pct u8 . newtype pid u32 . fn f output u8 . do return 0 . end") == true,
          "type: ★ `type N be T` / `newtype N be T` is REFUSED (E-TYPE-DECL) — `be` binds a value; one meaning, one spelling");
    check(CHECK("proc f output u64 . effects atomic . do return 0 . end") == false &&
          CHECK("proc f input k cap atomic . output u64 . effects atomic . do return 0 . end") == true,
          "effects: ★ `effects atomic` needs `cap atomic` (E-ATOMIC-NOCAP) — the same pair as io/alloc/heap");
    check(TYCK("struct pt do x u8 . y u8 . end type p2 pt . "
               "fn f input q p2 . output u8 .  do return field q x . end") == true,
          "type: a TRANSPARENT alias to a struct is still that struct (SPEC-004 §89)");

    // ★★ 효과는 **호출을 통해 전파된다** (SPEC-006 §36). 전파된 적이 없었다 —
    //   호출 이름은 form 의 머리에 오지 않고(괄호로 싸지 않는 문법), 인자 없는 호출은 그냥 원자다.
    check(CHECK("proc w output u8 . effects io . do return 1 . end "
              "fn c output u8 .  do return w . end") == false,
          "effects: ★ a pure fn calling an `effects io` proc from RETURN position is an EFFECT "
          "LEAK — statement position was caught (`do p . end` puts p at a form HEAD), but a call "
          "inside `return` is not a head, and a zero-arg call is a bare atom. Effects never "
          "propagated there at all");
    check(CHECK("proc w input a u8 . output u8 . effects io . do return a . end "
              "fn c output u8 .  do return w 1 . end") == false,
          "effects: …and with arguments too (calls are not parenthesised — the name is not a head)");
    check(CHECK("proc w input k cap io . input a u8 . output u8 . effects io . do return a . end "
              "proc c input k cap io . input a u8 . output u8 . effects io . do return w k a . end") == true,
          "effects: a proc that DECLARES io may call an io op — the check is subsumption, not a ban "
          "(둘 다 `cap io` 를 든다 — E-EFFECT-NO-CAP, RFC-0007 §6.7)");
    check(CHECK("fn c output u8 . effects io . do return 1 . end") == false,
          "effects: ★ `fn + effects io` is structurally rejected (SPEC-003 §33) — the "
          "pure/procedural bit is the guarantee callers depend on (§27)");

    // ★★ 지역이 최상위 이름을 **가릴 수 없다** — 이름공간이 평면이기 때문이다.
    //   (그리고 이것이 위의 효과 전파를 성립시킨다: 원자가 op 이름이면 **그것은 그 op 다.**
    //    가림이 가능하면 그 추론이 깨진다 — 두 규칙은 한 몸이다.)
    check(CHECK("proc w input k cap io . output u8 . effects io . do return 1 . end "
                "fn c output u8 .  do var w u8 be 3 . return w . end") == false,
          "names: ★ a local may not take a top-level name — the VM silently picked the LOCAL "
          "(returned 3) while the effect checker saw the op. Same sin as the module `g` calling "
          "another module's `f`: the resolver silently picks one of the two (E-NAME-SHADOW)");
    check(CHECK("proc w input k cap io . output u8 . effects io . do return 1 . end "
                "fn c output u8 .  do var q u8 be 3 . return q . end") == true,
          "names: a local with its own name is fine");

    // ★★ 수식자를 붙이면 **op 이 통째로 사라졌다** (SPEC-003 §21).
    //   `unsafe`·`local`·`extern` 은 어휘에 없어서 선언을 조각냈고, `export` 는 op 을 아래
    //   중첩시켜 최상위가 op 이 아니게 됐다. 소비자는 전부 kids[0]->kw == CALCOP 로 찾는다.
    check(CHECK("export fn f output u8 .  do return 7 . end "
                "fn c output u8 .  do return f . end") == true,
          "modifiers: ★ an `export` op EXISTS and is callable — it used to vanish (check ok, "
          "IR 0 defs, \"no such op\"): a declared op that does not exist");
    check(CHECK("unsafe proc f output u8 . effects unsafe . do return 7 . end "
                "unsafe proc c output u8 . effects unsafe . do return f . end") == true,
          "modifiers: `unsafe`/`local`/`extern` were not even in the lexicon — the declaration "
          "was SHREDDED at the first `.`");
    // ★★★ **`unsafe` 규율** — 수식자가 **장식**이었다(2026-07-14).
    check(CHECK("proc f output u8 . effects unsafe . do return 7 . end") == false,
          "unsafe: ★★★ declaring the `unsafe` EFFECT without the `unsafe` MODIFIER is an error "
          "(E-UNSAFE-UNDECLARED). The effect says WHAT it does; the modifier says WHO SIGNED FOR IT. "
          "An unsafe op nobody signed for is exactly the hole the discipline exists to close");
    check(CHECK("unsafe proc f output u8 . effects none . do return 7 . end") == false,
          "unsafe: ★ and an `unsafe` that buys NOTHING is also an error (E-UNSAFE-UNUSED) — a false "
          "alarm, and false alarms are how real ones stop being read");

    // ★ `satisfies` 가 **없는 trait** 를 가리켜도 통과했다 (SPEC-003 §60).
    check(CHECK("fn f satisfies nope . output u8 .  do return 1 . end") == false,
          "trait: ★ `satisfies` naming an undeclared trait is an ERROR — a claim that points at "
          "nothing checks nothing (trait/satisfies were not even in the lexicon)");
    // ★★★ **구조적 충족을 실제로 검사한다** (2026-07-14 — W-NOT-YET 를 갚았다).
    //   그리고 **충족하는 것은 타입이다, op 이 아니다** — RFC-0062 가 타입에 op 을 붙여 준
    //   순간 "op 하나가 trait 을 충족한다" 는 말은 **뜻을 잃었다.**
    check(CHECK("trait shape do area end "
                "fn f satisfies shape . output u8 .  do return 1 . end") == false,
          "trait: ★★★ an OP does not satisfy a trait — a TYPE does (E-TRAIT-RECV). This used to "
          "pass with a W-NOT-YET confession; a confession is not a fix");
    check(CHECK("trait shape do area input s self . output u64 . effects none . end "
                "struct rect do satisfies shape . w u8 . end "
                "fn rect.area input s rect . output u64 .  "
                " do return widen u64 (field s w) . end") == true,
          "trait: ★★★ a TYPE satisfies a trait, and do the satisfaction is CHECKED against the "
          "required signature (RFC-0026 §9)");
    check(CHECK("trait shape do area input s self . output u64 . effects none . end "
                "struct rect do satisfies shape . w u8 . end "
                "fn g output u8 .  do return 1 . end") == false,
          "trait: ★ a type that claims a trait but LACKS the required op is an error "
          "(E-TRAIT-MISSING) — a bound that carries no contract guarantees nothing");
    check(CHECK("trait shape do area input s self . output u64 . effects none . end "
                "struct rect do satisfies shape . w u8 . end "
                "fn rect.area input s rect . output u8 .  "
                " do return (field s w) . end") == false,
          "trait: ★ and a MISMATCHED signature is an error (E-TRAIT-SIG) — the output must match");
    check(CHECK("trait shape do area input s self . output u64 . effects none . end "
                "struct rect do satisfies shape . w u8 . end "
                "proc rect.area input s rect . output u64 . effects io . "
                " do return widen u64 (field s w) . end") == false,
          "trait: ★★ and an implementation with an EFFECT the trait does not declare is an error "
          "(E-TRAIT-EFFECT) — a caller reasons against the TRAIT's contract; if the impl does more, "
          "that reasoning is a LIE. This is exactly what makes dynamic dispatch safe or unsafe");

    // ★★ 말없이 무시되던 최상위 선언들 — 구현이 없으면 **없다고 말한다**(교훈 2).
    //   초록불이 **검사됐다는 뜻이 아니다.** 그것을 도구가 말해야 한다.
    // ★★ `test` 는 이제 **실행된다**(--test). 미구현이 아니게 됐으니 W-NOT-YET 도 걷었다 —
    //   남겨 두면 그것도 거짓말이다. 대신 `--check` 가 테스트를 **안 돌린다는 사실**을 말한다.
    check(CHECK_WARNS("test t1 do expect eq 1 1 . end "
                      "fn f output u8 .  do return 1 . end") > 0,
          "test: `--check` does NOT run the tests, and says so (W-TEST-NOT-RUN) — a green check "
          "means the code type-checks, NOT that the tests pass");
    // ★★ actor 는 이제 **컴파일된다**. 고지는 **아직 없는 것**(mailbox·restart)만 말한다 —
    //   거짓이 된 고지는 걷어야 한다.
    // ★ 2026-07-24 — 이 테스트는 **두 번 낡아** 있었다:
    //   ① `on` 은 어휘에서 **제거**됐다(E-VOCAB-REMOVED, RFC-0046) — 블록 안이라는 것이 이미
    //      "핸들러" 를 뜻하므로 `on` 은 아무것도 안 말하면서 순수/절차 비트를 우회했다.
    //      핸들러는 `proc`(상태를 바꾼다) 또는 `fn`(읽기만) 으로 쓴다.
    //   ② `mailbox bounded N` 은 이제 **진짜로 강제된다**(재진입 in-flight 상한, E-VM-MAILBOX-FULL,
    //      tests/vm_mailbox.low) — 그래서 "아직 강제 안 함" 고지가 **정당하게 걷혔다**. 거짓이 된
    //      고지를 걷는 것이 옳고, 고지를 기대하던 이 테스트가 따라오지 않았다.
    check(CHECK_WARNS("actor c do state do v u64 . end mailbox bounded 8 . "
                      "fn get output u64 . do return v . end end") == 0,
          "actor: `mailbox bounded N` 은 이제 강제된다 — 그래서 '아직 안 함' 고지가 **없다**. "
          "배달은 여전히 순차(한 번에 하나)이고 그것이 액터 모델의 보장이다; 동시성은 최적화이지 의미가 아니다");
    check(CHECK_WARNS("actor c do state do v u64 . end "
                      "on get output u64 . do return v . end end") == 0,
          "actor: ★ a plain actor compiles with NO not-yet notice — its ops used to vanish from "
          "the IR entirely (0 defs). LowentDRF.v proved what isolation gives; the tool now gives it");

    // ★ `access` 의 두 모드는 **읽기/쓰기 규율**이다 — 스케줄 힌트와 한 덩어리로 묶여
    //   "아직 미구현" 이었다. 그러면 **잡을 수 있는 거짓말을 놓친다.**
    check(CHECK("proc f input s mut slice u8 . . output u8 . effects none . access s shared_read . "
                " do set (index s 0) 9 . return index s 0 . end") == false,
          "access: ★ `shared_read` + a WRITE — that promise is exactly what lets several tasks "
          "hold the place at once (LowentDRF.v: no write ⇒ no race); a write breaks it");
    check(CHECK("fn f input s slice u8 . output u8 .  access s write_only . "
                " do return index s 0 . end") == false,
          "access: ★ `write_only` + a READ — a write_only place may be uninitialised, and the "
          "declaration is what told the caller it was safe");
    check(CHECK("fn f input s slice u8 . output u8 .  access s shared_read . "
                " do return index s 0 . end") == true,
          "access: `shared_read` reading only is fine");

    // ★ SPEC-002 의 제어 어휘에 있는데 구현이 없던 셋 — 그리고 `panic` 은 **오진**당했다.
    check(TYCK("fn f input a u8 . output u8 . effects panic . "
               " do if gt a 10 . do panic \"too big\" . end return a . end") == true,
          "control: `panic` is in the spec's control AND effect vocabulary — the tool called it "
          "\"undefined name\" (your program is wrong). It was not: the TOOL could not. Same "
          "misdiagnosis as bit_cast");

    // ★★ 문법에 **명시적으로** 있는 형태가 "네 프로그램이 틀렸다" 로 거절되고 있었다.
    check(TYCK("fn f input comptime n u8 . input a u8 . output u8 .  "
               " do return expr n + a . . end") == true,
          "comptime: ★ `input [comptime] N T .` IS the grammar (SPEC-002 §217) — nobody knew the "
          "word, so `comptime` was read as the parameter's NAME and the real name became the TYPE "
          "(\"undefined type\"). A grammatical form rejected as YOUR error");
    check(TYCK("fn f input s shared_read u8 . output u8 .  do return s . end") == true,
          "shared types: `shared_read`/`lock`/`rwlock`/`atomic` are SPEC-008 §74 types — they were "
          "rejected as undefined. The name is accepted now; the level-3 discipline is W-NOT-YET");

    // ★ SPEC-007 §28 채널 전환 — 명세의 형태가 E-IR-ARITY 로 거절됐다(오진 5번째).
    check(TYCK("enum e do bad end "
               "fn g input a u8 . output result u8 e .  do return ok a . end "
               "fn f input a u8 . output option u8 .  "
               " do return try (g a) else_none . end") == true,
          "channel: `try E else_none` (result→option) is SPEC-007 §28 — it was rejected as "
          "E-IR-ARITY (\"extra operands\"): the tool did not know the word and blamed the program");
    check(TYCK("proc f input fs cap file_system . output u8 . effects io . do return 1 . end") == true,
          "cap: ★ SPEC-006 §53's OWN EXAMPLE — the cap KIND (file_system/net/clock/rng/device, "
          "SPEC-004 §19) was read as a TYPE, so the spec's own example was \"an undefined type\"");

    // ★★ 명명 계약 — 선언하고, 충족한다고 적고, **아무 일도 일어나지 않았다.**
    check(CHECK("contract nonneg do requires ge a 1 . end "
                "fn f satisfies nonneg . input a u8 . output u8 .  "
                " do return a . end") == true,
          "named contract: ★ `satisfies` may name a CONTRACT, not only a trait (SPEC-002 §267) — "
          "a named contract used to be rejected as \"an undeclared trait\"");
    check(CHECK_WARNS("contract unused do requires ge a 1 . end "
                      "fn f input a u8 . output u8 .  do return a . end") > 0,
          "named contract: a contract NO op satisfies constrains nothing — the tool says so "
          "(W-UNBOUND). A contract that IS satisfied is enforced, and gets no notice");

    // ★★ 이름이 **빌트인**과 겹치면 그 선언은 **영원히 호출되지 않는다.**
    //   세 픽스처가 실제로 그러고 있었다(encode · sum · count).
    check(CHECK("fn count input a u8 . output u8 .  do return a . end") == false,
          "names: ★ an op named like a BUILTIN can never be called — the resolver always picks "
          "the builtin. It exists and does not exist (E-NAME-BUILTIN)");
    check(CHECK("fn tally input a u8 . output u8 .  do return a . end") == true,
          "names: a name of its own is fine");

    // ★★ 임의의 배열 크기가 **조용히 자른다** — 이번 세션 두 번째(prng[8] 에 이어 f[8]).
    check(TYCK("struct big do f1 u8 . f2 u8 . f3 u8 . f4 u8 . f5 u8 . f6 u8 . f7 u8 . f8 u8 . "
               "f9 u8 . f10 u8 . f11 u8 . f12 u8 . f13 u8 . f14 u8 . f15 u8 . f16 u8 . f17 u8 . end "
               "fn f output u8 .  do return 1 . end") == false,
          "fields: ★ past the limit the tool REFUSES (E-TYPE-LIMIT) — the extra fields used to be "
          "DROPPED SILENTLY: check was green, the layout came out short, `encode` wrote too few "
          "bytes, and the runtime error blamed the FIELD ACCESS. Same sin as prng[8]");
    check(CHECK_WARNS("contract c do requires ge x 0 . end "
                      "fn f input x u8 . output u8 .  do return x . end") > 0,
          "not-yet: a named `contract` is do enforced NOWHERE — no op is bound to it");

    // ★ SPEC-004 §190 T0 — `bit_cast`. `bool`/`enum` 은 **plain 이 아니다**(trap representation).
    check(TYCK("fn f input a u8 . output i8 .  do return bit_cast i8 a . end") == true,
          "bit_cast: T0 of the punning ladder — T1(view)/T2(encode) existed, the EASIEST rung "
          "was missing (and the tool called it \"undefined name\": your program is wrong — it "
          "was not; the TOOL could not)");

    // ── ★★ split binding — 타입의 닫는 `.` 가 **선언 폼을 먼저 닫아 버린다** ──
    // `var v vec u32 4 . be <expr> .` 에서 타입의 `.` 가 var 폼을 닫아서,
    // `be <expr> .` 이 **별개의 문장**이 된다. IR 은 이것을 알고 처리하는데
    // (low_ir.c: "split binding: 타입은 f 에 있다"), **타입체커는 몰랐다** —
    // 그래서 그런 선언의 **초기화식이 아예 검사되지 않았다.** vec 선언은 전부 이 모양이다.
    //
    // 그 결과 vec 의 레인 수도 원소 종류도 검사되지 않았다. VM 이 런타임에 잡긴 했지만
    // (E-VM-TYPE: vector lane counts differ) — **타입 오류를 런타임까지 미루는 것은
    // 이 언어가 아니다.** 정적으로 올렸다.
    check(TYCK("fn f input xs slice u8 . output u32 .  do "
               "  var va vec u32 4 . be load xs 0 . "
               "  var vb vec u32 8 . be load xs 4 . "
               "  var vc vec u32 4 . be add va vb . "
               "  return reduce_add vc . end") == false,
          "vec: ★★ adding a 4-lane and an 8-lane vector → E-TYPE-LANES. Two vectors of different "
          "width are different types (before this the initializer of a split binding was never "
          "checked AT ALL, and the VM caught it only at run time)");
    check(TYCK("fn f input xs slice u8 . output u32 .  do "
               "  var va vec u32 4 . be load xs 0 . "
               "  var vb vec f32 4 . be load xs 4 . "
               "  var vc vec u32 4 . be add va vb . "
               "  return reduce_add vc . end") == false,
          "vec: ★ an integer vector and a float vector do not mix → E-TYPE-MIX");
    check(TYCK("fn f input xs slice u8 . output u32 .  do "
               "  var va vec u32 4 . be load xs 0 . "
               "  var vb vec u32 4 . be load xs 4 . "
               "  var vc vec u32 4 . be add va vb . "
               "  return reduce_add vc . end") == true,
          "vec: matching lanes and lane kinds pass (no false positive — the existing SIMD fixtures "
          "stay green)");

    // ── ★ 선언의 **내용**도 뜻이 있어야 한다: 순환 별칭 · layout · align ──
    check(TYCK("type a b . . type b a . . "
               "fn f input x a . output u8 .  do return 1 . end") == false,
          "declarations: ★ a `type` alias that resolves back to itself → E-TYPE-CYCLE "
          "(resolving it would not terminate; the declaration names nothing)");
    check(TYCK("type a u8 . . type b a . . "
               "fn f input x b . output u8 .  do return x . end") == true,
          "declarations: a chain of aliases that ends at a builtin is fine");
    check(TYCK("struct s do layout nonsense . x u8 . end "
               "fn f output u8 .  do return 1 . end") == false,
          "declarations: ★ an unknown struct layout → E-TYPE-LAYOUT (the vocabulary is closed: "
          "packed | native)");
    check(TYCK("struct s do align 3 . x u8 . end "
               "fn f output u8 .  do return 1 . end") == false,
          "declarations: ★ `align 3` → E-TYPE-ALIGN (alignment must be a power of two — no "
          "hardware can honour anything else)");
    check(TYCK("struct s do layout packed . align 4 . x u8 . end "
               "fn f output u8 .  do return 1 . end") == true,
          "declarations: a real layout and a power-of-two align pass (no false positive)");

    // ── ★ 타입 이름이 **무언가를 가리켜야 한다** ──
    // `input a no_such_type .` 이 조용히 통과했다. 이름이 아무것도 안 가리키는데 아무도 안 봤다.
    // ★ SPEC-MVP §8 예제 B 는 타입을 **하나도 선언하지 않고** graph·node_id·bytes 를 썼다.
    check(TYCK("fn f input a no_such_type . output u8 .  do return 1 . end") == false,
          "type names: ★ an undeclared type name → E-TYPE-UNDEF (before this, a type name could "
          "point at nothing at all)");
    check(TYCK("type mytype u8 . . "
               "fn f input a mytype . output u8 .  do return a . end") == true,
          "type names: a declared `type` alias resolves");
    check(TYCK("struct s do x u8 . end "
               "fn f input a s . output u8 .  do return 1 . end") == true,
          "type names: a `struct` name do resolves");
    check(TYCK("fn f input a u8 . output u8 .  requires le a 200 . "
               " do return a . end") == true,
          "type names: contract clauses are NOT type positions (the clause scan must stop at "
          "`requires` — it did not, and flagged `le`/`a`/`200` as undeclared types)");

    // ── ★ `stack_new <region>` 의 region 이름 — **지운다고 검사 안 해도 되는 건 아니다** ──
    // IR 은 region 이름을 **지운다**(VM 풀이 아레나를 대신한다). 그래서 그 이름이
    // 아무것도 안 가리켜도 **조용히 통과했다.** 선언된 이름인데 아무도 확인하지 않았다.
    check(CHECK("type frame u64 . . "
                "proc f input r region frame . . output u64 . effects alloc . do "
                "  let s be stack_new nosuch capacity 4 . . push s 1 . return count s . end") == false,
          "region: ★ `stack_new` naming something that is not a `region` parameter → "
          "E-REGION-UNDEF (the lowering ERASES this name — that does not mean it need not be "
          "checked)");
    check(CHECK("type frame u64 . . "
                "proc f input r region frame . . output u64 . effects alloc . do "
                "  let s be stack_new r capacity 4 . . push s 1 . return count s . end") == true,
          "region: a real region parameter passes (no over-rejection)");

    // ── ★★★★★ **같은 이름, 다른 모듈 — 제 모듈이 이긴다** (2026-08-07) ──
    //
    //   이 두 줄은 오래 `== false` 였고, 그 이유가 주석에 이렇게 적혀 있었다:
    //     *"모듈 b 의 `g` 가 자기 모듈의 `f` 가 아니라 **모듈 a 의 `f` 를 불렀다**."*
    //   그 진단은 **옳았다** — 그리고 그것은 이름의 문제가 아니라 **해소 순서의 문제**였다.
    //   맨이름 조회가 이름만 보고 **첫 정의**를 집었기 때문이다.
    //
    //   ⇒ 이제 맨이름은 **제 모듈부터** 찾는다(`ir_def_find_in`·`ck_vis_walk`). 그러면
    //     b 의 `g` 는 b 의 `f` 를 부르고, 막아야 할 위험 자체가 **구성상 사라진다.**
    //   ★ 실물에서 걸린 값: `lib/utf8.low` 과 `lib/utf16.low` 은 `decode`·`next`·
    //     `count_chars`·`is_valid` 넷을 겹쳐서 **함께 쓸 수가 없었다** — 두 모듈의 대표
    //     용도가 바로 그 상호 변환인데. 공존 불가 쌍 22 → 19.
    //   ☞ **금지가 결함을 가리고 있었다**: 시끄러운 거절이 있으면 그 뒤의 조용한 오답을
    //     아무도 못 본다. 거절을 풀 때는 **뒤에 무엇이 있었는지** 반드시 봐야 한다
    //     (여기서는 실제로 있었고, 검사만 풀었다면 조용히 틀렸을 것이다).
    //   ★ 어느 `f` 를 골랐는지는 **값으로** 재야 하고, 유닛에는 실행기가 없다 —
    //     그 증인은 골든에 있다(`self-name: … returns 2, not 1` · 순서를 뒤집어도 같다).
    check(CHECK("module a .\n"
                "fn f input x u8 . output u8 .  do return 1 . end\n"
                "module b .\n"
                "fn f input x u8 . output u8 .  do return 2 . end\n"
                "fn g input x u8 . output u8 .  do return f x . end") == true,
          "self-name: ★★★★ the same op name in two modules is FINE, and a bare reference means "
          "the CALLER'S OWN — module b's `g` calls b's `f`. This used to be refused, and the "
          "reason given was that `g` silently called module a's `f`: that was a RESOLUTION-ORDER "
          "defect, not a naming one (declaration order changed the answer)");
    check(CHECK("module a .\ntype t u8 . .\n"
                "module b .\ntype t u32 . .\n"
                "fn h input x t . output u32 .  do return x . end") == true,
          "self-name: ★★ and the same for TYPE names — `h` lives in module b, so its `t` is b's "
          "`t` (u32). Nobody has to say which one it got: being inside the module IS saying it");
    // ★ 그리고 **반대편 증인**: 제 모듈에 없으면 여전히 맨이름으로 못 넘는다(위 visibility 시험).
    //   제 모듈 우선은 **문을 여는 규칙이 아니라 순서를 정하는 규칙**이다.
    check(CHECK("module a .\n"
                "export fn f input x u8 . output u8 .  do return 1 . end\n"
                "module b .\nuse a .\n"
                "fn g input x u8 . output u8 .  do return a.f x . end") == true,
          "duplicate names: distinct names across modules are fine (no over-rejection; the "
          "cross-module call is QUALIFIED — RFC-0011 §6.3)");
    // ★★★ 강제 플립(RFC-0011 §6.3): export 도 **bare 로는 못 넘는다** — 한정이 유일한 문이다.
    check(CHECK("module a .\n"
                "export fn f input x u8 . output u8 .  do return 1 . end\n"
                "module b .\nuse a .\n"
                "fn g input x u8 . output u8 .  do return f x . end") == false,
          "visibility: ★★★ an EXPORTED name reached BARE from another module is REFUSED — "
          "cross-module names are QUALIFIED (`a.f`), there is no glob import (RFC-0011 §6.3)");
    // ★★★ **가시성** — `export` 가 없으면 남의 모듈에서 못 부른다(2026-07-14).
    check(CHECK("module a .\n"
                "fn f input x u8 . output u8 .  do return 1 . end\n"
                "module b .\nuse a .\n"
                "fn g input x u8 . output u8 .  do return f x . end") == false,
          "visibility: ★★★ a non-`export`ed name cannot be reached from another module "
          "(E-VISIBILITY). The modifier used to be PARSED AND THROWN AWAY — the consumers could "
          "not even SEE it, so visibility could not be enforced. Same disease as `comptime`");

    // ── ★★ `match` 는 **닫혀야 한다** — 진짜 프로그램이 찾아낸 문법의 함정 ──
    // SPEC-002 §239: `match <x> do case* end`. 처음엔 `do` 없이 구현했다.
    // 그러면 case 들이 **형제 폼**이 되고, 그 뒤의 `end` 가 **바깥 블록을 조용히 닫는다.**
    // 루프 안의 match 에서 `set i expr i + 1 .` 이 **루프 밖으로 나갔다** → 무한 루프.
    // ★ 유닛 테스트도, 픽스처도 통과했다. **진짜 프로그램을 쓰자마자 드러났다.**
    //   문법이 열려 있으면 파서가 **조용히 다른 뜻으로 읽는다.** 닫힌 문법이 그것을 막는다.
    {
        const char *src =
            "enum k do\n a .\n b .\nend\n"
            "fn f input s slice u8 . output u64 .  do "
            "  var n u64 be 0 . var i u64 be 0 . "
            "  while lt i (len s) . do "
            "    match a "                       // ← do 가 없다
            "      case a . do set n expr n + 1 . . end "
            "      case b . do set n n . end "
            "    end "
            "    set i expr i + 1 . . end "
            "  return n . end";
        low_lex_result_t l = LEX(src); proven_arena_reset(&arena);
        low_parse_result_t p = low_parse(nodes, heap, &l.tokens);
        low_ir_t ir = low_ir_build(heap, &p);
        proven_size_t nlow = 0;
        for (proven_size_t i = 0; i < ir.ndefs; i++) if (!ir.defs[i].lowered) nlow++;
        check(nlow == 1 && HASDIAG(ir.diags, "E-IR-UNSUP"),
              "match: ★★ a `match` WITHOUT its do-block is REJECTED — the cases would become loose "
              "siblings and the following `end` would close the ENCLOSING block, silently moving a "
              "statement out of the loop (this was a real infinite loop; the unit tests and the "
              "fixtures all passed, and only a REAL program found it)");
        low_ir_free(heap, &ir);
        if (p.forms) heap.free_fn(heap.ctx, p.forms);
        proven_array_destroy(&p.diags); proven_array_destroy(&l.tokens); proven_array_destroy(&l.diags);
    }

    // ── ★★ **모듈 링크** — S5 코어의 마지막 벽 ──
    // 지금까지 파일 **하나**만 받았다. 그래서 `use` 가 아무것과도 대조되지 않았고
    // (W-NOT-YET 가 정직하게 그렇게 말했다), `tests` 절이 다른 파일의 op 을 걸 수도 없었다.
    // 이제 여러 .low 가 **하나의 컴파일 단위**로 이어진다(폼 배열을 합친다).
    //
    // ★ `use` 의 세 갈래 — 이 구분이 요점이다:
    //     단위 안에 있다   → **해석됐다.** 아무 말도 안 한다.
    //     단위 밖이다      → **확인할 수 없다**(모듈 탐색 경로가 없다) → W-USE-EXTERNAL.
    //                        오류라 부르면 거짓말이고(외부 모듈일 수 있다), 조용히 넘기면
    //                        그것도 거짓말이다. **확인할 수 없다는 사실을 말한다.**
    //     이름이 실제로 없다 → 호출 지점에서 E-IR-UNDEF. 프로그램이 틀렸다.
    check(CHECK("module app .\nuse no_such .\n"
                "fn f output u8 .  do return 1 . end") == true,
          "module link: an unresolved `use` is a NOTE, not an error — there is no module search "
          "path, so the tool says it CANNOT confirm it (claiming it is wrong would be a lie; "
          "saying nothing would also be a lie)");
    check(CHECK("module math .\n"
                "export fn dbl input a u8 . output u8 .  do return mul a 2 . end\n"
                "module app .\nuse math .\n"
                "fn run input a u8 . output u8 .  do return math.dbl a . end") == true,
          "module link: a `use` that resolves inside the unit is silent (it was CHECKED; the "
          "call is QUALIFIED — RFC-0011 §6.3)");
    // ★ 그리고 `tests` 절의 크로스모듈 참조가 **진짜로 검사된다.**
    check(CHECK("module lib .\n"
                "fn dbl input a u8 . output u8 .  tests dbl_test . "
                " do return mul a 2 . end") == false,
          "module link: alone, a `tests` target in another module does not resolve → "
          "E-CONTRACT-UNDEF");
    check(CHECK("module lib .\n"
                "export fn dbl input a u8 . output u8 .  tests dbl_test . "
                " do return mul a 2 . end\n"
                "module libtest .\nuse lib .\n"
                "export fn dbl_test input a u8 . output bool .  "
                " do return eq (lib.dbl a) (mul a 2) . end") == true,
          "module link: ★ with both files in the unit the cross-module `tests` target RESOLVES "
          "(this is what the module system buys: a promise in one file, kept in another)");

    // ── ★ 구조체 **필드 쓰기** — S5 코어의 마지막 벽 ──
    // 장소는 괄호로 감싼 form 이다: `set (field q x) v .`
    // (슬라이스 원소 쓰기 `set (index s i) v .` 와 같은 자리다.)
    {
        const char *src =
            "struct p do\n x u8 .\n y u8 .\nend\n"
            "fn f input v u8 . output u8 .  do "
            "  var q p be make p do x 1 . y 2 . end . "
            "  set (field q x) v . "
            "  return expr (field q x) + (field q y) . . end";
        low_lex_result_t l = LEX(src); proven_arena_reset(&arena);
        low_parse_result_t p = low_parse(nodes, heap, &l.tokens);
        low_ir_t ir = low_ir_build(heap, &p);
        check(ir.ok, "field write: `set (field q x) v .` lowers (the last hole in the S5 core)");
        proven_i64 nine = 9;
        low_ir_run_result_t r = low_ir_run(&ir, proven_u8str_view_from_cstr("f"), &nine, 1, heap, &ir.diags);
        check(r.ok && r.value == 11, "field write: the record is updated in place (9 + 2 = 11)");
        low_ir_free(heap, &ir);
        if (p.forms) heap.free_fn(heap.ctx, p.forms);
        proven_array_destroy(&p.diags); proven_array_destroy(&l.tokens); proven_array_destroy(&l.diags);
    }
    check(TYCK("struct p do\n x u8 .\nend\n"
               "fn f output u8 .  do "
               "  var q p be make p do x 1 . end . set (field q ghost) 5 . "
               "  return field q x . end") == false,
          "field write: ★ writing a field no struct declares → E-TYPE-FIELD");
    check(TYCK("struct p do\n x u8 .\nend\n"
               "fn f input a i32 . output u8 .  do "
               "  var q p be make p do x 1 . end . set (field q x) a . "
               "  return field q x . end") == false,
          "field write: ★ a value that does not match the field's declared type is caught "
          "(the ⊑ lattice applies here too)");

    // ── ★ `--check` 는 **검사한 것만 주장한다** ──
    // 정적 검사가 초록불이어도 이름이 **아무것도 안 가리키면** 프로그램은 실행되지 않는다.
    // 그 사실을 말하지 않는 것은 도구가 검사한 것보다 많이 주장하는 것이다.
    // (언어 완결성 작업으로 enum 값 · match · slice T · 필드 쓰기가 **전부 구현됐다.**
    //  남은 코어 밖: **모듈 링크** — `use` 가 W-NOT-YET 로 그렇게 말한다.)
    {
        const char *src = "fn f input a u8 . output u8 .  do "
                          "  return no_such_op a . end";
        low_lex_result_t l = LEX(src); proven_arena_reset(&arena);
        low_parse_result_t p = low_parse(nodes, heap, &l.tokens);
        low_ir_t ir = low_ir_build(heap, &p);
        proven_size_t nlow = 0;
        for (proven_size_t i = 0; i < ir.ndefs; i++) if (!ir.defs[i].lowered) nlow++;
        check(nlow == 1 && HASDIAG(ir.diags, "E-IR-UNDEF"),
              "audit: an undefined name is not a missing feature — the PROGRAM is wrong, and "
              "`--check` says so (it used to print `ok` and let you run into E-VM-UNSUP)");
        low_ir_free(heap, &ir);
        if (p.forms) heap.free_fn(heap.ctx, p.forms);
        proven_array_destroy(&p.diags); proven_array_destroy(&l.tokens); proven_array_destroy(&l.diags);
    }

    // ── ★ `make` 구조체 리터럴이 **아무 검사도 받지 않았다** ──
    // struct 선언이 아무것도 강제하지 않았다. 실제로 이런 것들이 조용히 통과했다:
    //   struct p  x u8 . y u8 . end
    //   make p do x 1 . end            → {x 1}            ★ y 가 **아예 없다**
    //   make p do x 1 . ghost 2 . end  → {x 1, ghost 2}   ★ 선언에 없는 필드가 들어간다
    //   make p do x <i32> . y 0 . end  → {x -1, y 0}      ★ u8 인데 -1
    // 선언은 검사되지 않으면 거짓말이 된다(PRINCIPLES.md §0).
    check(TYCK("struct p do x u8 . y u8 . end "
               "fn f output p .  do return make p do x 1 . end end") == false,
          "make: ★ a MISSING field is caught (before this it just… wasn't there)");
    check(TYCK("struct p do x u8 . end "
               "fn f output p .  do return make p do x 1 . ghost 2 . end end") == false,
          "make: ★ a field the struct does not declare is caught (before this it went straight in)");
    check(TYCK("struct p do x u8 . y u8 . end "
               "fn f input a i32 . output p .  do "
               "  return make p do x a . y 0 . end end") == false,
          "make: ★ a field value of the wrong type is caught (a u8 field was taking an i32)");
    check(TYCK("struct p do x u8 . y u8 . end "
               "fn f output p .  do return make p do x 1 . y 2 . end end") == true,
          "make: a correct struct literal still passes (no false positive)");

    // ── ★ `let` 이 타입체크를 **통째로 빠져나가고 있었다** ──
    // `var` 만 검사됐다. 같은 선언인데 한쪽만 검사한 것은 그냥 빠뜨린 것이다.
    check(TYCK("fn f input a i32 . output u8 .  do "
               "  var v u8 be a . return v . end") == false,
          "let/var: `var v u8 be <i32>` is caught (this always worked)");
    check(TYCK("fn f input a i32 . output u8 .  do "
               "  let v u8 be a . return v . end") == false,
          "let/var: ★ `let v u8 be <i32>` is caught too — before this, `let` escaped the type "
          "checker ENTIRELY and even `let v bool be <u8>` passed silently");
    check(TYCK("fn f input a u8 . output u8 .  do "
               "  let v bool be a . return a . end") == false,
          "let/var: `let v bool be <u8>` — a kind mismatch, now caught");
    check(TYCK("fn f input a u8 . output u8 .  do "
               "  let v u8 be a . return v . end") == true,
          "let/var: a correct `let` still passes (no false positive)");

    // ── ★ `try` 는 **풀 수 있는 것**에만 붙는다 ──
    // `try` 는 "이것은 실패할 수 있고, 실패하면 전파한다" 는 선언이다.
    // result/option 이 아닌 값에 붙으면 아무 일도 하지 않는다 — 선언이 거짓말이 된다.
    // ★ SPEC-MVP §8 의 **대표 예제가 실제로 그랬다**: read_u16_be 는 u16 을 돌려주는데
    //   본문이 `try read_u16_be …` 라고 적어 놨다. 아무도 확인하지 않았다.
    check(TYCK("fn plain input a u8 . output u8 .  do return a . end "
               "fn f input a u8 . output u8 .  do "
               "  let x u8 be try plain a . . return x . end") == false,
          "try: ★ `try` on a value that is not a result/option → E-TYPE-TRY (the flagship example "
          "in SPEC-MVP §8 had exactly this — a `try` that did nothing)");

    // ── ★ guard 의 분기 내로잉 — **애초에 작동하지 않고 있었다** ──
    // `guard C else …` 는 `C; not; brz` 로 낮아진다. 그런데 not 이 술어 정보를 통째로 버려서,
    // 구간 분석이 guard 에서 나온 사실을 **한 번도 쓴 적이 없었다.** 구간 분석 도입 이래로 계속.
    // 이제 not 은 술어를 **뒤집어서** 나른다. 가장 흔한 방어 패턴이 공짜가 된다.
    {
        const char *src =
            "fn safe input a u8 . output u8 .  do "
            "  guard le a 100 . else return 0 . . "
            "  return mul a 2 . end";     // a ≤ 100 → a*2 ≤ 200 ⊆ u8 ⇒ 검사 불필요
        low_lex_result_t l = LEX(src); proven_arena_reset(&arena);
        low_parse_result_t p = low_parse(nodes, heap, &l.tokens);
        low_ir_t ir = low_ir_build(heap, &p);
        check(ir.ok && ir.checks_total == 1 && ir.checks_proven == 1,
              "guard narrowing: ★ `guard le a 100` makes the following `mul a 2` provably safe "
              "(before the fix this was 0/1 — `not` threw the predicate away and NO guard ever "
              "narrowed anything)");
        proven_i64 v100 = 100, v200 = 200;
        low_ir_run_result_t r = low_ir_run(&ir, proven_u8str_view_from_cstr("safe"), &v100, 1, heap, &ir.diags);
        check(r.ok && r.value == 200, "guard narrowing: the guarded path computes");
        r = low_ir_run(&ir, proven_u8str_view_from_cstr("safe"), &v200, 1, heap, &ir.diags);
        check(r.ok && r.value == 0, "guard narrowing: the else path still runs (no false removal)");
        low_ir_free(heap, &ir);
        if (p.forms) heap.free_fn(heap.ctx, p.forms);
        proven_array_destroy(&p.diags); proven_array_destroy(&l.tokens); proven_array_destroy(&l.diags);
    }

    // ── `errors … when <조건>` — 선언은 **검사되지 않으면 거짓말이 된다** ──
    // 지금까지 when 조건은 **한 번도 평가되지 않았다.** 오류 이름만 검사됐다.
    // 이제 오류를 낼 때 **그 자리에서** 조건이 참인지 검사한다(ensures 와 같은 규칙).
    // ★ 그리고 정직한 선언은 **공짜다** — 술어 기억이 guard 의 조건과 when 을 같은 식으로 알아본다.
    {
        const char *src =
            "module t . type bytes slice u8 . . enum e do small end "
            // ① 정직 — guard 의 조건과 when 이 같은 식 ⇒ **증명되어 검사가 사라진다**
            "fn honest input data bytes . output result u8 e . .  "
            "  errors small lt len data . 4 . . "
            " do guard ge len data . 4 . else return error small . . . "
            "   return ok index data 0 . . end "
            // ② 거짓말 — 선언은 len<2, 본문은 len<9 ⇒ 증명 실패 ⇒ 검사 유지
            "fn liar input data bytes . output result u8 e . .  "
            "  errors small lt len data . 2 . . "
            " do guard ge len data . 9 . else return error small . . . "
            "   return ok index data 0 . . end "
            // ③ ★★ 가장 미묘한 거짓말 — **같은 식, 반대 극성**.
            //    선언: len ≥ 4 일 때 small.  본문: len < 4 일 때 small.
            //    지문이 같으므로 술어 기억이 그 식을 알아본다. **극성을 안 보면**
            //    "이 경로에서 알려진 식" 이라는 이유로 잘못 증명하고 검사를 지운다.
            //    극성(pneg)이 건전성의 축이다 — 실제로 그것을 빼면 E-VM-ANALYSIS 가 터진다.
            "fn inverted input data bytes . output result u8 e . .  "
            "  errors small ge len data . 4 . . "
            " do guard ge len data . 4 . else return error small . . . "
            "   return ok index data 0 . . end";
        low_lex_result_t l = LEX(src); proven_arena_reset(&arena);
        low_parse_result_t p = low_parse(nodes, heap, &l.tokens);
        low_ir_t ir = low_ir_build(heap, &p);
        check(ir.ok, "when: the fixture lowers (the condition is evaluated at each error site)");
        check(ir.checks_proven >= 1,
              "when: ★ the HONEST declaration is FREE — the guard's condition and the `when` "
              "condition are the same expression, and the predicate memory recognises that");

        proven_i64 a2[2] = { 1, 2 }, a3[3] = { 1, 2, 3 }, a5[5] = { 1, 2, 3, 4, 5 };
        low_ir_run_result_t r = low_ir_run(&ir, proven_u8str_view_from_cstr("honest"), a2, 2, heap, &ir.diags);
        check(r.ok, "when: the honest op returns its declared error (len 2 < 4 — as declared)");
        r = low_ir_run(&ir, proven_u8str_view_from_cstr("honest"), a5, 5, heap, &ir.diags);
        check(r.ok, "when: the honest op succeeds when the condition is false");
        // ★★ 거짓말: 본문은 small 을 내는데 선언한 조건(len<2)은 거짓이다(len=3)
        r = low_ir_run(&ir, proven_u8str_view_from_cstr("liar"), a3, 3, heap, &ir.diags);
        check(!r.ok && HASDIAG(ir.diags, "E-VM-CONTRACT"),
              "when: ★★ a LYING `errors` clause is caught — the op returned `small` on a path "
              "where its own declared `when` condition is false (before this, it passed silently)");
        r = low_ir_run(&ir, proven_u8str_view_from_cstr("liar"), a2, 1, heap, &ir.diags);
        check(r.ok, "when: the liar is fine on the path where it happens to tell the truth");
        // ★★ 같은 식 · 반대 극성 — 술어 기억이 식을 알아보되 **극성을 봐야** 한다.
        r = low_ir_run(&ir, proven_u8str_view_from_cstr("inverted"), a2, 2, heap, &ir.diags);
        check(!r.ok && HASDIAG(ir.diags, "E-VM-CONTRACT"),
              "when: ★★ the SAME expression with the OPPOSITE polarity is still a lie and is caught "
              "— drop the polarity check and the analysis wrongly discharges it (verified: doing so "
              "makes the VM fire E-VM-ANALYSIS). Polarity is the axis of soundness here");
        // ★★★ 전엔 이 줄이 `check(r.ok, "…오류 경로를 안 타면 괜찮다")` 였다. **틀렸다.**
        //   `inverted` 의 절은 `errors small ge (len data) 4 .` — **길이가 4 이상이면 오류**
        //   라고 선언한다. 그런데 len=5 로 **정상 반환**한다. **그것이 바로 거짓말이다.**
        //   옛 도구는 **오류 경로만** 봤다: `return error E` 에서 when 이 참인지. 반대 방향 —
        //   **when 이 참인데 오류를 안 냈을 때** — 는 **아무도 안 봤다.**
        //   그래서 계약 오라클이 그 절을 **믿고** 경계 테스트를 뽑았다. 거짓말 위에 서 있었다.
        //   이제 정상 반환에서도 **모든 when 이 거짓임**을 검사한다 ⇒ 여기서 잡힌다.
        r = low_ir_run(&ir, proven_u8str_view_from_cstr("inverted"), a5, 5, heap, &ir.diags);
        check(!r.ok && HASDIAG(ir.diags, "E-VM-CONTRACT"),
              "when: ★★★ and the inverted clause is a lie on the NORMAL path too — it declares "
              "`error when len >= 4` and then returns OK at len 5. The tool used to look only at "
              "the ERROR path (is `when` true when we error?) and never at the other direction "
              "(did we error whenever `when` is true?). The `errors` clause is an EXIT CONTRACT and "
              "it is now enforced BOTH ways");

        // 소진 검사 — 제거된 when 검사가 실제로 틀린 적이 있는가(E-VM-ANALYSIS).
        int accused = 0;
        for (proven_i64 len = 0; len <= 12; len++) {
            proven_i64 args[12];
            for (proven_i64 k = 0; k < len; k++) args[k] = 1 + k;
            (void)low_ir_run(&ir, proven_u8str_view_from_cstr("honest"), args, (proven_size_t)len, heap, &ir.diags);
        }
        if (HASDIAG(ir.diags, "E-VM-ANALYSIS")) accused++;
        check(accused == 0,
              "when: no false elimination — the discharged `when` check really did hold on every "
              "length 0..12 (a wrong removal would fire E-VM-ANALYSIS)");
        low_ir_free(heap, &ir);
        if (p.forms) heap.free_fn(heap.ctx, p.forms);
        proven_array_destroy(&p.diags); proven_array_destroy(&l.tokens); proven_array_destroy(&l.diags);
    }

    // ── E-CONTRACT-DEAD: requires 가 배제한 오류는 **도달할 수 없다** ──
    // ★ 판정 기준은 **안정성**이다(사용자 결정 2026-07-12):
    //   조건의 입력이 op 중에 변할 수 **없으면** 진입 검사가 본문 전체를 덮는다 ⇒ 죽은 경로.
    //   변할 수 **있으면**(volatile · atomic · shared · mut_ref) 진입 검사가 사용 시점을 못 덮는다
    //   ⇒ 둘 다 정당하다. 중복이 아니다. 이것이 언어가 두 기제를 모두 갖는 이유를 가른다.
    check(CTCK("fn g input a u8 . output result u8 small .  "
                " requires ge a 4 . errors small lt a 4 . . do return ok a . end") == false,
          "dead error: an immutable input — `requires ge a 4` makes `small when lt a 4` unreachable");
    check(CTCK("fn g input a u8 . output result u8 small .  "
                " requires ge a 4 . errors small lt a 2 . . do return ok a . end") == true,
          "dead error: a DIFFERENT condition is not dead (no false positive)");
    check(CTCK("fn g input a u8 . output result u8 small .  "
                " errors small lt a 4 . . do return ok a . end") == true,
          "dead error: `errors` alone is fine — the op handles the case and returns a value");
    check(CTCK("fn g input a u8 . output u8 .  "
               " requires ge a 4 . do return a . end") == true,
          "dead error: `requires` alone is fine — the caller must guarantee it");
    // ★★ 그리고 값이 **변할 수 있으면** 둘 다 정당하다 — volatile/atomic 이 설 자리다.
    check(CTCK("fn f input p mut_ref u32 . . output result u32 underrun .  "
               " requires ge deref p . 4 . errors underrun lt deref p . 4 . . "
               " do return ok deref p . . end") == true,
          "dead error: ★ a MUTABLE input — the entry check does not cover the use site, so "
          "`requires` and `errors` are BOTH legitimate (this is where volatile/atomic will live)");

    // ── 계약 절의 **이름은 무언가를 가리켜야 한다** (PRINCIPLES.md §0) ──
    // `tests`·`access`·`parallel` 은 이름을 적는다. 그런데 그 이름이 아무것도 가리키지 않아도
    // **조용히 통과했다.** 선언된 중복인데 아무도 교차 검사하지 않는다 — 그것이 곧 엔트로피다.
    check(CHECK("fn f input a u8 . output u8 .  tests nope . do return a . end") == false,
          "clause names: `tests` naming an op that does not exist → E-CONTRACT-UNDEF");
    check(CHECK("fn h input a u8 . output u8 .  do return a . end "
                "fn f input a u8 . output u8 .  tests h . do return a . end") == true,
          "clause names: `tests` naming an op that DOES exist is fine");
    check(CHECK("fn g input a u8 . output u8 .  access ghost sequential . "
                " do return a . end") == false,
          "clause names: `access` naming something that is not a parameter → E-CONTRACT-UNDEF");
    check(CHECK("fn g input buf slice u8 . output u64 .  access buf nonsense . "
                " do return len buf . end") == false,
          "clause names: an unknown access mode → E-CONTRACT-MODE");
    check(CHECK("fn g input buf slice u8 . output u64 .  access buf sequential . "
                " do return len buf . end") == true,
          "clause names: a real parameter + a real mode passes (with a W-NOT-YET note — the "
          "semantics are post-MVP, and we say so instead of ignoring it silently)");
    check(CHECK("fn g input a u8 . output u8 .  parallel ghost split . "
                " do return a . end") == false,
          "clause names: `parallel` naming something that is not a parameter → E-CONTRACT-UNDEF");

    // ── RFC-0008 §6.4 (미해결 Q6): 계약에서 테스트를 뽑는다 — **계약이 오라클도 준다** ──
    // Q6 은 "requires ge length 2 → 0 1 2 3 을 어떤 알고리즘이 만드는가" 를 물었다.
    // 답: **경계값**이다. 버그는 가운데가 아니라 가장자리에 살고, 우리 구간 분석이 검사를
    // 제거한 근거도 정확히 그 가장자리의 부등식이다. 그러니 계약의 경계를 치는 것이 곧
    // **분석의 주장을 치는 것**이다.
    // ★ 그리고 기대 출력을 적을 필요가 없다 — 계약이 오라클이다:
    //     안의 값 → 계약 위반으로 트랩하면 **안 된다**   밖의 값 → **반드시** 트랩해야 한다
    {
        const char *src =
            "fn scale input a range 0 100 . output u8 .  do return mul a 2 . end "
            "fn guarded input b u8 . output u8 .  requires le b 100 . do "
            "  return mul b 2 . end";
        low_lex_result_t l = LEX(src); proven_arena_reset(&arena);
        low_parse_result_t p = low_parse(nodes, heap, &l.tokens);
        low_ir_t ir = low_ir_build(heap, &p);
        low_ir_ctest_t ct = low_ir_contract_tests(&ir, heap, false);
        check(ct.ops == 2 && ct.cases > 0,
              "contract tests: boundary values are derived from BOTH `range` types and `requires`");
        check(ct.admitted > 0 && ct.rejected > 0,
              "contract tests: the generator produces values on BOTH sides of every contract edge do");
        check(ct.failures == 0,
              "contract tests: ★ every admitted input is accepted and every rejected input traps "
              "— the contract is its own oracle, so no expected outputs are needed");
        low_ir_free(heap, &ir);
        if (p.forms) heap.free_fn(heap.ctx, p.forms);
        proven_array_destroy(&p.diags); proven_array_destroy(&l.tokens); proven_array_destroy(&l.diags);
    }
    // ★★ 길이 축 — `errors E when lt (len data) 4` 는 길이 4 를 **경계로 지목한다.**
    //   그러면 3·4·5 를 친다. 오라클은 **when 조건 그 자체**다: 어떤 길이에서도 정직한 op 은
    //   자기 선언을 어기지 않는다. 어기면 E-VM-CONTRACT 가 나오고, 그것이 곧 실패다.
    {
        const char *src =
            "module t . type bytes slice u8 . . enum e do small end "
            // 정직 — 선언과 본문이 같은 경계(4)
            "fn honest input data bytes . output result u8 e . .  "
            "  errors small lt len data . 4 . . "
            " do guard ge len data . 4 . else return error small . . . "
            "   return ok index data 0 . . end "
            // 거짓말 — 선언은 2, 본문은 9
            "fn liar input data bytes . output result u8 e . .  "
            "  errors small lt len data . 2 . . "
            " do guard ge len data . 9 . else return error small . . . "
            "   return ok index data 0 . . end";
        low_lex_result_t l = LEX(src); proven_arena_reset(&arena);
        low_parse_result_t p = low_parse(nodes, heap, &l.tokens);
        low_ir_t ir = low_ir_build(heap, &p);
        low_ir_ctest_t ct = low_ir_contract_tests(&ir, heap, false);
        check(ct.len_cases > 0,
              "contract tests: ★ the LENGTH axis — the constants a contract names in `len …` become "
              "boundary lengths (a slice op has no integer range to shake; its length is the axis)");
        check(ct.failures > 0,
              "contract tests: ★★ the generator CATCHES the lying `errors` clause by itself — it "
              "walks the boundary length, the op returns the error, and the declared `when` "
              "condition is false there. No expected output was written anywhere");
        low_ir_free(heap, &ir);
        if (p.forms) heap.free_fn(heap.ctx, p.forms);
        proven_array_destroy(&p.diags); proven_array_destroy(&l.tokens); proven_array_destroy(&l.diags);
    }
    // 그리고 **정직한 op 은 통과한다** — 과잉 고발이 아니다.
    {
        const char *src =
            "module t . type bytes slice u8 . . enum e do small end "
            "fn honest input data bytes . output result u8 e . .  "
            "  errors small lt len data . 4 . . "
            " do guard ge len data . 4 . else return error small . . . "
            "   return ok index data 0 . . end";
        low_lex_result_t l = LEX(src); proven_arena_reset(&arena);
        low_parse_result_t p = low_parse(nodes, heap, &l.tokens);
        low_ir_t ir = low_ir_build(heap, &p);
        low_ir_ctest_t ct = low_ir_contract_tests(&ir, heap, false);
        check(ct.len_cases > 0 && ct.failures == 0,
              "contract tests: an HONEST errors clause passes every boundary length "
              "(no false accusation)");
        low_ir_free(heap, &ir);
        if (p.forms) heap.free_fn(heap.ctx, p.forms);
        proven_array_destroy(&p.diags); proven_array_destroy(&l.tokens); proven_array_destroy(&l.diags);
    }
    // ★★ 이 오라클이 실제로 잡은 결함의 회귀 테스트.
    //   `range` 파라미터는 **내부 호출 지점에서만** 강제됐고 op 진입에서는 강제되지 않았다.
    //   그런데 구간 분석은 그 범위를 **사실로 믿고** 오버플로 검사를 제거했다. 그래서 프로그램
    //   **바깥**(VM 진입·C main·FFI)에서 들어온 값에는 방벽이 없었다 — 네이티브는 트랩도 없이
    //   조용히 틀린 값을 냈다(u8 op 이 -2 를 냈다). RFC-0053 §6.6 그대로다:
    //   **계약을 사실로 쓰려면 강제해야 한다.** 경계가 그 마지막 문이다.
    {
        const char *src =
            "fn scale input a range 0 100 . output u8 .  do return mul a 2 . end";
        low_lex_result_t l = LEX(src); proven_arena_reset(&arena);
        low_parse_result_t p = low_parse(nodes, heap, &l.tokens);
        low_ir_t ir = low_ir_build(heap, &p);
        proven_i64 lo = -1, hi = 101, ok = 50;
        low_ir_run_result_t r = low_ir_run(&ir, proven_u8str_view_from_cstr("scale"), &ok, 1, heap, &ir.diags);
        check(r.ok && r.value == 100, "boundary: a value inside the declared range runs");
        r = low_ir_run(&ir, proven_u8str_view_from_cstr("scale"), &hi, 1, heap, &ir.diags);
        check(!r.ok && HASDIAG(ir.diags, "E-VM-CONTRACT"),
              "boundary: ★ a value ABOVE the declared range traps at the program boundary "
              "(before this, it slipped through — the analysis had already removed the check)");
        r = low_ir_run(&ir, proven_u8str_view_from_cstr("scale"), &lo, 1, heap, &ir.diags);
        check(!r.ok && HASDIAG(ir.diags, "E-VM-CONTRACT"),
              "boundary: a value BELOW the declared range traps at the program boundary");
        low_ir_free(heap, &ir);
        if (p.forms) heap.free_fn(heap.ctx, p.forms);
        proven_array_destroy(&p.diags); proven_array_destroy(&l.tokens); proven_array_destroy(&l.diags);
    }

    // ── RFC-0053 §6.6 dual: `ensures` — 계약이 **호출자에게 되돌아온다** ──
    // requires 가 계약을 callee 안으로 나른다면, ensures 는 그 반대 방향이다.
    // 그리고 같은 규칙이 걸린다: **사실로 쓰려면 강제해야 한다.**
    {
        const char *src =
            // ① 증명된다: a ∈ [0,100] → a*2 ∈ [0,200] → `le ret 200` 은 참. 출구 검사 제거.
            "fn double_it input a range 0 100 . output u16 .  ensures le ret 200 . do "
            "  return mul a 2 . end "
            // ② ★ 호출자가 그 사실을 **받는다** — [0,200] ⊆ u8 이므로 narrow 가 증명된다.
            //    ensures 가 없었다면 u16 전체 [0,65535] 라 검사가 남았을 것이다.
            "fn use_it input x range 0 100 . output u8 .  do "
            "  return narrow u8 (double_it x) . end "
            // ③ 지키지 못하는 약속 — 증명 실패 → 검사 유지 → 어기면 **이 op 이 고발당한다**
            "fn liar input a u16 . output u16 .  ensures le ret 200 . do "
            "  return mul a 2 . end";
        low_lex_result_t l = LEX(src); proven_arena_reset(&arena);
        low_parse_result_t p = low_parse(nodes, heap, &l.tokens);
        low_ir_t ir = low_ir_build(heap, &p);
        check(ir.ok, "ensures: the fixture lowers (a hidden `ret` local carries the value)");
        check(ir.checks_proven == 4 && ir.checks_total == 6,
              "ensures: the provable postcondition is discharged, AND the caller's narrow is "
              "discharged **because** the callee promised [0,200] — the liar's checks stay");
        proven_i64 v50 = 50, v100 = 100, v150 = 150;
        low_ir_run_result_t r = low_ir_run(&ir, proven_u8str_view_from_cstr("use_it"), &v100, 1, heap, &ir.diags);
        check(r.ok && r.value == 200,
              "ensures: ★ the contract crosses the op boundary BACKWARDS — the caller narrows "
              "u16 to u8 with NO check, purely because the callee promised le ret 200");
        r = low_ir_run(&ir, proven_u8str_view_from_cstr("liar"), &v50, 1, heap, &ir.diags);
        check(r.ok && r.value == 100, "ensures: the liar runs fine when it happens to keep its promise");
        r = low_ir_run(&ir, proven_u8str_view_from_cstr("liar"), &v150, 1, heap, &ir.diags);
        check(!r.ok && HASDIAG(ir.diags, "E-VM-CONTRACT"),
              "ensures: a broken postcondition traps at EXIT — **this op** broke its own promise "
              "(not the caller's fault; the diagnostic says so)");
        low_ir_free(heap, &ir);
        if (p.forms) heap.free_fn(heap.ctx, p.forms);
        proven_array_destroy(&p.diags); proven_array_destroy(&l.tokens); proven_array_destroy(&l.diags);
    }
    // ★ 순환 금지 — 진입 requires 검사는 **절대** 제거되지 않는다.
    //   분석이 requires 를 사실로 심었으므로, 그 검사를 지우면 자기가 심은 사실로 자기를
    //   정당화하게 된다(그러면 계약이 거짓말이 되고, 예전에 u8 이 256 을 냈던 그 버그로 돌아간다).
    {
        const char *src =
            "fn guarded input a u8 . output u8 .  requires le a 100 . do "
            "  return mul a 2 . end";
        low_lex_result_t l = LEX(src); proven_arena_reset(&arena);
        low_parse_result_t p = low_parse(nodes, heap, &l.tokens);
        low_ir_t ir = low_ir_build(heap, &p);
        proven_i64 v255 = 255;
        low_ir_run_result_t r = low_ir_run(&ir, proven_u8str_view_from_cstr("guarded"), &v255, 1, heap, &ir.diags);
        check(!r.ok && HASDIAG(ir.diags, "E-VM-CONTRACT"),
              "ensures/requires: the ENTRY check is never eliminated — eliminating it would let the "
              "analysis justify itself with a fact it planted (circular), and the contract becomes a lie");
        low_ir_free(heap, &ir);
        if (p.forms) heap.free_fn(heap.ctx, p.forms);
        proven_array_destroy(&p.diags); proven_array_destroy(&l.tokens); proven_array_destroy(&l.diags);
    }

    // ── RFC-0055 D5: 배열 인덱스 경계 검사 제거 — **관계 사실**이 필요하다 ──
    // 구간 영역만으로는 절대 못 한다: `index g i` 의 안전성은 `i < len(g)` 인데
    // len(g) 는 런타임 값이라 구간으로는 ⊤ 다. 그래서 구간 옆에 한 칸짜리 관계를 든다.
    {
        const char *src =
            // ① 가드가 사실을 만든다
            "fn at_guarded input i u64 . input g slice u8 . output u64 .  do "
            "  if lt i (len g) . do return index g i . end return 0 . end "
            // ② 루프 — 사실이 매 반복의 조건에서 다시 세워진다
            "fn sum_all input g slice u8 . output u64 .  do "
            "  var acc u64 be 0 . var i u64 be 0 . "
            "  while lt i (len g) . do set acc expr acc + (index g i) . set i expr i + 1 . . end "
            "  return acc . end "
            // ③ mod 이디엄 — ★ mod_is_a_safe_index (Qed) 가 그대로 근거다
            "fn wrapped input i u64 . input g slice u8 . output u64 .  do "
            "  return index g (mod i (len g)) . end "
            // ④ 가드 없음 — 검사가 **남아야** 한다
            "fn unguarded input i u64 . input g slice u8 . output u64 .  do "
            "  return index g i . end "
            // ⑤ 다른 슬라이스 — len(g) 를 안다고 h 를 인덱싱할 수는 없다
            "fn crossed input i u64 . input h slice u8 . input g slice u8 . output u64 .  do "
            "  if lt i (len g) . do return index h i . end return 0 . end";
        low_lex_result_t l = LEX(src); proven_arena_reset(&arena);
        low_parse_result_t p = low_parse(nodes, heap, &l.tokens);
        low_ir_t ir = low_ir_build(heap, &p);
        check(ir.ok, "bounds: the fixture lowers");
        // 색인 검사는 5개(op 마다 index 1개)이고 제거 가능한 것은 ①②③ 뿐이다.
        // ★ 그런데 `checks_proven` 은 **색인만 세지 않는다** — 산술·나눗셈·좁힘을 다 센다.
        //   2026-08-17(PERF-0003)에 64비트 산술이 증명 가능해지면서 ②의 `i + 1` 이 더해져
        //   3 → **4** 가 됐다(`i < len(g)` 이고 길이 상계는 자른 값이 아니다).
        //   ☞ 이 수가 움직였다고 색인 판정이 바뀐 것은 아니다. 그 판정은 **아래 격자**가
        //     행동으로 검사한다(거짓 제거는 곧 E-VM-ANALYSIS 다) — 수를 세는 검사와
        //     성질을 재는 검사가 여기 나란히 있는 이유다.
        check(ir.checks_proven == 4,
              "bounds: the provable checks are discharged and the unprovable ones are not — "
              "3 index checks (guarded, loop, mod) plus the loop counter's `i + 1`, which became "
              "provable when 64-bit arithmetic stopped being refused outright (PERF-0003). "
              "The unguarded and cross-slice indexes keep theirs");

        // ★ 소진 검사 — 이 제거가 건전한가. 네이티브는 검사를 **실제로 지우므로**
        //   거짓 제거는 곧 메모리 안전성 위반이다. VM 은 검사를 유지하고 스스로를
        //   고발하므로(E-VM-ANALYSIS), 격자를 전부 돌려 고발이 한 번도 안 나오는지 본다.
        int accused = 0, ran = 0;
        for (proven_i64 len = 0; len <= 8; len++) {
            for (proven_i64 i = 0; i <= 20; i++) {
                proven_i64 args[9];
                args[0] = i;
                for (proven_i64 k = 0; k < len; k++) args[1 + k] = 10 + k;
                const char *ops[] = { "at_guarded", "wrapped", "unguarded", "sum_all" };
                for (int o = 0; o < 4; o++) {
                    bool slice_only = (o == 3);                  // sum_all 은 슬라이스 하나만 받는다
                    const proven_i64 *ap = slice_only ? args + 1 : args;
                    proven_size_t an = slice_only ? (proven_size_t)len : (proven_size_t)(1 + len);
                    (void)low_ir_run(&ir, proven_u8str_view_from_cstr(ops[o]), ap, an, heap, &ir.diags);
                    ran++;
                }
            }
        }
        if (HASDIAG(ir.diags, "E-VM-ANALYSIS")) accused++;   // 격자 전체에서 단 한 번이라도 나오면 실패
        printf("  (bounds model check: %d runs over len 0..8 × i 0..20 — %d self-accusations)\n", ran, accused);
        check(accused == 0,
              "bounds: NO false elimination across the whole grid — every discharged index check "
              "really was in bounds (a wrong removal would fire E-VM-ANALYSIS, and in native code "
              "it would be an out-of-bounds read)");
        // 그리고 제거되지 **않은** 검사는 여전히 잡는다(건전성의 반대쪽: 과잉 제거가 없다).
        {
            proven_i64 args[4] = { 99, 10, 20, 30 };
            low_ir_run_result_t r = low_ir_run(&ir, proven_u8str_view_from_cstr("unguarded"), args, 4, heap, &ir.diags);
            check(!r.ok && HASDIAG(ir.diags, "E-VM-BOUNDS"),
                  "bounds: the unguarded index still traps out of bounds (the check was kept)");
        }
        low_ir_free(heap, &ir);
        if (p.forms) heap.free_fn(heap.ctx, p.forms);
        proven_array_destroy(&p.diags); proven_array_destroy(&l.tokens); proven_array_destroy(&l.diags);
    }

    // ── RFC-0054: 타깃 조건부 컴파일 — machine 질의 + **검사되는** 분기 ──
    {
        const char *src =
            "fn wsize output u64 .  do return machine.ptr_width . end "
            "fn etag output u64 .  do "
            "  if machine.big_endian . do return 1 . end else do return 0 . end end "
            "fn fpu input a u64 . output u64 .  do "
            "  if machine.has_fpu . do return add a 1 . end else do return add a 2 . end end";
        const char *targets[3] = { "x86_64", "cortex_m", "mips_be" };
        proven_i64 want_ptr[3] = { 64, 32, 32 };
        proven_i64 want_end[3] = { 0, 0, 1 };
        proven_i64 want_fpu[3] = { 11, 12, 11 };
        for (int ti = 0; ti < 3; ti++) {
            low_ir_set_target(targets[ti]);
            low_lex_result_t l = LEX(src); proven_arena_reset(&arena);
            low_parse_result_t p = low_parse(nodes, heap, &l.tokens);
            low_ir_t ir = low_ir_build(heap, &p);
            proven_i64 ten = 10;
            low_ir_run_result_t r = low_ir_run(&ir, proven_u8str_view_from_cstr("wsize"), NULL, 0, heap, &ir.diags);
            bool ok = ir.ok && r.ok && r.value == want_ptr[ti];
            r = low_ir_run(&ir, proven_u8str_view_from_cstr("etag"), NULL, 0, heap, &ir.diags);
            ok = ok && r.ok && r.value == want_end[ti];
            r = low_ir_run(&ir, proven_u8str_view_from_cstr("fpu"), &ten, 1, heap, &ir.diags);
            ok = ok && r.ok && r.value == want_fpu[ti];
            ok = ok && ir.folds == 2;   // 두 분기가 comptime 에 접혔다(런타임 비용 0)
            if (ti == 0) check(ok, "RFC-0054: `machine.*` selects code per target (x86_64)");
            if (ti == 1) check(ok, "RFC-0054: … and per target again (cortex_m: 32b, no FPU)");
            if (ti == 2) check(ok, "RFC-0054: … including endianness (mips_be)");
            low_ir_free(heap, &ir);
            if (p.forms) heap.free_fn(heap.ctx, p.forms);
            proven_array_destroy(&p.diags); proven_array_destroy(&l.tokens); proven_array_destroy(&l.diags);
        }
        low_ir_set_target("x86_64");
        // ★ 이것이 #ifdef 와의 결정적 차이다: **선택되지 않은 가지도 타입 검사를 받는다.**
        // C 라면 else 안의 오류가 3년 뒤 다른 플랫폼 빌드에서 처음 터진다.
        check(TYCK("fn f input a u8 . input b i32 . output u8 . do "
                   "  if machine.has_fpu . do return add a 1 . end "
                   "  else do return add a b . end end") == false,
              "RFC-0054: a type error in the DEAD arm is still caught — the #ifdef disease is cured");
    }

    // ── RFC-0053 §8-1: 루프 정밀도 — CFG 고정점 + 위드닝 + **분기 조건 내로잉** ──
    {
        const char *src =
            // ① 루프 카운터: while lt i 10 → 본문에서 i ∈ [0,9] → add i 1 은 u8 안에서 안전
            "fn counter output u8 .  do "
            "  var i u8 be 0 . while lt i 10 . do set i expr i + 1 . end return i . end "
            // ② 계약이 **루프 안까지** 전달된다: n ≤ 100 ∧ i < n ⇒ i ≤ 99
            "fn guarded input n u8 . output u8 . requires le n 100 .  do "
            "  var i u8 be 0 . while lt i n . do set i expr i + 1 . end return i . end "
            // ③ 경계가 없으면 증명 못 한다 — 보수적이되 건전(거짓 제거 없음)
            "fn unbounded input n u8 . output u8 .  do "
            "  var i u8 be 0 . while lt i n . do set i expr i + 100 . end return i . end";
        low_lex_result_t l = LEX(src); proven_arena_reset(&arena);
        low_parse_result_t p = low_parse(nodes, heap, &l.tokens);
        low_ir_t ir = low_ir_build(heap, &p);
        check(ir.ok, "loop IV: fixture lowers");
        check(ir.checks_total == 3 && ir.checks_proven == 2,
              "loop precision: the counter and the contracted bound are PROVEN inside the loop; "
              "the unbounded one is not (conservative, never optimistic)");
        low_ir_run_result_t r = low_ir_run(&ir, proven_u8str_view_from_cstr("counter"), NULL, 0, heap, &ir.diags);
        check(r.ok && r.value == 10, "loop IV: the proven loop still computes correctly");
        proven_i64 n7 = 7;
        r = low_ir_run(&ir, proven_u8str_view_from_cstr("guarded"), &n7, 1, heap, &ir.diags);
        check(r.ok && r.value == 7,
              "loop IV: `requires le n 100` reaches INSIDE the loop (i < n ⇒ i ≤ 99)");
        low_ir_free(heap, &ir);
        if (p.forms) heap.free_fn(heap.ctx, p.forms);
        proven_array_destroy(&p.diags); proven_array_destroy(&l.tokens); proven_array_destroy(&l.diags);
    }

    // ── RFC-0053 §8-7 결정: 계약을 사실로 쓰려면 **강제해야 한다**. assume 은 못 쓴다. ──
    {
        const char *src =
            // 검사되는 계약 → 진입 검사 + 사실로 사용 → 오버플로 검사 제거
            "fn checked input a u8 . output u8 . requires le a 200 .  "
            "  do return add a 1 . end "
            // assume 계약 → 검사 안 함(선언 전용) → **사실로 쓰지 않는다** → 검사 유지
            "fn assumed input a u8 . output u8 . requires assume le a 200 .  "
            "  do return add a 1 . end";
        low_lex_result_t l = LEX(src); proven_arena_reset(&arena);
        low_parse_result_t p = low_parse(nodes, heap, &l.tokens);
        low_ir_t ir = low_ir_build(heap, &p);
        check(ir.ok, "assume: fixture lowers");
        check(ir.checks_total == 2 && ir.checks_proven == 1,
              "assume is NOT used to remove checks — only the enforced contract is (spec §6.3: "
              "assume = HINT, never ELIM)");
        proven_i64 v200 = 200, v255 = 255;
        low_ir_run_result_t r;
        r = low_ir_run(&ir, proven_u8str_view_from_cstr("checked"), &v200, 1, heap, &ir.diags);
        check(r.ok && r.value == 201, "an honoured contract runs, and its check was removed");
        // 이것이 이번 수정의 핵심: 전에는 256 을 조용히 반환했다(u8 op 가!).
        r = low_ir_run(&ir, proven_u8str_view_from_cstr("checked"), &v255, 1, heap, &ir.diags);
        check(!r.ok && HASDIAG(ir.diags, "E-VM-CONTRACT"),
              "a BROKEN contract traps at entry — the fact the analysis relied on is enforced "
              "(before this fix a u8 op silently returned 256)");
        // assume 을 어겨도 UB 가 되지 않는다: 검사를 제거하지 않았으므로 오버플로가 잡힌다.
        r = low_ir_run(&ir, proven_u8str_view_from_cstr("assumed"), &v255, 1, heap, &ir.diags);
        check(!r.ok && HASDIAG(ir.diags, "E-VM-OVERFLOW"),
              "a broken `assume` does NOT become UB — its check was never removed");
        low_ir_free(heap, &ir);
        if (p.forms) heap.free_fn(heap.ctx, p.forms);
        proven_array_destroy(&p.diags); proven_array_destroy(&l.tokens); proven_array_destroy(&l.diags);
    }

    // ── RFC-0053 E5 (P2′ 오차 가시): sum_neumaier(보정) vs sum_seq(축차) — 오차가 이름에 있다 ──
    {
        const char *src =
            "type bytes slice u8 . . "
            "fn s_good input b bytes . output f64 .  do "
            "  var xs u64 . be view_array f64 b . return sum_neumaier xs . end "
            "fn s_fast input b bytes . output f64 .  do "
            "  var xs u64 . be view_array f64 b . return sum_seq xs . end";
        low_lex_result_t l = LEX(src); proven_arena_reset(&arena);
        low_parse_result_t p = low_parse(nodes, heap, &l.tokens);
        low_ir_t ir = low_ir_build(heap, &p);
        check(ir.ok, "E5: sum_neumaier / sum_seq lower");
        // [1e16, 1.0, -1e16] — 1e16 + 1 은 double 에서 반올림돼 1 을 잃는다(2^53 ≈ 9e15).
        double xs[3] = { 1e16, 1.0, -1e16 };
        proven_u8 raw[24];
        memcpy(raw, xs, 24);
        proven_i64 args[24];
        for (int i = 0; i < 24; i++) args[i] = raw[i];
        low_ir_run_result_t r = low_ir_run(&ir, proven_u8str_view_from_cstr("s_fast"), args, 24, heap, &ir.diags);
        check(r.ok && strcmp(r.text, "0.0") == 0,
              "E5: sum_seq (naive) LOSES the 1 — error is O(n·eps), and the name says so");
        r = low_ir_run(&ir, proven_u8str_view_from_cstr("s_good"), args, 24, heap, &ir.diags);
        check(r.ok && strcmp(r.text, "1.0") == 0,
              "E5: sum_neumaier KEEPS it — error is O(eps), independent of the term count. "
              "The accurate one is the DEFAULT; the fast one wears its cost in its name (P2').");
        low_ir_free(heap, &ir);
        if (p.forms) heap.free_fn(heap.ctx, p.forms);
        proven_array_destroy(&p.diags); proven_array_destroy(&l.tokens); proven_array_destroy(&l.diags);
    }

    // ── RFC-0053 E4: 하나의 분석이 세 기둥을 덮는다 — overflow · division · narrowing ──
    {
        const char *src =
            "fn dsafe input a u32 . input b u32 . output u32 . requires ge b 1 .  "
            "  do return div a b . end "
            "fn dbare input a u32 . input b u32 . output u32 .  "
            "  do return div a b . end "
            "fn nsafe input x u32 . output u8 . requires le x 200 .  "
            "  do return narrow u8 x . end "
            "fn nbare input x u32 . output u8 .  "
            "  do return narrow u8 x . end "
            "fn ntry input x u32 . output u64 .  do "
            "  var r u64 . be narrow_try u8 x . "
            "  guard is_some r . else return 999 . . "
            "  return some_value r . end";
        low_lex_result_t l = LEX(src); proven_arena_reset(&arena);
        low_parse_result_t p = low_parse(nodes, heap, &l.tokens);
        low_ir_t ir = low_ir_build(heap, &p);
        check(ir.ok, "E4: fixture lowers");
        check(ir.checks_total == 4 && ir.checks_proven == 2,
              "E4: one analysis discharges BOTH the div0 check and the narrowing trap "
              "(the contracted ops; the bare ones keep theirs)");
        proven_i64 ab[2] = { 10, 2 }, v200 = 200, v300 = 300, v42 = 42, z[2] = { 10, 0 };
        low_ir_run_result_t r = low_ir_run(&ir, proven_u8str_view_from_cstr("dsafe"), ab, 2, heap, &ir.diags);
        check(r.ok && r.value == 5, "E4: `requires ge b 1` removes the div0 check — still correct");
        r = low_ir_run(&ir, proven_u8str_view_from_cstr("dbare"), z, 2, heap, &ir.diags);
        check(!r.ok && HASDIAG(ir.diags, "E-VM-DIV0"),
              "E4: without the contract the div0 check stays (sound: no false removal)");
        r = low_ir_run(&ir, proven_u8str_view_from_cstr("nsafe"), &v200, 1, heap, &ir.diags);
        check(r.ok && r.value == 200, "E4: `requires le x 200` removes the narrowing trap");
        r = low_ir_run(&ir, proven_u8str_view_from_cstr("ntry"), &v300, 1, heap, &ir.diags);
        check(r.ok && r.value == 999, "narrow_try: out of range → none (a value, not a trap)");
        r = low_ir_run(&ir, proven_u8str_view_from_cstr("ntry"), &v42, 1, heap, &ir.diags);
        check(r.ok && r.value == 42, "narrow_try: in range → some(v)");
        low_ir_free(heap, &ir);
        if (p.forms) heap.free_fn(heap.ctx, p.forms);
        proven_array_destroy(&p.diags); proven_array_destroy(&l.tokens); proven_array_destroy(&l.diags);
    }

    // ── RFC-0052 D5/D6: 실패를 값으로 — chk_* (오버플로) · nonzero_of + div_nz (0 나누기) ──
    {
        const char *src =
            "fn chk input a u8 . output u64 .  do "
            "  var r u64 . be chk_add a 1 . "
            "  guard is_some r . else return 999 . . "
            "  return some_value r . end "
            "fn dz input a u32 . input b u32 . output u32 .  do "
            "  var nb u32 . be nonzero_of b . "
            "  guard is_some nb . else return 0 . . "
            "  return div_nz a (some_value nb) . end";
        low_lex_result_t l = LEX(src); proven_arena_reset(&arena);
        low_parse_result_t p = low_parse(nodes, heap, &l.tokens);
        low_ir_t ir = low_ir_build(heap, &p);
        check(ir.ok, "D5/D6: chk_* and nonzero_of/div_nz lower");
        proven_i64 a255 = 255, a1 = 1, ab[2];
        low_ir_run_result_t r;
        r = low_ir_run(&ir, proven_u8str_view_from_cstr("chk"), &a255, 1, heap, &ir.diags);
        check(r.ok && r.value == 999,
              "D5: chk_add returns none on overflow — a value, not a trap");
        r = low_ir_run(&ir, proven_u8str_view_from_cstr("chk"), &a1, 1, heap, &ir.diags);
        check(r.ok && r.value == 2, "D5: chk_add returns some(v) when it fits");
        ab[0] = 10; ab[1] = 2;
        r = low_ir_run(&ir, proven_u8str_view_from_cstr("dz"), ab, 2, heap, &ir.diags);
        check(r.ok && r.value == 5, "D6: div_nz divides once the divisor is proven nonzero");
        ab[1] = 0;
        r = low_ir_run(&ir, proven_u8str_view_from_cstr("dz"), ab, 2, heap, &ir.diags);
        check(r.ok && r.value == 0,
              "D6: a zero divisor is consumed by the guard — div_nz is never reached (no trap)");
        low_ir_free(heap, &ir);
        if (p.forms) heap.free_fn(heap.ctx, p.forms);
        proven_array_destroy(&p.diags); proven_array_destroy(&l.tokens); proven_array_destroy(&l.diags);
    }

    // ── RFC-0052 D3: 부동 리터럴 — comptime 무타입, 범위 초과는 컴파일 오류 ──
    check(TYCK("fn f output f32 . do var a f32 be 3.0e38 . return a . end") == true,
          "float literal: 3.0e38 fits f32");
    check(TYCK("fn f output f32 . do var a f32 be 1e50 . return a . end") == false,
          "float literal: 1e50 does NOT fit f32 → E-TYPE-WIDTH (was a silent inf)");
    check(TYCK("fn f output f64 . do var a f64 be 1e300 . return a . end") == true,
          "float literal: 1e300 fits f64");
    check(TYCK("fn f output f32 . do var a f32 be 1e-50 . return a . end") == true,
          "float literal: underflow to zero is IEEE, not an error");
    check(TYCK("fn f input a f32 . output f32 . do return add a 1.0 . end") == true,
          "float literal adopts the operand type (f32 + 1.0 stays f32 — no f64 promotion)");

    // ── RFC-0052 S4: 부동 폭 충실 (D9) + 빌트인 정리 (D14) ──
    {
        const char *src =
            "fn f32r output bool .  do "
            "  var a f32 be 16777216.0 . return eq (add a 1.0) a . end "
            "fn f64k output bool .  do "
            "  var a f64 be 16777216.0 . return eq (add a 1.0) a . end "
            "fn isqrt input x u64 . output u64 .  do return sqrt x . end "
            "fn fsqrt input x u64 . output f64 .  do return sqrt (cast f64 x) . end";
        low_lex_result_t l = LEX(src); proven_arena_reset(&arena);
        low_parse_result_t p = low_parse(nodes, heap, &l.tokens);
        low_ir_t ir = low_ir_build(heap, &p);
        check(ir.ok, "S4: fixture lowers");
        low_ir_run_result_t r;
        // 2^24 + 1 is not representable in f32 — if the sum were computed in double
        // (the C FLT_EVAL_METHOD trap) the equality would be false.
        r = low_ir_run(&ir, proven_u8str_view_from_cstr("f32r"), NULL, 0, heap, &ir.diags);
        check(r.ok && r.value == 1,
              "S4: f32 arithmetic rounds at f32 — no hidden double precision (D9)");
        r = low_ir_run(&ir, proven_u8str_view_from_cstr("f64k"), NULL, 0, heap, &ir.diags);
        check(r.ok && r.value == 0, "S4: the same sum in f64 keeps the extra bit");
        proven_i64 nine = 9;
        r = low_ir_run(&ir, proven_u8str_view_from_cstr("isqrt"), &nine, 1, heap, &ir.diags);
        check(!r.ok && HASDIAG(ir.diags, "E-VM-TYPE"),
              "S4: sqrt of an int is rejected — no implicit promotion (was 3; D14/G2)");
        r = low_ir_run(&ir, proven_u8str_view_from_cstr("fsqrt"), &nine, 1, heap, &ir.diags);
        check(r.ok && strcmp(r.text, "3.0") == 0, "S4: sqrt works once the conversion is explicit");
        low_ir_free(heap, &ir);
        if (p.forms) heap.free_fn(heap.ctx, p.forms);
        proven_array_destroy(&p.diags); proven_array_destroy(&l.tokens); proven_array_destroy(&l.diags);
    }

    // Regression: the lowering's type shadow stack must be reset PER DEF. It was not,
    // so every op after the first silently lost width/sign/precision faithfulness —
    // the same program gave different answers depending on where it sat in the file.
    // The test therefore places the check in the SECOND op on purpose.
    {
        const char *src =
            "fn first output u64 .  do return add 1 2 . end "
            "fn second output bool .  do "
            "  var a f32 be 16777216.0 . return eq (add a 1.0) a . end "
            "fn third input x u8 . output u8 .  do return add x 1 . end";
        low_lex_result_t l = LEX(src); proven_arena_reset(&arena);
        low_parse_result_t p = low_parse(nodes, heap, &l.tokens);
        low_ir_t ir = low_ir_build(heap, &p);
        low_ir_run_result_t r = low_ir_run(&ir, proven_u8str_view_from_cstr("second"), NULL, 0, heap, &ir.diags);
        check(r.ok && r.value == 1,
              "type tracking per-def: f32 precision holds in the SECOND op too");
        proven_i64 v255 = 255;
        r = low_ir_run(&ir, proven_u8str_view_from_cstr("third"), &v255, 1, heap, &ir.diags);
        check(!r.ok && HASDIAG(ir.diags, "E-VM-OVERFLOW"),
              "type tracking per-def: the u8 overflow trap holds in the THIRD op too");
        low_ir_free(heap, &ir);
        if (p.forms) heap.free_fn(heap.ctx, p.forms);
        proven_array_destroy(&p.diags); proven_array_destroy(&l.tokens); proven_array_destroy(&l.diags);
    }

    // ── RFC-0052 S3: 폭 충실 산술 + 처분 정책 (트랩 / wrap / sat) ──
    {
        const char *src =
            "fn u8ovf  input a u8 . output u8 .  do return add a 1 . end "
            "fn u8wrap input a u8 . output u8 .  do return wrap_add a 1 . end "
            "fn u8sat  input a u8 . output u8 .  do return sat_add a 1 . end "
            "fn nar    input x u32 . output u8 .  do return narrow u8 x . end "
            "fn narw   input x u32 . output u8 .  do return narrow_wrap u8 x . end "
            "fn nars   input x u32 . output u8 .  do return narrow_sat u8 x . end "
            "fn wide   input x u8 . output u32 .  do return widen u32 x . end";
        low_lex_result_t l = LEX(src); proven_arena_reset(&arena);
        low_parse_result_t p = low_parse(nodes, heap, &l.tokens);
        low_ir_t ir = low_ir_build(heap, &p);
        check(ir.ok, "S3: policy op families lower");
        proven_i64 v255 = 255, v300 = 300, v7 = 7, v1 = 1;
        low_ir_run_result_t r;
        r = low_ir_run(&ir, proven_u8str_view_from_cstr("u8ovf"), &v255, 1, heap, &ir.diags);
        check(!r.ok && HASDIAG(ir.diags, "E-VM-OVERFLOW"),
              "S3: u8 255 + 1 TRAPS — arithmetic happens at the declared width (was 256)");
        r = low_ir_run(&ir, proven_u8str_view_from_cstr("u8ovf"), &v1, 1, heap, &ir.diags);
        check(r.ok && r.value == 2, "S3: in-range arithmetic is unaffected");
        r = low_ir_run(&ir, proven_u8str_view_from_cstr("u8wrap"), &v255, 1, heap, &ir.diags);
        check(r.ok && r.value == 0, "S3: wrap_add wraps at the declared width (255 + 1 = 0)");
        r = low_ir_run(&ir, proven_u8str_view_from_cstr("u8sat"), &v255, 1, heap, &ir.diags);
        check(r.ok && r.value == 255, "S3: sat_add saturates (255 + 1 = 255)");
        r = low_ir_run(&ir, proven_u8str_view_from_cstr("nar"), &v300, 1, heap, &ir.diags);
        check(!r.ok && HASDIAG(ir.diags, "E-VM-CAST"),
              "S3: narrow TRAPS out of range — no silent masking (was 44)");
        r = low_ir_run(&ir, proven_u8str_view_from_cstr("narw"), &v300, 1, heap, &ir.diags);
        check(r.ok && r.value == 44, "S3: narrow_wrap masks (300 → 44) — the policy is now named");
        r = low_ir_run(&ir, proven_u8str_view_from_cstr("nars"), &v300, 1, heap, &ir.diags);
        check(r.ok && r.value == 255, "S3: narrow_sat saturates (300 → 255)");
        r = low_ir_run(&ir, proven_u8str_view_from_cstr("wide"), &v7, 1, heap, &ir.diags);
        check(r.ok && r.value == 7, "S3: widen is total — it cannot fail (Coq: widen_never_fails)");
        low_ir_free(heap, &ir);
        if (p.forms) heap.free_fn(heap.ctx, p.forms);
        proven_array_destroy(&p.diags); proven_array_destroy(&l.tokens); proven_array_destroy(&l.diags);
    }

    // ── RFC-0052 S2: 부호 충실 런타임 (div·mod·순서 비교) ──
    {
        const char *src =
            "fn ubig output bool .  do "
            "  var big u64 be 9223372036854775808 . return lt big 1 . end "
            "fn udiv output u64 .  do "
            "  var big u64 be 9223372036854775808 . return div big 2 . end "
            "fn ineg input a i32 . output bool .  do return lt a 0 . end "
            "fn idiv input a i32 . output i32 .  do return div a 2 . end";
        low_lex_result_t l = LEX(src); proven_arena_reset(&arena);
        low_parse_result_t p = low_parse(nodes, heap, &l.tokens);
        low_ir_t ir = low_ir_build(heap, &p);
        check(ir.ok, "S2: fixture lowers");
        low_ir_run_result_t r = low_ir_run(&ir, proven_u8str_view_from_cstr("ubig"), NULL, 0, heap, &ir.diags);
        check(r.ok && r.value == 0,
              "S2: u64 2^63 is NOT < 1 — compares follow the declared sign (was 1: the old bug)");
        r = low_ir_run(&ir, proven_u8str_view_from_cstr("udiv"), NULL, 0, heap, &ir.diags);
        check(r.ok && (proven_u64)r.value == (1ull << 62),
              "S2: unsigned division of 2^63 gives 2^62 (was a negative quotient)");
        proven_i64 neg5 = -5, neg7 = -7;
        r = low_ir_run(&ir, proven_u8str_view_from_cstr("ineg"), &neg5, 1, heap, &ir.diags);
        check(r.ok && r.value == 1, "S2: signed types still compare signed");
        r = low_ir_run(&ir, proven_u8str_view_from_cstr("idiv"), &neg7, 1, heap, &ir.diags);
        check(r.ok && r.value == -3, "S2: signed division truncates toward zero (-7 / 2 = -3)");
        low_ir_free(heap, &ir);
        if (p.forms) heap.free_fn(heap.ctx, p.forms);
        proven_array_destroy(&p.diags); proven_array_destroy(&l.tokens); proven_array_destroy(&l.diags);
    }

    // ── 의미 엔트로피 지표: 1-edit silent acceptance rate ──
    {
        // ★ 지표는 **컴파일러 전체**를 돌려야 한다. 그런데 여기는 typecheck + region 만
        //   돌리고 있었다 — low_check(effect·절 이름)와 low_contract(errors·when)를 **안 돌렸다.**
        //   그래서 지표가 **자기가 재는 것을 과소평가**했다. 도구가 검사한 것만 주장해야 하듯이,
        //   지표도 **실제로 돌린 것만** 재야 한다. 넷 다 돌린다.
        #define MPCK(s) ({ low_lex_result_t _l = LEX(s); proven_arena_reset(&arena);           \
            low_parse_result_t _p = low_parse(nodes, heap, &_l.tokens);                        \
            low_check_result_t _c = low_check(heap, &_p);                                      \
            low_typecheck_result_t _t = low_typecheck(heap, &_p);                              \
            low_contract_result_t _k = low_contract(heap, &_p);                                \
            low_region_result_t _r = low_region(heap, &_p);                                    \
            bool _ok = _p.ok && _c.ok && _t.ok && _k.ok && _r.ok;                              \
            proven_array_destroy(&_c.diags); proven_array_destroy(&_t.diags);                  \
            proven_array_destroy(&_k.diags); proven_array_destroy(&_r.diags);                  \
            if (_p.forms) heap.free_fn(heap.ctx, _p.forms);                                    \
            proven_array_destroy(&_p.diags); proven_array_destroy(&_l.tokens);                 \
            proven_array_destroy(&_l.diags); _ok; })

        int base_ok = 0, mutants[MC_N_CLASS] = { 0 }, caught[MC_N_CLASS] = { 0 };
        char src[2048];
        for (int seed = 1; seed <= 120; seed++) {
            mp_t base;
            mp_gen(&base, (proven_u64)seed);
            mp_emit(&base, src, sizeof src);
            if (!MPCK(src)) continue;              // only measure from a VALID program
            base_ok++;
            for (int c = 0; c < MC_N_CLASS; c++)
                for (int which = 0; which < 4; which++) {
                    mp_t mut = base;               // exactly one edit from `base`
                    if (!mp_mutate(&mut, (mp_class_t)c, which)) continue;
                    mp_emit(&mut, src, sizeof src);
                    mutants[c]++;
                    if (!MPCK(src)) caught[c]++;   // rejected → the edit was DETECTED
                }
        }
        printf("  (1-edit mutation metric — %d valid base programs)\n", base_ok);
        int type_mut = 0, type_caught = 0;
        for (int c = 0; c < MC_N_CLASS; c++) {
            int m = mutants[c], k = caught[c];
            printf("      %-30s  %4d mutants  %3d%% detected%s\n", MP_CLASS_NAME[c], m,
                   m ? 100 * k / m : 0, c <= MC_KIND ? "   ← RFC-0052 target" : "");
            if (c <= MC_KIND) { type_mut += m; type_caught += k; }
        }
        printf("      TYPE classes combined: %d%% detected  (silent acceptance = %d%%)\n",
               type_mut ? 100 * type_caught / type_mut : 0,
               type_mut ? 100 - 100 * type_caught / type_mut : 0);
        check(base_ok >= 100, "entropy metric: generator yields valid base programs");
        check(mutants[MC_REF] > 0 && caught[MC_REF] == mutants[MC_REF],
              "entropy metric: positive control — every `mut_ref`→`ref` write is detected");
        // ── ★★ **선언 축** 변이 지표 (2026-07-12 감사가 늘린 검사들) ──────────────
        //   기존 지표는 **타입**을 변이시킨다(u8→u32, mut_ref→ref …).
        //   이번 세션이 늘린 것은 **선언**이다 — 계약 절·구조체·이름.
        //   *선언을 한 번 편집했을 때 컴파일러가 그것을 잡는가?*
        //   이것이 곧 "검사되지 않는 중복은 거짓말로 썩는다" 의 **측정치**다.
        //
        // ★ 그리고 **정적/런타임을 가른다.** 어떤 거짓말은 컴파일타임에 잡히고, 어떤 것은
        //   그 경로를 **밟아야** 잡힌다(`errors … when` 위반 · `ensures` 위반). 그 구분을
        //   섞으면 지표가 거짓말을 한다 — 도구가 검사한 것만 주장해야 하듯이.
        {
            const char *base =
                "module m . type bytes slice u8 . . enum e do small . end "
                "struct p do x u8 . y u8 . end "
                "fn helper input a u8 . output u8 .  do return a . end "
                "fn f input d bytes . output result u8 e . .  "
                "  errors small lt len d . 4 . . tests helper . "
                " do guard ge len d . 4 . else return error small . . . "
                "   return ok index d 0 . . end "
                "fn g input a range u8 0 100 . output u8 .  "
                "  ensures le ret 200 . do return mul a 2 . end "
                "fn h output p .  do return make p do x 1 . y 2 . end . end";
            struct { const char *name; const char *from; const char *to; bool at_runtime; } muts[] = {
                { "errors variant renamed",     "errors small lt",    "errors ghost lt",       false },
                { "tests names a ghost op",     "tests helper .",     "tests ghost .",          false },
                { "range widened past its type","range u8 0 100",     "range u8 0 300",         false },
                { "make: a field dropped",      "do x 1 . y 2 . end", "do x 1 . end",           false },
                { "make: a ghost field added",  "do x 1 . y 2 . end", "do x 1 . y 2 . z 3 . end", false },
                { "type name unbound",          "input d bytes",      "input d nosuch",         false },
                { "effect word typo'd",         "effects none . do return a", "effects nne . do return a", false },
                { "`try` on a non-result",      "return ok index d 0", "return ok try index d 0 . ", false },
                // ★ 이 둘은 **런타임 계약**이다 — 그 경로를 밟아야 드러난다. 정적 지표가 못 잡는 것이
                //   정상이고, 그것을 **거짓말이라고 부르지 않는다.** 아래에서 실행으로 확인한다.
                { "errors condition flipped",   "errors small lt len d . 4", "errors small ge len d . 4", true  },
                { "ensures bound tightened",    "ensures le ret 200", "ensures le ret 100",     true  },
            };
            int n = (int)(sizeof muts / sizeof muts[0]);
            int nstatic = 0, det = 0, nrt = 0;
            char buf[3072];
            check(MPCK(base), "declaration metric: the base program is valid (every clause is TRUE)");
            for (int q = 0; q < n; q++) {
                const char *at = strstr(base, muts[q].from);
                if (!at) continue;
                size_t pre = (size_t)(at - base);
                snprintf(buf, sizeof buf, "%.*s%s%s", (int)pre, base, muts[q].to,
                         at + strlen(muts[q].from));
                if (muts[q].at_runtime) { nrt++; continue; }
                nstatic++;
                if (!MPCK(buf)) det++;             // 거부됨 → **정적으로 잡혔다**
                else printf("      ! NOT detected (static): %s\n", muts[q].name);
            }
            printf("      %-30s  %4d mutants  %3d%% detected   ← the 2026-07-12 audit\n",
                   "declaration (static)", nstatic, nstatic ? 100 * det / nstatic : 0);
            check(det == nstatic,
                  "★★ declaration metric: EVERY one-edit lie that CAN be caught statically IS "
                  "caught (a variant renamed, a `tests` target that does not exist, a `range` past "
                  "its type, a `make` field dropped or invented, an unbound type name, a typo'd "
                  "effect, a `try` on a non-result). Before this session, MOST of these passed "
                  "silently");
            printf("      %-30s  %4d mutants  (caught at the error/exit site, not at compile time)\n",
                   "declaration (runtime contract)", nrt);
            check(nrt == 2,
                  "declaration metric: two mutations are RUNTIME contracts by design (`errors … "
                  "when` and `ensures`) — the metric says so instead of calling them a gap");
        }

        check(caught[MC_LIT] == 0,
              "entropy metric: negative control — literal edits are NOT statically detectable");
        // The TYPE classes are the measurement RFC-0052 exists to move. No assert on the
        // rate yet: it is the *baseline*. Adopting RFC-0052 §9 S1 should drive it to ~100%.
        check(type_mut > 0, "entropy metric: type-mutation classes are measured");
        #undef MPCK
    }

    // ── V3-lite: exhaustive bounded model check of 정리 A ──
    {
        mc_ev_t ev[MC_N];
        proven_u8 muts0[MC_TAGS] = { 0 };
        mc_total = mc_green = mc_viol = mc_conserv = 0;
        mc_enum(ev, 0, 0, muts0);
        printf("  (model check: %lld sequences ≤ %d events — %lld static-green, %lld violations, %lld conservative-rejects)\n",
               mc_total, MC_N, mc_green, mc_viol, mc_conserv);
        check(mc_viol == 0, "V3-lite: theorem A holds on ALL bounded event sequences (static-green ⇒ dyn-clean)");
        check(mc_green > 0 && mc_conserv > 0, "V3-lite: space is non-trivial (greens and conservative rejects both present)");

        // Lemma B: every prefix(≤2) + loop-body(1..3) combination, unrolled k=1..3
        mc2_total = mc2_green = mc2_viol = 0;
        mc_ev_t ev2[MC2_P + MC2_B];
        proven_u8 muts2[MC_TAGS] = { 0 };
        for (int np = 0; np <= MC2_P; np++)
            for (int nb = 1; nb <= MC2_B; nb++)
                mc2_run(ev2, 0, np, nb, 0, muts2);
        printf("  (loop model check: %lld prefix+body shapes — %lld S2L-green, %lld violations over k=1..3 unrollings)\n",
               mc2_total, mc2_green, mc2_viol);
        check(mc2_viol == 0, "V3-lite: lemma B holds — loop-span statics cover every bounded wrap-around");
        check(mc2_green > 0, "V3-lite: loop space is non-trivial");

        // Path sensitivity (D4): prefix + two exclusive arms; BOTH executions must be clean
        mc3_total = mc3_green = mc3_viol = 0;
        for (int np = 0; np <= MC3_MAX; np++)
            for (int na = 0; na <= MC3_MAX; na++)
                for (int nb = 0; nb <= MC3_MAX; nb++) {
                    int n = np + na + nb;
                    if (n == 0) continue;
                    int arms[3 * MC3_MAX];
                    for (int i = 0; i < np; i++) arms[i] = -1;
                    for (int i = 0; i < na; i++) arms[np + i] = 0;
                    for (int i = 0; i < nb; i++) arms[np + na + i] = 1;
                    mc_ev_t ev3[3 * MC3_MAX];
                    int arm_of[3 * MC3_MAX];
                    proven_u8 muts3[MC_TAGS] = { 0 };
                    mc3_enum(ev3, arm_of, 0, n, arms, 0, muts3);
                }
        printf("  (path model check: %lld if-shapes — %lld path-green, %lld violations across both arms)\n",
               mc3_total, mc3_green, mc3_viol);
        check(mc3_viol == 0, "V3-lite: path-sensitive rules stay SOUND on every bounded if-shape (D4 fix verified)");
        check(mc3_green > 0, "V3-lite: path-sensitive space is non-trivial");
    }

    // ══ SMT 백엔드 — **반박기 자체를 시험한다** (REQ-0004 · 후속 M, 2026-08-05) ══════
    //
    //   ★ 왜 유닛인가: 이 절차의 결함은 *"증명이 안 된다"*(느려질 뿐)가 아니라
    //     *"안 되는 것을 증명한다"*(메모리 안전 구멍)로 나타난다. 그 방향을 재는 시험이
    //     픽스처보다 여기서 훨씬 싸고 정확하다 — **반례가 있는 계를 넣고 안 넘어가는지** 본다.
    {
        low_smt_sys_t s; proven_i64 a[LOW_SMT_MAXV], lam[LOW_SMT_MAXC];
        // x0 = i, x1 = n, x2 = len(s)
        low_smt_init(&s, 3);
        a[0]=1; a[1]=-1; a[2]=0;  low_smt_add(&s, a, 1);    // i < n
        a[0]=0; a[1]=1;  a[2]=-1; low_smt_add(&s, a, 0);    // n ≤ len
        a[0]=-1;a[1]=0;  a[2]=0;  low_smt_add(&s, a, 0);    // 0 ≤ i
        a[0]=-1;a[1]=0;  a[2]=1;  low_smt_add(&s, a, 0);    // ¬(i < len)  ← 목표의 부정
        bool ok = low_smt_refute(&s, lam);
        check(ok, "smt: i<n ∧ len≥n ∧ i≥0 ⟹ i<len (두 홉 사슬을 반박으로 넘는다)");
        check(!ok || low_smt_check(&s, lam), "smt: 낸 Farkas 계수로 모순이 **실제로 재현된다**");

        // ★★ 반례가 있는 계 — **넘어가면 안 된다**. i = n = len 이 그 반례다.
        low_smt_sys_t u; proven_i64 lu[LOW_SMT_MAXC];
        low_smt_init(&u, 3);
        a[0]=1; a[1]=-1; a[2]=0;  low_smt_add(&u, a, 0);    // i ≤ n  (비엄격!)
        a[0]=0; a[1]=1;  a[2]=-1; low_smt_add(&u, a, 0);
        a[0]=-1;a[1]=0;  a[2]=0;  low_smt_add(&u, a, 0);
        a[0]=-1;a[1]=0;  a[2]=1;  low_smt_add(&u, a, 0);
        check(!low_smt_refute(&u, lu), "smt: i≤n 뿐이면 **증명하지 않는다** (i=n=len 이 반례)");

        // ★★★ 위조 계수는 거절돼야 한다 — 검산이 없으면 소거의 버그가 곧 증명서가 된다.
        proven_i64 fake[LOW_SMT_MAXC] = {1,1,1,1,0,0,0,0,0,0};
        check(!low_smt_check(&u, fake), "smt: 아무 계수나 넣으면 검산이 거절한다");
        proven_i64 neg[LOW_SMT_MAXC] = {0};
        neg[0] = -1; neg[3] = 1;
        check(!low_smt_check(&s, neg), "smt: **음수** 계수는 거절한다 (부등호가 뒤집힌다)");
    }

    free(amem);
    printf("\n%s (%d failures)\n", g_fail ? "FAILED" : "ALL PASS", g_fail);
    return g_fail ? 1 : 0;
}
