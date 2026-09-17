#import "../lib.typ": *

= 모듈 — 감춘 것이 기본이다

#chapter-toc()

#prereq(
  ([#chref("surface") 겉모습], [점이 붙은 이름은 선언된 이름을 가리키는 경로다]),
  ([#chref("capabilities") 권한], [공개된 op 머리만 보면 라이브러리가 닿는 바깥을 안다]),
)

#deepqa[
  #chref("capabilities")의 마지막 사례에서, 들여온 라이브러리가 네트워크에 닿는지 알려면 무엇을 보면 된다고 했는가?
][
  그 라이브러리가 *내보낸* op 의 머리를 훑어 `cap net`(그리고 `cap c`·`cap machine`)을 받는 op 이 있는지 보면 된다고
  했다. 이 장은 그 "내보낸" 이 무엇이고, 다른 파일의 이름을 어떻게 들여오는지를 다룬다.
]

#why[
  프로그램이 파일 하나를 넘어서면 두 가지 물음이 생긴다. *무엇이 밖에서 보이는가*, 그리고 *이 파일이 무엇에 기대는가*.
  첫 물음의 답이 흐리면 내부 구현을 고칠 때마다 누가 깨질지 모르고, 둘째 물음의 답이 흐리면 같은 소스가 기계에 따라
  다른 것을 가져온다. Lowent 는 둘 다 소스에 적게 한다. 제6부 "추상" 의 첫 장으로, 코드를 나누는 가장 바깥 단위부터
  세운다.
]

#organizer[
  파일 하나가 모듈 하나이고 `export` 를 붙인 것만 밖에서 보인다는 것을 알게 된다. `use … from "자리"` 로 다른 파일을
  들여오고, 여러 파일을 한 번역 단위로 넘기며, 표준 라이브러리 이름을 들여오는 법을 익힌다. 모듈 이름으로 한정해도
  감춘 이름에는 닿지 못한다는 것, 검색 경로가 없다는 설계, 최상위 선언의 차례가 무관하다는 것도 보게 된다.
]

#chapter-questions()

== 파일 하나, 모듈 하나

#idx("모듈")
소스 파일 하나가 모듈 하나다. 모듈은 첫 줄에 자기 이름을 적는다. 모듈 이름은 파일 이름과 같을 필요가 없다.

#demo("examples/ch21/geom.low")

`geom` 모듈은 `point` 구조체와 `manhattan` op 을 `export` 로 내보낸다. `helper` 에는 `export` 가 없다. 그래서
`helper` 는 이 모듈 안에서만 쓸 수 있다.

#idx("export")
*감춘 것이 기본*이다. 밖에서 보이는 것은 곧 약속이고, 실수로 약속하는 일보다 실수로 감추는 일이 고치기 쉽다. 네이티브로
낼 때도 `export` 한 op 만 C 에서 부를 수 있는 심볼이 되고 나머지는 모두 `static` 이다.

== 들여오기

`use <모듈> from "<자리>" .` 은 그 자리의 파일에서 모듈을 들여온다. 자리는 *선언한 파일을 기준으로 한* 경로다.

#demo("examples/ch21/app.low")

들여온 이름은 모듈 이름으로 한정해 부른다 --- `geom.point`, `geom.manhattan`. 붙은 점은 값에 대한 연산이 아니라 선언된
이름을 가리키는 경로다(#chref("surface")).

한정해도 감춘 이름에는 닿지 못한다.

#demo("examples/ch21/hidden.low")

진단의 말대로 "아무것도 감출 수 없는 모듈은 모듈이 아니라 접두사다".

#qa[
  `from "…"` 없이 `use geom .` 이라고만 적으면 어디서 찾는가?
][
  같은 번역 단위 안에서 찾는다. `lowentc --check app.low geom.low` 처럼 파일을 여럿 넘기면 그 파일들이 하나의 단위가
  된다. 그 단위에 없으면 컴파일러는 `W-USE-EXTERNAL` 로 "이 모듈이 있는지 확인할 수 없다" 고 말하고, 그 이름을 실제로
  쓰면 거절한다. 표준 라이브러리 이름(`use allocs .`)은 설치된 표준 모듈 자리에서 해소된다.
]

