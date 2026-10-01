// low_cst.c — L1 point-closure CST parser (middle-ground reader).
//
// Generic forms are pure point-closure (operands until a '.'/','; nesting via parens
// or a headed do…end block). Four reserved block-heads (op/if/for/loop) need schema
// awareness because their do…end body belongs to the *statement*, not to the operand
// immediately before `do` (cf. `if cond do…end` — the block is if's, not cond's).
#include "low_cst.h"
#include "low_cst_priv.h"
#include "low_arity.h"

#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* low_parser_t — low_cst_priv.h */


// ── cursor ──
static const low_token_t *low_cur(low_parser_t *p) { return &p->toks[p->pos]; }
 low_tok_kind_t low_curk(low_parser_t *p)    { return p->toks[p->pos].kind; }
static low_kw_t low_curkw(low_parser_t *p)         { return p->toks[p->pos].kw; }
 low_token_t low_adv(low_parser_t *p) {
    low_token_t t = p->toks[p->pos];
    if (t.kind != LOW_TOK_EOF) p->pos++;
    return t;
}

 void low_pdiag(low_parser_t *p, const char *code, const char *msg,
                      proven_u32 line, proven_u32 col) {
    low_diag_t d = { .sev = LOW_SEV_ERROR, .code = code, .msg = msg, .line = line, .col = col };
    (void)proven_array_push(&p->out->diags, &d);
    p->out->ok = false;
}

// ── node allocation (arena) ──
// ★ 2026-09-27 — **노드 아레나가 차면 말없이 `ok = false` 만 남겼다.** 짧은 문장 381 개짜리 op 에서 `--check` 가
//   진단 하나 없이 «violations» 를 냈고, `--run` 은 그 거짓 신호를 무시하고 돌았다(골든 «norm truncation»).
//   찼다는 사실을 이름으로 말한다 — 모르는 이유로 실패하는 것이 가장 나쁜 실패다.
static void low_node_oom(low_parser_t *p) {
    p->out->ok = false;
    if (p->oom_said) return;
    p->oom_said = true;
    const low_token_t *t = &p->toks[p->pos < p->n ? p->pos : (p->n ? p->n - 1 : 0)];
    low_pdiag(p, "E-PARSE-LIMIT",
              "the syntax tree of this unit outgrew the parser's node arena, so the rest of the tree "
              "could not be built. Refusing is the honest answer: split the unit, or raise the arena "
              "size in main.c and say why",
              p->n ? t->line : 0, p->n ? t->col : 0);
}

 low_cst_t *low_node(low_parser_t *p, low_cst_kind_t kind, low_token_t tok) {
    proven_result_mem_mut_t r =
        p->node_alloc.alloc_fn(p->node_alloc.ctx, sizeof(low_cst_t), alignof(low_cst_t));
    if (r.err != PROVEN_OK) { low_node_oom(p); return NULL; }
    low_cst_t *nd = (low_cst_t *)r.value.ptr;
    *nd = (low_cst_t){ .kind = kind, .tok = tok, .line = tok.line, .col = tok.col };
    return nd;
}

// Copy a temp array of low_cst_t* into an arena-backed fixed array; destroys tmp.
 void low_take_kids(low_parser_t *p, low_cst_t *nd, proven_array_t *tmp) {
    nd->nkids = tmp->len;
    nd->kids = NULL;
    if (tmp->len > 0) {
        proven_result_mem_mut_t r = p->node_alloc.alloc_fn(
            p->node_alloc.ctx, sizeof(low_cst_t *) * tmp->len, alignof(low_cst_t *));
        if (r.err != PROVEN_OK) { low_node_oom(p); nd->nkids = 0; }
        else {
            nd->kids = (low_cst_t **)r.value.ptr;
            for (proven_size_t i = 0; i < tmp->len; i++) {
                nd->kids[i] = *(low_cst_t *const *)proven_array_get(tmp, i);
            }
        }
    }
    proven_array_destroy(tmp);
}

static bool low_is_atom_tok(low_tok_kind_t k) {
    return k == LOW_TOK_IDENT || k == LOW_TOK_NUMBER || k == LOW_TOK_STRING ||
           k == LOW_TOK_CHAR ||
           k == LOW_TOK_HEREDOC || k == LOW_TOK_OP;
}
static bool low_is_form_boundary(low_parser_t *p) {
    low_tok_kind_t k = low_curk(p);
    return k == LOW_TOK_EOF || k == LOW_TOK_RPAREN || low_curkw(p) == LOW_KW_END;
}

// forward decls
 low_cst_t *low_parse_form(low_parser_t *p);
static low_cst_t *low_parse_block(low_parser_t *p);
static low_cst_t *low_parse_block_stmt(low_parser_t *p);
static low_cst_t *low_parse_access(low_parser_t *p, bool headed_ok);

// primary = ATOM [do…end → headed block form, if headed_ok] | "(" form ")" | bare do…end
//         | ★ HEAD(.name) operand* [.]        — **머리 표시** (RFC-0061)
static low_cst_t *low_parse_primary(low_parser_t *p, bool headed_ok) {
    low_tok_kind_t k = low_curk(p);

    // ★★★ **`.name` — 머리를 연다.** 그리고 그 자리에서 **진짜 form** 이 선다.
    //
    //   · 피연산자를 계속 읽는다.
    //   · **저자가 찍은 `.`** 을 만나면 **이 머리를 닫는다**(가장 안쪽이 먼저 — 스택 규율).
    //   · **개행**을 만나면 닫지 **않고 돌려준다** — 개행은 **문장**의 닫개이고,
    //     열려 있던 머리들이 **한꺼번에** 닫힌다.
    //
    //   ⇒ `return .add a .mul b 2` ⏎  =  return( add(a, mul(b,2)) )     점 0개
    //   ⇒ `.f a .g b . c`              =  f( a, g(b), c )               점 하나가 g 를 닫는다
    //
    //   ★ 여기서 서는 나무는 **정규화 층이 arity 로 세우는 나무와 글자 그대로 같다**
    //     — 그래서 **def 해시가 같다**(RFC-0012 가 오라클이다). 두 표기, 한 뜻.
    if (k == LOW_TOK_HEAD) {
        low_token_t ht = low_adv(p);
        low_token_t nt = ht; nt.kind = LOW_TOK_IDENT;   // 머리 원자는 **보통 이름처럼** 보여야 한다
        proven_array_t kids = PROVEN_ARRAY_INIT(p->work, low_cst_t *, 4).value;
        (void)PROVEN_ARRAY_PUSH(&kids, low_cst_t *, low_node(p, LOW_CST_ATOM, nt));
        while (!low_is_form_boundary(p) && low_curk(p) != LOW_TOK_DOT &&
               low_curk(p) != LOW_TOK_RPAREN) {
            low_cst_t *o = low_parse_access(p, false);
            if (!o) break;
            (void)PROVEN_ARRAY_PUSH(&kids, low_cst_t *, o);
            if (o->kind == LOW_CST_BLOCK) break;
        }
        low_cst_t *f = low_node(p, LOW_CST_FORM, nt);
        // ★ GROUP 의 토큰은 **HEAD 종류 그대로** 둔다 — 그것이 "**저자가** 머리라고 주장했다" 는
        //   표시다. 정규화가 만든 괄호(아는 머리들)와 **구별해야** 한다: 검사는 **주장에만** 건다.
        low_cst_t *g = low_node(p, LOW_CST_GROUP, ht);
        if (!f || !g) { proven_array_destroy(&kids); return NULL; }
        f->closer = LOW_TOK_EOF;
        if (low_curk(p) == LOW_TOK_DOT) { low_adv(p); }  // ★ 이 점은 **이 머리**의 것이다
        low_take_kids(p, f, &kids);
        // ★★★ **정규화가 만드는 것과 글자 그대로 같은 모양**으로 낸다 — GROUP(FORM), synth.
        //   안 그러면 **한 뜻에 두 모양**이 되고(교훈 7), `low_flat_kids` 의 창이 `.head` 코드를
        //   못 본다. **표기는 둘, 나무는 하나.**
        f->synth = g->synth = true;
        proven_array_t one = PROVEN_ARRAY_INIT(p->work, low_cst_t *, 1).value;
        (void)PROVEN_ARRAY_PUSH(&one, low_cst_t *, f);
        low_take_kids(p, g, &one);
        return g;
    }

    // ★★ RFC-0132 §4 C13 · T2b — **원소 나열 리터럴은 제 점으로 닫는 폼이다**: `lit array u8 4 1 2 3 4 .` ·
    //   `lit slice u8 1 2 3 .` · `lit vec u32 4 1 2 3 4 .`. 그래서 문장 끝이면 `. .`(나열의 점 + 문장의 점),
    //   한가운데면 점 하나로 닫고 이어 간다(`f lit array u8 2 1 2 . x .`). 위의 `.name` 머리와 **같은 기제**다 —
    //   저자가 찍은 점이 가장 안쪽 폼을 닫는다. 구조체 값(`lit point do … end`)은 블록이 닫으므로 여기 오지 않는다.
    // ☞ `vec` 은 사용자 구조체 이름이기도 하다(`lib/vecgen.low` 의 `lit vec t a do … end`). 다음 점 전에 `do` 가
    //   오면 그것은 구조체 값이다 — 나열로 읽지 않는다.
    bool lit_list = false;
    if (low_curkw(p) == LOW_KW_LIT && p->pos + 1 < p->n && p->toks[p->pos + 1].kind == LOW_TOK_IDENT) {
        proven_u8str_view_t w1 = p->toks[p->pos + 1].lex;
        if (proven_u8str_view_eq(w1, PROVEN_LIT("array")) || proven_u8str_view_eq(w1, PROVEN_LIT("slice"))) lit_list = true;
        else if (proven_u8str_view_eq(w1, PROVEN_LIT("vec"))) {
            lit_list = true;
            for (proven_size_t q = p->pos + 2; q < p->n; q++) {
                if (p->toks[q].kind == LOW_TOK_DOT || p->toks[q].kind == LOW_TOK_RPAREN || p->toks[q].kind == LOW_TOK_EOF) break;
                if (p->toks[q].kw == LOW_KW_DO) { lit_list = false; break; }
            }
        }
    }
    if (lit_list) {
        low_token_t lt = low_adv(p);
        proven_array_t kids = PROVEN_ARRAY_INIT(p->work, low_cst_t *, 8).value;
        (void)PROVEN_ARRAY_PUSH(&kids, low_cst_t *, low_node(p, LOW_CST_ATOM, lt));
        while (!low_is_form_boundary(p) && low_curk(p) != LOW_TOK_DOT &&
               low_curk(p) != LOW_TOK_RPAREN) {
            low_cst_t *o = low_parse_access(p, false);
            if (!o) break;
            (void)PROVEN_ARRAY_PUSH(&kids, low_cst_t *, o);
            if (o->kind == LOW_CST_BLOCK) break;
        }
        bool by_block = kids.len && ((low_cst_t **)kids.data)[kids.len - 1]->kind == LOW_CST_BLOCK;
        low_cst_t *f = low_node(p, LOW_CST_FORM, lt);
        low_cst_t *g = low_node(p, LOW_CST_GROUP, lt);
        if (!f || !g) { proven_array_destroy(&kids); return NULL; }
        f->closer = LOW_TOK_EOF;
        // ★ 이 점은 **이 나열**의 것이다 — 단 칸 골라 채우기(`… do 2 5 . end`)는 구조체 값처럼 블록이 닫는다(§13.1)
        if (!by_block && low_curk(p) == LOW_TOK_DOT) { low_adv(p); }
        low_take_kids(p, f, &kids);
        f->synth = g->synth = true;
        proven_array_t one = PROVEN_ARRAY_INIT(p->work, low_cst_t *, 1).value;
        (void)PROVEN_ARRAY_PUSH(&one, low_cst_t *, f);
        low_take_kids(p, g, &one);
        return g;
    }

    if (k == LOW_TOK_LPAREN) {
        low_token_t lp = low_adv(p);
        low_cst_t *inner = low_parse_form(p);
        if (low_curk(p) == LOW_TOK_RPAREN) low_adv(p);
        else low_pdiag(p, "E-GROUP-UNCLOSED", "missing ')'", lp.line, lp.col);
        // ★ `(lit array u8 4 1 2 3 4)` — 괄호 안이 원소 나열 리터럴 **하나뿐**이면 한 겹으로 접는다. 안 접으면
        //   GROUP(FORM(GROUP(FORM(lit …)))) 이 되어 `lit …` 를 찾는 자리들이 못 본다(서식기가 값을 괄호로 싸서 찍는다).
        if (inner && inner->kind == LOW_CST_FORM && inner->nkids == 1 && inner->kids[0]->kind == LOW_CST_GROUP &&
            inner->kids[0]->nkids == 1 && inner->kids[0]->kids[0]->kind == LOW_CST_FORM &&
            inner->kids[0]->kids[0]->nkids && inner->kids[0]->kids[0]->kids[0]->kind == LOW_CST_ATOM &&
            inner->kids[0]->kids[0]->kids[0]->tok.kw == LOW_KW_LIT)
            inner = inner->kids[0]->kids[0];
        low_cst_t *g = low_node(p, LOW_CST_GROUP, lp);
        if (g) { proven_array_t one = PROVEN_ARRAY_INIT(p->work, low_cst_t *, 1).value;
                 (void)PROVEN_ARRAY_PUSH(&one, low_cst_t *, inner); low_take_kids(p, g, &one); }
        return g;
    }

    if (low_curkw(p) == LOW_KW_DO) {  // bare block
        // ★★ 2026-09-25 (소유자 결정) — `do … end` 는 **머리가 여는** 괄호다. 머리 없이 홀로 선 블록은 문법 표
        //   (정본 부록 A)에 없다. 전엔 받아 놓고 «낮출 수 없다» 는 주석과 함께 `--check` 가 초록이었다(돌지 않는다).
        const low_token_t *d = low_cur(p);
        // ☞ 괄호 식 뒤의 `do` 는 머리 있는 블록이다(`borrow b be (subslice s 0 2) do` · `make nest (nest u32) do`) —
        //   블록이 **폼을 여는** 자리(앞 낱말이 `.`·`end`·`do`·`(` 이거나 맨 앞)일 때만 머리가 없다.
        const low_token_t *pv = p->pos ? &p->toks[p->pos - 1] : NULL;
        bool opens = !pv || pv->kind == LOW_TOK_DOT || pv->kind == LOW_TOK_LPAREN ||
                     (pv->kind == LOW_TOK_IDENT && (pv->kw == LOW_KW_END || pv->kw == LOW_KW_DO));
        if (opens) low_pdiag(p, "E-BLOCK-NOHEAD",
                  "a `do … end` block needs a head that owns it — `if … do`, `while … do`, `fn … do`, `lit T do`, "
                  "`region … do` … A bare block is not in the grammar; it used to pass `--check` and then could not "
                  "be lowered. Put its statements where they belong, or give it its head", d->line, d->col);
        return low_parse_block(p);
    }

    if (low_is_atom_tok(k)) {
        low_token_t t = low_adv(p);
        // headed block: `record do … end` → FORM(head=record, [block]), end closer.
        // Suppressed (headed_ok=false) inside block statements, where the `do` belongs
        // to the *statement* (e.g. `if cond do…`), not the preceding atom.
        if (headed_ok && low_curkw(p) == LOW_KW_DO) {
            low_cst_t *blk = low_parse_block(p);
            low_cst_t *f = low_node(p, LOW_CST_FORM, t);
            if (f) {
                proven_array_t kids = PROVEN_ARRAY_INIT(p->work, low_cst_t *, 2).value;
                (void)PROVEN_ARRAY_PUSH(&kids, low_cst_t *, low_node(p, LOW_CST_ATOM, t));
                (void)PROVEN_ARRAY_PUSH(&kids, low_cst_t *, blk);
                f->closer = LOW_TOK_EOF;  // end
                low_take_kids(p, f, &kids);
            }
            return f;
        }
        return low_node(p, LOW_CST_ATOM, t);
    }

    low_token_t bad = low_adv(p);
    low_pdiag(p, "E-FORM-UNEXPECTED", "unexpected token in form", bad.line, bad.col);
    return low_node(p, LOW_CST_ERROR, bad);
}

// ★★ **중위 접근은 없앴다** (2026-07-13). `a to b` / `b in a` 는 `field a b` 와 **같은 뜻**이었다.
//   한 뜻에 네 철자가 있었고(전위·정방향·역방향·붙임 점), **이미 갈려 있었다**: 붙임 점만
//   인덱스를 못 했다. 이제 접근은 **전위 `field`/`index`**(정규형)와 **붙임 점**(설탕) 둘뿐이다.
//   `to`/`in` 은 어휘에서 사라졌다.
// ★★★ **`recv..op args` — 수신자 우선 머리** (RFC-0062).
//
//   `s..area 2`  ≡  `rect.area s 2`   (s 의 타입이 rect 일 때 — 해석은 IR 이 한다)
//
//   `.head` 와 **똑같은 나무**를 세운다 — 다만 **수신자가 첫 인자**로 앞에 온다.
//   닫는 규칙도 같다: 저자가 찍은 `.` 이 이 머리를 닫고, 개행은 남은 것을 다 닫는다.
//
//   ⇒ 이것이 사 주는 것: **op 이 타입의 이름공간에 산다.** `rect.area` 와 `circle.area` 가
//     공존하고, 전역 이름 `area` 는 **아무도 안 태운다.**
static low_cst_t *low_parse_access(low_parser_t *p, bool headed_ok) {
    low_cst_t *recv = low_parse_primary(p, headed_ok);
    while (recv && low_curk(p) == LOW_TOK_METHOD) {   // 사슬도 된다: s..scale 2 ..area
        low_token_t mt = low_adv(p);
        low_token_t nt = mt; nt.kind = LOW_TOK_IDENT;   // 이름 원자는 보통 이름처럼 보인다
        proven_array_t kids = PROVEN_ARRAY_INIT(p->work, low_cst_t *, 4).value;
        (void)PROVEN_ARRAY_PUSH(&kids, low_cst_t *, low_node(p, LOW_CST_ATOM, mt));  // 머리(METHOD 표시 유지)
        (void)PROVEN_ARRAY_PUSH(&kids, low_cst_t *, recv);                            // ★ 수신자 = 첫 인자
        while (!low_is_form_boundary(p) && low_curk(p) != LOW_TOK_DOT &&
               low_curk(p) != LOW_TOK_RPAREN &&
               low_curk(p) != LOW_TOK_METHOD) {
            low_cst_t *o = low_parse_primary(p, false);
            if (!o) break;
            (void)PROVEN_ARRAY_PUSH(&kids, low_cst_t *, o);
            if (o->kind == LOW_CST_BLOCK) break;
        }
        low_cst_t *f = low_node(p, LOW_CST_FORM, nt);
        low_cst_t *g = low_node(p, LOW_CST_GROUP, mt);   // tok.kind = METHOD — **저자의 주장**
        if (!f || !g) { proven_array_destroy(&kids); return recv; }
        f->closer = LOW_TOK_EOF;
        if (low_curk(p) == LOW_TOK_DOT) low_adv(p);      // 이 점은 **이 머리**의 것이다
        low_take_kids(p, f, &kids);
        f->synth = g->synth = true;
        proven_array_t one = PROVEN_ARRAY_INIT(p->work, low_cst_t *, 1).value;
        (void)PROVEN_ARRAY_PUSH(&one, low_cst_t *, f);
        low_take_kids(p, g, &one);
        recv = g;
    }
    return recv;
}

