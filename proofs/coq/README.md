# V3 기계화 — Coq 환경

RFC-0017의 신뢰 사다리 V3(기계 증명). 대상은
[lambda-lowent-core-agreement.md](../lambda-lowent-core-agreement.md)의 **정리 A**(직선 단편의
정적 EXCL ≡ 동적 borrow-stack 합치) — 종이 증명(V2)과 유계 전수 체크(V3-lite)를 **무계 기계
증명**으로 승격한다.

**왜 Coq(Rocq)인가 — Lean이 아니라.** 지금 증명하려는 단편은 동시성이 없어 순수 연산 의미론이고,
솔직히 **둘 다 된다.** 결정적 차이는 그다음이다: R2(결정론 병렬)·R7(RC11 atomics)을 증명하려면
**동시성 분리논리**가 필요하고 그것이 **Iris**(Coq 전용)다. Lean에는 대응하는 성숙한 프레임워크가
없다. 지금 Lean으로 시작하면 동시성에 도달하는 순간 이주해야 한다. 게다가 R1의 목표가 문자 그대로
**RustBelt**(Coq+Iris)다 — UNPROVEN.md가 대조군으로 지목한 그것.

Iris는 **아직 필요 없다.** 정리 A는 순수 Coq만으로 기계화된다.

## 설치

| 환경 | 명령 |
|---|---|
| **Arch Linux** | `sudo pacman -S coq` (IDE도 원하면 `coqide`) |
| Debian / Ubuntu | `sudo apt install coq` |
| 버전을 직접 고르고 싶을 때 | `opam install coq.8.20.1` (opam 부트스트랩 필요) |

Arch의 `coq` 패키지는 upstream을 그대로 따라간다. 8.20 이상이면 본 증명은 그대로 컴파일된다
(9.x = Rocq 리브랜딩; `coqc` 심볼릭 링크가 유지되므로 스크립트는 동일하게 동작할 것으로 보나,
9.x에서 실측하지는 않았다).

## Iris 설치 — 동시성(R2/R7)을 위한 준비

순차 단편은 **순수 Coq 로 끝났다**(열린 갭 0). 남은 것은 동시성이고, 거기에는 **동시성 분리논리**가
필요하다 — 그것이 Iris 다(Coq 전용. 이것이 Lean 이 아니라 Coq 을 고른 이유였다).

| 환경 | 명령 |
|---|---|
| **표준(opam 이 있으면)** | `opam install coq-iris` |

opam 없이 되는 이유: **Iris 와 std++ 는 순수 Coq 이다.** OCaml 플러그인이 없으므로 소스에서
`make && make install` 이면 끝이고, Coq 가 이미 사용자 prefix 에 있으니 설치 위치도 사용자 소유다.
opam 부트스트랩(수십 분)이 필요 없다.

**★ 버전 짝이 중요하다.** Iris 는 std++ 의 특정 커밋에 맞춰 나온다 — 아무거나 섞으면 안 된다.

| Coq | Iris | std++ |
|---|---|---|
| **8.20** (이 환경) | **iris-4.3.0** | **coq-stdpp-1.11.0** |
| 9.x | iris-4.4.0+ | coq-stdpp-1.12.0 |

설치 후 `. ~/coqroot/env.sh` 만 하면 된다(같은 env.sh 가 Iris 도 가리킨다 — `user-contrib` 에
설치되므로 `COQPATH` 를 따로 만질 필요가 없다).

**실측(2026-07-12):** Coq 8.20.1 · iris-4.3.0 · coq-stdpp-1.11.0 으로 빌드·설치 성공.
설치만 확인하지 않고 **실제 증명이 도는지** 확인했다 — heap_lang 위의 Hoare triple:

```coq
From iris.heap_lang Require Import lang proofmode notation.
Definition swap_twice : val := λ: "v", let: "l" := ref "v" in "l" <- #7 ;; !"l".
Lemma swap_twice_spec (v : val) : {{{ True }}} swap_twice v {{{ RET #7; True }}}.
Proof. iIntros (Φ) "_ HΦ". wp_lam. wp_alloc l as "Hl". wp_let. wp_store. wp_load.
       iApply "HΦ". done. Qed.
```
→ Qed. 분리논리·proofmode·heap_lang 전부 동작. 소스 47M · 설치본 ~41M · 빌드 수 분(16코어).

