# searchlib — 정렬된 슬라이스의 이진 탐색 (`lib/search.low`)

## 처음 쓰는 사람에게

**무엇을 하는 모듈인가.** **정렬된** 슬라이스에서 값을 이진 탐색으로 찾는다.

슬라이스(slice)는 "어떤 배열의 어디부터 어디까지" 를 가리키는 창이다 — 값을 복사해 갖지
않고 남의 메모리를 들여다볼 뿐이다([5장](../05-arrays-and-slices.md)). 이진 탐색은 가운데를
찍어 보고 찾는 값이 왼쪽인지 오른쪽인지 정한 뒤 남은 절반만 다시 뒤지는 방법이다 — 그래서
원소가 백만 개여도 스무 번쯤이면 끝난다(O(log n)). 대신 **미리 줄이 서 있어야** 가운데를
보고 방향을 정할 수 있다.

**언제 쓰나.** 정렬해 둔 표에서 빠르게 찾아야 할 때 쓴다. 정렬이 안 돼 있으면 답이 틀린다.

**최소 예제.** 아래가 이 모듈을 쓰는 가장 짧은 꼴이다. 각 줄이 무엇을 하는지는 주석에 적었다.

```lowent
use searchlib .
use sortlib .

sortlib.sort s .                            rem ① 먼저 정렬한다 — 이것이 전제다
let i option u64 . be searchlib.bsearch s 42 .   rem ② 그 다음 찾는다
```

두 번째 줄의 `option u64`(있을 수도 없을 수도 있는 u64)는 "찾았으면 인덱스, 못 찾았으면
`none`" 을 한 값으로 답하는 타입이다 — 꺼내 쓰기 전에 `guard is_some …` 로 갈라야 한다
([7장](../07-guard-and-option.md)).

**읽는 순서.** 급하면 «op 한눈에» 표만 봐도 된다. 왜 이렇게 생겼는지가 궁금하면 «왜 있는가»부터, 실제로 쓰다 막히면 «반례»와 «주의사항»을 먼저 보면 된다.

## 왜 있는가

이 절은 **이 모듈이 왜 따로 있고 왜 빌트인이 아닌지**를 말한다. 한 줄로는: 정렬의 짝이고,
언어가 이미 가진 것만으로 지을 수 있어서 라이브러리다.

정렬(`lib/sort.low`)의 짝이다(소스 상단 rem) — 정렬해 두면 이진 탐색이 O(log n) 에
찾는다. 둘이 함께 **정렬된 집합/맵**을 만든다: 해시맵([`hashmap`](hashmap.md))과 달리
**순서**를 지키므로 범위 질의·"다음 큰 값" 이 된다.

순수 계산이고 언어가 이미 가진 것(`len`·`index`·`while`·`option`)으로 지어진다 ⇒
빌트인이 아니라 라이브러리다. 빌트인 op 증가 **0**.

## 설계 의도와 경계

이 절은 **이 모듈이 해 주는 것과 일부러 안 해 주는 것**의 목록이다. 안 해 주는 쪽을 먼저
알아야 나중에 안 놀란다.

**하는 것** — 오름차순 정렬된 `slice u64` 에서 정확한 조회(`bsearch`)와
하한(`lower_bound`). 전부 `effects none`, 할당 0, 쓰기 0.

여기서 effect(효과)는 **그 op 이 바깥세상에 무슨 비용을 내는지**를 시그니처에 적어 두는
선언이다. `effects none` 은 "계산만 한다 — 파일도 화면도 안 건드리고, 몰래 메모리를 얻지도
않는다" 는 뜻이고, 컴파일러가 그 약속을 검사한다. 그래서 이 두 op 은 아무 데서나(순수 `fn`
안에서도) 부를 수 있다.

**정직하게 안 하는 것:**

- **정렬을 검사하지 않는다.** `s` 가 오름차순이라는 전제는 **호출자의 몫**이다
  (`sort` 로 만든다). 전제가 깨지면 답이 틀리지만 **트랩은 아니다** — 트랩(trap)은 프로그램을
  그 자리에서 멈추는 즉시 중단인데, 여기서는 범위 밖을 안 만지므로 멈출 일이 없다. 대신
  **조용히 틀린 답**이 나온다. 소스가 이 전제를 정직하게 적어 둔다.
