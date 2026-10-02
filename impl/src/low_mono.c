/* low_mono.c — 단형화(monomorphisation): 제네릭 틀에서 구체 인스턴스를 찍어 낸다.
 *
 * ★ `low_cst.c` 에서 **떼어 왔다** (2026-08-31, WO-0168 · X-0011). 뜻은 한 줄도 안 바꿨다.
 *   증명은 `scripts/check-emit-identical.py`(342 단위 `--emit-c`·`--ir` 바이트 동일).
 *
 * ★★ 왜 여기인가. 실측: **나가는 0 · 들여오는 6** — `low_cst.c` 의 네 구획 중 가장 크고
 *   가장 얇다. 그리고 책임이 뚜렷하다: 파싱이 끝난 **뒤**에 도는 별도의 패스이고,
 *   커서·노드 할당과 아무 상관이 없다.
 *
 * ★★★ 처음엔 «포매터» 를 고르려 했는데, 그것은 내가 **구획 표지를 그 절의 끝으로 읽은**
 *   잘못이었다(표지는 시작만 말한다). 다시 재서 정정했다.
 *   ☞ *구획 표지가 곧 책임 경계는 아니다 — 이 저장소에서 세 번째 같은 실수다.*
 */
#include "low_cst_priv.h"

#include "low_cst.h"
#include "low_arity.h"
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>


typedef struct {
    low_parser_t p;
    mono_gen_t   *g;  proven_size_t ng;      // 폼 수만큼 잡는다(정확한 상한)
    proven_u8str_view_t *iname;   // 이미 만든 인스턴스 이름 — **자란다**
    low_cst_t          **iform;
    proven_u32          *isite;   // ★ **만들어진 자리**(호출/사용 줄) — 진단이 여기를 가리킨다
    proven_size_t       ni;
    proven_size_t       nsub;               // ★ 몇 개를 단형화했나 — 정직하게 센다
    // ★★★★★ **맨이름은 남의 틀을 부르지 못한다** (2026-08-14). 한정 호출은 이미 모듈로 갈랐는데
    //   (2026-07-26) **맨이름**은 안 갈랐다. 그런데 가시성 규칙상 맨이름으로는 **남의 모듈을 넘을
    //   수 없다**(E-VISIBILITY) — 그러니 맨이름이 남의 제네릭 틀과 매칭되는 것은 언제나 오답이다.
    //   실측: `files.slurp` 의 `open fs path 0`(제 모듈의 `open` 호출)이 **vecgen 의 제네릭
    //   `open`** 과 매칭돼 `open#fs` 라는 유령 인스턴스를 만들었고, 그 인스턴스의 효과가 새어
    //   `E-EFFECT` 로 거절됐다 — 두 모듈이 한 단위에 **공존할 수 없었다**(원장의 아픈 쌍).
    proven_u8str_view_t callermod;           // 지금 다시 쓰는 자리가 속한 모듈
    proven_u8str_view_t *imod;               // 인스턴스가 태어난 모듈
    proven_size_t        icap;               // 인스턴스 네 배열의 현재 용량
} mono_ctx_t;

// 인스턴스 한 칸 더 — 모자라면 **두 배로 늘린다**(작업 아레나에 새로 잡아 옮긴다).
// 못 늘리면 false 를 답하고, 부르는 쪽은 **호출 자리를 다시 쓰지 않는다**.
static bool mono_inst_room(mono_ctx_t *c) {
    if (c->ni < c->icap) return true;
    proven_size_t cap = c->icap ? c->icap * 2 : 64;
    proven_result_mem_mut_t r1 = c->p.work.alloc_fn(c->p.work.ctx, cap * sizeof(proven_u8str_view_t), alignof(proven_u8str_view_t));
    proven_result_mem_mut_t r2 = c->p.work.alloc_fn(c->p.work.ctx, cap * sizeof(low_cst_t *), alignof(low_cst_t *));
    proven_result_mem_mut_t r3 = c->p.work.alloc_fn(c->p.work.ctx, cap * sizeof(proven_u32), alignof(proven_u32));
    proven_result_mem_mut_t r4 = c->p.work.alloc_fn(c->p.work.ctx, cap * sizeof(proven_u8str_view_t), alignof(proven_u8str_view_t));
    if (r1.err != PROVEN_OK || r2.err != PROVEN_OK || r3.err != PROVEN_OK || r4.err != PROVEN_OK) return false;
    proven_u8str_view_t *nn   = (proven_u8str_view_t *)r1.value.ptr;
    low_cst_t          **nf   = (low_cst_t **)r2.value.ptr;
    proven_u32          *nsit = (proven_u32 *)r3.value.ptr;
    proven_u8str_view_t *nmd  = (proven_u8str_view_t *)r4.value.ptr;
    if (c->ni) {
        memcpy(nn,   c->iname, c->ni * sizeof(proven_u8str_view_t));
        memcpy(nf,   c->iform, c->ni * sizeof(low_cst_t *));
        memcpy(nsit, c->isite, c->ni * sizeof(proven_u32));
        memcpy(nmd,  c->imod,  c->ni * sizeof(proven_u8str_view_t));
    }
    // ★ **새 칸은 0 으로 둔다.** 옛 고정 배열은 구조체 초기화가 0 을 채워 줬고,
    //   그 0(빈 모듈 이름)에 뒤의 규칙이 기대고 있었다. 잡아 온 메모리는 안 그렇다 —
    //   *표현을 바꿀 때는 그 표현이 공짜로 주던 것까지 옮겨야 한다.*
    memset(nn + c->ni,   0, (cap - c->ni) * sizeof(proven_u8str_view_t));
    memset(nf + c->ni,   0, (cap - c->ni) * sizeof(low_cst_t *));
    memset(nsit + c->ni, 0, (cap - c->ni) * sizeof(proven_u32));
    memset(nmd + c->ni,  0, (cap - c->ni) * sizeof(proven_u8str_view_t));
    c->iname = nn; c->iform = nf; c->isite = nsit; c->imod = nmd; c->icap = cap;
    return true;
}

// 아레나에 `<name>#<t1>#<t2>…` 를 짓는다. `#` 은 렉서가 절대 못 내는 글자다.
static proven_u8str_view_t mono_iname(mono_ctx_t *c, proven_u8str_view_t nm,
                                      const proven_u8str_view_t *ty, proven_size_t nt) {
    proven_size_t n = nm.size;
    for (proven_size_t i = 0; i < nt; i++) n += 1 + ty[i].size;
    proven_result_mem_mut_t r = c->p.node_alloc.alloc_fn(c->p.node_alloc.ctx, n, 1);
    if (r.err != PROVEN_OK) return (proven_u8str_view_t){ 0 };
    proven_u8 *b = (proven_u8 *)r.value.ptr;
    memcpy(b, nm.ptr, nm.size);
    proven_size_t o = nm.size;
    for (proven_size_t i = 0; i < nt; i++) {
        b[o++] = (proven_u8)'#';
        memcpy(b + o, ty[i].ptr, ty[i].size); o += ty[i].size;
    }
    return (proven_u8str_view_t){ .ptr = b, .size = n };
}

