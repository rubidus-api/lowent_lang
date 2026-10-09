# A 부록 A — 문법 요약 (Annex A: Grammar summary)

(1) 이 부록은 ⟦§6⟧ 이 정한 문법 가운데 **자주 찾는 것을 모은 참고 자료**다.
    규범은 본문이며, 이 부록과 본문이 어긋나면 본문이 옳다.

(1a) **낱말 목록(A.1)과 전체 문법(A.10)은 망라한다** — A.1 에 없는 것은 낱말이 아니고, A.10 의 생성 규칙이
    읽지 못하는 낱말의 나열은 이 언어의 소스가 아니다. A.2 ~ A.5 와 A.7 은 자주 쓰는 모양을 보기 좋게 모은 것이며
    모양을 다 담지 아니한다.

(1b) A.10 이 정하는 것은 **모양**뿐이다(⟦§1.5⟧ (4)). 모양이 맞는 것 가운데 어떤 것이 적합한지는 본문의 조항이
    정한다.

## A.1 낱말

(1) 다음이 낱말의 전부다. 이 목록에 없는 것은 낱말이 아니다.

```문법틀: 낱말 목록 — 이 목록이 전부다
actor      break      case       continue   contract   cycle      def
do         drop       else       end        enum       expect     export
expr       extern     false      fn         for        guard      if
keep       let        lit        match      module     newtype    next
none       proc       range      repeat     return     satisfies  send
set        spawn      state      step       struct     test       trait
true       try        type       unsafe     use        var        while
```

(2) 예약된 낱말은 이름이 될 수 없다(⟦§6.1.2⟧). 이 목록의 낱말은 모두 이 문서가
      다루며, 다루지 않는 채 예약만 해 둔 낱말은 **없다**.

> [!참고]
> 이 목록이 한 화면에 담기는 것은 우연이 아니라 규율이다(⟦§6.1.2⟧). 낱말을
> 더하려면 그만한 값어치를 보여야 한다.

## A.2 문장의 모양

(1) 아래는 자주 쓰는 모양을 모은 것이다. `<…>` 는 채워 넣는 자리다. 블록을 가진 선언도 `end` 뒤의 점으로 닫힌다(⟦§6.1.6⟧ (2b)).

```문법틀: 선언
module <이름> .

use <모듈이름> .

def type <이름> <타입> .
def newtype <이름> <타입> .

def struct <이름> do
  <칸이름> <타입> .
end .

def enum <이름> do
  <갈래이름> .
end .

export fn <이름> input <이름> <타입> . output <타입> .
  requires <조건> .
  ensures <조건> .
do
  <문장들>
end .

proc <이름> input <이름> <타입> . output <타입> . effects <효과들> .
do
  <문장들>
end .
```

## A.3 문장

```문법틀: 문장
let <이름> <타입> <식> .
var <이름> <타입> <식> .
let <이름> <타입> <식> else <빠져나가는 문장> .
set <자리> <식> .

if <조건> do <문장들> end .
if <조건> do <문장들> end else do <문장들> end .
while <조건> do <문장들> end .
for <이름> <원천> do <문장들> end .
repeat <이름> <타입> <횟수> do <문장들> end .
range <이름> <타입> <처음> <끝> do <문장들> end .
cycle <이름> <타입> <처음> while <조건> next <다음 값> do <문장들> end .
guard <조건> else <빠져나가는 문장> .

return <식> .
break .
continue .
```

(1) 이 틀의 `<식>` · `<조건>` · `<원천>` · `<자리>` 는 **제 점까지 든 폼**이다 — 변수면 `a.`, 부름이면 `add a. 1 .`, 리터럴이면
      `7` 이다(⟦§6.1.5⟧). 줄 끝의 점은 그 문장을 닫는다. 블록을 가진 문장도 `end` 뒤의 점으로 닫히고, `else` 로 이어진 사슬은
      끝에 점 하나다. `<빠져나가는 문장>` 도 제 점을 갖는다(`guard gt a. 0 . else return 0 . .`). 이 문장들의 생성 규칙은 A.10.3 에
      있고, 뜻은 ⟦§6.5.1⟧ · ⟦§6.5.3.1⟧ · ⟦§6.5.4⟧ 가 정한다.

## A.4 갈래·시험·액터

```문법틀: 갈래를 가르기 · 시험 · 액터
match <값> do
  case <갈래> do <문장들> end .
  case <갈래> do <문장들> end .
end .

test <이름>
do
  expect <조건> .
end .

actor <이름> do
  state do
    <칸이름> <타입> .
  end .

  proc <이름> output <타입> . effects state .
  do
    <문장들>
  end .
end .
```

## A.5 식

```문법틀: 식
rem 전위 — 이름이 열고 점이 닫는다. 우선순위가 없다
add <a> <b> .        sub <a> <b> .        mul <a> <b> .
div <a> <b> .        mod <a> <b> .
eq <a> <b> .         ne <a> <b> .         lt <a> <b> .
le <a> <b> .         gt <a> <b> .         ge <a> <b> .
and <a> <b> .        or <a> <b> .         not <a> .
len <슬라이스> .      idx <슬라이스> <번호> .
widen <타입> <값> .   narrow <타입> <값> .

rem 변수 — 이름에 점을 붙인다. 리터럴과 true · false · none 에는 점이 없다
<이름>.

rem 중위 — expr 섬 안에서만. 섬도 제 점으로 닫힌다
expr <a> + <b> * <c> .
```


## A.6 리터럴의 어휘 문법

(1) 이 절은 ⟦§6.1.4⟧ 가 산문으로 규범한 리터럴을 **생성 규칙으로** 다시 적는다. 산문과
      이 규칙이 갈리면 산문이 이긴다 — 이 절은 요약이지 새 규범이 아니다.

