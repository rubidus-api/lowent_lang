# soa — SoA 배치 시범: 필드마다 배열 하나

소스: `lib/soa.low` · 모듈명 `soa` (RFC-0070 재검토 조건 ③, 2026-07-20)

## 처음 쓰는 사람에게

**무엇을 하는 모듈인가.** 같은 종류의 필드를 **배열 하나씩** 따로 두는 배치(SoA) 시범이다.

**언제 쓰나.** 많은 원소의 한 필드만 반복해서 훑을 때(좌표 x 만 갱신) 캐시에 유리하다.

**최소 예제.** 아래가 이 모듈을 쓰는 가장 짧은 꼴이다. 각 줄이 무엇을 하는지는 주석에 적었다.

```lowent
use soa .

soa.step_x xs vxs n .                       rem x 배열만 훑는다 — y·z 는 캐시에 안 올라온다
```

**읽는 순서.** 급하면 «op 한눈에» 표만 봐도 된다. 왜 이렇게 생겼는지가 궁금하면 «왜 있는가»부터, 실제로 쓰다 막히면 «반례»와 «주의사항»을 먼저 보면 된다.

## 왜 있는가

이 모듈은 **같은 모양의 자료가 아주 많고, 그중 한두 필드만 자주 훑을 때** 쓴다. 먼저
낱말부터: 원소가 `{x, y, vx, vy}` 인 자료 n 개를 두는 방법이 두 가지다 —
**AoS**(Array of Structs)는 한 배열에 원소를 통째로 늘어놓는 배치이고,
**SoA**(Struct of Arrays)는 **필드마다 배열 하나**를 나란히 두는 배치다. x 만 훑는
계산이라면 SoA 쪽이 필요한 값만 연속으로 읽어서 캐시(느린 메모리 앞의 빠른 임시 저장소)를
알뜰하게 쓴다.

RFC-0070 은 SoA 를 언어 기능으로 넣자는 사전 조사(`store[T, soa]` 타입 생성자 + 키워드
15~20 + op 20+)를 **보류**하며 재검토 조건을 못박았다: *"`lib/` 에 SoA 컨테이너를 빌트인
증가 0 으로 먼저 써 본다. 거기서 막히는 곳이 언어 작업의 목록이 된다."*

이 파일이 그것이다 — **답이 아니라 측정이다.** 같은 계산을 SoA 판과 AoS 판으로 둘 다
지금 언어로 쓴다. 핵심 발견: **SoA 의 배치 자체는 라이브러리로 완전히 표현된다.** 필드별
배열·보폭 없는 순차 접근·한 필드만 훑는 커널 — 전부 새 언어 기계 없이 써진다. 그래서
언어가 SoA 를 빌트인으로 넣지 않았다.

## 설계 의도와 경계

시범이므로 "되는 것" 과 "막히는 것" 을 함께 적는 것이 이 모듈의 몫이다.

하는 것:

- 필드별 배열을 훑는 커널(`step_x`·`sum_field`·`step_all`)과, 같은 계산의 AoS 판
  (`step_x_aos`). 두 배치가 **같은 답**을 낸다 — 배치는 성능 축이지 의미 축이 아니다
  (`impl/tests/vm_soa.low` 가 그것을 잰다).

정직하게 안 하는 것(소스가 "막히는 자리" 로 적어 둔 것):

- **원소 하나를 "한 덩어리" 로 다루는 문법이 없다.** `load i` 가 레코드를 만들어 주지
  않으므로 호출자가 필드를 손으로 모은다(`get_x`). 불편할 뿐 불가능하지는 않다.
- ~~필드 개수만큼 인자가 늘어난다~~ — 이것은 **풀렸다**(2026-07-20, 구조체 필드에
  슬라이스 허용). 다만 이 파일은 측정 기록이라 네-인자 모양(`step_all`)을 그대로 둔다.
- **타입이 배치를 모른다.** AoS 판과 SoA 판이 서로 다른 op 이름이 된다(`step_x` vs
  `step_x_aos`). 사전 조사의 `store[T, L]` 이 풀려는 문제이고 그 값은 실재한다.

