#import "../lib.typ": *

= 태스크와 채널 — 묶인 흐름끼리 주고받기

#chapter-toc()

#prereq(
  ([#chref("effects") 효과], [`concurrent` 는 `wait` 을 딸고 온다]),
  ([#chref("actors") 액터], [상태를 가두고 메시지를 한 번에 하나씩 처리한다]),
  ([#chref("first-program") 첫 프로그램], [`test` 블록은 `--test` 로 돈다]),
)

#deepqa[
  #chref("effects")에서 `concurrent` 와 `wait` 은 무엇으로 갈렸는가?
][
  `wait` 은 누구의 도움 없이도 언젠가 깨어나는 기다림(커널·장치가 깨운다)이고, `concurrent` 는 *같은 프로그램의 다른
  흐름이 진행해야* 끝나는 기다림이다. 그래서 `concurrent` 를 적으면 `wait` 을 함께 적은 것이 된다. 이 장은 그 "다른 흐름"
  을 만들고, 흐름끼리 값을 주고받는 법을 다룬다.
]

#why[
  액터(#chref("actors"))는 상태를 가두는 모양이었다. 그러나 일을 여러 흐름에 나누고 결과를 모으거나, 생산자와 소비자가
  값을 넘겨받는 모양은 액터만으로 적기 번거롭다. 흐름을 다루는 흔한 결함은 셋이다 --- 만들어 놓고 잊은 흐름, 짝이 없어
  영원히 기다리는 흐름, 서로를 기다리는 교착. Lowent 는 흐름의 수명을 블록에 묶어 첫째를 문법으로 없애고, 둘째와 셋째 가운데
  적힌 것만 봐도 알 수 있는 경우를 번역에서 거절한다. 그리고 여러 흐름의 *모든 차례*를 시험하는 도구를 준다.
]

#organizer[
  `task_group` 안에서 `spawn <op>` 으로 흐름을 만들고 `await` 로 결과를 받는 법을 익힌다. 흐름이 자기를 만든 블록보다
  오래 살지 못한다는 규칙을 알게 된다. `channel`·`chsend`·`chrecv` 로 값을 주고받고, 채널 연산이 `concurrent` 효과인
  까닭을 이해한다. 짝 없는 기다림과 보내는 쪽이 없는 교착이 번역에서 거절되는 모습, 그리고 `schedule explore_interleavings`
  시험이 가능한 모든 차례를 돌려 보는 모습을 보게 된다.
]

#chapter-questions()

== 흐름은 블록에 묶인다

#demo("examples/ch26/await.low")

#idx("task_group")
- `task_group do … end` 는 그 안에서 만든 흐름들을 하나로 묶는다. 블록이 끝나는 자리에서 묶인 흐름이 모두 끝나며,
  하나라도 남은 채로 나가지 않는다.
- `spawn square 5` 는 `square 5` 를 도는 흐름을 만들고 그 흐름의 *핸들*을 준다.
- `await h1` 은 그 흐름이 끝날 때까지 기다렸다가 결과를 준다. 기다리는 동안 다른 흐름이 나아간다.

흐름은 *자기를 만든 블록보다 오래 살지 못한다.* 그래서 묶는 자리 밖에서 흐름을 만들면 거절된다.

#demo("examples/ch26/scope.low")

흐름을 만들어 놓고 잊으면 그것은 프로그램이 끝난 뒤에도 남거나, 이미 없어진 것을 만지거나, 아무도 읽지 않는 실패를
낸다. 블록이 수명을 쥐면 그 셋이 문법으로 사라진다 --- 잊을 자리가 없기 때문이다.

같은 파일의 `test join_any_order schedule explore_interleavings` 는 두 흐름이 도는 *모든 차례*를 돌려 보고 답이 같은지
확인한다. 결과 줄의 "2 interleavings agree, exhaustive" 가 그것이다.

#qa[
  `task_group cancel_on_error` 는 무엇을 하는가?
][
  묶인 흐름 하나가 오류로 끝나면, 아직 시작하지 않았거나 기다리는 형제들을 그 오류로 끝난 것으로 처리한다. 다만 취소는
  *되감지 않는다*. 이미 일을 낸 흐름의 효과는 되돌아가지 않고, 앞으로 할 일을 하지 않게 할 뿐이다. 어느 형제가
  취소되었는지는 차례에 달려 있고, 명세는 그 차례를 정하지 않는다. 오류 자체는 `await` 로 드러난다.
]

== 채널 — 차례 있는 그릇

#idx("채널")
채널은 흐름 사이에 값을 넣고 빼는 그릇이다. 넣은 차례대로 나오고, 담을 수 있는 수가 정해져 있다.

#demo("examples/ch26/chan.low")

- `channel u64` 가 채널을 만든다. `chsend ch n` 은 넣고, `chrecv ch` 는 뺀다.
- 빈 채널에서 빼려는 흐름은 멈추고, 가득 찬 채널에 넣으려는 흐름도 멈춘다. 멈춘 흐름은 상대가 오면 다시 이어 간다.
- 소비자 둘과 생산자 둘을 한 그룹에 묶었다. 어떤 차례로 돌든 `sink` 에는 10 과 32 가 모두 들어가 42 가 된다.

시험 줄의 "58 interleavings agree, exhaustive" 는 네 흐름이 멈추고 이어 가는 차례 58 가지를 *모두* 돌려 보았고 답이
모두 42 였다는 뜻이다. 동시성 결함은 대개 드문 차례에서만 드러나서 몇 번 돌려 보는 시험으로는 잡히지 않는다. 작은 그룹이라면
모든 차례를 도는 편이 확실하다.

채널 연산은 `concurrent` 효과다. 완결이 *다른 흐름의 진행에 달려 있기* 때문이다.

#demo("examples/ch26/pure_chan.low")

`effects none` 으로 적은 생산자가 `chsend` 를 부르자 `concurrent`·`wait` 을 선언하지 않았다고 거절된다. 이 효과가 머리에
있으면, 운영체제가 없어 동료 흐름을 돌려 줄 실행기가 없는 기계에서 이 op 을 쓸 수 없다는 것이 번역할 때 드러난다.

== 적힌 것만 봐도 아는 교착

묶는 자리가 흐름을 하나만 만들고 그 흐름이 짝을 기다리면 거절된다.

#demo("examples/ch26/alone.low")

짝이 없는 기다림은 어떤 차례로도 끝나지 않는다. 돌려 보고 매달리기를 기다리지 않고 번역할 때 말한다.

묶인 흐름들이 모두 받기만 하고 아무도 보내지 않아도 거절된다.

#demo("examples/ch26/deadlock.low")

#misconception[교착 검사가 있으니 이 언어에서는 교착이 일어나지 않는다][
  이 검사는 *적힌 것만 봐도 아는* 교착만 잡는다. 두 흐름이 서로 다른 채널을 엇갈려 기다리는 교착, 조건에 따라서만 생기는
  교착은 번역이 잡지 못한다. 그런 교착은 실행 중에 모든 흐름이 멈추면 처리기가 `E-VM-DEADLOCK` 으로 말하고, 작은 경우는
  `explore_interleavings` 시험이 모든 차례를 돌며 찾아낸다. 잡지 못하는 교착이 있다는 사실은 이 규칙이 무엇을 약속하는지
  분명히 하는 것이다.
]

== 아직 없는 것

끝이 없는 채널(`channel … unbounded`)과 여러 흐름이 나누어 가지는 자물쇠 상태(`lock`·`rwlock`)는 아직 받아들이지 않는다
(`E-CHAN-UNBOUNDED`·`E-LOCK-NOTYET`). 없는 것을 받아들이고 나중에 짓는 길도 있지만, 그러면 그 사이에 쓴 프로그램이 되는
줄 알았다가 안 되는 것을 겪는다. 없으면 없다고 말하는 편이 정직하다. 여러 흐름이 같은 메모리를 원자적으로 다루는 길은
#chref("parallel-atomic")이 다룬다.

== 흔한 실수

#antipattern[두 흐름이 서로에게서 받기부터 한다][
  #demo("examples/ch26/mistake_crossed.low")

  `relay a b` 는 `a` 에서 받아 `b` 로 넘기고, `relay b a` 는 그 반대다. 둘 다 받기부터 하므로 누구도 첫 값을 넣지 못한다. 흐름이 둘이고
  채널이 엇갈려서, 적힌 것만 봐서는 교착인지 알 수 없으므로 번역은 통과한다. 실행하면 모든 흐름이 멈춘 것을 처리기가 보고 `E-VM-DEADLOCK`
  으로 알린다. 엇갈린 기다림을 만들지 않으려면 흐름마다 "누가 먼저 보내는가" 를 정해 두고, 가능하면 값이 한 방향으로만 흐르게 짠다.
]

