// low_ir.h — S5: stack IR + content addressing (slice b: values + SCC hashing).
//
// Lowers MVP `fn`/`proc` bodies into a flat stack-IR word stream (SPEC-013
// §14.2), applying the canonical-IR normalizations that matter for identity
// (SPEC-011 §12.2): names → de Bruijn slot indices, `expr` infix → prefix words,
// guard/make/error sugar → IR nodes, surface decoration erased.
//
// S5b core: ints/bools + byte slices + records (`make`) + result (`ok`/`error <enum
// variant>`/`try`) + guard, on top of the S5a control/arith core. The VM executes
// tagged values; `--run OP <file> [ints…]` maps CLI ints to args (a single
// slice-typed param takes all ints as bytes) and renders the result value.
//
// Content addressing (SPEC-011 §12.3, BLAKE3-256):
//   iface-hash = H(kind, param types, return type, effects)
//   def-hash   = SCC fixed-point Merkle hash: call sites carry the callee's
//                def-hash (topological over the SCC DAG); within an SCC, members
//                are canonically ordered by a preliminary hash and call sites
//                carry the member index (RFC-0012/DECISION-0003 C32 direction).
// Structurally identical defs hash equal (dedup §15.4); a callee body edit now
// propagates into caller def-hashes (Merkle), while iface-hash stays the
// relink-only boundary.
//
// Still outside the core (→ S5c): strings/heredocs, for/loop over containers,
// stack/bitset/pool ops (§8 example B), float, access sugar, C backend.
#ifndef LOW_IR_H
#define LOW_IR_H

#include <stdio.h>
#include "proven/array.h"
#include "low_cst.h"

// ★★★ **뿌리 얼로케이터의 바닥 — 양 백엔드가 *이 한 숫자*를 나눠 쓴다** (RFC-0043 A3).
//
//   ★★★ **이것은 "그" 얼로케이터가 아니라 *뿌리* 얼로케이터다.** 바이트가 프로그램에
//     **처음 들어오는 한 자리**이고, 그래서 런타임이 주는 권한(`cap allocator`)이다 —
//     누군가는 진짜 메모리를 갖고 있어야 하는데, 그것이 ambient 면 RFC-0043 D1 이 무너진다.
//
//   ☞ **얼로케이터 종류는 언어가 정하지 않는다.** 범프·풀·프리리스트·카운팅·일부러 실패하는
//     것 — 전부 **Lowent 로 쓰는 라이브러리**이고, `slice u8` 을 받아 잘라 주면 된다
//     (`impl/tests/vm_alloc.low` 의 `trait allocator` + `actor bump` 가 이미 그렇게 산다).
//     즉 사용자 얼로케이터는 특별한 언어 장치가 아니라 **평범한 actor** 다.
//   ☞ 아래 바닥이 범프인 것은 **런타임의 사정**이지 언어의 약속이 아니다. 언어가 약속하는 것은
//     `alloc_bytes` 가 요청한 만큼의 바이트를 주거나 `none` 을 준다는 것뿐이다.
//     ★ 안 짓는 것(정직히): `free`·재사용·리셋 — 그래서 지금 수명은 **프로그램 전체**다.
//
//   ★ **그리고 오라클 *안*이다.** malloc 과 달리 (a) 시작이 0 이고 (b) 같은 프로그램이면
//     같은 순서로 같은 크기를 잘라 가므로, VM 과 네이티브가 **같은 바이트**를 본다.
//     주소는 다르지만 언어가 주소를 노출하지 않는다 ⇒ diff-sweep 이 이것을 덮는다.
//   ★★ 숫자가 **여기 하나만** 있는 이유: 풀 크기를 양쪽에 따로 적었다가 어긋난 전례가 있다
//     (`VM_MAXBOX 4096` vs 네이티브 `lw_boxes[128]`). 검사되지 않는 중복은 썩는다(§0).
// ★★★★ **두 뿌리** (RFC-0112 D3 · WO-0211, 2026-09-13).
//   · **고정 창** — `cap allocator` · `effects alloc` · `heap` 이 아닌 영역 블록이 깎는다. **안 자란다.**
//     호스티드 기본 크기가 아래 수다(`.bss` — 실행 파일에 크기가 박히지 않는다). 프리스탠딩은 링커가
//     주는 경계(`__lw_fixed_start`·`__lw_fixed_end`) 사이이고, `--emit-ldscript` 가 내는 기본 조각의
//     크기가 `LOW_FIXED_BYTES_BOARD` 다. VM 은 `--target` 에 따라 그 둘 가운데 하나를 쓴다(`--fixed-bytes`).
//   · **힙** — `cap heap` · `effects heap` · `region <이름> heap` 이 깎는다. **호스티드에만** 있고, 다 쓰면
//     청크를 잇는다(요청이 청크보다 크면 그 크기로). 이미 준 바이트는 옮기지 않는다.
//   ★ 64 KiB 는 옛 단일 아레나의 크기를 그대로 물려받은 수다 — 근거가 기록된 적이 없고, 이제 호스티드
//     프로그램이 더 필요하면 `cap heap` 을 받는다(고정 창을 키우는 것은 `-DLW_FIXED_BYTES`).
#define LOW_FIXED_BYTES        (64u * 1024u)
#define LOW_FIXED_BYTES_BOARD  4096u
#define LOW_HEAP_CHUNK         (64u * 1024u)
// 이 실행의 고정 창 크기 — `--fixed-bytes N` 이 정하고, 안 정하면 타깃이 정한다.
void          low_ir_set_fixed_bytes(proven_size_t n);
proven_size_t low_ir_fixed_bytes(void);

// ★★★ **박스 풀 — 양 백엔드가 이 한 숫자를 나눠 쓴다** (2026-07-19).
//   option/result 의 payload 가 여기 산다. VM 은 4096, 네이티브는 **128** 이었다 —
//   그리고 그 어긋남을 위(아레나) 주석이 *"검사되지 않는 중복은 썩는다"* 의 **전례**로 적어
//   두고 있었다. 그 전례가 실제로 물었다: 64 바이트 버퍼로 큰 파일을 스트리밍하면 반복마다
//   `some` 박스가 하나씩 쌓여 **VM 은 통과하고 네이티브만 "box pool" 로 죽었다**(RFC-0069 §6).
//   ⇒ 숫자를 **여기 하나만** 둔다. 베끼지 않고 흘려보낸다.
#define LOW_BOXPOOL 4096u

