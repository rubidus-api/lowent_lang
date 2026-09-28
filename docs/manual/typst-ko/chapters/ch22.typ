#import "../lib.typ": *

= 제네릭 — 번역 시점에 정해지는 매개변수

#chapter-toc()

#prereq(
  ([#chref("expr") 식], [`size_of` 와 `comptime` 은 번역할 때 계산된다]),
  ([#chref("fixed-memory") 할당기와 고정 메모리], [할당기를 `input comptime a type .` 으로 받아 갈아 끼운다]),
)

#deepqa[
  #chref("fixed-memory")의 `two_from` 은 `bump_bytes` 와 `bump_aligned` 를 모두 받았다. 할당기를 갈아 끼우는 비용이 왜
  0 이라고 했는가?
][
  할당기의 *타입*이 번역할 때 구체 타입으로 정해져서, 호출마다 그 타입 전용 코드가 만들어지기 때문이다. 가상 함수 표도
  간접 호출도 없다. 이 장은 그 "번역할 때 정해지는 매개변수" --- `comptime` --- 를 일반적으로 다룬다.
]

#why[
  같은 알고리즘을 타입마다 베껴 쓰면 고칠 자리가 타입 수만큼 늘어난다. 함수 포인터로 비교를 넘기면 호출 자리에서는
  공짜처럼 보이지만 간접 호출의 비용이 숨는다. Lowent 는 타입과 상수를 *번역 시점 매개변수*로 받고, 쓰인 조합마다
  실물을 만든다. 무엇이 만들어지는지는 부르는 자리에 보인다. 트레이트(#chref("traits"))와 `pipe`(#chref("pipe"))가
  모두 이 위에 서므로, 추상의 도구로 먼저 세운다.
]

#organizer[
  `input comptime n u8 .` 처럼 값을, `input comptime t type .` 처럼 타입을 번역 시점에 받는 법을 익힌다. 번역 시점에
  알 수 없는 값을 주면 거절된다는 것, 쓰인 조합마다 실물이 만들어지고(단형화) 그 수가 곧 코드의 양이라는 것을 알게
  된다. 타입 매개변수에 `requires <트레이트> t .` 로 조건을 걸고, 조건을 갖추지 못한 타입을 주면 무엇이 일어나는지도
  보게 된다.
]

#chapter-questions()

== 값과 타입을 번역 시점에 받는다

#idx("comptime")
`comptime` 을 붙인 매개변수는 번역 시점에 값이 정해져야 한다. 타입도 그렇게 받는다.

#demo("examples/ch22/sizes.low")

- `bytes_for` 의 `input comptime t type .` 은 타입을 받는다. 본문의 `size_of t` 는 그 타입의 크기를 번역할 때 준다.
  `bytes_for u8 100` 은 100, `bytes_for u64 100` 은 800 이다.
- `add_const` 의 `input comptime n u8 .` 은 값을 받는다. `add_const 7 10` 은 17 이다.
- 부르는 자리는 타입이나 상수를 *보통 인자처럼* 앞자리에 적는다. 꺾쇠(`<T>`)도, 추론도 없다.

comptime 매개변수는 머리에서 가장 앞(권한 입력보다도 앞)에 온다(#chref("surface")). 뒤따르는 입력과 출력의 타입이 그
이름을 쓰기 때문이다.

번역 시점에 알 수 없는 값을 주면 거절된다.

#demo("examples/ch22/rt_arg.low")

`k` 는 실행할 때에야 정해진다. 그 자리에 `comptime` 이라고 적힌 이상, 실행 값을 받으면 그 낱말이 거짓말이 된다.
comptime 자리에 올 수 있는 것은 정수 리터럴과 모듈 수준의 `let` 상수다.

#qa[
  타입 인자를 추론하지 않는 이유는 무엇인가? `max_of a b` 라고만 써도 `a` 의 타입에서 알 수 있지 않은가?
][
  알 수 있다. 그러나 추론하면 *무엇이 만들어지는지*가 부르는 자리에서 보이지 않는다. 같은 줄이 인자의 타입에 따라 다른
  실물을 부르고, 그 수는 소스를 따라가야 센다. Lowent 는 쓰인 조합의 수가 곧 만들어지는 코드의 양이라는 비용을 부르는
  자리에서 셀 수 있게 두는 쪽을 골랐다.
]

== 조합마다 실물이 만들어진다

#idx("단형화")
처리기는 쓰인 조합마다 그 조합 전용 op 을 만든다. 이것을 *단형화*(monomorphisation)라 한다. `bytes_for u8` 과
`bytes_for u64` 는 네이티브 코드에서 서로 다른 두 함수다. 타입이 확정된 뒤 만들어지므로 크기와 연산이 모두 상수로
박히고, 호출은 직접 호출이다.

```text
 소스 (틀 하나)                              번역 뒤 (쓰인 조합마다 한 벌)
 fn bytes_for input comptime t type …   ┌─▶ bytes_for#u8  :  return mul 1 items
     return mul (size_of t) items        │
                                         └─▶ bytes_for#u64 :  return mul 8 items
 부르는 자리:
   bytes_for u8 100   ────────────────────▶ bytes_for#u8 을 곧바로 부른다
   bytes_for u64 100  ────────────────────▶ bytes_for#u64 를 곧바로 부른다
```

틀에 적힌 `size_of t` 가 각 벌에서는 1 과 8 이라는 상수가 된다. 실행 중에 «t 가 무엇인가» 를 묻는 일이 남지 않는다.

대가는 코드의 양이다. 열 가지 타입으로 부르면 열 벌이 생긴다. 이 비용은 숨지 않는다. 부르는 자리에 타입이 적혀 있으니
몇 벌이 생길지를 소스에서 센다.

== 타입에 조건을 건다

타입을 받는 op 은 대개 그 타입이 무언가를 할 줄 알아야 한다. 최댓값을 고르려면 비교할 줄 알아야 한다. 그 조건을
`requires <트레이트> <타입> .` 으로 건다.

#demo("examples/ch22/bound.low")

- `trait ordered` 는 "`less` 라는 op 을 갖춘다" 는 약속이다. `score` 는 `satisfies ordered .` 로 그 약속을 갖추겠다고
  적고, `score.less` 로 실제로 갖춘다(#chref("traits")).
- `max_of` 의 `requires ordered t .` 가 타입 조건이다. 본문은 그 조건을 믿고 `method a less b` 를 부른다.
- `bigger` 는 `max_of score …` 로 부른다. 처리기는 `max_of` 의 `score` 전용 실물을 만든다. 방출된 C 에서 그 함수의
  이름에 `max_of_score` 가 들어 있는 것을 볼 수 있다.

조건을 갖추지 못한 타입으로 부르면 거절된다.

#demo("examples/ch22/unsat.low")

타입 조건은 약속이고, 부르는 쪽이 그것을 넘어 실물을 만들게 하면 받는 쪽의 계약이 거짓이 된다. 진단은 그 타입에
`satisfies` 와 필요한 op 을 더하라고 한다.

#misconception[제네릭 코드는 느리다][
  언어에 따라 다르다. 타입을 지우고 모든 값을 상자에 담아 간접 호출로 다루는 방식은 느리다. 단형화하는 방식은 손으로
  타입마다 베껴 쓴 코드와 같은 기계어가 된다. Lowent 는 단형화한다. 대가는 속도가 아니라 코드의 양이고, 그 양은 부르는
  자리에서 셀 수 있다.
]

== 값을 넘기지 않고 타입이 들고 온다

C 의 `qsort` 는 비교 함수를 *값*으로 받는다. Lowent 에는 일급 함수가 없고, 있더라도 함수 포인터를 거치는 간접 호출이
숨는다. 대신 *비교를 타입이 들고 온다*. 표준 라이브러리 `sortgen` 의 머리가 그 모양이다.

```lowent
export trait ordered do
  less input a self . input b self . output bool . effects none .
end

export proc sort_by input comptime t type . input s mut slice t .
  output void .
  effects none .
  requires ordered t .
```

정렬 기준을 바꾸려면 다른 `less` 를 가진 타입을 쓴다. 필드 하나짜리 구조체로 감싸도 배치는 그대로라서 비용이 없다.
내림차순이나 여러 키도 `less` 를 그렇게 쓰면 된다. 모드 인자를 다는 대신 타입이 뜻을 들고 온다(#chref("lib-containers")).

#realcase[할당 효과까지 타입이 정한다][
  #chref("effects")의 `via` 는 제네릭이 효과에 닿는 자리다. `vecgen.append` 는 `effects state via a .` 로 적혀 있어서,
  빌린 바이트 위의 범프로 단형화하면 `state` 만, 힙에서 깎는 `heap_bytes` 로 단형화하면 `heap state` 가 그 인스턴스의
  서명에 선다. 타입 인자가 알고리즘뿐 아니라 *효과*까지 정한다. 한 소스로 운영체제 없는 기계와 서버를 함께 다루는 방법이다.
]

== 흔한 실수

#antipattern[타입 인자를 빠뜨리고 추론되기를 기대한다][
  #demo("examples/ch22/mistake_notypearg.low")

  `max_of` 의 첫 입력은 타입이다. 인자의 타입에서 `score` 를 알아낼 수 있어 보여도 Lowent 는 추론하지 않는다 --- 무엇이 만들어지는지가
  부르는 자리에 보여야 하기 때문이다. 앞자리를 비우면 `E-MONO-NOTYPE` 으로, 타입을 앞에 적으라고 말한다. 전에는 같은 자리가
  "`max_of` 라는 이름이 없다"(`E-IR-UNDEF`)였다 --- op 은 분명히 있으므로 틀린 안내였다.
]