(2) 표기: `{ }` 는 0 회 이상, `[ ]` 는 선택, `|` 는 택일, `'…'` 는 그대로의 글자다.
      대문자 이름은 렉서가 내는 토큰이고, 소문자 이름은 그 토큰을 이루는 조각이다.

```문법틀: 수 리터럴
NUMBER  = [ sign ] , ( float | hex | bin | dec ) ;
sign    = '+' | '-' ;
sep     = '_' ;
digit   = '0'…'9' ;
hexd    = digit | 'a'…'f' | 'A'…'F' ;
bind    = '0' | '1' ;

dec     = digit , { [ sep ] , digit } ;
hex     = '0' , ( 'x' | 'X' ) , hexd , { [ sep ] , hexd } ;
bin     = '0' , ( 'b' | 'B' ) , bind , { [ sep ] , bind } ;

float   = dec , '.' , dec , [ dexp ]
        | dec , dexp
        | hex , [ '.' , hexd , { [ sep ] , hexd } ] , hexp ;
dexp    = ( 'e' | 'E' ) , [ sign ] , dec ;
hexp    = ( 'p' | 'P' ) , [ sign ] , dec ;
```

(3) **부호는 붙여야 부호다.** `-5` 는 리터럴 하나이고 `- 5` 는 아니다. `expr` 섬(⟦§6.3⟧)
      **밖**에서 숫자에 붙은 부호는 값의 일부이며, 섬 **안**에서 `-` 는 연산자다. 그러므로
      폼 자리의 `g 10 -3` 은 인자 **둘**이지 뺄셈이 아니다. 그 갈림을 띄어쓰기가 아니라
      **섬이** 정한다.

(4) **구분자는 자릿수 사이에만 온다.** 위 규칙의 `[ sep ] , digit` 이 그 뜻이다 — 맨 앞
      (`_1`)·맨 뒤(`1_`)·진법 표시 바로 뒤(`0x_1`)에는 올 수 없다. 구분자는 모든 진법과
      부동소수의 **모든 부분**(정수부·소수부·지수부)에서 쓸 수 있다.

(5) **`float` 이 `dec` 보다 먼저 온다.** 점도 지수도 없으면 `dec` 로 읽히고, 점이나 지수가
      있으면 `float` 이다. 점 **앞뒤 모두** 자릿수가 있어야 하므로(⟦§6.1.4⟧ (2)) `1.` 은
      부동소수가 아니라 정수 `1` 뒤에 폼을 닫는 점이다.

(6) **앞의 0 은 팔진이 아니다.** `dec` 는 `0` 으로 시작할 수 있고 값은 십진이다.

```문법틀: 문자·문자열 리터럴
CHAR    = [ prefix ] , "'" , ( charchar | escape ) , "'" ;
STRING  = [ prefix ] , '"' , { strchar | escape } , '"' ;

prefix  = 'u' | 'U' ;
charchar= ? "'" 도 '\' 도 아닌 글자 하나(UTF-8) ? ;
strchar = ? '"' 도 '\' 도 아닌 바이트 하나 ? ;
escape  = ? ⟦§6.1.4⟧ 의 «이스케이프 — 닫힌 집합 열넷» 표에 있는 것 ? ;
```

```문법틀: 텍스트 리터럴과 여러 줄 주석
TEXTLIT = 'text' , sp , { sp } , tag , [ sp , { sp } , proc ] , { sp } , [ rem ] , nl ,
          { line , nl } ,
          tag , { sp } , ( nl | eof ) ;
NOTE    = 'note' , sp , { sp } , tag , { linech } , nl ,
          { line , nl } ,
          tag , { sp } , ( nl | eof ) ;

tag     = ( letter | '_' ) , { letter | digit | '_' } ;
proc    = 'u' | 'U' ;
rem     = 'rem' , { linech } ;
line    = ? 닫는 줄이 아닌 줄 ? ;
linech  = ? 줄바꿈이 아닌 바이트 ? ;
sp      = ' ' | 탭 | CR ;
nl      = LF ;
eof     = ? 파일의 끝 ? ;
letter  = 'a'…'z' | 'A'…'Z' ;
```

(6a) 닫는 줄은 **줄의 맨 첫 칸에서** 여는 줄과 같은 `tag` 로 시작하고, 그 바로 뒤가 `letter` · `digit` · `'_'` 가
      아닌 줄이다. 그러므로 들여 쓴 `tag` 와 `tag` 보다 긴 낱말로 시작하는 줄은 `line` 이다. 위 규칙의 두 번째
      `tag` 는 첫 번째와 **같은 글자열**이어야 한다 — 이 조건은 생성 규칙만으로는 적을 수 없어 여기 글로 적는다.

(6b) `TEXTLIT` 의 값은 `line` 들을 그 사이의 `nl` 과 함께 이은 바이트열이다. **마지막 `line` 뒤의 `nl` 은 값에
      들지 아니한다.** `line` 이 하나도 없으면 값은 빈 바이트열이다. 본문은 이스케이프를 풀지 아니한다(⟦§6.1.4⟧ (29)).

(6c) 닫는 줄에서 `tag` 와 `{ sp }` 뒤에 `nl` 도 `eof` 도 아닌 것이 오면 번역이 거부된다
      (`TEXTLIT` 은 `E-TEXTLIT-TAIL`, `NOTE` 는 `E-NOTE-TAIL`). 여는 줄에서 `tag` 뒤에 `proc` 도 `rem` 도 아닌 것이 오면
      거부된다(`E-TEXTLIT-OPENER`). `NOTE` 의 여는 줄은 `tag` 뒤를 줄 끝까지 읽지 아니한다.
      `note` 뒤에 `tag` 가 없으면 거부된다(`E-NOTE-TERM`) — `TEXTLIT` 은 같은 자리에서 `E-TEXTLIT-TERM` 이다.