**아직 Iris 를 쓰는 증명은 없다.** 이것은 R2(결정론 병렬)·R7(RC11 atomics)을 향한 **환경 준비**이고,
그 증명들은 다음 작업이다.

## 빌드

```sh
. ~/coqroot/env.sh          # 사용자 영역 설치를 쓸 때만
make -C docs/proofs/coq     # 전부 컴파일 (Qed 실패 시 비0 종료)
```

## 파일

| 파일 | 내용 | 상태 |
|---|---|---|
| **`NumericLattice.v`** | RFC-0052 rev.b 수치 코어 — 안전 확대 격자 ⊑ · join · narrow · 나눗셈 | **전부 Qed** |
| **`LowentEXCL.v`** | 이벤트 열 · 정적 판정(S1/S2) · 동적 ledger(D1~D5) · **정리 A(무계)** | **전부 Qed** |
| **`LowentLoop.v`** | 토큰 기반 동적 의미 · **루프 정리(보조정리 B, 무계·모든 k)** | **전부 Qed** |
| **`LowentPlace.v`** | **place 일반화** — 지역을 사영 경로로, 동등성을 **겹침**으로 | **전부 Qed** |
| **`LowentLaunder.v`** | **op 경계** — 차용 세탁·별칭 등록 · 옛 규칙의 **반례** | **전부 Qed** |
| **`LowentBlock.v`** | **점-닫힘 문법** — 한 문장=한 블록 · 닫개의 결정성 · 줄잇기 한 규칙 | **전부 Qed** |
| **`LowentRC11.v`** | **R7-(3) RC11 유한 litmus** — 기본 seq_cst 가 SB 를 막고, relaxed 는 허용한다 | **전부 Qed** |
| **`LowentRC11SC.v`** | **R7-(3) 일반 방향** — 모든 접근이 seq_cst 면 `po∪rf∪mo∪fr` 가 **∀실행에서** 비순환 · 부분관계 비순환성 · mode 단조성 | **전부 Qed** |
| **`LowentRC11Sweep.v`** | **litmus 전수 열거** — 이름 있는 일곱(SB·LB·MP·CoRR·CoWR·2+2W·IRIW) + 유계 전수 단조성 | **전부 Qed**(93초) |
| **`LowentRC11Mono.v`** | ★★★ **단조성 일반 정리** — 약하게 하면 거동이 **늘어난다**(∀실행·∀배정). 유계 전수를 **정리로 대체** + 옛 세기 순서의 **반례** | **전부 Qed** |
| **`LowentNest.v`** | ★★★ **중첩 루프** — 임의 깊이 · **반복마다 다른 전개** · 옛 평평한 정리를 따름정리로 되찾음 | **전부 Qed** |
| **`LowentEffect.v`** | ★★★ **효과 건전성** — 선언이 실제를 덮는다(호출·**재귀·무한 재귀** 포함) + 순수 조각의 재정렬·제거 적법성 | **전부 Qed** |
| **`LowentType.v`** | ★★★ **타입 건전성(수치 식 코어)** — 진행·보존 + **표현 불변식**(값이 타입 폭 안에 있다) · 덫을 정직하게 모델링 | **전부 Qed** |
| **`LowentJoin.v`** | ★★★ **분기 합류에서 관계 사실이 정확하다**(무계) + 합집합 합류·죽이지 않기가 **불건전함의 반례** | **전부 Qed** |
| **`LowentTypeStore.v`** | ★★★ **참조·슬라이스·호출의 타입 건전성** — 진행·보존 · store 가 실행 내내 적격 · **색인은 범위 밖을 읽지 않는다** | **전부 Qed** |
| **`LowentOpt.v`** | ★★★ **효과 기반 최적화가 값 층에서 적법하다** — 재정렬·CSE·memoize·죽은 코드 제거 + **전제가 필요함을 반례로** | **전부 Qed** |
| **`LowentDeadlock.v`** | ★★★ **교착 자유** — 잠금을 오름차순으로만 잡으면 **진행 가능한 스레드가 반드시 있다** · 어기면 교착하는 상태를 계산 | **전부 Qed** |
| **`LowentFair.v`** | ★★★ **굶주림 없음(유계)** — 라운드로빈에서 준비된 태스크는 **자기 앞의 수+1 걸음 안에** 돈다 · 회전이 없으면 굶는다 | **전부 Qed** |
| **`LowentHash.v`** | ★★★ **내용 주소화 H1·H2·H3** + ★ **인코딩 단사성**(충돌은 오직 해시 함수의 문제로 좁혀진다) | **전부 Qed** |
| **`LowentMono.v`** | ★★★ **단형화** — 경계를 만족하는 인스턴스는 **반드시 타입이 붙는다** · 크기·소유가 인스턴스 시점에 **정해진다** · `dyn` 거절의 형식적 근거 | **전부 Qed** |
| **`LowentEnum.v`** | ★★★ **페이로드 enum** — `guard isa` 로 **좁히면 `get` 은 트랩하지 않는다**(RFC-0080 §4.6) · 좁힘 요구를 빼면 트랩하는 반례 | **전부 Qed** |
| **`LowentOrderExt.v`** | **순서 확장** — 위상 정렬이 성공하면 그 결과는 **선형 확장이다**(∀) | **전부 Qed** |
| **`LowentCert.v`** | **증명 운반 검사의 규칙** — 구간 산술·narrow·div 의 **건전성 증명** + `check_cert` | **전부 Qed** |
| **`LowentRWLock.v`** | **rwlock** — 쓰기 잠금의 배타성(**Iris**, 직접) · 읽기 쪽 분수 소유권은 iris `rw_spin_lock` 에서 **빌렸다**(그 파일 §5) | **전부 Qed** |
| **`LowentIRC11.v`** | ★★ **약한 메모리 프로그램 논리** — release/acquire 메시지 전달이 **RC11 에서** 데이터를 나른다(**iRC11/gpfsl** · Rocq 9 필요) | **Qed** · 공리 0 |
| **`LowentSPSC.v`** | ★★ **lock-free SPSC 알고리즘** — 밀어넣기/꺼내기의 자원 이전이 **RC11 에서** 옳다(iRC11 · `make weak`) | **Qed** · 공리 0 |
| **`LowentPar.v`** | **R2 결정론 병렬** — DET-1/2/3(Bernstein · 고정 reduction tree) | **전부 Qed** |
| **`LowentDRF.v`** | **R7 가둠** — 안전 코드(L1 disjoint · L2 actor)에는 **경합이 없다** | **전부 Qed** |
| **`LowentLock.v`** | **R7 level-3** — CAS 스핀락의 상호배제 (**Iris** · 유일하게 Iris 필요) | **전부 Qed** |

