# <a id="mod-collection"></a>`collection` — 원소마다 옮기기 · 거르기 · 접기

소스

`lib/collection.low`

층

L0 — 순수 계산

권한

없음

한 조각의 원소마다 같은 일을 해서 다른 조각에 담거나(`map`), 조건에 맞는 원소만 담거나(`filter`), 하나의 값으로 접는다(`fold`). 할 일은 **원소의 타입이 들고 온다** — 원소를 칸 하나짜리 구조체로 감싸고, 그 구조체가 트레이트 `mappable` · `keepable` · `foldable` 을 갖춘다. [`sortgen`](sec68.md#mod-sortgen) 이 비교를 원소 타입에서 찾는 것과 같은 길이다. 원소마다 이름 붙은 op 하나를 부르는 일이면 `pipe` 의 스테이지가 더 짧다(24장).

```lowent
use collection .

struct bump do
  satisfies collection.mappable .
  satisfies collection.foldable .
  v u64 .
end .

fn bump.apply input x bump . output bump . do
  return lit bump do v add field x. v . 1 . . end . .
end .

fn bump.combine input acc bump . input x bump . output bump . do
  return lit bump do v add field acc. v . field x. v . . . end . .
end .

proc bumped_sum input s slice bump . input out mut slice bump . output u64 . effects none . do
  let n u64 collection.map bump. s. out. . .
  let total bump collection.fold bump. lit bump do v 0 . end . out. . .
  return add n. field total. v . . .
end .
```

| **op** | **모양** | **실패** |
|---|---|---|
| `mappable` · `keepable` · `foldable` | trait — `apply (self) → self` · `keep (self) → bool` · `combine (self, self) → self` | 미충족 = `E-BOUND-UNSAT`(컴파일) |
| `map` | `(comptime t, slice t, mut slice t) → u64` — `dst[i]` 에 `src[i]` 의 `apply` | `dst` 가 짧으면 쓰려는 순간 멈춘다 |
| `filter` | `(comptime t, slice t, mut slice t) → u64` — `keep` 이 참인 원소를 앞에서부터 | 같다 |
| `fold` | `(comptime t, start t, slice t) → t` — 누산값이 먼저, 원소가 다음 | 없음 — 비면 `start` |

*표 50.1 — `collection` 의 op*

`map` 과 `filter` 는 **쓴 원소의 수**를 낸다(`collect into` 와 같은 `u64`). 저장은 부르는 쪽이 준다 — 이 모듈은 할당하지 않는다.

**왜 기본 연산이 아닌가.** 이 셋은 2026-10-16 까지 기본 연산이었다. 기본 연산의 이름은 저자가 쓸 수 없으므로 `map` 이라는 지역도 op 도 지을 수 없었고, [`mapgen`](sec112.md#mod-mapgen) 이 `table` 이라는 이름을 고른 것도 그 때문이다. 라이브러리로 옮기면서 그 이름이 풀렸다.

**왜 op 을 값으로 넘기지 않나.** 이 언어에는 op 을 값으로 넘기는 길이 없다. 있어도 간접 호출이 생겨 비용이 호출 자리에서 숨는다. 부를 때마다 그 타입 전용 op 이 만들어지고(단형화) `apply` 는 직접 호출로 박힌다. 그 대가로 **한 원소 타입에는 `apply` 가 하나**다 — 같은 바이트를 두 가지로 옮기려면 감싸개 타입이 둘 있어야 한다. 바이트 조각을 감싸개 조각으로 볼 때는 `view_array <감싸개> <조각>` 을 쓴다(칸 하나짜리라 배치가 같다).

> **반례. `u64` 조각을 그대로 넘긴다**
>
> > `collection.map u64. s. out. .` 은 `E-BOUND-UNSAT` 이다. `u64` 에는 메서드를 붙일 자리가 없어 트레이트를 갖출 수 없다 — 칸 하나짜리 구조체로 감싼다.

**짓지 않은 것** — 원소 타입을 바꾸는 `map`(`t` 에서 `u` 로), 상태를 지닌 변환(곱할 수 `k` 같은 것). 둘 다 트레이트가 타입 매개변수를 받거나 op 을 번역 시점 인자로 받아야 한다 — 그 문법은 다음 일로 남겨 두었다.

---

[← 이전](sec68.md) · [목차로](README.md) · [다음 →](sec70.md)
