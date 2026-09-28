// low_region.c — MVP region/escape + EXCL exclusivity check (S4, liveness v2).
//
// Escape: a reference to a *local* (`var`) must not leave the op via return/give
// (E-ESCAPE). EXCL: readers-XOR-writer over locals (E-EXCL) with **last-use
// liveness** (NLL-style approximation): a borrow lives from its creation to its
// last textual use; overlapping borrows (either mutable) conflict, and an owner
// access of the borrowed local inside a borrow's live interval conflicts when
// the borrow is mutable or the access is a write. Loops are handled
// conservatively: a borrow that crosses into a `while` body conflicts with any
// owner access inside that body regardless of textual order (iteration
// wrap-around). This precision upgrade was driven by the V1 dynamic verifier
// (borrow-stack) findings. (SPEC-004 §4.4, SPEC-005 §6.3.)
#include <stdio.h>   // ★ `--emit-events` 의 방출에만 쓴다
#include "low_region.h"

#include "low_token.h"
#include "low_diag.h"

#define RG_MAX 128
#define RG_MAXOWN 256

static bool veq(proven_u8str_view_t v, const char *s) { return proven_u8str_view_eq(v, proven_u8str_view_from_cstr(s)); }
static bool is_ref_head(proven_u8str_view_t v) { return veq(v, "ref") || veq(v, "mut_ref") || veq(v, "addr"); }
static bool is_atom(const low_cst_t *n) { return n && n->kind == LOW_CST_ATOM; }

// path context: the chain of (if-id, arm) the event sits under. Two events whose
// contexts disagree on some if-id are in *mutually exclusive* branches and can
// never co-occur in one execution — so they cannot conflict (D4 fix; the
// agreement theorem's soundness argument only needs pairs that co-occur).
// Loop wrap-around is the exception: across iterations, different arms of an if
// *inside* a loop can both run — so the conservative crossing rules (S2L and the
// loop guard in S1) ignore path context.
#define RG_PATHD 8
typedef struct { proven_u16 ifid[RG_PATHD]; proven_u8 arm[RG_PATHD]; proven_u8 n; } rg_path_t;
static bool path_compat(const rg_path_t *a, const rg_path_t *b) {
    for (proven_u8 i = 0; i < a->n; i++)
        for (proven_u8 j = 0; j < b->n; j++)
            if (a->ifid[i] == b->ifid[j] && a->arm[i] != b->arm[j]) return false;
    return true;
}

#define RG_ALIAS 4
typedef struct {   // a borrow with its live interval [c, l] (statement ticks)
    proven_u8str_view_t name;    // binding name; empty for transient (call-arg) borrows
    proven_u8str_view_t alias[RG_ALIAS];   // further names bound to this same borrow
    proven_u8       nalias;                // (a borrow laundered through a call — D3)
    proven_u8str_view_t target;
    bool mut;
    proven_size_t c, l;
    rg_path_t p;                 // path context at creation
    proven_u32 line;
} rg_bor_t;
typedef struct {   // a direct (owner) access of a local
    proven_u8str_view_t target;
    proven_size_t t, ls;         // tick; innermost enclosing while-body start (0 = none)
    bool write;
    rg_path_t p;
    proven_u32 line;
} rg_own_t;
typedef struct {   // a use *through* a borrow (extends its live interval)
    proven_size_t bor, t, ls;
    rg_path_t p;
    proven_u32 line;
} rg_use_t;

typedef struct {
    proven_array_t     *diags;
    bool               *ok;
    proven_u8str_view_t locals[RG_MAX];
    proven_size_t       nlocals;
    // ★★★★★ **지역 참조를 담은 지역** (2026-08-27). `walk_escape` 는 *반환식의 생김새*만
    //   봤다 — `return`/`give` 하위나무에서 `ref`/`mut_ref`/`addr` 낱말을 찾는 식이다.
    //   그래서 참조를 지역에 **한 번 담았다가** 돌려주면 통째로 빠져나갔다:
    //       let r be ref u64 ref l .  return r .      ← 정적 초록, VM 은 트랩, 네이티브는 0
    //   그리고 그것이 **VM ≡ native 를 깼다**(이 저장소가 가장 강하게 지키는 대조).
    //   ⇒ 이름이 무엇을 **담고 있는지**를 따라간다. 고정점까지 넓힌다(두 홉·세 홉).
    //   ☞ RFC-0093(2026-08-10)의 다음 층이다: 그때는 `let` 이 목록에 없어서 같은 갈림이
    //     났고 수정은 **모으는 대상**을 고쳤다. 이번은 목록은 맞는데 **흐름**을 안 봤다.
    //     *구멍을 막을 때 "이 모양" 만 막으면 한 홉 건너에 같은 구멍이 남는다.*
    proven_u8str_view_t refholders[RG_MAX];
    proven_size_t       nrefholders;
    rg_bor_t            bors[RG_MAX];
    proven_size_t       nbors;
    rg_own_t            owns[RG_MAXOWN];
    proven_size_t       nowns;
    rg_use_t            uses[RG_MAXOWN];
    proven_size_t       nuses;
    proven_size_t       tick;
    rg_path_t           path;    // current path context (if-arm chain)
    proven_u16          next_if;
    const low_parse_result_t *pr;   // ★ 피호출자의 반환 타입을 보려면 필요하다(아래 rg_ret_is_ref)
} rg_ctx_t;

