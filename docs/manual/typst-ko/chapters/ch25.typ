#import "../lib.typ": *

= 액터 — 상태를 가진 채 메시지로 사는 것

#chapter-toc()

#prereq(
  ([#chref("effects") 효과], [`state`·`panic` 은 권한 없이 적는 효과다]),
  ([#chref("ownership") 소유], [소유를 넘기면 보낸 쪽은 더 쓸 수 없다]),
  ([#chref("fixed-memory") 할당기와 고정 메모리], [할당기는 트레이트를 갖춘 액터다]),
)

#deepqa[
  #chref("fixed-memory")의 범프 할당기는 무엇으로 만들고 무엇으로 불렀는가? 할당기의 커서는 어디에 살았는가?
][
  `spawn actor allocs.bump_bytes` 로 만들고 `send a reserve 3` 으로 불렀다. 커서는 액터의 상태에 살았고, 바깥에서는
  메시지로만 그 상태에 닿았다. 이 장은 그 액터를 제대로 다룬다.
]

#why[
  제7부는 여러 일이 함께 진행되는 자리를 다룬다. 동시성 결함의 대부분은 *여러 흐름이 같은 상태를 함께 만지는* 데서
  나온다. 자물쇠로 막을 수 있지만 자물쇠를 잊거나 차례를 틀리면 조용히 틀린다. 액터는 반대쪽에서 접근한다. 상태를 액터
  안에만 두고, 메시지를 한 번에 하나씩 처리하게 한다. 그러면 그 상태를 동시에 만질 방법이 처음부터 없다. 제7부가 액터로
  시작하는 것은, 표준 라이브러리의 할당기처럼 이 모양이 이미 이 언어의 곳곳에 쓰이기 때문이다.
]

#organizer[
  `actor … do state do … end … end` 로 액터를 선언하고, `spawn actor` 로 만들고, `send` 로 메시지를 보내는 법을 익힌다.
  액터 안의 op 도 `fn`·`proc` 규칙을 따른다는 것, 메시지에 값을 싣고 소유를 넘기는 법을 알게 된다. 우편함에 넣고
  나중에 비우는 `spawn send`·`drain`, 터진 액터를 다시 세우는 `failure restart`, 그리고 액터를 쓸 수 있는 자리를 정하는
  `build profile` 도 보게 된다.
]

#chapter-questions()

== 선언하고, 만들고, 말을 건다

#demo("examples/ch25/counter.low")

#idx("액터")
- `actor counter do … end` 가 액터를 선언한다. `state do value u64 . end` 가 액터의 상태다. 상태는 액터 안에만 있고
  바깥에서 직접 건드릴 수 없다.
- 상태를 고치는 `inc` 는 `proc` 이고 `effects state` 다. 읽기만 하는 `get` 은 `fn` 이다. 액터 안의 op 도
  #chref("ops")의 갈래 규칙을 그대로 따른다.
- `var c counter be spawn actor counter .` 가 액터 하나를 만든다. 상태는 0 으로 시작한다.
- `send c inc` 는 `c` 에게 `inc` 메시지를 보내고 처리가 끝날 때까지 기다려 결과를 받는다.

액터는 메시지를 *한 번에 하나씩* 처리한다. 그래서 그 안의 상태는 동시에 건드려지지 않는다. 자물쇠를 손으로 걸 필요가
없는 이유다.

상태를 고치면서 `fn` 이라고 적으면 거절된다.

#demo("examples/ch25/pure_bad.low")

진단의 말대로 그 쓰기는 *다음 메시지*에 보인다. 호출자가 결과를 기억해 두거나 차례를 바꾸면 틀린 답이 나온다.

== 메시지에 값을 싣는다

메시지 이름 뒤에 값을 나란히 적는다. 받는 쪽에서 액터 자신은 첫 매개변수처럼 다뤄지고, 실은 값이 그 뒤를 잇는다.

#demo("examples/ch25/args.low")

`send acct deposit a` 는 `deposit` 의 `amount` 에 `a` 를 싣는다. 메시지 op 에도 계약을 적을 수 있다.

소유를 가진 값을 보내면 소유가 넘어간다.

#demo("examples/ch25/handoff.low")

`j` 를 첫 메시지로 넘긴 순간 `give_twice` 는 `j` 를 더 쓸 수 없다. 두 흐름이 같은 값을 함께 들고 있으면 경합이 생긴다.
소유가 메시지로만 옮겨 다니면 어느 순간에도 그 값을 만질 수 있는 쪽은 하나뿐이고, 경합은 막는 것이 아니라 *있을 자리가
없다.*

#qa[
  액터를 부르는 것은 결국 함수 호출과 무엇이 다른가?
][
  `send` 는 문법상 호출과 비슷하지만 두 가지가 다르다. 상태가 액터 안에 갇혀 있어 부르는 쪽이 필드를 읽거나 쓸 방법이
  없고, 처리가 한 번에 하나씩이라는 것이 보장된다. 액터에게서 빌린 것을 들고 그 액터에게 다시 말을 거는 것도 거절된다
  (`E-BORROW-EXCL`). 그 액터가 상태를 고치는 동안 우리가 그 상태를 보고 있게 되기 때문이다.
]

== 우편함 --- 넣기와 비우기

#idx("우편함")
`send` 는 처리가 끝날 때까지 기다린다. 결과가 필요 없는 전달은 `spawn send` 로 우편함에 *넣기만* 한다.

#demo("examples/ch25/mailbox.low")

`drive` 는 `inc` 를 세 번 넣고 `drain c` 로 우편함을 넣은 차례대로 비운 뒤 3 을 읽는다. `nodrain` 은 비우지 않았으므로
0 이다. 우편함에 든 메시지는 *저절로 처리되지 않는다.*

처리기가 언제 배달할지 스스로 정하지 않는 까닭은 *결정성*이다. 같은 프로그램은 같은 답을 내야 하고, 배달 시점이 곧
답이다. 비우는 자리를 사람이 고르므로 VM 과 네이티브가 같은 차례로 배달하고, 두 백엔드 대조가 그것을 지킨다. 모든
액터의 우편함을 한꺼번에 비우려면 `schedule .` 을 쓴다. `mailbox bounded 2 .` 처럼 우편함 크기를 정하면 넘치는 넣기가
멈추고, `try spawn send` 는 멈추는 대신 오류를 값으로 준다.

#misconception[액터는 각자 스레드 하나씩이다][
  액터는 실행 단위의 *모양*이지 스레드가 아니다. 이 판의 처리기에서 `send`·`spawn send`·`drain` 은 한 흐름 안에서
  결정적으로 배달된다. 액터가 주는 것은 병렬성이 아니라 *상태의 격리*다. 여러 흐름에 일을 나누는 것은 태스크와 채널
  (#chref("tasks-channels")), 데이터를 나누어 동시에 계산하는 것은 병렬 되풀이(#chref("parallel-atomic"))의 몫이다.
]

== 터지게 두고 다시 세운다

액터의 op 이 `panic` 하면 그 액터를 다시 세울 수 있다. 상태가 처음으로 돌아가고 그 메시지가 다시 처리된다.

#demo("examples/ch25/restart.low")

- `recov` 는 첫 `step` 에서 `bad` 를 1 로 만들고, 둘째 `step` 에서 `panic` 한다. `failure restart max 3 .` 이 있으므로
  상태를 처음(`bad = 0`)으로 되돌리고 메시지를 다시 처리해 42 를 낸다.
- `doomed` 는 언제나 터진다. 두 번 다시 세운 뒤에도 터지면 그 실패는 *위로 넘어가* 프로그램이 멈춘다. 조용히 멈추어
  있는 액터를 남기지 않는다.

정책은 셋이다 --- `restart max <수>`(그 수만큼), `never`(처음 터질 때 바로 위로), `always`(셈하지 않고 계속). 다시 세우는
것은 `panic` 에만 해당한다. 계약 위반은 프로그램이 스스로 적은 약속을 어긴 것이라 다시 해도 같으므로 다시 세우지 않는다.
되살아나는 것은 *자기 상태뿐*이고 다른 액터는 건드리지 않는다.

상태가 어쩌다 이상해진 실패를 하나하나 손으로 되돌리는 코드는 길고, 길면 그 코드 자체가 틀린다. 상태를 처음으로
돌리는 것은 짧고 언제나 맞는 복구다.

== 액터를 쓸 수 있는 자리

프로그램은 자기가 어느 자리에서 도는지 `build profile <이름> .` 으로 적을 수 있다. 적으면 그 자리가 감당하지 못하는
동시성을 번역할 때 거절한다.

#dtable(
  columns: 3,
  id: "actors-profiles",
  caption: [프로파일과 여는 등급],
  [*프로파일*], [*등급*], [*무엇까지*],
  [`freestanding`], [0], [운영체제 없음. 동시성을 쓰지 않는다],
  [`embedded`], [1], [정해진 일감을 나누어 도는 데까지(흐름이 정적으로 정해진다)],
  [`native`], [2], [흐름을 만들고 채널로 주고받는다],
  [`server`], [3], [액터까지],
)

