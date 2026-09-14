# `spsc` — lock-free SPSC 링 버퍼

> `lib/spsc.low` · RFC-0018 §8-3 · 증명 근거 `docs/proofs/coq/LowentIRC11.v`·`NumericLattice.v`

## 왜 있는가

스레드 사이로 값을 넘기는 가장 단순한 방법은 **락**이다. 그런데 생산자가 하나, 소비자가
하나뿐이면 락이 필요 없다 — 두 쪽이 **서로 다른 색인**만 쓰기 때문이다.

```
생산자만 쓴다:  tail        소비자만 쓴다:  head
서로의 것은 **원자적으로 읽기만** 한다.
```

이것이 SPSC(single-producer single-consumer)이고, **lock-free 중 유일하게 정직하게 짧은 것**이다.

## 설계 의도와 경계

- **한 칸을 늘 비워 둔다.** 그래서 실제 용량은 `len buf − 1` 이다. 그 대가로 `head == tail` 이
  **언제나** "비었다" 를 뜻한다 — 가득 참과 헷갈리지 않는다.
- **op 이 셋뿐이다**(`push`·`pop`·`capacity`, 그리고 관찰용 `count_about`).
  `peek`·`drain` 을 넣고 싶어지지만 넣지 않았다: SPSC 의 계약은 *"생산자 하나·소비자 하나"* 이고,
  **편의 op 이 늘수록 그 계약을 어기는 방법이 늘어난다**(두 곳에서 peek 하면 이미 SPSC 가 아니다).
  ★ 표준 라이브러리에서 **작은 표면은 게으름이 아니라 계약을 지키는 수단**이다.
- **MPSC/MPMC 는 넣지 않았다.** CAS 재시도 루프와 ABA 문제가 붙고, 그러면 *"표준에 넣는다"* 가
  **검증되지 않은 코드를 표준으로 만든다**는 뜻이 된다. 짧은 것부터 넣는다.

## 무엇이 이 코드를 받치나 — 증명 둘

| 무엇 | 어디 |
|---|---|
| **인덱스 안전** — 색인을 `mod … (len buf)` 로 감싸면 언제나 범위 안이다 | `mod_is_a_safe_index`(Qed) |
| **동기화** — 데이터 쓰기 → 색인 공개 → 색인 관찰 → 데이터 읽기가 **RC11 에서** 데이터를 나른다 | `LowentIRC11.v`(Qed) |

첫째는 실측으로 확인된다. `lowentc --emit-proof lib/spsc.low` 가 이렇게 낸다:

```
lowproof 1
# proven 4 certified 4
spsc_push 31 add.i64 R-ADD-LENLT 0 9223372036854775807 1 1 64 0
spsc_push 49 index.store R-IDX-LENLT 0 281474976710655
spsc_pop 51 index R-IDX-LENLT 0 281474976710655
spsc_pop 57 add.i64 R-ADD-LENLT 0 281474976710655 1 1 64 0
```

⇒ 링 버퍼의 **두 접근 모두 경계 검사가 증명으로 사라졌고**(`index`), 색인을 하나 올리는 덧셈의 넘침 검사도
함께 사라졌다(`add.i64`). 그 근거가 증명서로 남는다(RFC-0086). (2026-09-13 다시 잼 — 증명 수가 2 에서 4 로 늘었고
색인 상한이 슬라이스 길이의 실제 한계로 좁혀졌다.)

## ★★ 증명된 패턴 **그대로** 돈다 — 그리고 그 과정에서 도구의 구멍을 찾았다

이 문서의 초판은 *"약한 ordering 표기는 아직 언어에 없다"* 고 적었다. **틀렸다.**
있었다 — `order <name>` 절이(RFC-0018 §6.1). 다만 파서가 **arity 로 중첩**하는 바람에
`order acquire` 가 **형제**로 밀려나 `E-IR-ARITY: extra operands` 로 거절됐고, 그래서
**한 번도 닿지 못했다.** 2026-07-31 에 형제 자리에서 받도록 고쳤다.

> ★ *증명해 놓고 도구가 안 준* 자리였다(W-NOT-YET). 이 라이브러리를 쓰려다 드러났다 —
> **쓰지 않는 기능은 있어도 없는 것이다.**

지금 네 자리는 이렇게 돈다:

| 자리 | ordering | 왜 |
|---|---|---|
| 생산자가 head 를 읽는다 | **acquire** | 소비자가 공개한 것 — 동기화가 필요하다 |
| 생산자가 tail 을 읽는다 | **relaxed** | **내가 쓴 값**이다 — 남과 맞출 것이 없다 |
| 생산자가 tail 을 공개한다 | **release** | 앞의 데이터 쓰기가 먼저 보여야 한다 |
| 소비자 쪽 | 대칭 | head=relaxed·release · tail=acquire |