## 자료구조

선언된 자료구조가 없다는 것 자체가 이 모듈의 답이다 — SoA 컨테이너는 **호출자가 준
나란한 배열들**이다.

struct·actor 선언이 없다. `{x, y, vx, vy}` 라면 `slice u64` 넷이다. 한 필드를 훑으면
연속이다 — 보폭(stride: 다음 원소까지 건너뛰는 칸수)이 없다. 불변식은 호출자 몫이다:
나란한 배열들은 같은 길이(또는 최소한 처리할 `n` 이상)여야 한다. op 들은 짧은 쪽에 맞춰
동작 범위를 줄인다(아래 상세).

## op 한눈에

넷은 SoA 판(`step_x`·`sum_field`·`get_x`·`step_all`)이고 하나는 비교용 AoS 판
(`step_x_aos`)이다.

| op | 종류 | 시그니처 요약 | 실패 시 |
|---|---|---|---|
| `step_x` | proc | `xs mut slice u64`, `vxs slice u64`, `n u64` → `u64` (처리 수) | 실패 없음 — 범위를 줄인다 |
| `sum_field` | fn | `f slice u64` → `u64` (합) | 실패 없음 |
| `get_x` | fn | `xs slice u64`, `i u64` → `u64` | 범위 밖이면 `0` |
| `step_all` | proc | `xs`·`ys mut slice u64`, `vxs`·`vys slice u64`, `n u64` → `u64` | 실패 없음 — 최솟값 반환 |
| `step_x_aos` | proc | `rows mut slice u64`, `stride`·`xoff`·`voff`·`n u64` → `u64` | 범위 밖에 닿기 전에 `i` 반환 |

전부 `effects none .` 이다.

## op 상세

**매개변수를 왜 받나 — 이 모듈의 규약.** SoA 는 "필드마다 배열 하나" 다. 그래서 op 은
구조체 배열 하나가 아니라 **필드 배열들을 따로** 받는다(`xs`·`vxs` 처럼). 한 필드만 훑을 때
다른 필드가 캐시에 안 올라오는 것이 이 배치의 전부이고, 매개변수가 갈라져 있는 것이 바로 그
성질을 문법에 드러낸 것이다. `n` 은 훑을 개수 — 배열이 더 길어도 앞에서 n 개만 본다.


다섯 op 을 시그니처와 함께 본다. 매개변수마다 "왜 이것이 필요한가" 를 한 줄로 붙였다 —
필드마다 배열이 따로이므로 인자도 필드마다 하나씩 늘어난다는 것이 여기서 눈에 보인다.

### step_x

위치 배열을 속도 배열만큼 전진시킨다 — SoA 가 이기는 자리를 보여 주는 대표 커널이다.

```lowent
export proc step_x input xs mut slice u64 . . input vxs slice u64 . input n u64 .
  output u64 . effects none .
```

- `xs` — 위치 필드 배열(제자리에서 바뀌므로 `mut`). `vxs` — 속도 필드 배열(읽기만).
  필드마다 배열이 따로이므로 인자도 필드마다 하나씩이다.
- `n` — 처리할 원소 수. 배열 전체가 아니라 앞쪽 일부만 처리하고 싶을 수 있어서 받는다.
- `xs[i] += vxs[i]` 를 돈다. **x 와 vx 두 배열만** 만진다 — y·vy 는 캐시에 안 올라온다.
- 실제 처리 개수 `m = min(n, len xs, len vxs)` 를 돌려준다. 범위 밖은 안 만진다.

### sum_field

한 필드 배열 전체를 합친다.

```lowent
export fn sum_field input f slice u64 . output u64 .
```

- `f` — 합칠 필드 배열. 어느 필드든 배열이면 되므로 매개변수는 이것 하나다.
- 다른 필드는 읽지도 않는다. 일부만 합치려면 `subslice` 로 잘라 준다.

### get_x

원소 하나의 한 필드를 꺼낸다.

```lowent
export fn get_x input xs slice u64 . input i u64 . output u64 .
```