#demo("examples/ch25/profile.low")

`actor` 를 *선언*하는 것만으로 3 등급이다. 프로파일을 적지 않으면 막지 않는다. 적은 사람만 그 약속을 진다.

== 권한을 든 액터와 상태에 둘 수 있는 것

액터의 상태에는 권한 칸을 둘 수 있다. 권한은 번역할 때만 있는 표시라서 그 칸은 실행 중 크기가 0 이다. 대신 그 칸이 무엇으로
채워지는지가 규칙으로 정해진다.

#demo("examples/ch25/capfield.low")

- `root cap allocator .` 가 권한 칸이다. `write` 는 그 칸으로 `alloc_bytes root capacity 16` 을 부른다. 권한을 메시지마다 인자로
  건네지 않아도 된다.
- `main` 이 `cap allocator` 를 받았기 때문에 `spawn actor logbook` 이 허락된다. 칸은 *띄우는 자리의 권한*으로 채워진다.
- 기록 둘을 남기고 `count` 가 2 를 답한다.

권한을 받지 않은 op 이 같은 액터를 띄우면 거절된다.

#demo("examples/ch25/mistake_capfield.low")

이것이 허락되면 `spawn` 한 줄이 없던 권한을 지어낸다. 권한 칸은 "이 액터를 만든 자리가 이미 그 권한을 쥐었다" 는 사실을 옮겨
적는 것일 뿐이다. 표준 할당기 `allocs.fixed_bytes`·`heap_bytes` 도 이 규칙을 따르는 평범한 액터다(#chref("fixed-memory")).

상태 칸에 둘 수 있는 것과 없는 것은 이렇다.

#dtable(
  columns: 3,
  id: "actors-state-fields",
  caption: [액터 상태 칸의 타입],
  [*타입*], [*받아들이나*], [*까닭*],
  [수 · `bool` · `option` · 구조체], [받는다], [값이라서 액터 안에 갇힌다],
  [`slice` · `mut slice`], [받는다], [할당기가 받침 바이트를 이렇게 쥔다(`allocs.bump_bytes`)],
  [`cap allocator` · `cap heap`], [받는다 --- 띄우는 op 에 같은 권한이 있어야 한다], [`E-CAP-FORGE` 가 지어내기를 막는다],
  [`array <수> <타입>`], [거절 --- `E-TYPE-ARRAY`], [고정 길이 배열은 op 입력에서만 받는다. 길이를 둘 자리가 없다],
  [`ref` · `mut_ref`], [이 판은 받는다 --- 그러나 쓰면 멈춘다], [무엇을 빌렸는지 적을 자리가 없다. 아래 "흔한 실수" 를 본다],
)