★★ **두 도구에서 검증된다**(2026-07-31): **Coq 8.20.1** 과 **Rocq 9.2** 양쪽에서 14 파일이
**무수정** 통과한다(경고만 난다 — "Loading Stdlib without prefix is deprecated").
Rocq 9 환경에서는 **`COQLIB` 을 비워야 한다**: 남아 있으면 Rocq 9 가 8.20 의 라이브러리를 보고 죽는다.
☞ 도구를 하나 더 통과한 것이 왜 값인가: 정리는 **모델**에 대한 것이지만, 그 모델을 확인하는
  것은 **프로그램**이다. 서로 다른 두 확인기가 같은 답을 내면 확인기 자체의 결함 가능성이 준다.

★ **`make weak`** 는 `LowentIRC11.v`(iRC11/gpfsl · **Rocq 9 전용**)를 따로 짓는다 —
기본 목표에 넣지 않는다: 도구가 없는 환경에서 **다른 것까지 못 짓게 만들지 않는다.**

`make -C docs/proofs/coq` 는 **27 파일**을 전부 컴파일한다. Iris 가 없는 환경이면 `make pure` 로
순수 Coq **25 파일**만 검증할 수 있다 (Iris 가 필요한 것은 `LowentLock.v` **하나뿐**이다).
★ 이 수는 한때 "8 / 7" 이라고 적혀 있었다 — `LowentBlock.v`·`LowentRC11.v` 가 늘었는데 문장을
안 고쳤다(2026-07-30 정정). Makefile 의 `VS` 가 권위이고, 이 문장은 그것을 따라간다.

실측(2026-07-31): **Theorem 177 · Corollary 14 · Lemma 231 · Example 33 · 10,176 줄 ·
`Admitted`/`Axiom` 0.**
★ `LowentRC11SC.v` 는 **`LowentRC11.v` 를 Require 하는 첫 파일**이다(모델을 베끼지 않는다).
  그래서 Makefile 이 `-Q . ""` 와 순서 의존을 갖는다.

