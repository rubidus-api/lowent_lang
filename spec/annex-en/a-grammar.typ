#import "../lib.typ": *

#clause("A", "Annex A — Grammar summary")[
  #para("1")[
    This annex is #strong[reference material that gathers the parts of the grammar]; specified by #cref("6") #strong[that are often looked up];.
      The norm is the main text; where this annex and the main text differ, the main text is right.
  ]
  #para("1a")[
    #strong[The keyword list (A.1) and the full grammar (A.10) are exhaustive]; — what is not in A.1 is not a keyword, and a sequence of words that the productions of A.10
      cannot read is not source of this language. A.2 to A.5 and A.7 gather commonly used shapes for easy viewing and
      do not contain every shape.
  ]
  #para("1b")[
    What A.10 specifies is #strong[form]; only (#cref("1.5") (4)). Which of the well-formed ones conform is decided by the clauses of the main
      text.
  ]
  #sub("A.1", "Keywords")[
    #para("1")[
      The following are all the keywords. What is not in this list is not a keyword.
    ]
    #shape("the keyword list — this list is all of it", "actor      break      case       continue   contract   cycle      do
drop       else       end        enum       expect     export     expr
extern     false      fn         for        guard      if         keep
let        lit        match      module     newtype    next       none
proc       range      repeat     return     satisfies  send       set
spawn      state      step       struct     test       trait      true
try        type       unsafe     use        var        while")
    #para("2")[
      Reserved keywords cannot be names (#cref("6.1.2")). Every keyword in this list is dealt with by this document,
      and there is #strong[no]; keyword that is only reserved without being dealt with.
    ]
    #note[
      That this list fits on one screen is not a coincidence but a discipline (#cref("6.1.2")). To add a
      keyword, it has to show it is worth that much.
    ]
  ]
  #sub("A.2", "The shapes of statements")[
    #para("1")[
      Below are commonly used shapes. `<…>` is a place to fill in. Declarations that have blocks are closed by the dot after `end` too (#cref("6.1.6") (2b)).
    ]
    #shape("declarations", "module <name> .

use <module name> .

type <name> <type> .
newtype <name> <type> .

struct <name> do
  <field name> <type> .
end .

enum <name> do
  <variant name> .
end .

export fn <name> input <name> <type> . output <type> .
  requires <condition> .
  ensures <condition> .
do
  <statements>
end .

proc <name> input <name> <type> . output <type> . effects <effects> .
do
  <statements>
end .")
  ]
  #sub("A.3", "Statements")[
    #shape("statements", "let <name> <type> <expr> .
var <name> <type> <expr> .
let <name> <type> <expr> else <leaving statement> .
set <place> <expr> .

if <condition> do <statements> end .
if <condition> do <statements> end else do <statements> end .
while <condition> do <statements> end .
for <name> <source> do <statements> end .
repeat <name> <type> <count> do <statements> end .
range <name> <type> <first> <last> do <statements> end .
cycle <name> <type> <first> while <condition> next <next value> do <statements> end .
guard <condition> else <leaving statement> .

return <expr> .
break [<number>] .
continue [<number>] .")
    #para("1")[
      The `<expr>` · `<condition>` · `<source>` · `<place>` of these shapes are #strong[forms including their own dot]; — `a.` for a variable, `add a. 1 .` for a call,
      `7` for a literal (#cref("6.1.5")). The dot at the end of the line closes that statement. Statements with blocks are closed by the dot after `end` too, and a chain continued with `else`
      has one dot at the end. A `<leaving statement>` has its own dot too (`guard gt a. 0 . else return 0 . .`). The productions of these statements are in A.10.3,
      and their meaning is specified by #cref("6.5.1") · #cref("6.5.3.1") · #cref("6.5.4").
    ]
  ]
  #sub("A.4", "Variants · tests · actors")[
    #shape("telling variants apart · tests · actors", "match <value> do
  case <variant> do <statements> end .
  case <variant> do <statements> end .
end .

test <name>
do
  expect <condition> .
end .

actor <name> do
  state do
    <field name> <type> .
  end .

  proc <name> output <type> . effects state .
  do
    <statements>
  end .
end .")
  ]
  #sub("A.5", "Expressions")[
    #shape("expressions", "rem prefix — a name opens, a dot closes. There is no precedence
add <a> <b> .        sub <a> <b> .        mul <a> <b> .
div <a> <b> .        mod <a> <b> .
eq <a> <b> .         ne <a> <b> .         lt <a> <b> .
le <a> <b> .         gt <a> <b> .         ge <a> <b> .
and <a> <b> .        or <a> <b> .         not <a> .
len <slice> .        idx <slice> <index> .
widen <type> <value> .   narrow <type> <value> .

rem variables — a dot is attached to the name. Literals and true · false · none have no dot
<name>.

