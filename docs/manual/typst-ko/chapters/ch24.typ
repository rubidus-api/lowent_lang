#import "../lib.typ": *

= `pipe` — 하려는 일만 한 줄씩

#chapter-toc()

#prereq(
  ([#chref("control") 흐름], [`for` 는 슬라이스를 훑고 세는 반복은 `while` 로 적는다]),
  ([#chref("slices") 줄], [`mut slice` 여야 원소에 쓸 수 있다]),
  ([#chref("generics") 제네릭], [값 대신 이름 붙은 op 을 건넨다]),
)

#deepqa[
  #chref("control")에서 "`for` 로 0 부터 `n` 까지 세는 반복은 어떻게 적는가" 라는 물음에, 슬라이스를 걸러 세거나 모으는
  일은 무엇이 맡는다고 했는가?
][
  `pipe` 가 맡는다고 했다. 세는 반복은 `while` 과 `var` 로 적고, 슬라이스를 거르고 바꾸고 모으는 일은 `pipe` 로 적는다.
  이 장이 그 `pipe` 를 다룬다.
]

#why[
  슬라이스를 훑는 반복은 거의 언제나 같은 뼈대다. 색인을 두고, 끝인지 보고, 원소를 꺼내 조건을 보고, 무언가를 쌓고,
  색인을 늘린다. 이 뼈대를 손으로 쓰면 *하려는 일*("숫자만 남기고 센다")이 색인과 카운터 사이에 묻히고, 경계를 한 칸
  틀리는 실수가 거기서 난다. `pipe` 는 뼈대를 언어가 맡고 사람은 하려는 일만 줄마다 한 낱말로 적게 한다. 그러면서도
  손으로 쓴 반복과 똑같이 한 번만 훑는다. 추상(제6부)의 마지막 장으로, 추상이 비용을 숨기지 않는 모양을 보인다.
]

#organizer[
  `pipe <원천> do <스테이지…> <종결자> end` 의 모양과, 스테이지 일곱·종결자 다섯을 알게 된다. 스테이지에 이름 붙은
  op 을 건네는 법, `collect into` 로 호출자의 버퍼에 담는 법, `fold`·`any`·`all`·`take` 가 필요한 만큼만 읽는다는 것을
  익힌다. 한 `pipe` 가 중간 배열 없이 한 번에 훑는 것이 최적화가 아니라 *정의*라는 점도 이해하게 된다.
]

#chapter-questions()

== 같은 일, 두 가지 모양

#demo("examples/ch24/digits.low")

두 op 은 같은 답을 낸다. `digits_loop` 는 카운터 `n`, 색인 `i`, 끝 조건, 증가를 사람이 맞춘다. `digits_pipe` 는 두 줄이다.

#idx("pipe")
- `filter is_digit .` --- `is_digit` 이 참인 원소만 남긴다.
- `count .` --- 남은 원소를 센다. 흐름을 끝내는 낱말(*종결자*)은 정확히 하나다.

`pipe` 는 문장이지만 값을 내는 종결자(`count`·`fold`·`any`·`all`)로 끝나면 식처럼 `return pipe s do … end .` 로 쓸 수 있다.

== 스테이지와 종결자

#dtable(
  columns: 3,
  id: "pipe-words",
  caption: [`pipe` 의 낱말],
  [*자리*], [*낱말*], [*하는 일*],
  [스테이지], [`filter <op>`], [op 이 참인 원소만 통과시킨다],
  [], [`map <op>`], [원소마다 op 을 적용한다],
  [], [`take <n>` · `skip <n>`], [앞의 `n` 개만 통과시킨다 · 버린다],
  [], [`enumerate <op>`], [순번과 원소를 op 에 준다],
  [], [`zip <슬라이스> <op>`], [다른 슬라이스의 짝 원소와 op 으로 합친다],
  [], [`scan <초기값> <op>`], [누적한 값을 원소로 흘린다],
  [종결자], [`collect into <버퍼>`], [남은 원소를 버퍼에 담는다],
  [], [`fold <초기값> <op>`], [누적해 값 하나를 낸다],
  [], [`count`], [남은 원소를 센다],
  [], [`any <op>` · `all <op>`], [하나라도 참인가 · 모두 참인가],
)

#demo("examples/ch24/stages.low")

- `big_doubled` 는 2 보다 큰 것만 남겨(`filter`) 두 배로 만들고(`map`) `out` 에 담는다(`collect into`). 결과
  `[6,8,10,0,0]` 에서 담기지 않은 자리는 그대로다. 담을 버퍼는 *부르는 쪽이 준다.* `pipe` 는 할당하지 않는다.
- `sum_big` 의 `fold 0 addu` 는 0 에서 시작해 `addu` 로 누적한다.
- `has_zero` 는 `any is_zero`, `all_small` 은 `all under10` 이다.
- `middle` 은 `skip 1` 과 `take 2` 로 가운데 두 원소를 담는다.

스테이지에 주는 것은 *이름 붙은 op* 이다. 이 언어에는 이름 없는 함수(람다)가 없다. 스테이지 op 은 원소 하나를 받고
권한을 받지 않는다. `fold`·`scan` 의 op 은 누산값과 원소 둘을 받는다.

튜플이 없는데 순번이나 짝은 어떻게 다루는가? 쌍을 *만들지 않고 op 에 인자로 건넨다*.

#demo("examples/ch24/pairs.low")

`enumerate idxadd` 는 `idxadd` 에 순번과 원소를 준다. `zip ys addb` 는 `xs` 의 원소와 `ys` 의 같은 자리 원소를 `addb` 에
준다. 짧은 쪽이 끝나면 흐름 전체가 끝나서 짝이 늘 맞는다. `scan 0 addb` 는 누적합을 원소로 흘린다. 쌍을 만들었다 곧장
풀지 않으므로 숨은 할당도 없다.

#qa[
  `collect into` 로 쓰는 op 인데 머리가 `effects none` 이다. 호출자의 버퍼를 바꾸는데 효과가 없는가?
][
  명세의 예제도 `effects none` 으로 적고, 이 판의 처리기는 `collect into` 의 쓰기를 `state` 효과로 세지 않는다(`state`
  를 적으면 "선언했지만 하지 않는다" 는 경고가 난다). 반면 `set (index xs i) …` 로 호출자의 슬라이스에 쓰면
  `effects state` 를 요구한다. 두 판정이 어긋나는 자리이고, 이 책은 결함으로 본다. 어느 쪽이든 호출자는 `mut slice` 를
  건네는 자리에서 쓰기가 일어날 수 있다는 것을 안다(#chref("references")).
]

== 한 번에 훑는 것은 정의다

한 `pipe` 는 *한 번의 훑기*로 실행되고 스테이지 사이에 중간 배열이 만들어지지 않는다. 이것은 처리기가 해 볼 수 있는
최적화가 아니라 `pipe` 의 정의이고, 중간 배열을 만드는 처리기는 적합하지 않다.

왜 정의로 두는가. 융합을 최적화로 두면 "어디까지 융합되는가" 가 처리기마다 다르고, 쓰는 사람은 *절벽*을 만난다. 한
줄을 고쳤더니 갑자기 중간 배열이 생기고 느려지는 자리다. 그 절벽은 소스만 봐서는 안 보인다. Lowent 는 융합할 수 없는
연산을 스테이지 낱말에 *아예 넣지 않았다*. 정렬처럼 전체를 봐야 하는 연산이 스테이지에 없는 이유다.

#demo("examples/ch24/bad_stage.low")

스테이지 목록은 닫혀 있다. `pipe` 가 식이 아니라 문장인 것도 같은 이유다. 식이었다면 스테이지를 값으로 떼어 넘길 수
있고, 그러면 어디까지가 한 줄기인지 소스에서 안 보인다. `do … end` 가 그 경계를 눈에 보이게 한다.

== 필요한 만큼만 읽는다

`any` 는 참을 내는 첫 원소에서, `all` 은 거짓을 내는 첫 원소에서, `take n` 은 `n` 개를 지나보낸 뒤 훑기를 멈춘다. 멈춘
뒤의 원소는 읽히지 않고 그 원소에 대한 스테이지도 실행되지 않는다. `has_zero [4,0,9]` 는 9 를 보지 않는다. 이 성질이
의미에 적혀 있어야 끝이 없는 원천도 `pipe` 로 다룰 수 있다.

종결자는 흐름을 끝낸다. 종결자 뒤에 스테이지가 오면 거절된다.

#demo("examples/ch24/no_terminal.low")

#misconception[`pipe` 는 편하지만 손으로 쓴 반복보다 느리다][
  많은 언어에서 반복자 체인은 중간 객체나 간접 호출을 남기거나, 최적화기가 운 좋게 걷어내기를 기대한다. `pipe` 는 한
  번 훑기가 정의이고 스테이지 op 은 단형화된 직접 호출이다. `pipe` 하나는 반복 하나로 낮아지고, 비용은 손으로 쓴 반복과 같은
  자리에서 생긴다. 편한 쪽과 빠른 쪽이 갈리지 않도록 낱말을 고른 결과다.
]

