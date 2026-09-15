#import "../lib.typ": *

= 답을 담는 타입 — `option` 과 `result`

#chapter-toc()

#prereq(
  ([#chref("control") 흐름], [`guard` 와 `panic`]),
  ([#chref("structs-enums") 묶음], [값을 지닌 `enum` 과 `match`]),
)

#deepqa[
  #chref("structs-enums")의 `case rect w h .` 는 무엇을 하는가? 그리고 `match` 가 갈래 하나를 빠뜨리면
  어떻게 되는가?
][
  갈래가 `rect` 이면 그 갈래가 지닌 두 값을 `w` 와 `h` 에 묶는다. 갈래를 빠뜨리면
  `E-MATCH-INEXHAUSTIVE` 로 거절된다. 이 장의 `option` 과 `result` 는 언어가 미리 만들어 둔 두 갈래짜리
  열거형처럼 행동한다 --- 값이 있거나 없거나, 성공이거나 실패거나.
]

#why[
  실패를 −1 이나 널 포인터 같은 특별한 값으로 알리는 관습은 "이 −1 은 오류인가 그냥 −1 인가" 를 소스만
  보고 알 수 없게 만든다. 그리고 확인을 빠뜨려도 아무도 모른다. `option` 과 `result` 는 그 물음을 타입으로
  옮긴다. 데이터를 다루는 제3부의 한가운데에 이 장이 있는 것은, 조회·변환·파싱처럼 데이터를 꺼내는 거의
  모든 op 이 "없을 수 있음" 이나 "실패할 수 있음" 을 돌려주기 때문이다.
]

#organizer[
  `option` 을 `some`·`none` 으로 만들고, 묻고 꺼내거나 `value_or` 로 대신할 값을 주거나 `match` 로 가르는
  법을 익힌다. 확인 없이 꺼내면 멈춘다는 것을 알게 된다. `result` 와 `errors` 절이 짝을 이루고, `try` 가
  실패를 위로 넘기며, `else_none`·`else_error` 꼬리가 둘 사이를 건넌다는 것도 보게 된다. 실패를 말하는 세
  길 --- `result`·`option`·`panic` --- 을 가르는 기준을 세우고, or 패턴·겹친 패턴·번역 시점에 접히는 `match` 도 보게 된다.
]

#chapter-questions()

== `option` — 값이 있거나 없거나

#idx("option")
`option t` 는 `t` 값이 있거나(`some v`) 없거나(`none`) 둘 중 하나다.

#demo("examples/ch11/lookup.low")

만드는 쪽인 `find` 는 `return none .` 과 `return some (mul k 10) .` 으로 값을 싼다. VM 은 결과를 `some 20`
과 `none` 으로 보여 준다. 받는 쪽은 세 가지로 쓸 수 있다.

- `find_or` --- `value_or (find k) 99` 는 값이 있으면 그 값을, 없으면 99 를 준다.
- `find_asked` --- `guard is_some r . else …` 로 먼저 묻고 `some_value r` 로 꺼낸다.
- `find_match` --- `match` 로 `case some v .` 와 `case none .` 을 가른다. 두 갈래로 모든 경우가 덮인다.

`find_raw` 는 묻지 않고 바로 꺼낸다. 번역은 통과하지만 값이 없는 7 에서 실행이 멈춘다(`E-VM-NONE`).
꺼내는 연산은 *부분 연산*이다. 어느 길에서 값이 있는지는 저자가 아는 것이고 처리기가 언제나 알 수는
없으므로 번역이 막지 않는다. 대신 틀렸을 때 조용하지 않다 --- 0 을 내주고 계속 가지 않는다.

#qa[
  `value_or` 의 기본값 자리에 비싼 계산을 적으면 매번 계산되는가?
][
  아니다. 기본값은 *값이 없을 때만* 계산된다. `value_or (some 7) (div 1 0)` 은 7 이고 0 나누기는 일어나지
  않는다. 그래서 기본값 자리에 실패할 수 있는 계산을 적어도 된다. 이 동작은 한때 반대였다가 명세에 맞게
  고쳐졌다.
]

