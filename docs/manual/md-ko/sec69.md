# <a id="mod-searchlib"></a>`searchlib` — 정렬된 슬라이스의 이진 탐색

소스

`lib/search.low`

층

L0 — 순수 계산

권한

없음

**정렬된** 슬라이스에서 값을 이진 탐색으로 찾는다. 가운데를 찍어 보고 찾는 값이 왼쪽인지 오른쪽인지 정한 뒤 남은 절반만 다시 뒤지므로, 원소가 백만 개여도 스무 번쯤이면 끝난다. 대신 미리 줄이 서 있어야 방향을 정할 수 있다. [`sortlib`](sec67.md#mod-sortlib) 의 짝이고, 둘이 함께 **정렬된 집합 · 맵**을 만든다 — 해시맵과 달리 순서를 지키므로 범위 질의와 “다음 큰 값” 이 된다. 파일 이름은 `search.low` 이지만 모듈 이름은 `searchlib` 이다.

```lowent
use searchlib .
use sortlib .

sortlib.sort s .
let i option u64 searchlib.bsearch s 42 .
```

> **정렬을 검사하지 않는다**
>
> > `s` 가 오름차순이라는 전제는 **호출자의 몫**이다. 전제가 깨지면 답이 틀리지만 멈추지는 않는다 — 범위 밖을 만지지 않으므로 멈출 일이 없고, 대신 **조용히 틀린 답**이 나온다. “분명히 있는 값인데 `none`” 이면 십중팔구 정렬을 하지 않은 것이다.

| **op** | **모양** | **답** |
|---|---|---|
| `bsearch` | `(s slice u64, target u64) → option u64` | `target` 의 인덱스, 없으면 `none`. 중복이면 그중 **하나**(어느 것인지 규정하지 않는다) |
| `lower_bound` | `(s slice u64, target u64) → u64` | `target` 이상인 첫 인덱스. 모두 작으면 `len s` |

*표 50.1 — `searchlib` 의 op — 모두 `fn` · `effects none`*

고르는 법 — “있나 없나, 있으면 어디” 는 `bsearch`, “여기 끼우면 어디” 또는 “이 값 이상이 어디부터” 는 `lower_bound` 다. `lower_bound` 는 **삽입점**이자 **범위의 시작**이다 — `[lower_bound s a, lower_bound s b)` 가 `a ≤ x < b` 인 원소들의 인덱스 구간이다. `option` 이 아닌 까닭은 “못 찾음” 이 없는 물음이기 때문이다(끝 인덱스도 답이다). upper_bound · 삽입 · 삭제는 없다 — 하한 하나로 포함 여부 · 삽입점 · 범위 질의가 다 선다.

구현은 반열린 구간 `[lo, hi)` 로 돌고 중점은 `lo + (hi − lo)/2` 로 잡는다 — `(lo + hi)/2` 는 큰 인덱스에서 넘칠 수 있다(고전적 결함).

```lowent
fn count_in input s slice u64 . input a u64 . input b u64 . output u64 . do
  let i u64 searchlib.lower_bound s a .
  let j u64 searchlib.lower_bound s b .
  guard lt i j else return 0 .
  return sub j i .
end
```

> **반례. `bsearch` 의 `none` 을 가르지 않는다**
>
> > `return some_value r .` 는 컴파일은 통과하고, 못 찾은 순간 `E-VM-NONE` 으로 멈춘다. 있는 값만 넣어 시험하면 걸리지 않는다 — `option` 을 보면 `guard is_some` 을 붙인다(11장).

> **반례. 중복 원소에서 `bsearch` 인덱스에 뜻을 싣는다**
>
> > `[5,5,5]` 에서 `bsearch s 5` 는 0 · 1 · 2 중 어느 것이든 낼 수 있다. “첫 5 의 자리” 가 필요하면 `lower_bound s 5` 다.

> **반례. `lower_bound` 결과로 곧장 `idx` 한다**
>
> > 못 찾으면 `i` 가 `len s` 이고 그 자리는 없는 자리다 — `idx s i` 가 `E-VM-BOUNDS` 로 멈춘다. 값이 표의 최댓값보다 클 때만 터지므로 실제 데이터에서 걸리기 쉽다. `guard lt i (len s)` 를 먼저 둔다.

**주의.** 내림차순 자료에는 그대로 쓸 수 없다(비교자를 바꿔 끼울 자리가 없다). **정렬은 한 번, 탐색은 여러 번** — 찾을 때마다 `sort` 를 부르면 이득이 통째로 사라진다. 정렬 뒤에 원소 하나를 `set` 으로 바꾸면 다시 정렬해야 한다. 하한 둘의 차를 뺄 때는 순서를 지킨다 — `sub` 이 아래로 넘친다. 순수 op 이라 같은 슬라이스를 여러 곳에서 동시에 탐색해도 된다(쓰는 곳이 없을 때). 다른 원소 타입은 [`sortgen`](sec68.md#mod-sortgen) 의 `lower_by` · `find_by` 다.

---

[← 이전](sec68.md) · [목차로](README.md) · [다음 →](sec70.md)
