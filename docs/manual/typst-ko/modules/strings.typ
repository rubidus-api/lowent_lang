#import "../lib.typ": *

= `strings` --- 문자열 뷰 연산 <mod-strings>

#modhead(file: "lib/str.low", layer: [L0 --- 순수 계산], caps: [없음])

문자열을 *복사하지 않고* 비교하고, 찾고, 다듬고, 쪼갠다. 입력을 공백으로 나누거나, 접두사를 떼거나, 부분 문자열을 찾을 때 쓴다. 결과는 언제나
원본 위의 *뷰* --- 남의 바이트를 가리키기만 하는 창 --- 이고, 새 메모리가 한 바이트도 들지 않는다.

```lowent
use strings .

if strings.starts_with line. "GET " . do
  let rest slice u8 strings.remove_prefix line. "GET " . .
end .
```

`rest` 는 `line` 의 일부를 그대로 가리킨다. 이 모듈은 언어가 이미 가진 것(`len`·`idx`·`subslice`·`eq`·`option`·`guard`·`while`·`actor`)만으로
지어졌고 기본 연산을 하나도 늘리지 않았다. 문자열 연산의 대부분은 소유가 필요 없다 --- 자르기·찾기·비교는 뷰를 낼 뿐이다. 이어 붙이기처럼 쓰기 가능한
버퍼가 필요한 일은 #modref("strbuf")[`strbuf`] 가 맡는다.

== 설계와 경계

- *하는 것* --- 바이트 단위 비교·탐색·접두와 접미·다듬기·분할. 모든 `fn` 이 `effects none` 이고 아무것도 할당하지 않는다.
- *인코딩을 해석하지 않는다.* `str` 은 바이트다. UTF-8 검증과 해독은 #modref("utf8")[`utf8`] 의 일이다.
- *소유와 수정을 하지 않는다.* 원본을 한 바이트도 바꾸지 않는다.
- *`str_len`·`str_sub` 는 없다.* `str` 은 `slice u8` 의 별칭이므로 기본 연산 `len`·`subslice` 가 그대로 돈다. 같은 뜻에 두 이름을 두지 않는다.
- *자리표 값이 없다.* "−1 이면 못 찾음" 같은 관례를 쓰지 않는다. 못 찾음은 `option` 의 `none` 이다.

`def type str slice u8 .` 은 *투명 별칭*이다. `newtype` 이 아니므로 문자열 리터럴, `slice u8` 값, `subslice` 결과가 모두 그대로 `str` 자리에 들어간다.
상태를 드는 것은 `str_splitter` 액터 하나뿐이다 --- 원본 `src`, 구분 바이트 `sep`, 커서 `pos`, 소진 여부 `fin`.

== op 한눈에

모든 `fn` 은 `effects none` 이고, 액터의 `proc` 둘은 `effects state` 다. 부를 때는 `strings.find` 처럼 모듈 이름을 붙인다.

#dtable(
  columns: 3,
  id: "mod-strings-ops",
  caption: [`strings` 의 op],
  [*op*], [*시그니처*], [*안 될 때*],
  [`eq_str`], [`(a str, b str) → bool`], [다르면 `false`],
  [`has_byte`], [`(s str, b u8) → bool`], [없으면 `false`],
  [`find`], [`(hay str, needle str, from u64) → option u64`], [못 찾거나 `from > len hay` 이면 `none`],
  [`has`], [`(hay str, needle str) → bool`], [---],
  [`starts_with`], [`(s str, prefix str) → bool`], [---],
  [`ends_with`], [`(s str, suffix str) → bool`], [---],
  [`remove_prefix`], [`(s str, prefix str) → str`], [접두사가 없으면 *원본 그대로*],
  [`remove_suffix`], [`(s str, suffix str) → str`], [접미사가 없으면 원본 그대로],
  [`trim_start`], [`(s str, cut str) → str`], [모두 잘리면 빈 뷰],
  [`trim_end`], [`(s str, cut str) → str`], [모두 잘리면 빈 뷰],
  [`split_next`], [`(src str, sep u8, pos u64) → option str`], [`pos > len src` 이면 `none`],
  [`str_splitter`], [액터: `init(s2 slice u8, b u8) → u64` · `next → option slice u8`], [소진 뒤 `next` 는 `none`],
)

== op 상세