// Read a run of operands into `ops` (stops before comma / dot / boundary).
// Returns true if a do…end block terminated the run (block is the form's tail).
static bool low_read_run(low_parser_t *p, proven_array_t *ops, bool headed_ok) {
    while (!low_is_form_boundary(p)) {
        low_tok_kind_t k = low_curk(p);
        if (k == LOW_TOK_DOT) return false;
        // ★ X-0059 — 문장을 여는 낱말(`let`·`var`·`return`·`guard`)은 피연산자가 될 수 없다. 폼 한가운데서 만나면
        //   앞 문장의 점이 빠진 것이다(`let x be u64 a` ⏎ `return x .`). 전엔 둘이 한 폼이 되어 엉뚱한 진단
        //   (`E-RETURN-PARTIAL`)이 났다. `else` 바로 뒤는 나가는 문장의 자리라 제외한다(`else return 1 .`).
        //   머리가 낱말이 아닌 폼(`// …` 같은 없는 표기)은 그 진단(E-VOCAB-REMOVED)이 원인을 말하므로 건드리지 않는다.
        const low_cst_t *h0 = ops->len ? *(low_cst_t *const *)PROVEN_ARRAY_GET(ops, low_cst_t *, 0) : NULL;
        if (ops->len && p->pos && h0 && h0->kind == LOW_CST_ATOM && h0->tok.kind == LOW_TOK_IDENT) {
            low_kw_t kw = low_curkw(p);
            const low_token_t *pv = &p->toks[p->pos - 1];
            if ((kw == LOW_KW_LET || kw == LOW_KW_VAR || kw == LOW_KW_RETURN || kw == LOW_KW_GUARD) &&
                !(pv->kind == LOW_TOK_IDENT && (pv->kw == LOW_KW_ELSE || pv->kw == LOW_KW_DO))) {
                proven_u32 col = pv->col + (proven_u32)pv->lex.size;
                if (pv->kind == LOW_TOK_STRING) col += 2;
                low_pdiag(p, "E-DOT-MISSING",
                          "the statement before this one is not closed — every statement ends with its own `.` "
                          "(`let x be u64 a .` then `return x .`). A newline closes nothing", pv->line, col);
                return false;
            }
        }
        low_cst_t *op = low_parse_access(p, headed_ok);
        if (op) (void)PROVEN_ARRAY_PUSH(ops, low_cst_t *, op);
        if (op && op->kind == LOW_CST_BLOCK) return true;
        // ★★ **머리 붙은 블록도 form 을 닫는다.** SPEC-002 §2.3 R2: "블록으로 끝나는 form 은
        //   그 `end` 가 form 의 closer 를 겸한다". 그런데 **맨 블록에만** 적용하고 있었다.
        //   `else do … end` 같은 **머리 붙은 블록**은 run 을 안 끝냈고, 그래서 한 줄로 쓰면
        //   그 뒤의 문장이 **같은 form 으로 빨려 들어갔다**:
        //     do guard c . else do X . end  return 1 .  end
        //                                   ^^^^^^^^^^ else-form 과 한 form 이 된다
        //   개행이 **우연히** 가려 주고 있었다(`end` 뒤의 개행이 닫으니까). 한 줄로 쓰는 순간
        //   드러난다 — **서식기가 한 줄로 찍자 서식 보존 게이트가 즉시 잡았다.**
        if (op && op->kind == LOW_CST_FORM && op->nkids &&
            op->kids[op->nkids - 1]->kind == LOW_CST_BLOCK) return true;
        // 칸 골라 채우는 나열(`lit array u8 8 do 2 5 . end`)은 한 겹 GROUP 에 싸여 온다 — 구조체 값과 같이 블록이 닫는다
        if (op && op->kind == LOW_CST_GROUP && op->nkids == 1 && op->kids[0]->kind == LOW_CST_FORM && op->kids[0]->nkids &&
            op->kids[0]->kids[op->kids[0]->nkids - 1]->kind == LOW_CST_BLOCK) return true;
    }
    return false;
}

// block body: form* "end" — shared by `do…end` blocks and the headless
// struct/enum field list (`struct N 필드* end`, SPEC-002 schema — no `do`)
static low_cst_t *low_parse_block_body(low_parser_t *p, low_token_t at) {
    low_cst_t *blk = low_node(p, LOW_CST_BLOCK, at);
    proven_array_t kids = PROVEN_ARRAY_INIT(p->work, low_cst_t *, 4).value;
    while (low_curkw(p) != LOW_KW_END && low_curk(p) != LOW_TOK_EOF) {
        proven_size_t before = p->pos;
        low_cst_t *f = low_parse_form(p);
        if (f && f->nkids > 0) (void)PROVEN_ARRAY_PUSH(&kids, low_cst_t *, f);  // skip empty (stray closer)
        if (p->pos == before) low_adv(p);
    }
    if (low_curkw(p) == LOW_KW_END) low_adv(p);
    else low_pdiag(p, "E-BLOCK-UNCLOSED", "missing 'end' for do-block", at.line, at.col);
    if (blk) low_take_kids(p, blk, &kids); else proven_array_destroy(&kids);
    return blk;
}

// block = "do" form* "end"
// ★ 전엔 `do [as IDENT]` 였다(블록 레이블). `as` 는 어휘에서 없앴는데 **파서에 가지가 남아
//   있었다** — 절대 안 타는 코드다. 없앤 낱말의 **유령**은 개행 규칙에도 남아 있었다.
//   낱말을 빼면 그 낱말을 읽던 **모든 코드**를 함께 빼야 한다.
static low_cst_t *low_parse_block(low_parser_t *p) {
    low_token_t d = low_adv(p);  // 'do'
    return low_parse_block_body(p, d);
}

// generic form:  head-run closer
// The run yields head + args. Closer = '.' or a trailing do…end block's end.
// ★ 전엔 `( "," seg-run )*` 가 있었다 — R3(쉼표)이 다음 인자를 열었다. RFC-0103
//   (2026-08-27)에서 없앴다: 저장소 전체에서 COMMA 토큰이 셋뿐이었고 셋 다 줄잇기라,
//   인자 구분자로 쓴 자리가 **한 곳도 없었다**. 낱말을 빼면 그 낱말을 읽던 모든 코드를
//   함께 뺀다(`as` 가 파서에 유령으로 남아 있던 전례가 바로 위에 적혀 있다).
static low_cst_t *low_parse_generic(low_parser_t *p) {
    low_token_t head = *low_cur(p);
    proven_array_t ops = PROVEN_ARRAY_INIT(p->work, low_cst_t *, 4).value;
    bool block_tail = low_read_run(p, &ops, true);
    low_tok_kind_t closer = LOW_TOK_EOF;  // implicit / block-end unless '.' seen
    // ★★★ X-0052 (소유자 결정 2026-09-25) — `end` 는 자기 `do` 만 닫는다. 그래서 블록을 **몸으로 갖는** 머리
    //   (`else` · `region` · `borrow`)가 아니면, 블록으로 끝났어도 이 문장은 **자기 점**으로 닫는다
    //   (`let x be lit T do … end .` · `return pipe xs do … end .`). 괄호 안이면 `)` 가 닫는다.
    //   그리고 블록이 없어도 둘러싼 `end` 가 대신 닫아 주지 않는다 — `do return a end` 는 점이 빠졌다.
    bool owner = block_tail && head.kind == LOW_TOK_IDENT &&
                 (head.kw == LOW_KW_ELSE || head.kw == LOW_KW_DO ||   // do = 머리 없는 블록(이미 E-BLOCK-NOHEAD)
                  proven_u8str_view_eq(head.lex, proven_u8str_view_from_cstr("region")) ||
                  proven_u8str_view_eq(head.lex, proven_u8str_view_from_cstr("borrow")));
    if (!owner) {
        if (low_curk(p) == LOW_TOK_DOT) {
            closer = LOW_TOK_DOT;
            if (block_tail) (void)PROVEN_ARRAY_PUSH(&p->dot_ok, proven_size_t, p->pos);
            low_adv(p);
        } else if (low_curk(p) != LOW_TOK_RPAREN && low_curk(p) != LOW_TOK_EOF &&
                   (block_tail || low_curkw(p) == LOW_KW_END) && ops.len &&
                   // ☞ 여러 줄 원문(`text TERM … TERM`)은 끝 표지가 스스로 닫는다 — asm 몸의 `text ASM … ASM`.
                   !(p->pos && p->toks[p->pos - 1].kind == LOW_TOK_HEREDOC)) {
            const low_token_t *last = &p->toks[p->pos ? p->pos - 1 : 0];
            low_pdiag(p, "E-DOT-MISSING",
                      "this statement is not closed — `end` closes only its own `do` (it is a brace, not a "
                      "stop), so the statement must end with its own `.`: `let x be lit T do … end .`, "
                      "`return a .`", last->line, last->col + (proven_u32)last->lex.size);
        }
    }
    low_cst_t *f = low_node(p, LOW_CST_FORM, head);
    if (f) { f->closer = closer; low_take_kids(p, f, &ops); }
    else proven_array_destroy(&ops);
    return f;
}

// X-0059 — 머리의 절이 점 없이 끝났다. 자리는 그 절의 마지막 낱말 바로 뒤(점이 들어갈 곳).
static void low_head_dot_missing(low_parser_t *p, const low_token_t *last) {
    proven_u32 col = last->col + (proven_u32)last->lex.size;
    if (last->kind == LOW_TOK_STRING) col += 2;   // .lex 는 따옴표 안쪽이다
    low_pdiag(p, "E-DOT-MISSING",
              "this header clause is not closed — every clause of an op header ends with its own `.`, "
              "the same as a statement: `fn f input a u64 . output u64 . do … end`. A newline closes nothing, "
              "and the next clause word does not close the one before it", last->line, col);
}

// block-statement schema: HEAD operand* do BODY end   (op/if/for/loop);
// `if` may be followed by an else clause (block or nested if).
static low_cst_t *low_parse_block_stmt(low_parser_t *p) {
    low_token_t head = *low_cur(p);
    low_kw_t hkw = head.kw;
    proven_array_t ops = PROVEN_ARRAY_INIT(p->work, low_cst_t *, 4).value;
    (void)PROVEN_ARRAY_PUSH(&ops, low_cst_t *, low_node(p, LOW_CST_ATOM, low_adv(p)));  // head kw
    // struct/enum: `struct N 필드* end` — the field list is a headless block (no `do`,
    // SPEC-002 schema); a `do` form is also accepted (legacy S3 surface).
    // ★ actor 도 같은 스키마다: `actor N <state·on…> end` — 이름 + **무두 블록**.
    //   `state` 는 이름이 없다: `state <필드*> end`.
    if (hkw == LOW_KW_STRUCT || hkw == LOW_KW_ENUM || hkw == LOW_KW_TRAIT ||
        hkw == LOW_KW_ACTOR || hkw == LOW_KW_STATE || hkw == LOW_KW_CONTRACT) {
        if (hkw != LOW_KW_STATE && low_curk(p) == LOW_TOK_IDENT)
            (void)PROVEN_ARRAY_PUSH(&ops, low_cst_t *, low_node(p, LOW_CST_ATOM, low_adv(p)));
        // ★★★★★ **블록 선언도 `do … end` 다** (2026-09-15, 소유자 결정 — «do … end · 거절 + --fmt 수리»).
        //   전엔 `struct N .` · `struct N` · `struct N do` 셋이 다 통과했다. 개행은 닫개가 아니므로(§6.1.6) 줄바꿈 꼴은
        //   머리를 닫지도 못했다. 이제 fn 몸·if·while·make 와 같은 **한 규칙**이다: 블록은 `do` 로 열고 `end` 로 닫는다.
        //   다른 꼴은 거절하되 파스는 전처럼 이어 간다(뒤따르는 진단이 한 번에 보이게). `--fmt` 는 이미 `do` 꼴을 낸다.
        bool save_tr = p->in_trait;
        p->in_trait = (hkw == LOW_KW_TRAIT);
        if (low_curkw(p) == LOW_KW_DO) (void)PROVEN_ARRAY_PUSH(&ops, low_cst_t *, low_parse_block(p));
        else {
            low_pdiag(p, "E-STMT-NODO",
                      "a block declaration (struct/enum/trait/actor/contract/state) opens its body with `do` and closes "
                      "it with `end`: `struct rect do w u64 . end` — not `struct rect .` and not a bare line break "
                      "(a newline closes nothing). `--fmt` writes it for you", head.line, head.col);
            (void)PROVEN_ARRAY_PUSH(&ops, low_cst_t *, low_parse_block_body(p, *low_cur(p)));
        }
        p->in_trait = save_tr;
        low_cst_t *sf = low_node(p, LOW_CST_FORM, head);
        if (sf) { sf->closer = LOW_TOK_EOF; low_take_kids(p, sf, &ops); }
        else proven_array_destroy(&ops);
        return sf;
    }
    // operands up to 'do' — headed_ok=false so the `do` binds to this statement,
    // not to the operand before it (e.g. `if cond do…`, `for x in xs do…`).
    // '.'/',' before 'do' are MVP clause/condition separators (fn clauses,
    // `while cond . do`) — skipped so the block still binds to the statement.
    {
        // ★★★ 머리의 성질은 **둘**이다 — 그리고 그 둘을 섞어 놓은 것이 이 문법의 큰 모순이었다.
        //
        //   **선언 머리**(fn · proc · on · test · …) = **절의 목록**이다.
        //     `fn f input a u8 . output u8 . effects none . do …`
        //     여기서 `.` 은 **절을 나눈다**. 그러니 건너뛴다.
        //
        //   **제어 머리**(if · while · for · case · match) = **하나의 식**이다.
        //     `if gt a 10 . do …`
        //     여기서 `.` 은 **그 식을 닫는다.** 건너뛰면 안 된다 — 닫아야 한다.
        //
        //   전에는 **둘 다 건너뛰었다.** 그래서 제어 머리 뒤의 `.` 이 **아무 뜻도 없었고**,
        //   본체가 반드시 `do…end` 여야 했다(`if c . return 1 .` → E-STMT-NODO).
        //   그런데 `guard` 는 `else return 1 .` 를 받았다 — **같은 자리에서 규칙이 달랐다.**
        bool ctrl = (hkw == LOW_KW_IF || hkw == LOW_KW_WHILE || hkw == LOW_KW_FOR ||
                     hkw == LOW_KW_CASE || hkw == LOW_KW_MATCH);
        // ★★★ X-0059 (소유자 결정 ⓐ, 2026-09-25) — **머리의 절도 자기 점으로 닫는다.** 전엔 선언 머리의 점을
        //   건너뛰기만 해서 `fn f input a u64 output u64 do` 가 통과했다 — 절은 뒤 단계가 절 낱말로 잘라 주었다.
        //   문장과 칸은 점이 없으면 거절되는데 머리만 받아 주었다(정본 §6.1.6 (2): 닫개는 점 하나).
        //   이름 다음부터는 절 낱말이 절을 열고, 열린 절은 다음 절 낱말이나 `do` 앞에서 이미 점으로 닫혀 있어야 한다.
        bool decl = (hkw == LOW_KW_FN || hkw == LOW_KW_PROC || hkw == LOW_KW_TEST);
        bool named = false, open = false;
        // ★★★ X-0058 (소유자 결정 Ⓑ, 2026-09-25) — **몸이 C 에 있는 extern 은 블록 선언이다**: `extern proc f do <절>* end`.
        //   struct 가 칸을 `do … end` 에 담듯 절을 담는다. 전엔 `extern proc f <절>* end` 였다 — 짝 없는 `end` 가 이 한 자리뿐이었다
        //   (X-0052: `end` 는 자기 `do` 만 닫는다). 뒤 단계가 보는 나무는 **그대로다**: 블록 안의 절을 머리의 평평한 원자 열로 편다.
        bool cblock = p->in_extern && !p->in_export && decl;
        bool in_blk = false;
        low_token_t blk_at = head;
        while (cblock || (low_curkw(p) != LOW_KW_DO && !low_is_form_boundary(p))) {
            if (cblock) {
                if (!in_blk && named && low_curkw(p) == LOW_KW_DO) { blk_at = low_adv(p); in_blk = true; continue; }
                if (in_blk && low_curkw(p) == LOW_KW_END) { low_adv(p); break; }
                if (low_curk(p) == LOW_TOK_EOF || low_curk(p) == LOW_TOK_RPAREN) {
                    if (in_blk) low_pdiag(p, "E-BLOCK-UNCLOSED", "missing 'end' for do-block", blk_at.line, blk_at.col);
                    break;
                }
                if (!in_blk && low_curkw(p) == LOW_KW_END) {
                    // 옛 꼴 `extern proc f <절>* end` — 거절하되 같은 나무로 읽고(뒤 진단이 이어지게) `end` 를 먹는다.
                    low_pdiag(p, "E-STMT-NODO",
                              "an `extern` op whose body is in C is a block declaration: its clauses go inside "
                              "`do … end`, like the fields of a struct — `unsafe extern proc f do input k cap c . "
                              "output i64 . effects unsafe . link \"f\" . end`. `end` closes only its own `do`. "
                              "`--fmt` writes it for you", head.line, head.col);
                    low_adv(p);
                    break;
                }
                if (in_blk && !open && named && low_curk(p) == LOW_TOK_IDENT &&
                    !low_is_clause_word(low_cur(p)->lex) && low_curk(p) != LOW_TOK_DOT) {
                    low_pdiag(p, "E-FFI-BODY",
                              "an `extern` op's block holds its CLAUSES only (`input`, `output`, `effects`, `link` …) — "
                              "its body is IN C. A statement here would be a second body",
                              low_cur(p)->line, low_cur(p)->col);
                    while (low_curkw(p) != LOW_KW_END && low_curk(p) != LOW_TOK_EOF) low_adv(p);
                    continue;
                }
            }
            if (low_curk(p) == LOW_TOK_DOT) {
                low_adv(p);
                if (ctrl) break;          // ★ 제어 머리에서 `.` 은 **식을 닫는다**
                open = false;
                continue;                 //   선언 머리에서 `.` 은 **절을 나눈다**
            }
            // ★ **쉼표는 form 을 닫지 않는다**(R3). 처음엔 여기서 `.` 과 함께 닫게 했다 —
            //   내가 방금 적은 규칙을 내가 곧바로 어겼다. 쉼표는 **인자를 나눌** 뿐이다.
            proven_size_t at = p->pos;
            low_cst_t *op = low_parse_access(p, false);
            if (decl && op) {
                // 첫 낱말은 이름이다 — 이름이 절 낱말과 같아도(`proc link input …`) 절을 열지 않는다.
                bool cw = named && op->kind == LOW_CST_ATOM && low_is_clause_word(op->tok.lex);
                if (cw && open && at) low_head_dot_missing(p, &p->toks[at - 1]);
                if (cw) open = true;
                named = true;
            }
            if (op) (void)PROVEN_ARRAY_PUSH(&ops, low_cst_t *, op);
        }
        if (decl && open && p->pos) low_head_dot_missing(p, &p->toks[p->pos - 1]);
        if (cblock) {   // 몸 없는 선언으로 닫는다 — 아래의 `do`/`E-STMT-NODO` 가지를 타지 않는다
            low_cst_t *xf = low_node(p, LOW_CST_FORM, head);
            if (xf) { xf->closer = LOW_TOK_EOF; low_take_kids(p, xf, &ops); }
            else proven_array_destroy(&ops);
            return xf;
        }
    }
    if (low_curkw(p) == LOW_KW_DO) {
        (void)PROVEN_ARRAY_PUSH(&ops, low_cst_t *, low_parse_block(p));
    } else if (hkw == LOW_KW_IF || hkw == LOW_KW_WHILE || hkw == LOW_KW_FOR ||
               hkw == LOW_KW_CASE || hkw == LOW_KW_MATCH) {
        // ★★★ **한 문장은 곧 한 문장짜리 블록이다.**  S  ≡  do S end
        //   C 는 문장과 블록을 **다른 종류**로 갈라 놓았다. 우리는 가르지 않는다.
        //   파서가 여기서 **탈설탕**한다: 맨 form 하나를 `do … end` 로 감싼다.
        //   ⇒ 등가가 **구성적으로** 참이다(뒤 단계는 차이를 볼 수조차 없다).
        //     LowentBlock.v 의 `one_form_is_a_block` 이 그것을 기계로 확인한다.
        low_cst_t *one = low_parse_form(p);
        low_cst_t *blk = low_node(p, LOW_CST_BLOCK, head);
        if (blk && one) {
            proven_array_t bk = PROVEN_ARRAY_INIT(p->work, low_cst_t *, 1).value;
            (void)PROVEN_ARRAY_PUSH(&bk, low_cst_t *, one);
            low_take_kids(p, blk, &bk);
        }
        (void)PROVEN_ARRAY_PUSH(&ops, low_cst_t *, blk);
    } else if (p->in_extern) {
        // ★★★ **extern 은 본문이 없다.** 몸은 C 에 있다(RFC-0063). 여기서 몸을 요구하면
        //   "몸이 둘" 이 되거나, 있지도 않은 `do end` 를 쓰게 강요한다.
    } else {
        low_pdiag(p, "E-STMT-NODO",
                  "a declaration head (fn/proc/on/test/…) needs a `do … end` body — its "
                  "clauses are a LIST, so there is no single form to take as the body. "
                  "(Control heads — if/while/for/case/match — DO take one: `if c . return 1 .` "
                  "means exactly `if c . do return 1 . end`)", head.line, head.col);
    }
    // if … else …  (skip a soft newline-close between `end` and `else`)
    if (hkw == LOW_KW_IF) {
        proven_size_t save = p->pos;
        while (low_curk(p) == LOW_TOK_DOT) low_adv(p);
        if (low_curkw(p) != LOW_KW_ELSE) p->pos = save;   // no else: leave the closer(s) intact
    }
    if (hkw == LOW_KW_IF && low_curkw(p) == LOW_KW_ELSE) {
        low_adv(p);  // else
        if (low_curkw(p) == LOW_KW_IF) (void)PROVEN_ARRAY_PUSH(&ops, low_cst_t *, low_parse_block_stmt(p));
        else if (low_curkw(p) == LOW_KW_DO) (void)PROVEN_ARRAY_PUSH(&ops, low_cst_t *, low_parse_block(p));
        else {
            // ★ `else` 도 같은 규칙이다: **한 문장은 한 문장짜리 블록이다.**
            //   `guard` 의 else 는 이미 그랬는데 `if` 의 else 는 아니었다 — **같은 자리, 다른 규칙.**
            low_cst_t *one = low_parse_form(p);
            low_cst_t *blk = low_node(p, LOW_CST_BLOCK, head);
            if (blk && one) { proven_array_t bk = PROVEN_ARRAY_INIT(p->work, low_cst_t *, 1).value;
                              (void)PROVEN_ARRAY_PUSH(&bk, low_cst_t *, one); low_take_kids(p, blk, &bk); }
            (void)PROVEN_ARRAY_PUSH(&ops, low_cst_t *, blk);
        }
    }
    low_cst_t *f = low_node(p, LOW_CST_FORM, head);
    if (f) { f->closer = LOW_TOK_EOF; low_take_kids(p, f, &ops); }
    else proven_array_destroy(&ops);
    return f;
}