== `result` 와 `errors` 절

#idx("result")
`result t e` 는 성공한 값(`ok v`) 또는 오류(`error <갈래>`) 중 하나다. 오류 타입 `e` 는 보통 `enum` 이다.
#idx("errors 절")
그리고 `result` 를 돌려주는 op 은 *언제 어떤 오류를 내는지* `errors` 절에 적는다.

#demo("examples/ch11/halve.low")

`halve` 의 머리에는 오류가 둘 적혀 있다. `errors too_big gt a 200 .` 은 "`a` 가 200 보다 크면 `too_big` 을
낸다" 는 약속이다. 이 절은 *나가는 쪽의 계약*이다. 조건이 참인데 op 이 정상으로 돌아오거나, 적지 않은
오류를 돌려주면 계약 위반이다. 적지 않은 오류를 돌려주는 코드는 번역에서 거절된다.

#demo("examples/ch11/undeclared.low")

`too_long` 은 `parse_error` 의 갈래이지만 `first_byte` 의 `errors` 절에는 없다. 부르는 쪽은 머리만 보고
`empty` 만 다루면 된다고 믿을 것이다. 적은 것과 내는 것이 어긋나면 한쪽은 못 본 실패를, 다른 쪽은 오지
않을 실패를 다루게 된다.

== `try` — 실패를 위로 넘긴다

#idx("try")
실패를 확인하는 코드를 매번 손으로 쓰면 길어지고, 길어지면 빼먹는다. `try` 는 `result` 를 받아 성공이면
값을 꺼내고, 실패면 *그 오류를 그대로 돌려주며 op 을 떠난다.* `halve.low` 의 `halve_plus_one` 이 그
모양이다.

```lowent
let v u8 be try halve a .
return ok (add v 1) .
```

`halve_plus_one` 이 자기 머리에도 `errors` 를 적은 것에 주목한다. `try` 로 오류를 넘기려면 자기도 그
오류를 돌려줄 수 있어야 하고, 그 사실을 계약에 적어야 한다. 실패가 조용히 사라지는 길이 없다. 실패를
위로 넘기지 않고 이 자리에서 다루려면 `halve_or_zero` 처럼 `is_error` 로 묻고 `ok_value` 로 꺼낸다.

#misconception[`try` 는 예외 처리의 `try` 다][
  Java 나 C++ 의 `try` 는 블록 안 어디서든 던져진 예외를 잡는 자리다. Lowent 의 `try` 는 *식 하나*에
  붙어서 그 식의 실패를 *위로 넘기는* 표시이고, Rust 의 `?` 에 가깝다. 던지고 잡는 제어 흐름은 없다. 실패는
  언제나 값으로 돌아오고, 어느 식에서 넘어갈 수 있는지가 소스에 적혀 있다.
]

== 두 채널 사이를 건넌다

부르는 op 과 불리는 op 의 채널이 다를 때가 있다. `try` 뒤에 꼬리를 붙이면 담는 그릇을 바꾼다.

#demo("examples/ch11/tails.low")

#dtable(
  columns: 3,
  id: "optres-tails",
  caption: [`try` 의 꼬리로 채널을 바꾼다],
  [*모양*], [*방향*], [*무엇을 잃거나 얻나*],
  [`try <식> else_none`], [`result` → `option`], [오류를 *버린다*. 왜 실패했는지 더는 말하지 않는다],
  [`try <식> else_error <갈래>`], [`option` → `result`], [없음에 *이름을 붙인다*],
)

꼬리를 붙인 `try` 는 타입도 바꾼다. `try (halve a) else_none` 의 타입은 `option u8` 이지 `u8` 이 아니다.
그래서 `maybe_half` 는 그것을 그대로 돌려주고, `let v u8 be try … else_error …` 처럼 값 타입에 담으려 하면
거절된다.

`else_none` 은 정보를 버리는 선택이다. 편해서 습관이 되기 쉬운데, 그 순간부터 호출자는 "왜" 를 물을 수
없다. 버릴 만한 자리에서만 버린다.

== 실패를 말하는 세 길

