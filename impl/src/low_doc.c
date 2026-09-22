// low_doc.c — lowdoc prototype (RFC-0033 D12). Card fields → Markdown, deterministic
// section order. Reuses the shared front-end CST; no new parser.
//
//   (1) S4-aware:  requires/ensures grade badges [static|debug|assume]; calc = pure.
//   (2) output:    stdout, or <dir>/<module>.md + <dir>/llms.txt (--doc-out).
//   (3) prose:     a `lowdoc` clause, else adjacent `rem` comments rescanned from source.
#include "low_doc.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <sys/stat.h>

#include "low_token.h"
#include "low_diag.h"
#include "low_check.h"
#include "low_typecheck.h"
#include "low_contract.h"
#include "low_region.h"
#include "proven/array.h"

// one S4 diagnostic, attributed to an op by line
typedef struct { const char *code; const char *msg; proven_u32 line; } sd_t;
static void sd_collect(proven_array_t *da, sd_t *out, proven_size_t *n, proven_size_t cap) {
    for (proven_size_t i = 0; i < da->len && *n < cap; i++) {
        const low_diag_t *d = PROVEN_ARRAY_GET(da, low_diag_t, i);
        out[*n].code = d->code; out[*n].msg = low_diag_text(d); out[(*n)++].line = d->line;
    }
}
static proven_u32 form_first_line(const low_cst_t *f) {
    if (f->kind == LOW_CST_FORM && f->nkids > 0) return f->kids[0]->tok.line;
    return f->tok.line;
}

static bool dv(proven_u8str_view_t v, const char *s) { return proven_u8str_view_eq(v, proven_u8str_view_from_cstr(s)); }
static void ev(FILE *o, proven_u8str_view_t v) { if (v.size) fwrite(v.ptr, 1, v.size, o); }

// the flat MVP clause vocabulary (+ the lowdoc prose clause) = clause boundaries
static bool is_clause(proven_u8str_view_t v) {
    return dv(v,"input")||dv(v,"output")||dv(v,"effects")||dv(v,"requires")||dv(v,"ensures")||
           dv(v,"errors")||dv(v,"access")||dv(v,"inplace")||dv(v,"invalidates")||dv(v,"tests")||dv(v,"caps")||dv(v,"allocation")||
           dv(v,"unsafe")||dv(v,"lowdoc");
}
static bool is_grade(proven_u8str_view_t v) { return dv(v,"static")||dv(v,"debug")||dv(v,"assume"); }
static bool clause_here(const low_cst_t *f, proven_size_t j, const char *kw) {
    return f->kids[j]->kind == LOW_CST_ATOM && dv(f->kids[j]->tok.lex, kw);
}
// render a value node inline (atom text, or a parenthesised group/form for `(len data)`)
static void emit_node(FILE *o, const low_cst_t *nd);
static void emit_seq(FILE *o, const low_cst_t *nd) {  // operands of a form (kids[0] IS the head), or a bare atom
    if (nd->kind == LOW_CST_ATOM) { ev(o, nd->tok.lex); return; }
    if (nd->kind == LOW_CST_FORM) { for (proven_size_t k = 0; k < nd->nkids; k++) { if (k) fputc(' ', o); emit_node(o, nd->kids[k]); } return; }
    emit_node(o, nd);
}
static void emit_node(FILE *o, const low_cst_t *nd) {
    if (nd->kind == LOW_CST_ATOM) { ev(o, nd->tok.lex); return; }
    if (nd->kind == LOW_CST_GROUP) { fputc('(', o); if (nd->nkids) emit_seq(o, nd->kids[0]); fputc(')', o); return; }
    if (nd->kind == LOW_CST_FORM)  { fputc('(', o); emit_seq(o, nd); fputc(')', o); }
}
static proven_size_t run_end(const low_cst_t *f, proven_size_t s) {  // next clause-kw atom / block
    proven_size_t j = s;
    while (j < f->nkids && f->kids[j]->kind != LOW_CST_BLOCK &&
           !(f->kids[j]->kind == LOW_CST_ATOM && is_clause(f->kids[j]->tok.lex))) j++;
    return j;
}
static void emit_run(FILE *o, const low_cst_t *f, proven_size_t s, proven_size_t e) {
    for (proven_size_t k = s; k < e; k++) { if (k > s) fputc(' ', o); emit_node(o, f->kids[k]); }
}

