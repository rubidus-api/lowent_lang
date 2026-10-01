# <a id="mod-sortgen"></a>`sortgen` — 제네릭 정렬(비교는 타입이 들고 온다)

소스

`lib/sortgen.low`

층

L0 — 순수 계산

권한

없음

원소가 `u64` 가 아니어도 정렬한다. 무엇이 더 작은지는 **타입이 스스로 말한다**. 레코드를 어떤 필드 기준으로 줄 세울 때 쓴다 — 원소가 그냥 `u64` 면 [`sortlib`](sec67.md#mod-sortlib) 이 더 간단하고 빠르다. 세 가지가 한 벌이다 — ① 타입이 `ordered` 를 충족한다고 선언하고 ② 그 타입의 `less` 를 쓰고 ③ 타입을 인자로 주며 부른다.

```lowent
use sortgen .

def struct keyed do
  satisfies sortgen.ordered .
  k u64 .
end

fn keyed.less input a keyed . input b keyed . output bool . do
  return lt (field a k) (field b k) .
end

proc sorted3 input s mut slice keyed . . output u64 . effects none . do
  guard ge (len s) 3 . else return 90 .
  sortgen.sort_by keyed s .
  let f0 be keyed idx s 0 .
  let f1 be keyed idx s 1 .
  let f2 be keyed idx s 2 .
  return add (mul 100 (field f0 k)) (add (mul 10 (field f1 k)) (field f2 k)) .
end
```

**왜 비교 함수를 값으로 넘기지 않나.** 흔한 답(C 의 `qsort`, C++ 의 comparator)은 비교 함수를 값으로 넘기는 것이다. 이 언어는 그 길을 쓰지 않는다. 일급 함수가 없고, 있어도 **간접 호출**이 생긴다 — 호출 자리를 보면 공짜처럼 보이는데 함수 포인터를 거치는 비용이 숨는다. 이 언어의 답은 **comptime 타입 파라미터 + trait 경계**다 (22장, 23장). 호출마다 그 타입 전용 op 이 만들어지고 비교는 직접 호출로 박힌다. 간접 호출 0, 비교자를 담을 공간 0.

| **op** | **모양** | **실패** |
|---|---|---|
| `ordered` | trait — `less (self, self) → bool` | 미충족 = `E-BOUND-UNSAT`(컴파일) |
| `sort_by` | `(comptime t, mut slice t) → void`, `requires ordered t` — 삽입정렬(안정) | 없음 |
| `sort_fast` | 같은 모양 — quicksort, 큰 배열용 | 없음 |
| `lower_by` | `(comptime t, slice t, key t) → u64` | 없음 — 없으면 삽입점(`len s` 가능) |
| `find_by` | `(comptime t, slice t, key t) → option u64` | 없으면 `none` |

*표 50.1 — `sortgen` 의 op*

`less a b` 가 참이면 a 가 b 보다 앞에 온다. 이 관계는 **엄격한 약한 순서**여야 한다 — 특히 `less a a` 는 거짓이어야 한다. `requires ordered t` 가 없으면 `less` 가 있는지 아무도 묻지 않은 채 본문이 그것을 부른다. 경계가 있으면 없는 타입으로 부르는 순간 컴파일이 거절한다.

**`sort_fast` 이야기.** 처음엔 분할이 피벗의 **복사**를 요구한다고 보고 제네릭 quicksort 를 짓지 않았다. 틀렸다 — 피벗을 **자리**(인덱스)로 들고 비교할 때마다 다시 읽으면 된다. 분할 루프 동안 피벗은 `hi − 1` 에 가만히 있고, 옮기는 일은 전부 `swap` 이 한다.

> **반례. `satisfies` 를 빠뜨리거나 `u64` 를 그대로 넘긴다**
>
> > `satisfies sortgen.ordered .` 가 없는 구조체나 `u64` 로 `sort_by` 를 부르면 `E-BOUND-UNSAT` 이다. `u64` 는 메서드를 붙일 자리가 없어 trait 을 충족할 수 없다 — 필드 하나짜리 구조체로 감싸면 레이아웃은 그대로 8 바이트이고, 아니면 `sortlib.sort` 를 쓴다.

> **반례. `less` 가 같은 값에 참을 낸다**
>
> > `return le (field a key) (field b key) .` 는 컴파일은 통과하지만 같은 값이 둘 이상이면 루프가 끝나지 않을 수 있다. 비교는 `lt` 여야 한다.

**주의.** 내림차순 · 다중 키는 `less` 를 그렇게 쓰면 된다 — 모드 인자를 다는 것이 곧 엔트로피다. 타입마다 코드가 하나씩 생긴다(단형화의 대가, 대신 간접 호출이 없다). **읽으면 뷰다** — `let a be t idx s j` 는 복사가 아니라 그 자리를 가리키는 창이라, 원소를 맞바꿀 때 `swap` 을 쓴다(`set` 두 번으로는 자기 자신을 덮어쓴다). 직접 정렬을 짠다면 조심한다 — 한 번 맞바꾸고 나면 같은 이름 `a` · `b` 가 다른 값을 본다. 한 걸음에 판정은 한 번만 한다. 두 번 비교하면 두 번째가 이미 바뀐 것을 보고 원소가 한 칸만 내려가 멈추는 부분 정렬이 되고, 원소 셋에 단일 키면 답이 우연히 맞아 결함이 숨는다(실제로 그렇게 숨었다가 다중 키 예제에서 드러났다). `sortlib` 과 갈라져 있는 것은, 제네릭 틀이 들어오면 `sortlib` 을 쓰는 모든 파일이 일부 대조 검사에서 빠져야 했기 때문이다.

---

[← 이전](sec67.md) · [목차로](README.md) · [다음 →](sec69.md)