// for VAR in ITER do BODY end — the first `in` is the loop MARKER (not reverse
// access); the iterable may itself contain access. kids = [for, VAR, ITER…, BLOCK].
static low_cst_t *low_parse_for(low_parser_t *p) {
    low_token_t head = low_adv(p);  // 'for'
    proven_array_t ops = PROVEN_ARRAY_INIT(p->work, low_cst_t *, 4).value;
    (void)PROVEN_ARRAY_PUSH(&ops, low_cst_t *, low_node(p, LOW_CST_ATOM, head));
    if (low_curk(p) == LOW_TOK_IDENT)
        (void)PROVEN_ARRAY_PUSH(&ops, low_cst_t *, low_node(p, LOW_CST_ATOM, low_adv(p)));
    // ★ `in` 표시자는 **없앴다**: `for <name> <seq> do … end`. `do` 가 끝을 못 박으므로
    //   표시자는 아무것도 나르지 않았다(파서는 그것을 읽고 **트리에서 버리고** 있었다).
    // '.'/',' before 'do' are the iterable form's own closer/separators
    // (`for child in children_of g node . do`) — skipped, as in while/if
    // ★ `for` 도 **제어 머리**다: 첫 닫개가 iterable 을 닫는다. 그리고 본체는 `do…end` 이거나
    //   **form 하나**다(S ≡ do S end).
    while (low_curkw(p) != LOW_KW_DO && !low_is_form_boundary(p)) {
        if (low_curk(p) == LOW_TOK_DOT) {
            low_adv(p);
            // ★ RFC-0132 P2 (§8.1 점 규칙) — `for` 머리는 절의 열이다: `range … . where … . do` · `be τ v . while c . next e . do`.
            //   점 뒤에 이 절 낱말이 오면 머리가 이어진다 — 절 낱말은 원자로 넣는다(`while` 은 예약어라 문장으로 읽히면 안 된다).
            if (low_curkw(p) == LOW_KW_WHILE ||
                (low_curk(p) == LOW_TOK_IDENT && (low_view_eq_cstr(low_cur(p)->lex, "where") || low_view_eq_cstr(low_cur(p)->lex, "next")))) {
                (void)PROVEN_ARRAY_PUSH(&ops, low_cst_t *, low_node(p, LOW_CST_ATOM, low_adv(p)));
                continue;
            }
            break;
        }
        (void)PROVEN_ARRAY_PUSH(&ops, low_cst_t *, low_parse_access(p, false));
    }
    if (low_curkw(p) == LOW_KW_DO) (void)PROVEN_ARRAY_PUSH(&ops, low_cst_t *, low_parse_block(p));
    else {
        low_cst_t *one = low_parse_form(p);
        low_cst_t *blk = low_node(p, LOW_CST_BLOCK, head);
        if (blk && one) { proven_array_t bk = PROVEN_ARRAY_INIT(p->work, low_cst_t *, 1).value;
                          (void)PROVEN_ARRAY_PUSH(&bk, low_cst_t *, one); low_take_kids(p, blk, &bk); }
        (void)PROVEN_ARRAY_PUSH(&ops, low_cst_t *, blk);
    }
    low_cst_t *f = low_node(p, LOW_CST_FORM, head);
    if (f) { f->closer = LOW_TOK_EOF; low_take_kids(p, f, &ops); }
    else proven_array_destroy(&ops);
    return f;
}

// `module NAME do … end` (lowmini namespace record) OR bare `module NAME .` (본체 MVP
// file-level declaration). Block present → namespace; otherwise a plain `.`-closed form.
static low_cst_t *low_parse_module(low_parser_t *p) {
    low_token_t head = low_adv(p);  // 'module'
    proven_array_t ops = PROVEN_ARRAY_INIT(p->work, low_cst_t *, 4).value;
    (void)PROVEN_ARRAY_PUSH(&ops, low_cst_t *, low_node(p, LOW_CST_ATOM, head));
    while (low_curkw(p) != LOW_KW_DO && !low_is_form_boundary(p) && low_curk(p) != LOW_TOK_DOT) {
        low_cst_t *op = low_parse_access(p, false);
        if (op) (void)PROVEN_ARRAY_PUSH(&ops, low_cst_t *, op);
    }
    low_tok_kind_t closer = LOW_TOK_EOF;
    if (low_curkw(p) == LOW_KW_DO) (void)PROVEN_ARRAY_PUSH(&ops, low_cst_t *, low_parse_block(p));
    else if (low_curk(p) == LOW_TOK_DOT) { low_adv(p); closer = LOW_TOK_DOT; }  // bare declaration
    low_cst_t *f = low_node(p, LOW_CST_FORM, head);
    if (f) { f->closer = closer; low_take_kids(p, f, &ops); }
    else proven_array_destroy(&ops);
    return f;
}

// `export <definition>` — a prefix modifier that wraps the following form (proc/fn/
// let/…) as FORM(head=export, [export, def]); the evaluator marks the def's name public.
static low_cst_t *low_parse_export(low_parser_t *p) {
    low_token_t ex = low_adv(p);  // 'export' / 'unsafe' / 'extern'
    // ★★★ **`unsafe target <iset>`** (RFC-0040 D5) — `target` 은 문맥 낱말(task_group 처럼, 어휘 0).
    //   `unsafe` 뒤에 `target` 이 오면 그 다음 낱말이 명령셋(iset)이다. 이 op 은 target-게이트가 된다.
    proven_u8str_view_t iset = { 0 };
    if (ex.kw == LOW_KW_UNSAFE && low_curk(p) == LOW_TOK_IDENT &&
        proven_u8str_view_eq(low_cur(p)->lex, proven_u8str_view_from_cstr("target"))) {
        low_adv(p);  // 'target'
        if (low_curk(p) == LOW_TOK_IDENT) iset = low_adv(p).lex;  // <iset>
    }
    bool save_ext = p->in_extern, save_exp = p->in_export;
    if (ex.kw == LOW_KW_EXTERN) p->in_extern = true;
    if (ex.kw == LOW_KW_EXPORT) p->in_export = true;
    low_cst_t *def = low_parse_form(p);
    p->in_extern = save_ext; p->in_export = save_exp;
    low_cst_t *f = low_node(p, LOW_CST_FORM, ex);
    if (f) {
        f->target_iset = iset;   // 비어 있지 않으면 target-게이트
        proven_array_t k = PROVEN_ARRAY_INIT(p->work, low_cst_t *, 2).value;
        (void)PROVEN_ARRAY_PUSH(&k, low_cst_t *, low_node(p, LOW_CST_ATOM, ex));
        (void)PROVEN_ARRAY_PUSH(&k, low_cst_t *, def);
        f->closer = LOW_TOK_EOF; low_take_kids(p, f, &k);
    }
    return f;
}

 low_cst_t *low_parse_form(low_parser_t *p) {
    low_kw_t kw = (low_curk(p) == LOW_TOK_IDENT) ? low_curkw(p) : LOW_KW_NONE;
    // ★★★ **`task_group do … end`** (RFC-0009 D3-b) — 블록-문 스키마를 쓰지만 렉서 키워드는
    //   아니다(`drain`·`schedule` 처럼 문맥 낱말 — 어휘 0 증가). `head + do BODY end` 로 읽는다.
    if (kw == LOW_KW_NONE && low_curk(p) == LOW_TOK_IDENT &&
        proven_u8str_view_eq(low_cur(p)->lex, proven_u8str_view_from_cstr("task_group")))
        return low_parse_block_stmt(p);
    // ★★★ **`pipe <source> do … end`** (RFC-0010 §6.1, 표면 정정 2026-07-18) — task_group 과 같은
    //   블록-문 스키마. `pipe` 는 **문맥 낱말**이라 하드 키워드가 0 증가한다(어휘 폐집합 43 불변).
    //   원래 §6.1 은 "개행 유의미" 였으나 렉서가 개행 토큰을 내지 않는다(실측) — 그래서 `.` 종결 + 블록.
    if (kw == LOW_KW_NONE && low_curk(p) == LOW_TOK_IDENT &&
        proven_u8str_view_eq(low_cur(p)->lex, proven_u8str_view_from_cstr("pipe")))
        return low_parse_block_stmt(p);
    // ★★★ 2026-09-15 (소유자 결정: 추천안) — **trait 의 서명에는 `fn`/`proc` 을 적지 않는다.** 순수한지는 서명의 `effects` 줄이
    //   정하고, `fn` 인지 `proc` 인지는 갖추는 쪽이 고른다(정본 §6.11.2 (1b)). 전엔 적으면 `fn` 이 서명의 **이름**으로 읽혀
    //   엉뚱한 `E-TRAIT-MISSING` 이 났다. ⇒ 분명히 말하고, 낱말을 건너 서명으로 읽어 뒤 검사가 제대로 이어지게 한다.
    if (p->in_trait && (kw == LOW_KW_FN || kw == LOW_KW_PROC)) {
        low_token_t w = low_adv(p);
        low_pdiag(p, "E-TRAIT-SIG",
                  "a trait signature does not say `fn` or `proc` — write `area input s self . output u64 .`. Its `effects` "
                  "line says what the op may do (none = pure), and the implementation chooses `fn` or `proc` (§6.11.2)",
                  w.line, w.col);
        return low_parse_generic(p);
    }
    switch (kw) {
        // ★ SPEC-003 §21 의 수식자 넷. `export` 만 이 자리에 있었다 — 나머지 셋은
        //   기본 닫개(`.`)로 떨어져 선언을 **조각냈다**(op 이 통째로 사라진다).
        case LOW_KW_EXPORT: case LOW_KW_UNSAFE: case LOW_KW_EXTERN:
            return low_parse_export(p);
        case LOW_KW_MODULE: // module NAME do … end (namespace) OR bare `module NAME .` (MVP)
            return low_parse_module(p);
        case LOW_KW_FOR:
            return low_parse_for(p);
        case LOW_KW_IF:
        // op decls (mini `proc`, MVP `fn`/`proc`) + MVP block heads (S3) —
        // same block-stmt schema, added additively (BOOTSTRAP §3)
        // ★★ `make` 를 **뺐다.** 여기 있으면 괄호 안에서 `make` 가 **자기가 머리**가 되어
        //   `FORM(make, T, BLOCK)` 이 되고, 괄호 밖에서는 `ATOM(make) · FORM(T, BLOCK)` 이 된다 —
        //   **같은 소스가 두 개의 나무**가 된다(교훈 7). IR 에서 두 모양을 다 받도록 기웠지만
        //   그건 **뒤 단계에서 문법의 병을 치료한 것**이다. 병은 문법에 있다.
        //   `make` 는 **값**이지 문장이 아니다 ⇒ 일반 form 으로 읽으면 `make` 는 원자이고
        //   `T do … end` 가 머리 붙은 블록이 된다 — **괄호 안팎이 같아진다.**
        case LOW_KW_FN: case LOW_KW_PROC: case LOW_KW_TEST:
        case LOW_KW_ACTOR: case LOW_KW_STATE: case LOW_KW_CONTRACT:   // ★ actor N … end · state … end
        case LOW_KW_STRUCT: case LOW_KW_ENUM: case LOW_KW_TRAIT: case LOW_KW_MATCH:
        case LOW_KW_CASE:   case LOW_KW_WHILE:
            return low_parse_block_stmt(p);
        default:
            return low_parse_generic(p);
    }
}


// ══ 정규화 층 (DECISION-0015) ═══════════════════════════════════════════════
//
// ★★★ **"form 이 무엇인가" 는 한 곳에서만 답한다.**
//
// 이 문법은 파서를 **의도적으로 무지하게** 만든다(구조 = 문자열, RFC-0046): 파서는 arity 도
// 타입도 모른다. 조각을 **혼자서도 읽을 수 있게** 하려는 좋은 선택이다.
//
// 그런데 그 대가를 **한 곳에서** 치르지 않고 **여섯 곳에서** 치르고 있었다:
//   low_check · low_ir · low_typecheck · low_contract · low_region · low_doc
//   — 여섯이 평평한 CST 를 **각자** 훑으며 구조를 다시 알아냈다(kids[] 접근 772회).
//   그리고 여섯이 **조금씩 다른 답**을 냈다. 이번 세션에만 일곱 번 물렸고, 전부 **조용히 틀린 답**:
//     · guard 의 else 가 **자식이 아니라 형제**  → IR 이 앞으로 훑어 찾았다
//     · `var x be T v .` 가 **두 개의 form**   → IR 이 "split binding" 으로 재조립했다
//     · make 가 괄호 안팎에서 **두 개의 나무**   → IR 에 반창고를 붙였다
//     · actor 핸들러의 input 절이 **통째로 버려졌다**
//
// 이 층이 그 답을 **한 번만** 낸다: 파스 직후, **CST → CST**. 소비자는 하나도 안 고친다 —
// 그냥 **한 모양**만 보게 된다.
 low_cst_t *low_refit(low_parser_t *p, low_cst_t *f, low_cst_t **kids, proven_size_t n) {
    proven_result_mem_mut_t r = p->node_alloc.alloc_fn(
        p->node_alloc.ctx, sizeof(low_cst_t *) * (n ? n : 1), alignof(low_cst_t *));
    if (r.err != PROVEN_OK) { low_node_oom(p); return f; }
    low_cst_t **dst = (low_cst_t **)r.value.ptr;
    for (proven_size_t i = 0; i < n; i++) dst[i] = kids[i];
    f->kids = dst; f->nkids = n;
    return f;
}
static bool low_head_is(const low_cst_t *f, low_kw_t kw) {
    return f && f->kind == LOW_CST_FORM && f->nkids && f->kids[0]->kind == LOW_CST_ATOM &&
           f->kids[0]->tok.kw == kw;
}
// 한 겹씩 싸인 form 을 벗긴다 (`(else do … end)` 처럼 파서가 한 겹 더 감싼 경우)
static const low_cst_t *low_peel(const low_cst_t *f) {
    while (f && f->kind == LOW_CST_FORM && f->nkids == 1 && f->kids[0]->kind == LOW_CST_FORM)
        f = f->kids[0];
    return f;
}
 void low_norm_node(low_parser_t *p, low_cst_t *nd);

// 블록의 문장 열을 정규화한다: 조각난 form 을 **하나로** 붙인다.
//
// ★★★ 처음엔 `out[512]` · `buf[64]` **고정 배열**로 썼다. 그리고 넘치면 **말없이 잘렸다**:
//   문장 600 개짜리 블록이 **f() = 0 을 냈다** (600 이어야 하는데) — 512 에서 잘려 `return x`
//   가 통째로 사라졌고, **오류 하나 없었다.** 이 프로젝트가 이미 다섯 군데서 고친 그 유형이다
//   (prng[8] · f[8] · enumv[64] · ops[256] · VM_DEPTH). **내가 여섯 번째를 만들었다.**
//   임의의 상한은 **반드시** 넘긴다. 동적으로 잡는다 — 상한이 없으면 잘릴 것도 없다.
// ★★ RFC-0135 S1 — **바인딩 `else`**: `let n be T v . else <벗어남> .` — 점이 바인딩을 닫으므로 `else` 는 형제로 온다.
//   guard 처럼 **마지막 자식**으로 끌어들인다. 뜻(숨은 임시 · guard · 꺼내기)은 T1 패스 뒤의 펼치기(`low_bind_else_expand`)가
//   짓는다 — 여기서는 타입과 값의 경계를 모르기 때문이다. 서식기는 이 모양 그대로 찍는다.
static void low_bind_attach_else(low_parser_t *p, low_cst_t *blk, proven_size_t *i, proven_array_t *out) {
    if (*i + 1 >= blk->nkids || !out->len) return;
    const low_cst_t *nx = low_peel(blk->kids[*i + 1]);
    if (!nx || nx->kind != LOW_CST_FORM || !nx->nkids || !low_head_is(nx, LOW_KW_ELSE)) return;
    low_cst_t *bf = ((low_cst_t **)out->data)[out->len - 1];
    bool has_be = false;
    for (proven_size_t x = 0; x < bf->nkids; x++) if (bf->kids[x]->kind == LOW_CST_ATOM && bf->kids[x]->tok.kw == LOW_KW_BE) has_be = true;
    if (!has_be) return;
    proven_array_t k2 = PROVEN_ARRAY_INIT(p->work, low_cst_t *, bf->nkids + 1).value;
    for (proven_size_t x = 0; x < bf->nkids; x++) (void)PROVEN_ARRAY_PUSH(&k2, low_cst_t *, bf->kids[x]);
    (void)PROVEN_ARRAY_PUSH(&k2, low_cst_t *, (low_cst_t *)nx);
    ((low_cst_t **)out->data)[out->len - 1] = low_refit(p, bf, (low_cst_t **)k2.data, k2.len);
    proven_array_destroy(&k2);
    (*i)++;
}
static void low_norm_seq(low_parser_t *p, low_cst_t *blk) {
    proven_array_t out = PROVEN_ARRAY_INIT(p->work, low_cst_t *, 16).value;
    proven_array_t buf = PROVEN_ARRAY_INIT(p->work, low_cst_t *, 16).value;
    for (proven_size_t i = 0; i < blk->nkids; i++) {
        low_cst_t *f = blk->kids[i];
        if (f->kind != LOW_CST_FORM || !f->nkids || f->kids[0]->kind != LOW_CST_ATOM) {
            (void)PROVEN_ARRAY_PUSH(&out, low_cst_t *, f); continue;
        }
        low_kw_t hk = f->kids[0]->tok.kw;

        // ── ① `guard <cond…> . [<조각>…] else <…>` — **else 를 자식으로 끌어들인다.**
        //   점이 guard 를 닫으므로 else 는 **형제**로 온다. 그리고 점이 하나 더 있으면
        //   조건의 조각이 사이에 낀다. 여섯 소비자가 각자 이 재조립을 해야 했다.
        if (hk == LOW_KW_GUARD) {
            buf.len = 0;
            for (proven_size_t x = 0; x < f->nkids; x++)
                (void)PROVEN_ARRAY_PUSH(&buf, low_cst_t *, f->kids[x]);
            proven_size_t j = i + 1;
            const low_cst_t *els = NULL;
            for (; j < blk->nkids; j++) {
                const low_cst_t *nx = low_peel(blk->kids[j]);
                if (!nx || nx->kind != LOW_CST_FORM || !nx->nkids) break;
                if (low_head_is(nx, LOW_KW_ELSE)) { els = nx; break; }
                if (nx->kids[0]->kind == LOW_CST_ATOM && nx->kids[0]->tok.kw != LOW_KW_NONE) break;
                for (proven_size_t x = 0; x < nx->nkids; x++)   // 조건의 조각
                    (void)PROVEN_ARRAY_PUSH(&buf, low_cst_t *, nx->kids[x]);
            }
            if (els) {
                (void)PROVEN_ARRAY_PUSH(&buf, low_cst_t *, (low_cst_t *)els);   // ★ else 는 **마지막 자식**
                (void)PROVEN_ARRAY_PUSH(&out, low_cst_t *,
                                        low_refit(p, f, (low_cst_t **)buf.data, buf.len));
                i = j;
                continue;
            }
        }

        // ── ② `var x be <type> <value> .` — **be 를 form 안으로 끌어들인다.**
        //   타입이 자기 닫개를 가지면(`ref u8 .`) 점이 var 를 닫아 버려 `be …` 가 형제가 된다.
        if (hk == LOW_KW_VAR || hk == LOW_KW_LET) {
            bool inline_be = false;
            for (proven_size_t x = 0; x < f->nkids; x++)
                if (f->kids[x]->kind == LOW_CST_ATOM && f->kids[x]->tok.kw == LOW_KW_BE)
                    inline_be = true;
            if (!inline_be) {
                buf.len = 0;
                for (proven_size_t x = 0; x < f->nkids; x++)
                    (void)PROVEN_ARRAY_PUSH(&buf, low_cst_t *, f->kids[x]);
                proven_size_t j = i + 1;
                bool fused = false;
                for (; j < blk->nkids && j <= i + 3; j++) {
                    low_cst_t *v = blk->kids[j];
                    if (v->kind != LOW_CST_FORM || !v->nkids || v->kids[0]->kind != LOW_CST_ATOM) break;
                    // ★★★★ RFC-0112 D8(3) — `let v be (<타입> . using <출처>) …` : 타입의 닫개 뒤에 온 `using` 폼도
                    //   `be` 폼처럼 바인딩에 붙인다(그 폼 안에 `be` 가 있을 때).
                    bool using_be = false;
                    if (v->kids[0]->kind == LOW_CST_ATOM && low_view_eq_cstr(v->kids[0]->tok.lex, "using"))
                        for (proven_size_t x = 1; x < v->nkids; x++)
                            if (v->kids[x]->kind == LOW_CST_ATOM && v->kids[x]->tok.kw == LOW_KW_BE) using_be = true;
                    if (using_be) {
                        for (proven_size_t x = 0; x < v->nkids; x++)
                            (void)PROVEN_ARRAY_PUSH(&buf, low_cst_t *, v->kids[x]);
                        fused = true; break;
                    }
                    if (v->kids[0]->tok.kw != LOW_KW_BE) {              // 타입의 꼬리 조각
                        if (v->kids[0]->tok.kw != LOW_KW_NONE) break;
                        for (proven_size_t x = 0; x < v->nkids; x++)
                            (void)PROVEN_ARRAY_PUSH(&buf, low_cst_t *, v->kids[x]);
                        continue;
                    }
                    for (proven_size_t x = 0; x < v->nkids; x++)
                        (void)PROVEN_ARRAY_PUSH(&buf, low_cst_t *, v->kids[x]);
                    fused = true; break;
                }
                if (fused) {
                    (void)PROVEN_ARRAY_PUSH(&out, low_cst_t *,
                                            low_refit(p, f, (low_cst_t **)buf.data, buf.len));
                    i = j; low_bind_attach_else(p, blk, &i, &out); continue;
                }
            }
        }
        (void)PROVEN_ARRAY_PUSH(&out, low_cst_t *, f);
        if (hk == LOW_KW_VAR || hk == LOW_KW_LET) low_bind_attach_else(p, blk, &i, &out);
    }
    low_refit(p, blk, (low_cst_t **)out.data, out.len);
    proven_array_destroy(&out); proven_array_destroy(&buf);
    for (proven_size_t i = 0; i < blk->nkids; i++) low_norm_node(p, blk->kids[i]);
}
 void low_norm_node(low_parser_t *p, low_cst_t *nd) {
    if (!nd || nd->kind == LOW_CST_ATOM) return;
    if (nd->kind == LOW_CST_BLOCK) { low_norm_seq(p, nd); return; }
    for (proven_size_t i = 0; i < nd->nkids; i++) low_norm_node(p, nd->kids[i]);
}

