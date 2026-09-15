#import "../../book/lib.typ": *

= The surface --- full stops, blocks and clause order

#chapter-toc()

#prereq(
  ([#chref("intro"), What Lowent sets out to do], [one meaning, one spelling]),
  ([#chref("first-program"), A first program], [`--check` diagnostics and `--fmt`]),
)

#deepqa[
  In #chref("first-program"), what rejected `fn area output u64 . input w u64 . …`, and how far does `--fmt` fix it?
][
  `E-CLAUSE-ORDER` rejected it, because a data input came after `output`. `--fmt` moves clauses that are not inputs (output, effects,
  contracts) into place but leaves the order of inputs alone, because the order of inputs is also the order of the caller's
  arguments. This chapter covers that whole order table and the smaller rules underneath it --- full stops, parentheses, blocks.
]

#why[
  Lowent code looks strange at first. There are more full stops than parentheses, and words come before operands instead of
  operators between them. Memorising that strangeness as a list of exceptions takes a long time. But the rules of the surface are
  few, and all of them follow from #chref("intro")'s "one meaning, one spelling". Every later chapter is written on this surface, so
  it is gathered here in one place before values and flow (Part II).
]

#organizer[
  You will learn that a free-standing full stop closes a form while a newline closes nothing, that notation is prefix with the name
  first, and where the boundaries of the `expr` island that allows infix lie. You will pick up the rules for comments and literals,
  that names contain no dots and shadowing is forbidden, and that every block is `do … end`. Finally you will understand the whole
  clause-order table of an op head and why it is ordered as it is.
]

#chapter-questions()

== The name first, then the arguments

#idx("prefix notation")
Lowent expressions use *prefix notation*. The name of the operation comes first and its arguments follow. `add a b` adds `a` and
`b`, and a form inside a form is wrapped in parentheses.

```lowent
let total u64 be add 1 2 .
let mixed u64 be add 1 (mul 2 3) .
```

Prefix notation has no precedence. Someone reading `1 + 2 * 3` knows that multiplication comes first because they *memorised* it,
but in `add 1 (mul 2 3)` the parentheses already say so. The principle is the same for ops you write yourself. `write_out out 1 "hi"`,
`field p x` and `mean xs` all put the name first.

#qa[
  When arithmetic gets long, don't the parentheses pile up and become hard to read?
][
  That is why there is an `expr` island. Inside a place starting with `expr`, the four arithmetic operators, comparisons, `and` and
  `or` may be written infix. An expression in the island is translated to *exactly the same meaning* as the prefix form and costs
  nothing at run time. The island's boundaries appear later in this chapter and in detail in #chref("expr").
]

== A free-standing full stop closes

#idx("closer")
The end of a statement or clause is closed by a *free-standing* full stop `.`, called a *closer*. A newline is not a closer --- it is
whitespace, exactly like a space. So wherever you break a line, the meaning stays the same.

#demo("examples/ch03/poly.low")

The `return` in `poly` spans two lines but is one form; it ends at the full stop at the end of the second line. That is why there is
no line-continuation marker (C's trailing backslash, a trailing comma, indentation rules). `poly_expr` in the same file writes the
same computation as an `expr` island, and the two ops give the same answer.

`note WHY … WHY` is a multi-line comment; it ends at a line where the word written after `note` stands alone. `rem` is a line comment.
The language has no symbolic comments like `//` or `/* */` --- comments too are opened with words.

The number of full stops works as a kind of *checksum*. If opened forms and closers do not match, the compiler says so. Forgetting one
parenthesis looks like this.

#demo("examples/ch03/dots.low")

The first diagnostic means a parenthesis tried to leak past the block's `end`. Parentheses cannot cross a block boundary. So a single
wrongly closed parenthesis stops inside that block instead of silently swallowing the rest of the file.

#misconception[A semicolon closes statements too][
  It once did. When the processor met `;` it literally produced a full stop. Two spellings of one meaning force a reader to know
  both, so it was removed. Today `;` is rejected with `E-VOCAB-REMOVED`.
]