넘기는 `slice u8` 은 뷰이고 돌려받는 것도 같은 바이트 위의 다른 뷰다. 그래서 `mut` 가 아니고 `effects none` 이며, 원본이 살아 있는 동안만 유효하다.
비교와 검색의 두 인자는 늘 (찾을 대상, 찾는 것) 차례다 --- `has_byte s b` 는 "`s` 안에 `b` 가 있는가" 로 읽는다.

*`eq_str`* --- 바이트 단위 동등 비교. 길이가 다르면 바이트를 보기도 전에 `false` 다. 기본 연산 `eq` 는 스칼라 전용이라 슬라이스 비교가 여기 산다. 그래서
이름이 `eq` 로 짧아지지 못했다.

*`has_byte`* --- 바이트 `b` 하나가 `s` 안에 있는가. 문자열이 아니라 바이트 하나다. `trim_start`·`trim_end` 의 잘라낼 집합 검사가 이것을 쓴다.

*`find`* --- `hay` 에서 `needle` 을 `from` 부터 찾아 시작 색인을 `some` 으로 낸다. `from` 이 있는 이유가 이 모듈의 성격이다. *커서는 호출자가 드는
값이다.* 다음 검색은 찾은 자리 뒤를 `from` 으로 다시 부른다. 라이브러리가 상태를 어디에도 숨기지 않으므로, 같은 문자열을 여러 곳에서 동시에 훑어도
서로 간섭하지 않는다. `from > len hay` 면 `none`, 빈 `needle` 은 `some from. .` 이다.

*`has`* --- "들어 있는가" 만 물을 때. 속은 `is_some find hay. needle. 0 . .` 그대로다.

*`starts_with` · `ends_with`* --- 접두와 접미 검사. 조각이 `s` 보다 길면 볼 것도 없이 `false` 다.

*`remove_prefix` · `remove_suffix`* --- 접두나 접미를 뗀 뷰. 떼어 낼 조각이 *없으면 원본을 그대로* 돌려준다. 실패가 아니므로 반환이 `option` 이 아니다.
떼였는지 알아야 하면 먼저 `starts_with`·`ends_with` 로 묻는다.

*`trim_start` · `trim_end`* --- 앞이나 뒤에서 특정 바이트들을 걷어낸 뷰. `cut` 은 차례 있는 문자열이 아니라 *바이트의 집합*이다 --- `" \t"` 는 "공백
다음 탭" 이 아니라 "공백 또는 탭" 이다. 모두 잘리면 길이 0 인 뷰가 나온다.

*`split_next`* --- `src` 를 구분 바이트 `sep` 로 쪼갤 때, `pos` 부터 다음 `sep` 앞까지의 조각을 `some` 으로 낸다. *커서 전진은 호출자 몫이다* ---
조각을 받으면 `pos ← pos + len(조각) + 1` 로 스스로 민다(+1 은 구분 바이트 한 칸). 뒤에 `sep` 이 없는 마지막 조각도 `some` 이고, `pos > len src` 면
`none` 이 끝 신호다. `"a,b"` 는 `"a"`·`"b"`, `"a,"` 는 `"a"`·`""`(꼬리 빈 조각), `""` 는 `""` 하나다.

*`str_splitter`* --- `split_next` 의 당겨 읽기 판. 커서를 액터 상태가 든다. `init` 에 원본과 구분 바이트를 넣으면 이후 `next` 는 인자가 없고, 조각을
`option` 으로 내다가 소진되면 `none` 이다.

== 쓰는 법

모듈 이름은 파일 안의 `module` 선언이다. `lib/str.low` 의 모듈은 `strings` 다.

```lowent
use strings from "lib/str.low" .   rem 경로를 직접 적는다(부르는 파일에서의 상대 경로)
use strings .                            rem 표준 모듈 자리에서 풀린다
use strings as s .                       rem 별칭 --- 이후 s.eq_str 로 부른다
```

찾고 쪼갠다.

```lowent
module demo .

use strings .

fn t_find output u64 . do
  let r option u64 strings.find "hello world" "world" 0 . .
  guard is_some r. . else return 99 . .
  return some_value r. . .
end .

fn t_split output u64 . do
  rem "aa,b,,cc" 를 ',' (바이트 44) 로: "aa"·"b"·""·"cc" --- 네 조각
  var pos u64 0 .
  var pieces u64 0 .
  var r option slice u8 strings.split_next "aa,b,,cc" 44 pos. . .
  while is_some r. . do
    set pieces. add pieces. 1 . .
    set pos. add pos. add len some_value r. . . 1 . . .
    set r. strings.split_next "aa,b,,cc" 44 pos. . .
  end .
  return pieces. .
end .
```

