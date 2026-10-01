#ifndef LOW_IR_PRIV_H
#define LOW_IR_PRIV_H
/* low_ir_priv.h — `low_ir.c` · `low_vm.c` · `low_iv.c` 가 **나눠 갖는 것만** 적는다
 * (WO-0163 · WO-0164 · X-0011).
 *
 * ★ 공개 표면이 아니다. 밖에서 include 하지 말 것 — 여기 있는 것은 *"한 파일이었을 때
 *   그냥 보이던 것"* 이고, 쪼개면서 **이름을 붙여 준 것**뿐이다.
 *
 * ★★ 쪼개는 선은 줄 수가 아니라 **넘나드는 심볼 수**가 정했다(실측 2026-08-31):
 *       VM       4,101 줄 · 나가는 1 · 들여오는 11   ← 첫 조각(WO-0163)
 *       구간분석  4,809 줄 · 나가는 0 · 들여오는 43   ← 둘째 조각(WO-0164)
 *       하강      6,101 줄 · 나가는 44
 *   ☞ *가르는 선은 크기가 아니라 얼마나 적게 이야기하는가다.*
 *
 * ★★★ **그런데 「적게」의 뜻을 둘째 조각이 정교하게 했다.** 들여오는 43 을 뜯어 보니
 *   두 종류였다:
 *     · **공용 어휘** — `veq` 106 회 · `is_atom` 109 회 · `ir_fail` 24 회 …
 *       어느 조각을 떼든 따라온다. 이것이 길어지는 것은 **선이 나쁜 것이 아니다.**
 *     · **진짜 결합** — op 표(`g_opt`·`g_cfg`) · 계약 진입 · 하강 진입.
 *       **이것이 길어지면 선이 잘못된 것이다.**
 *   그래서 아래를 두 절로 나누어 적는다. 늘리기 전에 어느 절인지부터 묻는다.
 */
#include "low_ir.h"
#include "low_smt.h"   /* ★ smt_emit_query 가 이 타입을 받는다 */

/* ★ 지역 칸 수 — 하강이 세고 VM 이 그만큼 잡는다. **한 수가 두 파일에 걸리는 자리**라
 *   여기에 둔다(양쪽에 따로 적으면 갈린다 — 이 저장소가 여러 번 앓은 병이다). */
#define IR_MAXLOCALS 64



/* ── 나눠 갖는 상한 (WO-0164) — 맥락 구조체의 칸 수를 정한다 ────────────────
 * ★ 이 수들은 `ir_ctx_t` 의 배열 크기라 **타입과 함께 살아야** 한다. */
#define IR_MAXALIAS  32
#define IR_MAXENUMPF 4     // max payload fields per enum variant (RFC-0080; AST `add` needs 2)
#define IR_MAXENUMV  64
#define IR_MAXERRS   64
#define IR_MAXLOOP   16
#define IR_MAXMAKES  64
/* ★ 256 → 512 (2026-09-18, X-0032). `lowget` 이 TLS·암호·인증서·신뢰 저장소를 한 단위로
 *   묶으면서 op 이 256 을 넘었다. 도구는 **정직하게 거절했다**(E-IR-LIMIT) — 그것이 이 수가
 *   있는 까닭이고, 올릴 때도 그 규율은 그대로다. 이 표는 컴파일러 안의 자리일 뿐이고
 *   프로그램이 쓰는 메모리와 무관하다. 타입 검사 쪽 짝은 `TC_MAXSIG`(같은 날 512 로). */
#define IR_MAXOPS    512
#define IR_MAXPATCH  64
#define IR_MAXOPT 64
#define IR_MAXSTRS   256
#define IR_STRBUF    (256 * 1024)   // ★ 한 단위의 리터럴 **값** 총량(디코드 후). 넘으면 거절한다.











#define IR_CERT_EXT_MAX 65535
#define IV_MAXFACT 8

#define IR_COND_MAXBRZ 8

#define IR_CTFE_FUEL 4096

#define IR_MAXCOND   32

#define IR_MAXREGION 8

/* ── 나눠 갖는 타입 (WO-0164) ─────────────────────────────────────────────
 * 구간분석(`low_iv.c`)이 맥락과 op 표를 본다. 셋뿐이고, 셋 다 **읽기 위해** 필요하다.
 * ★ `ir_ctx_t` 는 115 줄이지만 구간분석의 `c->` 접근은 **32 회**뿐이다 —
 *   맥락이 크다고 결합이 큰 것은 아니다. */
typedef enum { IR_BM_DEBUG = 0, IR_BM_TEST, IR_BM_RELEASE_SAFE, IR_BM_RELEASE_FAST, IR_BM_AUDIT } ir_bmode_t;

typedef struct {
    proven_u8str_view_t def;      // 어느 op 인가
    proven_u32          pc;       // 그 op 안의 명령 위치
    low_irw_t           w;        // 어떤 옵코드의 검사를 지웠나
    const char         *rule;     // 규칙 id — 검증기가 이것으로 분기한다
    proven_i64          f[6];     // 그 규칙이 **실제로 쓴 수**
    proven_u8           nf;
    // ★★★ **가변 길이 증명서** (후속 M). SMT 규칙의 근거는 *수 여섯 개*로 안 담긴다 —
    //   **계 전체**(제약들의 계수와 상수)와 **Farkas 계수**를 다 실어야 검증기가 곱해 더해
    //   재현할 수 있다. ☞ 안 실으면 그 줄은 *"솔버가 그렇다더라"* 가 되고, 그것이 정확히
    //   이 저장소가 SMT 를 망설였던 이유다(네 번째 권위).
    proven_u16          ext_at, ext_n;      // g_cert_ext 안의 위치·길이
} ir_cert_t;

