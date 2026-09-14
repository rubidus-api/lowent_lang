# sortgen — 제네릭 정렬 (비교는 타입이 들고 온다)

소스: `lib/sortgen.low` · 검증: `impl/tests/vm_sortgen.low`

## 처음 쓰는 사람에게

**무엇을 하는 모듈인가.** 원소가 u64 가 아니어도 정렬해 주는 모듈이다. 무엇이 더 작은지는
**타입이 스스로 말한다**.

**언제 쓰나.** 레코드를 어떤 필드 기준으로 줄 세울 때 쓴다. 원소가 그냥 u64 면 `sortlib.sort`
가 더 간단하고 빠르다.

**최소 예제.** 세 가지가 한 벌이다: ① 정렬할 타입이 `ordered` 를 충족한다고 선언하고
② 그 타입의 `less` 를 쓰고 ③ 타입을 인자로 주며 부른다.

```lowent
use sortgen .

struct row
  satisfies sortgen.ordered .     rem ① 이 타입은 순서를 안다고 선언한다
  key u64 .
end

rem ② 무엇이 더 작은가 — 이 한 줄이 정렬 기준의 전부다
fn row.less output bool . input a row . input b row . do
  return lt (field a key) (field b key) .
end

proc go output void . input s mut slice row . . effects none . do
  sortgen.sort_by row s .          rem ③ 타입을 comptime 인자로 준다
end
```

**읽는 순서.** 급하면 위 예제만 베껴도 된다. 왜 비교 함수를 값으로 안 넘기는지가 궁금하면
«설계 의도와 경계» 를, 스칼라를 정렬하려다 막히면 «반례» 를 보면 된다.

## 왜 있는가

정렬은 알고리즘이 하나인데 **비교만 타입마다 다르다**. 그 차이를 어떻게 받을 것인가가
이 모듈이 답하는 질문이다.

흔한 답은 **비교 함수를 값으로 넘기는 것**이다(C 의 `qsort`, C++ 의 comparator). 이 언어는
그 길을 안 쓴다. 첫째로 일급 함수가 없고, 둘째로 있어도 **간접 호출**이 생긴다 — 호출 자리를
보면 공짜처럼 보이는데 실제로는 함수 포인터를 거치는 비용이 숨는다. 그것이 P2(비용 가시)가
막는 종류의 숨김이다.

이 언어의 답은 **comptime 타입 파라미터 + trait 경계**다(RFC-0021). 호출마다 그 타입 전용
op 이 만들어지고(단형화), 비교는 **직접 호출**로 박힌다. 간접 호출 0, 비교자를 담을 공간 0.

## 설계 의도와 경계

**하는 것.** 어떤 원소 타입이든 `ordered` 를 충족하면 제자리에서 오름차순 정렬한다.
알고리즘은 삽입정렬이라 **안정**(같은 값의 순서가 안 바뀐다)이고, 거의 정렬된 입력에 빠르다.

**정직하게 안 하는 것.**

- ~~큰 배열용 제네릭 quicksort~~ — **지었다**(`sort_fast`, 2026-07-26). 분할이 피벗의 *복사*를
  요구한다고 봤던 것이 틀렸다: 피벗을 **자리**(인덱스)로 들고 비교할 때마다 다시 읽으면 된다.
  분할 루프 동안 피벗은 `hi-1` 에 가만히 있고, 옮기는 일은 전부 `swap` 이 한다.
- **내림차순·다중 키** — `less` 를 그렇게 쓰면 된다. 모드 인자를 다는 것이 곧 엔트로피다.
- **스칼라 직접 정렬** — `u64` 는 trait 을 충족할 수 없다(메서드를 붙일 자리가 없다).
  필드 하나짜리 구조체로 감싸면 되고, 레이아웃은 그대로 8바이트다.

## 자료구조

새 자료구조는 없다. 대신 **약속**이 하나 있다.

```lowent
export trait ordered
  less output bool . input a self . input b self . effects none .
end
```

`self` 는 "이 trait 을 충족하는 그 타입" 을 뜻한다. `less a b` 가 참이면 a 가 b 보다 앞에
온다. 이 관계는 **엄격한 약한 순서**여야 한다 — 특히 `less a a` 는 거짓이어야 한다(자기가
자기보다 앞이라고 하면 정렬이 멈추지 않을 수 있다).

## op 한눈에

| op | 종류 | 시그니처 요약 | 실패 시 |
|---|---|---|---|
| `ordered` | trait | `less (self, self) → bool` | 미충족 = `E-BOUND-UNSAT`(컴파일) |
| `sort_by` | proc | `(comptime t, mut slice t) → void`, `requires ordered t` | 실패 없음(빈 슬라이스도 무동작) |
| `sort_fast` | proc | 같음 | 실패 없음 — quicksort, 큰 배열용 |
| `lower_by` | proc | `(comptime t, slice t, t) → u64` | 실패 없음 — 없으면 삽입점(`len s` 가능) |
| `find_by` | proc | `(comptime t, slice t, t) → option u64` | 없으면 `none` |

## op 상세

**매개변수를 왜 받나 — 이 모듈의 규약.** `t` 는 **컴파일 시에만 있는 인자**다 — 실행 파일에는
남지 않고, 대신 그 타입 전용 정렬 op 이 하나 만들어진다. `s` 가 `mut` 인 이유는 제자리에서
바꾸기 때문이다. 새 배열을 돌려주지 않는 것은 그러면 할당이 필요하고, 할당은 이 계층이 하지
않는 일이기 때문이다.