low_parse_result_t low_parse(proven_allocator_t node_alloc, proven_allocator_t work,
                             const proven_array_t *tokens) {
    low_parse_result_t out = { .ok = true };
    proven_result_array_t da = PROVEN_ARRAY_INIT(work, low_diag_t, 8);
    if (da.err != PROVEN_OK) { out.ok = false; return out; }
    out.diags = da.value;

    low_parser_t p = { .toks = (const low_token_t *)tokens->data, .n = tokens->len,
                       .pos = 0, .node_alloc = node_alloc, .work = work, .out = &out };

    p.dot_ok = PROVEN_ARRAY_INIT(work, proven_size_t, 16).value;

    proven_array_t forms = PROVEN_ARRAY_INIT(work, low_cst_t *, 32).value;
    while (low_curk(&p) != LOW_TOK_EOF) {
        proven_size_t before = p.pos;
        low_cst_t *f = low_parse_form(&p);
        if (f && f->nkids > 0) (void)PROVEN_ARRAY_PUSH(&forms, low_cst_t *, f);  // skip empty
        if (p.pos == before) low_adv(&p);  // guarantee progress
    }
    // ★★★ X-0052 (소유자 결정 2026-09-25) — **`do … end` 는 서로 짝인 괄호다. `end` 는 자기 `do` 만 닫는다.**
    //   블록을 몸으로 갖는 구문(fn·if·while·match·struct·region·borrow·else …)은 그 블록이 끝나면 끝나므로 뒤의 점은
    //   닫을 것이 없다. 블록을 품은 **값**을 쓰는 문장(`let x be lit T do … end .`)은 자기 닫개를 스스로 찍고,
    //   그 점은 파스가 `dot_ok` 에 적어 두었다. 적히지 않은 `end` 뒤 점과 `do` 뒤 점이 E-DOT-STRAY 다.
    //   ☞ 좁게 문다: 앞 낱말이 `end`·`do` 인 점만. 타입을 겹쳐 닫는 점(`slice u8 . .`)은 RFC-0113 R6 몫이다.
    {
        proven_size_t k = 0;
        const proven_size_t *ok = (const proven_size_t *)p.dot_ok.data;
        for (proven_size_t i = 1; i < p.n; i++) {
            const low_token_t *tk = &p.toks[i], *pv = &p.toks[i - 1];
            if (tk->kind != LOW_TOK_DOT || pv->kind != LOW_TOK_IDENT) continue;
            while (k < p.dot_ok.len && ok[k] < i) k++;
            if (pv->kw == LOW_KW_END && !(k < p.dot_ok.len && ok[k] == i))
                low_pdiag(&p, "E-DOT-STRAY",
                          "a stop after `end` closes nothing here — this construct owns its `do … end` block and "
                          "ends with it (like `}` in C). Delete the `.`. A statement that only USES a block value "
                          "(`let x be lit T do … end .`) does take its own stop",
                          tk->line, tk->col);
            else if (pv->kw == LOW_KW_DO)
                low_pdiag(&p, "E-DOT-STRAY",
                          "a stop after `do` closes nothing — `do` opens a block, there is no form to close yet. "
                          "Delete the `.`", tk->line, tk->col);
        }
    }
    proven_array_destroy(&p.dot_ok);
    // ★★★ **정규화 — "form 이 무엇인가" 를 여기서 한 번만 답한다.**
    for (proven_size_t i = 0; i < forms.len; i++)
        low_norm_node(&p, *(low_cst_t *const *)proven_array_get(&forms, i));

    out.nforms = forms.len;
    if (forms.len > 0) {
        proven_result_mem_mut_t r = work.alloc_fn(work.ctx, sizeof(low_cst_t *) * forms.len,
                                                  alignof(low_cst_t *));
        if (r.err == PROVEN_OK) {
            out.forms = (low_cst_t **)r.value.ptr;
            for (proven_size_t i = 0; i < forms.len; i++)
                out.forms[i] = *(low_cst_t *const *)proven_array_get(&forms, i);
        } else { out.ok = false; out.nforms = 0; }
    }
    proven_array_destroy(&forms);

    // ★★ **수식자를 붙이면 op 이 통째로 사라졌다.** (SPEC-003 §21: 수식자 export/unsafe/local/extern)
    //
    //   모든 소비자가 `f->kids[0]->tok.kw == LOW_KW_FN` 로 op 을 알아본다. 그런데 파서는
    //   수식자를 **머리로 삼고 op 을 그 아래 중첩**시킨다: (export (fn f …)).
    //   ⇒ 최상위 form 은 op 이 아니다. **op 으로 인식되지 않는다.**
    //     `export fn f output u8 . do return 7 . end`  →  `--check: ok`, **IR 0 defs.**
    //   선언한 op 이 **존재하지 않는데**, 아무도 안 부르면 초록불이었다. 문법에 있는 철자다.
    //
    //   여기서 **한 번에** 정규화한다(소비자가 9개 파일에 21곳 흩어져 있다 — 각자 고치면
    //   하나를 빠뜨린다). 껍질을 벗기면 op 은 **존재하고 정상 동작한다.**
    //   그리고 수식자의 **의미**(가시성·unsafe 규율·extern 링크)는 아직 강제하지 않는다 —
    //   그것을 **말한다**(W-NOT-YET). 조용히 무시하는 것이 죄다(PRINCIPLES.md §0 교훈 2).
    for (proven_size_t i = 0; i < out.nforms; i++) {
        bool peeled = false;
        proven_u32 mline = 0;
        proven_u32 outer_line = 0, outer_col = 0;   // ★ **가장 바깥** 수식자의 위치(RFC-0065)
        for (;;) {
            low_cst_t *f = out.forms[i];
            if (!f || f->kind != LOW_CST_FORM || f->nkids < 2) break;
            if (f->kids[0]->kind != LOW_CST_ATOM) break;
            proven_u8str_view_t w = f->kids[0]->tok.lex;
            if (!(proven_u8str_view_eq(w, proven_u8str_view_from_cstr("export")) ||
                  proven_u8str_view_eq(w, proven_u8str_view_from_cstr("unsafe")) ||
                  proven_u8str_view_eq(w, proven_u8str_view_from_cstr("extern")))) break;
            if (f->kids[1]->kind != LOW_CST_FORM || f->kids[1]->nkids < 2) break;
            low_cst_t *inner = f->kids[1];
            if (inner->kids[0]->kind != LOW_CST_ATOM) break;
            low_kw_t k = inner->kids[0]->tok.kw;
            // ★★★ **수식자는 op 에만 붙는 것이 아니다** — `export struct rect … end` 도 문법이다.
            //   그런데 벗기기가 **fn/proc 만** 봤다. 그래서 `export struct` 는 껍질에 싸인
            //   채로 남았고 — **선언된 타입으로 보이지 않았다.**
            //     · `rect.area` 가 **E-NAME-QUALIFIER**(거짓 오류)
            //     · 이름 중복·가시성 검사에서 그 struct 가 **투명인간**
            //   ★ 그런데 **IR 은 잘 돌았다**(프로그램이 24 를 냈다). **컴파일되고 실행되고
            //     검사만 틀렸다** — 조용히 틀린 답의 사촌이다.
            // ★★★ **수식자는 겹칠 수 있다**: `unsafe extern proc …` (RFC-0063 은 그 둘을 다 요구한다).
            //   한 겹만 벗겼더니 안쪽 op 이 **존재하지 않는 이름**이 됐다(E-IR-UNDEF) —
            //   op 이 통째로 사라진 것이다. 그러니 **모디파이어도 벗길 대상**이다.
            if (k != LOW_KW_FN && k != LOW_KW_PROC && k != LOW_KW_STRUCT &&
                k != LOW_KW_ENUM && k != LOW_KW_TYPE && k != LOW_KW_NEWTYPE && k != LOW_KW_ACTOR && k != LOW_KW_TRAIT &&
                k != LOW_KW_EXPORT && k != LOW_KW_UNSAFE && k != LOW_KW_EXTERN) break;
            mline = f->kids[0]->tok.line;
            // ★ **가장 바깥** 수식자의 위치를 기억한다(첫 겹). 겹이 여럿이면
            //   `export extern fn` 의 `export`(열 0)가 안전지대의 시작이다.
            if (!outer_col) { outer_line = f->kids[0]->tok.line; outer_col = f->kids[0]->tok.col; }
            inner->line = outer_line;
            inner->col  = outer_col;
            // ★ 껍질은 벗기되 **표시는 남긴다** — 그래야 가시성을 강제할 수 있다.
            // ★ 바깥 껍질의 표시를 **안쪽으로 물려준다** — 아니면 두 겹일 때 첫 표시가 사라진다.
            inner->is_export |= f->is_export;
            inner->is_unsafe |= f->is_unsafe;
            inner->is_extern |= f->is_extern;
            if (f->target_iset.size) inner->target_iset = f->target_iset;   // ★ target-게이트 물려주기 (RFC-0040 D5)
            if (proven_u8str_view_eq(w, proven_u8str_view_from_cstr("export"))) inner->is_export = true;
            if (proven_u8str_view_eq(w, proven_u8str_view_from_cstr("unsafe"))) inner->is_unsafe = true;
            if (proven_u8str_view_eq(w, proven_u8str_view_from_cstr("extern"))) inner->is_extern = true;
            out.forms[i] = inner;          // 껍질을 벗긴다 — op 이 존재하게 된다
            peeled = true;
        }
        (void)peeled;
        (void)mline;
        // ★★★ **고백이 사라졌다 — 구현됐기 때문이다** (2026-07-15, RFC-0063).
        //   여기 W-NOT-YET 이 있었다: *"`extern` LINKAGE 는 아직 강제되지 않는다"*.
        //   이제 `extern` 은 **몸이 C 에 있다**는 뜻이고(본문이 있으면 E-FFI-BODY),
        //   C 심볼을 부르며(`link`), **계약이 경계를 지킨다**(RFC-0063 D5).
        //   ★ 고백을 **구현 없이** 지우는 것이 최악이고, **구현하고도 남겨 두는 것**이 그다음이다.
        //     남겨 두면 그것도 거짓말이 된다 — 도구가 자기 능력을 **과소**하게 말하는 거짓말.
    }

    // ★★ `input [comptime] N T .` — **문법에 명시적으로 있다**(SPEC-002 §217).
    //   그런데 `comptime` 을 아무도 몰라서, 소비자들이 그것을 **파라미터 이름**으로 읽고
    //   진짜 이름을 **타입**으로 읽었다: `input comptime n u8 .` → "E-TYPE-UNDEF: 타입 n 이 없다".
    //   ⇒ **문법에 있는 형태를 "네 프로그램이 틀렸다" 고 거절**했다. 또 하나의 오진이다.
    //
    //   여기서 표식을 떼어낸다(소비자가 여러 파일에 흩어져 있다 — 각자 고치면 하나를 빠뜨린다).
    //   그리고 comptime **자체의 의미**(인자가 컴파일 시간 상수여야 한다 · 접힌다)는
    //   아직 강제하지 않는다 — **그것을 말한다**(W-NOT-YET).
    // ★★★ **2026-07-14 — 표식을 더는 떼어내지 않는다.** (RFC-0021 구현)
    //   전엔 여기서 `comptime` 을 CST 에서 **지웠다.** 그래서 `low_op_header` 의 `is_comptime`
    //   는 **영원히 false** 였고, RFC-0021 의 대표 문법(`input comptime t type .`)이
    //   **작동할 수 없었다.** 표식을 지운 채로 "comptime 은 아직" 이라고 말하는 것은
    //   **자백이 아니라 자물쇠**였다 — 지어질 수 없게 만든 것이다.
    //   ⇒ 이제 표식은 **남고**, `low_mono()` 가 그것으로 **단형화**한다.
    return out;
}

// ★★★ 절 어휘 — **유일한 목록.** (low_check · low_typecheck · low_contract · low_ir 이 여기를 본다.)
 bool low_view_eq_cstr(proven_u8str_view_t v, const char *s) {
    return proven_u8str_view_eq(v, proven_u8str_view_from_cstr(s));
}
bool low_is_clause_word(proven_u8str_view_t v) {
    // ★★★ **절 낱말은 키워드가 아니다** — 그래서 어휘를 늘리지 않고 문법을 늘릴 수 있다.
    //   `vector`/`priority` 는 ISR 의 절이다(RFC-0042 D5). 새 키워드 **0개**.
    static const char *K[] = { "input", "output", "effects", "requires", "ensures", "errors",
                               "tests", "access", "parallel", "reduce", "satisfies", "lowdoc",
                               "vector", "priority",
                               // ★★★★ RFC-0112 D8 — `using <이름> <타입> .` 은 op 이 깎아 쓰는 얼로케이터를 적는 **절**이다.
                               //   arity 에 들지 않는다 — 부르는 쪽은 그것을 위치로 적지 않는다(`low_using.c`).
                               "using",
                               // ★★★★ RFC-0116 D2 B1 (소유자 결정 ⓑ, 2026-09-22) — `inplace <쓰기 입력> <읽기 입력> .` 은
                               //   «이 둘이 같은 저장소여도 된다» 는 **절**이다. 부르는 쪽 검사(E-EXCL-INPLACE)가 읽는다.
                               "inplace",
                               // ★★★★ RFC-0116 D4 I (소유자 결정, 2026-09-22) — `invalidates <입력> .` 은 «이 op 은 그 입력에서
                               //   나온 뷰를 무효로 만든다» 는 **절**이다. 무효화 추적(E-VIEW-INVALIDATED)이 읽는다.
                               "invalidates",
                               // ★★★★★ RFC-0120 (소유자 결정 2026-09-23, «저런 흡수경계가 우리한테도 필요해요») —
                               //   `absorbs machine <이름> .` 는 «처리기가 못 보는 일을 여기서 멈춘다» 는 **절**이다.
                               //   그 op 은 `unsafe` 를 자기 시그니처에 싣지 않고, 몸 안에서 <이름> 이 `cap machine` 이 된다.
                               //   값으로 치르는 대가: `reference` 와 `why` 가 함께 있어야 하고 장부에 서명이 있어야 한다.
                               "absorbs", "reference", "why",
                               // ★ RFC-0041 — 인라인 asm 도 **절**이다(ISA·피연산자·clobber·options).
                               //   그래서 능력의 이름은 `cap machine` 이다: `asm` 은 절 낱말이라
                               //   **값 자리에 못 온다.** 충돌은 발견하는 게 아니라 **설계로 없앤다.**
                               "asm",
                               // ★ RFC-0063 — `link "strlen" .` 는 **절**이다. 새 키워드 0개.
                               "link",
                               // ★ RFC-0063 §5 — `variadic .` 는 씨의 가변인자 함수를 부른다는 **절**이다.
                               "variadic",
                               // ★ RFC-0009 D6 — `schedule explore_interleavings [limit N] .` 는 **절**이다.
                               //   test 를 결정적 스케줄러의 **모든 interleaving** 에서 검증하라는 지시.
                               "schedule" };
    for (proven_size_t i = 0; i < sizeof K / sizeof K[0]; i++)
        if (proven_u8str_view_eq(v, proven_u8str_view_from_cstr(K[i]))) return true;
    return false;
}

// ── dump ──
static void low_pv(proven_u8str_view_t v) { fwrite(v.ptr, 1, v.size, stdout); }
static const char *low_closer_name(low_tok_kind_t c) {
    switch (c) { case LOW_TOK_DOT: return "."; default: return "end"; }
}
static void low_dump_node(const low_cst_t *nd, int depth) {
    if (!nd) return;
    for (int i = 0; i < depth; i++) fputs("  ", stdout);
    switch (nd->kind) {
        case LOW_CST_ATOM:
            fputs("ATOM ", stdout); low_pv(nd->tok.lex);
            if (nd->tok.kw) printf(" (kw#%d)", (int)nd->tok.kw);
            putchar('\n');
            break;
        case LOW_CST_FORM:
            printf("FORM [%s]\n", low_closer_name(nd->closer));
            for (proven_size_t i = 0; i < nd->nkids; i++) low_dump_node(nd->kids[i], depth + 1);
            break;
        case LOW_CST_BLOCK:
            printf("BLOCK\n");
            for (proven_size_t i = 0; i < nd->nkids; i++) low_dump_node(nd->kids[i], depth + 1);
            break;
        case LOW_CST_GROUP:
            printf("GROUP\n");
            for (proven_size_t i = 0; i < nd->nkids; i++) low_dump_node(nd->kids[i], depth + 1);
            break;
        case LOW_CST_ACCESS:
            fputs("ACCESS", stdout);
            for (proven_size_t i = 0; i + 1 < nd->nkids; i++)
                printf(" %s", nd->ops && nd->ops[i] == LOW_KW_IN ? "in" : "to");
            putchar('\n');
            for (proven_size_t i = 0; i < nd->nkids; i++) low_dump_node(nd->kids[i], depth + 1);
            break;
        case LOW_CST_ERROR:
            fputs("ERROR ", stdout); low_pv(nd->tok.lex); putchar('\n');
            break;
    }
}
// ── formatter (canonical source; naked↔paren round-trip, S2) ──
static void low_fmt_node(const low_cst_t *nd, bool arg);
static void low_fmt_stmt(const low_cst_t *nd);