rem infix — only inside an expr island. The island is closed by its own dot too
expr <a> + <b> * <c> .")
  ]
  #sub("A.6", "The lexical grammar of literals")[
    #para("1")[
      This section writes again #strong[as productions]; the literals #cref("6.1.4") specified in prose. If the prose and
      these rules differ, the prose wins — this section is a summary, not a new norm.
    ]
    #para("2")[
      Notation: `{ }` is zero or more, `[ ]` optional, `|` alternatives, and `'…'` literal characters.
      Upper-case names are tokens the lexer produces, and lower-case names are the pieces that make up those tokens.
    ]
    #shape("number literals", "NUMBER  = [ sign ] , ( float | hex | bin | dec ) ;
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
      #strong[A sign is a sign only when attached.]; `-5` is one literal, and `- 5` is not. #strong[Outside]; an `expr` island (#cref("6.3")),
      a sign attached to a number is part of the value; #strong[inside]; an island, `-` is an operator. So
      `g 10 -3` in a form position is #strong[two]; arguments, not a subtraction. What decides that split is not spacing but
      #strong[the island];.
    ]
    #para("4")[
      #strong[Separators come only between digits.]; That is what `[ sep ] , digit` in the rules above means — they cannot come at the very front
      (`_1`) · the very end (`1_`) · directly after a radix mark (`0x_1`). Separators can be used in every radix and
      in #strong[every part]; of a floating-point number (integer part · fraction part · exponent part).
    ]
    #para("5")[
      #strong[`float` comes before `dec`.]; Without a point or an exponent it is read as `dec`; with a point or an exponent
      it is `float`. There shall be digits #strong[both before and after]; the point (#cref("6.1.4") (2)), so `1.` is
      not a floating-point number but the integer `1` followed by a dot that closes a form.
    ]
    #para("6")[
      #strong[A leading 0 is not octal.]; `dec` may begin with `0`, and its value is decimal.
    ]
    #shape("character · string literals", "CHAR    = [ prefix ] , \"'\" , ( charchar | escape ) , \"'\" ;
STRING  = [ prefix ] , '\"' , { strchar | escape } , '\"'
        | 'r' , '\"' , { rawchar } , '\"' ;   (* raw string — no escapes, §6.1 (13a) *)

prefix  = 'u' | 'U' ;
charchar= ? one character (UTF-8) that is neither \"'\" nor '\\' ? ;
strchar = ? one byte that is neither '\"' nor '\\' ? ;
rawchar = ? one byte that is neither '\"' nor a line feed ? ;
escape  = ? one in the table «Escapes — a closed set of fourteen» of ⟦§6.1.4⟧ ? ;")
    #shape("text literals and multi-line comments", "TEXTLIT = 'text' , sp , { sp } , tag , [ sp , { sp } , proc ] , { sp } , [ rem ] , nl ,
          { line , nl } ,
          tag , { sp } , ( nl | eof ) ;
NOTE    = 'note' , sp , { sp } , tag , { linech } , nl ,
          { line , nl } ,
          tag , { sp } , ( nl | eof ) ;

tag     = ( letter | '_' ) , { letter | digit | '_' } ;
proc    = 'u' | 'U' ;
rem     = 'rem' , { linech } ;
line    = ? a line that is not the closing line ? ;
linech  = ? a byte that is not a line feed ? ;
sp      = ' ' | tab | CR ;
nl      = LF ;
eof     = ? the end of the file ? ;
letter  = 'a'…'z' | 'A'…'Z' ;")
    #para("6a")[
      The closing line is a line that begins, #strong[in its very first column];, with the same `tag` as the opening line, immediately followed by something that is not
      `letter` · `digit` · `'_'`. So lines that begin with an indented `tag`, or with a word longer than `tag`, are `line`s. The second
      `tag` of the rules above shall be #strong[the same characters]; as the first — this condition cannot be written by productions alone, so it is written here in words.
    ]
    #para("6b")[
      The value of a `TEXTLIT` is the bytes of the `line`s joined with the `nl`s between them. #strong[The `nl` after the last `line` is not part of
      the value.]; If there are no `line`s, the value is an empty byte string. Escapes in the body are not processed (#cref("6.1.4") (29)).
    ]
    #para("6c")[
      If, on the closing line, something that is neither `nl` nor `eof` comes after `tag` and `{ sp }`, translation is refused
      (`E-TEXTLIT-TAIL` for `TEXTLIT`, `E-NOTE-TAIL` for `NOTE`). If, on the opening line, something that is neither `proc` nor `rem` comes after `tag`, it is
      refused (`E-TEXTLIT-OPENER`). The opening line of a `NOTE` does not read what follows `tag` to the end of the line.
      If there is no `tag` after `note`, it is refused (`E-NOTE-TERM`) — at the same place a `TEXTLIT` gives `E-TEXTLIT-TERM`.
    ]
    #para("7")[
      The prefixes are #strong[a closed set of two];. Attaching any other character in front of a literal is refused at translation
      (`E-STR-PREFIX`). The prefix decides what the elements of the value are taken to be (#cref("6.1.4") (27)).
    ]
    #para("8")[
      A character literal #strong[shall fit in one slot.]; If it exceeds one element as decided by the prefix, it is refused
      (`E-CHAR-WIDTH`), and an empty one is refused too (`E-CHAR-EMPTY`).
    ]
    #note[
      Not writing the set of escapes again here is deliberate. If the same list were in two places,
      the two would certainly part, and once parted nobody would know which is normative. The set is only in the one table of
      #cref("6.1.4").
    ]
  ]
  #sub("A.7", "Head words and closers")[
    #para("1")[
      A form is opened by one #strong[name]; and closed by one #strong[closer]; — a dot (#cref("6.1.5") · #cref("6.1.6")). The following table gathers the shape
      of each head word. The closer is a dot on every line — forms with blocks end with `end .`. So the closer column is not filled in separately; the dot written at the end of the shape is it.
    ]
    #para("2")[
      Notation: `<…>` is a place to fill in, `*` zero or more, `[…]` optional.
    ]
    #shape("head · shape · closer", "head                shape                                             closer
