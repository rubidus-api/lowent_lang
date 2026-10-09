# 부록 D — 문법 요약

규범은 명세의 본문이고, 이 부록은 자주 찾는 모양을 모은 참고 자료다. 완전한 형식 문법은 명세의 부록 A(A.10)에 있다.

## <a id="sx1"></a>op 머리의 절 차례

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
  access · inplace <쓰기> <읽기> · invalidates <입력> · parallel <이름> split · reduce  ⑩ 메모리를 만지는 법 · 나누어 도는 방법
  requires [static|debug|assume] <조건> .  ⑪ 입력 조건
  ensures <조건> .                         ⑫ 출력 약속 (ret)
  errors <갈래> [<조건>] .                 ⑬ 실패
  tests … · schedule …                     ⑭
do
  <문장들>
end .
```

몸이 C 에 있는 `extern` op 은 절을 같은 차례로 `do … end` **안에** 담는다 — `unsafe extern proc <이름> do <절>* end .`. 블록에는 절만 온다.

## <a id="sx2"></a>선언

```text
module <이름> .
use <모듈> [from "<자리>"] .
def type <이름> <타입> .
def newtype <이름> <타입> .
def struct <이름> do [satisfies <트레이트> .] [layout packed .] [mmio <주소> .] <칸 이름> <타입> [big|little] [rw|ro|wo] . … end .
def enum <이름> do <갈래> [<칸> <타입>]… . … end .
trait <이름> do <op 이름> <절…> . … end .
contract <이름> do requires <조건> . … end .
actor <이름> do [satisfies …] state do <칸> <타입> . … end [failure restart max <수> .] [mailbox bounded <수> .] <op>… end .
build profile <이름> .        build tier t0|t1|t2|t3 .        build <모드> .
build option <이름> bool|int|choice … default <값>
test <이름> [schedule explore_interleavings [limit <수>] .] do expect <조건> . … end .
```

## <a id="sx3"></a>문장

아래에서 `<식>`·`<값>`·`<조건>` 은 **제 점까지 든** 값이다 — 변수면 `a.`, 부름이면 `add a. 1 .`, 리터럴이면 `7`. 줄 끝의 ` .` 은 그 문장을 닫는 점이다.

```text
let <이름> [use <할당기>. | keep <할당기>.] <타입> <식> [else <떠나는 문장>] .
var <이름> <타입> <식> .
let <이름> <타입> <식> else do … end .
let <이름> <타입> <식> else error <이름> do … end .
set <자리> <식> .                         (자리 = <이름>. · field … . · idx … .)
if <조건> do … end [else if <조건> do … end]… [else do … end] .
while <조건> do … end .
for <이름> [mut] <슬라이스> [if <조건>] do … end .
repeat <이름> <타입> <횟수> [if <조건>] do … end .
range <이름> <타입> <처음> <끝> [step <걸음>] [if <조건>] do … end .
cycle <이름> <타입> <처음> while <조건> next <다음 값> [if <조건>] do … end .
guard <조건> else <떠나는 문장> .
match <값> do case <패턴> [when <조건>] do … end . … end .
return [<식>] .    break .    continue .    panic "<글>" .
drop <이름>. .
region <이름> stack|frame|arena|static|heap|mmap|disk|device do … end .
task_group [cancel_on_error] do … end .
pipe <원천> do <스테이지> . … <종결자> . end .
spawn send <액터> <메시지> <값>… . .    drain <액터> .    schedule .
```

## <a id="sx4"></a>패턴

패턴의 이름에는 점을 붙이지 않는다 — 쓰는 자리가 아니라 **짓는** 자리다. 갈래의 몸은 언제나 `do … end .` 이다.

```text
case _                      나머지 전부
case <정수>    case <아래> to <위>
case <갈래> [<이름>…]       case <패턴> or <패턴>
case some <이름>    case none    case ok <패턴>    case error
case <이름> when <조건>
```

## <a id="sx5"></a>식

```text
<연산> <인자>… .                        전위 --- 이름이 열고 점이 닫는다. 우선순위 없음
<변수>.                                 변수도 같은 규칙(이름 + 점). 리터럴 · true · false · none 은 점이 없다
( <식> )                                괄호는 꾸밈 --- 안의 식은 제 점으로 닫는다
expr <a> + <b> * <c> .                  중위 섬 --- * / 가 + - 보다, 비교가 그 아래, and 가 or 보다 강함
lit <타입> do <칸> <식> . … end .       <열거>.<갈래> <값>… .
lit array <타입> <길이> <값>… [_] .     lit slice <타입> <값>… .
field <값> <마디>… .   idx <슬라이스> <번호> .   method <값> <마디>… <이름>. <인자>… .
some <값> .   ok <값> .   error <갈래> .   none
try <식> [else_none | else_error <갈래>] .
comptime <식> .   size_of <타입> .   config <이름> .
spawn actor <타입> .   send <액터> <메시지> <값>… .   spawn <op> <인자>… .   await <핸들> .
alloc_bytes <뿌리> capacity <수> .
payload <값> <갈래> <칸> .
```

## <a id="sx6"></a>여는 이름과 닫개

폼은 이름 하나로 열리고 닫개 하나로 닫힌다. 닫개는 마침표 `.` 이고, 점 하나는 **가장 안쪽에 열린 폼 하나**를 닫는다. 변수도 폼이다 — `a.`. 리터럴과 `true`·`false`·`none`, 그리고 **짓는 자리**의 이름(`let x`, `input n`, 타입의 이름, 패턴의 이름)은 점을 받지 않는다. `do … end` 는 짝인 괄호라 `end` 는 자기 `do` 만 닫고, 블록을 가진 폼도 `end .` 으로 닫는다. `else` 로 이어진 사슬은 한 폼이라 점이 끝에 하나다. 개행은 닫개가 아니라 공백이다. 괄호는 꾸밈이고 블록 경계를 넘지 못한다.

---

[← 이전](sec55.md) · [목차로](README.md) · [다음 →](sec57.md)