static bool low_is_else_form(const low_cst_t *k) {
    return k && k->kind == LOW_CST_FORM && k->nkids && k->kids[0]->kind == LOW_CST_ATOM &&
           k->kids[0]->tok.kw == LOW_KW_ELSE;
}
int low_clause_rank(proven_u8str_view_t w);
int low_input_rank(const low_cst_t *f, proven_size_t at, proven_size_t end);
static bool low_fmt_clause_head(const low_cst_t *m, proven_size_t at) {
    return m && m->kind == LOW_CST_FORM && at < m->nkids && m->kids[at]->kind == LOW_CST_ATOM &&
           low_is_clause_word(m->kids[at]->tok.lex);
}
// ★★★ WO-0219 (소유자 결정 ⓑ 의 남은 반) — **trait 의 메서드 서명도 서식기가 차례를 바로잡는다.** 서명은 점마다 form 하나라
//   (`get input s self .` · `effects state .` · `output u64 .`) op 머리의 정렬(low_fmt_hdr_order)을 그대로 못 쓴다: 이름으로
//   시작하는 form 이 서명을 열고, 절 낱말로 시작하는 뒤따르는 form 들이 그 절이다. 절 단위로 같은 열쇠(입력은 단조)로
//   안정 정렬해 한 줄에 찍는다. 입력끼리는 자리를 바꾸지 않는다 — 구현의 입력 자리와 짝이기 때문이다.
static void low_fmt_trait_blk(const low_cst_t *blk) {
    fputs("do ", stdout);
    proven_size_t i = 0;
    while (i < blk->nkids) {
        const low_cst_t *m = blk->kids[i];
        if (!(m->kind == LOW_CST_FORM && m->nkids >= 2 && m->kids[0]->kind == LOW_CST_ATOM &&
              !low_is_clause_word(m->kids[0]->tok.lex) && low_fmt_clause_head(m, 1))) {
            low_fmt_stmt(m); putchar(' '); i++; continue;
        }
        proven_size_t j = i + 1;
        while (j < blk->nkids && low_fmt_clause_head(blk->kids[j], 0)) j++;
        enum { NS = 64 };
        const low_cst_t *sf[NS]; proven_size_t sat[NS]; int key[NS]; proven_size_t ns = 0;
        if (j - i > NS) { for (; i < j; i++) { low_fmt_stmt(blk->kids[i]); putchar(' '); } continue; }
        int inmax = 2;
        for (proven_size_t q = i; q < j; q++) {
            const low_cst_t *c = blk->kids[q];
            proven_size_t at = (q == i) ? 1 : 0;
            int r;
            if (low_view_eq_cstr(c->kids[at]->tok.lex, "input")) { r = low_input_rank(c, at, c->nkids); if (r < inmax) r = inmax; inmax = r; }
            else { r = low_clause_rank(c->kids[at]->tok.lex); if (r < 0) r = inmax; }
            sf[ns] = c; sat[ns] = at; key[ns] = r; ns++;
        }
        low_fmt_node(m->kids[0], true);
        for (int k = 0; k <= 16; k++)
            for (proven_size_t s = 0; s < ns; s++) {
                if (key[s] != k) continue;
                for (proven_size_t z = sat[s]; z < sf[s]->nkids; z++) { putchar(' '); low_fmt_node(sf[s]->kids[z], true); }
                fputs(" .", stdout);
            }
        putchar(' ');
        i = j;
    }
    fputs("end", stdout);
}
static void low_fmt_inner(const low_cst_t *nd) {  // emit a FORM's operands
    low_kw_t hk = (nd->nkids && nd->kids[0]->kind == LOW_CST_ATOM) ? nd->kids[0]->tok.kw : LOW_KW_NONE;
    bool after_then = false;
    // ★ X-0059 — 안에 놓인 선언(actor 몸의 `proc` 따위)도 머리 절마다 점을 찍는다. 파서가 머리의 점을 버리므로
    //   여기서 되살린다: 열린 절은 다음 절 낱말이나 몸 블록 앞에서 닫는다. (맨 위 선언은 low_fmt_decl 이 줄마다 찍는다.)
    bool decl = (hk == LOW_KW_FN || hk == LOW_KW_PROC || hk == LOW_KW_TEST), dopen = false;
    // ★ RFC-0132 P1·P2 (§8.1 점 규칙) — `for` 머리의 낱말 절(`count`·`range`·`be`·`where`·`while`·`next`)은 제 점으로 닫는다.
    //   파서가 그 점을 버리므로 여기서 되살린다: 다음 절 낱말과 몸 블록 앞에.
    bool fword = hk == LOW_KW_FOR && nd->nkids > 3 && nd->kids[2]->kind == LOW_CST_ATOM &&
                 (nd->kids[2]->tok.kw == LOW_KW_BE ||
                  (nd->kids[2]->tok.kw == LOW_KW_NONE && (low_view_eq_cstr(nd->kids[2]->tok.lex, "count") || low_view_eq_cstr(nd->kids[2]->tok.lex, "range"))));
    bool fwhere = false;
    if (hk == LOW_KW_FOR)
        for (proven_size_t i = 2; i < nd->nkids; i++)
            if (nd->kids[i]->kind == LOW_CST_ATOM && nd->kids[i]->tok.kw == LOW_KW_NONE && low_view_eq_cstr(nd->kids[i]->tok.lex, "where")) fwhere = true;
    for (proven_size_t i = 0; i < nd->nkids; i++) {
        if (i) putchar(' ');
        if (hk == LOW_KW_FOR && i >= 3) {
            const low_cst_t *k = nd->kids[i];
            bool cl = k->kind == LOW_CST_ATOM && (k->tok.kw == LOW_KW_WHILE ||
                      (k->tok.kw == LOW_KW_NONE && (low_view_eq_cstr(k->tok.lex, "where") || low_view_eq_cstr(k->tok.lex, "next"))));
            if (cl || (k->kind == LOW_CST_BLOCK && (fword || fwhere))) fputs(". ", stdout);
        }
        if (decl && i >= 2) {
            const low_cst_t *k = nd->kids[i];
            bool cw = k->kind == LOW_CST_ATOM && low_is_clause_word(k->tok.lex);
            if ((cw || k->kind == LOW_CST_BLOCK) && dopen) fputs(". ", stdout);
            if (cw) dopen = true;
        }
        // ★★ **정규화 층이 `else` 를 guard 의 자식으로 넣었다** — 그런데 표면 문법에서는
        //   `.` 이 조건을 닫고 나서 `else` 가 온다. 서식기는 **표면을 복원해야** 한다:
        //   그러지 않으면 `guard c else …` 로 찍히고, 다시 읽으면 `else` 가 **조건의 원자**로
        //   빨려 들어간다 — **서식기가 자기가 낸 것을 자기가 못 읽는다.**
        //   서식 보존 게이트가 즉시 잡았다(73 중 7 깨짐).
        if ((hk == LOW_KW_GUARD || hk == LOW_KW_LET || hk == LOW_KW_VAR) && low_is_else_form(nd->kids[i])) fputs(". ", stdout);
        if (hk == LOW_KW_IF && after_then) fputs("else ", stdout);  // re-insert dropped 'else'
        if (hk == LOW_KW_IF && after_then && nd->kids[i]->kind == LOW_CST_FORM)
            low_fmt_inner(nd->kids[i]);            // else-if: emit as statement, no parens
        else if (hk == LOW_KW_TRAIT && nd->kids[i]->kind == LOW_CST_BLOCK)
            low_fmt_trait_blk(nd->kids[i]);
        else
            low_fmt_node(nd->kids[i], true);
        if (hk == LOW_KW_IF && !after_then && nd->kids[i]->kind == LOW_CST_BLOCK) after_then = true;
    }
}
static void low_fmt_node(const low_cst_t *nd, bool arg) {
    if (!nd) return;
    switch (nd->kind) {
        case LOW_CST_ATOM:
            if (nd->tok.kind == LOW_TOK_CHAR) {
                // ★ 문자 리터럴도 **접두사를 다시 찍는다** — 안 찍으면 서식만 돌려도 타입이
                //   바뀐다(`U'한'` 이 `'한'` 이 되면 u32 가 u8 자리로 내려간다).
                if (nd->tok.aux.size) low_pv(nd->tok.aux);
                putchar('\''); low_pv(nd->tok.lex); putchar('\'');
            }
            else if (nd->tok.kind == LOW_TOK_STRING) {
                // ★★★ **접두사를 다시 찍는다** (RFC-0035 D5). 안 찍으면 `u"AB"` 가 `"AB"` 로
                //   나가고 — **서식만 돌려도 뜻이 바뀐다**(slice u16 이 slice u8 이 된다).
                //   실측으로 잡았다: 서식기 왕복 검사가 이 줄 하나 때문에 빨간불이었다.
                //   ☞ 서식기는 **자기가 낸 것을 자기가 읽을 수 있어야** 할 뿐 아니라,
                //     읽었을 때 **같은 프로그램**이어야 한다.
                if (nd->tok.aux.size) low_pv(nd->tok.aux);
                putchar('"'); low_pv(nd->tok.lex); putchar('"');
            }
            else if (nd->tok.kind == LOW_TOK_HEREDOC) {
                // ★ 종결자는 **자기 줄에** 있어야 한다. 렉서가 본문 끝의 개행 하나를 **떼므로**
                //   그냥 이어 붙이면 `…header.HEND` 가 되고 — **다시 읽을 수 없다**
                //   (E-HEREDOC-UNTERM). 서식기는 자기가 낸 것을 **자기가 읽을 수 있어야** 한다.
                // ★★ 태그도 다시 찍는다 — 같은 이유다(전엔 태그가 아무 뜻이 없어 안 드러났다).
                fputs("text ", stdout);
                if (nd->tok.aux.size) { low_pv(nd->tok.aux); putchar(' '); }
                fputs("HEND\n", stdout); low_pv(nd->tok.lex); fputs("\nHEND", stdout);
            }
            else low_pv(nd->tok.lex);
            break;
        case LOW_CST_ACCESS: break;   // ★ 더는 만들어지지 않는다 — 중위 `to`/`in` 을 없앴다
        case LOW_CST_GROUP: low_fmt_node(nd->nkids ? nd->kids[0] : NULL, arg); break;
        case LOW_CST_BLOCK:
            fputs("do ", stdout);
            for (proven_size_t i = 0; i < nd->nkids; i++) { low_fmt_stmt(nd->kids[i]); putchar(' '); }
            fputs("end", stdout);
            break;
        case LOW_CST_FORM: {
            // ★★ 머리가 `else` 인 form 은 **괄호로 싸면 안 된다.** `guard C . else do … end` 를
            //   `guard C . (else do … end) .` 로 찍고 있었고, 그러면 IR 이 자기 else 를 못 찾아
            //   **E-IR-UNSUP: guard without a matching else** 가 났다 — 서식기가 **동작을 깨뜨렸다.**
            //   ★ 왜 아무도 몰랐나: 서식 왕복 게이트가 `demo.low` 만 서식했다 —
            //     **옛 인터프리터의 두 번째 언어**였다. 게이트가 **엉뚱한 언어를 재고 있었다.**
            //     진짜 언어로 재자마자 즉시 나왔다(교훈 6: 픽스처의 모양이 감사의 시야다).
            bool is_else = nd->nkids && nd->kids[0]->kind == LOW_CST_ATOM &&
                           nd->kids[0]->tok.kw == LOW_KW_ELSE;
            // ★★★ **머리 붙은 블록도 괄호로 싸면 안 된다** — `end` 가 이미 닫는다(§2.3 R2).
            //   `make pairr do a 3 . b 9 . end` 를 `make (pairr do … end)` 로 찍고 있었고,
            //   IR 은 그것을 **"malformed make literal"** 이라 했다 — **서식이 뜻을 바꿨다.**
            //   ★ 왜 몰랐나: 서식 게이트가 **테스트된 op 의 동작**만 봤다. 그 op 들은 안 깨졌다.
            //     **def 해시 전체**를 맞대 보자 **아홉 픽스처**에서 즉시 나왔다.
            //     **"동작이 같다" 는 "뜻이 같다" 가 아니다.**
            bool blocktail = nd->nkids && nd->kids[nd->nkids - 1]->kind == LOW_CST_BLOCK;
            // ★★★★ **`spawn actor T` 도 괄호로 싸면 안 된다** (2026-08-26, F-B 작업 중 발견).
            //   `var b be a (spawn actor a) .` 는 `E-PAREN-STRAY` 다 — 안쪽에 닫개를 넣어도
            //   (`(spawn actor a .)`) 마찬가지다. `send`·일반 op 은 괄호가 된다(실측).
            //   ⇒ 이것이 "괄호로 싸면 안 되는" **세 번째** 자리다(else form · 머리 붙은 블록에 이어).
            //   ★ 왜 재파싱 실패 23 개가 이 자리에서 났나: 서식기가 몸통을 한 줄로 접을 때
            //     이 괄호가 블록 경계를 넘었다. 줄을 나눠도 괄호 자체가 남으면 여전히 깨진다 —
            //     **줄바꿈은 이 결함을 드러냈을 뿐 고치지 않았다.**
            //   ⇒ **되돌렸다**: 괄호를 빼면 `--ir --nest` 가 120 을 센다(구조가 문자열 밖으로
            //     나간다, R5 위반). 괄호를 치면 재파싱이 깨진다. **둘 다 못 지킨다** —
            //     진짜 원인은 서식기가 아니라 **렉서가 `actor` 를 블록 여는 낱말로 민다**는 것이다
            //     (`low_lex.c` ~460: struct·enum·trait·contract·actor·state). 그래서 `(spawn actor T)`
            //     의 `)` 가 블록 경계를 넘는다. 한 낱말에 두 뜻이고(선언 머리 · 타입 표지),
            //     고치려면 표면을 정해야 한다 ⇒ **소유자 판단 대기**(2026-08-26).
            // ★ **수신자 우선 머리**(`r..area x`)는 그렇게 찍어야 한다 — 나무는
            //   FORM(area, [머리, 수신자, 인자…]) 이지만 **표면은 수신자가 앞**이다.
            //   안 그러면 서식기가 `area r x` 로 찍고 — 그건 **다른 프로그램**이다
            //   (`area` 는 전역에 없다). def-해시 대조가 즉시 잡았다.
            if (nd->nkids >= 2 && nd->kids[0]->kind == LOW_CST_ATOM &&
                nd->kids[0]->tok.kind == LOW_TOK_METHOD) {
                if (arg) putchar('(');
                low_fmt_node(nd->kids[1], true);                 // 수신자
                fputs("..", stdout); low_pv(nd->kids[0]->tok.lex);
                for (proven_size_t i = 2; i < nd->nkids; i++) {
                    putchar(' '); low_fmt_node(nd->kids[i], true);
                }
                if (arg) putchar(')');
                break;
            }
            if (arg && nd->nkids > 1 && !is_else && !blocktail) {
                putchar('('); low_fmt_inner(nd); putchar(')');
            } else low_fmt_inner(nd);
            break;
        }
        default: break;
    }
}
// 블록을 **몸으로 갖는** 머리인가 — 그런 구문은 자기 블록의 `end` 에서 끝난다(C 의 `if (…) { }` 처럼).
// 블록을 품은 **값**을 쓰는 문장(`let x be lit T do … end .` · `return pipe xs do … end .`)은 아니다 — 자기 점을 찍는다.
// (X-0052, 소유자 결정 2026-09-25: `end` 는 자기 `do` 만 닫는다.)
static bool low_fmt_owns_block(const low_cst_t *nd) {
    if (!nd || nd->kind != LOW_CST_FORM || !nd->nkids || nd->kids[0]->kind != LOW_CST_ATOM) return false;
    const low_token_t *h = &nd->kids[0]->tok;
    switch (h->kw) {
        case LOW_KW_FN: case LOW_KW_PROC: case LOW_KW_TEST: case LOW_KW_ACTOR: case LOW_KW_STATE:
        case LOW_KW_CONTRACT: case LOW_KW_STRUCT: case LOW_KW_ENUM: case LOW_KW_TRAIT: case LOW_KW_MATCH:
        case LOW_KW_CASE: case LOW_KW_WHILE: case LOW_KW_IF: case LOW_KW_FOR: case LOW_KW_ELSE:
        case LOW_KW_GUARD: case LOW_KW_EXPORT: case LOW_KW_UNSAFE: case LOW_KW_EXTERN: case LOW_KW_MODULE:
            return true;
        default: break;
    }
    return low_view_eq_cstr(h->lex, "task_group") || low_view_eq_cstr(h->lex, "pipe") ||
           low_view_eq_cstr(h->lex, "region") || low_view_eq_cstr(h->lex, "borrow");
}
static void low_fmt_stmt(const low_cst_t *nd) {
    if (nd->kind == LOW_CST_FORM) {
        low_fmt_inner(nd);
        // ☞ `region r arena do …` · `borrow b be x do …` 는 블록이 마지막 자식 폼(`arena do …`) 안에 있다 — 따라 내려간다.
        const low_cst_t *tail = nd;
        while (tail && tail->kind == LOW_CST_FORM && tail->nkids) tail = tail->kids[tail->nkids - 1];
        bool blocktail = low_fmt_owns_block(nd) && tail && tail->kind == LOW_CST_BLOCK;
        // ★ `guard … else do … end` — 끝이 블록이면 닫개를 붙이지 않는다(`end .` 는 말더듬).
        if (!blocktail && low_fmt_owns_block(nd) && nd->nkids && low_is_else_form(nd->kids[nd->nkids - 1])) {
            const low_cst_t *e = nd->kids[nd->nkids - 1];
            if (e->nkids && e->kids[e->nkids - 1]->kind == LOW_CST_BLOCK) blocktail = true;
        }
        // ★ RFC-0103 ⓐ — **명시-전용으로 찍는다.** 블록으로 끝나도 닫개를 찍는다(`end .`).
        //   전에는 `end .` 를 "말더듬" 이라 보고 생략했다. 그것이 맞았던 이유는 **개행이
        //   대신 닫아 줬기** 때문이고, 그 규칙이 없어진다(§2.3 R2 폐지). 닫개가 하나뿐인
        //   언어에서 생략은 말더듬이 아니라 **빠뜨림**이다.
        //   무른 판과 def 해시가 같다는 것은 확인했다(dcc4205a28d7079c, IR 바이트 동일).
        // ★★ X-0052 (소유자 결정 2026-09-25) — `end` 는 자기 `do` 만 닫는다. 블록을 **몸으로 갖는** 구문은 그 블록에서
        //   끝나므로 닫개를 찍지 않고(찍으면 E-DOT-STRAY), 나머지 문장은 블록으로 끝나도 자기 점을 찍는다.
        if (!blocktail) fputs(" .", stdout);
    } else { low_fmt_node(nd, false); fputs(" .", stdout); }
}
// ══ F-B 정규형 — **절마다 줄바꿈** (2026-08-26 소유자 결정 · RFC-0102 §8-4) ═══════════
//
// 왜 바꿨나: 한 줄로 접는 출력은 **개행이 나르던 닫힘 정보를 버린다**(§2.3 R2 —
// 괄호 깊이 0 에서 개행이 form 을 닫는다). 그래서 서식기가 낸 것을 서식기가 못 읽는
// 파일이 코퍼스에 **23 개** 있었다(`E-PAREN-STRAY`, `usability-fmt-check`).
// 개행이 닫개인 언어에서 한 줄로 접는 것은 정보를 버리는 방향이다.
//
// ★ 절 낱말 목록을 여기서 다시 적지 않는다 — `low_is_clause_word()` 를 쓴다.
//   같은 뜻에 두 표현을 두면 그것이 갈림의 씨앗이다(교훈 7 · DECISION-0015).
//
// ★★ 판정은 **def 해시**로 한다. 서식기는 이 저장소에서 세 번 뜻을 바꾼 전력이 있고
//   (else 괄호 · 머리 붙은 블록 · 수식자 누락) 세 번 다 def 해시 대조가 잡았다.
//   "동작이 같다" 는 "뜻이 같다" 가 아니다.
static void low_fmt_ind(int depth) { for (int i = 0; i < depth; i++) fputs("  ", stdout); }

// 몸통 블록을 여러 줄로 찍는다: `do` · 문장마다 한 줄 · `end`
static void low_fmt_body(const low_cst_t *blk, int depth) {
    fputs("do\n", stdout);            // X-0052 ⓐ — `do` 뒤의 점은 빈 폼이다(E-DOT-STRAY)
    for (proven_size_t i = 0; i < blk->nkids; i++) {
        low_fmt_ind(depth + 1);
        low_fmt_stmt(blk->kids[i]);
        putchar('\n');
    }
    low_fmt_ind(depth);
    fputs("end", stdout);             // X-0052 ⓐ — `end` 가 폼을 닫는다
}