#antipattern[두 흐름에 같은 변수를 쓰기로 빌려준다][
  #demo("examples/ch26/mistake_sharedvar.low")

  두 흐름이 같은 `n` 을 동시에 올리면 데이터 경합이다. 쓰기 빌림은 하나여야 한다는 규칙(#chref("references"))이 흐름 사이에서도 그대로라서
  `E-EXCL` 로 거절된다. 첫 줄의 `W-EFFECT-OVER` 는 이 장의 주제와 상관없는 이 판의 결함이다(`mut_ref` 로 쓰는 것을 `state` 로 세지 않는다).
  흐름은 자기 몫을 *돌려주고*, 합치는 일은 묶는 쪽이 `await` 뒤에 한다.

  #demo("examples/ch26/sharedvar_fixed.low")
]

#misconception[한 번 돌려서 맞으면 동시성 코드도 맞다][
  #demo("examples/ch26/order_dependent.low")

  `first_seen` 은 처음 받은 값만 남긴다. `drive` 를 돌리면 32 가 나오고, 다른 차례라면 10 이 나온다. 한 번 돌려 본 결과는 그 한 차례의
  답일 뿐이다. `schedule explore_interleavings` 시험은 여덟 가지 차례를 모두 돌려 보고, 답이 차례에 따라 갈린다는 것을 `E-SCHED-NONDET` 으로
  알린다. 결정적인 프로그램은 모든 차례에서 같은 답을 내야 한다. 받은 값을 모두 더하는 `chan.low` 의 `sink` 처럼, 차례가 달라도 결과가
  같도록 처리기를 짠다.
]