#### sort_by
```lowent
export proc sort_by output void . input comptime t type . input s mut slice t . . effects none . requires ordered t .
```
- `comptime t type` — 정렬할 **원소 타입**. **왜 받나:** 비교를 어디서 가져올지 정하는 것이
  이 인자다. 컴파일 때 확정되므로 실행 중 비용은 0 이다.
- `s mut slice t` — 정렬할 자리. **왜 받나:** 모듈은 자료를 갖지 않는다. `mut` 은 이 안을
  실제로 뒤바꾼다는 뜻이다.
- `requires ordered t` — **왜 필요한가:** 이것이 없으면 `less` 가 있는지 아무도 안 물은 채
  본문이 그것을 부른다. 경계가 있으면 없는 타입으로 부르는 순간 **컴파일이** 거절한다.
- 반환: 없다. 결과는 `s` 안에 있다.
- 효과: `none` — 정렬은 순수 계산이다(자기 인자를 바꾸는 것은 효과가 아니라 인자의 성질이다).

## 사용법과 예제

아래는 실제로 통과하는 픽스처(`impl/tests/vm_sortgen.low`)의 골자다.

```lowent
module app .
use sortgen .

struct keyed
  satisfies sortgen.ordered .        rem 이 선언이 없으면 경계에서 걸린다
  k u64 .
end
fn keyed.less output bool . input a keyed . input b keyed . do
  return lt (field a k) (field b k) .   rem 기준은 k 필드 하나
end

proc sorted3 output u64 . input s mut slice keyed . . effects none . do
  guard ge (len s) 3 . else return 90 .
  sortgen.sort_by keyed s .          rem 여기서 keyed 전용 정렬 op 이 만들어진다
  let f0 keyed be index s 0 .        rem 읽으면 뷰다 — 복사가 아니다
  let f1 keyed be index s 1 .
  let f2 keyed be index s 2 .
  return add (mul 100 (field f0 k)) (add (mul 10 (field f1 k)) (field f2 k)) .   rem [3,1,2] → 123
end
```

**스칼라를 정렬하려면** 감싸면 된다. 8바이트짜리 u64 하나를 담은 구조체는 레이아웃이 u64 와
같으므로 공간 낭비가 없다.

## 반례 — 이렇게 쓰면 안 된다

**① `satisfies` 를 빠뜨린다.**
```lowent
struct row
  key u64 .                          rem satisfies 가 없다
end
fn row.less output bool . input a row . input b row . do return lt (field a key) (field b key) . end
rem sortgen.sort_by row s .          → E-BOUND-UNSAT
```
**증상:** 컴파일이 거절한다. 실행해 보고 아는 것이 아니라 **부르는 순간** 안다.

**② u64 를 그대로 넘긴다.**
```lowent
rem sortgen.sort_by u64 s .          → E-BOUND-UNSAT (u64 는 less 를 가질 수 없다)
```
**증상:** 같은 오류다. 고치는 법은 감싸거나, 그냥 `sortlib.sort` 를 쓰는 것이다.

**③ `less` 가 같은 값에 참을 낸다.**
```lowent
fn row.less output bool . input a row . input b row . do
  return le (field a key) (field b key) .            rem ★ le 는 같을 때도 참이다
end
```
**증상:** 컴파일은 통과한다. 그리고 **같은 값이 둘 이상이면 루프가 안 끝날 수 있다.**
비교는 `lt`(작다)여야지 `le`(작거나 같다)면 안 된다.

## 주의사항

- **왜 `sortlib` 과 갈라져 있나.** 제네릭 틀은 단형화되기 전까지 타입 파라미터를 들고 있어서,
  나무를 안 세우는 대조 모드에서 진단이 달라진다. 이것을 `sortlib` 에 두면 sortlib 을 쓰는
  **모든 파일**이 그 대조에서 빠져야 해 검증 덮개가 잠식된다. 그래서 제 모듈에 산다.
- **비용.** 삽입정렬이라 최악 O(n²)다. 수백 개를 넘어가면 `sortlib.sort`(quicksort, u64) 로
  키만 정렬하고 원본을 재배치하는 방법이 낫다.
- **타입마다 코드가 하나씩 생긴다.** 단형화의 대가다 — 세 타입으로 부르면 정렬 op 이 셋이다.
  대신 각각은 간접 호출이 없다.
- **읽으면 뷰다.** `let a t be index s j` 는 복사가 아니라 그 자리를 가리키는 창이다. 그래서
  이 모듈이 원소를 맞바꿀 때 `swap` 을 쓴다 — `set` 두 번으로는 자기 자신을 덮어쓴다.
- **뷰는 맞바꾼 뒤 다른 값을 가리킨다.** 직접 정렬을 짤 일이 있으면 이 함정을 조심해야 한다:
  한 번 맞바꾸고 나면 `a`·`b` 라는 **같은 이름이 다른 값**을 본다. 그래서 한 걸음에 판정은
  **한 번만** 해야 한다. 두 번 비교하면 두 번째는 이미 바뀐 것을 보고 "정렬됐다" 고 답해
  원소가 한 칸만 내려가고 멈춘다 — 결과는 **부분 정렬**이고, 원소 셋에 단일 키면 그래도
  답이 맞아 **버그가 숨는다**(실제로 그렇게 숨었다가 다중 키 예제에서 드러났다).