#antipattern[본문이 쓰는 행동을 타입 조건으로 적지 않는다][
  #demo("examples/ch22/mistake_nobound.low")

  `max_of` 의 본문은 `less` 를 부르지만 머리에 `requires ordered t .` 가 없다. 그러면 `plain` 으로 부른 잘못이 *템플릿 안*의 줄에서
  `E-METHOD-UNDEF` 로 나고, 도구는 `N-MONO-SITE` 로 "그 인스턴스를 청한 줄은 여기" 라고 덧붙인다. 조건을 적은 `unsat.low` 는 같은
  잘못을 부르는 자리에서 `E-BOUND-UNSAT` 으로 곧바로 말한다. 타입 조건은 부르는 쪽에게 주는 약속이면서 진단을 제자리로 데려오는 표시다.
]

#antipattern[타입 자리에 크기를 넘긴다][
  #demo("examples/ch22/mistake_valuetype.low")

  `u64` 가 8 바이트이니 8 을 넘기면 될 것 같지만, `input comptime t type .` 이 받는 것은 *타입*이다. 이 판의 도구는 수 8 을 타입으로
  읽으려다 템플릿 안의 `size_of t` 에서 `E-IR-UNDEF` 를 낸다. `bytes_for u64 100` 처럼 타입 이름을 적는다. 크기가 필요한 것은 템플릿의
  일이고, 부르는 쪽은 무엇의 크기인지를 말한다.
]

