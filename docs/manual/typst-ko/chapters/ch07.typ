#import "../lib.typ": *

= 흐름 — 갈래, 되풀이, 빠져나가기

#chapter-toc()

#prereq(
  ([#chref("numbers") 수], [참거짓은 수가 아니다]),
  ([#chref("locals") 지역], [`if` 는 값을 내지 않는 문장이다]),
)

#deepqa[
  #chref("locals")에서 `let x u64 be if gt a 1 . 5 else 6 .` 은 왜 거절되었고, 갈래마다 값을 정하려면
  어떻게 적는가?
][
  `if` 는 값을 내지 않는 문장이라서 식의 자리에 둘 수 없다(`E-IF-VALUE`). 갈래마다 값을 정하려면 `var`
  를 기본값으로 짓고 갈래 안에서 `set` 하거나, 갈래마다 `return` 한다. 이 장은 그 갈래 자체와, 되풀이와
  빠져나가기의 규칙을 다룬다.
]

#why[
  흐름의 문장은 어느 언어에나 있어서 새로 배울 것이 없어 보인다. 그러나 Lowent 의 흐름 문장에는 *검사*가
  붙어 있다. 조건은 참거짓이어야 하고, `guard` 의 `else` 는 반드시 떠나야 하며, 값을 돌려주는 op 은
  모든 길에서 돌려주어야 하고, `match` 는 모든 경우를 덮어야 한다. 이 검사들은 "어떤 길로 가면 값이 없다"
  는 결함을 번역할 때 없앤다. 데이터(제3부)로 넘어가기 전에 흐름의 약속을 먼저 세운다.
]

#organizer[
  `if … else`, `while`, `for`, `break`·`continue` 를 쓰는 법을 익힌다. `guard` 가 조건을 *그 아래 코드에
  대한 사실*로 바꾸는 방식과, `else` 가 떠나지 않으면 왜 거절되는지 알게 된다. 모든 길이 값을 돌려주어야
  한다는 규칙, 수와 범위를 가르는 `match` 와 그 망라 검사, 그리고 `panic` 이 효과라는 것도 보게 된다.
]

#chapter-questions()

== 조건과 되풀이

`if` 는 조건이 참일 때 블록을 실행하고, `else` 로 거짓일 때의 블록을 적는다. `while` 은 조건이 참인
동안 되풀이하고, `for` 는 슬라이스의 원소를 차례로 훑는다. 조건은 모두 `bool` 이어야 한다.

#demo("examples/ch07/loops.low")

- `count_big` 은 0 부터 `n` 미만까지 세면서 5 보다 큰 수만 센다. `while lt i n . do … end .` 의 모양을
  보면, 조건도 폼이므로 마침표로 닫고 그 뒤에 몸을 여는 `do` 가 온다.
- `first_zero` 는 0 을 만나면 `break` 로 되풀이를 벗어난다. 끝까지 없으면 길이를 돌려준다.
- `odd_sum` 은 `for x xs do … end` 로 원소를 훑고, 짝수면 `continue` 로 다음 원소로 넘어간다. `for`
  의 이름 `x` 는 슬라이스의 원소 타입(`u8`)이고 블록 안에서만 산다.

`for x in xs` 처럼 `in` 을 적으면 `E-VOCAB-REMOVED` 로 거절된다. 훑을 대상은 이름 바로 뒤에 온다.