// ── prose sources ─────────────────────────────────────────────────────────────
// a `lowdoc "…" | text …` clause value view (empty if none)
static proven_u8str_view_t lowdoc_clause(const low_cst_t *f) {
    for (proven_size_t j = 2; j + 1 < f->nkids; j++)
        if (clause_here(f, j, "lowdoc")) return f->kids[j + 1]->tok.lex;
    return (proven_u8str_view_t){ 0 };
}
static proven_u8str_view_t trim_lead(proven_u8str_view_t v) {
    proven_size_t i = 0; while (i < v.size && (v.ptr[i] == ' ' || v.ptr[i] == '\t')) i++;
    return (proven_u8str_view_t){ v.ptr + i, v.size - i };
}
// source line `n` (1-based, no newline)
static proven_u8str_view_t line_at(proven_u8str_view_t src, proven_u32 n) {
    proven_u32 cur = 1; proven_size_t start = 0;
    for (proven_size_t i = 0; i <= src.size; i++) {
        if (i == src.size || src.ptr[i] == '\n') {
            if (cur == n) { proven_size_t e = i; if (e > start && src.ptr[e-1] == '\r') e--; return (proven_u8str_view_t){ src.ptr + start, e - start }; }
            cur++; start = i + 1;
        }
    }
    return (proven_u8str_view_t){ 0 };
}
// a `rem …` comment line → its content view (after "rem "); ok=false if not a rem line
static proven_u8str_view_t rem_content(proven_u8str_view_t line, bool *ok) {
    proven_u8str_view_t t = trim_lead(line);
    if (t.size >= 3 && t.ptr[0]=='r' && t.ptr[1]=='e' && t.ptr[2]=='m' && (t.size == 3 || t.ptr[3]==' ')) {
        *ok = true; proven_size_t o = (t.size > 3) ? 4 : 3; return (proven_u8str_view_t){ t.ptr + o, t.size - o };
    }
    *ok = false; return (proven_u8str_view_t){ 0 };
}
// adjacent `rem` lines above the op head, in source order; returns count
static proven_size_t collect_rem(const low_cst_t *f, proven_u8str_view_t src, proven_u8str_view_t *lines, proven_size_t cap) {
    proven_u32 head = f->kids[0]->tok.line;
    if (head < 2 || src.size == 0) return 0;
    proven_size_t n = 0;
    for (proven_u32 ln = head - 1; ln >= 1 && n < cap; ln--) {
        bool ok = false; proven_u8str_view_t c = rem_content(line_at(src, ln), &ok);
        if (!ok) break;
        lines[n++] = c;                              // bottom-up
    }
    for (proven_size_t i = 0; i < n / 2; i++) { proven_u8str_view_t t = lines[i]; lines[i] = lines[n-1-i]; lines[n-1-i] = t; }  // → source order
    return n;
}
// (content hash is FNV-1a, inlined in body_present — stand-in for the RFC-0012 hash)

// ── effect inference (declared vs actual) ─────────────────────────────────────
typedef struct { proven_u8str_view_t name, eff[8]; proven_size_t neff; } opeff_t;
static opeff_t g_ops[128]; static proven_size_t g_nops;
// ★ 정규화된 요약을 부르려면 **단위 전체**가 필요하다(효과가 호출을 타고 전파되므로).
static const low_parse_result_t *g_pr;

