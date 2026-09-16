/* low_vm.c — 태그 값 스택 인터프리터(VM)와 빌림 스택.
 *
 * ★ `low_ir.c` 에서 **떼어 왔다** (2026-08-31, WO-0163 · X-0011). 뜻은 한 줄도 안 바꿨다 —
 *   옮기기만 했다. 그 증명은 `scripts/check-emit-identical.py` 다: 코퍼스 342 단위의
 *   `--emit-c` 와 `--ir` 출력이 **바이트 동일**해야 한다.
 *
 * ★★ 왜 이 구획이 첫 조각인가. 경계마다 넘나드는 심볼을 세었더니(실측):
 *       하강 6,101 줄 · 나가는 44        구간분석·SMT 4,815 줄 · 들여오는 45
 *       **VM 4,101 줄 · 나가는 1 · 들여오는 11**   ← 압도적으로 깨끗하다
 *   나눠 갖는 것은 `low_ir_priv.h` 에 있고, **그 목록이 늘면 선이 잘못된 것**이다.
 *
 * ★★★ 값: 증분 빌드의 96% 가 `low_ir.c` 였다(2724ms / 2833ms). 이제 둘이 `-j` 로 나란히 선다.
 */
#include "low_ir_priv.h"

extern char **environ;   // ★ cap env (RFC-0030 D2′) — 호스트 프로파일 전용

#include "low_hwm.h"
#include "low_ir.h"
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

// ★ SHA 본문을 **여기서 컴파일**한다(같은 매크로를 C 백엔드가 방출한다).
//   ☞ 쪼개면서 따라왔다 — `low_ir.c` 는 이것을 **한 번도 안 쓴다**(실측 0 회).
LOW_SHA256_BODY
LOW_SHA512_BODY

// ★ `round` 는 **반올림 방향을 이름이 못 말한다** — C 의 round() 는 half-away-from-zero 다.
//   방출 C 도 같은 함수를 쓰므로 두 뒤끝이 같다. (banker's rounding 이 필요하면 별도 이름으로.)
static double lw_round_half_away(double x) { return round(x); }

// ── stack interpreter (tagged values) ─────────────────────────────────────────

#define VM_STACK  256
#define VM_LOCALS IR_MAXLOCALS
// ★★ 호출 깊이 한계. 512 였는데 — **가드에 닿기 전에 C 스택이 터졌다.**
//   `vm_exec` 은 C 재귀이고 프레임이 크다(지역 64 + 피연산자 스택 256 ≈ **10KB**).
//   512 × 10KB ≈ **5MB** — 기본 스택(8MB)에 다른 것까지 얹히면 넘는다.
//   ⇒ **도구가 진단 없이 코어를 덤프했다.** 한계가 있는데 그 한계에 못 닿는다면
//     그 한계는 **거짓말**이다. 프레임 크기에 맞춘다.
// ★★★ green thread — 코루틴 상한·프레임 예산·스택. gthread i 는 base depth i*GT_DEPTH 에서 돈다
//   (frame 충돌 회피). 스택은 정적(vm 이 static·단일 스레드).
#define VM_GTHR   16   /* 네이티브(LW_GTHR)와 통일. 초과 시 재활용→그래도 없으면 거절 */
#define GT_DEPTH  8
// ★★★ **호출깊이 트랩은 프레임 배열 크기와 분리한다** (RFC-0072 E-Alloc P2, 2026-07-21).
//   VM_MAXCALL = 한 호출 사슬이 팔 수 있는 최대 프레임(네이티브 `lw_depth>128` 과 **일치**해야
//   오라클이 산다). 이건 **배열 크기가 아니다** — 배열은 아래 VM_FRAMES 로 따로 잡는다. 전엔 둘이
//   VM_GTHR*GT_DEPTH 로 묶여 있어, 배열을 키우려 하면 이 한계가 따라 움직여 네이티브와 갈라졌다.
#define VM_MAXCALL 128
#define VM_DEPTH  VM_MAXCALL   /* 호환 별칭(트랩 값) — 남은 참조가 이 이름을 쓴다 */
// ★★★ **프레임 배열**: 메인(spawn 한 쪽)이 [0, 메인깊이] 를 쓰고, 그린스레드는 그 **위**
//   [sched_base .. ] 에서 돈다(sched_base = 메인깊이+1). 겹치면 그린스레드가 메인 프레임을 덮어
//   task_group 후 메인 지역의 참조가 댕글링으로 **오판**된다(실측 발산 — E-Alloc P1 이 고친다).
//   ⇒ 메인 최대깊이(VM_MAXCALL) + 모든 그린스레드 창(VM_GTHR*GT_DEPTH) 이 안 겹치게 배열을 잡는다.
#define VM_FRAMES (VM_MAXCALL + VM_GTHR * GT_DEPTH)
#define GT_STK    (128 * 1024)
#define VM_MAXCHAN 64
#define VM_CHAN_CAP 64   // bounded channel 기본 용량
// ★ 풀은 여전히 **유한**하다 — 그러나 프레임 회수(아래 IRW_CALL)와 함께라면
//   루프가 호출을 통해 돌 때 **고갈되지 않는다**. 한 프레임 안의 긴 루프는 여전히 한계가 있다:
//   그것이 **타입 하강이 없애는 바로 그 한계**다(하강된 op 은 박스를 아예 안 쓴다).
#define VM_MAXREC 4096
#define VM_MBOX   4096   /* async 메일박스 전역 대기열 크기 (RFC-0009 spawn send) */
// ★ `some(v)`·`ok(v)` 를 담는 박스 풀 — **네 번째 유한 풀**이었다(레코드·비트셋·스택에 이어).
//   128칸이었고 **회수가 없었다** ⇒ **오류/옵션을 200번 다루면 조용히 죽었다.**
#define VM_MAXBOX 4096

typedef enum { VMV_INT = 0, VMV_SLICE, VMV_REC, VMV_OK, VMV_ERR, VMV_STACK, VMV_BITSET, VMV_REF,
               VMV_VIEW,   // zero-copy struct view: .p/.n = bytes, .box = struct index
               VMV_SOME,   // .box = boxed payload
               VMV_NONE,
               VMV_VARRAY, // typed array view: .p = bytes, .n = element count, .box = elem size
               VMV_VEC,    // portable SIMD vector: .box = vec pool index
               VMV_MASK,   // lane predicate: .i = bits, .box = lane count
               VMV_FLT     // IEEE-754 double: bits in .i
             } vmv_tag_t;
typedef struct {
    proven_u8           tag;
    proven_i64          i;     // INT value / ERR name index
    const proven_u8    *p;     // SLICE bytes
    proven_size_t       n;     // SLICE length
    proven_i32          box;   // REC/OK/STACK/BITSET: pool index
} vmv_t;
typedef struct { proven_size_t make_idx; vmv_t fields[IR_MAKE_MAXF]; proven_size_t nfields; } vmrec_t;
// ★ 스택 풀 — 레코드·비트셋과 **같은 규율**. 8칸이었다: **루프에서 아홉 번째를 만들면 죽었다.**
#define VM_MAXSTK   256
#define VM_STKCAP   128
// ★ 비트셋 풀 — 레코드와 **같은 규율**(프레임 회수 + 넉넉한 크기). 8칸이었다:
//   **루프에서 아홉 번째 비트셋을 만들면 죽었다.**
#define VM_MAXBSET  4096
#define VM_MAXFILE  32       // ★ 동시에 열 수 있는 파일 — 고정 풀(다른 풀과 같은 규율)
#define VM_MAXSOCK  32       // ★ 동시 소켓 — 고정 풀. socks[i]=fd+1 (0=빈 슬롯 · 정적 0-init 와 맞음)
// REF encoding in vmv_t: .i = slot | tag<<16 | VM_REF_MUT, .box = frame depth, .n = generation
#define VM_REF_MUT  ((proven_i64)1 << 62)
#define VM_REF_SLOT 0xffff
#define VM_REF_TAG(i) ((proven_u32)(((proven_u64)(i) >> 16) & 0xffffffffu))
// borrow-stack-lite (V1 dynamic EXCL, Stacked-Borrows 축소판): per borrowed slot,
// a stack of borrow tags. Conflicting accesses invalidate newer borrows; using an
// invalidated borrow is E-VM-EXCL.
#define VM_BSTK_DEPTH 8
#define VM_BSTK_SLOTS 16
#define VM_BTAG_MUT   0x80000000u
typedef struct { proven_u8 slot; proven_u8 n; proven_u32 tags[VM_BSTK_DEPTH]; } vmbstk_t;
typedef struct { vmv_t items[VM_STKCAP]; proven_size_t n, cap; } vmstk_t;
// dynamic verifier V1 (RFC-0017): every call frame gets a fresh generation; a
// reference carries (depth, generation) and every access revalidates them —
// a reference that outlives its frame is caught as V-DANGLING (use-after-return).
typedef struct { vmv_t *locs; proven_u64 gen; vmbstk_t bstk[VM_BSTK_SLOTS]; proven_u8 nbstk; } vmframe_t;
#define VM_MAXENC 8
#define VM_ENCCAP 64
typedef struct {
    const low_ir_t *ir;
    // ★★★ **걸음 예산** (2026-07-30) — **오라클만** 쓴다(0 = 무한, 사용자 실행의 기본).
    //   계약 오라클은 경계값을 먹여 op 을 **실제로 돌린다**. 그런데 반복 횟수가 입력인 op
    //   (`while lt i n` 에 n = 2^64−1)에서는 그 실행이 **끝나지 않는다** — 실측으로 `--ir` 가
    //   60초를 넘겨 멈추지 않았다(`--check` 는 정상). 도구가 멈추면 그건 도구의 결함이다.
    //   ⇒ 예산을 넘기면 그 케이스를 **건너뛰고, 건너뛴 것을 센다**(조용히 넘기지 않는다).
    proven_u64      budget, steps;
    bool            budget_hit;
    vmrec_t         recs[VM_MAXREC];  proven_size_t nrecs;
    vmv_t           boxes[VM_MAXBOX]; proven_size_t nbox;
    vmstk_t         stks[VM_MAXSTK];  proven_size_t nstk;
    proven_u64      bsets[VM_MAXBSET]; proven_u8 bwid[VM_MAXBSET]; proven_size_t nbset;
    alignas(64) proven_u8 encs[VM_MAXENC][VM_ENCCAP]; proven_size_t nenc;   // encode output buffers
                                                                          // (over-aligned: `align n` views of encode output must hold)
    struct { proven_u64 l[16]; proven_u8 n, esz; bool flt; bool sign; } vecs[32]; proven_size_t nvec;
    const proven_u8str_view_t *pargs;  proven_size_t npargs;   // ★ 프로그램 인자(D2′)
    // ★★★★ **두 뿌리** (RFC-0112 D3 · WO-0211). 고정 창은 안 자라고, 힙은 청크를 잇는다.
    //   ★ 네이티브와 **같은 규칙**이어야 오라클이 선다: 같은 순서·같은 크기 ⇒ 같은 바이트(0 으로 채운다).
    //   ★ 힙 청크는 **호스티드에서만** 잡힌다 — `--target` 이 no_heap 이면 검사기가 이미 거절했고,
    //     혹시 닿아도 여기서 `none` 이다(VM 이 네이티브보다 관대하면 오라클이 거짓말을 한다).
    proven_u8      *fixmem;  proven_size_t fixcap, fixn;          // 고정 창
    proven_u8     **hpch;    proven_size_t *hpcap, *hpbase;       // 힙 청크 · 크기 · 논리 시작
    proven_size_t   hpn, hpmax, hpcur, hpoff;                    // 잡은 수 · 표 용량 · 지금 청크 · 그 안 오프셋
    FILE           *files[VM_MAXFILE];   // ★ 열린 파일 — 핸들은 이 배열의 인덱스다(RFC-0069 §6)
    DIR            *dirs[VM_MAXFILE];     // ★ 열린 디렉터리 — 핸들은 이 배열의 인덱스다(RFC-0069 §6)
    int             socks[VM_MAXSOCK];   // ★ 열린 소켓 fd+1 — 핸들은 이 배열의 인덱스(0=빈 슬롯)
    vmframe_t      *frames;   // ★ 동적: 메인 + 안 겹치는 그린스레드 창. 그린스레드 수만큼 성장(E-Alloc P3).
    proven_u64      gcount;
    proven_u32      btag;    // borrow tag counter
    // ★★★ **bounded mailbox** — 레코드(=actor 인스턴스)별 **in-flight 메시지 수**.
    //   순차 배달에서 메일박스가 찰 수 있는 유일한 길은 **재진입**(A→A · A→B→A)이다.
    //   핸들러 진입에 +1, 반환에 −1. `mailbox bounded N` 을 넘으면 E-VM-MAILBOX-FULL.
    proven_u16      mbox_depth[VM_MAXREC];
    // ★★★ **async 메일박스** (RFC-0009 — call/cast 분리). `spawn send` 가 여기에 넣고, `drain` 이
    //   FIFO 로 비운다. 전역 대기열(도착 순서 = 삽입 순서). 각 항목은 (대상 레코드, 핸들러, 인자).
    //   `mailbox bounded N` 이면 한 인스턴스로 대기 중인 메시지가 N 을 못 넘는다.
    struct { proven_i32 box; proven_u16 handler; proven_u8 nargs; proven_i32 job; vmv_t args[LOW_MAX_PARAMS]; } mbox[VM_MBOX];
    proven_size_t   nmbox;
    // ★★★ **green thread** (RFC-0009 2/3) — 태스크가 `yield` 로 중단하고 스케줄러에 양보한다.
    //   각 실행 단위(태스크·메시지)가 자기 ucontext(코루틴)에서 돈다 — yield 는 스케줄러로 swap 하고,
    //   재개는 다시 swap. 협조적(한 번에 하나, OS 스레드 하나)이라 경합 없음. 오라클이 yield 순서를 탐색.
    //   frame 충돌을 피하려고 각 gthread 는 base depth = gi*GT_DEPTH 에서 vm_exec 한다(얕은 태스크 전제).
    struct gthr_s { ucontext_t ctx; proven_i32 box; proven_u16 handler; proven_u8 nargs;
                    proven_u8 state;   // 0=빈 · 1=새로 · 2=중단(runnable) · 3=실행 · 4=완료 · 5=채널블록 · 6=await블록
                    proven_i32 blk_chan; proven_u8 blk_recv; proven_i32 job, blk_job;   // 채널·job 블록 정보
                    unsigned base;     // ★ 이 그린스레드의 프레임 창 시작(sched_base + slot*GT_DEPTH) — 메인과 안 겹침
                    vmv_t args[LOW_MAX_PARAMS]; } *gthr;   // ★ 동적: 동시 그린스레드 수만큼 성장(E-Alloc P3)
    proven_size_t   ngthr;
    ucontext_t      sched_ctx;    // 스케줄러 컨텍스트(yield/완료가 여기로 swap)
    proven_size_t   cur_gthr;     // 지금 도는 gthread
    bool            in_gsched;    // 코루틴 스케줄러 안인가(yield 가 의미 있는지)
    unsigned        cur_ceiling;  // ★ 지금 도는 호출 사슬의 깊이 트랩(메인=VM_MAXCALL · 그린스레드=base+GT_DEPTH)
    unsigned        sched_base;   // ★ 그린스레드 창이 시작하는 절대깊이(메인깊이+1) — E-Alloc P1
    // ★★★ **channel** (RFC-0009 3/3) — bounded FIFO 통신 채널. chrecv 는 empty 에서, chsend 는 full 에서
    //   green thread 의 yield 로 블록한다(sender/recver 가 오면 재개). 진행이 멈추면 데드락으로 잡는다.
    struct { vmv_t buf[VM_CHAN_CAP]; proven_size_t head, tail, count; } chans[VM_MAXCHAN];
    proven_size_t   nchan;
    proven_u64      gprogress;    // 채널 op 성공·태스크 완료마다 +1 — 데드락 감지용
    // ★★★ **job 테이블** (await) — `spawn <op>` 이 job 을 만들고 핸들을 준다. 태스크가 끝나면 결과를
    //   여기 담고 done 을 세운다. `await <h>` 는 done 까지 블록했다가 result 를 읽는다.
    struct { proven_u8 done; vmv_t result; } jobs[VM_MBOX];
    proven_size_t   njobs;
    proven_array_t *diags;
} vm_ctx_t;

// ── borrow-stack helpers (dynamic EXCL) ───────────────────────────────────────
static vmbstk_t *bstk_find(vmframe_t *fr, proven_u8 slot, bool create) {
    for (proven_u8 i = 0; i < fr->nbstk; i++)
        if (fr->bstk[i].slot == slot) return &fr->bstk[i];
    if (!create || fr->nbstk >= VM_BSTK_SLOTS) return NULL;
    vmbstk_t *b = &fr->bstk[fr->nbstk++];
    b->slot = slot; b->n = 0;
    return b;
}
// owner read of a slot: unique (mut) borrows are invalidated, shared survive
static void bstk_owner_read(vmframe_t *fr, proven_u8 slot) {
    vmbstk_t *b = bstk_find(fr, slot, false);
    if (!b) return;
    proven_u8 w = 0;
    for (proven_u8 i = 0; i < b->n; i++)
        if (!(b->tags[i] & VM_BTAG_MUT)) b->tags[w++] = b->tags[i];
    b->n = w;
}
// owner write (rebind): every outstanding borrow is invalidated
static void bstk_owner_write(vmframe_t *fr, proven_u8 slot) {
    vmbstk_t *b = bstk_find(fr, slot, false);
    if (b) b->n = 0;
}
// use of a borrow: its tag must still be on the stack; a WRITE through it also
// invalidates everything pushed above it (they were derived later)
static bool bstk_use(vmframe_t *fr, proven_u8 slot, proven_u32 tag, bool write) {
    vmbstk_t *b = bstk_find(fr, slot, false);
    if (!b) return false;
    for (proven_u8 i = b->n; i-- > 0; ) {
        if ((b->tags[i] & ~VM_BTAG_MUT) == tag) {
            if (write) b->n = (proven_u8)(i + 1);
            else {   // read: invalidate mut borrows above this one
                proven_u8 w = (proven_u8)(i + 1);
                for (proven_u8 j = (proven_u8)(i + 1); j < b->n; j++)
                    if (!(b->tags[j] & VM_BTAG_MUT)) b->tags[w++] = b->tags[j];
                b->n = w;
            }
            return true;
        }
    }
    return false;
}

static vmv_t vmv_int(proven_i64 x) { return (vmv_t){ .tag = VMV_INT, .i = x }; }
static double vmv_f(vmv_t v) { double d; memcpy(&d, &v.i, 8); return d; }
static vmv_t vmv_flt(double d) { vmv_t v = { .tag = VMV_FLT }; memcpy(&v.i, &d, 8); return v; }

// ★★★ **벡터 풀이 되감긴다** (RFC-0089 R2-2 · 단계 B, 2026-08-03).
//   벡터는 **값이지 자원이 아니다** — `owned vec` 이 E-VEC-QUAL 로 거절되는 바로 그 이유다.
//   그런데 VM 은 그것을 32칸 풀의 **상자**로 만들고 **아무도 돌려주지 않았다**: 루프 하나가
//   풀을 다 먹고 **33번째 반복에서 VM 만 죽었다**(빠른 경로는 벡터를 프레임의 값으로 두므로
//   300만 번도 돈다 — `docs/known-defects/vector-pool-exhausted-in-loop.md`).
//   ⇒ 프레임이 **자기 바닥**(`vec_base`)을 들고, **뒤로 가는 분기**(=루프)와 **반환**에서
//     살아 있는 것만 바닥으로 모은다(mark-compact). 살아 있음의 뿌리는 이 프레임의
//     **지역과 피연산자 스택**뿐이다 — 벡터가 다른 풀로 새면 그 전제가 깨지므로 **되감지 않는다.**
//   ☞ 되감기는 **답을 바꾸지 않는다**: 핸들은 프로그램이 볼 수 없는 것이고, 옮겨진 뒤에도
//     같은 레인이 같은 자리에 있다. 바뀌는 것은 **얼마나 오래 도는가** 하나뿐이다.
// ★★★★ **뿌리에서 n 바이트** (RFC-0112 D3). 못 주면 NULL — 부르는 쪽이 `none` 을 낸다.
static proven_u8 *vm_root_take(vm_ctx_t *vm, int root, proven_size_t n) {
    if (root == 0) {
        if (!vm->fixmem) {                      // 첫 호출 — 이 실행의 창 크기로 잡는다
            vm->fixcap = low_ir_fixed_bytes();
            vm->fixmem = (proven_u8 *)calloc(vm->fixcap ? vm->fixcap : 1, 1);
            if (!vm->fixmem) return NULL;
        }
        if (n > vm->fixcap - vm->fixn) return NULL;
        proven_u8 *p = vm->fixmem + vm->fixn; vm->fixn += n;
        memset(p, 0, n); return p;
    }
    if (low_ir_target()->no_heap) return NULL;   // ★ 프리스탠딩엔 자라는 뿌리가 없다
    if (vm->hpn && n <= vm->hpcap[vm->hpcur] - vm->hpoff) {
        proven_u8 *p = vm->hpch[vm->hpcur] + vm->hpoff; vm->hpoff += n;
        memset(p, 0, n); return p;
    }
    // 되감은 뒤라면 이미 잡아 둔 다음 청크부터 다시 쓴다
    if (vm->hpcur + 1 < vm->hpn && n <= vm->hpcap[vm->hpcur + 1]) {
        vm->hpcur++; vm->hpoff = n;
        memset(vm->hpch[vm->hpcur], 0, n); return vm->hpch[vm->hpcur];
    }
    if (vm->hpn == vm->hpmax) {
        proven_size_t nm = vm->hpmax ? vm->hpmax * 2 : 16;
        proven_u8 **c2 = (proven_u8 **)realloc(vm->hpch, nm * sizeof *c2);
        if (!c2) return NULL;
        vm->hpch = c2;
        proven_size_t *k2 = (proven_size_t *)realloc(vm->hpcap, nm * sizeof *k2);
        if (!k2) return NULL;
        vm->hpcap = k2;
        proven_size_t *b2 = (proven_size_t *)realloc(vm->hpbase, nm * sizeof *b2);
        if (!b2) return NULL;
        vm->hpbase = b2;
        vm->hpmax = nm;
    }
    // ★ 되감긴 뒤의 남은 청크는 요청보다 작아서 못 쓴 것이다 — 뒤에 잇는다(순서는 논리 위치가 정한다).
    proven_size_t want = n > (proven_size_t)LOW_HEAP_CHUNK ? n : (proven_size_t)LOW_HEAP_CHUNK;
    proven_u8 *blk = (proven_u8 *)malloc(want);
    if (!blk) return NULL;
    // 지금 청크 뒤의 (작아서 못 쓴) 청크들은 버린다 — 논리 위치가 단조로워야 되감기가 선다.
    for (proven_size_t i = vm->hpcur + 1; i < vm->hpn; i++) free(vm->hpch[i]);
    proven_size_t i = vm->hpn ? vm->hpcur + 1 : 0;
    vm->hpch[i] = blk; vm->hpcap[i] = want;
    vm->hpbase[i] = i ? vm->hpbase[i - 1] + vm->hpcap[i - 1] : 0;
    vm->hpn = i + 1; vm->hpcur = i; vm->hpoff = n;
    memset(blk, 0, n); return blk;
}
static proven_size_t vm_root_mark(const vm_ctx_t *vm, int root) {
    if (root == 0) return vm->fixn;
    return vm->hpn ? vm->hpbase[vm->hpcur] + vm->hpoff : 0;
}
// ★ 논리 위치 `to` 로 되돌린다(`region` 블록의 끝). 청크는 **버리지 않는다** — 다시 쓴다.
static bool vm_root_reset(vm_ctx_t *vm, int root, proven_size_t to) {
    if (to > vm_root_mark(vm, root)) return false;
    if (root == 0) { vm->fixn = to; return true; }
    for (proven_size_t i = vm->hpn; i-- > 0; )
        if (to >= vm->hpbase[i]) { vm->hpcur = i; vm->hpoff = to - vm->hpbase[i]; return true; }
    vm->hpcur = 0; vm->hpoff = 0; return true;
}
static void vm_roots_free(vm_ctx_t *vm) {
    free(vm->fixmem);
    for (proven_size_t i = 0; i < vm->hpn; i++) free(vm->hpch[i]);
    free(vm->hpch); free(vm->hpcap); free(vm->hpbase);
}

static bool vm_vec_outside(const vm_ctx_t *vm, proven_size_t base) {
    #define VM_VEC_HIT(v) ((v).tag == VMV_VEC && (proven_size_t)(v).box >= base)
    for (proven_size_t i = 0; i < vm->nbox; i++) if (VM_VEC_HIT(vm->boxes[i])) return true;
    for (proven_size_t i = 0; i < vm->nrecs; i++)
        for (proven_size_t f = 0; f < vm->recs[i].nfields; f++) if (VM_VEC_HIT(vm->recs[i].fields[f])) return true;
    for (proven_size_t i = 0; i < vm->nstk; i++)
        for (proven_size_t k = 0; k < vm->stks[i].n; k++) if (VM_VEC_HIT(vm->stks[i].items[k])) return true;
    for (proven_size_t i = 0; i < vm->nchan; i++)
        for (proven_size_t k = 0; k < vm->chans[i].count; k++)
            if (VM_VEC_HIT(vm->chans[i].buf[(vm->chans[i].head + k) % VM_CHAN_CAP])) return true;
    for (proven_size_t i = 0; i < vm->nmbox; i++)
        for (proven_u8 k = 0; k < vm->mbox[i].nargs; k++) if (VM_VEC_HIT(vm->mbox[i].args[k])) return true;
    for (proven_size_t i = 0; i < vm->njobs; i++) if (VM_VEC_HIT(vm->jobs[i].result)) return true;
    #undef VM_VEC_HIT
    return false;
}
// 되감기의 전제가 서는가 — 셋 다 **사실**이어야 한다(아니면 옛 규율 그대로 단조 증가한다).
static bool vm_vec_may_rewind(const vm_ctx_t *vm, proven_size_t base) {
    if (vm->nvec <= base) return false;      // 이 프레임이 만든 것이 없다 — 공짜로 빠진다
    if (vm->in_gsched) return false;         // 그린스레드들이 풀을 **공유**한다: 다른 흐름의 뿌리가 안 보인다
    return !vm_vec_outside(vm, base);
}
static void vm_vec_rewind(vm_ctx_t *vm, proven_size_t base,
                          vmv_t *locals, proven_size_t nloc, vmv_t *stack, proven_size_t sp) {
    if (!vm_vec_may_rewind(vm, base)) return;
    proven_i32 map[32];
    for (proven_size_t i = 0; i < 32; i++) map[i] = -1;
    proven_size_t top = base;
    for (proven_size_t old = base; old < vm->nvec; old++) {   // **오름차순** — 옮기는 자리는 늘 원래 자리 이하다
        bool live = false;
        for (proven_size_t i = 0; i < nloc && !live; i++)
            live = (locals[i].tag == VMV_VEC && (proven_size_t)locals[i].box == old);
        for (proven_size_t i = 0; i < sp && !live; i++)
            live = (stack[i].tag == VMV_VEC && (proven_size_t)stack[i].box == old);
        if (!live) continue;
        if (top != old) vm->vecs[top] = vm->vecs[old];
        map[old] = (proven_i32)top; top++;
    }
    for (proven_size_t i = 0; i < nloc; i++)
        if (locals[i].tag == VMV_VEC && (proven_size_t)locals[i].box >= base) locals[i].box = map[locals[i].box];
    for (proven_size_t i = 0; i < sp; i++)
        if (stack[i].tag == VMV_VEC && (proven_size_t)stack[i].box >= base) stack[i].box = map[stack[i].box];
    vm->nvec = top;
}
// 반환 — 콜리의 벡터는 콜리와 함께 죽는다. **돌려주는 벡터 하나**만 콜리의 바닥으로 내려온다
// (호출자의 문장이 그것을 쓰는 동안 산다). 레코드·박스 풀이 이미 쓰는 워터마크와 같은 규율이다.
static void vm_vec_ret(vm_ctx_t *vm, proven_size_t base, vmv_t *rv) {
    if (!vm_vec_may_rewind(vm, base)) return;
    if (rv->tag == VMV_VEC && (proven_size_t)rv->box >= base) {
        if ((proven_size_t)rv->box != base) vm->vecs[base] = vm->vecs[rv->box];
        rv->box = (proven_i32)base; vm->nvec = base + 1;
    } else {
        vm->nvec = base;
    }
}

// RFC-0052 S3 — 선언 폭의 표현 범위. NumericLattice.v 의 lo/hi 와 같은 정의.
proven_i64 ity_lo(proven_u8 bits, bool sign) {
    if (!sign) return 0;
    return bits >= 64 ? INT64_MIN : -((proven_i64)1 << (bits - 1));
}
proven_i64 ity_hi(proven_u8 bits, bool sign) {
    if (bits >= 64) return sign ? INT64_MAX : -1;   // 무부호 64: 비트패턴 전체(아래 fits 가 처리)
    return sign ? ((proven_i64)1 << (bits - 1)) - 1 : ((proven_i64)1 << bits) - 1;
}
// 수학적 결과 r 이 (bits, sign) 에 표현 가능한가. 64비트 무부호는 전체 비트패턴이 유효하다.
static bool ity_fits(proven_i64 r, proven_u8 bits, bool sign) {
    if (bits >= 64) return sign ? true : true;      // 64비트는 래핑 전 값이 이미 64비트
    return r >= ity_lo(bits, sign) && r <= ity_hi(bits, sign);
}
static proven_i64 ity_wrap(proven_i64 r, proven_u8 bits, bool sign) {
    if (bits >= 64) return r;
    proven_u64 m = ((proven_u64)1 << bits) - 1;
    proven_u64 v = (proven_u64)r & m;
    if (sign && (v & ((proven_u64)1 << (bits - 1)))) return (proven_i64)(v | ~m);   // 부호 확장
    return (proven_i64)v;
}
static proven_i64 ity_sat(proven_i64 r, proven_u8 bits, bool sign) {
    if (bits >= 64) return r;
    proven_i64 lo = ity_lo(bits, sign), hi = ity_hi(bits, sign);
    return r < lo ? lo : (r > hi ? hi : r);
}

static void vm_diag(proven_array_t *diags, const char *code, const char *msg) {
    low_diag_t d = { .sev = LOW_SEV_ERROR, .code = code, .msg = msg, .line = 0, .col = 0 };
    (void)proven_array_push(diags, &d);
}

// ★★★ **참조를 통과해 읽는다 — 그리고 통과할 때마다 빌림을 검사한다.**
//   (2026-07-19: 참조가 op 경계를 넘게 하는 조각. HANDOFF §5-2 가 "정직한 경계" 로 지목한 것.)
//
//   참조는 `{프레임 깊이, 슬롯, 세대, 태그}` 를 싣고 다니므로 **호출 경계를 넘어도 유효하다** —
//   빠져 있던 것은 참조 자체가 아니라, `field`/`index`/`len` 같은 **컨테이너 연산이 참조를
//   따라가지 않는다**는 것뿐이었다(그래서 "field access needs a record/view" 로 죽었다).
//
//   ★★★ **그리고 여기가 안전성이 사는 자리다.** 그냥 포인터로 낮췄다면 이 두 검사가 사라진다:
//     ① **세대**(dangling) — 참조가 자기 프레임보다 오래 살면 E-VM-DANGLING.
//     ② **빌림 스택**(readers-XOR-writer) — 충돌하는 접근이 있었으면 E-VM-EXCL.
//   `write` 를 정확히 넘겨야 ②가 제 일을 한다: 쓰기는 위의 빌림들을 무효화하고,
//   읽기는 그 위의 **가변** 빌림만 무효화한다(bstk_use).
//
//   ☞ 참조가 아니면 **그대로 돌려준다** — 그래서 호출부는 "참조일 수도 있다" 만 알면 된다.
static vmv_t vm_through(vm_ctx_t *vm, vmv_t v, bool write, bool *ok) {
    *ok = true;
    if (v.tag != VMV_REF) return v;
    vmframe_t *fr = &vm->frames[v.box];
    if (!fr->locs || fr->gen != (proven_u64)v.n) {
        vm_diag(vm->diags, "E-VM-DANGLING", "reference outlived its frame (use-after-return)");
        *ok = false; return v;
    }
    if (!bstk_use(fr, (proven_u8)(v.i & VM_REF_SLOT), VM_REF_TAG(v.i), write)) {
        vm_diag(vm->diags, "E-VM-EXCL",
                "borrow invalidated by a conflicting access (readers-XOR-writer)");
        *ok = false; return v;
    }
    // ★ 가변 접근인데 **공유 참조**로 왔다면 그것은 읽기 전용 위반이다.
    if (write && !(v.i & VM_REF_MUT)) {
        vm_diag(vm->diags, "E-VM-READONLY",
                "writing through a SHARED reference (`ref`) — take `mut_ref` to write");
        *ok = false; return v;
    }
    return fr->locs[v.i & VM_REF_SLOT];
}


// ★★★ **활성화 프레임** (RFC-0073 P1a, 2026-07-21) — 한 op 호출의 실행 상태.
//   지금은 vm_loop 의 C 지역(stack·sp·pc)에 흩어져 있어 호출이 곧 C 재귀가 된다(깊이가 C 스택에
//   갇힌다). 이 구조체로 묶어 두면, 다음 단계(P1b)에서 이걸 **힙의 명시적 스택**에 올려 C 재귀를
//   드라이버 루프로 바꿀 수 있다 — 그때 깊이가 힙으로 옮겨가 "자라는 스택" 이 된다.
//   ☞ P1a 는 **동작 불변**이다: act 를 C 지역으로 두고 vm_loop 은 여전히 재귀한다. 구조만 세운다.
typedef struct vm_act {
    struct vm_act      *parent;   // 호출자 프레임(NULL=이 드라이버의 진입). 자유 리스트도 이걸 잇는다.
    const low_ir_def_t *d;        // 실행 중인 op
    unsigned            pc;       // 다음 명령 위치(suspend/resume)
    unsigned            depth;    // 호출 깊이 — **ceiling 검사 전용**(더는 frames[] 색인이 아니다)
    proven_size_t       slot;     // ★ D3: 참조 추적 프레임 슬롯(frames[slot]). 그린스레드마다 유일 → 창 없음
    proven_size_t       sp;       // 피연산자 스택 포인터
    // ★ P1b: 이 프레임이 낸 **단순 호출**의 사후처리 상태(호출자에 저장, 반환 때 쓴다).
    proven_size_t       rec_wm, bs_wm, sk_wm, bx_wm;   // 레코드/비트셋/스택/박스 풀 워터마크(스칼라 반환 시 회수)
    proven_size_t       vec_base;   // ★ 이 프레임의 **벡터 풀 바닥**(RFC-0089 B) — 루프와 반환이 여기로 되감는다
    // ★ 그리고 나머지 풀의 바닥(F1) — 박스·레코드·스택·비트셋도 같은 규율로 되감긴다.
    proven_size_t       bx_base, rec_base, sk_base, bs_base;
    vmv_t               locals[VM_LOCALS];   // 지역 — 프레임에 인라인(힙 프레임이 호출을 넘어 산다)
    vmv_t               stack[VM_STACK];      // 피연산자 스택
} vm_act;

// vm_loop 결과: DONE(프레임 완료·값은 *ret) · CALL(단순 호출·out 으로 callee/cargs) · 에러는 false.
enum { VMOUT_DONE = 0, VMOUT_CALL = 1 };
static bool vm_loop(vm_ctx_t *vm, vm_act *a, vmv_t *ret, int *outcome,
                    const low_ir_def_t **out_callee, vmv_t *out_cargs);

// ★★★ **명시적 프레임 스택 — 힙에서 자라는 링크드 체인** (RFC-0073 P1b, 2026-07-21).
//   단순 호출(actor 아닌 일반 op)의 깊이가 이제 **C 스택이 아니라 힙의 프레임 체인**에 쌓인다.
//   ☞ **왜 배열이 아니라 체인인가**: 그린스레드들이 이 스택을 **공유**한다(중단된 A 의 프레임과
//     도는 B 의 프레임이 함께 산다). 배열을 realloc 로 키우면 **중단된 A 의 프레임이 이동**해
//     A 의 언어 지역·참조(frames[].locs)가 깨진다. 프레임을 **개별 malloc 해 부모 포인터로 잇고**
//     자유 리스트로 재활용하면 **절대 안 움직인다** — realloc 픽스업도, 별칭 재취득도 필요 없다.
static proven_size_t slot_alloc(vm_ctx_t *vm);   // ★ D3: 참조 프레임 슬롯(정의는 아래)
static void slot_free(proven_size_t s);
static vm_act *g_act_free;   // 자유 리스트(parent 로 잇는다) — 단일 스레드라 잠금 없음
static vm_act *act_alloc(void) {
    vm_act *a = g_act_free;
    if (a) g_act_free = a->parent; else a = malloc(sizeof *a);
    return a;
}
static void act_free(vm_act *a) { a->parent = g_act_free; g_act_free = a; }
// ★★★ **살아 있는 사슬은 하나가 아니다** (RFC-0089 후속 F1, 2026-08-04).
//
//   단계 B 의 벡터 되감기는 뿌리를 **현재 프레임의 지역과 피연산자 스택**으로 잡았다. 그런데
//   값은 그 프레임 밖으로도 나간다: `mut_ref` 로 **호출자의 지역에 써 넣을 수 있고**(G7
//   write-through), actor 핸들러는 **자기 사슬**을 따로 잡아 돈다(중첩 vm_exec). 그 자리들이
//   뿌리에서 빠지면 되감기는 회수가 아니라 **살아 있는 것을 밟는 일**이 된다.
//
//   ⇒ 드라이버가 자기 사슬의 **꼭대기 포인터를 등록**한다. 되감기는 등록된 사슬을 전부 훑는다.
//     (프레임은 개별 malloc 이라 안 움직인다 — 포인터를 들고 있어도 안전하다.)
#define VM_MAXCHAIN 64
static vm_act **g_chain[VM_MAXCHAIN];
static proven_size_t g_nchain;

// ★★★★ **유한 풀은 루프에서 마르지 않는다** (RFC-0089 후속 F1, 2026-08-04).
//
//   단계 B 가 벡터 풀에 되감기를 넣었고, **소크 게이트가 곧바로 다음 것을 잡았다**: 박스 풀
//   (option/result)은 용량 2× 에서 VM 만 죽는다(네이티브 빠른 경로는 option 을 (태그,값) 쌍으로
//   나르므로 풀이 아예 없다). 레코드·스택·비트셋도 같은 자리에 있다.
//
//   ☞ 벡터는 **잎**이었다(레인 배열뿐). 박스·레코드·스택은 **서로를 품는다** — 박스 안에 레코드
//     핸들이, 레코드 필드에 스택 핸들이 들어간다. 그래서 "새면 포기" 로는 안 되고 **전이 표시**가
//     필요하다: 뿌리에서 출발해 핸들을 따라가며 살아 있는 것을 표시하고, 그 다음 **압축**한다.
//
//   뿌리 = 등록된 **모든 활성 사슬**의 (지역 · 피연산자 스택). 그리고 프레임이 시작될 때의
//   바닥보다 **아래**는 손대지 않는다 — 그 아래는 호출자의 것이고, 그가 도는 동안 우리는 멈춰 있었다.
#define VM_PGC_MAXN 4096
typedef struct { proven_i32 *map; proven_size_t base, n; } vm_pmap_t;

static void vm_gc_mark(vm_ctx_t *vm, vmv_t v, vm_pmap_t *bx, vm_pmap_t *rc,
                       vm_pmap_t *sk, vm_pmap_t *bs, vm_pmap_t *vc);

static void vm_gc_mark_slot(vm_ctx_t *vm, vm_pmap_t *m, proven_i32 idx, vmv_t v,
                            vm_pmap_t *bx, vm_pmap_t *rc, vm_pmap_t *sk, vm_pmap_t *bs, vm_pmap_t *vc) {
    (void)v;
    if (idx < 0 || (proven_size_t)idx < m->base || (proven_size_t)idx >= m->n) return;
    if (m->map[idx] != -1) return;          // 이미 표시됨(순환도 여기서 멈춘다)
    m->map[idx] = -2;                        // -2 = 살아 있음(자리는 아직 안 정함)
    // 자식을 따라간다 — 이 풀이 무엇을 품는지는 풀마다 다르다.
    if (m == bx) vm_gc_mark(vm, vm->boxes[idx], bx, rc, sk, bs, vc);
    else if (m == rc) {
        for (proven_size_t f = 0; f < vm->recs[idx].nfields; f++)
            vm_gc_mark(vm, vm->recs[idx].fields[f], bx, rc, sk, bs, vc);
    } else if (m == sk) {
        for (proven_size_t k = 0; k < vm->stks[idx].n; k++)
            vm_gc_mark(vm, vm->stks[idx].items[k], bx, rc, sk, bs, vc);
    }
    // 비트셋·벡터는 **잎**이다(자식이 없다).
}

static void vm_gc_mark(vm_ctx_t *vm, vmv_t v, vm_pmap_t *bx, vm_pmap_t *rc,
                       vm_pmap_t *sk, vm_pmap_t *bs, vm_pmap_t *vc) {
    switch (v.tag) {
        case VMV_SOME: case VMV_OK:  vm_gc_mark_slot(vm, bx, v.box, v, bx, rc, sk, bs, vc); break;
        case VMV_REC:  case VMV_VIEW: vm_gc_mark_slot(vm, rc, v.box, v, bx, rc, sk, bs, vc); break;
        case VMV_STACK: vm_gc_mark_slot(vm, sk, v.box, v, bx, rc, sk, bs, vc); break;
        case VMV_BITSET: vm_gc_mark_slot(vm, bs, v.box, v, bx, rc, sk, bs, vc); break;
        case VMV_VEC:   vm_gc_mark_slot(vm, vc, v.box, v, bx, rc, sk, bs, vc); break;
        default: break;   // 정수·슬라이스·마스크·참조 …는 풀을 안 쓴다
    }
}

// 핸들 하나를 새 자리로 옮겨 적는다(압축이 정한 자리로).
static void vm_gc_fix(vmv_t *v, const vm_pmap_t *bx, const vm_pmap_t *rc,
                      const vm_pmap_t *sk, const vm_pmap_t *bs, const vm_pmap_t *vc) {
    const vm_pmap_t *m = NULL;
    switch (v->tag) {
        case VMV_SOME: case VMV_OK: m = bx; break;
        case VMV_REC: case VMV_VIEW: m = rc; break;
        case VMV_STACK: m = sk; break;
        case VMV_BITSET: m = bs; break;
        case VMV_VEC: m = vc; break;
        default: return;
    }
    if (v->box < 0 || (proven_size_t)v->box < m->base || (proven_size_t)v->box >= m->n) return;
    if (m->map[v->box] >= 0) v->box = m->map[v->box];
}

// ★ 등록된 모든 사슬의 뿌리에 f 를 먹인다(지역 · 피연산자 스택).
#define VM_FOR_ROOTS(FN) do { \
    for (proven_size_t ci_ = 0; ci_ < g_nchain; ci_++) { \
        if (!g_chain[ci_]) continue; \
        for (vm_act *a_ = *g_chain[ci_]; a_; a_ = a_->parent) { \
            for (proven_size_t li_ = 0; li_ < VM_LOCALS; li_++) FN(&a_->locals[li_]); \
            for (proven_size_t si_ = 0; si_ < a_->sp; si_++)     FN(&a_->stack[si_]); \
        } \
    } } while (0)

// ★★★ **네 풀을 함께 되감는다**(F1) — 표시는 전이적이고, 압축은 프레임 바닥 위에서만 일어난다.
//   답은 안 바뀐다: 핸들은 프로그램이 볼 수 없고, 옮겨진 뒤에도 같은 값이 같은 자리에 있다.
static void vm_pool_gc(vm_ctx_t *vm, const vm_act *a) {
    if (vm->in_gsched) return;              // 그린스레드가 돌면 다른 흐름의 뿌리가 안 보인다
    static proven_i32 mbx[VM_MAXBOX], mrc[VM_MAXREC], msk[VM_MAXSTK], mbs[VM_MAXBSET], mvc[32];
    vm_pmap_t bx = { mbx, a->bx_base, vm->nbox }, rc = { mrc, a->rec_base, vm->nrecs },
              sk = { msk, a->sk_base, vm->nstk }, bs = { mbs, a->bs_base, vm->nbset },
              vc = { mvc, a->vec_base, vm->nvec };
    if (bx.n <= bx.base && rc.n <= rc.base && sk.n <= sk.base &&
        bs.n <= bs.base && vc.n <= vc.base) return;      // 이 프레임이 만든 것이 없다 — 공짜
    vm_pmap_t *all[5] = { &bx, &rc, &sk, &bs, &vc };
    for (int q = 0; q < 5; q++)
        for (proven_size_t i = all[q]->base; i < all[q]->n; i++) all[q]->map[i] = -1;

    // ① 표시 — 뿌리에서 출발해 핸들을 따라간다.
    #define MARK_ONE(pv) vm_gc_mark(vm, *(pv), &bx, &rc, &sk, &bs, &vc)
    VM_FOR_ROOTS(MARK_ONE);
    #undef MARK_ONE
    //    바닥 **아래**의 것들도 뿌리다 — 그들이 위쪽 것을 품고 있을 수 있다(호출자가 만든 상자가
    //    우리 프레임에서 만든 레코드를 가리키는 경우: `set` 이 그렇게 쓴다).
    for (proven_size_t i = 0; i < bx.base; i++) vm_gc_mark(vm, vm->boxes[i], &bx, &rc, &sk, &bs, &vc);
    for (proven_size_t i = 0; i < rc.base; i++)
        for (proven_size_t f = 0; f < vm->recs[i].nfields; f++)
            vm_gc_mark(vm, vm->recs[i].fields[f], &bx, &rc, &sk, &bs, &vc);
    for (proven_size_t i = 0; i < sk.base; i++)
        for (proven_size_t k = 0; k < vm->stks[i].n; k++)
            vm_gc_mark(vm, vm->stks[i].items[k], &bx, &rc, &sk, &bs, &vc);
    for (proven_size_t i = 0; i < vm->nchan; i++)
        for (proven_size_t k = 0; k < vm->chans[i].count; k++)
            vm_gc_mark(vm, vm->chans[i].buf[(vm->chans[i].head + k) % VM_CHAN_CAP], &bx, &rc, &sk, &bs, &vc);
    for (proven_size_t i = 0; i < vm->nmbox; i++)
        for (proven_u8 k = 0; k < vm->mbox[i].nargs; k++)
            vm_gc_mark(vm, vm->mbox[i].args[k], &bx, &rc, &sk, &bs, &vc);
    for (proven_size_t i = 0; i < vm->njobs; i++)
        vm_gc_mark(vm, vm->jobs[i].result, &bx, &rc, &sk, &bs, &vc);

    // ② 자리 정하기 — 살아 있는 것에 **오름차순**으로 새 자리를 준다(옮기는 자리는 늘 원래 자리 이하).
    proven_size_t ntop[5];
    for (int q = 0; q < 5; q++) {
        proven_size_t top = all[q]->base;
        for (proven_size_t i = all[q]->base; i < all[q]->n; i++)
            if (all[q]->map[i] == -2) all[q]->map[i] = (proven_i32)top++;
        ntop[q] = top;
    }
    // ③ 옮기기
    for (proven_size_t i = bx.base; i < bx.n; i++) if (mbx[i] >= 0 && (proven_size_t)mbx[i] != i) vm->boxes[mbx[i]] = vm->boxes[i];
    for (proven_size_t i = rc.base; i < rc.n; i++) if (mrc[i] >= 0 && (proven_size_t)mrc[i] != i) vm->recs[mrc[i]]  = vm->recs[i];
    for (proven_size_t i = sk.base; i < sk.n; i++) if (msk[i] >= 0 && (proven_size_t)msk[i] != i) vm->stks[msk[i]]  = vm->stks[i];
    for (proven_size_t i = bs.base; i < bs.n; i++) if (mbs[i] >= 0 && (proven_size_t)mbs[i] != i) { vm->bsets[mbs[i]] = vm->bsets[i]; vm->bwid[mbs[i]] = vm->bwid[i]; }
    for (proven_size_t i = vc.base; i < vc.n; i++) if (mvc[i] >= 0 && (proven_size_t)mvc[i] != i) vm->vecs[mvc[i]]  = vm->vecs[i];

    // ④ 핸들 고쳐 쓰기 — 뿌리와, 옮겨진 것들의 자식.
    #define FIX_ONE(pv) vm_gc_fix((pv), &bx, &rc, &sk, &bs, &vc)
    VM_FOR_ROOTS(FIX_ONE);
    for (proven_size_t i = 0; i < ntop[0]; i++) FIX_ONE(&vm->boxes[i]);
    for (proven_size_t i = 0; i < ntop[1]; i++)
        for (proven_size_t f = 0; f < vm->recs[i].nfields; f++) FIX_ONE(&vm->recs[i].fields[f]);
    for (proven_size_t i = 0; i < ntop[2]; i++)
        for (proven_size_t k = 0; k < vm->stks[i].n; k++) FIX_ONE(&vm->stks[i].items[k]);
    for (proven_size_t i = 0; i < vm->nchan; i++)
        for (proven_size_t k = 0; k < vm->chans[i].count; k++) FIX_ONE(&vm->chans[i].buf[(vm->chans[i].head + k) % VM_CHAN_CAP]);
    for (proven_size_t i = 0; i < vm->nmbox; i++)
        for (proven_u8 k = 0; k < vm->mbox[i].nargs; k++) FIX_ONE(&vm->mbox[i].args[k]);
    for (proven_size_t i = 0; i < vm->njobs; i++) FIX_ONE(&vm->jobs[i].result);
    #undef FIX_ONE

    vm->nbox = ntop[0]; vm->nrecs = ntop[1]; vm->nstk = ntop[2]; vm->nbset = ntop[3]; vm->nvec = ntop[4];
}

// 진입 프레임(및 그 위 단순 호출들)을 완주시키는 **드라이버**. 중첩 vm_exec(actor·메시지)는
// 자기 체인을 따로 잡아 돈다 — 프레임이 안 움직이니 서로 안 밟는다.
static bool vm_exec(vm_ctx_t *vm, const low_ir_def_t *d,
                    const vmv_t *args, vmv_t *ret, unsigned depth) {
    // ★ 경계는 이 사슬의 ceiling 프레임(메인=VM_MAXCALL · 그린스레드=base+GT_DEPTH · 넘으면 값).
    if (depth >= vm->cur_ceiling) { vm_diag(vm->diags, "E-VM-DEPTH", "call depth limit exceeded"); return false; }
    if (!d->lowered)      { vm_diag(vm->diags, "E-VM-UNSUP", "op body is outside the S5 core"); return false; }
    vm_act *top = act_alloc();
    if (!top) { vm_diag(vm->diags, "E-VM-OOM", "out of memory growing the call stack"); return false; }
    // ★ 이 사슬을 등록한다(F1) — 되감기가 **모든** 살아 있는 뿌리를 본다.
    proven_size_t chain_slot = g_nchain;
    // ★★★★★ **등록 못 하면 거절한다** (2026-08-30, WO-0150). 전엔 조용히 `chain_slot = -1` 로
    //   두고 계속 갔다 — 그러면 이 활성 사슬이 **되감기의 뿌리 목록에 없다**(RFC-0089 F1).
    //   되감기는 *"살아 있는 뿌리를 **전부** 본다"* 를 전제로 회수하므로, 안 보이는 뿌리는
    //   **살아 있는 값을 회수당할 수 있다.** 검사가 줄어드는 게 아니라 **틀릴 수 있는** 자리다.
    //   ⇒ 자르지 않고 거절한다. (실측 최고수위는 코퍼스에서 이 자리에 닿지 않는다.)
    if (g_nchain >= VM_MAXCHAIN) {
        vm_diag(vm->diags, "E-VM-CHAIN-LIMIT",
                "too many nested VM activations to register this one for rewind — refusing is the "
                "honest answer: an unregistered chain is invisible to reclamation, and a live value "
                "could be reclaimed under it");
        act_free(top);
        return false;
    }
    g_chain[g_nchain++] = &top; LOW_HWM("vm:rewind-chain", g_nchain, VM_MAXCHAIN);
    #define VM_CHAIN_LEAVE() do { if (chain_slot != (proven_size_t)-1 && g_nchain == chain_slot + 1) g_nchain = chain_slot; \
                                  else if (chain_slot != (proven_size_t)-1) g_chain[chain_slot] = NULL; } while (0)
    proven_size_t s0 = slot_alloc(vm);
    if (s0 == (proven_size_t)-1) { act_free(top); { VM_CHAIN_LEAVE(); return false; } }
    top->parent = NULL; top->d = d; top->pc = 0; top->depth = depth; top->sp = 0; top->slot = s0;
    top->vec_base = vm->nvec; top->bx_base = vm->nbox; top->rec_base = vm->nrecs;
    top->sk_base = vm->nstk; top->bs_base = vm->nbset;
    memset(top->locals, 0, sizeof top->locals);
    for (proven_size_t i = 0; i < d->nparams; i++) top->locals[i] = args[i];
    vm->frames[top->slot].locs = top->locals; vm->frames[top->slot].gen = ++vm->gcount; vm->frames[top->slot].nbstk = 0;
    for (;;) {
        vmv_t rv = vmv_int(0);
        int outcome = VMOUT_DONE; const low_ir_def_t *callee = NULL; vmv_t cargs[VM_LOCALS];
        bool ok = vm_loop(vm, top, &rv, &outcome, &callee, cargs);
        if (!ok) {   // 에러 — 이 드라이버의 체인을 전부 걷고 실패 전파
            while (top) { vm->frames[top->slot].locs = NULL; vm->frames[top->slot].gen = 0; slot_free(top->slot);
                          vm_act *p = top->parent; act_free(top); top = p; }
            { VM_CHAIN_LEAVE(); return false; }
        }
        if (outcome == VMOUT_CALL) {
            unsigned nd = top->depth + 1;
            vm_act *fc = (nd >= vm->cur_ceiling) ? NULL : act_alloc();
            proven_size_t sc = fc ? slot_alloc(vm) : (proven_size_t)-1;
            if (nd >= vm->cur_ceiling) vm_diag(vm->diags, "E-VM-DEPTH", "call depth limit exceeded");
            else if (!fc || sc == (proven_size_t)-1) vm_diag(vm->diags, "E-VM-OOM", "out of memory growing the call stack");
            if (!fc || sc == (proven_size_t)-1) {   // 깊이 초과(값) 또는 OOM — 체인을 걷고 실패
                if (fc) act_free(fc);
                while (top) { vm->frames[top->slot].locs = NULL; vm->frames[top->slot].gen = 0; slot_free(top->slot);
                              vm_act *p = top->parent; act_free(top); top = p; }
                { VM_CHAIN_LEAVE(); return false; }
            }
            fc->parent = top; fc->d = callee; fc->pc = 0; fc->depth = nd; fc->sp = 0; fc->slot = sc;
            fc->vec_base = vm->nvec; fc->bx_base = vm->nbox; fc->rec_base = vm->nrecs;
            fc->sk_base = vm->nstk; fc->bs_base = vm->nbset;
            memset(fc->locals, 0, sizeof fc->locals);
            for (proven_size_t i = 0; i < callee->nparams; i++) fc->locals[i] = cargs[i];
            vm->frames[sc].locs = fc->locals; vm->frames[sc].gen = ++vm->gcount; vm->frames[sc].nbstk = 0;
            top = fc;   // 호출자는 자기 호출 pc 에 멈춰 있다 — 콜리가 DONE 이면 값 얹고 재개
        } else {   // VMOUT_DONE — 이 프레임 완료
            vm->frames[top->slot].locs = NULL; vm->frames[top->slot].gen = 0; slot_free(top->slot);
            vm_act *done = top; top = top->parent;
            vm_vec_ret(vm, done->vec_base, &rv);   // ★ 벡터 풀 되감기(RFC-0089 B) — 콜리의 벡터는 콜리와 함께 죽는다
            if (!top) { *ret = rv; act_free(done); { VM_CHAIN_LEAVE(); return true; } }   // 진입 프레임이 끝났다
            // 스칼라 반환이면 콜리가 만든 레코드/박스 풀을 회수(호출 때 저장한 워터마크로).
            if (rv.tag == VMV_INT || rv.tag == VMV_FLT) {
                vm->nrecs = top->rec_wm; vm->nbset = top->bs_wm; vm->nstk = top->sk_wm; vm->nbox = top->bx_wm;
            }
            if (top->sp >= VM_STACK) { vm_diag(vm->diags, "E-VM-STACK", "operand stack overflow");
                act_free(done);
                while (top) { vm->frames[top->slot].locs = NULL; vm->frames[top->slot].gen = 0; slot_free(top->slot);
                              vm_act *p = top->parent; act_free(top); top = p; }
                { VM_CHAIN_LEAVE(); return false; } }
            top->stack[top->sp++] = rv;
            top->pc++;   // 호출 명령 다음으로 재개
            act_free(done);
        }
    }
}

// ══ green thread — 코루틴 스케줄러 (RFC-0009 2/3, VM 오라클 전용) ═══════════════════
// ★★★ 각 실행 단위(태스크·메시지)가 자기 ucontext 스택에서 vm_exec 한다. `yield` 는 스케줄러로
//   swapcontext(중단); 스케줄러가 오라클 계획대로 ready gthread 를 골라 재개한다. 협조적(한 번에
//   하나, OS 스레드 하나 — 경합 없음). 네이티브는 yield=no-op(오라클이 순서 무관을 증명한 것만 통과).
//   ★ frame 충돌 회피: gthread gi 는 base depth gi*GT_DEPTH 에서 돈다(태스크 재귀 < GT_DEPTH 전제).
//   makecontext 는 포인터 인자를 못 넘기므로(이식성) 전역으로 넘긴다 — vm 은 static·단일 스레드다.
// ★★★ **동적 그린스레드 백킹** (E-Alloc P3, 2026-07-21) — 상한을 없앤다.
//   전엔 gthr·frames·스택이 VM_GTHR(=16) 고정 배열이라, 동시에 16개 넘게 **막히면** 거절했다.
//   ⇒ 이 넷을 **성장하는 파일 스코프 백킹**으로 뺀다: vm 은 static·단일 스레드라 파일 스코프가
//     안전하고, `--test` 오라클이 vm 을 memset 으로 재사용해도 백킹은 **고수위로 살아남아**
//     반복마다 새지 않는다(never-free, 프로세스 종료 시 회수). vm->frames/gthr 는 이 백킹을 가리킨다.
//   ☞ frames realloc 은 배열을 옮기지만 **안전**하다: 참조는 절대깊이(.box) **인덱스**라 이동에
//     무관하고, 성장은 스케줄러 refill 시점(모든 그린스레드가 블록 지점에 있어 vmframe_t* 를 안
//     쥔 때)에만 일어난다. 블록 지점(chsend/chrecv/yield/await)은 stack·chans 만 만진다.
struct gthr_s;   // 전방(아래 헬퍼가 vm_ctx_t 정의 뒤에 오므로 태그는 이미 보인다)
static char        **g_gt_stk;      // [g_gthr_cap] 각각 GT_STK 바이트(malloc)
static vmframe_t    *g_frames;      static proven_size_t g_frames_cap;   // 프레임 수
static struct gthr_s *g_gthr_pool;  static proven_size_t g_gthr_cap;     // 그린스레드 슬롯 수
static proven_size_t *g_ready;      // [g_gthr_cap] 스케줄러 runnable 목록
static vm_ctx_t   *g_gt_vm;
static proven_size_t g_gt_idx;
static bool        g_gt_err;

// 백킹이 need_slots 그린스레드를 담도록 보장(필요하면 성장). vm->frames/gthr 를 백킹에 물린다.
// memset 후에도 매번 부른다 — vm 의 포인터 필드가 0 이 되므로 다시 물려야 한다.
static bool vm_conc_ensure(vm_ctx_t *vm, proven_size_t need_slots) {
    if (need_slots > g_gthr_cap) {
        proven_size_t nc = g_gthr_cap ? g_gthr_cap : VM_GTHR;
        while (nc < need_slots) nc *= 2;
        proven_size_t nframes = VM_MAXCALL + nc * GT_DEPTH + 2;
        vmframe_t    *nf = realloc(g_frames, nframes * sizeof *nf);
        struct gthr_s *ng = realloc(g_gthr_pool, nc * sizeof *ng);
        proven_size_t *nr = realloc(g_ready, nc * sizeof *nr);
        char        **ns = realloc(g_gt_stk, nc * sizeof *ns);
        if (!nf || !ng || !nr || !ns) { vm_diag(vm->diags, "E-VM-OOM", "out of memory growing the green-thread pool"); return false; }
        memset((char *)nf + g_frames_cap * sizeof *nf, 0, (nframes - g_frames_cap) * sizeof *nf);
        memset((char *)ng + g_gthr_cap   * sizeof *ng, 0, (nc - g_gthr_cap) * sizeof *ng);
        for (proven_size_t i = g_gthr_cap; i < nc; i++) { ns[i] = malloc(GT_STK); if (!ns[i]) { vm_diag(vm->diags, "E-VM-OOM", "out of memory allocating a green-thread stack"); return false; } }
        g_frames = nf; g_frames_cap = nframes; g_gthr_pool = ng; g_gthr_cap = nc; g_ready = nr; g_gt_stk = ns;
    }
    vm->frames = g_frames; vm->gthr = g_gthr_pool;   // memset 후 재연결
    return true;
}

// ★★★ **참조 프레임 슬롯 할당기** (RFC-0073 D3, 2026-07-21) — 참조를 깊이가 아니라 **슬롯**으로
//   인코딩하려고, frames[] 를 slot 으로 색인하고 프레임마다 유일한 슬롯을 준다. ⇒ 그린스레드가
//   깊이-창을 안 겹쳐 쓰려고 base 를 벌릴 필요가 없다(창 제거). 자유 리스트로 재활용, 넘치면 성장.
static proven_size_t *g_slot_free; static proven_size_t g_slot_free_n, g_slot_hw, g_slot_free_cap;
static proven_size_t slot_alloc(vm_ctx_t *vm) {
    if (g_slot_free_n > 0) return g_slot_free[--g_slot_free_n];
    proven_size_t s = g_slot_hw;
    if (s >= g_frames_cap) {   // 참조 프레임 풀을 키운다(frames[])
        proven_size_t nc = g_frames_cap ? g_frames_cap * 2 : 256;
        while (nc <= s) nc *= 2;
        vmframe_t *nf = realloc(g_frames, nc * sizeof *nf);
        if (!nf) { vm_diag(vm->diags, "E-VM-OOM", "out of memory growing the reference-frame pool"); return (proven_size_t)-1; }
        memset((char *)nf + g_frames_cap * sizeof *nf, 0, (nc - g_frames_cap) * sizeof *nf);
        g_frames = nf; g_frames_cap = nc; vm->frames = g_frames;
    }
    // 자유 리스트는 **독립적으로** 크기 관리한다(frames[] 는 vm_conc_ensure 도 키우므로 따로다).
    if (g_slot_hw + 1 > g_slot_free_cap) {
        proven_size_t nc = g_slot_free_cap ? g_slot_free_cap * 2 : 256;
        while (nc < g_slot_hw + 1) nc *= 2;
        proven_size_t *nsf = realloc(g_slot_free, nc * sizeof *nsf);
        if (!nsf) { vm_diag(vm->diags, "E-VM-OOM", "out of memory growing the slot free-list"); return (proven_size_t)-1; }
        g_slot_free = nsf; g_slot_free_cap = nc;
    }
    g_slot_hw = s + 1;
    return s;
}
static void slot_free(proven_size_t s) { g_slot_free[g_slot_free_n++] = s; }

static void gthr_trampoline(void) {
    vm_ctx_t *vm = g_gt_vm;
    proven_size_t gi = g_gt_idx;
    struct gthr_s *g = &vm->gthr[gi];
    vmv_t cargs[VM_LOCALS]; proven_u8 base = 0;
    if (g->box >= 0) cargs[base++] = (vmv_t){ .tag = VMV_REC, .box = g->box };
    for (proven_u8 x = 0; x < g->nargs; x++) cargs[base + x] = g->args[x];
    vmv_t rv;
    if (!vm_exec(vm, &vm->ir->defs[g->handler], cargs, &rv, g->base)) g_gt_err = true;   // ★ 창 시작 = g->base (메인 위)
    vm->gthr[gi].state = 4;   // 완료 — uc_link(sched_ctx) 로 자동 복귀
    // ★ job 결과를 담고 await 하던 태스크를 깨운다(완료가 awaiter 를 runnable 로).
    if (vm->gthr[gi].job >= 0) {
        proven_size_t jid = (proven_size_t)vm->gthr[gi].job;
        vm->jobs[jid].result = rv; vm->jobs[jid].done = 1; vm->gprogress++;
        for (proven_size_t q = 0; q < vm->ngthr; q++)
            if (vm->gthr[q].state == 6 && vm->gthr[q].blk_job == (proven_i32)jid) vm->gthr[q].state = 2;
    }
    // ★★★ **cancel_on_error** (SC4) — 이 자식이 **오류 result** 로 끝났고 취소 스코프면, 형제(안 시작·블록)를
    //   취소한다: done 처리하고 그 오류를 job result 로 채운다(await 하면 오류가 보인다 — 표면화는 await 로).
    if (g_cancel_scope && rv.tag == VMV_ERR) {
        for (proven_size_t q = 0; q < vm->ngthr; q++) {
            if (q == gi) continue;
            if (vm->gthr[q].state == 1 || vm->gthr[q].state == 5 || vm->gthr[q].state == 6) {
                vm->gthr[q].state = 4;   // 취소 → done(스케줄러가 건너뛴다; 블록된 코루틴은 버린다)
                if (vm->gthr[q].job >= 0) {
                    proven_size_t cj = (proven_size_t)vm->gthr[q].job;
                    vm->jobs[cj].result = rv; vm->jobs[cj].done = 1;
                    for (proven_size_t w = 0; w < vm->ngthr; w++)
                        if (vm->gthr[w].state == 6 && vm->gthr[w].blk_job == (proven_i32)cj) vm->gthr[w].state = 2;
                }
            }
        }
    }
}

// 전역 메일박스를 **코루틴 스케줄러**로 비운다(quiescence 까지). yield 가 있으면 진짜 인터리빙.
static bool vm_gsched(vm_ctx_t *vm, unsigned base_depth) {
    bool prev = vm->in_gsched; vm->in_gsched = true;
    // ★★★ **그린스레드 창은 메인의 살아있는 프레임 위에서 시작한다** (E-Alloc P1, 2026-07-21).
    //   메인이 depth D 에서 task_group 을 만나면 frames[0..D] 가 살아있다 — 그린스레드가 base 0
    //   부터 돌면 그것을 덮어, task_group 후 메인 지역의 참조가 댕글링으로 **오판**된다(실측 발산).
    //   ⇒ sched_base = D+1. 그린스레드 slot 은 sched_base + slot*GT_DEPTH 에서 돈다(겹침 없음).
    unsigned prev_ceiling = vm->cur_ceiling;
    vm->sched_base = base_depth;
    g_gt_err = false;
    proven_size_t rr = 0;      // round-robin 커서(기본 pick)
    for (;;) {
        // 1) mbox 의 대기 항목을 gthread 슬롯으로 흡수(도착 순서). 새로 온 것도 다음 라운드에 흡수.
        // ★★★ **게으른 재활용**: 상한 전엔 새 자리, **가득 찼을 때만** 끝난 자리(state 4)를 쓴다 (2026-07-21).
        //   ☞ 전엔 `ngthr < VM_GTHR` 만 봐 가득 차면 끝난 자리를 **안 재활용**했다 — 그래서 상한을
        //     넘겨 사는 태스크(실측: consumer 를 상한 절반 넘게 먼저 spawn)가 조용히 **데드락으로
        //     오진**됐다. 네이티브도 같은 결함이었다 — 둘을 같은 규율로 맞춘다.
        //   ☞ **게으르게** 하는 이유: 상한 전엔 새 자리를 써 슬롯 배정을 안 흔든다 ⇒ 상한 미만
        //     프로그램의 인터리빙 열거가 **불변**이다(재활용이 슬롯 인덱스를 바꾸면 오라클이 세는
        //     수가 달라진다). 재활용은 **정말 가득 찼을 때만** 개입한다.
        while (vm->nmbox > 0) {
            proven_size_t gi = (proven_size_t)-1;
            if (vm->ngthr < g_gthr_cap) { gi = vm->ngthr++; }
            else {
                // 가득: 먼저 끝난 자리(state 4)를 재활용한다(메모리를 안 늘리고 흔한 경우를 흡수).
                for (proven_size_t q = 0; q < vm->ngthr; q++) if (vm->gthr[q].state == 4) { gi = q; break; }
                // ★★★ **가득 + 다 바쁨 → 성장한다**(E-Alloc P3). 상한이 없다 — 동시에 막힌 태스크가
                //   수천이어도 백킹을 키워 담는다. 오직 **메모리**만 못 늘리면(OOM) 멈춘다(정직한 실패).
                //   ☞ realloc 이 frames 를 옮겨도 안전: 참조는 절대깊이 인덱스라 이동 무관, 여기(refill)는
                //     모든 그린스레드가 블록 지점에 있어 vmframe_t* 를 안 쥔 때다.
                if (gi == (proven_size_t)-1) {
                    if (!vm_conc_ensure(vm, g_gthr_cap + 1)) {
                        vm->ngthr = 0; vm->in_gsched = prev; vm->cur_ceiling = prev_ceiling; return false; }
                    gi = vm->ngthr++;
                }
            }
            struct gthr_s *g = &vm->gthr[gi];
            g->box = vm->mbox[0].box; g->handler = vm->mbox[0].handler; g->nargs = vm->mbox[0].nargs;
            g->job = vm->mbox[0].job;
            // ★ 창 시작 = sched_base + slot*GT_DEPTH. **슬롯 인덱스**로 정한다(재활용해도 창이 서로소).
            // ★★★ **그린스레드는 메인 사슬 깊이(sched_base)부터 센다** (RFC-0073, 2026-07-21).
            //   D3(슬롯 참조)로 프레임 창은 사라졌지만, **깊이 카운터**는 메인이 멈춘 자리부터
            //   이어야 네이티브와 정확히 맞는다: 네이티브의 협조적(peer=1) 태스크는 메인 스레드에서
            //   lw_depth 를 메인 체인 위로 이어 센다. gi*GT_DEPTH 창 오프셋은 없앤다(슬롯이 대신한다).
            g->base = vm->sched_base;
            for (proven_u8 x = 0; x < g->nargs; x++) g->args[x] = vm->mbox[0].args[x];
            g->state = 1;   // 새로 시작
            for (proven_size_t m = 0; m + 1 < vm->nmbox; m++) vm->mbox[m] = vm->mbox[m + 1];
            vm->nmbox--;
        }
        // 2) **runnable**(새·자발중단) gthread 를 모아 오라클 계획대로 하나 고른다. 채널 블록(5)은 제외 —
        //   그 태스크는 채널 op 성공이 깨워야만 runnable 이 된다(스케줄러가 막힌 태스크를 헛돌리지 않는다).
        proven_size_t *ready = g_ready, nready = 0;   // ★ 동적(g_gthr_cap 만큼) — E-Alloc P3
        for (proven_size_t i = 0; i < vm->ngthr; i++)
            if (vm->gthr[i].state == 1 || vm->gthr[i].state == 2) ready[nready++] = i;
        if (nready == 0) {
            // ★ refill 이 mbox 를 **항상** 비운다(자리가 모자라면 성장 — E-Alloc P3). 그래서 여기서
            //   nmbox 가 남는 일은 없다: runnable 도 없고 남은 건 블록뿐이면 그건 **진짜 데드락**이다.
            //   (전엔 상한 초과를 데드락과 구분해 E-VM-CONC-CAP 을 냈지만, 상한이 사라져 그 경우가 없다.)
            for (proven_size_t i = 0; i < vm->ngthr; i++)
                if (vm->gthr[i].state == 5 || vm->gthr[i].state == 6) {
                    vm_diag(vm->diags, "E-VM-DEADLOCK",
                            "the scheduler is stuck — every remaining task is blocked on a channel "
                            "(a `chrecv` on an empty channel or `chsend` on a full one) and none can wake "
                            "another. This is a real deadlock, not a tool limit: no interleaving completes");
                    vm->ngthr = 0; vm->in_gsched = prev; vm->cur_ceiling = prev_ceiling; return false;
                }
            break;   // quiescence
        }
        // ★ 기본 pick 은 **round-robin** — 블록된(방금 yield 한) 태스크만 계속 고르면 진짜 진행 못 하는
        //   프로그램도 막힌다(consumer 먼저 두면 chrecv 에서 영원히). rr 로 돌려 sender 에게도 차례를 준다.
        //   오라클이 계획을 준 자리(prefix)에서는 계획이 이긴다 — 그래야 인터리빙을 체계적으로 연다.
        proven_size_t pick;
        if (g_sched_step < SCHED_MAXSTEP) {
            g_sched_sizes[g_sched_step] = (proven_u16)nready;
            if (g_sched_step < g_sched_plan_len) {
                proven_size_t ch = (proven_size_t)g_sched_plan[g_sched_step];
                pick = (ch < nready) ? ch : (rr % nready);
            } else {
                pick = rr % nready;   // 계획 밖 — round-robin
            }
            g_sched_step++;
        } else {
            pick = rr % nready;
        }
        rr++;
        proven_size_t gi = ready[pick];
        struct gthr_s *g = &vm->gthr[gi];
        vm->cur_gthr = gi;
        // ★ 이 그린스레드를 돌리기 전에 ceiling 을 그 창으로 맞춘다(재개든 새로든 매번 — 사이에 다른
        //   그린스레드가 자기 ceiling 을 세워놨을 수 있다). 창을 넘으면 형제 프레임을 덮는다.
        vm->cur_ceiling = VM_MAXCALL;   // ★ 그린스레드도 메인과 같은 한계(창 없음 — D3 슬롯 참조)
        if (g->state == 1) {                       // 새로 시작 — makecontext
            getcontext(&g->ctx);
            g->ctx.uc_stack.ss_sp = g_gt_stk[gi];
            g->ctx.uc_stack.ss_size = GT_STK;
            g->ctx.uc_link = &vm->sched_ctx;
            g->state = 3;
            g_gt_vm = vm; g_gt_idx = gi;
            makecontext(&g->ctx, gthr_trampoline, 0);
            swapcontext(&vm->sched_ctx, &g->ctx);
        } else {                                    // state==2 중단 → 재개
            g->state = 3;
            swapcontext(&vm->sched_ctx, &g->ctx);
        }
        if (g_gt_err) { vm->ngthr = 0; vm->in_gsched = prev; vm->cur_ceiling = prev_ceiling; return false; }
    }
    vm->ngthr = 0; vm->in_gsched = prev; vm->cur_ceiling = prev_ceiling;
    return true;
}

// ★ P1b: 한 프레임을 **DONE·CALL·에러** 중 하나까지 돌린다(값 반환은 *ret, 단순 호출은 out_*).
static bool vm_loop(vm_ctx_t *vm, vm_act *a, vmv_t *ret, int *outcome,
                    const low_ir_def_t **out_callee, vmv_t *out_cargs) {
    // ★ 상태를 활성화 프레임에서 **별칭**한다 — 본문 277곳의 `stack[`·`sp` 는 안 건드린다.
    //   stack 은 포인터 별칭이라 변경이 act 에 남고, sp 는 지역 사본(단순 호출 suspend 에서 a->sp 로
    //   써넣는다). d·locals·depth 는 이 사슬 내내 불변이라 그냥 읽는다.
    *outcome = VMOUT_DONE;
    vmv_t *stack = a->stack;
    proven_size_t sp = a->sp;
    const low_ir_def_t *d = a->d;
    vmv_t *locals = a->locals;
    unsigned depth = a->depth;
    proven_size_t slot = a->slot;   // ★ D3: 참조 추적은 slot 으로 색인(depth 아님) — 그린스레드 창 없음
    for (proven_size_t pc = a->pc; pc < d->ncode; pc++) {
        if (vm->budget && ++vm->steps > vm->budget) {   // ★ 예산 초과 — 조용히 끝내지 않는다
            vm->budget_hit = true;
            vm_diag(vm->diags, "E-VM-BUDGET", "step budget exhausted (oracle run only)");
            return false;
        }
        const low_ir_ins_t *in = &d->code[pc];
        switch (in->w) {
            case IRW_CONST: if (sp >= VM_STACK) { vm_diag(vm->diags, "E-VM-STACK", "operand stack overflow"); return false; }
                            stack[sp++] = vmv_int(in->a); break;
            case IRW_LOAD:  if (sp >= VM_STACK) { vm_diag(vm->diags, "E-VM-STACK", "operand stack overflow"); return false; }
                            bstk_owner_read(&vm->frames[slot], (proven_u8)in->a);   // owner read invalidates uniques
                            stack[sp++] = locals[in->a]; break;
            case IRW_STORE: {
                if (sp == 0) return false;
                vmv_t v = stack[--sp], cur = locals[in->a];
                if (cur.tag == VMV_REF && v.tag != VMV_REF) {
                    // G7 write-through: `set <mut_ref-typed place> v` writes the referent
                    if (!(cur.i & VM_REF_MUT)) { vm_diag(vm->diags, "E-VM-READONLY", "write through a shared ref (needs mut_ref)"); return false; }
                    vmframe_t *fr = &vm->frames[cur.box];
                    if (!fr->locs || fr->gen != (proven_u64)cur.n) { vm_diag(vm->diags, "E-VM-DANGLING", "reference outlived its frame (use-after-return)"); return false; }
                    if (!bstk_use(fr, (proven_u8)(cur.i & VM_REF_SLOT), VM_REF_TAG(cur.i), true)) {
                        vm_diag(vm->diags, "E-VM-EXCL", "borrow invalidated by a conflicting access (readers-XOR-writer)");
                        return false;
                    }
                    fr->locs[cur.i & VM_REF_SLOT] = v;
                } else {
                    bstk_owner_write(&vm->frames[slot], (proven_u8)in->a);   // rebind kills borrows
                    locals[in->a] = v;
                }
                break;
            }
            case IRW_DROP:  if (sp) sp--; break;
            case IRW_NEG:   if (sp && stack[sp - 1].tag == VMV_INT)
                                stack[sp - 1].i = (proven_i64)(0 - (proven_u64)stack[sp - 1].i);
                            else if (sp && stack[sp - 1].tag == VMV_FLT)
                                stack[sp - 1] = vmv_flt(-vmv_f(stack[sp - 1]));
                            break;
            case IRW_FCONST:
                if (sp >= VM_STACK) { vm_diag(vm->diags, "E-VM-STACK", "operand stack overflow"); return false; }
                stack[sp++] = (vmv_t){ .tag = VMV_FLT, .i = in->a };
                break;
            // ★ SPEC-007 §28 — 채널 전환. **값은 그대로, 채널만 바뀐다.**
            case IRW_ELSE_NONE: {          // result → option : 오류를 **버린다**(그것이 선택이다)
                if (sp == 0) return false;
                vmv_t v = stack[--sp];
                if (v.tag == VMV_ERR) { stack[sp++] = (vmv_t){ .tag = VMV_NONE }; break; }
                if (v.tag != VMV_OK) { vm_diag(vm->diags, "E-VM-TYPE", "else_none needs a result"); return false; }
                if (vm->nbox >= VM_MAXBOX) { vm_diag(vm->diags, "E-VM-BOXPOOL",
                        "the VM's box pool is exhausted. `some(v)` / `ok(v)` wrap a value, and this "
                        "interpreter puts each one in a fixed pool. This is a limit OF THE VM, not "
                        "a defect in your program — the native build lowers option/result to a "
                        "(tag, value) PAIR in registers and boxes nothing"); return false; }
                vm->boxes[vm->nbox] = vm->boxes[v.box];
                stack[sp++] = (vmv_t){ .tag = VMV_SOME, .box = (proven_i32)vm->nbox++ };
                break;
            }
            case IRW_ELSE_ERR: {           // option → result : 없음을 **이름 있는 오류**로 만든다
                if (sp == 0) return false;
                vmv_t v = stack[--sp];
                if (v.tag == VMV_NONE) { stack[sp++] = (vmv_t){ .tag = VMV_ERR, .i = in->a }; break; }
                if (v.tag != VMV_SOME) { vm_diag(vm->diags, "E-VM-TYPE", "else_error needs an option"); return false; }
                if (vm->nbox >= VM_MAXBOX) { vm_diag(vm->diags, "E-VM-BOXPOOL",
                        "the VM's box pool is exhausted. `some(v)` / `ok(v)` wrap a value, and this "
                        "interpreter puts each one in a fixed pool. This is a limit OF THE VM, not "
                        "a defect in your program — the native build lowers option/result to a "
                        "(tag, value) PAIR in registers and boxes nothing"); return false; }
                vm->boxes[vm->nbox] = vm->boxes[v.box];
                stack[sp++] = (vmv_t){ .tag = VMV_OK, .box = (proven_i32)vm->nbox++ };
                break;
            }
            case IRW_PANIC: {    // ★ 즉시 트랩 — **복구 불가**. 계약 위반도 테스트 실패도 아니다.
                const char *m = "panic";
                (void)m;
                vm_diag(vm->diags, "E-VM-PANIC",
                        "the program called `panic` — this is an unrecoverable stop, and it is "
                        "NOT a contract violation (the code chose to stop, it did not break a promise)");
                return false;
            }
            // ★★★ **VM 은 asm 을 돌릴 수 없다 — 그리고 그렇게 말한다.**
            //   여기서 0 을 밀어 넣고 계속 갔다면 그것이 이 언어가 없애려는 바로 그 결함이다:
            //   **돌아가는 것처럼 보이는 거짓말.** 못 하면 **멈추고 못 한다고 말한다**(교훈 5).
            // ═══════════════════════════════════════════════════════════════
            // ★★★ **level-3 atomic** (RFC-0018) — RC11 은 증명됐다(LowentRC11.v, Qed).
            //   place = **슬라이스 + 인덱스** (C11 의 `&x`). parallel 워커가 **공유하는 버퍼**의
            //   한 원소를 원자적으로 만진다 — level-3 의 유일한 실사용처다.
            //   ★ VM 은 **단일 스레드**다: 관찰적으로 seq_cst 와 동일(경합이 없으니 ordering 이
            //     안 보인다, §6.5). 진짜 경합·약한 ordering 은 **네이티브(pthread)** 에서만 뜻이 있고,
            //     C 백엔드가 `atomic_*_explicit` 로 낸다. VM 은 흉내내지 않고 **값만 맞춘다.**
            case IRW_ALOAD: case IRW_ASTORE: case IRW_AADD: case IRW_ASUB:
            case IRW_AAND: case IRW_AOR: case IRW_AXOR: case IRW_ASWAP: case IRW_ACAS: {
                bool is_load = (in->w == IRW_ALOAD);
                bool is_cas  = (in->w == IRW_ACAS);
                int need = is_load ? 2 : is_cas ? 4 : 3;
                if (sp < (proven_size_t)need) return false;
                vmv_t x2 = {0}, x1 = {0};
                if (is_cas) { x2 = stack[--sp]; x1 = stack[--sp]; }   // desired, expected
                vmv_t vv = {0}; if (!is_load && !is_cas) vv = stack[--sp];   // rmw/store operand
                vmv_t iv = stack[--sp], sv = stack[--sp];
                if (iv.tag != VMV_INT || (sv.tag != VMV_SLICE && sv.tag != VMV_VARRAY)) {
                    vm_diag(vm->diags, "E-VM-TYPE", "an atomic op needs a slice place + index"); return false;
                }
                if (iv.i < 0 || (proven_size_t)iv.i >= sv.n) {
                    vm_diag(vm->diags, "E-VM-BOUNDS", "atomic index out of bounds"); return false;
                }
                int esz = sv.box ? sv.box : 8;
                proven_u8 *p = (proven_u8 *)(void *)(sv.p + (proven_size_t)iv.i * (proven_size_t)esz);
                proven_u64 old = 0;
                for (proven_i32 k = esz; k-- > 0; ) old = (old << 8) | p[k];
                proven_i64 res, nv = 0; bool write = true;
                switch (in->w) {
                    case IRW_ALOAD:  res = (proven_i64)old; write = false; break;
                    case IRW_ASTORE: nv = vv.i; res = 0; break;
                    case IRW_AADD:   nv = (proven_i64)old + vv.i; res = (proven_i64)old; break;
                    case IRW_ASUB:   nv = (proven_i64)old - vv.i; res = (proven_i64)old; break;
                    case IRW_AAND:   nv = (proven_i64)(old & (proven_u64)vv.i); res = (proven_i64)old; break;
                    case IRW_AOR:    nv = (proven_i64)(old | (proven_u64)vv.i); res = (proven_i64)old; break;
                    case IRW_AXOR:   nv = (proven_i64)(old ^ (proven_u64)vv.i); res = (proven_i64)old; break;
                    case IRW_ASWAP:  nv = vv.i; res = (proven_i64)old; break;
                    default:  // CAS: 성공하면 desired 를 쓰고 1, 아니면 안 쓰고 0
                        if ((proven_i64)old == x1.i) { nv = x2.i; res = 1; }
                        else { res = 0; write = false; }
                        break;
                }
                if (write) {   // 원소 폭으로 리틀엔디언 되쓰기 (단일 스레드라 tearing 없음)
                    proven_u64 u = (proven_u64)nv;
                    for (int k = 0; k < esz; k++) p[k] = (proven_u8)(u >> (8 * k));
                }
                stack[sp++] = vmv_int(res);
                break;
            }
            case IRW_PREFETCH:
                // ★★ **VM 은 힌트를 무시한다** — prefetch 는 결과를 안 바꾸므로 그것이 옳다.
                //   피연산자 둘을 먹고, 규약대로 자리 하나를 민다(아래 fence 주석 참조:
                //   *"모든 op 은 무언가를 민다"*, 문장 자리의 DROP 이 그것을 전제한다).
                //   ☞ 그래서 이 op 은 **VM 과 네이티브가 같은 답을 낸다** — 잴 것이 없다.
                if (sp < 2) { vm_diag(vm->diags, "E-VM-STACK", "operand stack underflow"); return false; }
                sp -= 2;
                stack[sp++] = vmv_int(0);
                break;
            case IRW_AFENCE:
                // 단일 스레드에서 fence 는 관찰적으로 무연산이다 — 그러나 **자리는 민다**:
                // 이 IR 의 규약이 *"모든 op 은 무언가를 민다"* 이고 문장 자리의 DROP 이 그것을
                // 전제한다. 안 밀었더니 네이티브가 한 칸 어긋났다(VM 21 · 네이티브 0).
                if (sp >= VM_STACK) { vm_diag(vm->diags, "E-VM-STACK", "operand stack overflow"); return false; }
                stack[sp++] = vmv_int(0);
                break;
            case IRW_FNREF:
                // ★ 콜백 참조는 **씨로 넘어갈 주소**다 — VM 안에서는 뜻이 없다(유일한 소비처가
                //   extern 호출이고, 그건 E-VM-EXTERN 으로 끝난다). 스택 균형용 자리표시자만 민다.
                if (sp >= VM_STACK) { vm_diag(vm->diags, "E-VM-STACK", "operand stack overflow"); return false; }
                stack[sp++] = vmv_int((proven_i64)(in->a & 0xffff));
                break;
            case IRW_ASEND: {
                // ★★★ **async send** — [instance, args…] 를 팝해 메일박스에 넣고 유닛을 민다(fire-and-forget).
                const low_ir_def_t *h = &vm->ir->defs[in->a];
                proven_size_t want = h->nparams;                 // instance + args
                if (sp < want) return false;
                vmv_t inst = stack[sp - want];
                if (inst.tag != VMV_REC || (proven_size_t)inst.box >= VM_MAXREC) {
                    vm_diag(vm->diags, "E-VM-TYPE", "`spawn send` needs a spawned actor instance"); return false;
                }
                // ★ bounded 강제 — 이 인스턴스로 대기 중인 메시지가 mailbox_cap 을 넘을 수 없다.
                proven_size_t sidx = h->param_sidx[0];
                if (sidx < vm->ir->nstructs && vm->ir->structs[sidx].mailbox_kind == 1) {
                    proven_i64 pend = 0;
                    for (proven_size_t q = 0; q < vm->nmbox; q++) if (vm->mbox[q].box == inst.box) pend++;
                    if (pend >= vm->ir->structs[sidx].mailbox_cap) {
                        vm_diag(vm->diags, "E-VM-MAILBOX-FULL",
                                "the actor's bounded mailbox is full — `spawn send` would exceed "
                                "`mailbox bounded N` pending messages (drain it first, or raise N)");
                        return false;
                    }
                }
                if (vm->nmbox >= VM_MBOX) { vm_diag(vm->diags, "E-VM-MBOX", "async mailbox pool exhausted — 4096 slots, never rewound (docs/runtime-pools.md)"); return false; }
                proven_size_t qi = vm->nmbox++;
                vm->mbox[qi].box = inst.box; vm->mbox[qi].handler = (proven_u16)in->a;
                vm->mbox[qi].nargs = (proven_u8)(want - 1); vm->mbox[qi].job = -1;
                for (proven_size_t g = 0; g < want - 1; g++) vm->mbox[qi].args[g] = stack[sp - want + 1 + g];
                sp -= want;
                stack[sp++] = vmv_int(0);   // 유닛(문장이 버린다)
                break;
            }
            case IRW_TASEND: {
                // ★★★ **try spawn send** — 회복 가능한 backpressure. IRW_ASEND 와 같이 큐잉하되,
                //   메일박스가 **차면 트랩 대신 result 의 err** 를 민다(ok=넣었다 · err=찼다).
                //   a 하위 16비트=핸들러, 상위(>>24)=`mailbox_full` 에러명 인덱스.
                proven_size_t hidx = (proven_size_t)(in->a & 0xffff);
                proven_i64 eidx = (in->a >> 24) & 0xffffff;
                const low_ir_def_t *h = &vm->ir->defs[hidx];
                proven_size_t want = h->nparams;
                if (sp < want) return false;
                vmv_t inst = stack[sp - want];
                if (inst.tag != VMV_REC || (proven_size_t)inst.box >= VM_MAXREC) {
                    vm_diag(vm->diags, "E-VM-TYPE", "`try spawn send` needs a spawned actor instance"); return false;
                }
                proven_size_t sidx = h->param_sidx[0];
                bool full = false;
                if (sidx < vm->ir->nstructs && vm->ir->structs[sidx].mailbox_kind == 1) {
                    proven_i64 pend = 0;
                    for (proven_size_t q = 0; q < vm->nmbox; q++) if (vm->mbox[q].box == inst.box) pend++;
                    if (pend >= vm->ir->structs[sidx].mailbox_cap) full = true;
                }
                if (full) {
                    // ★ 트랩하지 않는다 — 이게 `spawn send` 와의 유일한 차이다. 안 넣고 err 를 돌려준다.
                    sp -= want;
                    if (sp >= VM_STACK) { vm_diag(vm->diags, "E-VM-STACK", "operand stack overflow"); return false; }
                    stack[sp++] = (vmv_t){ .tag = VMV_ERR, .i = eidx };
                    break;
                }
                if (vm->nmbox >= VM_MBOX) { vm_diag(vm->diags, "E-VM-MBOX", "async mailbox pool exhausted — 4096 slots, never rewound (docs/runtime-pools.md)"); return false; }
                if (vm->nbox >= VM_MAXBOX) { vm_diag(vm->diags, "E-VM-BOXPOOL", "the VM's box pool is exhausted — 4096 slots for the values that `some`/`ok` wrap, and this pool is NEVER rewound (docs/runtime-pools.md). \u2605 The emitted C does NOT share this fate for a plain `option u64`: its fast path carries option/result as a (tag, value) PAIR with no pool at all, so the same loop keeps running natively — this is a DIVERGENCE between the backends, not your program being too big"); return false; }
                proven_size_t qi = vm->nmbox++;
                vm->mbox[qi].box = inst.box; vm->mbox[qi].handler = (proven_u16)hidx;
                vm->mbox[qi].nargs = (proven_u8)(want - 1); vm->mbox[qi].job = -1;
                for (proven_size_t g = 0; g < want - 1; g++) vm->mbox[qi].args[g] = stack[sp - want + 1 + g];
                sp -= want;
                vm->boxes[vm->nbox] = vmv_int(0);   // ok 페이로드 = 유닛
                stack[sp++] = (vmv_t){ .tag = VMV_OK, .box = (proven_i32)vm->nbox++ };
                break;
            }
            case IRW_DRAIN: {
                // ★★★ **drain** — [instance] 를 팝해 그 인스턴스의 메일박스를 **FIFO 로** 처리한다.
                //   재진입 send(핸들러가 또 spawn send)는 끝에 붙어 같은 drain 에서 처리된다.
                if (sp < 1) return false;
                vmv_t inst = stack[--sp];
                if (inst.tag != VMV_REC) { vm_diag(vm->diags, "E-VM-TYPE", "`drain` needs a spawned actor instance"); return false; }
                proven_i32 box = inst.box;
                proven_size_t i = 0;
                while (i < vm->nmbox) {
                    if (vm->mbox[i].box != box) { i++; continue; }
                    proven_u16 hh = vm->mbox[i].handler; proven_u8 na = vm->mbox[i].nargs;
                    vmv_t cargs[VM_LOCALS]; cargs[0] = inst;
                    for (proven_u8 g = 0; g < na; g++) cargs[g + 1] = vm->mbox[i].args[g];
                    // 큐에서 뺀다(앞으로 당긴다) — 그 다음에 실행(재진입 drain 대비).
                    for (proven_size_t m = i; m + 1 < vm->nmbox; m++) vm->mbox[m] = vm->mbox[m + 1];
                    vm->nmbox--;
                    const low_ir_def_t *h = &vm->ir->defs[hh];
                    vmv_t rv2;
                    if (!vm_exec(vm, h, cargs, &rv2, depth + 1)) return false;
                    // i 는 그대로 — 당겨진 다음 항목을 이어서 본다(FIFO 유지)
                }
                if (sp >= VM_STACK) { vm_diag(vm->diags, "E-VM-STACK", "operand stack overflow"); return false; }
                stack[sp++] = vmv_int(0);   // 유닛
                break;
            }
            case IRW_TSPAWN: {
                // ★★★ **task_group 태스크 spawn** (RFC-0009 D3-b) — op 를 태스크 큐(mbox, box=-1)에 넣고
                //   **job 핸들**을 민다(await 용 — 문장이면 버려진다). [args…](nparams 개) 팝.
                const low_ir_def_t *h = &vm->ir->defs[in->a];
                proven_size_t np = h->nparams;
                if (sp < np) return false;
                if (vm->nmbox >= VM_MBOX || vm->njobs >= VM_MBOX) { vm_diag(vm->diags, "E-VM-MBOX", "task/job pool exhausted — 4096 slots, never rewound (docs/runtime-pools.md)"); return false; }
                proven_size_t jid = vm->njobs++;
                vm->jobs[jid].done = 0; vm->jobs[jid].result = vmv_int(0);
                proven_size_t qi = vm->nmbox++;
                vm->mbox[qi].box = -1;   // ★ 태스크 마커 — 인스턴스 없음
                vm->mbox[qi].handler = (proven_u16)in->a;
                vm->mbox[qi].nargs = (proven_u8)np;
                vm->mbox[qi].job = (proven_i32)jid;
                for (proven_size_t g = 0; g < np; g++) vm->mbox[qi].args[g] = stack[sp - np + g];
                sp -= np;
                stack[sp++] = vmv_int((proven_i64)jid);   // job 핸들
                break;
            }
            case IRW_AWAIT: {
                // ★★★ **await** — job 완료까지 블록(green thread yield)했다가 결과를 준다.
                if (sp < 1) return false;
                vmv_t hv = stack[--sp];
                if (hv.tag != VMV_INT || (proven_size_t)hv.i >= vm->njobs) {
                    vm_diag(vm->diags, "E-VM-AWAIT", "`await` needs a job handle (from `spawn <op>`)"); return false; }
                proven_size_t jid = (proven_size_t)hv.i;
                if (!vm->jobs[jid].done) {
                    if (vm->in_gsched) {
                        // ★ 우리가 **태스크** 다 — 대상이 끝날 때까지 블록(yield). 완료가 깨운다.
                        while (!vm->jobs[jid].done) {
                            proven_size_t gi = vm->cur_gthr;
                            vm->gthr[gi].state = 6; vm->gthr[gi].blk_job = (proven_i32)jid;
                            swapcontext(&vm->gthr[gi].ctx, &vm->sched_ctx);
                        }
                    } else {
                        // ★ **그룹 본문** 이다(스케줄러 밖) — 스케줄러를 돌려 대기 태스크를 quiescence 까지
                        //   진행시킨다(대상 포함). 그 뒤에도 안 끝났으면 대상이 멈춘 것이다.
                        if (!vm_gsched(vm, depth + 1)) return false;
                        if (!vm->jobs[jid].done) { vm_diag(vm->diags, "E-VM-AWAIT",
                            "the awaited task never completed (it is blocked with no way to make progress)"); return false; }
                    }
                }
                if (sp >= VM_STACK) { vm_diag(vm->diags, "E-VM-STACK", "operand stack overflow"); return false; }
                stack[sp++] = vm->jobs[jid].result;
                break;
            }
            case IRW_CANCELSCOPE:
                // ★★★ **cancel_on_error** 스코프 진입/나감 — 전역만 세운다(스택 무변화, 문장).
                g_cancel_scope = (in->a != 0);
                break;
            case IRW_YIELD: {
                // ★★★ **green thread — `yield`** (RFC-0009 2/3). 도는 태스크가 스케줄러에 양보한다.
                //   코루틴 스케줄러 안에서만 뜻이 있다(밖이면 무시 — 태스크가 아닌 곳의 yield 는 no-op).
                if (vm->in_gsched) {
                    proven_size_t gi = vm->cur_gthr;
                    vm->gthr[gi].state = 2;   // 중단(재개 대기)
                    swapcontext(&vm->gthr[gi].ctx, &vm->sched_ctx);
                    // 재개되면 여기서 이어진다 — C 스택은 ucontext 가 보존한다.
                }
                if (sp >= VM_STACK) { vm_diag(vm->diags, "E-VM-STACK", "operand stack overflow"); return false; }
                stack[sp++] = vmv_int(0);   // 유닛
                break;
            }
            case IRW_CHNEW: {
                // ★★★ **channel** 만들기 — bounded FIFO 하나. 핸들(정수 인덱스)을 민다.
                if (vm->nchan >= VM_MAXCHAN) { vm_diag(vm->diags, "E-VM-CHAN", "channel pool exhausted — 64 channels, never rewound (docs/runtime-pools.md)"); return false; }
                proven_size_t ci = vm->nchan++;
                vm->chans[ci].head = vm->chans[ci].tail = vm->chans[ci].count = 0;
                if (sp >= VM_STACK) { vm_diag(vm->diags, "E-VM-STACK", "operand stack overflow"); return false; }
                stack[sp++] = vmv_int((proven_i64)ci);
                break;
            }
            case IRW_CHSEND: {
                // ★★★ **chsend** — 채널에 값을 넣는다. full 이면 **yield 로 블록**(recver 가 빼면 재개).
                if (sp < 2) return false;
                vmv_t v = stack[--sp], chv = stack[--sp];
                if (chv.tag != VMV_INT || (proven_size_t)chv.i >= vm->nchan) {
                    vm_diag(vm->diags, "E-VM-CHAN", "`chsend` needs a channel handle (from `channel`)"); return false; }
                proven_size_t ci = (proven_size_t)chv.i;
                while (vm->chans[ci].count >= VM_CHAN_CAP) {
                    if (!vm->in_gsched) { vm_diag(vm->diags, "E-VM-CHAN-FULL",
                        "`chsend` on a FULL channel with no scheduler to yield to — nobody can receive. "
                        "A blocking send needs a `task_group` with a concurrent receiver"); return false; }
                    proven_size_t gi = vm->cur_gthr;
                    vm->gthr[gi].state = 5; vm->gthr[gi].blk_chan = (proven_i32)ci; vm->gthr[gi].blk_recv = 0;   // send 블록
                    swapcontext(&vm->gthr[gi].ctx, &vm->sched_ctx);   // 재개되면 재검사
                }
                vm->chans[ci].buf[vm->chans[ci].tail] = v;
                vm->chans[ci].tail = (vm->chans[ci].tail + 1) % VM_CHAN_CAP;
                vm->chans[ci].count++;
                vm->gprogress++;
                // ★ 이 채널에서 recv 를 기다리던 태스크를 깨운다(데이터가 생겼다).
                for (proven_size_t q = 0; q < vm->ngthr; q++)
                    if (vm->gthr[q].state == 5 && vm->gthr[q].blk_chan == (proven_i32)ci && vm->gthr[q].blk_recv) vm->gthr[q].state = 2;
                if (sp >= VM_STACK) { vm_diag(vm->diags, "E-VM-STACK", "operand stack overflow"); return false; }
                stack[sp++] = vmv_int(0);
                break;
            }
            case IRW_CHRECV: {
                // ★★★ **chrecv** — 채널에서 하나 뺀다. empty 면 **yield 로 블록**(sender 가 넣으면 재개).
                if (sp < 1) return false;
                vmv_t chv = stack[--sp];
                if (chv.tag != VMV_INT || (proven_size_t)chv.i >= vm->nchan) {
                    vm_diag(vm->diags, "E-VM-CHAN", "`chrecv` needs a channel handle (from `channel`)"); return false; }
                proven_size_t ci = (proven_size_t)chv.i;
                while (vm->chans[ci].count == 0) {
                    if (!vm->in_gsched) { vm_diag(vm->diags, "E-VM-CHAN-EMPTY",
                        "`chrecv` on an EMPTY channel with no scheduler to yield to — nobody can send. "
                        "A blocking receive needs a `task_group` with a concurrent sender"); return false; }
                    proven_size_t gi = vm->cur_gthr;
                    vm->gthr[gi].state = 5; vm->gthr[gi].blk_chan = (proven_i32)ci; vm->gthr[gi].blk_recv = 1;   // recv 블록
                    swapcontext(&vm->gthr[gi].ctx, &vm->sched_ctx);   // 재개되면 재검사
                }
                vmv_t out = vm->chans[ci].buf[vm->chans[ci].head];
                vm->chans[ci].head = (vm->chans[ci].head + 1) % VM_CHAN_CAP;
                vm->chans[ci].count--;
                vm->gprogress++;
                // ★ 이 채널에서 send 를 기다리던 태스크를 깨운다(자리가 생겼다).
                for (proven_size_t q = 0; q < vm->ngthr; q++)
                    if (vm->gthr[q].state == 5 && vm->gthr[q].blk_chan == (proven_i32)ci && !vm->gthr[q].blk_recv) vm->gthr[q].state = 2;
                if (sp >= VM_STACK) { vm_diag(vm->diags, "E-VM-STACK", "operand stack overflow"); return false; }
                stack[sp++] = out;
                break;
            }
            case IRW_SCHED: {
                // ★★★ **결정적 스케줄러 + green thread** (RFC-0009 D6·2/3) — 전역 메일박스를 quiescence
                //   까지 배달한다. 각 단위는 **코루틴**에서 돌아 `yield` 로 중단할 수 있고, 스케줄러가 오라클
                //   계획대로 ready 를 골라 재개한다(vm_gsched). yield 가 없으면 계획대로 완주(옛 FIFO 와 동일).
                //   ★ **중첩 schedule**(태스크 안)은 코루틴 재진입을 피해 인라인 FIFO 로 폴백한다.
                if (!vm->in_gsched) {
                    if (!vm_gsched(vm, depth + 1)) return false;
                    if (sp >= VM_STACK) { vm_diag(vm->diags, "E-VM-STACK", "operand stack overflow"); return false; }
                    stack[sp++] = vmv_int(0);
                    break;
                }
                proven_size_t guard = 0;
                while (vm->nmbox > 0) {
                    if (++guard > 1000000) {   // 재진입 send 무한 루프 방어 — 정직한 상한
                        vm_diag(vm->diags, "E-VM-SCHED-LOOP",
                                "the scheduler did not reach quiescence — a handler keeps sending "
                                "(an unbounded send loop). This is a real non-termination, not a tool limit");
                        return false;
                    }
                    // 다음 메시지 인덱스 선택 — **스케줄 계획**을 따른다(오라클이 DFS 로 열거).
                    //   계획이 지시한 prefix 안이면 그 선택, 아니면 0(front=FIFO). 각 단계의 ready
                    //   크기를 기록해 오라클이 backtrack 에 쓴다. 계획이 비면(평소·네이티브) 전부 front.
                    proven_size_t pick;
                    if (g_sched_step < SCHED_MAXSTEP) {
                        g_sched_sizes[g_sched_step] = (proven_u16)vm->nmbox;   // ready 크기 기록
                        proven_size_t choice = (g_sched_step < g_sched_plan_len)
                                                 ? (proven_size_t)g_sched_plan[g_sched_step] : 0;
                        pick = (choice < vm->nmbox) ? choice : 0;               // 방어적 clamp
                        g_sched_step++;
                    } else {
                        pick = 0;   // 통제 깊이를 넘으면 front(FIFO) — 그 너머는 열거 못 한다(정직히 보고)
                    }
                    proven_i32 box = vm->mbox[pick].box;
                    proven_u16 hh = vm->mbox[pick].handler; proven_u8 na = vm->mbox[pick].nargs;
                    vmv_t cargs[VM_LOCALS];
                    proven_u8 base = 0;
                    if (box >= 0) cargs[base++] = (vmv_t){ .tag = VMV_REC, .box = box };  // 메시지 — 인스턴스가 슬롯 0
                    // box<0 = **task_group 태스크** — 인스턴스 없음, 인자가 op 파라미터 그대로.
                    for (proven_u8 g = 0; g < na; g++) cargs[base + g] = vm->mbox[pick].args[g];
                    // 큐에서 뺀 뒤 실행(재진입 대비) — drain 과 같은 규율.
                    for (proven_size_t m = pick; m + 1 < vm->nmbox; m++) vm->mbox[m] = vm->mbox[m + 1];
                    vm->nmbox--;
                    const low_ir_def_t *h = &vm->ir->defs[hh];
                    vmv_t rv2;
                    if (!vm_exec(vm, h, cargs, &rv2, depth + 1)) return false;
                }
                if (sp >= VM_STACK) { vm_diag(vm->diags, "E-VM-STACK", "operand stack overflow"); return false; }
                stack[sp++] = vmv_int(0);   // 유닛
                break;
            }
            case IRW_EXTERN:
                // ★★★ **VM 은 C 를 못 부른다 — 그리고 그렇게 말한다**(인라인 asm 과 같은 정직함).
                vm_diag(vm->diags, "E-VM-EXTERN",
                        "this op's body is IN C (`extern`) — the VM cannot call a C function. "
                        "It would have to PRETEND, and a pretending interpreter is worse than none. "
                        "Build native (`--emit-c`) to run it. The CONTRACT on this op is still "
                        "checked at the boundary in the native build (RFC-0063 D5)");
                return false;
            case IRW_CSTR2STR:
                // ★★★ **cstr 는 VM 밖 FFI 경계 전용**(RFC-0068 D4) — cstr 는 extern 만이 낼 수 있고
                //   (VM 은 그것을 E-VM-EXTERN 으로 거부한다), 그러므로 VM 에는 스캔할 포인터가
                //   애초에 없다. 있는 척하면 extern 을 흉내내는 것과 같은 거짓말이다.
                vm_diag(vm->diags, "E-VM-CSTR",
                        "`str_from_cstr` scans a raw C string pointer, which only exists at the FFI "
                        "boundary — and the VM does not run C (`extern` is E-VM-EXTERN). There is no "
                        "pointer here to scan. Build native (`--emit-c`) to run it");
                return false;
            case IRW_STR2CSTR:
                // ★★★ **cstr 는 VM 밖**(RFC-0068 D4) — str_buf → cstr 는 생 포인터를 만든다. VM 은
                //   포인터를 노출하지 않으므로(주소가 없다) 이 값에 뜻이 없다. 네이티브에서만 산다.
                vm_diag(vm->diags, "E-VM-CSTR",
                        "`cstr_of` takes the raw base pointer of a byte buffer as a cstr — a value that "
                        "only means anything at the FFI boundary, which the VM does not run (the VM does "
                        "not expose addresses). Build native (`--emit-c`) to run it");
                return false;
            // ═══════════════════════════════════════════════════════════════
            // ★★★ **비트 연산** (RFC-0064) — **폭이 계약이다.**
            //   C 는 `~(uint8_t)0` 을 **int 로 승격**해 `-1` 을 낸다. 우리는 **폭을 안다** ⇒
            //   u8 의 `bit_not 0` 은 **255** 다. 폭이 계약이면 **연산도 폭 안에 있어야 한다.**
            case IRW_BAND: case IRW_BOR: case IRW_BXOR:
            case IRW_SHL: case IRW_SHR: case IRW_WSHL: case IRW_WSHR:
            case IRW_ROTL: case IRW_ROTR: {
                if (sp < 2) return false;
                vmv_t bv = stack[--sp], av = stack[--sp];
                if (av.tag != VMV_INT || bv.tag != VMV_INT) {
                    vm_diag(vm->diags, "E-BITOP-TYPE",
                            "a bit operation needs INTEGERS — bits are a property of a machine word, "
                            "and a float or a slice does not have them (RFC-0064)");
                    return false;
                }
                int bits = (int)(in->a & 0xff);
                bool known = (in->a & IR_TY_KNOWN) != 0;
                bool sg = (in->a & IR_TY_SIGNED) != 0;
                if (!known || !bits) bits = 64;
                proven_u64 mask = (bits >= 64) ? ~0ull : ((1ull << bits) - 1ull);
                proven_u64 x = (proven_u64)av.i & mask;
                proven_i64 n = bv.i;
                proven_u64 r = 0;
                switch (in->w) {
                    case IRW_BAND: r = x & ((proven_u64)bv.i & mask); break;
                    case IRW_BOR:  r = x | ((proven_u64)bv.i & mask); break;
                    case IRW_BXOR: r = x ^ ((proven_u64)bv.i & mask); break;
                    case IRW_SHL: case IRW_SHR:
                        // ★★★ **시프트 양은 계약이다.** C 는 여기서 **UB** 다. 우리는 **트랩**한다.
                        //   (그리고 구간 분석이 증명하면 이 검사는 **사라진다** — RFC-0053.)
                        if (n < 0 || n >= bits) {
                            vm_diag(vm->diags, "E-VM-SHIFT",
                                    "the shift amount is not smaller than the type's width. In C this "
                                    "is UNDEFINED BEHAVIOUR — the program keeps running and the answer "
                                    "is whatever the machine felt like. Here it is a CONTRACT: it traps. "
                                    "(Want the masking? Say so by name: `wrap_shl` / `wrap_shr`.)");
                            return false;
                        }
                        if (in->w == IRW_SHL) r = (x << n) & mask;
                        else if (sg) {   // ★ 부호 있는 타입 ⇒ **산술 시프트**(타입이 이미 말했다)
                            proven_i64 sx = ity_wrap((proven_i64)x, (proven_u8)bits, true);
                            r = (proven_u64)(sx >> n) & mask;
                        } else r = (x >> n) & mask;
                        break;
                    case IRW_WSHL: case IRW_WSHR: {
                        proven_u64 k = (proven_u64)n & (proven_u64)(bits - 1);   // 폭으로 마스크
                        if (in->w == IRW_WSHL) r = (x << k) & mask;
                        else if (sg) {
                            proven_i64 sx = ity_wrap((proven_i64)x, (proven_u8)bits, true);
                            r = (proven_u64)(sx >> k) & mask;
                        } else r = (x >> k) & mask;
                        break;
                    }
                    case IRW_ROTL: case IRW_ROTR: {
                        proven_u64 k = (proven_u64)((n % bits + bits) % bits);
                        if (!k) { r = x; break; }
                        r = (in->w == IRW_ROTL) ? ((x << k) | (x >> (bits - k))) & mask
                                                : ((x >> k) | (x << (bits - k))) & mask;
                        break;
                    }
                    default: return false;
                }
                stack[sp++] = vmv_int(ity_wrap((proven_i64)r, (proven_u8)bits, sg));
                break;
            }
            case IRW_BNOT: case IRW_POPCNT: case IRW_CLZ: case IRW_CTZ: case IRW_BSWAP: {
                if (sp < 1) return false;
                vmv_t av = stack[--sp];
                if (av.tag != VMV_INT) {
                    vm_diag(vm->diags, "E-BITOP-TYPE", "a bit operation needs an INTEGER");
                    return false;
                }
                int bits = (int)(in->a & 0xff);
                bool known = (in->a & IR_TY_KNOWN) != 0;
                bool sg = (in->a & IR_TY_SIGNED) != 0;
                if (!known || !bits) bits = 64;
                proven_u64 mask = (bits >= 64) ? ~0ull : ((1ull << bits) - 1ull);
                proven_u64 x = (proven_u64)av.i & mask;
                proven_u64 r = 0;
                switch (in->w) {
                    case IRW_BNOT: r = (~x) & mask; break;   // ★ **폭 안에서** 뒤집는다
                    case IRW_POPCNT: { proven_u64 t = x; while (t) { r += t & 1ull; t >>= 1; } break; }
                    case IRW_CTZ: {
                        if (!x) { r = (proven_u64)bits; break; }
                        while (!((x >> r) & 1ull)) r++;
                        break;
                    }
                    case IRW_CLZ: {
                        if (!x) { r = (proven_u64)bits; break; }
                        int k = bits - 1;
                        while (k >= 0 && !((x >> k) & 1ull)) { r++; k--; }
                        break;
                    }
                    case IRW_BSWAP: {
                        int nb = bits / 8;
                        for (int b = 0; b < nb; b++)
                            r |= ((x >> (8 * b)) & 0xffull) << (8 * (nb - 1 - b));
                        break;
                    }
                    default: return false;
                }
                bool cnt = (in->w == IRW_POPCNT || in->w == IRW_CLZ || in->w == IRW_CTZ);
                stack[sp++] = vmv_int(cnt ? (proven_i64)r
                                          : ity_wrap((proven_i64)r, (proven_u8)bits, sg));
                break;
            }
            case IRW_RESBLK:
                // ★ VM 도 이것은 못 준다 — 예약된 칸은 **링커가 잡는 것**이고 인터프리터에는
                //   링커가 없다. MMIO 와 같은 자리: 흉내내는 인터프리터는 없느니만 못하다.
                vm_diag(vm->diags, "E-VM-RESERVE",
                        "this opens a `storage reserved .` block — storage the LINKER hands the type — and the "
                        "VM has no linker. It exists because an ISR has no parameters and there is no "
                        "module-level mutable state (RFC-0039 §9-1); neither of those is a thing the "
                        "interpreter can stand in for. Build native (`--emit-c` for a bare-metal "
                        "target), or hand the bytes in (`view T <slice>`) to exercise the logic here");
                return false;
            case IRW_MMIOBLK:
                // ★ 인터프리터는 0x40020000 을 만질 수 없다. 그리고 **만지는 척하면 안 된다** —
                //   흉내낸 레지스터는 장치가 아니라 거짓말이다(asm·extern·cstr 와 같은 자리).
                vm_diag(vm->diags, "E-VM-MMIO",
                        "this opens a register block at the type's ABSOLUTE base address — the VM "
                        "cannot touch physical memory. It would have to PRETEND to be the device, "
                        "and a pretending interpreter is worse than none. Build native "
                        "(`--emit-c` for a bare-metal target) to run it, or hand the bytes in "
                        "(`view T <slice>`) to exercise the logic here");
                return false;
            case IRW_ASM:
                vm_diag(vm->diags, "E-VM-ASM",
                        "this op IS inline assembly — the VM cannot execute machine instructions. "
                        "It would have to PRETEND, and a pretending interpreter is worse than none. "
                        "Build native (`--emit-c`) to run it");
                return false;
            case IRW_ASSERT: {   // 계약 검사 — 거짓이면 트랩
                if (sp == 0) return false;
                vmv_t v = stack[--sp];
                if (v.tag != VMV_INT || v.i == 0) {
                    // 진입(a=0) 은 **호출자**의 잘못, 출구(a&1) 는 **이 op** 의 잘못이다.
                    // 그리고 제거됐다고 표시된 출구 검사가 실패하면 그것은 **분석의 오류**다.
                    if ((in->a & 4) && !(in->a & IR_POL_PROVEN)) {   // ★ `expect` — 테스트 실패
                        vm_diag(vm->diags, "E-TEST-FAIL",
                                "an `expect` in this test is FALSE — the test failed (this is not a "
                                "contract violation: it is the test telling you the code is wrong)");
                        return false;
                    }
                    const char *code = (in->a & IR_POL_PROVEN) ? "E-VM-ANALYSIS" : "E-VM-CONTRACT";
                    const char *msg =
                        (in->a & IR_POL_PROVEN)
                          ? "a contract check was eliminated but the condition is VIOLATED "
                            "— the interval analysis is UNSOUND (this is a compiler bug)"
                          : (in->a & 8)
                              ? "this op returned NORMALLY on a path where an `errors … when` "
                                "condition is TRUE — the clause declared an error there and the op "
                                "did not produce it. The `errors` clause is an EXIT CONTRACT: the "
                                "op must fail EXACTLY when the clause says so. (Only the other "
                                "direction used to be checked — and the contract oracle DERIVES its "
                                "boundary tests from this clause, so it was standing on a lie.)"
                          : (in->a & 2)
                              ? "this op returned an error on a path where the `when` condition "
                                "declared for that error is FALSE — the `errors` clause lied"
                              : (in->a & 1)
                                  ? "`ensures` violated at exit — THIS op broke its own promise "
                                    "(the caller was told a lie)"
                                  : "`requires` violated at entry — the caller broke the contract";
                    vm_diag(vm->diags, code, msg);
                    return false;
                }
                break;
            }
            case IRW_UNM: {    // sqrt / abs / floor / ceil / nonzero_of
                if (sp == 0) return false;
                vmv_t v = stack[--sp];
                // ★ 선택자는 **하위 4비트**다 — abs 는 상위 비트에 선언 폭을 싣는다(위 meta 주석 참조).
                const int sel = (int)(in->a & 0xf);
                if (sel == 5 || sel == 6) {   // RFC-0053 E5: sum(보정) / sum_fast(축차)
                    if (v.tag != VMV_VARRAY) {
                        vm_diag(vm->diags, "E-VM-TYPE", "sum needs a typed array (view_array)");
                        return false;
                    }
                    bool flt = (v.i & IR_FLT_BIT) != 0;
                    double acc = 0.0, comp = 0.0;   // Neumaier 보정항
                    for (proven_size_t k2 = 0; k2 < v.n; k2++) {
                        proven_u64 raw = 0;
                        const proven_u8 *pb = v.p + k2 * (proven_size_t)v.box;
                        for (proven_i32 b2 = v.box; b2-- > 0; ) raw = (raw << 8) | pb[b2];
                        double x = flt ? ir_bits_to_f(raw, (proven_u8)v.box) : (double)(proven_i64)raw;
                        if (sel == 6) { acc += x; continue; }              // 축차 — 오차 O(n·ε)
                        double t = acc + x;                                   // Kahan-Babuška-Neumaier
                        comp += (fabs(acc) >= fabs(x)) ? (acc - t) + x : (x - t) + acc;
                        acc = t;
                    }
                    stack[sp++] = vmv_flt(sel == 6 ? acc : acc + comp);
                    break;
                }
                // ★★★ 초월 함수 (RFC-0090 N2) — **부동 전용**이다. 정수를 주면 거절한다:
                //   `sin 3` 이 무엇이어야 하는지 언어가 대신 정하지 않는다(암묵 변환 없음, D12 정신).
                if (sel >= 7 && sel <= 11) {
                    if (v.tag != VMV_FLT) {
                        vm_diag(vm->diags, "E-VM-TYPE",
                                "sin/cos/exp/log/round are float-only — convert first (`cast f64 n`)");
                        return false;
                    }
                    double x = vmv_f(v);
                    double r = sel == 7 ? sin(x) : sel == 8 ? cos(x) : sel == 9 ? exp(x)
                             : sel == 10 ? log(x) : lw_round_half_away(x);
                    stack[sp++] = vmv_flt(r);
                    break;
                }
                if (sel == 4) {   // D6: nonzero_of — 전제조건을 **타입에** 기록한다
                    if (v.tag != VMV_INT) { vm_diag(vm->diags, "E-VM-TYPE", "nonzero_of needs an integer"); return false; }
                    if (v.i == 0) { stack[sp++] = (vmv_t){ .tag = VMV_NONE }; break; }
                    if (vm->nbox >= VM_MAXBOX) { vm_diag(vm->diags, "E-VM-BOXPOOL",
                        "the VM's box pool is exhausted. `some(v)` / `ok(v)` wrap a value, and this "
                        "interpreter puts each one in a fixed pool. This is a limit OF THE VM, not "
                        "a defect in your program — the native build lowers option/result to a "
                        "(tag, value) PAIR in registers and boxes nothing"); return false; }
                    vm->boxes[vm->nbox] = v;
                    stack[sp++] = (vmv_t){ .tag = VMV_SOME, .box = (proven_i32)vm->nbox++ };
                    break;
                }
                if (v.tag == VMV_INT && (sel == 1 || sel == 2 || sel == 3)) {
                    if (sel == 1) {
                        // ★★★ **abs(폭-MIN) 은 그 폭에 안 맞는다** — |MIN| = +2^(N-1) (사용자 결정
                        //   2026-07-23 · div 와 같은 규율). 폭을 알 때만 판정한다(상위 비트에 실려 온다).
                        if ((in->a & 0x200000) && (in->a & 0x400000)) {   // 폭 있음 + 부호형
                            proven_u8 wb = (proven_u8)((in->a >> 24) & 0xff);
                            if (wb && v.i == ity_lo(wb, true)) {
                                vm_diag(vm->diags, "E-VM-OVERFLOW",
                                        "abs overflow: |MIN| does not fit the declared width"); return false;
                            }
                        }
                        stack[sp++] = vmv_int(v.i < 0 ? (proven_i64)(0 - (proven_u64)v.i) : v.i);
                    } else stack[sp++] = v;   // floor/ceil of an integer is itself
                    break;
                }
                double x;
                if (v.tag == VMV_FLT) x = vmv_f(v);
                else {   // D14: sqrt 는 **부동 전용** — 암묵 승급 없음(G2 와 정합)
                    vm_diag(vm->diags, "E-VM-TYPE",
                            "sqrt is float-only: convert explicitly (float_of / round_to)");
                    return false;
                }
                stack[sp++] = vmv_flt(sel == 0 ? sqrt(x) : sel == 1 ? fabs(x)
                                    : sel == 2 ? floor(x) : ceil(x));
                break;
            }
            case IRW_BINM: {   // fmod / min / max
                if (sp < 2) return false;
                vmv_t bv = stack[--sp], av = stack[--sp];
                if (av.tag == VMV_INT && bv.tag == VMV_INT && in->a != 0) {
                    stack[sp++] = vmv_int(in->a == 1 ? (av.i < bv.i ? av.i : bv.i)
                                                     : (av.i > bv.i ? av.i : bv.i));
                    break;
                }
                if (av.tag != VMV_FLT || bv.tag != VMV_FLT) {   // D14: fmod 는 부동 전용(정수는 rem/mod)
                    vm_diag(vm->diags, "E-VM-TYPE",
                            in->a == 0 ? "fmod is float-only (use rem/mod for integers)"
                                       : "min/max need two numbers of one kind");
                    return false;
                }
                double x = vmv_f(av), y = vmv_f(bv);
                stack[sp++] = vmv_flt(in->a == 0 ? fmod(x, y) : in->a == 1 ? (x < y ? x : y)
                                    : in->a == 2 ? (x > y ? x : y) : pow(x, y));
                break;
            }
            case IRW_CAST: {   // a = 0 → f64 | width → integer truncate/mask
                if (sp == 0) return false;
                vmv_t v = stack[--sp];
                if (in->a == 0) {
                    if (v.tag == VMV_INT) stack[sp++] = vmv_flt((double)v.i);
                    else if (v.tag == VMV_FLT) stack[sp++] = v;
                    else { vm_diag(vm->diags, "E-VM-TYPE", "cast needs a scalar"); return false; }
                } else {
                    // S3/D9: 폭 절단은 **트랩**이 기본. narrow_wrap / narrow_sat 이 정책을 고른다.
                    proven_u8 nb = (proven_u8)((in->a & 0xff) * 8);   // 바이트 → 비트
                    proven_i64 x;
                    if (v.tag == VMV_INT) x = v.i;
                    else if (v.tag == VMV_FLT) x = (proven_i64)vmv_f(v);
                    else { vm_diag(vm->diags, "E-VM-TYPE", "cast needs a scalar"); return false; }
                    bool nsign = (in->a & IR_SGN_BIT) != 0;   // ★ widen 은 대상 부호를 싣는다 → 부호 범위로 판정(전역성). narrow 는 무부호(손실 트랩 계약 유지)
                    bool fits = nb >= 64 || ity_fits(x, nb, nsign);
                    if (in->a & IR_POL_CHK) {   // narrow_try: option τ
                        if (!fits) { stack[sp++] = (vmv_t){ .tag = VMV_NONE }; break; }
                        if (vm->nbox >= VM_MAXBOX) { vm_diag(vm->diags, "E-VM-BOXPOOL",
                        "the VM's box pool is exhausted. `some(v)` / `ok(v)` wrap a value, and this "
                        "interpreter puts each one in a fixed pool. This is a limit OF THE VM, not "
                        "a defect in your program — the native build lowers option/result to a "
                        "(tag, value) PAIR in registers and boxes nothing"); return false; }
                        vm->boxes[vm->nbox] = vmv_int(x);
                        stack[sp++] = (vmv_t){ .tag = VMV_SOME, .box = (proven_i32)vm->nbox++ };
                        break;
                    }
                    if (fits) { stack[sp++] = vmv_int(x); break; }
                    if (in->a & IR_POL_WRAP) { stack[sp++] = vmv_int(ity_wrap(x, nb, nsign)); break; }
                    if (in->a & IR_POL_SAT)  { stack[sp++] = vmv_int(ity_sat(x, nb, nsign)); break; }
                    vm_diag(vm->diags, (in->a & IR_POL_PROVEN) ? "E-VM-ANALYSIS" : "E-VM-CAST",
                            (in->a & IR_POL_PROVEN)
                              ? "interval analysis claimed this narrowing always fits, but it did not"
                              : "value does not fit the target width (use narrow_wrap / narrow_sat / narrow_try)");
                    return false;
                }
                break;
            }
            case IRW_NOT:
                if (sp && stack[sp - 1].tag == VMV_MASK) {   // mask lift
                    proven_u64 lm = (stack[sp - 1].box >= 64) ? ~0ull : ((1ull << stack[sp - 1].box) - 1);
                    stack[sp - 1].i = (proven_i64)(~(proven_u64)stack[sp - 1].i & lm);
                } else if (sp && stack[sp - 1].tag == VMV_INT) {
                    stack[sp - 1].i = !stack[sp - 1].i;
                }
                break;
            // ★ **뒤로 가는 분기가 곧 루프다** — 반복 하나가 만든 벡터는 거기서 죽는다(RFC-0089 B).
            case IRW_BR:    if ((proven_size_t)in->a <= pc) { a->sp = sp; vm_pool_gc(vm, a); }
                            pc = (proven_size_t)in->a - 1; break;
            case IRW_SWITCH: {   // ★ RFC-0081 MM9-B2 — 계산 점프 테이블. v 를 pop → 뒤 BR 런의 v번째로.
                if (sp == 0) return false;
                vmv_t v = stack[--sp];
                if (v.tag != VMV_INT) { vm_diag(vm->diags, "E-VM-TYPE", "switch value is not an int/tag"); return false; }
                proven_i64 n = in->a;
                proven_i64 off = (v.i >= 0 && v.i < n) ? v.i : n;   // 범위 밖 → default(N번째)
                pc = pc + (proven_size_t)off;   // pc=SWITCH 위치; +off 뒤 루프 pc++ → SWITCH+1+off = off번째 BR
                break;
            }
            case IRW_BRZ: {
                if (sp == 0) return false;
                vmv_t v = stack[--sp];
                if (v.tag != VMV_INT) { vm_diag(vm->diags, "E-VM-TYPE", "branch condition is not a bool/int"); return false; }
                if (v.i == 0) {
                    if ((proven_size_t)in->a <= pc) { a->sp = sp; vm_pool_gc(vm, a); }
                    pc = (proven_size_t)in->a - 1;
                }
                break;
            }
            case IRW_RET:   *ret = sp ? stack[sp - 1] : vmv_int(0); return true;
            case IRW_LEN: {
                if (sp > 0 && stack[sp - 1].tag == VMV_REF) {   // ★ 참조를 따라간다
                    bool thr_; vmv_t d_ = vm_through(vm, stack[sp - 1], false, &thr_);
                    if (!thr_) return false;
                    stack[sp - 1] = d_;
                }
                if (sp == 0 || (stack[sp - 1].tag != VMV_SLICE && stack[sp - 1].tag != VMV_VARRAY)) {
                    vm_diag(vm->diags, "E-VM-TYPE", "len needs a slice"); return false;
                }
                stack[sp - 1] = vmv_int((proven_i64)stack[sp - 1].n);
                break;
            }
            case IRW_ISTORE: {   // ★ 슬라이스 원소 쓰기: slice|varray, i, v → ()
                if (sp < 3) return false;
                vmv_t vv = stack[--sp], iv = stack[--sp];
                bool thr_; vmv_t sv = vm_through(vm, stack[--sp], true, &thr_);   // ★ 쓰기 — write=true
                if (!thr_) return false;
                // ★★★ **aggregate 슬라이스 저장** (RFC-0080 — AST 아레나): 원소가 구조체(또는
                //   페이로드 enum 레이아웃)면 슬롯은 스칼라가 아니라 **레코드**를 받는다. 레코드의
                //   필드를 구조체 레이아웃의 오프셋에 인코딩한다(뷰 읽기의 역연산).
                bool is_structarr = (sv.tag == VMV_VARRAY) && (sv.i & IR_STRUCT_BIT);
                // ★★★ **읽은 것을 다시 쓸 수 있어야 한다** (2026-07-26). 구조체 슬라이스의 원소를
                //   읽으면 **VIEW**(바이트를 가리키는 창)가 나오는데, 저장은 **REC**(만들어진
                //   레코드)만 받고 있었다 ⇒ `set (index s i) (index s j)` 즉 **맞바꾸기가
                //   불가능**했다. 그래서 구조체 슬라이스를 제자리에서 정렬할 수가 없었다.
                //   읽기는 되고 쓰기는 안 되는 비대칭은 기능의 절반이 아니라 **못 쓰는 기능**이다.
                bool view_val = is_structarr && vv.tag == VMV_VIEW;
                if ((sv.tag != VMV_SLICE && sv.tag != VMV_VARRAY) || iv.tag != VMV_INT ||
                    (!is_structarr && vv.tag != VMV_INT && vv.tag != VMV_FLT) ||
                    (is_structarr && vv.tag != VMV_REC && vv.tag != VMV_VIEW)) {
                    vm_diag(vm->diags, "E-VM-TYPE", "set index needs a slice + an int + a value");
                    return false;
                }
                if (iv.i < 0 || (proven_size_t)iv.i >= sv.n) {
                    // 읽기와 같은 규칙: 제거됐다고 표시된 검사가 실패하면 **분석의 오류**다.
                    vm_diag(vm->diags, (in->a & IR_POL_PROVEN) ? "E-VM-ANALYSIS" : "E-VM-BOUNDS",
                            (in->a & IR_POL_PROVEN)
                              ? "an index-store bounds check was eliminated but the index is OUT OF "
                                "BOUNDS — the analysis is UNSOUND (compiler bug)"
                              : "slice index out of bounds on write (panic)");
                    return false;
                }
                if (is_structarr) {
                    proven_i32 si = (proven_i32)((sv.i >> 20) & 0xff);
                    const low_ir_struct_t *st = &vm->ir->structs[si];
                    proven_u8 *base = (proven_u8 *)(void *)(uintptr_t)sv.p +
                                      (proven_size_t)iv.i * (proven_size_t)sv.box;
                    if (view_val) {
                        // ★ 뷰 → 슬롯: 바이트를 그대로 옮긴다(레이아웃이 같으므로 재인코딩이
                        //   필요 없다). memmove 인 이유는 같은 배열 안의 두 자리를 맞바꿀 때
                        //   원본과 대상이 **겹칠 수 있기** 때문이다.
                        proven_size_t nb = (proven_size_t)sv.box;
                        if ((proven_size_t)vv.n < nb) nb = (proven_size_t)vv.n;
                        memmove(base, (const void *)(uintptr_t)vv.p, nb);
                        break;
                    }
                    for (proven_size_t z = 0; z < (proven_size_t)sv.box; z++) base[z] = 0;
                    vmrec_t *r = &vm->recs[vv.box];
                    const low_ir_make_t *mk = &vm->ir->makes[r->make_idx];
                    for (proven_size_t fi = 0; fi < st->nf; fi++) {   // 레이아웃 필드마다 레코드에서 찾아 인코딩
                        for (proven_size_t q = 0; q < r->nfields; q++) {
                            if (!proven_u8str_view_eq(mk->fields[q], st->f[fi].name)) continue;
                            proven_u64 x = (r->fields[q].tag == VMV_FLT)
                                             ? ir_f_to_bits(ir_bits_to_f((proven_u64)r->fields[q].i, 8), st->f[fi].size)
                                             : (proven_u64)r->fields[q].i;
                            proven_u8 *fp = base + st->f[fi].off;
                            if (st->f[fi].be)
                                for (proven_u8 kk = 0; kk < st->f[fi].size; kk++)
                                    fp[kk] = (proven_u8)(x >> (8 * (st->f[fi].size - 1 - kk)));
                            else
                                for (proven_u8 kk = 0; kk < st->f[fi].size; kk++)
                                    fp[kk] = (proven_u8)(x >> (8 * kk));
                            break;
                        }
                    }
                    break;
                }
                // 가변성은 **정적으로** 강제된다(E-TYPE-MUT — 아래 typecheck).
                // 런타임 슬라이스 값에는 mut 표시가 없다(포인터가 const 다). 그것을 정직하게 적는다.
                if (sv.tag == VMV_SLICE) {
                    ((proven_u8 *)(void *)(uintptr_t)sv.p)[iv.i] = (proven_u8)(vv.i & 0xff);
                } else {
                    // ★ 타입 있는 배열 — 원소를 **인코딩**한다(네이티브 = little-endian).
                    proven_u64 x = (sv.i & IR_FLT_BIT) ? ir_f_to_bits(vv.tag == VMV_FLT ? ir_bits_to_f((proven_u64)vv.i, 8)
                                                                         : (double)vv.i,
                                                        (proven_u8)sv.box)
                                        : (proven_u64)vv.i;
                    proven_u8 *p = (proven_u8 *)(void *)(uintptr_t)sv.p +
                                   (proven_size_t)iv.i * (proven_size_t)sv.box;
                    for (proven_i32 k = 0; k < sv.box; k++) p[k] = (proven_u8)((x >> (8 * k)) & 0xff);
                }
                break;
            }
            case IRW_INDEX: {
                if (sp < 2) return false;
                vmv_t iv = stack[--sp];
                bool thr_; vmv_t sv = vm_through(vm, stack[--sp], false, &thr_);   // ★ 참조를 따라간다
                if (!thr_) return false;
                if (iv.tag != VMV_INT || (sv.tag != VMV_SLICE && sv.tag != VMV_VARRAY)) {
                    vm_diag(vm->diags, "E-VM-TYPE", "index needs slice + int"); return false;
                }
                if (iv.i < 0 || (proven_size_t)iv.i >= sv.n) {
                    // ★ RFC-0055 D5: 제거됐다고 표시된 검사가 실패하면 그것은 **분석의 오류**다.
                    //   VM 은 검사를 유지하고 스스로를 고발한다 — 퍼저가 이 분석의 검증기가 된다.
                    vm_diag(vm->diags, (in->a & IR_POL_PROVEN) ? "E-VM-ANALYSIS" : "E-VM-BOUNDS",
                            (in->a & IR_POL_PROVEN)
                              ? "index bounds check was eliminated but the index is OUT OF BOUNDS "
                                "— the interval/relational analysis is UNSOUND (this is a compiler bug)"
                              : "slice index out of bounds (panic)");
                    return false;
                }
                if (sv.tag == VMV_SLICE) { stack[sp++] = vmv_int((proven_i64)sv.p[iv.i]); break; }
                // ★ 구조체 배열의 원소는 **구조체 뷰**다(무복사). `field` 가 바로 먹는다.
                if (sv.i & IR_STRUCT_BIT) {
                    proven_i32 si = (proven_i32)((sv.i >> 20) & 0xff);
                    stack[sp++] = (vmv_t){ .tag = VMV_VIEW,
                                           .p = sv.p + (proven_size_t)iv.i * (proven_size_t)sv.box,
                                           .n = (proven_size_t)sv.box, .box = si };
                    break;
                }
                proven_u64 x = 0;   // typed array: decode one element (native = le)
                const proven_u8 *p = sv.p + (proven_size_t)iv.i * (proven_size_t)sv.box;
                for (proven_i32 k = sv.box; k-- > 0; ) x = (x << 8) | p[k];
                // ★ 원소가 **부호형**이면 부호 확장한다 — 타입이 이미 그렇게 말했다.
                //   (안 하면 slice i8 의 0xFF 가 255 로, slice i32 의 -1 이 4294967295 로 샌다.)
                stack[sp++] = (sv.i & IR_FLT_BIT)
                                ? vmv_flt(ir_bits_to_f(x, (proven_u8)sv.box))
                                : vmv_int((sv.i & IR_SGN_BIT)
                                            ? ity_wrap((proven_i64)x, (proven_u8)(sv.box * 8), true)
                                            : (proven_i64)x);
                break;
            }
            // ★★★ **배열 내용 술어 검사** — 모든 원소가 cmp N 인지 훑는다(진입 계약).
            case IRW_ELEMCK: {
                if (sp < 2) return false;
                vmv_t nv = stack[--sp], sv = stack[--sp];
                if (nv.tag != VMV_INT || (sv.tag != VMV_SLICE && sv.tag != VMV_VARRAY)) {
                    vm_diag(vm->diags, "E-VM-TYPE", "elem_* needs a slice + int"); return false;
                }
                int cmp = (int)(in->a & 3); proven_i64 N = nv.i; bool ok = true;
                int esz = (sv.tag == VMV_SLICE) ? 1 : (sv.box ? sv.box : 8);
                for (proven_size_t i = 0; i < sv.n && ok; i++) {
                    proven_i64 e;
                    if (sv.tag == VMV_SLICE) e = (proven_i64)sv.p[i];
                    else {
                        proven_u64 x = 0; const proven_u8 *p = sv.p + i * (proven_size_t)esz;
                        for (proven_i32 k = esz; k-- > 0; ) { x = (x << 8) | p[k]; }
                        e = (proven_i64)x;
                    }
                    ok = cmp == 0 ? (e <  N) : cmp == 1 ? (e <= N) : cmp == 2 ? (e >  N) : (e >= N);
                }
                stack[sp++] = vmv_int(ok ? 1 : 0);
                break;
            }
            // ★ SPEC-004 §190 T0 — 비트 재해석. **폭은 안 바꾼다**(그건 변환이지 재해석이 아니다).
            case IRW_BITCAST: {
                if (sp == 0) return false;
                vmv_t v = stack[--sp];
                proven_u8 sz = (proven_u8)(in->a & 0xff);
                bool flt = (in->a & IR_FLT_BIT) != 0, sgn = (in->a & IR_SGN_BIT) != 0;
                proven_u64 bits;
                if (v.tag == VMV_FLT) bits = ir_f_to_bits(vmv_f(v), sz);
                else if (v.tag == VMV_INT) {
                    bits = (proven_u64)v.i;
                    if (sz < 8) bits &= (1ull << (sz * 8)) - 1;      // 폭만큼만 본다
                } else { vm_diag(vm->diags, "E-VM-TYPE", "bit_cast needs a plain scalar"); return false; }
                if (flt) { stack[sp++] = vmv_flt(ir_bits_to_f(bits, sz)); break; }
                proven_i64 out2 = (proven_i64)bits;
                if (sgn && sz < 8) {                                  // 부호 확장 — 같은 비트, 다른 해석
                    proven_u64 sb = 1ull << (sz * 8 - 1);
                    if (bits & sb) out2 = (proven_i64)(bits | ~((1ull << (sz * 8)) - 1));
                }
                stack[sp++] = vmv_int(out2);
                break;
            }
            case IRW_VARRAY: {
                if (sp == 0) return false;
                vmv_t b = stack[--sp];
                proven_u8 esz = (proven_u8)(in->a & 0xff);
                bool eflt = (in->a & IR_FLT_BIT) != 0;
                // ★★★ **멱등이어야 한다.** `slice T`(T ≠ u8) 파라미터는 진입에서 VARRAY 로 감싼다.
                //   그런데 그 값을 **다른 op 에 넘기면** 그쪽 진입에서 **또 감쌌다** —
                //   VARRAY 위에 VARRAY → "view_array needs a byte slice".
                //   즉 **타입 있는 슬라이스를 op 사이로 넘길 수가 없었다.**
                //   `slice u32` 든 `slice <struct>` 든 전부. 그리고 픽스처가 전부 CLI 에서 직접
                //   받기만 했기 때문에 **아무도 못 봤다** — PRINCIPLES.md §0 교훈 6 그대로.
                //   (세 번째 진짜 프로그램이 찾았다. `match` 함정도 그랬다.)
                if (b.tag == VMV_VARRAY) { stack[sp++] = b; break; }   // 이미 감싸여 있다
                if (b.tag != VMV_SLICE) { vm_diag(vm->diags, "E-VM-TYPE", "view_array needs a byte slice"); return false; }
                if (b.n % esz) { vm_diag(vm->diags, "E-VM-VIEW", "view_array: slice length is not a multiple of the element size (panic)"); return false; }
                // ★ 원소가 **구조체**면 그 사실을 나른다 — 그러면 `index` 가 정수가 아니라
                //   **구조체 뷰**를 준다(무복사). 구조체 배열은 시스템 프로그래밍의 기본 모양이다.
                // ★ 원소 종류를 값에 싣는다: 부동(IR_FLT_BIT)·부호(IR_SGN_BIT)·구조체.
                //   부호 비트가 있어야 `index` 가 부호 확장을 한다(구멍이었다: 부호형이 무부호로 샜다).
                proven_i64 etag = (in->a & IR_STRUCT_BIT)
                                    ? (IR_STRUCT_BIT | (((in->a >> 20) & 0xff) << 20))
                                    : ((eflt ? IR_FLT_BIT : 0) | (in->a & IR_SGN_BIT));
                stack[sp++] = (vmv_t){ .tag = VMV_VARRAY, .i = etag, .p = b.p, .n = b.n / esz, .box = (proven_i32)esz };
                break;
            }
            case IRW_STR: {
                if (sp >= VM_STACK) { vm_diag(vm->diags, "E-VM-STACK", "operand stack overflow"); return false; }
                proven_u8str_view_t s = vm->ir->strs[in->a];
                proven_u8 ew = vm->ir->strew[in->a];
                // ★ 폭 1 이면 여태처럼 **바이트 슬라이스**다(표면 불변).
                //   폭 2·4 는 이미 있는 **타입 있는 배열**로 민다 — 새 값 종류 0.
                //   `n` 은 **원소 수**다(바이트 수가 아니다): `len u"AB"` = 2.
                if (ew <= 1) stack[sp++] = (vmv_t){ .tag = VMV_SLICE, .p = s.ptr, .n = s.size };
                else stack[sp++] = (vmv_t){ .tag = VMV_VARRAY, .i = 0, .p = s.ptr,
                                            .n = s.size / ew, .box = (proven_i32)ew };
                break;
            }
            case IRW_SWAP: {   // ★ slice, i, j → () : 두 원소를 맞바꾼다 (원소 타입 무관)
                if (sp < 3) return false;
                vmv_t jv = stack[--sp], iv = stack[--sp];
                bool thr_; vmv_t sv = vm_through(vm, stack[--sp], true, &thr_);   // 쓰기다
                if (!thr_) return false;
                if ((sv.tag != VMV_SLICE && sv.tag != VMV_VARRAY) || iv.tag != VMV_INT || jv.tag != VMV_INT) {
                    vm_diag(vm->diags, "E-VM-TYPE", "swap needs a slice + two ints"); return false;
                }
                if (iv.i < 0 || jv.i < 0 || (proven_size_t)iv.i >= sv.n || (proven_size_t)jv.i >= sv.n) {
                    vm_diag(vm->diags, "E-VM-BOUNDS", "swap index out of bounds (panic)"); return false;
                }
                // 원소 크기 = 타입 있는 배열이면 box, 아니면 1(바이트 슬라이스).
                proven_size_t esz = (sv.tag == VMV_VARRAY && sv.box > 0) ? (proven_size_t)sv.box : 1;
                proven_u8 *base = (proven_u8 *)(void *)(uintptr_t)sv.p;
                proven_u8 *a = base + (proven_size_t)iv.i * esz;
                proven_u8 *b = base + (proven_size_t)jv.i * esz;
                for (proven_size_t z = 0; z < esz; z++) { proven_u8 tmp = a[z]; a[z] = b[z]; b[z] = tmp; }
                // ★ 값을 하나 남긴다 — 문장 자리에서 뒤따르는 `drop` 과 짝을 맞춘다.
                //   안 남기면 drop 이 **밑의 값을 지운다**(VM 은 우연히 통과하고 네이티브는
                //   태그 경로로 떨어져 조용히 아무 일도 안 했다 — 조용히 틀린 답의 전형).
                stack[sp++] = vmv_int(0);
                break;
            }
            case IRW_SUBSLICE: {
                if (sp < 3) return false;
                vmv_t hi = stack[--sp], lo = stack[--sp];
                bool thr_; vmv_t sv = vm_through(vm, stack[--sp], false, &thr_);   // ★ 참조를 따라간다
                if (!thr_) return false;
                if ((sv.tag != VMV_SLICE && sv.tag != VMV_VARRAY) || lo.tag != VMV_INT || hi.tag != VMV_INT) { vm_diag(vm->diags, "E-VM-TYPE", "subslice needs slice + ints"); return false; }
                if (lo.i < 0 || hi.i < lo.i || (proven_size_t)hi.i > sv.n) { vm_diag(vm->diags, "E-VM-BOUNDS", "subslice out of bounds (panic)"); return false; }
                // ★★★ **타입 있는 배열도 자를 수 있어야 한다** (2026-07-20, SoA 시범이 잡았다).
                //   전엔 `VMV_SLICE` 만 받아 `subslice` 가 **`slice u8` 전용**이었다 — `slice u32`·
                //   `slice u64` 는 E-VM-TYPE 이었다. SoA 는 필드마다 **타입 있는 배열**을 쓰므로
                //   정면으로 막혔다. ★ 아무 픽스처도 타입 있는 슬라이스를 자른 적이 없어서
                //   여태 안 보였다(교훈 6 — 픽스처의 모양이 감사의 시야다).
                //   ⇒ VARRAY 는 `.n` 이 **원소 수**이고 `.box` 가 원소 크기다: 자를 때 바이트
                //     오프셋은 **원소 크기를 곱해야** 하고, 원소 종류(`.box`·`.i`)는 물려준다.
                if (sv.tag == VMV_VARRAY)
                    stack[sp++] = (vmv_t){ .tag = VMV_VARRAY, .i = sv.i,
                                           .p = sv.p + (proven_size_t)lo.i * (proven_size_t)sv.box,
                                           .n = (proven_size_t)(hi.i - lo.i), .box = sv.box };
                else
                stack[sp++] = (vmv_t){ .tag = VMV_SLICE, .p = sv.p + lo.i, .n = (proven_size_t)(hi.i - lo.i) };
                break;
            }
            case IRW_WRAP_OK: {
                if (sp == 0) return false;
                if (vm->nbox >= VM_MAXBOX) { vm_diag(vm->diags, "E-VM-BOXPOOL", "the VM's box pool is exhausted — 4096 slots for the values that `some`/`ok` wrap, and this pool is NEVER rewound (docs/runtime-pools.md). \u2605 The emitted C does NOT share this fate for a plain `option u64`: its fast path carries option/result as a (tag, value) PAIR with no pool at all, so the same loop keeps running natively — this is a DIVERGENCE between the backends, not your program being too big"); return false; }
                vm->boxes[vm->nbox] = stack[--sp];
                stack[sp++] = (vmv_t){ .tag = VMV_OK, .box = (proven_i32)vm->nbox++ };
                break;
            }
            // ★★★ option 생성자 (RFC-0043 D5 — *"OOM 은 값이다"*).
            //   소비자(`is_some`·`some_value`)는 **있었는데** 생성자가 **없었다.**
            case IRW_WRAP_SOME: {
                if (sp == 0) return false;
                if (vm->nbox >= VM_MAXBOX) { vm_diag(vm->diags, "E-VM-BOXPOOL", "the VM's box pool is exhausted — 4096 slots for the values that `some`/`ok` wrap, and this pool is NEVER rewound (docs/runtime-pools.md). \u2605 The emitted C does NOT share this fate for a plain `option u64`: its fast path carries option/result as a (tag, value) PAIR with no pool at all, so the same loop keeps running natively — this is a DIVERGENCE between the backends, not your program being too big"); return false; }
                vm->boxes[vm->nbox] = stack[--sp];
                stack[sp++] = (vmv_t){ .tag = VMV_SOME, .box = (proven_i32)vm->nbox++ };
                break;
            }
            case IRW_WRAP_NONE:
                if (sp >= VM_STACK) return false;
                stack[sp++] = (vmv_t){ .tag = VMV_NONE };
                break;
            // ★ RFC-0030 D2′ — 프로그램 인자. VM 은 --run 이 건넨 인자 목록을 든다.
            case IRW_ARGC:
                if (sp >= VM_STACK) return false;
                stack[sp++] = vmv_int((proven_i64)vm->npargs);
                break;
            // ★ RFC-0030 D2′ — 환경 조회. VM 과 네이티브가 **같은 프로세스 전역**(environ)을
            //   읽으므로 같은 환경이면 같은 답이다(argv 와 같은 범주: 결정적 입력).
            // ★ RFC-0069 A2 — 표준출력. ★★ **오라클 밖이 아니다**: VM 과 네이티브가 **같은 fd 에
            //   같은 바이트**를 쓴다(할당처럼 표현이 갈리지 않는다). 게이트가 양쪽 stdout 을 대조한다.
            case IRW_WRITE: {
                if (sp < 2) return false;
                vmv_t bv = stack[--sp], fv = stack[--sp];
                if (fv.tag != VMV_INT || (bv.tag != VMV_SLICE && bv.tag != VMV_VARRAY)) {
                    vm_diag(vm->diags, "E-VM-TYPE", "write_out needs (fd, byte slice)"); return false;
                }
                if (fv.i != 1 && fv.i != 2) {
                    vm_diag(vm->diags, "E-VM-FD", "only 1 (stdout) and 2 (stderr) are writable — a "
                                                  "file descriptor is not a number you may invent"); return false;
                }
                FILE *fp_ = (fv.i == 1) ? stdout : stderr;
                unsigned long hfk_ = 1;
                int hf_ = lw_hf_probe("out", &hfk_);
                if (hf_ == 1) { fflush(fp_); stack[sp++] = vmv_int(0); break; }   // ★ 0 바이트 = 부분 쓰기 실패
                size_t want_ = (hf_ == 2) ? lw_hf_cap(bv.n, hfk_) : bv.n;
                size_t w = fwrite(bv.p, 1, want_, fp_);
                fflush(fp_);
                stack[sp++] = vmv_int((proven_i64)w);
                break;
            }
            // ★★★ RFC-0043 A3 — 얼로케이터에서 n 바이트를 얻는다. **범프 전용, 회수 없음.**
            //   모자라면 `none` — 센티널이 아니라 값이다(널 반환도, 죽는 것도 아니다).
            // ★★ **오라클 안이다**: 아레나는 0 에서 시작하고 같은 프로그램은 같은 순서로
            //   같은 크기를 자르므로 VM 과 네이티브가 **같은 바이트**를 본다. 주소는 다르지만
            //   언어가 주소를 노출하지 않는다 ⇒ malloc 과 달리 diff-sweep 이 이것을 덮는다.
            //   그래서 **명시적으로 0 으로 채운다** — "범프라서 늘 새 바이트다" 는 적히지 않은
            //   불변식이고, 적히지 않은 불변식은 나중에 조용히 깨진다(§0).
            case IRW_ALLOCB: {
                if (sp == 0) return false;
                vmv_t nv = stack[--sp];
                if (nv.tag != VMV_INT) {
                    vm_diag(vm->diags, "E-VM-TYPE", "alloc_bytes needs a byte count"); return false;
                }
                if (nv.i < 0) { stack[sp++] = (vmv_t){ .tag = VMV_NONE }; break; }
                if (vm->nbox >= VM_MAXBOX) { vm_diag(vm->diags, "E-VM-BOXPOOL", "the VM's box pool is exhausted — 4096 slots for the values that `some`/`ok` wrap, and this pool is NEVER rewound (docs/runtime-pools.md). The emitted C shares the same number (LW_BOXPOOL), so a loop that wraps a value per iteration ends the same way there"); return false; }
                // ★ RFC-0112 D3 — 피연산자가 뿌리다(0 고정 창 · 1 힙)
                proven_u8 *base = vm_root_take(vm, (int)in->a, (proven_size_t)nv.i);
                if (!base) { stack[sp++] = (vmv_t){ .tag = VMV_NONE }; break; }   // 더 못 잡는다 = 없음(값)
                vm->boxes[vm->nbox] = (vmv_t){ .tag = VMV_SLICE, .p = base, .n = (proven_size_t)nv.i };
                stack[sp++] = (vmv_t){ .tag = VMV_SOME, .box = (proven_i32)vm->nbox++ };
                break;
            }
            // ★★★ RFC-0069 A4 — 파일 전체를 **호출자 버퍼**로 읽는다. 열고·읽고·닫는다.
            //   · 못 열면 `none` · **안 들어가면 `none`**(잘라서 속이지 않는다 — lib/io.low 의
            //     `peek` 과 같은 규율: *"모자라면 none, 부분으로 속이지 않는다"*)
            //   ★★ **이것은 결정적 입력이다** — 같은 파일이면 VM 과 네이티브가 **같은 바이트**를
            //     얻는다(argv·env 와 같은 범주). RFC-0069 §7 은 os_fd 를 오라클 밖에 뒀으나,
            //     **읽기는 밖이 아니다**: 갈릴 표현이 없다. 밖인 것은 *상태를 바꾸는* 쪽이다.
            // ★★★ RFC-0069 A5 — 표준입력. `write_out` 의 짝이고 **같은 `cap io`** 를 쓴다.
            //   EOF 까지 읽는다 · **안 들어가면 `none`**(read_file 과 같은 규율 — 자르지 않는다).
            //   ★ fd 는 **0 만** 읽을 수 있다 — write_out 이 1·2 만 쓸 수 있는 것과 같은 규율.
            //     서술자는 지어내는 숫자가 아니다.
            case IRW_RDIN: {
                if (sp < 2) return false;
                vmv_t dv = stack[--sp], fv = stack[--sp];
                if (fv.tag != VMV_INT || (dv.tag != VMV_SLICE && dv.tag != VMV_VARRAY)) {
                    vm_diag(vm->diags, "E-VM-TYPE", "read_in needs (fd, destination bytes)"); return false;
                }
                if (fv.i != 0) {
                    vm_diag(vm->diags, "E-VM-FD", "only 0 (stdin) is readable — a file descriptor is "
                                                  "not a number you may invent"); return false;
                }
                // ★★★ **떠본 바이트를 되돌려 놓는다** (2026-07-20 — 데이터 손실 수정).
                //
                //   ☞ 전엔 `fgetc(stdin)` 으로 EOF 를 떠보고 **그 바이트를 그냥 버렸다.**
                //     ⇒ 버퍼를 채울 때마다 **한 바이트씩 사라졌다.** 10000 바이트를 4096 버퍼로
                //       흘리면 **9998** 이 나왔다(실측). 조용한 데이터 손실이다.
                //   ★★★ **오라클이 못 잡았다**: VM 과 네이티브가 **똑같이 틀렸기** 때문이다.
                //     두 백엔드 대조는 *다르게* 틀린 것만 잡는다 — **같이 틀린 것은 못 잡는다.**
                //     그리고 버퍼보다 **큰 입력을 흘려 본 픽스처가 하나도 없었다**(교훈 6).
                //   ⇒ 고침: ① 짧게 읽혔으면 그것이 곧 EOF 다 — **떠볼 필요가 없다.**
                //           ② 가득 찼을 때만 떠보고, 읽었으면 `ungetc` 로 **되돌려 놓는다.**
                proven_u8 *dst = (proven_u8 *)(void *)(uintptr_t)dv.p;
                size_t got = fread(dst, 1, dv.n, stdin);
                int more = 0;
                if (got == dv.n) { int pk = fgetc(stdin); if (pk != EOF) { ungetc(pk, stdin); more = 1; } }
                if (more) { stack[sp++] = (vmv_t){ .tag = VMV_NONE }; break; }
                if (vm->nbox >= VM_MAXBOX) { vm_diag(vm->diags, "E-VM-BOXPOOL", "the VM's box pool is exhausted — 4096 slots for the values that `some`/`ok` wrap, and this pool is NEVER rewound (docs/runtime-pools.md). The emitted C shares the same number (LW_BOXPOOL), so a loop that wraps a value per iteration ends the same way there"); return false; }
                vm->boxes[vm->nbox] = vmv_int((proven_i64)got);
                stack[sp++] = (vmv_t){ .tag = VMV_SOME, .box = (proven_i32)vm->nbox++ };
                break;
            }
            // ★★★ **reactor** (RFC-0071 A1) — VM 은 **블로킹 폴백만** 가진다.
            //
            //   ☞ VM 은 io_uring 을 못 돌린다. 그러면 흉내내는가? **아니다.**
            //     RFC-0071 §3: **시간은 오라클 밖, 바이트와 순서는 오라클 안.** 순차 실행은
            //     **유효한 스케줄 하나**이고(DET-1 과 같은 논증), 인터리빙이 답을 바꾸는
            //     프로그램은 **인터리빙 오라클이 이미 잡는다** ⇒ 새 갭이 아니다.
            //   ★ A1 의 게이트: `r_read` 의 답이 `read_in` 과 **한 바이트도 다르지 않다.**
            //     뒤 단계(epoll·io_uring)가 그것을 깨면 즉시 보인다.
            case IRW_RNEW: {
                if (sp < 2) return false;
                vmv_t dv = stack[--sp], mv = stack[--sp];
                if (dv.tag != VMV_INT || (mv.tag != VMV_SLICE && mv.tag != VMV_VARRAY)) {
                    vm_diag(vm->diags, "E-VM-TYPE", "reactor_new needs (backing bytes, queue depth)");
                    return false;
                }
                // ★ 뒷받침이 모자라면 **none** — 트랩이 아니다(OOM 은 값이다, RFC-0043 D5).
                //   한 자리당 24 바이트를 요구한다(A1 은 안 쓰지만 **약속을 미리 지킨다**:
                //   나중에 커널 큐가 들어와도 호출자가 준 자리 안에서 돌아야 한다).
                proven_i64 need = dv.i * 24;
                if (dv.i <= 0 || (proven_i64)mv.n < need) { stack[sp++] = (vmv_t){ .tag = VMV_NONE }; break; }
                if (vm->nbox >= VM_MAXBOX) { vm_diag(vm->diags, "E-VM-BOXPOOL", "the VM's box pool is exhausted — 4096 slots for the values that `some`/`ok` wrap, and this pool is NEVER rewound (docs/runtime-pools.md). The emitted C shares the same number (LW_BOXPOOL), so a loop that wraps a value per iteration ends the same way there"); return false; }
                vm->boxes[vm->nbox] = vmv_int(1);      // A1: 핸들은 1 하나뿐(폴백은 상태가 없다)
                stack[sp++] = (vmv_t){ .tag = VMV_SOME, .box = (proven_i32)vm->nbox++ };
                break;
            }
            case IRW_RREAD: {
                if (sp < 3) return false;
                vmv_t dv = stack[--sp], fv = stack[--sp], rv = stack[--sp];
                if (rv.tag != VMV_INT || fv.tag != VMV_INT ||
                    (dv.tag != VMV_SLICE && dv.tag != VMV_VARRAY)) {
                    vm_diag(vm->diags, "E-VM-TYPE", "r_read needs (reactor, fd, destination bytes)");
                    return false;
                }
                if (rv.i != 1) {
                    vm_diag(vm->diags, "E-VM-REACTOR", "this is not a reactor handle — a handle is "
                                                       "HANDED to you by `reactor_new`, not invented");
                    return false;
                }
                if (fv.i != 0) {
                    vm_diag(vm->diags, "E-VM-FD", "only 0 (stdin) is readable — a file descriptor is "
                                                  "not a number you may invent"); return false;
                }
                // ★ A1 = **블로킹 폴백**: `read_in` 과 **글자 그대로 같은 일**을 한다.
                //   ☞ 그리고 그 "같은 일" 을 짓다가 `read_in` 의 **데이터 손실**이 나왔다 —
                //     A1 이 벌써 값을 했다(위 IRW_RDIN 의 주석 참조).
                proven_u8 *dst = (proven_u8 *)(void *)(uintptr_t)dv.p;
                size_t got = fread(dst, 1, dv.n, stdin);
                int more = 0;
                if (got == dv.n) { int pk = fgetc(stdin); if (pk != EOF) { ungetc(pk, stdin); more = 1; } }
                if (more) { stack[sp++] = (vmv_t){ .tag = VMV_NONE }; break; }
                if (vm->nbox >= VM_MAXBOX) { vm_diag(vm->diags, "E-VM-BOXPOOL", "the VM's box pool is exhausted — 4096 slots for the values that `some`/`ok` wrap, and this pool is NEVER rewound (docs/runtime-pools.md). The emitted C shares the same number (LW_BOXPOOL), so a loop that wraps a value per iteration ends the same way there"); return false; }
                vm->boxes[vm->nbox] = vmv_int((proven_i64)got);
                stack[sp++] = (vmv_t){ .tag = VMV_SOME, .box = (proven_i32)vm->nbox++ };
                break;
            }
            // ★★★★ **쓰기 쪽** (2026-08-22, N4 단계1) — `r_read` 의 거울.
            //   A1 과 같은 규율: **블로킹 폴백**이고, 답이 `write_out` 과 한 바이트도
            //   다르지 않아야 한다. 그것이 이 단계의 게이트다.
            //   ☞ **부분 진행을 잃지 않는다**: `fwrite` 가 답한 수를 그대로 낸다. 여기(VM)는
            //     블로킹이라 대개 전부 가지만, **덜 갔으면 덜 갔다고 답한다** — `none` 은
            //     *한 바이트도 못 간* 실패에만 쓴다. 그 수를 버리면 호출자가 **어디서부터
            //     다시 보낼지** 모른다.
            case IRW_RWRITE: {
                if (sp < 3) return false;
                vmv_t sv = stack[--sp], fv = stack[--sp], rv = stack[--sp];
                if (rv.tag != VMV_INT || fv.tag != VMV_INT ||
                    (sv.tag != VMV_SLICE && sv.tag != VMV_VARRAY)) {
                    vm_diag(vm->diags, "E-VM-TYPE", "r_write needs (reactor, fd, source bytes)");
                    return false;
                }
                if (rv.i != 1) {
                    vm_diag(vm->diags, "E-VM-REACTOR", "this is not a reactor handle — a handle is "
                                                       "HANDED to you by `reactor_new`, not invented");
                    return false;
                }
                // ★ `write_out` 과 **같은 자리만** 연다: 1(stdout) · 2(stderr).
                //   fd 를 지어내 남의 파일에 쓰는 길을 열지 않는다.
                if (fv.i != 1 && fv.i != 2) {
                    vm_diag(vm->diags, "E-VM-FD", "only 1 (stdout) and 2 (stderr) are writable — a "
                                                  "file descriptor is not a number you may invent");
                    return false;
                }
                const proven_u8 *src = (const proven_u8 *)(const void *)(uintptr_t)sv.p;
                size_t put = fwrite(src, 1, sv.n, fv.i == 1 ? stdout : stderr);
                if (fv.i == 1) fflush(stdout); else fflush(stderr);
                if (vm->nbox >= VM_MAXBOX) { vm_diag(vm->diags, "E-VM-BOXPOOL", "the VM's box pool is exhausted — 4096 slots for the values that `some`/`ok` wrap, and this pool is NEVER rewound (docs/runtime-pools.md). The emitted C shares the same number (LW_BOXPOOL), so a loop that wraps a value per iteration ends the same way there"); return false; }
                vm->boxes[vm->nbox] = vmv_int((proven_i64)put);
                stack[sp++] = (vmv_t){ .tag = VMV_SOME, .box = (proven_i32)vm->nbox++ };
                break;
            }
            // ★★★ **스트림 리프 넷** (RFC-0069 §6). fd 는 정수 핸들이고, close 망각을
            //   컴파일 오류로 만드는 것은 **라이브러리의 몫**이다(lib/file.low) — 리프는 얇다.
            //   ★ VM 과 네이티브가 **같은 libc** 를 부르므로 같은 파일이면 같은 바이트다
            //     (읽기는 결정적 입력 — A4 에서 정한 판정 그대로).
            // ★★★ **어휘 region 블록** — 표식을 잡고 되돌린다. arena 는 bump 이므로 이게 전부다.
            //   ★ 되돌린 뒤의 바이트는 **다시 0 으로 채워져 나간다**(alloc_bytes 가 memset) ⇒
            //     두 백엔드가 같은 것을 본다. 되돌리기가 오라클을 깨지 않는다.
            case IRW_RMARK:
                if (sp >= VM_STACK) return false;
                // ★ 표식은 **그 뿌리의 논리 위치**다(청크 시작 합 + 오프셋). 뿌리는 피연산자다.
                stack[sp++] = vmv_int((proven_i64)vm_root_mark(vm, (int)in->a));
                break;
            case IRW_RRESET: {
                if (sp == 0) return false;
                vmv_t mv = stack[--sp];
                if (mv.tag != VMV_INT || mv.i < 0 || !vm_root_reset(vm, (int)in->a, (proven_size_t)mv.i)) {
                    vm_diag(vm->diags, "E-VM-TYPE", "region reset needs its own mark"); return false;
                }
                break;
            }
            case IRW_FOPEN: {
                if (sp < 2) return false;
                vmv_t mv = stack[--sp], pv = stack[--sp];
                if ((pv.tag != VMV_SLICE && pv.tag != VMV_VARRAY) || mv.tag != VMV_INT) {
                    vm_diag(vm->diags, "E-VM-TYPE", "file_open needs (path bytes, mode int)"); return false;
                }
                char pb[4096];
                if (pv.n == 0 || pv.n >= sizeof pb) { stack[sp++] = (vmv_t){ .tag = VMV_NONE }; break; }
                memcpy(pb, pv.p, pv.n); pb[pv.n] = '\0';
                // mode: 0 = 읽기 · 1 = 쓰기(자르고 만들기) · 2 = 덧붙이기. 그 밖은 거절한다.
                const char *m = (mv.i == 0) ? "rb" : (mv.i == 1) ? "wb" : (mv.i == 2) ? "ab" : NULL;
                if (!m) {
                    vm_diag(vm->diags, "E-VM-FMODE",
                            "open mode must be 0 (read), 1 (write) or 2 (append) — a mode is not a "
                            "number you may invent"); return false;
                }
                if (lw_hf_probe("open", NULL) == 1) { stack[sp++] = (vmv_t){ .tag = VMV_NONE }; break; }
                FILE *fh = fopen(pb, m);
                if (!fh) { stack[sp++] = (vmv_t){ .tag = VMV_NONE }; break; }
                proven_size_t sl = 0;
                while (sl < VM_MAXFILE && vm->files[sl]) sl++;
                if (sl >= VM_MAXFILE) { fclose(fh); vm_diag(vm->diags, "E-VM-FILEPOOL", "too many open files"); return false; }
                vm->files[sl] = fh;
                if (vm->nbox >= VM_MAXBOX) { vm_diag(vm->diags, "E-VM-BOXPOOL", "the VM's box pool is exhausted — 4096 slots for the values that `some`/`ok` wrap, and this pool is NEVER rewound (docs/runtime-pools.md). The emitted C shares the same number (LW_BOXPOOL), so a loop that wraps a value per iteration ends the same way there"); return false; }
                vm->boxes[vm->nbox] = vmv_int((proven_i64)sl);
                stack[sp++] = (vmv_t){ .tag = VMV_SOME, .box = (proven_i32)vm->nbox++ };
                break;
            }
            case IRW_FREAD: {
                if (sp < 2) return false;
                vmv_t dv = stack[--sp], hv = stack[--sp];
                if (hv.tag != VMV_INT || (dv.tag != VMV_SLICE && dv.tag != VMV_VARRAY)) {
                    vm_diag(vm->diags, "E-VM-TYPE", "file_read needs (handle, destination bytes)"); return false;
                }
                if (hv.i < 0 || hv.i >= VM_MAXFILE || !vm->files[hv.i]) {
                    vm_diag(vm->diags, "E-VM-FHANDLE", "not an open file handle"); return false;
                }
                unsigned long hfk_ = 1;
                int hf_ = lw_hf_probe("read", &hfk_);
                if (hf_ == 1) { stack[sp++] = (vmv_t){ .tag = VMV_NONE }; break; }   // ★ 실패는 값이다
                size_t want_ = (hf_ == 2) ? lw_hf_cap(dv.n, hfk_) : dv.n;
                size_t got = fread((proven_u8 *)(void *)(uintptr_t)dv.p, 1, want_, vm->files[hv.i]);
                if (vm->nbox >= VM_MAXBOX) { vm_diag(vm->diags, "E-VM-BOXPOOL", "the VM's box pool is exhausted — 4096 slots for the values that `some`/`ok` wrap, and this pool is NEVER rewound (docs/runtime-pools.md). The emitted C shares the same number (LW_BOXPOOL), so a loop that wraps a value per iteration ends the same way there"); return false; }
                vm->boxes[vm->nbox] = vmv_int((proven_i64)got);   // ★ 0 = EOF (실패가 아니다)
                stack[sp++] = (vmv_t){ .tag = VMV_SOME, .box = (proven_i32)vm->nbox++ };
                break;
            }
            case IRW_FWRITE: {
                if (sp < 2) return false;
                vmv_t bv = stack[--sp], hv = stack[--sp];
                if (hv.tag != VMV_INT || (bv.tag != VMV_SLICE && bv.tag != VMV_VARRAY)) {
                    vm_diag(vm->diags, "E-VM-TYPE", "file_write needs (handle, byte slice)"); return false;
                }
                if (hv.i < 0 || hv.i >= VM_MAXFILE || !vm->files[hv.i]) {
                    vm_diag(vm->diags, "E-VM-FHANDLE", "not an open file handle"); return false;
                }
                unsigned long hfk_ = 1;
                int hf_ = lw_hf_probe("write", &hfk_);
                if (hf_ == 1) { stack[sp++] = (vmv_t){ .tag = VMV_NONE }; break; }
                size_t want_ = (hf_ == 2) ? lw_hf_cap(bv.n, hfk_) : bv.n;
                size_t w = fwrite(bv.p, 1, want_, vm->files[hv.i]);
                // ★★ **모자란 쓰기는 성공이 아니다** (결함 노트 #65, 2026-09-16). 읽기 모드로 연
                //   파일에 쓰면 `fwrite` 가 0 을 답하는데, 그 0 을 `some 0`("0 바이트 썼다")으로
                //   싸서 **실패가 성공처럼** 보였다 — `is_ok w` 만 본 호출자는 그냥 지나간다.
                //   `ferror` 가 켜졌으면 답은 **없음**이다(스트림 오류는 값이 아니라 실패다).
                if (w < want_ && ferror(vm->files[hv.i])) {
                    clearerr(vm->files[hv.i]);
                    stack[sp++] = (vmv_t){ .tag = VMV_NONE };
                    break;
                }
                if (vm->nbox >= VM_MAXBOX) { vm_diag(vm->diags, "E-VM-BOXPOOL", "the VM's box pool is exhausted — 4096 slots for the values that `some`/`ok` wrap, and this pool is NEVER rewound (docs/runtime-pools.md). The emitted C shares the same number (LW_BOXPOOL), so a loop that wraps a value per iteration ends the same way there"); return false; }
                vm->boxes[vm->nbox] = vmv_int((proven_i64)w);
                stack[sp++] = (vmv_t){ .tag = VMV_SOME, .box = (proven_i32)vm->nbox++ };
                break;
            }
            case IRW_FCLOSE: {
                if (sp < 1) return false;
                vmv_t hv = stack[--sp];
                if (hv.tag != VMV_INT) { vm_diag(vm->diags, "E-VM-TYPE", "file_close needs a handle"); return false; }
                if (hv.i < 0 || hv.i >= VM_MAXFILE || !vm->files[hv.i]) {
                    vm_diag(vm->diags, "E-VM-FHANDLE", "not an open file handle"); return false;
                }
                int hfc_ = lw_hf_probe("close", NULL);
                int rc = fclose(vm->files[hv.i]);
                vm->files[hv.i] = NULL;
                if (hfc_ == 1) rc = -1;   // ★ 닫기 실패를 부른다 — 자원은 실제로 놓고 답만 실패다
                stack[sp++] = vmv_int(rc == 0 ? 1 : 0);   // ★ 닫기도 실패할 수 있다(NFS)
                break;
            }
            // ★★★ 소켓 리프 (cap net) — AF_UNIX socketpair. 핸들=풀 인덱스(socks[i]=fd+1, 0=빈).
            case IRW_NPAIR: {
                int sv[2];
                if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv) != 0) { stack[sp++] = (vmv_t){ .tag = VMV_NONE }; break; }
                proven_size_t s0 = 0; while (s0 < VM_MAXSOCK && vm->socks[s0]) s0++;
                proven_size_t s1 = s0 + 1; while (s1 < VM_MAXSOCK && vm->socks[s1]) s1++;
                if (s0 >= VM_MAXSOCK || s1 >= VM_MAXSOCK) {
                    close(sv[0]); close(sv[1]);
                    vm_diag(vm->diags, "E-VM-SOCKPOOL", "too many open sockets"); return false;
                }
                vm->socks[s0] = sv[0] + 1; vm->socks[s1] = sv[1] + 1;
                if (vm->nbox >= VM_MAXBOX) { vm_diag(vm->diags, "E-VM-BOXPOOL", "the VM's box pool is exhausted — 4096 slots for the values that `some`/`ok` wrap, and this pool is NEVER rewound (docs/runtime-pools.md). The emitted C shares the same number (LW_BOXPOOL), so a loop that wraps a value per iteration ends the same way there"); return false; }
                vm->boxes[vm->nbox] = vmv_int((proven_i64)((proven_u64)s0 | ((proven_u64)s1 << 32)));
                stack[sp++] = (vmv_t){ .tag = VMV_SOME, .box = (proven_i32)vm->nbox++ };
                break;
            }
            case IRW_NSEND: {
                if (sp < 2) return false;
                vmv_t bv = stack[--sp], hv = stack[--sp];
                if (hv.tag != VMV_INT || (bv.tag != VMV_SLICE && bv.tag != VMV_VARRAY)) {
                    vm_diag(vm->diags, "E-VM-TYPE", "net_send needs (handle, byte slice)"); return false;
                }
                if (hv.i < 0 || hv.i >= VM_MAXSOCK || !vm->socks[hv.i]) {
                    vm_diag(vm->diags, "E-VM-SHANDLE", "not an open socket handle"); return false;
                }
                unsigned long hfk_ = 1;
                int hf_ = lw_hf_probe("send", &hfk_);
                if (hf_ == 1) { stack[sp++] = (vmv_t){ .tag = VMV_NONE }; break; }
                ssize_t w = send(vm->socks[hv.i] - 1, bv.p,
                                 (hf_ == 2) ? lw_hf_cap(bv.n, hfk_) : bv.n, 0);
                if (w < 0) { stack[sp++] = (vmv_t){ .tag = VMV_NONE }; break; }
                if (vm->nbox >= VM_MAXBOX) { vm_diag(vm->diags, "E-VM-BOXPOOL", "the VM's box pool is exhausted — 4096 slots for the values that `some`/`ok` wrap, and this pool is NEVER rewound (docs/runtime-pools.md). The emitted C shares the same number (LW_BOXPOOL), so a loop that wraps a value per iteration ends the same way there"); return false; }
                vm->boxes[vm->nbox] = vmv_int((proven_i64)w);
                stack[sp++] = (vmv_t){ .tag = VMV_SOME, .box = (proven_i32)vm->nbox++ };
                break;
            }
            case IRW_NRECV: {
                if (sp < 2) return false;
                vmv_t dv = stack[--sp], hv = stack[--sp];
                if (hv.tag != VMV_INT || (dv.tag != VMV_SLICE && dv.tag != VMV_VARRAY)) {
                    vm_diag(vm->diags, "E-VM-TYPE", "net_recv needs (handle, destination bytes)"); return false;
                }
                if (hv.i < 0 || hv.i >= VM_MAXSOCK || !vm->socks[hv.i]) {
                    vm_diag(vm->diags, "E-VM-SHANDLE", "not an open socket handle"); return false;
                }
                unsigned long hfk_ = 1;
                int hf_ = lw_hf_probe("recv", &hfk_);
                if (hf_ == 1) { stack[sp++] = (vmv_t){ .tag = VMV_NONE }; break; }
                ssize_t got = recv(vm->socks[hv.i] - 1, (proven_u8 *)(void *)(uintptr_t)dv.p,
                                   (hf_ == 2) ? lw_hf_cap(dv.n, hfk_) : dv.n, 0);
                if (got < 0) { stack[sp++] = (vmv_t){ .tag = VMV_NONE }; break; }
                if (vm->nbox >= VM_MAXBOX) { vm_diag(vm->diags, "E-VM-BOXPOOL", "the VM's box pool is exhausted — 4096 slots for the values that `some`/`ok` wrap, and this pool is NEVER rewound (docs/runtime-pools.md). The emitted C shares the same number (LW_BOXPOOL), so a loop that wraps a value per iteration ends the same way there"); return false; }
                vm->boxes[vm->nbox] = vmv_int((proven_i64)got);   // ★ 0 = 상대가 닫음(EOF, 실패 아님)
                stack[sp++] = (vmv_t){ .tag = VMV_SOME, .box = (proven_i32)vm->nbox++ };
                break;
            }
            case IRW_NCLOSE: {
                if (sp < 1) return false;
                vmv_t hv = stack[--sp];
                if (hv.tag != VMV_INT) { vm_diag(vm->diags, "E-VM-TYPE", "net_close needs a handle"); return false; }
                if (hv.i < 0 || hv.i >= VM_MAXSOCK || !vm->socks[hv.i]) {
                    vm_diag(vm->diags, "E-VM-SHANDLE", "not an open socket handle"); return false;
                }
                int rc = close(vm->socks[hv.i] - 1);
                vm->socks[hv.i] = 0;
                stack[sp++] = vmv_int(rc == 0 ? 1 : 0);
                break;
            }
            // ★★★ 네트워크 면 — 127.0.0.1 TCP loopback. 포트 0 = 임시포트(충돌 없음 ⇒ 결정적).
            case IRW_NLISTEN: {
                if (sp < 1) return false;
                vmv_t pv = stack[--sp];
                if (pv.tag != VMV_INT) { vm_diag(vm->diags, "E-VM-TYPE", "net_listen needs a port"); return false; }
                if (pv.i < 0 || pv.i > 65535) { vm_diag(vm->diags, "E-VM-TYPE", "port out of range"); return false; }
                int fd = socket(AF_INET, SOCK_STREAM, 0);
                if (fd < 0) { stack[sp++] = (vmv_t){ .tag = VMV_NONE }; break; }
                int one = 1; (void)setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
                struct sockaddr_in sa; memset(&sa, 0, sizeof sa);
                sa.sin_family = AF_INET; sa.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
                sa.sin_port = htons((unsigned short)pv.i);
                if (bind(fd, (struct sockaddr *)&sa, sizeof sa) != 0 || listen(fd, 16) != 0) {
                    close(fd); stack[sp++] = (vmv_t){ .tag = VMV_NONE }; break;
                }
                proven_size_t sl = 0; while (sl < VM_MAXSOCK && vm->socks[sl]) sl++;
                if (sl >= VM_MAXSOCK) { close(fd); vm_diag(vm->diags, "E-VM-SOCKPOOL", "too many open sockets"); return false; }
                vm->socks[sl] = fd + 1;
                if (vm->nbox >= VM_MAXBOX) { vm_diag(vm->diags, "E-VM-BOXPOOL", "the VM's box pool is exhausted — 4096 slots for the values that `some`/`ok` wrap, and this pool is NEVER rewound (docs/runtime-pools.md). The emitted C shares the same number (LW_BOXPOOL), so a loop that wraps a value per iteration ends the same way there"); return false; }
                vm->boxes[vm->nbox] = vmv_int((proven_i64)sl);
                stack[sp++] = (vmv_t){ .tag = VMV_SOME, .box = (proven_i32)vm->nbox++ };
                break;
            }
            case IRW_NPORT: {
                if (sp < 1) return false;
                vmv_t hv = stack[--sp];
                if (hv.tag != VMV_INT) { vm_diag(vm->diags, "E-VM-TYPE", "net_port needs a handle"); return false; }
                if (hv.i < 0 || hv.i >= VM_MAXSOCK || !vm->socks[hv.i]) {
                    vm_diag(vm->diags, "E-VM-SHANDLE", "not an open socket handle"); return false;
                }
                struct sockaddr_in sa; socklen_t sl2 = sizeof sa; memset(&sa, 0, sizeof sa);
                if (getsockname(vm->socks[hv.i] - 1, (struct sockaddr *)&sa, &sl2) != 0) {
                    stack[sp++] = (vmv_t){ .tag = VMV_NONE }; break;
                }
                if (vm->nbox >= VM_MAXBOX) { vm_diag(vm->diags, "E-VM-BOXPOOL", "the VM's box pool is exhausted — 4096 slots for the values that `some`/`ok` wrap, and this pool is NEVER rewound (docs/runtime-pools.md). The emitted C shares the same number (LW_BOXPOOL), so a loop that wraps a value per iteration ends the same way there"); return false; }
                vm->boxes[vm->nbox] = vmv_int((proven_i64)ntohs(sa.sin_port));
                stack[sp++] = (vmv_t){ .tag = VMV_SOME, .box = (proven_i32)vm->nbox++ };
                break;
            }
            case IRW_NCONNECT: {
                if (sp < 1) return false;
                vmv_t pv = stack[--sp];
                if (pv.tag != VMV_INT) { vm_diag(vm->diags, "E-VM-TYPE", "net_connect needs a port"); return false; }
                if (pv.i < 0 || pv.i > 65535) { vm_diag(vm->diags, "E-VM-TYPE", "port out of range"); return false; }
                if (lw_hf_probe("connect", NULL) == 1) { stack[sp++] = (vmv_t){ .tag = VMV_NONE }; break; }
                int fd = socket(AF_INET, SOCK_STREAM, 0);
                if (fd < 0) { stack[sp++] = (vmv_t){ .tag = VMV_NONE }; break; }
                struct sockaddr_in sa; memset(&sa, 0, sizeof sa);
                sa.sin_family = AF_INET; sa.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
                sa.sin_port = htons((unsigned short)pv.i);
                if (connect(fd, (struct sockaddr *)&sa, sizeof sa) != 0) {
                    close(fd); stack[sp++] = (vmv_t){ .tag = VMV_NONE }; break;
                }
                proven_size_t sl = 0; while (sl < VM_MAXSOCK && vm->socks[sl]) sl++;
                if (sl >= VM_MAXSOCK) { close(fd); vm_diag(vm->diags, "E-VM-SOCKPOOL", "too many open sockets"); return false; }
                vm->socks[sl] = fd + 1;
                if (vm->nbox >= VM_MAXBOX) { vm_diag(vm->diags, "E-VM-BOXPOOL", "the VM's box pool is exhausted — 4096 slots for the values that `some`/`ok` wrap, and this pool is NEVER rewound (docs/runtime-pools.md). The emitted C shares the same number (LW_BOXPOOL), so a loop that wraps a value per iteration ends the same way there"); return false; }
                vm->boxes[vm->nbox] = vmv_int((proven_i64)sl);
                stack[sp++] = (vmv_t){ .tag = VMV_SOME, .box = (proven_i32)vm->nbox++ };
                break;
            }
            case IRW_NACCEPT: {
                if (sp < 1) return false;
                vmv_t hv = stack[--sp];
                if (hv.tag != VMV_INT) { vm_diag(vm->diags, "E-VM-TYPE", "net_accept needs a handle"); return false; }
                if (hv.i < 0 || hv.i >= VM_MAXSOCK || !vm->socks[hv.i]) {
                    vm_diag(vm->diags, "E-VM-SHANDLE", "not an open socket handle"); return false;
                }
                if (lw_hf_probe("accept", NULL) == 1) { stack[sp++] = (vmv_t){ .tag = VMV_NONE }; break; }
                int cfd = accept(vm->socks[hv.i] - 1, NULL, NULL);
                if (cfd < 0) { stack[sp++] = (vmv_t){ .tag = VMV_NONE }; break; }
                proven_size_t sl = 0; while (sl < VM_MAXSOCK && vm->socks[sl]) sl++;
                if (sl >= VM_MAXSOCK) { close(cfd); vm_diag(vm->diags, "E-VM-SOCKPOOL", "too many open sockets"); return false; }
                vm->socks[sl] = cfd + 1;
                if (vm->nbox >= VM_MAXBOX) { vm_diag(vm->diags, "E-VM-BOXPOOL", "the VM's box pool is exhausted — 4096 slots for the values that `some`/`ok` wrap, and this pool is NEVER rewound (docs/runtime-pools.md). The emitted C shares the same number (LW_BOXPOOL), so a loop that wraps a value per iteration ends the same way there"); return false; }
                vm->boxes[vm->nbox] = vmv_int((proven_i64)sl);
                stack[sp++] = (vmv_t){ .tag = VMV_SOME, .box = (proven_i32)vm->nbox++ };
                break;
            }
            case IRW_FSEEK: {
                if (sp < 3) return false;
                vmv_t wv = stack[--sp], ov = stack[--sp], hv = stack[--sp];   // whence, offset, handle
                if (hv.tag != VMV_INT || ov.tag != VMV_INT || wv.tag != VMV_INT) {
                    vm_diag(vm->diags, "E-VM-TYPE", "file_seek needs (handle, offset, whence) as ints"); return false;
                }
                if (hv.i < 0 || hv.i >= VM_MAXFILE || !vm->files[hv.i]) {
                    vm_diag(vm->diags, "E-VM-FHANDLE", "not an open file handle"); return false;
                }
                // whence: 0 = set(절대) · 1 = cur(현재 기준) · 2 = end(끝 기준). 그 밖은 거절한다.
                int w = (wv.i == 0) ? SEEK_SET : (wv.i == 1) ? SEEK_CUR : (wv.i == 2) ? SEEK_END : -1;
                if (w < 0) {
                    vm_diag(vm->diags, "E-VM-FWHENCE",
                            "seek whence must be 0 (set), 1 (cur) or 2 (end) — a whence is not a "
                            "number you may invent (mirror of E-VM-FMODE)"); return false;
                }
                if (fseek(vm->files[hv.i], (long)ov.i, w) != 0) { stack[sp++] = (vmv_t){ .tag = VMV_NONE }; break; }
                long pos = ftell(vm->files[hv.i]);
                if (pos < 0) { stack[sp++] = (vmv_t){ .tag = VMV_NONE }; break; }
                if (vm->nbox >= VM_MAXBOX) { vm_diag(vm->diags, "E-VM-BOXPOOL", "the VM's box pool is exhausted — 4096 slots for the values that `some`/`ok` wrap, and this pool is NEVER rewound (docs/runtime-pools.md). The emitted C shares the same number (LW_BOXPOOL), so a loop that wraps a value per iteration ends the same way there"); return false; }
                vm->boxes[vm->nbox] = vmv_int((proven_i64)pos);   // ★ 새 절대 위치
                stack[sp++] = (vmv_t){ .tag = VMV_SOME, .box = (proven_i32)vm->nbox++ };
                break;
            }
            case IRW_DOPEN: {
                if (sp < 1) return false;
                vmv_t pv = stack[--sp];
                if (pv.tag != VMV_SLICE && pv.tag != VMV_VARRAY) {
                    vm_diag(vm->diags, "E-VM-TYPE", "dir_open needs a path (byte slice)"); return false;
                }
                char pb[4096];
                if (pv.n == 0 || pv.n >= sizeof pb) { stack[sp++] = (vmv_t){ .tag = VMV_NONE }; break; }
                memcpy(pb, pv.p, pv.n); pb[pv.n] = '\0';
                DIR *dh = opendir(pb);
                if (!dh) { stack[sp++] = (vmv_t){ .tag = VMV_NONE }; break; }
                proven_size_t sl = 0;
                while (sl < VM_MAXFILE && vm->dirs[sl]) sl++;
                if (sl >= VM_MAXFILE) { closedir(dh); vm_diag(vm->diags, "E-VM-FILEPOOL", "too many open directories"); return false; }
                vm->dirs[sl] = dh;
                if (vm->nbox >= VM_MAXBOX) { vm_diag(vm->diags, "E-VM-BOXPOOL", "the VM's box pool is exhausted — 4096 slots for the values that `some`/`ok` wrap, and this pool is NEVER rewound (docs/runtime-pools.md). The emitted C shares the same number (LW_BOXPOOL), so a loop that wraps a value per iteration ends the same way there"); return false; }
                vm->boxes[vm->nbox] = vmv_int((proven_i64)sl);
                stack[sp++] = (vmv_t){ .tag = VMV_SOME, .box = (proven_i32)vm->nbox++ };
                break;
            }
            case IRW_TTYRAW: {   // (on) → bool  raw 모드 진입/복귀
                if (sp < 1) return false;
                vmv_t onv = stack[--sp];
                if (onv.tag != VMV_INT) { vm_diag(vm->diags, "E-VM-TYPE", "tty_raw needs a bool"); return false; }
                // ★★★ **원래 설정을 기억한다.** raw 로 바꿔 놓고 못 돌아오면 사용자의 셸이
                //   망가진 채 남는다 — 프로그램이 죽어도 터미널은 남기 때문이다.
                static struct termios saved;
                static int have_saved = 0;
                if (!isatty(0)) { stack[sp++] = vmv_int(0); break; }   // 터미널이 아니면 거짓 — 값으로 답한다
                if (onv.i) {
                    if (!have_saved) { if (tcgetattr(0, &saved) != 0) { stack[sp++] = vmv_int(0); break; } have_saved = 1; }
                    struct termios raw = saved;
                    raw.c_lflag &= (tcflag_t)~(ICANON | ECHO);
                    raw.c_cc[VMIN] = 0; raw.c_cc[VTIME] = 0;   // 논블로킹 — 없으면 0 바이트
                    stack[sp++] = vmv_int(tcsetattr(0, TCSANOW, &raw) == 0 ? 1 : 0);
                } else {
                    if (!have_saved) { stack[sp++] = vmv_int(1); break; }   // 켠 적이 없으면 되돌릴 것도 없다
                    stack[sp++] = vmv_int(tcsetattr(0, TCSANOW, &saved) == 0 ? 1 : 0);
                }
                break;
            }
            case IRW_TTYREAD: {   // (dst) → option u64 (읽은 바이트 수 · 0 = 지금은 없음)
                if (sp < 1) return false;
                bool thr_; vmv_t dv = vm_through(vm, stack[--sp], true, &thr_);
                if (!thr_) return false;
                if (dv.tag != VMV_SLICE && dv.tag != VMV_VARRAY) {
                    vm_diag(vm->diags, "E-VM-TYPE", "tty_read needs a destination buffer"); return false;
                }
                ssize_t got = read(0, (void *)(uintptr_t)dv.p, (size_t)dv.n);
                if (got < 0) { stack[sp++] = (vmv_t){ .tag = VMV_NONE }; break; }   // 오류 = none
                if (vm->nbox >= VM_MAXBOX) { vm_diag(vm->diags, "E-VM-BOXPOOL", "the VM's box pool is exhausted — 4096 slots for the values that `some`/`ok` wrap, and this pool is NEVER rewound (docs/runtime-pools.md). The emitted C shares the same number (LW_BOXPOOL), so a loop that wraps a value per iteration ends the same way there"); return false; }
                vm->boxes[vm->nbox] = vmv_int((proven_i64)got);
                stack[sp++] = (vmv_t){ .tag = VMV_SOME, .box = (proven_i32)vm->nbox++ };
                break;
            }
            // ★★★★★ **시계** (RFC-0090 N1) — proven_c_lib 의 것을 그대로 부른다.
            //   ☞ 오라클은 **단조성**이다: `now` 두 번은 뒤가 크거나 같다. 절대값은 오라클이
            //     없다(무엇과 대볼 것인가?) — 그래서 그것을 **주장하지 않는다**.
            case IRW_TIMENOW: {   // () → u64  단조 ns
                if (sp >= VM_STACK) { vm_diag(vm->diags, "E-VM-STACK", "operand stack overflow"); return false; }
                stack[sp++] = vmv_int((proven_i64)proven_sys_time_now_ns());
                break;
            }
            case IRW_TIMESLEEP: {   // (ms) → u64  실제로 잔 ms
                if (sp < 1) return false;
                vmv_t mv = stack[--sp];
                if (mv.tag != VMV_INT) { vm_diag(vm->diags, "E-VM-TYPE", "time_sleep needs a millisecond count"); return false; }
                // ★★★ **걸음 예산은 시간을 못 센다.** 오라클(계약 경계 시험 · 차등 훑기)은
                //   op 을 **진짜로 돌린다**. 그런데 `sleep_ms` 는 *한 걸음*이면서 **60초**를
                //   쓴다 — 2천만 걸음 예산이 이것을 전혀 막지 못한다. 실측: `--ir timeit.low`
                //   이 180초 타임아웃에 걸렸고, `--check` 는 멀쩡했다(교훈: 도구가 멈추지
                //   않으면 그것은 도구의 결함이다 — 이 파일 위쪽 같은 자리에 이미 적혀 있다).
                //   ⇒ **예산이 켜져 있으면**(도구가 부른 실행) 자지 않고 **예산 히트로 센다**.
                //     조용히 0 을 돌려주면 오라클이 "잤다" 고 믿는다 — 그래서 세어서 보고한다.
                //     사용자 실행은 예산 0(무한)이라 이 갈래를 타지 않는다.
                proven_u64 t0 = proven_sys_time_now_ns();
                if (vm->budget) { vm->budget_hit = true; mv.i = 0; }
                if (mv.i > 0) proven_sys_time_sleep_ms((unsigned)(mv.i > 60000 ? 60000 : mv.i));
                proven_u64 t1 = proven_sys_time_now_ns();
                if (sp >= VM_STACK) { vm_diag(vm->diags, "E-VM-STACK", "operand stack overflow"); return false; }
                stack[sp++] = vmv_int((proven_i64)((t1 - t0) / 1000000ull));
                break;
            }
            case IRW_TIMELOCAL: {   // () → u64  날짜시각 팩
                // 팩: 연<<36 | 월<<32 | 일<<27 | 시<<22 | 분<<16 | 초<<10  (lib/clock.low 이 푼다)
                proven_sys_datetime_t dt = { 0 };
                proven_sys_time_now_local(&dt);
                proven_u64 v = ((proven_u64)dt.year << 36) | ((proven_u64)dt.month << 32)
                             | ((proven_u64)dt.day << 27) | ((proven_u64)dt.hour << 22)
                             | ((proven_u64)dt.min << 16) | ((proven_u64)dt.sec << 10);
                if (sp >= VM_STACK) { vm_diag(vm->diags, "E-VM-STACK", "operand stack overflow"); return false; }
                stack[sp++] = vmv_int((proven_i64)v);
                break;
            }
            // ★★★ 해시 둘 (RFC-0090 N3) — **알고리즘을 여기 적어 둔다.** 벤더 라이브러리를
            //   부르지 않는다: 방출 C 는 **자립**해야 하므로(`cc file.c -lm`) 네이티브가
            //   같은 식을 스스로 갖고 있어야 하고, 그러면 **두 벌이 같은지**가 관건이 된다.
            //   ⇒ 두 벌을 나란히 두고 **같은 상수·같은 순서**로 쓴다(FNV-1a 64 · 반사 CRC-32).
            //   ☞ 표를 안 쓰고 비트 8 번 도는 형태를 고른 이유도 그것이다 — 256칸 표를
            //     두 곳에 복사하면 **한쪽만 고쳐질 자리**가 하나 더 생긴다.
            case IRW_HASH64: {   // (slice) → u64  FNV-1a 64
                if (sp < 1) return false;
                bool thr_h; vmv_t s = vm_through(vm, stack[--sp], false, &thr_h);
                if (s.tag != VMV_SLICE && s.tag != VMV_VARRAY && s.tag != VMV_VIEW) {
                    vm_diag(vm->diags, "E-VM-TYPE", "hash_bytes needs a slice"); return false;
                }
                proven_u64 h = 0xcbf29ce484222325ull;
                for (proven_size_t q = 0; q < s.n; q++) { h ^= (proven_u64)s.p[q]; h *= 0x100000001b3ull; }
                stack[sp++] = vmv_int((proven_i64)h);
                break;
            }
            case IRW_SAMESL: {   // (slice, slice) → bool — 같은 시작·같은 길이 (RFC-0112 D10)
                if (sp < 2) return false;
                bool thr_b; vmv_t b = vm_through(vm, stack[--sp], false, &thr_b);
                bool thr_a; vmv_t a = vm_through(vm, stack[--sp], false, &thr_a);
                bool sa = a.tag == VMV_SLICE || a.tag == VMV_VARRAY || a.tag == VMV_VIEW;
                bool sb = b.tag == VMV_SLICE || b.tag == VMV_VARRAY || b.tag == VMV_VIEW;
                if (!sa || !sb) { vm_diag(vm->diags, "E-VM-TYPE", "same_slice needs two slices"); return false; }
                stack[sp++] = vmv_int((a.p == b.p && a.n == b.n) ? 1 : 0);
                break;
            }
            case IRW_CRC32: {    // (slice) → u64  반사 CRC-32 (0xEDB88320)
                if (sp < 1) return false;
                bool thr_c; vmv_t s = vm_through(vm, stack[--sp], false, &thr_c);
                if (s.tag != VMV_SLICE && s.tag != VMV_VARRAY && s.tag != VMV_VIEW) {
                    vm_diag(vm->diags, "E-VM-TYPE", "crc32 needs a slice"); return false;
                }
                proven_u32 crc = 0xffffffffu;
                for (proven_size_t q = 0; q < s.n; q++) {
                    crc ^= (proven_u32)s.p[q];
                    for (int b = 0; b < 8; b++)
                        crc = (crc >> 1) ^ (0xedb88320u & (proven_u32)-(proven_i32)(crc & 1u));
                }
                stack[sp++] = vmv_int((proven_i64)(proven_u64)(crc ^ 0xffffffffu));
                break;
            }
            // ★★★ **재현 가능한 난수** (RFC-0090 N3b) — 권한 없음. splitmix64.
            //   상태를 받아 **다음 상태**를 돌려준다. 순수 함수라 시험이 재현된다.
            //   ★ 알고리즘을 고정한다(해시와 같은 이유): 고정 안 하면 "수가 나온다" 말고
            //     검사할 것이 없고, 두 뒤끝이 갈려도 모른다. splitmix64 는 상수가 규격이다.
            case IRW_SHA256: {   // (src, mut dst32) → u64 써 넣은 바이트 수 — 권한 없음
                if (sp < 2) return false;
                bool t2_; vmv_t dst = vm_through(vm, stack[--sp], true, &t2_);
                bool t1_; vmv_t src = vm_through(vm, stack[--sp], false, &t1_);
                if ((src.tag != VMV_SLICE && src.tag != VMV_VARRAY && src.tag != VMV_VIEW) ||
                    (dst.tag != VMV_SLICE && dst.tag != VMV_VARRAY)) {
                    vm_diag(vm->diags, "E-VM-TYPE", "sha256 needs (bytes, mutable 32-byte slice)"); return false;
                }
                long long wrote = lw_sha256(src.p, src.n, (unsigned char *)(proven_u8 *)dst.p, dst.n);
                stack[sp++] = vmv_int((proven_i64)wrote);
                break;
            }
            // ★ SHA-512 — 서명 쪽 전용(Ed25519 가 요구한다). 버퍼가 64 보다 짧으면 0 을
            //   답하고 **아무것도 안 쓴다** — sha256 과 같은 규율이다.
            case IRW_SHA512: {   // (src, mut dst64) → u64 써 넣은 바이트 수 — 권한 없음
                if (sp < 2) return false;
                bool t2b_; vmv_t dst = vm_through(vm, stack[--sp], true, &t2b_);
                bool t1b_; vmv_t src = vm_through(vm, stack[--sp], false, &t1b_);
                if ((src.tag != VMV_SLICE && src.tag != VMV_VARRAY && src.tag != VMV_VIEW) ||
                    (dst.tag != VMV_SLICE && dst.tag != VMV_VARRAY)) {
                    vm_diag(vm->diags, "E-VM-TYPE", "sha512 needs (bytes, mutable 64-byte slice)"); return false;
                }
                long long wrote5 = lw_sha512(src.p, src.n, (unsigned char *)(proven_u8 *)dst.p, dst.n);
                stack[sp++] = vmv_int((proven_i64)wrote5);
                break;
            }
            case IRW_RANDBYTES: {   // cap random — (mut slice) → u64 채운 바이트 수
                if (sp < 1) return false;
                bool thr_r; vmv_t dst = vm_through(vm, stack[--sp], true, &thr_r);
                if (dst.tag != VMV_SLICE && dst.tag != VMV_VARRAY) {
                    vm_diag(vm->diags, "E-VM-TYPE", "random_bytes needs a mutable byte slice"); return false;
                }
                unsigned long hfk_ = 1;
                int hf_ = lw_hf_probe("random", &hfk_);
                proven_size_t n = (hf_ == 2) ? (proven_size_t)lw_hf_cap(dst.n, hfk_) : dst.n;
                if (hf_ == 1) n = 0;   /* ★ random 은 option 이 아니다 — 채운 바이트 수가 답이다 */
                else if (n && !proven_sys_random_bytes((void *)(proven_u8 *)dst.p, n)) n = 0;
                stack[sp++] = vmv_int((proven_i64)n);
                break;
            }
            case IRW_RNGNEXT: {   // (u64 상태) → u64 다음 상태
                if (sp < 1) return false;
                vmv_t sv = stack[--sp];
                if (sv.tag != VMV_INT) { vm_diag(vm->diags, "E-VM-TYPE", "rng_next needs a u64 state"); return false; }
                proven_u64 z = (proven_u64)sv.i + 0x9e3779b97f4a7c15ull;
                z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ull;
                z = (z ^ (z >> 27)) * 0x94d049bb133111ebull;
                z = z ^ (z >> 31);
                stack[sp++] = vmv_int((proven_i64)z);
                break;
            }
            case IRW_TTYSIZE: {   // () → option u64 (행<<32 | 열)
                struct winsize ws;
                if (!isatty(1) || ioctl(1, TIOCGWINSZ, &ws) != 0 || ws.ws_col == 0) {
                    stack[sp++] = (vmv_t){ .tag = VMV_NONE }; break;   // 모르면 **모른다고** 답한다
                }
                if (vm->nbox >= VM_MAXBOX) { vm_diag(vm->diags, "E-VM-BOXPOOL", "the VM's box pool is exhausted — 4096 slots for the values that `some`/`ok` wrap, and this pool is NEVER rewound (docs/runtime-pools.md). The emitted C shares the same number (LW_BOXPOOL), so a loop that wraps a value per iteration ends the same way there"); return false; }
                vm->boxes[vm->nbox] = vmv_int(((proven_i64)ws.ws_row << 32) | (proven_i64)ws.ws_col);
                stack[sp++] = (vmv_t){ .tag = VMV_SOME, .box = (proven_i32)vm->nbox++ };
                break;
            }
            case IRW_DREAD: {
                if (sp < 2) return false;
                vmv_t dv = stack[--sp], hv = stack[--sp];
                if (hv.tag != VMV_INT || (dv.tag != VMV_SLICE && dv.tag != VMV_VARRAY)) {
                    vm_diag(vm->diags, "E-VM-TYPE", "dir_read needs (handle, destination bytes)"); return false;
                }
                if (hv.i < 0 || hv.i >= VM_MAXFILE || !vm->dirs[hv.i]) {
                    vm_diag(vm->diags, "E-VM-FHANDLE", "not an open directory handle"); return false;
                }
                errno = 0;
                struct dirent *de = readdir(vm->dirs[hv.i]);
                if (!de) {
                    // ★ NULL 은 **끝**(errno==0)이거나 **오류**(errno!=0)다 — 둘을 가른다.
                    if (errno) { stack[sp++] = (vmv_t){ .tag = VMV_NONE }; break; }
                    if (vm->nbox >= VM_MAXBOX) { vm_diag(vm->diags, "E-VM-BOXPOOL", "the VM's box pool is exhausted — 4096 slots for the values that `some`/`ok` wrap, and this pool is NEVER rewound (docs/runtime-pools.md). The emitted C shares the same number (LW_BOXPOOL), so a loop that wraps a value per iteration ends the same way there"); return false; }
                    vm->boxes[vm->nbox] = vmv_int(0);   // ★ 0 = 끝(EOF 와 같은 규율 — 이름은 절대 0 길이가 아니다)
                    stack[sp++] = (vmv_t){ .tag = VMV_SOME, .box = (proven_i32)vm->nbox++ };
                    break;
                }
                size_t nl = strlen(de->d_name);
                if (nl > dv.n) { stack[sp++] = (vmv_t){ .tag = VMV_NONE }; break; }   // 이름이 버퍼에 안 맞으면 none
                memcpy((void *)(uintptr_t)dv.p, de->d_name, nl);
                if (vm->nbox >= VM_MAXBOX) { vm_diag(vm->diags, "E-VM-BOXPOOL", "the VM's box pool is exhausted — 4096 slots for the values that `some`/`ok` wrap, and this pool is NEVER rewound (docs/runtime-pools.md). The emitted C shares the same number (LW_BOXPOOL), so a loop that wraps a value per iteration ends the same way there"); return false; }
                vm->boxes[vm->nbox] = vmv_int((proven_i64)nl);
                stack[sp++] = (vmv_t){ .tag = VMV_SOME, .box = (proven_i32)vm->nbox++ };
                break;
            }
            case IRW_DCLOSE: {
                if (sp < 1) return false;
                vmv_t hv = stack[--sp];
                if (hv.tag != VMV_INT) { vm_diag(vm->diags, "E-VM-TYPE", "dir_close needs a handle"); return false; }
                if (hv.i < 0 || hv.i >= VM_MAXFILE || !vm->dirs[hv.i]) {
                    vm_diag(vm->diags, "E-VM-FHANDLE", "not an open directory handle"); return false;
                }
                int rc = closedir(vm->dirs[hv.i]);
                vm->dirs[hv.i] = NULL;
                stack[sp++] = vmv_int(rc == 0 ? 1 : 0);
                break;
            }
            case IRW_LTYPE:
            case IRW_FTYPE: {
                if (sp < 1) return false;
                vmv_t pv = stack[--sp];
                if (pv.tag != VMV_SLICE && pv.tag != VMV_VARRAY) {
                    vm_diag(vm->diags, "E-VM-TYPE", "file_type needs a path (byte slice)"); return false;
                }
                char pb[4096];
                if (pv.n == 0 || pv.n >= sizeof pb) { stack[sp++] = (vmv_t){ .tag = VMV_NONE }; break; }
                memcpy(pb, pv.p, pv.n); pb[pv.n] = '\0';
                struct stat stbuf;
                // ★★★ `link_type` 은 **따라가지 않는다**(lstat) — 링크 자신을 본다. `file_type` 은
                //   그대로 따라간다(stat). 낱말이 둘인 이유가 이 한 줄이다(X-0030 ⓐ · WO-0210).
                bool nofollow = (in->w == IRW_LTYPE);
                if ((nofollow ? lstat(pb, &stbuf) : stat(pb, &stbuf)) != 0) { stack[sp++] = (vmv_t){ .tag = VMV_NONE }; break; }   // 없으면 none
                // 0 = 보통 파일 · 1 = 디렉터리 · 2 = 그 밖 · 3 = 심링크(link_type 에서만 나온다)
                proven_i64 t = (nofollow && S_ISLNK(stbuf.st_mode)) ? 3
                             : S_ISDIR(stbuf.st_mode) ? 1 : S_ISREG(stbuf.st_mode) ? 0 : 2;
                if (vm->nbox >= VM_MAXBOX) { vm_diag(vm->diags, "E-VM-BOXPOOL", "the VM's box pool is exhausted — 4096 slots for the values that `some`/`ok` wrap, and this pool is NEVER rewound (docs/runtime-pools.md). The emitted C shares the same number (LW_BOXPOOL), so a loop that wraps a value per iteration ends the same way there"); return false; }
                vm->boxes[vm->nbox] = vmv_int(t);
                stack[sp++] = (vmv_t){ .tag = VMV_SOME, .box = (proven_i32)vm->nbox++ };
                break;
            }
            case IRW_DMAKE: case IRW_PREMOVE: {
                if (sp < 1) return false;
                vmv_t pv = stack[--sp];
                if (pv.tag != VMV_SLICE && pv.tag != VMV_VARRAY) {
                    vm_diag(vm->diags, "E-VM-TYPE", "this fs op needs a path (byte slice)"); return false;
                }
                char pb[4096];
                if (pv.n == 0 || pv.n >= sizeof pb) { stack[sp++] = vmv_int(0); break; }   // 못 하면 false
                memcpy(pb, pv.p, pv.n); pb[pv.n] = '\0';
                int rc = (in->w == IRW_DMAKE) ? mkdir(pb, 0777) : remove(pb);
                stack[sp++] = vmv_int(rc == 0 ? 1 : 0);   // ★ 이미 있거나 권한·공간 없으면 false(트랩 아님)
                break;
            }
            case IRW_PRENAME: {
                if (sp < 2) return false;
                vmv_t nv = stack[--sp], ov = stack[--sp];   // new, old
                if ((ov.tag != VMV_SLICE && ov.tag != VMV_VARRAY) ||
                    (nv.tag != VMV_SLICE && nv.tag != VMV_VARRAY)) {
                    vm_diag(vm->diags, "E-VM-TYPE", "path_rename needs (old path, new path)"); return false;
                }
                char ob[4096], nb[4096];
                if (ov.n == 0 || ov.n >= sizeof ob || nv.n == 0 || nv.n >= sizeof nb) { stack[sp++] = vmv_int(0); break; }
                memcpy(ob, ov.p, ov.n); ob[ov.n] = '\0';
                memcpy(nb, nv.p, nv.n); nb[nv.n] = '\0';
                stack[sp++] = vmv_int(rename(ob, nb) == 0 ? 1 : 0);
                break;
            }
            case IRW_ENVGET: {
                if (sp == 0) return false;
                vmv_t nv = stack[--sp];
                if (nv.tag != VMV_SLICE && nv.tag != VMV_VARRAY) {
                    vm_diag(vm->diags, "E-VM-TYPE", "env_get needs a name (byte slice)"); return false;
                }
                if (lw_hf_probe("env", NULL) == 1) { stack[sp++] = (vmv_t){ .tag = VMV_NONE }; break; }
                const char *hit = NULL; proven_size_t hitn = 0;
                for (char **e = environ; e && *e; e++) {
                    const char *eq = strchr(*e, '=');
                    if (!eq) continue;
                    if ((proven_size_t)(eq - *e) != nv.n) continue;
                    if (memcmp(*e, nv.p, nv.n) != 0) continue;
                    hit = eq + 1; hitn = strlen(eq + 1); break;
                }
                if (!hit) { stack[sp++] = (vmv_t){ .tag = VMV_NONE }; break; }
                if (vm->nbox >= VM_MAXBOX) { vm_diag(vm->diags, "E-VM-BOXPOOL", "the VM's box pool is exhausted — 4096 slots for the values that `some`/`ok` wrap, and this pool is NEVER rewound (docs/runtime-pools.md). The emitted C shares the same number (LW_BOXPOOL), so a loop that wraps a value per iteration ends the same way there"); return false; }
                vm->boxes[vm->nbox] = (vmv_t){ .tag = VMV_SLICE, .p = (const proven_u8 *)hit, .n = hitn };
                stack[sp++] = (vmv_t){ .tag = VMV_SOME, .box = (proven_i32)vm->nbox++ };
                break;
            }
            case IRW_ARGV: {
                if (sp == 0) return false;
                vmv_t iv = stack[--sp];
                if (iv.tag != VMV_INT) { vm_diag(vm->diags, "E-VM-TYPE", "arg needs an integer index"); return false; }
                if (iv.i < 0 || (proven_u64)iv.i >= (proven_u64)vm->npargs) {
                    stack[sp++] = (vmv_t){ .tag = VMV_NONE };   // 범위 밖 = 없음(값)
                    break;
                }
                if (vm->nbox >= VM_MAXBOX) { vm_diag(vm->diags, "E-VM-BOXPOOL", "the VM's box pool is exhausted — 4096 slots for the values that `some`/`ok` wrap, and this pool is NEVER rewound (docs/runtime-pools.md). The emitted C shares the same number (LW_BOXPOOL), so a loop that wraps a value per iteration ends the same way there"); return false; }
                vm->boxes[vm->nbox] = (vmv_t){ .tag = VMV_SLICE, .p = vm->pargs[iv.i].ptr, .n = vm->pargs[iv.i].size };
                stack[sp++] = (vmv_t){ .tag = VMV_SOME, .box = (proven_i32)vm->nbox++ };
                break;
            }
            // ★★★ **`value_or` — 부분 op 을 총체형으로** (RFC-0016 §200). `some_value`/`ok_value` 는
            //   태그가 안 맞으면 **panic** 한다(부분형). `value_or v <기본값>` 은 그 자리를 **총체**로
            //   만든다: some/ok 면 payload, none/err 면 기본값. 트랩 대신 **값**을 돌려주는 길.
            case IRW_HASVAL: {
                // ★ *"값이 들어 있나"* — some/ok 면 1, none/err 면 0. `value_or` 의 지연 분기가 쓴다.
                //   `is_some` 은 result 를, `is_ok` 는 option 을 거절하므로 **둘 다 받는 것**이 필요했다.
                if (sp < 1) return false;
                vmv_t v = stack[sp - 1];
                if (v.tag == VMV_SOME || v.tag == VMV_OK) stack[sp - 1] = vmv_int(1);
                else if (v.tag == VMV_NONE || v.tag == VMV_ERR) stack[sp - 1] = vmv_int(0);
                else { vm_diag(vm->diags, "E-VM-TYPE", "value_or needs an option or a result"); return false; }
                break;
            }
            case IRW_VALOR: {
                if (sp < 2) return false;
                vmv_t dflt = stack[--sp], v = stack[--sp];
                if (v.tag == VMV_SOME || v.tag == VMV_OK) stack[sp++] = vm->boxes[v.box];
                else if (v.tag == VMV_NONE || v.tag == VMV_ERR) stack[sp++] = dflt;
                else { vm_diag(vm->diags, "E-VM-TYPE", "value_or needs an option or a result"); return false; }
                break;
            }
            // ★★★ **`pop` — 총체형 스택 꺼내기** (RFC-0016 §160). 비었으면 **none**(panic 아님).
            //   기존 IRW_SPOP_INTO(`pop s into v`)는 **부분형**(bool 을 주고 슬롯에 쓴다) — 이쪽은
            //   option 을 **값으로** 돌려주므로 `value_or` 와 바로 이어 붙는다.
            case IRW_SPOP: {
                if (sp == 0) return false;
                vmv_t s = stack[--sp];
                if (s.tag != VMV_STACK) { vm_diag(vm->diags, "E-VM-TYPE", "pop needs a stack"); return false; }
                vmstk_t *st = &vm->stks[s.box];
                if (st->n == 0) { stack[sp++] = (vmv_t){ .tag = VMV_NONE }; break; }
                if (vm->nbox >= VM_MAXBOX) { vm_diag(vm->diags, "E-VM-BOXPOOL", "the VM's box pool is exhausted — 4096 slots for the values that `some`/`ok` wrap, and this pool is NEVER rewound (docs/runtime-pools.md). \u2605 The emitted C does NOT share this fate for a plain `option u64`: its fast path carries option/result as a (tag, value) PAIR with no pool at all, so the same loop keeps running natively — this is a DIVERGENCE between the backends, not your program being too big"); return false; }
                vm->boxes[vm->nbox] = st->items[--st->n];
                stack[sp++] = (vmv_t){ .tag = VMV_SOME, .box = (proven_i32)vm->nbox++ };
                break;
            }
            case IRW_WRAP_ERR:
                if (sp >= VM_STACK) return false;
                stack[sp++] = (vmv_t){ .tag = VMV_ERR, .i = in->a };
                break;
            case IRW_TRY: {
                if (sp == 0) return false;
                vmv_t v = stack[--sp];
                if (v.tag == VMV_ERR) { *ret = v; return true; }   // propagate
                if (v.tag == VMV_OK) stack[sp++] = vm->boxes[v.box];
                else stack[sp++] = v;
                break;
            }
            case IRW_SNEW: {   // pop capacity (advisory in the VM), allot a stack
                if (sp == 0) return false;
                if (vm->nstk >= VM_MAXSTK) {
                    // ★ 레코드·비트셋과 **같은 말**: 이건 **VM 의 한계**이지 프로그램의 결함이 아니다.
                    vm_diag(vm->diags, "E-VM-STKPOOL",
                            "the VM's stack-container pool is exhausted. A stack is an ARRAY and a "
                            "LENGTH, but this interpreter boxes it in a fixed pool. This is a limit "
                            "OF THE VM, not a defect in your program — the native build lowers a "
                            "stack to a plain array in the frame");
                    return false;
                }
                // ★★★ **선언 capacity 를 실제 한도로 쓴다**(2026-07-23, F2/F3). 전엔 버리고(sp--)
                //   push 가 하드코딩 128 을 봐, `capacity 2` 에 4 개를 밀어도 통과했고(both-wrong),
                //   `capacity 129` 는 VM 통과·native panic(VM≠native)였다. 이제 capacity 를 담고
                //   push 가 그 한도를 본다. 물리 저장은 VM_STKCAP(128)까지 — 그보다 큰 capacity 는
                //   native(K_STK_CAP)와 같이 거절한다("없는 것을 있는 척하지 않는다").
                { vmv_t cv = stack[--sp];
                  if (cv.tag != VMV_INT || cv.i < 0) { vm_diag(vm->diags, "E-VM-TYPE", "stack capacity must be a non-negative int"); return false; }
                  if (cv.i > VM_STKCAP) { vm_diag(vm->diags, "E-VM-BOUNDS", "stack capacity exceeded"); return false; }
                  vm->stks[vm->nstk].n = 0; vm->stks[vm->nstk].cap = (proven_size_t)cv.i; }
                stack[sp++] = (vmv_t){ .tag = VMV_STACK, .box = (proven_i32)vm->nstk++ };
                break;
            }
            case IRW_SPUSH: {
                if (sp < 2) return false;
                vmv_t v = stack[--sp], s = stack[--sp];
                if (s.tag != VMV_STACK) { vm_diag(vm->diags, "E-VM-TYPE", "push needs a stack"); return false; }
                vmstk_t *st = &vm->stks[s.box];
                if (st->n >= st->cap) { vm_diag(vm->diags, "E-VM-BOUNDS", "stack capacity exceeded"); return false; }
                st->items[st->n++] = v;
                stack[sp++] = vmv_int(0);   // unit
                break;
            }
            case IRW_SPOP_INTO: {
                if (sp == 0) return false;
                vmv_t s = stack[--sp];
                if (s.tag != VMV_STACK) { vm_diag(vm->diags, "E-VM-TYPE", "pop needs a stack"); return false; }
                vmstk_t *st = &vm->stks[s.box];
                if (st->n == 0) { stack[sp++] = vmv_int(0); break; }
                locals[in->a] = st->items[--st->n];
                stack[sp++] = vmv_int(1);
                break;
            }
            case IRW_BNEW: {
                if (sp == 0) return false;
                if (vm->nbset >= VM_MAXBSET) {
                    // ★★★ 레코드와 **같은 말**을 한다: 이것은 **VM 의 한계**이지
                    //   프로그램의 결함이 아니다. 네이티브는 비트셋을 **64비트 마스크**로
                    //   내리고 **박스를 아예 안 쓴다**(DECISION-0018).
                    vm_diag(vm->diags, "E-VM-BSETPOOL",
                            "the VM's bitset pool is exhausted. A bitset is a 64-bit MASK, but this "
                            "interpreter boxes it in a fixed pool and a loop that makes bitsets runs "
                            "it dry. This is a limit OF THE VM, not a defect in your program — the "
                            "native build lowers a bitset to a plain machine word");
                    return false;
                }
                vmv_t nv = stack[--sp];
                if (nv.tag != VMV_INT || nv.i > 64) { vm_diag(vm->diags, "E-VM-BOUNDS", "bitset width > 64 (S5 core)"); return false; }
                vm->bsets[vm->nbset] = 0;
                vm->bwid[vm->nbset] = (proven_u8)(nv.i <= 0 ? 64 : nv.i);   // ★ 폭을 기억한다 — complement 가 필요로 한다
                stack[sp++] = (vmv_t){ .tag = VMV_BITSET, .box = (proven_i32)vm->nbset++ };
                break;
            }
            case IRW_CONTAINS: {
                if (sp < 2) return false;
                vmv_t iv = stack[--sp], bv = stack[--sp];
                if (bv.tag != VMV_BITSET || iv.tag != VMV_INT) { vm_diag(vm->diags, "E-VM-TYPE", "contains needs bitset + small int"); return false; }
                { proven_i64 bw = (in->a & 0x800000) ? (in->a & 0x7f) : 64;   // ★ 선언 폭까지
                  if (iv.i < 0 || iv.i >= bw) { vm_diag(vm->diags, "E-VM-BOUNDS", "bitset element out of range"); return false; } }
                stack[sp++] = vmv_int((vm->bsets[bv.box] >> iv.i) & 1u);
                break;
            }
            case IRW_COUNT: {
                if (sp == 0) return false;
                vmv_t v = stack[--sp];
                if (v.tag == VMV_STACK) { stack[sp++] = vmv_int((proven_i64)vm->stks[v.box].n); break; }
                if (v.tag != VMV_BITSET) { vm_diag(vm->diags, "E-VM-TYPE", "count needs a stack/bitset"); return false; }
                proven_u64 bits = vm->bsets[v.box]; proven_i64 k = 0;
                while (bits) { bits &= bits - 1; k++; }
                stack[sp++] = vmv_int(k);
                break;
            }
            // ★★★ **집합 연산** (RFC-0010 §6.7.1) — 전부 **분기 없는 워드 연산**이다(S2).
            case IRW_BREMOVE: {
                if (sp < 2) return false;
                vmv_t iv = stack[--sp], bv = stack[--sp];
                if (bv.tag != VMV_BITSET || iv.tag != VMV_INT) { vm_diag(vm->diags, "E-VM-TYPE", "remove needs a set + a small int"); return false; }
                { proven_i64 bw = (in->a & 0x800000) ? (in->a & 0x7f) : 64;   // ★ 선언 폭까지
                  if (iv.i < 0 || iv.i >= bw) { vm_diag(vm->diags, "E-VM-BOUNDS", "bitset element out of range"); return false; } }
                vm->bsets[bv.box] &= ~((proven_u64)1 << iv.i);
                stack[sp++] = vmv_int(0);   // unit
                break;
            }
            case IRW_BUNION: case IRW_BINTER: case IRW_BDIFF: {
                if (sp < 2) return false;
                vmv_t b2 = stack[--sp], a2 = stack[--sp];
                if (a2.tag != VMV_BITSET || b2.tag != VMV_BITSET) { vm_diag(vm->diags, "E-VM-TYPE", "union/intersect/difference need two sets"); return false; }
                if (vm->nbset >= VM_MAXBSET) { vm_diag(vm->diags, "E-VM-BSETPOOL", "the VM's bitset pool is exhausted"); return false; }
                proven_u64 x = vm->bsets[a2.box], y = vm->bsets[b2.box];
                vm->bsets[vm->nbset] = in->w == IRW_BUNION ? (x | y) : in->w == IRW_BINTER ? (x & y) : (x & ~y);
                vm->bwid[vm->nbset] = vm->bwid[a2.box];
                stack[sp++] = (vmv_t){ .tag = VMV_BITSET, .box = (proven_i32)vm->nbset++ };
                break;
            }
            case IRW_BCOMPL: {
                if (sp == 0) return false;
                vmv_t v2 = stack[--sp];
                if (v2.tag != VMV_BITSET) { vm_diag(vm->diags, "E-VM-TYPE", "complement needs a set"); return false; }
                if (vm->nbset >= VM_MAXBSET) { vm_diag(vm->diags, "E-VM-BSETPOOL", "the VM's bitset pool is exhausted"); return false; }
                proven_u8 w = in->a ? (proven_u8)in->a : (vm->bwid[v2.box] ? vm->bwid[v2.box] : 64);
                // ★ **폭으로 마스킹한다** — 그러지 않으면 `bitset 8` 의 여집합이 상위 56 비트를 켜서
                //   count 가 거짓말을 한다. 폭을 안 들고 있었으면 이 연산은 **옳을 수가 없었다**.
                proven_u64 m = w >= 64 ? ~(proven_u64)0 : (((proven_u64)1 << w) - 1);
                vm->bsets[vm->nbset] = (~vm->bsets[v2.box]) & m;
                vm->bwid[vm->nbset] = w;
                stack[sp++] = (vmv_t){ .tag = VMV_BITSET, .box = (proven_i32)vm->nbset++ };
                break;
            }
            case IRW_BEMPTY: {
                if (sp == 0) return false;
                vmv_t v2 = stack[--sp];
                if (v2.tag != VMV_BITSET) { vm_diag(vm->diags, "E-VM-TYPE", "is_empty needs a set"); return false; }
                stack[sp++] = vmv_int(vm->bsets[v2.box] == 0);
                break;
            }
            case IRW_BSUBSET: {
                if (sp < 2) return false;
                vmv_t b2 = stack[--sp], a2 = stack[--sp];
                if (a2.tag != VMV_BITSET || b2.tag != VMV_BITSET) { vm_diag(vm->diags, "E-VM-TYPE", "is_subset needs two sets"); return false; }
                stack[sp++] = vmv_int((vm->bsets[a2.box] & ~vm->bsets[b2.box]) == 0);
                break;
            }
            case IRW_REF: case IRW_MREF: {
                if (sp >= VM_STACK) { vm_diag(vm->diags, "E-VM-STACK", "operand stack overflow"); return false; }
                bool bmut = (in->w == IRW_MREF);
                proven_u32 tag = ++vm->btag & 0x7fffffffu;
                vmbstk_t *b = bstk_find(&vm->frames[slot], (proven_u8)in->a, true);
                if (!b || b->n >= VM_BSTK_DEPTH) { vm_diag(vm->diags, "E-VM-EXCL", "borrow stack overflow"); return false; }
                b->tags[b->n++] = tag | (bmut ? VM_BTAG_MUT : 0);
                stack[sp++] = (vmv_t){ .tag = VMV_REF,
                                       .i = in->a | ((proven_i64)tag << 16) | (bmut ? VM_REF_MUT : 0),
                                       .n = (proven_size_t)vm->frames[slot].gen,
                                       .box = (proven_i32)slot };
                break;
            }
            case IRW_DEREF: {
                if (sp == 0) return false;
                vmv_t r = stack[--sp];
                if (r.tag != VMV_REF) { vm_diag(vm->diags, "E-VM-TYPE", "deref needs a reference"); return false; }
                vmframe_t *fr = &vm->frames[r.box];
                if (!fr->locs || fr->gen != (proven_u64)r.n) { vm_diag(vm->diags, "E-VM-DANGLING", "reference outlived its frame (use-after-return)"); return false; }
                if (!bstk_use(fr, (proven_u8)(r.i & VM_REF_SLOT), VM_REF_TAG(r.i), false)) {
                    vm_diag(vm->diags, "E-VM-EXCL", "borrow invalidated by a conflicting access (readers-XOR-writer)");
                    return false;
                }
                stack[sp++] = fr->locs[r.i & VM_REF_SLOT];
                break;
            }
            case IRW_FSTORE: {   // ★ 구조체 필드 쓰기: record, v → ()
                if (sp < 2) return false;
                vmv_t vv = stack[--sp];
                bool thr_; vmv_t rv = vm_through(vm, stack[--sp], true, &thr_);
                if (!thr_) return false;          // ★ 쓰기이므로 write=true — 배타성이 여기서 산다
                // ★★★ **뷰의 필드에도 쓴다** — 그것이 곧 **디바이스 레지스터 쓰기**다(RFC-0042 D1).
                //   전엔 레코드에만 썼다. 그러면 MMIO 는 **읽기만** 가능했고, `write_volatile` 이
                //   **표현 불가능**했다. (뷰 읽기는 이미 있었다 — 쓰기만 없었다. `some`/`none` 과
                //   **똑같은 모양**의 구멍이다: 소비만 있고 생산이 없다.)
                if (rv.tag == VMV_VIEW) {
                    const low_ir_struct_t *s2 = &vm->ir->structs[rv.box];
                    proven_u8str_view_t w2 = vm->ir->fields[in->a];
                    bool f2 = false;
                    for (proven_size_t q = 0; q < s2->nf && !f2; q++) {
                        if (!proven_u8str_view_eq(s2->f[q].name, w2)) continue;
                        proven_u64 x = (proven_u64)vv.i;
                        proven_u8 *p2 = (proven_u8 *)(void *)(rv.p + s2->f[q].off);
                        if (s2->f[q].be)
                            for (proven_u8 kk = 0; kk < s2->f[q].size; kk++)
                                p2[kk] = (proven_u8)(x >> (8 * (s2->f[q].size - 1 - kk)));
                        else
                            for (proven_u8 kk = 0; kk < s2->f[q].size; kk++)
                                p2[kk] = (proven_u8)(x >> (8 * kk));
                        f2 = true;
                    }
                    if (!f2) { vm_diag(vm->diags, "E-VM-FIELD", "no such register in this block"); return false; }
                    break;
                }
                if (rv.tag != VMV_REC) {
                    vm_diag(vm->diags, "E-VM-TYPE", "set field needs a record"); return false;
                }
                proven_u8str_view_t want = vm->ir->fields[in->a];
                vmrec_t *r = &vm->recs[rv.box];
                const low_ir_make_t *mk = &vm->ir->makes[r->make_idx];
                bool found = false;
                for (proven_size_t q = 0; q < r->nfields && !found; q++)
                    if (proven_u8str_view_eq(mk->fields[q], want)) { r->fields[q] = vv; found = true; }
                if (!found) { vm_diag(vm->diags, "E-VM-FIELD", "no such field on this record"); return false; }
                break;
            }
            case IRW_FIELD: {
                if (sp == 0) return false;
                bool thr_; vmv_t v = vm_through(vm, stack[--sp], false, &thr_);
                if (!thr_) return false;          // ★ 참조면 따라간다(세대·빌림 검사 포함)
                proven_u8str_view_t want = vm->ir->fields[in->a];
                bool found = false;
                if (v.tag == VMV_REC) {
                    const vmrec_t *r = &vm->recs[v.box];
                    const low_ir_make_t *mk = &vm->ir->makes[r->make_idx];
                    for (proven_size_t i = 0; i < r->nfields && !found; i++)
                        if (proven_u8str_view_eq(mk->fields[i], want)) { stack[sp++] = r->fields[i]; found = true; }
                } else if (v.tag == VMV_VIEW) {
                    // zero-copy field read: bytes at the layout offset, endian-decoded
                    const low_ir_struct_t *s = &vm->ir->structs[v.box];
                    for (proven_size_t i = 0; i < s->nf && !found; i++)
                        if (proven_u8str_view_eq(s->f[i].name, want)) {
                            // ★ **중첩 구조체 필드** — 값이 아니라 **안쪽 뷰**를 준다(무복사).
                            //   그래야 `field (field v i) a` 가 이어진다.
                            if (s->f[i].sidx >= 0) {
                                stack[sp++] = (vmv_t){ .tag = VMV_VIEW, .p = v.p + s->f[i].off,
                                                       .n = s->f[i].size,
                                                       .box = (proven_i32)s->f[i].sidx };
                                found = true;
                                break;
                            }
                            proven_u64 x = 0;
                            const proven_u8 *p = v.p + s->f[i].off;
                            if (s->f[i].be) for (proven_u8 k = 0; k < s->f[i].size; k++) x = (x << 8) | p[k];
                            else            for (proven_u8 k = s->f[i].size; k-- > 0; ) x = (x << 8) | p[k];
                            stack[sp++] = s->f[i].flt ? vmv_flt(ir_bits_to_f(x, s->f[i].size))
                                                      : vmv_int((proven_i64)x);
                            found = true;
                        }
                } else {
                    vm_diag(vm->diags, "E-VM-TYPE", "field access needs a record/view");
                    return false;
                }
                if (!found) { vm_diag(vm->diags, "E-VM-FIELD", "no such field on this record"); return false; }
                break;
            }
            case IRW_VIEW: case IRW_TRYVIEW: {
                if (sp == 0) return false;
                vmv_t b = stack[--sp];
                const low_ir_struct_t *s = &vm->ir->structs[in->a];
                if (b.tag != VMV_SLICE) { vm_diag(vm->diags, "E-VM-TYPE", "view needs a byte slice"); return false; }
                // `align n` is a contract, so the view boundary is where it is checked
                // (RFC-0051 §5.1): a mis-aligned base fails the view, exactly like a short one.
                bool aligned = s->align <= 1 || ((uintptr_t)(const void *)b.p % s->align) == 0;
                bool fits = b.n >= s->total && aligned;
                if (in->w == IRW_VIEW) {
                    if (!aligned) { vm_diag(vm->diags, "E-VM-ALIGN", "view: base address violates the struct's align contract (panic)"); return false; }
                    if (!fits) { vm_diag(vm->diags, "E-VM-VIEW", "view: slice shorter than the layout (panic)"); return false; }
                    stack[sp++] = (vmv_t){ .tag = VMV_VIEW, .p = b.p, .n = b.n, .box = (proven_i32)in->a };
                } else if (!fits) {
                    stack[sp++] = (vmv_t){ .tag = VMV_NONE };
                } else {
                    if (vm->nbox >= VM_MAXBOX) { vm_diag(vm->diags, "E-VM-BOXPOOL",
                        "the VM's box pool is exhausted. `some(v)` / `ok(v)` wrap a value, and this "
                        "interpreter puts each one in a fixed pool. This is a limit OF THE VM, not "
                        "a defect in your program — the native build lowers option/result to a "
                        "(tag, value) PAIR in registers and boxes nothing"); return false; }
                    vm->boxes[vm->nbox] = (vmv_t){ .tag = VMV_VIEW, .p = b.p, .n = b.n, .box = (proven_i32)in->a };
                    stack[sp++] = (vmv_t){ .tag = VMV_SOME, .box = (proven_i32)vm->nbox++ };
                }
                break;
            }
            case IRW_ENCODE: {   // record/view → wire bytes per the layout (endian applied)
                if (sp == 0) return false;
                vmv_t v = stack[--sp];
                const low_ir_struct_t *s = &vm->ir->structs[in->a];
                if (vm->nenc >= VM_MAXENC || s->total > VM_ENCCAP) { vm_diag(vm->diags, "E-VM-VIEW", "encode buffer pool exhausted"); return false; }
                proven_u8 *outb = vm->encs[vm->nenc];
                memset(outb, 0, s->total);
                for (proven_size_t i = 0; i < s->nf; i++) {
                    proven_u64 x = 0;
                    if (v.tag == VMV_REC) {
                        const vmrec_t *r = &vm->recs[v.box];
                        const low_ir_make_t *mk = &vm->ir->makes[r->make_idx];
                        bool found = false;
                        for (proven_size_t j = 0; j < r->nfields && !found; j++)
                            if (proven_u8str_view_eq(mk->fields[j], s->f[i].name)) {
                                vmv_t fv = r->fields[j];
                                if (s->f[i].flt) {
                                    if (fv.tag == VMV_FLT) x = ir_f_to_bits(vmv_f(fv), s->f[i].size);
                                    else if (fv.tag == VMV_INT) x = ir_f_to_bits((double)fv.i, s->f[i].size);
                                    else { vm_diag(vm->diags, "E-VM-TYPE", "encode: float field needs a number"); return false; }
                                } else if (fv.tag == VMV_INT) {
                                    x = (proven_u64)fv.i;
                                } else {
                                    vm_diag(vm->diags, "E-VM-TYPE", "encode needs integer fields"); return false;
                                }
                                found = true;
                            }
                        if (!found) { vm_diag(vm->diags, "E-VM-FIELD", "encode: record lacks a layout field"); return false; }
                    } else if (v.tag == VMV_VIEW) {
                        const low_ir_struct_t *vs = &vm->ir->structs[v.box];
                        bool found = false;
                        for (proven_size_t j = 0; j < vs->nf && !found; j++)
                            if (proven_u8str_view_eq(vs->f[j].name, s->f[i].name)) {
                                const proven_u8 *p = v.p + vs->f[j].off;
                                if (vs->f[j].be) for (proven_u8 kk = 0; kk < vs->f[j].size; kk++) x = (x << 8) | p[kk];
                                else             for (proven_u8 kk = vs->f[j].size; kk-- > 0; ) x = (x << 8) | p[kk];
                                // re-normalize across differing float widths (f32 ⇄ f64)
                                if (s->f[i].flt && vs->f[j].flt && s->f[i].size != vs->f[j].size)
                                    x = ir_f_to_bits(ir_bits_to_f(x, vs->f[j].size), s->f[i].size);
                                found = true;
                            }
                        if (!found) { vm_diag(vm->diags, "E-VM-FIELD", "encode: view lacks a layout field"); return false; }
                    } else {
                        vm_diag(vm->diags, "E-VM-TYPE", "encode needs a record/view");
                        return false;
                    }
                    proven_u8 *p = outb + s->f[i].off;
                    if (s->f[i].be) for (proven_u8 kk = s->f[i].size; kk-- > 0; ) { p[kk] = (proven_u8)x; x >>= 8; }
                    else            for (proven_u8 kk = 0; kk < s->f[i].size; kk++) { p[kk] = (proven_u8)x; x >>= 8; }
                }
                stack[sp++] = (vmv_t){ .tag = VMV_SLICE, .p = outb, .n = s->total };
                vm->nenc++;
                break;
            }
            case IRW_SPLAT: case IRW_VLOAD: {
                proven_u8 lanes = (proven_u8)((in->a >> 8) & 0xff), esz = (proven_u8)(in->a & 0xff);
                bool lflt = (in->a & IR_FLT_BIT) != 0;
                bool lsgn = (in->a & IR_SGN_BIT) != 0;   // ★ 부호형 레인(vec iN)
                if (vm->nvec >= 32) { vm_diag(vm->diags, "E-VM-VIEW", "vector pool exhausted — the VM keeps vectors in a 32-slot pool. It REWINDS at loop boundaries and at return (docs/runtime-pools.md), so a loop no longer drains it; what is left is a real limit: this many vectors are LIVE AT ONCE (nested calls, or one frame naming more than 32 of them). The native fast path has NO such pool — it keeps a vector in the frame as a VALUE — so at THIS line the two backends still DIVERGE, and that is now the only divergence left here"); return false; }
                proven_u64 mask = esz >= 8 ? ~0ull : ((1ull << (8 * esz)) - 1);
                vm->vecs[vm->nvec].flt = lflt;
                vm->vecs[vm->nvec].sign = lsgn;
                if (in->w == IRW_SPLAT) {
                    if (sp == 0) return false;
                    vmv_t x = stack[--sp];
                    if (lflt) {
                        if (x.tag != VMV_FLT && x.tag != VMV_INT) { vm_diag(vm->diags, "E-VM-TYPE", "splat needs a number"); return false; }
                        double xd = x.tag == VMV_FLT ? vmv_f(x) : (double)x.i;
                        for (proven_u8 k = 0; k < lanes; k++) vm->vecs[vm->nvec].l[k] = ir_f_to_bits(xd, esz);
                    } else {
                        if (x.tag != VMV_INT) { vm_diag(vm->diags, "E-VM-TYPE", "splat needs an int"); return false; }
                        for (proven_u8 k = 0; k < lanes; k++) vm->vecs[vm->nvec].l[k] = (proven_u64)x.i & mask;
                    }
                } else {
                    if (sp < 2) return false;
                    vmv_t iv = stack[--sp], src = stack[--sp];
                    if (iv.tag != VMV_INT) { vm_diag(vm->diags, "E-VM-TYPE", "load index must be an int"); return false; }
                    proven_size_t i0 = (proven_size_t)iv.i;
                    if (src.tag == VMV_VARRAY && (proven_u8)src.box == esz) {
                        if (iv.i < 0 || i0 + lanes > src.n) { vm_diag(vm->diags, "E-VM-BOUNDS", "vector load out of bounds (panic)"); return false; }
                        for (proven_u8 k = 0; k < lanes; k++) {
                            proven_u64 x = 0;
                            const proven_u8 *p = src.p + (i0 + k) * (proven_size_t)esz;
                            for (proven_i32 b = esz; b-- > 0; ) x = (x << 8) | p[b];
                            vm->vecs[vm->nvec].l[k] = x;
                        }
                    } else if (src.tag == VMV_SLICE) {
                        if (iv.i < 0 || (i0 + lanes) * (proven_size_t)esz > src.n) { vm_diag(vm->diags, "E-VM-BOUNDS", "vector load out of bounds (panic)"); return false; }
                        for (proven_u8 k = 0; k < lanes; k++) {
                            proven_u64 x = 0;
                            const proven_u8 *p = src.p + (i0 + k) * (proven_size_t)esz;
                            for (proven_i32 b = esz; b-- > 0; ) x = (x << 8) | p[b];
                            vm->vecs[vm->nvec].l[k] = x;
                        }
                    } else {
                        vm_diag(vm->diags, "E-VM-TYPE", "load needs a matching typed array or byte slice");
                        return false;
                    }
                }
                vm->vecs[vm->nvec].n = lanes;
                vm->vecs[vm->nvec].esz = esz;
                stack[sp++] = (vmv_t){ .tag = VMV_VEC, .box = (proven_i32)vm->nvec++ };
                break;
            }
            case IRW_VSTORE: {   // dst, idx, vec → () ; 레인/esz 는 vec 이 실어 온다 (VLOAD 의 역)
                if (sp < 3) return false;
                vmv_t vv = stack[--sp], iv = stack[--sp], dst = stack[--sp];
                if (vv.tag != VMV_VEC || iv.tag != VMV_INT) { vm_diag(vm->diags, "E-VM-TYPE", "store needs a slice + an int + a vector"); return false; }
                proven_u8 lanes = vm->vecs[vv.box].n, esz = vm->vecs[vv.box].esz;
                proven_size_t i0 = (proven_size_t)iv.i;
                proven_u8 *base; proven_size_t limit;
                if (dst.tag == VMV_VARRAY && (proven_u8)dst.box == esz) { base = (proven_u8 *)(void *)(uintptr_t)dst.p; limit = dst.n; }
                else if (dst.tag == VMV_SLICE) { base = (proven_u8 *)(void *)(uintptr_t)dst.p; limit = dst.n / (proven_size_t)esz; }
                else { vm_diag(vm->diags, "E-VM-TYPE", "store needs a matching typed array or byte slice"); return false; }
                if (iv.i < 0 || i0 + lanes > limit) { vm_diag(vm->diags, "E-VM-BOUNDS", "vector store out of bounds (panic)"); return false; }
                for (proven_u8 k = 0; k < lanes; k++) {
                    proven_u64 x = vm->vecs[vv.box].l[k];
                    proven_u8 *p = base + (i0 + k) * (proven_size_t)esz;
                    for (proven_u8 b = 0; b < esz; b++) p[b] = (proven_u8)((x >> (8 * b)) & 0xff);   // little-endian, VLOAD 읽기의 역
                }
                stack[sp++] = vmv_int(0);   // unit
                break;
            }
            case IRW_VLOADM: {   // src, idx, mask, passthrough → vec ; 켜진 lane=메모리, 꺼진 lane=passthrough (merge)
                proven_u8 lanes = (proven_u8)((in->a >> 8) & 0xff), esz = (proven_u8)(in->a & 0xff);
                bool lflt = (in->a & IR_FLT_BIT) != 0;
                if (vm->nvec >= 32) { vm_diag(vm->diags, "E-VM-VIEW", "vector pool exhausted — the VM keeps vectors in a 32-slot pool. It REWINDS at loop boundaries and at return (docs/runtime-pools.md), so a loop no longer drains it; what is left is a real limit: this many vectors are LIVE AT ONCE (nested calls, or one frame naming more than 32 of them). The native fast path has NO such pool — it keeps a vector in the frame as a VALUE — so at THIS line the two backends still DIVERGE, and that is now the only divergence left here"); return false; }
                if (sp < 4) return false;
                vmv_t pv = stack[--sp], mv = stack[--sp], iv = stack[--sp], src = stack[--sp];
                if (mv.tag != VMV_MASK || iv.tag != VMV_INT || pv.tag != VMV_VEC) { vm_diag(vm->diags, "E-VM-TYPE", "load_masked needs a slice + an int + a mask + a passthrough vector"); return false; }
                if ((proven_u8)mv.box != lanes || vm->vecs[pv.box].n != lanes) { vm_diag(vm->diags, "E-VM-TYPE", "load_masked mask/passthrough lane count != vec lanes"); return false; }
                proven_size_t i0 = (proven_size_t)iv.i;
                const proven_u8 *base; proven_size_t limit;
                if (src.tag == VMV_VARRAY && (proven_u8)src.box == esz) { base = src.p; limit = src.n; }
                else if (src.tag == VMV_SLICE) { base = src.p; limit = src.n / (proven_size_t)esz; }
                else { vm_diag(vm->diags, "E-VM-TYPE", "load_masked needs a matching typed array or byte slice"); return false; }
                vm->vecs[vm->nvec].flt = lflt;
                vm->vecs[vm->nvec].sign = (in->a & IR_SGN_BIT) != 0;
                for (proven_u8 k = 0; k < lanes; k++) {
                    if (!((mv.i >> k) & 1)) { vm->vecs[vm->nvec].l[k] = vm->vecs[pv.box].l[k]; continue; }   // 꺼진 lane: 안 읽음, passthrough
                    if (iv.i < 0 || i0 + k >= limit) { vm_diag(vm->diags, "E-VM-BOUNDS", "load_masked active lane out of bounds (panic)"); return false; }
                    proven_u64 x = 0;
                    const proven_u8 *p = base + (i0 + k) * (proven_size_t)esz;
                    for (proven_i32 b = esz; b-- > 0; ) x = (x << 8) | p[b];
                    vm->vecs[vm->nvec].l[k] = x;
                }
                vm->vecs[vm->nvec].n = lanes;
                vm->vecs[vm->nvec].esz = esz;
                stack[sp++] = (vmv_t){ .tag = VMV_VEC, .box = (proven_i32)vm->nvec++ };
                break;
            }
            case IRW_VSTOREM: {   // dst, idx, vec, mask → () ; 켜진 lane 만 쓰고 나머지 보존
                if (sp < 4) return false;
                vmv_t mv = stack[--sp], vv = stack[--sp], iv = stack[--sp], dst = stack[--sp];
                if (vv.tag != VMV_VEC || iv.tag != VMV_INT || mv.tag != VMV_MASK) { vm_diag(vm->diags, "E-VM-TYPE", "store_masked needs a slice + an int + a vector + a mask"); return false; }
                proven_u8 lanes = vm->vecs[vv.box].n, esz = vm->vecs[vv.box].esz;
                if ((proven_u8)mv.box != lanes) { vm_diag(vm->diags, "E-VM-TYPE", "store_masked mask lane count != vec lanes"); return false; }
                proven_size_t i0 = (proven_size_t)iv.i;
                proven_u8 *base; proven_size_t limit;
                if (dst.tag == VMV_VARRAY && (proven_u8)dst.box == esz) { base = (proven_u8 *)(void *)(uintptr_t)dst.p; limit = dst.n; }
                else if (dst.tag == VMV_SLICE) { base = (proven_u8 *)(void *)(uintptr_t)dst.p; limit = dst.n / (proven_size_t)esz; }
                else { vm_diag(vm->diags, "E-VM-TYPE", "store_masked needs a matching typed array or byte slice"); return false; }
                for (proven_u8 k = 0; k < lanes; k++) {
                    if (!((mv.i >> k) & 1)) continue;   // 꺼진 lane: 안 씀(보존)
                    if (iv.i < 0 || i0 + k >= limit) { vm_diag(vm->diags, "E-VM-BOUNDS", "store_masked active lane out of bounds (panic)"); return false; }
                    proven_u64 x = vm->vecs[vv.box].l[k];
                    proven_u8 *p = base + (i0 + k) * (proven_size_t)esz;
                    for (proven_u8 b = 0; b < esz; b++) p[b] = (proven_u8)((x >> (8 * b)) & 0xff);
                }
                stack[sp++] = vmv_int(0);   // unit
                break;
            }
            case IRW_VAVG: {   // ★ D5 target intrinsic — 레인별 라운딩 평균 (a+b+1)>>1 (pavgb/vrhadd)
                if (sp < 2 || vm->nvec >= 32) return false;
                vmv_t bv = stack[--sp], av = stack[--sp];
                if (av.tag != VMV_VEC || bv.tag != VMV_VEC) { vm_diag(vm->diags, "E-VM-TYPE", "avg needs two vectors"); return false; }
                proven_u8 n = vm->vecs[av.box].n, esz = vm->vecs[av.box].esz;
                if (vm->vecs[bv.box].n != n || vm->vecs[bv.box].esz != esz) { vm_diag(vm->diags, "E-VM-TYPE", "avg lane counts/widths differ"); return false; }
                if (vm->vecs[av.box].flt || vm->vecs[bv.box].flt) { vm_diag(vm->diags, "E-VM-TYPE", "avg is an INTEGER intrinsic (rounding average of unsigned lanes)"); return false; }
                proven_u64 emask = esz >= 8 ? ~0ull : ((1ull << (8 * esz)) - 1);
                for (proven_u8 k = 0; k < n; k++) {
                    // ★ **widening** — u8 레인이면 a+b 가 넘친다. u64 로 넓혀 더하고 반올림한 뒤 좁힌다.
                    //   이것이 portable 합성으로 못 하는 이유이고 곧 이 명령이 존재하는 이유다.
                    proven_u64 x = vm->vecs[av.box].l[k] & emask, y = vm->vecs[bv.box].l[k] & emask;
                    vm->vecs[vm->nvec].l[k] = ((x + y + 1u) >> 1) & emask;
                }
                vm->vecs[vm->nvec].n = n;
                vm->vecs[vm->nvec].esz = esz;
                vm->vecs[vm->nvec].flt = false;
                vm->vecs[vm->nvec].sign = vm->vecs[av.box].sign;
                stack[sp++] = (vmv_t){ .tag = VMV_VEC, .box = (proven_i32)vm->nvec++ };
                break;
            }
            case IRW_SELECT: {
                if (sp < 3 || vm->nvec >= 32) return false;
                vmv_t vb = stack[--sp], va = stack[--sp], m = stack[--sp];
                if (m.tag != VMV_MASK || va.tag != VMV_VEC || vb.tag != VMV_VEC) { vm_diag(vm->diags, "E-VM-TYPE", "select needs mask + two vectors"); return false; }
                proven_u8 n = vm->vecs[va.box].n;
                if (vm->vecs[vb.box].n != n || (proven_u8)m.box != n) { vm_diag(vm->diags, "E-VM-TYPE", "select lane counts differ"); return false; }
                for (proven_u8 k = 0; k < n; k++)
                    vm->vecs[vm->nvec].l[k] = ((m.i >> k) & 1) ? vm->vecs[va.box].l[k] : vm->vecs[vb.box].l[k];
                vm->vecs[vm->nvec].n = n;
                vm->vecs[vm->nvec].esz = vm->vecs[va.box].esz;
                vm->vecs[vm->nvec].flt = vm->vecs[va.box].flt;
                vm->vecs[vm->nvec].sign = vm->vecs[va.box].sign;
                stack[sp++] = (vmv_t){ .tag = VMV_VEC, .box = (proven_i32)vm->nvec++ };
                break;
            }
            case IRW_VREVERSE: case IRW_VROTATE: case IRW_VSHUFFLE: {   // vec → vec (레인 재배열, 새 vec 복사)
                if (sp == 0 || vm->nvec >= 32) return false;
                vmv_t v = stack[--sp];
                if (v.tag != VMV_VEC) { vm_diag(vm->diags, "E-VM-TYPE", "reverse/rotate/shuffle needs a vector"); return false; }
                proven_u8 n = vm->vecs[v.box].n;
                if (in->w == IRW_VSHUFFLE) {
                    proven_u8 cnt = (proven_u8)((in->a >> 32) & 0xf);
                    if (cnt != n) { vm_diag(vm->diags, "E-VM-TYPE", "shuffle index count != lane count"); return false; }
                }
                for (proven_u8 k = 0; k < n; k++) {
                    proven_u8 src;
                    if (in->w == IRW_VREVERSE) src = (proven_u8)(n - 1 - k);
                    else if (in->w == IRW_VROTATE) src = (proven_u8)(((proven_u64)k + (proven_u64)in->a) % n);
                    else { src = (proven_u8)((in->a >> (4 * k)) & 0xf);
                           if (src >= n) { vm_diag(vm->diags, "E-VM-BOUNDS", "shuffle lane index out of range"); return false; } }
                    vm->vecs[vm->nvec].l[k] = vm->vecs[v.box].l[src];
                }
                vm->vecs[vm->nvec].n = n;
                vm->vecs[vm->nvec].esz = vm->vecs[v.box].esz;
                vm->vecs[vm->nvec].flt = vm->vecs[v.box].flt;
                vm->vecs[vm->nvec].sign = vm->vecs[v.box].sign;
                stack[sp++] = (vmv_t){ .tag = VMV_VEC, .box = (proven_i32)vm->nvec++ };
                break;
            }
            case IRW_RADD: case IRW_RMUL: case IRW_RMIN: case IRW_RMAX: {
                if (sp == 0) return false;
                vmv_t v = stack[--sp];
                if (v.tag != VMV_VEC) { vm_diag(vm->diags, "E-VM-TYPE", "reduce needs a vector"); return false; }
                proven_u8 vn = vm->vecs[v.box].n, vesz = vm->vecs[v.box].esz;
                if (vm->vecs[v.box].flt) {   // horizontal reduction over float lanes
                    double acc = in->w == IRW_RMUL ? 1.0 : 0.0;
                    for (proven_u8 k = 0; k < vn; k++) {
                        double x = ir_bits_to_f(vm->vecs[v.box].l[k], vesz);
                        if (k == 0 && (in->w == IRW_RMIN || in->w == IRW_RMAX)) { acc = x; continue; }
                        if (in->w == IRW_RADD) acc += x;
                        else if (in->w == IRW_RMUL) acc *= x;
                        else if (in->w == IRW_RMIN) acc = x < acc ? x : acc;
                        else acc = x > acc ? x : acc;
                    }
                    stack[sp++] = vmv_flt(acc);
                    break;
                }
                if (vm->vecs[v.box].sign) {
                    // ★★★ **부호형 레인은 부호로 접는다** — 레인은 폭 안에서 두 보수로 저장되므로
                    //   누산 전에 **부호확장**해야 한다. 안 하면 -3 이 65533 으로 더해진다(사용자 결정).
                    proven_i64 sacc = (in->w == IRW_RMUL) ? 1 : 0; bool first = true;
                    for (proven_u8 k = 0; k < vn; k++) {
                        proven_i64 x = ity_wrap((proven_i64)vm->vecs[v.box].l[k], (proven_u8)(vesz * 8), true);
                        if (in->w == IRW_RMIN || in->w == IRW_RMAX) {
                            if (first) { sacc = x; first = false; }
                            else if (in->w == IRW_RMIN) sacc = x < sacc ? x : sacc;
                            else sacc = x > sacc ? x : sacc;
                            continue;
                        }
                        if (in->w == IRW_RADD) sacc += x; else sacc *= x;
                    }
                    stack[sp++] = vmv_int(sacc);
                    break;
                }
                proven_u64 acc = (in->w == IRW_RMUL) ? 1
                               : (in->w == IRW_RMIN) ? ~0ull
                               : (in->w == IRW_RMAX) ? 0 : 0;
                for (proven_u8 k = 0; k < vn; k++) {
                    proven_u64 x = vm->vecs[v.box].l[k];
                    if (in->w == IRW_RADD) acc += x;
                    else if (in->w == IRW_RMUL) acc *= x;
                    else if (in->w == IRW_RMIN) acc = x < acc ? x : acc;
                    else acc = x > acc ? x : acc;
                }
                stack[sp++] = vmv_int((proven_i64)acc);
                break;
            }
            case IRW_MANY: case IRW_MALL: {
                if (sp == 0) return false;
                vmv_t m = stack[--sp];
                if (m.tag != VMV_MASK) { vm_diag(vm->diags, "E-VM-TYPE", "any/all needs a mask"); return false; }
                proven_u64 lanemask = (m.box >= 64) ? ~0ull : ((1ull << m.box) - 1);
                proven_u64 bits = (proven_u64)m.i & lanemask;
                stack[sp++] = vmv_int(in->w == IRW_MANY ? bits != 0 : bits == lanemask);
                break;
            }
            case IRW_ISSOME: {
                if (sp == 0) return false;
                vmv_t v = stack[--sp];
                if (v.tag != VMV_SOME && v.tag != VMV_NONE) { vm_diag(vm->diags, "E-VM-TYPE", "is_some needs an option"); return false; }
                stack[sp++] = vmv_int(v.tag == VMV_SOME);
                break;
            }
            case IRW_SOMEVAL: {
                if (sp == 0) return false;
                vmv_t v = stack[--sp];
                if (v.tag == VMV_NONE) { vm_diag(vm->diags, "E-VM-NONE", "some_value of none (panic)"); return false; }
                if (v.tag != VMV_SOME) { vm_diag(vm->diags, "E-VM-TYPE", "some_value needs an option"); return false; }
                stack[sp++] = vm->boxes[v.box];
                break;
            }
            case IRW_ISOK: case IRW_ISERR: {
                if (sp == 0) return false;
                vmv_t v = stack[--sp];
                if (v.tag != VMV_OK && v.tag != VMV_ERR) {
                    vm_diag(vm->diags, "E-VM-TYPE", "is_ok/is_error needs a result"); return false;
                }
                stack[sp++] = vmv_int(in->w == IRW_ISOK ? (v.tag == VMV_OK) : (v.tag == VMV_ERR));
                break;
            }
            case IRW_OKVAL: {
                if (sp == 0) return false;
                vmv_t v = stack[--sp];
                if (v.tag == VMV_ERR) { vm_diag(vm->diags, "E-VM-ERR", "ok_value of an error (panic)"); return false; }
                if (v.tag != VMV_OK) { vm_diag(vm->diags, "E-VM-TYPE", "ok_value needs a result"); return false; }
                stack[sp++] = vm->boxes[v.box];
                break;
            }
            case IRW_ERRVAL: {
                if (sp == 0) return false;
                vmv_t v = stack[--sp];
                if (v.tag == VMV_OK) { vm_diag(vm->diags, "E-VM-ERR", "error_value of an ok (panic)"); return false; }
                if (v.tag != VMV_ERR) { vm_diag(vm->diags, "E-VM-TYPE", "error_value needs a result"); return false; }
                stack[sp++] = vmv_int(v.i);           // 오류 이름 인덱스
                break;
            }
            case IRW_MAKE: {
                const low_ir_make_t *mk = &vm->ir->makes[in->a];
                if (sp < mk->nfields) return false;
                if (vm->nrecs >= VM_MAXREC) {
                    // ★★★ **VM 은 구조체를 박스에 넣는다. 그리고 풀은 유한하다.**
                    //   이것은 **VM 의 한계**이지 프로그램의 결함이 아니다 — 그리고 그렇게 말한다.
                    //   ★ 타입 하강된 네이티브 경로는 **박스를 아예 안 쓴다**: 구조체가 값이다.
                    //     그래서 같은 프로그램이 네이티브에서는 **끝까지 돈다.**
                    //     오진하지 않는 것이 중요하다(교훈 5): 프로그램은 멀쩡하다.
                    vm_diag(vm->diags, "E-VM-RECPOOL",
                            "the VM's record pool is exhausted. Structs are VALUES, but this "
                            "interpreter boxes them in a fixed pool and a long loop that MAKES "
                            "structs runs it dry. This is a limit OF THE VM, not a defect in your "
                            "program — the native build lowers structs to plain slots and does not "
                            "box them at all (DECISION-0018)");
                    return false;
                }
                vmrec_t *r = &vm->recs[vm->nrecs];
                r->make_idx = (proven_size_t)in->a;
                r->nfields = mk->nfields;
                for (proven_size_t i = 0; i < mk->nfields; i++) r->fields[mk->nfields - 1 - i] = stack[--sp];
                stack[sp++] = (vmv_t){ .tag = VMV_REC, .box = (proven_i32)vm->nrecs++ };
                break;
            }
            case IRW_CALL: {
                const low_ir_def_t *callee = &vm->ir->defs[IR_CALL_IDX(in->a)];
                vmv_t cargs[VM_LOCALS], rv = vmv_int(0);
                if (sp < callee->nparams) return false;
                for (proven_size_t i = 0; i < callee->nparams; i++) cargs[callee->nparams - 1 - i] = stack[--sp];
                // ★ RFC-0055 D3: 파라미터 범위는 **계약**이다. 호출 지점에서 증명되지
                //   않았으면 여기서 검사한다. 증명됐으면(PROVEN) 검사가 사라진다 —
                //   계약이 공짜가 되는 지점.
                if (!(in->a & IR_POL_PROVEN))
                    for (proven_size_t i = 0; i < callee->nparams && i < LOW_MAX_PARAMS; i++) {
                        if (!callee->prng[i].has_rng) continue;
                        if (cargs[i].tag != VMV_INT ||
                            cargs[i].i < callee->prng[i].rlo || cargs[i].i > callee->prng[i].rhi) {
                            vm_diag(vm->diags, "E-VM-CONTRACT",
                                    "argument is outside the parameter's declared range");
                            return false;
                        }
                    }
                // ★★★ **단순 호출(actor 아님)은 드라이버로** — 깊이가 힙 스택에 쌓인다 (RFC-0073 P1b).
                //   actor 호출만 mbox·재시작 슈퍼비전이 붙으니 아래 재귀 경로에 남긴다. 그 밖은 전부
                //   여기서 CALL 을 신호하고, 드라이버가 프레임을 push 한다(C 재귀 없음 → 깊이가 자란다).
                //   ☞ 스칼라 반환 시 풀 회수를 드라이버가 하도록 워터마크를 이 프레임에 저장한다.
                if (!callee->is_actor) {
                    a->rec_wm = vm->nrecs; a->bs_wm = vm->nbset; a->sk_wm = vm->nstk; a->bx_wm = vm->nbox;
                    a->pc = pc; a->sp = sp;   // 호출 명령에 멈춘다(드라이버가 반환값 얹고 pc+1)
                    *out_callee = callee;
                    for (proven_size_t i = 0; i < callee->nparams; i++) out_cargs[i] = cargs[i];
                    *outcome = VMOUT_CALL;
                    return true;
                }
                // ★★★ **레코드 풀을 회수한다** (2026-07-14).
                //   구조체는 **값**인데 VM 은 그것을 **64칸짜리 고정 풀의 박스**로 만들고
                //   **한 번도 반납하지 않았다.** ⇒ **루프 안에서 구조체를 만들면 65번째에
                //   죽는다.** 즉 그 프로그램은 **이 언어로 쓸 수가 없었다.**
                //   (타입 하강이 그것을 드러냈다: 하강된 네이티브는 300만 번을 그냥 돌았다.)
                //
                //   ★ 호출이 **스칼라를 돌려주면** 그 프레임이 만든 레코드는 **아무도 못 본다**
                //     — 값으로만 나갔으니까. 그러면 반납해도 된다. 레코드/컨테이너를
                //     돌려주면 **안 건드린다**(보수적: 과대근사는 안전하다).
                proven_size_t rec_wm = vm->nrecs, bs_wm = vm->nbset, sk_wm = vm->nstk,
                              bx_wm = vm->nbox;
                proven_size_t diag_base = vm->diags->len;   // ★ restart: 이번 호출의 진단 시작점
                // ★★★ **bounded mailbox** (RFC-0009 AC3) — actor 핸들러 호출은 이 인스턴스로 온
                //   메시지 하나다. `mailbox bounded N` 이면 이 인스턴스에 **동시에 몰린**(재진입)
                //   메시지가 N 을 넘을 수 없다 — 순차 모델에서 메일박스가 차는 유일한 길이 재진입이다.
                //   (async 큐는 별개의 미래 작업 — 동시 배달이 온다. 여기선 순차 in-flight 만 바운드.)
                proven_i32 mbox_ridx = -1;
                if (callee->is_actor && cargs[0].tag == VMV_REC
                    && (proven_size_t)cargs[0].box < VM_MAXREC) {
                    proven_size_t sidx = callee->param_sidx[0];
                    if (sidx < vm->ir->nstructs && vm->ir->structs[sidx].mailbox_kind == 1) {
                        mbox_ridx = cargs[0].box;
                        if ((proven_i64)(++vm->mbox_depth[mbox_ridx]) > vm->ir->structs[sidx].mailbox_cap) {
                            vm->mbox_depth[mbox_ridx]--;
                            vm_diag(vm->diags, "E-VM-MAILBOX-FULL",
                                    "the actor's bounded mailbox is full — too many messages are in "
                                    "flight to this instance at once (a re-entrant send exceeded "
                                    "`mailbox bounded N`)");
                            return false;
                        }
                    }
                }
                bool ok_call = vm_exec(vm, callee, cargs, &rv, depth + 1);
                if (mbox_ridx >= 0) vm->mbox_depth[mbox_ridx]--;   // 반환 — in-flight 하나 빠짐
                // ★★★ **restart 슈퍼비전** (RFC-0009 D4, 사용자: B3 재시도→escalate · 자기 상태만 초기화).
                //   핸들러가 **패닉**하면(계약 위반·경계 검사는 아니다 — RFC-0019 §6.5) 슈퍼바이저가
                //   그 인스턴스의 **상태를 초기화**하고 핸들러를 **다시 실행**한다. `max N` 번까지;
                //   계속 터지면 **escalate**(패닉 전파). `never`=재시도 0. `always`=상한까지.
                if (!ok_call && callee->is_actor && cargs[0].tag == VMV_REC
                    && (proven_size_t)cargs[0].box < VM_MAXREC) {
                    proven_size_t sidx = callee->param_sidx[0];
                    if (sidx < vm->ir->nstructs && vm->ir->structs[sidx].failure_policy) {
                        const low_ir_struct_t *ast = &vm->ir->structs[sidx];
                        bool was_panic = false;
                        for (proven_size_t di = diag_base; di < vm->diags->len; di++)
                            if (strcmp(PROVEN_ARRAY_GET(vm->diags, low_diag_t, di)->code, "E-VM-PANIC") == 0) { was_panic = true; break; }
                        if (was_panic) {
                            proven_i64 maxr = ast->failure_policy == 1 ? ast->failure_max        // restart max N
                                            : ast->failure_policy == 3 ? 4096 : 0;               // always(상한)·never
                            vmrec_t *r = &vm->recs[cargs[0].box];
                            for (proven_i64 att = 0; att < maxr && !ok_call; att++) {
                                for (proven_size_t f = 0; f < r->nfields; f++) r->fields[f] = vmv_int(0);  // 자기 상태만 초기화
                                vm->diags->len = diag_base;   // 패닉 진단을 걷는다(재시작했으므로)
                                ok_call = vm_exec(vm, callee, cargs, &rv, depth + 1);
                            }
                        }
                    }
                }
                if (!ok_call) return false;
                if (rv.tag == VMV_INT || rv.tag == VMV_FLT) {
                    vm->nrecs = rec_wm; vm->nbset = bs_wm; vm->nstk = sk_wm; vm->nbox = bx_wm;
                }
                if (sp >= VM_STACK) { vm_diag(vm->diags, "E-VM-STACK", "operand stack overflow"); return false; }
                stack[sp++] = rv;
                break;
            }
            default: {   // binary int words
                if (sp < 2) return false;
                vmv_t bv = stack[--sp], av = stack[--sp];
                if (av.tag == VMV_FLT && bv.tag == VMV_FLT) {   // float core (IEEE; div0 → inf)
                    double x = vmv_f(av), y = vmv_f(bv);
                    // D9: **f32 는 f32 정밀도로 계산한다** — 중간 결과를 double 에 남기지 않는다.
                    // (C 의 FLT_EVAL_METHOD 초과 정밀도 license 를 명시적으로 거부.)
                    bool f32 = (in->a & IR_TY_KNOWN) && (in->a & IR_TY_FLT) && (in->a & 0xff) == 32;
                    #define FRES(e) stack[sp++] = vmv_flt(f32 ? (double)(float)(e) : (e))
                    switch (in->w) {
                        case IRW_ADD: FRES(x + y); break;
                        case IRW_SUB: FRES(x - y); break;
                        case IRW_MUL: FRES(x * y); break;
                        case IRW_DIV: FRES(x / y); break;
                        case IRW_EQ:  stack[sp++] = vmv_int(x == y); break;
                        case IRW_NE:  stack[sp++] = vmv_int(x != y); break;
                        case IRW_LT:  stack[sp++] = vmv_int(x < y); break;
                        case IRW_LE:  stack[sp++] = vmv_int(x <= y); break;
                        case IRW_GT:  stack[sp++] = vmv_int(x > y); break;
                        case IRW_GE:  stack[sp++] = vmv_int(x >= y); break;
                        default: vm_diag(vm->diags, "E-VM-TYPE", "unsupported float operation"); return false;
                    }
                    #undef FRES
                    break;
                }
                if (av.tag == VMV_VEC && bv.tag == VMV_VEC) {   // RFC-0040: arith/compare lift
                    const proven_u8 n = vm->vecs[av.box].n, esz = vm->vecs[av.box].esz;
                    if (vm->vecs[bv.box].n != n) { vm_diag(vm->diags, "E-VM-TYPE", "vector lane counts differ"); return false; }
                    proven_u64 emask = esz >= 8 ? ~0ull : ((1ull << (8 * esz)) - 1);
                    if (vm->vecs[av.box].flt || vm->vecs[bv.box].flt) {   // float lanes
                        if (vm->vecs[av.box].flt != vm->vecs[bv.box].flt) { vm_diag(vm->diags, "E-VM-TYPE", "vector lane kinds differ"); return false; }
                        bool arith = in->w == IRW_ADD || in->w == IRW_SUB || in->w == IRW_MUL || in->w == IRW_DIV;
                        if (arith && vm->nvec >= 32) { vm_diag(vm->diags, "E-VM-VIEW", "vector pool exhausted — the VM keeps vectors in a 32-slot pool. It REWINDS at loop boundaries and at return (docs/runtime-pools.md), so a loop no longer drains it; what is left is a real limit: this many vectors are LIVE AT ONCE (nested calls, or one frame naming more than 32 of them). The native fast path has NO such pool — it keeps a vector in the frame as a VALUE — so at THIS line the two backends still DIVERGE, and that is now the only divergence left here"); return false; }
                        proven_u64 bits = 0;
                        for (proven_u8 k = 0; k < n; k++) {
                            double x = ir_bits_to_f(vm->vecs[av.box].l[k], esz);
                            double y = ir_bits_to_f(vm->vecs[bv.box].l[k], esz);
                            if (arith) {
                                double r2 = in->w == IRW_ADD ? x + y : in->w == IRW_SUB ? x - y
                                          : in->w == IRW_MUL ? x * y : x / y;
                                vm->vecs[vm->nvec].l[k] = ir_f_to_bits(r2, esz);
                            } else {
                                bool t2 = in->w == IRW_EQ ? x == y : in->w == IRW_NE ? x != y :
                                          in->w == IRW_LT ? x < y : in->w == IRW_LE ? x <= y :
                                          in->w == IRW_GT ? x > y : in->w == IRW_GE ? x >= y : false;
                                if (in->w != IRW_EQ && in->w != IRW_NE && in->w != IRW_LT && in->w != IRW_LE &&
                                    in->w != IRW_GT && in->w != IRW_GE) {
                                    vm_diag(vm->diags, "E-VM-TYPE", "unsupported float vector operation"); return false;
                                }
                                if (t2) bits |= 1ull << k;
                            }
                        }
                        if (arith) {
                            vm->vecs[vm->nvec].n = n;
                            vm->vecs[vm->nvec].esz = esz;
                            vm->vecs[vm->nvec].flt = true;
                            vm->vecs[vm->nvec].sign = false;
                            stack[sp++] = (vmv_t){ .tag = VMV_VEC, .box = (proven_i32)vm->nvec++ };
                        } else {
                            stack[sp++] = (vmv_t){ .tag = VMV_MASK, .i = (proven_i64)bits, .box = n };
                        }
                        break;
                    }
                    if (in->w == IRW_ADD || in->w == IRW_SUB || in->w == IRW_MUL) {
                        if (vm->nvec >= 32) { vm_diag(vm->diags, "E-VM-VIEW", "vector pool exhausted — the VM keeps vectors in a 32-slot pool. It REWINDS at loop boundaries and at return (docs/runtime-pools.md), so a loop no longer drains it; what is left is a real limit: this many vectors are LIVE AT ONCE (nested calls, or one frame naming more than 32 of them). The native fast path has NO such pool — it keeps a vector in the frame as a VALUE — so at THIS line the two backends still DIVERGE, and that is now the only divergence left here"); return false; }
                        for (proven_u8 k = 0; k < n; k++) {
                            proven_u64 x = vm->vecs[av.box].l[k], y = vm->vecs[bv.box].l[k];
                            proven_u64 r2 = in->w == IRW_ADD ? x + y : in->w == IRW_SUB ? x - y : x * y;
                            vm->vecs[vm->nvec].l[k] = r2 & emask;
                        }
                        vm->vecs[vm->nvec].n = n;
                        vm->vecs[vm->nvec].esz = esz;
                        vm->vecs[vm->nvec].flt = false;
                        vm->vecs[vm->nvec].sign = vm->vecs[av.box].sign;   // ★ 두 보수라 값은 같다 — 태그만 물려받는다
                        stack[sp++] = (vmv_t){ .tag = VMV_VEC, .box = (proven_i32)vm->nvec++ };
                        break;
                    }
                    if (in->w == IRW_EQ || in->w == IRW_NE || in->w == IRW_LT ||
                        in->w == IRW_LE || in->w == IRW_GT || in->w == IRW_GE) {
                        proven_u64 bits = 0;
                        // ★★★ **부호형 레인은 부호로 비교한다** — 레인은 두 보수라 무부호로 비교하면
                        //   -1 이 가장 큰 수가 된다(사용자 결정 2026-07-23). 부호확장 후 비교.
                        bool csg = vm->vecs[av.box].sign;
                        for (proven_u8 k = 0; k < n; k++) {
                            proven_u64 x = vm->vecs[av.box].l[k], y = vm->vecs[bv.box].l[k];
                            bool t2;
                            if (csg) {
                                proven_i64 sx = ity_wrap((proven_i64)x, (proven_u8)(esz * 8), true);
                                proven_i64 sy = ity_wrap((proven_i64)y, (proven_u8)(esz * 8), true);
                                t2 = in->w == IRW_EQ ? sx == sy : in->w == IRW_NE ? sx != sy :
                                     in->w == IRW_LT ? sx < sy : in->w == IRW_LE ? sx <= sy :
                                     in->w == IRW_GT ? sx > sy : sx >= sy;
                            } else {
                                t2 = in->w == IRW_EQ ? x == y : in->w == IRW_NE ? x != y :
                                     in->w == IRW_LT ? x < y : in->w == IRW_LE ? x <= y :
                                     in->w == IRW_GT ? x > y : x >= y;
                            }
                            if (t2) bits |= 1ull << k;
                        }
                        stack[sp++] = (vmv_t){ .tag = VMV_MASK, .i = (proven_i64)bits, .box = n };
                        break;
                    }
                    vm_diag(vm->diags, "E-VM-TYPE", "unsupported vector operation");
                    return false;
                }
                if ((in->w == IRW_AND || in->w == IRW_OR) && av.tag == VMV_MASK && bv.tag == VMV_MASK) {
                    if (av.box != bv.box) { vm_diag(vm->diags, "E-VM-TYPE", "mask lane counts differ"); return false; }
                    proven_u64 bits = in->w == IRW_AND ? ((proven_u64)av.i & (proven_u64)bv.i)
                                                       : ((proven_u64)av.i | (proven_u64)bv.i);
                    stack[sp++] = (vmv_t){ .tag = VMV_MASK, .i = (proven_i64)bits, .box = av.box };
                    break;
                }
                if (in->w == IRW_ADD && av.tag == VMV_BITSET && bv.tag == VMV_INT) {
                    // set-insertion `add` (RFC-0010 §6.7.1 — same name, dispatched by args)
                    // ★ 범위는 **선언 폭**(로워링이 구운 것)까지. 못 구웠으면(플래그 없음) 64 로 후퇴.
                    proven_i64 bw = (in->a & 0x800000) ? (in->a & 0x7f) : 64;
                    if (bv.i < 0 || bv.i >= bw) { vm_diag(vm->diags, "E-VM-BOUNDS", "bitset element out of range"); return false; }
                    vm->bsets[av.box] |= (proven_u64)1 << bv.i;
                    stack[sp++] = vmv_int(0);   // unit
                    break;
                }
                if (av.tag != VMV_INT || bv.tag != VMV_INT) { vm_diag(vm->diags, "E-VM-TYPE", "arithmetic/compare needs ints"); return false; }
                proven_i64 a = av.i, b = bv.i, r = 0;
                // RFC-0052 S2/D5: div·mod·순서 비교는 **선언된 부호**를 따른다.
                // 로워링이 IR_TY_KNOWN 을 실어 주지 못한 자리는 종전(부호 있음)으로 후퇴한다.
                bool ty_known  = (in->a & IR_TY_KNOWN) != 0;
                bool ty_signed = !ty_known || (in->a & IR_TY_SIGNED) != 0;
                proven_u8 ty_bits = (proven_u8)(in->a & 0xff);
                proven_i64 pol = in->a & IR_POL_MASK;
                // ── S3: 폭 충실 산술. 기본은 **트랩**, wrap_*/sat_* 가 정책을 이름으로 고른다. ──
                if ((in->w == IRW_ADD || in->w == IRW_SUB || in->w == IRW_MUL) && ty_known && ty_bits) {
                    bool ovf; proven_i64 e = 0;
                    if (!ty_signed && ty_bits >= 64) {          // u64 — 무부호 캐리로 판정
                        proven_u64 ua = (proven_u64)a, ub = (proven_u64)b, ur = 0;
                        ovf = in->w == IRW_ADD ? __builtin_add_overflow(ua, ub, &ur)
                            : in->w == IRW_SUB ? __builtin_sub_overflow(ua, ub, &ur)
                            :                    __builtin_mul_overflow(ua, ub, &ur);
                        e = (proven_i64)ur;
                    } else {
                        ovf = in->w == IRW_ADD ? __builtin_add_overflow(a, b, &e)
                            : in->w == IRW_SUB ? __builtin_sub_overflow(a, b, &e)
                            :                    __builtin_mul_overflow(a, b, &e);
                        if (!ovf && !ity_fits(e, ty_bits, ty_signed)) ovf = true;   // 선언 폭 초과
                    }
                    if (in->a & IR_POL_CHK) {   // chk_*: 오버플로를 **값으로** 돌려준다(none)
                        if (ovf) { stack[sp++] = (vmv_t){ .tag = VMV_NONE }; break; }
                        if (vm->nbox >= VM_MAXBOX) { vm_diag(vm->diags, "E-VM-BOXPOOL",
                        "the VM's box pool is exhausted. `some(v)` / `ok(v)` wrap a value, and this "
                        "interpreter puts each one in a fixed pool. This is a limit OF THE VM, not "
                        "a defect in your program — the native build lowers option/result to a "
                        "(tag, value) PAIR in registers and boxes nothing"); return false; }
                        vm->boxes[vm->nbox] = vmv_int(e);
                        stack[sp++] = (vmv_t){ .tag = VMV_SOME, .box = (proven_i32)vm->nbox++ };
                        break;
                    }
                    if (!ovf) { stack[sp++] = vmv_int(e); break; }
                    if (pol & IR_POL_WRAP) { stack[sp++] = vmv_int(ity_wrap(e, ty_bits, ty_signed)); break; }
                    if (pol & IR_POL_SAT) {
                        proven_i64 sat;
                        if (in->w == IRW_MUL) {
                            bool pos = ty_signed ? ((a < 0) == (b < 0)) : true;
                            sat = pos ? ity_hi(ty_bits, ty_signed) : ity_lo(ty_bits, ty_signed);
                        } else if (in->w == IRW_ADD) {
                            sat = (!ty_signed || a > 0) ? ity_hi(ty_bits, ty_signed) : ity_lo(ty_bits, ty_signed);
                        } else {
                            sat = (!ty_signed || a < b) ? ity_lo(ty_bits, ty_signed) : ity_hi(ty_bits, ty_signed);
                        }
                        if (!ty_signed && ty_bits >= 64) sat = (in->w == IRW_SUB) ? 0 : (proven_i64)~0ull;
                        stack[sp++] = vmv_int(sat);
                        break;
                    }
                    if (in->a & IR_POL_PROVEN) {   // RFC-0053 §7: 분석이 "제거 가능"이라 했는데 넘쳤다
                        vm_diag(vm->diags, "E-VM-ANALYSIS",
                                "interval analysis claimed this cannot overflow, but it did — the ANALYSIS is unsound");
                        return false;
                    }
                    vm_diag(vm->diags, "E-VM-OVERFLOW",
                            "integer overflow at the declared width (use wrap_*/sat_*, or prove the range)");
                    return false;
                }
                // 폭 미상(리터럴·추적 불가) — 종전대로 64비트 랩(회귀 없음)
                switch (in->w) {
                    case IRW_ADD: r = (proven_i64)((proven_u64)a + (proven_u64)b); break;
                    case IRW_SUB: r = (proven_i64)((proven_u64)a - (proven_u64)b); break;
                    case IRW_MUL: r = (proven_i64)((proven_u64)a * (proven_u64)b); break;
                    case IRW_DIV:
                        if (!b) {
                            vm_diag(vm->diags, (in->a & IR_POL_PROVEN) ? "E-VM-ANALYSIS" : "E-VM-DIV0",
                                    (in->a & IR_POL_PROVEN)
                                      ? "interval analysis claimed the divisor cannot be zero, but it was"
                                      : "divide by zero");
                            return false;
                        }
                        if (ty_signed) {
                            // ★★★ **몫이 선언 폭에 안 맞으면 트랩**(MIN/-1 = +2^(N-1), i8/i16/i32 도 · D5-(ii) ·
                            //   사용자 결정 2026-07-23). 전엔 INT64_MIN 만 봐 i32 의 -2^31/-1=+2^31 을 놓쳤다
                            //   (i64 로는 안 넘쳐 통과). add/sub/mul 은 이미 폭에서 트랩하는데 div 만 빠졌다.
                            proven_i64 wmin = (ty_known && ty_bits && ty_bits < 64) ? ity_lo(ty_bits, true) : INT64_MIN;
                            if (a == wmin && b == -1) {
                                vm_diag(vm->diags, "E-VM-OVERFLOW", "division overflow (MIN / -1) at the declared width"); return false;
                            }
                            r = a / b;
                        } else r = (proven_i64)((proven_u64)a / (proven_u64)b);
                        break;
                    case IRW_MOD:
                        if (!b) { vm_diag(vm->diags, "E-VM-DIV0", "modulo by zero"); return false; }
                        if (ty_signed) {
                            if (a == INT64_MIN && b == -1) { r = 0; break; }
                            // ★★★★ **제수 부호**(SPEC-004 · RFC-0052 D7, 2026-08-02 수리).
                            //   여기는 `rem — 피제수 부호(D7)` 라고 적고 C 의 `%` 를 냈다 —
                            //   D7 을 거꾸로 읽은 것이다. D7 은 *"관례를 이름으로 고른다"* 이고
                            //   이 낱말의 이름은 `mod` 이므로 짝은 `div_floor` 다. 그 어긋남이
                            //   SPEC-004 가 인용하는 **기계 증명을 깨고 있었다**
                            //   (`mod_is_a_safe_index` — Qed: 0 < n → 0 ≤ i mod n < n, i 가 음수여도).
                            r = a % b;
                            if (r && ((r < 0) != (b < 0))) r += b;
                        } else r = (proven_i64)((proven_u64)a % (proven_u64)b);
                        break;
                    case IRW_AND: r = (a != 0) && (b != 0); break;
                    case IRW_OR:  r = (a != 0) || (b != 0); break;
                    case IRW_EQ:  r = a == b; break;  case IRW_NE: r = a != b; break;
                    case IRW_LT:  r = ty_signed ? (a <  b) : ((proven_u64)a <  (proven_u64)b); break;
                    case IRW_LE:  r = ty_signed ? (a <= b) : ((proven_u64)a <= (proven_u64)b); break;
                    case IRW_GT:  r = ty_signed ? (a >  b) : ((proven_u64)a >  (proven_u64)b); break;
                    case IRW_GE:  r = ty_signed ? (a >= b) : ((proven_u64)a >= (proven_u64)b); break;
                    default: return false;
                }
                stack[sp++] = vmv_int(r);
                break;
            }
        }
    }
    *ret = vmv_int(0);   // fell off the end: unit
    return true;
}

// render a value into out (bounded)
// ★ f64 를 찍을 때 `3.0` 이 `3` 으로 나오면 **정수와 구분이 안 된다** — 도구가 타입을
//   흐리는 것이다. 소수점이 없으면 `.0` 을 붙인다. (C 백엔드도 같은 규칙을 쓴다.)
static void fmt_f(char *out, proven_size_t cap, double d) {
    int n = snprintf(out, cap, "%g", d);
    if (n < 0 || (proven_size_t)n + 3 >= cap) return;
    for (const char *p = out; *p; p++)
        if (*p == '.' || *p == 'e' || *p == 'E' || *p == 'n' || *p == 'i') return;   // nan/inf 포함
    snprintf(out + n, cap - (proven_size_t)n, ".0");
}
static void vmv_render(const vm_ctx_t *vm, vmv_t v, char *out, proven_size_t cap) {
    switch (v.tag) {
        case VMV_INT: snprintf(out, cap, "%lld", (long long)v.i); return;
        case VMV_FLT: fmt_f(out, cap, vmv_f(v)); return;
        case VMV_SLICE: {
            proven_size_t o = (proven_size_t)snprintf(out, cap, "[");
            for (proven_size_t i = 0; i < v.n && o + 8 < cap; i++)
                o += (proven_size_t)snprintf(out + o, cap - o, "%s%u", i ? " " : "", (unsigned)v.p[i]);
            snprintf(out + o, cap - o, "]");
            return;
        }
        case VMV_REC: {
            const vmrec_t *r = &vm->recs[v.box];
            const low_ir_make_t *mk = &vm->ir->makes[r->make_idx];
            proven_size_t o = (proven_size_t)snprintf(out, cap, "{");
            for (proven_size_t i = 0; i < r->nfields && o + 24 < cap; i++) {
                char fv[48];
                vmv_render(vm, r->fields[i], fv, sizeof fv);
                o += (proven_size_t)snprintf(out + o, cap - o, "%s%.*s %s", i ? ", " : "",
                                             (int)mk->fields[i].size, (const char *)mk->fields[i].ptr, fv);
            }
            snprintf(out + o, cap - o, "}");
            return;
        }
        case VMV_OK: {
            char pv[96];
            vmv_render(vm, vm->boxes[v.box], pv, sizeof pv);
            snprintf(out, cap, "ok %s", pv);
            return;
        }
        case VMV_ERR:
            snprintf(out, cap, "err %.*s", (int)vm->ir->errs[v.i].size, (const char *)vm->ir->errs[v.i].ptr);
            return;
        case VMV_STACK:  snprintf(out, cap, "stack(%zu)", (size_t)vm->stks[v.box].n); return;
        case VMV_BITSET: {
            proven_u64 bits = vm->bsets[v.box]; proven_i64 k = 0;
            while (bits) { bits &= bits - 1; k++; }
            snprintf(out, cap, "bitset(%lld)", (long long)k);
            return;
        }
        case VMV_REF:
            snprintf(out, cap, "%s", (v.i & VM_REF_MUT) ? "<mut_ref>" : "<ref>");
            return;
        case VMV_VIEW: {   // render like a record, decoded through the layout
            const low_ir_struct_t *s = &vm->ir->structs[v.box];
            proven_size_t o = (proven_size_t)snprintf(out, cap, "{");
            for (proven_size_t i = 0; i < s->nf && o + 24 < cap; i++) {
                proven_u64 x = 0;
                const proven_u8 *p = v.p + s->f[i].off;
                if (s->f[i].be) for (proven_u8 k = 0; k < s->f[i].size; k++) x = (x << 8) | p[k];
                else            for (proven_u8 k = s->f[i].size; k-- > 0; ) x = (x << 8) | p[k];
                o += s->f[i].flt
                   ? (proven_size_t)snprintf(out + o, cap - o, "%s%.*s %g", i ? ", " : "",
                                             (int)s->f[i].name.size, (const char *)s->f[i].name.ptr,
                                             ir_bits_to_f(x, s->f[i].size))
                   : (proven_size_t)snprintf(out + o, cap - o, "%s%.*s %llu", i ? ", " : "",
                                             (int)s->f[i].name.size, (const char *)s->f[i].name.ptr,
                                             (unsigned long long)x);
            }
            snprintf(out + o, cap - o, "}");
            return;
        }
        case VMV_SOME: {
            char pv[96];
            vmv_render(vm, vm->boxes[v.box], pv, sizeof pv);
            snprintf(out, cap, "some %s", pv);
            return;
        }
        case VMV_NONE:
            snprintf(out, cap, "none");
            return;
        case VMV_VEC: {
            proven_size_t o = (proven_size_t)snprintf(out, cap, "vec[");
            for (proven_u8 k = 0; k < vm->vecs[v.box].n && o + 24 < cap; k++)
                o += vm->vecs[v.box].flt
                   ? (proven_size_t)snprintf(out + o, cap - o, "%s%g", k ? " " : "",
                                             ir_bits_to_f(vm->vecs[v.box].l[k], vm->vecs[v.box].esz))
                   : (proven_size_t)snprintf(out + o, cap - o, "%s%llu", k ? " " : "",
                                             (unsigned long long)vm->vecs[v.box].l[k]);
            snprintf(out + o, cap - o, "]");
            return;
        }
        case VMV_MASK: {
            proven_size_t o = (proven_size_t)snprintf(out, cap, "mask[");
            for (proven_i32 k = 0; k < v.box && o + 4 < cap; k++)
                o += (proven_size_t)snprintf(out + o, cap - o, "%s%u", k ? " " : "",
                                             (unsigned)(((proven_u64)v.i >> k) & 1));
            snprintf(out + o, cap - o, "]");
            return;
        }
        case VMV_VARRAY: {
            proven_size_t o = (proven_size_t)snprintf(out, cap, "[");
            for (proven_size_t i = 0; i < v.n && o + 24 < cap; i++) {
                proven_u64 x = 0;
                const proven_u8 *p = v.p + i * (proven_size_t)v.box;
                for (proven_i32 k = v.box; k-- > 0; ) x = (x << 8) | p[k];
                o += v.i ? (proven_size_t)snprintf(out + o, cap - o, "%s%g", i ? " " : "",
                                                   ir_bits_to_f(x, (proven_u8)v.box))
                         : (proven_size_t)snprintf(out + o, cap - o, "%s%llu", i ? " " : "",
                                                   (unsigned long long)x);
            }
            snprintf(out + o, cap - o, "]");
            return;
        }
    }
    snprintf(out, cap, "?");
}

// ★★ 테스트 러너. `test` def 를 전부 돌린다 — 인자도, 계약도 없다. `expect` 만 있다.
//   (`expect` 의 ASSERT 는 a=4 라 `a & 3 == 0` 이고, 그래서 **절대 제거되지 않는다** —
//    최적화로 사라진 테스트는 **안 돌아간 테스트**다.)
// ★★★ R2 — **쪼개어 실행하고 순차와 비교한다** (RFC-0009 DET-1 · LowentPar.v Qed).
//
//   청크는 **부분 슬라이스**다 — 같은 버퍼를 가리키므로 쓰기가 그대로 반영된다. 발명이 없다.
//   순차(K=1)로 한 번 돌려 **버퍼와 값**을 기록하고, K=2·3·n 으로 **정순·역순** 쪼개어 돌린 뒤
//   **비트 단위로** 비교한다.
//     · 버퍼가 다르면      → `parallel` 선언이 **거짓말**이다(쓰기가 겹친다 — Bernstein 위반)
//     · reduce 값이 다르면 → 연산이 **결합적이지 않다**(DET-3)
//   ★ 정적 검사가 놓친 것을 **런타임이 잡는다.** VM 이 스스로를 고발하는 그 자리와 같다.
static proven_i64 par_combine(proven_u8 op, proven_i64 a, proven_i64 b) {
    switch (op) {
        case 1: return a + b;
        case 2: return a * b;
        case 3: return a < b ? a : b;
        case 4: return a > b ? a : b;
        default: return b;
    }
}
low_ir_par_t low_ir_par_check(const low_ir_t *ir, proven_allocator_t work, bool verbose) {
    low_ir_par_t r = { 0, 0, 0 };
    proven_array_t diags = PROVEN_ARRAY_INIT(work, low_diag_t, 8).value;
    for (proven_size_t di = 0; di < ir->ndefs; di++) {
        const low_ir_def_t *d = &ir->defs[di];
        if (!d->lowered || !d->is_par) continue;
        if (d->par_param >= d->nparams || !((d->param_slice >> d->par_param) & 1u)) continue;
        r.ops++;
        proven_size_t op_fail = 0;

        enum { PN = 24 };
        static const proven_u8 SEED[PN] = { 3, 1, 4, 1, 5, 9, 2, 6, 5, 3, 5, 8,
                                            9, 7, 9, 3, 2, 3, 8, 4, 6, 2, 6, 4 };
        proven_u8 golden[PN], work_buf[PN], orig[PN];
        for (proven_size_t q = 0; q < PN; q++) orig[q] = SEED[q];

        // ── 순차(K = 1) — 기준이다.
        for (proven_size_t q = 0; q < PN; q++) work_buf[q] = orig[q];
        low_ir_arg_t a[LOW_MAX_PARAMS];
        for (proven_size_t p = 0; p < d->nparams; p++)
            a[p] = (p == d->par_param)
                     ? (low_ir_arg_t){ .is_slice = true, .bytes = work_buf, .n = PN }
                     : (low_ir_arg_t){ .v = 1 };
        low_ir_run_result_t seq = low_ir_run_argv(ir, d->name, a, d->nparams, work, &diags, NULL, 0);
        for (proven_size_t q = 0; q < PN; q++) golden[q] = work_buf[q];

        // ── 쪼개기: K = 2, 3, 4, PN — 그리고 **역순**으로도(스케줄이 결과를 바꾸면 안 된다).
        static const proven_size_t KS[] = { 2, 3, 4, PN };
        for (proven_size_t ki = 0; ki < sizeof KS / sizeof KS[0]; ki++) {
            for (int rev = 0; rev < 2; rev++) {
                proven_size_t K = KS[ki];
                for (proven_size_t q = 0; q < PN; q++) work_buf[q] = orig[q];
                proven_i64 acc = 0; bool first = true;
                for (proven_size_t ci = 0; ci < K; ci++) {
                    proven_size_t c = rev ? (K - 1 - ci) : ci;
                    proven_size_t lo = (PN * c) / K, hi = (PN * (c + 1)) / K;
                    if (lo >= hi) continue;
                    for (proven_size_t p = 0; p < d->nparams; p++)
                        a[p] = (p == d->par_param)
                                 ? (low_ir_arg_t){ .is_slice = true, .bytes = work_buf + lo,
                                                   .n = hi - lo }
                                 : (low_ir_arg_t){ .v = 1 };
                    low_ir_run_result_t rr = low_ir_run_argv(ir, d->name, a, d->nparams,
                                                             work, &diags, NULL, 0);
                    if (!rr.ok) { op_fail++; r.failures++; break; }
                    acc = first ? rr.value : par_combine(d->red_op, acc, rr.value);
                    first = false;
                }
                r.splits++;
                // ★ 버퍼가 **비트 동일**해야 한다 — 쓰기가 겹치면 여기서 갈린다.
                for (proven_size_t q = 0; q < PN; q++)
                    if (work_buf[q] != golden[q]) {
                        op_fail++; r.failures++;
                        break;
                    }
                // ★ reduce 를 선언했으면 **값도 같아야 한다** — 결합적이지 않으면 여기서 갈린다.
                if (d->red_op && seq.ok && acc != seq.value) { op_fail++; r.failures++; }
            }
        }
        if (verbose)
            printf("   %.*s: split K=2,3,4,%d (forward+reverse)%s\n",
                   (int)d->name.size, (const char *)d->name.ptr, (int)PN,
                   op_fail ? "   ★ NOT DETERMINISTIC — the `parallel` declaration is a LIE"
                           : "   bit-identical to sequential (DET-1 holds)");
    }
    proven_array_destroy(&diags);
    return r;
}

low_ir_test_result_t low_ir_run_tests(const low_ir_t *ir, proven_allocator_t work, bool verbose) {
    low_ir_test_result_t r = { 0 };
    for (proven_size_t i = 0; i < ir->ndefs; i++) {
        const low_ir_def_t *d = &ir->defs[i];
        if (!d->is_test) continue;
        r.total++;

        // ★★★ **interleaving 오라클** (RFC-0009 D6) — `schedule explore_interleavings [limit N]` 절이
        //   붙은 test 를 **스케줄 계획을 DFS 로 체계적으로 열거**하며 돌린다(B). 각 실행이 한 배달 순서다.
        //   결정적 actor 프로그램은 **모든 순서에서 같은 결과**여야 한다(경합의 actor 판). expect 가 명세:
        //     · 모든 순서에서 통과 → 순서 무관(초록)
        //     · 일부만 통과       → 결과가 **순서에 의존** ⇒ E-SCHED-NONDET (잡았다)
        //   ★ 열거는 **동적 트리 DFS**다 — 핸들러가 메시지를 더 넣어 ready set 이 변하므로, 실행이
        //     기록한 각 단계의 ready 크기(g_sched_sizes)로 backtrack 한다. N(=limit, 기본 4096) 전에
        //     트리를 다 돌면 **전수(exhaustive)**, N 에서 멈추면 **bounded**(정직히 보고 — 조용한 캡 금지).
        //     A 의 K-정준 표본을 이것이 대체한다(같은 하네스·표면, 표본 → 체계적 열거로만 교체).
        if (d->sched_explore) {
            proven_size_t N = d->sched_limit ? (proven_size_t)d->sched_limit : 4096;
            g_sched_plan_len = 0;
            for (proven_size_t z = 0; z < SCHED_MAXSTEP; z++) g_sched_plan[z] = 0;
            proven_size_t nrun = 0, npass = 0, first_fail_run = 0;
            bool any_fail = false, exhausted = false;
            while (nrun < N) {
                proven_array_t dg = PROVEN_ARRAY_INIT(work, low_diag_t, 4).value;
                low_ir_run_result_t rr = low_ir_run(ir, d->name, NULL, 0, work, &dg);
                proven_array_destroy(&dg);
                nrun++;
                if (rr.ok) npass++; else if (!any_fail) { any_fail = true; first_fail_run = nrun; }
                // ★ backtrack — 이 실행이 기록한 단계별 ready 크기(g_sched_sizes[0..g_sched_step))에서
                //   가장 깊은 곳부터, 아직 안 써 본 선택이 있으면 그것으로 넘어간다(DFS). 없으면 소진.
                proven_size_t ns = g_sched_step, i = ns;
                bool advanced = false;
                while (i > 0) {
                    i--;
                    if ((proven_size_t)g_sched_plan[i] + 1 < (proven_size_t)g_sched_sizes[i]) {
                        g_sched_plan[i]++;
                        g_sched_plan_len = i + 1;
                        for (proven_size_t z = i + 1; z < SCHED_MAXSTEP; z++) g_sched_plan[z] = 0;
                        advanced = true; break;
                    }
                }
                if (!advanced) { exhausted = true; break; }
            }
            g_sched_plan_len = 0;   // 기본(FIFO) 복구
            bool ok = !any_fail;
            if (ok) r.passed++; else r.failed++;
            if (verbose || !ok) {
                printf("  %s %.*s", ok ? "[PASS]" : "[FAIL]", (int)d->name.size, (const char *)d->name.ptr);
                if (ok && verbose)
                    printf("  (schedule: %zu interleavings agree, %s)", nrun,
                           exhausted ? "exhaustive" : "bounded by limit");
                if (!ok && npass > 0)
                    printf("\n         E-SCHED-NONDET: this test PASSES under some message-delivery "
                           "orders and FAILS under others (diverged at interleaving %zu of %zu explored) — "
                           "its result DEPENDS on the order the scheduler delivers messages. A deterministic "
                           "actor program must give the SAME result under EVERY interleaving (RFC-0009 D6 — "
                           "the actor analogue of a data race). Fix the handler so order does not matter, or "
                           "serialize with `drain`", first_fail_run, nrun);
                else if (!ok) {   // 모든 순서에서 실패 → 평범한 test 실패. 기준(FIFO) 진단을 뽑아 보인다.
                    g_sched_plan_len = 0;
                    proven_array_t dg = PROVEN_ARRAY_INIT(work, low_diag_t, 4).value;
                    (void)low_ir_run(ir, d->name, NULL, 0, work, &dg);
                    for (proven_size_t k = 0; k < dg.len; k++) {
                        const low_diag_t *g = (const low_diag_t *)proven_array_get(&dg, k);
                        printf("\n         %s: %s", g->code, low_diag_text(g));
                    }
                    proven_array_destroy(&dg);
                }
                printf("\n");
            }
            continue;
        }

        proven_array_t diags = PROVEN_ARRAY_INIT(work, low_diag_t, 4).value;
        low_ir_run_result_t rr = low_ir_run(ir, d->name, NULL, 0, work, &diags);
        bool ok = rr.ok;
        if (ok) r.passed++; else r.failed++;
        if (verbose || !ok) {
            printf("  %s %.*s", ok ? "[PASS]" : "[FAIL]", (int)d->name.size, (const char *)d->name.ptr);
            if (!ok) {
                for (proven_size_t k = 0; k < diags.len; k++) {
                    const low_diag_t *dg = (const low_diag_t *)proven_array_get(&diags, k);
                    printf("\n         %s: %s", dg->code, low_diag_text(dg));
                }
            }
            printf("\n");
        }
        proven_array_destroy(&diags);
    }
    return r;
}

// ★ 오라클이 켜는 걸음 예산(0 = 무한). 사용자 실행(`--run`)은 건드리지 않는다 —
//   오래 도는 프로그램을 도구가 끊는 것은 옳지 않다. 끊어도 되는 것은 **우리가 만든 테스트**다.
/* ★★★ 오라클의 걸음 예산 — **한 자리에서** 정한다 (2026-09-02).
 *   그전에는 같은 수 `2000000` 이 **두 곳에 따로** 적혀 있었다: 예산을 켜는 자리와
 *   그것을 보고하는 자리. 한쪽만 고치면 **보고가 거짓말**이 되는데 아무도 못 본다.
 *   ☞ *한 수는 한 자리에서만 적는다 — 문서에 대고 되풀이해 배운 것이 코드에도
 *     그대로 걸린다.*
 *   `LOWENT_ORACLE_BUDGET` 으로 낮출 수 있게 연 까닭은 `E-VM-BUDGET` 에 **증인을
 *   세우기 위해서**다. 2M 걸음을 실제로 넘기는 픽스처는 골든을 느리게 만든다.
 *   ☞ *못 재는 진단을 두는 것보다, 잴 수 있게 만드는 편이 낫다.*
 *   사용자 실행에는 영향이 없다 — 오라클 경로에서만 켜지고 곧 0 으로 돌아간다. */
proven_u64 low_oracle_budget(void) {
    const char *bo = getenv("LOWENT_ORACLE_BUDGET");
    return (bo && *bo) ? (proven_u64)strtoull(bo, NULL, 10) : 2000000ull;
}

static proven_u64 g_vm_budget = 0;
static proven_u64 g_vm_budget_hits = 0;
// ★★★ **여유는 재야 여유다** (2026-08-14). 예산을 낮추면 *"정직한 케이스가 조용히 건너뛰어지는"*
//   위험이 생긴다 — 그것은 검사가 줄어드는 것이고, 아무 소리도 안 난다. 그래서 **완주한 케이스가
//   실제로 쓴 최대 걸음**을 센다: 그 수가 예산에 가까워지면 다음 픽스처 하나가 벼랑을 넘는다.
static proven_u64 g_vm_steps_max_ok = 0;
void low_ir_set_run_budget(proven_u64 steps) { g_vm_budget = steps; }
proven_u64 low_ir_run_budget_hits(void) { return g_vm_budget_hits; }
proven_u64 low_ir_run_steps_max_ok(void) { return g_vm_steps_max_ok; }
void low_ir_reset_steps_max_ok(void) { g_vm_steps_max_ok = 0; }

low_ir_run_result_t low_ir_run_argv(const low_ir_t *ir, proven_u8str_view_t op,
                                    const low_ir_arg_t *args, proven_size_t nargs,
                                    proven_allocator_t work, proven_array_t *diags,
                                    const proven_u8str_view_t *pargs, proven_size_t npargs) {
    (void)work;
    low_ir_run_result_t out = { .value = 0, .ok = false, .text = "" };
    bool found; proven_size_t di = ir_def_find(ir, op, &found);
    if (!found) { vm_diag(diags, "E-VM-UNDEF", "no such op"); return out; }
    const low_ir_def_t *d = &ir->defs[di];

    if (nargs != d->nparams || nargs > VM_LOCALS) {
        vm_diag(diags, "E-VM-ARITY", "argument count does not match op arity"); return out;
    }

    vmv_t vargs[VM_LOCALS];
    for (proven_size_t i = 0; i < nargs; i++) {
        bool want_slice = ((d->param_slice >> i) & 1u) || ((d->param_struct >> i) & 1u);
        if (args[i].is_slice != want_slice) {
            vm_diag(diags, "E-VM-ARITY",
                    args[i].is_slice ? "a slice was given where the op wants a scalar"
                                     : "the op wants a slice here — write it as [a,b,c]");
            return out;
        }
        if (args[i].is_slice && ((d->param_struct >> i) & 1u)) {
            // ★ 구조체 파라미터 — 준 바이트를 **그대로 view 한다.** `view` 와 **같은 검사**를
            //   건다: 짧으면 E-VM-VIEW, 정렬을 어기면 E-VM-ALIGN. 경계라고 봐주지 않는다.
            const low_ir_struct_t *st = &ir->structs[d->param_sidx[i]];
            if (!st->viewable) {
                vm_diag(diags, "E-VM-VIEW", "a struct parameter whose fields are not all sized "
                                            "scalars cannot be built from bytes at the boundary");
                return out;
            }
            if (st->align > 1 && ((uintptr_t)(const void *)args[i].bytes % st->align) != 0) {
                vm_diag(diags, "E-VM-ALIGN", "struct parameter: the bytes violate the struct's "
                                             "align contract at the program boundary");
                return out;
            }
            if (args[i].n < st->total) {
                vm_diag(diags, "E-VM-VIEW", "struct parameter: fewer bytes than the layout needs "
                                            "at the program boundary");
                return out;
            }
            vargs[i] = (vmv_t){ .tag = VMV_VIEW, .p = args[i].bytes, .n = args[i].n,
                                .box = (proven_i32)d->param_sidx[i] };
        } else if (args[i].is_slice) {
            vargs[i] = (vmv_t){ .tag = VMV_SLICE, .p = args[i].bytes, .n = args[i].n };
        } else if (args[i].is_flt || ((d->param_flt >> i) & 1u)) {
            // ★ f64 파라미터. 정수 리터럴(`2`)로 줘도 f64 로 받는다 — 사람이 쓰는 대로.
            //   (범위 검사는 하지 않는다: `range` 는 정수 축의 계약이다.)
            if (args[i].is_flt) vargs[i] = (vmv_t){ .tag = VMV_FLT, .i = args[i].v };
            else                vargs[i] = vmv_flt((double)args[i].v);
            // ★ `f32` 파라미터는 **경계에서 32 비트로 반올림한다** (#83) — 안 그러면 프로그램 안의
            //   `f32` 값과 밖에서 들어온 `f32` 값이 서로 다른 정밀도를 갖는다.
            if ((d->param_f32 >> i) & 1u) {
                double dv3; memcpy(&dv3, &vargs[i].i, 8);
                float fv3 = (float)dv3; double rv3 = (double)fv3;
                memcpy(&vargs[i].i, &rv3, 8);
            }
        } else {
            // ★★ **프로그램 경계의 진입 검사** (RFC-0055 D6 · RFC-0053 §6.6).
            //   파라미터의 선언 범위는 지금까지 **내부 호출 지점에서만** 강제됐다. 그런데 구간
            //   분석은 그 범위를 **사실로 믿고** 검사를 제거한다. 그러면 바깥(VM 진입·C main·FFI)에서
            //   들어온 값에는 아무 방벽이 없고, 분석이 심은 사실은 아무것도 뒷받침하지 않는다.
            //   ⇒ 계약을 사실로 쓰려면 **강제해야 한다.** 경계가 그 마지막 문이다.
            //
            //   ★★★ 그런데 **선언된 `range` 만** 보고 있었다 — **타입 자체의 범위는 안 봤다.**
            //     `input x u8` 에 **−1** 이 들어왔다. 분석은 `x ∈ [0,255]` 를 **사실로 믿고**
            //     좁힘 검사를 지웠고, VM 이 **스스로를 고발했다**(E-VM-ANALYSIS).
            //     네이티브는 그 검사를 **진짜로 지운다** — 거기선 **아무도 못 잡는 UB** 다.
            //     (자동 차등 퍼저가 값을 −1 까지 흔들어서 찾았다. 교훈 1 그대로:
            //      **믿는 코드 + 강제하지 않는 코드.**)
            //     ⇒ 타입의 범위도 **계약이다.** 경계에서 강제한다.
            // ★★★ **u64 의 위쪽 절반은 아직 들어올 수 없다** (결함 노트 #19·#69, 2026-09-16).
            //   `--run` 은 이제 그 글자를 **자르지 않고** 읽지만(전엔 조용히 i64 최댓값이 됐다),
            //   구간 분석이 `u64` 의 상한을 **i64 최댓값**으로 믿고 검사를 지운다. 그 믿음 위로
            //   더 큰 값을 들이면 분석이 스스로를 고발한다(E-VM-ANALYSIS) — 그래서 경계에서
            //   **정직하게 거절한다.** 부호 없는 64 비트 구간을 표현하는 일은 별도 작업이다.
            // ★★★ **부호 없는 64 비트의 상계는 «자른 값»이다** (결함 노트 #19·#69, 2026-09-16).
            //   구간 도메인이 i64 라 `u64` 의 상한을 `INT64_MAX` 로 적어 두고 **그 사실을 표시**해
            //   두었다(`iv_ty` 의 `wide`) — 분석은 그 상계를 믿지 않는다. 그런데 **경계 검사만**
            //   그 잘린 수를 진짜 상한처럼 강제해서, `u64` 의 정당한 값(2^64−1)을 거절했다.
            //   잘린 상계로는 아무것도 거절하지 않는다.
            // ★★★ 그리고 **그 잘린 상계 위로는 아직 못 들어온다**: 구간 도메인이 i64 라
            //   `u64` 의 위쪽 절반을 표현하지 못하고, 그 위 값을 들이면 분석이 스스로를
            //   고발한다(실측: `shard_of -1` → E-VM-ANALYSIS). 그래서 **조용히 자르지 않고
            //   거절한다** — 자르는 것은 다른 수를 준 것이고, 그것이 결함 노트 #19 였다.
            //   위쪽 절반을 받으려면 도메인이 부호 없는 64 비트를 표현해야 한다(#69 의 남은 몫).
            if (i < LOW_MAX_PARAMS && d->ptype[i].has_rng &&
                (args[i].v < d->ptype[i].rlo || args[i].v > d->ptype[i].rhi)) {
                vm_diag(diags, "E-VM-CONTRACT",
                        "a parameter's TYPE range was violated at the program boundary — the "
                        "value does not fit the declared type (the interval analysis TRUSTS the "
                        "type and deletes checks on that basis; the boundary is what makes that "
                        "trust true)");
                return out;
            }
            if (i < LOW_MAX_PARAMS && d->prng[i].has_rng &&
                (args[i].v < d->prng[i].rlo || args[i].v > d->prng[i].rhi)) {
                vm_diag(diags, "E-VM-CONTRACT",
                        "a parameter's declared range was violated at the program boundary "
                        "(the value entered from outside — no caller proved it)");
                return out;
            }
            vargs[i] = vmv_int(args[i].v);
        }
    }

    static vm_ctx_t vm;   // pools are large; single-threaded CLI/test use
    vm_roots_free(&vm);   // ★ 앞 실행이 잡은 뿌리를 돌려준다(--test 는 같은 구조체로 여러 번 돈다)
    memset(&vm, 0, sizeof vm);
    vm.ir = ir; vm.diags = diags;
    vm.budget = g_vm_budget; vm.steps = 0; vm.budget_hit = false;   // ★ 오라클만 0 이 아니다
    vm.pargs = pargs; vm.npargs = npargs;
    g_sched_step = 0;   // ★ 이 실행의 스케줄 단계 카운터 리셋(계획은 오라클이 세운 그대로 둔다)
    vmv_t rv = vmv_int(0);
    vm.cur_ceiling = VM_MAXCALL;   // ★ 메인 사슬의 깊이 트랩(그린스레드는 자기 창으로 갈아낀다)
    g_slot_hw = 0; g_slot_free_n = 0;   // ★ D3: 참조 슬롯 풀을 이 실행 기준으로 리셋(--test 재실행 대비)
    if (!vm_conc_ensure(&vm, VM_GTHR)) { out.ok = false; return out; }   // ★ 동적 그린스레드 백킹 연결(E-Alloc P3)
    out.ok = vm_exec(&vm, d, vargs, &rv, 0);
    if (vm.budget_hit) g_vm_budget_hits++;
    else if (vm.budget && vm.steps > g_vm_steps_max_ok) g_vm_steps_max_ok = vm.steps;
    if (out.ok) {
        if (rv.tag == VMV_INT) out.value = rv.i;
        vmv_render(&vm, rv, out.text, sizeof out.text);
        // ★★★★ **부호 없는 결과는 부호 없이 찍는다** (결함 노트 #69, 2026-09-16). `output u64` 인 op 이
        //   18446744073709551615 를 돌려주면 표시가 `-1` 이었다 — 값은 맞는데 **보이는 것이 틀렸다**.
        //   같은 수를 두 가지로 말하면 읽는 사람은 도구를 의심하고, 그 의심은 옳다.
        if (rv.tag == VMV_INT && rv.i < 0 && d->out_tyname.size &&
            d->out_tyname.ptr[0] == (proven_byte_t)'u')
            snprintf(out.text, sizeof out.text, "%llu", (unsigned long long)rv.i);
    }
    return out;
}

// 평평한 매핑(옛 CLI): 정수는 위치 인자, **맨 끝의 슬라이스 하나**가 남은 정수를 전부 먹는다.
// 여전히 쓰인다(골든의 대부분) — 위의 모양 있는 형태로 옮겨 준다.
low_ir_run_result_t low_ir_run(const low_ir_t *ir, proven_u8str_view_t op,
                               const proven_i64 *args, proven_size_t nargs,
                               proven_allocator_t work, proven_array_t *diags) {
    low_ir_run_result_t out = { .value = 0, .ok = false, .text = "" };
    bool found; proven_size_t di = ir_def_find(ir, op, &found);
    if (!found) { vm_diag(diags, "E-VM-UNDEF", "no such op"); return out; }
    const low_ir_def_t *d = &ir->defs[di];

    // over-aligned so an `align n` view of the whole argument slice can succeed;
    // a sub-slice at a non-multiple offset still (correctly) violates the contract
    alignas(64) static proven_u8 sbuf[64];
    low_ir_arg_t a[VM_LOCALS];
    bool tail_slice = d->nparams > 0 && d->param_slice == (1u << (d->nparams - 1));
    if (tail_slice) {
        if (nargs + 1 < d->nparams || d->nparams > VM_LOCALS) {
            vm_diag(diags, "E-VM-ARITY", "too few arguments"); return out;
        }
        for (proven_size_t i = 0; i + 1 < d->nparams; i++)
            a[i] = (low_ir_arg_t){ .is_slice = false, .v = args[i] };
        proven_size_t n = nargs - (d->nparams - 1);
        if (n > sizeof sbuf) n = sizeof sbuf;
        for (proven_size_t i = 0; i < n; i++) sbuf[i] = (proven_u8)args[d->nparams - 1 + i];
        a[d->nparams - 1] = (low_ir_arg_t){ .is_slice = true, .bytes = sbuf, .n = n };
        return low_ir_run_argv(ir, op, a, d->nparams, work, diags, NULL, 0);
    }
    if (nargs > VM_LOCALS) { vm_diag(diags, "E-VM-ARITY", "too many arguments"); return out; }
    for (proven_size_t i = 0; i < nargs; i++)
        a[i] = (low_ir_arg_t){ .is_slice = false, .v = args[i] };
    return low_ir_run_argv(ir, op, a, nargs, work, diags, NULL, 0);
}