- **u64 전용이다.** 다른 원소 타입·비교자 주입은 없다. 비교자 주입이란 "무엇이 더 작은가" 를
  정하는 함수를 밖에서 끼워 넣는 것인데, 그것이 없으므로 내림차순·키 추출은 못 한다.
- **upper_bound·삽입·삭제는 없다.** 하한 하나로 contains(=`bsearch`)·정렬 유지
  삽입점·범위 `[lo, hi)` 질의가 다 선다 — 그래서 표면이 둘이다.

## 자료구조

이 절은 **이 모듈이 무엇을 들고 있는가**를 말한다. 답은 "아무것도" 다 — 상태는 전부 호출자
쪽에 있다.

자료구조를 소유하지 않는다. 입력은 호출자의 `slice u64` 하나이고, 요구되는 불변식은
**오름차순 정렬** 하나다. 불변식(invariant)은 "op 을 부르는 내내 참이어야 하는 조건" 을
말한다. 구현은 반열린 구간 `[lo, hi)` 로 돌고(시작은 포함, 끝은 제외 — 길이가 곧 `hi−lo` 라
셈이 안 어긋난다), 중점은 `lo + (hi−lo)/2` 로 잡는다 — `(lo+hi)/2` 는 큰 인덱스에서 넘칠 수
있다(고전적 버그).

## op 한눈에

이 절은 **두 op 을 한 표로** 보여 준다. 급할 때는 여기만 봐도 된다.

모든 op 은 `fn` · `effects none`. `fn` 은 값만 계산하는 op 이라는 뜻이다(상태를 바꾸는
`proc` 과 구별된다 — [3장](../03-ops.md)).

| op | 종류 | 시그니처 요약 | 실패 시 |
|---|---|---|---|
| `bsearch` | fn | `(s slice u64, target u64) → option u64` | 없으면 `none` |
| `lower_bound` | fn | `(s slice u64, target u64) → u64` | — (모두 작으면 `len s`) |

고르는 법: "있나 없나, 있으면 어디" 는 `bsearch`, "여기 끼우면 어디" 또는 "이 값 이상이
어디부터" 는 `lower_bound` 다.

## op 상세

이 절은 **매개변수 하나하나가 왜 필요한지**까지 적는다. 두 op 모두 슬라이스와 찾는 값,
딱 둘만 받는다.

#### bsearch
```lowent
export fn bsearch input s slice u64 . input target u64 . output option u64 .
```
- `s` — 오름차순 정렬된 슬라이스(전제, 검사 안 함). **왜 받나:** 뒤질 대상이다. 모듈이
  자료를 들고 있지 않으므로 매번 어디를 볼지 건네받아야 한다. `mut` 이 아닌 것은 읽기만
  하기 때문이다.
- `target u64` — 찾는 값. **왜 받나:** 가운데 원소와 비교할 기준이 있어야 왼쪽/오른쪽을
  정할 수 있다.
- 반환: `target` 의 **인덱스**를 `some` 으로, 없으면 `none`. **중복이 있으면 그중
  하나**의 인덱스다(어느 것인지는 규정하지 않는다) — 첫 등장이 필요하면 `lower_bound`.
- 빈 슬라이스는 곧장 `none`.

#### lower_bound
```lowent
export fn lower_bound input s slice u64 . input target u64 . output u64 .
```
- `s` — `bsearch` 와 같다. 오름차순 전제도 같다. **왜 받나:** 경계를 잴 대상이다.
- `target u64` — 경계의 기준값. **왜 받나:** "이 값 이상" 의 "이 값" 이다.
- 반환: **`target` 이상인 첫 인덱스.** 모든 원소가 작으면 `len s`.
- 이것이 **삽입점**이다(여기에 끼워 넣으면 정렬이 유지된다), 그리고 **범위의 시작**이다:
  `[lower_bound s a, lower_bound s b)` 가 `a ≤ x < b` 인 원소들의 인덱스 구간이다.
- `option` 이 아니다 — "못 찾음" 이 없는 물음이기 때문이다(끝 인덱스도 답이다).