`t_find` 는 6 을, `t_split` 은 4 를 낸다. 상태를 원하면 액터 판을 쓴다 --- 커서를 액터가 든다.

```lowent
proc t_splitter output u64 . effects state . do
  var sp strings.str_splitter spawn actor strings.str_splitter . .
  let d u64 send sp. init "one,two,three" 44 . .
  var pieces u64 0 .
  var r option slice u8 send sp. next . .
  while is_some r. . do
    set pieces. add pieces. 1 . .
    set r. send sp. next . .
  end .
  return pieces. .
end .
```

두 판의 차이는 커서를 누가 드는가 하나다. 같은 원본을 여러 갈래로 동시에 훑을 일이 있으면 `split_next` 가 맞고, 반복 하나로 끝까지 당길 뿐이면 액터
판이 짧다.

== 반례

#antipattern[기본 연산 `eq` 로 슬라이스를 비교한다][
  ```lowent
  guard eq "abc" "abc" . else return 0 . .        rem ✗ eq 는 스칼라 전용이다
  ```
  `E-VM-TYPE` 이다. 슬라이스 비교는 `strings.eq_str` 로 한다.
]

#antipattern[분할 반복에서 커서를 밀지 않는다][
  ```lowent
  var r option slice u8 strings.split_next src. 44 pos. . .
  while is_some r. . do
    set r. strings.split_next src. 44 pos. . .   rem ✗ pos 가 그대로
  end .
  ```
  번역 오류가 아니다 --- *프로그램이 끝나지 않는다.* 같은 `pos` 를 주므로 같은 조각이 영원히 나온다. 조각을 받을 때마다 `pos + len(조각) + 1` 로 민다.
]

#antipattern[`option` 을 값처럼 쓴다][
  ```lowent
  let r option u64 strings.find "abc" "zz" 0 . .
  return some_value r. . .                         rem ✗ none 검사가 없다
  ```
  번역은 통과하고, 못 찾은 순간 실행 중 `E-VM-NONE` 으로 멈춘다. 찾히는 입력으로만 시험하면 드러나지 않는다. `guard is_some r else …` 가 먼저다
  (#chref("option-result")).
]

#antipattern[뷰가 가리키는 원본을 나중에 바꾼다][
  ```lowent
  let piece slice u8 strings.remove_prefix line. "GET " . .
  set idx line. 4 . 88 .                                     rem ✗ 원본을 고쳤다
  ```
  오류 없이 `piece` 의 내용이 조용히 바뀐다 --- 복사본이 아니라 창이기 때문이다. 내용을 붙잡아 두려면 #modref("strbuf")[`strbuf`] 로 복사한다.
]

== 주의

- *바이트 단위다, 글자 단위가 아니다.* 한글이나 이모지처럼 여러 바이트로 된 문자는 한 단위가 아니다. `needle` 로는 바이트열 그대로 찾히지만, `cut` 은
  바이트 집합이라 멀티바이트 문자를 한 단위로 다루지 못한다. 글자 단위는 #modref("utf8")[`utf8`] 로 간다.
- *뷰는 원본과 수명과 내용을 함께한다.* 원본이 바뀌면 뷰의 내용도 바뀌고, 원본이 사라지면 뷰도 못 쓴다. "값을 받았다" 가 아니라 "창을 받았다" 로 읽는다.
- *`remove_prefix` 는 실패를 알리지 않는다.* 뗐는지가 필요하면 `starts_with` 로 먼저 묻는다.
- *`find` 는 소박한 탐색이다.* 비용은 O(`len hay. .` × `len needle. .`) 이다. 아주 긴 문자열을 되풀이해 훑는 자리에서는 이 비용을 계산에 넣는다.
- *커서 판과 액터 판.* `split_next` 는 상태를 나누지 않으므로 여러 커서가 동시에 읽어도 자유롭다. `str_splitter` 는 상태를 들므로 띄운 곳이 소진까지 책임진다.
- *널 종단 문자열로 바로 가는 길은 없다.* 뷰는 끝의 0 바이트를 약속하지 못한다. #modref("strbuf")[`strbuf`] 의 `as_cstr` 를 거친다.
- *기본 연산 이름과 겹치는 이름.* `eq` 는 기본 연산이라 이 모듈이 `eq_str` 로 이름을 지었다. `has_byte` 는 비트셋의 `contains` 가 내장이던 때 지은 이름이다(지금은 `bitset_contains`). 내 코드의 변수 이름도 기본 연산 이름을 피한다.