// 나무를 복제하면서 `from` 이라는 이름의 원자를 `to` 로 바꾼다.
static low_cst_t *mono_clone(mono_ctx_t *c, const low_cst_t *nd,
                             proven_u8str_view_t from, low_token_t to) {
    if (!nd) return NULL;
    low_cst_t *n2 = low_node(&c->p, nd->kind, nd->tok);
    if (!n2) return NULL;
    n2->closer = nd->closer; n2->synth = nd->synth;
    // ★★★ **수식자 표시도 옮긴다.** 안 옮겼더니 `export fn twice` 의 **인스턴스가
    //   export 를 잃었고**, 그것을 부르는 모듈이 **E-VISIBILITY**(거짓 오류)를 맞았다.
    //   ★ 복제는 **모든 것**을 복제해야 한다. 하나를 빠뜨리면 그 하나가 조용히 사라진다.
    n2->is_export = nd->is_export; n2->is_unsafe = nd->is_unsafe;
    n2->line = nd->line; n2->col = nd->col;
    // ★★★★ **어느 파일에서 왔는지도 옮긴다** (2026-09-06). 복제가 `file` 을 안 옮겨서
    //   인스턴스의 진단이 **파일 없이** 나왔다(`151:181 W-EFFECT-OVER: …`) — 한 단위에 파일이
    //   여럿이면 그 줄 번호는 **아무 데도 가리키지 않는다**.
    //   ☞ 바로 위 주석이 이미 그 값을 배운 자리다: *복제는 **모든 것**을 복제해야 한다.
    //     하나를 빠뜨리면 그 하나가 조용히 사라진다.* — `export` 였고, 이번엔 `file` 이다.
    n2->file = nd->file;
    // ★★★ **토큰의 종류까지 옮겨야 한다.** lex 만 바꾸면 치환된 `5` 가 여전히 IDENT 라서
    //   뒷단이 그것을 **이름으로 조회**하고 E-IR-UNDEF 를 낸다. 타입 파라미터일 땐 결과가 다시
    //   식별자(`rect`)여서 이 버그가 **보이지 않았다** — 값 comptime 을 접기 시작하니 드러났다.
    if (nd->kind == LOW_CST_ATOM && proven_u8str_view_eq(nd->tok.lex, from)) {
        n2->tok.lex = to.lex; n2->tok.kind = to.kind; n2->tok.kw = to.kw;
    }
    if (!nd->nkids) return n2;
    proven_array_t kids = PROVEN_ARRAY_INIT(c->p.work, low_cst_t *, nd->nkids).value;
    for (proven_size_t i = 0; i < nd->nkids; i++)
        (void)PROVEN_ARRAY_PUSH(&kids, low_cst_t *, mono_clone(c, nd->kids[i], from, to));
    low_take_kids(&c->p, n2, &kids);
    return n2;
}

// 제네릭 선언에서 **타입 파라미터 절**(`input comptime t type .`)의 원자들을 뺀 헤더를 만든다.
static low_cst_t *mono_instance(mono_ctx_t *c, const mono_gen_t *g,
                                const low_token_t *ty, proven_size_t nt) {
    // ① comptime 파라미터를 **하나씩** 치환하며 복제 (여럿이면 여러 번). 타입이든 값이든 같은 기계다.
    low_cst_t *cl = mono_clone(c, g->form, g->tparam[0], ty[0]);
    for (proven_size_t i = 1; i < nt && cl; i++)
        cl = mono_clone(c, cl, g->tparam[i], ty[i]);
    if (!cl || cl->nkids < 2) return NULL;
    // ② 이름을 `<name>#<t1>#<t2>…` 로 (인자의 낱말로 — 타입 이름이든 값이든)
    proven_u8str_view_t tyv2[MONO_MAXT];
    for (proven_size_t i = 0; i < nt && i < MONO_MAXT; i++) tyv2[i] = ty[i].lex;
    cl->kids[1]->tok.lex = mono_iname(c, g->name, tyv2, nt);
    cl->tok.lex = cl->kids[1]->tok.lex;
    // ③ `input comptime <t> type` 절들을 헤더에서 **전부 뺀다**
    //   ★ 치환 뒤에는 `type` 이 구체 타입으로 바뀌어 있으므로, `comptime` 표식으로 찾는다.
    proven_array_t kids = PROVEN_ARRAY_INIT(c->p.work, low_cst_t *, cl->nkids).value;
    for (proven_size_t i = 0; i < cl->nkids; i++) {
        low_cst_t *k = cl->kids[i];
        if (k->kind == LOW_CST_ATOM && low_view_eq_cstr(k->tok.lex, "input") &&
            i + 3 < cl->nkids && cl->kids[i+1]->kind == LOW_CST_ATOM &&
            low_view_eq_cstr(cl->kids[i+1]->tok.lex, "comptime")) { i += 3; continue; }
        (void)PROVEN_ARRAY_PUSH(&kids, low_cst_t *, k);
    }
    (void)low_refit(&c->p, cl, (low_cst_t **)kids.data, kids.len);
    proven_array_destroy(&kids);
    return cl;
}