== 흔한 실수

#antipattern[스테이지에 식을 적어 람다를 흉내 낸다][
  #demo("examples/ch24/mistake_lambda.low")

  다른 언어의 `filter(x => x > 2)` 를 옮기면 `filter gt 2` 가 되기 쉽다. 스테이지는 *이름 붙은 op 하나*를 받으므로 `E-FOLD-OP` 다(진단은
  "그런 op 이 없다" 고만 말한다). 조건에 이름을 붙이는 것은 번거로워 보이지만, `over2` 라는 이름이 곧 그 줄의 설명이 되고 같은 조건을 다른
  `pipe` 에서도 쓴다.

  #demo("examples/ch24/lambda_fixed.low")
]

#antipattern[넓은 값을 내는 `map` 을 좁은 버퍼에 담는다][
  #demo("examples/ch24/mistake_widecollect.low")

  `times1000` 은 `u64` 를 내는데 `out` 은 `u8` 버퍼다. 1000 과 2000 이 232 와 208 로 담긴다. Lowent 의 원칙대로라면 값을 잃는 좁히기는
  `narrow` 를 적어야만 일어나야 하지만, 이 판의 도구는 `collect into` 의 타입을 대조하지 않고 조용히 감는다(개발 저장소에 결함으로 적어
  두었다). `map` 의 출력 타입과 버퍼의 원소 타입을 눈으로 맞춘다. 좁혀야 한다면 `map` 의 op 안에서 `narrow` 로 적어 멈출 자리를 드러낸다.
]