(7) 접두사는 **닫힌 집합 둘**이다. 그 밖의 글자를 리터럴 앞에 붙이면 번역이 거부된다
      (`E-STR-PREFIX`). 접두사가 값의 원소를 무엇으로 볼지 정한다(⟦§6.1.4⟧ (27)).

(8) 문자 리터럴은 **한 칸에 들어가야 한다.** 접두사가 정한 원소 하나를 넘으면 거부되며
      (`E-CHAR-WIDTH`), 빈 것도 거부된다(`E-CHAR-EMPTY`).

> [!참고]
> 이스케이프의 집합을 여기에 다시 적지 않은 것은 뜻이 있다. 같은 목록이 두 곳에 있으면
> 둘은 반드시 갈리고, 갈린 뒤에는 어느 쪽이 규범인지 아무도 모른다. 집합은 ⟦§6.1.4⟧ 의
> 표 한 곳에만 있다.

## A.7 머리 낱말과 닫개

(1) 폼은 **이름** 하나로 열려 **닫개** — 점 — 하나로 닫힌다(⟦§6.1.5⟧ · ⟦§6.1.6⟧). 다음 표가 머리 낱말마다
      어떤 모양을 갖는지를 모은 것이다. 닫개는 어느 줄에서나 점이다 — 블록을 가진 폼은 `end .` 으로 끝난다. 그래서 닫개 칸은 따로 채우지 않고, 모양의 끝에 적힌 점이 그것이다.

(2) 표기: `<…>` 는 채워 넣는 자리, `*` 는 0 회 이상, `[…]` 는 선택이다.

```문법틀: 머리 · 모양 · 닫개
머리                모양                                              닫개
──────────────────  ────────────────────────────────────────────────  ──────
module              module <이름> .
use                 use <이름> from "<경로>" [as <별칭>] .
def type            def type <이름> <타입> .
def newtype         def newtype <이름> <타입> .
def struct          def struct <이름> do <칸>* end .  (칸 = <이름> <타입> .)
def enum            def enum <이름> do <갈래>* end .  (갈래 = <이름> [<칸>*] .)
trait               trait <이름> do <서명>* end . (서명 = <이름> <절>*)
actor               actor <이름> do <state·절·op>* end .
state               state do <칸>* end .        (actor 안)
contract            contract <이름> do <절>* end .
fn / proc           [꾸밈]* fn <이름> <절>* do <폼>* end .
test                test <이름> [schedule <절>] do <폼>* end .
expect              expect <조건> .           (시험 블록의 단언)
input               input [comptime] <이름> <타입> .
using               using <이름> <타입> .     (op 이 깎아 쓰는 얼로케이터)
output              output <타입> .   |   output <이름> <타입> .     (이름 붙은 결과 — 0 에서 시작하는 지역, §6.4.1 (3e))
effects             effects <원자>* .
access              access <이름> <모드> .
parallel            parallel <이름> <모드> .
requires / ensures  requires <조건-폼>* .
errors              errors <갈래> [<조건-폼>] .  (한 절에 오류 하나)
tests               tests <이름>* .
let / var           let <이름> [use <이름>. | keep <이름>.] <타입> <폼> [else …] .   (else 가 블록으로 끝나도 점)
set                 set <자리-폼> <폼> .
into                pop <스택> into <자리> .   (프렐류드 문형)
if (문)             if <폼> do <폼>* end [else if <폼> do <폼>* end]* [else do <폼>* end] .
guard               guard <폼> else <나가는-폼> . .   (나가는 폼의 점, 그리고 guard 의 점)
while               while <폼> do <폼>* end .
for                 for <이름> [mut] <폼> [if <폼>] do <폼>* end .
repeat              repeat <이름> <타입> <폼> [if <폼>] do <폼>* end .
range               range <이름> <타입> <폼> <폼> [step <폼>] [if <폼>] do <폼>* end .
cycle               cycle <이름> <타입> <폼> while <폼> next <폼> [if <폼>] do <폼>* end .
region              region <이름> <종류> do <폼>* end .
borrow              borrow <이름> <폼> do <폼>* end .
return              return [<폼>] .
break               break .                   (라벨은 없다)
continue            continue .
lit                 lit <타입> do <칸초기>* end .  (칸초기 = <이름> <폼> .)
lit (나열)          lit array <타입> <길이> <값>… [_] .  ·  lit slice <타입> <값>… .
lit (칸 채우기)     lit array <타입> <길이> do <번호> <값> . … [_ <값> .] end .
lit (SIMD)          lit vec <타입> <레인> <값>… [_] .
extern              extern fn/proc <이름> do <절>* end .  (몸이 씨)
match               match <폼> do <가지>* end [else do <폼>* end] .   (가지 = case <패턴> do <폼>* end .)
```

(2a) `fn`/`proc` 머리의 `<절>*` 은 ⟦§6.4.1⟧ (3a) 의 한 차례를 따른다: `satisfies`·`lowdoc` · `vector`·`priority` ·
      comptime 입력 · 권한·영역 입력 · `using` · 데이터 입력 · `output` · `effects` · `link`·`variadic` · `asm` ·
      `asm`·`absorbs`·`reference`·`why` · `access`·`inplace`·`invalidates`·`parallel`·`reduce` · `requires` · `ensures` · `errors` · `tests` · `schedule` (`E-CLAUSE-ORDER`).