// 호출 자리를 찾아 다시 쓴다: FORM(pick, [pick, <타입낱말>, args…]) → FORM(pick#T, [pick#T, args…])
// ★ 모듈 한정 `M.member`(M=선언 모듈, 점 하나) → bare `member`. mono 는 파서 단계라 narrow 전에 돌아
//   한정 제네릭 호출(`gen.vp`)을 스스로 벗겨 g->name(`vp`)과 매칭해야 한다 (RFC-0011).
static proven_u8str_view_t mono_strip_mod(const low_parse_result_t *pr, proven_u8str_view_t v) {
    proven_size_t dot = v.size, ndot = 0;
    for (proven_size_t i = 0; i < v.size; i++)
        if (v.ptr[i] == (proven_u8)'.') { if (dot == v.size) dot = i; ndot++; }
    if (ndot != 1 || dot == 0 || dot + 1 >= v.size) return v;
    proven_u8str_view_t head = { .ptr = v.ptr, .size = dot };
    for (proven_size_t i = 0; i < pr->nforms; i++) {
        const low_cst_t *f = pr->forms[i];
        if (f->kind == LOW_CST_FORM && f->nkids >= 2 && f->kids[0]->kind == LOW_CST_ATOM &&
            f->kids[0]->tok.kw == LOW_KW_MODULE && f->kids[1]->kind == LOW_CST_ATOM &&
            proven_u8str_view_eq(f->kids[1]->tok.lex, head))
            return (proven_u8str_view_t){ .ptr = v.ptr + dot + 1, .size = v.size - dot - 1 };
    }
    return v;
}
static void mono_rewrite(mono_ctx_t *c, low_cst_t *nd) {
    if (!nd) return;
    if (nd->kind == LOW_CST_FORM && nd->nkids >= 2 && nd->kids[0]->kind == LOW_CST_ATOM &&
        nd->kids[1]->kind == LOW_CST_ATOM) {
        for (proven_size_t gi = 0; gi < c->ng; gi++) {
            const mono_gen_t *g = &c->g[gi];
            proven_u8str_view_t head = nd->kids[0]->tok.lex;
            proven_u8str_view_t bare = mono_strip_mod(c->p.out, head);
            if (!proven_u8str_view_eq(bare, g->name)) continue;
            // ★ 한정 호출은 **그 모듈의 틀**만 부른다. 벗겨진 이름이 같다고 남의 틀을 쓰면
            //   첫 인자가 타입 인자로 먹힌다(유령 인스턴스).
            if (bare.size < head.size) {
                proven_u8str_view_t qm = { .ptr = head.ptr, .size = head.size - bare.size - 1 };
                if (g->mod.size && !proven_u8str_view_eq(qm, g->mod)) continue;
            } else if (g->mod.size && c->callermod.size &&
                       !proven_u8str_view_eq(g->mod, c->callermod)) {
                continue;   // ★ 맨이름 — 제 모듈의 틀만(위 callermod 주석)
            }
            if (nd->nkids < 1 + g->nt) continue;                 // 타입 인자가 모자라다
            // ★ 타입 인자는 **앞자리들**에 순서대로 온다 (comptime 파라미터가 먼저 선언되므로)
            low_token_t ty[MONO_MAXT];
            proven_u8str_view_t tyv[MONO_MAXT];
            for (proven_size_t q = 0; q < g->nt; q++) { ty[q] = nd->kids[1 + q]->tok; tyv[q] = ty[q].lex; }
            proven_u8str_view_t in = mono_iname(c, g->name, tyv, g->nt);
            // 인스턴스를 (없으면) 만든다
            bool have = false;
            for (proven_size_t i = 0; i < c->ni; i++)
                if (proven_u8str_view_eq(c->iname[i], in)) have = true;
            if (!have && mono_inst_room(c)) {
                low_cst_t *inst = mono_instance(c, g, ty, g->nt);
                if (inst) {
                    c->iname[c->ni] = in; c->iform[c->ni] = inst;
                    // ★★ 인스턴스의 **본문은 틀의 코드**다 — 태어난 자리가 아니라 **틀의 모듈**을
                    //   물려받아야 그 안의 맨이름이 제 이웃을 찾는다(실측: 태어난 자리를 물려주자
                    //   제네릭 안의 맨이름이 아무것도 못 찾아 단형화가 통째로 멈췄다 — 골든 10 적색).
                    c->imod[c->ni] = g->mod;
                    c->isite[c->ni] = nd->kids[0]->tok.line;   // ★ 부른 자리
                    c->ni++;
                }
            }
            // 호출을 다시 쓴다 — 머리를 인스턴스 이름으로, **타입 낱말들을 뺀다**
            nd->kids[0]->tok.lex = in;
            nd->tok.lex = in;
            proven_array_t kids = PROVEN_ARRAY_INIT(c->p.work, low_cst_t *, nd->nkids).value;
            (void)PROVEN_ARRAY_PUSH(&kids, low_cst_t *, nd->kids[0]);
            for (proven_size_t i = 1 + g->nt; i < nd->nkids; i++)
                (void)PROVEN_ARRAY_PUSH(&kids, low_cst_t *, nd->kids[i]);
            (void)low_refit(&c->p, nd, (low_cst_t **)kids.data, kids.len);
            proven_array_destroy(&kids);
            c->nsub++;
            break;
        }
    }
    for (proven_size_t i = 0; i < nd->nkids; i++) mono_rewrite(c, nd->kids[i]);
}

// ★ 이 op 이 제네릭인가 — comptime 파라미터의 타입 낱말이 `type` 이면. **여럿일 수 있다.**
// ★ `vec <t> <name>` 의 레인 자리에 이 이름이 있는가 (재귀 — 본문의 var 선언까지 본다)
static bool mono_used_as_lane(const low_cst_t *nd, proven_u8str_view_t name) {
    if (!nd) return false;
    for (proven_size_t i = 0; i + 2 < nd->nkids; i++)
        if (nd->kids[i]->kind == LOW_CST_ATOM && low_view_eq_cstr(nd->kids[i]->tok.lex, "vec") &&
            nd->kids[i + 2]->kind == LOW_CST_ATOM &&
            proven_u8str_view_eq(nd->kids[i + 2]->tok.lex, name)) return true;
    for (proven_size_t i = 0; i < nd->nkids; i++)
        if (mono_used_as_lane(nd->kids[i], name)) return true;
    return false;
}
static proven_size_t mono_tparams(const low_cst_t *f, low_op_header_t *h,
                                  proven_u8str_view_t *out, proven_size_t max) {
    if (f->kind != LOW_CST_FORM || !f->nkids || f->kids[0]->kind != LOW_CST_ATOM) return 0;
    low_kw_t kw = f->kids[0]->tok.kw;
    if (kw != LOW_KW_FN && kw != LOW_KW_PROC) return 0;
    *h = low_op_header(f);
    proven_size_t n = 0;
    for (proven_size_t i = 0; i < h->np && n < max; i++) {
        if (!h->p[i].is_comptime || h->p[i].core >= f->nkids ||
            f->kids[h->p[i].core]->kind != LOW_CST_ATOM) continue;
        // ★★★ **타입 파라미터는 늘 접는다**(원래 동작).
        bool fold = low_view_eq_cstr(f->kids[h->p[i].core]->tok.lex, "type");
        // ★★★ **값 comptime 파라미터는 "컴파일 시 값이 꼭 필요할 때" 접는다** (RFC-0021 미구현분).
        //   그 자리란 **타입 자리**다 — `vec <t> <n>` 의 레인 수. 거기 서려면 값이 상수여야 하고,
        //   호출마다 다를 수 있으니 **단형화**밖에 길이 없다.
        //   ★ 값 파라미터를 **무조건** 접으면 안 된다: 지금 모델에서 comptime 값은 평소 런타임
        //     파라미터로 살고(직접 `--run f 3 4`·`parallel` 과 함께), 상수 강제는 E-COMPTIME-ARG 가
        //     한다. 무조건 접었더니 그 계약 여섯 가지가 깨졌다 — 접기는 **필요한 곳에만**.
        if (!fold) fold = mono_used_as_lane(f, h->p[i].name);
        if (fold) out[n++] = h->p[i].name;
    }
    return n;
}
// ★ 이 이름이 **타입의 레인 자리**(`vec <t> <name>`)에 쓰였는가 — 그렇다면 컴파일 시 값이 필요하다.
bool low_is_generic_template(const low_cst_t *f) {
    if (!f || f->kind != LOW_CST_FORM || !f->nkids || f->kids[0]->kind != LOW_CST_ATOM) return false;
    low_kw_t kw = f->kids[0]->tok.kw;
    if (kw != LOW_KW_FN && kw != LOW_KW_PROC) return false;
    low_op_header_t h = low_op_header(f);
    for (proven_size_t i = 0; i < h.np; i++) {
        if (!h.p[i].is_comptime || h.p[i].core >= f->nkids ||
            f->kids[h.p[i].core]->kind != LOW_CST_ATOM) continue;
        if (low_view_eq_cstr(f->kids[h.p[i].core]->tok.lex, "type")) return true;
        if (mono_used_as_lane(f, h.p[i].name)) return true;
    }
    return false;
}