## 사용법과 예제

이 절은 **실제로 부르는 모양**을 보여 준다. 요점은 하나다: 부르기 전에 정렬한다.

`lib/search.low` 의 모듈 이름은 **`searchlib`** 다 — 파일 이름(`search`)이 아니다.
모듈 이름은 파일 안의 `module` 선언이 정한다.

```lowent
use searchlib from "../../lib/search.low" .   rem 경로 직접 지정
use searchlib .                               rem from-생략 — 예약(std) 해소
use searchlib as bs .                         rem 별칭 관례 — bs.bsearch …
```

정렬과 함께 쓴다(픽스처 `impl/tests/vm_search.low` 기반) — **먼저 정렬한다**:

```lowent
module demo .

use sortlib from "../../lib/sort.low" .
use searchlib from "../../lib/search.low" .

rem 정렬 후 target 을 찾는다 → 인덱스+1 (0 = 못 찾음).
proc bs_find input s mut slice u64 . . input target u64 . output u64 . do
  sortlib.sort s .                          rem ① 전제 만들기 — 제자리 정렬(s 가 여기서 바뀐다)
  let r option u64 . be searchlib.bsearch s target .   rem ② 찾기 — 답은 option(있다/없다)
  guard is_some r . else return 0 .         rem ③ 없으면 여기서 0 을 내고 끝낸다
  return add (some_value r) 1 .             rem ④ some_value 로 인덱스를 꺼낸다(0 을 "없음"에 썼으므로 +1)
end

rem 정렬 후 하한 — 삽입점이자 범위 시작.
proc lb_find input s mut slice u64 . . input target u64 . output u64 . effects none . do
  sortlib.sort s .                              rem ① 여기서도 정렬이 먼저다
  return searchlib.lower_bound s target .       rem ② option 이 아니라 u64 그대로 — 갈라낼 것이 없다
end
```

```sh
$ build/lowentc --run bs_find demo.low [30,10,20] 20
2                                   # 정렬 [10,20,30] 에서 20 은 인덱스 1 → 1+1
$ build/lowentc --run lb_find demo.low [30,10,20] 15
1                                   # 15 이상인 첫 원소는 20(인덱스 1) — 삽입점
```

범위 질의는 하한 둘로 짠다: `a ≤ x < b` 인 원소 개수 =
`sub (lower_bound s b) (lower_bound s a)`.

```lowent
rem [a, b) 안에 든 원소의 개수. 두 하한의 차가 곧 개수다.
fn count_in input s slice u64 . input a u64 . input b u64 . output u64 . do
  let i u64 be searchlib.lower_bound s a .   rem 구간의 시작 인덱스
  let j u64 be searchlib.lower_bound s b .   rem 구간의 끝 인덱스(제외)
  guard lt i j . else return 0 .             rem 비었거나 뒤집힌 구간이면 0 (sub 이 넘치지 않게)
  return sub j i .
end
```

## 반례 — 이렇게 쓰면 안 된다

이 절은 **초보자가 실제로 하는 실수**와, 그것을 **무엇으로 알아채는지**(증상)를 적는다.
여기서 증상은 셋 중 하나다: 컴파일 에러(E-코드) · 트랩 · 조용히 틀린 답. 이 모듈의 함정은
대부분 **마지막 것**이라 특히 조심해야 한다.

**✗ 정렬 안 된 슬라이스에 부르기:**

```lowent
rem ✗ [30,10,20] 은 오름차순이 아니다
let r option u64 . be searchlib.bsearch s 10 .   rem none 이 나올 수 있다 — 10 이 있는데도
```
증상: **조용히 틀린 답**이다 — 오류도 트랩도 E-코드도 없고, `none` 이 나오거나 엉뚱한
인덱스가 나온다. 전제 위반은 검사되지 않는다. 알아채는 법: "분명히 있는 값인데 `none`" 이면
십중팔구 정렬을 안 한 것이다. `sort` 를 먼저 부르는 것이 계약이다.

**✗ `bsearch` 의 `none` 을 안 가르기:** 결과는 `option u64` 다. `guard is_some r .`
없이 `some_value r` 부터 꺼내면 없는 값일 때의 답이 없다
([7장](../07-guard-and-option.md)).