⇒ 동기화가 필요한 자리는 넷 중 **둘**뿐이고, 나머지 둘은 relaxed 로 **값을 치른다**.
방출된 C 를 실측하면 `lw_at_rmw(…,0)`·`(…,1)`·`(…,2)` 가 그대로 나온다.

★ 그리고 seq_cst 로 두었던 초판이 **틀린 것은 아니었다**: `sc_is_the_strongest` + 단조성이
*"센 쪽이 맞으면 맞다"* 를 준다. 지금은 **정확히 필요한 만큼만** 세다.

## op 한눈에

| op | 하는 일 | 돌려주는 것 |
|---|---|---|
| `spsc_capacity buf` | 담을 수 있는 최대 개수 | `len buf − 1` |
| `spsc_push k ctl buf v` | 밀어 넣기(**생산자 전용**) | 1 성공 · 0 가득 참 |
| `spsc_pop k ctl buf out` | 꺼내기(**소비자 전용**) · 값은 `out[0]` 에 | 1 성공 · 0 비었음 |
| `spsc_count_about k ctl buf` | 지금 개수(**관찰값**) | 개수 |

`ctl` 은 길이 2 이상의 `mut slice u64` — `[0]`=head(소비자) · `[1]`=tail(생산자).
`k` 는 `cap atomic` — 공유 자리를 원자적으로 만질 권한이다.

★ `count_about` 의 이름에 `about` 이 붙은 이유: **읽는 순간 이미 달라졌을 수 있다.**
이름이 그것을 말하지 않으면 쓰는 사람이 그 값을 믿는다.

## 사용법

```lowent
use spsc .

rem 생산자 쪽
let ok u64 be spsc.spsc_push k ctl buf 42 .
guard eq ok 1 . else … rem 가득 참

rem 소비자 쪽
let got u64 be spsc.spsc_pop k ctl buf out .
guard eq got 1 . else … rem 비었음
rem 값은 out[0] 에 있다
```

## 시험

`impl/tests/vm_spsc.low` 가 계약 넷을 잰다 — 빈 것에서 꺼내기 실패 · 가득 참에서 넣기 실패 ·
FIFO · **링을 한 바퀴 넘겨도** 그대로. 실측: roundtrip 7 · 용량 3(버퍼 4칸) · 링 20회 어긋남 0.

★ 그 시험은 **단일 스레드**다(VM 이 단일 스레드다). 진짜 두 스레드는 네이티브에서만 뜻이 있고,
그때 필요한 **메모리 순서의 정당성은 실행이 아니라 증명이 준다.** 그 둘을 섞지 않는다.

## 아직 없는 것

- **MPSC/MPMC** · **seqlock**(RFC-0018 §8-3 의 나머지).
- ~~약한 ordering 표기~~ ★ **있다**(위 참조) — `order relaxed/acquire/release/acq_rel/seq_cst`.
- ~~**알고리즘 자체의 기계 증명**~~ ★ **했다**(`docs/proofs/coq/LowentSPSC.v`, 2026-07-31).
  그 길을 걸어 보니 gpfsl 의 `circ_buff` 가 **우리 알고리즘 그 자체**였다 — 인덱스를 `mod` 로
  감싸는 것, 가득 참을 `w' = r` 로 판정하는 것(한 칸 비우기), 데이터를 **먼저** 쓰고 색인을
  **release 로** 공개하는 것까지 **한 줄씩 같다.**

  > `lowent_spsc_push_is_correct` — 성공하면 자원이 버퍼로 **넘어가고**, 실패(가득 참)하면
  > **되돌아온다**. *"넣었는데 사라졌다"* 도 *"실패했는데 뺏겼다"* 도 없다.
  > `lowent_spsc_pop_is_correct` — 성공하면 **생산자가 넣을 때의 그 자원**을 받는다.
  > (`Print Assumptions` = Closed under the global context)

  ★ **증명은 빌렸다**(gpfsl). 그리고 가장 큰 간극도 적어 둔다: 우리 `spsc_push` 가 gpfsl 의
  `try_prod` 와 **한 줄씩 같다는 것은 사람이 읽어서 확인한 것**이고, 그 대응은 기계 검증되지
  않았다 — 12장 ⑥-5 와 **같은 종류**의 간극이다.
- **MPSC/MPMC·seqlock** · 진행성(progress)은 여전히 없다.