// ★★★ **제네릭 소유 타입** (RFC-0084) — `gvec u32` 처럼 **타입**이 타입 인자를 받는다.
//
//   op 제네릭(위)과 같은 기계다: 틀을 복제해 `t` 를 구체 타입으로 바꾸고 `gvec#u32` 라 부른다.
//   다른 점은 **어디를 다시 쓰느냐**다 — op 은 *호출 자리*, 타입은 **타입 자리**다.
//   타입 자리는 문법적으로 표시가 없다(`slice u8` 처럼 그냥 낱말 둘). 그래서 규칙은 단순하다:
//   **제네릭 타입 이름 바로 뒤의 낱말이 타입 인자다.** 둘을 하나(`gvec#u32`)로 접는다.
//
//   ★ 왜 접는가: 접고 나면 뒷단(검사·타입·IR)은 **제네릭을 아예 모른다** — 그냥 구체 struct 다.
//     크기·소유·필드 오프셋이 전부 그 자리에서 나온다(RFC-0084 §2.1·§2.2).
//   ☞ 선언 표면: 틀의 **블록 첫 줄**이 `input comptime t type .` 이다(op 과 같은 철자 — 한 개념
//     한 철자). 그 줄은 인스턴스에서 사라진다.
// ★ 제네릭 **타입** 틀도 같은 이유로 상한을 없앴다(위 주석). 폼 수만큼 잡는다.
typedef struct {
    proven_u8str_view_t name;
    proven_u8str_view_t mod;        // ★ 선언 모듈 — 한정 이름을 남의 틀과 안 섞으려고
    const low_cst_t    *form;
    // ★★ 타입 파라미터는 **여럿일 수 있다**(`map k v`). op 제네릭은 처음부터 여럿을 받았는데
    //   타입 자리는 하나만 먹고 있었다 — 같은 개념이 자리에 따라 다른 능력을 갖는 것은
    //   낮은 엔트로피가 아니다(RFC-0084 §11).
    proven_u8str_view_t tparam[MONO_MAXT];
    proven_size_t       nt;
} mono_struct_t;

// 이 form 이 제네릭 struct 틀인가 — 그렇다면 타입 파라미터 이름을 준다.
static bool mono_is_tparam_form(const low_cst_t *c0, proven_u8str_view_t *out) {
    if (!c0 || c0->kind != LOW_CST_FORM || c0->nkids < 4) return false;
    for (proven_size_t i = 0; i < 4; i++) if (c0->kids[i]->kind != LOW_CST_ATOM) return false;
    if (!low_view_eq_cstr(c0->kids[0]->tok.lex, "input")) return false;
    if (!low_view_eq_cstr(c0->kids[1]->tok.lex, "comptime")) return false;
    if (!low_view_eq_cstr(c0->kids[3]->tok.lex, "type")) return false;
    if (out) *out = c0->kids[2]->tok.lex;
    return true;
}
// 파라미터 절은 블록 **맨 앞에 연달아** 온다 — 그 개수를 돌려준다(0 이면 제네릭이 아니다).
static proven_size_t mono_struct_gen(const low_cst_t *f, proven_u8str_view_t *tp, proven_size_t max) {
    if (!f || f->kind != LOW_CST_FORM || f->nkids < 3) return 0;
    if (f->kids[0]->kind != LOW_CST_ATOM || f->kids[0]->tok.kw != LOW_KW_STRUCT) return 0;
    if (f->kids[1]->kind != LOW_CST_ATOM) return 0;
    const low_cst_t *blk = f->kids[f->nkids - 1];
    if (blk->kind != LOW_CST_BLOCK || !blk->nkids) return 0;
    proven_size_t n = 0;
    while (n < blk->nkids && n < max && mono_is_tparam_form(blk->kids[n], tp ? &tp[n] : NULL)) n++;
    return n;
}

// 제네릭 struct 인스턴스: `t` 를 치환하고, 이름을 `gvec#u32` 로 바꾸고, 파라미터 줄을 뺀다.
static low_cst_t *mono_struct_instance(mono_ctx_t *c, const mono_struct_t *g,
                                       const low_token_t *ty, proven_size_t nt) {
    low_cst_t *cl = mono_clone(c, g->form, g->tparam[0], ty[0]);
    for (proven_size_t i = 1; i < nt && cl; i++)
        cl = mono_clone(c, cl, g->tparam[i], ty[i]);
    if (!cl || cl->nkids < 3) return NULL;
    proven_u8str_view_t tyv[MONO_MAXT];
    for (proven_size_t i = 0; i < nt; i++) tyv[i] = ty[i].lex;
    cl->kids[1]->tok.lex = mono_iname(c, g->name, tyv, nt);
    cl->tok.lex = cl->kids[1]->tok.lex;
    low_cst_t *blk = cl->kids[cl->nkids - 1];
    if (blk->kind != LOW_CST_BLOCK || blk->nkids <= nt) return NULL;
    // 파라미터 줄들을 뺀다 — 치환 뒤에는 `input comptime u32 type` 이라 뜻이 없다.
    proven_array_t kids = PROVEN_ARRAY_INIT(c->p.work, low_cst_t *, blk->nkids).value;
    for (proven_size_t i = nt; i < blk->nkids; i++)
        (void)PROVEN_ARRAY_PUSH(&kids, low_cst_t *, blk->kids[i]);
    (void)low_refit(&c->p, blk, (low_cst_t **)kids.data, kids.len);
    proven_array_destroy(&kids);
    return cl;
}

