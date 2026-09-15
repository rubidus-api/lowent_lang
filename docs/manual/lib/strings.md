# strings — 문자열 뷰 연산 (`lib/str.low`)

## 처음 쓰는 사람에게

**무엇을 하는 모듈인가.** 문자열을 **복사하지 않고** 비교·검색·자르는 연산 모음이다.

**언제 쓰나.** 입력을 토막 내거나(공백으로 나누기), 접두사를 떼거나, 부분 문자열을 찾을 때 쓴다.

**최소 예제.** 아래가 이 모듈을 쓰는 가장 짧은 꼴이다. 각 줄이 무엇을 하는지는 주석에 적었다.

```lowent
use strings .

if strings.starts_with line "GET " . do                        rem line 이 "GET " 로 시작하나
  let rest slice u8 be strings.remove_prefix line "GET " .      rem 새 버퍼가 아니라 **뷰**다
end
```

여기서 **뷰**(view)란 복사본이 아니라 남의 바이트를 가리키기만 하는 창이다. `rest` 는
`line` 의 일부를 그대로 가리키므로, 새 메모리가 한 바이트도 안 든다.

**읽는 순서.** 급하면 «op 한눈에» 표만 봐도 된다. 왜 이렇게 생겼는지가 궁금하면 «왜 있는가»부터, 실제로 쓰다 막히면 «반례»와 «주의사항»을 먼저 보면 된다.

## 왜 있는가

이 모듈이 왜 라이브러리이고 왜 전부 뷰인지를 적는다. 한 줄로 줄이면 "문자열 일은 대부분
복사가 필요 없다" 이다.

이 모듈은 **문자열을 비교하고·찾고·다듬고·쪼갤 때** 쓴다. 복사도 할당도 없다 —
결과는 언제나 원본 위의 **뷰**(view: 원본 일부를 가리키기만 하는 창, 새 메모리가 아니다)다.

Lowent 로 쓰인 **첫 표준 라이브러리 모듈**이다(RFC-0068 S2). 언어가 문자열 연산을
빌트인으로 넣지 않은 이유는 소스 상단에 적혀 있다: 이 파일은 언어가 이미 가진
것(`len`·`index`·`subslice`·`eq`·`option`·`guard`·`while`·`actor`)만으로 지어졌고,
**빌트인 op 증가는 0** 이다. 이미 가진 것으로 지을 수 있으면 그것은 언어가 아니라
라이브러리다.

문자열 연산의 압도적 다수는 **소유가 필요 없다** — 자르기·찾기·비교는 원본을 가리키는
뷰(subslice: 슬라이스의 일부를 잘라 낸 또 다른 슬라이스)를 낼 뿐이다. 소유(쓰기 가능한
버퍼)는 별도 모듈 [`strbuf`](strbuf.md)가 맡는다. 이것이 RFC-0068 의 핵심 분할이다.

## 설계 의도와 경계

이 절은 "무엇을 해 주는가" 와 "무엇을 일부러 안 하는가" 를 적는다. 경계를 알면 어디까지
믿어도 되는지가 보인다.

**하는 것** — 바이트 단위 비교·탐색·접두/접미·다듬기·분할. 모든 op 이 **순수하고
(`effects none`) 아무것도 할당하지 않는다.** `effects none` 이란 이 op 이 세상(파일·시계·
전역 상태)에 손대지 않는다는 뜻이다. 복사도 없다 — 결과는 언제나 원본 위의 뷰다.

**정직하게 안 하는 것:**

- **인코딩 해석.** `str` 은 바이트다(인코딩 불가지 — 글자가 아니라 옥텟으로만 본다).
  UTF-8 검증·디코드는 `lib/utf8.low` 소관이다.
- **소유·수정.** 이어 붙이기·키우기는 [`strbuf`](strbuf.md)로 간다. 여기의 op 은 원본을
  한 바이트도 바꾸지 않는다.
- **`str_len`·`str_sub` 는 없다.** `str` 이 별칭이므로 빌트인 `len`·`subslice` 가 그대로
  작동하고, 같은 뜻의 두 이름은 동의어 금지(§2.5)가 거부한다.
- **센티널 없음.** 센티널(sentinel)이란 "-1 이면 못 찾음" 처럼 정상 값의 자리를 하나 빌려
  신호로 겸용하는 관례다. 여기서는 "못 찾음" 이 `option` 의 `none` 이다 — 없음은 값이다.

## 자료구조

