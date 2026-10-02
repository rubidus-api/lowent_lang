#import "../../typst-ko/lib.typ": *

= Appendix D --- Grammar summary

The normative text is the specification; this appendix is a reference gathering frequently sought shapes. There is no complete formal grammar yet.

== Clause order of an op head

```text
[export] [unsafe] [extern] fn|proc <name>
  satisfies · lowdoc                       ① what it is
  vector · priority                        ② character of the whole op
  input <name> comptime …                  ③ translation-time inputs
  input <name> cap … · region …            ④ capability and region inputs
  using <name> <type> .                    ⑤ allocator drawn from
  input <name> <type> .                    ⑥ data inputs
  output <type> .                          ⑦
  effects <atoms…> [via <type>] .          ⑧
  link "<name>" · variadic · asm <machine> ⑨ joining the outside
  access · inplace <written> <read> · invalidates <input> · parallel <name> split · reduce  ⑩ how it touches memory · runs split
  requires [static|debug|assume] <cond> .  ⑪ input conditions
  ensures <cond> .                         ⑫ output promises (ret)
  errors <variant> [<cond>] .              ⑬ failure
  tests … · schedule …                     ⑭
do
  <statements>
end
```

An `extern` op whose body is in C holds the same clauses, in the same order, *inside* `do … end` --- `unsafe extern proc <name> do <clause>* end`. Only clauses go in that block.

== Declarations

```text
module <name> .
use <module> [from "<place>"] .
def type <name> <type> .
def newtype <name> <type> .
def struct <name> do [satisfies <trait> .] [layout packed .] [mmio <address> .] <field> <type> [big|little] [rw|ro|wo] . … end
def enum <name> do <variant> [<field> <type>]… . … end
trait <name> do <op name> <clauses…> . … end
contract <name> do requires <cond> . … end
actor <name> do [satisfies …] state do <field> <type> . … end [failure restart max <n> .] [mailbox bounded <n> .] <op>… end
build profile <name> .        build tier t0|t1|t2|t3 .        build <mode> .
build option <name> bool|int|choice … default <value> .
test <name> [schedule explore_interleavings [limit <n>] .] do expect <cond> . … end
```

== Statements

```text
let <name> [<type>] [using <source>] be <expr> .
var <name> [<type>] be <expr> .
set <place> <expr> .                      (place = name · field … · index …)
if <cond> . do … end [else do … end] .
while <cond> . do … end
for <name> <slice> do … end
guard <cond> . else <leaving statement> .
match <value> . do case <pattern> [when <cond>] . do … end … end
return [<expr>] .    break .    continue .    panic "<text>" .
drop <name> .
region <name> stack|frame|arena|static|heap|mmap|disk|device do … end
task_group [cancel_on_error] do … end
pipe <source> do <stage> . … <terminal> . end
spawn send <actor> <message> <value>… .    drain <actor> .    schedule .
```

== Patterns

```text
case _ .                    everything else
case <integer> .  case <low> to <high> .
case <variant> [<name>…] .  case <pattern> or <pattern> .
case some <name> .  case none .  case ok <pattern> .  case error .
case <name> when <cond> .
```

== Expressions

```text
<op> <args>…                            prefix --- no precedence, forms inside forms in parentheses
expr <a> + <b> * <c>                    infix island --- * / above + -, comparisons below, and above or
lit <type> do <field> <expr> . … end    <enum>.<variant> <values>…
lit array <type> <len> <value>… [_] .   lit slice <type> <value>… .  (a list closes with its own .)
field <value> <steps>…   index <slice> <n>   method <value> <name> <args>…
some <value>   ok <value>   error <variant>   none
try <expr> [else_none | else_error <variant>]
comptime <expr>   size_of <type>   config <name>
spawn actor <type>   send <actor> <message> <values>…   spawn <op> <args>…   await <handle>
alloc_bytes <root> capacity <n>
```

== Heads and closers

A form starts with one head and ends with one closer. The closer is the detached period `.`, and `)` also closes what is open inside it. `do … end` is a pair of braces, so `end` closes
only its own `do` --- a construct that owns a block ends at its `end`, and a statement that uses a block value ends with its own `.`.
A newline is not a closer but whitespace. Parentheses cannot cross block boundaries.