(2b) **블록 선언**(`struct`·`enum`·`trait`·`actor`·`state`·`contract`)의 몸은 `do` 로 열고 `end` 로 닫는다 — `fn` 의 몸과 제어
      블록과 같은 한 규칙이다. `def struct <이름> .` 처럼 점으로 열거나 이름 뒤에서 줄만 바꾸는 꼴은 거부된다(`E-STMT-NODO`).
      개행은 닫개가 아니므로(⟦§6.1.6⟧) 줄바꿈으로는 머리가 닫히지 않는다.

(3) 이 표는 **머리와 그 모양**을 규범한다. 모양 칸은 자주 쓰는 꼴을 적은 것이며, 정확한
      규범은 각 조항의 본문이다 — 둘이 갈리면 본문이 이긴다.

(4) 이 목록에 없는 머리는 없다. 낱말 아닌 머리(`region`·`borrow`·`into`)는 프렐류드
      연산이며, 그것도 ⟦§9⟧ 가 정한 이름이지 새로 지을 수 있는 것이 아니다.

## A.8 없앤 낱말

(1) 다음은 한때 이 언어에 있었으나 **없앤** 낱말이다. 적으면 거부되며
      (`E-VOCAB-REMOVED`), 처리기는 그 자리에서 무엇을 대신 쓰는지 말한다.

> [!표] 없앤 낱말 — 그리고 대신 쓰는 것
> #table(columns: (auto, auto, 1fr), stroke: 0.5pt + rgb("#bbb"), inset: 6pt,
> [*없앤 것*], [*대신*], [*왜*],
> [`loop`], [`while true do`], [똑같은 뜻의 두 철자였다],
> [`give`], [`return`], [똑같은 뜻의 두 철자였다],
> [`unit`], [`void`], [한 뜻에 두 철자 — 명세와 도구가 다르게 불렀다],
> [`calcop`], [`fn`], [순수한 셈은 함수다. 그리고 «op» 은 이제 프렐류드 연산만 가리킨다],
> [`procop`], [`proc`], [효과를 내는 것은 수학의 함수가 아니다],
> [`is`], [(적지 않는다)], [장식이었다 — 아무도 읽지 않아 `def type h zzz u8 .` 이 통과했다],
> [`as`], [(적지 않는다)], [어디서도 무게가 없었다 — 블록 이름은 읽히고 **버려졌다**],
> [`local`], [(적지 않는다)], [기본값이 이미 그것이다 — `export` 아닌 것은 밖에서 안 보인다],
> [`to`], [`field a. b .`], [중위 접근을 없앴다 — 한 뜻에 철자가 넷이었다],
> [`in`], [`field a. b .` · `idx a. i. .`], [`to` 의 거꾸로 철자 — 셋째 철자였다],
> [`when`], [(적지 않는다)], [`errors` 절이 오류 하나만 받으므로 표식이 필요 없다],
> [`on`], [`proc`], [액터 블록 안이면 이미 메시지 처리기다. 게다가 **순수/절차 비트를 우회했다**],
> [`fail`], [`return error <갈래>`], [명세의 어휘에 아예 없었다 — 옛 해석기에만 살아 있었다],
> [`;`], [`.`], [닫개의 **세 번째 철자**였다(⟦§6.1.6⟧)],
> [`make`], [`lit`], [값 리터럴의 머리를 하나로 — 구조체·배열·벡터·슬라이스 값이 모두 `lit <타입> …` 로 시작한다],
> [`index`], [`idx`], [자주 쓰는 낱말을 짧게 — 읽기 `idx a. 3 .` · 쓰기 `set idx a. 3 . v. .`],
> [`be`], [(적지 않는다)], [타입의 끝은 타입 문법의 인자 수가 정한다 — 이름과 타입 사이에서 나르는 것이 없었다. 거부의 코드는 `E-LET-BE` 다(⟦§6.5.1⟧ (3a))],
> [`.이름` (머리 표시)], [맨 전위 · 괄호], [머리를 여는 셋째 철자였다 — 띄어쓰기 한 칸이 닫개를 머리로 바꿨다],
> [`not is_some x. . .`], [`is_none x. .`], [같은 뜻의 두 철자였다(`E-NOT-IS-SOME`)],
> [`union` · `select` · `any` … (비트셋·레인)], [`bitset_union` · `lane_select` · `lane_any` …], [짧은 낱말을 저자에게 남긴다(⟦§6.3.3⟧ (1e))],
> )

(2) 없앤 까닭은 대개 **같은 뜻의 두 철자**이거나 **아무것도 사지 못하는 낱말**이었기
      때문이다. 낱말은 목록에 오르는 값을 해야 하며, 하지 못하면 내려온다.

(2a) 타입 선언의 옛 머리 — `def` 없이 `struct`·`enum`·`type`·`newtype` 으로 시작하는 선언 — 도 거부된다
      (`E-VOCAB-REMOVED`). 타입을 짓는 낱말은 `def` 하나다(⟦§6.1.2⟧ (3)). 네 낱말은 `def` 뒤의 갈래 이름으로,
      그리고 `type` 은 타입 매개변수의 종류(`input comptime t type .`)로 남는다. `lowentc --fmt` 가 옛 모양을 옮겨 쓴다.

(3) `as` 와 `to` 는 **자리 표식**으로만 남아 있다 — `use … as <별칭>` 과
      `case <아래> to <위>` 에서다. 그 자리 밖에서는 낱말이 아니다.

> [!산문]
> 없앤 낱말을 이 문서가 적어 두는 까닭. 옛 코드나 옛 글을 읽던 사람이 그 낱말을 만나면
> *"내가 뭘 잘못 쓴 건가"* 를 묻게 된다. 여기에 적혀 있으면 그 물음이 한 줄로 끝난다 —
> **없앴고, 대신 이것을 쓴다.**

## A.9 타입 낱말