#qa[
  `for` 로 0 부터 `n` 까지 세는 반복은 어떻게 적는가?
][
  `for` 는 슬라이스를 훑는 도구라서 수의 범위를 직접 받지 않는다. 수를 세는 반복은 `while` 과 `var`
  로 적는다. 세는 반복을 한 가지 모양으로만 적게 하면, 반복 변수의 범위를 컴파일러가 알아보기 쉽고
  경계 검사를 지우는 분석도 단순해진다. 슬라이스를 걸러 세거나 모으는 일은 `pipe`(#chref("pipe"))가
  맡는다.
]

== `guard` — 조건을 사실로 바꾼다

#idx("guard")
`guard <조건> . else <빠져나감> .` 은 조건이 참이 아니면 *그 자리에서 떠난다.* 떠나는 문장은
`return`·`break`·`continue`·`panic` 이다.

#demo("examples/ch07/guards.low")

`head_or_zero` 의 `guard` 를 지난 코드는 슬라이스가 비지 않은 세계에서만 산다. 그래서 `index data 0`
이 안전하다. `grade` 는 `guard` 로 범위 밖을 먼저 걸러 내고, 그 아래에서 `if … end else do … end` 로
갈래를 나눈다.

`guard` 가 `if not` 의 다른 이름이 아닌 까닭은 `else` 가 반드시 떠나야 한다는 데 있다. 떠나지 않으면
거절된다.

#demo("examples/ch07/fallthrough.low")

`else` 가 떠나지 않고 아래로 흘러내리면, `guard` 아래 코드는 `n` 이 5 이하라고 믿지만 실제로는 그렇지
않다. 믿을 수 없는 사실을 믿게 두느니 번역을 거절한다. 떠나지 않을 일이면 `if` 를 쓴다.

#misconception[`guard` 는 코드를 짧게 하는 문법 설탕이다][
  짧아지는 것은 부수 효과다. `guard` 의 본뜻은 *컴파일러와 읽는 사람에게 사실 하나를 건네는 것*이다.
  컴파일러는 `guard` 를 지난 뒤 그 조건을 참으로 알고 경계 검사를 지우는 데 쓴다. 같은 뜻을
  `if … do return … end` 로 적어도 동작은 같지만, 떠남이 문법으로 보장되지는 않는다.
]

== 모든 길이 값을 돌려준다

값을 돌려주는 op 은 *모든 길*에서 값을 돌려주고 끝나야 한다.

#demo("examples/ch07/partial.low")

`a` 가 0 이하인 길에는 `return` 이 없다. 옛 도구는 그 길에서 조용히 0 을 돌려주었다. 소스 어디에도 없는
값이다. 지금은 거절된다. 값을 돌려주고 끝나는 것으로 인정되는 문장은 `return`, 두 갈래가 모두 돌려주는
`if … else`, 모든 갈래가 돌려주는 `match` 다. `while`·`for`·`guard` 는 빠져나가는 길이 있으므로 그 자체로
돌려주는 것으로 보지 않는다. 그래서 반복 뒤에는 늘 `return` 이 온다.

돌려줄 값이 없는 op(`output void`)은 `return` 없이 몸의 끝까지 가도 된다. 끝나는 자리가 곧 돌아가는
자리다.

== `match` — 경우를 빠짐없이

#idx("match")
`match` 는 값을 경우별로 가른다. `if` 사슬과 다른 점은 *모든 경우를 다뤄야* 한다는 것이다.

#demo("examples/ch07/bands.low")

- `band` 는 `lo to hi` 범위(양 끝 포함)로 가른다. 정수는 경우가 많으므로 나머지를 덮는 `case _ .` 가 필요하다.
- `half` 는 두 범위가 `u8` 의 0 … 255 를 빈틈 없이 덮으므로 `_` 가 없어도 된다.
- `big` 의 `case y when gt y 100 .` 은 값 전체를 `y` 에 묶고 가드로 더 좁힌다. 가드가 붙은 갈래는
  거짓일 수 있으므로 망라에 세지 않는다. 그래서 `_` 가 여전히 필요하다.

빈틈이 있으면 거절된다.

#demo("examples/ch07/inexhaustive.low")

10 이 어느 갈래에도 들지 않는다. 같은 경우를 두 번 적거나 `_` 뒤에 갈래를 두면 `E-MATCH-REDUNDANT` 로
거절된다. 경고가 아니라 오류다 --- 한 번도 실행될 수 없는 갈래는 대개 결함을 숨긴다.