──────────────────  ────────────────────────────────────────────────  ──────
module              module <name> .
use                 use <name> from \"<path>\" [as <alias>] .
type            type <name> <type> .
newtype         newtype <name> <type> .
struct          struct <name> do <field>* end .  (field = <name> <type> .)
enum            enum <name> do <variant>* end .  (variant = <name> [<field>*] .)
trait               trait <name> do <signature>* end . (signature = <name> <clause>*)
actor               actor <name> do <state·clause·op>* end .
state               state do <field>* end .        (inside an actor)
contract            contract <name> do <clause>* end .
fn / proc           [modifier]* fn <name> <clause>* do <form>* end .
test                test <name> [schedule <clause>] do <form>* end .
expect              expect <condition> .           (the assertion of a test block)
input               input [comptime] <name> <type> .
use (op head)       use <name> <type> .     (the allocator the op carves from)
output              output <type> .   |   output <name> <type> .     (a named result — a local starting at 0, §6.4.1 (3e))
effects             effects <atom>* .
access              access <name> <mode> .
parallel            parallel <name> <mode> .
requires / ensures  requires <condition-form>* .
errors              errors <variant> [<condition-form>] .  (one error per clause)
tests               tests <name>* .
let / var           let <name> [use <name>. | keep <name>.] <type> <form> [else …] .   (a dot even when else ends in a block)
set                 set <place-form> <form> .
into                pop <stack> into <place> .   (a prelude statement form)
if (statement)      if <form> do <form>* end [else if <form> do <form>* end]* [else do <form>* end] .
guard               guard <form> else <leaving-form> . .   (the leaving form's dot, and guard's dot)
while               while <form> do <form>* end .
for                 for <name> [mut] <form> [if <form>] do <form>* end .
repeat              repeat <name> <type> <form> [if <form>] do <form>* end .
range               range <name> <type> <form> <form> [step <form>] [if <form>] do <form>* end .
cycle               cycle <name> <type> <form> while <form> next <form> [if <form>] do <form>* end .
region              region <name> <kind> do <form>* end .
borrow              borrow <name> <form> do <form>* end .
return              return [<form>] .
break               break [<number>] .            (number = the depth of loops to leave · there are no labels)
continue            continue [<number>] .
lit                 lit <type> do <field-init>* end .  (field-init = <name> <form> .)
lit (list)          lit array <length> <type> <value>… [_] .  ·  lit slice <type> <value>… .
lit (matrix)        lit matrix <dimensions> <size>… <type> <value>… [_] .
lit (slot filling)  lit array <length> <type> do <index> <value> . … [_ <value> .] end .
lit (SIMD)          lit vec <lanes> <type> <value>… [_] .
extern              extern fn/proc <name> do <clause>* end .  (body in C)
match               match <form> do <branch>* end [else do <form>* end] .   (branch = case <pattern> do <form>* end .)")
    #para("2a")[
      The `<clause>*` of an `fn` / `proc` head follows the one order of #cref("6.4.1") (3a): `satisfies` · `lowdoc` · `vector` · `priority` ·
      comptime inputs · capability and region inputs · `use` · data inputs · `output` · `effects` · `link` · `variadic` · `asm` ·
      `asm` · `absorbs` · `reference` · `why` · `access` · `inplace` · `invalidates` · `parallel` · `reduce` · `requires` · `ensures` · `errors` · `tests` · `schedule` (`E-CLAUSE-ORDER`).
    ]
    #para("2b")[
      The body of a #strong[block declaration]; (`struct` · `enum` · `trait` · `actor` · `state` · `contract`) is opened with `do` and closed with `end` — the same one rule as the body of an `fn`
      and control blocks. Shapes that open with a dot like `struct <name> .`, or only break the line after the name, are refused (`E-STMT-NODO`).
      A newline is not a closer (#cref("6.1.6")), so a line break does not close a head.
    ]
    #para("3")[
      This table regulates #strong[heads and their shapes];. The shape column writes commonly used forms, and the exact
      norm is the main text of each clause — where the two differ, the main text wins.
    ]
    #para("4")[
      There is no head outside this list. Heads that are not keywords (`region` · `borrow` · `into`) are prelude
      operations, and they too are names fixed by #cref("9"), not something that can be newly made.
    ]
  ]
  #sub("A.8", "Removed words")[
    #para("1")[
      The following are words that were once in this language but were #strong[removed];. Writing them is refused
      (`E-VOCAB-REMOVED`), and the processor says on the spot what to use instead.
    ]
    #tbl("Removed words — and what to use instead")[
      #table(columns: (22%, 24%, 1fr), stroke: 0.5pt + rgb("#bbb"), inset: 6pt,
      [*Removed*], [*Instead*], [*Why*],
      [`loop`], [`while true do`], [it was a second spelling of exactly the same meaning],
      [`give`], [`return`], [it was a second spelling of exactly the same meaning],
      [`unit`], [`void`], [two spellings for one meaning — the specification and the tool called it differently],
      [`calcop`], [`fn`], [a pure computation is a function. And «op» now refers only to prelude operations],
      [`procop`], [`proc`], [what produces effects is not a mathematical function],
      [`is`], [(not written)], [it was decoration — nobody read it, so `type h zzz u8 .` passed],
      [`as`], [(not written)], [it had no weight anywhere — block names were read and #strong[discarded];],
      [`local`], [(not written)], [it is already the default — what is not `export` is not visible from outside],
      [`to`], [`field a. b .`], [infix access was removed — one meaning had four spellings],
      [`in`], [`field a. b .` · `idx a. i. .`], [the reverse spelling of `to` — it was a third spelling],
      [`when`], [(not written)], [an `errors` clause takes only one error, so no marker is needed],
      [`on`], [`proc`], [inside an actor block it is already a message handler. Besides, #strong[it bypassed the pure/procedural bit];],
      [`fail`], [`return error <variant>`], [it was not in the specification's vocabulary at all — it lived only in the old interpreter],
      [`;`], [`.`], [it was #strong[the third spelling]; of the closer (⟦§6.1.6⟧)],
      [`make`], [`lit`], [one head for value literals — struct · array · vector · slice values all begin with `lit <type> …`],
      [`index`], [`idx`], [a frequently used word made short — reading `idx a. 3 .` · writing `set idx a. 3 . v. .`],
      [`be`], [(not written)], [the end of a type is decided by the number of arguments of the type grammar — it carried nothing between the name and the type. The refusal code is `E-LET-BE` (⟦§6.5.1⟧ (3a))],
      [`.name` (head mark)], [bare prefix · parentheses], [it was a third spelling for opening a head — one space turned a closer into a head],
      [`not is_some x. . .`], [`is_none x. .`], [two spellings of the same meaning (`E-NOT-IS-SOME`)],
      [`union` · `select` · `any` … (bitsets · lanes)], [`bitset_union` · `lane_select` · `lane_any` …], [short words are left to the author (⟦§6.3.3⟧ (1e))],
      )
    ]
    #para("2")[
      The reasons for removal were mostly #strong[two spellings of the same meaning]; or #strong[a word that bought nothing];.
      A word has to be worth its place on the list, and if it is not, it comes off.
    ]
    #para("2a")[
      The old heads of type declarations — `def struct` · `def enum` · `def type` · `def newtype` — are refused too (`E-DEF-OLD`). Type declarations
      begin with their own keyword (#cref("6.1.2") (3)). `def` is not a keyword, so in places not of that shape it is an author's name. `type` is
      also used as the kind of a type parameter (`input comptime t type .`).
    ]
    #para("3")[
      `as` and `to` remain only as #strong[position markers]; — in `use … as <alias>` and
      `case <low> to <high>`. Outside those positions they are not words.
    ]
    #plain[
      Why this document records removed words. Someone who used to read old code or old writing, on meeting such a word,
      asks #emph["did I write something wrong"];. With it written here, that question ends in one line —
      #strong[it was removed, and this is used instead.];
    ]
  ]
  #sub("A.9", "Type words")[
    #para("1")[
      The words that can come in a place that writes a type are the following and no more. Names not here are names the author made
      (#cref("6.2.9")) or nothing.
    ]
    #shape("type words that can be used", "rem numbers