typedef struct { char name[48], val[48]; } ir_cfg_t;

typedef struct { low_irw_t w; proven_i64 n; proven_u8str_view_t rhs; bool is_lit; } ir_ens_t;

typedef struct {
    bool known; proven_u8 bits; bool sign; bool flt;
    bool has_rng; proven_i64 rlo, rhi;   // RFC-0055: 선언된 **범위**(타입이 곧 계약)
} ityp_t;

// ★★★★ `elem` — **슬라이스 원소의 타입** (2026-09-09, REQ-0013).
//   슬라이스 자체는 스칼라가 아니라 `ty` 가 «미상» 이다. 그런데 `index s i` 의 **결과**는
//   스칼라이고, 그 부호를 모르면 순서 비교가 **부호 있는 비교**로 내려간다 —
//   그래서 `lt (index s 0) (index s 1)` 이 u64 의 큰 값을 음수처럼 다뤄
//   **VM 과 네이티브가 사이좋게 틀린 답**을 냈다(둘 다 틀리니 차등 훑기도 못 잡는다).
//   ⇒ 원소의 타입을 따로 들고, `index` 가 그것을 결과로 민다.
typedef struct { proven_u8str_view_t name; ityp_t ty; proven_u8str_view_t tyname; proven_u8 bset_w;
                 proven_u8str_view_t narrowed; ityp_t elem; } ir_local_t;

typedef struct {
    proven_size_t brk[IR_MAXPATCH]; proven_size_t nbrk;   // break → loop end
    proven_size_t cnt[IR_MAXPATCH]; proven_size_t ncnt;   // continue → cond (while) / step (for)
    proven_size_t rgdepth;                                 // ★ 루프에 들어설 때 열려 있던 영역 수(RFC-0112 D5(5))
    proven_size_t reldepth;                                // ★ 루프에 들어설 때 걸려 있던 돌려주기 수(RFC-0135 S2)
} ir_loop_t;

