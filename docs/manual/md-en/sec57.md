# Appendix D — Grammar summary

The normative text is the specification; this appendix is a reference gathering frequently sought shapes. There is no complete formal grammar yet.

## <a id="sx1"></a>Clause order of an op head

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
  access · inplace <written> <read> · parallel <name> split · reduce  ⑩ how it touches memory · runs split
  requires [static|debug|assume] <cond> .  ⑪ input conditions
  ensures <cond> .                         ⑫ output promises (ret)
  errors <variant> [<cond>] .              ⑬ failure
  tests … · schedule …                     ⑭
do
  <statements>
end .
```

## <a id="sx2"></a>Declarations

```text
module <name> .
use <module> [from "<place>"] .
type <name> <type> .
newtype <name> <type> .
struct <name> do [satisfies <trait> .] [layout packed .] [mmio <address> .] <field> <type> [big|little] [rw|ro|wo] . … end .
enum <name> do <variant> [<field> <type>]… . … end .
trait <name> do <op name> <clauses…> . … end .
contract <name> do requires <cond> . … end .
actor <name> do [satisfies …] state do <field> <type> . … end . [failure restart max <n> .] [mailbox bounded <n> .] <op>… end .
build profile <name> .        build tier t0|t1|t2|t3 .        build <mode> .
build option <name> bool|int|choice … default <value> .
test <name> [schedule explore_interleavings [limit <n>]] do expect <cond> . … end .
```

## <a id="sx3"></a>Statements

```text
let <name> [<type>] [using <source>] be <expr> .
var <name> [<type>] be <expr> .
set <place> <expr> .                      (place = name · field … · index …)
if <cond> . do … end [else do … end] .
while <cond> . do … end .
for <name> <slice> do … end .
guard <cond> . else <leaving statement> .
match <value> do case <pattern> [when <cond>] . do … end … end .
return [<expr>] .    break .    continue .    panic "<text>" .
drop <name> .
region <name> stack|frame|arena|static|heap|mmap|disk|device do … end .
task_group [cancel_on_error] do … end .
pipe <source> do <stage> . … <terminal> . end .
spawn send <actor> <message> <value>… .    drain <actor> .    schedule .
```

## <a id="sx4"></a>Patterns

```text
case _ .                    everything else
case <integer> .  case <low> to <high> .
case <variant> [<name>…] .  case <pattern> or <pattern> .
case some <name> .  case none .  case ok <pattern> .  case error .
case <name> when <cond> .
```

## <a id="sx5"></a>Expressions

```text
<op> <args>…                            prefix --- no precedence, forms inside forms in parentheses
expr <a> + <b> * <c>                    infix island --- * / above + -, comparisons below, and above or
make <type> do <field> <expr> . … end   <enum>.<variant> <values>…
field <value> <steps>…   index <slice> <n>   method <value> <name> <args>…
some <value>   ok <value>   error <variant>   none
try <expr> [else_none | else_error <variant>]
comptime <expr>   size_of <type>   config <name>
spawn actor <type>   send <actor> <message> <values>…   spawn <op> <args>…   await <handle>
alloc_bytes <root> capacity <n>
```

## <a id="sx6"></a>Heads and closers

A form starts with one head and ends with one closer. The closer is the detached period `.`, and `)` and `end` also close what is open inside them. A newline is not a closer but whitespace. Parentheses cannot cross block boundaries.

---

[← Prev](sec56.md) · [Contents](README.md) · [Next →](sec58.md)