```lowent
rem ✗ 못 찾았을 때가 없다
let r option u64 . be searchlib.bsearch s target .
return some_value r .            rem target 이 없으면 여기서 터진다
```
증상: 컴파일은 통과하고, 못 찾은 순간 실행 중 **E-VM-NONE 트랩(패닉)** 으로 멈춘다
(`some_value of none`). 있는 값만 넣어 시험하면 안 걸리므로, `option` 을 보면 반사적으로
`guard is_some` 을 붙이는 습관이 답이다.

**✗ 중복 원소에서 `bsearch` 인덱스에 의미 싣기:** `[5,5,5]` 에서 `bsearch s 5` 는
0·1·2 중 **어느 것이든** 낼 수 있다. 증상: **조용히 틀린 답** — 컴파일도 되고 실행도 되는데
"첫 번째" 를 기대한 코드가 가끔 다른 자리를 받는다(백엔드나 입력이 바뀌면 답이 달라져
재현이 어렵다). "첫 5 의 자리" 가 필요하면 `lower_bound s 5` 를 쓴다.

**✗ `lower_bound` 결과로 곧장 `index` 하기:**

```lowent
rem ✗ 못 찾으면 i 가 len s 다 — 그 자리는 없는 자리다
let i u64 be searchlib.lower_bound s t .
let v u64 be index s i .
```
증상: 실행 중 **E-VM-BOUNDS 트랩** — 프로그램이 그 줄에서 멈춘다. 값이 표의 최댓값보다 클 때만
터지므로 시험에서 안 걸리고 실제 데이터에서 걸리기 쉽다. `guard lt i (len s) .` 를 먼저 둔다.

## 주의사항

이 절은 **알고 있으면 하루를 아끼는 것들**이다. 대부분 "오름차순 전제" 와 "끝 인덱스" 두
가지에서 나온다.

- **전제는 오름차순이다.** 내림차순 정렬에는 그대로 못 쓴다. 이 라이브러리에는 비교자를
  바꿔 끼울 자리가 없으므로, 내림차순 자료는 뒤집어서 오름차순으로 만들어 두고 쓰는 수밖에
  없다.
- **정렬은 한 번, 탐색은 여러 번이다.** 찾을 때마다 `sort` 를 부르면 O(log n) 의 이득이
  통째로 사라진다(정렬이 훨씬 비싸다). 위 예제가 매번 정렬하는 것은 픽스처가 매 호출을
  독립적으로 돌리기 때문이지, 권장 모양이 아니다.
- **정렬 뒤에 슬라이스를 고치면 전제가 깨진다.** 원소 하나를 `set` 으로 바꿔 넣는 순간
  다시 정렬해야 한다. 이 모듈은 그 변경을 알아채지 못한다.
- `lower_bound` 의 반환 `len s` 는 유효한 **삽입점**이지만 유효한 **인덱스가 아니다** —
  `index s (lower_bound s t)` 를 그대로 쓰면 끝에서 `E-VM-BOUNDS` 로 트랩한다.
  `guard lt i (len s) .` 가 먼저다.
- **하한 둘의 차를 뺄 때 순서를 지킨다.** `sub` 은 u64 뺄셈이라 작은 값에서 큰 값을 빼면
  아래로 넘쳐 거대한 수가 된다. `a ≤ b` 인지 먼저 확인하거나 위 `count_in` 처럼 `guard` 로
  막는다.
- "target 이 있는가" 는 `lower_bound` 로도 된다: `i = lower_bound s t` 후
  `i < len s` 이고 `index s i == t` 인가. `bsearch` 는 그 관용구를 이름으로 준 것이다.
- 순수 op 이라 재진입·동시 읽기에 제약이 없다 — 같은 슬라이스를 여러 곳에서 동시에
  탐색해도 자유다(쓰는 곳이 없을 때). 재진입(reentrant)은 "여러 곳에서 겹쳐 불러도 서로를
  망치지 않는다" 는 뜻인데, 숨은 전역 상태가 없으니 공짜로 성립한다.

---

[← 목차](../README.md)
</content>
</invoke>