== 무엇을 내보낼 수 있고, 어떻게 줄여 부르나

`export` 는 이름을 가진 선언 대부분에 붙는다. 붙지 않는 것도 있다.

#dtable(
  columns: 3,
  id: "modules-export-kinds",
  caption: [`export` 를 붙일 수 있는 선언],
  [*선언*], [*`export`*], [*까닭*],
  [`fn` · `proc`], [된다], [다른 모듈이 부를 op],
  [`struct` · `enum` · `type` · `newtype`], [된다], [내보낸 op 의 입력·출력 타입도 함께 내보내야 쓸 수 있다],
  [`trait` · `actor`], [된다], [다른 모듈의 타입이 갖출 약속 · 다른 모듈이 띄울 액터],
  [최상위 `let`(상수)], [안 된다 --- `E-TOPLEVEL`], [상수는 모듈 안에 둔다. 밖에 알려야 하면 그 값을 돌려주는 `fn` 을 내보낸다],
  [`test`], [안 된다 --- `E-TOPLEVEL`], [시험은 그 모듈을 짓는 사람의 것이다],
)

모듈 이름이 길거나 두 모듈의 이름이 같으면 `as` 로 줄여 부른다.

#demo("examples/ch21/alias.low")

`use geom from "geom.low" as g .` 뒤로는 `g.point`·`g.manhattan` 으로 부른다. 별칭은 이 파일 안에서만 쓰는 이름이고, 모듈의 실제
이름(`geom`)은 바뀌지 않는다. 다만 *이 파일 안에서는* 원래 이름이 서지 않는다 --- `geom.point` 라고 적으면 `E-USE-ALIASED` 다. 별칭은
덧이름이 아니라 바꿔 부르기이고, 같은 모듈을 두 철자로 부르면 읽는 사람이 둘이 같은 것인지 확인해야 한다. 같은 이름을 두 번 들여오면
`E-NAME-COLLISION` 이 나는데, 그 진단이 권하는 해법이 이 별칭이다.

#antipattern[별칭을 적어 놓고 원래 이름으로 부른다][
  #demo("examples/ch21/mistake_aliasold.low")

  `as g` 를 적은 순간 이 파일에서 그 모듈의 이름은 `g` 하나다. `geom.point` 는 `E-USE-ALIASED` 로 거절된다. 두 철자가 다 서면 한쪽만 고친
  코드가 생기고, 읽는 사람은 둘이 같은 모듈인지 매번 확인해야 한다. 원래 이름을 적는 자리는 `use` 줄 하나뿐이다.
]

== 검색 경로가 없다

많은 언어는 이름만 적으면 도구가 어딘가의 검색 경로에서 모듈을 찾아온다. 그러면 이 프로그램이 무엇에 기대는지가 소스
밖의 지식이 되고, 같은 소스가 기계에 따라 다른 파일을 가져올 수 있다. Lowent 의 들여오기는 셋 가운데 하나다.

- 자리를 적는다: `use geom from "geom.low" .`
- 같은 번역 단위로 함께 넘긴다: `lowentc --check app.low geom.low`
- 표준 라이브러리의 예약된 이름을 쓴다: `use allocs .`