// 선언인가 — 머리가 원자이고 **마지막 자식이 블록**이며 절 낱말을 하나라도 가진 form.
static bool low_fmt_is_decl(const low_cst_t *f) {
    if (!f || f->kind != LOW_CST_FORM || f->nkids < 3) return false;
    if (f->kids[0]->kind != LOW_CST_ATOM) return false;
    if (f->kids[f->nkids - 1]->kind != LOW_CST_BLOCK) return false;
    for (proven_size_t i = 2; i + 1 < f->nkids; i++)
        if (f->kids[i]->kind == LOW_CST_ATOM && low_is_clause_word(f->kids[i]->tok.lex))
            return true;
    return false;
}

// ★★★★ WO-0217 (소유자 결정 ⓑ) — **서식기가 절의 차례를 바로잡는다.** 머리의 kids[from, to) 를 절 단위로 잘라
//   차례(low_clause_rank) 대로 **안정 정렬**한 kid 번호 열을 낸다. 입력은 **서로의 자리를 바꾸지 않는다** — 입력의 자리는
//   호출의 인자 자리라서, 옮기면 뜻이 바뀐다. 그래서 입력의 열쇠는 «지금까지 본 입력 차례의 최대» 로 둔다(단조).
//   `using` 과 입력 아닌 절만 움직인다. 입력끼리 차례를 어긴 것은 검사기(E-CLAUSE-ORDER)가 말하고 사람이 고친다.
enum { LOW_FMT_MAXSPAN = 256 };
static proven_size_t low_fmt_hdr_order(const low_cst_t *f, proven_size_t from, proven_size_t to, proven_size_t *idx) {
    proven_size_t st[LOW_FMT_MAXSPAN], en[LOW_FMT_MAXSPAN]; int key[LOW_FMT_MAXSPAN];
    proven_size_t ns = 0, i = from, n = 0;
    while (i < to && !(f->kids[i]->kind == LOW_CST_ATOM && low_is_clause_word(f->kids[i]->tok.lex))) idx[n++] = i++;
    int inmax = 2;
    while (i < to) {
        proven_size_t e = i + 1;
        while (e < to && !(f->kids[e]->kind == LOW_CST_ATOM && low_is_clause_word(f->kids[e]->tok.lex))) e++;
        if (ns == LOW_FMT_MAXSPAN) { for (proven_size_t q = from; q < to; q++) idx[q - from] = q; return to - from; }
        int r;
        if (low_view_eq_cstr(f->kids[i]->tok.lex, "input")) { r = low_input_rank(f, i, e); if (r < inmax) r = inmax; inmax = r; }
        else { r = low_clause_rank(f->kids[i]->tok.lex); if (r < 0) r = inmax; }
        st[ns] = i; en[ns] = e; key[ns] = r; ns++;
        i = e;
    }
    for (int k = 0; k <= 16; k++)
        for (proven_size_t s = 0; s < ns; s++)
            if (key[s] == k) for (proven_size_t q = st[s]; q < en[s]; q++) idx[n++] = q;
    return n;
}

// `fn f` · 절마다 한 줄(닫개 포함) — 머리의 kids[2, to). 몸 있는 op 과 몸 없는 extern 이 같이 쓴다.
static void low_fmt_decl_head(const low_cst_t *f, proven_size_t to, bool doblk) {
    low_fmt_node(f->kids[0], false);                 // 머리 (fn / proc / …)
    putchar(' ');
    low_fmt_node(f->kids[1], false);                 // 이름
    // RFC-0103 ⓐ — 머리를 개행이 닫고 있었다. X-0058 — 몸이 C 에 있는 extern 은 절을 `do … end` 에 담는다.
    fputs(doblk ? " do\n" : " .\n", stdout);
    bool open_clause = false;
    proven_size_t ord[f->nkids];
    proven_size_t nord = low_fmt_hdr_order(f, 2, to, ord);
    for (proven_size_t oi = 0; oi < nord; oi++) {
        const low_cst_t *k = f->kids[ord[oi]];
        bool starts = (k->kind == LOW_CST_ATOM && low_is_clause_word(k->tok.lex));
        if (starts) {
            if (open_clause) fputs(" .\n", stdout);  // 앞 절을 닫는다
            low_fmt_ind(1);
            open_clause = true;
        } else if (open_clause) {
            putchar(' ');
        } else {
            // 절 밖의 원자(예: 수식자 뒤 잔여) — 안전하게 같은 줄에 이어 붙인다
            putchar(' ');
        }
        low_fmt_node(k, true);
    }
    if (open_clause) fputs(" .\n", stdout);
}
// `fn f` · 절마다 한 줄(닫개 포함) · `do` … `end`
static void low_fmt_decl(const low_cst_t *f) {
    low_fmt_decl_head(f, f->nkids - 1, false);
    low_fmt_body(f->kids[f->nkids - 1], 0);
}

void low_cst_fmt(const low_parse_result_t *pr) {
    for (proven_size_t i = 0; i < pr->nforms; i++) {
        // ★★★ **수식자는 벗겨졌지만 사라진 것이 아니다.** 서식기가 그것을 안 찍어서
        //   `unsafe extern proc` 이 **평범한 proc** 으로 찍혔다 — **서식이 뜻을 바꿨다.**
        //   (def-해시 대조가 그것을 즉시 고발했다. 표시는 **찍어야** 표시다.)
        const low_cst_t *f = pr->forms[i];
        if (f) {
            if (f->is_export) fputs("export ", stdout);
            if (f->is_unsafe) fputs("unsafe ", stdout);
            // ★★★★ **네 번째 수식자.** `unsafe target x86_sse2 proc av` 가 평범한
            //   `unsafe proc` 으로 찍혀 op 이 게이트 밖으로 나왔다.
            //   export·unsafe·extern 이 같은 자리에서 같은 이유로 빠졌었고 세 번 다
            //   def 해시가 잡았다 — **이번엔 못 잡았다.** `target` 은 몸통이 아니라
            //   **게이트**라서 def 에 안 들어간다. 한 오라클이 세 번 잡았다고 네 번째도
            //   잡는 것은 아니다. 그래서 골든에 **fmt 출력을 다시 --check 하는** 검사를
            //   두었다(수식자든 무엇이든, 뜻이 바뀌면 거기서 걸린다).
            if (f->target_iset.size) {
                fputs("target ", stdout);
                fwrite(f->target_iset.ptr, 1, f->target_iset.size, stdout);
                putchar(' ');
            }
            if (f->is_extern) fputs("extern ", stdout);
        }
        if (low_fmt_is_decl(pr->forms[i])) low_fmt_decl(pr->forms[i]);
        else if (f && f->is_extern && f->kind == LOW_CST_FORM && f->nkids > 2) {
            // ★ WO-0217 — 몸 없는 extern 머리도 같은 차례로 찍는다.
            // ★★ X-0059 — 그리고 **같은 모양으로** 찍는다: 절마다 한 줄, 자기 점. 전엔 한 줄에 원자만 늘어놓아 절의 점을
            //   모두 지웠다(`… input k cap c input w i64 … link "x" . end`) — 서식기가 규범에 없는 꼴을 만들었다.
            low_fmt_decl_head(f, f->nkids, true);
            fputs("end\n", stdout);
            continue;
        }
        else low_fmt_stmt(pr->forms[i]);
        // ★★★ **`extern` 선언은 `end` 로 닫힌다** — 본문은 없지만 **끝은 있어야 한다.**
        //   없으면 절 목록이 **다음 선언의 `do` 를 삼킨다**(그리고 그 op 이 "몸이 둘" 이 된다).
        //   서식기가 그것을 안 찍어서 **서식이 뜻을 바꿨다** — def 해시가 즉시 고발했다.
        if (f && f->is_extern && !f->is_export) fputs(" end", stdout);
        putchar('\n');
    }
}

void low_cst_dump(const low_parse_result_t *pr) {
    printf("== CST (%zu top forms) ==\n", (size_t)pr->nforms);
    for (proven_size_t i = 0; i < pr->nforms; i++) low_dump_node(pr->forms[i], 0);
    if (pr->diags.len) {
        printf("== parse diagnostics (%zu) ==\n", (size_t)pr->diags.len);
        for (proven_size_t i = 0; i < pr->diags.len; i++) {
            const low_diag_t *d = PROVEN_ARRAY_GET(&pr->diags, low_diag_t, i);
            printf("  %u:%u %s: %s\n", d->line, d->col, d->code, low_diag_text(d));
        }
    }
}


// ══ 정규화된 op 헤더 — **op 의 머리는 여기서 한 번만 읽는다** (DECISION-0015) ═══
//
// ★★★★★ **2026-09-15 — 차례를 두 번 옮겼다.** 아침엔 소유자 지시로 `output` 을 맨 앞으로 옮겼고(코퍼스 이주),
//   같은 날 소유자가 되돌리며 논리적으로 다듬어 고정하라고 했다 — 아래 표 머리 주석이 그 결정이다.
//   코퍼스는 두 번 다 `lowent_lang_private/scripts/migrate-output-first.c`(`--reorder`: 이 표로 정렬)로 옮겨 썼다.
// ★★★★ **머리 절의 차례** (WO-0217 · 소유자 결정 ⓑ, 2026-09-13) — **유일한 표.** 검사기(`E-CLAUSE-ORDER`)·서식기·
//   `using` 해석이 모두 이것을 읽는다. 입력은 종류로 가른다: comptime(2) · 권한·영역(3) · 데이터(5). `using` 은 4.
//   *"op 서명의 절 순서에 대해 b로 하여 진행 바랍니다."* — 절 종류의 차례를 고정하고, 권한을 데이터보다 먼저 받는다.
//   ☞ 이 수들은 실측으로 정했다(코퍼스 op 머리 2,089): 적힌 차례가 이미 그랬다 — 어긴 곳은 19 자리였다.
int low_clause_rank(proven_u8str_view_t w) {
    static const struct { const char *w; int r; } T[] = {
        // ★★★★★ 2026-09-15 소유자 결정(두 번째) — *"satisfies 는 이름 뒤로 input이 output보다 먼저 나오게. op 선언부순서를
        //   논리적으로 서로 관계나 필요에 맞춰서 잘 다듬어 주세요. 그리고 고정해 주세요."* — 제안 차례를 골랐다:
        //   무엇인가(이름·약속) → 번역 시점 매개변수 → 권한 → 데이터 입력 → 출력 → 하는 일(효과) → 구현 방식 → 계약.
        //   앞의 것이 뒤의 것에 쓰인다(타입 매개변수가 입력·출력 타입에, 입력이 출력·계약에, 권한이 효과에).
        { "satisfies", 0 }, { "lowdoc", 0 }, { "vector", 1 }, { "priority", 1 },
        { "using", 4 }, { "output", 6 }, { "effects", 7 }, { "link", 8 }, { "variadic", 8 }, { "asm", 9 },
        { "absorbs", 9 }, { "reference", 9 }, { "why", 9 },   // ★ 흡수 경계 — 구현 방식 무리(asm 옆), RFC-0120
        { "access", 10 }, { "inplace", 10 }, { "invalidates", 10 }, { "parallel", 10 }, { "reduce", 10 },   // ★ inplace — 입력의 메모리를 어떻게 만지나(RFC-0116 D2 B1)
        { "requires", 11 }, { "ensures", 12 }, { "errors", 13 }, { "tests", 14 }, { "schedule", 15 },
    };
    for (proven_size_t i = 0; i < sizeof T / sizeof T[0]; i++)
        if (low_view_eq_cstr(w, T[i].w)) return T[i].r;
    return -1;     // `input` 은 종류가 정한다(low_input_rank)
}
int low_input_rank(const low_cst_t *f, proven_size_t at, proven_size_t end) {
    // `input [comptime] <이름> [comptime] <타입…>` — 권한(`cap …`)·영역(`region …`)은 3, comptime 은 2, 나머지 5
    proven_size_t j = at + 1;
    bool ct = false;
    while (j < end && f->kids[j]->kind == LOW_CST_ATOM && low_view_eq_cstr(f->kids[j]->tok.lex, "comptime")) { ct = true; j++; }
    j++;                                                         // 이름
    while (j < end && f->kids[j]->kind == LOW_CST_ATOM && low_view_eq_cstr(f->kids[j]->tok.lex, "comptime")) { ct = true; j++; }
    if (ct) return 2;
    while (j < end && f->kids[j]->kind == LOW_CST_ATOM &&
           (low_view_eq_cstr(f->kids[j]->tok.lex, "mut") || low_view_eq_cstr(f->kids[j]->tok.lex, "owned"))) j++;
    if (j < end && f->kids[j]->kind == LOW_CST_ATOM &&
        (low_view_eq_cstr(f->kids[j]->tok.lex, "cap") || low_view_eq_cstr(f->kids[j]->tok.lex, "region"))) return 3;
    return 5;
}

// op 의 머리는 **평평한 원자 열**이다(점-닫힘). 절은 `low_is_clause_word()` 로 잘린다.
//   proc f  input [comptime] n u8 .  input s mut slice u8 .  output u8 .  effects io .  do … end
//           └── 파라미터 ─────────┘  └── 파라미터 ────────┘  └ 출력 ─┘  └ 효과 ┘
//
// 한정자(`comptime` · `mut` · `owned`)는 **이름과 타입 사이**에 온다 — 그리고 각 소비자가
// 각자 건너뛰고 있었다. 여기서 **한 번만** 벗긴다.
low_op_header_t low_op_header(const low_cst_t *f) {
    low_op_header_t h = { 0 };
    if (!f || f->kind != LOW_CST_FORM || f->nkids < 2 || f->kids[0]->kind != LOW_CST_ATOM)
        return h;
    low_kw_t kw = f->kids[0]->tok.kw;
    if (kw != LOW_KW_FN && kw != LOW_KW_PROC) return h;
    h.form = f;
    h.is_calc = (kw == LOW_KW_FN);
    if (f->kids[1]->kind == LOW_CST_ATOM) h.name = f->kids[1]->tok.lex;
    if (f->kids[f->nkids - 1]->kind == LOW_CST_BLOCK) h.body = f->kids[f->nkids - 1];

    for (proven_size_t i = 2; i < f->nkids; i++) {
        if (f->kids[i]->kind != LOW_CST_ATOM) continue;
        proven_u8str_view_t w = f->kids[i]->tok.lex;
        if (!low_is_clause_word(w)) continue;
        // 절의 끝: 다음 절 낱말 또는 form 의 끝
        proven_size_t e = i + 1;
        while (e < f->nkids && f->kids[e]->kind == LOW_CST_ATOM &&
               !low_is_clause_word(f->kids[e]->tok.lex)) e++;

        if (low_view_eq_cstr(w, "input")) {
            if (i + 1 >= e) { i = e - 1; continue; }
            if (h.np >= LOW_HDR_MAXP) { h.too_many = true; i = e - 1; continue; }
            low_param_t *p = &h.p[h.np];
            proven_size_t j = i + 1;
            // ★ 한정자는 **이름 앞**에도 올 수 있다(`input comptime n u8 .`)
            while (j < e && f->kids[j]->kind == LOW_CST_ATOM &&
                   low_view_eq_cstr(f->kids[j]->tok.lex, "comptime")) { p->is_comptime = true; j++; }
            if (j >= e) { i = e - 1; continue; }
            p->name = f->kids[j]->tok.lex; j++;
            // ★ `comptime` 은 이름 뒤에도 올 수 있다 — **타입 밖**이므로 범위에서 뺀다.
            while (j < e && f->kids[j]->kind == LOW_CST_ATOM &&
                   low_view_eq_cstr(f->kids[j]->tok.lex, "comptime")) { p->is_comptime = true; j++; }
            p->ts = j; p->te = e;                  // 타입 = `mut`/`owned` **를 포함한** 낱말들
            // 타입의 **알맹이**는 그 한정자들도 벗긴 자리다 — 소유 검사는 이쪽이 필요하다.
            proven_size_t k2 = j;
            while (k2 < e && f->kids[k2]->kind == LOW_CST_ATOM) {
                proven_u8str_view_t m = f->kids[k2]->tok.lex;
                if      (low_view_eq_cstr(m, "mut"))        { p->is_mut = true;   k2++; }
                else if (low_view_eq_cstr(m, "owned"))      { p->is_owned = true; k2++; }
                else if (low_view_eq_cstr(m, "unsafe_ptr")) { p->is_uptr = true;  k2++; }  // ★ 생 포인터
                else break;
            }
            p->core = k2;
            h.np++;
        } else if (low_view_eq_cstr(w, "output")) {
            h.out_s = i + 1; h.out_e = e;
        } else if (low_view_eq_cstr(w, "effects")) {
            h.eff_s = i + 1; h.eff_e = e;
        } else if (low_view_eq_cstr(w, "variadic")) {
            h.is_variadic = true;   // ★ 가변인자 C 호출 (RFC-0063 §5)
        } else if (low_view_eq_cstr(w, "using")) {
            // ★★★★ RFC-0112 D8 — 얼로케이터 절. 이름과 타입만 적어 둔다(부르는 쪽 arity 는 아래서 가린다).
            if (i + 2 < e && f->kids[i + 1]->kind == LOW_CST_ATOM && f->kids[i + 2]->kind == LOW_CST_ATOM) {
                h.using_name = f->kids[i + 1]->tok.lex; h.using_type = f->kids[i + 2]->tok.lex;
            }
        }
        i = e - 1;
    }
    // ★ 부르는 쪽이 적는 인자 수: `using` 의 타입이 comptime 타입 매개변수면 그 매개변수도 적지 않는다.
    h.np_call = h.np;
    if (h.using_type.size)
        for (proven_size_t q = 0; q < h.np; q++)
            if (h.p[q].is_comptime && proven_u8str_view_eq(h.p[q].name, h.using_type)) { h.np_call--; break; }
    return h;
}

// ═══════════════════════════════════════════════════════════════════════════════
// ★★★ **arity 단일화** — 평평한 원자 열에 괄호를 박아 **진짜 나무**를 세운다.
//
//   파서는 그대로 무지하다(구조=문자열, RFC-0046). 나무는 **파스 다음에** 선다.
//   전엔 이 나무가 `low_ir.c` 안에서만 섰고 — 그것도 나무로 남지 않고 **곧장 스택 IR 로
//   증발했다.** 그래서 나머지 다섯 소비자는 평평한 열을 **각자 짐작**했다(raw kids[] 883회).
//
//   ★ 나무를 세우는 것은 곧 **괄호를 박는 것**이다. 언어에 이미 GROUP 이 있고 IR 이 그것을
//     이해한다. `add a mul b 2` → `add a (mul b 2)`. **뜻이 같다.**
//
//   ⇒ 그래서 **대조자가 공짜로 있다**: 내용주소화(RFC-0012). 괄호를 박기 전과 후의
//     **def 해시가 한 비트라도 다르면 나무가 틀린 것이다.** 73개 픽스처 전부에서 잰다.
//
//   ★ 그리고 **모르면 포기한다.** 불규칙한 머리(`view pt data` — `pt` 는 값이 아니라 타입)를
//     만나면 그 자리는 **평평하게 남긴다.** 조용히 틀린 나무를 세우느니 **안 세우는 게 낫다.**

// 머리의 **모양** — "VV" · "WV" · "VW" … (V=값, W=낱말). 보통 op 은 전부 V 다.
typedef struct { const char *name; const char *shape; } nest_head_t;
static const nest_head_t NEST_SHAPE[] = {
#define X(n, w, a) { #n, (a) == 1 ? "V" : (a) == 2 ? "VV" : (a) == 3 ? "VVV" : "" },
    // ★ **핵심 표만** 본다 (RFC-0125). 계산 잎 열다섯은 `call_builtin <이름> …` 로 오므로,
    //   여기서 그 이름에 모양을 주면 중첩기가 `sha256 s o` 를 **한 마디로 묶어 버리고**
    //   머리 푸는 자리가 이름을 못 본다. 그 자리는 평평하게 두고 하강이 읽는다(RFC-0046).
    LOW_BUILTINS_CORE(X)
#undef X
#define X(n, sh) { #n, sh },
    LOW_SHAPES(X)
#undef X
};
// ★ 키워드 머리 중에도 **모양이 하나인 것**이 있다.
//   `make T do…end` = 타입 낱말 + 블록 · `expr a + b` = **나머지 전부**(중위 섬).
//   R = 구간의 **나머지 전부**를 먹는다.
static const nest_head_t NEST_KW_SHAPE[] = {
    // ★ `make pairr do…end` — 파서가 `pairr do…end` 를 **머리 붙은 블록 form** 으로 이미
    //   묶어 놓는다(§2.3 R2). 그래서 make 가 먹는 것은 **낱말+블록이 아니라 값 하나**다.
    //   (처음엔 "WV" 라 적었고, 낱말 슬롯이 FORM 을 만나 포기했다 — **파서가 이미 세운
    //    나무를 내가 몰랐다.**)
    // ★ `lit` 는 타입 낱말 **여럿** 뒤에 블록이 올 수 있다(제네릭 `lit handle ga do … end` — 블록은 마지막 낱말에
    //   붙는다). 전엔 "V"(값 하나)라 `(lit handle) ga do …` 로 묶여 서식이 뜻을 바꿨다(옛 `make` 도 같았다, 2026-09-29).
    { "lit", "L" },        // RFC-0132 L1 (전 `make`)
    { "expr", "R" },       // 중위 섬 — 나머지 전부가 이 섬이다
    // ★ 아래 넷은 **아직 안을 모른다**(가변 모양: 선택적 표식·핸들러 arity).
    //   그렇다고 **구간 전체를 버릴 이유는 없다** — 이 자리만 평평하게 남기면 된다.
    //   R = "나머지를 그대로" ⇒ 뜻이 안 바뀐다(해시가 증명한다). **이 넷을 제대로 모델링하는
    //   것이 남은 빚이다** — 그러면 R 이 사라진다.
    { "try", "VR" },       // try <값> else_none | else_error <변형>
    { "send", "R" },       // send <액터> <메시지> <인자…> — arity 가 핸들러마다 다르다
    { "spawn", "R" },      // spawn [actor] <이름> — `actor` 표식이 선택적이다
};
// `stack_new R capacity n` — `capacity` 표식이 선택적이라 모양이 하나가 아니다. 나머지를 그대로.
static const nest_head_t NEST_OPAQUE_R[] = { { "stack_new", "R" } };