typedef struct {
    proven_u8           *dropped_out;   // ★ 이 def 에서 모드가 지운 검사 수를 적는 자리
    proven_allocator_t   work;
    low_ir_t            *out;
    // module-level knowledge
    proven_u8str_view_t  enumv[IR_MAXENUMV];      // all enum variant names
    proven_size_t        nenumv;
    // ★ 선언된 모듈 이름들 (RFC-0011 한정 정규화) — 타입 자리의 `M.Type` 를 bare `Type` 로 좁힐 때 쓴다.
    //   타입 이름은 빌트인과 안 겹치므로 좁혀도 안전(op 이름은 ir_modqual_tail 로 별도 처리 — get/len 충돌 회피).
    proven_u8str_view_t  modnames[256];
    proven_size_t        nmodnames;
    // ★★★ **별칭 → 모듈** (RFC-0060 재개 · 단계 U 슬라이스 ②). `use X from "…" .` 는 이름 X 를
    //   묶고, `use X … as Y .` 는 Y 를 묶는다. 한정 참조 `Y.op` 를 풀려면 **묶인 이름이 어느
    //   모듈인지**를 IR 이 알아야 한다 — 여태 IR 은 `use` 를 아예 안 읽었고, 그래서 한정자를
    //   떼고 맨이름으로 찾았다(한정자가 장식이었다).
    proven_u8str_view_t  usebind[256];   // 묶인 이름(별칭이 있으면 별칭)
    proven_u8str_view_t  usemod[256];    // 그 이름이 가리키는 모듈
    proven_size_t        nusebind;
    // ★★★ **페이로드 enum(합타입)** (RFC-0080) — 각 변형이 페이로드 필드를 가질 수 있다.
    //   현행 페이로드 없는 enum = 특수형(np==0)이라 뒤로 호환. option/result 를 사용자 선언으로
    //   여는 일반화(§2). 재귀는 간접 강제(owned 또는 인덱스, §4.2) — 직접 자기포함은 무한 크기.
    proven_u8str_view_t  enum_owner[IR_MAXENUMV];        // 이 변형이 속한 enum 타입 이름
    proven_u8            enum_np[IR_MAXENUMV];           // 페이로드 필드 수 (0 = 페이로드 없음)
    proven_u8str_view_t  enum_pname[IR_MAXENUMV][IR_MAXENUMPF];  // 페이로드 필드 이름
    proven_u8str_view_t  enum_pty[IR_MAXENUMV][IR_MAXENUMPF];    // 페이로드 필드 타입 이름
    // ★★★ **모듈 수준 `let` = 이름 붙은 상수** (2026-07-14).
    //   명세는 내내 이것을 요구했다(SPEC-002 §276: "읽기전용 지역 상수는 모듈 const").
    //   **그런데 `const` 는 어휘에도 구현에도 없었다.** 그래서 실제 코드가 **같은 숫자를 반복했다**:
    //       errors too_short lt (len data) 4 .     ← 4
    //       guard ge (len data) 4 . else …         ← 4    **같은 사실, 두 번**
    //   새 낱말은 필요 없다 — **`let` 이 이미 "불변" 이다.** 모듈 수준에서 되게 하면 그만이다
    //   (`const` 를 더하면 `let` 의 **동의어**가 된다 — §2.5 위반).
    //   초기값은 **comptime 이어야** 한다 ⇒ 값이 **그 자리에 박힌다**(상수 접기).
    proven_u8str_view_t  mconst[IR_MAXALIAS];
    proven_i64           mconstv[IR_MAXALIAS];
    ityp_t               mconstt[IR_MAXALIAS];
    proven_size_t        nmconst;
    proven_u8str_view_t  slice_alias[IR_MAXALIAS]; // `type N slice/array …`
    // ★ 별칭의 **원소 폭**. 없으면 백엔드가 "모른다" 로 떨어져 **별칭을 쓴 op 은 전부**
    //   느린 경로에 남는다 — 그리고 별칭은 이 언어에서 **권장되는 모양**이다.
    proven_u8           alias_ebits[IR_MAXALIAS];
    // ★★ **스칼라 타입 별칭** — 이게 없었다. IR 은 **슬라이스 별칭만** 등록했다.
    //   그래서 `type h u8 .` 뒤의 `input a h .` 는 타입이 **미상**이 됐고, 미상이면 **검사가
    //   하나도 안 붙는다.** 실측: `output u8` 인 op 이 **400 을 반환했다.** 조용히 틀린 답이다.
    //   그리고 `type h range 0 10 .` 은 range 계약이 **증발했다**(직접 쓰면 경계에서 트랩한다).
    proven_u8str_view_t  ty_alias[IR_MAXALIAS];
    ityp_t               ty_alias_ty[IR_MAXALIAS];
    proven_size_t        nty_alias;
    proven_size_t        nalias;
    // ★★★ **생 포인터 newtype 의 이름들** (RFC-0068 S4) — `newtype cstr unsafe_ptr u8` 처럼 선언된
    //   이름. extern 의 `output <이 이름>` 은 C 의 `char*` 를 돌려주므로 프로토타입이 `const char *`
    //   로 나가야 한다(out_ptr). 표현은 ty_alias 로 u64(위)지만, "포인터다" 는 사실은 별도로 든다.
    proven_u8str_view_t  ptr_alias[IR_MAXALIAS];
    proven_size_t        nptr_alias;
    // ★ level 2 — 지금 낮추는 것이 **actor 핸들러**라면, 상태 필드 이름들이 여기 있다.
    //   상태는 **레코드**다(슬롯 0 = 그 인스턴스). 맨 이름이 상태 필드면 `field self N` 이다.
    //   ⇒ 한 번에 한 메시지만 처리하므로(그것이 actor 모델이다) 이 접근은 **격리**돼 있다.
    //     LowentDRF.v: 소유가 격리되고 이전이 메시지로만 일어나면 **경합이 없다**(Qed).
    proven_u8str_view_t  sfield[IR_MAKE_MAXF];
    proven_u8           sfcap[IR_MAKE_MAXF];   // ★ 그 칸의 권한 종류(low_ir_sfield_t.capkind) — RFC-0112 D6
    proven_size_t        nsfield;
    // per-def state
    proven_array_t       code;              // of low_ir_ins_t
    ir_local_t           locals[IR_MAXLOCALS];
    proven_size_t        nlocals;
    ir_loop_t            loops[IR_MAXLOOP];
    proven_size_t        nloops;
    ityp_t               tstk[64];          // 로워링 시점의 타입 그림자 스택
    proven_size_t        tsp;
    bool                 tstk_bad;          // 스택이 어긋나면 추적을 포기한다(보수적)
    proven_u8            vec_lanes, vec_esz;   // comptime vector context (binding type)
    // ★ **비트셋 폭 컨텍스트** — `var c be bitset 8 complement a .` 의 8. 여집합은 폭이 있어야
    //   옳다(bitset 8 의 여집합이 상위 56 비트를 켜면 안 된다). 폭을 **하강 시점에 박아** 두 백엔드가
    //   같은 마스크를 쓰게 한다 — 그러지 않으면 VM(폭을 든다)과 네이티브(폭 없는 워드)가 갈린다.
    proven_u8            bset_w;
    bool                 vec_flt;
    bool                 vec_sign;   // ★ 벡터 레인이 **부호형**인가 (vec iN) — 레인 의미가 달라진다
    bool                 failed;
    // ★ 섬 안인가 — 단항 진단(E-EXPR-UNARY)은 섬에서만 참말이다. 섬 밖의
    //   맨 `- 5` 는 리터럴이 안 붙은 것이지 "섬에 단항이 없다" 가 아니다.
    proven_size_t        island;
    // ★ ensures — 출구 계약. 반환값을 담을 숨은 지역과 술어들.
    ir_ens_t             ens[4];
    proven_u8            nens;
    proven_size_t        ret_slot;
    // ★ errors … when — 오류를 낼 때 **그 조건이 실제로 참인지** 검사한다.
    //   지금까지 when 조건은 **한 번도 평가되지 않았다** — 본문과 정반대로 적어도 통과했다.
    //   선언은 검사되지 않으면 거짓말이 된다(PRINCIPLES.md §0).
    const low_cst_t     *def_form;                  // 계약 절을 담은 op form
    const low_parse_result_t *pr;                   // ★ 단형화 유래(사용 자리)를 읽으려고
    proven_u8str_view_t  def_name;                  // 지금 낮추는 def 이름(인스턴스면 `#` 를 담는다)
    // ★★★★★ 지금 낮추는 def 의 **소유 모듈**. 맨이름 해소가 이것을 먼저 본다 —
    //   같은 모듈의 정의가 있으면 그것이 답이고, 없을 때만 다른 모듈로 넘어간다.
    //   ☞ 없을 때 무슨 일이 났는지: `utf8` 과 `utf16` 이 둘 다 `next` 를 export 하는데
    //     `utf16.count_chars` 가 자기 `next` 를 맨이름으로 부르면 **먼저 선언된 모듈의**
    //     `next` 에 묶였다. **선언 순서가 답을 바꿨다**(실측 106 ↔ 107).
    proven_u8str_view_t  def_owner;
    struct { proven_u8str_view_t name; proven_size_t ws, we; } ewhen[8];
    proven_u8            newhen;
    bool                 in_tgroup;   // ★★★ task_group 본문 안인가 — `spawn <op>`=태스크(SC1, RFC-0009 D3-b)
    bool                 cur_cancel;  // ★★★ 지금 **취소 스코프** 안인가(cancel_on_error) — 중첩 save/restore 용
    // ★★★★★ **comptime 로 접힌 `let` 정수 바인딩** (2026-08-18, RFC-0040 D5 · RFC-0021).
    //   `vec u32 (native_lanes u32)` 는 됐지만 `let L be native_lanes u32 . … vec u32 L` 은 안 됐다.
    //   그래서 이식 가능한 커널이 폭을 **이름 하나로** 못 적고 같은 식을 자리마다 되풀이했다
    //   (D5 가 약속한 것이 정확히 그 반대다).
    //   ★ **`let` 만** 담는다 — `var` 는 나중에 `set` 으로 바뀔 수 있고, 그러면 타입 자리에
    //     실린 상수가 낡는다. 불변인 것만 타입이 될 수 있다.
    struct { proven_u8str_view_t name; proven_i64 val; } cint[16];
    proven_size_t        ncint;

    // ★ 이전 기간 동안만 켠다(`LOWENT_ALLOW_GLUED_FIELD=1`). 코퍼스를 다 옮기면 없앤다.
    bool allow_glued_field;
    // ★ 지금 낮추고 있는 form — **수리 id 를 원인별로 고르려고** 든다(2026-08-26).
    //   `in_tgroup`·`cur_cancel` 과 같은 자리다: 문맥은 여기 담는다.
    const low_cst_t     *cur_form;
    // ★ RFC-0132 T2b-2 — 틀 안 나열 자리(§13.2 ⓐ·ⓒ): 이 op 이 지금까지 쓴 틀 바이트, 그리고 «다음 나열은 틀 안에»
    //   (쓸 수 있는 `var` 에 묶거나 `mut` 매개변수로 넘길 때 부르는 쪽이 켠다).
    proven_size_t        lbuf_off;
    bool                 lit_frame;
    bool                 lit_into;   // ★ RFC-0132 §13.7 — 채울 바이트 슬라이스가 이미 스택에 있다(할당자에서 받음)
    // ★ RFC-0132 P1 — `for x mut buf`: 몸 안의 `x` 는 `buf` 의 그 칸이다(읽기 = INDEX, `set x` = ISTORE). 중첩되므로 쌓는다.
    proven_u8str_view_t  mel_name[8];
    proven_size_t        mel_buf[8], mel_idx[8];
    proven_size_t        nmel;
} ir_ctx_t;