// ★★★ **이 이름이 *참조를 돌려주는* op 인가** (2026-07-19).
//   탈출 검사는 `return` 하위의 `ref <지역>` 을 전부 잡았다. 그런데 **호출 인자로 소비될 뿐인
//   참조는 반환값으로 안 새어 나간다** — `return peek (ref q)` 에서 peek 이 u64 를 돌려주면
//   그 참조는 peek 안에서 끝난다. 그것을 거절한 것은 **거짓 거절**이었다(참조가 op 경계를
//   넘게 되면서 흔한 모양이 됐다).
//   ★ 다만 피호출자가 **참조를 돌려주면** 그것은 진짜 탈출이다 ⇒ 반환 타입을 본다.
//   ☞ `ok`/`some`/`error`/`make` 같은 감싸개는 사용자 op 이 아니므로 이 함수가 못 찾고,
//     따라서 **전파를 계속한다** — `return ok (mut_ref tmp)` 는 여전히 잡힌다(그게 맞다).
static bool rg_ret_is_ref(const rg_ctx_t *c, proven_u8str_view_t name, bool *found) {
    *found = false;
    if (!c->pr) return false;
    for (proven_size_t i = 0; i < c->pr->nforms; i++) {
        const low_cst_t *f = c->pr->forms[i];
        if (!f || f->kind != LOW_CST_FORM || f->nkids < 2 || !is_atom(f->kids[0]) || !is_atom(f->kids[1])) continue;
        low_kw_t kw = f->kids[0]->tok.kw;
        if (kw != LOW_KW_FN && kw != LOW_KW_PROC) continue;
        if (!proven_u8str_view_eq(f->kids[1]->tok.lex, name)) continue;
        *found = true;
        // `output` 절의 낱말들에 ref/mut_ref/addr 가 있나
        bool in_out = false;
        for (proven_size_t j = 2; j < f->nkids; j++) {
            if (!is_atom(f->kids[j])) { if (in_out) break; continue; }
            proven_u8str_view_t w = f->kids[j]->tok.lex;
            if (low_is_clause_word(w)) { in_out = veq(w, "output"); continue; }
            if (in_out && is_ref_head(w)) return true;
        }
        return false;
    }
    return false;
}

static void rg_emit(rg_ctx_t *c, const char *code, const char *msg, proven_u32 line) {
    low_diag_t d = { .sev = LOW_SEV_ERROR, .code = code, .msg = msg, .line = line, .col = 0 };
    (void)proven_array_push(c->diags, &d);
    *c->ok = false;
}
static void rg_escape(rg_ctx_t *c, proven_u32 line) {
    rg_emit(c, "E-ESCAPE", "reference to a local escapes the op (dangling)", line);
}
static void rg_excl(rg_ctx_t *c, proven_u32 line) {
    rg_emit(c, "E-EXCL", "exclusivity violation: overlapping borrow/owner access (readers-XOR-writer)", line);
}
static bool is_local(const rg_ctx_t *c, proven_u8str_view_t v) {
    for (proven_size_t i = 0; i < c->nlocals; i++) if (proven_u8str_view_eq(c->locals[i], v)) return true;
    return false;
}
static bool is_refholder(const rg_ctx_t *c, proven_u8str_view_t v) {
    for (proven_size_t i = 0; i < c->nrefholders; i++)
        if (proven_u8str_view_eq(c->refholders[i], v)) return true;
    return false;
}

// **이 식이 지역 참조를 나르는가** — 훑되 `deref` 하위나무는 건너뛴다.
//
//   ★ 구분은 *"어디에 나오나"* 가 아니라 ***"읽는가 나르는가"*** 다.
//     · `deref r`            → **읽는다.** 결과는 값이다. 안 나른다.
//     · `ref l` · 보유자 이름 → **나른다.**
//     · `make box do . p (ref l) . end` → 필드에 담아 **나른다**(구조체가 참조를 안고 간다).
//
//   ☞ 두 번의 수정이 여기로 수렴했다. WO-0124 는 처음에 "부분나무 어디든 참조가 나오면"
//     으로 넓게 썼다가 `var x be u8 deref r .` 에 걸려 **좁혔고**, 그 좁은 규칙은 구조체
//     필드를 놓쳤다(WO-0125 사냥: 네이티브가 죽은 칸에서 4242 를 읽어 **맞아 보이는 답**을
//     냈다 — 0 을 내는 것보다 나쁘다). 답은 넓게도 좁게도 아니고 **`deref` 를 건너뛰는 것**
//     이었다. *경계를 잘못 그으면 양쪽에서 틀린다.*
static bool carries_local_ref(const rg_ctx_t *c, const low_cst_t *nd) {
    if (!nd) return false;
    // `deref …` 는 값을 낸다 — 그 안은 안 본다.
    if (nd->kind == LOW_CST_FORM && nd->nkids >= 1 && is_atom(nd->kids[0]) &&
        veq(nd->kids[0]->tok.lex, "deref")) return false;
    // ★ **참조를 안 돌려주는 사용자 op** 도 같다 — 호출이 그것을 소비한다.
    //   `set t (bump (mut_ref q))` 에서 `bump` 는 u64 를 낸다. 이걸 안 보고 `t` 를
    //   보유자로 올렸다가 `return t` 를 거짓 거절했다(vm_refarg:50).
    //   `walk_escape` 가 이미 쓰는 **같은 판정**이다 — 두 자리가 같은 규칙을 써야 한다.
    if (nd->kind == LOW_CST_FORM && nd->nkids >= 1 && is_atom(nd->kids[0])) {
        bool found; bool rr = rg_ret_is_ref(c, nd->kids[0]->tok.lex, &found);
        if (found && !rr) return false;
    }
    if (nd->kind == LOW_CST_ATOM) return is_refholder(c, nd->tok.lex);
    if (nd->kind == LOW_CST_FORM)
        for (proven_size_t j = 0; j + 1 < nd->nkids; j++)
            if (is_atom(nd->kids[j]) && is_ref_head(nd->kids[j]->tok.lex) &&
                is_atom(nd->kids[j + 1]) && is_local(c, nd->kids[j + 1]->tok.lex))
                return true;
    for (proven_size_t i = 0; i < nd->nkids; i++)
        if (carries_local_ref(c, nd->kids[i])) return true;
    return false;
}