이 모듈이 다루는 타입은 사실상 하나이고, 그것도 새 타입이 아니라 별명이다. 상태를 드는
것은 `str_splitter` actor 하나뿐이다.

```lowent
type str slice u8 .
```

`str` 은 `slice u8` 의 **투명 별칭**이다 — newtype(같은 표현이지만 다른 이름으로 벽을
세우는 타입)이 아니다(RFC-0068 §3-D1′). 바이트 슬라이스 이상의 불변식이 없으므로 명목
벽을 세우지 않았다. 문자열 리터럴, `slice u8` 값, `subslice` 결과가 전부 그대로 `str`
자리에 들어간다.

`str_splitter` actor 는 상태 네 칸을 든다: `src slice u8`(원본) · `sep u8`(구분 바이트) ·
`pos u64`(커서 — 다음에 읽을 자리) · `fin bool`(소진 여부). actor 란 자기 상태를 들고
`send` 메시지로만 대화하는 객체다 — 상태에 직접 손을 못 대므로 커서가 밖에서 어긋나지
않는다.

## op 한눈에

급할 때 이 표만 봐도 된다. 왼쪽부터 "무슨 이름으로 부르고 · 무엇을 주고 · 무엇을 받고 ·
안 될 때 무엇이 오는가" 순이다.

모든 fn 은 `effects none` 이다. actor 의 proc 둘은 `effects state`.
부를 때는 모듈 접두사를 붙인다 — `strings.find` 처럼.

| op | 종류 | 시그니처 요약 | 실패 시 |
|---|---|---|---|
| `eq_str` | fn | `(a str, b str) → bool` | — (다르면 false) |
| `has_byte` | fn | `(s str, b u8) → bool` | — (없으면 false) |
| `find` | fn | `(hay str, needle str, from u64) → option u64` | 못 찾음·`from > len hay` → `none` |
| `has` | fn | `(hay str, needle str) → bool` | — |
| `starts_with` | fn | `(s str, prefix str) → bool` | — |
| `ends_with` | fn | `(s str, suffix str) → bool` | — |
| `remove_prefix` | fn | `(s str, prefix str) → str` | 접두사 없으면 **원본 그대로** |
| `remove_suffix` | fn | `(s str, suffix str) → str` | 접미사 없으면 원본 그대로 |
| `trim_start` | fn | `(s str, cut str) → str` | 전부 잘리면 빈 뷰 |
| `trim_end` | fn | `(s str, cut str) → str` | 전부 잘리면 빈 뷰 |
| `split_next` | fn | `(src str, sep u8, pos u64) → option str` | `pos > len src` → `none` |
| `str_splitter` | actor | `init(s2 slice u8, b u8) → u64` · `next → option slice u8` | 소진 후 `next` → `none` |

## op 상세

**매개변수를 왜 받나 — 이 모듈의 규약.** 이 모듈은 **아무것도 복사하지 않는다.** 넘기는
`slice u8` 은 남의 바이트를 가리키는 창(뷰)이고, 돌려받는 것도 같은 바이트를 가리키는 다른
창이다. 그래서 `mut` 이 아니고 `effects none` 이며, 원본이 살아 있는 동안만 유효하다.
비교·검색의 두 인자 순서는 늘 (찾을 대상, 찾는 것) 이다 — `has_byte s b` 는 "s 안에 b 가
있는가" 로 읽는다.


op 하나하나를 시그니처와 함께 본다. 매개변수마다 "왜 이것이 필요한가" 를 한 줄로 붙였다 —
특히 `from`·`pos` 같은 커서가 왜 인자인지가 이 모듈의 성격을 말해 준다.

#### eq_str
```lowent
export fn eq_str input a str . input b str . output bool .
```
바이트 단위 동등 비교다.

- `a` — 비교할 왼쪽 문자열.
- `b` — 비교할 오른쪽 문자열. 길이가 다르면 바이트를 보기도 전에 false 다.
- 빌트인 `eq` 는 스칼라(수 하나) 전용이라(`E-VM-TYPE`) 슬라이스 비교는 여기 산다.
  그래서 이름도 `eq` 로 짧게 못 가고 `eq_str` 다.

#### has_byte
```lowent
export fn has_byte input s str . input b u8 . output bool .
```
바이트 멤버십 — 바이트 `b` 하나가 `s` 안에 있는가.

- `s` — 뒤질 문자열.
- `b` — 찾을 바이트 하나. 문자열이 아니라 바이트라는 점에 주의한다.
- `trim_start`/`trim_end` 의 cut 집합 검사가 이것을 쓴다. 빌트인 `contains` 는 bitset
  전용이라 그 이름을 못 쓴다.

