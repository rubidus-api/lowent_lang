#import "../lib.typ": *

= 부록 D --- 문법 요약

규범은 명세의 본문이고, 이 부록은 자주 찾는 모양을 모은 참고 자료다. 완전한 형식 문법은 아직 없다.

== op 머리의 절 차례

```text
[export] [unsafe] [extern] fn|proc <이름>
  satisfies · lowdoc                       ① 무엇인가
  vector · priority                        ② op 전체의 성격
  input <이름> comptime …                  ③ 번역 시점 입력
  input <이름> cap … · region …            ④ 권한·영역 입력
  using <이름> <타입> .                    ⑤ 깎아 쓰는 할당기
  input <이름> <타입> .                    ⑥ 데이터 입력
  output <타입> .                          ⑦
  effects <원자…> [via <타입>] .           ⑧
  link "<이름>" · variadic · asm <기계>    ⑨ 바깥과 잇는 자리
  access · parallel <이름> split · reduce  ⑩ 나누어 도는 방법
  requires [static|debug|assume] <조건> .  ⑪ 입력 조건
  ensures <조건> .                         ⑫ 출력 약속 (ret)
  errors <갈래> [<조건>] .                 ⑬ 실패
  tests … · schedule …                     ⑭
do
  <문장들>
end .
```

== 선언

```text
module <이름> .
use <모듈> [from "<자리>"] .
type <이름> <타입> .
newtype <이름> <타입> .
struct <이름> do [satisfies <트레이트> .] [layout packed .] [mmio <주소> .] <칸 이름> <타입> [big|little] [rw|ro|wo] . … end .
enum <이름> do <갈래> [<칸> <타입>]… . … end .
trait <이름> do <op 이름> <절…> . … end .
contract <이름> do requires <조건> . … end .
actor <이름> do [satisfies …] state do <칸> <타입> . … end . [failure restart max <수> .] [mailbox bounded <수> .] <op>… end .
build profile <이름> .        build tier t0|t1|t2|t3 .        build <모드> .
build option <이름> bool|int|choice … default <값> .
test <이름> [schedule explore_interleavings [limit <수>]] do expect <조건> . … end .
```

== 문장

```text
let <이름> [<타입>] [using <출처>] be <식> .
var <이름> [<타입>] be <식> .
set <자리> <식> .                         (자리 = 이름 · field … · index …)
if <조건> . do … end [else do … end] .
while <조건> . do … end .
for <이름> <슬라이스> do … end .
guard <조건> . else <떠나는 문장> .
match <값> do case <패턴> [when <조건>] . do … end … end .
return [<식>] .    break .    continue .    panic "<글>" .
drop <이름> .
region <이름> stack|frame|arena|static|heap|mmap|disk|device do … end .
task_group [cancel_on_error] do … end .
pipe <원천> do <스테이지> . … <종결자> . end .
spawn send <액터> <메시지> <값>… .    drain <액터> .    schedule .
```

== 패턴

```text
case _ .                    나머지 전부
case <정수> .  case <아래> to <위> .
case <갈래> [<이름>…] .     case <패턴> or <패턴> .
case some <이름> .  case none .  case ok <패턴> .  case error .
case <이름> when <조건> .
```

== 식

```text
<연산> <인자>…                          전위 --- 우선순위 없음, 폼 안의 폼은 괄호
expr <a> + <b> * <c>                    중위 섬 --- * / 가 + - 보다, 비교가 그 아래, and 가 or 보다 강함
make <타입> do <칸> <식> . … end        <열거>.<갈래> <값>…
field <값> <마디>…   index <슬라이스> <번호>   method <값> <이름> <인자>…
some <값>   ok <값>   error <갈래>   none
try <식> [else_none | else_error <갈래>]
comptime <식>   size_of <타입>   config <이름>
spawn actor <타입>   send <액터> <메시지> <값>…   spawn <op> <인자>…   await <핸들>
alloc_bytes <뿌리> capacity <수>
```

== 머리와 닫개

폼은 머리 하나로 시작해 닫개 하나로 끝난다. 닫개는 떨어진 마침표 `.` 이고, `)` 와 `end` 가 안에 열린 것을 함께 닫는다. 개행은
닫개가 아니라 공백이다. 괄호는 블록 경계를 넘지 못한다.
