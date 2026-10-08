#import "../lib.typ": *

#clause("A", "부록 A — 문법 요약 (Annex A: Grammar summary)")[
  #para("1")[
    이 부록은 #cref("6") 이 정한 문법 가운데 #strong[자주 찾는 것을 모은 참고 자료];다.
      규범은 본문이며, 이 부록과 본문이 어긋나면 본문이 옳다.
  ]
  #para("1a")[
    다만 #strong[낱말 목록(A.1)만은 망라한다]; — 그 목록에 없는 것은 낱말이 아니다. 나머지 절은
      모양을 다 담지 아니한다.
  ]
  #note[
    ★ 완전한 형식 문법은 #strong[아직 없다.]; 여기 없는 모양이 있다고 해서 적합하지 않은 것이
    아니다 — 적합함을 정하는 것은 본문의 조항이다.
  ]
  #sub("A.1", "낱말")[
    #para("1")[
      다음이 낱말의 전부다. 이 목록에 없는 것은 낱말이 아니다.
    ]
    #shape("낱말 목록 — 이 목록이 전부다", "actor      be         break      case       continue   contract   def
do         drop       else       end        enum       expect     export
expr       extern     false      fn         for        guard      if
let        lit        match      module     newtype    none       proc
return     satisfies  send       set        spawn      state      struct
test       trait      true       try        type       unsafe     use
var        while")
    #para("2")[
      예약된 낱말은 이름이 될 수 없다(#cref("6.1.2")). 이 목록의 낱말은 모두 이 문서가
      다루며, 다루지 않는 채 예약만 해 둔 낱말은 #strong[없다];.
    ]
    #note[
      이 목록이 한 화면에 담기는 것은 우연이 아니라 규율이다(#cref("6.1.2")). 낱말을
      더하려면 그만한 값어치를 보여야 한다.
    ]
  ]
  #sub("A.2", "문장의 모양")[
    #para("1")[
      아래는 자주 쓰는 모양을 모은 것이다. `<…>` 는 채워 넣는 자리다.
    ]
    #shape("선언", "module <이름> .

use <모듈이름> .

def type <이름> <타입> .
def newtype <이름> <타입> .

def struct <이름> do
  <칸이름> <타입> .
end

def enum <이름> do
  <갈래이름> .
end

export fn <이름> input <이름> <타입> . output <타입> .
  requires <조건> .
  ensures <조건> .
do
  <문장들>
end

proc <이름> input <이름> <타입> . output <타입> . effects <효과들> .
do
  <문장들>
end")
  ]
  #sub("A.3", "문장")[
    #shape("문장", "let <이름> be <타입> <식> .
var <이름> be <타입> <식> .
set <이름> <식> .

if <조건> . do <문장들> end
if <조건> . do <문장들> else <문장들> end
while <조건> . do <문장들> end
for <이름> <머리> do <문장들> end
guard <조건> . else <빠져나가는 문장> .

return <식> .
break .
continue .")
    #para("1")[
      되풀이 둘은 다음과 같다. 표기는 A.6 (2) 를 따른다 — `{ }` 는 0 회 이상, `[ ]` 는 선택, `|` 는 택일이다.
    ]
    #shape("되풀이 — while 과 for", "WHILE  = 'while' , 식 , '.' , 'do' , { 문장 } , 'end' ;

FOR    = 'for' , 이름 , 머리 , [ 'where' , 식 , '.' ] , 'do' , { 문장 } , 'end' ;
머리   = 원천
       | 'mut' , 원천
       | 'count' , 정수타입 , 식 , '.'
       | 'range' , 정수타입 , 식 , 식 , [ 'step' , 식 ] , '.'
       | 'be' , 타입 , 식 , '.' , 'while' , 식 , '.' , 'next' , 식 , '.' ;
원천   = 이름
       | '(' , 식 , ')'
       | 식 , '.' ;

정수타입 = 'u8' | 'u16' | 'u32' | 'u64' | 'i8' | 'i16' | 'i32' | 'i64' ;")
    #para("2")[
      `WHILE` 의 `식` 과 `where` · 점화식 `while` 의 `식` 은 `bool` 이다. `원천` 은 슬라이스를 내는 식이다.
      각 머리의 뜻은 #cref("6.5.3.1") 이 정한다.
    ]
  ]
  #sub("A.4", "갈래·시험·액터")[
    #shape("갈래를 가르기 · 시험 · 액터", "match <값> . do
  case <갈래> . do <문장들> end
  case <갈래> . do <문장들> end