== 액터로 설계하기 --- 계좌 둘 사이의 이체

지금까지의 조각을 한 설계에 모은다. 계좌마다 액터 하나를 두고, 이체는 두 액터에게 차례로 말을 거는 op 이 맡는다.

#demo("examples/ch25/transfer.low")

- 잔액은 `account` 의 상태에만 있다. 바깥은 `deposit`·`withdraw`·`peek_balance` 세 메시지로만 계좌에 닿는다.
- `withdraw` 는 잔액이 모자라면 *상태를 건드리지 않고* `error insufficient` 를 돌려준다. 멈추지 않는다 --- 잔액 부족은 계좌를 쓰는 쪽이
  다룰 수 있는 실패다(#chref("errors-design")).
- `move` 는 빼기가 성공했을 때만 넣는다. `move 30` 은 보내는 쪽 20, 받는 쪽 30 이라 20030 을 낸다. `move 80` 은 빼기가 실패해 두 계좌가
  50 과 0 그대로다(50000).
- 메시지는 한 번에 하나씩 처리되므로 `withdraw` 가 잔액을 확인하고 줄이는 사이에 다른 메시지가 끼어들 수 없다. "확인하고 쓰기" 사이의
  경합이 이 모양에는 없다.

`withdraw` 의 `errors insufficient .` 에 조건을 붙이지 않은 것은 일부러다. 아래 "흔한 실수" 의 넷째 항목이 그 까닭이다.

== 흔한 실수

#antipattern[크기를 정한 우편함에 비우지 않고 계속 넣는다][
  #demo("examples/ch25/mistake_bounded.low")

  `mailbox bounded 2 .` 는 "대기 중인 메시지는 둘까지" 라는 약속이다. 셋째 `spawn send` 는 넘치므로 `E-VM-MAILBOX-FULL` 로 멈춘다. 멈추는
  대신 다루려면 `try spawn send` 로 넣는다. 그러면 넘침이 `result` 로 돌아오고, 비운 뒤 다시 넣을 수 있다.

  #demo("examples/ch25/bounded_fixed.low")
]

