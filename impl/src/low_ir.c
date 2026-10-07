// low_ir.c — S5b: MVP core (+slices/records/result) → stack IR + SCC Merkle hashing.
#include "proven/utf.h"
#include "low_hwm.h"
#include "low_ir.h"
proven_u64 low_oracle_budget(void);
#include "low_ir_priv.h"
#include "low_arity.h"
extern char **environ;   // ★ cap env (RFC-0030 D2′) — 호스트 프로파일 전용

#include <limits.h>
#include <stdarg.h>   // ★ 증명 운반 검사(cert_put)의 가변 인자
#include <math.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
// ★★★ 호스트 잎의 **결함 주입기** (RFC-0075 S5) — 유일한 원본. 방출기도 같은 바이트를 찍는다.
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

/* IR_MAXLOCALS — low_ir_priv.h (VM 의 지역 칸 수와 **같은 수**여야 한다) */
/* IR_MAXLOOP — low_ir_priv.h */
/* IR_MAXPATCH — low_ir_priv.h */
// ★★ 한계는 **하나의 수**여야 한다. `IR_MAXOPS` 는 256 인데 `IR_MAXDEFS` 는 **128 이었다** —
//   같은 한계의 **두 표현**이고, 반드시 갈렸다(교훈 7). 갈린 자리에서 무슨 일이 있었나:
//   op 이 128 개를 넘으면 `ir_def_hashes` 가 **조용히 돌아섰고**, 그러면 **모든 def 해시가 0** 이
//   됐다. **내용 주소화가 통째로 꺼지고 모든 op 이 같은 주소를 갖는다** — 경고 한 줄 없이.
//   내용 주소화는 이 언어의 **정체성 기제**다(SPEC-011). 그게 조용히 사라졌다.
//   ★ 실측 시점의 픽스처 전체가 **122 op** 이었다. **벼랑에서 여섯 걸음.**
/* IR_MAXOPS — low_ir_priv.h */
#define IR_MAXDEFS   IR_MAXOPS
/* IR_MAXENUMV — low_ir_priv.h */
/* IR_MAXENUMPF — low_ir_priv.h */
/* IR_MAXALIAS — low_ir_priv.h */
/* IR_MAXMAKES — low_ir_priv.h */
/* IR_MAXERRS — low_ir_priv.h */
// ★ 문자열 리터럴은 **자기 한계**를 가진다(2026-07-26). 전에는 IR_MAXERRS(64)를 같이 썼는데,
//   유니코드 속성 표처럼 리터럴이 곧 데이터인 모듈이 들어오자 오류 이름 개수와 무관한 이유로
//   한계에 부딪혔다 — 상수를 빌려 쓰면 한계가 **엉뚱한 이름**으로 나타난다.
/* IR_MAXSTRS — low_ir_priv.h */
/* IR_STRBUF — low_ir_priv.h */
/* IR_MAXCOND — low_ir_priv.h */

 bool veq(proven_u8str_view_t v, const char *s) { return proven_u8str_view_eq(v, proven_u8str_view_from_cstr(s)); }
 bool is_atom(const low_cst_t *n) { return n && n->kind == LOW_CST_ATOM; }

// ── build context ─────────────────────────────────────────────────────────────

// RFC-0052 S2: 로워링이 값의 (폭, 부호)를 추적한다 — 런타임 부호 충실성의 전제.
// known=false 는 '미상'(리터럴·비정수) — join 의 항등원이며, 그 경우 기존 동작(부호 있음)으로 후퇴한다.
/* ityp_t — low_ir_priv.h 로 옮겼다 (WO-0164: 구간분석이 쓴다) */

// ★ ensures 절 하나: `ensures <cmp> ret <N>` — 출구에서 강제되고, 증명되면 사라지고,
//   호출자에게는 **결과 구간**으로 넘어간다.
// ★★★ **다섯 번째 복사본**이었다. `ensures le ret <N>` 의 우변을 **리터럴만** 받았고,
//   변수면 **조용히 건너뛰었다**(`ensures le ret b .` 가 아무 일도 안 했다).
//   ⇒ 우변을 **이름으로도** 들고, `ir_contract_operand` 로 싣는다.
/* ir_ens_t — low_ir_priv.h */

 const ityp_t ITY_UNK = { false, 0, false, false, false, 0, 0 };
// ★ `tyname` — 이 지역의 **선언된 타입 이름**(구조체·별칭). 수신자 우선 호출(`s..area`)이
//   그것으로 op 을 찾는다: `<tyname>.area`. 전엔 IR 이 타입의 **수치 정보만** 들고 있었다.
// ★ `bset_w` — 이 지역이 **비트셋**이면 그 **선언 폭**(1..64), 아니면 0. `contains`/`remove`/
//   집합삽입 `add` 의 원소 범위검사와 `complement` 의 마스크가 이 폭을 **피연산자에서** 읽어
//   IR 에 굽는다(네이티브는 비트셋을 폭 없는 워드로 낮추므로 런타임엔 폭을 알 수 없다).
// ★ `narrowed` — 이 지역이 페이로드 enum 이고 `guard isa <이 지역> <변형>` 로 **좁혀졌으면**
//   그 변형 이름(RFC-0080 §4.6). 비어 있으면 안 좁혀진 것. `get` 이 이걸 보고 접근을 강제한다.
/* ir_local_t — low_ir_priv.h */

/* ir_loop_t — low_ir_priv.h */


/* ir_ctx_t — low_ir_priv.h 로 옮겼다 (WO-0164: 구간분석이 쓴다) */


// ★ 계약 피연산자를 싣는 **유일한 답** (정의는 아래). 다섯 곳이 각자 하던 일이다.
 bool ir_contract_operand(ir_ctx_t *c, proven_u8str_view_t v, proven_u32 line);

// ★★★ **단형화 인스턴스 안에서 터진 진단은 반쪽이다** (RFC-0084 §2.3).
//   틀의 줄은 옳지만, 저자가 **쓴 적 없는 코드**의 줄이다 — C++ 템플릿 오류가 악명 높은 이유가
//   정확히 이것이다. 저자가 쓴 것은 **호출/사용 한 줄**이고, 그 줄을 같이 말해야 고칠 수 있다.
//   ⇒ 지금 낮추는 def 이 인스턴스(`#`)면 **NOTE 를 하나 더** 낸다: "여기서 만들어졌다".
static void ir_mono_note(ir_ctx_t *c) {
    if (!c->pr || !c->def_name.size) return;
    bool inst = false;
    for (proven_size_t i = 0; i < c->def_name.size; i++)
        if (c->def_name.ptr[i] == (proven_u8)'#') inst = true;
    if (!inst) return;
    for (proven_size_t i = 0; i < c->pr->nsites; i++) {
        if (!proven_u8str_view_eq(c->pr->sites[i].name, c->def_name)) continue;
        if (!c->pr->sites[i].line) return;
        low_diag_t n = { .sev = LOW_SEV_NOTE, .code = "N-MONO-SITE",
                         .msg = "…and THIS is the line that asked for that instance — the error above "
                                "is inside a GENERIC template you did not write. The template's line is "
                                "right; what was missing is WHERE it was instantiated. Fix the type "
                                "argument here, or state the requirement on the template's boundary",
                         .line = c->pr->sites[i].line, .col = 0 };
        (void)proven_array_push(&c->out->diags, &n);
        return;
    }
}
 void ir_diag(ir_ctx_t *c, const char *code, const char *msg, proven_u32 line) {
    low_diag_t d = { .sev = LOW_SEV_ERROR, .code = code, .msg = msg, .line = line, .col = 0,
                     .file = low_cst_file_for_line(c->def_form, line) };
    (void)proven_array_push(&c->out->diags, &d);
    ir_mono_note(c);
}
 void ir_fail(ir_ctx_t *c, const char *code, const char *msg, proven_u32 line) {
    if (!c->failed) ir_diag(c, code, msg, line);
    c->failed = true;
}

// ★★★★ **못 세우는 계약은 «못 세운다» 고 말해야 한다** (2026-09-07, 소유자 지시 «경고 신설»).
//   진입 계약이 아는 모양은 몇 가지뿐이고, 그 밖의 모양은 여기서 **말없이 버려졌다**:
//   `requires lt (div n 8) (len s) .` 는 경고도 에러도 없이 사라졌고, 읽는 사람은
//   *"이 op 은 범위를 계약으로 막는다"* 고 믿었다. 그 믿음이 틀렸다.
//   ⇒ 오늘은 분석도 그 모양을 안 배우므로 **불건전하지는 않다**. 그래서 에러가 아니라
//     **경고**다 — 그러나 «조용함» 이라는 가장 나쁜 성질은 없앤다.
//   ☞ *못 지킬 약속은 안 적는 편이 낫고, 도구가 못 지킬 약속은 도구가 그렇다고 말해야 한다.*
//   (사연: docs/known-defects/compound-requires-ignored.md)
// ★★ **경고는 저자에게 하는 말이지, 실행이 나르는 값이 아니다** (2026-09-07, 실측이 가르쳐 줌).
//   처음엔 언제나 냈다. 그랬더니 골든 넷이 깨졌다 — VM 과 네이티브의 출력을 **바이트로** 견주는
//   검사들이었고, VM 쪽에만 경고 줄이 붙었기 때문이다. 그리고 그것이 옳은 신호다:
//   `--run` 의 표준출력은 프로그램의 답이고, `--emit-c` 의 것은 C 소스다.
//   ⇒ 저자를 향한 도구(`--check` · `--ir`)에서만 켠다. 실행은 조용하다.
//   ☞ *경고를 아무 데나 내면, 그 출력을 읽는 기계가 먼저 깨진다.*
// ★★ 그리고 **문장 안에 다른 진단 코드를 적지 않는다**: 처음엔 «상수 호출은 여전히
//   E-CONTRACT-IMPOSSIBLE 로 거절된다» 라고 썼는데, 예제를 컴파일해 `E-…` 를 grep 하는
//   검사(`check-spec-code`)가 그 글자를 보고 **멀쩡한 예제를 실패로 고발했다.**
//   ☞ *진단 문장은 사람이 읽지만, 진단 «코드» 는 기계가 읽는다. 문장 안에 코드를 적으면 둘이 섞인다.*
static bool g_warn_author = false;
void low_ir_set_author_warnings(bool on) { g_warn_author = on; }

// ★★★ **어느 파일의 몇 째 줄인가, 그리고 한 번만** (2026-09-07, 130 자리를 가르다 드러남).
//   처음엔 줄만 실었다. 그런데 이 경고가 붙는 절은 대개 **라이브러리**에 있고 단형화가
//   그 op 을 인스턴스마다 다시 만든다 ⇒ ⓐ 줄 번호는 파일마다 1 부터 다시 시작하므로
//   **엉뚱한 파일의 그 줄**을 가리켰고(`vm_vecgen.low:76` 은 그냥 `end .` 이었다)
//   ⓑ 같은 절이 인스턴스 수만큼 되풀이됐다(코퍼스 130 중 101 이 그 되풀이였다).
//   ⇒ 파일을 함께 싣고, **(파일·줄)이 같은 것은 한 번만** 낸다.
//   ☞ *같은 사실을 여러 번 세면 그 수는 «얼마나 나쁜가» 가 아니라 «얼마나 복제됐나» 를 잰다.*
void ir_warn_at(ir_ctx_t *c, const char *code, const char *msg, proven_u32 line,
                const char *file) {
    if (!g_warn_author) return;
    for (proven_size_t i = 0; i < c->out->diags.len; i++) {
        const low_diag_t *o = PROVEN_ARRAY_GET(&c->out->diags, low_diag_t, i);
        if (o->code == code && o->line == line && o->file == file) return;   // 이미 말했다
    }
    low_diag_t d = { .sev = LOW_SEV_WARNING, .code = code, .msg = msg, .line = line, .col = 0,
                     .file = file };
    (void)proven_array_push(&c->out->diags, &d);
}
// ★ 런타임에 만든 문장을 싣는다 — `msg` 는 **포인터로 보관**되므로 스택 버퍼를 그대로 주면
//   대롱거린다. 진단이 스스로 들고 있는 `detail` 로 **복사**한다(그 자리가 그러라고 있다).
// ★ 방출 자리가 수리를 **알 때**는 그것을 실어 보낸다. 표(low_repair.c)는 나머지를 맡는다 —
//   그 파일의 규율 ②가 그렇게 적혀 있다: *"방출 자리가 d->repair 를 채웠으면 그쪽이 이긴다."*
static void ir_fail_r(ir_ctx_t *c, const char *code, const char *msg,
                      const char *repair, proven_u32 line) {
    if (c->failed) return;
    low_diag_t d = { .sev = LOW_SEV_ERROR, .code = code, .msg = msg, .line = line, .col = 0,
                     .repair = repair, .file = low_cst_file_for_line(c->def_form, line) };
    (void)proven_array_push(&c->out->diags, &d);
    c->failed = true;
}

// ★★★ `alloc_bytes <뿌리> capacity <n>` 의 표식 (RFC-0112 D8(7) · WO-0219) — **필수**다. 전엔 있으면 건너뛰고 없어도
//   통과했다(`alloc_bytes al 8` 이 시험 코퍼스 여섯 자리에 있었다). 한 개념 한 철자.
static bool ir_capacity_mark(ir_ctx_t *c, low_cst_t *const *k, proven_size_t *pos, proven_size_t end, proven_u32 line) {
    if (*pos < end && is_atom(k[*pos]) && veq(k[*pos]->tok.lex, "capacity")) { (*pos)++; return true; }
    ir_fail(c, "E-ALLOC-CAPACITY-MARK",
            "`alloc_bytes` spells its size with the `capacity` marker — `alloc_bytes al capacity 16`. "
            "The marker is required (RFC-0112 D8(7)): the root and the size are otherwise two bare "
            "operands, and the operand-omitted form `alloc_bytes capacity n` is only told apart from "
            "a named root by the marker", line);
    return false;
}

static void ir_fail_buf(ir_ctx_t *c, const char *code, const char *msg, proven_u32 line) {
    if (!c->failed) {
        low_diag_t d = { .sev = LOW_SEV_ERROR, .code = code, .line = line, .col = 0,
                         .file = low_cst_file_for_line(c->def_form, line) };
        proven_size_t n = 0;
        while (msg[n] && n + 1 < sizeof d.detail) { d.detail[n] = msg[n]; n++; }
        d.detail[n] = '\0';
        d.msg = NULL;
        (void)proven_array_push(&c->out->diags, &d);
        // ★ `msg` 는 NULL 로 둔다 — 문장은 그 원소의 `detail` 에 있고 렌더러는 `low_diag_text` 로 읽는다
        //   (2026-09-14: `msg = detail` 로 가리키면 배열이 재할당될 때 옛 자리를 가리켰다).
        ir_mono_note(c);
    }
    c->failed = true;
}
 int ir_word_arity(const low_ir_t *ir, low_irw_t w, proven_i64 a);

// ⊑ 격자의 join (NumericLattice.v 의 join — 두 피연산자 중 하나, 발명하지 않는다).
// 미상(known=false)은 항등원처럼 작동한다: join(unk, T) = T.
static ityp_t ity_of_word(proven_u8str_view_t v);   // ★ 아래 정의를 앞당겨 쓴다(IRW_CALL 의 결과 타입)
static ityp_t ity_join(ityp_t a, ityp_t b) {
    if (!a.known) return b;
    if (!b.known) return a;
    if (a.flt != b.flt) return ITY_UNK;                 // kind 혼합 — S1 이 거부했다
    if (a.flt) return a.bits >= b.bits ? a : b;         // f32 ⊑ f64
    if (a.sign == b.sign) return a.bits >= b.bits ? a : b;
    return ITY_UNK;                                     // 부호 충돌 — S1 이 거부했다
}
// 폭·부호·kind 를 명령에 싣는다. 미상이면 0 → VM 은 기존 경로로 후퇴.
static proven_i64 ity_meta(ityp_t t) {
    if (!t.known) return 0;
    return IR_TY_KNOWN | (t.sign ? IR_TY_SIGNED : 0) | (t.flt ? IR_TY_FLT : 0) | t.bits;
}

static void ir_tpush(ir_ctx_t *c, ityp_t t) {
    if (c->tsp >= 64) { c->tstk_bad = true; return; }
    c->tstk[c->tsp++] = t;
}
static ityp_t ir_tpop(ir_ctx_t *c) {
    if (c->tsp == 0) { c->tstk_bad = true; return ITY_UNK; }
    return c->tstk[--c->tsp];
}

// ── RFC-0054: 타깃 표 ────────────────────────────────────────────────────────
static const low_target_t IR_TARGETS[] = {
    //  name        BE     ptr  fpu    nohp   nofl   simd  posix  max_slice_len       cache page
    //  ★ max_slice_len — **주소공간에서 나온 상한**. 64 비트는 2^48(현실적 가상주소),
    //    32 비트는 2^24(16 MiB — MMU 없는 프리스탠딩 기계의 넉넉한 상한).
    //    보수적으로 잡는다: 이 수를 넘는 슬라이스는 만들 수 없다는 **약속**이므로,
    //    크게 잡을수록 약속이 약해지고 작게 잡을수록 더 많이 지워진다.
    //    조절은 **이 줄 하나**다(소유자 요구: "차후 조절 가능").
    { "x86_64",   false, 64, 8, 8, false, true,  false, false, 16, true, (proven_i64)1 << 48, 64, 4096 },   // SSE2 baseline = 128-bit
    { "arm64",    false, 64, 8, 8, false, true,  false, false, 16, true, (proven_i64)1 << 48, 64, 4096 },   // NEON mandatory (ARMv8) = 128-bit
    { "cortex_m", false, 32, 4, 4, false, false, true,  true,  0,  false, (proven_i64)1 << 24, 4, 0 },   // freestanding: no_heap · no_float · SIMD 없음
    { "riscv64",  false, 64, 8, 8, false, true,  false, false, 0,  true, (proven_i64)1 << 48, 64, 4096 },   // baseline: 벡터(RVV) 는 opt-in ext → 없음
    { "mips_be",  true,  32, 4, 4, false, true,  false, false, 0,  true , (proven_i64)1 << 24, 32, 4096 },   // 빅엔디안 대표. MSA opt-in → baseline SIMD 없음
    // ★★★★★ **Windows — 크로스 빌드까지만** (2026-08-19, 소유자 결정).
    //   RFC-0069 §6.7 의 승격 조건 넷 중 ①②만 연다: 타깃이 있고, `--emit-c` 가 그 타깃에서
    //   **빌드된다**. ③(왕복 성질을 골든이 잼)·④(host-fault 가 그쪽 잎에서 돎)는 **실행**이
    //   필요한데 이 저장소에 Windows 도 wine 도 없다 ⇒ §6.7 은 **Proposed 로 남는다.**
    //   ☞ 그래서 이 타깃은 *"Windows 를 지원한다"* 가 아니라 *"POSIX 를 안 쓰는 프로그램이
    //     Windows 로 건너간다"* 를 뜻한다. POSIX 전용 잎은 **컴파일 시 거절**한다 —
    //     mingw 에서 헤더가 없다고 터지게 두는 것보다, 이유를 아는 자리에서 먼저 우는 편이 낫다.
    { "win64",    false, 64, 8, 8, false, true,  false, false, 16, false, (proven_i64)1 << 48, 64, 4096 },
    // ★★ RFC-0137 P5 — **첫 능력 포인터 대상: CHERIoT** (2026-10-04, 소유자 결정 Q6). 32 비트 RISC-V 마이크로컨트롤러 · 주소 32 비트 ·
    //   포인터(능력) 8 바이트 · 정렬 8 · 정수에서 포인터를 만들 수 없다. 운영체제 · 힙 · 부동 장치가 없다(cortex_m 과 같은 프리스탠딩 칸).
    //   지금은 **컴파일까지** 잰다(CHERIoT clang · `toolchains/cheriot-tools-*`) — 실행은 그 툴체인의 시뮬레이터로 뒤에.
    { "cheriot",  false, 32, 8, 8, true,  false, true,  true,  0,  false, (proven_i64)1 << 24, 8, 0 },
};
 const low_target_t *ir_tgt = &IR_TARGETS[0];
 bool g_smt_on = true;
void low_ir_set_smt(bool on) { g_smt_on = on; }

// ★★★ **질의를 표준 형식으로도 낸다** (`--emit-smt`, 후속 M).
//   왜 필요한가: 우리 절차가 *"모순이다"* 라고 말할 때, **다른 솔버도 같은 말을 하는가**를
//   물을 수 있어야 한다. SMT-LIB2 는 그 물음의 공용어다(Z3·CVC5·Yices 가 전부 읽는다).
//   ☞ 이것은 게이트의 재료이지 컴파일의 일부가 아니다 — 방출은 요청할 때만 한다.
//   ★ 그리고 이 출구는 **부정된 목표까지 그대로** 낸다: 외부 솔버가 `unsat` 이라고 답해야
//     우리가 지운 것이 옳다. 목표를 빼고 내면 그 대조는 아무것도 대조하지 않는다.
static bool g_smt_emit = false;
static FILE *g_smt_out = NULL;
void low_ir_set_smt_emit(bool on) { g_smt_emit = on; g_smt_out = stdout; }

 void smt_emit_query(const low_ir_def_t *d, proven_size_t pc, const low_smt_sys_t *s) {
    if (!g_smt_emit || !g_smt_out) return;
    fputs("; ── query: ", g_smt_out);
    fwrite(d->name.ptr, 1, d->name.size, g_smt_out);
    fprintf(g_smt_out, " @%zu — expect unsat ─────────────\n", (size_t)pc);
    for (proven_size_t i = 0; i < s->nv; i++)
        fprintf(g_smt_out, "(declare-const x%zu Int)\n", (size_t)i);
    for (proven_size_t j = 0; j < s->nc; j++) {
        fputs("(assert (<= (+", g_smt_out);
        for (proven_size_t i = 0; i < s->nv; i++)
            fprintf(g_smt_out, " (* %lld x%zu)", (long long)s->row[j].a[i], (size_t)i);
        fprintf(g_smt_out, " %lld) 0))\n", (long long)s->row[j].c);
    }
    fputs("(check-sat)\n", g_smt_out);
}

// ★ 정본 §5.6 (3) — 표에 없는 이름은 **거부**한다(false). 전에는 조용히 무시해 기본 기계(x86_64)로 지었다 —
//   `--target cheriot` 를 잘못 친 `--target cheriott` 가 호스트 빌드로 «통과» 했다.
bool low_ir_set_target(const char *name) {
    for (proven_size_t i = 0; i < sizeof IR_TARGETS / sizeof IR_TARGETS[0]; i++)
        if (strcmp(IR_TARGETS[i].name, name) == 0) { ir_tgt = &IR_TARGETS[i]; return true; }
    return false;
}
proven_size_t low_ir_target_count(void) { return sizeof IR_TARGETS / sizeof IR_TARGETS[0]; }
const char *low_ir_target_name(proven_size_t i) { return i < low_ir_target_count() ? IR_TARGETS[i].name : NULL; }
unsigned low_lbuf_max(void) { return low_ir_target()->no_heap ? LOW_LBUF_MAX_FREE : LOW_LBUF_MAX; }   // RFC-0135 S3
const low_target_t *low_ir_target(void) { return ir_tgt; }
static proven_size_t g_fixed_bytes;   // 0 = 타깃이 정한다
void low_ir_set_fixed_bytes(proven_size_t n) { g_fixed_bytes = n; }
proven_size_t low_ir_fixed_bytes(void) {
    if (g_fixed_bytes) return g_fixed_bytes;
    return ir_tgt->no_heap ? (proven_size_t)LOW_FIXED_BYTES_BOARD : (proven_size_t)LOW_FIXED_BYTES;
}
unsigned low_ptr_slot(void) {
    unsigned b = ir_tgt->ptr_bytes < 8 ? 8u : (unsigned)ir_tgt->ptr_bytes;
    return (b + 7u) & ~7u;
}
unsigned low_ptr_slot_align(void) { return ir_tgt->ptr_align < 8 ? 8u : (unsigned)ir_tgt->ptr_align; }
bool low_ir_target_known(proven_u8str_view_t name) {
    for (proven_size_t i = 0; i < sizeof IR_TARGETS / sizeof IR_TARGETS[0]; i++)
        if (proven_u8str_view_eq(name, proven_u8str_view_from_cstr(IR_TARGETS[i].name))) return true;
    return false;
}

// `machine.<prop>` → comptime 상수. 없는 속성이면 false 를 돌려주고 *found = false.
 proven_i64 ir_machine(proven_u8str_view_t v, bool *found) {
    *found = true;
    const low_target_t *t = ir_tgt;
    if (veq(v, "machine.big_endian"))    return t->big_endian;
    if (veq(v, "machine.little_endian")) return !t->big_endian;
    if (veq(v, "machine.ptr_width"))     return t->addr_bits;   // 옛 이름 — 주소 폭(RFC-0137: 포인터 크기와 다를 수 있다)
    if (veq(v, "machine.addr_bits"))     return t->addr_bits;
    if (veq(v, "machine.ptr_bytes"))     return t->ptr_bytes;
    if (veq(v, "machine.ptr_align"))     return t->ptr_align;
    if (veq(v, "machine.ptr_caps"))      return t->ptr_caps;
    if (veq(v, "machine.max_slice_len")) return t->max_slice_len;
    // ★ RFC-0104 §8-7 — 배치를 정하는 두 수. **질의이지 정책이 아니다**(자동 repack 없음).
    if (veq(v, "machine.cache_line"))    return t->cache_line;
    if (veq(v, "machine.page_size"))     return t->page_size;
    if (veq(v, "machine.has_fpu"))       return t->has_fpu;
    if (veq(v, "machine.no_heap"))       return t->no_heap;
    if (veq(v, "machine.no_float"))      return t->no_float;
    if (veq(v, "machine.is_x86_64"))     return strcmp(t->name, "x86_64") == 0;
    if (veq(v, "machine.is_arm64"))      return strcmp(t->name, "arm64") == 0;
    if (veq(v, "machine.is_cortex_m"))   return strcmp(t->name, "cortex_m") == 0;
    if (veq(v, "machine.is_riscv64"))    return strcmp(t->name, "riscv64") == 0;
    *found = false;
    return 0;
}
// 조건이 comptime 상수인가 — 지금은 `machine.<prop>` 원자 하나만(최소 구현, RFC-0054 §9 C1)
/* IR_MAXOPT — low_ir_priv.h */
/* ir_opt_t — low_ir_priv.h 로 옮겼다 (WO-0164: 구간분석이 쓴다) */

 ir_opt_t     g_opt[IR_MAXOPT];
 proven_size_t g_nopt;
/* ir_cfg_t — low_ir_priv.h */

 ir_cfg_t     g_cfg[IR_MAXOPT];
 proven_size_t g_ncfg;

void low_ir_config_reset(void) { g_ncfg = 0; }
void low_ir_config_add(const char *name, const char *value) {
    if (g_ncfg >= IR_MAXOPT) return;
    snprintf(g_cfg[g_ncfg].name, sizeof g_cfg[0].name, "%s", name);
    snprintf(g_cfg[g_ncfg].val, sizeof g_cfg[0].val, "%s", value);
    g_ncfg++;
}
proven_size_t low_ir_config_count(void) { return g_ncfg; }

 ir_opt_t *ir_opt_find(proven_u8str_view_t n) {
    for (proven_size_t i = 0; i < g_nopt; i++)
        if (proven_u8str_view_eq(n, proven_u8str_view_from_cstr(g_opt[i].name))) return &g_opt[i];
    return NULL;
}
 bool ir_opt_num(proven_u8str_view_t v, proven_i64 *out) {
    if (veq(v, "on") || veq(v, "true"))  { *out = 1; return true; }
    if (veq(v, "off") || veq(v, "false")) { *out = 0; return true; }
    proven_i64 n = 0; bool any = false;
    for (proven_size_t i = 0; i < v.size; i++) {
        if (v.ptr[i] < (proven_u8)'0' || v.ptr[i] > (proven_u8)'9') return false;
        n = n * 10 + (v.ptr[i] - (proven_u8)'0'); any = true;
    }
    *out = n;
    return any;
}

 bool ir_comptime_cond(ir_ctx_t *c, const low_cst_t *f, proven_size_t start, proven_size_t end,
                             proven_i64 *val) {
    (void)c;
    // ★★★ **구성 값은 comptime 상수다** (RFC-0036 D5 × RFC-0054 D2).
    //   `if config smp do … end` 는 **컴파일 시점에 접힌다** — 꺼진 가지는 생성물에 **없다**.
    //   그런데 `#ifdef` 와 달리 **두 가지 모두 파싱·타입 검사를 받는다**: 꺼진 코드가 썩지 않는다.
    {
        const low_cst_t *g = NULL;
        if (end == start + 1) {
            // ★ 나무 경로에서는 `config smp` 가 GROUP(FORM(...)) 으로 온다. 벗기지 않으면
            //   **평평할 때만 접히고 나무에서는 안 접힌다** — 그러면 같은 프로그램이 두 개의
            //   def 해시를 갖는다. 내용주소화가 그것을 **즉시 고발했다**(RFC-0012 = 오라클).
            const low_cst_t *u = f->kids[start];
            while (u && (u->kind == LOW_CST_GROUP) && u->nkids == 1) u = u->kids[0];
            if (u && u->kind == LOW_CST_FORM) g = u;
        }
        else if (end == start + 2 && is_atom(f->kids[start]) && veq(f->kids[start]->tok.lex, "config"))
            g = f;   // 평평한 형태: `if config smp do`
        if (g) {
            proven_size_t hi = (g == f) ? start : 0;
            if (g->nkids > hi + 1 && is_atom(g->kids[hi]) && veq(g->kids[hi]->tok.lex, "config") &&
                is_atom(g->kids[hi + 1])) {
                ir_opt_t *o = ir_opt_find(g->kids[hi + 1]->tok.lex);
                if (o) { *val = o->val; return true; }
            }
        }
    }
    if (end != start + 1 || !is_atom(f->kids[start])) return false;
    proven_u8str_view_t v = f->kids[start]->tok.lex;
    if (v.size < 8 || memcmp(v.ptr, "machine.", 8) != 0) return false;
    bool found;
    *val = ir_machine(v, &found);
    return found;
}

// ★★★★★ **POSIX 전용 잎은 POSIX 아닌 호스트에서 거절한다** (2026-08-19, RFC-0069 §6.7 ②).
//   `win64` 는 호스트지만 `dirent.h`·`sys/socket.h`·`termios.h`·`clock_gettime` 이 없다.
//   그대로 방출하면 mingw 가 *"헤더가 없다"* 로 터지고, 그 진단은 **원인을 안 말한다**.
//   ⇒ 여기서 먼저 운다.
//
//   ★ 관문이 **`ir_emit` 하나**인 것이 요점이다. 처음엔 제네릭 빌트인 경로에 걸었는데,
//     잎들은 각자 자기 표(FOPS·NOPS·TOPS·CLOPS…)에서 낮아지므로 **하나도 안 걸렸다**
//     (`ls`·`copy`·`nettcp` 가 전부 조용히 통과했다). 이 저장소가 두 번 당한
//     *"목록에서 한 줄 빠뜨림"* 의 세 번째 얼굴이다 — 표가 여럿이면 검사는 **그 아래**에 둔다.
//   ☞ 이 목록은 *지금 방출이 POSIX 를 쓰는 잎*이지 *"Windows 에서 불가능한 일"* 이 아니다.
//     Win32 로 다시 쓰면 목록에서 빠진다(그때가 §6.7 의 ③④다).
static bool ir_leaf_needs_posix(low_irw_t w) {
    switch (w) {
        case IRW_FOPEN: case IRW_FREAD: case IRW_FWRITE: case IRW_FSEEK: case IRW_FCLOSE:
        case IRW_DOPEN: case IRW_DREAD: case IRW_DCLOSE: case IRW_FTYPE: case IRW_LTYPE: case IRW_DMAKE:
        case IRW_PREMOVE: case IRW_PRENAME:
        case IRW_NPAIR: case IRW_NLISTEN: case IRW_NCONNECT:
        case IRW_NSEND: case IRW_NRECV: case IRW_NCLOSE: case IRW_NPORT:
        case IRW_TTYSIZE: case IRW_TTYRAW: case IRW_TTYREAD:
        case IRW_TIMENOW: case IRW_TIMELOCAL: case IRW_RANDBYTES:   // ★ time_sleep 는 Windows 에도 있다(RFC-0136, Sleep)
            return true;
        default: return false;
    }
}

 proven_size_t ir_emit(ir_ctx_t *c, low_irw_t w, proven_i64 a) {
    if (ir_leaf_needs_posix(w) && !low_ir_target()->posix && !low_ir_target()->no_heap)
        ir_fail(c, "E-TARGET-LEAF",
                "this host leaf is emitted with POSIX today (dirent / sockets / termios / "
                "clock_gettime), and the target asked for is a host WITHOUT POSIX "
                "(`--target win64`). The C would not compile there, so it is refused HERE — "
                "where the reason is known — instead of failing in the cross-compiler as a "
                "missing header. What crosses to that target today is every program that does "
                "not touch file / dir / net / tty / clock / random. RFC-0069 §6.7 is Proposed "
                "and names exactly what would open the rest (a Win32 rewrite of these leaves)",
                0);
    // 타입 그림자 스택 갱신 — 이항 수치 op 에는 부호를 실어 준다(S2).
    int ar = ir_word_arity(c->out, w, a);
    ityp_t res = ITY_UNK;
    if (ar >= 0) {
        ityp_t ops[3] = { ITY_UNK, ITY_UNK, ITY_UNK };
        for (int i = ar - 1; i >= 0; i--) { ityp_t t = ir_tpop(c); if (i < 3) ops[i] = t; }
        switch (w) {
            case IRW_LOAD:
                res = ((proven_size_t)a < c->nlocals) ? c->locals[a].ty : ITY_UNK;
                // ★★★ 슬라이스 지역은 스칼라가 아니라 `ty` 가 미상이다. 그 자리에 **원소의
                //   타입**을 실어 둔다 — 이 그림자를 읽는 것은 사실상 `index` 뿐이고(`len` 은
                //   제 결과를 따로 정한다), 그래야 `lt (index s 0) (index s 1)` 이 부호를 안다.
                //   ☞ *스택 기계의 그림자는 «지금 칸에 무엇이 있나» 를 말한다. 슬라이스 칸이
                //     아무 말도 안 하면, 거기서 꺼낸 값도 아무 말을 못 한다.*
                if (!res.known && (proven_size_t)a < c->nlocals && c->locals[a].elem.known)
                    res = c->locals[a].elem;
                break;
            case IRW_FCONST:
                res = ITY_UNK;   // D3: 부동 리터럴도 **comptime 무타입** — 상대 피연산자가 타입을 준다
                break;
            case IRW_ADD: case IRW_SUB: case IRW_MUL: case IRW_DIV: case IRW_MOD:
                res = ity_join(ops[0], ops[1]);
                if (!c->tstk_bad) a = ity_meta(res) | (a & (IR_POL_MASK | IR_POL_CHK | IR_POL_NZ));   // ★ 부호·폭 + 정책
                break;
            case IRW_LT: case IRW_LE: case IRW_GT: case IRW_GE:
                if (!c->tstk_bad) a = ity_meta(ity_join(ops[0], ops[1]));   // ★ 순서 비교의 부호
                res = ITY_UNK;                                              // 결과는 bool
                break;
            case IRW_NEG:
                res = ops[0];
                if (!c->tstk_bad) a = ity_meta(res);   // ★ 부호·폭 — 넘침을 선언 폭에서 본다 (RFC-0052 D4)
                break;
            // ★★★ **비트 연산은 폭을 안다** (RFC-0064). 그 폭이 곧 계약이다:
            //   `bit_not` 은 그 폭에서 뒤집고, 시프트는 그 폭보다 작아야 한다.
            //   ★ 시프트의 **오른쪽**(양)은 타입이 다르다 — **왼쪽의 타입**이 결과를 정한다.
            case IRW_BAND: case IRW_BOR: case IRW_BXOR:
                res = ity_join(ops[0], ops[1]);
                if (!c->tstk_bad) a = ity_meta(res);
                break;
            case IRW_CLMULLO: case IRW_CLMULHI:
                res = ity_join(ops[0], ops[1]);
                if (!c->tstk_bad) a = ity_meta(res);
                break;
            case IRW_SHL: case IRW_SHR: case IRW_WSHL: case IRW_WSHR:
            case IRW_ROTL: case IRW_ROTR:
                res = ops[0];
                if (!c->tstk_bad) a = ity_meta(res);
                break;
            case IRW_BNOT: case IRW_BSWAP:
                res = ops[0];
                if (!c->tstk_bad) a = ity_meta(res);
                break;
            case IRW_POPCNT: case IRW_CLZ: case IRW_CTZ:
                if (!c->tstk_bad) a = ity_meta(ops[0]);   // 폭은 **입력**의 것이다
                res = ITY_UNK;                            // 결과는 **개수**(작은 수)
                break;
            // ★★★ **abs 는 선언 폭을 알아야 한다** — abs(폭-MIN) = +2^(N-1) 은 그 폭에 안 맞는다
            //   (사용자 결정 2026-07-23: div 와 함께 트랩). 그런데 UNM 의 `a` 는 **op 선택자**(0~6)라
            //   폭 meta 와 자리가 겹친다 ⇒ 선택자는 **하위 4비트**에 그대로 두고 폭은 **상위 비트**에
            //   싣는다: bit21=폭 있음 · bit22=부호 · [24..31]=비트폭. 읽는 쪽은 전부 `& 0xf` 로
            //   선택자를 꺼낸다(sqrt/floor/ceil/sum/nonzero_of 가 안 깨지도록).
            case IRW_UNM:
                if ((a & 0xf) == 1 && !c->tstk_bad && ops[0].known && ops[0].bits && !ops[0].flt)
                    a |= 0x200000 | (ops[0].sign ? 0x400000 : 0)
                       | ((proven_i64)(ops[0].bits & 0xff) << 24);
                res = ITY_UNK;
                break;
            // ★★★★ **원소를 꺼내면 원소의 타입이 나온다** (2026-09-09, REQ-0013 / 결함
            //   `u64-index-comparison`). 여기가 `default` 로 떨어져 `index s 0` 의 결과가
            //   **타입 미상**이 됐고, 그러면 순서 비교가 부호 정보를 못 받아 **부호 있는 비교**로
            //   내려갔다 ⇒ `lt (index s 0) (index s 1)` 이 u64 의 큰 값(비트 63 이 선 값)을
            //   **음수처럼** 다뤄 **VM 과 네이티브가 사이좋게 틀린 답**을 냈다(둘 다 틀리므로
            //   차등 훑기도 못 잡는다 — 실험 heapsort 가 그것을 먼저 드러냈다).
            //   ⇒ 슬라이스의 그림자 타입(`ops[0]`)이 곧 원소의 타입이다. 그것을 그대로 민다.
            //   ☞ *«모르면 부호 있는 것으로 친다» 는 기본값이 조용한 오답을 만든다.*
            case IRW_INDEX:
                res = ops[0];
                break;
            // ★★★★★ **부른 op 의 결과도 타입이 있다** (결함 노트 #69, 2026-09-16).
            //   `fn top output u64 .` 을 부른 자리의 그림자가 «미상» 이라, 그 값을 쓰는
            //   `div`·`gt` 가 **부호 있는** 연산으로 낮아졌다: `div (top) 2 = 0` ·
            //   `gt (top) 9223372036854775807 = false`(VM·네이티브 같이 틀렸다).
            //   선언된 출력 타입이 곧 답이다 — 표에 이미 있다(`out_tyname`).
            case IRW_CALL:
                if (c->out) {
                    proven_size_t ci_ = (proven_size_t)IR_CALL_IDX(a);
                    if (ci_ < c->out->ndefs) res = ity_of_word(c->out->defs[ci_].out_tyname);
                }
                break;
            default:
                res = ITY_UNK;
                break;
        }
        if (ar > 0 || w == IRW_CONST || w == IRW_LOAD || w == IRW_FCONST ||
            w == IRW_STR || w == IRW_REF || w == IRW_MREF || w == IRW_WRAP_ERR)
            ir_tpush(c, res);
        else ir_tpush(c, res);
    } else {
        c->tsp = 0;   // 문장 경계 — 스택이 비는 지점
    }
    // ★ 자리표는 **문장의 것**이다: `c->cur_form` 이 이미 지금 낮추는 문장을 가리킨다
    //   (수리 id 를 고르려고 두었던 칸이 여기서 두 번째 쓸모를 얻는다).
    proven_u32 ln_ = c->cur_form ? c->cur_form->line : 0;
    proven_u16 fid_ = 0;
    if (c->cur_form && c->cur_form->file && c->out) {
        low_ir_t *o_ = c->out;
        for (proven_size_t q = 1; q < o_->nfiles; q++)
            if (o_->files[q] == c->cur_form->file) { fid_ = (proven_u16)q; break; }
        if (!fid_) {
            if (!o_->nfiles) { o_->files[0] = ""; o_->nfiles = 1; }
            if (o_->nfiles < 64) { o_->files[o_->nfiles] = c->cur_form->file;
                                   fid_ = (proven_u16)o_->nfiles++; }
        }
    }
    low_ir_ins_t ins = { .w = w, .a = a, .line = ln_, .fileid = fid_ };
    (void)proven_array_push(&c->code, &ins);
    return c->code.len - 1;
}
 low_ir_ins_t *ir_at(ir_ctx_t *c, proven_size_t i) { return (low_ir_ins_t *)proven_array_get(&c->code, i); }

// ★ X-0065 — 받는 자리에 쓰기 **전에** 찼는지 본다: `idx < len(out)` 이 아니면 «받는 자리가 찼다» 로 멈춘다.
//   전엔 일반 경계 검사(`slice index out of bounds on write`)가 멈춰, 무엇이 넘쳤는지 말하지 않았다.
void ir_emit_sinkfull_guard(ir_ctx_t *c, proven_size_t out_local, proven_size_t idx_local) {
    ir_emit(c, IRW_LOAD, (proven_i64)idx_local); ir_emit(c, IRW_LOAD, (proven_i64)out_local);
    ir_emit(c, IRW_LEN, 0); ir_emit(c, IRW_LT, (proven_i64)(IR_TY_KNOWN | 64));   // ★ u64 첨자 — 분석이 `j < len out` 을 세운다
    proven_size_t full = ir_emit(c, IRW_BRZ, 0);
    proven_size_t ok = ir_emit(c, IRW_BR, 0);
    ir_at(c, full)->a = (proven_i64)c->code.len;
    ir_emit(c, IRW_PANIC, IR_PANIC_SINKFULL);
    ir_at(c, ok)->a = (proven_i64)c->code.len;
}

// ★ 이 이름이 **actor 의 상태 필드**인가. 그렇다면 그것은 지역이 아니라 **인스턴스의 필드**다
//   (슬롯 0 = 인스턴스). 그래서 `value` 는 `field self value` 로 낮춰진다.
 bool ir_is_sfield(const ir_ctx_t *c, proven_u8str_view_t name) {
    for (proven_size_t i = 0; i < c->nsfield; i++)
        if (proven_u8str_view_eq(c->sfield[i], name)) return true;
    return false;
}
 proven_size_t ir_local_find(ir_ctx_t *c, proven_u8str_view_t name, bool *found) {
    for (proven_size_t i = c->nlocals; i-- > 0; )
        if (proven_u8str_view_eq(c->locals[i].name, name)) { *found = true; return i; }
    *found = false; return 0;
}
// 스칼라 타입 낱말 → (폭, 부호). 그 외(참조·슬라이스·구조체·부동)는 미상.
static ityp_t ity_of_word(proven_u8str_view_t v) {
    if (veq(v, "u8"))    return (ityp_t){ true, 8, false, false, false, 0, 0 };
    if (veq(v, "i8"))    return (ityp_t){ true, 8, true, false, false, 0, 0 };
    if (veq(v, "u16"))   return (ityp_t){ true, 16, false, false, false, 0, 0 };
    if (veq(v, "i16"))   return (ityp_t){ true, 16, true, false, false, 0, 0 };
    if (veq(v, "u32"))   return (ityp_t){ true, 32, false, false, false, 0, 0 };
    if (veq(v, "i32"))   return (ityp_t){ true, 32, true, false, false, 0, 0 };
    if (veq(v, "u64"))   return (ityp_t){ true, 64, false, false, false, 0, 0 };
    if (veq(v, "i64"))   return (ityp_t){ true, 64, true, false, false, 0, 0 };
    if (veq(v, "usize")) return (ityp_t){ true, 64, false, false, false, 0, 0 };
    if (veq(v, "isize")) return (ityp_t){ true, 64, true, false, false, 0, 0 };
    if (veq(v, "f32"))   return (ityp_t){ true, 32, false, true, false, 0, 0 };   // D9: f32 는 f32 정밀도
    if (veq(v, "f64"))   return (ityp_t){ true, 64, false, true, false, 0, 0 };
    return ITY_UNK;
}
// 선언의 타입 낱말들에서 스칼라 타입을 뽑는다. `mut_ref`/`ref` 가 앞에 오면 미상(참조).
/* ity_lo — low_ir_priv.h */
/* ity_hi — low_ir_priv.h */
 bool ir_int_lit(proven_u8str_view_t v, proven_i64 *out);
// RFC-0055 D1: 범위에서 **가장 싼 폭을 유도한다**(프로그래머가 폭을 고르지 않는다).
static ityp_t ity_of_range(proven_i64 lo, proven_i64 hi) {
    bool sign = lo < 0;
    proven_u8 bits = 64;
    for (proven_u8 b = 8; b <= 64; b = (proven_u8)(b * 2))
        if (lo >= ity_lo(b, sign) && hi <= ity_hi(b, sign)) { bits = b; break; }
    return (ityp_t){ true, bits, sign, false, true, lo, hi };
}
typedef struct ir_ctx_s ir_ctx_t_fwd;
 ityp_t ity_alias_lookup(const void *cv, proven_u8str_view_t w);
 ityp_t ity_of_decl_c(const void *cv, const low_cst_t *f, proven_size_t start, proven_size_t end);
 ityp_t ity_of_decl_c(const void *cv, const low_cst_t *f, proven_size_t start, proven_size_t end) {
    for (proven_size_t i = start; i < end && i < f->nkids; i++) {
        if (!is_atom(f->kids[i])) continue;
        proven_u8str_view_t w = f->kids[i]->tok.lex;
        if (veq(w, "ref") || veq(w, "mut_ref") || veq(w, "slice") || veq(w, "array") ||
            veq(w, "vec") || veq(w, "mask")) return ITY_UNK;
        // ★★★ **`unsafe_ptr <scalar>` = 생 포인터 = 기계어 한 칸(u64)** (RFC-0068 S4). `cstr` 는
        //   `newtype cstr unsafe_ptr u8` 이고, 그 런타임 표현은 **u8 이 아니라 u8 을 가리키는 포인터**다.
        //   포인터-크기 스칼라로 등록해야 값이 흐른다(안 하면 미상 타입 — 검사가 안 붙고 흐름이 깬다).
        //   ☞ `unsafe_ptr <struct>`(구조체 by-pointer 파라미터)는 여기 아니라 param_uptr 가 다룬다 —
        //     그래서 **다음 낱말이 스칼라 정수일 때만** 포인터로 본다.
        if (veq(w, "unsafe_ptr")) {
            if (i + 1 < end && i + 1 < f->nkids && is_atom(f->kids[i + 1])
                && ity_of_word(f->kids[i + 1]->tok.lex).known)
                return (ityp_t){ true, 64, false, false, false, 0, 0 };
            return ITY_UNK;
        }
        // ★★★ **`bits n` — 임의 비트폭 정수** (RFC-0002 §116). `type tenbit bits 10 .` 처럼 쓴다.
        //   폭이 2의 거듭제곱일 필요가 없다 — 산술은 **n 비트로 감긴다**(기존 폭 마스킹이 그대로 한다:
        //   u8 이 200+100 을 44 로 감듯 10비트는 mod 1024). 저장은 그 폭을 담는 가장 작은 기계 정수다.
        //   ★ SPEC §130 이 말한 대로 **빠름은 미보장**이다(비정렬·비2^n 배치는 비용이 다를 수 있다) —
        //     정확함만 보장한다. 그것을 문서와 게이트에 적는다(안 적으면 그것이 검사되지 않는 약속이다).
        if (veq(w, "bits")) {
            proven_i64 n = 0;
            if (i + 1 < end && i + 1 < f->nkids && is_atom(f->kids[i + 1]) &&
                ir_int_lit(f->kids[i + 1]->tok.lex, &n) && n >= 1 && n <= 64)
                return (ityp_t){ true, (proven_u8)n, false, false, false, 0, 0 };
            return ITY_UNK;
        }
        if (veq(w, "range")) {                        // ★ `range [τ] LO HI` — 타입이 곧 계약
            proven_size_t j = i + 1;
            ityp_t base = ITY_UNK;                    // RFC-0055 D7: τ 를 적으면 표현이 못 박힌다
            if (j < end && j < f->nkids && is_atom(f->kids[j])) {
                ityp_t b = ity_of_word(f->kids[j]->tok.lex);
                if (b.known && !b.flt) { base = b; j++; }
            }
            proven_i64 lo, hi;
            if (j + 1 < end && j + 1 < f->nkids && is_atom(f->kids[j]) && is_atom(f->kids[j + 1]) &&
                ir_int_lit(f->kids[j]->tok.lex, &lo) && ir_int_lit(f->kids[j + 1]->tok.lex, &hi) &&
                lo <= hi) {
                if (!base.known) return ity_of_range(lo, hi);          // τ 생략 → 가장 싼 폭 유도
                base.has_rng = true; base.rlo = lo; base.rhi = hi;     // τ 명시 → 그 표현 그대로
                return base;                                            // ([lo,hi] ⊆ τ 는 타입체커가 강제)
            }
            return ITY_UNK;
        }
        ityp_t t = ity_of_word(w);
        if (t.known) return t;
        t = ity_alias_lookup(cv, w);              // ★ 스칼라 타입 별칭 (`type h u8 .`)
        if (t.known) return t;
    }
    return ITY_UNK;
}

 ityp_t ity_alias_lookup(const void *cv, proven_u8str_view_t w) {
    if (!cv) return ITY_UNK;
    const ir_ctx_t *c = (const ir_ctx_t *)cv;
    for (proven_size_t i = 0; i < c->nty_alias; i++)
        if (proven_u8str_view_eq(c->ty_alias[i], w)) return c->ty_alias_ty[i];
    return ITY_UNK;
}

// ★ 지금 열려 있는 어휘 region 블록들의 이름 — `alloc_bytes <이름>` 이 이것을 본다.
/* IR_MAXREGION — low_ir_priv.h */
 proven_u8str_view_t g_rgnames[IR_MAXREGION];
 proven_size_t       g_nrg;
 proven_u8           g_rgroot[IR_MAXREGION];
 proven_size_t       g_rgslot[IR_MAXREGION];
const low_cst_t     *g_relform[IR_MAXREL];     // ★ RFC-0135 S2
proven_size_t        g_relslot[IR_MAXREL];
proven_size_t        g_nrel;
proven_u8str_view_t  g_relname[IR_MAXREL];
proven_size_t        g_using_ov = (proven_size_t)-1;
const low_cst_t     *g_relsub_atom;
proven_size_t        g_relsub_slot;
static bool ir_is_region_name(ir_ctx_t *c, proven_u8str_view_t n) {
    (void)c;
    for (proven_size_t i = 0; i < g_nrg; i++)
        if (proven_u8str_view_eq(g_rgnames[i], n)) return true;
    return false;
}
// ★ 그 이름의 영역이 깎는 뿌리 — **가장 안쪽** 같은 이름부터 본다(가림과 같은 차례).
static proven_u8 ir_region_root(proven_u8str_view_t n) {
    for (proven_size_t i = g_nrg; i-- > 0; )
        if (proven_u8str_view_eq(g_rgnames[i], n)) return g_rgroot[i];
    return 0;
}

 proven_size_t ir_local_declare(ir_ctx_t *c, proven_u8str_view_t name, proven_u32 line) {
    bool f; proven_size_t i = ir_local_find(c, name, &f);
    if (f) return i;
    if (c->nlocals >= IR_MAXLOCALS) { ir_fail(c, "E-IR-LOCALS", "too many locals", line); return 0; }
    c->locals[c->nlocals].name = name;
    c->locals[c->nlocals].bset_w = 0;   // ★ 기본은 '비트셋 아님' — 바인딩이 폭을 실어 준다
    c->locals[c->nlocals].narrowed = (proven_u8str_view_t){0};   // ★ 새 지역은 안 좁혀짐(RFC-0080 §4.6)
    return c->nlocals++;
}

// builtin prefix words (fixed arity)
typedef struct { const char *name; low_irw_t w; proven_size_t arity; } ir_builtin_t;
static const ir_builtin_t IR_BUILTINS[] = {
    // ★ 표는 **하나**다 (low_arity.h). 전엔 여기에만 있었고, 그래서 정규화 층이 나무를
    //   세우려면 arity 표를 **또 하나** 만들어야 했다 — 그것이 곧 여덟 번째 갈림이다.
#define X(n, w, a) { #n, w, a },
    LOW_BUILTINS(X)
#undef X
};
static const ir_builtin_t *ir_builtin(proven_u8str_view_t name) {
    for (proven_size_t i = 0; i < sizeof IR_BUILTINS / sizeof IR_BUILTINS[0]; i++)
        if (veq(name, IR_BUILTINS[i].name)) return &IR_BUILTINS[i];
    return NULL;
}
// ★★★★★ **자리에 가둔 이름** (RFC-0125, 2026-09-24) — 계산 잎 열다섯은 `call_builtin` 뒤에서만 선다.
//   `ir_builtin` 은 합집합을 보므로 arity·IR 낱말 조회는 전과 같다. 달라지는 것은 **이 술어**를
//   보고 «맨몸으로 왔나» 를 가르는 자리 하나뿐이다.
static const ir_builtin_t CB_BUILTINS[] = {
#define X(n, w, a) { #n, w, a },
    LOW_CALL_BUILTIN(X)
#undef X
};
static const ir_builtin_t *ir_call_builtin(proven_u8str_view_t name) {
    for (proven_size_t i = 0; i < sizeof CB_BUILTINS / sizeof CB_BUILTINS[0]; i++)
        if (veq(name, CB_BUILTINS[i].name)) return &CB_BUILTINS[i];
    return NULL;
}
// ★ RFC-0030 D2′ — 이 def 가 받은 `cap <kind>` 파라미터의 이름(없으면 빈 뷰).
//   `args` 로 시작했고 `env` 가 **같은 길을 그대로** 따라온다 — cap 이름만 다르다.
// ★★★★★ **권위는 종류로 온다 — 그리고 그 표는 하나여야 한다** (RFC-0077 §P1-2, 2026-08-14).
//   호스트 잎이 어떤 종류의 권한을 요구하는지는 여태 **하강 안에만** 있었다(TOPS·CLOPS·FOPS·
//   NOPS·…). 그래서 검사기는 그것을 모르고, 잘못된 종류를 든 프로그램이 `--check` 를
//   **통과했다** — 거절은 하강에서만 났고 진단은 `E-IR-UNSUP`("지원 안 함")이었다.
//   ⇒ **권위 문제가 미구현 문제로 보고되고 있었다.** 그 둘은 다른 것이다: 하나는 *"이 기능이
//     아직 없다"* 이고 다른 하나는 *"당신은 그럴 권리가 없다"* 다.
//   ★ 표를 여기 한 곳에 두고 검사기가 **빌려 쓴다**(두 벌이면 갈린다 — 이 저장소의 규율).
const char *low_ir_leaf_cap_kind(proven_u8str_view_t name) {
    static const struct { const char *w; const char *kind; } LEAF_CAP[] = {
        { "tty_raw", "tty" }, { "tty_read", "tty" }, { "tty_size", "tty" },
        { "time_now", "clock" }, { "time_sleep", "clock" }, { "time_local", "clock" },
        { "file_open", "file_system" }, { "file_read", "file_system" },
        { "file_write", "file_system" }, { "file_close", "file_system" },
        { "file_seek", "file_system" }, { "file_type", "file_system" }, { "link_type", "file_system" },
        { "dir_open", "file_system" }, { "dir_read", "file_system" }, { "dir_close", "file_system" },
        { "dir_make", "file_system" }, { "path_remove", "file_system" }, { "path_rename", "file_system" },
        { "net_pair", "net" }, { "net_send", "net" }, { "net_recv", "net" }, { "net_close", "net" },
        { "net_listen", "net" }, { "net_port", "net" }, { "net_connect", "net" }, { "net_accept", "net" },
        { "net_resolve", "net" },
        { "proc_spawn", "process" }, { "proc_read", "process" }, { "proc_poll", "process" },
        { "proc_wait", "process" }, { "proc_kill", "process" },
        { "random_bytes", "random" },
        { "env_get", "env" },
        { "alloc_bytes", "allocator" },
    };
    for (size_t i = 0; i < sizeof LEAF_CAP / sizeof LEAF_CAP[0]; i++)
        if (veq(name, LEAF_CAP[i].w)) return LEAF_CAP[i].kind;
    return 0;
}
static proven_u8str_view_t ir_capname_of(const low_cst_t *def_form, const char *kind) {
    proven_u8str_view_t none = { 0 };
    if (!def_form) return none;
    low_op_header_t h = low_op_header(def_form);
    for (proven_size_t q = 0; q < h.np; q++) {
        proven_size_t cw = h.p[q].core;
        if (cw + 1 < h.p[q].te && cw + 1 < def_form->nkids &&
            is_atom(def_form->kids[cw]) && is_atom(def_form->kids[cw + 1]) &&
            veq(def_form->kids[cw]->tok.lex, "cap") &&
            veq(def_form->kids[cw + 1]->tok.lex, kind))
            return h.p[q].name;
    }
    return none;
}
// ★★★ WO-0221 — 첫 피연산자가 **그 종류의 권한 입력 가운데 어느 것이든** 이름하면 된다(RFC-0112 D5(4) 와 같은 규칙).
//   전엔 그 종류의 **첫** 권한 이름과만 대조해서, 같은 종류의 둘째 권한을 대면 «권한이 없다»(`E-CAP-MISSING`)로 오진했다.
static bool ir_names_cap(const low_cst_t *def_form, proven_u8str_view_t name, const char *kind) {
    if (!def_form) return false;
    low_op_header_t h = low_op_header(def_form);
    for (proven_size_t q = 0; q < h.np; q++) {
        if (!proven_u8str_view_eq(h.p[q].name, name)) continue;
        proven_size_t cw = h.p[q].core;
        return cw + 1 < h.p[q].te && cw + 1 < def_form->nkids &&
               is_atom(def_form->kids[cw]) && is_atom(def_form->kids[cw + 1]) &&
               veq(def_form->kids[cw]->tok.lex, "cap") && veq(def_form->kids[cw + 1]->tok.lex, kind);
    }
    return false;
}
// ★★★ **«권한이 없다» 는 원인을 말해야 한다** (결함 노트 #50, 2026-09-16).
//   여태 잎이 첫 피연산자에서 권한을 못 찾으면 무조건 `E-CAP-MISSING`(«entry 가 받은 값을
//   첫 피연산자로 대라»)이었다. 그런데 실제로 흔한 두 원인은 다른 것이다:
//     ① 받은 권한을 **지역 이름에 옮겨 담았다**(`let k be cap io out .`) — 그 이름은
//        서명에 없으므로 추적이 끊긴다. 권한은 값처럼 복사해 다니는 것이 아니다.
//     ② **다른 종류**의 권한을 댔다(`write_out` 에 `cap file_system`) — 없는 것이 아니라
//        맞지 않는 것이다(`E-CAP-KIND`).
//   둘 다 "entry 가 첫 피연산자로 받았다" 는 문장으로는 **원인이 안 보인다**.
static bool ir_local_cap_kind(const low_cst_t *nd, proven_u8str_view_t name,
                              proven_u8str_view_t *kind) {
    if (!nd) return false;
    if (nd->kind == LOW_CST_FORM && nd->nkids >= 4 && is_atom(nd->kids[0]) &&
        (nd->kids[0]->tok.kw == LOW_KW_LET || nd->kids[0]->tok.kw == LOW_KW_VAR) &&
        is_atom(nd->kids[1]) && proven_u8str_view_eq(nd->kids[1]->tok.lex, name) &&
        is_atom(nd->kids[2]) && veq(nd->kids[2]->tok.lex, "cap") && is_atom(nd->kids[3])) {
        *kind = nd->kids[3]->tok.lex; return true;
    }
    for (proven_size_t i = 0; i < nd->nkids; i++)
        if (ir_local_cap_kind(nd->kids[i], name, kind)) return true;
    return false;
}
static bool ir_param_cap_kind(const low_cst_t *def_form, proven_u8str_view_t name,
                              proven_u8str_view_t *kind) {
    if (!def_form) return false;
    low_op_header_t h = low_op_header(def_form);
    for (proven_size_t q = 0; q < h.np; q++) {
        if (!proven_u8str_view_eq(h.p[q].name, name)) continue;
        proven_size_t cw = h.p[q].core;
        if (cw + 1 < h.p[q].te && cw + 1 < def_form->nkids &&
            is_atom(def_form->kids[cw]) && is_atom(def_form->kids[cw + 1]) &&
            veq(def_form->kids[cw]->tok.lex, "cap")) { *kind = def_form->kids[cw + 1]->tok.lex; return true; }
        return false;
    }
    return false;
}
// 원인을 알면 그것을 말하고 true 를 준다 — 그러면 부르는 쪽은 일반 문구를 내지 않는다.
static bool ir_cap_cause(ir_ctx_t *c, const char *kind, low_cst_t *const *k,
                         const proven_size_t *pos, proven_size_t end, proven_u32 line) {
    if (!(*pos < end && is_atom(k[*pos]))) return false;
    proven_u8str_view_t nm = k[*pos]->tok.lex, got = { 0 };
    char buf[256];
    if (ir_param_cap_kind(c->def_form, nm, &got)) {
        if (veq(got, kind)) return false;                 // 종류가 맞다 — 다른 이유다
        snprintf(buf, sizeof buf,
                 "this leaf needs a `cap %s`, and `%.*s` is a `cap %.*s`. One capability never "
                 "stands in for another: each names a different right, and holding one says "
                 "nothing about the other (RFC-0011)",
                 kind, (int)nm.size, (const char *)nm.ptr, (int)got.size, (const char *)got.ptr);
        ir_fail_buf(c, "E-CAP-KIND", buf, line);
        return true;
    }
    if (ir_local_cap_kind(c->def_form, nm, &got)) {
        snprintf(buf, sizeof buf,
                 "`%.*s` is a LOCAL that a `cap %.*s` was copied into. A capability is not a value "
                 "you carry in a local name: only a parameter of this op names a right it was "
                 "handed, so hand the parameter itself to the leaf (RFC-0030 D2)",
                 (int)nm.size, (const char *)nm.ptr, (int)got.size, (const char *)got.ptr);
        ir_fail_buf(c, "E-CAP-LOCAL", buf, line);
        return true;
    }
    return false;
}
// (구판 이름 유지 — 호출부가 읽기 쉽다)
//   권한은 여기서 갈린다: `count`/`arg` 는 이 이름을 첫 피연산자로 댈 때만 인자에 닿는다.
static proven_u8str_view_t ir_argscap_of(const low_cst_t *f) { return ir_capname_of(f, "args"); }
// ★★★ **문자열 리터럴의 이스케이프를 푼다** — 여태 **스캔만 하고 디코드를 안 했다.**
//   렉서는 `\\X` 를 만나면 두 글자를 건너뛴다(그래서 `\\"` 가 문자열을 안 끝낸다). 그런데
//   방출하는 값이 **원문 구간 그대로**라, `"a\\"b"` 가 4 바이트(a·\\·"·b)였다:
//   ⇒ **문자열 값에 따옴표를 넣을 방법이 아예 없었다.** 그리고 렉서 주석은
//     `with \\" and \\\\ escapes` 라고 **약속하고 있었다** — 검사되지 않는 약속은 거짓말이다(§0).
//   ★ 어휘는 **닫혀 있다**(7): \\\\ \\" \\n \\t \\r \\0 \\xNN. 모르는 이스케이프는 **거절한다** —
//     조용히 백슬래시를 남기면 그것이 다시 '반만 있는' 상태다.
//   ★ 디코드는 **줄이기만** 한다(2~4 글자 → 1 바이트)므로 원문 길이 예산이면 항상 넉넉하다.
// ★★★ **`\\xNN` 은 자리 수가 고정이다(정확히 두 자리)** — 그것이 이 설계의 요점이다.
//   C·C++ 의 `\\x` 는 16진수를 **끝없이** 먹어서 `"\\x41" "2"` 가 붙는 순간 다른 바이트가 된다.
//   Rust·Zig 는 그 실수를 **길이 고정**으로 닫았고(가변인 것은 중괄호를 쓰는 `\\u{…}` 뿐이다),
//   여기도 같은 규율을 쓴다: 두 자리가 아니면 **거절**한다. ⇒ 탐욕이 원리상 불가능하다.
//   ☞ Rust 의 `str` 은 UTF-8 이라 `\\x` 를 0x7F 까지로 막지만, 여기 문자열은 `slice u8` 이라
//     그 제한이 필요 없다(Zig 와 같은 자리) — 0x00~0xFF 를 전부 적을 수 있다.
//   ★ 이것이 없어서 `lib/term.low` 는 ESC 를 `set (index buf pos) 27 .` 로 **산술로** 썼다.
static proven_i32 ir_hexdig(proven_u8 ch) {
    if (ch >= '0' && ch <= '9') return (proven_i32)(ch - '0');
    if (ch >= 'a' && ch <= 'f') return (proven_i32)(ch - 'a' + 10);
    if (ch >= 'A' && ch <= 'F') return (proven_i32)(ch - 'A' + 10);
    return -1;
}
static bool ir_unescape(ir_ctx_t *c, proven_u8str_view_t raw, proven_u32 line,
                        proven_u8str_view_t *out) {
    low_ir_t *ir = c->out;
    if (ir->strbuf_len + raw.size > ir->strbuf_cap) {
        ir_fail(c, "E-IR-UNSUP", "too many bytes of string literal in one unit", line);
        return false;
    }
    proven_u8 *dst = ir->strbuf + ir->strbuf_len;
    proven_size_t n = 0;
    for (proven_size_t i = 0; i < raw.size; i++) {
        proven_u8 ch = raw.ptr[i];
        if (ch != '\\') { dst[n++] = ch; continue; }
        if (i + 1 >= raw.size) {
            ir_fail(c, "E-STR-ESCAPE", "a string ends with a lone backslash — an escape needs "
                    "the character it escapes", line);
            return false;
        }
        proven_u8 e = raw.ptr[++i];
        switch (e) {
            case '\\': dst[n++] = '\\'; break;
            case '"':  dst[n++] = '"';  break;
            // ★★★ **작은따옴표** (2026-08-25 · 소유자 결정). 이것이 없어서 `'\\''` 가 거부됐고,
            //   정본은 `CHAR` 줄에서 허용한다 적고 `escape` 줄에서는 안 적어 **자기 안에서
            //   갈려 있었다**. 더하는 것으로 그 갈림을 닫는다.
            case '\'': dst[n++] = '\''; break;
            case 'n':  dst[n++] = '\n'; break;
            case 't':  dst[n++] = '\t'; break;
            case 'r':  dst[n++] = '\r'; break;
            // ★★ C 의 남은 단순 이스케이프 넷. 제어 문자를 `\x07` 로만 적게 하면 읽는 사람이
            //   표를 찾아봐야 한다 — 이름이 있는 것에는 이름을 준다.
            case 'a':  dst[n++] = 0x07; break;   // 벨
            case 'b':  dst[n++] = 0x08; break;   // 백스페이스
            case 'f':  dst[n++] = 0x0C; break;   // 폼피드
            case 'v':  dst[n++] = 0x0B; break;   // 수직 탭
            case '0':  dst[n++] = 0;    break;
            // ★★★★ **유니버설 문자 이름** `\uXXXX` · `\UXXXXXXXX` (2026-08-25 · 소유자 결정).
            //   ☞ 코드포인트를 **UTF-8 바이트로** 푼다. 그러면 접두사 재인코딩(`u`·`U`)이
            //     그대로 맞물린다 — 이스케이프가 인코딩을 몰라도 된다.
            //   ★ 자리 수는 **고정**이다(4 · 8). `\xNN` 을 두 자리로 고정한 것과 같은 이유다:
            //     가변 길이면 리터럴 옆에 십육진 글자 하나가 붙는 것만으로 값이 바뀐다.
            //   ★★ 서러게이트 자리(D800~DFFF)와 0x10FFFF 초과는 **거부**한다. 그것들은
            //     코드포인트가 아니고, 조용히 받으면 그 리터럴이 거짓말이 된다.
            case 'u':
            case 'U': {
                proven_size_t want = (e == 'u') ? 4 : 8;
                if (i + want >= raw.size) {
                    ir_fail(c, "E-STR-ESCAPE",
                            "`\\u` needs EXACTLY four hex digits and `\\U` exactly eight — this one "
                            "runs off the end of the literal. The width is fixed on purpose, the "
                            "same reason `\\xNN` is: a greedy escape changes meaning when another "
                            "hex digit is written next to it", line);
                    return false;
                }
                proven_u32 cp = 0;
                for (proven_size_t q = 1; q <= want; q++) {
                    proven_i32 d = ir_hexdig(raw.ptr[i + q]);
                    if (d < 0) {
                        ir_fail(c, "E-STR-ESCAPE",
                                "`\\u` needs EXACTLY four hex digits and `\\U` exactly eight — "
                                "0-9 a-f A-F, no more and no fewer. Pad with leading zeros "
                                "(\\u00e9, not \\ue9)", line);
                        return false;
                    }
                    cp = (cp << 4) | (proven_u32)d;
                }
                i += want;
                if ((cp >= 0xD800u && cp <= 0xDFFFu) || cp > 0x10FFFFu) {
                    ir_fail(c, "E-STR-ESCAPE",
                            "this is not a code point. The surrogate range (D800-DFFF) belongs to "
                            "UTF-16's encoding, not to Unicode's characters, and nothing above "
                            "10FFFF exists. Accepting it would put a byte sequence in your literal "
                            "that names no character", line);
                    return false;
                }
                // UTF-8 로 싣는다.
                if (cp < 0x80u) dst[n++] = (proven_u8)cp;
                else if (cp < 0x800u) {
                    dst[n++] = (proven_u8)(0xC0u | (cp >> 6));
                    dst[n++] = (proven_u8)(0x80u | (cp & 0x3Fu));
                } else if (cp < 0x10000u) {
                    dst[n++] = (proven_u8)(0xE0u | (cp >> 12));
                    dst[n++] = (proven_u8)(0x80u | ((cp >> 6) & 0x3Fu));
                    dst[n++] = (proven_u8)(0x80u | (cp & 0x3Fu));
                } else {
                    dst[n++] = (proven_u8)(0xF0u | (cp >> 18));
                    dst[n++] = (proven_u8)(0x80u | ((cp >> 12) & 0x3Fu));
                    dst[n++] = (proven_u8)(0x80u | ((cp >> 6) & 0x3Fu));
                    dst[n++] = (proven_u8)(0x80u | (cp & 0x3Fu));
                }
                break;
            }
            case 'x': {
                // ★ **정확히 두 자리** — 하나만 있어도, 세 자리째가 16진이어도 상관없다.
                //   더 먹지 않으므로 `"\xff" `+`"e"` 같은 이어붙임이 값을 바꾸지 못한다.
                if (i + 2 >= raw.size) {
                    ir_fail(c, "E-STR-ESCAPE",
                            "`\\x` needs EXACTLY two hex digits (\\x00 … \\xff) — this one runs "
                            "off the end of the string. The width is fixed on purpose: a greedy "
                            "\\x is how \"\\x41\" changes meaning when another hex digit is "
                            "appended next to it", line);
                    return false;
                }
                proven_i32 hi = ir_hexdig(raw.ptr[i + 1]);
                proven_i32 lo = ir_hexdig(raw.ptr[i + 2]);
                if (hi < 0 || lo < 0) {
                    ir_fail(c, "E-STR-ESCAPE",
                            "`\\x` needs EXACTLY two hex digits (\\x00 … \\xff) — 0-9 a-f A-F, "
                            "no more and no fewer. Write a one-digit byte with a leading zero "
                            "(\\x07, not \\x7)", line);
                    return false;
                }
                dst[n++] = (proven_u8)(hi * 16 + lo);
                i += 2;
                break;
            }
            default:
                ir_fail(c, "E-STR-ESCAPE",
                        "unknown escape in a string literal — the set is CLOSED: "
                        "\\\\ \\\" \\' \\a \\b \\f \\n \\r \\t \\v \\0 \\xNN \\uXXXX \\UXXXXXXXX. "
                        "(Octal escapes are NOT in it: this language has no octal at all, and "
                        "`0755` is 755.) (Refusing beats silently keeping the backslash: "
                        "that is how this literal came to hold a byte nobody wrote.) For arbitrary "
                        "bytes and multiple lines, use a text literal (`text TAG … TAG`) — its body is RAW", line);
                return false;
        }
    }
    *out = (proven_u8str_view_t){ .ptr = dst, .size = n };
    ir->strbuf_len += n;
    return true;
}
// ★★★ **접두 리터럴의 어휘는 닫힌 집합이다** (RFC-0035 D5 · C23 의 `u8"…"`·`u"…"`·`U"…"`).
//
//     접두사 없음 · u8  → 원소 1 바이트 (UTF-8) — `u8"…"` 은 `"…"` 과 **완전히 같다**
//     u                 → 원소 2 바이트 (UTF-16 코드 유닛, 비-BMP 는 서러게이트 쌍)
//     U                 → 원소 4 바이트 (UTF-32 = 코드포인트)
//
//   ★ 반환하는 것은 **원소폭**이지 인코딩 이름이 아니다 — 어휘가 둘로 갈리지 않게(교훈 7).
//   ★ 그 밖의 접두사는 **거절**한다. 이 자리가 RFC-0035 D5 의 사용자 처리기가 들어올 문이고,
//     그때까지 열어 두면 `hex"41"` 이 **조용히 이름 없는 무언가**가 된다.
static proven_i32 ir_str_prefix_width(proven_u8str_view_t p) {
    if (p.size == 0)   return 1;
    // ★★★ **`u8` 를 없앴다** (2026-08-25 · 소유자 결정). `u8"…"` 은 `"…"` 과 **완전히 같았다**
    //   — 곧 순수한 동의어이고, §2.5 는 동의어를 금지한다. `;` 를 지운 것과 **같은 논거**다.
    //   이 언어의 기본이 이미 u8 이므로 그 접두사는 아무것도 더 말하지 않았다.
    //   ⇒ 접두사는 둘뿐이다: `u`(UTF-16 코드 유닛) · `U`(코드포인트).
    if (veq(p, "u"))   return 2;
    if (veq(p, "U"))   return 4;
    return -1;
}

// UTF-8 바이트열을 폭 `w`(2·4)의 코드 유닛으로 다시 싣는다 — **리틀엔디언**으로.
//   ★★★ 바이트 순서를 내가 고르지 않는다. **런타임 규약이 이미 있다**: 타입 있는 슬라이스의
//     원소는 어느 호스트·어느 타깃에서도 리틀엔디언으로 담기고, VM 은
//     `x = (x<<8) | p[k]` 로, 네이티브는 `lw_ld_le` 로 **같은 방식으로** 읽는다.
//     ⇒ 리터럴도 그 규약을 따르면 VM ≡ native 가 저절로 성립하고, `mips_be` 도 문제가 아니다.
//   ☞ 처음엔 "호스트 순서로 담고 C 는 원소 타입 배열로 낸다" 로 지었다가 **VM ≢ native** 를
//     실측했다(네이티브가 94, VM 이 42). 규약이 이미 있는 자리에 새 규약을 지으면 갈린다.
static bool ir_widen_units(ir_ctx_t *c, proven_u8str_view_t utf8, proven_i32 w,
                           proven_u32 line, proven_u8str_view_t *out) {
    low_ir_t *ir = c->out;
    // 최악의 경우: 모든 바이트가 ASCII 한 글자 ⇒ 바이트마다 원소 하나.
    if (ir->strbuf_len + utf8.size * (proven_size_t)w > ir->strbuf_cap) {
        ir_fail(c, "E-IR-UNSUP", "too many bytes of string literal in one unit", line);
        return false;
    }
    proven_u8 *dst = ir->strbuf + ir->strbuf_len;
    proven_size_t n = 0;
    for (proven_size_t i = 0; i < utf8.size; ) {
        // ★ 2026-10-02 — 한 글자 풀기는 proven v0.6.0 의 `proven_utf8_decode_next`(엄격: overlong·surrogate·범위도 본다).
        //   전엔 손으로 풀어 `\xC0\x80` 같은 overlong 을 글자 0 으로 받아 주었다.
        proven_utf8_char_t uc = proven_utf8_decode_next(utf8, i);
        if (uc.err == PROVEN_ERR_NEED_MORE) {
            ir_fail(c, "E-STR-ESCAPE", "a multi-byte character is cut short in this literal", line);
            return false;
        }
        if (uc.err != PROVEN_OK) {
            // ★ 소스는 이미 well-formed UTF-8 이다(E-LEX-UTF8). 여기 닿는 유일한 길은
            //   `\xNN` 이 0x7F 위의 **생바이트**를 넣은 것이다 — 그 바이트는 문자가 아니다.
            proven_u8 b0 = utf8.ptr[i];
            bool lone = (b0 & 0xc0) == 0x80 || b0 == 0xc0 || b0 == 0xc1 || b0 >= 0xf5;   // 어떤 글자의 첫 바이트도 될 수 없다
            ir_fail(c, "E-STR-ESCAPE", lone
                    ? "`\\xNN` above \\x7f has no meaning inside a `u\"…\"` or `U\"…\"` literal: "
                      "those literals hold CHARACTERS (code units), and a lone byte above ASCII is "
                      "not a character. Write the character itself (the source is UTF-8), or use a "
                      "plain \"…\"/u8\"…\" literal where \\xNN names a BYTE"
                    : "`\\xNN` above \\x7f has no meaning inside a `u\"…\"` or `U\"…\"` literal "
                      "(it cut a character in half). Write the character itself, or use a plain "
                      "\"…\"/u8\"…\" literal where \\xNN names a BYTE", line);
            return false;
        }
        proven_u32 cp = uc.cp; proven_size_t need = uc.len;
        i += need;
        if (w == 4) {
            for (proven_size_t k = 0; k < 4; k++) dst[n + k] = (proven_u8)((cp >> (8 * k)) & 0xff);
            n += 4;
        } else if (cp >= 0x10000u) {
            // ★ 비-BMP = **서러게이트 쌍**. 여기서 조용히 자르면 `u"…"` 이 U+FFFD 나 잘린
            //   문자를 담게 된다 — UTF-16 이 원래 그렇게 생겼으므로 쌍으로 낸다.
            proven_u32 v = cp - 0x10000u;
            proven_u32 hi = 0xd800u + (v >> 10), lo = 0xdc00u + (v & 0x3ffu);
            proven_u32 pair[2] = { hi, lo };
            for (proven_size_t p = 0; p < 2; p++) {
                dst[n++] = (proven_u8)(pair[p] & 0xff);
                dst[n++] = (proven_u8)((pair[p] >> 8) & 0xff);
            }
        } else {
            dst[n++] = (proven_u8)(cp & 0xff);
            dst[n++] = (proven_u8)((cp >> 8) & 0xff);
        }
    }
    *out = (proven_u8str_view_t){ .ptr = dst, .size = n };
    ir->strbuf_len += n;
    return true;
}
// ★★ 이름이 **빌트인**과 겹치는가. 겹치면 그 선언은 **영원히 호출되지 않는다** —
//   해석기가 언제나 빌트인을 고른다. `fn count …` 이 컴파일되고, `count a` 는
//   **빌트인 count** 를 부른다. 사용자의 op 은 **존재하지만 존재하지 않는다.**
//   이름공간이 평면이므로(E-NAME-DUP · E-NAME-SHADOW) 이것도 같은 죄다: **조용히 다른 것을 고른다.**
bool low_ir_is_builtin_name(proven_u8str_view_t name) {
    if (ir_builtin(name)) return true;               // LOW_BUILTINS (add·gt·len·index …)
    // ★ 전엔 여기 **손으로 적은 목록(NB)** 이 있었고 **39 개 중 34 개가 arity 표와 중복**이었다.
    //   같은 어휘를 두 곳에 적으면 갈린다(교훈 7). 이제 표에서 읽는다 — arity 를 못 적는
    //   특수형과 파이프라인 스테이지도 **같은 헤더**에 산다(LOW_SPECIAL · LOW_STAGES).
    // ★★★ **LOW_SHAPES 도 여기 든다** (2026-07-22, 실측으로 세 번 아팠다): `sum`·`ok`·`min`·`max`·
    //   `view`·`cast`·`fold`·`map`·`filter` … 는 **전위 적용 빌트인**이라 지역/op 이름으로 쓰면
    //   해석기가 가로채 문장이 다르게 묶인다(`return sum .` → 빌트인 `sum` 이 피연산자를 찾는다 →
    //   "피연산자 부족" → 정규화기가 그 구간을 flat 하게 남긴다 = **조용한 태그-경로**). 전엔
    //   LOW_BUILTINS·LOW_SPECIAL 만 봐서 이 부류가 새어 나갔다(`gt` 는 잡히는데 `sum` 은 안 잡혔다).
    //   ⇒ **이제 잡는다.** ★ LOW_VOCAB_ONLY(pipe 스테이지 scan·take …)는 **여전히 제외** — 그것들은
    //     `pipe … do … end` **안에서만** 스테이지라, 밖의 사용자 이름과 안 부딪힌다(json.low 의 `scan`).
    static const char *NB[] = {
#define X(n) #n,
        LOW_SPECIAL(X)          /* ★ **예약 이름만**. 어휘 전체가 아니다(LOW_VOCAB_ONLY 는 제외) */
#undef X
    };
    for (proven_size_t i = 0; i < sizeof NB / sizeof NB[0]; i++) if (veq(name, NB[i])) return true;
    static const char *SH[] = {
#define X(n, s) #n,
        LOW_SHAPES(X)           /* ★ 전위 적용 shape 빌트인 — 지역/op 이름 금지 */
#undef X
    };
    for (proven_size_t i = 0; i < sizeof SH / sizeof SH[0]; i++) if (veq(name, SH[i])) return true;
    return false;
}
// ★ 구간 분석 스택이 넘쳐 **포기한** 횟수(RFC-0077 P1-5) — 조용하지 않게 하려고 센다.
 proven_u64 g_iv_stack_full = 0;
proven_u64 low_ir_iv_stack_full(void) { return g_iv_stack_full; }
proven_size_t ir_def_find(const low_ir_t *ir, proven_u8str_view_t name, bool *found) {
    for (proven_size_t i = 0; i < ir->ndefs; i++)
        if (proven_u8str_view_eq(ir->defs[i].name, name)) { *found = true; return i; }
    *found = false; return 0;
}
// ★★★★★ **맨이름은 제 모듈부터 찾는다** (2026-08-07).
//
//   `ir_def_find` 는 이름만 보고 **첫 정의**를 집는다. 한 모듈만 있을 때는 그것이 옳았다.
//   그러나 두 모듈이 같은 이름을 export 하면 — `utf8` 과 `utf16` 은 `decode`·`next`·
//   `count_chars`·`is_valid` 넷을 겹친다 — 자기 export 를 맨이름으로 부르는 자리가
//   **먼저 선언된 모듈**에 묶였다. 즉 **선언 순서가 프로그램의 뜻을 바꿨다**:
//
//       lowentc … utf8.low utf16.low conv.low   →  f() = 107   (utf16 이 깨진다)
//       lowentc … utf16.low utf8.low conv.low   →  f() = 106   (utf8 이 깨진다)
//
//   ☞ 그 전에는 `E-VISIBILITY` 가 이 자리를 **시끄럽게** 막고 있었다(맨이름으로 남의 모듈을
//     넘지 못한다). 그래서 결함은 *"평평한 이름의 한계"* 로 기록돼 있었다 — **막고 있던
//     오답이라 아무도 그 뒤를 못 봤다.** 검사만 풀면 조용한 오답이 된다: 둘 다 고쳐야 한다.
//   ★ 규칙은 한 줄이다: **제 모듈 안에서 제 이름은 제 것이다.** 없을 때만 밖을 본다.
 proven_size_t ir_def_find_in(ir_ctx_t *c, proven_u8str_view_t name, bool *found) {
    const low_ir_t *ir = c->out;
    if (c->def_owner.size) {
        for (proven_size_t i = 0; i < ir->ndefs; i++)
            if (proven_u8str_view_eq(ir->defs[i].name, name) &&
                ir->defs[i].owner_mod.size &&
                proven_u8str_view_eq(ir->defs[i].owner_mod, c->def_owner)) { *found = true; return i; }
    }
    return ir_def_find(ir, name, found);
}
// ★★★★★ **검사기가 좁힌 한정을 하강이 되찾는다** (알려진 결함 qualifier-erased-flat-lookup,
//   2026-08-11 발견 → 2026-08-14 고침).
//
//   `ck_narrow_qual` 은 `M.member` 를 bare `member` 로 **파괴적으로** 좁힌다 — 뒤의 검사기들이
//   bare 를 보게 하려고(RFC-0011). 그런데 **하강은 같은 CST 를 읽는다**: 그래서 아래
//   `ir_modqual_find`(모듈 안에서 찾는 옳은 조회)가 볼 것이 없어졌고, `ir_def_find_in` 이
//   이름만으로 **첫 정의**를 집었다. 두 모듈이 같은 이름을 가지면 **파일 순서가 뜻을 바꿨다**:
//
//       lowentc --check caller.low poly.low chacha.low   →  ok
//       lowentc --check caller.low chacha.low poly.low   →  E-TYPE-ARGMUT / E-IR-ARITY (틀림)
//
//   ⇒ 좁히는 쪽이 **가리킨 모듈을 `nd->qual_mod` 에 남기고**(low_cst.h), 여기서 그것을 읽는다.
//   ★ 되돌리기(검사 뒤 토큰 복원)로도 고칠 수 있었지만 **계약 검사가 bare 를 기대해** 깨졌다
//     (E-REQ-UNDEF, 실측) — 뒤끝을 바꾸는 편이 앞을 되돌리는 것보다 좁고 안전하다.
static proven_size_t ir_def_find_q(ir_ctx_t *c, const low_cst_t *nd, bool *found) {
    if (nd->qual_mod.size) {
        for (proven_size_t i = 0; i < c->out->ndefs; i++)
            if (proven_u8str_view_eq(c->out->defs[i].name, nd->tok.lex) &&
                c->out->defs[i].owner_mod.size &&
                proven_u8str_view_eq(c->out->defs[i].owner_mod, nd->qual_mod)) {
                *found = true; return i;
            }
    }
    return ir_def_find_in(c, nd->tok.lex, found);
}
// ★ 모듈 한정 op 참조 `X.op` (RFC-0011): 점 정확히 하나·양쪽 비지 않음·head 가 **지역이 아니면**
//   (필드 접근 아님) 뒤 마디를 op 이름으로 본다. X 가 진짜 import 모듈이고 op 을 export 하는지는
//   검사층(ck_visibility) 소관 — 여기선 해소만. Type.op(def 이름에 점 포함)은 full-name 조회가 먼저 잡는다.
static bool ir_modqual_tail(ir_ctx_t *c, proven_u8str_view_t v, proven_u8str_view_t *tail) {
    proven_size_t dot = v.size;
    for (proven_size_t i = 0; i < v.size; i++)
        if (v.ptr[i] == (proven_u8)'.') { if (dot != v.size) return false; dot = i; }  // 점 하나만
    if (dot == v.size || dot == 0 || dot + 1 >= v.size) return false;
    proven_u8str_view_t head = { .ptr = v.ptr, .size = dot };
    bool lf; ir_local_find(c, head, &lf);
    if (lf) return false;   // 지역이면 필드 접근이지 모듈 한정이 아니다
    *tail = (proven_u8str_view_t){ .ptr = v.ptr + dot + 1, .size = v.size - dot - 1 };
    return true;
}
// ★★★ **한정자를 쓰이게 한다** (RFC-0060 재개 · 단계 U 슬라이스 ②).
//
//   `Y.op` 에서 Y 를 **떼고 맨이름으로** 찾던 것을, Y 가 가리키는 **모듈 안에서** 찾는다.
//   찾으면 그 def 이 답이다 — 같은 이름의 op 이 다른 모듈에 또 있어도 상관없다. 이것이
//   *"공존 불가 27 쌍"* 을 푸는 열쇠이고, 한정자가 장식이 아니게 되는 지점이다.
//
//   ★ **못 찾으면 맨이름으로 되돌아간다.** 아직 슬라이스 ③(E-NAME-DUP 완화)이 안 왔고,
//     `Type.op`(def 이름에 점이 든 것)·모듈 선언이 없는 단일 파일 등 여태 굴러온 자리가
//     있기 때문이다. **되돌아가기를 남겨 두는 동안에는 이 변경이 순수한 확장**이다 —
//     전에 풀리던 것은 전부 그대로 풀린다.
static proven_size_t ir_modqual_find(ir_ctx_t *c, proven_u8str_view_t v, bool *found) {
    proven_u8str_view_t tail;
    *found = false;
    if (!ir_modqual_tail(c, v, &tail)) return 0;
    proven_size_t dot = 0;
    while (dot < v.size && v.ptr[dot] != (proven_u8)'.') dot++;
    proven_u8str_view_t head = { .ptr = v.ptr, .size = dot };
    // 별칭 → 모듈 (없으면 머리 자신을 모듈 이름으로 본다)
    proven_u8str_view_t m = head;
    for (proven_size_t a = 0; a < c->nusebind; a++)
        if (proven_u8str_view_eq(head, c->usebind[a])) { m = c->usemod[a]; break; }
    for (proven_size_t i = 0; i < c->out->ndefs; i++)
        if (proven_u8str_view_eq(c->out->defs[i].name, tail) &&
            c->out->defs[i].owner_mod.size &&
            proven_u8str_view_eq(c->out->defs[i].owner_mod, m)) { *found = true; return i; }
    return ir_def_find_in(c, tail, found);   // ← 되돌아가기(위 ★)
}
// ★★★ **맨이름이 두 모듈에 걸리면 그 자리는 결정되지 않는다** (단계 U 슬라이스 ③).
//
//   슬라이스 ②가 한정자를 쓰이게 했으므로 **다른 모듈의 같은 이름**은 이제 허용된다.
//   그러나 **가르지 않은 자리**는 여전히 "조용히 하나를 고르는" 자리다 — 규칙이 지키던 것은
//   *이름의 유일성*이 아니라 **참조가 유일하게 결정되는가** 였다.
//
//   ★ **이 판정은 해소기의 일이다.** 처음엔 검사층에서 CST 를 훑어 *"점이 있으면 한정"* 으로
//     가리려 했는데, 그 원자의 낱말이 이미 `pick` 이었다(파싱 뒤 어느 패스가 뷰를 **제자리에서**
//     좁힌다 — `lex.ptr` 이 `qa.pick` 안쪽을 가리키고 앞 세 글자가 `qa.` 다, 실측).
//     ⇒ 구문으로 다시 유도하지 말고 **이미 아는 곳에서 묻는다.** 해소기는 한정으로 찾았는지
//       맨이름으로 찾았는지 **정의상** 안다.
static bool ir_bare_is_ambiguous(ir_ctx_t *c, proven_u8str_view_t name,
                                 proven_u8str_view_t *m1, proven_u8str_view_t *m2) {
    proven_u8str_view_t first = { 0 };
    bool have = false;
    for (proven_size_t i = 0; i < c->out->ndefs; i++) {
        if (!proven_u8str_view_eq(c->out->defs[i].name, name)) continue;
        proven_u8str_view_t om = c->out->defs[i].owner_mod;
        if (!om.size) continue;                       // 모듈 밖 선언 — 가릴 것이 없다
        if (!have) { first = om; have = true; continue; }
        if (!proven_u8str_view_eq(first, om)) { *m1 = first; *m2 = om; return true; }
    }
    return false;
}
// ★ 타입 자리 한정 정규화 (RFC-0011): `M.Type`(M=선언 모듈·점 하나) → bare `Type`. 아니면 그대로.
//   타입 이름은 빌트인과 안 겹치므로 좁혀도 안전. op 이름(get/len 충돌)은 여기 안 쓰고 ir_modqual_tail 이 맡는다.
 proven_u8str_view_t ir_strip_mod(ir_ctx_t *c, proven_u8str_view_t v) {
    proven_size_t dot = v.size, ndot = 0;
    for (proven_size_t i = 0; i < v.size; i++)
        if (v.ptr[i] == (proven_u8)'.') { if (dot == v.size) dot = i; ndot++; }
    if (ndot != 1 || dot == 0 || dot + 1 >= v.size) return v;
    proven_u8str_view_t head = { .ptr = v.ptr, .size = dot };
    for (proven_size_t m = 0; m < c->nmodnames; m++)
        if (proven_u8str_view_eq(head, c->modnames[m]))
            return (proven_u8str_view_t){ .ptr = v.ptr + dot + 1, .size = v.size - dot - 1 };
    // ★★ **`use M as N` 의 별칭도 머리다**(결함 #14). 별칭은 모듈 이름 표에 없으므로 위 고리가
    //   놓쳤고, 그래서 매뉴얼이 권하는 `use allocs as al .` 뒤의 `spawn actor al.bump_bytes` 가
    //   «액터가 아니다» 로 떨어졌다 — 별칭을 쓰라고 적어 놓고 별칭을 모르는 것이다.
    for (proven_size_t m = 0; m < c->nusebind; m++)
        if (proven_u8str_view_eq(head, c->usebind[m]))
            return (proven_u8str_view_t){ .ptr = v.ptr + dot + 1, .size = v.size - dot - 1 };
    return v;
}
// ★★★ **없는 이름은 그 이름을 댄다** (2026-09-14 — 공개 전 점검). 전엔 `E-IR-UNDEF: undefined name in expression`
//   한 줄이라, 처음 쓰는 사람은 **무엇이** 없는지 몰랐다(`strings.count_byte` 처럼 모듈에 없는 op 을 부른 흔한 실수).
//   ⇒ 이름을 대고, 한정된 이름이면 앞마디가 무엇인지(모듈·enum)에 따라 갈라 말한다. enum 에 없는 갈래는
//     제 코드(`E-ENUM-NOVARIANT`)로, 그 enum 의 갈래 목록과 함께.
static const char *low_renamed_word(proven_u8str_view_t w) {   // RFC-0127 — 옛 철자 → 새 철자(진단에만 쓴다)
#define X(o, n) if (veq(w, #o)) return #n;
    LOW_RENAMED(X)
#undef X
    return NULL;
}
static void ir_fail_undef(ir_ctx_t *c, const low_cst_t *nd) {
    proven_u8str_view_t lex = nd->tok.lex, head = nd->qual_mod, tail = lex;
    if (!head.size) {
        proven_size_t i = low_view_last(lex, '.');
        if (i != PROVEN_INDEX_NOT_FOUND) { head = (proven_u8str_view_t){ .ptr = lex.ptr, .size = i }; tail = low_view_after_last(lex, '.'); }
    }
    char buf[256];
    if (head.size) {
        char vl[160]; size_t vn = 0; vl[0] = 0; int nv = 0;
        for (proven_size_t i = 0; i < c->nenumv; i++)
            if (c->enum_owner[i].size && proven_u8str_view_eq(c->enum_owner[i], head)) {
                nv++;
                int w = snprintf(vl + vn, sizeof vl - vn, "%s%.*s", vn ? ", " : "", (int)c->enumv[i].size, (const char *)c->enumv[i].ptr);
                if (w < 0 || (size_t)w >= sizeof vl - vn) { vn = sizeof vl - 1; break; }
                vn += (size_t)w;
            }
        if (nv) {
            snprintf(buf, sizeof buf, "enum `%.*s` has no variant `%.*s` — its variants are: %s",
                     (int)head.size, (const char *)head.ptr, (int)tail.size, (const char *)tail.ptr, vl);
            ir_fail_buf(c, "E-ENUM-NOVARIANT", buf, nd->line);
            return;
        }
        bool is_mod = false;
        for (proven_size_t m = 0; m < c->nmodnames; m++) if (proven_u8str_view_eq(head, c->modnames[m])) is_mod = true;
        if (is_mod)
            snprintf(buf, sizeof buf, "module `%.*s` has no op or value named `%.*s` — check the spelling, and "
                     "that the module exports it (`export fn`/`export proc`)",
                     (int)head.size, (const char *)head.ptr, (int)tail.size, (const char *)tail.ptr);
        else
            snprintf(buf, sizeof buf, "undefined name `%.*s` — `%.*s` is not a module used by this unit (`use %.*s .`) "
                     "nor an enum", (int)lex.size, (const char *)lex.ptr, (int)head.size, (const char *)head.ptr,
                     (int)head.size, (const char *)head.ptr);
    } else if (low_renamed_word(lex))
        snprintf(buf, sizeof buf, "undefined name `%.*s` — this builtin is now spelled `%s` (RFC-0127: the bitset and "
                 "lane ops carry a prefix, so these short words are free for your own ops)%s",
                 (int)lex.size, (const char *)lex.ptr, low_renamed_word(lex),
                 (veq(lex, "any") || veq(lex, "all")) ? ". Inside `pipe … do … end` the terminal stays `any`/`all`" : "");
    else
        snprintf(buf, sizeof buf, "undefined name `%.*s` — not a local, an input, an op, a constant or an enum "
                 "variant in scope here", (int)lex.size, (const char *)lex.ptr);
    ir_fail_buf(c, "E-IR-UNDEF", buf, nd->line);
}
static bool ir_is_enum_variant(const ir_ctx_t *c, proven_u8str_view_t name) {
    for (proven_size_t i = 0; i < c->nenumv; i++)
        if (proven_u8str_view_eq(c->enumv[i], name)) return true;
    return false;
}
// ★ enum 값의 표현: **변형의 인덱스**(정수). 비용 가시 — 런타임 태그가 없다.
//   지금까지 enum 값은 값 자리에 올 수 없었다(E-IR-UNSUP). 이제 온다.
 proven_i64 ir_variant_index(const ir_ctx_t *c, proven_u8str_view_t name) {
    for (proven_size_t i = 0; i < c->nenumv; i++)
        if (proven_u8str_view_eq(c->enumv[i], name)) return (proven_i64)i;
    return -1;
}
static proven_size_t ir_err_intern(ir_ctx_t *c, proven_u8str_view_t name) {
    for (proven_size_t i = 0; i < c->out->nerrs; i++)
        if (proven_u8str_view_eq(c->out->errs[i], name)) return i;
    if (c->out->nerrs < IR_MAXERRS) { c->out->errs[c->out->nerrs] = name; return c->out->nerrs++; }
    return 0;
}
 proven_size_t ir_struct_find(const low_ir_t *ir, proven_u8str_view_t name, bool *found) {
    for (proven_size_t i = 0; i < ir->nstructs; i++)
        if (proven_u8str_view_eq(ir->structs[i].name, name)) { *found = true; return i; }
    *found = false; return 0;
}
// ★★★ **필드 표지인가** — 타입 낱말과 표지를 가르는 유일한 경계다.
static bool ir_field_marker(proven_u8str_view_t w) {
    return veq(w, "big") || veq(w, "little") || veq(w, "rw") || veq(w, "ro") || veq(w, "wo");
}

// ★★★ **매개변수를 받는 타입의 머리인가** (2026-07-20).
//
//   ☞ **전엔 필드 타입이 낱말 하나였다.** `xs slice u64 .` 를 쓰면 `slice` 가 타입이 되고
//     `u64` 가 **모르는 표지**가 되어 `E-FIELD-MARK` 로 죽었다. 그래서 구조체가
//     슬라이스·option·배열을 **하나도** 담지 못했다.
//   ★ 원인을 오래 `mut` 이라 적어 뒀는데(RFC-0070 §5.2) **틀렸다**: 불변 `slice u64` 도
//     똑같이 죽었다. 막던 것은 `mut` 이 아니라 **"타입은 낱말 하나"** 라는 문법이었다.
//   ⇒ 머리를 알아보고 **표지가 나올 때까지** 낱말을 타입으로 먹는다.
//
//   ★★★ 런타임은 **이미 할 수 있었다**: VM 의 레코드는 필드를 `vmv_t` 로,
//     C 백엔드는 `lowv` 로 든다 — 둘 다 태그 값이라 슬라이스가 들어간다.
//     막고 있던 것은 **앞단 문법뿐**이었다.
static bool ir_field_tyhead(proven_u8str_view_t w) {
    // ★ `owned` 도 타입 머리다(2026-07-23) — 그래야 `a owned h .` 이 **두 낱말 타입**으로
    //   읽히고 아핀 필드가 표현된다. 전엔 없어서 `owned` 가 타입, `h` 가 모르는 표식이 돼
    //   E-FIELD-MARK 로 죽었다 ⇒ 필드 단위 부분 이동이 **문법에서** 도달불가였다.
    return veq(w, "slice") || veq(w, "mut") || veq(w, "option") || veq(w, "array") ||
           veq(w, "ref") || veq(w, "mut_ref") || veq(w, "result") || veq(w, "owned");
}

// 타입이 차지하는 낱말 수를 센다(첫 낱말은 kids[1]). 표지 앞에서 멈춘다.
 proven_size_t ir_field_tywords(const low_cst_t *fld) {
    if (fld->nkids < 2 || !is_atom(fld->kids[1])) return 0;
    if (!ir_field_tyhead(fld->kids[1]->tok.lex)) return 1;      // 평범한 한 낱말 타입
    proven_size_t n = 1;
    while (1 + n < fld->nkids && is_atom(fld->kids[1 + n]) &&
           !ir_field_marker(fld->kids[1 + n]->tok.lex)) n++;
    return n;
}

 proven_u8 ir_field_size(proven_u8str_view_t w) {
    // ★★★★ **bool 은 메모리에 설 자리가 없었다** (RFC-0092, 2026-08-10).
    //   이 표에 `bool` 이 없어서 0 을 돌려줬다 — 그런데 **선언 검사기는 이 표를 안 본다.**
    //   그래서 `struct rec . a bool . b u8 .` 은 **초록**이었고, 그 struct 의 `size_of` 와
    //   `view` 는 **E-IR-UNDEF** 였다: **선언은 받아 주고 쓰려 하면 없는** 타입이었다.
    //   ⇒ **1 바이트**로 못박는다(값은 0/1 만 유효 · 정렬 1). 소유자 판단이 맞았다.
    //   ☞ 따라오는 결론 하나: bool 은 `plain` 이 **아니다**(254 개 비트 패턴이 무효) —
    //     바이트를 bool 로 퍼닝하는 것은 막아야 하고, 반대 방향은 안전하다.
    if (veq(w, "bool")) return 1;
    if (veq(w, "u8") || veq(w, "i8")) return 1;
    if (veq(w, "u16") || veq(w, "i16")) return 2;
    if (veq(w, "u32") || veq(w, "i32") || veq(w, "f32")) return 4;
    if (veq(w, "u64") || veq(w, "i64") || veq(w, "usize") || veq(w, "isize") || veq(w, "f64")) return 8;
    return 0;
}
 bool ir_is_float_ty(proven_u8str_view_t w) { return veq(w, "f32") || veq(w, "f64"); }
// ★ 명세에 있으나 처리기가 **아직 못 하는** 낱말들. 정본 §4.7 이 이 자리를 규범한다:
//   *"없는 것처럼 다루지 아니한다 — 못 하는 것은 처리기 쪽이라고 말하여야 한다."*
//   이것들을 "없는 이름" 이라 하면 사용자는 **없는 오타를 찾으러 간다.**
static bool ir_is_postmvp_word(proven_u8str_view_t w) {
    return veq(w, "await") || veq(w, "defer") || veq(w, "map_error") ||
           veq(w, "spawn_task") || veq(w, "channel") || veq(w, "select");
}
// IEEE bits (4/8 bytes) ⇄ double — f32 fields/lanes round-trip through the native float
double ir_bits_to_f(proven_u64 bits, proven_u8 size) {
    if (size == 4) { float f; proven_u32 b = (proven_u32)bits; memcpy(&f, &b, 4); return (double)f; }
    double d; memcpy(&d, &bits, 8); return d;
}
proven_u64 ir_f_to_bits(double d, proven_u8 size) {
    if (size == 4) { float f = (float)d; proven_u32 b; memcpy(&b, &f, 4); return b; }
    proven_u64 bits; memcpy(&bits, &d, 8); return bits;
}
// layout: packed = cumulative offsets; native = each field aligned to its size,
// total padded to the max alignment (RFC-0050 §6.6.1 — padding is visible policy).
// `align n` (RFC-0051 §5.1) is an alignment CONTRACT: it raises the struct's
// alignment (and hence the trailing pad) and is checked at every view boundary.
 void ir_struct_layout(const low_ir_t *ir, low_ir_struct_t *s) {
    proven_u16 off = 0, maxal = 1;
    s->viewable = s->nf > 0;
    for (proven_size_t i = 0; i < s->nf; i++) {
        // ★ RFC-0132 T2b-3d ⓓ (소유자 «C 와 같은 인라인 배치») — 배열 칸은 바이트 안에 그대로 놓인다(`uint8_t body[16]`):
        //   크기 = 원소 수 × 원소 폭, 정렬 = 원소 폭. 레코드에서는 여전히 보는 슬라이스(태그 한 칸)이고 바이트는 레코드 버퍼에
        //   있다 — 이 오프셋을 읽는 것은 view·encode·C 헤더뿐이다(빠른 경로는 배열 칸 구조체를 받지 않는다).
        if (s->f[i].arrn) {
            proven_u32 asz = s->f[i].arrn * s->f[i].arresz, aal = s->f[i].arresz ? s->f[i].arresz : 1;
            if (!s->packed) {
                if (off % aal) off = (proven_u16)(off + aal - off % aal);
                if (aal > maxal) maxal = (proven_u16)aal;
            }
            s->f[i].off = off;
            if ((proven_u32)off + asz > 0xffffu) { s->viewable = false; return; }
            off = (proven_u16)(off + asz);
            continue;
        }
        proven_u8 sz = s->f[i].size;
        if (!sz) { s->viewable = false; return; }
        // ★★★ 태그 값 필드는 **바이트 레이아웃이 없다** — 크기가 8 이라 스칼라처럼 보이지만
        //   아니다. 표식을 안 봤다면 `view` 가 슬라이스 필드를 가진 구조체를 **받아 버린다**
        //   (2026-07-20 실측: 실제로 받았다 — 크기로만 판단하고 있었다).
        // ★★★★★ **여기서 통째로 나가면 뒤 필드의 오프셋이 전부 0 이 된다** (RFC-0106 단계 2).
        //   `boxed` 는 *"이 필드는 바이트로 **볼** 수 없다"* 는 뜻이지 *"이 struct 에 자리가
        //   없다"* 가 아니다. 그런데 여기서 `return` 하는 바람에 그 뒤 필드가 **오프셋 0**
        //   을 갖고, 빠른 경로가 네 개의 스칼라를 **같은 자리에** 쓰고 있었다
        //   (실측: `lw_st_f(ar[0] + 0, …)` 가 네 번 — 차등 스윕이 13 건으로 잡았다).
        //   ⇒ `viewable` 만 끄고 **자리는 끝까지 센다.** 뷰·encode 는 `viewable` 로 막히므로
        //     이 오프셋을 안 읽는다 — 읽는 것은 빠른 프레임뿐이다.
        //   ☞ *"못 본다" 와 "없다" 는 다른 말이고, 한 줄이 그 둘을 섞고 있었다.*
        if (s->f[i].boxed) s->viewable = false;
        // ★ 중첩 구조체 필드의 **정렬은 안쪽 구조체의 정렬**이다 — 그 **크기**가 아니다.
        //   (3바이트 구조체를 3으로 정렬하려 들면 아무것도 맞지 않는다.)
        proven_u16 al = sz;
        if (s->is_shadow && s->f[i].slmeta) al = (proven_u16)low_ptr_slot_align();   // ★ RFC-0135 D13 — (포인터, 길이): 포인터 정렬(RFC-0137 P3)
        if (s->f[i].sidx >= 0 && ir && (proven_size_t)s->f[i].sidx < ir->nstructs)
            al = ir->structs[s->f[i].sidx].align ? ir->structs[s->f[i].sidx].align : 1;
        if (!s->packed) {
            if (al && off % al) off = (proven_u16)(off + al - off % al);
            if (al > maxal) maxal = al;
        }
        s->f[i].off = off;
        off = (proven_u16)(off + sz);
    }
    if (s->align > maxal) maxal = s->align;         // declared contract wins
    if (s->packed) maxal = s->align ? s->align : 1; // packed: only the declaration aligns
    if (maxal > 1 && off % maxal) off = (proven_u16)(off + maxal - off % maxal);
    s->total = off;
    s->align = maxal;   // effective alignment (natural, or raised by `align n`)
}

 proven_size_t ir_field_intern(ir_ctx_t *c, proven_u8str_view_t name) {
    for (proven_size_t i = 0; i < c->out->nfields; i++)
        if (proven_u8str_view_eq(c->out->fields[i], name)) return i;
    if (c->out->nfields < IR_MAXERRS) { c->out->fields[c->out->nfields] = name; return c->out->nfields++; }
    return 0;
}

// integer literal → value (dec/hex/bin, '_' separators; floats are unsupported here)
 bool ir_int_lit(proven_u8str_view_t v, proven_i64 *out) {
    proven_size_t i = 0; proven_u64 val = 0, base = 10; bool neg = false;
    // ★ **부호 있는 정수 리터럴** — 렉서는 `-5` 를 한 NUMBER 토큰으로 낸다. 이 부호를 안 봐서
    //   `-5` 가 정수로 안 읽히고 **float(FCONST)로 하강**됐다 → 부호형 슬라이스 원소 저장이
    //   VM(0 을 씀)≠native(panic: value)로 발산했다(음수 정수 상수만; 런타임값·양수·float 은 정상).
    //   `-1.5` 같은 float 은 `.` 에서 false 나 그대로 float 경로로 간다.
    if (i < v.size && (v.ptr[i] == '-' || v.ptr[i] == '+')) {
        neg = (v.ptr[i] == '-'); i++;
        if (i >= v.size) return false;                 // 부호 뒤에 숫자가 없으면 정수 아님
    }
    if (i + 1 < v.size && v.ptr[i] == '0' && (v.ptr[i + 1] == 'x' || v.ptr[i + 1] == 'X')) { base = 16; i += 2; }
    else if (i + 1 < v.size && v.ptr[i] == '0' && (v.ptr[i + 1] == 'b' || v.ptr[i + 1] == 'B')) { base = 2; i += 2; }
    for (; i < v.size; i++) {
        char ch = (char)v.ptr[i];
        if (ch == '_') continue;
        proven_u64 d;
        if (ch >= '0' && ch <= '9') d = (proven_u64)(ch - '0');
        else if (base == 16 && ch >= 'a' && ch <= 'f') d = (proven_u64)(ch - 'a' + 10);
        else if (base == 16 && ch >= 'A' && ch <= 'F') d = (proven_u64)(ch - 'A' + 10);
        else return false;
        if (d >= base) return false;
        val = val * base + d;
    }
    *out = neg ? -(proven_i64)val : (proven_i64)val;
    return true;
}

// ── expression lowering ───────────────────────────────────────────────────────
// A flat operand run is bracketed by arity (Γ), exactly like the evaluator: the
// first operand names an op → it consumes `arity` values recursively; extras are
// a strict-arity error. A bare name that is a local loads it (locals shadow ops).

// ★★★ 값의 **머리 이름**을 모양과 무관하게 읽는다.
//
//   평평한 `error delta` 든, 괄호 친 `(error delta)` 든 **머리는 `error` 다.** 그런데 IR 이
//   `kids[1]` 이 **ATOM 인지**만 보고 판별하고 있었다 — 즉 **모양을 외웠다.** arity 단일화가
//   나무를 세우자 `return (error delta)` 를 **정상 반환으로 착각**했고, errors 절의 완전성
//   검사(a=9)를 **오류 경로에 잘못 걸었다.** 해시 오라클이 즉시 잡았다.
//   ★ 이것이 883곳의 raw kids[] 접근이 품고 있는 위험의 두 번째 실물이다.
 bool ir_head_named(const low_cst_t *nd, const char *s2) {
    if (!nd) return false;
    if (nd->kind == LOW_CST_ATOM) return veq(nd->tok.lex, s2);
    if (nd->kind == LOW_CST_GROUP && nd->nkids == 1) return ir_head_named(nd->kids[0], s2);
    if (nd->kind == LOW_CST_FORM && nd->nkids && nd->kids[0]->kind == LOW_CST_ATOM)
        return veq(nd->kids[0]->tok.lex, s2);
    return false;
}

 void ir_run_ex(ir_ctx_t *c, low_cst_t *const *k, proven_size_t start, proven_size_t n, bool allow_infix);
 void ir_run(ir_ctx_t *c, low_cst_t *const *k, proven_size_t start, proven_size_t n);
 void ir_node(ir_ctx_t *c, const low_cst_t *nd);
 void ir_island_climb(ir_ctx_t *c, low_cst_t *const *k, proven_size_t n, proven_size_t *pos, int minp);

// ★★★ **MMIO 권한 — plain `field`/`set (field …)`/붙임점 경로**(RFC-0042 D3, 건전성 구멍 메움).
//   `read_volatile`/`write_volatile` 와 **같은 IR 옵**(IRW_FIELD/IRW_FSTORE)으로 같은 레지스터에
//   닿는 zero-copy 뷰 접근은 권한 검사를 하나도 안 받고 있었다. 여기서 그 구멍을 막는다 —
//   단, **이름이 아니라 타입으로** 판정한다: `base` 가 **mmio 로 표시된 struct** 를 뷰하는 지역일
//   때만 검사한다. 그래야 다른(비-mmio) struct 의 동명 필드가 오탐되지 않는다.
//   base 의 타입을 여기서 소리 있게 못 알아내면(예: 복합식) **거절하지 않는다**(보수적).
 void ir_mmio_perm_check(ir_ctx_t *c, proven_u8str_view_t base,
                               proven_u8str_view_t reg, bool wr, proven_u32 line) {
    bool lf; proven_size_t ls = ir_local_find(c, base, &lf);
    if (!lf) return;                                  // 지역이 아니면 판정 못 함 — 보수적
    proven_u8str_view_t tn = c->locals[ls].tyname;
    if (!tn.size) return;
    bool sf; proven_size_t si = ir_struct_find(c->out, tn, &sf);
    if (!sf) return;
    const low_ir_struct_t *st = &c->out->structs[si];
    if (!st->is_mmio) return;                          // ★ **mmio 뷰일 때만** 검사한다(타입 기반)
    // ★★★★★ **장치 칸은 보통 칸처럼 만지지 아니한다** (결함 노트 #68 · RFC-0115 §8-7, 2026-09-17).
    //
    //   이 함수를 부르는 자리는 **전부 보통 접근**이다 — `field g idr` · `set (field g moder) 2` ·
    //   붙은 이름. `read_volatile`/`write_volatile` 은 저 위에서 따로 내려가고 여기 오지 않는다.
    //   ☞ 그러니 여기 닿았다는 것 자체가 «장치 레지스터를 보통 칸으로 만졌다» 는 뜻이다.
    //   ★ 왜 거절인가: 보통 칸 접근은 처리기가 **합치거나 지워도 되는** 연산이다(같은 칸을 두 번
    //     읽으면 한 번으로, 읽지 않는 쓰기는 없애도 된다). 장치에서는 그 접근 **자체가 일**이라
    //     지워지면 하드웨어가 틀린 일을 한다. 앞서 이 자리는 `ro`/`wo` 만 보고 있었으므로
    //     **«쓸 수 있는 칸을 보통으로 쓰는 것»** 은 조용히 통과했다 — 가장 흔한 모양이 그것이다.
    ir_fail(c, "E-MMIO-PLAIN",
            "this is a DEVICE register, and this reads/writes it like ordinary memory. An ordinary "
            "access is one the translator may merge or delete (two reads of the same field become "
            "one; a store nothing reads goes away) — for a device the access ITSELF is the work, so "
            "deleting it makes the hardware do the wrong thing. Write `read_volatile <block> <reg>` "
            "or `write_volatile <block> <reg> <value>`, which say exactly once, in the written order "
            "(RFC-0042 D1). The `ro`/`wo` permission is checked there too", line);
    if (c->failed) return;
    for (proven_size_t z = 0; z < st->nf; z++) {
        if (!proven_u8str_view_eq(st->f[z].name, reg)) continue;
        if (wr && st->f[z].perm == FP_RO)
            ir_fail(c, "E-MMIO-PERM",
                    "this register is READ-ONLY (`ro`) — writing it is a compile "
                    "error, not a runtime surprise (RFC-0042 D3). The device says "
                    "what it will accept; the type says it back", line);
        if (!wr && st->f[z].perm == FP_WO)
            ir_fail(c, "E-MMIO-PERM",
                    "this register is WRITE-ONLY (`wo`) — reading it is a compile "
                    "error. A wo register often reads as garbage (or has a read "
                    "side effect), so the read is not merely useless: it is wrong", line);
    }
}

// `s.f.g` — one glued qualified IDENT; when the first segment is a local, the
// remaining segments are field selections (rev.e §2.8). Returns false if the
// name has no dot or its head is not a local (caller falls through).
static bool ir_glued_local(ir_ctx_t *c, proven_u8str_view_t v, proven_u32 line) {
    proven_size_t dot = 0;
    for (proven_size_t i = 0; i < v.size; i++) if (v.ptr[i] == '.') { dot = i; break; }
    if (!dot || dot + 1 >= v.size) return false;
    proven_u8str_view_t head = { .ptr = v.ptr, .size = dot };
    bool f; proven_size_t slot = ir_local_find(c, head, &f);
    if (!f) return false;
    // ★★★★★ **붙은 점 필드 접근은 폐지됐다** (2026-08-25 · 소유자 결정).
    //   `p.x` → `(field p x)`. 같은 뜻을 적는 길이 둘이면 읽는 사람이 둘 다 알아야 한다.
    //   ☞ **여기가 유일하게 정확한 자리다**: 첫 마디가 **지역/파라미터로 풀렸을 때만**
    //     필드 접근이다. 모듈 한정(`allocs.byte_allocator`) · 변형 이름(`too.short`) ·
    //     선언 이름(`fn pt.twice`)은 여기 오지 않는다 — 그것들은 지역이 아니다.
    //   ★ 글자만 보고 고치려다 세 번 물렸다(문자열 안의 파일 이름 · `use X .` · 변형 이름).
    //     **누가 그것을 아는가**를 물었어야 했고, 답은 이름 해소였다.
    // ★★ **이전 훑기 모드**(`LOWENT_LIST_GLUED=1`) — 멈추지 않고 **자리를 다 적는다**.
    //   진단은 첫 하나만 실리므로(ir_fail 이 c->failed 를 세운다) 자동 이전이 한 파일에서
    //   한 자리씩만 볼 수 있었다. 같은 **판정 지점**을 쓰되 흐름을 안 끊는다 —
    //   그래야 *"글자로 추측하지 않는다"* 를 지키면서 한 바퀴에 다 옮길 수 있다.
    if (getenv("LOWENT_LIST_GLUED")) {
        char nb2[160]; proven_size_t n2 = v.size < sizeof nb2 - 1 ? v.size : sizeof nb2 - 1;
        memcpy(nb2, v.ptr, n2); nb2[n2] = 0;
        char sp2[160]; proven_size_t s2 = 0;
        for (proven_size_t z = 0; z < n2 && s2 < sizeof sp2 - 1; z++)
            sp2[s2++] = nb2[z] == '.' ? ' ' : nb2[z];
        sp2[s2] = 0;
        fprintf(stderr, "GLUED\t%u\t%s\t(field %s)\n", (unsigned)line, nb2, sp2);
    } else if (!c->allow_glued_field) {
        {   // ★ 진단이 **그 이름을 그대로 보여 준다** — 한 줄에 붙은 점이 여럿일 때
            //   *"어느 것인가"* 를 사람이 다시 찾지 않아도 되도록.
            char nb[160]; proven_size_t nn = v.size < sizeof nb - 1 ? v.size : sizeof nb - 1;
            memcpy(nb, v.ptr, nn); nb[nn] = 0;
            char spaced[160];
            proven_size_t sn = 0;
            for (proven_size_t z = 0; z < nn && sn < sizeof spaced - 1; z++)
                spaced[sn++] = nb[z] == '.' ? ' ' : nb[z];
            spaced[sn] = 0;
            char msg[512];
            snprintf(msg, sizeof msg,
                     "glued-dot field access is gone — write `(field %s)` instead of `%s`. "
                     "One meaning gets one spelling: the dot still means module qualification, "
                     "a variant name and a type-associated declaration, so a fourth meaning made "
                     "the same letters mean four things. `field` chains: `(field o i z)`",
                     spaced, nb);
            ir_fail_buf(c, "E-FIELD-GLUED", msg, line);
            return true;
        }
        ir_fail(c, "E-FIELD-GLUED",
                "glued-dot field access is gone — write `(field <value> <name>…)` instead. "
                "One meaning gets one spelling: the dot still means module qualification "
                "(`mod.name`), a variant name (`err.too_short`) and a type-associated op "
                "declaration (`fn pt.twice`), so keeping a fourth meaning made the same "
                "letters mean four things. `field` also chains: `(field o i z)`",
                line);
        return true;
    }
    // ★ mmio 뷰의 첫 마디(레지스터)를 **읽는** 것이면 wo 권한을 검사한다(타입 기반, RFC-0042 D3).
    {
        proven_size_t fe = dot + 1;
        while (fe < v.size && v.ptr[fe] != '.') fe++;
        proven_u8str_view_t seg0 = { .ptr = v.ptr + dot + 1, .size = fe - (dot + 1) };
        proven_i64 dummy;
        if (seg0.size && !ir_int_lit(seg0, &dummy))
            ir_mmio_perm_check(c, head, seg0, false, line);
        if (c->failed) return true;
    }
    ir_emit(c, IRW_LOAD, (proven_i64)slot);
    proven_size_t s = dot + 1;
    for (proven_size_t i = s; i <= v.size; i++) {
        if (i == v.size || v.ptr[i] == '.') {
            proven_u8str_view_t seg = { .ptr = v.ptr + s, .size = i - s };
            if (!seg.size) { ir_fail(c, "E-IR-UNSUP", "empty segment in glued access", line); return true; }
            // ★ 숫자로만 된 마디 = **인덱스**. 그 밖 = **필드**. 하나의 문법, 두 접근.
            proven_i64 idx;
            if (ir_int_lit(seg, &idx)) {
                ir_emit(c, IRW_CONST, idx);
                ir_emit(c, IRW_INDEX, 0);
            } else {
                ir_emit(c, IRW_FIELD, (proven_i64)ir_field_intern(c, seg));
            }
            s = i + 1;
        }
    }
    return true;
}

// ★★★ **붙임점을 `set` 의 place 로도 쓴다** (2026-07-20 — 사용자 요청).
//
//   ☞ `p.x` 는 **읽기에서는** 되는데(`ir_glued_local`) **`set` 의 place 로는 안 됐다**:
//     `set p.x 5 .` → `E-IR-UNDEF: set of an undeclared name` — set 이 그 원자를 **통째로
//     이름 하나**로 찾았기 때문이다. `set (field p x) 5 .` 로는 됐다.
//   ★★★ 즉 **같은 표기가 읽기와 쓰기에서 다르게 취급됐다.** RFC-0049 가 중위 `to`/`in` 을
//     지우면서 *"접근은 붙임점으로 일원화한다"* 고 했는데, 그 일원화가 **절반만** 서 있었다 —
//     `some`/`none` 이 소비만 있고 생산이 없던 것과 **같은 모양의 구멍**이다.
//
//   ⇒ 읽기와 같은 방식으로 마디를 가르되, **마지막 마디만 저장으로** 바꾼다:
//     컨테이너까지를 스택에 올리고(중간 마디는 읽기), 마지막이 숫자면 인덱스를 밀어
//     `ISTORE`, 이름이면 `FSTORE` 다. 중간 마디가 여럿이어도 된다(`a.b.c`).
//
//   반환: 처리했으면 true. `*is_index` 와 `*fid`(FSTORE 용 필드 id)를 채운다.
 bool ir_glued_place(ir_ctx_t *c, proven_u8str_view_t v, proven_u32 line,
                           bool *is_index, proven_i64 *fid) {
    proven_size_t dot = 0;
    for (proven_size_t i = 0; i < v.size; i++) if (v.ptr[i] == '.') { dot = i; break; }
    if (!dot || dot + 1 >= v.size) return false;
    proven_u8str_view_t head = { .ptr = v.ptr, .size = dot };
    bool f; proven_size_t slot = ir_local_find(c, head, &f);
    if (!f) return false;
    // 마지막 마디의 시작 자리를 먼저 찾는다 — 그 앞까지는 **읽기**다.
    proven_size_t last_s = dot + 1;
    for (proven_size_t i = dot + 1; i < v.size; i++) if (v.ptr[i] == '.') last_s = i + 1;
    if (last_s >= v.size) { ir_fail(c, "E-IR-UNSUP", "empty segment in glued access", line); return true; }
    // ★ mmio 뷰의 첫 마디(레지스터)를 검사한다(타입 기반, RFC-0042 D3): 첫 마디가 곧 마지막
    //   마디면 그 레지스터에 **쓰는** 것(ro 금지); 그 앞이면 중간 **읽기**(wo 금지)다.
    {
        proven_size_t fe = dot + 1;
        while (fe < v.size && v.ptr[fe] != '.') fe++;
        proven_u8str_view_t seg0 = { .ptr = v.ptr + dot + 1, .size = fe - (dot + 1) };
        proven_i64 dummy;
        if (seg0.size && !ir_int_lit(seg0, &dummy))
            ir_mmio_perm_check(c, head, seg0, (dot + 1 == last_s), line);
        if (c->failed) return true;
    }
    ir_emit(c, IRW_LOAD, (proven_i64)slot);
    proven_size_t s = dot + 1;
    for (proven_size_t i = s; i < last_s; i++) {          // 중간 마디 = 읽기
        if (v.ptr[i] == '.') {
            proven_u8str_view_t seg = { .ptr = v.ptr + s, .size = i - s };
            if (!seg.size) { ir_fail(c, "E-IR-UNSUP", "empty segment in glued access", line); return true; }
            proven_i64 idx;
            if (ir_int_lit(seg, &idx)) { ir_emit(c, IRW_CONST, idx); ir_emit(c, IRW_INDEX, 0); }
            else ir_emit(c, IRW_FIELD, (proven_i64)ir_field_intern(c, seg));
            s = i + 1;
        }
    }
    proven_u8str_view_t last = { .ptr = v.ptr + last_s, .size = v.size - last_s };
    proven_i64 idx;
    if (ir_int_lit(last, &idx)) {          // 숫자 마디 = 인덱스 쓰기
        ir_emit(c, IRW_CONST, idx);        // ISTORE 는 (슬라이스, 인덱스, 값) 을 먹는다
        *is_index = true; *fid = 0;
    } else {
        *is_index = false; *fid = (proven_i64)ir_field_intern(c, last);
    }
    return true;
}

// ★★★ **타입의 lane 자리에 오는 comptime 식을 접는다** (RFC-0021 — 값 comptime 접기).
//   `vec u32 4`(리터럴)·`vec u32 scalable`(target 폭) 위에, `vec u32 (native_lanes u32)` 처럼 **식**을
//   쓸 수 있게 한다. 접기는 **컴파일 시**에 끝나므로 뒷단은 정수 하나만 본다 ⇒ VM·C 가 같은 값 ⇒ 오라클 안전.
//   ★ 이것이 서면 `scalable` 은 특수 문법이 아니라 **이 식의 한 경우**가 된다(개념 통합).
 proven_i64 ir_native_lanes_of(proven_u8 esz) {
    proven_u8 sb = low_ir_target()->simd_bytes;
    proven_i64 n = (esz && sb) ? (proven_i64)(sb / esz) : 1;
    if (n < 1) n = 1;
    if (n > 16) n = 16;   // 도구가 실제로 만들 수 있는 폭까지만
    return n;
}
 bool ir_fold_lane_expr(const ir_ctx_t *c, const low_cst_t *nd, proven_i64 *out) {
    if (!nd) return false;
    if (nd->kind == LOW_CST_ATOM) {
        if (veq(nd->tok.lex, "scalable")) return false;   // scalable 은 호출자가 따로 본다
        if (ir_int_lit(nd->tok.lex, out)) return true;
        // ★ **comptime 로 접힌 `let` 이름**도 폭이 될 수 있다 — 그래야 이식 가능한 커널이
        //   `let L be native_lanes u32 .` 하나로 폭을 정하고 `vec u32 L` 을 되풀이한다.
        if (c) for (proven_size_t q = 0; q < c->ncint; q++)
            if (proven_u8str_view_eq(c->cint[q].name, nd->tok.lex)) { *out = c->cint[q].val; return true; }
        return false;
    }
    const low_cst_t *g = nd;                       // 괄호 겹을 벗긴다
    while (g && g->kind == LOW_CST_GROUP && g->nkids == 1) g = g->kids[0];
    if (g && g->kind == LOW_CST_FORM && g->nkids >= 2 && g->kids[0]->kind == LOW_CST_ATOM &&
        g->kids[1]->kind == LOW_CST_ATOM && veq(g->kids[0]->tok.lex, "native_lanes")) {
        proven_u8 esz = ir_field_size(g->kids[1]->tok.lex);
        if (!esz) return false;
        *out = ir_native_lanes_of(esz);
        return true;
    }
    return false;
}
// ★★★ **comptime 스칼라 CTFE 접기** (RFC-0015 §6.1). `comptime <expr>` 의 <expr> 이
//   **순수 정수식**(리터럴 + 순수 스칼라 빌트인 + machine.* + 중첩 comptime)이면 컴파일
//   시점에 접어 하나의 상수로 만든다 — 뒷단은 IRW_CONST 하나만 본다(VM·C 가 같은 값 ⇒
//   오라클 안전). 재귀 fuel 로 종료를 보장한다(§6.2). 순수 정수 부분집합만 지원:
//   슬라이스/alloc/op-call/데이터 임베딩(text→table, arena→static region)은 §8 후속이다.
//   접지 못하면 false 를 돌려주고 **호출자**가 E-COMPTIME-NONCONST/FUEL 을 낸다.
/* IR_CTFE_FUEL — low_ir_priv.h */
 bool ir_ctfe_fold(const low_cst_t *nd, proven_i64 *out, int fuel) {
    if (!nd || fuel <= 0) return false;
    while (nd->kind == LOW_CST_GROUP && nd->nkids == 1) nd = nd->kids[0];   // 괄호 겹 벗기기
    if (nd->kind == LOW_CST_ATOM) return ir_int_lit(nd->tok.lex, out);
    if (nd->kind != LOW_CST_FORM || nd->nkids < 1 || !is_atom(nd->kids[0])) return false;
    proven_u8str_view_t head = nd->kids[0]->tok.lex;
    proven_size_t nargs = nd->nkids - 1;
    // 중첩 `comptime <expr>` — 이미 컴파일타임이므로 사실상 항등(RFC-0015 §6.1).
    if (veq(head, "comptime") && nargs == 1) return ir_ctfe_fold(nd->kids[1], out, fuel - 1);
    // ★ `config <opt>` — 구성 값은 comptime 상수다(RFC-0036 D5). CTFE·match scrutinee 접기가 읽는다.
    if (veq(head, "config") && nargs == 1 && is_atom(nd->kids[1])) {
        ir_opt_t *o = ir_opt_find(nd->kids[1]->tok.lex);
        if (o) { *out = o->val; return true; }
        return false;
    }
    // 연산자 원자(`+ - * /`) 는 2-항.
    if (nd->kids[0]->tok.kind == LOW_TOK_OP && nargs == 2) {
        proven_i64 a, b;
        if (!ir_ctfe_fold(nd->kids[1], &a, fuel - 1) || !ir_ctfe_fold(nd->kids[2], &b, fuel - 1)) return false;
        switch ((char)head.ptr[0]) {
            case '+': *out = a + b; return true;
            case '-': *out = a - b; return true;
            case '*': *out = a * b; return true;
            case '/': if (b == 0) return false; *out = a / b; return true;
            default:  return false;
        }
    }
    // 이름 있는 순수 스칼라 빌트인. min/max 는 IR_BUILTINS 밖(수치 표)이라 따로 본다.
    if ((veq(head, "min") || veq(head, "max")) && nargs == 2) {
        proven_i64 a, b;
        if (!ir_ctfe_fold(nd->kids[1], &a, fuel - 1) || !ir_ctfe_fold(nd->kids[2], &b, fuel - 1)) return false;
        *out = veq(head, "min") ? (a < b ? a : b) : (a > b ? a : b); return true;
    }
    const ir_builtin_t *bt = ir_builtin(head);
    if (!bt || bt->arity != nargs) return false;
    if (nargs == 1) {
        proven_i64 a;
        if (!ir_ctfe_fold(nd->kids[1], &a, fuel - 1)) return false;
        switch (bt->w) {
            case IRW_NEG:  *out = -a; return true;
            case IRW_NOT:  *out = a ? 0 : 1; return true;   // 논리 not
            default: return false;                          // 폭 의존(bit_not/clz/…)·비스칼라는 미지원
        }
    }
    if (nargs == 2) {
        proven_i64 a, b;
        if (!ir_ctfe_fold(nd->kids[1], &a, fuel - 1) || !ir_ctfe_fold(nd->kids[2], &b, fuel - 1)) return false;
        switch (bt->w) {
            case IRW_ADD: *out = a + b; return true;
            case IRW_SUB: *out = a - b; return true;
            case IRW_MUL: *out = a * b; return true;
            case IRW_DIV: if (b == 0) return false; *out = a / b; return true;
            // ★ comptime 접기도 **제수 부호**여야 한다 — 여기만 남으면 같은 식이 상수일 때와
            //   아닐 때 다른 답을 낸다(SPEC-004 · RFC-0052 D7, 2026-08-02).
            case IRW_MOD: if (b == 0) return false;
                          { proven_i64 r = a % b; if (r && ((r < 0) != (b < 0))) r += b; *out = r; }
                          return true;
            case IRW_AND: *out = (a && b) ? 1 : 0; return true;   // 논리 and/or
            case IRW_OR:  *out = (a || b) ? 1 : 0; return true;
            case IRW_BAND: *out = a & b; return true;             // 비트 and/or/xor (폭 무관)
            case IRW_BOR:  *out = a | b; return true;
            case IRW_BXOR: *out = a ^ b; return true;
            case IRW_CLMULLO: case IRW_CLMULHI: {   // 캐리 없는 곱셈 — 상수도 접는다
                unsigned long long x = (unsigned long long)a, y = (unsigned long long)b, lo = 0, hi = 0;
                for (int i = 0; i < 64; i++) if ((y >> i) & 1ull) { lo ^= x << i; if (i) hi ^= x >> (64 - i); }
                *out = (proven_i64)(bt->w == IRW_CLMULLO ? lo : hi); return true; }
            case IRW_SHL: if (b < 0 || b >= 64) return false; *out = a << b; return true;
            case IRW_SHR: if (b < 0 || b >= 64) return false; *out = a >> b; return true;
            case IRW_EQ:  *out = (a == b) ? 1 : 0; return true;
            case IRW_NE:  *out = (a != b) ? 1 : 0; return true;
            case IRW_LT:  *out = (a <  b) ? 1 : 0; return true;
            case IRW_LE:  *out = (a <= b) ? 1 : 0; return true;
            case IRW_GT:  *out = (a >  b) ? 1 : 0; return true;
            case IRW_GE:  *out = (a >= b) ? 1 : 0; return true;
            default: return false;
        }
    }
    return false;
}
// comptime vector context from a binding's declared type words (`vec t n`)
// ★ 바인딩 타입에서 **비트셋 폭**을 읽는다 (`bitset n`) — vec 의 레인 수와 같은 결.
// ★★★ **비트셋 피연산자의 선언 폭** — `contains`/`remove`/집합삽입 `add`/`complement` 이
//   원소 범위와 마스크를 **바인딩이 아니라 피연산자**에서 읽게 한다(F5: `complement s` 의 폭은
//   결과 바인딩 c 가 아니라 s 의 것이다). 피연산자가 폭을 아는 비트셋 지역이면 그 폭, 아니면 0.
static proven_u8 ir_operand_bset_w(ir_ctx_t *c, const low_cst_t *node) {
    if (!node || !is_atom(node)) return 0;
    bool f; proven_size_t sl = ir_local_find(c, node->tok.lex, &f);
    if (!f) return 0;
    return c->locals[sl].bset_w;
}
 void ir_bset_context(ir_ctx_t *c, const low_cst_t *f, proven_size_t start, proven_size_t end) {
    c->bset_w = 0;
    for (proven_size_t i = start; i + 1 < end && i + 1 < f->nkids; i++) {
        if (!is_atom(f->kids[i]) || !veq(f->kids[i]->tok.lex, "bitset")) continue;
        proven_i64 n = 0;
        if (is_atom(f->kids[i + 1]) && ir_int_lit(f->kids[i + 1]->tok.lex, &n) && n >= 1 && n <= 64)
            c->bset_w = (proven_u8)n;
        return;
    }
}
 void ir_vec_context(ir_ctx_t *c, const low_cst_t *f, proven_size_t start, proven_size_t end) {
    c->vec_lanes = 0; c->vec_esz = 0; c->vec_flt = false; c->vec_sign = false;
    for (proven_size_t i = start; i + 2 < end && i + 2 < f->nkids; i++) {
        if (!is_atom(f->kids[i]) || !veq(f->kids[i]->tok.lex, "vec")) continue;
        if (!is_atom(f->kids[i + 1])) return;
        proven_u8 esz = ir_field_size(f->kids[i + 1]->tok.lex);
        proven_i64 n = 0;
        // ★★★ **`vec t scalable`** (RFC-0040 D7) — 폭을 **소스에 안 박는다**. 길이-불가지 코드:
        //   같은 소스가 target 의 네이티브 폭으로 선다(x86_64/arm64 simd_bytes=16 → u8 16 · u32 4 레인,
        //   SIMD 없는 target → 1 = scalar fallback). 폭은 **컴파일 시 target 이 정한다** ⇒ VM·C 가 같은
        //   값을 굽는다 ⇒ 오라클 안전. (하드웨어가 런타임에 폭을 바꾸는 진짜 SVE/RVV 는 코드젠 몫이다.)
        if (is_atom(f->kids[i + 2]) && veq(f->kids[i + 2]->tok.lex, "scalable")) {
            n = ir_native_lanes_of(esz);   // ★ scalable = `(native_lanes t)` 와 같은 값(개념 통합)
        } else if (!ir_fold_lane_expr(c, f->kids[i + 2], &n)) return;   // ★ comptime 식 접기(RFC-0021)
        if (esz && n >= 1 && n <= 16 && (n & (n - 1)) == 0) {
            c->vec_lanes = (proven_u8)n; c->vec_esz = esz;
            c->vec_flt = ir_is_float_ty(f->kids[i + 1]->tok.lex);
            c->vec_sign = ity_of_word(f->kids[i + 1]->tok.lex).sign;   // ★ vec iN — 부호형 레인
        }
        return;
    }
}

// one access link: a bare non-local name selects a field; anything else indexes

// `make T do field value . … end` — the parser hands `make` an argument form
// FORM[T, BLOCK{field-forms}] (headed block); register the site and lower fields.
static void ir_make(ir_ctx_t *c, const low_cst_t *arg, proven_u32 line) {
    const low_cst_t *form = arg;
    if (form && form->kind == LOW_CST_GROUP && form->nkids == 1) form = form->kids[0];
    if (!form || form->kind != LOW_CST_FORM || form->nkids < 2 || !is_atom(form->kids[0]) ||
        form->kids[form->nkids - 1]->kind != LOW_CST_BLOCK) {
        ir_fail(c, "E-IR-UNSUP", "malformed make literal", line);
        return;
    }
    if (c->out->nmakes >= IR_MAXMAKES) { ir_fail(c, "E-IR-UNSUP", "too many make sites", line); return; }
    proven_size_t my = c->out->nmakes++;   // reserve first: field values may nest more makes
    low_ir_make_t mk = { .type_name = form->kids[0]->tok.lex, .nfields = 0 };
    const low_cst_t *blk = form->kids[form->nkids - 1];
    for (proven_size_t i = 0; i < blk->nkids; i++) {
        const low_cst_t *fld = blk->kids[i];
        if (fld->kind != LOW_CST_FORM || fld->nkids < 2 || !is_atom(fld->kids[0]) ||
            mk.nfields >= IR_MAKE_MAXF) {
            ir_fail(c, "E-IR-UNSUP", "malformed make field", line);
            c->out->makes[my] = mk;
            return;
        }
        mk.fields[mk.nfields++] = fld->kids[0]->tok.lex;
        ir_run(c, fld->kids, 1, fld->nkids - 1);   // field value (declaration order)
    }
    c->out->makes[my] = mk;
    ir_emit(c, IRW_MAKE, (proven_i64)my);
}

// ★★ RFC-0135 S2a (D9) — **할당기 바이트에 짓는 구조체** `var q using al be pt lit pt do … end . else …`. 받은 바이트가
//   스택에 있다: 0 으로 채우고(IRW_BFILL) 배치를 얹은 뒤(IRW_VIEW — `view pt b` 와 같은 표현) 칸을 적은 차례로 쓴다.
//   바이트 배치가 없는 구조체(슬라이스 · owned · 배열 칸)는 담을 표현이 아직 없다 — 정직하게 거절한다.
static proven_size_t ir_hidden_local(ir_ctx_t *c, proven_u32 line);
// ★★ RFC-0135 D13 (소유자 «슬라이스 칸만 연다») — 슬라이스 칸을 가진 구조체의 **할당기 배치**. 칸마다 원래 배치 규칙을 따르고,
//   슬라이스 칸만 (주소, 길이) 16 바이트(정렬 8)다 — C 의 `struct { const T *p; size_t n; }`. `owned`·actor·cap 칸, 원소를
//   모르는 슬라이스(구조체의 슬라이스), 배치 없는 안쪽 구조체가 있으면 짓지 못한다(-1). 한 구조체에 그림자 하나.
proven_i32 ir_alloc_shadow(low_ir_t *ir, proven_size_t si) {
    low_ir_struct_t *s = &ir->structs[si];
    if (s->viewable) return (proven_i32)si;
    if (s->ashadow1) return (proven_i32)s->ashadow1 - 1;
    if (s->packed || s->is_mmio || s->is_reserve || s->is_actor_state || !s->nf) return -1;
    for (proven_size_t q = 0; q < s->nf; q++) {
        const low_ir_sfield_t *f = &s->f[q];
        if (f->owned || f->capkind) return -1;
        if (f->arrn || f->slmeta || f->sidx >= 0) continue;
        if (f->boxed || !f->size) return -1;
    }
    if (ir->nstructs >= 32) return -1;            // ★ 구조체 표는 32 칸이다(low_ir_build 의 잡은 수)
    proven_size_t ni = ir->nstructs++;
    ir->structs[ni] = *s;
    low_ir_struct_t *g = &ir->structs[ni];
    g->is_shadow = true; g->ashadow1 = 0; g->align = 0;
    for (proven_size_t q = 0; q < g->nf; q++)
        if (g->f[q].slmeta) { g->f[q].boxed = false; g->f[q].size = (proven_u8)(low_ptr_slot() + 8); }   // ★ (포인터 칸, 길이 8) — RFC-0137 P3
    ir_struct_layout(ir, g);
    if (!g->viewable) { ir->nstructs--; return -1; }
    ir->structs[si].ashadow1 = (proven_u16)(ni + 1);
    return (proven_i32)ni;
}
static void ir_lit_struct_into(ir_ctx_t *c, const low_cst_t *arg, proven_u32 line) {
    const low_cst_t *form = arg;
    while (form && form->kind == LOW_CST_GROUP && form->nkids == 1) form = form->kids[0];
    if (!form || form->kind != LOW_CST_FORM || form->nkids != 2 || !is_atom(form->kids[0]) || form->kids[1]->kind != LOW_CST_BLOCK) {
        ir_fail(c, "E-LIT-UNBUILT", "malformed struct literal in an allocator's bytes", line);
        return;
    }
    bool sfound; proven_size_t si = ir_struct_find(c->out, ir_strip_mod(c, form->kids[0]->tok.lex), &sfound);
    proven_i32 gi = sfound ? ir_alloc_shadow(c->out, si) : -1;
    if (gi < 0) {
        ir_fail(c, "E-LIT-UNBUILT", "a struct built in an allocator's bytes holds sized numbers, bools, arrays, structs with a byte "
                "layout and slices of numbers (each slice as an address and a length) — an `owned`, actor or capability field "
                "has no place there, because the bytes would have to be finished with it (RFC-0135 D9 · D13)", line);
        return;
    }
    si = (proven_size_t)gi;
    proven_size_t tl = ir_hidden_local(c, line);
    if (tl == (proven_size_t)-1) return;
    ir_emit(c, IRW_BFILL, (proven_i64)((proven_u64)c->out->structs[si].total << 32));
    ir_emit(c, IRW_VIEW, (proven_i64)si);
    ir_emit(c, IRW_STORE, (proven_i64)tl);
    const low_cst_t *blk = form->kids[1];
    for (proven_size_t i = 0; i < blk->nkids && !c->failed; i++) {
        const low_cst_t *fld = blk->kids[i];
        if (fld->kind != LOW_CST_FORM || fld->nkids < 2 || !is_atom(fld->kids[0])) { ir_fail(c, "E-IR-UNSUP", "malformed struct field", line); return; }
        ir_emit(c, IRW_LOAD, (proven_i64)tl);
        ir_run(c, fld->kids, 1, fld->nkids - 1);
        ir_emit(c, IRW_FSTORE, (proven_i64)ir_field_intern(c, fld->kids[0]->tok.lex));
    }
    ir_emit(c, IRW_LOAD, (proven_i64)tl);
}

// ★★ RFC-0132 T2b-1 — **상수 배열·슬라이스 리터럴(§13.2 ⓑ)**: `lit array T N v… [_] .` · `lit slice T v… .`.
//   원소를 T 의 폭으로 **작은 끝(little-endian)** 에 싸서 문자열 상수 풀에 넣는다 — 같은 바이트면 한 자리를 쓴다
//   (L4: 같은 내용의 전역 리터럴은 같은 자리일 수도 있다 — 합치는 곳은 이 풀 한 곳이고, VM 과 C 뒤끝이 같이 쓴다).
//   u8 이면 바이트 슬라이스 그대로, 그 밖이면 `view_array` 와 같은 타입 보기(IRW_VARRAY)를 씌운다.
//   원소 검증(개수 · 폭 · 상수인가)은 검사층(low_check `ck_lit_walk`)이 먼저 한다 — 여기는 믿되 막는다.
// ★ 상수 원소 하나를 T 의 폭 비트로(작은 끝은 부르는 쪽이 싼다). 상수가 아니면 false.
static bool ir_lit_const_bits(const low_cst_t *e, bool flt, proven_u8 esz, proven_u64 *bits) {
    if (!is_atom(e)) return false;
    if (e->tok.kw == LOW_KW_TRUE) { *bits = 1; return true; }
    if (e->tok.kw == LOW_KW_FALSE) { *bits = 0; return true; }
    if (e->tok.kind != LOW_TOK_NUMBER) return false;
    if (flt) {
        double dv = low_num_to_double(e->tok.lex);
        if (esz == 4) { float fv = (float)dv; proven_u32 b32; memcpy(&b32, &fv, 4); *bits = b32; }
        else memcpy(bits, &dv, 8);
        return true;
    }
    proven_i64 v;
    if (!ir_int_lit(e->tok.lex, &v)) return false;
    *bits = (proven_u64)v;
    return true;
}
// ★ 바이트 줄을 상수 풀에 넣는다 — 같은 바이트면 한 자리(L4, §13.3: 합치는 곳은 이 풀 한 곳).
static bool ir_intern_bytes(ir_ctx_t *c, const proven_u8 *src, proven_size_t len, proven_size_t *si_out, proven_u32 line) {
    low_ir_t *ir = c->out;
    for (proven_size_t i2 = 0; i2 < ir->nstrs; i2++)
        if (ir->strew[i2] == 1 && ir->strs[i2].size == len && (len == 0 || memcmp(ir->strs[i2].ptr, src, len) == 0)) { *si_out = i2; return true; }
    if (ir->strbuf_len + len > ir->strbuf_cap) { ir_fail(c, "E-IR-UNSUP", "too many bytes of literal data in one unit", line); return false; }
    if (ir->nstrs >= IR_MAXSTRS) { ir_fail(c, "E-IR-UNSUP", "too many string literals", line); return false; }
    proven_u8 *dst = ir->strbuf + ir->strbuf_len;
    if (len) memmove(dst, src, len);
    ir->strbuf_len += len;
    ir->strew[ir->nstrs] = 1;
    ir->strs[ir->nstrs] = (proven_u8str_view_t){ .ptr = dst, .size = len };
    *si_out = ir->nstrs++;
    return true;
}
static proven_size_t ir_hidden_local(ir_ctx_t *c, proven_u32 line) {
    if (c->nlocals >= IR_MAXLOCALS) { ir_fail(c, "E-IR-LOCALS", "list literal: too many locals", line); return (proven_size_t)-1; }
    proven_size_t tl = c->nlocals++;
    c->locals[tl].name = (proven_u8str_view_t){ 0 };
    return tl;
}
static void ir_cell_value(ir_ctx_t *c, const low_cst_t *cf) {
    if (cf->nkids == 2) ir_node(c, cf->kids[1]);
    else ir_run(c, cf->kids, 1, cf->nkids - 1);
}
// ★★ RFC-0132 T2b-3 §5.3 — **칸 골라 채우기** `lit array T N do <번호> <값> . … [_ <값> .] end`. 검사층
//   (`ck_lit_walk`)이 번호(상수 · 길이 안 · 한 번씩)와 «모든 칸이 정해졌나» 를 먼저 본다 — 여기는 믿되 막는다.
//   상수 칸과 상수 `_` 는 **본**(길이 N 의 바이트 줄) 하나로 모은다. 값이 모두 상수이고 쓸 자리를 요구받지 않았으면
//   그 본이 곧 읽기 전용 자리(ⓑ)다. 아니면 틀 안 자리(ⓐ·ⓒ)에 본을 베끼고(`IRW_LBUFC`, 본이 모두 0 이면 `IRW_LBUF`),
//   실행 중 값인 칸을 **적은 차례로** 쓴다. `_` 의 값이 실행 중 값이면 **한 번** 계산해 이름 붙은 칸을 뺀 칸마다
//   쓴다(이름 붙은 칸은 상수 가림 줄로 건너뛴다).
static void ir_lit_fill(ir_ctx_t *c, const low_cst_t *blk, proven_size_t n, proven_u8 esz, bool flt, bool typed,
                        proven_i64 vmeta, bool want_frame, bool into, proven_u32 line) {
    const low_cst_t **cell = n ? calloc(n, sizeof *cell) : NULL;
    proven_u8 *tbuf = n ? calloc(n, esz) : NULL;
    proven_u8 *mask = n ? calloc(n, 1) : NULL;
    if (n && (!cell || !tbuf || !mask)) { free(cell); free(tbuf); free(mask); ir_fail(c, "E-IR-UNSUP", "out of memory for a cell fill", line); return; }
    const low_cst_t *rest = NULL;
    bool all_const = true, named = false;
    for (proven_size_t q = 0; q < blk->nkids; q++) {
        const low_cst_t *cf = blk->kids[q];
        if (cf->kind != LOW_CST_FORM || cf->nkids < 2 || !is_atom(cf->kids[0])) { ir_fail(c, "E-LIT-INDEX", "malformed cell fill", line); goto done; }
        proven_u64 bits = 0;
        bool k_const = cf->nkids == 2 && ir_lit_const_bits(cf->kids[1], flt, esz, &bits);
        if (!k_const) all_const = false;
        if (veq(cf->kids[0]->tok.lex, "_")) { rest = cf; continue; }
        proven_i64 m;
        if (!ir_int_lit(cf->kids[0]->tok.lex, &m) || m < 0 || (proven_size_t)m >= n || cell[m]) { ir_fail(c, "E-LIT-INDEX", "bad cell index", line); goto done; }
        cell[m] = cf; mask[m] = 1; named = true;
        if (k_const) for (proven_u8 b = 0; b < esz; b++) tbuf[(proven_size_t)m * esz + b] = (proven_u8)(bits >> (8 * b));
    }
    {
        proven_u64 rbits = 0;
        bool rest_const = rest && rest->nkids == 2 && ir_lit_const_bits(rest->kids[1], flt, esz, &rbits);
        if (rest_const)
            for (proven_size_t m = 0; m < n; m++)
                if (!cell[m]) for (proven_u8 b = 0; b < esz; b++) tbuf[m * esz + b] = (proven_u8)(rbits >> (8 * b));
        for (proven_size_t m = 0; m < n; m++) if (!cell[m] && !rest) { ir_fail(c, "E-LIT-COUNT", "unfilled cell and no `_ <value> .`", line); goto done; }
        proven_size_t bytes = n * esz, si;
        if (all_const && !want_frame && !into) {                          // ⓑ 읽기 전용 자리
            if (!ir_intern_bytes(c, tbuf, bytes, &si, line)) goto done;
            ir_emit(c, IRW_STR, (proven_i64)si);
            if (typed) ir_emit(c, IRW_VARRAY, vmeta);
            goto done;
        }
        bool zero = true;
        for (proven_size_t b = 0; b < bytes; b++) if (tbuf[b]) { zero = false; break; }
        if (!zero && !ir_intern_bytes(c, tbuf, bytes, &si, line)) goto done;
        proven_size_t tl;
        if (into) {                                                       // §13.7 — 할당자가 준 바이트(이미 스택에)
            tl = ir_hidden_local(c, line);
            if (tl == (proven_size_t)-1) goto done;
            ir_emit(c, IRW_BFILL, (proven_i64)(((proven_u64)bytes << 32) | (zero ? 0u : (proven_u64)si + 1u)));
        } else {
            proven_size_t off = (c->lbuf_off + 7u) & ~(proven_size_t)7u;
            if (off + bytes > low_lbuf_max()) { ir_fail(c, "E-FRAME-SIZE", "list literals need more frame bytes than one op may hold", line); goto done; }
            c->lbuf_off = off + bytes;
            tl = ir_hidden_local(c, line);
            if (tl == (proven_size_t)-1) goto done;
            if (zero) ir_emit(c, IRW_LBUF, (proven_i64)((proven_u64)off | ((proven_u64)bytes << 32)));
            else ir_emit(c, IRW_LBUFC, (proven_i64)((proven_u64)off | ((proven_u64)si << 32)));
        }
        if (typed) ir_emit(c, IRW_VARRAY, vmeta);
        ir_emit(c, IRW_STORE, (proven_i64)tl);
        for (proven_size_t q = 0; q < blk->nkids && !c->failed; q++) {   // 실행 중 값인 칸 — 적은 차례로
            const low_cst_t *cf = blk->kids[q];
            proven_u64 bits;
            if (cf == rest || (cf->nkids == 2 && ir_lit_const_bits(cf->kids[1], flt, esz, &bits))) continue;
            proven_i64 m; (void)ir_int_lit(cf->kids[0]->tok.lex, &m);
            ir_emit(c, IRW_LOAD, (proven_i64)tl);
            ir_emit(c, IRW_CONST, m);
            ir_cell_value(c, cf);
            ir_emit(c, IRW_ISTORE, 0);
        }
        if (rest && !rest_const && !c->failed) {                          // `_ <실행 중 값> .` — 한 번 계산해 남은 칸마다
            proven_size_t rv = ir_hidden_local(c, line), iv = ir_hidden_local(c, line);
            if (c->failed) goto done;
            ir_cell_value(c, rest);
            ir_emit(c, IRW_STORE, (proven_i64)rv);
            ir_emit(c, IRW_CONST, 0); ir_emit(c, IRW_STORE, (proven_i64)iv);
            proven_size_t si_m = 0;
            if (named && !ir_intern_bytes(c, mask, n, &si_m, line)) goto done;
            proven_size_t top = c->code.len;
            ir_emit(c, IRW_LOAD, (proven_i64)iv); ir_emit(c, IRW_CONST, (proven_i64)n); ir_emit(c, IRW_LT, 0);
            proven_size_t out_br = ir_emit(c, IRW_BRZ, 0);
            proven_size_t skip_br = (proven_size_t)-1;
            if (named) {
                ir_emit(c, IRW_STR, (proven_i64)si_m); ir_emit(c, IRW_LOAD, (proven_i64)iv); ir_emit(c, IRW_INDEX, 0);
                ir_emit(c, IRW_CONST, 0); ir_emit(c, IRW_NE, 0);
                proven_size_t go = ir_emit(c, IRW_BRZ, 0);                // 가림이 0 이면 쓴다
                skip_br = ir_emit(c, IRW_BR, 0);                          // 이름 붙은 칸 — 건너뛴다
                ir_at(c, go)->a = (proven_i64)c->code.len;
            }
            ir_emit(c, IRW_LOAD, (proven_i64)tl); ir_emit(c, IRW_LOAD, (proven_i64)iv); ir_emit(c, IRW_LOAD, (proven_i64)rv);
            ir_emit(c, IRW_ISTORE, 0);
            if (skip_br != (proven_size_t)-1) ir_at(c, skip_br)->a = (proven_i64)c->code.len;
            ir_emit(c, IRW_LOAD, (proven_i64)iv); ir_emit(c, IRW_CONST, 1); ir_emit(c, IRW_ADD, 0); ir_emit(c, IRW_STORE, (proven_i64)iv);
            ir_emit(c, IRW_BR, (proven_i64)top);
            ir_at(c, out_br)->a = (proven_i64)c->code.len;
        }
        ir_emit(c, IRW_LOAD, (proven_i64)tl);
    }
done:
    free(cell); free(tbuf); free(mask);
}
static void ir_lit_list_in(ir_ctx_t *c, low_cst_t *const *k, proven_size_t *pos, proven_size_t end, proven_u32 line, bool as_array);
// ★ RFC-0132 T2b-3 — `lit vec T N v… .` 은 같은 바이트를 배열로 짓고(ⓑ 상수 풀, 실행 중 원소면 틀 안) 그 줄에서
//   레인을 싣는다(`load` 와 같은 명령, 레인 모양은 리터럴 자신의 타입에서 — 묶는 자리의 타입을 기다리지 않는다).
static void ir_lit_list(ir_ctx_t *c, low_cst_t *const *k, proven_size_t *pos, proven_size_t end, proven_u32 line) {
    bool is_vec = *pos + 2 < end && is_atom(k[*pos]) && veq(k[*pos]->tok.lex, "vec") && is_atom(k[*pos + 1]) && is_atom(k[*pos + 2]);
    if (!is_vec) { ir_lit_list_in(c, k, pos, end, line, false); return; }
    proven_u8str_view_t ty = k[*pos + 1]->tok.lex;
    proven_i64 lanes = 0;
    proven_u8 esz = ir_field_size(ty);
    if (!esz || !ir_int_lit(k[*pos + 2]->tok.lex, &lanes) || lanes < 1 || lanes > 16 || (lanes & (lanes - 1))) {
        ir_fail(c, "E-LIT-COUNT", "`lit vec T N` needs a sized element type and 1..16 lanes (a power of two)", line); return;
    }
    proven_i64 meta = (lanes << 8) | esz | (ir_is_float_ty(ty) ? IR_FLT_BIT : 0) | (ity_of_word(ty).sign ? IR_SGN_BIT : 0);
    c->lit_frame = false;                                          // 값은 레인으로 베껴진다 — 쓸 자리가 필요 없다
    ir_lit_list_in(c, k, pos, end, line, true);
    if (c->failed) return;
    ir_emit(c, IRW_CONST, 0);
    ir_emit(c, IRW_VLOAD, meta);
}
// ★★ RFC-0132 T2b-3d (N1) — **구조체 원소 나열** `lit array pt N <값>… [_] .` · `lit array pt N do <번호> <값> . _ <값> . end` ·
//   `lit slice pt <값>… .`. 원소는 바이트 배치가 있는 구조체(칸이 모두 크기 있는 수)이고, 자리는 스칼라 나열과 같다(틀 · 할당기 ·
//   모두 `_` 면 읽기 전용 0). 바이트 위에 `view_array pt` 와 같은 보기(IRW_VARRAY + 구조체 표시)를 얹고, 원소마다 값을 한 번
//   계산해 칸을 베껴 쓴다(구조체는 값이다 — 원소는 그 값의 사본이다).
static void ir_lit_struct_list(ir_ctx_t *c, low_cst_t *const *k, proven_size_t *pos, proven_size_t end, proven_u32 line,
                               proven_size_t si, bool is_array, bool want_frame, bool into) {
    const low_ir_struct_t *st = &c->out->structs[si];
    if (!st->viewable || !st->total || st->total > 255) {
        ir_fail(c, "E-LIT-UNBUILT", "the elements of a list literal must have a byte layout — a struct with a slice, `owned` or "
                "array field (or larger than 255 bytes) cannot be laid out in a list yet (RFC-0132 T2b-3d)", line);
        return;
    }
    for (proven_size_t q = 0; q < st->nf; q++)
        if (st->f[q].sidx >= 0) { ir_fail(c, "E-LIT-UNBUILT", "a struct element whose field is itself a struct is not laid out in a list yet (RFC-0132 T2b-3d)", line); return; }
    proven_size_t esz = st->total;
    proven_i64 vmeta = (proven_i64)esz | IR_STRUCT_BIT | ((proven_i64)si << 20);
    proven_i64 n = -1;
    if (is_array) {
        if (*pos >= end || !is_atom(k[*pos]) || !ir_int_lit(k[*pos]->tok.lex, &n) || n < 0) { ir_fail(c, "E-LIT-COUNT", "`lit array T N …` needs its length N as a literal", line); return; }
        (*pos)++;
    }
    const low_cst_t *blk = (*pos < end && k[end - 1]->kind == LOW_CST_BLOCK) ? k[end - 1] : NULL;
    proven_size_t e0 = *pos, cnt = 0;
    bool any_value = blk != NULL;
    if (!blk)
        for (proven_size_t q = e0; q < end; q++) {
            if (is_atom(k[q]) && k[q]->tok.kind == LOW_TOK_IDENT && veq(k[q]->tok.lex, "_")) continue;
            cnt++; any_value = true;
        }
    proven_size_t cap_elems = is_array ? (proven_size_t)n : cnt;
    if (cnt > cap_elems) { ir_fail(c, "E-LIT-COUNT", "more elements than the length", line); return; }
    proven_size_t bytes = cap_elems * esz;
    *pos = end;
    if (!any_value && !want_frame && !into) {                      // ⓑ 모두 `_` — 읽기 전용 0 바이트
        proven_u8 *z = bytes ? calloc(bytes, 1) : NULL;
        if (bytes && !z) { ir_fail(c, "E-IR-UNSUP", "out of memory for a list literal", line); return; }
        proven_size_t sidx;
        bool ok = ir_intern_bytes(c, z, bytes, &sidx, line);
        free(z);
        if (!ok) return;
        ir_emit(c, IRW_STR, (proven_i64)sidx);
        ir_emit(c, IRW_VARRAY, vmeta);
        return;
    }
    if (into) ir_emit(c, IRW_BFILL, (proven_i64)((proven_u64)bytes << 32));
    else {
        proven_size_t off = (c->lbuf_off + 7u) & ~(proven_size_t)7u;
        if (off + bytes > low_lbuf_max()) { ir_fail(c, "E-FRAME-SIZE", "list literals need more frame bytes than one op may hold", line); return; }
        c->lbuf_off = off + bytes;
        ir_emit(c, IRW_LBUF, (proven_i64)((proven_u64)off | ((proven_u64)bytes << 32)));
    }
    ir_emit(c, IRW_VARRAY, vmeta);
    proven_size_t tl = ir_hidden_local(c, line), tv = ir_hidden_local(c, line);
    if (c->failed) return;
    ir_emit(c, IRW_STORE, (proven_i64)tl);
    #define LSL_PUT(cell) do { for (proven_size_t q_ = 0; q_ < st->nf && !c->failed; q_++) { \
            proven_size_t fid_ = ir_field_intern(c, st->f[q_].name); \
            ir_emit(c, IRW_LOAD, (proven_i64)tl); ir_emit(c, IRW_CONST, (proven_i64)(cell)); ir_emit(c, IRW_INDEX, 0); \
            ir_emit(c, IRW_LOAD, (proven_i64)tv); ir_emit(c, IRW_FIELD, (proven_i64)fid_); ir_emit(c, IRW_FSTORE, (proven_i64)fid_); } } while (0)
    if (blk) {                                                     // 칸 골라 채우기 — 검사층이 번호 · 모든 칸을 먼저 본다
        unsigned char *named = cap_elems ? calloc(cap_elems, 1) : NULL;
        if (cap_elems && !named) { ir_fail(c, "E-IR-UNSUP", "out of memory for a cell fill", line); return; }
        const low_cst_t *rest = NULL;
        for (proven_size_t q = 0; q < blk->nkids && !c->failed; q++) {
            const low_cst_t *cf = blk->kids[q];
            if (cf->kind != LOW_CST_FORM || cf->nkids < 2 || !is_atom(cf->kids[0])) { ir_fail(c, "E-LIT-INDEX", "malformed cell fill", line); break; }
            if (veq(cf->kids[0]->tok.lex, "_")) { rest = cf; continue; }
            proven_i64 m;
            if (!ir_int_lit(cf->kids[0]->tok.lex, &m) || m < 0 || (proven_size_t)m >= cap_elems || named[m]) { ir_fail(c, "E-LIT-INDEX", "bad cell index", line); break; }
            named[m] = 1;
            ir_cell_value(c, cf);
            ir_emit(c, IRW_STORE, (proven_i64)tv);
            LSL_PUT(m);
        }
        if (rest && !c->failed) {
            ir_cell_value(c, rest);
            ir_emit(c, IRW_STORE, (proven_i64)tv);
            for (proven_size_t m = 0; m < cap_elems && !c->failed; m++) if (!named[m]) LSL_PUT(m);
        }
        free(named);
    } else {
        proven_size_t m = 0;
        for (proven_size_t q = e0; q < end && !c->failed; q++) {
            const low_cst_t *e = k[q];
            if (is_atom(e) && e->tok.kind == LOW_TOK_IDENT && veq(e->tok.lex, "_")) continue;
            ir_node(c, e);
            ir_emit(c, IRW_STORE, (proven_i64)tv);
            LSL_PUT(m);
            m++;
        }
    }
    #undef LSL_PUT
    ir_emit(c, IRW_LOAD, (proven_i64)tl);
}
// ★★ RFC-0132 §13.10 (소유자 «줄 보기») — **줄의 나열** `lit array (array T M) N <줄>… [_] .`. 바이트는 N×M×|T| 한 덩어리이고
//   위에 줄의 배열 보기(IRW_VARRAY + IR_ROW_BIT)를 얹는다 — `index g r` 가 그 줄을 보는 슬라이스다. 줄 값(`lit array T M …` ·
//   이름)은 칸마다 베낀다(줄은 값의 사본). `_` 줄은 0. 칸 베끼기는 펼쳐 적으므로 한 나열에 1024 칸까지만 받는다.
static void ir_lit_row_list(ir_ctx_t *c, low_cst_t *const *k, proven_size_t *pos, proven_size_t end, proven_u32 line,
                            proven_u8str_view_t ity, proven_size_t m, bool is_array, bool want_frame, bool into) {
    proven_u8 ie = ir_field_size(ity);
    if (!ie) { ir_fail(c, "E-LIT-UNBUILT", "a row's element type must be a sized number or `bool` here (RFC-0132 §13.10)", line); return; }
    bool flt = ir_is_float_ty(ity), sgn = ity_of_word(ity).sign;
    proven_size_t rb = m * ie;
    if (rb > 0xffffffu) { ir_fail(c, "E-LIT-UNBUILT", "a row this long cannot be laid out in a list", line); return; }
    proven_i64 n = -1;
    if (is_array) {
        if (*pos >= end || !is_atom(k[*pos]) || !ir_int_lit(k[*pos]->tok.lex, &n) || n < 0) { ir_fail(c, "E-LIT-COUNT", "`lit array (array T M) N …` needs its length N as a literal", line); return; }
        (*pos)++;
    }
    if (*pos < end && k[end - 1]->kind == LOW_CST_BLOCK) { ir_fail(c, "E-LIT-UNBUILT", "filling chosen rows (`do … end`) is not built yet — list the rows (RFC-0132 §13.10)", line); return; }
    proven_size_t e0 = *pos, cnt = 0, stores = 0;
    for (proven_size_t q = e0; q < end; q++) if (!(is_atom(k[q]) && veq(k[q]->tok.lex, "_"))) cnt++;
    proven_size_t rows = is_array ? (proven_size_t)n : cnt;
    if (cnt > rows) { ir_fail(c, "E-LIT-COUNT", "more rows than the length", line); return; }
    stores = cnt * m;
    if (stores > 1024) { ir_fail(c, "E-LIT-UNBUILT", "a list of rows copies at most 1024 cells from its row values — build the big rows with `_` and fill the cells (RFC-0132 §13.10)", line); return; }
    proven_size_t bytes = rows * rb;
    proven_i64 vmeta = IR_ROW_BIT | (proven_i64)ie | (flt ? IR_FLT_BIT : 0) | (sgn ? IR_SGN_BIT : 0) | (proven_i64)((proven_u64)rb << 32);
    *pos = end;
    if (!cnt && !want_frame && !into) {                            // ⓑ 모두 `_` — 읽기 전용 0
        proven_u8 *z = bytes ? calloc(bytes, 1) : NULL;
        if (bytes && !z) { ir_fail(c, "E-IR-UNSUP", "out of memory for a list literal", line); return; }
        proven_size_t sidx; bool ok = ir_intern_bytes(c, z, bytes, &sidx, line); free(z);
        if (!ok) return;
        ir_emit(c, IRW_STR, (proven_i64)sidx);
        ir_emit(c, IRW_VARRAY, vmeta);
        return;
    }
    if (into) ir_emit(c, IRW_BFILL, (proven_i64)((proven_u64)bytes << 32));
    else {
        proven_size_t off = (c->lbuf_off + 7u) & ~(proven_size_t)7u;
        if (off + bytes > low_lbuf_max()) { ir_fail(c, "E-FRAME-SIZE", "list literals need more frame bytes than one op may hold", line); return; }
        c->lbuf_off = off + bytes;
        ir_emit(c, IRW_LBUF, (proven_i64)((proven_u64)off | ((proven_u64)bytes << 32)));
    }
    ir_emit(c, IRW_VARRAY, vmeta);
    proven_size_t tl = ir_hidden_local(c, line), tv = ir_hidden_local(c, line);
    if (c->failed) return;
    ir_emit(c, IRW_STORE, (proven_i64)tl);
    proven_size_t r = 0;
    for (proven_size_t q = e0; q < end && !c->failed; q++) {
        const low_cst_t *e = k[q];
        if (is_atom(e) && veq(e->tok.lex, "_")) continue;
        ir_node(c, e);
        ir_emit(c, IRW_STORE, (proven_i64)tv);
        for (proven_size_t j = 0; j < m && !c->failed; j++) {       // 줄 r 의 칸 j ← 값의 칸 j
            ir_emit(c, IRW_LOAD, (proven_i64)tl); ir_emit(c, IRW_CONST, (proven_i64)r); ir_emit(c, IRW_INDEX, 0);
            ir_emit(c, IRW_CONST, (proven_i64)j);
            ir_emit(c, IRW_LOAD, (proven_i64)tv); ir_emit(c, IRW_CONST, (proven_i64)j); ir_emit(c, IRW_INDEX, 0);
            ir_emit(c, IRW_ISTORE, 0);
        }
        r++;
    }
    ir_emit(c, IRW_LOAD, (proven_i64)tl);
}
static void ir_lit_list_in(ir_ctx_t *c, low_cst_t *const *k, proven_size_t *pos, proven_size_t end, proven_u32 line, bool as_array) {
    bool want_frame = c->lit_frame; c->lit_frame = false;   // ★ 부르는 쪽의 요구는 이 나열 하나에만
    bool into = c->lit_into; c->lit_into = false;            // ★ §13.7 — 채울 바이트가 이미 스택에 있다
    if (into) want_frame = true;
    // ★ RFC-0132 §13.10 — 원소가 줄(`(array T M)`)인 나열
    if (*pos + 1 < end && is_atom(k[*pos]) && !is_atom(k[*pos + 1])) {
        const low_cst_t *rt = k[*pos + 1];
        while (rt && rt->kind == LOW_CST_GROUP && rt->nkids == 1) rt = rt->kids[0];
        proven_i64 rm;
        if (rt && rt->kind == LOW_CST_FORM && rt->nkids == 3 && is_atom(rt->kids[0]) && veq(rt->kids[0]->tok.lex, "array") &&
            is_atom(rt->kids[1]) && is_atom(rt->kids[2]) && ir_int_lit(rt->kids[2]->tok.lex, &rm) && rm > 0) {
            bool ra = as_array || veq(k[*pos]->tok.lex, "array");
            *pos += 2;
            ir_lit_row_list(c, k, pos, end, line, rt->kids[1]->tok.lex, (proven_size_t)rm, ra, want_frame, into);
            return;
        }
    }
    if (*pos + 1 >= end || !is_atom(k[*pos]) || !is_atom(k[*pos + 1])) { ir_fail(c, "E-LIT-UNBUILT", "malformed list literal", line); return; }
    bool is_array = as_array || veq(k[*pos]->tok.lex, "array"), is_slice = veq(k[*pos]->tok.lex, "slice");
    if (!is_array && !is_slice) { ir_fail(c, "E-LIT-UNBUILT", "unknown list literal", line); return; }
    proven_u8str_view_t ty = k[*pos + 1]->tok.lex;
    proven_u8 esz = ir_field_size(ty);
    if (!esz) {
        bool sf; proven_size_t si = ir_struct_find(c->out, ir_strip_mod(c, ty), &sf);
        if (sf) { *pos += 2; ir_lit_struct_list(c, k, pos, end, line, si, is_array, want_frame, into); return; }
        ir_fail(c, "E-LIT-UNBUILT", "a list literal's element type must be a sized scalar or a struct with a byte layout (RFC-0132 T2b-3d)", line);
        return;
    }
    bool flt = ir_is_float_ty(ty), sgn = ity_of_word(ty).sign;
    proven_i64 vmeta = (proven_i64)esz | (flt ? IR_FLT_BIT : 0) | (sgn ? IR_SGN_BIT : 0);
    bool typed = esz > 1 || sgn || flt || veq(ty, "bool");
    *pos += 2;
    proven_i64 n = -1;
    if (is_array) {
        if (*pos >= end || !is_atom(k[*pos]) || !ir_int_lit(k[*pos]->tok.lex, &n) || n < 0) { ir_fail(c, "E-LIT-COUNT", "`lit array T N …` needs its length N as a literal", line); return; }
        (*pos)++;
        if (*pos < end && k[end - 1]->kind == LOW_CST_BLOCK) {   // 칸 골라 채우기(T2b-3)
            const low_cst_t *blk = k[end - 1];
            *pos = end;
            ir_lit_fill(c, blk, (proven_size_t)n, esz, flt, typed, vmeta, want_frame, into, line);
            return;
        }
    }
    proven_size_t e0 = *pos, cnt = 0;
    bool all_const = true;
    for (proven_size_t q = e0; q < end; q++) {
        const low_cst_t *e = k[q];
        if (is_atom(e) && e->tok.kind == LOW_TOK_IDENT && veq(e->tok.lex, "_")) continue;
        cnt++;
        if (!(is_atom(e) && (e->tok.kind == LOW_TOK_NUMBER || e->tok.kw == LOW_KW_TRUE || e->tok.kw == LOW_KW_FALSE))) all_const = false;
    }
    proven_size_t cap_elems = is_array ? (proven_size_t)n : cnt;
    if (cnt > cap_elems) { ir_fail(c, "E-LIT-COUNT", "more elements than the length", line); return; }
    // ★★ RFC-0132 T2b-2 — **틀 안 자리(§13.2 ⓐ·ⓒ)**: 실행 중 값이 섞였거나, 부르는 쪽이 쓸 수 있는 자리를 요구할 때
    //   (`var` 에 묶기 · `mut` 매개변수로 넘기기). op 의 틀에 선언 자리마다 한 칸을 잡고(8 바이트로 맞춤), 그 칸을
    //   0 으로 채운 쓸 수 있는 슬라이스를 얻은 뒤 원소를 하나씩 쓴다(0 인 상수 원소는 이미 0 이라 건너뛴다).
    if (!all_const || want_frame) {
        proven_size_t bytes = cap_elems * esz;
        if (c->nlocals >= IR_MAXLOCALS) { ir_fail(c, "E-IR-LOCALS", "list literal: too many locals", line); return; }
        if (into) ir_emit(c, IRW_BFILL, (proven_i64)((proven_u64)bytes << 32));   // §13.7 — 받은 바이트를 0 으로
        else {
            proven_size_t off = (c->lbuf_off + 7u) & ~(proven_size_t)7u;
            if (off + bytes > low_lbuf_max()) { ir_fail(c, "E-FRAME-SIZE", "list literals need more frame bytes than one op may hold", line); return; }
            c->lbuf_off = off + bytes;
            ir_emit(c, IRW_LBUF, (proven_i64)((proven_u64)off | ((proven_u64)bytes << 32)));
        }
        proven_size_t tl = c->nlocals++;
        c->locals[tl].name = (proven_u8str_view_t){ 0 };
        if (typed) ir_emit(c, IRW_VARRAY, vmeta);
        ir_emit(c, IRW_STORE, (proven_i64)tl);
        proven_size_t m = 0;
        for (*pos = e0; *pos < end && !c->failed; (*pos)++) {
            const low_cst_t *e = k[*pos];
            if (is_atom(e) && e->tok.kind == LOW_TOK_IDENT && veq(e->tok.lex, "_")) continue;
            bool zero = false;
            if (is_atom(e) && e->tok.kw == LOW_KW_FALSE) zero = true;
            else if (is_atom(e) && e->tok.kind == LOW_TOK_NUMBER) {
                proven_i64 v; zero = !flt && ir_int_lit(e->tok.lex, &v) && v == 0;
                if (flt) zero = low_num_to_double(e->tok.lex) == 0.0 && e->tok.lex.ptr[0] != '-';
            }
            if (!zero) {
                ir_emit(c, IRW_LOAD, (proven_i64)tl);
                ir_emit(c, IRW_CONST, (proven_i64)m);
                if (is_atom(e) && e->tok.kind == LOW_TOK_NUMBER && flt) {
                    double dv = low_num_to_double(e->tok.lex); proven_i64 bits; memcpy(&bits, &dv, 8);
                    ir_emit(c, IRW_FCONST, bits);
                } else if (is_atom(e) && e->tok.kw == LOW_KW_TRUE) ir_emit(c, IRW_CONST, 1);
                else ir_node(c, e);
                ir_emit(c, IRW_ISTORE, 0);
            }
            m++;
        }
        ir_emit(c, IRW_LOAD, (proven_i64)tl);
        return;
    }
    // ★★ ⓑ — **읽기 전용 상수**(T2b-1): 원소를 T 의 폭으로 작은 끝에 싸서 문자열 상수 풀에 넣는다 — 같은 바이트면
    //   한 자리(L4: 합치는 곳은 이 풀 한 곳이고 VM 과 C 뒤끝이 같이 쓴다).
    low_ir_t *ir = c->out;
    if (ir->strbuf_len + cap_elems * esz > ir->strbuf_cap) { ir_fail(c, "E-IR-UNSUP", "too many bytes of literal data in one unit", line); return; }
    proven_u8 *dst = ir->strbuf + ir->strbuf_len;
    proven_size_t m = 0;
    for (*pos = e0; *pos < end; (*pos)++) {
        const low_cst_t *e = k[*pos];
        if (is_atom(e) && e->tok.kind == LOW_TOK_IDENT && veq(e->tok.lex, "_")) {   // 나머지 칸은 0
            while (m < cap_elems) { memset(dst + m * esz, 0, esz); m++; }
            continue;
        }
        proven_u64 bits = 0;
        if (e->tok.kw == LOW_KW_TRUE) bits = 1;
        else if (e->tok.kw == LOW_KW_FALSE) bits = 0;
        else if (flt) {
            double dv = low_num_to_double(e->tok.lex);
            if (esz == 4) { float fv = (float)dv; proven_u32 b32; memcpy(&b32, &fv, 4); bits = b32; }
            else memcpy(&bits, &dv, 8);
        } else {
            proven_i64 v;
            if (!ir_int_lit(e->tok.lex, &v)) { ir_fail(c, "E-LIT-UNBUILT", "list literal element is not an integer literal", line); return; }
            bits = (proven_u64)v;
        }
        for (proven_u8 b = 0; b < esz; b++) dst[m * esz + b] = (proven_u8)(bits >> (8 * b));
        m++;
    }
    if (m != cap_elems) { ir_fail(c, "E-LIT-COUNT", "fewer elements than the length and no trailing `_`", line); return; }
    proven_u8str_view_t val = { .ptr = dst, .size = m * esz };
    proven_size_t si = ir->nstrs;
    for (proven_size_t i2 = 0; i2 < ir->nstrs; i2++)
        if (ir->strew[i2] == 1 && proven_u8str_view_eq(ir->strs[i2], val)) { si = i2; break; }
    if (si == ir->nstrs) {
        if (ir->nstrs >= IR_MAXSTRS) { ir_fail(c, "E-IR-UNSUP", "too many string literals", line); return; }
        ir->strbuf_len += val.size;
        ir->strew[ir->nstrs] = 1;
        ir->strs[ir->nstrs++] = val;
    }
    ir_emit(c, IRW_STR, (proven_i64)si);
    if (typed) ir_emit(c, IRW_VARRAY, vmeta);
}

// ★★★ **send 하강** — 값 `send`(동기, IRW_CALL)와 `spawn send`(async, IRW_ASEND)가 공유한다.
//   핸들러는 **수신자의 선언 타입** 안에서 찾는다(맨 이름이 아니라 — 두 액터가 같은 핸들러
//   이름을 가지면 조용히 갈리기 때문, DECISION-0011). 인스턴스가 슬롯 0, 인자가 뒤따른다.
static void ir_send(ir_ctx_t *c, low_cst_t *const *k, proven_size_t *pos, proven_size_t end,
                    proven_u32 line, bool async, bool try_send) {
    if (*pos + 1 >= end) { ir_fail(c, "E-IR-UNSUP", "send needs `<actor> <message> [args…]`", line); return; }
    const low_cst_t *tgt = k[(*pos)++];
    const low_cst_t *mnode = k[(*pos)++];
    if (!is_atom(mnode)) { ir_fail(c, "E-IR-UNSUP", "send needs `<actor> <message> [args…]`", line); return; }
    proven_size_t asx = (proven_size_t)-1;
    if (is_atom(tgt)) {
        bool lf; proven_size_t lsl = ir_local_find(c, tgt->tok.lex, &lf);
        if (lf && c->locals[lsl].tyname.size) {
            bool sf2; proven_size_t si2 = ir_struct_find(c->out, c->locals[lsl].tyname, &sf2);
            if (sf2) asx = si2;
        }
    }
    // ★★★★ **받는 쪽이 칸이면 그 칸의 타입으로 찾는다** (WO-0213 에서 찾음). `send (field g al) used` 처럼
    //   받는 actor 가 **묶음의 칸**이면 전엔 타입을 몰라 **이름으로 첫 처리기**를 집었다 — 한 모듈에 `used` 를 가진
    //   actor 가 둘이면(범프 · 힙) 남의 처리기가 불렸고, VM 은 «그런 칸이 없다» 로 멈췄다. 칸의 `sidx` 를 따른다.
    {
        const low_cst_t *fx = tgt;
        if (fx && fx->kind == LOW_CST_GROUP && fx->nkids) fx = fx->kids[0];
        if (asx == (proven_size_t)-1 && fx && fx->kind == LOW_CST_FORM && fx->nkids == 3 &&
            is_atom(fx->kids[0]) && veq(fx->kids[0]->tok.lex, "field") && is_atom(fx->kids[1]) && is_atom(fx->kids[2])) {
            bool lf; proven_size_t lsl = ir_local_find(c, fx->kids[1]->tok.lex, &lf);
            if (lf && c->locals[lsl].tyname.size) {
                bool sf2; proven_size_t si2 = ir_struct_find(c->out, c->locals[lsl].tyname, &sf2);
                if (sf2) {
                    const low_ir_struct_t *st = &c->out->structs[si2];
                    for (proven_size_t z = 0; z < st->nf; z++) {
                        if (!proven_u8str_view_eq(st->f[z].name, fx->kids[2]->tok.lex)) continue;
                        if (st->f[z].sidx >= 0) { asx = (proven_size_t)st->f[z].sidx; break; }
                        if (st->f[z].tyname.size) {
                            proven_u8str_view_t tn = st->f[z].tyname;
                            tn = low_view_after_last(tn, '.');
                            bool sf3; proven_size_t si3 = ir_struct_find(c->out, tn, &sf3);
                            if (sf3 && c->out->structs[si3].is_actor_state) asx = si3;
                        }
                        break;
                    }
                }
            }
        }
    }
    bool hf = false; proven_size_t hd = 0;
    for (proven_size_t q = 0; q < c->out->ndefs; q++) {
        const low_ir_def_t *dd = &c->out->defs[q];
        if (!dd->is_actor) continue;
        if (!proven_u8str_view_eq(dd->name, mnode->tok.lex)) continue;
        if (asx != (proven_size_t)-1 && dd->param_sidx[0] != (proven_u8)asx) continue;
        hf = true; hd = q; break;
    }
    if (!hf) {
        ir_fail(c, "E-IR-UNDEF",
                "send names a message this actor does not handle (the handler is looked up "
                "IN THE RECEIVER'S TYPE — it used to be found by BARE NAME, so two actors "
                "with a handler of the same name silently shared one)", line);
        return;
    }
    proven_size_t want = c->out->defs[hd].nparams;      // 인스턴스 + 인자들
    proven_size_t given = end - *pos;
    if (given + 1 != want) {
        ir_fail(c, "E-IR-ARITY",
                "this message was given the wrong number of arguments — an actor message IS "
                "an op call, and its `input …` clause says how many it takes", line);
        return;
    }
    ir_node(c, tgt);                       // 슬롯 0 — 받는 actor 인스턴스
    while (*pos < end) ir_node(c, k[(*pos)++]);
    if (try_send) {
        // ★ 회복 가능한 형태 — 트랩 대신 result 를 돌려준다. 에러명은 고정("mailbox_full") 이라
        //   `error_value` 가 뜻 있는 이름을 준다. a = 핸들러 | (에러명 인덱스 << 24).
        proven_size_t ei = ir_err_intern(c, proven_u8str_view_from_cstr("mailbox_full"));
        ir_emit(c, IRW_TASEND, (proven_i64)((proven_u64)hd | ((proven_u64)ei << 24)));
    } else {
        ir_emit(c, async ? IRW_ASEND : IRW_CALL, (proven_i64)hd);
    }
}

// ★ 파이프라인은 **식**이다(RFC-0010 §6.1) — 문장 자리(ir_stmt)와 식 자리(ir_value) 양쪽에서 쓴다.
 void ir_pipe(ir_ctx_t *c, const low_cst_t *f, proven_size_t first, bool as_value);
 void ir_value(ir_ctx_t *c, low_cst_t *const *k, proven_size_t *pos, proven_size_t end);

// ★★★ **페이로드 enum 구성** (RFC-0080 §4.3) — `<enum>.<variant> <payload-args>`.
//   변형 값 = **태그 필드 `$t` + 페이로드 필드를 가진 레코드**로 표현한다. 그래서 구성은
//   MAKE 하나, 해체(`get`)는 FIELD 하나로 낮아진다(§6: option/result 의 태그 유니온을 변형
//   인덱스로 일반화). 레코드라서 VM·네이티브가 **기존 경로로** 돈다 — 새 옵코드 0.
//   `$t` 는 사용자 필드가 될 수 없는 이름($ 는 식별자 문법 밖)이라 충돌 없음.
 const proven_byte_t IR_ENUM_TAGF[2] = { '$', 't' };
 bool ir_enum_variant_of(const ir_ctx_t *c, proven_u8str_view_t ehead,
                               proven_u8str_view_t vseg, proven_size_t *out_vi) {
    for (proven_size_t i = 0; i < c->nenumv; i++)
        if (proven_u8str_view_eq(c->enumv[i], vseg) &&
            c->enum_owner[i].size && proven_u8str_view_eq(c->enum_owner[i], ehead)) {
            *out_vi = i; return true;
        }
    return false;
}
// ★ 변형 narrowing 지원 (RFC-0080 §4.6). 변형 `v` 가 속한 enum 이름·그 enum 의 변형 수·
//   정확히 2-변형 enum 의 "다른" 변형(else 가지의 좁힘).
 proven_u8str_view_t ir_variant_owner(const ir_ctx_t *c, proven_u8str_view_t v) {
    for (proven_size_t i = 0; i < c->nenumv; i++)
        if (proven_u8str_view_eq(c->enumv[i], v)) return c->enum_owner[i];
    return (proven_u8str_view_t){0};
}
static proven_size_t ir_enum_nvariants(const ir_ctx_t *c, proven_u8str_view_t ename) {
    proven_size_t n = 0;
    if (!ename.size) return 0;
    for (proven_size_t i = 0; i < c->nenumv; i++)
        if (c->enum_owner[i].size && proven_u8str_view_eq(c->enum_owner[i], ename)) n++;
    return n;
}
 proven_u8str_view_t ir_enum_other2(const ir_ctx_t *c, proven_u8str_view_t ename,
                                          proven_u8str_view_t v) {
    proven_u8str_view_t other = {0}; proven_size_t n = 0;
    for (proven_size_t i = 0; i < c->nenumv; i++)
        if (c->enum_owner[i].size && proven_u8str_view_eq(c->enum_owner[i], ename)) {
            n++;
            if (!proven_u8str_view_eq(c->enumv[i], v)) other = c->enumv[i];
        }
    return (n == 2) ? other : (proven_u8str_view_t){0};
}
// ★ MM3 — 이 변형이 속한 enum 이 **페이로드 enum(fat struct)** 인가. 그렇다면 값은 레코드라
//   태그를 `$t` 필드로 읽어야 하고, 페이로드를 필드로 바인딩할 수 있다. 페이로드 없는 enum 은
//   값이 정수 인덱스 그대로다(태그 비교=값 비교, 바인딩 없음).
 bool ir_enum_is_payload(const ir_ctx_t *c, proven_i64 vi) {
    if (vi < 0) return false;
    proven_u8str_view_t owner = c->enum_owner[vi];
    if (!owner.size) return false;
    for (proven_size_t i = 0; i < c->nenumv; i++)
        if (proven_u8str_view_eq(c->enum_owner[i], owner) && c->enum_np[i] > 0) return true;
    return false;
}
static bool ir_enum_ctor(ir_ctx_t *c, const low_cst_t *nd,
                         low_cst_t *const *k, proven_size_t *pos, proven_size_t end) {
    proven_u8str_view_t v = nd->tok.lex;
    proven_size_t dot = 0;
    for (proven_size_t i = 0; i < v.size; i++) if (v.ptr[i] == '.') { dot = i; break; }
    if (!dot || dot + 1 >= v.size) return false;
    proven_u8str_view_t ehead = { v.ptr, dot };
    proven_u8str_view_t vseg  = { v.ptr + dot + 1, v.size - dot - 1 };
    proven_size_t vi;
    if (!ir_enum_variant_of(c, ehead, vseg, &vi)) return false;   // not <enum>.<variant> — caller falls through
    // ★★★★★ **페이로드 없는 enum 은 값이 인덱스다** (결함 노트 #21·#41, 2026-09-16).
    //   맨 이름 `green` 은 `IRW_CONST <인덱스>` 로 낮아지는데, 한정한 `light.green` 은 여기서
    //   **태그 필드를 가진 레코드**를 지었다 — 같은 값의 두 표현. 그래서 `--check` 는 통과하고
    //   실행에서 `E-VM-TYPE: arithmetic/compare needs ints` 로 멈췄다(두 뒤끝 모두).
    //   ☞ *한 값에 표현이 둘이면 그 둘이 만나는 자리에서 반드시 터진다.*
    if (!ir_enum_is_payload(c, (proven_i64)vi)) {
        ir_emit(c, IRW_CONST, (proven_i64)vi);
        return true;
    }
    if (c->out->nmakes >= IR_MAXMAKES) { ir_fail(c, "E-IR-UNSUP", "too many make sites", nd->line); return true; }
    proven_size_t my = c->out->nmakes++;
    low_ir_make_t mk = { .type_name = ehead, .nfields = 0 };
    mk.fields[mk.nfields++] = (proven_u8str_view_t){ IR_ENUM_TAGF, 2 };   // field 0 = 태그
    ir_emit(c, IRW_CONST, (proven_i64)vi);                               //   값 = 변형 인덱스
    proven_u8 np = c->enum_np[vi];
    for (proven_u8 fld = 0; fld < np && mk.nfields < IR_MAKE_MAXF; fld++) {
        if (*pos >= end) {
            ir_fail(c, "E-ENUM-ARITY", "this enum variant needs more payload arguments than were "
                    "given (RFC-0080 §4.3)", nd->line);
            c->out->makes[my] = mk; return true;
        }
        mk.fields[mk.nfields++] = c->enum_pname[vi][fld];
        ir_value(c, k, pos, end);                                        // 페이로드 피연산자 하나
        if (c->failed) { c->out->makes[my] = mk; return true; }
    }
    c->out->makes[my] = mk;
    ir_emit(c, IRW_MAKE, (proven_i64)my);
    return true;
}
// ★★★ **atomic 의 `order <name>` 를 형제 자리에서 받는다** (RFC-0018 §6.1 · 2026-07-31).
//
//   이 기능은 오래전에 **반쯤** 들어와 있었다: `ir_value` 의 빌트인 갈래가 `order <name>` 를
//   **자기 피연산자 목록 안에서** 찾는다. 그런데 파서는 **arity 로 중첩**한다 —
//   `atomic_load s 0 order acquire` 에서 `atomic_load` 는 `s 0` 만 먹고, `order acquire` 는
//   **형제**가 된다. 그래서 그 코드는 **한 번도 도달하지 못했고**(`--flat` 모드에서만 닿는다),
//   `order` 를 쓰면 "extra operands (strict arity)" 로 거절됐다.
//   ⇒ **증명은 해 놓고**(11장 · LowentIRC11) 도구가 안 준 것이다 — 이 저장소가 반복해서
//     경계하는 실패 양식(W-NOT-YET). 형제 자리에서 받아 **닿게** 만든다.
//
//   `mark` = 방금 방출된 atomic 명령의 인덱스. 그 자리의 `a`(하위 3비트)를 덮어쓴다.
static bool ir_take_order(ir_ctx_t *c, low_cst_t *const *k, proven_size_t *pos, proven_size_t end,
                          proven_size_t mark) {
    if (mark >= c->code.len) return false;
    low_ir_ins_t *in = ir_at(c, mark);
    if (in->w < IRW_ALOAD || in->w > IRW_AFENCE) return false;
    if (*pos + 1 >= end || !is_atom(k[*pos]) || !veq(k[*pos]->tok.lex, "order")) return false;
    if (!is_atom(k[*pos + 1])) return false;
    proven_u8str_view_t o = k[*pos + 1]->tok.lex;
    proven_i64 ord = veq(o, "relaxed") ? 0 : veq(o, "acquire") ? 1 : veq(o, "release") ? 2
                   : veq(o, "acq_rel") ? 3 : veq(o, "seq_cst") ? 4 : -1;
    if (ord < 0) {
        ir_fail(c, "E-ATOMIC-ORDER", "unknown memory ordering — one of relaxed·acquire·release·"
                "acq_rel·seq_cst (RFC-0018 §6.1)", k[*pos + 1]->line);
        return true;
    }
    // ★ op 별 허용 ordering (§6.1). 이것을 안 막으면 `atomic_load … order release` 처럼
    //   **뜻이 없는 조합**이 조용히 통과한다 — C11 도 그것을 UB 로 둔다.
    bool ok = true;
    if (in->w == IRW_ALOAD)       ok = (ord == 0 || ord == 1 || ord == 4);
    else if (in->w == IRW_ASTORE) ok = (ord == 0 || ord == 2 || ord == 4);
    else if (in->w == IRW_AFENCE) ok = (ord != 0);
    if (!ok) {
        ir_fail(c, "E-ATOMIC-ORDER", "this memory ordering is not valid for this atomic op "
                "(a load cannot be `release`, a store cannot be `acquire`, a fence cannot be "
                "`relaxed`) — RFC-0018 §6.1", k[*pos + 1]->line);
        return true;
    }
    in->a = (in->a & ~(proven_i64)7) | ord;
    *pos += 2;
    return true;
}

 void ir_value(ir_ctx_t *c, low_cst_t *const *k, proven_size_t *pos, proven_size_t end) {
    if (*pos >= end || c->failed) { ir_emit(c, IRW_CONST, 0); return; }
    const low_cst_t *nd = k[(*pos)++];
    if (g_relsub_atom && nd == g_relsub_atom) { ir_emit(c, IRW_LOAD, (proven_i64)g_relsub_slot); return; }   // ★ RFC-0135 S2
    // ★ METHOD 원자(`..name`)도 **이름 원자**다 — 이 블록이 IDENT 만 열려 있어서 통째로
    //   건너뛰고 fallthrough 에서 "unsupported atom" 이 났다. (그 진단은 **오진**이었다.)
    // ★★ RFC-0132 §13.7 — using 패스가 만든 [using, (send <출처> reserve <바이트>), <나열>]: 받으면 채워 `some`, 못 받으면 `none`.
    if (is_atom(nd) && nd->synth && veq(nd->tok.lex, "using") && *pos + 2 <= end) {
        const low_cst_t *snd = k[(*pos)++], *lst = k[(*pos)++];
        if (*pos < end) (*pos)++;                        // ★ RFC-0135 S2 — 넷째 자식(돌려주기)은 값이 아니다(바인딩이 건다)
        {   // RFC-0135 S2a — 구조체면 바이트 배치가 있는지 먼저 본다(없으면 `size_of` 가 먼저 엉뚱한 말을 한다)
            const low_cst_t *lf = lst;
            while (lf && lf->kind == LOW_CST_GROUP && lf->nkids == 1) lf = lf->kids[0];
            const low_cst_t *sv = lf && lf->kind == LOW_CST_FORM && lf->nkids == 2 && is_atom(lf->kids[0]) && lf->kids[0]->tok.kw == LOW_KW_LIT ? lf->kids[1] : NULL;
            while (sv && sv->kind == LOW_CST_GROUP && sv->nkids == 1) sv = sv->kids[0];
            if (sv && sv->kind == LOW_CST_FORM && sv->nkids == 2 && is_atom(sv->kids[0]) && sv->kids[1]->kind == LOW_CST_BLOCK) {
                bool sf; proven_size_t si = ir_struct_find(c->out, ir_strip_mod(c, sv->kids[0]->tok.lex), &sf);
                if (!sf || ir_alloc_shadow(c->out, si) < 0) {   // ★ RFC-0135 D13 — 슬라이스 칸은 (주소, 길이) 로 담는다
                    ir_fail(c, "E-LIT-UNBUILT", "a struct built in an allocator's bytes holds sized numbers, bools, arrays, structs with a "
                            "byte layout and slices of numbers (each slice as an address and a length) — an `owned`, actor or capability "
                            "field has no place there, because the bytes would have to be finished with it (RFC-0135 D9 · D13)", nd->line);
                    return;
                }
            }
        }
        ir_node(c, snd);
        proven_size_t ov = ir_hidden_local(c, nd->line);
        if (c->failed) return;
        ir_emit(c, IRW_STORE, (proven_i64)ov);
        g_using_ov = ov;                                   // ★ RFC-0135 S2 — 돌려줄 바이트는 이 슬롯에 있다
        ir_emit(c, IRW_LOAD, (proven_i64)ov); ir_emit(c, IRW_HASVAL, 0);
        proven_size_t to_none = ir_emit(c, IRW_BRZ, 0);
        ir_emit(c, IRW_LOAD, (proven_i64)ov); ir_emit(c, IRW_SOMEVAL, 0);
        c->lit_into = true;
        ir_node(c, lst);
        c->lit_into = false;
        ir_emit(c, IRW_WRAP_SOME, 0);
        proven_size_t to_end = ir_emit(c, IRW_BR, 0);
        ir_at(c, to_none)->a = (proven_i64)c->code.len;
        ir_emit(c, IRW_WRAP_NONE, 0);
        ir_at(c, to_end)->a = (proven_i64)c->code.len;
        return;
    }
    if (is_atom(nd) && (nd->tok.kind == LOW_TOK_IDENT || nd->tok.kind == LOW_TOK_METHOD)) {
        // ★★★ **`comptime <expr>`** — 순수 정수식을 컴파일 시점에 접어 상수로 박는다
        //   (RFC-0015 §6.1 CTFE). 값 위치의 `comptime` 은 파라미터 한정자(`input comptime …`,
        //   low_cst.c 가 처리)와 갈린다: 여기 오면 뒤따르는 **한 값**을 접는다. 접히면 IRW_CONST
        //   (런타임 비용 0), 못 접으면 E-COMPTIME-NONCONST 로 이유를 댄다. 데이터 임베딩(text→표,
        //   arena→static region)은 §8 후속 — 지금은 스칼라 정수만.
        if (veq(nd->tok.lex, "comptime")) {
            if (*pos >= end) { ir_fail(c, "E-IR-UNSUP", "`comptime` needs an expression to fold", nd->line); return; }
            const low_cst_t *ce = k[(*pos)++];
            proven_i64 cv;
            if (ir_ctfe_fold(ce, &cv, IR_CTFE_FUEL)) { ir_emit(c, IRW_CONST, cv); return; }
            ir_fail(c, "E-COMPTIME-NONCONST",
                    "this `comptime` expression could not be evaluated at compile time. Today's CTFE "
                    "folds a PURE INTEGER expression only — integer literals and pure scalar ops "
                    "(add/sub/mul/div/mod/neg, and/or/not, bit_and/bit_or/bit_xor, shl/shr, the "
                    "comparisons, min/max). Runtime names, slices, allocation, op calls, and data "
                    "embedding (text→table, arena→static region) are not folded yet (RFC-0015 §8)",
                    nd->line);
            return;
        }
        // ★★★ `config <option>` — **구성 값은 comptime 상수다** (RFC-0036 D5).
        //   런타임 비용 0 이고, 조건에 쓰이면 **가지가 통째로 접힌다**(RFC-0054 D2).
        //   ★ 그런데 `#ifdef` 와 달리 **꺼진 가지도 파싱되고 타입 검사를 받는다.**
        //     C 의 조건부 컴파일은 안 켜진 코드를 **썩게 두고**, 그것이 커널 급 프로젝트에서
        //     "그 옵션 조합은 빌드조차 안 된다" 로 나타난다. 여기서는 **불가능하다.**
        if (veq(nd->tok.lex, "config")) {
            if (*pos >= end || !is_atom(k[*pos])) {
                ir_fail(c, "E-CONFIG-UNDEF", "`config` needs the name of a build option", nd->line);
                return;
            }
            const low_cst_t *on = k[(*pos)++];
            ir_opt_t *o = ir_opt_find(on->tok.lex);
            if (!o) {
                ir_fail(c, "E-CONFIG-UNDEF", "this names a build option that was never declared "
                        "(`build option <name> bool default on .`). Reading a knob nobody installed "
                        "always reads the same thing — and it reads as a decision that was made",
                        on->line);
                return;
            }
            ir_emit(c, IRW_CONST, o->val);
            return;
        }
        if (nd->tok.kw == LOW_KW_EXPR) {   // expr island as an argument: takes the rest
            proven_size_t ipos = 0;
            c->island++;
            ir_island_climb(c, k + *pos, end - *pos, &ipos, 0);
            c->island--;
            *pos = end;
            return;
        }
        if (nd->tok.kw == LOW_KW_TRY) {    // try e — unwrap ok / early-return err
            // ★★★ **`try spawn send <actor> <msg>`** — 회복 가능한 backpressure(RFC-0009 §AC3).
            //   메일박스가 차면 트랩하는 `spawn send` 와 달리, `result<unit, mailbox_full>` 를 돌려준다.
            //   `spawn` 을 유지하는 건 **async 표식**을 지우지 않기 위함(`try send` 는 기존 try-unwrap
            //   연산자와 갈린다). 여기서 가로채지 않으면 `try` 가 unit 을 unwrap 하려다 항등이 된다.
            //   ★ **모양이 둘이다.** 괄호 안(`is_ok (try spawn send …)`)에서는 원자들이 **평평**하다.
            //     그러나 bare(`var r be try spawn send …`)면 arity 단일화가 `try` 의 값 피연산자로
            //     `spawn send …` 를 **하나의 그룹**으로 묶는다(spawn·send 는 "R") — 그때 다음 노드는
            //     원자가 아니라 GROUP(FORM([spawn, send, …]))이다. 둘 다 잡지 않으면 bare 가 조용히
            //     그냥 `spawn send`(트랩) 로 새어 나간다. (이 갈림이 정확히 교훈 7 — 한 뜻, 두 모양.)
            if (*pos + 1 < end && is_atom(k[*pos]) && k[*pos]->tok.kw == LOW_KW_SPAWN
                && is_atom(k[*pos + 1]) && k[*pos + 1]->tok.kw == LOW_KW_SEND) {
                *pos += 2;   // `spawn` `send` 소비 (평평)
                ir_send(c, k, pos, end, nd->line, true, true);   // try_async → IRW_TASEND
                return;
            }
            {   // 중첩: try (spawn send …) — 그룹의 kids 로 같은 하강을 돌린다.
                const low_cst_t *g = (*pos < end) ? k[*pos] : NULL;
                if (g && g->kind == LOW_CST_GROUP && g->nkids == 1) g = g->kids[0];
                if (g && g->kind == LOW_CST_FORM && g->nkids >= 2
                    && is_atom(g->kids[0]) && g->kids[0]->tok.kw == LOW_KW_SPAWN
                    && is_atom(g->kids[1]) && g->kids[1]->tok.kw == LOW_KW_SEND) {
                    (*pos)++;   // 그룹 소비
                    proven_size_t gpos = 2;
                    ir_send(c, g->kids, &gpos, g->nkids, nd->line, true, true);
                    return;
                }
            }
            ir_value(c, k, pos, end);
            // ★ SPEC-007 §28 — **채널 전환**. `try E else_none` / `try E else_error <variant>`.
            //   이것들은 `try` 의 **조기 반환을 대신한다**: 오류를 이 자리에서 다른 채널로 옮긴다.
            if (*pos < end && is_atom(k[*pos]) && veq(k[*pos]->tok.lex, "else_none")) {
                (*pos)++;
                ir_emit(c, IRW_ELSE_NONE, 0);
                return;
            }
            if (*pos + 1 < end && is_atom(k[*pos]) && veq(k[*pos]->tok.lex, "else_error") &&
                is_atom(k[*pos + 1])) {
                proven_size_t ei = ir_err_intern(c, k[*pos + 1]->tok.lex);
                *pos += 2;
                ir_emit(c, IRW_ELSE_ERR, (proven_i64)ei);
                return;
            }
            if (*pos < end && is_atom(k[*pos]) && veq(k[*pos]->tok.lex, "map_error")) {
                ir_fail(c, "E-IR-UNSUP",
                        "`try E map_error <op>` is not implemented yet — the tool cannot map the "
                        "error through an op. (`else_none` and `else_error` ARE implemented.)",
                        nd->line);
                return;
            }
            ir_emit(c, IRW_TRY, 0);
            return;
        }
        if (nd->tok.kw == LOW_KW_LIT) {   // make T do … end
            if (*pos >= end) { ir_fail(c, "E-IR-UNSUP", "lit without a literal", nd->line); return; }
            // ★ 이 자리에 **두 모양을 다 받는 반창고**가 있었다 — `make` 가 괄호 안에서
            //   자기가 머리가 되어 다른 나무를 만들었기 때문이다. **문법을 고치니 사라졌다**
            //   (`make` 를 블록-문장 머리 목록에서 뺐다). 뒷단의 반창고는 대개 **앞단의 병**이다.
            // ★ RFC-0132 T2b-1: 원소 나열 리터럴은 파서가 GROUP(FORM[lit, array|slice|vec, …]) 로 닫아 둔다 —
            //   여기서는 그 폼의 나머지 전부가 리터럴이다.
            if (is_atom(k[*pos]) && (veq(k[*pos]->tok.lex, "array") || veq(k[*pos]->tok.lex, "slice") ||
                                     (veq(k[*pos]->tok.lex, "vec") && is_atom(k[end - 1])))) {   // `lit vec t a do … end` 는 구조체 값
                ir_lit_list(c, k, pos, end, nd->line);
                *pos = end;
                return;
            }
            if (c->lit_into) { c->lit_into = false; ir_lit_struct_into(c, k[(*pos)++], nd->line); return; }   // RFC-0135 S2a
            ir_make(c, k[(*pos)++], nd->line);
            return;
        }
        // ★★ level 2 — `spawn actor N` · `send M to C`.
        //   actor 는 **레코드 + 그것을 소유한 op 들**이다. 그러면:
        //     spawn = **make**(상태를 0 으로)  ·  send = **call**(인스턴스를 인자로)
        //   발명할 것이 없다. 그리고 한 번에 한 메시지만 처리된다 — 그것이 actor 모델이고,
        //   LowentDRF.v 가 **그것이면 경합이 없다**고 증명한 바로 그 규율이다(Qed).
        if (nd->tok.kw == LOW_KW_SPAWN) {
            // ★★★ `spawn send <actor> <msg> [args]` — **async(fire-and-forget)** 배달(RFC-0009).
            //   메시지를 메일박스에 넣고 즉시 반환한다. `drain <actor>` 가 FIFO 로 비운다.
            if (*pos < end && is_atom(k[*pos]) && k[*pos]->tok.kw == LOW_KW_SEND) {
                (*pos)++;   // `send` 소비
                ir_send(c, k, pos, end, nd->line, true, false);   // async → IRW_ASEND
                return;
            }
            // ★★★ **task_group 태스크 spawn** (RFC-0009 D3-b) — 그룹 안에서 `spawn <op> [args]` 는
            //   **태스크**를 큐잉한다(actor 인스턴스가 아니라). `spawn actor X` 는 여전히 인스턴스다
            //   (아래 `actor` 분기). 이름이 op 이면 태스크, actor 타입이면 인스턴스로 갈린다.
            if (*pos < end && is_atom(k[*pos]) && k[*pos]->tok.kw != LOW_KW_ACTOR) {
                bool df; proven_size_t di = ir_def_find_in(c, k[*pos]->tok.lex, &df);
                if (df && !c->out->defs[di].is_actor) {
                    if (!c->in_tgroup) {   // SC1 — 태스크 spawn 은 그룹 안에서만
                        ir_fail(c, "E-SPAWN-SCOPE",
                                "spawning a task (`spawn <op>`) is only allowed inside a `task_group` "
                                "(RFC-0009 D3-b SC1: no task may outlive its group). Wrap it in "
                                "`task_group do … end`, or if you meant an actor instance write `spawn actor <T>`",
                                nd->line);
                        return;
                    }
                    (*pos)++;   // op 이름
                    proven_size_t np = c->out->defs[di].nparams, given = 0;
                    while (*pos < end && !c->failed) { ir_node(c, k[(*pos)++]); given++; }
                    if (given != np) {
                        ir_fail(c, "E-IR-ARITY",
                                "a spawned task was given the wrong number of arguments — it is an op "
                                "call, and its `input …` clause says how many it takes", nd->line);
                        return;
                    }
                    ir_emit(c, IRW_TSPAWN, (proven_i64)di);
                    return;
                }
            }
            if (*pos < end && is_atom(k[*pos]) && k[*pos]->tok.kw == LOW_KW_ACTOR) (*pos)++;
            if (*pos >= end || !is_atom(k[*pos])) {
                ir_fail(c, "E-IR-UNSUP", "spawn needs an actor name", nd->line); return;
            }
            bool af; proven_size_t as = ir_struct_find(c->out, ir_strip_mod(c, k[*pos]->tok.lex), &af);
            if (!af) { ir_fail(c, "E-IR-UNDEF", "spawn names something that is not a declared actor", nd->line); return; }
            const low_ir_struct_t *st = &c->out->structs[as];
            if (c->out->nmakes >= IR_MAXMAKES) { ir_fail(c, "E-IR-UNSUP", "too many make sites", nd->line); return; }
            proven_size_t my = c->out->nmakes++;
            low_ir_make_t mk = { .type_name = st->name, .nfields = 0 };
            for (proven_size_t q = 0; q < st->nf && q < IR_MAKE_MAXF; q++) {
                mk.fields[mk.nfields++] = st->f[q].name;
                ir_emit(c, IRW_CONST, 0);          // 상태의 초기값 — 0 (선언적 초기화는 후속)
            }
            c->out->makes[my] = mk;
            (*pos)++;
            ir_emit(c, IRW_MAKE, (proven_i64)my);
            return;
        }
        if (nd->tok.kw == LOW_KW_SEND) {
            ir_send(c, k, pos, end, nd->line, false, false);   // ★ 값 send — 동기(IRW_CALL)
            return;
        }
        // ★★★ `none` — option 의 **빈 값** (RFC-0043 D5: *"OOM 은 값이다"*).
        //   ★ 처음엔 이 분기를 **아래 블록 안**에 넣었다 — 그 블록은 `kw == LOW_KW_NONE`,
        //     즉 **"키워드가 아닌 이름"** 을 다루는 자리다. `none` 은 **키워드**다
        //     (`LOW_KW_NONEVAL`) ⇒ **절대 닿지 않았다.** 이름이 비슷해서 속았다:
        //     `LOW_KW_NONE` = "키워드 아님" 이고 `LOW_KW_NONEVAL` = 낱말 `none` 이다.
        if (nd->tok.kw == LOW_KW_NONEVAL) {
            ir_emit(c, IRW_WRAP_NONE, 0);
            return;
        }
        if (nd->tok.kw == LOW_KW_NONE) {
            // ★★★★★ **한정 이름은 지역이 아니다** (2026-09-05).
            //   `ck_narrow_qual` 이 `segv.total` 을 bare `total` 로 **제자리에서 좁히고**
            //   가리킨 모듈을 `nd->qual_mod` 에 남긴다. 그런데 여기서는 좁혀진 낱말을 그대로
            //   지역 표에 물었고, 마침 같은 이름의 지역이 있으면 **지역이 이겼다**:
            //       let total be u64 3 .        rem 내 모듈의 지역
            //       return (segv.total a) .     rem 남의 모듈의 op 을 **한정으로** 부른다
            //   → 머리가 지역(arity 0)으로 풀려 `a` 가 남고 `E-IR-ARITY: extra operands` 가 났다.
            //   **한정으로 부른 것을 지역이라 읽고, 그 결과를 사용자의 문법 실수라 불렀다.**
            //   ☞ *한정 이름은 정의상 지역일 수 없다 — 좁히는 쪽이 남긴 표시를 읽는 쪽도 봐야 한다.*
            //   (같은 뿌리의 앞선 사례: `qualifier-erased-flat-lookup`, 2026-08-14.)
            if (!nd->qual_mod.size)                                   // ★ RFC-0132 P1 — `for x mut buf` 의 x = buf 의 그 칸
                for (proven_size_t q = c->nmel; q-- > 0; )
                    if (proven_u8str_view_eq(c->mel_name[q], nd->tok.lex)) {
                        ir_emit(c, IRW_LOAD, (proven_i64)c->mel_buf[q]); ir_emit(c, IRW_LOAD, (proven_i64)c->mel_idx[q]);
                        ir_emit(c, IRW_INDEX, 0);
                        return;
                    }
            bool isloc; proven_size_t slot = ir_local_find(c, nd->tok.lex, &isloc);
            if (isloc && nd->qual_mod.size) isloc = false;
            if (isloc) { ir_emit(c, IRW_LOAD, (proven_i64)slot); return; }
            // ★ actor 의 상태 필드 — 지역이 아니라 **인스턴스의 필드**다(슬롯 0 = 인스턴스).
            if (ir_is_sfield(c, nd->tok.lex)) {
                ir_emit(c, IRW_LOAD, 0);
                ir_emit(c, IRW_FIELD, (proven_i64)ir_field_intern(c, nd->tok.lex));
                return;
            }
            if (ir_glued_local(c, nd->tok.lex, nd->line)) return;   // s.f.g sugar
            if (ir_enum_ctor(c, nd, k, pos, end)) return;           // <enum>.<variant> 구성 (RFC-0080)
            // ★★★ **volatile 접근** (RFC-0042 D1) — *정확히 쓴 대로*.
            //   `read_volatile <블록> <레지스터>` · `write_volatile <블록> <레지스터> <값>`
            //   ★ 블록은 **디바이스의 지도**(byte slice 위의 struct view)다 ⇒ VM 에서 **진짜로 돈다**:
            //     "디바이스" 는 호출자가 건네는 바이트 버퍼다.
            //   ★ 권한(D3)은 **여기서 강제된다**: `ro` 를 쓰거나 `wo` 를 읽으면 **컴파일 에러**.
            if (veq(nd->tok.lex, "read_volatile") || veq(nd->tok.lex, "write_volatile")) {
                bool wr = veq(nd->tok.lex, "write_volatile");
                ir_value(c, k, pos, end);                       // 블록(뷰)
                if (*pos >= end || !is_atom(k[*pos])) {
                    ir_fail(c, "E-IR-UNSUP", "volatile access needs a register name", nd->line); return;
                }
                proven_u8str_view_t reg = k[(*pos)++]->tok.lex;
                // 권한을 찾는다 — 어느 블록인지는 **타입 그림자**가 아니라 이름으로 찾는다(v1)
                for (proven_size_t si = 0; si < c->out->nstructs; si++) {
                    const low_ir_struct_t *st = &c->out->structs[si];
                    if (!st->is_mmio) continue;
                    for (proven_size_t z = 0; z < st->nf; z++) {
                        if (!proven_u8str_view_eq(st->f[z].name, reg)) continue;
                        if (wr && st->f[z].perm == FP_RO)
                            ir_fail(c, "E-MMIO-PERM",
                                    "this register is READ-ONLY (`ro`) — writing it is a compile "
                                    "error, not a runtime surprise (RFC-0042 D3). The device says "
                                    "what it will accept; the type says it back", nd->line);
                        if (!wr && st->f[z].perm == FP_WO)
                            ir_fail(c, "E-MMIO-PERM",
                                    "this register is WRITE-ONLY (`wo`) — reading it is a compile "
                                    "error. A wo register often reads as garbage (or has a read "
                                    "side effect), so the read is not merely useless: it is wrong",
                                    nd->line);
                    }
                }
                if (c->failed) return;
                if (wr) {
                    ir_value(c, k, pos, end);                   // 값
                    ir_emit(c, IRW_FSTORE, (proven_i64)ir_field_intern(c, reg));
                    ir_emit(c, IRW_CONST, 0);                   // 문장으로 쓰이면 버려진다
                } else {
                    ir_emit(c, IRW_FIELD, (proven_i64)ir_field_intern(c, reg));
                }
                return;
            }
            if (veq(nd->tok.lex, "field")) {   // canonical prefix access (G1)
                // ★ 레코드가 **직접 지역 이름**이면 mmio 뷰 권한을 검사할 수 있다(타입 기반).
                //   복합식이면 base 이름이 없으므로 검사를 건너뛴다(보수적 — 오탐 금지).
                proven_u8str_view_t base = { 0 };
                if (*pos < end && is_atom(k[*pos])) base = k[*pos]->tok.lex;
                ir_value(c, k, pos, end);      // the record
                if (*pos >= end || !is_atom(k[*pos])) { ir_fail(c, "E-IR-UNSUP", "field needs a name", nd->line); return; }
                if (base.size) ir_mmio_perm_check(c, base, k[*pos]->tok.lex, false, nd->line);
                if (c->failed) return;
                // ★★★★★ **`field` 는 다단이다** (2026-08-25 · 소유자 결정).
                //   `field o i z .` ≡ `field (field o i) z .` — 마디를 왼쪽부터 잇는다.
                //   ☞ 이것이 붙은 점 `o.i.z` 가 하던 일이고, 그 표기를 없애는 대신 **전위 하나로**
                //     모은다. 같은 뜻을 적는 길이 둘이면 읽는 사람이 둘 다 알아야 한다(§2.5 정신).
                //   ★ 인자 수가 **폼의 끝까지**다 — 내장 op 가운데 처음이다. 그래서 닫개가
                //     경계를 말해야 한다: `field o i z .` 의 `.` 이 여기를 닫는다. 괄호 없는
                //     중첩은 그대로 선다(`add 1 field o i z .` 처럼 안쪽이 먼저 닫히면 된다).
                //   ★★ 숫자 마디는 **인덱스**다 — 붙은 점이 하던 규칙을 그대로 옮긴다.
                do {
                    proven_u8str_view_t seg = k[(*pos)++]->tok.lex;
                    proven_i64 idx;
                    if (ir_int_lit(seg, &idx)) { ir_emit(c, IRW_CONST, idx); ir_emit(c, IRW_INDEX, 0); }
                    else ir_emit(c, IRW_FIELD, (proven_i64)ir_field_intern(c, seg));
                } while (*pos < end && is_atom(k[*pos]));
                return;
            }
            if (veq(nd->tok.lex, "isa")) {   // ★ 변형 태그 술어 (RFC-0080 §4.4): `is <value> <variant>`
                ir_value(c, k, pos, end);   // 값(레코드)을 스택에
                if (*pos >= end || !is_atom(k[*pos])) {
                    ir_fail(c, "E-IR-UNSUP", "`isa` needs a variant name: `isa <value> <variant>`", nd->line); return; }
                proven_i64 vi = ir_variant_index(c, k[(*pos)++]->tok.lex);
                if (vi < 0) { ir_fail(c, "E-ENUM-NOVARIANT", "this is not a declared enum variant", nd->line); return; }
                ir_emit(c, IRW_FIELD, (proven_i64)ir_field_intern(c, (proven_u8str_view_t){ IR_ENUM_TAGF, 2 }));
                ir_emit(c, IRW_CONST, vi);
                ir_emit(c, IRW_EQ, 0);      // 태그 == 변형 인덱스 → bool
                return;
            }
            if (veq(nd->tok.lex, "get")) {  // ★ 변형 페이로드 접근 (RFC-0080 §4.4): `get <value> <variant> <field>`
                // ★ 값이 **맨 지역 이름**이면 narrowing 검사를 위해 이름을 먼저 붙든다.
                proven_u8str_view_t gvn = (*pos < end && is_atom(k[*pos])) ? k[*pos]->tok.lex : (proven_u8str_view_t){0};
                ir_value(c, k, pos, end);   // 값(레코드)
                if (*pos + 1 >= end || !is_atom(k[*pos]) || !is_atom(k[*pos + 1])) {
                    ir_fail(c, "E-IR-UNSUP", "`get` needs a variant and a field: `get <value> <variant> <field>`", nd->line); return; }
                proven_u8str_view_t vname = k[(*pos)++]->tok.lex;   // 변형
                proven_u8str_view_t fname = k[(*pos)++]->tok.lex;   // 필드
                // ★ 정적 안전(RFC-0080 §4.6): 변형·필드가 **선언에 있어야** 한다. 오타는
                //   런타임이 아니라 컴파일타임에 잡는다 — 이것이 페이로드 enum 을 고른 이유다.
                proven_i64 gvi = ir_variant_index(c, vname);
                if (gvi < 0) { ir_fail(c, "E-ENUM-NOVARIANT", "this is not a declared enum variant", nd->line); return; }
                bool okf = false;
                for (proven_u8 fi = 0; fi < c->enum_np[gvi]; fi++)
                    if (proven_u8str_view_eq(c->enum_pname[gvi][fi], fname)) { okf = true; break; }
                if (!okf) { ir_fail(c, "E-ENUM-NOFIELD", "this variant has no payload field by that "
                                    "name (RFC-0080 §4.6) — check the enum declaration", nd->line); return; }
                // ★★★ **narrowing 강제** (RFC-0080 §4.6): 여러 변형을 가진 enum 에서 `get n V f` 는
                //   `n` 이 그 자리에서 **V 로 좁혀졌음**이 알려져야 한다(그렇지 않으면 fat-struct 라
                //   다른 변형의 슬롯을 조용히 읽는다). 좁힘은 `guard isa n V . else …` 가 준다.
                //   단일 변형 enum 은 모호성이 없으니 면제. 값이 맨 지역이 아니면(복합식) 추적 불가라
                //   보수적으로 통과(추적 못 하는 걸 거짓 거절하지 않는다).
                {
                    proven_u8str_view_t owner = ir_variant_owner(c, vname);
                    if (ir_enum_nvariants(c, owner) > 1 && gvn.size) {
                        bool lf; proven_size_t ls = ir_local_find(c, gvn, &lf);
                        if (lf) {   // 맨 지역일 때만 강제 (추적 가능한 자리)
                            proven_u8str_view_t nw = c->locals[ls].narrowed;
                            if (!nw.size)
                                ir_fail(c, "E-ENUM-UNCHECKED",
                                        "this enum has several variants, so `get … <variant> …` needs "
                                        "the value to be NARROWED to that variant first — put it after "
                                        "`guard isa <value> <variant> . else …` (RFC-0080 §4.6). Reading a "
                                        "field of the wrong variant would otherwise read a neighbouring slot",
                                        nd->line);
                            else if (!proven_u8str_view_eq(nw, vname))
                                ir_fail(c, "E-ENUM-VARIANT",
                                        "this value was narrowed to a DIFFERENT variant here — a `guard "
                                        "isa` proved another case, so this `get` reads the wrong one "
                                        "(RFC-0080 §4.6)", nd->line);
                            if (c->failed) return;
                        }
                    }
                }
                ir_emit(c, IRW_FIELD, (proven_i64)ir_field_intern(c, fname));
                return;
            }
            if (veq(nd->tok.lex, "some")) {           // ★ option 생성자 (D5: OOM 은 값이다)
                ir_value(c, k, pos, end);
                ir_emit(c, IRW_WRAP_SOME, 0);
                return;
            }
            if (veq(nd->tok.lex, "ok")) {
                ir_value(c, k, pos, end);
                ir_emit(c, IRW_WRAP_OK, 0);
                return;
            }
            if (veq(nd->tok.lex, "error")) {
                if (*pos >= end || !is_atom(k[*pos]) || !ir_is_enum_variant(c, k[*pos]->tok.lex)) {
                    ir_fail(c, "E-IR-UNDEF", "error payload is not a declared enum variant", nd->line);
                    return;
                }
                proven_u8str_view_t ename = k[*pos]->tok.lex;
                proven_size_t ei = ir_err_intern(c, k[(*pos)++]->tok.lex);
                // ★ 이 오류에 `when` 조건이 선언돼 있으면, **여기서 그것이 참인지 검사한다.**
                //   거짓이면 errors 절이 거짓말을 한 것이다(a = 2 로 표시 — 출구 계약과 같은 부류).
                for (proven_u8 wi = 0; wi < c->newhen; wi++) {
                    if (!proven_u8str_view_eq(c->ewhen[wi].name, ename)) continue;
                    if (!c->def_form || c->ewhen[wi].we <= c->ewhen[wi].ws) break;
                    ir_run(c, c->def_form->kids, c->ewhen[wi].ws, c->ewhen[wi].we - c->ewhen[wi].ws);
                    ir_emit(c, IRW_ASSERT, 2);
                    break;
                }
                ir_emit(c, IRW_WRAP_ERR, (proven_i64)ei);
                return;
            }
            if (veq(nd->tok.lex, "ref") || veq(nd->tok.lex, "mut_ref")) {
                // runtime reference to a local/param (dynamic verifier V1 tracks it)
                if (*pos >= end || !is_atom(k[*pos])) { ir_fail(c, "E-IR-UNSUP", "ref of a non-local is outside the core", nd->line); return; }
                bool rf; proven_size_t rslot = ir_local_find(c, k[*pos]->tok.lex, &rf);
                if (!rf) { ir_fail(c, "E-IR-UNDEF", "ref target is not a local/param", nd->line); return; }
                (*pos)++;
                ir_emit(c, veq(nd->tok.lex, "mut_ref") ? IRW_MREF : IRW_REF, (proven_i64)rslot);
                return;
            }
            {   // RFC-0052 S3: 처분을 이름으로 고르는 산술 계열 (기본 add/sub/mul = 트랩)
                static const struct { const char *n; low_irw_t w; proven_i64 pol; } PB[] = {
                    { "wrap_add", IRW_ADD, IR_POL_WRAP }, { "wrap_sub", IRW_SUB, IR_POL_WRAP },
                    { "wrap_mul", IRW_MUL, IR_POL_WRAP },
                    { "sat_add",  IRW_ADD, IR_POL_SAT  }, { "sat_sub",  IRW_SUB, IR_POL_SAT  },
                    { "sat_mul",  IRW_MUL, IR_POL_SAT  },
                    { "chk_add",  IRW_ADD, IR_POL_CHK  }, { "chk_sub",  IRW_SUB, IR_POL_CHK  },
                    { "chk_mul",  IRW_MUL, IR_POL_CHK  },
                    { "div_nz",   IRW_DIV, IR_POL_NZ   },   // 제수 ≠ 0 이 타입으로 보장됨
                };
                for (proven_size_t bi = 0; bi < sizeof PB / sizeof PB[0]; bi++)
                    if (veq(nd->tok.lex, PB[bi].n)) {
                        ir_value(c, k, pos, end);
                        ir_value(c, k, pos, end);
                        ir_emit(c, PB[bi].w, PB[bi].pol);
                        return;
                    }
            }
            {   // RFC-0052 S3: 폭 변환을 이름으로 고른다 — widen(전역) / narrow(트랩) / _wrap / _sat
                static const struct { const char *n; proven_i64 pol; } NW[] = {
                    { "widen", 0 }, { "narrow", 0 },
                    { "narrow_wrap", IR_POL_WRAP }, { "narrow_sat", IR_POL_SAT },
                    { "narrow_try", IR_POL_CHK },   // 실패를 값으로: option τ
                };
                for (proven_size_t bi = 0; bi < sizeof NW / sizeof NW[0]; bi++)
                    if (veq(nd->tok.lex, NW[bi].n)) {
                        if (*pos >= end || !is_atom(k[*pos])) {
                            ir_fail(c, "E-IR-UNSUP", "widen/narrow needs a scalar type", nd->line); return;
                        }
                        proven_u8str_view_t tw = k[*pos]->tok.lex;
                        proven_i64 meta;
                        if (veq(tw, "f32") || veq(tw, "f64")) meta = 0;
                        else {
                            proven_u8 sz = ir_field_size(tw);
                            if (!sz) { ir_fail(c, "E-IR-UNDEF", "widen/narrow target must be a sized scalar", nd->line); return; }
                            meta = sz | NW[bi].pol;
                            // ★★★ **대상이 부호형이면 부호를 실어 부호 범위로 판정한다**(widen·narrow 공통).
                            //   전엔 로워링이 폭만 실어 런타임이 **무부호 범위**로 판정 → 두 결함:
                            //   ① `widen i16 (-1)` 이 [0,65535] 에 −1 이 없다고 트랩(SPEC-004 §66
                            //      widen_never_fails 위반 · effects none 거짓말),
                            //   ② `narrow i8 200` 이 [0,255] 에 맞다고 통과(부호형 i8 범위 [-128,127] 밖인데)
                            //      또 `narrow i8 (-1)` 은 [0,255] 에 −1 이 없다고 잘못 트랩.
                            //   부호를 실으면 widen 은 전역(넓히면 늘 맞음)·narrow 는 부호 범위 손실 트랩
                            //   (계약대로 — 감아돌리려면 narrow_wrap 을 이름으로 고른다). 구간분석·VM·native
                            //   가 같은 부호로 판정해 발산 없음. cast 는 별 의미(재해석)라 여기서 안 건드린다.
                            if (ity_of_word(tw).sign) meta |= IR_SGN_BIT;
                        }
                        (*pos)++;
                        ir_value(c, k, pos, end);
                        ir_emit(c, IRW_CAST, meta);
                        return;
                    }
            }
            {   // numeric builtins — fixed arity, tag-dispatched (int and float)
                static const struct { const char *n; low_irw_t w; proven_i64 a; proven_size_t ar; } NB[] = {
                    { "sqrt", IRW_UNM, 0, 1 }, { "abs", IRW_UNM, 1, 1 },
                    { "floor", IRW_UNM, 2, 1 }, { "ceil", IRW_UNM, 3, 1 },
                    { "nonzero_of", IRW_UNM, 4, 1 },   // D6: τ → option (nonzero τ)
                    // RFC-0053 E5 (P2′ 오차 가시): 오차 특성을 **이름이** 말한다.
                    { "sum_neumaier", IRW_UNM, 5, 1 },   // 보정합(Neumaier). 오차 O(ε) — 항의 개수에 무관
                    { "sum_seq",      IRW_UNM, 6, 1 },   // 축차합(앞에서 뒤로 한 번). 오차 O(n·ε)
                    // ★★★ **초월 함수 여섯** (RFC-0090 N2, 2026-08-11) — libm 에 붙는다.
                    //   권한 없음(순수 계산). ★ 오라클은 **알려진 답과 항등식**이다:
                    //   sin 0 = 0 · exp 0 = 1 · log 1 = 0 · sin²+cos² = 1 (오차 한계 안에서).
                    //   ☞ 정확한 비트를 주장하지 않는다 — libm 은 구현마다 마지막 자리가
                    //     다를 수 있고, 그것을 오라클로 삼으면 **거짓 실패**가 난다.
                    { "sin", IRW_UNM, 7, 1 },  { "cos", IRW_UNM, 8, 1 },
                    { "exp", IRW_UNM, 9, 1 },  { "log", IRW_UNM, 10, 1 },
                    { "round", IRW_UNM, 11, 1 },
                    { "fmod", IRW_BINM, 0, 2 }, { "min", IRW_BINM, 1, 2 }, { "max", IRW_BINM, 2, 2 },
                    { "pow", IRW_BINM, 3, 2 },
                };
                for (proven_size_t bi = 0; bi < sizeof NB / sizeof NB[0]; bi++)
                    if (veq(nd->tok.lex, NB[bi].n)) {
                        for (proven_size_t k2 = 0; k2 < NB[bi].ar; k2++) ir_value(c, k, pos, end);
                        ir_emit(c, NB[bi].w, NB[bi].a);
                        return;
                    }
            }
            if (veq(nd->tok.lex, "cast")) {   // G2 runtime: `cast <scalar> <e>`
                if (*pos >= end || !is_atom(k[*pos])) { ir_fail(c, "E-IR-UNSUP", "cast needs a scalar type", nd->line); return; }
                proven_u8str_view_t tw = k[*pos]->tok.lex;
                proven_i64 meta;
                if (veq(tw, "f32") || veq(tw, "f64")) meta = 0;
                else {
                    proven_u8 sz = ir_field_size(tw);
                    bool tsign = ity_of_word(tw).sign;
                    // ★ newtype/스칼라 별칭으로 감싼다 — `cast node_id x`. base 폭으로 낮춘다(표현 동일).
                    if (!sz) { ityp_t a = ity_alias_lookup(c, tw); if (a.known) { sz = (proven_u8)(a.bits / 8); tsign = a.sign; } }
                    if (!sz) { ir_fail(c, "E-IR-UNDEF", "cast target must be a sized scalar", nd->line); return; }
                    meta = sz;
                    // ★★★ **대상이 부호형이면 부호를 실어 부호 범위로 판정한다**(narrow/widen 과 같은 규율 ·
                    //   사용자 결정 2026-07-23). 전엔 cast 만 이걸 안 해 적합성 검사가 **무부호 범위**로 판정 →
                    //   `cast i8 -1` 이 [0,255] 에 −1 없다고 잘못 트랩하고 `cast i8 200`(i8 범위 [-128,127] 밖)이
                    //   통과했다. 같은폭 부호 재해석(i32↔u32)은 `bit_cast`(비트 보존)가 담당한다(RFC-0002 구별).
                    if (tsign) meta |= IR_SGN_BIT;
                }
                (*pos)++;
                ir_value(c, k, pos, end);
                ir_emit(c, IRW_CAST, meta);
                return;
            }
            if (veq(nd->tok.lex, "splat") || veq(nd->tok.lex, "load")) {
                // lanes/elem width come from the binding's `vec t n` type (comptime)
                if (!c->vec_lanes) { ir_fail(c, "E-IR-UNSUP", "splat/load needs a `vec t n`-typed binding context", nd->line); return; }
                proven_i64 meta = ((proven_i64)c->vec_lanes << 8) | c->vec_esz | (c->vec_flt ? IR_FLT_BIT : 0)
                                 | (c->vec_sign ? IR_SGN_BIT : 0);   // ★ 부호형 레인
                if (veq(nd->tok.lex, "splat")) {
                    ir_value(c, k, pos, end);
                    ir_emit(c, IRW_SPLAT, meta);
                } else {
                    ir_value(c, k, pos, end);   // the source (varray/slice)
                    ir_value(c, k, pos, end);   // element index
                    ir_emit(c, IRW_VLOAD, meta);
                }
                return;
            }
            // ★★★ **masked load** (RFC-0040 D3/D4, merge 의미) — `load_masked <src> <idx> <mask> <passthrough>` → vec.
            //   꼬리(tail) 안전: 켜진 lane 만 메모리를 읽고(그 lane 은 i+k < len 요구), **꺼진 lane 은 메모리를 안 읽고
            //   passthrough vec 의 그 lane 값을 가진다**(merge — 꺼진 lane 값을 프로그래머가 고른다; 0 을 원하면 zero vec).
            //   레인/esz 는 load 처럼 `vec t n` 바인딩 컨텍스트에서 온다. mask·passthrough 레인수는 그와 같아야 한다(런타임 검사).
            if (veq(nd->tok.lex, "load_masked")) {
                if (!c->vec_lanes) { ir_fail(c, "E-IR-UNSUP", "load_masked needs a `vec t n`-typed binding context", nd->line); return; }
                proven_i64 meta = ((proven_i64)c->vec_lanes << 8) | c->vec_esz | (c->vec_flt ? IR_FLT_BIT : 0)
                                 | (c->vec_sign ? IR_SGN_BIT : 0);   // ★ 부호형 레인
                ir_value(c, k, pos, end);   // the source (varray/slice)
                ir_value(c, k, pos, end);   // element index
                ir_value(c, k, pos, end);   // mask (lane predicate)
                ir_value(c, k, pos, end);   // passthrough vec (꺼진 lane 값)
                ir_emit(c, IRW_VLOADM, meta);
                return;
            }
            // ★★★ **store** (RFC-0040 — VLOAD 의 역). `store <dst> <idx> <vec>` — vec 의 레인을 배열에 되쓴다.
            //   레인/esz 는 **vec 피연산자가 실어 온다**(SELECT 처럼) — 바인딩 컨텍스트 불요, meta 없음.
            if (veq(nd->tok.lex, "store")) {
                ir_value(c, k, pos, end);   // dst (varray/slice)
                ir_value(c, k, pos, end);   // element index
                ir_value(c, k, pos, end);   // vec
                ir_emit(c, IRW_VSTORE, 0);  // unit 을 민다(문장 경로가 DROP)
                return;
            }
            // ★★★ **masked store** (RFC-0040 D3/D4) — `store_masked <dst> <idx> <vec> <mask>` → (). 꼬리 안전:
            //   **켜진 lane 만** 메모리에 쓰고(그 lane 은 i+k < len 요구) 꺼진 lane 은 안 쓴다(그 자리 보존).
            //   레인/esz 는 store 처럼 vec 피연산자가 실어 온다(meta 없음). VSTORE 의 predicate 판.
            if (veq(nd->tok.lex, "store_masked")) {
                ir_value(c, k, pos, end);   // dst (varray/slice)
                ir_value(c, k, pos, end);   // element index
                ir_value(c, k, pos, end);   // vec
                ir_value(c, k, pos, end);   // mask (lane predicate)
                ir_emit(c, IRW_VSTOREM, 0); // unit 을 민다(문장 경로가 DROP)
                return;
            }
            // ★ **식 자리의 파이프라인** — `var t be u64 pipe xs do … count . end` (RFC-0010 §6.1).
            //   헤디드 블록이므로 `make` 처럼 식으로 온다. 값을 남긴다(as_value=true).
            if (veq(nd->tok.lex, "pipe")) {
                if (*pos < end && k[*pos] && k[*pos]->kind == LOW_CST_FORM) { ir_pipe(c, k[(*pos)++], 0, true); return; }
                // ★ 2026-10-01 — 원천이 괄호 식이면(`pipe (pick xs) do … end`) 파서가 괄호와 블록을 따로 둔다. 둘을 한 폼처럼
                //   건넨다 — 전엔 `--check` 는 초록인데 «낮출 수 없다» 로 떨어졌다(RFC-0121 §6.3 의 준비 차례 시험이 드러냈다).
                if (*pos + 1 < end && k[*pos] && k[*pos]->kind == LOW_CST_GROUP && k[*pos + 1] && k[*pos + 1]->kind == LOW_CST_BLOCK) {
                    low_cst_t tmp = { .kind = LOW_CST_FORM, .kids = (low_cst_t **)&k[*pos], .nkids = 2, .line = k[*pos]->line };
                    *pos += 2; ir_pipe(c, &tmp, 0, true); return;
                }
                if (nd->kind == LOW_CST_FORM) { ir_pipe(c, nd, 1, true); return; }
                ir_fail(c, "E-IR-UNSUP", "`pipe` needs a source and a `do … end` block", nd->line); return;
            }
            // ★★★ **총체형 `pop`** (RFC-0016 §160) — `pop <stack>` → option T (비었으면 none).
            //   ★ arity 표에 넣으면 안 된다: `pop s into v`(부분형, while 조건)가 **같은 이름의 다른 모양**이라
            //     표(이름당 모양 하나)가 그것을 깨뜨린다. 그래서 여기 특수형으로 둔다 — 표는 모양이 하나일 때만.
            // ★★★ **`complement s`** (RFC-0010 §6.7.1) — 여집합. **폭을 immediate 로 박는다**:
            //   VM 과 네이티브가 **같은 마스크**를 써야 하고, 네이티브 빠른 경로는 비트셋을 폭 없는
            //   워드로 낮추므로 런타임에 폭을 알 수 없다. 바인딩 타입(`var c be bitset 8 …`)이 준다.
            if (veq(nd->tok.lex, "bitset_complement")) {
                // ★ 폭은 **피연산자 s** 의 것이다(F5): `complement s` 의 여집합 폭은 결과 바인딩이
                //   아니라 s 가 정한다. 피연산자 폭을 못 알면 바인딩 폭으로 후퇴한다(종전 동작).
                proven_u8 op_w = (*pos < end) ? ir_operand_bset_w(c, k[*pos]) : 0;
                proven_u8 comp_w = op_w ? op_w : c->bset_w;
                if (!comp_w) {
                    ir_fail(c, "E-IR-UNSUP",
                            "`bitset_complement` needs to know the set's WIDTH, and it reads that from the "
                            "operand's (or the binding's) declared type — write `var <name> bitset <n> . "
                            "be bitset_complement <s> .` (the complement of a `bitset 8` must not light the upper "
                            "56 bits, so the width is part of the answer, not a detail; RFC-0010 §6.7.1)", nd->line);
                    return;
                }
                ir_value(c, k, pos, end);
                ir_emit(c, IRW_BCOMPL, (proven_i64)comp_w);
                return;
            }
            if (veq(nd->tok.lex, "pop")) {
                ir_value(c, k, pos, end);
                ir_emit(c, IRW_SPOP, 0);
                return;
            }
            // ★★★ **target intrinsic `avg`** (RFC-0040 D5) — `avg <va> <vb>` → 레인별 라운딩 평균
            //   (a+b+1)>>1. x86 pavgb/pavgw · ARM vrhadd 의 의미. **widening 이라 portable 합성 불가**
            //   (u8 레인에서 a+b 가 넘친다) — 그래서 특수 명령이고 D5 대상이다. 격리: `unsafe target <iset>`
            //   안에서만(ck_target_intrin 이 강제). 비트-정확 ⇒ VM·C 가 같은 값 ⇒ diff-sweep 로 검증된다.
            if (veq(nd->tok.lex, "lane_avg")) {
                ir_value(c, k, pos, end);
                ir_value(c, k, pos, end);
                ir_emit(c, IRW_VAVG, 0);
                return;
            }
            // ★★★ **레인 재배열** (RFC-0040) — 모두 **새 벡터를 낸다**(복사, 값 의미). 레인수는 vec 이 실어 온다.
            if (veq(nd->tok.lex, "lane_reverse")) {     // lane_reverse <vec> — 레인 역순
                ir_value(c, k, pos, end);
                ir_emit(c, IRW_VREVERSE, 0);
                return;
            }
            if (veq(nd->tok.lex, "lane_rotate")) {      // lane_rotate <vec> <n> — n칸 회전(comptime)
                ir_value(c, k, pos, end);
                proven_i64 rn = 0;
                if (*pos < end && is_atom(k[*pos]) && ir_int_lit(k[*pos]->tok.lex, &rn)) (*pos)++;
                else { ir_fail(c, "E-IR-UNSUP", "lane_rotate needs a compile-time lane count: `lane_rotate <vec> <n>`", nd->line); return; }
                ir_emit(c, IRW_VROTATE, rn);
                return;
            }
            if (veq(nd->tok.lex, "shuffle")) {          // shuffle <vec> <i0> <i1> … — 임의 comptime 순열
                ir_value(c, k, pos, end);
                proven_i64 meta = 0, cnt = 0;
                while (*pos < end && is_atom(k[*pos])) {
                    proven_i64 ix;
                    if (!ir_int_lit(k[*pos]->tok.lex, &ix)) break;
                    if (cnt >= 8 || ix < 0 || ix > 15) { ir_fail(c, "E-IR-UNSUP", "shuffle lane index out of range (0..15, up to 8 lanes)", nd->line); return; }
                    meta |= (ix & 0xf) << (4 * cnt); cnt++; (*pos)++;
                }
                if (cnt == 0) { ir_fail(c, "E-IR-UNSUP", "shuffle needs comptime lane indices: `shuffle <vec> <i0> <i1> …`", nd->line); return; }
                meta |= (cnt << 32);
                ir_emit(c, IRW_VSHUFFLE, meta);
                return;
            }
            // ★★★ **native_lanes** (RFC-0040 D5 §120) — `native_lanes <t>` → **comptime int**: 이 target 의
            //   원소 t 에 대한 네이티브 벡터 폭(레인 수). 성능-이식 코드가 target 최적 폭을 질의한다.
            //   ★ comptime 이라 IRW_CONST 로 **정수 리터럴로 확장**된다 — 백엔드 무변경, VM·C 가 같은 값을 굽는다
            //   (같은 target 프로파일) ⇒ 오라클 안전. SIMD 없는 target(simd_bytes=0)은 **1**(scalar fallback).
            if (veq(nd->tok.lex, "native_lanes")) {
                if (*pos >= end || !is_atom(k[*pos])) { ir_fail(c, "E-IR-UNSUP", "native_lanes needs a scalar type: `native_lanes <t>`", nd->line); return; }
                proven_u8 esz = ir_field_size(k[*pos]->tok.lex);
                if (!esz) { ir_fail(c, "E-IR-UNDEF", "native_lanes element type must be a sized scalar", nd->line); return; }
                (*pos)++;
                proven_u8 sb = low_ir_target()->simd_bytes;
                proven_i64 lanes = sb ? (proven_i64)(sb / esz) : 1;   // SIMD 없으면 scalar fallback = 1 레인
                if (lanes > 16) lanes = 16;   // ★ 도구가 실제로 만들 수 있는 폭만 답한다(레인 배열 상한)
                ir_emit(c, IRW_CONST, lanes);
                return;
            }
            // ★★ `size_of <t>` → **comptime 정수**: t 한 개가 차지하는 바이트 수 (RFC-0084).
            //   제네릭 컨테이너가 원소 n 개의 자리를 바이트로 요구할 때 이것이 없으면
            //   **최대 폭으로 잡는 수밖에 없다** — `gvec u8` 이 8배를 쓰게 된다. 비용이 보이려면
            //   비용을 **물을 수 있어야** 한다. native_lanes 와 같은 기계(상수로 접힌다) ⇒ 오라클 안전.
            if (veq(nd->tok.lex, "size_of")) {
                if (*pos >= end || !is_atom(k[*pos])) { ir_fail(c, "E-IR-UNSUP", "size_of needs a type word: `size_of <t>`", nd->line); return; }
                proven_u8 sz = ir_field_size(k[*pos]->tok.lex);
                if (!sz) {   // ★ 구조체도 답한다 — 레이아웃이 있으면 크기가 있다.
                    bool sf; proven_size_t si = ir_struct_find(c->out, k[*pos]->tok.lex, &sf);
                    if (sf && c->out->structs[si].viewable && c->out->structs[si].total &&
                        c->out->structs[si].total <= 255)
                        sz = (proven_u8)c->out->structs[si].total;
                    // ★ RFC-0135 D13 — 펼치기가 지은 `(size_of T)`(할당기에 받을 바이트 수)만 그림자 배치의 크기를 묻는다.
                    //   사람이 적은 `size_of` 는 여전히 바이트 배치가 있는 타입에만 답한다.
                    if (!sz && sf && nd->synth) {
                        proven_i32 gi = ir_alloc_shadow(c->out, si);
                        if (gi >= 0 && c->out->structs[gi].total <= 255) sz = (proven_u8)c->out->structs[gi].total;
                    }
                }
                if (!sz) { ir_fail(c, "E-IR-UNDEF", "size_of needs a SIZED type (a scalar, or a VIEWABLE struct the unit declares) — it does not answer for slices or views, whose size is not a property of the type", nd->line); return; }
                (*pos)++;
                ir_emit(c, IRW_CONST, (proven_i64)sz);
                return;
            }
            // ★★★★★ **`segments` 세 op — 전부 기존 op 으로 탈설탕된다** (RFC-0104 §8-8).
            //   새 옵코드 0: 뷰는 백업·서술자 두 슬라이스를 든 구조체이고, 그 그릇은 이미 있다.
            //     view_segments b d  →  make segments do back b . descs d . end
            //     segs ss            →  div (len (field ss descs)) 2
            //     seg ss i           →  subslice (field ss back) at (add at n)
            //   ☞ 서술자는 `slice u64` 에 (at, n) 쌍이다 — **새 구조체 타입도 안 만든다**.
            // ★★★★★ **prefetch — 유일하게 승격된 특례** (RFC-0104 §8-14).
            //   `prefetch <슬라이스> <색인>` — 순수 힌트다. 값을 안 남긴다.
            if (veq(nd->tok.lex, "prefetch")) {
                ir_value(c, k, pos, end);      // 슬라이스
                ir_value(c, k, pos, end);      // 색인
                ir_emit(c, IRW_PREFETCH, 0);
                return;
            }
            if (veq(nd->tok.lex, "view_segments")) {
                if (c->out->nmakes >= IR_MAXMAKES) {
                    ir_fail(c, "E-IR-UNSUP", "too many make sites", nd->line); return;
                }
                proven_size_t my = c->out->nmakes++;
                low_ir_make_t mk = { .type_name = proven_u8str_view_from_cstr("segments"), .nfields = 0 };
                mk.fields[mk.nfields++] = proven_u8str_view_from_cstr("back");
                mk.fields[mk.nfields++] = proven_u8str_view_from_cstr("descs");
                ir_value(c, k, pos, end);      // 백업 슬라이스
                ir_value(c, k, pos, end);      // 서술자 슬라이스 (at, n) 쌍
                c->out->makes[my] = mk;
                ir_emit(c, IRW_MAKE, (proven_i64)my);
                return;
            }
            if (veq(nd->tok.lex, "segs")) {
                ir_value(c, k, pos, end);      // 뷰
                ir_emit(c, IRW_FIELD, (proven_i64)ir_field_intern(c, proven_u8str_view_from_cstr("descs")));
                ir_emit(c, IRW_LEN, 0);
                ir_emit(c, IRW_CONST, 2);
                ir_emit(c, IRW_DIV, 0);
                return;
            }
            if (veq(nd->tok.lex, "seg")) {
                // seg ss i — i 번째 조각을 **복사 없이** 준다.
                //   지역 둘을 빌려 뷰와 색인을 붙든다(스택 기계라 값을 두 번 못 읽는다).
                if (c->nlocals + 3 > IR_MAXLOCALS) { ir_fail(c, "E-IR-UNSUP", "seg: too many locals", nd->line); return; }
                proven_size_t lv = c->nlocals++, li = c->nlocals++, lat = c->nlocals++;
                c->locals[lv].name = (proven_u8str_view_t){ 0 };
                c->locals[li].name = (proven_u8str_view_t){ 0 };
                c->locals[lat].name = (proven_u8str_view_t){ 0 };
                ir_value(c, k, pos, end);  ir_emit(c, IRW_STORE, (proven_i64)lv);   // 뷰
                ir_value(c, k, pos, end);  ir_emit(c, IRW_STORE, (proven_i64)li);   // i
                proven_size_t fdesc = ir_field_intern(c, proven_u8str_view_from_cstr("descs"));
                proven_size_t fback = ir_field_intern(c, proven_u8str_view_from_cstr("back"));
                // at = descs[2i]
                ir_emit(c, IRW_LOAD, (proven_i64)lv); ir_emit(c, IRW_FIELD, (proven_i64)fdesc);
                ir_emit(c, IRW_LOAD, (proven_i64)li); ir_emit(c, IRW_CONST, 2); ir_emit(c, IRW_MUL, 0);
                ir_emit(c, IRW_INDEX, 0);
                ir_emit(c, IRW_STORE, (proven_i64)lat);
                // back, at, at + descs[2i+1]
                ir_emit(c, IRW_LOAD, (proven_i64)lv); ir_emit(c, IRW_FIELD, (proven_i64)fback);
                ir_emit(c, IRW_LOAD, (proven_i64)lat);
                ir_emit(c, IRW_LOAD, (proven_i64)lat);
                ir_emit(c, IRW_LOAD, (proven_i64)lv); ir_emit(c, IRW_FIELD, (proven_i64)fdesc);
                ir_emit(c, IRW_LOAD, (proven_i64)li); ir_emit(c, IRW_CONST, 2); ir_emit(c, IRW_MUL, 0);
                ir_emit(c, IRW_CONST, 1); ir_emit(c, IRW_ADD, 0);
                ir_emit(c, IRW_INDEX, 0);
                ir_emit(c, IRW_ADD, 0);
                ir_emit(c, IRW_SUBSLICE, 0);
                return;
            }
            if (veq(nd->tok.lex, "view_array")) {
                // `view_array T b` — T = sized scalar **또는 viewable 구조체**; bytes → typed array
                if (*pos >= end || !is_atom(k[*pos])) { ir_fail(c, "E-IR-UNSUP", "view_array needs a type word", nd->line); return; }
                proven_u8 esz = ir_field_size(k[*pos]->tok.lex);
                proven_i64 meta = 0;
                if (esz) {
                    meta = esz | (ir_is_float_ty(k[*pos]->tok.lex) ? IR_FLT_BIT : 0)
                              | (ity_of_word(k[*pos]->tok.lex).sign ? IR_SGN_BIT : 0);
                } else {
                    // ★★ **구조체 원소** (2026-07-26 · RFC-0084 §10 이 막혔던 자리).
                    //   기계장치는 **이미 있었다** — VM·C 백엔드 둘 다 IR_STRUCT_BIT 를 읽어
                    //   `index` 가 정수 대신 **구조체 뷰**를 준다(진입에서 `slice <struct>`
                    //   파라미터를 감쌀 때 쓰던 바로 그 경로). 표면만 안 이어져 있었고,
                    //   그래서 제네릭 컨테이너가 스칼라에 갇혀 있었다.
                    bool sf; proven_size_t si = ir_struct_find(c->out, k[*pos]->tok.lex, &sf);
                    if (!sf || !c->out->structs[si].viewable || !c->out->structs[si].total ||
                        c->out->structs[si].total > 255) {
                        ir_fail(c, "E-IR-UNDEF",
                                "view_array element type must be a SIZED scalar or a VIEWABLE struct "
                                "(a struct whose fields all have a byte layout — no slices, no owned "
                                "handles, total <= 255). A type with no layout has no array either",
                                nd->line);
                        return;
                    }
                    esz = (proven_u8)c->out->structs[si].total;
                    meta = (proven_i64)esz | IR_STRUCT_BIT | ((proven_i64)si << 20);
                }
                (*pos)++;
                ir_value(c, k, pos, end);
                ir_emit(c, IRW_VARRAY, meta);
                return;
            }
            // ★ SPEC-004 §190 T0 — `bit_cast t v`: **같은 폭 plain↔plain** 의 무비용 재해석.
            //   plain 이 아닌 것(bool·enum: trap representation 이 있다)은 안전 퍼닝 대상이 아니다.
            // ★ `panic "msg" .` — 즉시 트랩. **복구 불가**다(계약 위반도, 테스트 실패도 아니다).
            if (veq(nd->tok.lex, "panic")) {
                proven_size_t si = 0;
                if (*pos < end && is_atom(k[*pos]) && k[*pos]->tok.kind == LOW_TOK_STRING) {
                    proven_u8str_view_t msg = k[(*pos)++]->tok.lex;
                    si = c->out->nstrs;
                    for (proven_size_t i = 0; i < c->out->nstrs; i++)
                        if (proven_u8str_view_eq(c->out->strs[i], msg)) { si = i; break; }
                    if (si == c->out->nstrs) {
                        if (c->out->nstrs >= IR_MAXSTRS) { ir_fail(c, "E-IR-UNSUP", "too many string literals", nd->line); return; }
                        c->out->strs[c->out->nstrs++] = msg;
                    }
                } else { si = IR_MAXSTRS; }   // 메시지 없는 panic
                ir_emit(c, IRW_PANIC, (proven_i64)si);
                return;
            }
            // ★ `expect <cond> .` — **테스트의 단언**. 거짓이면 실패한다.
            //   ASSERT 의 a = 4: 진입 requires(0) · ensures(1) · errors…when(2) 과 구별한다.
            //   (구별해야 진단이 **누구의 잘못인지** 말할 수 있다 — 계약 위반이 아니라 **테스트 실패**다.)
            if (veq(nd->tok.lex, "expect")) {
                ir_value(c, k, pos, end);
                ir_emit(c, IRW_ASSERT, 4);
                return;
            }
            if (veq(nd->tok.lex, "bit_cast")) {
                if (*pos >= end || !is_atom(k[*pos])) {
                    ir_fail(c, "E-IR-UNSUP", "bit_cast needs a target type name", nd->line); return;
                }
                proven_u8str_view_t tn = k[*pos]->tok.lex;
                proven_u8 sz = ir_field_size(tn);
                // ★★★★ **`plain` 인가를 "폭 표에 없는가" 로 대신 묻고 있었다** (RFC-0092).
                //   바로 아래 진단문이 *"bool 은 trap 표현이 있어 plain 이 아니다"* 라고
                //   **정확히 적어 두고**, 판정은 `ir_field_size(bool) == 0` 이라는 **빈칸에
                //   얹혀** 있었다. bool 에 1 바이트를 주자 그 빈칸이 메워지면서 규칙이
                //   조용히 사라졌다 — 골든이 잡았다(`[FAIL] bit_cast plain`).
                //   ☞ **우연히 성립하던 판정은 그 우연이 사라지는 날 조용히 뒤집힌다.**
                //     말한 것과 검사한 것을 같게 만든다: plain 여부를 **직접** 묻는다.
                bool plain = sz != 0 && !veq(tn, "bool");
                if (!plain) {
                    ir_fail(c, "E-TYPE-BITCAST",
                            "bit_cast's target must be a `plain` scalar type — every bit pattern "
                            "valid, no padding (SPEC-004 §190). bool/enum have trap representations "
                            "and are NOT plain, so they are outside safe punning", nd->line);
                    return;
                }
                bool flt = ir_is_float_ty(tn);
                bool sgn = !flt && (tn.size && tn.ptr[0] == 'i');
                proven_i64 meta = (proven_i64)sz | (flt ? IR_FLT_BIT : 0) | (sgn ? IR_SGN_BIT : 0);
                (*pos)++;
                ir_value(c, k, pos, end);
                ir_emit(c, IRW_BITCAST, meta);
                return;
            }
            if (veq(nd->tok.lex, "view") || veq(nd->tok.lex, "try_view") || veq(nd->tok.lex, "encode")) {
                // `view T b` / `try_view T b` / `encode T v` — T = declared sized struct
                low_irw_t w = veq(nd->tok.lex, "view") ? IRW_VIEW
                            : veq(nd->tok.lex, "try_view") ? IRW_TRYVIEW : IRW_ENCODE;
                if (*pos >= end || !is_atom(k[*pos])) { ir_fail(c, "E-IR-UNSUP", "view/encode needs a struct type name", nd->line); return; }
                bool sfound; proven_size_t si = ir_struct_find(c->out, k[*pos]->tok.lex, &sfound);
                if (!sfound || !c->out->structs[si].viewable) {
                    ir_fail(c, "E-IR-UNDEF", "view/encode type is not a declared sized struct", nd->line);
                    return;
                }
                (*pos)++;
                // ★★★ **`view T .` — 피연산자가 없다** (RFC-0042 §8-2). 그러면 블록은 **타입 자신의
                //   기저 주소**에서 열린다. 새 낱말 0: 없는 것이 곧 뜻이다("바이트를 안 준다 =
                //   디바이스가 이미 어디 있는지 타입이 안다").
                //   ★ `try_view`/`encode` 에는 이 폼이 없다 — try_view 는 *길이가 모자랄 수 있는*
                //     바이트를 재는 것이고, 기저 주소에는 잴 길이가 없다(장치는 거기 있거나 없다).
                if (w == IRW_VIEW && *pos >= end) {
                    const low_ir_struct_t *ms = &c->out->structs[si];
                    if (ms->is_reserve) {
                        // ★★★★ **예약된 정적 블록**(RFC-0039 §9-2 갈래①). 주소는 링커가 준다.
                        //   ☞ **프리스탠딩 전용**이다 — 호스트에서 전역 가변 블록을 여는 것은
                        //     이 언어가 의도적으로 안 준 것이고, T1 의 논증(ISR 이 루프에 닿아야
                        //     한다)은 베어메탈에서만 선다. 넓히려면 그때 다시 논증한다.
                        if (!low_ir_target()->no_heap) {
                            ir_fail(c, "E-RESERVE-NOHOST",
                                    "`storage reserved .` opens a block of STATIC storage, and that is granted "
                                    "only on a bare-metal target. Under an OS the program has an "
                                    "allocator and a caller to hand it memory (RFC-0043); the reason "
                                    "this exists is that an ISR has NEITHER — it takes no parameters "
                                    "and there is no module-level mutable state. Build for a "
                                    "bare-metal target, or pass the bytes in (`view T <slice>`). "
                                    "Sibling of E-MMIO-NOHOST (RFC-0039 §9-2)", nd->line);
                            return;
                        }
                        // ★ **권한 검사는 여기 없다** — `ck_reserve`(low_check.c)가 CST 에서 한다.
                        //   이 저장소의 관례다: 권한은 **서명**의 성질이고 서명은 CST 가 안다
                        //   (E-MMIO-NOCAP · E-ISR-CALLED 이 같은 자리에 산다).
                        ir_emit(c, IRW_RESBLK, (proven_i64)si);
                        ir_emit(c, IRW_VIEW, (proven_i64)si);
                        return;
                    }
                    if (!ms->is_mmio) {
                        ir_fail(c, "E-MMIO-NOBASE",
                                "`view <T> .` with no bytes opens the block at the TYPE's own base "
                                "address, but this struct declares no `mmio <base> .`. A map with no "
                                "origin maps nothing — either give the bytes (`view T <slice>`) or "
                                "give the type a base (RFC-0042 D2/§8-2)", nd->line);
                        return;
                    }
                    // ★ 절대 주소는 **그 장치를 가진 기계**에서만 뜻이 있다. 호스트 빌드에서 이것을
                    //   내면 방출된 C 가 0x40020000 을 역참조한다 — 검사기가 통과시킨 프로그램을
                    //   기계가 못 돈다. E-CAP-NOHOST 와 같은 논증의 **반대 방향**이다.
                    if (!low_ir_target()->no_heap) {
                        ir_fail(c, "E-MMIO-NOHOST",
                                "this opens a register block at an ABSOLUTE address, but the build "
                                "target is HOSTED. Under an OS that address is not the device — it is "
                                "someone else's memory or nothing at all, and the program would fault "
                                "rather than blink. Build for a bare-metal target (`--target cortex_m`), "
                                "or hand the bytes in (`view T <slice>`) as the host tests do. "
                                "Sibling of E-CAP-NOHOST (RFC-0038 · RFC-0042 §8-2)", nd->line);
                        return;
                    }
                    ir_emit(c, IRW_MMIOBLK, (proven_i64)si);
                    ir_emit(c, IRW_VIEW, (proven_i64)si);
                    return;
                }
                ir_value(c, k, pos, end);   // the byte slice (view) / the value (encode)
                ir_emit(c, w, (proven_i64)si);
                return;
            }
            if (veq(nd->tok.lex, "stack_new")) {
                // `stack_new R capacity n` (RFC-0049 surface) — the region argument is
                // erased at lowering (VM pools stand in for arenas); capacity is a marker
                // ★★★ RFC-0112 D8(7) (WO-0219) — 뿌리 피연산자와 `capacity` 표식은 **적어야** 한다. 전엔 둘 다 건너뛰었다:
                //   `stack_new capacity 4` 가 영역 없이 돌았고 `stack_new r 4` 가 표식 없이 돌았다(코퍼스 0 자리).
                //   영역 블록 이름은 받지 않는다 — 검사기가 `stack_new` 의 이름을 **영역 매개변수**로만 인정한다(E-REGION-UNDEF).
                if (*pos < end && is_atom(k[*pos]) && veq(k[*pos]->tok.lex, "capacity")) {
                    ir_fail(c, "E-ALLOC-NOROOT",
                            "`stack_new` needs the REGION it takes the stack from as its first operand — "
                            "`stack_new <region parameter> capacity <n>`. There is no ambient region: the op "
                            "must receive one (`input r region T . .`) and name it (RFC-0112 D8(7) · RFC-0043 D1)",
                            nd->line);
                    return;
                }
                if (*pos < end && is_atom(k[*pos])) (*pos)++;                                           // region name
                if (!(*pos < end && is_atom(k[*pos]) && veq(k[*pos]->tok.lex, "capacity"))) {
                    ir_fail(c, "E-ALLOC-CAPACITY-MARK",
                            "`stack_new` spells its size with the `capacity` marker — `stack_new r capacity 16`. "
                            "The marker is required (RFC-0112 D8(7)): without it the region and the size are two "
                            "bare operands a reader has to count", nd->line);
                    return;
                }
                (*pos)++;                                                                               // marker
                ir_value(c, k, pos, end);
                ir_emit(c, IRW_SNEW, 0);
                return;
            }
            if (nd->tok.lex.size >= 8 && memcmp(nd->tok.lex.ptr, "machine.", 8) == 0) {
                bool mf;                                   // RFC-0054: comptime 상수(값 위치)
                proven_i64 mv = ir_machine(nd->tok.lex, &mf);
                if (!mf) { ir_fail(c, "E-CFG-UNKNOWN-PROP", "no such machine property", nd->line); return; }
                ir_emit(c, IRW_CONST, mv);
                return;
            }
            // ★★★ **수신자 우선 호출** — `s..area 2`  ≡  `rect.area s 2` (RFC-0062).
            //
            //   파서가 이미 나무를 세웠다: FORM(area, [ATOM area(METHOD), 수신자, 인자…]).
            //   여기서는 **수신자의 선언 타입**으로 진짜 op 이름 `<타입>.area` 를 찾는다.
            //
            //   ★ 이것이 이름공간을 아낀다: `rect.area` 와 `circle.area` 가 공존하고,
            //     전역 이름 `area` 는 **아무도 안 태운다.**
            //   ★ 그리고 헷갈릴 수 없다: 지역이 타입 이름을 가릴 수 없으므로(E-NAME-SHADOW)
            //     `rect.area` 의 첫 마디는 **언제나 타입**이다(RFC-0060 R7 과 같은 논증).
            // ★★★★★ **`method` 전위** (2026-08-25 · 소유자 결정)
            //   `method 인스턴스 마디… 이름 인자… .`  ≡  `인스턴스.마디….이름 인자…` (`..`)
            //
            //   ☞ 붙은 점을 없애면서 메서드도 **전위 하나로** 모은다 — `field` 와 짝이다.
            //   ★ **새 해소 규칙을 짓지 않는다.** 아래 `LOW_TOK_METHOD` 자리가 이미
            //     *"수신자의 타입을 따라가 `<타입>.<이름>` 을 찾는"* 기계를 갖고 있다.
            //     그래서 여기서는 **모양만 그 자리로 옮겨** 준다 — 마디들을 붙임 점으로 이어
            //     하나의 원자로 만들고, 이름을 메서드 토큰으로 세운다.
            //     두 벌로 지으면 갈린다(교훈 7).
            //   ★★ 어디까지가 마디이고 어디부터가 인자인가: **낱말 원자가 이어지는 동안이
            //     마디+이름**이고, 그중 **마지막이 이름**이다. 인자는 그 뒤 전부다.
            if (nd->tok.kw == LOW_KW_NONE && veq(nd->tok.lex, "method")) {
                if (*pos + 1 >= end) {
                    ir_fail(c, "E-METHOD-RECV",
                            "`method` needs a receiver and a name: "
                            "`method <value> <member…> <name> <args…>`", nd->line);
                    return;
                }
                // ★ 수신자가 **맨 이름이 아니면**(사슬 `method (method x grow 2) area`)
                //   기존 `..` 기계로 넘긴다 — 그쪽이 *"앞 op 의 출력 타입"* 을 이미 안다.
                //   여기서 같은 것을 또 짓지 않는다(교훈 7).
                if (!is_atom(k[*pos])) {
                    if (*pos + 1 >= end || !is_atom(k[*pos + 1])) {
                        ir_fail(c, "E-METHOD-RECV",
                                "`method` needs a name after the receiver", nd->line);
                        return;
                    }
                    low_token_t mt3 = k[*pos + 1]->tok;
                    mt3.kind = LOW_TOK_METHOD;
                    low_cst_t mnode3 = *k[*pos + 1];
                    mnode3.tok = mt3; mnode3.kind = LOW_CST_ATOM; mnode3.nkids = 0;
                    low_cst_t *kk3[LOW_MAX_PARAMS + 3];
                    proven_size_t nn3 = 0;
                    kk3[nn3++] = &mnode3;
                    kk3[nn3++] = (low_cst_t *)k[*pos];
                    for (proven_size_t q = *pos + 2; q < end && nn3 < LOW_MAX_PARAMS + 3; q++)
                        { kk3[nn3++] = (low_cst_t *)k[q]; LOW_HWM("ir:call-args", nn3, LOW_MAX_PARAMS + 3); }
                    proven_size_t p3 = 0;
                    ir_value(c, kk3, &p3, nn3);
                    *pos = end;
                    return;
                }
                // ★ 마디와 이름을 가르는 것은 **타입을 따라가 보는 것**이다:
                //   수신자의 타입에서 마디를 하나씩 내려가다가, **그 타입에 그 이름의 op 이
                //   있으면 거기가 이름**이다. 그 뒤는 전부 인자다.
                //   ☞ *"마지막 낱말이 이름"* 으로 잡으면 **인자를 이름으로 오인한다**
                //     (`method s plus 5` 에서 `5` 가 원자가 아니라 다행이지만
                //      `method o i twice` 에서는 `twice` 앞의 `i` 를 못 가른다).
                proven_size_t first = *pos;
                proven_size_t lastw = 0;
                proven_u8str_view_t mtype = { 0 };
                {
                    bool rf0; proven_size_t rsl0 = ir_local_find(c, k[first]->tok.lex, &rf0);
                    proven_u8str_view_t cur = rf0 ? c->locals[rsl0].tyname : (proven_u8str_view_t){ 0 };
                    for (proven_size_t q = first + 1; q < end && is_atom(k[q]); q++) {
                        proven_u8str_view_t sg = k[q]->tok.lex;
                        if (cur.size) {
                            proven_u8 tb[160];
                            if (cur.size + 1 + sg.size <= sizeof tb) {
                                memcpy(tb, cur.ptr, cur.size); tb[cur.size] = '.';
                                memcpy(tb + cur.size + 1, sg.ptr, sg.size);
                                proven_u8str_view_t qn5 = { .ptr = tb, .size = cur.size + 1 + sg.size };
                                bool qf5; (void)ir_def_find_in(c, qn5, &qf5);
                                if (qf5) { lastw = q; mtype = cur; break; }   // ★ 여기가 이름이다
                            }
                        }
                        {   // 필드로 보고 한 마디 내려간다(위 붙임 점 경로와 같은 방식)
                            bool sf; proven_size_t si = ir_struct_find(c->out, cur, &sf);
                            proven_u8str_view_t nxt = { 0 };
                            if (sf) {
                                const low_ir_struct_t *st = &c->out->structs[si];
                                for (proven_size_t z = 0; z < st->nf; z++)
                                    if (proven_u8str_view_eq(st->f[z].name, sg) && st->f[z].sidx >= 0)
                                        nxt = c->out->structs[st->f[z].sidx].name;
                            }
                            cur = nxt;
                        }
                        if (!cur.size) break;
                    }
                }
                if (!lastw) {
                    ir_fail(c, "E-METHOD-UNDEF",
                            "no op of that name is associated with the receiver's type — declare "
                            "it as `fn <type>.<name> input <recv> <type> . …` (RFC-0062)",
                            nd->line);
                    return;
                }
                // ★ 수신자 사슬을 **직접 낮춘다** — 값 하나를 스택에 올리고 마디를 잇는다.
                //   (붙임 점 문자열을 지어 넘기려다 **지역 버퍼가 살아남지 않는** 것에 물렸다.)
                {
                    bool rf1; proven_size_t rsl1 = ir_local_find(c, k[first]->tok.lex, &rf1);
                    if (!rf1) {
                        ir_fail(c, "E-METHOD-RECV",
                                "`method` receiver is not a local or parameter", nd->line);
                        return;
                    }
                    ir_emit(c, IRW_LOAD, (proven_i64)rsl1);
                    for (proven_size_t z = first + 1; z < lastw; z++)
                        ir_emit(c, IRW_FIELD, (proven_i64)ir_field_intern(c, k[z]->tok.lex));
                }
                // 이름으로 op 을 찾아 **나머지를 인자로** 부른다.
                {
                    proven_u8 nb[192];
                    proven_u8str_view_t mn3 = k[lastw]->tok.lex;
                    proven_u8str_view_t tnm = mtype;
                    if (tnm.size + 1 + mn3.size > sizeof nb) {
                        ir_fail(c, "E-METHOD-UNDEF", "qualified op name is too long", nd->line); return;
                    }
                    memcpy(nb, tnm.ptr, tnm.size); nb[tnm.size] = '.';
                    memcpy(nb + tnm.size + 1, mn3.ptr, mn3.size);
                    proven_u8str_view_t qn6 = { .ptr = nb, .size = tnm.size + 1 + mn3.size };
                    bool qf6; proven_size_t di6 = ir_def_find_in(c, qn6, &qf6);
                    if (!qf6) {
                        ir_fail(c, "E-METHOD-UNDEF", "method not found", nd->line); return;
                    }
                    proven_size_t ap = lastw + 1;
                    const low_ir_def_t *dd = &c->out->defs[di6];
                    for (proven_size_t z = 1; z < dd->nparams; z++) ir_value(c, k, &ap, end);
                    ir_emit(c, IRW_CALL, (proven_i64)di6);
                }
                *pos = end;
                return;
            }
            if (nd->tok.kind == LOW_TOK_METHOD) {
                if (*pos >= end) {
                    ir_fail(c, "E-METHOD-RECV", "`recv..op` needs a receiver", nd->line); return;
                }
                // ★★★ **수신자의 타입을 세 가지 모양에서 찾는다.**
                //   전엔 **맨 이름(지역/파라미터)만** 봤다. 그래서 이런 것들이 못 돌았다:
                //     · `o.i..area`         — **필드**에 메서드 (필드의 타입은 **알 수 있다**)
                //     · `x..grow 2 ..area`  — **메서드 사슬** (앞 op 의 **출력 타입**이 그것이다)
                //   ★ **도구가 이미 알고 있는 것을 안 찾고 있었다** — 그리고 "타입을 모른다" 고
                //     말했다. 그건 사실이 아니었다.
                proven_u8str_view_t tn = { 0 };
                const low_cst_t *rv = k[*pos];
                if (is_atom(rv)) {
                    proven_u8str_view_t v = rv->tok.lex;
                    proven_size_t dot = 0;
                    for (proven_size_t q = 0; q < v.size; q++) if (v.ptr[q] == '.') { dot = q; break; }
                    if (!dot) {                                   // ① 맨 이름 — 지역/파라미터
                        bool rf; proven_size_t rsl = ir_local_find(c, v, &rf);
                        if (rf) tn = c->locals[rsl].tyname;
                    } else {                                      // ② 붙임 점 — 필드를 따라간다
                        proven_u8str_view_t base = { .ptr = v.ptr, .size = dot };
                        bool rf; proven_size_t rsl = ir_local_find(c, base, &rf);
                        proven_u8str_view_t cur = rf ? c->locals[rsl].tyname : (proven_u8str_view_t){ 0 };
                        proven_size_t q = dot;
                        while (cur.size && q < v.size) {
                            proven_size_t e2 = q + 1;
                            while (e2 < v.size && v.ptr[e2] != '.') e2++;
                            proven_u8str_view_t fld = { .ptr = v.ptr + q + 1, .size = e2 - q - 1 };
                            bool sf; proven_size_t si = ir_struct_find(c->out, cur, &sf);
                            cur = (proven_u8str_view_t){ 0 };
                            if (sf) {
                                const low_ir_struct_t *st = &c->out->structs[si];
                                for (proven_size_t z = 0; z < st->nf; z++)
                                    if (proven_u8str_view_eq(st->f[z].name, fld) && st->f[z].sidx >= 0)
                                        cur = c->out->structs[st->f[z].sidx].name;
                            }
                            q = e2;
                        }
                        tn = cur;
                    }
                } else {                                          // ③ 사슬 — 앞 op 의 **출력 타입**
                    // ★ **껍질을 벗긴다.** 저자가/서식기가 괄호를 하나 더 씌우면
                    //   `GROUP(FORM(x, [GROUP(FORM(grow…))]))` 처럼 **FORM 이 한 겹 더** 생긴다.
                    //   그 한 겹 때문에 *"타입을 모른다"* 고 했다 — **껍질일 뿐인데.**
                    //   (서식기가 자기 출력으로 자기를 깨뜨렸고, def-해시 대조가 잡았다.)
                    const low_cst_t *pv = rv;
                    for (int guard = 0; guard < 8 && pv; guard++) {
                        if (pv->kind == LOW_CST_GROUP && pv->nkids == 1) { pv = pv->kids[0]; continue; }
                        if (pv->kind == LOW_CST_FORM && pv->nkids == 1) { pv = pv->kids[0]; continue; }
                        break;
                    }
                    if (!pv || pv->kind != LOW_CST_FORM || !pv->nkids || !is_atom(pv->kids[0]))
                        goto method_recv_unknown;
                    const low_cst_t *hd = pv->kids[0];
                    proven_u8str_view_t cn = hd->tok.lex;
                    if (hd->tok.kind == LOW_TOK_METHOD) {         // 앞도 메서드였다 — 자격 이름을 짓는다
                        const low_cst_t *ir2 = pv;
                        if (ir2->nkids >= 2 && is_atom(ir2->kids[1])) {
                            bool rf; proven_size_t rsl = ir_local_find(c, ir2->kids[1]->tok.lex, &rf);
                            if (rf && c->locals[rsl].tyname.size) {
                                proven_u8 qb[128];
                                proven_u8str_view_t bt = c->locals[rsl].tyname;
                                if (bt.size + 1 + cn.size <= sizeof qb) {
                                    memcpy(qb, bt.ptr, bt.size); qb[bt.size] = '.';
                                    memcpy(qb + bt.size + 1, cn.ptr, cn.size);
                                    proven_u8str_view_t qn2 = { .ptr = qb, .size = bt.size + 1 + cn.size };
                                    bool qf2; proven_size_t qi2 = ir_def_find_in(c, qn2, &qf2);
                                    if (qf2) tn = c->out->defs[qi2].out_tyname;
                                }
                            }
                        }
                    } else if (hd->tok.kw == LOW_KW_NONE && veq(cn, "method")) {
                        // ★★★★★ **안쪽도 전위 `method` 다** (2026-08-25 — `..` 폐기의 뒤끝).
                        //   사슬 `method (method x grow 2) area` 에서 앞 op 의 출력 타입을
                        //   얻으려면 안쪽 폼을 읽어야 하는데, 이 가지는 **`..` 토큰만**
                        //   알아봤다. `..` 를 없앤 순간 사슬이 *"타입을 모른다"* 가 됐다 —
                        //   ★ **표기를 바꾸면 그 표기를 읽던 자리를 전부 따라가야 한다.**
                        //   마디를 따라 내려가다 `<타입>.<마디>` op 이 있으면 거기가 이름이고,
                        //   그 op 의 출력 타입이 곧 이 수신자의 타입이다(위 ② 와 같은 걸음).
                        if (pv->nkids >= 3 && is_atom(pv->kids[1])) {
                            bool rf; proven_size_t rsl = ir_local_find(c, pv->kids[1]->tok.lex, &rf);
                            proven_u8str_view_t cur = rf ? c->locals[rsl].tyname
                                                        : (proven_u8str_view_t){ 0 };
                            for (proven_size_t j = 2; cur.size && j < pv->nkids; j++) {
                                if (!is_atom(pv->kids[j])) break;
                                proven_u8str_view_t seg = pv->kids[j]->tok.lex;
                                proven_u8 qb[128];
                                if (cur.size + 1 + seg.size > sizeof qb) break;
                                memcpy(qb, cur.ptr, cur.size); qb[cur.size] = '.';
                                memcpy(qb + cur.size + 1, seg.ptr, seg.size);
                                proven_u8str_view_t qn3 = { .ptr = qb, .size = cur.size + 1 + seg.size };
                                bool qf3; proven_size_t qi3 = ir_def_find_in(c, qn3, &qf3);
                                if (qf3) { tn = c->out->defs[qi3].out_tyname; break; }
                                // 이름이 아니면 마디다 — 필드를 따라 한 칸 내려간다.
                                bool sf; proven_size_t si = ir_struct_find(c->out, cur, &sf);
                                proven_u8str_view_t nx = { 0 };
                                if (sf) {
                                    const low_ir_struct_t *st = &c->out->structs[si];
                                    for (proven_size_t z = 0; z < st->nf; z++)
                                        if (proven_u8str_view_eq(st->f[z].name, seg) && st->f[z].sidx >= 0)
                                            nx = c->out->structs[st->f[z].sidx].name;
                                }
                                cur = nx;
                            }
                        }
                    } else {
                        bool qf2; proven_size_t qi2 = ir_def_find_in(c, cn, &qf2);
                        if (qf2) tn = c->out->defs[qi2].out_tyname;
                    }
                }
                method_recv_unknown:
                if (!tn.size) {
                    ir_fail(c, "E-METHOD-RECV",
                            "cannot determine the receiver's TYPE here — the op is found by the "
                            "receiver's declared type. A local/parameter, a field path (`o.i..m`), "
                            "or another method's result (`x..a ..b`) all work; an arbitrary "
                            "expression does not (yet)",
                            nd->line);
                    return;
                }
                proven_u8str_view_t mn = nd->tok.lex;
                proven_u8 buf[128];
                if (tn.size + 1 + mn.size > sizeof buf) {
                    ir_fail(c, "E-METHOD-UNDEF", "qualified op name is too long", nd->line); return;
                }
                memcpy(buf, tn.ptr, tn.size); buf[tn.size] = '.';
                memcpy(buf + tn.size + 1, mn.ptr, mn.size);
                proven_u8str_view_t qn = { .ptr = buf, .size = tn.size + 1 + mn.size };
                bool qf; proven_size_t qi = ir_def_find_in(c, qn, &qf);
                if (!qf) {
                    ir_fail(c, "E-METHOD-UNDEF",
                            "no op of that name is associated with the receiver's type — declare it "
                            "as `fn <type>.<name> input <recv> <type> . …` (RFC-0062)",
                            nd->line);
                    return;
                }
                proven_size_t np = c->out->defs[qi].nparams;
                if (*pos + np > end) {
                    ir_fail(c, "E-IR-ARITY", "this method was called with TOO FEW arguments", nd->line);
                    return;
                }
                for (proven_size_t q = 0; q < np; q++) ir_value(c, k, pos, end);   // 수신자 = 첫 인자
                ir_emit(c, IRW_CALL, (proven_i64)qi);
                return;
            }
            // ★ RFC-0030 D2′ — cap args 질의. `count <cap>` 는 전용 낱말로(타입 유도 —
            //   런타임 태그 분기 0), `arg` 는 cap 을 **댈 때만** 통과한다(인자는 건네받는
            //   권리다 — ambient 금지). cap 토큰은 정적 권한이라 런타임 값이 필요 없다.
            {
                proven_u8str_view_t ac = ir_argscap_of(c->def_form);
                if (ac.size && veq(nd->tok.lex, "count") && *pos < end && is_atom(k[*pos]) &&
                    ir_names_cap(c->def_form, k[*pos]->tok.lex, "args")) {
                    (*pos)++; ir_emit(c, IRW_ARGC, 0); return;
                }
                // ★ `env_get <cap env> <name>` — 이름으로 조회. args 와 **같은 규율**:
                //   cap 을 **대야** 통과한다(ambient 금지). 없으면 none — 센티널 없음.
                // ★ `write_out <cap io> <fd> <bytes>` — 표준출력/표준오류로 바이트를 낸다.
                //   args·env 와 **같은 규율**: cap 을 대야 통과한다. **숨은 stdout 이 없다.**
                if (veq(nd->tok.lex, "write_out")) {
                    proven_u8str_view_t ic = ir_capname_of(c->def_form, "io");
                    // ★★★ WO-0221 (소유자 결정 A3-a) — 권한을 안 댄 호스트 잎은 **프로그램이 틀린 것**이다(`E-CAP-MISSING`). 전엔 `E-IR-UNSUP`
                    //   («처리기가 아직 못 한다») 이라 `--check` 가 «낮출 수 없는 op» 주석과 함께 초록이었다. 아래 열 자리가 같은 코드다.
                    if (!(ic.size && *pos < end && is_atom(k[*pos]) &&
                          ir_names_cap(c->def_form, k[*pos]->tok.lex, "io"))) {
                        if (ir_cap_cause(c, "io", k, pos, end, nd->line)) return;
                        ir_fail(c, "E-CAP-MISSING",
                                "`write_out` writes to the process output and needs the `cap io` value "
                                "the entry received as its FIRST operand — output is a RIGHT you are "
                                "handed (RFC-0030 D2), never ambient. There is no hidden stdout", nd->line);
                        return;
                    }
                    (*pos)++;
                    ir_value(c, k, pos, end);      // fd
                    ir_value(c, k, pos, end);      // bytes
                    ir_emit(c, IRW_WRITE, 0); return;
                }
                // ★★★ `alloc_bytes <cap allocator> capacity <n>` — 바이트를 **얻는다**.
                //   RFC-0043 D1: *"할당하려면 allocator 를 인자로 받아야 한다. ambient 힙 없음."*
                //   ★ 그래서 **cap 을 대야** 한다. 전역에서 몰래 꺼내는 판이면 cap 은 그저
                //     전역 힙을 여는 열쇠고, 그건 D1 이 금지한 것을 토큰 뒤에 숨긴 것이다 —
                //     `stack_new R capacity n` 이 region 을 앞에 두는 것과 **같은 이유**다.
                //   ★ `capacity` 는 **이미 있는 절 표식**이다(stack_new 와 공유) — 새 어휘 0.
                //     ☞ SPEC-004 §4.5 는 `alloc τ R count n` 이라 적었으나 `count` 는 이미
                //       op(IRW_COUNT)이고 `alloc` 은 이미 효과 이름이라 철자를 바꿨다(RFC-0043 에 기록).
                if (veq(nd->tok.lex, "alloc_bytes")) {
                    // ★★★ **어디서 할당하는가 — cap 이거나 region 이다**(SPEC-004 §4.5 · RFC-0043 D7:
                    //   *"region = allocator 종류 + 수명"*). 둘 다 **첫 피연산자로 대야** 한다.
                    //   ★ region 판은 그 이름이 **어휘 region 블록**이 연 지역이면 통과한다 —
                    //     블록이 끝나면 그 할당은 **일괄 해제**된다(수명 = 스코프).
                    // ★ region 이름은 **지역이 아니다** — 블록이 연 이름이다(값이 아니라 자리).
                    if (*pos < end && is_atom(k[*pos])) {
                        if (ir_is_region_name(c, k[*pos]->tok.lex)) {
                            proven_u8 root = ir_region_root(k[*pos]->tok.lex);
                            (*pos)++;
                            if (!ir_capacity_mark(c, k, pos, end, nd->line)) return;
                            ir_value(c, k, pos, end);
                            ir_emit(c, IRW_ALLOCB, (proven_i64)root); return;
                        }
                    }
                    // ★★★★ **권한은 이름으로 가린다** (RFC-0112 D5(4) · D3). 전엔 `cap allocator` 가운데
                    //   **첫째**만 알아봐서 둘째 권한을 대면 거절됐다(F8). 이제 그 이름이 가리키는 입력의
                    //   종류가 답이다: `cap allocator` → 고정 창(0) · `cap heap` → 자라는 뿌리(1).
                    int aroot = -1;
                    // ★★★ RFC-0112 D5(3) (WO-0212) — **영역 매개변수**로도 깎는다(F7). 매개변수는 고정 창이다
                    //   (D3(4)): 부른 쪽이 연 영역의 커서 위에서 깎고, 그 영역의 `end` 가 걷는다. 부른 op 이
                    //   자기 안에서 같은 뿌리의 영역을 여는 경우는 검사기가 `E-ALLOC-NESTED` 로 막는다.
                    if (*pos < end && is_atom(k[*pos]) && c->def_form) {
                        low_op_header_t rh = low_op_header(c->def_form);
                        for (proven_size_t q = 0; q < rh.np && aroot < 0; q++) {
                            if (!proven_u8str_view_eq(rh.p[q].name, k[*pos]->tok.lex)) continue;
                            for (proven_size_t z = rh.p[q].ts; z < rh.p[q].te && z < c->def_form->nkids; z++)
                                if (is_atom(c->def_form->kids[z]) && veq(c->def_form->kids[z]->tok.lex, "region")) { aroot = 0; break; }
                        }
                    }
                    // ★★★★ RFC-0112 D6 (WO-0213) — **actor 의 권한 칸**으로 깎는다(`alloc_bytes root capacity n`).
                    if (aroot < 0 && *pos < end && is_atom(k[*pos]))
                        for (proven_size_t q = 0; q < c->nsfield; q++)
                            if (proven_u8str_view_eq(c->sfield[q], k[*pos]->tok.lex) && c->sfcap[q])
                                { aroot = (int)c->sfcap[q] - 1; break; }
                    if (aroot < 0 && *pos < end && is_atom(k[*pos]) && c->def_form) {
                        low_op_header_t ah = low_op_header(c->def_form);
                        for (proven_size_t q = 0; q < ah.np && aroot < 0; q++) {
                            proven_size_t cw = ah.p[q].core;
                            if (!proven_u8str_view_eq(ah.p[q].name, k[*pos]->tok.lex)) continue;
                            if (cw + 1 < ah.p[q].te && cw + 1 < c->def_form->nkids &&
                                is_atom(c->def_form->kids[cw]) && is_atom(c->def_form->kids[cw + 1]) &&
                                veq(c->def_form->kids[cw]->tok.lex, "cap")) {
                                if (veq(c->def_form->kids[cw + 1]->tok.lex, "allocator")) aroot = 0;
                                else if (veq(c->def_form->kids[cw + 1]->tok.lex, "heap")) aroot = 1;
                            }
                        }
                    }
                    // ★★★ WO-0219 — 이 거절은 **프로그램이 틀렸다** 이지 «도구가 아직 못 한다» 가 아니다. 전엔 `E-IR-UNSUP` 이라
                    //   `--check` 가 «낮출 수 없는 op» 주석을 달고 **초록**을 냈다(영역 밖의 `alloc_bytes capacity 8` ·
                    //   없는 이름). RFC-0112 D8(7) 은 그것을 거절이라 적는다.
                    if (aroot < 0) {
                        ir_fail(c, "E-ALLOC-NOROOT",
                                "`alloc_bytes` needs the ROOT it carves from as its first operand, BY NAME — a "
                                "`cap allocator` or `cap heap` input, a region parameter, an open `region` block, "
                                "or a capability field of this actor. There is no ambient heap (RFC-0043 D1). The "
                                "operand may be left out (`alloc_bytes capacity n`) only INSIDE a region block, "
                                "where the innermost region fills it; a capability is never filled in for you "
                                "(RFC-0112 D8(7))", nd->line);
                        return;
                    }
                    (*pos)++;
                    if (!ir_capacity_mark(c, k, pos, end, nd->line)) return;
                    ir_value(c, k, pos, end);      // n
                    ir_emit(c, IRW_ALLOCB, (proven_i64)aroot); return;
                }
                // ★★★ `read_in <cap io> <fd> <dst>` — 표준입력에서 호출자 버퍼로.
                //   ★ **같은 `cap io` 다** — fd 0·1·2 는 프로세스가 태어날 때 받는 한 묶음이고,
                //     그것을 셋으로 쪼개면 권한이 아니라 장식이 된다.
                //   ☞ **`write_out` 의 단순한 "짝" 이 아니다**: 양쪽 다 가운데 버퍼가 있지만
                //     비싸고 **실패할 수 있는** 연산이 반대편에 있다 — 읽기는 **채우기**(syscall·EOF),
                //     쓰기는 **비우기**(syscall·ENOSPC·데이터 손실). mirror image 이지 parallel 이 아니고,
                //     그것이 `finish`+E-OWN-LEAK 을 **쓰기에만** 두는 이유다.
                //   ★ 여기(리프)가 하는 일은 **채우기**다. 비우기의 갈래(peek/take/toss)는 `lib/io.low`.
                if (veq(nd->tok.lex, "read_in")) {
                    proven_u8str_view_t ic = ir_capname_of(c->def_form, "io");
                    if (!(ic.size && *pos < end && is_atom(k[*pos]) &&
                          ir_names_cap(c->def_form, k[*pos]->tok.lex, "io"))) {
                        if (ir_cap_cause(c, "io", k, pos, end, nd->line)) return;
                        ir_fail(c, "E-CAP-MISSING",
                                "`read_in` reads the process input and needs the `cap io` value as "
                                "its FIRST operand — input is a RIGHT you are handed (RFC-0030 D2), "
                                "never ambient. There is no hidden stdin", nd->line);
                        return;
                    }
                    (*pos)++;
                    ir_value(c, k, pos, end);      // fd
                    ir_value(c, k, pos, end);      // dst
                    ir_emit(c, IRW_RDIN, 0); return;
                }
                // ★★★ **reactor** (RFC-0071 A1). `read_in` 과 **같은 규율**: `cap io` 를 첫
                //   피연산자로 이름 대조한다 — 핸들은 u64 라 **지어낼 수 있으므로**, 권한은
                //   매 호출에서 보여야 한다(`lib/pool.low` 이 세대로 하는 일을 여기선 cap 이 한다).
                if (veq(nd->tok.lex, "reactor_new") || veq(nd->tok.lex, "r_read") ||
                    veq(nd->tok.lex, "r_write")) {
                    bool is_new = veq(nd->tok.lex, "reactor_new");
                    bool is_wr  = veq(nd->tok.lex, "r_write");
                    proven_u8str_view_t ic = ir_capname_of(c->def_form, "io");
                    if (!(ic.size && *pos < end && is_atom(k[*pos]) &&
                          ir_names_cap(c->def_form, k[*pos]->tok.lex, "io"))) {
                        if (ir_cap_cause(c, "io", k, pos, end, nd->line)) return;
                        ir_fail(c, "E-CAP-MISSING",
                                is_new
                                  ? "`reactor_new` needs the `cap io` value as its FIRST operand. "
                                    "A reactor is the right to do I/O made concrete — there is no "
                                    "ambient reactor, for the same reason there is no ambient heap "
                                    "(RFC-0043 D1, RFC-0071 D2)"
                                  : (is_wr
                                  ? "`r_write` needs the `cap io` value as its FIRST operand — the "
                                    "same rule as `r_read`. The reactor handle is a plain integer "
                                    "and could be INVENTED, so the right must be visible at every "
                                    "call, not just at creation"
                                  : "`r_read` needs the `cap io` value as its FIRST operand. The "
                                    "reactor handle is a plain integer and could be INVENTED, so "
                                    "the right must be visible at every call — not just at creation"),
                                nd->line);
                        return;
                    }
                    (*pos)++;
                    ir_value(c, k, pos, end);      // reactor_new: mem   · r_read/r_write: 핸들
                    ir_value(c, k, pos, end);      // reactor_new: depth · r_read/r_write: fd
                    if (!is_new) ir_value(c, k, pos, end);   // r_read: dst · r_write: src
                    ir_emit(c, is_new ? IRW_RNEW : (is_wr ? IRW_RWRITE : IRW_RREAD), 0); return;
                }
                // ★★★ **스트림 리프 넷** (RFC-0069 §6) — 전부 `cap file_system` 을 **대야** 한다.
                //   ☞ `read_file`(파일 통째 읽기)은 여기 **있었다가 걷혔다**: 스트림 리프가 서자
                //     `lib/file.low` 의 `f_slurp` 이 그것을 Lowent 로 쓸 수 있게 됐고, 그러면
                //     같은 뜻의 두 경로가 되어 §2.5(동의어 금지)에 걸린다. **리프 규칙의 부채 상환.**
                {
                    // ★★★ cap tty (2026-07-26) — 터미널의 **권한 있는 절반**. 순수 절반(ANSI
                    //   조립·화면 diff·표시 폭)은 lib/term.low 가 이미 하고, 커널이 필요한 것은
                    //   셋뿐이다: raw 모드·키 읽기·화면 크기. 파일 리프와 **같은 규율**로
                    //   `cap tty` 를 든 op 만 부를 수 있다(안 들었으면 E-IR-UNSUP).
                    static const struct { const char *w; low_irw_t k; int n; } TOPS[] = {
                        { "tty_raw",  IRW_TTYRAW,  1 },   // (on)  → bool
                        { "tty_read", IRW_TTYREAD, 1 },   // (dst) → option u64 (0 = 지금은 없음)
                        { "tty_size", IRW_TTYSIZE, 0 },   // ()    → option u64 (행<<32 | 열)
                    };
                    for (size_t ti = 0; ti < sizeof TOPS / sizeof TOPS[0]; ti++) {
                        if (!veq(nd->tok.lex, TOPS[ti].w)) continue;
                        proven_u8str_view_t tc = ir_capname_of(c->def_form, "tty");
                        if (!(tc.size && *pos < end && is_atom(k[*pos]) &&
                              ir_names_cap(c->def_form, k[*pos]->tok.lex, "tty"))) {
                            if (ir_cap_cause(c, "tty", k, pos, end, nd->line)) return;
                            ir_fail(c, "E-CAP-MISSING",
                                    "a terminal leaf needs the `cap tty` this op received — write "
                                    "`tty_read <cap> …`. Without the capability the right is not held, "
                                    "and a right nobody holds cannot be exercised (RFC-0011)", nd->tok.line);
                            return;
                        }
                        (*pos)++;   // cap 이름을 먹는다 — 값이 아니라 **권한 표식**이다
                        for (int q = 0; q < TOPS[ti].n; q++) {
                            if (*pos >= end) { ir_fail(c, "E-IR-ARITY", "terminal leaf: missing operand", nd->tok.line); return; }
                            ir_value(c, k, pos, end);
                        }
                        ir_emit(c, TOPS[ti].k, 0);
                        return;
                    }
                    // ★★★★★ **시계 리프** (RFC-0090 N1, 2026-08-07). tty 와 **같은 규율**:
                    //   `cap clock` 을 든 op 만 부를 수 있고, 안 들었으면 E-IR-UNSUP.
                    static const struct { const char *w; low_irw_t k; int n; } CLOPS[] = {
                        { "time_now",   IRW_TIMENOW,   0 },   // ()   → u64 단조 ns
                        { "time_sleep", IRW_TIMESLEEP, 1 },   // (ms) → u64 실제로 잔 ms
                        { "time_local", IRW_TIMELOCAL, 0 },   // ()   → u64 날짜시각 팩
                    };
                    for (size_t ci = 0; ci < sizeof CLOPS / sizeof CLOPS[0]; ci++) {
                        if (!veq(nd->tok.lex, CLOPS[ci].w)) continue;
                        proven_u8str_view_t cc = ir_capname_of(c->def_form, "clock");
                        if (!(cc.size && *pos < end && is_atom(k[*pos]) &&
                              ir_names_cap(c->def_form, k[*pos]->tok.lex, "clock"))) {
                            if (ir_cap_cause(c, "clock", k, pos, end, nd->line)) return;
                            ir_fail(c, "E-CAP-MISSING",
                                    "a clock leaf needs the `cap clock` this op received — write "
                                    "`time_now <cap>`. Reading a clock has no side effect you can see, "
                                    "but it BREAKS DETERMINISM: the same input gives a different answer. "
                                    "That is why it is a right and not a calculation (RFC-0011 · RFC-0090)",
                                    nd->tok.line);
                            return;
                        }
                        (*pos)++;   // cap 이름을 먹는다
                        for (int q = 0; q < CLOPS[ci].n; q++) {
                            if (*pos >= end) { ir_fail(c, "E-IR-ARITY", "clock leaf: missing operand", nd->tok.line); return; }
                            ir_value(c, k, pos, end);
                        }
                        ir_emit(c, CLOPS[ci].k, 0);
                        return;
                    }
                    // ★★★ **OS 엔트로피 — cap random** (RFC-0090 N3b, 2026-08-11).
                    //   시계와 같은 자리: 결정성을 깨므로 계산이 아니라 **권리**다.
                    //   ★ 재현 가능한 `rng_next` 와 **일부러 갈라 뒀다**: 시험은 재현되어야
                    //     하고 열쇠는 예측되면 안 된다. 한 낱말로는 둘 다 못 한다.
                    if (veq(nd->tok.lex, "random_bytes")) {
                        proven_u8str_view_t rc = ir_capname_of(c->def_form, "random");
                        if (!(rc.size && *pos < end && is_atom(k[*pos]) &&
                              ir_names_cap(c->def_form, k[*pos]->tok.lex, "random"))) {
                            if (ir_cap_cause(c, "random", k, pos, end, nd->line)) return;
                            ir_fail(c, "E-CAP-MISSING",
                                    "`random_bytes` needs the `cap random` this op received — write "
                                    "`random_bytes <cap> <dst>`. Asking the OS for entropy breaks "
                                    "determinism AND is the material of keys and tokens: where it came "
                                    "from must be visible in the signature (RFC-0011 · RFC-0090 N3b). "
                                    "For a reproducible stream use `rng_next`, which needs no capability",
                                    nd->tok.line);
                            return;
                        }
                        (*pos)++;                       // cap 이름을 먹는다
                        if (*pos >= end) { ir_fail(c, "E-IR-ARITY", "random_bytes: missing destination", nd->tok.line); return; }
                        ir_value(c, k, pos, end);       // dst
                        ir_emit(c, IRW_RANDBYTES, 0);
                        return;
                    }
                    static const struct { const char *w; low_irw_t k; int n; } FOPS[] = {
                        { "file_open",  IRW_FOPEN,  2 },   // (path, mode)
                        { "file_read",  IRW_FREAD,  2 },   // (fd, dst)
                        { "file_write", IRW_FWRITE, 2 },   // (fd, bytes)
                        { "file_close", IRW_FCLOSE, 1 },   // (fd)
                        { "file_seek",  IRW_FSEEK,  3 },   // (fd, off, whence)
                        { "dir_open",   IRW_DOPEN,  1 },   // (path)
                        { "dir_read",   IRW_DREAD,  2 },   // (dh, dst)
                        { "dir_close",  IRW_DCLOSE, 1 },   // (dh)
                        { "file_type",  IRW_FTYPE,  1 },   // (path)
                        { "link_type",  IRW_LTYPE,  1 },   // (path) — 심링크를 따라가지 않는다(lstat)
                        { "dir_make",   IRW_DMAKE,  1 },   // (path)
                        { "path_remove",IRW_PREMOVE,1 },   // (path)
                        { "path_rename",IRW_PRENAME,2 },   // (old, new)
                    };
                    for (size_t fi = 0; fi < sizeof FOPS / sizeof FOPS[0]; fi++) {
                        if (!veq(nd->tok.lex, FOPS[fi].w)) continue;
                        proven_u8str_view_t fc = ir_capname_of(c->def_form, "file_system");
                        if (!(fc.size && *pos < end && is_atom(k[*pos]) &&
                              ir_names_cap(c->def_form, k[*pos]->tok.lex, "file_system"))) {
                            if (ir_cap_cause(c, "file_system", k, pos, end, nd->line)) return;
                            ir_fail(c, "E-CAP-MISSING",
                                    "this stream op touches the file system and needs the "
                                    "`cap file_system` value as its FIRST operand — the file system "
                                    "is a RIGHT you are handed (RFC-0011), never ambient", nd->line);
                            return;
                        }
                        (*pos)++;
                        for (int q = 0; q < FOPS[fi].n; q++) ir_value(c, k, pos, end);
                        ir_emit(c, FOPS[fi].k, 0); return;
                    }
                }
                // ★★★ **소켓 리프** (2026-07-23) — 전부 `cap net` 을 **대야** 한다(스트림 리프와 같은 규율).
                {
                    static const struct { const char *w; low_irw_t k; int n; } NOPS[] = {
                        { "net_pair",  IRW_NPAIR,  0 },   // ()          → option u64 (두 핸들 팩)
                        { "net_send",  IRW_NSEND,  2 },   // (fd, bytes) → option u64
                        { "net_recv",  IRW_NRECV,  2 },   // (fd, dst)   → option u64
                        { "net_close", IRW_NCLOSE, 1 },   // (fd)        → bool
                        { "net_listen",  IRW_NLISTEN,  1 },   // (port)  → option u64 (0=임시)
                        { "net_port",    IRW_NPORT,    1 },   // (fd)    → option u64 (묶인 포트)
                        { "net_connect", IRW_NCONNECT, 2 },   // (addr u32, port) → option u64
                        { "net_accept",  IRW_NACCEPT,  1 },   // (fd)    → option u64
                        { "net_resolve", IRW_NRESOLVE, 1 },   // (이름)  → option u32 (IPv4)
                    };
                    for (size_t ni = 0; ni < sizeof NOPS / sizeof NOPS[0]; ni++) {
                        if (!veq(nd->tok.lex, NOPS[ni].w)) continue;
                        proven_u8str_view_t nc = ir_capname_of(c->def_form, "net");
                        if (!(nc.size && *pos < end && is_atom(k[*pos]) &&
                              ir_names_cap(c->def_form, k[*pos]->tok.lex, "net"))) {
                            if (ir_cap_cause(c, "net", k, pos, end, nd->line)) return;
                            ir_fail(c, "E-CAP-MISSING",
                                    "this socket op touches the network and needs the "
                                    "`cap net` value as its FIRST operand — the network "
                                    "is a RIGHT you are handed (RFC-0011), never ambient", nd->line);
                            return;
                        }
                        (*pos)++;
                        for (int q = 0; q < NOPS[ni].n; q++) ir_value(c, k, pos, end);
                        ir_emit(c, NOPS[ni].k, 0); return;
                    }
                }
                // ★★★ **프로세스 잎** (RFC-0136, 2026-10-03) — 소켓 잎과 같은 규율: 첫 피연산자는 `cap process` 다.
                {
                    static const struct { const char *w; low_irw_t k; int n; } POPS[] = {
                        { "proc_spawn", IRW_PSPAWN, 1 },   // (argv)   → option u64
                        { "proc_read",  IRW_PREAD,  2 },   // (h, dst) → option u64
                        { "proc_poll",  IRW_PPOLL,  1 },   // (h)      → option u64
                        { "proc_wait",  IRW_PWAIT,  1 },   // (h)      → option u64
                        { "proc_kill",  IRW_PKILL,  1 },   // (h)      → bool
                    };
                    for (size_t pi = 0; pi < sizeof POPS / sizeof POPS[0]; pi++) {
                        if (!veq(nd->tok.lex, POPS[pi].w)) continue;
                        proven_u8str_view_t pc = ir_capname_of(c->def_form, "process");
                        if (!(pc.size && *pos < end && is_atom(k[*pos]) &&
                              ir_names_cap(c->def_form, k[*pos]->tok.lex, "process"))) {
                            if (ir_cap_cause(c, "process", k, pos, end, nd->line)) return;
                            ir_fail(c, "E-CAP-MISSING",
                                    "this process op starts or watches another program and needs the "
                                    "`cap process` value as its FIRST operand — running programs is a RIGHT "
                                    "you are handed (RFC-0011 · RFC-0136), never ambient", nd->line);
                            return;
                        }
                        (*pos)++;
                        for (int q = 0; q < POPS[pi].n; q++) ir_value(c, k, pos, end);
                        ir_emit(c, POPS[pi].k, 0); return;
                    }
                }
                if (veq(nd->tok.lex, "env_get")) {
                    proven_u8str_view_t ec = ir_capname_of(c->def_form, "env");
                    if (!(ec.size && *pos < end && is_atom(k[*pos]) &&
                          ir_names_cap(c->def_form, k[*pos]->tok.lex, "env"))) {
                        ir_fail(c, "E-CAP-MISSING",
                                "`env_get` reads the environment and needs the `cap env` value the "
                                "entry received as its FIRST operand — the environment is a RIGHT you "
                                "are handed (RFC-0030 D2), not ambient authority", nd->line);
                        return;
                    }
                    (*pos)++;
                    ir_value(c, k, pos, end);
                    ir_emit(c, IRW_ENVGET, 0); return;
                }
                if (veq(nd->tok.lex, "arg")) {
                    if (!(ac.size && *pos < end && is_atom(k[*pos]) &&
                          ir_names_cap(c->def_form, k[*pos]->tok.lex, "args"))) {
                        ir_fail(c, "E-CAP-MISSING",
                                "`arg` reads the program arguments and needs the `cap args` value "
                                "the entry received as its FIRST operand — arguments are a RIGHT "
                                "you are handed (RFC-0030 D2), not ambient authority", nd->line);
                        return;
                    }
                    (*pos)++;
                    ir_value(c, k, pos, end);
                    ir_emit(c, IRW_ARGV, 0); return;
                }
            }
            // ★★★ **`is_none` — 정본 부록 D 에 있는데 하강이 없었다** (WO-0176, 2026-09-03).
            //   부록 D · 타입검사 · 검사 표 **세 곳**에 다 있는 빌트인이 여기서 `E-IR-UNDEF` 를 냈다:
            //   `LOW_BUILTINS` 에 IR 낱말이 없고 `LOW_VOCAB_ONLY`("문맥 의존 전위")에만 있어서다.
            //   `check-builtins` 는 **이름**만 대므로 못 봤다 — *있다* 와 *내려간다* 는 다른 사실이다.
            //   ⇒ `match` 의 `none` 갈래가 이미 쓰는 모양 그대로 낮춘다: `is.some` 뒤 `not`.
            //     새 IR 낱말 0 · VM·C 뒤끝 변경 0 (둘 다 그 두 낱말을 이미 안다).
            if (veq(nd->tok.lex, "is_none")) {
                if (*pos >= end) {
                    ir_fail_r(c, "E-IR-ARITY",
                            "this prefix word was given TOO FEW operands. A form is `head operand*` "
                            "and ends at its closer `.` — count the operands this word needs.",
                            "R-FEWER-PARAMS", nd->line);
                    return;
                }
                ir_value(c, k, pos, end);
                ir_emit(c, IRW_ISSOME, 0);
                ir_emit(c, IRW_NOT, 0);
                return;
            }
            const ir_builtin_t *b = ir_builtin(nd->tok.lex);
            if (!b && veq(nd->tok.lex, "bitset_insert")) b = ir_builtin((proven_u8str_view_t){ .ptr = (const proven_byte_t *)"add", .size = 3 });   // RFC-0113 R5
            // ★★★★★ **`call_builtin <이름> …`** (RFC-0125) — 계산 잎의 이름을 **자리에** 가둔다.
            //   전역에 느는 이름은 `call_builtin` 하나뿐이고, 꾸러미(RFC-0126)가 잎을 더해도 안 는다.
            //   ☞ 머리가 `call_builtin` 이면 **다음 원자가 이름**이다. 아는 이름이면 그것으로 내리고,
            //     모르는 이름이면 `E-BUILTIN-NAME` — 조용히 사용자 op 으로 넘기지 않는다(오타가 숨는다).
            if (!b && veq(nd->tok.lex, "call_builtin")) {
                if (*pos >= end || !is_atom(k[*pos]) || k[*pos]->tok.kind != LOW_TOK_IDENT) {
                    ir_fail(c, "E-BUILTIN-NAME",
                            "`call_builtin` takes the NAME of a computation leaf as its first operand "
                            "(`call_builtin sha256 msg out`). The name is scoped to this position on "
                            "purpose: it is not a global word.", nd->line);
                    return;
                }
                b = ir_call_builtin(k[*pos]->tok.lex);
                if (!b) {
                    ir_fail(c, "E-BUILTIN-NAME",
                            "this is not a computation leaf. The names that stand after `call_builtin` "
                            "are a closed set (clmul_lo, clmul_hi, aes_round, aes_round_last, aes_ctr, "
                            "ghash, chacha20, poly1305, aes_gcm, sha256, sha384, sha512, crc32, "
                            "hash_bytes, rng_next, chacha_poly — and the host leaves file_*, dir_*, path_remove, path_rename, link_type, net_*).", nd->line);
                    return;
                }
                (*pos)++;
                nd = k[*pos - 1];   /* 진단의 자리를 이름 쪽으로 옮긴다 */
            }
            // ★ 가둔 이름을 **맨몸으로** 부르면 거절한다 — 그러지 않으면 가둔 것이 아니다.
            else if (b && ir_call_builtin(nd->tok.lex)) {
                ir_fail(c, "E-BUILTIN-BARE",
                        "this computation leaf stands only after `call_builtin` — write "
                        "`call_builtin <name> ...`. Its name is scoped to that position so that the "
                        "global vocabulary does not grow with words a program needs once.", nd->line);
                return;
            }
            if (b) {
                // ★★★ **피연산자가 모자라면 거절한다.** 전엔 `end` 를 넘어가도 그냥 불렀고,
                //   `ir_value` 가 조용히 아무것도 안 하면 스택에 **쓰레기가 남았다**:
                //     `return add a .`  →  **f(3) = 3**   (오류 없음! `add` 가 인자 하나로 돌았다)
                //   그리고 이것이 **줄잇기를 위험하게** 만들었다:
                //     `return add a` ⏎ `mul b 2 .`  →  개행이 닫아 버리고 뒷줄이 **고아**가 된다
                //                                     → **f(3,4) = 3** (11 이어야 한다). 조용히 틀렸다.
                //   초과(extra operands)는 잡으면서 **부족은 안 잡고 있었다** — 한쪽만 본 검사다.
                if (*pos + b->arity > end) {
                    // ★ 개행 닫힘이 없어진 뒤(RFC-0103) 이 진단의 원인은 하나다:
                    //   **정말로 인자가 모자란 것**. 전에는 줄 나눔이 흔한 원인이라
                    //   `soft_close` 로 갈라 `R-JOIN-LINE` 을 실어 보냈는데, 그 원인
                    //   자체가 없어졌으므로 갈래도 없앤다. (같이 없어진 수리 id 를
                    //   `low_repair.c` 에서도 회수했다 — 규칙이 죽으면 처방도 죽는다.)
                    ir_fail_r(c, "E-IR-ARITY",
                            "this prefix word was given TOO FEW operands. A form is `head operand*` "
                            "and ends at its closer `.` — count the operands this word needs.",
                            "R-FEWER-PARAMS",
                            nd->line);
                    return;
                }
                // ★★★ **논리 `and`/`or` 은 단락 평가한다** (RFC-0064 D5).
                //   전엔 **양쪽을 다 계산했다.** 그러면 가드 관용구가 성립하지 않는다:
                //     `and (ne n 0) (eq (mod x n) 0)`  ← 두 번째가 **항상** 돌아 0으로 나눈다.
                //   부수효과가 없는 언어라 **값**은 같지만 **트랩이 다르다** — 그리고 트랩도 의미다.
                //   ⇒ 분기로 낮춘다. 값은 그대로 0/1 이다.
                // ★★★★★ **`value_or` 의 기본값은 지연평가한다** (SPEC-007 §8.4 · DECISION-0003 C1).
                //   전엔 **둘 다 계산했다** — `value_or (some 7) (boom)` 이 값이 있는데도 트랩했다.
                //   정본이 *"성공 시 미평가 — effect 전파 최소"* 라고 **이미 정해 둔** 자리다.
                //   ⇒ `and`/`or` 와 같은 처방: **분기로 낮춘다.**
                //     <대상> STORE t · LOAD t HASVAL · BRZ else
                //     LOAD t VALOR(자기 자신을 기본값으로 — 반드시 present 라 안 쓰인다) · BR end
                //     else: <기본값>
                //   ☞ 그래서 기본값은 **없을 때만** 돈다. 값은 전과 같다.
                if (b->w == IRW_VALOR) {
                    if (c->nlocals >= IR_MAXLOCALS) { ir_fail(c, "E-IR-LOCALS", "value_or: too many locals", nd->line); return; }
                    proven_size_t t = c->nlocals++;
                    c->locals[t].name = (proven_u8str_view_t){ 0 };
                    ir_value(c, k, pos, end);                      // 대상(option|result)
                    ir_emit(c, IRW_STORE, (proven_i64)t);
                    ir_emit(c, IRW_LOAD, (proven_i64)t);
                    ir_emit(c, IRW_HASVAL, 0);
                    proven_size_t brz = ir_emit(c, IRW_BRZ, 0);
                    // 값이 있다 — 껍질을 벗긴다. VALOR 는 두 자리를 먹으므로 자기 자신을 기본값
                    // 자리에 넣는다(present 가 확실하므로 그 자리는 절대 안 쓰인다).
                    ir_emit(c, IRW_LOAD, (proven_i64)t);
                    ir_emit(c, IRW_LOAD, (proven_i64)t);
                    ir_emit(c, IRW_VALOR, 0);
                    proven_size_t brend = ir_emit(c, IRW_BR, 0);
                    ir_at(c, brz)->a = (proven_i64)c->code.len;
                    ir_value(c, k, pos, end);                      // ★ 기본값은 **여기서만** 계산된다
                    ir_at(c, brend)->a = (proven_i64)c->code.len;
                    return;
                }
                if (b->w == IRW_AND || b->w == IRW_OR) {
                    // ★ 기존 낱말만으로 짓는다(BRZ·BR·CONST·NOT) — **새 IR 낱말 0개**.
                    ir_value(c, k, pos, end);                 // 왼쪽을 계산한다
                    if (b->w == IRW_OR) ir_emit(c, IRW_NOT, 0);   // or: 왼쪽이 참이면 건너뛴다
                    proven_size_t brz = ir_emit(c, IRW_BRZ, 0);   // (조건을 소비한다)
                    ir_value(c, k, pos, end);                 // ★ 오른쪽은 **여기서만** 계산된다
                    ir_emit(c, IRW_NOT, 0); ir_emit(c, IRW_NOT, 0);   // 0/1 정규화
                    proven_size_t brend = ir_emit(c, IRW_BR, 0);
                    ir_at(c, brz)->a = (proven_i64)c->code.len;
                    ir_emit(c, IRW_CONST, b->w == IRW_AND ? 0 : 1);   // 단락된 답
                    ir_at(c, brend)->a = (proven_i64)c->code.len;
                    return;
                }
                // ★ 집합 op(contains/remove/집합삽입 add)의 **첫 피연산자 = 비트셋**. 인자를
                //   소비하기 전에 노드를 잡아 두고, 방출 뒤 **선언 폭**을 immediate 에 굽는다.
                const low_cst_t *bset_op0 = (b->w == IRW_CONTAINS || b->w == IRW_BREMOVE || b->w == IRW_ADD)
                                          ? (*pos < end ? k[*pos] : NULL) : NULL;
                // ★★★ RFC-0113 R5 (소유자 2026-10-02 «bitset_insert + 거절») — `add` 는 수의 덧셈 하나만 뜻한다. 비트셋에 넣는 것은
                //   `bitset_insert` 다. 한 낱말이 두 일을 하면 «값을 버린 `add s 100 .`» 을 막는 규칙을 예외 없이 세울 수 없었다.
                //   (비트셋인지는 선언 폭을 아는 지역으로 판정한다 — 폭을 모르는 자리는 하강이 런타임 태그로 가르던 그대로다.)
                if (b->w == IRW_ADD && bset_op0 && ir_operand_bset_w(c, bset_op0) && veq(nd->tok.lex, "add")) {
                    ir_fail(c, "E-BITSET-ADD",
                            "`add` on a bitset is spelled `bitset_insert <set> <n> .` — `add` means numeric addition only "
                            "(RFC-0113 R5), pairing with `bitset_remove`", nd->line);
                    return;
                }
                for (proven_size_t i = 0; i < b->arity; i++) ir_value(c, k, pos, end);
                // ★★★ **atomic ordering** (RFC-0018 §6.1) — 선택 절 `order <name>`(어휘 안 늘림).
                //   하위 3비트에 인코딩(0=relaxed 1=acquire 2=release 3=acq_rel 4=seq_cst). 생략=seq_cst.
                proven_i64 ameta = 0;
                if (b->w >= IRW_ALOAD && b->w <= IRW_AFENCE) {
                    ameta = 4;   // 기본 seq_cst
                    if (*pos + 1 < end && is_atom(k[*pos]) && veq(k[*pos]->tok.lex, "order") && is_atom(k[*pos + 1])) {
                        proven_u8str_view_t o = k[*pos + 1]->tok.lex;
                        proven_i64 ord = veq(o, "relaxed") ? 0 : veq(o, "acquire") ? 1 : veq(o, "release") ? 2
                                       : veq(o, "acq_rel") ? 3 : veq(o, "seq_cst") ? 4 : -1;
                        if (ord < 0) {
                            ir_fail(c, "E-ATOMIC-ORDER", "unknown memory ordering — one of relaxed·acquire·release·acq_rel·seq_cst", nd->line);
                            return;
                        }
                        // ★ op 별 허용 ordering (§6.1): load=relaxed/acquire/seq_cst · store=relaxed/release/seq_cst ·
                        //   fence=acquire/release/acq_rel/seq_cst · rmw/cas=임의.
                        bool ok = true;
                        if (b->w == IRW_ALOAD)  ok = (ord == 0 || ord == 1 || ord == 4);
                        else if (b->w == IRW_ASTORE) ok = (ord == 0 || ord == 2 || ord == 4);
                        else if (b->w == IRW_AFENCE) ok = (ord != 0);
                        if (!ok) {
                            ir_fail(c, "E-ATOMIC-ORDER", "this memory ordering is not valid for this atomic op (a load cannot release, a store cannot acquire, a fence cannot be relaxed — SPEC-018 §6.1)", nd->line);
                            return;
                        }
                        ameta = ord;
                        *pos += 2;
                    }
                }
                proven_size_t ei = ir_emit(c, b->w, ameta);
                // ★ 비트셋 원소 범위검사를 **선언 폭**으로 (F4/F5). 폭을 못 알면(파라미터·집합연산
                //   결과 등) 0 을 남겨 두 백엔드가 똑같이 64(물리 폭)로 후퇴한다 — 발산 없음.
                //   집합삽입 add 는 런타임에 태그/종류로 갈리므로 이 폭이 수치 add 를 안 건드린다.
                if (bset_op0) {
                    proven_u8 bw = ir_operand_bset_w(c, bset_op0);
                    //   비트 23(0x800000) = '비트셋 폭 있음', 하위 바이트 = 폭(1..64). add 의
                    //   미패치 즉값은 **수치 meta** 라 폭과 혼동되므로 전용 플래그로 구분한다.
                    if (bw) ir_at(c, ei)->a = (proven_i64)(0x800000 | bw);
                }
                return;
            }
            // ★ 문맥 머리 낱말은 **같은 이름의 사용자 op 이 있으면 그 부름이다** (2026-10-04, `copy` 와 같은 규칙 — RFC-0132 P3).
            //   전에는 `proc drain` 을 선언하고 문장 머리에서 부르면 이 특수형이 가로채 엉뚱한 `E-IR-ARITY` 가 났다(apps 를 짜다 밟았다).
            //   골든은 «op 이름이 drain 이어도 된다» 를 일부러 확인한다 — 그래서 예약하지 않고 사용자 op 을 고른다.
            bool ctx_user_ = false;
            if (veq(nd->tok.lex, "drain") || veq(nd->tok.lex, "schedule") || veq(nd->tok.lex, "yield") ||
                veq(nd->tok.lex, "channel") || veq(nd->tok.lex, "chsend") || veq(nd->tok.lex, "chrecv") ||
                veq(nd->tok.lex, "await") || veq(nd->tok.lex, "unsafe_fn")) {
                bool uf_ = false; proven_size_t ui_ = ir_def_find_in(c, nd->tok.lex, &uf_);
                // ★ 액터 처리기(`on drain`)는 고르지 않는다 — 처리기는 `send x drain` 으로만 닿고, 같은 파일의 `drain c .` 는 특수형이어야 한다.
                ctx_user_ = uf_ && !c->out->defs[ui_].is_actor;
            }
            // ★★★ **`drain <actor>`** (RFC-0009 — async 배달의 drain 지점, 사용자 결정 D1).
            //   그 인스턴스의 메일박스를 FIFO 로 비운다(문맥 문장 헤드 — 렉서 키워드가 아니다).
            if (!ctx_user_ && veq(nd->tok.lex, "drain")) {
                if (*pos >= end) { ir_fail(c, "E-IR-UNSUP", "`drain` needs an actor instance — `drain <actor>`", nd->line); return; }
                ir_node(c, k[(*pos)++]);            // 인스턴스를 민다
                ir_emit(c, IRW_DRAIN, 0);
                return;
            }
            // ★★★ **`schedule .`** (RFC-0009 D6 — 결정적 스케줄러). `drain <actor>` 이 인스턴스 하나를
            //   비운다면, `schedule` 은 **모든 actor 의 대기 메시지 전부**를 quiescence 까지 배달한다
            //   (문맥 문장 헤드 — 렉서 키워드가 아니다). 인자 없음. 기본 순서 FIFO(결정적).
            if (!ctx_user_ && veq(nd->tok.lex, "schedule")) {
                ir_emit(c, IRW_SCHED, 0);
                return;
            }
            // ★★★ **`yield`** (green thread, RFC-0009 2/3) — 실행 중 태스크가 스케줄러에 양보한다.
            //   문맥 문장 헤드(렉서 키워드 아님). 태스크 안에서만 뜻이 있다(밖이면 no-op 스케줄러 진입).
            if (!ctx_user_ && veq(nd->tok.lex, "yield")) {
                ir_emit(c, IRW_YIELD, 0);
                return;
            }
            // ★★★ **channel** (RFC-0009 3/3) — bounded FIFO 통신 채널. 모두 문맥 낱말(새 키워드 0개).
            //   `channel [<elemtype>]` 만들기 · `chsend <ch> <v>` 넣기(full 이면 블록) · `chrecv <ch>`
            //   빼기(empty 면 블록). 블록은 green thread 의 yield 로 — sender/recver 가 오면 재개한다.
            if (!ctx_user_ && veq(nd->tok.lex, "channel")) {
                // 선택적 원소 타입 토큰을 소비한다(런타임 값은 vmv_t 라 타입은 표식일 뿐 — 완전 타이핑은 후속).
                if (*pos < end && is_atom(k[*pos]) && !veq(k[*pos]->tok.lex, "unbounded")) (*pos)++;
                // ★★★ **`channel … unbounded` 는 거절한다** (RFC-0009 D5 · unbounded mailbox 와 같은 규율).
                //   채널 버퍼는 고정(VM_CHAN_CAP)이라 **진짜로 못 자란다** — 무한이라 하면 반쯤 거짓이다.
                //   RFC 는 unbounded ⇒ alloc 효과(+ RFC-0043 allocator capability)를 요구하는데, 안 쓰는
                //   권한을 요구하는 또 다른 반쪽 거짓이 된다. `bounded 0`·unbounded mailbox 를 거절한 그
                //   규율(닿을 수 없는 약속은 거짓말)로. 진짜 성장(allocator 스레딩)이 오면 그때 연다.
                if (*pos < end && is_atom(k[*pos]) && veq(k[*pos]->tok.lex, "unbounded")) {
                    ir_fail(c, "E-CHAN-UNBOUNDED",
                            "`channel … unbounded` is not accepted yet — and refusing it is the honest thing. "
                            "The channel buffer is a FIXED size under the hood, so it cannot actually grow "
                            "without bound: it would block or trap, which is not `unbounded` at all. RFC-0009 "
                            "says an unbounded channel must expose an `alloc` effect and (RFC-0043) carry an "
                            "allocator capability — but a buffer that never really allocates would be demanding "
                            "a permission it never uses. Use a bounded `channel` until real, allocator-backed "
                            "growth lands (same discipline that refuses `mailbox unbounded`)", nd->line);
                    return;
                }
                ir_emit(c, IRW_CHNEW, 0);
                return;
            }
            if (!ctx_user_ && veq(nd->tok.lex, "chsend")) {
                if (*pos + 1 >= end) { ir_fail(c, "E-IR-UNSUP", "`chsend` needs `<channel> <value>`", nd->line); return; }
                ir_node(c, k[(*pos)++]);   // 채널 핸들
                ir_node(c, k[(*pos)++]);   // 값
                ir_emit(c, IRW_CHSEND, 0);
                return;
            }
            if (!ctx_user_ && veq(nd->tok.lex, "chrecv")) {
                if (*pos >= end) { ir_fail(c, "E-IR-UNSUP", "`chrecv` needs `<channel>`", nd->line); return; }
                ir_node(c, k[(*pos)++]);   // 채널 핸들
                ir_emit(c, IRW_CHRECV, 0);
                return;
            }
            // ★★★ **await** (RFC-0009 §8.3) — `spawn <op>` 이 준 job 핸들을 받아, 그 태스크가 끝날 때까지
            //   블록했다가 결과를 준다. 문맥 낱말(새 키워드 0개).
            if (!ctx_user_ && veq(nd->tok.lex, "await")) {
                if (*pos >= end) { ir_fail(c, "E-IR-UNSUP", "`await` needs a job handle — `await <h>`", nd->line); return; }
                ir_node(c, k[(*pos)++]);   // job 핸들
                ir_emit(c, IRW_AWAIT, 0);
                return;
            }
            // ★★★ **`unsafe_fn <op>`** (RFC-0066) — export extern op 의 **주소**를 값으로 낸다(콜백).
            //   다음 원자가 op 이름이다. 씨에 함수 포인터로 넘어간다 — 경계 밖은 씨의 규율(unsafe).
            if (!ctx_user_ && veq(nd->tok.lex, "unsafe_fn")) {
                if (*pos >= end || !is_atom(k[*pos])) {
                    ir_fail(c, "E-FN-VALUE", "`unsafe_fn` must be followed by an op name — `unsafe_fn <op>`", nd->line);
                    return;
                }
                bool ff; proven_size_t fi = ir_def_find_in(c, k[*pos]->tok.lex, &ff);
                (*pos)++;
                if (!ff) { ir_fail(c, "E-FN-UNDEF", "`unsafe_fn` names an op that does not exist", nd->line); return; }
                if (!c->out->defs[fi].is_export) {
                    ir_fail(c, "E-FN-NOTEXPORT",
                            "`unsafe_fn <op>` needs an `export extern` op — only those have a C-callable "
                            "symbol whose entry checks the contract (RFC-0066 §2). A plain op has no "
                            "address C can call", nd->line);
                    return;
                }
                // ★★★ **콜백은 능력을 요구할 수 없다** (RFC-0066 — E-FN-CAP). 콜백은 **씨가 부른다**,
                //   그리고 씨는 건네줄 능력이 없다. `cap` 파라미터를 가진 op 을 콜백 값으로 내주면,
                //   그 능력을 채울 자리가 경계 밖에 없다. 능력이 필요한 일은 **콜백 밖**에서 하고,
                //   콜백은 순수/제한된 효과로 둔다.
                if (c->out->defs[fi].param_cap) {
                    ir_fail(c, "E-FN-CAP",
                            "`unsafe_fn <op>` names an op that requires a CAPABILITY (a `cap` "
                            "parameter) — but a callback is entered BY C, and C has no capability to "
                            "hand it. Do the capability-needing work OUTSIDE the callback and keep the "
                            "callback pure or limited-effect (RFC-0066)", nd->line);
                    return;
                }
                ir_emit(c, IRW_FNREF, (proven_i64)fi);
                return;
            }
            // ★★★ **fold** (RFC-0016 — 첫 고차 프렐류드 op). `fold <init> <op> <slice>` — 단일패스 리덕션.
            //   op 은 이름 붙은 op (Acc,T)->Acc. 클로저·람다 **없이** op 이름을 참조하고(unsafe_fn 과 같은 결)
            //   컴파일러가 **호출지에서 단형화**한다: 루프를 인라인하고 op 을 **직접 IRW_CALL** 한다.
            //   ⇒ 백엔드는 fold 를 몰라도 된다 — 이미 있는 opcode(LOOP/INDEX/CALL)로만 낮아지므로 diff-sweep 자동.
            if (veq(nd->tok.lex, "fold")) {
                if (c->nlocals + 3 > IR_MAXLOCALS) { ir_fail(c, "E-IR-UNSUP", "fold: too many locals", nd->line); return; }
                proven_size_t acc = c->nlocals++, it = c->nlocals++, idx = c->nlocals++;
                c->locals[acc].name = (proven_u8str_view_t){ 0 };
                c->locals[it].name  = (proven_u8str_view_t){ 0 };
                c->locals[idx].name = (proven_u8str_view_t){ 0 };
                ir_value(c, k, pos, end);                        // <init>
                ir_emit(c, IRW_STORE, (proven_i64)acc);
                if (*pos >= end || !is_atom(k[*pos])) {
                    ir_fail(c, "E-FOLD-OP", "fold needs an op NAME as its second argument — `fold <init> <op> <slice>`", nd->line); return;
                }
                bool of; proven_size_t oi = ir_def_find_in(c, k[*pos]->tok.lex, &of);
                (*pos)++;
                if (!of) { ir_fail(c, "E-FOLD-OP", "fold's op names an op that does not exist", nd->line); return; }
                // ★ **건전성 바닥** (타입검사가 생략돼도 지켜야 한다): op 은 정확히 2 인자(Acc,T)·비가변·능력 불요.
                //   arity≠2 면 IRW_CALL 이 스택을 망친다. (효과 순수성은 low_check 의 ck_fold 가 본다.)
                if (c->out->defs[oi].nparams != 2 || c->out->defs[oi].is_variadic || c->out->defs[oi].param_cap) {
                    ir_fail(c, "E-FOLD-OP", "fold's op must take exactly two arguments (Acc, T) and no capability — the result folds back as the next accumulator", nd->line); return;
                }
                ir_value(c, k, pos, end);                        // <slice>
                ir_emit(c, IRW_STORE, (proven_i64)it);
                ir_emit(c, IRW_CONST, 0);
                ir_emit(c, IRW_STORE, (proven_i64)idx);
                proven_size_t cond = c->code.len;
                ir_emit(c, IRW_LOAD, (proven_i64)idx);
                ir_emit(c, IRW_LOAD, (proven_i64)it);
                ir_emit(c, IRW_LEN, 0);
                ir_emit(c, IRW_LT, 0);
                proven_size_t brz = ir_emit(c, IRW_BRZ, 0);
                ir_emit(c, IRW_LOAD, (proven_i64)acc);           // op 인자 0 = 누산기
                ir_emit(c, IRW_LOAD, (proven_i64)it);
                ir_emit(c, IRW_LOAD, (proven_i64)idx);
                ir_emit(c, IRW_INDEX, 0);                        // op 인자 1 = 원소
                ir_emit(c, IRW_CALL, (proven_i64)oi);            // acc = op(acc, elem)
                ir_emit(c, IRW_STORE, (proven_i64)acc);
                ir_emit(c, IRW_LOAD, (proven_i64)idx);
                ir_emit(c, IRW_CONST, 1);
                ir_emit(c, IRW_ADD, 0);
                ir_emit(c, IRW_STORE, (proven_i64)idx);
                ir_emit(c, IRW_BR, (proven_i64)cond);
                proven_size_t fend = c->code.len;
                ir_at(c, brz)->a = (proven_i64)fend;
                ir_emit(c, IRW_LOAD, (proven_i64)acc);           // fold 결과 = 표현식 값
                return;
            }
            // ★★★ **map / filter** (RFC-0016 — 원소별 스테이지). `map <out> <op> <in>` · `filter <out> <pred> <in>`.
            //   결과는 **호출자가 준 mut slice `out` 에 쓴다**(할당 없음 — 유계 싱크, RFC-0016 collect-into 모델).
            //   op/pred 은 이름 op **1 인자**(T->U · T->bool). 경계 안전: `idx < len(in) AND idx < len(out)` 까지만
            //   쓰고, **쓴 개수를 반환**한다(out 이 짧으면 — X-0062 부터 — 쓰는 순간 멈춘다). fold 과 같은 기계 + ISTORE.
            {
                bool ismap = veq(nd->tok.lex, "map"), isfilter = veq(nd->tok.lex, "filter");
                if (ismap || isfilter) {
                    if (c->nlocals + 4 > IR_MAXLOCALS) { ir_fail(c, "E-IR-UNSUP", "map/filter: too many locals", nd->line); return; }
                    proven_size_t ou = c->nlocals++, in = c->nlocals++, i = c->nlocals++, j = c->nlocals++;
                    c->locals[ou].name = (proven_u8str_view_t){ 0 };
                    c->locals[in].name = (proven_u8str_view_t){ 0 };
                    c->locals[i].name  = (proven_u8str_view_t){ 0 };
                    c->locals[j].name  = (proven_u8str_view_t){ 0 };
                    ir_value(c, k, pos, end);                     // <out> (mut slice)
                    ir_emit(c, IRW_STORE, (proven_i64)ou);
                    if (*pos >= end || !is_atom(k[*pos])) { ir_fail(c, "E-FOLD-OP", "map/filter needs an op NAME as its second argument", nd->line); return; }
                    bool of; proven_size_t oi = ir_def_find_in(c, k[*pos]->tok.lex, &of);
                    (*pos)++;
                    if (!of) { ir_fail(c, "E-FOLD-OP", "map/filter's op names an op that does not exist", nd->line); return; }
                    if (c->out->defs[oi].nparams != 1 || c->out->defs[oi].is_variadic || c->out->defs[oi].param_cap) {
                        ir_fail(c, "E-FOLD-OP", "map/filter's op must take exactly ONE argument (the element) and no capability", nd->line); return;
                    }
                    ir_value(c, k, pos, end);                     // <in> (slice)
                    ir_emit(c, IRW_STORE, (proven_i64)in);
                    ir_emit(c, IRW_CONST, 0); ir_emit(c, IRW_STORE, (proven_i64)i);   // read index
                    ir_emit(c, IRW_CONST, 0); ir_emit(c, IRW_STORE, (proven_i64)j);   // write index (= count)
                    proven_size_t cond = c->code.len;
                    // ★★ X-0062 (소유자 결정 ⓒ, 2026-09-25) — 받는 자리가 차도 **말없이 멈추지 않는다.** 전엔 조건에
                    //   `j < len(out)` 이 있어 남은 원소를 조용히 버렸다(원소 3 을 2 칸에 → 둘만 쓰고 끝). 이제 원천을 끝까지 돌고,
                    //   찬 자리에 쓰려는 순간 경계 검사가 멈춘다(VM E-VM-BOUNDS · 네이티브 panic). 번역 시점에 알면 거절한다.
                    ir_emit(c, IRW_LOAD, (proven_i64)i); ir_emit(c, IRW_LOAD, (proven_i64)in); ir_emit(c, IRW_LEN, 0); ir_emit(c, IRW_LT, 0);
                    proven_size_t brz = ir_emit(c, IRW_BRZ, 0);
                    proven_size_t skip = 0; bool have_skip = false;
                    if (isfilter) {
                        // if pred(in[i]) == 0 → skip the write (but still advance i)
                        ir_emit(c, IRW_LOAD, (proven_i64)in); ir_emit(c, IRW_LOAD, (proven_i64)i); ir_emit(c, IRW_INDEX, 0);
                        ir_emit(c, IRW_CALL, (proven_i64)oi);     // pred(in[i]) → bool
                        skip = ir_emit(c, IRW_BRZ, 0); have_skip = true;
                        // out[j] = in[i]
                        ir_emit_sinkfull_guard(c, ou, j);
                        ir_emit(c, IRW_LOAD, (proven_i64)ou); ir_emit(c, IRW_LOAD, (proven_i64)j);
                        ir_emit(c, IRW_LOAD, (proven_i64)in); ir_emit(c, IRW_LOAD, (proven_i64)i); ir_emit(c, IRW_INDEX, 0);
                        ir_emit(c, IRW_ISTORE, 0);
                        ir_emit(c, IRW_LOAD, (proven_i64)j); ir_emit(c, IRW_CONST, 1); ir_emit(c, IRW_ADD, 0); ir_emit(c, IRW_STORE, (proven_i64)j);
                    } else {
                        // out[i] = op(in[i]); write index tracks read index for map (1:1)
                        ir_emit_sinkfull_guard(c, ou, i);
                        ir_emit(c, IRW_LOAD, (proven_i64)ou); ir_emit(c, IRW_LOAD, (proven_i64)i);
                        ir_emit(c, IRW_LOAD, (proven_i64)in); ir_emit(c, IRW_LOAD, (proven_i64)i); ir_emit(c, IRW_INDEX, 0);
                        ir_emit(c, IRW_CALL, (proven_i64)oi);     // op(in[i]) → U
                        ir_emit(c, IRW_ISTORE, 0);
                        ir_emit(c, IRW_LOAD, (proven_i64)j); ir_emit(c, IRW_CONST, 1); ir_emit(c, IRW_ADD, 0); ir_emit(c, IRW_STORE, (proven_i64)j);
                    }
                    if (have_skip) ir_at(c, skip)->a = (proven_i64)c->code.len;   // skip lands on the i++
                    ir_emit(c, IRW_LOAD, (proven_i64)i); ir_emit(c, IRW_CONST, 1); ir_emit(c, IRW_ADD, 0); ir_emit(c, IRW_STORE, (proven_i64)i);
                    ir_emit(c, IRW_BR, (proven_i64)cond);
                    proven_size_t mend = c->code.len;
                    ir_at(c, brz)->a = (proven_i64)mend;
                    ir_emit(c, IRW_LOAD, (proven_i64)j);          // 결과 = 쓴/거른 개수
                    return;
                }
            }
            bool isdef; proven_size_t di = ir_def_find_q(c, nd, &isdef);
            if (!isdef) {   /* ★ 모듈 한정 op 참조 X.op → 뒤 마디로 해소 (RFC-0011) */
                di = ir_modqual_find(c, nd->tok.lex, &isdef);
            }
            if (isdef) {
                proven_size_t np = c->out->defs[di].nparams;
                if (*pos + np > end) {
                    // ★ 2026-09-14 — 이름과 수를 댄다. 옛 문장의 «줄 끝에 `,`» 권고는 RFC-0103 에서 죽은 규칙이었다.
                    char abuf[256];
                    snprintf(abuf, sizeof abuf, "`%.*s` takes %zu argument%s but %zu %s given here — an op call "
                             "is `<name> <arg>…` with exactly as many arguments as its `input` clauses",
                             (int)nd->tok.lex.size, (const char *)nd->tok.lex.ptr, (size_t)np, np == 1 ? "" : "s",
                             (size_t)(end - *pos), (end - *pos) == 1 ? "is" : "are");
                    ir_fail_buf(c, "E-IR-ARITY", abuf, nd->line);
                    return;
                }
                // ★★★ **가변인자 씨 호출** (RFC-0063 §5) — 고정 인자 + **나머지 전부**를 밀고,
                //   IRW_EXTERN 을 **호출 지점에서** 직접 낸다(고정 arity 를 쓰는 IRW_CALL 대신).
                //   총 인자 수를 상위 비트에 실어, 백엔드가 스택에서 그만큼 읽어 씨 함수를 부른다.
                if (c->out->defs[di].is_variadic) {
                    proven_size_t argc = 0;
                    for (proven_size_t i = 0; i < np; i++)     { ir_value(c, k, pos, end); argc++; }
                    while (*pos < end && !c->failed)           { ir_value(c, k, pos, end); argc++; }
                    if (argc > 0x7fff) { ir_fail(c, "E-IR-ARITY",
                        "a variadic C call has more arguments than the tool can carry", nd->line); return; }
                    ir_emit(c, IRW_EXTERN, (proven_i64)(di | (argc << 16)));
                    return;
                }
                for (proven_size_t i = 0; i < np; i++) {
                    // ★★★ **참조를 인자로 넘기는 것은 아직 코어 밖이다 — 그리고 그것을 *말한다*.**
                    //   (2026-07-19, lib/out.low 을 지으며 드러났다.)
                    //   여태 이 모양은 `--check` 를 **초록으로 통과하고 런타임에 죽었다**:
                    //     `bump (mut_ref q)` → "E-VM-TYPE: field access needs a record/view"
                    //   피호출자가 받는 것은 레코드가 아니라 **참조 값**인데, 양 백엔드의 `field`
                    //   (그리고 field 쓰기)가 참조를 따라갈 줄 모른다.
                    //   ★★ **초록 체크가 거짓말을 하고 있었다** — 이 도구의 규율은 그 반대다:
                    //     *"`--check` 는 컴파일러가 받지 않을 단위에서 초록일 수 없다."*
                    //     E-ESCAPE 은 이것을 못 잡는다(그건 **반환** 안의 ref 만 본다).
                    //   ★ 아무 픽스처도 참조를 인자로 넘긴 적이 없어서 여태 안 보였다(교훈 6).
                    //   ☞ 제대로 하려면 **빌림 추적을 빠른 경로까지 보존**해야 한다(HANDOFF §5-2):
                    //     생 포인터로 낮추면 양성은 돌지만 E-VM-EXCL/READONLY 트랩이 **사라진다** —
                    //     배타성이 런타임 추적이기 때문이다. 그래서 지금은 **거절하고 이름을 부른다.**
                    if (*pos < end) {
                        // ★★★ **참조를 인자로 넘기는 것을 *기록* 한다** (2026-07-19).
                        //   전엔 여기서 거절했다 — 피호출자의 `field` 가 참조를 못 따라가서
                        //   `--check` 초록 + 런타임 사망이었기 때문이다. 이제 따라간다(vm_through).
                        //   ★ 다만 그것은 **런타임 검사**(프레임 세대 · 빌림 스택)에 기대므로,
                        //     그 추적이 없는 **빠른 경로에는 내릴 수 없다** ⇒ 피호출자를 표시해
                        //     태그 경로에 남긴다. 속도를 포기하는 자리를 **이름으로** 표시하는 것이,
                        //     생 포인터로 낮춰 **음성 트랩을 조용히 지우는 것**보다 낫다.
                        //   ☞ 인자는 `(mut_ref q)` 처럼 GROUP 으로 싸여 올 수 있고,
                        //     FORM 은 머리 토큰이 `.tok` 자체다(kids[0] 이 아니다).
                        const low_cst_t *a2 = k[*pos];
                        while (a2 && a2->kind == LOW_CST_GROUP && a2->nkids) a2 = a2->kids[0];
                        proven_u8str_view_t hl = { 0 };
                        if (a2 && (a2->kind == LOW_CST_ATOM || a2->kind == LOW_CST_FORM)) hl = a2->tok.lex;
                        if (hl.size && (veq(hl, "ref") || veq(hl, "mut_ref") || veq(hl, "addr")))
                            c->out->defs[di].takes_ref_arg = true;
                        // ★ RFC-0132 T2b-2 — 쓸 수 있는 매개변수(`mut`·`owned`·`mut_ref`)에 넘기는 나열 리터럴은
                        //   읽기 전용 상수(ⓑ)가 아니라 **틀 안 임시(ⓒ)** 다(§13.2) — 받는 쪽이 쓸 수 있어야 한다.
                        if (a2 && a2->kind == LOW_CST_FORM && a2->nkids >= 2 && is_atom(a2->kids[0]) &&
                            a2->kids[0]->tok.kw == LOW_KW_LIT && i < 32 && ((c->out->defs[di].param_mutw >> i) & 1u))
                            c->lit_frame = true;
                    }
                    ir_value(c, k, pos, end);
                    c->lit_frame = false;
                }
                ir_emit(c, IRW_CALL, (proven_i64)di);
                return;
            }
            // ★ enum 값 — 변형의 **인덱스**로 낮아진다(런타임 태그 없음, 비용 가시).
            if (ir_is_enum_variant(c, nd->tok.lex)) {
                ir_emit(c, IRW_CONST, ir_variant_index(c, nd->tok.lex));
                return;
            }
            // ★ actor 의 상태 필드 — **expr 섬 안에서도** 풀려야 한다(`set value expr value + 1 .`).
            if (ir_is_sfield(c, nd->tok.lex)) {
                ir_emit(c, IRW_LOAD, 0);
                ir_emit(c, IRW_FIELD, (proven_i64)ir_field_intern(c, nd->tok.lex));
                return;
            }
            // ★★ PRINCIPLES.md §0 교훈 5 — **이 이름이 명세에 있는가?**
            //   있다면 사용자의 오류가 아니라 **내 결함이다.** 그러면 그렇게 말해야 한다.
            //   (SPEC-002 §256: MVP = match/defer/await/comptime/actor/list **제외**.
            //    명세가 스스로 "나중" 이라 적어 둔 낱말들이다 — 그것을 "없는 이름" 이라 하면
            //    사용자는 오타를 찾으러 간다. 없는 오타를.)
            if (ir_is_postmvp_word(nd->tok.lex)) {
                ir_fail(c, "E-IR-UNSUP",
                        veq(nd->tok.lex, "select")
                            ? "`select` (choosing among channels) IS in the spec — the tool does not implement it yet. "
                              "That is the TOOL's gap, not an error in your program (§4.7). The lanewise blend "
                              "`select <mask> <a> <b>` is now spelled `lane_select` (RFC-0127)"
                            : "this word IS in the spec — the tool does not implement it yet. "
                        "That is the TOOL's gap, not an error in your program "
                        "(the standard names this case: §4.7)", nd->line);
                return;
            }
            // ★ **모듈 상수** — 값을 그 자리에 **박는다**(접기). 지역보다 뒤에 본다:
            //   지역이 같은 이름을 가리면 지역이 이긴다(섀도잉은 별도 검사가 잡는다).
            for (proven_size_t mi = 0; mi < c->nmconst; mi++)
                if (proven_u8str_view_eq(c->mconst[mi], nd->tok.lex)) {
                    ir_emit(c, IRW_CONST, c->mconstv[mi]);
                    return;
                }
            // ★★★ RFC-0112 D5(3) (WO-0212) — **영역 블록의 이름을 인자로 건넨다.** 영역 매개변수는 실행 중
            //   슬롯이 하나 있지만 값은 쓰이지 않는다(뿌리는 번역 시점에 정해진다) — 그래서 0 을 싣는다.
            //   전엔 여기서 `E-IR-UNDEF` 였다: 영역을 아래로 건네는 길이 문법만 있고 동작이 없었다(F7).
            if (ir_is_region_name(c, nd->tok.lex)) { ir_emit(c, IRW_CONST, 0); return; }
            ir_fail_undef(c, nd);
            return;
        }
    }
    ir_node(c, nd);
}

// ── expr island — precedence climbing ────────────────────────────────────────
// ★★★ **표의 정본은 SPEC-002 §2.6.1 이다**(RFC-0091). 여기 숫자와 그 표는 게이트
//   `check-expr` 가 대조한다 — 한쪽만 고치면 빨간불이 난다.
//     5  * /        좌
//     4  + -        좌
//     3  비교        **비결합**  (E-EXPR-CHAIN)
//     2  and        좌
//     1  or         좌
// ★★ 예전엔 `and` 와 `or` 가 **같은 1** 이었다. 그러면 `a or b and c` 가 왼쪽부터 묶여
//   `(a or b) and c` 가 된다 — 다른 모든 언어와 **반대**이고, 읽는 사람이 확인할 수 없는
//   방식으로 틀린다. 명세(RFC-0001 §6.6)는 처음부터 6·7 로 **갈라 적어** 두었는데
//   구현이 안 따랐다: **명세가 앞서 있었고 아무도 대조하지 않았다.**
#define IR_PREC_CMP 3
static int ir_prec(const low_cst_t *nd) {
    if (!is_atom(nd)) return -1;
    if (nd->tok.kind == LOW_TOK_OP) { char ch = (char)nd->tok.lex.ptr[0]; return (ch == '*' || ch == '/') ? 5 : 4; }
    proven_u8str_view_t v = nd->tok.lex;
    if (veq(v, "eq") || veq(v, "ne") || veq(v, "lt") || veq(v, "le") || veq(v, "gt") || veq(v, "ge"))
        return IR_PREC_CMP;
    if (veq(v, "and")) return 2;
    if (veq(v, "or"))  return 1;
    return -1;
}
static void ir_island_binop(ir_ctx_t *c, const low_cst_t *opk) {
    if (opk->tok.kind == LOW_TOK_OP) {
        switch ((char)opk->tok.lex.ptr[0]) {
            case '+': ir_emit(c, IRW_ADD, 0); return;
            case '-': ir_emit(c, IRW_SUB, 0); return;
            case '*': ir_emit(c, IRW_MUL, 0); return;
            case '/': ir_emit(c, IRW_DIV, 0); return;
            default: break;
        }
    }
    const ir_builtin_t *b = ir_builtin(opk->tok.lex);
    if (b && b->arity == 2) { ir_emit(c, b->w, 0); return; }
    ir_fail(c, "E-IR-UNSUP", "unsupported expr-island operator", opk->line);
}
// ★★★ **섬 안에서 op 호출은 괄호가 필수다** (SPEC-002 §2.6.1 · RFC-0091 결정 3).
//   `expr len d ge 4` 는 사람이 `(len d) ge 4` 로 읽지만, 섬의 문법에는 적용(병치)이
//   없으므로 도구는 `len` 을 **값 이름**으로 찾다가 못 찾는다. 그러면 나오는 말이
//   `E-IR-UNDEF: undefined name in expression` — **이름이 없다**고 한다. 그런데 이름은
//   있다. 없는 것은 **괄호**다. 도구가 원인이 아니라 증상을 말하면 쓰는 사람은
//   엉뚱한 데(오타·import)를 뒤진다. ⇒ 그 낱말이 **아는 op** 이면 그렇게 말한다.
static bool ir_island_bad_app(ir_ctx_t *c, const low_cst_t *nd) {
    if (!is_atom(nd) || nd->tok.kind != LOW_TOK_IDENT) return false;
    if (ir_prec(nd) >= 0) return false;                      // 중위 연산자 자리는 여기 아님
    bool known = ir_builtin(nd->tok.lex) != NULL;
    // 사용자 op 도 마찬가지다 — `fn`/`proc` 선언에서 그 이름을 찾는다.
    if (!known && c->pr)
        for (proven_size_t q = 0; q < c->pr->nforms && !known; q++) {
            const low_cst_t *ct = c->pr->forms[q];
            if (ct->kind != LOW_CST_FORM || ct->nkids < 2 || !is_atom(ct->kids[0]) || !is_atom(ct->kids[1]))
                continue;
            proven_u8str_view_t h = ct->kids[0]->tok.lex;
            proven_size_t ni = 1;
            if (veq(h, "export") && ct->nkids >= 3 && is_atom(ct->kids[1]) && is_atom(ct->kids[2])) {
                h = ct->kids[1]->tok.lex; ni = 2;
            }
            if (!veq(h, "fn") && !veq(h, "proc")) continue;
            if (proven_u8str_view_eq(ct->kids[ni]->tok.lex, nd->tok.lex)) known = true;
        }
    if (!known) return false;
    ir_fail(c, "E-EXPR-APP",
            "an op call inside an `expr` island must be parenthesised — write `(len d) ge 4`, "
            "not `len d ge 4`. The island has infix operators only: without the parentheses "
            "nothing says where the argument list ends, and you and the compiler would read it "
            "differently", nd->line);
    return true;
}
 void ir_island_climb(ir_ctx_t *c, low_cst_t *const *k, proven_size_t n, proven_size_t *pos, int minp) {
    if (*pos < n && ir_island_bad_app(c, k[*pos])) return;
    if (*pos < n) ir_node(c, k[(*pos)++]); else ir_emit(c, IRW_CONST, 0);
    bool had_cmp = false;   // ★ 이 자리에서 비교를 이미 하나 먹었는가(비결합 판정)
    while (*pos < n && !c->failed) {
        int prec = ir_prec(k[*pos]);
        if (prec < minp) break;
        // ★★★ **비교는 비결합이다** (SPEC-002 §2.6.1). `a lt b lt c` 는 수학의 `a<b<c` 로
        //   읽히지만 어떤 결합으로 묶어도 그 뜻이 아니다 — 왼쪽부터 묶으면 `(a lt b) lt c`,
        //   즉 **bool 을 c 와 견준다**. 조용히 다른 뜻이 되느니 **거절한다**.
        //   명세는 처음부터 "체이닝 금지" 라고 적었는데 도구는 통과시키고 있었다.
        if (prec == IR_PREC_CMP) {
            if (had_cmp) {
                ir_fail(c, "E-EXPR-CHAIN",
                        "comparisons do not chain in an `expr` island — `a lt b lt c` reads like "
                        "mathematics but means `(a lt b) lt c`, which compares a bool with a number. "
                        "Say what you mean: `expr (a lt b) and (b lt c)`", k[*pos]->line);
                return;
            }
            had_cmp = true;
        }
        const low_cst_t *opk = k[(*pos)++];
        // ★★ 섬의 `and`/`or` 도 **단락 평가**다(정본 §6.3.2 표) — 전위와 같은 모양(BRZ·BR·CONST·NOT)
        //   으로 짓는다. 전엔 양쪽을 다 계산하는 `and` 옵코드를 내서 `expr (ne n 0) and (eq (mod x n) 0)`
        //   이 n = 0 에서 E-VM-DIV0 으로 멈췄다(전위 `and` 는 멈추지 않는다, 2026-09-28).
        if (is_atom(opk) && opk->tok.kind == LOW_TOK_IDENT && (veq(opk->tok.lex, "and") || veq(opk->tok.lex, "or"))) {
            bool is_or = veq(opk->tok.lex, "or");
            if (is_or) ir_emit(c, IRW_NOT, 0);                 // or: 왼쪽이 참이면 건너뛴다
            proven_size_t brz = ir_emit(c, IRW_BRZ, 0);
            ir_island_climb(c, k, n, pos, prec + 1);          // ★ 오른쪽은 **여기서만** 계산된다
            ir_emit(c, IRW_NOT, 0); ir_emit(c, IRW_NOT, 0);   // 0/1 정규화
            proven_size_t brend = ir_emit(c, IRW_BR, 0);
            ir_at(c, brz)->a = (proven_i64)c->code.len;
            ir_emit(c, IRW_CONST, is_or ? 1 : 0);              // 단락된 답
            ir_at(c, brend)->a = (proven_i64)c->code.len;
            continue;
        }
        ir_island_climb(c, k, n, pos, prec + 1);
        ir_island_binop(c, opk);
    }
}

 void ir_node(ir_ctx_t *c, const low_cst_t *nd) {
    if (c->failed) return;
    switch (nd->kind) {
        case LOW_CST_ATOM:
            if (nd->tok.lex.size >= 8 && memcmp(nd->tok.lex.ptr, "machine.", 8) == 0) {
                bool found;
                proven_i64 mv = ir_machine(nd->tok.lex, &found);
                if (!found) { ir_fail(c, "E-CFG-UNKNOWN-PROP", "no such machine property", nd->line); return; }
                ir_emit(c, IRW_CONST, mv);   // comptime 상수 — 런타임 비용 0
                return;
            }
            if (nd->tok.kind == LOW_TOK_NUMBER) {
                proven_i64 v;
                if (ir_int_lit(nd->tok.lex, &v)) { ir_emit(c, IRW_CONST, v); return; }
                double dv = low_num_to_double(nd->tok.lex);  // ★ 구분자 `_` 를 거른다(한 벌)
                proven_i64 bits;
                memcpy(&bits, &dv, 8);
                ir_emit(c, IRW_FCONST, bits);
                return;
            }
            // ★ **텍스트 리터럴도 값이다** — 여태 렉서만 알고 IR 은 몰라 값 자리에서 거절됐다
            //   (E-IR-UNSUP). 렉서는 이미 옳게 한다: 한 줄이면 개행 없는 문자열, 개행을
            //   남기려면 빈 줄 하나, CRLF 처리, 본문은 **원문 그대로**. 남은 것은 이 한 줄이었다.
            //   ⇒ 텍스트 리터럴이 개행·따옴표·제어 바이트의 **정공법**이다(이스케이프 불요).
            // ★★★★★ **문자 리터럴 → 수 상수** (2026-08-07 · 소유자 결정).
            //   접두사가 **폭**을, 내용이 **값**을 정한다 — 문자열 접두사 가족과 같은 어휘다:
            //     (없음)·u8  → u8  바이트        `u8'd'` = 100
            //     u          → u16 코드 유닛     `u'한'`  = 54620
            //     U          → u32 코드포인트    `U'😀'` = 128512
            //   ★ 한 칸에 **안 들어가면 거절**한다. `u8'한'` 은 UTF-8 3 바이트이고,
            //     `u'😀'` 는 서러게이트 **쌍**이다 — 조용히 자르는 것이 이 언어가 가장 싫어하는
            //     오답이다. 잘라 놓고 "문자 하나" 라고 부르면 그 리터럴이 거짓말이 된다.
            //   ☞ 왜 짓나: `lib/regex.low` 이 자기 이스케이프 글자를 **십진수로** 적고 있었고
            //     (`if eq c 100` = 'd'), `lib/fmt.low` 은 **주석으로 글자를 날랐다**
            //     (`put_byte buf pos 45 . rem '-'`). **주석은 검사되지 않는다.**
            if (nd->tok.kind == LOW_TOK_CHAR) {
                proven_i32 cw = ir_str_prefix_width(nd->tok.aux);
                if (cw < 0) {
                    ir_fail(c, "E-STR-PREFIX",
                            "unknown literal prefix. The set is CLOSED: `u` (a UTF-16 code unit) · "
                            "`U` (a code point). Characters and strings share it. There is no `u8` "
                            "prefix: this language's default IS u8, so `u8'x'` said nothing that "
                            "`'x'` did not — a pure synonym, and synonyms are not kept (SPEC-002 2.5)", nd->line);
                    return;
                }
                proven_u8str_view_t cv = nd->tok.lex;
                if (!ir_unescape(c, cv, nd->line, &cv)) return;
                if (cv.size == 0) {
                    ir_fail(c, "E-CHAR-EMPTY",
                            "a character literal is EMPTY. There is no such value — a character "
                            "literal names ONE unit, and none is not one. For an empty string write "
                            "\"\"", nd->line);
                    return;
                }
                if (cw > 1 && !ir_widen_units(c, cv, cw, nd->line, &cv)) return;
                if (cv.size != (proven_size_t)cw) {
                    ir_fail(c, "E-CHAR-WIDTH",
                            "this character does not fit ONE unit of the prefix you chose. `u8'…'` "
                            "holds one BYTE (a Hangul syllable is three in UTF-8), `u'…'` one UTF-16 "
                            "code unit (a non-BMP character is a surrogate PAIR), `U'…'` one code "
                            "point. Widen the prefix, or use a string — the tool will not silently "
                            "keep the first unit and call it the character", nd->line);
                    return;
                }
                proven_u64 val = 0;
                for (proven_size_t z = cw; z-- > 0; ) val = (val << 8) | cv.ptr[z];
                ir_emit(c, IRW_CONST, (proven_i64)val);
                return;
            }
            if (nd->tok.kind == LOW_TOK_STRING || nd->tok.kind == LOW_TOK_TEXTLIT) {
                proven_u8str_view_t val = nd->tok.lex;
                // ★★★ **접두사가 원소폭을 정한다** (RFC-0035 D5). 두 표면이 **같은 어휘**를
                //   쓴다: 인라인 `u"AB"` 와 여러 줄 `text TERM u … TERM`.
                //   ☞ 전엔 텍스트 리터럴의 태그가 **무엇이든 받고 조용히 버려졌다** — `text
                //     zzz_not_an_encoding END` 가 `check: ok` 였다. 태그 자리를 렉싱해 놓고
                //     읽는 이가 없으면 그 자리는 **약속처럼 보이는 빈칸**이다.
                proven_i32 ew = ir_str_prefix_width(nd->tok.aux);
                if (ew < 0) {
                    ir_fail(c, "E-STR-PREFIX",
                            "unknown literal prefix. The set is CLOSED: `u` (UTF-16 code units, "
                            "slice u16) · `U` (code points, slice u32). Both surfaces share it — "
                            "`u\"AB\"` and `text TERM u`. There is no `u8` prefix: the default IS "
                            "u8, so `u8\"…\"` said nothing that `\"…\"` did not (SPEC-002 2.5 forbids "
                            "synonyms). "
                            "A prefix outside the set names nothing, and accepting it would mean "
                            "the tool kept a promise it never made", nd->line);
                    return;
                }
                // 텍스트 리터럴 본문은 **원문 그대로**다 — 풀지 않는다. 따옴표 리터럴만 푼다.
                if (nd->tok.kind == LOW_TOK_STRING &&
                    !ir_unescape(c, nd->tok.lex, nd->line, &val)) return;
                if (ew > 1 && !ir_widen_units(c, val, ew, nd->line, &val)) return;
                proven_size_t si = c->out->nstrs;
                for (proven_size_t i = 0; i < c->out->nstrs; i++)
                    if (c->out->strew[i] == (proven_u8)ew &&
                        proven_u8str_view_eq(c->out->strs[i], val)) { si = i; break; }
                if (si == c->out->nstrs) {
                    if (c->out->nstrs >= IR_MAXSTRS) { ir_fail(c, "E-IR-UNSUP", "too many string literals", nd->line); return; }
                    c->out->strew[c->out->nstrs] = (proven_u8)ew;
                    c->out->strs[c->out->nstrs++] = val;
                }
                ir_emit(c, IRW_STR, (proven_i64)si);
                return;
            }
            if (nd->tok.kw == LOW_KW_TRUE)  { ir_emit(c, IRW_CONST, 1); return; }
            if (nd->tok.kw == LOW_KW_FALSE) { ir_emit(c, IRW_CONST, 0); return; }
            if (nd->tok.kind == LOW_TOK_IDENT && nd->tok.kw == LOW_KW_NONE) {
                bool f; proven_size_t slot = ir_local_find(c, nd->tok.lex, &f);
                if (f && nd->qual_mod.size) f = false;   // ★ 한정 이름은 지역이 아니다(위 ★★★★★)
                if (f) { ir_emit(c, IRW_LOAD, (proven_i64)slot); return; }
                // ★ actor 의 상태 필드 — 지역이 아니라 **인스턴스의 필드**다(슬롯 0).
                if (ir_is_sfield(c, nd->tok.lex)) {
                    ir_emit(c, IRW_LOAD, 0);
                    ir_emit(c, IRW_FIELD, (proven_i64)ir_field_intern(c, nd->tok.lex));
                    return;
                }
                if (ir_glued_local(c, nd->tok.lex, nd->line)) return;   // s.f.g sugar
                bool isdef; proven_size_t di = ir_def_find_q(c, nd, &isdef);
            if (!isdef) {   /* ★ 모듈 한정 op 참조 X.op → 뒤 마디로 해소 (RFC-0011) */
                di = ir_modqual_find(c, nd->tok.lex, &isdef);
            }
                if (isdef && c->out->defs[di].nparams == 0) { ir_emit(c, IRW_CALL, (proven_i64)di); return; }
                if (ir_is_postmvp_word(nd->tok.lex)) {
                    ir_fail(c, "E-IR-UNSUP",
                            veq(nd->tok.lex, "select")
                                ? "`select` (choosing among channels) IS in the spec — the tool does not implement it yet. "
                                  "That is the TOOL's gap, not an error in your program (§4.7). The lanewise blend "
                                  "`select <mask> <a> <b>` is now spelled `lane_select` (RFC-0127)"
                                : "this word IS in the spec — the tool does not implement it yet. "
                            "That is the TOOL's gap, not an error in your program "
                            "(the standard names this case: §4.7)", nd->line);
                    return;
                }
                ir_fail_undef(c, nd);
                return;
            }
            // ★ 개행 닫힘이 없어진 뒤(RFC-0103) `expr` 섬이 줄로 나뉘어 고아가
            //   생기는 일은 없다 — 섬은 자기 닫개 `.` 에서 끝난다. 그래서 그 경우를
            //   가리던 판별자(`OP` 머리)와 수리 id `R-ONE-LINE-ISLAND` 를 회수했다.
            //   *규칙이 죽으면 그 규칙이 만든 병의 처방도 죽는다.*
            // ★★★★★ **섬에는 단항이 없다** — 그것을 **이름 붙여** 말한다(2026-08-29, spill 22).
            //   전엔 `expr - 5` 가 `E-IR-UNSUP: unsupported atom` 으로 죽었다. 그 진단은
            //   *"이 도구가 아직 못 낮춘다"* 는 뜻이라, 읽는 사람은 **곧 될 것**으로 읽는다.
            //   실제로는 문서가 *"단항은 섬에 없다"* 고 **정해 둔** 것이다(SPEC-002 §2.5 · RFC-0091).
            //   ☞ *구현의 빈틈과 언어의 결정을 같은 진단으로 말하면, 둘 다 못 읽는다.*
            if (c->island && nd->kind == LOW_CST_ATOM &&
                (veq(nd->tok.lex, "-") || veq(nd->tok.lex, "+") ||
                 veq(nd->tok.lex, "!") || veq(nd->tok.lex, "~"))) {
                ir_fail(c, "E-EXPR-UNARY",
                        "an `expr` island has INFIX operators only — there is no unary form. "
                        "`expr - 5` has nothing on the left of `-`. Write the prefix op instead "
                        "(`neg 5`, `not b`, `bit_not x`), or give the left operand. The island "
                        "exists to read like arithmetic; a unary sign would make `a - 5` and "
                        "`a (- 5)` two readings of the same letters (SPEC-002 §2.5 · RFC-0091)",
                        nd->line);
                return;
            }
            // ★★★ WO-0220 (소유자 결정 A1-a) — `if` 는 **문**이다(정본 §6.5.2 (4)). 값 자리의 `if` 는 «처리기가 아직 못 한다»
            //   가 아니라 **언어가 정한 것**이다 — 부록 A 에서 if-식 행을 뺐다. 전엔 이 자리가 `E-IR-UNSUP` 이라 `--check` 가 초록이었다.
            if (nd->kind == LOW_CST_ATOM && nd->tok.kw == LOW_KW_IF) {
                ir_fail(c, "E-IF-VALUE",
                        "`if` is a STATEMENT — it gives no value (§6.5.2 (4)). Choose the value in each branch "
                        "instead: `var x be u64 6 .  if c do set x 5 . end`, or `return` from each branch",
                        nd->line);
                return;
            }
            ir_fail(c, "E-IR-UNSUP", "unsupported atom in the S5 core", nd->line);
            return;
        case LOW_CST_GROUP:
            // a paren group nested inside an island re-enters island context
            // (§2.6: 섬 내부 괄호 = 무점 구조 표지) → allow bare infix here
            if (nd->nkids == 1 && nd->kids[0]->kind == LOW_CST_FORM)
                ir_run_ex(c, nd->kids[0]->kids, 0, nd->kids[0]->nkids, true);
            else if (nd->nkids == 1)
                ir_node(c, nd->kids[0]);
            else
                ir_emit(c, IRW_CONST, 0);
            return;
        case LOW_CST_FORM: {
            const low_cst_t *prev = c->cur_form;   // ★ 중첩을 위해 저장·복원한다
            c->cur_form = nd;
            ir_run(c, nd->kids, 0, nd->nkids);
            c->cur_form = prev;
            return;
        }
        case LOW_CST_ACCESS:   // ★ 더는 만들어지지 않는다 — 중위 `to`/`in` 을 없앴다
            ir_fail(c, "E-IR-UNSUP", "infix access is gone", nd->line);
            return;
        default:
            ir_fail(c, "E-IR-UNSUP", "unsupported construct in the S5 core (string)", nd->line);
            return;
    }
}

 void ir_run_ex(ir_ctx_t *c, low_cst_t *const *k, proven_size_t start, proven_size_t n, bool allow_infix) {
    if (c->failed) return;
    if (n == 0) { ir_emit(c, IRW_CONST, 0); return; }
    if (is_atom(k[start]) && k[start]->tok.kw == LOW_KW_EXPR) {   // expr island head
        proven_size_t ipos = 0;
        c->island++;
        ir_island_climb(c, k + start + 1, n - 1, &ipos, 0);
        c->island--;
        if (ipos != n - 1 && !c->failed)
            ir_fail(c, "E-IR-EXTRA", "trailing operands after expr island", k[start]->line);
        return;
    }
    if (allow_infix) {   // inside an island's paren group, bare infix is the island
        // ★★★ **기호만 보고 낱말은 안 봤다.** 여기는 `+ - * /`(LOW_TOK_OP)가 있을 때만
        //   섬으로 다시 들어갔다. 그래서 `expr (1 lt 2) and (2 lt 3)` 이 **E-IR-ARITY** 로
        //   죽었다 — 괄호 안이 전위로 파싱되어 `1` 을 머리로 읽은 것이다.
        //   하필 그것은 비결합 진단이 *"이렇게 쓰라"* 고 권하는 바로 그 형태였다:
        //   **도구가 시킨 대로 쓰면 도구가 거절했다.** 진단문을 쓰면서 그 예제를 실제로
        //   돌려보지 않았다면 영영 몰랐을 결함이다(그래서 돌려봤다).
        //   ⇒ 낱말 중위(`lt`·`and`…)도 같은 증거다. 단 **머리 자리는 세지 않는다**:
        //     `(ge a b)` 는 전위 호출이고 `(a ge b)` 만 중위다.
        for (proven_size_t i = start; i < start + n; i++) {
            if (!is_atom(k[i])) continue;
            bool infix = (k[i]->tok.kind == LOW_TOK_OP) || (i > start && ir_prec(k[i]) >= 0);
            if (!infix) continue;
            proven_size_t ipos = 0;
            c->island++;
            ir_island_climb(c, k + start, n, &ipos, 0);
            c->island--;
            if (ipos != n && !c->failed)
                ir_fail(c, "E-IR-EXTRA", "trailing operands after infix expression", k[start + ipos]->line);
            return;
        }
    }
    proven_size_t pos = start, end = start + n;
    ir_value(c, k, &pos, end);
    // ★ atomic 이면 뒤따르는 `order <name>` 를 여기서 받는다 — 파서가 arity 로 중첩하므로
    //   그 절은 **형제**로 온다(위 `ir_take_order` 주석 참조).
    //   ★ 표시는 **마지막** 명령이다: 피연산자가 먼저 방출되고 op 이 그 뒤에 온다
    //     (처음엔 `ir_value` 전의 길이를 썼다가 `load s` 를 가리켰다).
    if (c->code.len) (void)ir_take_order(c, k, &pos, end, c->code.len - 1);
    if (pos != end && !c->failed)
        ir_fail(c, "E-IR-ARITY", "extra operands in expression (strict arity)", k[pos]->line);
}
 void ir_run(ir_ctx_t *c, low_cst_t *const *k, proven_size_t start, proven_size_t n) {
    ir_run_ex(c, k, start, n, false);
}

// ★★★★★ **조건 자리의 `and` 는 값이 아니라 분기다** (2026-08-18).
//
//   `if and (ge c 65) (le c 90) . do return add c 32 . end` — 파서에게는 자연스럽고,
//   문자 부류를 판정하는 코드가 **전부 이 모양**이다(HTTP·JSON 파서의 뼈대).
//   그런데 이 모양은 검사를 하나도 안 지웠다. 이유는 분석이 게을러서가 아니라 **낳는
//   모양** 때문이다:
//
//     ge c 65 · brz L · le c 90 · not · not · br J · L: const 0 · J: brz M
//
//   `J` 에서 조건은 **두 경로가 합류한 불리언**이라 술어가 통째로 사라진다. 그래서
//   `c ≤ 90` 을 몸통이 못 본다. 같은 뜻을 중첩 `if` 로 손수 쓰면 **둘 다 지워진다** —
//   즉 *같은 프로그램인데 쓰는 법에 따라 검사가 남았다.*
//
//   ⇒ 고칠 자리는 합류가 아니라 **여기**다. 조건 자리에서는 `and` 를 불리언으로
//     만들지 않고 **분기 여러 개**로 낮춘다. 거짓 목적지가 하나뿐이므로 BRZ 를 여러 개
//     내고 전부 같은 곳으로 꿰매면 된다.
//
//   ★ 의미가 같은가: `and` 는 정수에도 쓰이지만 **논리곱**이다(`(a && b) ? 1 : 0`,
//     상수접기 표와 같은 정의). 그리고 지금 낮추기도 이미 **단락**이다 — 오른쪽은
//     왼쪽이 참일 때만 계산된다. 중첩 분기는 그 순서를 그대로 지킨다.
//     ⇒ 값도 트랩도 같고, 사라지는 것은 **불리언을 물질화하는 일** 뿐이다.
//
//   ☞ `or` 은 안 한다: 참 목적지가 갈라져 꿰맬 곳이 둘이 되고, 무엇보다 **재 보니
//     코퍼스에 값이 없었다**. 값이 있는 쪽만 싣는다.
/* IR_COND_MAXBRZ — low_ir_priv.h */
static bool is_atom_tok_and(const low_cst_t *h) {
    return h->tok.kind == LOW_TOK_IDENT && veq(h->tok.lex, "and");
}
// 괄호는 값을 안 바꾼다 — `and` 를 찾을 때만 벗긴다(방출은 원래 노드로 한다).
static const low_cst_t *ir_cond_peel(const low_cst_t *nd) {
    while (nd && nd->kind == LOW_CST_GROUP && nd->nkids == 1) nd = nd->kids[0];
    return nd;
}
 proven_size_t ir_cond_node(ir_ctx_t *c, const low_cst_t *nd,
                                  proven_size_t *sites, proven_size_t ns);
// 평평한 형태(`and A B` 가 형제로 온다) — 스트림에서 하나씩 소비하며 가른다.
static proven_size_t ir_cond_stream(ir_ctx_t *c, low_cst_t *const *k, proven_size_t *pos,
                                    proven_size_t end, proven_size_t *sites, proven_size_t ns) {
    if (c->failed) return ns;
    if (*pos < end && is_atom(k[*pos]) && is_atom_tok_and(k[*pos]) &&
        ns + 2 <= IR_COND_MAXBRZ) {
        (*pos)++;                                   // `and` 를 소비한다
        ns = ir_cond_stream(c, k, pos, end, sites, ns);   // 왼쪽
        ns = ir_cond_stream(c, k, pos, end, sites, ns);   // 오른쪽
        return ns;
    }
    // ★ 자기 완결적인 노드(괄호 그룹·arity 로 중첩된 폼)는 **노드 판정에 넘긴다** —
    //   평평한 실행에서도 `and (and A B) C` 의 안쪽이 갈라지도록. 원자는 넘기지 않는다:
    //   원자는 형제를 피연산자로 데려가므로 노드 하나로 끝나지 않는다.
    if (*pos < end && k[*pos] && !is_atom(k[*pos])) {
        const low_cst_t *nd = k[(*pos)++];
        return ir_cond_node(c, nd, sites, ns);
    }
    ir_value(c, k, pos, end);
    if (ns < IR_COND_MAXBRZ) sites[ns++] = ir_emit(c, IRW_BRZ, 0);
    return ns;
}
// ★★ 조건 자리의 **섬**도 `and` 에서 가른다(2026-09-28, 코드 검토). 섬의 `and` 는 단락 평가로 낮추지만 결과를
//   0/1 로 합류시키므로 몸통이 술어를 못 봤다 — `if expr (lt i (len xs)) and (lt i 8) .` 은 첨자 검사가 남고, 같은
//   뜻의 전위 `if and (lt i (len xs)) (lt i 8) .` 은 지웠다(쓰는 법에 따라 검사가 남는 위 병과 같다).
//   섬의 맨 위 층에 `or` 가 없으면 `and` 가 가장 약한 연산자이므로(§6.3.2 표), 맨 위의 `and` 마다 잘라
//   조각마다 값을 내고 BRZ 를 낸다. 맨 위에 `or` 가 있거나 `and` 가 없으면 0 을 돌려 보통 길로 보낸다.
static proven_size_t ir_cond_island(ir_ctx_t *c, low_cst_t *const *k, proven_size_t n,
                                    proven_size_t *sites, proven_size_t ns) {
    proven_size_t nand = 0;
    for (proven_size_t i = 1; i < n; i++) {
        if (!is_atom(k[i])) continue;
        int pr = ir_prec(k[i]);
        if (pr == 1) return 0;                         // 맨 위에 or — 가르지 않는다
        if (pr == 2) nand++;
    }
    if (!nand || ns + nand + 1 > IR_COND_MAXBRZ) return 0;
    proven_size_t s = 0;
    for (proven_size_t i = 0; i <= n && !c->failed; i++) {
        if (i < n && !(i > s && is_atom(k[i]) && ir_prec(k[i]) == 2)) continue;
        proven_size_t ipos = 0, len = i - s;
        c->island++;
        ir_island_climb(c, k + s, len, &ipos, 0);
        c->island--;
        if (ipos != len && !c->failed)
            ir_fail(c, "E-IR-EXTRA", "trailing operands after expr island", k[s]->line);
        sites[ns++] = ir_emit(c, IRW_BRZ, 0);
        s = i + 1;
    }
    return ns;
}
 proven_size_t ir_cond_node(ir_ctx_t *c, const low_cst_t *nd,
                                  proven_size_t *sites, proven_size_t ns) {
    if (c->failed || !nd) return ns;
    const low_cst_t *h = ir_cond_peel(nd);
    if (h && h->kind == LOW_CST_FORM && h->nkids >= 2 && is_atom(h->kids[0]) && h->kids[0]->tok.kw == LOW_KW_EXPR) {
        proven_size_t r = ir_cond_island(c, h->kids + 1, h->nkids - 1, sites, ns);
        if (r) return r;
    }
    // ★ FORM 의 kids[0] 은 **머리 원자**다(헤더 주석과 달리 인자만 있는 게 아니다 —
    //   `le c 90` 이 nkids=3 이다). 그래서 인자는 kids[1], kids[2] 다.
    if (h && h->kind == LOW_CST_FORM && h->nkids == 3 && is_atom_tok_and(h) &&
        ns + 2 <= IR_COND_MAXBRZ) {
        ns = ir_cond_node(c, h->kids[1], sites, ns);   // 왼쪽 — 거짓이면 곧장 나간다
        ns = ir_cond_node(c, h->kids[2], sites, ns);   // 오른쪽 — 왼쪽이 참일 때만 온다
        return ns;
    }
    ir_node(c, nd);
    if (ns < IR_COND_MAXBRZ) sites[ns++] = ir_emit(c, IRW_BRZ, 0);
    return ns;
}
// 조건을 낮추고 **거짓일 때 뛰는 자리들**을 돌려준다. 항상 1 개 이상이다.
 proven_size_t ir_cond_brz(ir_ctx_t *c, low_cst_t *const *k, proven_size_t start,
                                 proven_size_t n, proven_size_t *sites) {
    // ★ 조건이 **노드 하나**일 때(파서가 arity 로 중첩한 보통 모양).
    if (n == 1) {
        proven_size_t ns = ir_cond_node(c, k[start], sites, 0);
        if (ns) return ns;
    }
    // ★★ **평평한 실행도 같이 갈라야 한다** (골든 `nest` 가 잡았다). `--ir --flat` 은
    //   중첩 없이 `and (ge …) (le …)` 를 **노드 여럿**으로 준다. 한쪽만 가르면 두 실행이
    //   *다른 코드*를 내고, def 해시가 갈라진다 — RFC-0012 가 오라클인 자리다:
    //   **같은 뜻이면 같은 해시**. 모양이 둘이면 낮추기도 둘 다 알아야 한다.
    if (n >= 2 && is_atom(k[start]) && k[start]->tok.kw == LOW_KW_EXPR) {   // 평평한 실행의 섬
        proven_size_t r = ir_cond_island(c, k + start + 1, n - 1, sites, 0);
        if (r) return r;
    }
    if (n >= 2 && is_atom(k[start]) && is_atom_tok_and(k[start])) {
        proven_size_t pos = start, end = start + n;
        proven_size_t ns = ir_cond_stream(c, k, &pos, end, sites, 0);
        if (pos != end && !c->failed)
            ir_fail(c, "E-IR-ARITY", "extra operands in expression (strict arity)", k[pos]->line);
        if (ns) return ns;
    }
    ir_run(c, k, start, n);
    sites[0] = ir_emit(c, IRW_BRZ, 0);
    return 1;
}


// ── clause scanning + hashing ─────────────────────────────────────────────────

 bool is_clause_word(proven_u8str_view_t v) { return low_is_clause_word(v); }  // ★ 하나의 어휘

typedef struct { proven_array_t bytes; } hbuf_t;   // of proven_u8
static void hput(hbuf_t *h, const void *p, proven_size_t n) {
    for (proven_size_t i = 0; i < n; i++) (void)proven_array_push(&h->bytes, (const proven_u8 *)p + i);
}
static void hput_view(hbuf_t *h, proven_u8str_view_t v) {
    proven_u32 n = (proven_u32)v.size;
    hput(h, &n, 4);
    hput(h, v.ptr, v.size);
}
static void hput_u32(hbuf_t *h, proven_u32 v) { hput(h, &v, 4); }

// iface-hash: kind + per-param type words + return type words + effects atoms
// ★ 효과 어휘는 **하나**다 — low_check.h 의 요약과 같은 비트를 쓴다(두 벌이면 갈린다).
static unsigned ir_effect_bit(proven_u8str_view_t v) {
    if (veq(v, "io")) return 1u;
    if (veq(v, "alloc")) return 2u;
    if (veq(v, "state")) return 4u;
    if (veq(v, "panic")) return 8u;
    if (veq(v, "unsafe")) return 16u;
    return 0u;   // none, 또는 아직 원시어가 없는 효과
}
 void ir_iface_hash(proven_allocator_t work, const low_cst_t *f, low_ir_def_t *d) {
    hbuf_t h = { .bytes = PROVEN_ARRAY_INIT(work, proven_u8, 64).value };
    hput(&h, "LOWIFACE1", 9);
    proven_u8 kind = d->is_calc ? 1 : 2;
    hput(&h, &kind, 1);
    for (proven_size_t i = 2; i < f->nkids; i++) {
        if (!is_atom(f->kids[i])) continue;
        proven_u8str_view_t w = f->kids[i]->tok.lex;
        if (!is_clause_word(w)) continue;
        bool is_input = veq(w, "input"), is_output = veq(w, "output"), is_effects = veq(w, "effects");
        // ★★★ RFC-0012 §6.7 — **계약도 인터페이스다.** 여기가 시그니처만 해싱하고 있었다.
        //   §477 의 정의는 `iface-hash = hash(시그니처 + 계약, 본문 제외)` 인데, 실측(2026-07-30):
        //       fn twice … .                       → iface:501eba9b550c4bbb
        //       fn twice … requires le a 100 .      → iface:501eba9b550c4bbb    ← **같다**
        //       fn twice … ensures ge ret a .       → iface:501eba9b550c4bbb    ← **같다**
        //   ⇒ 그리고 그것이 왜 위험한가: §484-492 의 증분 전파는 *"D 가 callee X 를 타입검사·
        //     **계약검사**할 때 의존하는 것은 X 의 iface-hash 다"* 에 기댄다. 계약이 iface 에
        //     없으면 **X 의 계약이 바뀌어도 D 가 캐시 적중**한다 — D 는 **옛 계약을 믿은 채**
        //     남는다. 검사를 지우는 근거가 계약인데(05장·RFC-0055) 그 계약이 바뀐 것을
        //     의존자가 모르면, 그것은 **증명을 낡은 전제 위에 세우는 것**이다.
        //   ⇒ requires·ensures·errors 를 iface 에 싣는다. 절의 토큰을 **소스 순서로** 해싱한다.
        //     순서에 민감한 것은 **안전한 방향의 부정확**이다: 같은 뜻을 다르게 적으면 재빌드가
        //     한 번 더 도는 손해뿐이고, 반대(다른 뜻이 같은 해시)는 캐시가 거짓말을 한다.
        //     (effects 는 아래에서 정규화한다 — 그쪽은 **집합**이라고 문법이 말하기 때문이다.)
        bool is_ctr = veq(w, "requires") || veq(w, "ensures") || veq(w, "errors");
        if (!is_input && !is_output && !is_effects && !is_ctr) continue;
        // ★★★ RFC-0012(내용주소화) × RFC-0057(정규화된 요약) —
        //   `effects` 는 **집합**이다. 그런데 절의 **원문 텍스트**를 해싱하고 있었다:
        //       effects io alloc  →  h:cb42ef12…
        //       effects alloc io  →  h:edaeb15e…     ← **같은 뜻인데 다른 해시**
        //   ⇒ 내용주소화가 **의미가 아니라 철자**를 주소화했다. 그러면 캐시가 갈리고,
        //     "같은 인터페이스" 라는 말이 아무것도 뜻하지 않는다.
        //   ⇒ 효과는 **정규화된 순서**로(비트 오름차순) 해싱한다. 나머지 절은 그대로.
        const char *tag = is_input ? "P" : is_output ? "R" : is_effects ? "E" : "C";
        hput(&h, tag, 1);
        if (is_ctr) {
            hput_view(&h, w);                       // 어느 계약 절인가(requires/ensures/errors)
            for (proven_size_t j = i + 1; j < f->nkids; j++) {
                if (!is_atom(f->kids[j])) break;
                if (is_clause_word(f->kids[j]->tok.lex)) break;
                hput_view(&h, f->kids[j]->tok.lex);
            }
            continue;
        }
        if (is_effects) {
            unsigned e = 0;
            for (proven_size_t j = i + 1; j < f->nkids; j++) {
                if (!is_atom(f->kids[j])) break;
                if (is_clause_word(f->kids[j]->tok.lex)) break;
                e |= ir_effect_bit(f->kids[j]->tok.lex);
            }
            proven_u8 eb[4] = { (proven_u8)(e & 0xff), (proven_u8)((e >> 8) & 0xff),
                                (proven_u8)((e >> 16) & 0xff), (proven_u8)((e >> 24) & 0xff) };
            hput(&h, eb, 4);
            continue;
        }
        proven_size_t j = i + (is_input ? 2 : 1);   // skip the param name
        for (; j < f->nkids; j++) {
            if (!is_atom(f->kids[j])) break;
            if (is_clause_word(f->kids[j]->tok.lex)) break;
            hput_view(&h, f->kids[j]->tok.lex);
        }
    }
    low_blake3_256(h.bytes.data, h.bytes.len, d->iface_hash);
    proven_array_destroy(&h.bytes);
}

// ── SCC fixed-point Merkle def-hash (SPEC-011 §12.3) ─────────────────────────
// Call sites in the encoding carry the callee's def-hash (topological over the
// SCC DAG). Within an SCC, members are canonically ordered by a preliminary
// name-free hash; in-SCC call sites then carry the member's canonical index, the
// group is hashed once, and each member's def-hash binds (group, index).

typedef enum { ENC_PRELIM, ENC_FINAL } enc_mode_t;

// operand identity encoding (shared by tree nodes)
static void enc_operand(const low_ir_t *ir, low_irw_t w, proven_i64 a, hbuf_t *h,
                        enc_mode_t mode, const int *scc_of, int my_scc, const int *canon_idx) {
    switch (w) {
        case IRW_CALL: {
            a = (proven_i64)IR_CALL_IDX(a);
            proven_size_t ci = (proven_size_t)a;
            if (scc_of[ci] == my_scc) {
                if (mode == ENC_PRELIM) { hput(h, "\xff", 1); hput(h, ir->defs[ci].iface_hash, 32); }
                else { hput(h, "\xfe", 1); hput_u32(h, (proven_u32)canon_idx[ci]); }
            } else {
                hput(h, "\xfd", 1); hput(h, ir->defs[ci].def_hash, 32);
            }
            break;
        }
        case IRW_MAKE: {
            const low_ir_make_t *mk = &ir->makes[a];
            hput_view(h, mk->type_name);
            hput_u32(h, (proven_u32)mk->nfields);
            for (proven_size_t x = 0; x < mk->nfields; x++) hput_view(h, mk->fields[x]);
            break;
        }
        case IRW_WRAP_ERR: hput_view(h, ir->errs[a]); break;
        case IRW_FIELD:    hput_view(h, ir->fields[a]); break;   // field names are structural
        case IRW_STR:      hput_view(h, ir->strs[a]); break;     // literal bytes are structural
        case IRW_LBUFC:    hput_u32(h, (proven_u32)((proven_u64)a & 0xffffffffu)); hput_view(h, ir->strs[(proven_u64)a >> 32]); break;
        case IRW_BFILL:    hput_u32(h, (proven_u32)((proven_u64)a >> 32)); if ((proven_u64)a & 0xffffffffu) hput_view(h, ir->strs[((proven_u64)a & 0xffffffffu) - 1]); break;
        case IRW_RESBLK: {
            // ★ 이 낱말의 정체는 **어느 타입의 칸인가**다 — 주소는 링커가 나중에 준다.
            const low_ir_struct_t *rs = &ir->structs[a];
            hput_u32(h, (proven_u32)rs->total);
            hput_view(h, rs->name);
            break;
        }
        case IRW_MMIOBLK: {
            // ★ 이 낱말의 정체는 **주소**다 — 배치가 같아도 기저가 다르면 다른 블록이다.
            const low_ir_struct_t *ms = &ir->structs[a];
            hput_u32(h, (proven_u32)ms->total);
            proven_i64 mb = ms->mmio_base;
            hput(h, &mb, 8);
            break;
        }
        case IRW_VIEW: case IRW_TRYVIEW: case IRW_ENCODE: {
            // the layout is the type's structural identity
            const low_ir_struct_t *s = &ir->structs[a];
            proven_u8 pk = s->packed ? 1 : 0;
            hput(h, &pk, 1);
            hput_u32(h, (proven_u32)s->align);   // the align contract is part of the type
            hput_u32(h, (proven_u32)s->nf);
            for (proven_size_t x = 0; x < s->nf; x++) {
                hput_view(h, s->f[x].name);
                proven_u8 meta[3] = { s->f[x].size, s->f[x].be ? 1 : 0, s->f[x].flt ? 1 : 0 };
                hput(h, meta, 3);
            }
            break;
        }
        default:
            hput(h, &a, 8);
            break;
    }
}

// ── canonical TREE encoding (SPEC-011 §12.2) ─────────────────────────────────
// The word stream is deterministic post-sugar de-Bruijn code already; for the
// canonical identity we re-express it as the §12.2 tree: expressions become
// nested nodes (reconstructed by symbolic stack simulation — the lowering is
// statement-granular, so the stack is empty at every branch boundary), and
// branch targets are normalized to LABEL ids (order of appearance) instead of
// absolute instruction offsets. Falls back to the flat stream on any stack
// irregularity (never expected; safety for future words).

#define TR_MAXNODE 4096
#define TR_MAXKID  16
#define TR_LABEL   250   // pseudo-words in the tree stream
#define TR_STMT    251
typedef struct { proven_u8 w; proven_i64 a; proven_u16 kid[TR_MAXKID]; proven_u8 nk; } tr_node_t;

 int ir_word_arity(const low_ir_t *ir, low_irw_t w, proven_i64 a) {
    switch (w) {
        /* ★★★ **빌트인의 인자 수는 `low_arity.h` 한 곳에서 온다** (X-0047, 2026-09-21).
           전에는 같은 80 개의 수를 여기에도 손으로 적었고, 떨어지는 `case` 묶음 사이에 새 잎
           (`IRW_AESCTR`)을 끼우자 이웃 셋(`IRW_ALOAD`·`IRW_ELEMCK`·`IRW_SAMESL`)이 **넷을 문다**고
           답했다 — 구간 분석이 스택을 잘못 세어 타입 메타가 사라지고, VM 은 옳은데 네이티브만
           틀렸다. 같은 수를 두 곳에 적은 것이 원인이다. 이제 새 빌트인은 **이 표에 안 들어온다**;
           누가 손으로 다시 적으면 `case` 가 겹쳐 **컴파일이 멈춘다**.
           ☞ 아래에 남은 것은 표면 이름이 없는 **내부 낱말**(90 개)뿐이고, 그것들은 **한 줄씩 닫는다**
             (`case X: return N;` — X-0047 ⓐ). 떨어지는 묶음이 없으니 사이에 끼워도 이웃이 안 바뀐다.
             내부 낱말을 더할 때도 이 모양으로. */
#define X(n, w, a) case w: return a;
        LOW_BUILTINS(X)
#undef X
        case IRW_CONST: return 0;
        case IRW_LOAD: return 0;
        case IRW_STR: return 0;
        case IRW_REF: return 0;
        case IRW_MREF: return 0;
        case IRW_SCHED: return 0;                              // ★ `schedule` — 피연산자 0, 단위 push
        case IRW_YIELD: return 0;                              // ★ `yield` — 피연산자 0, 단위 push
        case IRW_CHNEW: return 0;                              // ★ `channel` — 피연산자 0, 핸들 push
        case IRW_WRAP_ERR: return 0;                           // ★ `none` — 피연산자 0
        case IRW_FCONST: return 0;
        case IRW_WRAP_NONE: return 0;
        case IRW_ARGC: return 0;                               // ★ 인자 개수 — 피연산자 0
        case IRW_RMARK: return 0;                              // ★ region 표식 — 피연산자 0
        case IRW_FNREF: return 0;                              // ★ `unsafe_fn <op>` — 주소를 낸다(0 소비)
        case IRW_NPAIR: return 0;                              // ★ 소켓 쌍 — 피연산자 0(cap 만), option u64 push
        case IRW_CAST: return 1;
        case IRW_UNM: return 1;
        case IRW_ASSERT: return 1;
        case IRW_TTYSIZE: return 0;                            // ★ cap tty — 인자 없음
        case IRW_PANIC: return 0;
        case IRW_BINM: return 2;
        case IRW_WRITE: return 2;                              // ★ (fd, bytes)
        case IRW_RDIN: return 2;                               // ★ (fd, dst)
        case IRW_RNEW: return 2;                               // ★ (mem, depth)
        case IRW_FOPEN: return 2;                              // ★ 스트림 리프 (둘씩)
        case IRW_FREAD: return 2;
        case IRW_FWRITE: return 2;
        case IRW_DREAD: return 2;                              // ★ (dh, dst)
        case IRW_PRENAME: return 2;                            // ★ (old, new)
        case IRW_NSEND: return 2;                              // ★ 소켓 (fd, bytes)/(fd, dst)
        case IRW_NRECV: return 2;
        case IRW_RREAD: return 3;                              // ★ (rh, fd, dst)
        case IRW_RWRITE: return 3;                             // ★ (rh, fd, src)
        case IRW_FSEEK: return 3;                              // ★ (fd, off, whence)
        case IRW_TTYRAW: return 1;                             // ★ cap tty — (on)
        case IRW_TTYREAD: return 1;                            // ★ cap tty — (dst)
        case IRW_FCLOSE: return 1;                             // ★ (fd)
        case IRW_DOPEN: return 1;                              // ★ (path)
        case IRW_DCLOSE: return 1;                             // ★ (dh)
        case IRW_FTYPE: return 1;                              // ★ (path)
        case IRW_LTYPE: return 1;
        case IRW_DMAKE: return 1;                              // ★ (path)
        case IRW_PREMOVE: return 1;                            // ★ (path)
        case IRW_RRESET: return 1;                             // ★ region 표식으로 되돌린다
        case IRW_NCLOSE: return 1;                             // ★ 소켓 (fd)
        case IRW_NLISTEN: return 1;                            // ★ 네트워크 (port)/(fd)
        case IRW_NPORT: return 1;
        case IRW_NCONNECT: return 2;                           // ★ (addr, port) — 2026-10-03: 1 로 남아 있었다(X-0032 에서 주소를 더할 때 이 줄만 안 고쳤다) — 구간 분석의 스택 셈이 한 칸 어긋났다
        case IRW_NRESOLVE: return 1;                           // ★ (name)
        case IRW_PSPAWN: return 1;                             // ★ RFC-0136 — (argv)
        case IRW_PREAD: return 2;                              // ★ (h, dst)
        case IRW_PPOLL: return 1;
        case IRW_PWAIT: return 1;
        case IRW_PKILL: return 1;
        case IRW_NACCEPT: return 1;
        case IRW_WRAP_OK: return 1;
        case IRW_WRAP_SOME: return 1;
        case IRW_ARGV: return 1;
        case IRW_ENVGET: return 1;
        case IRW_ALLOCB: return 1;
        case IRW_HASVAL: return 1;                             // ★ option|result → bool (value_or 의 지연 분기용)
        case IRW_TRY: return 1;
        case IRW_VARRAY: return 1;
        case IRW_ELSE_NONE: return 1;
        case IRW_ELSE_ERR: return 1;
        case IRW_VIEW: return 1;
        case IRW_TRYVIEW: return 1;
        case IRW_ENCODE: return 1;
        case IRW_SPLAT: return 1;
        case IRW_BITCAST: return 1;
        case IRW_SNEW: return 1;
        case IRW_FIELD: return 1;
        case IRW_VREVERSE: return 1;                           // vec → vec (레인 재배열, vec 1개 소비)
        case IRW_VROTATE: return 1;
        case IRW_VSHUFFLE: return 1;
        case IRW_SPOP_INTO: return 1;
        case IRW_DRAIN: return 1;                              // ★ drain — 인스턴스 하나 소비
        case IRW_CHRECV: return 1;                             // ★ chrecv — 채널 하나 소비, 값 push
        case IRW_AWAIT: return 1;                              // ★ await — job 핸들 하나 소비, 결과 push
        case IRW_CHSEND: return 2;                             // ★ chsend — 채널·값 소비, 단위 push
        case IRW_VLOAD: return 2;
        case IRW_FSTORE: return 2;
        case IRW_VAVG: return 2;                               // va, vb → vec (target intrinsic)
        case IRW_ELEMCK: return 2;
        case IRW_ISTORE: return 3;
        case IRW_VSTORE: return 3;
        case IRW_VLOADM: return 4;                             // src, idx, mask, passthrough → vec (merge)
        case IRW_VSTOREM: return 4;                            // dst, idx, vec, mask → ()
        case IRW_PREFETCH: return 2;                           // ★ 슬라이스 + 색인 — 미는 값은 없다(힌트)
        case IRW_RESBLK: return 0;                             // ★ 피연산자 0 — 저장소는 **링커가 준다**(RFC-0039 §9-2)
        case IRW_MMIOBLK: return 0;                            // ★ 피연산자 0 — 기저 주소는 **타입 안에** 있다(RFC-0042 §8-2)
        case IRW_CALL: return (int)ir->defs[IR_CALL_IDX(a)].nparams;
        case IRW_ASEND: return (int)ir->defs[a & 0xffff].nparams;   // ★ async send — 핸들러 파라미터 수(instance+args) 소비
        case IRW_TASEND: return (int)ir->defs[a & 0xffff].nparams;   // ★ try async send — 같은 팝(instance+args), result 를 민다
        case IRW_TSPAWN: return (int)ir->defs[a & 0xffff].nparams;   // ★ task spawn — op 파라미터 수(인스턴스 없음) 소비
        case IRW_MAKE: return (int)ir->makes[a].nfields;
        case IRW_LBUF: return 0;                               // ★ T2b-2 — 피연산자 0, 슬라이스 push
        case IRW_LBUFC: return 0;                              // ★ T2b-3 — 피연산자 0, 슬라이스 push
        case IRW_BFILL: return 1;                              // ★ §13.7 — 슬라이스 → 채운 슬라이스
        case IRW_SCOPY: return 2;                              // ★ RFC-0132 P3 — (받는 쪽, 주는 쪽) → 0
        default:
            return -1;   // statements (STORE/DROP/RET/BR/BRZ) handled separately
    }
}

static void tr_encode_node(const low_ir_t *ir, const tr_node_t *nodes, proven_u16 n, hbuf_t *h,
                           enc_mode_t mode, const int *scc_of, int my_scc, const int *canon_idx) {
    const tr_node_t *nd = &nodes[n];
    hput(h, &nd->w, 1);
    if (nd->w == TR_LABEL || nd->w == TR_STMT || nd->w == (proven_u8)IRW_BR || nd->w == (proven_u8)IRW_BRZ)
        hput_u32(h, (proven_u32)nd->a);   // label id / statement kind — offset-free
    else
        enc_operand(ir, (low_irw_t)nd->w, nd->a, h, mode, scc_of, my_scc, canon_idx);
    proven_u8 nk = nd->nk;
    hput(h, &nk, 1);
    for (proven_u8 k = 0; k < nd->nk; k++)
        tr_encode_node(ir, nodes, nd->kid[k], h, mode, scc_of, my_scc, canon_idx);
}

static void ir_encode_def(const low_ir_t *ir, const low_ir_def_t *d, hbuf_t *h,
                          enc_mode_t mode, const int *scc_of, int my_scc, const int *canon_idx) {
    hput(h, d->iface_hash, 32);

    // label ids: branch targets in order of target offset (offset-independent ids)
    proven_i64 targets[64]; proven_size_t ntargets = 0;
    for (proven_size_t i = 0; i < d->ncode; i++)
        if ((d->code[i].w == IRW_BR || d->code[i].w == IRW_BRZ) && ntargets < 64) {
            bool seen = false;
            for (proven_size_t t = 0; t < ntargets && !seen; t++) seen = targets[t] == d->code[i].a;
            if (!seen) targets[ntargets++] = d->code[i].a;
        }
    for (proven_size_t a2 = 0; a2 + 1 < ntargets; a2++)   // sort ascending
        for (proven_size_t b2 = a2 + 1; b2 < ntargets; b2++)
            if (targets[b2] < targets[a2]) { proven_i64 t = targets[a2]; targets[a2] = targets[b2]; targets[b2] = t; }

    static tr_node_t nodes[TR_MAXNODE];
    proven_u16 stmts[TR_MAXNODE]; proven_size_t nstmts = 0, nnodes = 0;
    proven_u16 stk[256]; proven_size_t sp = 0;
    bool ok = d->ncode <= TR_MAXNODE;

    for (proven_size_t i = 0; i < d->ncode && ok; i++) {
        // ★★ 넘치면 **포기한다**(아래 식 자리와 같은 처방). 전엔 이 자리에서만 라벨 표지를
        //   *조용히 안 넣고* 계속 갔다 — 부분 트리로 검증하면 그 검증은 거짓말이다.
        //   ☞ 검증기는 *"통과"* 와 *"못 봤다"* 를 갈라 말해야 하고, 그 둘 사이에
        //     *"덜 보고 통과"* 라는 셋째 답은 없어야 한다.
        for (proven_size_t t = 0; t < ntargets; t++)
            if (targets[t] == (proven_i64)i) {   // label marker
                if (nnodes >= TR_MAXNODE) { ok = false; break; }
                nodes[nnodes] = (tr_node_t){ .w = TR_LABEL, .a = (proven_i64)t, .nk = 0 };
                stmts[nstmts++] = (proven_u16)nnodes++; LOW_HWM("ir:tr-nodes", nnodes, TR_MAXNODE);
            }
        if (!ok) break;
        low_irw_t w = d->code[i].w;
        proven_i64 a = d->code[i].a;
        int ar = ir_word_arity(ir, w, a);
        if (ar >= 0) {   // expression node
            if ((proven_size_t)ar > sp || ar > TR_MAXKID || nnodes >= TR_MAXNODE) { ok = false; break; }
            tr_node_t nd = { .w = (proven_u8)w, .a = a, .nk = (proven_u8)ar };
            for (int k = ar; k-- > 0; ) nd.kid[k] = stk[--sp];
            nodes[nnodes] = nd;
            stk[sp++] = (proven_u16)nnodes++;
            continue;
        }
        // statement words
        proven_u8 nk = 0; proven_u16 kid0 = 0; proven_i64 sa = a;
        switch (w) {
            case IRW_STORE: case IRW_DROP: case IRW_RET:
                if (sp == 0) { ok = false; break; }
                kid0 = stk[--sp]; nk = 1;
                break;
            case IRW_BRZ:
                if (sp == 0) { ok = false; break; }
                kid0 = stk[--sp]; nk = 1;
                for (proven_size_t t = 0; t < ntargets; t++) if (targets[t] == a) sa = (proven_i64)t;
                break;
            case IRW_BR:
                for (proven_size_t t = 0; t < ntargets; t++) if (targets[t] == a) sa = (proven_i64)t;
                break;
            default:
                ok = false;
                break;
        }
        if (!ok || nnodes >= TR_MAXNODE) { ok = false; break; }
        tr_node_t nd = { .w = (proven_u8)w, .a = sa, .nk = nk };
        if (nk) nd.kid[0] = kid0;
        nodes[nnodes] = nd;
        stmts[nstmts++] = (proven_u16)nnodes++;
    }
    if (ok && sp != 0) ok = false;   // dangling expression — not statement-granular

    if (ok) {
        hput(h, "LOWTREE1", 8);
        hput_u32(h, (proven_u32)nstmts);
        for (proven_size_t s2 = 0; s2 < nstmts; s2++)
            tr_encode_node(ir, nodes, stmts[s2], h, mode, scc_of, my_scc, canon_idx);
        return;
    }
    // fallback: flat stream (safety — not expected with the current lowering)
    hput(h, "LOWFLAT1", 8);
    hput_u32(h, (proven_u32)d->ncode);
    for (proven_size_t i = 0; i < d->ncode; i++) {
        proven_u8 w = (proven_u8)d->code[i].w;
        hput(h, &w, 1);
        enc_operand(ir, d->code[i].w, d->code[i].a, h, mode, scc_of, my_scc, canon_idx);
    }
}

typedef struct {
    const low_ir_t *ir;
    int  index[IR_MAXDEFS], low[IR_MAXDEFS], scc_of[IR_MAXDEFS];
    bool onstack[IR_MAXDEFS];
    int  stack[IR_MAXDEFS], sp, next_index, nsccs;
    int  scc_members[IR_MAXDEFS];   // members grouped by emission; see scc_start
    int  scc_start[IR_MAXDEFS + 1];
    int  nmembers;
} scc_t;

static void scc_visit(scc_t *s, int u) {
    s->index[u] = s->low[u] = s->next_index++;
    s->stack[s->sp++] = u; s->onstack[u] = true;
    const low_ir_def_t *d = &s->ir->defs[u];
    if (d->lowered)
        for (proven_size_t i = 0; i < d->ncode; i++) {
            if (d->code[i].w != IRW_CALL) continue;
            int v = (int)IR_CALL_IDX(d->code[i].a);
            if (s->index[v] < 0) { scc_visit(s, v); if (s->low[v] < s->low[u]) s->low[u] = s->low[v]; }
            else if (s->onstack[v] && s->index[v] < s->low[u]) s->low[u] = s->index[v];
        }
    if (s->low[u] == s->index[u]) {   // root: pop one SCC (emitted callees-first)
        s->scc_start[s->nsccs] = s->nmembers;
        int v;
        do {
            v = s->stack[--s->sp]; s->onstack[v] = false;
            s->scc_of[v] = s->nsccs;
            s->scc_members[s->nmembers++] = v;
        } while (v != u);
        s->nsccs++;
    }
}

 void ir_def_hashes(proven_allocator_t work, low_ir_t *ir) {
    // ★ 여기서 **조용히 돌아서면 모든 def 해시가 0 이 된다** — 내용 주소화가 꺼지고
    //   모든 op 이 같은 주소를 갖는다. 이제 op 수는 IR_MAXOPS 에서 이미 거절되므로
    //   여기 도달할 수 없다. 그래도 남겨 둔다: 도달하면 **말한다**(조용히 틀리지 않는다).
    if (ir->ndefs > IR_MAXDEFS) {
        low_diag_t d_ = { .sev = LOW_SEV_ERROR, .code = "E-IR-LIMIT",
                          .msg = "this compilation unit has more ops than the tool can carry, and "
                                 "the CONTENT HASHES were about to be skipped SILENTLY — every op "
                                 "would have gotten the same address (all zeroes). Content "
                                 "addressing IS this language's identity (SPEC-011). Split the unit",
                          .line = 0, .col = 0 };
        (void)proven_array_push(&ir->diags, &d_);
        ir->ok = false;
        return;
    }
    // unlowered defs have no body: their def-hash stands in as the iface-hash
    for (proven_size_t i = 0; i < ir->ndefs; i++)
        if (!ir->defs[i].lowered) memcpy(ir->defs[i].def_hash, ir->defs[i].iface_hash, 32);

    scc_t s = { .ir = ir, .sp = 0, .next_index = 0, .nsccs = 0, .nmembers = 0 };
    for (proven_size_t i = 0; i < ir->ndefs; i++) { s.index[i] = -1; s.scc_of[i] = -1; s.onstack[i] = false; }
    for (proven_size_t i = 0; i < ir->ndefs; i++)
        if (ir->defs[i].lowered && s.index[i] < 0) scc_visit(&s, (int)i);
    s.scc_start[s.nsccs] = s.nmembers;

    int canon_idx[IR_MAXDEFS] = { 0 };
    for (int g = 0; g < s.nsccs; g++) {
        int *members = &s.scc_members[s.scc_start[g]];
        int n = s.scc_start[g + 1] - s.scc_start[g];
        // canonical member order by preliminary (name-free) hash
        proven_u8 prelim[IR_MAXDEFS > 64 ? 64 : IR_MAXDEFS][32];   // n ≤ ndefs; scratch per group
        for (int m = 0; m < n && m < 64; m++) {
            hbuf_t h = { .bytes = PROVEN_ARRAY_INIT(work, proven_u8, 256).value };
            hput(&h, "LOWPRE1", 7);
            ir_encode_def(ir, &ir->defs[members[m]], &h, ENC_PRELIM, s.scc_of, g, canon_idx);
            low_blake3_256(h.bytes.data, h.bytes.len, prelim[m]);
            proven_array_destroy(&h.bytes);
        }
        for (int a = 0; a < n; a++)   // insertion sort by prelim bytes
            for (int b = a + 1; b < n; b++)
                if (memcmp(prelim[b], prelim[a], 32) < 0) {
                    proven_u8 t[32]; memcpy(t, prelim[a], 32); memcpy(prelim[a], prelim[b], 32); memcpy(prelim[b], t, 32);
                    int ti = members[a]; members[a] = members[b]; members[b] = ti;
                }
        for (int m = 0; m < n; m++) canon_idx[members[m]] = m;
        // group hash over canonical encodings, then bind (group, index) per member
        hbuf_t gh = { .bytes = PROVEN_ARRAY_INIT(work, proven_u8, 512).value };
        hput(&gh, "LOWSCC1", 7);
        hput_u32(&gh, (proven_u32)n);
        for (int m = 0; m < n; m++)
            ir_encode_def(ir, &ir->defs[members[m]], &gh, ENC_FINAL, s.scc_of, g, canon_idx);
        proven_u8 group[32];
        low_blake3_256(gh.bytes.data, gh.bytes.len, group);
        proven_array_destroy(&gh.bytes);
        for (int m = 0; m < n; m++) {
            hbuf_t mh = { .bytes = PROVEN_ARRAY_INIT(work, proven_u8, 64).value };
            hput(&mh, "LOWDEF2", 7);
            hput(&mh, group, 32);
            hput_u32(&mh, (proven_u32)m);
            low_blake3_256(mh.bytes.data, mh.bytes.len, ir->defs[members[m]].def_hash);
            proven_array_destroy(&mh.bytes);
        }
    }
}

const char *low_irw_name(low_irw_t w) {
    switch (w) {
        case IRW_CONST: return "push.const"; case IRW_LOAD: return "load.local";
        case IRW_STORE: return "store.local"; case IRW_DROP: return "drop";
        case IRW_ADD: return "add.i64"; case IRW_SUB: return "sub.i64";
        case IRW_MUL: return "mul.i64"; case IRW_DIV: return "div.i64";
        case IRW_MOD: return "mod.i64"; case IRW_NEG: return "neg.i64";
        case IRW_AND: return "and"; case IRW_OR: return "or"; case IRW_NOT: return "not";
        case IRW_EQ: return "eq.i64"; case IRW_NE: return "ne.i64";
        case IRW_LT: return "lt.i64"; case IRW_LE: return "le.i64";
        case IRW_GT: return "gt.i64"; case IRW_GE: return "ge.i64";
        case IRW_BR: return "br"; case IRW_BRZ: return "brz";
        case IRW_SWITCH: return "switch";
        case IRW_CALL: return "call"; case IRW_RET: return "ret";
        case IRW_LEN: return "len"; case IRW_INDEX: return "index";
        case IRW_ISTORE: return "index.store"; case IRW_FSTORE: return "field.store";
        case IRW_SUBSLICE: return "subslice"; case IRW_SWAP: return "swap";
        case IRW_TTYRAW: return "tty_raw"; case IRW_TTYREAD: return "tty_read"; case IRW_TTYSIZE: return "tty_size";
        case IRW_TIMENOW: return "time.now"; case IRW_TIMESLEEP: return "time.sleep";
        case IRW_TIMELOCAL: return "time.local";
        case IRW_HASH64: return "hash.bytes"; case IRW_CRC32: return "hash.crc32";
        case IRW_SAMESL: return "slice.same";
        case IRW_RANDBYTES: return "random.bytes"; case IRW_RNGNEXT: return "random.next";
        case IRW_SHA256: return "hash.sha256";
        case IRW_SHA512: return "hash.sha512";
        case IRW_SHA384: return "hash.sha384";
        case IRW_AESCTR: return "crypto.aes_ctr";
        case IRW_GHASH:  return "crypto.ghash";
        case IRW_AESROUND: return "crypto.aes_round"; case IRW_AESLAST: return "crypto.aes_round_last";
        case IRW_CHACHA20: return "crypto.chacha20";
        case IRW_POLY1305: return "crypto.poly1305";
        case IRW_AESGCM:   return "crypto.aes_gcm";
        case IRW_CHAPOLY:  return "crypto.chacha_poly";
        case IRW_WRAP_OK: return "wrap.ok"; case IRW_WRAP_SOME: return "wrap.some"; case IRW_WRAP_NONE: return "wrap.none"; case IRW_WRAP_ERR: return "wrap.err";
        case IRW_LBUF: return "frame.bytes"; case IRW_LBUFC: return "frame.bytes.from"; case IRW_BFILL: return "bytes.fill"; case IRW_TRY: return "try"; case IRW_MAKE: return "make"; case IRW_SCOPY: return "slice.copy";
        case IRW_SNEW: return "stack.new"; case IRW_SPUSH: return "stack.push";
        case IRW_SPOP_INTO: return "stack.pop"; case IRW_BNEW: return "bitset.new";
        case IRW_CONTAINS: return "contains"; case IRW_COUNT: return "count";
        case IRW_BREMOVE: return "set.remove"; case IRW_BUNION: return "set.union";
        case IRW_BINTER: return "set.intersect"; case IRW_BDIFF: return "set.difference";
        case IRW_BCOMPL: return "set.complement"; case IRW_BEMPTY: return "set.is_empty";
        case IRW_BSUBSET: return "set.is_subset";
        case IRW_REF: return "ref.local"; case IRW_MREF: return "mref.local";
        case IRW_DEREF: return "deref"; case IRW_FIELD: return "field";
        case IRW_ARGC: return "args.count"; case IRW_ARGV: return "args.at";
        case IRW_ENVGET: return "env.get"; case IRW_WRITE: return "io.write";
        case IRW_ALLOCB: return "alloc.bytes"; case IRW_FOPEN: return "file.open"; case IRW_FREAD: return "file.read"; case IRW_FSEEK: return "file.seek";
        case IRW_DOPEN: return "dir.open"; case IRW_DREAD: return "dir.read"; case IRW_DCLOSE: return "dir.close";
        case IRW_FTYPE: return "file.type";
        case IRW_DMAKE: return "dir.make"; case IRW_PREMOVE: return "path.remove"; case IRW_PRENAME: return "path.rename";
        case IRW_FWRITE: return "file.write"; case IRW_FCLOSE: return "file.close";
        case IRW_NPAIR: return "net.pair"; case IRW_NSEND: return "net.send"; case IRW_NRECV: return "net.recv"; case IRW_NCLOSE: return "net.close";
        case IRW_NLISTEN: return "net.listen"; case IRW_NPORT: return "net.port"; case IRW_NCONNECT: return "net.connect"; case IRW_NACCEPT: return "net.accept";
        case IRW_NRESOLVE: return "net.resolve";
        case IRW_PSPAWN: return "proc.spawn"; case IRW_PREAD: return "proc.read"; case IRW_PPOLL: return "proc.poll"; case IRW_PWAIT: return "proc.wait"; case IRW_PKILL: return "proc.kill";
        case IRW_RMARK: return "region.mark"; case IRW_RRESET: return "region.reset"; case IRW_RDIN: return "io.read"; case IRW_RNEW: return "io.reactor"; case IRW_RREAD: return "io.rread"; case IRW_RWRITE: return "io.rwrite";
        case IRW_VIEW: return "view"; case IRW_TRYVIEW: return "try.view";
        case IRW_HASVAL: return "has.value";
        case IRW_ISSOME: return "is.some"; case IRW_SOMEVAL: return "some.value";
        case IRW_ISOK: return "is.ok"; case IRW_ISERR: return "is.err";
        case IRW_OKVAL: return "ok.value"; case IRW_ERRVAL: return "err.value";
        case IRW_ENCODE: return "encode";
        case IRW_VARRAY: return "view.array"; case IRW_STR: return "push.str";
        case IRW_BITCAST: return "bit_cast"; case IRW_PANIC: return "panic";
        case IRW_ASM: return "asm";
        case IRW_EXTERN: return "extern.call";
        case IRW_CSTR2STR: return "cstr.scan";
        case IRW_STR2CSTR: return "cstr.of";
        case IRW_FNREF:  return "fn.ref";
        case IRW_ASEND:  return "async.send";
        case IRW_TASEND: return "try.async.send";
        case IRW_DRAIN:  return "drain";
        case IRW_SCHED:  return "schedule";
        case IRW_TSPAWN: return "task.spawn";
        case IRW_YIELD:  return "yield";
        case IRW_CHNEW:  return "channel.new";
        case IRW_CHSEND: return "channel.send";
        case IRW_CHRECV: return "channel.recv";
        case IRW_AWAIT:  return "await";
        case IRW_CANCELSCOPE: return "cancel.scope";
        case IRW_ELEMCK: return "elem.check";
        case IRW_BAND: return "bit.and"; case IRW_BOR: return "bit.or";
        case IRW_BXOR: return "bit.xor"; case IRW_BNOT: return "bit.not";
        case IRW_SHL: return "shl"; case IRW_SHR: return "shr";
        case IRW_WSHL: return "wrap.shl"; case IRW_WSHR: return "wrap.shr";
        case IRW_ROTL: return "rotl"; case IRW_ROTR: return "rotr";
        case IRW_CLMULLO: return "clmul.lo"; case IRW_CLMULHI: return "clmul.hi";
        case IRW_POPCNT: return "count.ones"; case IRW_CLZ: return "leading.zeros";
        case IRW_CTZ: return "trailing.zeros"; case IRW_BSWAP: return "byte.swap";
        case IRW_ALOAD: return "atomic.load"; case IRW_ASTORE: return "atomic.store";
        case IRW_AADD: return "atomic.add"; case IRW_ASUB: return "atomic.sub";
        case IRW_AAND: return "atomic.and"; case IRW_AOR: return "atomic.or";
        case IRW_AXOR: return "atomic.xor"; case IRW_ASWAP: return "atomic.swap";
        case IRW_ACAS: return "atomic.cas"; case IRW_AFENCE: return "atomic.fence";
        case IRW_PREFETCH: return "prefetch";
        case IRW_MMIOBLK: return "mmio.block"; case IRW_RESBLK: return "reserve.block";
        case IRW_ELSE_NONE: return "else.none"; case IRW_ELSE_ERR: return "else.err";
        case IRW_FCONST: return "push.f64"; case IRW_CAST: return "cast";
        case IRW_UNM: return "num.un"; case IRW_BINM: return "num.bin";
        case IRW_ASSERT: return "assert";
        case IRW_SPLAT: return "splat"; case IRW_VLOAD: return "vec.load"; case IRW_VSTORE: return "vector.store";
        case IRW_VLOADM: return "vec.load_masked"; case IRW_VSTOREM: return "vector.store_masked";
        case IRW_VAVG: return "vec.avg";
        case IRW_VALOR: return "value_or"; case IRW_SPOP: return "stack.pop";
        case IRW_VREVERSE: return "vec.reverse"; case IRW_VROTATE: return "vec.rotate"; case IRW_VSHUFFLE: return "vec.shuffle";
        case IRW_SELECT: return "select";
        case IRW_RADD: return "reduce.add"; case IRW_RMUL: return "reduce.mul";
        case IRW_RMIN: return "reduce.min"; case IRW_RMAX: return "reduce.max";
        case IRW_MANY: return "mask.any"; case IRW_MALL: return "mask.all";
    }
    return "?";
}

// ── build / dump / free ───────────────────────────────────────────────────────

// ★ `slice <T>` 의 원소 크기/부동 여부. T 를 못 읽으면 0(= u8 바이트 슬라이스).
//   지금까지 이것을 아무도 안 봤다: `slice u32` 가 **조용히 바이트로 취급**됐다 —
//   len 이 원소 수가 아니라 **바이트 수**를 내고, index 가 **바이트**를 냈다.
//   컴파일되고, 실행되고, **틀린 값을 냈다.** 미구현보다 나쁘다.
// ★ 원소 크기. 스칼라면 폭, **구조체면 레이아웃 크기**(sidx 로 그 구조체를 알려 준다).
 proven_u8 ir_slice_esz_ex(const low_ir_t *ir, const low_cst_t *f, proven_size_t from,
                                 proven_size_t to, bool *eflt, int *sidx, bool *esgn) {
    *eflt = false; if (sidx) *sidx = -1; if (esgn) *esgn = false;
    for (proven_size_t i = from; i < to && i < f->nkids; i++) {
        if (!is_atom(f->kids[i])) continue;
        if (!veq(f->kids[i]->tok.lex, "slice") && !veq(f->kids[i]->tok.lex, "array")) continue;
        if (i + 1 >= to || i + 1 >= f->nkids || !is_atom(f->kids[i + 1])) return 0;
        ityp_t e = ity_of_word(f->kids[i + 1]->tok.lex);
        if (e.known && e.bits) { *eflt = e.flt; if (esgn) *esgn = e.sign; return (proven_u8)(e.bits / 8); }
        if (ir) {   // ★ 구조체 원소
            bool sf; proven_size_t si = ir_struct_find(ir, f->kids[i + 1]->tok.lex, &sf);
            if (sf && ir->structs[si].viewable && ir->structs[si].total &&
                ir->structs[si].total <= 255) {
                if (sidx) *sidx = (int)si;
                return (proven_u8)ir->structs[si].total;
            }
        }
        return 0;
    }
    return 0;
}

 bool ir_type_is_slice(const ir_ctx_t *c, proven_u8str_view_t w) {
    if (veq(w, "slice") || veq(w, "array")) return true;
    for (proven_size_t i = 0; i < c->nalias; i++)
        if (proven_u8str_view_eq(c->slice_alias[i], w)) return true;
    return false;
}

/* ity_lo · ity_hi — low_ir_priv.h */

// ── 계약 진입 검사 (RFC-0008 · RFC-0053 §8-7) ────────────────────────────────
// `requires <cmp> <name> <lit>` 를 op 진입에서 **강제**한다. 위반이면 E-VM-CONTRACT.
//
// 왜 필요한가: 구간 분석이 requires 를 *사실*로 써서 런타임 검사를 제거하는데,
// 그 계약이 아무 데서도 강제되지 않으면 **거짓 사실 위에 최적화가 선다**.
// (실측: requires le a 200 인 u8 op 가 a=255 에서 256 을 반환했다.)
// PRINCIPLES.md §0 의 규칙 그대로 — **검사되지 않는 중복은 거짓말로 썩는다.**
//
// `assume` 등급은 정의상 검사하지 않는다(SPEC-005 §6.2: "검사 안 함, 최적화·문서 전제").
// 따라서 **분석도 그것을 사실로 쓰지 않는다** — 검사 없는 전제로 검사를 제거하면
// UB 를 재도입하는 셈이고, 그것은 P1(UB 없음)의 정면 위반이다.
// ★★★ **빌드 모드 × 등급 처리표** (RFC-0008 §6.5 — *"이 RFC 의 심장"*)
//
//   `build <mode> .` 는 **파싱만 됐고 아무 데서도 강제되지 않았다.** 도구가 그렇게 자백했다:
//     *"a `build` declaration is parsed but enforced NOWHERE."*
//   즉 **모드를 적으면 무언가 달라진다고 믿게 두고, 아무것도 안 했다.**
//
//   등급(RFC-0008 §6.1)은 이미 있다: `static`(기본) · `debug` · `assume`.
//   ⇒ 모드가 **어떤 검사를 남기는지**를 실제로 정한다:
//
//        등급 \ 모드   debug   test   release_safe   release_fast   audit
//        static        CHECK   CHECK  TRAP           **DROP**       CHECK+REPORT
//        debug         CHECK   CHECK  **DROP**       **DROP**       CHECK+REPORT
//        assume        —       —      —              —              —      (언제나 무검사)
//
//   ★ 그리고 **무엇을 지웠는지 반드시 말한다**(`--ir`). 모드가 **조용히** 검사를 지우면
//     그것은 최적화가 아니라 **보증의 소멸**이고, 아무도 못 본다.
/* ir_bmode_t — low_ir_priv.h */

 ir_bmode_t g_bmode = IR_BM_DEBUG;
// ★★★ RFC-0009 D6 — interleaving 오라클의 **스케줄 계획**. `schedule` 이 메시지를 고를 때마다 한
//   **선택**을 한다(그 시점 ready 메시지 중 몇 번째). 계획은 그 선택들의 열이다. 오라클이 이 계획을
//   **DFS 로 체계적으로 열거**한다(B — A 의 K-정준 표본을 대체). 평소·네이티브는 계획이 비어 FIFO.
//     · g_sched_plan[step] = 그 단계에서 고를 인덱스(prefix 만 지시, 나머지는 0=front)
//     · g_sched_sizes[step] = 그 단계의 ready 크기(VM 이 채운다 — DFS 가 backtrack 에 쓴다)
//     · g_sched_step = 실행 중 단계 카운터(매 실행 0 리셋) → 끝나면 그 실행의 총 선택 수
/* SCHED_MAXSTEP — low_ir_priv.h */
bool         g_cancel_scope;   // ★ cancel_on_error 스코프 안인가(자식 오류 → 형제 취소)
proven_u16   g_sched_plan[SCHED_MAXSTEP];
proven_size_t g_sched_plan_len;   // 지시된 prefix 길이(그 뒤는 front)
proven_u16   g_sched_sizes[SCHED_MAXSTEP];
proven_size_t g_sched_step;
 proven_size_t g_bdropped = 0;

proven_size_t low_ir_build_dropped(void) { return g_bdropped; }
bool low_ir_set_build_mode(proven_u8str_view_t m) {
    if      (veq(m, "debug"))         g_bmode = IR_BM_DEBUG;
    else if (veq(m, "test"))          g_bmode = IR_BM_TEST;
    else if (veq(m, "release_safe"))  g_bmode = IR_BM_RELEASE_SAFE;
    else if (veq(m, "release_fast"))  g_bmode = IR_BM_RELEASE_FAST;
    else if (veq(m, "audit"))         g_bmode = IR_BM_AUDIT;
    else return false;
    return true;
}
// 이 등급의 검사를 이 모드에서 **남기는가**.
static bool ir_grade_kept(bool is_debug_grade) {
    if (g_bmode == IR_BM_RELEASE_FAST) return false;                 // 전부 지운다
    if (g_bmode == IR_BM_RELEASE_SAFE) return !is_debug_grade;       // debug 등급만 지운다
    return true;                                                     // debug/test/audit — 남긴다
}

 bool ir_requires_at(const low_cst_t *f, proven_size_t i, proven_size_t *k, bool *is_assume,
                           bool *is_debug) {
    if (!is_atom(f->kids[i]) || !veq(f->kids[i]->tok.lex, "requires")) return false;
    proven_size_t j = i + 1;
    *is_assume = false;
    if (is_debug) *is_debug = false;
    while (j < f->nkids && is_atom(f->kids[j]) &&
           (veq(f->kids[j]->tok.lex, "static") || veq(f->kids[j]->tok.lex, "debug") ||
            veq(f->kids[j]->tok.lex, "assume"))) {
        if (veq(f->kids[j]->tok.lex, "assume")) *is_assume = true;
        if (is_debug && veq(f->kids[j]->tok.lex, "debug")) *is_debug = true;
        j++;
    }
    // ★★★ **피연산자가 원자여야 한다고 못박고 있었다** (2026-07-30).
    //   그래서 `requires ge (len a) (mul n n) .` 은 **여기서 이미 버려졌다** — 술어 낱말(`ge`)까지
    //   읽고도 피연산자가 form 이라는 이유로 "requires 가 아니다" 라고 답했다. 그 결과 도구가
    //   *"requires 로 경계를 닫아 보라"* 고 조언하면서 정작 **그 모양을 읽지 않았다.**
    //   ⇒ 술어 낱말만 원자를 요구한다. 피연산자의 모양은 **읽는 쪽**이 판단한다
    //     (원자만 쓰는 기존 경로는 자기가 다시 확인한다 — 아래 두 호출자 모두 그렇게 고쳤다).
    if (j + 2 >= f->nkids || !is_atom(f->kids[j])) return false;
    *k = j;
    return true;
}
// `ensures [static|debug] <cmp> ret <N>` — requires 와 같은 모양, 좌변이 `ret` 이다.
 bool ir_ensures_at(const low_cst_t *f, proven_size_t i, proven_size_t *k) {
    if (!is_atom(f->kids[i]) || !veq(f->kids[i]->tok.lex, "ensures")) return false;
    proven_size_t j = i + 1;
    while (j < f->nkids && is_atom(f->kids[j]) &&
           (veq(f->kids[j]->tok.lex, "static") || veq(f->kids[j]->tok.lex, "debug"))) j++;
    if (j + 2 >= f->nkids || !is_atom(f->kids[j]) || !is_atom(f->kids[j + 1]) ||
        !is_atom(f->kids[j + 2])) return false;
    if (!veq(f->kids[j + 1]->tok.lex, "ret")) return false;   // 좌변은 반환값이어야 한다
    *k = j;
    return true;
}
// ★★★ **계약 피연산자를 싣는 유일한 답.**
//
//   같은 규칙이 **세 곳**에 있었다(`ir_contract_entry` · `ir_req_emit` · errors 조건) —
//   그리고 **갈렸다**:
//     · `errors bad gt s.w 10 .`   → **강제됐다** (필드 경로를 읽는다)
//     · `requires ge s.w 1 .`      → *"정의되지 않은 이름"* (**오진**)
//     · `requires le s.w k .`      → **조용히 아무 일도 안 했다** (v1 이 리터럴 우변만 봤다)
//   ⇒ **하나로 만든다.** 리터럴 · 지역/파라미터 · **필드 경로** 를 전부 싣는다.
//     못 실으면 **거짓을 반환**하고, 호출자는 **시끄럽게** 거절한다 — 조용히 넘기지 않는다.
 bool ir_contract_operand(ir_ctx_t *c, proven_u8str_view_t v, proven_u32 line) {
    proven_i64 n;
    if (ir_int_lit(v, &n)) { ir_emit(c, IRW_CONST, n); return true; }
    bool glued = false;
    for (proven_size_t z = 0; z < v.size; z++) if (v.ptr[z] == '.') glued = true;
    if (glued) return ir_glued_local(c, v, line);
    bool f2; proven_size_t sl = ir_local_find(c, v, &f2);
    if (!f2) return false;
    ir_emit(c, IRW_LOAD, (proven_i64)sl);
    return true;
}

// ★★★★★ **계약도 `field` 폼을 읽어야 한다** (2026-08-25 — 전위화의 뒤끝).
//   계약 하강은 피연산자를 **낱말**로만 받았다(`ir_contract_operand`). 붙임점
//   시절엔 `s.w` 가 낱말 하나였으니 그것으로 충분했는데, 접근이 **폼**이 되자
//   `requires ge (field s w) 1 .` 이 어느 모양에도 안 맞아 **조용히 건너뛰어졌다** —
//   검사는 초록이고 계약은 없는 상태. ★ 계약이 조용히 사라지는 것은 이 언어가
//   없애려는 바로 그 결함이다(교훈 1). ⇒ 노드를 받는 짝을 둔다.
static bool ir_field_form(const low_cst_t *nd) {
    return nd && nd->kind == LOW_CST_FORM && nd->nkids >= 3 && is_atom(nd->kids[0]) &&
           veq(nd->kids[0]->tok.lex, "field") && is_atom(nd->kids[1]);
}
static const low_cst_t *ir_peel(const low_cst_t *nd) {
    while (nd && nd->kind == LOW_CST_GROUP && nd->nkids == 1) nd = nd->kids[0];
    return nd;
}
// 실을 수 있는가 — **emit 하기 전에** 묻는다(실패한 뒤엔 스택이 이미 더럽다).
static bool ir_contract_loadable(ir_ctx_t *c, const low_cst_t *nd) {
    nd = ir_peel(nd);
    if (!nd) return false;
    if (is_atom(nd)) {
        proven_i64 n;
        if (ir_int_lit(nd->tok.lex, &n)) return true;
        bool f2; (void)ir_local_find(c, nd->tok.lex, &f2);
        return f2;
    }
    if (!ir_field_form(nd)) return false;
    bool f2; (void)ir_local_find(c, nd->kids[1]->tok.lex, &f2);
    if (!f2) return false;
    for (proven_size_t m = 2; m < nd->nkids; m++)
        if (!is_atom(nd->kids[m])) return false;
    return true;
}
static bool ir_contract_node(ir_ctx_t *c, const low_cst_t *nd, proven_u32 line) {
    nd = ir_peel(nd);
    if (!nd) return false;
    if (is_atom(nd)) return ir_contract_operand(c, nd->tok.lex, line);
    if (!ir_field_form(nd)) return false;
    bool f2; proven_size_t sl = ir_local_find(c, nd->kids[1]->tok.lex, &f2);
    if (!f2) return false;
    ir_emit(c, IRW_LOAD, (proven_i64)sl);
    for (proven_size_t m = 2; m < nd->nkids; m++) {
        if (!is_atom(nd->kids[m])) return false;
        proven_i64 ix;
        if (ir_int_lit(nd->kids[m]->tok.lex, &ix))
            { ir_emit(c, IRW_CONST, ix); ir_emit(c, IRW_INDEX, 0); }
        else
            ir_emit(c, IRW_FIELD, (proven_i64)ir_field_intern(c, nd->kids[m]->tok.lex));
    }
    return true;
}
 low_irw_t ir_cmp_word(proven_u8str_view_t op) {
    return veq(op, "lt") ? IRW_LT : veq(op, "le") ? IRW_LE
         : veq(op, "gt") ? IRW_GT : veq(op, "ge") ? IRW_GE
         : veq(op, "eq") ? IRW_EQ : veq(op, "ne") ? IRW_NE : IRW_NOT;
}
// ★★ **명명 계약**(SPEC-002 §267: "contract = 명명 계약 · satisfies = 충족 확인").
//   `contract N do requires … end` 이 선언되고 op 이 `satisfies N .` 이라 적으면 —
//   그 계약은 **이 op 의 계약이다.** 지금까지는 **아무 데서도 강제되지 않았다**:
//   선언하고, 충족한다고 적고, **아무 일도 일어나지 않았다.** 그것이 곧 엔트로피다.
//   (그리고 진입 requires 는 구간 분석이 **사실로 심는다** — 강제하지 않으면 그 사실은
//    아무것도 뒷받침하지 않는다. 교훈 1 그대로다.)
static void ir_req_emit(ir_ctx_t *c, const low_cst_t *const *k, proven_size_t kk) {
    // ★★★ **네 번째 복사본이었다.** 명명 계약(`satisfies N`)의 진입 검사도 **리터럴 우변과
    //   맨 지역만** 실었다 ⇒ 필드 경로(`s.w`)도 관계형(`le a b`)도 **조용히 무시**됐다.
    //   그런데 명명 계약은 **강제되라고** 만든 기능이다. 선언하고, 충족한다고 적고,
    //   **아무 일도 일어나지 않았다** — 정확히 이 언어가 없애려는 그 결함이다.
    //   ⇒ 하나의 낮춤기(`ir_contract_operand`)로 통일한다. 못 실으면 **시끄럽게** 거절한다.
    low_irw_t w = ir_cmp_word(k[kk]->tok.lex);
    if (w == IRW_NOT) return;
    if (!ir_contract_loadable(c, k[kk + 1]) || !ir_contract_loadable(c, k[kk + 2]) ||
        !ir_contract_node(c, k[kk + 1], k[kk + 1]->tok.line) ||
        !ir_contract_node(c, k[kk + 2], k[kk + 2]->tok.line)) {
        ir_fail(c, "E-REQ-UNSUP",
                "this named contract's `requires` names something the tool cannot load here "
                "(an integer literal, a parameter/local, or a field path like `s.w`). It used to be "
                "SKIPPED SILENTLY — and a named contract exists precisely to BE ENFORCED",
                k[kk + 1]->tok.line);
        return;
    }
    ir_emit(c, w, 0);
    ir_emit(c, IRW_ASSERT, 0);
}
// op 이 `satisfies N` 이라 적은 **명명 계약**의 requires 들을 이 op 의 진입 검사로 낸다.
 void ir_named_contracts(ir_ctx_t *c, const low_parse_result_t *pr, const low_cst_t *f) {
    for (proven_size_t i = 0; i + 1 < f->nkids; i++) {
        if (!is_atom(f->kids[i]) || f->kids[i]->tok.kw != LOW_KW_SATISFIES) continue;
        for (proven_size_t r = i + 1; r < f->nkids && is_atom(f->kids[r]) &&
             f->kids[r]->tok.kw == LOW_KW_NONE; r++) {
            proven_u8str_view_t want = f->kids[r]->tok.lex;
            for (proven_size_t q = 0; q < pr->nforms; q++) {
                const low_cst_t *ct = pr->forms[q];
                if (ct->kind != LOW_CST_FORM || ct->nkids < 3 || !is_atom(ct->kids[0])) continue;
                if (!veq(ct->kids[0]->tok.lex, "contract") || !is_atom(ct->kids[1])) continue;
                if (!proven_u8str_view_eq(ct->kids[1]->tok.lex, want)) continue;
                const low_cst_t *blk = ct->kids[ct->nkids - 1];
                if (blk->kind != LOW_CST_BLOCK) continue;
                for (proven_size_t s = 0; s < blk->nkids; s++) {
                    const low_cst_t *rq = blk->kids[s];
                    if (rq->kind != LOW_CST_FORM || rq->nkids < 4 || !is_atom(rq->kids[0])) continue;
                    if (!veq(rq->kids[0]->tok.lex, "requires")) continue;
                    // ★ 피연산자는 낱말일 수도 **폼**(`(field s w)`)일 수도 있다 —
                    //   원자만 통과시키면 `field` 계약이 **조용히 사라진다**.
                    if (!is_atom(rq->kids[1])) continue;
                    ir_req_emit(c, (const low_cst_t *const *)rq->kids, 1);
                }
            }
        }
    }
}
// ★★★ **`add` 사슬은 진입에서 셀 수 있다** (2026-09-07 — 130 자리를 가른 결과).
//   `requires le (add (add slot_bits shard_bits) gen_bits) 64 .`(`lib/budget.low`)는 지금까지
//   **적혀 있고 강제되지 않았다**: 인자가 전부 상수면 부르는 자리의 상수 검사가 잡지만,
//   실행 시각 값으로 들어오면 아무도 안 막았다. 그런데 이 모양은 **셀 수 있다** —
//   피연산자가 지역이거나 리터럴이고, 연산이 덧셈뿐이면 진입에서 그대로 계산하면 된다.
//   ★ 넘침은 **트랩 덧셈**이 잡는다(`IR_TY_KNOWN|64`) — 계약을 세느라 조용히 감싸지 않는다.
//   ☞ *«못 세운다» 와 «안 세웠다» 는 다르다. 가른 뒤에야 그 둘이 갈린다.*
// ★ **먼저 묻고, 그 다음에 낸다.** IR 낱말은 되돌릴 수 없으므로 «실을 수 있나» 를 순수하게
//   판정하는 짝이 필요하다 — 반쯤 낸 채로 실패하면 그 op 의 진입 코드가 망가진다.
static bool ir_addchain_ok(ir_ctx_t *c, const low_cst_t *n, int depth) {
    if (!n || depth > 4) return false;
    while (n->kind == LOW_CST_GROUP && n->nkids == 1) n = n->kids[0];
    if (is_atom(n)) {
        proven_i64 lit; bool fs;
        if (ir_int_lit(n->tok.lex, &lit)) return true;
        (void)ir_local_find(c, n->tok.lex, &fs);
        return fs;
    }
    if (n->kind == LOW_CST_FORM && n->nkids == 3 && is_atom(n->kids[0]) &&
        veq(n->kids[0]->tok.lex, "add"))
        return ir_addchain_ok(c, n->kids[1], depth + 1) && ir_addchain_ok(c, n->kids[2], depth + 1);
    return false;
}

static bool ir_contract_addchain(ir_ctx_t *c, const low_cst_t *n, proven_u32 line, int depth) {
    if (!n || depth > 4) return false;
    while (n->kind == LOW_CST_GROUP && n->nkids == 1) n = n->kids[0];
    if (is_atom(n)) return ir_contract_operand(c, n->tok.lex, line);
    if (n->kind == LOW_CST_FORM && n->nkids == 3 && is_atom(n->kids[0]) &&
        veq(n->kids[0]->tok.lex, "add")) {
        if (!ir_contract_addchain(c, n->kids[1], line, depth + 1)) return false;
        if (!ir_contract_addchain(c, n->kids[2], line, depth + 1)) return false;
        ir_emit(c, IRW_ADD, (proven_i64)IR_TY_KNOWN | 64);   // 넘치면 트랩한다
        return true;
    }
    return false;
}

// ★ 술어 낱말이 **트레이트 이름**인가 — `requires <트레이트> <타입>` 은 비교가 아니라
//   **경계**이고, 타입/트레이트 검사기가 부르는 자리에서 정적으로 강제한다.
//   자격 이름(`allocs.byte_allocator`)으로도 적히므로 **점 뒤만** 떼어 견준다.
static bool ir_names_trait(const low_parse_result_t *pr, proven_u8str_view_t w) {
    proven_u8str_view_t tail = w;
    for (proven_size_t i = 0; i < w.size; i++)
        if (w.ptr[i] == (proven_u8)'.') { tail.ptr = w.ptr + i + 1; tail.size = w.size - i - 1; }
    for (proven_size_t q = 0; q < pr->nforms; q++) {
        const low_cst_t *ct = pr->forms[q];
        if (ct->kind != LOW_CST_FORM || ct->nkids < 2 || !is_atom(ct->kids[0]) ||
            !is_atom(ct->kids[1])) continue;
        if (ct->kids[0]->tok.kw != LOW_KW_TRAIT) continue;
        if (proven_u8str_view_eq(ct->kids[1]->tok.lex, tail)) return true;
    }
    return false;
}

 void ir_contract_entry(ir_ctx_t *c, const low_cst_t *f) {
    // ★★★★★ **`range` 매개변수도 진입에서 선다** (결함 노트 #10, 2026-09-16).
    //
    //   `input p (range u8 0 100) .` 을 `pct 200` 으로 불러도, `pct x`(증명 없는 u8)로 불러도
    //   `--check` 는 통과했고 **실행도 200 을 그대로 돌려줬다.** 그런데 구간 분석은 그 범위를
    //   **사실로 심는다** — 아무도 지키지 않는 것을 사실로 쓰는 것이 정본 교훈 1 이 말하는
    //   바로 그 자리다(경계 검사가 그 «사실» 위에서 지워진다). 폭이 다른 경우는 `E-TYPE-WIDTH`
    //   가 정적으로 잡지만, **폭이 같은 값**(u8 → range u8 0 100)은 아무 층도 보지 않았다.
    //   ⇒ `requires ge p <lo> . requires le p <hi> .` 를 적은 것과 **같은 검사**를 진입에서 낸다.
    {
        low_op_header_t oh = low_op_header(f);
        for (proven_size_t pi = 0; pi < oh.np; pi++) {
            const low_cst_t *tn = (oh.p[pi].core < f->nkids) ? f->kids[oh.p[pi].core] : NULL;
            low_cst_t *const *tk = NULL; proven_size_t tn_n = 0;
            if (tn && tn->kind == LOW_CST_GROUP && tn->nkids == 1 &&
                tn->kids[0]->kind == LOW_CST_FORM) { tk = tn->kids[0]->kids; tn_n = tn->kids[0]->nkids; }
            else { tk = &f->kids[oh.p[pi].core];
                   tn_n = (oh.p[pi].te > oh.p[pi].core) ? oh.p[pi].te - oh.p[pi].core : 0; }
            proven_i64 lo = 0, hi = 0; bool got = false;
            for (proven_size_t w = 0; w + 2 < tn_n; w++) {
                if (!is_atom(tk[w]) || !veq(tk[w]->tok.lex, "range")) continue;
                // `range <lo> <hi>` 또는 `range <바탕타입> <lo> <hi>` — 뒤의 정수 둘이 경계다.
                proven_i64 a, b;
                if (is_atom(tk[w + 1]) && is_atom(tk[w + 2]) &&
                    ir_int_lit(tk[w + 1]->tok.lex, &a) && ir_int_lit(tk[w + 2]->tok.lex, &b)) { lo = a; hi = b; got = true; }
                else if (w + 3 < tn_n && is_atom(tk[w + 2]) && is_atom(tk[w + 3]) &&
                         ir_int_lit(tk[w + 2]->tok.lex, &a) && ir_int_lit(tk[w + 3]->tok.lex, &b)) { lo = a; hi = b; got = true; }
                break;
            }
            if (!got || hi < lo) continue;
            if (!ir_contract_operand(c, oh.p[pi].name, f->line)) continue;
            ir_emit(c, IRW_CONST, lo); ir_emit(c, IRW_GE, 0); ir_emit(c, IRW_ASSERT, 0);
            if (!ir_contract_operand(c, oh.p[pi].name, f->line)) continue;
            ir_emit(c, IRW_CONST, hi); ir_emit(c, IRW_LE, 0); ir_emit(c, IRW_ASSERT, 0);
        }
    }
    for (proven_size_t i = 0; i + 3 < f->nkids; i++) {
        proven_size_t k; bool is_assume, is_debug;
        if (!ir_requires_at(f, i, &k, &is_assume, &is_debug)) continue;
        if (is_assume) continue;                     // assume = 검사 없음(선언 전용)
        // ★★★ **트레이트 경계는 이 자리의 일이 아니다** (2026-09-07 — 130 자리를 가르다 드러남).
        //   `requires allocs.byte_allocator a .` 는 비교가 아니라 **경계**이고,
        //   타입/트레이트 검사기가 부르는 자리에서 **정적으로** 강제한다. 그것을
        //   «강제되지 않는다» 고 경고하면 멀쩡한 계약을 고발하는 것이다 — 첫 판이 그랬고,
        //   코퍼스 130 중 **101** 이 이 부류였다(그리고 단형화가 인스턴스마다 복제했다).
        //   ☞ *경고를 새로 낼 때는 «이미 다른 층이 지키는 것» 을 먼저 빼야 한다.*
        if (c->pr && ir_names_trait(c->pr, f->kids[k]->tok.lex)) continue;
        // ★★★ **`requires ge (len s) (mul p q) .` 의 검사를 실제로 낸다** (2026-07-30).
        //   이 모양은 여기서 조용히 건너뛰어졌다 — 즉 **계약이 문서였고 검사는 없었다.**
        //   그 상태에서 행우선 규칙이 그 계약을 **믿기 시작하면** 곧 메모리 안전 구멍이다:
        //   실제로 VM 의 자기검사(`E-VM-ANALYSIS`)가 그것을 잡아냈다 — 계약을 어긴 호출에서
        //   제거된 경계 검사가 범위 밖을 읽었다. **믿을 것은 검사되는 것뿐이다.**
        //   ⇒ 여기서 `len s` 와 `p*q` 를 실제로 계산해 비교하고 ASSERT 를 낸다.
        if (!is_atom(f->kids[k + 1]) || !is_atom(f->kids[k + 2])) {
            const low_cst_t *L = f->kids[k + 1], *R = f->kids[k + 2];
            while (L && L->kind == LOW_CST_GROUP && L->nkids == 1) L = L->kids[0];
            while (R && R->kind == LOW_CST_GROUP && R->nkids == 1) R = R->kids[0];
            proven_u8str_view_t opw = f->kids[k]->tok.lex;
            // ★ **`field` 폼이 낀 비교는 여기서 곧장 선다** — 아래 특수 모양들은
            //   `len`·`mul` 을 위한 것이라 `field` 는 어디에도 안 맞아 조용히 빠졌다.
            {
                low_irw_t cw = ir_cmp_word(opw);
                if (cw != IRW_NOT && (ir_field_form(ir_peel(L)) || ir_field_form(ir_peel(R))) &&
                    ir_contract_loadable(c, L) && ir_contract_loadable(c, R)) {
                    if (!ir_grade_kept(is_debug)) { g_bdropped++; if (c->dropped_out) (*c->dropped_out)++; continue; }
                    (void)ir_contract_node(c, L, f->kids[k]->tok.line);
                    (void)ir_contract_node(c, R, f->kids[k]->tok.line);
                    ir_emit(c, cw, 0);
                    ir_emit(c, IRW_ASSERT, 0);   // 진입 계약 — 절대 제거하지 않는다
                    continue;
                }
            }
            bool len_form_l = L && L->kind == LOW_CST_FORM && L->nkids == 2 &&
                              is_atom(L->kids[0]) && veq(L->kids[0]->tok.lex, "len") && is_atom(L->kids[1]);
            bool len_lhs = veq(opw, "ge") && len_form_l;
            // ★★★★★ **길이끼리 견주는 절도 세운다** (2026-09-10, RFC-0111 §8-17 · WO-0194).
            //   `requires le (len b) (len prev) .` 는 **사람이 실제로 적는 모양**이다 — 두 배열을
            //   나란히 쓰는 코드(편집 거리의 두 줄, 복사, 병합)가 전부 이렇게 말한다. 그런데
            //   진입 검사가 이 모양을 못 세워 **조용히 버려졌다**(`W-CONTRACT-IGNORED` 갈래 B).
            //   그래서 RFC-0111 §8-15 의 찌르기는 같은 사실을 **리터럴 1500/1501 로 박아야** 했고,
            //   그 판은 픽스처 크기에 매여 실을 수가 없었다.
            //   ☞ *사람이 적는 모양을 도구가 못 받으면, 사람은 도구가 받는 모양으로 거짓말을
            //     적게 된다 — 그리고 그 거짓말은 크기가 바뀌는 날 깨진다.*
            //   ★ 값은 비교 하나다: 양쪽 길이를 밀고 견준다. 순서가 뜻이다(좌, 우).
            {
                bool len_form_r = R && R->kind == LOW_CST_FORM && R->nkids == 2 &&
                                  is_atom(R->kids[0]) && veq(R->kids[0]->tok.lex, "len") &&
                                  is_atom(R->kids[1]);
                low_irw_t cw2 = ir_cmp_word(opw);
                if (len_form_l && len_form_r && cw2 != IRW_NOT) {
                    if (!ir_grade_kept(is_debug)) { g_bdropped++; if (c->dropped_out) (*c->dropped_out)++; continue; }
                    bool f1, f2;
                    proven_size_t s1 = ir_local_find(c, L->kids[1]->tok.lex, &f1);
                    proven_size_t s2 = ir_local_find(c, R->kids[1]->tok.lex, &f2);
                    if (f1 && f2) {
                        ir_emit(c, IRW_LOAD, (proven_i64)s1);
                        ir_emit(c, IRW_LEN, 0);
                        ir_emit(c, IRW_LOAD, (proven_i64)s2);
                        ir_emit(c, IRW_LEN, 0);
                        ir_emit(c, cw2, 0);
                        ir_emit(c, IRW_ASSERT, 0);   // 진입 계약 — 절대 제거하지 않는다
                        continue;
                    }
                }
            }
            // ★★★★★★ **길이의 상계도 검사한다** (2026-09-05, 교훈 1).
            //
            //   여기 있던 규칙들은 길이의 **하계**만 냈다(`ge (len s) N` · `gt (len s) i` ·
            //   `lt i (len s)`). 그런데 구간 분석은 **상계도 사실로 심는다**
            //   (`iv_apply_len_requires`: `lt`/`le` 는 `v->hi` 를 좁히고 `wide = 0` 까지 켠다 —
            //   *"이 상계는 진짜다"*). 그래서 이런 프로그램이 있었다:
            //
            //       fn f input s slice u8 . requires le (len s) 4 . output u8 . do
            //         let n be u8 cast u8 (len s) . .  return mul n 60 .   rem 4*60=240 ≤ 255
            //
            //   분석이 «길이 ≤ 4» 를 믿고 u8 넘침 검사를 지웠는데 **아무도 그 계약을 강제하지
            //   않았다** ⇒ 5 칸짜리를 넣으면 VM 은 `E-VM-ANALYSIS`(자기 분석이 틀렸다고 신고),
            //   **네이티브는 `u8` 자리에 300 을 조용히 냈다.**
            //   ☞ *믿는 코드와 강제하지 않는 코드가 만나면, 심은 사실은 아무것도 뒷받침하지
            //     않고 지운 검사는 되돌아오지 않는다*(PRINCIPLES.md §0 교훈 1).
            //   ⇒ 하계와 **같은 자리에서** 상계도 낸다. 값은 길이 비교 하나다.
            //   (발견: `check-contract-enforced` 를 지은 첫 실행 ·
            //    `docs/known-defects/len-upper-bound-unenforced.md`)
            {
                low_irw_t lw = IRW_NOT;
                const low_cst_t *lenf = NULL, *other = NULL;
                bool len_first = true;
                if (len_form_l && R && is_atom(R) &&
                    (veq(opw, "lt") || veq(opw, "le"))) {           // len s < N · len s ≤ N
                    lenf = L; other = R; len_first = true;
                    lw = veq(opw, "lt") ? IRW_LT : IRW_LE;
                } else if (R && R->kind == LOW_CST_FORM && R->nkids == 2 && is_atom(R->kids[0]) &&
                           veq(R->kids[0]->tok.lex, "len") && is_atom(R->kids[1]) && L && is_atom(L) &&
                           (veq(opw, "gt") || veq(opw, "ge"))) {    // N > len s · N ≥ len s
                    lenf = R; other = L; len_first = false;
                    lw = veq(opw, "gt") ? IRW_GT : IRW_GE;
                }
                if (lw != IRW_NOT) {
                    if (!ir_grade_kept(is_debug)) { g_bdropped++; if (c->dropped_out) (*c->dropped_out)++; continue; }
                    bool fs; proven_size_t s = ir_local_find(c, lenf->kids[1]->tok.lex, &fs);
                    if (fs) {
                        // 순서가 뜻이다 — 비교는 (좌, 우) 를 이 순서로 먹는다.
                        if (len_first) {
                            ir_emit(c, IRW_LOAD, (proven_i64)s);
                            ir_emit(c, IRW_LEN, 0);
                        }
                        if (!ir_contract_operand(c, other->tok.lex, other->tok.line)) {
                            ir_fail(c, "E-REQ-UNSUP", "this length contract names something the tool "
                                    "cannot load here (a parameter/local or an integer literal)",
                                    f->kids[k]->tok.line);
                            continue;
                        }
                        if (!len_first) {
                            ir_emit(c, IRW_LOAD, (proven_i64)s);
                            ir_emit(c, IRW_LEN, 0);
                        }
                        ir_emit(c, lw, 0);
                        ir_emit(c, IRW_ASSERT, 0);   // 진입 계약 — 절대 제거하지 않는다
                        continue;
                    }
                }
            }
            // ★★★★★ **`requires lt i (len s) .` 를 진입에서 검사한다** (RFC-0093, 2026-08-10).
            //   이 검사가 **먼저** 있어야 같은 모양을 사실로 심을 수 있다(iv_apply_rel_requires).
            //   순서를 뒤집어 봤기에 안다: 사실만 심고 검사를 안 뒀더니 **네이티브가 범위 밖을
            //   조용히 읽고 0 을 돌려줬다**(VM 은 E-VM-ANALYSIS 로 자기를 고발했다).
            //   위 주석이 이미 같은 말을 하고 있다 — **믿을 것은 검사되는 것뿐이다.**
            //   ☞ 거울 `gt (len s) i` 도 같은 사실이므로 같이 받는다.
            {
                // ★★ **`le` 도 같은 사실이다** (2026-09-07 — 130 자리를 가르다 나온 둘).
                //   `requires le hi (len a) .` 는 «hi 는 길이를 넘지 않는다» 이고, `lt` 와 **같은
                //   기제**로 선다(비교 낱말 하나만 다르다). 그런데 여기가 `lt`/`gt` 만 받아서
                //   그 절은 **적혀 있고 강제되지 않았다**(vm_chainbound · vm_relchain).
                //   ☞ *한 기제가 두 비교를 다 다룰 수 있으면, 하나만 받는 것은 규율이 아니라 구멍이다.*
                const low_cst_t *nm = NULL, *ln = NULL;
                low_irw_t nw = IRW_LT;
                if (veq(opw, "lt") && is_atom(L) && R && R->kind == LOW_CST_FORM) { nm = L; ln = R; nw = IRW_LT; }
                else if (veq(opw, "le") && is_atom(L) && R && R->kind == LOW_CST_FORM) { nm = L; ln = R; nw = IRW_LE; }
                else if (veq(opw, "gt") && is_atom(R) && L && L->kind == LOW_CST_FORM) { nm = R; ln = L; nw = IRW_LT; }
                else if (veq(opw, "ge") && is_atom(R) && L && L->kind == LOW_CST_FORM) { nm = R; ln = L; nw = IRW_LE; }
                if (nm && ln && ln->nkids == 2 && is_atom(ln->kids[0]) &&
                    veq(ln->kids[0]->tok.lex, "len") && is_atom(ln->kids[1])) {
                    if (!ir_grade_kept(is_debug)) { g_bdropped++; if (c->dropped_out) (*c->dropped_out)++; continue; }
                    bool fs; proven_size_t s = ir_local_find(c, ln->kids[1]->tok.lex, &fs);
                    if (fs) {
                        // 순서가 뜻이다: LT 는 (좌, 우) 를 이 순서로 먹는다 — i < len s.
                        if (!ir_contract_operand(c, nm->tok.lex, nm->tok.line)) {
                            ir_fail(c, "E-REQ-UNSUP", "this `requires lt i (len s)` names something "
                                    "the tool cannot load here", f->kids[k]->tok.line);
                            continue;
                        }
                        ir_emit(c, IRW_LOAD, (proven_i64)s);
                        ir_emit(c, IRW_LEN, 0);
                        ir_emit(c, nw, 0);
                        ir_emit(c, IRW_ASSERT, 0);    // 진입 계약 — 절대 제거하지 않는다
                        continue;
                    }
                }
            }
            // ★★★ **`requires eq (len s) N .` 도 진입에서 세운다** (2026-09-14). 전엔 `ge` 만 받아서
            //   `eq`·`le`·`lt`·`gt`·`ne` 를 적은 절이 **`W-CONTRACT-IGNORED` 로 버려졌다** — 고정 길이 입력
            //   (`array n t`, 정본 §6.2.6)의 «길이는 타입의 일부» 가 바로 이 절로 선다. 거울(`eq N (len s)`)도 받는다.
            {
                low_irw_t cw3 = ir_cmp_word(opw);
                bool len_form_r3 = R && R->kind == LOW_CST_FORM && R->nkids == 2 && is_atom(R->kids[0]) &&
                                   veq(R->kids[0]->tok.lex, "len") && is_atom(R->kids[1]);
                const low_cst_t *LF = NULL, *AT = NULL;
                if (!veq(opw, "ge") && len_form_l && R && is_atom(R)) { LF = L; AT = R; }
                else if ((veq(opw, "eq") || veq(opw, "ne")) && len_form_r3 && L && is_atom(L)) { LF = R; AT = L; }
                if (LF && cw3 != IRW_NOT) {
                    if (!ir_grade_kept(is_debug)) { g_bdropped++; if (c->dropped_out) (*c->dropped_out)++; continue; }
                    bool fs; proven_size_t s = ir_local_find(c, LF->kids[1]->tok.lex, &fs);
                    if (fs) {
                        ir_emit(c, IRW_LOAD, (proven_i64)s);
                        ir_emit(c, IRW_LEN, 0);                            // 좌: len s
                        if (!ir_contract_operand(c, AT->tok.lex, AT->tok.line)) {
                            ir_fail(c, "E-REQ-UNSUP", "this `requires <cmp> (len s) X` names something the "
                                    "tool cannot load here", f->kids[k]->tok.line);
                            continue;
                        }
                        ir_emit(c, cw3, 0);
                        ir_emit(c, IRW_ASSERT, 0);      // 진입 계약 — 절대 제거하지 않는다
                        continue;
                    }
                }
            }
            // ★ **용량 형태**: `requires ge (len s) cap` — 곱이 아니라 지역 하나다.
            //   lru·sched 같은 코드가 정확히 이 모양이다(`j < cap` 로 도는 루프).
            if (len_lhs && R && is_atom(R)) {
                if (!ir_grade_kept(is_debug)) { g_bdropped++; if (c->dropped_out) (*c->dropped_out)++; continue; }
                bool fs; proven_size_t s = ir_local_find(c, L->kids[1]->tok.lex, &fs);
                if (fs) {
                    ir_emit(c, IRW_LOAD, (proven_i64)s);
                    ir_emit(c, IRW_LEN, 0);                            // 좌: len s
                    if (!ir_contract_operand(c, R->tok.lex, R->tok.line)) {
                        ir_fail(c, "E-REQ-UNSUP", "this `requires ge (len s) X` names something the "
                                "tool cannot load here", f->kids[k]->tok.line);
                        continue;
                    }
                    ir_emit(c, IRW_GE, 0);
                    ir_emit(c, IRW_ASSERT, 0);      // 진입 계약 — 절대 제거하지 않는다
                    continue;
                }
            }
            if (len_lhs && R && R->kind == LOW_CST_FORM && R->nkids == 3 &&
                is_atom(R->kids[0]) && veq(R->kids[0]->tok.lex, "mul") &&
                is_atom(R->kids[1]) && is_atom(R->kids[2])) {
                if (!ir_grade_kept(is_debug)) { g_bdropped++; if (c->dropped_out) (*c->dropped_out)++; continue; }
                bool fs; proven_size_t s = ir_local_find(c, L->kids[1]->tok.lex, &fs);
                if (fs) {
                    // ★ 순서가 뜻이다: `ge` 는 (좌, 우) 를 이 순서로 먹는다. 처음에 곱을 먼저 밀어
                    //   `p*q ≥ len s` 를 검사했고 — 반대 방향이라 계약 위반이 안 걸렸다.
                    ir_emit(c, IRW_LOAD, (proven_i64)s);
                    ir_emit(c, IRW_LEN, 0);                              // 좌: len s
                    if (!ir_contract_operand(c, R->kids[1]->tok.lex, R->kids[1]->tok.line) ||
                        !ir_contract_operand(c, R->kids[2]->tok.lex, R->kids[2]->tok.line)) {
                        ir_fail(c, "E-REQ-UNSUP", "this `requires ge (len s) (mul p q)` names "
                                "something the tool cannot load here", f->kids[k]->tok.line);
                        continue;
                    }
                    ir_emit(c, IRW_MUL, (proven_i64)IR_TY_KNOWN | 64);   // 우: p*q — **트랩 곱**이다
                    ir_emit(c, IRW_GE, 0);            // len s ≥ p*q
                    ir_emit(c, IRW_ASSERT, 0);        // 진입 계약 — 절대 제거하지 않는다
                    continue;
                }
                ir_fail(c, "E-REQ-UNSUP", "this `requires ge (len s) (mul p q)` names something the "
                        "tool cannot load here (v1: a parameter/local for s, p, q)",
                        f->kids[k]->tok.line);
                continue;
            }
            // ★ **덧셈 사슬 비교** — `cmp (add …) <이름|리터럴>` 과 그 거울. 셀 수 있으면 센다.
            {
                low_irw_t cw = ir_cmp_word(opw);
                bool l_add = L && L->kind == LOW_CST_FORM && L->nkids == 3 && is_atom(L->kids[0]) &&
                             veq(L->kids[0]->tok.lex, "add");
                bool r_add = R && R->kind == LOW_CST_FORM && R->nkids == 3 && is_atom(R->kids[0]) &&
                             veq(R->kids[0]->tok.lex, "add");
                if (cw != IRW_NOT && (l_add || r_add) &&
                    ir_addchain_ok(c, L, 0) && ir_addchain_ok(c, R, 0)) {
                    if (!ir_grade_kept(is_debug)) { g_bdropped++; if (c->dropped_out) (*c->dropped_out)++; continue; }
                    (void)ir_contract_addchain(c, L, f->kids[k]->tok.line, 0);
                    (void)ir_contract_addchain(c, R, f->kids[k]->tok.line, 0);
                    ir_emit(c, cw, 0);
                    ir_emit(c, IRW_ASSERT, 0);       // 진입 계약 — 절대 제거하지 않는다
                    continue;
                }
            }
            // ★ **그 밖의 중첩 모양** — 진입 검사를 못 낸다. 전에는 여기서 조용히 사라졌다.
            ir_warn_at(c, "W-CONTRACT-IGNORED",
                    "this `requires` has an EXPRESSION in it that the ENTRY check cannot build "
                    "(it knows `cmp <name> <literal>`, `cmp (len s) …`, `elem_*` and field paths). "
                    "A call whose arguments are all CONSTANTS is still refused at the call site "
                    "by the constant contract check — but a RUNTIME value that breaks this clause is "
                    "stopped by NOBODY, and the interval analysis does not learn it either. "
                    "So it is not unsound today; it is a promise the tool keeps only halfway. "
                    "Say it in a `guard` (answer with a value) or write a shape the entry check knows",
                    f->kids[k]->tok.line, f->file);
            continue;
        }
        // ★★★ **처리표** — 모드가 이 등급을 지우면, **지웠다고 센다**(조용히 지우지 않는다).
        if (!ir_grade_kept(is_debug)) { g_bdropped++; if (c->dropped_out) (*c->dropped_out)++; continue; }
        proven_u8str_view_t op = f->kids[k]->tok.lex;
        // ★★★ **배열 내용 술어** — `requires elem_lt s N`(모든 원소 < N). 진입에서 슬라이스를
        //   훑어 검사한다(ELEMCK). 분석은 이걸로 원소 구간을 심는다(iv_apply_requires 참조).
        {
            proven_i64 ecmp = veq(op, "elem_lt") ? 0 : veq(op, "elem_le") ? 1
                            : veq(op, "elem_gt") ? 2 : veq(op, "elem_ge") ? 3 : -1;
            if (ecmp >= 0) {
                if (!ir_contract_operand(c, f->kids[k + 1]->tok.lex, f->kids[k + 1]->tok.line) ||
                    !ir_contract_operand(c, f->kids[k + 2]->tok.lex, f->kids[k + 2]->tok.line)) {
                    ir_fail(c, "E-REQ-UNSUP", "this `requires elem_*` needs a slice parameter and an "
                            "integer literal bound", f->kids[k + 1]->tok.line);
                    continue;
                }
                ir_emit(c, IRW_ELEMCK, ecmp);
                ir_emit(c, IRW_ASSERT, 0);
                continue;
            }
        }
        low_irw_t w = veq(op, "lt") ? IRW_LT : veq(op, "le") ? IRW_LE
                    : veq(op, "gt") ? IRW_GT : veq(op, "ge") ? IRW_GE
                    : veq(op, "eq") ? IRW_EQ : veq(op, "ne") ? IRW_NE : IRW_NOT;
        // ★★★ **트레이트 경계는 이 자리의 일이 아니다** (2026-09-07, 130 자리를 가르다 드러남).
        //   `requires allocs.byte_allocator a .` 는 **피연산자가 하나**인 절 — 비교가 아니라
        //   트레이트 경계이고, **타입/트레이트 검사기가 부르는 자리에서 정적으로** 강제한다.
        //   그것을 «강제되지 않는다» 고 경고하면 **멀쩡한 계약을 고발하는 것**이다.
        //   실제로 첫 판이 그랬다: 130 중 101 이 이 부류(그리고 단형화가 인스턴스마다 복제)였다.
        //   ⇒ 세 번째 자리가 절의 끝(절 낱말이거나 `do`)이면 **피연산자가 하나**다 — 넘어간다.
        //   ☞ *경고를 새로 낼 때는 «이미 다른 층이 지키는 것» 을 먼저 빼야 한다.*
        if (w == IRW_NOT) {                          // 지원하지 않는 술어 — 검사 생략(분석도 안 쓴다)
            ir_warn_at(c, "W-CONTRACT-IGNORED",
                    "this `requires` uses a predicate the ENTRY check does not know "
                    "(it knows lt · le · gt · ge · eq · ne · elem_*). Nothing stops a runtime "
                    "value that breaks it, and the analysis does not learn it either",
                    f->kids[k]->tok.line, f->file);
            continue;
        }
        // ★★★ **피연산자는 하나의 낮춤기로 싣는다** (ir_contract_operand).
        //   같은 규칙이 세 곳에 있었고 **갈렸다**: `errors` 는 필드를 읽었고,
        //   `requires` 는 필드를 **오진**했고, `requires <필드> <변수>` 는 **조용히 죽었다.**
        //   ⇒ 못 실으면 **시끄럽게** 거절한다. **조용히 넘기지 않는다.**
        if (!ir_contract_operand(c, f->kids[k + 1]->tok.lex, f->kids[k + 1]->tok.line) ||
            !ir_contract_operand(c, f->kids[k + 2]->tok.lex, f->kids[k + 2]->tok.line)) {
            ir_fail(c, "E-REQ-UNSUP",
                    "this `requires` names something the tool cannot load here (v1: an integer "
                    "literal, a parameter/local, or a field path like `s.w`). It used to be SKIPPED "
                    "SILENTLY — the clause was there, the check was not, and the interval analysis "
                    "believed it anyway",
                    f->kids[k + 1]->tok.line);
            continue;
        }
        ir_emit(c, w, 0);
        ir_emit(c, IRW_ASSERT, 0);           // 진입 계약 — 절대 제거하지 않는다
    }
}




// ── RFC-0008 §6.4 (미해결 Q6) — 계약에서 테스트를 뽑는다 ─────────────────────
// "requires ge length 2 → 0 1 2 3 을 어떤 알고리즘이 만드는가?"  답: **경계값**이다.
//
// 버그는 가운데가 아니라 **가장자리**에 산다. 그리고 우리 구간 분석이 검사를 제거한
// 근거도 정확히 그 가장자리에 걸려 있다(v.hi ≤ r.lo 같은 부등식). 그러니 계약의 경계를
// 치는 것이 곧 **분석의 주장을 치는 것**이다 — 그래서 이 테스트는 분석의 검증기이기도 하다.
//
// ★ 그리고 계약은 **오라클도 공짜로 준다.** 기대 출력을 적을 필요가 없다:
//     구간 안의 값 → 계약 위반으로 트랩하면 **안 된다**(제 계약이 허용한 입력을 거부한 것)
//     구간 밖의 값 → 계약 위반으로 **반드시** 트랩해야 한다(안 하면 검사가 잘못 제거됐다)
//   ⇒ 생성기가 **자기 검증**된다. 이것이 PRINCIPLES.md §0 의 규칙 그대로다:
//     "의도적 중복(계약)은 기계가 교차 검사해야 한다."

#define CT_MAXV 8
// 구간 [lo,hi] 의 경계값. 안쪽 3개(lo, mid, hi) + 바깥 2개(lo-1, hi+1).
static proven_size_t ct_values(proven_i64 lo, proven_i64 hi, proven_i64 *out) {
    proven_size_t n = 0;
    if (lo > INT64_MIN) out[n++] = lo - 1;                       // ★ 바깥 — 반드시 트랩
    out[n++] = lo;                                               // 안쪽 경계
    if (lo + 1 <= hi) out[n++] = lo + 1;
    if (hi - 1 > lo + 1) out[n++] = hi - 1;
    if (hi > lo) out[n++] = hi;                                  // 안쪽 경계
    if (hi < INT64_MAX) out[n++] = hi + 1;                       // ★ 바깥 — 반드시 트랩
    return n;
}

static bool ct_diag_has(const proven_array_t *diags, proven_size_t from, const char *code) {
    for (proven_size_t i = from; i < diags->len; i++)
        if (strcmp(PROVEN_ARRAY_GET(diags, low_diag_t, i)->code, code) == 0) return true;
    return false;
}
// ★★ **누구의 계약인가**를 가려야 한다.
//   오라클의 규칙("계약이 거부한 입력은 트랩해야 하고, 허용한 입력은 트랩하면 안 된다")은
//   **이 op 자신의 진입 계약**에 대한 것이다. 그런데 본문 안에서도 계약 트랩이 난다:
//     · 피호출자의 진입 검사 — 증명 못 한 값을 넘긴 것(RFC-0055 D3: **정상 프로그램**이다)
//     · ensures · errors…when
//   그것들을 "내 계약이 스스로 모순" 이라고 세면 **정상 프로그램이 실패로 잡힌다.**
//   ⇒ **진입 계약의 진단만** 본다(경계 검사 · requires 진입 검사).
static bool ct_entry_contract_trap(const proven_array_t *diags, proven_size_t from) {
    for (proven_size_t i = from; i < diags->len; i++) {
        const low_diag_t *d = PROVEN_ARRAY_GET(diags, low_diag_t, i);
        if (strcmp(d->code, "E-VM-CONTRACT") != 0) continue;
        if (strstr(low_diag_text(d), "at the program boundary") || strstr(low_diag_text(d), "violated at entry"))
            return true;
    }
    return false;
}

// ★★ X-0041 ⓐ — 이 op 이 망·파일 잎에 **닿는가**(직접, 또는 부르는·띄우는 op 을 거쳐).
//   memo: 0 모름 · 1 보는 중(되부름 고리 — 닿지 않는 쪽으로 친다, 고리의 다른 자리가 답한다) · 2 아니다 · 3 닿는다.
static bool ct_irw_touches_world(low_irw_t w) {
    switch (w) {
    case IRW_FOPEN: case IRW_FREAD: case IRW_FWRITE: case IRW_FCLOSE: case IRW_FSEEK:
    case IRW_DOPEN: case IRW_DREAD: case IRW_DCLOSE: case IRW_FTYPE: case IRW_LTYPE:
    case IRW_DMAKE: case IRW_PREMOVE: case IRW_PRENAME:
    case IRW_NPAIR: case IRW_NSEND: case IRW_NRECV: case IRW_NCLOSE: case IRW_NLISTEN:
    case IRW_NPORT: case IRW_NCONNECT: case IRW_NACCEPT: case IRW_NRESOLVE:
    case IRW_PSPAWN: case IRW_PREAD: case IRW_PPOLL: case IRW_PWAIT: case IRW_PKILL:   // ★ 프로그램을 띄운다(RFC-0136)
        return true;
    default:
        return false;
    }
}
static bool ct_reaches_world(const low_ir_t *ir, proven_size_t di, proven_u8 *memo) {
    if (di >= ir->ndefs) return false;
    if (memo[di] >= 2) return memo[di] == 3;
    if (memo[di] == 1) return false;
    memo[di] = 1;
    bool hit = false;
    const low_ir_def_t *d = &ir->defs[di];
    for (proven_size_t k = 0; k < d->ncode && !hit; k++) {
        const low_ir_ins_t *in = &d->code[k];
        if (ct_irw_touches_world(in->w)) hit = true;
        else if (in->w == IRW_CALL) hit = ct_reaches_world(ir, IR_CALL_IDX(in->a), memo);
        else if (in->w == IRW_ASEND || in->w == IRW_TSPAWN) hit = ct_reaches_world(ir, (proven_size_t)in->a, memo);
    }
    memo[di] = hit ? 3 : 2;
    return hit;
}

low_ir_ctest_t low_ir_contract_tests(const low_ir_t *ir, proven_allocator_t work, bool verbose) {
    // ★★★ **오라클은 멈춰야 한다.** 반복 횟수가 입력인 op 에 경계값을 먹이면 그 실행은 끝나지
    //   않는다(실측: `--ir` 가 60초를 넘겨도 안 끝났다 — `--check` 는 정상). 도구가 멈추면
    //   그것은 도구의 결함이다. ⇒ 예산을 켜고, 걸린 케이스는 **건너뛰며 센다**.
    //   ★★★★ **2천만 → 2백만** (2026-08-14, 실측). 예산은 *"정직한 케이스가 끝나는 자리"* 여야
    //   하는데, 값이 크면 **끝나지 않을 케이스를 끝까지 태우는 값**이 된다: `vm_soak.low` 하나가
    //   그렇게 **15.7 초**를 썼다(반복 횟수가 입력인 op 14 개 × 2천만 걸음). 그리고 그 시간은
    //   전부 *"이건 안 끝난다"* 를 알아내는 데만 쓰인다.
    //   ⇒ 픽스처 **235 개 전부**에 대고 20M · 5M · 2M · 1M · 500k 를 재 봤다: 건너뛴 수 ·
    //     본문 트랩 · 오라클 실패 · 케이스 수가 **한 자리도 안 바뀐다**(주장 손실 0).
    //     500k 까지 안전한 것을 확인하고 **4 배 여유**를 둬 2M 을 쓴다. 픽스처 훑기 35 → 7 초.
    //   ★ 그리고 이 여유는 **게이트가 지킨다**: 정직한 케이스가 실제로 쓴 최대 걸음을 세어
    //     예산에 너무 가까워지면(= 조용히 건너뛸 참이면) 골든이 운다.
    low_ir_set_run_budget(low_oracle_budget());
    low_ir_reset_steps_max_ok();
    proven_u64 budget_hits0 = low_ir_run_budget_hits();
    low_ir_ctest_t r = { 0 };
    proven_array_t diags = PROVEN_ARRAY_INIT(work, low_diag_t, 8).value;
    // ★ X-0041 ⓐ — 바깥 세계에 닿는 op 을 먼저 가려 둔다(아래 세 갈래가 모두 건너뛴다).
    proven_u8 *world = NULL;
    if (ir->ndefs) {
        proven_result_array_t wr = PROVEN_ARRAY_INIT(work, proven_u8, ir->ndefs);
        if (wr.err == PROVEN_OK && wr.value.cap >= ir->ndefs) {
            world = (proven_u8 *)wr.value.data;
            memset(world, 0, ir->ndefs);
            for (proven_size_t q = 0; q < ir->ndefs; q++) (void)ct_reaches_world(ir, q, world);
            for (proven_size_t q = 0; q < ir->ndefs; q++)
                if (world[q] == 3 && ir->defs[q].lowered && !ir->defs[q].is_test) r.world_skips++;
        }
    }
#define CT_SKIP_WORLD(di_) (world && world[(di_)] == 3)

    // ── (A) 슬라이스를 받는 op — 계약이 말하는 **길이**를 흔든다 ──────────────
    // `errors E when lt (len data) 4` 는 길이 4 를 경계로 지목한다. 그러면 3·4·5 를 친다.
    // ★ 오라클은 **when 조건 그 자체**다: 그 길이에서 op 이 그 오류를 내는가?
    //   그리고 어떤 길이에서든 **E-VM-CONTRACT / E-VM-ANALYSIS 가 나오면 안 된다** —
    //   나온다면 errors 절이 거짓말을 했거나(선언 ≠ 본문), 분석이 잘못 제거한 것이다.
    for (proven_size_t di = 0; di < ir->ndefs; di++) {
        const low_ir_def_t *d = &ir->defs[di];
        if (!d->lowered) continue;
        if (CT_SKIP_WORLD(di)) continue;
        if (d->nparams != 1 || d->param_slice != 1u) continue;   // 슬라이스 하나짜리 op
        // ★★★ 예전엔 계약이 `len …` 으로 **상수를 언급한** op 만 뽑았다(nlenk > 0).
        //   그러면 슬라이스를 받는 대부분의 op 이 **길이를 한 번도 흔들어 보지 못한다.**
        //   그런데 **빈 슬라이스**야말로 경계다 — 그리고 관계 분석(`i < len(s)`)이 경계 검사를
        //   지우는 자리이기도 하다. 지운 검사가 틀리면 **VM 이 스스로를 고발한다**(E-VM-ANALYSIS).
        //   ⇒ 계약이 말한 길이 **더하기** 0·1·2·3·4·8 을 **언제나** 흔든다.
        //     (길이는 계약이 말해 주지 않아도 **타입이 말한다**: 슬라이스는 0 부터다.)
        // ★★★ **원소 크기를 맞춘다** (2026-07-31). 길이는 **바이트 수**로 주는데, 원소가
        //   u8 이 아니면(예: `slice u64`) 바이트 길이가 원소 크기의 배수여야 슬라이스가 선다.
        //   안 맞으면 `view_array` 가 트랩하고 — 그것은 **계약이 거절한 것이 아니다.**
        //   ⇒ 그때 오라클은 *"거부해야 하는데 통과했다"* 로 세고 **맞는 프로그램을 고발했다.**
        //   실제로 그렇게 됐다: `lib/spsc.low` 의 `spsc_capacity`(slice u64 + `requires ge (len buf) 2`)가
        //   길이 1·3 에서 2 건 실패로 잡혔다. **오라클이 틀렸고 프로그램은 옳았다.**
        //   ★ 이런 결함은 조합이 있어야 드러난다: `slice u64` **와** 길이 `requires` 가 함께 있어야
        //     한다(둘 중 하나만이면 이 갈래를 안 탄다). 그래서 오늘까지 안 보였다.
        //   ⇒ 원소 폭을 알면 그 **배수**로 흔든다. 모르면(255) 예전대로 둔다.
        proven_i64 esz = 1;
        if (d->nparams >= 1 && d->param_ebits[0] != 255 && d->param_ebits[0] >= 8)
            esz = (proven_i64)(d->param_ebits[0] / 8);
        if (esz < 1) esz = 1;
        // ★★ **자르면 세어 말한다** (RFC-0077 P1-5 감사, 2026-08-14). 넘친 길이 케이스는 그냥
        //   안 만들어졌다 — 그러면 **덜 검사하고도 조용하다**. 예산 초과는 이미 세어 말하는데
        //   이쪽만 침묵하는 것은 같은 오라클 안의 두 규율이다. ⇒ 센다(요약이 말한다).
        proven_i64 lens[32]; proven_size_t nlen = 0; proven_size_t lens_dropped = 0;
        static const proven_i64 BASE[] = { 0, 1, 2, 3, 4, 8 };
        for (proven_size_t q = 0; q < sizeof BASE / sizeof BASE[0]; q++) {
            if (nlen < 32) lens[nlen++] = BASE[q] * esz; else lens_dropped++;
        }
        for (proven_u8 ki = 0; ki < d->nlenk; ki++)
            for (proven_i64 delta = -1; delta <= 1; delta++) {
                if (nlen >= 32) { lens_dropped++; continue; }
                proven_i64 L = (d->lenk[ki] + delta) * esz;
                bool dup = false;
                for (proven_size_t q = 0; q < nlen; q++) if (lens[q] == L) dup = true;
                if (!dup && L >= 0 && L <= 64) { if (nlen < 32) lens[nlen++] = L; else lens_dropped++; }
            }
        r.len_dropped += lens_dropped;   // ★ 표가 차서 못 만든 케이스를 **센다**(조용하지 않게)
        r.ops++;
        proven_size_t op_cases = 0, op_fail = 0;
        {
            for (proven_size_t li = 0; li < nlen; li++) {
                proven_i64 len = lens[li];
                if (len < 0 || len > 64) continue;
                proven_i64 args[64];
                // ★ 내용은 `1+q`(version 1 등 정상값). 단, `requires elem_lt s N` 처럼 원소 구간이
                //   좁혀진 슬라이스면 **그 구간 안으로 클램프**한다 — 안 그러면 도구가 계약을
                //   어기는 바이트를 먹여 놓고 트랩을 프로그램 탓으로 고발한다(PRINCIPLES.md §0).
                bool erng = (d->param_elem_rng >> 0) & 1u;
                proven_i64 elo = erng ? d->param_elo[0] : 0, ehi = erng ? d->param_ehi[0] : 0;
                for (proven_i64 q = 0; q < len; q++) {
                    proven_i64 v = 1 + q;
                    if (erng) { if (v < elo) v = elo; if (v > ehi) v = ehi; }
                    args[q] = v;
                }
                proven_size_t base = diags.len;
                (void)low_ir_run(ir, d->name, args, (proven_size_t)len, work, &diags);
                op_cases++; r.cases++; r.len_cases++;
                // ★ 계약 위반이나 분석 오류가 나오면 **실패**다. 정직한 op 은 어떤 길이에서도
                //   자기 선언을 어기지 않는다 — 짧으면 선언한 오류를 낼 뿐이다.
                bool ctrap = ct_diag_has(&diags, base, "E-VM-CONTRACT");
                bool abug   = ct_diag_has(&diags, base, "E-VM-ANALYSIS");
                // ★ 계약은 **원소 수**로 말하고 `len` 은 **바이트 수**다 — 나눠서 견준다.
                //   (원소 폭을 반영하고도 이 줄을 안 고쳐 `slice u64` 에서 1 건이 남았다.
                //    한 자리를 고치면 **짝이 되는 자리**가 있는지 늘 물어야 한다.)
                // ★ 계약이 허용하는 길이인가 — **하한과 상한 둘 다**(상한 0 = 없음).
                bool admitted_len = (len / esz) >= d->minlen &&
                                    (d->maxlen == 0 || (len / esz) <= d->maxlen);
                if (abug) { op_fail++; r.failures++; }                    // 분석 오류는 언제나 실패
                else if (admitted_len && ctrap) { op_fail++; r.failures++; }   // 허용했는데 제 검사가 거부
                else if (!admitted_len && !ctrap) { op_fail++; r.failures++; } // 거부해야 하는데 통과

            }
        }
        if (verbose && op_cases)
            printf("   %-16.*s %3zu length case(s)%s\n", (int)d->name.size,
                   (const char *)d->name.ptr, op_cases,
                   op_fail ? "   ★ ORACLE FAILURE" : "   all oracles hold");
    }

    // ── (B) 정수 파라미터 — 계약이 말하는 **값의 경계**를 흔든다 ────────────────
    for (proven_size_t di = 0; di < ir->ndefs; di++) {
        const low_ir_def_t *d = &ir->defs[di];
        if (!d->lowered || d->nparams == 0 || d->nparams > 3 || d->param_slice) continue;
        if (CT_SKIP_WORLD(di)) continue;

        // ★★★ 예전엔 계약이 **타입보다 좁아야만** 뽑았다. "타입만 있는 파라미터는 바깥이
        //   뭔지 계약이 말해 주지 않는다" 고 적어 두고 건너뛰었다.
        //   **그것이 틀렸다.** 방금 확인했다(차등 퍼저가 `wide(-1)` 로 찾았다):
        //   **타입의 범위도 계약이다.** `u8` 은 [0,255] 라고 말하고, 구간 분석은 그것을
        //   **사실로 믿고 검사를 지운다.** 그러니 `u8` 의 바깥은 **−1 과 256** 이다 —
        //   계약이 분명히 말해 준다.
        //   ⇒ **정수 파라미터가 있는 모든 op** 에서 경계값을 뽑는다. 오라클은 그대로다:
        //     **안이면 트랩하면 안 되고, 밖이면 트랩해야 한다.**
        // ★ **actor 핸들러는 인스턴스 없이 부를 수 없다.** 그 인스턴스는 오라클이 만들 수 있는
        //   값이 아니다(spawn 해야 나온다). 그래서 **건너뛴다** — 그리고 `skipped` 에 **센다.**
        //   조용히 넘기면 "62 op 을 다 쟀다" 는 말이 거짓말이 된다(도구는 자기가 확인한 것만
        //   주장한다). 메시지가 인자를 싣게 되자마자 오라클이 이 핸들러들을 **인스턴스 자리에
        //   아무 값이나 넣고** 부르기 시작했고, 6 건을 고발했다 — 오라클이 옳았다.
        if (d->is_actor) { r.skipped++; continue; }
        // ★★★ **VM 은 C 를 못 부른다** ⇒ 계약 오라클도 이 op 을 못 돌린다.
        //   그리고 그것은 **전파된다**: extern 을 부르는 op 도 VM 에서는 거기서 멈춘다.
        //   조용히 빼지 않는다: **센다.** (계약은 여전히 **네이티브 경계에서** 검사된다 —
        //   그것이 RFC-0063 D5 의 요점이다.)
        {
            bool reaches_c = d->is_extern;
            for (proven_size_t k = 0; k < d->ncode && !reaches_c; k++) {
                if (d->code[k].w != IRW_CALL) continue;
                proven_size_t cc = IR_CALL_IDX(d->code[k].a);
                if (cc < ir->ndefs && ir->defs[cc].is_extern) reaches_c = true;
            }
            if (reaches_c) { r.skipped++; continue; }
        }
        bool derivable = false;
        for (proven_size_t p = 0; p < d->nparams && p < LOW_MAX_PARAMS; p++)
            if (d->ptest[p].has_rng || d->ptype[p].has_rng) derivable = true;
        for (proven_size_t p = 0; p < d->nparams && p < LOW_MAX_PARAMS; p++)
            if ((d->param_struct >> p) & 1u) derivable = true;   // ★ 구조체도 흔들 축이 있다
        if (!derivable) { r.skipped++; continue; }
        // ★★★ **못 만드는 인자가 있으면 건너뛴다 — 그리고 그렇게 말한다.**
        //
        //   값 축은 **스칼라(proven_i64)만** 만든다. 구조체·슬라이스 파라미터에는 `0`/`1` 을
        //   넣고 있었고 — VM 은 그것을 **쓰레기 핸들**로 읽고 트랩했다. 그러면 오라클이
        //   *"계약이 감당 못 할 입력을 허용한다"* 고 **프로그램을 고발했다.** **오진이다.**
        //
        //   ★ 왜 이제야 나왔나: **구조체 + 스칼라를 함께 받는 op 이 하나도 없었다**
        //     (`rect.scaled_area input s rect . input k u8 .` 가 처음이다). 교훈 6 —
        //     **픽스처에 없는 모양은 아무도 검사하지 않는다.** 그리고 그 자리에서 나온 결함은
        //     이번에도 **조용히 틀린 답**(오진)이었다.
        //
        //   ⇒ **도구가 못 하는 것을 프로그램의 죄로 돌리지 않는다**(PRINCIPLES.md §0).
        //   (도구는 파라미터의 **모양**을 이미 안다 — `--ops` 가 t/s/f/i 로 찍는다.)
        // ★★★ **구조체 인자를 이제 만든다** (2026-07-14).
        //   전엔 스칼라만 만들 수 있어서 구조체를 받는 op 을 **통째로 건너뛰었다** —
        //   즉 **그 자리의 계약은 아무도 검증하지 않았다.** 도구는 `param_sidx` 와
        //   구조체의 `total`(바이트 크기)를 **이미 알고 있었다.** 쓰기만 하면 됐다.
        //   경계 바이트를 흔든다: **0 · 최대(0xFF) · 중간**. (슬라이스는 **길이 축**이 따로 있다.)
        bool constructible = true;
        for (proven_size_t p = 0; p < d->nparams && p < LOW_MAX_PARAMS; p++)
            if ((d->param_slice >> p) & 1u) constructible = false;
        // ★★★ **레이아웃이 없는 구조체 파라미터도 못 만든다** (2026-07-20).
        //
        //   구조체 인자는 경계에서 **바이트를 view 해서** 만든다(위 vm_call 참조 —
        //   `!st->viewable` 이면 E-VM-VIEW). 그런데 구조체 필드가 이제 **슬라이스를 담을 수
        //   있게 되면서**(같은 날), 그런 구조체는 **바이트 레이아웃이 아예 없다.**
        //   ⇒ 오라클이 그것을 만들려다 정당하게 트랩했고, 그 트랩을
        //     *"계약이 감당 못 할 입력을 허용한다"* 로 세어 **프로그램을 고발했다. 오진이다.**
        //
        //   ★ 바로 위 주석이 **같은 오진을 한 번 겪고 적어 둔 것**이다(2026-07-14, 구조체+스칼라).
        //     같은 함정을 같은 자리에서 두 번째로 밟았다 — 새 능력이 늘 때마다
        //     *"이 인자를 만들 수 있는가"* 를 **다시 물어야** 한다는 뜻이다.
        //   ⇒ **도구가 못 하는 것을 프로그램의 죄로 돌리지 않는다**(PRINCIPLES.md §0).
        for (proven_size_t p = 0; p < d->nparams && p < LOW_MAX_PARAMS; p++)
            if (((d->param_struct >> p) & 1u) && (proven_size_t)d->param_sidx[p] < ir->nstructs &&
                !ir->structs[d->param_sidx[p]].viewable) constructible = false;
        if (!constructible) { r.unbuildable++; continue; }   // ★ **다른 이유다. 다르게 센다.**
        // ★★★ **빌드 모드가 이 op 의 검사를 지웠다** — 그러면 오라클은 그것을 **검증할 수 없다.**
        //   그리고 그것은 **프로그램의 죄가 아니다**: 모드가 그렇게 하라고 했다.
        //   오라클은 **계약**을 검증하지 **빌드**를 검증하지 않는다.
        //   ⇒ **고발하지 않는다. 못 한다고 말한다.** (교훈 5 — 오진이 최악이다.)
        if (d->ndropped) { r.mode_dropped++; continue; }
        r.ops++;

        proven_i64 vals[3][CT_MAXV];
        proven_size_t nv[3];
        for (proven_size_t p = 0; p < d->nparams; p++) {
            if ((d->param_struct >> p) & 1u) {          // ★ 구조체 — 경계 **바이트**를 흔든다
                vals[p][0] = 0x00; vals[p][1] = 0xFF; vals[p][2] = 0x01; nv[p] = 3;
                continue;
            }
            // 계약이 좁히면 그 경계를, 아니면 **타입의 경계**를 쓴다(둘 다 계약이다).
            if (d->ptest[p].has_rng) nv[p] = ct_values(d->ptest[p].rlo, d->ptest[p].rhi, vals[p]);
            else if (d->ptype[p].has_rng) nv[p] = ct_values(d->ptype[p].rlo, d->ptype[p].rhi, vals[p]);
            else { vals[p][0] = 0; vals[p][1] = 1; nv[p] = 2; }
        }

        proven_size_t op_cases = 0, op_fail = 0;
        proven_size_t i0 = 0, i1 = 0, i2 = 0;
        do { do { do {
            proven_i64 args[3];
            proven_size_t idx[3] = { i0, i1, i2 };
            bool admitted = true;
            for (proven_size_t p = 0; p < d->nparams; p++) {
                args[p] = vals[p][idx[p]];
                if (d->ptest[p].has_rng &&
                    (args[p] < d->ptest[p].rlo || args[p] > d->ptest[p].rhi)) admitted = false;
            }
            // ★ 관계형 계약도 **인정 조건**이다 — `requires le a b .` 를 어기는 입력은 적법하지 않다.
            // ★★★★★ **구조체가 낀 관계는 못 센다** (2026-08-30, WO-0151 — known-defect
            //   `oracle-cross-struct-relation`). `args[p]` 는 구조체를 **채운 씨앗 바이트**이지
            //   `field a id` 가 아니다. 그 둘을 비교하면 **엉뚱한 양**을 비교하는 것이고,
            //   그러면 인정/거절 판정이 어긋나 멀쩡한 계약이 실패로 셈된다.
            //   ⇒ 그런 절은 **판정에서 빼고**, 그 op 의 어긋남은 실패가 아니라 못 잰 것이다.
            //   ☞ 손해는 이미 났다: `lib/shard.low` 의 `rejoin` 이 이 고발 때문에 두 계약을
            //     **지웠다**(2026-08-28 주석이 그렇게 적는다). 도구가 프로그램을 바꿨다.
            // ★★★ **금지된 점을 «허용» 에서 뺀다** (2026-09-07). `requires ne m 0 .` 은 구간에
            //   못 실리므로 여기서 따로 본다 — 안 그러면 0 이 «허용된 입력» 으로 들어가고,
            //   진입 계약이 그것을 무는 것을 이 오라클이 **실패로 고발한다**(강제는 옳은데
            //   분류가 틀린 것이다. 실제로 `wire` 를 지을 때 그 고발을 받았다).
            for (proven_u8 q = 0; q < d->npne; q++) {
                proven_size_t pp = (proven_size_t)d->pne[q].p;
                if (pp >= d->nparams || ((d->param_struct >> pp) & 1u)) continue;
                if (args[pp] == d->pne[q].v) admitted = false;
            }
            // ★★★ **덧셈 사슬 계약을 허용 판정에 넣는다** — 강제와 같은 사실이어야 한다.
            for (proven_u8 q = 0; q < d->npsum; q++) {
                // ★★★ **넘침도 거절이다** (2026-09-07, 첫 판이 여기서 걸렸다).
                //   진입 검사의 덧셈은 **트랩 덧셈**이다 — `slot_bits + shard_bits` 가 넘치면
                //   그 자리에서 멈춘다. 오라클이 그것을 모르고 «합이 작다» 고 계산하면
                //   (넘쳐서 작아 보인다) 그 입력을 «허용» 이라 부르고, op 이 옳게 멈추는 것을
                //   **결함으로 고발한다**(실제로 `gen_of` 에서 그렇게 났다).
                //   ☞ *계약을 흉내 낼 때는 그 계약이 **어떻게 계산하는지**까지 흉내 내야 한다.*
                proven_u64 sum = (proven_u64)d->psum[q].k;
                bool usable = true, over = false;
                for (proven_u8 z = 0; z < d->psum[q].n; z++) {
                    proven_size_t pp = (proven_size_t)d->psum[q].p[z];
                    if (pp >= d->nparams || ((d->param_struct >> pp) & 1u)) { usable = false; break; }
                    proven_u64 add = (proven_u64)args[pp];
                    if (sum + add < sum) over = true;
                    sum += add;
                }
                if (!usable) continue;
                if (over) { admitted = false; continue; }      // 넘치는 입력은 계약 밖이다
                proven_u64 b = (proven_u64)d->psum[q].bound;
                bool holds = d->psum[q].cmp == 0 ? sum <  b : d->psum[q].cmp == 1 ? sum <= b
                           : d->psum[q].cmp == 2 ? sum >  b : d->psum[q].cmp == 3 ? sum >= b
                           : d->psum[q].cmp == 4 ? sum == b : sum != b;
                if (!holds) admitted = false;
            }
            bool rel_unmodelled = false;
            for (proven_u8 q = 0; q < d->nprel; q++) {
                proven_size_t pa = (proven_size_t)d->prel[q].a, pb = (proven_size_t)d->prel[q].b;
                if (pa >= d->nparams || pb >= d->nparams) continue;
                if (((d->param_struct >> pa) & 1u) || ((d->param_struct >> pb) & 1u)) {
                    rel_unmodelled = true; continue;      // 씨앗 값은 필드가 아니다
                }
                if (d->prel[q].strict ? !(args[pa] <  args[pb])
                                      : !(args[pa] <= args[pb])) admitted = false;
            }
            proven_size_t base = diags.len;
            // ★ 구조체 파라미터는 **바이트 슬라이스**로 넘긴다(호출자 소유). 나머지는 스칼라.
            low_ir_arg_t sa[LOW_MAX_PARAMS];
            static proven_u8 sbuf[LOW_MAX_PARAMS][64];
            for (proven_size_t p = 0; p < d->nparams && p < LOW_MAX_PARAMS; p++) {
                if ((d->param_struct >> p) & 1u) {
                    proven_size_t si = d->param_sidx[p];
                    proven_size_t n = (si < ir->nstructs) ? ir->structs[si].total : 1;
                    if (n == 0 || n > 64) n = 1;
                    for (proven_size_t b = 0; b < n; b++) sbuf[p][b] = (proven_u8)args[p];
                    sa[p] = (low_ir_arg_t){ .is_slice = true, .bytes = sbuf[p], .n = n };
                } else {
                    sa[p] = (low_ir_arg_t){ .is_slice = false, .v = args[p] };
                }
            }
            low_ir_run_result_t res = low_ir_run_argv(ir, d->name, sa, d->nparams, work, &diags, NULL, 0);
            (void)res;
            op_cases++; r.cases++;
            if (admitted) r.admitted++; else r.rejected++;

            bool contract_trap = ct_entry_contract_trap(&diags, base);   // ★ **내** 진입 계약만
            bool analysis_bug  = ct_diag_has(&diags, base, "E-VM-ANALYSIS");

            if (analysis_bug) {                    // 언제나 실패 — 분석이 거짓 제거를 했다
                op_fail++; r.failures++;
            } else if (rel_unmodelled && (contract_trap != admitted)) {
                // ★ 관계 절을 못 세운 op — 인정과 트랩이 어긋나도 그것은 **오라클의 무지**다.
                r.unmodelled++;
            } else if (admitted && contract_trap && d->param_struct) {
                // ★ 구조체 인자는 **모든 바이트가 같은 값**이다 — 필드별로 못 흔든다.
                //   그런 입력을 op 의 계약이 거절하는 것은 **오라클의 한계**이지
                //   프로그램의 잘못이 아니다. 실패가 아니라 **못 잰 것**으로 센다.
                r.unmodelled++;
            } else if (admitted && contract_trap) {
                // 제 계약이 허용한 입력을 제 계약 검사가 거부했다 — 계약이 스스로 모순이다
                op_fail++; r.failures++;
            } else if (!admitted && !contract_trap) {
                // ★ 계약이 거부해야 할 입력이 그냥 통과했다 — **검사가 잘못 제거됐다**
                op_fail++; r.failures++;
            } else if (admitted && !res.ok) {
                r.admitted_traps++;                // 경고: 계약은 허용하는데 본문이 못 견딘다
            }
        } while (d->nparams > 2 && ++i2 < nv[2]);
           i2 = 0;
        } while (d->nparams > 1 && ++i1 < nv[1]);
           i1 = 0;
        } while (++i0 < nv[0]);

        if (verbose) {
            printf("   %-16.*s %3zu cases", (int)d->name.size, (const char *)d->name.ptr, op_cases);
            if (op_fail) printf("   ★ %zu ORACLE FAILURE(S)", op_fail);
            else         printf("   all oracles hold");
            printf("\n");
        }
    }
    proven_array_destroy(&diags);

    // ★★★ **모양 축** — 파라미터를 **하나씩** 경계로 흔든다(나머지는 기준값).
    //   값 축(슬라이스 없는 op)과 길이 축(슬라이스 하나)이 못 덮는 모양을 덮는다:
    //   **슬라이스 둘**(쓰기 op) · 정수+슬라이스 혼합 · 구조체 파라미터.
    //   오라클은 둘이다:
    //     · **E-VM-ANALYSIS 가 나오면 언제나 실패다** — 제거된 검사가 틀렸다는 뜻이고,
    //       네이티브는 그것을 **진짜로 지운다**(거기선 아무도 못 잡는 UB).
    //     · **진입 계약 트랩은 계약 밖 입력에서만** 나야 한다.
    {
        static const proven_i64 SH_INT[] = { 0, 1, 255, 256, -1, 127, 128, 65535 };
        static const proven_size_t SH_LEN[] = { 0, 1, 2, 3, 4, 8 };
        alignas(64) static proven_u8 sbuf[4][64];
        for (proven_size_t di = 0; di < ir->ndefs; di++) {
            const low_ir_def_t *d = &ir->defs[di];
            if (!d->lowered || d->is_test || d->nparams == 0 || d->nparams > 4) continue;
            if (CT_SKIP_WORLD(di)) continue;
            proven_u32 nslice = 0;
            for (proven_size_t p = 0; p < d->nparams; p++)
                if (((d->param_slice | d->param_struct) >> p) & 1u) nslice++;
            if (nslice < 2) continue;      // 값 축·길이 축이 이미 덮는다
            r.ops++;
            proven_size_t op_fail = 0, op_cases = 0;
            for (proven_size_t vary = 0; vary < d->nparams; vary++) {
                proven_size_t nsteps = (((d->param_slice | d->param_struct) >> vary) & 1u)
                                         ? sizeof SH_LEN / sizeof SH_LEN[0]
                                         : sizeof SH_INT / sizeof SH_INT[0];
                for (proven_size_t step = 0; step < nsteps; step++) {
                    low_ir_arg_t a[LOW_MAX_PARAMS]; proven_size_t sb = 0;
                    bool bad_arg = false;
                    for (proven_size_t p = 0; p < d->nparams; p++) {
                        bool is_s = ((d->param_slice | d->param_struct) >> p) & 1u;
                        if (is_s) {
                            proven_size_t len = (p == vary) ? SH_LEN[step] : 8;
                            if (sb >= 4) { bad_arg = true; break; }
                            for (proven_size_t q = 0; q < len && q < 64; q++)
                                sbuf[sb][q] = (proven_u8)(1 + q);
                            a[p] = (low_ir_arg_t){ .is_slice = true, .bytes = sbuf[sb], .n = len };
                            sb++;
                        } else if ((d->param_flt >> p) & 1u) {
                            a[p] = (low_ir_arg_t){ .is_flt = true, .v = 0 };   // 0.0 의 비트
                        } else {
                            proven_i64 v = (p == vary) ? SH_INT[step] : 1;
                            a[p] = (low_ir_arg_t){ .v = v };
                        }
                    }
                    if (bad_arg) continue;
                    proven_size_t base = diags.len;
                    (void)low_ir_run_argv(ir, d->name, a, d->nparams, work, &diags, NULL, 0);
                    op_cases++; r.cases++; r.shape_cases++;
                    if (ct_diag_has(&diags, base, "E-VM-ANALYSIS")) { op_fail++; r.failures++; }
                }
            }
            if (verbose && op_cases)
                printf("   %.*s: %zu shape case(s)%s\n", (int)d->name.size,
                       (const char *)d->name.ptr, op_cases,
                       op_fail ? "   ★ ORACLE FAILURE" : "   all oracles hold");
        }
    }

    r.budget_skips = (proven_size_t)(low_ir_run_budget_hits() - budget_hits0);
    r.steps_max_ok = low_ir_run_steps_max_ok();
    r.step_budget  = low_oracle_budget();
    low_ir_set_run_budget(0);          // ★ 사용자 실행은 다시 무한이다
#undef CT_SKIP_WORLD
    return r;
}