static bool is_op(const low_cst_t *f);  // fwd (defined below)
static void build_opeff(const low_parse_result_t *pr) {
    g_nops = 0;
    for (proven_size_t i = 0; i < pr->nforms && g_nops < 128; i++) {
        const low_cst_t *f = pr->forms[i];
        if (!is_op(f)) continue;
        opeff_t *e = &g_ops[g_nops++];
        e->name = f->kids[1]->tok.lex; e->neff = 0;
        for (proven_size_t j = 2; j < f->nkids; j++) {
            if (!(f->kids[j]->kind == LOW_CST_ATOM && dv(f->kids[j]->tok.lex, "effects"))) continue;
            for (proven_size_t k = j + 1; k < f->nkids && f->kids[k]->kind == LOW_CST_ATOM &&
                 !is_clause(f->kids[k]->tok.lex) && e->neff < 8; k++)
                if (!dv(f->kids[k]->tok.lex, "none")) e->eff[e->neff++] = f->kids[k]->tok.lex;
            break;
        }
    }
}
static bool walk_mentions(const low_cst_t *nd, const char *name) {
    if (!nd) return false;
    if (nd->kind == LOW_CST_ATOM)
        return proven_u8str_view_eq(nd->tok.lex, proven_u8str_view_from_cstr(name));
    for (proven_size_t i = 0; i < nd->nkids; i++)
        if (walk_mentions(nd->kids[i], name)) return true;
    return false;
}
// ★ 옛 walk_infer 는 **지웠다.** 두 번째 추론 경로가 존재하는 한, 그것은 언젠가 갈린다.
//   (실제로 갈렸다: 이 파일은 `print` 둘만 알았다.)

// ── per-op render ─────────────────────────────────────────────────────────────
static void render_prose(FILE *o, const low_cst_t *f, proven_u8str_view_t src, proven_u8str_view_t *out_summary) {
    proven_u8str_view_t cl = lowdoc_clause(f);          // (3) clause takes priority (D12: 절 > 인접)
    if (cl.size) {
        proven_size_t nl = 0; while (nl < cl.size && cl.ptr[nl] != '\n') nl++;
        fputc('\n', o); fwrite(cl.ptr, 1, nl, o); fputc('\n', o);
        if (out_summary) *out_summary = (proven_u8str_view_t){ cl.ptr, nl };
        if (nl < cl.size) { fputc('\n', o); fwrite(cl.ptr + nl + 1, 1, cl.size - nl - 1, o); fputc('\n', o); }
        return;
    }
    // (3) rescan adjacent `rem` lines directly above the op head (source order)
    proven_u8str_view_t lines[64]; proven_size_t n = collect_rem(f, src, lines, 64);
    if (n == 0) return;
    fputc('\n', o); ev(o, lines[0]); fputc('\n', o);           // first line = summary
    if (out_summary) *out_summary = lines[0];
    if (n > 1) { fputc('\n', o); for (proven_size_t k = 1; k < n; k++) { ev(o, lines[k]); fputc('\n', o); } }
}
// description body only (matches body_present's hashed bytes); for details externalization
static void emit_body(FILE *o, const low_cst_t *f, proven_u8str_view_t src) {
    proven_u8str_view_t cl = lowdoc_clause(f);
    if (cl.size) {
        proven_size_t nl = 0;
        while (nl < cl.size && cl.ptr[nl] != '\n') nl++;
        if (nl + 1 < cl.size) fwrite(cl.ptr + nl + 1, 1, cl.size - nl - 1, o);
        return;
    }
    proven_u8str_view_t lines[64]; proven_size_t n = collect_rem(f, src, lines, 64);
    for (proven_size_t k = 1; k < n; k++) { fwrite(lines[k].ptr, 1, lines[k].size, o); fputc('\n', o); }
}
static bool body_present(const low_cst_t *f, proven_u8str_view_t src, unsigned *hash) {
    unsigned h = 2166136261u; bool any = false;
    proven_u8str_view_t cl = lowdoc_clause(f);
    if (cl.size) { proven_size_t nl = 0; while (nl < cl.size && cl.ptr[nl] != '\n') nl++;
        for (proven_size_t i = nl + 1; i < cl.size; i++) { h ^= cl.ptr[i]; h *= 16777619u; any = true; } }
    else { proven_u8str_view_t lines[64]; proven_size_t n = collect_rem(f, src, lines, 64);
        for (proven_size_t k = 1; k < n; k++) { for (proven_size_t i = 0; i < lines[k].size; i++) { h ^= lines[k].ptr[i]; h *= 16777619u; } h ^= '\n'; h *= 16777619u; any = true; } }
    *hash = h; return any;
}