(1) 타입을 적는 자리에 올 수 있는 낱말은 다음이 전부다. 여기 없는 이름은 저자가 지은
      이름(⟦§6.2.9⟧)이거나 아무것도 아니다.

```문법틀: 쓸 수 있는 타입 낱말
rem 수
u8    i8    u16   i16   u32   i32   u64   i64   usize  isize   f32   f64
bool  void

rem 줄과 묶음
slice   array   segments   set   stack   range   vec   bitset   mask

rem 답을 담는 것
result   option

rem 자리와 빌림
ref   mut_ref   mut   owned   region

rem 바깥과 이어지는 것
cap   mmio
fn    unsafe_fn

rem 그 밖
self
```

(2) 다음 낱말은 **이름으로 받되 뜻이 아직 없다.** 처리기는 그것을 말한다(`W-NOT-YET`,
      ⟦§4.7⟧) — 조용히 받아 주지 아니한다.

```문법틀: 이름만 받는 타입 낱말
byte   char   str   string   bytes_view   dyn   atomic
list   raw   addr
rng   clock   device   file_system   net   tty   process
```

(2a) 뒤의 넷(`list`·`raw`·`addr`·`rng`)은 이 판에서 이 갈래로 옮겼다. 위 (1) 의 목록에
      실려 있었으나 **뜻을 정한 조항이 이 정본 어디에도 없었고**, 하강도 그 이름을 읽지
      못한다 — 그 서명의 op 은 통째로 해석기로 내려간다. 뜻을 지어내는 것보다 **아직
      없다고 말하는 것**이 옳다.

(2b) `addr` 과 `raw` 는 **연산으로는 뜻이 있다**(`addr <이름>` 은 자리를 얻고, 그것은
      `unsafe` 효과다 --- ⟦§8.11⟧). 여기서 뜻이 없다고 하는 것은 **타입을 적는 자리**의
      낱말이다. 마찬가지로 `cap rng` 의 `rng` 는 권능의 **종류**이므로 이 조항에 들지
      아니한다 --- 홀로 타입으로 선 `rng` 만이 이름뿐이다. 그래서 위 (1) 의 권능 종류 줄에서
      `rng` 을 내렸다.

(2c) `rng` · `clock` · `device` · `file_system` · `net` · `tty` · `process` 일곱은 **권능의 종류**이며,
      `cap <종류>` 자리에서만 뜻이 있다(⟦§7.2⟧). 홀로 타입으로 적으면 이 갈래에 든다 ---
      일곱이 같은 처지이므로 **같이 말한다.** 하나만 말하고 여섯이 조용하면, 읽는 사람은
      그 차이에 뜻이 있다고 여기게 된다.

(3) 다음 셋은 **공유 상태의 타입**이며 지금은 거절된다. 이름이 어휘에 있는 까닭은
      *"없는 타입"* 이라 말하는 것이 거짓이기 때문이다 — 명세에 있는 타입을 두고
      **네 프로그램이 틀렸다** 고 말할 수는 없다.

```문법틀: 아직 거절되는 타입 낱말
shared_read   lock   rwlock
```

(4) `unsafe_ptr` 은 타입이 아니라 **한정자**다. `mut`·`owned` 처럼 타입 앞에 붙으며,
      홀로 오면 거절된다.

(5) **주소 공간 한정자의 자리는 예약되어 있다.** 프로그램 메모리와 데이터 메모리가 나뉜 기계나 가속기처럼 공간마다
      포인터의 크기와 뜻이 다른 실행 환경(⟦§5.3⟧ (4a))을 위해, 슬라이스와 참조 타입에는 «어느 주소 공간을 가리키는가» 를
      적는 한정자 자리를 둔다. 그 철자와 뜻은 그런 실행 환경을 대상으로 들일 때 정한다. 지금은 기본 공간 하나뿐이며 그
      자리에 오는 낱말은 없다.

> [!산문]
> 이 세 갈래를 갈라 적는 까닭. *"쓸 수 있다"* 와 *"이름은 안다"* 와 *"없다"* 는 읽는
> 사람에게 **서로 다른 일**을 시킨다 — 쓰거나, 기다리거나, 다른 길을 찾거나. 셋을
> 한 목록에 뭉뚱그리면 그 판단을 사람이 매번 도구를 돌려 보고 해야 한다.

## A.10 전체 문법

(1) 이 절은 소스 파일 하나의 문법을 **처음부터 끝까지** 생성 규칙으로 적는다. 표기는 ⟦§1.5⟧ 의 것이다.
      본문의 조항에 같은 이름의 규칙이 있으면 이 절의 것은 그것과 **글자까지 같다.**

(2) 문법은 두 층이다(⟦§1.5⟧ (3a)). A.10.1 은 글자를 토큰으로 가르고, A.10.2 부터는 토큰의 나열을 읽는다.
      공백과 주석은 A.10.1 에서 버려지고 그 뒤에는 나타나지 아니한다.

(3) 소스의 모양은 다음 **네 규칙**으로 선다. 이 절의 생성 규칙은 그 넷을 풀어 적은 것이다.

> [!표] 모양을 세우는 네 규칙
> #table(columns: (auto, 1fr), stroke: 0.5pt + rgb("#bbb"), inset: 6pt,
> [*규칙*], [*내용*],
> [머리], [모든 문장과 선언은 머리 낱말 하나로 시작한다. 머리가 그 뒤의 모양을 정한다],
> [블록], [`do` 는 `end` 와 짝이다. `end` 는 제 `do` 만 닫는다],
> [이름과 점], [쓰는 자리의 이름은 폼을 열고, 점 하나가 가장 안쪽에 열린 폼 하나를 닫는다. 변수는 곧바로 닫힌 폼이다(`a.`). 문장 · 선언 · 절 · 칸도 제 머리가 열고 점이 닫는다],
> [낱말의 자리], [이름을 만드는 자리 · 타입의 자리 · 문법과 부록 D 의 표가 정한 낱말의 자리에 선 이름은 폼을 열지 아니한다(점이 없다)],
> )