#### find
```lowent
export fn find input hay str . input needle str . input from u64 . output option u64 .
```
`hay`(찾을 대상) 에서 `needle`(찾는 부분열)을 `from` 부터 찾아 시작 인덱스를 `some` 으로
낸다.

- `hay` — 뒤질 대상 문자열("건초 더미").
- `needle` — 찾을 부분 문자열("바늘").
- `from` — **어디서부터 찾을지**. 이 인자가 있는 이유가 이 모듈의 성격이다:
  **커서는 호출자가 드는 값**이다(RFC-0069 §4). 다음 검색은 찾은 자리 뒤를 `from` 으로
  다시 부른다 — 라이브러리가 상태를 어디에도 숨기지 않으므로 같은 문자열을 여러 곳에서
  동시에 훑어도 서로 간섭하지 않는다.
- `from > len hay` 면 `none`. 빈 needle 은 `some from`(규약).

#### has
```lowent
export fn has input hay str . input needle str . output bool .
```
"들어 있는가" 만 물을 때 쓴다.

- `hay`·`needle` — `find` 와 같다. 자리를 안 쓸 것이므로 `from` 도 없다.
- 속은 `is_some (find hay needle 0)` 그대로다.

#### starts_with · ends_with
```lowent
export fn starts_with input s str . input prefix str . output bool .
export fn ends_with input s str . input suffix str . output bool .
```
접두/접미 검사다.

- `s` — 검사할 문자열.
- `prefix`/`suffix` — 앞/뒤에 붙어 있는지 볼 조각. `s` 보다 길면 볼 것도 없이 false 다.

#### remove_prefix · remove_suffix
```lowent
export fn remove_prefix input s str . input prefix str . output str .
export fn remove_suffix input s str . input suffix str . output str .
```
접두/접미를 제거한 뷰를 낸다.

- `s` — 다듬을 문자열.
- `prefix`/`suffix` — 떼어 낼 조각. **없으면 원본을 그대로** 돌려준다 — 실패가 아니다.
  그래서 반환이 `option` 이 아니라 그냥 `str` 이다. 떼였는지 알아야 하면 미리
  `starts_with`/`ends_with` 로 묻는다.

#### trim_start · trim_end
```lowent
export fn trim_start input s str . input cut str . output str .
export fn trim_end input s str . input cut str . output str .
```
앞/뒤에서 특정 바이트들을 걷어낸 뷰를 낸다.

- `s` — 다듬을 문자열.
- `cut` — 잘라낼 **바이트들의 집합**이다(예: `" \t\n"`). 순서 있는 문자열이 아니라
  집합이라는 점이 중요하다 — `" \t"` 는 "공백 다음 탭" 이 아니라 "공백 또는 탭" 이다.
- 전부 잘리면 빈 뷰(길이 0)가 나온다 — `none` 이 아니다.

#### split_next
```lowent
export fn split_next input src str . input sep u8 . input pos u64 . output option str .
```
`src` 를 구분 바이트 `sep` 로 쪼갤 때, `pos` 부터 다음 `sep` 전까지의 조각을 `some` 으로
낸다.

- `src` — 쪼갤 원본. 매번 같은 것을 준다.
- `sep` — 구분 바이트 하나(예: 쉼표 = 44). 문자열이 아니라 바이트다.
- `pos` — **다음에 읽을 자리**. 이 인자가 있는 이유: **커서 전진은 호출자 몫**이다 —
  조각을 받으면 `pos ← pos + len(조각) + 1` 로 스스로 민다(+1 은 구분 바이트 한 칸이다).
  라이브러리가 상태를 안 들기 때문에 같은 `src` 를 여러 커서가 동시에 읽어도 자유다.
- 마지막 조각(뒤에 sep 없음)도 `some` 이다. `pos > len src` 면 `none` — 그것이 끝 신호다.
- 규약: `"a,b"` → `"a"`·`"b"` / `"a,"` → `"a"`·`""`(꼬리 빈 조각) / `""` → `""`(빈 조각 하나).

#### str_splitter (actor)
```text
export actor str_splitter do
  state do
    src slice u8 .
    sep u8 .
    pos u64 .
    fin bool .
  end
  proc init input s2 slice u8 . input b u8 . output u64 . effects state .
  proc next output option slice u8 . effects state .
end
```
`split_next` 의 pull 소스 판 — 커서를 actor 상태가 대신 든다.