typedef struct {
    char        name[48];
    int         kind;          // 0 = bool · 1 = int · 2 = choice
    proven_i64  val;           // 해결된 값 (bool: 0/1)
    proven_i64  ch[8];
    proven_size_t nch;
    char        dep[48];       // depends <opt> — 그것이 꺼져 있으면 이것도 못 켠다
    bool        has_dep;
    bool        from_cfg;      // ★ 해결본(lowent.config)이 이 값을 **직접 골랐는가**
    proven_u32  line;
} ir_opt_t;

typedef struct { proven_u64 eid, deps; bool truth; proven_size_t ifrom, ito; } iv_fact_t;

typedef struct { proven_i64 lo, hi; proven_u8 wide; } iv_t;

typedef struct {
    iv_t loc[IR_MAXLOCALS];
    proven_i32 lenlt[IR_MAXLOCALS];
    proven_u8  lenstr[IR_MAXLOCALS];   // 1 = k < len(s) · 0 = k ≤ len(s) (약함) — 위 ivs_t 주석 참조

    // ★★★ **`let n be len s` 는 아주 흔한 관용구인데, 그 사실이 저장에서 죽었다** (2026-07-29).
    //   `lenof` 는 **스택 값**의 속성이라 지역에 담는 순간 사라졌고, 그래서 `while lt i n` 이
    //   `i < len(s)` 임을 못 봤다 — 경계 검사 제거(RFC-0055 D5)가 **길이를 직접 쓴 루프에만**
    //   먹었다. 표준적인 코드가 검사를 지고 돌았다는 뜻이다.
    //   ⇒ 지역에도 그 사실을 싣는다: lenofloc[k] = "지역 k 는 len(지역 s) 다".
    //   무효화·병합 규율은 `lenlt` 와 **똑같다**(따로 만들면 갈린다).
    proven_i32 lenofloc[IR_MAXLOCALS];
    // ★ `len(지역 s) ≥ 지역 p · 지역 q` — 진입 requires 에서만 심는다(위 §행우선 참조).
    proven_i32 lenge_p[IR_MAXLOCALS], lenge_q[IR_MAXLOCALS];
    // ★★★★★ **길이끼리의 관계** (2026-09-10, RFC-0111 §8-17 · WO-0194):
    //   `slenle_t[t] = s` 는 «len(s) ≤ len(t)», `slenle_str[t]` 가 1 이면 «<» 다.
    //   ★ 관계를 **긴 쪽(색인 대상)에 건다.** 짧은 쪽에 걸었더니 `requires lt (len b) (len prev)`
    //     와 `requires lt (len b) (len cur)` 가 **서로를 덮어썼다**(슬롯당 관계는 하나다).
    //     쓰는 자리는 언제나 «이 슬라이스를 색인해도 되나» 이므로, 그 슬라이스가 주인이어야 한다.
    //   진입 `requires` 에서만 심는다(`lenge_p` 와 같은 규율) — 본문에서 만들지 않으므로
    //   합류에서 약해질 것이 없고, 슬라이스를 몸통에서 재대입하면 그 자리에서 죽는다.
    //   왜 필요한가: 두 배열을 나란히 쓰는 코드가 `requires le (len b) (len prev)` 라고
    //   적는데, 그 절이 **아무 사실도 안 남겨** 색인 검사가 남았다(RFC-0111 §8-15 는 그래서
    //   크기를 리터럴로 박아야 했다).
    proven_i32 slenle_t[IR_MAXLOCALS]; proven_u8 slenle_str[IR_MAXLOCALS];
    // ★ 나눗셈 출처도 **지역을 타고 흘러야** 한다: 실제 코드는 `let q be div x 6 .` 로 담아
    //   놓고 나중에 쓴다(`bench_lru`). 값에만 실으면 store/load 한 번에 사라진다 —
    //   `lenlt`·`aff_s` 가 이미 같은 이유로 슬롯 배열을 갖는다.
    proven_i32 divofloc_s[IR_MAXLOCALS]; proven_i64 divofloc_c[IR_MAXLOCALS];
    iv_t lenv[IR_MAXLOCALS];
    // ★★★ **배열 내용 도메인** (R5 잔여): elemv[k] = 슬라이스 지역 k 의 **모든 원소**가 드는 구간.
    //   `requires elem_lt s N` 이 심고, `index s i` 가 ⊤ 대신 이 구간을 낸다. 슬라이스에 쓰면
    //   (istore) 그 값이 이 구간 안임이 증명 안 되면 ⊤ 로 넓힌다(건전성).
    iv_t elemv[IR_MAXLOCALS];
    // ★★ R5 — **차분 제약(difference constraint)**: `lerel[k] = j` 는 loc[k] ≤ loc[j] 를 뜻한다
    //   (lestr[k] 면 **강한** 부등호 <). 구간 하나로는 절대 표현할 수 없는 사실이다.
    //   이것 하나로 `requires le a b . ⇒ sub b a ≥ 0` 이 증명된다 — 그전엔 런타임 검사가 남았다.
    proven_i32 lerel[IR_MAXLOCALS];
    proven_u8  lestr[IR_MAXLOCALS];
    // ★★★★★ **차분 제약의 여백** (2026-08-16, WO-0056 P2) — 길이 쪽(`lenoff`)과 같은 이유·같은 모양:
    //     사실은 언제나 **loc[k] + leoff[k] (<|≤) loc[lerel[k]]** 이고,
    //     여백 `lemargin(k) = leoff[k] + (lestr[k] ? 1 : 0)` 하나만 본다.
    //   ☞ **길이 쪽만 고쳐서는 안 닿는다**: 슬라이스 파라미터는 진입 프롤로그가 자기-재대입
    //     (`load 0 · view.array · store 0`)을 내므로 `len a ≥ n` 이 `lenlt` 로만 남으면
    //     그 store 가 **죽인다**. 살아남는 것은 `lenge_p`(용량 형태)이고, 그것과 맞물리는
    //     사실은 길이가 아니라 **지역 사이의** 차분(`i + 4 ≤ n`)이다. 그래서 여백이 둘 다 필요하다.
    proven_i64 leoff[IR_MAXLOCALS];
    // ★★★ **레인 수도 지역을 건너야 한다** (2026-08-17, SIMD-0001).
    //   스택 값에만 실었더니 `var v be vec u32 4 load a i .` 한 줄에서 사라졌다 —
    //   그 문장은 STORE 로 지역에 넣고 나중에 LOAD 로 꺼내므로, 값에만 붙은 것은 죽는다.
    //   실측으로 그 자리를 봤다(디버그: 저장 시점 L=0). `lenofloc` 이 `lenof` 에 대해
    //   하는 일과 **똑같다** — 흔한 관용구가 사실을 죽이면 그 사실은 없는 것과 같다.
    proven_u8 vlanesloc[IR_MAXLOCALS];
    // ★★★★★ **여백(margin)** — `lenlt`/`lenstr` 의 세 번째 칸 (2026-08-16, WO-0056 P2).
    //   전에는 길이 사실이 두 모양뿐이었다: `k < len(s)` 와 `k ≤ len(s)`. 그런데 **벡터 로드**가
    //   묻는 것은 `i + 4 ≤ len(s)` 다 — 담을 자리가 없어서 그 사실은 **어디에서도 학습되지
    //   않았고**, 그래서 `while le (add i 4) n` 이라는 표준적인 벡터 루프가 반복마다 경계를
    //   다시 쟀다(실측: simd 내적 시간의 **절반**).
    //   ⇒ 칸을 하나 더 두는 대신 **셋을 하나로 읽는다**: 사실은 언제나
    //        **loc[k] + lenoff[k] (<|≤) len(lenlt[k])**
    //     이고, 쓰는 쪽은 **여백** `margin(k) = lenoff[k] + (lenstr[k] ? 1 : 0)` 하나만 본다:
    //        `loc[k] + margin ≤ len(s)`  — 즉 **k 에서 시작해 margin 칸까지 읽어도 안전하다.**
    //     · 스칼라 `index s k` 는 margin ≥ 1 을 요구한다(옛 규칙 `lenstr` = margin 1 그대로다).
    //     · 레인 L 짜리 벡터 로드는 margin ≥ L 을 요구한다.
    //   ★ 건전성 방향: 미지 = 0 = **가장 약함**. 실수로 안 실으면 검사가 되살아날 뿐이다.
    //     위험한 실수는 *덮어쓰기* 뿐이라 — `lenlt` 를 쓰는 자리를 전부 `iv_put_lenlt()` 한
    //     함수로 몰았다(이 저장소가 두 번 당한 *"목록에서 한 줄 빠뜨림"* 을 구조로 막는다).
    proven_i64 lenoff[IR_MAXLOCALS];
    iv_fact_t facts[IV_MAXFACT];   // ★ 이 경로에서 참/거짓임이 **밝혀진** 식들
    proven_u8  nfacts;
} ivstate_t;