u8    i8    u16   i16   u32   i32   u64   i64   usize  idiff   f32   f64
u128  i128
bool  void

rem rows and aggregates
slice   array   matrix   segments   set   stack   range   vec   bitset   mask

rem what holds an answer
result   option

rem flows and channels (⟦§10.3⟧ · ⟦§10.6⟧)
task   chan

rem places and borrows
ref   mut_ref   mut   owned   region

rem a window over bytes (⟦§6.2.13⟧)
view

rem what connects to the outside
cap   mmio
fn    unsafe_fn

rem others
self")
    #para("2")[
      The following words #strong[are accepted as names but have no meaning yet.]; The processor says so (`W-NOT-YET`,
      #cref("4.7")) — it does not accept them silently.
    ]
    #shape("type words accepted only as names", "byte   char   str   string   bytes_view   dyn   atomic
rng   clock   device   file_system   net   tty   process")
    #para("2a")[
      Four (`list` · `raw` · `addr` · `rng`) were moved into this group in the edition of 2026-09-17. They were listed in (1) above,
      but #strong[no clause anywhere in this canon fixed their meaning];, and lowering cannot read those names
      either — ops with such signatures go down to the interpreter wholesale. Rather than make up a meaning, #strong[saying that it does not exist
      yet]; is right. Of those, `list` got its meaning from #cref("6.2.25") on 2026-11-12 and moved to (3) below.
    ]
    #para("2b")[
      `addr` and `raw` are words whose meaning #cref("6.9.4") decides but which are #strong[not built yet]; (`addr <name>` obtains a place, and that is
      the `unsafe` effect). Since 2026-11-12 the two in a type position are refused, not warned (`E-UNSAFE-UNBUILT`).
      And the `rng` of `cap rng` is a #strong[kind]; of capability, so it is not covered by this clause
      --- only `rng` standing alone as a type is a mere name. That is why `rng` was taken down from the capability-kind row
      of (1) above.
    ]
    #para("2c")[
      The seven `rng` · `clock` · `device` · `file_system` · `net` · `tty` · `process` are #strong[kinds of capability];,
      and they have meaning only in the position `cap <kind>` (#cref("7.2")). Written alone as a type they fall into this group ---
      the seven are in the same situation, so #strong[they are spoken of together.]; If one were spoken of while six stayed silent, readers would
      take that difference to mean something.
    ]
    #para("3")[
      The following three are #strong[shared-state types]; and are refused for now. Their names are in the vocabulary because
      saying #emph["no such type"]; would be false — one cannot say #strong[your program is wrong]; about a type that is in the specification.
    ]
    #shape("type words still refused", "shared_read   lock   rwlock