### `NumericLattice.v` — 무엇을 증명했나

RFC-0052 rev.b 의 중심 주장은 *"값 보존이 완벽히 보장되는 확대만 암묵 허용한다"* 이다.
그 "완벽히 보장된다"를 기계가 검사한다:

| 정리 | 내용 | 무엇을 정당화하나 |
|---|---|---|
| `sub_preserves` | τ ⊑ τ' → 모든 값이 보존된다 | **암묵 확대의 안전성 전부** |
| `sub_refl/trans/antisym` | ⊑ 는 부분 순서 | "넓은 쪽"이 well-defined |
| `join_sound` | 비교 가능하면 join 이 둘 다 담는다 | 이항 연산의 결과 타입 |
| `join_is_an_operand` | join 은 두 피연산자 중 하나 | **타입을 발명하지 않는다** |
| `narrow_ok_iff` | narrow 는 fits 일 때만 성공 | 조용한 마스킹 금지 |
| `widen_never_fails` | ⊑ 확대는 실패 불가 | `widen` 이 전역 함수 |
| `div_unsigned_total` | 무부호 나눗셈의 실패 = **0 나누기뿐** | `div_nz` 가 순수 문맥에서 전역 |
| `div_signed_failure_is_only_min_neg1` | 부호 나눗셈의 실패 = **0 나누기 + MIN/-1 뿐** | 구간 분석이 방전할 의무 확정 |
| `rem_pairs_with_div` / `mod_pairs_with_div_floor` | 나머지는 **자기 몫과만** 짝을 이룬다 | D7 — 섞으면 항등식이 깨진다 |
| `rem_sign_follows_dividend` | rem 의 부호 = **피제수** 부호 | D7 |
| `mod_sign_follows_divisor` | mod 의 부호 = **제수** 부호(크기도 가둔다) | D7 |
| `mod_is_a_safe_index` | 0 < n → 0 ≤ i mod n < n (**i 가 음수여도**) | D7 — 버킷·시계·순환버퍼 |
| `rem_can_be_negative` | rem (-7) 3 = -1 vs mod (-7) 3 = 2 | D7 — 반례 |
| `rem_eq_mod_when_nonneg` | 무부호에선 두 관례가 **일치** | 이름 변경 파급이 좁은 근거 |

**정리 27개 전부 Qed.** 특히 값진 것 둘:
- **나눗셈의 트랩 지점이 정확히 둘뿐임을 증명** ⇒ RFC-0053 구간 분석의 의무가 확정된다.
- **rem/mod 의 부호 규칙을 증명** ⇒ D7 이 관례 선택이 아니라 정리 위에 선다.
  `mod` 는 음수 인덱스에도 안전하고(`mod_is_a_safe_index`), `rem` 은 그렇지 않다 —
  C·Rust·Go 의 `%` 가 rem 이고 Python 의 `%` 가 mod 인 혼란을, **이름을 나눠서** 끝낸다.

### `LowentEXCL.v` — V3 완료: 정리 A의 **무계** 기계 증명

```coq
Theorem agreement : forall evs,
  wf evs -> static_green evs = true -> dyn_clean evs = true.
```
> **정적 검사를 통과한 프로그램은 실행 중 차용 위반(⚡)을 일으키지 않는다.**
> `Print Assumptions agreement` → **Closed under the global context** (공리 0, admit 0).

V3-lite(유계 전수, 10,490,024 시퀀스)가 확인하던 것을 **임의 길이·임의 태그 수**로 확장했다.

증명의 심장은 두 불변식이다:

| 불변식 | 내용 | 왜 필요한가 |
|---|---|---|
| `INV` | 정적으로 살아 있는 차용은 동적 스택에도 있다 | ⚡ 는 "스택에 없는 태그 사용"에서만 나므로, 이것이 곧 결론 |
| `SINV` | **같은 지역의 서로 다른 살아 있는 차용은 모두 shr** | S1(Create 의 has_conflict)이 보존한다. 이 사실이 D3(쓰기)의 `pop_above` 가 *다른* 살아 있는 차용을 지우지 않음을 보장한다 |