/* ── low_ir.c 가 내주는 것 (VM 이 쓴다) ─────────────────────────────────── */
proven_i64  ity_lo(proven_u8 bits, bool sign);
proven_i64  ity_hi(proven_u8 bits, bool sign);
proven_size_t ir_def_find(const low_ir_t *ir, proven_u8str_view_t name, bool *found);
double      ir_bits_to_f(proven_u64 bits, proven_u8 size);
proven_u64  ir_f_to_bits(double d, proven_u8 size);

/* ★ 스케줄러의 지시(RFC-0009 D5 · 결정론 시험). 계약 검사 쪽이 세우고 VM 이 읽는다 —
 *   *한 뜻이 두 파일에 걸린 유일한 자리*이므로 여기 이름을 붙여 눈에 띄게 둔다. */
#define SCHED_MAXSTEP 64
extern bool          g_cancel_scope;
extern proven_u16    g_sched_plan[SCHED_MAXSTEP];
extern proven_size_t g_sched_plan_len;
extern proven_u16    g_sched_sizes[SCHED_MAXSTEP];
extern proven_size_t g_sched_step;

/* ── low_vm.c 가 내주는 것 — **없다.** ──────────────────────────────────────
 * ★ 처음 셀 때 `vm_through` 가 나간다고 봤는데, 실제로는 **주석에만** 나왔다.
 *   ☞ *이름을 세는 도구는 주석을 코드로 읽는다 — 세고 나서 한 번은 눈으로 본다.*
 *   그래서 이 방향은 **비어 있다**: `low_ir.c` 는 VM 을 하나도 안 부른다. */