typedef enum {
    IRW_CONST,   // a = immediate i64
    IRW_LOAD,    // a = slot
    IRW_STORE,   // a = slot
    IRW_DROP,
    IRW_ADD, IRW_SUB, IRW_MUL, IRW_DIV, IRW_MOD, IRW_NEG,
    IRW_AND, IRW_OR, IRW_NOT,
    IRW_EQ, IRW_NE, IRW_LT, IRW_LE, IRW_GT, IRW_GE,
    IRW_BR,      // a = absolute target
    IRW_BRZ,     // a = absolute target (branch if popped value == 0)
    IRW_SWITCH,  // ★ RFC-0081 MM9-B2 — a = N (case 수). 값 v 를 pop → 뒤따르는 N+1 개 BR 중
                 //   v(0..N-1)번째로, 범위 밖이면 N번째(default)로 **계산 점프**한다(pc += clamp(v,0,N)).
                 //   즉 점프 테이블 = SWITCH 뒤의 BR 런. 조밀 정수/enum-tag match 를 O(1) 디스패치로.
    IRW_CALL,    // a = def index (encoding substitutes the callee def-hash)
    IRW_RET,
    // S5b — values
    IRW_LEN,      // slice → length
    IRW_INDEX,    // slice, i → byte
    IRW_ISTORE,   // slice, i, v → ()   ★ 슬라이스 원소 쓰기 (커널·병렬의 전제)
                  //   a = IR_POL_PROVEN 이면 경계 검사가 제거됐다(관계 사실 i < len(s))
    IRW_SUBSLICE, // slice, lo, hi → slice [lo,hi)
    IRW_SWAP,     // slice, i, j → ()  ★ 두 원소를 맞바꾼다 (원소 타입 무관)
    IRW_WRAP_OK,  // v → ok(v)
    // ★★★ **option 을 직접 만들 수 없었다.** `some x` · `none` 생성자가 **IR 에 없었다** —
    //   option 은 오직 `try … else_none` 으로만 생겼다. 그러면 RFC-0043 D5(*"OOM 은 값이다"*)를
    //   **표현할 수가 없다**: 할당 실패를 `none` 으로 돌려주는 것이 그 모델의 전부인데.
    //   ★ 소비자(`is_some`·`some_value`)는 **있었다.** **만들 수만 없었다.**
    IRW_WRAP_SOME,  // v → some(v)
    IRW_WRAP_NONE,  //   → none
    IRW_WRAP_ERR, // a = error-name index (low_ir_t.errs) → err
    IRW_TRY,      // ok(v) → v; err → early-return it; plain value → itself
    IRW_MAKE,     // a = make-site index (low_ir_t.makes); pops nfields values
    // S5c — containers (§8 example B)
    IRW_SNEW,     // capacity → new stack handle (region is erased at lowering)
    IRW_SPUSH,    // stack, v → unit (mutates the pool entry)
    IRW_SPOP_INTO,// a = slot; stack → bool (pop into slot; false when empty)
    IRW_BNEW,     // nbits → new bitset handle
    IRW_CONTAINS, // bitset, i → bool
    // ★★★ **집합 연산** (RFC-0010 §6.7.1) — 표현은 dense bit-vector(한 워드). 전부 **분기 없는 bitwise**.
    IRW_BREMOVE,  // bitset, i → ()      비트 clear
    IRW_BUNION,   // a, b → bitset       워드별 OR
    IRW_BINTER,   // a, b → bitset       워드별 AND
    IRW_BDIFF,    // a, b → bitset       워드별 AND-NOT
    IRW_BCOMPL,   // s → bitset          워드별 NOT (**폭으로 마스킹** — 그래서 폭을 저장한다)
    IRW_BEMPTY,   // s → bool
    IRW_BSUBSET,  // a, b → bool         a ⊆ b

    IRW_COUNT,    // bitset → popcount / stack → element count
    IRW_ARGC,     // ★ cap args — 프로그램 인자 개수 (RFC-0030 D2′; 타입 유도 하강)
    IRW_ARGV,     // ★ cap args — i 번째 인자 → option str (범위 밖 = none)
    IRW_ENVGET,   // ★ cap env  — 이름으로 조회 → option str (없으면 none)
    IRW_WRITE,    // ★ cap io   — 바이트열을 fd 로 (1=stdout, 2=stderr) → 쓴 바이트 수
    IRW_ALLOCB,   // ★ cap allocator — n 바이트를 얻는다 → option slice u8 (모자라면 none)
    IRW_RDIN,     // ★ cap io   — 표준입력에서 호출자 버퍼로 → option u64 (EOF 까지 · 안 들어가면 none)
    // ★★★ **reactor** (RFC-0071 A1) — IO 를 **중단 가능**하게 만들 자리.
    //   A1 은 **블로킹 폴백만** 짓는다: 답이 `read_in` 과 **한 바이트도 다르지 않아야** 하고,
    //   그것이 A1 의 게이트다. 뒤 단계(epoll·io_uring)가 그 게이트를 깨면 즉시 보인다.
    IRW_RNEW,     // ★ cap io   — (mem, depth) → option u64(핸들). 뒷받침은 **호출자가 준다**
    IRW_RREAD,    // ★ cap io   — (rh, fd, dst) → option u64. `read_in` 과 **같은 의미**
    // ★★★★ **쓰기 쪽** (2026-08-22, N4 단계1). reactor 는 **읽기만** 중단 가능했다 —
    //   그래서 `task_group`·`await` 의 약속이 **쓰기에서는 거짓말**이었다.
    //   ☞ **부분 쓰기를 숨기지 않는다**: 이번에 몇 바이트 갔는지만 정직하게 답하고,
    //     전부 보내는 것은 호출자의 일이다(`r_read` 와 같은 규율 — `send_all` 의 루프는
    //     라이브러리에 남는다). 숨기면 호출자가 *"얼마나 갔는지"* 를 못 본다.
    IRW_RWRITE,   // ★ cap io   — (rh, fd, src) → option u64 (보낸 바이트)
    // ★★★ **스트림 리프 넷** (RFC-0069 §6) — 파일을 **조각으로** 읽고 쓴다.
    //   `read_file` 은 파일 전체를 버퍼에 담아 버퍼보다 크면 `none` 이었다. 이 넷이 그 벽을 없앤다.
    //   핸들은 **정수 fd** 이고, 그것을 `owned` 로 감싸 close 망각을 컴파일 오류로 만드는 것은
    //   **라이브러리의 몫**이다(lib/file.low — A6 의 `pending` 과 같은 패턴). 리프는 얇게 둔다.
    IRW_FOPEN,    // ★ cap file_system — (path, mode) → option u64 (fd) · 못 열면 none
    IRW_FREAD,    // ★ cap file_system — (fd, dst)    → option u64 (읽은 바이트 · 0 = EOF)
    IRW_FWRITE,   // ★ cap file_system — (fd, bytes)  → option u64 (쓴 바이트)
    IRW_FCLOSE,   // ★ cap file_system — (fd)         → bool (닫기도 실패할 수 있다 — NFS)
    // ★★★ **랜덤 접근** (RFC-0069 §4 read_at) — 스트림 커서를 옮긴다(fseek/ftell).
    IRW_FSEEK,    // ★ cap file_system — (fd, off, whence) → option u64 (새 위치) · whence 0/1/2 · 실패 none
    // ★★★ **디렉터리 순회** (RFC-0069 §6 — opendir/readdir/closedir) — 파일시스템을 **열거**한다.
    IRW_DOPEN,    // ★ cap file_system — (path)      → option u64 (dir 핸들) · 못 열면 none
    IRW_DREAD,    // ★ cap file_system — (dh, dst)   → option u64 (이름 길이 · **0 = 끝**) · dst 안 맞으면 none
    IRW_DCLOSE,   // ★ cap file_system — (dh)        → bool
    // ★★★ **파일 타입 질의** (RFC-0069 §6 — stat) — 경로가 **무엇인지** 묻는다(파일? 디렉터리?).
    //   디렉터리 순회가 드러낸 구멍: 나열은 되는데 각 엔트리가 뭔지 몰라 재귀(find·du·tree)를 못 했다.
    IRW_FTYPE,    // ★ cap file_system — (path)      → option u64 (0=파일·1=디렉터리·2=기타) · 없으면 none
    // ★★★ **따라가지 않는 짝** (X-0030 ⓐ · WO-0210). `file_type` 은 `stat` 이라 심링크를 **가리키는
    //   대상**으로 본다 — 그래서 «한쪽은 링크, 한쪽은 진짜 파일» 을 구분하지 못했다(lowdiff 가 그것을
    //   «같다» 고 답했다, 실측). 이것은 `lstat` 이다: 링크 자신을 본다(3 = 심링크).
    //   ☞ 기존 낱말의 뜻은 **한 자리도 안 바꾼다** — 새 낱말을 더한다. 그것이 소유자가 고른 길이다.
    IRW_LTYPE,    // ★ cap file_system — (path)      → option u64 (0=파일·1=디렉터리·2=기타·3=심링크)
    // ★★★ **파일시스템 변경** (RFC-0069 §6) — 읽기 능력의 짝. 상태를 **바꾼다**(설치·빌드·원자적 교체).
    IRW_DMAKE,    // ★ cap file_system — (path)      → bool (mkdir · 이미 있거나 못 만들면 false)
    IRW_PREMOVE,  // ★ cap file_system — (path)      → bool (remove — 파일이든 빈 디렉터리든)
    IRW_PRENAME,  // ★ cap file_system — (old, new)  → bool (rename — 원자적 이동/이름변경)
    // ★★★ cap tty (2026-07-26) — 터미널의 **권한 있는 절반**. 순수 절반(ANSI 조립·화면 diff·
    //   폭 계산)은 lib/term.low 가 이미 한다. 여기 셋만 커널이 필요하다.
    IRW_TTYRAW,   // ★ cap tty — (on)      → bool  (raw 모드 진입/복귀 — 에코·행버퍼 끄기)
    IRW_TTYREAD,  // ★ cap tty — (dst)     → option u64 (읽은 바이트 수 · 0 = 지금은 없음)
    IRW_TTYSIZE,  // ★ cap tty — ()        → option u64 (행<<32 | 열 — 한 값에 둘)
    // ★★★★★ **시계** (RFC-0090 N1) — `cap clock`.
    //   proven_c_lib 이 이미 준다(`proven_time_now`·`proven_time_sleep`) ⇒ 일은 **노출**이다.
    //   ☞ 왜 권한이 필요한가: 시계는 부수효과가 없어 보이지만 **결정성을 깬다** — 같은 입력에
    //     다른 답이다. 그 사실이 시그니처에 보여야 순수 계산과 갈린다(`cap atomic` 과 다른 점).
    IRW_TIMENOW,  // ★ cap clock — ()      → u64   단조 나노초(절대 시각이 아니다)
    IRW_TIMESLEEP,// ★ cap clock — (ms)    → u64   실제로 잔 밀리초(요청과 다를 수 있다)
    IRW_TIMELOCAL,// ★ cap clock — ()      → u64   지역 날짜시각 팩(아래 lib/clock.low 이 푼다)
    // ★★★ 해시 둘 (RFC-0090 N3) — **권한 없음**. 알고리즘이 고정이라 알려진 답이 오라클이다.
    IRW_HASH64,   // (slice) → u64   FNV-1a 64
    IRW_CRC32,    // (slice) → u64   반사 CRC-32 (0xEDB88320)
    IRW_SAMESL,   // (slice, slice) → bool  같은 시작 · 같은 길이 (RFC-0112 D10)
    // ★★★ 난수 (RFC-0090 N3b) — 하나만 권한이 필요하다.
    IRW_RANDBYTES,// cap random — (mut slice) → u64  채운 바이트 수 (OS 엔트로피)
    IRW_RNGNEXT,  // (u64 상태) → u64  xoshiro-류 다음 값 — **권한 없음**(시드는 호출자 것)
    IRW_SHA256,
    IRW_SHA512,   // (slice, mut slice 32) → u64  써 넣은 바이트 수 — **권한 없음**(순수)
    IRW_SHA384,   // ★ (slice, mut slice 48) → u64 — SHA-512 의 다른 시작값 (X-0032)
    // ★★★★ **AES 와 GHASH 는 잎이다** (X-0043 ⓑ, 소유자 지시 2026-09-20). 이 언어로 쓴 구현이
    //   각각 0.6 MB/s · 4.1 MB/s 였고(실측), 그래서 AES-128-GCM 전체가 0.5 MB/s 였다.
    //   ☞ 내린 것은 **계산**뿐이다: GCM 의 짜임(J0 · 태그를 먼저 검증하고 복호 · 길이를 비트로)은
    //     `lib/gcm.low` 에 남는다. 숨은 규율은 검사할 수 없다.
    IRW_AESCTR,   // (key16, mut ctr16, src, mut dst) → u64  처리한 바이트 수 · 권한 없음
    IRW_GHASH,    // (h16, mut z16, data)             → u64  먹인 바이트 수   · 권한 없음
    // ★★★ **소켓 리프** (2026-07-23) — 전부 `cap net` 을 **대야** 한다(파일과 같은 규율).
    //   AF_UNIX socketpair 로 시작: 주소·listen/accept 없이 **두 연결된 fd** — 결정적 loopback 이라
    //   VM·native 가 같은 바이트를 본다(오라클 안). 핸들은 **풀 인덱스**(raw fd 위조 방지, 파일과 같다).
    //   확장(connect/bind/listen/accept · cap net 도메인)은 후속 — 리프는 얇게 둔다.
    IRW_NPAIR,    // ★ cap net — ()          → option u64 (두 소켓 핸들 팩: slot0 | slot1<<32) · 실패 none
    IRW_NSEND,    // ★ cap net — (fd, bytes)  → option u64 (보낸 바이트) · 실패 none
    IRW_NRECV,    // ★ cap net — (fd, dst)    → option u64 (받은 바이트 · 0 = 상대가 닫음) · 실패 none
    IRW_NCLOSE,   // ★ cap net — (fd)         → bool
    // ★★★ **네트워크 면** (2026-07-23) — socketpair 위에 진짜 TCP loopback 을 얹는다.
    //   포트는 **0 을 주면 임시포트**를 받고 net_port 로 되묻는다 ⇒ 고정 포트 충돌이 없어
    //   테스트가 결정적이다. listen→connect→accept 가 한 프로세스에서 블로킹 없이 끝난다
    //   (loopback 은 백로그가 있으면 connect 가 즉시 완료되고 accept 가 대기 없이 받는다).
    IRW_NLISTEN,  // ★ cap net — (port)      → option u64 (듣는 소켓 핸들) · 0 = 임시포트
    IRW_NPORT,    // ★ cap net — (fd)        → option u64 (실제로 묶인 포트)
    IRW_NCONNECT, // ★ cap net — (addr u32, port) → option u64 (연결된 소켓 핸들)
    //   ★★★ 2026-09-18 (X-0032): 주소를 **받는다**. 그전에는 127.0.0.1 이 박혀 있어
    //     이 언어는 자기 기계 밖으로 나갈 수 없었다. 주소는 호스트 바이트 차례의 u32 다.
    IRW_NRESOLVE, // ★ cap net — (이름 slice) → option u32 (IPv4 주소) · DNS
    //   ★ 이름을 주소로 바꾸는 일도 **바깥에 닿는 일**이라 `cap net` 이고 `effects io` 다.
    //     connect 와 **가른 까닭**: 주소만 가진 프로그램이 DNS 에 닿지 않게 하고, 골든이
    //     주소를 손으로 주어 **망 없이** 연결을 잴 수 있게 하기 위해서다.
    IRW_NACCEPT,  // ★ cap net — (fd)        → option u64 (받아들인 연결 핸들)
    // ★★★ **어휘 region 블록** (SPEC-004 §4.5 도입형태 ① · RFC-0004 §157).
    //   `region <이름> <종류> do … end` — 수명이 **스코프**다. MVP region 은 명세 그대로
    //   **범위 기반 일괄 free**(객체별 free 없음)이고 arena 는 bump 다.
    //   ⇒ 진입에서 범프 커서의 **표식**을 잡고, 끝에서 거기로 **되돌린다**. 두 워드면 된다.
    //   ★ 안전은 정적으로 지킨다: 그 블록에서 할당한 슬라이스가 밖으로 새면 **거절**한다
    //     (E-REGION-ESCAPE) — 안 그러면 되돌리기가 곧 use-after-free 다.
    IRW_RMARK,    //   → u64   현재 범프 커서(표식)
    IRW_RRESET,   // u64 → ()  그 표식으로 되돌린다(그 뒤 할당은 전부 무효가 된다)
    // S-impl-4 — dynamic verifier V1 (RFC-0017): runtime references
    IRW_REF,      // a = slot → shared reference to this frame's local
    IRW_MREF,     // a = slot → mutable reference to this frame's local
    IRW_DEREF,    // ref → referent value (V-DANGLING checked via frame generations)
    // structs first-class: canonical prefix `field` (G1); `s to f`/`f in s`
    // (RFC-0047) and glued `s.f` (rev.e §2.8) normalize to it at lowering
    IRW_FIELD,    // a = field-name index (low_ir_t.fields); record/view → field value
    IRW_FSTORE,   // a = field-name index; record, v → ()   ★ 구조체 **필드 쓰기**
    // RFC-0025 §6.6 T2: zero-copy byte→type views (layout from struct decls)
    IRW_VIEW,     // a = struct index; slice → view (length-checked; panic when short)
    IRW_TRYVIEW,  // a = struct index; slice → some(view) | none
    IRW_ISSOME,   // option → bool
    IRW_SOMEVAL,  // some(v) → v (panic on none)
    IRW_ISOK,     // result → bool          ★ result 소비 — option 쪽만 있고 여기가 비어 있었다
    IRW_ISERR,    // result → bool
    IRW_OKVAL,    // ok(v) → v   (panic on err)
    IRW_ERRVAL,   // err(e) → e  (panic on ok; 오류 이름 인덱스를 정수로 준다)
    IRW_ENCODE,   // a = struct index; record/view → bytes (RFC-0025 §6.7 encode 방향)
    // ★ SPEC-004 §188/§190 — 표현 재해석 사다리의 **T0**: `bit_cast t v` (무비용 plain↔plain).
    //   T1(`view`)·T2(`encode`)는 있었는데 **가장 쉬운 칸이 비어 있었다.** 그리고 도구는
    //   `bit_cast` 를 "E-IR-UNDEF: undefined name" 이라 했다 — *네 프로그램이 틀렸다*.
    //   실제로는 *도구가 못 한다* 였다. **진단이 둘을 섞으면 아무것도 알 수 없다**(교훈 2).
    IRW_BITCAST,  // a = size | IR_FLT_BIT | IR_SGN_BIT; 같은 폭 plain 값의 **비트 재해석**
    // ★ `panic "msg" .` — SPEC-002 의 제어 어휘이자 effect 어휘(`effects panic`)인데
    //   **구현이 없었고**, 도구는 "E-IR-UNDEF: undefined name" 이라 했다 — *네 프로그램이 틀렸다*.
    //   아니다. **도구가 못 한 것이다.** bit_cast 와 같은 오진이다.
    IRW_PANIC,    // a = string index; 즉시 트랩(복구 불가 — 계약 위반과 구별된다)
    // ★ SPEC-007 §28 — **채널 전환**. `try E else_none`(result→option) ·
    //   `try E else_error <variant>`(option→result). 명세의 형태인데 **E-IR-ARITY**
    //   ("여분의 피연산자") 로 거절됐다 — 또 오진이다(교훈 5).
    IRW_ELSE_NONE,  // result → option : ok(v) → some(v) · err(_) → none
    IRW_ELSE_ERR,   // a = error-name index; option → result : some(v) → ok(v) · none → err(E)
    IRW_VARRAY,   // a = element size | IR_FLT_BIT; slice → typed array view (len=count, index=decode)
    IRW_STR,      // a = string index (low_ir_t.strs); → byte slice of the literal
    // RFC-0040 portable SIMD: lanes are comptime (from the binding's `vec t n` type);
    // arith/compare lift onto vectors (compare → mask); only vector-specific ops are new
    // float core + runtime cast (G2)
    IRW_FCONST,   // a = IEEE-754 double bits
    IRW_CAST,     // a = 0 → f64 | width(1/2/4/8) → integer truncate/mask
    IRW_UNM,      // a = 0 sqrt | 1 abs | 2 floor | 3 ceil   (numeric, 1 operand)
    IRW_BINM,     // a = 0 fmod | 1 min  | 2 max             (numeric, 2 operands)
    IRW_SPLAT,    // a = lanes<<8 | elem size (| IR_FLT_BIT); scalar → vec
    IRW_VLOAD,    // a = lanes<<8 | elem size (| IR_FLT_BIT); (varray|slice), elem idx → vec
    IRW_VSTORE,   // a = 0; dst(varray|slice), idx, vec → () ; lanes/esz from the vec operand (VLOAD 의 역)
    IRW_VLOADM,   // a = lanes<<8 | esz (| IR_FLT_BIT); (varray|slice), idx, mask → vec (꺼진 lane=0, 안 읽음)
    IRW_VSTOREM,  // a = 0; dst, idx, vec, mask → () ; 켜진 lane 만 쓴다(나머지 보존). 레인/esz 는 vec 이 실어 온다
    IRW_VAVG,     // a = 0; va, vb → vec — **target intrinsic**(RFC-0040 D5): 레인별 라운딩 평균 (a+b+1)>>1
                  //   (x86 pavgb/pavgw · ARM vrhadd). widening 이라 단일 portable op 으로 표현 불가.
                  //   `unsafe target <iset>` 안에서만 쓸 수 있다(격리). 비트-정확 ⇒ diff-sweep 로 VM≡native.
    IRW_VALOR,    // a = 0; (option|result), default → t  — **총체형**: some/ok 면 payload, none/err 면 default
    IRW_SPOP,     // a = 0; mut stack T → option T — **총체형**: 비었으면 none (부분형 panic 아님)
    IRW_SELECT,   // mask, va, vb → lanewise m?a:b
    IRW_VREVERSE, // a = 0; vec → vec (레인 역순). 레인수는 vec 이 실어 온다
    IRW_VROTATE,  // a = 회전량 n; vec → vec (out[k]=v[(k+n) mod lanes])
    IRW_VSHUFFLE, // a = (count<<32) | idx0..idxN (4bit each); vec → vec (out[k]=v[idx[k]], comptime 순열)
    IRW_RADD, IRW_RMUL, IRW_RMIN, IRW_RMAX,   // vec → scalar (horizontal)
    IRW_MANY, IRW_MALL,                        // mask → bool
    // RFC-0008/0053: 계약 진입 검사. bool → (없음). 거짓이면 E-VM-CONTRACT 트랩.
    // **계약을 정적 사실로 쓰려면 그것이 강제되어야 한다** — 검사되지 않는 중복은 거짓말로 썩는다.
    IRW_ASSERT,
    // ★★★ **인라인 asm** (RFC-0041). a = asm 표의 인덱스.
    //   컴파일러는 **템플릿 안을 안 읽는다** — 어셈블러의 몫이다. 그래서 이 낱말은
    //   **불투명 블록**이다. 그런데 **피연산자와 템플릿은 서로를 참조한다** — 그리고
    //   그 둘이 **한 뜻의 두 표현**이다(교훈 7). 그래서 **그 둘만은 반드시 대조한다.**
    IRW_ASM,
    // ★★★ **C 를 부른다** (RFC-0063). a = def 인덱스. VM 은 못 부르고 **그렇게 말한다**.
    IRW_EXTERN,
    // ★★★ **cstr → str** (RFC-0068 S4 · C2) — 널종단 C 문자열의 **길이를 몰라 스캔한다**(strlen, O(n)).
    //   이름이 비용을 말한다(D4). cstr 는 **VM 밖 FFI 경계 전용**이라 VM 은 못 하고 **그렇게 말한다**
    //   (extern 과 같은 정직함) — 네이티브 리프 빌트인 하나(strlen 격)로 `{p, strlen(p)}` 를 낸다.
    IRW_CSTR2STR,
    // ★★★ **str_buf → cstr** (RFC-0068 S4 · C4 · D2 봉인) — 널종단 바이트 버퍼의 **base 포인터**를
    //   생 cstr 로 낸다(O(1)·무할당·**뷰**). `effects unsafe` — 널종단을 **호출자가 보증**한다(D4:
    //   str 뷰는 널을 약속 못 하므로 안전한 str→cstr 은 없다 · str_buf 만이 봉인하는 봉인 경로다).
    //   VM 은 못 본다(cstr 는 FFI 경계 전용 — E-VM-CSTR).
    IRW_STR2CSTR,
    // ★★★ **콜백 참조** (`unsafe_fn <op>`, RFC-0066) — export extern op 의 **주소**를 값으로 낸다.
    //   a = def 인덱스. 씨에 함수 포인터로 넘긴다(경계 밖은 씨의 규율 — 이름이 unsafe 라 말한다).
    IRW_FNREF,
    // ★★★ **async 배달** (RFC-0009 — call/cast 분리). 값 `send` 는 동기(IRW_CALL)로 두고, `spawn
    //   send <actor> <msg> [args]` 는 메시지를 **메일박스에 넣고 즉시 반환**한다(fire-and-forget).
    //   `drain <actor>` 가 그 메일박스를 **FIFO 로** 비운다(단일 스레드 결정적 — 스케줄러 불필요).
    IRW_ASEND,   // a = 핸들러 def 인덱스. [instance, args…](nparams 개) 팝 → 큐에 넣는다.
    IRW_DRAIN,   // [instance] 팝 → 그 인스턴스의 메일박스를 FIFO 로 처리한다.
    // ★★★ **결정적 스케줄러** (RFC-0009 D6, 첫 슬라이스) — `schedule .` 문. 인스턴스 하나가 아니라
    //   **모든 actor 의 대기 메시지 전부**를 quiescence 까지 배달한다(핸들러가 또 보내면 그것도).
    //   기본 순서는 FIFO(결정적). interleaving 오라클이 이 순서를 바꿔가며 결과 불변을 검사한다.
    IRW_SCHED,   // 피연산자 0 — 전역 메일박스를 스케줄러 순서로 비운다. 단위값을 민다.
    // ★★★ **구조적 동시성** (RFC-0009 D3-b, task_group) — `spawn <op> [args]` 가 그룹 안에서 태스크를
    //   큐잉한다. a = op def 인덱스. [args…](nparams 개) 팝 → 태스크 큐(mbox 에 box=-1 마커)에 넣는다.
    //   그룹 end 의 IRW_SCHED 가 태스크·메시지를 함께 quiescence 까지 join 한다(SC2). 태스크는 인스턴스가
    //   없다(box=-1) — 스케줄러가 op 를 인자만으로 부른다. 오라클이 태스크 실행 순서를 탐색한다.
    IRW_TSPAWN,
    // ★★★ **green thread — `yield`** (RFC-0009, task_group→green_thread→channel 2/3). 실행 중인 태스크가
    //   **중단**하고 스케줄러에 양보한다(다른 ready 태스크가 돌고 나중에 재개). VM 은 ucontext 코루틴으로
    //   진짜 중단한다; 네이티브는 no-op(오라클이 순서 무관을 증명한 프로그램만 통과 → 완주해도 같은 결과).
    //   블로킹의 최소 원시어 — channel 의 블로킹 recv 가 이 위에 얹힌다. 피연산자 0, 단위 push.
    IRW_YIELD,
    // ★★★ **channel — 블로킹 통신** (RFC-0009, task_group→green_thread→channel 3/3). green thread 의
    //   yield 위에 얹는다: `chrecv` 는 빈 채널에서 **블록**(yield)하고 sender 가 넣으면 재개, `chsend` 는
    //   찬 채널에서 블록(yield)하고 recver 가 빼면 재개. bounded FIFO(RFC-0009 D5: 통신=bounded channel).
    IRW_CHNEW,   // 피연산자 0(또는 capacity) — 채널 하나 만들고 핸들(정수 인덱스)을 민다.
    IRW_CHSEND,  // [ch, v] 팝 → 채널에 v 를 넣는다(full 이면 yield 로 블록). 단위 push.
    IRW_CHRECV,  // [ch] 팝 → 채널에서 하나 뺀다(empty 면 yield 로 블록). 그 값을 push.
    // ★★★ **await** (RFC-0009 §8.3) — `spawn <op>` 이 job 핸들을 돌려주고, `await <h>` 는 그 태스크가
    //   **끝날 때까지 블록**(green thread yield)한 뒤 결과를 준다. 완료가 awaiter 를 깨운다(채널과 같은 규율).
    IRW_AWAIT,   // [job핸들] 팝 → 그 job 완료까지 블록 → job 의 반환값을 push.
    // ★★★ **cancel_on_error** (RFC-0009 SC4) — task_group 절. a=1 이면 취소 스코프 진입(자식 오류 →
    //   형제 취소), a=0 이면 나감. 스케줄러가 이 전역을 보고 오류 자식이 나오면 안 시작한/블록된 형제를
    //   취소한다(그 오류로 done 처리 — RFC-0006: unwinding 없는 취소, 오류는 await 로 표면화).
    IRW_CANCELSCOPE,
    // ★★★ **회복 가능한 backpressure** (RFC-0009 §AC3 — full=`try send`(즉시 실패)). `try spawn send`
    //   는 IRW_ASEND 와 같이 큐잉하되, 메일박스가 **차면 트랩 대신 `result<unit, mailbox_full>`** 를
    //   돌려준다(ok=넣었다 · err=찼다). 단일 스레드라 `wait`(backpressure)는 데드락 → 즉시 실패만
    //   정직하다. a 하위 16비트=핸들러 def 인덱스, 상위(>>24)=`mailbox_full` 에러명 인덱스.
    IRW_TASEND,
    // ★★★ **배열 내용 술어** (R5 잔여) — `requires elem_lt s N` 의 진입 검사. a = cmp(0 lt·1 le·
    //   2 gt·3 ge). [slice, N] → bool(모든 원소가 cmp N 이면 1). 분석은 이걸로 **원소 구간**을
    //   심어 `index s i` 가 ⊤ 대신 그 구간을 낸다 — 구간 밖의 마지막 술어(배열 내용)를 닫는다.
    IRW_ELEMCK,
    // ★★★ **비트 연산** (RFC-0064) — 시스템 언어인데 **하나도 없었다.**
    //   a = 타입 meta(폭·부호). **폭이 계약이다**: `bit_not` 은 그 폭에서 뒤집는다
    //   (C 의 정수 승격이 없다 — `~(uint8_t)0` 이 `-1` 이 되는 그 결함이 여기엔 없다).
    IRW_BAND, IRW_BOR, IRW_BXOR, IRW_BNOT,
    IRW_SHL, IRW_SHR,           // 시프트 양 ≥ 폭 ⇒ **트랩**(C 의 UB 가 아니다)
    IRW_WSHL, IRW_WSHR,         // wrap_* — 처분을 **이름이 고른다**(RFC-0052)
    IRW_ROTL, IRW_ROTR,
    IRW_CLMULLO, IRW_CLMULHI,          // 캐리 없는 곱셈의 아랫말·윗말 (RFC-0119)
    IRW_AESROUND, IRW_AESLAST,         // AES 한 라운드 · 마지막 라운드 (RFC-0119)
    // ★★★★ **ChaCha20 은 폭이 모자라서 낱말이 됐다** (RFC-0122, 2026-09-24).
    //   알고리즘은 이 언어로 표현된다(`lib/chacha.low` 가 그렇게 쓰여 있고, 그것이 뜻의 정의로
    //   남는다). 없는 것은 **폭**이다: 서로 독립인 블록 넷·여덟을 한 레지스터에 싣는 자리.
    IRW_CHACHA20, // (key32, mut ctr16, src, mut dst) → u64  처리한 바이트 수 · 권한 없음
    // ★★★ **Poly1305 의 블록 되풀이** (RFC-0122). 상태의 자리는 `lib/poly.low` 와 **같다**
    //   (`st[0..5]` = h · `st[5..10]` = r) — `setup`·`emit` 은 언어에 남고 가운데만 내린다.
    IRW_POLY1305, // (mut st(원소 10 이상), data) → u64  먹인 바이트 수 · 권한 없음
    // ★★★★ **GCM 한 덩이** (RFC-0124, 2026-09-24). 흐름과 누산을 **한 바퀴에** 돈다 —
    //   내린 것은 여전히 **계산**이고, 차례(무엇을 먼저 먹이나 · 언제 증언하나)는 `lib/gcm.low` 에 남는다.
    //   ☞ 복호는 이 낱말을 **안 쓴다**: 태그를 먼저 증언하고 나서 푸는 차례가 그 자리의 물건이다.
    IRW_AESGCM,   // (key16, mut ctr16, h16, mut z16, src, mut dst) → u64  흐른 바이트 수 · 권한 없음
    // ★★★★ **ChaCha20-Poly1305 한 덩이** (RFC-0128) — 흐름과 누산을 한 바퀴에.
    //   둘은 쓰는 실행 자원이 다르다(AVX2 벡터 대 정수 곱셈기) ⇒ 겹치면 거의 공짜다.
    IRW_CHAPOLY,  // (key32, mut ctr16, mut st(u64 10↑), src, mut dst) → u64 · 권한 없음
    IRW_POPCNT, IRW_CLZ, IRW_CTZ, IRW_BSWAP,
    // ★★★ **level-3 atomic** (RFC-0018) — RC11 은 **증명됐는데**(LowentRC11.v, 7정리 Qed)
    //   도구가 **안 줬다**(W-NOT-YET). *증명해 놓고 구현 안 한 것*도 검사되지 않는 중복이다.
    //   a = ordering(하위 3비트: 0=relaxed 1=acquire 2=release 3=acq_rel 4=seq_cst) | 타입 meta.
    //   load(1) · store(2) · rmw(2, add/sub/and/or/xor/swap) · cas(3) · fence(0).
    IRW_ALOAD, IRW_ASTORE,
    IRW_AADD, IRW_ASUB, IRW_AAND, IRW_AOR, IRW_AXOR, IRW_ASWAP,
    IRW_ACAS, IRW_AFENCE,
    // ★★★ **레지스터 블록을 자기 기저 주소에서 연다** (RFC-0042 §8-2). a = struct 인덱스.
    //   피연산자 0 — **바이트 슬라이스 `{ (u8*)mmio_base, total }`** 를 민다. 뒤이은 IRW_VIEW 가
    //   그것을 평소대로 먹는다(정렬·길이 계약이 공짜로 따라온다 — 기저 주소의 정렬은 MMIO 에서
    //   **진짜 계약**이다).
    //
    //   ★ 이것이 필요한 이유: 블록을 얻는 길이 `view T <슬라이스>` 뿐이었고 그 슬라이스는
    //     **호출자가 준다**. ISR 은 인자를 못 받으므로(E-ISR-PARAMS) **줄 사람이 없었다** —
    //     디바이스 때문에 존재하는 함수가 디바이스에 닿을 수 없었다.
    //   ★ VM 은 이것을 **못 돌린다**(0x40020000 을 만질 수 없다) — asm·extern·cstr 과 같은 자리다.
    //     흉내내지 않고 **그렇게 말한다**(E-VM-MMIO).
    IRW_MMIOBLK,
    IRW_RESBLK,
    // ★★★★★ **prefetch — 유일하게 승격된 특례** (RFC-0104 §8-14, 서명 2026-08-29).
    //   후보 다섯을 문턱 `max(5%, 잡음×2)` = **5%** 로 재서 하나만 남았다:
    //     bit scan/count −45%(타깃 플래그 문제였다) · wide CAS −50%(라이브러리 호출) ·
    //     pause +29.6%(쓸 자리가 없다) · gather +13.1%(RFC-0040 소관) · **prefetch +25.2%**.
    //   ☞ 그리고 prefetch 만 **결과를 안 바꾼다** — 순수 힌트라 "결과 동일" 을 잴 필요가 없다.
    //   피연산자 둘(슬라이스·색인), 미는 값 **없음**. VM 은 **그냥 무시한다**(힌트니까).
    IRW_PREFETCH,
    // ★★★★★ **`value_or` 의 기본값을 지연시키려고 더한 내부 낱말** (결함 value-or-eager-default).
    //   정본(SPEC-007 §8.4 · DECISION-0003 C1)은 *"기본값은 성공 시 미평가"* 라고 적었는데
    //   도구는 **먼저 평가했다** — `value_or (some 7) (boom)` 이 트랩했다. 값이 있는데도.
    //   ⇒ `value_or` 를 op 이 아니라 **분기**로 낮춘다(`and`/`or` 의 단락평가와 같은 처방).
    //   그러려면 *"값이 들어 있나"* 를 묻는 술어가 필요한데, `is_some` 은 result 를 거절하고
    //   `is_ok` 는 option 을 거절한다 — **둘 다 받는 것이 없었다.** 이것이 그 자리다.
    //   ☞ **표면이 아니다**: 빌트인도 낱말도 아니고 하강이 만들어 쓰는 내부 낱말이다.
    IRW_HASVAL,   // option|result → bool (some/ok = 1, none/err = 0)
} low_irw_t;