static void render_contract_kind(FILE *o, const low_cst_t *f, const char *kw, const char *label, bool *hdr) {
    for (proven_size_t j = 2; j < f->nkids; j++) {
        if (!clause_here(f, j, kw)) continue;
        if (!*hdr) { fputs("\n### Contract\n\n", o); *hdr = true; }
        proven_size_t e = run_end(f, j + 1), vs = j + 1;
        fprintf(o, "- **%s** ", label);
        if (vs < e && f->kids[vs]->kind == LOW_CST_ATOM && is_grade(f->kids[vs]->tok.lex)) {  // (1) grade badge
            fputs("[", o); ev(o, f->kids[vs]->tok.lex); fputs("] ", o); vs++;
        }
        fputc('`', o); emit_run(o, f, vs, e); fputs("`\n", o);
        if (strcmp(kw, "errors") == 0) return;  // errors: single line
    }
}

static void render_op(FILE *o, const low_cst_t *f, proven_u8str_view_t src,
                      const sd_t *ds, proven_size_t nds, proven_u32 lo, proven_u32 hi) {
    proven_u8str_view_t name = f->kids[1]->tok.lex;
    bool calc = f->kids[0]->tok.kw == LOW_KW_FN;

    fputs("## ", o); ev(o, name); fputc('\n', o);
    fprintf(o, "\n`%s op`%s\n", calc ? "calc" : "proc", calc ? " · pure" : "");   // (1) calc = pure

    render_prose(o, f, src, NULL);

    bool sig = false;                                                             // Signature
    for (proven_size_t j = 2; j < f->nkids; j++) {
        if (!clause_here(f, j, "input")) continue;
        if (!sig) { fputs("\n### Signature\n\n", o); sig = true; }
        proven_size_t e = run_end(f, j + 1);
        fputs("- `", o); if (j + 1 < e) ev(o, f->kids[j + 1]->tok.lex);
        fputs("` : `", o); emit_run(o, f, j + 2, e); fputs("`\n", o);
    }
    for (proven_size_t j = 2; j < f->nkids; j++) {
        if (!clause_here(f, j, "output")) continue;
        if (!sig) { fputs("\n### Signature\n\n", o); sig = true; }
        fputs("- **returns** `", o); emit_run(o, f, j + 1, run_end(f, j + 1)); fputs("`\n", o);
        break;
    }

    bool con = false;                                                            // Contract
    render_contract_kind(o, f, "requires", "requires", &con);
    render_contract_kind(o, f, "ensures", "ensures", &con);
    render_contract_kind(o, f, "errors", "errors", &con);

    // ★★ `access` — **호출자의 버퍼에 무슨 짓을 하는가.** 이게 문서에 없었다.
    //   문서는 requires·ensures·effects 를 다 말하면서, "이 op 이 당신이 넘긴 슬라이스를
    //   **덮어쓴다**" 는 사실은 **한마디도 하지 않았다.** 그건 계약의 일부다 — 아니, 계약 중에서도
    //   호출자가 **가장 먼저** 알아야 하는 쪽이다. 검사기는 그것을 강제하고 있었는데(E-ACCESS-MODE)
    //   문서는 침묵했다: **하나의 뜻이 한 곳에서만 보였다**(교훈 7).
    render_contract_kind(o, f, "access", "access", &con);

    {   // RFC-0053 E5 / P2′ — **오차 가시**. 수치 오차는 숨은 비용이다: 본문이 쓰는 op 의
        // 오차 특성을 문서가 *반드시* 드러낸다. 이름만으로 구별되는 계열이라 더더욱 그렇다.
        static const struct { const char *op; const char *bound; } ERRB[] = {
            { "sum_neumaier", "|result - Σxᵢ| ≤ 2ε·|Σ|xᵢ||   (Kahan-Babuška-Neumaier — **항의 개수에 무관**)" },
            { "sum_seq",      "|result - Σxᵢ| ≤ n·ε·Σ|xᵢ|    (축차합 — 오차가 항의 개수에 **비례**한다)" },
            { "fma",       "단일 반올림(융합) — 명시 호출 시에만. fp-contraction 은 기본 off" },
        };
        const low_cst_t *b0 = (f->nkids && f->kids[f->nkids-1]->kind == LOW_CST_BLOCK) ? f->kids[f->nkids-1] : NULL;
        bool hdr = false;
        for (proven_size_t e = 0; e < sizeof ERRB / sizeof ERRB[0]; e++)
            if (b0 && walk_mentions(b0, ERRB[e].op)) {
                if (!hdr) { fputs("\n### Numeric error\n\n", o); hdr = true; }
                fprintf(o, "- `%s`: %s\n", ERRB[e].op, ERRB[e].bound);
            }
    }

    {   // ★★★ Effects — **정규화된 요약 하나만** 쓴다 (RFC-0057, DECISION-0009).
        //   전에는 이 파일이 **자기 추론**을 가지고 있었고, 아는 낱말이 `print` 둘뿐이었다.
        //   그래서 **검사기가 E-EFFECT 로 거절하는 op 을 문서는 "효과 없음" 이라 적었다** —
        //   **문서가 순수하다고 거짓말했다.** 경로가 갈리면 그중 하나는 반드시 썩는다.
        //   ⇒ 이제 `low_op_summary()` 를 부른다. **검사기와 같은 함수, 같은 답.**
        low_op_summary_t sum = low_op_summary(g_pr, f);
        static const unsigned BITS[] = { 1u, 2u, 4u, 8u, 16u };   // io alloc state panic unsafe
        fputs("\n### Effects\n\n- declared: `", o);
        {
            bool any = false;
            for (proven_size_t k = 0; k < sizeof BITS / sizeof BITS[0]; k++)
                if (sum.declared & BITS[k]) {
                    if (any) fputc(' ', o);
                    fputs(low_effect_bit_name(BITS[k]), o); any = true;
                }
            if (!any) fputs("none", o);
        }
        fputs("`\n- inferred: `", o);
        {
            bool any = false;
            for (proven_size_t k = 0; k < sizeof BITS / sizeof BITS[0]; k++)
                if (sum.actual & BITS[k]) {
                    if (any) fputc(' ', o);
                    fputs(low_effect_bit_name(BITS[k]), o); any = true;
                }
            if (!any) fputs("none", o);
        }
        fputs("`\n", o);
        for (proven_size_t k = 0; k < sizeof BITS / sizeof BITS[0]; k++)
            if ((sum.actual & BITS[k]) && !(sum.declared & BITS[k])) {
                fputs("- ⚠ inferred effect `", o);
                fputs(low_effect_bit_name(BITS[k]), o);
                fputs("` is not in the declared set\n", o);
            }
        // ★ 관측적 순수성도 **같은 요약**에서 나온다 — 문서가 그것을 숨기지 않는다.
        if (sum.is_calc && sum.writes_caller)
            fputs("- ⚠ this `fn` WRITES through a `mut` parameter — that write is visible to "
                  "the caller, so it is **not pure** (RFC-0057)\n", o);
    }
    for (proven_size_t j = 2; j < f->nkids; j++) {                               // Examples
        if (!clause_here(f, j, "tests")) continue;
        fputs("\n### Examples\n\n- tests: `", o); emit_run(o, f, j + 1, run_end(f, j + 1)); fputs("`\n", o);
        break;
    }

    // Checks — injected S4 verdict (effect · type · contract · region)
    bool bad = false;
    for (proven_size_t k = 0; k < nds; k++) {
        if (ds[k].line < lo || ds[k].line >= hi) continue;
        if (!bad) { fputs("\n### Checks\n\n", o); bad = true; }
        fprintf(o, "- ⚠ `%s` — %s (line %u)\n", ds[k].code, ds[k].msg, ds[k].line);
    }
    if (!bad) fputs("\n### Checks\n\n- ✓ effect · type · contract · region checks pass\n", o);
    fputc('\n', o);
}