#antipattern[액터가 다루지 않는 메시지를 보낸다][
  #demo("examples/ch25/mistake_unknownmsg.low")

  메시지 이름은 *받는 액터의 타입 안에서* 찾는다. `counter` 에는 `dec` 가 없으므로 `E-IR-UNDEF` 다. 진단이 덧붙인 사연대로, 한때는 이름만으로
  찾아서 같은 이름의 처리기를 가진 두 액터가 조용히 하나를 나눠 썼다.
]

#antipattern[바깥에서 액터의 상태 칸을 읽는다][
  #demo("examples/ch25/mistake_peekstate.low")

  이 장의 첫 약속은 "상태는 액터 안에만 있고 바깥에서 직접 건드릴 수 없다" 였다. `field c value` 는 그 문을 돌아가려는 시도이고,
  `E-ACTOR-FIELD` 로 거절된다. 상태가 바깥에서 읽히면 메시지를 한 번에 하나씩 처리한다는 전제가 소용없어진다 --- 읽는 쪽이 메시지
  사이의 값을 보기 때문이다. 상태가 필요하면 `get` 같은 읽기 메시지를 두고 `send c get` 으로 묻는다.
]

#antipattern[메시지 op 의 오류 조건에 상태 칸을 쓴다][
  #demo("examples/ch25/mistake_errorsstate.low")

  `errors insufficient gt amount balance .` 는 "잔액보다 많이 빼려 하면 이 오류" 라는 뜻으로 적었다. 그런데 `errors` 의 조건은 op 에
  *들어올 때*의 값으로 읽는다(정본 6.4.2). `errors` 는 `requires` 와 같은 쪽 --- **부르는 쪽이 무엇을 잘못했는가**를 말하는 절이고,
  부르는 쪽이 한 일은 건넨 것뿐이기 때문이다. 그래서 몸통이 바꿀 수 있는 이름(상태 칸 · 모듈 `var` · `mut` 자리의 인자)은 조건에 설 수
  없고, 번역이 `E-ERRORS-STATE` 로 거절한다. 나갈 때 값으로 읽으면 성공한 실행이 스스로를 고발한다 --- 잔액이 50 에서 20 으로 줄었으므로
  나갈 때 `gt 30 20` 이 참이 되어 "조건이 참인데 오류를 내지 않았다" 가 된다. 오류 조건은 *입력*으로 적고, 상태를 보는 판정은 본문의
  `guard` 가 맡는다 --- 위의 `transfer.low` 처럼 조건 없이 `errors insufficient .` 만 적는다.
]