// ★★ **`make gvec u32 do … end` 는 파스 모양이 갈라진다** — 그리고 그것이 이 기능의 유일한 구멍이었다.
//
//   이 언어에서 `do` 는 **바로 앞 낱말**에 블록을 붙인다(headed block). 그래서 평범한
//   `make box do … end` 는 `make(FORM[box, BLOCK])` 로 예쁘게 서지만, 타입 인자가 끼면
//       make box u32 do … end   →   make(box) 와 **형제** FORM[u32, BLOCK]
//   로 갈라진다 — 블록이 **타입 인자**에 붙어 버린다. 접기만으로는 못 고친다(둘이 **다른 부모**다).
//
//   ⇒ 접기 **전에** 그 두 조각을 도로 붙인다: 꼬리 원자 `box` 자리에 FORM[box, BLOCK] 을 놓고
//     형제를 없앤다. 그러면 이후는 평범한 make 와 **똑같은 나무**다.
//   ☞ 오인 위험: "제네릭 타입 이름 + 곧바로 [원자, 블록] 형제" 라는 매우 좁은 모양에서만 움직인다.
// ★★★★★ **제네릭 타입 이름도 모듈로 가른다** (2026-08-30, WO-0149 — 실물 프로그램이 찾았다).
//   전엔 이름만 봤다. 그래서 `pagecache` 의 `make pin b do id id . depth 1 . **held** c . end .`
//   에서 **필드 이름 `held`** 가 `nodelist` 의 제네릭 타입 `held` 로 잡혔고, make 가
//   엉뚱하게 봉합되어 `E-TYPE-FIELD`(*"이 struct 에 그런 필드가 없다"*) 가 났다.
//   ⇒ 두 라이브러리가 **한 프로그램에 못 섰다**. 규칙은 이미 정해져 있다:
//     **한정이면 그 모듈, 맨이름이면 제 모듈.** 여기에도 그대로 건다.
//   ☞ 같은 뿌리를 고친 일곱 번째 자리다(하강·효과조회·단형화기 op·파서 arity 표·
//     타입검사 sig_find·효과표 되짚기·그리고 여기 단형화기 **타입**).
static bool mono_is_gen_struct(mono_ctx_t *c, proven_u8str_view_t nm,
                               const mono_struct_t *sg, proven_size_t ns) {
    proven_u8str_view_t bare = mono_strip_mod(c->p.out, nm);
    proven_u8str_view_t qmod = { 0 };
    if (bare.size < nm.size) qmod = (proven_u8str_view_t){ .ptr = nm.ptr, .size = nm.size - bare.size - 1 };
    for (proven_size_t q = 0; q < ns; q++) {
        if (!proven_u8str_view_eq(bare, sg[q].name)) continue;
        if (qmod.size) { if (proven_u8str_view_eq(sg[q].mod, qmod)) return true; continue; }
        // 맨이름 — 틀이 **제 모듈**의 것일 때만 잡는다. 남의 모듈 타입은 한정으로만 부른다.
        if (!sg[q].mod.size || !c->callermod.size ||
            proven_u8str_view_eq(sg[q].mod, c->callermod)) return true;
    }
    return false;
}
// A 의 **꼬리 원자**가 제네릭 타입 이름이면 그 원자를 담은 노드를 준다(없으면 NULL).
static low_cst_t *mono_tail_owner(mono_ctx_t *c, low_cst_t *a,
                                  const mono_struct_t *sg, proven_size_t ns) {
    while (a && a->nkids && (a->kind == LOW_CST_FORM || a->kind == LOW_CST_GROUP)) {
        low_cst_t *last = a->kids[a->nkids - 1];
        if (last->kind == LOW_CST_ATOM)
            return mono_is_gen_struct(c, last->tok.lex, sg, ns) ? a : NULL;
        a = last;
    }
    return NULL;
}
static void mono_make_fix(mono_ctx_t *c, low_cst_t *nd, const mono_struct_t *sg, proven_size_t ns) {
    if (!nd || nd->nkids < 2) {
        if (nd) for (proven_size_t i = 0; i < nd->nkids; i++) mono_make_fix(c, nd->kids[i], sg, ns);
        return;
    }
    // ★★★ **이미 봉합한 자리는 다시 안 본다.** 봉합 결과([G, 인자…, BLOCK])는 ⓑ 의 모양과
    //   구별이 안 된다 — 그래서 표시를 남긴다. 없으면 패스마다 다시 감싸며 나무가 자란다
    //   (`FORM{FORM{FORM{…}}}`) 그리고 아레나가 마른다. **멱등이 아닌 수리는 수리가 아니다.**
    if (nd->synth) return;
    bool changed = false;
    proven_array_t kids = PROVEN_ARRAY_INIT(c->p.work, low_cst_t *, nd->nkids).value;
    for (proven_size_t i = 0; i < nd->nkids; i++) {
        low_cst_t *a = nd->kids[i];
        // 이 자리(또는 앞 형의 꼬리)가 제네릭 타입 이름인가 — 그리고 몇 개의 인자를 받는가.
        bool here = (a->kind == LOW_CST_ATOM) && mono_is_gen_struct(c, a->tok.lex, sg, ns);
        low_cst_t *own = here ? nd : mono_tail_owner(c, a, sg, ns);
        low_cst_t *tail = own ? (here ? a : own->kids[own->nkids - 1]) : NULL;
        proven_size_t nt = 0;
        if (tail) {
            proven_u8str_view_t bare = mono_strip_mod(c->p.out, tail->tok.lex);
            for (proven_size_t q = 0; q < ns; q++)
                if (proven_u8str_view_eq(bare, sg[q].name)) nt = sg[q].nt;
        }
        // ★ 갈라진 모양은 **둘**이다:
        //   ⓐ [G, 인자1 … 인자_{n-1}, FORM[인자_n, BLOCK]] — 마지막 **낱말** 인자에 블록이 붙었다.
        //   ⓑ [G, 인자1 … 인자_n, BLOCK]                    — 마지막 인자가 **괄호**라 붙을 낱말이
        //      없어 블록이 형제로 남았다(중첩: `make box (box u32) do …`).
        //   `do` 는 바로 앞 **낱말**을 잡는다 — 그래서 괄호로 끝나면 모양이 달라진다.
        // ⓐ 인자가 낱말로 끝나면 마지막 인자와 블록이 **한 형제**로 융합된다: 인자 n-1 개가
        //    흩어지고 [인자_n, BLOCK] 이 i+nt 에 온다.
        // ⓑ 인자가 괄호로 끝나면 붙을 낱말이 없어 **블록이 홀로** 남는다: 인자 n 개가 흩어지고
        //    BLOCK 이 i+nt+1 에 온다.
        proven_size_t sep = i + nt;
        bool splittable = nt && sep < nd->nkids &&
                          nd->kids[sep]->kind == LOW_CST_FORM && nd->kids[sep]->nkids == 2 &&
                          nd->kids[sep]->kids[0]->kind == LOW_CST_ATOM &&
                          nd->kids[sep]->kids[1]->kind == LOW_CST_BLOCK;
        // ★★ ⓑ 는 **마지막 인자가 진짜 괄호일 때만**이다. 이 조건을 빼면 **이미 봉합한 모양**
        //   ([G, 인자, BLOCK])이 다음 패스에서 또 걸려 매 패스마다 새 노드를 만든다 —
        //   아레나가 마르고, 그러면 `mono_iname` 이 **빈 이름**을 내며 타입이 조용히 사라진다.
        //   (증상: 헤더에 `ATOM <빈칸>` 이 남고 E-TYPE-UNDEF 가 무더기로 뜬다.)
        bool paren_end = false;
        if (!splittable && nt && i + nt < nd->nkids &&
            nd->kids[i + nt]->kind == LOW_CST_GROUP &&
            i + nt + 1 < nd->nkids && nd->kids[i + nt + 1]->kind == LOW_CST_BLOCK) {
            paren_end = true; splittable = true; sep = i + nt + 1;
        }
        for (proven_size_t q = 1; q < (paren_end ? nt + 1 : nt) && splittable; q++)
            if (i + q >= nd->nkids ||
                (nd->kids[i + q]->kind != LOW_CST_ATOM && nd->kids[i + q]->kind != LOW_CST_GROUP))
                splittable = false;
        if (splittable) {
            low_cst_t *b = nd->kids[sep];
            low_cst_t *nf = low_node(&c->p, LOW_CST_FORM, tail->tok);
            if (nf) {
                proven_array_t k2 = PROVEN_ARRAY_INIT(c->p.work, low_cst_t *, nt + 2).value;
                (void)PROVEN_ARRAY_PUSH(&k2, low_cst_t *, tail);
                if (paren_end) {
                    for (proven_size_t q = 1; i + q < sep; q++)          // 흩어진 인자 전부
                        (void)PROVEN_ARRAY_PUSH(&k2, low_cst_t *, nd->kids[i + q]);
                    (void)PROVEN_ARRAY_PUSH(&k2, low_cst_t *, b);       // BLOCK 자신
                } else {
                    for (proven_size_t q = 1; q < nt; q++)                  // 흩어진 앞 인자들
                        (void)PROVEN_ARRAY_PUSH(&k2, low_cst_t *, nd->kids[i + q]);
                    (void)PROVEN_ARRAY_PUSH(&k2, low_cst_t *, b->kids[0]);  // 마지막 인자
                    (void)PROVEN_ARRAY_PUSH(&k2, low_cst_t *, b->kids[1]);  // BLOCK
                }
                low_take_kids(&c->p, nf, &k2);
                nf->synth = true;                 // ★ 표시 — 다음 패스가 이걸 또 감싸지 않는다
                if (here) { (void)PROVEN_ARRAY_PUSH(&kids, low_cst_t *, nf); }
                else { own->kids[own->nkids - 1] = nf;
                       (void)PROVEN_ARRAY_PUSH(&kids, low_cst_t *, a); }
                i = sep;                        // 흩어진 인자들과 형제를 **삼킨다**
                changed = true;
                continue;
            }
        }
        (void)PROVEN_ARRAY_PUSH(&kids, low_cst_t *, a);
    }
    if (changed) (void)low_refit(&c->p, nd, (low_cst_t **)kids.data, kids.len);
    proven_array_destroy(&kids);
    for (proven_size_t i = 0; i < nd->nkids; i++) mono_make_fix(c, nd->kids[i], sg, ns);
}