// **펴진 꼬리가 참조를 나르는가** — `nd->kids[from..]` 를 한 식처럼 본다.
//   ★ 평평 경로에서는 `let r be ref u64 ref l` 이 `[let,r,ref,u64,be,ref,l]` 로 펴져
//     `ref l` 이 **짝으로 안 보인다**(FORM 이 아니라 형제 원자 둘이다). 나무 경로만
//     보고 고쳤다가 골든의 flat/tree 대조에 두 번 걸렸다. *같은 규칙을 두 경로에.*
static bool range_carries(const rg_ctx_t *c, const low_cst_t *nd, proven_size_t from) {
    if (from >= nd->nkids) return false;
    // 꼬리의 머리가 **소비하는 낱말**이면(값을 낸다) 여기서 끊는다.
    if (is_atom(nd->kids[from])) {
        if (veq(nd->kids[from]->tok.lex, "deref")) return false;
        bool found; bool rr = rg_ret_is_ref(c, nd->kids[from]->tok.lex, &found);
        if (found && !rr) return false;
    }
    for (proven_size_t j = from; j < nd->nkids; j++) {
        if (j + 1 < nd->nkids && is_atom(nd->kids[j]) && is_ref_head(nd->kids[j]->tok.lex) &&
            is_atom(nd->kids[j + 1]) && is_local(c, nd->kids[j + 1]->tok.lex))
            return true;
        if (carries_local_ref(c, nd->kids[j])) return true;
    }
    return false;
}

// `let/var NAME be … <식>` 에서 <식> 이 지역 참조를 낳으면 NAME 을 ref-holder 로 올린다.
static bool collect_refholders_once(rg_ctx_t *c, const low_cst_t *nd) {
    bool grew = false;
    if (!nd) return false;
    if (nd->kind == LOW_CST_FORM && nd->nkids >= 2 && is_atom(nd->kids[0]) &&
        (nd->kids[0]->tok.kw == LOW_KW_VAR || nd->kids[0]->tok.kw == LOW_KW_LET) &&
        is_atom(nd->kids[1]) && !is_refholder(c, nd->kids[1]->tok.lex) &&
        c->nrefholders < RG_MAX) {
        // `be` 뒤가 초기화식이다. 타입 낱말은 그 앞에 온다 — 거기까지 보면 안 된다.
        proven_size_t be_at = 0;
        for (proven_size_t i = 2; i < nd->nkids; i++)
            if (is_atom(nd->kids[i]) && veq(nd->kids[i]->tok.lex, "be")) { be_at = i; break; }
        if (be_at && range_carries(c, nd, be_at + 1)) {
            c->refholders[c->nrefholders++] = nd->kids[1]->tok.lex;
            grew = true;
        }
    }
    // ★ `set (field X f) <나르는 것> .` — 만든 뒤에 담는 길. 이것도 X 를 보유자로 만든다.
    //   (`set X …` 자체도 같다: 이미 있는 이름에 나르는 값을 넣는 경우.)
    if (nd->kind == LOW_CST_FORM && nd->nkids >= 3 && is_atom(nd->kids[0]) &&
        nd->kids[0]->tok.kw == LOW_KW_SET) {
        // 대상 이름을 찾는다: `set X …` 이거나 `set (field X f) …`
        const low_cst_t *tgt = nd->kids[1];
        while (tgt && tgt->kind != LOW_CST_ATOM) {
            const low_cst_t *nx = NULL;
            for (proven_size_t j = 0; j + 1 < tgt->nkids; j++)
                if (is_atom(tgt->kids[j]) && veq(tgt->kids[j]->tok.lex, "field")) {
                    nx = tgt->kids[j + 1]; break;
                }
            if (!nx && tgt->nkids) nx = tgt->kids[0];
            if (nx == tgt) break;
            tgt = nx;
        }
        if (tgt && tgt->kind == LOW_CST_ATOM && is_local(c, tgt->tok.lex) &&
            !is_refholder(c, tgt->tok.lex) && c->nrefholders < RG_MAX)
            if (range_carries(c, nd, 2)) {
                c->refholders[c->nrefholders++] = tgt->tok.lex;
                grew = true;
            }
    }
    for (proven_size_t i = 0; i < nd->nkids; i++)
        if (collect_refholders_once(c, nd->kids[i])) grew = true;
    return grew;
}

// 고정점 — 한 바퀴로는 `a be ref l . b be a . return b .` 를 못 잡는다.
// (RG_MAX 바퀴면 반드시 멈춘다: 바퀴마다 집합이 최소 하나 자란다.)
static void collect_refholders(rg_ctx_t *c, const low_cst_t *nd) {
    for (proven_size_t round = 0; round < RG_MAX; round++)
        if (!collect_refholders_once(c, nd)) break;
}

static bool bor_named(const rg_bor_t *b, proven_u8str_view_t name) {
    if (b->name.size && proven_u8str_view_eq(b->name, name)) return true;
    for (proven_u8 i = 0; i < b->nalias; i++)
        if (proven_u8str_view_eq(b->alias[i], name)) return true;
    return false;
}
static rg_bor_t *bor_find(rg_ctx_t *c, proven_u8str_view_t name) {
    if (!name.size) return NULL;
    for (proven_size_t i = c->nbors; i-- > 0; )
        if (bor_named(&c->bors[i], name)) return &c->bors[i];
    return NULL;
}