(4) 네 규칙은 모두 **글자만 보고** 판정한다 — 선언을 읽지 않아도 나무가 선다. op 이 인자를 몇 개 받는지는 모양을
      세우는 재료가 아니고, 나무가 선 뒤의 검사다(⟦§6.1.5⟧ (3)). «점» 규칙에 예외는 없다.

## A.10.1 어휘 문법

```구문: 전체 문법 — 어휘
source    ::= { separator | token }
separator ::= whitespace | line-comment | block-comment
token     ::= keyword | name | path | literal | punctuation

punctuation ::= "." | "(" | ")" | "," | operator
operator    ::= "+" | "-" | "*" | "/"

whitespace      ::= " " | tab | carriage-return | newline
blank           ::= " " | tab | carriage-return
tab             ::= ? U+0009 ?
carriage-return ::= ? U+000D ?
newline         ::= ? U+000A ?
line-comment    ::= "rem" { line-char }
block-comment   ::= "note" blank { blank } tag { line-char } newline
                    { comment-line newline }
                    tag { blank } ( newline | end-of-source )
tag             ::= name
line-char       ::= ? newline 이 아닌 글자 ?
comment-line    ::= ? 닫는 줄이 아닌 줄 ?
end-of-source   ::= ? 소스의 끝 ?

keyword ::= "actor" | "break" | "case" | "continue" | "contract" | "cycle" | "def"
          | "do" | "drop" | "else" | "end" | "enum" | "expect" | "export"
          | "expr" | "extern" | "false" | "fn" | "for" | "guard" | "if"
          | "keep" | "let" | "lit" | "match" | "module" | "newtype" | "next"
          | "none" | "proc" | "range" | "repeat" | "return" | "satisfies" | "send"
          | "set" | "spawn" | "state" | "step" | "struct" | "test" | "trait"
          | "true" | "try" | "type" | "unsafe" | "use" | "var" | "while"

name       ::= name-start { name-char }
name-start ::= "a" … "z" | "A" … "Z" | "_"
name-char  ::= name-start | "0" … "9"
path       ::= name "." path-part { "." path-part }
path-part  ::= name-char { name-char }

literal        ::= number | char-literal | string-literal | text-literal

number         ::= [ sign ] ( float | hex | bin | dec )
sign           ::= "+" | "-"
digit          ::= "0" … "9"
hex-digit      ::= digit | "a" … "f" | "A" … "F"
bin-digit      ::= "0" | "1"
dec            ::= digit { [ "_" ] digit }
hex            ::= "0" ( "x" | "X" ) hex-digit { [ "_" ] hex-digit }
bin            ::= "0" ( "b" | "B" ) bin-digit { [ "_" ] bin-digit }
float          ::= dec "." dec [ dec-exponent ]
                 | dec dec-exponent
                 | hex [ "." hex-digit { [ "_" ] hex-digit } ] hex-exponent
dec-exponent   ::= ( "e" | "E" ) [ sign ] dec
hex-exponent   ::= ( "p" | "P" ) [ sign ] dec

char-literal   ::= [ prefix ] single-quote ( char-char | escape ) single-quote
string-literal ::= [ prefix ] double-quote { string-char | escape } double-quote
prefix         ::= "u" | "U"
escape         ::= backslash ( simple-escape
                             | "x" hex-digit hex-digit
                             | "u" hex-digit hex-digit hex-digit hex-digit
                             | "U" hex-digit hex-digit hex-digit hex-digit hex-digit hex-digit hex-digit hex-digit )
simple-escape  ::= backslash | double-quote | single-quote
                 | "a" | "b" | "f" | "n" | "r" | "t" | "v" | "0"
single-quote   ::= ? U+0027 ?
double-quote   ::= ? U+0022 ?
backslash      ::= ? U+005C ?
char-char      ::= ? 작은따옴표도 역슬래시도 줄바꿈도 아닌 글자 하나 ?
string-char    ::= ? 큰따옴표도 역슬래시도 줄바꿈도 아닌 바이트 하나 ?

text-literal   ::= "text" blank { blank } tag [ blank { blank } processor ] { blank } [ line-comment ] newline
                   { text-line newline }
                   tag { blank } ( newline | end-of-source )
processor      ::= "u" | "U"
text-line      ::= ? 닫는 줄이 아닌 줄 ?
```

(1) `keyword` 가운데 `rem` · `note` · `text` 로 시작하는 토큰은 없다 — 그 셋은 낱말이 아니라 주석과 텍스트 리터럴을
      여는 글자열이다(⟦§6.1.1⟧ · ⟦§6.1.4⟧).

(2) 아래 구문 문법에 `"from"` · `"input"` · `"slice"` 처럼 `keyword` 에 없는 글자열이 끝 기호로 나오면, 그것은 **그
      자리에서만** 뜻을 갖는 `name` 토큰이다(문맥 낱말). 그 자리 밖에서는 보통 이름이다.

## A.10.2 번역 단위와 선언