패키지 매니페스트(`pkg.low`)로 외부 의존을 적을 때도 원칙은 같다. 의존은 *내용 해시와 함께* 고정되고, 바이트가 바뀌면
빌드가 거절된다(#chref("build-test")).

#misconception[모듈 이름은 파일 이름에서 온다][
  표준 라이브러리에서 이 오해가 자주 걸린다. `use` 가 찾는 것은 파일 안의 `module` 선언이다. `lib/alloc.low` 의 모듈
  이름은 `allocs` 이고, `lib/str.low` 는 `strings`, `lib/vec.low` 는 `vecs` 다. 이름이 다른 모듈의 목록은 표준 라이브러리
  부(#chref("lib-map"))의 첫 표에 있다.
]

== 이름이 부딪히면

한 모듈 안에서 같은 이름을 두 번 선언할 수 없다.

#demo("examples/ch21/dup.low")

두 번째가 첫 번째를 가리는 것이 아니라, 둘 가운데 하나에 조용히 닿을 수 없게 된다. 그래서 거절한다. 들여온 이름끼리
부딪히면 부르는 자리에서 모듈 이름으로 밝혀 적는다. 들여오기는 이미 있는 이름을 덮어쓰지 않는다(`E-NAME-COLLISION`).

== 최상위 선언의 차례는 무관하다

모듈 안의 최상위 선언은 차례와 상관이 없다. 뒤에 선언한 op 을 앞에서 부를 수 있고, 서로 부르는 op 도 된다.

#demo("examples/ch21/order.low")

`is_even` 은 뒤에 선언된 `is_odd` 를 부르고, `is_odd` 는 다시 `is_even` 을 부른다. 반면 op *본문 안*의 지역 이름은
쓰기 전에 선언해야 한다. 파일 전체는 훑어보며 읽지만 본문은 위에서 아래로 읽기 때문이다.

== 흔한 실수

#antipattern[들여온 이름을 모듈 이름 없이 부른다][
  #demo("examples/ch21/mistake_bare.low")

  어떤 언어는 들여온 이름을 그냥 쓰게 하거나 `import *` 로 한꺼번에 풀어놓는다. 그러면 `point` 가 이 파일의 것인지 어느 모듈의 것인지
  읽어서 알 수 없고, 들여오는 모듈이 늘면 이름이 조용히 부딪힌다. Lowent 에는 풀어놓기가 없다. 다른 모듈의 이름은 언제나
  `geom.point` 처럼 한정해 적는다. 진단이 같은 줄에 두 번 나오는 것은 `let` 의 타입 자리와 `make` 자리를 따로 세기 때문이다.
]

#antipattern[`from` 의 자리에 확장자를 빠뜨린다][
  #demo("examples/ch21/mistake_noext.low")

  자리는 짐작되지 않는다. `"geom"` 이라고 적으면 도구는 정확히 `geom` 이라는 파일을 찾고, 없으면 `E-DEP-MISSING` 이다. 검색 경로가
  없는 것과 같은 까닭이다 --- 도구가 `.low` 를 붙여 보거나 다른 디렉터리를 뒤지기 시작하면 무엇에 기대는지가 다시 소스 밖으로 나간다.
  `use geom from "geom.low" .` 처럼 파일 이름을 그대로 적는다.
]

#antipattern[파일 이름으로 표준 모듈을 들여온다][
  #demo("examples/ch21/mistake_filename.low")

  `lib/str.low` 파일의 모듈 이름은 `strings` 다. `use str .` 은 그런 모듈을 찾지 못해 `W-USE-EXTERNAL` 로 "확인할 수 없다" 고 알리고,
  이름을 실제로 쓰는 자리에서 `E-IR-UNDEF` 로 거절된다. 경고를 먼저 보고 모듈 이름을 확인한다.

  #demo("examples/ch21/filename_fixed.low")
]

#antipattern[내보낸 op 의 서명에 감춘 타입을 쓴다][
  #demo("examples/ch21/secretbox.low")

  `secretbox` 자체는 번역을 통과하지만 내보내는 쪽에서 `W-EXPORT-HIDDEN` 으로 알린다 --- 이 export 는 밖에서 쓸 수 없다는 뜻이다.
  그 알림이 없다면 문제는 들여오는 쪽에서야 드러난다.

  #demo("examples/ch21/mistake_privtype.low")

  `make_secret` 은 내보냈지만 그 결과의 타입 `secret` 은 감췄다. 들여온 쪽은 결과를 담을 이름을 적을 수 없어 `E-VISIBILITY` 로
  거절된다. 쓸 수 없는 export 인 셈이다. 내보낸 op 의 입력·출력 타입도 함께 `export` 한다.
]