raw   addr
c8   c16   c32   c64
b8   b16   b32   b64   b128
list")
    #para("3a")[
      The last two (`raw` · `addr`) are words whose meaning #cref("6.9.4") decides but which are not built yet (`E-UNSAFE-UNBUILT`) — they are in the
      vocabulary for the same reason as the shared-state types.
    ]
    #para("3b")[
      The ten after them (`c8` … `c64` · `b8` … `b128` · `list`) are words whose meaning #cref("6.2.25") decides but which are not built yet
      (`E-TYPE-UNBUILT`) — they are in the vocabulary for the same reason. If the author declares a type of that name, the declaration stands.
    ]
    #para("4")[
      `unsafe_ptr` is not a type but #strong[a qualifier];. It attaches in front of a type like `mut` · `owned`,
      and coming alone it is refused.
    ]
    #para("5")[
      #strong[A place for address-space qualifiers is reserved.]; For execution environments in which pointer size and meaning differ by space
      (#cref("5.3") (4a)), such as machines with separate program and data memory or accelerators, slice and reference types have a qualifier position
      that writes «which address space it points at». Its spelling and meaning will be fixed when such an execution environment is brought in as a target. For now there is only
      one default space, and no word comes in that position.
    ]
    #plain[
      Why these three groups are written separately. #emph["Can be used"];, #emph["the name is known"]; and #emph["does not exist"]; have the reader
      do #strong[different things]; — use it, wait, or find another way. Lumping the three
      into one list would make people decide that by running the tool every time.
    ]
  ]
  #sub("A.10", "The full grammar")[
    #para("1")[
      This section writes the grammar of one source file #strong[from beginning to end]; as productions. The notation is that of #cref("1.5").
      If a clause of the main text has a rule of the same name, the one in this section is #strong[the same down to the characters];.
    ]
    #para("2")[
      The grammar has two layers (#cref("1.5") (3a)). A.10.1 divides characters into tokens, and from A.10.2 on, sequences of tokens are read.
      White space and comments are discarded in A.10.1 and do not appear after it.
    ]
    #para("3")[
      The shape of a source stands on the following #strong[four rules];. The productions of this section spell those four out.
    ]
    #tbl("The four rules that stand the shape up")[
      #table(columns: (auto, 1fr), stroke: 0.5pt + rgb("#bbb"), inset: 6pt,
      [*Rule*], [*Content*],
      [head], [every statement and declaration begins with one head word. The head decides the shape that follows],
      [block], [`do` pairs with `end`. `end` closes only its own `do`],
      [names and dots], [a name in a use position opens a form, and one dot closes the innermost open form. A variable is a form closed at once (`a.`). Statements · declarations · clauses · fields too are opened by their heads and closed by dots],
      [word positions], [a name standing in a position that makes a name · the position of a type · a word position fixed by the grammar and the tables of annex D does not open a form (it has no dot)],
      )
    ]
    #para("4")[
      All four rules are decided #strong[by the characters alone]; — the tree stands without reading declarations. How many arguments an op takes is not material
      for standing the shape up but a check after the tree stands (#cref("6.1.5") (3)). The «dot» rule has no exceptions.
    ]
  ]
  #sub("A.10.1", "The lexical grammar")[
    #syntax("the full grammar — lexical", "source    ::= { separator | token }
separator ::= whitespace | line-comment | block-comment
token     ::= keyword | name | path | literal | punctuation

punctuation ::= \".\" | \"(\" | \")\" | \",\" | operator
operator    ::= \"+\" | \"-\" | \"*\" | \"/\"

whitespace      ::= \" \" | tab | carriage-return | newline
blank           ::= \" \" | tab | carriage-return
tab             ::= ? U+0009 ?
carriage-return ::= ? U+000D ?
newline         ::= ? U+000A ?
line-comment    ::= \"rem\" { line-char }
block-comment   ::= \"note\" blank { blank } tag { line-char } newline
                    { comment-line newline }
                    tag { blank } ( newline | end-of-source )
tag             ::= name
line-char       ::= ? a character that is not newline ?
comment-line    ::= ? a line that is not the closing line ?
end-of-source   ::= ? the end of the source ?

keyword ::= \"actor\" | \"break\" | \"case\" | \"continue\" | \"contract\" | \"cycle\" | \"do\"
          | \"drop\" | \"else\" | \"end\" | \"enum\" | \"expect\" | \"export\" | \"expr\"
          | \"extern\" | \"false\" | \"fn\" | \"for\" | \"guard\" | \"if\" | \"keep\"
          | \"let\" | \"lit\" | \"match\" | \"module\" | \"newtype\" | \"next\" | \"none\"
          | \"proc\" | \"range\" | \"repeat\" | \"return\" | \"satisfies\" | \"send\" | \"set\"
          | \"spawn\" | \"state\" | \"step\" | \"struct\" | \"test\" | \"trait\" | \"true\"
          | \"try\" | \"type\" | \"unsafe\" | \"use\" | \"var\" | \"while\"

name       ::= name-start { name-char }
name-start ::= \"a\" … \"z\" | \"A\" … \"Z\" | \"_\"
name-char  ::= name-start | \"0\" … \"9\"
path       ::= name \".\" path-part { \".\" path-part }
path-part  ::= name-char { name-char }