```구문: 전체 문법 — 선언
unit           ::= { top-form }
top-form       ::= module-decl | use-decl | package-decl | build-decl | declaration | binding
module-decl    ::= "module" name "."
use-decl       ::= "use" name [ "from" string-literal ] [ "as" name ] "."
package-decl   ::= "package" name word "."
build-decl     ::= "build" name { word } "."
word           ::= name | path | literal
declaration    ::= { modifier } ( type-decl | op-decl | extern-decl | actor-decl | trait-decl )
                 | contract-decl
                 | test-decl
modifier       ::= "export" | "extern" | "unsafe" [ "target" name ]
type-decl      ::= "def" ( "type" | "newtype" ) name type "."
                 | "def" "struct" name "do" { field | struct-attr } "end" "."
                 | "def" "enum" name "do" { variant } "end" "."
field          ::= name type "."
struct-attr    ::= ( "layout" name | "align" word | "mmio" number ) "."
variant        ::= name { name type } "."
op-decl        ::= ( "fn" | "proc" ) op-name { clause } block "."
extern-decl    ::= ( "fn" | "proc" ) op-name "do" { clause } "end" "."
op-name        ::= name | path
clause         ::= "input" [ "comptime" ] name type "."
                 | "using" name type "."
                 | "output" [ name ] type "."
                 | "effects" { name } "."
                 | ( "requires" | "ensures" ) [ grade ] term "."
                 | "errors" name [ term ] "."
                 | "tests" { name } "."
                 | "satisfies" name "."
                 | ( "access" | "parallel" ) name name "."
                 | "asm" name "." { asm-item "." }
                 | other-clause-word { word } "."
grade          ::= "assume" | "static" | "debug"
other-clause-word ::= "reduce" | "lowdoc" | "vector" | "priority" | "inplace" | "invalidates"
                 | "absorbs" | "reference" | "why" | "strlen" | "link" | "variadic" | "schedule"
asm-item       ::= ? `asm` 절의 항목 하나 — 낱말과 리터럴의 나열(⟦§6.9⟧) ?
actor-decl     ::= "actor" name "do" { state-decl | clause | op-decl } "end" "."
state-decl     ::= "state" "do" { field } "end" "."
trait-decl     ::= "trait" name "do" { signature } "end" "."
signature      ::= op-name { clause }
contract-decl  ::= "contract" name "do" { clause } "end" "."
test-decl      ::= "test" name [ "schedule" { word } "." ] block "."
```

(1) `extern-decl` 은 `modifier` 에 `"extern"` 이 있고 `"export"` 가 없는 선언의 꼴이다 — 몸이 씨(C)에 있으므로 블록에는
      절만 든다(⟦§6.9⟧). 그 밖의 `fn` · `proc` 은 `op-decl` 이다.

(2) `clause` 의 차례는 생성 규칙이 정하지 아니한다. 차례는 ⟦§6.4.1⟧ (3a) 가 정한다(`E-CLAUSE-ORDER`).

(2a) `package-decl` 과 `build-decl` 의 열쇠말과 값은 ⟦§5.7⟧ · ⟦§5.8⟧ · ⟦§6.4.7⟧ · ⟦§10.7⟧ 이 정한다.

(3) `struct-attr` · `variant` · `signature` 의 낱낱의 제약은 ⟦§6.2.7⟧ · ⟦§6.2.15⟧ · ⟦§6.2.19⟧ · ⟦§6.11⟧ 이 정한다.

(4) `signature` 는 op 의 이름 하나와 그 뒤의 절들이다. 절은 절 낱말로 시작하고 제 점으로 닫히므로, 절의 점 다음에 오는
      «절 낱말이 아닌 이름» 이 다음 서명의 이름이다.

(5) `requires` · `ensures` 의 `term` 이 계약 · 트레이트의 이름으로 시작하면 그것은 타입에 대한 술어다
      (`requires ordered t. . .` — ⟦§6.11⟧). 꼴은 여느 폼과 같다.

## A.10.3 문장

```구문: 전체 문법 — 문장
block          ::= "do" { statement } "end"
statement      ::= binding | guard-stmt | set-stmt | return-stmt | jump-stmt | expect-stmt
                 | if-statement | while-stmt | for-stmt | repeat-stmt | range-stmt | cycle-stmt
                 | match-stmt | region-stmt | borrow-stmt | task-group
                 | drop-stmt | op-decl | call-stmt
binding      ::= ( "let" | "var" ) name [ alloc-clause ] [ type ] term [ fail-clause ] "."
alloc-clause ::= ( "use" | "keep" ) name "."
fail-clause  ::= "else" leave "."
               | "else" block
               | "else" "error" name block
leave        ::= "return" [ term ] | "break" | "continue" | "panic" term
guard-stmt     ::= "guard" term "else" ( leave "." | block ) "."
set-stmt       ::= "set" term term "."
return-stmt    ::= "return" [ term ] "."
jump-stmt      ::= ( "break" | "continue" ) "."
expect-stmt    ::= "expect" term "."
drop-stmt      ::= "drop" name "." "."
call-stmt      ::= term
if-statement ::= "if" term block { "else" "if" term block } [ "else" block ] "."
while-stmt     ::= "while" term block "."
for-stmt    ::= "for"    name [ "mut" ] term [ filter ] block "."
repeat-stmt ::= "repeat" name type term [ filter ] block "."
range-stmt  ::= "range"  name type term term [ "step" term ] [ filter ] block "."
cycle-stmt  ::= "cycle"  name type term "while" term "next" term [ filter ] block "."
filter      ::= "if" term
match-stmt     ::= "match" [ "comptime" ] term "do" { case-arm | "else" block } "end" [ "else" block ] "."
case-arm       ::= "case" pattern block "."
pattern        ::= word { word } | word "to" word
region-stmt    ::= "region" name name block "."
borrow-stmt    ::= "borrow" name term block "."
task-group     ::= "task_group" [ name ] block "."
```

(1) 문장은 제 머리가 열고 **점 하나가 닫는다** — 블록으로 끝나는 문장도 그렇다(⟦§6.1.6⟧ (2b)). 점이 빠지면 번역이
      거부된다(`E-DOT-MISSING`). 닫힌 것 뒤에 점을 더 적어도 거부된다(`E-CLOSER-EXTRA`).