#define NEST_MAXOPS 256
typedef struct {
    low_parser_t        p;
    proven_u8str_view_t op[NEST_MAXOPS];    // 선언된 op 이름
    // ★★★ **어느 모듈이 선언했는가** (2026-07-26). 없으면 같은 이름의 op 이 두 모듈에 있을 때
    //   한정 호출(`vecgen.open`)이 **아무거나** 먼저 맞는 것으로 arity 를 잡는다 — 그러면 인자가
    //   덜 묶이고 `E-IR-ARITY: 인자가 너무 적다` 가 뜬다. 진단은 프로그램을 탓하지만 틀린 것은
    //   도구다. 자리(선언 순서)가 곧 모듈 소속이라 그것을 그대로 기록한다.
    proven_u8str_view_t opmod[NEST_MAXOPS]; // 그 op 을 선언한 모듈
    proven_size_t       ar[NEST_MAXOPS];    // 그 arity (low_op_header — 이미 IR 과 대조됐다)
    bool                var[NEST_MAXOPS];   // ★ 가변인자 op 인가 — 모양에 'R'(나머지 전부)을 더한다
    proven_size_t       nop;
    // ★★★★★ **맨이름도 제 모듈부터** (2026-08-15). 한정 호출은 2026-07-26 에 갈랐는데
    //   **맨이름**은 여전히 *첫 일치*였다. 그래서 `str.low` 의 액터 핸들러 `next`(인자 0→
    //   슬롯 1)가 `utf8.next`(인자 2)의 **arity 를 대신 정해** 폼이 한 자리 짧게 묶였고,
    //   그 뒤 IR 이 *"인자가 너무 적다"* 고 **프로그램을 탓했다**(원장의 아픈 쌍 셋).
    //   ⇒ 파싱 중인 자리의 모듈을 들고 다니며 **제 모듈의 op 을 먼저** 본다.
    // ★★★★★ **지역 이름은 남의 모듈 op 이 아니다** (2026-08-29, spill 30).
    //   되짚기(제 모듈에 없으면 아무 모듈의 동명 op)가 **지역 변수를 훔쳤다**:
    //     tls13:  `let total be u64 … .` · `guard ge (len info) total . else return 0 .`
    //     segview: `export fn total …`(arity 1)
    //   ⇒ `total` 이 **호출로 묶여 `else` 를 삼켰고**, 검사층은 *"guard 의 else 가 안 떠난다"*
    //     (`E-GUARD-FALLTHROUGH`) 라고 **엉뚱한 것을 탓했다**. 이분해서 찾은 자리다.
    //   ☞ 되짚기 자체는 **제네릭 인스턴스가 쓴다**(없애면 코퍼스 14 개가 깨진다 — 실측).
    //     그래서 없애지 않고, **제 모듈의 지역 이름일 때만** 안 되짚는다.
    proven_u8str_view_t loc[NEST_MAXOPS];   // 지역으로 묶인 이름
    proven_u8str_view_t locmod[NEST_MAXOPS];// 그 지역이 사는 모듈
    proven_size_t       nloc;
    proven_u8str_view_t curmod;             // 지금 묶고 있는 자리가 속한 모듈
    proven_size_t       nested;             // ★ 정직하게 센다: 괄호를 몇 개 박았나
    proven_size_t       gave_up;            // ★ 그리고 몇 번 **포기**했나
    // ★★ 그리고 **무엇 때문에** 포기했는지 말한다. 숫자만 있고 이유가 없으면 줄일 수 없다.
    proven_u8str_view_t why[16];
    proven_size_t       whyn[16], nwhy;
} nest_ctx_t;

static void nest_blame(nest_ctx_t *c, proven_u8str_view_t w) {
    for (proven_size_t i = 0; i < c->nwhy; i++)
        if (proven_u8str_view_eq(c->why[i], w)) { c->whyn[i]++; return; }
    if (c->nwhy < 16) { c->why[c->nwhy] = w; c->whyn[c->nwhy++] = 1; }
}

static bool nest_veq(proven_u8str_view_t v, const char *s) {
    proven_size_t n = 0; while (s[n]) n++;
    return v.size == n && memcmp(v.ptr, s, n) == 0;
}
// ★ 한정 op 머리 `M.op`(M=선언된 모듈, 점 하나) → bare `op`. arity 정규화는 파서 단계라
//   narrow 전에 돌아, 한정 호출(`gen.vp`)의 arity 를 찾으려면 스스로 접두 모듈을 벗겨야 한다.
//   벗기지 못하면 잎으로 취급돼 호출이 **평평하게** 남고, 이후 mono·IR 이 머리를 못 본다 (RFC-0011).
static proven_u8str_view_t nest_strip_mod(nest_ctx_t *c, proven_u8str_view_t v) {
    proven_size_t dot = v.size, ndot = 0;
    for (proven_size_t i = 0; i < v.size; i++)
        if (v.ptr[i] == (proven_u8)'.') { if (dot == v.size) dot = i; ndot++; }
    if (ndot != 1 || dot == 0 || dot + 1 >= v.size) return v;
    proven_u8str_view_t head = { .ptr = v.ptr, .size = dot };
    const low_parse_result_t *pr = c->p.out;
    for (proven_size_t i = 0; i < pr->nforms; i++) {
        const low_cst_t *f = pr->forms[i];
        if (f->kind == LOW_CST_FORM && f->nkids >= 2 && f->kids[0]->kind == LOW_CST_ATOM &&
            f->kids[0]->tok.kw == LOW_KW_MODULE && f->kids[1]->kind == LOW_CST_ATOM &&
            proven_u8str_view_eq(f->kids[1]->tok.lex, head))
            return (proven_u8str_view_t){ .ptr = v.ptr + dot + 1, .size = v.size - dot - 1 };
    }
    return v;
}
// 이 이름의 **모양**은 무엇인가. 잎이면 "" (빈 모양), **모양을 모르면 NULL(포기)**.
static const char *nest_shape(nest_ctx_t *c, proven_u8str_view_t v) {
    static char buf[LOW_HDR_MAXP + 1];
    for (proven_size_t i = 0; i < sizeof(NEST_OPAQUE_R)/sizeof(*NEST_OPAQUE_R); i++)
        if (nest_veq(v, NEST_OPAQUE_R[i].name)) return NEST_OPAQUE_R[i].shape;
    for (proven_size_t i = 0; i < sizeof(NEST_SHAPE)/sizeof(*NEST_SHAPE); i++)
        if (nest_veq(v, NEST_SHAPE[i].name)) return NEST_SHAPE[i].shape;
    proven_u8str_view_t vb = nest_strip_mod(c, v);        // ★ `M.op` → `op` (M=선언 모듈)
    // ★ 한정 호출이면 **그 모듈의 op 을 먼저** 찾는다 — 이름만 맞는 남의 op 이 아니라.
    proven_u8str_view_t qmod = { 0 };
    if (vb.size < v.size) qmod = (proven_u8str_view_t){ .ptr = v.ptr, .size = v.size - vb.size - 1 };
    proven_size_t hit = c->nop;
    if (qmod.size)
        for (proven_size_t i = 0; i < c->nop; i++)
            if (proven_u8str_view_eq(c->op[i], vb) && proven_u8str_view_eq(c->opmod[i], qmod)) { hit = i; break; }
    if (hit < c->nop) {
        proven_size_t n = c->ar[hit] > LOW_HDR_MAXP ? LOW_HDR_MAXP : c->ar[hit];
        for (proven_size_t q = 0; q < n; q++) buf[q] = 'V';
        if (c->var[hit] && n < LOW_HDR_MAXP) { buf[n] = 'R'; buf[n + 1] = 0; return buf; }
        buf[n] = 0;
        return buf;
    }
    if (c->curmod.size)                                   // ★ 맨이름 — 제 모듈의 op 을 먼저
        for (proven_size_t i = 0; i < c->nop; i++)
            if (proven_u8str_view_eq(c->op[i], vb) && c->opmod[i].size &&
                proven_u8str_view_eq(c->opmod[i], c->curmod)) { hit = i; break; }
    if (hit < c->nop) {
        proven_size_t n = c->ar[hit] > LOW_HDR_MAXP ? LOW_HDR_MAXP : c->ar[hit];
        for (proven_size_t q = 0; q < n; q++) buf[q] = 'V';
        if (c->var[hit] && n < LOW_HDR_MAXP) { buf[n] = 'R'; buf[n + 1] = 0; return buf; }
        buf[n] = 0;
        return buf;
    }
    // ★★★★★ **제 모듈에 없는 맨이름은 남의 모듈에서 집지 않는다** (2026-08-29, spill 30).
    //   2026-08-15 에 *"맨이름도 제 모듈부터"* 를 넣었지만 **되짚기가 남아 있었다** — 제 모듈에
    //   없으면 아무 모듈의 동명 op 이나 집었다. 그래서 **지역 변수**가 남의 export 로 묶였다:
    //     tls13:  `let total be u64 … .`  ·  `guard ge (len info) total . else return 0 .`
    //     segview: `export fn total …`(arity 1)
    //   ⇒ `total` 이 호출로 묶여 **`else` 를 삼켰고**, 검사층은 *"guard 의 else 가 안 떠난다"*
    //     (`E-GUARD-FALLTHROUGH`)고 **엉뚱한 것을 탓했다**. 실측으로 이분해 찾은 자리다.
    //   ☞ 맨이름으로 남의 모듈 op 을 부르는 길은 애초에 없다(한정 이름 `mod.op` 으로 쓴다).
    //     그러니 되짚기는 얻는 것 없이 **지역 이름을 훔치기만** 했다.
    // ★ 되짚기 전에 **이 모듈의 지역 이름인가**를 묻는다(위 주석 참조).
    if (c->curmod.size)
        for (proven_size_t i = 0; i < c->nloc; i++)
            if (proven_u8str_view_eq(c->loc[i], vb) &&
                proven_u8str_view_eq(c->locmod[i], c->curmod)) return "";   // 잎이다 — 호출이 아니다
    for (proven_size_t i = 0; i < c->nop; i++)
        if (proven_u8str_view_eq(c->op[i], vb)) {         // 선언된 op — 인자는 전부 값이다
            proven_size_t n = c->ar[i] > LOW_HDR_MAXP ? LOW_HDR_MAXP : c->ar[i];
            for (proven_size_t q = 0; q < n; q++) buf[q] = 'V';
            // ★ 가변인자 op — 고정 인자 뒤의 **나머지 전부**를 인자로 잡는다(R). 씨의 printf 류.
            if (c->var[i] && n < LOW_HDR_MAXP) { buf[n] = 'R'; buf[n + 1] = 0; return buf; }
            buf[n] = 0;
            return buf;
        }
    return "";   // 잎 — 지역·파라미터·리터럴·enum 변형·상수·타입 이름
}

// 한 값을 읽는다. 실패하면 `*bad = true` 이고 호출자가 **전부 포기한다**.
static low_cst_t *nest_value(nest_ctx_t *c, low_cst_t *const *k, proven_size_t *pos,
                             proven_size_t end, bool *bad) {
    if (*pos >= end) { *bad = true; nest_blame(c, (proven_u8str_view_t){ .ptr = (const proven_u8 *)"<피연산자부족>", .size = 20 }); return NULL; }
    low_cst_t *nd = k[(*pos)++];
    if (nd->kind != LOW_CST_ATOM) return nd;              // GROUP/BLOCK/FORM — 이미 닫힌 것
    if (nd->tok.kind == LOW_TOK_NUMBER || nd->tok.kind == LOW_TOK_STRING ||
        nd->tok.kind == LOW_TOK_CHAR ||
        nd->tok.kind == LOW_TOK_HEREDOC) return nd;                    // 리터럴 = 잎
    if (nd->tok.kind != LOW_TOK_IDENT) { *bad = true; nest_blame(c, nd->tok.lex); return NULL; }
    // ★ `true`/`false` 는 **값**이다 — 키워드라는 이유로 포기하고 있었다.
    //   ★★ `none` 도 같다(2026-08-19). `return none .` 한 줄이 있으면 그 구간을 평평하게
    //     남기고 `blame: none` 을 찍었다 — 뜻은 안 바뀌지만(해시가 증명한다) **나무가 덜 선다**.
    //     찾은 경위: 트레이트에 `grow` 를 더하며 픽스처에 `return none .` 을 썼더니 골든
    //     래칫(`nest gives up <= 2`)이 3 으로 울었고, **래칫을 풀지 않고** 원인을 적어 두었다.
    //     ☞ `none` 은 인자 0 인 **값**이다(`LOW_KW_NONEVAL` — 0 인 sentinel `LOW_KW_NONE` 과
    //       다른 것이다). `some x` 는 단항이고 그쪽은 이 표가 이미 안다.
    if (nd->tok.kw == LOW_KW_TRUE || nd->tok.kw == LOW_KW_FALSE ||
        nd->tok.kw == LOW_KW_NONEVAL) return nd;
    const char *sh = NULL;
    if (nd->tok.kw != LOW_KW_NONE) {
        for (proven_size_t i = 0; i < sizeof(NEST_KW_SHAPE)/sizeof(*NEST_KW_SHAPE); i++)
            if (nest_veq(nd->tok.lex, NEST_KW_SHAPE[i].name)) { sh = NEST_KW_SHAPE[i].shape; break; }
        if (!sh) { *bad = true; nest_blame(c, nd->tok.lex); return NULL; }   // 모양을 모르는 키워드 머리
    } else {
        sh = nest_shape(c, nd->tok.lex);
        if (!sh) { *bad = true; nest_blame(c, nd->tok.lex); return NULL; }
    }
    if (!*sh) return nd;                                               // 잎

    proven_size_t ns = 0; while (sh[ns]) ns++;
    proven_array_t kids = PROVEN_ARRAY_INIT(c->p.work, low_cst_t *, ns + 1).value;
    (void)PROVEN_ARRAY_PUSH(&kids, low_cst_t *, nd);                   // 머리도 자식이다(파서와 같은 모양)
    char shbuf[LOW_HDR_MAXP + 1];
    for (proven_size_t q = 0; q <= ns && q <= LOW_HDR_MAXP; q++) shbuf[q] = sh[q];  // buf 재사용 방지
    for (proven_size_t i = 0; i < ns; i++) {
        low_cst_t *arg = NULL;
        if (shbuf[i] == 'R') {   // ★ **나머지 전부** — 중위 섬(expr). 안은 건드리지 않는다.
            while (*pos < end) (void)PROVEN_ARRAY_PUSH(&kids, low_cst_t *, k[(*pos)++]);
            break;
        }
        if (shbuf[i] == 'L') {   // ★ **리터럴 몸** — 타입 낱말들 + 블록 붙은 폼(`handle ga do … end`). 없으면 값 하나.
            proven_size_t j = *pos;
            while (j < end && k[j]->kind == LOW_CST_ATOM) j++;
            if (j > *pos && j < end && k[j]->kind == LOW_CST_FORM && k[j]->nkids >= 2 &&
                k[j]->kids[k[j]->nkids - 1]->kind == LOW_CST_BLOCK) {
                // 낱말들 + 블록 폼을 **값 하나**로 묶는다 — `(handle (ga do … end))` 가 아니라 `(handle ga-블록)`:
                //   타입 인자가 제대로 잡혔을 때 V 가 세우는 나무와 같은 모양(단형화가 `handle ga` 를 인스턴스로 바꾼다).
                // ★ 모양은 단형화가 이어 붙인 것과 **같게** — FORM[타입, 인자…, 마지막 인자, BLOCK](mono_make_fix 의 ⓐ 결과).
                //   `synth` 표시가 «이미 이어 붙였다» 는 뜻이라 단형화가 다시 손대지 않고, 뒤에서 타입 자리만 인스턴스로 바꾼다.
                proven_array_t in = PROVEN_ARRAY_INIT(c->p.work, low_cst_t *, j - *pos + 2).value;
                const low_token_t t0 = k[*pos]->tok;
                while (*pos < j) (void)PROVEN_ARRAY_PUSH(&in, low_cst_t *, k[(*pos)++]);
                for (proven_size_t q = 0; q < k[j]->nkids; q++) (void)PROVEN_ARRAY_PUSH(&in, low_cst_t *, k[j]->kids[q]);
                (*pos)++;
                low_cst_t *ff = low_node(&c->p, LOW_CST_FORM, t0);
                if (!ff) { proven_array_destroy(&in); *bad = true; proven_array_destroy(&kids); return NULL; }
                ff->closer = LOW_TOK_EOF; ff->synth = true;
                low_take_kids(&c->p, ff, &in);
                arg = ff;
            } else
            arg = nest_value(c, k, pos, end, bad);
        } else if (shbuf[i] == 'W') {   // ★ **낱말 슬롯** — 맨 원자 하나를 그대로. 적용이 아니다.
            if (*pos >= end || k[*pos]->kind != LOW_CST_ATOM) {
                *bad = true;
                nest_blame(c, (proven_u8str_view_t){ .ptr = (const proven_u8 *)"<낱말슬롯>", .size = 14 });
            }
            else arg = k[(*pos)++];
        } else {
            arg = nest_value(c, k, pos, end, bad);
        }
        if (*bad) { proven_array_destroy(&kids); return NULL; }
        (void)PROVEN_ARRAY_PUSH(&kids, low_cst_t *, arg);
    }
    low_cst_t *f = low_node(&c->p, LOW_CST_FORM, nd->tok);
    low_cst_t *g = low_node(&c->p, LOW_CST_GROUP, nd->tok);
    if (!f || !g) { proven_array_destroy(&kids); *bad = true; return NULL; }
    f->closer = LOW_TOK_EOF;
    f->synth = g->synth = true;        // ★ 정규화가 만든 괄호다 — 저자의 것과 구별한다
    low_take_kids(&c->p, f, &kids);
    proven_array_t one = PROVEN_ARRAY_INIT(c->p.work, low_cst_t *, 1).value;
    (void)PROVEN_ARRAY_PUSH(&one, low_cst_t *, f);
    low_take_kids(&c->p, g, &one);
    c->nested++;
    return g;
}