/* ── 공용 어휘 — 어느 조각을 떼든 따라오는 낱말 ─────────────────────────────
 * ★ 이 목록이 길어지는 것은 **선이 나쁜 것이 아니다**. 파일의 낱말이 는 것이다.
 *   짧아야 하는 것은 아래 「진짜 결합」이다. (WO-0164 에서 규칙을 이렇게 갈랐다.) */
bool veq(proven_u8str_view_t v, const char *s);
bool is_atom(const low_cst_t *n);                                              /* 127회 */
void ir_fail(ir_ctx_t *c, const char *code, const char *msg, proven_u32 line); /* 25회 */
/* ★ 저자에게만 보이는 알림 — (파일·줄)이 같은 것은 한 번만 낸다. 문 하강의 `ensures` 도 쓴다. */
void ir_warn_at(ir_ctx_t *c, const char *code, const char *msg, proven_u32 line, const char *file);
bool ir_int_lit(proven_u8str_view_t v, proven_i64 *out);                       /* 20회 */
proven_size_t ir_local_find(ir_ctx_t *c, proven_u8str_view_t name, bool *found); /* 16회 */
proven_size_t ir_emit(ir_ctx_t *c, low_irw_t w, proven_i64 a);                 /* 11회 */

/* ── 진짜 결합 — 구간분석이 **하강·계약·op 표**에 기대는 자리 ───────────────
 * ★★ **이 목록이 늘면 선이 잘못된 것이다.** 늘리기 전에 「왜 저쪽이 이것을 알아야
 *   하는가」를 먼저 묻는다. */