#antipattern[`fold` 의 op 에서 누산값과 원소의 차례를 바꾼다][
  #demo("examples/ch24/mistake_foldorder.low")

  `fold` 는 op 에 *누산값을 먼저, 원소를 다음* 에 준다. `add_small_swapped` 는 차례를 거꾸로 받았으므로 `x` 에 누산값이, `acc` 에 원소가
  들어온다. 100 이상인 원소를 건너뛰어야 할 조건이 누산값에 걸려 200 이 더해지고 답이 206 이 된다. 타입이 `u8` 과 `u64` 로 달라도 이 판의
  도구는 대조하지 않는다(결함으로 적어 두었다). `fold`·`scan` 의 op 은 언제나 `input acc … . input x … .` 차례로 적는다.
]

#antipattern[멈출 수 있는 스테이지 op 을 쓰고 `fn` 으로 적는다][
  #demo("examples/ch24/mistake_stageeffect.low")

  `pipe` 는 스테이지 op 을 부르는 반복이므로, 스테이지 op 의 효과가 `pipe` 를 쓴 op 으로 번진다. `nonzero` 가 `panic` 할 수 있으니
  `count_checked` 도 `panic` 을 낸다. 그래서 `fn` 이면 `E-EFFECT-CALC` 다. `proc … effects panic .` 으로 적거나, 멈추는 대신 조건을 만족하지
  않는 원소를 걸러 내는 순수한 op 을 쓴다.
]

#misconception[`collect into` 는 버퍼가 모자라면 멈춘다][
  #demo("examples/ch24/short_buffer.low")

  남는 원소는 넷(3·4·5·6)인데 `out` 은 두 칸이다. 담기는 버퍼가 찰 때 끝나고 멈추지 않는다. `pipe` 는 할당하지 않으므로 버퍼를 늘릴 수도 없다.
  모두 담겼는지 알아야 하면 같은 스테이지에 `count` 로 끝나는 `pipe` 를 하나 더 두어 개수를 먼저 재고, 버퍼의 길이와 견준다.
]

== 이 장의 문법 한눈에

#dtable(
  columns: 3,
  id: "pipe-glance",
  caption: [`pipe` 의 문법 --- 모양 · 뜻 · 왜 이렇게 생겼나],
  [*모양*], [*뜻*], [*왜 이렇게*],
  [`pipe xs do … end .`], [원천 `xs` 를 한 번 훑는다], [뼈대(색인·끝 조건)를 언어가 맡는다],
  [`filter over2 .` · `map dbl .`], [남기기 · 바꾸기 --- 이름 붙은 op 을 건넨다], [람다가 없다 --- 이름이 설명이 된다],
  [`take 2 .` · `skip 1 .`], [앞 몇 개만 · 앞 몇 개 버리기], [필요한 만큼만 읽는다],
  [`enumerate idxadd .` · `zip ys addb .`], [순번·짝을 op 의 인자로 건넨다], [튜플을 만들지 않는다],
  [`scan 0 addb .` · `fold 0 addu .`], [누적해 흘리기 · 누적해 값 하나], [op 은 누산값이 먼저, 원소가 다음],
  [`count .` · `any is_zero .` · `all under10 .`], [값을 내는 종결자], [`return pipe … end .` 로 쓸 수 있다],
  [`collect into out .`], [부르는 쪽 버퍼에 담는다], [`pipe` 는 할당하지 않는다 --- 차면 끝난다],
  [종결자는 정확히 하나, 맨 끝], [뒤에 스테이지가 오면 `E-PIPE-NO-TERMINAL`], [흐름의 끝이 한곳에 보인다],
  [`sort` 같은 낱말], [없다 --- `E-PIPE-STAGE`], [융합할 수 없는 연산은 넣지 않았다],
)

#recap[
  `pipe <원천> do … end` 는 스테이지(`filter`·`map`·`take`·`skip`·`enumerate`·`zip`·`scan`)를 거쳐 종결자
  (`collect into`·`fold`·`count`·`any`·`all`) 하나로 끝난다. 스테이지에는 이름 붙은 op 을 건네고, 담을 버퍼는 부르는
  쪽이 준다. 한 번 훑기와 중간 배열 없음은 정의이며, 융합할 수 없는 연산은 낱말에 없다. `any`·`all`·`take` 는 필요한
  만큼만 읽는다.
]