// 타입 자리를 다시 쓴다: (ATOM gvec)(ATOM u32) → (ATOM gvec#u32). 없으면 인스턴스를 만든다.
// 반환 = 이번 훑기에서 접은 자리 수.
// ★★ 타입 인자가 **낱말 하나**여야 할 이유는 없다 — `(gvec.vec u8)` 처럼 괄호로 싸인 것도
//   접고 나면 원자 하나다. 그래서 **안쪽부터** 접는다(후위 순회): 안쪽이 `vec#u8` 이 되고 나면
//   바깥은 평범한 (이름)(원자) 쌍이 된다 ⇒ 중첩이 특별한 경우가 아니게 된다.
//   ☞ 괄호 없이 `vec vec u8` 로 쓰는 것은 **일부러 안 받는다**: 인자가 몇 개인지 눈으로 못 센다.
static low_cst_t *mono_arg_atom(low_cst_t *k) {
    if (!k) return NULL;
    if (k->kind == LOW_CST_ATOM) return (k->tok.kind == LOW_TOK_IDENT) ? k : NULL;
    // GROUP{FORM{ATOM}} — 안쪽 접기가 끝나면 이 모양이 된다.
    while ((k->kind == LOW_CST_GROUP || k->kind == LOW_CST_FORM) && k->nkids == 1) k = k->kids[0];
    return (k->kind == LOW_CST_ATOM && k->tok.kind == LOW_TOK_IDENT) ? k : NULL;
}
static proven_size_t mono_type_rewrite(mono_ctx_t *c, low_cst_t *nd,
                                       const mono_struct_t *sg, proven_size_t ns) {
    if (!nd || !nd->nkids) return 0;
    proven_size_t n = 0;
    // ★ **안쪽 먼저**(후위) — 이 한 줄이 중첩을 공짜로 만든다.
    for (proven_size_t i = 0; i < nd->nkids; i++) n += mono_type_rewrite(c, nd->kids[i], sg, ns);
    bool changed = false;
    proven_array_t kids = PROVEN_ARRAY_INIT(c->p.work, low_cst_t *, nd->nkids).value;
    for (proven_size_t i = 0; i < nd->nkids; i++) {
        low_cst_t *k = nd->kids[i];
        proven_size_t gi = ns;
        low_cst_t *ak[MONO_MAXT];
        if (k->kind == LOW_CST_ATOM) {
            proven_u8str_view_t bare = mono_strip_mod(c->p.out, k->tok.lex);
            proven_u8str_view_t qm = { 0 };
            if (bare.size < k->tok.lex.size)
                qm = (proven_u8str_view_t){ .ptr = k->tok.lex.ptr, .size = k->tok.lex.size - bare.size - 1 };
            for (proven_size_t q = 0; q < ns; q++) {
                if (!proven_u8str_view_eq(bare, sg[q].name)) continue;
                if (qm.size && sg[q].mod.size && !proven_u8str_view_eq(qm, sg[q].mod)) continue;
                // ★★★ **한정하지 않은 이름은 제 모듈의 것이다** (2026-08-28, RFC-0104 §8-2 가
                //   밟았다). `lib/pool.low` 이 `handle` 이라는 **제네릭 틀**을 갖게 되자,
                //   `lib/file.low` 의 평범한 `handle`(제 모듈의 struct)이 그 틀로 오인되어
                //   **뒤따르는 낱말을 타입 인자로 삼켰다** ⇒ E-TYPE-UNDEF. 두 라이브러리가
                //   한 단위에 못 서게 된 것이다.
                //   ☞ 이 저장소가 이미 두 번 적어 둔 규율을 여기도 건다:
                //     **한정이면 그 모듈, 아니면 제 모듈.** 남의 모듈 틀을 한정 없이 집지 않는다.
                if (!qm.size && sg[q].mod.size && c->callermod.size &&
                    !proven_u8str_view_eq(sg[q].mod, c->callermod)) continue;
                gi = q;
            }
            // ★ 인자가 **개수만큼** 뒤따라야 한다 — 하나라도 모자라면 접지 않는다(선언 자리
            //   자신의 이름 등). 못 접은 것은 뒤의 타입 검사가 정직하게 고발한다.
            if (gi < ns) {
                for (proven_size_t q = 0; q < sg[gi].nt; q++) {
                    ak[q] = (i + 1 + q < nd->nkids) ? mono_arg_atom(nd->kids[i + 1 + q]) : NULL;
                    if (!ak[q]) { gi = ns; break; }
                }
            }
        }
        if (gi < ns) {
            proven_size_t nt = sg[gi].nt;
            low_token_t ty[MONO_MAXT]; proven_u8str_view_t tyv[MONO_MAXT];
            for (proven_size_t q = 0; q < nt; q++) { ty[q] = ak[q]->tok; tyv[q] = ty[q].lex; }
            proven_u8str_view_t in = mono_iname(c, sg[gi].name, tyv, nt);
            bool have = false;
            for (proven_size_t q = 0; q < c->ni; q++)
                if (proven_u8str_view_eq(c->iname[q], in)) have = true;
            if (!have && mono_inst_room(c)) {
                low_cst_t *inst = mono_struct_instance(c, &sg[gi], ty, nt);
                if (inst) {
                    c->iname[c->ni] = in; c->iform[c->ni] = inst;
                    // ★★★★★ **타입 인스턴스도 틀의 모듈을 물려받는다** (2026-08-30, WO-0149).
                    //   여기서만 안 적고 있었다(op 인스턴스는 적었다). 그래서 뒤의 고정점
                    //   되풀이가 이 인스턴스를 훑을 때 `callermod` 이 **비었고**, 모듈 검사가
                    //   통째로 꺼졌다 ⇒ `pagecache` 의 make 필드 이름 `held` 가 `nodelist` 의
                    //   제네릭 타입 `held` 로 접혀 `E-TYPE-FIELD` 가 났다.
                    //   ☞ *빈 값은 "모른다" 가 아니라 "검사하지 마라" 로 읽혔다.*
                    c->imod[c->ni] = sg[gi].mod;
                    // ★★ **어디서 만들어졌는지 적어 둔다** — 인스턴스 안에서 터지는 진단이
                    //   "네가 안 쓴 코드의 줄" 만 가리키지 않도록(RFC-0084 §2.3).
                    c->isite[c->ni] = k->tok.line;
                    c->ni++;
                }
            }
            k->tok.lex = in;
            (void)PROVEN_ARRAY_PUSH(&kids, low_cst_t *, k);
            i += nt;                  // 타입 인자(원자든 괄호든)를 **먹는다**
            changed = true; n++; c->nsub++;
            continue;
        }
        (void)PROVEN_ARRAY_PUSH(&kids, low_cst_t *, k);
    }
    if (changed) (void)low_refit(&c->p, nd, (low_cst_t **)kids.data, kids.len);
    proven_array_destroy(&kids);
    // ☞ 여기서 **다시 훑지 않는다**. 바깥 고정점 루프가 한 번 더 돌아 준다 — 여기서 재귀하면
    //   같은 나무를 여러 번 접으며 이름을 계속 새로 할당해 **아레나가 마른다**(그러면
    //   `mono_iname` 이 빈 이름을 내고, 헤더의 타입이 **빈 원자**가 되어 조용히 망가진다).
    return n;
}