literal        ::= number | char-literal | string-literal | text-literal

number         ::= [ sign ] ( float | hex | bin | dec )
sign           ::= \"+\" | \"-\"
digit          ::= \"0\" … \"9\"
hex-digit      ::= digit | \"a\" … \"f\" | \"A\" … \"F\"
bin-digit      ::= \"0\" | \"1\"
dec            ::= digit { [ \"_\" ] digit }
hex            ::= \"0\" ( \"x\" | \"X\" ) hex-digit { [ \"_\" ] hex-digit }
bin            ::= \"0\" ( \"b\" | \"B\" ) bin-digit { [ \"_\" ] bin-digit }
float          ::= dec \".\" dec [ dec-exponent ]
                 | dec dec-exponent
                 | hex [ \".\" hex-digit { [ \"_\" ] hex-digit } ] hex-exponent
dec-exponent   ::= ( \"e\" | \"E\" ) [ sign ] dec
hex-exponent   ::= ( \"p\" | \"P\" ) [ sign ] dec

char-literal   ::= [ prefix ] single-quote ( char-char | escape ) single-quote
string-literal ::= [ prefix ] double-quote { string-char | escape } double-quote
                 | raw-string
raw-string     ::= \"r\" double-quote { raw-char } double-quote
raw-char       ::= ? one byte that is neither a double quote nor a line feed ?
prefix         ::= \"u\" | \"U\"
escape         ::= backslash ( simple-escape
                             | \"x\" hex-digit hex-digit
                             | \"u\" hex-digit hex-digit hex-digit hex-digit
                             | \"U\" hex-digit hex-digit hex-digit hex-digit hex-digit hex-digit hex-digit hex-digit )
simple-escape  ::= backslash | double-quote | single-quote
                 | \"a\" | \"b\" | \"f\" | \"n\" | \"r\" | \"t\" | \"v\" | \"0\"
single-quote   ::= ? U+0027 ?
double-quote   ::= ? U+0022 ?
backslash      ::= ? U+005C ?
char-char      ::= ? one character that is not a single quote, a backslash or a line feed ?
string-char    ::= ? one byte that is not a double quote, a backslash or a line feed ?

text-literal   ::= \"text\" blank { blank } tag [ blank { blank } processor ] { blank } [ line-comment ] newline
                   { text-line newline }
                   tag { blank } ( newline | end-of-source )
processor      ::= \"u\" | \"U\"
text-line      ::= ? a line that is not the closing line ?")
    #para("1")[
      No `keyword` token begins with `rem` · `note` · `text` — those three are not keywords but the character sequences that open comments and text literals
      (#cref("6.1.1") · #cref("6.1.4")).
    ]
    #para("2")[
      If character sequences not in `keyword`, such as `"from"` · `"input"` · `"slice"`, appear as terminal symbols in the syntactic grammar below, they are `name` tokens that have meaning #strong[only
      in that place]; (context words). Outside that place they are ordinary names.
    ]
  ]
  #sub("A.10.2", "Translation units and declarations")[
    #syntax("the full grammar — declarations", "unit           ::= { top-form }
top-form       ::= module-decl | use-decl | package-decl | build-decl | declaration | binding
module-decl    ::= \"module\" name \".\"
use-decl       ::= \"use\" name [ \"from\" string-literal ] [ \"as\" name ] \".\"
package-decl   ::= \"package\" name word \".\"
build-decl     ::= \"build\" name { word } \".\"
word           ::= name | path | literal
declaration    ::= { modifier } ( type-decl | op-decl | extern-decl | actor-decl | trait-decl )
                 | contract-decl
                 | test-decl
modifier       ::= \"export\" | \"extern\" | \"unsafe\" [ \"target\" name ]
type-decl      ::= ( \"type\" | \"newtype\" ) name type \".\"
                 | \"struct\" name \"do\" { field | struct-attr } \"end\" \".\"
                 | \"enum\" name \"do\" { variant } \"end\" \".\"
field          ::= name type \".\"
struct-attr    ::= ( \"layout\" name | \"align\" word | \"mmio\" number ) \".\"
variant        ::= name { name type } \".\"
op-decl        ::= ( \"fn\" | \"proc\" ) op-name { clause } block \".\"
extern-decl    ::= ( \"fn\" | \"proc\" ) op-name \"do\" { clause } \"end\" \".\"
op-name        ::= name | path
clause         ::= \"input\" [ \"comptime\" ] name type \".\"
                 | \"use\" name type \".\"
                 | \"output\" [ name ] type \".\"
                 | \"effects\" { name } \".\"
                 | ( \"requires\" | \"ensures\" ) [ grade ] term \".\"
                 | \"errors\" name [ term ] \".\"
                 | \"tests\" { name } \".\"
                 | \"satisfies\" name \".\"
                 | ( \"access\" | \"parallel\" ) name name \".\"
                 | \"asm\" name \".\" { asm-item \".\" }
                 | other-clause-word { word } \".\"
grade          ::= \"assume\" | \"static\" | \"debug\"
other-clause-word ::= \"reduce\" | \"lowdoc\" | \"vector\" | \"priority\" | \"inplace\" | \"invalidates\"
                 | \"absorbs\" | \"reference\" | \"why\" | \"strlen\" | \"link\" | \"variadic\" | \"schedule\"
asm-item       ::= ? one item of an `asm` clause — a sequence of words and literals (⟦§6.9⟧) ?
actor-decl     ::= \"actor\" name \"do\" { state-decl | clause | op-decl } \"end\" \".\"
state-decl     ::= \"state\" \"do\" { field } \"end\" \".\"
trait-decl     ::= \"trait\" name \"do\" { signature } \"end\" \".\"
signature      ::= op-name { clause }
contract-decl  ::= \"contract\" name \"do\" { clause } \"end\" \".\"
test-decl      ::= \"test\" name [ \"schedule\" { word } \".\" ] block \".\"")
    #para("1")[
      `extern-decl` is the shape of a declaration whose `modifier` has `"extern"` and not `"export"` — its body is in C, so the block holds
      only clauses (#cref("6.9")). Other `fn` · `proc` are `op-decl`.
    ]
    #para("2")[
      The order of `clause`s is not fixed by the productions. The order is fixed by #cref("6.4.1") (3a) (`E-CLAUSE-ORDER`).
    ]
    #para("2a")[
      The keys and values of `package-decl` and `build-decl` are specified by #cref("5.7") · #cref("5.8") · #cref("6.4.7") · #cref("10.7").
    ]
    #para("3")[
      The individual constraints on `struct-attr` · `variant` · `signature` are specified by #cref("6.2.7") · #cref("6.2.15") · #cref("6.2.19") · #cref("6.11").
    ]
    #para("4")[
      A `signature` is one op name and the clauses after it. A clause begins with a clause word and is closed by its own dot, so
      «a name that is not a clause word» coming after a clause's dot is the name of the next signature.
    ]
    #para("5")[
      If the `term` of `requires` · `ensures` begins with the name of a contract · trait, it is a predicate on a type
      (`requires ordered t. . .` — #cref("6.11")). Its shape is the same as any form.
    ]
  ]
  #sub("A.10.3", "Statements")[
    #syntax("the full grammar — statements", "block          ::= \"do\" { statement } \"end\"
statement      ::= binding | guard-stmt | set-stmt | return-stmt | jump-stmt | expect-stmt
                 | if-statement | while-stmt | for-stmt | repeat-stmt | range-stmt | cycle-stmt
                 | match-stmt | region-stmt | borrow-stmt | task-group
                 | drop-stmt | op-decl | call-stmt
binding      ::= ( \"let\" | \"var\" ) name [ alloc-clause ] [ type ] term [ fail-clause ] \".\"
alloc-clause ::= ( \"use\" | \"keep\" ) name \".\"
fail-clause  ::= \"else\" leave \".\"
               | \"else\" block
               | \"else\" \"error\" name block
leave        ::= \"return\" [ term ] | \"break\" [ number ] | \"continue\" [ number ] | \"panic\" term
guard-stmt     ::= \"guard\" term \"else\" ( leave \".\" | block ) \".\"
set-stmt       ::= \"set\" term term \".\"
return-stmt    ::= \"return\" [ term ] \".\"
jump-stmt      ::= ( \"break\" | \"continue\" ) [ number ] \".\"
expect-stmt    ::= \"expect\" term \".\"
drop-stmt      ::= \"drop\" name \".\" [ \"use\" name \".\" ] \".\"
call-stmt      ::= term
if-statement ::= \"if\" term block { \"else\" \"if\" term block } [ \"else\" block ] \".\"
while-stmt     ::= \"while\" term block \".\"
for-stmt    ::= \"for\"    name [ \"mut\" ] term [ filter ] block \".\"
repeat-stmt ::= \"repeat\" name type term [ filter ] block \".\"
range-stmt  ::= \"range\"  name type term term [ \"step\" term ] [ filter ] block \".\"
cycle-stmt  ::= \"cycle\"  name type term \"while\" term \"next\" term [ filter ] block \".\"
filter      ::= \"if\" term
match-stmt     ::= \"match\" [ \"comptime\" ] term \"do\" { case-arm | \"else\" block } \"end\" [ \"else\" block ] \".\"
case-arm       ::= \"case\" pattern block \".\"
pattern        ::= word { word } | word \"to\" word
region-stmt    ::= \"region\" name name block \".\"
borrow-stmt    ::= \"borrow\" name term block \".\"
task-group     ::= \"task_group\" [ name ] block \".\"")
    #para("1")[
      A statement is opened by its head and #strong[closed by one dot]; — so are statements that end in a block (#cref("6.1.6") (2b)). If the dot is missing, translation is
      refused (`E-DOT-MISSING`). Writing more dots after what is closed is refused too (`E-CLOSER-EXTRA`).
    ]
    #para("2")[
      A `call-stmt` is a `term` as it is — the dot that closes that call is the statement's dot (in `print x. .` the inner dot closes `x` and the outer dot
      `print`).
    ]
    #para("3")[
      The first `term` of a `set-stmt` is the place to put the value — a name (`i.`) or a form that gives a place (`idx a. i. .` · `field p. x .`).
    ]
    #para("4")[
      Words that open a `statement` (`let` · `var` · `return` · `guard` …) cannot come inside an expression. `if` is a statement, not a value (`E-IF-VALUE`).
    ]
    #para("5")[
      A `block` standing alone without a head is not a statement (`E-BLOCK-NOHEAD`). Names inside a `pattern` do not open forms.
    ]
    #para("6")[
      An `op-decl` standing inside a block is an op visible only inside that op (#cref("6.10")).
    ]
  ]
  #sub("A.10.4", "Expressions")[
    #syntax("the full grammar — expressions", "term           ::= literal | \"true\" | \"false\" | \"none\"
                 | call | paren-term
                 | struct-literal | fill-literal | list-literal
                 | island | pipe-form | word-form