#antipattern[다른 모듈의 열거형 갈래를 `case` 에 모듈 이름과 함께 적는다][
  #demo("examples/ch21/sizes.low")

  `sizes` 모듈은 열거형 `kind` 와 그것을 돌려주는 `classify` 를 내보낸다. 들여온 쪽에서 갈래를 모듈 이름으로 한정해 적으면 이렇게 된다.

  #demo("examples/ch21/mistake_enumcase.low")

  `describe 500` 은 `big` 이라 2 를, `describe 5` 는 1 을 돌려준다. 한정해 적어도 갈래로 읽히기 때문이다. 2026-09-16 까지는 그렇지
  않았다 --- 하강이 `case sizes.small` 을 갈래가 아니라 *아무 값에나 맞는 자리*로 읽어 첫 갈래가 모든 값을 가져갔고, 500 도 1 을
  냈다. 검사기는 한정 이름을 갈래로 좁히는데 하강은 그러지 않아 생긴 어긋남이었고, 이제 두 층이 같은 나무를 본다. 그래도 `case` 에는
  갈래 이름만 적는 편이 짧고, 어느 열거형의 갈래인지는 `k` 의 타입이 정한다.

  #demo("examples/ch21/enumcase_fixed.low")
]

#misconception[두 모듈이 서로를 들여오면 안 된다][
  #demo("examples/ch21/cycle_a.low")

  `cycle_a` 는 `cycle_b` 를 들여오고 `cycle_b` 도 `cycle_a` 를 들여오지만 번역되고 돈다. C 의 헤더처럼 "먼저 읽은 것만 안다" 가 아니라,
  번역 단위 전체를 모은 뒤 이름을 해소하기 때문이다. 최상위 선언의 차례가 무관한 것과 같은 원리다. 그래도 서로 들여오는 모듈은
  하나로 합치거나 공통 부분을 셋째 모듈로 빼는 편이 읽기 쉽다. 되부름의 깊이는 계약(`requires le n 10 .`)이 막는다.
]

== 이 장의 문법 한눈에

#dtable(
  columns: 3,
  id: "modules-glance",
  caption: [모듈의 문법 --- 모양 · 뜻 · 왜 이렇게 생겼나],
  [*모양*], [*뜻*], [*왜 이렇게*],
  [`module geom .`], [이 파일이 모듈 `geom` 이다(첫 줄)], [모듈 이름은 파일 이름과 따로 --- `use` 는 이 이름을 찾는다],
  [`export fn manhattan …` · `export struct point …`], [밖에서 보이게 한다], [감춘 것이 기본 --- 보이는 것은 약속],
  [`use geom from "geom.low" .`], [선언한 파일 기준의 자리에서 들여온다], [검색 경로가 없다 --- 무엇에 기대는지 소스에 보인다],
  [`use allocs .`], [같은 번역 단위나 표준 라이브러리에서 들여온다], [없으면 `W-USE-EXTERNAL`],
  [`geom.point` · `geom.manhattan a b`], [들여온 이름은 모듈 이름으로 한정한다], [풀어놓기가 없다],
  [`geom.helper`(감춘 이름)], [거절(`E-VISIBILITY`)], [한정해도 문이 열리지 않는다],
  [같은 이름을 두 번 선언 · 두 들여오기가 같은 이름], [거절(`E-NAME-DUP` · `E-NAME-COLLISION`)], [어느 쪽에 닿는지 조용히 정하지 않는다],
  [최상위 선언의 차례], [무관 --- 서로 불러도 된다], [파일은 훑어 읽고 본문은 위에서 아래로 읽는다],
  [`use geom from "geom.low" as g .`], [들여온 모듈을 이 파일에서 `g` 로 부른다], [긴 이름 · 같은 이름을 풀어 준다 --- 모듈의 실제 이름은 그대로],
  [`export let …` · `export test …`], [거절(`E-TOPLEVEL`)], [상수는 값을 돌려주는 `fn` 으로 내보낸다],
  [다른 모듈 열거형의 `case small`], [갈래 이름만 적는다], [`case sizes.small` 은 이 판에서 아무 값에나 맞는다],
)

#recap[
  파일 하나가 모듈 하나이고, `export` 한 것만 밖에서 보인다. `use <모듈> from "<자리>" .` 는 선언한 파일 기준의 자리에서
  들여오고, 자리를 적지 않으면 같은 번역 단위나 표준 라이브러리에서 찾는다. 검색 경로는 없다. 한정해도 감춘 이름에는
  닿지 못하고, 같은 이름의 중복 선언은 거절된다. 최상위 선언의 차례는 무관하지만 본문의 지역은 먼저 선언한다.
]