// collect `var NAME …` **and `let NAME …`** names declared anywhere in the body
// ★★★★★ **`let` 은 이 검사기에 보이지 않았다** (RFC-0093, 2026-08-10).
//   여기서 `var` 만 모았다. 그런데 지역이 지역인 것은 **바뀌느냐가 아니라 언제 죽느냐**다 —
//   `let` 도 op 이 끝나면 함께 죽는다. 그래서:
//       var l be u64 5 . return ref l .   →  E-ESCAPE  (잡힌다)
//       let l be u64 5 . return ref l .   →  check: ok (샌다)
//   그리고 그 프로그램을 실제로 돌리면 **두 뒤끝이 갈렸다**:
//   VM 은 `E-VM-DANGLING` 으로 트랩하고, **네이티브는 스택 주소를 값으로 돌려줬다**
//   (실측 140733983170928). 이 저장소가 가장 강하게 지키는 VM ≡ native 가 깨지는 자리다.
//   ☞ **같은 실수가 다른 검사기에서 되풀이됐다.** `low_typecheck.c` 에도 똑같은 자국이 있다:
//     *"`let` 은 타입체크를 통째로 빠져나가고 있었다 — `var` 만 검사됐다."*
//     한 검사기에서 배운 것을 **다른 검사기에 옮겨 적지 않으면** 같은 구멍이 다시 난다.
//   ★ 그리고 이 구멍은 **더 흔한 쪽**에 나 있었다: 이 언어는 `let` 이 기본이고 `var` 는
//     바꿀 때만 쓴다. 즉 검사기가 **덜 쓰이는 철자만** 지키고 있었다.
static void collect_locals(rg_ctx_t *c, const low_cst_t *nd) {
    if (!nd) return;
    if (nd->kind == LOW_CST_FORM && nd->nkids >= 2 && is_atom(nd->kids[0]) &&
        (nd->kids[0]->tok.kw == LOW_KW_VAR || nd->kids[0]->tok.kw == LOW_KW_LET) &&
        is_atom(nd->kids[1]) && c->nlocals < RG_MAX)
        c->locals[c->nlocals++] = nd->kids[1]->tok.lex;
    for (proven_size_t i = 0; i < nd->nkids; i++) collect_locals(c, nd->kids[i]);
}

// ── escape: once inside a return/give subtree, flag `ref/mut_ref/addr LOCAL` ──
static void walk_escape(rg_ctx_t *c, const low_cst_t *nd, bool in_return) {
    if (!nd) return;
    bool ret = in_return;
    if (nd->kind == LOW_CST_FORM && nd->nkids >= 1 && is_atom(nd->kids[0]) &&
        (nd->kids[0]->tok.kw == LOW_KW_RETURN || nd->kids[0]->tok.kw == LOW_KW_GIVE)) {
        ret = true;
        // ★★★ **평평 경로도 같은 답을 내야 한다** (2026-07-19 — 골든의 flat/tree 대조가 잡았다).
        //   나무 경로에서는 `(peek (ref q))` 가 중첩 폼이라 아래 머리 검사가 걸리지만,
        //   평평 경로에서는 kids 가 [return, peek, ref, q] 로 **펴져** 중첩이 없다.
        //   ⇒ 반환 바로 뒤 낱말이 **참조를 안 돌려주는 사용자 op** 이면 여기서 전파를 끊는다.
        //   ★ `return ref q` 는 kids[1] 이 `ref` 라 op 이 아니고, `return ok (mut_ref tmp)` 는
        //     `ok` 가 사용자 op 이 아니다 ⇒ 둘 다 **여전히 잡힌다**(그게 맞다).
        if (nd->nkids >= 2 && is_atom(nd->kids[1])) {
            bool found2; bool rr2 = rg_ret_is_ref(c, nd->kids[1]->tok.lex, &found2);
            if (found2 && !rr2) ret = false;
            // ★ 평평 경로의 `deref` 도 같다 — `[return, deref, r]` 로 펴지면 아래 머리
            //   검사(kids[0])가 `return` 을 보므로 `deref` 를 못 본다. 골든의 flat/tree
            //   대조가 이것을 잡았다("나무에서 검사가 침묵한다"). *같은 규칙을 두 경로에
            //   각각 적어야 한다 — 한쪽만 적으면 그 차이가 곧 결함이다.*
            if (veq(nd->kids[1]->tok.lex, "deref")) ret = false;
        }
    }
    // ★ 머리가 **참조를 안 돌려주는 사용자 op** 이면, 그 인자 안의 참조는 반환값으로 흐르지
    //   않는다 — 호출이 그것을 소비한다. ⇒ 여기서 전파를 끊는다(거짓 거절 제거).
    if (ret && nd->kind == LOW_CST_FORM && nd->nkids >= 1 && is_atom(nd->kids[0])) {
        bool found; bool rr = rg_ret_is_ref(c, nd->kids[0]->tok.lex, &found);
        if (found && !rr) ret = false;
        // ★ `deref` 는 **값**을 낸다 — 참조를 밖으로 나르지 않는다. 위 규칙이 사용자 op 에
        //   대해 하는 일을 붙박이 낱말에도 해 준다. (안 하면 `return deref r .` 이 거짓
        //   거절이 된다 — 실측: vm_tgroup_ref.low 36 행.)
        //   *"참조를 읽는 것" 과 "참조를 나르는 것" 은 다르다* — 이 파일에서 두 번째로
        //   같은 구분을 놓쳤다(앞은 `var x be u8 deref r .`).
        if (veq(nd->kids[0]->tok.lex, "deref")) ret = false;
    }
    if (ret && nd->kind == LOW_CST_FORM)
        for (proven_size_t j = 0; j + 1 < nd->nkids; j++)
            if (is_atom(nd->kids[j]) && is_ref_head(nd->kids[j]->tok.lex) &&
                is_atom(nd->kids[j + 1]) && is_local(c, nd->kids[j + 1]->tok.lex))
                rg_escape(c, nd->kids[j]->line);
    // ★ 그리고 **이름이 담고 있는 것**도 본다 — `let r be ref u64 ref l . return r .`
    //   여기서 반환식은 그냥 `r` 이라 위의 생김새 검사가 아무것도 못 봤다.
    if (ret && nd->kind == LOW_CST_ATOM && is_refholder(c, nd->tok.lex))
        rg_escape(c, nd->line);
    for (proven_size_t i = 0; i < nd->nkids; i++) walk_escape(c, nd->kids[i], ret);
}