// ★★★★★ **명령이 자기가 온 자리를 든다** (2026-09-10, REQ-0015 · WO-0196).
//   여기 줄이 없어서 방출된 C 에 `#line` 이 **하나도** 없었고, 그래서 네이티브가 죽으면
//   `gdb` 는 사람이 쓴 `.low` 가 아니라 **생성된 C** 를 보여 줬다 — 준비도 평가가
//   「남에게 맡기는 코드에는 아직 이르다」 고 판정한 두 까닭 중 하나가 그것이다.
//   ★ 해시는 **안 움직인다**: def 인코딩(`ir_encode_def`)은 `.w` 와 `.a` 만 읽는다.
//     증분 빌드와 dedup 이 이 두 칸을 안 본다는 것이 이 설계의 전제다.
//   ★ 파일은 **번호**로 든다(모듈 여럿이 한 단위로 링크된다) — 이름표는 `low_ir_t` 에 있다.
typedef struct {
    low_irw_t   w;
    proven_i64  a;
    proven_u32  line;     // 소스 줄 (0 = 모름)
    proven_u16  fileid;   // low_ir_t.files 의 색인 (0 = 모름)
} low_ir_ins_t;

// ★ asm 피연산자 — **레지스터에 묶인 Lowent 값**. `{name}` 이 템플릿에서 이것을 부른다.
typedef struct {
    proven_u8str_view_t name;   // 템플릿의 `{name}` 이자 지역/파라미터의 이름
    proven_u8str_view_t reg;    // 레지스터 **클래스**(`reg`) 또는 **특정 레지스터**(`rax`…)
    bool                is_out;
    proven_i16          slot;   // 지역 슬롯 (in 일 때)
} low_ir_asmop_t;