call           ::= ( name | path ) { term } \".\"
paren-term     ::= \"(\" term \")\"
struct-literal ::= \"lit\" type \"do\" { field-init } \"end\" \".\"
field-init     ::= name term \".\"
fill-literal   ::= \"lit\" \"array\" word type \"do\" { fill-item } \"end\" \".\"
fill-item      ::= ( word | \"_\" ) term \".\"
list-literal   ::= \"lit\" list-kind { term | \"_\" } \".\"
list-kind      ::= \"array\" word type | \"slice\" type | \"vec\" word type | \"matrix\" number number { number } type
island         ::= \"expr\" island-item { operator island-item } \".\"
island-item    ::= term | \"(\" island-item { operator island-item } \")\"
pipe-form      ::= \"pipe\" term \"do\" { stage } \"end\" \".\"
stage          ::= stage-word { word | term } [ \"with\" term ] \".\"
stage-word     ::= ? a word of a pipe stage — a closed vocabulary (⟦§6.12⟧) ?
word-form      ::= \"field\" term segment { segment } \".\"
                 | \"method\" term { segment } name \".\" { term } \".\"
                 | \"payload\" term name name \".\"
                 | \"send\" term name { term } \".\"
                 | \"spawn\" \"actor\" path \".\"
                 | \"spawn\" \"send\" term name { term } \".\" \".\"
                 | \"spawn\" name { term } \".\"
                 | \"try\" term { name } \".\"
                 | \"call_builtin\" name { term } \".\"
                 | shaped-form
segment        ::= name | number
shaped-form    ::= ? a core op whose word positions are fixed by the tables of annex D — `cast <type> <term> .` · `isa <term> <variant> .` · `size_of <type> .` and the like ?")
    #para("1")[
      #strong[An expression is one form.]; A `call` is opened by a name and closed by a dot — without arguments it is the value of that name (`a.`). A form inside a form closes first with its own dot,
      so this grammar stands up the tree of an expression #strong[without reading declarations];. Whether the number of arguments matches the declaration is looked at after that (`E-IR-ARITY`).
    ]
    #para("2")[
      A `paren-term` only wraps one form and does not change the meaning (#cref("6.1.5") (3a)).
    ]
    #para("3")[
      Forms with word positions are #strong[a closed vocabulary];: the heads of statements, the words written in `word-form`, and the core ops whose shapes the tables of annex D write
      (`shaped-form`). The arguments of any other `call` — an op the user declared — are all `term`s. Even when a type is passed
      (`max_of u64. a. b. .`).
    ]
    #para("4")[
      The op name of `method` is #strong[the first name closed with a dot];. The names and numbers before it are steps, and what comes after are arguments
      (`method o. inner area. w. 2 .`). The two words of `payload` are the names of a variant and a field (#cref("6.2.19")).
    ]
    #para("5")[
      The `operator` inside an `island` is an infix operator (#cref("6.3.2")). Parentheses group one piece of the island.
    ]
    #para("6")[
      The words of a `stage` and the shapes of their arguments are specified by #cref("6.12").
    ]
  ]
  #sub("A.10.5", "Types")[
    #syntax("the full grammar — types", "type           ::= { type-qualifier } type-core