(2) `call-stmt` 는 `term` 그대로다 — 그 호출을 닫는 점이 곧 문장의 점이다(`print x. .` 에서 안쪽 점이 `x` 를, 바깥 점이
      `print` 를 닫는다).

(3) `set-stmt` 의 첫 `term` 은 값을 넣을 자리다 — 이름(`i.`)이거나 자리를 내는 폼(`idx a. i. .` · `field p. x .`)이다.

(4) `statement` 를 여는 낱말(`let` · `var` · `return` · `guard` …)은 식 안에 올 수 없다. `if` 는 문장이고 값이 아니다(`E-IF-VALUE`).

(5) 머리 없이 홀로 선 `block` 은 문장이 아니다(`E-BLOCK-NOHEAD`). `pattern` 안의 이름은 폼을 열지 아니한다.

(6) `op-decl` 이 블록 안에 서면 그것은 그 op 안에서만 보이는 op 이다(⟦§6.10⟧).

## A.10.4 식

```구문: 전체 문법 — 식
term           ::= literal | "true" | "false" | "none"
                 | call | paren-term
                 | struct-literal | fill-literal | list-literal
                 | island | pipe-form | word-form
call           ::= ( name | path ) { term } "."
paren-term     ::= "(" term ")"
struct-literal ::= "lit" type "do" { field-init } "end" "."
field-init     ::= name term "."
fill-literal   ::= "lit" "array" type word "do" { fill-item } "end" "."
fill-item      ::= ( word | "_" ) term "."
list-literal   ::= "lit" list-kind { term | "_" } "."
list-kind      ::= "array" type word | "slice" type | "vec" type word
island         ::= "expr" island-item { operator island-item } "."
island-item    ::= term | "(" island-item { operator island-item } ")"
pipe-form      ::= "pipe" term "do" { stage } "end" "."
stage          ::= stage-word { word | term } [ "with" term ] "."
stage-word     ::= ? 파이프 단계의 낱말 — 닫힌 어휘다(⟦§6.12⟧) ?
word-form      ::= "field" term segment { segment } "."
                 | "method" term { segment } name "." { term } "."
                 | "payload" term name name "."
                 | "send" term name { term } "."
                 | "spawn" "actor" path "."
                 | "spawn" "send" term name { term } "." "."
                 | "spawn" name { term } "."
                 | "try" term { name } "."
                 | "call_builtin" name { term } "."
                 | shaped-form
segment        ::= name | number
shaped-form    ::= ? 부록 D 의 표가 낱말의 자리를 정한 기본 연산 — `cast <타입> <항> .` · `isa <항> <갈래> .` · `size_of <타입> .` 따위 ?
```

(1) **식은 폼 하나다.** `call` 은 이름이 열고 점이 닫는다 — 인자가 없으면 그 이름의 값이다(`a.`). 폼 안의 폼은 먼저 제 점으로
      닫히므로 이 문법은 **선언을 읽지 않고** 식의 나무를 세운다. 인자의 수가 선언과 맞는지는 그 뒤에 본다(`E-IR-ARITY`).

(2) `paren-term` 은 폼 하나를 감쌀 뿐 뜻을 바꾸지 아니한다(⟦§6.1.5⟧ (3a)).

(3) 낱말의 자리를 가진 폼은 **닫힌 어휘**다: 문장의 머리, `word-form` 에 적은 낱말, 그리고 부록 D 의 표가 모양을 적은
      기본 연산(`shaped-form`). 그 밖의 `call` — 사용자가 선언한 op — 의 인자는 모두 `term` 이다. 타입을 넘길 때도 그렇다
      (`max_of u64. a. b. .`).

(4) `method` 의 op 이름은 **점으로 닫은 첫 이름**이다. 그 앞의 이름과 수는 마디이고 그 뒤는 인자다
      (`method o. inner area. w. 2 .`). `payload` 의 두 낱말은 갈래와 칸의 이름이다(⟦§6.2.19⟧).

(5) `island` 안의 `operator` 는 중위 연산자다(⟦§6.3.2⟧). 괄호는 섬의 한 토막을 묶는다.

(6) `stage` 의 낱말과 그 인자의 꼴은 ⟦§6.12⟧ 가 정한다.

## A.10.5 타입

```구문: 전체 문법 — 타입
type           ::= { type-qualifier } type-core
type-qualifier ::= "mut" | "owned" | "ref" | "mut_ref" | "unsafe_ptr"
type-core      ::= name | path
                 | "(" applied-type ")"
                 | "slice" type
                 | "array" type word
                 | "option" type
                 | "result" type type
                 | "vec" type word
                 | "segments" type
                 | "bitset" [ number ]
                 | "bits" number
                 | "cap" name
                 | "region" name
                 | "range" [ type ] word word
                 | other-type
applied-type   ::= type | ( name | path ) type { type }
other-type     ::= ? ⟦§6.2⟧ 가 정하는 그 밖의 타입 꼴(`mask` · `set` · `stack` · `fn` · `unsafe_fn` · `mmio`) ?
```

(1) `u8` · `bool` · `void` 같은 타입 낱말(A.9)과 저자가 지은 타입의 이름은 `name` 이다.

(2) 타입은 **글자만으로 끝난다** — 닫는 표시가 없다. 생성자는 닫힌 어휘이고(`slice u8` 은 낱말 둘, `result u64 perr` 는
      셋), 그 밖의 타입은 이름 하나다. 인자를 받는 사용자 타입과 인자 수가 정해지지 않은 꼴은 괄호로 싼다
      (`(box u64)` — ⟦§6.5.1⟧ (3b)).

(3) 타입 안의 이름과 수는 폼을 열지 아니한다 — 점이 없다(`array u8 n`).