- `init` 의 `s2` — 쪼갤 원본. `b` — 구분 바이트. 둘을 actor 안에 넣어 두므로 이후
  `next` 는 인자가 없다.
- `next` — 매개변수가 없다. 무엇을 어디까지 읽었는지 전부 actor 상태에 있기 때문이다.
  조각을 `option` 으로 내고 소진되면 `none` 이다.
- 쓰는 법: `spawn` 뒤 `send sp init <src> <sep>` 로 채우고 `send sp next` 를 반복한다.

## 사용법과 예제

기본 흐름은 "모듈을 `use` 로 끌어오고 → `strings.` 접두사로 부른다" 이다. 아래 예제는
전부 `impl/tests/vm_str.low` 의 통과 코드를 근거로 한다.

모듈 이름은 **파일 안의 `module` 선언**이다 — `lib/str.low` 의 모듈은 `strings` 다
(파일 이름이 아니다). 가져오는 길은 둘:

```lowent
use strings from "../../lib/str.low" .   rem 경로 직접 지정(저장소 안 상대 경로)
use strings .                            rem from-생략 — 예약(std) 해소: 설치본 std/, 개발 트리 lib/
use strings as s .                       rem 별칭 — 이후 s.eq_str 로 부른다
```

찾고·다듬고·쪼갠다(픽스처 `impl/tests/vm_str.low` 의 `t_find`·`t_split` 기반):

```lowent
module demo .

use strings from "../../lib/str.low" .

fn t_find output u64 . do
  let r option u64 . be strings.find "hello world" "world" 0 .   rem 0 부터 찾는다
  guard is_some r . else return 99 .          rem 못 찾았으면(none) 여기서 떠난다
  return some_value r .                       rem 검사를 통과했으니 꺼낸다 → 6
end

fn t_split output u64 . do
  rem "aa,b,,cc" 를 ','(바이트 44) 로: "aa"·"b"·""·"cc" — 4 조각.
  var pos u64 be 0 .                          rem 커서 — 호출자인 내가 든다
  var pieces u64 be 0 .                       rem 조각 개수 누산
  var r option slice u8 . be strings.split_next "aa,b,,cc" 44 pos .   rem 첫 조각
  while is_some r . do                        rem none 이 나오면 끝이다
    set pieces (add pieces 1) .               rem 조각 하나를 셌다
    set pos (add pos (add (len (some_value r)) 1)) .   rem ★ 커서 전진: 조각 길이 + sep 1
    set r (strings.split_next "aa,b,,cc" 44 pos) .     rem 전진한 pos 로 다음 조각
  end
  return pieces .                             rem 4
end
```

상태를 원하면 actor 판 — 커서를 actor 가 대신 든다(`vm_str.low` 의 `t_splitter` 기반):

```lowent
proc t_splitter output u64 . effects state . do
  var sp strings.str_splitter . be spawn actor strings.str_splitter .   rem actor 를 띄운다
  let d u64 be send sp init "one,two,three" 44 .   rem 원본과 구분 바이트를 채운다
  var pieces u64 be 0 .                            rem 조각 개수 누산
  var r option slice u8 . be send sp next .        rem 조각 하나를 당겨 온다
  while is_some r . do                             rem none 이 나오면 소진이다
    set pieces (add pieces 1) .                    rem 조각 하나를 셌다
    set r (send sp next) .                         rem 커서 전진은 actor 가 알아서 한다
  end
  return pieces .                             rem 3
end
```

두 판의 차이는 커서를 누가 드는가 하나뿐이다. 같은 원본을 여러 갈래로 동시에 훑을 일이
있으면 `split_next`(커서=값) 쪽이 맞고, 루프 하나로 끝까지 당길 뿐이면 actor 판이 짧다.

## 반례 — 이렇게 쓰면 안 된다

실제로 자주 밟는 잘못된 코드들이다. 각 반례에 **증상**을 적었다 — 컴파일 때 잡히는지,
프로그램이 안 끝나는지, 실행 중에 멈추는지가 대처를 가른다.

**✗ 빌트인 `eq` 로 슬라이스 비교:**

```lowent
guard eq "abc" "abc" . else return 0 .        rem ✗ eq 는 스칼라 전용이다
```

증상: **`E-VM-TYPE`** — `eq` 는 수 하나끼리만 비교한다. 슬라이스 비교는 `strings.eq_str`
로 간다. 진단에 이 코드가 그대로 뜨므로 알아채기는 쉬운 편이다.