proven_size_t ir_struct_find(const low_ir_t *ir, proven_u8str_view_t name, bool *found); /* 7회 */
extern ir_opt_t g_opt[IR_MAXOPT];                                              /* 6회 */
proven_u8 ir_field_size(proven_u8str_view_t w);                                /* 6회 */
extern proven_size_t g_nopt;                                                   /* 5회 */
bool ir_requires_at(const low_cst_t *f, proven_size_t i, proven_size_t *k, bool *is_assume, bool *is_debug); /* 5회 */
bool ir_is_float_ty(proven_u8str_view_t w);
ityp_t ity_of_decl_c(const void *cv, const low_cst_t *f, proven_size_t start, proven_size_t end); /* 4회 */
extern bool g_smt_on;                                                          /* 3회 */
proven_size_t ir_field_tywords(const low_cst_t *fld);                          /* 3회 */
proven_size_t ir_local_declare(ir_ctx_t *c, proven_u8str_view_t name, proven_u32 line); /* 3회 */
bool ir_opt_num(proven_u8str_view_t v, proven_i64 *out);                       /* 3회 */
bool ir_type_is_slice(const ir_ctx_t *c, proven_u8str_view_t w);               /* 3회 */
void smt_emit_query(const low_ir_def_t *d, proven_size_t pc, const low_smt_sys_t *s); /* 3회 */
extern const ityp_t ITY_UNK;                                                   /* 2회 */
extern ir_cfg_t g_cfg[IR_MAXOPT];                                              /* 2회 */
low_irw_t ir_cmp_word(proven_u8str_view_t op);                                 /* 2회 */
void ir_diag(ir_ctx_t *c, const char *code, const char *msg, proven_u32 line); /* 2회 */
void ir_iface_hash(proven_allocator_t work, const low_cst_t *f, low_ir_def_t *d); /* 2회 */
ir_opt_t *ir_opt_find(proven_u8str_view_t n);                                  /* 2회 */
proven_u8 ir_slice_esz_ex(const low_ir_t *ir, const low_cst_t *f, proven_size_t from, proven_size_t to, bool *eflt, int *sidx, bool *esgn); /* 2회 */
extern const low_target_t *ir_tgt;                                             /* 2회 */
extern ir_bmode_t g_bmode;
extern ir_opt_t g_opt[IR_MAXOPT];                                              /* 1회 */
extern proven_size_t g_bdropped;                                               /* 1회 */
extern ir_bmode_t g_bmode;                                                     /* 1회 */
extern proven_u64 g_iv_stack_full;                                             /* 1회 */
extern proven_size_t g_ncfg;                                                   /* 1회 */
void ir_block(ir_ctx_t *c, const low_cst_t *blk);                              /* 1회 */
void ir_bset_context(ir_ctx_t *c, const low_cst_t *f, proven_size_t start, proven_size_t end); /* 1회 */
void ir_contract_entry(ir_ctx_t *c, const low_cst_t *f);                       /* 1회 */
void ir_def_hashes(proven_allocator_t work, low_ir_t *ir);                     /* 1회 */
bool ir_ensures_at(const low_cst_t *f, proven_size_t i, proven_size_t *k);     /* 1회 */
proven_i64 ir_machine(proven_u8str_view_t v, bool *found);                     /* 1회 */
void ir_named_contracts(ir_ctx_t *c, const low_parse_result_t *pr, const low_cst_t *f); /* 1회 */
proven_u8str_view_t ir_strip_mod(ir_ctx_t *c, proven_u8str_view_t v);          /* 1회 */
void ir_struct_layout(const low_ir_t *ir, low_ir_struct_t *s);                 /* 1회 */
int ir_word_arity(const low_ir_t *ir, low_irw_t w, proven_i64 a);              /* 1회 */
bool is_clause_word(proven_u8str_view_t v);


/* ── low_iv.c 가 내주는 것 — 계약→시험 절이 구간 추론을 읽는다 ────────────── */
void cert_reset(void);
void ir_interval(ir_ctx_t *c, const low_cst_t *f, low_ir_def_t *d, proven_size_t *proven, proven_size_t *total);
void iv_apply_named(ir_ctx_t *c, const low_parse_result_t *pr, const low_cst_t *f, iv_t *sig);
void iv_apply_rel_requires(ir_ctx_t *c, const low_cst_t *f, ivstate_t *e0);
void iv_apply_requires(ir_ctx_t *c, const low_cst_t *f, iv_t *sig);
iv_t iv_ty(ityp_t t);
#define IR_CERT_MAX 4096
extern const iv_t IV_TOP;
extern ir_cert_t g_cert[IR_CERT_MAX];
extern proven_size_t g_cert_drop;
extern proven_i64 g_cert_ext[IR_CERT_EXT_MAX];
extern proven_size_t g_ncert;