#demo("examples/ch03/semi.low")

== A block is always `do … end`

Every place that groups statements opens with `do` and closes with `end`: an op body, the body of `if`, `while` and `for`, the arms of
`match`, and the bodies of declarations such as `struct`, `enum`, `trait` and `actor`.

```lowent
struct point do
  x u64 .
  y u64 .
end .

fn sum_to input n u64 . output u64 .
do
  var total u64 be 0 .
  var i u64 be 1 .
  while le i n . do
    set total (add total i) .
    set i (add i 1) .
  end .
  return total .
end .
```

The full stop after `end` closes the form that contains the block (the `struct` declaration, the `while` statement). `end` also closes
the forms opened inside it, so leaving that full stop out changes nothing, but this book always writes it. Opening a declaration block
with only a line break instead of `do` is rejected with `E-STMT-NODO`, and `--fmt` inserts the `do`.

== Comments and literals

#dtable(
  columns: 2,
  id: "surface-literals",
  caption: [Writing literals],
  [*Kind*], [*Examples*],
  [Integers (decimal, hex, binary)], [`42` · `0xFF` · `0b1010` · `1_000_000`],
  [Floating point], [`3.14` · `6e-3` · `0x1.8p3`],
  [Strings (bytes)], [`"hello\n"`],
  [Strings (UTF-16 · code points)], [`u"…"` · `U"…"`],
  [Booleans · absence], [`true` · `false` · `none`],
)

An integer literal has no type of its own; the place where it is used gives it one. A leading 0 is not octal (`0755` is 755). A
literal that does not fit the type of its place is rejected rather than silently truncated.

#demo("examples/ch03/lit300.low")

C truncates 300 to 44 when it is put in a `u8`. Then the value written in the source and the actual value differ. Lowent stops it at
translation time. Shrinking is done by *writing* one of the named `narrow` operations (#chref("numbers")).

The set of string escapes is closed. There are the familiar `\n`, `\t`, `\\` and `\"`, plus `\xNN` (exactly two digits), `\uXXXX`
(four) and `\UXXXXXXXX` (eight); any other escape is rejected with `E-STR-ESCAPE`. C's `\x` eats hex digits without end, so the meaning
of `"\x41e"` depends on the character next to it; in Lowent `\x41e` is always `A` followed by `e`. A string is not a separate type but
a byte slice (`slice u8`).

== Names

A name starts with an ASCII letter or underscore and continues with letters, digits and underscores. Two things differ from other
languages.

First, *declared names contain no dots.* A place with a dot attached is a path *referring* to a name, and it has only three meanings
--- a module's name (`allocs.byte_allocator`), a variant's name (`node.lit`), and the name of a declaration attached to a type
(`rect.area`). Looking inside a value (reading a field, calling a method) is not a glued dot but a form: `field p x` · `method s area`.

#idx("shadowing")
Second, *there is no shadowing.* An inner name that reuses the spelling of an outer name is rejected.

#demo("examples/ch03/shadow.low")

A shadowed name makes someone who read the head *think of a different value*. If you had to work out on every line whether `n` is the
parameter or the new local, that is exactly semantic entropy. No parameter, module name, builtin op name or name still alive in an
outer block can be shadowed. Choose a new name instead.

#qa[
  Can a module and an op share a name?
][
  No. With an op `twice` inside module `twice`, both would be referred to as `twice.twice` and `twice` and could not be told apart, so
  it is rejected with `E-NAME-DUP`. That is why the module names in this book's examples differ slightly from their op names.
]

== Words are a budget

The words the language gives meaning to (keywords) form a closed list. Adding a new word grows the language, so nothing that existing
words can express gets a new one. Roughly, they fall into these groups.