#dtable(
  columns: 3,
  id: "optres-channels",
  caption: [실패를 말하는 세 길],
  [*무엇*], [*무엇을 말하나*], [*부르는 쪽이 하는 일*],
  [`result t e`], [고칠 수 있는 실패], [어느 쪽인지 묻고 다룬다. 안 다루면 위로 넘긴다],
  [`option t`], [값이 없음], [있는지 묻고 꺼내거나, 대신 쓸 값을 준다],
  [`panic` · 계약 위반], [약속이 깨졌다], [다룰 수 없다. 프로그램이 멈춘다],
)

셋을 가르는 물음은 "부르는 쪽이 무엇을 할 수 있는가" 다. 파일이 없으면 다른 파일을 열어 볼 수 있으니
`result` 다. 찾는 것이 목록에 없으면 그냥 없는 것이니 `option` 이다. 부르는 쪽이 계약을 어겼다면 이미
약속이 깨진 것이라 고칠 수 있는 일이 아니고, 그래서 멈춘다. `panic` 은 되돌아 풀리지 않는다. 중간에 잡아
이어 가는 길은 없다. 이 기준으로 op 의 실패를 설계하는 법은 #chref("errors-design")에서 다시 다룬다.

== 패턴을 묶고 겹친다

`option`·`result`·`enum` 을 모두 보았으니 `match` 의 패턴을 더 넓게 쓸 수 있다.

#demo("examples/ch11/patterns.low")

#idx("패턴")
- *or 패턴.* `case red or green .` 은 어느 하나라도 맞으면 그 갈래다. 갈래마다 망라에 기여하므로 `blue` 까지 다루면 `_` 가 필요 없다. 값을 지닌
  갈래를 or 로 묶을 때는 *모든 가지가 같은 이름을 묶어야* 한다 --- `combine` 은 `plus` 든 `times` 든 `l`·`r` 을 꺼내 쓴다. 이름이 어긋나면
  `E-MATCH-ORBIND` 다.
- *겹친 패턴.* `case ok (some x) .` 는 `result` 안의 `option` 을 한 번에 가른다. 겹친 패턴은 단락 평가라서 `ok` 가 아니면 안쪽을 아예 보지 않는다.
  그래서 `error` 에서 값을 잘못 꺼내 멈추는 일이 없다.
- *번역 시점에 접힌다.* 가르는 값이 번역 시점 상수 --- 리터럴, `comptime <식>`, `config <이름>` --- 이면 `match` 는 맞는 갈래 하나로 접혀 실행 중
  비교가 없다. 죽은 갈래도 타입 검사는 받는다. C 의 `#ifdef` 와 다른 점이다(#chref("build-test")).
- *범위가 타입을 덮는다.* `half` 의 두 범위는 `u8` 의 0 … 255 를 빈틈 없이 덮으므로 `_` 가 없어도 망라다. 빈틈이 있으면
  `E-MATCH-INEXHAUSTIVE`, 범위가 겹치면 `E-MATCH-REDUNDANT` 다.

`_` 뒤에 갈래를 두면 거절된다.

#demo("examples/ch11/arm_after_wild.low")

`_` 가 이미 모두 받았으므로 뒤의 갈래는 영영 돌지 않는다. 죽은 갈래는 경고가 아니라 오류다 --- 읽을 때 각 경우가 한 번씩이 아닌 `match` 는 결함을
숨긴다.

#recap[
  `option` 은 `some`·`none`, `result` 는 `ok`·`error` 로 만든다. 받는 쪽은 묻고 꺼내거나(`is_some`·`some_value`
  · `is_error`·`ok_value`), `value_or` 로 대신할 값을 주거나, `match` 로 가른다. 꺼내기는 부분 연산이라
  없는 쪽을 꺼내면 멈춘다. `result` 를 돌려주는 op 은 `errors` 절로 오류를 약속하고, `try` 는 실패를 위로
  넘기며, `else_none`·`else_error` 꼬리는 채널과 타입을 바꾼다. 패턴은 or 로 묶고(같은 이름을 묶는다) 겹쳐 쓸 수 있으며, 상수를 가르는
  `match` 는 번역에서 접힌다.
]