#antipattern[`comptime` 입력을 자료 입력 뒤에 둔다][
  #demo("examples/ch22/mistake_comptimeorder.low")

  머리의 차례는 하나로 정해져 있다. `comptime` 입력은 권한 입력보다도 앞, 가장 앞이다. 뒤따르는 입력과 출력의 타입이 그 이름을 쓸 수
  있어야 하고, 부르는 자리에서도 "무엇을 만들지" 가 먼저 읽혀야 하기 때문이다. `E-CLAUSE-ORDER` 의 진단은 머리 차례 전체를 한 줄로
  보여 준다.
]

#misconception[값이 늘 같은 지역 `let` 은 번역 시점 상수다][
  #demo("examples/ch22/mistake_localconst.low")

  `k` 는 언제나 7 이지만, op 안의 `let` 은 op 이 불릴 때 생기는 이름이다. `comptime` 자리가 받는 것은 정수 리터럴과 *모듈 수준의* `let`
  뿐이다. 식을 따라가 "결국 상수" 인지 판정하기 시작하면, 어느 식이 번역 시점에 풀리는지가 도구의 영리함에 달린다. 상수로 쓸 값은 모듈
  수준에 이름을 붙인다.

  #demo("examples/ch22/module_const.low")
]

== 이 장의 문법을 한눈에

#dtable(
  columns: 3,
  id: "generics-glance",
  caption: [제네릭의 문법 --- 모양 · 뜻 · 왜 이렇게 생겼나],
  [*모양*], [*뜻*], [*왜 이렇게*],
  [`input comptime t type .`], [타입을 번역 시점에 받는다(머리 맨 앞)], [뒤따르는 타입이 그 이름을 쓴다],
  [`input comptime n u8 .`], [값을 번역 시점에 받는다], [상수로 접히고 검사가 지워진다],
  [`bytes_for u64 100` · `add_const 7 10`], [타입과 상수를 앞자리 인자로 적는다], [꺾쇠도 추론도 없다 --- 무엇이 만들어지는지 보인다],
  [`let step u8 be 7 .`(모듈 수준)], [`comptime` 자리에 올 수 있는 이름 붙은 상수], [op 안의 `let` 은 실행 중의 이름],
  [`size_of t`], [번역 시점에 타입의 크기], [크기가 상수로 박힌다],
  [`requires ordered t .`], [타입 조건 --- 트레이트를 갖추어야 한다], [못 갖추면 부르는 자리에서 `E-BOUND-UNSAT`],
  [`method a less b`], [조건이 약속한 op 을 부른다], [직접 호출로 단형화된다],
  [쓰인 조합마다 실물 한 벌], [단형화], [비용은 속도가 아니라 코드의 양 --- 소스에서 센다],
)

#recap[
  `comptime` 매개변수는 번역 시점에 값이 정해지고, 타입도 `input comptime t type .` 으로 받는다. 부르는 자리는 타입과
  상수를 앞자리 인자로 적으며 추론은 없다. 실행 값을 comptime 자리에 주면 거절된다. 쓰인 조합마다 실물이 만들어져
  직접 호출이 되고, 그 수가 곧 코드의 양이다. `requires <트레이트> t .` 는 타입 조건이고, 갖추지 못한 타입은 거절된다.
  비교 같은 행동은 값이 아니라 타입이 들고 온다.
]