- `xs` — 필드 배열. `i` — 몇 번째 원소인지 — SoA 에는 "원소" 라는 덩어리가 없으므로
  자리 번호로 가리킬 수밖에 없다.
- `i >= len xs` 면 **0 을 돌려준다** — 트랩이 아니다. 0 과 "범위 밖" 을 가려야 하면
  호출 전에 길이를 검사한다.
- "막히는 자리 ①" 의 실물: 원소 전체를 레코드로 주는 op 은 없고, 필드마다 이렇게 꺼낸다.

### step_all

두 필드 쌍(x·y)을 함께 전진시킨다.

```lowent
export proc step_all input xs mut slice u64 . . input ys mut slice u64 . .
  input vxs slice u64 . input vys slice u64 . input n u64 .
  output u64 .
```

- 인자가 넷인 이유가 곧 기록이다: 필드 수만큼 인자가 늘어나는 모양("막히는 자리 ②" —
  지금은 구조체로 묶을 수 있으나 이 파일은 측정 기록이라 그대로 둔다).
- `step_x` 를 x·y 에 한 번씩 부르고, 두 호출이 처리한 개수 중 **작은 쪽**을 돌려준다.

### step_x_aos

같은 계산의 AoS 판 — 한 배열에 원소가 보폭으로 섞여 있다.

```lowent
export proc step_x_aos input rows mut slice u64 . . input stride u64 . input xoff u64 .
  input voff u64 . input n u64 .
  output u64 .
```

- `rows` — 원소들이 통째로 늘어선 한 배열. `stride` — 원소 하나가 차지하는 칸수
  (`{x, vx}` 면 2). `xoff`/`voff` — 원소 안에서 x·vx 가 몇 번째 칸인지. AoS 는 배치
  정보가 타입에 없으므로 이 세 수를 호출자가 들고 다녀야 한다 — 그것이 비교의 요점이다.
- `rows[i*stride + xoff] += rows[i*stride + voff]` 를 돈다.
- 원소 `i` 의 속도 자리가 범위 밖이면 **그 자리에서 `i` 를 돌려주고 멈춘다**(그때까지는
  처리됨). 끝까지 가면 `n`.
- 배치가 타입에 안 실려 이름이 갈리는 것("막히는 자리 ③")의 실물이다.

## 사용법과 예제

같은 자료를 두 배치로 놓고 같은 계산을 돌려 본다 — 답이 같아야 한다.

```lowent
use soa .               rem from-생략 std 해소
use soa as s .          rem 별칭 관례
```

같은 계산, 두 배치, 같은 답(`impl/tests/vm_soa.low` 를 줄인 것):

```lowent
proc demo input xs mut slice u64 . . input vxs mut slice u64 . .
  input rows mut slice u64 . . output u64 .
do
  rem ── SoA 판: 필드마다 배열 — xs 는 위치만, vxs 는 속도만 든다 ──
  set (index xs 0) 1 .          rem 원소 0 의 x
  set (index xs 1) 2 .          rem 원소 1 의 x
  set (index vxs 0) 10 .        rem 원소 0 의 vx
  set (index vxs 1) 20 .        rem 원소 1 의 vx
  let n1 u64 be soa.step_x xs vxs 2 .    rem x += vx 를 2 개 원소에
  guard eq n1 2 . else return 90 .       rem 정말 2 개가 처리됐는지 확인
  let s1 u64 be soa.sum_field (subslice xs 0 2) .    rem 11 + 22 = 33

  rem ── AoS 판: {x, vx} 를 보폭 2 로 한 배열에 섞는다 ──
  set (index rows 0) 1 .        rem 원소 0 의 x  (자리 = 0*2+0)
  set (index rows 1) 10 .       rem 원소 0 의 vx (자리 = 0*2+1)
  set (index rows 2) 2 .        rem 원소 1 의 x  (자리 = 1*2+0)
  set (index rows 3) 20 .       rem 원소 1 의 vx (자리 = 1*2+1)
  let n2 u64 be soa.step_x_aos rows 2 0 1 2 .    rem stride=2, xoff=0, voff=1, n=2
  guard eq n2 2 . else return 91 .
  var s2 u64 be add (index rows 0) (index rows 2) .   rem AoS 에서 x 들을 손으로 모은다

  guard eq s1 s2 . else return 92 .    rem 배치가 답을 바꾸면 안 된다
  return s1 .                          rem 33
end
```