/* (WO-0165) 공용 어휘 — 문 하강도 같은 낱말을 쓴다 */
low_ir_ins_t *ir_at(ir_ctx_t *c, proven_size_t i);
void ir_emit_sinkfull_guard(ir_ctx_t *c, proven_size_t out_local, proven_size_t idx_local);
proven_size_t ir_field_intern(ir_ctx_t *c, proven_u8str_view_t name);
void ir_run(ir_ctx_t *c, low_cst_t *const *k, proven_size_t start, proven_size_t n);

/* (WO-0165) 진짜 결합 — 문 하강이 **식 하강·맥락**에 기대는 자리 */
bool ir_comptime_cond(ir_ctx_t *c, const low_cst_t *f, proven_size_t start, proven_size_t end, proven_i64 *val);
proven_size_t ir_cond_brz(ir_ctx_t *c, low_cst_t *const *k, proven_size_t start, proven_size_t n, proven_size_t *sites);
bool ir_contract_operand(ir_ctx_t *c, proven_u8str_view_t v, proven_u32 line);
bool ir_ctfe_fold(const low_cst_t *nd, proven_i64 *out, int fuel);
proven_size_t ir_def_find_in(ir_ctx_t *c, proven_u8str_view_t name, bool *found);
bool ir_enum_is_payload(const ir_ctx_t *c, proven_i64 vi);
proven_u8str_view_t ir_enum_other2(const ir_ctx_t *c, proven_u8str_view_t ename, proven_u8str_view_t v);
bool ir_enum_variant_of(const ir_ctx_t *c, proven_u8str_view_t ehead, proven_u8str_view_t vseg, proven_size_t *out_vi);
bool ir_fold_lane_expr(const ir_ctx_t *c, const low_cst_t *nd, proven_i64 *out);
bool ir_glued_place(ir_ctx_t *c, proven_u8str_view_t v, proven_u32 line, bool *is_index, proven_i64 *fid);
bool ir_head_named(const low_cst_t *nd, const char *s2);
bool ir_is_sfield(const ir_ctx_t *c, proven_u8str_view_t name);
void ir_mmio_perm_check(ir_ctx_t *c, proven_u8str_view_t base, proven_u8str_view_t reg, bool wr, proven_u32 line);
proven_i64 ir_native_lanes_of(proven_u8 esz);
void ir_value(ir_ctx_t *c, low_cst_t *const *k, proven_size_t *pos, proven_size_t end);
proven_i64 ir_variant_index(const ir_ctx_t *c, proven_u8str_view_t name);
proven_u8str_view_t ir_variant_owner(const ir_ctx_t *c, proven_u8str_view_t v);
void ir_vec_context(ir_ctx_t *c, const low_cst_t *f, proven_size_t start, proven_size_t end);
extern const proven_byte_t IR_ENUM_TAGF[2];
extern proven_size_t g_nrg;
extern proven_u8str_view_t g_rgnames[IR_MAXREGION];
// ★ RFC-0112 D3 — 그 영역이 **어느 뿌리**에서 깎는가(1 = 힙, 0 = 고정 창). 이름과 같은 자리.
extern proven_u8 g_rgroot[IR_MAXREGION];
extern proven_size_t g_rgslot[IR_MAXREGION];   // 그 깊이의 표식이 사는 지역 슬롯
// ★★ RFC-0135 S2 (D11) — 블록을 나갈 때 돌려줄 할당기 바이트(바인딩 슬롯 + `send <출처> release (some_value <이름>)`)
#define IR_MAXREL 64
extern const low_cst_t *g_relform[IR_MAXREL];
extern proven_size_t    g_relslot[IR_MAXREL];
extern proven_size_t    g_nrel;
extern proven_u8str_view_t g_relname[IR_MAXREL];   // 숨은 임시의 이름(`$t`) — `drop t` 가 찾는다
// 돌려줄 것은 **받은 그대로의 바이트**(reserve 의 답)다 — 바인딩의 타입 붙은 보기가 아니다(길이가 원소 수라 release 가 거절한다).
//   값 하강이 그 답을 담은 숨은 슬롯을 g_using_ov 에 남기고, 돌려주기를 넣는 동안 g_relsub_atom(폼 안의 이름 원자)을 그 슬롯으로 읽는다.
extern proven_size_t    g_using_ov;
extern const low_cst_t *g_relsub_atom;
extern proven_size_t    g_relsub_slot;


/* (WO-0165) 진짜 결합 — 문 하강이 **식 하강·맥락**에 기대는 자리 */
proven_size_t ir_cond_node(ir_ctx_t *c, const low_cst_t *nd, proven_size_t *sites, proven_size_t ns);
void ir_island_climb(ir_ctx_t *c, low_cst_t *const *k, proven_size_t n, proven_size_t *pos, int minp);
void ir_node(ir_ctx_t *c, const low_cst_t *nd);
void ir_pipe(ir_ctx_t *c, const low_cst_t *f, proven_size_t first, bool as_value);
void ir_run_ex(ir_ctx_t *c, low_cst_t *const *k, proven_size_t start, proven_size_t n, bool allow_infix);
ityp_t ity_alias_lookup(const void *cv, proven_u8str_view_t w);

#endif /* LOW_IR_PRIV_H */