typedef struct {
    proven_u8str_view_t target;   // 이 ISA 에서만 컴파일된다 — 아니면 **거절한다**(조용히 빼지 않는다)
    proven_u8str_view_t tmpl;     // heredoc 원문 (RFC-0001 §4.8) — 컴파일러는 **안 읽는다**
    low_ir_asmop_t      ops[8];   proven_size_t nops;
    proven_u8str_view_t clob[8];  proven_size_t nclob;
    proven_u8str_view_t opt[8];   proven_size_t nopt;
    proven_u32          line;
} low_ir_asm_t;

#define IR_FLT_BIT   0x10000   // marks f32/f64 element/lane types in word operands
#define IR_SGN_BIT   0x20000   // marks a SIGNED integer target (bit_cast)
// ★ `slice <struct>` — 원소가 **구조체**인 슬라이스. 구조체 배열은 시스템 프로그래밍의
//   기본 모양인데, 원소 크기를 못 구해서 **바이트 슬라이스로 남았다**: `len` 이 **바이트를 셌다**
//   (2바이트 구조체 2개짜리 슬라이스의 len 이 4). `slice u32` 에서 고쳤던 그 병인데
//   **구조체 원소는 손대지 않았다.** 컴파일되고, 실행되고, 틀린다.
#define IR_STRUCT_BIT 0x40000   // VARRAY 의 원소가 구조체다 — (meta >> 20) = 구조체 인덱스
// RFC-0052 S2 — the operand type carried by arithmetic/compare words:
//   bits 0..7 = declared width · IR_TY_SIGNED = declared signedness · IR_TY_KNOWN = tracked
// A word with IR_TY_KNOWN unset falls back to the old (signed, 64-bit) behavior.
#define IR_TY_KNOWN  0x1000
#define IR_TY_SIGNED 0x2000
// RFC-0052 S3 — 처분 정책(오버플로·폭 절단). 기본(비트 없음) = **트랩**.
//   wrap_add/wrap_sub/wrap_mul · narrow_wrap  → IR_POL_WRAP
//   sat_add /sat_sub /sat_mul  · narrow_sat   → IR_POL_SAT
#define IR_POL_WRAP  0x4000
#define IR_POL_SAT   0x8000
#define IR_POL_MASK  (IR_POL_WRAP | IR_POL_SAT)
#define IR_TY_FLT    0x20000   // 피연산자가 부동 — 폭(32/64)이 **연산 정밀도**다 (D9)
// RFC-0052 D5/D6 — 오버플로/0나누기를 **값으로** 돌려주는 계열.
//   chk_add/chk_sub/chk_mul → option τ  (none = 오버플로)
//   div_nz : (τ, nonzero τ) -> τ        (제수가 0 이 아님이 **타입으로** 보장됨)
#define IR_POL_CHK   0x40000
#define IR_POL_NZ    0x80000
// RFC-0053 — 구간 분석이 **범위를 증명한** 자리. 검사를 제거해도 안전하다.
//   VM 은 그래도 검사를 유지하고, 발화하면 E-VM-ANALYSIS 로 **분석을 고발**한다
//   (퍼저가 곧 분석의 건전성 검증기가 된다 — RFC-0053 §7).
//   C 백엔드는 이 자리에서 검사 없는 원시 산술을 방출한다(성능 회수).
#define IR_POL_PROVEN 0x100000
// IRW_CALL 의 a 는 def 인덱스 + 플래그다. RFC-0055 D3: PROVEN 이면 인자 범위 검사를
// **호출 지점에서 이미 증명**했다는 뜻 — 진입 검사를 건너뛴다(계약이 공짜가 된다).
#define IR_CALL_IDX(a) ((proven_size_t)((a) & 0xFFFF))
// ★★ 구조체/레코드의 **필드 개수 한계**. 8이었고, **9번째부터 조용히 버려졌다**:
//   `--check` 는 초록불이었고, 레이아웃 크기도 8바이트로 **조용히 틀렸다**(encode 가 8바이트만 쓴다).
//   런타임에야 E-VM-FIELD 로 터지고, 그 진단은 **필드 접근을 탓한다** — 잘못된 자리를.
//   ⇒ `prng[8]`(파라미터 범위) 과 **정확히 같은 병**이다: **임의의 배열 크기가 조용히 자른다.**
//     한계를 올리고, **넘으면 거절한다**(E-IR-LIMIT). 잘라 놓고 아무 말 안 하는 것보다 낫다.
#define IR_MAKE_MAXF 16
typedef struct { proven_u8str_view_t type_name; proven_u8str_view_t fields[IR_MAKE_MAXF]; proven_size_t nfields; } low_ir_make_t;

// struct layout (from `struct N [layout packed .] fields… end`; RFC-0025 §6.3/6.4)
// ★ 필드가 **구조체**일 수도 있다(중첩 구조체 — 와이어 포맷의 기본 모양).
//   sidx < 0 이면 스칼라. 없어서 `struct outer { i inner . }` 이 **"선언된 구조체가 아니다"** 로
//   거절됐다 — 선언은 돼 있는데. (또 하나의 오진: E-IR-UNDEF = *네 프로그램이 틀렸다*.)
// ★★★ **레지스터의 권한** (RFC-0042 D3) — `ro` 를 쓰거나 `wo` 를 읽으면 **컴파일 에러**다.
//   그것이 이 RFC 의 안전 성질이다: *"wo 레지스터 읽기·ro 쓰기 = 컴파일 에러"*.
typedef enum { FP_RW = 0, FP_RO, FP_WO } low_fperm_t;
// ★★★ `boxed` — 이 필드는 **바이트 표현이 없다**(슬라이스·option·배열 …). 값 한 칸을 쓰지만
//   그 한 칸은 **태그 값**이지 레이아웃이 아니다. ⇒ 이 필드가 하나라도 있으면 구조체는
//   `viewable` 이 아니다. (크기만 보면 8 이라 스칼라와 구별되지 않는다 — 그래서 표식이 필요했다.)
typedef struct { proven_u8str_view_t name; proven_u8 size; bool be; bool flt; bool boxed;
                 proven_i16 sidx; proven_u16 off; low_fperm_t perm;
                 // ★ **아핀 필드**(RFC-0044 §9.1) — `owned T` 로 선언된 필드. 이것이 있어야
                 //   필드 단위 소유·부분 이동(OS1~OS4)이 도달 가능하다.
                 bool owned;
                 // ★★★★★ **슬라이스 필드의 원소 폭** (2026-08-30, RFC-0106 단계 0).
                 //   `boxed` 는 *"바이트 표현이 없다"* 만 말하고 **무엇의 슬라이스인지**는
                 //   안 말했다. 그래서 그 필드를 빠른 경로로 나르려 해도 `index` 의
                 //   **스트라이드를 모른다** — 표현을 옮기려면 폭을 알아야 한다.
                 //   0 = 슬라이스 필드가 아니다. 그 밖 = 원소 한 개의 바이트 수.
                 //   ☞ 이 칸은 **아무 동작도 안 바꾼다** — 나르기만 한다. 쓰는 쪽은 RFC-0106.
                 proven_u8 elem;
                 // ★★★★ **권한 칸** (RFC-0112 D6 · WO-0213) — `h cap heap .` 처럼 actor 상태에 든 권한.
                 //   0 = 권한 칸이 아니다 · 1 = `cap allocator`(고정 창) · 2 = `cap heap`(힙).
                 //   권한은 번역 시점 표시라 실행 중 뜻이 없다 — 하강이 `alloc_bytes <칸>` 의 뿌리를 여기서 읽는다.
                 proven_u8 capkind;
                 // ★ 칸의 **타입 낱말**(첫 낱말) — `send (field g al) m` 이 받는 actor 를 타입으로 찾는다(WO-0213).
                 proven_u8str_view_t tyname; } low_ir_sfield_t;