// ── EXCL v2: event collection ─────────────────────────────────────────────────
// One walk records three event kinds per statement tick:
//   borrow creation (bound by var/let, or transient in a call argument),
//   borrow USE (any mention of the binding name → extends its interval),
//   owner access (any other mention of a borrowed local; write when it is the
//   set place). `ref X` positions are borrow events, never owner accesses.

// ★★★ **`make … do … end` 는 제어 블록이 아니라 값이다.**
//   아래 두 walker 는 중첩 블록을 **통째로 건너뛰었다** — 제어 블록(if/while/for/…)의 본문을
//   두 번 세지 않으려는 것이었다. 그런데 `make` 의 블록은 **값 블록**이다: 그 안의 읽기는
//   지금 이 문장의 읽기다. 건너뛰니 **보이지 않았다.**
//     var r0 be mut_ref u64 mut_ref v0 .          rem v0 을 빌린다
//     var q be s make s do  b add v0 23 .  end    rem ★ v0 을 **읽는다** — 안 보였다
//     set r0 49 .                                    rem 그리고 빌린 곳에 쓴다 → EXCL 위반
//   **정적으로 초록불이었고 런타임에 트랩했다**(E-VM-EXCL). 퍼저가 찾았다 —
//   그리고 퍼저가 그걸 찾을 수 있었던 것은, 그 직전에 **파서가 삼키던 문장들을 되살렸기**
//   때문이다(머리 붙은 블록이 form 을 닫게 고쳤다). 결함 하나가 다음 결함을 데려온다.
static bool rg_is_control_head(const low_cst_t *f) {
    if (!f || f->kind != LOW_CST_FORM || !f->nkids || f->kids[0]->kind != LOW_CST_ATOM) return false;
    low_kw_t k = f->kids[0]->tok.kw;
    return k == LOW_KW_IF || k == LOW_KW_WHILE || k == LOW_KW_FOR || k == LOW_KW_MATCH ||
           k == LOW_KW_CASE || k == LOW_KW_ELSE || k == LOW_KW_GUARD;
}

// scan a statement subtree for uses/owner accesses — **값 블록에는 들어간다**
static void excl2_mentions(rg_ctx_t *c, const low_cst_t *nd, proven_size_t ls,
                           proven_u8str_view_t write_place, const low_cst_t *skip_upto) {
    if (!nd || nd->kind == LOW_CST_BLOCK) return;
    if (nd->kind == LOW_CST_FORM || nd->kind == LOW_CST_GROUP || nd->kind == LOW_CST_ACCESS) {
        bool ctrl = rg_is_control_head(nd);
        for (proven_size_t j = 0; j < nd->nkids; j++) {
            const low_cst_t *k = nd->kids[j];
            if (k->kind == LOW_CST_BLOCK) {          // ★ 값 블록(make)이면 들어간다
                if (ctrl) continue;                  //   제어 블록은 문장 walker 가 따로 본다
                for (proven_size_t q = 0; q < k->nkids; q++)
                    excl2_mentions(c, k->kids[q], ls, write_place, skip_upto);
                continue;
            }
            if (!is_atom(k)) { excl2_mentions(c, k, ls, write_place, skip_upto); continue; }
            if (k == skip_upto) continue;   // declaration-position name (var N …)
            if (is_ref_head(k->tok.lex) && j + 1 < nd->nkids && is_atom(nd->kids[j + 1])) {
                j++;   // `ref X` — a borrow event, handled by the caller; skip X here
                continue;
            }
            // borrow use — a name may alias SEVERAL borrows (D3: laundered through a
            // call with more than one reference argument); extend every candidate.
            bool is_use = false;
            for (proven_size_t bi = 0; bi < c->nbors; bi++) {
                if (!bor_named(&c->bors[bi], k->tok.lex)) continue;
                is_use = true;
                c->bors[bi].l = c->tick;
                if (c->nuses < RG_MAXOWN)
                    c->uses[c->nuses++] = (rg_use_t){ .bor = bi, .t = c->tick, .ls = ls,
                                                      .p = c->path, .line = k->line };
            }
            if (is_use) continue;
            if (is_local(c, k->tok.lex) && c->nowns < RG_MAXOWN) {
                c->owns[c->nowns++] = (rg_own_t){ .target = k->tok.lex, .t = c->tick, .ls = ls,
                                                  .write = k->tok.lex.size && write_place.size &&
                                                           proven_u8str_view_eq(k->tok.lex, write_place),
                                                  .p = c->path, .line = k->line };
            }
        }
    }
}