// [vs, ve) 구간을 나무로 만든다. 하나라도 어긋나면 **구간 전체를 그대로 둔다.**
static bool nest_region(nest_ctx_t *c, low_cst_t *f, proven_size_t vs, proven_size_t ve) {
    if (ve <= vs) return false;
    proven_array_t out = PROVEN_ARRAY_INIT(c->p.work, low_cst_t *, f->nkids).value;
    for (proven_size_t i = 0; i < vs; i++) (void)PROVEN_ARRAY_PUSH(&out, low_cst_t *, f->kids[i]);
    bool bad = false;
    proven_size_t pos = vs, before = c->nested;
    while (pos < ve && !bad) {
        // ★★★ **`order <낱말>` 은 절이다 — 그 낱말은 머리가 아니다** (RFC-0018 §6.1).
        //   원자 연산의 ordering 은 새 키워드를 안 늘리려고 **절 낱말**로 준다
        //   (`atomic_load s i order acquire`). 그런데 정규화기는 원자를 보면 모양표를
        //   찾으므로, 단위 안에 `acquire` 라는 **이름의 op** 이 있으면 그것을 머리로 읽고
        //   피연산자를 찾다가 **포기한다**(구간이 flat 으로 남는다).
        //   ⇒ `lib/pool.low`(export `release`) + `lib/spsc.low` 가 정확히 그 자리였다.
        //   ★ 같은 병을 효과 검사기(low_check.c walk_effects)에서 먼저 고쳤다. **문법 자리를
        //     아는 곳이 두 군데면 두 군데 다 알아야 한다** — 한 곳만 고치면 나머지가 남는다.
        //   ☞ 포기는 *틀린 나무보다 낫다*(안전한 실패)였지만, 그래도 구간이 안 묶이면
        //     아래층 검사가 그만큼 덜 본다. 절은 절로 읽는다.
        if (f->kids[pos]->kind == LOW_CST_ATOM &&
            f->kids[pos]->tok.kw == LOW_KW_NONE &&
            nest_veq(f->kids[pos]->tok.lex, "order") &&
            pos + 1 < ve && f->kids[pos + 1]->kind == LOW_CST_ATOM) {
            (void)PROVEN_ARRAY_PUSH(&out, low_cst_t *, f->kids[pos]);       // `order`
            (void)PROVEN_ARRAY_PUSH(&out, low_cst_t *, f->kids[pos + 1]);   // 순서 낱말 그대로
            pos += 2;
            continue;
        }
        low_cst_t *v = nest_value(c, f->kids, &pos, ve, &bad);
        if (!bad) (void)PROVEN_ARRAY_PUSH(&out, low_cst_t *, v);
    }
    if (bad || c->nested == before) {          // 포기했거나, 바꿀 것이 없었다
        if (bad) c->gave_up++;
        c->nested = before;
        proven_array_destroy(&out);
        return false;
    }
    for (proven_size_t i = ve; i < f->nkids; i++) (void)PROVEN_ARRAY_PUSH(&out, low_cst_t *, f->kids[i]);
    (void)low_refit(&c->p, f, (low_cst_t **)out.data, out.len);
    proven_array_destroy(&out);
    return true;
}

// 이 form 의 **값 구간**은 어디인가. 모르면 잡지 않는다.
// ★ 이 표가 곧 "문장의 어디부터가 값인가" 에 대한 **유일한 답**이 되어 간다.
static bool nest_region_of(const low_cst_t *f, proven_size_t *vs, proven_size_t *ve) {
    if (f->kind != LOW_CST_FORM || !f->nkids || f->kids[0]->kind != LOW_CST_ATOM) return false;
    proven_size_t n = f->nkids;
    while (n > 0 && f->kids[n-1]->kind == LOW_CST_BLOCK) n--;     // 본문 블록은 값이 아니다
    // ★ RFC-0135 S1 — 끝에 붙은 `else` 폼(guard · 바인딩 else)도 값이 아니다 — 값의 인자로 삼키면 안 된다
    if (n > 1 && f->kids[n-1]->kind == LOW_CST_FORM && f->kids[n-1]->nkids && f->kids[n-1]->kids[0]->kind == LOW_CST_ATOM &&
        f->kids[n-1]->kids[0]->tok.kw == LOW_KW_ELSE) n--;
    switch (f->kids[0]->tok.kw) {
        case LOW_KW_RETURN: case LOW_KW_IF: case LOW_KW_WHILE: case LOW_KW_GUARD:
            *vs = 1; *ve = n; return n > 1;
        case LOW_KW_SET:                       // set <place> <value…> — place 는 값이 아니다
            *vs = 2; *ve = n; return n > 2;
        case LOW_KW_VAR: case LOW_KW_LET: {    // … be <value…>
            for (proven_size_t i = 1; i < n; i++)
                if (f->kids[i]->kind == LOW_CST_ATOM && f->kids[i]->tok.kw == LOW_KW_BE) {
                    *vs = i + 1; *ve = n; return n > i + 1;
                }
            return false;
        }
        default: return false;                 // 나머지는 **모른다** — 건드리지 않는다
    }
}

static void nest_walk(nest_ctx_t *c, low_cst_t *nd) {
    if (!nd) return;
    if (nd->kind == LOW_CST_FORM) {
        proven_size_t vs, ve;
        if (nest_region_of(nd, &vs, &ve)) nest_region(c, nd, vs, ve);
    }
    for (proven_size_t i = 0; i < nd->nkids; i++) nest_walk(c, nd->kids[i]);
}

void low_nest_blame(const low_cst_t *unused, char *buf, proven_size_t cap);   // (아래 정의)

static nest_ctx_t g_last;   // ★ 마지막 실행의 이유표 — `--nest` 가 사람에게 말해 준다

void low_nest_report(char *buf, proven_size_t cap) {
    proven_size_t o = 0;
    for (proven_size_t i = 0; i < g_last.nwhy && o + 32 < cap; i++) {
        if (o) buf[o++] = ' ';
        for (proven_size_t q = 0; q < g_last.why[i].size && o + 16 < cap; q++)
            buf[o++] = (char)g_last.why[i].ptr[q];
        int k = snprintf(buf + o, cap - o, "(%zu)", (size_t)g_last.whyn[i]);
        if (k > 0) o += (proven_size_t)k;
    }
    buf[o < cap ? o : cap - 1] = 0;
}

// ★ `let`/`var`/`input` 이 만든 이름을 모은다 — 되짚기가 그것을 훔치지 않게(spill 30).
//   문법이 단순해서 훑기도 단순하다: 그 낱말 **바로 다음 원자**가 이름이다.
static void nest_collect_locals(nest_ctx_t *c, const low_cst_t *nd, proven_u8str_view_t mod) {
    if (!nd) return;
    if (nd->kind == LOW_CST_FORM || nd->kind == LOW_CST_BLOCK || nd->kind == LOW_CST_GROUP) {
        for (proven_size_t i = 0; i + 1 < nd->nkids; i++) {
            const low_cst_t *a = nd->kids[i];
            if (a->kind != LOW_CST_ATOM) continue;
            low_kw_t kw = a->tok.kw;
            if (kw != LOW_KW_LET && kw != LOW_KW_VAR && !proven_u8str_view_eq(a->tok.lex, proven_u8str_view_from_cstr("input"))) continue;
            const low_cst_t *nm = nd->kids[i + 1];
            if (nm->kind != LOW_CST_ATOM || !nm->tok.lex.size) continue;
            if (c->nloc >= NEST_MAXOPS) continue;   // ★ 넘치면 **더 안 담을 뿐** 답은 안 바꾼다
            c->loc[c->nloc] = nm->tok.lex; c->locmod[c->nloc] = mod; c->nloc++;
        }
        for (proven_size_t i = 0; i < nd->nkids; i++) nest_collect_locals(c, nd->kids[i], mod);
    }
}

// ★★★★★ **필드 이름도 잎이다** (2026-08-30, WO-0149 — 실물 프로그램이 찾았다).
//   `segarena` 의 `struct arena` 에 필드 `used` 가 있고, `alloc` 에 `proc used`(effects state)
//   가 있다. 그 둘이 한 단위에 서자 `make arena … used 0 …` 의 **필드 이름**이 남의 모듈
//   op 으로 잡혀, segarena 의 순수한 `fn open` 이 *"효과를 수행한다"*(E-EFFECT-CALC) 고
//   거절됐다 — **엉뚱한 모듈, 엉뚱한 이유**. 지역 이름과 **같은 뿌리**이고 같은 처방이다:
//   제 모듈의 필드 이름이면 되짚기가 그것을 훔치지 못한다.
//   ☞ 같은 뿌리를 고친 여섯 번째 자리다(하강 · 효과조회 · 단형화기 · 파서 arity 표 ·
//     타입검사 sig_find · 그리고 여기).
static void nest_collect_fields(nest_ctx_t *c, const low_cst_t *nd, proven_u8str_view_t mod) {
    if (!nd) return;
    for (proven_size_t i = 0; i < nd->nkids; i++) {
        const low_cst_t *b = nd->kids[i];
        if (b->kind != LOW_CST_BLOCK) { nest_collect_fields(c, b, mod); continue; }
        // 블록 안의 각 절은 `<이름> <타입…> .` — 첫 원자가 필드 이름이다.
        for (proven_size_t q = 0; q < b->nkids; q++) {
            const low_cst_t *f = b->kids[q];
            const low_cst_t *nm = (f->kind == LOW_CST_FORM && f->nkids && f->kids[0]->kind == LOW_CST_ATOM)
                                  ? f->kids[0] : (f->kind == LOW_CST_ATOM ? f : NULL);
            if (!nm || !nm->tok.lex.size) continue;
            if (nm->tok.kw) continue;                 // `input comptime b type .` 같은 절은 아니다
            if (c->nloc >= NEST_MAXOPS) continue;
            c->loc[c->nloc] = nm->tok.lex; c->locmod[c->nloc] = mod; c->nloc++;
        }
    }
}

static proven_size_t g_report_limit;   // 0 = 단위 전체
void low_nest_set_report_limit(proven_size_t n) { g_report_limit = n; }

void low_nest(low_parse_result_t *pr, proven_allocator_t node_alloc, proven_allocator_t work,
              proven_size_t *nested, proven_size_t *gave_up) {
    nest_ctx_t c = { .p = { .node_alloc = node_alloc, .work = work, .out = pr } };
    // Γ — 선언된 op 의 arity. `low_op_header()` 가 그 유일한 답이고, 이미 IR 과 대조됐다.
    proven_u8str_view_t curmod = { 0 };            // ★ 자리가 곧 모듈 소속이다
    for (proven_size_t i = 0; i < pr->nforms; i++) {
        const low_cst_t *f = pr->forms[i];
        if (f->kind != LOW_CST_FORM || !f->nkids || f->kids[0]->kind != LOW_CST_ATOM) continue;
        low_kw_t kw = f->kids[0]->tok.kw;
        if (kw == LOW_KW_MODULE && f->nkids >= 2 && f->kids[1]->kind == LOW_CST_ATOM) {
            curmod = f->kids[1]->tok.lex;
            continue;
        }
        if (kw == LOW_KW_STRUCT) {
            nest_collect_fields(&c, f, curmod);      // ★ 이 struct 가 만든 필드 이름들
        }
        if (kw == LOW_KW_FN || kw == LOW_KW_PROC) {
            nest_collect_locals(&c, f, curmod);      // ★ 이 op 이 만든 지역 이름들
            low_op_header_t h = low_op_header(f);
            if (h.name.size && c.nop < NEST_MAXOPS) { c.op[c.nop] = h.name; c.opmod[c.nop] = curmod; c.var[c.nop] = h.is_variadic; c.ar[c.nop++] = h.np_call; }
        } else if (kw == LOW_KW_ACTOR) {       // 액터 핸들러도 op 이다 — 슬롯 0 = 인스턴스
            // ★★★★★ **액터 핸들러의 지역 이름도 모은다** (2026-08-30, WO-0149).
            //   여태 위의 `fn`/`proc` 만 모았다 — 그래서 `alloc.low` 의 액터 안 `var at u64`
            //   가 목록에 없었고, `segarena` 의 `export proc at` 이 그 자리를 **집었다**.
            //   결과: segarena 의 순수한 `fn open` 이 *"효과를 수행한다"* 고 거절됐다
            //   (E-EFFECT-CALC) — **엉뚱한 모듈, 엉뚱한 이유**. 자리를 하나 빠뜨리면
            //   규칙은 그 자리에서만 조용히 틀린다.
            nest_collect_locals(&c, f, curmod);
            for (proven_size_t j = 0; j < f->nkids; j++) {
                const low_cst_t *b = f->kids[j];
                if (b->kind != LOW_CST_BLOCK) continue;
                for (proven_size_t q = 0; q < b->nkids; q++) {
                    const low_cst_t *hf = b->kids[q];
                    if (hf->kind != LOW_CST_FORM || !hf->nkids || hf->kids[0]->kind != LOW_CST_ATOM) continue;
                    low_kw_t hk = hf->kids[0]->tok.kw;
                    if (hk != LOW_KW_FN && hk != LOW_KW_PROC) continue;
                    low_op_header_t h = low_op_header(hf);
                    if (h.name.size && c.nop < NEST_MAXOPS) { c.op[c.nop] = h.name; c.opmod[c.nop] = curmod; c.ar[c.nop++] = h.np_call + 1; }
                }
            }
        }
    }
    // ★★★ **나무는 단위 전체에 세우고, 보고는 파일에 대해 한다.**
    //   의존을 진짜로 링크하기 시작하자(RFC-0032) 이 계수기가 **의존의 폼까지 셌다** —
    //   그래서 "서식한 소스는 괄호가 0 필요하다" 는 게이트가 **서식 안 된 의존** 때문에 깨졌다.
    //   `--fmt`·`-t` 와 **같은 규칙**이다: 파일 도구는 **파일**을 말한다.
    proven_size_t lim = g_report_limit ? g_report_limit : pr->nforms;
    proven_u8str_view_t walkmod = { 0 };
    for (proven_size_t i = 0; i < pr->nforms; i++) {
        proven_size_t n0 = c.nested, g0 = c.gave_up;
        {   const low_cst_t *f0 = pr->forms[i];
            if (f0->kind == LOW_CST_FORM && f0->nkids >= 2 && f0->kids[0]->kind == LOW_CST_ATOM &&
                f0->kids[0]->tok.kw == LOW_KW_MODULE && f0->kids[1]->kind == LOW_CST_ATOM)
                walkmod = f0->kids[1]->tok.lex;           // ★ 자리가 곧 모듈 소속이다
            c.curmod = walkmod;
        }
        nest_walk(&c, pr->forms[i]);
        if (i >= lim) { c.nested = n0; c.gave_up = g0; }   // 의존의 폼은 **세지 않는다**(나무는 선다)
    }
    if (nested)  *nested  = c.nested;
    if (gave_up) *gave_up = c.gave_up;
    g_last = c;
}

// ★★★ **나무를 도로 평평하게 편다** — 나무 이전의 눈을 가진 코드를 위한 **단 하나의** 창.
//
//   arity 단일화가 `add a (mul b 2)` 를 만들자, **한 층의 인접 원자**를 외우고 있던 스캐너들이
//   조용히 죽기 시작했다 — `parallel` 의 Bernstein 판별기 · `return (error E)` · `E-EXCL` 의
//   빌림 스캐너. 셋 다 **오류 없이** 죽었다.
//
//   ★ 그 스캐너들을 하루아침에 전부 나무로 옮길 수는 없다(raw kids[] 883회).
//     그래서 **되돌리는 창**을 하나 둔다: 나무를 **원래의 평평한 열**로 다시 편다.
//
//     · 평평한 입력에서는 **항등 함수**다 ⇒ 동작이 바뀔 수 **없다**(구성상 보장).
//     · 나무 입력에서는 **정확히 같은 열**을 준다 ⇒ 옛 스캐너가 **그대로 산다.**
//
//   ★★ 이것은 **다리이지 목적지가 아니다.** 소비자가 이 창을 안 쓰고 나무를 직접 읽게 되는
//     것이 이행의 끝이다. **이 창을 쓰는 자리 수가 곧 남은 빚이다.**
// ★★★★ **저자가 친 괄호까지 편다** (2026-09-05).
//
//   위 창은 **정규화가 만든** 괄호만 편다 — 그래야 «평평한 입력에 항등» 이 성립한다.
//   그런데 빌림 스캐너(E-EXCL)에게는 그 구별이 **뜻이 없다**: `be view T s` 와
//   `be (view T s)` 는 같은 프로그램이고, 뒤엣것에서 빌림을 못 보면 **검사가 조용히 안 돈다.**
//   실제로 그랬다 — `--fmt` 은 호출을 언제나 괄호로 찍으므로, **서식을 한 번 돌리면
//   E-EXCL 이 사라졌다**(`vm_stale.low`: 거절되던 픽스처가 초록이 됐다).
//   ☞ *괄호는 사람에게는 읽기 편하자고 치는 것이고 기계에게는 아무 뜻도 없다 —
//     그런데 기계가 그 괄호를 보고 눈을 감으면, 편하자고 친 괄호가 검사를 지운다.*
//
//   ★ 그래서 이 깊은 창은 **borrow 스캐너 전용**이다. `low_flat_kids` 는 그대로 둔다:
//     저자의 괄호를 지우는 것이 옳지 않은 소비자(서식·정규형 해시)가 있기 때문이다.
proven_size_t low_flat_kids_deep(const low_cst_t *f, const low_cst_t **out, proven_size_t max) {
    proven_size_t n = 0;
    if (!f) return 0;
    for (proven_size_t i = 0; i < f->nkids && n < max; i++) {
        const low_cst_t *k = f->kids[i];
        if (k->kind == LOW_CST_GROUP && k->nkids == 1 && k->kids[0]->kind == LOW_CST_FORM)
            n += low_flat_kids_deep(k->kids[0], out + n, max - n);
        else
            out[n++] = k;
    }
    return n;
}

proven_size_t low_flat_kids(const low_cst_t *f, const low_cst_t **out, proven_size_t max) {
    proven_size_t n = 0;
    if (!f) return 0;
    for (proven_size_t i = 0; i < f->nkids && n < max; i++) {
        const low_cst_t *k = f->kids[i];
        // 정규화가 만든 괄호 — GROUP(FORM(head, args…)) — 는 **머리와 인자를 그대로 편다**
        if (k->kind == LOW_CST_GROUP && k->synth && k->nkids == 1 &&
            k->kids[0]->kind == LOW_CST_FORM) {
            n += low_flat_kids(k->kids[0], out + n, max - n);
        } else {
            out[n++] = k;
        }
    }
    return n;
}

// ═══════════════════════════════════════════════════════════════════════════════
// ★★★ **제네릭 = comptime 타입 파라미터 + 단형화** (RFC-0021 — P1 채택, 구현 0 이었다)
//
//   RFC-0021 은 2026-06-28 에 **채택**됐다. 그리고 **한 줄도 지어지지 않았다.**
//   파서는 `comptime` 표식을 **떼어내 버렸고**, `type` 은 타입 낱말이 아니라
//   `E-TYPE-UNDEF` 로 거절됐다 — **RFC 의 대표 문법이 작동하지 않았다.**
//
//       fn pick
//         input comptime t type .        ← **타입 파라미터**
//         input a t . input b t .
//         output t .
//         effects none .
//         requires ord t .               ← **경계** = trait 술어 (RFC-0021 §6.3)
//       do … end
//
//       return pick rect r1 r2 .          ← 타입을 **comptime 인자**로 준다
//
//   ★ 단형화는 **CST → CST** 다: 호출 자리마다 `pick#rect` 라는 **구체 op** 을 만들고,
//     본문의 `t` 를 `rect` 로 바꾼다. 그러면 뒷단(검사·타입·IR)은 **제네릭을 아예 모른다.**
//     간접 호출 0 · 인라인 가능 · **계약이 그대로 전파된다**(RFC-0021 의 약속).
//
//   ★★ 그리고 **경계는 이제 진짜다**: trait 의 구조적 충족이 오늘 서기 전까지
//      `requires ord t` 는 **아무것도 보장하지 못했다.** 그래서 이것이 그 다음 순서였다.

// ★★★★★ **여기 있던 세 고정 표를 없앴다** (2026-08-30, WO-0149 — 실물 프로그램이 찾았다).
//   전엔 `MONO_MAXG 32`(제네릭 **틀**) · `MONO_MAXI 64`(인스턴스) · `MONO_MAXS 16`(타입 틀)
//   이었고, **넘치면 조용히 등록을 안 했다.** 그런데 호출 자리는 그와 무관하게 인스턴스
//   이름으로 **다시 쓰였다** ⇒ *없는 인스턴스를 부르는 코드*가 나오고, 타입 검사가
//   `E-TYPE-INSTANCE` 로 **엉뚱한 것을 탓했다**(프로그램은 멀쩡했다).
//   실측: `shard`+`nodelist`+`pagecache`+`segarena` 넷의 제네릭 export 합이 **40 > 32**.
//   셋까지는 초록이고 **넷이 모이면** 터진다 — 즉 **§8 이 지은 라이브러리 넷 이상이
//   한 프로그램에 못 섰다.** 픽스처는 저마다 하나씩만 세우므로 영영 안 나왔을 답이다.
//   ⇒ 틀 표는 **폼 수만큼**(정확한 상한) 잡고, 인스턴스 표는 **자란다**. 상한이 없으면
//     "넘치면 무엇을 하나" 라는 물음 자체가 사라진다 — *없애기가 후퇴보다 낫다*.
//   ★ 고정점 되풀이의 **횟수**만 상한이 남는다(무한 루프 방지). 그것은 자르는 상한이
//     아니라 **말하는 상한**이다: 닿으면 `E-MONO-FIXPOINT` 로 거절한다.
/* MONO_MAXT — low_cst_priv.h */
/* mono_gen_t — low_cst_priv.h */

