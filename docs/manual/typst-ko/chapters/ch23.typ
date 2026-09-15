#import "../lib.typ": *

= 트레이트 — 여러 타입이 지키는 한 가지 약속

#chapter-toc()

#prereq(
  ([#chref("structs-enums") 묶음], [`struct` 와 `field`]),
  ([#chref("effects") 효과], [효과 줄은 좁히는 절이고 부르는 쪽으로 번진다]),
  ([#chref("generics") 제네릭], [`requires <트레이트> t .` 는 타입 조건이다]),
)

#deepqa[
  #chref("generics")의 `max_of` 는 `requires ordered t .` 를 믿고 본문에서 무엇을 불렀는가? 그리고 조건을 갖추지 못한
  `plain` 을 주면 어떻게 되었는가?
][
  `method a less b` 를 불렀다. `plain` 을 주면 `E-BOUND-UNSAT` 으로 거절되었다. 조건은 약속이고, 약속을 넘어 실물을
  만들면 받는 쪽의 계약이 거짓이 되기 때문이다. 이 장은 그 약속 --- 트레이트 --- 를 선언하고 갖추는 법 전체를 다룬다.
]

#why[
  사각형과 정사각형은 넓이를 구하는 방법이 다르지만 "넓이를 알려 준다" 는 약속은 같다. 그 약속에 이름을 붙이면, "넓이를
  알려 주는 것이면 무엇이든" 받는 op 을 한 번만 쓸 수 있다. 이 언어에는 상속이 없으므로 여러 타입을 한 이름으로 다루는
  길은 트레이트다. 할당기(#chref("fixed-memory"))와 정렬(#chref("generics"))에서 이미 쓴 도구를 여기서 제대로 편다.
]

#organizer[
  타입에 op 을 붙이고(`fn rect.area`) `method` 로 부르는 법을 익힌다. 트레이트를 선언하고 `satisfies` 로 갖추며, op 을
  여럿 가진 트레이트를 쓰는 법을 알게 된다. 트레이트 서명에 `fn`·`proc` 을 적지 않고 `effects` 줄이 갖추는 쪽을 정한다는
  규칙, 갖추지 못했을 때의 진단들, `via self` 와 트레이트가 상속이 아니라는 점도 보게 된다.
]

#chapter-questions()

== 무엇에 쓰나

#demo("examples/ch23/why.low")

#idx("트레이트")
- `trait shape do … end` 는 "`area` 를 갖춘다" 는 약속이다. 서명의 `self` 는 이 약속을 갖출 타입 자신이다.
- `rect` 와 `square` 는 몸 안에 `satisfies shape .` 를 적어 약속을 갖추겠다고 선언한다.
- `fn rect.area` 와 `fn square.area` 가 실제로 갖춘다. 이름의 `rect.` 는 그 op 이 `rect` 에 붙었다는 뜻이다.
- `double_area` 는 *어떤 모양이든* 받는다. `requires shape t .` 가 "넓이를 알려 주는 타입만" 이라고 못 박는다.
- `method s area` 는 `s` 의 타입에 붙은 `area` 를 부른다. `rect` 면 `rect.area`, `square` 면 `square.area` 다.

`double_area rect r` 과 `double_area square q` 는 각각 전용 실물로 단형화되므로(#chref("generics")), `method` 는 실행 중에
찾는 것이 아니라 번역할 때 정해진다. 가상 함수 표가 없다.

== 타입에 붙은 op 과 `method`

트레이트와 상관없이도 op 을 타입에 붙일 수 있다. 이름 앞에 타입 이름과 점을 붙이면 되고, 첫 입력은 그 타입의 값이다.
#idx("method")
`method <값> <이름> <인자…>` 가 그 op 을 부른다. 수신자가 다른 폼의 결과여도 된다 --- `method (method r grow 1) area`.
붙은 op 이 없으면 거절된다.

#demo("examples/ch23/method_undef.low")

없는 이름을 부르면 위로 찾아 올라가는 일이 없다. 붙은 op 은 이름 칸을 나누는 장치이지 상속이 아니다.

== op 을 여럿 가진 트레이트

#demo("examples/ch23/many.low")

적는 법은 셋이다.

- 서명마다 한 줄이다. 줄은 op 의 *이름*으로 시작하고, op 머리와 같은 차례로 절을 적는다 --- 입력, 출력, 효과. 다음
  이름이 나오면 다음 서명이다. 몇 개든 적는다.
- `grow` 처럼 출력이 `self` 면 갖추는 쪽은 자기 타입(`rect`)을 돌려준다.
- 갖추는 op 의 이름은 `<타입>.<이름>` 이다.

`demo_empty` 는 폭이 0 인 사각형으로 `checked_area` 를 불러 멈춘다. 서명이 `effects panic` 이라고 약속했으므로 부르는
쪽은 멈출 수 있다는 것을 안다.

#realcase[`method` 로 부른 효과가 번지지 않는 결함][
  위 예제를 `--check` 하면 `demo` 에 `W-EFFECT-OVER`(`panic` 을 선언했지만 하지 않는다)라는 경고가 붙는다. 틀린 경고다.
  `demo_empty` 는 실제로 멈춘다. 이 책을 쓰며 확인한 바로, 이 판의 처리기는 `method` 로 부른 op 의 효과를 부르는 쪽에
  번지게 하지 않는다. 그래서 순수한 `fn` 이 `method` 로 `panic` 하는 op 을 불러도 번역이 통과한다. 같은 op 을
  `rect.checked_area r` 로 직접 부르면 올바르게 거절된다. 명세(#chref("effects"))가 요구하는 동작은 직접 부름 쪽이고,
  `method` 쪽은 결함이다. 고쳐지기 전까지는 효과가 있는 붙은 op 을 순수한 op 안에서 `method` 로 부르지 않는다.
]

== 서명에는 `fn`·`proc` 을 적지 않는다

트레이트 서명에 `fn` 이나 `proc` 을 적으면 거절된다.

#demo("examples/ch23/sig_kind.low")

그 op 이 무엇을 할 수 있는지는 서명의 `effects` 줄이 정한다.

#dtable(
  columns: 2,
  id: "traits-effects",
  caption: [서명의 `effects` 와 갖추는 쪽],
  [*서명의 `effects`*], [*갖추는 쪽*],
  [없음], [`fn`, 또는 `effects` 를 적은 `proc`],
  [`effects panic` 처럼 있음], [그 효과나 그보다 적은 효과를 적은 `proc`. 효과를 안 쓰면 `fn` 도 된다],
)

`effects` 줄이 *없는* `proc` 은 좁히지 않은 것이라 무엇이든 할 수 있다고 읽힌다(#chref("ops")). 그래서 효과 없는 서명을
그런 `proc` 으로 갖추면 거절된다.

#demo("examples/ch23/proc_noeff.low")

서명보다 많은 효과를 적어도 거절된다.

#demo("examples/ch23/more_effect.low")

부르는 쪽은 *트레이트의 계약*을 보고 판단한다. 갖춘 쪽이 계약에 없는 일을 하면 그 판단이 틀린다. 이름만 맞고 효과가
다르면 트레이트는 계약을 실어 나르지 못한다.

== 갖추지 못했을 때

#dtable(
  columns: 2,
  id: "traits-diags",
  caption: [트레이트를 갖추지 못한 자리],
  [*진단*], [*뜻*],
  [`E-TRAIT-UNDEF`], [`satisfies` 가 선언되지 않은 트레이트를 부른다],
  [`E-TRAIT-MISSING`], [목록의 op 하나가 없다],
  [`E-TRAIT-SIG`], [서명에 `fn`·`proc` 을 적었거나, op 은 있는데 매개변수 수가 다르다],
  [`E-TRAIT-EFFECT`], [갖춘 op 이 서명보다 많은 효과를 가진다],
  [`E-TRAIT-RECV`], [타입이 아니라 op 에 `satisfies` 를 적었다],
)

#demo("examples/ch23/missing.low")

`satisfies` 는 주석이 아니다. 갖추겠다고 적은 순간부터 처리기가 목록 전체를 검사한다.

#qa[
  트레이트의 op 에 기본 구현을 줄 수 있는가?
][
  줄 수 없다. 트레이트는 목록일 뿐 코드를 담지 않는다. 여러 타입이 같은 구현을 나누고 싶다면, 그 구현을 제네릭 op
  (`double_area` 처럼)으로 쓰고 타입들은 그 op 이 필요로 하는 최소한만 갖춘다. 기본 구현이 있으면 어떤 타입의 `area`
  가 어디서 왔는지를 찾아 올라가야 하고, 그것이 상속이 만드는 엔트로피다.
]

== `via self` — 할당 효과만 더 적을 수 있다

할당기 트레이트처럼 구현마다 할당 효과가 다른 약속이 있다. 서명의 효과 줄에 `via self` 를 적으면, 갖추는 쪽은 할당 계열
효과(`alloc`·`heap`·`lock`·`atomic`)를 서명보다 *더* 적을 수 있다. 그 밖의 효과를 더 적는 것은 여전히 `E-TRAIT-EFFECT` 다.

```lowent
export trait byte_allocator do
  reserve input s self . input n u64 . output option mut slice u8 . effects state via self .
  grow input s self . input old mut slice u8 . input newn u64 . output option mut slice u8 . effects state via self .
  used input s self . output u64 . effects state .
end .
```

범프의 `reserve` 는 `state` 뿐이고, 힙에서 깎는 `heap_bytes` 의 `reserve` 는 `heap state` 다. 그 차이가 제네릭 op 의
`via a` 를 따라 부르는 쪽까지 올라간다(#chref("fixed-memory")).

#misconception[트레이트를 갖추면 그 트레이트의 무언가를 물려받는다][
  물려받는 것은 없다. 트레이트를 갖췄다는 것은 "그 op 들을 갖고 있다" 는 *사실*일 뿐이고, 타입 사이에 위아래가 생기지
  않는다. `rect` 와 `square` 는 `shape` 를 갖춘 뒤에도 서로 아무 관계가 없다. 이 언어에는 상속이 없다.
]

#recap[
  `fn <타입>.<이름>` 은 타입에 op 을 붙이고 `method` 가 부른다. 트레이트는 타입이 갖출 op 의 목록이고, 타입은 몸 안의
  `satisfies` 로 갖추겠다고 선언한다. 서명은 이름으로 시작해 입력·출력·효과 차례로 적고, `fn`·`proc` 은 적지 않는다.
  서명의 `effects` 가 갖추는 쪽의 효과 상한이다. 어긋나면 `E-TRAIT-*` 로 거절되고, `via self` 는 할당 계열 효과만 더
  적게 허락한다. 트레이트는 상속이 아니다.
]