#qa[
  `match` 가 `if` 사슬보다 나은 점은 망라 검사 말고 무엇인가?
][
  나중에 경우가 늘어날 때 드러난다. 열거형에 변형을 하나 더하면, 그 열거형을 가르는 모든 `match` 가
  번역에서 멈춰 고쳐야 할 자리를 알려 준다(#chref("structs-enums")). `if` 사슬은 새 경우를 조용히 마지막
  `else` 로 흘려보낸다. 또 `--ir` 로 보면 도구가 갈래 수와 최악 비교 횟수 같은 디스패치 비용을 말해 준다.
]

== `panic` 은 효과다

#idx("panic")
`panic` 은 프로그램을 즉시 멈춘다. 되돌릴 수 없는 멈춤이므로 효과이고, 쓰는 op 은 `effects panic` 을
적는 `proc` 이어야 한다.

#demo("examples/ch07/panics.low")

VM 은 `E-VM-PANIC` 을 내며, 이것이 *계약 위반이 아니라는* 것을 분명히 한다 --- 코드가 멈추기로 고른
것이지 약속을 어긴 것이 아니다. 순수한 `fn` 은 `panic` 을 쓸 수 없다. 다만 넘침이나 계약 위반으로
처리기가 일으키는 멈춤은 `fn` 에서도 일어난다. 그것은 op 이 한 일이 아니라 처리기가 약속을 지키게 한
일이다.

`panic` 은 복구할 수 없는 상황에만 쓴다. 호출자가 다룰 수 있는 실패는 값으로 돌려준다
(#chref("option-result")). 둘을 가르는 기준은 #chref("errors-design")이 다룬다.

== 흔한 실수

#antipattern[`for x in xs` 로 적는다][
  #demo("examples/ch07/mistake_forin.low")

  `for` 는 `for <이름> <슬라이스> do` 다. 훑을 대상이 이름 바로 뒤에 오고, 몸의 시작은 `do` 가 알리므로 `in` 이 전할 것이 없다.
  같은 뜻에 표시를 하나 더 두지 않으려고 없앴다. 진단의 긴 설명은 붙임 점(`a.b`)을 대안으로 들지만 그것 또한 지금은 거절되는
  표기다 --- 이 판의 진단문이 낡은 자리다. 고치는 법: `for x xs do`.
]

#antipattern[`elif` 로 갈래를 잇는다][
  #demo("examples/ch07/mistake_elif.low")

  `elif`·`elsif`·`else if:` 는 언어마다 다르다. Lowent 는 이미 있는 낱말을 이어 붙인다 --- 앞 블록을 `end` 로 닫고 `else if` 를
  붙인다. `elif` 는 낱말이 아니므로 모르는 이름(`E-IR-UNDEF`)으로 읽힌다.

  #demo("examples/ch07/elseif.low")

  마지막 갈래는 `end else do … end .` 이다. 갈래가 셋 이상이고 모두 한 값을 가르는 것이면 `match` 가 더 알맞다.
]

#antipattern[C 처럼 블록 안에 `else` 를 둔다][
  #demo("examples/ch07/mistake_innerelse.low")

  C 나 몇몇 언어는 `if … { … } else { … }` 처럼 `else` 가 앞 블록 뒤에 붙는다. Lowent 에서 `else` 는 앞 블록을 `end` 로 닫은 *뒤에*
  온다(`end else do`). 블록 안에 `else` 를 한 줄로 두면 이 판의 도구는 번역을 거절하지 않고 "낮출 수 없는 op 이 하나 있다" 는 알림만
  붙인 채 `check: ok` 를 낸다. 실행하면 `E-VM-UNSUP` 으로 멈추고, 네이티브 빌드에서는 그 op 이 아예 빠진다(개발 저장소에 결함으로 적어
  두었다). `check: ok` 위에 붙은 알림(note)도 읽는다.
]

#antipattern[조건에 `<` 같은 기호를 쓴다][
  #demo("examples/ch07/mistake_less.low")

  비교는 낱말이다 --- `lt`(작다)·`le`(작거나 같다)·`gt`·`ge`·`eq`·`ne`. `<` 는 이 언어가 모르는 글자라 `E-CHAR` 가 난다.
  기호의 우선순위를 외우지 않아도 되게 하려는 선택이고, 긴 산술에는 `expr` 섬이 있다(#chref("expr")). 고치는 법: `while lt i n . do`.
]