typedef struct {
    proven_u8str_view_t name;
    bool                packed;
    bool                viewable;   // all fields are sized scalars
    proven_u16          align;      // `align n` contract → effective alignment (RFC-0051 §5.1)
    // ★ 그 정렬이 **타깃 질의에서 왔는가**(`align machine.cache_line .`) — RFC-0104 §8-7.
    //   왔다면 이 구조체의 레이아웃은 **타깃마다 다르다**. 공개 ABI 표면에서는 거절한다.
    bool                align_from_target;
    low_ir_sfield_t     f[IR_MAKE_MAXF];
    proven_size_t       nf;
    proven_u16          total;      // layout size in bytes
    // ★★★ **MMIO 레지스터 블록** (RFC-0042 D2) — `mmio <base> .` 의사필드가 이것을 켠다.
    //   그러면 이 struct 는 **디바이스 레지스터의 지도**다: 접근은 **volatile** 이어야 하고,
    //   **`cap mmio` 를 건네받아야** 하며(할당과 같은 규율), **권한이 강제된다**.
    bool                is_mmio;
    proven_i64          mmio_base;
    // ★★★★ **예약된 정적 블록** (RFC-0039 §9-2 갈래①, 2026-08-02) — `reserve .` 의사필드가
    //   이것을 켠다. `mmio <base>` 가 *"내 기저는 이 주소"* 라면 이쪽은 *"내 기저는 **링커가
    //   준다**"* 다: 도구가 그 타입 크기만큼 한 칸을 잡아 두고, `view T .` 이 거기서 연다.
    //   ☞ 왜 필요했나: ISR 은 인자가 없어(E-ISR-PARAMS) 루프에게 건넬 통로가 없고, 최상위
    //     가변 상태도 없다(E-TOPLEVEL) — T1 의 정의(ISR→잡)가 표현 불가능했다.
    //   ★ 전역 가변 상태와 다른 점: **권한이 있어야 열린다**(ISR 이거나 `cap machine`).
    //     문은 여전히 하나 폭이고, 연 다음의 뷰는 평범한 값이라 라이브러리에 넘어간다.
    bool                is_reserve;
    // ★★★ **actor 정책** (RFC-0009 D4/D5) — 이 struct 가 actor 의 state 면 여기 실린다.
    //   지금은 **파싱·검증·저장**만 한다(발명 0): 순차 배달은 이미 의미론적으로 옳고
    //   (LowentDRF.v), 바운드 async 큐(mailbox)와 슈퍼바이저(failure)는 **동시 배달**을
    //   전제하므로 그 런타임은 아직 없다 — 그 위에 얹을 자리를 먼저 정직하게 만든다.
    //   ★ 그래도 절이 **거짓이 아니게** 됐다: 틀린 형태(`bounded 0`·미지 정책)는 거절된다.
    bool                is_actor_state;   // 이 struct 가 actor 의 state 인가
    proven_u8           mailbox_kind;     // 0=미선언, 1=bounded, 2=unbounded
    proven_i64          mailbox_cap;      // bounded 의 용량 N (>0)
    proven_u8           failure_policy;   // 0=미선언, 1=restart max, 2=restart never, 3=restart always
    proven_i64          failure_max;      // `restart max N` 의 N (>0)
    proven_i64          failure_within;   // `within D <unit>` 를 초로 환산 (0=없음). 결정적 클럭이
                                          //   없어 이 창(window) 의 **강제는 아직** — 값만 싣는다.
} low_ir_struct_t;