// ── module + index ────────────────────────────────────────────────────────────
static proven_u8str_view_t module_name(const low_parse_result_t *pr) {
    for (proven_size_t i = 0; i < pr->nforms; i++) {
        const low_cst_t *f = pr->forms[i];
        if (f->kind == LOW_CST_FORM && f->nkids >= 2 && f->kids[0]->kind == LOW_CST_ATOM &&
            f->kids[0]->tok.kw == LOW_KW_MODULE) return f->kids[1]->tok.lex;
    }
    return proven_u8str_view_from_cstr("module");
}
static bool is_op(const low_cst_t *f) {
    return f->kind == LOW_CST_FORM && f->nkids >= 2 && f->kids[0]->kind == LOW_CST_ATOM &&
           (f->kids[0]->tok.kw == LOW_KW_FN || f->kids[0]->tok.kw == LOW_KW_PROC);
}
static int render_module(FILE *o, const low_parse_result_t *pr, proven_u8str_view_t src, proven_allocator_t work) {
    build_opeff(pr);  // op → declared effects, for inference
    // run the S4 passes once; attribute each diagnostic to its op by line range
    low_check_result_t     cr = low_check(work, pr);
    low_typecheck_result_t tr = low_typecheck(work, pr);
    low_contract_result_t  kr = low_contract(work, pr);
    low_region_result_t    rr = low_region(work, pr);
    sd_t ds[256]; proven_size_t nds = 0;
    sd_collect(&cr.diags, ds, &nds, 256); sd_collect(&tr.diags, ds, &nds, 256);
    sd_collect(&kr.diags, ds, &nds, 256); sd_collect(&rr.diags, ds, &nds, 256);

    fputs("# ", o); ev(o, module_name(pr)); fputs("\n\n", o);
    int n = 0;
    for (proven_size_t i = 0; i < pr->nforms; i++) {
        if (!is_op(pr->forms[i])) continue;
        proven_u32 lo = form_first_line(pr->forms[i]);
        proven_u32 hi = (i + 1 < pr->nforms) ? form_first_line(pr->forms[i + 1]) : 0xFFFFFFFFu;
        render_op(o, pr->forms[i], src, ds, nds, lo, hi);
        n++;
    }
    proven_array_destroy(&cr.diags); proven_array_destroy(&tr.diags);
    proven_array_destroy(&kr.diags); proven_array_destroy(&rr.diags);
    return n;
}
// llms.txt — curated root index (RFC-0014 §6.6): links + one-line summaries
static void render_llms(FILE *o, const low_parse_result_t *pr, proven_u8str_view_t src) {
    proven_u8str_view_t mod = module_name(pr);
    fputs("# ", o); ev(o, mod); fputs("\n\n> Lowent module API index (generated by lowdoc).\n\n## Ops\n\n", o);
    for (proven_size_t i = 0; i < pr->nforms; i++) {
        const low_cst_t *f = pr->forms[i];
        if (!is_op(f)) continue;
        proven_u8str_view_t name = f->kids[1]->tok.lex, sum = { 0 };
        // reuse the summary extraction (render to a throwaway sink)
        FILE *dev = fopen("/dev/null", "w");
        if (dev) { render_prose(dev, f, src, &sum); fclose(dev); }
        fputs("- [", o); ev(o, name); fputs("](", o); ev(o, mod); fputs(".md#", o); ev(o, name); fputs(")", o);
        if (sum.size) { fputs(" — ", o); ev(o, sum); }
        fputc('\n', o);
    }
}