end

test <이름>
do
  expect <조건> .
end

actor <이름>
  state
    <칸이름> <타입> .
  end

  proc <이름> output <타입> . effects state .
  do
    <문장들>
  end
end")
  ]
  #sub("A.5", "식")[
    #shape("식", "rem 전위 — 우선순위가 없다
add <a> <b>          sub <a> <b>          mul <a> <b>
div <a> <b>          rem <a> <b>
eq <a> <b>           ne <a> <b>           lt <a> <b>
le <a> <b>           gt <a> <b>           ge <a> <b>
and <a> <b>          or <a> <b>           not <a>
len <슬라이스>        idx <슬라이스> <번호>
widen <타입> <값>     narrow <타입> <값>

rem 중위 — expr 섬 안에서만
expr <a> + <b> * <c>")
  ]
  #sub("A.6", "리터럴의 어휘 문법")[
    #para("1")[
      이 절은 #cref("6.1.4") 가 산문으로 규범한 리터럴을 #strong[생성 규칙으로]; 다시 적는다. 산문과
      이 규칙이 갈리면 산문이 이긴다 — 이 절은 요약이지 새 규범이 아니다.
    ]
    #para("2")[
      표기: `{ }` 는 0 회 이상, `[ ]` 는 선택, `|` 는 택일, `'…'` 는 그대로의 글자다.
      대문자 이름은 렉서가 내는 토큰이고, 소문자 이름은 그 토큰을 이루는 조각이다.
    ]
    #shape("수 리터럴", "NUMBER  = [ sign ] , ( float | hex | bin | dec ) ;
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
hexp    = ( 'p' | 'P' ) , [ sign ] , dec ;")
    #para("3")[
      #strong[부호는 붙여야 부호다.]; `-5` 는 리터럴 하나이고 `- 5` 는 아니다. `expr` 섬(#cref("6.3"))
      #strong[밖];에서 숫자에 붙은 부호는 값의 일부이며, 섬 #strong[안];에서 `-` 는 연산자다. 그러므로
      폼 자리의 `g 10 -3` 은 인자 #strong[둘];이지 뺄셈이 아니다. 그 갈림을 띄어쓰기가 아니라
      #strong[섬이]; 정한다.
    ]
    #para("4")[
      #strong[구분자는 자릿수 사이에만 온다.]; 위 규칙의 `[ sep ] , digit` 이 그 뜻이다 — 맨 앞
      (`_1`)·맨 뒤(`1_`)·진법 표시 바로 뒤(`0x_1`)에는 올 수 없다. 구분자는 모든 진법과
      부동소수의 #strong[모든 부분];(정수부·소수부·지수부)에서 쓸 수 있다.
    ]
    #para("5")[
      #strong[`float` 이 `dec` 보다 먼저 온다.]; 점도 지수도 없으면 `dec` 로 읽히고, 점이나 지수가
      있으면 `float` 이다. 점 #strong[앞뒤 모두]; 자릿수가 있어야 하므로(#cref("6.1.4") (2)) `1.` 은
      부동소수가 아니라 정수 `1` 뒤에 폼을 닫는 점이다.
    ]
    #para("6")[
      #strong[앞의 0 은 팔진이 아니다.]; `dec` 는 `0` 으로 시작할 수 있고 값은 십진이다.
    ]
    #shape("문자·문자열 리터럴", "CHAR    = [ prefix ] , \"'\" , ( charchar | escape ) , \"'\" ;
STRING  = [ prefix ] , '\"' , { strchar | escape } , '\"' ;