void low_mono(low_parse_result_t *pr, proven_allocator_t node_alloc, proven_allocator_t work,
              proven_size_t *ninst, proven_size_t *nsub) {
    mono_ctx_t c = { .p = { .node_alloc = node_alloc, .work = work, .out = pr } };
    // ① 제네릭 선언을 모은다 (★ 어느 모듈의 것인지도 — 자리가 곧 소속이다)
    // ★ 틀은 **폼 하나에 최대 하나**다 — `nforms` 가 정확한 상한이고, 그만큼 잡으면
    //   "넘침" 이라는 경우가 **없어진다**.
    proven_size_t gcap = pr->nforms ? pr->nforms : 1;
    proven_result_mem_mut_t rg = work.alloc_fn(work.ctx, gcap * sizeof(mono_gen_t), alignof(mono_gen_t));
    proven_result_mem_mut_t rs = work.alloc_fn(work.ctx, gcap * sizeof(mono_struct_t), alignof(mono_struct_t));
    if (rg.err != PROVEN_OK || rs.err != PROVEN_OK) { if (ninst) *ninst = 0; if (nsub) *nsub = 0; return; }
    c.g = (mono_gen_t *)rg.value.ptr;
    mono_struct_t *sg = (mono_struct_t *)rs.value.ptr; proven_size_t ns = 0;
    proven_u8str_view_t curmod = { 0 };
    for (proven_size_t i = 0; i < pr->nforms; i++) {
        const low_cst_t *f0 = pr->forms[i];
        if (f0->kind == LOW_CST_FORM && f0->nkids >= 2 && f0->kids[0]->kind == LOW_CST_ATOM &&
            f0->kids[0]->tok.kw == LOW_KW_MODULE && f0->kids[1]->kind == LOW_CST_ATOM) {
            curmod = f0->kids[1]->tok.lex;
            continue;
        }
        {
            low_op_header_t h;
            proven_u8str_view_t tp[MONO_MAXT];
            proven_size_t nt = mono_tparams(f0, &h, tp, MONO_MAXT);
            if (nt) {
                c.g[c.ng].name = h.name; c.g[c.ng].form = f0; c.g[c.ng].mod = curmod;
                for (proven_size_t q = 0; q < nt; q++) c.g[c.ng].tparam[q] = tp[q];
                c.g[c.ng].nt = nt; c.g[c.ng].np = h.np;
                c.ng++;
                continue;
            }
        }
        // ①' 제네릭 **타입** 틀(RFC-0084) — struct 도 타입 파라미터를 받는다.
        {
            proven_u8str_view_t tp2[MONO_MAXT];
            proven_size_t nt2 = mono_struct_gen(f0, tp2, MONO_MAXT);
            if (nt2) {
                sg[ns].name = f0->kids[1]->tok.lex;
                sg[ns].mod  = curmod;
                sg[ns].form = f0;
                for (proven_size_t q = 0; q < nt2; q++) sg[ns].tparam[q] = tp2[q];
                sg[ns].nt = nt2;
                ns++;
            }
        }
    }
    if (!c.ng && !ns) { if (ninst) *ninst = 0; if (nsub) *nsub = 0; return; }

    // ② 호출 자리를 다시 쓰고 인스턴스를 만든다 (제네릭 선언 **자신**은 건드리지 않는다)
    {
        proven_u8str_view_t walkmod = { 0 };
        for (proven_size_t i = 0; i < pr->nforms; i++) {
            const low_cst_t *f0 = pr->forms[i];
            if (f0->kind == LOW_CST_FORM && f0->nkids >= 2 && f0->kids[0]->kind == LOW_CST_ATOM &&
                f0->kids[0]->tok.kw == LOW_KW_MODULE && f0->kids[1]->kind == LOW_CST_ATOM)
                walkmod = f0->kids[1]->tok.lex;      // ★ 자리가 곧 모듈 소속이다
            bool is_tpl = false;
            for (proven_size_t gi = 0; gi < c.ng; gi++) if (pr->forms[i] == c.g[gi].form) is_tpl = true;
            c.callermod = walkmod;
            if (!is_tpl) mono_rewrite(&c, pr->forms[i]);
        }
    }
    // ★★★ **작업목록(worklist)** — 방금 만든 인스턴스의 **본문도 다시 훑어야 한다.**
    //
    //   제네릭이 제네릭을 부르면(`h` 가 `g t a` 를 부른다) 그 호출은 **인스턴스 안**에 있다.
    //   인스턴스는 위 루프 **뒤에** 만들어지므로 **한 번도 안 훑겼다** ⇒ `g` 가 제네릭 이름인
    //   채로 남고, 틀이 제거된 IR 이 **E-IR-UNDEF** 를 냈다. **컴파일이 안 됐다.**
    //
    //   ⇒ 새 인스턴스가 안 나올 때까지 **되풀이한다**(고정점). 상한이 있으므로 반드시 멈춘다.
    // ★ 여기엔 상한이 필요 없다 — 한 바퀴마다 `done` 이 `c.ni` 까지 올라가고, 새 인스턴스가
    //   안 생기면 아래에서 끊는다. **진행이 곧 종료 근거**다.
    for (proven_size_t done = 0; done < c.ni; ) {
        proven_size_t n0 = c.ni;
        for (proven_size_t i = done; i < n0; i++) { c.callermod = c.imod[i]; mono_rewrite(&c, c.iform[i]); }
        done = n0;
        if (c.ni == n0) break;                 // 새 인스턴스가 안 나왔다 — 끝났다
    }

    // ②' **타입 자리**를 접는다 (RFC-0084). op 단형화가 **끝난 뒤**여야 한다 — op 인스턴스 안의
    //   `gvec t` 는 그때 비로소 `gvec u32` 가 되기 때문이다. 그리고 새로 생긴 struct 인스턴스의
    //   필드가 또 다른 제네릭 타입을 쓸 수 있으므로 **고정점까지** 되풀이한다.
    if (ns) {
        // ★ 이 되풀이는 **접기(fold)** 가 진행을 만들므로 수학적 종료 근거가 약하다.
        //   그래서 상한을 두되 **자르지 않고 말한다**: 닿으면 아래에서 거절한다.
        proven_size_t lim = pr->nforms + ns + 64, pass = 0;
        for (; pass < lim; pass++) {
            proven_size_t n0 = c.ni, folded = 0;
            proven_u8str_view_t tmod = { 0 };      // ★ 자리가 곧 모듈 소속이다(위와 같은 규율)
            for (proven_size_t i = 0; i < pr->nforms; i++) {
                const low_cst_t *f1 = pr->forms[i];
                if (f1->kind == LOW_CST_FORM && f1->nkids >= 2 && f1->kids[0]->kind == LOW_CST_ATOM &&
                    f1->kids[0]->tok.kw == LOW_KW_MODULE && f1->kids[1]->kind == LOW_CST_ATOM)
                    tmod = f1->kids[1]->tok.lex;
                c.callermod = tmod;
                bool is_tpl = false;
                for (proven_size_t q = 0; q < ns; q++) if (pr->forms[i] == sg[q].form) is_tpl = true;
                for (proven_size_t q = 0; q < c.ng; q++) if (pr->forms[i] == c.g[q].form) is_tpl = true;
                if (is_tpl) continue;
                mono_make_fix(&c, pr->forms[i], sg, ns);
                folded += mono_type_rewrite(&c, pr->forms[i], sg, ns);
            }
            for (proven_size_t i = 0; i < c.ni; i++) {
                c.callermod = c.imod[i];
                mono_make_fix(&c, c.iform[i], sg, ns);
                folded += mono_type_rewrite(&c, c.iform[i], sg, ns);
            }
            if (!folded && c.ni == n0) break;
        }
        // ★★ **닿았으면 말한다.** 전엔 조용히 나갔고, 그러면 절반만 접힌 나무가
        //   뒷단으로 흘러 *엉뚱한 진단*이 났다. 한계는 있어도 되지만 **말 없는 한계는 안 된다.**
        if (pass >= lim)
            low_pdiag(&c.p, "E-MONO-FIXPOINT",
                      "generic monomorphisation did not reach a fixed point — the type folding kept "
                      "finding new work after the pass budget. This is a compiler limit, not a mistake "
                      "in your program: report it with the unit that triggered it", 0, 0);
    }

    // ③ 새 form 목록 — **제네릭 틀은 빠지고**(타입 `t` 는 존재하지 않는다), 인스턴스가 들어온다
    //
    //   ★★★ **인스턴스는 틀이 있던 자리에 넣는다.** 끝에 몰아 붙였더니 — 최상위 form 의
    //     **순서가 곧 모듈 소속**이므로(`module m .` 선언 뒤의 것들이 그 모듈이다),
    //     인스턴스가 **마지막 모듈 소속**이 되어 버렸다. 그러면 `requires sized t .` 의
    //     `sized`(제 모듈의 trait)가 **남의 모듈 이름**으로 보여 **E-VISIBILITY**(거짓 오류).
    //   ★ **자리는 뜻이다.** 이 언어에서 순서는 장식이 아니다.
    proven_array_t nf = PROVEN_ARRAY_INIT(work, low_cst_t *, pr->nforms + c.ni).value;
    // ★★★★★ **버린 틀을 세어 둔다** — 아래 재작성은 *"틀 자신은 빠진다"* 인데,
    //   인스턴스가 0 개면 **아무것도 안 들어가** 그 폼이 통째로 사라진다.
    //   사라진 폼은 검사도 안 받으므로, 여기 남겨 이름·접근 검사가 훑게 한다.
    proven_array_t gf = PROVEN_ARRAY_INIT(work, low_cst_t *, 8).value;
    for (proven_size_t i = 0; i < pr->nforms; i++) {
        proven_size_t gi2 = c.ng;
        for (proven_size_t gi = 0; gi < c.ng; gi++) if (pr->forms[i] == c.g[gi].form) gi2 = gi;
        proven_u8str_view_t tname = { 0 };
        for (proven_size_t q = 0; q < ns; q++) if (pr->forms[i] == sg[q].form) tname = sg[q].name;
        if (tname.size) {
            // ★ struct 틀은 **빠진다**(필드 타입 `t` 는 존재하지 않는다). 인스턴스가 이 자리에 온다 —
            //   자리가 곧 모듈 소속이라, 끝에 몰면 남의 모듈 것이 된다(위 교훈과 같은 이유).
            for (proven_size_t q = 0; q < c.ni; q++) {
                proven_u8str_view_t in = c.iname[q];
                if (in.size <= tname.size) continue;
                if (!proven_u8str_view_starts_with(in, tname)) continue;   // ★ proven 뷰 어휘(2026-10-02)
                if (in.ptr[tname.size] != (proven_u8)'#') continue;
                (void)PROVEN_ARRAY_PUSH(&nf, low_cst_t *, c.iform[q]);
            }
            continue;
        }
        if (gi2 == c.ng) { (void)PROVEN_ARRAY_PUSH(&nf, low_cst_t *, pr->forms[i]); continue; }
        // 이 틀의 인스턴스들을 **바로 이 자리에** 놓는다 (틀 자신은 빠진다)
        (void)PROVEN_ARRAY_PUSH(&gf, low_cst_t *, pr->forms[i]);   // ★ 버린 틀
        for (proven_size_t q = 0; q < c.ni; q++) {
            proven_u8str_view_t in = c.iname[q];
            if (in.size <= c.g[gi2].name.size) continue;
            if (memcmp(in.ptr, c.g[gi2].name.ptr, c.g[gi2].name.size) != 0) continue;
            if (in.ptr[c.g[gi2].name.size] != (proven_u8)'#') continue;
            (void)PROVEN_ARRAY_PUSH(&nf, low_cst_t *, c.iform[q]);
        }
    }
    pr->forms = (low_cst_t **)nf.data;
    pr->nforms = nf.len;
    pr->gforms = (low_cst_t **)gf.data;
    pr->ngforms = gf.len;
    // ★ 유래를 넘긴다 — 인스턴스 안에서 터지는 진단이 **사용 자리**도 말할 수 있게.
    pr->nsites = 0;
    for (proven_size_t i = 0; i < c.ni && pr->nsites < LOW_MONO_MAXSITE; i++) {
        pr->sites[pr->nsites].name = c.iname[i];
        pr->sites[pr->nsites].line = c.isite[i];
        pr->nsites++;
    }
    if (ninst) *ninst = c.ni;
    if (nsub)  *nsub  = c.nsub;
}
