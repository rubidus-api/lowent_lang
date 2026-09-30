// low_check.c — MVP static effect discipline (S4).
#include "low_hwm.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "low_ir.h"
#include "low_typecheck.h"
#include "low_check.h"

#include "low_token.h"
#include "low_diag.h"

// Effect atoms (MVP subset of the RFC-0006 lattice).
// ★★★ RFC-0057 (채택 2026-07-13, DECISION-0009) — **의미 없는 효과 낱말은 거짓 보증이다.**
//   어휘는 13종을 받는데 **비트는 셋뿐**이었고, 그나마 **추론되는 것은 io 하나**였다:
//   `alloc`·`state` 는 비트만 있고 **아무것도 그것을 추론하지 않았다.**
//   ⇒ `effects none` 인데 `panic` 을 해도, `stack_new` 로 할당해도 **아무도 못 잡았다.**
//     선언은 있고 강제는 없다 — 교훈 1 그대로다.
//   이제 **원시어가 있는 효과는 전부 추론된다.** (원시어가 아직 없는 효과 —
//    wait·lock·atomic·device 등 — 는 위반할 방법이 없으므로 선언만 받는다.)
enum { EFF_NONE = 0, EFF_IO = 1u, EFF_ALLOC = 2u, EFF_STATE = 4u,
       EFF_PANIC = 8u, EFF_UNSAFE = 16u, EFF_ATOMIC = 32u,
       // ★★★ **`concurrent`** (RFC-0022 D-A · RFC-0071 A5) — 완결이 **다른 태스크의 진행에
       //   의존**한다. `wait` 와 다르다: `wait` 는 커널·하드웨어가 깨워 주므로 **어떤 executor
       //   로도** 만족되지만, `concurrent` 는 **동료를 돌릴 수 있는 executor** 가 있어야 한다.
       //       await read conn buf .   커널이 깨운다. 아무도 안 도와도 언젠가 온다.  → wait
       //       chrecv ch .             송신 태스크가 돌아야 온다. 아무도 안 도우면 영원히. → concurrent
       EFF_CONCURRENT = 64u,
       // ★★★ **`wait`** (RFC-0083 L1) — 이제 격자의 **일급 원자**다(전엔 "선언만·비트 없음"). 그래야
       //   폐포 `concurrent ⊇ wait` 를 표현한다: concurrent 를 가지면 **자동으로** wait 도 가진다
       //   (concurrent = "동료가 돌아야" 는 wait = "언젠가 깨어남" 을 **포함**한다). RFC-0077 §P1-2 해소.
       EFF_WAIT = 128u,
       // ★★★★ **`heap`** (RFC-0112 D2 · WO-0211) — 뿌리가 **실행 중에 자랄 수 있다**. 호스티드 전용.
       //   `alloc` 과 갈라 둔 까닭: 프리스탠딩도 **고정 창에서 깎는 일**(alloc)은 한다. 못 하는 것은
       //   *자라는 것* 하나다 — 그래서 막는 것도 그 하나여야 한다(전엔 alloc 전체를 막았다, F3).
       EFF_HEAP = 256u,
       // ★★★★ **원시어가 없는 원자도 격자의 자리를 갖는다** (결함 노트 #85, 2026-09-16).
       //   전엔 이 여섯이 `EFF_NONE` 으로 접혔다 — 그러면 `proc … effects blocking .` 을 **순수한
       //   `fn` 이 불러도 통과한다**: 선언은 받아 놓고 전파는 하지 않았다. 특히 `device`(장치를
       //   직접 건드린다)가 순수 함수 뒤에 숨었다. 비트를 주면 «부른 것의 효과는 부르는 것의
       //   효과에 든다»(§7.1)가 이 여섯에도 선다.
       //   ★ 추론할 원시어가 없으므로 "선언했는데 안 한다"(W-EFFECT-OVER)에서는 뺀다 —
       //     그 경고는 «몸이 그것을 하는가» 를 묻는데, 물을 몸이 없다.
       EFF_LOCK = 512u, EFF_DEVICE = 1024u, EFF_PAGEFAULT = 2048u,
       EFF_BLOCKING = 4096u, EFF_CANCEL = 8192u, EFF_DETACH = 16384u };
#define EFF_NOPRIM (EFF_LOCK | EFF_DEVICE | EFF_PAGEFAULT | EFF_BLOCKING | EFF_CANCEL | EFF_DETACH)
// ★★★ **폐포 연산자** close(S) = S ∪ {wait | concurrent ∈ S} (RFC-0083 §5·§1.4).
//   확대(S⊆close(S))·멱등·단조를 만족하는 closure operator. concurrent 를 가지면 wait 을 파생한다 —
//   포함관계를 **한 곳에서만** 강제(평평한 원자 표는 그대로). join 뒤·비교 전에 적용한다.
static unsigned eff_close(unsigned e) {
    if (e & EFF_CONCURRENT) e |= EFF_WAIT;
    return e;
}

typedef struct {
    proven_u8str_view_t name;
    bool                is_calc;    // fn → declares purity
    bool                explicit_eff; // proc 이 effects 절을 **명시**했나(W-EFFECT-OVER 판정용)
    unsigned            declared;   // declared effect set
    const low_cst_t    *body;       // op body block (or NULL)
    const low_cst_t    *form;       // ★ op form 전체 — 파라미터의 소유성을 보려면 필요하다
    unsigned            self_declared; // ★ 자기검사용 declared(핸들러 panic 함의 포함) — 호출 전파엔 안 씀
    bool                is_handler; // ★ actor 핸들러인가 (state 필드 접근·panic 규율이 다르다)
    const low_cst_t    *actor_blk;  // ★ 핸들러면 담은 actor 의 BLOCK(상태 필드 이름 판별용)
    proven_u8str_view_t mod;        // ★ **선언 모듈** — 한정된 호출을 이 표에서 가릴 때 쓴다
} low_opinfo_t;

// ★★★ **인자 자리라고 다 이동이 아니다 — 파라미터의 타입이 정한다** (2026-07-19).
//   SPEC-004 §4.8: *"copy/move 는 타입+access 로 자동 분류: `ref`·read slice·원시값=copy,
//   `owned`·`mut_ref`·mut slice=move-only."* 그런데 검사기는 **인자 자리이기만 하면 이동**이라
//   했다 ⇒ 비소유 파라미터로 **읽기만** 넘겨도 E-OWN-MOVED 였다(과엄격).
//   ★ 그래서 파일 핸들처럼 *"여러 번 읽고 마지막에 닫는다"* 는 가장 흔한 모양이
//     표현 불가능했다 — A6 에서 라이터가 겪은 것과 **같은 벽**이다.
//   ⇒ 피호출자의 그 파라미터가 `owned`(또는 `mut`/`mut_ref` — 배타 접근)일 때만 이동이다.
static bool ck_param_moves(const low_opinfo_t *tab, proven_size_t nt,
                           proven_u8str_view_t opname, proven_size_t argi) {
    for (proven_size_t i = 0; i < nt; i++) {
        if (!proven_u8str_view_eq(tab[i].name, opname)) continue;
        if (!tab[i].form) return true;              // 모르면 **보수적으로** 이동이라 본다
        low_op_header_t h = low_op_header(tab[i].form);
        if (argi >= h.np) return true;              // 자리를 못 세면 보수적으로
        return h.p[argi].is_owned || h.p[argi].is_mut;
    }
    return true;                                    // 표에 없으면 보수적으로
}

static bool veq(proven_u8str_view_t v, const char *s) {
    return proven_u8str_view_eq(v, proven_u8str_view_from_cstr(s));
}
static bool ck_clause_word(proven_u8str_view_t v);   // ★ 절 경계 — decl_effect 가 쓴다
// ★ 지금 검사하는 단위 — `via <타입>` 이 그 타입의 op 들을 찾고, actor 의 권한 칸을 찾는 데 쓴다.
static const low_parse_result_t *g_ck_pr;
static bool ck_atom(const low_cst_t *n);
static unsigned ck_via_effects(proven_u8str_view_t ty);
static unsigned effect_of_word(proven_u8str_view_t v) {
    if (veq(v, "io")) return EFF_IO;
    if (veq(v, "concurrent")) return EFF_CONCURRENT;
    if (veq(v, "alloc")) return EFF_ALLOC;
    if (veq(v, "heap")) return EFF_HEAP;
    if (veq(v, "state")) return EFF_STATE;
    if (veq(v, "panic")) return EFF_PANIC;
    if (veq(v, "unsafe")) return EFF_UNSAFE;
    if (veq(v, "atomic")) return EFF_ATOMIC;   // ★ 이제 원시어가 있다(atomic_* op, RFC-0018)
    if (veq(v, "wait")) return EFF_WAIT;        // ★ RFC-0083 L1 — wait 도 이제 격자 일급 원자(폐포 대상)
    // 나머지 어휘(lock·device·page_fault·blocking·cancel·detach)는
    // **아직 원시어가 없다** — 위반할 방법이 없으므로 선언만 받는다(과대근사는 안전하다).
    // ★ 원시어가 없는 여섯도 **자리를 갖는다**(EFF_NOPRIM) — 추론은 못 해도 전파는 한다.
    if (veq(v, "lock")) return EFF_LOCK;
    if (veq(v, "device")) return EFF_DEVICE;
    if (veq(v, "page_fault")) return EFF_PAGEFAULT;
    if (veq(v, "blocking")) return EFF_BLOCKING;
    if (veq(v, "cancel")) return EFF_CANCEL;
    if (veq(v, "detach")) return EFF_DETACH;
    return EFF_NONE;  // "none" or an effect with no primitive yet
}
// ★ effect 어휘는 **닫혀 있다**(SPEC-002 §부록 K — 13 평면 atom + 바닥 `none`).
//   그런데 effect_of_word 는 **모르는 낱말을 조용히 none 으로 삼켰다.**
//   `effects io` 를 `effects lo` 로 오타 내면 op 이 **순수로 선언된다** —
//   선언이 조용히 뜻을 바꾼다. 이름이 아무것도 안 가리키는데 아무도 안 봤다.
/* ★★★★★ **효과 낱말과 등급표를 한 자리에서 낸다** (2026-09-03, 소유자 결정 A).
 *
 *   그전에는 같은 집합이 **두 번** 적혀 있었다: 여기가 열다섯을 알고 아래 `CK_T*` 가
 *   열넷을 담았다 — `concurrent` 가 빠져 있었다. 그래서 `effects concurrent` 를 쓰는
 *   op 은 **등급을 선언하지 않았을 때만** 섰고, 가장 넉넉한 `t3` 에서도 거절됐다.
 *   정본 §10.8 이 t3 를 *"곧 전부"* 라 부른 것이 **참이 아니었다.**
 *
 *   ⇒ 낱말을 X 매크로 하나로 모으고 등급표는 **그 목록에서 골라 만든다.** 낱말이
 *     늘면 어느 등급이 감당하는지 **여기서 정해야** 하므로 한쪽만 자라지 못한다.
 *   ☞ *두 곳에 적힌 같은 집합은 언젠가 갈린다 — 갈리지 않게 하는 유일한 길은
 *     한 곳에서 내는 것이다.*
 *
 *   뒤의 수는 **그것을 감당하는 가장 낮은 등급**이다. `concurrent` 가 2 인 까닭:
 *   「완결이 다른 흐름의 진행에 달린다」는 성질은 `blocking`·`lock` 과 같은 자리다. */
#define LOW_EFFECT_WORDS(X)                                                   \
    X("none", 0)  X("unsafe", 0)  X("panic", 0)  X("state", 0)                \
    X("wait", 0)  X("cancel", 0)                                              \
    X("io", 1)    X("device", 1)                                              \
    X("alloc", 2) X("lock", 2)    X("atomic", 2)  X("blocking", 2)            \
    X("heap", 3)                                                              \
    X("concurrent", 2)                                                        \
    X("page_fault", 3) X("detach", 3)

static bool effect_word_known(proven_u8str_view_t v) {
#define LOW_EFF_CHECK(w, tier) if (veq(v, w)) return true;
    LOW_EFFECT_WORDS(LOW_EFF_CHECK)
#undef LOW_EFF_CHECK
    return false;
}
// io-performing prelude/builtin names
static unsigned builtin_effect(proven_u8str_view_t v) {
    if (veq(v, "write_out"))                              return EFF_IO;   // ★ RFC-0069 A2 — 실제 출력
    if (veq(v, "read_in"))                                return EFF_IO;   // ★ A5 — 표준입력
    // ★★★★ **reactor 세 리프도 io 다** (2026-08-22, N4 단계1 에서 드러났다).
    //   `r_write` 를 넣고 `--check` 를 돌리자 **W-EFFECT-OVER** 가 났다: *"io 를 선언했는데
    //   안 한다"*. 그런데 그 프로그램은 stdout 에 쓴다 — **표가 낡았던 것**이고,
    //   `reactor_new`·`r_read` 도 **처음부터 빠져 있었다.**
    //   ☞ 효과가 안 세어지면 **순수 op 이 reactor 를 돌릴 수 있다** — effect 절이 거짓말이 된다.
    //     새 리프가 옛 구멍을 드러낸 자리다(RFC-0007: 선언과 행동이 어긋나면 둘 다 거짓).
    if (veq(v, "reactor_new") || veq(v, "r_read") || veq(v, "r_write"))
        return EFF_IO;
    if (veq(v, "file_open") || veq(v, "file_read") ||                      // ★ §6 스트림 리프
        veq(v, "file_write") || veq(v, "file_close") ||
        veq(v, "file_seek") ||                                             // ★ §4 랜덤 접근
        veq(v, "dir_open") || veq(v, "dir_read") || veq(v, "dir_close") || // ★ §6 디렉터리 순회
        veq(v, "file_type") || veq(v, "link_type") ||                       // ★ §6 파일 타입 질의(뒤엣것은 lstat)
        veq(v, "dir_make") || veq(v, "path_remove") || veq(v, "path_rename")) // ★ §6 파일시스템 변경
        return EFF_IO;
    // ★★★★ **여기 일곱 이름이 있었고 그 일곱은 존재하지 않았다** (2026-08-31, 소유자 결정).
    //   `print` · `run` · `write_file` · `capture` · `read_line` · `env` · `exit` —
    //   전부 `E-IR-UNDEF` 다. arity 표에도 부록 D 에도 없다. 2026-07-03 의 첫 효과 규율이
    //   그때 쓰던 이름을 적었고, op 들이 `write_out`·`file_write`·`env_get` 으로 자리를 잡는
    //   동안 이 줄만 남았다.
    //
    //   ★ **두 단계가 같은 이름에 다른 답을 했다**: 검사는 `E-EFFECT`(*"io 를 내는데 선언
    //     안 했다"*), 하강은 `E-IR-UNDEF`(*"그런 이름이 없다"*). 뒤쪽이 옳다.
    //   ★★ 실측으로 이 표가 **실제로 하는 일**을 쟀더니 하나뿐이었다 — **없는 이름 `print`
    //     를 예약하는 것.** 실재 io op(`write_out`·`file_write`·`env_get`·`alloc_bytes`·
    //     `panic`)은 **전부 다른 경로**가 이미 예약한다(`E-NAME-BUILTIN`, 지운 뒤에도 그대로).
    //     곧 이 표는 언어에 없는 낱말 하나를 지역 이름으로 못 쓰게 막고 있었다.
    //   ⇒ 지웠다. 이제 `print a .` 는 한 목소리로 «그런 이름이 없다» 이고, `var print u64` 는
    //     쓸 수 있다.
    //   ☞ *죽은 표는 틀린 표보다 낫지 않다. 틀린 표는 언젠가 물리지만, 죽은 표는 조용히
    //     사람에게 없는 규칙을 지키게 한다.*
    if (veq(v, "net_pair") || veq(v, "net_send") ||                        // ★ 소켓 리프 (cap net)
        veq(v, "net_recv") || veq(v, "net_close") ||
        veq(v, "net_listen") || veq(v, "net_port") ||                      // ★ 네트워크 면
        veq(v, "net_connect") || veq(v, "net_accept") ||
        veq(v, "net_resolve"))   // ★ 이름 해석도 바깥에 닿는다 (X-0032)
        return EFF_IO;
    // ★ RFC-0057 — 원시어가 있는 효과는 **전부** 추론한다. 안 하면 선언이 장식이다.
    if (veq(v, "stack_new") || veq(v, "alloc_bytes"))     return EFF_ALLOC;   // ★ A3 — 바이트를 얻는다
    if (veq(v, "panic"))                                  return EFF_PANIC;
    if (veq(v, "raw") || veq(v, "addr") || veq(v, "mmio")) return EFF_UNSAFE;
    // ★★★ **cstr → str 는 생 포인터를 읽는다** (RFC-0068 S4) — 세상에 닿지는 않지만(syscall 아님)
    //   C 가 준 포인터를 역참조하므로 unsafe 다. cstr 를 얻으려면 이미 `cap c` 를 거쳤고(extern),
    //   스캔 자체는 그 위의 O(n) 비용이다 — 그래서 마킹(unsafe)이 정직한 최소다(mmio 와 같은 규율).
    if (veq(v, "str_from_cstr"))                          return EFF_UNSAFE;
    // ★★★ **str_buf → cstr 도 unsafe** (RFC-0068 S4 · C4 · D4) — 널종단을 **호출자가 보증**한다.
    //   이것이 D4 "str 뷰 → cstr 거절"의 기계적 강제다: 순수 fn 은 이 효과를 못 내므로
    //   **안전한 str→cstr 은 아예 없다**(E-EFFECT-CALC). 봉인하는 유일한 경로는 str_buf 다.
    if (veq(v, "cstr_of"))                                return EFF_UNSAFE;
    if (veq(v, "spawn") || veq(v, "send"))                return EFF_STATE;   // actor 상태를 만진다
    // ★★★ **채널은 동료를 요구한다** — 그것이 `concurrent` 의 정의다(RFC-0022 D-A).
    //   빈 채널의 `chrecv` 는 **누군가 보내야** 풀리고, 찬 채널의 `chsend` 는 **누군가 받아야** 풀린다.
    //   ⇒ 커널이 깨워 주는 `wait` 와 **다른 요구**이고, 그래서 다른 낱말이다.
    if (veq(v, "chrecv") || veq(v, "chsend"))             return EFF_CONCURRENT;
    // ★★★★ **자는 것은 기다리는 것이다** (2026-09-06, 소유자 결정 ②).
    //   `time_sleep` 은 정본 §7.1 의 `wait`("기다린다. 다른 누구의 도움 없이도 언젠가
    //   깨어난다")에 정확히 들어맞는다. 여태 이 잎은 아무 효과도 안 냈고, `clock.sleep_ms`
    //   가 그 자리를 `io` 로 적고 있었다 — 그런데 `io` 는 *"바깥과 자료를 주고받는다"* 이고
    //   자는 것은 자료를 주고받지 않는다. ⇒ 잎이 **맞는 원자**를 낸다.
    //   ☞ *어떤 효과인지 모를 때 가장 가까운 큰 것을 적으면, 그 계약은 부르는 쪽에게
    //     없는 비용을 물린다 — 순수한 호출자가 시계를 못 쓰게 된다.*
    if (veq(v, "time_sleep"))                             return EFF_WAIT;
    // ★★★ **level-3 atomic** (RFC-0018 G1) — atomic op 은 `atomic` 효과를 방출한다.
    //   방출하는데 선언 안 하면 오류다(효과가 장식이 아니려면). op 이름이 `atomic_` 로 시작한다.
    if (v.size >= 7 && memcmp(v.ptr, "atomic_", 7) == 0) return EFF_ATOMIC;
    return EFF_NONE;
}
// ★★★ **효과를 내는 이름도 예약된 이름이다** (2026-07-19, 여섯 번째 진짜 프로그램이 찾았다).
//   `low_ir_is_builtin_name` 은 arity 표 + IR 예약 목록만 본다. 그런데 위 `builtin_effect` 의
//   낱말들(raw·addr·mmio·print·run·env·exit …)은 **op 표에 없으면서 효과를 낸다**. 그래서
//   그 이름을 파라미터나 지역으로 쓰면 **효과 추론이 조용히 오염**됐다:
//
//       fn f input raw u64 . output u64 . effects none . do return add raw 1 . end
//         → E-EFFECT-CALC: "fn performs an effect (declared pure)"
//
//   ★ 이것이 **오진**이다(교훈 5 — 오진이 최악이다): 진짜 이유는 순수성이 아니라 *이름이 두
//     뜻을 갖는다* 인데, 진단은 엉뚱한 곳을 가리켜 프로그래머를 순수성 문제로 몰고 간다.
//     `max` 는 arity 표에 있어 E-NAME-BUILTIN 으로 정확히 잡히는데 `raw` 는 빠져나갔다 —
//     **같은 죄에 두 개의 진단**이었고 그중 하나가 거짓말이었다.
static bool ck_effect_word(proven_u8str_view_t v) { return builtin_effect(v) != EFF_NONE; }
// ★★ **비트마다 이름이 있어야 한다** (2026-09-06). 이 함수가 아는 여섯 밖의 비트
//   (`concurrent`·`wait`)는 «none» 으로 떨어졌다 — 그리고 그 «none» 이 경고 문장에 실리자
//   *"선언했는데 안 하는 효과는 none 이다"* 라는 **뜻 없는 말**이 나왔다(코퍼스 4 건).
//   ☞ *이름을 모르는 자리는 «없다» 가 아니라 «모른다» 라고 말해야 한다.*
static const char *effect_name(unsigned e) {
    if (e & EFF_ATOMIC) return "atomic";
    if (e & EFF_IO) return "io";
    if (e & EFF_ALLOC) return "alloc";
    if (e & EFF_HEAP) return "heap";
    if (e & EFF_STATE) return "state";
    if (e & EFF_PANIC) return "panic";
    if (e & EFF_UNSAFE) return "unsafe";
    if (e & EFF_CONCURRENT) return "concurrent";
    if (e & EFF_WAIT) return "wait";
    return e ? "(이름 없는 효과 비트)" : "none";
}

// Declared effect from an op's flat clauses: scan for an `effects <word>` pair.
// *found is set false if there is no effects clause (declaration absent).
// ★★★ RFC-0057 — `effects` 절은 **원자의 목록**이다(SPEC-006: `effects 원자* .`).
//   그런데 **첫 낱말 하나만** 읽고 있었다: `effects io alloc` 이 **io 로만** 등록됐다.
//   ⇒ 두 번째 원자부터는 **선언해도 없는 것과 같았다** — 그러면 그 op 이 alloc 을 해도
//     "선언 안 한 효과" 로 잡힌다(거짓 양성)거나, 반대로 놓친다. **절 전체를 읽는다.**
static unsigned decl_effect(const low_cst_t *op, bool *found) {
    unsigned e = EFF_NONE;
    *found = false;
    for (proven_size_t i = 1; i + 1 < op->nkids; i++) {
        if (op->kids[i]->kind != LOW_CST_ATOM || !veq(op->kids[i]->tok.lex, "effects")) continue;
        *found = true;
        for (proven_size_t j = i + 1; j < op->nkids; j++) {
            if (op->kids[j]->kind != LOW_CST_ATOM) break;
            if (ck_clause_word(op->kids[j]->tok.lex)) break;      // 다음 절
            // ★★★★ RFC-0112 D7 — `via <타입>`: 그 타입이 **할당 계열 효과**를 더 적었으면 그것까지 선언이다.
            if (veq(op->kids[j]->tok.lex, "via") && j + 1 < op->nkids && op->kids[j + 1]->kind == LOW_CST_ATOM) {
                e |= ck_via_effects(op->kids[j + 1]->tok.lex);
                j++;
                continue;
            }
            e |= effect_of_word(op->kids[j]->tok.lex);
        }
        return e;
    }
    return EFF_NONE;
}

// ★ 한정된 호출이면 그 모듈의 항목만 읽는다(위 ck_launder_walk 와 같은 규율). `qmod.size == 0`
//   이면 bare 호출이므로 전과 같이 첫 일치를 쓴다.
// ★★★★★ **맨이름은 제 모듈부터 찾는다** — 하강이 2026-08-07 에 배운 것을(`ir_def_find_in`)
//   검사층도 배운다(2026-08-14). 전엔 이름만 맞춰 **첫 정의**를 집었고, 그래서 두 모듈이 같은
//   이름을 export 하면 **남의 효과**를 읽었다: `file.open`(effects io)과 `growvec.open`
//   (effects state)이 한 단위에 있으면, growvec 안의 맨이름 `open` 호출이 io 를 물고 와
//   **E-EFFECT** 로 거절됐다 — 두 모듈이 공존할 수 없었다(원장의 *아픈 쌍*).
//   ⇒ 규칙은 한 줄이다: **한정이 있으면 그 모듈, 없으면 제 모듈, 그래도 없으면 첫 일치.**
static unsigned op_declared_q(const low_opinfo_t *tab, proven_size_t n, proven_u8str_view_t name,
                              proven_u8str_view_t qmod, proven_u8str_view_t own) {
    if (qmod.size) {
        for (proven_size_t i = 0; i < n; i++)
            if (proven_u8str_view_eq(tab[i].name, name) && tab[i].mod.size &&
                proven_u8str_view_eq(tab[i].mod, qmod)) return tab[i].declared;
    }
    if (own.size) {
        for (proven_size_t i = 0; i < n; i++)
            if (proven_u8str_view_eq(tab[i].name, name) && tab[i].mod.size &&
                proven_u8str_view_eq(tab[i].mod, own)) return tab[i].declared;
    }
    // ★★★★★ **여기 있던 되짚기를 없앴다** (2026-08-30, WO-0149 — 실물 프로그램이 찾았다).
    //   전엔 *"제 모듈에도 없고 한정도 아니면 **아무 모듈의 동명 op**"* 이었다. 그런데
    //   맨이름으로 남의 모듈 op 을 부르는 길은 이 언어에 **없다**(E-VISIBILITY) — 그러니
    //   그 되짚기는 얻는 것 없이 **남의 이름을 훔치기만** 했다:
    //     `segarena` 의 `struct arena` 필드 `used` ↔ `alloc` 의 `proc used`(effects state)
    //   ⇒ 순수한 `fn open` 이 *"효과를 수행한다"*(E-EFFECT-CALC) 고 거절됐다.
    //     **엉뚱한 모듈, 엉뚱한 이유** — 그리고 두 라이브러리가 한 단위에 못 섰다.
    //   ☞ 파서 arity 표에서 같은 되짚기를 없앤 것과 **같은 처방**이다(2026-08-29, spill 30).
    //     같은 뿌리를 고친 여섯 번째 자리다.
    //   ★ 모듈이 **없는** op(단위에 `module` 선언이 없는 옛 파일 · 단형화 인스턴스)은
    //     여전히 이름으로 찾는다 — 그것들에겐 소속이라는 축 자체가 없다.
    for (proven_size_t i = 0; i < n; i++)
        if (proven_u8str_view_eq(tab[i].name, name) && !tab[i].mod.size) return tab[i].declared;
    // ★★★★★★ **단형화 인스턴스는 이름만으로 찾는다** (2026-09-06).
    //
    //   2026-08-30 에 «맨이름 되짚기» 를 없앤 것은 옳았다 — 맨이름으로 남의 모듈 op 을 부르는
    //   길은 없으니 그 되짚기는 남의 이름을 훔치기만 했다. 그런데 그 자리에 **하나가 딸려
    //   죽었다**: 단형화가 만든 인스턴스는 호출 자리에서 **한정자가 벗겨진다**
    //   (`mono_strip_mod` — mono 는 파서 단계라 좁히기 전에 돈다). 그래서
    //       genlib:  export proc gopen … effects state      (제네릭)
    //       wrap:    return genlib.gopen u64 x .            → 호출 자리는 `gopen#u64`(한정 없음)
    //   이 조회가 **아무것도 못 찾고 0 을 냈다** ⇒ 효과가 **제네릭 호출을 못 건넜다**.
    //   코퍼스에서 그 값: *"선언했는데 안 한다"* 경고 146 건 중 **77 건**이 이 뿌리였다
    //   (`growvec`·`vecgen` 계열이 대표 — 컨테이너를 얇게 감싼 자리마다 났다).
    //
    //   ★ 이름에 `#` 이 든 것은 **도구가 지은 이름**이다(`mono_iname`: `<name>#<타입>…`).
    //     사용자 이름은 `#` 을 가질 수 없으므로, 이 되짚기는 **훔칠 이름이 없다** —
    //     없앤 그 되짚기와 모양은 같아도 **닿는 집합이 다르다**.
    //   ☞ *규칙을 없앨 때는 그 규칙이 **누구를 태우고 있었는지** 본다. 한 배에 둘이 타고
    //     있으면, 한 명을 내리게 하려다 다른 한 명까지 내리게 된다.*
    for (proven_size_t z = 0; z < name.size; z++)
        if (name.ptr[z] == (proven_u8)'#') {
            for (proven_size_t i = 0; i < n; i++)
                if (proven_u8str_view_eq(tab[i].name, name)) return tab[i].declared;
            break;
        }
    if (!own.size)
        for (proven_size_t i = 0; i < n; i++)
            if (proven_u8str_view_eq(tab[i].name, name)) return tab[i].declared;
    return 0;
}

// Accumulate effects performed in a subtree: effectful builtins + declared effects of
// called ops (one level of declared-effect summary; sufficient since callees' declared
// effects already summarise their bodies — the classic effect-summary fixpoint).

// ★★ 효과는 **호출을 통해 전파된다** (SPEC-006 §36: "이 한 검사가 **호출 지점 검사도 포함한다**").
//
//   그런데 이 함수는 **form 의 머리만** 봤다. 이 문법에서 호출은 **괄호로 싸지 않는다** —
//   `return writer 1 .` 의 form 은 (return writer 1) 이고, 호출 이름 `writer` 는 **머리가 아니다.**
//   인자 없는 호출은 아예 **그냥 원자**다(`return writer .`).
//   ⇒ 즉 **효과가 호출을 통해 전파된 적이 없다.** `effects` 절은 직접 쓴 빌트인에만 걸렸고,
//     나머지는 장식이었다. `effects none` 인 fn 이 `effects io` 인 proc 을 불러도 초록불.
//     **순수성 보증이 통째로 새고 있었다** — 그리고 SPEC-003 §27 은 "호출자가 의존하는 보증" 이라 적었다.
//
//   이제 **모든 원자**를 op 표와 대조한다. IR 이 호출을 낮추는 방식과 같다(원자 이름 → call).
//   이름공간이 **평면**이므로(E-NAME-DUP) op 이름과 같은 원자는 그 op 를 가리킨다.
// ★ 이 form 이 `task_group` 이면 **띄운 멤버 수**를 센다(spawn 의 개수).
static proven_size_t ck_group_members(const low_cst_t *nd) {
    proven_size_t k = 0;
    if (!nd) return 0;
    if (nd->kind == LOW_CST_ATOM) return veq(nd->tok.lex, "spawn") ? 1 : 0;
    for (proven_size_t i = 0; i < nd->nkids; i++) k += ck_group_members(nd->kids[i]);
    return k;
}

static bool ck_is_op(const low_opinfo_t *tab, proven_size_t n, proven_u8str_view_t name);   // 전방 선언
// ★★★ **파라미터 이름은 호출이 아니다** (X-0008 · 2026-08-15).
//
//   아래 워크는 *"op 표에 있는 이름의 ATOM 을 보면 그 op 의 효과를 합친다"* 로 도는데,
//   **제 파라미터 이름**도 그냥 ATOM 이다. 그래서 남의 모듈이 같은 이름의 op 을 갖고 있으면
//   파라미터를 읽는 자리가 **호출로 세어졌다**:
//
//       lib/file.low   export proc write … input bytes slice u8 . effects io .
//                      do  return file_write fs (field h fd) bytes .        ← `bytes` = 파라미터
//       lib/pool.low   export actor block_pool …  proc bytes … effects state .
//       ⇒ `use file` + `use pool` 두 줄이면 **E-EFFECT**(state 가 io 를 넘는다).
//
//   ★ 후보 치환이 못박았다: `pool` 의 `bytes` 이름만 바꾸면 오류 1 → 0.
//   ☞ walk_effects_in 주석이 이미 적어 둔 **같은 병의 세 번째 얼굴**이다 — 첫째는 효과 낱말이
//     사용자 op 이름과 겹치는 경우, 둘째는 `order release` 의 **문법 자리**, 셋째가 이것:
//     **지역 이름**. 셋 다 *"이 ATOM 은 정말 호출인가"* 를 안 묻고 이름만 본 탓이다.
static bool ck_is_param_name(const low_cst_t *form, proven_u8str_view_t name) {
    if (!form) return false;
    for (proven_size_t i = 0; i + 1 < form->nkids; i++) {
        if (form->kids[i]->kind != LOW_CST_ATOM || !veq(form->kids[i]->tok.lex, "input")) continue;
        const low_cst_t *nm = form->kids[i + 1];
        if (nm->kind == LOW_CST_ATOM && proven_u8str_view_eq(nm->tok.lex, name)) return true;
    }
    return false;
}

// ★★★★ **`alloc_bytes` 의 효과는 뿌리 피연산자의 종류가 정한다** (RFC-0112 D2(5) · WO-0211).
//   `cap heap` 이거나 `region <이름> heap` 블록이면 `heap`(자라는 뿌리), 그 밖은 `alloc`(고정 창).
//   이름 휴리스틱(`builtin_effect`)은 인자를 못 보므로 여기서 한 번 더 가른다.
// ★★★★★ RFC-0120 §5.2-10 — 매니페스트가 허락한 모듈 목록(`build absorb <모듈> .`).
//   비어 있으면 **아무도 못 한다** — 그것이 기본값이다.
static char ck_absorb_allow[16][64];
static int  ck_absorb_nallow;
void low_check_set_absorb_allow(const char *const *names, int n) {
    ck_absorb_nallow = 0;
    for (int i = 0; i < n && i < 16; i++) {
        snprintf(ck_absorb_allow[ck_absorb_nallow], 64, "%s", names[i]);
        ck_absorb_nallow++;
    }
}
// ★ 이 번역 단위의 모듈 이름 — `module <이름> .` 한 폼에서 읽는다.
static proven_u8str_view_t ck_module_name(const low_parse_result_t *pr) {
    proven_u8str_view_t none = { 0 };
    if (!pr) return none;
    for (proven_size_t i = 0; i < pr->nforms; i++) {
        const low_cst_t *f = pr->forms[i];
        if (!f || f->kind != LOW_CST_FORM || f->nkids < 2) continue;
        if (ck_atom(f->kids[0]) && f->kids[0]->tok.kw == LOW_KW_MODULE && ck_atom(f->kids[1]))
            return f->kids[1]->tok.lex;
    }
    return none;
}
static bool ck_absorb_place_ok(proven_u8str_view_t mod) {
    for (int i = 0; i < ck_absorb_nallow; i++) {
        proven_size_t n = strlen(ck_absorb_allow[i]);
        if (n == mod.size && memcmp(ck_absorb_allow[i], mod.ptr, n) == 0) return true;
    }
    return false;
}

// ★★★★★ RFC-0120 — 이 op 이 `absorbs machine <이름> .` 을 적었나. 적었으면 묶는 이름을 낸다.
//   흡수는 **효과 줄에서 `unsafe` 를 멈추고**, 몸 안에서 그 이름을 `cap machine` 으로 만든다.
//   그 대가는 §5.2 의 사전요건 다섯이고, 장부(`check-absorb.py`)가 서명·해시를 센다.
static bool ck_absorbs_machine(const low_cst_t *form, proven_u8str_view_t *bound) {
    if (!form) return false;
    for (proven_size_t i = 2; i + 2 < form->nkids; i++) {
        if (!ck_atom(form->kids[i]) || !veq(form->kids[i]->tok.lex, "absorbs")) continue;
        if (!ck_atom(form->kids[i + 1]) || !veq(form->kids[i + 1]->tok.lex, "machine")) continue;
        if (!ck_atom(form->kids[i + 2])) continue;
        if (bound) *bound = form->kids[i + 2]->tok.lex;
        return true;
    }
    return false;
}

static bool ck_param_cap_kind(const low_cst_t *form, proven_u8str_view_t name, const char *kind) {
    if (!form) return false;
    // ★ 흡수가 묶은 이름은 그 op 의 몸 안에서 `cap machine` 이다(RFC-0120 §8-1: «만들어 낼 수 있다,
    //   단 machine 에 한해서만»). 오늘 `cap machine` 은 **어디에서도 얻을 수 없었다** — 시작점이
    //   받는 권한 목록(정본 §7.2.2)에 없기 때문이다. 흡수가 그 자리를 연다.
    if (kind && strcmp(kind, "machine") == 0) {
        proven_u8str_view_t bound = { 0 };
        if (ck_absorbs_machine(form, &bound) && proven_u8str_view_eq(bound, name)) return true;
    }
    for (proven_size_t i = 0; i + 3 < form->nkids; i++) {
        if (form->kids[i]->kind != LOW_CST_ATOM || !veq(form->kids[i]->tok.lex, "input")) continue;
        const low_cst_t *nm = form->kids[i + 1], *c = form->kids[i + 2], *k = form->kids[i + 3];
        if (nm->kind == LOW_CST_ATOM && proven_u8str_view_eq(nm->tok.lex, name) &&
            c->kind == LOW_CST_ATOM && veq(c->tok.lex, "cap") &&
            k->kind == LOW_CST_ATOM && veq(k->tok.lex, kind)) return true;
    }
    return false;
}
// 이 나무 안에서 이름 `name` 으로 연 `region` 블록의 **종류 낱말**(없으면 NULL).
static const low_cst_t *ck_region_kind_of(const low_cst_t *nd, proven_u8str_view_t name) {
    if (!nd || nd->kind == LOW_CST_ATOM) return NULL;
    if (nd->kind == LOW_CST_FORM && nd->nkids >= 3 && nd->kids[0]->kind == LOW_CST_ATOM &&
        veq(nd->kids[0]->tok.lex, "region") && nd->kids[1]->kind == LOW_CST_ATOM &&
        proven_u8str_view_eq(nd->kids[1]->tok.lex, name)) {
        // ★ 두 모양: `[region, r, arena, BLOCK]` 또는 `[region, r, FORM(arena, BLOCK)]`(블록이 앞 낱말에 붙는다)
        if (nd->kids[2]->kind == LOW_CST_ATOM) return nd->kids[2];
        const low_cst_t *lf = nd->kids[2];
        if (lf->kind == LOW_CST_FORM && lf->nkids && lf->kids[0]->kind == LOW_CST_ATOM) return lf->kids[0];
    }
    for (proven_size_t i = 0; i < nd->nkids; i++) {
        const low_cst_t *r = ck_region_kind_of(nd->kids[i], name);
        if (r) return r;
    }
    return NULL;
}
// ★ `root` 는 `alloc_bytes` 바로 뒤의 피연산자다 — 평평한 CST 에서도, 나무에서도 같은 자리.
// ★★★★ RFC-0112 D6 — actor 와 그 상태의 권한 칸.
static proven_u8str_view_t ck_bare_name(proven_u8str_view_t v) {
    for (proven_size_t i = v.size; i-- > 0; )
        if (v.ptr[i] == (proven_u8)'.') return (proven_u8str_view_t){ .ptr = v.ptr + i + 1, .size = v.size - i - 1 };
    return v;
}
static const low_cst_t *ck_actor_named(proven_u8str_view_t name) {
    if (!g_ck_pr) return NULL;
    proven_u8str_view_t b = ck_bare_name(name);
    for (proven_size_t i = 0; i < g_ck_pr->nforms; i++) {
        const low_cst_t *f = g_ck_pr->forms[i];
        if (f && f->kind == LOW_CST_FORM && f->nkids >= 3 && f->kids[0]->kind == LOW_CST_ATOM &&
            f->kids[0]->tok.kw == LOW_KW_ACTOR && f->kids[1]->kind == LOW_CST_ATOM &&
            proven_u8str_view_eq(f->kids[1]->tok.lex, b)) return f;
    }
    return NULL;
}
static const low_cst_t *ck_enclosing_actor(const low_cst_t *op) {
    if (!g_ck_pr || !op) return NULL;
    for (proven_size_t i = 0; i < g_ck_pr->nforms; i++) {
        const low_cst_t *f = g_ck_pr->forms[i];
        if (!f || f->kind != LOW_CST_FORM || f->nkids < 3 || f->kids[0]->kind != LOW_CST_ATOM ||
            f->kids[0]->tok.kw != LOW_KW_ACTOR) continue;
        const low_cst_t *blk = f->kids[f->nkids - 1];
        if (blk->kind != LOW_CST_BLOCK) continue;
        for (proven_size_t j = 0; j < blk->nkids; j++) if (blk->kids[j] == op) return f;
    }
    return NULL;
}
// 그 actor 상태에서 이름 `nm` 인 칸의 권한 종류: 1 = cap heap · 0 = cap allocator · -1 = 아님.
// `nm` 을 비워 두면(size 0) **어떤 권한 칸이든** 첫 것을 답한다 — 대신 `want` 종류가 있는지 묻는다.
static int ck_actor_capfield(const low_cst_t *actor, proven_u8str_view_t nm) {
    if (!actor) return -1;
    const low_cst_t *blk = actor->kids[actor->nkids - 1];
    if (blk->kind != LOW_CST_BLOCK) return -1;
    for (proven_size_t j = 0; j < blk->nkids; j++) {
        const low_cst_t *st = blk->kids[j];
        if (st->kind != LOW_CST_FORM || st->nkids < 2 || st->kids[0]->kind != LOW_CST_ATOM ||
            st->kids[0]->tok.kw != LOW_KW_STATE) continue;
        const low_cst_t *sb = st->kids[st->nkids - 1];
        if (sb->kind != LOW_CST_BLOCK) continue;
        for (proven_size_t q = 0; q < sb->nkids; q++) {
            const low_cst_t *fld = sb->kids[q];
            if (fld->kind != LOW_CST_FORM || fld->nkids < 3 || !ck_atom(fld->kids[0]) ||
                !ck_atom(fld->kids[1]) || !ck_atom(fld->kids[2]) || !veq(fld->kids[1]->tok.lex, "cap")) continue;
            if (!proven_u8str_view_eq(fld->kids[0]->tok.lex, nm)) continue;
            if (veq(fld->kids[2]->tok.lex, "heap")) return 1;
            if (veq(fld->kids[2]->tok.lex, "allocator")) return 0;
        }
    }
    return -1;
}
static bool ck_actor_has_capkind(const low_cst_t *actor, const char *kind) {
    if (!actor) return false;
    const low_cst_t *blk = actor->kids[actor->nkids - 1];
    if (blk->kind != LOW_CST_BLOCK) return false;
    for (proven_size_t j = 0; j < blk->nkids; j++) {
        const low_cst_t *st = blk->kids[j];
        if (st->kind != LOW_CST_FORM || st->nkids < 2 || st->kids[0]->kind != LOW_CST_ATOM ||
            st->kids[0]->tok.kw != LOW_KW_STATE) continue;
        const low_cst_t *sb = st->kids[st->nkids - 1];
        if (sb->kind != LOW_CST_BLOCK) continue;
        for (proven_size_t q = 0; q < sb->nkids; q++) {
            const low_cst_t *fld = sb->kids[q];
            if (fld->kind == LOW_CST_FORM && fld->nkids >= 3 && ck_atom(fld->kids[1]) && ck_atom(fld->kids[2]) &&
                veq(fld->kids[1]->tok.lex, "cap") && veq(fld->kids[2]->tok.lex, kind)) return true;
        }
    }
    return false;
}
// ★★★★ RFC-0112 D7 — `via <타입>` 의 뜻: 그 타입(actor)의 op 들이 적은 효과 가운데 **할당 계열**
//   (`alloc` · `heap` · `atomic`)의 합. `self`(트레이트 안)는 아무것도 더하지 않는다 — 구현이 정한다.
static unsigned ck_via_effects(proven_u8str_view_t ty) {
    if (veq(ty, "self")) return EFF_NONE;
    const low_cst_t *a = ck_actor_named(ty);
    if (!a) return EFF_NONE;
    const low_cst_t *blk = a->kids[a->nkids - 1];
    if (blk->kind != LOW_CST_BLOCK) return EFF_NONE;
    unsigned e = EFF_NONE;
    for (proven_size_t j = 0; j < blk->nkids; j++) {
        const low_cst_t *op = blk->kids[j];
        if (op->kind != LOW_CST_FORM || op->nkids < 2 || op->kids[0]->kind != LOW_CST_ATOM) continue;
        if (op->kids[0]->tok.kw != LOW_KW_PROC && op->kids[0]->tok.kw != LOW_KW_FN) continue;
        bool f = false;
        e |= decl_effect(op, &f) & (EFF_ALLOC | EFF_HEAP | EFF_ATOMIC);
    }
    return e;
}

// 이 actor 의 op 가운데 이름이 `m` 인 것
static const low_cst_t *ck_actor_proc(const low_cst_t *actor, proven_u8str_view_t m) {
    if (!actor) return NULL;
    const low_cst_t *blk = actor->kids[actor->nkids - 1];
    if (blk->kind != LOW_CST_BLOCK) return NULL;
    for (proven_size_t j = 0; j < blk->nkids; j++) {
        const low_cst_t *op = blk->kids[j];
        if (op->kind == LOW_CST_FORM && op->nkids >= 2 && op->kids[0]->kind == LOW_CST_ATOM &&
            (op->kids[0]->tok.kw == LOW_KW_PROC || op->kids[0]->tok.kw == LOW_KW_FN) &&
            op->kids[1]->kind == LOW_CST_ATOM && proven_u8str_view_eq(op->kids[1]->tok.lex, m)) return op;
    }
    return NULL;
}
// 이 op 의 입력·지역 이름 `nm` 의 **타입 첫 낱말**(한정자 mut/owned 는 건너뛴다)
static proven_u8str_view_t ck_name_type_word(const low_cst_t *nd, proven_u8str_view_t nm) {
    proven_u8str_view_t none = { 0 };
    if (!nd || nd->kind == LOW_CST_ATOM) return none;
    for (proven_size_t i = 0; i + 2 < nd->nkids; i++) {
        const low_cst_t *k0 = nd->kids[i];
        bool binder = k0->kind == LOW_CST_ATOM &&
                      (veq(k0->tok.lex, "input") || k0->tok.kw == LOW_KW_LET || k0->tok.kw == LOW_KW_VAR);
        if (!binder || nd->kids[i + 1]->kind != LOW_CST_ATOM || !proven_u8str_view_eq(nd->kids[i + 1]->tok.lex, nm)) continue;
        for (proven_size_t q = i + 2; q < nd->nkids && nd->kids[q]->kind == LOW_CST_ATOM; q++) {
            proven_u8str_view_t w = nd->kids[q]->tok.lex;
            if (veq(w, "mut") || veq(w, "owned") || veq(w, "comptime")) continue;
            return w;
        }
        return none;
    }
    for (proven_size_t i = 0; i < nd->nkids; i++) {
        proven_u8str_view_t w = ck_name_type_word(nd->kids[i], nm);
        if (w.size) return w;
    }
    return none;
}
static const low_cst_t *ck_struct_named(proven_u8str_view_t name) {
    if (!g_ck_pr) return NULL;
    for (proven_size_t i = 0; i < g_ck_pr->nforms; i++) {
        const low_cst_t *f = g_ck_pr->forms[i];
        if (f && f->kind == LOW_CST_FORM && f->nkids >= 3 && f->kids[0]->kind == LOW_CST_ATOM &&
            f->kids[0]->tok.kw == LOW_KW_STRUCT && f->kids[1]->kind == LOW_CST_ATOM &&
            proven_u8str_view_eq(f->kids[1]->tok.lex, name)) return f;
    }
    return NULL;
}
// `send` 의 받는 쪽이 어느 actor 인가 — 이름(입력·지역) 또는 `field <이름> <칸>`(묶음의 칸)
static const low_cst_t *ck_send_target_actor(const low_cst_t *tgt, const low_cst_t *selfform) {
    if (!tgt) return NULL;
    if (tgt->kind == LOW_CST_GROUP && tgt->nkids) tgt = tgt->kids[0];
    if (tgt->kind == LOW_CST_ATOM) {
        proven_u8str_view_t tw = ck_name_type_word(selfform, tgt->tok.lex);
        return tw.size ? ck_actor_named(tw) : NULL;
    }
    if (tgt->kind == LOW_CST_FORM && tgt->nkids == 3 && tgt->kids[0]->kind == LOW_CST_ATOM &&
        veq(tgt->kids[0]->tok.lex, "field") && tgt->kids[1]->kind == LOW_CST_ATOM && tgt->kids[2]->kind == LOW_CST_ATOM) {
        proven_u8str_view_t sw = ck_name_type_word(selfform, tgt->kids[1]->tok.lex);
        const low_cst_t *st = sw.size ? ck_struct_named(sw) : NULL;
        if (!st) return NULL;
        for (proven_size_t i = 0; i + 1 < st->nkids; i++) {
            const low_cst_t *fl = st->kids[i];
            if (fl->kind == LOW_CST_FORM && fl->nkids >= 2 && fl->kids[0]->kind == LOW_CST_ATOM &&
                proven_u8str_view_eq(fl->kids[0]->tok.lex, tgt->kids[2]->tok.lex) && fl->kids[1]->kind == LOW_CST_ATOM)
                return ck_actor_named(fl->kids[1]->tok.lex);
        }
        // 평평한 struct(`struct n . a t . b u . end`)는 칸이 폼이 아닐 수 있다 — 낱말 열에서 찾는다
        for (proven_size_t i = 2; i + 1 < st->nkids; i++)
            if (st->kids[i]->kind == LOW_CST_ATOM && proven_u8str_view_eq(st->kids[i]->tok.lex, tgt->kids[2]->tok.lex) &&
                st->kids[i + 1]->kind == LOW_CST_ATOM)
                return ck_actor_named(st->kids[i + 1]->tok.lex);
    }
    return NULL;
}

static bool ck_alloc_root_is_heap(const low_cst_t *root, const low_cst_t *selfform) {
    if (!root || root->kind != LOW_CST_ATOM) return false;
    proven_u8str_view_t nm = root->tok.lex;
    if (ck_actor_capfield(ck_enclosing_actor(selfform), nm) == 1) return true;
    if (ck_param_cap_kind(selfform, nm, "heap")) return true;
    const low_cst_t *k = ck_region_kind_of(selfform, nm);
    return k && veq(k->tok.lex, "heap");
}

static unsigned walk_effects_in(const low_cst_t *nd, const low_opinfo_t *tab, proven_size_t n,
                                proven_u8str_view_t own, const low_cst_t *selfform) {
    if (!nd) return EFF_NONE;
    unsigned e = EFF_NONE;
    // ★★★ **`method` 로 부른 op 의 효과도 부르는 쪽의 것이다** (정본 §7.1 · §6.11.3 · 결함 노트 #15).
    //   `method s boom` 은 `rect.boom s` 를 부르는 폼인데, 이 워크는 원자 `boom` 만 보았고 표에
    //   있는 이름은 `rect.boom` 이라 **아무것도 못 찾았다** — 순수한 `fn` 이 `panic` 하는 메서드를
    //   불러도 통과했다(직접 부르면 `E-EFFECT-CALC`). 같은 부름이 철자에 따라 다르게 판정됐다.
    //   ★ 수신자의 타입을 알면 `<타입>.<이름>` 하나를 고르고, 모르면 **그 이름의 메서드가 단위에
    //     딱 하나일 때만** 센다 — 지어내지 않는다(여럿이면 어느 것인지 이 층은 모른다).
    //   ☞ CST 는 **평평하다** — `return method s boom .` 은 한 폼이라 `method` 가 머리가 아니다.
    //     그래서 자리를 가리지 않고 훑는다(`ck_capkind_walk` 가 같은 이유로 그렇게 돈다).
    proven_size_t mpos = nd->nkids;
    if (nd->kind == LOW_CST_FORM)
        for (proven_size_t z = 0; z < nd->nkids; z++)
            if (nd->kids[z]->kind == LOW_CST_ATOM && nd->kids[z]->tok.kw == LOW_KW_NONE &&
                veq(nd->kids[z]->tok.lex, "method")) { mpos = z; break; }
    if (mpos + 2 < nd->nkids) {
        for (proven_size_t q = mpos + 2; q < nd->nkids; q++) {
            if (nd->kids[q]->kind != LOW_CST_ATOM || nd->kids[q]->tok.kw != LOW_KW_NONE) continue;
            proven_u8str_view_t mn = nd->kids[q]->tok.lex;
            proven_size_t hit = n, nhit = 0;
            for (proven_size_t i = 0; i < n; i++) {
                proven_u8str_view_t tn = tab[i].name;
                proven_size_t dot = tn.size;
                for (proven_size_t z = tn.size; z-- > 0; ) if (tn.ptr[z] == (proven_byte_t)'.') { dot = z; break; }
                if (dot >= tn.size) continue;
                proven_u8str_view_t suf = { .ptr = tn.ptr + dot + 1, .size = tn.size - dot - 1 };
                if (!proven_u8str_view_eq(suf, mn)) continue;
                hit = i; nhit++;
            }
            if (nhit == 1) e |= tab[hit].declared;
        }
    }
    // ★★★ **`task_group` 이 `concurrent` 를 흡수한다 — 멤버가 둘 이상일 때만** (RFC-0071 A5).
    //
    //   `concurrent` 는 *"동료가 돌아야 완결된다"* 는 요구다. 멤버가 **둘 이상인** 그룹은
    //   그 동료를 **자기 안에** 갖고 있으므로 요구가 **그 자리에서 만족된다** — 밖으로 안 샌다.
    //   ⇒ `vm_chan.low` 의 `drive` 는 consumer 와 producer 를 함께 띄우므로 `concurrent` 를
    //     선언할 필요가 없다. **그룹이 답했다.**
    //
    //   ★★★ 그리고 그것이 곧 **멤버가 하나면 데드락**인 이유다 — 아래 `ck_lone_concurrent`.
    //     RFC-0022 D-A 가 어휘 등재의 조건으로 요구한 *"무는 검사"* 가 이것이다.
    if (nd->kind == LOW_CST_FORM && nd->nkids > 0 && nd->kids[0]->kind == LOW_CST_ATOM &&
        veq(nd->kids[0]->tok.lex, "task_group")) {
        for (proven_size_t i = 0; i < nd->nkids; i++) e |= walk_effects_in(nd->kids[i], tab, n, own, selfform);
        if (ck_group_members(nd) >= 2) e &= ~(unsigned)EFF_CONCURRENT;
        return e;
    }
    // ★★★ **빌트인의 효과를 머리 자리에서만 세고 있었다.**
    //
    //   `if (FORM) e |= builtin_effect(kids[0])` — 즉 **문장의 머리일 때만.**
    //   그래서 식 **안에 중첩된** 효과 있는 빌트인은 **보이지 않았다**:
    //
    //       let work be stack node_id stack_new temp capacity n . . .
    //       effects none .                    ← **거짓말이었다.** stack_new 은 alloc 이다.
    //
    //   그리고 `--check` 는 **초록불**이었다. 효과 계약이 **강제되지 않는 자리**가 있었다.
    //
    //   ★ 이것을 **arity 단일화가 찾았다**: 나무를 세우자 `stack_new` 이 자기 form 의 머리가
    //     되면서 검사에 걸렸고, "평평 vs 나무 진단 동일성" 오라클이 **불일치를 고발했다.**
    //     ⇒ **나무가 옳고 평평한 쪽이 틀렸다.** 이제 **어느 자리에 있든** 센다.
    // ★★★ **효과 낱말이 사용자 op 이름이면, 그 op 의 선언 효과가 진실이다** (거짓양성 방지).
    //   builtin_effect 는 정의 없는 **프리미티브**(print/run/env … 네이티브 내장)를 위한 이름
    //   휴리스틱이다. 그런데 사용자가 `fn run … effects none` 을 정의해도 **호출자에게 IO 를
    //   물렸다**: op 이름 `run` 이 효과 낱말과 겹친다는 이유만으로. `run`·`send` 는 흔한 메서드
    //   이름이라 이 오진은 정당한 프로그램을 막았다. ⇒ 이름이 **op 표에 있으면**(사용자가 정의했으면)
    //   builtin_effect 를 생략한다 — 그 op 의 진짜 효과는 **자기 본문 워크**가 이미 잡는다.
    if (nd->kind == LOW_CST_ATOM && !ck_is_param_name(selfform, nd->tok.lex)) {
        e |= op_declared_q(tab, n, nd->tok.lex, nd->qual_mod, own);
        if (!ck_is_op(tab, n, nd->tok.lex)) e |= builtin_effect(nd->tok.lex);
    }
    if (nd->kind == LOW_CST_FORM && nd->nkids > 0 && nd->kids[0]->kind == LOW_CST_ATOM &&
        !ck_is_op(tab, n, nd->kids[0]->tok.lex)) {
        if (veq(nd->kids[0]->tok.lex, "alloc_bytes") && nd->nkids >= 2 &&
            ck_alloc_root_is_heap(nd->kids[1], selfform)) {
            e |= EFF_HEAP;                              // ★ RFC-0112 D2(5) — 자라는 뿌리
            // 머리 원자를 아래 되풀이가 다시 보면 이름 휴리스틱이 `alloc` 을 더한다 — 건너뛴다.
            for (proven_size_t i = 1; i < nd->nkids; i++) e |= walk_effects_in(nd->kids[i], tab, n, own, selfform);
            return e;
        } else
            e |= builtin_effect(nd->kids[0]->tok.lex);
    }
    // ★★★ **asm 문장은 `unsafe` 를 한다** (RFC-0042 D11) — 안 세면 도구가 **자기 자신과 모순**한다:
    //   `ck_asm` 은 `effects unsafe` 를 **요구**하는데(E-ASM-NOEFFECT) 여기서 안 세면 같은 줄이
    //   *"선언했는데 안 한다"*(W-EFFECT-OVER)로 경고된다. 둘 중 하나는 반드시 거짓말이다.
    if (nd->kind == LOW_CST_FORM && nd->nkids > 0 && nd->kids[0]->kind == LOW_CST_ATOM &&
        nd->kids[0]->tok.kw == LOW_KW_NONE && veq(nd->kids[0]->tok.lex, "asm"))
        e |= EFF_UNSAFE;
    // ★★★ **`order release` 의 `release` 는 호출이 아니다 — 메모리 순서 낱말이다.**
    //
    //   이 함수는 *"op 표에 있는 이름의 ATOM 을 보면 그 op 의 선언 효과를 합친다"* 로 도는데
    //   (위 250~253), 원자 연산의 순서 낱말(`relaxed`·`acquire`·`release`·`acq_rel`·`seq_cst`)도
    //   그냥 ATOM 이다. 그래서 **`release` 라는 이름의 op 이 단위 안에 있으면**
    //   `atomic_store … order release .` 이 그 op 을 부른 것으로 세어졌다:
    //
    //       lib/pool.low   export proc release … effects state .
    //       lib/spsc.low   … effects atomic .  do  atomic_store ctl 1 nx order release .
    //       ⇒ `use pool` + `use spsc` 두 줄이면 **E-EFFECT**(state 가 atomic 을 넘는다).
    //
    //   ★ 그리고 **코드젠은 옳았다** — 방출된 C 는 여전히 `memory_order_release` 다(실측).
    //     즉 도구의 두 반쪽이 같은 토큰을 **다르게** 읽고 있었고, 검사기 쪽만 틀렸다:
    //     **초록이어야 할 프로그램에 빨간불**(거짓양성). 그래서 두 모듈이 공존 못 했다.
    //   ☞ 이것은 위 244 의 교훈과 **같은 병의 두 번째 얼굴**이다(효과 낱말이 사용자 op 이름과
    //     겹치는 경우). 거기서는 빌트인 휴리스틱을 껐고, 여기서는 **문법 자리**를 존중한다.
    for (proven_size_t i = 0; i < nd->nkids; i++) {
        if (i + 1 < nd->nkids && nd->kids[i]->kind == LOW_CST_ATOM &&
            nd->kids[i]->tok.kw == LOW_KW_NONE && veq(nd->kids[i]->tok.lex, "order") &&
            nd->kids[i + 1]->kind == LOW_CST_ATOM) {
            i++;                 // 순서 낱말을 건너뛴다 — 이름이 아니라 **문법 자리**다
            continue;
        }
        // ★★★★ WO-0213 — `send <받는 쪽> <메시지>`: 받는 쪽의 **타입**으로 처리기를 찾아 그 효과를 싣는다.
        //   전엔 메시지 이름을 op 표에서 **이름으로** 찾아 첫 일치를 썼다 — 한 모듈에 `reserve` 를 가진 actor 가
        //   여럿이면(범프 · 힙) 남의 효과를 읽었고, 힙 얼로케이터로 단형화한 인스턴스가 `heap` 을 «안 한다» 고 했다.
        if (i + 2 < nd->nkids && nd->kids[i]->kind == LOW_CST_ATOM && nd->kids[i]->tok.kw == LOW_KW_SEND &&
            nd->kids[i + 2]->kind == LOW_CST_ATOM) {
            const low_cst_t *act = ck_send_target_actor(nd->kids[i + 1], selfform);
            const low_cst_t *h = act ? ck_actor_proc(act, nd->kids[i + 2]->tok.lex) : NULL;
            if (h) {
                bool fd = false;
                e |= EFF_STATE | decl_effect(h, &fd);
                e |= walk_effects_in(nd->kids[i + 1], tab, n, own, selfform);
                i += 2;
                continue;
            }
        }
        // ★ RFC-0112 D2(5) — 평평한 자리의 `alloc_bytes <뿌리>`: 뿌리가 힙이면 `alloc` 이 아니라 `heap` 이다.
        if (i + 1 < nd->nkids && nd->kids[i]->kind == LOW_CST_ATOM && veq(nd->kids[i]->tok.lex, "alloc_bytes") &&
            !ck_is_op(tab, n, nd->kids[i]->tok.lex) && ck_alloc_root_is_heap(nd->kids[i + 1], selfform)) {
            e |= EFF_HEAP;
            continue;
        }
        e |= walk_effects_in(nd->kids[i], tab, n, own, selfform);
    }
    return e;
}

static unsigned walk_effects(const low_cst_t *nd, const low_opinfo_t *tab, proven_size_t n) {
    proven_u8str_view_t none = { 0 };
    return walk_effects_in(nd, tab, n, none, NULL);   // 문맥이 없는 자리 — 전과 같이 첫 일치
}


// ★★★ **핸들러가 자기 actor 의 상태 필드를 쓰는가** (EFF_STATE 판별용). actor 의 BLOCK 에서 상태
//   필드 이름들을 모으고, body 에 `set <그 이름> …` 또는 `set (index <그 이름> …) …` 가 있으면 참.
static bool ck_name_is_state_field(const low_cst_t *actor_blk, proven_u8str_view_t nm) {
    if (!actor_blk) return false;
    for (proven_size_t i = 0; i < actor_blk->nkids; i++) {
        const low_cst_t *sf = actor_blk->kids[i];
        if (sf->kind != LOW_CST_FORM || !sf->nkids || sf->kids[0]->kind != LOW_CST_ATOM) continue;
        if (sf->kids[0]->tok.kw != LOW_KW_STATE) continue;
        const low_cst_t *sb = (sf->kids[sf->nkids-1]->kind == LOW_CST_BLOCK) ? sf->kids[sf->nkids-1] : NULL;
        if (!sb) continue;
        for (proven_size_t q = 0; q < sb->nkids; q++) {
            const low_cst_t *fl = sb->kids[q];
            if (fl->kind == LOW_CST_FORM && fl->nkids && fl->kids[0]->kind == LOW_CST_ATOM &&
                proven_u8str_view_eq(fl->kids[0]->tok.lex, nm)) return true;
        }
    }
    return false;
}
static bool ck_handler_writes_state(const low_cst_t *nd, const low_cst_t *actor_blk) {
    if (!nd) return false;
    if (nd->kind == LOW_CST_FORM && nd->nkids >= 2 && nd->kids[0]->kind == LOW_CST_ATOM &&
        nd->kids[0]->tok.kw == LOW_KW_SET) {
        const low_cst_t *tgt = nd->kids[1];
        while (tgt && (tgt->kind == LOW_CST_GROUP || tgt->kind == LOW_CST_FORM) && tgt->nkids == 1) tgt = tgt->kids[0];
        // `set <name> …`
        if (tgt && tgt->kind == LOW_CST_ATOM && ck_name_is_state_field(actor_blk, tgt->tok.lex)) return true;
        // `set (index <name> i) …` / `set (field <name> f) …`
        if (tgt && tgt->kind == LOW_CST_FORM && tgt->nkids >= 2 && tgt->kids[1]->kind == LOW_CST_ATOM &&
            ck_name_is_state_field(actor_blk, tgt->kids[1]->tok.lex)) return true;
    }
    for (proven_size_t i = 0; i < nd->nkids; i++)
        if (ck_handler_writes_state(nd->kids[i], actor_blk)) return true;
    return false;
}

// ★ 2026-09-14 — 지금 검사 중인 최상위 폼. 노드를 모르는 `emit()` 도 그 줄이 이 폼 안이면 **파일을 싣는다**.
static const low_cst_t *ck_cur_form;
static void emit(low_check_result_t *out, const char *code, const char *msg, proven_u32 line) {
    low_diag_t d = { .sev = LOW_SEV_ERROR, .code = code, .msg = msg, .line = line, .col = 0,
                     .file = low_cst_file_for_line(ck_cur_form, line) };
    (void)proven_array_push(&out->diags, &d);
    out->ok = false;
}
// ★★★ **자리를 아는 진단은 파일도 말한다** (단계 V). 한 단위가 여러 파일이고 줄은 파일마다
//   1 부터 다시 시작하므로, 노드를 손에 쥔 자리에서는 그 노드의 파일을 함께 싣는다.
//   ☞ 노드를 모르는 자리는 여태처럼 `emit()` 을 쓴다 — **없는 것을 지어내지 않는다.**
static void emit_at(low_check_result_t *out, const char *code, const char *msg,
                    const low_cst_t *nd) {
    low_diag_t d = { .sev = LOW_SEV_ERROR, .code = code, .msg = msg,
                     .line = nd ? nd->line : 0, .col = nd ? nd->col : 0,
                     .file = nd ? nd->file : NULL };
    // ★ 같은 코드가 **같은 자리**에 바로 앞에 이미 있으면 다시 내지 않는다(2026-09-29). `be lit <T> …` 는
    //   선언 타입으로 리터럴의 타입 노드를 **복사**해 쓰므로(RFC-0132 §13.1), 그 낱말을 보는 검사가 같은 자리를
    //   두 번 짚었다(실측: E-USE-ALIASED 가 9:16 에 둘).
    if (out->diags.len && nd) {
        const low_diag_t *pv = (const low_diag_t *)out->diags.data + out->diags.len - 1;
        if (pv->code == code && pv->line == d.line && pv->col == d.col && pv->file == d.file) { out->ok = false; return; }
    }
    (void)proven_array_push(&out->diags, &d);
    out->ok = false;
}
// ★ same as emit(), plus a STABLE repair id (REQ-0003). Use only where the fix is unambiguous
//   from the emit site's context — the id is a machine contract, not a hint that might be wrong.
static void emit_r(low_check_result_t *out, const char *code, const char *msg,
                   const char *repair, proven_u32 line) {
    low_diag_t d = { .sev = LOW_SEV_ERROR, .code = code, .msg = msg, .line = line, .col = 0,
                     .repair = repair, .file = low_cst_file_for_line(ck_cur_form, line) };
    (void)proven_array_push(&out->diags, &d);
    out->ok = false;
}
// 경고는 ok 를 깨지 않는다 — 그러나 **보인다.** 조용히 무시하는 것이 죄다.
static void warn(low_check_result_t *out, const char *code, const char *msg, proven_u32 line) {
    low_diag_t d = { .sev = LOW_SEV_WARNING, .code = code, .msg = msg, .line = line, .col = 0,
                     .file = low_cst_file_for_line(ck_cur_form, line) };
    (void)proven_array_push(&out->diags, &d);
}
// ★★★★ **경고도 파일을 말한다** (2026-09-06). `emit_at` 이 오류에 대해 하던 일을 경고에도.
//   한 단위는 `use` 로 딸려 온 파일을 함께 검사하고 **줄 번호는 파일마다 1 부터** 다시 센다.
//   그래서 «75 번 줄» 이라는 경고가 **어느 파일의 75 번 줄인지** 알 수 없었고, 분류 도구가
//   *"가장 가까운 앞선 머리"* 로 짐작해야 했다(그리고 틀릴 수 있었다).
//   ☞ *줄만 있는 자리는 파일이 하나일 때만 자리다. 단위가 여럿이면 그것은 자리가 아니라 힌트다.*
static void warn_at(low_check_result_t *out, const char *code, const char *msg,
                    const low_cst_t *nd) {
    low_diag_t d = { .sev = LOW_SEV_WARNING, .code = code, .msg = msg,
                     .line = nd ? nd->line : 0, .col = nd ? nd->col : 0,
                     .file = nd ? nd->file : NULL };
    (void)proven_array_push(&out->diags, &d);
}

// ★★★ **`.head` 는 검사되는 주장이다.**
//
//   `.name` 은 *"이 이름은 머리다(적용이다)"* 라고 **말한다.** 말했으면 **확인해야 한다** —
//   확인하지 않는 주장은 이 저장소가 평생 쫓는 바로 그것(검사되지 않는 중복)이다.
//
//   전엔 거짓 표시가 **오진**을 냈다: `.a 5`(a 는 지역) → **"extra operands"**.
//   진짜 이유는 *"a 를 머리라 했는데 a 는 op 이 아니다"* 다. (교훈 5 — 오진이 최악이다.)
static void ck_head_claims(low_check_result_t *out, const low_cst_t *nd,
                           const low_opinfo_t *tab, proven_size_t nops) {
    if (!nd) return;
    // ★ **저자가 `.head` 라고 주장한 것만** 검사한다. 정규화가 만든 괄호는 이미 아는 머리다.
    if (nd->kind == LOW_CST_GROUP && nd->synth && nd->tok.kind == LOW_TOK_HEAD &&
        nd->nkids == 1 &&
        nd->kids[0]->kind == LOW_CST_FORM && nd->kids[0]->nkids &&
        nd->kids[0]->kids[0]->kind == LOW_CST_ATOM) {
        proven_u8str_view_t h = nd->kids[0]->kids[0]->tok.lex;
        bool known = low_ir_is_builtin_name(h);
        for (proven_size_t i = 0; i < nops && !known; i++)
            if (proven_u8str_view_eq(tab[i].name, h)) known = true;
        if (!known && !low_is_clause_word(h))
            emit(out, "E-HEAD-NOT-AN-OP",
                 "`.name` says THIS NAME IS A HEAD (an application) — but this name is not an op "
                 "and not a builtin. A head marker is a CLAIM, and a claim that is not checked is "
                 "the defect this language exists to remove. (If you meant the value, drop the dot.)",
                 nd->kids[0]->kids[0]->tok.line);
    }
    for (proven_size_t i = 0; i < nd->nkids; i++) ck_head_claims(out, nd->kids[i], tab, nops);
}

// ★★★ **이름은 맨 식별자다** (사용자 결정, 2026-07-14).
//
//   *"모듈 이름, 구조체 이름 모두 변수랑 똑같은 제한조건을 걸어요.
//     타 언어처럼 http.java.module.subsystem 이런거 안 씁니다.
//     쓸려면 http_java_module_subsystem 이렇게 쓰라고 합시다."*
//
//   ★ 그리고 이것이 **모든 모호함을 한 번에 없앤다.**
//     다단계 경로가 없으면 `a.b.c` 가 무엇인지 물을 일이 **아예 없다.**
//     붙임 점의 뜻이 **정확히 둘**로 닫힌다:
//
//         value.field     **필드 접근**  (왼쪽이 값)
//         Type.op         **연관 op**    (왼쪽이 타입)   ← **딱 한 겹**
//
//   ⇒ 선언되는 이름은 **전부 맨 식별자**다. 유일한 예외가 op 의 `Type.op` 이고,
//     그 `Type` 은 **선언된 타입이어야** 한다. 그것 말고는 점이 든 이름이 없다.
static bool ck_has_dot(proven_u8str_view_t v) {
    for (proven_size_t i = 0; i < v.size; i++) if (v.ptr[i] == (proven_u8)'.') return true;
    return false;
}
static proven_u8str_view_t ck_before_dot(proven_u8str_view_t v) {
    for (proven_size_t i = 0; i < v.size; i++)
        if (v.ptr[i] == (proven_u8)'.') return (proven_u8str_view_t){ .ptr = v.ptr, .size = i };
    return v;
}
static proven_size_t ck_ndots(proven_u8str_view_t v) {
    proven_size_t n = 0;
    for (proven_size_t i = 0; i < v.size; i++) if (v.ptr[i] == (proven_u8)'.') n++;
    return n;
}
// ★ 식별자 규칙 (RFC-0011, 2026-07-25) — 모든 선언 이름 공통.
static void ck_ident_rules(low_check_result_t *out, proven_u8str_view_t nm, proven_u32 line) {
    // 홑 `_` = match wildcard 예약 → 선언 이름 금지 (§2.5).
    if (nm.size == 1 && nm.ptr[0] == (proven_u8)'_') {
        emit(out, "E-NAME-WILDCARD",
             "a lone `_` is the match WILDCARD, not a name — it cannot be declared. Give it a real "
             "name (a single leading `_` like `_x` is fine; a bare `_` is only for `case _`) (RFC-0011)",
             line);
        return;
    }
    // `__` 접두 = 컴파일러/벤더 예약 → 사용자 선언 하드거부 (위장 방지·미래충돌 없음, C `__` 관례 동형).
    if (nm.size >= 2 && nm.ptr[0] == (proven_u8)'_' && nm.ptr[1] == (proven_u8)'_') {
        emit(out, "E-NAME-VENDOR-RESERVED",
             "a name beginning with `__` is RESERVED for the compiler and library vendors — user code "
             "may not declare it. This is a guarantee, not a convention: `__x` always means internal, so "
             "nothing can masquerade as a builtin. Use a single leading `_` or none (RFC-0011)",
             line);
    }
}

static void ck_plain_name(low_check_result_t *out, proven_u8str_view_t nm, const char *what,
                          proven_u32 line) {
    ck_ident_rules(out, nm, line);
    if (!ck_has_dot(nm)) return;
    emit(out, "E-NAME-DOTTED",
         "a declared name must be a PLAIN identifier — no dots. There are no multi-level "
         "namespaces in this language: if you want a hierarchy in the name, write it with "
         "underscores (`http_java_module_subsystem`), not dots. The glued dot means exactly "
         "TWO things and nothing else: `value.field` (access) and `Type.op` (a type's op)",
         line);
    (void)what;
}

// ★ 지역 바인더(var/let/for)가 최상위 이름을 가리는가.
static void ck_no_shadow(low_check_result_t *out, const low_cst_t *nd,
                         const proven_u8str_view_t *top, proven_size_t ntop) {
    if (!nd) return;
    if (nd->kind == LOW_CST_FORM && nd->nkids >= 2 && nd->kids[0]->kind == LOW_CST_ATOM &&
        nd->kids[1]->kind == LOW_CST_ATOM) {
        proven_u8str_view_t h = nd->kids[0]->tok.lex;
        if (veq(h, "var") || veq(h, "let") || veq(h, "for")) {
            proven_u8str_view_t nm = nd->kids[1]->tok.lex;
            ck_plain_name(out, nm, "local", nd->kids[1]->tok.line);   // ★ 지역도 맨 이름이다
            // ★★★ **지역이 빌트인을 가리는 것은 아무도 안 막고 있었다.**
            //   `E-NAME-BUILTIN` 은 **최상위 선언만** 지켰다. 그래서 `var len be u8 3 .` 이
            //   `== check: ok ==` 였다. 그런데 이것이 바로 RFC-0046 이 **P1(무음 재괄호)** 로
            //   지목한 그 자리다:
            //       f len data      ← len 이 프렐류드(arity 1) 냐 지역(arity 0) 이냐로
            //                         **같은 글자가 두 개의 다른 나무**가 된다
            //   ★ 최상위 이름은 E-NAME-SHADOW 가 막고 있었는데(그래서 op 가림은 안전했다),
            //     **빌트인만 구멍이었다.** 하류의 타입 검사가 우연히 잡아 준 것뿐이다.
            //   ⇒ 머리/원자 비트를 **전역적으로 유일하게** 만든다. 그러면 구조가 Γ 에
            //     흔들리지 않는다 — 문법을 안 바꾸고 P1 의 마지막 조각을 닫는다.
            if (low_ir_is_builtin_name(nm) || ck_effect_word(nm))
                emit(out, "E-NAME-BUILTIN",
                     "a local takes the name of a BUILTIN op. The namespace is FLAT (no shadowing), "
                     "so this name now means two things — and which one it means decides how the "
                     "SENTENCE IS BRACKETED: `f len data` reads as `f(len(data), …)` if `len` is the "
                     "builtin (arity 1) and as `f(len, data)` if it is your local (arity 0). Same "
                     "letters, different tree, no error. That is RFC-0046 P1. Rename the local",
                     nd->kids[1]->tok.line);
            for (proven_size_t i = 0; i < ntop; i++)
                if (proven_u8str_view_eq(top[i], nm)) {
                    emit(out, "E-NAME-SHADOW",
                         "a local binding takes the name of a top-level declaration — the namespace "
                         "is FLAT (SPEC: no shadowing), so this does not shadow it cleanly: the "
                         "resolver silently picks one of the two. Rename the local",
                         nd->kids[1]->tok.line);
                    break;
                }
        }
    }
    for (proven_size_t i = 0; i < nd->nkids; i++) ck_no_shadow(out, nd->kids[i], top, ntop);
}


// ═══ **가림 전면 금지** · **낱말은 이름이 아니다** (SPEC-002 §2.10 · 부록 K · WO-0074) ═══
//
// ★★★ 정본은 *"겹치는 scope 에서 동명 재바인딩 금지 → 한 이름이 한 scope 에서 정확히 한 대상"*
//   이라 적었는데, 도구는 **둘만** 막고 있었다(모듈 이름 · 내장 op 이름). 매개변수를 가리는 것,
//   같은 블록에서 같은 이름을 두 번 묶는 것, 안쪽 블록이 바깥 지역을 가리는 것은 **통과했다.**
//   ⇒ 정본이 도구보다 넓게 말하는 상태였고, 그것이 이 저장소가 가장 싫어하는 모양이다
//     (**검사되지 않는 중복**). 소유자 결정(2026-08-24): **도구를 정본에 맞춘다.**
//
// ★★ `ck_no_shadow` 는 나무를 **평평하게** 훑는다 — 그래서 범위를 물을 수가 없었다.
//   여기서는 **블록 깊이를 들고** 훑는다: `do … end` 에 들어갈 때 깊이가 늘고, 나올 때
//   그 깊이에서 묶인 이름들을 **버린다**. 그래야 *"바깥을 가렸다"* 와 *"같은 블록에서 두 번"* 을
//   갈라 말할 수 있다 — 사람에게 그 둘은 다른 실수다.
//
// ★ **낱말**(`match` · `return` · `while` …)을 이름으로 **선언하는 것**도 여기서 막는다.
//   전에는 선언이 통과하고 **쓸 때** `E-IR-UNSUP`("S5 코어 밖의 원자")로 막혔는데, 그 말은
//   *"낱말을 이름으로 썼다"* 를 **말하지 않는다**. 원인을 짚지 않는 진단은 진단이 아니다.
//   (같은 부류를 이미 `E-NAME-BUILTIN` 이 제대로 하고 있다 — 낱말도 같은 대접을 받는다.)
#define CK_MAXBIND 256
typedef struct { proven_u8str_view_t nm; proven_size_t depth; proven_u32 line; } ck_scb_t;

static void ck_name_decl(low_check_result_t *out, const low_cst_t *na,
                         ck_scb_t *b, proven_size_t *n, proven_size_t depth,
                         const proven_u8str_view_t *params, proven_size_t nparams) {
    proven_u8str_view_t nm = na->tok.lex;
    if (na->tok.kw != LOW_KW_NONE) {
        emit(out, "E-NAME-KEYWORD",
             "a KEYWORD is used as a name. The 43 reserved words are not identifiers: they are "
             "how the reader tells structure from data at a glance. Declaring `let match … .` "
             "used to be accepted here and to fail only where the name was USED, with a message "
             "that never said why. Rename the binding",
             na->tok.line);
        return;   // 낱말이면 여기까지 — 가림까지 겹쳐 말하면 소음이다
    }
    for (proven_size_t i = 0; i < nparams; i++)
        if (proven_u8str_view_eq(params[i], nm)) {
            emit(out, "E-NAME-SHADOW",
                 "a local binding takes the name of a PARAMETER of the same op. The namespace is "
                 "FLAT (SPEC-002 2.10: no shadowing) — from here on the same letters mean the new "
                 "binding, and a reader who saw the signature will read the wrong value. "
                 "Rename the local",
                 na->tok.line);
            return;
        }
    for (proven_size_t i = *n; i > 0; i--)
        if (proven_u8str_view_eq(b[i - 1].nm, nm)) {
            if (b[i - 1].depth == depth)
                emit(out, "E-NAME-SHADOW",
                     "the same name is bound TWICE in the same block. One name means exactly one "
                     "thing in one scope (SPEC-002 2.10) — a second binding does not replace the "
                     "first, it makes the letters ambiguous to the reader. Rename one of them",
                     na->tok.line);
            else
                emit(out, "E-NAME-SHADOW",
                     "an inner block binds a name that is already bound OUTSIDE it. The namespace "
                     "is FLAT (SPEC-002 2.10: no shadowing) — the outer value is still alive and "
                     "the same letters now point at a different one. Rename the inner binding",
                     na->tok.line);
            return;
        }
    if (*n < CK_MAXBIND) { b[*n].nm = nm; b[*n].depth = depth; b[*n].line = na->tok.line; (*n)++; }
}

static bool ck_atom(const low_cst_t *n);   // ★ 아래 정의를 앞당겨 쓴다

// ★★★★★ **붙은 점 필드 접근을 여기서도 문다** (2026-08-25 · 소유자 결정).
//   하강(`ir_glued_local`)에도 같은 판정이 있는데, 거기는 **제네릭 op 안을 못 본다** —
//   `comptime` op 은 쓰이는 자리에서만 실체화되므로 라이브러리를 홀로 검사하면 그 몸이
//   낮아지지 않는다(실측: `lib/mapgen.low` 의 자리들이 안 보였다).
//   ⇒ 검사층은 **모든 op 의 몸을 한 번은 훑는다.** 그래서 여기 두면 제네릭도 걸린다.
//   ★ 판정 기준은 하강과 **같다**: 첫 마디가 **지역/파라미터**면 필드 접근이다.
//     모듈 한정·변형 이름·선언 이름은 지역이 아니므로 여기 안 걸린다.
static bool ck_is_bound(const ck_scb_t *b, proven_size_t n, proven_u8str_view_t nm,
                        const proven_u8str_view_t *params, proven_size_t nparams) {
    for (proven_size_t i = 0; i < nparams; i++)
        if (proven_u8str_view_eq(params[i], nm)) return true;
    for (proven_size_t i = 0; i < n; i++)
        if (proven_u8str_view_eq(b[i].nm, nm)) return true;
    return false;
}

static void ck_glued_field(low_check_result_t *out, const low_cst_t *nd,
                           const ck_scb_t *b, proven_size_t n,
                           const proven_u8str_view_t *params, proven_size_t nparams) {
    if (!ck_atom(nd) || nd->tok.kind != LOW_TOK_IDENT) return;
    proven_u8str_view_t v = nd->tok.lex;
    proven_size_t dot = 0;
    for (proven_size_t i = 0; i < v.size; i++) if (v.ptr[i] == '.') { dot = i; break; }
    if (!dot || dot + 1 >= v.size) return;
    proven_u8str_view_t head = { .ptr = v.ptr, .size = dot };
    if (!ck_is_bound(b, n, head, params, nparams)) return;   // 모듈·변형·선언 이름이다
    char nb[160]; proven_size_t nn = v.size < sizeof nb - 1 ? v.size : sizeof nb - 1;
    memcpy(nb, v.ptr, nn); nb[nn] = 0;
    char sp[160]; proven_size_t sn = 0;
    for (proven_size_t z = 0; z < nn && sn < sizeof sp - 1; z++)
        sp[sn++] = nb[z] == '.' ? ' ' : nb[z];
    sp[sn] = 0;
    if (getenv("LOW_LIST_GLUED2")) { fprintf(stderr, "GLUED2\t%u\t%s\t(field %s)\n",
                                            (unsigned)nd->tok.line, nb, sp); return; }
    if (getenv("LOWENT_ALLOW_GLUED_FIELD")) return;   // ★ 되돌릴 문 (기본은 거절)
    emit_at(out, "E-FIELD-GLUED",
            "glued-dot field access is gone — write `(field <value> <name>…)` instead. One "
            "meaning gets one spelling: the dot still means module qualification (`mod.name`), "
            "a variant name (`err.too_short`) and a type-associated declaration (`fn pt.twice`), "
            "so a fourth meaning made the same letters mean four things. `field` chains: "
            "`(field o i z)`", nd);
    {   // ★ 그 이름을 **detail 에 복사**해 둔다 — 진단이 어느 것인지 스스로 말하게.
        low_diag_t *last = (low_diag_t *)out->diags.data + (out->diags.len - 1);
        snprintf(last->detail, sizeof last->detail, "`%s` → `(field %s)`", nb, sp);
    }
}

static void ck_scope_walk(low_check_result_t *out, const low_cst_t *nd,
                          ck_scb_t *b, proven_size_t *n, proven_size_t depth,
                          const proven_u8str_view_t *params, proven_size_t nparams) {
    if (!nd) return;
    ck_glued_field(out, nd, b, *n, params, nparams);
    if (nd->kind == LOW_CST_FORM && nd->nkids >= 2 && nd->kids[0]->kind == LOW_CST_ATOM &&
        nd->kids[1]->kind == LOW_CST_ATOM) {
        proven_u8str_view_t h = nd->kids[0]->tok.lex;
        if (veq(h, "var") || veq(h, "let") || veq(h, "for"))
            ck_name_decl(out, nd->kids[1], b, n, depth, params, nparams);
    }
    proven_size_t d = depth + (nd->kind == LOW_CST_BLOCK ? 1 : 0);
    proven_size_t mark = *n;
    for (proven_size_t i = 0; i < nd->nkids; i++)
        ck_scope_walk(out, nd->kids[i], b, n, d, params, nparams);
    if (nd->kind == LOW_CST_BLOCK) *n = mark;   // 블록을 나가면 그 안의 이름은 사라진다
}


// ═══ **모든 길이 값을 돌려주는가** (RFC-0019 G-TOTAL · WO-0074) ═══════════════
//
// ★★★ 값을 낸다고 적어 놓고 어떤 길에 `return` 이 없으면, 도구가 **0 을 채워 넣었다** —
//   그리고 그 0 은 **소스 어디에도 없다**. 이 언어는 미정의 동작을 없앴다고 말하는데,
//   여기서는 저자가 안 쓴 값을 조용히 내보내고 있었다(PRINCIPLES.md §0 의 의미 엔트로피).
//
// ★★ 검사는 **갈래를 빠짐없이 적은 `match`** 를 값을 내는 것으로 인정해야 한다. 안 그러면
//   옳은 프로그램이 깨진다(실측: 코퍼스의 세 자리가 전부 그 모양이었다). `match` 가
//   빠짐없다는 것은 이미 `E-MATCH-INEXHAUSTIVE` 가 따로 지키므로, 여기서는 **모든 갈래가
//   값을 내는가**만 물으면 된다.
//
// ★ 여기서 세는 것은 **문장의 모양**이지 값이 아니다 — 조건이 참인지 거짓인지는 안 본다.
//   그래서 이 검사는 **보수적이다**: 통과시키면 확실히 안전하고, 막을 때 사람이 `return` 을
//   한 줄 더 적으면 된다. 조건을 따져 "여기는 못 온다"고 우기는 쪽이 위험하다.
static bool ck_blk_returns(const low_cst_t *blk);

static bool ck_stmt_returns(const low_cst_t *f) {
    if (!f) return false;
    if (f->kind == LOW_CST_BLOCK) return ck_blk_returns(f);
    // ★ 묶음(`( … )`)과 합성 묶음은 **속을 그대로 본다** — 파서가 머리 표시·메서드를 위해
    //   만든 GROUP 이 그 안에 옮겨진 문장을 품고 있을 수 있다(아래 ★★★★ 참조).
    if (f->kind == LOW_CST_GROUP) {
        for (proven_size_t i = 0; i < f->nkids; i++)
            if (ck_stmt_returns(f->kids[i])) return true;
        return false;
    }
    if (f->kind != LOW_CST_FORM || !f->nkids || !ck_atom(f->kids[0])) return false;
    low_kw_t k = f->kids[0]->tok.kw;
    if (k == LOW_KW_RETURN) return true;
    if (k == LOW_KW_MATCH) {
        const low_cst_t *arms = f->kids[f->nkids - 1];
        if (arms->kind != LOW_CST_BLOCK || !arms->nkids) return false;
        bool any = false;
        for (proven_size_t q = 0; q < arms->nkids; q++) {
            const low_cst_t *arm = arms->kids[q];
            if (arm->kind != LOW_CST_FORM || !arm->nkids || !ck_atom(arm->kids[0]) ||
                arm->kids[0]->tok.kw != LOW_KW_CASE) continue;
            any = true;
            if (!ck_stmt_returns(arm->kids[arm->nkids - 1])) return false;   // 한 갈래라도 안 내면 아니다
        }
        return any;
    }
    if (k == LOW_KW_IF) {
        proven_size_t b = f->nkids;
        for (proven_size_t z = 1; z < f->nkids; z++)
            if (f->kids[z]->kind == LOW_CST_BLOCK) { b = z; break; }
        if (b == f->nkids || b + 1 >= f->nkids) return false;   // else 가 없으면 빠져나가는 길이 있다
        return ck_stmt_returns(f->kids[b]) && ck_stmt_returns(f->kids[b + 1]);
    }
    // `unsafe do … end` · `region … do … end` 처럼 **반드시 한 번 도는** 몸은 그 몸을 따른다.
    if (k == LOW_KW_UNSAFE || (k == LOW_KW_NONE && ck_atom(f->kids[0]) &&
                               veq(f->kids[0]->tok.lex, "region")))
        return f->nkids && ck_stmt_returns(f->kids[f->nkids - 1]);
    // ★★★★ **합성된 머리가 저자의 점을 먹으면 뒤 문장이 자식으로 들어간다** (2026-08-25).
    //   `.f`(머리 표시)와 `recv..m`(메서드)은 파서가 만든 폼이고, **저자가 찍은 점을 그
    //   머리의 것으로 가져간다**. 그러면 `let u be u64 .g 3 .` 다음의 `return u .` 이
    //   형제가 아니라 **`let` 폼의 자식**이 된다 — 그리고 이 검사가 그것을 못 보고
    //   *"값을 안 내는 길이 있다"* 고 **오진했다**(2026-08-25 실측: `.f` 와 `..` 둘 다).
    //
    //   ★ 내려가도 안전한 이유: `return` 은 **문장이지 값이 아니다.** 값 자리에 올 수 없으므로,
    //     문장 폼의 자식에서 `return` 을 보면 그것은 **파서가 옮겨 놓은 문장**이다.
    //   ★★ 다만 **제어 폼에는 안 내려간다** — `guard c . else return x .` 의 `return` 은
    //     옮겨진 것이 아니라 **그 갈래의 것**이고, 그것을 "이 문장이 값을 낸다" 로 읽으면
    //     이번엔 반대 방향으로 틀린다.
    for (proven_size_t i = 1; i < f->nkids; i++)
        if (ck_stmt_returns(f->kids[i])) return true;
    return false;   // while · guard · 그 밖의 문장은 빠져나가는 길이 있다
}

static bool ck_blk_returns(const low_cst_t *blk) {
    if (!blk) return false;
    for (proven_size_t i = 0; i < blk->nkids; i++)
        if (ck_stmt_returns(blk->kids[i])) return true;   // 뒤는 죽은 코드다
    return false;
}

// 이 op 이 **값을 낸다고 적었는가** — `output <타입>` 이 있고 그 타입이 `void` 가 아닌가.
//   ★ 인라인 asm op 은 빼 준다: 몸이 **기계 명령**이고 값은 `out reg r` 이 낸다 —
//     `return` 문장이 없는 것이 정상이다(RFC-0041). 실측으로 걸려서 알았다.
static bool ck_op_yields_value(const low_cst_t *f) {
    for (proven_size_t j = 1; j < f->nkids; j++)
        if (ck_atom(f->kids[j]) && veq(f->kids[j]->tok.lex, "asm")) return false;
    for (proven_size_t j = 1; j + 1 < f->nkids; j++)
        if (ck_atom(f->kids[j]) && veq(f->kids[j]->tok.lex, "output")) {
            const low_cst_t *t = f->kids[j + 1];
            if (ck_atom(t) && veq(t->tok.lex, "void")) return false;
            return true;
        }
    return false;
}



// ★★★ **한 op 의 이름들을 본다** — 최상위 op 만 보던 것을 함수로 뽑아 **액터 안까지** 부른다.
//   ☞ 실측(2026-08-24, WO-0074): `actor` 안의 `fn`/`proc` 은 이 검사들이 **아예 안 보고 있었다**
//     — 새로 넣은 가림 검사도, 원래 있던 `E-NAME-BUILTIN`·`E-NAME-SHADOW` 도 그랬다.
//     최상위 폼만 훑었기 때문이다. **안 보는 자리는 통과하는 자리와 구별되지 않는다.**
static void ck_op_names(low_check_result_t *out, const low_cst_t *f,
                        const proven_u8str_view_t *mine, proven_size_t nmine) {
    const low_cst_t *blk = NULL;
    for (proven_size_t z = f->nkids; z > 0; z--)
        if (f->kids[z - 1]->kind == LOW_CST_BLOCK) { blk = f->kids[z - 1]; break; }
    if (!blk) return;
    ck_no_shadow(out, blk, mine, nmine);
    // ★ 범위를 따지는 한 벌 더 — 매개변수를 모아서 준다(WO-0074).
    //   ☞ 매개변수 이름 자체도 낱말이면 안 된다: 여기서 함께 본다.
    proven_u8str_view_t ps[LOW_MAX_PARAMS];
    proven_size_t np = 0;
    for (proven_size_t j = 1; j + 1 < f->nkids; j++)
        if (ck_atom(f->kids[j]) && veq(f->kids[j]->tok.lex, "input")) {
            proven_size_t k = j + 1;
            while (k + 1 < f->nkids && ck_atom(f->kids[k]) &&
                   (veq(f->kids[k]->tok.lex, "comptime") || veq(f->kids[k]->tok.lex, "mut"))) k++;
            if (!ck_atom(f->kids[k])) continue;
            if (f->kids[k]->tok.kw != LOW_KW_NONE)
                emit(out, "E-NAME-KEYWORD",
                     "a KEYWORD is used as a PARAMETER name. The 43 reserved words are not "
                     "identifiers — rename the parameter",
                     f->kids[k]->tok.line);
            else if (np < LOW_MAX_PARAMS) { ps[np++] = f->kids[k]->tok.lex; LOW_HWM("check:params", np, LOW_MAX_PARAMS); }
        }
    ck_scb_t binds[CK_MAXBIND];
    proven_size_t nb = 0;
    ck_scope_walk(out, blk, binds, &nb, 0, ps, np);
    if (ck_op_yields_value(f) && !ck_blk_returns(blk)) {
        ck_cur_form = f;   // ★ 진단에 **파일 이름**을 싣는다(결함 노트 #48)
        emit(out, "E-RETURN-PARTIAL",
             "this op says it OUTPUTS a value, but some path through its body reaches the end "
             "without a `return`. Until now the tool quietly returned 0 there — a value that "
             "appears NOWHERE in your source (RFC-0019 G-TOTAL). Give every path a `return`, or "
             "say `output void` if it really produces nothing. An exhaustive `match` whose every "
             "arm returns counts as returning",
             f->line);
    }
}

// ★ 액터·트레이트 몸 안의 op 까지 내려가며 위를 부른다.
static void ck_op_names_deep(low_check_result_t *out, const low_cst_t *nd,
                             const proven_u8str_view_t *mine, proven_size_t nmine) {
    if (!nd) return;
    if (nd->kind == LOW_CST_FORM && nd->nkids && ck_atom(nd->kids[0])) {
        low_kw_t k = nd->kids[0]->tok.kw;
        if (k == LOW_KW_FN || k == LOW_KW_PROC) { ck_op_names(out, nd, mine, nmine); return; }
    }
    for (proven_size_t i = 0; i < nd->nkids; i++) ck_op_names_deep(out, nd->kids[i], mine, nmine);
}


// ── 계약 절의 **이름은 무언가를 가리켜야 한다** (PRINCIPLES.md §0) ────────────────
// `tests`·`access`·`parallel` 은 이름을 적는다. 그런데 그 이름이 아무것도 가리키지 않아도
// 조용히 통과했다 — 존재하지 않는 op, 존재하지 않는 파라미터를 적어도 됐다.
// **선언된 중복인데 아무도 교차 검사하지 않는다.** 그것이 곧 엔트로피다.
static bool ck_is_param(const low_cst_t *f, proven_u8str_view_t name) {
    for (proven_size_t j = 2; j + 1 < f->nkids; j++)
        if (f->kids[j]->kind == LOW_CST_ATOM && veq(f->kids[j]->tok.lex, "input") &&
            f->kids[j + 1]->kind == LOW_CST_ATOM &&
            proven_u8str_view_eq(f->kids[j + 1]->tok.lex, name)) return true;
    return false;
}
static bool ck_is_op(const low_opinfo_t *tab, proven_size_t n, proven_u8str_view_t name) {
    for (proven_size_t i = 0; i < n; i++)
        if (proven_u8str_view_eq(tab[i].name, name)) return true;
    return false;
}
static bool ck_access_mode(proven_u8str_view_t v) {   // SPEC-002 §부록 K 의 7가지
    return veq(v, "sequential") || veq(v, "random") || veq(v, "streaming") || veq(v, "tiled") ||
           veq(v, "read_mostly") || veq(v, "write_only") || veq(v, "shared_read");
}
static bool ck_clause_word(proven_u8str_view_t v) { return low_is_clause_word(v); }  // ★ 하나의 어휘

// ★ `access` 의 두 모드는 **스케줄링 힌트가 아니라 읽기/쓰기 규율**이다:
//     shared_read — 이 op 은 그 자리에 **쓰면 안 된다**(공유해서 읽는다는 뜻이니까)
//     write_only  — 이 op 은 그 자리를 **읽으면 안 된다**
//   나머지(sequential/random/streaming/tiled/read_mostly)는 커널 스케줄 힌트라 아직 의미가 없다.
//   ⇒ **지금 강제할 수 있는 것은 지금 강제한다.** 못 하는 것만 W-NOT-YET 로 말한다.
//     (그 둘을 한 덩어리로 "아직" 이라고 하면, 잡을 수 있는 거짓말을 놓친다.)
// ★★ **호출자 저장소에 쓰면 `state` 다** (결함 노트 #4 · #16 · #63, 2026-09-16).
//   `mut`/`mut_ref` 매개변수와 `collect into <매개변수>` 는 **부르는 쪽이 보는 자리**에 쓴다.
//   순수성 쪽(E-EFFECT-PURITY)은 그것을 이미 그렇게 판정하는데 효과 계수는 안 세어,
//   같은 몸을 `proc … effects state .` 로 적으면 «선언했는데 안 한다»(W-EFFECT-OVER)가 났다.
//   요구와 경고가 서로를 부정하면 둘 중 하나는 거짓말이다 — 한 판정으로 모은다.
static bool ck_writes_place(const low_cst_t *nd, proven_u8str_view_t name);
static bool ck_writes_name(const low_cst_t *nd, proven_u8str_view_t name);
static bool ck_writes_caller_storage(const low_cst_t *f) {
    if (!f) return false;
    low_op_header_t h = low_op_header(f);
    if (!h.body) return false;
    for (proven_size_t q = 0; q < h.np; q++) {
        bool mut_like = h.p[q].is_mut;
        for (proven_size_t k2 = h.p[q].ts; k2 < h.p[q].te && k2 < f->nkids; k2++)
            if (f->kids[k2]->kind == LOW_CST_ATOM &&
                (veq(f->kids[k2]->tok.lex, "mut_ref") || veq(f->kids[k2]->tok.lex, "mut")))
                mut_like = true;
        if (!mut_like) continue;
        if (ck_writes_place(h.body, h.p[q].name) || ck_writes_name(h.body, h.p[q].name)) return true;
    }
    return false;
}

// ★ **이름에 직접 쓰는가** — `set v expr v + 1 . .`
//   `ck_writes_place` 는 `set (index X …)` 처럼 **장소를 거친** 쓰기만 본다(`mut` 파라미터가
//   그 모양이다). 그런데 **액터 상태는 이름에 바로 쓴다.** 그 서술어가 없어서 상태 쓰기가
//   순수성 검사에 **보이지 않았다** — `on` 이 1비트를 우회한 것과 겹쳐 **두 겹으로 안 보였다.**
static bool ck_writes_name(const low_cst_t *nd, proven_u8str_view_t name) {
    if (!nd) return false;
    if (nd->kind == LOW_CST_FORM && nd->nkids >= 2 && nd->kids[0]->kind == LOW_CST_ATOM &&
        nd->kids[0]->tok.kw == LOW_KW_SET && nd->kids[1]->kind == LOW_CST_ATOM &&
        proven_u8str_view_eq(nd->kids[1]->tok.lex, name)) return true;
    for (proven_size_t i = 0; i < nd->nkids; i++)
        if (ck_writes_name(nd->kids[i], name)) return true;
    return false;
}
static bool ck_writes_place(const low_cst_t *nd, proven_u8str_view_t name) {
    if (!nd) return false;
    if (nd->kind == LOW_CST_FORM && nd->nkids >= 3 && nd->kids[0]->kind == LOW_CST_ATOM &&
        nd->kids[0]->tok.kw == LOW_KW_SET && nd->kids[1]->kind == LOW_CST_GROUP &&
        nd->kids[1]->nkids == 1) {
        const low_cst_t *pl = nd->kids[1]->kids[0];
        if (pl->kind == LOW_CST_FORM && pl->nkids >= 2 && pl->kids[0]->kind == LOW_CST_ATOM &&
            pl->kids[1]->kind == LOW_CST_ATOM &&
            (veq(pl->kids[0]->tok.lex, "index") || veq(pl->kids[0]->tok.lex, "field")) &&
            proven_u8str_view_eq(pl->kids[1]->tok.lex, name)) return true;
    }
    // ★ `map <out> …` · `filter <out> …` 도 out(첫 인자)에 **쓴다** — 관측적 순수성(RFC-0057)이
    //   이 쓰기를 봐야 한다. 그래서 mut 파라미터에 map/filter 하는 fn 은 E-EFFECT-PURITY 로 잡힌다.
    if (nd->kind == LOW_CST_FORM && nd->nkids >= 2 && nd->kids[0]->kind == LOW_CST_ATOM &&
        (veq(nd->kids[0]->tok.lex, "map") || veq(nd->kids[0]->tok.lex, "filter")) &&
        nd->kids[1]->kind == LOW_CST_ATOM && proven_u8str_view_eq(nd->kids[1]->tok.lex, name)) return true;
    // ★★★ `collect into <자리>` 도 그 자리에 **쓴다** (결함 노트 #16, 2026-09-16). 호출자 저장소
    //   쓰기의 효과 판정이 세 갈래로 갈려 있었다: `mut slice` 원소 쓰기는 `state` 를 요구하고,
    //   `collect into` 는 세지 않으며(`effects state` 면 «선언했는데 안 한다»), `mut_ref` 는
    //   이름으로 쓰므로 아무도 안 봤다. 한 규칙으로 센다 — 쓰는 자리는 다 쓰는 것이다.
    if (nd->kind == LOW_CST_FORM && nd->nkids >= 3 && nd->kids[0]->kind == LOW_CST_ATOM &&
        veq(nd->kids[0]->tok.lex, "collect") && nd->kids[1]->kind == LOW_CST_ATOM &&
        veq(nd->kids[1]->tok.lex, "into") && nd->kids[2]->kind == LOW_CST_ATOM &&
        proven_u8str_view_eq(nd->kids[2]->tok.lex, name)) return true;
    // ★ `store <dst> <idx> <vec>`·`store_masked <dst> <idx> <vec> <mask>` (RFC-0040) 도 dst(첫 인자)에 **쓴다**
    //   — 관측적 순수성이 봐야 한다(fn 금지). masked store 는 켜진 lane 만 쓰지만 여전히 메모리 쓰기다.
    if (nd->kind == LOW_CST_FORM && nd->nkids >= 2 && nd->kids[0]->kind == LOW_CST_ATOM &&
        (veq(nd->kids[0]->tok.lex, "store") || veq(nd->kids[0]->tok.lex, "store_masked")) &&
        nd->kids[1]->kind == LOW_CST_ATOM && proven_u8str_view_eq(nd->kids[1]->tok.lex, name)) return true;
    for (proven_size_t i = 0; i < nd->nkids; i++)
        if (ck_writes_place(nd->kids[i], name)) return true;
    return false;
}
// 읽기 — 쓰기의 **장소**(set 의 괄호 그룹) 안이 아닌 곳에 그 이름이 나오면 읽은 것이다.
static bool ck_reads_name(const low_cst_t *nd, proven_u8str_view_t name) {
    if (!nd) return false;
    if (nd->kind == LOW_CST_ATOM) return proven_u8str_view_eq(nd->tok.lex, name);
    bool is_set_place = (nd->kind == LOW_CST_FORM && nd->nkids >= 3 &&
                         nd->kids[0]->kind == LOW_CST_ATOM && nd->kids[0]->tok.kw == LOW_KW_SET);
    for (proven_size_t i = 0; i < nd->nkids; i++) {
        if (is_set_place && i == 1) {
            // 장소 그룹: `(index s i)` — s 는 쓰기 대상이지만 **인덱스는 읽기**다.
            const low_cst_t *g = nd->kids[1];
            if (g->kind == LOW_CST_GROUP && g->nkids == 1) {
                const low_cst_t *pl = g->kids[0];
                for (proven_size_t q = 2; q < pl->nkids; q++)
                    if (ck_reads_name(pl->kids[q], name)) return true;
                continue;
            }
        }
        if (ck_reads_name(nd->kids[i], name)) return true;
    }
    return false;
}
// ★ RFC-0009 DET-3 — **결합적인 연산만 트리를 자유롭게 고를 수 있다.**
//   ★ assoc_shape_free (Qed): 결합적이면 트리 모양이 결과를 안 바꾼다.
//   ★ nonassoc_shape_matters (Qed): 결합적이지 않으면 **모양이 결과를 바꾼다.**
//   부동소수 덧셈은 결합적이지 않다 ⇒ 쪼개면 결과가 스케줄에 의존한다 ⇒ **거부한다.**
static bool ck_assoc_op(proven_u8str_view_t v) {
    return veq(v, "add") || veq(v, "mul") || veq(v, "min") || veq(v, "max") ||
           veq(v, "bit_and") || veq(v, "bit_or") || veq(v, "bit_xor");
}
static void ck_clause_names(low_check_result_t *out, const low_cst_t *f,
                            const low_opinfo_t *tab, proven_size_t nops,
                            proven_u8str_view_t modname) {
    for (proven_size_t i = 2; i < f->nkids; i++) {
        if (f->kids[i]->kind != LOW_CST_ATOM) continue;
        proven_u8str_view_t w = f->kids[i]->tok.lex;

        if (veq(w, "tests")) {                    // tests N* — 각 N 은 **존재하는 op** 여야 한다
            for (proven_size_t j = i + 1; j < f->nkids; j++) {
                if (f->kids[j]->kind != LOW_CST_ATOM) break;
                if (ck_clause_word(f->kids[j]->tok.lex)) break;
                if (!ck_is_op(tab, nops, f->kids[j]->tok.lex))
                    emit(out, "E-CONTRACT-UNDEF",
                         "the `tests` clause names an op that does not exist", f->line);
            }
        } else if (veq(w, "using")) {
            // ★★★★ RFC-0112 D8 — `using` 절은 **단형화 앞에서** 첫 실행 입력으로 바꿔 적힌다(`low_using.c`). 검사기에
            //   절이 남아 있다면 그 패스가 돌지 않은 것이다 — 나무를 끄는 대조 스위치(`--flat`)가 그렇다. 조용히
            //   «입력 하나 모자란 op» 로 검사하면 엉뚱한 진단이 나므로 여기서 말한다.
            emit(out, "E-USING-UNRESOLVED",
                 "this op still carries a `using` clause at check time, so the allocator was never threaded in. "
                 "`using` is resolved on the TREE, before monomorphisation; the `--flat` contrast switch does not "
                 "build the tree. Check without `--flat` (RFC-0112 D8)", f->line);
        } else if (veq(w, "effects")) {          // ★ effect 어휘는 닫혀 있다
            // ★★ **effect 절 위생** (RFC-0057 E2·E3, DECISION-0009) — 형제 E1(UNDEF)·E4·E5 는 이미 섰다.
            //   effect 행은 **리스트가 아니라 집합**이다: 중복 원자(io io)도, `none` 과 실효과의 혼재
            //   (none io)도 조용히 통과하면 안 된다. 둘 다 독자가 "무시해도 되나?" 를 스스로 판단하게 만든다.
            proven_u8str_view_t eff[16]; proven_size_t neff = 0;
            bool has_none = false, has_real = false, dup_done = false;
            for (proven_size_t j = i + 1; j < f->nkids; j++) {
                if (f->kids[j]->kind != LOW_CST_ATOM) break;
                proven_u8str_view_t a = f->kids[j]->tok.lex;
                if (ck_clause_word(a)) break;
                if (veq(a, "via")) { j++; continue; }        // ★ RFC-0112 D7 — `via <타입>` 은 원자가 아니다
                if (!effect_word_known(a))
                    emit(out, "E-EFFECT-UNDEF",
                         "unknown effect (the vocabulary is closed: none/alloc/heap/io/wait/concurrent/lock/"
                         "atomic/unsafe/device/page_fault/blocking/cancel/detach/panic/state) — "
                         "a typo here silently declares the op PURE", f->line);
                if (veq(a, "none")) has_none = true; else has_real = true;
                for (proven_size_t k = 0; k < neff && !dup_done; k++)
                    if (proven_u8str_view_eq(eff[k], a)) {
                        emit(out, "E-EFFECT-DUP",
                             "an effect atom appears more than once in the `effects` clause — the row "
                             "is a SET, not a list. A repeat says nothing the first one didn't, and "
                             "the reader has to decide it's noise (RFC-0057 E3)", f->line);
                        dup_done = true;
                    }
                if (neff < 16) eff[neff++] = a;
            }
            if (has_none && has_real)
                emit(out, "E-EFFECT-NONE-MIX",
                     "`none` sits in the `effects` clause ALONGSIDE a real effect — `none` means this "
                     "op performs no effects, so it cannot co-occur with one. Say the effects, or say "
                     "none; not both (RFC-0057 E2)", f->line);
        } else if (veq(w, "invalidates")) {
            // ★★★★ RFC-0116 D4 I (소유자 결정, 2026-09-22) — `invalidates <입력> .` : «이 op 은 그 입력에서 나온 뷰를
            //   무효로 만든다»(블록을 돌려준다 · 자라며 옮긴다 · 되감는다). 이름 **하나**, 이 op 의 입력이어야 한다.
            proven_size_t n = 0; proven_u8str_view_t nm1 = { 0 }; bool bad = false;
            for (proven_size_t j = i + 1; j < f->nkids; j++) {
                if (f->kids[j]->kind != LOW_CST_ATOM || ck_clause_word(f->kids[j]->tok.lex)) break;
                if (f->kids[j]->tok.kind != LOW_TOK_IDENT) { bad = true; break; }
                if (n == 0) nm1 = f->kids[j]->tok.lex;
                n++;
            }
            if (bad || n != 1 || !ck_is_param_name(f, nm1))
                emit(out, "E-INVALIDATES-FORM",
                     "an `invalidates` clause names exactly ONE input of this op — the storage whose views the op "
                     "makes invalid (releases a block, grows and moves, rewinds): `invalidates p .` (RFC-0116 D4)", f->line);
        } else if (veq(w, "absorbs")) {
            // ★★★★★ RFC-0120 (소유자 결정 2026-09-23) — `absorbs machine <이름> .` : «처리기가 못 보는 일을
            //   **여기서 멈춘다**». 그 op 은 `unsafe` 를 시그니처에 싣지 않고, 몸 안에서 <이름> 이 `cap machine` 이 된다.
            //   ☞ 번짐 자체는 옳다(정본 §7.2.1(4)). 문제는 **멈추는 자리가 없다**는 것이었고, 그래서 `lib/` 의 asm 사용이 0 이었다.
            //   ★ 값으로 치르는 대가가 이 검사들이다 — 하나라도 빠지면 흡수는 성립하지 않는다.
            proven_size_t n = 0; proven_u8str_view_t kindw = { 0 }, bindn = { 0 }; bool bad = false;
            for (proven_size_t j = i + 1; j < f->nkids; j++) {
                if (f->kids[j]->kind != LOW_CST_ATOM || ck_clause_word(f->kids[j]->tok.lex)) break;
                if (f->kids[j]->tok.kind != LOW_TOK_IDENT) { bad = true; break; }
                if (n == 0) kindw = f->kids[j]->tok.lex;
                else if (n == 1) bindn = f->kids[j]->tok.lex;
                n++;
            }
            // ① 범위 — `machine` 하나뿐이다. 세상에 닿는 권한은 흡수하지 못한다(RFC-0120 §5.2-1).
            if (bad || n != 2 || !veq(kindw, "machine"))
                emit(out, "E-ABSORB-SCOPE",
                     "an `absorbs` clause reads `absorbs machine <name> .` — and `machine` is the ONLY thing that may be "
                     "absorbed. A capability that touches the world (`c`, `io`, `net`, `file_system`, `heap`, `mmio`, "
                     "`clock`, `random`) is never absorbed: absorbing it would MINT authority the caller cannot see. "
                     "The name is bound inside the body as the `cap machine` this op vouches for (RFC-0120 §5.2)", f->line);
            else {
                low_op_header_t ah = low_op_header(f);
                // ② 순수 — 흡수 뒤 효과 줄은 비어 있어야 한다.
                bool any_eff = false, none_eff = false;
                for (proven_size_t j = ah.eff_s; j < ah.eff_e; j++) {
                    if (!ck_atom(f->kids[j])) continue;
                    if (veq(f->kids[j]->tok.lex, "none")) none_eff = true;
                    else if (!veq(f->kids[j]->tok.lex, "effects")) any_eff = true;
                }
                if (any_eff || !none_eff)
                    emit(out, "E-ABSORB-IMPURE",
                         "an op that absorbs `machine` must end up PURE — `effects none .`. Absorption stops the "
                         "`unsafe` mark, not the rest: if this op also does io, allocates or touches state, the caller "
                         "still has to know, and mixing the two makes the boundary unreadable (RFC-0120 §5.2-2)", f->line);
                // ③ 참조 구현 · ④ 계약 · ⑤ 근거 — 셋 다 있어야 한다.
                bool has_ref = false, has_why = false, has_req = false, why_empty = false;
                for (proven_size_t j = 2; j + 1 < f->nkids; j++) {
                    if (!ck_atom(f->kids[j])) continue;
                    if (veq(f->kids[j]->tok.lex, "reference") && ck_atom(f->kids[j + 1])) has_ref = true;
                    if (veq(f->kids[j]->tok.lex, "requires")) has_req = true;
                    if (veq(f->kids[j]->tok.lex, "why")) {
                        has_why = true;
                        if (f->kids[j + 1]->kind == LOW_CST_ATOM &&
                            f->kids[j + 1]->tok.kind == LOW_TOK_STRING && f->kids[j + 1]->tok.lex.size == 0)
                            why_empty = true;
                    }
                }
                if (!has_ref)
                    emit(out, "E-ABSORB-NOREF",
                         "an absorbing op must name a PURE-LOWENT reference implementation — `reference <op> .`. "
                         "Without it there is no second place that computes the same answer, and then nothing can tell "
                         "«right» from «consistently wrong»: a wrong GHASH reduction once passed a seal→open round-trip "
                         "for exactly that reason (RFC-0120 §5.2-3)", f->line);
                if (!has_req)
                    emit(out, "E-ABSORB-NOCONTRACT",
                         "an absorbing op must state what it assumes at the boundary — at least one `requires` clause "
                         "(lengths, alignment, ranges). Inside, the tool cannot see; at the door, it can and does "
                         "(RFC-0120 §5.2-4)", f->line);
                // ⑩ 자리 제한 — 매니페스트가 이름을 적은 모듈만 흡수할 수 있다(RFC-0120 §5.2-10).
                {
                    proven_u8str_view_t mod = modname;
                    if (!ck_absorb_place_ok(mod)) {
                        // ★ 진단은 문구를 **가리킨다** — 스택 버퍼를 주면 그 자리가 사라진 뒤 빈 줄이 찍힌다(실측).
                        static char pbuf[8][460]; static int pbn;
                        char *pb = pbuf[pbn++ & 7];
                        snprintf(pb, 460,
                                 "this op absorbs `machine`, but the manifest does not allow module `%.*s` to. "
                                 "Absorption says a HUMAN vouches for what the tool cannot see, so WHO may vouch "
                                 "is decided by the project's identity file, not by the source that wants the "
                                 "right: add `build absorb %.*s .` to pkg.low (and sign the row in the absorb "
                                 "registry). With no manifest at all, nobody may absorb (RFC-0120 §5.2-10)",
                                 (int)mod.size, mod.size ? (const char *)mod.ptr : "",
                                 (int)mod.size, mod.size ? (const char *)mod.ptr : "");
                        emit(out, "E-ABSORB-PLACE", pb, f->line);
                    }
                }
                if (!has_why || why_empty)
                    emit(out, "E-ABSORB-NOWHY",
                         "an absorbing op must write down WHY the absorbed code is safe — `why \"…\" .`, not empty. "
                         "This sentence is what a reviewer signs in `docs/operations/absorb-registry.md`; a promise "
                         "nobody wrote down is one nobody can check (RFC-0120 §5.2-5)", f->line);
            }
        } else if (veq(w, "inplace")) {
            // ★★★★ RFC-0116 D2 B1 (소유자 결정 ⓑ, 2026-09-22) — `inplace <쓰기 입력> <읽기 입력> .`
            //   «이 둘이 같은 저장소여도 된다» 는 선언이다. 모양: 이름 **둘**, 둘 다 이 op 의 입력, 앞의 것은
            //   `mut`, 둘은 다르다. 선언이 **참인지**(몸이 읽기를 다 마친 뒤에만 쓰는지)는 이 판이 재지 않는다 —
            //   지은 이의 의무다(정본 §8.12(7)). 재는 일은 B2.
            proven_size_t n = 0; proven_u8str_view_t nm2[2] = { { 0 }, { 0 } }; bool bad = false;
            for (proven_size_t j = i + 1; j < f->nkids; j++) {
                if (f->kids[j]->kind != LOW_CST_ATOM || ck_clause_word(f->kids[j]->tok.lex)) break;
                if (f->kids[j]->tok.kind != LOW_TOK_IDENT) { bad = true; break; }
                if (n < 2) nm2[n] = f->kids[j]->tok.lex;
                n++;
            }
            bool wmut = false;
            if (n == 2 && !bad) {
                low_op_header_t hh = low_op_header(f);
                for (proven_size_t q = 0; q < hh.np; q++)
                    if (proven_u8str_view_eq(hh.p[q].name, nm2[0])) { wmut = hh.p[q].is_mut; break; }
            }
            if (bad || n != 2 || !ck_is_param_name(f, nm2[0]) || !ck_is_param_name(f, nm2[1]) ||
                proven_u8str_view_eq(nm2[0], nm2[1]) || !wmut)
                emit(out, "E-INPLACE-FORM",
                     "an `inplace` clause names exactly TWO inputs of this op: first the `mut` input it writes, then "
                     "the input it reads — `inplace out a .` says «`out` and `a` may be the same storage». One pair "
                     "per clause; the two must differ (RFC-0116 D2 · §8.12(7))", f->line);
        } else if (veq(w, "access") && i + 1 < f->nkids && f->kids[i + 1]->kind == LOW_CST_ATOM) {
            if (!ck_is_param(f, f->kids[i + 1]->tok.lex))
                emit(out, "E-CONTRACT-UNDEF",
                     "the `access` clause names something that is not a parameter of this op", f->line);
            if (i + 2 < f->nkids && f->kids[i + 2]->kind == LOW_CST_ATOM &&
                !ck_clause_word(f->kids[i + 2]->tok.lex) &&
                !ck_access_mode(f->kids[i + 2]->tok.lex))
                emit(out, "E-CONTRACT-MODE",
                     "unknown access mode (sequential/random/streaming/tiled/read_mostly/"
                     "write_only/shared_read)", f->line);
            // ★★ 지금 강제할 수 있는 것은 **지금 강제한다.**
            if (i + 2 < f->nkids && f->kids[i + 2]->kind == LOW_CST_ATOM) {
                proven_u8str_view_t nm = f->kids[i + 1]->tok.lex, md = f->kids[i + 2]->tok.lex;
                const low_cst_t *bd = (f->kids[f->nkids - 1]->kind == LOW_CST_BLOCK)
                                        ? f->kids[f->nkids - 1] : NULL;
                if (bd && veq(md, "shared_read") && ck_writes_place(bd, nm))
                    emit(out, "E-ACCESS-MODE",
                         "`access <p> shared_read` says this op only READS that place — but it "
                         "WRITES it. shared_read is what lets several tasks hold it at once "
                         "(LowentDRF.v: no write ⇒ no race); a write here breaks exactly that",
                         f->line);
                if (bd && veq(md, "write_only") && ck_reads_name(bd, nm))
                    emit(out, "E-ACCESS-MODE",
                         "`access <p> write_only` says this op never READS that place — but it "
                         "does. A write_only place may be uninitialised: reading it is reading "
                         "garbage, and the declaration is what told the caller it was safe",
                         f->line);
                // 나머지 모드(sequential/random/streaming/tiled/read_mostly)는 **스케줄 힌트**다.
                if (!veq(md, "shared_read") && !veq(md, "write_only"))
                    warn(out, "W-NOT-YET",
                         "this `access` mode is a kernel SCHEDULING hint and is not implemented yet "
                         "(kernel ops are post-MVP) — it constrains nothing today. "
                         "(`shared_read` and `write_only` ARE enforced: they are read/write "
                         "discipline, not scheduling)", f->line);
            }
        } else if (veq(w, "parallel") && i + 1 < f->nkids && f->kids[i + 1]->kind == LOW_CST_ATOM) {
            if (!ck_is_param(f, f->kids[i + 1]->tok.lex))
                emit(out, "E-CONTRACT-UNDEF",
                     "the `parallel` clause names something that is not a parameter of this op", f->line);

        }
    }
}

// ── ★ RFC-0009 DET-1 — `parallel <s> split .` 의 **Bernstein 조건을 검사한다** ────
//
// LowentPar.v 가 증명한 것: 태스크들이 서로의 읽기/쓰기 집합을 건드리지 않으면
// **병렬 결과는 순차와 비트 동일하다**(det1_par_eq_seq, Qed). 그리고 겹치면 순서가 결과를
// 바꾼다(overlap_is_nondeterministic, Qed). 그러니 컴파일러가 할 일은 하나다 —
// **그 조건이 성립하는지 검사하는 것.**
//
// 여기서 검사하는 형태(보수적·구문적):
//     while lt <i> (len <s>) . do  … end        ← s 는 parallel 절이 지목한 슬라이스
//   반복 i 는:
//     · `set (index s i) …` 로 **자기 원소만** 쓴다        (쓰기-쓰기 분리)
//     · `index s <X>` 를 읽되 X 는 **i 뿐**이다             (읽기-쓰기 분리)
//     · 반복 밖에 사는 지역에 **쓰지 않는다**               (루프 운반 의존 없음)
//   하나라도 어기면 거부한다. 증명 못 하면 거부한다 — 보수적이 안전한 쪽이다.
//
// ★ DET-1 덕분에 **순차 실행이 곧 올바른 구현**이다(병렬 ≡ 순차). 그래서 지금은 검사만
//   넣고 실행은 나중에 붙여도 **의미가 바뀌지 않는다.** 그것이 증명이 사 준 자유다.

static bool ck_atom_is(const low_cst_t *n, const char *s2) {
    return n && n->kind == LOW_CST_ATOM && veq(n->tok.lex, s2);
}
// 이 노드(하위 전체)에서 `index <s> <X>` 읽기를 찾아, X 가 루프변수가 아니면 참.
static bool ck_bad_read(const low_cst_t *nd, proven_u8str_view_t s2, proven_u8str_view_t iv) {
    if (!nd) return false;
    for (proven_size_t j = 0; j + 2 < nd->nkids; j++) {
        if (!ck_atom_is(nd->kids[j], "index")) continue;
        if (nd->kids[j + 1]->kind != LOW_CST_ATOM ||
            !proven_u8str_view_eq(nd->kids[j + 1]->tok.lex, s2)) continue;
        const low_cst_t *ix = nd->kids[j + 2];
        if (ix->kind != LOW_CST_ATOM || !proven_u8str_view_eq(ix->tok.lex, iv)) return true;
    }
    for (proven_size_t j = 0; j < nd->nkids; j++)
        if (ck_bad_read(nd->kids[j], s2, iv)) return true;
    return false;
}
// ★★★ 조건을 **모양과 무관하게** 읽는다.
//
//   전엔 `while lt <i> (len <s>)` 를 **평평한 kids 로만** 알아봤다. 그런데 arity 단일화가
//   나무를 세우자 그것이 `while (lt <i> (len <s>))` 가 되었고 — **판별기가 못 알아보고
//   분석 자체를 포기했다**(W-NOT-YET). 즉 **거짓말하는 `parallel` 을 정적으로 못 잡게 됐다.**
//   (다행히 런타임 분할 대조가 여전히 잡았다 — 이중 검사가 살렸다.)
//
//   ★ 이것이 "구조를 이해하지 않고 **모양을 외운**" 코드의 값이다: 모양이 바뀌면 **조용히
//     죽는다.** 오류도 안 낸다. 그래서 arity 단일화가 필요한 것이고, 그 첫 증거가 이것이다.
static bool ck_while_lt_iv(const low_cst_t *st, proven_u8str_view_t *iv) {
    if (st->nkids >= 3 && ck_atom_is(st->kids[1], "lt") && st->kids[2]->kind == LOW_CST_ATOM) {
        *iv = st->kids[2]->tok.lex; return true;                       // 평평
    }
    const low_cst_t *g = st->nkids >= 2 ? st->kids[1] : NULL;          // 나무
    if (g && g->kind == LOW_CST_GROUP && g->nkids == 1 && g->kids[0]->kind == LOW_CST_FORM) {
        const low_cst_t *c = g->kids[0];
        if (c->nkids >= 3 && ck_atom_is(c->kids[0], "lt") && c->kids[1]->kind == LOW_CST_ATOM) {
            *iv = c->kids[1]->tok.lex; return true;
        }
    }
    return false;
}

// 루프 본문의 문장들을 본다.
typedef struct { proven_u8str_view_t acc, op; } ck_red_t;
static bool ck_is_reduction(const ck_red_t *r, proven_size_t n, proven_u8str_view_t nm,
                            proven_u8str_view_t *op) {
    for (proven_size_t i = 0; i < n; i++)
        if (proven_u8str_view_eq(r[i].acc, nm)) { *op = r[i].op; return true; }
    return false;
}
// ★★★★ **한계를 없앤다** (RFC-0077 P1-5 감사, 2026-08-14 소유자 결정: *"없앨 수 있으면 없앤다"*).
//   전엔 루프 **앞에 선언된 지역**을 `outer[32]` 에 모아 왔다. 33 번째부터는 조용히 안 모였고,
//   그러면 그 이름에 대한 **루프 반송 의존을 못 본다** — 쪼개면 안 되는 루프가 쪼개진다.
//   *덜 검사하는* 정도가 아니라 **틀린 병렬화**로 이어지는 자리였다.
//   ⇒ 모으지 않는다: 필요할 때 **그 자리 앞을 훑는다**(상한이 사라진다). 몸이 작아 값이 싸다.
static bool ck_declared_before(const low_cst_t *encl, proven_size_t upto, proven_u8str_view_t nm) {
    if (!encl) return false;
    if (upto > encl->nkids) upto = encl->nkids;
    for (proven_size_t i = 0; i < upto; i++) {
        const low_cst_t *st = encl->kids[i];
        if (st->kind != LOW_CST_FORM || st->nkids < 2 || st->kids[0]->kind != LOW_CST_ATOM) continue;
        low_kw_t kw = st->kids[0]->tok.kw;
        if ((kw == LOW_KW_VAR || kw == LOW_KW_LET) && st->kids[1]->kind == LOW_CST_ATOM &&
            proven_u8str_view_eq(st->kids[1]->tok.lex, nm)) return true;
    }
    return false;
}
static void ck_par_body(low_check_result_t *out, const low_cst_t *blk,
                        proven_u8str_view_t s2, proven_u8str_view_t iv,
                        const low_cst_t *encl, proven_size_t upto,
                        const ck_red_t *reds, proven_size_t nreds) {
    if (!blk) return;
    for (proven_size_t i = 0; i < blk->nkids; i++) {
        const low_cst_t *f = blk->kids[i];
        if (f->kind != LOW_CST_FORM || f->nkids == 0 || f->kids[0]->kind != LOW_CST_ATOM) continue;
        low_kw_t kw = f->kids[0]->tok.kw;
        if (kw == LOW_KW_SET && f->nkids >= 3 && f->kids[1]->kind == LOW_CST_GROUP) {
            // `set (index s X) v .` — X 는 루프변수여야 하고, s 는 지목된 슬라이스여야 한다
            const low_cst_t *g = f->kids[1];
            if (g->nkids == 1 && g->kids[0]->kind == LOW_CST_FORM && g->kids[0]->nkids == 3 &&
                ck_atom_is(g->kids[0]->kids[0], "index")) {
                const low_cst_t *sn = g->kids[0]->kids[1], *ix = g->kids[0]->kids[2];
                bool same_s = sn->kind == LOW_CST_ATOM && proven_u8str_view_eq(sn->tok.lex, s2);
                bool same_i = ix->kind == LOW_CST_ATOM && proven_u8str_view_eq(ix->tok.lex, iv);
                if (!same_s || !same_i)
                    emit(out, "E-PAR-WRITE",
                         "a splittable loop may only write its OWN element `index <s> <i>` — "
                         "this write can collide with another iteration (Bernstein: wr ∩ wr = ∅)",
                         f->line);
            }
        } else if (kw == LOW_KW_SET && f->nkids >= 2 && f->kids[1]->kind == LOW_CST_ATOM) {
            // 반복 밖에 사는 지역에 쓰는가 → 루프 운반 의존
            proven_u8str_view_t nm = f->kids[1]->tok.lex;
            proven_u8str_view_t rop;
            if (ck_is_reduction(reds, nreds, nm, &rop)) continue;   // ★ 선언된 리덕션은 허용
            if (!proven_u8str_view_eq(nm, iv) && ck_declared_before(encl, upto, nm))
                emit(out, "E-PAR-CARRY",
                     "a splittable loop may not write a local that lives across iterations "
                     "(a loop-carried dependence) — declare it: `reduce <acc> <op> .`",
                     f->line);
        }
        // 어느 문장에서든, 지목된 슬라이스를 **자기 인덱스가 아닌 곳**에서 읽으면 안 된다
        if (ck_bad_read(f, s2, iv))
            emit(out, "E-PAR-READ",
                 "a splittable loop may only read its OWN element of the split slice — reading "
                 "another index creates a cross-iteration dependence (Bernstein: rd ∩ wr = ∅)",
                 f->line);
        // 중첩 블록도 훑는다
        for (proven_size_t j = 0; j < f->nkids; j++)
            if (f->kids[j]->kind == LOW_CST_BLOCK)
                ck_par_body(out, f->kids[j], s2, iv, encl, upto, reds, nreds);
    }
}
// ═══════════════════════════════════════════════════════════════════════════
// ★★★ **trait 의 구조적 충족을 실제로 검사한다** (RFC-0026 §9 의 W-NOT-YET 를 갚는다)
//
//   지금까지 `satisfies` 는 **이름만** 확인했다. 도구가 스스로 그렇게 말했다:
//     *"the trait is declared and the name resolves, but STRUCTURAL satisfaction … is not
//       checked yet."* — **정직했지만, 자백은 해결이 아니다.**
//
//   ★ 그리고 이것은 **제네릭(RFC-0021)과 동적 디스패치의 선행 조건**이다:
//     트레이트가 **계약을 나르지 못하면** 경계(bound)도 vtable 도 **아무것도 보장하지 못한다.**
//
//   문법 (새 낱말 0 — 있는 것만 쓴다):
//
//       trait shape
//         area   input s self . output u64 . effects none .
//         scaled input s self . input k u8 . output u64 . effects none .
//       end
//
//       struct rect
//         satisfies shape .            ← **타입**이 충족한다(op 이 아니라). RFC-0062 와 맞물린다
//         w u8 .
//         h u8 .
//       end
//
//       fn rect.area   input s rect . output u64 . effects none . do … end
//       fn rect.scaled input s rect . input k u8 . output u64 . effects none . do … end
//
//   `self` 는 **그 타입 자신**으로 읽힌다. 검사하는 것:
//     · 요구된 op 이 `<Type>.<name>` 으로 **있는가**        → E-TRAIT-MISSING
//     · 파라미터 수와 **타입 낱말**이 같은가 · 출력이 같은가 → E-TRAIT-SIG
//     · 구현의 효과가 트레이트가 선언한 효과의 **부분집합**인가 → E-TRAIT-EFFECT
//       (구현이 트레이트보다 **더 많은 일**을 하면 호출자의 추론이 무너진다.)
// ★★★ 2026-09-15 — **요구 수와 절 수에 한도가 없다.** 전엔 요구 16 개·서명마다 뒤따르는 절 form 8 개를 배열에 담았고,
//   넘치면 **소리 없이** 앞 요구에 붙이거나 버렸다: 요구가 17 개인 trait, 입력이 8 개를 넘는 서명은 올바른 구현에도
//   `E-TRAIT-SIG`(파라미터 수가 다르다)를 받았다(실측). 이제 절은 trait 블록의 자리를 **가리키기만** 하고, 요구는 하나씩 푼다.
typedef struct {
    proven_u8str_view_t name;
    const low_cst_t    *head;                 // 요구의 첫 form (이름 + 첫 절)
    const low_cst_t *const *clauses;          // 뒤따르는 절 form 들 — trait 블록 kids 의 한 조각
    proven_size_t       nclauses;
} ck_treq_t;

static bool ck_atom(const low_cst_t *n) { return n && n->kind == LOW_CST_ATOM; }


// ★ 본문 어딘가에 어휘 region 블록이 있나 — 그것이 곧 할당 권한이다.
// ★ 종류로 가른 영역 블록 — `want_heap` 이면 `heap` 블록, 아니면 그 밖의 일곱 종류(고정 창).
static bool ck_has_region_kind(const low_cst_t *nd, bool want_heap) {
    if (!nd || nd->kind == LOW_CST_ATOM) return false;
    if (nd->kind == LOW_CST_FORM && nd->nkids >= 3 && ck_atom(nd->kids[0]) &&
        veq(nd->kids[0]->tok.lex, "region")) {
        const low_cst_t *k = nd->kids[2];
        if (k->kind == LOW_CST_FORM && k->nkids && ck_atom(k->kids[0])) k = k->kids[0];
        if (ck_atom(k) && veq(k->tok.lex, "heap") == want_heap) return true;
    }
    for (proven_size_t i = 0; i < nd->nkids; i++)
        if (ck_has_region_kind(nd->kids[i], want_heap)) return true;
    return false;
}

// 절 낱말로 시작하는 form 인가 (op 헤더와 **같은 규칙**으로 이어 붙인다)
static bool ck_form_is_clause(const low_cst_t *f) {
    return f && f->kind == LOW_CST_FORM && f->nkids && f->kids[0]->kind == LOW_CST_ATOM &&
           low_is_clause_word(f->kids[0]->tok.lex);
}

// 이 요구/구현에서 낱말 `w` 로 시작하는 절의 **뒤 낱말들**을 모은다.
static proven_size_t ck_clause_words(const low_cst_t *head, const low_cst_t *const *cl,
                                     proven_size_t ncl, const char *w,
                                     proven_u8str_view_t *out, proven_size_t max) {
    proven_size_t n = 0;
    for (proven_size_t i = 1; i + 0 < head->nkids && n < max; i++)
        if (ck_atom(head->kids[i]) && veq(head->kids[i]->tok.lex, w))
            for (proven_size_t j = i + 1; j < head->nkids && n < max; j++) {
                if (!ck_atom(head->kids[j]) || low_is_clause_word(head->kids[j]->tok.lex)) break;
                out[n++] = head->kids[j]->tok.lex;
            }
    for (proven_size_t c = 0; c < ncl && n < max; c++) {
        const low_cst_t *f = cl[c];
        if (!f->nkids || !ck_atom(f->kids[0]) || !veq(f->kids[0]->tok.lex, w)) continue;
        for (proven_size_t j = 1; j < f->nkids && n < max; j++) {
            if (!ck_atom(f->kids[j])) break;
            out[n++] = f->kids[j]->tok.lex;
        }
    }
    return n;
}

// ★★★ **trait 요구의 파라미터를 *구현과 같은 규칙으로* 센다** (2026-07-19, lib/alloc.low 이 찾았다).
//
//   전에는 낱말을 평평하게 뽑아 **짝수로 잘랐다**: `input <이름> <타입>` 이 낱말 **둘**이라고
//   가정한 것이다. 그래서 타입이 한 낱말보다 길면 — `input backing mut slice u8 . .` —
//   낱말 넷이 **파라미터 둘**로 세어졌다:
//       세어진 것:  (backing:mut) (slice:u8)      실제:  backing : mut slice u8
//   ⇒ 멀쩡한 구현이 E-TRAIT-SIG 로 **거짓 거절**되고, 반대로 어긋난 타입이 우연히 줄을
//     맞추면 **거짓 통과**한다. 계약을 지키라는 검사가 계약을 **틀리게 읽고 있었다.**
//
//   ★ 그리고 주석이 *"타입만 뽑는다"* 고 **단언하고 있었다** — 검사되지 않은 단언이다(§0).
//     trait 파라미터에 여러 낱말짜리 타입을 쓴 픽스처가 저장소에 하나도 없어서 여태 안 보였다.
//     (`vm_alloc.low` 의 trait 은 전부 `u64`·`self`·`option u64` — 출력만 두 낱말이었고,
//      출력 비교는 낱말 **목록**을 통째로 대조해서 멀쩡했다. **파라미터 쪽만 썩어 있었다.**)
//
//   ⇒ 이제 `low_op_header` 와 **같은 규칙**으로 판다: 이름을 떼고(`comptime` 은 앞뒤 모두),
//     `mut`/`owned`/`unsafe_ptr` 를 벗긴 **알맹이 낱말**이 그 파라미터의 타입이다.
//   ★★ 그리고 **알맹이 낱말 하나만 대조하면 부족하다**: `slice u8` 과 `slice u64` 는 알맹이가
//     둘 다 `slice` 라 그냥 통과했다 — 원소 타입이 달라도. 진단은 *"정확히 일치"* 를 요구한다고
//     **말하고 있었으므로**, 알맹이부터 절 끝까지의 낱말을 **전부** 대조한다.
//     ☞ 아직 안 보는 것(정직히): `mut`/`owned` 같은 **접근 한정자**. 그것을 계약에 넣을지는
//       별개 질문이고(구현이 trait 보다 덜 빌려주는 것은 안전할 수 있다), 여기서 답하지 않는다.
#define CK_TP_MAXW 6
typedef struct { proven_u8str_view_t w[CK_TP_MAXW]; proven_size_t nw; } ck_tparam_t;
static proven_size_t ck_trait_params(const low_cst_t *head, const low_cst_t *const *cl,
                                     proven_size_t ncl,
                                     ck_tparam_t *out, proven_size_t max) {
    proven_size_t n = 0;
    // 한 `input` 절(kids[s..e))에서 알맹이부터 끝까지의 타입 낱말들을 뽑는다.
    #define CK_TP_ONE(F, S, E)                                                              \
        do {                                                                                \
            proven_size_t j_ = (S);                                                         \
            while (j_ < (E) && ck_atom((F)->kids[j_]) && veq((F)->kids[j_]->tok.lex, "comptime")) j_++; \
            if (j_ >= (E) || !ck_atom((F)->kids[j_])) break;                                 \
            j_++;                       /* 이름 */                                          \
            while (j_ < (E) && ck_atom((F)->kids[j_]) && veq((F)->kids[j_]->tok.lex, "comptime")) j_++; \
            while (j_ < (E) && ck_atom((F)->kids[j_]) &&                                     \
                   (veq((F)->kids[j_]->tok.lex, "mut") || veq((F)->kids[j_]->tok.lex, "owned") || \
                    veq((F)->kids[j_]->tok.lex, "unsafe_ptr"))) j_++;                        \
            if (j_ >= (E) || !ck_atom((F)->kids[j_]) || n >= max) break;                     \
            out[n].nw = 0;                                                                   \
            for (; j_ < (E) && ck_atom((F)->kids[j_]) && out[n].nw < CK_TP_MAXW; j_++)        \
                out[n].w[out[n].nw++] = (F)->kids[j_]->tok.lex;                              \
            n++;                                                                             \
        } while (0)

    for (proven_size_t i = 1; i < head->nkids && n < max; i++) {
        if (!ck_atom(head->kids[i]) || !veq(head->kids[i]->tok.lex, "input")) continue;
        proven_size_t e = i + 1;
        while (e < head->nkids && ck_atom(head->kids[e]) &&
               !low_is_clause_word(head->kids[e]->tok.lex)) e++;
        CK_TP_ONE(head, i + 1, e);
        i = e - 1;
    }
    for (proven_size_t c = 0; c < ncl && n < max; c++) {
        const low_cst_t *f = cl[c];
        if (!f->nkids || !ck_atom(f->kids[0]) || !veq(f->kids[0]->tok.lex, "input")) continue;
        proven_size_t e = 1;
        while (e < f->nkids && ck_atom(f->kids[e])) e++;
        CK_TP_ONE(f, 1, e);
    }
    #undef CK_TP_ONE
    return n;
}

// ★ 이 이름이 **선언된 타입**인가 (struct · enum · type). `Type.op` 의 앞마디가 그것이어야 한다.
static bool ck_is_type_name(const low_parse_result_t *pr, proven_u8str_view_t name) {
    for (proven_size_t i = 0; i < pr->nforms; i++) {
        const low_cst_t *f = pr->forms[i]; ck_cur_form = f;
        if (f->kind != LOW_CST_FORM || f->nkids < 2 || f->kids[0]->kind != LOW_CST_ATOM) continue;
        low_kw_t kw = f->kids[0]->tok.kw;
        if (kw != LOW_KW_STRUCT && kw != LOW_KW_ENUM && kw != LOW_KW_TYPE && kw != LOW_KW_NEWTYPE && kw != LOW_KW_ACTOR) continue;
        if (f->kids[1]->kind == LOW_CST_ATOM && proven_u8str_view_eq(f->kids[1]->tok.lex, name))
            return true;
    }
    return false;
}
// ★ 이름이 **집합(struct/enum)** 인가 — 스칼라가 아니다. map/filter/store 원소 검사에 쓴다.
static bool ck_is_aggregate(const low_parse_result_t *pr, proven_u8str_view_t name) {
    for (proven_size_t i = 0; i < pr->nforms; i++) {
        const low_cst_t *f = pr->forms[i]; ck_cur_form = f;
        if (f->kind != LOW_CST_FORM || f->nkids < 2 || f->kids[0]->kind != LOW_CST_ATOM) continue;
        low_kw_t kw = f->kids[0]->tok.kw;
        if ((kw == LOW_KW_STRUCT || kw == LOW_KW_ENUM) &&
            f->kids[1]->kind == LOW_CST_ATOM && proven_u8str_view_eq(f->kids[1]->tok.lex, name))
            return true;
    }
    return false;
}

// ★★★ trait 충족 검사 본체.
static void ck_trait_satisfy(low_check_result_t *out, const low_parse_result_t *pr) {
    for (proven_size_t si = 0; si < pr->nforms; si++) {
        const low_cst_t *sf = pr->forms[si];
        if (sf->kind != LOW_CST_FORM || sf->nkids < 2 || !ck_atom(sf->kids[0])) continue;
        // ★★★ **액터도 타입이다** — 그리고 `satisfies` 를 적을 수 있다.
        //   그런데 검사기가 **`struct` 만** 봤다 ⇒ **액터의 주장은 아무도 확인하지 않았다.**
        //   *"없는 op 을 충족한다"* 고 적어도 **조용히 통과**했다 — 정확히 이 언어가
        //   없애려는 그 결함(검사되지 않는 주장)이다.
        //   ★ 액터의 op 은 **핸들러**다(수신자는 암묵적 — 슬롯 0 이 인스턴스다).
        bool is_actor = (sf->kids[0]->tok.kw == LOW_KW_ACTOR);
        if ((sf->kids[0]->tok.kw != LOW_KW_STRUCT && !is_actor) || !ck_atom(sf->kids[1])) continue;
        proven_u8str_view_t tyname = sf->kids[1]->tok.lex;
        const low_cst_t *sblk = sf->kids[sf->nkids - 1];
        if (sblk->kind != LOW_CST_BLOCK) continue;

        for (proven_size_t q = 0; q < sblk->nkids; q++) {           // `satisfies T .` 절을 찾는다
            const low_cst_t *cf = sblk->kids[q];
            if (cf->kind != LOW_CST_FORM || cf->nkids < 2 || !ck_atom(cf->kids[0])) continue;
            if (cf->kids[0]->tok.kw != LOW_KW_SATISFIES || !ck_atom(cf->kids[1])) continue;
            proven_u8str_view_t tn = cf->kids[1]->tok.lex;
            proven_u32 line = cf->kids[1]->tok.line;

            // 그 trait 을 찾는다
            const low_cst_t *tblk = NULL;
            for (proven_size_t ti = 0; ti < pr->nforms; ti++) {
                const low_cst_t *tf = pr->forms[ti];
                if (tf->kind != LOW_CST_FORM || tf->nkids < 2 || !ck_atom(tf->kids[0])) continue;
                if (tf->kids[0]->tok.kw != LOW_KW_TRAIT || !ck_atom(tf->kids[1])) continue;
                if (!proven_u8str_view_eq(tf->kids[1]->tok.lex, tn)) continue;
                if (tf->kids[tf->nkids - 1]->kind == LOW_CST_BLOCK) tblk = tf->kids[tf->nkids - 1];
            }
            if (!tblk) {
                emit(out, "E-TRAIT-UNDEF",
                     "`satisfies` names a trait that is not declared — a claim that points at "
                     "nothing checks nothing", line);
                continue;
            }

            // trait 의 요구를 하나씩 푼다 (절 낱말 form 은 앞의 요구에 붙는다 — op 헤더와 같은 규칙).
            //   각 요구에 대해 `<Type>.<name>` op 을 찾아 **시그니처를 대조**한다.
            for (proven_size_t k0 = 0; k0 < tblk->nkids; k0++) {
                const low_cst_t *rf = tblk->kids[k0];
                if (rf->kind != LOW_CST_FORM || !rf->nkids || !ck_atom(rf->kids[0]) || ck_form_is_clause(rf)) continue;
                proven_size_t k1 = k0 + 1;
                while (k1 < tblk->nkids && tblk->kids[k1]->kind == LOW_CST_FORM && tblk->kids[k1]->nkids &&
                       ck_atom(tblk->kids[k1]->kids[0]) && ck_form_is_clause(tblk->kids[k1])) k1++;
                ck_treq_t req[1] = { { .name = rf->kids[0]->tok.lex, .head = rf,
                                       .clauses = (const low_cst_t *const *)&tblk->kids[k0 + 1], .nclauses = k1 - (k0 + 1) } };
                const proven_size_t r = 0;
                proven_u8 qbuf[128];
                if (tyname.size + 1 + req[r].name.size > sizeof qbuf) continue;
                memcpy(qbuf, tyname.ptr, tyname.size); qbuf[tyname.size] = '.';
                memcpy(qbuf + tyname.size + 1, req[r].name.ptr, req[r].name.size);
                proven_u8str_view_t qn = { .ptr = qbuf, .size = tyname.size + 1 + req[r].name.size };

                const low_cst_t *impl = NULL;
                bool recv_implicit = false;
                if (is_actor) {
                    // ★ 액터의 op = **블록 안의 핸들러**. 이름은 자격 없이 맨 이름이고,
                    //   수신자(인스턴스)는 **암묵적**이다 ⇒ trait 의 `self` 파라미터는 건너뛴다.
                    for (proven_size_t q2 = 0; q2 < sblk->nkids && !impl; q2++) {
                        const low_cst_t *hf = sblk->kids[q2];
                        if (hf->kind != LOW_CST_FORM || hf->nkids < 2 || !ck_atom(hf->kids[0])) continue;
                        low_kw_t hk = hf->kids[0]->tok.kw;
                        if ((hk != LOW_KW_FN && hk != LOW_KW_PROC) || !ck_atom(hf->kids[1])) continue;
                        if (proven_u8str_view_eq(hf->kids[1]->tok.lex, req[r].name)) impl = hf;
                    }
                    recv_implicit = true;
                } else {
                    for (proven_size_t oi = 0; oi < pr->nforms; oi++) {
                        const low_cst_t *of = pr->forms[oi];
                        if (of->kind != LOW_CST_FORM || of->nkids < 2 || !ck_atom(of->kids[0])) continue;
                        low_kw_t k = of->kids[0]->tok.kw;
                        if ((k != LOW_KW_FN && k != LOW_KW_PROC) || !ck_atom(of->kids[1])) continue;
                        if (proven_u8str_view_eq(of->kids[1]->tok.lex, qn)) impl = of;
                    }
                }
                if (!impl) {
                    emit(out, "E-TRAIT-MISSING",
                         "this type claims to satisfy a trait, but the trait REQUIRES an op that the "
                         "type does not have. Declare it as `fn <Type>.<name> …` (RFC-0062). "
                         "A claim that is not checked is the defect this language exists to remove",
                         line);
                    continue;
                }

                // ── 파라미터: 개수와 **타입 낱말** (`self` = 이 타입)
                ck_tparam_t rp[LOW_HDR_MAXP]; proven_size_t nrp = 0;
                // ★ 구현과 **같은 규칙**으로 판다(ck_trait_params) — 짝수 자르기는 타입이
                //   한 낱말일 때만 맞았고, `mut slice u8` 에서 소리 없이 틀렸다.
                nrp = ck_trait_params(req[r].head, req[r].clauses, req[r].nclauses,
                                      rp, LOW_HDR_MAXP);
                // ★ 액터의 핸들러는 **수신자를 안 적는다**(슬롯 0 이 인스턴스다) ⇒ `self` 를 뺀다.
                if (recv_implicit && nrp > 0 && rp[0].nw == 1 && veq(rp[0].w[0], "self")) {
                    for (proven_size_t z = 1; z < nrp; z++) rp[z-1] = rp[z];
                    nrp--;
                }
                low_op_header_t ih = low_op_header(impl);
                if (ih.np != nrp) {
                    emit(out, "E-TRAIT-SIG",
                         "the op exists but its PARAMETER COUNT does not match what the trait "
                         "requires — a trait that is not matched exactly cannot carry a contract, "
                         "and a bound that carries no contract guarantees nothing", line);
                    continue;
                }
                bool sigbad = false;
                for (proven_size_t i = 0; i < nrp; i++) {
                    // ★ 알맹이부터 절 끝까지의 낱말을 **전부** 대조한다 — 알맹이 하나만 보면
                    //   `slice u8` 과 `slice u64` 가 둘 다 `slice` 라 그냥 통과했다.
                    proven_size_t got = 0;
                    for (proven_size_t z = ih.p[i].core; z < ih.p[i].te && z < impl->nkids; z++) {
                        if (!ck_atom(impl->kids[z])) break;
                        proven_u8str_view_t want = (got < rp[i].nw) ? rp[i].w[got]
                                                                    : proven_u8str_view_from_cstr("");
                        if (veq(want, "self")) want = tyname;           // ★ `self` = 이 타입
                        if (got >= rp[i].nw ||
                            !proven_u8str_view_eq(impl->kids[z]->tok.lex, want)) { sigbad = true; break; }
                        got++;
                    }
                    if (sigbad || got != rp[i].nw) { sigbad = true; break; }
                }
                if (sigbad) {
                    emit(out, "E-TRAIT-SIG",
                         "the op exists but a PARAMETER TYPE does not match what the trait requires "
                         "(`self` means THIS type). The bound is only worth what it checks", line);
                    continue;
                }

                // ── 출력
                proven_u8str_view_t ro[8], io_[8];
                proven_size_t nro = ck_clause_words(req[r].head, req[r].clauses, req[r].nclauses,
                                                    "output", ro, 8);
                proven_size_t nio = 0;
                for (proven_size_t i = ih.out_s; i < ih.out_e && nio < 8; i++)
                    if (ck_atom(impl->kids[i])) io_[nio++] = impl->kids[i]->tok.lex;
                bool outbad = (nro != nio);
                for (proven_size_t i = 0; !outbad && i < nro; i++) {
                    proven_u8str_view_t want = ro[i];
                    if (veq(want, "self")) want = tyname;
                    if (!proven_u8str_view_eq(io_[i], want)) outbad = true;
                }
                if (outbad) {
                    emit(out, "E-TRAIT-SIG",
                         "the op exists but its OUTPUT does not match what the trait requires", line);
                    continue;
                }

                // ── 효과: 구현 ⊆ 트레이트. 더 많은 일을 하면 호출자의 추론이 무너진다.
                proven_u8str_view_t re[8], ie[8];
                proven_size_t nre = ck_clause_words(req[r].head, req[r].clauses, req[r].nclauses,
                                                    "effects", re, 8);
                proven_size_t nie = 0;
                for (proven_size_t i = ih.eff_s; i < ih.eff_e && nie < 8; i++)
                    if (ck_atom(impl->kids[i])) ie[nie++] = impl->kids[i]->tok.lex;
                // ★★★★ RFC-0112 D7 — 트레이트가 `via self` 를 적었으면 구현은 **할당 계열 효과**를 더 적을 수 있다.
                //   (뿌리를 딛는 얼로케이터는 `reserve` 가 `alloc`/`heap` 을 한다 — 그것을 막으면 기본 얼로케이터가
                //   사용자 얼로케이터와 같은 자리에 못 선다.) 제네릭 op 이 `via a` 로 그것을 부르는 쪽까지 나른다.
                bool via_self = false;
                for (proven_size_t j = 0; j + 1 < nre; j++)
                    if (veq(re[j], "via") && veq(re[j + 1], "self")) via_self = true;
                // ★★★ 2026-09-15 — **효과 절이 없는 `proc` 은 «무엇이든 한다» 이다**(본문 검사가 그렇게 읽는다). 그런데 이 대조는
                //   구현이 **적은 낱말**만 봐서, 절이 없으면 빈 목록이라 `effects none` 요구를 그대로 통과했다(실측: 그 구현을 부른
                //   `fn` 만 E-EFFECT-CALC 로 걸렸다). 트레이트가 무언가를 적었다면, 적지 않은 구현은 그보다 많은 일을 할 수 있다.
                if (!ih.is_calc && ih.eff_s == ih.eff_e && !recv_implicit) {
                    emit(out, "E-TRAIT-EFFECT",
                         "the implementation is a `proc` with NO `effects` clause, so it may do anything — more than the trait "
                         "declares. Write the same `effects` line the trait has (or fewer), or make it a `fn` if it is pure", line);
                    continue;
                }
                for (proven_size_t i = 0; i < nie; i++) {
                    if (veq(ie[i], "none")) continue;
                    bool found = false;
                    for (proven_size_t j = 0; j < nre; j++)
                        if (proven_u8str_view_eq(re[j], ie[i])) found = true;
                    if (via_self && (veq(ie[i], "alloc") || veq(ie[i], "heap") || veq(ie[i], "lock") || veq(ie[i], "atomic")))
                        found = true;
                    if (!found) {
                        emit(out, "E-TRAIT-EFFECT",
                             "the implementation has an EFFECT the trait does not declare. A caller "
                             "reasons against the TRAIT's contract — if the implementation does more, "
                             "that reasoning is a lie (this is exactly what makes dynamic dispatch "
                             "safe or unsafe)", line);
                        break;
                    }
                }
            }
        }
    }
}

// ★★★ **제네릭의 경계(bound)를 강제한다** — `requires <trait> <type> .` (RFC-0021 §6.3)
//
//   이것은 **런타임 술어가 아니다.** *"이 타입은 그 trait 을 충족해야 한다"* 는 **컴파일타임
//   요구**다. 그리고 단형화가 끝난 뒤에는 `<type>` 이 **구체 타입**이므로 검사할 수 있다.
//
//   ★ 오늘 아침까지 이것은 **불가능했다**: trait 충족이 검사되지 않았으니(W-NOT-YET)
//     경계도 **아무것도 보장할 수 없었다.** 그래서 그것이 이것의 **선행 조건**이었다.
static bool ck_type_satisfies(const low_parse_result_t *pr, proven_u8str_view_t ty,
                              proven_u8str_view_t tr) {
    for (proven_size_t i = 0; i < pr->nforms; i++) {
        const low_cst_t *f = pr->forms[i]; ck_cur_form = f;
        if (f->kind != LOW_CST_FORM || f->nkids < 2 || !ck_atom(f->kids[0])) continue;
        // ★★★ **액터도 타입이다** — 경계 검사도 그것을 봐야 한다.
        //   충족 검사(ck_trait_satisfy)는 오늘 고쳤는데 **경계 검사는 안 고쳤다.**
        //   **같은 규칙이 두 곳에 있고 한 곳만 옳았다** — 오늘만 세 번째다(교훈 7).
        if ((f->kids[0]->tok.kw != LOW_KW_STRUCT && f->kids[0]->tok.kw != LOW_KW_ACTOR) ||
            !ck_atom(f->kids[1])) continue;
        if (!proven_u8str_view_eq(f->kids[1]->tok.lex, ty)) continue;
        const low_cst_t *b = f->kids[f->nkids - 1];
        if (b->kind != LOW_CST_BLOCK) return false;
        for (proven_size_t q = 0; q < b->nkids; q++) {
            const low_cst_t *cf = b->kids[q];
            if (cf->kind != LOW_CST_FORM || cf->nkids < 2 || !ck_atom(cf->kids[0])) continue;
            if (cf->kids[0]->tok.kw == LOW_KW_SATISFIES && ck_atom(cf->kids[1]) &&
                proven_u8str_view_eq(cf->kids[1]->tok.lex, tr)) return true;
        }
    }
    return false;
}
static bool ck_is_trait_name(const low_parse_result_t *pr, proven_u8str_view_t nm) {
    for (proven_size_t i = 0; i < pr->nforms; i++) {
        const low_cst_t *f = pr->forms[i]; ck_cur_form = f;
        if (f->kind != LOW_CST_FORM || f->nkids < 2 || !ck_atom(f->kids[0])) continue;
        if (f->kids[0]->tok.kw == LOW_KW_TRAIT && ck_atom(f->kids[1]) &&
            proven_u8str_view_eq(f->kids[1]->tok.lex, nm)) return true;
    }
    return false;
}
static bool ck_uint_literal(const low_cst_t *a, proven_u64 *v);   // ★ 아래 정의를 앞당겨 쓴다
// ★★ **함께 참일 수 없는 전제는 모든 부름을 입구에서 막는다** (결함 노트 #47, 2026-09-16).
//   `requires le a 100 .` 과 `requires ge a 200 .` 을 나란히 적으면 어떤 인자로 불러도 진입에서
//   멈춘다 — 그런데 `--check` 는 조용했다. 일어날 수 없는 **오류 선언**은 이미 물면서
//   (`E-CONTRACT-DEAD`) 일어날 수 없는 **전제**는 안 봤다. 대개 고치다 옛 줄을 안 지운 자리다.
//   ★ 좁게 문다: 같은 이름에 대한 `le`/`lt`/`ge`/`gt`/`eq` 와 **수 리터럴**만 본다.
static void ck_requires_unsat(low_check_result_t *out, const low_parse_result_t *pr) {
    for (proven_size_t i = 0; i < pr->nforms; i++) {
        const low_cst_t *f = pr->forms[i]; ck_cur_form = f;
        if (f->kind != LOW_CST_FORM || f->nkids < 2 || !ck_atom(f->kids[0])) continue;
        low_kw_t kw = f->kids[0]->tok.kw;
        if (kw != LOW_KW_FN && kw != LOW_KW_PROC) continue;
        struct { proven_u8str_view_t nm; proven_u64 lo, hi; proven_u32 line; } iv[16];
        proven_size_t niv = 0;
        for (proven_size_t j = 2; j + 3 < f->nkids; j++) {
            if (!ck_atom(f->kids[j]) || !veq(f->kids[j]->tok.lex, "requires")) continue;
            const low_cst_t *c = f->kids[j + 1], *n = f->kids[j + 2], *v = f->kids[j + 3];
            if (!ck_atom(c) || !ck_atom(n) || n->tok.kind != LOW_TOK_IDENT) continue;
            proven_u64 k = 0;
            if (!ck_uint_literal(v, &k)) continue;
            proven_u64 lo = 0, hi = ~(proven_u64)0;
            proven_u8str_view_t w = c->tok.lex;
            if      (veq(w, "le")) hi = k;
            else if (veq(w, "lt")) { if (!k) continue; hi = k - 1; }
            else if (veq(w, "ge")) lo = k;
            else if (veq(w, "gt")) { if (k == ~(proven_u64)0) continue; lo = k + 1; }
            else if (veq(w, "eq")) { lo = k; hi = k; }
            else continue;
            proven_size_t z = niv;
            for (proven_size_t q = 0; q < niv; q++)
                if (proven_u8str_view_eq(iv[q].nm, n->tok.lex)) z = q;
            if (z == niv) {
                if (niv >= 16) continue;                      // ★ 조용히 자르지 않는다 — 더 못 보면 안 본다
                iv[niv].nm = n->tok.lex; iv[niv].lo = 0; iv[niv].hi = ~(proven_u64)0;
                iv[niv].line = f->kids[j]->tok.line; niv++;
            }
            if (lo > iv[z].lo) iv[z].lo = lo;
            if (hi < iv[z].hi) iv[z].hi = hi;
            if (iv[z].lo > iv[z].hi) {
                emit(out, "E-CONTRACT-UNSAT",
                     "two preconditions on the same input cannot both hold, so EVERY call stops at "
                     "the door and the body never runs. A contract that no argument satisfies is not "
                     "a strong contract, it is a dead op — usually one line left behind when the "
                     "other was edited. Keep the one you meant",
                     f->kids[j]->tok.line);
                break;
            }
        }
    }
}

static void ck_trait_bound(low_check_result_t *out, const low_parse_result_t *pr) {
    for (proven_size_t i = 0; i < pr->nforms; i++) {
        const low_cst_t *f = pr->forms[i]; ck_cur_form = f;
        if (f->kind != LOW_CST_FORM || f->nkids < 2 || !ck_atom(f->kids[0])) continue;
        low_kw_t kw = f->kids[0]->tok.kw;
        if (kw != LOW_KW_FN && kw != LOW_KW_PROC) continue;
        for (proven_size_t j = 2; j + 2 < f->nkids; j++) {
            if (!ck_atom(f->kids[j]) || !veq(f->kids[j]->tok.lex, "requires")) continue;
            if (!ck_atom(f->kids[j+1]) || !ck_atom(f->kids[j+2])) continue;
            proven_u8str_view_t tr = f->kids[j+1]->tok.lex, ty = f->kids[j+2]->tok.lex;
            if (!ck_is_trait_name(pr, tr)) continue;               // 경계가 아니라 술어다
            if (!ck_type_satisfies(pr, ty, tr))
                emit(out, "E-BOUND-UNSAT",
                     "this generic was instantiated with a type that does NOT satisfy the required "
                     "trait. A bound is a PROMISE the callee relies on — instantiating past it would "
                     "make the callee's contract a lie (RFC-0021 §6.3). Add `satisfies <trait> .` to "
                     "the struct, and the required ops as `fn <Type>.<name>`",
                     f->kids[j+2]->tok.line ? f->kids[j+2]->tok.line : f->line);
        }
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// ★★★ **가시성을 강제한다** — `export` 는 이제 뜻이 있다 (RFC-0011 §6.2 · RFC-0028 §5-4)
//
//   지금까지 `export`/`local` 은 **파싱만** 됐다. 도구가 스스로 그렇게 말했다:
//     *"the modifier's MEANING (visibility …) is not yet enforced."*
//   ★ 그리고 **그럴 수밖에 없었다** — 파서가 수식자 껍질을 **통째로 버려서** 소비자가
//     그것을 **볼 수 없었다.** `comptime` 과 **똑같은 병**이다:
//     **지어질 수 없게 만들어 놓고 "아직 못 한다" 고 말했다.**
//
//   규칙:
//     · 최상위 선언은 **자기 앞의 `module` 선언**에 속한다(파일 순서대로 이어진다).
//     · 다른 모듈의 이름은 **`export` 된 것만** 부를 수 있고, **`use` 로 들여와야** 한다.
//     · 그렇지 않으면 **E-VISIBILITY**.
//
//   ★ 한계(정직하게): **한정 경로가 없으므로 이름은 여전히 전역적으로 유일해야 한다**
//     (사용자 결정 — RFC-0060 §7). 가시성은 **접근**을 막지, 이름의 **재사용**을 열지 않는다.
#define CK_VIS_MAX 512
typedef struct {
    proven_u8str_view_t name, mod;
    bool exported;
} ck_vis_t;

// ★★★★★ **이 검사기는 지역 바인딩을 몰랐다** (RFC-0093 후속, 2026-08-11).
//   6 줄로 재현된다 — 남의 모듈이 `slot` 을 export 하기만 하면:
//       use m1 from "…" .          rem m1 이 slot 을 export 한다
//       fn f … do  var slot be u64 7 .  return slot .  end   → **E-VISIBILITY**
//   `use` 를 빼면 초록이다. 즉 **내 지역 변수의 합법성이 남의 모듈에 달려 있었다.**
//   SPEC-002 는 해상 순서를 **지역 → 모듈 → import** 라고 **이미 적어 뒀다**(§4단 중첩).
//   틀린 것은 명세가 아니라 이 검사기다 — 이 자리는 자기 모듈 우선까지는 배웠는데
//   (아래 ★ 주석) **지역은 아예 세지 않았다.**
//   ☞ 값이 큰 이유: 조합 게이트의 아픈 쌍 상당수가 이것이다. `hashmap` 의 `var slot`,
//     `growvec`·`strbuf` … 라이브러리가 **평범한 지역 이름을 쓸 수 없었다.**
//   ★ 근사는 **op 단위 과대근사**다: 이 op 어딘가에서 묶인 이름이면 이 op 안의 맨이름
//     사용은 지역 참조로 본다. 바인딩 **앞**에서 쓰는 경우를 놓치지만 그것은 미정의
//     이름 오류로 **다른 검사가 잡는다** — 여기서 두 번 잡을 일이 아니다.
#define CK_VIS_MAXLOC 256
typedef struct { proven_u8str_view_t n[CK_VIS_MAXLOC]; proven_size_t c; } ck_locset_t;
static ck_locset_t g_ck_loc;   // 이 파일의 다른 표들과 같은 규율(단일 스레드 검사기)
static void ck_loc_add(proven_u8str_view_t n) {
    if (!n.size || g_ck_loc.c >= CK_VIS_MAXLOC) return;
    for (proven_size_t i = 0; i < g_ck_loc.c; i++)
        if (proven_u8str_view_eq(g_ck_loc.n[i], n)) return;
    g_ck_loc.n[g_ck_loc.c++] = n;
}
static bool ck_loc_has(proven_u8str_view_t n) {
    for (proven_size_t i = 0; i < g_ck_loc.c; i++)
        if (proven_u8str_view_eq(g_ck_loc.n[i], n)) return true;
    return false;
}
// op 하나가 묶는 이름을 전부 모은다: 매개변수(`input <이름> …`) · `let`/`var` · `for <이름>`.
static void ck_loc_collect(const low_cst_t *nd) {
    if (!nd) return;
    if (nd->kind == LOW_CST_FORM && nd->nkids >= 2 && ck_atom(nd->kids[0]) && ck_atom(nd->kids[1])) {
        low_kw_t k = nd->kids[0]->tok.kw;
        if (k == LOW_KW_LET || k == LOW_KW_VAR || k == LOW_KW_FOR) ck_loc_add(nd->kids[1]->tok.lex);
    }
    for (proven_size_t i = 0; i + 1 < nd->nkids; i++)
        if (ck_atom(nd->kids[i]) && veq(nd->kids[i]->tok.lex, "input") && ck_atom(nd->kids[i + 1]))
            ck_loc_add(nd->kids[i + 1]->tok.lex);
    for (proven_size_t i = 0; i < nd->nkids; i++) ck_loc_collect(nd->kids[i]);
}
static void ck_vis_walk(low_check_result_t *out, const low_cst_t *nd, const ck_vis_t *v,
                        proven_size_t nv, proven_u8str_view_t cur) {
    if (!nd) return;
    if (nd->kind == LOW_CST_ATOM && nd->tok.kind == LOW_TOK_IDENT && nd->tok.kw == LOW_KW_NONE) {
        proven_u8str_view_t lx = nd->tok.lex;
        // ★ mono 인스턴스 이름(`op#type`)은 기계가 만든 것 — 한정 표면이 없다. 면제.
        bool inst = false;
        for (proven_size_t z = 0; z < lx.size; z++) if (lx.ptr[z] == (proven_u8)'#') { inst = true; break; }
        if (inst) goto kids;
        // ★ **지역이 먼저다** — 이 op 이 묶은 이름이면 남의 모듈을 볼 일이 없다(위 주석).
        if (ck_loc_has(lx)) goto kids;
        // 점 하나 = 한정 `M.member` 후보 — 이 검사는 narrow **전에** 돌므로 한정이 그대로 보인다.
        proven_size_t dot = lx.size, ndot = 0;
        for (proven_size_t z = 0; z < lx.size; z++)
            if (lx.ptr[z] == (proven_u8)'.') { if (dot == lx.size) dot = z; ndot++; }
        if (ndot == 1 && dot > 0 && dot + 1 < lx.size) {
            // 한정 참조: 비공개면 한정으로도 못 들어간다. (M 이 모듈이 아니면 필드 접근 — 침묵.)
            proven_u8str_view_t head = { .ptr = lx.ptr, .size = dot };
            proven_u8str_view_t tail = { .ptr = lx.ptr + dot + 1, .size = lx.size - dot - 1 };
            for (proven_size_t i = 0; i < nv; i++) {
                if (!proven_u8str_view_eq(v[i].name, tail) || !proven_u8str_view_eq(v[i].mod, head)) continue;
                if (proven_u8str_view_eq(v[i].mod, cur)) break;      // 제 모듈 — 자유롭다
                if (v[i].exported) break;                            // 내보낸 것 — 한정으로 자유롭다
                emit(out, "E-VISIBILITY",
                     "this name belongs to ANOTHER module and is not `export`ed — qualifying it does "
                     "not open the door. A module that cannot keep anything private is not a module, "
                     "it is a prefix. Mark it `export`, or stop reaching in (RFC-0011 §6.2)",
                     nd->tok.line);
                break;
            }
        } else if (ndot == 0) {
            // ★★★★★ **자기 모듈을 먼저 본다** (2026-08-07).
            //   전엔 이름이 같은 **첫** 항목을 집고, 그것이 남의 모듈이면 그 자리에서 거절했다 —
            //   **뒤에 자기 모듈의 같은 이름이 있어도 보지 않았다.**
            //   ⇒ `utf8` 과 `utf16` 처럼 **둘 다 `decode`·`next`·`count_chars` 를 export 하는**
            //     두 모듈이 한 단위에 오면, 자기 export 를 맨이름으로 부르는 자리마다
            //     E-VISIBILITY 가 났다. **순서를 뒤집으면 우는 쪽이 바뀐다**(실측) — 그것이
            //     이 결함의 서명이다: 판정이 **선언 순서**에 달려 있으면 그것은 규칙이 아니다.
            //   ☞ 규칙 자체는 옳았다(맨이름으로 남의 모듈을 넘지 못한다). **틀린 것은 탐색
            //     순서**였고, 그래서 "평평한 이름의 한계" 로 오래 기록돼 있었다.
            //     제 모듈 안에서 제 이름은 언제나 제 것이다.
            bool mine = false;
            for (proven_size_t i = 0; i < nv; i++)
                if (proven_u8str_view_eq(v[i].name, lx) &&
                    proven_u8str_view_eq(v[i].mod, cur)) { mine = true; break; }
            for (proven_size_t i = 0; !mine && i < nv; i++) {
                if (!proven_u8str_view_eq(v[i].name, lx)) continue;
                if (proven_u8str_view_eq(v[i].mod, cur)) break;      // 같은 모듈 — 자유롭다
                if (v[i].exported) {
                    // ★★★ 강제 플립(RFC-0011 §6.3): 내보냈어도 **bare 로는 못 넘는다** — 남의
                    //   모듈 이름은 **한정**으로 부른다. bare import(글롭)는 이 언어에 없다.
                    emit(out, "E-VISIBILITY",
                         "this name is another module's export, reached BARE — a cross-module name "
                         "must be QUALIFIED: write `<module>.<name>` (RFC-0011 qualified-by-default; "
                         "there is no glob import)",
                         nd->tok.line);
                    break;
                }
                emit(out, "E-VISIBILITY",
                     "this name belongs to ANOTHER module and is not `export`ed — a module that cannot "
                     "keep anything private is not a module, it is a prefix. Mark it `export`, or stop "
                     "reaching into another module's insides (RFC-0011 §6.2)",
                     nd->tok.line);
                break;
            }
        }
    }
kids:
    // ★★★ **선언되는 이름은 참조가 아니다.** `proc open …` 의 `open` 은 이 자리에서 *만들어지는*
    //   이름이지 남을 가리키는 이름이 아니다 — 그런데 액터 핸들러처럼 최상위 표에 없는 선언은
    //   남의 모듈이 우연히 같은 이름을 export 하면 **자기 선언이 침입으로 고발됐다**
    //   (io 의 핸들러 `open` 이 files 의 export `open` 때문에). 이름을 짓는 자리는 면제한다.
    {
        low_kw_t hk = (nd->kind == LOW_CST_FORM && nd->nkids >= 2 && nd->kids[0]->kind == LOW_CST_ATOM)
                        ? nd->kids[0]->tok.kw : LOW_KW_NONE;
        if (hk == LOW_KW_FN || hk == LOW_KW_PROC || hk == LOW_KW_STRUCT || hk == LOW_KW_ACTOR ||
            hk == LOW_KW_TRAIT || hk == LOW_KW_ENUM || hk == LOW_KW_TYPE || hk == LOW_KW_NEWTYPE) {
            ck_vis_walk(out, nd->kids[0], v, nv, cur);
            for (proven_size_t i = 2; i < nd->nkids; i++) {
                if (nd->kids[i]->kind == LOW_CST_ATOM && veq(nd->kids[i]->tok.lex, "tests") &&
                    i + 1 < nd->nkids) { i++; continue; }
                // ★★★ 2026-09-25 — **머리의 절 낱말도 참조가 아니다** (결함 link-clause-false-visibility).
                //   절 낱말은 키워드가 아니라 맨 IDENT 라(low_is_clause_word), `link "…"` 의 `link` 를
                //   이름 참조로 읽었다. 그래서 **부르는 쪽 모듈이 `link` 라는 op 을 가지면**, 그것을 부른 적도
                //   없는 남의 모듈 머리가 «비공개 이름에 손댄다» 로 고발됐다(certverify 의 `proc link`).
                //   최소 재현이 오래 안 만들어진 까닭이 그것이다 — 겹치는 이름이 **소비자 쪽**에 있어야 했다.
                if (nd->kids[i]->kind == LOW_CST_ATOM && low_is_clause_word(nd->kids[i]->tok.lex)) continue;
                if (nd->kids[i]->kind == LOW_CST_ATOM && nd->kids[i]->tok.kw == LOW_KW_SEND &&
                    i + 2 < nd->nkids) {
                    ck_vis_walk(out, nd->kids[i + 1], v, nv, cur);
                    i += 2;
                    continue;
                }
                ck_vis_walk(out, nd->kids[i], v, nv, cur);
            }
            return;
        }
    }
    for (proven_size_t i = 0; i < nd->nkids; i++) {
        // ★ op 선언의 `tests <이름> .` 절은 **비계 연결**이다 — 검증 op 은 어느 모듈에서든
        //   이름으로 건다(모듈 링크의 존재 이유). 다음 원자(이름)를 면제한다.
        if (nd->kids[i]->kind == LOW_CST_ATOM && veq(nd->kids[i]->tok.lex, "tests") &&
            i + 1 < nd->nkids) { i++; continue; }
        // ★★ `send <액터> <메시지> …` 의 **메시지 슬롯**도 면제한다 — 그 이름은 모듈
        //   이름공간에서 찾는 것이 아니라 **그 액터의 핸들러**에서 찾는다(수신자가 정한다).
        //   면제하지 않으면 남의 모듈이 우연히 같은 이름을 export 했다는 이유만으로
        //   내 액터에게 메시지를 못 보낸다 — 이름 지형이 남의 export 에 흔들리는 것이다.
        if (nd->kids[i]->kind == LOW_CST_ATOM && nd->kids[i]->tok.kw == LOW_KW_SEND &&
            i + 2 < nd->nkids) {
            ck_vis_walk(out, nd->kids[i + 1], v, nv, cur);   // 액터는 본다
            i += 2;                                          // 메시지 이름만 건너뛴다
            continue;
        }
        ck_vis_walk(out, nd->kids[i], v, nv, cur);
    }
}

// ★ 모듈 한정 정규화 (RFC-0011, 2026-07-25) — `M.member`(M=선언 모듈, 점 하나)를 bare `member` 로 좁힌다.
//   이래야 모든 이름-검증기(타입·트레이트·requires·qualifier·comptime)가 bare 를 보고 통과한다. IR 도 같은
//   CST 를 쓰므로 bare 로 해소(실 lib op 은 접두어라 빌트인과 안 겹침). additive: 한정 이름을 돕고 bare 는 불변.
static void ck_narrow_walk(const low_cst_t *nd, const proven_u8str_view_t *mods, proven_size_t nmods) {
    if (!nd) return;
    if (nd->kind == LOW_CST_ATOM && nd->tok.kind == LOW_TOK_IDENT && nd->tok.kw == LOW_KW_NONE) {
        proven_u8str_view_t v = nd->tok.lex;
        proven_size_t dot = v.size, ndot = 0;
        for (proven_size_t i = 0; i < v.size; i++)
            if (v.ptr[i] == (proven_u8)'.') { if (dot == v.size) dot = i; ndot++; }
        if (ndot == 1 && dot > 0 && dot + 1 < v.size) {
            proven_u8str_view_t head = { .ptr = v.ptr, .size = dot };
            for (proven_size_t m = 0; m < nmods; m++)
                if (proven_u8str_view_eq(head, mods[m])) {
                    low_cst_t *mut = (low_cst_t *)nd;
                    mut->tok.lex.ptr = v.ptr + dot + 1;
                    mut->tok.lex.size = v.size - dot - 1;
                    mut->qual_mod = head;   // ★ 가리킨 모듈을 남긴다(위 low_cst.h 의 주석)
                    break;
                }
        }
    }
    for (proven_size_t i = 0; i < nd->nkids; i++) ck_narrow_walk(nd->kids[i], mods, nmods);
}
// ★★★★ **별칭은 덧이름이 아니라 바꿔 부르기다** (RFC-0115 §8-24 · 정본 §6.10.2(4b), 2026-09-17).
//
//   `use files as f .` 를 적고 나면 그 모듈을 부를 이름은 **`f` 하나**다. 앞선 판은 `files.open` 도
//   함께 받았다(실측) — 그러면 같은 모듈을 두 이름으로 부르는 코드가 생기고, 읽는 사람이 둘이
//   같은 것인지 확인해야 한다. 별칭을 적은 까닭은 원래 이름이 불편해서인데, 그 이름이 계속 서 있으면
//   별칭이 한 일이 없다. 「한 뜻에 한 철자」.
static void ck_alias_walk(low_check_result_t *out, const low_cst_t *nd,
                          const proven_u8str_view_t *old, const proven_u8str_view_t *nw,
                          proven_size_t n) {
    if (!nd) return;
    if (nd->kind == LOW_CST_ATOM && nd->tok.kind == LOW_TOK_IDENT && nd->tok.kw == LOW_KW_NONE) {
        proven_u8str_view_t v = nd->tok.lex;
        proven_size_t dot = v.size;
        for (proven_size_t i = 0; i < v.size; i++)
            if (v.ptr[i] == (proven_u8)'.') { dot = i; break; }
        if (dot > 0 && dot + 1 < v.size) {
            proven_u8str_view_t head = { .ptr = v.ptr, .size = dot };
            for (proven_size_t m = 0; m < n; m++)
                if (proven_u8str_view_eq(head, old[m])) {
                    // ★ 진단은 문구를 **가리킨다**(복사하지 않는다). 그래서 문구에 **그 자리에서만
                    //   달라지는 것**(멤버 이름)을 넣으면, 같은 별칭의 두 번째 자리가 첫 번째의 문구를
                    //   덮는다 — 실측으로 확인했다(E-CLAUSE-ORDER 가 같은 함정을 적어 두었다).
                    //   ⇒ 별칭마다 한 칸을 두고, 문구에는 **별칭만** 넣는다.
                    static char abuf[64][288];
                    char *buf = abuf[m < 64 ? m : 63];
                    snprintf(buf, sizeof abuf[0],
                             "this unit renamed that module with `as`, so its name here is `%.*s` — "
                             "the original name no longer stands. One thing, one spelling: if both "
                             "names worked, a reader would have to check that they mean the same "
                             "module. Write the member under `%.*s.`",
                             (int)nw[m].size, (const char *)nw[m].ptr,
                             (int)nw[m].size, (const char *)nw[m].ptr);
                    emit_at(out, "E-USE-ALIASED", buf, nd);
                    break;
                }
        }
    }
    for (proven_size_t i = 0; i < nd->nkids; i++) ck_alias_walk(out, nd->kids[i], old, nw, n);
}
static void ck_use_aliased(low_check_result_t *out, const low_parse_result_t *pr) {
    proven_u8str_view_t old[64], nw[64]; proven_size_t n = 0;
    for (proven_size_t i = 0; i < pr->nforms && n < 64; i++) {
        const low_cst_t *f = pr->forms[i];
        if (f->kind != LOW_CST_FORM || f->nkids < 2 || !ck_atom(f->kids[0])) continue;
        if (f->kids[0]->tok.kw != LOW_KW_USE || !ck_atom(f->kids[1])) continue;
        for (proven_size_t j = 2; j + 1 < f->nkids; j++)
            if (ck_atom(f->kids[j]) && veq(f->kids[j]->tok.lex, "as") && ck_atom(f->kids[j + 1])) {
                old[n] = f->kids[1]->tok.lex; nw[n] = f->kids[j + 1]->tok.lex; n++; break;
            }
    }
    if (!n) return;
    for (proven_size_t i = 0; i < pr->nforms; i++) {
        const low_cst_t *f = pr->forms[i]; ck_cur_form = f;
        if (f->kind == LOW_CST_FORM && f->nkids >= 2 && ck_atom(f->kids[0]) &&
            f->kids[0]->tok.kw == LOW_KW_USE) continue;          // `use` 줄 자신은 원래 이름을 적는 자리다
        ck_alias_walk(out, f, old, nw, n);
    }
}
static void ck_narrow_qual(const low_parse_result_t *pr) {
    proven_u8str_view_t mods[256]; proven_size_t nmods = 0;
    for (proven_size_t i = 0; i < pr->nforms && nmods < 256; i++) {
        const low_cst_t *f = pr->forms[i]; ck_cur_form = f;
        if (f->kind == LOW_CST_FORM && f->nkids >= 2 && ck_atom(f->kids[0]) &&
            f->kids[0]->tok.kw == LOW_KW_MODULE && ck_atom(f->kids[1]))
            mods[nmods++] = f->kids[1]->tok.lex;
    }
    if (nmods) for (proven_size_t i = 0; i < pr->nforms; i++) ck_narrow_walk(pr->forms[i], mods, nmods);
}

// ★★★★★ **좁히기는 검사기만의 일이 아니다** (결함 노트 #77, 2026-09-16).
//   `M.member` → bare 좁히기가 `low_check` 안에서만 돌아서, `--run`·`--emit-c` 는 좁혀지지 않은
//   나무를 봤다: 하강은 `case sizes.small` 을 갈래로 못 알아보고 «이름 하나 묶기»로 읽어 그 갈래가
//   **모든 값을 잡았다**(500=`big` 이 첫 갈래를 돌아 1). 한 나무를 두 층이 다르게 읽으면 틀린다.
void low_narrow_qualified(const low_parse_result_t *pr) { ck_narrow_qual(pr); }

// ★ import 는 이름을 덮어쓰지 않는다 (RFC-0011, 2026-07-25) — 바인딩 이름(별칭 Y, 또는 모듈 X)이
//   다른 import 바인딩이나 최상위 선언과 겹치면 `E-NAME-COLLISION`. glob 이 없으므로 충돌면은 낱말 하나뿐.
static void ck_import_noshadow(low_check_result_t *out, const low_parse_result_t *pr) {
    // ★ import 스코프는 **모듈**이다 — 유닛이 아니다. 여러 파일이 한 유닛으로 이어져도
    //   `module vecs` 의 `use allocs` 와 `module vecgrow` 의 `use allocs` 는 **다른 스코프**다.
    //   유닛 전역으로 세면 두 파일이 같은 의존을 갖는 것만으로 거짓 E-NAME-COLLISION 이 났다.
    proven_u8str_view_t bind[CK_VIS_MAX]; proven_u32 bl[CK_VIS_MAX]; proven_size_t nb = 0;
    proven_u8str_view_t decl[CK_VIS_MAX]; proven_size_t ndc = 0;
    proven_u8str_view_t modname = { 0 };
    for (proven_size_t i = 0; i < pr->nforms; i++) {
        const low_cst_t *f = pr->forms[i]; ck_cur_form = f;
        if (f->kind != LOW_CST_FORM || f->nkids < 2 || !ck_atom(f->kids[0])) continue;
        low_kw_t kw = f->kids[0]->tok.kw;
        if (kw == LOW_KW_MODULE && ck_atom(f->kids[1])) {
            modname = f->kids[1]->tok.lex; nb = 0; ndc = 0;      // 새 모듈 = 새 import 스코프
            continue;
        }
        if (kw == LOW_KW_USE && ck_atom(f->kids[1])) {
            // 바인딩 = `as <alias>` 가 있으면 alias, 없으면 모듈 이름(kids[1]).
            proven_u8str_view_t nm = f->kids[1]->tok.lex; proven_u32 ln = f->kids[1]->tok.line;
            for (proven_size_t j = 2; j + 1 < f->nkids; j++)
                if (ck_atom(f->kids[j]) && veq(f->kids[j]->tok.lex, "as") && ck_atom(f->kids[j + 1])) {
                    nm = f->kids[j + 1]->tok.lex; ln = f->kids[j + 1]->tok.line; break;
                }
            for (proven_size_t k = 0; k < nb; k++)
                if (proven_u8str_view_eq(bind[k], nm))
                    emit(out, "E-NAME-COLLISION",
                         "two imports bind the SAME name — an import may never overwrite a name that is "
                         "already in scope. Give one an `as <alias>` so both are visible (RFC-0011)", ln);
            for (proven_size_t k = 0; k < ndc; k++)
                if (proven_u8str_view_eq(decl[k], nm) && !proven_u8str_view_eq(nm, modname))
                    emit(out, "E-NAME-COLLISION",
                         "an import binds a name that a top-level declaration already uses — an import may "
                         "never overwrite a name in scope. Rename one, or `use … as <alias>` (RFC-0011)", ln);
            if (nb < CK_VIS_MAX) { bind[nb] = nm; bl[nb] = ln; nb++; }
            continue;
        }
        if (kw != LOW_KW_FN && kw != LOW_KW_PROC && kw != LOW_KW_STRUCT && kw != LOW_KW_ENUM &&
            kw != LOW_KW_TYPE && kw != LOW_KW_NEWTYPE && kw != LOW_KW_ACTOR && kw != LOW_KW_TRAIT) continue;
        if (!ck_atom(f->kids[1])) continue;
        proven_u8str_view_t dn = f->kids[1]->tok.lex;
        for (proven_size_t k = 0; k < nb; k++)
            if (proven_u8str_view_eq(bind[k], dn) && !proven_u8str_view_eq(dn, modname))
                emit(out, "E-NAME-COLLISION",
                     "an import binds a name that a top-level declaration already uses — an import may "
                     "never overwrite a name in scope. Rename one, or `use … as <alias>` (RFC-0011)", bl[k]);
        if (ndc < CK_VIS_MAX) { decl[ndc++] = dn; LOW_HWM("check:vis-decls", ndc, CK_VIS_MAX); }
    }
}

static void ck_visibility(low_check_result_t *out, const low_parse_result_t *pr) {
    ck_vis_t v[CK_VIS_MAX]; proven_size_t nv = 0;
    proven_u8str_view_t cur = { 0 };
    bool multi = false;

    // ① 최상위 이름 → 소유 모듈 · export 여부
    for (proven_size_t i = 0; i < pr->nforms && nv < CK_VIS_MAX; i++) {
        const low_cst_t *f = pr->forms[i]; ck_cur_form = f;
        if (f->kind != LOW_CST_FORM || !f->nkids || !ck_atom(f->kids[0])) continue;
        low_kw_t kw = f->kids[0]->tok.kw;
        if (kw == LOW_KW_MODULE && f->nkids >= 2 && ck_atom(f->kids[1])) {
            if (cur.size) multi = true;
            cur = f->kids[1]->tok.lex;
            continue;
        }
        // ★★★ **trait · contract · actor 도 최상위 이름이다** — 그런데 이 표에 없었다.
        //   ⇒ **비공개 trait 를 남의 모듈이 그냥 썼다**(충족까지 했다). 아무도 안 물었다.
        //   *"모듈이 아무것도 감출 수 없으면 그것은 모듈이 아니라 접두사다"* — 절반만 지켰다.
        if (kw != LOW_KW_FN && kw != LOW_KW_PROC && kw != LOW_KW_STRUCT &&
            kw != LOW_KW_ENUM && kw != LOW_KW_TYPE && kw != LOW_KW_LET &&
            kw != LOW_KW_TRAIT && kw != LOW_KW_CONTRACT && kw != LOW_KW_ACTOR) continue;
        if (f->nkids < 2 || !ck_atom(f->kids[1])) continue;
        v[nv].name = f->kids[1]->tok.lex; v[nv].mod = cur;
        v[nv].exported = f->is_export;
        nv++;
    }
    if (!multi) return;      // 모듈이 하나면 가릴 것이 없다

    // ② 각 op 의 본문에서 **다른 모듈의 비공개 이름**을 쓰는가
    cur = (proven_u8str_view_t){ 0 };
    for (proven_size_t i = 0; i < pr->nforms; i++) {
        const low_cst_t *f = pr->forms[i]; ck_cur_form = f;
        if (f->kind != LOW_CST_FORM || !f->nkids || !ck_atom(f->kids[0])) continue;
        low_kw_t kw = f->kids[0]->tok.kw;
        if (kw == LOW_KW_MODULE && f->nkids >= 2 && ck_atom(f->kids[1])) { cur = f->kids[1]->tok.lex; continue; }
        // ★ op 의 **본문**만 보면 안 된다 — `satisfies T .`(struct) · `requires <trait> t .`(경계)
        //   같은 **선언 자리**에서도 남의 모듈 이름을 쓴다. **선언 전체**를 훑는다.
        if (kw != LOW_KW_FN && kw != LOW_KW_PROC && kw != LOW_KW_STRUCT &&
            kw != LOW_KW_ACTOR) continue;
        // ★★★ **단형화 인스턴스의 본문은 저자의 코드가 아니다** (2026-07-26).
        //   `a.use_t s x` 를 부르면 인스턴스 `use_t#s` 가 **틀이 있던 자리**(= 틀의 모듈)에
        //   놓이는데, 그 본문은 **호출자의 타입** `s` 를 참조한다 ⇒ "남의 모듈의 비공개 이름"
        //   으로 고발됐다. 저자가 쓴 것은 호출 한 줄이고 그것은 이미 검사됐다(제 모듈의 `s`,
        //   한정된 `a.use_t`). 기계가 만든 몸을 저자에게 청구하면 **제네릭이 모듈을 못 넘는다.**
        if (f->nkids >= 2 && ck_atom(f->kids[1])) {
            proven_u8str_view_t dn = f->kids[1]->tok.lex;
            bool inst = false;
            for (proven_size_t z = 0; z < dn.size; z++)
                if (dn.ptr[z] == (proven_u8)'#') { inst = true; break; }
            if (inst) continue;
        }
        // ★ 이 최상위 form(대개 op 하나)이 묶는 이름을 먼저 모은다 — 지역이 먼저이므로.
        g_ck_loc.c = 0;
        ck_loc_collect(f);
        ck_vis_walk(out, f, v, nv, cur);
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// ★★★ **엔트리 포인트** — 이 언어로 **실행 파일을 만들 수 있게 한다** (RFC-0030)
//
//   RFC-0030 은 **채택**됐고 **구현이 0** 이었다. `entry` 도 `main` 도 종료 상태도 **문자열조차
//   없었다.** 즉 **이 언어로 프로그램을 만들 수 없었다** — 라이브러리만 만들 수 있었다.
//
//   D1: 엔트리는 **특별한 폼이 아니라 지정된 보통 op** 이다. 관례 이름 **`main`**.
//       보일러플레이트 0 (`public static void main(String[])` 류 의례 없음).
//   D3: **종료 상태는 엔트리의 출력이 정한다** — 수동 `exit()` 불요.
//         output void .   → 언제나 정상 종료(0)
//         output u8 .     → **그 값이 곧 OS 종료 코드**
//
//   ★ v1 의 한계(정직하게): **엔트리는 인자를 받지 않는다.** RFC-0030 D2 는 args/env 를
//     **capability 입력**으로 받으라고 한다(argv 매직 아님) — 그런데 **capability(RFC-0011)가
//     아직 안 지어졌다.** 그래서 **못 한다고 말한다**: 인자를 선언하면 E-ENTRY-PARAMS.
//     **argv 매직 파라미터를 임시로 넣지 않는다** — 그것은 RFC 가 명시적으로 거부한 것이다.
static void ck_entry(low_check_result_t *out, const low_parse_result_t *pr) {
    for (proven_size_t i = 0; i < pr->nforms; i++) {
        const low_cst_t *f = pr->forms[i]; ck_cur_form = f;
        if (f->kind != LOW_CST_FORM || f->nkids < 2 || !ck_atom(f->kids[0])) continue;
        low_kw_t kw = f->kids[0]->tok.kw;
        if (kw != LOW_KW_FN && kw != LOW_KW_PROC) continue;
        if (!ck_atom(f->kids[1]) || !veq(f->kids[1]->tok.lex, "main")) continue;

        low_op_header_t h = low_op_header(f);
        // ★ RFC-0030 D2′ — 엔트리 파라미터는 **전부 capability** 여야 한다. 데이터 파라미터는
        //   곧 매직 argv 라서 D2 가 명시적으로 거부한 것이고, cap 이라도 **줄 수 없는 권한**
        //   (env·io — 아직 실체가 없다)을 받는 척하면 그것이 장식이다(§0). 지금 줄 수 있는
        //   것은 `cap args` 하나 — 목록은 실체가 생길 때마다 자란다.
        for (proven_size_t q = 0; q < h.np; q++) {
            proven_size_t capw = 0; bool is_cap = false;
            for (proven_size_t w = h.p[q].ts; w < h.p[q].te && w < f->nkids; w++)
                if (ck_atom(f->kids[w]) && veq(f->kids[w]->tok.lex, "cap")) { is_cap = true; capw = w; break; }
            if (!is_cap) {
                emit(out, "E-ENTRY-PARAMS",
                     "the entry point takes CAPABILITY inputs only (`input a cap args .`). A data "
                     "parameter here would be a magic argv — RFC-0030 D2 rejects that: arguments "
                     "are a RIGHT the runtime hands you, queried via `count`/`arg` on the cap",
                     f->kids[1]->tok.line);
                continue;
            }
            // ★ 줄 수 있는 cap 만 받는다. 목록은 **실체가 생길 때마다 자란다** —
            //   args(2026-07-19) → + env(같은 날). io 는 RFC-0069 의 실체가 먼저다.
            bool givable = false;
            if (capw + 1 < h.p[q].te && capw + 1 < f->nkids && ck_atom(f->kids[capw + 1])) {
                proven_u8str_view_t kd = f->kids[capw + 1]->tok.lex;
                givable = veq(kd, "args") || veq(kd, "env") || veq(kd, "io") ||
                          veq(kd, "allocator") ||   // ★ A3 — RFC-0043 의 할당 권한
                          // ★★★★ **자라는 뿌리 — cap heap** (RFC-0112 D2(2) · WO-0211). 호스티드에서만 줄 수 있다 —
                          //   프리스탠딩은 `ck_alloc_cap` 이 `E-HEAP-NOHOST` 로 먼저 거절한다.
                          veq(kd, "heap") ||
                          veq(kd, "file_system") ||  // ★ A4 — RFC-0069 §6 의 파일 권한
                          veq(kd, "net") ||          // ★ 소켓 — cap net (socketpair loopback)
                          veq(kd, "tty") ||          // ★ 터미널 — cap tty (raw·키·크기)
                          // ★★★★★ **원자 연산 — cap atomic** (2026-08-07, 소유자 결정).
                          //   이것이 없어서 `lib/spsc.low` 을 **어떤 실물 프로그램도 부를 수
                          //   없었다**: 그 모듈의 모든 op 이 `cap atomic` 을 받는데 진입점이
                          //   그 권한을 못 받으니 사슬이 시작될 수가 없었다. 원장은 그것을
                          //   *"안 쓰는 모듈"* 로 셌지만 실은 **쓸 수 없는** 모듈이었다.
                          //   ☞ 다른 여섯과 달리 atomic 은 **OS 자원이 아니다** — 런타임이
                          //     건네줄 실체가 없고, 이 언어에서 cap 은 애초에 **값이 아니라
                          //     권리**다(`param_cap`). 그래서 목록에 넣는 것이 곧 실체다:
                          //     뜻은 *"이 코드가 원자 연산을 쓴다"* 는 표시이고, `effects
                          //     atomic` 과 짝을 이뤄 호출자에게 그 비용을 보이게 한다.
                          veq(kd, "atomic") ||
                          // ★★★ **시계 — cap clock** (RFC-0090 N1, 2026-08-07).
                          //   atomic 과 달리 이것은 **결정성을 깬다**: 같은 입력에 다른 답이다.
                          //   그래서 권한이 더 무겁고, 시그니처에 보이는 값도 더 크다.
                          veq(kd, "clock") ||
                          // ★★★ **난수 — cap random** (RFC-0090 N3b, 2026-08-11).
                          //   시계와 **같은 이유**로 권한이다: 결정성을 깬다 — 같은 입력에 다른 답.
                          //   ☞ 그런데 시계보다 무겁다. 시계는 *언제인가*를 새게 하고, 난수는
                          //     **키·토큰·주소 무작위화**의 재료다. 그 재료가 어디서 왔는지
                          //     시그니처에 안 적히면 그것을 쓴 코드는 감사할 수 없다.
                          //   ★ 재현 가능한 rng(`rng_seed`/`rng_next`)는 **권한이 없다** —
                          //     시드를 호출자가 주므로 순수 계산이다. 권한이 필요한 것은
                          //     **OS 에게 엔트로피를 달라고 하는 자리** 하나뿐이다.
                          veq(kd, "random");
            }
            if (!givable)
                emit(out, "E-ENTRY-CAP",
                     "the entry asked for a capability the runtime cannot hand it yet — only "
                     "`cap args`, `cap env`, `cap io`, `cap allocator`, `cap file_system`, `cap net`, `cap tty`, `cap atomic`, `cap random` and `cap clock` are provided "
                     "(RFC-0030 D2\u2032). Declaring a right "
                     "nobody can grant would be decoration, and decoration is a lie",
                     f->kids[1]->tok.line);
        }

        // 종료 상태 = 출력 (D3). `void`/작은 정수 → 그 값. 또는 `result<T>E` → ok=0·error=code(RFC-0030 결정).
        bool ok = false;
        proven_size_t nw = 0;
        proven_u8str_view_t w0 = { 0 };
        for (proven_size_t j = h.out_s; j < h.out_e; j++)
            if (ck_atom(f->kids[j])) { if (nw == 0) w0 = f->kids[j]->tok.lex; nw++; }
        if (nw == 1 && (veq(w0, "void") || veq(w0, "u8") || veq(w0, "i32") || veq(w0, "u32"))) ok = true;
        else if (veq(w0, "result")) ok = true;   // ★ result<T>E — ok→종료 0·error→code(e) (RFC-0030 D3, 2026-07-17)
        if (!ok)
            emit(out, "E-ENTRY-OUTPUT",
                 "the entry point's OUTPUT is its EXIT STATUS (RFC-0030 D3) — no manual `exit()`. "
                 "It accepts `output void .` (always 0), `output u8 .` (that value IS the OS exit "
                 "code), or `output result <T> <E> .` (ok → 0, error → a nonzero code from the error "
                 "variant, printed to stderr)",
                 f->kids[1]->tok.line);
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// ★★★ **`unsafe` 규율을 강제한다** — 수식자가 **장식**이었다 (RFC-0011 · SPEC-003 §21)
//
//   `effects unsafe .` 를 선언한 op 이 **`unsafe` 수식자 없이 통과했다.** 즉:
//     · 안전하지 않은 일을 하겠다고 **선언해 놓고**
//     · 그 자격을 **아무도 묻지 않았다.**
//   그리고 도구는 *"unsafe discipline is not enforced"* 라고 **자백만** 했다.
//
//   ⇒ 두 방향 다 검사한다:
//     · `effects unsafe` 인데 수식자가 없다      → **E-UNSAFE-UNDECLARED**
//     · `unsafe` 라고 붙였는데 unsafe 효과가 없다 → **E-UNSAFE-UNUSED**
//       (쓰지 않는 `unsafe` 는 **거짓 경보**다 — 진짜 unsafe 를 못 보게 만든다.)
static void ck_unsafe(low_check_result_t *out, const low_parse_result_t *pr) {
    for (proven_size_t i = 0; i < pr->nforms; i++) {
        const low_cst_t *f = pr->forms[i]; ck_cur_form = f;
        if (f->kind != LOW_CST_FORM || f->nkids < 2 || !ck_atom(f->kids[0])) continue;
        low_kw_t kw = f->kids[0]->tok.kw;
        if (kw != LOW_KW_FN && kw != LOW_KW_PROC) continue;
        low_op_header_t h = low_op_header(f);
        bool decl_unsafe = false;
        for (proven_size_t j = h.eff_s; j < h.eff_e; j++)
            if (ck_atom(f->kids[j]) && veq(f->kids[j]->tok.lex, "unsafe")) decl_unsafe = true;
        if (decl_unsafe && !f->is_unsafe)
            emit(out, "E-UNSAFE-UNDECLARED",
                 "this op declares the `unsafe` EFFECT but is not marked `unsafe`. The effect says "
                 "WHAT it does; the modifier says WHO takes responsibility. Write `unsafe proc …` "
                 "— an unsafe op that nobody signed for is exactly the hole the discipline exists "
                 "to close",
                 f->kids[1]->tok.line);
        if (!decl_unsafe && f->is_unsafe)
            emit(out, "E-UNSAFE-UNUSED",
                 "this op is marked `unsafe` but declares no `unsafe` effect. An `unsafe` that buys "
                 "nothing is a FALSE ALARM — and false alarms are how real ones stop being read. "
                 "Drop the modifier",
                 f->kids[1]->tok.line);
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// ★★★ **`comptime` 값 파라미터를 강제한다** — 안 그러면 그 낱말이 **거짓말**이다
//
//   `input comptime n u8 .` 은 *"이 인자는 **컴파일타임 상수**다"* 라는 **선언**이다.
//   그런데 **런타임 값을 넘겨도 조용히 통과**했다:
//
//       fn g input comptime n u8 . input a u8 . …
//       fn f input x u8 . … do  return g x 1 .  end     →  ok · f(5) = 6
//
//   ★★ 그리고 이 자리는 원래 **W-NOT-YET 로 자백**하고 있었다. 제네릭을 지으면서
//     `comptime` 표식을 **살렸고**, 그때 **자백도 같이 지웠다.** 그러면 남는 것은
//     **조용한 거짓말**이고, **그것이 자백보다 나쁘다.**
//   ⇒ 이제 **강제한다**: comptime 자리의 인자는 **정수 리터럴** 또는 **모듈 상수**여야 한다.
//     (타입 파라미터는 단형화가 이미 강제한다 — 타입 이름이 아니면 인스턴스가 안 선다.)
static bool ck_is_int_lit(proven_u8str_view_t v) {
    if (!v.size) return false;
    for (proven_size_t i = 0; i < v.size; i++) {
        proven_u8 ch = v.ptr[i];
        if (i == 0 && (ch == '-' || ch == '+') && v.size > 1) continue;
        if (!(ch >= '0' && ch <= '9') && ch != '_' && ch != 'x' && ch != 'X' &&
            !(ch >= 'a' && ch <= 'f') && !(ch >= 'A' && ch <= 'F')) return false;
    }
    return (v.ptr[0] >= '0' && v.ptr[0] <= '9') || v.ptr[0] == '-' || v.ptr[0] == '+';
}
// ★ MM4 — 정수 리터럴의 **값**(십진·0x16진·밑줄 허용). 실패면 false.
static bool ck_int_val(proven_u8str_view_t v, proven_i64 *out) {
    if (!v.size) return false;
    proven_size_t i = 0; bool neg = false;
    if (v.ptr[0] == '-' || v.ptr[0] == '+') { neg = (v.ptr[0] == '-'); i = 1; if (v.size < 2) return false; }
    proven_u64 acc = 0; int base = 10;
    if (v.size - i >= 2 && v.ptr[i] == '0' && (v.ptr[i+1] == 'x' || v.ptr[i+1] == 'X')) { base = 16; i += 2; if (i >= v.size) return false; }
    bool any = false;
    for (; i < v.size; i++) {
        proven_u8 ch = v.ptr[i];
        if (ch == '_') continue;
        int d;
        if (ch >= '0' && ch <= '9') d = ch - '0';
        else if (base == 16 && ch >= 'a' && ch <= 'f') d = ch - 'a' + 10;
        else if (base == 16 && ch >= 'A' && ch <= 'F') d = ch - 'A' + 10;
        else return false;
        if (d >= base) return false;
        acc = acc * (proven_u64)base + (proven_u64)d; any = true;
    }
    if (!any) return false;
    *out = neg ? -(proven_i64)acc : (proven_i64)acc;
    return true;
}
// ★ MM4 — scrutinee 이름이 **부호 없는 정수 타입**(u8/u16/u32/u64)의 파라미터/지역이면 그 폭(비트)을
//   돌려준다(아니면 0). op 의 `input <name> <ty>` 와 본문의 `let/var <name> <ty>` 를 훑는다.
static proven_u8 ck_uint_width_of(const low_cst_t *op, proven_u8str_view_t name) {
    if (!op || op->kind != LOW_CST_FORM) return 0;
    proven_u8str_view_t ty = { 0 };
    // 입력 절: `input <name> <ty> .` — op FORM 의 평평한 자식들에서 input 다음다음이 타입.
    for (proven_size_t i = 0; i + 2 < op->nkids; i++) {
        if (!ck_atom(op->kids[i]) || !veq(op->kids[i]->tok.lex, "input")) continue;
        if (ck_atom(op->kids[i+1]) && proven_u8str_view_eq(op->kids[i+1]->tok.lex, name) && ck_atom(op->kids[i+2]))
            ty = op->kids[i+2]->tok.lex;
    }
    // 본문 let/var: `let <name> <ty> …`
    const low_cst_t *bd = (op->nkids && op->kids[op->nkids-1]->kind == LOW_CST_BLOCK) ? op->kids[op->nkids-1] : NULL;
    if (bd) for (proven_size_t q = 0; q < bd->nkids; q++) {
        const low_cst_t *s = bd->kids[q];
        if (s->kind != LOW_CST_FORM || s->nkids < 3 || !ck_atom(s->kids[0])) continue;
        if ((s->kids[0]->tok.kw == LOW_KW_LET || s->kids[0]->tok.kw == LOW_KW_VAR) &&
            ck_atom(s->kids[1]) && proven_u8str_view_eq(s->kids[1]->tok.lex, name) && ck_atom(s->kids[2]))
            ty = s->kids[2]->tok.lex;
    }
    if (!ty.size) return 0;
    if (veq(ty, "u8")) return 8;
    if (veq(ty, "u16")) return 16;
    if (veq(ty, "u32")) return 32;
    if (veq(ty, "u64")) return 64;
    return 0;
}
static bool ck_is_module_const(const low_parse_result_t *pr, proven_u8str_view_t nm) {
    for (proven_size_t i = 0; i < pr->nforms; i++) {
        const low_cst_t *f = pr->forms[i]; ck_cur_form = f;
        if (f->kind != LOW_CST_FORM || f->nkids < 2 || !ck_atom(f->kids[0])) continue;
        if (f->kids[0]->tok.kw == LOW_KW_LET && ck_atom(f->kids[1]) &&
            proven_u8str_view_eq(f->kids[1]->tok.lex, nm)) return true;
    }
    return false;
}
typedef struct { proven_u8str_view_t name; proven_u32 mask; } ck_cts_t;   // comptime **값** 파라미터 비트

static void ck_comptime_walk(low_check_result_t *out, const low_parse_result_t *pr,
                             const low_cst_t *nd, const ck_cts_t *tab, proven_size_t nt) {
    if (!nd) return;
    if (nd->kind == LOW_CST_FORM && nd->nkids >= 2 && ck_atom(nd->kids[0]))
        for (proven_size_t i = 0; i < nt; i++) {
            if (!tab[i].mask) continue;
            if (!proven_u8str_view_eq(nd->kids[0]->tok.lex, tab[i].name)) continue;
            for (proven_size_t p = 0; p < 16 && 1 + p < nd->nkids; p++) {
                if (!((tab[i].mask >> p) & 1u)) continue;
                const low_cst_t *a = nd->kids[1 + p];
                bool ok = ck_atom(a) && (ck_is_int_lit(a->tok.lex) ||
                                          ck_is_module_const(pr, a->tok.lex));
                if (!ok)
                    emit(out, "E-COMPTIME-ARG",
                         "this argument sits in a `comptime` parameter, so it must be a "
                         "COMPILE-TIME CONSTANT (an integer literal, or a module `let`). A runtime "
                         "value there makes the word `comptime` a lie — and the analysis, the "
                         "folding and the monomorphisation all believe it",
                         a->line ? a->line : nd->line);
            }
        }
    for (proven_size_t i = 0; i < nd->nkids; i++)
        ck_comptime_walk(out, pr, nd->kids[i], tab, nt);
}

static void ck_comptime(low_check_result_t *out, const low_parse_result_t *pr) {
    ck_cts_t tab[64]; proven_size_t nt = 0;
    for (proven_size_t i = 0; i < pr->nforms && nt < 64; i++) {
        const low_cst_t *f = pr->forms[i]; ck_cur_form = f;
        if (f->kind != LOW_CST_FORM || f->nkids < 2 || !ck_atom(f->kids[0])) continue;
        low_kw_t kw = f->kids[0]->tok.kw;
        if (kw != LOW_KW_FN && kw != LOW_KW_PROC) continue;
        low_op_header_t h = low_op_header(f);
        proven_u32 m = 0;
        for (proven_size_t q = 0; q < h.np && q < 16; q++)
            if (h.p[q].is_comptime && h.p[q].core < f->nkids && ck_atom(f->kids[h.p[q].core]) &&
                !veq(f->kids[h.p[q].core]->tok.lex, "type"))     // 타입 파라미터는 단형화가 본다
                m |= 1u << q;
        if (!m) continue;
        tab[nt].name = h.name; tab[nt].mask = m; nt++;
    }
    if (!nt) return;
    for (proven_size_t i = 0; i < pr->nforms; i++) ck_comptime_walk(out, pr, pr->forms[i], tab, nt);
}

// ═══════════════════════════════════════════════════════════════════════════
// ★★★ **계층 × 효과 게이팅** (RFC-0039 D4) — *"ATtiny 에 thread pool" = 컴파일 거부*
//
//   RFC-0039 의 D1 이 못박은 것: **동시성 *안전*은 코어이고, *런타임*은 라이브러리다.**
//   그러면 언어가 줄 것은 **런타임이 아니라 게이트**다:
//
//     · 하드웨어 규모(tier)가 **어떤 효과를 감당할 수 있는지** 선언한다.
//     · 그 규모가 못 감당하는 효과를 쓰는 op 은 **컴파일이 거부된다.**
//     · 그리고 **효과는 호출 사슬을 타고 전파되므로**, thread pool 을 부르는 코드는
//       **부르는 자리에서** 거부된다 — 링크 시간이 아니라 **컴파일 시간**에.
//
//   ⇒ **규모-기능 불일치를 정적으로 차단한다**(RFC-0039 D4). 그것이 이 RFC 의 약속이다.
//
//        t0  협력 floor (ATtiny·bare)   none unsafe panic state wait cancel
//        t1  small     (Cortex-M)       + io device
//        t2  rtos                       + alloc lock atomic blocking
//        t3  hosted    (OS·멀티코어)     전부 (page_fault detach 포함)
//
//   ★ 기본은 **t3**(호스트) — 아무것도 안 적으면 아무것도 안 막는다. 게이트는 **선언해야** 문다.
typedef struct { const char *name; const char *const *eff; proven_size_t n; } ck_tier_t;
/* ★ 등급표는 위 `LOW_EFFECT_WORDS` 에서 **골라 만든다** — 손으로 두 번 적지 않는다.
 *   각 표는 「그 등급 이하가 감당하는 낱말」이다(누적). 낱말 하나를 늘리면 이 넷이
 *   저절로 따라오므로, 한쪽만 자라 갈리는 일이 없다. */
#define LOW_EFF_AT0_0(w) w,
#define LOW_EFF_AT0_1(w)
#define LOW_EFF_AT0_2(w)
#define LOW_EFF_AT0_3(w)
#define LOW_EFF_AT1_0(w) w,
#define LOW_EFF_AT1_1(w) w,
#define LOW_EFF_AT1_2(w)
#define LOW_EFF_AT1_3(w)
#define LOW_EFF_AT2_0(w) w,
#define LOW_EFF_AT2_1(w) w,
#define LOW_EFF_AT2_2(w) w,
#define LOW_EFF_AT2_3(w)
#define LOW_EFF_PICK0(w, t) LOW_EFF_AT0_##t(w)
#define LOW_EFF_PICK1(w, t) LOW_EFF_AT1_##t(w)
#define LOW_EFF_PICK2(w, t) LOW_EFF_AT2_##t(w)
#define LOW_EFF_PICK3(w, t) w,
static const char *const CK_T0[] = { LOW_EFFECT_WORDS(LOW_EFF_PICK0) };
static const char *const CK_T1[] = { LOW_EFFECT_WORDS(LOW_EFF_PICK1) };
static const char *const CK_T2[] = { LOW_EFFECT_WORDS(LOW_EFF_PICK2) };
static const char *const CK_T3[] = { LOW_EFFECT_WORDS(LOW_EFF_PICK3) };
static const ck_tier_t CK_TIERS[] = {
    { "t0", CK_T0, sizeof CK_T0 / sizeof *CK_T0 },
    { "t1", CK_T1, sizeof CK_T1 / sizeof *CK_T1 },
    { "t2", CK_T2, sizeof CK_T2 / sizeof *CK_T2 },
    { "t3", CK_T3, sizeof CK_T3 / sizeof *CK_T3 },
};

// ★★★ **`package` 매니페스트 — 지금까지 아무도 안 봤다** (RFC-0031 §6.1, 2026-07-20).
//
//   ☞ 실측: `package … end` 는 **무엇을 적든 통과**했다 — 모르는 열쇠말도, 빈 채로도,
//     **두 번 써도** `check: ok`. 프로젝트의 **정체를 말하는 파일**이 검사되지 않는 선언이었다.
//     RFC-0031 이 이 파일을 고정 이름으로 둔 근거가 *"도구·AI 가 소스 전체를 안 읽고 이것
//     하나만 읽어 정체를 싸게 파악한다"* 인데, **읽어서 믿을 수 없으면 그 값이 통째로 없다.**
//
//   ★★★ **형태를 RFC 와 다르게 정했다 — 어휘 비용 0 때문이다.**
//     RFC §6.1 은 `package … end` **블록**으로 그렸는데, 블록 머리는 파서가 `LOW_KW_*` 로
//     가르므로 **새 키워드가 필요하다**(43 → 44). 그런데 `build tier t0 .` 이 **이미 평평한
//     `<머리> <열쇠말> <값> .`** 으로 같은 일을 하고 있다.
//     ⇒ **`package <열쇠말> <값> .`** 로 간다. 새 키워드 **0**, 그리고 빌드 선언과 **같은
//       관용구**다 — 관용구가 둘이면 그것도 두 번째 표현이다(교훈 7).
//
//   ★ 지금 잰다: 열쇠말이 닫힌 집합인가 · `name` 과 `version` 이 있는가 · semver 인가 ·
//     같은 열쇠말이 두 번 오지 않는가.
//   ☞ 안 지은 것(정직히): deps(레지스트리) · feature · config · 엔트리 발견 · `use` 이행폐포.
//     **소비자가 생길 때** 짓는다(RFC-0031 은 P2 다).
static bool ck_semver(proven_u8str_view_t v) {
    proven_size_t s = 0, e = v.size;
    if (e >= 2 && v.ptr[0] == '"' && v.ptr[e - 1] == '"') { s = 1; e--; }
    if (s >= e) return false;
    int dots = 0; bool digit = false;
    for (proven_size_t i = s; i < e; i++) {
        proven_byte_t c = v.ptr[i];
        if (c == '.') { if (!digit) return false; dots++; digit = false; }
        else if (c >= '0' && c <= '9') digit = true;
        else return false;
    }
    return dots == 2 && digit;
}

// ★★★ **멤버가 하나뿐인 `task_group` 이 `concurrent` 를 요구하면 증명 가능한 데드락**
//   (RFC-0022 D-A 트리거 (b) · RFC-0071 A5, 2026-07-20).
//
//   `concurrent` 는 *"동료가 돌아야 완결된다"* 는 뜻이다. 그룹의 멤버가 **하나뿐인데**
//   그 하나가 그것을 요구하면, **동료가 존재할 수 없다** — 부모는 그룹에서 기다리고 있다.
//   ⇒ 이것은 "느릴 수 있다" 가 아니라 **어떤 인터리빙으로도 안 끝난다**. 컴파일 시점에 안다.
//
//   ☞ 전엔 **런타임에만** 죽었다(E-VM-DEADLOCK / `panic: deadlock`). 실측 2026-07-20:
//     `check: ok` 인데 양 백엔드가 데드락으로 죽었다 — **도구가 아는 것을 안 말하고 있었다.**
//   ★ 이것이 RFC-0022 D-A 가 어휘 등재의 조건으로 요구한 **"무는 검사"** 다.
//     검사 없이 낱말만 넣으면 그것이 장식이고, §0 이 거절하는 그것이다.
static void ck_lone_concurrent(low_check_result_t *out, const low_cst_t *nd,
                               const low_opinfo_t *tab, proven_size_t n) {
    if (!nd) return;
    if (nd->kind == LOW_CST_FORM && nd->nkids > 0 && nd->kids[0]->kind == LOW_CST_ATOM &&
        veq(nd->kids[0]->tok.lex, "task_group")) {
        if (ck_group_members(nd) == 1) {
            unsigned e = EFF_NONE;
            for (proven_size_t i = 0; i < nd->nkids; i++) e |= walk_effects(nd->kids[i], tab, n);
            if (e & EFF_CONCURRENT)
                emit(out, "E-CONC-ALONE",
                     "this `task_group` spawns exactly ONE task, and that task needs a PEER to "
                     "finish (`concurrent` — a `chrecv` on an empty channel, or a `chsend` on a "
                     "full one). There is no peer: the parent is waiting on the group. This is not "
                     "\"might be slow\" — NO interleaving completes, so it is a provable deadlock, "
                     "and the compiler can see it here instead of letting it die at runtime. "
                     "Spawn the other side in the same group, or use `wait` I/O, which the KERNEL "
                     "wakes and which needs no peer (RFC-0022 D-A)",
                     nd->line);
        }
        return;   // 안쪽 그룹은 이 그룹이 답했다고 본다(중첩은 A5 밖 — 후속)
    }
    for (proven_size_t i = 0; i < nd->nkids; i++) ck_lone_concurrent(out, nd->kids[i], tab, n);
}

// 한 subtree 안에 `word` 원자가 있는가(비이행 — **직접** 나타나는 것만).
static bool ck_subtree_has_atom(const low_cst_t *nd, const char *word) {
    if (!nd) return false;
    if (nd->kind == LOW_CST_ATOM) return veq(nd->tok.lex, word);
    for (proven_size_t i = 0; i < nd->nkids; i++)
        if (ck_subtree_has_atom(nd->kids[i], word)) return true;
    return false;
}

// subtree 에서 `word` 원자에 닿는가 — **파일 내 op 을 따라 이행적으로**(호출 이름 원자 → 그 op
// 본체로 내려간다). 못 보는 op(외부/본체 없음)은 **따라가지 않는다** — 놓치면 그저 거부를 덜
// 할 뿐이라 안전한 방향이다. `seen` 은 재귀(op 이 자기·서로를 부름) 사이클 차단.
static bool ck_reaches_atom(const low_cst_t *nd, const low_opinfo_t *tab, proven_size_t n,
                            const char *word, const low_opinfo_t **seen, proven_size_t *nseen,
                            proven_size_t cap) {
    if (!nd) return false;
    if (nd->kind == LOW_CST_ATOM) {
        if (veq(nd->tok.lex, word)) return true;
        // 이 원자가 **파일 내 op 의 이름**이면 그 op 의 본체로 이행한다(이름공간은 평면 —
        // op 이름과 같은 원자는 그 op 를 가리킨다: E-NAME-DUP 이 지역 가림을 이미 막는다).
        for (proven_size_t i = 0; i < n; i++) {
            if (!proven_u8str_view_eq(tab[i].name, nd->tok.lex) || !tab[i].body) continue;
            for (proven_size_t s = 0; s < *nseen; s++) if (seen[s] == &tab[i]) return false;
            if (*nseen < cap) seen[(*nseen)++] = &tab[i];
            return ck_reaches_atom(tab[i].body, tab, n, word, seen, nseen, cap);
        }
        return false;
    }
    for (proven_size_t i = 0; i < nd->nkids; i++)
        if (ck_reaches_atom(nd->kids[i], tab, n, word, seen, nseen, cap)) return true;
    return false;
}

// ★★★ **≥2 멤버 `task_group` 인데 전원이 소비자면 증명 가능한 데드락**
//   (E-CONC-DEADLOCK — RFC-0022 D-A 트리거 (b) 의 다중 멤버판, 2026-07-22).
//
//   `E-CONC-ALONE` 은 멤버가 **하나**뿐인 그룹을 잡는다. 그런데 멤버가 **둘 이상**이어도
//   그 전부가 **받기만**(chrecv) 하고 **아무도 보내지 않으면**(chsend 부재) 똑같이 데드락이다:
//   빈 채널에서 모두가 막히고, 부모는 그룹 join 에서 기다린다 — 어떤 인터리빙도 안 끝난다.
//   ☞ 전엔 `task_group` 이 멤버 ≥2 면 `concurrent` 를 **무조건 흡수**했다(walk_effects) — 동료가
//     정말 **맞는 짝**(sender)인지 안 보고. 그래서 소비자 둘짜리 그룹이 `check: ok` 였고 런타임
//     에서만 E-VM-DEADLOCK 으로 죽었다(실측 2026-07-22). 도구가 아는 것을 안 말하고 있었다.
//
//   ★★★ **건전성(soundness)이 전부다 — 합법 프로그램을 절대 오거부하지 않는다.**
//     채널은 **분석한 op 밖**에서 먹여질 수 있다(파라미터로 받은 채널 · 다른 모듈의 sender ·
//     밖으로 새는 채널). 그래서 **송신이 존재할 여지가 조금이라도 있으면 거부하지 않는다.**
//     오직 다음이 **전부** 성립할 때만 거부한다 — 이때 송신은 **어디에도 존재할 수 없다**:
//       (1) 파일에 `use`(수입) 가 **없다** — 프로그램이 자기완결적이다(외부 코드/sender 불가).
//       (2) 파일 전체에 `chsend` 원자가 **하나도 없다** — 어떤 채널도 먹여지지 않는다.
//       (3) 그룹을 담은 op 가 **파라미터가 없다** — 채널이 지역 생성이라 호출자가 못 먹인다
//           (이 op 이 export 되어 다른 파일이 채널을 넘겨 보낼 여지도 없앤다).
//       (4) 그룹이 (파일 내 op 을 따라 이행적으로) **chrecv 에 닿는다** — 진짜 막히는 수신자다.
//     (1)+(2) 로 이 프로그램 어디에도 송신이 없고, (3) 으로 채널이 밖에서 안 들어오며, join 동안
//     도는 것은 그룹의 태스크뿐이다(부모는 join 에서 막혀 채널을 밖으로 내보내지도 못한다) —
//     그 태스크 전부가 받기만 한다. ⇒ **어떤 인터리빙도 안 끝나는, 증명된 데드락.**
//   ☞ (1)(2)(3) 은 **호출부**(driver)가 판정해 참일 때만 이 워커를 부른다. 여기선 (4) 만 본다.
//   ☞ 보수적으로 흘려보내는 것(잔여, 정직히): `use` 있는 파일 · chsend 가 하나라도 있는 파일 ·
//     채널을 파라미터로 받는 op. 이들은 런타임 데드락 검출기(E-VM-DEADLOCK)가 여전히 잡는다 —
//     좁고 건전한 검사가 넓고 불건전한 검사보다 낫다(합법 거부는 0 이어야 한다).
static void ck_group_deadlock(low_check_result_t *out, const low_cst_t *nd,
                              const low_opinfo_t *tab, proven_size_t n) {
    if (!nd) return;
    if (nd->kind == LOW_CST_FORM && nd->nkids > 0 && nd->kids[0]->kind == LOW_CST_ATOM &&
        veq(nd->kids[0]->tok.lex, "task_group")) {
        if (ck_group_members(nd) >= 2) {
            const low_opinfo_t *seen[256]; proven_size_t nseen = 0;
            if (ck_reaches_atom(nd, tab, n, "chrecv", seen, &nseen, 256))
                emit(out, "E-CONC-DEADLOCK",
                     "this `task_group`'s tasks wait to RECEIVE (`chrecv`) but NOTHING sends. There "
                     "is no `chsend` anywhere in this self-contained module (no `use` imports), and "
                     "the channels are created locally — the enclosing op takes no parameters, so no "
                     "caller can feed them either. Every task blocks on an empty channel while the "
                     "parent waits on the group: NO interleaving completes, so it is a PROVABLE "
                     "deadlock the compiler names here instead of letting it die at runtime "
                     "(E-VM-DEADLOCK). Add a producer (`chsend`), or a task that sends, in the same "
                     "group (RFC-0022 D-A — the multi-member sibling of E-CONC-ALONE)",
                     nd->line);
        }
        return;   // 안쪽 그룹은 이 그룹이 답했다고 본다(E-CONC-ALONE 과 같은 규율)
    }
    for (proven_size_t i = 0; i < nd->nkids; i++) ck_group_deadlock(out, nd->kids[i], tab, n);
}

// ★★★ **불일치 채널 데드락 — chsend 가 있어도, 그것이 *다른* 채널이면** (E-CONC-DEADLOCK 확장,
//   2026-07-22). `ck_group_deadlock`(위)은 모듈에 `chsend` 가 **하나라도** 있으면 통째로 손을
//   뗀다 — 채널이 불투명한 u64 라 "이 chsend 가 저 chrecv 의 짝인가"를 안 봤기 때문이다. 여기서는
//   **지역 채널의 정체(identity)를 추적**해 그 구멍을 좁힌다: 그룹의 한 태스크가 받는 채널에
//   **어떤 chsend 도 닿을 수 없음**을 증명하면, 다른 채널에 chsend 가 있어도 데드락이다.
//
//   ☞ 실측(2026-07-22): 아래 프로그램은 `check: ok` 인데 양 백엔드가 E-VM-DEADLOCK 으로 죽었다 —
//       proc cons input ch u64 . … chrecv ch …            (a 에서 받는다)
//       proc prod input ch u64 . … chsend ch 7 …          (b 를 쓰지만 **spawn 되지 않는다**)
//       proc drv … var a … channel . var b … channel .
//         task_group do  spawn cons a .  spawn cons a .  end (a 를 아무도 안 먹인다)
//     chsend 가 모듈에 **있으므로** ck_group_deadlock 은 안 돌았다. a 의 정체를 좇으면 송신자가
//     없음이 증명되고, 이제 컴파일 시점에 잡는다.
//
//   ★★★ **건전성이 전부다.** 채널 값이 **추적 못 하는 곳으로 새면**(별칭 `var d be c` · 비-spawn
//     호출 인자 · 반환 · 다른 채널로 send · 괄호/식 안 · set 재대입 · 빌트인에 전달) 곧바로
//     `untracked` 로 **보수 판정**하고 **절대 거부하지 않는다**. 태스크는 **인자로만** 값을 받으므로
//     (vm_tgroup: box=-1, 스케줄러가 인자만으로 부른다 — 암묵 캡처 없음), 지역 채널 c 를 가질 수
//     있는 코드는 **오직** 우리가 좇는 명시적 spawn-인자 사슬뿐이다. 그 사슬 전체에 chsend 가 없고
//     (=send 미설정) 새는 곳이 없으면(=untracked 미설정), c 에는 **송신자가 존재할 수 없다**.
//   ★ 전제(호출부 판정): 모듈에 `use` 없음 + 담은 op 이 **파라미터 없음**(c 가 밖에서 안 들어온다).
static const low_cst_t *cd_find_chan_decl(const low_cst_t *nd, proven_u8str_view_t name) {
    if (!nd) return NULL;
    if (nd->kind == LOW_CST_FORM && nd->nkids >= 2 && nd->kids[0]->kind == LOW_CST_ATOM &&
        (nd->kids[0]->tok.kw == LOW_KW_VAR || nd->kids[0]->tok.kw == LOW_KW_LET) &&
        nd->kids[1]->kind == LOW_CST_ATOM && proven_u8str_view_eq(nd->kids[1]->tok.lex, name)) {
        for (proven_size_t i = 2; i + 1 < nd->nkids; i++)
            if (nd->kids[i]->kind == LOW_CST_ATOM && nd->kids[i]->tok.kw == LOW_KW_BE &&
                nd->kids[i + 1]->kind == LOW_CST_ATOM && veq(nd->kids[i + 1]->tok.lex, "channel"))
                return nd;   // `var/let NAME be … channel …` — 지역 채널 생성
    }
    for (proven_size_t i = 0; i < nd->nkids; i++) {
        const low_cst_t *r = cd_find_chan_decl(nd->kids[i], name);
        if (r) return r;
    }
    return NULL;
}

typedef struct { bool send, recv, untracked; } cd_flow_t;
typedef struct { const low_cst_t *form; proven_size_t pidx; } cd_visit_t;

// 채널 값(이름 `name`)의 **전 흐름**을 좇는다 — 이 몸체 `nd` 안의 모든 등장을 분류한다.
//   · chsend 의 채널 자리  → send   (누군가 이 채널에 보낸다)
//   · chrecv 의 채널 자리  → recv    (누군가 이 채널에서 받는다 — 짝 없으면 영영 막힌다)
//   · spawn P … name …    → P 의 그 파라미터로 **이행**해 계속 좇는다
//   · 그 밖의 자리         → untracked (별칭/반환/식/빌트인/재대입 등 — 좇을 수 없다 → 보수)
//   `decl` 은 `name` 을 **선언**하는 var/let form(있으면). 그 선언 자리(kids[1])는 사용이 아니다.
static void cd_flow(const low_cst_t *nd, proven_u8str_view_t name, const low_cst_t *decl,
                    const low_opinfo_t *tab, proven_size_t n,
                    cd_visit_t *vis, proven_size_t *nvis, proven_size_t vcap, cd_flow_t *fl) {
    if (!nd) return;
    if (nd->kind != LOW_CST_FORM && nd->kind != LOW_CST_BLOCK && nd->kind != LOW_CST_GROUP) return;
    bool is_spawn = nd->kind == LOW_CST_FORM && nd->nkids > 0 && nd->kids[0]->kind == LOW_CST_ATOM &&
                    veq(nd->kids[0]->tok.lex, "spawn");
    bool is_bind  = nd->kind == LOW_CST_FORM && nd->nkids > 1 && nd->kids[0]->kind == LOW_CST_ATOM &&
                    (nd->kids[0]->tok.kw == LOW_KW_VAR || nd->kids[0]->tok.kw == LOW_KW_LET);
    // 이 form 의 **직속 원자 자식**을 그 자리(부모 문맥)에서 분류한다.
    for (proven_size_t i = 0; i < nd->nkids; i++) {
        const low_cst_t *k = nd->kids[i];
        if (k->kind != LOW_CST_ATOM || !proven_u8str_view_eq(k->tok.lex, name)) continue;
        if (is_bind && i == 1) {                       // 이 form 이 name 을 **바인딩**한다
            if (nd == decl) continue;                  //   원 선언이면 — 사용 아님, 건너뜀
            fl->untracked = true; continue;            //   그 밖의 재바인딩/가림 → 보수
        }
        const low_cst_t *prev = (i >= 1) ? nd->kids[i - 1] : NULL;
        if (prev && prev->kind == LOW_CST_ATOM && veq(prev->tok.lex, "chsend")) { fl->send = true; continue; }
        if (prev && prev->kind == LOW_CST_ATOM && veq(prev->tok.lex, "chrecv")) { fl->recv = true; continue; }
        if (is_spawn && i >= 2 && nd->kids[1]->kind == LOW_CST_ATOM) {
            proven_size_t argk = i - 2;                // spawn P a0 a1 … → 인자 자리
            const low_opinfo_t *P = NULL;
            for (proven_size_t t = 0; t < n; t++)
                if (proven_u8str_view_eq(tab[t].name, nd->kids[1]->tok.lex)) { P = &tab[t]; break; }
            if (!P || !P->body || !P->form) { fl->untracked = true; continue; }
            low_op_header_t hh = low_op_header(P->form);
            if (hh.too_many || argk >= hh.np) { fl->untracked = true; continue; }
            bool seen = false;
            for (proven_size_t v = 0; v < *nvis; v++)
                if (vis[v].form == P->form && vis[v].pidx == argk) { seen = true; break; }
            if (seen) continue;                        // 그 (op,파라미터)는 이미 다 좇았다
            if (*nvis < vcap) vis[(*nvis)++] = (cd_visit_t){ P->form, argk };
            else { fl->untracked = true; continue; }   // 깊이 한계 → 보수
            cd_flow(P->body, hh.p[argk].name, NULL, tab, n, vis, nvis, vcap, fl);
            continue;
        }
        fl->untracked = true;                          // 그 밖의 자리 → 좇을 수 없다
    }
    for (proven_size_t i = 0; i < nd->nkids; i++)
        cd_flow(nd->kids[i], name, decl, tab, n, vis, nvis, vcap, fl);
}

// 지역 채널 c(`ebody` 안, 선언 `decl`)가 **증명 가능하게 송신자가 없는가** — 흐름이 전부
//   추적되고(untracked 미설정) chsend 가 하나도 안 닿으면(send 미설정) 참.
static bool cd_chan_provably_unfed(const low_cst_t *ebody, proven_u8str_view_t cname,
                                   const low_cst_t *decl, const low_opinfo_t *tab, proven_size_t n) {
    cd_visit_t vis[256]; proven_size_t nvis = 0; cd_flow_t fl = { 0 };
    cd_flow(ebody, cname, decl, tab, n, vis, &nvis, 256, &fl);
    return !fl.untracked && !fl.send;
}

// op P 가 자기 파라미터 k 에서 (이행적으로) **받는가**(chrecv) — 흐름이 추적되는 한.
static bool cd_param_recvs(const low_opinfo_t *P, proven_size_t k,
                           const low_opinfo_t *tab, proven_size_t n) {
    if (!P || !P->body || !P->form) return false;
    low_op_header_t h = low_op_header(P->form);
    if (h.too_many || k >= h.np) return false;
    cd_visit_t vis[256]; proven_size_t nvis = 0; cd_flow_t fl = { 0 };
    vis[nvis++] = (cd_visit_t){ P->form, k };
    cd_flow(P->body, h.p[k].name, NULL, tab, n, vis, &nvis, 256, &fl);
    return fl.recv && !fl.untracked;
}

// 그룹의 spawn 멤버들을 훑어(중첩 그룹 경계는 넘지 않는다) 지역 채널의 죽은 수신자를 찾는다.
static void cd_group_scan(const low_cst_t *nd, const low_cst_t *group, const low_cst_t *ebody,
                          const low_opinfo_t *tab, proven_size_t n, bool *dead, proven_u32 *line) {
    if (!nd || *dead) return;
    if (nd != group && nd->kind == LOW_CST_FORM && nd->nkids > 0 && nd->kids[0]->kind == LOW_CST_ATOM &&
        veq(nd->kids[0]->tok.lex, "task_group"))
        return;                                        // 중첩 그룹 — 그쪽 멤버는 그 그룹 소관
    if (nd->kind == LOW_CST_FORM && nd->nkids >= 2 && nd->kids[0]->kind == LOW_CST_ATOM &&
        veq(nd->kids[0]->tok.lex, "spawn") && nd->kids[1]->kind == LOW_CST_ATOM) {
        const low_opinfo_t *P = NULL;
        for (proven_size_t t = 0; t < n; t++)
            if (proven_u8str_view_eq(tab[t].name, nd->kids[1]->tok.lex)) { P = &tab[t]; break; }
        if (P)
            for (proven_size_t a = 2; a < nd->nkids; a++) {
                if (nd->kids[a]->kind != LOW_CST_ATOM) continue;
                proven_u8str_view_t arg = nd->kids[a]->tok.lex;
                const low_cst_t *decl = cd_find_chan_decl(ebody, arg);
                if (!decl) continue;                   // 지역 채널이 아니다 — 무시
                if (cd_param_recvs(P, a - 2, tab, n) &&
                    cd_chan_provably_unfed(ebody, arg, decl, tab, n)) {
                    *dead = true; *line = group->line; return;
                }
            }
    }
    for (proven_size_t i = 0; i < nd->nkids && !*dead; i++)
        cd_group_scan(nd->kids[i], group, ebody, tab, n, dead, line);
}

static void ck_group_mismatch(low_check_result_t *out, const low_cst_t *nd, const low_cst_t *ebody,
                              const low_opinfo_t *tab, proven_size_t n) {
    if (!nd) return;
    if (nd->kind == LOW_CST_FORM && nd->nkids > 0 && nd->kids[0]->kind == LOW_CST_ATOM &&
        veq(nd->kids[0]->tok.lex, "task_group")) {
        if (ck_group_members(nd) >= 2) {
            bool dead = false; proven_u32 line = nd->line;
            cd_group_scan(nd, nd, ebody, tab, n, &dead, &line);
            if (dead)
                emit(out, "E-CONC-DEADLOCK",
                     "this `task_group` has a task that waits to RECEIVE (`chrecv`) on a LOCAL "
                     "channel that NOTHING can send to. Tracking that channel's identity through "
                     "every place it flows (spawn arguments only — it never escapes to an alias, a "
                     "call, a return, or another channel), no `chsend` reaches it — even though this "
                     "module DOES send on a DIFFERENT channel. The receiving task blocks forever "
                     "while the parent waits on the group: NO interleaving completes, so it is a "
                     "PROVABLE deadlock the compiler names here instead of letting it die at runtime "
                     "(E-VM-DEADLOCK). Feed that channel with a `chsend`, or receive on the channel "
                     "your producer actually sends to (RFC-0022 D-A — mismatched-channel sibling)",
                     line);
        }
        // ★ 중첩 그룹도 **내려가서** 본다 — 여기선 건전하다. `cd_chan_provably_unfed` 는 채널을
        //   op **전 몸체**에 걸쳐 추적하므로, 안쪽 그룹의 수신 채널을 **바깥 형제**가 먹이면
        //   그 chsend 가 보이고(=fed) 절대 거부하지 않는다. 안쪽 그룹의 죽은 수신자는 바깥
        //   join 을 통해 온 프로그램을 막으므로 — 그 자리(안쪽 그룹 줄)에서 이름 붙인다.
        //   (`cd_group_scan` 은 각 그룹의 **직속** 멤버만 셈해 중첩 경계에서 멈춘다.)
    }
    for (proven_size_t i = 0; i < nd->nkids; i++) ck_group_mismatch(out, nd->kids[i], ebody, tab, n);
}

// ★★★ **톱레벨 form 은 선언 머리로 시작해야 한다** (2026-07-20, RFC-0044 감사에서 나옴).
//
//   ☞ 실측: `zzz_nonsense foo bar .` 같은 **아무 톱레벨 form 이나 조용히 통과**했다.
//     그리고 그 구멍으로 `drop res input r res . do…end` — RFC-0026 소멸자처럼 **보이는** 선언 —
//     이 아무것으로도 등록 안 된 채 삼켜졌다(같은 타입에 `drop` 을 **두 번** 선언해도 통과).
//   ★★★ 이것이 §0 그 자체다: 도구가 명세에 있는 형태를 **받는 척도 거절도 안 하고 삼킨다.**
//     읽는 사람은 소멸자를 정의했다고 믿는데, 아무 일도 안 일어난다 — 조용히 틀린 것이다.
//
//   ★ 톱레벨 머리는 **닫힌 집합**이다(선언 키워드 + 문맥 낱말 `build`/`package`). modifier
//     (`export`/`unsafe`/`extern`)는 CST 머리에 안 나타난다 — 파서가 `export fn` 을
//     `FORM[fn …]` 로 정규화하므로, 첫 원자는 **언제나 선언 키워드 자체**다(실측).
// ★★★ **struct/state 필드는 이름 + 타입이어야 한다** (2026-07-20, 톱레벨 구멍의 형제).
//
//   ☞ 실측: `struct s  a u64 .  bad .  end` 가 **check: ok** 였다 — `bad`(타입 없는 필드)와
//     빈 필드(`.`)를 IR 하강이 `nkids < 2` 로 **조용히 건너뛰었다.** 유령 필드가 되어
//     레이아웃에서 사라지고(실측 VM=16, `bad` 무시), 아무도 안 봤다.
//   ★ 톱레벨 구멍과 **같은 부류**다: 명세 형태를 받는 척도 거절도 안 하고 삼킨다(§0).
//     ⇒ 필드는 **원자 이름 + 최소 한 낱말 타입**이어야 한다. 아니면 거절한다.
//   ☞ `E-FIELD-MARK`(모르는 표지)는 하강이 이미 잡는다 — 여기서 잡는 것은 그 **앞 단계**,
//     즉 필드가 form 도 아니거나 이름/타입 자체가 없는 경우다.
static void ck_struct_fields(low_check_result_t *out, const low_parse_result_t *pr) {
    for (proven_size_t i = 0; i < pr->nforms; i++) {
        const low_cst_t *f = pr->forms[i]; ck_cur_form = f;
        if (f->kind != LOW_CST_FORM || !f->nkids || f->kids[0]->kind != LOW_CST_ATOM) continue;
        low_kw_t kw = f->kids[0]->tok.kw;
        // ★ struct 와 state 블록만 본다 — 그 둘의 자식이 **필드**다. actor 블록의 자식은
        //   state/핸들러 form 이라 여기서 안 본다(state 는 위에서 따로 걸린다).
        if (kw != LOW_KW_STRUCT && kw != LOW_KW_STATE) continue;
        // 필드 블록은 마지막 자식(무두 블록)이다.
        const low_cst_t *blk = NULL;
        for (proven_size_t z = 0; z < f->nkids; z++)
            if (f->kids[z]->kind == LOW_CST_BLOCK) blk = f->kids[z];
        if (!blk) continue;
        for (proven_size_t q = 0; q < blk->nkids; q++) {
            const low_cst_t *fld = blk->kids[q];
            if (fld->kind != LOW_CST_FORM || fld->nkids < 2 ||
                fld->kids[0]->kind != LOW_CST_ATOM || fld->kids[1]->kind != LOW_CST_ATOM)
                emit(out, "E-FIELD-FORM",
                     "a struct field must be a NAME followed by a TYPE (`x u64 .`). This field has "
                     "one or the other missing — and the lowerer would SILENTLY SKIP it, so the "
                     "field vanishes from the layout with no error while a reader believes it is "
                     "there. A form the spec neither accepts nor rejects is the PRINCIPLES.md §0 lie",
                     fld->kind == LOW_CST_FORM && fld->nkids && fld->kids[0]->kind == LOW_CST_ATOM
                       ? fld->kids[0]->tok.line : (blk->nkids ? f->line : f->line));
        }
    }
}

static void ck_toplevel(low_check_result_t *out, const low_parse_result_t *pr) {
    for (proven_size_t i = 0; i < pr->nforms; i++) {
        const low_cst_t *f = pr->forms[i]; ck_cur_form = f;
        if (f->kind != LOW_CST_FORM || !f->nkids || f->kids[0]->kind != LOW_CST_ATOM) continue;
        low_kw_t kw = f->kids[0]->tok.kw;
        if (kw == LOW_KW_MODULE || kw == LOW_KW_USE || kw == LOW_KW_FN || kw == LOW_KW_PROC ||
            kw == LOW_KW_STRUCT || kw == LOW_KW_ENUM || kw == LOW_KW_TYPE || kw == LOW_KW_NEWTYPE ||
            kw == LOW_KW_ACTOR || kw == LOW_KW_TRAIT || kw == LOW_KW_CONTRACT || kw == LOW_KW_TEST ||
            kw == LOW_KW_LET || kw == LOW_KW_VAR)   // ★ 모듈 수준 상수(`let`)/가변 전역(`var`)
            continue;   // ☞ 톱레벨 `var` 는 **여기서 안 잡는다** — 뒷단이 E-IR-UNSUP 으로
                        //   거절한다(가변 전역은 안전장치가 아직 없다, RFC-0029). 그 진단이
                        //   더 정확하므로 이 게이트가 가로채지 않는다.
        proven_u8str_view_t h = f->kids[0]->tok.lex;
        if (veq(h, "build") || veq(h, "package")) continue;   // 문맥 낱말(선언 자리 전용)
        // ★ `export let …` / `export test …` 는 **머리가 `export` 로 남는다** — 벗기기가
        //   fn·proc·struct·enum·type·newtype·actor·trait 만 벗기기 때문이다(low_cst.c).
        //   그때 위의 일반 문구는 «선언 머리로 시작하라» 고만 말해 **원인을 안 말한다**:
        //   쓴 사람은 선언 머리(`let`)를 이미 적었고, 틀린 것은 **거기에 `export` 를 붙인 것**이다
        //   (결함 노트 #76, 2026-09-16).
        // ★ 밀려난 것이 **문장**이면 원인은 «선언 머리를 안 썼다» 가 아니라 «앞의 `end` 가
        //   op 을 먼저 닫았다» 이다. 제어 머리는 `do` 없이도 **뒤따르는 한 문장**을 몸으로
        //   삼으므로(`if c . return 1 .` ≡ `if c . do return 1 . end`), `if` 에 `do` 를 안 쓰고
        //   `end` 를 적으면 그 `end` 가 **op 의 끝**이 된다 — 뒤 문장들이 통째로 밖으로 나온다.
        //   전에는 그 자리에서 이 진단과 `E-RETURN-PARTIAL` 만 나와 **원인을 아무도 말하지
        //   않았다**(결함 노트 #29, 2026-09-16).
        if (kw == LOW_KW_RETURN || kw == LOW_KW_SET || kw == LOW_KW_IF || kw == LOW_KW_WHILE ||
            kw == LOW_KW_GUARD || kw == LOW_KW_MATCH || kw == LOW_KW_BREAK ||
            kw == LOW_KW_CONTINUE) {
            emit(out, "E-TOPLEVEL",
                 "this is a STATEMENT, and it sits outside every op body — an `end` above it "
                 "closed the op earlier than you meant. The usual cause is a control head written "
                 "without `do`: `if <cond> .` alone takes the ONE statement that follows as its "
                 "body, so the `end` written for the `if` ends the OP instead. Write the body as "
                 "`if <cond> . do … end` whenever it holds more than one statement",
                 f->line);
            continue;
        }
        if (veq(h, "export") || veq(h, "unsafe") || veq(h, "extern")) {
            emit(out, "E-TOPLEVEL",
                 "`export` · `unsafe` · `extern` attach to a DECLARATION that can be named from "
                 "another module — fn · proc · struct · enum · type · newtype · actor · trait. What "
                 "follows here is none of those. A module constant (`let`), a module variable (`var`) "
                 "and a `test` block CANNOT be exported: a constant is not part of the module surface "
                 "(hand it out through an op — `fn limit output u64 . do return 100 . end`), and a "
                 "test belongs to the module that owns it. Written this way the modifier stays as the "
                 "form's head and the declaration under it is never seen at all",
                 f->line);
            continue;
        }
        emit(out, "E-TOPLEVEL",
             "a top-level form must begin with a DECLARATION head — module · use · fn · "
             "proc · struct · enum · type · newtype · actor · trait · contract · test · "
             "let (module constant) · build · package. This one is none of those, so the tool "
             "would SILENTLY IGNORE it. That is how a `drop <T> …` written as a destructor "
             "(RFC-0026 has no such surface yet) gets swallowed with no error and no effect — a "
             "form the spec neither accepts-and-runs nor rejects is the lie PRINCIPLES.md §0 is about",
             f->line);
    }
}

static void ck_package(low_check_result_t *out, const low_parse_result_t *pr) {
    static const char *KEYS[] = { "name", "version", "description", "date", "authors", "license" };
    bool any = false, has_name = false, has_ver = false;
    proven_u32 first_line = 0;
    bool dup[6] = { false, false, false, false, false, false };
    for (proven_size_t i = 0; i < pr->nforms; i++) {
        const low_cst_t *f = pr->forms[i]; ck_cur_form = f;
        if (f->kind != LOW_CST_FORM || !f->nkids || !ck_atom(f->kids[0])) continue;
        if (!veq(f->kids[0]->tok.lex, "package")) continue;
        if (!any) { any = true; first_line = f->line; }
        if (f->nkids < 3 || !ck_atom(f->kids[1])) {
            emit(out, "E-PKG-FORM",
                 "the manifest form is `package <key> <value> .` — the same shape as "
                 "`build tier t0 .`, so there is ONE idiom for declarations, not two. "
                 "(RFC-0031 §6.1 drew it as a `package … end` block; that would need a new "
                 "KEYWORD, and the closed vocabulary is 43 for a reason)", f->line);
            continue;
        }
        proven_u8str_view_t k = f->kids[1]->tok.lex;
        proven_size_t ki = 6;
        for (proven_size_t q = 0; q < 6; q++) if (veq(k, KEYS[q])) ki = q;
        if (ki == 6) {
            emit(out, "E-PKG-KEY",
                 "unknown key in `package`. The manifest keys are a CLOSED set: name · version · "
                 "description · date · authors · license. A key nobody reads is a key nobody "
                 "checks — and this file exists precisely so a tool can trust it WITHOUT reading "
                 "the whole source (RFC-0031 §6.1)", f->line);
            continue;
        }
        if (dup[ki] && ki != 4)   // authors 는 여럿일 수 있다
            emit(out, "E-PKG-DUP",
                 "this manifest key is declared twice. An identity has ONE value — with two, every "
                 "tool that reads the manifest must pick, and they will not all pick the same one",
                 f->line);
        dup[ki] = true;
        if (ki == 0) has_name = true;
        if (ki == 1) {
            has_ver = true;
            if (!ck_semver(f->kids[2]->tok.lex))
                emit(out, "E-PKG-VERSION",
                     "`version` must be a semver triple like \"1.4.0\" (RFC-0023 requires a "
                     "VERIFIED semver — a version string nobody parses cannot ORDER releases, and "
                     "ordering releases is the only thing a version is for)", f->line);
        }
    }
    if (!any) return;            // 매니페스트가 없으면 아무것도 요구하지 않는다
    if (!has_name)
        emit(out, "E-PKG-NONAME",
             "this unit declares `package` keys but no `name`. An identity file with no identity "
             "is decoration", first_line);
    if (!has_ver)
        emit(out, "E-PKG-NOVERSION",
             "this unit declares `package` keys but no `version`. Without one nothing downstream "
             "can pin or order this package (RFC-0023)", first_line);
}

static void ck_tier(low_check_result_t *out, const low_parse_result_t *pr) {
    const ck_tier_t *t = NULL;
    for (proven_size_t i = 0; i < pr->nforms; i++) {
        const low_cst_t *f = pr->forms[i]; ck_cur_form = f;
        if (f->kind != LOW_CST_FORM || f->nkids < 3 || !ck_atom(f->kids[0])) continue;
        if (!veq(f->kids[0]->tok.lex, "build") || !ck_atom(f->kids[1])) continue;
        if (!veq(f->kids[1]->tok.lex, "tier") || !ck_atom(f->kids[2])) continue;
        for (proven_size_t q = 0; q < sizeof CK_TIERS / sizeof *CK_TIERS; q++)
            if (veq(f->kids[2]->tok.lex, CK_TIERS[q].name)) t = &CK_TIERS[q];
        if (!t)
            emit(out, "E-BUILD-TIER",
                 "the concurrency TIER is a closed set: t0 (cooperative floor · ATtiny · bare) · "
                 "t1 (small · Cortex-M) · t2 (rtos) · t3 (hosted · OS). The tier says what the "
                 "HARDWARE CAN CARRY, and effects the tier cannot carry are REFUSED (RFC-0039 D4)",
                 f->line);
    }
    if (!t) return;              // 선언 안 하면 아무것도 안 막는다 (기본 = 호스트)

    for (proven_size_t i = 0; i < pr->nforms; i++) {
        const low_cst_t *f = pr->forms[i]; ck_cur_form = f;
        if (f->kind != LOW_CST_FORM || f->nkids < 2 || !ck_atom(f->kids[0])) continue;
        low_kw_t kw = f->kids[0]->tok.kw;
        if (kw != LOW_KW_FN && kw != LOW_KW_PROC) continue;
        low_op_header_t h = low_op_header(f);
        for (proven_size_t j = h.eff_s; j < h.eff_e; j++) {
            if (!ck_atom(f->kids[j])) continue;
            proven_u8str_view_t e = f->kids[j]->tok.lex;
            if (!effect_word_known(e)) continue;                 // E-EFFECT-UNDEF 가 따로 잡는다
            bool ok = false;
            for (proven_size_t q = 0; q < t->n; q++) if (veq(e, t->eff[q])) ok = true;
            if (!ok)
                emit(out, "E-TIER-EFFECT",
                     "this effect is not available at the declared concurrency TIER. The tier says "
                     "what the HARDWARE CAN CARRY — a thread pool on an ATtiny is not a runtime "
                     "problem, it is a COMPILE ERROR (RFC-0039 D4). And because effects PROPAGATE "
                     "along the call chain, the refusal lands where you CALL it, not at link time",
                     f->kids[j]->tok.line ? f->kids[j]->tok.line : f->line);
        }
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// ★★★ **할당 = 명시 권한** (RFC-0043 D1) — *ambient 힙은 없다*
//
//   대부분의 언어는 `new`/`malloc` 이 **암묵 전역 힙**에서 할당한다: 요청하지 않았고,
//   제어할 수 없고, **숨은 의존**이고, **ambient 권한**이다.
//
//   RFC-0043 D1: *"할당하려면 **allocator 를 인자로 받아야** 한다. 전역/ambient 힙 op 없음."*
//   그런데 **아무것도 강제되지 않았다**: `effects alloc` 이라 적고 **아무것도 안 받아도** 통과했다.
//   즉 **"이 op 은 할당한다" 는 선언만 있고, 어디서 할당하는지는 아무도 묻지 않았다.**
//
//   ⇒ `effects alloc` 인 op 은 **할당 권한을 인자로 받아야** 한다:
//        · `cap allocator`  — 명시 capability (D1)
//        · `region <r>`     — arena/bump (D7: *"region = allocator 종류 + 수명"*)
//      둘 다 *"어디서 할당하는가"* 를 말한다. 없으면 **E-ALLOC-NOCAP**.
//
//   ★ 그리고 **계층 게이트**(RFC-0039 D4)가 이미 t0/t1 에서 `alloc` 을 거부한다 ⇒
//     *"ATtiny 에 malloc"* 은 **이미 정적으로 차단**돼 있다(RFC-0043 D6).
// ═══════════════════════════════════════════════════════════════════════════
// ★★★ **인라인 asm — 검증할 수 없는 탈출구다. 그래서 *격리* 한다.** (RFC-0041 D1/D7)
//
//   컴파일러는 템플릿 **안을 못 읽는다**(어셈블러의 몫). 즉 asm 안에서는 **모든 불변식이
//   깨질 수 있고, 도구는 그것을 볼 수 없다.** 볼 수 없으면 **가둔다**:
//
//     · `unsafe`      — 못 보는 것을 정직하게 표시한다
//     · `cap machine` — 기계 명령을 낼 **권리를 건네받는다**(주변적 권한이 아니다)
//     · `effects unsafe` — 그리고 **시그니처에 실린다**(호출자가 안다)
//     · target ISA    — 이식성이 **없다**. 도구는 있는 척하지 않는다.
//
//   ★ 그리고 `options pure` 는 **컴파일러에게 하는 약속**이다: "부작용 없음 — 지워도 되고
//     합쳐도 된다." 그런데 같은 op 이 `effects io`/`state` 를 선언했다면 **두 말이 서로를
//     부정한다.** 둘 중 하나는 거짓말이고, **검사되지 않는 중복은 거짓말로 썩는다**(§0).
// ★★★ **asm 은 op 의 절로도, 몸의 문장으로도 온다** (RFC-0042 D11) — 격리는 **같아야 한다.**
//   몸 안의 `asm <타깃> … .` 문장을 찾는다(그 다음 heredoc 이 템플릿이다 — low_ir 가 짝짓는다).
static const low_cst_t *ck_asm_stmt_find(const low_cst_t *nd) {
    if (!nd || nd->kind != LOW_CST_FORM) return NULL;
    if (nd->nkids > 0 && ck_atom(nd->kids[0]) && nd->kids[0]->tok.kw == LOW_KW_NONE &&
        veq(nd->kids[0]->tok.lex, "asm")) return nd;
    for (proven_size_t i = 0; i < nd->nkids; i++) {
        const low_cst_t *k = nd->kids[i];
        if (!k) continue;
        if (k->kind == LOW_CST_FORM || k->kind == LOW_CST_BLOCK) {
            const low_cst_t *hit = NULL;
            if (k->kind == LOW_CST_BLOCK) {
                for (proven_size_t j = 0; j < k->nkids && !hit; j++) hit = ck_asm_stmt_find(k->kids[j]);
            } else hit = ck_asm_stmt_find(k);
            if (hit) return hit;
        }
    }
    return NULL;
}

// ★★★★★ **op 이 `asm` 을 가지는가 — 절이든 문장이든** (2026-09-04, WO-0187).
//   `ck_asm` 은 그런 op 에 `effects unsafe` 를 **요구**하고(E-ASM-NOEFFECT), 효과 워크는 그것을
//   **세야** 한다. 그런데 워크는 **문장 자리의 `asm` 만** 봤다: 헤더 절로 쓴
//   `unsafe proc wait_irq . … effects unsafe . asm cortex_m .` 은 못 세어, 요구대로 선언한 op 이
//   *"선언했는데 안 한다"*(W-EFFECT-OVER)로 경고됐다 — **요구와 계수가 서로를 부정한다.**
//   ⇒ 같은 병의 **셋째 자리**다(문장 asm 2026-08 · 장치 레지스터 2026-09-04 · 여기).
//     그래서 이번엔 판정을 **함수 하나로 뽑아** 요구하는 쪽과 세는 쪽이 **같은 것**을 부른다.
//   ☞ *두 곳이 같은 사실을 따로 판정하면 언젠가 갈린다 — 세 번 갈렸으면 함수로 만든다.*
static bool ck_op_has_asm(const low_cst_t *f) {
    if (!f || f->kind != LOW_CST_FORM || f->nkids < 2 || !ck_atom(f->kids[0])) return false;
    for (proven_size_t j = 2; j < f->nkids; j++)
        if (ck_atom(f->kids[j]) && veq(f->kids[j]->tok.lex, "asm")) return true;   // 헤더 절
    low_op_header_t h = low_op_header((low_cst_t *)f);
    if (h.body)
        for (proven_size_t j = 0; j < h.body->nkids; j++)
            if (ck_asm_stmt_find(h.body->kids[j])) return true;                    // 몸의 문장
    return false;
}
static void ck_asm(low_check_result_t *out, const low_parse_result_t *pr) {
    for (proven_size_t i = 0; i < pr->nforms; i++) {
        const low_cst_t *f = pr->forms[i]; ck_cur_form = f;
        if (f->kind != LOW_CST_FORM || f->nkids < 2 || !ck_atom(f->kids[0])) continue;
        low_kw_t kw = f->kids[0]->tok.kw;
        if (kw != LOW_KW_FN && kw != LOW_KW_PROC) continue;
        proven_size_t as = 0, ae = 0;
        for (proven_size_t j = 2; j < f->nkids; j++) {
            if (!ck_atom(f->kids[j]) || !veq(f->kids[j]->tok.lex, "asm")) continue;
            as = j + 1; ae = as;
            while (ae < f->nkids && ck_atom(f->kids[ae]) && !ck_clause_word(f->kids[ae]->tok.lex)) ae++;
            break;
        }
        low_op_header_t h = low_op_header(f);
        // ★ 절이 없으면 **몸**을 본다 — 문장 asm 도 같은 문을 지난다.
        const low_cst_t *stmt = NULL;
        if (as == 0 && h.body) {
            for (proven_size_t j = 0; j < h.body->nkids && !stmt; j++)
                stmt = ck_asm_stmt_find(h.body->kids[j]);
            if (!stmt) continue;
        } else if (as == 0) continue;
        proven_u32 ln = stmt ? stmt->kids[0]->tok.line : f->kids[1]->tok.line;

        if (!f->is_unsafe)
            emit(out, "E-ASM-NOUNSAFE",
                 "inline assembly without `unsafe`. The compiler does NOT read the template — "
                 "inside it, every invariant this language enforces can be broken and the tool "
                 "cannot see it. What cannot be checked must at least be MARKED", ln);

        bool has_cap = false, unsafe_eff = false, pure_opt = false, dirty_eff = false;
        for (proven_size_t q = 0; q < h.np; q++)
            for (proven_size_t z = h.p[q].ts; z < h.p[q].te; z++)
                if (ck_atom(f->kids[z]) && veq(f->kids[z]->tok.lex, "machine")) has_cap = true;
        // ★★★ **`vector N .` 이 곧 권한이다 — 여기서도** (RFC-0042 D5-b → D11, 2026-08-03).
        //
        //   ISR·리셋 진입은 **인자를 받을 수 없다**(E-ISR-PARAMS) ⇒ `cap machine` 을 영원히
        //   들 수 없다. 그리고 그것을 **건네받을 수도 없다**: D5-b 의 권한은 피호출자에게
        //   미치면 안 되므로(미치면 그 문이 샌다) `wait_irq k .` 같은 asm op 을 부를 길이 없다.
        //   ⇒ 리셋의 끝이 `while true . do end` — **바쁜 대기**였다. 배터리가 있는 기계에서
        //     `wfi` 와 그것의 차이가 전부다.
        //
        //   그래서 같은 논증을 그대로 세운다: **하드웨어가 불렀다는 사실이 그 권한이다.**
        //   ★ 그리고 이 권한도 **샐 수 없다** — 문이 정확히 하나 폭이다: 명령은 **그 op 의 몸
        //     안에서만** 나고, ISR 은 프로그램이 부를 수 없고(E-ISR-CALLED), 넘겨줄 인자가 없다.
        //   ★★ **보통 op 은 여전히 `cap machine` 을 받아야 한다** — 골든이 그 대칭을 검사한다.
        for (proven_size_t j = 2; j + 1 < f->nkids; j++)
            if (ck_atom(f->kids[j]) && veq(f->kids[j]->tok.lex, "vector")) has_cap = true;
        for (proven_size_t j = h.eff_s; j < h.eff_e; j++) {
            if (!ck_atom(f->kids[j])) continue;
            if (veq(f->kids[j]->tok.lex, "unsafe")) unsafe_eff = true;
            if (veq(f->kids[j]->tok.lex, "io") || veq(f->kids[j]->tok.lex, "state") ||
                veq(f->kids[j]->tok.lex, "device")) dirty_eff = true;
        }
        if (stmt) {
            for (proven_size_t j = 1; j < stmt->nkids; j++)
                if (ck_atom(stmt->kids[j]) && veq(stmt->kids[j]->tok.lex, "pure")) pure_opt = true;
        } else {
            for (proven_size_t j = as; j < ae; j++)
                if (ck_atom(f->kids[j]) && veq(f->kids[j]->tok.lex, "pure")) pure_opt = true;
        }

        if (!has_cap)
            emit(out, "E-ASM-NOCAP",
                 "this op emits raw machine instructions but receives NO right to do so. "
                 "Emitting instructions is a capability you are HANDED — `input k cap machine .` "
                 "— exactly like the heap (RFC-0043 D1) and the device bus (RFC-0042 D2). "
                 "An ambient escape hatch is one every caller silently inherits. (An INTERRUPT "
                 "handler or reset entry is the one exception and it is not ambient either: "
                 "`vector N .` IS the grant, because the hardware called you — RFC-0042 D5-b/D11)",
                 ln);
        if (!unsafe_eff)
            emit(out, "E-ASM-NOEFFECT",
                 "this op is assembly and does not declare `effects unsafe`. The effect row is how "
                 "a CALLER learns what it is taking on; assembly that hides in a clean signature is "
                 "the hidden cost this language exists to remove", ln);
        if (pure_opt && dirty_eff)
            emit(out, "E-ASM-OPTLIE",
                 "`options pure` tells the compiler this assembly has NO side effects — it may be "
                 "deleted, hoisted, or run once instead of twice. The effect row of the same op says "
                 "it touches io/state/device. Both cannot be true, and the compiler BELIEVES the "
                 "option (it cannot read the template). One of these two is a lie", ln);
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// ★★★ **아무도 안 읽는 손잡이는 거짓말이다** (RFC-0036 D5)
//
//   `build option smp bool default true .` 는 사용자에게 **"이것은 당신이 고르는 것"** 이라
//   말한다. 그런데 코드에 `config smp` 가 **하나도 없으면** 그 손잡이는 **아무것도 안 한다.**
//   menuconfig 에는 뜨고, 끄면 아무 일도 안 일어난다 — 그리고 그 사실을 **아무도 모른다.**
//
//   ★ 이것은 이 언어의 서명 검사와 **같은 모양**이다: 검사되지 않는 중복은 거짓말로 썩는다.
//     (죽은 `test` 절 · 아무것도 안 바꾸는 `access` 절 · 안 쓰인 asm 피연산자 — 전부 한 병이다.)
static bool ck_reads_config(const low_cst_t *nd, proven_u8str_view_t name) {
    if (!nd) return false;
    if (nd->kind == LOW_CST_FORM || nd->kind == LOW_CST_GROUP || nd->kind == LOW_CST_BLOCK)
        for (proven_size_t i = 0; i + 1 < nd->nkids; i++)
            if (ck_atom(nd->kids[i]) && veq(nd->kids[i]->tok.lex, "config") &&
                ck_atom(nd->kids[i + 1]) && proven_u8str_view_eq(nd->kids[i + 1]->tok.lex, name))
                return true;
    for (proven_size_t i = 0; i < nd->nkids; i++)
        if (ck_reads_config(nd->kids[i], name)) return true;
    return false;
}

static void ck_option(low_check_result_t *out, const low_parse_result_t *pr) {
    for (proven_size_t i = 0; i < pr->nforms; i++) {
        const low_cst_t *f = pr->forms[i]; ck_cur_form = f;
        if (f->kind != LOW_CST_FORM || f->nkids < 4 || !ck_atom(f->kids[0])) continue;
        if (!veq(f->kids[0]->tok.lex, "build") || !ck_atom(f->kids[1])) continue;
        if (!veq(f->kids[1]->tok.lex, "option") || !ck_atom(f->kids[2])) continue;
        proven_u8str_view_t nm = f->kids[2]->tok.lex;
        bool read = false;
        for (proven_size_t j = 0; j < pr->nforms && !read; j++)
            if (pr->forms[j] != f && ck_reads_config(pr->forms[j], nm)) read = true;
        if (!read)
            emit(out, "E-OPT-UNUSED",
                 "this build option is declared and NO code reads it (`config <name>`). It shows up "
                 "in the configuration, the user turns it off — and nothing happens. A knob that "
                 "does nothing is worse than no knob: it is a decision that LOOKS like it was made",
                 f->line);
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// ★★★ **C ABI / FFI** (RFC-0063) — **이 언어를 섬으로 두지 않는다.**
//
//   C 를 부르는 순간 **모든 불변식이 깨질 수 있다.** 보통의 언어는 여기서 포기한다
//   ("unsafe 니까"). **우리는 포기하지 않는다:**
//
//     ★ **계약이 경계를 지킨다** — `requires` 는 **C 를 부르기 전에**, `ensures` 는 **돌아온 뒤에**.
//       **C 안은 못 보지만, C 가 넘나드는 문은 우리 것이다.**
//
//   그리고 못 보는 것은 **가둔다**(인라인 asm 과 **같은 규율**):
//     unsafe(표시) · `cap c`(건네받는 권리) · effects(호출자가 안다).
// ★★ **내보낸 op 의 서명에 감춘 타입을 쓸 수 없다** (결함 노트 #55, 2026-09-16).
//   `export fn make_secret output secret .` 에서 `secret` 이 `export` 가 아니면, 들여온 쪽은
//   `k4.secret` 을 적는 순간 `E-VISIBILITY` 라 **결과를 받을 이름을 지을 수가 없다** — 쓸 수 없는
//   export 다. 그런데 거절은 **들여온 쪽에서만** 났다: 내보내는 쪽은 초록이었다. 자기 서명의
//   결함은 자기가 알아야 한다.
static bool ck_type_is_exported(const low_parse_result_t *pr, proven_u8str_view_t ty, bool *found,
                                const char *same_file) {
    *found = false;
    for (proven_size_t i = 0; i < pr->nforms; i++) {
        const low_cst_t *g = pr->forms[i];
        if (!(g->kind == LOW_CST_FORM && g->nkids >= 2 && ck_atom(g->kids[0]) && ck_atom(g->kids[1])))
            continue;
        low_kw_t k = g->kids[0]->tok.kw;
        if (k != LOW_KW_STRUCT && k != LOW_KW_ENUM && k != LOW_KW_NEWTYPE &&
            k != LOW_KW_ACTOR) continue;   // ★ `type` 별칭은 **투명**하다 — 쓰는 쪽이 바탕 타입을 적으면 된다
        if (!proven_u8str_view_eq(g->kids[1]->tok.lex, ty)) continue;
        // ★ **같은 모듈**의 타입만 따진다. 한 번역 단위에 여러 파일이 들어오고, 제네릭이
        //   단형화되면 **부르는 쪽의 타입 이름**이 내보낸 서명에 실려 온다 — 그것은 감춘 것이
        //   아니라 애초에 그 모듈의 것이다(lib/sortgen.low 의 `sort_by` 가 그 자리다).
        if (same_file && g->file && g->file != same_file) continue;
        if (same_file && !g->file) continue;
        *found = true;
        return g->is_export;
    }
    return false;
}
// ★★ **제네릭 op 은 타입 인자를 앞자리에 받는다 — 빠뜨리면 그렇게 말해야 한다**
//   (결함 노트 #57, 2026-09-16). 전에는 `E-IR-UNDEF: undefined name max_of` 였다 — op 은 분명히
//   있는데 «없다» 고 말하는 진단이다. 단형화가 타입 인자로 인스턴스를 만들므로, 인자가 없으면
//   만들 인스턴스가 없어 이름이 안 풀린 것뿐이다. 원인은 «없음» 이 아니라 «앞자리가 비었음» 이다.
static bool ck_is_type_word(const low_parse_result_t *pr, proven_u8str_view_t w) {
    static const char *PRIM[] = { "u8","u16","u32","u64","usize","i8","i16","i32","i64",
                                  "f32","f64","bool","str","void","slice","option","result" };
    for (size_t i = 0; i < sizeof PRIM / sizeof PRIM[0]; i++) if (veq(w, PRIM[i])) return true;
    for (proven_size_t i = 0; i < pr->nforms; i++) {
        const low_cst_t *g = pr->forms[i];
        if (!(g->kind == LOW_CST_FORM && g->nkids >= 2 && ck_atom(g->kids[0]) && ck_atom(g->kids[1])))
            continue;
        low_kw_t k = g->kids[0]->tok.kw;
        if ((k == LOW_KW_STRUCT || k == LOW_KW_ENUM || k == LOW_KW_TYPE || k == LOW_KW_NEWTYPE ||
             k == LOW_KW_ACTOR) && proven_u8str_view_eq(g->kids[1]->tok.lex, w)) return true;
    }
    return false;
}
// ★★ **`splat` 은 레인 수를 문맥에서 받는다** (결함 노트 #73, 2026-09-16).
//   `var m be mask 4 gt v (splat 5) .` 처럼 식 **안**에 바로 쓰면 몇 레인짜리를 지어야 하는지
//   알 자리가 없어 스칼라로 읽히고, 진단은 `E-TYPE-VAR: expected mask, found bool` 이라
//   **원인을 말하지 않았다**. 쓸 수 있는 자리는 하나다: `var lim be vec u32 4 splat 5 .`
//   — 선언된 벡터 타입이 레인 수를 말해 주는 자리.
static void ck_splat_walk(low_check_result_t *out, const low_cst_t *nd,
                          const low_cst_t *parent, proven_size_t idx) {
    if (!nd) return;
    bool is_splat = (nd->kind == LOW_CST_FORM && nd->nkids >= 1 && ck_atom(nd->kids[0]) &&
                     nd->kids[0]->tok.kw == LOW_KW_NONE && veq(nd->kids[0]->tok.lex, "splat")) ||
                    (nd->kind == LOW_CST_ATOM && nd->tok.kw == LOW_KW_NONE &&
                     veq(nd->tok.lex, "splat") && idx != 0);   // ★ 제 폼의 머리 자리는 제 자신이다
    if (is_splat && parent && parent->kind == LOW_CST_FORM && ck_atom(parent->kids[0])) {
        bool ok = false;
        low_kw_t k0 = parent->kids[0]->tok.kw;
        if (k0 == LOW_KW_LET || k0 == LOW_KW_VAR) {
            for (proven_size_t b = 1; b < parent->nkids; b++)
                if (ck_atom(parent->kids[b]) && parent->kids[b]->tok.kw == LOW_KW_BE && b + 1 == idx)
                    ok = true;                                   // `… be splat <값> .`
        } else if (k0 == LOW_KW_BE && idx == 1) ok = true;        // 갈라진 바인딩의 뒷줄
        if (!ok)
            emit(out, "E-VEC-SPLAT",
                 "`splat` fills every lane of a vector, and how many lanes there are comes from the "
                 "declared type — inside an expression there is nothing to say it, so the value is "
                 "read as a plain scalar and the surrounding comparison stops matching its `mask` "
                 "type. Bind it first, with the lane count written down: `var lim be vec u32 4 splat "
                 "5 .`, then use `lim`",
                 nd->line);
    }
    for (proven_size_t i = 0; i < nd->nkids; i++) {
        const low_cst_t *ch = nd->kids[i];
        // 저자가 적은 괄호는 자리만 감싼다 — 부모는 그대로 물려준다.
        if (ch && ch->kind == LOW_CST_GROUP && ch->nkids == 1)
            ck_splat_walk(out, ch->kids[0], parent && nd->kind == LOW_CST_GROUP ? parent : nd, i);
        else
            ck_splat_walk(out, ch, nd, i);
    }
}

typedef struct { proven_u8str_view_t name, mod; const char *file; } ck_tpl_t;
// 이 파일이 선언한 모듈 이름 (없으면 빈 것).
static proven_u8str_view_t ck_module_of_file(const low_parse_result_t *pr, const char *file) {
    for (proven_size_t i = 0; i < pr->nforms; i++) {
        const low_cst_t *g = pr->forms[i];
        if (g->kind == LOW_CST_FORM && g->nkids >= 2 && ck_atom(g->kids[0]) &&
            g->kids[0]->tok.kw == LOW_KW_MODULE && ck_atom(g->kids[1]) && g->file == file)
            return g->kids[1]->tok.lex;
    }
    return (proven_u8str_view_t){ 0 };
}
static void ck_typearg_walk(low_check_result_t *out, const low_cst_t *nd,
                            const low_parse_result_t *pr, const ck_tpl_t *tpl,
                            proven_size_t ntpl) {
    if (!nd) return;
    if (nd->kind == LOW_CST_FORM)
        for (proven_size_t z = 0; z + 1 < nd->nkids; z++) {
            if (nd->kids[z]->kind != LOW_CST_ATOM || nd->kids[z]->tok.kw != LOW_KW_NONE) continue;
            // ★ **이름만으로 고르지 않는다.** 두 모듈이 같은 이름의 op 을 가질 수 있고
            //   (`vecgen.open` 은 틀, `growvec.open` 은 아니다), 그때 이름만 보면 남의 틀을
            //   이 부름에 씌운다 — 실측으로 골든 여섯이 그렇게 거짓 거절됐다.
            bool is_tpl = false;
            proven_u8str_view_t qm = nd->kids[z]->qual_mod;
            for (proven_size_t q = 0; q < ntpl; q++) {
                if (!proven_u8str_view_eq(tpl[q].name, nd->kids[z]->tok.lex)) continue;
                if (qm.size) { if (tpl[q].mod.size && proven_u8str_view_eq(qm, tpl[q].mod)) is_tpl = true; }
                else if (nd->file == tpl[q].file) is_tpl = true;   // 맨이름은 제 모듈 안에서만
            }
            if (!is_tpl) continue;
            const low_cst_t *a = nd->kids[z + 1];
            if (a->kind == LOW_CST_ATOM && a->tok.kw == LOW_KW_NONE &&
                ck_is_type_word(pr, a->tok.lex)) continue;                 // 타입 인자가 있다
            emit(out, "E-MONO-NOTYPE",
                 "this op takes a TYPE as its first input (`input comptime t type .`) and the call "
                 "does not give one. Nothing is inferred from the argument types here: what is being "
                 "built has to be visible at the call — write the type first, as in `max_of score a "
                 "b`. (Without it the instance is never built, which is why the tool used to say the "
                 "op did not exist)",
                 nd->kids[z]->tok.line);
        }
    for (proven_size_t i = 0; i < nd->nkids; i++) ck_typearg_walk(out, nd->kids[i], pr, tpl, ntpl);
}
static void ck_typeargs(low_check_result_t *out, const low_parse_result_t *pr) {
    ck_tpl_t tpl[64]; proven_size_t ntpl = 0;
    // ★ 단형화는 **인스턴스가 하나도 없는 틀**을 폼 목록에서 뺀다(`pr->gforms` 에 남긴다) —
    //   타입 인자를 빠뜨린 부름이 정확히 그 경우다. 그래서 두 목록을 함께 훑는다.
    for (int pass = 0; pass < 2 && ntpl < 64; pass++) {
        low_cst_t *const *fs = pass ? pr->gforms : pr->forms;
        proven_size_t nfs = pass ? pr->ngforms : pr->nforms;
        for (proven_size_t i = 0; i < nfs && ntpl < 64; i++) {
            const low_cst_t *f = fs[i];
            if (!(f->kind == LOW_CST_FORM && f->nkids >= 2 && ck_atom(f->kids[0]) && ck_atom(f->kids[1])))
                continue;
            low_kw_t k = f->kids[0]->tok.kw;
            if (k != LOW_KW_FN && k != LOW_KW_PROC) continue;
            if (low_is_generic_template(f)) {
                tpl[ntpl].name = f->kids[1]->tok.lex; tpl[ntpl].file = f->file;
                tpl[ntpl].mod = ck_module_of_file(pr, f->file); ntpl++;
            }
        }
    }
    if (!ntpl) return;
    for (proven_size_t i = 0; i < pr->nforms; i++) {
        const low_cst_t *f = pr->forms[i]; ck_cur_form = f;
        if (!(f->kind == LOW_CST_FORM && f->nkids >= 2 && ck_atom(f->kids[0]))) continue;
        low_kw_t k = f->kids[0]->tok.kw;
        if (k != LOW_KW_FN && k != LOW_KW_PROC) continue;
        if (low_is_generic_template(f)) continue;        // 틀 안에서는 `t` 가 타입 이름이다
        low_op_header_t h = low_op_header(f);
        if (h.body) ck_typearg_walk(out, h.body, pr, tpl, ntpl);
    }
}

static void ck_export_surface(low_check_result_t *out, const low_parse_result_t *pr) {
    for (proven_size_t i = 0; i < pr->nforms; i++) {
        const low_cst_t *f = pr->forms[i]; ck_cur_form = f;
        if (!(f->kind == LOW_CST_FORM && f->nkids >= 2 && ck_atom(f->kids[0]))) continue;
        low_kw_t kw = f->kids[0]->tok.kw;
        if ((kw != LOW_KW_FN && kw != LOW_KW_PROC) || !f->is_export) continue;
        low_op_header_t h = low_op_header(f);
        proven_size_t spans[LOW_HDR_MAXP + 1][2]; proven_size_t ns = 0;
        for (proven_size_t q = 0; q < h.np && ns < LOW_HDR_MAXP; q++) {
            spans[ns][0] = h.p[q].ts; spans[ns][1] = h.p[q].te; ns++;
        }
        if (h.out_s) { spans[ns][0] = h.out_s; spans[ns][1] = h.out_e; ns++; }
        for (proven_size_t z = 0; z < ns; z++)
            for (proven_size_t w = spans[z][0]; w < spans[z][1] && w < f->nkids; w++) {
                if (!ck_atom(f->kids[w]) || f->kids[w]->tok.kw != LOW_KW_NONE) continue;
                bool found = false;
                bool exported = ck_type_is_exported(pr, f->kids[w]->tok.lex, &found, f->file);
                if (!found || exported) continue;
                // ★ 경고다 — 프로그램 자체는 옳고(한 모듈만 있는 자리에서는 아무 문제가 없다),
                //   **다른 모듈이 쓸 수 없다**는 사실을 내보내는 쪽에 알리는 것이 이 자리의 일이다.
                warn(out, "W-EXPORT-HIDDEN",
                     "this op is exported and its signature names a type this module keeps to "
                     "itself. An importing module cannot write that type, so it has nowhere to put "
                     "the value and the export cannot be used from outside (§6.10.1). Export the "
                     "type too, or give the op a signature made of types the other side can name",
                     f->kids[w]->tok.line);
            }
    }
}

static void ck_ffi(low_check_result_t *out, const low_parse_result_t *pr) {
    for (proven_size_t i = 0; i < pr->nforms; i++) {
        const low_cst_t *f = pr->forms[i]; ck_cur_form = f;
        if (f->kind != LOW_CST_FORM || f->nkids < 2 || !ck_atom(f->kids[0])) continue;
        low_kw_t kw = f->kids[0]->tok.kw;
        if (kw != LOW_KW_FN && kw != LOW_KW_PROC) continue;
        if (!f->is_extern) continue;
        proven_u32 ln = f->kids[1]->tok.line;
        low_op_header_t h = low_op_header(f);

        // ★★★ **`export extern` 은 반대 방향이다**: 몸은 **우리 것**이고 **C 가 그것을 부른다**.
        //   그러면 unsafe 도 cap 도 필요 없다 — 우리가 C 를 부르는 게 아니니까.
        //   ★ 대신 **ABI 로 나를 수 있는 타입**이어야 한다(그건 아래에서 함께 본다).
        if (f->is_export) {
            if (!h.body)
                emit(out, "E-FFI-BODY",
                     "an `export extern` op has NO body. This is the OTHER direction — C calls US, "
                     "so the body is OURS. (`extern` alone means the body is in C.)", ln);
            // 타입 사상만 확인하고 넘어간다
            for (proven_size_t q = 0; q < h.np; q++)
                for (proven_size_t z = h.p[q].core; z < h.p[q].te; z++)
                    if (ck_atom(f->kids[z])) {
                        proven_u8str_view_t t = f->kids[z]->tok.lex;
                        if (veq(t, "option") || veq(t, "result") || veq(t, "vec") ||
                            veq(t, "stack") || veq(t, "bitset"))
                            emit(out, "E-FFI-TYPE",
                                 "this signature carries a type the C ABI cannot express — C cannot "
                                 "receive it, so we will not pretend it can", ln);
                    }
            continue;
        }

        // ★ 몸이 둘일 수는 없다.
        if (h.body)
            emit(out, "E-FFI-BODY",
                 "an `extern` op has a body. Its body is IN C — that is what `extern` MEANS. "
                 "Two bodies is not a program, it is a question nobody can answer", ln);

        // ★★★ **C 심볼의 이름은 저자가 적는다 — 도구가 op 이름에서 지어내지 않는다**
        //   (RFC-0063 · 결함 노트 #67, 2026-09-16). `link` 절이 없으면 하강이 op 이름을 C 심볼로
        //   삼았다. 그러면 op 의 이름을 바꾸는 순간 **다른 C 함수를 부르게 되고**, 그 사실이
        //   소스 어디에도 안 적혀 있다. 약속은 적힌 것이어야 한다.
        {
            bool has_link = false;
            for (proven_size_t z = 2; z + 1 < f->nkids; z++)
                if (ck_atom(f->kids[z]) && veq(f->kids[z]->tok.lex, "link")) has_link = true;
            if (!has_link)
                emit(out, "E-FFI-LINK",
                     "an `extern` op does not say which C symbol it calls. Write `link \"strlen\" .` "
                     "(and `link \"sin\" from \"m\" .` when it lives in a library). The C name is a "
                     "promise made to another language, so the author writes it: derived from the op "
                     "name it would change the moment the op is renamed, with nothing in the source "
                     "saying so (RFC-0063)", ln);
        }

        if (!f->is_unsafe)
            emit(out, "E-FFI-NOUNSAFE",
                 "calling C without `unsafe`. Inside that C function every invariant this language "
                 "enforces can be broken, and the tool cannot see it. What cannot be checked must "
                 "at least be MARKED (RFC-0063 D1)", ln);

        // ★★★ RFC-0083 L3 (RFC-0077 §P1-2) — FFI 는 cap 이 **있는지**가 아니라 **종류가 `c` 인지**를
        //   본다. `cap allocator` 가 C 를 인가하면 안 된다(무관한 권위). 격자 판정 `cap ⊑c c`:
        //   `cap` 다음 낱말이 kind 다(`input k cap c .`). c-권위를 정확히 요구한다.
        bool has_cap = false, has_cap_c = false, has_eff = false, bad_ty = false;
        for (proven_size_t q = 0; q < h.np; q++)
            for (proven_size_t z = h.p[q].ts; z < h.p[q].te; z++)
                if (ck_atom(f->kids[z]) && veq(f->kids[z]->tok.lex, "cap")) {
                    has_cap = true;
                    if (z + 1 < h.p[q].te && ck_atom(f->kids[z + 1]) && veq(f->kids[z + 1]->tok.lex, "c"))
                        has_cap_c = true;
                }
        for (proven_size_t j = h.eff_s; j < h.eff_e; j++)
            if (ck_atom(f->kids[j]) && !veq(f->kids[j]->tok.lex, "none")) has_eff = true;

        // ★ 타입 사상은 **좁고 정직하다**. 못 나르는 것은 **안 나른다**.
        for (proven_size_t q = 0; q < h.np; q++)
            for (proven_size_t z = h.p[q].core; z < h.p[q].te; z++)
                if (ck_atom(f->kids[z])) {
                    proven_u8str_view_t t = f->kids[z]->tok.lex;
                    if (veq(t, "option") || veq(t, "result") || veq(t, "vec") ||
                        veq(t, "stack") || veq(t, "bitset")) bad_ty = true;
                }
        for (proven_size_t j = h.out_s; j < h.out_e; j++)
            if (ck_atom(f->kids[j])) {
                proven_u8str_view_t t = f->kids[j]->tok.lex;
                if (veq(t, "option") || veq(t, "result") || veq(t, "vec")) bad_ty = true;
            }

        if (!has_cap)
            emit(out, "E-FFI-NOCAP",
                 "this op calls C but receives NO right to do so. Calling into C is a capability you "
                 "are HANDED — `input k cap c .` — exactly like the heap (RFC-0043), the device bus "
                 "(RFC-0042) and raw machine instructions (RFC-0041). An ambient door into C is one "
                 "every caller silently inherits", ln);
        else if (!has_cap_c)
            emit(out, "E-FFI-CAPKIND",
                 "this op calls C but the capability it receives is NOT `cap c` — an UNRELATED "
                 "capability (allocator, file_system, …) does NOT authorize the C boundary. Authority "
                 "is by KIND, not by mere presence: the FFI right is exactly `input k cap c .` "
                 "(RFC-0083 L3 · RFC-0077 §P1-2). A cap of another kind opening C would be ambient "
                 "authority in disguise", ln);
        if (!has_eff)
            emit(out, "E-FFI-NOEFFECT",
                 "this op calls C and declares no effect. The effect row is how a CALLER learns what "
                 "it is taking on; C that hides in a clean signature is the hidden cost this language "
                 "exists to remove (declare at least `effects unsafe`)", ln);
        if (bad_ty)
            emit(out, "E-FFI-TYPE",
                 "this signature carries a type the C ABI cannot express (option / result / vector / "
                 "container). C has no such thing, and the tool will not PRETEND it does: pass "
                 "integers, f64, or a byte slice (which becomes a POINTER and a LENGTH — two C "
                 "arguments, because that is what a slice honestly IS in C)", ln);
    }
}

// ★★★★ RFC-0112 D6 (WO-0213) — **권한을 든 actor 는 권한이 보이는 자리에서만 태어난다** (`E-CAP-FORGE`).
//   권한 칸(`h cap heap .`)은 실행 중 값이 아니다 — 그 칸이 뜻을 갖는 것은 `spawn` 자리가 같은 종류의 권한을
//   **이미 쥐고 있을** 때뿐이다. 안 그러면 권한 하나 없는 op 이 `spawn actor heap_bytes` 한 줄로 힙을 지어낸다.
static void ck_forge_walk(low_check_result_t *out, const low_cst_t *nd, const low_cst_t *op) {
    if (!nd || nd->kind == LOW_CST_ATOM) return;
    for (proven_size_t j = 0; j + 2 < nd->nkids; j++) {
        if (!ck_atom(nd->kids[j]) || nd->kids[j]->tok.kw != LOW_KW_SPAWN) continue;
        if (!ck_atom(nd->kids[j + 1]) || nd->kids[j + 1]->tok.kw != LOW_KW_ACTOR || !ck_atom(nd->kids[j + 2])) continue;
        const low_cst_t *act = ck_actor_named(nd->kids[j + 2]->tok.lex);
        static const char *K[] = { "allocator", "heap" };
        for (proven_size_t k = 0; k < 2; k++) {
            if (!ck_actor_has_capkind(act, K[k])) continue;
            bool held = false;
            low_op_header_t h = low_op_header(op);
            for (proven_size_t q = 0; q < h.np && !held; q++) {
                proven_size_t cw = h.p[q].core;
                if (cw + 1 < h.p[q].te && cw + 1 < op->nkids && ck_atom(op->kids[cw]) && ck_atom(op->kids[cw + 1]) &&
                    veq(op->kids[cw]->tok.lex, "cap") && veq(op->kids[cw + 1]->tok.lex, K[k])) held = true;
            }
            if (!held) held = ck_actor_has_capkind(ck_enclosing_actor(op), K[k]);
            if (!held)
                emit(out, "E-CAP-FORGE",
                     "this spawns an actor whose state HOLDS a capability (`cap allocator` / `cap heap`), but the "
                     "op spawning it holds no capability of that kind. A capability field means nothing at run "
                     "time — it is only real because the place that creates the actor already had the right. "
                     "Without that, one `spawn` would forge authority out of thin air (RFC-0112 D6). Take the "
                     "capability as an input of this op",
                     nd->kids[j]->tok.line);
        }
    }
    for (proven_size_t i = 0; i < nd->nkids; i++) ck_forge_walk(out, nd->kids[i], op);
}
static bool ck_spawns_actor(const low_cst_t *nd, proven_u8str_view_t name) {
    if (!nd || nd->kind == LOW_CST_ATOM) return false;
    for (proven_size_t j = 0; j + 2 < nd->nkids; j++)
        if (ck_atom(nd->kids[j]) && nd->kids[j]->tok.kw == LOW_KW_SPAWN &&
            ck_atom(nd->kids[j + 1]) && nd->kids[j + 1]->tok.kw == LOW_KW_ACTOR && ck_atom(nd->kids[j + 2]) &&
            proven_u8str_view_eq(ck_bare_name(nd->kids[j + 2]->tok.lex), name)) return true;
    for (proven_size_t i = 0; i < nd->nkids; i++) if (ck_spawns_actor(nd->kids[i], name)) return true;
    return false;
}
static void ck_actor_caps(low_check_result_t *out, const low_parse_result_t *pr) {
    for (proven_size_t i = 0; i < pr->nforms; i++) {
        const low_cst_t *f = pr->forms[i]; ck_cur_form = f;
        if (!f || f->kind != LOW_CST_FORM || f->nkids < 2 || !ck_atom(f->kids[0])) continue;
        low_kw_t kw = f->kids[0]->tok.kw;
        if (kw == LOW_KW_FN || kw == LOW_KW_PROC) { ck_forge_walk(out, f, f); continue; }
        if (kw != LOW_KW_ACTOR || f->nkids < 3) continue;
        const low_cst_t *blk = f->kids[f->nkids - 1];
        if (blk->kind != LOW_CST_BLOCK) continue;
        for (proven_size_t j = 0; j < blk->nkids; j++) {
            const low_cst_t *op = blk->kids[j];
            if (op->kind == LOW_CST_FORM && op->nkids >= 2 && ck_atom(op->kids[0]) &&
                (op->kids[0]->tok.kw == LOW_KW_PROC || op->kids[0]->tok.kw == LOW_KW_FN))
                ck_forge_walk(out, op, op);
        }
        // 베어메탈: 힙 칸을 든 actor 와, 힙을 적은 actor op 은 거절(E-HEAP-NOHOST — 세 자리와 같은 규칙)
        // ★ 단, 이 단위 어디서도 **띄우지 않는** actor 는 묻지 않는다 — 라이브러리가 호스티드용 얼로케이터를
        //   함께 담았다는 이유로 그 라이브러리의 베어메탈 쪽(범프·고정 창)까지 못 쓰게 되면 안 된다.
        bool spawned = false;
        for (proven_size_t q = 0; q < pr->nforms && !spawned; q++)
            spawned = ck_spawns_actor(pr->forms[q], f->kids[1]->tok.lex);
        if (!spawned) continue;
        if (low_ir_target()->no_heap && ck_actor_has_capkind(f, "heap"))
            emit(out, "E-HEAP-NOHOST",
                 "this actor's state holds `cap heap` — the GROWING root — but the build target is FREESTANDING "
                 "(`machine.no_heap`). A bare-metal board cannot hand out more memory at run time; hold "
                 "`cap allocator` (the fixed window) instead (RFC-0112 D2)", f->kids[1]->tok.line);
        for (proven_size_t j = 0; j < blk->nkids; j++) {
            const low_cst_t *op = blk->kids[j];
            if (op->kind != LOW_CST_FORM || op->nkids < 2 || !ck_atom(op->kids[0])) continue;
            if (op->kids[0]->tok.kw != LOW_KW_PROC && op->kids[0]->tok.kw != LOW_KW_FN) continue;
            if (!low_ir_target()->no_heap) continue;
            low_op_header_t h = low_op_header(op);
            for (proven_size_t z = h.eff_s; z < h.eff_e; z++)
                if (ck_atom(op->kids[z]) && veq(op->kids[z]->tok.lex, "heap"))
                    emit(out, "E-HEAP-NOHOST",
                         "this actor op declares the `heap` effect — the GROWING root — but the build target is "
                         "FREESTANDING (`machine.no_heap`). Carve the fixed window with `alloc` instead (RFC-0112 D2)",
                         op->kids[1]->tok.line);
        }
    }
}

// 이 나무에서 입력·`let`·`var` 로 묶인 이름의 타입이 권한 칸을 가진 actor(또는 그 이름을 `#` 뒤에 담은
// 단형화 인스턴스)면 그 칸의 종류를 켠다.
static void ck_mark_actor_kind(proven_u8str_view_t tw, bool *fixed, bool *heap) {
    const low_cst_t *a = ck_actor_named(tw);
    if (!a) {                                     // `vec#u32#allocs.heap_bytes` — 마지막 `#` 뒤
        for (proven_size_t i = tw.size; i-- > 0; )
            if (tw.ptr[i] == (proven_u8)'#') { a = ck_actor_named((proven_u8str_view_t){ .ptr = tw.ptr + i + 1, .size = tw.size - i - 1 }); break; }
    }
    if (!a) return;
    if (ck_actor_has_capkind(a, "allocator")) *fixed = true;
    if (ck_actor_has_capkind(a, "heap")) *heap = true;
}
static void ck_held_actor_caps(const low_cst_t *nd, const low_cst_t *op, bool *fixed, bool *heap) {
    if (!nd || nd->kind == LOW_CST_ATOM) return;
    for (proven_size_t i = 0; i + 2 < nd->nkids; i++) {
        const low_cst_t *k0 = nd->kids[i];
        bool binder = k0->kind == LOW_CST_ATOM &&
                      (veq(k0->tok.lex, "input") || k0->tok.kw == LOW_KW_LET || k0->tok.kw == LOW_KW_VAR);
        if (!binder || nd->kids[i + 1]->kind != LOW_CST_ATOM) continue;
        for (proven_size_t q = i + 2; q < nd->nkids && nd->kids[q]->kind == LOW_CST_ATOM; q++) {
            proven_u8str_view_t w = nd->kids[q]->tok.lex;
            if (veq(w, "mut") || veq(w, "owned") || veq(w, "option")) continue;
            ck_mark_actor_kind(w, fixed, heap);
            break;
        }
    }
    for (proven_size_t i = 0; i < nd->nkids; i++) ck_held_actor_caps(nd->kids[i], op, fixed, heap);
}

// ★★★★ RFC-0112 D11 · F18 (WO-0216) — **태스크와 뿌리.**
//
//   뿌리(고정 창 · 힙)의 커서는 **실행 흐름 사이에 나뉘지 않는다**. 두 태스크가 영역을 열고 번갈아 깎으면 한쪽의
//   `end` 가 다른 쪽의 바이트를 되감는다 — VM 의 그린 스레드에서 실측(`region reset needs its own mark`), 네이티브는
//   진짜 스레드라 경합이다. 잠금으로도 못 막는다: 영역의 되감기는 **차례(LIFO)** 에 기대는데 두 흐름이 섞이면 차례가 없다.
//   ⇒ 규칙 둘:
//     · `E-ALLOC-TASK` — 태스크로 띄우는 op 은 뿌리에서 깎지 않는다(효과에 `alloc`·`heap` 이 없다). 태스크는 빌린 바이트
//       위의 얼로케이터를 받아 쓴다.
//     · `E-ALLOC-SHARED` — 태스크에 얼로케이터 actor 를 인자로 건네려면 그 `reserve` 가 `atomic` 을 적어야 한다(원자적으로
//       커서를 민다). `lock` 은 아직 원시어가 없어 **지켜졌는지 검사할 수 없으므로** 인정하지 않는다 — 원시어가 생기면 넓힌다.
static const low_cst_t *ck_top_op_named(const low_parse_result_t *pr, proven_u8str_view_t name) {
    proven_u8str_view_t b = ck_bare_name(name);
    for (proven_size_t i = 0; i < pr->nforms; i++) {
        const low_cst_t *f = pr->forms[i]; ck_cur_form = f;
        if (f && f->kind == LOW_CST_FORM && f->nkids >= 2 && ck_atom(f->kids[0]) &&
            (f->kids[0]->tok.kw == LOW_KW_PROC || f->kids[0]->tok.kw == LOW_KW_FN) && ck_atom(f->kids[1]) &&
            proven_u8str_view_eq(f->kids[1]->tok.lex, b)) return f;
    }
    return NULL;
}
static bool ck_actor_is_allocator(const low_cst_t *actor) {
    if (!actor) return false;
    const low_cst_t *blk = actor->kids[actor->nkids - 1];
    if (blk->kind != LOW_CST_BLOCK) return false;
    for (proven_size_t j = 0; j < blk->nkids; j++) {
        const low_cst_t *s = blk->kids[j];
        if (s->kind == LOW_CST_FORM && s->nkids >= 2 && ck_atom(s->kids[0]) && veq(s->kids[0]->tok.lex, "satisfies"))
            for (proven_size_t z = 1; z < s->nkids; z++) {
                if (!ck_atom(s->kids[z])) continue;
                proven_u8str_view_t t = ck_bare_name(s->kids[z]->tok.lex);
                if (veq(t, "byte_allocator") || veq(t, "freeing_allocator")) return true;
            }
    }
    return false;
}
static void ck_task_walk(low_check_result_t *out, const low_parse_result_t *pr, const low_cst_t *nd, const low_cst_t *op) {
    if (!nd || nd->kind == LOW_CST_ATOM) return;
    for (proven_size_t j = 0; j + 1 < nd->nkids; j++) {
        if (!ck_atom(nd->kids[j]) || nd->kids[j]->tok.kw != LOW_KW_SPAWN) continue;
        const low_cst_t *pn = nd->kids[j + 1];
        if (!ck_atom(pn) || pn->tok.kw == LOW_KW_ACTOR) continue;
        const low_cst_t *callee = ck_top_op_named(pr, pn->tok.lex);
        if (callee) {
            bool fd = false;
            if (decl_effect(callee, &fd) & (EFF_ALLOC | EFF_HEAP))
                emit(out, "E-ALLOC-TASK",
                     "this spawns a TASK whose op takes memory from a ROOT (its effects include `alloc` or `heap`). A root "
                     "has one cursor, and regions rewind it in order — two tasks interleaving would rewind each other's "
                     "bytes (measured on the VM's green threads: `region reset needs its own mark`; native tasks are real "
                     "threads). Give the task an allocator over borrowed bytes instead (RFC-0112 D11)",
                     nd->kids[j]->tok.line);
        }
    }
    for (proven_size_t i = 0; i < nd->nkids; i++) ck_task_walk(out, pr, nd->kids[i], op);
}
// ★ 한 이름이 이 나무 어딘가에 몇 번 나오나 — 「그 얼로케이터를 다른 데서도 쓰는가」를 잰다.
static proven_size_t ck_name_uses(const low_cst_t *nd, proven_u8str_view_t nm) {
    if (!nd) return 0;
    if (ck_atom(nd)) return (nd->tok.kind == LOW_TOK_IDENT &&
                             proven_u8str_view_eq(nd->tok.lex, nm)) ? 1 : 0;
    proven_size_t n = 0;
    for (proven_size_t i = 0; i < nd->nkids; i++) n += ck_name_uses(nd->kids[i], nm);
    return n;
}
// ★★★★ **나눠 쓰는 것이 문제다 — 건네는 것 자체가 아니다** (결함 노트 #75, 2026-09-16).
//
//   `E-ALLOC-SHARED` 는 「태스크마다 제 얼로케이터를 줘라」고 권하면서, **따로 띄운** 얼로케이터
//   둘을 태스크 둘에 하나씩 건네도 같은 진단으로 거절했다 — 얼로케이터를 태스크에 넘기는 것
//   자체를 막았기 때문이다. 진단이 권하는 해법이 통하지 않으면 그 진단은 길을 막을 뿐이다.
//   ⇒ 재는 것을 **경합**으로 바꾼다: 한 블록 안에서 같은 얼로케이터 이름이
//     ⓐ 태스크 **둘 이상**에 건네지거나 ⓑ 태스크에 건네진 채 그 블록에서 **또 쓰이면** 문다.
//     이름이 하나씩 짝지어 나뉘면 커서가 하나씩이므로 경합이 없다.
static void ck_alloc_shared_blk(low_check_result_t *out, const low_parse_result_t *pr,
                                const low_cst_t *blk, const low_cst_t *op) {
    if (!blk || ck_atom(blk)) return;
    proven_u8str_view_t nm[16]; proven_size_t cnt[16], nn = 0; proven_i64 ln[16];
    for (proven_size_t j = 0; j < blk->nkids; j++) {
        const low_cst_t *sf = blk->kids[j];
        if (!sf || sf->kind != LOW_CST_FORM || sf->nkids < 3 || !ck_atom(sf->kids[0])) continue;
        if (sf->kids[0]->tok.kw != LOW_KW_SPAWN) continue;
        if (!ck_atom(sf->kids[1]) || sf->kids[1]->tok.kw == LOW_KW_ACTOR) continue;
        for (proven_size_t q = 2; q < sf->nkids; q++) {
            if (!ck_atom(sf->kids[q]) || sf->kids[q]->tok.kind != LOW_TOK_IDENT) continue;
            proven_u8str_view_t a = sf->kids[q]->tok.lex;
            proven_u8str_view_t tw = ck_name_type_word(op, a);
            const low_cst_t *act = tw.size ? ck_actor_named(tw) : NULL;
            if (!ck_actor_is_allocator(act)) continue;
            const low_cst_t *rs = ck_actor_proc(act, proven_u8str_view_from_cstr("reserve"));
            bool fd = false;
            if (rs && (decl_effect(rs, &fd) & EFF_ATOMIC)) continue;   // 원자적이면 나눠 써도 된다
            proven_size_t s = 0;
            while (s < nn && !proven_u8str_view_eq(nm[s], a)) s++;
            if (s == nn) { if (nn >= 16) continue; nm[nn] = a; cnt[nn] = 0; ln[nn] = sf->kids[0]->tok.line; nn++; }
            cnt[s]++;
        }
    }
    for (proven_size_t s = 0; s < nn; s++) {
        proven_size_t uses = ck_name_uses(blk, nm[s]);
        if (cnt[s] < 2 && uses <= cnt[s]) continue;     // 태스크 하나에만, 다른 데서 안 쓴다 — 경합 없음
        emit(out, "E-ALLOC-SHARED",
             "the same ALLOCATOR is in play in two places at once here, and its `reserve` does not move "
             "its cursor atomically (no `atomic` effect): it is handed to more than one task, or handed "
             "to a task and still used beside it. Two of them reserving race on one cursor. Give each "
             "task its OWN allocator over its own bytes — two `spawn actor` instances, each `init`ed on "
             "a disjoint slice — or use one whose `reserve` is atomic (RFC-0112 D11)",
             ln[s]);
    }
    for (proven_size_t i = 0; i < blk->nkids; i++) ck_alloc_shared_blk(out, pr, blk->kids[i], op);
}
// ★★★★ **갓 띄운 액터의 상태 칸은 아직 아무것도 아니다** (결함 노트 #53, 2026-09-16).
//
//   `spawn actor allocs.bump_bytes` 는 상태 칸을 **0 으로** 채운다. 그런데 `mem mut slice u8 .`
//   같은 칸에 0 은 슬라이스가 아니다 — `init` 없이 `reserve` 를 보내면 **VM 은 멈추고
//   (`E-VM-TYPE: len needs a slice`) 네이티브는 `none` 을 냈다.** 두 뒤끝이 갈렸고, `--check` 는
//   둘 다 통과시켰다. 어느 쪽이 옳은지를 정하기 전에, **그 자리에 닿지 못하게** 한다.
//   ⇒ 한 블록 안에서 `spawn actor` 로 묶은 이름에 **처음 보내는 말**은, 그 액터의 슬라이스 칸을
//     **읽기만 하는** 핸들러여서는 안 된다. 곧은 문장 차례만 본다(그래서 놓칠지언정 헛맞지 않는다).
static bool ck_body_writes_field(const low_cst_t *nd, proven_u8str_view_t fld) {
    if (!nd || ck_atom(nd)) return false;
    if (nd->kind == LOW_CST_FORM && nd->nkids >= 2 && ck_atom(nd->kids[0]) &&
        nd->kids[0]->tok.kw == LOW_KW_SET) {
        const low_cst_t *t = nd->kids[1];
        while (t && (t->kind == LOW_CST_GROUP || t->kind == LOW_CST_FORM) && t->nkids == 1) t = t->kids[0];
        if (t && ck_atom(t) && proven_u8str_view_eq(t->tok.lex, fld)) return true;
        if (t && t->kind == LOW_CST_FORM && t->nkids >= 2 && ck_atom(t->kids[1]) &&
            proven_u8str_view_eq(t->kids[1]->tok.lex, fld)) return true;
    }
    for (proven_size_t i = 0; i < nd->nkids; i++)
        if (ck_body_writes_field(nd->kids[i], fld)) return true;
    return false;
}
static void ck_actor_uninit_blk(low_check_result_t *out, const low_cst_t *blk) {
    if (!blk || ck_atom(blk)) return;
    proven_u8str_view_t lname[8], lact[8]; proven_size_t nl = 0; bool spoke[8] = { 0 };
    for (proven_size_t j = 0; j < blk->nkids; j++) {
        const low_cst_t *st = blk->kids[j];
        if (!st || ck_atom(st)) continue;
        // ① `var|let <이름> <타입>… be (spawn actor <A>)` — 묶은 이름과 액터를 적어 둔다.
        if (st->kind == LOW_CST_FORM && st->nkids >= 4 && ck_atom(st->kids[0]) &&
            (st->kids[0]->tok.kw == LOW_KW_VAR || st->kids[0]->tok.kw == LOW_KW_LET) &&
            ck_atom(st->kids[1])) {
            const low_cst_t *rhs = st->kids[st->nkids - 1];
            while (rhs && rhs->kind == LOW_CST_GROUP && rhs->nkids == 1) rhs = rhs->kids[0];
            if (rhs && rhs->kind == LOW_CST_FORM && rhs->nkids >= 3 && ck_atom(rhs->kids[0]) &&
                rhs->kids[0]->tok.kw == LOW_KW_SPAWN && ck_atom(rhs->kids[1]) &&
                rhs->kids[1]->tok.kw == LOW_KW_ACTOR && ck_atom(rhs->kids[2]) && nl < 8) {
                lname[nl] = st->kids[1]->tok.lex; lact[nl] = rhs->kids[2]->tok.lex; nl++;
                continue;
            }
        }
        // ② 이 문장 안의 첫 `send <이름> <op>` — 그 이름이 위에서 띄운 것이면 본다.
        const low_cst_t *sd = st;
        for (proven_size_t d = 0; d < 4 && sd; d++) {
            if (sd->kind == LOW_CST_FORM && sd->nkids >= 3 && ck_atom(sd->kids[0]) &&
                sd->kids[0]->tok.kw == LOW_KW_SEND) break;
            sd = (sd->nkids) ? sd->kids[sd->nkids - 1] : NULL;
        }
        if (!sd || sd->kind != LOW_CST_FORM || sd->nkids < 3 || !ck_atom(sd->kids[0]) ||
            sd->kids[0]->tok.kw != LOW_KW_SEND || !ck_atom(sd->kids[1]) || !ck_atom(sd->kids[2])) continue;
        proven_size_t s = 0;
        while (s < nl && !proven_u8str_view_eq(lname[s], sd->kids[1]->tok.lex)) s++;
        if (s == nl || spoke[s]) continue;
        spoke[s] = true;                                   // 이 이름에 **처음** 보내는 말이다
        const low_cst_t *act = ck_actor_named(lact[s]);
        if (!act) continue;
        const low_cst_t *h = ck_actor_proc(act, sd->kids[2]->tok.lex);
        if (!h) continue;
        const low_cst_t *hb = h->kids[h->nkids - 1];
        if (!hb || hb->kind != LOW_CST_BLOCK) continue;
        // 슬라이스 칸을 찾아, 이 핸들러가 그것을 **쓰지 않으면서 읽으면** 문다.
        const low_cst_t *ab = act->kids[act->nkids - 1];
        if (!ab || ab->kind != LOW_CST_BLOCK) continue;
        for (proven_size_t i = 0; i < ab->nkids; i++) {
            const low_cst_t *sf = ab->kids[i];
            if (sf->kind != LOW_CST_FORM || !sf->nkids || !ck_atom(sf->kids[0]) ||
                sf->kids[0]->tok.kw != LOW_KW_STATE) continue;
            const low_cst_t *sb = sf->kids[sf->nkids - 1];
            if (!sb || sb->kind != LOW_CST_BLOCK) continue;
            for (proven_size_t q = 0; q < sb->nkids; q++) {
                const low_cst_t *fl = sb->kids[q];
                if (fl->kind != LOW_CST_FORM || fl->nkids < 2 || !ck_atom(fl->kids[0])) continue;
                bool is_slice = false;
                for (proven_size_t w = 1; w < fl->nkids; w++)
                    if (ck_atom(fl->kids[w]) && (veq(fl->kids[w]->tok.lex, "slice") ||
                                                 veq(fl->kids[w]->tok.lex, "ref"))) is_slice = true;
                if (!is_slice) continue;
                proven_u8str_view_t fname = fl->kids[0]->tok.lex;
                if (ck_body_writes_field(hb, fname)) continue;          // 이 말이 그 칸을 세운다
                if (!ck_name_uses(hb, fname)) continue;                 // 읽지도 않는다
                emit(out, "E-ACTOR-UNINIT",
                     "this is the FIRST message sent to an actor that was just spawned, and the handler "
                     "READS a state field that holds a slice without setting it. A fresh actor's state "
                     "is all zeroes, and zero is not a slice: the VM stops (`len needs a slice`) while "
                     "native quietly answers `none` — the two back ends disagree, which means one of "
                     "them is lying. Send the message that sets it up first (the one whose handler "
                     "`set`s that field, typically `init`)",
                     sd->kids[0]->tok.line);
                break;
            }
        }
    }
    for (proven_size_t i = 0; i < blk->nkids; i++) ck_actor_uninit_blk(out, blk->kids[i]);
}
static void ck_task_alloc(low_check_result_t *out, const low_parse_result_t *pr) {
    for (proven_size_t i = 0; i < pr->nforms; i++) {
        const low_cst_t *f = pr->forms[i]; ck_cur_form = f;
        if (!f || f->kind != LOW_CST_FORM || f->nkids < 2 || !ck_atom(f->kids[0])) continue;
        if (f->kids[0]->tok.kw == LOW_KW_PROC || f->kids[0]->tok.kw == LOW_KW_FN) {
            ck_task_walk(out, pr, f, f); ck_alloc_shared_blk(out, pr, f, f);
            ck_actor_uninit_blk(out, f); continue; }
        if (f->kids[0]->tok.kw != LOW_KW_ACTOR || f->nkids < 3) continue;
        const low_cst_t *blk = f->kids[f->nkids - 1];
        if (blk->kind != LOW_CST_BLOCK) continue;
        for (proven_size_t j = 0; j < blk->nkids; j++) {
            const low_cst_t *o = blk->kids[j];
            if (o->kind == LOW_CST_FORM && o->nkids >= 2 && ck_atom(o->kids[0]) &&
                (o->kids[0]->tok.kw == LOW_KW_PROC || o->kids[0]->tok.kw == LOW_KW_FN)) {
                ck_task_walk(out, pr, o, o); ck_alloc_shared_blk(out, pr, o, o);
                ck_actor_uninit_blk(out, o); }
        }
    }
}

// ★★★★ **머리 절의 차례** (WO-0217 · 소유자 결정 ⓑ). 차례를 어긴 절 하나를 이름으로 말한다.
static const char *ck_rank_name(int r) {
    switch (r) {
        case 0: return "`satisfies`/`lowdoc`"; case 1: return "`vector`/`priority`"; case 2: return "a `comptime` input";
        case 3: return "a capability/region input"; case 4: return "`using`"; case 5: return "a data input";
        case 6: return "`output`"; case 7: return "`effects`"; case 8: return "`link`/`variadic`"; case 9: return "`asm`";
        case 10: return "`access`/`inplace`/`invalidates`/`parallel`/`reduce`"; case 11: return "`requires`"; case 12: return "`ensures`";
        case 13: return "`errors`"; case 14: return "`tests`"; case 15: return "`schedule`"; default: return "a clause";
    }
}
static void ck_clause_order_one(low_check_result_t *out, const low_cst_t *f, proven_size_t from) {
    if (!f || f->kind != LOW_CST_FORM || f->nkids < 2 || !ck_atom(f->kids[0])) return;
    if (f->kids[0]->tok.kw != LOW_KW_FN && f->kids[0]->tok.kw != LOW_KW_PROC) return;
    int maxr = -1;
    for (proven_size_t i = from; i < f->nkids; i++) {
        if (f->kids[i]->kind != LOW_CST_ATOM || !low_is_clause_word(f->kids[i]->tok.lex)) continue;
        proven_size_t e = i + 1;
        while (e < f->nkids && f->kids[e]->kind == LOW_CST_ATOM && !low_is_clause_word(f->kids[e]->tok.lex)) e++;
        int r = veq(f->kids[i]->tok.lex, "input") ? low_input_rank(f, i, e) : low_clause_rank(f->kids[i]->tok.lex);
        if (r < 0) continue;
        if (r < maxr) {
            // ★ 진단은 문구를 **가리킨다**(복사하지 않는다) — 버퍼 하나를 돌려쓰면 앞 op 의 문구가 덮인다(실측:
            //   두 머리가 같은 «`output` comes after `requires`» 를 말했다). 짝마다 제 칸을 둔다.
            static char msgs[17][17][520];
            char *buf = msgs[r][maxr];
            snprintf(buf, sizeof msgs[0][0],
                     "%s comes after %s. An op header has ONE order: `satisfies`/`lowdoc` · `vector`/`priority` · `comptime` inputs · "
                     "capability/region inputs · `using` · data inputs · `output` · `effects` · `link`/`variadic` · `asm` · "
                     "`access`/`inplace`/`invalidates`/`parallel`/`reduce` · `requires` · `ensures` · `errors` · `tests` (`--fmt` moves the non-input "
                     "clauses for you; inputs are call positions, so reorder those and their call sites yourself)",
                     ck_rank_name(r), ck_rank_name(maxr));
            emit_at(out, "E-CLAUSE-ORDER", buf, f->kids[i]);
            return;
        }
        if (r > maxr) maxr = r;
        i = e - 1;
    }
}
static void ck_clause_order(low_check_result_t *out, const low_parse_result_t *pr) {
    for (proven_size_t i = 0; i < pr->nforms; i++) {
        const low_cst_t *f = pr->forms[i]; ck_cur_form = f;
        if (!f || f->kind != LOW_CST_FORM || f->nkids < 2 || !ck_atom(f->kids[0])) continue;
        if ((f->kids[0]->tok.kw == LOW_KW_ACTOR || f->kids[0]->tok.kw == LOW_KW_TRAIT) && f->nkids >= 3 &&
            f->kids[f->nkids - 1]->kind == LOW_CST_BLOCK) {
            // actor 안의 proc 과 **trait 의 메서드 서명**(`reserve input s self . … effects state via self .`)도 같은 차례
            const low_cst_t *blk = f->kids[f->nkids - 1];
            if (f->kids[0]->tok.kw != LOW_KW_TRAIT) {
                for (proven_size_t j = 0; j < blk->nkids; j++) ck_clause_order_one(out, blk->kids[j], 2);
                continue;
            }
            // trait 의 서명은 **점마다 form 하나**다: `get input s self .` 가 이름과 첫 절, 뒤따르는 `effects state .`·
            //   `output u64 .` 는 절 낱말로 시작하는 form 이다. 이름으로 시작하는 form 이 새 서명을 연다.
            int maxr = -1;
            bool told = false;                       // 한 서명에 한 번만(WO-0219 — 전엔 99 를 넣어 뒤 절마다 다시 울렸다)
            for (proven_size_t j = 0; j < blk->nkids; j++) {
                const low_cst_t *m = blk->kids[j];
                if (!m || m->kind != LOW_CST_FORM || m->nkids < 1 || !ck_atom(m->kids[0])) continue;
                proven_size_t at = 0;
                if (!low_is_clause_word(m->kids[0]->tok.lex)) {
                    maxr = -1;
                    told = false;
                    if (m->nkids < 2 || !ck_atom(m->kids[1]) || !low_is_clause_word(m->kids[1]->tok.lex)) continue;
                    at = 1;
                }
                if (told) continue;
                int r = veq(m->kids[at]->tok.lex, "input") ? low_input_rank(m, at, m->nkids) : low_clause_rank(m->kids[at]->tok.lex);
                if (r < 0) continue;
                if (r < maxr) {
                    static char tmsgs[17][17][520];
                    char *buf = tmsgs[r][maxr];
                    snprintf(buf, sizeof tmsgs[0][0],
                             "%s comes after %s in a trait method signature. It has the same ONE order as an op header "
                             "(`--fmt` moves the non-input clauses for you; inputs pair with the implementation's inputs, so reorder those yourself)",
                             ck_rank_name(r), ck_rank_name(maxr));
                    emit_at(out, "E-CLAUSE-ORDER", buf, m->kids[at]);
                    told = true;
                    continue;
                }
                if (r > maxr) maxr = r;
            }
        } else ck_clause_order_one(out, f, 2);
    }
}

static void ck_alloc_cap(low_check_result_t *out, const low_parse_result_t *pr) {
    ck_clause_order(out, pr);
    ck_actor_caps(out, pr);
    ck_task_alloc(out, pr);
    for (proven_size_t i = 0; i < pr->nforms; i++) {
        const low_cst_t *f = pr->forms[i]; ck_cur_form = f;
        if (f->kind != LOW_CST_FORM || f->nkids < 2 || !ck_atom(f->kids[0])) continue;
        low_kw_t kw = f->kids[0]->tok.kw;
        if (kw != LOW_KW_FN && kw != LOW_KW_PROC) continue;
        low_op_header_t h = low_op_header(f);
        // ★★★ **OS 가 없는 기계엔 OS 능력이 없다** (RFC-0038 · RFC-0030 D4 — 2026-08-01).
        //
        //   `E-HEAP-NOHOST`(자라는 뿌리)·`E-FLOAT-NOFLOAT`(부동소수)의 **형제**다. 힙과 FPU 는 막으면서
        //   **파일·소켓·터미널·인자·환경은 안 막고 있었다** — 그런데 프리스탠딩 프로파일에는
        //   그것들을 줄 OS 자체가 없다. RFC-0030 D4 가 이미 그렇게 적어 두었다:
        //   *"프리스탠딩(embedded): OS 없음 → args/env/exit 없음. 플랫폼 cap(mmio/device)만."*
        //
        //   ☞ 실측(2026-08-01, 빌드 서버): `tests/prog/greet.low`(cap io 로 인사말을 찍는다)가
        //     `--target cortex_m --check` 에서 **초록**이었고, 방출된 C 는 `printf` 를 부르는데
        //     프리스탠딩 헤더엔 `<stdio.h>` 가 없다 ⇒ 실기 툴체인에서 **컴파일 불가**.
        //     즉 검사기가 통과시킨 프로그램을 백엔드가 지을 수 없었다. **둘 중 하나는 거짓말이다.**
        //   ⇒ 남기는 것: `cap machine`·`cap device`(하드웨어는 거기 있다) · `cap c`(C 호출은
        //     베어메탈에서도 뜻이 있다) · `cap allocator`(alloc 효과는 위에서 이미 막힌다).
        if (low_ir_target()->no_heap) {
            static const char *hostcaps[] = { "io", "file_system", "net", "tty", "args", "env", "clock", "random" };
            for (proven_size_t q = 0; q < h.np; q++)
                for (proven_size_t z = h.p[q].ts; z + 1 < h.p[q].te; z++) {
                    if (!ck_atom(f->kids[z]) || !veq(f->kids[z]->tok.lex, "cap")) continue;
                    if (!ck_atom(f->kids[z + 1])) continue;
                    for (proven_size_t c = 0; c < sizeof hostcaps / sizeof hostcaps[0]; c++)
                        if (veq(f->kids[z + 1]->tok.lex, hostcaps[c]))
                            emit(out, "E-CAP-NOHOST",
                                 "this op asks for an OPERATING SYSTEM capability, but the build target "
                                 "is FREESTANDING (`machine.no_heap` — a bare-metal profile like "
                                 "cortex_m). There is no filesystem, no socket, no terminal, no argv "
                                 "and no environment there: the capability is not merely unavailable, "
                                 "it does not exist. Sibling of E-HEAP-NOHOST and E-FLOAT-NOFLOAT. "
                                 "On bare metal you get the hardware instead — `cap device` (MMIO), "
                                 "`cap machine` (instructions), `cap c` (call into C). RFC-0038 · RFC-0030 D4",
                                 f->kids[1]->tok.line);
                }
        }
        bool alloc = false, heap = false;
        for (proven_size_t j = h.eff_s; j < h.eff_e; j++) {
            if (ck_atom(f->kids[j]) && veq(f->kids[j]->tok.lex, "alloc")) alloc = true;
            if (ck_atom(f->kids[j]) && veq(f->kids[j]->tok.lex, "heap"))  heap = true;
        }
        // 권한 입력의 종류와 영역 블록의 종류를 센다 — 둘 다 두 뿌리 가운데 하나를 연다.
        bool cap_fixed = false, cap_heap = false, rparam = false;
        for (proven_size_t q = 0; q < h.np; q++)
            for (proven_size_t z = h.p[q].ts; z < h.p[q].te; z++) {
                if (!ck_atom(f->kids[z])) continue;
                if (veq(f->kids[z]->tok.lex, "region")) rparam = true;
                if (veq(f->kids[z]->tok.lex, "cap") && z + 1 < h.p[q].te && ck_atom(f->kids[z + 1])) {
                    if (veq(f->kids[z + 1]->tok.lex, "allocator")) cap_fixed = true;
                    if (veq(f->kids[z + 1]->tok.lex, "heap"))      cap_heap = true;
                }
            }
        bool blk_heap = ck_has_region_kind(f, true), blk_fixed = ck_has_region_kind(f, false);
        // ★★★★ RFC-0112 D6 (WO-0213) — **권한을 든 값도 권한이다.** 권한 칸을 가진 actor(또는 그것을 담은
        //   단형화 인스턴스 `vec#u32#allocs.heap_bytes`)를 입력·지역으로 쥔 op 은, 그 actor 를 띄운 자리가 이미
        //   권한을 쥐고 있었으므로(`E-CAP-FORGE`) 같은 종류의 권한을 받은 것과 같다. `via` 를 적은 제네릭 op 도
        //   권한을 타입 인자로부터 받는다.
        {
            bool via = false;
            for (proven_size_t j = h.eff_s; j < h.eff_e; j++)
                if (ck_atom(f->kids[j]) && veq(f->kids[j]->tok.lex, "via")) via = true;
            if (via) { cap_fixed = true; cap_heap = true; }
            if (!cap_heap || !cap_fixed) ck_held_actor_caps(f, f, &cap_fixed, &cap_heap);
        }
        // ★★★★ **자라는 뿌리는 호스티드에만 있다** (RFC-0112 D2(3) · WO-0211 — `E-ALLOC-NOHEAP` 을 대신한다).
        //   전엔 no_heap 타깃에서 `alloc` **전체**를 막았다(F3) — 고정 창에서 깎는 일까지. 그런데
        //   베어메탈이 못 하는 것은 *자라는 것* 하나다. 그래서 막는 것도 그 하나다: `heap` 효과 ·
        //   `cap heap` 입력 · `region <이름> heap` 블록. 셋 중 어느 자리로 들어와도 같은 진단이다.
        if (low_ir_target()->no_heap && (heap || cap_heap || blk_heap)) {
            emit(out, "E-HEAP-NOHOST",
                 "this op asks for the GROWING root — the `heap` effect, a `cap heap` input or a "
                 "`region <name> heap` block — but the build target is FREESTANDING (`machine.no_heap`, "
                 "e.g. cortex_m). A bare-metal board has no allocator that can hand out more memory at "
                 "run time. What it DOES have is the fixed window its linker script reserves: carve from "
                 "that with `cap allocator` / `effects alloc` or any other region kind (RFC-0112 D2 · "
                 "RFC-0038 — the gate is on what the MACHINE cannot do, and only that)",
                 f->kids[1]->tok.line);
            continue;
        }
        if (heap && !cap_heap && !blk_heap)
            emit(out, "E-HEAP-NOCAP",
                 "this op declares the `heap` effect but receives NO right to grow the root. There is "
                 "no ambient heap in this language: to take memory that can grow you must be HANDED it "
                 "— `input h cap heap .` — or open a `region <name> heap do … end` block (RFC-0112 D2). "
                 "`cap allocator` is the FIXED window and does not authorize growth",
                 f->kids[1]->tok.line);
        if (!alloc) continue;
        bool has_cap = cap_fixed || rparam || blk_fixed;
        // ★★★ **어휘 `region` 블록도 할당 권한이다**(SPEC-004 §4.5 도입형태 ①) — 고정 창의 블록이면.
        //   ★ 본문 **어디든** 있으면 된다 — 루프 안에 있을 수도 있다(첫 판이 최상위만 봐서 놓쳤다).
        if (!has_cap)
            emit(out, "E-ALLOC-NOCAP",
                 "this op declares the `alloc` effect but receives NO allocation capability. "
                 "There is no ambient heap in this language (RFC-0043 D1): to allocate you must be "
                 "HANDED the right — `input a cap allocator .` or `input r region <name> . .` "
                 "(a region IS an arena allocator, RFC-0043 D7). An allocation nobody granted is "
                 "exactly the hidden dependency this model removes",
                 f->kids[1]->tok.line);
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// ★★★ **MMIO 도 권한이다** (RFC-0042 D2/D7) — 할당과 **같은 규율**
//
//   디바이스 레지스터를 만지려면 **`cap mmio` 를 건네받아야** 하고, **`device` 효과**를
//   선언해야 한다. 그렇지 않으면 그것은 **ambient 하드웨어 접근**이다 — 요청하지 않았고,
//   제어할 수 없고, 숨은 의존이다. **allocator 와 정확히 같은 문제**다(RFC-0043 D1).
//
//   ★ 그리고 `raw`/`addr` 로 만지면 **`unsafe`** 다(D7). 그 규율은 이미 강제된다.
// ★★★ **effect ↔ capability LINK — io 편** (RFC-0007 §6.7). cap-effect = { io, device }.
//   device 는 ck_mmio(E-MMIO-NOCAP)가 잡는다. io 도 같은 불변식: `effects io` 를 선언하면
//   **io 를 인가하는 capability 값**을 시그니처에 하나라도 들고 있어야 한다(`input … cap … .`).
//   없으면 E-EFFECT-NO-CAP. (LINK 는 §6.7 대로 **소유 불변식**만 — 어느 cap 이 io 를 인가하는지의
//   타입 매핑은 RFC-0011. 그래서 여기선 `cap` 토큰 보유만 본다; region 은 alloc 권한이라 제외.)
// ★★★★ **`atomic` 도 권한 효과다** (2026-09-15, 소유자 결정 — *"cap atomic 요구하게 하고"*). 권한 표에는 `cap atomic` 이
//   있었는데 `effects atomic` 은 그것 없이 통과했다 — 짝의 한쪽이 비어 있었다. io·alloc·heap 과 같은 규칙:
//   효과 줄에 `atomic` 을 적으면 `input … cap atomic .` 을 받아야 한다. `via` 로 효과를 타입 인자에게서 받는 제네릭 op 은
//   권한도 그쪽에서 온다(alloc 과 같은 처방).
static void ck_atomic_cap(low_check_result_t *out, const low_parse_result_t *pr) {
    for (proven_size_t i = 0; i < pr->nforms; i++) {
        const low_cst_t *f = pr->forms[i]; ck_cur_form = f;
        if (f->kind != LOW_CST_FORM || f->nkids < 2 || !ck_atom(f->kids[0])) continue;
        low_kw_t kw = f->kids[0]->tok.kw;
        if (kw != LOW_KW_FN && kw != LOW_KW_PROC) continue;
        low_op_header_t h = low_op_header(f);
        bool atomic = false, via = false;
        for (proven_size_t j = h.eff_s; j < h.eff_e; j++) {
            if (ck_atom(f->kids[j]) && veq(f->kids[j]->tok.lex, "atomic")) atomic = true;
            if (ck_atom(f->kids[j]) && veq(f->kids[j]->tok.lex, "via")) via = true;
        }
        if (!atomic || via) continue;
        bool has_cap = false;
        for (proven_size_t q = 0; q < h.np && !has_cap; q++)
            for (proven_size_t z = h.p[q].ts; z + 1 < h.p[q].te; z++)
                if (ck_atom(f->kids[z]) && veq(f->kids[z]->tok.lex, "cap") &&
                    ck_atom(f->kids[z + 1]) && veq(f->kids[z + 1]->tok.lex, "atomic")) has_cap = true;
        if (!has_cap)
            emit(out, "E-ATOMIC-NOCAP",
                 "this op declares the `atomic` effect but receives NO `cap atomic`. Like `io`, `alloc` and `heap`, "
                 "`atomic` is an effect you are HANDED the right to: `input k cap atomic .` (§7.2 (6))",
                 f->kids[1]->tok.line);
    }
}
static void ck_io_cap(low_check_result_t *out, const low_parse_result_t *pr) {
    for (proven_size_t i = 0; i < pr->nforms; i++) {
        const low_cst_t *f = pr->forms[i]; ck_cur_form = f;
        if (f->kind != LOW_CST_FORM || f->nkids < 2 || !ck_atom(f->kids[0])) continue;
        low_kw_t kw = f->kids[0]->tok.kw;
        if (kw != LOW_KW_FN && kw != LOW_KW_PROC) continue;
        low_op_header_t h = low_op_header(f);
        bool io = false;
        for (proven_size_t j = h.eff_s; j < h.eff_e; j++)
            if (ck_atom(f->kids[j]) && veq(f->kids[j]->tok.lex, "io")) io = true;
        if (!io) continue;
        bool has_cap = false;
        // ★★★ **`region` 블록은 여기서 세지 않는다** (2026-07-20 — 구멍을 막는다).
        //
        //   ☞ 여기 `ck_alloc_cap` 에서 복사해 온 블록이 있었다: *"어휘 region 블록도 할당
        //     권한이다"* — **할당 검사에서는 맞는 말이고, io 검사에서는 틀린 말이다.**
        //     그 결과 `region r arena do … end` 한 줄이 `effects io` 를 **능력 없이 통과**시켰다:
        //
        //         proc no_cap_io … effects io . do  region r arena do … end  return 1 . end
        //         → check: ok          (region 을 빼면 E-EFFECT-NO-CAP)
        //
        //   ★★★ **할당할 권리가 입출력을 인가했다.** 그리고 **바로 위 이 함수의 머리 주석이
        //     "region 은 alloc 권한이라 제외" 라고 적어 두고 있었다** — 주석과 코드가 한 줄
        //     사이에서 어긋나 있었고, 아무 픽스처도 `region` 과 `effects io` 를 같이 쓰지
        //     않아서 여태 안 보였다(교훈 6).
        //   ⇒ 복사해 온 검사는 **원래 자리의 전제까지 함께 오지 않는다.** 지운다.
        // ★★★ **어느 cap 인지도 본다** (2026-07-20 — SPEC-010 §31 이 규범으로 요구한다:
        //   *"effect 클래스 X 를 행사하려면 `cap X` 를 보유해야 한다"*).
        //
        //   ☞ 전엔 **`cap` 토큰이 하나라도 있으면** 통과였다. 그래서 이런 것이 초록이었다:
        //         input k cap args .  … effects io .        → check: ok
        //         input k cap teleporter . … effects io .    → check: ok
        //     ★★★ **`cap teleporter` 는 이 저장소가 "이미 거절한다" 고 적어 둔 바로 그 예시다**
        //       — 진입점(E-ENTRY-CAP)에서만 거절했고, 그 밖에서는 io 를 인가하고 있었다.
        //     ⇒ 능력 규율의 값은 *"권한을 가진 자만 한다"* 인데, **아무 권한이나** 되면
        //       그 값이 통째로 사라진다. 그런데 시그니처는 여전히 검사받은 것처럼 읽힌다.
        //
        //   ★ **io 를 인가하는 cap 은 닫힌 집합**이다 — 실측으로 정한다:
        //       `cap io`          `lib/out.low`·`lib/fmt.low`
        //       `cap file_system` `lib/file.low` 이 `effects io` 와 함께 쓴다(정당하다)
        //     ⇒ 그 둘만 센다. 새 종류가 생기면 **여기서 막히고, 그때 답한다**(조용히 늘지 않는다).
        //   ☞ `asm` 은 이미 자기 클래스(`machine`)를 검사한다 — io 만 안 하고 있었다.
        for (proven_size_t q = 0; q < h.np && !has_cap; q++)
            for (proven_size_t z = h.p[q].ts; z + 1 < h.p[q].te; z++)
                if (ck_atom(f->kids[z]) && veq(f->kids[z]->tok.lex, "cap") &&
                    ck_atom(f->kids[z + 1]) &&
                    (veq(f->kids[z + 1]->tok.lex, "io") ||
                     veq(f->kids[z + 1]->tok.lex, "file_system") ||
                     veq(f->kids[z + 1]->tok.lex, "net") ||
                     veq(f->kids[z + 1]->tok.lex, "tty") ||
                     // ★ 시계도 io 를 인가한다 (RFC-0090 N1) — 시계를 읽는 것은 **바깥을 보는
                     //   일**이다. 값을 만들지 않고 관찰만 하지만, 그 관찰이 결정성을 깬다.
                     veq(f->kids[z + 1]->tok.lex, "clock") ||
                     // ★ `cap random` 도 `io` 효과를 인가한다 — OS 에게 엔트로피를 청하는
                     //   것은 바깥과의 대화다(시계와 같은 자리, RFC-0090 N3b).
                     veq(f->kids[z + 1]->tok.lex, "random"))) has_cap = true;
        if (!has_cap)
            emit(out, "E-EFFECT-NO-CAP",
                 "this op declares the `io` effect but receives NO capability that authorizes it. "
                 "`io` is a cap-effect (RFC-0007 §6.7): I/O is a RIGHT you are HANDED, not an ambient "
                 "power — `input fs cap file_system .` (or another `cap …` input). An effect you "
                 "declare but hold no capability for is a claim the signature cannot back",
                 f->kids[1]->tok.line);
    }
}
// ★★★ **no_float 타깃에서 float 은 없다** (RFC-0038 D5 — 임베디드 floor; E-HEAP-NOHOST 의 형제).
//   no-float 프로파일(cortex_m 등)엔 FPU 도 소프트플로트 런타임도 없다 — f32/f64 는 계산할 수 있는
//   값이 아니라 **존재하지 않는다**. 선언 어디에든 f32/f64 타입 낱말이 있으면 거절한다(선언당 한 번).
static bool ck_has_float_tok(const low_cst_t *nd) {
    if (!nd) return false;
    if (nd->kind == LOW_CST_ATOM) {
        proven_u8str_view_t w = nd->tok.lex;
        if (veq(w, "f32") || veq(w, "f64")) return true;
        // ★ float **리터럴**(1.5 등)도 float 이다 — no_float 타깃에선 이것도 거절(그렇지 않으면 실제 double 로
        //   컴파일돼 FPU 없는 기계에서 돈다). 숫자로 시작하고 소수점을 가진 원자는 float 리터럴이다.
        if (w.size >= 2 && w.ptr[0] >= '0' && w.ptr[0] <= '9')
            for (proven_size_t z = 1; z < w.size; z++) if (w.ptr[z] == '.') return true;
    }
    for (proven_size_t i = 0; i < nd->nkids; i++) if (ck_has_float_tok(nd->kids[i])) return true;
    return false;
}
static void ck_float_target(low_check_result_t *out, const low_parse_result_t *pr) {
    if (!low_ir_target()->no_float) return;
    for (proven_size_t i = 0; i < pr->nforms; i++) {
        const low_cst_t *f = pr->forms[i]; ck_cur_form = f;
        if (f->kind != LOW_CST_FORM || !f->nkids || f->kids[0]->kind != LOW_CST_ATOM) continue;
        low_kw_t kw = f->kids[0]->tok.kw;
        if (kw != LOW_KW_FN && kw != LOW_KW_PROC && kw != LOW_KW_STRUCT &&
            kw != LOW_KW_TYPE && kw != LOW_KW_NEWTYPE) continue;
        if (ck_has_float_tok(f))
            emit(out, "E-FLOAT-NOFLOAT",
                 "this declaration uses a FLOAT type (`f32`/`f64`), but the build target has NO FLOAT "
                 "(`machine.no_float` — a freestanding/embedded profile like cortex_m). Float is opt-in "
                 "(RFC-0038 D5): a no_float target has no FPU and no soft-float runtime, so a float is "
                 "not a value it can compute. Use fixed-point integers, or build for a target with float",
                 (f->nkids >= 2 && f->kids[1]->kind == LOW_CST_ATOM) ? f->kids[1]->tok.line : f->line);
    }
}
// ★★★ **벡터/마스크는 순수 값이다 — 소유·빌림 수식자가 붙을 수 없다** (RFC-0040; PRINCIPLES.md §0).
//   `vec`/`mask` 는 SIMD **레지스터 값**이다: 레인이 숫자이고 값으로 **복사**되며(rvalue), `drop` 도 완결도
//   없는 자원 아닌 값. 그래서 `owned`(선형 소유)·`ref`/`mut_ref`(빌림)·`mut`(쓰기 접근)는 **아무것도 바꾸지
//   못하는 장식**이다 — 심지어 `owned vec` 은 복사 가능 값에 선형성을 **잘못** 강제할 수 있다. 배열/슬라이스는
//   주소 있는 **메모리**라 이 수식자들이 정상이지만(그건 안 건드린다), 벡터엔 무의미하므로 **거절**한다.
static void ck_vec_value(low_check_result_t *out, const low_parse_result_t *pr) {
    for (proven_size_t i = 0; i < pr->nforms; i++) {
        const low_cst_t *f = pr->forms[i]; ck_cur_form = f;
        if (f->kind != LOW_CST_FORM || !f->nkids || f->kids[0]->kind != LOW_CST_ATOM) continue;
        low_kw_t kw = f->kids[0]->tok.kw;
        if (kw != LOW_KW_FN && kw != LOW_KW_PROC) continue;
        low_op_header_t h = low_op_header(f);
        for (proven_size_t q = 0; q < h.np; q++) {
            // 수식자를 직접 벗겨 **진짜 타입 head** 를 찾는다. low_op_header 는 mut/owned/unsafe_ptr 만
            // core 로 벗기고 ref/mut_ref 는 core 에 남기므로(borrow 수식자), 여기서 함께 스킵한다.
            // head 가 vec/mask 이면서 그 앞에 소유·빌림 수식자가 있으면 거절 — `mut slice vec`(head=slice,
            // 벡터는 원소)처럼 벡터가 **집합체의 원소**인 정상 케이스는 head 로 걸러져 오검출되지 않는다.
            proven_size_t z = h.p[q].ts; bool qual = false;
            for (; z < h.p[q].te && f->kids[z]->kind == LOW_CST_ATOM; z++) {
                proven_u8str_view_t m = f->kids[z]->tok.lex;
                if (veq(m, "owned") || veq(m, "mut") || veq(m, "ref") || veq(m, "mut_ref")) qual = true;
                else if (veq(m, "unsafe_ptr") || veq(m, "comptime")) { /* 소유·빌림 아님 — 넘김 */ }
                else break;   // head
            }
            if (z >= h.p[q].te || f->kids[z]->kind != LOW_CST_ATOM) continue;
            proven_u8str_view_t ct = f->kids[z]->tok.lex;
            if (qual && (veq(ct, "vec") || veq(ct, "mask")))
                emit(out, "E-VEC-QUAL",
                     "a `vec`/`mask` is a pure VALUE (an SIMD register — its lanes are numbers, copied "
                     "by value, never a resource with `drop`). `owned`/`ref`/`mut` are meaningless on it "
                     "(RFC-0040; PRINCIPLES.md §0 forbids a qualifier that changes nothing — `owned` would even "
                     "impose linearity on a copyable value). A vector is not addressable memory like a "
                     "slice/array; pass it by value: `input <name> vec <T> <n> .`",
                     f->kids[z]->tok.line);
        }
    }
}
// ★★★ **`unsafe target <iset>` — target-intrinsic 격리 게이트** (RFC-0040 D5). target intrinsic 은 VM 이 실행
//   못 하므로 **오라클(diff-sweep) 밖**이다(할당처럼). 그래서 지금은 **격리 스캐폴드**만 싣는다 — intrinsic 몸통은
//   없고, 이 op 이 (1) **아는 명령셋**을 대고(E-TARGET-ISET, 폐집합) (2) 그 명령셋을 **주는 target 에서만** 컴파일
//   되게(E-TARGET-INTRIN) 강제한다. 검증 불가한 건 하나도 안 실린다 — 나중에 intrinsic 이 이 게이트 안으로 들어온다.
// ★ 몸통 어딘가에서 그 낱말을 머리로 쓰는가 (intrinsic 사용 탐지)
static bool ck_uses_head(const low_cst_t *nd, const char *w) {
    if (!nd) return false;
    if (nd->kind == LOW_CST_FORM && nd->nkids >= 1 && nd->kids[0]->kind == LOW_CST_ATOM &&
        veq(nd->kids[0]->tok.lex, w)) return true;
    for (proven_size_t i = 0; i < nd->nkids; i++) if (ck_uses_head(nd->kids[i], w)) return true;
    return false;
}
static void ck_target_intrin(low_check_result_t *out, const low_parse_result_t *pr) {
    static const struct { const char *iset; const char *tgt; } ISETS[] = {
        { "x86_sse2", "x86_64" }, { "x86_avx2", "x86_64" }, { "x86_avx512", "x86_64" },
        { "arm_neon", "arm64" },  { "arm_sve", "arm64" },   { "riscv_v", "riscv64" },
    };
    // ★★★ **`avg` 의 격리를 푼다** (결함 노트 #70 · RFC-0115 §8-8, 2026-09-17).
    //   여기 있던 것: `avg` 를 «x86 PAVGB / ARM VRHADD 가 있어야 뜻이 서는 기계 붙박이» 로 보고
    //   `unsafe target <iset>` 안에서만 쓰게 막았다(`E-INTRIN-OUTSIDE` · `E-INTRIN-ISET`).
    //   ☞ 그런데 **정본 §6.3.7 은 `avg` 를 보통 레인 op 으로 적는다**. 그리고 실제로 두 뒤끝 모두
    //     기계 명령이 아니라 **넓힌 정수 산술**로 낸다(VM: u64 로 넓혀 더함 · 씨: 같은 식을 그대로
    //     찍는다). 곧 이 op 은 이미 어느 기계에서나 같은 값을 내고 오라클 안에 있다 —
    //     막고 있던 것은 **있지도 않은 기계 의존**이었다. 글과 도구가 갈린 자리에서 **글을 따른다.**
    //   ★ 잃지 않아야 할 지식: 뒷날 이 op 을 기계 명령으로 내리려는 처리기는 그 명령이 정확히
    //     (a+b+1)>>1 인지 확인해야 한다 — RISC-V 의 vaaddu 는 반올림이 vxrm CSR 에 달려 있어
    //     비트-정확이 보장되지 않는다. 그 사실은 정본 §6.3.7 의 [!주의] 로 옮겨 적었다.
    for (proven_size_t i = 0; i < pr->nforms; i++) {
        const low_cst_t *f = pr->forms[i]; ck_cur_form = f;
        if (!f || f->kind != LOW_CST_FORM || !f->nkids || f->kids[0]->kind != LOW_CST_ATOM) continue;
        if (!f->target_iset.size) continue;
        const char *need_tgt = NULL;
        for (proven_size_t z = 0; z < sizeof ISETS / sizeof ISETS[0]; z++)
            if (veq(f->target_iset, ISETS[z].iset)) { need_tgt = ISETS[z].tgt; break; }
        if (!need_tgt) {
            emit(out, "E-TARGET-ISET",
                 "`unsafe target <iset>` names an instruction set the compiler does not know (RFC-0040 D5). "
                 "The set is CLOSED, like the keyword vocabulary: x86_sse2 / x86_avx2 / x86_avx512 · arm_neon / "
                 "arm_sve · riscv_v. An unknown iset would be a silent no-op — refused instead", f->line);
            continue;
        }
        if (strcmp(low_ir_target()->name, need_tgt) != 0)
            emit(out, "E-TARGET-INTRIN",
                 "this `unsafe target <iset>` op needs an instruction set the BUILD TARGET does not provide — a "
                 "target-specific intrinsic op only compiles for its own target (RFC-0040 D5, RFC-0037 acceptance "
                 "bar): it breaks portability, so its target is visible and enforced. Build for the matching "
                 "target, or guard this op out on others (it is not a value this machine can compute)", f->line);
    }
}
// ★★★ **profile 이 동시성 상한을 게이팅한다** (RFC-0009 §105·§139).
//   `build profile <p>` 는 이 모듈이 **어떤 기계 위에서 살 것인가**를 말한다. 그 기계가 줄 수 없는
//   동시성을 쓰면 그것은 **컴파일 시에 알 수 있는 거짓말**이다:
//     freestanding = 동시성 0 · embedded = 정적 task graph · native = job/green thread · server = actor
//   level 은 **선언하지 않는다** — 쓴 구문에서 계산되고(§131) profile 이 상한을 건다.
//   ★ profile 을 **안 적으면 게이팅하지 않는다**(오늘 동작 유지) — 적은 사람만 그 약속에 매인다.
static int ck_conc_level_of(const low_cst_t *nd) {
    if (!nd) return 0;
    int lv = 0;
    if (nd->kind == LOW_CST_ATOM || (nd->kind == LOW_CST_FORM && nd->nkids && nd->kids[0]->kind == LOW_CST_ATOM)) {
        proven_u8str_view_t w = (nd->kind == LOW_CST_ATOM) ? nd->tok.lex : nd->kids[0]->tok.lex;
        // ★ **레벨 3(actor)은 `actor` 선언으로만 판정한다.** `send` 는 **채널 송신에도 쓰이고**
        //   (vm_tgroup), `drain` 은 아예 **사용자 op 이름**이기도 하다(vm_stackfast) — 낱말만 보고
        //   레벨을 올리면 **엉뚱한 프로그램을 고발한다**. 낱말이 두 뜻이면 그 낱말로 판정하지 않는다.
        // ★★★★★ **`spawn` 은 한 낱말이 두 등급이다** (2026-08-19, 소유자 결정 — 표를 다시 봄).
        //   RFC-0009 §6.1 은 level 1(data-parallel)의 opt-in 트리거로 **`spawn`/`task_group`** 을
        //   적어 두었고, 그 줄의 profile 칸이 바로 *"embedded = 정적 graph만"* 이다.
        //   level 2(actor)의 트리거는 `actor`·`channel`·**`spawn actor`**·`on` 이다.
        //   그런데 이 검사기는 **낱말만 보고** `spawn` 을 통째로 2 로 올렸다 ⇒ `task_group` 안의
        //   구조적 job spawn 이 모듈을 2 로 밀어 올려 **embedded 가 자기 것을 못 썼다.**
        //   ☞ 그래서 백로그에 *"spawn 이 level 2 라 embedded 는 한계"* 라고 적혀 있었는데,
        //     그것은 **표가 아니라 구현**의 상태였다. 표는 처음부터 허용하고 있었다.
        //   ⇒ 낱말이 두 뜻이면 **모양으로** 가른다(바로 위 문단이 `send`·`drain` 에 대해 이미
        //     내린 결론이다): `spawn actor …` · `spawn send …` 만 actor 급이고, 나머지
        //     `spawn <op> [args…]` 는 **구조적 job = level 1** 이다.
        if (veq(w, "spawn")) {
            lv = 1;
            if (nd->kind == LOW_CST_FORM && nd->nkids >= 2 && nd->kids[1]->kind == LOW_CST_ATOM &&
                (veq(nd->kids[1]->tok.lex, "actor") || veq(nd->kids[1]->tok.lex, "send")))
                lv = 2;
        }
        else if (veq(w, "spawn_task") || veq(w, "green_thread") ||
                 veq(w, "channel") || veq(w, "await") || veq(w, "schedule") || veq(w, "send")) lv = 2;
        else if (veq(w, "task_group")) lv = 1;                                // 정적 task graph
    }
    for (proven_size_t i = 0; i < nd->nkids; i++) {
        int c = ck_conc_level_of(nd->kids[i]);
        if (c > lv) lv = c;
    }
    return lv;
}
static void ck_profile(low_check_result_t *out, const low_parse_result_t *pr) {
    static const struct { const char *name; int cap; } PROF[] = {
        { "freestanding", 0 }, { "embedded", 1 }, { "native", 2 }, { "server", 3 },
    };
    int cap = -1; proven_u32 pline = 0;
    for (proven_size_t i = 0; i < pr->nforms; i++) {
        const low_cst_t *f = pr->forms[i]; ck_cur_form = f;
        if (f->kind != LOW_CST_FORM || f->nkids < 3 || !ck_atom(f->kids[0]) || !ck_atom(f->kids[1]) ||
            !veq(f->kids[0]->tok.lex, "build") || !veq(f->kids[1]->tok.lex, "profile")) continue;
        if (!ck_atom(f->kids[2])) continue;
        pline = f->kids[0]->tok.line;
        for (proven_size_t z = 0; z < sizeof PROF / sizeof PROF[0]; z++)
            if (veq(f->kids[2]->tok.lex, PROF[z].name)) { cap = PROF[z].cap; break; }
        if (cap < 0)
            emit(out, "E-PROFILE-UNKNOWN",
                 "the build PROFILE is a CLOSED set: freestanding · embedded · native · server "
                 "(RFC-0009 §105). The profile says what MACHINE this module will live on, and it "
                 "gates which concurrency features may exist — an unknown one would gate nothing "
                 "while looking like it gated something", pline);
    }
    if (cap < 0) return;   // profile 을 안 적었으면 게이팅하지 않는다
    int lv = 0;
    for (proven_size_t i = 0; i < pr->nforms; i++) {
        const low_cst_t *f = pr->forms[i]; ck_cur_form = f;
        int c = 0;
        if (f->kind == LOW_CST_FORM && f->nkids && ck_atom(f->kids[0]) &&
            f->kids[0]->tok.kw == LOW_KW_ACTOR) c = 3;      // actor 선언 자체가 server 급이다
        int b = ck_conc_level_of(f);
        if (b > c) c = b;
        if (c > lv) lv = c;
    }
    if (lv > cap)
        emit(out, "E-PROFILE-LEVEL",
             "this module uses a CONCURRENCY FEATURE its build profile does not provide "
             "(RFC-0009 §105/§139): freestanding has no concurrency at all, embedded has a static "
             "task graph, native adds jobs/green threads, server adds actors. The level is not "
             "declared — it is COMPUTED from the constructs you used — and the profile caps it. "
             "Either raise the profile or stop using the feature", pline);
}
// ★★★ **map/filter 의 싱크는 mut 여야 한다** (RFC-0016 — 호출자-싱크 모델). map/filter 는 결과를 첫 인자
//   `out` 에 **쓴다**(ISTORE). 그 자리가 읽기 전용 slice 면 읽기 전용에 쓰는 것이다 — mut 규율(E-TYPE-MUT)이
//   `set (index …)` 만 보던 틈이다. out 은 **mut slice 파라미터**여야 한다(할당 없는 유계 싱크).
static void ck_map_sink_walk(low_check_result_t *out, const low_cst_t *nd, const low_op_header_t *h,
                             const low_cst_t *opf, const low_parse_result_t *pr) {
    if (!nd) return;
    if (nd->kind == LOW_CST_FORM && nd->nkids >= 2 && nd->kids[0]->kind == LOW_CST_ATOM &&
        (veq(nd->kids[0]->tok.lex, "map") || veq(nd->kids[0]->tok.lex, "filter")) &&
        nd->kids[1]->kind == LOW_CST_ATOM) {
        proven_u8str_view_t on = nd->kids[1]->tok.lex;
        bool mut = false, found = false; proven_size_t pq = 0;
        for (proven_size_t q = 0; q < h->np; q++)
            if (proven_u8str_view_eq(h->p[q].name, on)) { mut = h->p[q].is_mut; found = true; pq = q; break; }
        if (found && !mut)
            emit(out, "E-MAP-SINK",
                 "map/filter WRITES its result into the first argument (the sink), so that must be a "
                 "`mut slice` PARAMETER — a caller-provided buffer (no allocation, RFC-0016). Writing "
                 "into a read-only slice is exactly what the mut discipline forbids. Declare it "
                 "`input <name> mut slice <T> .`", nd->kids[0]->tok.line);
        // ★★ 원소는 **스칼라여야** 한다 — map/filter 는 원소를 index-store 로 쓴다(scalar-only). 구조체/enum
        //   슬라이스는 백엔드가 낮출 수 없다(VM 은 E-VM-TYPE, native 는 panic). **정적으로 거절**한다.
        if (found && opf) {
            for (proven_size_t z = h->p[pq].core; z + 1 < h->p[pq].te; z++)
                if (opf->kids[z]->kind == LOW_CST_ATOM &&
                    (veq(opf->kids[z]->tok.lex, "slice") || veq(opf->kids[z]->tok.lex, "array")) &&
                    opf->kids[z + 1]->kind == LOW_CST_ATOM &&
                    ck_is_aggregate(pr, opf->kids[z + 1]->tok.lex)) {
                    emit(out, "E-MAP-ELEM",
                         "map/filter's element type is a STRUCT/ENUM, but these ops write each element "
                         "with a scalar index-store — an aggregate element cannot be lowered that way "
                         "(RFC-0016). Use a scalar element slice, or hand-write the loop", nd->kids[0]->tok.line);
                    break;
                }
        }
    }
    for (proven_size_t i = 0; i < nd->nkids; i++) ck_map_sink_walk(out, nd->kids[i], h, opf, pr);
}
static void ck_map_sink(low_check_result_t *out, const low_parse_result_t *pr) {
    for (proven_size_t i = 0; i < pr->nforms; i++) {
        const low_cst_t *f = pr->forms[i]; ck_cur_form = f;
        if (f->kind != LOW_CST_FORM || !f->nkids || f->kids[0]->kind != LOW_CST_ATOM) continue;
        low_kw_t kw = f->kids[0]->tok.kw;
        if (kw != LOW_KW_FN && kw != LOW_KW_PROC) continue;
        low_op_header_t h = low_op_header(f);
        const low_cst_t *body = (f->nkids && f->kids[f->nkids - 1]->kind == LOW_CST_BLOCK)
                                    ? f->kids[f->nkids - 1] : NULL;
        ck_map_sink_walk(out, body, &h, f, pr);
    }
}
// ★★★ **plain `field`/`set` 도 MMIO 접근이다** (RFC-0042 건전성 구멍). `read_volatile`/`write_volatile`
//   만 MMIO 로 보던 탓에, 같은 레지스터를 `field g idr` / `set (field g idr) …` / 붙임점 `g.idr` 로
//   만지면 cap·effect 게이트를 통째로 우회했다. 여기서는 **타입으로** 판정한다: 접근의 base 가
//   **mmio 로 표시된 struct 를 뷰하는 지역**일 때만 MMIO 로 센다(동명 필드 오탐 금지).
static bool ck_is_mmio_struct(const low_parse_result_t *pr, proven_u8str_view_t name) {
    if (!name.size) return false;
    for (proven_size_t i = 0; i < pr->nforms; i++) {
        const low_cst_t *f = pr->forms[i]; ck_cur_form = f;
        if (f->kind != LOW_CST_FORM || f->nkids < 2 || !ck_atom(f->kids[0])) continue;
        if (f->kids[0]->tok.kw != LOW_KW_STRUCT || !ck_atom(f->kids[1])) continue;
        if (!proven_u8str_view_eq(f->kids[1]->tok.lex, name)) continue;
        const low_cst_t *blk = f->kids[f->nkids - 1];
        if (blk->kind != LOW_CST_BLOCK) return false;
        for (proven_size_t j = 0; j < blk->nkids; j++) {
            const low_cst_t *fld = blk->kids[j];
            if (fld->kind == LOW_CST_FORM && fld->nkids >= 1 && ck_atom(fld->kids[0]) &&
                veq(fld->kids[0]->tok.lex, "mmio")) return true;
        }
        return false;
    }
    return false;
}
typedef struct { proven_u8str_view_t name, ty; } ck_mm_bind_t;
// op 안의 var/let 바인딩을 name→타입이름 으로 모은다(한정자 mut/owned/ref/mut_ref 는 벗긴다).
static void ck_mm_collect_binds(const low_cst_t *nd, ck_mm_bind_t *b, proven_size_t *nb, proven_size_t cap) {
    if (!nd) return;
    if (nd->kind == LOW_CST_FORM && nd->nkids >= 3 && ck_atom(nd->kids[0]) &&
        (nd->kids[0]->tok.kw == LOW_KW_VAR || nd->kids[0]->tok.kw == LOW_KW_LET) &&
        ck_atom(nd->kids[1])) {
        proven_size_t tw = 2;
        while (tw < nd->nkids && ck_atom(nd->kids[tw]) &&
               (veq(nd->kids[tw]->tok.lex, "mut") || veq(nd->kids[tw]->tok.lex, "owned") ||
                veq(nd->kids[tw]->tok.lex, "ref") || veq(nd->kids[tw]->tok.lex, "mut_ref")))
            tw++;
        if (tw < nd->nkids && ck_atom(nd->kids[tw]) && nd->kids[tw]->tok.kw != LOW_KW_BE &&
            *nb < cap) {
            b[*nb].name = nd->kids[1]->tok.lex;
            b[*nb].ty   = nd->kids[tw]->tok.lex;
            (*nb)++;
        }
    }
    // ★★★ **파라미터도 이름이다** (2026-08-03). `input q mut tickq .` 의 `q` 를 안 모으면
    //   라이브러리 op 의 volatile 대상이 *"모르는 이름"* 이 되고, 보수 판정이 `cap mmio` 를
    //   요구한다 — 그게 리액터의 큐 조작을 막았다. 잡는 모양은 `var` 와 같다.
    for (proven_size_t i = 0; i < nd->nkids; i++) ck_mm_collect_binds(nd->kids[i], b, nb, cap);
}
// ★★★ **파라미터도 이름이다 — 다만 volatile 판정에만 쓴다** (2026-08-03).
//   `ck_field_touches_mmio` 에까지 먹이면 **by-value mmio 파라미터**가 검사기에서 먼저
//   울어(E-MMIO-NOCAP) 하강이 내는 **더 정확한 진단**(E-MMIO-BYVALUE — 그 파라미터는 애초에
//   가질 수 없다)에 못 닿는다. 골든이 그것을 잡았다. ⇒ 넓힘은 **필요한 자리에만** 준다.
//   ☞ 파라미터는 폼이 아니라 **평평한 형제 원자**다(괄호 없는 전위 표기).
static void ck_mm_collect_params(const low_cst_t *nd, ck_mm_bind_t *b, proven_size_t *nb, proven_size_t cap) {
    if (!nd) return;
    for (proven_size_t i = 0; i + 2 < nd->nkids; i++) {
        if (!ck_atom(nd->kids[i]) || !veq(nd->kids[i]->tok.lex, "input")) continue;
        if (!ck_atom(nd->kids[i + 1])) continue;
        proven_size_t tw = i + 2;
        while (tw < nd->nkids && ck_atom(nd->kids[tw]) &&
               (veq(nd->kids[tw]->tok.lex, "mut") || veq(nd->kids[tw]->tok.lex, "owned") ||
                veq(nd->kids[tw]->tok.lex, "ref") || veq(nd->kids[tw]->tok.lex, "mut_ref")))
            tw++;
        if (tw < nd->nkids && ck_atom(nd->kids[tw]) && *nb < cap) {
            b[*nb].name = nd->kids[i + 1]->tok.lex;
            b[*nb].ty   = nd->kids[tw]->tok.lex;
            (*nb)++;
        }
    }
    for (proven_size_t i = 0; i < nd->nkids; i++) ck_mm_collect_params(nd->kids[i], b, nb, cap);
}
static bool ck_local_is_mmio(const low_parse_result_t *pr, const ck_mm_bind_t *b, proven_size_t nb,
                             proven_u8str_view_t local) {
    for (proven_size_t i = 0; i < nb; i++)
        if (proven_u8str_view_eq(b[i].name, local))
            return ck_is_mmio_struct(pr, b[i].ty);
    return false;
}
static bool ck_field_mmio_walk(const low_parse_result_t *pr, const low_cst_t *nd,
                               const ck_mm_bind_t *b, proven_size_t nb) {
    if (!nd) return false;
    // canonical: (field <local> <reg>) — `set (field …)` 의 안쪽도 이 형태다
    if (nd->kind == LOW_CST_FORM && nd->nkids == 3 && ck_atom(nd->kids[0]) &&
        veq(nd->kids[0]->tok.lex, "field") && ck_atom(nd->kids[1]) &&
        ck_local_is_mmio(pr, b, nb, nd->kids[1]->tok.lex)) return true;
    // 붙임점 원자: <local>.<reg> (읽기 `g.idr` · `set g.idr …` 양쪽)
    if (nd->kind == LOW_CST_ATOM && nd->tok.kind == LOW_TOK_IDENT) {
        proven_u8str_view_t v = nd->tok.lex;
        proven_size_t dot = 0;
        for (proven_size_t i = 0; i < v.size; i++) if (v.ptr[i] == '.') { dot = i; break; }
        if (dot && dot + 1 < v.size) {
            proven_u8str_view_t head = { .ptr = v.ptr, .size = dot };
            if (ck_local_is_mmio(pr, b, nb, head)) return true;
        }
    }
    for (proven_size_t i = 0; i < nd->nkids; i++)
        if (ck_field_mmio_walk(pr, nd->kids[i], b, nb)) return true;
    return false;
}
static bool ck_field_touches_mmio(const low_parse_result_t *pr, const low_cst_t *f) {
    ck_mm_bind_t b[64]; proven_size_t nb = 0;
    ck_mm_collect_binds(f, b, &nb, 64);
    return ck_field_mmio_walk(pr, f, b, nb);
}
// ★★★★ **예약된 정적 블록도 권한이다** (RFC-0039 §9-2 갈래①, 2026-08-02).
//   `storage reserved .` 는 *"내 저장소는 링커가 준다"* 이고, 그것을 아무나 열 수 있으면 그 순간
//   **전역 변수**다 — 아무도 건네주지 않은 권한이고, 이 언어가 유일하게 안 가진 것이다.
//   ⇒ 여는 데 권한이 필요하다: **ISR 이거나**(`vector N .` — 하드웨어가 불렀다는 사실이 곧
//     권한, D5-b 의 논증 그대로) **`cap machine` 을 건네받았거나**.
//   ★ 새 cap 낱말 0 — 정적 저장소는 기계의 자원이고 `wfi` 를 부를 권한과 같은 종류다.
//   ☞ 좁게 잡는다: `reserve` 타입 이름이 `view` 뒤에 나오기만 해도 권한을 요구한다.
//     `view T <바이트>` 는 권한이 필요 없지만, 둘을 여기서 가르려면 CST 에서 "피연산자가
//     없다" 를 재야 하고 그건 하강이 하는 일이다. **넓게 요구하고 좁게 푸는** 편이 안전하다.
static bool ck_is_reserve_struct(const low_parse_result_t *pr, proven_u8str_view_t name) {
    for (proven_size_t i = 0; i < pr->nforms; i++) {
        const low_cst_t *f = pr->forms[i]; ck_cur_form = f;
        if (f->kind != LOW_CST_FORM || f->nkids < 3 || !ck_atom(f->kids[0])) continue;
        if (f->kids[0]->tok.kw != LOW_KW_STRUCT) continue;
        if (!ck_atom(f->kids[1]) || !proven_u8str_view_eq(f->kids[1]->tok.lex, name)) continue;
        const low_cst_t *blk = f->kids[f->nkids - 1];
        for (proven_size_t j = 0; j < blk->nkids; j++) {
            const low_cst_t *fl = blk->kids[j];
            if (fl->kind == LOW_CST_FORM && fl->nkids >= 1 && ck_atom(fl->kids[0])
                && fl->nkids >= 2 && veq(fl->kids[0]->tok.lex, "storage")
                && ck_atom(fl->kids[1]) && veq(fl->kids[1]->tok.lex, "reserved")) return true;
        }
        return false;
    }
    return false;
}
// ☞ 괄호 없는 전위 표기라 `be view qq .` 의 `view` 와 `qq` 는 **중첩 폼이 아니라 평평한
//   형제 원자 쌍**이다(처음에 중첩으로 찾다가 아무것도 못 잡았다 — 골든이 그것을 잡았다).
static bool ck_opens_reserve(const low_parse_result_t *pr, const low_cst_t *nd) {
    if (!nd) return false;
    for (proven_size_t i = 0; i + 1 < nd->nkids; i++)
        if (ck_atom(nd->kids[i]) && veq(nd->kids[i]->tok.lex, "view")
            && ck_atom(nd->kids[i + 1])
            && ck_is_reserve_struct(pr, nd->kids[i + 1]->tok.lex)) return true;
    for (proven_size_t i = 0; i < nd->nkids; i++)
        if (ck_opens_reserve(pr, nd->kids[i])) return true;
    return false;
}
static void ck_reserve(low_check_result_t *out, const low_parse_result_t *pr) {
    for (proven_size_t i = 0; i < pr->nforms; i++) {
        const low_cst_t *f = pr->forms[i]; ck_cur_form = f;
        if (f->kind != LOW_CST_FORM || f->nkids < 2 || !ck_atom(f->kids[0])) continue;
        low_kw_t kw = f->kids[0]->tok.kw;
        if (kw != LOW_KW_FN && kw != LOW_KW_PROC) continue;
        if (!ck_opens_reserve(pr, f)) continue;
        low_op_header_t h = low_op_header(f);
        bool ok = false;
        for (proven_size_t q = 0; q < h.np && !ok; q++)
            for (proven_size_t z = h.p[q].ts; z < h.p[q].te; z++)
                if (ck_atom(f->kids[z]) && veq(f->kids[z]->tok.lex, "machine")) { ok = true; break; }
        for (proven_size_t j = 2; j + 1 < f->nkids && !ok; j++)
            if (ck_atom(f->kids[j]) && veq(f->kids[j]->tok.lex, "vector")) ok = true;
        // ★★★★ **예약 블록을 만지면 `state` 다** (RFC-0029 · RFC-0039 §9-7, 2026-08-03 소유자 결정).
        //   `state` 의 정의가 이미 정확히 이것이다: *"모듈 가변 전역 접근을 표시하는 13번째
        //   atom — 전역 상태가 escape·은닉·레이스를 일으키지 않음을 effect 로 가시화한다"*.
        //   예약 블록은 **링커가 잡아 주는, 프로그램 수명 내내 사는, 권한이 있어야 열리는
        //   모듈 가변 전역**이다. 권한을 뺀 나머지는 전역 변수가 맞다.
        //   ☞ 그전까지 큐 조작 op 들이 `effects none` 이었다 — 순수하다는 뜻이고, **거짓말**이다
        //     (effect 는 계약이고 지키지 않는 계약은 정직하지 않다, RFC-0007).
        //   ★ 그래서 `fn`(순수)은 예약 블록을 못 만진다. 그게 맞다: ISR 이 바꾸는 값을 읽는
        //     것은 순수가 아니다.
        bool has_state = false;
        for (proven_size_t j = h.eff_s; j < h.eff_e; j++)
            if (ck_atom(f->kids[j]) && veq(f->kids[j]->tok.lex, "state")) has_state = true;
        if (!has_state)
            emit(out, "E-RESERVE-NOEFFECT",
                 "this op opens a `storage reserved .` block but does not declare `effects state`. "
                 "That block is MODULE-LEVEL MUTABLE GLOBAL storage — the linker hands it out and it "
                 "lives for the whole program — and `state` is exactly the atom that makes touching "
                 "such storage visible (RFC-0029). Declaring `none` here says the op is pure while it "
                 "mutates a global an interrupt also writes; an effect you perform but never declare "
                 "is the mirror of one you declare but never do, and both make the clause a lie "
                 "(RFC-0007). A `fn` therefore cannot touch one, which is correct: reading a value an "
                 "ISR changes underneath you is not a pure calculation",
                 f->kids[1]->tok.line);
        if (!ok)
            emit(out, "E-RESERVE-NOCAP",
                 "this op opens a `storage reserved .` block — storage the LINKER hands the type — but it was "
                 "handed no authority. Either be an INTERRUPT HANDLER (`vector N .` IS the grant: the "
                 "hardware called you, RFC-0042 D5-b) or receive `cap machine`. Static storage anyone "
                 "may open is a global variable, and a global variable is authority NOBODY HANDED "
                 "YOU — the one thing this language does not have (RFC-0039 §9-2)",
                 f->kids[1]->tok.line);
    }
}
// ★★★★ **volatile 은 장치의 것만이 아니다** (RFC-0039 §9-6, 2026-08-03).
//   `read_volatile`/`write_volatile` 은 *"이 메모리는 내 밑에서 바뀐다 — 접기지 말라"* 는 말이고,
//   그 사실은 **장치 레지스터에만** 있는 게 아니다: ISR 이 게시하는 잡 큐가 정확히 그렇다.
//   그런데 이 게이트는 op 이 volatile 을 **쓰기만 하면** `cap mmio` 를 요구했다 — 그래서
//   T1 리액터의 폴링 루프가 **평범한 메모리를 volatile 로 읽을 길이 없었다**(RFC-0039 §9-4).
//   ⇒ **대상을 본다.** 모든 volatile 접근이 *알려진 비-mmio 지역*을 향하면 이 게이트는 비켜선다.
//     ☞ 모르면 **요구한다**(대상을 못 읽는 형태는 보수적으로 장치로 본다) — 넓게 요구하고
//       좁게 푸는 편이 안전하다.
static bool ck_vol_walk(const low_parse_result_t *pr, const low_cst_t *nd,
                        const ck_mm_bind_t *b, proven_size_t nb) {
    if (!nd) return false;
    for (proven_size_t i = 0; i < nd->nkids; i++) {
        const low_cst_t *k = nd->kids[i];
        if (!ck_atom(k)) continue;
        if (!veq(k->tok.lex, "read_volatile") && !veq(k->tok.lex, "write_volatile")) continue;
        if (i + 1 >= nd->nkids || !ck_atom(nd->kids[i + 1])) return true;   // 대상을 못 읽는다
        proven_u8str_view_t tgt = nd->kids[i + 1]->tok.lex;
        bool known = false;
        for (proven_size_t z = 0; z < nb; z++)
            if (proven_u8str_view_eq(b[z].name, tgt)) { known = true; break; }
        if (!known) return true;                       // 지역이 아니면 보수적으로
        if (ck_local_is_mmio(pr, b, nb, tgt)) return true;
    }
    for (proven_size_t i = 0; i < nd->nkids; i++)
        if (ck_vol_walk(pr, nd->kids[i], b, nb)) return true;
    return false;
}
static bool ck_volatile_is_device(const low_parse_result_t *pr, const low_cst_t *f) {
    ck_mm_bind_t b[64]; proven_size_t nb = 0;
    ck_mm_collect_binds(f, b, &nb, 64);
    ck_mm_collect_params(f, b, &nb, 64);
    return ck_vol_walk(pr, f, b, nb);
}
// ★★★★★ **예약 블록을 만지는 것은 `state` 다** (2026-09-06 — 요구와 경고가 서로를 부정하던
//   **네 번째** 자리).
//
//   `E-RESERVE-NOEFFECT` 는 예약 블록(`storage reserved .`)을 여는 op 에 **`effects state` 를
//   요구한다** — 그 메시지가 이유까지 적는다: *"그 블록은 모듈 수준 가변 전역이고, `state` 가
//   바로 그것을 만지는 일을 보이게 하는 원자다(RFC-0029)"*. 그런데 효과를 **세는** 쪽은
//   그 사실을 안 셌다 ⇒ 요구대로 선언한 op 이 곧바로 *"선언했는데 안 한다"* 로 경고됐다
//   (`prog/reactor.low` 의 큐 조작 셋: `post`·`pending`·`take`).
//   ⇒ mmio 와 **같은 모양**으로 센다: 대상의 타입이 예약 블록이면 그 접근은 모듈 상태다.
//     요구하는 쪽과 세는 쪽이 **같은 판정**을 쓴다.
//   ☞ *한 사실을 두 곳이 다르게 판정하면 둘 중 하나는 반드시 거짓말이 된다* — 이 파일이
//     mmio 자리에 적어 둔 그 문장을 여기서 한 번 더 쓴다.
static bool ck_local_is_reserved(const low_parse_result_t *pr, const ck_mm_bind_t *b,
                                 proven_size_t nb, proven_u8str_view_t local) {
    for (proven_size_t i = 0; i < nb; i++)
        if (proven_u8str_view_eq(b[i].name, local))
            return ck_is_reserve_struct(pr, b[i].ty);
    return false;
}
static bool ck_res_walk(const low_parse_result_t *pr, const low_cst_t *nd,
                        const ck_mm_bind_t *b, proven_size_t nb) {
    if (!nd) return false;
    for (proven_size_t i = 0; i < nd->nkids; i++) {
        const low_cst_t *k = nd->kids[i];
        if (!ck_atom(k)) continue;
        if (!veq(k->tok.lex, "read_volatile") && !veq(k->tok.lex, "write_volatile")) continue;
        if (i + 1 < nd->nkids && ck_atom(nd->kids[i + 1]) &&
            ck_local_is_reserved(pr, b, nb, nd->kids[i + 1]->tok.lex)) return true;
    }
    for (proven_size_t i = 0; i < nd->nkids; i++)
        if (ck_res_walk(pr, nd->kids[i], b, nb)) return true;
    return false;
}
static bool ck_touches_reserved(const low_parse_result_t *pr, const low_cst_t *f) {
    ck_mm_bind_t b[64]; proven_size_t nb = 0;
    ck_mm_collect_binds(f, b, &nb, 64);
    ck_mm_collect_params(f, b, &nb, 64);
    return ck_res_walk(pr, f, b, nb);
}
static void ck_mmio(low_check_result_t *out, const low_parse_result_t *pr) {
    for (proven_size_t i = 0; i < pr->nforms; i++) {
        const low_cst_t *f = pr->forms[i]; ck_cur_form = f;
        if (f->kind != LOW_CST_FORM || f->nkids < 2 || !ck_atom(f->kids[0])) continue;
        low_kw_t kw = f->kids[0]->tok.kw;
        if (kw != LOW_KW_FN && kw != LOW_KW_PROC) continue;
        // ★ read_volatile/write_volatile 뿐 아니라, mmio 뷰를 `field`/`set`/붙임점으로 만지는
        //   것도 MMIO 접근이다 — cap·effect 게이트는 그 전부에 걸린다(RFC-0042 D2/D7).
        if (!ck_volatile_is_device(pr, f) && !ck_field_touches_mmio(pr, f)) continue;
        low_op_header_t h = low_op_header(f);
        bool has_cap = false, has_dev = false;
        for (proven_size_t q = 0; q < h.np; q++)
            for (proven_size_t z = h.p[q].ts; z < h.p[q].te; z++)
                if (ck_atom(f->kids[z]) && veq(f->kids[z]->tok.lex, "mmio")) has_cap = true;
        // ★★★ **`vector N .` 이 곧 권한이다** (RFC-0042 D5-b · 2026-08-01).
        //
        //   실측으로 드러난 막다른 길: ISR 은 **인자를 받을 수 없다**(E-ISR-PARAMS — 하드웨어는
        //   인자를 안 넘긴다). 그러면 `cap mmio` 를 **영원히 들 수 없다.** 그런데 ISR 이
        //   존재하는 이유가 바로 디바이스다 ⇒ **존재 이유가 표현 불가능**했다.
        //   (RFC-0042 의 예제 ISR 이 `return 0` 뿐이었던 것이 그 증거다.)
        //
        //   답: **하드웨어가 불렀다는 사실이 그 권한이다.** 이것은 예외가 아니라 이 저장소가
        //   이미 쓰는 논증이다 — `export extern`(씨가 우리를 부른다)도 **문 앞에서 계약을
        //   검사하고 문 안을 우리 것으로 삼는다**(RFC-0055 D6). ISR 은 하드웨어의 콜백이고,
        //   그 권위는 **벡터 테이블에 실렸다는 사실**에서 온다.
        //
        //   ★ 그리고 이 권한은 **샐 수 없다**: ISR 은 프로그램이 부를 수 없고(E-ISR-CALLED)
        //     인자가 없으므로 남에게 건넬 것도 없다. 권한이 그 함수 안에 갇힌다.
        for (proven_size_t j = 2; j + 1 < f->nkids; j++)
            if (ck_atom(f->kids[j]) && veq(f->kids[j]->tok.lex, "vector")) has_cap = true;
        for (proven_size_t j = h.eff_s; j < h.eff_e; j++)
            if (ck_atom(f->kids[j]) && veq(f->kids[j]->tok.lex, "device")) has_dev = true;
        if (!has_cap)
            emit(out, "E-MMIO-NOCAP",
                 "this op touches DEVICE REGISTERS (`read_volatile`/`write_volatile`) but receives "
                 "no `cap mmio`. Hardware access is a RIGHT you are HANDED, not an ambient power "
                 "(RFC-0042 D2) — exactly like allocation (RFC-0043 D1). (An INTERRUPT HANDLER is "
                 "the one exception and it is not ambient either: `vector N .` IS the grant, "
                 "because the hardware called you — RFC-0042 D5-b)",
                 f->kids[1]->tok.line);
        if (!has_dev)
            emit(out, "E-MMIO-NOEFFECT",
                 "this op touches device registers but does not declare `effects device`. A volatile "
                 "access is a SIDE EFFECT — each one happens exactly once, in the order written, and "
                 "the caller must be able to SEE that in the signature",
                 f->kids[1]->tok.line);
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// ★★★ **ISR — 하드웨어가 부르는 진입** (RFC-0042 D5)
//
//   `vector <n> .` 절이 op 을 **인터럽트 핸들러**로 만든다. 새 키워드 **0개** —
//   절 낱말은 키워드가 아니다.
//
//   ISR 은 **보통 op 이 아니다**:
//     · **하드웨어가 부른다** ⇒ Lowent 코드가 부르면 안 된다 (E-ISR-CALLED)
//     · **인자가 없다** (하드웨어 컨텍스트) · **반환이 없다** (`output void`)
//     · **`device` 효과**를 선언한다 — 무엇을 만지는지 시그니처에 보인다
//     · **짧아야 한다**: 디바이스 읽고 → 큐에 게시 → 처리는 태스크에서(RFC-0039 D1c)
//       (그 "짧음" 은 아직 강제하지 않는다 — **못 한다고 말한다.**)
static void ck_isr_walk(low_check_result_t *out, const low_cst_t *nd,
                        const proven_u8str_view_t *isr, proven_size_t nisr) {
    if (!nd) return;
    if (nd->kind == LOW_CST_ATOM && nd->tok.kind == LOW_TOK_IDENT && nd->tok.kw == LOW_KW_NONE)
        for (proven_size_t i = 0; i < nisr; i++)
            if (proven_u8str_view_eq(isr[i], nd->tok.lex)) {
                emit(out, "E-ISR-CALLED",
                     "nobody calls an interrupt handler — the HARDWARE does. Calling it from Lowent "
                     "code runs it on the wrong stack, at the wrong priority, with interrupts in the "
                     "wrong state (RFC-0042 D5)",
                     nd->tok.line);
                break;
            }
    for (proven_size_t i = 0; i < nd->nkids; i++) ck_isr_walk(out, nd->kids[i], isr, nisr);
}

static void ck_isr(low_check_result_t *out, const low_parse_result_t *pr) {
    proven_u8str_view_t isr[32]; proven_size_t nisr = 0;
    for (proven_size_t i = 0; i < pr->nforms && nisr < 32; i++) {
        const low_cst_t *f = pr->forms[i]; ck_cur_form = f;
        if (f->kind != LOW_CST_FORM || f->nkids < 2 || !ck_atom(f->kids[0])) continue;
        low_kw_t kw = f->kids[0]->tok.kw;
        if (kw != LOW_KW_FN && kw != LOW_KW_PROC) continue;
        bool has_vec = false;
        for (proven_size_t j = 2; j < f->nkids; j++)
            if (ck_atom(f->kids[j]) && veq(f->kids[j]->tok.lex, "vector")) has_vec = true;
        if (!has_vec) continue;
        low_op_header_t h = low_op_header(f);
        isr[nisr++] = h.name;
        if (h.np != 0)
            emit(out, "E-ISR-PARAMS",
                 "an interrupt handler takes NO parameters — the HARDWARE calls it, and hardware "
                 "does not pass arguments. Shared state goes through the priority/queue discipline "
                 "(RFC-0039 D1c), not through a parameter list",
                 f->kids[1]->tok.line);
        bool voidout = false, dev = false;
        for (proven_size_t j = h.out_s; j < h.out_e; j++)
            if (ck_atom(f->kids[j]) && veq(f->kids[j]->tok.lex, "void")) voidout = true;
        for (proven_size_t j = h.eff_s; j < h.eff_e; j++)
            if (ck_atom(f->kids[j]) && veq(f->kids[j]->tok.lex, "device")) dev = true;
        if (!voidout)
            emit(out, "E-ISR-OUTPUT",
                 "an interrupt handler returns NOTHING (`output void .`) — there is nobody to return "
                 "to: the hardware resumed you, and the hardware will resume what you interrupted",
                 f->kids[1]->tok.line);
        if (!dev)
            emit(out, "E-ISR-EFFECT",
                 "an interrupt handler must declare `effects device` — it exists because the device "
                 "asked for it", f->kids[1]->tok.line);
    }
    if (!nisr) return;
    // ★ 아무도 ISR 을 **부르면 안 된다** — 하드웨어가 부른다.
    for (proven_size_t i = 0; i < pr->nforms; i++) {
        const low_cst_t *f = pr->forms[i]; ck_cur_form = f;
        if (f->kind != LOW_CST_FORM || !f->nkids || !ck_atom(f->kids[0])) continue;
        low_kw_t kw = f->kids[0]->tok.kw;
        if (kw != LOW_KW_FN && kw != LOW_KW_PROC) continue;
        const low_cst_t *body = f->kids[f->nkids - 1];
        if (body->kind != LOW_CST_BLOCK) continue;
        ck_isr_walk(out, body, isr, nisr);
    }
}

// ★ 이 이름이 **명명 계약**인가(trait 이 아니라). 계약이면 **강제된다**(IR 이 진입 검사를 낸다) —
//   그러니 "구조적 충족은 아직" 이라는 고지를 붙이면 **거짓말**이다.
static bool ck_is_contract(const low_parse_result_t *pr, proven_u8str_view_t name) {
    for (proven_size_t i = 0; i < pr->nforms; i++) {
        const low_cst_t *f = pr->forms[i]; ck_cur_form = f;
        if (f->kind != LOW_CST_FORM || f->nkids < 2 || f->kids[0]->kind != LOW_CST_ATOM) continue;
        if (f->kids[0]->tok.kw != LOW_KW_CONTRACT || f->kids[1]->kind != LOW_CST_ATOM) continue;
        if (proven_u8str_view_eq(f->kids[1]->tok.lex, name)) return true;
    }
    return false;
}
// ★ 이 나무 어딘가에서 `machine.*` 를 쓰는가.
static bool ck_uses_machine(const low_cst_t *nd) {
    if (!nd) return false;
    if (nd->kind == LOW_CST_ATOM) {
        proven_u8str_view_t v = nd->tok.lex;
        return v.size >= 8 && memcmp(v.ptr, "machine.", 8) == 0;
    }
    for (proven_size_t i = 0; i < nd->nkids; i++)
        if (ck_uses_machine(nd->kids[i])) return true;
    return false;
}

// ★★★ RFC-0058 (채택) — **release 와 completion 을 가른다.**
//
//   SPEC-004 §4.8 은 이렇게 적어 뒀다:
//     "owned 값 = **affine 단일소유**(정확히 1회 drop 책임) · 이동(move) = 책임 이전 ·
//      **use-after-move = 컴파일 에러**"
//   그런데 **아무것도 강제되지 않았다.** 누수도, 중복 소비도, 이동 후 사용도 **전부 통과했다.**
//   그리고 `drop` 은 **어휘에도 없어서** "E-IR-UNDEF: 없는 이름" 이라 했다 — 명세에 있는 낱말인데.
//   ⇒ 교훈 1(믿는 코드 + 강제하지 않는 코드) + 교훈 5(오진). 둘 다다.
//
//   ★ 그리고 **가르는 선**(RFC-0058 의 결정):
//     · **release** = 자동 `drop`. **실패하지 않고 대기하지 않는** 최후의 회수.
//       그래서 스코프 끝에서 **조용히** 일어나도 된다 — 실패할 게 없으니까.
//     · **completion** = flush · commit · close handshake. **실패하거나 대기할 수 있다.**
//       ⇒ **명시적이어야 한다.** 암묵적 drop 에 실으면 **그 실패를 아무도 받지 못한다.**
//       이 언어에서 completion 은 **그냥 op 호출**이다(`owned` 를 값으로 받고 `result` 를 낸다) —
//       **새 어휘가 필요 없다.** 어휘는 작고 직교적이어야 한다(P1).
//
//   여기서 강제하는 것: `owned` 지역은 **정확히 한 번** 소비된다.
//     소비 = `drop x .`(release) 또는 **값으로 넘김**(transfer / completion).
typedef struct { proven_u8str_view_t name; int consumed; proven_u32 line; } own_t;

static bool ck_is_owned_decl(const low_cst_t *f) {
    // `var h be owned T …` / `let h be owned T …`
    if (f->kind != LOW_CST_FORM || f->nkids < 3 || f->kids[0]->kind != LOW_CST_ATOM) return false;
    low_kw_t kw = f->kids[0]->tok.kw;
    if (kw != LOW_KW_VAR && kw != LOW_KW_LET) return false;
    for (proven_size_t i = 2; i < f->nkids; i++) {
        if (f->kids[i]->kind != LOW_CST_ATOM) break;
        if (f->kids[i]->tok.kw == LOW_KW_BE) break;
        if (veq(f->kids[i]->tok.lex, "owned")) return true;
    }
    return false;
}

// ★★ affine 의 규칙은 단순하다: **`owned` 의 모든 사용은 이동이다.**
//   읽기만 하고 싶으면 **빌린다**(`ref h` / `mut_ref h`) — 그것이 빌림이 존재하는 이유다.
//   ⇒ 이름이 나타난 **횟수**를 센다. 0 = 누수 · 1 = 옳다 · 2 이상 = 이동 후 사용.
//
//   ★ 처음엔 "호출의 인자로 넘어가는가" 를 보려 했다 — **호출이 form 의 머리에 안 온다.**
//     이 문법은 호출을 괄호로 싸지 않는다(`return g h .` 의 머리는 `return`).
//     **효과 전파에서 이미 겪은 함정이다.** 같은 자리에서 두 번 넘어질 뻔했다.
// ★★ **소비가 무엇인지 정확히 말해야 한다.**
//   처음 쓴 규칙은 "owned 의 모든 언급은 이동" 이었다. 너무 거칠었다:
//   `if eq h 0 .` 는 **읽기**다. 그걸 이동으로 세면 멀쩡한 프로그램을 오진한다(교훈 5).
//
//   소비(consumption)는 셋 중 하나다 — 책임이 **떠나는** 자리:
//     (1) `drop h .`                    → 해제(release)
//     (2) 선언된 op 에 인자로 넘김       → 이전(transfer) · 완결(completion)
//     (3) `return` 에 실림               → 부르는 쪽으로 넘어간다
//   그 외의 언급(비교·조건·`deref`)은 **읽기**이고, `ref h` 는 **빌림**이다 — 둘 다 이동이 아니다.
//
//   ★ 이 분석은 **경로에 둔감하다**(path-insensitive): `if c do drop h . end` 는 소비로 센다.
//     그래서 누수를 **놓칠** 수는 있어도 멀쩡한 코드를 누수라 부르진 않는다.
//     이 한계를 여기 적어 둔다 — **도구는 자기가 확인한 것만 주장한다**(교훈 3).
// ★★★ **필드 장소** `(field <base> <fld>)` 인가 (RFC-0044 §9.1 — 정적으로 이름 붙는 아핀 필드).
//   소유 흐름은 여태 **원자 이름**만 봤다. 필드 단위 부분 이동을 보려면 이 **폼**을 하나의
//   장소로 인식해야 한다. 저자·정규화 괄호는 벗긴다.
static bool ck_is_fieldplace(const low_cst_t *k, proven_u8str_view_t name, proven_u8str_view_t fld) {
    if (!k || !fld.size) return false;
    while (k->kind == LOW_CST_GROUP && k->nkids == 1) k = k->kids[0];
    return k->kind == LOW_CST_FORM && k->nkids >= 3 &&
           k->kids[0]->kind == LOW_CST_ATOM && veq(k->kids[0]->tok.lex, "field") &&
           k->kids[1]->kind == LOW_CST_ATOM && proven_u8str_view_eq(k->kids[1]->tok.lex, name) &&
           k->kids[2]->kind == LOW_CST_ATOM && proven_u8str_view_eq(k->kids[2]->tok.lex, fld);
}
static void ck_own_mentions(const low_cst_t *nd, proven_u8str_view_t name, proven_u8str_view_t fld,
                            proven_size_t *n, proven_u32 *line) {
    if (!nd || nd->kind == LOW_CST_ATOM) return;
    if (fld.size && ck_is_fieldplace(nd, name, fld)) { if (!*n) *line = nd->kids[0]->tok.line; (*n)++; return; }
    for (proven_size_t i = 0; i < nd->nkids; i++) {
        const low_cst_t *k = nd->kids[i];
        if (fld.size) {   // ★ 필드 모드 — 기반 이름이 아니라 **그 필드 장소**만 센다
            if (ck_is_fieldplace(k, name, fld)) { if (!*n) *line = k->kids[0]->tok.line; (*n)++; }
            else if (k->kind != LOW_CST_ATOM) ck_own_mentions(k, name, fld, n, line);
            continue;
        }
        if (k->kind != LOW_CST_ATOM) { ck_own_mentions(k, name, fld, n, line); continue; }
        // `ref h` / `mut_ref h` — 빌림. 이 문법은 괄호를 안 쓴다: `ref` 와 `h` 는 **형제**다.
        if (veq(k->tok.lex, "ref") || veq(k->tok.lex, "mut_ref")) { i++; continue; }
        if (proven_u8str_view_eq(k->tok.lex, name)) { if (!*n) *line = k->tok.line; (*n)++; }
    }
}

static bool ck_own_consumes(const low_cst_t *nd, proven_u8str_view_t name, proven_u8str_view_t fld,
                            const low_opinfo_t *tab, proven_size_t nt, bool in_return) {
    if (!nd || nd->kind == LOW_CST_ATOM) return false;
    bool ret = in_return;
    if (nd->nkids && nd->kids[0]->kind == LOW_CST_ATOM) {
        low_kw_t kw = nd->kids[0]->tok.kw;
        // (4) ★ **다른 이름에 묶는 것도 이동이다** — `var x be T h .` / `set x h .`
        //     가장 흔한 이동 형태인데 첫 판본이 놓쳤다. `check-live` 게이트가 잡았다:
        //     owned 를 두 번 묶는 프로그램이 owned 아닌 판과 **출력이 똑같았다** —
        //     즉 `owned` 가 그 자리에서 **아무것도 바꾸지 않고 있었다.**
        //     ★ 단, RHS 가 **h 그 자체**일 때만이다. `be expr h + 1 .` 은 값을 **읽는** 것이지
        //       자원을 옮기는 게 아니다 — 거기까지 이동이라 부르면 멀쩡한 코드를 오진한다(교훈 5).
        if (kw == LOW_KW_VAR || kw == LOW_KW_LET || kw == LOW_KW_SET) {
            proven_size_t be = nd->nkids;
            for (proven_size_t i = 1; i < nd->nkids; i++)
                if (nd->kids[i]->kind == LOW_CST_ATOM && nd->kids[i]->tok.kw == LOW_KW_BE)
                    { be = i; break; }
            proven_size_t vs = (be < nd->nkids) ? be + 1 : 2;      // `set x h .` 는 be 가 없다
            if (vs + 1 == nd->nkids) {
                if (fld.size) { if (ck_is_fieldplace(nd->kids[vs], name, fld)) return true; }
                else if (nd->kids[vs]->kind == LOW_CST_ATOM &&
                         proven_u8str_view_eq(nd->kids[vs]->tok.lex, name)) return true;
            }
        }
        // (5) ★★ **`send` 는 소유를 이전한다** — RFC-0005 §6.9 B1–B3 이 **의미론은 이미 정했다**
        //     ("send = 배타적 move, 송신측 무효화"). 그런데 그걸 **쓸 문법이 없었다.**
        //     이제 메시지가 인자를 싣는다(DECISION-0011) ⇒ 그 인자는 **떠난다.**
        //     ★ 이것이 LowentDRF.v 가 증명한 격리(소유 이전이 **메시지로만** 일어난다)를
        //       도구가 실제로 주는 유일한 길이다. **증명해 놓고 강제하지 않으면 그것도 거짓말이다.**
        //     ★ `send` 는 form 의 **머리에 오지 않는다** — `var r be u64 send take h to x .` 에서
        //       머리는 `var` 다. 이 문법은 호출을 괄호로 싸지 않는다: `send`·`take` 는 **형제**다.
        //       **호출 전파·효과 전파·소유 이동에서 이미 세 번 넘어진 그 함정이다.** 형제를 훑는다.
        {
            bool sends = false;
            for (proven_size_t i = 0; i < nd->nkids; i++)
                if (nd->kids[i]->kind == LOW_CST_ATOM && nd->kids[i]->tok.kw == LOW_KW_SEND)
                    sends = true;
            if (sends) {
                proven_size_t m = 0; proven_u32 l = 0;
                ck_own_mentions(nd, name, fld, &m, &l);
                if (m) return true;
            }
        }
        if (kw == LOW_KW_DROP || kw == LOW_KW_RETURN) {
            proven_size_t m = 0; proven_u32 l = 0;
            for (proven_size_t i = 1; i < nd->nkids; i++) ck_own_mentions(nd, name, fld, &m, &l);
            (void)l;
            if (kw == LOW_KW_DROP) { m = 0; ck_own_mentions(nd, name, fld, &m, &l); if (m) return true; }
            ret = true;                                  // `return` 안의 언급은 넘겨주는 것이다
        }
    }
    for (proven_size_t i = 0; i < nd->nkids; i++) {
        const low_cst_t *k = nd->kids[i];
        bool isplace = fld.size && ck_is_fieldplace(k, name, fld);
        if (!isplace && k->kind != LOW_CST_ATOM) { if (ck_own_consumes(k, name, fld, tab, nt, ret)) return true; continue; }
        if (!isplace) {
            if (veq(k->tok.lex, "ref") || veq(k->tok.lex, "mut_ref")) { i++; continue; }
            if (fld.size || !proven_u8str_view_eq(k->tok.lex, name)) continue;
        }
        if (ret) return true;                            // (3) return 에 실렸다
        // (2) 선언된 op 의 **인자 자리**인가? 호출은 괄호로 싸지 않는다 —
        //     그래서 "왼쪽 어딘가에 있는 op 이름" 을 찾는다.
        for (proven_size_t j = i; j-- > 0; ) {
            if (nd->kids[j]->kind != LOW_CST_ATOM) break;
            if (ck_is_op(tab, nt, nd->kids[j]->tok.lex)) {
                // ★ 그 op 의 **몇 번째 인자**인지 세어(op 이름 바로 뒤부터) 파라미터 타입을 본다.
                if (ck_param_moves(tab, nt, nd->kids[j]->tok.lex, i - j - 1)) return true;
                break;                              // 복사다 — 이 언급은 이동이 아니다
            }
        }
    }
    return false;
}

// ★★★ **provenance 는 전파되지만 *추출* 은 안 된다** (SPEC-004 §4.5: *"provenance 는 자동
//   전파 — subslice·ref 가 상속"*). region 슬라이스에서 **바이트 하나를 읽어** 나가는 것은
//   안전하다(스칼라 복사다). 새면 안 되는 것은 **자리를 가리키는 값** 자체다.
//   ★ 첫 판이 이 구별을 안 해서 `set t (narrow u64 (index b 0))` 를 거짓 거절했다 —
//     그러면 region 을 **쓸 수가 없다**(할당해 놓고 읽어 나올 수 없으니).
//   ⇒ 스칼라를 내는 머리 아래로는 provenance 가 **끊긴다**. 그 밖의 머리는 **보수적으로 잇는다**
//     (모르는 것은 잇는 쪽이 안전하다 — 놓치면 use-after-free 이고, 과하면 거절일 뿐이다).
static bool ck_scalar_head(proven_u8str_view_t w) {
    static const char *S[] = { "index","len","count","is_some","is_none","is_ok","is_error",
                               "eq","ne","lt","le","gt","ge","add","sub","mul","div","mod",
                               "narrow","cast","bit_cast","and","or","not","neg" };
    for (proven_size_t i = 0; i < sizeof S / sizeof S[0]; i++) if (veq(w, S[i])) return true;
    return false;
}

// ★★★ **`borrow` — 빌린 것의 수명을 스코프로 못 박는다** (2026-07-19, 사용자 결정).
//
//   `lib/pool.low` 의 세대 검사는 **문에서 한 번** 돈다(`bytes`). 그 문을 지나면 나오는 것은
//   **평범한 슬라이스**이고, 그래서 기존 op 이 하나도 안 늘어난다 — 그것이 이 설계의 값이다.
//   ★★ 그런데 그 슬라이스를 **든 채** 블록을 해제하면 아무도 안 잡는다: 해제된 자리는 곧
//     남에게 다시 나가므로 **조용한 데이터 오염**이다. 이 프로젝트가 반복해서
//     *"조용히 틀린 답이 거절보다 나쁘다"* 고 적어 온 바로 그 범주다(fmt 의 전량-아니면-무,
//     read_file 이 자르는 대신 none 을 내는 것 — 전부 같은 문장에서 나왔다).
//   ⇒ 그래서 이것은 *"감수할 9%"* 가 아니라 **닫아야 할 자리**였다. 닫는 법이 이것이다.
//
//   규칙 둘. **둘 다 언어가 라이브러리를 모르는 채로** 성립한다:
//     ① 빌린 이름은 **블록 밖으로 못 나간다**(E-BORROW-ESCAPE) — region 과 같은 기계.
//     ② 식이 `send <P> …` 였다면 블록 안에서 **P 에게 다시 보낼 수 없다**(E-BORROW-EXCL).
//        빌려준 자에게 말을 걸면 빌린 것이 무효가 될 수 있으므로 — 이것이 곧
//        **readers-XOR-writer 를 actor 로 옮긴 것**이다.
//   ☞ ②는 **과엄격이다**(P 에게 읽기만 묻는 것도 막는다). 그런데 과엄격은 안전하고 구멍은
//     안전하지 않다 — 이 세션에서 이미 두 번 같은 저울을 썼다.
// ★★★★ **밖 이름에 대입해도 나가는 것이다** (결함 `borrow-escape-by-assignment`, 2026-09-21).
//   전엔 ①이 `return`/`give` 만 봤다. `set keep v .` · `set keep (subslice v 0 4) .` 는 빌림을 블록
//   밖 이름에 담는데 통과했다. region 의 같은 검사(E-REGION-ESCAPE)가 이미 하던 것을 옮긴다:
//     · 블록 안에서 **선언된** 이름은 안쪽이다(거기 담는 것은 나가는 게 아니다)
//     · 빌린 이름에서 나온 값을 받은 안쪽 이름도 **빌림을 든다**(오염이 번진다)
//     · 스칼라를 내는 식(`len`·`index`·`narrow` …)은 자리를 안 들고 나간다
static bool ck_name_in(const proven_u8str_view_t *v, proven_size_t n, proven_u8str_view_t w);
static bool ck_carries_taint(const low_cst_t *nd, const proven_u8str_view_t *t, proven_size_t nt);
static bool ck_type_word_scalar(proven_u8str_view_t w);
#define CK_BW_NAMES 64
typedef struct { proven_u8str_view_t inner[CK_BW_NAMES]; proven_size_t ninner;
                 proven_u8str_view_t taint[CK_BW_NAMES + 1]; proven_size_t ntaint; } ck_bwctx_t;   // 오염 ⊆ 안쪽 이름 ∪ {빌린 이름}
static void ck_borrow_walk_in(low_check_result_t *out, const low_cst_t *nd,
                              proven_u8str_view_t nm, proven_u8str_view_t lender,
                              bool have_lender, bool inside, ck_bwctx_t *bc);
static void ck_borrow_walk(low_check_result_t *out, const low_cst_t *nd,
                           proven_u8str_view_t nm, proven_u8str_view_t lender,
                           bool have_lender, bool inside) {
    ck_bwctx_t bc = { .ninner = 0, .ntaint = 0 };
    ck_borrow_walk_in(out, nd, nm, lender, have_lender, inside, &bc);
}
static void ck_borrow_walk_in(low_check_result_t *out, const low_cst_t *nd,
                              proven_u8str_view_t nm, proven_u8str_view_t lender,
                              bool have_lender, bool inside, ck_bwctx_t *bc) {
    if (!nd || nd->kind == LOW_CST_ATOM) return;
    bool is_borrow = (nd->kind == LOW_CST_FORM && nd->nkids >= 4 && ck_atom(nd->kids[0]) &&
                      veq(nd->kids[0]->tok.lex, "borrow") && ck_atom(nd->kids[1]));
    if (is_borrow) {
        const low_cst_t *blk = NULL;
        for (proven_size_t i = 0; i < nd->nkids; i++)
            if (nd->kids[i]->kind == LOW_CST_BLOCK) blk = nd->kids[i];
        if (!blk) {
            const low_cst_t *last = nd->kids[nd->nkids - 1];
            if (last && last->kind == LOW_CST_FORM && last->nkids &&
                last->kids[last->nkids - 1]->kind == LOW_CST_BLOCK)
                blk = last->kids[last->nkids - 1];
        }
        if (blk) {
            // 빌려준 자를 찾는다: `be` 뒤의 식이 `send <P> …` 면 P 가 그 자다.
            proven_u8str_view_t lend = { 0 }; bool has = false;
            for (proven_size_t i = 2; i + 1 < nd->nkids; i++) {
                const low_cst_t *v = nd->kids[i];
                while (v && v->kind == LOW_CST_GROUP && v->nkids) v = v->kids[0];
                if (!v) continue;
                proven_u8str_view_t hd = { 0 };
                if (v->kind == LOW_CST_FORM && v->nkids && ck_atom(v->kids[0])) hd = v->kids[0]->tok.lex;
                else if (v->kind == LOW_CST_ATOM) hd = v->tok.lex;
                if (hd.size && veq(hd, "send")) {
                    const low_cst_t *tgt = (v->kind == LOW_CST_FORM && v->nkids > 1) ? v->kids[1]
                                         : ((i + 1 < nd->nkids) ? nd->kids[i + 1] : NULL);
                    if (tgt && ck_atom(tgt)) { lend = tgt->tok.lex; has = true; }
                }
            }
            // ★ 새 빌림 — 제 안쪽 이름과 오염을 새로 센다(빌린 이름 자신이 첫 오염이다)
            ck_bwctx_t sub = { .ninner = 0, .ntaint = 0 };
            sub.taint[sub.ntaint++] = nd->kids[1]->tok.lex;
            for (proven_size_t i = 0; i < blk->nkids; i++)
                ck_borrow_walk_in(out, blk->kids[i], nd->kids[1]->tok.lex, lend, has, true, &sub);
            return;
        }
    }
    if (inside && nd->kind == LOW_CST_FORM && nd->nkids >= 2 && ck_atom(nd->kids[0])) {
        low_kw_t kw = nd->kids[0]->tok.kw;
        // ⓪ 블록 안에서 선언된 이름 — 초기식이 빌림을 들면 그 이름도 빌림을 든다
        if ((kw == LOW_KW_LET || kw == LOW_KW_VAR) && ck_atom(nd->kids[1]) && bc->ninner >= CK_BW_NAMES) {
            emit(out, "E-IR-LIMIT",
                 "too many names declared inside this `borrow` block for the escape checker's table — "
                 "refused rather than checked partly. Split the block",
                 nd->kids[0]->tok.line);
            return;
        }
        if ((kw == LOW_KW_LET || kw == LOW_KW_VAR) && ck_atom(nd->kids[1])) {
            bc->inner[bc->ninner] = nd->kids[1]->tok.lex; bc->ninner += 1;
            proven_size_t be = nd->nkids;
            for (proven_size_t q = 2; q < nd->nkids; q++)
                if (ck_atom(nd->kids[q]) && nd->kids[q]->tok.kw == LOW_KW_BE) { be = q; break; }
            bool scalar = (be == 3 && ck_atom(nd->kids[2]) && ck_type_word_scalar(nd->kids[2]->tok.lex));
            for (proven_size_t q = be + 1; q < nd->nkids && !scalar; q++)
                if (ck_carries_taint(nd->kids[q], bc->taint, bc->ntaint)) {
                    // ★ 오염 표는 안쪽 이름 표 + 빌린 이름 하나를 넘지 않는다(안쪽 이름만 오염된다) — 자리가 늘 있다
                    bc->taint[bc->ntaint] = nd->kids[1]->tok.lex; bc->ntaint += 1;
                    break;
                }
        }
        // ① 빌린 것이 블록 밖으로 나가나 — return · give · 밖 이름(또는 그 칸·원소)에 대입
        bool leaves = (kw == LOW_KW_RETURN || kw == LOW_KW_GIVE);
        if (kw == LOW_KW_SET) {
            const low_cst_t *tg = nd->kids[1];
            for (int hop = 0; tg && hop < 16; hop++) {           // `set (index s i) v` — 밑동 이름
                if (tg->kind == LOW_CST_GROUP) tg = tg->nkids ? tg->kids[0] : NULL;
                else if (tg->kind == LOW_CST_FORM && tg->nkids >= 2) tg = tg->kids[1];
                else break;
            }
            if (tg && ck_atom(tg)) {
                bool is_inner = ck_name_in(bc->inner, bc->ninner, tg->tok.lex) ||
                                proven_u8str_view_eq(tg->tok.lex, nm);
                leaves = !is_inner;
                if (is_inner && !ck_name_in(bc->taint, bc->ntaint, tg->tok.lex))
                    for (proven_size_t j = 2; j < nd->nkids; j++)
                        if (ck_carries_taint(nd->kids[j], bc->taint, bc->ntaint)) { bc->taint[bc->ntaint] = tg->tok.lex; bc->ntaint += 1; break; }
            }
        }
        if (leaves) {
            for (proven_size_t j = 1; j < nd->nkids; j++) {
                if (kw == LOW_KW_SET && j == 1) continue;       // 대상은 값이 아니다
                if (j > 1 && ck_atom(nd->kids[j]) && ck_atom(nd->kids[j - 1]) && ck_scalar_head(nd->kids[j - 1]->tok.lex))
                    continue;
                if (ck_carries_taint(nd->kids[j], bc->taint, bc->ntaint)) {
                    emit(out, "E-BORROW-ESCAPE",
                         "this borrow is being carried OUT of its `borrow` block. The borrow lives "
                         "for the block and not one statement longer — that is what makes it safe to "
                         "hand out a plain slice at all. Storing it in a name declared OUTSIDE the "
                         "block (or in that name's field or element) carries it out just as `return` "
                         "does. Copy what you need, or widen the block",
                         nd->kids[0]->tok.line);
                    return;
                }
            }
        }
        // ② 빌려준 자에게 다시 말을 거나
        if (have_lender && lender.size) {
            for (proven_size_t j = 0; j + 1 < nd->nkids; j++)
                if (ck_atom(nd->kids[j]) && veq(nd->kids[j]->tok.lex, "send") &&
                    ck_atom(nd->kids[j + 1]) &&
                    proven_u8str_view_eq(nd->kids[j + 1]->tok.lex, lender)) {
                    emit(out, "E-BORROW-EXCL",
                         "you are holding a borrow from this actor and sending it another message. "
                         "Anything it does could invalidate what it lent you — that is "
                         "readers-XOR-writer, moved from locals to actors. Finish with the borrow "
                         "first (close the block), then talk to it again",
                         nd->kids[j]->tok.line);
                    return;
                }
        }
    }
    for (proven_size_t i = 0; i < nd->nkids; i++)
        ck_borrow_walk_in(out, nd->kids[i], nm, lender, have_lender, inside, bc);
}

// ★★★ **region 에서 할당한 것은 그 블록을 벗어날 수 없다** (SPEC-004 §4.5 ESC · 2026-07-19).
//
//   어휘 `region` 블록은 끝에서 범프 커서를 **되돌린다**(범위 기반 일괄 free — 명세의 선택).
//   그래서 그 안에서 할당한 슬라이스가 밖으로 새면 **되돌리기가 곧 use-after-free** 다.
//   ★★ 이 검사가 없으면 이 기능은 **안전한 것(절대 free 안 함)을 위험한 것으로 바꾼다.**
//     `free` 를 안 넣은 이유가 그것이었고, 넣는 순간 값을 치러야 한다 — 이것이 그 값이다.
//
//   ★ 새는 길 둘만 본다(이 코어에서 관측 가능한 전부):
//     ① 블록 **밖**의 이름에 대입 — `set keep <region 슬라이스>` / `var keep be …`
//     ② `return` 에 실림
//   ☞ SPEC-004 는 세 지점(return · field-store · out-param write)을 말한다. field-store 는
//     region 슬라이스를 필드에 넣는 길인데 **struct 필드에 `mut slice` 를 못 넣으므로**
//     (A6 실측) 현재 도달 불가다 — 그 표면이 생기면 여기도 함께 늘어야 한다.
// ★ 이름이 이 부분트리 어딘가에 있나 — `set keep (some_value g)` 처럼 **괄호 안**에 있을 수
//   있으므로 직접 자식만 보면 놓친다(첫 판이 그렇게 놓쳤다).
// ★★★★ RFC-0112 D4·D5 (WO-0212) — 영역 검사를 **오염(taint)** 으로 다시 세운다.
//
//   전엔 «블록 안에서 선언된 이름» 이면 무엇이든 영역의 자리를 든 것으로 쳤다. 그래서
//     ① `let n be u64 5 . set keep n .` 이 **거짓 양성**이었다(스칼라는 자리를 안 든다 — 실측) 이고
//     ② `set (field h store) b .` 는 대상이 이름이 아니라 폼이라 **아예 안 봤다**(F5 — VM 55 / 네이티브 77).
//   이제 이름은 **그 값이 영역의 바이트를 들고 있을 때만** 오염된다: 타입이 스칼라가 아니고, 초기식이
//   `alloc_bytes`·`stack_new` 이거나 이미 오염된 이름을 싣는다(스칼라를 내는 머리 — `len`·`index`·… —
//   를 거치면 싣지 않는다).
//   ★ 그리고 두 규칙을 더한다:
//     · `E-ALLOC-NESTED` — 같은 뿌리의 영역이 안쪽에 열린 동안 **바깥 이름으로 깎는 것**(F4). 뿌리마다
//       커서가 하나라 안쪽 `end` 가 그 바이트를 걷는다. 영역 매개변수와 권한(`cap allocator`·`cap heap`)도
//       같은 뿌리의 블록이 열려 있으면 같은 이유로 거절한다.
//     · `E-ALLOC-OUTLIVES` — 오염된 값을 **영역 밖에서 태어난 actor** 에게 `send` 로 건네는 것(F6).
//       actor 가 그것을 저장하는지 번역 시점에 알 수 없으므로 보수적으로 막는다.
#define CK_RG_MAX 8
static bool ck_is_region_param(const low_cst_t *f, proven_u8str_view_t name);
#define CK_RG_NAMES 64
typedef struct {
    proven_u8str_view_t rgn[CK_RG_MAX]; proven_u8 root[CK_RG_MAX]; proven_size_t nrgn;
    const low_cst_t *opform;
} ck_rgctx_t;
static bool ck_name_in(const proven_u8str_view_t *v, proven_size_t n, proven_u8str_view_t w) {
    for (proven_size_t i = 0; i < n; i++) if (proven_u8str_view_eq(v[i], w)) return true;
    return false;
}
// 오염된 이름을 싣는가 — 평평한 폼에서도: 스칼라 머리 **바로 뒤**의 이름은 싣지 않는다.
// ★★★★ 후속 ③ R1 (2026-09-22) — **칸마다 오염**. 부름의 결과가 구조체이고 피호출자의 요약이 칸마다 출처를 말하면,
//   오염된 칸만 적는다(`r.c` 는 영역 바이트, `r.h` 는 영역 밖 입력). `(field r h)` 는 그 칸이 오염됐을 때만 들고 나간다.
//   이름 `r` 을 통째로 쓰면 칸 하나라도 오염됐으면 오염이다(보수적).
struct ck_r1_sum_s; static const low_opinfo_t *ck_r1_tab; static proven_size_t ck_r1_nt;
static const low_opinfo_t *ck_find_callee(const low_opinfo_t *tab, proven_size_t nt, proven_u8str_view_t callee);
static bool ck_r1_flow_mask(const low_opinfo_t *op, unsigned long long *all);
#define CK_FT_MAX 64
static proven_u8str_view_t ck_ft_name[CK_FT_MAX], ck_ft_fld[CK_FT_MAX]; static proven_size_t ck_ft_n; static bool ck_ft_full;
static bool ck_ft_has_name(proven_u8str_view_t w) {
    for (proven_size_t i = 0; i < ck_ft_n; i++) if (proven_u8str_view_eq(ck_ft_name[i], w)) return true;
    return false;
}
static bool ck_ft_has(proven_u8str_view_t w, proven_u8str_view_t fl) {
    for (proven_size_t i = 0; i < ck_ft_n; i++)
        if (proven_u8str_view_eq(ck_ft_name[i], w) && proven_u8str_view_eq(ck_ft_fld[i], fl)) return true;
    return false;
}
static void ck_ft_add(proven_u8str_view_t w, proven_u8str_view_t fl) {
    if (ck_ft_has(w, fl)) return;
    if (ck_ft_n >= CK_FT_MAX) { ck_ft_full = true; return; }       // 넘침은 부른 쪽이 E-IR-LIMIT 로 말한다
    ck_ft_name[ck_ft_n] = w; ck_ft_fld[ck_ft_n] = fl; ck_ft_n += 1;
}
static bool ck_carries_taint(const low_cst_t *nd, const proven_u8str_view_t *t, proven_size_t nt) {
    if (!nd) return false;
    if (nd->kind == LOW_CST_ATOM) return ck_name_in(t, nt, nd->tok.lex) || ck_ft_has_name(nd->tok.lex);
    if (nd->kind == LOW_CST_FORM && nd->nkids >= 3 && ck_atom(nd->kids[0]) && veq(nd->kids[0]->tok.lex, "field") &&
        ck_atom(nd->kids[1]) && ck_atom(nd->kids[2]) && !ck_name_in(t, nt, nd->kids[1]->tok.lex) &&
        ck_ft_has_name(nd->kids[1]->tok.lex))
        return ck_ft_has(nd->kids[1]->tok.lex, nd->kids[2]->tok.lex);
    // ★ 후속 ③ R1 — 요약을 아는 op 의 부름은 **결과로 흐르는 인자만** 오염을 옮긴다(`pick_first a <영역 바이트>` 는 `a` 만).
    if (ck_r1_tab && nd->kind == LOW_CST_FORM && nd->nkids >= 2 && ck_atom(nd->kids[0]) && nd->kids[0]->tok.kw == LOW_KW_NONE &&
        !ck_scalar_head(nd->kids[0]->tok.lex)) {
        const low_opinfo_t *op = ck_find_callee(ck_r1_tab, ck_r1_nt, nd->kids[0]->tok.lex);
        unsigned long long all = 0;
        if (op && ck_r1_flow_mask(op, &all)) {
            for (proven_size_t q = 1; q < nd->nkids && q - 1 < 64; q++)
                if ((all >> (q - 1) & 1ull) && ck_carries_taint(nd->kids[q], t, nt)) return true;
            return false;
        }
    }
    if (nd->nkids && ck_atom(nd->kids[0]) && ck_scalar_head(nd->kids[0]->tok.lex)) return false;
    for (proven_size_t i = 0; i < nd->nkids; i++) {
        if (i > 0 && ck_atom(nd->kids[i]) && ck_atom(nd->kids[i - 1]) && ck_scalar_head(nd->kids[i - 1]->tok.lex))
            continue;
        if (ck_carries_taint(nd->kids[i], t, nt)) return true;
    }
    return false;
}
static bool ck_type_word_scalar(proven_u8str_view_t w) {
    static const char *SC[] = { "u8","u16","u32","u64","i8","i16","i32","i64","bool","f32","f64" };
    for (proven_size_t i = 0; i < sizeof SC / sizeof SC[0]; i++) if (veq(w, SC[i])) return true;
    return false;
}
// 이 폼 어디에 `alloc_bytes`·`stack_new` 가 있나(영역의 바이트를 새로 받는 자리)
static bool ck_has_fresh_bytes(const low_cst_t *nd) {
    if (!nd) return false;
    if (nd->kind == LOW_CST_ATOM) return veq(nd->tok.lex, "alloc_bytes") || veq(nd->tok.lex, "stack_new");
    for (proven_size_t i = 0; i < nd->nkids; i++) if (ck_has_fresh_bytes(nd->kids[i])) return true;
    return false;
}
// 권한 입력의 종류: 1 = cap heap, 0 = cap allocator, -1 = 아님
static int ck_cap_root_of(const low_cst_t *f, proven_u8str_view_t name) {
    if (ck_param_cap_kind(f, name, "heap")) return 1;
    if (ck_param_cap_kind(f, name, "allocator")) return 0;
    return -1;
}
static void ck_nested_scan(low_check_result_t *out, const low_cst_t *nd, const ck_rgctx_t *rc) {
    if (!nd || nd->kind == LOW_CST_ATOM || !rc->nrgn) return;
    for (proven_size_t j = 0; j + 1 < nd->nkids; j++) {
        if (!ck_atom(nd->kids[j]) || !veq(nd->kids[j]->tok.lex, "alloc_bytes") || !ck_atom(nd->kids[j + 1])) continue;
        proven_u8str_view_t x = nd->kids[j + 1]->tok.lex;
        int root = -1; proven_size_t k = rc->nrgn;       // k = 그 이름의 영역 깊이(없으면 nrgn)
        for (proven_size_t q = rc->nrgn; q-- > 0; )
            if (proven_u8str_view_eq(rc->rgn[q], x)) { k = q; root = rc->root[q]; break; }
        if (k == rc->nrgn) {
            root = ck_cap_root_of(rc->opform, x);
            if (root < 0 && ck_is_region_param(rc->opform, x)) root = 0;
        }
        if (root < 0) continue;
        bool nested = false;
        for (proven_size_t m = (k == rc->nrgn ? 0 : k + 1); m < rc->nrgn; m++)
            if (rc->root[m] == (proven_u8)root) nested = true;
        if (nested)
            emit(out, "E-ALLOC-NESTED",
                 "this allocation names an OUTER source while a region of the SAME root is open inside it. "
                 "Each root (the fixed window, the heap) has ONE cursor, so the inner region's `end` rewinds "
                 "past these bytes and hands them to the next allocation — the value would silently change "
                 "under you (RFC-0112 D4). Allocate from the innermost region, move this allocation outside "
                 "the inner block, or open the inner region on the other root",
                 nd->kids[j]->tok.line);
    }
}
// ★★★★ 후속 ③ R1 — op 요약: 결과(와 결과 구조체의 칸마다)가 **어느 입력에서 오나**. `return` 식들을 읽는다.
//   식이 입력 아닌 지역 이름을 쓰면 «모른다» → 모든 입력(지금까지처럼 뭉뚱그림). 부름의 인자로 쓰인 입력은 결과로 흐른다고 본다.
#define CK_R1_FLD 12
typedef struct { unsigned long long all; bool struct_ok; proven_size_t nf;
                 proven_u8str_view_t fname[CK_R1_FLD]; unsigned long long fmask[CK_R1_FLD]; } ck_r1_sum_t;
static unsigned long long ck_r1_expr_mask(const low_cst_t *nd, const low_op_header_t *h, bool head, bool *unknown) {
    if (!nd) return 0;
    if (nd->kind == LOW_CST_ATOM) {
        if (head || nd->tok.kind != LOW_TOK_IDENT || nd->tok.kw != LOW_KW_NONE) return 0;
        for (proven_size_t q = 0; q < h->np && q < 64; q++) if (proven_u8str_view_eq(h->p[q].name, nd->tok.lex)) return 1ull << q;
        *unknown = true; return 0;                                 // 입력이 아닌 이름 — 모른다
    }
    unsigned long long m = 0;
    for (proven_size_t i = 0; i < nd->nkids; i++)
        m |= ck_r1_expr_mask(nd->kids[i], h, nd->kind == LOW_CST_FORM && i == 0, unknown);
    return m;
}
static const low_cst_t *ck_r1_make_block(const low_cst_t *e) {
    while (e && e->kind == LOW_CST_GROUP && e->nkids == 1) e = e->kids[0];
    if (!e || e->kind != LOW_CST_FORM || e->nkids < 2 || !ck_atom(e->kids[0]) || e->kids[0]->tok.kw != LOW_KW_LIT) return NULL;
    for (proven_size_t i = 1; i < e->nkids; i++) {
        const low_cst_t *k = e->kids[i];
        if (k->kind == LOW_CST_BLOCK) return k;
        if (k->kind == LOW_CST_FORM && k->nkids && k->kids[k->nkids - 1]->kind == LOW_CST_BLOCK) return k->kids[k->nkids - 1];
    }
    return NULL;
}
static void ck_r1_scan(const low_cst_t *nd, const low_op_header_t *h, ck_r1_sum_t *s, bool *seen_nonmake, bool *seen_make) {
    if (!nd || nd->kind == LOW_CST_ATOM) return;
    if (nd->kind == LOW_CST_FORM && nd->nkids >= 2 && ck_atom(nd->kids[0]) &&
        (nd->kids[0]->tok.kw == LOW_KW_RETURN || nd->kids[0]->tok.kw == LOW_KW_GIVE)) {
        const low_cst_t *mb = (nd->nkids == 2) ? ck_r1_make_block(nd->kids[1]) : NULL;
        if (mb) {
            *seen_make = true;
            for (proven_size_t i = 0; i < mb->nkids; i++) {
                const low_cst_t *fe = mb->kids[i];
                if (fe->kind != LOW_CST_FORM || fe->nkids < 2 || !ck_atom(fe->kids[0])) { s->struct_ok = false; continue; }
                bool unk = false; unsigned long long m = 0;
                for (proven_size_t j = 1; j < fe->nkids; j++) m |= ck_r1_expr_mask(fe->kids[j], h, false, &unk);
                if (unk) m = ~0ull;
                s->all |= m;
                proven_size_t k = 0;
                while (k < s->nf && !proven_u8str_view_eq(s->fname[k], fe->kids[0]->tok.lex)) k++;
                if (k == s->nf) { if (s->nf >= CK_R1_FLD) { s->struct_ok = false; continue; } s->fname[s->nf] = fe->kids[0]->tok.lex; s->fmask[s->nf] = 0; s->nf++; }
                s->fmask[k] |= m;
            }
        } else {
            *seen_nonmake = true;
            bool unk = false; unsigned long long m = 0;
            for (proven_size_t j = 1; j < nd->nkids; j++) m |= ck_r1_expr_mask(nd->kids[j], h, false, &unk);
            s->all |= unk ? ~0ull : m;
        }
        return;
    }
    for (proven_size_t i = 0; i < nd->nkids; i++) ck_r1_scan(nd->kids[i], h, s, seen_nonmake, seen_make);
}
// 요약을 못 내면 false(부른 쪽은 지금처럼 뭉뚱그린다)
static bool ck_r1_summary(const low_opinfo_t *op, ck_r1_sum_t *s);
static bool ck_r1_flow_mask(const low_opinfo_t *op, unsigned long long *all) {
    ck_r1_sum_t s;
    if (!ck_r1_summary(op, &s)) return false;
    *all = s.all;
    return true;
}
static bool ck_r1_summary(const low_opinfo_t *op, ck_r1_sum_t *s) {
    memset(s, 0, sizeof *s);
    if (!op || !op->body) return false;
    low_op_header_t h = low_op_header(op->form);
    if (h.np > 64) return false;
    s->struct_ok = true;
    bool nonmake = false, mk = false;
    ck_r1_scan(op->body, &h, s, &nonmake, &mk);
    if (nonmake || !mk) s->struct_ok = false;                        // 칸마다는 모든 return 이 make 일 때만
    return true;
}
static void ck_region_walk(low_check_result_t *out, const low_cst_t *nd, ck_rgctx_t *rc,
                           proven_u8str_view_t *inner, proven_size_t *ninner,
                           proven_u8str_view_t *taint, proven_size_t *ntaint, bool inside) {
    if (!nd || nd->kind == LOW_CST_ATOM) return;
    bool is_region = (nd->kind == LOW_CST_FORM && nd->nkids >= 3 && ck_atom(nd->kids[0]) &&
                      veq(nd->kids[0]->tok.lex, "region"));
    if (is_region && rc->nrgn < CK_RG_MAX) {
        // 블록을 찾는다(직접 자식 또는 `<종류> do…end` 안)
        const low_cst_t *blk = NULL, *kind = nd->kids[2];
        for (proven_size_t i = 0; i < nd->nkids; i++)
            if (nd->kids[i]->kind == LOW_CST_BLOCK) blk = nd->kids[i];
        if (!blk) {
            const low_cst_t *last = nd->kids[nd->nkids - 1];
            if (last && last->kind == LOW_CST_FORM && last->nkids &&
                last->kids[last->nkids - 1]->kind == LOW_CST_BLOCK)
                blk = last->kids[last->nkids - 1];
        }
        if (kind->kind == LOW_CST_FORM && kind->nkids) kind = kind->kids[0];
        if (blk && ck_atom(nd->kids[1])) {
            proven_u8str_view_t sub[CK_RG_NAMES], subt[CK_RG_NAMES]; proven_size_t nsub = 0, nsubt = 0;
            rc->rgn[rc->nrgn] = nd->kids[1]->tok.lex;
            rc->root[rc->nrgn] = (ck_atom(kind) && veq(kind->tok.lex, "heap")) ? 1 : 0;
            rc->nrgn++;
            for (proven_size_t i = 0; i < blk->nkids; i++)
                ck_region_walk(out, blk->kids[i], rc, sub, &nsub, subt, &nsubt, true);
            rc->nrgn--;
            return;
        }
    }
    if (nd->kind == LOW_CST_FORM) ck_nested_scan(out, nd, rc);
    if (inside && nd->kind == LOW_CST_FORM && nd->nkids >= 2 && ck_atom(nd->kids[0])) {
        low_kw_t kw = nd->kids[0]->tok.kw;
        // ① 이 블록 안에서 **선언된** 이름 — 오염은 타입과 초기식이 정한다.
        if ((kw == LOW_KW_LET || kw == LOW_KW_VAR) && ck_atom(nd->kids[1]) && *ninner < CK_RG_NAMES) {
            inner[(*ninner)++] = nd->kids[1]->tok.lex;
            proven_size_t be = nd->nkids;
            for (proven_size_t q = 2; q < nd->nkids; q++)
                if (ck_atom(nd->kids[q]) && nd->kids[q]->tok.kw == LOW_KW_BE) { be = q; break; }
            bool scalar = (be == 3 && ck_atom(nd->kids[2]) && ck_type_word_scalar(nd->kids[2]->tok.lex));
            bool fresh = false;
            // ★ 후속 ③ R1 — 초기식이 **요약을 아는 op 의 부름 하나**면, 결과로 흐르는 인자만 본다(칸마다면 칸마다).
            bool r1_done = false;
            if (be + 2 == nd->nkids && ck_r1_tab) {
                const low_cst_t *call = nd->kids[be + 1];
                while (call && call->kind == LOW_CST_GROUP && call->nkids == 1) call = call->kids[0];
                if (call && call->kind == LOW_CST_FORM && call->nkids >= 2 && ck_atom(call->kids[0]) &&
                    call->kids[0]->tok.kw == LOW_KW_NONE && !ck_has_fresh_bytes(call)) {
                    const low_opinfo_t *op = ck_find_callee(ck_r1_tab, ck_r1_nt, call->kids[0]->tok.lex);
                    ck_r1_sum_t sm;
                    if (op && ck_r1_summary(op, &sm)) {
                        unsigned long long targ = 0;
                        for (proven_size_t q = 1; q < call->nkids && q - 1 < 64; q++)
                            if (ck_carries_taint(call->kids[q], taint, *ntaint)) targ |= 1ull << (q - 1);
                        if (sm.struct_ok && sm.nf) {
                            for (proven_size_t k = 0; k < sm.nf; k++)
                                if (sm.fmask[k] & targ) ck_ft_add(nd->kids[1]->tok.lex, sm.fname[k]);
                        } else if (sm.all & targ) fresh = true;
                        r1_done = true;
                    }
                }
            }
            for (proven_size_t q = be; q < nd->nkids && !r1_done; q++) {
                if (ck_has_fresh_bytes(nd->kids[q])) fresh = true;
                if (q > be && ck_atom(nd->kids[q]) && ck_atom(nd->kids[q - 1]) && ck_scalar_head(nd->kids[q - 1]->tok.lex))
                    continue;
                if (ck_carries_taint(nd->kids[q], taint, *ntaint)) fresh = true;
            }
            if (!scalar && fresh && *ntaint < CK_RG_NAMES) taint[(*ntaint)++] = nd->kids[1]->tok.lex;
        }
        // ② 나가는 자리: return · 바깥 이름(또는 바깥 이름의 칸)에 대입
        bool leaves = (kw == LOW_KW_RETURN || kw == LOW_KW_GIVE);
        if (kw == LOW_KW_SET) {
            const low_cst_t *tg = nd->kids[1];
            // `set (field h store) v` · `set (index s i) v` — 대상의 **밑동** 이름을 본다
            // (괄호는 GROUP 한 겹이다 — 벗긴다)
            for (int hop = 0; tg && hop < 16; hop++) {
                if (tg->kind == LOW_CST_GROUP) tg = tg->nkids ? tg->kids[0] : NULL;
                else if (tg->kind == LOW_CST_FORM && tg->nkids >= 2) tg = tg->kids[1];
                else break;
            }
            if (tg && ck_atom(tg)) {
                bool is_inner = ck_name_in(inner, *ninner, tg->tok.lex);
                leaves = !is_inner;
                if (is_inner && *ntaint < CK_RG_NAMES && !ck_name_in(taint, *ntaint, tg->tok.lex))
                    for (proven_size_t j = 2; j < nd->nkids; j++)
                        if (ck_carries_taint(nd->kids[j], taint, *ntaint)) { taint[(*ntaint)++] = tg->tok.lex; break; }
            }
        }
        if (leaves) {
            for (proven_size_t j = 1; j < nd->nkids; j++) {
                if (kw == LOW_KW_SET && j == 1) continue;       // 대상은 값이 아니다
                if (j > 1 && ck_atom(nd->kids[j]) && ck_atom(nd->kids[j - 1]) && ck_scalar_head(nd->kids[j - 1]->tok.lex))
                    continue;
                if (ck_carries_taint(nd->kids[j], taint, *ntaint)) {
                    emit(out, "E-REGION-ESCAPE",
                         "this value was allocated inside a `region` block and is being carried "
                         "OUT of it. The block RECLAIMS its memory at `end` (SPEC-004 §4.5: the "
                         "lifetime IS the scope — that is why there is no `free`), so the value "
                         "would point at bytes the next allocation hands to somebody else. "
                         "Copy what you need into memory that outlives the block, or move the "
                         "block outward so it covers every use",
                         nd->kids[0]->tok.line);
                    return;
                }
            }
        }
        // ③ 영역 밖에서 태어난 actor 에게 영역의 바이트를 건넨다
        for (proven_size_t j = 0; j + 1 < nd->nkids; j++) {
            if (!ck_atom(nd->kids[j]) || !veq(nd->kids[j]->tok.lex, "send") || !ck_atom(nd->kids[j + 1])) continue;
            if (ck_name_in(inner, *ninner, nd->kids[j + 1]->tok.lex)) continue;
            for (proven_size_t q = j + 2; q < nd->nkids; q++)
                if (ck_carries_taint(nd->kids[q], taint, *ntaint)) {
                    emit(out, "E-ALLOC-OUTLIVES",
                         "this hands bytes from a `region` block to an actor that was born OUTSIDE the block. "
                         "The actor outlives the region, and nothing here can see whether it keeps the slice — "
                         "if it does, it will read bytes the region's `end` gave to somebody else (RFC-0112 D5). "
                         "Create the actor inside the region, or give it memory that outlives it",
                         nd->kids[j]->tok.line);
                    return;
                }
        }
    }
    for (proven_size_t i = 0; i < nd->nkids; i++)
        ck_region_walk(out, nd->kids[i], rc, inner, ninner, taint, ntaint, inside);
}

// ★★ **해제(release)와 완결(completion)은 다르다** — RFC-0058.
//
//   해제는 **전면적이고 중단하지 않는다**: 언제나 성공하고, 기다리지 않는다.
//     그래서 스코프 끝에서 **조용히** 일어나도 된다(SPEC-004 §4.8 ① 자동 drop).
//   완결은 flush · commit · close 다: **실패할 수 있고, 기다릴 수 있다.**
//     그래서 조용히 일어나면 **안 된다** — 암묵적 drop 은 그 실패를 건네줄 자리가 없다.
//
//   ★ 처음 판본은 소비되지 않은 owned 를 전부 `E-OWN-LEAK` 이라 불렀다. **오진이다**(교훈 5):
//     명세가 자동 drop 을 **기본**이라고 못 박아 두었다. 도구가 명세를 어긴 것이다.
//
//   그러면 완결 타입을 무엇으로 아는가? **프로그램 자신의 선언으로.**
//     `owned T` 를 값으로 받고 `result` 를 돌려주는 op 가 있다 = "이걸 끝내는 건 실패할 수 있다"
//     고 이미 선언한 것이다. 그런 T 를 조용히 drop 하면 **그 실패를 삼킨다.**
//     새 어휘는 필요 없다(P1: 어휘는 작고 직교하게).
static bool ck_type_needs_completion_d(const low_parse_result_t *pr, proven_u8str_view_t ty, int depth);
static proven_size_t ck_struct_owned_field_types(const low_parse_result_t *pr, proven_u8str_view_t ty,
                                                 proven_u8str_view_t *outt, proven_size_t cap);
static bool ck_type_needs_completion(const low_parse_result_t *pr, proven_u8str_view_t ty) {
    return ck_type_needs_completion_d(pr, ty, 0);
}
// ★★★★ **자원을 품은 것은 그 자체가 자원이다** (2026-08-15, RFC-0075 Stage 4 Task 3B).
//
//   여기까지 이 물음은 *"이 타입을 `owned` 로 받고 `result` 를 내는 op 이 있는가"* **하나**였다.
//   그래서 `duo { a owned conn . b owned conn }` 를 만들고 **하나도 닫지 않아도** 통과했다:
//   `conn` 은 자원인데 `duo` 는 아니었고, `duo` 를 놓으면 그 안의 둘이 **조용히** 사라졌다.
//   (실측: 지역 `owned conn` 은 빨강, 같은 것을 필드에 넣으면 초록.)
//
//   ★ 범위를 좁게 둔다: **`owned` 로 선언한 필드만** 본다. 필드에 `owned` 를 적는 것이
//     *"이 struct 가 저 자원을 소유한다"* 는 선언이고, 안 적은 필드까지 물면 값을 **읽기만**
//     하는 struct 가 자원이 된다.
//   ★★ 깊이 상한을 둔다 — 서로를 품는 struct 에서 안 돌아야 한다(그 순환 자체는 별개의 진단감).
static bool ck_type_needs_completion_d(const low_parse_result_t *pr, proven_u8str_view_t ty, int depth) {
    for (proven_size_t i = 0; i < pr->nforms; i++) {
        const low_cst_t *f = pr->forms[i]; ck_cur_form = f;
        if (f->kind != LOW_CST_FORM || f->nkids < 2 || f->kids[0]->kind != LOW_CST_ATOM) continue;
        low_kw_t kw = f->kids[0]->tok.kw;
        if (kw != LOW_KW_FN && kw != LOW_KW_PROC) continue;
        bool takes = false, fallible = false;
        for (proven_size_t j = 0; j < f->nkids; j++) {
            if (f->kids[j]->kind != LOW_CST_ATOM) continue;
            if (veq(f->kids[j]->tok.lex, "input")) {
                proven_size_t e = j + 1; bool owned = false;
                while (e < f->nkids && f->kids[e]->kind == LOW_CST_ATOM &&
                       !low_is_clause_word(f->kids[e]->tok.lex)) {
                    if (veq(f->kids[e]->tok.lex, "owned")) owned = true;
                    else if (owned && proven_u8str_view_eq(f->kids[e]->tok.lex, ty)) takes = true;
                    e++;
                }
            } else if (veq(f->kids[j]->tok.lex, "output")) {
                if (j + 1 < f->nkids && f->kids[j + 1]->kind == LOW_CST_ATOM &&
                    veq(f->kids[j + 1]->tok.lex, "result")) fallible = true;
            }
        }
        if (takes && fallible) return true;
    }
    // ★ 직접 선언된 완결 op 이 없으면 — **품은 것**을 본다.
    if (depth < 4) {
        proven_u8str_view_t ft[8];
        proven_size_t nf = ck_struct_owned_field_types(pr, ty, ft, 8);
        for (proven_size_t q = 0; q < nf; q++)
            if (!proven_u8str_view_eq(ft[q], ty) && ck_type_needs_completion_d(pr, ft[q], depth + 1))
                return true;
    }
    return false;
}

// ★★★ **소유 흐름 분석** (RFC-0044 §9.3 JOIN1 · §9.4 FD2). 전엔 **경로에 둔감한 카운터**였다
//   (전체 본문의 소비 횟수: 0=누수·1=OK·2+=이동후사용) — **조건부 소비를 오진**했다: 한 가지에서 소비하고
//   다른 가지가 흘려보내면, 카운터는 1 로 세어 **누수를 놓치고**, `if c do return sink h . end drop h .`
//   같은 정상 코드는 2 로 세어 **오탐**했다. 이제 상태(LIVE/CONSUMED)를 경로별로 나르고 합류점에서 일치를 본다.
//   ★ 핵심: **발산 분기는 합류에 참여하지 않는다**(§9.4 FD2 — return/give/break/continue/fail/panic·guard 발산).
//     그래서 vm_own.low 의 `if eq h 0 . do return error . end`(발산) 은 살아남는다.
typedef enum { OWN_LIVE, OWN_CONSUMED } ck_own_state_t;

static ck_own_state_t ck_own_flow(low_check_result_t *out, const low_parse_result_t *pr,
                                  const low_cst_t **kids, proven_size_t nkids,
                                  proven_u8str_view_t name, proven_u8str_view_t fld,
                                  const low_opinfo_t *tab, proven_size_t nt,
                                  ck_own_state_t st, proven_u32 decl_line, bool *diverges) {
    *diverges = false;
    for (proven_size_t j = 0; j < nkids; j++) {
        const low_cst_t *s = kids[j];
        proven_size_t m = 0; proven_u32 l = decl_line;
        ck_own_mentions(s, name, fld, &m, &l);
        // ★★★ **`set <name> <expr>` 는 *재초기화* 다 — 사용이 아니다** (2026-07-19, lib/out.low 이 찾았다).
        //   전엔 소비된 이름이 **언급되기만 해도** E-OWN-MOVED 였다. 그런데 이동된 자리에
        //   **새 값을 넣는 것**은 use-after-move 가 아니라 그 자리를 다시 살리는 것이다
        //   (Rust 도 `x = new` 를 재초기화로 본다 — 이동은 *값* 이 떠난 것이지 *자리* 가 죽은 게 아니다).
        //   ⇒ 그래서 **소유값을 루프로 이어받는 모양이 아예 표현 불가능**했다:
        //       `let r be ... step p .`  `set p (ok_value r) .`   ← 여기서 거짓 거절
        //     그리고 그것이 버퍼링 라이터의 모양이다(RFC-0069 §3) — 핸들을 여러 번 쓰고 끝낸다.
        //   ★ **자기참조 재대입은 여전히 사용이다**: `set p (f p)` 는 오른쪽이 p 를 읽으므로 거절된다.
        //   ★ **필드 대입은 재초기화가 아니다**: `set (field p x) 0` 은 자리가 아니라 *그 안* 을
        //     만지는 것이라 p 가 살아 있어야 한다 — 대상이 **맨 이름**일 때만 재초기화다.
        bool reinit = false;
        if (s->kind == LOW_CST_FORM && s->nkids >= 3 && s->kids[0]->kind == LOW_CST_ATOM &&
            s->kids[0]->tok.kw == LOW_KW_SET &&
            s->kids[1]->kind == LOW_CST_ATOM &&
            proven_u8str_view_eq(s->kids[1]->tok.lex, name)) {
            proven_size_t rm = 0; proven_u32 rl = decl_line;
            for (proven_size_t q = 2; q < s->nkids; q++) ck_own_mentions(s->kids[q], name, fld, &rm, &rl);
            if (!rm) reinit = true;
        }
        if (st == OWN_CONSUMED && m && !reinit)
            emit(out, "E-OWN-MOVED",
                 "this `owned` value was already MOVED (consumed) — using it again is "
                 "use-after-move, which SPEC-004 §4.8 has always called a compile error and "
                 "which nothing enforced. To keep using it, either CONSUME AND PUT IT BACK "
                 "(`set <name> <new value>` re-initialises the place — that is how a handle "
                 "threads through a loop), or borrow it LOCALLY with `ref h`. ☞ borrowing across "
                 "an OP BOUNDARY is not lowered yet (E-IR-UNSUP says so at the call site), so "
                 "`f (ref h)` is not a way out today", l);
        low_kw_t kw = (s->kind == LOW_CST_FORM && s->nkids && s->kids[0]->kind == LOW_CST_ATOM)
                        ? s->kids[0]->tok.kw : LOW_KW_NONE;
        bool head_panic = (s->kind == LOW_CST_FORM && s->nkids && s->kids[0]->kind == LOW_CST_ATOM &&
                           veq(s->kids[0]->tok.lex, "panic"));
        if (kw == LOW_KW_IF || kw == LOW_KW_MATCH) {
            // 각 arm 을 진입 상태 st 에서 재귀 → 발산 안 하는 arm 들 + fall-through 를 합류(JOIN1)
            int nlive = 0, ncons = 0;
            if (kw == LOW_KW_IF) {
                proven_size_t bi = 0;
                while (bi < s->nkids && s->kids[bi]->kind != LOW_CST_BLOCK) bi++;
                if (bi < s->nkids) {
                    bool ad; const low_cst_t *tb = s->kids[bi];
                    ck_own_state_t ex = ck_own_flow(out, pr, (const low_cst_t **)tb->kids, tb->nkids, name, fld, tab, nt, st, decl_line, &ad);
                    if (!ad) { if (ex == OWN_LIVE) nlive++; else ncons++; }
                    if (bi + 1 < s->nkids) {                    // else: 블록 또는 else-if form
                        const low_cst_t *e = s->kids[bi + 1]; bool ad2; ck_own_state_t ex2;
                        if (e->kind == LOW_CST_BLOCK) ex2 = ck_own_flow(out, pr, (const low_cst_t **)e->kids, e->nkids, name, fld, tab, nt, st, decl_line, &ad2);
                        else { const low_cst_t *one[1] = { e }; ex2 = ck_own_flow(out, pr, one, 1, name, fld, tab, nt, st, decl_line, &ad2); }
                        if (!ad2) { if (ex2 == OWN_LIVE) nlive++; else ncons++; }
                    } else {                                    // else 없음 → fall-through 는 진입 상태 유지
                        if (st == OWN_LIVE) nlive++; else ncons++;
                    }
                }
            } else {                                            // MATCH (망라 가정 — fall-through 없음)
                const low_cst_t *arms = s->kids[s->nkids - 1];
                if (arms->kind == LOW_CST_BLOCK)
                    for (proven_size_t q = 0; q < arms->nkids; q++) {
                        const low_cst_t *cf = arms->kids[q];
                        if (cf->kind != LOW_CST_FORM || !cf->nkids || cf->kids[0]->kind != LOW_CST_ATOM ||
                            cf->kids[0]->tok.kw != LOW_KW_CASE) continue;
                        const low_cst_t *ab = cf->kids[cf->nkids - 1];
                        if (ab->kind != LOW_CST_BLOCK) continue;
                        bool ad; ck_own_state_t ex = ck_own_flow(out, pr, (const low_cst_t **)ab->kids, ab->nkids, name, fld, tab, nt, st, decl_line, &ad);
                        if (!ad) { if (ex == OWN_LIVE) nlive++; else ncons++; }
                    }
            }
            if (nlive > 0 && ncons > 0)
                emit(out, "E-OWN-JOIN",
                     "this `owned` value is CONSUMED on one path of this branch but still LIVE on "
                     "another where they merge — Lowent requires the ownership state to be STATICALLY "
                     "consistent at a join (RFC-0044 §9.3): no hidden drop-flag decides it at runtime. "
                     "Consume it on EVERY path, or make it conditionally owned (`?owned`)",
                     s->kids[0]->tok.line);
            if (nlive == 0 && ncons == 0) { *diverges = true; return st; }   // 모든 arm 발산 → 분기 발산
            st = (ncons > 0) ? OWN_CONSUMED : OWN_LIVE;
        } else if (kw == LOW_KW_WHILE || kw == LOW_KW_LOOP || kw == LOW_KW_FOR) {
            // ★★★ **루프 본문도 흐름 분석한다** (2026-07-19, lib/out.low 이 찾았다).
            //   전엔 `while` 전체를 **소비 문장 하나**로 봤다: 본문 어딘가에서 소비하면
            //   루프가 끝난 뒤 무조건 CONSUMED 였고, 본문 안의 **재초기화를 볼 수가 없었다.**
            //   ⇒ 소유값을 루프로 이어받는 모양(`step p` → `set p …`)이 표현 불가능했고,
            //     그것이 곧 버퍼링 라이터의 모양이다(RFC-0069 §3 — 핸들을 여러 번 쓰고 끝낸다).
            //   ★ 규칙은 **한 바퀴가 자기 자신과 맞아야 한다**: 진입 LIVE 로 본문을 돌려
            //     끝에도 LIVE 면 루프는 안전하다(다음 바퀴가 같은 상태에서 시작한다).
            //     끝이 CONSUMED 면 **두 번째 바퀴가 use-after-move** 다 — 거기서 잡는다.
            const low_cst_t *lb = (s->nkids && s->kids[s->nkids - 1]->kind == LOW_CST_BLOCK)
                                    ? s->kids[s->nkids - 1] : NULL;
            if (lb) {
                bool ad;
                ck_own_state_t ex = ck_own_flow(out, pr, (const low_cst_t **)lb->kids, lb->nkids,
                                                name, fld, tab, nt, OWN_LIVE, decl_line, &ad);
                if (!ad && ex == OWN_CONSUMED) {
                    emit(out, "E-OWN-MOVED",
                         "this `owned` value is CONSUMED inside a LOOP and never put back — the "
                         "SECOND iteration would be a use-after-move. A loop body must end the way "
                         "it began: consume it and REASSIGN the place (`set <name> …`, which "
                         "re-initialises it), or move the consumption out of the loop",
                         s->kids[0]->tok.line);
                    st = OWN_CONSUMED;
                }
            } else if (st == OWN_LIVE && ck_own_consumes(s, name, fld, tab, nt, false)) {
                st = OWN_CONSUMED;   // 블록을 못 찾으면 종전대로 보수적으로
            }
        } else if (kw == LOW_KW_GUARD) {
            // guard 의 else 는 **발산**한다(§9.4 FD2) — 그 소비는 진행 경로에 반영하지 않는다. COND 는 읽기.
            continue;
        } else {
            if (st == OWN_LIVE && ck_own_consumes(s, name, fld, tab, nt, false)) st = OWN_CONSUMED;
            // ★ 재초기화는 자리를 **다시 살린다** — 그리고 새 값도 결국 완결돼야 한다
            //   (스코프 끝 검사가 그것을 요구한다). 그래서 LIVE 로 되돌린다.
            if (reinit) st = OWN_LIVE;
            if (kw == LOW_KW_RETURN || kw == LOW_KW_GIVE || kw == LOW_KW_BREAK ||
                kw == LOW_KW_CONTINUE || kw == LOW_KW_FAIL || head_panic) { *diverges = true; return st; }
        }
    }
    return st;
}

// ★★★ **아핀 필드 수집** (RFC-0044 §9.1) — 타입 이름의 struct 선언에서 `owned` 로 표시된
//   필드들을 모은다. 이것이 있어야 `p.a` 를 **정적으로 이름 붙는 place** 로 추적할 수 있다.
static proven_size_t ck_struct_owned_fields(const low_parse_result_t *pr, proven_u8str_view_t ty,
                                            proven_u8str_view_t *outn, proven_size_t cap) {
    proven_size_t n = 0;
    if (!ty.size) return 0;
    for (proven_size_t i = 0; i < pr->nforms; i++) {
        const low_cst_t *f = pr->forms[i]; ck_cur_form = f;
        if (!f || f->kind != LOW_CST_FORM || f->nkids < 2 || !ck_atom(f->kids[0])) continue;
        if (f->kids[0]->tok.kw != LOW_KW_STRUCT) continue;
        if (!ck_atom(f->kids[1]) || !proven_u8str_view_eq(f->kids[1]->tok.lex, ty)) continue;
        const low_cst_t *blk = NULL;
        for (proven_size_t z = 0; z < f->nkids; z++)
            if (f->kids[z]->kind == LOW_CST_BLOCK) blk = f->kids[z];
        if (!blk) continue;
        for (proven_size_t q = 0; q < blk->nkids && n < cap; q++) {
            const low_cst_t *fl = blk->kids[q];
            if (fl->kind != LOW_CST_FORM || fl->nkids < 2 || !ck_atom(fl->kids[0])) continue;
            for (proven_size_t z = 1; z < fl->nkids; z++)
                if (ck_atom(fl->kids[z]) && veq(fl->kids[z]->tok.lex, "owned"))
                    { outn[n++] = fl->kids[0]->tok.lex; break; }
        }
    }
    return n;
}

// ★★★ **owned 필드의 타입**을 뽑는다 — 이름만 뽑는 위 함수의 짝이다.
//   위 함수는 *부분 이동*을 보려고 **이름**이 필요했고, 아래 규칙은 *"무엇을 품었나"* 를
//   보려고 **타입**이 필요하다. 같은 절을 두 번 읽는 대신 두 함수가 한 절을 나눠 읽는다.
static proven_size_t ck_struct_owned_field_types(const low_parse_result_t *pr, proven_u8str_view_t ty,
                                                 proven_u8str_view_t *outt, proven_size_t cap) {
    proven_size_t n = 0;
    if (!ty.size) return 0;
    for (proven_size_t i = 0; i < pr->nforms; i++) {
        const low_cst_t *f = pr->forms[i]; ck_cur_form = f;
        if (!f || f->kind != LOW_CST_FORM || f->nkids < 2 || !ck_atom(f->kids[0])) continue;
        if (f->kids[0]->tok.kw != LOW_KW_STRUCT) continue;
        if (!ck_atom(f->kids[1]) || !proven_u8str_view_eq(f->kids[1]->tok.lex, ty)) continue;
        const low_cst_t *blk = NULL;
        for (proven_size_t z = 0; z < f->nkids; z++)
            if (f->kids[z]->kind == LOW_CST_BLOCK) blk = f->kids[z];
        if (!blk) continue;
        for (proven_size_t q = 0; q < blk->nkids && n < cap; q++) {
            const low_cst_t *fl = blk->kids[q];
            if (fl->kind != LOW_CST_FORM || fl->nkids < 2 || !ck_atom(fl->kids[0])) continue;
            for (proven_size_t z = 1; z < fl->nkids; z++) {
                if (!ck_atom(fl->kids[z]) || !veq(fl->kids[z]->tok.lex, "owned")) continue;
                if (z + 1 < fl->nkids && ck_atom(fl->kids[z + 1])) outt[n++] = fl->kids[z + 1]->tok.lex;
                break;
            }
        }
    }
    return n;
}

// ★★★ **빌린 뒤 옮기면 그 빌림은 죽는다** (RFC-0005 EXCL · RFC-0044 EXCL-F, 2026-07-23).
//   `ref x` **식 빌림**은 여태 이동에 대해 **전혀 추적되지 않았다**: 아래가 정적·런타임 모두
//   통과했다 — 옮겨간 자리를 빌림이 계속 읽는다.
//       let r be ref h ref p .
//       let z be h p .            ← p 가 떠났다
//       return field r fd .       ← 그런데 r 로 계속 읽는다
//   ck_borrow_walk 는 `borrow …do…end` **블록**만 봐서 이 모양을 못 봤다.
//   ⇒ 이 이름을 빌린 **별칭**들을 모으고, 이름이 **소비된 뒤** 별칭이 언급되면 거절한다.
//   ★ 보수적이지 않다(거짓양성 회피): 이동 **이전**의 빌림 사용은 그대로 통과한다.
static void ck_borrow_after_move(low_check_result_t *out, const low_cst_t *body,
                                 proven_u8str_view_t name, const proven_u8str_view_t *fns,
                                 proven_size_t nfn, const low_opinfo_t *tab, proven_size_t nt) {
    proven_u8str_view_t al[8]; proven_size_t na = 0;
    proven_u8str_view_t nofld = { .ptr = NULL, .size = 0 };
    for (proven_size_t i = 0; i < body->nkids && na < 8; i++) {
        const low_cst_t *d = body->kids[i];
        if (d->kind != LOW_CST_FORM || d->nkids < 4 || !ck_atom(d->kids[0])) continue;
        low_kw_t kw = d->kids[0]->tok.kw;
        if ((kw != LOW_KW_VAR && kw != LOW_KW_LET) || !ck_atom(d->kids[1])) continue;
        for (proven_size_t j = 2; j + 1 < d->nkids; j++) {
            if (!ck_atom(d->kids[j]) || d->kids[j]->tok.kw != LOW_KW_BE) continue;
            // `be ref <name>` / `be mut_ref <name>` — **두 모양**이 다 온다: 형제 원자로 평평하게
            //   오기도 하고, 정규화가 GROUP(FORM(ref, name)) 으로 싸기도 한다. 둘 다 본다.
            {
                const low_cst_t *v = d->kids[j + 1];
                while (v && (v->kind == LOW_CST_GROUP || v->kind == LOW_CST_FORM) && v->nkids == 1)
                    v = v->kids[0];
                bool hit = false;
                if (v && v->kind == LOW_CST_FORM && v->nkids >= 2 &&
                    ck_atom(v->kids[0]) && ck_atom(v->kids[1]) &&
                    (veq(v->kids[0]->tok.lex, "ref") || veq(v->kids[0]->tok.lex, "mut_ref")) &&
                    proven_u8str_view_eq(v->kids[1]->tok.lex, name)) hit = true;
                if (!hit && j + 2 < d->nkids && ck_atom(d->kids[j + 1]) && ck_atom(d->kids[j + 2]) &&
                    (veq(d->kids[j + 1]->tok.lex, "ref") || veq(d->kids[j + 1]->tok.lex, "mut_ref")) &&
                    proven_u8str_view_eq(d->kids[j + 2]->tok.lex, name)) hit = true;
                if (hit) al[na++] = d->kids[1]->tok.lex;
            }
            break;
        }
    }
    if (!na) return;
    bool moved = false;
    for (proven_size_t i = 0; i < body->nkids; i++) {
        const low_cst_t *st2 = body->kids[i];
        if (moved) {
            for (proven_size_t q = 0; q < na; q++) {
                proven_size_t m = 0; proven_u32 l = 0;
                ck_own_mentions(st2, al[q], nofld, &m, &l);
                if (m) {
                    emit(out, "E-EXCL-MOVED",
                         "this borrow outlived the value it borrows — the owner was MOVED and the "
                         "borrow is still used afterwards. A move ends every borrow of that place "
                         "(RFC-0005 EXCL: readers-XOR-writer, and a move is the ultimate write). "
                         "`ref x` expression borrows used to escape this check entirely — only "
                         "`borrow … do … end` BLOCKS were tracked — so a borrow could read a place "
                         "whose owner had already left. Finish with the borrow BEFORE moving the "
                         "owner, or take the borrow again after the move from its new owner", l);
                    return;
                }
            }
        }
        if (!moved && ck_own_consumes(st2, name, nofld, tab, nt, false)) moved = true;
        // ★ **EXCL-F**(RFC-0044) — 필드 하나를 옮기는 것도 그 집합의 빌림을 무효화한다.
        //   집합을 빌린 채 필드를 빼가면 빌림은 구멍 난 집합을 온전한 것으로 읽는다.
        if (!moved)
            for (proven_size_t q = 0; q < nfn; q++)
                if (ck_own_consumes(st2, name, fns[q], tab, nt, false)) { moved = true; break; }
    }
}

// ★★★★ **전부 옮겼으면 완결된 것이다** (2026-08-15, 소유자 결정 ②A).
//   `report` 가 거짓이면 *"흐름 끝에 살아 있다"* 를 **말하지 않는다**. 부르는 쪽이 그 집합의
//   `owned` 필드를 **하나도 빠짐없이** 옮겼다는 것을 이미 셌을 때만 거짓이 된다 — 그때 집합은
//   껍데기이고, 껍데기를 자동 해제하는 것은 아무것도 삼키지 않는다.
//   ☞ **부분** 이동은 여전히 오류다(아래 `E-OWN-PARTIAL`) — 하나만 옮기고 나머지를 잊는 것이
//     정확히 이 규율이 막으려는 것이기 때문이다.
static ck_own_state_t ck_own_linear_r(low_check_result_t *out, const low_parse_result_t *pr, const low_cst_t *body,
                          proven_u8str_view_t name, proven_u8str_view_t fld,
                          proven_u8str_view_t ty, proven_u32 decl_line,
                          proven_size_t from, const low_opinfo_t *tab, proven_size_t nt, bool report) {
    bool diverges = false;
    ck_own_state_t st = ck_own_flow(out, pr, (const low_cst_t **)(body->kids + from), body->nkids - from,
                                    name, fld, tab, nt, OWN_LIVE, decl_line, &diverges);
    // 흐름 끝에서 **live** 로 남았다 = 그 출구에서의 **자동 해제**. `return 0`(값을 안 넘기는 발산)도
    //   포함이다 — h 를 소비하지 않고 나가면 return 직전에 h 가 자동 drop 된다. 그 자체는 옳다(해제는
    //   실패 안 함) — 실패할 수 있다고 이 프로그램이 스스로 선언한 타입이 아니라면. (`return h` 는 소비 →
    //   CONSUMED 라 여기 안 온다.)  (void)diverges;
    (void)diverges;
    if (report && st == OWN_LIVE && ty.size && ck_type_needs_completion(pr, ty))
        emit(out, "E-OWN-INCOMPLETE",
             "this value is dropped automatically at the end of scope — but the program itself "
             "declares an op that takes this type `owned` BY VALUE and returns a `result`: "
             "finishing it CAN FAIL. An automatic drop is a RELEASE (total, non-suspending), and "
             "it has nowhere to hand you that failure — it would SWALLOW it. A fallible finish "
             "(flush/commit/close) is a COMPLETION and must be EXPLICIT: call it and handle the "
             "`result`. If you really mean to discard the value and its failure, say so with "
             "`drop`. RFC-0058",
             decl_line);
    return st;
}
static ck_own_state_t ck_own_linear(low_check_result_t *out, const low_parse_result_t *pr, const low_cst_t *body,
                          proven_u8str_view_t name, proven_u8str_view_t fld,
                          proven_u8str_view_t ty, proven_u32 decl_line,
                          proven_size_t from, const low_opinfo_t *tab, proven_size_t nt) {
    return ck_own_linear_r(out, pr, body, name, fld, ty, decl_line, from, tab, nt, true);
}

static void ck_ownership(low_check_result_t *out, const low_parse_result_t *pr, const low_cst_t *f,
                         const low_opinfo_t *tab, proven_size_t nt) {
    const low_cst_t *body = (f->kids[f->nkids - 1]->kind == LOW_CST_BLOCK)
                              ? f->kids[f->nkids - 1] : NULL;
    if (!body) return;

    // ★ **값으로 받은 소유도 소유다.** `input h owned T .` 는 책임의 이전이다.
    //   지역 변수만 보면 **경계에서 새는** 선형성이 된다(교훈 1).
    //
    //   ★★ 전엔 이 자리가 **자기만의 `input` 스캐너**를 갖고 있었다 — 절을 자르고, `owned` 를
    //     찾고, 타입 낱말을 골랐다. 그리고 **여섯 소비자가 각자** 그랬다. 그 여섯이 조금씩
    //     다르게 틀렸고, actor 핸들러의 `input` 절은 **통째로 버려졌다**(그런데 `--check` 는
    //     초록불이었고 실행하면 **산술을 탓했다**).
    //   이제 `low_op_header()` 가 **유일한 답**이다(DECISION-0015).
    low_op_header_t h = low_op_header(f);
    if (h.too_many)
        emit(out, "E-IR-ARITY",
             "this op declares more parameters than the tool can carry a header for — refusing is "
             "the honest answer (a silently dropped parameter is a silently wrong program)",
             f->kids[0]->tok.line);
    for (proven_size_t i = 0; i < h.np; i++) {
        if (!h.p[i].is_owned) continue;
        proven_u8str_view_t ty = (h.p[i].core < h.p[i].te && f->kids[h.p[i].core]->kind == LOW_CST_ATOM)
                                   ? f->kids[h.p[i].core]->tok.lex   // ★ 타입의 **알맹이**
                                   : (proven_u8str_view_t){ .ptr = NULL, .size = 0 };
        proven_u8str_view_t nofld = { .ptr = NULL, .size = 0 };
        // ★ 필드를 **먼저** 센다(2026-08-15): 전부 옮겼는지 알아야 집합에 대해 말할지 정할 수 있다.
        proven_u8str_view_t fns0[8];
        proven_size_t nfn0 = ck_struct_owned_fields(pr, ty, fns0, 8);
        proven_size_t nmoved0 = 0;
        bool fst[8];
        for (proven_size_t q = 0; q < nfn0; q++) {
            fst[q] = ck_own_linear(out, pr, body, h.p[i].name, fns0[q], ty,
                                   f->kids[0]->tok.line, 0, tab, nt) == OWN_CONSUMED;
            if (fst[q]) nmoved0++;
        }
        bool all_fields_moved = (nfn0 > 0 && nmoved0 == nfn0);
        ck_own_state_t base_st =
            ck_own_linear_r(out, pr, body, h.p[i].name, nofld, ty, f->kids[0]->tok.line, 0, tab, nt,
                            !all_fields_moved);
        // ★★★ **필드 단위 부분 이동**(RFC-0044 §9.1 OS3/OS4) — 이 소유 값이 아핀 필드를 가진
        //   구조체면 `p.a` 각각을 **따로** 추적한다. 한 필드를 옮기면 그 필드만 consumed 가 되고
        //   (partial), 다시 쓰면 use-after-move 로 잡힌다. 나머지 필드는 그대로 산다.
        //   ★ PM2(객체는 부분이동 금지)는 지금 **무의미**하다 — 전체-객체 drop op 이 아직 없다
        //     (destructor 선언은 거절된다) ⇒ 모든 집합이 plain 이라 PM1 이 보편 적용된다.
        {
            ck_borrow_after_move(out, body, h.p[i].name, fns0, nfn0, tab, nt);   // ★ 빌림 vs 이동/필드이동
            bool any_field_moved = false;
            for (proven_size_t q = 0; q < nfn0; q++) if (fst[q]) any_field_moved = true;
            // ★★★ **OS1 — 집합의 파생 상태**(RFC-0044 §9.1). 필드를 하나라도 옮겼으면 그 집합은
            //   **partial** 이다. partial 인 집합을 **통째로** 옮기면 이미 떠난 필드까지 넘기는
            //   셈이라 **이중 소유**가 된다 — 받는 쪽은 구멍 난 집합을 온전한 것으로 안다.
            //   ⇒ 필드 이동과 전체 이동이 **둘 다 일어나면** 거절한다(보수적·흐름 무관).
            //   ★ 반대 순서(전체 먼저·필드 나중)는 기반 흐름이 이미 E-OWN-MOVED 로 잡는다.
            if (any_field_moved && base_st == OWN_CONSUMED)
                emit(out, "E-OWN-PARTIAL",
                     "this aggregate is PARTIALLY MOVED — one of its `owned` fields was moved out — "
                     "and then the WHOLE aggregate is moved as well. The receiver would believe it "
                     "owns every field, including the one that already left: that is DOUBLE "
                     "OWNERSHIP (RFC-0044 §9.1 OS1 — a partially moved place is not whole-live and "
                     "cannot be consumed as a whole). Move the remaining fields individually, or do "
                     "not move the fields out if you mean to hand over the whole thing",
                     f->kids[0]->tok.line);
        }
    }

    for (proven_size_t i = 0; i < body->nkids; i++) {
        const low_cst_t *d = body->kids[i];
        // ★★★★ **낱말을 빼면 책임도 빠진다** (2026-08-15, 소유자 결정 ①A).
        //   여기까지 규율은 *"자원은 완결해야 한다"* 가 아니라 *"자원이라고 **적은** 것은 완결해야
        //   한다"* 였다: `var f be files.handle …` 는 `owned` 한 낱말이 없다는 이유로 아무 말도
        //   듣지 않았고, 파일은 조용히 샜다. **잊기 가장 쉬운 낱말이 곧 안전장치**였던 셈이다.
        //   ⇒ 완결필요 타입을 맨 이름으로 묶으면 **적으라고 요구한다**. 추론해서 숨기지 않는 이유:
        //     소유는 비용이고, 이 언어가 파는 것은 **비용이 보이는 것**이다(P2).
        //   ★ 좁게: 선언의 **알맹이 낱말 하나**가 완결필요 타입일 때만 문다. `option handle` 이나
        //     `result void file_error` 는 아직 자원이 아니다(풀어야 자원이 나온다).
        if (!ck_is_owned_decl(d) && d->kind == LOW_CST_FORM && d->nkids >= 3 &&
            d->kids[0]->kind == LOW_CST_ATOM &&
            (d->kids[0]->tok.kw == LOW_KW_VAR || d->kids[0]->tok.kw == LOW_KW_LET) &&
            d->kids[2]->kind == LOW_CST_ATOM && d->kids[2]->tok.kw == LOW_KW_NONE &&
            !veq(d->kids[2]->tok.lex, "mut") && !veq(d->kids[2]->tok.lex, "ref") &&
            !veq(d->kids[2]->tok.lex, "mut_ref") &&
            (d->nkids == 3 || (d->kids[3]->kind == LOW_CST_ATOM && d->kids[3]->tok.kw == LOW_KW_BE)) &&
            ck_type_needs_completion(pr, d->kids[2]->tok.lex))
            emit(out, "E-OWN-BARE",
                 "this binds a value whose completion CAN FAIL, but the binding does not say "
                 "`owned` — and without that word nothing requires you to complete it. The type "
                 "already knows it is a resource (some op takes it `owned` and returns a `result`), "
                 "so the tool could infer this silently; it refuses to, because ownership is a COST "
                 "and this language sells costs you can see. Write `owned` and the compiler will "
                 "hold you to it; if you truly mean to let it go, take it `owned` and `drop` it. "
                 "RFC-0058",
                 d->kids[0]->tok.line);
        if (!ck_is_owned_decl(d)) continue;
        proven_u8str_view_t ty = { .ptr = NULL, .size = 0 };
        for (proven_size_t j = 2; j < d->nkids; j++)
            if (d->kids[j]->kind == LOW_CST_ATOM && veq(d->kids[j]->tok.lex, "owned") &&
                j + 1 < d->nkids && d->kids[j + 1]->kind == LOW_CST_ATOM)
                { ty = d->kids[j + 1]->tok.lex; break; }
        proven_u8str_view_t nofld2 = { .ptr = NULL, .size = 0 };
        (void)ck_own_linear(out, pr, body, d->kids[1]->tok.lex, nofld2, ty,
                            d->kids[0]->tok.line, i + 1, tab, nt);
    }
}

static void ck_parallel(low_check_result_t *out, const low_cst_t *f) {
    // parallel 절이 지목한 슬라이스
    proven_u8str_view_t s2 = { 0 }; bool have = false;
    for (proven_size_t i = 2; i + 1 < f->nkids; i++)
        if (f->kids[i]->kind == LOW_CST_ATOM && veq(f->kids[i]->tok.lex, "parallel") &&
            f->kids[i + 1]->kind == LOW_CST_ATOM) { s2 = f->kids[i + 1]->tok.lex; have = true; break; }
    if (!have) return;
    const low_cst_t *body = (f->kids[f->nkids - 1]->kind == LOW_CST_BLOCK) ? f->kids[f->nkids - 1] : NULL;
    if (!body) return;

    // 본문에서 `while lt <i> (len <s>) . do … end` 를 찾는다. 그 밖의 지역은 '바깥' 이다.
    // ★ `reduce <acc> <op> .` 절 — 리덕션 선언. DET-3 가 여기에 걸린다.
    ck_red_t reds[8]; proven_size_t nreds = 0;
    for (proven_size_t i = 2; i + 2 < f->nkids && nreds < 8; i++) {
        if (f->kids[i]->kind != LOW_CST_ATOM || !veq(f->kids[i]->tok.lex, "reduce")) continue;
        if (f->kids[i + 1]->kind != LOW_CST_ATOM || f->kids[i + 2]->kind != LOW_CST_ATOM) continue;
        reds[nreds].acc = f->kids[i + 1]->tok.lex;
        reds[nreds].op  = f->kids[i + 2]->tok.lex;
        // ★★ DET-3: 결합적이지 않은 연산은 **트리를 고정해야** 한다 ⇒ 쪼갤 수 없다.
        if (!ck_assoc_op(reds[nreds].op))
            emit(out, "E-PAR-ASSOC",
                 "this reduction operator is not associative, so the shape of the reduction tree "
                 "changes the result — a split tree is not deterministic "
                 "(★ nonassoc_shape_matters, Qed — docs/proofs/coq/LowentPar.v)", f->line);
        nreds++;
    }

    // ★★★★★ **시작값이 항등원이 아니면 나눈 답이 순차와 다르다** (DET-1 · 결함 노트 #64, 2026-09-16).
    //   조각마다 그 시작값에서 다시 시작하므로 조각 수만큼 더해진다(실측: 순차 121 · 네이티브 621).
    //   결합성은 *모양*을, 항등원은 *시작*을 지킨다 — 둘 다 있어야 한 답이다.
    for (proven_size_t q = 0; q < nreds; q++) {
        const char *want = NULL;
        if (veq(reds[q].op, "add") || veq(reds[q].op, "bit_or") || veq(reds[q].op, "bit_xor")) want = "0";
        else if (veq(reds[q].op, "mul")) want = "1";
        else if (veq(reds[q].op, "max")) want = "0";
        if (!want) continue;                 // min · bit_and 의 항등원은 폭의 최댓값 — 여기서는 안 본다
        for (proven_size_t i = 0; i < body->nkids; i++) {
            const low_cst_t *st = body->kids[i];
            if (st->kind != LOW_CST_FORM || st->nkids < 5 || st->kids[0]->kind != LOW_CST_ATOM) continue;
            low_kw_t kw2 = st->kids[0]->tok.kw;
            if (kw2 != LOW_KW_VAR && kw2 != LOW_KW_LET) continue;
            if (st->kids[1]->kind != LOW_CST_ATOM ||
                !proven_u8str_view_eq(st->kids[1]->tok.lex, reds[q].acc)) continue;
            if (veq(reds[q].op, "max") && st->kids[2]->kind == LOW_CST_ATOM &&
                st->kids[2]->tok.lex.size && st->kids[2]->tok.lex.ptr[0] != (proven_u8)0x75) break;
            const low_cst_t *init = st->kids[st->nkids - 1];
            if (init->kind != LOW_CST_ATOM || init->tok.kind != LOW_TOK_NUMBER) break;
            if (!veq(init->tok.lex, want))
                emit(out, "E-PAR-IDENTITY",
                     "the accumulator this reduction starts from is not the IDENTITY of its "
                     "operator, so a split answer is not the sequential one: every piece starts "
                     "again from that value and it is counted once per piece (measured: sequential "
                     "121, native split 621). DET-1 promises a split is bit-identical — that holds "
                     "only from the identity (`add`/`bit_or`/`bit_xor` → 0, `mul` → 1, `max` → 0 on "
                     "an unsigned width). Start from the identity and add the offset once, after "
                     "the loop", st->line);
            break;
        }
    }

    bool found_loop = false, any_err = false;
    proven_size_t before = 0;
    for (proven_size_t i = 0; i < body->nkids; i++) {
        const low_cst_t *st = body->kids[i];
        if (st->kind != LOW_CST_FORM || st->nkids == 0 || st->kids[0]->kind != LOW_CST_ATOM) continue;
        low_kw_t kw = st->kids[0]->tok.kw;
        if ((kw == LOW_KW_VAR || kw == LOW_KW_LET) && st->nkids >= 2 &&
            st->kids[1]->kind == LOW_CST_ATOM) {
            // ★★ DET-3: **부동소수 누산기는 쪼갤 수 없다.** 부동 덧셈은 결합적이지 않다 —
            //   쪼개면 트리 모양이 바뀌고, 결과가 **스케줄에 의존한다.**
            //   (RFC-0053 의 `sum`(Neumaier) 이 순차·결정론적 선택지다.)
            if (st->nkids >= 3 && st->kids[2]->kind == LOW_CST_ATOM) {
                proven_u8str_view_t tw = st->kids[2]->tok.lex;
                if ((veq(tw, "f32") || veq(tw, "f64"))) {
                    proven_u8str_view_t dummy;
                    if (ck_is_reduction(reds, nreds, st->kids[1]->tok.lex, &dummy))
                        emit(out, "E-PAR-FLOAT",
                             "a FLOAT reduction cannot be split: float addition is not associative, "
                             "so the tree shape changes the result and the answer would depend on "
                             "the schedule (★ nonassoc_shape_matters, Qed). Use the sequential "
                             "`sum` (Neumaier) — determinism is part of the meaning, not a detail",
                             st->line);
                }
            }
        }
        if (kw != LOW_KW_WHILE || st->nkids < 3) continue;
        proven_u8str_view_t iv;
        if (!ck_while_lt_iv(st, &iv)) continue;             // 조건: lt <i> (len <s>) — 평평하든 나무든
        const low_cst_t *blk = NULL;
        for (proven_size_t j = 0; j < st->nkids; j++)
            if (st->kids[j]->kind == LOW_CST_BLOCK) blk = st->kids[j];
        if (!blk) continue;
        found_loop = true;
        before = out->diags.len;
        ck_par_body(out, blk, s2, iv, body, i, reds, nreds);
        if (out->diags.len != before) any_err = true;   // ★ 위반이 있으면 OK 라고 말하면 안 된다
    }
    if (any_err) return;
    if (!found_loop)
        // ★★★ **거절한다 — 경고가 아니다** (2026-07-20).
        //
        //   `parallel s split .` 은 *"이 루프를 쪼개도 답이 같다"* 는 **주장**이고, DET-1 이
        //   그것을 증명해 주는 것은 **쪼갤 루프가 실제로 있을 때뿐**이다. 루프를 못 찾으면
        //   도구는 **아무것도 검증하지 않는다** — 그런데 절은 소스에 남아, 읽는 사람에게는
        //   *"검사받았다"* 로 보인다. ⇒ **검사되지 않는 약속이고, 그것이 §0 이 말하는 거짓말이다.**
        //
        //   ★★ 이 저장소는 같은 판단을 이미 두 번 했다: `mailbox unbounded`(진짜로 못 자라는
        //     것을 무한이라 부르지 않는다) · `bounded 0`(닿을 수 없는 약속은 거짓말이다).
        //     **여기만 경고로 남아 있었다.**
        //   ★★★ 그리고 이 경로는 **실제로 물린 적이 있다**: 나무 파싱이 서자 판별기가
        //     `while lt i (len s)` 를 못 알아보고 이 자리로 떨어졌고, 그래서 **거짓말하는
        //     `parallel` 을 정적으로 놓쳤다**(런타임 분할 대조가 잡았다 — golden.sh 가 적어 뒀다).
        //     경고였기 때문에 초록이었고, 초록이었기 때문에 아무도 안 봤다.
        //   ⇒ 쪼갤 루프가 없으면 **절을 지우라**고 말한다. 지우면 잃는 것이 없다 —
        //     검사되지 않던 절이니 뜻이 없었다.
        emit(out, "E-PAR-NOLOOP",
            "the `parallel` clause names a slice, but no `while lt <i> (len <slice>) . do … end` "
            "loop was found to split. That clause is a CLAIM — DET-1 proves a split is "
            "bit-identical only when there IS a loop to split — so with no loop the compiler "
            "verifies NOTHING while the annotation still tells every reader it was checked. "
            "An unchecked promise is the lie PRINCIPLES.md §0 is about; the same judgement already "
            "refused `mailbox unbounded` and `bounded 0`. Delete the clause (it means nothing "
            "here) or write the loop it describes", f->line);
    else
        warn(out, "W-PAR-OK",
             "this loop satisfies the Bernstein conditions and may be split "
             "(DET-1 proves the parallel result is bit-identical to the sequential one — "
             "docs/proofs/coq/LowentPar.v). Execution is still sequential, which is a VALID "
             "implementation precisely because of that theorem", f->line);
}

// ★★ `match` 의 **완전성** — 모든 변형이 다뤄져야 한다.
//   이것이 match 의 값어치다: 나중에 enum 에 변형을 추가하면 **컴파일러가 빠진 곳을 전부
//   찾아 준다.** 그것이 없으면 match 는 그냥 if 사슬이다.
//   (그리고 없는 변형을 case 로 걸면 그것도 잡는다 — 이름이 아무것도 안 가리킨다.)
// ★ MM6 — 이 이름이 **선언된 enum 변형**인가(아니면 P-BIND 후보). pr 의 모든 enum 을 훑는다.
static bool ck_name_is_variant(const low_parse_result_t *pr, proven_u8str_view_t name) {
    for (proven_size_t q = 0; q < pr->nforms; q++) {
        const low_cst_t *e = pr->forms[q];
        if (e->kind != LOW_CST_FORM || e->nkids < 3 || e->kids[0]->kind != LOW_CST_ATOM) continue;
        if (e->kids[0]->tok.kw != LOW_KW_ENUM) continue;
        const low_cst_t *b2 = e->kids[e->nkids - 1];
        if (b2->kind != LOW_CST_BLOCK) continue;
        for (proven_size_t v = 0; v < b2->nkids; v++) {
            const low_cst_t *vn = b2->kids[v];
            proven_u8str_view_t nm = (vn->kind == LOW_CST_ATOM) ? vn->tok.lex
                : (vn->kind == LOW_CST_FORM && vn->nkids && vn->kids[0]->kind == LOW_CST_ATOM)
                    ? vn->kids[0]->tok.lex : (proven_u8str_view_t){ 0 };
            if (nm.size && proven_u8str_view_eq(nm, name)) return true;
        }
    }
    return false;
}
static void ck_match(low_check_result_t *out, const low_parse_result_t *pr,
                     const low_cst_t *blk, const low_cst_t *op) {
    if (!blk) return;
    for (proven_size_t i = 0; i < blk->nkids; i++) {
        const low_cst_t *m = blk->kids[i];
        // 중첩 블록도 훑는다
        if (m->kind == LOW_CST_FORM)
            for (proven_size_t q = 0; q < m->nkids; q++)
                if (m->kids[q]->kind == LOW_CST_BLOCK) ck_match(out, pr, m->kids[q], op);
        if (m->kind != LOW_CST_FORM || m->nkids < 2 || m->kids[0]->kind != LOW_CST_ATOM) continue;
        if (m->kids[0]->tok.kw != LOW_KW_MATCH) continue;

        // ★ 명세형: `match <x> do  case <v> . do … end  …  end` — case 들이 **블록 안**에 있다.
        //   (do 를 빠뜨린 모양은 IR 이 거부한다 — 그 경우 case 들이 형제가 되고 뒤의 `end` 가
        //    바깥 블록을 조용히 닫는다. 진짜 프로그램을 쓰자마자 무한 루프로 드러났다.)
        proven_u8str_view_t seen[32]; proven_size_t nseen = 0;
        bool has_wild = false; proven_i64 wild_line = 0;   // ★ MM1: 와일드카드 `_` catch-all
        // ★ MM2: 리터럴 패턴 (정수/bool). enum 변형과 **도메인이 다르다**(무한 int vs 유한 bool/enum).
        proven_u8str_view_t litseen[32]; proven_size_t nlit = 0;
        bool lit_int = false, lit_bool = false, bt = false, bf = false;
        // ★ MM3b: option/result 패턴 도메인(유한 2-변형). some/none · ok/error.
        bool opt_dom = false, has_some = false, has_none = false, has_ok = false, has_err = false;
        // ★ MM7b — 바깥 꼬리표(ok · error · some · none)마다 **안쪽 패턴**을 모은다.
        struct { bool some, none, wild; proven_u8str_view_t var[16]; proven_size_t nvar; } nst[4];
        memset(nst, 0, sizeof nst);
        bool saw_case = false;   // ★ MM6 — 이 match 에 case arm 이 하나라도 있었나(가드-only 미망라 판정용)
        struct { proven_i64 lo, hi; } iv[64]; proven_size_t niv = 0;   // ★ MM4 — 정수 리터럴·범위 구간
        const low_cst_t *arms = m->kids[m->nkids - 1];
        if (arms->kind != LOW_CST_BLOCK) continue;
        for (proven_size_t q = 0; q < arms->nkids && nseen < 32; q++) {
            const low_cst_t *a2 = arms->kids[q];
            if (a2->kind != LOW_CST_FORM || a2->nkids < 1) continue;
            // ★ `else` 가지는 **한 겹 더 싸여 온다**(`FORM[end] > FORM[end] > ATOM else`) —
            //   앞선 `case` 가 닫히는 자리에 붙기 때문이다. 벗겨 내고 본다.
            if (a2->kids[0]->kind == LOW_CST_FORM && a2->kids[0]->nkids >= 1 &&
                a2->kids[0]->kids[0]->kind == LOW_CST_ATOM &&
                a2->kids[0]->kids[0]->tok.kw == LOW_KW_ELSE)
                a2 = a2->kids[0];
            if (a2->nkids < 2 || a2->kids[0]->kind != LOW_CST_ATOM) continue;
            // ★★ **`match` 안의 `else` 는 나머지를 받는 자리다**(정본 A.7 문법 · §6.5.4(6) · 결함 #81).
            //   부록 A.7 의 문법과 미망라 진단문이 둘 다 `else` 를 권하는데 이 층이 그것을 세지
            //   않아, 권하는 대로 쓴 글이 같은 진단을 다시 받았다. 문법이 받는 것을 검사가
            //   모르면 그 문법은 없는 것이다.
            if (a2->kids[0]->tok.kw == LOW_KW_ELSE) {
                if (has_wild)
                    emit(out, "E-MATCH-REDUNDANT",
                         "this `else` comes after an arm that already matches everything — it is DEAD "
                         "CODE (RFC-0020 §6.4). A `match` has one catch-all", a2->kids[0]->tok.line);
                else { has_wild = true; wild_line = a2->kids[0]->tok.line; }
                continue;
            }
            if (a2->kids[0]->tok.kw != LOW_KW_CASE) continue;
            saw_case = true;
            // ★★ **MM6: 가드 `when`** — 가드가 있는 arm 은 망라에 **기여하지 않는다**(가드가 거짓일 수
            //   있으므로 그 변형/값을 덮는다고 보증 못 한다, RFC-0020 §6.4). seen 에 더하지 않고 넘어간다
            //   — 그래서 가드만 있는 match 는 여전히 `_`/else 를 요구한다(보수적·건전).
            {
                bool arm_guarded = false;
                for (proven_size_t j = 2; j + 1 < a2->nkids; j++)
                    if (a2->kids[j]->kind == LOW_CST_ATOM && veq(a2->kids[j]->tok.lex, "when")) { arm_guarded = true; break; }
                if (arm_guarded) continue;
            }
            // ★★ **MM5: or-패턴** `case a or b or c .` — 각 서브패턴을 도메인에 더한다. 안 그러면
            //   `case red or green` 이 green 을 안 세어 **거짓 미망라**가 난다.
            {
                bool arm_is_or = false;
                for (proven_size_t j = 2; j + 1 < a2->nkids; j++)
                    if (a2->kids[j]->kind == LOW_CST_ATOM && veq(a2->kids[j]->tok.lex, "or")) { arm_is_or = true; break; }
                if (arm_is_or) {
                    if (has_wild) {
                        emit(out, "E-MATCH-REDUNDANT",
                             "this `case` comes AFTER a `_` (wildcard) arm — it can never run (RFC-0020 §6.4)",
                             a2->kids[1]->tok.line);
                        continue;
                    }
                    // ★ MM5 — 각 브랜치의 **헤드만** 도메인에 센다(뒤 원자는 페이로드 바인딩이라 건너뛴다).
                    bool head = true;
                    for (proven_size_t j = 1; j + 1 < a2->nkids && nseen < 32; j++) {
                        if (a2->kids[j]->kind != LOW_CST_ATOM) { head = false; continue; }
                        proven_u8str_view_t sp = a2->kids[j]->tok.lex;
                        if (veq(sp, "or")) { head = true; continue; }
                        if (!head) continue;   // 바인딩 이름 — 건너뛴다
                        head = false;          // 이 원자가 헤드; 다음 `or` 까지는 바인딩
                        if (ck_is_int_lit(sp)) { lit_int = true; continue; }
                        if (veq(sp, "true")) { bt = true; lit_bool = true; continue; }
                        if (veq(sp, "false")) { bf = true; lit_bool = true; continue; }
                        // 변형: 중복 검사 후 seen 에 더한다.
                        bool dup = false;
                        for (proven_size_t s = 0; s < nseen; s++)
                            if (proven_u8str_view_eq(seen[s], sp)) { dup = true; break; }
                        if (dup)
                            emit(out, "E-MATCH-REDUNDANT",
                                 "an or-pattern branch names a variant an earlier arm already handles — "
                                 "DEAD CODE (RFC-0020 §6.4)", a2->kids[j]->tok.line);
                        else seen[nseen++] = sp;
                    }
                    continue;
                }
            }
            if (a2->kids[1]->kind == LOW_CST_ATOM) {
                proven_u8str_view_t v2 = a2->kids[1]->tok.lex;
                // ★★ **와일드카드 `_`** (RFC-0020 P-WILD · RFC-0081 MM1) = catch-all: 나머지를 전부 덮어
                //   망라를 채운다. 그러나 catch-all **뒤의 arm 은 죽은 코드**다(앞의 `_` 가 늘 이긴다) —
                //   중복=에러 규율(§6.4)을 그대로 적용한다.
                if (veq(v2, "_")) {
                    if (has_wild)
                        emit(out, "E-MATCH-REDUNDANT",
                             "a second `_` (wildcard) arm — the first `_` already matches everything, so "
                             "this one is DEAD CODE (RFC-0020 §6.4)", a2->kids[1]->tok.line);
                    else { has_wild = true; wild_line = a2->kids[1]->tok.line; }
                    continue;
                }
                // ★ catch-all 뒤의 어떤 arm 도 도달불가다.
                if (has_wild) {
                    emit(out, "E-MATCH-REDUNDANT",
                         "this `case` comes AFTER a `_` (wildcard) arm — the wildcard already matched, so "
                         "this arm can never run. A dead arm is an error here, not a warning (RFC-0020 §6.4)",
                         a2->kids[1]->tok.line);
                    continue;
                }
                // ★★ **MM4: 범위 패턴** `case lo to hi .` — 정수 도메인에 기여. 구간 [lo,hi] 를 기록해
                //   타입 전 도메인을 타일하면 `_` 없이 망라(아래 타일 검사).
                if (a2->nkids >= 4 && a2->kids[2]->kind == LOW_CST_ATOM && veq(a2->kids[2]->tok.lex, "to")) {
                    lit_int = true;
                    proven_i64 lo, hi;
                    if (ck_int_val(a2->kids[1]->tok.lex, &lo) && ck_int_val(a2->kids[3]->tok.lex, &hi) && lo <= hi && niv < 64)
                        { iv[niv].lo = lo; iv[niv].hi = hi; niv++; }
                    continue;
                }
                // ★★ **MM2: 정수 리터럴 패턴** — enum 변형이 아니라 값 비교. 같은 리터럴 재등장 = 죽은 코드.
                if (ck_is_int_lit(v2)) {
                    lit_int = true;
                    bool dup = false;
                    for (proven_size_t s = 0; s < nlit; s++)
                        if (proven_u8str_view_eq(litseen[s], v2)) { dup = true; break; }
                    if (dup)
                        emit(out, "E-MATCH-REDUNDANT",
                             "this `case` matches an integer literal an earlier arm already handles — "
                             "DEAD CODE (the first arm wins). A redundant arm is an error, not a warning "
                             "(RFC-0020 §6.4)", a2->kids[1]->tok.line);
                    else if (nlit < 32) litseen[nlit++] = v2;
                    proven_i64 lv;
                    if (ck_int_val(v2, &lv) && niv < 64) { iv[niv].lo = lv; iv[niv].hi = lv; niv++; }
                    continue;
                }
                // ★★ **MM2: bool 리터럴 패턴** — 유한 도메인 {true,false}.
                if (veq(v2, "true") || veq(v2, "false")) {
                    lit_bool = true;
                    bool istrue = veq(v2, "true");
                    if ((istrue && bt) || (!istrue && bf))
                        emit(out, "E-MATCH-REDUNDANT",
                             "this `case` matches a bool literal an earlier arm already handles — DEAD "
                             "CODE (RFC-0020 §6.4)", a2->kids[1]->tok.line);
                    else if (istrue) bt = true; else bf = true;
                    continue;
                }
                // ★★ **MM3b: option/result 패턴** — some/none/ok/error 는 내장 2-변형이다(P-BIND 아님).
                if (veq(v2, "some") || veq(v2, "none") || veq(v2, "ok") || veq(v2, "error")) {
                    // ★ MM7 — **중첩** arm(`case ok (some x)`·`case ok none`)은 내부 패턴이 있어 단순
                    //   opt_dom 추적으로 망라·중복을 표현할 수 없다. 보수적으로 **기여 안 함**(가드 arm 처럼) —
                    //   그래서 중첩 match 는 `_`/명시 덮기를 요구한다(건전·과대근사). 중첩 = kids[2]가
                    //   GROUP 이거나 내부가 option/result 원자.
                    bool nested = false;
                    if (a2->nkids >= 4) {
                        const low_cst_t *sub = a2->kids[2];
                        if (sub->kind == LOW_CST_GROUP) nested = true;
                        else if (sub->kind == LOW_CST_ATOM) {
                            proven_u8str_view_t s2 = sub->tok.lex;
                            if (veq(s2, "some") || veq(s2, "none") || veq(s2, "ok") || veq(s2, "error")) nested = true;
                            // ★ `case error <갈래>` 의 이름이 **선언된 갈래**면 그것은 묶음이
                            //   아니라 안쪽 패턴이다(결함 노트 #52) — 망라도 그렇게 센다.
                            else if (ck_name_is_variant(pr, s2)) nested = true;
                        }
                    }
                    // ★★ **MM7b — 중첩도 센다**(결함 노트 #42, 2026-09-16). 전에는 중첩 arm 을
                    //   통째로 건너뛰어, `case ok (some x)` · `case ok none` · `case error bad` 처럼
                    //   **빠짐없이 가른 match** 가 `_` 를 요구받았다. 쓸모없는 `_` 는 나중에 갈래가
                    //   늘어도 아무 말을 안 하므로, 그것을 강요하는 것은 망라 검사를 **끄게 만든다.**
                    //   ⇒ 바깥 꼬리표마다 안쪽 패턴을 모아, 그 안쪽이 스스로 망라면 바깥을 덮은 것으로 센다.
                    if (nested) {
                        proven_size_t oi = veq(v2, "ok") ? 0 : veq(v2, "error") ? 1
                                         : veq(v2, "some") ? 2 : 3;
                        const low_cst_t *sub = a2->kids[2];
                        // ★ 묶음은 한 겹 더 싸여 온다 — `GROUP > FORM > ATOM <머리>`.
                        const low_cst_t *ih = NULL;
                        if (sub->kind == LOW_CST_ATOM) ih = sub;
                        else if (sub->kind == LOW_CST_GROUP && sub->nkids) {
                            const low_cst_t *g = sub->kids[0];
                            if (g->kind == LOW_CST_ATOM) ih = g;
                            else if (g->kind == LOW_CST_FORM && g->nkids &&
                                     g->kids[0]->kind == LOW_CST_ATOM) ih = g->kids[0];
                        }
                        if (ih) {
                            proven_u8str_view_t iv2 = ih->tok.lex;
                            opt_dom = true;
                            if (veq(iv2, "_")) nst[oi].wild = true;
                            else if (veq(iv2, "some")) nst[oi].some = true;
                            else if (veq(iv2, "none")) nst[oi].none = true;
                            else if (ck_name_is_variant(pr, iv2)) {
                                if (nst[oi].nvar < 16) nst[oi].var[nst[oi].nvar++] = iv2;
                            } else nst[oi].wild = true;   // 맨 이름 = 묶음 ⇒ 나머지를 다 받는다
                        }
                        continue;
                    }
                    opt_dom = true;
                    bool *slot = veq(v2, "some") ? &has_some : veq(v2, "none") ? &has_none
                              : veq(v2, "ok") ? &has_ok : &has_err;
                    if (*slot)
                        emit(out, "E-MATCH-REDUNDANT",
                             "this option/result case is already handled by an earlier arm — DEAD CODE "
                             "(RFC-0020 §6.4)", a2->kids[1]->tok.line);
                    else *slot = true;
                    continue;
                }
                /* ★★★★★ **맨 이름은 언제나 갈래다** (2026-09-03, 소유자 결정 A).
                 *
                 *   그전에는 여기가 MM6: P-BIND 였다 — 갈래도 리터럴도 아닌 한 이름을
                 *   **전체를 잡는 묶음**으로 읽었다. 그래서 갈래 이름을 오타 내면
                 *   「이름이 틀렸다」가 아니라 **모든 것을 잡는 갈래**가 됐고,
                 *   `match` 의 가장 값진 성질인 **빠짐 검사까지 만족**시켰다.
                 *   나중에 갈래를 하나 더 늘려도 아무 말이 없었다 — 그 오타 갈래가
                 *   새 갈래까지 삼키기 때문이다.
                 *
                 *   ⇒ 맨 이름이 선언된 갈래가 아니면 **거절한다.** 무엇이든 잡으려면
                 *     `_` 를 쓴다(그 철자는 이미 「아무거나」로 정해져 있다 — §6.4.11).
                 *   실측(2026-09-03): 저장소 전체에서 갈래가 아닌 맨 이름을 `case` 에
                 *     쓴 자리는 **0** — 이 조임의 비용은 0 이다.
                 *   ☞ *컴파일러가 가장 쉽게 잡을 수 있는 잘못을 잡지 못하는 자리가
                 *     있으면, 그 자리는 규칙이 아니라 구멍이다.* */
                /* ★ **`true`·`false` 는 갈래가 아니지만 값이다** — 참거짓은 유한
                 *   도메인이므로 둘을 다 적으면 `_` 없이도 망라가 된다(골든이 그것을
                 *   지킨다). 맨 이름을 조이며 이 정당한 자리를 물었고, 골든이 잡았다.
                 *   ☞ *조일 때는 조이는 것이 무엇을 함께 잡는지 본다 — 그것을 보는
                 *     눈이 시험이다.* */
                if (veq(v2, "true") || veq(v2, "false")) {
                    lit_int = true;
                    continue;
                }
                /* ★ **뒤에 이름이 더 오면 그것은 해체다** — `case point x a y b .` 는
                 *   짜임 `point` 를 풀어 칸을 묶는 자리이고(MM8), `case val x .` 는 값을
                 *   지닌 갈래를 푸는 자리다(MM3).
                 *   ★★ 처음에는 여기서 `continue` 했는데, 그러면 **정상 갈래까지 세지
                 *     못해** 망라 검사가 무너졌다(정본 §6.6 의 예제가 곧바로 빨개졌다).
                 *     예외는 「이 조임을 건너뛴다」이지 「이 arm 을 없는 셈 친다」가 아니다.
                 *   ☞ *조일 때는 조이는 것이 무엇을 함께 잡는지 본다 — 골든과 정본이
                 *     그것을 세 번 알려 줬다(참거짓 · 해체 · 망라).* */
                /* ★ 해체는 **이름 뒤에 또 이름이 오는가**로 가른다. `nkids` 로 세면
                 *   몸(do…end)이 함께 세어져 보통 갈래도 3 이 된다 — 처음에 그렇게
                 *   써서 망라 검사를 무너뜨렸다(정본 예제가 잡았다).
                 *   ☞ *셈으로 가르기 전에 그 셈이 무엇을 포함하는지 본다.* */
                bool destructuring = (a2->nkids > 2 && a2->kids[2]->kind == LOW_CST_ATOM);
                if (!destructuring && !ck_name_is_variant(pr, v2)) {
                    emit(out, "E-MATCH-UNDEF",
                         "this `case` names something that is not a declared enum variant. A bare "
                         "name here is a VARIANT, not a binding — if you meant \"anything\", write "
                         "`_`. (Before 2026-09-03 a typo here silently became a catch-all and "
                         "satisfied the exhaustiveness check, which is exactly the kind of quiet "
                         "wrongness this language exists to refuse.)",
                         a2->kids[1]->tok.line);
                    continue;
                }
                // ★★ **중복 arm 은 도달불가 = 에러** (RFC-0020 §4 D3·§6.4 RED — Rust 의 warning 보다 엄격).
                //   같은 변형을 두 번 다루면 뒤 arm 은 **죽은 코드**다(앞 arm 이 늘 이긴다). 망라·미정의
                //   변형은 이미 잡는데, 도달불가만 조용히 통과했다. `case red … case red` 를 거절한다.
                bool dup = false;
                for (proven_size_t s = 0; s < nseen; s++)
                    if (proven_u8str_view_eq(seen[s], v2)) { dup = true; break; }
                if (dup)
                    emit(out, "E-MATCH-REDUNDANT",
                         "this `case` names a variant an earlier arm already handles — it is DEAD "
                         "CODE (the first arm always wins). A redundant arm is an ERROR here, not a "
                         "warning: a match you cannot read top-to-bottom as 'each variant once' hides "
                         "a bug (RFC-0020 §6.4). Remove it — or one of the two is a typo for another "
                         "variant", a2->kids[1]->tok.line);
                else
                    seen[nseen++] = v2;
            }
        }
        // ★ MM7b — 안쪽이 스스로 망라면 바깥 꼬리표를 덮은 것으로 센다.
        for (proven_size_t oi = 0; oi < 4; oi++) {
            bool full = nst[oi].wild || (nst[oi].some && nst[oi].none);
            if (!full && nst[oi].nvar) {
                // 첫 갈래가 속한 열거를 찾아 **그 갈래를 다 적었는가** 를 본다.
                const low_cst_t *eb2 = NULL;
                for (proven_size_t z = 0; z < pr->nforms && !eb2; z++) {
                    const low_cst_t *e2 = pr->forms[z];
                    if (e2->kind != LOW_CST_FORM || e2->nkids < 3 ||
                        e2->kids[0]->kind != LOW_CST_ATOM || e2->kids[0]->tok.kw != LOW_KW_ENUM) continue;
                    const low_cst_t *bb = e2->kids[e2->nkids - 1];
                    if (bb->kind != LOW_CST_BLOCK) continue;
                    for (proven_size_t v = 0; v < bb->nkids; v++) {
                        const low_cst_t *vn = bb->kids[v];
                        proven_u8str_view_t nm = (vn->kind == LOW_CST_ATOM) ? vn->tok.lex
                            : (vn->kind == LOW_CST_FORM && vn->nkids && vn->kids[0]->kind == LOW_CST_ATOM)
                                ? vn->kids[0]->tok.lex : (proven_u8str_view_t){ 0 };
                        if (nm.size && proven_u8str_view_eq(nm, nst[oi].var[0])) { eb2 = bb; break; }
                    }
                }
                if (eb2) {
                    bool all = true;
                    for (proven_size_t v = 0; v < eb2->nkids && all; v++) {
                        const low_cst_t *vn = eb2->kids[v];
                        proven_u8str_view_t nm = (vn->kind == LOW_CST_ATOM) ? vn->tok.lex
                            : (vn->kind == LOW_CST_FORM && vn->nkids && vn->kids[0]->kind == LOW_CST_ATOM)
                                ? vn->kids[0]->tok.lex : (proven_u8str_view_t){ 0 };
                        if (!nm.size) continue;
                        bool got = false;
                        for (proven_size_t z = 0; z < nst[oi].nvar; z++)
                            if (proven_u8str_view_eq(nst[oi].var[z], nm)) { got = true; break; }
                        if (!got) all = false;
                    }
                    full = all;
                }
            }
            if (!full) continue;
            if (oi == 0) has_ok = true; else if (oi == 1) has_err = true;
            else if (oi == 2) has_some = true; else has_none = true;
        }
        // ★★ **MM3b: option/result 도메인 망라** (유한 2-변형: some&none · ok&error, 아니면 `_`).
        if (opt_dom) {
            bool full = (has_some && has_none) || (has_ok && has_err);
            if (full) {
                if (has_wild)
                    emit(out, "E-MATCH-REDUNDANT",
                         "this `_` is unreachable — both cases of the option/result are already handled "
                         "(RFC-0020 §6.4)", wild_line);
            } else if (!has_wild)
                emit(out, "E-MATCH-INEXHAUSTIVE",
                     "this `match` on an option/result does not handle every case — add the missing "
                     "`case some`/`case none` (option) or `case ok`/`case error` (result), or a `_` wildcard",
                     m->line);
            continue;
        }
        // ★★ **MM2: 리터럴 도메인 망라** (변형과 섞이지 않은 순수 리터럴 match).
        if (lit_int || lit_bool) {
            if (lit_bool && !lit_int) {
                // bool = 유한 {true,false}: 둘 다 or `_` 여야 망라.
                if (bt && bf) {
                    if (has_wild)
                        emit(out, "E-MATCH-REDUNDANT",
                             "this `_` (wildcard) is unreachable — both `true` and `false` are already "
                             "handled, so the wildcard covers nothing (RFC-0020 §6.4)", wild_line);
                } else if (!has_wild)
                    emit(out, "E-MATCH-INEXHAUSTIVE",
                         "this `match` on a bool does not handle every case — add the missing "
                         "`case true` / `case false`, or a `case _ .` wildcard", m->line);
            } else {
                // ★★ **MM4 타일 망라**: scrutinee 가 부호없는 정수 타입이고, 구간들이 **전 도메인을
                //   타일**하면(빈틈 0) `_` 없이도 망라다. 겹치면 죽은 코드(REDUNDANT). 타입을 못 읽거나
                //   빈틈이 있으면 `_` 필수(보수적).
                bool tiled = false;
                proven_u8 w = 0;
                if (m->nkids == 3 && ck_atom(m->kids[1])) w = ck_uint_width_of(op, m->kids[1]->tok.lex);
                if (w && niv > 0) {
                    // 구간 정렬(삽입정렬 — 최대 64, 필드별 이동)
                    for (proven_size_t a = 1; a < niv; a++) {
                        proven_i64 klo = iv[a].lo, khi = iv[a].hi; proven_size_t b = a;
                        while (b > 0 && iv[b-1].lo > klo) { iv[b].lo = iv[b-1].lo; iv[b].hi = iv[b-1].hi; b--; }
                        iv[b].lo = klo; iv[b].hi = khi;
                    }
                    proven_u64 domainmax = (w >= 64) ? ~0ull : ((1ull << w) - 1);
                    proven_i64 expect = 0; bool overlap = false, gap = false;
                    for (proven_size_t a = 0; a < niv; a++) {
                        if ((proven_u64)iv[a].lo > (proven_u64)expect) { gap = true; break; }      // 빈틈
                        if (iv[a].lo < expect) overlap = true;                                     // 겹침
                        if ((proven_u64)iv[a].hi >= expect) expect = (proven_i64)((proven_u64)iv[a].hi + 1);
                    }
                    if (!gap && (proven_u64)(expect - 1) >= domainmax) tiled = true;
                    if (overlap)
                        emit(out, "E-MATCH-REDUNDANT",
                             "two arms of this `match` cover overlapping integer ranges — the later "
                             "overlap is partly DEAD CODE (RFC-0020 §6.4). Make the ranges disjoint", m->line);
                }
                if (!has_wild && !tiled)
                    emit(out, "E-MATCH-INEXHAUSTIVE",
                         "this `match` on an integer is not exhaustive — the literals/ranges leave a gap, "
                         "so a `case _ .` wildcard (or `else`) is required, OR the ranges must TILE the "
                         "whole domain of the scrutinee's type (e.g. `0 to 127` + `128 to 255` on a u8) "
                         "(RFC-0020 §6.4; MM4 tiling)", m->line);
                else if (has_wild && tiled)
                    emit(out, "E-MATCH-REDUNDANT",
                         "this `_` (wildcard) is unreachable — the ranges already TILE the whole domain "
                         "of the scrutinee's type, so nothing is left (RFC-0081 MM4)", wild_line);
            }
            continue;   // 리터럴 도메인은 enum 검사를 건너뛴다
        }
        // ★★ **MM6: 가드-only match 미망라** — case arm 이 있었으나 **모두 가드**여서 아무 도메인도
        //   덮지 않고, catch-all 도 없으면 → 매칭이 없을 때 조용히 fall-through 한다. RFC-0020 은
        //   match 를 **항상 망라**로 규정하므로 `_`/else 를 요구한다.
        if (saw_case && !has_wild && nseen == 0 && !lit_int && !lit_bool && !opt_dom) {
            emit(out, "E-MATCH-INEXHAUSTIVE",
                 "every arm of this `match` is guarded (`when …`), so none is guaranteed to run — a "
                 "`match` is always exhaustive (RFC-0020 §6.4), add a `case _ .` (or an unguarded arm) "
                 "to cover the case where every guard is false", m->line);
            continue;
        }
        if (!nseen) continue;   // 순수 `_`(변형 없음) = 무엇에나 붙는 망라 catch-all — 검사할 enum 없음

        // 그 변형들이 속한 enum 을 찾는다(첫 변형으로)
        const low_cst_t *eb = NULL;
        for (proven_size_t q = 0; q < pr->nforms && !eb; q++) {
            const low_cst_t *e = pr->forms[q];
            if (e->kind != LOW_CST_FORM || e->nkids < 3 || e->kids[0]->kind != LOW_CST_ATOM) continue;
            if (e->kids[0]->tok.kw != LOW_KW_ENUM) continue;
            const low_cst_t *b2 = e->kids[e->nkids - 1];
            if (b2->kind != LOW_CST_BLOCK) continue;
            for (proven_size_t v = 0; v < b2->nkids; v++) {
                const low_cst_t *vn = b2->kids[v];
                proven_u8str_view_t nm = (vn->kind == LOW_CST_ATOM) ? vn->tok.lex
                    : (vn->kind == LOW_CST_FORM && vn->nkids && vn->kids[0]->kind == LOW_CST_ATOM)
                        ? vn->kids[0]->tok.lex : (proven_u8str_view_t){ 0 };
                if (nm.size && proven_u8str_view_eq(nm, seen[0])) { eb = b2; break; }
            }
        }
        if (!eb) {
            /* ★ 첫 갈래 이름으로 **열거를 못 찾았다.** 그것이 오타일 수도 있고,
             *   짜임 해체(`case point x a y b .`)일 수도 있다 — 뒤엣것은 열거가 아니므로
             *   여기서 못 찾는 것이 정상이다. 위쪽 arm 고리가 이미 오타를 잡으므로
             *   (2026-09-03 소유자 결정 A), 여기서는 **조용히 넘어간다.**
             *   ☞ *같은 잘못을 두 자리에서 물면, 그중 하나는 엉뚱한 것을 문다.* */
            continue;
        }
        // ★ 모든 변형이 다뤄졌는가. `_` 가 있으면 나머지를 catch-all 이 덮으니 미망라를 묻지 않는다.
        proven_size_t total = 0, covered = 0;
        for (proven_size_t v = 0; v < eb->nkids; v++) {
            const low_cst_t *vn = eb->kids[v];
            proven_u8str_view_t nm = (vn->kind == LOW_CST_ATOM) ? vn->tok.lex
                : (vn->kind == LOW_CST_FORM && vn->nkids && vn->kids[0]->kind == LOW_CST_ATOM)
                    ? vn->kids[0]->tok.lex : (proven_u8str_view_t){ 0 };
            if (!nm.size) continue;
            total++;
            bool got = false;
            for (proven_size_t q = 0; q < nseen; q++)
                if (proven_u8str_view_eq(seen[q], nm)) { got = true; break; }
            if (got) covered++;
            else if (!has_wild)
                emit(out, "E-MATCH-INEXHAUSTIVE",
                     "this `match` does not handle every variant of the enum — add the missing "
                     "`case` (that exhaustiveness is what a `match` buys you over an if-chain: "
                     "add a variant later and the compiler finds every place that must change), "
                     "or add a `case _ .` wildcard to cover the rest", m->line);
        }
        // ★ `_` 가 **덮을 게 없으면**(모든 변형이 이미 명시로 다뤄짐) 그 와일드카드는 죽은 코드다.
        //   중복=에러 규율: 아무것도 안 하는 catch-all 은 결정이 아니라 실수다(Rust 도 unreachable 경고).
        if (has_wild && total > 0 && covered == total)
            emit(out, "E-MATCH-REDUNDANT",
                 "this `_` (wildcard) arm is unreachable — every variant of the enum is already "
                 "handled by an explicit `case`, so the wildcard covers nothing. Remove it (RFC-0020 §6.4)",
                 wild_line);
        // ★ 그리고 case 가 그 enum 에 **없는** 변형을 걸면 잡는다
        for (proven_size_t q = 0; q < nseen; q++) {
            bool in_enum = false;
            for (proven_size_t v = 0; v < eb->nkids; v++) {
                const low_cst_t *vn = eb->kids[v];
                proven_u8str_view_t nm = (vn->kind == LOW_CST_ATOM) ? vn->tok.lex
                    : (vn->kind == LOW_CST_FORM && vn->nkids && vn->kids[0]->kind == LOW_CST_ATOM)
                        ? vn->kids[0]->tok.lex : (proven_u8str_view_t){ 0 };
                if (nm.size && proven_u8str_view_eq(nm, seen[q])) { in_enum = true; break; }
            }
            if (!in_enum)
                emit(out, "E-MATCH-UNDEF",
                     "this `case` names a variant that the enum does not contain", m->line);
        }
    }
}

// ★ `stack_new <region> capacity <n>` — region 이름은 **선언된 region 파라미터**여야 한다.
//   IR 은 그 이름을 **지운다**(VM 풀이 아레나를 대신한다). 그래서 아무것도 안 가리켜도
//   조용히 통과했다 — 선언된 이름인데 아무도 확인하지 않았다.
//   지운다는 것이 **검사하지 않아도 된다**는 뜻은 아니다.
static bool ck_is_region_param(const low_cst_t *f, proven_u8str_view_t name) {
    for (proven_size_t j = 2; j + 2 < f->nkids; j++) {
        if (f->kids[j]->kind != LOW_CST_ATOM || !veq(f->kids[j]->tok.lex, "input")) continue;
        if (f->kids[j + 1]->kind != LOW_CST_ATOM ||
            !proven_u8str_view_eq(f->kids[j + 1]->tok.lex, name)) continue;
        // 타입 낱말에 `region` 이 있어야 한다
        for (proven_size_t q = j + 2; q < f->nkids && f->kids[q]->kind == LOW_CST_ATOM; q++) {
            if (ck_clause_word(f->kids[q]->tok.lex)) break;
            if (veq(f->kids[q]->tok.lex, "region")) return true;
        }
        return false;
    }
    return false;
}
// ★★★★★ **슬라이스로의 배타 수정 참조는 서술자 갈아끼우기 말고 아무것도 더 주지 않는다**
//   (2026-08-13, 소유자 결정 D0(c)). `mut slice` 를 그냥 넘기면 **원소는 이미 쓸 수 있다**
//   (서술자는 값으로 복사되지만 가리키는 바이트는 같다). 그러니 `mut ref slice` 가 더 주는
//   능력은 **호출자의 서술자 자체를 바꾸는 것** 하나뿐이고, 그것이 곧 창이다:
//     proc shrink input p mut ref slice u8 . do set p (subslice p 0 0) . end
//   호출자 본문에는 대입이 없는데 길이가 바뀐다 — 분석은 그것을 못 보고 색인 검사를 지웠다
//   (VM 이 E-VM-ANALYSIS 로 고발, 네이티브는 침묵). D0(a) 가 사실을 죽여 **건전성은 회복**했고,
//   이 검사는 그 **창 자체를 닫는다**: 길이가 달라지는 것은 **반환값으로** 말한다.
//   ★ 막는 것은 `mut ref` 도 아니고 `mut slice` 도 아니다 — **둘의 조합**뿐이다.
//   ☞ 저장소 전체(lib · tests)에 이 조합은 **한 번도 없었다** — 능력을 뺏는 것이 아니라
//     쓰이지 않는 위험한 자리를 닫는 것이다.
static void ck_mref_slice(low_check_result_t *out, const low_cst_t *f) {
    for (proven_size_t j = 0; j + 1 < f->nkids; j++) {
        if (f->kids[j]->kind != LOW_CST_ATOM) continue;
        // ★ 철자가 **둘**이다: 선언에서는 `mut ref slice …`, 식에서는 `mut_ref …`.
        //   한쪽만 보면 반쪽만 막힌다 — 실제로 첫 판이 그랬다(선언형을 못 봤다).
        proven_size_t tw = 0;
        if (veq(f->kids[j]->tok.lex, "mut_ref")) tw = j + 1;
        else if (veq(f->kids[j]->tok.lex, "mut") && j + 2 < f->nkids &&
                 f->kids[j + 1]->kind == LOW_CST_ATOM && veq(f->kids[j + 1]->tok.lex, "ref"))
            tw = j + 2;
        else continue;
        const low_cst_t *nx = f->kids[tw];
        if (nx->kind == LOW_CST_ATOM && veq(nx->tok.lex, "slice"))
            emit(out, "E-MREF-SLICE",
                 "`mut ref slice` gives nothing that `mut slice` does not — the elements are "
                 "already writable through a plain `mut slice` (the descriptor is copied, the "
                 "bytes are shared). The ONLY thing it adds is replacing the caller's descriptor, "
                 "which silently changes the length behind the caller's back. Say the new slice "
                 "with a RETURN VALUE instead (SPEC-004 §4.4a: the windows through which someone "
                 "else can change your local are listed, and this one is closed)",
                 f->kids[j]->tok.line);
    }
}

// ★★★ **남은 `array` 는 거절이다** (정본 §6.2.6 (1) · 2026-09-14). 입력 자리의 `array N T` 는 `using` 패스가
//   `slice T` + `requires eq (len x) N .` 으로 바꿔 적었다(`low_using.c`). 여기까지 남은 `array` 는 둘 중 하나다:
//   ① 차례가 틀렸다(`array 4 u64` — 정본은 `array <타입> <개수>`, RFC-0132 C12) · ② 입력이 아닌 자리(출력·지역·칸·별칭)라
//   **길이를 지킬 곳이 없다**. 전엔 둘 다 `slice` 로 조용히 읽혔다 — 길이를 버린 채로.
static bool ck_elem_sized(proven_u8str_view_t t) {
    return veq(t, "u8") || veq(t, "i8") || veq(t, "u16") || veq(t, "i16") || veq(t, "u32") || veq(t, "i32") || veq(t, "u64") ||
           veq(t, "i64") || veq(t, "usize") || veq(t, "isize") || veq(t, "f32") || veq(t, "f64") || veq(t, "bool");
}
static unsigned ck_elem_bytes(proven_u8str_view_t t) {
    return veq(t, "u16") || veq(t, "i16") ? 2 : veq(t, "u32") || veq(t, "i32") || veq(t, "f32") ? 4 :
           veq(t, "u64") || veq(t, "i64") || veq(t, "f64") || veq(t, "usize") || veq(t, "isize") ? 8 : 1;
}
// ★★ RFC-0132 T2b-3b (C5 · N1 · N2) — **구조체의 배열 칸** `body array u8 16 .` 은 받는다: 길이가 타입에 있으니 칸이 제
//   바이트를 가진다(구조체를 값으로 베끼면 그 바이트도 베껴진다). 원소는 크기 있는 수·`bool`. 원소가 구조체이거나 배열인 칸은
//   아직 짓지 않았다(`E-LIT-UNBUILT`). 한 구조체의 배열 칸 바이트 합은 틀 한도와 같은 규칙(N2 — `E-FRAME-SIZE`).
static bool ck_struct_array_field(low_check_result_t *out, const low_cst_t *fld, unsigned long long *bytes) {
    if (!(fld->kind == LOW_CST_FORM && fld->nkids >= 2 && ck_atom(fld->kids[0]) && ck_atom(fld->kids[1]) && veq(fld->kids[1]->tok.lex, "array")))
        return false;
    const low_cst_t *t = fld->nkids > 2 ? fld->kids[2] : NULL, *n = fld->nkids > 3 ? fld->kids[3] : NULL;
    if (t && n && ck_atom(t) && ck_atom(n) && n->tok.kind == LOW_TOK_NUMBER && t->tok.kind == LOW_TOK_IDENT && !ck_elem_sized(t->tok.lex)) {
        emit(out, "E-LIT-UNBUILT", "an array field whose elements are structs or arrays is not built yet (RFC-0132 T2b-3) — "
             "today the elements are sized numbers or `bool`", fld->kids[1]->tok.line);
        return true;
    }
    if (!(t && n && ck_atom(t) && ck_atom(n) && n->tok.kind == LOW_TOK_NUMBER)) return false;   // 모양이 틀리면 아래의 거절이 말한다
    unsigned long long cnt = strtoull((const char *)n->tok.lex.ptr, NULL, 0);
    if (cnt == 0) {
        emit(out, "E-TYPE-ARRAY", "an array field holds at least one element — `array <type> 0` has nothing to keep", fld->kids[1]->tok.line);
        return true;
    }
    *bytes += cnt * ck_elem_bytes(t->tok.lex);
    return true;
}
static bool ck_array_walk(low_check_result_t *out, const low_cst_t *nd) {
    if (!nd) return false;
    if (nd->kind == LOW_CST_FORM && nd->nkids >= 3 && ck_atom(nd->kids[0]) && nd->kids[0]->tok.kw == LOW_KW_STRUCT &&
        nd->kids[nd->nkids - 1]->kind == LOW_CST_BLOCK) {
        const low_cst_t *blk = nd->kids[nd->nkids - 1];
        unsigned long long bytes = 0;
        bool hit = false;
        for (proven_size_t j = 0; j < blk->nkids; j++) {
            if (ck_struct_array_field(out, blk->kids[j], &bytes)) continue;
            if (blk->kids[j]->kind != LOW_CST_ATOM && ck_array_walk(out, blk->kids[j])) hit = true;
        }
        if (bytes > low_lbuf_max())
            emit(out, "E-FRAME-SIZE", "this struct's array fields hold more bytes than one value may (RFC-0132 N2 — the same "
                 "rule as a frame): copying the struct copies them all. Keep a large table outside and hold a slice to it", nd->line);
        return hit;
    }
    for (proven_size_t j = 0; j < nd->nkids; j++) {
        const low_cst_t *k = nd->kids[j];
        if (k->kind == LOW_CST_ATOM && k->tok.kind == LOW_TOK_IDENT && veq(k->tok.lex, "array")) {
            const low_cst_t *nx = (j + 1 < nd->nkids) ? nd->kids[j + 1] : NULL;
            bool lit_next = nx && nx->kind == LOW_CST_ATOM && nx->tok.kind == LOW_TOK_NUMBER;
            emit(out, "E-TYPE-ARRAY",
                 !lit_next
                   ? "a fixed-length `array <type> <count>` is accepted as an op INPUT (its length is checked at entry) and "
                     "as a STRUCT FIELD (the record keeps the bytes). In an output, a local type or an alias the length "
                     "would have nowhere to be kept (it used to be dropped silently). Take `slice <type>` and "
                     "state the length in a contract (`requires eq (len x) N .`), or build a local with `var buf be lit array …`"
                   : "`array` is written `array <type> <count>` — the element type first, then the length as a literal "
                     "(`array u64 4`, the same order as `vec u32 4`). The order was flipped on 2026-09-27 (RFC-0132); "
                     "`array 4 u64` is the old order",
                 k->tok.line ? k->tok.line : nd->line);
            return true;
        }
        // ★ 원소 나열 리터럴(`lit array T N …`)의 `array` 는 **값의 타입**이다 — 길이를 리터럴이 쥔다(RFC-0132 T2b-1).
        if (k->kind == LOW_CST_FORM && k->nkids && k->kids[0]->kind == LOW_CST_ATOM && k->kids[0]->tok.kw == LOW_KW_LIT) continue;
        if (k->kind != LOW_CST_ATOM && ck_array_walk(out, k)) return true;
    }
    return false;
}

// ★★ RFC-0132 T2b-1 — **원소 나열 리터럴을 번역 시점에 가린다**: `lit array T N v… [_] .` · `lit slice T v… .`.
//   · 개수(E-LIT-COUNT): 배열은 원소 수가 N 과 같거나, 모자라면 끝이 `_`(나머지 칸 0) 여야 한다. 넘치면 거절.
//     `_` 는 끝에만, 슬라이스에는 없다(슬라이스의 길이는 원소 수다). 길이 N 은 정수 리터럴이다.
//   · 폭(E-TYPE-WIDTH): 원소가 T 에 들어가야 한다 — 부호 없는 T 에 음수도 안 된다(X-0074 의 교훈: 폭 검사를
//     빠져나가는 리터럴이 없어야 한다). bool 원소는 `true`/`false` 만.
//   · 아직 없는 것(E-LIT-UNBUILT): 칸 골라 채우기(`do … end`) · `lit vec` · 크기 없는 원소 타입 — T2b-3 에서 짓는다.
//     정직하게 거절하고 무엇이 아직인지 말한다. 실행 중 원소는 T2b-2 에서 지었다(틀 안 자리, `ck_lit_frames`).
static bool ck_lit_elem_fits(proven_u8str_view_t ty, proven_u8str_view_t num) {
    bool neg = num.size && num.ptr[0] == '-';
    proven_u8str_view_t mag = neg ? (proven_u8str_view_t){ .ptr = num.ptr + 1, .size = num.size - 1 } : num;
    for (proven_size_t i = 0; i < mag.size; i++) if (mag.ptr[i] == '.' || mag.ptr[i] == 'e' || mag.ptr[i] == 'E') {
        if (!(mag.size > 1 && mag.ptr[0] == '0' && (mag.ptr[1] == 'x' || mag.ptr[1] == 'X'))) return false;   // 소수는 정수 원소가 아니다
    }
    // 크기를 unsigned 128 없이 잰다: 부호 없는 64 비트로 읽고, 넘치면 맞지 않는다.
    unsigned long long v = 0; int base = 10; proven_size_t i = 0;
    if (mag.size > 2 && mag.ptr[0] == '0' && (mag.ptr[1] == 'x' || mag.ptr[1] == 'X')) { base = 16; i = 2; }
    else if (mag.size > 2 && mag.ptr[0] == '0' && (mag.ptr[1] == 'b' || mag.ptr[1] == 'B')) { base = 2; i = 2; }
    for (; i < mag.size; i++) {
        char ch = (char)mag.ptr[i]; if (ch == '_') continue;
        int d = (ch >= '0' && ch <= '9') ? ch - '0' : (ch >= 'a' && ch <= 'f') ? ch - 'a' + 10 : (ch >= 'A' && ch <= 'F') ? ch - 'A' + 10 : 99;
        if (d >= base) return false;
        if (v > (~0ull - (unsigned long long)d) / (unsigned long long)base) return false;
        v = v * (unsigned long long)base + (unsigned long long)d;
    }
    int bits = veq(ty, "u8") || veq(ty, "i8") ? 8 : veq(ty, "u16") || veq(ty, "i16") ? 16 :
               veq(ty, "u32") || veq(ty, "i32") ? 32 : 64;
    bool sgn = ty.size && ty.ptr[0] == 'i';
    if (!sgn) return !neg && (bits == 64 || v < (1ull << bits));
    unsigned long long lim = 1ull << (bits - 1);                      // 음수는 lim 까지, 양수는 lim-1 까지
    return neg ? v <= lim : v < lim;
}
static void ck_lit_walk(low_check_result_t *out, const low_cst_t *nd) {
    if (!nd) return;
    if (nd->kind == LOW_CST_FORM && nd->nkids >= 2 && nd->kids[0]->kind == LOW_CST_ATOM &&
        nd->kids[0]->tok.kw == LOW_KW_LIT && nd->kids[1]->kind == LOW_CST_ATOM &&
        (veq(nd->kids[1]->tok.lex, "array") || veq(nd->kids[1]->tok.lex, "slice") || veq(nd->kids[1]->tok.lex, "vec")) &&
        !(veq(nd->kids[1]->tok.lex, "vec") && nd->kids[nd->nkids - 1]->kind != LOW_CST_ATOM)) {   // `lit vec t a do … end` 는 구조체 값
        proven_u32 ln = nd->kids[0]->tok.line;
        bool is_array = veq(nd->kids[1]->tok.lex, "array");
        // ★ RFC-0132 T2b-3 — `lit vec T N v… [_] .` 은 SIMD 값이다. 원소 규칙은 배열과 같고(개수 · `_` · 폭), 더해 레인 수
        //   N 은 1~16 의 2 의 거듭제곱, 원소는 크기 있는 수(`bool` 아님)다 — `vec T N` 타입과 같은 규칙.
        if (veq(nd->kids[1]->tok.lex, "vec")) {
            const low_cst_t *vt = nd->nkids > 2 ? nd->kids[2] : NULL, *vn = nd->nkids > 3 ? nd->kids[3] : NULL;
            bool num = vt && ck_atom(vt) && !veq(vt->tok.lex, "bool") &&
                       (veq(vt->tok.lex, "f32") || veq(vt->tok.lex, "f64") || veq(vt->tok.lex, "u8") || veq(vt->tok.lex, "i8") ||
                        veq(vt->tok.lex, "u16") || veq(vt->tok.lex, "i16") || veq(vt->tok.lex, "u32") || veq(vt->tok.lex, "i32") ||
                        veq(vt->tok.lex, "u64") || veq(vt->tok.lex, "i64"));
            long long lanes = vn && ck_atom(vn) && vn->tok.kind == LOW_TOK_NUMBER ? strtoll((const char *)vn->tok.lex.ptr, NULL, 0) : 0;
            if (!num || lanes < 1 || lanes > 16 || (lanes & (lanes - 1))) {
                emit(out, "E-LIT-COUNT", "`lit vec <type> <lanes> …` — the element type is a sized number and the lane count a "
                     "power of two from 1 to 16, the same rules as the type `vec <type> <lanes>`", ln);
                return;
            }
            is_array = true;                                     // 개수 · `_` · 폭은 배열과 같다
        }
        if (nd->nkids < 3 || nd->kids[2]->kind != LOW_CST_ATOM) { emit(out, "E-LIT-UNBUILT", "a list literal needs its element type: `lit array u8 4 1 2 3 4 .`", ln); return; }
        proven_u8str_view_t ty = nd->kids[2]->tok.lex;
        bool flt = veq(ty, "f32") || veq(ty, "f64"), isbool = veq(ty, "bool");
        bool sized = flt || isbool || veq(ty, "u8") || veq(ty, "i8") || veq(ty, "u16") || veq(ty, "i16") || veq(ty, "u32") ||
                     veq(ty, "i32") || veq(ty, "u64") || veq(ty, "i64") || veq(ty, "usize") || veq(ty, "isize");
        if (!sized) {
            emit(out, "E-LIT-UNBUILT", "the element type of a list literal must be a sized scalar today (an integer, `f32`/`f64` "
                 "or `bool`) — arrays of structs come with RFC-0132 T2b-3", ln);
            return;
        }
        proven_size_t i = 3; long long n = -1;
        if (is_array) {
            const low_cst_t *nn = nd->nkids > 3 ? nd->kids[3] : NULL;
            if (!nn || nn->kind != LOW_CST_ATOM || nn->tok.kind != LOW_TOK_NUMBER || !ck_lit_elem_fits(PROVEN_LIT("u32"), nn->tok.lex)) {
                emit(out, "E-LIT-COUNT", "`lit array <type> <length> …` — the length is a plain integer literal, the same one the "
                     "type `array <type> <length>` would carry", ln);
                return;
            }
            n = strtoll((const char *)nn->tok.lex.ptr, NULL, 0);
            i = 4;
        }
        proven_size_t cnt = 0; bool tail_fill = false;
        // ★ RFC-0132 T2b-3 §5.3 — **칸 골라 채우기** `lit array T N do <번호> <값> . … [_ <값> .] end`. 번호는 정수
        //   리터럴이고 길이 안이며 한 번씩만, `_ <값> .` 은 끝에서 «나머지 칸 모두». 모든 칸이 정해져야 한다 —
        //   남는 칸이 있는데 `_` 가 없으면 거절한다(말없이 0 으로 채우지 않는다). 원소 나열과 섞지 않는다.
        if (nd->kids[nd->nkids - 1]->kind == LOW_CST_BLOCK) {
            const low_cst_t *blk = nd->kids[nd->nkids - 1];
            if (!is_array) { emit(out, "E-LIT-COUNT", "filling chosen cells needs a length — a slice literal has none. Write `lit array <type> <length> do … end`", ln); return; }
            if (i != nd->nkids - 1) { emit(out, "E-LIT-COUNT", "a list literal either lists its elements or fills chosen cells (`do … end`) — not both", ln); return; }
            if (n > (1LL << 24)) { emit(out, "E-LIT-COUNT", "this array is too long to fill cell by cell", ln); return; }
            proven_size_t nb = ((proven_size_t)n + 7u) / 8u;
            unsigned char *seen = nb ? calloc(nb, 1) : NULL;
            if (nb && !seen) return;
            long long got = 0; bool rest = false;
            for (proven_size_t q = 0; q < blk->nkids; q++) {
                const low_cst_t *c = blk->kids[q];
                proven_u32 cl = c->kind == LOW_CST_FORM && c->nkids ? c->kids[0]->tok.line : ln;
                if (c->kind != LOW_CST_FORM || c->nkids < 2 || c->kids[0]->kind != LOW_CST_ATOM) {
                    emit(out, "E-LIT-INDEX", "each line of a cell fill is `<index> <value> .` (or `_ <value> .` for the rest)", cl); free(seen); return;
                }
                const low_cst_t *ix = c->kids[0];
                if (ix->tok.kind == LOW_TOK_IDENT && veq(ix->tok.lex, "_")) {
                    if (q + 1 != blk->nkids) { emit(out, "E-LIT-INDEX", "`_ <value> .` («every remaining cell») comes LAST in a cell fill", cl); free(seen); return; }
                    rest = true;
                } else {
                    if (ix->tok.kind != LOW_TOK_NUMBER || !ck_lit_elem_fits(PROVEN_LIT("u32"), ix->tok.lex)) {
                        emit(out, "E-LIT-INDEX", "a cell index is a plain non-negative integer literal — which cells a literal fills is "
                             "known when the program is translated", cl); free(seen); return;
                    }
                    long long k = strtoll((const char *)ix->tok.lex.ptr, NULL, 0);
                    if (k >= n) { emit(out, "E-LIT-INDEX", "this cell index is outside the array — indices run from 0 to length − 1", cl); free(seen); return; }
                    if (seen[k / 8] & (1u << (k % 8))) { emit(out, "E-LIT-INDEX", "this cell is filled twice — each index appears once", cl); free(seen); return; }
                    seen[k / 8] |= (unsigned char)(1u << (k % 8)); got++;
                }
                if (c->nkids == 2 && c->kids[1]->kind == LOW_CST_ATOM) {
                    const low_cst_t *e = c->kids[1];
                    bool tf = e->tok.kw == LOW_KW_TRUE || e->tok.kw == LOW_KW_FALSE;
                    if (tf && !isbool) { emit(out, "E-TYPE-WIDTH", "a `bool` value in a list of numbers — write the number", cl); free(seen); return; }
                    if (e->tok.kind == LOW_TOK_NUMBER && isbool) { emit(out, "E-TYPE-WIDTH", "a `bool` list holds `true`/`false` — a number is not a truth value", cl); free(seen); return; }
                    if (e->tok.kind == LOW_TOK_NUMBER && !flt && !ck_lit_elem_fits(ty, e->tok.lex)) {
                        emit(out, "E-TYPE-WIDTH", "this value does not fit the list's element type (a negative number never fits an unsigned type)", cl); free(seen); return;
                    }
                }
            }
            free(seen);
            if (got < n && !rest)
                emit(out, "E-LIT-COUNT", "some cells are not filled — end the fill with `_ <value> .` to say what every remaining cell "
                     "holds; cells are never filled silently", ln);
            if (rest && got == n)
                emit(out, "E-LIT-COUNT", "`_ <value> .` fills no cell here — every cell is already named. Drop it", ln);
            return;
        }
        for (; i < nd->nkids; i++) {
            const low_cst_t *e = nd->kids[i];
            if (e->kind == LOW_CST_ATOM && e->tok.kind == LOW_TOK_IDENT && veq(e->tok.lex, "_")) {
                if (!is_array || i + 1 != nd->nkids) {
                    emit(out, "E-LIT-COUNT", !is_array
                         ? "`_` («the rest are zero») has no meaning in a SLICE literal — its length IS the number of elements"
                         : "`_` stands only at the END of the list — it means «the remaining cells are zero»", ln);
                    return;
                }
                tail_fill = true; continue;
            }
            if (e->kind == LOW_CST_ATOM && (e->tok.kw == LOW_KW_TRUE || e->tok.kw == LOW_KW_FALSE)) {
                if (!isbool) { emit(out, "E-TYPE-WIDTH", "a `bool` element in a list of numbers — write the number", ln); return; }
                cnt++; continue;
            }
            if (e->kind == LOW_CST_ATOM && e->tok.kind == LOW_TOK_NUMBER) {
                if (isbool) { emit(out, "E-TYPE-WIDTH", "a `bool` list holds `true`/`false` — a number is not a truth value", ln); return; }
                if (!flt && !ck_lit_elem_fits(ty, e->tok.lex)) {
                    emit(out, "E-TYPE-WIDTH", "this element does not fit the list's element type — every element is checked "
                         "against the type written after `lit array`/`lit slice` (a negative number never fits an unsigned type)", ln);
                    return;
                }
                cnt++; continue;
            }
            // ★ RFC-0132 T2b-2 — 실행 중에 계산되는 원소도 된다(틀 안 자리 ⓐ·ⓒ). 그 타입은 타입 검사층이 원소 타입에 댄다.
            cnt++;
        }
        if (is_array && ((long long)cnt > n || ((long long)cnt < n && !tail_fill))) {
            emit(out, "E-LIT-COUNT", (long long)cnt > n
                 ? "more elements than the array's length — the length is a promise the list must keep"
                 : "fewer elements than the array's length — end the list with `_` to say «the rest are zero»; "
                   "cells are never filled silently", ln);
        }
        if (!is_array && cnt == 0) emit(out, "E-LIT-COUNT", "an empty slice literal says nothing — a slice literal lists at least one element", ln);
        return;
    }
    for (proven_size_t j = 0; j < nd->nkids; j++) ck_lit_walk(out, nd->kids[j]);
}

// ★★ RFC-0134 N3 (2026-09-29) — **`export` 의 `link "<C 이름>"` 은 그대로 C 이름이 된다** — 그러니 C 가 받을 수 있는
//   이름인지 번역 시점에 가린다(`E-LINK-NAME`). 기본 이름(`lw_<길이><마디>…`)은 부딪칠 수 없고, `link` 는 저자가
//   고른 약속이다. 방출 C 가 불러오는 헤더의 이름과 부딪치는지는 여기서 보지 않는다 — 그 목록은 대상 기계마다
//   다르고 닫혀 있지 않다(RFC-0134 N3). `extern`(C 를 부르는 쪽)의 `link` 는 남의 이름이라 가리지 않는다.
static const char *const CK_C_RESERVED[] = {
    // C23
    "auto","break","case","char","const","continue","default","do","double","else","enum","extern","float","for",
    "goto","if","inline","int","long","register","restrict","return","short","signed","sizeof","static","struct",
    "switch","typedef","union","unsigned","void","volatile","while","alignas","alignof","bool","constexpr","false",
    "nullptr","static_assert","thread_local","true","typeof","typeof_unqual",
    // C++ (헤더가 `extern "C"` 로 C++ 에서도 쓰인다)
    "and","and_eq","asm","bitand","bitor","catch","char8_t","char16_t","char32_t","class","compl","concept",
    "const_cast","consteval","constinit","co_await","co_return","co_yield","decltype","delete","dynamic_cast",
    "explicit","export","friend","mutable","namespace","new","noexcept","not","not_eq","operator","or","or_eq",
    "private","protected","public","reinterpret_cast","requires","static_cast","template","this","throw","try",
    "typeid","typename","using","virtual","wchar_t","xor","xor_eq", NULL };
static void ck_link_names(low_check_result_t *out, const low_parse_result_t *pr) {
    for (proven_size_t i = 0; i < pr->nforms; i++) {
        const low_cst_t *f = pr->forms[i];
        if (!f || f->kind != LOW_CST_FORM || !f->is_export || f->is_extern || f->nkids < 3) continue;
        for (proven_size_t j = 2; j + 1 < f->nkids; j++) {
            const low_cst_t *a = f->kids[j];
            if (a->kind != LOW_CST_ATOM || !veq(a->tok.lex, "link") || f->kids[j + 1]->kind != LOW_CST_ATOM) continue;
            proven_u8str_view_t n = f->kids[j + 1]->tok.lex;
            const char *why = NULL;
            bool ident = n.size > 0 && !(n.ptr[0] >= '0' && n.ptr[0] <= '9');
            for (proven_size_t q = 0; q < n.size && ident; q++) {
                proven_u8 c = n.ptr[q];
                ident = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
            }
            if (!ident) why = "a C name is ASCII letters, digits and `_`, and does not start with a digit";
            else {
                for (proven_size_t k = 0; CK_C_RESERVED[k] && !why; k++)
                    if (veq(n, CK_C_RESERVED[k])) why = "that is a C or C++ keyword — the header is meant for both";
                if (!why && n.size >= 3 && n.ptr[0] == 'l' && n.ptr[1] == 'w' && n.ptr[2] == '_')
                    why = "`lw_` names belong to Lowent itself — the default C names (`lw_<length><name>…`) and the runtime";
                if (!why && n.size >= 2 && n.ptr[0] == '_' && (n.ptr[1] == '_' || (n.ptr[1] >= 'A' && n.ptr[1] <= 'Z')))
                    why = "C keeps names beginning with `__` or `_` + a capital letter for the implementation";
            }
            if (why) {
                static char lbuf[8][400]; static int li = 0;
                char *b = lbuf[li++ & 7];
                snprintf(b, sizeof lbuf[0], "`link \"%.*s\"` cannot be a C name: %s. Without `link` the export gets the "
                         "default name `lw_<length><module>_<length><name>` (RFC-0134), which never collides",
                         (int)n.size, (const char *)n.ptr, why);
                emit(out, "E-LINK-NAME", b, a->tok.line);
            }
            break;
        }
    }
}

// ★★★ RFC-0132 T2b-2 (2026-09-29) — **틀 안 나열 자리의 수명**(§13.2 ⓐ·ⓒ).
//   ⓐ `var` 에 묶은 나열 · 실행 중 원소가 든 `let` 나열은 op 의 틀에 선언 자리마다 한 칸이다 — 그 **선언이 든 블록**이
//     끝나면(또는 반복이 그 선언을 다시 돌면) 그 칸은 다른 값이 된다. ⓒ 받을 자리 없이 쓰인 틀 안 나열(실행 중 원소 ·
//     `mut` 매개변수로 넘긴 것)은 **그 문장이 끝날 때까지**만 산다.
//   ⇒ 그 자리를 보는 값이 **더 바깥 블록의 이름**(매개변수 · 모듈 이름 포함, 그 칸이나 원소)에 담기거나, `return` 되거나,
//     actor 에게 보내지면 거부한다(`E-LIT-ESCAPE`). 문장 임시를 이름에 담는 것도 거부한다. 영역 검사(`ck_region_walk`)와
//     같은 모양이다 — 영역 대신 **선언 블록**이 수명이다. 흐름은 op 호출 요약(R1)을 따라간다.
//   ⇒ `var` 배열 이름은 다시 묶을 수 없다(`E-ARRAY-SET`) — 칸은 `set (index buf i) v` 로 쓴다(§6).
//   ⇒ 한 op 의 틀 안 나열 바이트가 한도를 넘으면 거부한다(`E-FRAME-SIZE`, LOW_LBUF_MAX).
static bool ck_param_mutq(const low_cst_t *f, const low_op_header_t *h, proven_size_t q);   // 아래에 정의
#define LC_INF 1000000
#define LC_MAX 1024
typedef struct {
    struct { proven_u8str_view_t name; int depth; } t[LC_MAX]; proven_size_t nt;        // 이름 → 그것이 보는 틀 자리의 블록 깊이
    struct { proven_u8str_view_t name; int depth; bool scalar; } d[LC_MAX]; proven_size_t nd;  // 선언된 이름 → 블록 깊이
    proven_u8str_view_t arr[LC_MAX]; proven_size_t narr;                                  // `var` 배열 이름
    proven_size_t bytes;                                                                  // 이 op 의 틀 안 나열 바이트
    bool told, over;                                                                      // over = 표가 찼다(부분 검사 대신 거절)
    bool autorel;                                                                         // 블록 끝에 돌려줄 할당기 바이트가 있다(RFC-0135 S2)
    bool scalar_out;                                                                      // op 의 출력이 스칼라 한 낱말 — `return` 이 자리를 싣지 못한다
} lc_t;
static const low_cst_t *lc_list(const low_cst_t *nd) {
    while (nd && nd->kind == LOW_CST_GROUP && nd->nkids == 1) nd = nd->kids[0];
    if (nd && nd->kind == LOW_CST_FORM && nd->nkids >= 3 && ck_atom(nd->kids[0]) && nd->kids[0]->tok.kw == LOW_KW_LIT &&
        ck_atom(nd->kids[1]) && (veq(nd->kids[1]->tok.lex, "array") || veq(nd->kids[1]->tok.lex, "slice")))
        return nd;
    return NULL;
}
static bool lc_const_atom(const low_cst_t *e) {
    return ck_atom(e) && (e->tok.kind == LOW_TOK_NUMBER || e->tok.kw == LOW_KW_TRUE || e->tok.kw == LOW_KW_FALSE || veq(e->tok.lex, "_"));
}
static bool lc_const(const low_cst_t *l) {
    proven_size_t e0 = veq(l->kids[1]->tok.lex, "array") ? 4 : 3;
    for (proven_size_t q = e0; q < l->nkids; q++) {
        const low_cst_t *e = l->kids[q];
        if (e->kind == LOW_CST_BLOCK) {                           // 칸 골라 채우기 — 값이 모두 상수여야 ⓑ
            for (proven_size_t r = 0; r < e->nkids; r++) {
                const low_cst_t *cf = e->kids[r];
                if (!(cf->kind == LOW_CST_FORM && cf->nkids == 2 && lc_const_atom(cf->kids[1]))) return false;
            }
            continue;
        }
        if (!(ck_atom(e) && (e->tok.kind == LOW_TOK_NUMBER || e->tok.kw == LOW_KW_TRUE || e->tok.kw == LOW_KW_FALSE ||
                             veq(e->tok.lex, "_")))) return false;
    }
    return true;
}
static proven_size_t lc_bytes(const low_cst_t *l) {
    proven_size_t esz = ck_atom(l->kids[2]) ? (veq(l->kids[2]->tok.lex, "u16") || veq(l->kids[2]->tok.lex, "i16") ? 2 :
                        veq(l->kids[2]->tok.lex, "u32") || veq(l->kids[2]->tok.lex, "i32") || veq(l->kids[2]->tok.lex, "f32") ? 4 :
                        veq(l->kids[2]->tok.lex, "u64") || veq(l->kids[2]->tok.lex, "i64") || veq(l->kids[2]->tok.lex, "f64") ||
                        veq(l->kids[2]->tok.lex, "usize") || veq(l->kids[2]->tok.lex, "isize") ? 8 : 1) : 1;
    proven_size_t n = 0;
    if (veq(l->kids[1]->tok.lex, "array") && l->nkids > 3 && ck_atom(l->kids[3]) && l->kids[3]->tok.kind == LOW_TOK_NUMBER)
        n = (proven_size_t)strtoull((const char *)l->kids[3]->tok.lex.ptr, NULL, 0);
    else for (proven_size_t q = 3; q < l->nkids; q++) if (!(ck_atom(l->kids[q]) && veq(l->kids[q]->tok.lex, "_"))) n++;
    return ((n * esz) + 7u) & ~(proven_size_t)7u;
}
static int lc_taint(const lc_t *x, proven_u8str_view_t n) {
    for (proven_size_t i = x->nt; i-- > 0; ) if (proven_u8str_view_eq(x->t[i].name, n)) return x->t[i].depth;
    return 0;
}
static int lc_decl(const lc_t *x, proven_u8str_view_t n, bool *scalar) {
    for (proven_size_t i = x->nd; i-- > 0; ) if (proven_u8str_view_eq(x->d[i].name, n)) { if (scalar) *scalar = x->d[i].scalar; return x->d[i].depth; }
    if (scalar) *scalar = false;
    return 0;                                                    // 매개변수 · 모듈 이름 — op 보다 오래 산다
}
static void lc_set_taint(lc_t *x, proven_u8str_view_t n, int depth) {
    for (proven_size_t i = 0; i < x->nt; i++) if (proven_u8str_view_eq(x->t[i].name, n)) { if (depth > x->t[i].depth) x->t[i].depth = depth; return; }
    if (x->nt < LC_MAX) { x->t[x->nt].name = n; x->t[x->nt].depth = depth; x->nt++; } else x->over = true;
}
static int lc_call(const lc_t *x, low_cst_t *const *k, proven_size_t n);
// 이 값이 보는 틀 자리의 가장 깊은 블록(0 = 없음 · LC_INF = 문장 임시)
static bool lc_is_into(const low_cst_t *nd) {             // §13.7 — [using, (send …), <나열>]: 바이트는 할당자의 것
    return nd && nd->kind == LOW_CST_FORM && (nd->nkids == 3 || nd->nkids == 4) && ck_atom(nd->kids[0]) && nd->kids[0]->synth &&
           veq(nd->kids[0]->tok.lex, "using");   // 넷째 자식 = 블록 끝의 돌려주기(RFC-0135 S2)
}
// 구조체 `ty` 의 칸 `fname` 이 배열 칸(`array T N`)인가 — 그 칸에 주는 값은 바이트로 **베껴진다**(T2b-3b).
static bool ck_is_array_field(proven_u8str_view_t ty, proven_u8str_view_t fname) {
    if (!g_ck_pr) return false;
    for (proven_size_t i = ty.size; i-- > 0; ) if (ty.ptr[i] == '.') { ty.ptr += i + 1; ty.size -= i + 1; break; }
    for (proven_size_t i = 0; i < g_ck_pr->nforms; i++) {
        const low_cst_t *f = g_ck_pr->forms[i];
        if (f->kind != LOW_CST_FORM || f->nkids < 3 || !ck_atom(f->kids[0]) || f->kids[0]->tok.kw != LOW_KW_STRUCT ||
            !ck_atom(f->kids[1]) || !proven_u8str_view_eq(f->kids[1]->tok.lex, ty)) continue;
        const low_cst_t *blk = f->kids[f->nkids - 1];
        if (blk->kind != LOW_CST_BLOCK) return false;
        for (proven_size_t j = 0; j < blk->nkids; j++) {
            const low_cst_t *fl = blk->kids[j];
            if (fl->kind == LOW_CST_FORM && fl->nkids >= 2 && ck_atom(fl->kids[0]) && ck_atom(fl->kids[1]) &&
                proven_u8str_view_eq(fl->kids[0]->tok.lex, fname)) return veq(fl->kids[1]->tok.lex, "array");
        }
        return false;
    }
    return false;
}
static bool ck_struct_exists(proven_u8str_view_t ty) {
    if (!g_ck_pr) return false;
    for (proven_size_t i = ty.size; i-- > 0; ) if (ty.ptr[i] == '.') { ty.ptr += i + 1; ty.size -= i + 1; break; }
    for (proven_size_t i = 0; i < g_ck_pr->nforms; i++) {
        const low_cst_t *f = g_ck_pr->forms[i];
        if (f->kind == LOW_CST_FORM && f->nkids >= 3 && ck_atom(f->kids[0]) && f->kids[0]->tok.kw == LOW_KW_STRUCT &&
            ck_atom(f->kids[1]) && proven_u8str_view_eq(f->kids[1]->tok.lex, ty)) return true;
    }
    return false;
}
static int lc_carry(const lc_t *x, const low_cst_t *nd) {
    if (!nd) return 0;
    // 구조체 값 `lit T do <칸> <값> . … end` — 배열 칸에 주는 값은 레코드의 바이트로 베껴지므로 아무것도 들고 가지 않는다
    while (nd->kind == LOW_CST_GROUP && nd->nkids == 1 && nd->kids[0]->kind == LOW_CST_FORM && nd->kids[0]->nkids == 2 &&
           ck_atom(nd->kids[0]->kids[0]) && nd->kids[0]->kids[0]->tok.kw == LOW_KW_LIT) nd = nd->kids[0];
    const low_cst_t *sf = NULL;
    if (nd->kind == LOW_CST_FORM && nd->nkids == 2 && ck_atom(nd->kids[0]) && nd->kids[0]->tok.kw == LOW_KW_LIT &&
        nd->kids[1]->kind == LOW_CST_FORM && nd->kids[1]->nkids >= 2 && ck_atom(nd->kids[1]->kids[0]) &&
        nd->kids[1]->kids[nd->kids[1]->nkids - 1]->kind == LOW_CST_BLOCK) sf = nd->kids[1];
    else if (nd->kind == LOW_CST_FORM && nd->nkids >= 2 && ck_atom(nd->kids[0]) && nd->kids[0]->tok.kw == LOW_KW_NONE &&
             nd->kids[nd->nkids - 1]->kind == LOW_CST_BLOCK && ck_struct_exists(nd->kids[0]->tok.lex)) sf = nd;   // `--flat`: [lit] [T do … end]
    if (sf) {
        const low_cst_t *blk = sf->kids[sf->nkids - 1];
        int r = 0;
        for (proven_size_t j = 0; j < blk->nkids; j++) {
            const low_cst_t *fl = blk->kids[j];
            if (fl->kind == LOW_CST_FORM && fl->nkids >= 2 && ck_atom(fl->kids[0]) &&
                ck_is_array_field(sf->kids[0]->tok.lex, fl->kids[0]->tok.lex)) continue;
            int d = lc_carry(x, fl);
            if (d > r) r = d;
        }
        return r;
    }
    if (lc_is_into(nd)) return lc_carry(x, nd->kids[1]);         // 받은 바이트는 할당자의 것 — 할당자가 틀을 보면 그것도 본다
    if (nd->kind == LOW_CST_ATOM) return nd->tok.kind == LOW_TOK_IDENT ? lc_taint(x, nd->tok.lex) : 0;
    const low_cst_t *l = lc_list(nd);
    if (l) return lc_const(l) ? 0 : LC_INF;                     // 실행 중 원소가 든 나열 = 문장 임시(ⓒ)
    while (nd->kind == LOW_CST_GROUP && nd->nkids == 1) nd = nd->kids[0];
    if (nd->kind == LOW_CST_FORM && nd->nkids >= 1 && ck_atom(nd->kids[0]) && nd->kids[0]->tok.kw == LOW_KW_NONE)
        return lc_call(x, nd->kids, nd->nkids);
    int r = 0;
    for (proven_size_t i = 0; i < nd->nkids; i++) {
        if (i > 0 && ck_atom(nd->kids[i]) && ck_atom(nd->kids[i - 1]) && ck_scalar_head(nd->kids[i - 1]->tok.lex)) continue;
        int di = lc_carry(x, nd->kids[i]);
        if (di > r) r = di;
    }
    return r;
}
// 머리 + 인자 줄(중첩된 FORM 이든, `--flat` 의 납작한 문장 꼬리든 같은 가름).
static int lc_call(const lc_t *x, low_cst_t *const *k, proven_size_t n) {
    {
        if (ck_scalar_head(k[0]->tok.lex)) return 0;
        const low_opinfo_t *op = ck_r1_tab ? ck_find_callee(ck_r1_tab, ck_r1_nt, k[0]->tok.lex) : NULL;
        if (op) {
            low_op_header_t hc = op->form ? low_op_header(op->form) : (low_op_header_t){ 0 };
            // 스칼라를 돌려주는 op 은 바이트를 들고 나갈 수 없다(영역 검사와 같은 가름)
            if (op->form && hc.out_e == hc.out_s + 1 && hc.out_s < op->form->nkids && ck_atom(op->form->kids[hc.out_s]) &&
                ck_type_word_scalar(op->form->kids[hc.out_s]->tok.lex)) return 0;
            unsigned long long all = ~0ull;
            bool sum = ck_r1_flow_mask(op, &all);
            int r = 0;
            for (proven_size_t q = 1; q < n; q++) {
                int dq = lc_carry(x, k[q]);
                const low_cst_t *lq = lc_list(k[q]);
                if (lq && op->form && q - 1 < hc.np && ck_param_mutq(op->form, &hc, q - 1)) dq = LC_INF;   // `mut` 에 넘긴 나열 = ⓒ
                if (sum && q - 1 < 64 && !((all >> (q - 1)) & 1ull)) continue;                             // 결과로 안 흐른다
                if (dq > r) r = dq;
            }
            return r;
        }
    }
    int r = 0;
    for (proven_size_t i = 1; i < n; i++) { int di = lc_carry(x, k[i]); if (di > r) r = di; }
    return r;
}
// 문장의 값 꼬리 kids[from..] — 납작하면(`return bump lit … .`) 첫 낱말이 머리다.
static int lc_tail(const lc_t *x, const low_cst_t *nd, proven_size_t from) {
    if (from + 1 < nd->nkids && ck_atom(nd->kids[from]) && nd->kids[from]->tok.kind == LOW_TOK_IDENT &&
        nd->kids[from]->tok.kw == LOW_KW_NONE)
        return lc_call(x, nd->kids + from, nd->nkids - from);
    int r = 0;
    for (proven_size_t q = from; q < nd->nkids; q++) { int d = lc_carry(x, nd->kids[q]); if (d > r) r = d; }
    return r;
}
static void lc_escape(low_check_result_t *out, lc_t *x, proven_u32 line, bool temp) {
    if (x->told) return;
    x->told = true;
    emit(out, "E-LIT-ESCAPE", temp
         ? "a list literal with no receiving place lives only until the end of its statement (RFC-0132 §13.2 ⓒ) — here "
           "its bytes are being kept in a name. Bind the list itself (`var buf be lit array … .`) and use that name"
         : "this carries a view of a list literal's frame storage — or of bytes an allocator gets back at the end of the block (RFC-0135 D11) — OUT of the block that declared it (a `return`, a name "
           "declared further out, a parameter's field or element, or an actor). That storage belongs to the declaring "
           "block (RFC-0132 §13.2 ⓐ): after the block — or on the next pass of a loop — the bytes are something else. "
           "Copy what you need into storage that outlives the block, or declare the list further out", line);
}
static void lc_walk(low_check_result_t *out, lc_t *x, const low_cst_t *nd, int depth) {
    if (!nd || nd->kind == LOW_CST_ATOM) return;
    if (lc_is_into(nd) && nd->nkids == 4) {                    // ★ RFC-0135 S2 — 넷째 자식(블록 끝의 돌려주기)은 처리기가 넣은 것이다
        for (proven_size_t i = 0; i < 3; i++) lc_walk(out, x, nd->kids[i], depth);
        return;
    }
    if (nd->kind == LOW_CST_BLOCK) {
        proven_size_t nt0 = x->nt, nd0 = x->nd, na0 = x->narr;
        for (proven_size_t i = 0; i < nd->nkids; i++) lc_walk(out, x, nd->kids[i], depth + 1);
        x->nt = nt0; x->nd = nd0; x->narr = na0;                // 블록을 나가면 그 안의 이름은 없다
        return;
    }
    if (nd->kind == LOW_CST_FORM && nd->nkids >= 2 && ck_atom(nd->kids[0])) {
        low_kw_t kw = nd->kids[0]->tok.kw;
        if ((kw == LOW_KW_LET || kw == LOW_KW_VAR) && ck_atom(nd->kids[1])) {
            proven_size_t be = nd->nkids;
            for (proven_size_t q = 2; q < nd->nkids; q++) if (ck_atom(nd->kids[q]) && nd->kids[q]->tok.kw == LOW_KW_BE) { be = q; break; }
            bool scalar = be == 3 && ck_atom(nd->kids[2]) && ck_type_word_scalar(nd->kids[2]->tok.lex);
            const low_cst_t *l = (be + 2 == nd->nkids) ? lc_list(nd->kids[be + 1]) : NULL;
            if (l) {
                if (kw == LOW_KW_VAR || !lc_const(l)) {         // ⓐ 이 블록의 틀 안 자리
                    lc_set_taint(x, nd->kids[1]->tok.lex, depth);
                    if (kw == LOW_KW_VAR) { if (x->narr < LC_MAX) x->arr[x->narr++] = nd->kids[1]->tok.lex; else x->over = true; }
                }
                for (proven_size_t q = 3; q < l->nkids; q++) {   // 원소 속의 흐름(원소는 칸에 복사된다 — 원소 자체가 나열이면 안 된다)
                    if (lc_carry(x, l->kids[q]) >= LC_INF) lc_escape(out, x, nd->kids[0]->tok.line, true);
                }
            } else if (!scalar) {
                // ★ RFC-0135 S2 (D11) — 블록 끝에서 돌려줄 할당기 바이트는 이 블록의 것이다(밖으로 나가면 돌려준 뒤를 본다)
                const low_cst_t *uv = be + 2 == nd->nkids ? nd->kids[be + 1] : NULL;
                while (uv && uv->kind == LOW_CST_GROUP && uv->nkids == 1) uv = uv->kids[0];
                if (lc_is_into(uv) && uv->nkids == 4) lc_set_taint(x, nd->kids[1]->tok.lex, depth);
                int dv = lc_tail(x, nd, be + 1);
                if (dv >= LC_INF) lc_escape(out, x, nd->kids[0]->tok.line, true);
                else if (dv > 0) lc_set_taint(x, nd->kids[1]->tok.lex, dv);
            }
            if (x->nd < LC_MAX) { x->d[x->nd].name = nd->kids[1]->tok.lex; x->d[x->nd].depth = depth; x->d[x->nd].scalar = scalar; x->nd++; }
            else x->over = true;
        } else if (kw == LOW_KW_SET && nd->nkids >= 3) {
            const low_cst_t *tg = nd->kids[1];
            bool bare = ck_atom(tg);
            for (int hop = 0; tg && hop < 16; hop++) {
                if (tg->kind == LOW_CST_GROUP) tg = tg->nkids ? tg->kids[0] : NULL;
                else if (tg->kind == LOW_CST_FORM && tg->nkids >= 2) tg = tg->kids[1];
                else break;
            }
            if (tg && ck_atom(tg)) {
                proven_u8str_view_t base = tg->tok.lex;
                if (bare) for (proven_size_t i = 0; i < x->narr; i++)
                    if (proven_u8str_view_eq(x->arr[i], base)) {
                        emit(out, "E-ARRAY-SET", "an array's storage cannot be re-pointed: `set <array> …` would leave its cells "
                             "behind. Write the cells — `set (index buf i) v .` (RFC-0132 §6)", nd->kids[0]->tok.line);
                        break;
                    }
                bool bscalar = false; int bd = lc_decl(x, base, &bscalar);
                if (!(bare && bscalar)) {
                    int dv = lc_tail(x, nd, 2);
                    if (dv >= LC_INF) lc_escape(out, x, nd->kids[0]->tok.line, true);
                    else if (dv > bd) lc_escape(out, x, nd->kids[0]->tok.line, false);
                    else if (dv > 0 && bare) lc_set_taint(x, base, dv);
                }
            }
        } else if (kw == LOW_KW_RETURN && !x->scalar_out) {   // 스칼라를 돌려주면 바이트 자리는 따라 나가지 못한다(X-0086)
            int dv = lc_tail(x, nd, 1);
            if (dv > 0) lc_escape(out, x, nd->kids[0]->tok.line, dv >= LC_INF);
        }
        for (proven_size_t j = 0; j + 1 < nd->nkids; j++) {   // actor 에게 건넨다 — actor 가 그 자리보다 오래 살 수 있다
            if (!ck_atom(nd->kids[j]) || !veq(nd->kids[j]->tok.lex, "send")) continue;
            // ★ 받는 actor 가 **같은 블록이나 더 안쪽**에 선언된 지역이면 나열보다 오래 살지 못한다 — 건네도 된다
            //   (틀 안 배열을 뒤받침으로 준 범프 할당자 `send bb init buf`). 매개변수 · 칸 · 모듈 이름은 깊이 0 이다.
            const low_cst_t *tg = nd->kids[j + 1];
            int ad = ck_atom(tg) && tg->tok.kind == LOW_TOK_IDENT ? lc_decl(x, tg->tok.lex, NULL) : 0;
            for (proven_size_t q = j + 2; q < nd->nkids; q++) {
                int dq = lc_carry(x, nd->kids[q]);
                if (dq > 0 && (dq >= LC_INF || ad < dq)) { lc_escape(out, x, nd->kids[j]->tok.line, false); break; }
                if (dq > 0 && ck_atom(tg)) lc_set_taint(x, tg->tok.lex, dq);   // 이제 그 actor 가 주는 것도 이 틀을 본다
            }
        }
    }
    for (proven_size_t i = 0; i < nd->nkids; i++) lc_walk(out, x, nd->kids[i], depth);
}
// 틀 안 나열 바이트를 센다 — `var` 에 묶은 것 · 실행 중 원소가 든 것 · `mut` 매개변수로 넘긴 것(IR 과 같은 가름).
static void lc_count(lc_t *x, const low_cst_t *nd, bool forced) {
    if (!nd || nd->kind == LOW_CST_ATOM) return;
    if (lc_is_into(nd)) {                                        // 틀을 쓰지 않는다 — 원소 속만 센다
        if (nd->nkids == 4) x->autorel = true;                   // 그래도 그 바이트의 수명은 본다(블록 끝에 돌려준다)
        lc_count(x, nd->kids[1], false);
        const low_cst_t *l = lc_list(nd->kids[2]);
        if (l) for (proven_size_t q = 3; q < l->nkids; q++) lc_count(x, l->kids[q], false);
        return;
    }
    const low_cst_t *l = lc_list(nd);
    if (l) {
        if (forced || !lc_const(l)) x->bytes += lc_bytes(l);
        for (proven_size_t q = 3; q < l->nkids; q++) lc_count(x, l->kids[q], false);
        return;
    }
    if (nd->kind == LOW_CST_FORM && nd->nkids >= 3 && ck_atom(nd->kids[0]) && nd->kids[0]->tok.kw == LOW_KW_VAR) {
        proven_size_t be = nd->nkids;
        for (proven_size_t q = 2; q < nd->nkids; q++) if (ck_atom(nd->kids[q]) && nd->kids[q]->tok.kw == LOW_KW_BE) { be = q; break; }
        for (proven_size_t q = 0; q < nd->nkids; q++) lc_count(x, nd->kids[q], q == be + 1 && be + 2 == nd->nkids);
        return;
    }
    // 머리는 첫 자리(중첩) 또는 문장 꼬리의 첫 낱말(`--flat`) — 그 뒤가 인자다.
    const low_opinfo_t *op = NULL; low_op_header_t hc = { 0 }; proven_size_t h0 = 0;
    if (nd->kind == LOW_CST_FORM && ck_r1_tab)
        for (proven_size_t j = 0; j + 1 < nd->nkids && !op; j++)
            if (ck_atom(nd->kids[j]) && nd->kids[j]->tok.kind == LOW_TOK_IDENT && nd->kids[j]->tok.kw == LOW_KW_NONE) {
                op = ck_find_callee(ck_r1_tab, ck_r1_nt, nd->kids[j]->tok.lex);
                h0 = j;
                if (op && op->form) hc = low_op_header(op->form);
                if (!op) break;
            }
    for (proven_size_t q = 0; q < nd->nkids; q++)
        lc_count(x, nd->kids[q], op && op->form && q > h0 && q - h0 - 1 < hc.np && ck_param_mutq(op->form, &hc, q - h0 - 1));
}
// 이 몸에서 부르는 op 을 따라가 `target` 에 닿는가(재귀 고리) — 한 번 본 op 은 다시 안 본다.
static bool lc_reaches(const low_cst_t *nd, const low_cst_t *target, unsigned char *seen, int depth) {
    if (!nd || depth > 64) return false;
    if (nd->kind == LOW_CST_ATOM) {
        if (nd->tok.kind != LOW_TOK_IDENT || nd->tok.kw != LOW_KW_NONE || !ck_r1_tab) return false;
        const low_opinfo_t *op = ck_find_callee(ck_r1_tab, ck_r1_nt, nd->tok.lex);
        if (!op || !op->form) return false;
        if (op->form == target) return true;
        proven_size_t oi = (proven_size_t)(op - ck_r1_tab);
        if (oi >= 4096 || seen[oi]) return false;
        seen[oi] = 1;
        return lc_reaches(op->body, target, seen, depth + 1);
    }
    for (proven_size_t i = 0; i < nd->nkids; i++) if (lc_reaches(nd->kids[i], target, seen, depth)) return true;
    return false;
}
// ★★ RFC-0132 T2b-3 §13.3 (L4) — 같은 내용의 읽기 전용 리터럴(ⓑ) 둘이 **같은 자리인지는 정하지 않는다**. 그러니
//   두 쪽이 모두 ⓑ 리터럴에서 왔다는 것이 번역 시점에 보이는 «같은 자리인가» 물음(`same_slice`)은 거절한다
//   (`E-LIT-IDENTITY`) — 답이 처리기·최적화·뒤끝(VM/네이티브)마다 다를 수 있다. 보이는 것만 본다: 문자열
//   리터럴 · 상수 나열 · 그런 값에 `let` 으로 묶은 이름 · 그것의 `subslice`. op 경계를 건넌 뒤는 못 본다(정본이
//   «그 답에 기대는 프로그램은 적합하지 아니하다» 로 적는다).
#define LI_MAX 256
typedef struct { proven_u8str_view_t n[LI_MAX]; proven_size_t nn; } li_t;
static bool li_ro(const li_t *x, const low_cst_t *e) {
    while (e && e->kind == LOW_CST_GROUP && e->nkids == 1) e = e->kids[0];
    if (!e) return false;
    if (e->kind == LOW_CST_ATOM) {
        if (e->tok.kind == LOW_TOK_STRING) return true;
        if (e->tok.kind == LOW_TOK_IDENT)
            for (proven_size_t i = 0; i < x->nn; i++) if (proven_u8str_view_eq(x->n[i], e->tok.lex)) return true;
        return false;
    }
    const low_cst_t *l = lc_list(e);
    if (l) return lc_const(l);
    if (e->kind == LOW_CST_FORM && e->nkids >= 2 && ck_atom(e->kids[0]) && veq(e->kids[0]->tok.lex, "subslice"))
        return li_ro(x, e->kids[1]);
    return false;
}
static void li_walk(low_check_result_t *out, li_t *x, const low_cst_t *nd) {
    if (!nd || nd->kind == LOW_CST_ATOM) return;
    if (nd->kind == LOW_CST_FORM && nd->nkids >= 3 && ck_atom(nd->kids[0]) && nd->kids[0]->tok.kw == LOW_KW_LET && ck_atom(nd->kids[1])) {
        proven_size_t be = nd->nkids;
        for (proven_size_t q = 2; q < nd->nkids; q++) if (ck_atom(nd->kids[q]) && nd->kids[q]->tok.kw == LOW_KW_BE) { be = q; break; }
        proven_size_t nt = nd->nkids - (be + 1);
        bool ro = (nt == 1 && li_ro(x, nd->kids[be + 1])) ||
                  (nt == 3 && ck_atom(nd->kids[be + 1]) && veq(nd->kids[be + 1]->tok.lex, "slice") && ck_atom(nd->kids[be + 3]) &&
                   nd->kids[be + 3]->tok.kind == LOW_TOK_STRING);
        if (ro && x->nn < LI_MAX) x->n[x->nn++] = nd->kids[1]->tok.lex;
    }
    for (proven_size_t j = 0; j + 2 < nd->nkids; j++) {
        if (!ck_atom(nd->kids[j]) || !veq(nd->kids[j]->tok.lex, "same_slice")) continue;
        if (li_ro(x, nd->kids[j + 1]) && li_ro(x, nd->kids[j + 2]))
            emit(out, "E-LIT-IDENTITY", "both sides of this `same_slice` come from read-only literals — whether two equal "
                 "literals share one place is deliberately left open (RFC-0132 §13.3), so the answer could differ between "
                 "translators, optimisation levels and the VM and native builds. Compare the CONTENTS instead", nd->kids[j]->tok.line);
    }
    for (proven_size_t i = 0; i < nd->nkids; i++) li_walk(out, x, nd->kids[i]);
}
static void ck_lit_identity(low_check_result_t *out, const low_cst_t *f) {
    static li_t x;
    x.nn = 0;
    li_walk(out, &x, f);
}
static void ck_lit_frames(low_check_result_t *out, const low_cst_t *f) {
    static lc_t x;
    memset(&x, 0, sizeof x);
    lc_count(&x, f, false);
    if (!x.bytes && !x.autorel) return;                          // 틀 안 나열도 돌려줄 바이트도 없으면 볼 수명이 없다
    {
        low_op_header_t h = low_op_header(f);
        proven_size_t nw = 0; proven_u8str_view_t w0 = { 0 };
        for (proven_size_t j = h.out_s; j < h.out_e; j++)
            if (ck_atom(f->kids[j]) && !veq(f->kids[j]->tok.lex, "output")) { if (nw == 0) w0 = f->kids[j]->tok.lex; nw++; }
        x.scalar_out = nw == 1 && ck_type_word_scalar(w0);
    }
    lc_walk(out, &x, f, 0);
    if (x.over)
        emit(out, "E-IR-LIMIT", "too many names in this op for the list-literal lifetime checker's table — refused rather "
             "than checked partly. Split the op", f->line);
    if (x.bytes > low_lbuf_max()) {
        static char fb[320];
        snprintf(fb, sizeof fb, "this op's list literals need %zu bytes of frame storage — more than one op may hold on this "
                 "machine (%u). A frame this large is a stack overflow waiting for a deep call; keep big tables constant (`let`, "
                 "read-only) or take them from an allocator (`using`, RFC-0135)", (size_t)x.bytes, low_lbuf_max());
        emit(out, "E-FRAME-SIZE", fb, f->line);
    } else if (x.bytes && ck_r1_tab) {
        // ★★ RFC-0135 S3 (D7) — 재귀는 깊이만큼 틀을 곱하고, 그 깊이는 번역 시점에 모른다. 그러면 스택이 넘치지 않음을
        //   빌드가 보일 수 없다 — 알림이 아니라 **거절**이다(전엔 W-FRAME-RECURSIVE; X-0084 가 실제로 넘쳤다).
        static unsigned char seen[4096];
        memset(seen, 0, sizeof seen);
        const low_cst_t *body = f->kids[f->nkids - 1];
        if (body && body->kind == LOW_CST_BLOCK && lc_reaches(body, f, seen, 0)) {
            static char wb[360];
            snprintf(wb, sizeof wb, "this op holds %zu bytes of list-literal frame storage and can call ITSELF (directly or "
                     "through other ops) — every level of the recursion holds its own copy and the depth is not known when "
                     "the program is translated, so the build cannot show the stack is enough. Take the list from an "
                     "allocator inside the recursion (`var buf using al be … . else …`), or make the op not recursive "
                     "(RFC-0135 D7)", (size_t)x.bytes);
            emit(out, "E-FRAME-RECURSIVE", wb, f->line);
        }
    }
}

// ★★ RFC-0135 S3 — **진입 op 마다 스택의 틀 안 나열 최대**. 부르는 관계를 따라 가장 깊은 길의 틀 안 나열 바이트 합을
//   재어, 실행 중 예산(S0 · LOW_SBUDGET_*)을 넘으면 **번역 시점에** 거절한다 — 재귀하는 op 은 틀 안 나열을 가질 수 없으므로
//   (E-FRAME-RECURSIVE) 순환이 있어도 그 길의 합은 정해진다. 실행 중 검사(S0)는 그대로 둔다(그린스레드 · 검사 밖 경로의 안전망).
static proven_size_t ck_op_lbytes(const low_cst_t *f) {
    static lc_t x;
    memset(&x, 0, sizeof x);
    lc_count(&x, f, false);
    return x.bytes;
}
static void sb_callees(const low_cst_t *nd, proven_size_t *out, proven_size_t *n, proven_size_t cap) {
    if (!nd) return;
    if (nd->kind == LOW_CST_ATOM) {
        if (nd->tok.kind != LOW_TOK_IDENT || nd->tok.kw != LOW_KW_NONE || !ck_r1_tab) return;
        const low_opinfo_t *op = ck_find_callee(ck_r1_tab, ck_r1_nt, nd->tok.lex);
        if (!op) return;
        proven_size_t oi = (proven_size_t)(op - ck_r1_tab);
        for (proven_size_t i = 0; i < *n; i++) if (out[i] == oi) return;
        if (*n < cap) out[(*n)++] = oi;
        return;
    }
    for (proven_size_t i = 0; i < nd->nkids; i++) sb_callees(nd->kids[i], out, n, cap);
}
static unsigned long long sb_path(proven_size_t oi, unsigned long long *memo, unsigned char *state, const proven_size_t *own, int depth) {
    if (state[oi] == 2) return memo[oi];
    if (state[oi] == 1 || depth > 256) return 0;               // 순환 — 재귀하는 op 은 틀 안 나열이 없다(E-FRAME-RECURSIVE)
    state[oi] = 1;
    proven_size_t local[256]; proven_size_t nl = 0;
    sb_callees(ck_r1_tab[oi].body, local, &nl, 256);
    unsigned long long best = 0;
    for (proven_size_t i = 0; i < nl; i++) { unsigned long long p = sb_path(local[i], memo, state, own, depth + 1); if (p > best) best = p; }
    memo[oi] = own[oi] + best; state[oi] = 2;
    return memo[oi];
}
static void ck_stack_bound(low_check_result_t *out) {
    if (!ck_r1_tab || !ck_r1_nt) return;
    proven_size_t n = ck_r1_nt;
    unsigned long long *memo = calloc(n, sizeof *memo);
    unsigned char *state = calloc(n, 1), *called = calloc(n, 1);
    proven_size_t *own = calloc(n, sizeof *own);
    if (!memo || !state || !called || !own) { free(memo); free(state); free(called); free(own); return; }
    bool any = false;
    for (proven_size_t i = 0; i < n; i++) {
        own[i] = ck_r1_tab[i].form ? ck_op_lbytes(ck_r1_tab[i].form) : 0;
        if (own[i]) any = true;
    }
    if (any) {
        for (proven_size_t i = 0; i < n; i++) {
            proven_size_t local[256]; proven_size_t nl = 0;
            sb_callees(ck_r1_tab[i].body, local, &nl, 256);
            for (proven_size_t k = 0; k < nl; k++) if (local[k] != i) called[local[k]] = 1;
        }
        long budget = low_ir_target()->no_heap ? LOW_SBUDGET_FREE : LOW_SBUDGET_HOST;
        for (proven_size_t i = 0; i < n; i++) {
            if (called[i] || !ck_r1_tab[i].form) continue;          // 진입(아무도 부르지 않는 op)만 — 길은 거기서 시작한다
            unsigned long long p = sb_path(i, memo, state, own, 0);
            if (p > (unsigned long long)budget) {
                static char sbuf[360];
                snprintf(sbuf, sizeof sbuf, "the deepest call path from this op keeps %llu bytes of list literals on the stack at "
                         "once — more than the stack budget on this machine (%ld). The program would stop there at run time; "
                         "take the big lists from an allocator (`using`), or split the path (RFC-0135 S3)", p, budget);
                emit(out, "E-STACK-BUDGET", sbuf, ck_r1_tab[i].form->line);
            }
        }
    }
    free(memo); free(state); free(called); free(own);
}

static void ck_regions(low_check_result_t *out, const low_cst_t *f, const low_cst_t *nd) {
    if (!nd) return;
    for (proven_size_t j = 0; j + 1 < nd->nkids; j++) {
        if (nd->kids[j]->kind != LOW_CST_ATOM || !veq(nd->kids[j]->tok.lex, "stack_new")) continue;
        const low_cst_t *rn = nd->kids[j + 1];
        if (rn->kind != LOW_CST_ATOM) continue;
        if (veq(rn->tok.lex, "capacity")) continue;        // region 을 생략한 형태
        if (!ck_is_region_param(f, rn->tok.lex))
            emit(out, "E-REGION-UNDEF",
                 "`stack_new` names something that is not a `region` parameter of this op "
                 "(the lowering ERASES this name — that does not mean it need not be checked)",
                 nd->line);
    }
    for (proven_size_t j = 0; j < nd->nkids; j++) ck_regions(out, f, nd->kids[j]);
}


// ★★★ RFC-0057 — **하나의 정규화된 의미 요약.** 검사기·문서·감사가 이것만 부른다.
//   (효과 추론이 세 곳에 흩어져 각자 다른 낱말을 알았다. 문서는 `print` 둘만 알아서,
//    검사기가 거절하는 op 을 **"효과 없음"** 이라 적었다. 경로가 갈리면 하나는 반드시 썩는다.)
const char *low_effect_bit_name(unsigned bit) {
    switch (bit) {
        case EFF_IO:     return "io";
        case EFF_ALLOC:  return "alloc";
        case EFF_HEAP:   return "heap";
        case EFF_STATE:  return "state";
        case EFF_PANIC:  return "panic";
        case EFF_UNSAFE: return "unsafe";
        case EFF_ATOMIC: return "atomic";
        case EFF_CONCURRENT: return "concurrent";
        case EFF_WAIT:   return "wait";
        case EFF_LOCK:   return "lock";
        case EFF_DEVICE: return "device";
        case EFF_PAGEFAULT: return "page_fault";
        case EFF_BLOCKING:  return "blocking";
        case EFF_CANCEL: return "cancel";
        case EFF_DETACH: return "detach";
        default:         return "none";
    }
}
low_op_summary_t low_op_summary(const low_parse_result_t *pr, const low_cst_t *f) {
    low_op_summary_t s = { 0 };
    if (!f || f->kind != LOW_CST_FORM || f->nkids < 2 || f->kids[0]->kind != LOW_CST_ATOM)
        return s;
    low_kw_t kw = f->kids[0]->tok.kw;
    if (kw != LOW_KW_FN && kw != LOW_KW_PROC) return s;
    s.is_calc = (kw == LOW_KW_FN);

    bool found = false;
    unsigned de = decl_effect(f, &found);
    s.declared = s.is_calc ? EFF_NONE
               : (found ? de : (EFF_IO | EFF_ALLOC | EFF_STATE | EFF_PANIC | EFF_UNSAFE));
    if (s.is_calc && found) s.declared = de;   // fn 의 선언은 그대로 보고한다(위반은 따로 잡는다)

    // 호출을 타고 전파되는 실제 효과 — op 표가 필요하다(같은 규칙, 같은 코드).
    const low_cst_t *body = (f->kids[f->nkids - 1]->kind == LOW_CST_BLOCK)
                              ? f->kids[f->nkids - 1] : NULL;
    if (pr && body) {
        low_opinfo_t tab[256]; proven_size_t n = 0;
        for (proven_size_t i = 0; i < pr->nforms && n < 256; i++) {
            const low_cst_t *g = pr->forms[i];
            if (g->kind != LOW_CST_FORM || g->nkids < 2 || g->kids[0]->kind != LOW_CST_ATOM) continue;
            low_kw_t k2 = g->kids[0]->tok.kw;
            if (k2 != LOW_KW_FN && k2 != LOW_KW_PROC) continue;
            bool f2 = false; unsigned d2 = decl_effect(g, &f2);
            bool c2 = (k2 == LOW_KW_FN);
            tab[n].name = g->kids[1]->tok.lex;
            tab[n].is_calc = c2;
            tab[n].declared = c2 ? EFF_NONE
                                 : (f2 ? d2 : (EFF_IO | EFF_ALLOC | EFF_STATE | EFF_PANIC | EFF_UNSAFE));
            tab[n].form = g;
            tab[n].body = (g->kids[g->nkids - 1]->kind == LOW_CST_BLOCK)
                            ? g->kids[g->nkids - 1] : NULL;
            n++;
        }
        s.actual = walk_effects(body, tab, n);

        // ★ 호출자에게 보이는 쓰기 — `mut` 파라미터에 쓰는가(관측적 순수성의 가르는 선).
        for (proven_size_t j = 2; j + 2 < f->nkids && !s.writes_caller; j++) {
            if (f->kids[j]->kind != LOW_CST_ATOM || !veq(f->kids[j]->tok.lex, "input")) continue;
            if (f->kids[j + 1]->kind != LOW_CST_ATOM) continue;
            bool is_mut = false;
            for (proven_size_t q = j + 2; q < f->nkids && f->kids[q]->kind == LOW_CST_ATOM; q++) {
                proven_u8str_view_t w = f->kids[q]->tok.lex;
                if (low_is_clause_word(w)) break;       // ★ 2026-09-15 — `output` 이 앞으로 가서 뒤따르는 절이 무엇이든 멈춘다
                if (veq(w, "mut") || veq(w, "mut_ref")) { is_mut = true; break; }
            }
            if (is_mut && ck_writes_place(body, f->kids[j + 1]->tok.lex)) s.writes_caller = true;
        }
    }
    return s;
}

// ★★ 2026-07-13 에 없앤 여섯 낱말 — 그리고 **왜** 없앴는지, **무엇으로 바꿔 쓰는지.**
static const struct { const char *word; const char *why; } CK_REMOVED[] = {
    // ★ RFC-0132 L1 (2026-09-29, 소유자 «make 보다는 lit 가 낫겠어요»): 값 리터럴의 머리는 `lit` 하나다.
    { "make", "`make` is now `lit` (RFC-0132): one head for every literal value — `lit point do x 1 . y 2 . end`. "
              "Write `lit`" },
    // ★★★ `fn`/`proc` → `fn`/`proc` (RFC-0082). "op" 은 **원시 연산**(add/len, RFC-0016)을
    //   가리키던 낱말인데 정의 키워드도 "op" 이라 **한 낱말에 두 뜻**이었다(§2.5 위반). 또 proc 은
    //   함수가 아니다(효과) — `proc` 이 그것을 정직히 말한다. 순수/효과 1비트는 fn/proc 로 유지.
    { "calcop", "`calcop` is now `fn` (RFC-0082). A pure calculation IS a function, so it is called what "
                "it is — and 'op' now means only a primitive operation (add/len). Write `fn`." },
    { "procop", "`procop` is now `proc` (RFC-0082). A procedure with effects is NOT a mathematical "
                "function, so it is not called `fn`; it is `proc`. The pure/effect bit stays in the name. "
                "Write `proc`." },
    // ★★★ `local` 은 **"기본값" 의 동의어**였다 (2026-07-14, 사용자 결정).
    //   가시성이 강제되기 전에는 아무 뜻도 없었고, 강제한 **뒤에도** 아무 뜻이 없었다:
    //   **`export` 가 아닌 모든 것이 이미 비공개**다. `local` 은 그것을 **한 번 더 말할 뿐**이다.
    //   §2.5 는 동의어를 금지한다 — `give`(=return) · `;`(=.) · `\`(=줄잇기) 를 지운 그 이유다.
    { "local", "`local` said what the DEFAULT already says: everything that is not `export`ed is "
               "private. It bought nothing — and a word that buys nothing still costs the spelling "
               "in every namespace (P1: keep the vocabulary small and orthogonal). Just drop it" },
    { "is",   "`is` was DECORATION — nothing ever read it (`type h zzz u8 .` used to check clean). "
              "Write `type <name> <type> .`" },
    { "as",   "`as` carried no weight anywhere it appeared: `use M as N` was never enforced, and a "
              "block label `do as <name>` was parsed and THROWN AWAY. Write `use <module> .`" },
    { "give", "`give` was an exact SYNONYM of `return` — both returned the same value — and "
              "SPEC-002 §2.5 forbids synonyms. (The spec meant it as a block's tail VALUE, but "
              "blocks are not values yet. When expression-oriented blocks land, it comes back.) "
              "Write `return`" },
    { "fail", "`fail` is not in the spec's vocabulary AT ALL — it only survived inside the old "
              "tree-walking interpreter, which is gone. Write `return error <variant>` or `panic`" },
    { "unit", "the spec calls it `void`, the tool called it `unit` — one meaning, two spellings. "
              "Write `void`" },
    // ★★ 2026-07-13 (2차)
    { "to",   "infix access is gone. `a to b` meant EXACTLY what `field a b` means — access had "
              "FOUR spellings for one meaning (prefix · `to` · `in` · the glued dot), and they had "
              "already DIVERGED: the glued dot could not index. Write `field a b` / `index a i` — the glued "
              "dot (`a.b`) is refused too (`E-FIELD-GLUED`). (`send` now reads `send <actor> <message> [args…]` — "
              "the actor comes FIRST, because a message IS an op call and the instance IS its first "
              "parameter)" },
    { "in",   "`in` is gone. In a loop write `for <name> <slice> do` — the slice follows the name and "
              "`do` marks the body, so `in` carried nothing. As access, `b in a` was a reverse spelling "
              "of `field a b`: write `field a b` / `index a i` (the glued dot `a.b` is refused too, "
              "`E-FIELD-GLUED`)" },
    { "loop", "`loop` was an exact SYNONYM of `while true .` — SPEC-002 §2.5 forbids synonyms. "
              "Write `while true . do … end`" },
    { "when", "`when` is gone. The `errors` clause now takes ONE error per clause — "
              "`errors <variant> [<condition>] .` — so its arity is FIXED (1 or 2) and no marker is "
              "needed. `when` only existed because the clause was VARIADIC: `errors a b .` already "
              "meant TWO variants, so `errors a <condition>` could not be told apart from it by "
              "arity alone. (Parentheses could not settle it either: in this language parens are "
              "DECORATION — the formatter adds and removes them — and decoration cannot carry "
              "meaning.) Write `errors too_short lt (len data) 4 .` and put each error on its own "
              "clause" },
    { "on",   "`on` is gone. Being INSIDE an actor block already means \"this is a message "
              "handler\" — so `on` said nothing that the block did not. And it did REAL harm: it "
              "bypassed the pure/procedural bit. The IR decides purity by `kw != proc`, so an "
              "`on` handler counted as PURE — a handler that MUTATES the actor's state could "
              "declare `effects none` and nobody objected (a `fn` writing a caller's buffer is "
              "caught by E-EFFECT-PURITY; this was not). RFC-0046 removed the bare-`op` default "
              "precisely so the 1 bit is ALWAYS stated. Write `proc` (it mutates state) or "
              "`fn` (it only reads)" },
};
static void ck_removed_walk(low_check_result_t *out, const low_cst_t *nd) {
    if (!nd) return;
    if (nd->kind == LOW_CST_ATOM) {
        // ★★ **이름 자리에서만 본다.** 이 검사는 낱말이 **어휘로 쓰였는가**를 묻는데,
        //   전엔 토큰 **종류를 안 보고 렉심만** 비교했다 ⇒ 문자열 리터럴 `"on"`·`"give"` 가
        //   제거된 키워드 사용으로 **오탐**됐다(2026-07-19 실측: `return len "on" .` 이 거절됨).
        //   `rem[^\n]*` 가 낱말 경계를 안 봐서 `remove` 를 주석으로 삼킨 것과 **같은 부류**다:
        //   *무엇에 대한 텍스트인지 안 보고 텍스트만 보는 검사.*
        //   ★ 문자열을 실제로 쓰는 픽스처가 없어서 여태 **닿지 않았다** — 픽스처의 모양이
        //     감사의 시야다(교훈 6). 이스케이프·heredoc 을 닫고 나서야 드러났다.
        if (nd->tok.kind != LOW_TOK_IDENT) return;
        for (proven_size_t i = 0; i < sizeof CK_REMOVED / sizeof CK_REMOVED[0]; i++)
            if (veq(nd->tok.lex, CK_REMOVED[i].word))
                emit(out, "E-VOCAB-REMOVED", CK_REMOVED[i].why, nd->tok.line);
        return;
    }
    bool in_case = (nd->kind == LOW_CST_FORM && nd->nkids >= 1 &&
                    nd->kids[0]->kind == LOW_CST_ATOM && nd->kids[0]->tok.kw == LOW_KW_CASE);
    // ★ import 별칭 `use X from "path" as Y .` 의 `as` 는 **제거된 낱말이 아니라 별칭 표식**이다
    //   (RFC-0011, 2026-07-25). `as` 는 어휘에서 다른 뜻이 없으므로(§2.5 위반 아님) `use` 폼 안에서만
    //   재사용한다 — `to`/`when` 이 케이스 arm 에서 재사용되는 것과 같은 규율.
    bool in_use = (nd->kind == LOW_CST_FORM && nd->nkids >= 1 &&
                   nd->kids[0]->kind == LOW_CST_ATOM && nd->kids[0]->tok.kw == LOW_KW_USE);
    for (proven_size_t i = 0; i < nd->nkids; i++) {
        if (in_use && nd->kids[i]->kind == LOW_CST_ATOM && veq(nd->kids[i]->tok.lex, "as"))
            continue;
        // ★ MM4 — 범위 패턴 `case lo to hi .` 의 `to` 는 **제거된 infix-access 가 아니라 범위
        //   표식**이다. `to` 는 지금 어휘에서 다른 뜻이 없으므로(§2.5 위반 아님) 범위에 재사용한다 —
        //   그러나 그 자리(케이스 arm 의 kids[2], 양옆이 정수 리터럴)에서만 어휘 검사를 면제한다.
        if (in_case && i == 2 && nd->nkids >= 4 &&
            nd->kids[2]->kind == LOW_CST_ATOM && veq(nd->kids[2]->tok.lex, "to") &&
            nd->kids[1]->kind == LOW_CST_ATOM && nd->kids[3]->kind == LOW_CST_ATOM &&
            ck_is_int_lit(nd->kids[1]->tok.lex) && ck_is_int_lit(nd->kids[3]->tok.lex))
            continue;
        // ★ MM6 — 가드 `case p when c .` 의 `when` 은 **제거된 errors-절 낱말이 아니라 가드 표식**이다.
        //   `when` 도 어휘에서 다른 뜻이 없으므로 가드에 재사용하되 **케이스 arm 의 구분자 자리**에서만
        //   면제한다(가드 식 `c` 안의 다른 제거 낱말은 그대로 검사된다 — 구분자 원자 하나만 건너뛴다).
        if (in_case && nd->kids[i]->kind == LOW_CST_ATOM && veq(nd->kids[i]->tok.lex, "when"))
            continue;
        ck_removed_walk(out, nd->kids[i]);
    }
}
static void ck_removed_words(low_check_result_t *out, const low_parse_result_t *pr) {
    for (proven_size_t i = 0; i < pr->nforms; i++) ck_removed_walk(out, pr->forms[i]);
}

// ★★ **`let` 은 불변이다.** 그전엔 `let x be u8 5 . set x 7 .` 이 **그냥 통과했다** —
//   즉 `let` 과 `var` 는 **완전한 동의어**였고, SPEC-002 §2.5 는 동의어를 금지한다.
//   두 낱말이 하나의 뜻을 가지면 어휘만 늘고 뜻은 안 는다. 이제 **두 낱말이 두 뜻을 갖는다.**
//   (그리고 이건 공짜가 아니다: 불변 바인딩은 재대입이 없으므로 **구간 분석이 사실을 잃지 않는다.**)
static void ck_immutable_block(low_check_result_t *out, const low_cst_t *blk) {
    if (!blk || blk->kind != LOW_CST_BLOCK) return;
    proven_u8str_view_t lets[64]; proven_size_t nl = 0;
    for (proven_size_t i = 0; i < blk->nkids; i++) {
        const low_cst_t *st = blk->kids[i];
        if (st->kind != LOW_CST_FORM || !st->nkids || st->kids[0]->kind != LOW_CST_ATOM) continue;
        low_kw_t kw = st->kids[0]->tok.kw;
        if (kw == LOW_KW_LET && st->nkids >= 2 && st->kids[1]->kind == LOW_CST_ATOM && nl < 64)
            lets[nl++] = st->kids[1]->tok.lex;
        else if (kw == LOW_KW_SET && st->nkids >= 2 && st->kids[1]->kind == LOW_CST_ATOM) {
            for (proven_size_t j = 0; j < nl; j++)
                if (proven_u8str_view_eq(lets[j], st->kids[1]->tok.lex))
                    emit(out, "E-IMMUTABLE",
                         "this binding was made with `let`, which is IMMUTABLE — `set` cannot "
                         "reassign it. Until now `let` and `var` meant exactly the same thing "
                         "(two spellings, one meaning — and SPEC-002 §2.5 forbids synonyms). "
                         "Use `var` if you mean to reassign it",
                         st->kids[0]->tok.line);
        }
        for (proven_size_t j = 0; j < st->nkids; j++)
            if (st->kids[j]->kind == LOW_CST_BLOCK) ck_immutable_block(out, st->kids[j]);
    }
}
// ★★ **모듈 `let` 도 불변이다** — 당연하다. 그런데 `set LIMIT 5 .` 이
//   **"없는 이름"**(E-IR-UNDEF) 이라고 나왔다. **있는 이름인데.** 오진이다(교훈 5).
//   사용자는 오타를 찾으러 간다. 없는 오타를.
static void ck_module_const_set(low_check_result_t *out, const low_parse_result_t *pr,
                                const low_cst_t *blk) {
    if (!blk || blk->kind != LOW_CST_BLOCK) return;
    for (proven_size_t i = 0; i < blk->nkids; i++) {
        const low_cst_t *st = blk->kids[i];
        for (proven_size_t j = 0; j < st->nkids; j++)
            if (st->kids[j]->kind == LOW_CST_BLOCK) ck_module_const_set(out, pr, st->kids[j]);
        if (st->kind != LOW_CST_FORM || st->nkids < 2 || st->kids[0]->kind != LOW_CST_ATOM) continue;
        if (st->kids[0]->tok.kw != LOW_KW_SET || st->kids[1]->kind != LOW_CST_ATOM) continue;
        for (proven_size_t q = 0; q < pr->nforms; q++) {
            const low_cst_t *g = pr->forms[q];
            if (g->kind != LOW_CST_FORM || g->nkids < 2 || g->kids[0]->kind != LOW_CST_ATOM) continue;
            if (g->kids[0]->tok.kw != LOW_KW_LET || g->kids[1]->kind != LOW_CST_ATOM) continue;
            if (!proven_u8str_view_eq(g->kids[1]->tok.lex, st->kids[1]->tok.lex)) continue;
            emit(out, "E-IMMUTABLE",
                 "this is a module-level `let` — a NAMED CONSTANT. It is not a variable and it "
                 "cannot be assigned. (Its value is folded into the code at every use, so there "
                 "is nothing there to assign to.) A mutable module-level `var` is a MUTABLE GLOBAL: "
                 "RFC-0029 allows one, but only with the harms removed (escape-safe, a visible "
                 "`state` effect, a concurrency gate) — and none of that is enforced yet",
                 st->kids[0]->tok.line);
        }
    }
}
static void ck_immutable(low_check_result_t *out, const low_parse_result_t *pr) {
    for (proven_size_t i = 0; i < pr->nforms; i++) {
        const low_cst_t *f = pr->forms[i]; ck_cur_form = f;
        if (f->kind != LOW_CST_FORM || !f->nkids) continue;
        for (proven_size_t j = 0; j < f->nkids; j++)
            if (f->kids[j]->kind == LOW_CST_BLOCK) {
                ck_immutable_block(out, f->kids[j]);
                ck_module_const_set(out, pr, f->kids[j]);
            }
    }
}

// ★★ **`guard` 의 else 는 반드시 *나가야* 한다** (SPEC-002 D6).
//
//   ★ 용어: 명세는 이것을 **"발산(diverge)"** 이라 불렀다. 그건 타입 이론의 낱말이다(⊥ — 이
//     식은 정상적으로 돌아오지 않는다). **이 언어의 원칙에 안 맞는다**: 어휘는 작고 **쉬워야**
//     한다(P6). "발산" 은 **무엇이 잘못됐는지 말하지 않는다.**
//     결함의 이름은 **"떨어졌다(fall through)"** 다: else 가 나가지 않고 **그냥 아래로 떨어졌고**,
//     그러면 guard 뒤의 코드가 조건이 참이라고 **잘못 믿는다.** 그래서 `E-GUARD-FALLTHROUGH` 다.
//   명세가 그렇게 적어 두고 **강제하지 않았다.** 그래서 `guard` 는 사실상 `if not` 의
//   **동의어**였다 — §2.5 위반이고, 교훈 1(믿는 코드 + 강제하지 않는 코드)이다.
//   강제하면 `guard` 가 **진짜 다른 뜻**을 갖는다: **실패 경로를 먼저 갈라낸다.**
//   그래야 본문이 "여기서부터는 조건이 참이다" 를 **문법적으로** 보장받는다.
// ★ `else` 의 짐은 **블록일 수도, 그냥 나가는 form 일 수도** 있다:
//     guard C . else do return X . end     ← 블록
//     guard C . else return X .            ← 바로 발산 (D6 이 "발산-form" 이라 부른 그것)
//   둘 다 받아야 한다. 한쪽만 보면 멀쩡한 픽스처 일곱 개를 오진한다(실제로 그랬다).
static bool ck_leaves(const low_cst_t *nd) {   // 이 가지는 **나가는가** (return/break/continue/panic)
    if (!nd) return false;
    if (nd->kind == LOW_CST_ATOM) {
        low_kw_t kw = nd->tok.kw;
        return kw == LOW_KW_RETURN || kw == LOW_KW_BREAK || kw == LOW_KW_CONTINUE ||
               veq(nd->tok.lex, "panic");
    }
    for (proven_size_t i = 0; i < nd->nkids; i++)
        if (ck_leaves(nd->kids[i])) return true;
    return false;
}
// ★★ **정규화 층이 이미 붙여 놨다** (DECISION-0015): `else` 는 이제 guard 의 **마지막 자식**이다.
//   전엔 이 함수가 형제를 앞으로 훑으며 재조립했다 — 그리고 **여섯 소비자가 각자** 그랬다.
//   그 훑기가 세 번 틀렸다(블록으로 싸인 else · 몇 칸 뒤로 밀린 else · 맨 발산 form).
//   이제 **읽기만** 한다.
static void ck_guard_block(low_check_result_t *out, const low_cst_t *blk) {
    if (!blk || blk->kind != LOW_CST_BLOCK) return;
    for (proven_size_t i = 0; i < blk->nkids; i++) {
        const low_cst_t *st = blk->kids[i];
        for (proven_size_t j = 0; j < st->nkids; j++)
            if (st->kids[j]->kind == LOW_CST_BLOCK) ck_guard_block(out, st->kids[j]);
        if (st->kind != LOW_CST_FORM || !st->nkids || st->kids[0]->kind != LOW_CST_ATOM) continue;
        if (st->kids[0]->tok.kw != LOW_KW_GUARD) continue;
        const low_cst_t *eb = (st->nkids >= 2) ? st->kids[st->nkids - 1] : NULL;
        if (eb && !(eb->kind == LOW_CST_FORM && eb->nkids && eb->kids[0]->kind == LOW_CST_ATOM &&
                    eb->kids[0]->tok.kw == LOW_KW_ELSE)) eb = NULL;
        if (!ck_leaves(eb) && st->kids[0]->synth)          // RFC-0135 S1 — 바인딩 `else` 에서 온 guard
            emit(out, "E-GUARD-FALLTHROUGH",
                 "this binding's `else` FALLS THROUGH — it must LEAVE (`return` / `break` / `continue` / `panic`). "
                 "After the binding the name holds the value taken out of the option/result; if `else` came back "
                 "down there would be nothing to hold (RFC-0135 §4.2)", eb ? eb->kids[0]->tok.line : st->kids[0]->tok.line);
        else if (!ck_leaves(eb))
            emit(out, "E-GUARD-FALLTHROUGH",
                 "this `guard`'s `else` FALLS THROUGH — it must LEAVE (`return` / `break` / "
                 "`continue` / `panic`). If it falls through, the code AFTER the guard wrongly "
                 "believes the condition holds. Leaving is the WHOLE point of `guard`: it is what "
                 "lets the rest of the op assume the condition. (SPEC-002 D6 always said so and "
                 "nothing enforced it — which made `guard` an exact synonym of `if not`.) "
                 "If you do not mean to leave, write `if`",
                 st->kids[0]->tok.line);
    }
}

static void ck_guard_diverges(low_check_result_t *out, const low_parse_result_t *pr) {
    for (proven_size_t i = 0; i < pr->nforms; i++) {
        const low_cst_t *f = pr->forms[i]; ck_cur_form = f;
        if (f->kind != LOW_CST_FORM) continue;
        for (proven_size_t j = 0; j < f->nkids; j++)
            if (f->kids[j]->kind == LOW_CST_BLOCK) ck_guard_block(out, f->kids[j]);
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// ★★★ **안전지대 앵커** (RFC-0065) — 최상위 선언은 **열 0** 에서 시작한다.
//
//   최상위는 **선언 form 만** 담는다(RFC-0046). 그 머리가 열 0 이면, 프로그램의 **안전지대**
//   (모든 것이 초기화되는 바깥 섬)의 경계가 **정규식 한 줄**로 잡힌다 — 위로 스캔은 시작,
//   다음 열-0 머리 직전은 끝. 에디터·AI 가 파일을 **파싱하지 않고** 의미 단위로 자른다.
//
//   ★ **오류가 아니라 경고다.** 열-0 은 **표현이지 의미가 아니다**(같은 나무·같은 def 해시).
//     표현 위반을 컴파일 오류로 만들면 의미와 표현을 섞는 것이다 — 서식기가 보장한다(gofmt).
static void ck_col0(low_check_result_t *out, const low_parse_result_t *pr) {
    for (proven_size_t i = 0; i < pr->nforms; i++) {
        const low_cst_t *f = pr->forms[i]; ck_cur_form = f;
        if (f->kind != LOW_CST_FORM || !f->nkids || f->kids[0]->kind != LOW_CST_ATOM) continue;
        low_kw_t kw = f->kids[0]->tok.kw;
        // 최상위 **선언** 머리만 본다(수식자는 이미 벗겨져 안쪽 머리에 붙는다).
        bool is_decl = kw == LOW_KW_MODULE || kw == LOW_KW_STRUCT || kw == LOW_KW_ENUM ||
                       kw == LOW_KW_FN || kw == LOW_KW_PROC || kw == LOW_KW_ACTOR ||
                       kw == LOW_KW_TRAIT;
        if (!is_decl) continue;
        // col 은 1-based. 열 0(첫 칸)은 col == 1. **form 노드의 col** 을 본다 —
        //   수식자 벗기기가 바깥 수식자의 위치를 여기 물려주기 때문이다.
        proven_u32 col = f->col ? f->col : f->kids[0]->tok.col;
        if (col != 1)
            warn(out, "W-COL0",
                 "a top-level declaration does not start at column 0. The top level is the safe "
                 "zone — the outer island where everything is initialised — and a column-0 head "
                 "lets an editor or an AI find a declaration's bounds with ONE regex, scanning up "
                 "and down, WITHOUT parsing the file (RFC-0065). This is a WARNING, not an error: "
                 "column 0 is presentation, not meaning (same tree, same def hash). Run `--fmt`",
                 f->kids[0]->tok.line);
    }
}

// ★★★ RFC-0057 후속 — **호출 인자를 통한 세탁(laundering)을 막는다** (2026-07-22).
//
//   `fn`/공유 slice 의 "읽기 전용" 보증은 **직접** 쓰기(`set (index s …)`)에 대해서만
//   강제됐다(E-TYPE-MUT · E-EFFECT-PURITY). 그런데 그 s 를 **다른 op 의 `mut` 파라미터로 넘기면**
//   그 op 이 대신 써 준다 — 호출 지점에서 아무도 "이 인자가 정말 쓸 수 있는 장소인가" 를 안 봤다:
//
//       proc writer input s mut slice u8 . . output u8 . effects none . do set (index s 0) 99 . … end
//       fn sneaky input s slice u8 .        output u8 . effects none . do return writer s .    end
//                          ↑ 읽기 전용                                              ↑ mut 로 세탁된다
//
//   ⇒ **규칙(호출 지점):** 피호출자의 `mut`/`owned`/`mut_ref` 파라미터에 묶이는 인자는
//     **가변 장소(mutable place)** 여야 한다. `sneaky` 의 `s` 처럼 **공유(비-mut·비-owned)
//     파라미터**를 그 자리에 넘기면 거절한다.
//
//   ★ **왜 타입검사가 아니라 여기인가:** low_typecheck 의 `ty_t.is_mut` 는 **타입**의 mut 한정만
//     담는다 — `var b str_buf`(가변 장소지만 타입은 그냥 str_buf)와 공유 파라미터를 구별 못 한다.
//     바인딩의 **종류**(var/let/param mut/공유)는 여기 소유·빌림 패스만 정확히 안다.
//
//   ★★ **가변 장소(mutable place)의 정확한 술어** — 이제 두 좁힘을 모두 닫는다:
//     ① 인자가 **맨 이름**뿐 아니라 `ref X`/`mut_ref X`/`subslice X …`/`index X …` 로 감싼 식도
//        본다. `mut_ref X` = 가변 · `ref X` = 읽기 전용(거절) · `subslice`/`index` 는 X(kids[1])의
//        가변성으로 재귀한다. 그 밖의 form·리터럴은 보수적으로 통과시킨다(오탐 회피).
//     ② **지역 바인딩의 종류로 판정한다:** `var` = 항상 가변 장소 · `let` = **선언 타입 낱말**에
//        `mut`/`mut_ref` 가 있을 때만 가변(`let x mut slice u8`·`let r mut_ref pt` = 가변,
//        `let r ref pt`·`let x slice u8` = 읽기 전용). 파라미터는 `mut`/`owned`/`mut_ref` 만 가변.
//   ★★★ **왜 별도 코드(E-TYPE-ARGMUT)이고 nest 게이트에서 걸러지는가:** 호출 탐지는 괄호에
//        의존하므로 이 검사는 **정규화된 나무**에서만 운다 — `--flat`(정규화 생략)은 호출을 못 본다.
//        이는 nest 게이트가 단형화에 대해 이미 용인하는 **같은 한계**다(golden.sh 가 vm_generic
//        등을 뺀다: "--flat 은 단형화를 못 한다"). 그래서 **똑같이** 해소한다: 진단을 고유 코드
//        `E-TYPE-ARGMUT` 로 분리하고, nest 게이트의 flat↔tree 진단 대조에서 그 코드만 양쪽에서
//        걸러낸다 — 대신 아래 전용 게이트가 이 검사가 여전히 우는지 단언한다(조용히 죽을 수 없다).
static bool ck_param_mutq(const low_cst_t *f, const low_op_header_t *h, proven_size_t q) {
    if (q >= h->np) return false;
    if (h->p[q].is_mut || h->p[q].is_owned) return true;
    for (proven_size_t z = h->p[q].ts; z < h->p[q].te; z++)
        if (f->kids[z]->kind == LOW_CST_ATOM && veq(f->kids[z]->tok.lex, "mut_ref")) return true;
    return false;
}

typedef struct { proven_u8str_view_t name; bool is_mut_place; } ck_bind_t;

// 본체의 지역 `var`/`let` 바인딩을 모은다. **판정(위 좁힘 ②):** `var` 는 타입과 무관하게 항상
//   가변 장소다. `let` 은 **선언 타입 낱말**(이름과 `be` 사이)에 `mut`/`mut_ref` 가 있을 때만
//   가변이다 — `let x slice u8`(mut 없음) 은 읽기 전용, `let x mut slice u8`/`let r mut_ref pt` 는 가변.
static void ck_collect_binds(const low_cst_t *nd, ck_bind_t *b, proven_size_t *nb, proven_size_t cap) {
    if (!nd) return;
    if (nd->kind == LOW_CST_FORM && nd->nkids >= 2 && nd->kids[0]->kind == LOW_CST_ATOM &&
        (nd->kids[0]->tok.kw == LOW_KW_VAR || nd->kids[0]->tok.kw == LOW_KW_LET) &&
        nd->kids[1]->kind == LOW_CST_ATOM && *nb < cap) {
        bool mut_place;
        if (nd->kids[0]->tok.kw == LOW_KW_VAR) {
            mut_place = true;                                     // `var` — 언제나 가변 장소
        } else {
            mut_place = false;                                    // `let` — 타입에 mut/mut_ref 가 있어야
            for (proven_size_t z = 2; z < nd->nkids; z++) {
                const low_cst_t *k = nd->kids[z];
                if (k->kind != LOW_CST_ATOM) continue;
                if (k->tok.kw == LOW_KW_BE) break;                // 타입 낱말은 이름과 `be` 사이만
                if (veq(k->tok.lex, "mut") || veq(k->tok.lex, "mut_ref")) { mut_place = true; break; }
            }
        }
        b[*nb].name = nd->kids[1]->tok.lex; b[(*nb)++].is_mut_place = mut_place;
    }
    for (proven_size_t i = 0; i < nd->nkids; i++) ck_collect_binds(nd->kids[i], b, nb, cap);
}

// 이름이 **확정적으로 읽기 전용 장소**인가 (가변 장소로 보이면 false — 이름공간이 평면이라
//   가림은 없지만, 혹시 겹치면 가변 쪽을 살려 오탐을 피한다).
static bool ck_name_is_ro(const ck_bind_t *b, proven_size_t nb, proven_u8str_view_t nm) {
    bool found = false, any_mut = false;
    for (proven_size_t z = 0; z < nb; z++)
        if (proven_u8str_view_eq(b[z].name, nm)) { found = true; if (b[z].is_mut_place) any_mut = true; }
    return found && !any_mut;
}

// ★★★ X-0082 (2026-09-30, RFC-0132 T2b-3b 를 짓다 드러남) — **레코드의 칸에 쓰려면 그 레코드가 쓸 수 있는 자리여야 한다.**
//   `set x …` 는 `let` 이면 거절했는데(`E-IMMUTABLE`), `set (field r x) …` 는 **이름만 보는 검사를 비껴갔다**: `let r` 의 칸도,
//   `mut` 아닌 매개변수의 칸도 써졌다. 레코드는 참조로 넘어가므로 순수 `fn` 이 **부른 쪽의 `let` 레코드를 바꿨다**(VM·네이티브
//   모두 같은 틀린 답이라 차등 검사도 못 봤다). 정본의 예(`input o mut outer`)가 이미 전제한 규칙이다.
//   · `set (field B …) v` — 칸 쓰기. · `set (index (field B … f) i) v` · `set (field B … f k) v` — f 가 **배열 칸**이면 레코드
//     바이트 쓰기(슬라이스 칸은 남의 바이트를 가리키므로 원소 쓰기는 그 슬라이스의 `mut` 이 정한다 — 이 검사 밖).
//   · 배열 칸을 `mut slice` 로 묶는 것도 쓰기다(`var v be mut slice u8 (field r w)`).
//   B 가 이 op 의 `let`(타입에 `mut` 없음)이면 `E-IMMUTABLE`. `mut` 아닌 **매개변수**는 지역 복사라 거절하지 않고 하강이 진입에서
//   베낀다(정본 §6.5.1 (2a) — 그 복사가 없어서 부른 쪽이 바뀌었다). 모르는 이름은 보지 않는다.
typedef struct { ck_bind_t b[256]; proven_size_t nb; const low_cst_t *f; low_op_header_t h; } rw_t;
static int rw_base_kind(const rw_t *x, proven_u8str_view_t nm) {          // 0 = 모름/가변 · 1 = let · 2 = mut 아닌 매개변수
    bool found = false, any_mut = false;
    for (proven_size_t z = 0; z < x->nb; z++)
        if (proven_u8str_view_eq(x->b[z].name, nm)) { found = true; if (x->b[z].is_mut_place) any_mut = true; }
    if (found) return any_mut ? 0 : 1;
    // 매개변수는 거절하지 않는다 — 값으로 받은 구조체는 이 op 의 지역 복사이고(정본 §6.5.1 (2a)), 몸이 안쪽에 쓰면
    //   하강이 진입에서 베낀다(`ir_param_written`). `mut` 매개변수는 부른 쪽의 것에 쓴다.
    return 0;
}
static proven_u8str_view_t rw_base_type(const rw_t *x, proven_u8str_view_t nm) {   // 레코드의 타입 이름(첫 알맹이 낱말)
    for (proven_size_t q = 0; q < x->h.np; q++)
        if (proven_u8str_view_eq(x->h.p[q].name, nm) && x->h.p[q].core < x->f->nkids && ck_atom(x->f->kids[x->h.p[q].core]))
            return x->f->kids[x->h.p[q].core]->tok.lex;
    return (proven_u8str_view_t){ 0 };
}
static const low_cst_t *rw_unwrap(const low_cst_t *n) {
    while (n && n->kind == LOW_CST_GROUP && n->nkids == 1) n = n->kids[0];
    return n;
}
static proven_u8str_view_t rw_local_type(const low_cst_t *nd, proven_u8str_view_t nm) {   // 지역 `let|var nm <타입…> be` 의 알맹이
    if (!nd || nd->kind == LOW_CST_ATOM) return (proven_u8str_view_t){ 0 };
    if (nd->kind == LOW_CST_FORM && nd->nkids >= 3 && ck_atom(nd->kids[0]) &&
        (nd->kids[0]->tok.kw == LOW_KW_LET || nd->kids[0]->tok.kw == LOW_KW_VAR) && ck_atom(nd->kids[1]) &&
        proven_u8str_view_eq(nd->kids[1]->tok.lex, nm)) {
        for (proven_size_t z = 2; z < nd->nkids && ck_atom(nd->kids[z]) && nd->kids[z]->tok.kw != LOW_KW_BE; z++)
            if (!veq(nd->kids[z]->tok.lex, "mut") && !veq(nd->kids[z]->tok.lex, "owned") && !veq(nd->kids[z]->tok.lex, "ref") &&
                !veq(nd->kids[z]->tok.lex, "mut_ref")) return nd->kids[z]->tok.lex;
    }
    for (proven_size_t i = 0; i < nd->nkids; i++) { proven_u8str_view_t t = rw_local_type(nd->kids[i], nm); if (t.size) return t; }
    return (proven_u8str_view_t){ 0 };
}
static bool rw_is_array_field(const rw_t *x, proven_u8str_view_t base, proven_u8str_view_t fname) {
    proven_u8str_view_t ty = rw_base_type(x, base);
    if (!ty.size) ty = rw_local_type(x->f, base);
    return ty.size && ck_is_array_field(ty, fname);
}
static void rw_refuse(low_check_result_t *out, int kind, proven_u32 line) {
    if (kind == 1)
        emit(out, "E-IMMUTABLE", "this record was bound with `let`, which is IMMUTABLE — its fields (and the bytes of its array "
             "fields) cannot be written either. Use `var` if you mean to change it", line);
    else
        emit(out, "E-TYPE-MUT", "writing into a record parameter that is not declared `mut` — the caller's record would change "
             "behind its back. Declare the input `mut <type>` (and the op a `proc`), or build a new record and return it", line);
}
// 구조체 `ty` 의 배열 칸 `fname` 선언(`array T N`)의 T·N 낱말.
static bool ck_array_field_decl(proven_u8str_view_t ty, proven_u8str_view_t fname, const low_cst_t **t, const low_cst_t **n) {
    if (!g_ck_pr) return false;
    for (proven_size_t i = ty.size; i-- > 0; ) if (ty.ptr[i] == '.') { ty.ptr += i + 1; ty.size -= i + 1; break; }
    for (proven_size_t i = 0; i < g_ck_pr->nforms; i++) {
        const low_cst_t *f = g_ck_pr->forms[i];
        if (f->kind != LOW_CST_FORM || f->nkids < 3 || !ck_atom(f->kids[0]) || f->kids[0]->tok.kw != LOW_KW_STRUCT ||
            !ck_atom(f->kids[1]) || !proven_u8str_view_eq(f->kids[1]->tok.lex, ty)) continue;
        const low_cst_t *blk = f->kids[f->nkids - 1];
        for (proven_size_t j = 0; blk->kind == LOW_CST_BLOCK && j < blk->nkids; j++) {
            const low_cst_t *fl = blk->kids[j];
            if (fl->kind == LOW_CST_FORM && fl->nkids >= 4 && ck_atom(fl->kids[0]) && ck_atom(fl->kids[1]) &&
                proven_u8str_view_eq(fl->kids[0]->tok.lex, fname) && veq(fl->kids[1]->tok.lex, "array")) {
                *t = fl->kids[2]; *n = fl->kids[3]; return true;
            }
        }
        return false;
    }
    return false;
}
// ★ T2b-3b — 배열 칸에 주는 **나열 리터럴**은 칸의 원소 타입·길이와 같아야 한다(번역 시점에 보인다 — 실행까지 미루지 않는다).
static void rw_field_lits(low_check_result_t *out, const low_cst_t *sf) {
    const low_cst_t *blk = sf->kids[sf->nkids - 1];
    for (proven_size_t j = 0; j < blk->nkids; j++) {
        const low_cst_t *fl = blk->kids[j];
        if (fl->kind != LOW_CST_FORM || fl->nkids != 2 || !ck_atom(fl->kids[0])) continue;
        const low_cst_t *t = NULL, *n = NULL;
        if (!ck_array_field_decl(sf->kids[0]->tok.lex, fl->kids[0]->tok.lex, &t, &n)) continue;
        const low_cst_t *l = lc_list(fl->kids[1]);
        if (!l || !ck_atom(t) || !ck_atom(n)) continue;
        bool same_t = ck_atom(l->kids[2]) && proven_u8str_view_eq(l->kids[2]->tok.lex, t->tok.lex);
        unsigned long long want = strtoull((const char *)n->tok.lex.ptr, NULL, 0), got = 0;
        if (veq(l->kids[1]->tok.lex, "array")) got = l->nkids > 3 && ck_atom(l->kids[3]) ? strtoull((const char *)l->kids[3]->tok.lex.ptr, NULL, 0) : 0;
        else if (veq(l->kids[1]->tok.lex, "slice"))
            for (proven_size_t q = 3; q < l->nkids; q++) got++;
        if (!same_t || got != want)
            emit(out, "E-TYPE-FIELD", "this list does not fit the array field — the field is `array <type> <length>` and the list "
                 "must carry the same element type and exactly that length", fl->kids[0]->tok.line);
    }
}
static void rw_walk(low_check_result_t *out, const rw_t *x, const low_cst_t *nd) {
    if (!nd || nd->kind == LOW_CST_ATOM) return;
    if (nd->kind == LOW_CST_FORM && nd->nkids >= 2 && ck_atom(nd->kids[0]) && nd->kids[nd->nkids - 1]->kind == LOW_CST_BLOCK &&
        nd->kids[0]->tok.kw == LOW_KW_NONE && ck_struct_exists(nd->kids[0]->tok.lex)) rw_field_lits(out, nd);
    if (nd->kind == LOW_CST_FORM && nd->nkids >= 3 && ck_atom(nd->kids[0]) && nd->kids[0]->tok.kw == LOW_KW_SET) {
        const low_cst_t *pl = rw_unwrap(nd->kids[1]);
        proven_u32 ln = nd->kids[0]->tok.line;
        if (pl && pl->kind == LOW_CST_FORM && pl->nkids >= 3 && ck_atom(pl->kids[0])) {
            if (veq(pl->kids[0]->tok.lex, "field") && ck_atom(pl->kids[1])) {
                int k = rw_base_kind(x, pl->kids[1]->tok.lex);
                const low_cst_t *last = pl->kids[pl->nkids - 1];
                bool elem = ck_atom(last) && last->tok.kind == LOW_TOK_NUMBER;
                // 끝이 번호면 원소 쓰기 — 그 앞 칸이 배열 칸일 때만 레코드 쓰기다
                if (k && (!elem || (pl->nkids >= 4 && ck_atom(pl->kids[pl->nkids - 2]) &&
                                    rw_is_array_field(x, pl->kids[1]->tok.lex, pl->kids[pl->nkids - 2]->tok.lex))))
                    rw_refuse(out, k, ln);
            } else if (veq(pl->kids[0]->tok.lex, "index")) {
                const low_cst_t *sv = rw_unwrap(pl->kids[1]);
                if (sv && sv->kind == LOW_CST_FORM && sv->nkids == 3 && ck_atom(sv->kids[0]) && veq(sv->kids[0]->tok.lex, "field") &&
                    ck_atom(sv->kids[1]) && ck_atom(sv->kids[2])) {
                    int k = rw_base_kind(x, sv->kids[1]->tok.lex);
                    if (k && rw_is_array_field(x, sv->kids[1]->tok.lex, sv->kids[2]->tok.lex)) rw_refuse(out, k, ln);
                }
            }
        }
    }
    // `let|var v be mut slice T (field B f)` — 배열 칸을 쓸 수 있는 보기로 꺼낸다
    if (nd->kind == LOW_CST_FORM && nd->nkids >= 4 && ck_atom(nd->kids[0]) &&
        (nd->kids[0]->tok.kw == LOW_KW_LET || nd->kids[0]->tok.kw == LOW_KW_VAR)) {
        proven_size_t be = nd->nkids; bool mutw = false;
        for (proven_size_t z = 2; z < nd->nkids; z++) {
            if (ck_atom(nd->kids[z]) && nd->kids[z]->tok.kw == LOW_KW_BE) { be = z; break; }
            if (ck_atom(nd->kids[z]) && (veq(nd->kids[z]->tok.lex, "mut") || veq(nd->kids[z]->tok.lex, "mut_ref"))) mutw = true;
        }
        const low_cst_t *v = be + 2 == nd->nkids ? rw_unwrap(nd->kids[be + 1]) : NULL;
        if (mutw && v && v->kind == LOW_CST_FORM && v->nkids == 3 && ck_atom(v->kids[0]) && veq(v->kids[0]->tok.lex, "field") &&
            ck_atom(v->kids[1]) && ck_atom(v->kids[2])) {
            int k = rw_base_kind(x, v->kids[1]->tok.lex);
            if (k && rw_is_array_field(x, v->kids[1]->tok.lex, v->kids[2]->tok.lex)) rw_refuse(out, k, nd->kids[0]->tok.line);
        }
    }
    for (proven_size_t i = 0; i < nd->nkids; i++) rw_walk(out, x, nd->kids[i]);
}
static void ck_record_writes(low_check_result_t *out, const low_cst_t *f) {
    static rw_t x;
    memset(&x, 0, sizeof x);
    x.f = f; x.h = low_op_header(f);
    ck_collect_binds(f->kids[f->nkids - 1], x.b, &x.nb, 256);
    rw_walk(out, &x, f->kids[f->nkids - 1]);
}
// **필드 `fname` 이 (이 파일의 어느 struct 정의에서든) 확정적으로 비-mut 인가.**
//   ★★ **왜 필드는 subslice/index 처럼 구조체 장소로만 재귀하면 안 되는가:** 슬라이스 **필드**는
//     `mut slice u64` 로 선언될 수 있고(2026-07-20, vm_sfield.low), 그러면 **공유 구조체에서 꺼내도**
//     원소가 가변이다 — `step_bundled` 가 공유 파라미터 `p` 의 `field p xs`(mut slice)에 실제로 쓴다.
//     따라서 `field <공유> <mut-slice-필드>` 는 **합법**이고, 구조체 장소로만 재귀하면 그걸 오탐한다.
//     ⇒ 필드가 **확정적으로 비-mut** 일 때만(모든 정의에서 `mut`/`mut_ref` 없음, 그리고 적어도
//        하나에서 발견) 읽기 전용으로 본다. mut 로 선언된 자리가 **하나라도** 있으면 보수적으로 통과.
//     이름만으로 본다(장소의 정확한 struct 타입 추적 없이) — 그래서 서로 다른 struct 가 같은 필드명을
//     mut/비-mut 로 나눠 쓰면 보수적으로 통과한다(**건전**: 확정 비-mut 일 때만 거절). 파일 밖 struct
//     (import)면 발견 실패 → 통과. 어느 쪽도 합법 프로그램을 거절하지 않는다.
static bool ck_field_provably_nonmut(const low_parse_result_t *pr, proven_u8str_view_t fname) {
    bool found = false;
    for (proven_size_t i = 0; i < pr->nforms; i++) {
        const low_cst_t *f = pr->forms[i]; ck_cur_form = f;
        if (f->kind != LOW_CST_FORM || !f->nkids || f->kids[0]->kind != LOW_CST_ATOM) continue;
        if (f->kids[0]->tok.kw != LOW_KW_STRUCT) continue;
        const low_cst_t *blk = NULL;
        for (proven_size_t z = 0; z < f->nkids; z++)
            if (f->kids[z]->kind == LOW_CST_BLOCK) blk = f->kids[z];
        if (!blk) continue;
        for (proven_size_t q = 0; q < blk->nkids; q++) {
            const low_cst_t *fld = blk->kids[q];
            if (fld->kind != LOW_CST_FORM || fld->nkids < 2 || fld->kids[0]->kind != LOW_CST_ATOM) continue;
            if (!proven_u8str_view_eq(fld->kids[0]->tok.lex, fname)) continue;
            found = true;
            for (proven_size_t z = 1; z < fld->nkids; z++)
                if (fld->kids[z]->kind == LOW_CST_ATOM &&
                    (veq(fld->kids[z]->tok.lex, "mut") || veq(fld->kids[z]->tok.lex, "mut_ref")))
                    return false;                                // 이 필드가 가변 → 확정 비-mut 아님
        }
    }
    return found;                                                // 발견됨 + 어디서도 mut 아님 → 확정 비-mut
}

// 인자가 **확정적으로 읽기 전용 장소**인가 — mut/owned/mut_ref 자리로 넘기면 거절해야 하는가.
//   맨 이름 → 바인딩 종류로 판정. `mut_ref X` → 가변(false) · `ref X` → 읽기 전용(true, 거절).
//   `subslice X …`/`index X …` → X(kids[1])로 재귀. `field X F` → 구조체 장소 X 가 읽기 전용이고
//   필드 F 가 **확정 비-mut** 일 때만 읽기 전용(위 ck_field_provably_nonmut 의 이유). 그 밖은 통과.
static bool ck_arg_is_ro(const low_cst_t *arg, const ck_bind_t *binds, proven_size_t nb,
                         const low_parse_result_t *pr) {
    if (!arg) return false;
    // 저자·정규화가 만든 괄호(GROUP 하나짜리)를 벗긴다 — `(ref q)` 는 GROUP(FORM(ref, q)) 다.
    while (arg->kind == LOW_CST_GROUP && arg->nkids == 1) arg = arg->kids[0];
    if (arg->kind == LOW_CST_ATOM) {
        if (arg->tok.kind == LOW_TOK_IDENT && arg->tok.kw == LOW_KW_NONE)
            return ck_name_is_ro(binds, nb, arg->tok.lex);       // 맨 이름
        // ★★★★ **리터럴은 고칠 수 있는 자리가 아니다** (정본 §6.1.4(12) · §8.8 · 결함 노트 #84, 2026-09-16).
        //   문자열 리터럴을 `mut slice u8` 자리에 넘기는 것이 통과했다 — VM 은 그 바이트를 고치고
        //   네이티브는 안 고쳐(두 뒤끝이 갈렸다) 표준 라이브러리가 거기 쓰면 네이티브가 죽었다.
        if (arg->tok.kind == LOW_TOK_STRING || arg->tok.kind == LOW_TOK_HEREDOC) return true;
        return false;                                            // 그 밖의 리터럴 — 보수적으로 통과
    }
    // ★ RFC-0132 T2b-2 — 나열 리터럴을 `mut` 자리에 넘기면 **틀 안 임시(ⓒ)** 로 지어진다(§13.2) — 쓸 수 있는 자리다.
    //   (T2b-1 에서는 상수 나열을 읽기 전용이라 거절했다. 이제 받는 쪽이 쓰는 것은 그 문장의 임시다.)
    if (arg->kind == LOW_CST_FORM && arg->nkids >= 1 &&
        arg->kids[0]->kind == LOW_CST_ATOM && arg->kids[0]->tok.kw == LOW_KW_NONE) {
        proven_u8str_view_t head = arg->kids[0]->tok.lex;
        if (veq(head, "mut_ref")) return false;                  // 가변 장소
        if (veq(head, "ref"))     return true;                   // 공유 참조 — 읽기 전용, 거절
        if (veq(head, "subslice") || veq(head, "index"))         // 슬라이스 X 의 가변성으로 재귀
            return arg->nkids >= 2 ? ck_arg_is_ro(arg->kids[1], binds, nb, pr) : false;
        if (veq(head, "field")) {                                // `field X F` — 별칭 접근
            if (arg->nkids < 3 || arg->kids[2]->kind != LOW_CST_ATOM) return false;
            if (!ck_arg_is_ro(arg->kids[1], binds, nb, pr)) return false;  // 구조체 장소가 가변 → 통과
            return ck_field_provably_nonmut(pr, arg->kids[2]->tok.lex);    // 필드가 확정 비-mut 여야 거절
        }
    }
    return false;                                                // 그 밖의 form — 보수적으로 통과
}

// ★★★★★ **권위는 종류로 온다 — 그리고 그 말은 `--check` 가 해야 한다** (RFC-0077 §P1-2).
//
//   전엔 호스트 잎의 종류 검사가 **하강에만** 있었다. 그래서 잘못된 종류를 든 프로그램이
//   `--check` 를 **통과했다**: 오류는 `E-IR-UNSUP`("지원 안 함")으로 나고, 요약은
//   *"1 op 은 정적 검사를 다 통과했지만 하강할 수 없다"* 는 **주석**을 달고 `ok` 를 찍었다.
//   ⇒ **권위 문제가 미구현 문제로 보고되고 있었다.** 그 둘은 다른 것이다: 하나는 *"이 기능이
//     아직 없다"*, 다른 하나는 *"당신은 그럴 권리가 없다"*. FFI 는 이미 검사층에서 문다
//     (`E-FFI-CAPKIND`) — 호스트 잎만 예외였다.
//   ★ 표는 하강에서 빌려 온다(`low_ir_leaf_cap_kind`) — 두 벌이면 갈린다.
static void ck_capkind_walk(low_check_result_t *out, const low_cst_t *nd, const low_cst_t *def_form) {
    if (!nd) return;
    // ★ CST 는 평평하다 — `return time_now k .` 은 **한 폼**(kids = [return, time_now, k])이다.
    //   그래서 머리만 보면 잎을 못 본다: 모든 자리를 훑는다(첫판에 이것 때문에 안 물었다).
    for (proven_size_t z = 0; z + 1 < nd->nkids; z++) {
        if (nd->kids[z]->kind != LOW_CST_ATOM) continue;
        const char *kind = low_ir_leaf_cap_kind(nd->kids[z]->tok.lex);
        if (kind && nd->kids[z + 1]->kind == LOW_CST_ATOM) {
            proven_u8str_view_t want = { 0 };
            low_op_header_t h = low_op_header(def_form);
            for (proven_size_t q = 0; q < h.np; q++) {
                proven_size_t cw = h.p[q].core;
                if (cw + 1 < h.p[q].te && cw + 1 < def_form->nkids &&
                    ck_atom(def_form->kids[cw]) && ck_atom(def_form->kids[cw + 1]) &&
                    veq(def_form->kids[cw]->tok.lex, "cap") &&
                    veq(def_form->kids[cw + 1]->tok.lex, kind)) { want = h.p[q].name; break; }
            }
            proven_u8str_view_t given = nd->kids[z + 1]->tok.lex;
            // ★★★★ **이름이 곧 답이다** (RFC-0112 D5(4) · F8). 전엔 그 종류의 **첫** 권한만 `want` 로 삼아
            //   같은 종류의 둘째 권한을 대면 «종류가 틀렸다» 고 오진했다. 이제 댄 이름의 종류를 본다.
            //   그리고 `alloc_bytes` 는 두 종류를 받는다 — `cap allocator`(고정 창) · `cap heap`(힙).
            for (proven_size_t q = 0; q < h.np; q++) {
                proven_size_t cw = h.p[q].core;
                if (!proven_u8str_view_eq(h.p[q].name, given)) continue;
                if (cw + 1 < h.p[q].te && cw + 1 < def_form->nkids &&
                    ck_atom(def_form->kids[cw]) && ck_atom(def_form->kids[cw + 1]) &&
                    veq(def_form->kids[cw]->tok.lex, "cap") &&
                    (veq(def_form->kids[cw + 1]->tok.lex, kind) ||
                     (strcmp(kind, "allocator") == 0 && veq(def_form->kids[cw + 1]->tok.lex, "heap"))))
                    want = given;
                break;
            }
            bool holds_any_cap = false;
            for (proven_size_t q = 0; q < h.np; q++)
                if (proven_u8str_view_eq(h.p[q].name, given)) {
                    proven_size_t cw = h.p[q].core;
                    if (cw < def_form->nkids && ck_atom(def_form->kids[cw]) &&
                        veq(def_form->kids[cw]->tok.lex, "cap")) holds_any_cap = true;
                    break;
                }
            if (holds_any_cap && !(want.size && proven_u8str_view_eq(want, given)))
                emit(out, "E-CAP-KIND",
                     "this host leaf was handed a capability of the WRONG KIND. Authority is by "
                     "KIND, not by mere presence: an unrelated right (io, net, …) does not "
                     "authorize the clock, the filesystem, the socket or the OS entropy "
                     "(RFC-0083 L3 · RFC-0077 §P1-2). Take the right this leaf names — the FFI "
                     "boundary has always been checked this way (E-FFI-CAPKIND); the host leaves "
                     "used to be refused only at lowering, where a WRONG RIGHT was reported as an "
                     "UNSUPPORTED FEATURE",
                     nd->kids[z]->tok.line);
        }
    }
    for (proven_size_t i = 0; i < nd->nkids; i++) ck_capkind_walk(out, nd->kids[i], def_form);
}

// ★★★ **한 저장소를 쓰기 자리와 다른 자리에 함께 넘길 수 없다** (정본 §8.4 · §8.12 · 결함 노트 #9).
//   `copy_into buf buf` 는 같은 바이트를 **쓰는 쪽과 읽는 쪽**으로 동시에 건넨다. `mut_ref` 를
//   두 번 넘기면 `E-EXCL` 인데 이 모양은 통과했다 — 배타 규칙은 «쓰기 하나 **또는** 읽기 여럿»
//   이지 «쓰기 하나와 읽기 하나» 가 아니다.
// ★★★★ **«같은 이름» 이 아니라 «같은 저장소»** (결함 `excl-same-storage-by-alias`, 2026-09-21).
//   정본 §8.12(6) 은 같은 **저장소**를 `mut` 자리 둘에 넘기는 것을 거절한다. 전엔 같은 **이름**만
//   봐서 `var x be buf` 뒤 `two buf x`, `two (subslice buf 0 4) (subslice buf 0 4)` 가 통과했다 —
//   뒤 쓰기가 앞 쓰기를 조용히 덮었다. 이제 인자마다 **저장소 열쇠**(밑동 이름 + 구간)를 낸다:
//     · 이름 — 이 op 안의 별칭(`let/var x be … <이름>` · `be subslice <이름> a b`)을 따라 밑동으로
//     · `subslice E a b` — a·b 가 **수 리터럴**이면 구간을 안다(별칭의 구간과 합친다)
//   겹침 판정: 밑동이 같고, 한쪽이 전체이거나 두 상수 구간이 겹치면 `E-EXCL`.
//   ☞ 구간이 **상수가 아니면** 가르지 않는다(거절하지 않는다) — 그것은 RFC-0116 D2(동적 범위)의 몫이다.
//     작업 버퍼를 상수 구간으로 서로소하게 잘라 쓰는 모양(코퍼스 ~300 줄)은 그대로 통과한다.
//   ☞ 통째로 다시 대입되는 이름(`set x …`)은 별칭으로 믿지 않는다 — 흐름을 안 따라가는 대신 좁게 본다.
#define CK_SK_MAX 96
// ★★★ **칸 경로도 저장소다** (결함 `field-path-exclusivity`, 2026-09-22 · RFC-0116 D3). `(field s a)` 를 `mut` 입력에
//   넘기면 칸 **자리**가 넘어간다 — 그래서 같은 칸 두 번 · 전체와 그 칸은 쓰기 둘이다. 열쇠에 경로를 더한다:
//   경로가 갈라지면(`a` 와 `b`) 서로소, 한쪽이 다른 쪽의 앞부분이면(같은 칸 · 부모와 자식) 겹친다.
#define CK_SK_PATH 4
typedef struct { proven_u8str_view_t root; bool whole, known; unsigned long long a, b;
                 proven_u8str_view_t path[CK_SK_PATH]; unsigned np; } ck_skey_t;
typedef struct { proven_u8str_view_t name; ck_skey_t key; } ck_salias_t;
typedef struct { ck_salias_t v[CK_SK_MAX]; proven_size_t n;
                 proven_u8str_view_t reset[CK_SK_MAX]; proven_size_t nreset; bool full; } ck_sctx_t;

static bool ck_num_lit(const low_cst_t *nd, unsigned long long *out) {
    while (nd && nd->kind == LOW_CST_GROUP && nd->nkids == 1) nd = nd->kids[0];
    if (!nd || nd->kind != LOW_CST_ATOM || nd->tok.kind != LOW_TOK_NUMBER) return false;
    unsigned long long v = 0;
    if (!nd->tok.lex.size || nd->tok.lex.size > 19) return false;
    for (proven_size_t i = 0; i < nd->tok.lex.size; i++) {
        unsigned c = nd->tok.lex.ptr[i];
        if (c < '0' || c > '9') return false;            // 10 진 리터럴만 — 나머지는 «모른다»
        v = v * 10 + (c - '0');
    }
    *out = v; return true;
}
static bool ck_sk_reset(const ck_sctx_t *c, proven_u8str_view_t n) {
    for (proven_size_t i = 0; i < c->nreset; i++) if (proven_u8str_view_eq(c->reset[i], n)) return true;
    return false;
}
// kids[from..to) 를 한 식으로 보고 저장소 열쇠를 낸다(root.size == 0 → 저장소를 모른다)
static ck_skey_t ck_skey_seq(const ck_sctx_t *c, const low_cst_t *const *k, proven_size_t from, proven_size_t to, int depth) {
    ck_skey_t z = { 0 };
    if (depth > 8 || from >= to) return z;
    if (to - from == 1) {
        const low_cst_t *nd = k[from];
        while (nd && nd->kind == LOW_CST_GROUP && nd->nkids == 1) nd = nd->kids[0];
        if (!nd) return z;
        if (nd->kind == LOW_CST_ATOM) {
            if (nd->tok.kind != LOW_TOK_IDENT) return z;
            for (proven_size_t i = 0; i < c->n; i++)
                if (proven_u8str_view_eq(c->v[i].name, nd->tok.lex)) return c->v[i].key;
            z.root = nd->tok.lex; z.whole = true; return z;
        }
        if ((nd->kind == LOW_CST_FORM || nd->kind == LOW_CST_GROUP) && nd->nkids)
            return ck_skey_seq(c, (const low_cst_t *const *)nd->kids, 0, nd->nkids, depth + 1);
        return z;
    }
    // ★★ `field <값> <마디> …` — 정본 §6.2 의 철자는 **여러 마디 하나**다(`field s a x`). 괄호로 겹친 꼴
    //   `(field (field s a) x)` 도 같은 경로로 읽는다. 2026-09-22 첫 판은 한 마디만 읽어서 정본 철자의 깊은 칸
    //   (`two (field s a x) (field s a x)`)이 통과했다 — **철자가 둘이면 둘 다 가르친다.**
    if (to - from >= 3 && ck_atom(k[from]) && veq(k[from]->tok.lex, "field")) {
        ck_skey_t base = ck_skey_seq(c, k, from + 1, from + 2, depth + 1);
        // ★ 조각을 잘라 낸 뒤의 칸(`field (subslice …) f`)은 뜻이 없다 · 경로가 표보다 깊으면 «모른다»(가르지 않는다)
        if (!base.root.size || !base.whole) return z;
        for (proven_size_t m = from + 2; m < to; m++) {
            if (!ck_atom(k[m]) || base.np >= CK_SK_PATH) return z;
            base.path[base.np++] = k[m]->tok.lex;       // 이름 마디 · 정수 마디(자리) 모두 경로다
        }
        return base;
    }
    if (to - from == 4 && ck_atom(k[from]) && veq(k[from]->tok.lex, "subslice")) {
        ck_skey_t base = ck_skey_seq(c, k, from + 1, from + 2, depth + 1);
        if (!base.root.size) return z;
        unsigned long long a, b;
        if (!(ck_num_lit(k[from + 2], &a) && ck_num_lit(k[from + 3], &b)) || b < a) {
            base.whole = false; base.known = false; return base;      // 구간을 모른다
        }
        if (base.whole) { base.whole = false; base.known = true; base.a = a; base.b = b; return base; }
        if (base.known) { base.a += a; base.b = base.a + (b - a); return base; }
        return base;                                                  // 밑이 이미 모르는 구간
    }
    return z;
}
static void ck_sk_collect(ck_sctx_t *c, const low_cst_t *nd, bool resets) {
    if (!nd || nd->kind == LOW_CST_ATOM) return;
    if (nd->kind == LOW_CST_FORM && nd->nkids >= 2 && ck_atom(nd->kids[0])) {
        low_kw_t kw = nd->kids[0]->tok.kw;
        if (resets && kw == LOW_KW_SET && ck_atom(nd->kids[1]) && !ck_sk_reset(c, nd->kids[1]->tok.lex)) {   // ★ 이름마다 한 번(600 문장 몸이 표를 넘쳤다 — vm_bigbody)
            if (c->nreset >= CK_SK_MAX) c->full = true;
            else { c->reset[c->nreset] = nd->kids[1]->tok.lex; c->nreset += 1; }
        }
        if (!resets && (kw == LOW_KW_LET || kw == LOW_KW_VAR) && ck_atom(nd->kids[1]) &&
            !ck_sk_reset(c, nd->kids[1]->tok.lex)) {
            proven_size_t be = nd->nkids;
            for (proven_size_t q = 2; q < nd->nkids; q++)
                if (ck_atom(nd->kids[q]) && nd->kids[q]->tok.kw == LOW_KW_BE) { be = q; break; }
            if (be + 1 < nd->nkids) {
                ck_skey_t k = ck_skey_seq(c, (const low_cst_t *const *)nd->kids, be + 1, nd->nkids, 0);
                // ★ 별칭은 **이름이나 subslice** 에서만 선다 — 부름의 결과는 새 저장소일 수 있다
                const low_cst_t *h = nd->kids[be + 1];
                while (h && h->kind == LOW_CST_GROUP && h->nkids == 1) h = h->kids[0];
                bool shape = (be + 2 == nd->nkids && h && h->kind == LOW_CST_ATOM) ||
                             (h && ck_atom(h) && (veq(h->tok.lex, "subslice") || veq(h->tok.lex, "field"))) ||
                             (h && h->kind == LOW_CST_FORM && h->nkids && ck_atom(h->kids[0]) &&
                              (veq(h->kids[0]->tok.lex, "subslice") || veq(h->kids[0]->tok.lex, "field")));
                if (k.root.size && shape && !proven_u8str_view_eq(k.root, nd->kids[1]->tok.lex)) {
                    if (c->n >= CK_SK_MAX) c->full = true;
                    else { c->v[c->n].name = nd->kids[1]->tok.lex; c->v[c->n].key = k; c->n += 1; }
                }
            }
        }
    }
    for (proven_size_t i = 0; i < nd->nkids; i++) ck_sk_collect(c, nd->kids[i], resets);
}
static bool ck_sk_overlap(ck_skey_t x, ck_skey_t y) {
    if (!x.root.size || !y.root.size || !proven_u8str_view_eq(x.root, y.root)) return false;
    unsigned m = x.np < y.np ? x.np : y.np;
    for (unsigned i = 0; i < m; i++)
        if (!proven_u8str_view_eq(x.path[i], y.path[i])) return false;     // 갈라진 칸 — 서로소
    if (x.np != y.np) return true;                                         // 부모와 자식 — 겹친다
    if (x.whole && y.whole) return true;
    if (x.whole) return y.known && y.b > y.a;
    if (y.whole) return x.known && x.b > x.a;
    if (!x.known || !y.known) return false;                           // 모르는 구간 — D2 의 몫
    unsigned long long lo = x.a > y.a ? x.a : y.a, hi = x.b < y.b ? x.b : y.b;
    return lo < hi;
}
// 한정 호출(`m.f`)은 그 모듈의 op 을, 맨이름은 이름으로 찾는다(op_declared_q 와 같은 규율)
static const low_opinfo_t *ck_find_callee(const low_opinfo_t *tab, proven_size_t nt, proven_u8str_view_t callee) {
    proven_size_t dot = callee.size;
    for (proven_size_t i = 0; i < callee.size; i++) if (callee.ptr[i] == '.') { dot = i; break; }
    if (dot < callee.size) {
        proven_u8str_view_t head = { callee.ptr, dot }, tail = { callee.ptr + dot + 1, callee.size - dot - 1 };
        for (proven_size_t t = 0; t < nt; t++)
            if (tab[t].form && proven_u8str_view_eq(tab[t].name, tail) && proven_u8str_view_eq(tab[t].mod, head))
                return &tab[t];
        return NULL;
    }
    for (proven_size_t t = 0; t < nt; t++)
        if (tab[t].form && proven_u8str_view_eq(tab[t].name, callee)) return &tab[t];
    return NULL;
}

// 이 op 머리에 `inplace <w> <r> .` 이 있나
static bool ck_has_inplace(const low_cst_t *form, proven_u8str_view_t w, proven_u8str_view_t r) {
    if (!form) return false;
    for (proven_size_t i = 0; i + 2 < form->nkids; i++) {
        if (form->kids[i]->kind == LOW_CST_BLOCK) break;
        if (!(ck_atom(form->kids[i]) && veq(form->kids[i]->tok.lex, "inplace"))) continue;
        if (ck_atom(form->kids[i + 1]) && ck_atom(form->kids[i + 2]) &&
            proven_u8str_view_eq(form->kids[i + 1]->tok.lex, w) && proven_u8str_view_eq(form->kids[i + 2]->tok.lex, r))
            return true;
    }
    return false;
}
static void ck_excl_args_walk2(low_check_result_t *out, const low_cst_t *nd,
                               const low_opinfo_t *tab, proven_size_t nt, const ck_sctx_t *c) {
    if (!nd) return;
    if (nd->kind == LOW_CST_FORM && nd->nkids >= 3 && ck_atom(nd->kids[0]) &&
        nd->kids[0]->tok.kw == LOW_KW_NONE) {
        // ★★ **쓰는 자리 둘에 같은 저장소** — §8.12 는 «쓰는 쪽 하나, 아니면 읽는 쪽 여럿» 이다.
        //   ☞ **쓰는 자리 하나 + 읽는 자리**(`mont_mul acc acc r2 …`)는 여기서 안 문다 — 정본 §8.12(7) ·
        //     RFC-0115 §8-15 ⓒ(작성자 의무) · RFC-0116 D2(현행 유지, 2026-09-21 소유자 결정).
        const low_opinfo_t *op = ck_find_callee(tab, nt, nd->kids[0]->tok.lex);
        if (op) {
            low_op_header_t h = low_op_header(op->form);
            bool hit = false;
            for (proven_size_t q = 1; q < nd->nkids && !hit; q++) {
                proven_size_t ai = q - 1;
                if (ai >= h.np || !h.p[ai].is_mut) continue;
                ck_skey_t kq = ck_skey_seq(c, (const low_cst_t *const *)nd->kids, q, q + 1, 0);
                if (!kq.root.size) continue;
                for (proven_size_t r = q + 1; r < nd->nkids; r++) {
                    proven_size_t bi = r - 1;
                    if (bi >= h.np || !h.p[bi].is_mut) continue;
                    ck_skey_t kr = ck_skey_seq(c, (const low_cst_t *const *)nd->kids, r, r + 1, 0);
                    if (!ck_sk_overlap(kq, kr)) continue;
                    emit(out, "E-EXCL",
                         "the same storage is handed to this op in TWO places it writes. The rule is "
                         "one writer or many readers (§8.12), never two writers at once: which write "
                         "survives depends on the order of statements inside the op you called, not "
                         "on anything written here. (Two names that alias one buffer, or two subslices "
                         "whose constant ranges overlap, are the same storage.) Give each `mut` "
                         "position its own storage",
                         nd->kids[0]->tok.line);
                    hit = true; break;
                }
            }
            // ★★★★ **쓰는 자리 하나 + 읽는 자리 — 선언한 짝만** (RFC-0116 D2 B1, 소유자 결정 ⓑ 2026-09-22).
            //   전엔 이 모양을 아무도 안 봤다(RFC-0115 §8-15 ⓒ — 지은 이의 의무). 이제 피호출자가
            //   `inplace <쓰기> <읽기> .` 로 그 짝을 허락했을 때만 같거나 겹치는 저장소를 받는다. 허락 없는 op 이
            //   몸 가운데서 그 자리에 먼저 쓰면 뒤의 읽기가 **바뀐 값**을 읽는다 — 진단이 아니라 틀린 답이다.
            //   ☞ 겹침 판정은 위와 같은 저장소 열쇠다. 구간이 상수가 아니면 가르지 않는다.
            for (proven_size_t q = 1; q < nd->nkids && !hit; q++) {
                proven_size_t ai = q - 1;
                if (ai >= h.np || !h.p[ai].is_mut) continue;
                ck_skey_t kq = ck_skey_seq(c, (const low_cst_t *const *)nd->kids, q, q + 1, 0);
                if (!kq.root.size) continue;
                for (proven_size_t r = 1; r < nd->nkids; r++) {
                    proven_size_t bi = r - 1;
                    if (r == q || bi >= h.np || h.p[bi].is_mut) continue;
                    ck_skey_t kr = ck_skey_seq(c, (const low_cst_t *const *)nd->kids, r, r + 1, 0);
                    if (!ck_sk_overlap(kq, kr)) continue;
                    // ★ `inplace` 는 «**같은 구간**이어도 된다» 만 허락한다. 부분 겹침(어긋난 구간 · 버퍼 전체와 그 일부)은
                    //   선언으로도 안 된다 — 원소마다 읽고 쓰는 몸(`madd` 꼴)은 같은 구간에서만 옳고, 부르는 쪽은 피호출자가
                    //   버퍼의 **어디에** 쓰는지 모른다. 저장소를 겹치지 않게 잘라 넘긴다(RFC-0116 §6.2 의 세 경우).
                    bool same = kq.np == kr.np && ((kq.whole && kr.whole) || (kq.known && kr.known && kq.a == kr.a && kq.b == kr.b));   // ★ 부모와 자식은 같은 구간이 아니다
                    if (same && ck_has_inplace(op->form, h.p[ai].name, h.p[bi].name)) continue;
                    if (!same) {
                        emit(out, "E-EXCL-INPLACE",
                             "the storage this op WRITES overlaps, but is not the same range as, storage it READS (shifted "
                             "ranges, or a whole buffer and a piece of it). No declaration allows that: an op that reads and "
                             "writes element by element is right only on the SAME range, and the caller cannot see where in "
                             "the buffer the op writes. Hand the op storage that does not overlap — carve the workspace so "
                             "the input lives outside the piece it writes (RFC-0116 D2 · §6.2)",
                             nd->kids[0]->tok.line);
                        hit = true; break;
                    }
                    // ★ 문구에 **피호출자와 두 입력 이름**을 싣는다 — 고칠 자리(`inplace <w> <r> .`)를 그대로 말한다.
                    //   진단은 문구를 가리키므로 버퍼는 한 번씩만 쓴다. 다 쓰면 일반 문구로 돌아간다(덮어쓰지 않는다).
                    static char ibuf[256][460]; static unsigned ibn;
                    const char *msg =
                         "the same storage is handed to this op in a position it WRITES and a position it READS, and "
                         "the op does not say it can take that. If its body writes before it has finished reading, the "
                         "later read sees the new value — a wrong answer, not a diagnostic. The op must declare the pair "
                         "(`inplace <written> <read> .`, RFC-0116 D2), or give the output its own storage";
                    if (ibn < 256) {
                        proven_u8str_view_t cn = nd->kids[0]->tok.lex, wn = h.p[ai].name, rn = h.p[bi].name;
                        snprintf(ibuf[ibn], sizeof ibuf[0],
                                 "the same storage goes to `%.*s`'s WRITTEN input `%.*s` and its READ input `%.*s`, and the op "
                                 "does not say it can take that — if its body writes before it has finished reading, the read "
                                 "sees the new value (a wrong answer, not a diagnostic). Declare `inplace %.*s %.*s .` on the op "
                                 "if its body reads first (RFC-0116 D2), or give the output its own storage",
                                 (int)cn.size, (const char *)cn.ptr, (int)wn.size, (const char *)wn.ptr,
                                 (int)rn.size, (const char *)rn.ptr, (int)wn.size, (const char *)wn.ptr,
                                 (int)rn.size, (const char *)rn.ptr);
                        msg = ibuf[ibn++];
                    }
                    emit(out, "E-EXCL-INPLACE", msg, nd->kids[0]->tok.line);
                    hit = true; break;
                }
            }
        }
    }
    for (proven_size_t i = 0; i < nd->nkids; i++) ck_excl_args_walk2(out, nd->kids[i], tab, nt, c);
}
// ★★★★ **struct 에서 빌린 동안 빌려준 저장소에 쓰지 않는다** (RFC-0116 D1 (iii), 소유자 결정 2026-09-21).
//   §8.12(4) 는 액터에게서 빌린 채 그 액터에게 말 거는 것을 막는다(`E-BORROW-EXCL`, `borrow … be send P …`).
//   그런데 풀은 2026-08-28 봉인(RFC-0104 §8-2)에서 **struct** 가 됐고, 그날부터 그 규칙이 풀을 못 지켰다 —
//   `borrow v be … do pool.release pa p hh . set (index v 0) 5 . end` 가 통과해 반환된 블록에 썼다.
//   ⇒ 빌림 머리 식에 나온 **저장소 이름**(`some_value (pool.bytes pa p hh)` 의 `p` 등)을 빌려준 자로 본다.
//     빌림 동안 ⓐ 그 이름을 부름의 `mut` 자리에 넘기거나 그 이름(의 칸·원소)에 직접 쓰는 것,
//     ⓑ 그 이름을 머리에 둔 **두 번째 빌림**을 여는 것 — 둘 다 `E-BORROW-EXCL` 이다.
//   ☞ 과엄격이다: 빌림 동안 같은 풀에서 **다른 블록**을 받거나 빌리는 것도 막는다(과엄격은 안전하다).
//   ☞ 못 막는 것(정직히): 빌림 **전에** `let bv be … bytes …` 로 뷰를 묶어 두면 식에 풀 이름이 없다.
//     그 모양은 라이브러리 규약(«`bytes` 는 빌림 머리에서만»)으로 막고, 규칙으로 막는 것은 D4(출처)의 몫이다.
#define CK_LEND_MAX 32
static proven_u8str_view_t ck_arg_root(const low_cst_t *nd, int depth) {
    proven_u8str_view_t z = { 0 };
    while (nd && nd->kind == LOW_CST_GROUP && nd->nkids == 1) nd = nd->kids[0];
    if (!nd || depth > 8) return z;
    if (nd->kind == LOW_CST_ATOM) return nd->tok.kind == LOW_TOK_IDENT ? nd->tok.lex : z;
    if ((nd->kind == LOW_CST_FORM || nd->kind == LOW_CST_GROUP) && nd->nkids >= 2 && ck_atom(nd->kids[0]) &&
        (veq(nd->kids[0]->tok.lex, "subslice") || veq(nd->kids[0]->tok.lex, "field") ||
         veq(nd->kids[0]->tok.lex, "index")))
        return ck_arg_root(nd->kids[1], depth + 1);
    return z;
}
static void ck_lend_names(const low_cst_t *nd, proven_u8str_view_t *v, proven_size_t *n, bool head_of_form, bool *full) {
    if (!nd) return;
    if (nd->kind == LOW_CST_ATOM) {
        if (head_of_form || nd->tok.kind != LOW_TOK_IDENT || nd->tok.kw != LOW_KW_NONE) return;
        if (ck_name_in(v, *n, nd->tok.lex)) return;
        if (*n >= CK_LEND_MAX) { *full = true; return; }      // ★ 넘침은 아래에서 E-IR-LIMIT 로 말한다
        v[*n] = nd->tok.lex; *n += 1;
        return;
    }
    for (proven_size_t i = 0; i < nd->nkids; i++)
        ck_lend_names(nd->kids[i], v, n, nd->kind == LOW_CST_FORM && i == 0, full);
}
static void ck_borrow_lend_walk(low_check_result_t *out, const low_cst_t *nd,
                                const low_opinfo_t *tab, proven_size_t nt,
                                const proven_u8str_view_t *lend, proven_size_t nlend) {
    if (!nd || nd->kind == LOW_CST_ATOM) return;
    bool is_borrow = (nd->kind == LOW_CST_FORM && nd->nkids >= 4 && ck_atom(nd->kids[0]) &&
                      veq(nd->kids[0]->tok.lex, "borrow") && ck_atom(nd->kids[1]));
    if (is_borrow) {
        proven_size_t bi = nd->nkids;
        const low_cst_t *blk = NULL;
        for (proven_size_t i = 0; i < nd->nkids; i++)
            if (nd->kids[i]->kind == LOW_CST_BLOCK) { blk = nd->kids[i]; bi = i; }
        if (!blk) {
            const low_cst_t *last = nd->kids[nd->nkids - 1];
            if (last && last->kind == LOW_CST_FORM && last->nkids &&
                last->kids[last->nkids - 1]->kind == LOW_CST_BLOCK) { blk = last->kids[last->nkids - 1]; bi = nd->nkids - 1; }
        }
        if (blk) {
            proven_u8str_view_t mine[CK_LEND_MAX]; proven_size_t nm = 0; bool full = false;
            for (proven_size_t i = 2; i < bi; i++)
                if (!(ck_atom(nd->kids[i]) && nd->kids[i]->tok.kw == LOW_KW_BE))
                    ck_lend_names(nd->kids[i], mine, &nm, false, &full);
            if (bi == nd->nkids - 1 && blk != nd->kids[bi]) {       // `be <식> do…end` 가 한 폼으로 붙은 모양
                const low_cst_t *last = nd->kids[bi];
                for (proven_size_t i = 0; i + 1 < last->nkids; i++) ck_lend_names(last->kids[i], mine, &nm, i == 0, &full);
            }
            // ⓑ 바깥 빌림의 빌려준 자로 두 번째 빌림을 연다
            for (proven_size_t i = 0; i < nm; i++)
                if (ck_name_in(lend, nlend, mine[i])) {
                    emit(out, "E-BORROW-EXCL",
                         "this opens a SECOND borrow from storage that is already lent out by an enclosing "
                         "`borrow`. Two live borrows of one storage are two writers (§8.12(1)) — each can "
                         "change what the other is reading. Close the outer borrow first, or take both "
                         "views in one borrow (RFC-0116 D1)",
                         nd->kids[0]->tok.line);
                    return;
                }
            // ★ 표가 차면 **자르지 않고 거절한다** — 잘린 빌려준 자는 검사가 안 도는 자리다(check-limits).
            if (full || nlend + nm > CK_LEND_MAX * 2) {
                emit(out, "E-IR-LIMIT",
                     "too many storage names in this borrow head (or in the borrows around it) for the "
                     "checker's lender table. Rather than check some of them and silently skip the rest, "
                     "this is refused — split the expression, or narrow the nesting",
                     nd->kids[0]->tok.line);
                return;
            }
            proven_u8str_view_t all[CK_LEND_MAX * 2]; proven_size_t na = 0;
            for (proven_size_t i = 0; i < nlend; i++) all[na++] = lend[i];
            for (proven_size_t i = 0; i < nm; i++) all[na++] = mine[i];
            for (proven_size_t i = 0; i < blk->nkids; i++) ck_borrow_lend_walk(out, blk->kids[i], tab, nt, all, na);
            return;
        }
    }
    if (nlend && nd->kind == LOW_CST_FORM && nd->nkids >= 2 && ck_atom(nd->kids[0])) {
        // ⓐ-1 빌려준 저장소(의 칸·원소)에 직접 쓴다
        if (nd->kids[0]->tok.kw == LOW_KW_SET) {
            proven_u8str_view_t r = ck_arg_root(nd->kids[1], 0);
            if (r.size && ck_name_in(lend, nlend, r)) {
                emit(out, "E-BORROW-EXCL",
                     "this writes to storage that is lent out by the enclosing `borrow`. While the borrow "
                     "lives, the lender has exactly one writer — the borrow (§8.12(1)). Write through the "
                     "borrowed name, or close the block first (RFC-0116 D1)",
                     nd->kids[0]->tok.line);
                return;
            }
        }
        // ⓐ-2 빌려준 저장소를 부름의 `mut` 자리에 넘긴다(`pool.release pa p hh` — 반환이 빌린 뷰를 무효로 만든다)
        if (nd->kids[0]->tok.kw == LOW_KW_NONE) {
            const low_opinfo_t *op = ck_find_callee(tab, nt, nd->kids[0]->tok.lex);
            if (op) {
                low_op_header_t h = low_op_header(op->form);
                for (proven_size_t q = 1; q < nd->nkids; q++) {
                    proven_size_t ai = q - 1;
                    if (ai >= h.np || !h.p[ai].is_mut) continue;
                    proven_u8str_view_t r = ck_arg_root(nd->kids[q], 0);
                    if (!r.size || !ck_name_in(lend, nlend, r)) continue;
                    emit(out, "E-BORROW-EXCL",
                         "this hands storage that is lent out by the enclosing `borrow` to a position the "
                         "callee WRITES. Whatever it does — release a block, grow, reset — can invalidate the "
                         "view you are holding, and nothing would say so: the view is a plain slice. "
                         "Finish with the borrow first (close the block), then call this. For a pool: take "
                         "the view in the borrow head and release after the block (RFC-0116 D1)",
                         nd->kids[0]->tok.line);
                    return;
                }
            }
        }
    }
    for (proven_size_t i = 0; i < nd->nkids; i++) ck_borrow_lend_walk(out, nd->kids[i], tab, nt, lend, nlend);
}

// ★★★★★ **무효화된 출처에서 나온 뷰를 쓰지 않는다** (RFC-0116 D4 I, 소유자 결정 2026-09-22).
//   풀의 `bytes` 가 준 슬라이스는 평범한 `{ptr, len}` 이라 반환을 모른다 — 빌림 **전에** 이름에 묶어 두면
//   `release` 뒤에도 쓸 수 있었고, 그 쓰기가 다음 주인의 블록과 자유 목록 링크를 덮었다(결함
//   `pool-view-outlives-release`). D1 (iii) 은 빌림 머리에서 꺼낸 뷰만 지켰다.
//   ⇒ op 몸 안에서 **뷰 지역**(타입에 `slice` 가 든 `let`/`var`/`borrow` 이름)마다 출처 — 초기식에 나온 저장소 이름,
//     뷰 이름이면 그 출처를 이어받는다 — 를 들고 다닌다. `invalidates <입력> .` 을 선언한 op 을 부르면 그 자리에
//     넘긴 저장소에서 나온 뷰가 모두 무효가 되고, 그 뒤 그 이름을 쓰면 `E-VIEW-INVALIDATED` 다.
//   ☞ 흐름: 문장 차례대로. 갈래(`if`·`match`)는 갈래마다 따로 보고 «어느 한 갈래에서라도 무효» 로 합친다.
//     반복(`while`·`for`)은 몸을 두 번 훑어 다음 바퀴로 넘어가는 무효화도 본다. 이름을 통째로 다시 대입하면
//     새 출처로 되살아난다.
//   ☞ 핸들·수 같은 **값**은 뷰가 아니다 — 반환 뒤의 핸들로 `alive`/`bytes` 를 묻는 것은 정상이다(세대가 답한다).
//   ☞ 출처는 뭉뚱그린다(부름의 결과는 인자 모두에서 나온다) — 과엄격한 쪽이다. 칸마다 출처(R1)는 수요가 생길 때.
#define CK_VT_MAX 64
#define CK_VT_SRC 8
// ★★★★ 후속 ① (2026-09-22) — **같은 부름 열쇠의 쓰기 뷰 둘**. 효과 없는 op(같은 인자면 같은 것을 돌려주는 접근자 —
//   `pool.bytes pa p hh`)을 이름·수 인자로 부른 결과는 «부름 열쇠» 를 든다. 같은 열쇠의 **쓰기 뷰**가 또 생기면 앞의 뷰는
//   쓰는 이가 둘이 된 것이다(§8.12(1)) — 그 뒤 앞의 뷰를 쓰면 `E-EXCL`. 효과가 있는 부름(`alloc_bytes`)은 매번 새 저장소라 뺀다.
#define CK_VT_KEY 10
typedef struct { proven_u8str_view_t name; proven_u8str_view_t src[CK_VT_SRC]; unsigned ns;
                 bool invalid, told; proven_u32 inval_line;
                 bool mutv, aliased; proven_u32 alias_line; proven_u8str_view_t key[CK_VT_KEY]; unsigned nk;
                 bool isfld; proven_u8str_view_t fld; } ck_view_t;   // isfld: `set (field name fld) <뷰>` 로 칸에 담은 뷰
typedef struct { ck_view_t v[CK_VT_MAX]; proven_size_t n; bool full; } ck_vstate_t;

static ck_view_t *ck_vt_find(ck_vstate_t *s, proven_u8str_view_t nm) {
    for (proven_size_t i = s->n; i-- > 0; ) if (!s->v[i].isfld && proven_u8str_view_eq(s->v[i].name, nm)) return &s->v[i];
    return NULL;
}
static ck_view_t *ck_vt_find_fld(ck_vstate_t *s, proven_u8str_view_t nm, proven_u8str_view_t fl) {
    for (proven_size_t i = s->n; i-- > 0; )
        if (s->v[i].isfld && proven_u8str_view_eq(s->v[i].name, nm) && proven_u8str_view_eq(s->v[i].fld, fl)) return &s->v[i];
    return NULL;
}
static void ck_vt_addsrc(ck_vstate_t *s, ck_view_t *d, proven_u8str_view_t w) {
    for (unsigned k = 0; k < d->ns; k++) if (proven_u8str_view_eq(d->src[k], w)) return;
    if (d->ns >= CK_VT_SRC) { s->full = true; return; }
    d->src[d->ns++] = w;
}
// 식에 나온 저장소 이름을 출처로 모은다(부름의 머리 낱말 · 예약어 · 수는 빼고, 뷰 이름은 그 출처를 잇는다)
static void ck_vt_sources(ck_vstate_t *s, ck_view_t *d, const low_cst_t *nd, bool head) {
    if (!nd) return;
    if (nd->kind == LOW_CST_ATOM) {
        if (head || nd->tok.kind != LOW_TOK_IDENT || nd->tok.kw != LOW_KW_NONE) return;
        ck_view_t *o = ck_vt_find(s, nd->tok.lex);
        if (o) { for (unsigned k = 0; k < o->ns; k++) ck_vt_addsrc(s, d, o->src[k]); return; }
        ck_vt_addsrc(s, d, nd->tok.lex);
        return;
    }
    for (proven_size_t i = 0; i < nd->nkids; i++)
        ck_vt_sources(s, d, nd->kids[i], nd->kind == LOW_CST_FORM && i == 0);
}
static bool ck_vt_is_view_type(const low_cst_t *f, proven_size_t from, proven_size_t to) {
    for (proven_size_t i = from; i < to; i++) {
        const low_cst_t *k = f->kids[i];
        if (ck_atom(k) && (veq(k->tok.lex, "slice") || veq(k->tok.lex, "view"))) return true;
        if (k->kind == LOW_CST_GROUP || k->kind == LOW_CST_FORM)
            if (ck_vt_is_view_type(k, 0, k->nkids)) return true;
    }
    return false;
}
static ck_view_t *ck_vt_new(ck_vstate_t *s, proven_u8str_view_t nm) {
    if (s->n >= CK_VT_MAX) { s->full = true; return NULL; }
    ck_view_t *d = &s->v[s->n++];
    memset(d, 0, sizeof *d); d->name = nm;
    return d;
}
// 쓰인 이름 가운데 무효가 된 뷰가 있으면 말한다(통째 대입의 대상 자리는 쓰기가 아니라 새 출처라 뺀다)
static void ck_vt_uses(low_check_result_t *out, ck_vstate_t *s, const low_cst_t *nd, const low_cst_t *skip) {
    if (!nd || nd == skip) return;
    if (nd->kind == LOW_CST_ATOM) {
        if (nd->tok.kind != LOW_TOK_IDENT) return;
        ck_view_t *v = ck_vt_find(s, nd->tok.lex);
        if (v && !v->invalid && v->aliased && !v->told) {
            static char abuf[64][360]; static unsigned abn;
            const char *msg = "this write view shares its storage with a second write view taken later from the same "
                              "accessor call — two writers of one storage (§8.12(1)); either can change what the other "
                              "holds. Take one view, or take the second only after you are done with the first";
            if (abn < 64) {
                snprintf(abuf[abn], sizeof abuf[0],
                         "`%.*s` is a write view, and line %u took a SECOND write view of the same storage from the same "
                         "accessor call — two writers of one storage (§8.12(1)); either can change what the other holds. "
                         "Use one view, or take the second only after you are done with the first",
                         (int)v->name.size, (const char *)v->name.ptr, (unsigned)v->alias_line);
                msg = abuf[abn++];
            }
            emit(out, "E-EXCL", msg, nd->tok.line);
            v->told = true;
        }
        if (v && v->invalid && !v->told) {
            static char vbuf[128][360]; static unsigned vbn;
            const char *msg = "this view came from storage that a call above made INVALID (it released a block, grew and "
                              "moved, or rewound — the callee says so with `invalidates`). The view is a plain slice and "
                              "does not know: reading it sees stale bytes, writing it lands in memory someone else now owns. "
                              "Take the view again after the call (RFC-0116 D4)";
            if (vbn < 128) {
                snprintf(vbuf[vbn], sizeof vbuf[0],
                         "`%.*s` is a view whose storage was made INVALID by the call on line %u (the callee declares "
                         "`invalidates`: it released, moved or rewound that storage). The view does not know — reading it "
                         "sees stale bytes, writing it lands in memory someone else now owns. Take the view again after "
                         "that call (RFC-0116 D4)",
                         (int)v->name.size, (const char *)v->name.ptr, (unsigned)v->inval_line);
                msg = vbuf[vbn++];
            }
            emit(out, "E-VIEW-INVALIDATED", msg, nd->tok.line);
            v->told = true;
        }
        return;
    }
    // ★ 후속 ② — 칸에 담은 뷰: `(field h f)` 를 읽으면 그 칸의 출처를 본다
    if (nd->kind == LOW_CST_FORM && nd->nkids >= 3 && ck_atom(nd->kids[0]) && veq(nd->kids[0]->tok.lex, "field") &&
        ck_atom(nd->kids[1]) && ck_atom(nd->kids[2])) {
        ck_view_t *v = ck_vt_find_fld(s, nd->kids[1]->tok.lex, nd->kids[2]->tok.lex);
        if (v && v->invalid && !v->told) {
            emit(out, "E-VIEW-INVALIDATED",
                 "this field holds a view (stored with `set (field …) <view>`) whose storage a call above made INVALID — "
                 "the callee releases, moves or rewinds it (declared with `invalidates`, or inferred from a call it makes). "
                 "The field still points at the old place. Store a fresh view after that call (RFC-0116 D4)",
                 nd->kids[0]->tok.line);
            v->told = true;
        }
    }
    for (proven_size_t i = 0; i < nd->nkids; i++) ck_vt_uses(out, s, nd->kids[i], skip);
}
// ★★★★ 후속 ② (2026-09-22) — **무효화도 op 경계를 넘는다.** `invalidates` 를 적지 않았어도 몸 안에서 무효화 op 에
//   자기 입력을 넘기는 op(감싼 함수)은 그 입력을 무효로 만든다. 입력마다 한 비트 — 재귀는 고정점까지 돈다.
#define CK_INV_OPS 4096
static const low_opinfo_t *ck_inv_tab; static proven_size_t ck_inv_nt;
static unsigned long long ck_inv_mask[CK_INV_OPS]; static unsigned char ck_inv_state[CK_INV_OPS];   // 0 모름 · 1 도는 중 · 2 앎
static unsigned long long ck_inv_params(const low_opinfo_t *op);
static void ck_inv_scan(const low_cst_t *nd, const low_op_header_t *mine, unsigned long long *mask) {
    if (!nd || nd->kind == LOW_CST_ATOM) return;
    for (proven_size_t i = 0; i < nd->nkids; i++) ck_inv_scan(nd->kids[i], mine, mask);
    if (nd->kind != LOW_CST_FORM || nd->nkids < 2 || !ck_atom(nd->kids[0]) || nd->kids[0]->tok.kw != LOW_KW_NONE) return;
    const low_opinfo_t *callee = ck_find_callee(ck_inv_tab, ck_inv_nt, nd->kids[0]->tok.lex);
    if (!callee) return;
    unsigned long long cm = ck_inv_params(callee);
    if (!cm) return;
    for (proven_size_t q = 0; q < 64 && q + 1 < nd->nkids; q++) {
        if (!(cm >> q & 1ull)) continue;
        proven_u8str_view_t r = ck_arg_root(nd->kids[q + 1], 0);
        if (!r.size) continue;
        for (proven_size_t m = 0; m < mine->np && m < 64; m++)
            if (proven_u8str_view_eq(mine->p[m].name, r)) *mask |= 1ull << m;
    }
}
static unsigned long long ck_inv_params(const low_opinfo_t *op) {
    proven_size_t idx = (proven_size_t)(op - ck_inv_tab);
    if (!ck_inv_tab || idx >= ck_inv_nt || idx >= CK_INV_OPS) return 0;
    if (ck_inv_state[idx] == 2 || ck_inv_state[idx] == 1) return ck_inv_mask[idx];   // 도는 중이면 지금까지 안 것
    ck_inv_state[idx] = 1;
    low_op_header_t h = low_op_header(op->form);
    unsigned long long m = 0;
    const low_cst_t *form = op->form;
    for (proven_size_t i = 0; i + 1 < form->nkids; i++) {             // 선언한 것
        if (form->kids[i]->kind == LOW_CST_BLOCK) break;
        if (ck_atom(form->kids[i]) && veq(form->kids[i]->tok.lex, "invalidates") && ck_atom(form->kids[i + 1]))
            for (proven_size_t q = 0; q < h.np && q < 64; q++)
                if (proven_u8str_view_eq(h.p[q].name, form->kids[i + 1]->tok.lex)) m |= 1ull << q;
    }
    ck_inv_mask[idx] = m;
    if (op->body) ck_inv_scan(op->body, &h, &m);                        // 추론한 것
    ck_inv_mask[idx] = m;
    ck_inv_state[idx] = 2;
    return m;
}
static void ck_inv_prepare(const low_opinfo_t *tab, proven_size_t nt) {
    if (ck_inv_tab == tab && ck_inv_nt == nt) return;
    ck_inv_tab = tab; ck_inv_nt = nt;
    memset(ck_inv_state, 0, sizeof ck_inv_state); memset(ck_inv_mask, 0, sizeof ck_inv_mask);
    // ★ 고정점: 재귀 사슬에서 «도는 중» 으로 읽힌 부분 답이 있을 수 있으므로 바뀌지 않을 때까지 다시 돈다
    for (int round = 0; round < 8; round++) {
        unsigned long long before = 0, after = 0;
        for (proven_size_t i = 0; i < nt && i < CK_INV_OPS; i++) before += ck_inv_mask[i] * (i + 1);
        for (proven_size_t i = 0; i < nt && i < CK_INV_OPS; i++) ck_inv_state[i] = 0;
        for (proven_size_t i = 0; i < nt && i < CK_INV_OPS; i++) (void)ck_inv_params(&tab[i]);
        for (proven_size_t i = 0; i < nt && i < CK_INV_OPS; i++) after += ck_inv_mask[i] * (i + 1);
        if (before == after) break;
    }
}
// 이 식 안의 부름 가운데 `invalidates` 를 선언한 것이 있으면 그 자리의 저장소에서 나온 뷰를 무효로 만든다
static void ck_vt_invalidate(ck_vstate_t *s, const low_cst_t *nd, const low_opinfo_t *tab, proven_size_t nt) {
    if (!nd || nd->kind == LOW_CST_ATOM) return;
    for (proven_size_t i = 0; i < nd->nkids; i++) ck_vt_invalidate(s, nd->kids[i], tab, nt);
    if (nd->kind != LOW_CST_FORM || nd->nkids < 2 || !ck_atom(nd->kids[0]) || nd->kids[0]->tok.kw != LOW_KW_NONE) return;
    const low_opinfo_t *op = ck_find_callee(tab, nt, nd->kids[0]->tok.lex);
    if (!op) return;
    unsigned long long cm = ck_inv_params(op);                          // 선언 + 추론(후속 ②)
    for (proven_size_t q = 0; q < 64 && q + 1 < nd->nkids; q++) {
        if (!(cm >> q & 1ull)) continue;
        proven_u8str_view_t r = ck_arg_root(nd->kids[q + 1], 0);
        if (!r.size) continue;
        for (proven_size_t k = 0; k < s->n; k++)
            for (unsigned m = 0; m < s->v[k].ns; m++)
                if (proven_u8str_view_eq(s->v[k].src[m], r) && !s->v[k].invalid) {
                    s->v[k].invalid = true; s->v[k].inval_line = nd->kids[0]->tok.line; break;
                }
    }
}
// 초기식이 «효과 없는 op 을 이름·수 인자로 부른 것» 이면 그 부름 열쇠를 적는다(`some_value` 겹은 벗긴다).
//   돌려주는 것이 `mut` 뷰인지도 함께 답한다(피호출자의 `output` 절에 `mut` 이 있나).
static bool ck_vt_key(ck_view_t *d, const low_cst_t *const *k, proven_size_t from, proven_size_t to,
                      const low_opinfo_t *tab, proven_size_t nt, bool *out_mut) {
    for (int hop = 0; hop < 4; hop++) {
        if (to - from == 1) {
            const low_cst_t *n = k[from];
            while (n && n->kind == LOW_CST_GROUP && n->nkids == 1) n = n->kids[0];
            if (!n || n->kind == LOW_CST_ATOM) return false;
            k = (const low_cst_t *const *)n->kids; from = 0; to = n->nkids;
            continue;
        }
        if (ck_atom(k[from]) && veq(k[from]->tok.lex, "some_value")) { from++; continue; }
        break;
    }
    if (to - from < 2 || !ck_atom(k[from]) || to - from > CK_VT_KEY) return false;
    const low_opinfo_t *op = ck_find_callee(tab, nt, k[from]->tok.lex);
    if (!op || op->declared != 0) return false;                       // 효과 없는 접근자만
    for (proven_size_t i = from + 1; i < to; i++) if (!ck_atom(k[i])) return false;
    d->nk = 0;
    for (proven_size_t i = from; i < to; i++) d->key[d->nk++] = k[i]->tok.lex;
    bool m = false; const low_cst_t *form = op->form;
    for (proven_size_t i = 0; i < form->nkids; i++) {
        if (form->kids[i]->kind == LOW_CST_BLOCK) break;
        if (ck_atom(form->kids[i]) && veq(form->kids[i]->tok.lex, "output")) {
            for (proven_size_t j = i + 1; j < form->nkids && ck_atom(form->kids[j]) && !ck_clause_word(form->kids[j]->tok.lex); j++)
                if (veq(form->kids[j]->tok.lex, "mut")) m = true;
            break;
        }
    }
    *out_mut = m;
    return true;
}
static bool ck_vt_same_key(const ck_view_t *a, const ck_view_t *b) {
    if (!a->nk || a->nk != b->nk) return false;
    for (unsigned i = 0; i < a->nk; i++) if (!proven_u8str_view_eq(a->key[i], b->key[i])) return false;
    return true;
}
// 새 쓰기 뷰 d 와 같은 열쇠를 가진, 아직 살아 있는 쓰기 뷰에 «쓰는 이가 둘» 표시
static void ck_vt_mark_alias(ck_vstate_t *s, ck_view_t *d, proven_u32 line) {
    if (!d->mutv || !d->nk) return;
    for (proven_size_t i = 0; i < s->n; i++) {
        ck_view_t *o = &s->v[i];
        if (o == d || !o->mutv || o->invalid || o->aliased) continue;
        if (ck_vt_same_key(o, d)) { o->aliased = true; o->alias_line = line; }
    }
}
static void ck_vt_block(low_check_result_t *out, ck_vstate_t *s, const low_cst_t *blk,
                        const low_opinfo_t *tab, proven_size_t nt);
static void ck_vt_stmt(low_check_result_t *out, ck_vstate_t *s, const low_cst_t *nd,
                       const low_opinfo_t *tab, proven_size_t nt) {
    if (!nd || nd->kind == LOW_CST_ATOM) return;
    if (nd->kind == LOW_CST_BLOCK) { ck_vt_block(out, s, nd, tab, nt); return; }
    if (nd->kind != LOW_CST_FORM || !nd->nkids || !ck_atom(nd->kids[0])) { ck_vt_uses(out, s, nd, NULL); ck_vt_invalidate(s, nd, tab, nt); return; }
    low_kw_t kw = nd->kids[0]->tok.kw;
    bool has_block = false;
    for (proven_size_t i = 0; i < nd->nkids; i++) if (nd->kids[i]->kind == LOW_CST_BLOCK) has_block = true;
    // borrow <이름> be <식> do … end — 빌린 이름도 뷰다(머리 식의 출처를 잇는다)
    if (ck_atom(nd->kids[0]) && veq(nd->kids[0]->tok.lex, "borrow") && nd->nkids >= 4 && ck_atom(nd->kids[1])) {
        const low_cst_t *blk = NULL; proven_size_t bi = nd->nkids;
        for (proven_size_t i = 0; i < nd->nkids; i++) if (nd->kids[i]->kind == LOW_CST_BLOCK) { blk = nd->kids[i]; bi = i; }
        // ★ 머리 식과 블록이 한 폼으로 붙는 꼴 `borrow v be FORM[<식…> BLOCK]` 이 흔하다(다른 빌림 검사도 이 꼴을 본다)
        const low_cst_t *glued = NULL;
        if (!blk) {
            const low_cst_t *last = nd->kids[nd->nkids - 1];
            if (last && last->kind == LOW_CST_FORM && last->nkids && last->kids[last->nkids - 1]->kind == LOW_CST_BLOCK) {
                blk = last->kids[last->nkids - 1]; bi = nd->nkids - 1; glued = last;
            }
        }
        for (proven_size_t i = 2; i < bi; i++) { ck_vt_uses(out, s, nd->kids[i], NULL); ck_vt_invalidate(s, nd->kids[i], tab, nt); }
        if (glued) for (proven_size_t i = 0; i + 1 < glued->nkids; i++) { ck_vt_uses(out, s, glued->kids[i], NULL); ck_vt_invalidate(s, glued->kids[i], tab, nt); }
        proven_size_t keep = s->n;
        ck_view_t *d = ck_vt_new(s, nd->kids[1]->tok.lex);
        if (d) {
            for (proven_size_t i = 2; i < bi; i++) ck_vt_sources(s, d, nd->kids[i], false);
            if (glued) for (proven_size_t i = 0; i + 1 < glued->nkids; i++) ck_vt_sources(s, d, glued->kids[i], i == 0 && glued->nkids > 2);
            bool omut = false, hk = false;
            if (glued) hk = ck_vt_key(d, (const low_cst_t *const *)glued->kids, 0, glued->nkids - 1, tab, nt, &omut);
            else if (bi > 3) hk = ck_vt_key(d, (const low_cst_t *const *)nd->kids, 3, bi, tab, nt, &omut);
            if (hk) { d->mutv = omut; ck_vt_mark_alias(s, d, nd->kids[0]->tok.line); }
        }
        if (blk) ck_vt_block(out, s, blk, tab, nt);
        // 빌린 이름은 블록과 함께 끝난다 — 바깥 뷰의 무효 표시는 남긴다
        s->n = keep;
        return;
    }
    if (has_block) {
        // 조건·머리(블록 아닌 조각) 먼저
        for (proven_size_t i = 1; i < nd->nkids; i++)
            if (nd->kids[i]->kind != LOW_CST_BLOCK) { ck_vt_uses(out, s, nd->kids[i], NULL); ck_vt_invalidate(s, nd->kids[i], tab, nt); }
        bool loop = (kw == LOW_KW_WHILE || kw == LOW_KW_FOR);
        if (loop) {
            for (int pass = 0; pass < 2; pass++)
                for (proven_size_t i = 1; i < nd->nkids; i++)
                    if (nd->kids[i]->kind == LOW_CST_BLOCK) ck_vt_block(out, s, nd->kids[i], tab, nt);
            return;
        }
        // 갈래 — 갈래마다 앞 상태의 사본으로 보고, 무효 표시를 합친다
        static ck_vstate_t pre[16]; static int depth;
        if (depth >= 16) { s->full = true; return; }
        ck_vstate_t *p0 = &pre[depth++];
        *p0 = *s;
        ck_vstate_t acc = *s;
        for (proven_size_t i = 1; i < nd->nkids; i++) {
            if (nd->kids[i]->kind != LOW_CST_BLOCK) continue;
            ck_vstate_t br = *p0;
            ck_vt_block(out, &br, nd->kids[i], tab, nt);
            for (proven_size_t k = 0; k < p0->n && k < br.n; k++) {
                if (br.v[k].invalid && !acc.v[k].invalid) { acc.v[k].invalid = true; acc.v[k].inval_line = br.v[k].inval_line; }
                if (br.v[k].told) acc.v[k].told = true;
            }
            if (br.full) acc.full = true;
        }
        depth--;
        *s = acc;
        return;
    }
    // ★ 후속 ② — `set (field h f) <식>` : 식이 뷰를 들면 그 칸이 뷰의 출처를 든다(칸에 담은 뷰)
    if (kw == LOW_KW_SET && nd->nkids >= 3) {
        const low_cst_t *tg = nd->kids[1];
        while (tg && tg->kind == LOW_CST_GROUP && tg->nkids == 1) tg = tg->kids[0];
        if (tg && tg->kind == LOW_CST_FORM && tg->nkids == 3 && ck_atom(tg->kids[0]) && veq(tg->kids[0]->tok.lex, "field") &&
            ck_atom(tg->kids[1]) && ck_atom(tg->kids[2])) {
            for (proven_size_t i = 2; i < nd->nkids; i++) { ck_vt_uses(out, s, nd->kids[i], NULL); ck_vt_invalidate(s, nd->kids[i], tab, nt); }
            ck_view_t tmp; memset(&tmp, 0, sizeof tmp);
            bool holds_view = false;
            for (proven_size_t i = 2; i < nd->nkids; i++) {
                const low_cst_t *v = nd->kids[i];
                while (v && v->kind == LOW_CST_GROUP && v->nkids == 1) v = v->kids[0];
                if (v && ck_atom(v) && ck_vt_find(s, v->tok.lex)) holds_view = true;
                ck_vt_sources(s, &tmp, nd->kids[i], false);
            }
            ck_view_t *d = ck_vt_find_fld(s, tg->kids[1]->tok.lex, tg->kids[2]->tok.lex);
            if (holds_view) {
                if (!d) d = ck_vt_new(s, tg->kids[1]->tok.lex);
                if (d) { proven_u8str_view_t nm = tg->kids[1]->tok.lex, fl = tg->kids[2]->tok.lex;
                         *d = tmp; d->name = nm; d->fld = fl; d->isfld = true; }
            } else if (d) {                                          // 뷰가 아닌 것을 담으면 그 칸은 더 뷰가 아니다
                d->invalid = false; d->ns = 0; d->told = true;
            }
            return;
        }
    }
    // 통째 대입 `set <뷰 이름> <식>` — 새 출처로 되살린다
    if (kw == LOW_KW_SET && nd->nkids >= 3 && ck_atom(nd->kids[1]) && ck_vt_find(s, nd->kids[1]->tok.lex)) {
        for (proven_size_t i = 2; i < nd->nkids; i++) { ck_vt_uses(out, s, nd->kids[i], NULL); ck_vt_invalidate(s, nd->kids[i], tab, nt); }
        ck_view_t *d = ck_vt_find(s, nd->kids[1]->tok.lex);
        ck_view_t fresh; memset(&fresh, 0, sizeof fresh); fresh.name = d->name;
        for (proven_size_t i = 2; i < nd->nkids; i++) ck_vt_sources(s, &fresh, nd->kids[i], false);
        *d = fresh;
        return;
    }
    ck_vt_uses(out, s, nd, NULL);
    ck_vt_invalidate(s, nd, tab, nt);
    // let/var <이름> <타입…> be <식> — 타입에 slice 가 들면 뷰로 따라간다
    if ((kw == LOW_KW_LET || kw == LOW_KW_VAR) && nd->nkids >= 4 && ck_atom(nd->kids[1])) {
        proven_size_t be = nd->nkids;
        for (proven_size_t q = 2; q < nd->nkids; q++)
            if (ck_atom(nd->kids[q]) && nd->kids[q]->tok.kw == LOW_KW_BE) { be = q; break; }
        if (be < nd->nkids && ck_vt_is_view_type(nd, 2, be)) {
            ck_view_t *d = ck_vt_new(s, nd->kids[1]->tok.lex);
            if (d) {
                for (proven_size_t q = be + 1; q < nd->nkids; q++) ck_vt_sources(s, d, nd->kids[q], false);
                bool tmut = false;
                for (proven_size_t q = 2; q < be; q++) if (ck_atom(nd->kids[q]) && veq(nd->kids[q]->tok.lex, "mut")) tmut = true;
                bool omut = false;
                if (ck_vt_key(d, (const low_cst_t *const *)nd->kids, be + 1, nd->nkids, tab, nt, &omut)) {
                    d->mutv = tmut;
                    ck_vt_mark_alias(s, d, nd->kids[0]->tok.line);
                }
            }
        }
    }
}
static void ck_vt_block(low_check_result_t *out, ck_vstate_t *s, const low_cst_t *blk,
                        const low_opinfo_t *tab, proven_size_t nt) {
    proven_size_t keep = s->n;
    for (proven_size_t i = 0; i < blk->nkids; i++) ck_vt_stmt(out, s, blk->kids[i], tab, nt);
    s->n = keep;          // 블록 안에서 난 이름은 블록과 함께 끝난다
}
static void ck_view_inval_walk(low_check_result_t *out, const low_cst_t *body,
                               const low_opinfo_t *tab, proven_size_t nt) {
    if (!body || body->kind != LOW_CST_BLOCK) return;
    static ck_vstate_t s;
    s.n = 0; s.full = false;
    ck_inv_prepare(tab, nt);
    ck_vt_block(out, &s, body, tab, nt);
    if (nt > CK_INV_OPS) s.full = true;      // 추론 표 밖의 op 은 선언도 못 읽는다 — 놓치느니 거절한다
    if (s.full)                              // ★ 표가 차면 자르지 않고 거절한다(check-limits)
        emit(out, "E-IR-LIMIT",
             "this op holds more views (or view sources, or nested branches) — or the program more ops — than the invalidation tracker's table — "
             "refused rather than tracked partly, which would let a stale view through. Split the op (RFC-0116 D4)",
             body->nkids && body->kids[0]->kind != LOW_CST_ATOM && body->kids[0]->nkids && ck_atom(body->kids[0]->kids[0])
                 ? body->kids[0]->kids[0]->tok.line : 0);
}

// ★★★★★ 후속 ④ B2 (2026-09-22) — **`inplace w r` 선언이 참인지 몸에서 본다.** 선언은 «같은 구간이어도 옳다» 는 약속이고,
//   이제 처리기가 두 모양 가운데 하나를 요구한다(못 보이면 `E-INPLACE-UNPROVEN`):
//     ⓐ 읽기 먼저 — `r`(과 그 별칭)을 마지막으로 읽는 윗문장 L 보다 **앞의** 윗문장은 `w`(와 그 별칭)에 쓰지 않는다. L 뒤는 자유.
//     ⓑ 원소별 — L 자신이 `w` 에 쓰면: L 안의 `r` 읽기는 모두 `(index r V)`, `w` 쓰기는 모두 `set (index w V) …` 이고 V 는
//        **한 낱말**로 같다. L 이 반복이면 그 몸의 윗문장 차례에서 `r` 읽기가 `w` 쓰기보다 뒤에 오지 않는다.
//   ☞ 위임: `w`·`r` 을 **그대로** 받는 부름이 피호출자의 선언한 짝(`inplace X Y`)으로 넘기면 그 부름은 ⓑ 를 만족한 쓰기로 본다.
//   ☞ `w` 를 부름의 `mut` 자리에 넘기는 것은 쓰기, `r` 을 어디든 넘기는 것은 읽기다. 뷰 별칭(`let x be subslice r …`)은 따라간다.
#define CK_B2_NAMES 16
typedef struct { proven_u8str_view_t r[CK_B2_NAMES], w[CK_B2_NAMES]; proven_size_t nr, nw; bool full; } ck_b2_t;
static bool ck_b2_in(const proven_u8str_view_t *v, proven_size_t n, proven_u8str_view_t x) {
    for (proven_size_t i = 0; i < n; i++) if (proven_u8str_view_eq(v[i], x)) return true;
    return false;
}
static bool ck_b2_mentions(const low_cst_t *nd, const proven_u8str_view_t *v, proven_size_t n) {
    if (!nd) return false;
    if (nd->kind == LOW_CST_ATOM) return nd->tok.kind == LOW_TOK_IDENT && ck_b2_in(v, n, nd->tok.lex);
    for (proven_size_t i = 0; i < nd->nkids; i++) if (ck_b2_mentions(nd->kids[i], v, n)) return true;
    return false;
}
// 별칭 모으기: `let/var x be … <식>` 의 식이 r(w) 이름을 쓰고 x 가 뷰 타입이면 x 도 r(w) 다
static void ck_b2_aliases(const low_cst_t *nd, ck_b2_t *b) {
    if (!nd || nd->kind == LOW_CST_ATOM) return;
    if (nd->kind == LOW_CST_FORM && nd->nkids >= 4 && ck_atom(nd->kids[0]) &&
        (nd->kids[0]->tok.kw == LOW_KW_LET || nd->kids[0]->tok.kw == LOW_KW_VAR) && ck_atom(nd->kids[1])) {
        proven_size_t be = nd->nkids;
        for (proven_size_t q = 2; q < nd->nkids; q++) if (ck_atom(nd->kids[q]) && nd->kids[q]->tok.kw == LOW_KW_BE) { be = q; break; }
        if (be < nd->nkids && ck_vt_is_view_type(nd, 2, be)) {
            for (proven_size_t q = be + 1; q < nd->nkids; q++) {
                if (ck_b2_mentions(nd->kids[q], b->r, b->nr) && !ck_b2_in(b->r, b->nr, nd->kids[1]->tok.lex)) {
                    if (b->nr >= CK_B2_NAMES) b->full = true; else b->r[b->nr++] = nd->kids[1]->tok.lex;
                }
                if (ck_b2_mentions(nd->kids[q], b->w, b->nw) && !ck_b2_in(b->w, b->nw, nd->kids[1]->tok.lex)) {
                    if (b->nw >= CK_B2_NAMES) b->full = true; else b->w[b->nw++] = nd->kids[1]->tok.lex;
                }
            }
        }
    }
    for (proven_size_t i = 0; i < nd->nkids; i++) ck_b2_aliases(nd->kids[i], b);
}
// r 을 읽나(쓰기 대상 자리 밖에서 r 이름이 나오나)
static bool ck_b2_reads_r(const low_cst_t *nd, const ck_b2_t *b) { return ck_b2_mentions(nd, b->r, b->nr); }
// w 에 쓰는 자리를 훑는다. 모양 ⓑ 를 만족하는 쓰기(색인 V, 또는 위임)만 있으면 *ok, 아니면 *bad. 쓰기가 하나라도 있으면 *any.
static void ck_b2_writes(const low_cst_t *nd, const ck_b2_t *b, const low_opinfo_t *tab, proven_size_t nt,
                         proven_u8str_view_t *var, bool *any, bool *bad) {
    if (!nd || nd->kind == LOW_CST_ATOM) return;
    if (nd->kind == LOW_CST_FORM && nd->nkids >= 2 && ck_atom(nd->kids[0])) {
        low_kw_t kw = nd->kids[0]->tok.kw;
        if (kw == LOW_KW_SET) {
            const low_cst_t *tg = nd->kids[1];
            while (tg && tg->kind == LOW_CST_GROUP && tg->nkids == 1) tg = tg->kids[0];
            proven_u8str_view_t root = ck_arg_root(tg, 0);
            if (root.size && ck_b2_in(b->w, b->nw, root)) {
                *any = true;
                bool idx_ok = tg && tg->kind == LOW_CST_FORM && tg->nkids == 3 && ck_atom(tg->kids[0]) &&
                              veq(tg->kids[0]->tok.lex, "index") && ck_atom(tg->kids[1]) && ck_atom(tg->kids[2]);
                if (!idx_ok) *bad = true;
                else if (!var->size) *var = tg->kids[2]->tok.lex;
                else if (!proven_u8str_view_eq(*var, tg->kids[2]->tok.lex)) *bad = true;
            }
        } else if (kw == LOW_KW_NONE) {
            const low_opinfo_t *op = ck_find_callee(tab, nt, nd->kids[0]->tok.lex);
            if (op) {
                low_op_header_t h = low_op_header(op->form);
                for (proven_size_t q = 0; q + 1 < nd->nkids && q < h.np; q++) {
                    if (!h.p[q].is_mut) continue;
                    proven_u8str_view_t root = ck_arg_root(nd->kids[q + 1], 0);
                    if (!root.size || !ck_b2_in(b->w, b->nw, root)) continue;
                    *any = true;
                    // 위임: w 를 그대로 받은 자리 X 에 대해, r 을 받은 모든 자리 Y 가 선언된 짝이면 좋다
                    const low_cst_t *wa = nd->kids[q + 1];
                    while (wa && wa->kind == LOW_CST_GROUP && wa->nkids == 1) wa = wa->kids[0];
                    bool whole_w = wa && ck_atom(wa);
                    bool deleg = whole_w;
                    for (proven_size_t y = 0; y + 1 < nd->nkids && y < h.np && deleg; y++) {
                        if (y == q || !ck_b2_mentions(nd->kids[y + 1], b->r, b->nr)) continue;
                        const low_cst_t *ra = nd->kids[y + 1];
                        while (ra && ra->kind == LOW_CST_GROUP && ra->nkids == 1) ra = ra->kids[0];
                        if (!(ra && ck_atom(ra)) || !ck_has_inplace(op->form, h.p[q].name, h.p[y].name)) deleg = false;
                    }
                    if (!deleg) *bad = true;
                }
            }
        }
    }
    for (proven_size_t i = 0; i < nd->nkids; i++) ck_b2_writes(nd->kids[i], b, tab, nt, var, any, bad);
}
// r 읽기가 모두 `(index r V)` 인가(V 가 정해졌으면 같아야 한다)
static bool ck_b2_reads_indexed(const low_cst_t *nd, const ck_b2_t *b, proven_u8str_view_t var) {
    if (!nd) return true;
    if (nd->kind == LOW_CST_ATOM) return !(nd->tok.kind == LOW_TOK_IDENT && ck_b2_in(b->r, b->nr, nd->tok.lex));
    if (nd->kind == LOW_CST_FORM && nd->nkids == 3 && ck_atom(nd->kids[0]) && veq(nd->kids[0]->tok.lex, "index") &&
        ck_atom(nd->kids[1]) && ck_b2_in(b->r, b->nr, nd->kids[1]->tok.lex))
        return ck_atom(nd->kids[2]) && (!var.size || proven_u8str_view_eq(var, nd->kids[2]->tok.lex));
    // `set (index w V) …` 의 대상 자리는 읽기가 아니다
    for (proven_size_t i = 0; i < nd->nkids; i++) if (!ck_b2_reads_indexed(nd->kids[i], b, var)) return false;
    return true;
}
static bool ck_b2_pair_ok(const low_cst_t *body, proven_u8str_view_t w, proven_u8str_view_t r,
                          const low_opinfo_t *tab, proven_size_t nt, bool *full) {
    ck_b2_t b; memset(&b, 0, sizeof b);
    b.r[b.nr++] = r; b.w[b.nw++] = w;
    for (int k = 0; k < 3; k++) ck_b2_aliases(body, &b);             // 별칭의 별칭
    if (b.full) { *full = true; return false; }
    proven_size_t L = body->nkids;                                     // r 을 마지막으로 읽는 윗문장
    for (proven_size_t i = body->nkids; i-- > 0; ) if (ck_b2_reads_r(body->kids[i], &b)) { L = i; break; }
    if (L == body->nkids) return true;                                 // r 을 안 읽는다
    for (proven_size_t i = 0; i < L; i++) {                            // ⓐ L 앞은 w 에 안 쓴다
        proven_u8str_view_t v = { 0 }; bool any = false, bad = false;
        ck_b2_writes(body->kids[i], &b, tab, nt, &v, &any, &bad);
        if (any) return false;
    }
    const low_cst_t *S = body->kids[L];
    proven_u8str_view_t v = { 0 }; bool any = false, bad = false;
    ck_b2_writes(S, &b, tab, nt, &v, &any, &bad);
    if (!any) return true;                                             // ⓐ
    if (bad) return false;
    if (!v.size) return true;                                          // 위임만 있는 문장
    if (!ck_b2_reads_indexed(S, &b, v)) return false;                  // ⓑ 같은 색인 하나
    // 반복이면 그 몸의 윗문장 차례에서 r 읽기가 w 쓰기 뒤에 오지 않는다
    const low_cst_t *blk = NULL;
    if (S->kind == LOW_CST_FORM) for (proven_size_t i = 0; i < S->nkids; i++) if (S->kids[i]->kind == LOW_CST_BLOCK) blk = S->kids[i];
    if (blk) {
        bool wrote = false;
        for (proven_size_t i = 0; i < blk->nkids; i++) {
            proven_u8str_view_t v2 = { 0 }; bool a2 = false, b2 = false;
            ck_b2_writes(blk->kids[i], &b, tab, nt, &v2, &a2, &b2);
            bool rd = ck_b2_reads_r(blk->kids[i], &b);
            if (wrote && rd) return false;
            if (a2) wrote = true;
        }
    }
    return true;
}
static void ck_inplace_body_check(low_check_result_t *out, const low_cst_t *form, const low_cst_t *body,
                                  const low_opinfo_t *tab, proven_size_t nt) {
    if (!form || !body || body->kind != LOW_CST_BLOCK) return;
    for (proven_size_t i = 0; i + 2 < form->nkids; i++) {
        if (form->kids[i]->kind == LOW_CST_BLOCK) break;
        if (!(ck_atom(form->kids[i]) && veq(form->kids[i]->tok.lex, "inplace") && ck_atom(form->kids[i + 1]) && ck_atom(form->kids[i + 2])))
            continue;
        bool full = false;
        if (ck_b2_pair_ok(body, form->kids[i + 1]->tok.lex, form->kids[i + 2]->tok.lex, tab, nt, &full)) continue;
        if (full) { emit(out, "E-IR-LIMIT", "too many view aliases for the `inplace` body check's table — refused rather than "
                         "checked partly (RFC-0116 B2)", form->kids[i]->tok.line); continue; }
        static char pbuf[64][460]; static unsigned pbn;
        const char *msg = "this op declares `inplace`, but its body is not one of the two shapes the processor can show "
                          "right on the SAME range: ⓐ every read of the read input comes before any write to the written "
                          "input, or ⓑ in the last statement that reads it, reads and writes are element by element with ONE "
                          "index and each read comes before the write. Reshape the body, or drop the declaration (RFC-0116 B2)";
        if (pbn < 64) {
            proven_u8str_view_t w = form->kids[i + 1]->tok.lex, r = form->kids[i + 2]->tok.lex;
            snprintf(pbuf[pbn], sizeof pbuf[0],
                     "`inplace %.*s %.*s` is declared, but the body is not one of the two shapes the processor can show "
                     "right on the SAME range: ⓐ every read of `%.*s` before any write to `%.*s`, or ⓑ in the last statement "
                     "that reads `%.*s`, element-by-element reads and writes with ONE index, each read before the write. "
                     "Reshape the body, or drop the declaration (RFC-0116 B2)",
                     (int)w.size, (const char *)w.ptr, (int)r.size, (const char *)r.ptr, (int)r.size, (const char *)r.ptr,
                     (int)w.size, (const char *)w.ptr, (int)r.size, (const char *)r.ptr);
            msg = pbuf[pbn++];
        }
        emit(out, "E-INPLACE-UNPROVEN", msg, form->kids[i]->tok.line);
    }
}

static void ck_excl_args_walk(low_check_result_t *out, const low_cst_t *nd,
                              const low_opinfo_t *tab, proven_size_t nt) {
    static ck_sctx_t c;                       // op 하나씩 — 다시 부를 때 비운다
    c.n = 0; c.nreset = 0; c.full = false;
    ck_sk_collect(&c, nd, true);              // ① 통째로 다시 대입되는 이름
    ck_sk_collect(&c, nd, false);             // ② 별칭(이름·subslice)
    if (c.full) {                             // ★ 별칭 표가 차면 자르지 않고 거절한다(check-limits)
        emit(out, "E-IR-LIMIT",
             "this op declares more slice aliases (or whole-name reassignments) than the exclusivity "
             "checker's table holds. Rather than miss an alias and let two writers through, this is "
             "refused — split the op",
             nd && nd->kind != LOW_CST_ATOM && nd->nkids && ck_atom(nd->kids[0]) ? nd->kids[0]->tok.line : 0);
        return;
    }
    ck_excl_args_walk2(out, nd, tab, nt, &c);
}

// ★★★★★ **권한은 건네받는 것이지 지어내는 것이 아니다** (RFC-0030 D2 · 결함 노트 #49, 2026-09-16).
//   `input k cap io .` 을 받는 op 을 `say 0` 으로 부르면 — 권한 자리에 **수 리터럴** — `--check` 가
//   통과했고 VM·네이티브 모두 출력을 냈다. 권한을 하나도 안 받은 `main` 이 그렇게 바깥에 닿았다.
//   자리표 `0` 은 **도구의 입구**(`--run`)에서만 뜻이 있다 — 프로그램 안에서는 아니다.
static void ck_capforge_walk(low_check_result_t *out, const low_cst_t *nd,
                             const low_opinfo_t *tab, proven_size_t nt) {
    if (!nd) return;
    if (nd->kind == LOW_CST_FORM && nd->nkids >= 2 && nd->kids[0]->kind == LOW_CST_ATOM &&
        nd->kids[0]->tok.kw == LOW_KW_NONE) {
        proven_u8str_view_t callee = nd->kids[0]->tok.lex;
        proven_u8str_view_t qmod = nd->kids[0]->qual_mod;
        for (proven_size_t i = 0; i < nt; i++) {
            if (!proven_u8str_view_eq(tab[i].name, callee)) continue;
            if (qmod.size && !proven_u8str_view_eq(tab[i].mod, qmod)) continue;
            if (!tab[i].form) break;
            low_op_header_t ch = low_op_header(tab[i].form);
            for (proven_size_t q = 0; q < ch.np && q + 1 < nd->nkids; q++) {
                proven_size_t cw = ch.p[q].core;
                if (cw >= tab[i].form->nkids || !ck_atom(tab[i].form->kids[cw]) ||
                    !veq(tab[i].form->kids[cw]->tok.lex, "cap")) continue;
                const low_cst_t *arg = nd->kids[q + 1];
                while (arg && arg->kind == LOW_CST_GROUP && arg->nkids == 1) arg = arg->kids[0];
                if (!arg || arg->kind != LOW_CST_ATOM) continue;
                bool literal = arg->tok.kind == LOW_TOK_NUMBER || arg->tok.kind == LOW_TOK_STRING ||
                               arg->tok.kind == LOW_TOK_HEREDOC ||
                               arg->tok.kw == LOW_KW_TRUE || arg->tok.kw == LOW_KW_FALSE;
                if (literal)
                    emit(out, "E-CAP-FORGE",
                         "a LITERAL was passed where a capability is taken. A capability is handed "
                         "over, never conjured: it can only be a name you were given (an `input … "
                         "cap …`) or an actor's capability field (RFC-0030 D2). Passing a number "
                         "here would let an op that received NO right reach the outside, and then "
                         "the entry point no longer tells what the program can touch. The "
                         "placeholder `0` means something only at the tool's door (`--run`)",
                         arg->tok.line);
            }
            break;
        }
    }
    for (proven_size_t i = 0; i < nd->nkids; i++) ck_capforge_walk(out, nd->kids[i], tab, nt);
}

// 이 op 폼이 어떤 actor 블록 **안에** 있는가 (핸들러면 상태 칸을 맨 이름으로 읽는 것이 정상이다).
static bool ck_form_is_handler_of(const low_parse_result_t *pr, const low_cst_t *opform) {
    for (proven_size_t i = 0; i < pr->nforms; i++) {
        const low_cst_t *f = pr->forms[i];
        if (!(f->kind == LOW_CST_FORM && f->nkids >= 2 && ck_atom(f->kids[0]) &&
              f->kids[0]->tok.kw == LOW_KW_ACTOR)) continue;
        for (proven_size_t b = 0; b < f->nkids; b++) {
            const low_cst_t *blk = f->kids[b];
            if (!blk || blk->kind != LOW_CST_BLOCK) continue;
            for (proven_size_t q = 0; q < blk->nkids; q++) if (blk->kids[q] == opform) return true;
        }
    }
    return false;
}
static bool ck_is_actor_type(const low_parse_result_t *pr, proven_u8str_view_t nm) {
    for (proven_size_t i = 0; i < pr->nforms; i++) {
        const low_cst_t *f = pr->forms[i];
        if (f->kind == LOW_CST_FORM && f->nkids >= 2 && ck_atom(f->kids[0]) &&
            f->kids[0]->tok.kw == LOW_KW_ACTOR && ck_atom(f->kids[1]) &&
            proven_u8str_view_eq(f->kids[1]->tok.lex, nm)) return true;
    }
    return false;
}
static void ck_collect_actor_locals(const low_cst_t *nd, const low_parse_result_t *pr,
                                    proven_u8str_view_t *names, proven_size_t *n, proven_size_t cap) {
    if (!nd) return;
    if (nd->kind == LOW_CST_FORM && nd->nkids >= 4 && ck_atom(nd->kids[0]) &&
        (nd->kids[0]->tok.kw == LOW_KW_LET || nd->kids[0]->tok.kw == LOW_KW_VAR) &&
        ck_atom(nd->kids[1]) && ck_atom(nd->kids[2]) &&
        ck_is_actor_type(pr, nd->kids[2]->tok.lex) && *n < cap)
        names[(*n)++] = nd->kids[1]->tok.lex;
    for (proven_size_t i = 0; i < nd->nkids; i++) ck_collect_actor_locals(nd->kids[i], pr, names, n, cap);
}
// ★★★★★ **액터의 상태는 액터 안에만 있다** (정본 §10.2(1) · 결함 노트 #61, 2026-09-16).
static void ck_actorfield_walk(low_check_result_t *out, const low_cst_t *nd,
                               const proven_u8str_view_t *names, proven_size_t n) {
    if (!nd) return;
    if (nd->kind == LOW_CST_FORM && nd->nkids >= 3 && ck_atom(nd->kids[0]) &&
        veq(nd->kids[0]->tok.lex, "field") && ck_atom(nd->kids[1])) {
        for (proven_size_t i = 0; i < n; i++)
            if (proven_u8str_view_eq(names[i], nd->kids[1]->tok.lex)) {
                emit(out, "E-ACTOR-FIELD",
                     "this reads a STATE FIELD of an actor from outside it. An actor's state lives "
                     "inside the actor and nowhere else (§10.2): the only door is a message "
                     "(`send a <op> …`). If the state were readable from outside, the isolation that "
                     "makes actors safe without locks — one message at a time — would not hold, and "
                     "the reader would see a value between two messages. Add an op to the actor that "
                     "returns what you need",
                     nd->kids[1]->tok.line);
                break;
            }
    }
    for (proven_size_t i = 0; i < nd->nkids; i++) ck_actorfield_walk(out, nd->kids[i], names, n);
}
// ★★★★ **빌림은 상태 칸에 살 수 없다** (정본 §8.4.1 · 결함 노트 #74).
static void ck_actor_state_refs(low_check_result_t *out, const low_cst_t *actor_form) {
    for (proven_size_t i = 0; i < actor_form->nkids; i++) {
        const low_cst_t *blk = actor_form->kids[i];
        if (!blk || blk->kind != LOW_CST_BLOCK) continue;
        for (proven_size_t j = 0; j < blk->nkids; j++) {
            const low_cst_t *st = blk->kids[j];
            if (!st || st->kind != LOW_CST_FORM || st->nkids < 2 || !ck_atom(st->kids[0])) continue;
            if (st->kids[0]->tok.kw != LOW_KW_STATE) continue;
            for (proven_size_t b = 0; b < st->nkids; b++) {
                const low_cst_t *sb = st->kids[b];
                if (!sb || sb->kind != LOW_CST_BLOCK) continue;
                for (proven_size_t q = 0; q < sb->nkids; q++) {
                    const low_cst_t *fld = sb->kids[q];
                    if (!fld || fld->kind != LOW_CST_FORM) continue;
                    for (proven_size_t z = 1; z < fld->nkids; z++)
                        if (ck_atom(fld->kids[z]) &&
                            (veq(fld->kids[z]->tok.lex, "ref") || veq(fld->kids[z]->tok.lex, "mut_ref"))) {
                            emit(out, "E-ACTOR-STATE-REF",
                                 "an actor STATE field may not be a borrow (`ref` / `mut_ref`). A "
                                 "borrow may not outlive what it borrows (§8.4.1), and a state field "
                                 "lives as long as the actor — there is nowhere here to say what it "
                                 "borrows. Keep a VALUE in state (copy it in), or keep a slice the "
                                 "actor was handed and owns for its lifetime",
                                 fld->line);
                            break;
                        }
                }
            }
        }
    }
}
// ★★★★★ **나갈 때 다시 읽히는 상태 칸을 오류 조건에 쓰지 않는다** (결함 노트 #62, 2026-09-16).
//
//   `errors insufficient gt amount balance .` — 조건이 액터의 상태 칸 `balance` 를 읽는다.
//   성공 경로가 그 칸을 줄이면, **나갈 때 다시 읽힌 조건**이 참이 되어 «조건이 참인데 그 오류를
//   내지 않았다» 가 된다. 그런데 VM 은 그것을 계약 위반(`E-VM-CONTRACT`)이 아니라 **컴파일러
//   결함**(`E-VM-ANALYSIS`)으로 알리고, **네이티브는 아예 검사하지 않는다** — 두 뒤끝이 갈렸다.
//   정본은 `errors` 조건을 들어올 때 값으로 읽는지 나갈 때 값으로 읽는지 적지 않았다(RFC-0115 §8).
//   ⇒ 뜻이 정해질 때까지 **그 모양을 거절한다.** 검사할 수 없는 선언을 안전의 근거로 삼지 않는다.
// (담은 actor 의 BLOCK 은 `op_actor[]` 표가 이미 들고 있다 — 따로 찾지 않는다.)
// 조건 나무가 상태 칸 이름을 읽는가 — **되돌림으로 훑는다**(고정 표는 깊은 조건을 조용히 자른다).
// ★ 조건 나무가 **바뀔 수 있는 이름**을 읽는가. 바뀔 수 있는 것은 셋이다 —
//   액터 상태 칸 · 모듈 `var` · `mut` 파라미터. 그 밖의 이름은 들어올 때와 나갈 때가 같다.
typedef struct { proven_u8str_view_t n[64]; proven_size_t nn; } ck_mutset_t;
static bool ck_mutset_has(const ck_mutset_t *ms, proven_u8str_view_t v) {
    for (proven_size_t i = 0; i < ms->nn; i++) if (proven_u8str_view_eq(ms->n[i], v)) return true;
    return false;
}
static void ck_mutset_add(ck_mutset_t *ms, proven_u8str_view_t v) {
    if (ms->nn < 64 && v.size && !ck_mutset_has(ms, v)) ms->n[ms->nn++] = v;
}
static bool ck_cond_reads_state(const low_cst_t *nd, const low_cst_t *actor_blk,
                                const ck_mutset_t *ms) {
    if (!nd) return false;
    if (ck_atom(nd) && nd->tok.kind == LOW_TOK_IDENT &&
        (ck_name_is_state_field(actor_blk, nd->tok.lex) || ck_mutset_has(ms, nd->tok.lex)))
        return true;
    for (proven_size_t i = 0; i < nd->nkids; i++)
        if (ck_cond_reads_state(nd->kids[i], actor_blk, ms)) return true;
    return false;
}
static void ck_errors_on_state(low_check_result_t *out, const low_cst_t *f,
                               const low_cst_t *actor_blk, const ck_mutset_t *modvars) {
    ck_mutset_t ms = modvars ? *modvars : (ck_mutset_t){ 0 };
    {   // ★ `mut` 파라미터도 바뀔 수 있다 — 머리가 이미 그것을 밝히고 있다.
        low_op_header_t h = low_op_header(f);
        for (proven_size_t q = 0; q < h.np; q++) if (h.p[q].is_mut) ck_mutset_add(&ms, h.p[q].name);
    }
    for (proven_size_t j = 2; j + 1 < f->nkids; j++) {
        if (!ck_atom(f->kids[j]) || !veq(f->kids[j]->tok.lex, "errors")) continue;
        for (proven_size_t e = j + 2; e < f->nkids; e++) {          // j+1 은 오류 갈래 이름
            const low_cst_t *n2 = f->kids[e];
            if (!n2 || n2->kind == LOW_CST_BLOCK) break;              // ★ 몸이 시작되면 절은 끝났다
            if (ck_atom(n2) && ck_clause_word(n2->tok.lex)) break;   // 다음 절
            if (ck_cond_reads_state(n2, actor_blk, &ms)) {
                emit(out, "E-ERRORS-STATE",
                     "the condition of this `errors` clause names something whose value can DIFFER "
                     "between entry and exit — an actor state field, a module `var`, or a `mut` "
                     "parameter. An `errors` condition is read on the values the op was ENTERED "
                     "with (canon §6.4.2): the clause says what the CALLER got wrong, and what the "
                     "caller handed over is all it can be blamed for. A name the body may change "
                     "cannot carry that meaning — read on exit it accuses the op of owing an error "
                     "it never owed. Write the condition over the inputs that do not change (and "
                     "module constants), and guard on the changing thing inside the body",
                     f->line);
                return;
            }
        }
    }
}
// ═══ 결함 노트 #30·#33·#66 — **흐름의 구멍 셋** (2026-09-16) ═══

// ① #30 — `output void` 인 op 이 값을 돌려준다.
static void ck_void_return(low_check_result_t *out, const low_cst_t *nd, const low_cst_t *f,
                           const low_op_header_t *h) {
    if (!nd) return;
    if (nd->kind == LOW_CST_FORM && nd->nkids >= 2 && ck_atom(nd->kids[0]) &&
        nd->kids[0]->tok.kw == LOW_KW_RETURN) {
        const low_cst_t *v = nd->kids[1];
        bool has_value = v && !(ck_atom(v) && v->tok.kw == LOW_KW_ELSE);
        if (has_value && h->out_s && h->out_s < f->nkids && ck_atom(f->kids[h->out_s]) &&
            veq(f->kids[h->out_s]->tok.lex, "void"))
            emit(out, "E-TYPE-RETURN",
                 "this op declares `output void .` and yet returns a VALUE. A void op returns with "
                 "`return .` alone (§6.5.5(2)) — the value written here goes nowhere, and the caller "
                 "has no place to put it. Declare the type you meant to return, or drop the value",
                 nd->kids[0]->tok.line);
    }
    for (proven_size_t i = 0; i < nd->nkids; i++) ck_void_return(out, nd->kids[i], f, h);
}

// ② #66 — 블록 **안**의 `else`(C 식). 앞 블록을 `end else do` 로 닫아야 한다.
static void ck_inner_else(low_check_result_t *out, const low_cst_t *blk) {
    if (!blk) return;
    if (blk->kind == LOW_CST_BLOCK)
        for (proven_size_t i = 0; i < blk->nkids; i++) {
            const low_cst_t *st = blk->kids[i];
            if (st && st->kind == LOW_CST_FORM && st->nkids >= 1 && ck_atom(st->kids[0]) &&
                st->kids[0]->tok.kw == LOW_KW_ELSE)
                emit(out, "E-STMT-ELSE",
                     "`else` sits INSIDE the block, the way C writes it. Here a block is closed "
                     "before the other arm opens: `if <cond> . do … end else do … end`. Written "
                     "this way the arm used to be accepted by every static check and then dropped at "
                     "lowering — the VM stopped with an unsupported body and the native build "
                     "silently left the op out",
                     st->kids[0]->tok.line);
        }
    for (proven_size_t i = 0; i < blk->nkids; i++) ck_inner_else(out, blk->kids[i]);
}

// ③ #33 — 블록 **안**에서 지은 이름을 블록 **밖**에서 읽는다(안 들어간 길에서는 조용히 0 이었다).
static void ck_block_names(const low_cst_t *blk, proven_u8str_view_t *names, proven_size_t *n,
                           proven_size_t cap) {
    if (!blk) return;
    if (blk->kind == LOW_CST_FORM && blk->nkids >= 2 && ck_atom(blk->kids[0]) &&
        (blk->kids[0]->tok.kw == LOW_KW_LET || blk->kids[0]->tok.kw == LOW_KW_VAR) &&
        ck_atom(blk->kids[1]) && *n < cap)
        names[(*n)++] = blk->kids[1]->tok.lex;
    for (proven_size_t i = 0; i < blk->nkids; i++) ck_block_names(blk->kids[i], names, n, cap);
}
// 이 나무 **어디에서든** 그 이름을 새로 짓는가(let/var) — 그러면 그것은 다른 이름이다.
static bool ck_declares_name(const low_cst_t *nd, proven_u8str_view_t nm) {
    if (!nd) return false;
    if (nd->kind == LOW_CST_FORM && nd->nkids >= 2 && ck_atom(nd->kids[0]) &&
        (nd->kids[0]->tok.kw == LOW_KW_LET || nd->kids[0]->tok.kw == LOW_KW_VAR) &&
        ck_atom(nd->kids[1]) && proven_u8str_view_eq(nd->kids[1]->tok.lex, nm)) return true;
    for (proven_size_t i = 0; i < nd->nkids; i++)
        if (ck_declares_name(nd->kids[i], nm)) return true;
    return false;
}
static bool ck_uses_name(const low_cst_t *nd, proven_u8str_view_t nm) {
    if (!nd) return false;
    if (ck_atom(nd) && nd->tok.kind == LOW_TOK_IDENT && nd->tok.kw == LOW_KW_NONE &&
        proven_u8str_view_eq(nd->tok.lex, nm)) return true;
    for (proven_size_t i = 0; i < nd->nkids; i++) if (ck_uses_name(nd->kids[i], nm)) return true;
    return false;
}
static void ck_scope_escape(low_check_result_t *out, const low_cst_t *body) {
    if (!body || body->kind != LOW_CST_BLOCK) return;
    for (proven_size_t i = 0; i < body->nkids; i++) {
        const low_cst_t *st = body->kids[i];
        if (!st || st->kind != LOW_CST_FORM || st->nkids < 1 || !ck_atom(st->kids[0])) continue;
        low_kw_t kw = st->kids[0]->tok.kw;
        if (kw != LOW_KW_IF && kw != LOW_KW_WHILE && kw != LOW_KW_FOR) continue;
        proven_u8str_view_t inner[64]; proven_size_t ni = 0;
        for (proven_size_t b = 0; b < st->nkids; b++)
            if (st->kids[b] && st->kids[b]->kind == LOW_CST_BLOCK)
                ck_block_names(st->kids[b], inner, &ni, 64);
        for (proven_size_t q = 0; q < ni; q++)
            for (proven_size_t j = i + 1; j < body->nkids; j++) {
                const low_cst_t *later = body->kids[j];
                // ★ 뒤에서 **다시 선언**하면 그것은 새 이름이다 — 그 자리부터는 볼 것이 없다
                //   (표준 라이브러리가 같은 이름을 블록 안팎에서 따로 짓는다: lib/sort.low 의 `ti`).
                if (ck_declares_name(later, inner[q])) break;
                if (ck_uses_name(later, inner[q])) {
                    emit(out, "E-NAME-SCOPE",
                         "this name was declared INSIDE a block and is read outside it. A block is "
                         "where a name lives (§6.5.1): on the path that did not enter the block the "
                         "name never existed, and the tool used to answer 0 there — a value that "
                         "appears nowhere in the source. Declare it before the block (`var … be 0 .`) "
                         "and set it inside",
                         body->kids[j]->line);
                    q = ni; break;
                }
            }
    }
    for (proven_size_t i = 0; i < body->nkids; i++)
        if (body->kids[i]) for (proven_size_t b = 0; b < body->kids[i]->nkids; b++)
            ck_scope_escape(out, body->kids[i]->kids[b]);
}

// ═══ 결함 노트 #20·#28·#35 — **오류 코드를 찍고도 초록이던 셋** (2026-09-16) ═══
//
//   셋 다 `E-IR-UNSUP`("도구가 아직 못 한다")로 나왔고, 그 갈래는 `--check` 를 빨갛게 하지
//   않는다 — 옳은 규칙이다. 틀린 것은 **분류**였다: 이 셋은 «아직 못 한다» 가 아니라
//   «프로그램이 틀렸다» 이거나 «없는 낱말이다». 그래서 각자의 이름으로 거절한다.
static void ck_slashslash_walk(low_check_result_t *out, const low_cst_t *nd) {
    if (!nd) return;
    // ★ 렉서는 `//` 를 낱말 `/` **둘**로 쪼갠다. 그 **연속된 둘**만 본다 — `expr` 섬 안의 나눗셈
    //   (`expr a / b`)과 문자열 `"/"` 는 정당하다(실측으로 둘 다 물렸다).
    bool slashslash = false;
    for (proven_size_t i = 0; i + 1 < nd->nkids; i++) {
        const low_cst_t *a = nd->kids[i], *b = nd->kids[i + 1];
        if (ck_atom(a) && ck_atom(b) && a->tok.kind != LOW_TOK_STRING && b->tok.kind != LOW_TOK_STRING &&
            a->tok.lex.size == 1 && a->tok.lex.ptr[0] == (proven_byte_t)'/' &&
            b->tok.lex.size == 1 && b->tok.lex.ptr[0] == (proven_byte_t)'/') { slashslash = true; break; }
    }
    if (slashslash)
        emit(out, "E-VOCAB-REMOVED",
             "`//` is not a comment here — this language has never had it. A comment starts with "
             "`rem` (to the end of the line) or `note <tag>` … `<tag>` (several lines). Until today "
             "`//` slipped through to lowering and was reported as an unsupported FEATURE, which "
             "sent the reader looking for a missing capability instead of a wrong spelling",
             nd->line);
    for (proven_size_t i = 0; i < nd->nkids; i++) ck_slashslash_walk(out, nd->kids[i]);
}
static void ck_loopword_walk(low_check_result_t *out, const low_cst_t *nd, bool in_loop) {
    if (!nd) return;
    bool opens_loop = in_loop;
    if (nd->kind == LOW_CST_FORM && nd->nkids >= 1 && ck_atom(nd->kids[0]) &&
        (nd->kids[0]->tok.kw == LOW_KW_WHILE || nd->kids[0]->tok.kw == LOW_KW_FOR))
        opens_loop = true;
    if (nd->kind == LOW_CST_FORM && nd->nkids >= 1 && ck_atom(nd->kids[0]) &&
        (nd->kids[0]->tok.kw == LOW_KW_BREAK || nd->kids[0]->tok.kw == LOW_KW_CONTINUE) && !in_loop)
        emit(out, "E-LOOP-OUTSIDE",
             "`break` / `continue` name a loop to leave or to continue, and there is no loop here. "
             "This used to reach lowering and be reported as an unsupported feature — the tool said "
             "\"not built\" about a program that simply has no loop to break out of",
             nd->kids[0]->tok.line);
    for (proven_size_t i = 0; i < nd->nkids; i++) ck_loopword_walk(out, nd->kids[i], opens_loop);
}
static void ck_fieldborrow_walk(low_check_result_t *out, const low_cst_t *nd) {
    if (!nd) return;
    if (nd->kind == LOW_CST_FORM && nd->nkids >= 2 && ck_atom(nd->kids[0]) &&
        (veq(nd->kids[0]->tok.lex, "ref") || veq(nd->kids[0]->tok.lex, "mut_ref"))) {
        const low_cst_t *t = nd->kids[1];
        while (t && t->kind == LOW_CST_GROUP && t->nkids == 1) t = t->kids[0];
        if (t && t->kind == LOW_CST_FORM && t->nkids >= 2 && ck_atom(t->kids[0]) &&
            veq(t->kids[0]->tok.lex, "field"))
            emit(out, "E-BORROW-FIELD",
                 "a borrow of a FIELD is not built. A borrow carries a lifetime, and a field's "
                 "lifetime is the whole value's — there is no way to say that yet, so the tool "
                 "refuses instead of lowering something it cannot check (it used to report this as "
                 "an unsupported feature AND still print `check: ok`). Copy the field into a local "
                 "and borrow that, or pass the whole value as `mut`",
                 nd->kids[0]->tok.line);
    }
    for (proven_size_t i = 0; i < nd->nkids; i++) ck_fieldborrow_walk(out, nd->kids[i]);
}

// ═══ 결함 노트 #31·#39·#44·#45·#58 — **타입 검사를 지나 실행에서 멈추던 다섯** (2026-09-16) ═══
//
//   다섯 모양이 `--check` 를 지나 실행 중 `E-VM-TYPE` 으로 멈췄다. 실행까지 갈 이유가 없다:
//   타입은 머리에 적혀 있고, 그 머리를 읽으면 셋 다 번역에서 말할 수 있다.

// 이름이 이 op 의 파라미터라면 그 **알맹이 타입 낱말**을 돌려준다(없으면 빈 것).
static proven_u8str_view_t ck_param_core_word(const low_cst_t *f, const low_op_header_t *h,
                                              proven_u8str_view_t nm) {
    for (proven_size_t q = 0; q < h->np; q++)
        if (proven_u8str_view_eq(h->p[q].name, nm)) {
            proven_size_t cw = h->p[q].core;
            if (cw < f->nkids && ck_atom(f->kids[cw])) return f->kids[cw]->tok.lex;
            break;
        }
    return (proven_u8str_view_t){ 0 };
}
static bool ck_is_arith_word(proven_u8str_view_t w) {
    return veq(w, "add") || veq(w, "sub") || veq(w, "mul") || veq(w, "div") || veq(w, "mod") ||
           veq(w, "lt") || veq(w, "le") || veq(w, "gt") || veq(w, "ge") ||
           veq(w, "eq") || veq(w, "ne");
}
// 지역 이름의 **선언된 타입 낱말**(`let p be point …` → `point`). 없으면 빈 것.
static proven_u8str_view_t ck_local_type_word(const low_cst_t *body, proven_u8str_view_t nm) {
    if (!body) return (proven_u8str_view_t){ 0 };
    if (body->kind == LOW_CST_FORM && body->nkids >= 4 && ck_atom(body->kids[0]) &&
        (body->kids[0]->tok.kw == LOW_KW_LET || body->kids[0]->tok.kw == LOW_KW_VAR) &&
        ck_atom(body->kids[1]) && proven_u8str_view_eq(body->kids[1]->tok.lex, nm) &&
        ck_atom(body->kids[2]) && body->kids[2]->tok.kw != LOW_KW_BE)
        return body->kids[2]->tok.lex;
    for (proven_size_t i = 0; i < body->nkids; i++) {
        proven_u8str_view_t r = ck_local_type_word(body->kids[i], nm);
        if (r.size) return r;
    }
    return (proven_u8str_view_t){ 0 };
}
static bool ck_name_is_struct(const low_parse_result_t *pr, proven_u8str_view_t ty) {
    if (!ty.size) return false;
    for (proven_size_t i = 0; i < pr->nforms; i++) {
        const low_cst_t *g = pr->forms[i];
        if (g->kind == LOW_CST_FORM && g->nkids >= 2 && ck_atom(g->kids[0]) &&
            g->kids[0]->tok.kw == LOW_KW_STRUCT && ck_atom(g->kids[1]) &&
            proven_u8str_view_eq(g->kids[1]->tok.lex, ty)) return true;
    }
    return false;
}
// ★★ **자리의 타입에 안 들어가는 리터럴과의 비교는 언제나 같은 답이다** (결함 노트 #32, 2026-09-16).
//   `u8` 지역과 `lt i 256` 은 참일 수밖에 없다. 루프의 끝 조건을 그렇게 적으면 **끝나지 않는다**.
//   대입 자리의 리터럴은 `E-TYPE-WIDTH` 가 오래 전부터 물었는데 **비교 자리는 안 봤다**.
static bool ck_int_range(proven_u8str_view_t ty, proven_i64 *lo, proven_u64 *hi) {
    static const struct { const char *w; proven_i64 lo; proven_u64 hi; } R[] = {
        { "u8", 0, 255u }, { "u16", 0, 65535u }, { "u32", 0, 4294967295u },
        { "i8", -128, 127u }, { "i16", -32768, 32767u }, { "i32", -2147483648LL, 2147483647u },
    };
    for (size_t i = 0; i < sizeof R / sizeof R[0]; i++)
        if (veq(ty, R[i].w)) { *lo = R[i].lo; *hi = R[i].hi; return true; }
    return false;
}
// 십진·십육진 정수 리터럴만 읽는다 — 부동소수·밑줄·부호는 여기서 안 본다(보수적으로 건너뛴다).
static bool ck_uint_literal(const low_cst_t *a, proven_u64 *v) {
    if (!a || a->kind != LOW_CST_ATOM || a->tok.kind != LOW_TOK_NUMBER) return false;
    proven_u8str_view_t x = a->tok.lex;
    if (!x.size) return false;
    proven_u64 acc = 0; proven_size_t i = 0; int base = 10;
    if (x.size > 2 && x.ptr[0] == '0' && (x.ptr[1] == 'x' || x.ptr[1] == 'X')) { base = 16; i = 2; }
    for (; i < x.size; i++) {
        proven_u8 ch = x.ptr[i]; int d;
        if (ch == '_') continue;
        if (ch >= '0' && ch <= '9') d = ch - '0';
        else if (base == 16 && ch >= 'a' && ch <= 'f') d = ch - 'a' + 10;
        else if (base == 16 && ch >= 'A' && ch <= 'F') d = ch - 'A' + 10;
        else return false;                       // `.`·`e`·접미사 — 정수가 아니다
        if (acc > (~(proven_u64)0 - (proven_u64)d) / (proven_u64)base) return false;  // 넘침 — 안 본다
        acc = acc * (proven_u64)base + (proven_u64)d;
    }
    *v = acc; return true;
}
static void ck_cmpwidth_walk(low_check_result_t *out, const low_cst_t *nd, const low_cst_t *f,
                             const low_op_header_t *h, const low_cst_t *body) {
    if (!nd) return;
    if (nd->kind == LOW_CST_FORM && nd->nkids == 3 && ck_atom(nd->kids[0]) &&
        nd->kids[0]->tok.kw == LOW_KW_NONE) {
        proven_u8str_view_t w = nd->kids[0]->tok.lex;
        if (veq(w, "lt") || veq(w, "le") || veq(w, "gt") || veq(w, "ge") ||
            veq(w, "eq") || veq(w, "ne")) {
            for (int side = 0; side < 2; side++) {
                const low_cst_t *nm = nd->kids[1 + side], *lit = nd->kids[2 - side];
                if (!ck_atom(nm) || nm->tok.kind != LOW_TOK_IDENT || nm->tok.kw != LOW_KW_NONE) continue;
                proven_u64 v = 0;
                if (!ck_uint_literal(lit, &v)) continue;
                proven_u8str_view_t ty = ck_param_core_word(f, h, nm->tok.lex);
                if (!ty.size) ty = ck_local_type_word(body, nm->tok.lex);
                proven_i64 lo = 0; proven_u64 hi = 0;
                if (!ck_int_range(ty, &lo, &hi)) continue;
                if (v <= hi) continue;
                emit(out, "E-TYPE-WIDTH",
                     "this comparison holds a value that the other side's type cannot hold, so the "
                     "answer is the same for every input — the comparison decides nothing. A name "
                     "declared `u8` is at most 255, `u16` at most 65535, `i8` at most 127; a literal "
                     "beyond that is out of range exactly as it would be in an assignment, where the "
                     "tool has always refused it. Widen the name's type, or compare against a value "
                     "the type can reach. (This is how a loop written `while lt i 256 .` over a `u8` "
                     "counter never ends)",
                     nd->kids[0]->tok.line);
            }
        }
    }
    for (proven_size_t i = 0; i < nd->nkids; i++) ck_cmpwidth_walk(out, nd->kids[i], f, h, body);
}

// ★★ **fold 의 누산값은 첫 입력이고, 그것이 곧 결과다** (결함 노트 #60, 2026-09-16).
//   `fold 0 addu` 의 단계 op 머리를 `input x u8 . input acc u64 .` 로 거꾸로 적어도 통과했고,
//   400 이 넘는 누산값이 `u8` 매개변수로 들어갔다(`f([200,200,200]) = 600`). 차례를 대조할 자리가
//   없었던 것이 아니라 **아무도 안 봤다**: acc' = op(acc, x) 이므로 첫 입력의 타입과 출력 타입은
//   같아야 한다. 다르면 누산이 매 걸음 다른 자리에 들어간다.
static void ck_foldorder_walk(low_check_result_t *out, const low_cst_t *nd,
                              const low_opinfo_t *tab, proven_size_t nt) {
    if (!nd) return;
    if (nd->kind == LOW_CST_FORM && nd->nkids >= 3 && ck_atom(nd->kids[0]) &&
        (veq(nd->kids[0]->tok.lex, "fold") || veq(nd->kids[0]->tok.lex, "scan")) &&
        ck_atom(nd->kids[2]) && nd->kids[2]->tok.kw == LOW_KW_NONE) {
        proven_u8str_view_t opn = nd->kids[2]->tok.lex;
        for (proven_size_t i = 0; i < nt; i++) {
            if (!proven_u8str_view_eq(tab[i].name, opn) || !tab[i].form) continue;
            const low_cst_t *g = tab[i].form;
            low_op_header_t gh = low_op_header(g);
            if (gh.np < 1) break;
            proven_size_t cw = gh.p[0].core;
            if (!(cw < g->nkids && ck_atom(g->kids[cw]))) break;
            if (!(gh.out_s && gh.out_s < g->nkids && ck_atom(g->kids[gh.out_s]))) break;
            proven_u8str_view_t a = g->kids[cw]->tok.lex, o = g->kids[gh.out_s]->tok.lex;
            proven_i64 lo1 = 0, lo2 = 0; proven_u64 h1 = 0, h2 = 0;
            // 폭이 있는 정수 타입끼리만 따진다 — 이름만으로는 별칭을 갈라 볼 수 없다.
            if (!(ck_int_range(a, &lo1, &h1) || veq(a, "u64") || veq(a, "i64") || veq(a, "usize"))) break;
            if (!(ck_int_range(o, &lo2, &h2) || veq(o, "u64") || veq(o, "i64") || veq(o, "usize"))) break;
            if (proven_u8str_view_eq(a, o)) break;
            emit(out, "E-FOLD-ORDER",
                 "the accumulator of a `fold`/`scan` is the FIRST input of its op and also its "
                 "output, because each step computes `acc = op(acc, element)`. Here the first input "
                 "and the output are declared with different types, so the running total is handed "
                 "to a parameter that cannot hold it — a `u8` first input takes a total of 600 as "
                 "88 and the answer is silently wrong. Write the op as `input acc <out-type> . "
                 "input x <element-type> .`",
                 nd->kids[0]->tok.line);
            break;
        }
    }
    for (proven_size_t i = 0; i < nd->nkids; i++) ck_foldorder_walk(out, nd->kids[i], tab, nt);
}

// ★★ X-0061 (소유자 결정 ⓐ, 2026-09-25) — **판정 op 은 `bool` 을 낸다.** `filter`·`any`·`all`(과 내장 `filter <받는 자리> <op>
//   <원천>`)의 op 이 수를 내도 통과했고 0 아닌 값을 참으로 보았다 — 이 언어는 `bool` 을 수로 보지 않는다(`cast` 도 `bool`
//   을 거절한다). 수 타입(정수·부동소수)을 내는 op 을 거절한다 — `type` 별칭은 밑 타입까지 따라간다(X-0065).
// ★ X-0065 — `type flag u8 .` 같은 **별칭**을 밑 타입 이름까지 따라간다(몇 겹이든, 고리는 8 번에서 끊는다).
//   판정 op 이 별칭으로 수를 내도 E-PIPE-PRED 가 보도록. `newtype` 은 새 타입이라 따라가지 않는다.
static proven_u8str_view_t ck_alias_base(proven_u8str_view_t nm) {
    for (int hop = 0; g_ck_pr && hop < 8; hop++) {
        bool moved = false;
        for (proven_size_t i = 0; i < g_ck_pr->nforms; i++) {
            const low_cst_t *f = g_ck_pr->forms[i];
            if (f->kind != LOW_CST_FORM || f->nkids != 3 || !ck_atom(f->kids[0]) || f->kids[0]->tok.kw != LOW_KW_TYPE) continue;
            if (!(ck_atom(f->kids[1]) && ck_atom(f->kids[2]) && proven_u8str_view_eq(f->kids[1]->tok.lex, nm))) continue;
            nm = f->kids[2]->tok.lex; moved = true; break;
        }
        if (!moved) break;
    }
    return nm;
}

static void ck_predbool_walk(low_check_result_t *out, const low_cst_t *nd,
                             const low_opinfo_t *tab, proven_size_t nt) {
    if (!nd) return;
    const low_cst_t *opa = NULL;
    if (nd->kind == LOW_CST_FORM && ck_atom(nd->kids[0]) && nd->kids[0]->tok.kw == LOW_KW_NONE) {
        proven_u8str_view_t w = nd->kids[0]->tok.lex;
        if (nd->nkids == 2 && (veq(w, "filter") || veq(w, "any") || veq(w, "all"))) opa = nd->kids[1];
        else if (nd->nkids == 4 && veq(w, "filter")) opa = nd->kids[2];
    }
    if (opa && ck_atom(opa) && opa->tok.kw == LOW_KW_NONE) {
        for (proven_size_t i = 0; i < nt; i++) {
            if (!proven_u8str_view_eq(tab[i].name, opa->tok.lex) || !tab[i].form) continue;
            const low_cst_t *g = tab[i].form;
            low_op_header_t gh = low_op_header(g);
            if (!(gh.out_s && gh.out_s < g->nkids && ck_atom(g->kids[gh.out_s]))) break;
            proven_u8str_view_t o = ck_alias_base(g->kids[gh.out_s]->tok.lex);
            proven_i64 lo = 0; proven_u64 hi = 0;
            if (!(ck_int_range(o, &lo, &hi) || veq(o, "u64") || veq(o, "i64") || veq(o, "usize") ||
                  veq(o, "f32") || veq(o, "f64"))) break;
            emit(out, "E-PIPE-PRED",
                 "the op given to `filter`/`any`/`all` is a PREDICATE — it must answer `bool`. This one "
                 "answers a number, which used to be taken as true when it was not 0. A number is not a "
                 "truth value in this language (`cast` refuses `bool` both ways), so say the question: "
                 "`output bool .` and `return ne x 0 .`",
                 nd->kids[0]->tok.line);
            break;
        }
    }
    for (proven_size_t i = 0; i < nd->nkids; i++) ck_predbool_walk(out, nd->kids[i], tab, nt);
}

// ★ X-0072 ①(소유자 «추천대로», 2026-09-27) — **`expect` 는 시험의 단언이다.** op 의 몸에 적으면 받아 주고, 거짓이면
//   «this test failed» 라고 말했다 — 시험이 아닌데 시험 실패라고. 계약이 할 일이면 `requires`·`ensures`, 멈춤이면 `panic` 이다.
static void ck_expect_place_walk(low_check_result_t *out, const low_cst_t *nd) {
    if (!nd) return;
    if (nd->kind == LOW_CST_FORM && nd->nkids && ck_atom(nd->kids[0]) && nd->kids[0]->tok.kw == LOW_KW_EXPECT) {
        emit(out, "E-EXPECT-PLACE",
             "`expect` is the assertion of a `test` block, and this one is outside any test. In an op body "
             "it used to run and, when false, report that «the test failed» — there was no test. Say what you "
             "mean: a promise about inputs or results is `requires` / `ensures`; stopping on purpose is `panic`",
             nd->kids[0]->tok.line);
        return;
    }
    for (proven_size_t i = 0; i < nd->nkids; i++) ck_expect_place_walk(out, nd->kids[i]);
}

// ★★ X-0062 (소유자 결정 ⓒ, 2026-09-25) — **받는 자리가 모자란 것을 번역 시점에 알면 거절한다.** 모르면 실행 중 멈춘다
//   (쓰기의 경계 검사). 여기서 «안다» 는 좁게 잡는다: 원천과 받는 자리의 길이가 둘 다 머리의 계약(`requires eq (len x) N`,
//   `array <N> <T>` 입력도 그리 바뀐다)에 적혀 있고, 사이의
//   스테이지가 개수를 모르는 것(`filter`·`zip`)이 아닐 때다. `take`·`skip` 은 개수를 그대로 셈한다.
static proven_u64 ck_param_array_len(const low_cst_t *f, const low_op_header_t *h, const low_cst_t *nm) {
    // 길이는 머리의 계약 `requires eq (len <이름>) <N> .` 에 있다 — `array <N> <T>` 입력도 여기로 바뀌어 온다(low_using.c).
    (void)h;
    if (!nm || !ck_atom(nm)) return 0;
    for (proven_size_t i = 0; i + 3 < f->nkids; i++) {
        if (!(ck_atom(f->kids[i]) && veq(f->kids[i]->tok.lex, "requires"))) continue;
        const low_cst_t *op = f->kids[i + 1], *ln = f->kids[i + 2], *nv = f->kids[i + 3];
        if (!(ck_atom(op) && veq(op->tok.lex, "eq"))) continue;
        while (ln->kind == LOW_CST_GROUP && ln->nkids == 1) ln = ln->kids[0];   // `(len x)` 는 괄호 한 겹 안의 폼
        if (!(ln->kind == LOW_CST_FORM && ln->nkids == 2 && ck_atom(ln->kids[0]) && veq(ln->kids[0]->tok.lex, "len") &&
              ck_atom(ln->kids[1]) && proven_u8str_view_eq(ln->kids[1]->tok.lex, nm->tok.lex))) continue;
        proven_u64 n = 0;
        if (ck_uint_literal(nv, &n)) return n;
    }
    return 0;
}
// ★ X-0065 ② (2026-09-27, 소유자 «지금») — 길이를 번역 시점에 **확실히** 아는 자리를 넓힌다: 머리의 계약(위) ·
//   역슬래시도 접두사도 없는 문자열 리터럴(바이트 수 = 본문 길이) · 그런 리터럴에 묶인 `let` 이름(`let` 은 다시 묶이지
//   않는다). 이스케이프가 든 리터럴은 «모른다» 로 둔다 — 풀이는 한 곳(low_ir.c `ir_unescape`)에만 산다(X-0070 의 교훈).
//   모르는 것은 실행 중 «받는 자리가 찼다» 가 잡는다.
static proven_u64 ck_lit_len(const low_cst_t *a) {
    if (!a || !ck_atom(a) || a->tok.kind != LOW_TOK_STRING || a->tok.aux.size) return 0;
    for (proven_size_t i = 0; i < a->tok.lex.size; i++) if (a->tok.lex.ptr[i] == (proven_u8)'\\') return 0;
    return a->tok.lex.size;
}
static const low_cst_t *ck_let_value(const low_cst_t *nd, proven_u8str_view_t nm) {
    if (!nd) return NULL;
    if (nd->kind == LOW_CST_FORM && nd->nkids >= 4 && ck_atom(nd->kids[0]) && nd->kids[0]->tok.kw == LOW_KW_LET &&
        ck_atom(nd->kids[1]) && proven_u8str_view_eq(nd->kids[1]->tok.lex, nm)) {
        const low_cst_t *be = nd->kids[nd->nkids - 2];
        return (ck_atom(be) && veq(be->tok.lex, "be")) ? nd->kids[nd->nkids - 1] : NULL;
    }
    for (proven_size_t i = 0; i < nd->nkids; i++) { const low_cst_t *v = ck_let_value(nd->kids[i], nm); if (v) return v; }
    return NULL;
}
static proven_u64 ck_known_len(const low_cst_t *f, const low_op_header_t *h, const low_cst_t *nm) {
    proven_u64 n = ck_param_array_len(f, h, nm);
    if (n) return n;
    if ((n = ck_lit_len(nm))) return n;
    if (nm && ck_atom(nm) && nm->tok.kind == LOW_TOK_IDENT) return ck_lit_len(ck_let_value(h->body, nm->tok.lex));
    return 0;
}
static void ck_collectfull_emit(low_check_result_t *out, proven_u32 line, proven_u64 n, proven_u64 cap) {
    (void)n; (void)cap;
    emit(out, "E-COLLECT-FULL",
         "this puts more elements into a place than it can hold, and both lengths are known here (from "
         "`requires eq (len x) N` — an `array N T` input says the same — or from a plain string literal, "
         "directly or through a `let`). A full place used to drop the rest "
         "without a word; now an overflow never passes: known lengths are refused here, unknown ones stop the "
         "run at the first element that does not fit. Give a place at least as long as the flow, or `take` what fits",
         line);
}
static void ck_collectfull_walk(low_check_result_t *out, const low_cst_t *nd,
                                const low_cst_t *f, const low_op_header_t *h) {
    if (!nd) return;
    // `pipe xs do … end`(문장: FORM(pipe, xs, BLOCK)) 와 `return pipe xs do … end .`(값: …, ATOM pipe, FORM(xs, BLOCK)) 둘 다
    const low_cst_t *src = NULL, *blk = NULL;
    if (nd->kind == LOW_CST_FORM && nd->nkids >= 3 && ck_atom(nd->kids[0]) && veq(nd->kids[0]->tok.lex, "pipe") &&
        nd->kids[nd->nkids - 1]->kind == LOW_CST_BLOCK) { src = nd->kids[1]; blk = nd->kids[nd->nkids - 1]; }
    for (proven_size_t i = 0; !blk && nd->kind == LOW_CST_FORM && i + 1 < nd->nkids; i++) {
        const low_cst_t *nx = nd->kids[i + 1];
        if (ck_atom(nd->kids[i]) && veq(nd->kids[i]->tok.lex, "pipe") && nx->kind == LOW_CST_FORM && nx->nkids >= 2 &&
            nx->kids[nx->nkids - 1]->kind == LOW_CST_BLOCK) { src = nx->kids[0]; blk = nx->kids[nx->nkids - 1]; }
    }
    {
        if (blk) {
            proven_u64 n = ck_known_len(f, h, src);
            for (proven_size_t s = 0; n && s < blk->nkids; s++) {
                const low_cst_t *st = blk->kids[s];
                if (!(st->kind == LOW_CST_FORM && st->nkids && ck_atom(st->kids[0]))) { n = 0; break; }
                proven_u8str_view_t sw = st->kids[0]->tok.lex; proven_u64 k = 0;
                if (veq(sw, "filter") || veq(sw, "zip")) { n = 0; break; }
                if (veq(sw, "take") && st->nkids >= 2 && ck_uint_literal(st->kids[1], &k)) { if (k < n) n = k; continue; }
                if (veq(sw, "skip") && st->nkids >= 2 && ck_uint_literal(st->kids[1], &k)) { n = k < n ? n - k : 0; continue; }
                if (veq(sw, "take") || veq(sw, "skip")) { n = 0; break; }
                if (veq(sw, "collect") && st->nkids >= 3) {
                    proven_u64 cap = ck_known_len(f, h, st->kids[2]);
                    if (cap && n > cap) ck_collectfull_emit(out, st->kids[0]->tok.line, n, cap);
                    break;
                }
            }
        } else if (nd->kind == LOW_CST_FORM && nd->nkids == 4 && ck_atom(nd->kids[0]) && veq(nd->kids[0]->tok.lex, "map")) {
            proven_u64 n = ck_known_len(f, h, nd->kids[3]), cap = ck_known_len(f, h, nd->kids[1]);
            if (n && cap && n > cap) ck_collectfull_emit(out, nd->kids[0]->tok.line, n, cap);
        }
    }
    for (proven_size_t i = 0; i < nd->nkids; i++) ck_collectfull_walk(out, nd->kids[i], f, h);
}

static void ck_typeholes_walk(low_check_result_t *out, const low_cst_t *nd,
                              const low_cst_t *f, const low_op_header_t *h,
                              const low_parse_result_t *pr, const low_opinfo_t *tab,
                              proven_size_t nt) {
    if (!nd) return;
    if (nd->kind == LOW_CST_FORM && nd->nkids >= 2 && ck_atom(nd->kids[0]) &&
        nd->kids[0]->tok.kw == LOW_KW_NONE) {
        proven_u8str_view_t head = nd->kids[0]->tok.lex;
        // ① #45 — 빌림을 **수처럼** 쓴다: `add p p`(p 는 `ref u64`). `deref p` 가 빠졌다.
        // ② #39 — 구조체를 `eq`/`ne` 로 비교한다. 「같다」의 뜻은 타입마다 다르다.
        // ③ #31 — 부동소수와 **정수 리터럴**을 섞어 셈한다(`div x 2`).
        if (ck_is_arith_word(head)) {
            bool saw_float = false, saw_intlit = false;
            for (proven_size_t q = 1; q < nd->nkids; q++) {
                const low_cst_t *a = nd->kids[q];
                while (a && a->kind == LOW_CST_GROUP && a->nkids == 1) a = a->kids[0];
                if (!a || !ck_atom(a)) continue;
                if (a->tok.kind == LOW_TOK_NUMBER) {
                    // ★ X-0070 — 판정은 low_typecheck.c 한 곳(`low_num_is_float`). 여기 사본이 16진을 틀리게 갈랐다.
                    if (low_num_is_float(a->tok.lex)) saw_float = true; else saw_intlit = true;
                    continue;
                }
                if (a->tok.kind != LOW_TOK_IDENT || a->tok.kw != LOW_KW_NONE) continue;
                proven_u8str_view_t pw = ck_param_core_word(f, h, a->tok.lex);
                if (veq(pw, "ref") || veq(pw, "mut_ref")) {
                    emit(out, "E-TYPE-REFVAL",
                         "a BORROW is being used where a number is expected. `ref t` / `mut_ref t` "
                         "names a place, not the value in it — read it with `deref <name>` (and write "
                         "through it with `set <name> …`). It used to pass `--check` and stop at run "
                         "time with `E-VM-TYPE`, which blamed the arithmetic instead of the missing read",
                         a->tok.line);
                    return;
                }
                if (veq(pw, "f32") || veq(pw, "f64")) saw_float = true;
                proven_u8str_view_t lw = pw.size ? pw : ck_local_type_word(h->body, a->tok.lex);
                if (veq(lw, "f32") || veq(lw, "f64")) saw_float = true;
                if ((veq(head, "eq") || veq(head, "ne")) && ck_name_is_struct(pr, lw)) {
                    emit(out, "E-TYPE-KIND",
                         "`eq` / `ne` compare numbers and booleans, not STRUCTS. What \"equal\" means "
                         "for a struct differs by type — every field, or only the identifying one? — so "
                         "the language does not guess: write an op that says it (field by field). This "
                         "used to pass `--check` and stop at run time with `E-VM-TYPE`",
                         a->tok.line);
                    return;
                }
            }
            if (saw_float && saw_intlit) {
                emit(out, "E-TYPE-MIX",
                     "a floating-point value and an INTEGER literal are mixed in one operation. "
                     "Floating point and integers do not convert implicitly (§6.2.5), so write the "
                     "literal as a float (`2.0`) — or convert the other side. It used to pass "
                     "`--check` and stop at run time (`E-VM-TYPE: arithmetic needs ints`)",
                     nd->kids[0]->tok.line);
                return;
            }
        }
        // ④ #44 — `mut_ref`/`ref` 를 받는 자리에 **맨 값**을 넘긴다.
        for (proven_size_t i = 0; i < nt; i++) {
            if (!proven_u8str_view_eq(tab[i].name, head)) continue;
            if (!tab[i].form) break;
            low_op_header_t ch = low_op_header(tab[i].form);
            for (proven_size_t q = 0; q < ch.np && q + 1 < nd->nkids; q++) {
                proven_size_t cw = ch.p[q].core;
                if (cw >= tab[i].form->nkids || !ck_atom(tab[i].form->kids[cw])) continue;
                proven_u8str_view_t want = tab[i].form->kids[cw]->tok.lex;
                if (!veq(want, "ref") && !veq(want, "mut_ref")) continue;
                const low_cst_t *arg = nd->kids[q + 1];
                while (arg && arg->kind == LOW_CST_GROUP && arg->nkids == 1) arg = arg->kids[0];
                bool ok = false;
                if (arg && arg->kind == LOW_CST_FORM && arg->nkids >= 2 && ck_atom(arg->kids[0]) &&
                    (veq(arg->kids[0]->tok.lex, "ref") || veq(arg->kids[0]->tok.lex, "mut_ref")))
                    ok = true;
                if (arg && ck_atom(arg) && arg->tok.kind == LOW_TOK_IDENT) {
                    proven_u8str_view_t aw = ck_param_core_word(f, h, arg->tok.lex);
                    if (!aw.size) aw = ck_local_type_word(h->body, arg->tok.lex);
                    if (veq(aw, "ref") || veq(aw, "mut_ref")) ok = true;
                }
                if (!ok)
                    emit(out, "E-TYPE-ARG",
                         "this op takes a BORROW here (`ref` / `mut_ref`), and a plain value was "
                         "passed. Take the borrow at the call — `mut_ref <name>` — so the reader sees "
                         "where the callee may write. It used to pass `--check` and stop at run time "
                         "with `E-VM-TYPE: deref needs a reference`",
                         arg && ck_atom(arg) ? arg->tok.line : nd->kids[0]->tok.line);
            }
            break;
        }
    }
    for (proven_size_t i = 0; i < nd->nkids; i++)
        ck_typeholes_walk(out, nd->kids[i], f, h, pr, tab, nt);
}
// ⑥ #72 — `sum_neumaier`·`sum_seq` 는 **부동소수 합**이다(결함 노트 #71). 정수 출력에 그대로 돌려주면
//   `--check` 가 통과하고 결과가 `20.0` 으로 찍혔다 — 반환 타입 검사가 그 결과 타입을 몰랐다.
static void ck_sum_return(low_check_result_t *out, const low_cst_t *nd, const low_cst_t *f,
                          const low_op_header_t *h) {
    if (!nd) return;
    const low_cst_t *rv = NULL;
    if (nd->kind == LOW_CST_FORM && nd->nkids >= 2 && ck_atom(nd->kids[0]) &&
        nd->kids[0]->tok.kw == LOW_KW_RETURN) {
        rv = nd->kids[1];
        while (rv && rv->kind == LOW_CST_GROUP && rv->nkids == 1) rv = rv->kids[0];
        if (rv && rv->kind == LOW_CST_FORM && rv->nkids >= 1) rv = rv->kids[0];   // 감싼 폼의 머리
    }
    if (rv && ck_atom(rv) && (veq(rv->tok.lex, "sum_neumaier") || veq(rv->tok.lex, "sum_seq")) &&
        h->out_s && h->out_s < f->nkids && ck_atom(f->kids[h->out_s])) {
        proven_u8str_view_t ow = f->kids[h->out_s]->tok.lex;
        bool is_int_out = ow.size >= 2 &&
            (ow.ptr[0] == (proven_byte_t)'u' || ow.ptr[0] == (proven_byte_t)'i') &&
            ow.ptr[1] >= (proven_byte_t)'0' && ow.ptr[1] <= (proven_byte_t)'9';
        if (is_int_out)
            emit(out, "E-TYPE-RETURN",
                 "`sum_neumaier` / `sum_seq` add FLOATING-POINT values (a compensated and a running "
                 "sum), so their result is a float — returning it where an integer is declared used to "
                 "pass `--check` and then print `20.0` from an op whose head said `u64`. To add lanes of "
                 "an integer vector use `reduce_add`; to keep the float, declare the output as one",
                 rv->tok.line);
    }
    for (proven_size_t i = 0; i < nd->nkids; i++) ck_sum_return(out, nd->kids[i], f, h);
}
// ═══ 결함 노트 #51 — **실패가 조용히 사라지는 세 자리** (RFC-0115 §8-5, 2026-09-17) ═══
//
//   `result` 는 «실패가 값이다» 를 쓰는 타입이다. 그런데 그 값을 **버리는** 것이 아무 말 없이
//   통과하고 있었다: 문장으로 부르고 답을 안 받거나(`check_port 0 .`), `let r` 에 담고 한 번도
//   안 읽거나, `errors` 절을 적어 놓고 몸통이 그 오류를 **한 번도 내지 않거나**.
//   ⇒ 오류가 아니라 **알림**이다 — 실패를 정말 무시해도 되는 자리가 있고, 그 자리를 적는 문법
//     (`ignore …` 따위)이 아직 없다. 없는 문법을 지키라고 거절할 수는 없다.
// ★ 이 op 이 `result` 를 돌려주는가 — 표에서 찾아 머리의 `output` 첫 낱말을 본다.
static bool ck_op_returns_result(const low_opinfo_t *tab, proven_size_t nt,
                                 proven_u8str_view_t name) {
    for (proven_size_t i = 0; i < nt; i++) {
        if (!proven_u8str_view_eq(tab[i].name, name) || !tab[i].form) continue;
        low_op_header_t h = low_op_header(tab[i].form);
        if (!h.out_s || h.out_s >= tab[i].form->nkids) return false;
        const low_cst_t *o = tab[i].form->kids[h.out_s];
        return ck_atom(o) && veq(o->tok.lex, "result");
    }
    return false;
}
// ★ 이름이 나무에 몇 번 나오는가 — 묶은 자리 말고 **읽는 자리**가 있는지 세려는 것이다.
static proven_size_t ck_name_count(const low_cst_t *nd, proven_u8str_view_t nm) {
    if (!nd) return 0;
    proven_size_t n = (ck_atom(nd) && nd->tok.kind == LOW_TOK_IDENT &&
                       proven_u8str_view_eq(nd->tok.lex, nm)) ? 1 : 0;
    for (proven_size_t i = 0; i < nd->nkids; i++) n += ck_name_count(nd->kids[i], nm);
    return n;
}
static void ck_result_discard_walk(low_check_result_t *out, const low_cst_t *body,
                                   const low_cst_t *nd, const low_opinfo_t *tab, proven_size_t nt) {
    if (!nd) return;
    if (nd->kind == LOW_CST_BLOCK) {
        for (proven_size_t i = 0; i < nd->nkids; i++) {
            const low_cst_t *s = nd->kids[i];
            if (!s || s->kind != LOW_CST_FORM || !s->nkids || !ck_atom(s->kids[0])) continue;
            low_kw_t kw = s->kids[0]->tok.kw;
            // ① 문장으로 부르고 답을 안 받는다
            if (kw == LOW_KW_NONE && ck_op_returns_result(tab, nt, s->kids[0]->tok.lex))
                warn(out, "W-RESULT-DISCARD",
                     "this op returns a `result` — it says failure is a VALUE — and the value is "
                     "dropped here, so a failure leaves no trace at all. Bind it and look at it "
                     "(`let r be … …` then `is_error`), forward it (`try`), or say in the code why "
                     "the failure does not matter", s->kids[0]->tok.line ? s->kids[0]->tok.line : s->line);
            // ② `let r be … <result 를 내는 부름>` 인데 r 을 한 번도 안 읽는다.
            //   ★★★ 이 갈래는 한 번 뺐다가 되돌렸다 (RFC-0115 §8-25, 2026-09-17). 뺐던 까닭은
            //     «나는 이 실패를 일부러 넘긴다» 를 적는 철자가 없어서였다 — 물어도 없앨 방법이
            //     없는 경고는 길을 닫는다(결함 노트 #75). 소유자가 그 철자를 **`drop`** 으로 골랐고,
            //     `drop` 은 이미 «나는 이것을 여기서 끝낸다» 를 뜻하므로 새 낱말이 들지 않았다.
            //   ☞ 재는 법이 그래서 단순하다: `drop r .` 도 **이름을 한 번 부르는 것**이므로 아래
            //     세기에 잡힌다. 곧 담아 두고 `drop` 도 안 한 자리만 남는다.
            if ((kw == LOW_KW_LET || kw == LOW_KW_VAR) && s->nkids >= 3 && ck_atom(s->kids[1])) {
                const low_cst_t *rhs = s->kids[s->nkids - 1];
                while (rhs && rhs->kind == LOW_CST_GROUP && rhs->nkids == 1) rhs = rhs->kids[0];
                proven_u8str_view_t head = { 0 };
                if (rhs && rhs->kind == LOW_CST_FORM && rhs->nkids && ck_atom(rhs->kids[0]))
                    head = rhs->kids[0]->tok.lex;
                else if (rhs && ck_atom(rhs)) head = rhs->tok.lex;
                if (head.size && ck_op_returns_result(tab, nt, head) &&
                    ck_name_count(body, s->kids[1]->tok.lex) <= 1)
                    warn(out, "W-RESULT-DISCARD",
                         "this binding holds a `result` that nothing ever reads — the failure it "
                         "carries is discarded as surely as if the call were a bare statement. Look "
                         "at it (`is_error`), forward it (`try`), or — if letting this one go is the "
                         "right thing, as it often is when closing a handle on an error path — say "
                         "so with `drop <name> .`",
                         s->kids[1]->tok.line ? s->kids[1]->tok.line : s->line);
            }
        }
    }
    for (proven_size_t i = 0; i < nd->nkids; i++) ck_result_discard_walk(out, body, nd->kids[i], tab, nt);
}
// ③ `errors <갈래>` 를 적어 놓고 몸통이 그 오류를 한 번도 내지 않는다.
//   ★ `try` 가 있으면 **부른 쪽의 오류를 그대로 넘긴다** — 그 갈래는 여기서 안 보이므로 세지 않는다.
// ★ 몸통이 **남의 result 를 그대로 넘기는가** — `return <result 를 내는 부름>`.
//   그 갈래는 부른 쪽이 내는 것이라 여기서는 안 보인다. 안 보이는 것을 «없다» 고 하면 오탐이다.
//   (`docs/manual/examples/ch19/complete.low` 의 `session` 이 이것을 잡아 주었다.)
static bool ck_forwards_result(const low_cst_t *nd, const low_opinfo_t *tab, proven_size_t nt) {
    if (!nd) return false;
    if (nd->kind == LOW_CST_FORM && nd->nkids >= 2 && ck_atom(nd->kids[0]) &&
        nd->kids[0]->tok.kw == LOW_KW_RETURN) {
        const low_cst_t *v = nd->kids[1];
        while (v && v->kind == LOW_CST_GROUP && v->nkids == 1) v = v->kids[0];
        proven_u8str_view_t h = { 0 };
        if (v && v->kind == LOW_CST_FORM && v->nkids && ck_atom(v->kids[0])) h = v->kids[0]->tok.lex;
        else if (v && ck_atom(v)) h = v->tok.lex;
        if (h.size && ck_op_returns_result(tab, nt, h)) return true;
    }
    for (proven_size_t i = 0; i < nd->nkids; i++)
        if (ck_forwards_result(nd->kids[i], tab, nt)) return true;
    return false;
}
static void ck_errors_unraised(low_check_result_t *out, const low_cst_t *f, const low_cst_t *body,
                               const low_opinfo_t *tab, proven_size_t nt) {
    if (!body) return;
    if (ck_uses_head(body, "try")) return;
    if (ck_forwards_result(body, tab, nt)) return;
    for (proven_size_t j = 2; j + 1 < f->nkids; j++) {
        if (!ck_atom(f->kids[j]) || !veq(f->kids[j]->tok.lex, "errors")) continue;
        if (!ck_atom(f->kids[j + 1])) continue;
        proven_u8str_view_t v = f->kids[j + 1]->tok.lex;
        if (ck_name_count(body, v) > 0) continue;
        warn(out, "W-ERRORS-UNRAISED",
             "this `errors` clause names a failure the body never returns. The clause is read as a "
             "promise about what this op can do, and the callers write their handling from it — a "
             "branch that can never be taken is dead code the reader cannot tell from live code. "
             "Return it (`return error <variant>`), forward one (`try`), or remove the clause",
             f->kids[j]->tok.line ? f->kids[j]->tok.line : f->line);
    }
}
// ⑤ #58 — 붙은 op(`fn <타입>.<이름>`)의 **수신자는 첫 입력**이어야 한다.
static void ck_method_receiver(low_check_result_t *out, const low_cst_t *f) {
    if (f->nkids < 2 || !ck_atom(f->kids[1])) return;
    proven_u8str_view_t nm = f->kids[1]->tok.lex;
    proven_size_t dot = nm.size;
    for (proven_size_t i = 0; i < nm.size; i++) if (nm.ptr[i] == (proven_byte_t)'.') { dot = i; break; }
    if (dot >= nm.size || dot == 0) return;                 // 붙은 op 이 아니다
    proven_u8str_view_t ty = { .ptr = nm.ptr, .size = dot };
    low_op_header_t h = low_op_header(f);
    if (!h.np) return;
    proven_size_t cw = h.p[0].core;
    if (cw < f->nkids && ck_atom(f->kids[cw]) && proven_u8str_view_eq(f->kids[cw]->tok.lex, ty))
        return;                                             // 첫 입력이 수신자다 — 옳다
    for (proven_size_t q = 1; q < h.np; q++) {              // 다른 자리에 있으면 그 자리를 말한다
        proven_size_t c2 = h.p[q].core;
        if (c2 < f->nkids && ck_atom(f->kids[c2]) && proven_u8str_view_eq(f->kids[c2]->tok.lex, ty)) {
            emit(out, "E-METHOD-RECV",
                 "an op attached to a type takes that type as its FIRST input — that is what `method "
                 "<value> <name> …` passes. Here the receiver is not first, so a `method` call hands "
                 "the value to the wrong parameter: it used to pass `--check` and stop at run time "
                 "with `E-VM-TYPE`. Move the receiver to the first `input` clause",
                 f->kids[1]->tok.line);
            return;
        }
    }
}
// ★★★★★ **쓰기 빌림은 하나다 — 액터 경계를 넘어서도** (정본 §8.4 · 결함 노트 #54, 2026-09-16).
//   같은 `mut slice` 를 두 할당기에 `init` 으로 건네면 둘이 **같은 자리를 나눠 줬다**(`pv[0]` 에 65 를
//   쓰고 `qv[0]` 에 66 을 쓰면 `pv` 를 읽어도 66). 배타 규칙이 `send` 경계에서 서지 않았다.
//   ★ 좁게 문다: **쓰기 가능한 자리**를 **서로 다른 두 액터**에게 건넨 자리만.
typedef struct { proven_u8str_view_t name, actor; proven_u32 line; } ck_lent_t;
static void ck_actor_lend_walk(low_check_result_t *out, const low_cst_t *nd,
                               const ck_bind_t *binds, proven_size_t nb,
                               ck_lent_t *seen, proven_size_t *ns, proven_size_t cap) {
    if (!nd) return;
    if (nd->kind == LOW_CST_FORM && nd->nkids >= 4 && ck_atom(nd->kids[0]) &&
        nd->kids[0]->tok.kw == LOW_KW_SEND && ck_atom(nd->kids[1])) {
        proven_u8str_view_t act = nd->kids[1]->tok.lex;
        for (proven_size_t q = 3; q < nd->nkids; q++) {
            const low_cst_t *arg = nd->kids[q];
            if (!ck_atom(arg) || arg->tok.kind != LOW_TOK_IDENT || arg->tok.kw != LOW_KW_NONE) continue;
            bool mutable_place = false;
            for (proven_size_t z = 0; z < nb; z++)
                if (proven_u8str_view_eq(binds[z].name, arg->tok.lex) && binds[z].is_mut_place)
                    mutable_place = true;
            if (!mutable_place) continue;
            bool found = false;
            for (proven_size_t z = 0; z < *ns; z++)
                if (proven_u8str_view_eq(seen[z].name, arg->tok.lex)) {
                    found = true;
                    if (!proven_u8str_view_eq(seen[z].actor, act))
                        emit(out, "E-EXCL",
                             "the same WRITABLE place was handed to a SECOND actor. A write borrow is "
                             "exclusive (§8.4): two actors holding the same bytes both hand them out, so "
                             "two containers silently overlap and a write through one is read through the "
                             "other (measured: 65 written, 66 read back). Give each actor its own bytes — "
                             "`subslice` the buffer into pieces that do not overlap",
                             arg->tok.line);
                    break;
                }
            if (!found && *ns < cap) {
                seen[*ns].name = arg->tok.lex; seen[*ns].actor = act;
                seen[*ns].line = arg->tok.line; (*ns)++;
            }
        }
    }
    for (proven_size_t i = 0; i < nd->nkids; i++)
        ck_actor_lend_walk(out, nd->kids[i], binds, nb, seen, ns, cap);
}
// ★★★★ **리터럴은 고칠 수 없다 — 묶는 자리에서도** (정본 §6.1.4(12) · 결함 노트 #84, 2026-09-16).
//   `let buf be mut slice u8 "abc" .` 뒤의 `set (index buf 0) 65` 가 통과했다. 리터럴은 프로그램에
//   박힌 바이트이고 고칠 자리가 아니다 — VM 은 고치고 네이티브는 안 고쳤다(두 답).
static void ck_mut_literal_bind_walk(low_check_result_t *out, const low_cst_t *nd) {
    if (!nd) return;
    if (nd->kind == LOW_CST_FORM && nd->nkids >= 4 && ck_atom(nd->kids[0]) &&
        (nd->kids[0]->tok.kw == LOW_KW_LET || nd->kids[0]->tok.kw == LOW_KW_VAR)) {
        bool has_mut = false, via_alloc = false; proven_size_t be = nd->nkids;
        for (proven_size_t z = 2; z < nd->nkids; z++) {
            if (!ck_atom(nd->kids[z])) continue;
            if (nd->kids[z]->tok.kw == LOW_KW_BE) { be = z; break; }
            if (veq(nd->kids[z]->tok.lex, "mut") || veq(nd->kids[z]->tok.lex, "mut_ref")) has_mut = true;
            if (veq(nd->kids[z]->tok.lex, "using")) via_alloc = true;   // 할당기가 준 바이트 — 박힌 리터럴이 아니다(RFC-0132 §13.7)
        }
        if (has_mut && !via_alloc && be + 1 < nd->nkids) {
            const low_cst_t *init = nd->kids[be + 1];
            // ★★★★★ **리터럴은 한 겹 뒤에 숨을 수 있다** (2026-09-18, X-0032).
            //   이 검사는 `be` 바로 뒤의 **원자**만 보고 있었다. 그래서
            //   `var d be mut slice u8 view_array u8 "  " .` 가 **그대로 통과했고**,
            //   VM 은 그 리터럴을 고쳐 주고 네이티브는 **트랩했다** — 차등 훑기가 그것을
            //   잡았다. 검사기가 볼 수 있었던 것을 오라클이 대신 잡은 자리다.
            //   ⇒ 재해석하는 한 겹(`view_array`·`subslice`)을 지나서도 본다.
            while (init && init->kind == LOW_CST_GROUP && init->nkids == 1) init = init->kids[0];
            if (init && init->kind == LOW_CST_FORM && init->nkids >= 2 && ck_atom(init->kids[0]) &&
                veq(init->kids[0]->tok.lex, "view_array")) {
                // ☞ `subslice <리터럴> 0 0` 은 넣지 않는다 — 길이 0 인 자리표이고 쓸 수가 없다.
                //   («쓸 수 있는 것처럼 보이는가» 가 아니라 «쓸 수 있는가» 로 가른다.)
                const low_cst_t *inner = init->kids[init->nkids - 1];
                while (inner && inner->kind == LOW_CST_GROUP && inner->nkids == 1) inner = inner->kids[0];
                for (proven_size_t q = 1; q < init->nkids; q++)
                    if (ck_atom(init->kids[q]) &&
                        (init->kids[q]->tok.kind == LOW_TOK_STRING ||
                         init->kids[q]->tok.kind == LOW_TOK_HEREDOC)) { init = init->kids[q]; break; }
            }
            // ★ RFC-0132 T2b-1 — 상수 원소 나열 리터럴도 박힌 바이트다(§13.2 ⓑ). 쓸 수 있는 자리는 T2b-2 의 `var` 배열이다.
            if (nd->kids[0]->tok.kw == LOW_KW_LET &&                         // ★ T2b-2: `var` 는 쓸 수 있는 틀 안 자리(ⓐ)다
                init && init->kind == LOW_CST_FORM && init->nkids >= 2 && ck_atom(init->kids[0]) &&
                init->kids[0]->tok.kw == LOW_KW_LIT && ck_atom(init->kids[1]) &&
                (veq(init->kids[1]->tok.lex, "array") || veq(init->kids[1]->tok.lex, "slice")))
                emit(out, "E-TYPE-ARGMUT",
                     "a list LITERAL was bound with `let` to a type marked `mut`. A `let` list is a VIEW (read-only — "
                     "constant lists are bytes baked into the program, RFC-0132 §13.2 ⓑ). To write the cells, declare it "
                     "with `var`: `var buf be lit array u8 16 _ . .`", nd->kids[0]->tok.line);
            if (ck_atom(init) && (init->tok.kind == LOW_TOK_STRING || init->tok.kind == LOW_TOK_HEREDOC))
                emit(out, "E-TYPE-ARGMUT",
                     "a string LITERAL was bound to a name declared `mut`. A literal is bytes baked "
                     "into the program, not a place that can be written: the VM used to change them "
                     "while the native build did not, so the same program gave two answers, and a "
                     "library op writing there killed the native build. Take bytes you will change "
                     "from `alloc_bytes` or from the caller's buffer, and copy the literal into them",
                     init->tok.line);
        }
    }
    for (proven_size_t i = 0; i < nd->nkids; i++) ck_mut_literal_bind_walk(out, nd->kids[i]);
}
// ★★★★★ **`let` 의 불변은 참조로 뚫리지 않는다** (정본 §6.5.1(1) · §8.8 · 결함 노트 #46).
static void ck_mutref_of_ro_walk(low_check_result_t *out, const low_cst_t *nd,
                                 const ck_bind_t *binds, proven_size_t nb) {
    if (!nd) return;
    if (nd->kind == LOW_CST_FORM && nd->nkids >= 2 && ck_atom(nd->kids[0]) &&
        veq(nd->kids[0]->tok.lex, "mut_ref") && ck_atom(nd->kids[1]) &&
        nd->kids[1]->tok.kind == LOW_TOK_IDENT && nd->kids[1]->tok.kw == LOW_KW_NONE &&
        ck_name_is_ro(binds, nb, nd->kids[1]->tok.lex))
        emit(out, "E-TYPE-ARGMUT",
             "a WRITE borrow (`mut_ref`) was taken of a name that cannot be written: a `let` binding "
             "(or a shared input). `let` says the value does not change (§6.5.1) — if a borrow could "
             "change it, the reader who checked that name once would be wrong, and the borrow makes "
             "the change invisible at the call site. Bind it with `var`, or take a read borrow (`ref`)",
             nd->kids[1]->tok.line);
    for (proven_size_t i = 0; i < nd->nkids; i++) ck_mutref_of_ro_walk(out, nd->kids[i], binds, nb);
}
static void ck_launder_walk(low_check_result_t *out, const low_cst_t *nd,
                            const low_opinfo_t *tab, proven_size_t nt,
                            const ck_bind_t *binds, proven_size_t nb,
                            const low_parse_result_t *pr) {
    if (!nd) return;
    if (nd->kind == LOW_CST_FORM && nd->nkids >= 2 && nd->kids[0]->kind == LOW_CST_ATOM &&
        nd->kids[0]->tok.kw == LOW_KW_NONE) {
        proven_u8str_view_t callee = nd->kids[0]->tok.lex;
        // ★★★★ **한정된 호출은 그 모듈의 항목만 본다** (알려진 결함 qualifier-erased-flat-lookup).
        //   전엔 이름만 맞춰 **첫 일치**를 썼다 ⇒ 같은 이름을 가진 두 모듈이 한 단위에 있으면
        //   **파일 순서에 따라** 남의 시그니처로 판정했다: `mypoly.block` 이 `mycha.block` 의
        //   `mut` 파라미터로 검사돼 **초록이어야 할 프로그램이 빨간불**이 됐다(실측 2026-08-14).
        //   같은 프로그램이 두 답을 내는 것은 순서가 뜻을 바꾼 것이고, 그것은 결함이다.
        proven_u8str_view_t qmod = nd->kids[0]->qual_mod;
        for (proven_size_t i = 0; i < nt; i++) {
            if (!proven_u8str_view_eq(tab[i].name, callee)) continue;
            if (qmod.size && tab[i].mod.size && !proven_u8str_view_eq(tab[i].mod, qmod)) continue;
            if (!tab[i].form) break;
            low_op_header_t hc = low_op_header(tab[i].form);
            for (proven_size_t q = 0; q < hc.np; q++) {
                proven_size_t ai = 1 + q;
                if (ai >= nd->nkids) break;
                const low_cst_t *arg = nd->kids[ai];
                if (!ck_param_mutq(tab[i].form, &hc, q)) continue;    // mut/owned/mut_ref 자리만
                if (ck_arg_is_ro(arg, binds, nb, pr))
                    emit(out, "E-TYPE-ARGMUT",
                         "this argument is passed to a `mut`/`owned`/`mut_ref` parameter, but it is "
                         "NOT a mutable place — it is READ-ONLY (a shared parameter, a `let` without "
                         "`mut`, a `ref X` shared reference, or a subslice/index of one). The callee "
                         "could write through it, mutating storage the caller only holds read-only — "
                         "laundering the shared-slice / fn-purity guarantee that the DIRECT "
                         "`set (index s …)` already forbids. Pass a mutable place — a `var` local, a "
                         "`mut`/`owned`/`mut_ref` binding, or `mut_ref X` — or declare this parameter `mut`",
                         arg->tok.line);
            }
            break;
        }
    }
    for (proven_size_t i = 0; i < nd->nkids; i++) ck_launder_walk(out, nd->kids[i], tab, nt, binds, nb, pr);
}

// ★★★ RFC-0057 후속 — **반환값을 통한 세탁을 막는다** (2026-07-23, A2).
//
//   인자 세탁(위)의 쌍둥이 구멍: op 이 **공유(읽기 전용) 입력**을 `mut slice`/`mut_ref` 로 선언한
//   **출력으로 반환**하면, 호출자가 그 반환값을 통해 자기 읽기 전용 버퍼를 쓴다 — 검사가 못 봤다:
//
//       proc leak   input s slice u8 . output mut slice u8 . effects none . do return s . end
//       fn attack input s slice u8 . output u8 .          effects none . do
//         let m be mut slice u8 leak s . set (index m 0) 99 . return index s 0 . end
//                                        ↑ m 은 s 의 별칭 — fn 이 공유 입력을 변조(실측 99)
//
//   `attack` 의 지역 쓰기는 **관측적 순수**(로컬 var 라 봄)로 통과하고, `leak s` 의 인자 s 는
//   leak 의 파라미터가 mut 이 아니라(공유) 인자-세탁 검사도 안 걸린다. 세탁은 **leak 의 반환**에서 난다.
//
//   ⇒ **규칙(정의 지점):** op 의 **출력 타입이 가변 장소**(`mut slice`/`mut_ref`)이면, 그 op 의
//     `return <식>` 이 **확정 읽기 전용 장소**(공유 파라미터·`let` 무-mut·`ref X`·그 subslice/index/
//     field)를 돌려줘선 안 된다. 인자 세탁과 **같은 술어 `ck_arg_is_ro`** 를 반환 자리에 쓴다.
//     신선한 할당·`mut` 파라미터·`mut` 지역을 돌려주는 정당한 mut-출력은 통과한다(ro 아님).
//   ★ E-TYPE-ARGMUT 과 같은 이유로 **나무 의존**(subslice/index 탐지) ⇒ 고유 코드 E-TYPE-RETMUT,
//     nest 게이트의 flat↔tree 대조에서 함께 걸러낸다(맨 이름 `return s` 는 어차피 양쪽 동일).
static bool ck_output_is_mut_place(const low_cst_t *f, const low_op_header_t *h) {
    for (proven_size_t z = h->out_s; z < h->out_e && z < f->nkids; z++)
        if (f->kids[z]->kind == LOW_CST_ATOM &&
            (veq(f->kids[z]->tok.lex, "mut") || veq(f->kids[z]->tok.lex, "mut_ref")))
            return true;
    return false;
}

static void ck_retlaunder_walk(low_check_result_t *out, const low_cst_t *nd,
                               const ck_bind_t *binds, proven_size_t nb,
                               const low_parse_result_t *pr) {
    if (!nd) return;
    if (nd->kind == LOW_CST_FORM && nd->nkids == 2 && nd->kids[0]->kind == LOW_CST_ATOM &&
        nd->kids[0]->tok.kw == LOW_KW_RETURN && ck_arg_is_ro(nd->kids[1], binds, nb, pr))
        emit(out, "E-TYPE-RETMUT",
             "this op declares a MUTABLE output (`mut slice`/`mut_ref`) but RETURNS a read-only "
             "place — a shared parameter, a `let` without `mut`, a `ref X`, or a subslice/index/field "
             "of one. The caller would receive a mutable alias of storage that is only held read-only, "
             "and could write through it — laundering the shared / fn-purity guarantee (measured: "
             "a pure fn mutated its shared input via such a return). Return a mutable place — a "
             "fresh allocation, a `mut`/`owned` parameter, or a `var`/`mut` local — or drop `mut` from "
             "the output type",
             nd->kids[0]->tok.line);
    for (proven_size_t i = 0; i < nd->nkids; i++) ck_retlaunder_walk(out, nd->kids[i], binds, nb, pr);
}

low_check_result_t low_check(proven_allocator_t work, const low_parse_result_t *pr) {
    g_ck_pr = pr;   // ★ RFC-0112 D6·D7 — `via` 와 권한 칸이 단위를 찾는다
    low_check_result_t out = { .ok = true };
    proven_result_array_t da = PROVEN_ARRAY_INIT(work, low_diag_t, 8);
    proven_result_array_t oa = PROVEN_ARRAY_INIT(work, low_opinfo_t, 16);
    if (da.err != PROVEN_OK || oa.err != PROVEN_OK) { out.ok = false; return out; }
    out.diags = da.value;
    proven_array_t ops = oa.value;

    // ★★ **없앤 낱말** — 조용히 IDENT 로 재해석되게 두면 안 된다.
    //   `type h is u8 .` 의 `is` 가 타입 이름으로 읽히면 **조용히 틀린 답**이 된다.
    //   그래서 **거절하고, 무엇으로 바꿔 쓰라고 말한다.** (어휘 축소의 값은 여기서 결정된다:
    //   낱말을 빼면서 그 자리를 침묵으로 채우면, 줄인 게 아니라 **숨긴 것**이다.)
    ck_removed_words(&out, pr);
    // ★★★ 가시성·no-shadow 는 **narrow 전에** 돈다 — 한정(`M.x`)과 bare 를 **구별해야** 강제할
    //   수 있다(RFC-0011 §6.3 qualified-by-default). narrow 가 먼저 벗기면 그 구별이 사라진다.
    ck_visibility(&out, pr);      // ★★★ **가시성** — export 도 bare 로는 못 넘는다(한정 강제)
    ck_import_noshadow(&out, pr); // ★★★ **import 는 이름을 덮어쓰지 않는다** (E-NAME-COLLISION)
    ck_use_aliased(&out, pr);   // ★ 별칭을 적었으면 원래 이름은 서지 않는다 (#24)
    ck_narrow_qual(pr);   // ★ `M.member` → bare (뒤 검증기·IR 이 bare 로 본다, RFC-0011). additive.
    ck_package(&out, pr);
    ck_toplevel(&out, pr);
    ck_struct_fields(&out, pr);
    // ★ region 탈출 검사는 op 표가 선 **뒤로** 옮겼다(후속 ③ R1 — 부름의 요약을 읽는다). 아래 «op 표» 다음.
    ck_immutable(&out, pr);       // ★ `let` 은 **불변**이다 — 아니면 `var` 의 동의어일 뿐이다
    ck_guard_diverges(&out, pr);  // ★ `guard` 의 else 는 **발산**해야 한다 — 아니면 `if not` 이다

    // pass 1: collect fn/proc declarations.
    // ★★★ **actor 핸들러도 모은다**(2026-07-23). 전엔 top-level `pr->forms` 만 봐서 actor 폼 안에
    //   중첩된 핸들러가 **테이블에 안 들어갔고**, 그래서 pass2 효과 초과 검사도, 순수성 검사도
    //   통째로 건너뛰었다 — `fn 핸들러 effects none` 이 다른 actor 에 **변이 메시지를 send**
    //   해도 통과했다(효과 세탁). ⇒ actor 의 BLOCK 을 열어 그 안의 fn/proc 도 같은 op 으로 본다.
    //   ★ atomic_* 같은 **이름 기반** 효과는 CST 검사가 이미 잡았지만 send(호출) 는 안 잡혔다.
    const low_cst_t *op_forms[512]; const low_cst_t *op_actor[512]; proven_size_t nop_forms = 0;
    proven_u8str_view_t op_mod[512]; proven_u8str_view_t curmod = { 0 };
    for (proven_size_t i = 0; i < pr->nforms && nop_forms < 512; i++) {
        const low_cst_t *f = pr->forms[i]; ck_cur_form = f;
        if (f->kind != LOW_CST_FORM || f->nkids < 1 || f->kids[0]->kind != LOW_CST_ATOM) continue;
        low_kw_t k0 = f->kids[0]->tok.kw;
        // ★ 선언 모듈을 따라간다 — 한정된 호출이 **어느 표 항목**을 뜻했는지 가리려면 필요하다.
        if (k0 == LOW_KW_MODULE && f->nkids >= 2 && f->kids[1]->kind == LOW_CST_ATOM) {
            curmod = f->kids[1]->tok.lex; continue;
        }
        if (k0 == LOW_KW_FN || k0 == LOW_KW_PROC) { op_actor[nop_forms] = NULL; op_mod[nop_forms] = curmod; op_forms[nop_forms++] = f; }
        else if (k0 == LOW_KW_ACTOR) {
            const low_cst_t *blk = (f->kids[f->nkids - 1]->kind == LOW_CST_BLOCK) ? f->kids[f->nkids - 1] : NULL;
            if (blk)
                for (proven_size_t q = 0; q < blk->nkids && nop_forms < 512; q++) {
                    const low_cst_t *h = blk->kids[q];
                    if (h->kind == LOW_CST_FORM && h->nkids >= 2 && h->kids[0]->kind == LOW_CST_ATOM &&
                        (h->kids[0]->tok.kw == LOW_KW_FN || h->kids[0]->tok.kw == LOW_KW_PROC))
                        { op_actor[nop_forms] = blk; op_mod[nop_forms] = curmod; op_forms[nop_forms++] = h; }
                }
        }
    }
    for (proven_size_t fi = 0; fi < nop_forms; fi++) {
        const low_cst_t *f = op_forms[fi];
        if (f->kind != LOW_CST_FORM || f->nkids < 2 || f->kids[0]->kind != LOW_CST_ATOM) continue;
        low_kw_t kw = f->kids[0]->tok.kw;
        if (kw != LOW_KW_FN && kw != LOW_KW_PROC) continue;
        const low_cst_t *body = (f->kids[f->nkids - 1]->kind == LOW_CST_BLOCK) ? f->kids[f->nkids - 1] : NULL;
        bool found = false;
        unsigned de = decl_effect(f, &found);
        bool is_calc = (kw == LOW_KW_FN);
        // fn: pure by default (no clause = none). proc: unrestricted unless it
        // narrows itself with an explicit effects clause (so mini procops aren't flagged).
        // ★★ SPEC-003 §33: "`fn + effects io` 는 **구조적으로 거부된다**."
        //   §27: "`fn` = **강제된 순수 계약**(effects none) — 호출자가 의존하는 보증."
        //   그런데 `fn c . effects io .` 가 조용히 통과했다. 그러면 fn/proc 의
        //   **1비트가 아무것도 뜻하지 않는다** — 순수/절차를 항상 명시하라는 규칙이 장식이 된다.
        if (is_calc && found && de != EFF_NONE)
            emit_r(&out, "E-EFFECT-CALC",
                 "a fn is an ENFORCED purity contract (SPEC-003 §27) — it cannot declare an "
                 "effect. `fn + effects io` is structurally rejected (§33): the pure/procedural "
                 "bit is what callers depend on. Declare it a `proc` instead",
                 "R-CALC-TO-PROC",   // the fix is unambiguous: this op declares effects → it is a proc
                 f->line);
        // ★★★★★ **`fn` 에 `effects none` 은 에러다** (2026-08-26 · 소유자 결정).
        //   `fn` 은 **강제된 순수 계약**이라 효과 집합이 이미 `none` 으로 정해져 있다
        //   (바로 위 `E-EFFECT-CALC` 가 다른 값을 구조적으로 거절한다). 그러면
        //   `effects none .` 은 **아무것도 말하지 않으면서 모든 선언에 한 줄을 더한다.**
        //
        //   ★★ **이 자리는 하루 만에 뒤집혔다 — 뒤집힌 이유를 지우지 않는다.**
        //   2026-08-25 에는 **경고**였고, 근거는 이랬다: *"뜻이 틀린 게 아니라 없어도 같다.
        //   틀린 것은 막고 남는 것은 알린다 — 둘을 같은 세기로 다루면 둘 다 무뎌진다."*
        //   2026-08-26 소유자가 승격했다. 다른 규율을 택한 것이다: **동의어를 남기지 않는다**
        //   (SPEC-002 §2.5 "동의어 금지" 와 같은 선). 같은 뜻에 두 표기가 공존하면 코퍼스가
        //   갈리고, 그러면 문법 카드도 두 가지를 가르쳐야 한다.
        //   ⇒ 지금은 **하나만 옳다**: `fn` 헤더에 `effects` 절은 없다. `effects` 는 `proc` 전용이다.
        //   ☞ 수리는 여전히 명확하다(절을 지운다) — 그래서 repair id `R-DROP-EFFECT` 를 그대로 나른다.
        if (is_calc && found && de == EFF_NONE)
            emit_r(&out, "E-EFFECT-REDUNDANT",
                 "a `fn` is pure by contract, so `effects none .` says nothing that the `fn` "
                 "did not already say — DROP the clause. One meaning must have one spelling "
                 "(SPEC-002 §2.5). (Keep writing `effects` on `proc`: there it NARROWS, and "
                 "omitting it means unrestricted.)",
                 "R-DROP-EFFECT",
                 f->line);
        unsigned declared = is_calc ? EFF_NONE
                                    : (found ? de : (EFF_IO | EFF_ALLOC | EFF_STATE));
        low_opinfo_t info = { .name = f->kids[1]->tok.lex, .is_calc = is_calc, .form = f,
                              .explicit_eff = (!is_calc && found), .declared = declared, .body = body,
                              .is_handler = (op_actor[fi] != NULL), .actor_blk = op_actor[fi],
                              .mod = op_mod[fi] };
        // ★ 핸들러가 `effects state` 를 선언하면 panic 도 함의한다 — 오염된 상태는 panic 으로
        //   드러나고(vm_restart 의 restart 패턴), state 권리와 한 묶음이다(RFC-0007 핸들러 규율).
        info.self_declared = declared;
        if (op_actor[fi] && (declared & EFF_STATE)) info.self_declared |= EFF_PANIC;   // 자기검사만
        (void)PROVEN_ARRAY_PUSH(&ops, low_opinfo_t, info);
    }

    // ★ region 탈출 검사 — op 본문마다. (op 표를 읽는다 — 후속 ③ R1 요약)
    ck_r1_tab = (const low_opinfo_t *)ops.data; ck_r1_nt = ops.len;
    for (proven_size_t i = 0; i < pr->nforms; i++) {
        const low_cst_t *f = pr->forms[i]; ck_cur_form = f;
        if (!f || f->kind != LOW_CST_FORM || f->nkids < 2 || !ck_atom(f->kids[0])) continue;
        low_kw_t k0 = f->kids[0]->tok.kw;
        if (k0 != LOW_KW_FN && k0 != LOW_KW_PROC) continue;
        ck_rgctx_t rc = { .nrgn = 0, .opform = f };
        proven_u8str_view_t inn[CK_RG_NAMES], tnt[CK_RG_NAMES]; proven_size_t nin = 0, ntn = 0;
        ck_ft_n = 0; ck_ft_full = false;
        ck_region_walk(&out, f, &rc, inn, &nin, tnt, &ntn, false);
        ck_lit_frames(&out, f);   // ★ RFC-0132 T2b-2 — 틀 안 나열 자리의 수명 · 한도
        ck_lit_identity(&out, f); // ★ RFC-0132 T2b-3 — ⓑ 둘의 «같은 자리인가» 물음 거절(§13.3)
        ck_record_writes(&out, f); // ★ X-0082 — 쓸 수 없는 레코드의 칸·배열 칸에 쓰기 거절
        if (ck_ft_full)
            emit(&out, "E-IR-LIMIT", "this op holds more field-level taints than the region checker's table — refused "
                 "rather than checked partly (RFC-0116 R1)", f->line);
        ck_ft_n = 0;
        proven_u8str_view_t z = { 0 };
        ck_borrow_walk(&out, f, z, z, false, false);
    }
    ck_stack_bound(&out);   // ★ RFC-0135 S3 — 진입 op 마다 스택의 틀 안 나열 최대
    ck_r1_tab = NULL; ck_r1_nt = 0;

    // ★ 액터 상태 칸에 빌림을 두는 것 (§8.4.1 · 결함 노트 #74) · 상태를 읽는 오류 조건 (#62)
    // ★★★ **모듈 수준 `var` 를 먼저 모은다** (결함 노트 #62 의 넓힘, 2026-09-17).
    //   `errors` 조건은 **들어올 때 값**으로 읽는다(정본 §6.4.2). 그러므로 조건에 설 수 있는
    //   이름은 들어올 때와 나갈 때가 **같은 것**뿐이다 — 모듈 `var` 는 몸통이 바꿀 수 있다.
    ck_mutset_t ck_modvars = { 0 };
    for (proven_size_t i = 0; i < pr->nforms; i++) {
        const low_cst_t *mf = pr->forms[i];
        if (mf && mf->kind == LOW_CST_FORM && mf->nkids >= 2 && ck_atom(mf->kids[0]) &&
            mf->kids[0]->tok.kw == LOW_KW_VAR && ck_atom(mf->kids[1]))
            ck_mutset_add(&ck_modvars, mf->kids[1]->tok.lex);
    }
    // ★ 액터 밖의 op 도 같은 규율을 받는다 — 액터만 보던 것이 결함 노트 #62 의 좁음이었다.
    for (proven_size_t i = 0; i < pr->nforms; i++) {
        const low_cst_t *tf = pr->forms[i];
        if (tf && tf->kind == LOW_CST_FORM && tf->nkids >= 2 && ck_atom(tf->kids[0]) &&
            (tf->kids[0]->tok.kw == LOW_KW_FN || tf->kids[0]->tok.kw == LOW_KW_PROC))
            ck_errors_on_state(&out, tf, NULL, &ck_modvars);
    }
    for (proven_size_t i = 0; i < pr->nforms; i++) {
        const low_cst_t *af = pr->forms[i];
        if (!(af->kind == LOW_CST_FORM && af->nkids >= 2 && ck_atom(af->kids[0]) &&
              af->kids[0]->tok.kw == LOW_KW_ACTOR)) continue;
        ck_actor_state_refs(&out, af);
        for (proven_size_t b = 0; b < af->nkids; b++) {
            const low_cst_t *blk = af->kids[b];
            if (!blk || blk->kind != LOW_CST_BLOCK) continue;
            for (proven_size_t q = 0; q < blk->nkids; q++) {
                const low_cst_t *hop = blk->kids[q];
                if (hop && hop->kind == LOW_CST_FORM && hop->nkids >= 2 && ck_atom(hop->kids[0]) &&
                    (hop->kids[0]->tok.kw == LOW_KW_FN || hop->kids[0]->tok.kw == LOW_KW_PROC))
                    ck_errors_on_state(&out, hop, blk, &ck_modvars);
            }
        }
    }

    // ★★ **중복 이름** — 이름공간이 **평면**이다(가림도, 한정 경로도 없다).
    //   그러면 같은 이름을 두 번 선언하는 것은 **오류다.** 구분할 방법이 없기 때문이다.
    //   모듈 링크가 이것을 드러냈다: 모듈 b 의 `g` 가 자기 모듈의 `f` 가 아니라
    //   **모듈 a 의 `f` 를 불렀다.** 조용히 틀린 이름을 골랐다 — 오류보다 나쁘다.
    //   (한정 경로(`a.f`)가 생기면 그때 이 규칙을 풀면 된다. 지금은 못 푼다.)
    {
        struct { proven_u8str_view_t name; const char *what; proven_u8str_view_t mod;
                 const low_cst_t *at; } decls[512];   // ★ at = 그 선언의 자리(진단이 짚는다)
        proven_size_t nd = 0;
        proven_u8str_view_t dcur = { 0 };     // ★ 지금 훑고 있는 모듈 (슬라이스 ③)
        for (proven_size_t i = 0; i < pr->nforms && nd < 512; i++) {
            const low_cst_t *f = pr->forms[i]; ck_cur_form = f;
            if (f->kind != LOW_CST_FORM || f->nkids < 2 || f->kids[0]->kind != LOW_CST_ATOM) continue;
            low_kw_t kw = f->kids[0]->tok.kw;
            if (kw == LOW_KW_MODULE && f->kids[1]->kind == LOW_CST_ATOM) dcur = f->kids[1]->tok.lex;
            const char *what = NULL;
            if (kw == LOW_KW_FN || kw == LOW_KW_PROC) what = "op";
            // ★★★ **모듈 이름과 타입 이름은 같은 이름공간을 써야 한다** (사용자 지적, RFC-0062).
            //
            //   `aaa` 모듈의 `bbb` 구조체는 자격 있는 이름 `aaa.bbb` 를 만든다.
            //   그런데 `aaa.bbb` 라는 **모듈**이 따로 있으면 — **같은 글자가 두 뜻**이 된다.
            //   그리고 이 언어는 그런 것을 견디지 않는다(교훈 7).
            //
            //   ⇒ 모듈 · 타입 · struct · enum · op 이 **한 통**에서 유일해야 한다.
            //     그래야 자격 있는 경로 `a.b.c` 의 각 마디가 **전역적으로 유일**하다.
            else if (kw == LOW_KW_MODULE) what = "module";
            else if (kw == LOW_KW_TYPE)   what = "type";
            else if (kw == LOW_KW_NEWTYPE) what = "newtype";
            else if (kw == LOW_KW_STRUCT) what = "struct";
            else if (kw == LOW_KW_ENUM)   what = "enum";
            // ★ X-0072 ②(소유자 «추천대로», 2026-09-27) — 시험 이름도 **같은 한 통**에 든다. 이름 공간이 하나이고 가리기가
            //   없는 언어에서 `fn a` 와 `test a` 가 함께 서면, `--run a` 가 어느 것을 부르는지 이름만으로는 말할 수 없다.
            else if (kw == LOW_KW_TEST)   what = "test";
            // ★★ X-0079 (2026-09-29, 소유자 «추천대로») — actor · trait · 모듈 `let`/`var` 도 **같은 한 통**이다.
            //   빠져 있었다: actor 둘이 같은 이름 · `actor pt` 와 `struct pt` · `trait pt` 와 `fn pt` 가 초록이었고,
            //   모듈 `let pt` 가 둘이면 **둘째가 조용히 버려졌고**, `let pt` 와 `fn pt` 가 함께면 `pt` 가 **함수 호출로
            //   읽혔다**(조용히 틀린 답). 정본은 처음부터 «여러 겹의 이름 공간은 없다» 였다 — 구현이 따라간다.
            //   C 쪽 이름(RFC-0134)도 «모듈 안 이름은 하나뿐» 에 기댄다.
            else if (kw == LOW_KW_ACTOR)  what = "actor";
            else if (kw == LOW_KW_TRAIT)  what = "trait";
            else if (kw == LOW_KW_LET || kw == LOW_KW_VAR) what = "module binding";
            else continue;
            if (f->kids[1]->kind != LOW_CST_ATOM) continue;
            {   // ★★★ **이름은 맨 식별자다** — 유일한 예외가 op 의 `Type.op` 이다.
                proven_u8str_view_t nm = f->kids[1]->tok.lex;
                proven_u32 ln = f->kids[1]->tok.line;
                // ★ mono 인스턴스(`op#T`, RFC-0021)는 **생성된 이름**이다 — 사용자 선언이 아니고, 타입부에 모듈
                //   한정 점(`op#mod.Type`)이 있을 수 있다. 이름 규칙 검증에서 제외한다(원본 op·타입은 각자 검사됨).
                bool is_inst = false;
                for (proven_size_t z = 0; z < nm.size; z++) if (nm.ptr[z] == (proven_u8)'#') { is_inst = true; break; }
                bool isop = (kw == LOW_KW_FN || kw == LOW_KW_PROC);
                if (isop && !is_inst) ck_ident_rules(&out, nm, ln);   // ★ op 이름도 _/__ 규칙(비-op 은 ck_plain_name 안에서)
                if (is_inst) {
                    /* 생성 인스턴스 — 건너뜀 */
                } else if (!isop) {
                    ck_plain_name(&out, nm, what, ln);
                } else if (ck_has_dot(nm)) {
                    if (ck_ndots(nm) != 1) {
                        emit(&out, "E-NAME-DOTTED",
                             "a type-associated op is `<Type>.<name>` — **exactly one** dot. "
                             "There are no multi-level namespaces (RFC-0062)", ln);
                    } else if (!ck_is_type_name(pr, ck_before_dot(nm))) {
                        emit(&out, "E-NAME-QUALIFIER",
                             "the part before the dot must be a DECLARED TYPE (struct/enum/type) — "
                             "`<Type>.<op>` puts the op in that type's namespace. It is not a module "
                             "path: this language has no multi-level namespaces (RFC-0062)", ln);
                    }
                }
            }
            // ★★ **빌트인과 같은 이름**은 그 선언을 **도달 불가능**하게 만든다.
            //   해석기가 언제나 빌트인을 고르기 때문이다. 선언했는데 **존재하지 않는다.**
            //   (수식자가 op 을 통째로 삼키던 것과 같은 병 — 다만 이번엔 이름이 삼킨다.)
            // ★★★ **제네릭은 이 검사를 통째로 빠져나가고 있었다** (2026-07-26, RFC-0084 §10 이 발견).
            //   틀은 단형화가 **검사 전에** 지우고, 남는 것은 `add#u32` 라 목록에 안 걸린다 ⇒
            //   `export proc add …` 가 제네릭이기만 하면 **조용히 통과**했다. 비제네릭 `proc add` 는
            //   정확히 걸리는데. **제네릭이라는 이유만으로 검사가 사라지는 것**은 검사가 아니다.
            //   ⇒ `#` 앞머리로 본다. 인스턴스 이름이 곧 틀의 이름이므로 진단은 여전히 정확하다.
            proven_u8str_view_t dn = f->kids[1]->tok.lex;
            for (proven_size_t q = 0; q < dn.size; q++)
                if (dn.ptr[q] == (proven_u8)'#') { dn.size = q; break; }
            if (low_ir_is_builtin_name(dn))
                emit(&out, "E-NAME-BUILTIN",
                     "this name is a BUILTIN — the resolver always picks the builtin, so your "
                     "declaration can never be called: it exists and does not exist. The namespace "
                     "is FLAT (no shadowing). Rename it",
                     f->kids[1]->tok.line);
            decls[nd].name = f->kids[1]->tok.lex; decls[nd].what = what; decls[nd].mod = dcur;
            decls[nd].at = f->kids[1]; nd++;
            // enum 변형도 이름이다 — 두 enum 이 같은 변형 이름을 쓰면 값이 뒤섞인다.
            if (kw == LOW_KW_ENUM) {
                const low_cst_t *b2 = f->kids[f->nkids - 1];
                if (b2->kind != LOW_CST_BLOCK) continue;
                for (proven_size_t v = 0; v < b2->nkids && nd < 512; v++) {
                    const low_cst_t *vn = b2->kids[v];
                    proven_u8str_view_t nm = (vn->kind == LOW_CST_ATOM) ? vn->tok.lex
                        : (vn->kind == LOW_CST_FORM && vn->nkids && vn->kids[0]->kind == LOW_CST_ATOM)
                            ? vn->kids[0]->tok.lex : (proven_u8str_view_t){ 0 };
                    if (!nm.size) continue;
                    decls[nd].name = nm; decls[nd].what = "enum variant"; decls[nd].mod = dcur;
                    decls[nd].at = f; nd++;
                }
            }
        }
        // ★★★ **이 규칙을 푼다 — 위 주석이 예고한 그 조건이 왔다** (RFC-0060 재개 · 단계 U ③).
        //   *"(한정 경로(`a.f`)가 생기면 그때 이 규칙을 풀면 된다. 지금은 못 푼다.)"*
        //   슬라이스 ②가 한정자를 **쓰이게** 했으므로, 다른 모듈의 같은 이름은 더 이상
        //   "구분할 방법이 없는" 것이 아니다. ⇒ 규칙이 둘로 갈린다:
        //     ① **같은 모듈** 안의 중복 = 여전히 E-NAME-DUP(한정자로도 못 가른다)
        //     ② **다른 모듈** 사이 = 허용. 단 **맨이름으로 부르면** E-NAME-AMBIG.
        // ★ 여기 `ambig[128]` 이 있었다 — **채워지기만 하고 아무도 안 읽었다**(RFC-0077 P1-5
        //   감사, 2026-08-14). 죽은 수집은 고정 표의 가장 조용한 형태다: 자르든 안 자르든
        //   아무 일도 안 일어나므로 **한계가 있다는 사실조차 뜻이 없다.** 지운다.
        for (proven_size_t i = 0; i < nd; i++)
            for (proven_size_t j = i + 1; j < nd; j++)
                if (proven_u8str_view_eq(decls[i].name, decls[j].name)) {
                    if (proven_u8str_view_eq(decls[i].mod, decls[j].mod)) {
                        // ★★★ **이름을 대고, 그 자리를 짚는다** (2026-08-07).
                        //   전엔 이름을 안 대고 **언제나 `1:0`**(첫 form)을 가리켰다. 그래서
                        //   두 라이브러리를 함께 쓰다 이 오류를 만나면 *"무엇이 겹쳤는지"* 를
                        //   손으로 찾아야 했다 — 실제로 이 조사가 거기서 멈췄다.
                        //   ☞ 진단은 **무엇이 틀렸는지**뿐 아니라 **어디인지**도 말해야 한다.
                        //   ★ `detail[96]` 에 **복사**한다 — `msg` 는 포인터로 보관되므로
                        //     스택 버퍼를 넘기면 매달린다(low_ir 에서 이미 한 번 밟은 함정).
                        emit_at(&out, "E-NAME-DUP",
                             "this name is declared twice in the SAME module — a qualifier cannot "
                             "tell them apart (both are `<module>.<name>`), so the second "
                             "declaration does not hide the first: it silently makes one of the "
                             "two unreachable",
                             decls[j].at ? decls[j].at : pr->forms[0]);
                        {
                            // ★ 쓰는 자리라 가변 매크로를 쓴다(`PROVEN_ARRAY_GET` 은 const 를 준다).
                            low_diag_t *last = PROVEN_ARRAY_GET_MUT(&out.diags, low_diag_t,
                                                                    out.diags.len - 1);
                            snprintf(last->detail, sizeof last->detail,
                                     "`%.*s` (%s) is declared twice in module `%.*s` — a qualifier "
                                     "cannot tell the two apart (both are `<module>.<name>`), so "
                                     "the second does not hide the first: it silently makes one of "
                                     "them unreachable",
                                     (int)decls[j].name.size, (const char *)decls[j].name.ptr,
                                     decls[j].what ? decls[j].what : "declaration",
                                     (int)decls[j].mod.size, (const char *)decls[j].mod.ptr);
                            last->msg = NULL;   // ★ 렌더러는 low_diag_text 로 detail 을 읽는다
                        }
                    }
                    j = nd;   // 이 이름은 한 번만 보고한다
                }
    }

    // ★★ `use <module>` — **이제 실제로 해석된다.** (모듈 링크: 여러 .low 가 하나의 단위)
    //   지금까지는 아무것과도 대조되지 않았고 W-NOT-YET 가 그렇게 말했다. 이제 말하지 않는다 —
    //   **검사하기 때문이다.** 선언된 모듈이 없으면 그 이름은 아무것도 가리키지 않는다.
    for (proven_size_t i = 0; i < pr->nforms; i++) {
        const low_cst_t *f = pr->forms[i]; ck_cur_form = f;
        if (f->kind != LOW_CST_FORM || f->nkids < 2 || f->kids[0]->kind != LOW_CST_ATOM) continue;
        if (f->kids[0]->tok.kw != LOW_KW_USE) continue;
        if (f->kids[1]->kind != LOW_CST_ATOM) continue;
        proven_u8str_view_t want = f->kids[1]->tok.lex;
        bool found = false;
        for (proven_size_t q = 0; q < pr->nforms && !found; q++) {
            const low_cst_t *m = pr->forms[q];
            if (m->kind != LOW_CST_FORM || m->nkids < 2 || m->kids[0]->kind != LOW_CST_ATOM) continue;
            if (m->kids[0]->tok.kw != LOW_KW_MODULE) continue;
            if (m->kids[1]->kind == LOW_CST_ATOM &&
                proven_u8str_view_eq(m->kids[1]->tok.lex, want)) found = true;
        }
        // ★ 이 단위 안에 있으면 **해석됐다** — 아무 말도 안 한다.
        //   없으면? **확인할 수 없다.** 모듈 탐색 경로가 없기 때문이다(표준 라이브러리도 없다).
        //   오류라고 부르면 거짓말이고(외부 모듈일 수 있다), 조용히 넘기면 그것도 거짓말이다.
        //   **확인할 수 없다는 사실을 말한다.** 그것이 정직한 세 번째 답이다.
        if (!found)
            warn(&out, "W-USE-EXTERNAL",
                 "this module is not in the compilation unit — there is no module search path, so "
                 "nothing here can confirm it exists. Pass the file that declares it and it WILL "
                 "be checked (several .low files link into one unit)", f->line);
    }

    // pass 1b: ★ 계약 절의 이름은 무언가를 가리켜야 한다 (PRINCIPLES.md §0 — 검사되지 않는 중복 금지)
    {
        const low_opinfo_t *tab0 = (const low_opinfo_t *)ops.data;
        for (proven_size_t i = 0; i < pr->nforms; i++) {
            const low_cst_t *f = pr->forms[i]; ck_cur_form = f;
            if (f->kind != LOW_CST_FORM || f->nkids < 2 || f->kids[0]->kind != LOW_CST_ATOM) continue;
            low_kw_t kw = f->kids[0]->tok.kw;
            if (kw != LOW_KW_FN && kw != LOW_KW_PROC) continue;
            ck_clause_names(&out, f, tab0, ops.len, ck_module_name(pr));
            ck_mref_slice(&out, f);        // ★ D0(c) — 서술자 갈아끼우기 창을 닫는다
            ck_parallel(&out, f);          // ★ RFC-0009 DET-1 — Bernstein 조건
            {                              // ★ match 의 완전성 · region 이름
                const low_cst_t *bd = (f->kids[f->nkids - 1]->kind == LOW_CST_BLOCK)
                                        ? f->kids[f->nkids - 1] : NULL;
                ck_match(&out, pr, bd, f);
                ck_regions(&out, f, bd);
            }
        }
    }

    for (proven_size_t i = 0; i < pr->nforms; i++) (void)ck_array_walk(&out, pr->forms[i]);
    for (proven_size_t i = 0; i < pr->nforms; i++) ck_lit_walk(&out, pr->forms[i]);   // RFC-0132 T2b-1
    ck_link_names(&out, pr);   // RFC-0134 N3

    // ★★ **지역이 최상위 이름을 가릴 수 없다** — 이름공간이 **평면**이기 때문이다.
    //
    //   E-NAME-DUP 는 최상위끼리만 봤다. 그런데 지역 변수가 op 이름을 가지면 VM 은 **조용히
    //   지역을 고른다** — 모듈 링크에서 `g` 가 남의 `f` 를 부른 것과 정확히 같은 죄다:
    //   **조용히 다른 이름을 고른다.** SPEC 은 가림이 없다고 말한다. 그러면 이것은 오류다.
    //
    //   (그리고 이것이 효과 전파를 성립시킨다: 원자가 op 이름이면 **그것은 그 op 다.**
    //    가림이 가능하면 그 추론이 깨진다 — 그래서 두 규칙은 한 몸이다.)
    {
        const low_opinfo_t *tab0 = (const low_opinfo_t *)ops.data;
        // ★★★★ **여기 한계가 있었다** (RFC-0077 P1-5 감사, 2026-08-14). 최상위 이름을 512 개까지만
        //   모으고 **넘치면 조용히 그만뒀다** — 그러면 그 뒤의 이름들은 가림(shadow) 검사를
        //   **안 받는다**. 검사가 줄어드는데 아무 소리도 안 난다.
        //   ⇒ 표를 **동적으로** 잡는다(이 함수는 `work` 얼로케이터를 이미 쓴다 — `ops`).
        //     한계를 없앨 수 있으면 없앤다: 닿을 수 없는 한계는 거짓말이고, 닿는 한계는 결함이다.
        proven_array_t topa = PROVEN_ARRAY_INIT(work, proven_u8str_view_t, 64).value;
        proven_array_t topma = PROVEN_ARRAY_INIT(work, proven_u8str_view_t, 64).value;
        // ★★★★★ **가림 통에 남의 모듈 이름까지 담고 있었다** (RFC-0093 후속, 2026-08-11).
        //   `top[]` 은 **단위 전체**에서 모인다 — 그리고 단위에는 `use` 로 끌어온 모듈들이
        //   함께 산다. 그래서 남의 모듈이 `slot` 을 선언하기만 하면 **내 지역 `var slot` 이
        //   E-NAME-SHADOW** 였다: 내 이름의 합법성이 **남이 무엇을 선언했는가**에 달렸다.
        //   ⇒ 라이브러리가 **평범한 지역 이름을 쓸 수 없었고**, 조합 게이트의 아픈 쌍
        //     상당수가 이것이다(`hashmap` 의 `var slot`, `growvec`+`strbuf` …).
        //   ★ 규칙 자체는 옳다(평평한 이름공간, 가림 금지). **범위가 틀렸다**: 남의 모듈
        //     이름은 **언제나 한정으로만** 부른다(`m.slot` — RFC-0011 qualified-by-default,
        //     글롭 import 없음). 한정으로만 닿는 이름은 맨이름 지역이 **가릴 수가 없다** —
        //     가릴 수 없는 것을 가린다고 고발하면 그것은 규칙이 아니라 사고다.
        //   ⇒ **제 모듈의 최상위 이름만** 센다. 이것은 위 `ck_vis_walk` 의 *"자기 모듈 먼저"*
        //     와 같은 교정이다 — 같은 실수가 **이웃 자리에서 한 번 더** 있었다.
        proven_u8str_view_t tcur = { 0 };
        for (proven_size_t i = 0; i < pr->nforms; i++) {
            const low_cst_t *f = pr->forms[i]; ck_cur_form = f;
            if (f->kind != LOW_CST_FORM || f->nkids < 2 || f->kids[0]->kind != LOW_CST_ATOM) continue;
            low_kw_t kw = f->kids[0]->tok.kw;
            if (kw == LOW_KW_MODULE && ck_atom(f->kids[1])) { tcur = f->kids[1]->tok.lex; continue; }
            // ★ **모듈 `let` = 이름 붙은 상수**도 최상위 이름이다. 이걸 안 넣어서 지역이
            //   모듈 상수를 **조용히 가렸다** — 그리고 상수를 가리는 것은 op 을 가리는 것보다
            //   더 위험하다: **같은 이름이 두 값을 갖는데 아무도 말하지 않는다.**
            // ★★★ **N2(타입)는 N1(값)과 다른 이름공간이다** (RFC-0060 §5 R1·R2).
            //   타입 이름(type·struct·enum)은 **타입 자리**에만 산다. 파라미터·지역은 **값 자리**다.
            //   자리가 겹칠 수 없으므로 뜻이 유일하다 — 그래서 `input path path .` 은 통과해야 한다
            //   (파라미터 `path` : 타입 `path`). RFC-0060 이 든 예: `var point` 이 `struct point` 과 공존.
            //   ⇒ 이 가림 통에는 **N1 값**만 넣는다: op(calc/proc)·모듈 상수(let). 타입은 빼자.
            //   (E-NAME-DUP 의 최상위 통은 여전히 module·type·op 를 한 데 묶어 `Type.op` 경로를
            //    유일하게 지킨다 — RFC-0062. 그건 최상위 선언 유일성이고, 함수-지역 값 슬롯과 무관하다.)
            if (kw != LOW_KW_FN && kw != LOW_KW_PROC && kw != LOW_KW_LET) continue;
            if (f->kids[1]->kind != LOW_CST_ATOM) continue;
            (void)PROVEN_ARRAY_PUSH(&topma, proven_u8str_view_t, tcur);
            (void)PROVEN_ARRAY_PUSH(&topa, proven_u8str_view_t, f->kids[1]->tok.lex);
        }
        // ★ **op 을 폼에서 직접 훑는다** — 그러면 그 op 의 모듈을 바로 알 수 있다.
        //   처음엔 `ops` 표를 돌며 폼을 되찾으려 했는데 `body` 는 블록이라 폼과 안 맞았고,
        //   그 결과 **모듈 목록이 비어** 같은 모듈 안 가림까지 통과했다(실측). 규칙을
        //   좁히려다 **꺼 버릴 뻔했다** — 좁힐 때는 *"원래 물던 것이 아직 무는가"* 를 반드시 쟀다.
        {
            proven_u8str_view_t scan = { 0 };
            // ★★★★★ **버려진 제네릭 틀까지 훑는다** (2026-08-25).
            //   단형화는 *"틀 자신은 빠진다"* 로 폼 목록을 다시 쓴다. 인스턴스가
            //   하나라도 있으면 그 자리에 인스턴스가 들어가지만, **아무도 부르지
            //   않은 제네릭**은 아무것도 안 들어가 폼이 사라졌다 — 그리고 사라진
            //   폼은 **검사도 안 받았다**: 같은 몸이 호출되면 `E-FIELD-GLUED` 를
            //   물고 안 부르면 `check: ok` 였다(실측). ★ 안 보는 자리는 통과하는
            //   자리와 구별되지 않는다 — 같은 실수가 **다른 층에서 한 번 더**.
            //   ⇒ 이름·접근 검사(타입을 몰라도 되는 것)만 `gforms` 도 돈다.
            for (proven_size_t q = 0; q < pr->nforms + pr->ngforms; q++) {
                const low_cst_t *f = q < pr->nforms ? pr->forms[q]
                                                    : pr->gforms[q - pr->nforms];
                if (f->kind != LOW_CST_FORM || !f->nkids || f->kids[0]->kind != LOW_CST_ATOM) continue;
                low_kw_t fk = f->kids[0]->tok.kw;
                if (fk == LOW_KW_MODULE && f->nkids >= 2 && ck_atom(f->kids[1]))
                    { scan = f->kids[1]->tok.lex; continue; }
                if (fk != LOW_KW_FN && fk != LOW_KW_PROC &&
                    fk != LOW_KW_ACTOR && fk != LOW_KW_TRAIT) continue;   // ★ 액터 안까지 본다
                // ★ 같은 이유로 여기도 한계를 없앤다 — 위 표에서 이 모듈 몫만 추린다.
                proven_array_t minea = PROVEN_ARRAY_INIT(work, proven_u8str_view_t, 32).value;
                for (proven_size_t t = 0; t < topa.len; t++)
                    if (proven_u8str_view_eq(((proven_u8str_view_t *)topma.data)[t], scan))
                        (void)PROVEN_ARRAY_PUSH(&minea, proven_u8str_view_t,
                                                ((proven_u8str_view_t *)topa.data)[t]);
                ck_op_names_deep(&out, f, (const proven_u8str_view_t *)minea.data, minea.len);
            }
        }
        for (proven_size_t i = 0; i < ops.len; i++)
            ck_head_claims(&out, tab0[i].body, tab0, ops.len);
        ck_trait_satisfy(&out, pr);   // ★★★ trait 의 **구조적 충족을 실제로 검사한다**
        ck_trait_bound(&out, pr);     // ★★★ 제네릭의 **경계**를 강제한다 (RFC-0021 §6.3)
        // (가시성·no-shadow 는 narrow 전에 이미 돌았다 — 위 ck_removed_words 직후.)
        ck_entry(&out, pr);           // ★★★ **엔트리 포인트** — 출력이 곧 종료 상태 (RFC-0030)
        ck_unsafe(&out, pr);          // ★★★ **`unsafe` 규율** — 수식자가 장식이었다
        ck_comptime(&out, pr);        // ★★★ **comptime 값 인자는 컴파일타임 상수여야 한다**
        ck_tier(&out, pr);            // ★★★ **계층 × 효과 게이팅** (RFC-0039 D4)
        ck_alloc_cap(&out, pr);       // ★★★ **할당 = 명시 권한** (RFC-0043 D1)
        ck_atomic_cap(&out, pr);      // ★ 2026-09-15 — atomic 도 권한 효과다
        ck_io_cap(&out, pr);          // ★★★ **io 도 권한이다** (RFC-0007 §6.7 LINK — cap-effect)
        ck_float_target(&out, pr);    // ★★★ **no_float 타깃엔 float 이 없다** (RFC-0038 D5)
        ck_vec_value(&out, pr);       // ★★★ **벡터/마스크엔 소유·빌림 수식자가 못 붙는다** (RFC-0040)
        ck_target_intrin(&out, pr);   // ★★★ **unsafe target <iset> — intrinsic 격리 게이트** (RFC-0040 D5)
        ck_profile(&out, pr);         // ★★★ **profile 이 동시성 상한을 게이팅한다** (RFC-0009 §105)
        ck_map_sink(&out, pr);        // ★★★ **map/filter 의 싱크는 mut slice 여야 한다** (RFC-0016)
        ck_mmio(&out, pr);            // ★★★ **MMIO 도 권한이다** (RFC-0042 D2/D7)
        ck_reserve(&out, pr);         // ★★★ **예약된 정적 블록도 권한이다** (RFC-0039 §9-2)
        ck_asm(&out, pr);             // ★★★ **인라인 asm = 검증 불가한 탈출구 ⇒ 격리** (RFC-0041)
        ck_option(&out, pr);          // ★★★ **아무도 안 읽는 손잡이는 거짓말이다** (RFC-0036 D5)
        ck_ffi(&out, pr);             // ★★★ **C ABI/FFI — 계약이 경계를 지킨다** (RFC-0063)
        ck_requires_unsat(&out, pr);  // ★ 함께 참일 수 없는 전제 (#47)
        ck_export_surface(&out, pr);  // ★ 내보낸 서명에 감춘 타입 (#55)
        ck_typeargs(&out, pr);        // ★ 제네릭 op 의 앞자리 타입 인자 (#57)
        ck_col0(&out, pr);            // ★★★ **안전지대 앵커 — 최상위는 열 0** (RFC-0065)
        ck_isr(&out, pr);             // ★★★ **ISR — 하드웨어가 부르는 진입** (RFC-0042 D5)

        // ★ **파라미터도 이름이다** — 그리고 같은 구멍이었다.
        //   `fn f input len u8 .` 이 통과했다. 그 op 안에서 `len` 은 이제 두 뜻이고,
        //   어느 쪽이냐가 **문장이 어떻게 괄호 쳐지는지**를 정한다(RFC-0046 P1).
        proven_u8str_view_t pcur = { 0 };
        for (proven_size_t i = 0; i < pr->nforms; i++) {
            const low_cst_t *f = pr->forms[i]; ck_cur_form = f;
            if (f->kind != LOW_CST_FORM || !f->nkids || f->kids[0]->kind != LOW_CST_ATOM) continue;
            low_kw_t kw = f->kids[0]->tok.kw;
            // ★★★ **여기도 같은 자리였다** — 지역(`ck_no_shadow`)은 모듈로 좁혔는데
            //   **매개변수**는 여전히 단위 전체 이름을 보고 있었다. 오늘 세 번째다:
            //   한 자리를 고치면 **이웃을 물어라**(RFC-0093 §1).
            if (kw == LOW_KW_MODULE && f->nkids >= 2 && ck_atom(f->kids[1]))
                { pcur = f->kids[1]->tok.lex; continue; }
            if (kw != LOW_KW_FN && kw != LOW_KW_PROC) continue;
            low_op_header_t h = low_op_header(f);
            for (proven_size_t q = 0; q < h.np; q++) {
                ck_plain_name(&out, h.p[q].name, "parameter", f->line);   // ★ 파라미터도 맨 이름
                if (low_ir_is_builtin_name(h.p[q].name) || ck_effect_word(h.p[q].name))
                    emit(&out, "E-NAME-BUILTIN",
                         "a PARAMETER takes the name of a builtin op — inside this op the name now "
                         "means two things, and which one it means decides how the sentence is "
                         "bracketed (RFC-0046 P1). Rename the parameter", f->line);
                for (proven_size_t t = 0; t < topa.len; t++)
                    if (proven_u8str_view_eq(((proven_u8str_view_t *)topma.data)[t], pcur) &&
                        proven_u8str_view_eq(((proven_u8str_view_t *)topa.data)[t], h.p[q].name))
                        emit(&out, "E-NAME-SHADOW",
                             "a PARAMETER takes the name of a top-level declaration — the namespace "
                             "is FLAT (no shadowing). Rename the parameter", f->line);
            }
        }
    }

    // ★★ **말없이 무시되던 최상위 선언들** — `test` · `actor` · `contract`.
    //
    //   셋 다 문법에 있다(SPEC-002 §215 · §266). 셋 다 **조용히 통과했다.**
    //   그리고 셋 다 **아무 일도 하지 않는다**:
    //     · `test N do … end` — **한 번도 실행되지 않는다.** 테스트를 썼는데 안 돌아간다.
    //       그것이 가장 나쁜 거짓말이다: 초록불이 **검사됐다는 뜻이 아니다.**
    //     · `actor N do … end` — 안에 든 op 들이 **IR 에서 통째로 사라진다**(0 defs).
    //       수식자 버그와 같은 계열이다. level-2 액터는 아직 없다.
    //     · `contract N do … end` — 명명 계약. 아무 op 에도 걸리지 않는다.
    //
    //   구현이 없으면 **없다고 말한다.** 조용히 통과시키는 것이 죄다(PRINCIPLES.md §0 교훈 2:
    //   "미구현은 오류가 아니라 **고지**여야 한다 — 그러나 **조용히 무시하면 안 된다**").
    for (proven_size_t i = 0; i < pr->nforms; i++) {
        const low_cst_t *f = pr->forms[i]; ck_cur_form = f;
        if (f->kind != LOW_CST_FORM || f->nkids < 1 || f->kids[0]->kind != LOW_CST_ATOM) continue;
        proven_u8str_view_t h = f->kids[0]->tok.lex;
        if (!veq(h, "test")) ck_expect_place_walk(&out, f);   // ★ X-0072 ① — 시험 밖의 expect
        // ★ `test` 는 이제 **실행된다**(`--test`). 그래서 W-NOT-YET 를 걷었다 —
        //   미구현이 아니게 되면 **고지도 걷어야 한다.** 남겨 두면 그것도 거짓말이다.
        //   다만 `--check` 는 테스트를 **돌리지 않는다.** 그 사실은 말해 준다:
        //   초록불이 "테스트가 통과했다" 는 뜻이 아니다.
        if (veq(h, "test")) {
            warn(&out, "W-TEST-NOT-RUN",
                 "this unit has `test` blocks, and `--check` does NOT run them — a green check "
                 "means the code type-checks, NOT that the tests pass. Run `--test`",
                 f->kids[0]->tok.line);
            // ★ 같은 이름의 시험 둘 · 시험과 op 의 같은 이름은 위의 E-NAME-DUP 한 곳이 잡는다(X-0071 · X-0072).
        }
        else if (veq(h, "actor")) {
            // ★ actor 는 이제 **컴파일된다**(상태 격리 + `on` 핸들러 + spawn/send).
            //   그래서 "안의 op 이 사라진다" 는 고지는 **걷었다** — 거짓이 되면 걷어야 한다.
            //   남은 것만 말한다: **mailbox 와 restart 정책**은 아직 강제하지 않는다.
            //   (그리고 지금은 **순차 실행**이다 — 한 번에 한 메시지. 그것이 actor 모델의
            //    보장이고, LowentDRF.v 가 그 규율이면 경합이 없다고 증명한 그것이다. 동시성은
            //    최적화이지 의미가 아니다.)
            for (proven_size_t j = 0; j < f->nkids; j++) {
                const low_cst_t *b = f->kids[j];
                if (b->kind != LOW_CST_BLOCK) continue;
                for (proven_size_t q = 0; q < b->nkids; q++) {
                    const low_cst_t *cl = b->kids[q];
                    if (cl->kind != LOW_CST_FORM || !cl->nkids || cl->kids[0]->kind != LOW_CST_ATOM) continue;
                    proven_u8str_view_t w = cl->kids[0]->tok.lex;
                    // ★★★ **`mailbox` 는 이제 고지가 없다** (2026-07-17) — 교훈 2대로 걷었다.
                    //   `bounded N` 은 완전히 실재하고(capacity: 재진입 in-flight + async 큐, 초과 트랩 ·
                    //   `try spawn send` 로 회복 가능한 backpressure), `unbounded` 는 **거절**된다(low_ir.c —
                    //   고정 풀은 진짜로 못 자란다). 둘 다 W-NOT-YET 이 아니다: 하나는 실재, 하나는 오류.
                    //   단일 스레드 배달은 미구현이 아니라 **설계**다(LowentDRF.v: 동시성은 최적화, 의미 아님).
                    if (veq(w, "failure")) {
                        // ★ restart 슈퍼비전은 이제 **강제된다**(핸들러 panic → 상태 초기화 후 재시도,
                        //   소진하면 escalate). 남은 건 `within <D>` 창뿐 — 결정적 클럭이 없어 그 창은
                        //   아직 강제 못 한다. 그래서 `within` 이 있을 때만 정직하게 고지한다.
                        bool has_within = false;
                        for (proven_size_t q2 = 0; q2 < cl->nkids; q2++)
                            if (cl->kids[q2]->kind == LOW_CST_ATOM && veq(cl->kids[q2]->tok.lex, "within"))
                                has_within = true;
                        if (has_within)
                            warn(&out, "W-NOT-YET",
                                 "the actor's `failure restart` policy IS now enforced (a handler `panic` "
                                 "reinitializes the instance's state and retries, up to `max N`, then "
                                 "escalates — RFC-0019 §6.5). But the `within <D>` WINDOW is only recorded, "
                                 "not enforced: rate-limiting restarts over time needs a deterministic clock, "
                                 "which the level-2 model does not have yet. The `max N` count is enforced; "
                                 "the time window is not",
                                 cl->kids[0]->tok.line);
                    }
                }
            }
        }
        else if (veq(h, "build")) {
            // ★★★ `build` 는 이제 **두 축**을 갖는다 (RFC-0013: target × mode × profile):
            //     build <mode> .        계약 검사의 처분   (RFC-0008 §6.5)
            //     build tier <t> .      하드웨어 규모      (RFC-0039 D4)
            //   갈라 읽지 않으면 `build tier t3 .` 이 **"모르는 모드"** 가 된다 — 오진이다.
            if (f->nkids >= 2 && f->kids[1]->kind == LOW_CST_ATOM &&
                veq(f->kids[1]->tok.lex, "tier")) { /* ck_tier 가 본다 */ }
            // ★ `build option …` 은 **모드가 아니라 손잡이의 선언**이다(RFC-0036 D5).
            //   갈라 읽지 않으면 그것이 **"모르는 모드"** 가 된다 — 또 하나의 오진(교훈 5).
            else if (f->nkids >= 2 && f->kids[1]->kind == LOW_CST_ATOM &&
                veq(f->kids[1]->tok.lex, "option")) { /* ck_option 이 본다 */ }
            // ★★★ **`build profile <p>` — 세 번째 축**(RFC-0009 §105 · RFC-0011 §13). 갈라 읽지 않으면
            //   profile 선언이 **"모르는 모드"** 로 오진된다(tier·option 과 같은 병). ck_profile 이 본다.
            else if (f->nkids >= 2 && f->kids[1]->kind == LOW_CST_ATOM &&
                veq(f->kids[1]->tok.lex, "profile")) { /* ck_profile 이 본다 */ }
            // ★★★★★ RFC-0120 §5.2-10 — `build absorb <모듈> .` 은 **흡수를 허락한 모듈**이다.
            //   매니페스트의 열쇠말이고 모드가 아니다. 갈라 읽지 않으면 «모르는 모드» 로 오진된다
            //   (tier·option·profile 과 같은 병 — 교훈 5).
            else if (f->nkids >= 2 && f->kids[1]->kind == LOW_CST_ATOM &&
                veq(f->kids[1]->tok.lex, "absorb")) { /* 매니페스트가 읽는다 — low_pkg.c */ }
            // ★ `build entry`·`build target` 도 매니페스트 열쇠말이다 — 같은 까닭으로 가른다.
            else if (f->nkids >= 2 && f->kids[1]->kind == LOW_CST_ATOM &&
                (veq(f->kids[1]->tok.lex, "entry") || veq(f->kids[1]->tok.lex, "target"))) { /* 매니페스트가 읽는다 */ }
            else
            // ★★★ **모드 어휘는 닫혀 있다** (RFC-0008 §6.5 · SPEC-005 §33).
            //   전엔 이 선언이 **파싱만 되고 아무 데서도 강제되지 않았다** — 즉 무엇을 적든
            //   **아무 일도 안 일어나면서** 무언가 달라진다고 믿게 뒀다.
            //   이제 모드가 **어떤 등급의 계약 검사가 런타임에 남는지**를 실제로 고른다.
            if (f->nkids < 2 || f->kids[1]->kind != LOW_CST_ATOM ||
                !(veq(f->kids[1]->tok.lex, "debug") || veq(f->kids[1]->tok.lex, "test") ||
                  veq(f->kids[1]->tok.lex, "release_safe") ||
                  veq(f->kids[1]->tok.lex, "release_fast") ||
                  veq(f->kids[1]->tok.lex, "audit")))
                emit(&out, "E-BUILD-MODE",
                     "the build mode is a CLOSED set: debug · test · release_safe · release_fast · "
                     "audit (RFC-0008 §6.5). The mode chooses WHICH CONTRACT CHECKS SURVIVE at "
                     "runtime — an unknown mode would silently mean `debug`, and you would believe "
                     "you had chosen something",
                     f->kids[0]->tok.line);
        }
        else if (veq(h, "contract") && f->nkids >= 2 && f->kids[1]->kind == LOW_CST_ATOM) {
            // ★ 명명 계약은 **이제 강제된다** — `satisfies N` 이라 적은 op 의 진입 검사가 된다.
            //   그러니 고지는 **아무도 안 건 계약**에만 붙인다. 걸린 계약에 "강제 안 된다" 고
            //   말하면 그것이 거짓말이다(구현하고서 고지를 안 걷는 것 — 이번 세션에 두 번 겪었다).
            bool bound = false;
            for (proven_size_t q = 0; q < pr->nforms && !bound; q++) {
                const low_cst_t *o = pr->forms[q];
                if (o->kind != LOW_CST_FORM || o->kids[0]->kind != LOW_CST_ATOM) continue;
                low_kw_t ok2 = o->kids[0]->tok.kw;
                if (ok2 != LOW_KW_FN && ok2 != LOW_KW_PROC) continue;
                for (proven_size_t r = 0; r + 1 < o->nkids && !bound; r++)
                    if (o->kids[r]->kind == LOW_CST_ATOM && o->kids[r]->tok.kw == LOW_KW_SATISFIES &&
                        o->kids[r + 1]->kind == LOW_CST_ATOM &&
                        proven_u8str_view_eq(o->kids[r + 1]->tok.lex, f->kids[1]->tok.lex))
                        bound = true;
            }
            if (!bound)
                warn(&out, "W-UNBOUND",
                     "this named `contract` is declared but NO op says `satisfies` it — it "
                     "constrains nothing. (A contract that IS satisfied is enforced: its "
                     "`requires` become that op's entry checks.)",
                     f->kids[0]->tok.line);
        }
    }

    // ★★★ RFC-0054(`machine.*` 타깃 조건부 컴파일)는 **보류**다(DECISION-0008).
    //   사유: §8 미해결 5건 중 **`comptime` 자체가 미구현**(RFC-0015)이고, 본 RFC 는 그 위에 선다.
    //
    //   ★ 그런데 **구현은 이미 있다.** 그러면 도구가 **명세에 없는 것을 주는 셈**이다 —
    //     그리고 이 프로젝트가 금지하는 것이 바로 그것이다(PRINCIPLES.md §0: 도구는 검사한 것만
    //     주장해야 한다). **말없이 주면 그것도 거짓말이다.**
    //   ⇒ 쓸 수는 있다. 그러나 **명세가 보증하지 않는다는 것을 말한다.**
    for (proven_size_t i = 0; i < pr->nforms; i++) {
        const low_cst_t *f = pr->forms[i]; ck_cur_form = f;
        if (f->kind != LOW_CST_FORM || f->nkids < 2 || f->kids[0]->kind != LOW_CST_ATOM) continue;
        low_kw_t kw = f->kids[0]->tok.kw;
        if (kw != LOW_KW_FN && kw != LOW_KW_PROC) continue;
        bool hit = false;
        for (proven_size_t j = 0; j < f->nkids && !hit; j++)
            hit = ck_uses_machine(f->kids[j]);
        if (hit)
            warn(&out, "W-RFC-PENDING",
                 "`machine.*` comes from RFC-0054 (target-conditional compilation), which is "
                 "ON HOLD (보류, DECISION-0008): its §8 rests on `comptime`, and comptime "
                 "itself is not implemented (RFC-0015). It WORKS — the branches are checked, not "
                 "#ifdef'd away — but **the spec does not sanction it yet**. The tool says so "
                 "rather than hand you a feature the spec has never seen",
                 f->kids[0]->tok.line);
    }

    // ★ SPEC-008 §74 의 **공유 상태 타입** — `shared_read t` · `lock t` · `rwlock t` · `atomic t`.
    //   어휘에 아예 없어서 **"E-TYPE-UNDEF: 없는 타입"** 으로 거절됐다 — *네 프로그램이 틀렸다.*
    //   명세에 있는 타입인데. 이제 이름은 받는다. 그러나 **의미(level-3 규율·RC11 ordering)는
    //   아직 없다** — 그것을 말한다. 조용히 통과시키면 "공유했으니 안전하다" 는 거짓말이 된다.
    for (proven_size_t i = 0; i < pr->nforms; i++) {
        const low_cst_t *f = pr->forms[i]; ck_cur_form = f;
        if (f->kind != LOW_CST_FORM || f->nkids < 2 || f->kids[0]->kind != LOW_CST_ATOM) continue;
        low_kw_t kw = f->kids[0]->tok.kw;
        if (kw != LOW_KW_FN && kw != LOW_KW_PROC) continue;
        for (proven_size_t j = 0; j < f->nkids; j++) {
            if (f->kids[j]->kind != LOW_CST_ATOM) continue;
            proven_u8str_view_t w = f->kids[j]->tok.lex;
            if (!veq(w, "shared_read") && !veq(w, "lock") && !veq(w, "rwlock") && !veq(w, "atomic"))
                continue;
            // ★ `access s shared_read .` 의 **모드**는 타입이 아니다 — 그것은 이미 강제된다.
            //   (모양: [access, <param>, <mode>] — 두 칸 앞이 `access` 면 모드다.)
            if (j >= 2 && f->kids[j - 2]->kind == LOW_CST_ATOM &&
                veq(f->kids[j - 2]->tok.lex, "access")) continue;
            // ★★★ `cap atomic` (능력) · `effects atomic` (효과) 의 `atomic` 은 **타입이 아니다.**
            //   앞 낱말이 `cap`/`effects`/`errors`… 면 그건 능력·효과 자리다 — level-3 op 은
            //   이제 **구현됐으니**(RFC-0018) 그 자리에 경고하면 도구가 자기 능력을 과소하게 말한다.
            if (j >= 1 && f->kids[j - 1]->kind == LOW_CST_ATOM) {
                proven_u8str_view_t prev = f->kids[j - 1]->tok.lex;
                if (veq(prev, "cap") || veq(prev, "effects") || veq(w, "atomic"))
                    // atomic 은 op(atomic_*)로 쓴다 — 타입 `atomic t` 는 아직 안 쓴다. 조용히 넘긴다.
                    if (veq(w, "atomic")) continue;
            }
            // ★★★ **거절한다 — 경고가 아니다** (2026-07-20).
            //
            //   `input s lock u64 .` 는 읽는 사람에게 *"이 상태는 락으로 보호된다"* 고 말한다.
            //   **아무도 그것을 검사하지 않는다** — 락은 걸리지 않는다. ⇒ 검사되지 않는
            //   안전 주장이고, 그것이 §0 이 말하는 거짓말이다.
            //
            //   ★★★ 그리고 여기엔 **더 강한 이유**가 있다: 지금 런타임은 **협조적이고 OS
            //     스레드가 하나**이며 액터 전달이 **순차**다(SPEC-008 §74 가 그렇게 적는다).
            //     ⇒ **락이 막을 대상이 아예 없다.** 이 타입은 "아직 구현 안 된 것" 이 아니라
            //     **지금은 뜻이 없는 것**이고, 쓰면 오해만 만든다. 그리고 겨룰 상대가 없으니
            //     구현해도 **시험할 수가 없다** — 시험 못 하는 안전 기계는 장식이다.
            //
            //   ★★ 같은 판단을 이 저장소가 이미 여러 번 했다: `mailbox unbounded`(진짜로 못
            //     자라는 것을 무한이라 부르지 않는다) · `bounded 0` · `cap teleporter` ·
            //     `parallel` 에 쪼갤 루프가 없을 때(E-PAR-NOLOOP, 같은 날).
            //   ☞ `atomic` 은 **다르다** — memory ordering 이 **진짜로 방출된다**(RFC-0018 §6.1).
            //     그래서 `atomic` 은 위에서 통과시킨다. **되는 것과 안 되는 것을 가른다.**
            emit(&out, "E-LOCK-NOTYET",
                 "`lock t` / `rwlock t` / `shared_read t` are level-3 shared-state types that "
                 "SPEC-008 §74 names but this compiler DOES NOT IMPLEMENT: no lock is taken, no "
                 "release happens. Writing one tells every reader the state is lock-protected while "
                 "NOTHING checks that — an unchecked safety claim is the lie PRINCIPLES.md §0 is about. "
                 "★ And today there is nothing for a lock to protect against: execution is "
                 "cooperative on ONE OS thread and actor delivery is SEQUENTIAL, so such a lock "
                 "could never even be exercised. This is refused for the same reason as "
                 "`mailbox unbounded` and `bounded 0`. Use an `actor` (sequential delivery IS the "
                 "mutual exclusion) or the `atomic_*` ops, whose memory ordering IS real (RFC-0018 "
                 "§6.1). These types come back when real OS threads do",
                 f->kids[j]->tok.line);
            break;
        }
    }

    // ★★ `satisfies` — **없는 trait 를 충족한다고 주장해도 통과했다.**
    //
    //   SPEC-003 §60: "인터페이스 = trait(구조적 충족) … `satisfies` 로 충족 확인(선택)."
    //   그런데 `trait`·`satisfies` 는 **어휘에 아예 없었다.** `satisfies nonexistent_trait .` 이
    //   조용히 통과했다 — **아무것도 가리키지 않는 주장**이다. 정확히 "검사되지 않는 중복" 이다.
    //
    //   구조적 충족(op 집합이 trait 의 요구를 만족하는가)은 **아직 구현하지 않았다.**
    //   그러면 정직한 답은 셋이다:
    //     · 이름은 **강제한다** — 없는 trait 는 오류다(E-TRAIT-UNDEF). 이건 지금 할 수 있다.
    //     · 충족 검사는 **못 한다고 말한다**(W-NOT-YET). 조용히 통과시키지 않는다.
    //     · 둘을 섞지 않는다: *프로그램이 틀렸다* 와 *도구가 아직 못 한다* 는 다른 말이다.
    {
        proven_u8str_view_t traits[128]; proven_size_t ntr = 0;
        for (proven_size_t i = 0; i < pr->nforms && ntr < 128; i++) {
            const low_cst_t *f = pr->forms[i]; ck_cur_form = f;
            if (f->kind != LOW_CST_FORM || f->nkids < 2 || f->kids[0]->kind != LOW_CST_ATOM) continue;
            // ★ `satisfies` 는 **trait 또는 명명 계약**을 가리킨다(SPEC-002 §267).
            if ((f->kids[0]->tok.kw != LOW_KW_TRAIT && f->kids[0]->tok.kw != LOW_KW_CONTRACT) ||
                f->kids[1]->kind != LOW_CST_ATOM) continue;
            traits[ntr++] = f->kids[1]->tok.lex;
        }
        for (proven_size_t i = 0; i < pr->nforms; i++) {
            const low_cst_t *f = pr->forms[i]; ck_cur_form = f;
            if (f->kind != LOW_CST_FORM || f->nkids < 2 || f->kids[0]->kind != LOW_CST_ATOM) continue;
            low_kw_t kw = f->kids[0]->tok.kw;
            if (kw != LOW_KW_FN && kw != LOW_KW_PROC) continue;
            for (proven_size_t j = 2; j + 1 < f->nkids; j++) {
                if (f->kids[j]->kind != LOW_CST_ATOM ||
                    f->kids[j]->tok.kw != LOW_KW_SATISFIES) continue;
                for (proven_size_t r = j + 1; r < f->nkids && f->kids[r]->kind == LOW_CST_ATOM &&
                     f->kids[r]->tok.kw == LOW_KW_NONE &&
                     !ck_clause_word(f->kids[r]->tok.lex); r++) {   // ★ 절 끝에서 멈춘다
                    bool known = false;
                    for (proven_size_t t = 0; t < ntr && !known; t++)
                        if (proven_u8str_view_eq(traits[t], f->kids[r]->tok.lex)) known = true;
                    if (!known) {
                        emit(&out, "E-TRAIT-UNDEF",
                             "`satisfies` names a trait that is not declared — a claim that points "
                             "at nothing checks nothing",
                             f->kids[r]->tok.line);
                    } else if (!ck_is_contract(pr, f->kids[r]->tok.lex)) {
                        // ★★★ **op 이 trait 을 충족한다는 주장은 이제 문법이 아니다.**
                        //   충족하는 것은 **타입**이다(`struct S satisfies T .`) — 그리고 그것은
                        //   **실제로 검사된다**(ck_trait_satisfy). RFC-0062 가 타입에 op 을
                        //   붙여 준 순간, "op 하나가 trait 을 충족한다" 는 말은 **뜻을 잃었다.**
                        emit(&out, "E-TRAIT-RECV",
                             "an OP does not satisfy a trait — a TYPE does. Write `satisfies <trait> .` "
                             "inside the struct, and declare the trait's ops as `fn <Type>.<name>` "
                             "(RFC-0062). The satisfaction is then CHECKED, not merely claimed",
                             f->kids[r]->tok.line);
                    }
                }
            }
        }
    }

    // ★★★ RFC-0057 (채택 2026-07-13) — **관측적 순수성**.
    //
    //   `fn` 은 **강제된 순수 계약**이다(SPEC-003 §27) — "호출자가 의존하는 보증".
    //   그런데 **호출자의 저장소를 고쳐 쓰면서** `effects none` 으로 통과하고 있었다:
    //
    //       fn w input s mut slice u8 . . output u64 . effects none .
    //       do set (index s 0) 9 . return len s . end        ← 호출자의 버퍼가 바뀐다
    //
    //   그런 op 은 **메모이즈할 수도, 재배치할 수도 없다.** 순수하지 않다.
    //   ⇒ **선언은 있고 강제는 없었다** — 교훈 1 그대로다. (픽스처 셋이 실제로 그러고 있었다.)
    //
    //   ★ 그러나 **지역 변이는 순수하다**(RFC-0057 옵션 B 기각): 저장소가 op 안에 **갇혀
    //     있으면** 밖에서 관측할 수 없다. 누산기를 쓴다고 proc 이 될 이유는 없다.
    //     **기계의 쓰기와 관측 가능한 효과는 다른 것이다.**
    //   ⇒ 가르는 선은 **호출자에게 보이는가**: `mut` 로 선언된 파라미터에 쓰는가.
    //   ★★★ 그런데 이 검사는 **최상위 form 만** 훑고 있었다. actor 핸들러는 **블록 안**에 있어서
    //     **한 번도 안 봤다.** 그리고 `on` 이 calc/proc 비트를 우회했으므로, **액터 상태를 고쳐
    //     쓰는 핸들러가 순수(calc)로 취급되고 아무 말도 안 들었다.** `on` 을 없애 1비트는
    //     돌아왔지만, **그 자리를 보는 눈이 없으면 1비트도 거짓말이 된다.**
    //   ⇒ 액터 안까지 본다. 그리고 `fn` 핸들러가 **상태 필드에 쓰면** 그것도 순수의 거짓말이다:
    //     상태는 **인스턴스의 것**이고, 그 변화는 **다음 메시지에게 보인다.**
    for (proven_size_t i = 0; i < pr->nforms; i++) {
        const low_cst_t *top = pr->forms[i];
        if (top->kind != LOW_CST_FORM || !top->nkids || top->kids[0]->kind != LOW_CST_ATOM) continue;
        bool is_actor = (top->kids[0]->tok.kw == LOW_KW_ACTOR);
        const low_cst_t *ablk = (is_actor && top->kids[top->nkids - 1]->kind == LOW_CST_BLOCK)
                                  ? top->kids[top->nkids - 1] : NULL;
        // 이 actor 의 상태 필드 이름들
        proven_u8str_view_t sf[32]; proven_size_t nsf = 0;
        if (ablk)
            for (proven_size_t j = 0; j < ablk->nkids; j++) {
                const low_cst_t *st = ablk->kids[j];
                if (st->kind != LOW_CST_FORM || !st->nkids || st->kids[0]->kind != LOW_CST_ATOM) continue;
                if (st->kids[0]->tok.kw != LOW_KW_STATE) continue;
                const low_cst_t *fb = st->kids[st->nkids - 1];
                if (fb->kind != LOW_CST_BLOCK) continue;
                for (proven_size_t q = 0; q < fb->nkids && nsf < 32; q++)
                    if (fb->kids[q]->kind == LOW_CST_FORM && fb->kids[q]->nkids &&
                        fb->kids[q]->kids[0]->kind == LOW_CST_ATOM)
                        sf[nsf++] = fb->kids[q]->kids[0]->tok.lex;
            }
        // 최상위 op 하나, 또는 actor 안의 핸들러들
        proven_size_t nscan = ablk ? ablk->nkids : 1;
        for (proven_size_t z = 0; z < nscan; z++) {
            const low_cst_t *f = ablk ? ablk->kids[z] : top;
            if (f->kind != LOW_CST_FORM || f->nkids < 2 || f->kids[0]->kind != LOW_CST_ATOM) continue;
            if (f->kids[0]->tok.kw != LOW_KW_FN) continue;      // proc 은 그래도 된다
            low_op_header_t h = low_op_header(f);                   // ★ 머리는 한 번만 읽는다
            if (!h.body) continue;

            // ① 액터 상태에 쓰는가 — **다음 메시지에게 보인다.**
            bool wrote_state = false;
            for (proven_size_t q = 0; q < nsf; q++)
                if (ck_writes_name(h.body, sf[q]) || ck_writes_place(h.body, sf[q]))
                    wrote_state = true;
            if (wrote_state) {
                emit(&out, "E-EFFECT-PURITY",
                     "a `fn` message handler WRITES the actor's STATE — and that write is "
                     "visible to the NEXT message. A fn is an enforced purity contract "
                     "(SPEC-003 §27): callers may memoise, reorder or elide it. Declare it a "
                     "`proc` (and say `effects state`). "
                     "★ Nobody used to look here: handlers were declared with `on`, which said "
                     "NOTHING about pure-vs-procedural, and the IR read that as PURE. RFC-0046 "
                     "removed the bare-`op` default exactly so the 1 bit is always stated — `on` "
                     "had put it back. RFC-0057",
                     f->kids[0]->tok.line);
                continue;
            }

            // ② `mut` 파라미터를 통해 **호출자의** 저장소에 쓰는가
            for (proven_size_t q = 0; q < h.np; q++) {
                if (!h.p[q].is_mut) {
                    bool mref = false;
                    for (proven_size_t k2 = h.p[q].ts; k2 < h.p[q].te; k2++)
                        if (f->kids[k2]->kind == LOW_CST_ATOM && veq(f->kids[k2]->tok.lex, "mut_ref"))
                            mref = true;
                    if (!mref) continue;
                }
                // ★ `mut_ref` 는 장소를 거치지 않고 **이름에 바로** 쓴다(`set p (add p 1)`) —
                //   `ck_writes_place` 만 보던 이 자리가 그 모양을 통째로 놓쳤다(결함 노트 #8).
                if (!ck_writes_place(h.body, h.p[q].name) &&
                    !ck_writes_name(h.body, h.p[q].name)) continue;
                emit(&out, "E-EFFECT-PURITY",
                     "a `fn` WRITES through a `mut` parameter — that write is visible to the "
                     "CALLER. A fn is an enforced purity contract (SPEC-003 §27): callers may "
                     "memoise it, reorder it, or elide it. An op that changes caller-owned storage "
                     "can do none of those. Declare it a `proc`. (Local mutation stays pure: "
                     "storage confined to the op is not observable — a machine write is not an "
                     "observable effect. RFC-0057)",
                     f->kids[0]->tok.line);
                break;
            }
        }
    }

    // ★★★ RFC-0057 후속 — **인자를 통한 세탁을 막는다** (2026-07-22).
    //   피호출자의 `mut`/`owned`/`mut_ref` 파라미터에 **읽기 전용 장소**(공유 파라미터·불변 `let`)를
    //   넘기면 거절한다. 직접 쓰기(위 E-EFFECT-PURITY / typecheck 의 E-TYPE-MUT)만 막던 구멍이다.
    {
        const low_opinfo_t *tab0 = (const low_opinfo_t *)ops.data;
        for (proven_size_t i = 0; i < pr->nforms; i++) {
            const low_cst_t *f = pr->forms[i]; ck_cur_form = f;
            if (f->kind != LOW_CST_FORM || f->nkids < 2 || f->kids[0]->kind != LOW_CST_ATOM) continue;
            low_kw_t kw = f->kids[0]->tok.kw;
            if (kw != LOW_KW_FN && kw != LOW_KW_PROC) continue;
            low_op_header_t h = low_op_header(f);
            if (!h.body) continue;
            ck_bind_t binds[256]; proven_size_t nb = 0;
            for (proven_size_t q = 0; q < h.np && nb < 256; q++) {
                binds[nb].name = h.p[q].name;
                binds[nb].is_mut_place = ck_param_mutq(f, &h, q);
                nb++;
            }
            ck_collect_binds(h.body, binds, &nb, 256);
            ck_launder_walk(&out, h.body, tab0, ops.len, binds, nb, pr);
            ck_capkind_walk(&out, h.body, f);   // ★ 권위는 종류로 (RFC-0077 §P1-2)
            ck_capforge_walk(&out, h.body, tab0, ops.len);  // ★ 권한은 지어낼 수 없다 (RFC-0030 D2)
            ck_excl_args_walk(&out, h.body, tab0, ops.len); // ★ 쓰기 자리와 읽기 자리에 같은 저장소 (#9)
            ck_borrow_lend_walk(&out, h.body, tab0, ops.len, NULL, 0);  // ★ struct 빌려준 자 (RFC-0116 D1)
            ck_view_inval_walk(&out, h.body, tab0, ops.len);            // ★ 무효화된 출처의 뷰 (RFC-0116 D4 I)
            ck_inplace_body_check(&out, f, h.body, tab0, ops.len);      // ★ `inplace` 선언이 참인가 (RFC-0116 B2)
            ck_mutref_of_ro_walk(&out, h.body, binds, nb);   // ★ `let` 은 참조로도 안 바뀐다 (#46)
            ck_mut_literal_bind_walk(&out, h.body);          // ★ 리터럴은 고칠 자리가 아니다 (#84)
            ck_typeholes_walk(&out, h.body, f, &h, pr, tab0, ops.len);  // ★ 실행까지 새던 다섯 (#31·#39·#44·#45)
            ck_method_receiver(&out, f);                     // ★ 수신자는 첫 입력 (#58)
            ck_sum_return(&out, h.body, f, &h);              // ★ sum 은 부동소수 합 (#72)
            ck_result_discard_walk(&out, h.body, h.body, tab0, ops.len);  // ★ 버려지는 실패 (#51)
            ck_errors_unraised(&out, f, h.body, tab0, ops.len);  // ★ 한 번도 내지 않는 오류 갈래 (#51)
            ck_slashslash_walk(&out, h.body);                // ★ `//` 는 주석이 아니다 (#28)
            ck_loopword_walk(&out, h.body, false);           // ★ 반복 밖의 break (#35)
            ck_fieldborrow_walk(&out, h.body);               // ★ 칸의 빌림은 안 지었다 (#20)
            ck_void_return(&out, h.body, f, &h);             // ★ void 는 값을 안 돌려준다 (#30)
            ck_inner_else(&out, h.body);                     // ★ 블록 안의 else (#66)
            ck_splat_walk(&out, h.body, NULL, 0);            // ★ splat 은 문맥에서 레인 수를 받는다 (#73)
            {   low_op_header_t hh = low_op_header(f);
                ck_cmpwidth_walk(&out, h.body, f, &hh, h.body); }   // ★ 자리에 안 들어가는 리터럴과의 비교 (#32)
            ck_foldorder_walk(&out, h.body, tab0, ops.len);        // ★ fold 단계 op 의 누산 차례 (#60)
            ck_predbool_walk(&out, h.body, tab0, ops.len);         // ★ 판정 op 은 bool (X-0061; 별칭도 X-0065)
            {   low_op_header_t hh = low_op_header(f);
                ck_collectfull_walk(&out, h.body, f, &hh); }        // ★ 받는 자리가 모자란 것을 알면 거절 (X-0062)
            ck_scope_escape(&out, h.body);                   // ★ 블록 안 이름을 밖에서 (#33)
            {   // ★ 같은 쓰기 자리를 두 액터에게 (#54)
                ck_lent_t lent[64]; proven_size_t nl = 0;
                ck_actor_lend_walk(&out, h.body, binds, nb, lent, &nl, 64);
            }
            if (!ck_form_is_handler_of(pr, f)) {             // ★ 액터 밖에서 상태 칸을 읽는가 (§10.2)
                proven_u8str_view_t anames[64]; proven_size_t na = 0;
                ck_collect_actor_locals(h.body, pr, anames, &na, 64);
                if (na) ck_actorfield_walk(&out, h.body, anames, na);
            }
            // ★ A2 — 반환값 세탁: 출력이 가변 장소인 op 이 공유(읽기 전용) 입력을 돌려주면 거절.
            if (ck_output_is_mut_place(f, &h))
                ck_retlaunder_walk(&out, h.body, binds, nb, pr);
        }
    }

    // ★★★ RFC-0058 — 선형 소유권. 명세가 요구하고 있었고, 아무도 강제하지 않았다.
    {
        const low_opinfo_t *tab0 = (const low_opinfo_t *)ops.data;
        for (proven_size_t i = 0; i < pr->nforms; i++) {
            const low_cst_t *f = pr->forms[i]; ck_cur_form = f;
            if (f->kind != LOW_CST_FORM || f->nkids < 2 ||
                f->kids[0]->kind != LOW_CST_ATOM) continue;
            low_kw_t kw = f->kids[0]->tok.kw;
            if (kw != LOW_KW_FN && kw != LOW_KW_PROC) continue;
            ck_ownership(&out, pr, f, tab0, ops.len);
        }
    }

    // pass 2: each op's actual effects must be ⊆ declared
    const low_opinfo_t *tab = (const low_opinfo_t *)ops.data;
    for (proven_size_t i = 0; i < ops.len; i++)
        ck_lone_concurrent(&out, tab[i].body, tab, ops.len);   // ★ A5 — 증명 가능한 데드락
    // ★ E-CONC-DEADLOCK 전제(모듈 전역): `use`(수입) 가 없고 `chsend` 원자가 하나도 없어야
    //   프로그램이 자기완결적이고 **아무도 송신하지 않음**이 증명된다. 둘 중 하나라도 있으면
    //   외부/다른 데서 채널이 먹여질 여지가 있으므로 이 검사는 **아예 돌지 않는다**(건전성).
    {
        bool mod_has_chsend = false, mod_has_use = false;
        for (proven_size_t i = 0; i < pr->nforms; i++) {
            const low_cst_t *f = pr->forms[i]; ck_cur_form = f;
            if (ck_subtree_has_atom(f, "chsend")) mod_has_chsend = true;
            if (f && f->kind == LOW_CST_FORM && f->nkids > 0 &&
                f->kids[0]->kind == LOW_CST_ATOM && f->kids[0]->tok.kw == LOW_KW_USE)
                mod_has_use = true;
        }
        // ★ 두 층은 `chsend` 유무로 **깔끔히 갈린다**(이중 emit 방지):
        //   · chsend 가 **하나도 없다** → `ck_group_deadlock`(모듈 전역 "아무도 안 보낸다").
        //   · chsend 가 **있다**       → `ck_group_mismatch`(채널 정체 추적 — 짝 없는 수신자).
        //   둘 다 전제: `use` 없음 + 담은 op **파라미터 없음**(채널이 밖에서 안 들어온다).
        if (!mod_has_use)
            for (proven_size_t i = 0; i < ops.len; i++) {
                if (!tab[i].form || !tab[i].body) continue;    // 헤더/본체 못 보면 보수적으로 건너뜀
                low_op_header_t h = low_op_header(tab[i].form);
                if (h.np != 0 || h.too_many) continue;         // 파라미터 있으면 채널이 밖에서 올 수 있다
                if (!mod_has_chsend) ck_group_deadlock(&out, tab[i].body, tab, ops.len);
                else                 ck_group_mismatch(&out, tab[i].body, tab[i].body, tab, ops.len);
            }
    }
    for (proven_size_t i = 0; i < ops.len; i++) {
        unsigned used = walk_effects_in(tab[i].body, tab, ops.len, tab[i].mod, tab[i].form);   // ★ 제 모듈 우선 · 제 파라미터는 호출이 아니다
        // ★★★ **actor 핸들러의 상태 필드 쓰기 = state 효과**(2026-07-23). walk_effects 는 어느 이름이
        //   state 필드인지 몰라 `set bad 1`(맨 이름)을 효과로 못 셌다 — 그래서 `effects state` 핸들러가
        //   "state 를 선언했는데 안 함"(W-EFFECT-OVER)으로 오진됐다. 핸들러 body 에서 상태 필드를
        //   `set` 하면 EFF_STATE 를 더한다(actor_blk 의 필드 이름과 대조).
        if (tab[i].is_handler && tab[i].actor_blk && tab[i].body &&
            ck_handler_writes_state(tab[i].body, tab[i].actor_blk))
            used |= EFF_STATE;
        // ★★★★★ **장치 레지스터를 만지는 op 은 `unsafe` 를 한다** (2026-09-04, WO-0186 · RFC-0042).
        //   `ck_mmio` 는 그런 op 에 `cap mmio` 와 `unsafe` 를 **요구한다**. 그런데 이 워크가 그것을
        //   안 세어, 요구대로 선언한 op 이 곧바로 *"선언했는데 안 한다"*(W-EFFECT-OVER)로 경고됐다 —
        //   **요구와 경고가 서로를 부정한다.** `asm` 이 2026-08 에 같은 자리에서 고쳐졌다.
        //
        //   ★★ **낱말이 아니라 대상을 본다.** 처음엔 `builtin_effect` 에 `read_volatile`/`write_volatile`
        //     을 넣었다가 골든이 잡았다: *"volatile 은 장치의 것만이 아니다"*(RFC-0039 §9-6) —
        //     ISR 이 게시하는 잡 큐를 평범한 메모리에서 volatile 로 읽는 op 이 **거절됐다**(E-EFFECT).
        //     ⇒ `cap mmio` 를 요구할 때 쓰는 **바로 그 판정**(`ck_volatile_is_device`·
        //       `ck_field_touches_mmio`)을 여기서도 쓴다. 요구와 계수가 **같은 자리에서** 난다.
        //   ☞ *한 사실을 두 곳이 다르게 판정하면 둘 중 하나는 반드시 거짓말이 된다.*
        if (tab[i].form && (ck_volatile_is_device(pr, tab[i].form) || ck_field_touches_mmio(pr, tab[i].form)))
            used |= EFF_UNSAFE;
        // ★ 예약 블록(모듈 가변 전역)을 만지면 `state` 다 — 요구하는 쪽과 같은 판정으로(위 ★★★★★).
        if (tab[i].form && ck_touches_reserved(pr, tab[i].form))
            used |= EFF_STATE;
        // ★ `asm` 도 같은 규율이다 — **헤더 절로 쓴 것까지** 센다(WO-0187). 본문 워크는 문장 자리만
        //   보므로 `unsafe proc wait_irq … asm cortex_m .` 이 새어 나갔다. 요구하는 쪽(`ck_asm`)과
        //   **같은 함수**를 부른다.
        if (tab[i].form && ck_op_has_asm(tab[i].form))
            used |= EFF_UNSAFE;
        // ★ RFC-0083 L1 — subeffecting 은 **폐포 하에서** 판정한다: I ⊑ D ⟺ close(I) ⊆ close(D).
        //   그러면 `concurrent` 를 선언하면 `wait` 사용을 덮고(concurrent ⊇ wait), `wait` 만 선언하고
        //   `concurrent` 를 하면 새어 나온다(E-EFFECT). 포함관계가 이 한 줄에서 대수적으로 강제된다.
        // ★★★★★ RFC-0120 — **흡수가 여기서 멈춘다.** `absorbs machine <이름> .` 을 적은 op 은
        //   `unsafe` 를 «선언한 것» 으로 쳐서 leak 에서 빠진다 — 그러나 **시그니처에는 싣지 않는다**.
        //   그래서 부르는 쪽은 아무것도 적지 않는다. 그 자리가 장부에 남고 게이트가 센다(§5.2-6).
        //   ☞ 나머지 효과는 그대로 샌다: 흡수는 `unsafe` 하나만 멈춘다.
        unsigned declared_eff = tab[i].self_declared;
        if (tab[i].form && ck_absorbs_machine(tab[i].form, NULL)) declared_eff |= EFF_UNSAFE;
        unsigned leak = eff_close(used) & ~eff_close(declared_eff);
        if (leak) {
            // ★ 오류 쪽도 같은 자리다 — 파일을 아는 노드를 손에 쥐고 있으므로 함께 싣는다.
            // ★★★ **어느 효과가 새는지 말한다** (2026-09-14). `leak` 은 여기 있었는데 문장은 «어떤 효과» 라고만 했다.
            emit_at(&out, tab[i].is_calc ? "E-EFFECT-CALC" : "E-EFFECT", NULL, tab[i].body);
            low_diag_t *last = (low_diag_t *)out.diags.data + (out.diags.len - 1);
            char names[96]; size_t no = 0; names[0] = 0;
            static const unsigned bits[] = { EFF_IO, EFF_ALLOC, EFF_HEAP, EFF_STATE, EFF_PANIC,
                                             EFF_UNSAFE, EFF_ATOMIC, EFF_CONCURRENT, EFF_WAIT };
            for (size_t b = 0; b < sizeof bits / sizeof bits[0]; b++) {
                if (!(leak & bits[b])) continue;
                int w = snprintf(names + no, sizeof names - no, "%s`%s`", no ? ", " : "", effect_name(bits[b]));
                if (w < 0 || (size_t)w >= sizeof names - no) break;
                no += (size_t)w;
            }
            if (!no) snprintf(names, sizeof names, "%s", effect_name(leak));
            if (tab[i].is_calc)
                snprintf(last->detail, sizeof last->detail,
                         "this fn is declared pure but performs %s — make it a `proc` with `effects …`, or remove the effect", names);
            else
                snprintf(last->detail, sizeof last->detail,
                         "this op performs %s, which its `effects` clause does not declare — add it to `effects …`, "
                         "or stop calling what needs it", names);
        }
        // ★★★ **W-EFFECT-OVER** (RFC-0007) — 반대 방향: **선언했는데 안 하는** 효과. 명시적으로
        //   effects 절을 쓴 proc 에서만(기본 unrestricted proc 은 판정 안 함). 안 하는 걸 선언하면
        //   호출자가 없는 비용을 떠안는다 — effect 는 계약이고, 지키지 않는 계약은 정직하지 않다.
        if (tab[i].explicit_eff) {
            // ★ 핸들러가 panic 하면 `effects state` 는 만족된 것으로 본다(panic 함의) — 안 그러면
            //   panic 만 하는 핸들러가 "state 선언했는데 안 함" 으로 거짓 경고된다(vm_restart boom).
            unsigned used_over = used;
            if (tab[i].is_handler && (used & EFF_PANIC)) used_over |= EFF_STATE;
            // ★★ **호출자 저장소에 쓰면 `state` 를 적은 것이 과장이 아니다** (결함 노트 #4 · #16 · #63).
            //   `mut`/`mut_ref` 매개변수와 `collect into <매개변수>` 는 **부르는 쪽이 보는 자리**에
            //   쓴다 — 순수성 쪽은 그것을 그렇게 판정하는데(E-EFFECT-PURITY) 이 경고만 안 세어,
            //   같은 몸을 `proc … effects state .` 로 정직하게 적으면 «선언했는데 안 한다» 고 울었다.
            //   ☞ 요구하지는 않는다(그것은 언어 설계를 바꾸는 일이다) — **적은 것을 부정하지 않을 뿐**이다.
            if (tab[i].form && ck_writes_caller_storage(tab[i].form)) used_over |= EFF_STATE;
            unsigned over = tab[i].declared & ~used_over & ~EFF_NOPRIM;   // ★ 추론할 원시어가 없는 여섯은 묻지 않는다
            // ★★★★ RFC-0112 D7 — `via <타입>` 으로 들어온 효과는 **그 타입이 할 수 있는 것**의 합이다. 이 op 이
            //   그 가운데 일부만 부르는 것(예: `grow` 만)은 과장이 아니다 — 그 몫은 경고에서 뺀다.
            if (tab[i].form) {
                const low_cst_t *of = tab[i].form;
                for (proven_size_t z = 1; z + 1 < of->nkids; z++)
                    if (of->kids[z]->kind == LOW_CST_ATOM && veq(of->kids[z]->tok.lex, "via") &&
                        of->kids[z + 1]->kind == LOW_CST_ATOM)
                        over &= ~ck_via_effects(of->kids[z + 1]->tok.lex);
            }
            // ★★★★★ **몸이 없는 op 은 셀 것이 없다** (2026-09-06). `extern` 은 몸이 C 에 있다
            //   (RFC-0063). 그런데 이 경고는 그런 선언에도 붙었고 — 그 자리는 **도구가 요구한
            //   선언**이다: `effects unsafe` 를 빼면 `E-FFI-NOEFFECT`·`E-UNSAFE-UNUSED` 로 거절한다.
            //   **요구하고 나서 그 요구를 지킨 것을 경고했다**(코퍼스 27 건, 전부 `extern`).
            //   ☞ *요구와 경고가 서로를 부정하면 둘 중 하나는 거짓말이다* — 같은 모양을
            //     `read_volatile` 에서 한 번 고쳤다(WO-0186). 여기가 그 셋째 자리다.
            if (over && !tab[i].body) over = 0;
            if (over) {
                // ★★★★ **어느 효과인지 말한다** (2026-09-06). 이 경고는 넉 달 동안
                //   *"선언한 효과 하나를 안 한다"* 고만 말했다 — 그런데 그 정보(`over`)는
                //   **바로 여기 있었다**. 그래서 코퍼스의 204 건을 «표가 낡은 자리» 와
                //   «선언이 과장인 자리» 로 가르려면 사람이 op 마다 소스를 읽어야 했고,
                //   그 값이 비싸서 **아무도 안 갈랐다**.
                //   ☞ *가진 사실을 말하지 않는 진단은, 그것을 다시 알아내는 값만큼 사람을 태운다.*
                //   (바로 위 `E-EFFECT` 자리에 `(void)effect_name; // kept for future richer
                //    messages` 가 넉 달 있었다 — 그 «나중» 이 오늘이다.)
                // ★ 문장은 `detail` 에 짓고 `msg` 가 그것을 가리킨다 — 스택 버퍼를 주면
                //   대롱거린다(`ir_fail_buf` 가 같은 이유로 같은 자리를 쓴다).
                warn_at(&out, "W-EFFECT-OVER", NULL, tab[i].body);
                low_diag_t *last = (low_diag_t *)out.diags.data + (out.diags.len - 1);
                snprintf(last->detail, sizeof last->detail,
                         "this op DECLARES `%s` but never PERFORMS it. A declared effect is a cost "
                         "the caller must budget for (a pure caller cannot call an `io` op). Drop it, "
                         "or — if a future version will perform it — say so (RFC-0007)",
                         effect_name(over));
                last->msg = NULL;
            }
        }
    }

    proven_array_destroy(&ops);
    return out;
}