prefix  = 'u' | 'U' ;
charchar= ? \"'\" 도 '\\' 도 아닌 글자 하나(UTF-8) ? ;
strchar = ? '\"' 도 '\\' 도 아닌 바이트 하나 ? ;
escape  = ? ⟦§6.1.4⟧ 의 «이스케이프 — 닫힌 집합 열넷» 표에 있는 것 ? ;")
    #shape("텍스트 리터럴과 여러 줄 주석", "TEXTLIT = 'text' , sp , { sp } , tag , [ sp , { sp } , proc ] , { sp } , [ rem ] , nl ,
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
letter  = 'a'…'z' | 'A'…'Z' ;")
    #para("6a")[
      닫는 줄은 #strong[줄의 맨 첫 칸에서]; 여는 줄과 같은 `tag` 로 시작하고, 그 바로 뒤가 `letter` · `digit` · `'_'` 가
      아닌 줄이다. 그러므로 들여 쓴 `tag` 와 `tag` 보다 긴 낱말로 시작하는 줄은 `line` 이다. 위 규칙의 두 번째
      `tag` 는 첫 번째와 #strong[같은 글자열];이어야 한다 — 이 조건은 생성 규칙만으로는 적을 수 없어 여기 글로 적는다.
    ]
    #para("6b")[
      `TEXTLIT` 의 값은 `line` 들을 그 사이의 `nl` 과 함께 이은 바이트열이다. #strong[마지막 `line` 뒤의 `nl` 은 값에
      들지 아니한다.]; `line` 이 하나도 없으면 값은 빈 바이트열이다. 본문은 이스케이프를 풀지 아니한다(#cref("6.1.4") (29)).
    ]
    #para("6c")[
      닫는 줄에서 `tag` 와 `{ sp }` 뒤에 `nl` 도 `eof` 도 아닌 것이 오면 번역이 거부된다
      (`TEXTLIT` 은 `E-TEXTLIT-TAIL`, `NOTE` 는 `E-NOTE-TAIL`). 여는 줄에서 `tag` 뒤에 `proc` 도 `rem` 도 아닌 것이 오면
      거부된다(`E-TEXTLIT-OPENER`). `NOTE` 의 여는 줄은 `tag` 뒤를 줄 끝까지 읽지 아니한다.
      `note` 뒤에 `tag` 가 없으면 거부된다(`E-NOTE-TERM`) — `TEXTLIT` 은 같은 자리에서 `E-TEXTLIT-TERM` 이다.
    ]
    #para("7")[
      접두사는 #strong[닫힌 집합 둘];이다. 그 밖의 글자를 리터럴 앞에 붙이면 번역이 거부된다
      (`E-STR-PREFIX`). 접두사가 값의 원소를 무엇으로 볼지 정한다(#cref("6.1.4") (27)).
    ]
    #para("8")[
      문자 리터럴은 #strong[한 칸에 들어가야 한다.]; 접두사가 정한 원소 하나를 넘으면 거부되며
      (`E-CHAR-WIDTH`), 빈 것도 거부된다(`E-CHAR-EMPTY`).
    ]
    #note[
      이스케이프의 집합을 여기에 다시 적지 않은 것은 뜻이 있다. 같은 목록이 두 곳에 있으면
      둘은 반드시 갈리고, 갈린 뒤에는 어느 쪽이 규범인지 아무도 모른다. 집합은 #cref("6.1.4") 의
      표 한 곳에만 있다.
    ]
  ]
  #sub("A.7", "머리 낱말과 닫개")[
    #para("1")[
      폼은 #strong[머리]; 하나로 시작해 #strong[닫개]; 하나로 끝난다(#cref("6.1.6")). 다음 표가 머리마다
      어떤 모양을 갖고 무엇으로 닫는지를 모은 것이다.
    ]
    #para("2")[
      표기: `<…>` 는 채워 넣는 자리, `*` 는 0 회 이상, `[…]` 는 선택이다.
    ]
    #shape("머리 · 모양 · 닫개", "머리                모양                                              닫개
