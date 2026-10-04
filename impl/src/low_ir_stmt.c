/* low_ir_stmt.c — 문(statement) 하강.
 *
 * ★ `low_ir.c` 에서 **떼어 왔다** (2026-08-31, WO-0165 · X-0011). 뜻은 한 줄도 안 바꿨다.
 *   증명은 `scripts/check-emit-identical.py`(코퍼스 342 단위 `--emit-c`·`--ir` 바이트 동일).
 *
 * ★★ 왜 여기인가. 남은 구획의 경계를 다시 재니(실측):
 *       식 하강  2,416 줄 · 나가는 **20** · 들여오는 19   ← 양방향, 잘라도 헤더만 두꺼워진다
 *       **문 하강 3,441 줄 · 나가는 0 · 들여오는 28**      ← **층이 한 방향**이다
 *   문은 식을 부르지만 **식은 문을 안 부른다**. 그 한 방향이 이 선의 근거다.
 *
 * ★★★ 들여오는 28 은 대부분 **공용 어휘**다(`veq`·`is_atom`·`ir_emit`…). `low_ir_priv.h` 가
 *   어휘와 **진짜 결합**을 나누어 적는다 — 짧아야 하는 것은 결합이다.
 */
#include "low_ir_priv.h"

#include "low_hwm.h"
#include "low_ir.h"
#include "low_ir_priv.h"
#include "low_arity.h"
#include <limits.h>
#include <stdarg.h>   // ★ 증명 운반 검사(cert_put)의 가변 인자
#include <math.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "low_hostfault.inc"
#include <dirent.h>     // ★ 디렉터리 순회 (RFC-0069 §6 — opendir/readdir/closedir)
#include "low_sha256.h"
#include "low_sha512.h"        // ★ SHA-256 **한 벌** — VM 은 컴파일, 백엔드는 문자열화
#include "proven_sys_random.h" // ★ 난수 리프 (RFC-0090 N3b) — OS 엔트로피 한 자리
#include "proven_sys_time.h"   // ★ 시계 리프 (RFC-0090 N1) — proven_c_lib 이 이미 준다
#include <sys/socket.h> // ★ 소켓 리프 (socketpair/send/recv — cap net)
#include <netinet/in.h> // ★ 네트워크 면 (sockaddr_in · TCP loopback)
#include <arpa/inet.h>  // ★ htons/htonl
#include <unistd.h>     // ★ close (소켓/파일 fd)
#include <sys/stat.h>   // ★ 파일 타입 질의 (RFC-0069 §6 — stat/S_ISDIR/S_ISREG)
#include <errno.h>      // ★ readdir 의 끝(NULL·errno==0) vs 오류(NULL·errno!=0) 를 가른다
#include <termios.h>    // ★ cap tty — raw 모드(에코·행버퍼 끄기)
#include <sys/ioctl.h>  // ★ cap tty — 화면 크기(TIOCGWINSZ)
#include <ucontext.h>   // ★★★ green thread — 태스크 중단(yield)을 위한 코루틴(VM 오라클 전용)
#include "low_token.h"
#include "low_diag.h"
#include "low_blake3.h"
#include "low_smt.h"    /* ★ REQ-0004: 손으로 넣던 관계 특수경우들을 **일반 절차**로 (후속 M) */

// ── statement lowering ────────────────────────────────────────────────────────

 void ir_block(ir_ctx_t *c, const low_cst_t *blk);
static void ir_stmt_inner(ir_ctx_t *c, const low_cst_t *f);

static proven_size_t form_block_index(const low_cst_t *f) {
    for (proven_size_t i = 0; i < f->nkids; i++)
        if (f->kids[i]->kind == LOW_CST_BLOCK) return i;
    return f->nkids;
}

// ★★ RFC-0135 S2 (D11) — 걸린 돌려주기를 깊이 `depth` 까지 넣는다(안쪽부터 — 선언의 거꾸로). 받지 못한 것(none)은 건너뛴다.
static void ir_release_one(ir_ctx_t *c, proven_size_t d);
static void ir_release_down_to(ir_ctx_t *c, proven_size_t depth) {
    for (proven_size_t d = g_nrel; d-- > depth; ) ir_release_one(c, d);
}
static void ir_release_one(ir_ctx_t *c, proven_size_t d) {
    ir_emit(c, IRW_LOAD, (proven_i64)g_relslot[d]);
    ir_emit(c, IRW_HASVAL, 0);
    proven_size_t skip = ir_emit(c, IRW_BRZ, 0);
    // 폼은 [send <출처> release (some_value <이름>)] — <이름> 자리에서 받은 그대로의 바이트(슬롯)를 읽는다
    const low_cst_t *rg = g_relform[d]->kids[3];
    while (rg && rg->kind == LOW_CST_GROUP && rg->nkids == 1) rg = rg->kids[0];
    g_relsub_atom = (rg && rg->kind == LOW_CST_FORM && rg->nkids == 2) ? rg->kids[1] : nullptr;
    g_relsub_slot = g_relslot[d];
    ir_node(c, g_relform[d]);
    g_relsub_atom = nullptr;
    ir_emit(c, IRW_DROP, 0);                         // release 는 돌려받았는지(bool)를 답한다 — 여기서는 쓰지 않는다
    ir_at(c, skip)->a = (proven_i64)c->code.len;
}
// ★★ RFC-0132 P1 (§8.1 · §8.2) — `for` 머리의 새 원천. 머리가 읽는 것(끝 · step · 원천 슬라이스)은 들어갈 때 한 번 계산해 숨은
//   지역에 얼린다. `range` 는 넘침 없는 내림(§8.2): 거리 = st>0 ? hi−i : i−lo(감싸는 64 비트 뺄셈), 거리 < |st|(부호 없는
//   비교)면 끝, 아니면 i += st. `count τ n` 은 [0, n) — i < n 을 τ 의 비교로. `for x mut buf` 는 x 를 buf 의 칸으로 바꿔 읽는다.
// 몸 — `where c .` 가 있으면 몸 첫머리에서 거짓이면 다음 바퀴로(continue 와 같은 자리)
static void ir_for_body(ir_ctx_t *c, const low_cst_t *f, proven_size_t b, proven_size_t wi, ir_loop_t *lp) {
    if (wi < b) {
        ir_run(c, f->kids, wi + 1, b - wi - 1);
        proven_size_t br = ir_emit(c, IRW_BRZ, 0);
        if (lp->ncnt >= IR_MAXPATCH) { ir_fail(c, "E-IR-LIMIT", "too many `continue` sites in one loop", f->line); return; }
        lp->cnt[lp->ncnt++] = br;
    }
    ir_block(c, f->kids[b]);
}
// ★★★ RFC-0132 P1 의 `for i count τ n .` 지름길 (2026-10-03, 벤치 새 판이 찾았다). 일반 길은 끝과 셈을 숨은 지역에 얼려
//   두고 셈을 사용자 이름에 베낀다 — 뜻은 옳지만 구간 분석이 그 사슬을 못 좇아 첨자 증명이 죽었다(행렬곱 15/18 → 3/15).
//   명세 §6.5 (8) 이 셈 이름과 머리가 읽은 이름을 몸 안에서 `set` 못 하게 하므로, 끝이 **이름 하나 · 수 · `len <이름>`** 이면
//   바퀴마다 다시 읽어도 같은 값이다 ⇒ `while lt i n . do … set i (add i 1) . end` 와 같은 모양으로 내린다(분석이 아는 모양).
//   ★ 몸이 그 이름들을 `mut_ref` 로 빌려주면(남이 바꿀 수 있다) 일반 길로 돌아간다.
static bool ir_mutref_of(const low_cst_t *nd, proven_u8str_view_t a, proven_u8str_view_t b) {
    if (!nd) return false;
    for (proven_size_t i = 0; i + 1 < nd->nkids; i++)
        if (is_atom(nd->kids[i]) && veq(nd->kids[i]->tok.lex, "mut_ref") && is_atom(nd->kids[i + 1]) &&
            (proven_u8str_view_eq(nd->kids[i + 1]->tok.lex, a) || (b.size && proven_u8str_view_eq(nd->kids[i + 1]->tok.lex, b))))
            return true;
    for (proven_size_t i = 0; i < nd->nkids; i++) if (ir_mutref_of(nd->kids[i], a, b)) return true;
    return false;
}
static bool ir_count_simple(ir_ctx_t *c, const low_cst_t *f, proven_size_t b, proven_size_t wi) {
    if (wi != 5) return false;
    const low_cst_t *e = f->kids[4];
    while (e && e->kind == LOW_CST_GROUP && e->nkids == 1) e = e->kids[0];
    proven_u8str_view_t nm = { 0 };
    bool fnd = false;
    if (is_atom(e)) {
        proven_i64 v;
        if (ir_int_lit(e->tok.lex, &v)) return !ir_mutref_of(f->kids[b], f->kids[1]->tok.lex, nm);
        (void)ir_local_find(c, e->tok.lex, &fnd);
        if (!fnd) return false;
        nm = e->tok.lex;
    } else if (e && e->kind == LOW_CST_FORM && e->nkids == 2 && is_atom(e->kids[0]) && veq(e->kids[0]->tok.lex, "len") && is_atom(e->kids[1])) {
        (void)ir_local_find(c, e->kids[1]->tok.lex, &fnd);
        if (!fnd) return false;
        nm = e->kids[1]->tok.lex;
    } else return false;
    return !ir_mutref_of(f->kids[b], f->kids[1]->tok.lex, nm);
}
static void ir_for_p1(ir_ctx_t *c, const low_cst_t *f, proven_size_t b) {
    proven_u8str_view_t kind = f->kids[2]->tok.lex;
    proven_size_t wi = b;                                          // `where` 의 자리(없으면 b)
    for (proven_size_t q = 2; q < b; q++) if (is_atom(f->kids[q]) && f->kids[q]->tok.kw == LOW_KW_NONE && veq(f->kids[q]->tok.lex, "where")) { wi = q; break; }
    bool counted = is_atom(f->kids[2]) && f->kids[2]->tok.kw == LOW_KW_NONE && (veq(kind, "count") || veq(kind, "range"));
    bool recur = is_atom(f->kids[2]) && f->kids[2]->tok.kw == LOW_KW_BE;
    bool mutk = is_atom(f->kids[2]) && f->kids[2]->tok.kw == LOW_KW_NONE && veq(kind, "mut");
    if (c->nloops >= IR_MAXLOOP || c->nlocals + 9 > IR_MAXLOCALS) { ir_fail(c, "E-IR-UNSUP", "loop nesting too deep", f->line); return; }
    #define HID(v_) proven_size_t v_ = c->nlocals++; c->locals[v_].name = (proven_u8str_view_t){ 0 }
    const proven_i64 W64 = (proven_i64)(IR_TY_KNOWN | 64 | IR_POL_WRAP), S64 = (proven_i64)(IR_TY_KNOWN | IR_TY_SIGNED | 64), U64 = (proven_i64)(IR_TY_KNOWN | 64);
    if (!counted && !recur) {                                      // for x mut buf · for x xs (where 가 붙은 이름·식 원천)
        proven_size_t s0 = mutk ? 3 : 2;
        // ★ 원천이 지역 이름 하나면 숨은 복사를 안 만든다(2026-10-03) — 머리가 읽은 이름은 몸 안에서 못 바꾸고(§6.5 (8)),
        //   `for x mut buf` 는 몸 안에서 buf 를 아예 못 만진다(§6.5 (9)). 복사본에 대한 증명은 분석이 원본으로 못 이어서
        //   `for x mut s do set x 1 . end` 의 칸 쓰기 검사가 남았다(체 벤치 14% 느림).
        bool direct = false; proven_size_t it = 0;
        if (wi == s0 + 1 && is_atom(f->kids[s0]) && f->kids[s0]->tok.kind == LOW_TOK_IDENT &&
            !ir_mutref_of(f->kids[b], f->kids[s0]->tok.lex, f->kids[1]->tok.lex)) {
            bool fnd = false; proven_size_t sl = ir_local_find(c, f->kids[s0]->tok.lex, &fnd);
            if (fnd) { direct = true; it = sl; }
        }
        if (!direct) { it = c->nlocals++; c->locals[it].name = (proven_u8str_view_t){ 0 }; }
        HID(idx);
        c->locals[idx].ty = (ityp_t){ .known = true, .bits = 64, .sign = false };   // 숨은 첨자도 u64 다 — 타입이 없으면 분석이 하계를 모른다
        proven_size_t var0 = (proven_size_t)-1;
        if (!mutk) { var0 = ir_local_declare(c, f->kids[1]->tok.lex, f->line); }
        if (!direct) {
            ir_run(c, f->kids, s0, wi - s0);
            ir_emit(c, IRW_STORE, (proven_i64)it);
        }
        ir_emit(c, IRW_CONST, 0); ir_emit(c, IRW_STORE, (proven_i64)idx);
        proven_size_t cond = c->code.len;
        ir_emit(c, IRW_LOAD, (proven_i64)idx); ir_emit(c, IRW_LOAD, (proven_i64)it); ir_emit(c, IRW_LEN, 0); ir_emit(c, IRW_LT, U64);
        proven_size_t brz = ir_emit(c, IRW_BRZ, 0);
        if (mutk) {
            if (c->nmel >= 8) { ir_fail(c, "E-IR-LIMIT", "too many nested `for x mut` loops", f->line); return; }
            c->mel_name[c->nmel] = f->kids[1]->tok.lex; c->mel_buf[c->nmel] = it; c->mel_idx[c->nmel] = idx; c->nmel++;
        } else {
            ir_emit(c, IRW_LOAD, (proven_i64)it); ir_emit(c, IRW_LOAD, (proven_i64)idx); ir_emit(c, IRW_INDEX, 0);
            ir_emit(c, IRW_STORE, (proven_i64)var0);
        }
        ir_loop_t *lp = &c->loops[c->nloops++];
        lp->rgdepth = g_nrg; lp->reldepth = g_nrel; lp->nbrk = 0; lp->ncnt = 0;
        ir_for_body(c, f, b, wi, lp);
        if (mutk) c->nmel--;
        proven_size_t step = c->code.len;
        ir_emit(c, IRW_LOAD, (proven_i64)idx); ir_emit(c, IRW_CONST, 1); ir_emit(c, IRW_ADD, U64); ir_emit(c, IRW_STORE, (proven_i64)idx);   // idx < len ⇒ 안 넘친다(검사된 덧셈이 분석에 범위를 준다)
        ir_emit(c, IRW_BR, (proven_i64)cond);
        proven_size_t end = c->code.len;
        ir_at(c, brz)->a = (proven_i64)end;
        for (proven_size_t i = 0; i < lp->nbrk; i++) ir_at(c, lp->brk[i])->a = (proven_i64)end;
        for (proven_size_t i = 0; i < lp->ncnt; i++) ir_at(c, lp->cnt[i])->a = (proven_i64)step;
        c->nloops--;
        return;
    }
    if (wi < 5 || !is_atom(f->kids[3])) { ir_fail(c, "E-IR-UNSUP", "malformed `for` head", f->line); return; }
    ityp_t ty = ity_of_decl_c(c, f, 3, 4);
    proven_i64 TM = (proven_i64)(IR_TY_KNOWN | (ty.sign ? IR_TY_SIGNED : 0) | (ty.bits ? ty.bits : 64));
    proven_size_t var = ir_local_declare(c, f->kids[1]->tok.lex, f->line);
    c->locals[var].ty = ty;
    if (recur) {                                                   // for i be τ v . while c . next e .
        proven_size_t wh = wi, nx = wi;
        for (proven_size_t q = 4; q < wi; q++) {
            if (is_atom(f->kids[q]) && f->kids[q]->tok.kw == LOW_KW_WHILE && wh == wi) wh = q;
            else if (is_atom(f->kids[q]) && f->kids[q]->tok.kw == LOW_KW_NONE && veq(f->kids[q]->tok.lex, "next") && wh < wi) { nx = q; break; }
        }
        if (wh == wi || nx == wi || wh == 4 || nx == wh + 1 || nx + 1 >= wi) {
            ir_fail(c, "E-IR-UNSUP", "a recurrence head is `for i be <type> <start> . while <condition> . next <step> .` (RFC-0132 §8.1)", f->line); return;
        }
        ir_run(c, f->kids, 4, wh - 4); ir_emit(c, IRW_STORE, (proven_i64)var);
        proven_size_t top = c->code.len;
        ir_run(c, f->kids, wh + 1, nx - wh - 1);
        proven_size_t ex = ir_emit(c, IRW_BRZ, 0);
        ir_loop_t *lp = &c->loops[c->nloops++];
        lp->rgdepth = g_nrg; lp->reldepth = g_nrel; lp->nbrk = 0; lp->ncnt = 0;
        ir_for_body(c, f, b, wi, lp);
        proven_size_t cont = c->code.len;
        ir_run(c, f->kids, nx + 1, wi - nx - 1); ir_emit(c, IRW_STORE, (proven_i64)var);
        ir_emit(c, IRW_BR, (proven_i64)top);
        proven_size_t end = c->code.len;
        ir_at(c, ex)->a = (proven_i64)end;
        for (proven_size_t i = 0; i < lp->nbrk; i++) ir_at(c, lp->brk[i])->a = (proven_i64)end;
        for (proven_size_t i = 0; i < lp->ncnt; i++) ir_at(c, lp->cnt[i])->a = (proven_i64)cont;
        c->nloops--;
        return;
    }
    if (veq(kind, "count") && ir_count_simple(c, f, b, wi)) {       // 지름길 — while 모양(위 ir_count_simple)
        ir_emit(c, IRW_CONST, 0); ir_emit(c, IRW_STORE, (proven_i64)var);
        proven_size_t cond = c->code.len;
        ir_emit(c, IRW_LOAD, (proven_i64)var); ir_run(c, f->kids, 4, 1); ir_emit(c, IRW_LT, TM);
        proven_size_t brz = ir_emit(c, IRW_BRZ, 0);
        ir_loop_t *lp = &c->loops[c->nloops++];
        lp->rgdepth = g_nrg; lp->reldepth = g_nrel; lp->nbrk = 0; lp->ncnt = 0;
        ir_for_body(c, f, b, wi, lp);
        proven_size_t step = c->code.len;
        ir_emit(c, IRW_LOAD, (proven_i64)var); ir_emit(c, IRW_CONST, 1); ir_emit(c, IRW_ADD, TM); ir_emit(c, IRW_STORE, (proven_i64)var);
        ir_emit(c, IRW_BR, (proven_i64)cond);
        proven_size_t end = c->code.len;
        ir_at(c, brz)->a = (proven_i64)end;
        for (proven_size_t i = 0; i < lp->nbrk; i++) ir_at(c, lp->brk[i])->a = (proven_i64)end;
        for (proven_size_t i = 0; i < lp->ncnt; i++) ir_at(c, lp->cnt[i])->a = (proven_i64)step;
        c->nloops--;
        return;
    }
    if (veq(kind, "count")) {                                      // for i count τ n .
        HID(endv); HID(iv);
        c->locals[endv].ty = ty; c->locals[iv].ty = ty;   // 숨은 끝·셈도 τ 다
        ir_run(c, f->kids, 4, wi - 4);
        ir_emit(c, IRW_STORE, (proven_i64)endv);
        ir_emit(c, IRW_CONST, 0); ir_emit(c, IRW_STORE, (proven_i64)iv);
        proven_size_t cond = c->code.len;
        ir_emit(c, IRW_LOAD, (proven_i64)iv); ir_emit(c, IRW_LOAD, (proven_i64)endv); ir_emit(c, IRW_LT, TM);
        proven_size_t brz = ir_emit(c, IRW_BRZ, 0);
        ir_emit(c, IRW_LOAD, (proven_i64)iv); ir_emit(c, IRW_STORE, (proven_i64)var);
        ir_loop_t *lp = &c->loops[c->nloops++];
        lp->rgdepth = g_nrg; lp->reldepth = g_nrel; lp->nbrk = 0; lp->ncnt = 0;
        ir_for_body(c, f, b, wi, lp);
        proven_size_t step = c->code.len;
        ir_emit(c, IRW_LOAD, (proven_i64)iv); ir_emit(c, IRW_CONST, 1); ir_emit(c, IRW_ADD, U64); ir_emit(c, IRW_STORE, (proven_i64)iv);
        ir_emit(c, IRW_BR, (proven_i64)cond);
        proven_size_t end = c->code.len;
        ir_at(c, brz)->a = (proven_i64)end;
        for (proven_size_t i = 0; i < lp->nbrk; i++) ir_at(c, lp->brk[i])->a = (proven_i64)end;
        for (proven_size_t i = 0; i < lp->ncnt; i++) ir_at(c, lp->cnt[i])->a = (proven_i64)step;
        c->nloops--;
        return;
    }
    // range τ a b [step k] — 머리 낱말: kids[4..b) = a b [step k] (a · b · k 는 각각 한 마디)
    proven_size_t sp_ = wi;
    for (proven_size_t q = 4; q < wi; q++) if (is_atom(f->kids[q]) && f->kids[q]->tok.kw == LOW_KW_NONE && veq(f->kids[q]->tok.lex, "step")) { sp_ = q; break; }
    if (sp_ - 4 != 2 || (sp_ < wi && sp_ + 2 != wi)) { ir_fail(c, "E-IR-UNSUP", "`range τ a b [step k]` — a, b and k are one term each (wrap an expression in parentheses)", f->line); return; }
    HID(av); HID(bv); HID(lo); HID(hi); HID(st); HID(iv); HID(dv); HID(ab);
    c->locals[av].ty = ty; c->locals[bv].ty = ty; c->locals[lo].ty = ty; c->locals[hi].ty = ty; c->locals[iv].ty = ty;
    c->locals[st].ty = (ityp_t){ .known = true, .bits = 64, .sign = true };
    c->locals[dv].ty = (ityp_t){ .known = true, .bits = 64, .sign = false }; c->locals[ab].ty = c->locals[dv].ty;
    ir_node(c, f->kids[4]); ir_emit(c, IRW_STORE, (proven_i64)av);
    ir_node(c, f->kids[5]); ir_emit(c, IRW_STORE, (proven_i64)bv);
    // lo, hi
    ir_emit(c, IRW_LOAD, (proven_i64)av); ir_emit(c, IRW_LOAD, (proven_i64)bv); ir_emit(c, IRW_LE, TM);
    proven_size_t j1 = ir_emit(c, IRW_BRZ, 0);
    ir_emit(c, IRW_LOAD, (proven_i64)av); ir_emit(c, IRW_STORE, (proven_i64)lo); ir_emit(c, IRW_LOAD, (proven_i64)bv); ir_emit(c, IRW_STORE, (proven_i64)hi);
    proven_size_t j2 = ir_emit(c, IRW_BR, 0);
    ir_at(c, j1)->a = (proven_i64)c->code.len;
    ir_emit(c, IRW_LOAD, (proven_i64)bv); ir_emit(c, IRW_STORE, (proven_i64)lo); ir_emit(c, IRW_LOAD, (proven_i64)av); ir_emit(c, IRW_STORE, (proven_i64)hi);
    ir_at(c, j2)->a = (proven_i64)c->code.len;
    proven_size_t skips[4]; proven_size_t nsk = 0;
    if (sp_ < wi) {                                                // step 을 적었다 — 방향은 step 이 정한다(규칙 5)
        ir_node(c, f->kids[sp_ + 1]); ir_emit(c, IRW_STORE, (proven_i64)st);
        ir_emit(c, IRW_LOAD, (proven_i64)st); ir_emit(c, IRW_CONST, 0); ir_emit(c, IRW_EQ, S64);
        proven_size_t nz = ir_emit(c, IRW_BRZ, 0);
        {   // step 0 — 멈춘다(번역 시점 상수면 검사층이 먼저 거절)
            proven_size_t si = c->out->nstrs; static const char msg[] = "`range … step 0` never moves (RFC-0132 §8.2)";
            proven_u8str_view_t mv = { .ptr = (const proven_u8 *)msg, .size = sizeof msg - 1 };
            for (proven_size_t i = 0; i < c->out->nstrs; i++) if (proven_u8str_view_eq(c->out->strs[i], mv)) { si = i; break; }
            if (si == c->out->nstrs) { if (c->out->nstrs >= IR_MAXSTRS) { ir_fail(c, "E-IR-UNSUP", "too many string literals", f->line); return; } c->out->strs[c->out->nstrs++] = mv; }
            ir_emit(c, IRW_PANIC, (proven_i64)si);
        }
        ir_at(c, nz)->a = (proven_i64)c->code.len;
        ir_emit(c, IRW_LOAD, (proven_i64)st); ir_emit(c, IRW_CONST, 0); ir_emit(c, IRW_GT, S64);
        proven_size_t neg = ir_emit(c, IRW_BRZ, 0);
        ir_emit(c, IRW_LOAD, (proven_i64)av); ir_emit(c, IRW_LOAD, (proven_i64)bv); ir_emit(c, IRW_GT, TM);
        proven_size_t ok1 = ir_emit(c, IRW_BRZ, 0);
        skips[nsk++] = ir_emit(c, IRW_BR, 0);                      // st>0 이고 a>b → 0 번
        ir_at(c, neg)->a = (proven_i64)c->code.len;
        ir_emit(c, IRW_LOAD, (proven_i64)av); ir_emit(c, IRW_LOAD, (proven_i64)bv); ir_emit(c, IRW_LT, TM);
        proven_size_t ok2 = ir_emit(c, IRW_BRZ, 0);
        skips[nsk++] = ir_emit(c, IRW_BR, 0);                      // st<0 이고 a<b → 0 번
        ir_at(c, ok1)->a = (proven_i64)c->code.len; ir_at(c, ok2)->a = (proven_i64)c->code.len;
    } else {                                                       // step 이 없다 — 두 끝이 방향을 정한다
        ir_emit(c, IRW_LOAD, (proven_i64)av); ir_emit(c, IRW_LOAD, (proven_i64)bv); ir_emit(c, IRW_LE, TM);
        proven_size_t m1 = ir_emit(c, IRW_BRZ, 0);
        ir_emit(c, IRW_CONST, 1); ir_emit(c, IRW_STORE, (proven_i64)st);
        proven_size_t m2 = ir_emit(c, IRW_BR, 0);
        ir_at(c, m1)->a = (proven_i64)c->code.len;
        ir_emit(c, IRW_CONST, -1); ir_emit(c, IRW_STORE, (proven_i64)st);
        ir_at(c, m2)->a = (proven_i64)c->code.len;
    }
    ir_emit(c, IRW_LOAD, (proven_i64)av); ir_emit(c, IRW_STORE, (proven_i64)iv);
    proven_size_t top = c->code.len;
    ir_emit(c, IRW_LOAD, (proven_i64)iv); ir_emit(c, IRW_STORE, (proven_i64)var);
    ir_loop_t *lp = &c->loops[c->nloops++];
    lp->rgdepth = g_nrg; lp->reldepth = g_nrel; lp->nbrk = 0; lp->ncnt = 0;
    ir_for_body(c, f, b, wi, lp);
    proven_size_t cont = c->code.len;
    // 거리 = st>0 ? hi−i : i−lo
    ir_emit(c, IRW_LOAD, (proven_i64)st); ir_emit(c, IRW_CONST, 0); ir_emit(c, IRW_GT, S64);
    proven_size_t d1 = ir_emit(c, IRW_BRZ, 0);
    ir_emit(c, IRW_LOAD, (proven_i64)hi); ir_emit(c, IRW_LOAD, (proven_i64)iv); ir_emit(c, IRW_SUB, W64); ir_emit(c, IRW_STORE, (proven_i64)dv);
    ir_emit(c, IRW_LOAD, (proven_i64)st); ir_emit(c, IRW_STORE, (proven_i64)ab);
    proven_size_t d2 = ir_emit(c, IRW_BR, 0);
    ir_at(c, d1)->a = (proven_i64)c->code.len;
    ir_emit(c, IRW_LOAD, (proven_i64)iv); ir_emit(c, IRW_LOAD, (proven_i64)lo); ir_emit(c, IRW_SUB, W64); ir_emit(c, IRW_STORE, (proven_i64)dv);
    ir_emit(c, IRW_CONST, 0); ir_emit(c, IRW_LOAD, (proven_i64)st); ir_emit(c, IRW_SUB, W64); ir_emit(c, IRW_STORE, (proven_i64)ab);
    ir_at(c, d2)->a = (proven_i64)c->code.len;
    ir_emit(c, IRW_LOAD, (proven_i64)dv); ir_emit(c, IRW_LOAD, (proven_i64)ab); ir_emit(c, IRW_LT, U64);
    proven_size_t go = ir_emit(c, IRW_BRZ, 0);
    proven_size_t out1 = ir_emit(c, IRW_BR, 0);                    // 거리 < |st| → 끝
    ir_at(c, go)->a = (proven_i64)c->code.len;
    ir_emit(c, IRW_LOAD, (proven_i64)iv); ir_emit(c, IRW_LOAD, (proven_i64)st); ir_emit(c, IRW_ADD, W64); ir_emit(c, IRW_STORE, (proven_i64)iv);
    ir_emit(c, IRW_BR, (proven_i64)top);
    proven_size_t end = c->code.len;
    ir_at(c, out1)->a = (proven_i64)end;
    for (proven_size_t i = 0; i < nsk; i++) ir_at(c, skips[i])->a = (proven_i64)end;
    for (proven_size_t i = 0; i < lp->nbrk; i++) ir_at(c, lp->brk[i])->a = (proven_i64)end;
    for (proven_size_t i = 0; i < lp->ncnt; i++) ir_at(c, lp->cnt[i])->a = (proven_i64)cont;
    c->nloops--;
    #undef HID
}
// guard diverge / plain terminal statements from an operand run
static void ir_diverge(ir_ctx_t *c, low_cst_t *const *k, proven_size_t start, proven_size_t n, proven_u32 line) {
    if (n == 0 || !is_atom(k[start])) { ir_fail(c, "E-IR-UNSUP", "unsupported guard diverge", line); return; }
    low_kw_t kw = k[start]->tok.kw;
    if (kw == LOW_KW_RETURN) {
        // ★★★ **`errors E when C .` 는 출구 계약이다 — 그런데 반쪽만 강제되고 있었다.**
        //
        //   `return error E` 는 이미 **C 가 참인지** 검사한다(a=2). 그런데 **반대 방향이 없었다:**
        //   **C 가 참인데 오류를 안 내고 정상 반환**하면 아무도 안 봤다.
        //
        //     errors too_small when lt n 5 .              rem 절: n < 5 면 오류라고 **선언**
        //     guard ge n 1 . else return error too_small . rem 본문: n < 1 일 때만 오류
        //     → g(3) = **ok 3**   (절은 오류라 했는데!)  `--check` 는 **ok** 였다.
        //
        //   그리고 **계약 오라클은 그 절을 믿고** 경계 테스트를 뽑는다 — 거짓말 위에 선다.
        //
        //   ⇒ **정상 반환에서는 선언된 모든 `when` 이 거짓이어야 한다.** 이제 검사한다(a=2).
        //
        //   ★★ 여기가 **`guard` 가 값을 하는 자리다.** `guard ¬C . else return error E .` 는
        //     그 자체로 **정상 경로에서 C 가 거짓임의 증명**이다 — 구간/관계 분석이 그것을
        //     보고 이 검사를 **지운다**(비용 0). 임의의 `if` 로 같은 일을 하면 분석이 대개
        //     증명하지 못해 **런타임 검사가 남는다.** guard 는 이제 **더 빠르다.**
        bool is_err = (n > 1 && ir_head_named(k[start + 1], "error"));
        if (!is_err && c->newhen && c->def_form) {
            for (proven_u8 wi = 0; wi < c->newhen; wi++) {
                if (c->ewhen[wi].we <= c->ewhen[wi].ws) continue;
                ir_run(c, c->def_form->kids, c->ewhen[wi].ws,
                       c->ewhen[wi].we - c->ewhen[wi].ws);
                ir_emit(c, IRW_NOT, 0);          // ★ 이 오류의 조건은 **거짓**이어야 한다
                ir_emit(c, IRW_ASSERT, 9);       // a=9: 출구(1) + **errors 절의 완전성**(8)
            }
        }
        if (n > 1) ir_run(c, k, start + 1, n - 1);
        else ir_emit(c, IRW_CONST, 0);
        // ★ RFC-0053 §6.6 — 계약을 사실로 쓰려면 **강제해야 한다.** ensures 도 마찬가지다.
        //   반환값을 숨은 지역 `ret` 에 담고, 술어를 검사하고, 되돌려 준다.
        //   a = 1 로 표시한다: 이것은 **출구** 검사이므로 구간 분석이 제거할 수 있다.
        //   (진입 requires 검사는 a = 0 — 절대 제거하지 않는다. 그것을 제거하면 분석이
        //    자기가 심은 사실로 자기를 정당화하는 **순환**이 된다.)
        if (c->nens > 0) {
            ir_emit(c, IRW_STORE, (proven_i64)c->ret_slot);
            for (proven_u8 e = 0; e < c->nens; e++) {
                ir_emit(c, IRW_LOAD, (proven_i64)c->ret_slot);
                // ★ 우변은 **리터럴이거나 이름**이다 — 하나의 낮춤기가 싣는다.
                if (c->ens[e].is_lit) ir_emit(c, IRW_CONST, c->ens[e].n);
                else if (!ir_contract_operand(c, c->ens[e].rhs, line)) {
                    ir_fail(c, "E-ENS-UNSUP",
                            "this `ensures` names something the tool cannot load here (an integer "
                            "literal, a parameter/local, or a field path). It used to be SKIPPED "
                            "SILENTLY — the promise was written and nothing checked it",
                            line);
                    return;
                }
                ir_emit(c, c->ens[e].w, 0);
                ir_emit(c, IRW_ASSERT, 1);
            }
            ir_emit(c, IRW_LOAD, (proven_i64)c->ret_slot);
        }
        // ★★★ RFC-0112 D5(5) (WO-0212) — **나가는 길마다 열린 영역을 되감는다.** 전엔 영역 안의 `return` 이
        //   되감기를 건너뛰었다 — 그 바이트는 프로그램이 끝날 때까지 돌아오지 않았다(루프라면 창이 샌다).
        //   값은 이미 스택에 있다(탈출 검사가 영역의 자리를 싣는 값을 막았다). 안쪽부터 되감는다.
        ir_release_down_to(c, 0);                           // ★ RFC-0135 S2 — 나가기 전에 할당기 바이트를 돌려준다
        for (proven_size_t d = g_nrg; d-- > 0; ) {
            ir_emit(c, IRW_LOAD, (proven_i64)g_rgslot[d]);
            ir_emit(c, IRW_RRESET, (proven_i64)g_rgroot[d]);
        }
        ir_emit(c, IRW_RET, 0);
        return;
    }
    if (kw == LOW_KW_GIVE) { ir_fail(c, "E-IR-UNSUP", "give is gone", line); return; }
    if ((kw == LOW_KW_BREAK || kw == LOW_KW_CONTINUE) && n == 1 && c->nloops > 0) {
        ir_loop_t *lp = &c->loops[c->nloops - 1];
        // ★ 루프 **안에서 연** 영역만 되감는다 — 루프를 감싼 영역은 계속 산다.
        ir_release_down_to(c, lp->reldepth);                // ★ RFC-0135 S2 — 루프 안에서 받은 것만 돌려준다
        for (proven_size_t d = g_nrg; d-- > lp->rgdepth; ) {
            ir_emit(c, IRW_LOAD, (proven_i64)g_rgslot[d]);
            ir_emit(c, IRW_RRESET, (proven_i64)g_rgroot[d]);
        }
        if (kw == LOW_KW_CONTINUE) { if (lp->ncnt < IR_MAXPATCH) lp->cnt[lp->ncnt++] = ir_emit(c, IRW_BR, 0); }
        else if (lp->nbrk < IR_MAXPATCH) lp->brk[lp->nbrk++] = ir_emit(c, IRW_BR, 0);
        return;
    }
    // ★ RFC-0135 S1 (D3) — `… . else panic "…" .`: 멈춤도 벗어나는 길이다(검사층의 E-GUARD-FALLTHROUGH 가 이미 받는다).
    //   전엔 하강이 몰라 `guard c . else panic "…" .` 가 `E-IR-UNSUP` 였다 — 검사는 초록인데 돌지 않았다.
    if (is_atom(k[start]) && veq(k[start]->tok.lex, "panic")) { ir_run(c, k, start, n); return; }
    ir_fail(c, "E-IR-UNSUP", "unsupported guard diverge (fail is outside the S5 core)", line);
}

    // ★★★ **`pipe <source> do <stage>* <terminal> . end`** (RFC-0010 §6.1·§6.3, D-A 융합=보장).
    //   융합은 **최적화가 아니라 의미론**이다: 잘 형성된 파이프라인은 **구성에 의해** 단 하나의 루프로
    //   낮아진다(중간 배열·closure 0). 여기 코드가 §6.3 의 LOWER(source, stages, terminal) 그대로다:
    //
    //     preamble(terminal) ;  for i in 0..len(src) { x = src[i] ; <스테이지 폴드> ; sink } ;  epilogue
    //
    //   스테이지는 **이름 op 참조**만 받는다(§6.2 — 람다 없음 ⇒ closure 가 생길 곳이 없다).
    //   첫 슬라이스(D-C)는 stage {filter,map} × terminal {collect into, fold} 넷이다.
// ★★ RFC-0121 §6.1 — 단계의 **문맥** `with <식>`. 단계 op 이름(kids[opk]) 바로 뒤에 `with` 가 오면 그 뒤 전부가 식 하나다.
//   `with` 는 이 자리에서만 표지다(전역 낱말이 아니다). 있으면 *wfrom 에 식의 첫 자리를 주고 true. 이름 뒤에 다른 낱말이
//   남거나 `with` 뒤가 비면 E-PIPE-WITH.
static bool ir_pipe_with(ir_ctx_t *c, const low_cst_t *ln, proven_size_t opk, proven_size_t *wfrom, proven_u32 line) {
    if (ln->nkids <= opk + 1) return false;
    const low_cst_t *w = ln->kids[opk + 1];
    if (!is_atom(w) || !veq(w->tok.lex, "with")) {
        ir_fail(c, "E-PIPE-WITH", "a `pipe` stage ends after its op name — the only thing that may follow is `with <expr>`, "
                "the stage's context (RFC-0121 §6.1)", line);
        return false;
    }
    if (ln->nkids <= opk + 2) { ir_fail(c, "E-PIPE-WITH", "`with` needs an expression: `filter above with limit .` (RFC-0121 §6.1)", line); return false; }
    *wfrom = opk + 2;
    return true;
}
// 단계 줄 어디에든 `with` 가 있는가 — 처리 op 이 없는 단계(`take`·`skip`·`count`·`collect`)는 문맥을 받지 않는다.
static bool ir_pipe_has_with(const low_cst_t *ln) {
    for (proven_size_t q = 1; q < ln->nkids; q++) if (is_atom(ln->kids[q]) && veq(ln->kids[q]->tok.lex, "with")) return true;
    return false;
}
 void ir_pipe(ir_ctx_t *c, const low_cst_t *f, proven_size_t first, bool as_value) {
    const proven_i64 PU64 = (proven_i64)(IR_TY_KNOWN | 64);   // ★ 숨은 첨자의 비교·덧셈 표시(아래 i · j 주석)
        proven_size_t b = form_block_index(f);
        if (b == f->nkids || b <= first) { ir_fail(c, "E-IR-UNSUP", "`pipe` needs a source and a `do … end` block", f->line); return; }
        if (c->nlocals + 4 > IR_MAXLOCALS) { ir_fail(c, "E-IR-UNSUP", "pipe: too many locals", f->line); return; }
        proven_size_t sv = c->nlocals++, i = c->nlocals++, j = c->nlocals++, acc = c->nlocals++;
        // ★ 숨은 첨자(i · j)와 세는 수는 u64 다 — 비교·덧셈도 u64 로 낸다(2026-10-04). 타입 없이(0) 내면 분석이 부호를 몰라
        //   `i < len src` · `j < len out` 을 사실로 못 세웠고, `collect into` 의 읽기·쓰기마다 경계 검사가 남았다.
        c->locals[i].ty = (ityp_t){ .known = true, .bits = 64, .sign = false };
        //   (j 에는 타입을 안 준다 — `count` 의 j 는 상계가 안 서서, 타입을 주면 그 덧셈이 원소마다 넘침 검사를 달았다(체 ×1.4).
        //    collect 의 j 는 비교·덧셈에 u64 표시를 직접 달아 사실을 세운다.)
        c->locals[sv].name = (proven_u8str_view_t){ 0 };
        c->locals[i].name  = (proven_u8str_view_t){ 0 };
        c->locals[j].name  = (proven_u8str_view_t){ 0 };
        c->locals[acc].name = (proven_u8str_view_t){ 0 };
        // ★★★ **pull 소스** (RFC-0010 §8-8) — 소스가 `next` 핸들러를 가진 액터면 **당겨서** 읽는다.
        //   ★ **새 문법이 0 이다**: 소스의 **타입이 모양을 결정한다**. 슬라이스면 인덱스 루프, `next` 를
        //   가진 액터면 pull 루프. 둘 다 **단일 루프**라 융합 보장(D-A)이 유지된다.
        //   ★ pull 반복자 = **상태 액터 + `next` → option T**. 새 기계가 없다: trait·단형화·option 이
        //   이미 있었고, allocator(vm_alloc)가 바로 그 모양이다. 무한 소스는 `take`/`any`/`all` 의
        //   단락으로 끝난다 — 단락이 없으면 끝나지 않는다(그래서 take 가 먼저 필요했다).
        proven_size_t nexth = (proven_size_t)-1;
        if (first + 1 == b && is_atom(f->kids[first])) {
            bool lf; proven_size_t lsl = ir_local_find(c, f->kids[first]->tok.lex, &lf);
            if (lf && c->locals[lsl].tyname.size) {
                bool sf; proven_size_t si = ir_struct_find(c->out, c->locals[lsl].tyname, &sf);
                if (sf) for (proven_size_t q = 0; q < c->out->ndefs; q++) {
                    const low_ir_def_t *dd = &c->out->defs[q];
                    if (dd->is_actor && proven_u8str_view_eq(dd->name, proven_u8str_view_from_cstr("next")) &&
                        dd->param_sidx[0] == (proven_u8)si) { nexth = q; break; }
                }
            }
        }
        { proven_size_t sp2 = first; ir_value(c, f->kids, &sp2, b); }   // source
        ir_emit(c, IRW_STORE, (proven_i64)sv);

        // ── 본문을 읽어 스테이지 목록과 terminal 을 뽑는다(G1: terminal 정확히 하나, 마지막)
        const low_cst_t *blk = f->kids[b];
        // ★ 스테이지: filter/map 은 op 이름을, take/skip 은 **comptime 개수**를 든다.
        enum { ST_FILTER, ST_MAP, ST_TAKE, ST_SKIP, ST_ENUM, ST_ZIP, ST_SCAN };
        struct { int kind; proven_size_t op; proven_i64 n; proven_size_t ctr; proven_size_t oth; bool hasctx; proven_size_t ctx; } st[8]; proven_size_t nst = 0;
        int term = -1; proven_size_t term_op = 0; const low_cst_t *term_arg = NULL; proven_u32 tline = f->line;
        bool term_hasctx = false; proven_size_t term_ctx = 0;
        // ★★ RFC-0121 §6.3 — 루프 **전에** 한 번 평가하는 것들(zip 의 짝 · scan/fold 의 초깃값 · collect 의 받는 자리 · `with` 식).
        //   절 차례대로 모은다. `with` 가 없는 pipe 는 옛 차례(끝 단계의 것 먼저)를 그대로 지킨다 — 이 RFC 로 소급해 바꾸지 않는다.
        //   slot == (size_t)-1 은 collect 의 받는 자리(그 지역은 평가 직전에 연다 — 옛 지역 번호를 그대로 두려고).
        struct { const low_cst_t *const *kids; proven_size_t from, to, slot; bool term, isctx; proven_u32 line; } prep[24]; proven_size_t nprep = 0;
        bool any_with = false;
        size_t nzip = 0;
        for (proven_size_t z = 0; z < blk->nkids; z++) {
            const low_cst_t *ln = blk->kids[z];
            if (!ln || ln->kind != LOW_CST_FORM || !ln->nkids || !is_atom(ln->kids[0])) continue;
            proven_u8str_view_t h = ln->kids[0]->tok.lex; tline = ln->kids[0]->tok.line;
            // ★★★ **`take n` · `skip n`** (RFC-0010 §8-2 → 구현). 접두 절단이다:
            //   take n = 앞 n 개만 통과시키고 **거기서 상류를 멈춘다**(단락 — any/all 과 같은 기계).
            //   skip n = 앞 n 개를 버리고 나머지를 통과(단락 아님 — 세면서 버릴 뿐).
            //   ★ 둘 다 **카운터 하나**라 단일 패스를 깨지 않는다 ⇒ 융합 보장(D-A) 유지.
            //   ★ `take` 는 allocator 의 `reserve`(옛 `take`)와 **다른 뜻**이라 그쪽 이름을 바꿨다:
            //     하나는 자원을 얻고 하나는 흐름을 끊는다(§2.5 — 한 낱말에 두 뜻 금지).
            bool ist = veq(h, "take"), isk = veq(h, "skip");
            if (ist || isk) {
                if (term >= 0) { ir_fail(c, "E-PIPE-NO-TERMINAL", "a `pipe` stage may not come AFTER the terminal (RFC-0010 G1)", tline); return; }
                if (ir_pipe_has_with(ln)) { ir_fail(c, "E-PIPE-WITH", "`take`/`skip` call no op, so they take no context (RFC-0121 §6.1)", tline); return; }
                proven_i64 nn = 0;
                if (nst >= 8 || ln->nkids < 2 || !is_atom(ln->kids[1]) || !ir_int_lit(ln->kids[1]->tok.lex, &nn) || nn < 0) {
                    ir_fail(c, "E-PIPE-STAGE", "`take`/`skip` needs a COMPTIME non-negative count: `take <n> .` (RFC-0010 §8-2)", tline); return;
                }
                if (c->nlocals >= IR_MAXLOCALS) { ir_fail(c, "E-IR-UNSUP", "pipe: too many locals", tline); return; }
                st[nst].kind = ist ? ST_TAKE : ST_SKIP; st[nst].n = nn;
                st[nst].ctr = c->nlocals++; c->locals[st[nst].ctr].name = (proven_u8str_view_t){ 0 };
                nst++;
                continue;
            }
            // ★★★ **`enumerate <op>` · `zip <other> <op>`** (RFC-0010 §8-2 → 구현).
            //   ★ **튜플을 만들지 않는다.** 이 언어엔 튜플이 없고, 더할 이유도 없다: `,` 는 이미
            //     **인자 닫개**(§0 D8)라 `(a, b)` 는 쉼표에 두 번째 뜻을 주고, 튜플 자체가 "이름 없는
            //     struct" 라 곱 타입에 **두 표현**이 생긴다(§2.5 금지). 대신 **op 에 인자로 건넨다**:
            //       enumerate f : f(i, x) → x'      — 인덱스를 op 이 받는다
            //       zip ys g    : g(x, ys[k]) → x'  — 짝을 op 이 합친다
            //     쌍을 **만들었다 즉시 분해**하지 않으므로 P3(숨은 할당 0)·P2(비용 가시)에 더 맞다.
            //   ★ zip 은 **짧은 쪽이 끝나면 전체 종료**한다(단락).
            // ★★★ **`scan <init> <op>`** (RFC-0010 §8-3 → 구현) — 누적 prefix. 각 원소가 **누적 상태**가 된다:
            //     acc0 = init ;  acc(n+1) = op(acc n, x n) ;  흘려보내는 값 = acc(n+1)
            //   ★ §8-3 이 남긴 질문("출력이 입력 길이만큼이라 collect 와의 bounded 규칙 재검토")은
            //     **새 규칙 없이 풀린다**: collect 가 이미 유계 싱크다(§6.5 — out 이 짧으면 넘치지 않고
            //     거기서 멈춘다). scan 은 원소 수를 **늘리지 않으므로**(1:1) 기존 경계가 그대로 맞다.
            //   ★ 누산기 하나라 단일 패스를 안 깨고 융합 보장(D-A)이 유지된다.
            if (veq(h, "scan")) {
                if (term >= 0) { ir_fail(c, "E-PIPE-NO-TERMINAL", "a `pipe` stage may not come AFTER the terminal (RFC-0010 G1)", tline); return; }
                if (nst >= 8 || ln->nkids < 3 || !is_atom(ln->kids[2])) { ir_fail(c, "E-FOLD-OP", "`scan` needs `<init> <op>` — the op is a NAME (RFC-0010 §6.2)", tline); return; }
                bool of; proven_size_t oi = ir_def_find_in(c, ln->kids[2]->tok.lex, &of);
                if (!of) { ir_fail(c, "E-FOLD-OP", "scan's op names an op that does not exist", tline); return; }
                proven_size_t wf = 0; bool hw = ir_pipe_with(c, ln, 2, &wf, tline); if (c->failed) return;
                if (c->out->defs[oi].nparams != (hw ? 3u : 2u) || c->out->defs[oi].param_cap) {
                    ir_fail(c, hw ? "E-PIPE-CONTEXT-ARG" : "E-FOLD-OP", hw ? "with a context, scan's op takes exactly THREE arguments (the accumulator, the element, the context) and no capability (RFC-0121 §6.1)"
                                                                       : "scan's op takes exactly TWO arguments (the accumulator, the element)", tline); return; }
                if (c->nlocals + 2 > IR_MAXLOCALS || nzip >= 8 || nprep + 2 > 24) { ir_fail(c, "E-IR-UNSUP", "pipe: too many locals", tline); return; }
                st[nst].kind = ST_SCAN; st[nst].op = oi; st[nst].oth = 0; st[nst].hasctx = false;
                st[nst].ctr = c->nlocals++; c->locals[st[nst].ctr].name = (proven_u8str_view_t){ 0 };
                prep[nprep++] = (typeof(prep[0])){ (const low_cst_t *const *)ln->kids, 1, 2, st[nst].ctr, false, false, tline }; nzip++;   // init 은 루프 **밖**에서 한 번
                if (hw) { st[nst].hasctx = true; st[nst].ctx = c->nlocals++; c->locals[st[nst].ctx].name = (proven_u8str_view_t){ 0 };
                          prep[nprep++] = (typeof(prep[0])){ (const low_cst_t *const *)ln->kids, wf, ln->nkids, st[nst].ctx, false, true, tline }; any_with = true; }
                nst++;
                continue;
            }
            bool ise = veq(h, "enumerate"), isz = veq(h, "zip");
            if (ise || isz) {
                if (term >= 0) { ir_fail(c, "E-PIPE-NO-TERMINAL", "a `pipe` stage may not come AFTER the terminal (RFC-0010 G1)", tline); return; }
                proven_size_t need = isz ? 3u : 2u;
                if (nst >= 8 || ln->nkids < need || !is_atom(ln->kids[need - 1])) {
                    ir_fail(c, "E-FOLD-OP", isz ? "`zip` needs `<other-slice> <op>` — the op is a NAME (RFC-0010 §6.2)"
                                                : "`enumerate` needs an op NAME: `enumerate <op> .` (RFC-0010 §6.2)", tline); return;
                }
                bool of; proven_size_t oi = ir_def_find_in(c, ln->kids[need - 1]->tok.lex, &of);
                if (!of) { ir_fail(c, "E-FOLD-OP", "this `pipe` stage names an op that does not exist", tline); return; }
                proven_size_t wf = 0; bool hw = ir_pipe_with(c, ln, need - 1, &wf, tline); if (c->failed) return;
                if (c->out->defs[oi].nparams != (hw ? 3u : 2u) || c->out->defs[oi].param_cap) {
                    if (hw) { ir_fail(c, "E-PIPE-CONTEXT-ARG", isz ? "with a context, zip's op takes exactly THREE arguments (the element, the other element, the context) and no capability (RFC-0121 §6.1)"
                                                                   : "with a context, enumerate's op takes exactly THREE arguments (the index, the element, the context) and no capability (RFC-0121 §6.1)", tline); return; }
                    ir_fail(c, "E-FOLD-OP", isz ? "zip's op takes exactly TWO arguments (the element, the other element)"
                                                : "enumerate's op takes exactly TWO arguments (the index, the element)", tline); return;
                }
                if (c->nlocals + 3 > IR_MAXLOCALS || nprep + 2 > 24) { ir_fail(c, "E-IR-UNSUP", "pipe: too many locals", tline); return; }
                st[nst].kind = ise ? ST_ENUM : ST_ZIP; st[nst].op = oi; st[nst].hasctx = false;
                st[nst].ctr = c->nlocals++; c->locals[st[nst].ctr].name = (proven_u8str_view_t){ 0 };
                st[nst].oth = 0;
                if (isz) {
                    st[nst].oth = c->nlocals++; c->locals[st[nst].oth].name = (proven_u8str_view_t){ 0 };
                    prep[nprep++] = (typeof(prep[0])){ (const low_cst_t *const *)ln->kids, 1, 2, st[nst].oth, false, false, tline }; nzip++;   // 루프 **밖**에서 평가한다
                }
                if (hw) { st[nst].hasctx = true; st[nst].ctx = c->nlocals++; c->locals[st[nst].ctx].name = (proven_u8str_view_t){ 0 };
                          prep[nprep++] = (typeof(prep[0])){ (const low_cst_t *const *)ln->kids, wf, ln->nkids, st[nst].ctx, false, true, tline }; any_with = true; }
                nst++;
                continue;
            }
            bool isf = veq(h, "filter"), ism = veq(h, "map");
            if (isf || ism) {
                if (term >= 0) { ir_fail(c, "E-PIPE-NO-TERMINAL", "a `pipe` stage may not come AFTER the terminal — the terminal ends the pipeline (RFC-0010 G1)", tline); return; }
                if (nst >= 8 || ln->nkids < 2 || !is_atom(ln->kids[1])) { ir_fail(c, "E-FOLD-OP", "`filter`/`map` needs an op NAME (RFC-0010 §6.2 — no lambdas)", tline); return; }
                bool of; proven_size_t oi = ir_def_find_in(c, ln->kids[1]->tok.lex, &of);
                if (!of) { ir_fail(c, "E-FOLD-OP", "this `pipe` stage names an op that does not exist", tline); return; }
                proven_size_t wf = 0; bool hw = ir_pipe_with(c, ln, 1, &wf, tline); if (c->failed) return;
                if (c->out->defs[oi].nparams != (hw ? 2u : 1u) || c->out->defs[oi].param_cap) {
                    ir_fail(c, hw ? "E-PIPE-CONTEXT-ARG" : "E-FOLD-OP", hw ? "with a context, a `pipe` stage op takes exactly TWO arguments (the element, the context) and no capability (RFC-0121 §6.1)"
                                                                       : "a `pipe` stage op takes exactly ONE argument (the element) and no capability", tline); return; }
                st[nst].kind = isf ? ST_FILTER : ST_MAP; st[nst].op = oi; st[nst].hasctx = false;
                if (hw) { if (c->nlocals + 1 > IR_MAXLOCALS || nprep + 1 > 24) { ir_fail(c, "E-IR-UNSUP", "pipe: too many locals", tline); return; }
                          st[nst].hasctx = true; st[nst].ctx = c->nlocals++; c->locals[st[nst].ctx].name = (proven_u8str_view_t){ 0 };
                          prep[nprep++] = (typeof(prep[0])){ (const low_cst_t *const *)ln->kids, wf, ln->nkids, st[nst].ctx, false, true, tline }; any_with = true; }
                nst++;
            } else if (veq(h, "collect")) {
                if (ln->nkids < 3 || !is_atom(ln->kids[1]) || !veq(ln->kids[1]->tok.lex, "into")) { ir_fail(c, "E-IR-UNSUP", "the collect terminal is `collect into <mut slice>` (RFC-0010 §6.5)", tline); return; }
                if (ir_pipe_has_with(ln)) { ir_fail(c, "E-PIPE-WITH", "`collect into` calls no op, so it takes no context — `with` belongs to `filter`·`map`·`any`·`all`·`scan`·`fold`·`zip`·`enumerate` (RFC-0121 §6.1)", tline); return; }
                term = 0; term_arg = ln->kids[2];
                if (nprep + 1 > 24) { ir_fail(c, "E-IR-UNSUP", "pipe: too many locals", tline); return; }
                prep[nprep++] = (typeof(prep[0])){ (const low_cst_t *const *)ln->kids, 2, 3, (proven_size_t)-1, true, false, tline };
            } else if (veq(h, "count")) {
                if (ir_pipe_has_with(ln)) { ir_fail(c, "E-PIPE-WITH", "`count` calls no op, so it takes no context (RFC-0121 §6.1)", tline); return; }
                term = 2;
            } else if (veq(h, "any") || veq(h, "all")) {
                if (ln->nkids < 2 || !is_atom(ln->kids[1])) { ir_fail(c, "E-FOLD-OP", "`any`/`all` needs a predicate op NAME (RFC-0010 §6.2)", tline); return; }
                bool of; proven_size_t oi = ir_def_find_in(c, ln->kids[1]->tok.lex, &of);
                if (!of) { ir_fail(c, "E-FOLD-OP", "any/all names an op that does not exist", tline); return; }
                proven_size_t wf = 0; bool hw = ir_pipe_with(c, ln, 1, &wf, tline); if (c->failed) return;
                if (c->out->defs[oi].nparams != (hw ? 2u : 1u) || c->out->defs[oi].param_cap) {
                    ir_fail(c, hw ? "E-PIPE-CONTEXT-ARG" : "E-FOLD-OP", hw ? "with a context, any/all's predicate takes exactly TWO arguments (the element, the context) (RFC-0121 §6.1)"
                                                                       : "any/all's predicate takes exactly ONE argument (the element)", tline); return; }
                term = veq(h, "any") ? 3 : 4; term_op = oi;
                if (hw) { if (c->nlocals + 1 > IR_MAXLOCALS || nprep + 1 > 24) { ir_fail(c, "E-IR-UNSUP", "pipe: too many locals", tline); return; }
                          term_hasctx = true; term_ctx = c->nlocals++; c->locals[term_ctx].name = (proven_u8str_view_t){ 0 };
                          prep[nprep++] = (typeof(prep[0])){ (const low_cst_t *const *)ln->kids, wf, ln->nkids, term_ctx, true, true, tline }; any_with = true; }
            } else if (veq(h, "fold")) {
                if (ln->nkids < 3 || !is_atom(ln->kids[2])) { ir_fail(c, "E-FOLD-OP", "the fold terminal is `fold <init> <op>` — the op is a NAME (RFC-0010 §6.2)", tline); return; }
                bool of; proven_size_t oi = ir_def_find_in(c, ln->kids[2]->tok.lex, &of);
                if (!of) { ir_fail(c, "E-FOLD-OP", "fold's op names an op that does not exist", tline); return; }
                proven_size_t wf = 0; bool hw = ir_pipe_with(c, ln, 2, &wf, tline); if (c->failed) return;
                if (c->out->defs[oi].nparams != (hw ? 3u : 2u) || c->out->defs[oi].param_cap) {
                    ir_fail(c, hw ? "E-PIPE-CONTEXT-ARG" : "E-FOLD-OP", hw ? "with a context, fold's op takes exactly THREE arguments (acc, element, context) (RFC-0121 §6.1)"
                                                                       : "fold's op takes exactly TWO arguments (acc, element)", tline); return; }
                term = 1; term_op = oi; term_arg = ln->kids[1];
                if (nprep + 2 > 24) { ir_fail(c, "E-IR-UNSUP", "pipe: too many locals", tline); return; }
                prep[nprep++] = (typeof(prep[0])){ (const low_cst_t *const *)ln->kids, 1, 2, acc, true, false, tline };
                if (hw) { if (c->nlocals + 1 > IR_MAXLOCALS) { ir_fail(c, "E-IR-UNSUP", "pipe: too many locals", tline); return; }
                          term_hasctx = true; term_ctx = c->nlocals++; c->locals[term_ctx].name = (proven_u8str_view_t){ 0 };
                          prep[nprep++] = (typeof(prep[0])){ (const low_cst_t *const *)ln->kids, wf, ln->nkids, term_ctx, true, true, tline }; any_with = true; }
            } else {
                // ★ G5 — 닫힌 스테이지 어휘. 모르는 낱말이 스테이지 자리에 오면 **거절**한다. 이것이
                //   융합 보장의 문법적 근거다: 융합 못 할 것은 애초에 **쓸 수 없다**(절벽이 생기지 않는다).
                ir_fail(c, "E-PIPE-STAGE",
                        "this word is not a `pipe` stage. The stage vocabulary is CLOSED (RFC-0010 G5/D-A): "
                        "stages are `filter <op> .`, `map <op> .`, `take <n> .`, `skip <n> .`, `enumerate <op> .`, "
                        "`zip <other> <op> .` and `scan <init> <op> .`, and the pipeline ends with exactly one "
                        "terminal — `collect into <mut slice> .`, `fold <init> <op> .`, `count .`, `any <op> .` "
                        "or `all <op> .`. Fusion here is the "
                        "MEANING, not an optimization, so anything that could not fuse into the single loop is "
                        "not writable as a stage — use an explicit loop, or break the pipeline with an explicit "
                        "intermediate collect", tline);
                return;
            }
        }
        if (term < 0) { ir_fail(c, "E-PIPE-NO-TERMINAL", "a `pipe` must end with exactly ONE terminal: `collect into <mut slice> .` or `fold <init> <op> .` (RFC-0010 G1)", tline); return; }

        // ★★★★★ **값을 잃는 변환은 암묵적으로 일어나지 않는다** (정본 §6.2.5(1) · 결함 노트 #59, 2026-09-16).
        //   `map` 이 `u64` 를 내는데 `collect into` 가 `mut slice u8` 이면 통과했고, 값이 **조용히
        //   감겼다**(1000 → 232 · 2000 → 208, VM·네이티브 같음). 좁히는 일은 이름 붙은 연산으로
        //   **적어서** 한다(`narrow`·`narrow_wrap`·`narrow_sat`). 여기서는 그 이름이 없다.
        if (term == 0 && term_arg && is_atom(term_arg)) {
            proven_u8str_view_t prod = { 0 };                  // 마지막 map 이 내는 타입 이름
            for (proven_size_t z = nst; z-- > 0; )
                if (st[z].kind == ST_MAP) { prod = c->out->defs[st[z].op].out_tyname; break; }
            if (prod.size) {
                proven_u8 pw = ir_field_size(prod);            // 바이트
                bool df; proven_size_t dslot = ir_local_find(c, term_arg->tok.lex, &df);
                proven_u8 dw = 0;
                if (df && c->locals[dslot].elem.known) dw = (proven_u8)(c->locals[dslot].elem.bits / 8);
                if (pw && dw && pw > dw)
                    ir_fail(c, "E-TYPE-COLLECT",
                            "this `collect into` would put a WIDER value into a narrower buffer, and "
                            "that loses bits silently (measured: 1000 became 232). A conversion that "
                            "loses value never happens implicitly here (§6.2.5) — put a `map` in front "
                            "that says which narrowing you mean (`narrow`, `narrow_wrap`, `narrow_sat`, "
                            "`narrow_try`), or collect into a buffer of the produced width", tline);
                if (c->failed) return;
            }
        }

        // ── preamble
        if (term == 0 || term == 2) { ir_emit(c, IRW_CONST, 0); ir_emit(c, IRW_STORE, (proven_i64)j); }
        else if (term == 3) { ir_emit(c, IRW_CONST, 0); ir_emit(c, IRW_STORE, (proven_i64)acc); }   // any: 기본 false
        else if (term == 4) { ir_emit(c, IRW_CONST, 1); ir_emit(c, IRW_STORE, (proven_i64)acc); }   // all: 기본 true
        proven_size_t ou = 0;
        if (term == 0) { if (c->nlocals >= IR_MAXLOCALS) { ir_fail(c, "E-IR-UNSUP", "pipe: too many locals", tline); return; }
                         ou = c->nlocals++; c->locals[ou].name = (proven_u8str_view_t){ 0 }; }
        // ★ 루프 밖에서 **한 번** 평가하는 것들(fold 초깃값 · collect 받는 자리 · zip 짝 · scan 초깃값 · `with` 식).
        //   `with` 가 없으면 옛 차례(끝 단계의 것 먼저, 그다음 단계 차례) — 있으면 절 차례 그대로(RFC-0121 §6.3).
        //   준비 중 하나가 실패(`try` · panic)하면 뒤 준비도 순회도 없다 — 차례대로 흐르는 코드라 저절로 그렇다.
        (void)nzip;
        for (int pass = 0; pass < 2; pass++)
            for (proven_size_t q = 0; q < nprep; q++) {
                if (any_with ? pass == 1 : ((pass == 0) != prep[q].term)) continue;
                proven_size_t ap = prep[q].from;
                ir_value(c, (low_cst_t **)prep[q].kids, &ap, prep[q].to);
                if (c->failed) return;
                if (ap != prep[q].to) {
                    ir_fail(c, "E-PIPE-WITH", "the context after `with` is ONE expression and it ends at the stage's `.` — here words are left "
                            "over. Several settings go into one value without references (a struct) (RFC-0121 §6.1)", prep[q].line);
                    return;
                }
                ir_emit(c, IRW_STORE, (proven_i64)(prep[q].slot == (proven_size_t)-1 ? ou : prep[q].slot));
            }
        for (proven_size_t z = 0; z < nst; z++)
            if (st[z].kind == ST_TAKE || st[z].kind == ST_SKIP || st[z].kind == ST_ENUM || st[z].kind == ST_ZIP)
                { ir_emit(c, IRW_CONST, 0); ir_emit(c, IRW_STORE, (proven_i64)st[z].ctr); }   // ★ scan 은 제외 — init 을 든다
        ir_emit(c, IRW_CONST, 0); ir_emit(c, IRW_STORE, (proven_i64)i);

        // ── 단일 루프: cond
        proven_size_t cond = c->code.len;
        proven_size_t ov = 0;
        if (nexth != (proven_size_t)-1) {
            // ★★★ **pull 소스에서는 take 소진을 *pull 전에* 검사한다** (2026-07-23, F1 수정).
            //   `send it next` 는 소스를 **한 칸 전진시키는 부수효과**다 — take 한도를 다 채운
            //   뒤에도 루프 머리가 무조건 pull 하면 **원소 하나를 뽑아 버린다**(다음 pipe 가 그
            //   다음 원소부터 봐서 조용히 유실). 슬라이스 소스는 인덱스라 무해하지만 pull 은 아니다.
            //   ⇒ take 카운터가 남았을 때만(모든 take 스테이지에 대해 ctr < n) pull 한다.
            //   단락 스테이지(§8-2)의 "상류를 멈춘다" 를 pull 소스에서도 지키는 자리다.
            ov = c->nlocals++; c->locals[ov].name = (proven_u8str_view_t){ 0 };
            proven_size_t tk_brz[9]; proven_size_t ntk = 0;
            for (proven_size_t z = 0; z < nst; z++)
                if (st[z].kind == ST_TAKE) {
                    ir_emit(c, IRW_LOAD, (proven_i64)st[z].ctr); ir_emit(c, IRW_CONST, st[z].n); ir_emit(c, IRW_LT, PU64);
                    tk_brz[ntk++] = ir_emit(c, IRW_BRZ, 0);   // 소진 → pull 건너뛰고 종료로
                }
            // ★ **collect 의 sink-full 단락도 pull 전에** — out 이 꽉 차면(§6.5 유계 싱크) 더는 pull 안 한다.
            //   전엔 sink-full 을 pull **뒤** cond 의 AND 에서만 봐, 꽉 찬 뒤에도 원소 하나를 뽑아 버렸다
            //   (take 없는 map→collect 가 len-0 싱크에도 소스를 한 칸 전진시켰다 — 같은 근인).
            ir_emit(c, IRW_LOAD, (proven_i64)sv); ir_emit(c, IRW_CALL, (proven_i64)nexth);
            ir_emit(c, IRW_STORE, (proven_i64)ov);
            ir_emit(c, IRW_LOAD, (proven_i64)ov); ir_emit(c, IRW_ISSOME, 0);
            // is_some 결과가 스택에 있다 — take 소진이면 그 자리에 0(거짓)을 놓아 루프를 끝낸다.
            if (ntk) {
                proven_size_t past = ir_emit(c, IRW_BR, 0);
                for (proven_size_t q = 0; q < ntk; q++) ir_at(c, tk_brz[q])->a = (proven_i64)c->code.len;
                ir_emit(c, IRW_CONST, 0);   // 소진 경로 — 계속 여부 = 거짓
                ir_at(c, past)->a = (proven_i64)c->code.len;
            }
        } else {
            ir_emit(c, IRW_LOAD, (proven_i64)i); ir_emit(c, IRW_LOAD, (proven_i64)sv); ir_emit(c, IRW_LEN, 0); ir_emit(c, IRW_LT, PU64);
        }
        proven_size_t brz = ir_emit(c, IRW_BRZ, 0);

        // ── 스테이지 폴드: x 를 스택 위에 두고 filter 는 건너뛰기, map 은 재바인딩
        proven_size_t skips[8]; proven_size_t nskip = 0;
        proven_size_t sc_break[10]; proven_size_t nsc = 0;
        if (nexth != (proven_size_t)-1) { ir_emit(c, IRW_LOAD, (proven_i64)ov); ir_emit(c, IRW_SOMEVAL, 0); }   // x = some_value o
        else { ir_emit(c, IRW_LOAD, (proven_i64)sv); ir_emit(c, IRW_LOAD, (proven_i64)i); ir_emit(c, IRW_INDEX, 0); }   // x = src[i]
        proven_size_t xv = c->nlocals++; c->locals[xv].name = (proven_u8str_view_t){ 0 };
        ir_emit(c, IRW_STORE, (proven_i64)xv);
        for (proven_size_t z = 0; z < nst; z++) {
            if (st[z].kind == ST_FILTER) {
                ir_emit(c, IRW_LOAD, (proven_i64)xv);
                if (st[z].hasctx) ir_emit(c, IRW_LOAD, (proven_i64)st[z].ctx);   // ★ 문맥은 늘 마지막 인자(RFC-0121 §6.1)
                ir_emit(c, IRW_CALL, (proven_i64)st[z].op);
                skips[nskip++] = ir_emit(c, IRW_BRZ, 0);          // 통과 못하면 이 원소는 버린다
            } else if (st[z].kind == ST_TAKE) {
                // 이미 n 개를 통과시켰으면 **루프를 곧장 빠져나온다**(상류 중지).
                ir_emit(c, IRW_LOAD, (proven_i64)st[z].ctr); ir_emit(c, IRW_CONST, st[z].n); ir_emit(c, IRW_LT, PU64);
                proven_size_t go = ir_emit(c, IRW_BRZ, 0);        // 남은 몫이 없으면 탈출
                ir_emit(c, IRW_LOAD, (proven_i64)st[z].ctr); ir_emit(c, IRW_CONST, 1); ir_emit(c, IRW_ADD, PU64);
                ir_emit(c, IRW_STORE, (proven_i64)st[z].ctr);
                proven_size_t pass = ir_emit(c, IRW_BR, 0);
                ir_at(c, go)->a = (proven_i64)c->code.len;
                sc_break[nsc++] = ir_emit(c, IRW_BR, 0);
                ir_at(c, pass)->a = (proven_i64)c->code.len;
            } else if (st[z].kind == ST_SCAN) {
                // acc = op(acc, x) ; 흘려보내는 값 = acc (원소 수는 그대로 1:1)
                ir_emit(c, IRW_LOAD, (proven_i64)st[z].ctr); ir_emit(c, IRW_LOAD, (proven_i64)xv);
                if (st[z].hasctx) ir_emit(c, IRW_LOAD, (proven_i64)st[z].ctx);
                ir_emit(c, IRW_CALL, (proven_i64)st[z].op); ir_emit(c, IRW_STORE, (proven_i64)st[z].ctr);
                ir_emit(c, IRW_LOAD, (proven_i64)st[z].ctr); ir_emit(c, IRW_STORE, (proven_i64)xv);
            } else if (st[z].kind == ST_ENUM) {
                // x = f(i, x) — 인덱스는 **이 스테이지에 도달한 순번**이다(앞선 filter 를 반영한다)
                ir_emit(c, IRW_LOAD, (proven_i64)st[z].ctr); ir_emit(c, IRW_LOAD, (proven_i64)xv);
                if (st[z].hasctx) ir_emit(c, IRW_LOAD, (proven_i64)st[z].ctx);
                ir_emit(c, IRW_CALL, (proven_i64)st[z].op); ir_emit(c, IRW_STORE, (proven_i64)xv);
                ir_emit(c, IRW_LOAD, (proven_i64)st[z].ctr); ir_emit(c, IRW_CONST, 1); ir_emit(c, IRW_ADD, 0);
                ir_emit(c, IRW_STORE, (proven_i64)st[z].ctr);
            } else if (st[z].kind == ST_ZIP) {
                // 짧은 쪽이 끝나면 **전체 종료**(단락) — 그래야 두 흐름의 짝이 항상 맞는다
                ir_emit(c, IRW_LOAD, (proven_i64)st[z].ctr); ir_emit(c, IRW_LOAD, (proven_i64)st[z].oth);
                ir_emit(c, IRW_LEN, 0); ir_emit(c, IRW_LT, PU64);
                proven_size_t go2 = ir_emit(c, IRW_BRZ, 0);
                ir_emit(c, IRW_LOAD, (proven_i64)xv);
                ir_emit(c, IRW_LOAD, (proven_i64)st[z].oth); ir_emit(c, IRW_LOAD, (proven_i64)st[z].ctr); ir_emit(c, IRW_INDEX, 0);
                if (st[z].hasctx) ir_emit(c, IRW_LOAD, (proven_i64)st[z].ctx);
                ir_emit(c, IRW_CALL, (proven_i64)st[z].op); ir_emit(c, IRW_STORE, (proven_i64)xv);
                ir_emit(c, IRW_LOAD, (proven_i64)st[z].ctr); ir_emit(c, IRW_CONST, 1); ir_emit(c, IRW_ADD, PU64);
                ir_emit(c, IRW_STORE, (proven_i64)st[z].ctr);
                proven_size_t pass2 = ir_emit(c, IRW_BR, 0);
                ir_at(c, go2)->a = (proven_i64)c->code.len;
                sc_break[nsc++] = ir_emit(c, IRW_BR, 0);
                ir_at(c, pass2)->a = (proven_i64)c->code.len;
            } else if (st[z].kind == ST_SKIP) {
                // 앞 n 개는 버린다(세면서). 단락이 아니다 — 뒤 원소는 계속 흐른다.
                ir_emit(c, IRW_LOAD, (proven_i64)st[z].ctr); ir_emit(c, IRW_CONST, st[z].n); ir_emit(c, IRW_LT, PU64);
                proven_size_t keep = ir_emit(c, IRW_BRZ, 0);      // 이미 다 버렸으면 통과
                ir_emit(c, IRW_LOAD, (proven_i64)st[z].ctr); ir_emit(c, IRW_CONST, 1); ir_emit(c, IRW_ADD, PU64);
                ir_emit(c, IRW_STORE, (proven_i64)st[z].ctr);
                skips[nskip++] = ir_emit(c, IRW_BR, 0);           // 이 원소는 버린다 → i++ 로
                ir_at(c, keep)->a = (proven_i64)c->code.len;
            } else {
                ir_emit(c, IRW_LOAD, (proven_i64)xv);
                if (st[z].hasctx) ir_emit(c, IRW_LOAD, (proven_i64)st[z].ctx);
                ir_emit(c, IRW_CALL, (proven_i64)st[z].op);
                ir_emit(c, IRW_STORE, (proven_i64)xv);            // 원소 재바인딩(SSA)
            }
        }
        // ── sink — ★★ X-0062 (소유자 결정 ⓒ): 꽉 찬 자리에서 **말없이 멈추지 않는다.** 전엔 cond 에 `j < len(out)` 을
        //   두어 남은 원소를 조용히 버렸다. 이제 찬 뒤에 원소가 오면 이 쓰기의 경계 검사가 멈춘다(VM·네이티브 같다).
        if (term == 0) {
            ir_emit_sinkfull_guard(c, ou, j);   // ★ X-0065 — 넘침은 «받는 자리가 찼다» 로 이름을 대고 멈춘다
            ir_emit(c, IRW_LOAD, (proven_i64)ou); ir_emit(c, IRW_LOAD, (proven_i64)j); ir_emit(c, IRW_LOAD, (proven_i64)xv);
            ir_emit(c, IRW_ISTORE, 0);
            ir_emit(c, IRW_LOAD, (proven_i64)j); ir_emit(c, IRW_CONST, 1); ir_emit(c, IRW_ADD, PU64); ir_emit(c, IRW_STORE, (proven_i64)j);
        } else if (term == 2) {
            ir_emit(c, IRW_LOAD, (proven_i64)j); ir_emit(c, IRW_CONST, 1); ir_emit(c, IRW_ADD, 0); ir_emit(c, IRW_STORE, (proven_i64)j);
        } else if (term == 3 || term == 4) {
            // ★ 단락 종료(§6.3): any 는 첫 참에서, all 은 첫 거짓에서 **루프를 곧장 빠져나온다**.
            //   그래서 무한이 아닌 소스에서도 필요한 만큼만 읽는다 — 이것이 §8-8 pull 소스의 전제이기도 하다.
            ir_emit(c, IRW_LOAD, (proven_i64)xv);
            if (term_hasctx) ir_emit(c, IRW_LOAD, (proven_i64)term_ctx);
            ir_emit(c, IRW_CALL, (proven_i64)term_op);
            if (term == 4) ir_emit(c, IRW_NOT, 0);            // all: 술어가 거짓일 때 빠져나온다
            proven_size_t cont = ir_emit(c, IRW_BRZ, 0);      // 조건이 0 이면 계속
            ir_emit(c, IRW_CONST, term == 3 ? 1 : 0); ir_emit(c, IRW_STORE, (proven_i64)acc);
            sc_break[nsc++] = ir_emit(c, IRW_BR, 0);          // 루프 밖으로
            ir_at(c, cont)->a = (proven_i64)c->code.len;
        } else {
            ir_emit(c, IRW_LOAD, (proven_i64)acc); ir_emit(c, IRW_LOAD, (proven_i64)xv);
            if (term_hasctx) ir_emit(c, IRW_LOAD, (proven_i64)term_ctx);
            ir_emit(c, IRW_CALL, (proven_i64)term_op); ir_emit(c, IRW_STORE, (proven_i64)acc);
        }
        for (proven_size_t z = 0; z < nskip; z++) ir_at(c, skips[z])->a = (proven_i64)c->code.len;   // 버린 원소도 i++ 로
        if (nexth == (proven_size_t)-1) { ir_emit(c, IRW_LOAD, (proven_i64)i); ir_emit(c, IRW_CONST, 1); ir_emit(c, IRW_ADD, PU64); ir_emit(c, IRW_STORE, (proven_i64)i); }
        ir_emit(c, IRW_BR, (proven_i64)cond);
        ir_at(c, brz)->a = (proven_i64)c->code.len;
        for (proven_size_t z = 0; z < nsc; z++) ir_at(c, sc_break[z])->a = (proven_i64)c->code.len;
        // ── epilogue: 파이프라인은 **식**이다(§6.1) — 값을 남긴다. 문장 자리면 호출자가 DROP 한다.
        ir_emit(c, IRW_LOAD, (proven_i64)((term == 0 || term == 2) ? j : acc));
        if (!as_value) ir_emit(c, IRW_DROP, 0);
        return;
}

static void ir_stmt(ir_ctx_t *c, const low_cst_t *f) {
    // ★ 문장 하나가 곧 form 하나다 — **여기가 수리 id 를 고르는 문맥**이다(2026-08-26).
    //   `ir_node` 의 FORM 분기만으로는 부족했다: 블록 문장은 `ir_block` → `ir_stmt` 로 와서
    //   그 분기를 안 지나므로 `cur_form` 이 바깥 `fn` 폼에 머물렀고, 그러면 줄 나눔을 못 본다.
    //   (실측으로 드러났다 — 기대 `R-JOIN-LINE` 인데 `R-FEWER-PARAMS` 가 나왔다.)
    const low_cst_t *prev_form = c->cur_form;
    c->cur_form = f;
    ir_stmt_inner(c, f);
    c->cur_form = prev_form;
}

static void ir_stmt_inner(ir_ctx_t *c, const low_cst_t *f) {
    if (c->failed || f->kind != LOW_CST_FORM || f->nkids == 0 || !is_atom(f->kids[0])) {
        if (!c->failed && f->kind == LOW_CST_FORM) { ir_run(c, f->kids, 0, f->nkids); ir_emit(c, IRW_DROP, 0); }
        return;
    }
    low_kw_t kw = f->kids[0]->tok.kw;
    // ★★★ **`task_group do … end`** (RFC-0009 D3-b — 구조적 동시성) — 문맥 문장 헤드(렉서 키워드 아님).
    //   본문 안의 `spawn <op> [args]` 는 **태스크**를 큐잉하고(SC1: 그룹 안에서만), 그룹 **end** 는
    //   전원을 quiescence 까지 **join** 한다(SC2 — 어떤 태스크도 그룹보다 오래 살지 못한다). 태스크
    //   실행 순서는 결정적 스케줄러가 정하고, interleaving 오라클이 그 순서 불변을 검사한다.
    //   ★ 아직 없는 것(정직히): `await`(결과 수확) · `cancel_on_error`(취소 전파) · `detach` · child_of region.
    if (kw == LOW_KW_NONE && veq(f->kids[0]->tok.lex, "pipe")) { ir_pipe(c, f, 1, false); return; }
    // ★★★ **어휘 `region` 블록** — `region <이름> <종류> do … end` (SPEC-004 §4.5 도입형태 ①).
    //   명세가 의미를 이미 못 박아 뒀다: **수명 = 스코프**, MVP region 은 **범위 기반 일괄 free**
    //   (객체별 free 없음), arena 는 **bump**. ⇒ 하강은 두 줄이다: 진입에서 범프 커서의 **표식**을
    //   지역에 담고, 블록이 끝나면 그 표식으로 **되돌린다**.
    //   ★ 그래서 `free` 라는 낱말이 필요 없다 — 스코프가 곧 해제 시점이고, 그것이 명세의 선택이다.
    //   ★★ 안전은 정적으로 지킨다(low_check 의 E-REGION-ESCAPE): 이 블록에서 할당한 슬라이스가
    //     밖으로 새면 거절한다. 되돌리기가 곧 use-after-free 이기 때문이다.
    // ★★★ **`borrow <이름> be <식> do … end`** — 빌린 것의 수명을 **스코프로 못 박는다**.
    //   ☞ 왜 필요한가: `lib/pool.low` 의 세대 검사는 **문에서 한 번** 돈다. 그 문을 지나면
    //     평범한 슬라이스가 나오는데(그래서 op 이 이중화되지 않는다), 그 슬라이스를 **든 채**
    //     블록을 해제하면 아무도 안 잡는다 — **조용한 데이터 오염**이고, 이 프로젝트가
    //     반복해서 *"거절보다 나쁘다"* 고 적어 온 그것이다.
    //   ⇒ 빌림을 어휘적으로 가둔다. 검사는 low_check 가 한다(E-BORROW-ESCAPE · E-BORROW-EXCL).
    //   ★ 언어는 `pool` 을 모른다 — 규칙이 **일반적**이다: 빌려준 자에게 말을 걸지 마라.
    if (kw == LOW_KW_NONE && veq(f->kids[0]->tok.lex, "borrow")) {
        const low_cst_t *blk = NULL;
        proven_size_t bi = form_block_index(f);
        if (bi < f->nkids) blk = f->kids[bi];
        else if (f->nkids >= 3) {
            const low_cst_t *last = f->kids[f->nkids - 1];
            if (last && last->kind == LOW_CST_FORM && last->nkids &&
                last->kids[last->nkids - 1]->kind == LOW_CST_BLOCK)
                blk = last->kids[last->nkids - 1];
        }
        // `borrow <이름> be <식…> do … end` — `be` 뒤부터 블록 앞까지가 식이다.
        proven_size_t be = 0;
        for (proven_size_t i = 1; i < f->nkids; i++)
            if (is_atom(f->kids[i]) && f->kids[i]->tok.kw == LOW_KW_BE) { be = i; break; }
        if (!blk || !be || be < 2 || !is_atom(f->kids[1])) {
            ir_fail(c, "E-IR-UNSUP",
                    "`borrow` needs a name, a value and a block: "
                    "`borrow <name> be <expr> do … end`. The borrow lives for the block and not "
                    "one statement longer — that is the whole point", f->line);
            return;
        }
        // ★★★ **값이 괄호 없는 이름이면 파서가 블록을 그 이름에 붙여 버린다** (2026-07-20).
        //   `borrow v be s do … end` 는 `[borrow][v][be][FORM(s, BLOCK)]` 으로 파싱된다 —
        //   `make pairr do…end` 와 **같은 모양**(§2.3 R2)이다. 전엔 `f->kids` 를 그대로 먹어
        //   **블록까지 값의 일부로** 평가했고, `v` 에는 슬라이스가 아닌 것이 들어갔다.
        //   ⇒ 증상은 블록 **안에서** 났다: `index v 0` 이 E-VM-TYPE. 원인과 멀찍이 떨어진 자리다.
        //   ★ 기존 픽스처가 전부 `borrow b be (subslice s 0 2)` 처럼 **괄호 식**이라 안 보였다 —
        //     괄호가 있으면 블록이 직접 자식이 된다(교훈 6: 픽스처의 모양이 감사의 시야다).
        //   ⇒ 머리 붙은 폼이면 **그 폼의 머리 낱말들**을 값으로 이어 붙인다.
        low_cst_t *vk[64]; proven_size_t nvk = 0;
        if (bi < f->nkids) {
            for (proven_size_t i = be + 1; i < bi && nvk < 64; i++) vk[nvk++] = f->kids[i];
        } else {
            for (proven_size_t i = be + 1; i + 1 < f->nkids && nvk < 64; i++) vk[nvk++] = f->kids[i];
            const low_cst_t *lf = f->kids[f->nkids - 1];
            for (proven_size_t j = 0; j + 1 < lf->nkids && nvk < 64; j++) vk[nvk++] = lf->kids[j];
        }
        if (!nvk) {
            ir_fail(c, "E-IR-UNSUP", "`borrow` needs a value after `be`", f->line);
            return;
        }
        proven_size_t pos = 0;
        ir_value(c, vk, &pos, nvk);            // ★ 값 **하나** — ir_run 은 구간을 다 먹어 arity 가 어긋난다
        proven_size_t slot = ir_local_declare(c, f->kids[1]->tok.lex, f->line);
        ir_emit(c, IRW_STORE, (proven_i64)slot);
        for (proven_size_t i = 0; i < blk->nkids && !c->failed; i++) ir_stmt(c, blk->kids[i]);
        return;
    }
    if (kw == LOW_KW_NONE && veq(f->kids[0]->tok.lex, "region")) {
        // ★ 블록은 **직접 자식**일 수도, `<종류> do … end` 처럼 **머리 붙은 폼 안**에 있을 수도
        //   있다(파서가 `make pairr do…end` 를 묶는 것과 같은 모양 — §2.3 R2).
        const low_cst_t *blk = NULL;
        proven_size_t bi = form_block_index(f);
        if (bi < f->nkids) blk = f->kids[bi];
        else if (f->nkids >= 3) {
            const low_cst_t *last = f->kids[f->nkids - 1];
            if (last && last->kind == LOW_CST_FORM && last->nkids &&
                last->kids[last->nkids - 1]->kind == LOW_CST_BLOCK)
                blk = last->kids[last->nkids - 1];
        }
        if (!blk || f->nkids < 3 || !is_atom(f->kids[1])) {
            ir_fail(c, "E-IR-UNSUP",
                    "`region` needs a name, a kind and a block: `region <name> <kind> do … end`. "
                    "The lifetime IS the scope (SPEC-004 §4.5) — that is why there is no `free`",
                    f->line);
            return;
        }
        // ★★★★★ **종류는 닫힌 어휘 여덟이다** (2026-08-31, WO-0176 · 원장 N-03).
        //   전에는 이 자리를 **읽지도 않았다** — `region r banana do` 가 그대로 통과했다(실측).
        //   정본 SPEC-004 §291 은 여덟을 적어 두었고 표준 명세는 *"이 판의 처리기는 구별하지
        //   아니한다"* 고 정직하게 적었는데, 그 「구별하지 않음」이 **아무 낱말이나 받는 것**까지
        //   뜻하지는 않는다. 소유자가 규범 어휘로 굳히기로 정했다.
        //   ☞ *어휘를 닫는 것과 동작을 가르는 것은 다른 일이다.* 여기서 닫는 것은 **어휘**다 —
        //     여덟은 다 받고, 그 밖은 거절한다. 동작의 구별은 아직 없으며 그 사실은 명세가 적는다.
        // ★ 종류가 **어디 있는지**는 파스 모양에 달렸다: `region r arena do … end` 는
        //   `[region, r, FORM(arena, BLOCK)]` 로 접힌다(블록이 앞 낱말에 붙는다).
        //   그래서 `kids[2]` 가 아톰이면 그것이고, 아니면 **마지막 폼의 첫 아톰**이다.
        //   ☞ *트리의 모양을 추측하지 말고 두 모양을 다 다룬다 — 한 모양만 보면 조용히 안 본다.*
        const low_cst_t *kindn = NULL;
        if (f->nkids >= 3 && is_atom(f->kids[2])) kindn = f->kids[2];
        else if (f->nkids >= 3) {
            const low_cst_t *lf = f->kids[f->nkids - 1];
            if (lf && lf->kind == LOW_CST_FORM && lf->nkids && is_atom(lf->kids[0]))
                kindn = lf->kids[0];
        }
        if (kindn) {
            static const char *RKIND[] = { "stack", "frame", "arena", "static",
                                           "heap", "mmap", "disk", "device" };
            proven_u8str_view_t kv = kindn->tok.lex;
            bool okk = false;
            for (size_t ki = 0; ki < sizeof RKIND / sizeof *RKIND; ki++)
                if (veq(kv, RKIND[ki])) { okk = true; break; }
            if (!okk) {
                ir_fail(c, "E-REGION-KIND",
                        "a region's KIND is a closed vocabulary of eight: stack · frame · arena · "
                        "static · heap · mmap · disk · device (SPEC-004 §4.5). The name you chose is "
                        "not one of them. A kind says WHERE the storage comes from, and a reader must "
                        "be able to tell that from the word alone — if any word were allowed, the word "
                        "would carry no information",
                        f->line);
                return;
            }
        }
        // ★★★★ **뿌리는 종류가 정한다** (RFC-0112 D3(4) · WO-0211): `heap` 은 자라는 뿌리(1),
        //   그 밖의 일곱은 고정 창(0). 두 뿌리는 **따로** 표식하고 되감는다 — 그래서 안쪽 힙 영역을
        //   되감아도 바깥 고정 창의 값이 산다(D3(6)).
        proven_u8 root = (kindn && veq(kindn->tok.lex, "heap")) ? 1 : 0;
        // 표식을 지역 슬롯에 담아 둔다(블록 안의 이름과 충돌하지 않게 내부 이름을 쓴다).
        // ★★★ **깊이마다 다른 슬롯** (WO-0211 에서 찾음). 전엔 이름 하나(`  region.mark`)를 모든 깊이가
        //   나눠 써서 — `ir_local_declare` 는 같은 이름이면 **같은 슬롯**을 준다 — 안쪽 블록의 표식이
        //   바깥 것을 덮었다. 바깥 `end` 는 **안쪽 표식까지만** 되감았다(바이트가 조용히 샌다).
        static const char *MK[IR_MAXREGION] = { "  region.mark0", "  region.mark1", "  region.mark2",
            "  region.mark3", "  region.mark4", "  region.mark5", "  region.mark6", "  region.mark7" };
        if (g_nrg >= IR_MAXREGION) {
            ir_fail(c, "E-IR-LIMIT",
                    "regions nest at most 8 deep in this version — the lowering keeps one mark per depth. "
                    "This is a compiler limit, not a rule of the language: split the op", f->line);
            return;
        }
        proven_u8str_view_t mk = proven_u8str_view_from_cstr(MK[g_nrg]);
        proven_size_t slot = ir_local_declare(c, mk, f->line);
        ir_emit(c, IRW_RMARK, (proven_i64)root);
        ir_emit(c, IRW_STORE, (proven_i64)slot);
        g_rgroot[g_nrg] = root;
        g_rgslot[g_nrg] = slot;
        g_rgnames[g_nrg++] = f->kids[1]->tok.lex;
        // ★★★ RFC-0112 D5(5) (WO-0212) — 블록의 몸은 **블록**으로 낮춘다. 전엔 문장 하나씩(`ir_stmt`)
        //   돌려서 `guard`·`match`·`asm` 처럼 형제를 함께 보는 문장이 영역 안에서만 «S5 코어 밖» 이었다(F9).
        ir_block(c, blk);
        if (g_nrg) g_nrg--;                        // 블록을 나가면 그 이름은 더 못 쓴다
        ir_emit(c, IRW_LOAD, (proven_i64)slot);
        ir_emit(c, IRW_RRESET, (proven_i64)root);
        return;
    }
    if (kw == LOW_KW_NONE && veq(f->kids[0]->tok.lex, "task_group")) {
        proven_size_t b = form_block_index(f);
        if (b == f->nkids) { ir_fail(c, "E-IR-UNSUP", "`task_group` needs a `do … end` block", f->line); return; }
        // ★★★ **cancel_on_error** (RFC-0009 SC4) — 그룹 헤드의 절. 한 자식이 오류로 끝나면 형제를 취소한다.
        bool cancel = false;
        for (proven_size_t j = 1; j < b; j++)
            if (is_atom(f->kids[j]) && veq(f->kids[j]->tok.lex, "cancel_on_error")) cancel = true;
        bool prev = c->in_tgroup; c->in_tgroup = true;
        // ★★★ **취소 스코프는 중첩된다 — 저장하고 복원한다** (경계 스윕 D). 전엔 cancel 그룹만
        //   진입에 CANCELSCOPE 1, 나감에 **절대 0** 을 냈다. 그래서 두 방향 다 틀렸다:
        //     · cancel 안의 **평범** 그룹은 아무것도 안 내 바깥의 1 을 상속 → 평범 자식이 취소됐다.
        //     · cancel 안의 그룹이 나가며 0 으로 리셋 → 바깥 cancel 이 join 전에 스코프를 잃었다.
        //   ⇒ **모든** 그룹이 진입에 자기 값으로 세우고 나감에 **바깥 값으로 복원**한다(값이 바뀔 때만
        //     낸다). join(SCHED)·본문 await 는 이 그룹의 스코프 안에서 돈다. 전역 하나로 정직하게 중첩된다.
        bool prev_scope = c->cur_cancel;
        if (cancel != prev_scope) ir_emit(c, IRW_CANCELSCOPE, cancel ? 1 : 0);   // 진입: 이 그룹의 스코프
        c->cur_cancel = cancel;
        ir_block(c, f->kids[b]);       // 본문 — spawn <op> 들이 태스크를 큐잉
        c->in_tgroup = prev;
        ir_emit(c, IRW_SCHED, 0);      // 그룹 end — 전원 join (SC2), 이 그룹 스코프 안에서
        ir_emit(c, IRW_DROP, 0);       // schedule 이 민 단위값을 버린다(task_group 은 문장)
        c->cur_cancel = prev_scope;
        if (cancel != prev_scope) ir_emit(c, IRW_CANCELSCOPE, prev_scope ? 1 : 0);   // 나감: 바깥 값 복원
        return;
    }
    switch (kw) {
        // ★ `expect <cond> .` — 테스트의 단언. **문장**이다(값을 남기지 않는다).
        case LOW_KW_EXPECT: {
            if (f->nkids < 2) { ir_fail(c, "E-IR-UNSUP", "expect needs a condition", f->line); return; }
            ir_run(c, f->kids, 1, f->nkids - 1);
            ir_emit(c, IRW_ASSERT, 4);      // a=4 — 진입(0)·ensures(1)·when(2) 과 구별한다
            return;
        }
        // ★ `drop x .` — **해제(release)**. 전면적이고 중단하지 않는다: 그래서 조용히 일어나도 된다.
        //   실패할 수 있거나 기다려야 하는 것(flush·commit·close)은 **해제가 아니라 완결(completion)**
        //   이고, 완결은 **명시적인 op 호출**이어야 한다 — 암묵적 drop 은 그 실패를 건네줄 자리가 없다.
        //   (RFC-0058) 지금 이 언어의 값에는 소멸자가 없다. 그러므로 해제는 **값을 버리는 것**이다.
        //   소멸자가 생기면 여기가 그 자리다. 그 전까지 이 코드는 자기가 확인한 것만 주장한다.
        case LOW_KW_DROP: {
            if (f->nkids < 2) { ir_fail(c, "E-IR-UNSUP", "drop needs a value", f->line); return; }
            // ★ RFC-0135 S2 — 블록 끝에 돌려줄 할당기 바이트면 지금 돌려주고, 받은 자리를 비워 블록 끝에서는 건너뛰게 한다
            if (f->nkids == 2 && is_atom(f->kids[1])) {
                proven_u8str_view_t dn = f->kids[1]->tok.lex;
                for (proven_size_t d = g_nrel; d-- > 0; )
                    if (g_relname[d].size == dn.size + 1 && g_relname[d].ptr[0] == '$' && memcmp(g_relname[d].ptr + 1, dn.ptr, dn.size) == 0) {
                        ir_release_one(c, d);
                        ir_emit(c, IRW_WRAP_NONE, 0);
                        ir_emit(c, IRW_STORE, (proven_i64)g_relslot[d]);
                        return;
                    }
            }
            ir_run(c, f->kids, 1, f->nkids - 1);
            ir_emit(c, IRW_DROP, 0);
            return;
        }
        case LOW_KW_VAR: case LOW_KW_LET: {
            if (f->nkids < 2 || !is_atom(f->kids[1])) { ir_fail(c, "E-IR-UNSUP", "malformed binding", f->line); return; }
            proven_size_t be = f->nkids;
            for (proven_size_t i = 0; i < f->nkids; i++)
                if (is_atom(f->kids[i]) && f->kids[i]->tok.kw == LOW_KW_BE) { be = i; break; }
            proven_size_t vstart = (be < f->nkids) ? be + 1 : 2;
            // ★ `using <출처>` 가 남아 있으면 using 패스(나무 모드)를 거치지 않은 것이다(`--flat`) — 할당자를 채우지 못한
            //   채 값을 묶으면 타입과 값이 어긋난다(RFC-0132 §13.7 에서 드러남). 정직하게 거절한다.
            for (proven_size_t i = 2; i < be && i < f->nkids; i++)
                if (is_atom(f->kids[i]) && veq(f->kids[i]->tok.lex, "using") && !f->kids[i]->synth) {
                    ir_fail(c, "E-IR-UNSUP", "a binding that names its allocator (`using`) is resolved by the tree pass — "
                            "`--flat` does not run it (RFC-0112 D8(6))", f->line);
                    return;
                }
            ir_vec_context(c, f, 2, (be < f->nkids) ? be : f->nkids);
            ir_bset_context(c, f, 2, (be < f->nkids) ? be : f->nkids);
            // ★★★★★ **값이 없는 `be` 가 조용히 0 을 냈다** (2026-08-25 · RFC-0100 · 소유자 결정).
            //   `let x be u8 .` 이 통과하고 `x` 가 0 이 됐다 — **소스 어디에도 없는 값**이다.
            //   RFC-0098 이 op 의 끝에서 닫은 것과 **같은 병이 옆자리에 남아 있었다.**
            //   ☞ 사람이 이 자리에 오는 가장 흔한 길은 **`let y be f64 .5 .`** 이다:
            //     앞선 점은 **닫개**라(§6.1.6) `be` 뒤가 비고, 쓴 사람은 자기가 값을 적었다고
            //     믿는다. 그래서 진단이 그 함정을 **직접 짚는다** — *"값이 없다"* 만 말하면
            //     원인을 못 찾는다.
            //   ★ 이 언어에는 *"선언만 하고 나중에 채운다"* 는 표기가 **없다**(`let`·`var` 둘 다
            //     `be` 로 값을 받는다). 그러므로 값 없는 `be` 는 **어떤 뜻도 아니다.**
            if (vstart >= f->nkids) {
                ir_fail(c, "E-LET-NOVALUE",
                        "this binding has NO VALUE — `be` is followed by nothing, and the tool used "
                        "to quietly bind 0 there: a value that appears NOWHERE in your source. "
                        "If you wrote a float like `.5`, that is the cause: a LEADING DOT is a "
                        "form CLOSER here, not part of a number, so the value vanished before it "
                        "was ever read. Write `0.5`. Otherwise give the binding a value — this "
                        "language has no way to declare a name and fill it in later",
                        f->line);
                return;
            }
            // ★★★★★ **소유가 없는 값은 베껴진다** (정본 §8.3(1) · 결함 노트 #40, 2026-09-16).
            //
            //   `var q be point p .` 뒤의 `set (field q x) 99 .` 가 **`p` 의 칸도** 바꿨다 —
            //   VM 은 구조체를 상자에 담고, 묶기가 그 **상자를 함께 가리켰기** 때문이다.
            //   그러면 `let p` 가 «바뀌지 않는다» 고 말해 놓고 바뀐다(§6.5.1). 정본은 옮기기와
            //   베끼기를 가른다: 소유가 있으면 옮기고, 없으면 **벤다.** 그래서 구조체 이름을
            //   그대로 묶는 자리에서 **칸을 읽어 새 값을 짓는다**(액터 인스턴스는 제외 — 그것은
            //   값이 아니라 실행 단위이고, 베끼면 상태가 둘이 된다).
            bool copied_struct = false;
            if (vstart + 1 == f->nkids && is_atom(f->kids[vstart]) &&
                f->kids[vstart]->tok.kind == LOW_TOK_IDENT && f->kids[vstart]->tok.kw == LOW_KW_NONE) {
                bool sfnd; proven_size_t sslot2 = ir_local_find(c, f->kids[vstart]->tok.lex, &sfnd);
                if (sfnd && c->locals[sslot2].tyname.size) {
                    bool stf; proven_size_t si2 = ir_struct_find(c->out, c->locals[sslot2].tyname, &stf);
                    if (stf) {
                        const low_ir_struct_t *sd = &c->out->structs[si2];
                        bool plain = sd->nf > 0 && !sd->is_actor_state && !sd->is_mmio &&
                                     !(sd->f[0].name.size == 2 && sd->f[0].name.ptr[0] == (proven_u8)'$');
                        bool has_actor_def = false;      // actor 인스턴스 타입이면 베끼지 않는다
                        for (proven_size_t q = 0; q < c->out->ndefs; q++)
                            if (c->out->defs[q].is_actor && c->out->defs[q].param_sidx[0] == (proven_u8)si2)
                                { has_actor_def = true; break; }
                        if (plain && !has_actor_def && c->out->nmakes < IR_MAXMAKES &&
                            sd->nf <= IR_MAKE_MAXF) {
                            proven_size_t my2 = c->out->nmakes++;
                            low_ir_make_t mk2 = { .type_name = sd->name, .nfields = 0 };
                            for (proven_size_t q = 0; q < sd->nf; q++) {
                                mk2.fields[mk2.nfields++] = sd->f[q].name;
                                ir_node(c, f->kids[vstart]);                 // 원본 값
                                ir_emit(c, IRW_FIELD, (proven_i64)ir_field_intern(c, sd->f[q].name));
                            }
                            c->out->makes[my2] = mk2;
                            ir_emit(c, IRW_MAKE, (proven_i64)my2);
                            copied_struct = true;
                        }
                    }
                }
            }
            proven_size_t code_before = c->code.len;
            // ★ RFC-0132 T2b-2 — `var` 에 묶는 나열 리터럴은 **쓸 수 있는 틀 안 자리**(§13.2 ⓐ)다. 값이 나열 하나면
            //   그 나열에게 틀을 요구한다(`ir_lit_list` 가 한 번 쓰고 끈다).
            if (!copied_struct && f->kids[0]->tok.kw == LOW_KW_VAR && vstart + 1 == f->nkids) {
                const low_cst_t *lv = f->kids[vstart];
                while (lv && lv->kind == LOW_CST_GROUP && lv->nkids == 1) lv = lv->kids[0];
                if (lv && lv->kind == LOW_CST_FORM && lv->nkids >= 2 && is_atom(lv->kids[0]) && lv->kids[0]->tok.kw == LOW_KW_LIT &&
                    is_atom(lv->kids[1]) && (veq(lv->kids[1]->tok.lex, "array") || veq(lv->kids[1]->tok.lex, "slice")))
                    c->lit_frame = true;
            }
            if (!copied_struct) ir_run(c, f->kids, vstart, f->nkids - vstart);
            c->lit_frame = false;
            // ★★★★★ **`f32` 자리의 리터럴은 32 비트로 반올림한다** (IEEE 754 · 결함 노트 #83, 2026-09-16).
            //   `let a be f32 0.1 .` 의 값이 **f64 의 0.1 그대로** 남아 있었다 — 넓혀서 비교하면
            //   `f64` 의 0.1 과 같다고 나왔다(VM·네이티브 같음). 셈을 한 번 거친 값만 32 비트가
            //   됐으니, **같은 타입의 값 둘이 어디서 왔느냐에 따라 다른 수**였다.
            {
                bool decl_f32 = false;
                proven_size_t de2 = (be < f->nkids) ? be : f->nkids;
                for (proven_size_t z = 2; z < de2; z++)
                    if (is_atom(f->kids[z]) && veq(f->kids[z]->tok.lex, "f32")) { decl_f32 = true; break; }
                if (decl_f32)
                    for (proven_size_t q = code_before; q < c->code.len; q++) {
                        low_ir_ins_t *in2 = ir_at(c, q);
                        if (!in2 || in2->w != IRW_FCONST) continue;
                        double dv2; memcpy(&dv2, &in2->a, 8);
                        float fv2 = (float)dv2; double rv2 = (double)fv2;
                        memcpy(&in2->a, &rv2, 8);
                    }
            }
            proven_u8 decl_bset_w = c->bset_w;   // ★ 리셋 전에 선언 폭을 잡아 둔다
            c->vec_lanes = 0; c->vec_esz = 0; c->vec_sign = false; c->bset_w = 0;
            // ★★★★★ **`let` 이 comptime 정수면 이름을 표에 담는다** (2026-08-18).
            //   그래야 `let L be native_lanes u32 .` 뒤의 `vec u32 L` 이 선다 — RFC-0040 D5 가
            //   약속한 *"타깃에게 폭을 물어 **한 번만** 쓴다"* 가 그제야 참이 된다.
            //   ★ `var` 는 담지 않는다: 나중에 `set` 으로 바뀌면 타입 자리의 상수가 낡는다.
            //     **불변인 것만 타입이 될 수 있다.**
            //   ☞ 접는 범위는 폭 자리와 **같은 접기**(리터럴 · `native_lanes t` · 이미 담긴 이름)로
            //     한정한다. 넓히려면 재고 넓힌다 — 여기서 CTFE 전체를 끌어오지 않는다.
            if (f->kids[0]->tok.kw == LOW_KW_LET && vstart < f->nkids && c->ncint < 16) {
                proven_i64 cv = 0; bool got = false;
                if (vstart + 1 == f->nkids) got = ir_fold_lane_expr(c, f->kids[vstart], &cv);
                else if (vstart + 2 == f->nkids && is_atom(f->kids[vstart]) &&
                         is_atom(f->kids[vstart + 1]) &&
                         veq(f->kids[vstart]->tok.lex, "native_lanes")) {
                    proven_u8 esz = ir_field_size(f->kids[vstart + 1]->tok.lex);
                    if (esz) { cv = ir_native_lanes_of(esz); got = true; }
                }
                if (got) { c->cint[c->ncint].name = f->kids[1]->tok.lex;
                           c->cint[c->ncint].val = cv; c->ncint++; }
            }
            proven_size_t slot = ir_local_declare(c, f->kids[1]->tok.lex, f->line);
            c->locals[slot].ty = ity_of_decl_c(c, f, 2, (be < f->nkids) ? be : f->nkids);   // S2
            // ★★★ **지역의 원소 타입도 싣는다** (2026-09-09, REQ-0013). 파라미터만 실었더니
            //   `var v be mut slice u64 view_array u64 buf .` 위의 `lt (index v 0) (index v 1)` 이
            //   여전히 부호 있는 비교였다 — **같은 규칙이 두 곳에 있고 한 곳만 옳은** 그 모양이다
            //   (바로 위 주석이 같은 교훈을 적어 두었다. 교훈 7).
            c->locals[slot].elem = ITY_UNK;
            {
                proven_size_t de = (be < f->nkids) ? be : f->nkids;
                for (proven_size_t z = 2; z + 1 < de; z++) {
                    if (!is_atom(f->kids[z])) continue;
                    proven_u8str_view_t w0 = f->kids[z]->tok.lex;
                    if (veq(w0, "slice") || veq(w0, "vec") || veq(w0, "array")) {
                        c->locals[slot].elem = ity_of_decl_c(c, f, z + 1, z + 2);
                        break;
                    }
                }
            }
            c->locals[slot].bset_w = decl_bset_w;   // ★ 비트셋이면 폭(1..64), 아니면 0

            // ★ 선언된 타입의 **이름** — 다만 **한정자를 벗긴 알맹이**여야 한다.
            //   `var x be owned r …` 의 타입 이름은 **`r`** 이지 `owned` 가 아니다.
            //   ★ 파라미터는 `low_op_header` 의 `core` 가 이미 벗기고 있었다. **지역만 안 벗겼다** —
            //     그래서 `x..take` 가 **`owned.take`** 를 찾다가 E-METHOD-UNDEF 를 냈다.
            //     (같은 규칙이 두 곳에 있고 **한 곳만 옳았다** — 교훈 7 의 작은 사례다.)
            {
                proven_size_t tw = 2;
                while (tw < f->nkids && is_atom(f->kids[tw]) &&
                       (veq(f->kids[tw]->tok.lex, "mut") || veq(f->kids[tw]->tok.lex, "owned") ||
                        veq(f->kids[tw]->tok.lex, "ref") || veq(f->kids[tw]->tok.lex, "mut_ref")))
                    tw++;
                if (tw < f->nkids && is_atom(f->kids[tw])) c->locals[slot].tyname = ir_strip_mod(c, f->kids[tw]->tok.lex);
            }
            // ★ 생성 기반 narrowing (RFC-0080 §4.6). `let b be box.val 42 .` 는 b 가 **val 임을
            //   증명**한다 — guard 없이도 `get b val v` 가 안전하다. RHS 첫 원자가 `<enum>.<변형>`
            //   글루드 이름이면 그 변형으로 좁힌다. (아니면 위 선언에서 이미 {0} 으로 비워졌다.)
            if (vstart < f->nkids && is_atom(f->kids[vstart])) {
                proven_u8str_view_t rv = f->kids[vstart]->tok.lex;
                proven_size_t rd = 0;
                for (proven_size_t i = 0; i < rv.size; i++) if (rv.ptr[i] == '.') { rd = i; break; }
                if (rd && rd + 1 < rv.size) {
                    proven_u8str_view_t reh = { rv.ptr, rd };
                    proven_u8str_view_t rvs = { rv.ptr + rd + 1, rv.size - rd - 1 };
                    proven_size_t rvi;
                    if (ir_enum_variant_of(c, reh, rvs, &rvi)) c->locals[slot].narrowed = rvs;
                }
            }
            ir_emit(c, IRW_STORE, (proven_i64)slot);
            // ★ RFC-0135 S2 (D11) — 값이 «돌려주기가 붙은» 할당기 폼이면 이 블록을 나갈 때 돌려준다
            if (vstart + 1 == f->nkids) {
                const low_cst_t *uv = f->kids[vstart];
                while (uv && uv->kind == LOW_CST_GROUP && uv->nkids == 1) uv = uv->kids[0];
                    if (uv && uv->kind == LOW_CST_FORM && uv->nkids == 4 && is_atom(uv->kids[0]) && uv->kids[0]->synth &&
                    veq(uv->kids[0]->tok.lex, "using")) {
                    if (g_nrel >= IR_MAXREL) { ir_fail(c, "E-IR-LIMIT", "too many allocator releases pending in one op", f->line); return; }
                    if (g_using_ov == (proven_size_t)-1) { ir_fail(c, "E-IR-UNSUP", "allocator release without its reserved bytes", f->line); return; }
                    g_relform[g_nrel] = uv->kids[3]; g_relslot[g_nrel] = g_using_ov; g_relname[g_nrel] = f->kids[1]->tok.lex; g_nrel++;   // 받은 그대로의 option 바이트
                    g_using_ov = (proven_size_t)-1;
                }
            }
            return;
        }
        case LOW_KW_SET: {
            // ★ `set (index s i) v .` — 슬라이스 **원소** 쓰기.
            //   지금까지 set 은 지역 이름만 받았다. 그래서 커널이 결과를 쓸 데가 없었고,
            //   level-1 병렬(R2)의 전제가 빠져 있었다.
            //   장소는 **괄호로 감싼 form** 이다(점-클로저에서 중첩은 괄호가 만든다).
            if (f->nkids >= 3 && f->kids[1]->kind == LOW_CST_GROUP && f->kids[1]->nkids == 1) {
                const low_cst_t *pl = f->kids[1]->kids[0];
                if (pl->kind == LOW_CST_FORM && pl->nkids == 3 && is_atom(pl->kids[0]) &&
                    veq(pl->kids[0]->tok.lex, "idx") && is_atom(pl->kids[1])) {
                    bool sf; proven_size_t sslot = ir_local_find(c, pl->kids[1]->tok.lex, &sf);
                    if (!sf) { ir_fail(c, "E-IR-UNDEF", "set index of an undeclared name", f->line); return; }
                    ir_emit(c, IRW_LOAD, (proven_i64)sslot);   // 슬라이스
                    ir_run(c, pl->kids, 2, 1);                 // 인덱스
                    ir_run(c, f->kids, 2, f->nkids - 2);       // 값
                    ir_emit(c, IRW_ISTORE, 0);
                    return;
                }
                // ★ RFC-0132 T2b-3b — `set (index (field q body) i) v .`: 슬라이스를 내는 **식**의 원소에 쓴다. 구조체의
                //   배열 칸은 레코드가 가진 바이트를 보는 슬라이스라 여기 쓰면 그 레코드가 바뀐다(쓸 수 있는지는 검사층이 본다).
                if (pl->kind == LOW_CST_FORM && pl->nkids == 3 && is_atom(pl->kids[0]) &&
                    veq(pl->kids[0]->tok.lex, "idx") && !is_atom(pl->kids[1])) {
                    ir_node(c, pl->kids[1]);                   // 슬라이스
                    ir_run(c, pl->kids, 2, 1);                 // 인덱스
                    ir_run(c, f->kids, 2, f->nkids - 2);       // 값
                    ir_emit(c, IRW_ISTORE, 0);
                    return;
                }
                // ★ `set (field q x) v .` — 구조체 **필드 쓰기**.
                if (pl->kind == LOW_CST_FORM && pl->nkids >= 3 && is_atom(pl->kids[0]) &&
                    veq(pl->kids[0]->tok.lex, "field") && is_atom(pl->kids[1]) &&
                    is_atom(pl->kids[pl->nkids - 1])) {
                    bool rf; proven_size_t rslot = ir_local_find(c, pl->kids[1]->tok.lex, &rf);
                    if (!rf) { ir_fail(c, "E-IR-UNDEF", "set field of an undeclared name", f->line); return; }
                    // ★ mmio 뷰의 `ro` 레지스터에 쓰면 컴파일 에러(타입 기반 판정).
                    ir_mmio_perm_check(c, pl->kids[1]->tok.lex, pl->kids[2]->tok.lex, true, f->line);
                    if (c->failed) return;
                    // ★★★★★ **정수 마디는 필드가 아니라 자리다** (2026-08-25).
                    //   `field` 를 다단 전위로 넓히며 **읽는 쪽**은 정수를 INDEX 로
                    //   내리게 가르쳤는데, **쓰는 쪽**은 안 가르쳤다. 그래서
                    //   `set (field s 0) 11 .` 이 검사는 통과하고 VM 에서
                    //   `E-VM-TYPE: set field needs a record` 로 죽었다 —
                    //   ★ **읽기와 쓰기는 같은 철자를 쓰면 같은 규칙도 써야 한다.**
                    //   (골든의 차등이 이것을 잡았다: 검사만 보면 초록이었다.)
                    // ★★★★★ **자리도 다단이다** — 마지막 마디만 쓰고, 그 앞은 읽는다.
                    //   `set (field o i a) 30 .` = `o` 를 싣고 `i` 를 **읽어 내려간 뒤**
                    //   `a` 에 쓴다. 읽기 쪽이 다단이 된 날 쓰기 쪽은 `nkids == 3` 에
                    //   묶여 있었고, 그래서 다단 자리는 `E-IR-UNSUP` 였다 —
                    //   ★ **한 철자를 두 방향으로 쓰면 두 방향을 같이 넓혀야 한다.**
                    const low_cst_t *last = pl->kids[pl->nkids - 1];
                    ir_emit(c, IRW_LOAD, (proven_i64)rslot);
                    for (proven_size_t m = 2; m + 1 < pl->nkids; m++) {   // 중간 마디 = 읽기
                        if (!is_atom(pl->kids[m])) {
                            ir_fail(c, "E-IR-UNSUP", "a `field` place segment must be a name "
                                    "or an integer", f->line); return;
                        }
                        proven_i64 mid;
                        if (ir_int_lit(pl->kids[m]->tok.lex, &mid))
                            { ir_emit(c, IRW_CONST, mid); ir_emit(c, IRW_INDEX, 0); }
                        else
                            ir_emit(c, IRW_FIELD,
                                    (proven_i64)ir_field_intern(c, pl->kids[m]->tok.lex));
                    }
                    proven_i64 sidx;
                    if (ir_int_lit(last->tok.lex, &sidx)) {   // 마지막이 자리 번호
                        ir_emit(c, IRW_CONST, sidx);
                        ir_run(c, f->kids, 2, f->nkids - 2);   // 값
                        ir_emit(c, IRW_ISTORE, 0);
                        return;
                    }
                    proven_size_t fi = ir_field_intern(c, last->tok.lex);
                    ir_run(c, f->kids, 2, f->nkids - 2);
                    ir_emit(c, IRW_FSTORE, (proven_i64)fi);
                    return;
                }
                ir_fail(c, "E-IR-UNSUP",
                        "the place forms in the S5 core are `(index <slice> <i>)` and "
                        "`(field <record> <name>)`", f->line);
                return;
            }
            // ★ 붙임점 place — `set p.x 5 .` · `set s.0 5 .` · `set a.b.c 5 .`
            if (f->nkids >= 3 && is_atom(f->kids[1])) {
                bool gis; proven_i64 gfid;
                if (ir_glued_place(c, f->kids[1]->tok.lex, f->line, &gis, &gfid)) {
                    if (c->failed) return;
                    ir_run(c, f->kids, 2, f->nkids - 2);       // 값
                    ir_emit(c, gis ? IRW_ISTORE : IRW_FSTORE, gfid);
                    return;
                }
            }
            if (f->nkids < 3 || !is_atom(f->kids[1])) { ir_fail(c, "E-IR-UNSUP", "set place must be a name in the S5 core", f->line); return; }
            // ★ actor 의 상태 필드에 쓰기 — 그것은 지역이 아니라 **인스턴스의 필드**다.
            if (ir_is_sfield(c, f->kids[1]->tok.lex)) {
                proven_size_t fi = ir_field_intern(c, f->kids[1]->tok.lex);
                ir_emit(c, IRW_LOAD, 0);                   // 슬롯 0 = 인스턴스
                ir_run(c, f->kids, 2, f->nkids - 2);
                ir_emit(c, IRW_FSTORE, (proven_i64)fi);
                return;
            }
            for (proven_size_t q = c->nmel; q-- > 0; )            // ★ RFC-0132 P1 — `set x v` 가 buf 의 그 칸에 쓴다
                if (proven_u8str_view_eq(c->mel_name[q], f->kids[1]->tok.lex)) {
                    ir_emit(c, IRW_LOAD, (proven_i64)c->mel_buf[q]); ir_emit(c, IRW_LOAD, (proven_i64)c->mel_idx[q]);
                    ir_run(c, f->kids, 2, f->nkids - 2);
                    ir_emit(c, IRW_ISTORE, 0);
                    return;
                }
            bool found; proven_size_t slot = ir_local_find(c, f->kids[1]->tok.lex, &found);
            if (!found) { ir_fail(c, "E-IR-UNDEF", "set of an undeclared name", f->line); return; }
            ir_run(c, f->kids, 2, f->nkids - 2);
            ir_emit(c, IRW_STORE, (proven_i64)slot);
            c->locals[slot].narrowed = (proven_u8str_view_t){0};   // ★ 재대입 → 변형 좁힘 무효(RFC-0080 §4.6)
            return;
        }
        case LOW_KW_RETURN: case LOW_KW_GIVE: case LOW_KW_BREAK: case LOW_KW_CONTINUE:
            ir_diverge(c, f->kids, 0, f->nkids, f->line);
            return;
        case LOW_KW_IF: {
            proven_size_t b = form_block_index(f);
            if (b == f->nkids || b < 2) { ir_fail(c, "E-IR-UNSUP", "malformed if", f->line); return; }
            // ★ RFC-0054 D2: 조건이 comptime 상수면 **접는다**. 죽은 가지는 코드 생성에서
            //   사라진다(런타임 비용 0). 그러나 **두 가지 모두 타입 검사를 받는다** —
            //   접히는 것은 코드 생성이지 검사가 아니다. #ifdef 와의 결정적 차이.
            proven_i64 cv;
            if (ir_comptime_cond(c, f, 1, b - 1 + 1, &cv)) {
                c->out->folds++;
                if (cv) ir_block(c, f->kids[b]);
                else if (b + 1 < f->nkids) {
                    if (f->kids[b + 1]->kind == LOW_CST_BLOCK) ir_block(c, f->kids[b + 1]);
                    else ir_stmt(c, f->kids[b + 1]);
                }
                return;
            }
            proven_size_t cbrz[IR_COND_MAXBRZ];
            proven_size_t ncbrz = ir_cond_brz(c, f->kids, 1, b - 1, cbrz);
            ir_block(c, f->kids[b]);
            if (b + 1 < f->nkids) {   // else part: nested if-form or a block
                proven_size_t br = ir_emit(c, IRW_BR, 0);
                for (proven_size_t q = 0; q < ncbrz; q++)
                    ir_at(c, cbrz[q])->a = (proven_i64)c->code.len;
                if (f->kids[b + 1]->kind == LOW_CST_BLOCK) ir_block(c, f->kids[b + 1]);
                else ir_stmt(c, f->kids[b + 1]);
                ir_at(c, br)->a = (proven_i64)c->code.len;
            } else {
                for (proven_size_t q = 0; q < ncbrz; q++)
                    ir_at(c, cbrz[q])->a = (proven_i64)c->code.len;
            }
            return;
        }
        // ★ `loop do … end` — **무조건 반복**(SPEC-002 제어 어휘). `while true` 와 같다.
        //   어휘에 있는데 **구현이 없었다**: break 는 이미 있었으니 조건만 상수 1 이면 된다.
        case LOW_KW_LOOP:
        case LOW_KW_WHILE: {
            proven_size_t b = form_block_index(f);
            if (b == f->nkids || (kw == LOW_KW_WHILE && b < 2)) { ir_fail(c, "E-IR-UNSUP", "malformed while", f->line); return; }
            if (c->nloops >= IR_MAXLOOP) { ir_fail(c, "E-IR-UNSUP", "loop nesting too deep", f->line); return; }
            proven_size_t loop_start = c->code.len;
            // `while pop STACK into NAME do` — pop-as-condition binds NAME (G3 into)
            proven_size_t into = 0;
            for (proven_size_t x = 1; x < b; x++)
                if (is_atom(f->kids[x]) && veq(f->kids[x]->tok.lex, "into")) { into = x; break; }
            if (into) {
                if (!is_atom(f->kids[1]) || !veq(f->kids[1]->tok.lex, "pop") ||
                    into + 1 >= b || !is_atom(f->kids[into + 1])) {
                    ir_fail(c, "E-IR-UNSUP", "unsupported into-binding condition", f->line);
                    return;
                }
                ir_run(c, f->kids, 2, into - 2);   // the stack value
                proven_size_t slot = ir_local_declare(c, f->kids[into + 1]->tok.lex, f->line);
                ir_emit(c, IRW_SPOP_INTO, (proven_i64)slot);
            } else if (kw == LOW_KW_LOOP) {
                ir_emit(c, IRW_CONST, 1);          // 무조건 — 나가는 길은 `break` 뿐이다
            }
            proven_size_t cbrz[IR_COND_MAXBRZ]; proven_size_t ncbrz;
            if (into || kw == LOW_KW_LOOP) { cbrz[0] = ir_emit(c, IRW_BRZ, 0); ncbrz = 1; }
            else ncbrz = ir_cond_brz(c, f->kids, 1, b - 1, cbrz);
            ir_loop_t *lp = &c->loops[c->nloops++];
            lp->rgdepth = g_nrg; lp->reldepth = g_nrel;   // ★ RFC-0112 D5(5) · RFC-0135 S2 — break/continue 가 되감을 경계
            lp->nbrk = 0; lp->ncnt = 0;
            ir_block(c, f->kids[b]);
            ir_emit(c, IRW_BR, (proven_i64)loop_start);
            proven_size_t end = c->code.len;
            for (proven_size_t q = 0; q < ncbrz; q++) ir_at(c, cbrz[q])->a = (proven_i64)end;
            for (proven_size_t i = 0; i < lp->nbrk; i++) ir_at(c, lp->brk[i])->a = (proven_i64)end;
            for (proven_size_t i = 0; i < lp->ncnt; i++) ir_at(c, lp->cnt[i])->a = (proven_i64)loop_start;
            c->nloops--;
            return;
        }
        case LOW_KW_FOR: {
            // for VAR in ITER do … end — ITER is a slice value; iterate its elements
            proven_size_t b = form_block_index(f);
            if (b == f->nkids || b < 3 || !is_atom(f->kids[1])) { ir_fail(c, "E-IR-UNSUP", "malformed for", f->line); return; }
            // ★★ RFC-0132 P1 — `for i count τ n .` · `for i range τ a b [step k] .` · `for x mut buf`
            bool has_where = false;
            for (proven_size_t q = 2; q < b; q++) if (is_atom(f->kids[q]) && f->kids[q]->tok.kw == LOW_KW_NONE && veq(f->kids[q]->tok.lex, "where")) has_where = true;
            if (has_where || (is_atom(f->kids[2]) && f->kids[2]->tok.kw == LOW_KW_BE) ||
                (is_atom(f->kids[2]) && f->kids[2]->tok.kw == LOW_KW_NONE &&
                 (veq(f->kids[2]->tok.lex, "count") || veq(f->kids[2]->tok.lex, "range") || veq(f->kids[2]->tok.lex, "mut")))) {
                ir_for_p1(c, f, b);
                return;
            }
            if (c->nloops >= IR_MAXLOOP || c->nlocals + 3 > IR_MAXLOCALS) { ir_fail(c, "E-IR-UNSUP", "loop nesting too deep", f->line); return; }
            proven_size_t it = c->nlocals++, idx = c->nlocals++;   // anonymous slots
            c->locals[it].name = (proven_u8str_view_t){ 0 };
            c->locals[idx].name = (proven_u8str_view_t){ 0 };
            // ★★ 숨은 첨자는 u64 이고 비교·덧셈도 u64 로 낸다 (2026-10-04) — `ir_for_p1` 의 원천 루프와 같은 규칙.
            //   전에는 타입 없이(0) 냈고, 분석이 부호를 몰라 `idx < len it` 를 사실로 세우지 못했다 ⇒ `for c hay do` 의
            //   원소 읽기마다 경계 검사가 남았다(KMP 새 판이 옛 `while lt i n` 판보다 1.7 배 느렸던 까닭).
            c->locals[idx].ty = (ityp_t){ .known = true, .bits = 64, .sign = false };
            const proven_i64 U64f = (proven_i64)(IR_TY_KNOWN | 64);
            proven_size_t var = ir_local_declare(c, f->kids[1]->tok.lex, f->line);
            ir_run(c, f->kids, 2, b - 2);
            ir_emit(c, IRW_STORE, (proven_i64)it);
            ir_emit(c, IRW_CONST, 0);
            ir_emit(c, IRW_STORE, (proven_i64)idx);
            proven_size_t cond = c->code.len;
            ir_emit(c, IRW_LOAD, (proven_i64)idx);
            ir_emit(c, IRW_LOAD, (proven_i64)it);
            ir_emit(c, IRW_LEN, 0);
            ir_emit(c, IRW_LT, U64f);
            proven_size_t brz = ir_emit(c, IRW_BRZ, 0);
            ir_emit(c, IRW_LOAD, (proven_i64)it);
            ir_emit(c, IRW_LOAD, (proven_i64)idx);
            ir_emit(c, IRW_INDEX, 0);
            ir_emit(c, IRW_STORE, (proven_i64)var);
            ir_loop_t *lp = &c->loops[c->nloops++];
            lp->rgdepth = g_nrg; lp->reldepth = g_nrel;   // ★ RFC-0112 D5(5) · RFC-0135 S2 — break/continue 가 되감을 경계
            lp->nbrk = 0; lp->ncnt = 0;
            ir_block(c, f->kids[b]);
            proven_size_t step = c->code.len;   // continue lands on the increment
            ir_emit(c, IRW_LOAD, (proven_i64)idx);
            ir_emit(c, IRW_CONST, 1);
            ir_emit(c, IRW_ADD, U64f);   // idx < len ⇒ 안 넘친다(검사된 덧셈이 분석에 범위를 준다)
            ir_emit(c, IRW_STORE, (proven_i64)idx);
            ir_emit(c, IRW_BR, (proven_i64)cond);
            proven_size_t end = c->code.len;
            ir_at(c, brz)->a = (proven_i64)end;
            for (proven_size_t i = 0; i < lp->nbrk; i++) ir_at(c, lp->brk[i])->a = (proven_i64)end;
            for (proven_size_t i = 0; i < lp->ncnt; i++) ir_at(c, lp->cnt[i])->a = (proven_i64)step;
            c->nloops--;
            return;
        }
        default:
            // ★★ RFC-0132 P3 (§6) — `copy <받는 쪽> <주는 쪽> .` (문맥 낱말 — 같은 이름의 op 이 있으면 그 부름이다)
            if (kw == LOW_KW_NONE && is_atom(f->kids[0]) && veq(f->kids[0]->tok.lex, "copy") && !f->kids[0]->qual_mod.size) {
                bool uf; (void)ir_def_find_in(c, f->kids[0]->tok.lex, &uf);
                if (!uf) {
                    if (f->nkids != 3) { ir_fail(c, "E-IR-UNSUP", "`copy <to> <from> .` takes the receiving slice and the source — one term each (wrap an expression in parentheses)", f->line); return; }
                    ir_node(c, f->kids[1]); ir_node(c, f->kids[2]);
                    ir_emit(c, IRW_SCOPY, 0); ir_emit(c, IRW_DROP, 0);
                    return;
                }
            }
            // ★ `spawn send …` · `send …` 는 **문장으로도** 온다(효과를 위해 부른다 — 값을 버린다).
            //   spawn/send 는 값 컨텍스트(ir_value)에서 처리되므로 표현식-문장 경로로 보낸다.
            if (kw != LOW_KW_NONE && kw != LOW_KW_EXPR && kw != LOW_KW_SPAWN && kw != LOW_KW_SEND) {
                ir_fail(c, "E-IR-UNSUP", "statement outside the S5 core (for/match/fail/…)", f->line);
                return;
            }
            ir_run(c, f->kids, 0, f->nkids);   // expression statement (call for effect)
            ir_emit(c, IRW_DROP, 0);
            return;
    }
}

// ★★★ **정규화 층이 이미 붙여 놨다** (low_cst.c, DECISION-0015).
//   guard 의 `else` 는 이제 **마지막 자식**이고, 바인딩의 `be` 는 **form 안에** 있다.
//   전엔 이 파일이 형제를 앞으로 훑어 **재조립**했다 — 그리고 **여섯 소비자가 각자** 그랬다.
//   ⇒ `ir_binding_split` 은 통째로 사라졌고, `ir_guard` 는 **재조립 없이** 읽기만 한다.
//   **뒷단이 줄어드는 것이 앞단이 옳아졌다는 증거다.**
static proven_size_t ir_guard(ir_ctx_t *c, const low_cst_t *blk, proven_size_t i) {
    const low_cst_t *g = blk->kids[i];
    const low_cst_t *else_form = (g->nkids >= 2 && g->kids[g->nkids - 1]->kind == LOW_CST_FORM &&
                                  g->kids[g->nkids - 1]->nkids &&
                                  is_atom(g->kids[g->nkids - 1]->kids[0]) &&
                                  g->kids[g->nkids - 1]->kids[0]->tok.kw == LOW_KW_ELSE)
                                   ? g->kids[g->nkids - 1] : NULL;
    if (!else_form) {
        ir_fail(c, "E-IR-UNSUP", "guard without a matching else", g->line);
        return i + 1;
    }
    low_cst_t *cond[IR_MAXCOND]; proven_size_t nc = 0;
    for (proven_size_t j = 1; j + 1 < g->nkids && nc < IR_MAXCOND; j++) { cond[nc++] = g->kids[j]; LOW_HWM("ir:guard-cond", nc, IR_MAXCOND); }
    // ★★★ **조건이 잘리면 guard 가 더 약해진다 — 그것은 다른 프로그램이다.**
    //   전엔 32 번째 낱말 뒤를 조용히 버렸다. 2026-08-14 에 `match` 의 `or` 가지에서
    //   고친 것과 **같은 병**이고 같은 처방이다: 자르지 말고 **거절한다**.
    if (g->nkids >= 2 && nc >= IR_MAXCOND && (proven_size_t)(g->nkids - 2) > nc) {
        ir_fail(c, "E-IR-LIMIT",
                "this `guard` condition has more top-level words than the lowerer can carry. "
                "Refusing is the honest answer — a silently dropped condition makes the guard "
                "WEAKER, which is a different program. Split it: bind a part with `let` first",
                g->line);
        return i + 1;
    }
    // ★★★ **변형 narrowing** (RFC-0080 §4.6): 조건이 `isa <지역> <변형>` 이면, else 가지에서는
    //   그 지역이 (2-변형 enum 일 때) **다른 변형**으로, guard 통과 후에는 **그 변형**으로 좁혀진다.
    //   guard 의 else 는 반드시 떠나므로(E-GUARD-FALLTHROUGH), 통과 후 좁힘은 건전하다.
    proven_size_t nslot = (proven_size_t)-1; proven_u8str_view_t nvar = {0}, nsave = {0}, nother = {0};
    if (nc == 3 && is_atom(cond[0]) && veq(cond[0]->tok.lex, "isa") && is_atom(cond[1]) && is_atom(cond[2])) {
        bool lf; proven_size_t ls = ir_local_find(c, cond[1]->tok.lex, &lf);
        if (lf && ir_variant_index(c, cond[2]->tok.lex) >= 0) {
            nslot = ls; nvar = cond[2]->tok.lex; nsave = c->locals[ls].narrowed;
            nother = ir_enum_other2(c, ir_variant_owner(c, nvar), nvar);
        }
    }
    ir_run(c, cond, 0, nc);
    ir_emit(c, IRW_NOT, 0);
    proven_size_t brz = ir_emit(c, IRW_BRZ, 0);   // cond true → skip the diverge
    if (nslot != (proven_size_t)-1) c->locals[nslot].narrowed = nother;   // else 가지: 다른 변형(2-변형만)
    if (else_form->nkids >= 2 && else_form->kids[else_form->nkids - 1]->kind == LOW_CST_BLOCK)
        ir_block(c, else_form->kids[else_form->nkids - 1]);
    else
        ir_diverge(c, else_form->kids, 1, else_form->nkids - 1, else_form->line);
    ir_at(c, brz)->a = (proven_i64)c->code.len;
    if (nslot != (proven_size_t)-1) c->locals[nslot].narrowed = nvar;      // 통과 후: 그 변형으로 좁힘
    (void)nsave;
    return i + 1;
}

// ★ `match <x> do  case <v> . do … end  case <w> . do … end  end` — enum 분기.
//   ★★ SPEC-002 §239 가 정한 모양이다: **match 는 `do` … `end` 로 닫힌다.**
//   처음에 `do` 없이 구현했더니 case 들이 **형제 폼**이 되고, 그 뒤의 `end` 가
//   **바깥 블록을 조용히 닫아 버렸다** — 루프 안의 match 에서 `i = i + 1` 이 **루프 밖으로
//   나갔다.** 무한 루프. 진짜 프로그램(tests/prog/json.low)을 쓰자마자 드러났다.
//   문법이 열려 있으면 파서가 **조용히 다른 뜻으로 읽는다.** 닫힌 문법이 그것을 막는다.
//
//   낮추기: 피검사값을 숨은 지역에 담고, case 마다 `eq tmp <idx>` + BRZ 사슬.
//   enum 값은 **변형의 인덱스**이므로 비교가 곧 정수 비교다(런타임 태그 없음 — 비용 가시).
//
// ★★★ **MM9-B2 — 점프 테이블**(RFC-0081, 사용자 결정 2026-07-25). arm 이 **많고 조밀한**(모두 bare
//   payloadless 변형 또는 정수 리터럴, 가드·or·범위·바인딩 없음) match 는 선형 사슬(O(N)) 대신
//   `IRW_SWITCH` **계산 점프**(O(1))로 낮춘다. 작은 match 는 선형이 빠른 경로에 남는 게 나아 손대지
//   않는다(임계값). 반환값 true = 점프 테이블로 처리함(호출자는 그만둔다).
static bool ir_match_jumptable(ir_ctx_t *c, const low_cst_t *arms, proven_size_t tmp, proven_size_t mline) {
    struct { proven_i64 val; const low_cst_t *body; } arm[64]; proven_size_t narm = 0;
    const low_cst_t *dflt = NULL;
    for (proven_size_t q = 0; q < arms->nkids; q++) {
        const low_cst_t *f = arms->kids[q];
        if (f->kind != LOW_CST_FORM || f->nkids < 3 || !is_atom(f->kids[0]) || f->kids[0]->tok.kw != LOW_KW_CASE) continue;
        // 오직 **단순 패턴**: `case <p> . do…end` = 정확히 [case, p, block]. 가드·or·범위·바인딩이면 nkids>3.
        if (f->nkids != 3 || !is_atom(f->kids[1]) || f->kids[f->nkids - 1]->kind != LOW_CST_BLOCK) return false;
        proven_u8str_view_t p1 = f->kids[1]->tok.lex;
        if (veq(p1, "_")) { if (dflt) return false; dflt = f->kids[2]; continue; }
        if (veq(p1, "some") || veq(p1, "none") || veq(p1, "ok") || veq(p1, "error")) return false;  // option/result → 선형
        proven_i64 v;
        if (ir_int_lit(p1, &v)) { /* 정수 */ }
        else if (veq(p1, "true")) v = 1;
        else if (veq(p1, "false")) v = 0;
        else {
            proven_i64 vi = ir_variant_index(c, p1);
            if (vi < 0 || ir_enum_is_payload(c, vi)) return false;   // P-BIND·페이로드 enum → 선형
            v = vi;
        }
        if (narm >= 64) return false;
        arm[narm].val = v; arm[narm].body = f->kids[2]; narm++;
    }
    if (narm < 6) return false;                       // ★ 작은 match 는 선형 유지(빠른 경로)
    proven_i64 lo = arm[0].val, hi = arm[0].val;
    for (proven_size_t k = 1; k < narm; k++) { if (arm[k].val < lo) lo = arm[k].val; if (arm[k].val > hi) hi = arm[k].val; }
    proven_i64 span = hi - lo + 1;
    if (span < 1 || span > 256 || span > 4 * (proven_i64)narm) return false;   // 너무 희소 → 선형
    // ── 방출: LOAD tmp; [CONST lo; SUB]; SWITCH span; (span+1 BR 테이블); 본문들 ──
    ir_emit(c, IRW_LOAD, (proven_i64)tmp);
    if (lo != 0) { ir_emit(c, IRW_CONST, lo); ir_emit(c, IRW_SUB, 0); }
    ir_emit(c, IRW_SWITCH, (proven_i64)span);
    proven_size_t tbl[257];
    for (proven_i64 k = 0; k <= span; k++) tbl[k] = ir_emit(c, IRW_BR, 0);   // 테이블 자리표시(span + default)
    proven_size_t body_pc[64], ends[66], nend = 0;
    for (proven_size_t k = 0; k < narm; k++) {
        body_pc[k] = c->code.len;
        ir_block(c, arm[k].body);
        if (c->failed) return true;
        ends[nend++] = ir_emit(c, IRW_BR, 0);
    }
    proven_size_t dflt_pc = c->code.len;
    if (dflt) { ir_block(c, dflt); if (c->failed) return true; ends[nend++] = ir_emit(c, IRW_BR, 0); }
    proven_size_t end_pc = c->code.len;
    if (!dflt) dflt_pc = end_pc;
    // 테이블 패치: 값 lo+slot 을 가진 arm → 그 본문, 없으면(빈칸) default.
    for (proven_i64 slot = 0; slot < span; slot++) {
        proven_i64 want = lo + slot;
        proven_size_t tgt = dflt_pc;
        for (proven_size_t k = 0; k < narm; k++) if (arm[k].val == want) { tgt = body_pc[k]; break; }
        ir_at(c, tbl[slot])->a = (proven_i64)tgt;
    }
    ir_at(c, tbl[span])->a = (proven_i64)dflt_pc;   // 범위 밖 → default
    for (proven_size_t e = 0; e < nend; e++) ir_at(c, ends[e])->a = (proven_i64)end_pc;
    c->out->match_sites++; c->out->match_arms += narm; c->out->match_jt++;   // ★ B1/B2 비용 가시
    (void)mline;
    return true;
}
// ★ MM10 — match scrutinee 를 comptime 상수로 접어 본다. scrutinee = m->kids[1..nkids-2].
//   한 노드(리터럴/괄호식)·`comptime <expr>`·`config <opt>` 만 접는다(그 외는 런타임).
static bool ir_scrutinee_fold(const low_cst_t *m, proven_i64 *out) {
    if (m->nkids < 3) return false;
    proven_size_t sn = m->nkids - 2;   // scrutinee 노드 수
    if (sn == 1) return ir_ctfe_fold(m->kids[1], out, IR_CTFE_FUEL);
    if (sn == 2 && is_atom(m->kids[1])) {
        if (veq(m->kids[1]->tok.lex, "comptime")) return ir_ctfe_fold(m->kids[2], out, IR_CTFE_FUEL);
        if (veq(m->kids[1]->tok.lex, "config") && is_atom(m->kids[2])) {
            ir_opt_t *o = ir_opt_find(m->kids[2]->tok.lex);
            if (o) { *out = o->val; return true; }
        }
    }
    return false;
}
static void ir_match(ir_ctx_t *c, const low_cst_t *m) {
    if (m->nkids < 3) { ir_fail(c, "E-IR-UNSUP", "malformed match (needs `match <x> do … end`)", m->line); return; }
    // ★ `do` 를 빠뜨리면 case 들이 **형제 폼**이 되고, 뒤의 `end` 가 **바깥 블록을 닫는다.**
    //   그러면 루프 안의 문장이 루프 밖으로 나간다(무한 루프). **조용히 다른 뜻이 된다.**
    //   그래서 그 모양을 **명시적으로 거부한다.**
    for (proven_size_t q = 1; q + 1 < m->nkids; q++)
        if (is_atom(m->kids[q]) && m->kids[q]->tok.kw == LOW_KW_CASE) {
            ir_fail(c, "E-IR-UNSUP",
                    "`match` needs a do-block: `match <x> do case <v> . do … end … end` "
                    "(SPEC-002 §239). Without it the cases are loose siblings and a following "
                    "`end` closes the ENCLOSING block — a statement after the match would silently "
                    "leave the loop", m->line);
            return;
        }
    const low_cst_t *arms = m->kids[m->nkids - 1];
    if (arms->kind != LOW_CST_BLOCK) {
        ir_fail(c, "E-IR-UNSUP",
                "`match` needs a do-block: `match <x> do case <v> . do … end … end` "
                "(without it the cases are loose siblings and a following `end` closes the "
                "ENCLOSING block — SPEC-002 §239)", m->line);
        return;
    }
    // ★★★ MM10 — **comptime scrutinee 접기**(RFC-0081 B3). scrutinee 가 comptime 상수면(리터럴·
    //   `comptime <expr>`·`config <opt>`) 매칭되는 **arm 하나만** 생성한다(죽은 arm 은 ck 가 여전히
    //   타입검사 — #ifdef 와 다르다). 디스패치 코드 0. 가드·바인딩·복합 패턴이 끼면 접지 않고 정상 하강.
    {
        proven_i64 sc;
        if (ir_scrutinee_fold(m, &sc)) {
            bool any_complex = false;
            for (proven_size_t q = 0; q < arms->nkids && !any_complex; q++) {
                const low_cst_t *f = arms->kids[q];
                if (f->kind != LOW_CST_FORM || f->nkids < 3 || !is_atom(f->kids[0]) || f->kids[0]->tok.kw != LOW_KW_CASE) continue;
                for (proven_size_t j = 2; j + 1 < f->nkids; j++)   // 가드가 있으면 접기 불가(런타임)
                    if (is_atom(f->kids[j]) && veq(f->kids[j]->tok.lex, "when")) { any_complex = true; break; }
            }
            if (!any_complex) {
                for (proven_size_t q = 0; q < arms->nkids; q++) {
                    const low_cst_t *f = arms->kids[q];
                    if (f->kind != LOW_CST_FORM || f->nkids < 3 || !is_atom(f->kids[0]) || f->kids[0]->tok.kw != LOW_KW_CASE || !is_atom(f->kids[1])) continue;
                    proven_u8str_view_t p1 = f->kids[1]->tok.lex;
                    bool matched = false, simple = (f->nkids == 3);   // 바인딩 없는 bare 패턴만 접는다
                    proven_i64 v;
                    if (veq(p1, "_")) { matched = true; }
                    else if (ir_int_lit(p1, &v)) matched = (sc == v);
                    else if (veq(p1, "true")) matched = (sc == 1);
                    else if (veq(p1, "false")) matched = (sc == 0);
                    else if (f->nkids >= 5 && is_atom(f->kids[2]) && veq(f->kids[2]->tok.lex, "to")) {
                        proven_i64 lo, hi;
                        if (ir_int_lit(f->kids[1]->tok.lex, &lo) && ir_int_lit(f->kids[3]->tok.lex, &hi)) { matched = (sc >= lo && sc <= hi); simple = true; }
                    } else {
                        bool is_or = false;
                        for (proven_size_t j = 2; j + 1 < f->nkids; j++) if (is_atom(f->kids[j]) && veq(f->kids[j]->tok.lex, "or")) { is_or = true; break; }
                        if (is_or) {
                            simple = true;
                            for (proven_size_t j = 1; j + 1 < f->nkids; j++) {
                                if (!is_atom(f->kids[j]) || veq(f->kids[j]->tok.lex, "or")) continue;
                                proven_i64 sv;
                                if (ir_int_lit(f->kids[j]->tok.lex, &sv)) { if (sc == sv) matched = true; }
                                else { proven_i64 vi = ir_variant_index(c, f->kids[j]->tok.lex); if (vi >= 0 && sc == vi) matched = true; }
                            }
                        } else {
                            proven_i64 vi = ir_variant_index(c, p1);
                            if (vi >= 0 && !ir_enum_is_payload(c, vi)) matched = (sc == vi);   // bare payloadless 변형
                            else { any_complex = true; break; }   // struct/option/P-BIND/payload → 접지 않음
                        }
                    }
                    if (matched && simple) {
                        ir_block(c, f->kids[f->nkids - 1]);   // ★ 이 arm 하나만 생성
                        c->out->folds++;
                        return;
                    }
                    if (matched && !simple) { any_complex = true; break; }   // 매칭됐으나 바인딩 있음 → 정상 하강
                }
            }
        }
    }

    proven_size_t tmp = ir_local_declare(c, proven_u8str_view_from_cstr("$m"), m->line);
    ir_run(c, m->kids, 1, m->nkids - 2);
    ir_emit(c, IRW_STORE, (proven_i64)tmp);

    // ★ MM9-B2 — 크고 조밀한 단순 match 면 점프 테이블로. 아니면(false) 아래 선형 사슬.
    if (ir_match_jumptable(c, arms, tmp, m->line)) return;

    proven_size_t ends[IR_MAXPATCH]; proven_size_t nend = 0;
    for (proven_size_t q = 0; q < arms->nkids && !c->failed; q++) {
        const low_cst_t *f = arms->kids[q];
        // ★★ **`match` 안의 `else` = 나머지 가지**(정본 A.7 · §6.5.4(6) · 결함 #81). 앞선 `case` 가
        //   닫히는 자리에 붙어 **한 겹 더 싸여** 온다(`FORM > FORM > ATOM else`) — 벗겨 내고 본다.
        //   여기서 받지 않으면 문법과 진단문이 함께 권하는 모양이 `E-IR-UNSUP` 으로 떨어진다.
        if (f->kind == LOW_CST_FORM && f->nkids >= 1 && f->kids[0]->kind == LOW_CST_FORM &&
            f->kids[0]->nkids >= 1 && is_atom(f->kids[0]->kids[0]) &&
            f->kids[0]->kids[0]->tok.kw == LOW_KW_ELSE)
            f = f->kids[0];
        if (f->kind == LOW_CST_FORM && f->nkids >= 2 && is_atom(f->kids[0]) &&
            f->kids[0]->tok.kw == LOW_KW_ELSE) {
            const low_cst_t *eb = f->kids[f->nkids - 1];
            if (eb->kind != LOW_CST_BLOCK) {
                ir_fail(c, "E-IR-UNSUP", "`else` needs a do-block", m->line); return;
            }
            ir_block(c, eb);                       // 조건 없이 — 나머지를 전부 받는다
            break;                                 // 그 뒤 가지는 없다(검사가 죽은 코드로 문다)
        }
        if (f->kind != LOW_CST_FORM || f->nkids < 3 || !is_atom(f->kids[0]) ||
            f->kids[0]->tok.kw != LOW_KW_CASE || !is_atom(f->kids[1])) {
            ir_fail(c, "E-IR-UNSUP", "malformed case", m->line); return;
        }
        const low_cst_t *cb = f->kids[f->nkids - 1];
        if (cb->kind != LOW_CST_BLOCK) { ir_fail(c, "E-IR-UNSUP", "case needs a do-block", m->line); return; }
        // ★ MM6 — 가드 `when`: 패턴이 맞아도 가드 식이 참이라야 arm 선택. `when` 은 패턴의 끝을
        //   가른다 — 패턴은 [1, pat_end), 가드 식은 [when_idx+1, nkids-1), 블록은 nkids-1.
        proven_size_t when_idx = 0;
        for (proven_size_t j = 2; j + 1 < f->nkids; j++)
            if (is_atom(f->kids[j]) && veq(f->kids[j]->tok.lex, "when")) { when_idx = j; break; }
        bool has_guard = (when_idx != 0);
        proven_size_t pat_end = has_guard ? when_idx : (f->nkids - 1);   // 패턴 원자는 [1, pat_end)

        // ── 패턴 검사식을 방출한다(스택에 bool 을 남긴다). `_` 는 is_wild=참(항상 참). ──
        bool is_wild = false;
        proven_i64 bind_vi = -1;   // 단일 변형이면 그 인덱스(페이로드 바인딩용), 아니면 -1
        low_irw_t opt_extract = (low_irw_t)0;   // MM3b: option/result 페이로드 추출 opcode(SOMEVAL/OKVAL/ERRVAL)
        proven_u8str_view_t opt_bind = { 0 };   // MM3b: option/result 바인딩 이름
        low_irw_t nest_ex = (low_irw_t)0;       // MM7: 중첩 — outer 페이로드 추출 opcode
        const low_cst_t *nest_inner = NULL;     // MM7: 중첩 내부 패턴(FORM `(some x)` 또는 atom `none`)
        proven_u8str_view_t orb_field[8], orb_name[8]; proven_size_t n_orb = 0;   // MM5: 페이로드 or 바인딩
        {
            proven_u8str_view_t p1 = f->kids[1]->tok.lex;
            bool is_or = false;
            for (proven_size_t j = 2; j < pat_end; j++)
                if (is_atom(f->kids[j]) && veq(f->kids[j]->tok.lex, "or")) { is_or = true; break; }
            if (veq(p1, "_") && pat_end == 2) {          // ── MM1 와일드카드
                is_wild = true;
                ir_emit(c, IRW_CONST, 1);
            } else if (is_or) {                          // ── MM5 or-패턴 (+ 페이로드 or, RFC-0081)
                // 브랜치 = `or` 로 나눈 조각. 각 브랜치 = [헤드, 바인딩...]. 페이로드 or 는 모든
                //   브랜치가 **같은 이름**을 바인딩하고, 각 위치의 **필드 이름이 같아야** 한다(그래야 한
                //   FIELD 로 읽는다). 태그 테스트 = 각 브랜치 변형의 (FIELD $t == vi) 를 OR.
                // ★★★★ **넘치면 거절한다 — 자르지 않는다** (RFC-0077 P1-5 감사, 2026-08-14).
                //   전엔 `nbr < 16` 이 넘친 `or` 가지를 **조용히 버렸다.** 17 번째 변형은 태그
                //   테스트에 안 들어가므로, 그 값이 오면 **아무 가지도 안 맞는다** — 진단 없이
                //   **틀린 코드**가 난다. 자르는 표는 검사만 못 하게 하는 것이 아니라 여기서는
                //   **뜻을 바꾼다.** ⇒ 세고, 넘치면 말한다.
                proven_size_t bs[16]; proven_size_t nbr = 0; bs[nbr++] = 1;
                proven_size_t nor = 0;
                for (proven_size_t j = 1; j < pat_end; j++)
                    if (is_atom(f->kids[j]) && veq(f->kids[j]->tok.lex, "or")) {
                        nor++;
                        if (nbr < 16) bs[nbr++] = j + 1;
                    }
                if (nor + 1 > 16) {
                    ir_fail(c, "E-IR-LIMIT",
                            "this pattern has too many `or` branches (the lowering table holds 16). "
                            "It used to DROP the extras silently — and a dropped branch means the "
                            "value it was meant to match falls through to nothing. Split the arm, or "
                            "match on a narrower shape", f->line);
                    return;
                }
                // 첫 브랜치가 헤드 뒤에 바인딩을 가지면 페이로드 or.
                proven_size_t b0_end = (nbr > 1) ? bs[1] - 1 : pat_end;
                bool payload_or = (b0_end - bs[0]) > 1;
                if (payload_or) {
                    proven_size_t nb = b0_end - bs[0] - 1;   // 바인딩 개수
                    bool first = true;
                    for (proven_size_t b = 0; b < nbr; b++) {
                        proven_size_t st = bs[b];
                        proven_size_t en = (b + 1 < nbr) ? bs[b+1] - 1 : pat_end;
                        if (!is_atom(f->kids[st])) { ir_fail(c, "E-IR-UNSUP", "malformed or-pattern branch", m->line); return; }
                        proven_i64 vi = ir_variant_index(c, f->kids[st]->tok.lex);
                        if (vi < 0 || !ir_enum_is_payload(c, vi)) { ir_fail(c, "E-MATCH-ORBIND", "in a payload or-pattern every branch must name a payload-carrying variant of the same enum (`case add l r or mul l r`) — RFC-0081 MM5", m->line); return; }
                        if ((en - st - 1) != nb || (proven_size_t)c->enum_np[vi] != nb) { ir_fail(c, "E-MATCH-ORBIND", "every branch of a payload or-pattern must bind the SAME number of names, one per payload field (RFC-0081 MM5)", m->line); return; }
                        // 위치별 필드/바인딩 이름 일치 검사 + 첫 브랜치에서 기록.
                        for (proven_size_t k = 0; k < nb; k++) {
                            if (!is_atom(f->kids[st + 1 + k])) { ir_fail(c, "E-IR-UNSUP", "an or-pattern binding must be a name", m->line); return; }
                            proven_u8str_view_t bn = f->kids[st + 1 + k]->tok.lex;
                            proven_u8str_view_t fn = c->enum_pname[vi][k];
                            if (b == 0) { orb_field[k] = fn; orb_name[k] = bn; }
                            else {
                                if (!proven_u8str_view_eq(orb_name[k], bn) || !proven_u8str_view_eq(orb_field[k], fn)) {
                                    ir_fail(c, "E-MATCH-ORBIND", "every branch of a payload or-pattern must bind IDENTICAL names, and the variants' fields must line up by name (`add l r or mul l r`, not `add l r or mul r l`) — RFC-0081 MM5", m->line); return;
                                }
                            }
                        }
                        ir_emit(c, IRW_LOAD, (proven_i64)tmp);
                        ir_emit(c, IRW_FIELD, (proven_i64)ir_field_intern(c, (proven_u8str_view_t){ IR_ENUM_TAGF, 2 }));
                        ir_emit(c, IRW_CONST, vi);
                        ir_emit(c, IRW_EQ, 0);
                        if (!first) ir_emit(c, IRW_OR, 0);
                        first = false;
                    }
                    n_orb = nb;   // 바인딩은 brz_pat 뒤에 방출(태그 일치 가지 안에서만)
                } else {
                    bool first = true;
                    for (proven_size_t j = 1; j < pat_end && !c->failed; j++) {
                        if (!is_atom(f->kids[j]) || veq(f->kids[j]->tok.lex, "or")) continue;
                        proven_u8str_view_t sp = f->kids[j]->tok.lex;
                        proven_i64 sv; bool tg = false;
                        if (ir_int_lit(sp, &sv)) { /* 정수 */ }
                        else if (veq(sp, "true")) sv = 1;
                        else if (veq(sp, "false")) sv = 0;
                        else {
                            sv = ir_variant_index(c, sp);
                            if (sv < 0) { ir_fail(c, "E-IR-UNDEF", "an or-pattern branch is not a variant, integer, or bool literal", m->line); return; }
                            if (c->enum_np[sv] > 0) { ir_fail(c, "E-MATCH-ORBIND", "this or-pattern branch names a payload-carrying variant but binds no names — either bind one name per field in EVERY branch (`add l r or mul l r`) or use payloadless variants (RFC-0081 MM5)", m->line); return; }
                            tg = ir_enum_is_payload(c, sv);
                        }
                        ir_emit(c, IRW_LOAD, (proven_i64)tmp);
                        if (tg) ir_emit(c, IRW_FIELD, (proven_i64)ir_field_intern(c, (proven_u8str_view_t){ IR_ENUM_TAGF, 2 }));
                        ir_emit(c, IRW_CONST, sv);
                        ir_emit(c, IRW_EQ, 0);
                        if (!first) ir_emit(c, IRW_OR, 0);
                        first = false;
                    }
                }
                if (c->failed) return;
            } else if (pat_end >= 4 && is_atom(f->kids[2]) && is_atom(f->kids[3]) && veq(f->kids[2]->tok.lex, "to")
                       && ir_int_lit(f->kids[1]->tok.lex, &bind_vi)) {   // ── MM4 범위 (bind_vi 는 임시로 lo)
                proven_i64 lo, hi;
                ir_int_lit(f->kids[1]->tok.lex, &lo);
                if (!ir_int_lit(f->kids[3]->tok.lex, &hi)) { ir_fail(c, "E-IR-UNSUP", "range hi must be an integer literal", m->line); return; }
                bind_vi = -1;   // 범위는 바인딩 없음
                ir_emit(c, IRW_LOAD, (proven_i64)tmp); ir_emit(c, IRW_CONST, lo); ir_emit(c, IRW_GE, 0);
                ir_emit(c, IRW_LOAD, (proven_i64)tmp); ir_emit(c, IRW_CONST, hi); ir_emit(c, IRW_LE, 0);
                ir_emit(c, IRW_AND, 0);
            } else if (veq(p1, "some") || veq(p1, "none") || veq(p1, "ok") || veq(p1, "error")) {
                // ── MM3b: option/result 패턴. some/ok/error 는 페이로드를 바인딩할 수 있다.
                ir_emit(c, IRW_LOAD, (proven_i64)tmp);
                if (veq(p1, "some")) { ir_emit(c, IRW_ISSOME, 0); opt_extract = IRW_SOMEVAL; }
                else if (veq(p1, "none")) { ir_emit(c, IRW_ISSOME, 0); ir_emit(c, IRW_NOT, 0); }
                else if (veq(p1, "ok")) { ir_emit(c, IRW_ISOK, 0); opt_extract = IRW_OKVAL; }
                else { ir_emit(c, IRW_ISERR, 0); opt_extract = IRW_ERRVAL; }
                if (opt_extract && pat_end == 3) {
                    const low_cst_t *sub = f->kids[2];
                    const low_cst_t *inner = sub;
                    while (inner && inner->kind == LOW_CST_GROUP && inner->nkids == 1) inner = inner->kids[0];
                    // ★ MM7 — 내부가 **option/result 패턴**이면 중첩이다(`case ok (some x)`·`case ok none`).
                    proven_u8str_view_t iv = { 0 };
                    if (inner && inner->kind == LOW_CST_FORM && inner->nkids >= 1 && is_atom(inner->kids[0])) iv = inner->kids[0]->tok.lex;
                    else if (is_atom(sub)) iv = sub->tok.lex;
                    // ★★★★ **`case error <갈래>` 의 이름은 갈래다** (결함 노트 #52, 2026-09-16).
                    //   그전에는 여기서 **무조건 묶음 이름**으로 읽었다 — `case error not_digit .` 이
                    //   «어떤 오류든» 을 받았고, `ok` 와 그것 둘만 적은 `match` 가 망라로 통과했으며,
                    //   실제 오류가 `empty` 여도 `not_digit` 가지가 돌았다. 갈래 이름을 적었는데
                    //   **다른 갈래가 그 가지로 들어오는 것**은 §6.6(7)(맨 이름은 언제나 갈래다)이
                    //   `match` 전체에 대해 이미 정한 규율의 정반대다. ⇒ 선언된 갈래면 **갈래**로,
                    //   아니면 그대로 묶음 이름으로 읽는다(오류 값을 통째로 받는 자리는 남는다).
                    bool inner_optres = iv.size && (veq(iv, "some") || veq(iv, "none") || veq(iv, "ok") || veq(iv, "error"));
                    bool inner_variant = !inner_optres && iv.size && is_atom(sub) &&
                                         ir_variant_index(c, iv) >= 0;
                    if (inner_optres || inner_variant) {
                        nest_ex = opt_extract;   // outer 페이로드를 꺼내 내부를 매칭
                        nest_inner = (inner && inner->kind == LOW_CST_FORM) ? inner : sub;
                        opt_extract = (low_irw_t)0;   // outer 자체는 이름 바인딩 안 함(내부가 매칭)
                    } else if (is_atom(sub)) {
                        opt_bind = sub->tok.lex;      // 평범한 페이로드 바인딩(MM3b)
                    } else {
                        ir_fail(c, "E-IR-UNSUP", "an option/result payload here is matched by a name or a nested option/result pattern (`case ok v` / `case ok (some x)`) — RFC-0081 MM7", m->line);
                        return;
                    }
                } else if (opt_extract && pat_end != 2) {
                    ir_fail(c, "E-MATCH-ARITY", "an option/result pattern binds at most one payload name (`case some x` / `case ok v` / `case error e`) — RFC-0081 MM3b", m->line);
                    return;
                }
            } else {                                     // ── MM2 리터럴 / MM3 변형(+바인딩) / P-BIND
                proven_i64 vi = 0;
                if (ir_int_lit(p1, &vi)) {                // 정수 리터럴
                    ir_emit(c, IRW_LOAD, (proven_i64)tmp); ir_emit(c, IRW_CONST, vi); ir_emit(c, IRW_EQ, 0);
                } else if (veq(p1, "true") || veq(p1, "false")) {
                    ir_emit(c, IRW_LOAD, (proven_i64)tmp); ir_emit(c, IRW_CONST, veq(p1, "true") ? 1 : 0); ir_emit(c, IRW_EQ, 0);
                } else {
                    vi = ir_variant_index(c, p1);
                    if (vi >= 0) {                        // 변형 패턴
                        bool tagged = ir_enum_is_payload(c, vi);
                        ir_emit(c, IRW_LOAD, (proven_i64)tmp);
                        if (tagged) ir_emit(c, IRW_FIELD, (proven_i64)ir_field_intern(c, (proven_u8str_view_t){ IR_ENUM_TAGF, 2 }));
                        ir_emit(c, IRW_CONST, vi);
                        ir_emit(c, IRW_EQ, 0);
                        bind_vi = vi;
                    } else if (pat_end == 2) {            // ★ P-BIND (RFC-0020 P-BIND): 변형/리터럴이
                        //   아닌 **한 이름** = scrutinee 전체를 그 이름에 바인딩(항상 매칭). 가드가 흔히 쓴다
                        //   (`case y when gt y 100`). catch-all 이므로 is_wild.
                        is_wild = true;
                        proven_size_t bslot = ir_local_declare(c, p1, m->line);
                        ir_emit(c, IRW_LOAD, (proven_i64)tmp);
                        ir_emit(c, IRW_STORE, (proven_i64)bslot);
                        ir_emit(c, IRW_CONST, 1);         // 항상 참
                    } else {
                        // ★ MM8 — struct 패턴 `case point x a y b .`(평평한 필드/이름 쌍, 사용자 결정
                        //   2026-07-25). 구조체는 태그가 없어 **항상 매칭**(destructure)이다. 페이로드 enum
                        //   레이아웃(`$t` 필드)은 struct 로 등록돼 있으나 그건 변형 타입이라 제외한다.
                        bool sf; proven_size_t si = ir_struct_find(c->out, p1, &sf);
                        bool is_plain_struct = sf && !(c->out->structs[si].nf > 0 &&
                            c->out->structs[si].f[0].name.size == 2 && c->out->structs[si].f[0].name.ptr[0] == '$');
                        if (is_plain_struct) {
                            if ((pat_end - 2) % 2 != 0) { ir_fail(c, "E-MATCH-ARITY", "a struct pattern binds FIELD/NAME pairs: `case point x a y b .` — an odd number of names is malformed (RFC-0081 MM8)", m->line); return; }
                            is_wild = true;   // 구조체는 항상 매칭 = catch-all
                            for (proven_size_t j = 2; j + 1 < pat_end; j += 2) {
                                if (!is_atom(f->kids[j]) || !is_atom(f->kids[j + 1])) { ir_fail(c, "E-IR-UNSUP", "struct-pattern field/name must be names", m->line); return; }
                                proven_u8str_view_t nm = f->kids[j + 1]->tok.lex;
                                if (veq(nm, "_")) continue;   // 무시 필드
                                proven_size_t bslot = ir_local_declare(c, nm, m->line);
                                proven_size_t fi2 = si < c->out->nstructs ? 0 : 0; (void)fi2;
                                for (proven_size_t z = 0; z < c->out->structs[si].nf; z++)
                                    if (proven_u8str_view_eq(c->out->structs[si].f[z].name, f->kids[j]->tok.lex) && c->out->structs[si].f[z].sidx >= 0)
                                        c->locals[bslot].tyname = c->out->structs[c->out->structs[si].f[z].sidx].name;
                                ir_emit(c, IRW_LOAD, (proven_i64)tmp);
                                ir_emit(c, IRW_FIELD, (proven_i64)ir_field_intern(c, f->kids[j]->tok.lex));
                                ir_emit(c, IRW_STORE, (proven_i64)bslot);
                            }
                            ir_emit(c, IRW_CONST, 1);   // 항상 참
                        } else {
                            ir_fail(c, "E-IR-UNDEF", "case names something that is not an enum variant, an integer, a bool literal, a struct, or a single binding name", m->line);
                            return;
                        }
                    }
                }
            }
        }
        proven_size_t brz_pat = ir_emit(c, IRW_BRZ, 0);   // 패턴 불일치 → 다음 arm

        // ── MM7 중첩: outer 가 맞은 이 가지에서 outer 페이로드를 꺼내(안전) 내부 패턴을 **순차** 매칭한다.
        //   (eager AND 는 error 에서 OKVAL 을 실행해 panic 하므로 단락분기여야 한다.) 내부 불일치 →
        //   brz_nested 로 다음 arm. option/result 한 겹만(RFC-0081 MM7). ──
        proven_size_t brz_nested = 0; bool has_nested = false;
        if (nest_ex) {
            proven_size_t t2 = ir_local_declare(c, proven_u8str_view_from_cstr("$mn"), m->line);
            ir_emit(c, IRW_LOAD, (proven_i64)tmp);
            ir_emit(c, nest_ex, 0);
            ir_emit(c, IRW_STORE, (proven_i64)t2);
            // 내부 변형·바인딩 뽑기
            proven_u8str_view_t iv; proven_u8str_view_t ibind = { 0 };
            if (nest_inner->kind == LOW_CST_FORM) {
                iv = nest_inner->kids[0]->tok.lex;
                if (nest_inner->nkids >= 2 && is_atom(nest_inner->kids[1])) ibind = nest_inner->kids[1]->tok.lex;
            } else iv = nest_inner->tok.lex;
            low_irw_t iextract = (low_irw_t)0;
            proven_i64 ivi = (veq(iv, "some") || veq(iv, "none") || veq(iv, "ok") || veq(iv, "error"))
                             ? -1 : ir_variant_index(c, iv);
            ir_emit(c, IRW_LOAD, (proven_i64)t2);
            if (ivi >= 0) {                                  // ★ 안쪽이 **열거 갈래** — 꼬리표를 견준다
                if (ir_enum_is_payload(c, ivi))
                    ir_emit(c, IRW_FIELD, (proven_i64)ir_field_intern(c, (proven_u8str_view_t){ IR_ENUM_TAGF, 2 }));
                ir_emit(c, IRW_CONST, ivi);
                ir_emit(c, IRW_EQ, 0);
            }
            else if (veq(iv, "some")) { ir_emit(c, IRW_ISSOME, 0); iextract = IRW_SOMEVAL; }
            else if (veq(iv, "none")) { ir_emit(c, IRW_ISSOME, 0); ir_emit(c, IRW_NOT, 0); }
            else if (veq(iv, "ok")) { ir_emit(c, IRW_ISOK, 0); iextract = IRW_OKVAL; }
            else { ir_emit(c, IRW_ISERR, 0); iextract = IRW_ERRVAL; }
            brz_nested = ir_emit(c, IRW_BRZ, 0); has_nested = true;
            if (iextract && ibind.size && !veq(ibind, "_")) {
                proven_size_t ibslot = ir_local_declare(c, ibind, m->line);
                ir_emit(c, IRW_LOAD, (proven_i64)t2);
                ir_emit(c, iextract, 0);
                ir_emit(c, IRW_STORE, (proven_i64)ibslot);
            }
        }
        // ── MM5 페이로드 or 바인딩: 태그가 맞은 이 가지에서 공유 필드를 이름에 묶는다. ──
        for (proven_size_t k = 0; k < n_orb; k++) {
            proven_size_t bslot = ir_local_declare(c, orb_name[k], m->line);
            ir_emit(c, IRW_LOAD, (proven_i64)tmp);
            ir_emit(c, IRW_FIELD, (proven_i64)ir_field_intern(c, orb_field[k]));
            ir_emit(c, IRW_STORE, (proven_i64)bslot);
        }
        // ── MM3b option/result 바인딩: 매칭된 이 가지에서 payload 를 추출해 이름에 묶는다. ──
        if (opt_extract && opt_bind.size) {
            proven_size_t obslot = ir_local_declare(c, opt_bind, m->line);
            ir_emit(c, IRW_LOAD, (proven_i64)tmp);
            ir_emit(c, opt_extract, 0);
            ir_emit(c, IRW_STORE, (proven_i64)obslot);
        }
        // ── MM3 페이로드 바인딩 (단일 변형 패턴에 한함): 태그가 맞은 이 가지 안에서만.
        //   nbind = 패턴 원자 중 변형 이름 뒤의 것들 = [2, pat_end). 변형 패턴에만 의미가 있다. ──
        proven_size_t nbind = (pat_end >= 2) ? pat_end - 2 : 0;
        if (bind_vi >= 0 && nbind > 0) {
            proven_u8 np = c->enum_np[bind_vi];
            if (nbind != (proven_size_t)np) {
                ir_fail(c, "E-MATCH-ARITY",
                        "this `case` binds a different number of names than the variant has payload "
                        "fields — bind exactly one name per field, in order (RFC-0081 MM3). Use `_` "
                        "in a field position you do not need (MM8), or drop the bindings to match the "
                        "tag only", m->line);
                return;
            }
            for (proven_size_t j = 0; j < nbind; j++) {
                if (!is_atom(f->kids[2 + j])) { ir_fail(c, "E-IR-UNSUP", "a payload binding must be a name", m->line); return; }
                proven_u8str_view_t bn = f->kids[2 + j]->tok.lex;
                proven_size_t bslot = ir_local_declare(c, bn, m->line);
                c->locals[bslot].tyname = c->enum_pty[bind_vi][j];
                ir_emit(c, IRW_LOAD, (proven_i64)tmp);
                ir_emit(c, IRW_FIELD, (proven_i64)ir_field_intern(c, c->enum_pname[bind_vi][j]));
                ir_emit(c, IRW_STORE, (proven_i64)bslot);
            }
        }

        // ── MM6 가드: 패턴이 맞아도 가드 식이 참이라야 arm 진입. 거짓이면 다음 arm 으로. ──
        proven_size_t brz_guard = 0;
        if (has_guard) {
            proven_size_t gcount = (f->nkids - 1) - (when_idx + 1);   // 가드 식 원자 수
            if (gcount == 0) { ir_fail(c, "E-IR-UNSUP", "`when` needs a guard expression: `case p when <cond> . do…end`", m->line); return; }
            ir_run(c, f->kids, when_idx + 1, gcount);
            brz_guard = ir_emit(c, IRW_BRZ, 0);
        }

        ir_block(c, cb);
        if (nend < IR_MAXPATCH) ends[nend++] = ir_emit(c, IRW_BR, 0);
        ir_at(c, brz_pat)->a = (proven_i64)c->code.len;
        if (has_nested) ir_at(c, brz_nested)->a = (proven_i64)c->code.len;
        if (has_guard) ir_at(c, brz_guard)->a = (proven_i64)c->code.len;
        // ★ 가드 없는 `_` 는 진짜 catch-all — 이후 arm 은 도달불가라 하강을 멈춘다. 가드 있으면 계속.
        if (is_wild && !has_guard) break;
    }
    for (proven_size_t q = 0; q < nend; q++) ir_at(c, ends[q])->a = (proven_i64)c->code.len;
    // ★ MM9/B1 — **비용 가시**: 이 match 의 순차 검사 상한(선형 사슬 = arm 수)을 센다.
    //   결정트리(B2)가 아직이라 최악은 선형이다 — 그 비용을 **도구가 말한다**(Rust 는 안 한다).
    {
        proven_size_t narm = 0;
        for (proven_size_t q = 0; q < arms->nkids; q++) {
            const low_cst_t *a = arms->kids[q];
            if (a->kind == LOW_CST_FORM && a->nkids >= 1 && is_atom(a->kids[0]) && a->kids[0]->tok.kw == LOW_KW_CASE) narm++;
        }
        c->out->match_sites++;
        c->out->match_arms += narm;
        if (narm > c->out->match_worst) c->out->match_worst = narm;
    }
}

static proven_size_t ir_asm_stmt(ir_ctx_t *c, const low_cst_t *blk, proven_size_t i);

static bool ir_asm_clause(ir_ctx_t *c, low_ir_asm_t *a, const low_cst_t *f,
                          proven_size_t s, proven_size_t e) {
    if (s >= e) { ir_fail(c, "E-ASM-TARGET", "an `asm` clause must name its instruction set "
                          "(`asm x86_64 …`) — assembly is NOT portable, and the tool will not "
                          "pretend it is", f->line); return false; }
    a->target = f->kids[s]->tok.lex;

    // ★★★ **asm 절의 이름은 빌드 타깃이고, 그것이 대조된다** (RFC-0041, 2026-08-02 수리).
    //
    //   그전까지 이 이름은 **아무도 안 읽었다.** 뒤끝의 판정이 `low_ir_target()` 이 아니라
    //   문자열 `"x86_64"` 를 **하드코딩**하고 있었고, 그래서 정확히 거꾸로 굴렀다:
    //     · `--target cortex_m` + `asm thumb`  → **거절**(그리고 진단이 *"this build targets
    //       x86_64"* 라고 **거짓을 말했다**) ⇒ 유일한 프리스탠딩 타깃에서 asm 이 **쓸 수 없었다**.
    //     · `--target cortex_m` + `asm x86_64` → **통과**, x86 명령이 ARM 프리스탠딩 빌드에 실렸다.
    //   ☞ 그래서 RFC-0042 §8-4 의 `wfi` 를 쓸 수 없었다. 대표 프로그램이 또 벽을 찾아냈다.
    //
    //   ★ 그리고 판정을 **여기(검사 단계)** 로 올린다: 뒤끝에서만 잡으면 `--check` 가 초록인데
    //     방출이 실패한다 — `E-CAP-NOHOST` 가 고쳤던 바로 그 거짓말이다. 덤으로, 방출이
    //     시작조차 안 하므로 **반쯤 쓰인 C 파일**도 남지 않는다.
    //   ★★ 어휘를 둘로 두지 않는다: ISA 이름표를 따로 만들면 타깃 표와 **갈린다**(교훈 7).
    //      `asm` 이 이름하는 것은 **타깃 표의 이름**이고, 표가 그 권위다.
    if (!low_ir_target_known(a->target)) {
        ir_fail(c, "E-ASM-TARGET-UNKNOWN",
                "this `asm` clause names something that is not a build target. The names are a "
                "CLOSED set — the target table is the authority (x86_64 · arm64 · cortex_m · "
                "riscv64 · mips_be). A name outside it matches NO build, so the op would be "
                "silently dropped or silently mis-built; both are what this RFC exists to prevent",
                f->line);
        return false;
    }
    if (!proven_u8str_view_eq(a->target, proven_u8str_view_from_cstr(low_ir_target()->name))) {
        ir_fail(c, "E-ASM-TARGET",
                "this op's body is assembly for a DIFFERENT target than the one being built. "
                "Assembly is not portable and the tool will not pretend it is: it will neither "
                "drop the op (it would silently do nothing) nor emit it (the assembler would be "
                "handed instructions for another machine). Give the op a portable body, or build "
                "for that target with `--target`", f->line);
        return false;
    }

    // 피연산자: `reg a` = **입력**(값을 레지스터에 싣는다) · `out reg r` = **출력**.
    // ★ `in` 은 **쓸 수 없다** — 이 언어가 **이미 버린 낱말**이다(중위 접근 `b in a`).
    //   버린 철자를 새 뜻으로 되살리면 그것이 곧 동의어이자 함정이다(§2.5).
    for (proven_size_t i = s + 1; i < e; ) {
        proven_u8str_view_t w = f->kids[i]->tok.lex;
        if (veq(w, "clobber")) {
            i++;
            while (i < e && !veq(f->kids[i]->tok.lex, "options")) {
                if (a->nclob < 8) a->clob[a->nclob++] = f->kids[i]->tok.lex;
                i++;
            }
            continue;
        }
        if (veq(w, "options")) {
            i++;
            while (i < e && !veq(f->kids[i]->tok.lex, "clobber")) {
                if (a->nopt < 8) a->opt[a->nopt++] = f->kids[i]->tok.lex;
                i++;
            }
            continue;
        }
        bool is_out = veq(w, "out");
        proven_size_t rp = is_out ? i + 1 : i;
        if (rp + 1 >= e) {
            ir_fail(c, "E-ASM-OPERAND", "an asm operand is `<reg> <name>` (an input) or "
                    "`out <reg> <name>` (the result) — a register with nothing in it says nothing",
                    f->line);
            return false;
        }
        if (a->nops >= 8) { ir_fail(c, "E-IR-UNSUP", "too many asm operands", f->line); return false; }
        low_ir_asmop_t *o = &a->ops[a->nops++];
        o->reg = f->kids[rp]->tok.lex;
        o->name = f->kids[rp + 1]->tok.lex;
        o->is_out = is_out;
        o->slot = -1;
        if (!is_out) {
            bool found = false;
            proven_size_t sl = ir_local_find(c, o->name, &found);
            if (!found) {
                // ★ 진단이 정확해야 한다: 이건 "모르는 이름" 이 아니라 **asm 입력이 아무 값에도
                //   안 묶였다** 는 것이다. 그 레지스터에는 **무엇이든** 들어 있을 수 있다.
                ir_fail(c, "E-ASM-BIND", "this asm input names something that is not a parameter or "
                        "a local — an input operand must BE a value; otherwise the register holds "
                        "whatever happened to be there", f->line);
                return false;
            }
            o->slot = (proven_i16)sl;
        }
        i = rp + 2;
    }
    return true;
}

static const low_cst_t *ir_asm_heredoc(const low_cst_t *nd) {
    while (nd && nd->kind == LOW_CST_FORM && nd->nkids == 1) nd = nd->kids[0];
    if (nd && nd->kind == LOW_CST_ATOM && nd->tok.kind == LOW_TOK_HEREDOC) return nd;
    return NULL;
}

static void ir_asm_tmpl_check(ir_ctx_t *c, low_ir_asm_t *a, proven_u32 line) {
    bool used[8] = { 0 };
    for (proven_size_t i = 0; a->tmpl.size && i + 1 < a->tmpl.size; i++) {
        if (a->tmpl.ptr[i] != (proven_byte_t)'{') continue;
        if (a->tmpl.ptr[i + 1] == (proven_byte_t)'{') { i++; continue; }   // `{{` = 리터럴 중괄호
        proven_size_t j = i + 1;
        while (j < a->tmpl.size && a->tmpl.ptr[j] != (proven_byte_t)'}') j++;
        if (j >= a->tmpl.size) break;
        proven_u8str_view_t nm = { a->tmpl.ptr + i + 1, j - i - 1 };
        bool hit = false;
        for (proven_size_t k = 0; k < a->nops; k++)
            if (proven_u8str_view_eq(nm, a->ops[k].name)) { used[k] = true; hit = true; }
        if (!hit)
            ir_fail(c, "E-ASM-UNBOUND", "the template names an operand the `asm` clause never "
                    "declared — the assembler would either reject it or, worse, read WHATEVER "
                    "register happens to be there", line);
        i = j;
    }
    for (proven_size_t k = 0; k < a->nops; k++)
        if (!used[k])
            ir_fail(c, "E-ASM-UNUSED", "this operand is declared and the template never uses it. "
                    "It burns a register and it says something the assembly does not do — "
                    "unchecked redundancy rots into a lie (PRINCIPLES.md §0)", line);
}

// ★ heredoc 하나를 꺼낸다(한 겹 폼으로 감싸여 올 수 있다).
/* ir_asm_heredoc — ir_asm_stmt 와 한 덩어리라 low_ir.c 로 함께 되돌렸다 */


// ★★★ **op 의 몸이 통째로 asm 인 경우** (RFC-0041 D2) — `asm` 이 op 의 **절**로 왔다.
static bool ir_asm_lower(ir_ctx_t *c, const low_cst_t *f, const low_cst_t *body, low_ir_t *ir) {
    // `asm` 절 구간 찾기
    proven_size_t s = 0, e = 0;
    for (proven_size_t i = 2; i < f->nkids; i++) {
        if (f->kids[i]->kind != LOW_CST_ATOM) continue;
        if (!veq(f->kids[i]->tok.lex, "asm")) continue;
        if (!low_is_clause_word(f->kids[i]->tok.lex)) continue;
        s = i + 1; e = s;
        while (e < f->nkids && f->kids[e]->kind == LOW_CST_ATOM &&
               !low_is_clause_word(f->kids[e]->tok.lex)) e++;
        break;
    }
    if (s == 0) return false;
    if (ir->nasms >= 32) { ir_fail(c, "E-IR-UNSUP", "too many asm blocks", f->line); return true; }
    low_ir_asm_t *a = &ir->asms[ir->nasms];
    memset(a, 0, sizeof(*a));
    a->line = f->line;

    if (!ir_asm_clause(c, a, f, s, e)) return true;

    // 몸 = **heredoc 하나**. 그것이 템플릿이다(RFC-0041 D2).
    const low_cst_t *t = NULL;
    if (body) for (proven_size_t i = 0; i < body->nkids; i++)
        if ((t = ir_asm_heredoc(body->kids[i])) != NULL) break;
    if (!t) {
        ir_fail(c, "E-ASM-BODY", "the body of an asm op IS the assembly: one heredoc "
                "(`text ASM … ASM`). There is nothing else it could be — the compiler does not "
                "mix machine instructions with lowered code", f->line);
        return true;
    }
    a->tmpl = t->tok.lex;
    ir_asm_tmpl_check(c, a, f->line);

    ir_emit(c, IRW_ASM, (proven_i64)ir->nasms);
    ir_emit(c, IRW_RET, 0);
    ir->nasms++;
    return true;
}

/* ir_asm_stmt — **하강이지 분석이 아니다.** 구획 주석이 그은 선 안에 섞여 있었으나
   책임으로 보면 `low_ir.c` 쪽이라 되돌렸다 (WO-0164). ☞ 구획 표지가 곧 책임은 아니다. */



// ═══════════════════════════════════════════════════════════════════════════
// ★★★ **구성 = 손잡이의 선언 + 그 값의 해결** (RFC-0036 D5)
//
//   `build option smp bool default on .`          — 손잡이를 **선언한다**
//   `build option hz choice 100 250 . default 250 . depends smp .`
//   `lowent.config`(생성물) 가 값을 고르고, `config smp` 가 그것을 **comptime 상수**로 읽는다.
//
//   ★ 그러면 꺼진 옵션의 코드는 **생성물에서 사라진다**(접기). 그런데 `#ifdef` 와 달리
//     **두 가지 모두 파싱·타입 검사를 받는다** — 꺼진 가지가 썩지 않는다.

// ★ 이 단위가 낱말 하나를 **어디서든** 쓰는가 — 컴파일러가 아는 타입을 *쓰는 단위에만*
//   등록하려고 쓴다. 토큰을 훑을 뿐이라 값이 싸고, 틀려도 안전한 쪽(등록 안 함)이 아니라
//   **넉넉한 쪽**(등록)으로 틀린다: 주석·문자열에 그 낱말이 있어도 구조체 하나가 늘 뿐이다.
static bool ir_unit_mentions_node(const low_cst_t *n, const char *w) {
    if (!n) return false;
    if (n->kind == LOW_CST_ATOM && veq(n->tok.lex, w)) return true;
    if (n->kind == LOW_CST_FORM && veq(n->tok.lex, w)) return true;
    for (proven_size_t i = 0; i < n->nkids; i++)
        if (ir_unit_mentions_node(n->kids[i], w)) return true;
    return false;
}
static bool ir_unit_mentions(const low_parse_result_t *pr, const char *w) {
    for (proven_size_t i = 0; i < pr->nforms; i++)
        if (ir_unit_mentions_node(pr->forms[i], w)) return true;
    return false;
}

// ★ X-0082 — 몸이 이름 `nm`(구조체 값)의 **안쪽에 쓰는가**: `set (field nm …)` · `set (index (field nm f) i)` ·
//   `let|var v be mut … (field nm f)`(쓸 수 있는 보기). 이름을 통째로 바꾸는 `set nm …` 은 안쪽 쓰기가 아니다.
static const low_cst_t *ipw_unwrap(const low_cst_t *n) { while (n && n->kind == LOW_CST_GROUP && n->nkids == 1) n = n->kids[0]; return n; }
static bool ipw_field_of(const low_cst_t *n, proven_u8str_view_t nm) {
    n = ipw_unwrap(n);
    return n && n->kind == LOW_CST_FORM && n->nkids >= 3 && is_atom(n->kids[0]) && veq(n->kids[0]->tok.lex, "field") &&
           is_atom(n->kids[1]) && proven_u8str_view_eq(n->kids[1]->tok.lex, nm);
}
static bool ipw_arr(const low_ir_struct_t *sd, const low_cst_t *fname) {
    if (!sd || !is_atom(fname)) return false;
    for (proven_size_t z = 0; z < sd->nf; z++) if (sd->f[z].arrn && proven_u8str_view_eq(sd->f[z].name, fname->tok.lex)) return true;
    return false;
}
static bool ir_param_written(const low_cst_t *nd, proven_u8str_view_t nm, const low_ir_struct_t *sd) {
    if (!nd || nd->kind == LOW_CST_ATOM) return false;
    if (nd->kind == LOW_CST_FORM && nd->nkids >= 3 && is_atom(nd->kids[0]) && nd->kids[0]->tok.kw == LOW_KW_SET) {
        const low_cst_t *pl = ipw_unwrap(nd->kids[1]);
        if (ipw_field_of(pl, nm)) {                          // 칸 쓰기 — 끝이 번호면 그 앞 칸의 원소 쓰기(배열 칸일 때만 레코드 쓰기)
            const low_cst_t *last = pl->kids[pl->nkids - 1];
            if (!(is_atom(last) && last->tok.kind == LOW_TOK_NUMBER)) return true;
            if (pl->nkids >= 4 && ipw_arr(sd, pl->kids[pl->nkids - 2])) return true;
        }
        if (pl && pl->kind == LOW_CST_FORM && pl->nkids == 3 && is_atom(pl->kids[0]) && veq(pl->kids[0]->tok.lex, "idx") &&
            ipw_field_of(pl->kids[1], nm) && ipw_arr(sd, ipw_unwrap(pl->kids[1])->kids[2])) return true;
    }
    if (nd->kind == LOW_CST_FORM && nd->nkids >= 4 && is_atom(nd->kids[0]) &&
        (nd->kids[0]->tok.kw == LOW_KW_LET || nd->kids[0]->tok.kw == LOW_KW_VAR)) {
        bool mutw = false; proven_size_t be = nd->nkids;
        for (proven_size_t z = 2; z < nd->nkids; z++) {
            if (is_atom(nd->kids[z]) && nd->kids[z]->tok.kw == LOW_KW_BE) { be = z; break; }
            if (is_atom(nd->kids[z]) && (veq(nd->kids[z]->tok.lex, "mut") || veq(nd->kids[z]->tok.lex, "mut_ref"))) mutw = true;
        }
        if (mutw && be + 2 == nd->nkids && ipw_field_of(nd->kids[be + 1], nm) && ipw_arr(sd, ipw_unwrap(nd->kids[be + 1])->kids[2])) return true;
    }
    for (proven_size_t i = 0; i < nd->nkids; i++) if (ir_param_written(nd->kids[i], nm, sd)) return true;
    return false;
}
low_ir_t low_ir_build(proven_allocator_t work, const low_parse_result_t *pr) {
    // ★★★ `build <mode> .` — **처리표를 켠다** (RFC-0008 §6.5).
    g_bmode = IR_BM_DEBUG; g_bdropped = 0;
    cert_reset();      // ★ 증명서는 **이 빌드의 것**이다 — 앞선 빌드의 것을 물려받지 않는다
    for (proven_size_t i = 0; i < pr->nforms; i++) {
        const low_cst_t *bf = pr->forms[i];
        if (bf->kind != LOW_CST_FORM || bf->nkids < 2 || !is_atom(bf->kids[0])) continue;
        if (!veq(bf->kids[0]->tok.lex, "build") || !is_atom(bf->kids[1])) continue;
        if (veq(bf->kids[1]->tok.lex, "tier")) continue;    // ★ tier 축은 low_check 가 본다
        (void)low_ir_set_build_mode(bf->kids[1]->tok.lex);   // 알 수 없는 모드는 low_check 가 거절한다
    }
    low_ir_t ir = { .ok = true };
    ir.diags = PROVEN_ARRAY_INIT(work, low_diag_t, 8).value;

    proven_result_mem_mut_t mm = work.alloc_fn(work.ctx, IR_MAXMAKES * sizeof(low_ir_make_t), alignof(low_ir_make_t));
    proven_result_mem_mut_t em = work.alloc_fn(work.ctx, IR_MAXERRS * sizeof(proven_u8str_view_t), alignof(proven_u8str_view_t));
    proven_result_mem_mut_t fm = work.alloc_fn(work.ctx, IR_MAXERRS * sizeof(proven_u8str_view_t), alignof(proven_u8str_view_t));
    proven_result_mem_mut_t stm = work.alloc_fn(work.ctx, IR_MAXSTRS * sizeof(proven_u8str_view_t), alignof(proven_u8str_view_t));
    if (mm.err != PROVEN_OK || em.err != PROVEN_OK || fm.err != PROVEN_OK || stm.err != PROVEN_OK) { ir.ok = false; return ir; }
    ir.makes = (low_ir_make_t *)(void *)mm.value.ptr;
    ir.errs = (proven_u8str_view_t *)(void *)em.value.ptr;
    ir.fields = (proven_u8str_view_t *)(void *)fm.value.ptr;
    ir.strs = (proven_u8str_view_t *)(void *)stm.value.ptr;
    // ★ 원소폭 표(1·2·4). 기본은 1 이라 0 으로 두면 안 된다 — 폭 0 은 나눗셈에서 죽는다.
    proven_result_mem_mut_t ewm = work.alloc_fn(work.ctx, IR_MAXSTRS, 1);
    if (ewm.err != PROVEN_OK) { ir.ok = false; return ir; }
    ir.strew = (proven_u8 *)(void *)ewm.value.ptr;
    for (proven_size_t i = 0; i < IR_MAXSTRS; i++) ir.strew[i] = 1;
    // ★ 리터럴 **값** 저장소(이스케이프 디코드 · heredoc 본문). 한계는 이름이 있고,
    //   넘으면 **거절한다**(조용히 자르지 않는다 — 잘린 문자열은 조용히 틀린 값이다).
    proven_result_mem_mut_t sbm = work.alloc_fn(work.ctx, IR_STRBUF, 1);
    if (sbm.err != PROVEN_OK) { ir.ok = false; return ir; }
    ir.strbuf = (proven_u8 *)(void *)sbm.value.ptr;
    ir.strbuf_cap = IR_STRBUF; ir.strbuf_len = 0;
    proven_result_mem_mut_t am = work.alloc_fn(work.ctx, 32 * sizeof(low_ir_asm_t), alignof(low_ir_asm_t));
    if (am.err != PROVEN_OK) { ir.ok = false; return ir; }
    ir.asms = (low_ir_asm_t *)(void *)am.value.ptr;
    memset(ir.asms, 0, 32 * sizeof(low_ir_asm_t));

    ir_ctx_t mod = { .work = work, .out = &ir };   // module-level tables live here
    // ★★★★★ **이전이 끝났다 — 기본이 거절이다** (2026-08-25).
    //   코퍼스의 붙임점 필드 접근이 **0** 이 된 것을 재고 나서 뒤집었다(STRICT 거부
    //   14 = 기준선 14). 되돌릴 문(`LOWENT_ALLOW_GLUED_FIELD`)은 남긴다 — 저장소
    //   **밖**의 코드가 있을 수 있고, 문을 없애는 것과 기본을 바꾸는 것은 다른 일이다.
    mod.allow_glued_field = getenv("LOWENT_ALLOW_GLUED_FIELD") != NULL;
    // ★ 선언된 모듈 이름 수집 (한정 타입 정규화용, RFC-0011).
    for (proven_size_t i = 0; i < pr->nforms && mod.nmodnames < 256; i++) {
        const low_cst_t *f = pr->forms[i];
        if (f->kind == LOW_CST_FORM && f->nkids >= 2 && is_atom(f->kids[0]) &&
            f->kids[0]->tok.kw == LOW_KW_MODULE && is_atom(f->kids[1]))
            mod.modnames[mod.nmodnames++] = f->kids[1]->tok.lex;
    }
    // ★ `use <모듈> from "…" [as <별칭>] .` — 묶인 이름과 그 모듈을 짝지어 둔다(슬라이스 ②).
    //   별칭이 없으면 묶인 이름 = 모듈 이름이다. 이 표가 `Y.op` 를 **Y 가 가리키는 모듈 안에서**
    //   찾게 해 준다 — 그 전까지는 머리를 떼고 맨이름으로 찾았다.
    for (proven_size_t i = 0; i < pr->nforms && mod.nusebind < 256; i++) {
        const low_cst_t *f = pr->forms[i];
        if (f->kind != LOW_CST_FORM || f->nkids < 2 || !is_atom(f->kids[0])) continue;
        if (f->kids[0]->tok.kw != LOW_KW_USE || !is_atom(f->kids[1])) continue;
        proven_u8str_view_t m = f->kids[1]->tok.lex, bind = m;
        for (proven_size_t j = 2; j + 1 < f->nkids; j++)
            if (is_atom(f->kids[j]) && veq(f->kids[j]->tok.lex, "as") && is_atom(f->kids[j + 1])) {
                bind = f->kids[j + 1]->tok.lex; break;
            }
        mod.usebind[mod.nusebind] = bind; mod.usemod[mod.nusebind] = m; mod.nusebind++;
    }

    // ★★★ **구성 손잡이를 읽는다** (RFC-0036 D5) — `build option <n> …`
    g_nopt = 0;
    for (proven_size_t i = 0; i < pr->nforms; i++) {
        const low_cst_t *bf = pr->forms[i];
        if (bf->kind != LOW_CST_FORM || bf->nkids < 4 || !is_atom(bf->kids[0])) continue;
        if (!veq(bf->kids[0]->tok.lex, "build") || !is_atom(bf->kids[1])) continue;
        if (!veq(bf->kids[1]->tok.lex, "option") || !is_atom(bf->kids[2])) continue;
        if (g_nopt >= IR_MAXOPT) break;
        ir_opt_t *o = &g_opt[g_nopt];
        memset(o, 0, sizeof(*o));
        proven_u8str_view_t nm = bf->kids[2]->tok.lex;
        proven_size_t cn = nm.size < sizeof o->name - 1 ? nm.size : sizeof o->name - 1;
        memcpy(o->name, nm.ptr, cn); o->name[cn] = 0;
        o->line = bf->line;
        proven_u8str_view_t ty = is_atom(bf->kids[3]) ? bf->kids[3]->tok.lex
                                                      : proven_u8str_view_from_cstr("");
        o->kind = veq(ty, "bool") ? 0 : veq(ty, "int") ? 1 : veq(ty, "choice") ? 2 : -1;
        if (o->kind < 0) {
            ir_fail(&mod, "E-OPT-TYPE", "a build option is `bool`, `int` or `choice` — an option "
                    "with no type is a knob whose positions nobody can name", bf->line);
            continue;
        }
        for (proven_size_t j = 4; j < bf->nkids; j++) {
            if (!is_atom(bf->kids[j])) continue;
            proven_u8str_view_t w = bf->kids[j]->tok.lex;
            if (veq(w, "default") && j + 1 < bf->nkids && is_atom(bf->kids[j + 1])) {
                if (!ir_opt_num(bf->kids[j + 1]->tok.lex, &o->val))
                    ir_fail(&mod, "E-OPT-TYPE", "this option's default is not a value it can take",
                            bf->line);
                j++;
            } else if (veq(w, "depends") && j + 1 < bf->nkids && is_atom(bf->kids[j + 1])) {
                proven_u8str_view_t d = bf->kids[j + 1]->tok.lex;
                proven_size_t dn = d.size < sizeof o->dep - 1 ? d.size : sizeof o->dep - 1;
                memcpy(o->dep, d.ptr, dn); o->dep[dn] = 0;
                o->has_dep = true;
                j++;
            } else if (o->kind == 2 && o->nch < 8) {
                proven_i64 cv;
                if (ir_opt_num(w, &cv)) o->ch[o->nch++] = cv;
            }
        }
        // ★ choice 의 기본값은 **고를 수 있는 값 중 하나**여야 한다. 아니면 그 손잡이는
        //   **아무도 못 돌리는 위치**에서 시작한다.
        if (o->kind == 2) {
            bool ok2 = false;
            for (proven_size_t k = 0; k < o->nch; k++) if (o->ch[k] == o->val) ok2 = true;
            if (!ok2)
                ir_fail(&mod, "E-OPT-TYPE", "this `choice` option's default is not one of its "
                        "choices — the knob starts at a position it does not have", bf->line);
        }
        g_nopt++;
    }
    // ★★ 해결본(`lowent.config`)을 얹는다. **손잡이에 없는 이름**을 고르는 것은 오류다 —
    //   조용히 무시하면 그 설정 파일은 **아무 일도 안 하면서 무언가 하는 척한다.**
    for (proven_size_t i = 0; i < g_ncfg; i++) {
        ir_opt_t *o = ir_opt_find(proven_u8str_view_from_cstr(g_cfg[i].name));
        if (!o) {
            ir_fail(&mod, "E-CONFIG-UNDEF", "the config selects an option that is not declared. "
                    "A setting nobody reads changes nothing — and it LOOKS like it does", 0);
            continue;
        }
        proven_i64 v;
        if (!ir_opt_num(proven_u8str_view_from_cstr(g_cfg[i].val), &v)) {
            ir_fail(&mod, "E-CONFIG-TYPE", "the config gives this option a value it cannot take", o->line);
            continue;
        }
        if (o->kind == 0 && v != 0 && v != 1) {
            ir_fail(&mod, "E-CONFIG-TYPE", "a `bool` option is on or off", o->line);
            continue;
        }
        if (o->kind == 2) {
            bool ok2 = false;
            for (proven_size_t k = 0; k < o->nch; k++) if (o->ch[k] == v) ok2 = true;
            if (!ok2) { ir_fail(&mod, "E-CONFIG-TYPE",
                                "the config picks a value this `choice` option does not offer", o->line);
                        continue; }
        }
        o->val = v;
        o->from_cfg = true;
    }
    // ★★★ **의존은 강제된다.** `hz depends smp` 인데 smp 가 꺼진 채 hz 를 켜면, 그 빌드는
    //   **존재하지 않는 구성**이다. Kconfig 가 하는 일이 정확히 이것이고, 하지 않으면
    //   그 의존 선언은 **장식**이다(검사되지 않는 중복은 거짓말로 썩는다).
    // ★★ **`choice` 손잡이에는 「꺼짐」이 없다** (결함 노트 #18, 2026-09-16). 전에는 `val == 0`
    //   하나로 「켜져 있는가」를 재서, `hz choice 100 250 . depends smp .` 처럼 0 을 고를 수 없는
    //   손잡이가 **늘 켜진 것**이 됐다 — 그래서 `smp` 를 끄는 구성이 하나도 존재할 수 없었다
    //   (`hz` 를 적든 안 적든 `E-CONFIG-DEPENDS`). 아무 구성으로도 끌 수 없는 손잡이는 손잡이가
    //   아니다. ⇒ bool 은 그대로 강제하고(켠 채로 바탕이 꺼지면 거절), `choice`·`int` 는
    //   「의존이 안 맞으면 이 손잡이는 **쓰이지 않는다**」로 읽는다. 해결본이 굳이 값을 고르면
    //   그 값이 **버려진다는 것**을 알린다 — 거절이 아니라 알림이다(RFC-0115 §8-21 은 이것을
    //   기록으로 남긴다: 정본이 `choice` 의 의존을 적지 않았다).
    for (proven_size_t i = 0; i < g_nopt; i++) {
        if (!g_opt[i].has_dep) continue;
        ir_opt_t *d = ir_opt_find(proven_u8str_view_from_cstr(g_opt[i].dep));
        if (!d) {
            ir_fail(&mod, "E-OPT-DEPENDS", "this option depends on an option that does not exist",
                    g_opt[i].line);
            continue;
        }
        if (d->val != 0) continue;                       // 바탕이 켜져 있다 — 볼 것이 없다
        if (g_opt[i].kind == 0) {                        // bool: 켠 채로 바탕이 꺼졌으면 없는 빌드다
            if (g_opt[i].val != 0)
                ir_fail(&mod, "E-CONFIG-DEPENDS", "this option is ON while the option it depends on is "
                        "OFF. That build does not exist — and without this check the `depends` clause "
                        "is decoration", g_opt[i].line);
            continue;
        }
        if (g_opt[i].from_cfg)                           // choice/int: 고른 값이 버려진다
            ir_warn_at(&mod, "W-CONFIG-DEPENDS",
                       "the config picks a value for this option while the option it DEPENDS ON is off. "
                       "A `choice` knob has no `off` position, so this is not an impossible build — but "
                       "the knob is out of play, and any code that still reads it is reading a setting "
                       "for a feature that is turned off. Guard that code with the option this one "
                       "depends on, drop this line from the config, or drop the `depends`",
                       g_opt[i].line, NULL);
    }

    proven_result_mem_mut_t sm = work.alloc_fn(work.ctx, 32 * sizeof(low_ir_struct_t), alignof(low_ir_struct_t));
    if (sm.err != PROVEN_OK) { ir.ok = false; return ir; }
    ir.structs = (low_ir_struct_t *)(void *)sm.value.ptr;
    memset(ir.structs, 0, 32 * sizeof(low_ir_struct_t));

    // ★★ pass 0a — **actor 의 상태를 struct 로 등록한다.**
    //   actor = **레코드 + 그 레코드를 소유한 op 들**이다. 그러면 make/field/fstore 를 그대로
    //   쓰고, VM 도 네이티브 백엔드도 **공짜로 따라온다**. 발명하지 않는다.
    for (proven_size_t i = 0; i < pr->nforms && ir.nstructs < 32; i++) {
        const low_cst_t *f = pr->forms[i];
        if (f->kind != LOW_CST_FORM || f->nkids < 3 || !is_atom(f->kids[0])) continue;
        if (f->kids[0]->tok.kw != LOW_KW_ACTOR || !is_atom(f->kids[1])) continue;
        const low_cst_t *blk = f->kids[f->nkids - 1];
        if (blk->kind != LOW_CST_BLOCK) continue;
        low_ir_struct_t *s = &ir.structs[ir.nstructs];
        s->name = f->kids[1]->tok.lex; s->viewable = true; s->align = 1;
        s->is_actor_state = true;   // ★ 이 struct 는 actor 의 state 다 (mailbox/failure 유효 자리)
        for (proven_size_t j = 0; j < blk->nkids; j++) {
            const low_cst_t *st = blk->kids[j];
            if (st->kind != LOW_CST_FORM || st->nkids < 2 || !is_atom(st->kids[0])) continue;
            if (st->kids[0]->tok.kw != LOW_KW_STATE) continue;
            const low_cst_t *sb = st->kids[st->nkids - 1];
            if (sb->kind != LOW_CST_BLOCK) continue;
            for (proven_size_t q = 0; q < sb->nkids && s->nf < IR_MAKE_MAXF; q++) {
                const low_cst_t *fld = sb->kids[q];
                if (fld->kind != LOW_CST_FORM || fld->nkids < 2 ||
                    !is_atom(fld->kids[0]) || !is_atom(fld->kids[1])) continue;
                proven_u8 sz = ir_field_size(fld->kids[1]->tok.lex);
                bool boxed_ = ir_field_tywords(fld) > 1;   // ★ 매개변수 타입 — 태그 값 한 칸
                proven_u8 capk_ = (fld->nkids >= 3 && is_atom(fld->kids[1]) && is_atom(fld->kids[2]) &&
                                   veq(fld->kids[1]->tok.lex, "cap"))
                                  ? (veq(fld->kids[2]->tok.lex, "allocator") ? 1 :
                                     veq(fld->kids[2]->tok.lex, "heap") ? 2 : 0) : 0;
                // ★ 권한 칸은 실행 중 뜻이 없다 — 한 바이트 스칼라 자리로 둔다. «낱말 둘짜리 타입 = 태그 값 한 칸» 으로
                //   두면 그 actor 가 빠른 경로에 못 실려(check-tagged 1101 → 1265, 실측) 얼로케이터 op 전부가 느려진다.
                if (capk_) { boxed_ = false; sz = 1; }
                if (boxed_) sz = 0;
                if (!sz) { s->viewable = false; sz = 8; }
                s->f[s->nf].boxed = boxed_;
                s->f[s->nf].capkind = capk_;
                s->f[s->nf].name = fld->kids[0]->tok.lex;
                s->f[s->nf].size = sz;
                // ★★★ **액터 상태 필드의 `sidx` 가 안 세워지고 있었다** — 0 으로 남았고,
                //   0 은 *"중첩 구조체 0번"* 을 뜻한다. 스칼라 필드가 **구조체라고 말하고**
                //   있었던 것이다. 뷰로 읽히면 정수 대신 **안쪽 뷰**가 나온다 —
                //   지금은 액터가 REC 로만 다뤄져서 **조용히 잠복해 있었다.**
                //   (타입 하강이 그것을 밟고서야 드러났다: 액터 핸들러가 **하나도** 안 내려갔다.)
                s->f[s->nf].sidx = -1;
                // ★ 슬라이스 필드면 **원소 폭**을 적어 둔다(RFC-0106 단계 0). 타입 낱말이
                //   `… slice <T> …` 꼴이면 그 `<T>` 의 크기다. 못 읽으면 0(= 모른다).
                s->f[s->nf].elem = 0;
                if (boxed_) {
                    proven_size_t tw = ir_field_tywords(fld);
                    for (proven_size_t z = 1; z + 1 <= tw; z++) {
                        const low_cst_t *w = fld->kids[1 + z - 1];
                        if (!is_atom(w) || !veq(w->tok.lex, "slice")) continue;
                        if (1 + z < fld->nkids && is_atom(fld->kids[1 + z])) {
                            proven_u8str_view_t et_ = fld->kids[1 + z]->tok.lex;
                            s->f[s->nf].elem = ir_field_size(et_);
                            // ★ 원소의 부동·부호도 적는다(2026-10-04) — 빠른 길이 이 칸의 슬라이스를 읽을 때 부호를 늘리거나 부동으로 본다.
                            if (s->f[s->nf].elem)
                                s->f[s->nf].slmeta = (proven_i64)s->f[s->nf].elem | (ir_is_float_ty(et_) ? IR_FLT_BIT : 0) |
                                                     (et_.size && et_.ptr[0] == (proven_u8)'i' ? IR_SGN_BIT : 0);
                        }
                        break;
                    }
                }
                s->f[s->nf].flt = ir_is_float_ty(fld->kids[1]->tok.lex);
                s->f[s->nf].off = s->total;
                s->total = (proven_u16)(s->total + sz);
                s->nf++;
            }
        }
        // ★★★ **actor 정책 절** — `mailbox …` · `failure restart …` (RFC-0009 §6.5 문법).
        //   전엔 이 절들이 **검증 없이** 통과했다: `mailbox bounded 0` 도, `mailbox 플럽 9` 도
        //   똑같이 "파싱만 됨" 경고 하나였다 — 뜻 없는 절은 곧 거짓말이다(PRINCIPLES.md §0).
        //   이제 **형태를 강제한다**(틀리면 거절)하고 정책을 **저장한다**. 저장된 capacity 는 런타임에서
        //   **강제된다** — 재진입 in-flight 도, async 대기열(`spawn send`/`drain`)도 바운드한다(VM·네이티브).
        //   아직 없는 건 회복 가능한 backpressure(`try send`)와 진짜 동시 배달뿐. 여기서 하는 건
        //   **절을 real 로 만드는 것**(파싱 시 형태 확정 + 저장)이다.
        #define ACTOR_ERR(codestr, msgstr, ln) do {                                            \
            low_diag_t d_ = { .sev = LOW_SEV_ERROR, .code = (codestr), .msg = (msgstr),        \
                              .line = (proven_u32)(ln), .col = 0 };                            \
            (void)proven_array_push(&ir.diags, &d_); ir.ok = false; } while (0)
        for (proven_size_t j = 0; j < blk->nkids; j++) {
            const low_cst_t *cl = blk->kids[j];
            if (cl->kind != LOW_CST_FORM || !cl->nkids || !is_atom(cl->kids[0])) continue;
            proven_u8str_view_t hh = cl->kids[0]->tok.lex;
            proven_u32 ln = cl->kids[0]->tok.line;
            if (veq(hh, "mailbox")) {
                if (s->mailbox_kind) { ACTOR_ERR("E-ACTOR-MAILBOX",
                    "an actor declares `mailbox` twice — one mailbox policy per actor", ln); continue; }
                if (cl->nkids < 2 || !is_atom(cl->kids[1])) { ACTOR_ERR("E-ACTOR-MAILBOX",
                    "`mailbox` needs a policy: `mailbox bounded <N>` or `mailbox unbounded`", ln); continue; }
                proven_u8str_view_t sub = cl->kids[1]->tok.lex;
                if (veq(sub, "bounded")) {
                    proven_i64 cap;
                    if (cl->nkids < 3 || !is_atom(cl->kids[2]) || !ir_int_lit(cl->kids[2]->tok.lex, &cap)
                        || cap <= 0) {
                        ACTOR_ERR("E-ACTOR-MAILBOX",
                            "`mailbox bounded` needs a POSITIVE integer capacity — a mailbox of 0 "
                            "can hold nothing, so the actor could never receive a message", ln);
                        continue;
                    }
                    s->mailbox_kind = 1; s->mailbox_cap = cap;
                } else if (veq(sub, "unbounded")) {
                    // ★★★ **`unbounded` 는 지금 거절한다** (2026-07-17, 사용자 결정 B) — 구현 현실에
                    //   정직하려면. async 큐는 실제로 **고정 풀**(lw_ambox[4096])이라 진짜로 자라지
                    //   못하고, 차면 트랩한다. 그러니 "unbounded" 는 반쯤 거짓이다 — 무한이라 말하며
                    //   4096 에서 캡·트랩. RFC-0009 D5 는 unbounded ⇒ alloc 효과(cost-visible) + (RFC-0043
                    //   D1) allocator capability 를 요구하는데, 큐가 진짜로 자라지 않으니 그 capability 는
                    //   **안 쓰는 권한을 요구하는 또 다른 반쪽 거짓**이 된다. ⇒ **닿을 수 없는 약속은
                    //   거절한다**(`bounded 0` 을 거절한 그 규율). 진짜 성장(allocator 스레딩)이 오면 그때 연다.
                    ACTOR_ERR("E-ACTOR-MAILBOX",
                        "`mailbox unbounded` is not accepted yet — and refusing it is the honest thing. "
                        "The async queue is a FIXED pool under the hood, so it cannot actually grow "
                        "without bound: it would cap and trap, which is not `unbounded` at all. RFC-0009 "
                        "says an unbounded mailbox must expose an `alloc` effect and (RFC-0043) carry an "
                        "allocator capability — but a queue that never really allocates would be demanding "
                        "a permission it never uses. Use `mailbox bounded N` (fully enforced, with `try "
                        "spawn send` for recoverable backpressure) until real, allocator-backed growth lands", ln);
                } else {
                    ACTOR_ERR("E-ACTOR-MAILBOX",
                        "unknown `mailbox` policy — it must be `bounded <N>` (`unbounded` is recognized "
                        "but not accepted yet — the queue is a fixed pool and cannot truly grow)", ln);
                }
            } else if (veq(hh, "failure")) {
                if (s->failure_policy) { ACTOR_ERR("E-ACTOR-FAILURE",
                    "an actor declares `failure` twice — one restart policy per actor", ln); continue; }
                if (cl->nkids < 3 || !is_atom(cl->kids[1]) || !veq(cl->kids[1]->tok.lex, "restart")
                    || !is_atom(cl->kids[2])) {
                    ACTOR_ERR("E-ACTOR-FAILURE",
                        "`failure` needs a restart policy: `failure restart max <N> [within <D> <unit>]`, "
                        "`failure restart never`, or `failure restart always`", ln);
                    continue;
                }
                proven_u8str_view_t pol = cl->kids[2]->tok.lex;
                if (veq(pol, "max")) {
                    proven_i64 mx;
                    if (cl->nkids < 4 || !is_atom(cl->kids[3]) || !ir_int_lit(cl->kids[3]->tok.lex, &mx)
                        || mx <= 0) {
                        ACTOR_ERR("E-ACTOR-FAILURE",
                            "`failure restart max` needs a POSITIVE restart count", ln);
                        continue;
                    }
                    s->failure_policy = 1; s->failure_max = mx;
                    // ★ 선택적 `within <D> <unit>` — 값은 초로 싣되, 결정적 클럭이 없어 **창(window) 의
                    //   강제는 아직**이다(그 사실은 체커가 W-NOT-YET 로 정직하게 말한다).
                    if (cl->nkids == 4) {
                        /* `max N` 만 — 창 없음, 정상 */
                    } else if (cl->nkids == 7 && is_atom(cl->kids[4]) && veq(cl->kids[4]->tok.lex, "within")
                               && is_atom(cl->kids[5]) && is_atom(cl->kids[6])) {
                        proven_i64 dur;
                        if (!ir_int_lit(cl->kids[5]->tok.lex, &dur) || dur <= 0) {
                            ACTOR_ERR("E-ACTOR-FAILURE",
                                "`within` needs a positive duration, e.g. `within 60 seconds`", ln);
                            continue;
                        }
                        proven_u8str_view_t unit = cl->kids[6]->tok.lex;
                        proven_i64 mul = veq(unit, "second") || veq(unit, "seconds") ? 1
                                       : veq(unit, "minute") || veq(unit, "minutes") ? 60
                                       : veq(unit, "hour")   || veq(unit, "hours")   ? 3600 : -1;
                        if (mul < 0) {
                            ACTOR_ERR("E-ACTOR-FAILURE",
                                "unknown `within` time unit — use seconds, minutes, or hours", ln);
                            continue;
                        }
                        s->failure_within = dur * mul;
                    } else {
                        ACTOR_ERR("E-ACTOR-FAILURE",
                            "malformed `failure restart max` — expected `max <N>` "
                            "optionally followed by `within <D> <unit>`", ln);
                    }
                } else if (veq(pol, "never")) {
                    s->failure_policy = 2;
                } else if (veq(pol, "always")) {
                    s->failure_policy = 3;
                } else {
                    ACTOR_ERR("E-ACTOR-FAILURE",
                        "unknown restart policy — use `max <N>`, `never`, or `always`", ln);
                }
            }
        }
        #undef ACTOR_ERR
        if (s->nf) ir.nstructs++;
    }

    // ★★★★★ **`segments T` — 조각 여럿을 한 값으로** (RFC-0104 §8-8, 소유자 서명 2026-08-29).
    //
    //   왜 언어가 아는가: 오늘 `slice (slice u8)` 은 **선언만 되고 원소를 못 꺼낸다**
    //   (`E-TYPE-LET`, 실측). 그래서 조각 여럿을 값으로 못 들고, FFI 로는 조각마다 한 번씩만
    //   나갈 수 있다 — `writev` 1 회 12 ms 대 `write` 64 회 339 ms, **28 배**다.
    //
    //   ★ 그런데 **새 옵코드는 하나도 안 만든다**: 뷰는 *백업 슬라이스* 와 *(at, n) 서술자
    //     슬라이스* 두 칸이고, 그 둘을 드는 그릇은 이미 있다(슬라이스 필드를 든 구조체 —
    //     `pool.block_pool` 이 그 증인이다). 그래서 여기서 **그 구조체를 컴파일러가 등록**하고,
    //     세 op 은 `make`·`field`·`len`·`index`·`subslice` 로 **탈설탕**된다.
    //   ☞ 그러므로 이 기능이 IR·VM·C 백엔드에 더한 것은 **0** 이다. 표면만 늘었다.
    //   ★ 그리고 **쓰는 단위에만** 등록한다. 처음엔 늘 등록했더니 `segments` 를 한 번도
    //     안 쓰는 프로그램의 구조체 수가 하나씩 늘어 단위 시험 둘이 붉어졌다
    //     (`ir.nstructs == 1` 을 기대하던 자리). ☞ *안 쓰는 프로그램에는 있지도 않아야 한다.*
    if (ir.nstructs < 32 && ir_unit_mentions(pr, "segments")) {
        low_ir_struct_t *sg = &ir.structs[ir.nstructs];
        *sg = (low_ir_struct_t){ 0 };
        sg->name = proven_u8str_view_from_cstr("segments");
        sg->f[0].name = proven_u8str_view_from_cstr("back");
        sg->f[0].size = 0; sg->f[0].boxed = true; sg->f[0].sidx = -1;
        sg->f[1].name = proven_u8str_view_from_cstr("descs");
        sg->f[1].size = 0; sg->f[1].boxed = true; sg->f[1].sidx = -1;
        sg->nf = 2;
        sg->viewable = false;     // 슬라이스를 드는 그릇은 바이트 배치가 없다
        sg->total = 0;
        ir.nstructs++;
    }

    // ★★ **임의의 배열 크기가 조용히 자른다** — 이 세션에 같은 병이 세 번 났다
    //   (prng[8] · 구조체 필드 f[8] · 그리고 여기: enum 변형 64 · op 256 · 별칭 32 · 오류 이름 64).
    //   자르고 나면 진단은 **"없는 이름"** 이라고 한다 — *네 프로그램이 틀렸다.* 두 겹의 거짓말이다.
    //   ⇒ **넘치면 거절한다.** 잘라 놓고 아무 말 안 하는 것보다 낫다.
    #define IR_LIMIT(cond, what)                                                           \
        do { if (cond) {                                                                   \
            low_diag_t d_ = { .sev = LOW_SEV_ERROR, .code = "E-IR-LIMIT",                  \
                              .msg = "this compilation unit has more " what " than the "   \
                                     "tool can carry — they used to be dropped SILENTLY, " \
                                     "and then the diagnostic said the NAME did not exist "\
                                     "(blaming your program for the tool's limit). "       \
                                     "Refusing is the honest answer",                      \
                              .line = f->line, .col = 0 };                                 \
            (void)proven_array_push(&ir.diags, &d_); ir.ok = false;                        \
        } } while (0)

    // ★★★ pass 0a: **모듈 수준 `let` = 이름 붙은 상수** (2026-07-14).
    //   `let NAME be [T] <comptime>` — 초기값은 **컴파일타임에 알려져야** 한다.
    //   ★ 모듈 수준 `var` 는 **아직 아니다**: RFC-0029 는 그것을 **가변 전역**으로 정의하고
    //     `state` 효과·escape-safe·concurrency-gated 를 요구한다. 그것을 강제할 준비가 되기
    //     전까지 **거절한다** — 조용히 상수처럼 굴게 두면 그것이 곧 거짓말이다.
    mod.nmconst = 0;
    for (proven_size_t i = 0; i < pr->nforms; i++) {
        const low_cst_t *f = pr->forms[i];
        if (f->kind != LOW_CST_FORM || f->nkids < 3 || !is_atom(f->kids[0])) continue;
        low_kw_t kw0 = f->kids[0]->tok.kw;
        if (kw0 == LOW_KW_VAR) {
            low_diag_t vd = { .sev = LOW_SEV_ERROR, .code = "E-IR-UNSUP",
                              .msg = "a module-level `var` is a MUTABLE GLOBAL. RFC-0029 allows it "
                                     "— but only with the harms removed: escape-safe (it may hold "
                                     "only owned/values, never a region reference), a visible "
                                     "`state` effect that PROPAGATES to every caller, and a "
                                     "concurrency gate. None of that is enforced yet, so the tool "
                                     "REFUSES rather than let it quietly behave like a constant. "
                                     "For a named constant, write `let`",
                              .line = f->line, .col = 0 };
            (void)proven_array_push(&ir.diags, &vd);
            ir.ok = false;
            continue;
        }
        if (kw0 != LOW_KW_LET || !is_atom(f->kids[1])) continue;
        proven_size_t be = f->nkids;
        for (proven_size_t j = 2; j < f->nkids; j++)
            if (is_atom(f->kids[j]) && f->kids[j]->tok.kw == LOW_KW_BE) { be = j; break; }
        if (be + 1 >= f->nkids || !is_atom(f->kids[be + 1])) {
            low_diag_t cd = { .sev = LOW_SEV_ERROR, .code = "E-CONST-NOTCOMPTIME",
                              .msg = "a module-level `let` is a NAMED CONSTANT — its value must be "
                                     "known at COMPILE TIME (a literal). A module constant that had "
                                     "to be computed at run time would need a place to run, and an "
                                     "order to run in, and then it would not be a constant",
                              .line = f->line, .col = 0 };
            (void)proven_array_push(&ir.diags, &cd);
            ir.ok = false;
            continue;
        }
        proven_i64 cv;
        if (!ir_int_lit(f->kids[be + 1]->tok.lex, &cv) || be + 2 != f->nkids) {
            low_diag_t cd = { .sev = LOW_SEV_ERROR, .code = "E-CONST-NOTCOMPTIME",
                              .msg = "a module-level `let` is a NAMED CONSTANT — its value must be "
                                     "a compile-time literal (only integer literals for now; the "
                                     "tool SAYS so rather than pretending)",
                              .line = f->line, .col = 0 };
            (void)proven_array_push(&ir.diags, &cd);
            ir.ok = false;
            continue;
        }
        if (mod.nmconst < IR_MAXALIAS) {
            mod.mconst[mod.nmconst] = f->kids[1]->tok.lex;
            mod.mconstv[mod.nmconst] = cv;
            mod.mconstt[mod.nmconst] = ity_of_decl_c(&mod, f, 2, be);
            mod.nmconst++;
        } else {
            IR_LIMIT(true, "module constants");
        }
    }

    // pass 0: enums (variant names) + slice-like type aliases + struct layouts
    for (proven_size_t i = 0; i < pr->nforms; i++) {
        const low_cst_t *f = pr->forms[i];
        if (f->kind != LOW_CST_FORM || f->nkids < 2 || !is_atom(f->kids[0])) continue;
        low_kw_t kw = f->kids[0]->tok.kw;
        if (kw == LOW_KW_STRUCT && is_atom(f->kids[1]) &&
            f->kids[f->nkids - 1]->kind == LOW_CST_BLOCK && ir.nstructs < 32) {
            low_ir_struct_t *s = &ir.structs[ir.nstructs];
            s->name = f->kids[1]->tok.lex;
            const low_cst_t *blk = f->kids[f->nkids - 1];
            for (proven_size_t j = 0; j < blk->nkids; j++) {
                const low_cst_t *fld = blk->kids[j];
                if (fld->kind != LOW_CST_FORM || fld->nkids < 2 || !is_atom(fld->kids[0])) continue;
                // ★ `satisfies <trait> .` 는 **필드가 아니라 주장**이다(RFC-0026). 배치에 안 들어간다.
                if (fld->kids[0]->tok.kw == LOW_KW_SATISFIES) continue;
                // ★★★ `mmio <base> .` — **레지스터 블록의 기저 주소**(RFC-0042 D2). 의사 필드다.
                if (veq(fld->kids[0]->tok.lex, "mmio")) {
                    proven_i64 base = 0;
                    if (is_atom(fld->kids[1]) && ir_int_lit(fld->kids[1]->tok.lex, &base)) {
                        s->is_mmio = true; s->mmio_base = base;
                    } else {
                        low_diag_t md = { .sev = LOW_SEV_ERROR, .code = "E-MMIO-BASE",
                                          .msg = "`mmio <base> .` needs an integer base address — "
                                                 "the register block is a MAP of the device, and a "
                                                 "map with no origin maps nothing",
                                          .line = fld->line, .col = 0 };
                        (void)proven_array_push(&ir.diags, &md);
                    }
                    continue;
                }
                // ★★★★ `storage reserved .` — **저장소를 링커가 준다**(RFC-0039 §9-2 갈래①). `mmio <base>` 의
                //   형제다: 저쪽은 주소를 **적고**, 이쪽은 주소를 **받는다**. 의사 필드이므로
                //   낱말로 매칭한다 — 키워드 수(43)는 안 늘어난다.
                if (veq(fld->kids[0]->tok.lex, "storage")) {
                    if (is_atom(fld->kids[1]) && veq(fld->kids[1]->tok.lex, "reserved")) s->is_reserve = true;
                    continue;
                }
                if (veq(fld->kids[0]->tok.lex, "layout")) {   // `layout packed .` pseudo-field
                    if (is_atom(fld->kids[1]) && veq(fld->kids[1]->tok.lex, "packed")) s->packed = true;
                    continue;
                }
                if (veq(fld->kids[0]->tok.lex, "align")) {    // `align n .` — RFC-0051 §5.1 contract
                    proven_i64 n = 0;
                    bool from_tgt = false;
                    // ★★★ **정렬은 타깃에게 물어볼 수 있다** (RFC-0104 §8-7, 소유자 결정 7-B).
                    //   `align machine.cache_line .` — 캐시라인 격리를 **이식 가능하게** 적는 길이다.
                    //   그 수는 comptime 상수이므로 여기서 **접힌다**(런타임 조회가 아니다).
                    //   ☞ `comptime` 을 앞에 적어도 같다 — 접기는 이미 그 낱말을 안다.
                    {
                        const low_cst_t *av = fld->kids[1];
                        if (is_atom(av) && veq(av->tok.lex, "comptime") && fld->nkids > 2) av = fld->kids[2];
                        if (is_atom(av)) {
                            bool found = false;
                            proven_i64 mv = ir_machine(av->tok.lex, &found);
                            if (found) { n = mv; from_tgt = true; }
                        }
                    }
                    if (from_tgt && n >= 1 && n <= 4096 && (n & (n - 1)) == 0) {
                        s->align = (proven_u16)n;
                        s->align_from_target = true;
                    } else if (is_atom(fld->kids[1]) && ir_int_lit(fld->kids[1]->tok.lex, &n) &&
                        n >= 1 && n <= 4096 && (n & (n - 1)) == 0) {
                        s->align = (proven_u16)n;
                    } else {
                        low_diag_t d = { .sev = LOW_SEV_ERROR, .code = "E-IR-UNSUP",
                                         .msg = "align needs a power-of-two byte count",
                                         .line = fld->line, .col = 0 };
                        (void)proven_array_push(&ir.diags, &d);
                        ir.ok = false;
                    }
                    continue;
                }
                if (s->nf >= IR_MAKE_MAXF) {
                    low_diag_t d = { .sev = LOW_SEV_ERROR, .code = "E-IR-LIMIT",
                                     .msg = "this struct has more fields than the tool can carry "
                                            "(16) — the extra fields used to be DROPPED SILENTLY: "
                                            "the layout came out short and `encode` wrote too few "
                                            "bytes. Refusing is the honest answer",
                                     .line = fld->line, .col = 0 };
                    (void)proven_array_push(&ir.diags, &d);
                    ir.ok = false;
                    continue;
                }
                s->f[s->nf].name = fld->kids[0]->tok.lex;
                // ★ 타입이 몇 낱말인가 — 매개변수를 받는 머리면 표지 전까지 전부 타입이다.
                proven_size_t tw_ = ir_field_tywords(fld);
                bool param_ty_ = tw_ > 1;
                s->f[s->nf].size = is_atom(fld->kids[1]) ? ir_field_size(fld->kids[1]->tok.lex) : 0;
                // ★★ 매개변수 타입 필드는 **바이트 레이아웃이 없다** — 태그 값 한 칸이다.
                //   그래서 `viewable` 이 false 가 되고(아래 자동 계산), view/encode 는 이 구조체를
                //   거절한다. **없는 레이아웃을 있는 척하지 않는다.**
                if (param_ty_) { s->f[s->nf].size = 8; s->f[s->nf].boxed = true; s->viewable = false; }
                else s->f[s->nf].boxed = false;
                // ★ 슬라이스 필드면 **원소 폭**을 적어 둔다(RFC-0106 단계 0). 타입 낱말이
                //   `… slice <T> …` 꼴이면 그 `<T>` 의 크기다. 0 = 슬라이스가 아니거나 모른다.
                //   ☞ 아무 동작도 안 바꾼다 — **나르기만** 한다. `boxed` 는 *"바이트 표현이
                //     없다"* 만 말했고 **무엇의 슬라이스인지**는 안 말했다. 표현을 옮기려면
                //     스트라이드를 알아야 한다.
                s->f[s->nf].elem = 0;
                s->f[s->nf].slmeta = 0;
                if (param_ty_)
                    for (proven_size_t z = 1; z < tw_; z++) {
                        const low_cst_t *w_ = fld->kids[z];
                        if (!is_atom(w_) || !veq(w_->tok.lex, "slice")) continue;
                        if (z + 1 < fld->nkids && is_atom(fld->kids[z + 1])) {
                            proven_u8str_view_t et_ = fld->kids[z + 1]->tok.lex;
                            s->f[s->nf].elem = ir_field_size(et_);
                            // ★ RFC-0135 D13 — 원소가 크기 있는 수·bool 이면 (주소, 길이) 로 바이트에 담을 수 있다
                            proven_u8 es_ = veq(et_, "bool") ? 1 : s->f[s->nf].elem;
                            if (es_) s->f[s->nf].slmeta = (proven_i64)es_ | (ir_is_float_ty(et_) ? IR_FLT_BIT : 0) |
                                                          (et_.size && et_.ptr[0] == (proven_u8)'i' ? IR_SGN_BIT : 0);
                        }
                        break;
                    }
                s->f[s->nf].sidx = -1;
                s->f[s->nf].arrn = 0; s->f[s->nf].arresz = 0; s->f[s->nf].arrmeta = 0;
                if (param_ty_ && is_atom(fld->kids[1]) && veq(fld->kids[1]->tok.lex, "array") && fld->nkids >= 4 &&
                    is_atom(fld->kids[2]) && is_atom(fld->kids[3])) {             // ★ T2b-3b 배열 칸
                    proven_i64 an = 0; proven_u8 ae = ir_field_size(fld->kids[2]->tok.lex);
                    if (ae && ir_int_lit(fld->kids[3]->tok.lex, &an) && an > 0) {
                        bool aflt = ir_is_float_ty(fld->kids[2]->tok.lex), asgn = fld->kids[2]->tok.lex.size && fld->kids[2]->tok.lex.ptr[0] == (proven_u8)'i';
                        s->f[s->nf].arrn = (proven_u32)an; s->f[s->nf].arresz = ae;
                        s->f[s->nf].arrmeta = (proven_i64)ae | (aflt ? IR_FLT_BIT : 0) | (asgn ? IR_SGN_BIT : 0);
                        s->f[s->nf].elem = ae;
                    }
                }
                s->f[s->nf].tyname = is_atom(fld->kids[1]) ? fld->kids[1]->tok.lex : (proven_u8str_view_t){0};
                // ★ **중첩 구조체** — 필드의 타입이 이미 선언된 구조체면 그 레이아웃을 쓴다.
                //   (앞에서 선언된 것만 — 순환 구조체는 크기가 없다.)
                if (!param_ty_ && !s->f[s->nf].size && is_atom(fld->kids[1])) {
                    bool nf2; proven_size_t ni = ir_struct_find(&ir, fld->kids[1]->tok.lex, &nf2);
                    if (nf2 && ir.structs[ni].viewable && ir.structs[ni].total &&
                        ir.structs[ni].total <= 255) {
                        s->f[s->nf].size = (proven_u8)ir.structs[ni].total;
                        s->f[s->nf].sidx = (proven_i16)ni;
                    }
                }
                s->f[s->nf].flt = is_atom(fld->kids[1]) && ir_is_float_ty(fld->kids[1]->tok.lex);
                // ★★ 엔디안 표기 — `big` / `little` (2026-07-13 개명).
                //   그전엔 `be` / `le` 였고, **둘 다 충돌했다**:
                //     `be` = **바인딩 표시자**(`var x be u8 5 .`)와 같은 철자. 위치로만 갈렸다.
                //     `le` = **≤ 비교 연산자**(`requires le a b`)와 같은 철자.
                //   그리고 `le` 는 **아무도 안 읽었다** — `magic u32 le .` 과 `magic u32 zzz .` 가
                //   구별되지 않았다. 리틀엔디안이 **기본값**이라 "동작하는 것처럼" 보였을 뿐이다.
                //   **틀린 이유로 맞는 것**은 맞는 게 아니다(PRINCIPLES.md §0).
                //   이제 `little` 은 **진짜로 읽히고**, 모르는 필드 표기는 **거절한다.**
                s->f[s->nf].be = false;
                s->f[s->nf].perm = FP_RW;
                // ★ 아핀 필드 표식 — 타입 낱말 중 `owned` 가 있으면 이 필드는 소유다(RFC-0044 §9.1).
                s->f[s->nf].owned = false;
                for (proven_size_t y = 1; y < 1 + (tw_ ? tw_ : 1) && y < fld->nkids; y++)
                    if (is_atom(fld->kids[y]) && veq(fld->kids[y]->tok.lex, "owned"))
                        s->f[s->nf].owned = true;
                for (proven_size_t x = 1 + (tw_ ? tw_ : 1); x < fld->nkids; x++) {
                    if (!is_atom(fld->kids[x])) continue;
                    proven_u8str_view_t w = fld->kids[x]->tok.lex;
                    if      (veq(w, "big"))    s->f[s->nf].be = true;
                    else if (veq(w, "little")) s->f[s->nf].be = false;
                    // ★★★ **레지스터 권한** (RFC-0042 D3) — 표지 자리를 그대로 쓴다. 새 키워드 0.
                    else if (veq(w, "rw"))     s->f[s->nf].perm = FP_RW;
                    else if (veq(w, "ro"))     s->f[s->nf].perm = FP_RO;
                    else if (veq(w, "wo"))     s->f[s->nf].perm = FP_WO;
                    else if (fld->kids[x]->tok.line > fld->kids[0]->tok.line && is_atom(fld->kids[x - 1])) {
                        // ★ X-0059 — «모르는 표지» 가 **다음 줄**에 있으면 대개 표지가 아니라 다음 칸이다: 윗칸의 점이
                        //   빠져 두 칸이 한 폼이 됐다(`x u64` ⏎ `y u64 .`). 개행은 닫지 않으므로 뜻은 그대로 거절이고,
                        //   진단만 실제 원인을 말한다. 자리는 윗칸 마지막 낱말 바로 뒤 — 점이 들어갈 곳.
                        const low_token_t *lt = &fld->kids[x - 1]->tok;
                        low_diag_t fd = { .sev = LOW_SEV_ERROR, .code = "E-DOT-MISSING",
                                          .msg = "this struct field is not closed — each field ends with its own `.` "
                                                 "(`x u64 .` then `y u64 .`). A newline closes nothing, so the next "
                                                 "field was read as a marker of this one",
                                          .line = lt->line, .col = lt->col + (proven_u32)lt->lex.size };
                        (void)proven_array_push(&ir.diags, &fd);
                        ir.ok = false;
                        break;
                    }
                    else {
                        low_diag_t fd = { .sev = LOW_SEV_ERROR, .code = "E-FIELD-MARK",
                                          .msg = "unknown marker on a struct field — the field markers "
                                                 "are `big`/`little` (byte order) and `rw`/`ro`/`wo` "
                                                 "(register access). Byte order used to be spelled `be`/`le`, which "
                                                 "COLLIDED with "
                                                 "the binding marker `be` and with the `le` (≤) "
                                                 "operator — and `le` was never actually READ: it was "
                                                 "indistinguishable from a typo, and only 'worked' "
                                                 "because little-endian is the default. Being right "
                                                 "for the wrong reason is not being right",
                                          .line = fld->kids[x]->tok.line, .col = 0 };
                        (void)proven_array_push(&ir.diags, &fd);
                        ir.ok = false;
                    }
                }
                s->nf++;
            }
            ir_struct_layout(&ir, s);
            // ★★★ **디바이스 레지스터는 한 번에, 폭이 맞게 닿아야 한다** (RFC-0042 D1 · §8-2).
            //   보통 뷰의 필드는 바이트 루프로 읽고 쓴다 — 버퍼에는 그게 옳고 엔디언에 이식성도
            //   있다. 그런데 **레지스터에는 틀리다**: u32 레지스터에 바이트를 네 번 대는 것은 한 번
            //   대는 것과 **다른 일**이고(부분 쓰기가 래치를 반만 흔든다), 정렬이 어긋난 접근은
            //   버스가 아예 거절한다. 그래서 뒤끝이 **폭이 맞는 volatile 접근 하나**로 내리는데,
            //   그러려면 **그렇게 낼 수 있는 배치여야 한다.** 낼 수 없는 배치를 조용히 바이트로
            //   내리면 그것이 바로 이 RFC 가 막으려던 결함이다 ⇒ **선언 자리에서 거절한다.**
            if (s->is_mmio) {
                for (proven_size_t z = 0; z < s->nf; z++) {
                    proven_u8 fz = s->f[z].size;
                    bool width_ok = (fz == 1 || fz == 2 || fz == 4 || fz == 8);
                    if (s->f[z].sidx >= 0 || s->f[z].boxed || !width_ok ||
                        s->f[z].be || (s->f[z].off % fz) != 0) {
                        low_diag_t md = { .sev = LOW_SEV_ERROR, .code = "E-MMIO-FIELD",
                                          .msg = "a register in an `mmio` block must be reachable in "
                                                 "ONE properly-sized access: width 1/2/4/8, naturally "
                                                 "aligned within the block, native byte order, not a "
                                                 "nested struct. This one is not, so the backend could "
                                                 "only reach it byte by byte — and four byte writes to "
                                                 "a 32-bit register are a DIFFERENT EVENT on the bus "
                                                 "than one word write (a half-shaken latch), which is "
                                                 "the exact failure this RFC exists to prevent. "
                                                 "`big`/`little` do not apply: a device register has "
                                                 "the machine's byte order, not the wire's (RFC-0042 D1)",
                                          .line = f->kids[1]->tok.line, .col = 0 };
                        (void)proven_array_push(&ir.diags, &md);
                        ir.ok = false;
                    }
                }
            }
            // ★★★★★ **타깃마다 달라지는 정렬은 공개 ABI 로 못 나간다** (RFC-0104 §8-7,
            //   소유자 결정 2026-08-28). `align machine.cache_line .` 로 정렬한 구조체는
            //   **타깃마다 레이아웃이 다르다**. 그것이 `export` 로 밖에 나가면 — 헤더를 받아
            //   가는 C 쪽은 그 사실을 모른 채 **한 레이아웃을 가정한다**. 조용히 어긋난다.
            //   ⇒ 내부 배치에는 마음껏 쓰고, **경계에서는 리터럴만** 쓴다.
            //   ☞ 헤더와 바이너리가 같은 타깃에서 나왔다는 것을 지금 아무도 안 지킨다 —
            //     그 보장을 게이트로 세우면 그때 이 금지를 다시 볼 수 있다.
            if (s->align_from_target && f->is_export) {
                low_diag_t d = { .sev = LOW_SEV_ERROR, .code = "E-ABI-TARGET-ALIGN",
                                 .msg = "an EXPORTED type cannot take its alignment from the target "
                                        "(`align machine.cache_line`) — its layout would differ per "
                                        "target while the header a C caller reads assumes ONE layout, "
                                        "so the two drift silently. Use a literal `align N .` on the "
                                        "boundary type, or keep the target-aligned type INTERNAL and "
                                        "convert at the edge (RFC-0104 §8-7)",
                                 .line = f->line, .col = 0 };
                (void)proven_array_push(&ir.diags, &d);
                ir.ok = false;
            }
            ir.nstructs++;
        }
        if (kw == LOW_KW_ENUM && f->kids[f->nkids - 1]->kind == LOW_CST_BLOCK) {
            const low_cst_t *blk = f->kids[f->nkids - 1];
            // ★ enum 타입 이름 = `enum <name>` 의 name (kids[1]); 페이로드 필드가 자기 타입을
            //   값으로 담으면 무한 크기라 거절한다(RFC-0080 §4.2, 재귀는 owned/인덱스 경유).
            proven_u8str_view_t ename = (f->nkids >= 2 && is_atom(f->kids[1]))
                                            ? f->kids[1]->tok.lex : (proven_u8str_view_t){0};
            for (proven_size_t j = 0; j < blk->nkids; j++)
                if (blk->kids[j]->kind == LOW_CST_FORM && blk->kids[j]->nkids >= 1 &&
                    is_atom(blk->kids[j]->kids[0])) {
                    const low_cst_t *vf = blk->kids[j];
                    IR_LIMIT(mod.nenumv >= IR_MAXENUMV, "enum variants");
                    if (mod.nenumv >= IR_MAXENUMV) continue;
                    proven_size_t vi = mod.nenumv++;
                    mod.enumv[vi] = vf->kids[0]->tok.lex;
                    mod.enum_owner[vi] = ename;
                    // 나머지 kids = (필드이름, 필드타입) 쌍. 홀수/초과는 방어적으로 자른다.
                    // ★ 페이로드 타입은 **한 낱말**이어야 한다(스칼라나 인덱스 이름). 변형 한 줄에는
                    //   필드 구분자가 없어서(`add l u32 r u32 .`) `owned tree`·`slice u8` 같은 **여러
                    //   낱말 타입**은 짝-파서가 조용히 오해석했다(→ E-ENUM-ARITY 오진 또는 낱말 유실).
                    //   그런 머리를 만나면 **정확한 유예 진단**을 낸다: owned 박스 재귀(RFC-0080 §4.6
                    //   증분 4)는 변형 단위 부분이동/drop(RFC-0044 §9)이 걸린 후속이고, `slice`/`ref`
                    //   같은 여러 낱말 타입은 아직 페이로드로 못 쓴다 — 재귀는 **인덱스**(`u32`)로 끊어라.
                    proven_u8 np = 0;
                    for (proven_size_t t = 1; t + 1 < vf->nkids && np < IR_MAXENUMPF; t += 2) {
                        if (!is_atom(vf->kids[t]) || !is_atom(vf->kids[t + 1])) break;
                        proven_u8str_view_t fty = vf->kids[t + 1]->tok.lex;
                        if (veq(fty, "owned") || veq(fty, "ref") || veq(fty, "mut_ref") ||
                            veq(fty, "mut") || veq(fty, "slice") || veq(fty, "vec") ||
                            veq(fty, "mask") || veq(fty, "option") || veq(fty, "result") ||
                            veq(fty, "map")) {
                            ir_diag(&mod, "E-ENUM-PAYLOAD",
                                    "an enum-variant payload type must be a SINGLE word today (a scalar "
                                    "like `u32`, or an index into a node arena). A multi-word type "
                                    "(`owned <t>`, `slice <t>`, `ref <t>`, `option <t>`, ...) cannot be "
                                    "written here: the single-line variant grammar has no per-field "
                                    "delimiter, so its words cannot be told apart. `owned`-box recursion "
                                    "(RFC-0080 §4.6 increment 4) is deferred — it needs variant-scoped "
                                    "partial-move/drop (RFC-0044 §9). Break recursion with an INDEX (`u32`) "
                                    "into a `slice <node>` arena instead (RFC-0080 §5)",
                                    vf->line);
                            ir.ok = false;
                            break;
                        }
                        // ★ 직접 자기포함(값) = 무한 크기 (RFC-0080 §4.2). owned/인덱스로 끊으라.
                        if (ename.size && proven_u8str_view_eq(fty, ename)) {
                            ir_diag(&mod, "E-ENUM-INFINITE",
                                    "an enum variant cannot embed its OWN type BY VALUE — the size "
                                    "would be infinite. Recursion must go through indirection: an "
                                    "index into a node arena (e.g. `u32`) or an `owned` box (RFC-0080 §4.2)",
                                    vf->line);
                            ir.ok = false;
                        }
                        mod.enum_pname[vi][np] = vf->kids[t]->tok.lex;
                        mod.enum_pty[vi][np]   = fty;
                        np++;
                    }
                    mod.enum_np[vi] = np;
                }
            // ★★★ **페이로드 enum 을 struct 로도 등록** (RFC-0080 아레나) — 슬라이스 원소가
            //   되려면 레이아웃이 필요하다. fat-struct: `$t`(태그 u32) + 변형들의 유니크 페이로드
            //   필드. 그래야 `slice node` 가 struct-array 로 잡혀 aggregate-store·뷰-읽기가 돈다.
            //   페이로드 없는 enum(전부 np==0)은 등록 안 함 — 변형=정수 인덱스로 기존 불변.
            {
                bool has_payload = false;
                for (proven_size_t vi = 0; vi < mod.nenumv; vi++)
                    if (proven_u8str_view_eq(mod.enum_owner[vi], ename) && mod.enum_np[vi] > 0) { has_payload = true; break; }
                if (has_payload && ename.size && ir.nstructs < 32) {
                    low_ir_struct_t *s = &ir.structs[ir.nstructs];
                    memset(s, 0, sizeof *s);
                    s->name = ename; s->viewable = true; s->align = 1;
                    static const proven_byte_t ENUM_TAGF[2] = { '$', 't' };
                    s->f[s->nf].name = (proven_u8str_view_t){ ENUM_TAGF, 2 };
                    s->f[s->nf].size = 4; s->f[s->nf].sidx = -1; s->f[s->nf].off = s->total;
                    s->total = (proven_u16)(s->total + 4); s->nf++;
                    for (proven_size_t vi = 0; vi < mod.nenumv; vi++) {
                        if (!proven_u8str_view_eq(mod.enum_owner[vi], ename)) continue;
                        for (proven_u8 fp = 0; fp < mod.enum_np[vi] && s->nf < IR_MAKE_MAXF; fp++) {
                            bool seen = false;   // 같은 이름 = 같은 슬롯(변형 간 공유). 슬롯을 겹치지 않고 이어 붙이는 fat-struct.
                            for (proven_size_t z = 0; z < s->nf; z++)
                                if (proven_u8str_view_eq(s->f[z].name, mod.enum_pname[vi][fp])) { seen = true; break; }
                            if (seen) continue;
                            proven_u8 sz = ir_field_size(mod.enum_pty[vi][fp]);
                            if (!sz) { s->viewable = false; sz = 8; }
                            s->f[s->nf].name = mod.enum_pname[vi][fp];
                            s->f[s->nf].size = sz; s->f[s->nf].sidx = -1;
                            s->f[s->nf].flt = ir_is_float_ty(mod.enum_pty[vi][fp]);
                            s->f[s->nf].off = s->total;
                            s->total = (proven_u16)(s->total + sz); s->nf++;
                        }
                    }
                    ir.nstructs++;
                }
            }
        } else if ((kw == LOW_KW_TYPE || kw == LOW_KW_NEWTYPE) && f->nkids >= 3 && is_atom(f->kids[1]) && is_atom(f->kids[2])) {
            // ★ `is` 를 없앴다 ⇒ 대상은 kids[2] 다 (`type <name> <type> .`).
            //   ★ newtype 도 여기서 별칭으로 등록한다 — **런타임 표현은 base 와 동일**(명목 구별은
            //     타입검사에서만). 그래야 파라미터·지역·필드·cast 가 base 레이아웃/검사를 얻는다.
            if (ir_type_is_slice(&mod, f->kids[2]->tok.lex)) {
                IR_LIMIT(mod.nalias >= IR_MAXALIAS, "type aliases");
                if (mod.nalias < IR_MAXALIAS) {
                    mod.alias_ebits[mod.nalias] = 255;
                    if (f->nkids >= 4 && is_atom(f->kids[3])) {
                        proven_u8str_view_t e = f->kids[3]->tok.lex;
                        mod.alias_ebits[mod.nalias] = veq(e, "u8") ? 8 : veq(e, "u16") ? 16
                                                    : veq(e, "u32") ? 32 : veq(e, "u64") ? 64 : 255;
                    }
                    mod.slice_alias[mod.nalias++] = f->kids[1]->tok.lex;
                }
            } else {
                // ★★ **스칼라 별칭도 별칭이다.** 이걸 안 해서 `type h u8 .` 뒤의 파라미터가
                //   **타입 미상**이 됐고, 미상이면 검사가 하나도 안 붙어서 `output u8` 인 op 이
                //   **400 을 반환했다.** `type h range 0 10 .` 은 계약이 통째로 증발했다.
                ityp_t at = ity_of_decl_c(&mod, f, 2, f->nkids);
                if (at.known) {
                    IR_LIMIT(mod.nty_alias >= IR_MAXALIAS, "type aliases");
                    if (mod.nty_alias < IR_MAXALIAS) {
                        mod.ty_alias[mod.nty_alias] = f->kids[1]->tok.lex;
                        mod.ty_alias_ty[mod.nty_alias] = at;
                        mod.nty_alias++;
                    }
                    // ★★★ **생 포인터 newtype 이면 그 이름을 따로 든다** (RFC-0068 S4 · C1) —
                    //   `newtype cstr unsafe_ptr u8` 의 코어 낱말이 `unsafe_ptr` 다. 표현은 u64(위)지만
                    //   extern 출력이 이 이름이면 프로토타입 반환은 `const char *` 여야 한다.
                    if (is_atom(f->kids[2]) && veq(f->kids[2]->tok.lex, "unsafe_ptr")
                        && mod.nptr_alias < IR_MAXALIAS)
                        mod.ptr_alias[mod.nptr_alias++] = f->kids[1]->tok.lex;
                }
            }
        }
    }

    // ★★★ **같은 이름에 번호가 둘이었다** — 그리고 그것이 **조용히 틀린 답**을 냈다.
    //
    //   오류 값은 **오류-이름 표**의 인덱스이고(IRW_WRAP_ERR / error_value),
    //   enum 변형 리터럴은 **변형 표**의 인덱스다(RFC: enum 값 = 변형 인덱스, 런타임 태그 없음).
    //   두 표는 **순서가 다르다.** 그래서:
    //
    //       if eq (error_value r) delta . do … end       ← r 은 진짜로 err delta 인데
    //
    //   **거짓**이 나왔다. 프로그램이 자기 오류를 자기 이름과 비교하는데 **틀린 답**을 받는다.
    //   진단도 없이. 이것이 가장 나쁜 종류다 — 컴파일되고, 실행되고, **틀린다.**
    //
    //   ⇒ 두 표를 **하나의 번호로** 묶는다: 변형 이름들을 **변형 표와 같은 순서로** 오류 표에
    //     미리 심는다. 그러면 모든 enum 변형에서 `err-index == variant-index` 다.
    //     (enum 없이 쓰는 자유 오류 이름은 그 뒤에 붙는다 — 언어가 그 형태도 허용한다.)
    for (proven_size_t i = 0; i < mod.nenumv && ir.nerrs < IR_MAXERRS; i++) {
        bool have = false;
        for (proven_size_t q = 0; q < ir.nerrs; q++)
            if (proven_u8str_view_eq(ir.errs[q], mod.enumv[i])) { have = true; break; }
        if (!have) ir.errs[ir.nerrs++] = mod.enumv[i];
    }

    // ★★ **투명 별칭**(SPEC-004 §89): `type X is Y` 면 **X 와 Y 는 같은 타입이다.**
    //   그런데 IR 은 구조체 별칭을 전혀 몰랐다:
    //     type p2 is pt .   →   `view s as p2` = "선언된 구조체가 아니다"
    //                           `input q p2`   = 구조체 파라미터로 안 잡힌다
    //   명세가 "같은 타입" 이라고 한 것을 도구가 **다른 타입으로 취급**했다.
    //   같은 레이아웃을 그 이름으로도 등록한다. (별칭의 별칭까지 — 그래서 고정점까지 돈다.)
    for (int round = 0; round < 4; round++) {
        bool grew = false;
        for (proven_size_t i = 0; i < pr->nforms && ir.nstructs < 32; i++) {
            const low_cst_t *f = pr->forms[i];
            if (f->kind != LOW_CST_FORM || f->nkids < 3 || !is_atom(f->kids[0]) ||
                f->kids[0]->tok.kw != LOW_KW_TYPE || !is_atom(f->kids[1]) || !is_atom(f->kids[2]))
                continue;
            bool have; (void)ir_struct_find(&ir, f->kids[1]->tok.lex, &have);
            if (have) continue;                                  // 이미 등록됨
            bool src; proven_size_t si = ir_struct_find(&ir, f->kids[2]->tok.lex, &src);   // ★ `is` 제거: 대상은 kids[2]
            if (!src) continue;                                  // 구조체 별칭이 아니다
            ir.structs[ir.nstructs] = ir.structs[si];            // **같은 레이아웃**
            ir.structs[ir.nstructs].name = f->kids[1]->tok.lex;  // …새 이름으로
            ir.nstructs++;
            grew = true;
        }
        if (!grew) break;
    }

    // ★★ op 같은 form 의 **평평한 목록**. actor 의 `on` 핸들러는 actor 블록 **안**에 있으므로
    //   최상위만 훑으면 **통째로 사라진다** — 그것이 바로 이 버그였다(IR 0 defs).
    //   여기서 한 번 모으고, pass1/pass2 는 이 목록을 돈다. `sidx` = 그 핸들러가 속한 actor
    //   상태 레코드의 인덱스(핸들러의 유일한 파라미터가 그 인스턴스다).
    // ★ `owner` = 이 form 이 놓인 **모듈**(RFC-0060 재개). 여러 파일이 한 단위로 링크되므로
    //   `module X .` 이 여러 번 나오고, 그 사이의 선언이 X 의 것이다. 선형 훑기로 충분하다.
    struct { const low_cst_t *f; int sidx; proven_u8str_view_t owner; } opf[IR_MAXOPS];
    proven_size_t nops = 0;
    proven_u8str_view_t cur_mod = { 0 };
    for (proven_size_t i = 0; i < pr->nforms; i++) {
        const low_cst_t *f = pr->forms[i];
        if (f->kind != LOW_CST_FORM || f->nkids < 2 || !is_atom(f->kids[0])) continue;
        low_kw_t kw = f->kids[0]->tok.kw;
        if (kw == LOW_KW_MODULE && is_atom(f->kids[1])) { cur_mod = f->kids[1]->tok.lex; continue; }
        if (nops >= IR_MAXOPS) { IR_LIMIT(true, "ops"); break; }
        if (kw == LOW_KW_FN || kw == LOW_KW_PROC || kw == LOW_KW_TEST) {
            // ★★★ **제네릭 틀은 낮추지 않는다** (RFC-0021). 단형화가 호출 자리를 전부 인스턴스
            //   (`op#T`·`op#4`)로 바꾸므로 틀 자신은 **부를 수 없다**. 그런데 틀의 몸은 comptime
            //   파라미터를 참조하고 그 파라미터는 지워지므로, 낮추면 **미정의 이름**이 된다.
            //   ★ 타입 파라미터만 있을 땐 이 병이 안 보였다 — 몸이 `t` 를 **값으로 안 썼기** 때문이다.
            //     값 comptime 파라미터를 접기 시작하니 비로소 드러났다(`mul x k` 의 `k`).
            if (low_is_generic_template(f)) continue;
            opf[nops].f = f; opf[nops].sidx = -1; opf[nops].owner = cur_mod; nops++;
            continue;
        }
        if (kw != LOW_KW_ACTOR || !is_atom(f->kids[1])) continue;
        bool afound; proven_size_t as = ir_struct_find(&ir, f->kids[1]->tok.lex, &afound);
        if (!afound) continue;
        const low_cst_t *blk = f->kids[f->nkids - 1];
        if (blk->kind != LOW_CST_BLOCK) continue;
        for (proven_size_t j = 0; j < blk->nkids; j++) {
            if (nops >= IR_MAXOPS) { IR_LIMIT(true, "ops"); break; }
            const low_cst_t *h = blk->kids[j];
            if (h->kind != LOW_CST_FORM || h->nkids < 2 || !is_atom(h->kids[0])) continue;
            // ★ 액터 블록 **안에 있다는 것**이 곧 "핸들러다" 이다 — `on` 은 필요 없었다.
            low_kw_t hk = h->kids[0]->tok.kw;
            if (hk != LOW_KW_FN && hk != LOW_KW_PROC) continue;
            opf[nops].f = h; opf[nops].sidx = (int)as; opf[nops].owner = cur_mod; nops++;
        }
    }
    proven_result_mem_mut_t dm = work.alloc_fn(work.ctx, (nops ? nops : 1) * sizeof(low_ir_def_t), alignof(low_ir_def_t));
    if (dm.err != PROVEN_OK) { ir.ok = false; return ir; }
    ir.defs = (low_ir_def_t *)(void *)dm.value.ptr;
    memset(ir.defs, 0, (nops ? nops : 1) * sizeof(low_ir_def_t));

    for (proven_size_t i = 0; i < nops; i++) {
        const low_cst_t *f = opf[i].f;
        low_kw_t kw = f->kids[0]->tok.kw;
        low_ir_def_t *d = &ir.defs[ir.ndefs++];
        d->name = f->kids[1]->tok.lex;
        d->owner_mod = opf[i].owner;        // ★ 출처를 싣는다 (RFC-0060 재개 · 단계 U 슬라이스 ①)
        d->is_calc = (kw != LOW_KW_PROC);
        d->is_test = (kw == LOW_KW_TEST);   // ★ 파라미터도 계약도 없다 — 몸통뿐이다.
        d->isr_vector = -1; d->isr_priority = -1;
        // ★★★ **`vector N .` / `priority P .`** (RFC-0042 D5) — 이 절이 op 을 ISR 로 만든다.
        //   여기서 IR 로 옮겨야 뒤끝이 **벡터 테이블**을 낼 수 있다(§8-4).
        for (proven_size_t j = 2; j + 1 < f->nkids; j++) {
            if (!is_atom(f->kids[j]) || !is_atom(f->kids[j + 1])) continue;
            proven_i64 nv;
            if (veq(f->kids[j]->tok.lex, "vector") &&
                ir_int_lit(f->kids[j + 1]->tok.lex, &nv) && nv >= 0 && nv < 496)
                d->isr_vector = (proven_i32)nv;
            else if (veq(f->kids[j]->tok.lex, "priority") &&
                     ir_int_lit(f->kids[j + 1]->tok.lex, &nv) && nv >= 0 && nv < 256)
                d->isr_priority = (proven_i32)nv;
        }
        // ★★★ R2 — `parallel <s> split .` / `reduce <acc> <op> .`
        for (proven_size_t j = 2; j + 1 < f->nkids; j++) {
            if (!is_atom(f->kids[j]) || !is_atom(f->kids[j + 1])) continue;
            if (veq(f->kids[j]->tok.lex, "parallel")) {
                // ★ 그 이름이 **몇 번째 파라미터인가** — 헤더에게 묻는다(DECISION-0015).
                //   전엔 여기도 `kids[q+1]` 을 이름으로 잡았다 — `input comptime s slice u8 .`
                //   이면 이름이 **`comptime`** 이 되어 **parallel 이 아무것도 못 찾는다.**
                low_op_header_t ph = low_op_header(f);
                for (proven_size_t q = 0; q < ph.np; q++)
                    if (proven_u8str_view_eq(ph.p[q].name, f->kids[j + 1]->tok.lex)) {
                        d->is_par = true; d->par_param = (proven_u8)q;
                    }
            } else if (veq(f->kids[j]->tok.lex, "reduce") && j + 2 < f->nkids &&
                       is_atom(f->kids[j + 2])) {
                proven_u8str_view_t ro = f->kids[j + 2]->tok.lex;
                d->red_op = veq(ro, "add") ? 1 : veq(ro, "mul") ? 2
                          : veq(ro, "min") ? 3 : veq(ro, "max") ? 4 : 0;
            } else if (veq(f->kids[j]->tok.lex, "schedule") &&
                       veq(f->kids[j + 1]->tok.lex, "explore_interleavings")) {
                // ★★★ RFC-0009 D6 — 이 test 를 interleaving 오라클에 태운다(K-정준 순서).
                d->sched_explore = true;
                if (j + 3 < f->nkids && is_atom(f->kids[j + 2])
                    && veq(f->kids[j + 2]->tok.lex, "limit") && is_atom(f->kids[j + 3])) {
                    proven_i64 lim;
                    if (ir_int_lit(f->kids[j + 3]->tok.lex, &lim) && lim > 0 && lim < 65536)
                        d->sched_limit = (proven_u16)lim;   // A 는 참고만 — B 가 N-열거로 쓴다
                }
            }
        }
        if (opf[i].sidx >= 0) {
            // ★ actor 핸들러 — **슬롯 0 은 그 인스턴스**(상태 레코드)다.
            //   `send inc to c` 는 그래서 그냥 **호출**이다: inc(c).
            //   ★★ 여기 `continue;` 가 하나 있었고, 그것이 **세 가지를 동시에 망가뜨렸다**:
            //     ① 핸들러의 `input` 절이 **통째로 버려졌다** — 그런데 --check 는 **초록불**이었고,
            //        실행하면 "산술에는 정수가 필요하다"(E-VM-TYPE)고 **산술을 탓했다.**
            //        진실은 *"도구가 네 파라미터를 버려서 n 이 수가 아니다"* 였다(교훈 5: 오진).
            //     ② `ir_iface_hash` 를 건너뛰어 액터 핸들러의 **인터페이스 해시가 0** 이었다 —
            //        내용 주소화가 액터에서 통째로 비어 있었다(SPEC-011).
            //     ③ 그래서 메시지가 **데이터를 실어 나를 수 없었다.**
            //   이제 인자는 **슬롯 1 부터**다. 메시지는 곧 op 호출이다(DECISION-0011).
            d->is_actor = true;
            d->param_struct |= 1u;
            d->param_sidx[0] = (proven_u8)opf[i].sidx;
            d->nparams = 1;
        }
        // ★★★ **op 의 머리는 한 번만 읽는다** (DECISION-0015). 전엔 이 자리가 자기만의 스캐너로
        //   `input` 절을 자르고 `mut` 을 건너뛰었다 — 그리고 **여섯 소비자가 각자** 그랬다.
        //   (low_typecheck 는 이름 한 낱말만 건너뛰어 `comptime`/`mut` 을 **타입에 섞었고**,
        //    low_contract 는 `comptime` 을 **파라미터 이름**으로 잡았다.)
        {
            low_op_header_t h = low_op_header(f);
            d->is_variadic = h.is_variadic;   // ★ 가변인자 C 함수 (RFC-0063 §5)
            if (h.too_many) {
                low_diag_t hd = { .sev = LOW_SEV_ERROR, .code = "E-IR-ARITY",
                                  .msg = "an op declares more parameters than the tool can carry a "
                                         "header for — refusing is the honest answer",
                                  .line = f->line, .col = 0 };
                (void)proven_array_push(&ir.diags, &hd);
                ir.ok = false;
            }
            for (proven_size_t q = 0; q < h.np && q < LOW_MAX_PARAMS; q++) {   // ★ T2b-2: 쓸 수 있는 매개변수
                bool mw = h.p[q].is_mut || h.p[q].is_owned;
                for (proven_size_t z = h.p[q].ts; z < h.p[q].te && !mw; z++)
                    if (is_atom(f->kids[z]) && veq(f->kids[z]->tok.lex, "mut_ref")) mw = true;
                if (mw) d->param_mutw |= 1u << (d->nparams + q);
            }
            for (proven_size_t q = 0; q < h.np; q++) {
                proven_size_t tw = h.p[q].core;   // ★ 타입의 **알맹이** — `mut`/`owned` 도 벗긴 자리
                if (tw < h.p[q].te && is_atom(f->kids[tw])) {
                    proven_u8str_view_t t0 = f->kids[tw]->tok.lex;
                    if (ir_type_is_slice(&mod, t0) && d->nparams < LOW_MAX_PARAMS) {
                        d->param_slice |= 1u << d->nparams;
                        // ★ 원소 폭을 적어 둔다 — 백엔드가 **바이트 슬라이스인지** 물어야 한다.
                        // ★ 원소 낱말은 타입 구간의 **다음 원자**다. `te` 를 경계로 쓰면
                        //   `slice u32` 에서 `u32` 를 **못 본다**(구간이 `slice` 에서 끝난다).
                        if (d->nparams < LOW_MAX_PARAMS) {
                            proven_u8 eb = 255;
                            // ★★ **별칭이면 별칭의 폭을 쓴다** — `type graph slice u8 .` 처럼.
                            for (proven_size_t ai2 = 0; ai2 < mod.nalias; ai2++)
                                if (proven_u8str_view_eq(mod.slice_alias[ai2], t0))
                                    eb = mod.alias_ebits[ai2];
                            if (eb == 255 && tw + 1 < f->nkids && is_atom(f->kids[tw + 1])) {
                                proven_u8str_view_t e = f->kids[tw + 1]->tok.lex;
                                eb = veq(e, "u8") ? 8 : veq(e, "i8") ? 108
                                   : veq(e, "u16") ? 16 : veq(e, "u32") ? 32
                                   : veq(e, "u64") ? 64
                                   : veq(e, "i16") ? 116 : veq(e, "i32") ? 132   // 부호형=100+비트(i8=108)
                                   : veq(e, "i64") ? 164 : 255;
                            }
                            d->param_ebits[d->nparams] = eb;
                            // ★ 원소가 **구조체**면 그 인덱스를 든다(폭 대신).
                            if (eb == 255 && tw + 1 < f->nkids && is_atom(f->kids[tw + 1])) {
                                bool esf; proven_size_t esi =
                                    ir_struct_find(&ir, f->kids[tw + 1]->tok.lex, &esf);
                                if (esf && d->nparams < LOW_MAX_PARAMS)
                                    d->param_selem[d->nparams] = (proven_u8)(esi + 1);
                            }
                        }
                    }
                    if (veq(t0, "cap") && d->nparams < LOW_MAX_PARAMS)
                        d->param_cap |= 1u << d->nparams;   // ★ 능력 = 권리(값이 아니다)
                    if (veq(t0, "unsafe_fn") && d->nparams < LOW_MAX_PARAMS)
                        d->param_ufn |= 1u << d->nparams;   // ★ 콜백 파라미터 — 씨에 함수 포인터로
                    if (veq(t0, "bitset") && d->nparams < LOW_MAX_PARAMS)
                        d->param_bset |= 1u << d->nparams;  // ★ 비트셋 파라미터 (64비트 마스크)
                    if (ir_is_float_ty(t0) && d->nparams < LOW_MAX_PARAMS)
                        d->param_flt |= 1u << d->nparams;   // ★ f64 파라미터를 CLI 가 알아보게
                    if (veq(t0, "f32") && d->nparams < LOW_MAX_PARAMS)
                        d->param_f32 |= 1u << d->nparams;   // ★ 32 비트 — 경계에서 반올림한다 (#83)
                    if ((veq(t0, "u64") || veq(t0, "usize")) && d->nparams < LOW_MAX_PARAMS)
                        d->param_u64 |= 1u << d->nparams;   // ★ 위쪽 절반이 정당한 값이다 (#19·#69)
                    if (d->nparams < LOW_MAX_PARAMS)                    // ★ cstr 파라미터 → C 로 `const char *` (RFC-0068 S4)
                        for (proven_size_t pa = 0; pa < mod.nptr_alias; pa++)
                            if (proven_u8str_view_eq(mod.ptr_alias[pa], t0)) {
                                d->param_cstr |= 1u << d->nparams; break;
                            }
                    if ((veq(t0, "option") || veq(t0, "result")) && d->nparams < LOW_MAX_PARAMS)
                        d->param_opt |= 1u << d->nparams;   // ★ option·result = (태그,값) 쌍
                    if (veq(t0, "vec") && d->nparams < LOW_MAX_PARAMS && tw + 2 < f->nkids
                        && is_atom(f->kids[tw + 1]) && is_atom(f->kids[tw + 2])) {
                        // ★ 벡터 파라미터 `vec <t> <n>` — 원소 폭(param_ebits)과 레인 수를 기록한다.
                        proven_u8str_view_t et = f->kids[tw + 1]->tok.lex;
                        proven_u8 vez = ir_field_size(et);
                        proven_i64 vln = 0;
                        if (vez && ir_int_lit(f->kids[tw + 2]->tok.lex, &vln)
                            && vln >= 1 && vln <= 16 && d->nparams < LOW_MAX_PARAMS) {
                            d->param_vec |= 1u << d->nparams;
                            d->param_ebits[d->nparams] = vez;
                            d->param_vlanes[d->nparams] = (proven_u8)vln;
                        }
                    }
                    if (d->nparams < LOW_MAX_PARAMS) {
                        bool sf; proven_size_t si = ir_struct_find(&ir, t0, &sf);
                        if (sf && h.p[q].is_uptr) {          // ★ `unsafe_ptr <struct>` — C 로의 생 포인터
                            // viewable 구조체만: 바이트 레이아웃이 C 와 맞을 수 있어야 한다(모르면 거절).
                            if (!ir.structs[si].viewable) {
                                low_diag_t ud = { .sev = LOW_SEV_ERROR, .code = "E-UPTR-TYPE",
                                    .msg = "`unsafe_ptr` needs a struct whose fields are all sized "
                                           "scalars — a raw pointer to C only makes sense when the "
                                           "byte layout is fixed and can match C's",
                                    .line = f->line, .col = 0 };
                                (void)proven_array_push(&ir.diags, &ud); ir.ok = false;
                            }
                            // ★ 내부적으론 **바이트 슬라이스처럼** 저장한다(프롤로그·분석 재사용). C 로의
                            //   인터페이스만 다르다: `void *` 단일(길이 없음). 그 갈림은 param_uptr 로 한다.
                            d->param_uptr  |= 1u << d->nparams;
                            d->param_slice |= 1u << d->nparams;
                            d->param_ebits[d->nparams] = 8;      // 바이트
                            d->param_sidx[d->nparams]  = (proven_u8)si;
                        } else if (sf) {                     // ★ 구조체 파라미터(값)
                            // ★★★ **레지스터 블록은 값이 아니다** (RFC-0042 D3 · §8-4).
                            //   구조체 파라미터는 **값으로 넘어간다** ⇒ 뒤끝이 경계에서 뷰를
                            //   레코드로 **구체화**하고, 그러려면 **모든 필드를 읽는다**. 그 중에
                            //   `wo` 가 있으면 D3 가 컴파일 시간에 막겠다고 한 읽기가 **조용히
                            //   일어난다** — 그리고 읽는 시점은 프로그램이 적은 곳도 아니다.
                            //   ☞ 실측(2026-08-02): `fn peek input g gpio .` 가 `check: ok` 였고
                            //     방출된 C 가 moder(rw)와 **bsrr(wo)를 둘 다** 읽었다. 채택된
                            //     보증이 뒤끝에서 조용히 뚫려 있었다.
                            //   ⇒ 블록은 **뷰로만** 다룬다(`view T .` / `view T <슬라이스>`).
                            if (ir.structs[si].is_mmio) {
                                low_diag_t md = { .sev = LOW_SEV_ERROR, .code = "E-MMIO-BYVALUE",
                                    .msg = "an `mmio` register block cannot be a by-value parameter. "
                                           "A struct parameter is COPIED, and copying a device means "
                                           "reading EVERY register at once — including the `wo` ones, "
                                           "whose reads D3 promises to reject at compile time, at a "
                                           "moment the program never wrote. A register block is a "
                                           "DEVICE, not a value: pass the view instead (RFC-0042 D3 · §8-4)",
                                    .line = f->line, .col = 0 };
                                (void)proven_array_push(&ir.diags, &md); ir.ok = false;
                            }
                            d->param_struct |= 1u << d->nparams;
                            d->param_sidx[d->nparams] = (proven_u8)si;
                        } else if (h.p[q].is_uptr) {         // ★ unsafe_ptr 인데 구조체가 아니다
                            low_diag_t ud = { .sev = LOW_SEV_ERROR, .code = "E-UPTR-TYPE",
                                .msg = "`unsafe_ptr` must name a struct — it is a raw pointer to a "
                                       "struct's bytes for C, nothing else",
                                .line = f->line, .col = 0 };
                            (void)proven_array_push(&ir.diags, &ud); ir.ok = false;
                        }
                    }
                }
                d->nparams++;
            }
        }
        // ★ 한계를 넘으면 **거절한다.** 여기서 조용히 넘어가면, 그 파라미터의 `range` 는
        //   아무 데서도 강제되지 않는데 구간 분석은 그것을 믿는다 — 불건전하다.
        if (d->nparams > LOW_MAX_PARAMS) {
            low_diag_t pd = { .sev = LOW_SEV_ERROR, .code = "E-IR-ARITY",
                              .msg = "an op takes more parameters than the tool can carry contracts "
                                     "for (a parameter's declared range would be TRUSTED by the "
                                     "interval analysis but enforced by NO ONE — refusing is the "
                                     "honest answer)", .line = f->line, .col = 0 };
            (void)proven_array_push(&ir.diags, &pd);
            ir.ok = false;
        }
        // ★ 여기 `E-HDR-DIVERGE` 자기 대조가 있었다 — 헤더와 **IR 자신의 스캐너**가 같은 답을
        //   내는지 매 빌드에서 맞춰 봤다. 그 검사는 **제 할 일을 다 했다**: 초록인 채로 남아
        //   IR 의 스캐너를 헤더로 **안전하게 이행**할 수 있게 해 줬다. 이제 스캐너가 **하나뿐**이라
        //   자기를 자기와 비교하는 셈이 됐으므로 걷어낸다.
        //   ★ 표현을 통일하는 일의 순서는 이것이다: **먼저 대조를 켜고, 그다음 옮기고, 그다음
        //     대조를 끈다.** 대조 없이 옮기는 것은 통일이 아니라 **희망**이다.
        ir_iface_hash(work, f, d);
    }

    // pass 2: lower bodies
    proven_size_t di = 0;
    for (proven_size_t i = 0; i < nops; i++) {
        const low_cst_t *f = opf[i].f;
        low_ir_def_t *d = &ir.defs[di++];
        ir_ctx_t c = mod;   // inherit module tables; fresh per-def state
        c.nlocals = 0; c.nloops = 0; c.failed = false; c.ncint = 0;
        c.lbuf_off = 0; c.lit_frame = false;   // ★ T2b-2 — 틀 안 나열 자리는 op 마다 0 에서
        g_nrg = 0; g_nrel = 0; g_using_ov = (proven_size_t)-1; g_relsub_atom = nullptr;   // ★ 앞 op 이 영역 안에서 실패했어도 이 op 은 빈 영역 쌓기에서 시작한다
        if (opf[i].sidx >= 0) {          // ★ actor 핸들러: 슬롯 0 = 인스턴스, 상태 필드를 싣는다
            // ★ 전에는 여기서 슬롯 0 을 선언하고 **바로 다음 줄이 nlocals 를 0 으로 지웠다.**
            //   인자가 없을 때는 우연히 굴러갔다(슬롯 0 은 호출자가 넣어 주니까). 인자가 생기면
            //   **파라미터가 슬롯 0 에서 인스턴스와 충돌한다.** 이제 제대로 선언한다.
            const low_ir_struct_t *as = &ir.structs[opf[i].sidx];
            (void)ir_local_declare(&c, as->name, f->line);
            c.locals[0].ty = ITY_UNK;
            for (proven_size_t q = 0; q < as->nf && q < IR_MAKE_MAXF; q++) {
                c.sfcap[c.nsfield] = as->f[q].capkind;
                c.sfield[c.nsfield++] = as->f[q].name;
            }
        }
        c.tsp = 0; c.tstk_bad = false;   // ★ per-def: 타입 그림자 스택은 def 마다 새로 시작한다
        c.code = PROVEN_ARRAY_INIT(work, low_ir_ins_t, 32).value;
        // ★★★ **op 의 머리는 한 번만 읽는다** (DECISION-0015).
        //   전엔 이 자리가 이름을 `kids[j+1]` 로 잡고(→ `comptime` 이 이름이 된다), 절의 끝을
        //   **`input`/`output`/`effects` 세 낱말로만** 찾았다(→ `requires` 가 사이에 오면
        //   타입 범위가 그것까지 삼킨다). `low_is_clause_word()` 는 **열두 낱말**을 안다.
        {
            low_op_header_t hh = low_op_header(f);
            // ★ 출력 타입의 **이름** — `output rect .` 의 `rect`. 메서드 사슬이 이것을 쓴다.
            if (hh.out_s < hh.out_e && is_atom(f->kids[hh.out_s])) {
                d->out_tyname = f->kids[hh.out_s]->tok.lex;
                // ★★★ **출력이 생 포인터 newtype 이면 표시한다** (RFC-0068 S4 · C1) — extern 이 C 의
                //   char* 를 돌려줄 자리. cbe 가 프로토타입 반환을 `const char *` 로 낸다.
                for (proven_size_t pa = 0; pa < mod.nptr_alias; pa++)
                    if (proven_u8str_view_eq(mod.ptr_alias[pa], d->out_tyname)) { d->out_ptr = true; break; }
            }
            for (proven_size_t q = 0; q < hh.np; q++) {
                proven_size_t sl = ir_local_declare(&c, hh.p[q].name, f->line);
                c.locals[sl].ty = ity_of_decl_c(&c, f, hh.p[q].ts, hh.p[q].te);    // S2
                // ★★★ **원소의 타입도 싣는다** (2026-09-09, REQ-0013 · 결함 u64-index-comparison).
                //   `slice u64` 는 스칼라가 아니라 `ty` 가 미상이다 — 그런데 `index s i` 는
                //   **스칼라를 낸다**. 그 부호를 아무도 안 실어서 순서 비교가 부호 있는 비교로
                //   내려갔고, u64 의 큰 값이 음수처럼 다뤄졌다. 여기서 한 칸 더 읽으면 끝난다.
                c.locals[sl].elem = ITY_UNK;
                for (proven_size_t z = hh.p[q].ts; z + 1 < hh.p[q].te && z + 1 < f->nkids; z++) {
                    if (!is_atom(f->kids[z])) continue;
                    proven_u8str_view_t w0 = f->kids[z]->tok.lex;
                    if (veq(w0, "slice") || veq(w0, "vec") || veq(w0, "array")) {
                        c.locals[sl].elem = ity_of_decl_c(&c, f, z + 1, z + 2);
                        break;
                    }
                }
                // ★ 비트셋 파라미터의 **선언 폭** — 지역과 똑같이 실어야 원소 범위검사가 폭까지
                //   (안 실으면 파라미터 s 에 add/contains 가 64 로 후퇴 → 태그 경로와 어긋난다).
                c.bset_w = 0;
                ir_bset_context(&c, f, hh.p[q].ts, hh.p[q].te);
                c.locals[sl].bset_w = c.bset_w;
                c.bset_w = 0;
                // ★ 파라미터의 **선언 타입 이름** — 한정자(mut/owned)를 벗긴 알맹이 자리
                if (hh.p[q].core < f->nkids && is_atom(f->kids[hh.p[q].core]))
                    c.locals[sl].tyname = ir_strip_mod(&c, f->kids[hh.p[q].core]->tok.lex);
                if (sl < LOW_MAX_PARAMS) {                        // RFC-0055: 시그니처의 범위
                    ityp_t pt = c.locals[sl].ty;
                    d->prng[sl].has_rng = pt.has_rng;
                    d->prng[sl].rlo = pt.rlo; d->prng[sl].rhi = pt.rhi;
                }
            }
        }
        // ★★★★★ RFC-0120 — `absorbs machine <이름> .` 이 묶는 이름은 **그 몸 안의 지역**이다.
        //   권한은 실행 시 값을 나르지 않는다(정적 토큰) — 그래서 지역 하나면 족하고, 그 지역이
        //   어셈블리 잎에 넘길 `cap machine` 자리를 채운다. 밖에서는 아무도 그것을 넘기지 않는다:
        //   **그 op 이 그 권한을 보증하는 자리**이고, 장부가 그 사실을 센다(RFC-0120 §5.2).
        for (proven_size_t j2 = 2; j2 + 2 < f->nkids; j2++) {
            if (!is_atom(f->kids[j2]) || !veq(f->kids[j2]->tok.lex, "absorbs")) continue;
            if (!is_atom(f->kids[j2 + 1]) || !veq(f->kids[j2 + 1]->tok.lex, "machine")) continue;
            if (!is_atom(f->kids[j2 + 2])) continue;
            proven_size_t asl = ir_local_declare(&c, f->kids[j2 + 2]->tok.lex, f->line);
            c.locals[asl].ty = ITY_UNK;
            break;
        }
        // ★★ `slice T` (T ≠ u8) 파라미터를 **타입 있는 배열**로 감싼다.
        //   지금까지 이것을 안 해서 slice u32 가 **조용히 바이트로 취급**됐다:
        //     len(slice u32 of 4 elems) = 16   ← 바이트 수를 냈다 (4 여야 한다)
        //     index(s, 1)               = 0    ← 바이트 1번을 냈다 (원소 1 = 2 여야 한다)
        //   컴파일되고 실행되고 **틀린 값을 냈다.** 미구현보다 나쁘다.
        //   기계장치는 이미 있었다(IRW_VARRAY — view_array 가 쓰는 것). 진입에서 씌운다.
        low_op_header_t vh = low_op_header(f);          // ★ 헤더가 유일한 답이다
        for (proven_size_t q = 0; q < vh.np; q++) {
            bool eflt, esgn; int esidx = -1;
            proven_u8 esz = ir_slice_esz_ex(&ir, f, vh.p[q].ts, vh.p[q].te, &eflt, &esidx, &esgn);
            // ★ u8 바이트 슬라이스는 그대로 둔다 — 단, **부호형 i8** 은 감싼다: 런타임에
            //   부호를 실어야 `index` 가 부호 확장을 한다(안 그러면 0xFF 가 255 로 샌다).
            // ★★★★★ **조용히 건너뛰면 런타임에 터진다** (2026-08-27, WO-0127).
            //   `ir_slice_esz_ex` 는 원소 타입이 **뷰 불가 구조체**(참조·슬라이스·소유
            //   핸들 필드가 있어 바이트 배치가 없는 것)면 0 을 낸다. 그러면 여기서
            //   `continue` 로 넘어가 슬라이스가 **바이트 슬라이스인 채로** 남고,
            //   `set (index buf 0) (make box …)` 가 런타임에 가서야 터졌다:
            //       정적 `--check` 초록 · VM `E-VM-TYPE` · 네이티브 `panic`
            //   그리고 진단이 *"set index needs a slice + an int + a value"* 라 **원인을
            //   안 짚었다** — 인자 모양은 맞았다. 진짜 이유는 원소 타입이었다.
            //   ⇒ 여기서 **이름을 대고 거절한다**. `--check` 초록은 "돌릴 수 있다" 는 뜻이어야 한다.
            //   ☞ `view_array` 는 같은 조건을 이미 이렇게 거절하고 있었다(§view_array).
            //     한쪽 문만 잠겨 있었던 것이다.
            if (!esz) {
                // ★ 타입 낱말 범위를 훑는다 — `mut slice box` 처럼 수식자가 앞에 붙는다.
                //   처음에 `ts` 가 곧 `slice` 라고 짐작했다가 안 걸렸다(`ts` 는 `mut`).
                bool sf2; proven_size_t si2 = 0;
                for (proven_size_t w = vh.p[q].ts; w + 1 < vh.p[q].te && w + 1 < f->nkids; w++) {
                    if (!is_atom(f->kids[w]) || !is_atom(f->kids[w + 1])) continue;
                    if (!ir_type_is_slice(&c, f->kids[w]->tok.lex)) continue;
                    si2 = ir_struct_find(&ir, f->kids[w + 1]->tok.lex, &sf2);
                    if (sf2 && !ir.structs[si2].viewable)
                        ir_fail(&c, "E-IR-UNDEF",
                                "a `slice <struct>` parameter needs a VIEWABLE element type — a struct "
                                "whose fields all have a byte layout (no refs, no slices, no owned "
                                "handles, total <= 255). This struct has none, so the slice would stay "
                                "a BYTE slice and `set (index …)` would fail at run time instead of here",
                                f->line);
                    break;
                }
            }
            if (esz <= 1 && !esgn) continue;
            bool sf; proven_size_t sl = ir_local_find(&c, vh.p[q].name, &sf);
            if (!sf) continue;
            ir_emit(&c, IRW_LOAD, (proven_i64)sl);
            ir_emit(&c, IRW_VARRAY, (proven_i64)esz | (eflt ? IR_FLT_BIT : 0) | (esgn ? IR_SGN_BIT : 0) |
                                    (esidx >= 0 ? (IR_STRUCT_BIT | ((proven_i64)esidx << 20)) : 0));
            ir_emit(&c, IRW_STORE, (proven_i64)sl);
        }
        // ★★★ X-0082 (2026-09-30) — **값으로 받은 구조체 매개변수는 이 op 의 지역 복사다**(정본 §6.5.1 (2a)). 그런데 레코드는
        //   손잡이로 넘어와서, 몸이 그 칸에 쓰면(`set (field p x) …` · 배열 칸 원소 · 배열 칸의 쓸 수 있는 보기) **부른 쪽의
        //   레코드가 바뀌었다** — 순수 `fn` 이 남의 `let` 을 바꿨다. ⇒ 몸이 그렇게 쓰는 매개변수만 진입에서 베낀다(쓰지 않으면 비용 0).
        //   `mut`·`owned`·`mut_ref` 는 부른 쪽의 것을 쓰라는 뜻이므로 그대로 둔다. actor 인스턴스는 값이 아니다(베끼지 않는다).
        for (proven_size_t q = 0; q < vh.np; q++) {
            if (vh.p[q].is_mut || vh.p[q].is_owned) continue;
            bool mref = false;
            for (proven_size_t w = vh.p[q].ts; w < vh.p[q].te && w < f->nkids; w++)
                if (is_atom(f->kids[w]) && veq(f->kids[w]->tok.lex, "mut_ref")) mref = true;
            if (mref || !vh.body) continue;
            bool sf; proven_size_t sl = ir_local_find(&c, vh.p[q].name, &sf);
            if (!sf || !c.locals[sl].tyname.size) continue;
            bool stf; proven_size_t si2 = ir_struct_find(&ir, c.locals[sl].tyname, &stf);
            if (!stf) continue;
            const low_ir_struct_t *sd = &ir.structs[si2];
            if (!ir_param_written(vh.body, vh.p[q].name, sd)) continue;
            bool plain = sd->nf > 0 && !sd->is_actor_state && !sd->is_mmio && !sd->is_reserve &&
                         !(sd->f[0].name.size == 2 && sd->f[0].name.ptr[0] == (proven_u8)'$');
            for (proven_size_t z = 0; z < sd->nf; z++) if (sd->f[z].owned) plain = false;
            if (!plain || ir.nmakes >= IR_MAXMAKES || sd->nf > IR_MAKE_MAXF) continue;
            proven_size_t my2 = ir.nmakes++;
            low_ir_make_t mk2 = { .type_name = sd->name, .nfields = 0 };
            for (proven_size_t z = 0; z < sd->nf; z++) {
                mk2.fields[mk2.nfields++] = sd->f[z].name;
                ir_emit(&c, IRW_LOAD, (proven_i64)sl);
                ir_emit(&c, IRW_FIELD, (proven_i64)ir_field_intern(&c, sd->f[z].name));
            }
            ir.makes[my2] = mk2;
            ir_emit(&c, IRW_MAKE, (proven_i64)my2);
            ir_emit(&c, IRW_STORE, (proven_i64)sl);
        }

        // ★ 계약이 슬라이스 **길이**에 대해 말하는 상수들을 모은다.
        //   `… len <name> … <N>` 꼴이 나오면 N 이 경계 길이 후보다.
        d->nlenk = 0;
        d->minlen = 0;
        d->maxlen = 0;
        // ★ `requires [static|debug] ge (len s) <N>` / `gt (len s) <N>` — **길이 하한**을 읽는다.
        //   괄호는 GROUP 이다(장식) — 벗기고 본다.
        for (proven_size_t j = 0; j + 3 < f->nkids; j++) {
            proven_size_t kk; bool asum, dbg;
            if (!ir_requires_at(f, j, &kk, &asum, &dbg) || asum) continue;
            if (kk + 2 >= f->nkids) continue;
            proven_u8str_view_t ow = f->kids[kk]->tok.lex;
            bool ge = veq(ow, "ge"), gt = veq(ow, "gt");
            if (!ge && !gt) continue;
            const low_cst_t *L = f->kids[kk + 1], *R = f->kids[kk + 2];
            while (L && L->kind == LOW_CST_GROUP && L->nkids == 1) L = L->kids[0];
            if (!(L && L->kind == LOW_CST_FORM && L->nkids == 2 && is_atom(L->kids[0]) &&
                  veq(L->kids[0]->tok.lex, "len"))) continue;
            proven_i64 nv;
            if (!is_atom(R) || !ir_int_lit(R->tok.lex, &nv)) continue;
            proven_i64 need = gt ? nv + 1 : nv;
            if (need > d->minlen) d->minlen = need;
        }
        // ★★★★ **길이 상한**(`lt`/`le` 가 왼쪽에 len · `gt`/`ge` 가 오른쪽에 len) — 오라클이
        //   이것을 알아야 «상계를 어긴 긴 슬라이스» 를 계약 밖 입력으로 센다(low_ir.h 참조).
        for (proven_size_t j = 0; j + 3 < f->nkids; j++) {
            proven_size_t kk; bool asum, dbg;
            if (!ir_requires_at(f, j, &kk, &asum, &dbg) || asum) continue;
            if (kk + 2 >= f->nkids) continue;
            proven_u8str_view_t ow = f->kids[kk]->tok.lex;
            bool lt = veq(ow, "lt"), le = veq(ow, "le"), gt2 = veq(ow, "gt"), ge2 = veq(ow, "ge");
            const low_cst_t *L = f->kids[kk + 1], *R = f->kids[kk + 2];
            while (L && L->kind == LOW_CST_GROUP && L->nkids == 1) L = L->kids[0];
            while (R && R->kind == LOW_CST_GROUP && R->nkids == 1) R = R->kids[0];
            const low_cst_t *lenf = NULL, *num = NULL;
            bool strict = false;
            if ((lt || le) && L && L->kind == LOW_CST_FORM && L->nkids == 2 && is_atom(L->kids[0]) &&
                veq(L->kids[0]->tok.lex, "len") && is_atom(R)) {          // len s < N · len s ≤ N
                lenf = L; num = R; strict = lt;
            } else if ((gt2 || ge2) && R && R->kind == LOW_CST_FORM && R->nkids == 2 &&
                       is_atom(R->kids[0]) && veq(R->kids[0]->tok.lex, "len") && is_atom(L)) {
                lenf = R; num = L; strict = gt2;                           // N > len s · N ≥ len s
            }
            if (!lenf || !num) continue;
            proven_i64 nv;
            if (!ir_int_lit(num->tok.lex, &nv)) continue;
            proven_i64 cap = strict ? nv - 1 : nv;
            if (cap < 0) cap = 0;
            if (d->maxlen == 0 || cap < d->maxlen) d->maxlen = cap;
        }
        for (proven_size_t j = 2; j + 2 < f->nkids; j++) {
            if (!is_atom(f->kids[j]) || !veq(f->kids[j]->tok.lex, "len")) continue;
            for (proven_size_t q = j + 1; q < f->nkids && q <= j + 3; q++) {
                proven_i64 nv;
                if (!is_atom(f->kids[q]) || !ir_int_lit(f->kids[q]->tok.lex, &nv)) continue;
                if (nv < 0 || nv > 64) break;
                bool dup = false;
                for (proven_u8 t2 = 0; t2 < d->nlenk; t2++) if (d->lenk[t2] == nv) dup = true;
                if (!dup && d->nlenk < 8) d->lenk[d->nlenk++] = nv;
                break;
            }
        }

        // ★★★ **`errors <변형> [<조건>] .`** — 절 하나에 **오류 하나** (2026-07-14).
        //
        //   전엔 `errors a when C . b when D .` 였다. `when` 이 왜 있었나 —
        //   **`errors` 가 variadic 이었기 때문이다.** `errors a b .` 가 이미 **"변형 둘"** 을
        //   뜻하므로, `errors a <조건>` 과 아리티로 구별할 수 없었다. **표지가 필요했다.**
        //
        //   절 하나에 오류 하나로 만들면 **아리티가 고정된다**(1 또는 2) ⇒ **`when` 이 필요 없다.**
        //     errors too_short lt (len data) 4 .     rem 조건 있음
        //     errors too_long .                      rem 조건 없음
        //   그리고 이 문법의 핵심 원리(`head form* closer`)에 **균일하게** 들어온다 —
        //   `errors` 만 자기 나름의 목록 규칙 + 표지를 갖고 있었다.
        //
        //   ★ 괄호로 가릴 수는 없었다: 이 언어에서 **괄호는 장식**이다(R5, 서식기가 넣고 뺀다).
        //     **장식은 뜻을 나를 수 없다.**
        c.def_form = f;
        c.pr = pr;                                  // ★ 유래 표(사용 자리)를 읽을 수 있게
        c.def_name = is_atom(f->kids[1]) ? f->kids[1]->tok.lex : (proven_u8str_view_t){ 0 };
        c.def_owner = d->owner_mod;                 // ★ 맨이름은 **제 모듈부터** 찾는다
        c.newhen = 0;
        d->nerrv = 0;
        for (proven_size_t j = 2; j + 1 < f->nkids; j++) {
            if (!is_atom(f->kids[j]) || !veq(f->kids[j]->tok.lex, "errors")) continue;
            // ★ 절의 끝: **다음 절 낱말** 또는 **본체 블록**. 원자만 훑으면 **괄호에서 멈춘다** —
            //   그리고 조건에는 괄호가 들어간다(`lt (len data) 4`). 그 조각이 잘려서
            //   `lt` 만 남았고, 피연산자 부족으로 터졌다(E-IR-ARITY 가 잡았다).
            proven_size_t ee = j + 1;
            while (ee < f->nkids && f->kids[ee]->kind != LOW_CST_BLOCK &&
                   !(is_atom(f->kids[ee]) && is_clause_word(f->kids[ee]->tok.lex))) ee++;
            if (j + 1 >= ee || !is_atom(f->kids[j + 1])) { j = ee - 1; continue; }
            if (d->nerrv < 255) d->nerrv++;
            proven_size_t ws = j + 2, we = ee;                 // 조건 = 변형 뒤의 낱말들
            if (we > ws && c.newhen < 8) {
                c.ewhen[c.newhen].name = f->kids[j + 1]->tok.lex;
                c.ewhen[c.newhen].ws = ws;
                c.ewhen[c.newhen].we = we;
                // ★ **시그니처에 싣는다** — `<cmp> <param> <lit>` 이면 호출자가 쓸 수 있다.
                d->ewhen[c.newhen].cmp = 0;
                if (we - ws == 3 && is_atom(f->kids[ws]) && is_atom(f->kids[ws + 1]) &&
                    is_atom(f->kids[ws + 2])) {
                    low_irw_t cw = ir_cmp_word(f->kids[ws]->tok.lex);
                    proven_i64 lit;
                    bool pf; proven_size_t pi = ir_local_find(&c, f->kids[ws + 1]->tok.lex, &pf);
                    if (cw != IRW_NOT && pf && pi < LOW_MAX_PARAMS &&
                        ir_int_lit(f->kids[ws + 2]->tok.lex, &lit)) {
                        d->ewhen[c.newhen].cmp = (proven_u8)cw;
                        d->ewhen[c.newhen].p = (proven_i8)pi;
                        d->ewhen[c.newhen].n = lit;
                    }
                }
                c.newhen++;
            }
            j = ee - 1;                                        // 다음 절로
        }
        d->newhen = c.newhen;

        // ★ ensures 수집 — 출구 계약. 반환값을 담을 숨은 지역을 하나 만든다.
        //   그리고 그 술어들이 **결과 범위**를 정의한다 — 그것이 호출자에게 넘어간다.
        c.nens = 0;
        d->eret = (low_ir_prng_t){ false, 0, 0 };
        {
            proven_i64 elo = INT64_MIN, ehi = INT64_MAX;
            bool any = false;
            for (proven_size_t j = 0; j + 3 < f->nkids; j++) {
                proven_size_t k2;
                if (!ir_ensures_at(f, j, &k2)) {
                    // ★★ **못 세운 `ensures` 는 말한다**(결함 노트 #11). `requires` 쪽은 이미
                    //   `W-CONTRACT-IGNORED` 로 「이 절은 절반만 지킨다」고 말하는데, `ensures` 는
                    //   같은 모양을 **조용히 버렸다** — `ensures le (mul ret 2) n .` 은 아무 데서도
                    //   검사되지 않으면서 문서에는 약속으로 남는다. 검사되지 않는 약속을 조용히
                    //   두는 것이 이 언어가 거절하는 바로 그것이다(정본 §6.4.12).
                    if (is_atom(f->kids[j]) && veq(f->kids[j]->tok.lex, "ensures"))
                        ir_warn_at(&c, "W-CONTRACT-IGNORED",
                                   "this `ensures` is not enforced at the exit. The exit check knows "
                                   "one shape — `ensures <cmp> ret <literal|name|field path>` — and "
                                   "this clause is not it (an expression on either side, or a left "
                                   "side that is not `ret`). Nothing stops a return value that breaks "
                                   "it, and the caller's interval analysis does not learn it either: "
                                   "the promise stands in the documentation and nowhere else. Return "
                                   "through a name you can compare directly, or say it with `guard`",
                                   f->kids[j]->tok.line, f->file);
                    continue;
                }
                low_irw_t w = ir_cmp_word(f->kids[k2]->tok.lex);
                proven_i64 nv;
                if (w == IRW_NOT) {
                    ir_warn_at(&c, "W-CONTRACT-IGNORED",
                               "this `ensures` uses a predicate the EXIT check does not know (it knows "
                               "lt · le · gt · ge · eq · ne). Nothing stops a return value that breaks "
                               "it, and the analysis does not learn it either",
                               f->kids[k2]->tok.line, f->file);
                    continue;
                }
                bool lit = ir_int_lit(f->kids[k2 + 2]->tok.lex, &nv);
                if (c.nens < 4) {
                    c.ens[c.nens].w = w; c.ens[c.nens].n = lit ? nv : 0;
                    c.ens[c.nens].rhs = f->kids[k2 + 2]->tok.lex;
                    c.ens[c.nens].is_lit = lit;
                    c.nens++;
                }
                switch (w) {                       // 술어 → 결과 범위
                    case IRW_LT: if (nv - 1 < ehi) ehi = nv - 1; any = true; break;
                    case IRW_LE: if (nv     < ehi) ehi = nv;     any = true; break;
                    case IRW_GT: if (nv + 1 > elo) elo = nv + 1; any = true; break;
                    case IRW_GE: if (nv     > elo) elo = nv;     any = true; break;
                    case IRW_EQ: elo = nv; ehi = nv;             any = true; break;
                    default: break;                // ne 는 구간으로 표현 불가
                }
            }
            if (c.nens > 0) c.ret_slot = ir_local_declare(&c, proven_u8str_view_from_cstr("ret"), f->line);
            // ★ 결과 범위는 **선언된 출력 타입과의 교집합**이다. `ensures le ret 200` 은
            //   상계만 준다 — 하계는 타입이 준다(u16 이면 0). 그것을 놓치면 호출자가
            //   "결과는 [−2⁶³, 200]" 이라는 쓸모없는 사실을 받는다(그리고 narrow 가 증명 안 된다).
            // ★ 절의 끝을 **다섯 낱말로만** 찾고 있었다 — `low_is_clause_word()` 는 **열두 낱말**
            //   을 안다. `errors`/`access`/`tests` 가 output 뒤에 오면 타입 범위가 삼켰다.
            //   헤더가 그 경계를 안다(DECISION-0015).
            {
                low_op_header_t oh = low_op_header(f);
                ityp_t ot = (oh.out_e > oh.out_s) ? ity_of_decl_c(&c, f, oh.out_s, oh.out_e) : ITY_UNK;
                if (ot.known && !ot.flt) {
                    proven_i64 tl = ot.has_rng ? ot.rlo : ity_lo(ot.bits, ot.sign);
                    proven_i64 th = ot.has_rng ? ot.rhi : ity_hi(ot.bits, ot.sign);
                    if (tl > elo) elo = tl;
                    if (th < ehi) ehi = th;
                }
            }
            if (any && elo <= ehi) d->eret = (low_ir_prng_t){ true, elo, ehi };
        }
        c.dropped_out = &d->ndropped;
        ir_contract_entry(&c, f);   // ★ 계약을 사실로 쓰려면 강제해야 한다 (RFC-0053 §8-7)
        ir_named_contracts(&c, pr, f);   // ★ `satisfies N` — **명명 계약도 계약이다**
        // ★ RFC-0008 §6.4/Q6 — 계약이 **허용하는** 파라미터 구간을 뽑아 둔다(경계값 테스트의 원천).
        //   구간 분석이 쓰는 것과 **같은 원천**이다: 타입 ∩ range ∩ requires.
        //   같은 원천을 쓴다는 것이 요점이다 — 테스트가 분석의 주장을 정확히 그 경계에서 친다.
        {
            iv_t sig[IR_MAXLOCALS];
            for (proven_size_t k2 = 0; k2 < IR_MAXLOCALS; k2++) sig[k2] = IV_TOP;
            for (proven_size_t k2 = 0; k2 < c.nlocals && k2 < IR_MAXLOCALS; k2++)
                sig[k2] = iv_ty(c.locals[k2].ty);
            for (proven_size_t k2 = 0; k2 < c.nlocals && k2 < LOW_MAX_PARAMS; k2++) {
                ityp_t t = c.locals[k2].ty;
                ityp_t bare = t; bare.has_rng = false;      // range 를 뺀 '타입만' 의 구간
                iv_t tv = iv_ty(bare);
                d->ptype[k2] = (low_ir_prng_t){ t.known && !t.flt, tv.lo, tv.hi };
            }
            iv_apply_requires(&c, f, sig);                  // requires 를 얹는다
            iv_apply_named(&c, pr, f, sig);                 // ★ **명명 계약도 계약이다**
            for (proven_size_t k2 = 0; k2 < c.nlocals && k2 < LOW_MAX_PARAMS; k2++)
                d->ptest[k2] = (low_ir_prng_t){ c.locals[k2].ty.known && !c.locals[k2].ty.flt,
                                                sig[k2].lo, sig[k2].hi };
            // ★★ R5 — 관계형 requires 를 **오라클에게도** 준다. 안 주면 오라클이 계약을 어기는
            //   입력을 "적법" 이라 부르고 넣는다(그리고 실제로 그렇게 고발했다).
            d->nprel = 0;
            {
                ivstate_t rs;
                for (proven_size_t k2 = 0; k2 < IR_MAXLOCALS; k2++) { rs.lerel[k2] = -1; rs.lestr[k2] = 0; }
                iv_apply_rel_requires(&c, f, &rs);
                for (proven_size_t k2 = 0; k2 < c.nlocals && k2 < LOW_MAX_PARAMS && d->nprel < 4; k2++) {
                    if (rs.lerel[k2] < 0 || (proven_size_t)rs.lerel[k2] >= LOW_MAX_PARAMS) continue;
                    d->prel[d->nprel].a = (proven_i8)k2;
                    d->prel[d->nprel].b = (proven_i8)rs.lerel[k2];
                    d->prel[d->nprel].strict = rs.lestr[k2];
                    d->nprel++;
                }
            }
            // ★★★ **금지된 점** — `requires ne <이름> <리터럴>` (2026-09-07).
            //   구간은 구멍을 못 내므로 이 사실은 `ptest` 에 안 실린다. 오라클에게는 그것이
            //   *"0 도 허용된 입력"* 으로 보였고, 진입 계약이 0 을 무는 것을 **실패로 셌다**.
            //   ⇒ 절을 여기서 한 번 더 읽어 **금지된 점**으로 싣는다(강제는 이미 옳다).
            //   ★ 원자 두 개짜리 모양만 읽는다 — 그것이 진입 검사가 실제로 세우는 모양이고,
            //     그 밖의 모양은 오늘 `W-CONTRACT-IGNORED` 가 «안 세운다» 고 말한다.
            for (proven_size_t i2 = 0; i2 + 3 < f->nkids && d->npne < 4; i2++) {
                proven_size_t kk; bool is_asm, is_dbg;
                if (!ir_requires_at(f, i2, &kk, &is_asm, &is_dbg)) continue;
                if (is_asm) continue;                       // assume = 검사 없음
                if (!veq(f->kids[kk]->tok.lex, "ne")) continue;
                if (!is_atom(f->kids[kk + 1]) || !is_atom(f->kids[kk + 2])) continue;
                proven_i64 lit;
                if (!ir_int_lit(f->kids[kk + 2]->tok.lex, &lit)) continue;   // 리터럴만
                bool fs2; proven_size_t s2 = ir_local_find(&c, f->kids[kk + 1]->tok.lex, &fs2);
                if (!fs2 || s2 >= LOW_MAX_PARAMS || s2 >= c.nlocals) continue;
                d->pne[d->npne].p = (proven_i8)s2;
                d->pne[d->npne].v = lit;
                d->npne++;
            }
            // ★★★ **덧셈 사슬도 싣는다** — 진입에서 강제하기 시작했으므로(2026-09-07).
            //   모양: `<cmp> (add … ) <리터럴>`. 왼쪽 사슬의 잎은 매개변수이거나 리터럴이다.
            for (proven_size_t i2 = 0; i2 + 3 < f->nkids && d->npsum < 2; i2++) {
                proven_size_t kk; bool is_asm, is_dbg;
                if (!ir_requires_at(f, i2, &kk, &is_asm, &is_dbg)) continue;
                if (is_asm) continue;
                proven_u8str_view_t cw = f->kids[kk]->tok.lex;
                proven_u8 code = veq(cw, "lt") ? 0 : veq(cw, "le") ? 1 : veq(cw, "gt") ? 2
                               : veq(cw, "ge") ? 3 : veq(cw, "eq") ? 4 : veq(cw, "ne") ? 5 : 255;
                if (code == 255) continue;
                const low_cst_t *L2 = f->kids[kk + 1], *R2 = f->kids[kk + 2];
                while (L2 && L2->kind == LOW_CST_GROUP && L2->nkids == 1) L2 = L2->kids[0];
                if (!L2 || L2->kind != LOW_CST_FORM || !is_atom(R2)) continue;
                proven_i64 bound;
                if (!ir_int_lit(R2->tok.lex, &bound)) continue;
                // 사슬을 평평하게 편다 — 잎이 셋을 넘거나 모양이 다르면 안 싣는다(모르면 안 적는다).
                const low_cst_t *stack[8]; proven_size_t sp2 = 0; stack[sp2++] = L2;
                proven_i8 ps[3]; proven_u8 np = 0; proven_i64 konst = 0; bool ok2 = true;
                while (sp2 && ok2) {
                    const low_cst_t *n2 = stack[--sp2];
                    while (n2 && n2->kind == LOW_CST_GROUP && n2->nkids == 1) n2 = n2->kids[0];
                    if (!n2) { ok2 = false; break; }
                    if (is_atom(n2)) {
                        proven_i64 lv;
                        if (ir_int_lit(n2->tok.lex, &lv)) { konst += lv; continue; }
                        bool fs3; proven_size_t s3 = ir_local_find(&c, n2->tok.lex, &fs3);
                        if (!fs3 || s3 >= LOW_MAX_PARAMS || np >= 3) { ok2 = false; break; }
                        ps[np++] = (proven_i8)s3;
                        continue;
                    }
                    if (n2->kind == LOW_CST_FORM && n2->nkids == 3 && is_atom(n2->kids[0]) &&
                        veq(n2->kids[0]->tok.lex, "add") && sp2 + 2 <= 8) {
                        stack[sp2++] = n2->kids[1];
                        stack[sp2++] = n2->kids[2];
                        continue;
                    }
                    ok2 = false;
                }
                if (!ok2 || np == 0) continue;
                d->psum[d->npsum].n = np;
                for (proven_u8 q2 = 0; q2 < np; q2++) d->psum[d->npsum].p[q2] = ps[q2];
                d->psum[d->npsum].cmp = code;
                d->psum[d->npsum].k = konst;
                d->psum[d->npsum].bound = bound;
                d->npsum++;
            }
        }
        d->is_export = f->is_export;
        // ★★★ **`link` 는 양방향이다 — C 심볼의 이름을 **저자가** 정한다** (RFC-0063 · 단계 W).
        //
        //   여태 `link` 는 `extern`(우리가 C 를 부른다)에서만 읽혔다. 그런데 절의 뜻은
        //   *"C 심볼 이름(생략하면 op 이름)"* 이고, 그것은 **반대 방향에서도 똑같다**:
        //   `export` 는 C 가 우리를 부르는 자리이고, 그 이름도 C 심볼이다.
        //   ⇒ 같은 절을 export 에서도 읽는다. **새 낱말 0** — 이미 있는 절이다.
        //
        //   ★★★ 이것이 필요해진 이유: 단계 U ③ 이 **다른 모듈의 같은 이름**을 허용하자
        //     `export fn pick` 둘이 C 에서 부딪혔다(C 이름공간은 평평하다). 한때 도구가
        //     `<모듈>_<이름>` 으로 **지어냈는데** — 그것이 틀렸다: **C 심볼은 다른 언어에 하는
        //     약속이고, 약속은 저자의 것이다.** 도구는 이름을 발명하지 않는다. 겹치면
        //     거절하고(E-ABI-NAME-DUP) `link` 를 가리킨다.
        //   ☞ 내부 심볼(`lw_s_…__<해시>`)은 **다르다** — C 저자가 볼 일이 없으므로 도구가
        //     지어도 된다(거기 모듈을 넣은 것은 그대로다).
        if (f->is_export) {
            for (proven_size_t j = 2; j + 1 < f->nkids; j++)
                if (is_atom(f->kids[j]) && veq(f->kids[j]->tok.lex, "link") &&
                    is_atom(f->kids[j + 1]))
                    { d->link_name = f->kids[j + 1]->tok.lex; break; }
        }
        // ★★★ **`extern` — 몸이 C 에 있다** (RFC-0063). 본문을 낮추지 않는다.
        //   ★ 다만 `export extern` 은 **반대 방향**이다: 몸은 **우리 것**이고, C 가 그것을 부른다.
        if (f->is_extern && !f->is_export) {
            d->is_extern = true;
            d->link_name = d->name;
            for (proven_size_t j = 2; j + 1 < f->nkids; j++)
                if (is_atom(f->kids[j]) && veq(f->kids[j]->tok.lex, "link") &&
                    is_atom(f->kids[j + 1])) {
                    d->link_name = f->kids[j + 1]->tok.lex;   // `link "strlen" .`
                    // ★★★ **`link <심볼> from <라이브러리> .`** (2026-07-20) — 어느 라이브러리에
                    //   있는지 **언어가 말할 수 있게** 한다.
                    //   ☞ 전엔 말할 수가 없었다: `link sin .` 은 초록인데 링크가 `undefined
                    //     reference to 'sin'` 으로 죽었고, **골든이 `-lm` 을 37 곳에서 손으로**
                    //     붙이고 있었다. 그것이 곧 **두 번째 표현**이고, 갈린다(교훈 7 —
                    //     `--emit-h` 를 도구가 내게 만든 것과 **같은 논증**이다).
                    //   ★★ **새 낱말 0**: `from` 은 `use X from "path"` 가 이미 쓰는 문맥
                    //     낱말이다. 키워드 43 도 빌트인 op 148 도 안 는다.
                    if (j + 3 < f->nkids && is_atom(f->kids[j + 2]) &&
                        veq(f->kids[j + 2]->tok.lex, "from") && is_atom(f->kids[j + 3]))
                        d->link_lib = f->kids[j + 3]->tok.lex;
                }
            // 계약은 **경계에서** 검사된다 — 그것이 이 언어의 FFI 가 다른 이유다.
            ir_emit(&c, IRW_EXTERN, (proven_i64)i);
            ir_emit(&c, IRW_RET, 0);
            d->code = (low_ir_ins_t *)(void *)c.code.data;
            d->ncode = c.code.len;
            d->nlocals = c.nlocals;
            d->lowered = true;
            continue;
        }
        const low_cst_t *body = (f->kids[f->nkids - 1]->kind == LOW_CST_BLOCK) ? f->kids[f->nkids - 1] : NULL;
        // ★★★ **인라인 asm** (RFC-0041) — 이 op 의 몸이 **기계 명령**이다.
        //   컴파일러는 템플릿 **안을 안 읽는다**(어셈블러의 몫). 대신 **주변**을 안다:
        //   어느 값이 어느 레지스터로 들어가고, 무엇이 망가지고, 어느 ISA 인가.
        if (ir_asm_lower(&c, f, body, &ir)) { /* 몸은 asm 이 전부다 */ }
        else ir_block(&c, body);
        if (!c.failed) {
            ir_emit(&c, IRW_CONST, 0);   // implicit unit return at body end
            ir_emit(&c, IRW_RET, 0);
            d->code = (low_ir_ins_t *)(void *)c.code.data;
            d->ncode = c.code.len;
            d->nlocals = c.nlocals;
            d->lbuf_size = c.lbuf_off;   // ★ T2b-2 — 이 op 의 틀 안 나열 바이트
            d->lowered = true;
            {   // RFC-0053 — 파일 합계와 **def 몫**을 같이 센다(§8-9: 계수의 가중 교정)
                proven_size_t p0 = ir.checks_proven, t0 = ir.checks_total;
                ir_interval(&c, f, d, &ir.checks_proven, &ir.checks_total);
                d->checks_proven = ir.checks_proven - p0;
                d->checks_total  = ir.checks_total  - t0;
            }
        } else {
            d->lowered = false;
            ir.ok = false;
            proven_array_destroy(&c.code);
        }
    }

    // pass 3: SCC fixed-point Merkle def-hashes
    ir_def_hashes(work, &ir);
    // ★★★ **인스턴스 dedup** (RFC-0012) — iface·def 해시가 **둘 다** 같은 def 은 같은 정의다.
    //   내용주소화가 이미 그렇게 판정하는데(같은 해시) 뒤끝은 이름으로 심볼을 지어 본문을 두 번
    //   찍고 있었다. 정본을 가리켜 접는다. ★ export/extern 은 **심볼 자체가 계약**이라 접지 않는다.
    for (proven_size_t i = 0; i < ir.ndefs; i++) {
        low_ir_def_t *d = &ir.defs[i];
        // ★ **액터 핸들러는 접지 않는다** — 심볼이 **함수 포인터로 디스패치**되므로 두 액터의 같은
        //   이름 핸들러가 한 심볼이 되면 디스패치가 갈린다(put_sym 이 이미 그 사고를 기록해 뒀다).
        //   export/extern 과 같은 이유다: 거기서는 **심볼 자체가 계약**이다.
        if (!d->lowered || d->is_export || d->is_extern || d->is_test || d->is_actor) continue;
        for (proven_size_t j = 0; j < i; j++) {
            low_ir_def_t *e = &ir.defs[j];
            if (!e->lowered || e->is_export || e->is_extern || e->is_test || e->is_actor || e->canon_of) continue;
            if (memcmp(d->def_hash, e->def_hash, 32) == 0 &&
                memcmp(d->iface_hash, e->iface_hash, 32) == 0) { d->canon_of = (void *)e; break; }
        }
    }
    return ir;
}

/* low_irw_name — **op 이름표는 IR 코어의 것**이다. 문 하강 표지 안에 살고 있었으나
   책임으로 보면 `low_ir.c` 쪽이라 되돌렸다 (WO-0165). ☞ 구획 표지가 곧 책임은 아니다.
   ★ 이 자리는 `oracle-coverage` 가 **파일 이름으로** 찾던 곳이라 게이트가 즉시 울었다. */

static void hex8(const proven_u8 *h, char out[17]) {
    static const char hexd[] = "0123456789abcdef";
    for (unsigned i = 0; i < 8; i++) { out[2 * i] = hexd[h[i] >> 4]; out[2 * i + 1] = hexd[h[i] & 15]; }
    out[16] = 0;
}

// ★★★ **`.lowdb` — 내용주소 해시를 사이드파일로 낸다** (RFC-0012, 2026-07-20).
//
//   ☞ 해시는 **여태 계산만 되고 버려졌다.** `--ir` 이 화면에 찍을 뿐이라, 다음 실행이 이전
//     실행과 대조할 방법이 없었다 ⇒ RFC-0012 의 중심 주장인 *"변한 op 과 그 의존자만 다시
//     짓는다"* 가 **검사할 수 없는 주장**이었다. 검사되지 않는 주장은 §0 이 말하는 그것이다.
//
//   ★★ 이 파일이 **캐시 키로 쓸 만한지**를 이제 잴 수 있다:
//     ① 같은 소스 → **같은 바이트**(결정적이어야 한다. 안 그러면 캐시가 늘 빗나간다)
//     ② op 하나를 고치면 **그 op 의 def 해시만** 바뀐다(안 그러면 재사용할 것이 없다)
//     ③ **주석·서식만 바꾸면 아무것도 안 바뀐다**(canonical IR 은 표면을 안 본다)
//   ⇒ 골든이 셋 다 잰다. **증분 빌드 자체는 아직 안 지었다** — 그 전제부터 재는 것이 순서다.
//
//   형식: 사람이 읽고 `diff` 할 수 있는 텍스트. 줄 순서는 **선언 순서**(재현 가능).
// ★★★ 증명서 방출 — `--emit-proof`. **사람이 읽고 `diff` 할 수 있는** 텍스트다(lowdb 와 같은 결).
//   한 줄 = 지운 검사 하나.  `<op> <pc> <opcode> <rule> <수…>`
//   ☞ 검증기(`scripts/check-certificates.py`)가 이 줄만 보고 **규칙의 산술을 다시 한다.**
int low_ir_emit_proof(const low_ir_t *ir, FILE *out) {
    fputs("lowproof 1\n", out);
    // ★★★ **덮지 못한 자리를 먼저 말한다.** 지운 검사 수(분석기가 센 것)와 증명서 수가
    //   다르면, 그 차이만큼은 **근거 없이 지워진 것**이다 — 검증기가 그것을 보고해야 한다.
    //   (증명서를 붙이지 않은 규칙이 남아 있다는 뜻이고, 그 사실을 숨기면 이 파일이
    //    "전부 검증됐다" 로 읽힌다. 15장 오라클과 같은 규칙: **안 한 것을 말한다.**)
    fprintf(out, "# proven %zu certified %zu\n",
            (size_t)ir->checks_proven, (size_t)g_ncert);
    for (proven_size_t i = 0; i < g_ncert; i++) {
        const ir_cert_t *c = &g_cert[i];
        fwrite(c->def.ptr, 1, c->def.size, out);
        fprintf(out, " %u %s %s", (unsigned)c->pc, low_irw_name(c->w), c->rule);
        for (int k = 0; k < c->nf; k++) fprintf(out, " %lld", (long long)c->f[k]);
        for (proven_size_t k = 0; k < c->ext_n; k++)
            fprintf(out, " %lld", (long long)g_cert_ext[c->ext_at + k]);
        fputc('\n', out);
    }
    // ★ 넘쳐서 못 적은 것은 **말한다.** 조용히 자르면 검증기가 "전부 봤다" 고 착각한다.
    if (g_cert_drop)
        fprintf(out, "# DROPPED %zu certificate(s) — the buffer (%d) overflowed. "
                     "The validator must NOT read this file as complete.\n",
                (size_t)g_cert_drop, IR_CERT_MAX);
    return (int)g_ncert;
}

int low_ir_emit_db(const low_ir_t *ir, FILE *out) {
    int n = 0;
    fputs("lowdb 1\n", out);
    for (proven_size_t i = 0; i < ir->ndefs; i++) {
        const low_ir_def_t *d = &ir->defs[i];
        char ih[17], dh[17];
        hex8(d->iface_hash, ih);
        fputs(d->is_calc ? "fn " : "proc ", out);
        fwrite(d->name.ptr, 1, d->name.size, out);
        fprintf(out, "/%zu iface:%s", (size_t)d->nparams, ih);
        if (d->lowered) { hex8(d->def_hash, dh); fprintf(out, " def:%s", dh); }
        else fputs(" def:-", out);          // 하강 못 한 것은 **없다고 적는다**(있는 척 안 한다)
        fputc('\n', out);
        n++;
    }
    return n;
}

void low_ir_dump(const low_ir_t *ir) {
    printf("== stack IR (%zu def%s) ==\n", (size_t)ir->ndefs, ir->ndefs == 1 ? "" : "s");
    for (proven_size_t i = 0; i < ir->ndefs; i++) {
        const low_ir_def_t *d = &ir->defs[i];
        char ih[17], dh[17];
        hex8(d->iface_hash, ih);
        fputs(d->is_calc ? "fn " : "proc ", stdout);
        fwrite(d->name.ptr, 1, d->name.size, stdout);
        printf("/%zu  iface h:%s", (size_t)d->nparams, ih);
        if (!d->lowered) { printf("  (not lowered: outside the S5 core)\n"); continue; }
        hex8(d->def_hash, dh);
        printf("  def h:%s  locals %zu\n", dh, (size_t)d->nlocals);
        for (proven_size_t j = 0; j < d->ncode; j++) {
            printf("  %3zu  %-11s", (size_t)j, low_irw_name(d->code[j].w));
            switch (d->code[j].w) {
                case IRW_CONST: case IRW_LOAD: case IRW_STORE: case IRW_BR: case IRW_BRZ:
                case IRW_SWITCH:
                    printf(" %lld", (long long)d->code[j].a); break;
                case IRW_CALL: {
                    proven_size_t cix = IR_CALL_IDX(d->code[j].a);
                    char ch[17]; hex8(ir->defs[cix].def_hash, ch);
                    fputs(" ", stdout);
                    fwrite(ir->defs[cix].name.ptr, 1, ir->defs[cix].name.size, stdout);
                    printf(" h:%s%s", ch,
                           (d->code[j].a & IR_POL_PROVEN) ? " [args proven in range]" : "");
                    break;
                }
                case IRW_MAKE: {
                    const low_ir_make_t *mk = &ir->makes[d->code[j].a];
                    fputs(" ", stdout);
                    fwrite(mk->type_name.ptr, 1, mk->type_name.size, stdout);
                    printf("/%zu", (size_t)mk->nfields); break;
                }
                case IRW_WRAP_ERR:
                    fputs(" ", stdout);
                    fwrite(ir->errs[d->code[j].a].ptr, 1, ir->errs[d->code[j].a].size, stdout);
                    break;
                case IRW_FIELD:
                    fputs(" ", stdout);
                    fwrite(ir->fields[d->code[j].a].ptr, 1, ir->fields[d->code[j].a].size, stdout);
                    break;
                default: break;
            }
            putchar('\n');
        }
    }
    // RFC-0053 §6.4 — 제거되지 않은 검사는 **보고된다**. 침묵하면 성능 모델이 엔트로피가 된다.
    if (ir->folds) {   // RFC-0054 D6 — 어느 arm 이 선택됐는지 **보고된다**(비용 가시)
        printf("\n-- comptime target folding (RFC-0054) --\n");
        printf("   target = %s  (ptr %ub · %s-endian · fpu %s · no_heap %s)\n",
               low_ir_target()->name, low_ir_target()->ptr_width,
               low_ir_target()->big_endian ? "big" : "little",
               low_ir_target()->has_fpu ? "yes" : "no",
               low_ir_target()->no_heap ? "yes" : "no");
        printf("   %zu branch(es) folded at comptime — the dead arm is NOT emitted,\n", ir->folds);
        printf("   but it IS parsed and type-checked (unlike C's #ifdef).\n");
    }
    printf("\n-- runtime checks (interval analysis: overflow · division · narrowing) --\n");
    printf("   %zu / %zu removed", ir->checks_proven, ir->checks_total);
    if (ir->checks_total)
        printf("  (%zu%%)", 100 * ir->checks_proven / ir->checks_total);
    printf("\n");
    // ★★★★★ **「지웠다」 가 「안 나간다」 는 뜻은 아니다** (2026-09-09, RFC-0111 §8-16 · WO-0193).
    //   증명된 **산술**은 IR 에 표시만 되고 방출은 여전히 `lw_arith_i(...)` 트랩 호출을 낸다
    //   (`low_cbe.c` 가 그 까닭을 적는다 — 산술 의미를 두 곳에 두지 않으려고. 값은 두 번 쟀고
    //   두 번 다 ±1% 였다: 2026-08-18 · 2026-09-09). 색인·저장의 증명은 방출이 **실제로 쓴다**
    //   (`lw_index_nc`) — 그쪽은 진짜로 사라진다.
    //   ⇒ 그러니 위 수는 *"분석이 증명한 것"* 이지 *"코드에서 사라진 것"* 이 아니다.
    //     성능을 그 수로 예측하면 틀린다(실측: 계약을 더해 배수가 3.33 → 1.90 이 됐는데 이
    //     수는 **안 움직였다**). 셈을 바꾸는 대신 **말을 보탠다** — 수는 게이트가 읽으니까.
    //   ☞ *셈이 거짓말하면 셈을 고치거나 말을 고쳐야 한다. 둘 다 안 하면 다음 사람이 속는다.*
    {
        proven_size_t still = 0;
        for (proven_size_t d2 = 0; d2 < ir->ndefs; d2++)
            for (proven_size_t k2 = 0; k2 < ir->defs[d2].ncode; k2++) {
                const low_ir_ins_t *q = &ir->defs[d2].code[k2];
                if (!(q->a & IR_POL_PROVEN)) continue;
                if (q->w == IRW_ADD || q->w == IRW_SUB || q->w == IRW_MUL) still++;
            }
        if (still)
            printf("   ★ of those, %zu are ARITHMETIC: the proof is recorded in the IR but the C\n"
                   "     back end still emits the trapping call (measured twice — emitting them raw\n"
                   "     is worth about 1%%, and it would put integer semantics in two places).\n"
                   "     So this number is WHAT WAS PROVEN, not what disappeared from the code:\n"
                   "     do not predict performance from it (index/store proofs DO disappear).\n",
                   still);
    }
    for (proven_size_t d = 0; d < ir->ndefs; d++) {
        const low_ir_def_t *df = &ir->defs[d];
        if (!df->lowered) continue;
        proven_size_t rem = 0;
        // ★★★ **"몇 개 남았다" 만으로는 다음 걸음을 못 뗀다** (2026-08-17).
        //   `sort` 를 파는 데 하루가 걸렸는데 그중 적잖은 시간이 *어느 op 이 남았는지* 를
        //   손으로 되짚는 일이었다(방출 C 의 `lw_arith_i` 를 세어 IR 과 맞대는 식으로).
        //   **도구가 이미 아는 것을 사람이 다시 찾고 있었다** ⇒ 자리를 찍는다.
        for (proven_size_t i2 = 0; i2 < df->ncode; i2++) {
            const low_ir_ins_t *in = &df->code[i2];
            bool site = false;
            if ((in->w == IRW_ADD || in->w == IRW_SUB || in->w == IRW_MUL ||
                 in->w == IRW_DIV || in->w == IRW_MOD) &&
                (in->a & IR_TY_KNOWN) && !(in->a & IR_TY_FLT)) site = true;
            if (in->w == IRW_CAST) {
                proven_u8 nb = (proven_u8)((in->a & 0xff) * 8);
                if (nb && nb < 64) site = true;
            }
            if (!site) continue;
            if (in->a & (IR_POL_PROVEN | IR_POL_WRAP | IR_POL_SAT | IR_POL_CHK | IR_POL_NZ)) continue;
            rem++;
        }
        if (rem) {
            // ★ 이 자리에서 도구는 **거짓 조언**을 하고 있었다: "add a `requires` to prove the range".
            //   관계형 술어(`le a b`)는 그때 **읽히지도 않았고**(죽은 절이었다), 배열 내용에
            //   관한 사실은 이 도메인에 **표현할 자리가 아예 없다.** 조언을 따르면 아무 일도
            //   일어나지 않는다. **도구가 자기 한계를 프로그램 탓으로 돌린 것이다**(교훈 5).
            printf("   %.*s: %zu check(s) remain — the interval domain could not prove the range here.\n",
                   (int)df->name.size, (const char *)df->name.ptr, rem);
            printf("     · a `range`/`requires` bound on the inputs may close it — INCLUDING a relational\n");
            printf("       one (`requires le a b .`), which is now both ENFORCED and USED as a fact.\n");
            printf("     · but a fact about ARRAY CONTENTS (\"every element < 10\") has no place in this\n");
            printf("       domain at all: NO `requires` will close it. That is the TOOL's limit, not your\n");
            printf("       program's bug — and the check stays, so the answer is still right (R5, SPEC-016).\n");
            // ★ **자리를 찍는다** — `--ir` 덤프의 그 색인이다.
            //   ☞ 처음엔 색인을 `ridx[24]` 에 모았다가 `check-limits` 에 걸렸다(기준선 0):
            //     **자르는 고정 표는 "이게 전부" 로 읽힌다.** 표를 없애고 **다시 훑는다** —
            //     진단 경로라 두 번 훑는 값이 아깝지 않고, 자를 자리가 아예 없다.
            printf("     남은 자리(`--ir` 덤프의 색인):");
            for (proven_size_t i2 = 0; i2 < df->ncode; i2++) {
                const low_ir_ins_t *in2 = &df->code[i2];
                bool s2 = false;
                if ((in2->w == IRW_ADD || in2->w == IRW_SUB || in2->w == IRW_MUL ||
                     in2->w == IRW_DIV || in2->w == IRW_MOD) &&
                    (in2->a & IR_TY_KNOWN) && !(in2->a & IR_TY_FLT)) s2 = true;
                if (in2->w == IRW_CAST) {
                    proven_u8 nb2 = (proven_u8)((in2->a & 0xff) * 8);
                    if (nb2 && nb2 < 64) s2 = true;
                }
                if (!s2) continue;
                if (in2->a & (IR_POL_PROVEN | IR_POL_WRAP | IR_POL_SAT |
                              IR_POL_CHK | IR_POL_NZ)) continue;
                printf(" %zu:%s", (size_t)i2, low_irw_name(in2->w));
            }
            printf("\n");
        }
    }
}

void low_ir_free(proven_allocator_t work, low_ir_t *ir) {
    for (proven_size_t i = 0; i < ir->ndefs; i++)
        if (ir->defs[i].lowered && ir->defs[i].code) work.free_fn(work.ctx, ir->defs[i].code);
    if (ir->defs) work.free_fn(work.ctx, ir->defs);
    if (ir->makes) work.free_fn(work.ctx, ir->makes);
    if (ir->errs) work.free_fn(work.ctx, ir->errs);
    if (ir->fields) work.free_fn(work.ctx, ir->fields);
    if (ir->structs) work.free_fn(work.ctx, ir->structs);
    if (ir->strs) work.free_fn(work.ctx, ir->strs);
    if (ir->strew) work.free_fn(work.ctx, ir->strew);
    proven_array_destroy(&ir->diags);
}

// ═══════════════════════════════════════════════════════════════════════════
// ★★★ **asm 이 문장 자리에 온다** (RFC-0042 D11 · 2026-08-03)
//
//   대표 프로그램(blink)의 리셋 진입이 이것을 요구했다. 리셋은 `vector 1 .` 로 **하드웨어가
//   부르는** op 이고(D5-c), 그래서:
//     · 몸 전체를 asm 으로 둘 수 없다 — 클럭·GPIO·SysTick 초기화가 **Lowent** 여야 한다.
//     · `wait_irq k .`(cap machine 을 받는 asm op)를 **부를 수도 없다** — D5-b/c 의 권한은
//       **피호출자에게 미치면 안 된다**(미치면 그 문이 샌다). 건넬 cap 이 애초에 없다.
//   ⇒ 남는 길은 하나뿐이다: **`wfi` 를 진입 자신의 몸에 둔다.** 그전까지 리셋의 끝은
//     `while true . do end` — **바쁜 대기**였고, 배터리가 있는 기계에서 그것이 전부다.
//
//   문법(새 낱말 **0** — `asm`·`text` 재사용):
//
//       asm cortex_m options nomem nostack .      ← 절과 **같은 알맹이**를 읽는다
//       text ASM
//         wfi
//       ASM                                        ← 바로 다음 문장이 그 템플릿이다
//
//   ★ 격리는 **그대로**다: `unsafe` · `effects unsafe` · 타깃 대조(E-ASM-TARGET). 권한만
//     D5-b 의 논증을 따른다 — 하드웨어가 부른 op 은 **자기 몸 안에서** 명령을 낼 수 있고,
//     보통 op 은 여전히 `cap machine` 을 **건네받아야** 한다(low_check 의 ck_asm).
//   반환: 다음에 볼 문장의 인덱스.
static proven_size_t ir_asm_stmt(ir_ctx_t *c, const low_cst_t *blk, proven_size_t i) {
    const low_cst_t *f = blk->kids[i];
    low_ir_t *ir = c->out;
    if (ir->nasms >= 32) { ir_fail(c, "E-IR-UNSUP", "too many asm blocks", f->line); return i + 1; }
    low_ir_asm_t *a = &ir->asms[ir->nasms];
    memset(a, 0, sizeof(*a));
    a->line = f->line;

    if (!ir_asm_clause(c, a, f, 1, f->nkids)) return i + 1;

    // 템플릿 = **바로 다음 문장**의 heredoc. 없으면 그 asm 은 **아무 명령도 아니다**.
    const low_cst_t *nx = (i + 1 < blk->nkids) ? blk->kids[i + 1] : NULL;
    const low_cst_t *t = ir_asm_heredoc(nx);
    // ★★★ **heredoc 도 문장이므로 `.` 로 닫는다.** 안 닫으면 점-닫힘 파서가 **다음 문장들을
    //   그 폼 안으로 빨아들인다** — 그러면 템플릿은 못 찾고, 뒤 문장은 조용히 사라진다.
    //   ⇒ 그 모양을 알아보고 **이름을 불러 준다**(진단이 원인 자리를 가리켜야 한다).
    if (!t && nx && nx->kind == LOW_CST_FORM && nx->nkids > 1 &&
        nx->kids[0]->kind == LOW_CST_ATOM && nx->kids[0]->tok.kind == LOW_TOK_HEREDOC) {
        ir_fail(c, "E-ASM-BODY", "the assembly template of an `asm` statement is itself a statement, "
                "so it ends with `.` — write `ASM .` on the closing line. Without it the "
                "point-closure parser swallows the statements that follow INTO the template's form, "
                "and they vanish without a word", f->line);
        return i + 2;
    }
    if (!t) {
        ir_fail(c, "E-ASM-BODY", "an `asm` statement is followed by ITS assembly: one heredoc "
                "(`text ASM … ASM`) as the very next statement. An `asm` with no template names "
                "an instruction set and then says nothing — the tool will not guess which "
                "instructions you meant", f->line);
        return i + 1;
    }
    a->tmpl = t->tok.lex;
    ir_asm_tmpl_check(c, a, f->line);

    ir_emit(c, IRW_ASM, (proven_i64)ir->nasms);
    ir_emit(c, IRW_DROP, 0);   // ★ 문장이다 — 값을 남기지 않는다(op 의 몸일 때만 그것이 반환값이다)
    ir->nasms++;
    return i + 2;              // ★ heredoc 은 이 문장이 **먹었다**
}

 void ir_block(ir_ctx_t *c, const low_cst_t *blk) {
    if (!blk) return;
    proven_size_t i = 0;
    proven_size_t rel0 = g_nrel;                          // ★ RFC-0135 S2 — 이 블록에서 걸린 돌려주기는 블록 끝에서
    while (i < blk->nkids && !c->failed) {
        const low_cst_t *f = blk->kids[i];
        if (f->kind == LOW_CST_FORM && f->nkids > 0 && is_atom(f->kids[0])) {
            low_kw_t kw = f->kids[0]->tok.kw;
            if (kw == LOW_KW_MATCH) { ir_match(c, f); i++; continue; }
            if (kw == LOW_KW_GUARD) { i = ir_guard(c, blk, i); continue; }
            // ★★★ **asm 문장** (RFC-0042 D11) — 이 문장과 **바로 다음 heredoc** 이 한 쌍이다.
            //   그래서 ir_stmt 가 아니라 여기서 먹는다(다음 형제를 봐야 하므로).
            if (kw == LOW_KW_NONE && veq(f->kids[0]->tok.lex, "asm")) { i = ir_asm_stmt(c, blk, i); continue; }
            // ★ `ir_binding_split` 이 여기 있었다 — 정규화 층이 `be` 를 form 안으로 넣었으므로
            //   **통째로 사라졌다.** 재조립 코드가 사라지는 것이 앞단이 옳아졌다는 증거다.
        }
        ir_stmt(c, f);
        i++;
    }
    if (!c->failed) ir_release_down_to(c, rel0);
    g_nrel = rel0;
}
