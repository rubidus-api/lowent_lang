# budget — 핸들 bit 예산과 wrap 정책

소스: `lib/budget.low` · 모듈명 `budget` (RFC-0104 §8-3, 소유자 서명 2026-08-28 · §5.14)

## 처음 쓰는 사람에게

**무엇을 하는 모듈인가.** 세대 핸들을 **한 word** 에 넣을 때 *"세 조각에 몇 비트씩 주나"* 를
정하고, 그 예산이 **들어맞는지 컴파일타임에** 확인한다. 세 조각은 이렇다.

```text
h = (generation << (shard_bits + slot_bits)) | (shard << slot_bits) | slot
      몇 번째 삶            어느 샤드                        어느 칸
```

**언제 쓰나.** 저장소를 직접 만들 때. `pool`·`shard` 처럼 칸을 나눠 주고 되받는 것을 지을 때
핸들의 폭을 정해야 하는데, 그 수를 **소스에 적고 도구가 지키게** 하는 자리가 여기다.

**최소 예제.**

```lowent
use budget .

let p budget.plan be budget.default64 .              rem 32 · 8 · 24 (권고 기본값)
let ho option u64 . be budget.pack 32 8 24 7 1 0 .   rem 칸 7 · 샤드 1 · 세대 0
guard is_some ho . else return 1 .
let h u64 be some_value ho .
rem 되꺼낼 때도 **같은 예산**으로 묻는다
let slot u64 be budget.slot_of 32 h .
```

> ### ⚠ 이 모듈이 지키는 것
>
> **들어가지 않는 예산은 컴파일이 안 된다.** `budget.pack 40 16 16 …` 은 합이 72 라
> 한 word 를 넘는다 — 상수뿐이므로 **부르는 자리에서** `E-CONTRACT-IMPOSSIBLE` 이다.
> 전에는 이런 프로그램이 초록으로 컴파일되고 **실행 때** 터졌다.
>
> **값이 자기 칸에 안 들어가면 `none`.** 조용히 잘라 담으면 서로 다른 두 핸들이 같아진다.
>
> **세대가 한 바퀴 돌면 그 칸은 은퇴한다.** `next_gen` 이 `none` 을 답하고 되살리지 않는다 —
> 0 으로 돌아가면 **그 칸의 옛 핸들이 전부 되살아난다**. 세대 핸들이 막으려던 바로 그 일이다.

## 권고 기본값과 그 근거

| 기계 | slot · shard · gen | 담는 수 | 근거(측정) |
|---|---|---|---|
| 64 bit | **32 · 8 · 24** | 4.3e9 칸 · 256 샤드 · 1.7e7 삶 | 가장 넓은 실물 id 가 SQLite 페이지 **32 bit** |
| 32 bit | **16 · 4 · 12** | 65,536 칸 · 16 샤드 · 4,096 삶 | lwIP 가 길이를 **16 bit** 로 센다 |

측정과 출처(고정 commit·줄 번호)는 개발 기록(비공개 벤치마크 문서)에 있다.
**기본값일 뿐이다** — 저장소가 자기 수를 적으면 그 수가 이긴다.

## op 한눈에

| op | 시그니처 요약 | 실패 시 |
|---|---|---|
| `default64` / `default32` | → `plan` | 없음 |
| `pow2` | `n u64` → `u64` (계약: `n < 63`) | 계약 위반은 진입 트랩 |
| `slots` / `shards` / `lives` | `p plan` → `option u64` | 폭이 63 이상이면 `none` |
| `pack` | 예산 셋 + 값 셋 → `option u64` | 값이 칸에 안 들어가면 `none` · 예산이 넘치면 **컴파일 에러** |
| `slot_of` / `shard_of` / `gen_of` | 예산 + `h` → `u64` | 계약 위반은 진입 트랩 |
| `retired` | `gen_bits`, `gen` → `bool` | 없음 |
| `next_gen` | `gen_bits`, `gen` → `option u64` | 은퇴한 칸이면 `none` |

## 반례 — 이렇게 쓰면 안 된다

```lowent
rem ✗ 한 word 에 안 들어가는 예산
let h option u64 . be budget.pack 40 16 16 1 1 1 .
```

증상: **컴파일 에러 `E-CONTRACT-IMPOSSIBLE`** — 40+16+16 = 72 다. 실행까지 가지 않는다
(음성 픽스처 `impl/tests/vm_budget_over.low`).

```lowent
rem ✗ 세대를 돌려 쓴다
let g u64 be add gen 1 .        rem 은퇴 판정 없이 그냥 올린다
```

증상: 에러는 없지만, 폭을 넘으면 **다른 칸의 핸들과 같아진다**. `next_gen` 을 쓰고 `none` 을
받으라 — 그 칸은 끝났다는 뜻이다.

```lowent
rem ✗ 63 비트 칸을 세려 한다
let n u64 be budget.pow2 63 .
```

증상: **진입 계약이 거절한다.** `shl 1 63` 은 부호 있는 64 비트에서 넘쳐 음수가 된다 —
조용히 틀린 수를 돌려주느니 거절한다.

## 주의사항

- **되꺼낼 때 같은 예산을 준다.** `pack` 과 `slot_of` 가 다른 수를 보면 답이 조용히 틀린다.
  예산을 상수 셋으로 한 자리에 적어 두고 그것만 쓰는 편이 낫다.
- **이름이 `pow2` 인 이유**: `cap` 은 **문법 자리**다(`input k cap clock .`). op 이름으로 쓰면
  `lib/clock.low` 과 한 단위에 못 선다(실측). 흔한 낱말·문법 자리의 낱말은 op 이름으로 쓰지 않는다.
- **두 word 핸들은 안 짓는다.** 필요해지면 그때 적는다 — 지금은 한 word 만 잰다.