──────────────────  ────────────────────────────────────────────────  ──────
module              module <이름> .
use                 use <이름> from \"<경로>\" [as <별칭>] .
def type            def type <이름> <타입> .
def newtype         def newtype <이름> <타입> .
def struct          def struct <이름> do <칸>* end  (칸 = <이름> <타입> .) end
def enum            def enum <이름> do <갈래>* end  (갈래 = <이름> [<칸>*] .) end
trait               trait <이름> do <서명>* end (서명 = <이름> <절>*)     end
actor               actor <이름> do <state·절·op>* end                  end
state               state do <칸>* end        (actor 안)                 end
contract            contract <이름> do <절>* end                        end
fn / proc           [꾸밈]* fn <이름> <절>* do <폼>* end                end
test                test <이름> [schedule <절>] do <폼>* end           end
expect              expect <조건> .           (시험 블록의 단언)         .
input               input [comptime] <이름> <타입> .
using               using <이름> <타입> .     (op 이 깎아 쓰는 얼로케이터) .
output              output <타입> .   |   output <이름> <타입> .     (이름 붙은 결과 — 0 에서 시작하는 지역, §6.4.1 (3e))
effects             effects <원자>* .
access              access <이름> <모드> .
parallel            parallel <이름> <모드> .
requires / ensures  requires <조건-폼>* .
errors              errors <갈래> [<조건-폼>] .  (한 절에 오류 하나)     .
tests               tests <이름>* .
let / var           let <이름> [using <이름> [keep]] be <타입> <폼> .
set                 set <자리-폼> <폼> .
into                pop <스택> into <이름> .   (프렐류드 문형)           .
if (문)             if <폼> . do <폼>* end [else (if문 | do <폼>* end)]   end
guard               guard <폼> else <나가는-폼> .
while               while <폼> . do <폼>* end                            end
for                 for <이름> <머리> do <폼>* end   (머리는 A.3)       end
region              region <이름> <종류> do <폼>* end                   end
borrow              borrow <이름> be <폼> do <폼>* end                  end
return              return [<폼>] .           (값이 블록으로 끝나도 점)   .
break               break .                   (라벨은 없다)             .
continue            continue .
lit                 lit <타입> do <칸초기>* end  (칸초기 = <이름> <폼> .) end
lit (나열)          lit array <타입> <길이> <값>… [_] .  ·  lit slice <타입> <값>… .     .
lit (칸 채우기)     lit array <타입> <길이> do <번호> <값> . … [_ <값> .] end            블록이 닫는다
lit (SIMD)          lit vec <타입> <레인> <값>… [_] .                                 .
extern              extern fn/proc <이름> do <절>* end  (몸이 씨)       end
match               match <폼> . do <가지>* [else do <폼>* end] end       end")
    #para("2a")[
      `fn`/`proc` 머리의 `<절>*` 은 #cref("6.4.1") (3a) 의 한 차례를 따른다: `satisfies`·`lowdoc` · `vector`·`priority` ·
      comptime 입력 · 권한·영역 입력 · `using` · 데이터 입력 · `output` · `effects` · `link`·`variadic` · `asm` ·
      `asm`·`absorbs`·`reference`·`why` · `access`·`inplace`·`invalidates`·`parallel`·`reduce` · `requires` · `ensures` · `errors` · `tests` · `schedule` (`E-CLAUSE-ORDER`).
    ]
    #para("2b")[
      #strong[블록 선언];(`struct`·`enum`·`trait`·`actor`·`state`·`contract`)의 몸은 `do` 로 열고 `end` 로 닫는다 — `fn` 의 몸과 제어
      블록과 같은 한 규칙이다. `def struct <이름> .` 처럼 점으로 열거나 이름 뒤에서 줄만 바꾸는 꼴은 거부된다(`E-STMT-NODO`).
      개행은 닫개가 아니므로(#cref("6.1.6")) 줄바꿈으로는 머리가 닫히지 않는다. 서식기는 `do` 꼴로 옮겨 적는다.
    ]
    #para("3")[
      이 표는 #strong[머리와 닫개];를 규범한다. 모양 칸은 자주 쓰는 꼴을 적은 것이며, 정확한
      규범은 각 조항의 본문이다 — 둘이 갈리면 본문이 이긴다.
    ]
    #para("4")[
      이 목록에 없는 머리는 없다. 낱말 아닌 머리(`region`·`borrow`·`into`)는 프렐류드
      연산이며, 그것도 #cref("9") 가 정한 이름이지 새로 지을 수 있는 것이 아니다.
    ]
  ]
  #sub("A.8", "없앤 낱말")[
    #para("1")[
      다음은 한때 이 언어에 있었으나 #strong[없앤]; 낱말이다. 적으면 거부되며
      (`E-VOCAB-REMOVED`), 처리기는 그 자리에서 무엇을 대신 쓰는지 말한다.
    ]
    #tbl("없앤 낱말 — 그리고 대신 쓰는 것")[
      #table(columns: (auto, auto, 1fr), stroke: 0.5pt + rgb("#bbb"), inset: 6pt,
      [*없앤 것*], [*대신*], [*왜*],
      [`loop`], [`while true .`], [똑같은 뜻의 두 철자였다],
      [`give`], [`return`], [똑같은 뜻의 두 철자였다],
      [`unit`], [`void`], [한 뜻에 두 철자 — 명세와 도구가 다르게 불렀다],
      [`calcop`], [`fn`], [순수한 셈은 함수다. 그리고 «op» 은 이제 프렐류드 연산만 가리킨다],
      [`procop`], [`proc`], [효과를 내는 것은 수학의 함수가 아니다],
      [`is`], [(적지 않는다)], [장식이었다 — 아무도 읽지 않아 `def type h zzz u8 .` 이 통과했다],
      [`as`], [(적지 않는다)], [어디서도 무게가 없었다 — 블록 이름은 읽히고 #strong[버려졌다];],
      [`local`], [(적지 않는다)], [기본값이 이미 그것이다 — `export` 아닌 것은 밖에서 안 보인다],
      [`to`], [`field a b`], [중위 접근을 없앴다 — 한 뜻에 철자가 넷이었다],
      [`in`], [`field a b` · `idx a i`], [`to` 의 거꾸로 철자 — 셋째 철자였다],
      [`when`], [(적지 않는다)], [`errors` 절이 오류 하나만 받으므로 표식이 필요 없다],
      [`on`], [`proc`], [액터 블록 안이면 이미 메시지 처리기다. 게다가 #strong[순수/절차 비트를 우회했다];],
      [`fail`], [`return error <갈래>`], [명세의 어휘에 아예 없었다 — 옛 해석기에만 살아 있었다],
      [`;`], [`.`], [닫개의 #strong[세 번째 철자];였다(⟦§6.1.6⟧)],
      [`make`], [`lit`], [값 리터럴의 머리를 하나로 — 구조체·배열·벡터·슬라이스 값이 모두 `lit <타입> …` 로 시작한다],
      [`index`], [`idx`], [자주 쓰는 낱말을 짧게 — 읽기 `idx a 3` · 쓰기 `set (idx a 3) v .`],
      [`.이름` (머리 표시)], [맨 전위 · 괄호], [머리를 여는 셋째 철자였다 — 띄어쓰기 한 칸이 닫개를 머리로 바꿨다],
      [`not is_some x`], [`is_none x`], [같은 뜻의 두 철자였다(`E-NOT-IS-SOME`)],
      [`union` · `select` · `any` … (비트셋·레인)], [`bitset_union` · `lane_select` · `lane_any` …], [짧은 낱말을 저자에게 남긴다(⟦§6.3.3⟧ (1e))],
      )
    ]
    #para("2")[
      없앤 까닭은 대개 #strong[같은 뜻의 두 철자];이거나 #strong[아무것도 사지 못하는 낱말];이었기
      때문이다. 낱말은 목록에 오르는 값을 해야 하며, 하지 못하면 내려온다.
    ]
    #para("2a")[
      타입 선언의 옛 머리 — `def` 없이 `struct`·`enum`·`type`·`newtype` 으로 시작하는 선언 — 도 거부된다
      (`E-VOCAB-REMOVED`). 타입을 짓는 낱말은 `def` 하나다(#cref("6.1.2") (3)). 네 낱말은 `def` 뒤의 갈래 이름으로,
      그리고 `type` 은 타입 매개변수의 종류(`input comptime t type .`)로 남는다. `lowentc --fmt` 가 옛 모양을 옮겨 쓴다.
    ]
    #para("3")[
      `as` 와 `to` 는 #strong[자리 표식];으로만 남아 있다 — `use … as <별칭>` 과
      `case <아래> to <위>` 에서다. 그 자리 밖에서는 낱말이 아니다.
    ]
    #plain[
      없앤 낱말을 이 문서가 적어 두는 까닭. 옛 코드나 옛 글을 읽던 사람이 그 낱말을 만나면
      #emph["내가 뭘 잘못 쓴 건가"]; 를 묻게 된다. 여기에 적혀 있으면 그 물음이 한 줄로 끝난다 —
      #strong[없앴고, 대신 이것을 쓴다.];
    ]
  ]
  #sub("A.9", "타입 낱말")[
    #para("1")[
      타입을 적는 자리에 올 수 있는 낱말은 다음이 전부다. 여기 없는 이름은 저자가 지은
      이름(#cref("6.2.9"))이거나 아무것도 아니다.
    ]
    #shape("쓸 수 있는 타입 낱말", "rem 수
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
self")
    #para("2")[
      다음 낱말은 #strong[이름으로 받되 뜻이 아직 없다.]; 처리기는 그것을 말한다(`W-NOT-YET`,
      #cref("4.7")) — 조용히 받아 주지 아니한다.
    ]
    #shape("이름만 받는 타입 낱말", "byte   char   str   string   bytes_view   dyn   atomic