== 이 장의 문법 한눈에

#dtable(
  columns: 3,
  id: "tasks-channels-glance",
  caption: [태스크와 채널의 문법 --- 모양 · 뜻 · 왜 이렇게 생겼나],
  [*모양*], [*뜻*], [*왜 이렇게*],
  [`task_group do … end .`], [흐름들을 블록에 묶는다 --- 끝에서 모두 끝난다], [만들어 놓고 잊은 흐름이 문법으로 사라진다],
  [`var h1 u64 be spawn square 5 .`], [흐름을 만들고 핸들을 받는다], [묶는 자리 밖이면 `E-SPAWN-SCOPE`],
  [`await h1`], [그 흐름의 결과를 기다려 받는다], [기다리는 동안 다른 흐름이 나아간다],
  [`task_group cancel_on_error do … end .`], [오류 하나에 형제를 취소한다], [취소는 되감지 않는다],
  [`var ch u64 be channel u64 .`], [크기가 정해진 차례 있는 그릇], [끝없는 채널은 아직 없다 --- `E-CHAN-UNBOUNDED`],
  [`chsend ch n` · `chrecv ch`], [넣기 · 빼기 --- 차거나 비면 멈춘다], [`concurrent` 효과 --- 완결이 남에게 달렸다],
  [짝 없는 기다림 · 보내는 쪽 없는 받기], [번역에서 거절(`E-CONC-ALONE` · `E-CONC-DEADLOCK`)], [적힌 것만 봐도 아는 교착],
  [`test … schedule explore_interleavings do … end .`], [가능한 모든 차례를 돌려 답을 맞댄다], [드문 차례의 결함을 시험이 찾는다],
)

#recap[
  `task_group` 안에서 `spawn <op>` 은 흐름을 만들고 핸들을 주며, `await` 가 결과를 받는다. 흐름은 자기를 만든 블록보다
  오래 살지 못하므로 묶는 자리 밖의 `spawn` 은 거절된다. `channel`·`chsend`·`chrecv` 는 크기가 정해진 차례 있는 그릇이고
  채널 연산은 `concurrent` 효과다. 짝 없는 기다림과 보내는 쪽이 없는 교착은 번역에서 거절되고, `schedule explore_interleavings`
  시험은 모든 차례를 돌려 답을 맞댄다.
]