#antipattern[상태 칸에 빌림을 둔다][
  #demo("examples/ch25/mistake_reffield.low")

  빌림(`ref`)은 빌려준 쪽보다 오래 살 수 없다(#chref("references")). 액터의 상태는 액터가 사는 동안 남으므로, 그 칸이 무엇을
  빌렸는지 적을 자리가 없다. 그래서 선언에서 `E-ACTOR-STATE-REF` 로 거절한다. 2026-09-16 까지는 선언을 받아들이고 칸을 비운 채 액터를
  띄웠고, `deref r` 에 이르러서야 VM 은 `E-VM-TYPE` 으로, 네이티브는 `panic` 으로 멈췄다. 상태에는 빌림 대신 값을 두고, 값이 크면
  액터가 사는 동안 유효한 슬라이스를 받아 둔다.
]

#misconception[다시 세운 액터는 터지기 직전 상태에서 이어 간다][
  #demo("examples/ch25/restart_resets.low")

  `inc` 두 번으로 값이 2 가 된 뒤 셋째 메시지에서 터진다. 다시 세우면 상태는 *처음*(0)으로 돌아가고 그 메시지가 다시 처리되므로 답은 3 이
  아니라 1 이다. 터지기 직전의 상태는 바로 그 상태 때문에 터졌을 수 있으므로 믿지 않는다. 잃으면 안 되는 값은 액터 바깥 --- 다른 액터나
  파일 --- 에 두고, 다시 선 액터가 그것을 읽게 한다.
]

#misconception[같은 액터 타입의 값들은 상태를 나눠 쓴다][
  #demo("examples/ch25/separate_state.low")

  `spawn actor counter` 를 두 번 하면 상태가 둘 생긴다. `left` 를 두 번 올려도 `right` 는 1 부터 센다. 액터 타입은 설계도이고 상태는
  `spawn` 할 때마다 새로 생긴다. 여럿이 같은 값을 봐야 한다면 그 값을 가진 액터 *하나*를 두고 모두가 그 액터에게 말을 건다.
]

== 이 장의 문법 한눈에

#dtable(
  columns: 3,
  id: "actors-glance",
  caption: [액터의 문법 --- 모양 · 뜻 · 왜 이렇게 생겼나],
  [*모양*], [*뜻*], [*왜 이렇게*],
  [`actor counter do state do value u64 . end … end`], [상태를 가둔 실행 단위를 선언], [상태를 동시에 만질 길이 처음부터 없다],
  [`proc inc … effects state .` · `fn get …`], [상태를 고치는 메시지 · 읽기만 하는 메시지], [`fn`·`proc` 규칙이 그대로 --- 고치면서 `fn` 이면 `E-EFFECT-PURITY`],
  [`var c counter be spawn actor counter .`], [액터 하나를 만든다(상태는 0 에서 시작)], [`spawn` 마다 상태가 따로],
  [`send c inc` · `send acct deposit a`], [보내고 처리가 끝날 때까지 기다린다 · 값을 싣는다], [액터가 먼저 --- 메시지는 액터를 첫 매개변수로 받는 op],
  [`spawn send c inc .` · `drain c .` · `schedule .`], [우편함에 넣기 · 그 액터의 우편함 비우기 · 모두 비우기], [배달 시점을 사람이 고른다 --- 결정성],
  [`mailbox bounded 2 .` · `try spawn send`], [우편함 크기 · 넘침을 값으로 받기], [넘치면 멈추거나 `result`],
  [`failure restart max 3 .` · `never` · `always`], [`panic` 한 액터를 처음 상태로 다시 세운다], [다 쓰면 실패를 위로 넘긴다],
  [`build profile server .`], [액터(3 등급)를 쓸 수 있는 자리], [적은 사람만 그 약속을 진다],
  [`state do root cap allocator . … end`], [권한 칸 --- 실행 중 크기 0], [띄우는 op 에 같은 권한이 없으면 `E-CAP-FORGE`],
)

#recap[
  액터는 상태를 안에 가두고 메시지를 한 번에 하나씩 처리한다. `spawn actor` 로 만들고 `send` 로 기다려 부르며, 메시지
  op 은 `fn`·`proc` 규칙을 따른다. 메시지에는 값을 싣고, 소유 값을 보내면 소유가 넘어간다. `spawn send` 는 우편함에
  넣기만 하고 `drain`·`schedule` 이 비운다. `failure restart` 는 `panic` 한 액터의 상태만 처음으로 돌려 다시 세우고,
  다 쓰면 실패를 위로 넘긴다. `build profile` 은 액터를 쓸 수 있는 자리를 정한다.
]