// ★★ 파라미터 개수의 **한계**. 이 배열들이 8칸이었고, 9번째 파라미터의 `range` 는
//   **조용히 버려졌다** — 경계에서도, 호출 지점에서도 강제되지 않았다. 그런데 구간 분석은
//   지역 타입에서 그 범위를 **그대로 믿고** 검사를 지웠다(8-절단이 거기엔 없었으니까).
//   ⇒ 정확히 PRINCIPLES.md §0 교훈 1 의 조합이다: **믿는 코드 + 강제하지 않는 코드.**
//     VM 이 스스로를 고발해서(E-VM-ANALYSIS) 이것을 확인해 줬다.
//   이제 한계를 넘으면 **거절한다**(E-IR-ARITY). 강제 못 할 계약을 받아 주는 것보다 낫다.
//   ★ 2026-09-12 (WO-0204): 8 → 16 → **32**. 32 는 임의의 수가 아니라 **표현의 천장**이다 —
//     파라미터 종류표(`param_slice`·`param_cap` …)가 `proven_u32` 비트마스크라 33 번째 파라미터를
//     가리킬 비트가 없다. 그 위로 가려면 마스크가 64 비트가 되어야 한다. 그래서 아래에서 **못 박는다**:
//     한도를 손으로 더 올리면 컴파일이 거기서 멈춘다(조용히 비트를 흘리는 대신).
#define LOW_MAX_PARAMS LOW_HDR_MAXP   /* ★ 수를 베끼지 않는다 — 원본은 low_cst.h 하나다 */
// ★★★ 위의 수를 더 올리려면 **먼저 마스크를 넓혀라.** `param_slice`·`param_cap`·`param_flt` …
//   가 32 비트라, 한도만 올리면 33 번째 파라미터의 종류가 **조용히 사라진다**(그리고 구간 분석은
//   사라진 것을 «없다» 로 읽는다 — 정확히 교훈 1 의 모양). 여기서 멈추는 편이 낫다.
static_assert(LOW_MAX_PARAMS <= 32, "parameter kind masks are 32-bit (param_slice 등) — widen them first");
typedef struct { bool has_rng; proven_i64 rlo, rhi; } low_ir_prng_t;   // RFC-0055: 파라미터 범위
typedef struct {
    proven_u8str_view_t name;      // surface name (metadata only — not hashed)
    bool                is_calc;
    low_ir_prng_t       prng[LOW_MAX_PARAMS];   // 파라미터의 선언 범위(시그니처가 계약을 나른다)
    // ★ RFC-0008 §6.4 — 계약이 슬라이스 **길이**에 대해 말하는 상수들.
    //   `requires ge (len data) 4` · `errors E when lt (len data) 4` 의 4 가 그것이다.
    //   생성기가 그 **경계 길이**(3·4·5 …)를 흔든다. 길이가 흔들어야 할 축이다.
    proven_i64          lenk[8];   // 계약에 나온 길이 상수들
    proven_u8           nlenk;
    // ★★★ **계약이 길이의 하한을 말하면 오라클도 그것을 알아야 한다** (2026-07-30).
    //   길이 축은 "정직한 op 은 **어떤 길이에서도** 자기 선언을 어기지 않는다" 고 가정했다.
    //   그 가정은 계약이 길이를 제한하는 순간 틀린다: `requires ge (len s) 1` 인 op 에 빈
    //   슬라이스를 먹이면 **트랩해야 맞다**. 그 트랩을 실패로 세면 계약이 일할 때마다 게이트가
    //   붉어진다 — 실제로 그렇게 됐다(이 하한을 안 읽고 있었다).
    proven_i64          minlen;     // 계약이 요구하는 최소 길이(없으면 0)
    // ★★★★ **최대 길이도 계약이 말한다** (2026-09-06). 상계를 진입에서 강제하기 시작하자
    //   오라클이 곧바로 걸렸다: 길이 축은 `minlen` 만 알아서 **상계를 어긴 긴 슬라이스**를
    //   «계약이 허용한 입력» 이라 부르고, 그것이 트랩하니 **op 의 실패로 셌다**.
    //   ☞ *오라클이 못 보는 계약을 시험하면, 그 오라클은 프로그램을 틀렸다고 말한다*
    //     (같은 문장이 `low_iv.c` 의 명명 계약 자리에도 있다 — 두 번째다).
    //   없으면 0 = 상한 없음.
    proven_i64          maxlen;
    low_ir_prng_t       ptest[LOW_MAX_PARAMS];  // ★ RFC-0008 §6.4/Q6: 계약이 **허용하는** 파라미터 구간
                                   //   (타입 ∩ range ∩ requires). 경계값 테스트의 원천.
    low_ir_prng_t       ptype[LOW_MAX_PARAMS];  //   타입만의 구간 — 계약이 타입보다 좁은지 판정용.
    // ★★ R5 — **관계형 계약**(`requires le a b .`). 계약이 허용하는 입력이 매개변수마다
    //   독립적이지 **않다.** 오라클이 이걸 모르면 a > b 인 입력을 "적법" 이라 부르고 넣는다 →
    //   op 이 제 계약으로 트랩한다 → 오라클이 그것을 **결함으로 고발한다.**
    //   (그리고 실제로 고발했다 — 관계형 사실을 켜자마자 6 건. 오라클이 제 할 일을 했다.)
    //   하나의 뜻(requires)이 세 곳에서 읽힌다: 강제 · 구간 사실 · 오라클. **셋이 같아야 한다.**
    struct { proven_i8 a, b; proven_u8 strict; } prel[4];   // arg[a] ≤ arg[b] (strict → <)
    proven_u8           nprel;
    // ★★★ **«그 값만 아니면 된다» 도 계약이다** (2026-09-07, `ne-contract-false-oracle-failure`).
    //   구간은 **구멍을 못 낸다**: `requires ne m 0 .` 은 [0,MAX] 를 좁히지 못하므로 0 이 계속
    //   «허용» 으로 남았고, 진입 계약이 그것을 무는 것을 오라클이 *"계약이 스스로 모순"* 이라
    //   **실패로 고발했다**(강제는 옳고 분류가 틀렸다).
    //   ⇒ 구간 옆에 **금지된 점**을 따로 둔다. 구간이 못 담는 사실은 구간 밖에 적는다.
    //   ☞ *격자가 담지 못하는 사실을 격자에 우겨넣지 않는다 — 옆에 적고, 읽는 쪽이 함께 본다.*
    struct { proven_i8 p; proven_i64 v; } pne[4];           // arg[p] != v
    proven_u8           npne;
    // ★★★ **덧셈 사슬 계약** — `requires le (add (add a b) c) 64 .` (2026-09-07).
    //   진입에서 **강제하기 시작했으면** 오라클도 그것을 알아야 한다 — 모르면 계약을 어기는
    //   입력을 «적법» 이라 부르고 넣고, op 이 제 계약으로 멈추는 것을 **결함으로 고발한다**
    //   (실제로 `lib/budget.low` 에서 72 건이 났다. 강제만 넓히고 오라클을 안 넓힌 값이다).
    //   ☞ *하나의 뜻(`requires`)은 세 곳에서 읽힌다 — 강제 · 구간 사실 · 오라클. 셋이 같아야 한다.*
    struct { proven_i8 p[3]; proven_u8 n; proven_u8 cmp; proven_i64 k, bound; } psum[2];
    proven_u8           npsum;                             // sum(arg[p…]) + k  <cmp>  bound
    // ★★★ **`errors E when C .` 를 시그니처에 싣는다** (RFC-0053 §6.6 의 세 번째 쌍대).
    //   `requires` 는 계약을 callee 안으로 나르고, `ensures` 는 그것을 호출자에게 되돌린다.
    //   그런데 **`errors … when` 은 아무 데도 안 갔다** — ir_ctx 안에만 있고 def 에 안 실렸다.
    //   그래서 **호출자가 "이 op 은 언제 실패하는가" 를 알 수 없었다.**
    //
    //   호출자가 그 조건이 **거짓임을 증명하면** 그 호출은 **실패할 수 없다** —
    //   `try` 의 오류 가지는 **죽은 코드**다.
    //
    //   ★ 그리고 이것이 **`guard` 가 값을 하는 자리다.** `guard ¬C . else return error E .` 는
    //     `errors E when C .` 가 **정직함의 증명**이다(양방향으로 강제된다). 정직한 시그니처만이
    //     **호출자를 최적화한다.** guard 는 CFG 에 없는 정보를 **시그니처에 싣게** 해 준다.
    struct { proven_u8 cmp;      // IRW_LT/LE/GT/GE/EQ/NE — 0 = 이 형태가 아니다(증명 불가)
             proven_i8 p;        // 파라미터 번호
             proven_i64 n; } ewhen[8];
    proven_u8           newhen;  // when 이 달린 오류 수
    proven_u8           nerrv;   // errors 절이 이름 붙인 오류 **변형 수**
    low_ir_prng_t       eret;      // ★ RFC-0053 §6.6 dual: `ensures` 가 주는 **결과 범위**.
                                   //   requires 는 계약을 callee 안으로 나르고,
                                   //   ensures 는 그것을 **호출자에게 되돌려 준다.**
    proven_size_t       nparams;
    proven_u32          param_slice;   // bitmask: param i is slice-typed
    // ★★★ **`unsafe_ptr T`** (RFC-0066 가족) — C 로 넘길 **생 포인터**. 구조체의 바이트에 대한
    //   단일 포인터를 C 에 `void *` 로 넘긴다(길이 없음). unsafe: 레이아웃 일치는 사용자 책임이고
    //   이름이 그렇게 말한다. viewable 구조체만(바이트 레이아웃이 C 와 맞을 수 있는 것). sidx 는
    //   param_sidx 에 든다.
    proven_u32          param_uptr;    // bitmask: param i is `unsafe_ptr <struct>`
    // ★★★ **콜백 파라미터** (`unsafe_fn`, RFC-0066) — 씨에 함수 포인터로 넘어간다(void*, ABI 호환).
    //   값은 `unsafe_fn <op>`(export extern op 의 주소, IRW_FNREF). 경계 밖은 씨의 규율(unsafe).
    proven_u32          param_ufn;     // bitmask: param i is `unsafe_fn` (callback)
    // ★ f64 파라미터는 **CLI 로 줄 수가 없었다** — 정수만 읽었으니까. 그래서 float op 들은
    //   IEEE 바이트를 슬라이스로 밀어 넣어 우회했다. 그것은 실행이 아니라 회피다(교훈 4).
    proven_u32          param_flt;     // bitmask: param i is float-typed
    // ★★★ **`f32` 는 `f64` 가 아니다** (결함 노트 #83, 2026-09-16). `--run` 이 준 인자를 f64 그대로
    //   실으면 `f32` 파라미터가 **32 비트로 반올림되지 않은 값**을 받는다 — 같은 타입의 값이
    //   어디서 왔느냐에 따라 다른 수가 된다. 어느 파라미터가 32 비트인지 여기서 말한다.
    proven_u32          param_f32;     // bitmask: param i is f32 (not f64)
    // ★★★ **부호 없는 64 비트의 위쪽 절반은 i64 로 표현되지 않는다** (결함 노트 #19·#69).
    //   그래서 «타입의 범위» 를 i64 한 쌍으로 적은 경계 검사가 18446744073709551615 를
    //   거절했다 — 그 값은 `u64` 의 **정당한 값**이다. 그 자리만 검사를 건너뛴다.
    proven_u32          param_u64;     // bitmask: param i is u64 / usize
    // ★★★ **cstr 파라미터** (RFC-0068 S4 · C4) — extern 이 cstr 를 **입력**으로 받으면 C 로 `const
    //   char *` 단일 포인터로 넘어간다(길이 없음 — 널종단이다). 값은 포인터-크기 스칼라(u64).
    //   str_buf → cstr(sb_as_cstr)가 낸 널종단 버퍼를 C 의 char* 파라미터에 그대로 건넨다.
    proven_u32          param_cstr;     // bitmask: param i is `cstr` (raw char* to C)
    // ★ 구조체 파라미터. **내부 호출로는 넘어간다**(VM·네이티브 둘 다). 그런데 CLI 경계는
    //   그 모양을 표현하지 못해서, 구조체를 받는 op 은 **바깥에서 한 번도 실행된 적이 없었다**
    //   — 경계 범위 검사도, 계약 테스트도, 네이티브 차등도 그 자리를 못 봤다(교훈 4).
    //   경계에서는 **바이트를 받아 `view` 한다**(T1 무복사 — 길이·정렬을 그대로 검사한다).
    proven_u8str_view_t out_tyname;    // ★ 선언된 **출력 타입 이름** — 메서드 사슬의 수신자 타입
    // ★★★ **출력이 생 포인터 newtype 인가** (RFC-0068 S4 · C1) — `output cstr`(= newtype cstr
    //   unsafe_ptr u8) 처럼 extern 이 C 의 `char*` 를 돌려줄 때 참. 그러면 프로토타입 반환 타입이
    //   `const char *` 로 나가고(f64 가 double 로 나가는 것과 같은 정직함), 값은 포인터-크기
    //   스칼라로 받는다. VM 은 이 자리를 못 본다(extern 이 이미 E-VM-EXTERN).
    bool                out_ptr;
    proven_u8           ndropped;      // ★ 빌드 모드가 지운 이 op 의 계약 검사 수
    proven_u32          param_struct;  // bitmask: param i is a struct
    proven_u8           param_sidx[LOW_MAX_PARAMS];   // …그 구조체의 인덱스
    // ★ 슬라이스 파라미터의 **원소 폭**(비트). `slice u8` = 8 = **바이트 슬라이스**.
    //   백엔드의 빠른 경로가 이것을 묻는다: 바이트 슬라이스는 `(const unsigned char*, size_t)`
    //   로 내릴 수 있지만, `slice u32` 는 런타임 표현이 **다르다**(VARRAY). 모르면 **안 내린다.**
    proven_u8           param_ebits[LOW_MAX_PARAMS];
    // ★★★ **원소 술어의 구간** — `requires elem_lt s N` 이 심은 슬라이스 s 의 원소 [lo,hi].
    //   계약 오라클의 **길이 축**이 슬라이스를 채울 때 이 구간 안의 바이트를 써야 한다:
    //   안 그러면 도구가 계약을 어기는 내용을 먹여 놓고 트랩을 **프로그램의 죄로 고발한다**
    //   (PRINCIPLES.md §0 — 도구가 못 하는 것을 프로그램 탓으로 돌리지 않는다).
    proven_u32          param_elem_rng;                // bitmask: 원소 구간이 좁혀진 슬라이스 파라미터
    proven_i64          param_elo[LOW_MAX_PARAMS];     // 원소 하한 (타입 + requires elem_*)
    proven_i64          param_ehi[LOW_MAX_PARAMS];     // 원소 상한
    // ★ **구조체 슬라이스**(`slice pt`)의 원소 구조체 인덱스 (+1; 0 = 아님).
    //   구조체 배열은 시스템 코드의 기본 모양인데 하강이 못 만졌다 — 원소가 구조체라
    //   `param_ebits` 가 "모른다"(255)로 떨어졌기 때문이다.
    proven_u8           param_selem[LOW_MAX_PARAMS];
    // ★ **비트셋 파라미터** — IR 이 이것을 안 들고 있었다. 그래서 백엔드는 비트셋 인자를
    //   **평범한 정수**로 볼 수밖에 없었고, `mut bitset` 을 받는 op(BFS 의 visited 집합이
    //   정확히 그 모양이다)은 **전부 느린 경로**에 남았다.
    proven_u32          param_bset;
    // ★★★ **능력은 컴파일 시점의 권리다 — 값이 아니다.**
    //   C 로 넘기면 안 된다(C 는 그것을 뜻하는 게 없다). FFI 가 그 사실을 처음으로 물었다.
    proven_u32          param_cap;
    // ★★★ **이 op 이 어딘가에서 *참조* 를 인자로 받는가** (2026-07-19 — 참조가 op 경계를 넘는다).
    //   참조를 따라가는 것은 **런타임 검사**(프레임 세대 + 빌림 스택)에 기대는데, 빠른 경로에는
    //   그 추적이 **없다** — 거기서는 레코드가 정수 슬롯으로 펴지고 어댑터가 필드를 정수로 읽는다.
    //   ⇒ 참조를 받을 수 있는 op 은 **태그 경로에 남긴다.** 그래야 E-VM-EXCL/DANGLING 트랩이 산다.
    //   ☞ 이것이 *"생 포인터로 낮추면 양성은 돌지만 음성 트랩이 사라진다"* 에 대한 답이다:
    //     안 낮춘다. 속도를 포기하는 자리를 **이름으로** 표시한다.
    bool                takes_ref_arg;
    // ★★★ **option·result 파라미터** — (태그,값) 쌍이다. 빠른 경로의 ws[] 스택이 그 모양을
    //   이미 담지만, 파라미터로 받으려면 그 사실을 표시해야 한다(안 그러면 정수로 오인).
    proven_u32          param_opt;
    // ★★★ **벡터 파라미터**(vec t n) — 레인 배열이다. 원소 폭은 param_ebits, 레인 수는 여기.
    proven_u32          param_vec;
    proven_u8           param_vlanes[LOW_MAX_PARAMS];
    proven_size_t       nlocals;   // params + vars
    low_ir_ins_t       *code;
    proven_size_t       ncode;
    proven_u8           iface_hash[32];
    proven_u8           def_hash[32];
    // ★★★ **내용이 같은 def 은 한 몸이다** (RFC-0012 인스턴스 dedup). iface·def 해시가 모두 같으면
    //   그 둘은 **같은 정의**다 — 내용주소화가 이미 그렇게 말한다. 그런데 뒤끝은 이름으로 심볼을
    //   만들어 **본문을 두 번 방출**했다(단형화 인스턴스에서 특히: 같은 틀의 서로 다른 이름).
    //   여기 정본을 가리켜 두면 방출이 한 번으로 접힌다. NULL = 자기가 정본.
    void               *canon_of;   /* low_ir_def_t* — 무명 typedef 라 void* 로 든다 */
    bool                lowered;   // false → unsupported construct (E-IR-UNSUP)
    bool                is_test;   // ★ `test N do … end` — 러너가 돌린다(`--test`). release 에서 제거.
    // ★★★ **C 에 몸이 있다** (RFC-0063). 본문이 없고, VM 은 못 부르며, C 백엔드가 그것을 부른다.
    bool                is_extern;
    // ★ `export extern` — **C 가 우리를 부른다**(반대 방향). 몸은 **우리 것**이다.
    bool                is_export;
    // ★★★ **가변인자 C 함수** (RFC-0063 §5, 사용자 결정 2026-07-16) — `variadic` 마커. `printf` 류를
    //   부른다. 고정 인자 뒤에 **타입된 추가 인자**가 오고, 도구가 C 기본 인자 승격을 적용한다.
    //   ★ 방향은 한쪽뿐: **씨의** 가변인자 함수는 부르되, 씨가 우리 가변인자를 부를 순 없다(콜백 금지).
    bool                is_variadic;
    proven_u8str_view_t link_name;   // C 심볼(생략하면 op 이름)
    // ★★★ **어느 라이브러리에 있는가** — `link sin from m .` (2026-07-20).
    //   비어 있으면 "따로 링크할 것 없음"(libc 등 기본으로 딸려 오는 것).
    //   ☞ **새 낱말을 안 쓴다**: `from` 은 `use X from "path"` 가 이미 쓰는 문맥 낱말이다.
    proven_u8str_view_t link_lib;
    bool                is_actor;  // ★ actor 의 `on M` 핸들러 — 유일한 파라미터가 그 인스턴스다.
    // ★★★ R2 — `parallel <s> split .` · `reduce <acc> <op> .` (RFC-0009 DET-1/DET-3).
    //   LowentPar.v: 태스크들이 서로의 읽기/쓰기 집합을 안 건드리면 **병렬 = 순차**(비트 동일).
    //   그러면 **쪼개어 실행할 수 있다.** 그리고 그 순간 **새 오라클**이 생긴다:
    //     쪼갠 결과가 순차와 **비트 동일한가?** 아니면 `parallel` 선언이 **거짓말**이고
    //     정적 Bernstein 검사가 그것을 놓친 것이다.
    //   ★ 청크는 **부분 슬라이스**다 — 같은 버퍼를 가리키므로 쓰기가 그대로 반영된다.
    //     발명할 것이 없다.
    bool                is_par;      // `parallel <s> split` 이 선언됐다
    proven_u8           par_param;   // 그 슬라이스 파라미터의 인덱스
    proven_u8           red_op;      // 0=없음 1=add 2=mul 3=min 4=max  (reduce 절)
    // ★★★ RFC-0009 D6 — `schedule explore_interleavings [limit N]` 절이 선언됐다(test 전용).
    //   러너가 이 test 를 **K 개 정준 스케줄 순서**로 돌려 결과 불변을 요구한다(interleaving 오라클).
    bool                sched_explore;
    proven_u16          sched_limit;   // `limit N` (0=기본). A 는 K-정준 표본, B 가 N-열거로 대체.
    // ★★★ **ISR — 하드웨어가 부르는 진입** (RFC-0042 D5 · §8-4). `vector N .` / `priority P .`
    //   지금까지 이 절은 **검사만** 됐다(E-ISR-*). 그런데 검사만 해서는 부팅하지 않는다:
    //   Cortex-M 은 주소 0 의 **벡터 테이블**에서 핸들러 주소를 읽는다. 그 표가 없으면
    //   ISR 은 **선언됐지만 아무도 못 부르는 함수**다 — 선언이 장식이 된다.
    //   ⇒ 뒤끝이 표를 낸다(프리스탠딩 타깃에서만). -1 = ISR 이 아니다.
    proven_i32          isr_vector;
    proven_i32          isr_priority;   // -1 = 안 적음

    // ★★★ **이 op 이 어느 모듈에서 왔는가** (RFC-0060 재개, 2026-08-06 · 단계 U).
    //
    //   여태 def 은 **이름만** 들었다. 그래서 `X.op` 한정 참조가 와도 해소기는 `X` 를 떼고
    //   **맨이름으로** 찾았다 — 한정자가 **장식**이었다(ir_modqual_tail 의 주석 그대로:
    //   *"X 가 진짜 그 op 을 export 하는지는 검사층 소관"*). 그 대가가 평평한 이름공간이고,
    //   실측하면 **26 모듈 325 쌍 중 27 쌍이 한 단위에 못 들어간다**(check-lib-pairs).
    //   ⇒ 한정자를 **쓰이게** 하려면 def 이 자기 출처를 알아야 한다. 그 자리가 여기다.
    //   ★ 빈 view = 모듈 선언 밖(단일 파일 단위의 최상위). 그 경우는 여태처럼 맨이름이다.
    proven_u8str_view_t owner_mod;

    // ★★★ **지우기 계수는 def 마다 있어야 한다** (2026-08-27, RFC-0053 §8-9).
    //   전엔 파일 단위 합계만 있었다. 그런데 `--ir` 은 **딸려온 모듈까지** 낮추므로
    //   공유 라이브러리의 같은 자리가 **소비자 수만큼 다시 세어졌다**(실측: 남은 검사
    //   10,876 이 def 로 중복을 없애면 1,876 — 약 5.8 배). 수가 커서가 문제가 아니라
    //   **가중이 틀린 것**이 문제다: 널리 쓰이는 함수를 고치면 수가 크게 움직이고
    //   잎 함수는 같은 크기여도 거의 안 움직인다 — 계수가 일감의 우선순위를 왜곡했다.
    //   ⇒ def 은 자기 몫을 들고 다닌다. 세는 쪽은 **def 해시로 중복을 없앤다.**
    proven_size_t checks_proven, checks_total;

} low_ir_def_t;

