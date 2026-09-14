# segarena — 고정 크기 세그먼트 아레나

소스: `lib/segarena.low` · 모듈명 `segarena` (RFC-0104 §8-5, 소유자 서명 2026-08-28 · §5.15)

## 처음 쓰는 사람에게

**무엇을 하는 모듈인가.** 저장소를 한 덩어리로 잡지 않고 **같은 크기의 조각(segment)** 으로
늘려 간다. 조각 안은 연속이라 훑기가 빠르고, 자라날 때 **이미 있는 것을 옮기지 않는다**.

색인 하나가 조각과 자리로 갈린다 — 그게 이 모듈의 전부다.

```text
segment = i >> kbits          slot = i & ((1 << kbits) - 1)
address = directory[segment] + slot
```

**최소 예제.**

```lowent
use segarena .

newtype grid u8 .                                  rem 저장소 브랜드(§8-2)

var a segarena.arena grid . be segarena.open grid 4 .    rem 조각당 16 칸
let b option u64 . be segarena.grow grid a dir 1024 .    rem 조각을 하나 연다
guard is_some b . else return 1 .
let v option u64 . be segarena.at grid a dir mem 9 .     rem 전역 색인으로 읽는다
```

> ### ⚠ 비용이 **op 이름에 적혀 있다** — 이것이 이 모듈의 요점이다
>
> | 훑는 방법 | 조각마다 directory 를 몇 번 보나 | 이 기계에서 잰 시간 |
> |---|---:|---:|
> | `sum_seg` (조각을 한 번 빌린다) | **1**(루프 밖) | **21 ms** |
> | `sum_global` (전역 색인으로 매번) | 원소마다 **1** | **81 ms** |
>
> 2,048만 원소를 훑은 수이고 **답은 같다**(측정: `docs/bench/capacity.md` §⑤).
> ★ 컴파일러는 세그먼트를 **모른다**. 빠른 쪽을 고르는 것은 **당신이 부르는 op** 이다 —
> RFC-0104 §5.5 가 *"전역 random access 의 비용을 평평한 slice 처럼 숨기지 않는다"* 고
> 적은 그대로다. 그래서 새 낱말도 새 내장 연산도 **하나도 안 늘었다**(어휘 43 · 빌트인 182).

## op 한눈에

| op | 하는 일 | 실패 시 |
|---|---|---|
| `open` | 아레나를 연다(`kbits` = 조각당 `2^kbits` 칸) | 계약: `1 ≤ kbits ≤ 20` — **상수면 컴파일타임 거절** |
| `grow` | 조각을 하나 더 연다 · `dir` 에 시작 자리를 적는다 | 자리가 없거나 용량을 넘으면 `none` |
| `seg_slots` | 조각당 칸 수 | — |
| `seg_of` / `slot_of` | 전역 색인 → 조각 / 자리 | — |
| `at` | **전역 색인**으로 한 칸 읽기(편하다 · directory 를 매번 본다) | 범위 밖·안 연 조각이면 `none` |
| `walk_base` | 조각의 시작 자리를 **한 번** 빌린다(뜨거운 순회용) | 안 연 조각이면 `none` |
| `sum_seg` / `sum_global` | 같은 합을 두 경로로 — 비용 대조의 증인 | — |

## 반례 — 이렇게 쓰면 안 된다

```lowent
rem ✗ 조각 크기가 뜻이 없다
var a segarena.arena g . be segarena.open g 0 .
```

증상: **컴파일 에러 `E-CONTRACT-IMPOSSIBLE`** — 조각이 한 칸이면 세그먼트가 아니다. 인자가
상수라 부르는 자리에서 판정된다(§8-3 이 컴파일러에 더한 규칙 · 음성 픽스처 `vm_segarena_bad.low`).

```lowent
rem ✗ 뜨거운 순회를 전역 색인으로 돈다
while lt i n . do
  let v option u64 . be segarena.at grid a dir mem i .   rem 원소마다 directory
  ...
end .
```

증상: 에러는 없다 — **느릴 뿐이다**(이 기계에서 3.9 배). 조각 단위로 돌면서 `walk_base` 로
시작 자리를 한 번만 빌리라.

## 주의사항

- **저장은 호출자 것이다.** `dir`(조각마다 시작 자리)과 `mem`(칸이 사는 곳)을 밖에서 준다.
  아레나는 커서와 모양만 든다 — `pool`·`shard` 와 같은 규율이다.
- **회수는 일괄이다.** 조각 하나만 돌려주는 길은 없다(그것이 아레나의 뜻이다).
  개별 회수가 필요하면 [`pool`](pool.md) 이 그 자리다.
- **가변 크기 조각은 안 짓는다.** 그러면 `i >> k` 공식이 안 서고 prefix directory·트리 조회가
  필요하다 — 그때는 비용이 달라지므로 **다른 모듈**이어야 한다.
- 조각을 **빌리는 뷰**(`segments T`·커서)는 RFC-0104 §8-8 이 맡는다. 여기서는 안 짓는다.