// register borrow-creating mentions (`ref X` pairs) in a value subtree; the
// first one at `head` (if bound_name is set) becomes the named borrow
static void excl2_borrows(rg_ctx_t *c, const low_cst_t *nd, proven_u8str_view_t *bound_name) {
    if (!nd || nd->kind == LOW_CST_BLOCK) return;
    if (nd->kind == LOW_CST_FORM || nd->kind == LOW_CST_GROUP) {
        if (!rg_is_control_head(nd))                 // ★ 값 블록 안의 빌림도 빌림이다
            for (proven_size_t j = 0; j < nd->nkids; j++)
                if (nd->kids[j]->kind == LOW_CST_BLOCK)
                    for (proven_size_t q = 0; q < nd->kids[j]->nkids; q++)
                        excl2_borrows(c, nd->kids[j]->kids[q], bound_name);
        // ★ 나무가 서면 `ref x` 는 `(ref x)` 가 된다 — 창을 통해 **평평하게** 본다.
        //   평평한 입력에는 **항등**이므로 옛 동작이 그대로다(구성상 보장).
        const low_cst_t *fk[128];
        proven_size_t fn = low_flat_kids(nd, fk, 128);
        for (proven_size_t j = 0; j + 1 < fn; j++)
            if (is_atom(fk[j]) &&
                (veq(fk[j]->tok.lex, "ref") || veq(fk[j]->tok.lex, "mut_ref")) &&
                is_atom(fk[j + 1]) && is_local(c, fk[j + 1]->tok.lex) && c->nbors < RG_MAX) {
                rg_bor_t *b = &c->bors[c->nbors++];
                b->name = bound_name ? *bound_name : (proven_u8str_view_t){ 0 };
                b->target = fk[j + 1]->tok.lex;
                b->mut = veq(fk[j]->tok.lex, "mut_ref");
                b->c = b->l = c->tick;
                b->p = c->path;
                b->line = fk[j]->line;
                if (!b->name.size && c->nuses < RG_MAXOWN)   // transient (call-arg) borrow:
                    c->uses[c->nuses++] = (rg_use_t){ .bor = c->nbors - 1, .t = c->tick,   // created and
                                                      .ls = 0, .p = c->path,               // consumed at
                                                      .line = b->line };                   // the same tick
                if (bound_name) bound_name = NULL;   // only the first is the binding
            }
    }
    // ★ 정규화가 **만든** 괄호는 위의 평평한 창이 **이미 폈다** — 다시 들어가면 **두 번 센다.**
    //   (그래서 E-EXCL 이 한 줄에서 다섯 번 나왔다. 창과 재귀가 같은 자리를 겹쳐 봤다.)
    for (proven_size_t i = 0; i < nd->nkids; i++)
        if (!(nd->kids[i]->kind == LOW_CST_GROUP && nd->kids[i]->synth))
            excl2_borrows(c, nd->kids[i], bound_name);
}

static void excl2_if(rg_ctx_t *c, const low_cst_t *f, proven_size_t ls);

// does the binding's declared type spell a reference? (`var t be mut_ref u32 …`)
static bool rg_decl_is_ref(const low_cst_t *f, proven_size_t start, proven_size_t end) {
    for (proven_size_t i = start; i < end && i < f->nkids; i++)
        if (is_atom(f->kids[i]) && is_ref_head(f->kids[i]->tok.lex)) return true;
    return false;
}

static proven_size_t rg_kw_index(const low_cst_t *f, low_kw_t kw) {
    for (proven_size_t i = 0; i < f->nkids; i++)
        if (is_atom(f->kids[i]) && f->kids[i]->tok.kw == kw) return i;
    return f->nkids;
}