`SINV` 가 없으면 정리가 성립하지 않는다 — 기계화 과정에서 드러난 사실이다.
종이 증명(§4)은 이를 암묵적으로 썼고, Coq 이 그것을 명시하게 만들었다.

**기계화가 잡아낸 것:** 첫 형식화에서 D2(차용 읽기)를 "t 위를 전부 버림"으로 옮겼는데,
실제 규칙은 **"t 위의 exc 만 버림"**(shr 은 남는다)이다. 구현·종이와 대조해 교정했다.
유계 모델 체크는 이 오류를 잡지 못했을 것이다 — 두 모델이 *같은* 오해를 공유했다면.

범위: **직선 단편**(루프 없음). 루프는 `LowentLoop.v` 가 덮는다(아래).

### `LowentLoop.v` — 루프 정리 (보조정리 B의 기계화)

```coq
Theorem loop_agreement : forall pre body,
  (exists lpre, srun [] pre = Some lpre /\ srun lpre body = Some lpre) ->
  forall k, dyn_clean pre body k = true.
```
> **정적 상태가 루프 본문의 고정점이면, 본문을 몇 번 돌든 차용 위반이 없다.**
> `Print Assumptions loop_agreement` → **Closed under the global context** (공리 0, admit 0).

**모델의 열쇠 — 신선한 토큰.** `Create` 는 실행할 때마다 **새 토큰**을 발행한다(구현과 동일).
그래서 본문이 만드는 차용은 반복마다 다른 값이고, 지난 반복의 토큰은 `env` 가 더 이상 가리키지
않으므로 *사용될 수 없다* — **반복 간 오염이 원천적으로 없다.** 이 한 가지 설계가 태그 재명명
기계장치 없이 루프를 다룬다. `FRESH` 불변식(발행된 토큰은 모두 `next` 미만)이 그것을 형식화한다.

**남는 위험은 하나뿐이다:** `pre` 에서 만들어져 `body` 에서 쓰이는 차용. 반복 i 의 `own(x)` 가
그것을 죽이면 반복 i+1 의 `use` 가 ⚡ 를 낸다. 정리의 전제(**정적 고정점** `srun lpre body = lpre`)가
정확히 그 짝을 배제한다 — 종이 §5 의 S2L 이 하는 일이다.

증명 구조: `agreement_step`(한 걸음) → `agreement_run`(직선) → `loop_fixpoint_clean`(k 귀납)
→ `loop_agreement`. 세 불변식 `INV`·`SINV`·`FRESH` 가 함께 보존된다.

## 대응 관계 (종이 ↔ 기계 ↔ 구현)

| 개념 | 종이 증명 | Coq | 구현 |
|---|---|---|---|
| 이벤트 | §1 `ev` | `Inductive ev` | 문장 tick |
| 정적 판정 | §2 S1/S2 | `static_green : list ev -> bool` | `low_region.c excl2_conflicts` |
| 동적 ledger | §3 D1~D5 | `run : list ev -> option state` | `low_ir.c` borrow stack |
| **정리 A** | §4 (종이) | **`Theorem agreement` (Qed)** | 유계 전수 체크(V3-lite) |

**네 재구현(종이·Coq·C·모델체커)이 독립적**이라는 점이 요점이다 — 넷이 일치하면 넷 다 틀렸을 확률이 낮다.

### `LowentPlace.v` — place 일반화: `x.a` 와 `x.b` 는 다른 곳이다

`LowentEXCL.v` 는 지역을 평평한 `nat` 으로 봤다. 그래서 **`x.a` 와 `x.b` 를 구별할 수 없다** —
둘 다 그냥 "x" 다. 그 모델에서는 서로 다른 필드의 두 mut 차용이 충돌로 보인다(거짓 양성).
실제 언어는 필드를 구별하는데 증명이 못 따라가고 있었다.

지역을 **place**(base :: 필드 경로)로 올리고, 동등성(`=`)을 **겹침**으로 바꾼다:

```coq
Definition ov (p q : place) : bool := pre p q || pre q p.   (* 한쪽이 다른 쪽의 접두사 *)

Example sibling_fields_are_disjoint : ov [0;1] [0;2] = false.   (* x.a 와 x.b — 분리 *)
Example whole_overlaps_field        : ov [0]   [0;1] = true.    (* x 와 x.a — 포함 *)
```