// ── .lowctx card (terse; RFC-0014 §6.2.2) — details body is externalised by hash ──
static void card_clause_line(FILE *o, const low_cst_t *f, const char *kw) {
    for (proven_size_t j = 2; j < f->nkids; j++)
        if (clause_here(f, j, kw)) { fprintf(o, "%s ", kw); emit_run(o, f, j + 1, run_end(f, j + 1)); fputc('\n', o); }
}
static void render_card(FILE *o, const low_parse_result_t *pr, proven_u8str_view_t src) {
    for (proven_size_t i = 0; i < pr->nforms; i++) {
        const low_cst_t *f = pr->forms[i];
        if (!is_op(f)) continue;
        fputs("op ", o); ev(o, f->kids[1]->tok.lex); fputc('\n', o);
        fprintf(o, "kind %s\n", f->kids[0]->tok.kw == LOW_KW_FN ? "calc" : "proc");
        proven_u8str_view_t sum = { 0 }; FILE *dev = fopen("/dev/null", "w");
        if (dev) { render_prose(dev, f, src, &sum); fclose(dev); }
        if (sum.size) { fputs("summary ", o); ev(o, sum); fputc('\n', o); }
        unsigned bh; if (body_present(f, src, &bh)) fprintf(o, "details h:%08x\n", bh);  // externalised
        card_clause_line(o, f, "input");   card_clause_line(o, f, "output");
        card_clause_line(o, f, "effects"); card_clause_line(o, f, "requires");
        card_clause_line(o, f, "ensures"); card_clause_line(o, f, "errors");
        card_clause_line(o, f, "tests");
        fputc('\n', o);
    }
}