static void excl2_block(rg_ctx_t *c, const low_cst_t *blk, proven_size_t ls) {
    if (!blk) return;
    for (proven_size_t i = 0; i < blk->nkids; i++) {
        const low_cst_t *f = blk->kids[i];
        if (f->kind != LOW_CST_FORM || f->nkids == 0 || !is_atom(f->kids[0])) continue;
        c->tick++;
        low_kw_t kw = f->kids[0]->tok.kw;

        if ((kw == LOW_KW_VAR || kw == LOW_KW_LET) && f->nkids >= 2 && is_atom(f->kids[1])) {
            proven_u8str_view_t name = f->kids[1]->tok.lex;
            const low_cst_t *vf = f;
            proven_size_t be = rg_kw_index(f, LOW_KW_BE);
            proven_size_t vstart = (be < f->nkids) ? be + 1 : 2;
            if (be == f->nkids) {   // reader-split binding: value is the `be`-headed sibling
                for (proven_size_t j = i + 1; j < blk->nkids && j <= i + 3; j++) {
                    const low_cst_t *nx = blk->kids[j];
                    if (nx->kind != LOW_CST_FORM || nx->nkids < 2 || !is_atom(nx->kids[0])) break;
                    if (nx->kids[0]->tok.kw != LOW_KW_BE) continue;
                    vf = nx; vstart = 1; i = j;
                    break;
                }
            }
            // borrow creation? first value token is a ref head on a local
            proven_u8str_view_t *bind = NULL;
            if (vstart < vf->nkids && is_atom(vf->kids[vstart]) && is_ref_head(vf->kids[vstart]->tok.lex))
                bind = &name;
            for (proven_size_t j = vstart; j < vf->nkids; j++) {
                if (vf->kids[j]->kind == LOW_CST_GROUP && vf->kids[j]->synth) continue;  // 창이 이미 폈다
                excl2_borrows(c, vf->kids[j], bind);
                if (bind && c->nbors && c->bors[c->nbors - 1].c == c->tick &&
                    proven_u8str_view_eq(c->bors[c->nbors - 1].name, name)) bind = NULL;
            }
            // flat `ref X` pair directly among the value operands
            // ★ 창을 통해 본다 — 나무에서 `(ref x)` 가 되어도 같은 열이다.
            const low_cst_t *vk[128];
            proven_size_t vn = low_flat_kids(vf, vk, 128);
            for (proven_size_t j = vstart; j + 1 < vn; j++)
                if (is_atom(vk[j]) &&
                    (veq(vk[j]->tok.lex, "ref") || veq(vk[j]->tok.lex, "mut_ref")) &&
                    is_atom(vk[j + 1]) && is_local(c, vk[j + 1]->tok.lex) && c->nbors < RG_MAX) {
                    rg_bor_t *b = &c->bors[c->nbors++];
                    b->name = (j == vstart) ? name : (proven_u8str_view_t){ 0 };
                    b->target = vk[j + 1]->tok.lex;
                    b->mut = veq(vk[j]->tok.lex, "mut_ref");
                    b->c = b->l = c->tick;
                    b->p = c->path;
                    b->line = vk[j]->line;
                }
            // a zero-copy view borrows its source slice (RFC-0025 §6.6 EXCL):
            // `view T s` / `try_view T s` / `view_array T s` with a local s
            //
            // ★★★★ **저자의 괄호까지 펴서 본다** (2026-09-05). 위의 얕은 창은 정규화가 만든
            //   괄호만 편다. 그래서 `var h be T (view T s) .` — 저자가 괄호를 친 것 — 에서
            //   빌림을 **못 봤고, 검사가 조용히 안 돌았다.** 그리고 `--fmt` 은 호출을 언제나
            //   괄호로 찍으므로 **서식을 한 번 돌리면 E-EXCL 이 사라졌다**(`vm_stale.low` 가
            //   거절되던 픽스처인데 서식 뒤 초록이 됐다 — 서식이 뜻을 바꾼 것이다).
            // ★ **여기(view 묶기)에만** 깊은 창을 쓴다. 위의 `ref`/`mut_ref` **쌍** 스캐너까지
            //   깊게 보면 `set t (bump (mut_ref q)) .` 처럼 **호출에 소비될 뿐인** 임시 빌림을
            //   묶인 빌림으로 오해해 거짓 거절이 난다(실측: `vm_refarg.low` 가 그렇게 걸렸다).
            //   ☞ *창을 넓히는 것은 공짜가 아니다 — 넓힌 창으로 무엇이 새로 보이는지 재고,
            //     본 것이 전부 참인 자리에만 넓힌다.*
            const low_cst_t *vkd[128];
            proven_size_t vnd = low_flat_kids_deep(vf, vkd, 128);
            for (proven_size_t j = vstart; j + 2 < vnd; j++)
                if (is_atom(vkd[j]) &&
                    (veq(vkd[j]->tok.lex, "view") || veq(vkd[j]->tok.lex, "try_view") ||
                     veq(vkd[j]->tok.lex, "view_array")) &&
                    is_atom(vkd[j + 2]) && is_local(c, vkd[j + 2]->tok.lex) && c->nbors < RG_MAX) {
                    rg_bor_t *b = &c->bors[c->nbors++];
                    b->name = name;                       // the view binding keeps it live
                    b->target = vkd[j + 2]->tok.lex;
                    b->mut = false;                       // views are shared borrows
                    b->c = b->l = c->tick;
                    b->p = c->path;
                    b->line = vkd[j]->line;
                }
            // uses / owner accesses (ref-pair positions are skipped inside)
            proven_size_t uses_before = c->nuses;
            excl2_mentions(c, vf, ls, (proven_u8str_view_t){ 0 }, f->kids[1]);

            // D3 — a borrow LAUNDERED through an op: `var t be mut_ref T f r .`
            // E-ESCAPE guarantees a returned reference can only derive from an
            // argument (it may not point at a callee local), so binding a
            // reference-typed result makes `t` an ALIAS of every borrow the call
            // consumed — conservative when several are passed. (ESC-INV joint,
            // agreement theorem §6 D3.)
            if (!bind && rg_decl_is_ref(f, 2, (be < f->nkids) ? be : f->nkids)) {
                for (proven_size_t bi = 0; bi < c->nbors; bi++) {
                    bool touched = c->bors[bi].c == c->tick;   // transient arg borrow
                    for (proven_size_t u = uses_before; u < c->nuses && !touched; u++)
                        if (c->uses[u].bor == bi) touched = true;
                    if (touched && c->bors[bi].nalias < RG_ALIAS)
                        c->bors[bi].alias[c->bors[bi].nalias++] = name;
                }
            }
            continue;
        }

        // set place: a borrow name = borrow use (write-through); a local = owner write
        proven_u8str_view_t write_place = { 0 };
        if (kw == LOW_KW_SET && f->nkids >= 2 && is_atom(f->kids[1]) && !bor_find(c, f->kids[1]->tok.lex))
            write_place = f->kids[1]->tok.lex;
        excl2_mentions(c, f, ls, write_place, f->kids[0]);
        excl2_borrows(c, f, NULL);   // transient borrows in call arguments

        // nested blocks. `if` arms are mutually exclusive → each opens a distinct
        // path context (D4); while/for bodies open a loop span (no exclusivity).
        if (kw == LOW_KW_IF) {
            excl2_if(c, f, ls);
            continue;
        }
        for (proven_size_t j = 0; j < f->nkids; j++)
            if (f->kids[j]->kind == LOW_CST_BLOCK)
                excl2_block(c, f->kids[j], (kw == LOW_KW_WHILE || kw == LOW_KW_FOR) ? c->tick : ls);
    }
}

// if / else-if / else — each arm gets its own (if-id, arm) frame. Nested else-if
// forms are traversed too (previously their bodies were skipped entirely: the
// block loop only recursed into BLOCK kids, so an `else if` body's borrows and
// owner accesses were invisible to EXCL — a false-negative fixed here).
static void excl2_if(rg_ctx_t *c, const low_cst_t *f, proven_size_t ls) {
    proven_u16 id = c->next_if++;
    proven_u8 arm = 0;
    bool deep = c->path.n >= RG_PATHD;
    for (proven_size_t j = 0; j < f->nkids; j++) {
        const low_cst_t *k = f->kids[j];
        if (k->kind == LOW_CST_BLOCK) {
            if (!deep) {
                c->path.ifid[c->path.n] = id;
                c->path.arm[c->path.n] = arm;
                c->path.n++;
            }
            excl2_block(c, k, ls);
            if (!deep) c->path.n--;
            arm++;
        } else if (k->kind == LOW_CST_FORM && k->nkids && is_atom(k->kids[0]) &&
                   k->kids[0]->tok.kw == LOW_KW_IF) {   // `else if …`
            if (!deep) {
                c->path.ifid[c->path.n] = id;
                c->path.arm[c->path.n] = arm;
                c->path.n++;
            }
            c->tick++;
            excl2_mentions(c, k, ls, (proven_u8str_view_t){ 0 }, k->kids[0]);
            excl2_if(c, k, ls);
            if (!deep) c->path.n--;
            arm++;
        }
    }
}