동적 상태도 place 로 색인하고(**함수** `place -> list tag`), 한 걸음이 *겹치는 모든 place* 를
건드리게 했다. 그 위에서 **정리 A 가 그대로 성립한다**:

```coq
Theorem agreement : forall evs, wf evs -> static_green evs = true -> dyn_clean evs = true.
```
> `Print Assumptions agreement` → **Closed under the global context**.

정밀도를 얻으면서 건전성을 잃지 않았음을 실증이 못 박는다:

| 프로그램 | 결과 | 뜻 |
|---|---|---|
| `&mut x.a` + `&mut x.b` | **green · dyn-clean** | 형제 필드의 두 배타 차용은 **공존한다** |
| `&mut x` + `&mut x.a` | **거부** | 겹치면 여전히 잡는다 |
| `&mut x.a` 뒤 `x.b = …` | **green** | 형제에 쓰는 것은 차용을 죽이지 않는다 |
| `&mut x.a` 뒤 `x = …` | **거부 · dyn-dirty** | 전체에 쓰면 안쪽 차용이 죽는다 |

평평한 모델은 place 길이 1 의 특수 경우다(`ov_singleton : ov [x] [y] = Nat.eqb x y`) —
`LowentEXCL.v` 는 이 정리의 **부분**이다.

### `LowentLaunder.v` — op 경계: 옛 규칙을 **반증**하고 새 규칙을 **증명**한다

`LowentEXCL.v` 의 이벤트 언어에는 **호출이 없다.** 그래서 이 패턴이 모델 밖에 있었다:

```
let r  = &mut x .     rem 차용 t1
let r2 = f r .        rem ★ op 이 인자 차용을 **세탁해** 참조를 돌려준다
set x  = … .          rem 소유자 쓰기 — t1 이 죽는다
use r2 .              rem ⚡ r2 는 t1 과 **같은 차용**이다
```

구현은 이것을 겪었다(UNPROVEN 의 "열린 갭 D3"): 정적 검사가 `r2` 를 차용으로 **알아보지
못해** 통과시켰다. E-ESCAPE(반환 참조는 인자 유래)를 근거로 **결과를 인자 차용의 별칭으로
등록**해 고쳤다(`low_region.c` 의 `alias[]`).

지금까지 그것은 *"우리가 찾아서 고쳤다"* 는 **이야기**였다. 이제 **정리**다:

```coq
(* ★ 옛 규칙은 건전하지 않았다 — 기계 검증된 반례 *)
Theorem old_rule_is_unsound :
  wf laundered_escape /  static_green_bad laundered_escape = true /\    (* 정적 검사는 통과시킨다 *)
  dyn_clean laundered_escape = false.            (* 그런데 실행하면 ⚡ *)

(* ★ 새 규칙은 그것을 잡는다 *)
Theorem new_rule_catches_it : static_green laundered_escape = false.

(* ★★ 그리고 새 규칙에서 정리 A 가 성립한다 — 고침이 건전하다 *)
Theorem agreement : forall evs, wf evs -> static_green evs = true -> dyn_clean evs = true.
```
> 셋 다 `Print Assumptions` → **Closed under the global context**.

과잉 거부도 아니다: **정상적인 세탁**(`honest_laundering`)은 green 이고 dyn-clean 이며,
원본 이름과 세탁된 이름을 번갈아 써도(`both_names`) 통과한다 — **같은 차용**이기 때문이다.

**형식화가 드러낸 것:** 세탁이 들어오면 `SINV` 를 **태그가 아니라 근본 태그(alias_of)로**
비교해야 한다. 태그로 비교하면 세탁된 이름과 원본이 "같은 지역의 서로 다른 mut 차용" 으로
보여 불변식이 거짓이 된다. 별칭은 새 차용이 **아니다** — 그 한 줄이 증명의 축이다.

### `LowentPar.v` — R2: 결정론 병렬. **Iris 없이 갚았다.**

UNPROVEN R2 는 *"level-1 병렬은 순차와 비트 동일 — DET-1 disjoint split · DET-2 의존 sync ·
DET-3 고정 reduction tree"* 를 주장하고 **미증명·가정**으로 두고 있었다.