type-qualifier ::= \"mut\" | \"owned\" | \"ref\" | \"mut_ref\" | \"unsafe_ptr\"
type-core      ::= name | path
                 | \"(\" applied-type \")\"
                 | \"slice\" type
                 | \"array\" word type
                 | \"matrix\" number matrix-size { matrix-size } type
                 | \"option\" type
                 | \"result\" type type
                 | \"vec\" word type
                 | \"segments\" type
                 | \"task\" type
                 | \"chan\" type
                 | \"bitset\" [ number ]
                 | \"bits\" number
                 | \"cap\" name
                 | \"region\" name
                 | \"range\" [ type ] word word
                 | other-type
applied-type   ::= type | ( name | path ) type { type }
other-type     ::= ? other type shapes ⟦§6.2⟧ specifies (`mask` · `set` · `stack` · `fn` · `unsafe_fn` · `mmio`) ?
matrix-size    ::= number | name")
    #note[
      The `name` of `matrix-size` is used only in an op's `input` — it is the name of a translation-time value input of that op (#cref("6.2.6.1") (3a)).
    ]
    #para("1")[
      Type words such as `u8` · `bool` · `void` (A.9) and the names of types the author made are `name`s.
    ]
    #para("2")[
      A type #strong[ends by its characters alone]; — there is no closing mark. Constructors are a closed vocabulary (`slice u8` is two words, `result u64 perr`
      three), and any other type is one name. User types that take arguments, and shapes whose number of arguments is not fixed, are wrapped in parentheses
      (`(box u64)` — #cref("6.5.1") (3b)).
    ]
    #para("3")[
      Names and numbers inside a type do not open forms — they have no dot (`array n u8`).
    ]
    #para("4")[
      Constructors that take a count (`array` · `vec` · `matrix`) write the count first and the element type at the very end. So even if the element is itself a constructor, no parentheses
      are needed — `array 3 array 4 u8` (#cref("6.2.6") (1a) · (1d)).
    ]
  ]
]