## 반례 — 이렇게 쓰면 안 된다

각 반례에 **증상**을 적었다. 이 모듈은 범위를 조용히 줄이거나 0 을 돌려주는 쪽이라
"에러가 안 났으니 잘 됐다" 는 판단이 특히 위험하다.

```lowent
rem ✗ 반환값(실제 처리 수)을 안 본다
let m u64 be soa.step_x xs vxs 1000 .
```

증상: 트랩이 안 난다 — op 이 조용히 범위를 줄인다. `xs` 가 3 칸이면 `m = 3` 이고 나머지
997 개는 처리되지 않은 채 다음 계산이 이어진다. `n` 개가 전부 처리됐다고 가정하는 코드는
`guard eq m n .` 으로 그것을 확인해야 한다.

```lowent
rem ✗ get_x 의 0 을 값으로 믿는다
let x u64 be soa.get_x xs 999 .
```

증상: 에러 없이 `0` 이 돌아온다 — "xs[999] 가 0 이다" 가 아니라 "범위 밖이다" 인데 값만
보면 구분이 안 된다. 구분이 필요하면 호출 전에 `lt 999 (len xs)` 를 검사한다.

```lowent
rem ✗ 라이브러리를 안 거치고 직접 범위 밖을 만진다
set (index xs 999) 1 .
```

증상: `xs` 가 3 칸이면 실행 중 **E-VM-BOUNDS 트랩** — 라이브러리 op 들과 달리 맨
`index` 는 범위를 줄여 주지 않는다.

```lowent
rem ✗ 불변 슬라이스를 mut 자리에 넘긴다
fn f input xs slice u64 . output u64 . do
  return soa.step_x xs xs 3 .
```

증상: **컴파일 타입 오류** — 첫 인자는 `mut slice u64` 여야 한다. `ref` 로 감싼 값을
`mut` 자리에 넘겨도 같은 부류다(E-TYPE-ARGMUT). 실행까지 가지 않는다.

```lowent
rem ✗ AoS 판에 오프셋을 바꿔 낀다
let n u64 be soa.step_x_aos rows 2 1 0 3 .    rem xoff 와 voff 를 뒤집었다
```

증상: 에러가 전혀 없이 **틀린 답**이 나온다 — 위치에 속도가 더해지는 대신 속도에 위치가
더해져, 속도 배열 자리가 자라난다. AoS 의 오프셋 인자는 타입이 전부 `u64` 라 컴파일러가
못 지켜 준다 — 이것이 "배치가 타입에 안 실린다" 는 막힘의 실감이다.

## 주의사항

반례가 보여 주듯 이 모듈의 실패는 대부분 조용하다 — 값 검사가 습관이어야 한다.

- **속도 주장은 측정 없이 하지 않는다**(RFC-0070 §4). 이 라이브러리가 보장하는 것은
  정확성(두 배치가 같은 답)뿐이다. SoA 가 항상 빠른 것도 아니다 — 원소 전체를 만지는
  코드는 AoS 가 낫다. 고를 수 있게 하는 것이 이 파일의 몫이다.
- 나란한 배열들의 길이를 맞추는 것은 호출자 책임이다. op 은 짧은 쪽에 맞춰 줄일 뿐,
  "길이가 어긋났다" 고 알려 주지는 않는다.
- `get_x` 의 실패 값 0 은 정상 값과 구분되지 않는다. 구분이 필요하면 호출 전 길이 검사.
- 전부 `effects none` 인 순수 계산이라 오라클 안이다 — VM 과 네이티브가 같은 답을 내고
  diff-sweep 이 임의 입력에서 그것을 지킨다.
- 문법 함정 하나(소스가 실제로 밟았다): 긴 `let`/`set` 을 줄바꿈으로 나누면 **개행이
  form 을 닫는다.** 이어 쓰려면 줄 끝에 `,` 를 둔다.