int low_doc(const low_parse_result_t *pr, proven_u8str_view_t src, proven_allocator_t work) {
    g_pr = pr;
    return render_module(stdout, pr, src, work);
}
int low_doc_write(const low_parse_result_t *pr, proven_u8str_view_t src, proven_allocator_t work, const char *dir) {
    g_pr = pr;
    (void)mkdir(dir, 0755);   // ignore EEXIST
    proven_u8str_view_t mod = module_name(pr);
    char path[1024];
    snprintf(path, sizeof path, "%s/%.*s.md", dir, (int)mod.size, (const char *)mod.ptr);
    FILE *md = fopen(path, "w"); if (!md) return -1;
    int n = render_module(md, pr, src, work); fclose(md);

    // (1) terse card + externalised details bodies (content-hash keyed)
    snprintf(path, sizeof path, "%s/%.*s.lowctx", dir, (int)mod.size, (const char *)mod.ptr);
    FILE *card = fopen(path, "w"); if (card) { render_card(card, pr, src); fclose(card); }
    snprintf(path, sizeof path, "%s/details", dir); (void)mkdir(path, 0755);
    for (proven_size_t i = 0; i < pr->nforms; i++) {
        const low_cst_t *f = pr->forms[i]; unsigned bh;
        if (!is_op(f) || !body_present(f, src, &bh)) continue;
        snprintf(path, sizeof path, "%s/details/h-%08x.md", dir, bh);
        FILE *b = fopen(path, "w"); if (b) { emit_body(b, f, src); fclose(b); }
    }

    snprintf(path, sizeof path, "%s/llms.txt", dir);            // (curated index)
    FILE *idx = fopen(path, "w"); if (!idx) return -1;
    render_llms(idx, pr, src); fclose(idx);

    snprintf(path, sizeof path, "%s/llms-full.txt", dir);       // (3) full-body bundle
    FILE *full = fopen(path, "w"); if (full) { render_module(full, pr, src, work); fclose(full); }
    return n;
}