// ★★★ **방출 자격은 한 번 계산되고 여러 번 읽힌다** (RFC-0089 R1 · 단계 C, 2026-08-03).
//
//   그 전에는 같은 판정이 **최소 세 곳에서 각자 재구성**됐다: 헤더 방출(`--emit-h`) ·
//   export 래퍼 방출 · 그 래퍼의 전방 선언. 조건이 한 자리에서만 바뀌면 나머지가 **갈린다** —
//   실측된 결과가 *"헤더가 오브젝트에 없는 심볼을 약속한다"* 였다(리뷰 B-01 ·
//   `docs/known-defects/emit-h-declares-undefined-symbol.md`). 헤더는 **약속**이므로,
//   그것을 믿고 링크한 쪽은 링크 시간에 죽는다.
//
//   ⇒ def 하나마다 **판정 기록 하나**. 백엔드가 한 번 채우고, 방출기는 **읽기만 한다.**
//   ☞ 여기 없는 칸(런타임 의존 `deps`·프리스탠딩 적합)은 **아직 읽는 사람이 없다** —
//     읽히지 않는 칸은 검사되지도 않으므로, 그것을 쓸 게이트가 생길 때 함께 온다
//     (RFC-0089 §5 의 규율: *"게이트 없는 조각은 이 RFC 에 넣지 않는다"*).
typedef struct {
    bool        valid;            // 이 기록이 채워졌는가(아니면 아무도 읽으면 안 된다)
    bool        lowered;          // 빠른 경로로 내릴 수 있는가        (cbe_mark_scalar 의 결과)
    bool        emittable;        // 몸을 C 로 낼 수 있는가            (하강됨 + 크기 한계)
    bool        exported_symbol;  // **외부 심볼이 실제로 나오는가**   (헤더가 약속해도 되는가)
    bool        ret_flt;          // 그 심볼의 반환이 double 인가       (아니면 long long)
    const char *why;              // export 인데 심볼이 안 나오면 **왜** (없으면 NULL)
} low_ir_plan_t;

typedef struct {
    low_ir_def_t       *defs;
    proven_size_t       ndefs;
    low_ir_make_t      *makes;     // make-literal sites
    proven_size_t       nmakes;
    proven_u8str_view_t *errs;     // enum variant names referenced by `error X`
    proven_size_t       nerrs;
    proven_u8str_view_t *fields;   // field names referenced by access/`field`
    proven_size_t       nfields;
    low_ir_struct_t     *structs;  // declared struct layouts (for view/try_view)
    proven_size_t       nstructs;
    proven_u8str_view_t *strs;     // string literals (byte content — **escapes DECODED**)
    proven_size_t       nstrs;
    // ★★ 리터럴의 **원소폭** 1·2·4 (RFC-0035 D5 접두 리터럴 — `"…"`/`u8"…"`=1 · `u"…"`=2 ·
    //    `U"…"`=4). `strs[i]` 는 그 원소들을 **호스트 바이트 순서**로 담는다: VM 은 호스트에서
    //    돌아 그대로 읽고, C 뒤끝은 **값으로 되읽어 원소 타입 배열로** 낸다.
    //    ☞ 바이트 순서를 리터럴에 **굽지 않는 이유**: 타깃 표에 `mips_be` 가 있다. 리틀엔디안
    //      바이트를 구우면 그 타깃에서 **조용히** 틀리고, VM 은 호스트 순서로 읽으므로
    //      VM ≢ native 가 된다 — 이 저장소가 게이트로 막고 있는 바로 그 부류다.
    proven_u8           *strew;

    // ★ 리터럴의 **값**을 담는 소유 저장소. `tok.lex` 는 **원문 구간**이라야 한다 —
    //   서식기가 그것을 그대로 다시 찍어 왕복을 지킨다(low_cst.c). 그래서 이스케이프
    //   디코드 결과는 여기 산다. 디코드는 **줄이기만** 하므로 원문 길이면 항상 넉넉하다.
    proven_u8            *strbuf;
    proven_size_t        strbuf_cap, strbuf_len;
    low_ir_asm_t       *asms;      // ★ 인라인 asm 블록 (RFC-0041)
    proven_size_t       nasms;
    proven_size_t       folds;         // RFC-0054: comptime 에 접힌 분기 수(비용 가시)
    proven_size_t       checks_proven; // RFC-0053: 구간 분석이 제거 가능으로 증명한 산술 검사
    proven_size_t       checks_total;  //           전체 산술 검사 자리
    proven_size_t       match_sites;   // RFC-0081 B1: match 자리 수(비용 가시)
    proven_size_t       match_arms;    //             전 match 의 arm 총합
    proven_size_t       match_worst;   //             한 선형 match 의 최악 순차 비교 수(선형 사슬 상한)
    proven_size_t       match_jt;      // RFC-0081 B2: 점프 테이블로 낮춘 match 수(O(1) 디스패치)
    proven_array_t      diags;     // of low_diag_t
    // ★ 명령이 가리키는 **파일 이름표** (REQ-0015): 0 번은 «모름» 이다(빈 이름).
    //   한 단위에 모듈 여럿이 링크되므로 줄만으로는 자리를 못 짚는다.
    const char         *files[64];
    proven_size_t       nfiles;
    bool                ok;
} low_ir_t;

// RFC-0054 — 타깃 조건부 컴파일. `machine.<prop>` 은 **comptime 상수**다(런타임 조회 아님).
// 빌드 타깃을 바꾸면 접히는 가지가 달라진다 — 그러나 **두 가지 모두 타입 검사**를 받는다.
// (그것이 #ifdef 와의 결정적 차이다: 검사되지 않는 코드는 썩는다 — PRINCIPLES.md §0.)
typedef struct {
    const char *name;      // x86_64 · arm64 · cortex_m · riscv64
    bool        big_endian;
    proven_u8   ptr_width; // 16 | 32 | 64
    bool        has_fpu;
    bool        no_heap;
    bool        no_float;
    proven_u8   simd_bytes; // 네이티브 벡터 레지스터 폭(바이트). 0 = SIMD 없음 → scalar fallback (native_lanes=1)
    // ★★★ **POSIX 인가** (2026-08-19, Windows 크로스 빌드). 지금까지 타깃 축은 *기계*만 말했다
    //   (엔디안·포인터 폭·FPU·힙·부동·SIMD). 그런데 방출 C 가 무엇을 include 할 수 있는가는
    //   기계가 아니라 **OS 면**이 정한다: `dirent.h`·`sys/socket.h`·`termios.h` 는 Windows 에
    //   없다. 그것을 `no_heap`(프리스탠딩)으로 대신 판정하면 *호스트인데 POSIX 는 아닌* 칸이
    //   생기지 않는다 — Windows 가 정확히 그 칸이다.
    //   ⇒ 축을 하나 더 둔다. `false` = 이 타깃에서는 POSIX 전용 잎을 **거절한다**(조용히 안 낸다).
    bool        posix;
    // ★★★★★ **슬라이스 길이의 상한** (2026-08-27, WO-0131 — 소유자 결정).
    //   전에는 구간 분석이 길이를 `[0, INT64_MAX]` 로 봤고, 그 hi 는 *"길이는 음수가
    //   아니다"* 라는 **하한의 자리 표시자**이지 상계가 아니었다. 그래서 `while lt i
    //   (len a)` 관용구의 누산기가 반복마다 넘침 검사를 달았다(코퍼스 100 곳).
    //   ⇒ 상한은 **주소공간에서 나온다**. 그러므로 `ptr_width` 와 같은 표에 산다:
    //     32 비트 기계의 슬라이스가 2^48 개일 수는 없다.
    //   ★ 사용자도 `machine.max_slice_len` 으로 **물어볼 수 있다**(comptime 상수, 비용 0).
    //     새 낱말을 만들지 않은 이유가 그것이다 — `machine.*` 가 이미 그 자리다.
    //   ☞ 조절은 **이 표 한 줄**이다. 그것이 소유자가 요구한 "차후 조절 가능" 이다.
    proven_i64  max_slice_len;
    // ★★★★★ **캐시라인과 페이지** (2026-08-28, RFC-0104 §8-7 — 소유자 결정 7-B).
    //   배치를 정하려면 두 수가 필요하다: *"어디까지가 한 줄인가"*(스레드별 카운터를 갈라
    //   놓는 자리)와 *"어디까지가 한 페이지인가"*(세그먼트·슬랩 크기를 고르는 자리).
    //   ★ 딱 **둘만** 둔다. `l1_size`·`cache_levels` 는 지금 **재는 자리가 없다** —
    //     표에 박힌 값이 실기와 달라도 아무도 안 운다. *재는 것만 이름을 받는다.*
    //   ☞ 그리고 이 수는 **hint 다**: 자동 repack 은 없다(RFC-0070). 프로그램이 물어보고
    //     **자기가** 배치를 적는다 — 그래야 비용이 소스에 보인다.
    //   ☞ 프리스탠딩(cortex_m)은 캐시도 MMU 도 없을 수 있다: `page_size = 0` 이 *"이 기계엔
    //     페이지가 없다"* 는 정직한 답이고, `cache_line` 은 **정렬 하한**(워드)으로 둔다.
    proven_u32  cache_line;
    proven_u32  page_size;
} low_target_t;
void low_ir_set_target(const char *name);      // 기본: x86_64 (호스트)
// ★★★ **SMT 백엔드를 끈다** (`--no-smt`) — 기능 스위치가 아니라 **측정 도구**다 (후속 M).
//   같은 바이너리로 켠 판과 끈 판을 재야 *"SMT 가 몇 개를 더 지웠나"* 가 **실측**이 된다.
//   ☞ 그리고 대조군은 게이트에도 쓰인다: 끈 판이 지운 것을 켠 판이 못 지우면 그것은 **회귀**다
//     (SMT 는 마지막 수단으로만 불리므로, 켜서 줄어드는 일은 있을 수 없다).
void low_ir_set_smt(bool on);
// ★ `--emit-smt` — 지운 자리마다 SMT-LIB2 질의를 낸다(외부 솔버와의 대조용).
void low_ir_set_smt_emit(bool on);

// ★★★ **빌드 모드** (RFC-0008 §6.5 — 이 RFC 의 "심장"). `build <mode> .` 이 정한다.
//   모드는 **어떤 등급의 계약 검사가 런타임에 남는지**를 고른다. 지금까지 이 선언은
//   **파싱만 되고 아무 데서도 강제되지 않았다** — 즉 **믿게 두고 아무것도 안 했다.**
const char   *low_irw_name(low_irw_t w);   // ★ 낱말의 이름 — 진단이 그것을 부를 수 있어야 한다
bool          low_ir_set_build_mode(proven_u8str_view_t m);   // 알 수 없는 모드면 false