★ 먼저 정직해지자: **이 셋은 Iris 가 필요 없다.** 결정론은 데이터 경합의 문제가 아니라
**Bernstein 조건**의 문제다. 태스크들이 서로의 읽기/쓰기 집합을 건드리지 않으면 순서를 바꿔도
결과가 같다 — 순수 Coq 이다. Iris 가 진짜 필요한 곳은 **R7**(atomics · race=UB)이지 여기가 아니다.

**DET-1 — disjoint split 이면 병렬 = 순차.** "disjoint" 의 정확한 내용이 Bernstein 조건이다:

```coq
Definition indep (t1 t2 : task) : Prop :=
  disj (wr t1) (wr t2) /\ disj (wr t1) (rd t2) /\ disj (rd t1) (wr t2).

Theorem det1_par_eq_seq : forall ts s, Forall local ts -> pw ts ->
  forall i, par_run ts s i = seq_run ts s i.
```
> 병렬(모두가 **원래 상태**를 읽고 쓰기를 합침) = 순차(뒤가 앞의 쓰기를 봄).
> **"비트 동일" 은 문자 그대로다** — 근사도 재결합도 오차 허용도 없이 *같은 값*이다.

**그리고 disjoint 가 왜 필요한지도 증명한다** — 없으면 DET-1 은 장식이다:

```coq
Theorem overlap_is_nondeterministic :
  ~ indep writes_1 writes_2 /\
  seq_run [writes_1; writes_2] zero 0 <> seq_run [writes_2; writes_1] zero 0.
```

**DET-2 — 충돌하는 쌍의 순서만 지키면 스케줄은 자유다.**

```coq
Theorem det2_schedule_free : forall ts ts', sched_equiv ts ts' -> Forall local ts ->
  forall s i, seq_run ts s i = seq_run ts' s i.
```
> `sched_equiv` = 인접한 **독립** 태스크 교환의 반사·추이 폐포.
> 스케줄러의 자유도가 **정확히** 이만큼임을 못 박는다 — 그 이상도 이하도 아니다.

**DET-3 — reduction tree 를 왜 고정해야 하는가.** 축약 연산이 결합적이지 **않으면** 트리 모양이
결과를 바꾸기 때문이다. IEEE 를 공리로 들여오지 않고, 비결합 연산의 대표(정수 뺄셈)로 기계 검증했다:

```coq
Theorem assoc_shape_free : forall op, assoc op ->
  forall t1 t2, leaves t1 = leaves t2 -> red op t1 = red op t2.   (* 결합적 → 트리 자유 *)

Theorem nonassoc_shape_matters :                                   (* 비결합 → 트리 고정 필수 *)
  ~ assoc sub /\ leaves left_tree = leaves right_tree /\ red sub left_tree <> red sub right_tree.
```
> ⇒ 정수 `add`·`min`·`max`·`bit_or` 는 트리를 스케줄러가 골라도 된다.
> **부동소수 `add`·`mul` 은 프로그램이 트리를 고정해야 한다.**
> RFC-0009 가 "고정 reduction tree" 라고만 부르던 것의 *이유*가 이것이고,
> RFC-0053 의 `sum`(Neumaier) vs `sum_fast` 구분과 **같은 뿌리**다 —
> 부동소수의 결합 순서는 **의미의 일부**다.

**정직한 한계:** level-1 병렬은 **아직 구현되지 않았다**(MVP 제외). 그러므로 이 파일은 구현을
검증하지 않는다 — 구현이 **지켜야 할 조건을 확정한다.** 무엇을 disjoint 라 불러야 하는지,
어떤 축약이 트리를 고정해야 하는지가 이제 정리로 못 박혀 있다. 구현이 오면 이것을 검사하면 된다.

### `LowentDRF.v` — R7 의 심장: 안전 코드에는 **경합이 없다**

RFC-0018 §8-1 은 *"안전 코드 race-free ⟹ DRF-SC 는 DRF1~4 로 **스케치**. 기계 증명은
iGPS/Cosmo 로"* 라고 남겨 뒀다. 그런데 이 명제는 **RC11 도 Iris 도 필요 없다.**

"안전 코드에 경합이 없다" 는 메모리 모델에 대한 주장이 아니라 **규율에 대한 주장**이다:

```coq
(* level 1 — disjoint split: Bernstein 조건이면 어떤 자리에서도 경합하지 않는다 *)
Theorem l1_no_race : forall t1 t2 l, indep t1 t2 -> ~ races t1 t2 l.

(* level 2 — actor 격리: 서로 다른 액터의 두 접근 사이에는 **반드시 메시지가 있다** *)
Theorem l2_accesses_are_separated_by_a_message :
  wf o (tr1 ++ Access t1 l w1 :: tr2 ++ Access t2 l w2 :: tr3) -> t1 <> t2 ->
  has_send l tr2 = true.
```
> 메시지는 happens-before 간선이므로(RFC-0009 MHB), 두 접근은 hb 로 순서지어진다 —
> 그리고 hb 로 순서지어진 두 접근은 **정의상 경합이 아니다.**
> 열쇠 보조정리: `owner_stable` — **메시지가 없으면 소유권은 바뀌지 않는다.** 접근은 소유권을
> 옮기지 못한다. 그래서 소유자가 바뀌었다면 그 사이에 메시지가 있었다.

그리고 두 조건이 **필요**하다는 것도 보인다(`l1_condition_is_necessary` ·
`unsynchronised_sharing_is_not_wf`) — 규율을 빼면 바로 경합이다. 조건은 장식이 아니다.

⇒ **RFC-0018 §6.5 "동시성 복잡도 pay-as-you-go" 가 정리 위에 선다:**
메모리 모델을 만나려면 level 3 으로 **내려가야만** 한다. 그 위에서는 만날 수가 없다 —
경합이 없기 때문이다. 이것이 R7 이 왜 "가둠"(containment)인가에 대한 답이다.

### `LowentLock.v` — Iris 가 **실제로 필요한 유일한 자리**

level 3 의 lock 은 다르다. 두 스레드가 **같은 자리(lock word)를 정말로 동시에 두드린다.**
여기서는 규율이 아니라 **불변식**이 안전을 지킨다 — 그것이 동시성 분리논리의 일이고, Iris 다.
RFC-0018 §6.4 는 *"lock 은 atomic CAS 위의 라이브러리"* 라고만 적어 뒀다. 그 말이 참인지 증명한다.

```coq
Definition acquire : val := rec: "acq" "l" := if: try_acquire "l" then #() else "acq" "l".

Lemma acquire_spec γ lk R : {{{ is_lock γ lk R }}} acquire lk {{{ RET #(); locked γ ∗ R }}}.
Lemma release_spec γ lk R : {{{ is_lock γ lk R ∗ locked γ ∗ R }}} release lk {{{ RET #(); True }}}.

(* ★ 상호배제의 본질 — 열쇠는 **복제 불가능**하다 *)
Lemma locked_exclusive γ : locked γ -∗ locked γ -∗ False.
```
> 두 스레드가 동시에 lock 을 잡았다고 주장하면 **False 가 나온다.** 그것이 전부다.
> 클라이언트(`two_threads_spec`)에서 두 스레드가 같은 카운터를 만져도 경합이 없다.
> `Print Assumptions` → **Closed under the global context** (Iris 자체가 공리 없이 세워져 있다).

**★ 이것이 `LowentPar.v` 의 `overlap_is_nondeterministic` 과 짝을 이룬다:**
겹치는 쓰기는 순서가 결과를 바꾼다 — **lock 없이는.** lock 이 그 순서를 다시 정한다.
level 1(disjoint)로 피할 수 있으면 피하고, 못 피하면 level 3 에서 lock 으로 값을 치른다.
RFC-0018 의 *"lock 은 최후 수단"* 이 그 뜻이다.

**★ 정직한 범위:** heap_lang 은 **순차 일관성(SC)** 모델이다. RC11 의 약한
ordering(relaxed·acquire·release)은 여기서 다루지 않는다 — iGPS/Cosmo 가 필요하고,
RFC-0018 §8-1 이 그렇게 적어 뒀다. 그러나 그것으로 **대부분이 덮인다**: 우리 언어의 기본
ordering 은 **seq_cst** 이므로(RFC-0018 §6.1) 기본값으로 쓰는 level-3 코드는 정확히 SC 모델
안에 있다. 약한 ordering 을 **명시해서** SC 를 벗어나는 코드만 iGPS/Cosmo 의 몫으로 남고,
그 코드는 audit 에 기록된다(G4) — **어디가 아직 증명 밖인지가 소스에 보인다.**