list   raw   addr
rng   clock   device   file_system   net   tty   process")
    #para("2a")[
      뒤의 넷(`list`·`raw`·`addr`·`rng`)은 이 판에서 이 갈래로 옮겼다. 위 (1) 의 목록에
      실려 있었으나 #strong[뜻을 정한 조항이 이 정본 어디에도 없었고];, 하강도 그 이름을 읽지
      못한다 — 그 서명의 op 은 통째로 해석기로 내려간다. 뜻을 지어내는 것보다 #strong[아직
      없다고 말하는 것];이 옳다.
    ]
    #para("2b")[
      `addr` 과 `raw` 는 #strong[연산으로는 뜻이 있다];(`addr <이름>` 은 자리를 얻고, 그것은
      `unsafe` 효과다 --- #cref("8.11")). 여기서 뜻이 없다고 하는 것은 #strong[타입을 적는 자리];의
      낱말이다. 마찬가지로 `cap rng` 의 `rng` 는 권능의 #strong[종류];이므로 이 조항에 들지
      아니한다 --- 홀로 타입으로 선 `rng` 만이 이름뿐이다. 그래서 위 (1) 의 권능 종류 줄에서
      `rng` 을 내렸다.
    ]
    #para("2c")[
      `rng` · `clock` · `device` · `file_system` · `net` · `tty` · `process` 일곱은 #strong[권능의 종류];이며,
      `cap <종류>` 자리에서만 뜻이 있다(#cref("7.2")). 홀로 타입으로 적으면 이 갈래에 든다 ---
      일곱이 같은 처지이므로 #strong[같이 말한다.]; 하나만 말하고 여섯이 조용하면, 읽는 사람은
      그 차이에 뜻이 있다고 여기게 된다.
    ]
    #para("3")[
      다음 셋은 #strong[공유 상태의 타입];이며 지금은 거절된다. 이름이 어휘에 있는 까닭은
      #emph["없는 타입"]; 이라 말하는 것이 거짓이기 때문이다 — 명세에 있는 타입을 두고
      #strong[네 프로그램이 틀렸다]; 고 말할 수는 없다.
    ]
    #shape("아직 거절되는 타입 낱말", "shared_read   lock   rwlock")
    #para("4")[
      `unsafe_ptr` 은 타입이 아니라 #strong[한정자];다. `mut`·`owned` 처럼 타입 앞에 붙으며,
      홀로 오면 거절된다.
    ]
    #para("5")[
      #strong[주소 공간 한정자의 자리는 예약되어 있다.]; 프로그램 메모리와 데이터 메모리가 나뉜 기계나 가속기처럼 공간마다
      포인터의 크기와 뜻이 다른 실행 환경(#cref("5.3") (4a))을 위해, 슬라이스와 참조 타입에는 «어느 주소 공간을 가리키는가» 를
      적는 한정자 자리를 둔다. 그 철자와 뜻은 그런 실행 환경을 대상으로 들일 때 정한다. 지금은 기본 공간 하나뿐이며 그
      자리에 오는 낱말은 없다.
    ]
    #plain[
      이 세 갈래를 갈라 적는 까닭. #emph["쓸 수 있다"]; 와 #emph["이름은 안다"]; 와 #emph["없다"]; 는 읽는
      사람에게 #strong[서로 다른 일];을 시킨다 — 쓰거나, 기다리거나, 다른 길을 찾거나. 셋을
      한 목록에 뭉뚱그리면 그 판단을 사람이 매번 도구를 돌려 보고 해야 한다.
    ]
  ]
]