// A conflict needs two events that can co-occur in ONE execution. Mutually
// exclusive if-arms never do (path_compat) — except across loop iterations, where
// a borrow that spans the loop can meet an owner access from any arm; those pairs
// keep the old context-insensitive treatment (loop guards below).
static void excl2_conflicts(rg_ctx_t *c) {
    // borrow vs borrow: a use of the older borrow at/after the newer one's
    // creation (= interval overlap), on one target, either mutable
    for (proven_size_t i = 0; i < c->nbors; i++)
        for (proven_size_t j = i + 1; j < c->nbors; j++) {
            const rg_bor_t *a = &c->bors[i], *b = &c->bors[j];
            if (!proven_u8str_view_eq(a->target, b->target)) continue;
            if (!(a->mut || b->mut)) continue;
            for (proven_size_t u = 0; u < c->nuses; u++) {
                const rg_use_t *uu = &c->uses[u];
                if (uu->bor != i || uu->t < b->c) continue;
                bool loop_cross = uu->ls > 0 && b->c < uu->ls;   // may re-meet across iterations
                if (loop_cross || path_compat(&uu->p, &b->p)) { rg_excl(c, b->line); break; }
            }
        }
    // owner access vs borrow: an owner access strictly inside the borrow's life,
    // i.e. after its creation and before a use that can co-occur with the access
    for (proven_size_t o = 0; o < c->nowns; o++)
        for (proven_size_t i = 0; i < c->nbors; i++) {
            const rg_own_t *w = &c->owns[o];
            const rg_bor_t *b = &c->bors[i];
            if (!proven_u8str_view_eq(w->target, b->target)) continue;
            if (!(b->mut || w->write)) continue;
            if (w->ls > 0 && b->c < w->ls && b->l >= w->ls) {   // borrow spans into the loop
                rg_excl(c, w->line);                            // (S2L — order-free, path-free)
                continue;
            }
            if (w->t <= b->c) continue;
            for (proven_size_t u = 0; u < c->nuses; u++) {
                const rg_use_t *uu = &c->uses[u];
                if (uu->bor != i || uu->t < w->t) continue;
                if (path_compat(&uu->p, &w->p)) { rg_excl(c, w->line); break; }
            }
        }
}

// ★ 방출 스위치 — `--emit-events` 가 켠다. 기본은 꺼져 있다(도구의 기본 출력을 바꾸지 않는다).
static bool g_emit_events = false;
void low_region_set_emit_events(bool on) { g_emit_events = on; }
static bool low_region_emit_events(void) { return g_emit_events; }

low_region_result_t low_region(proven_allocator_t work, const low_parse_result_t *pr) {
    low_region_result_t out = { .ok = true };
    proven_result_array_t da = PROVEN_ARRAY_INIT(work, low_diag_t, 8);
    if (da.err != PROVEN_OK) { out.ok = false; return out; }
    out.diags = da.value;
    rg_ctx_t c = { .diags = &out.diags, .ok = &out.ok, .pr = pr };

    for (proven_size_t i = 0; i < pr->nforms; i++) {
        const low_cst_t *f = pr->forms[i];
        if (f->kind != LOW_CST_FORM || f->nkids < 2 || !is_atom(f->kids[0])) continue;
        low_kw_t kw = f->kids[0]->tok.kw;
        if (kw != LOW_KW_FN && kw != LOW_KW_PROC) continue;
        const low_cst_t *body = (f->kids[f->nkids - 1]->kind == LOW_CST_BLOCK) ? f->kids[f->nkids - 1] : NULL;
        c.nlocals = 0;
        collect_locals(&c, body);
        collect_refholders(&c, body);   // ★ 이름이 담은 것까지 — 고정점(2026-08-27)
        walk_escape(&c, body, false);
        c.nbors = 0; c.nowns = 0; c.tick = 0;
        excl2_block(&c, body, 0);
        excl2_conflicts(&c);
        // ★★★ **모델의 입력을 IR 에서 뽑는다** (2026-07-31 · 16장 ⑥′ 의 "손이 닿는 것" 마지막 줄).
        //   유계 전수 모델 체크(`run_tests.c`)는 **합성한** 이벤트 열을 돌린다 — 그래서
        //   *"모델이 옳다"* 는 말하지만 *"컴파일러가 그 모델대로 본다"* 는 말하지 못한다.
        //   ⇒ **진짜 프로그램**에서 차용 이벤트를 뽑아 같은 모델에 먹이면, 그 간극이
        //     **함수 하나**(이 방출)로 줄어든다. 그것이 이 출력의 전부다.
        //   형식: `E <op> C <지역> <mut> <tick>` · `U <op> <차용> <wr> <tick>` · `O <op> <지역> <wr> <tick>`
        //   ★ 판정하지 않는다 — **사실만 낸다.** 판정은 모델(스크립트)이 한다.
        if (low_region_emit_events()) {
            for (proven_size_t bi = 0; bi < c.nbors; bi++)
                printf("E %.*s C %.*s %d %zu\n",
                       (int)f->kids[1]->tok.lex.size, f->kids[1]->tok.lex.ptr,
                       (int)c.bors[bi].target.size, c.bors[bi].target.ptr,
                       c.bors[bi].mut ? 1 : 0, (size_t)c.bors[bi].c);
            for (proven_size_t ui = 0; ui < c.nuses; ui++)
                printf("U %.*s %zu %d %zu\n",
                       (int)f->kids[1]->tok.lex.size, f->kids[1]->tok.lex.ptr,
                       (size_t)c.uses[ui].bor, 0, (size_t)c.uses[ui].t);
            for (proven_size_t oi = 0; oi < c.nowns; oi++)
                printf("O %.*s %.*s %d %zu\n",
                       (int)f->kids[1]->tok.lex.size, f->kids[1]->tok.lex.ptr,
                       (int)c.owns[oi].target.size, c.owns[oi].target.ptr,
                       c.owns[oi].write ? 1 : 0, (size_t)c.owns[oi].t);
        }
    }
    return out;
}