#dtable(
  columns: 2,
  id: "surface-keywords",
  caption: [Groups of core words],
  [*Group*], [*Words*],
  [Declarations], [`module` `use` `type` `newtype` `struct` `enum` `trait` `contract` `actor` `state`],
  [Ops], [`fn` `proc` `export` `unsafe` `extern`],
  [Locals and flow], [`let` `var` `set` `return` `if` `else` `for` `while` `guard` `match` `case` `try` `break` `continue` `expr`],
  [Values], [`make` `true` `false` `none` `be`],
  [Others], [`spawn` `send` `drop` `test` `expect` `satisfies` `do` `end`],
)

`add`, `len`, `neg` and the like are not words but *builtin ops*. They cannot be used as parameter names either, but they occupy the
space of names rather than grammar. Removed words (`in`, `loop`, `as`, `to` and so on) are not quietly accepted; `E-VOCAB-REMOVED`
tells you what to use instead.

== The boundaries of the `expr` island

What the island allows fits in one table. `*` and `/` bind tighter than `+` and `-`, comparisons (`eq`, `lt` and so on) come below
them, and `and` binds tighter than `or`. Bitwise operations, remainder, minimum and the like are written in prefix form even inside the
island. The precedence of bitwise operations differs between languages, so putting them in the island would not make them easier to
read --- it would add something to look up.

Comparisons cannot be chained.

#demo("examples/ch03/chain.low")

Mathematical `a < b < c` means "a is less than b and b is less than c", but in many languages that spelling reads as `(a < b) < c`. A
spelling that means one thing to people and another to the machine is a place where code is silently wrong. As the diagnostic
suggests, write `expr (a lt b) and (b lt c)`.

== Clause order in an op head

#idx("clause order")
An op head is a name followed by *clauses*. Each clause is closed by a full stop, and clauses have one fixed order.

#dtable(
  columns: 3,
  id: "surface-clause-order",
  caption: [Clause order in an op head (front to back)],
  [*Order*], [*Clause*], [*Why here*],
  [1], [`satisfies` · `lowdoc`], [Says *what* the op is first],
  [2], [`vector` · `priority`], [The character of the op as a whole],
  [3], [`comptime` inputs], [Later input and output types use these names],
  [4], [Capability and region inputs (`cap …` · `region …`)], [Who allowed it is visible before what it receives],
  [5], [`using`], [Which allocator the following data uses],
  [6], [Data inputs], [The values the op receives],
  [7], [`output`], [The output type may use the inputs' type parameters],
  [8], [`effects`], [*What it does* with the capabilities received above],
  [9], [`link` · `variadic` · `asm`], [Places that connect to the outside (C, machine code)],
  [10], [`access` · `parallel` · `reduce`], [How it runs split up],
  [11], [`requires` → `ensures` → `errors` → `tests`], [Input conditions, output promises, failure, tests],
)

You need not memorise every clause; most ops use four or five of them. What to remember is the principle --- clauses are placed so that
*earlier ones are used by later ones*. Type parameters before inputs, capabilities before data, inputs before the output, capabilities
before effects, effects before contracts.

```lowent
proc copy_upper
  input out cap io .            rem capability input
  input src slice u8 .          rem data input
  output u64 .
  effects io .
  requires gt (len src) 0 .
do
  return write_out out 1 src .
end .
```

Written on one line or one clause per line, the meaning is the same (newlines are whitespace). This book writes short heads on one
line and heads with contracts one clause per line.

#realcase[When the output came first][
  This order was not settled in one go. For a while the order put `output` at the very front of the head --- on the grounds that what
  an op returns should be seen first. But in generic ops whose output type uses an input's type parameter, that meant a name being
  used before it was declared, and capabilities ended up far from their effects. So the order was redrawn to follow how clauses use
  one another, and then fixed. The compiler enforces it with a rank table.
]

#recap[
  A free-standing full stop closes a form, and a newline is whitespace. Expressions are prefix with the name first, and infix is
  written only inside `expr` islands. A block is always `do … end`. Names contain no dots, and shadowing is forbidden. A literal that
  does not fit its place's type is rejected. The clauses of an op head have one order, arranged so that earlier clauses are used by
  later ones.
]