#antipattern[`continue` 가 증가를 건너뛴다][
  `while` 로 세면서 몸 가운데서 `continue` 하면, 그 아래에 둔 `set i (add i 1) .` 도 함께 건너뛴다.

  ```lowent
  fn odd_count input xs slice u8 . output u64 .
  do
    var n u64 be 0 .
    var i u64 be 0 .
    while lt i (len xs) . do
      if eq (mod (index xs i) 2) 0 . do
        continue .
      end .
      set n (add n 1) .
      set i (add i 1) .
    end .
    return n .
  end .
  ```

  첫 짝수에서 `i` 가 더 이상 오르지 않아 반복이 끝나지 않는다. 번역도 실행 검사도 이것을 잡지 못한다 --- 멈추지 않는 것은
  넘침이 아니기 때문이다. 증가를 몸의 *맨 앞*으로 옮기거나(그러면 색인에는 증가 전의 값을 따로 담는다), 원소를 훑는 일이면
  처음부터 `for x xs do` 를 쓴다. `for` 는 다음 원소로 넘어가는 일을 언어가 맡으므로 이 결함이 생길 자리가 없다.
]

#misconception[`match` 의 갈래도 C 의 `switch` 처럼 아래로 흘러간다][
  #demo("examples/ch07/nofall.low")

  맞은 갈래 *하나만* 실행되고 다음 갈래로 흘러가지 않는다. `break` 를 적는 버릇도, 빠뜨려서 생기는 결함도 없다. 두 경우에 같은
  일을 하려면 갈래를 `or` 로 묶는다(#chref("option-result")).
]

== 이 장의 문법 한눈에

#dtable(
  columns: 3,
  id: "control-glance",
  caption: [흐름의 문법 --- 모양 · 뜻 · 왜 이렇게 생겼나],
  [*모양*], [*뜻*], [*왜 이렇게*],
  [`if c . do … end .`], [조건이 참이면 블록], [조건도 폼이라 마침표로 닫고, 몸은 `do` 로 연다],
  [`if c . do … end else do … end .`], [둘 중 하나], [여는 말과 닫는 말이 늘 짝을 이룬다],
  [`… end else if c2 . do … end .`], [갈래 잇기], [새 낱말(`elif`) 없이 있는 낱말을 잇는다],
  [`while c . do … end .`], [조건이 참인 동안], [수를 세는 반복은 이 모양 하나],
  [`for x xs do … end .`], [슬라이스의 원소를 차례로], [다음 원소로 넘어가는 일을 언어가 맡는다],
  [`break .` · `continue .`], [반복에서 나가기 · 다음 회차로], [흐름을 바꾸는 문장],
  [`guard c . else return … .`], [조건이 아니면 떠난다 --- 지난 뒤엔 조건이 사실], [`else` 가 반드시 떠나야 한다],
  [`return e .`], [값을 돌려주고 끝낸다], [값을 내는 op 은 모든 길에서],
  [`match v do case … . 문장 … end .`], [경우별로 가르기], [빠짐없이, 겹침 없이 --- 흘러내림이 없다],
  [`case 1 to 9 .` · `case _ .` · `case y when c .`], [범위 · 나머지 전부 · 가드가 붙은 갈래], [정수는 경우가 많아 `_` 가 흔히 필요하다],
  [`panic "…" .`], [되돌릴 수 없는 멈춤(효과)], [`effects panic` 을 적는 `proc` 에서만],
)

#recap[
  조건은 `bool` 이어야 한다. `while` 은 조건으로, `for` 는 슬라이스로 되풀이하고 `break`·`continue` 로
  흐름을 바꾼다. `guard` 의 `else` 는 반드시 떠나고, 지난 뒤에는 조건이 사실이 된다. 값을 돌려주는 op
  은 모든 길에서 돌려주어야 한다. `match` 는 빠짐없이, 겹침 없이 경우를 덮어야 한다. `panic` 은 효과다.
]