```
E-VM-TYPE: eq 는 스칼라 전용이다 — 슬라이스는 strings.eq_str 로
```

**✗ 분할 루프에서 커서를 안 전진:**

```lowent
var r option slice u8 . be strings.split_next src 44 pos .
while is_some r . do
  set r (strings.split_next src 44 pos) .   rem ✗ pos 가 그대로 — 같은 조각이 계속 나온다
end
```

증상: 컴파일 오류가 **아니다** — **프로그램이 안 끝난다**(무한 루프). 같은 `pos` 를 계속
주므로 `split_next` 가 같은 조각을 영원히 낸다. 알아채는 법: 출력이 멈추지 않거나 시험이
시간 초과로 죽는다. 조각을 받을 때마다 `pos + len(조각) + 1` 로 전진해야 한다.

**✗ `option` 을 값처럼 쓰기:**

```lowent
let r option u64 . be strings.find "abc" "zz" 0 .
return some_value r .                         rem ✗ none 검사가 없다
```

증상: 컴파일은 통과하고, 못 찾은 순간 실행 중 **E-VM-NONE 트랩(패닉)** 으로 멈춘다
(`some_value of none`). 찾히는 입력으로만 시험하면 안 잡히므로 특히 방심하기 쉽다.
[7장](../07-guard-and-option.md)의 규율대로 `guard is_some r . else …` 가 먼저다.

**✗ 뷰가 가리키는 원본을 나중에 바꾸기:**

```lowent
let piece slice u8 be strings.remove_prefix line "GET " .   rem line 위의 뷰다
set (index line 4) 88 .                                     rem ✗ 원본을 고쳤다
```

증상: 에러가 하나도 없이 **`piece` 의 내용이 조용히 바뀐다** — 복사본이 아니라 창이기
때문이다. 알아채는 법: 값을 안 바꿨는데 나중에 읽은 문자열이 다르다. 내용을 붙잡아 두려면
[`strbuf`](strbuf.md)로 복사해 두어야 한다.

## 주의사항

초보자가 자주 걸리는 함정을 모았다. 대부분 "이 모듈은 복사를 안 한다" 와 "바이트 단위다"
에서 나온다.

- **op 은 모듈 접두사로 부른다** — `strings.find`·`strings.has`. 옛 `find` 같은
  `str_` 접두사 이름은 없다. 단 `eq_str`·`has_byte` 는 더 짧게 못 갔다 — `eq` 는 빌트인
  (스칼라 전용)이고 `contains` 도 빌트인(bitset 전용)이라 이름이 겹친다. 이런 충돌은
  자기 코드의 변수 이름에서도 만난다(빌트인과 겹치는 이름은 피한다).
- **바이트 단위다, 글자 단위가 아니다.** 한글·이모지처럼 여러 바이트로 된 문자는 이
  모듈에서 한 단위가 아니다. needle 로는 바이트열 그대로 찾히지만,
  `trim_start`/`trim_end` 의 `cut` 은 **바이트 집합**이라 멀티바이트 문자를 한 단위로
  다루지 못한다 — 글자 단위가 필요하면 `lib/utf8.low` 로 간다.
- **뷰는 원본과 수명·내용을 공유한다.** 원본 버퍼가 나중에 바뀌면 뷰가 가리키는 내용도
  바뀌고, 원본이 사라지면 뷰도 못 쓴다. "값을 받았다" 가 아니라 "창을 받았다" 로 읽는다.
- **`remove_prefix` 는 실패를 안 알린다.** 접두사가 없으면 원본 그대로가 돌아오므로,
  뗐는지 여부가 필요하면 `starts_with` 로 먼저 묻는다.
- `find` 는 소박한 탐색이라 O(len hay × len needle) 이다. 아주 긴 문자열을 반복해서
  훑는 자리에서는 이 비용을 계산에 넣는다.
- `split_next` 는 상태를 나눠 갖지 않으므로 같은 `src` 를 **여러 커서가 동시에** 읽어도
  자유다. `str_splitter` actor 는 반대로 상태를 들므로 spawn 한 곳이 소진까지 책임진다.
- `str → cstr`(널종단: 끝을 0 바이트로 표시하는 C 방식 문자열) 직행 경로는 **없다** —
  뷰는 널을 약속하지 못한다. [`strbuf`](strbuf.md)의 `as_cstr` 를 거친다.

---

[← 목차](../README.md)