// ★★★ **구성(configuration)** (RFC-0036 D5) — Kconfig 의 자리.
//   `build option smp bool default on .` 이 **손잡이**를 선언하고, `lowent.config`(생성물)가
//   **그 값을 고른다**. 그리고 `config smp` 는 **comptime 상수**다 ⇒ 꺼진 가지는 **코드에서
//   사라진다**(RFC-0054 D2 접기). `#ifdef` 와의 차이: **두 가지 모두 타입 검사를 받는다.**
void          low_ir_config_reset(void);
void          low_ir_config_add(const char *name, const char *value);
proven_size_t low_ir_config_count(void);
proven_size_t low_ir_build_dropped(void);   // ★ 모드가 **지운 검사 수** — 조용히 지우지 않는다
const low_target_t *low_ir_target(void);
// ★ 타깃 이름은 **닫힌 집합**이다(표가 그 권위다). `asm <타깃>` 절이 이것으로 검사된다 —
//   모르는 이름을 통과시키면 그 asm 은 **어느 빌드에서도 안 맞고**, 조용히 빠지거나
//   엉뚱한 빌드에 실린다(둘 다 이 RFC 가 막으려던 것).
bool low_ir_target_known(proven_u8str_view_t name);

[[nodiscard]] low_ir_t low_ir_build(proven_allocator_t work, const low_parse_result_t *pr);
// ★ 이름이 빌트인과 겹치면 그 선언은 **영원히 호출되지 않는다**(해석기가 빌트인을 고른다).
bool low_ir_is_builtin_name(proven_u8str_view_t name);
void low_ir_dump(const low_ir_t *ir);
// ★★★ **`.lowdb` 사이드파일** (RFC-0012) — 내용주소 해시를 **밖으로 낸다**.
//   해시는 여태 계산만 되고 **버려졌다**: 다음 실행이 이전 실행과 대조할 방법이 없었으므로
//   *"변한 op 만 다시 짓는다"* 는 증분 빌드의 전제가 **검사 불가능**했다.
int low_ir_emit_db(const low_ir_t *ir, FILE *out);
// ★ 증명 운반 검사(RFC-0086): 지운 검사마다 **규칙 id + 쓴 수**를 낸다.
int low_ir_emit_proof(const low_ir_t *ir, FILE *out);
// ★ 저자를 향한 경고(W-CONTRACT-IGNORED 따위)를 켠다 — `--check`·`--ir` 만.
void low_ir_set_author_warnings(bool on);

void low_ir_free(proven_allocator_t work, low_ir_t *ir);

// Run an op. CLI mapping: ints are positional args; if the op has exactly one
// param and it is slice-typed, all ints become that byte slice. `text` is the
// rendered result value (`120`, `ok {version 1, flags 7, length 4}`, `err too_short`).
typedef struct { proven_i64 value; bool ok; char text[160]; } low_ir_run_result_t;
// ★ **오라클 전용 걸음 예산** (2026-07-30). 0 = 무한(사용자 실행의 기본).
//   계약 오라클은 op 을 실제로 돌린다. 반복 횟수가 입력인 op 에 경계값(2^64−1)을 먹이면
//   그 실행이 끝나지 않아 **도구가 멈춘다**(실측). 예산을 넘긴 케이스는 건너뛰고 **센다**.
void low_ir_set_run_budget(proven_u64 steps);
proven_u64 low_ir_run_budget_hits(void);
proven_u64 low_ir_run_steps_max_ok(void);
proven_u64 low_ir_iv_stack_full(void);
// ★ 호스트 잎이 요구하는 **권한의 종류**(없으면 0) — 검사기가 이 표를 빌려 쓴다(RFC-0077 §P1-2).
const char *low_ir_leaf_cap_kind(proven_u8str_view_t name);   // 구간 분석이 스택 한계로 포기한 횟수
void low_ir_reset_steps_max_ok(void);

[[nodiscard]] low_ir_run_result_t low_ir_run(const low_ir_t *ir, proven_u8str_view_t op,
                                             const proven_i64 *args, proven_size_t nargs,
                                             proven_allocator_t work, proven_array_t *diags);

// ★ 인자의 **모양**을 명시하는 형태. 위의 평평한 매핑은 슬라이스가 **맨 끝에 하나** 일 때만
//   통한다 — 나머지 정수를 전부 그 슬라이스로 삼기 때문이다. 그래서 슬라이스가 둘인 op,
//   즉 `encode(src, mut dst)` 같은 **모든 쓰기 op** 는 아예 실행할 수가 없었다.
//   (도구가 못 하는 모양의 프로그램은 **한 번도 실행되지 않는다** — 그러면 그 자리의 결함은
//    아무도 못 본다. 그래서 이것은 편의 문제가 아니다.)
//   bytes 는 **호출자 소유이고 쓰기 가능**하다: mut 슬라이스로 쓴 값이 그대로 비쳐 보인다.
typedef struct {
    bool          is_slice;
    bool          is_flt;   // 부동소수 스칼라(v 에 IEEE-754 비트)
    proven_i64    v;        // 스칼라(정수값 또는 f64 비트)
    proven_u8    *bytes;    // 슬라이스 저장소(호출자 소유 · 가변)
    proven_size_t n;
} low_ir_arg_t;
[[nodiscard]] low_ir_run_result_t low_ir_run_argv(const low_ir_t *ir, proven_u8str_view_t op,
                                                  const low_ir_arg_t *args, proven_size_t nargs,
                                                  proven_allocator_t work, proven_array_t *diags,
                                                  const proven_u8str_view_t *pargs, proven_size_t npargs);

// ★★ **테스트 러너** — `test N do … end` 은 **죽은 문법이었다**(파싱되고 절대 실행 안 됨).
//   초록불이 "검사됐다" 가 아니라 **"아무도 안 돌렸다"** 였다. 그것이 이 언어가 금지하는 거짓말이다.
//   이제 돈다: 각 test 를 op 처럼 실행하고, `expect` 가 거짓이면 **실패**다(E-TEST-FAIL).
typedef struct { proven_size_t total, passed, failed; } low_ir_test_result_t;
[[nodiscard]] low_ir_test_result_t low_ir_run_tests(const low_ir_t *ir, proven_allocator_t work,
                                                    bool verbose);

// ★★★ R2 — **쪼개어 실행하고, 순차와 비교한다** (RFC-0009 DET-1).
//   LowentPar.v(Qed): 태스크가 서로의 읽기/쓰기 집합을 안 건드리면 **병렬 = 순차**(비트 동일).
//   그러니 `parallel s split` 을 **진짜로 쪼갠다** — 청크는 **부분 슬라이스**다(같은 버퍼).
//   그리고 그 순간 **오라클**이 생긴다: 쪼갠 결과가 순차와 **비트 동일한가?**
//     · 버퍼가 다르면  → `parallel` 선언이 **거짓말**이다(쓰기가 겹친다)
//     · reduce 값이 다르면 → 연산이 **결합적이지 않다**(DET-3)
//   정적 Bernstein 검사가 놓친 것을 **런타임이 잡는다.** VM 의 자기 고발과 같은 자리다.
typedef struct {
    proven_size_t ops;        // parallel 을 선언한 op 수
    proven_size_t splits;     // 시험한 쪼개기 수 (K = 1,2,3,n · 정순·역순)
    proven_size_t failures;   // ★ 순차와 달랐다 — 0 이어야 한다
} low_ir_par_t;
[[nodiscard]] low_ir_par_t low_ir_par_check(const low_ir_t *ir, proven_allocator_t work,
                                            bool verbose);

// ── RFC-0008 §6.4 (미해결 Q6) — 계약에서 테스트를 뽑는다 ──────────────────────
// "requires ge length 2 → 0 1 2 3 을 어떤 알고리즘이 만드는가?"
//
// 답: **경계값**이다. 계약이 구간 [lo,hi] 를 주면 lo-1 · lo · lo+1 · hi-1 · hi · hi+1 을 만든다.
// 그리고 계약은 **오라클도 공짜로 준다** — 기대 출력이 필요 없다:
//     구간 **안**의 값 → 계약 위반으로 트랩하면 **안 된다** (제 계약이 허용한 입력을 거부)
//     구간 **밖**의 값 → 계약 위반으로 **반드시** 트랩해야 한다 (안 하면 검사가 잘못 제거됐다)
// 그래서 이 생성기는 **자기 검증**된다. 그것이 이 기능의 값어치다.
typedef struct {
    proven_size_t cases;      // 생성·실행된 경우의 수
    proven_size_t admitted;   // 계약이 허용하는 입력
    proven_size_t rejected;   // 계약이 거부하는 입력
    proven_size_t failures;   // ★ 오라클 위반 — 0 이어야 한다
    proven_size_t ops;        // 계약에서 테스트를 뽑을 수 있었던 op 수
    proven_size_t skipped;    // 계약이 없어 뽑을 수 없었던 op 수 (조용히 넘기지 않는다)
    // ★★★ **이건 다른 이유다. 다르게 센다.**
    //   값 축은 **스칼라만** 만든다. 구조체·슬라이스 파라미터에는 `0`/`1` 을 넣고 있었고,
    //   VM 은 그것을 **쓰레기 핸들**로 읽고 트랩했다 — 그러면 오라클이 *"계약이 감당 못 할
    //   입력을 허용한다"* 고 **프로그램을 고발했다. 오진이다.**
    //   ⇒ **도구가 못 하는 것을 프로그램의 죄로 돌리지 않는다**(PRINCIPLES.md §0 교훈 5).
    proven_size_t unbuildable;  // 인자를 **만들 수 없어서** 못 뽑은 op 수
    // ★★★ **빌드 모드가 검사를 지운 op** — 오라클은 그것을 **검증할 수 없다.**
    //   그리고 그것은 **프로그램의 죄가 아니다**: 모드가 그렇게 하라고 했다.
    //   오라클은 **계약**을 검증하지 **빌드**를 검증하지 않는다 ⇒ `debug` 로 검증하라고 말한다.
    proven_size_t mode_dropped;

    proven_size_t admitted_traps;  // 계약이 허용했는데 본문이 넘친 경우 (경고 — 실패는 아니다)
    // ★★★★★ **오라클이 인정 조건을 못 세운 자리** (2026-08-30, WO-0151).
    //   값 축은 스칼라만 짓고, 구조체 인자는 **모든 바이트를 같은 값으로** 채운다.
    //   그러면 `requires lt (field s w) 63` 같은 **필드 경로 계약**은 만족시킬 수가 없다 —
    //   오라클이 만든 입력을 op 의 계약이 거절하는 것은 **op 의 잘못이 아니라 오라클의
    //   한계**다. 전엔 그것을 실패로 세어 **거짓 고발**을 했고, 그 고발 때문에 사람이
    //   *"계약을 쓰지 말자"* 로 가게 됐다(그것이 진짜 손해다).
    //   ⇒ 실패가 아니라 **못 잰 것**으로 센다. 그리고 그 수를 **말한다**.
    //   ☞ *못 재는 것을 실패라고 부르면, 고쳐야 할 것은 도구인데 프로그램을 고치게 된다.*
    proven_size_t unmodelled;      // 인정 조건을 못 세운 케이스(구조체 필드 경로 계약)
    proven_size_t len_cases;  // 슬라이스 **길이** 축에서 생성된 경우
    // ★★★ **모양 축** — 값 축은 슬라이스가 있으면 건너뛰고, 길이 축은 슬라이스가 **정확히 하나**
    //   일 때만 돈다. 그래서 **슬라이스가 둘인 op**(= `encode(src, mut dst)` — 모든 쓰기 op 의
    //   모양)이 오라클 **밖**에 있었다. 이번 세션에 겨우 **실행 가능해진** 모양인데,
    //   그러자마자 **검증 밖**에 남아 있었다.
    //   ⇒ 파라미터를 **하나씩** 경계로 흔든다(나머지는 기준값). 모든 모양을 덮는다.
    proven_size_t shape_cases;
    // ★ 걸음 예산에 걸려 **건너뛴** 경우 — 조용히 넘기지 않는다(반복 횟수가 입력인 op 이 그렇다).
    proven_size_t budget_skips;
    // ★★ **바깥 세계에 닿는 op 은 오라클이 돌리지 않는다** (X-0041 ⓐ, 2026-09-26).
    //   망·파일 잎(`net_*`·`file_*`·`dir_*`·`path_*`)에 직접 또는 부르는 op 을 거쳐 닿는 op 에
    //   경계 인자를 먹이면 **진짜로** 포트를 열고 파일을 만든다 — 재는 도구가 기계의 상태를 바꾸고,
    //   그 실패가 **남의 구획**에서 나타났다. 계약 검사는 이 op 들에 대해서도 번역 때 그대로 선다.
    //   건너뛴 op 의 수를 **센다**(말하지 않고 빼면 덜 검사한 것이 안 보인다).
    proven_size_t world_skips;
    proven_size_t len_dropped;   // ★ 표가 차서 못 만든 길이 케이스(RFC-0077 P1-5)
    proven_u64    steps_max_ok;   // ★ 완주한 케이스가 쓴 **최대 걸음**(여유의 증인)
    proven_u64    step_budget;    // 그때의 예산
} low_ir_ctest_t;

// verbose 면 각 op 의 결과를 stdout 에 적는다.
[[nodiscard]] low_ir_ctest_t low_ir_contract_tests(const low_ir_t *ir, proven_allocator_t work,
                                                   bool verbose);

#endif // LOW_IR_H
