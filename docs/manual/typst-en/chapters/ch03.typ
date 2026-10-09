#import "../../typst-ko/lib.typ": *

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
Lowent expressions use *prefix notation*. The name of the operation comes first and its arguments follow. One rule reads all of it ---
*a name opens, a stop closes.* In `add a. b. .`, `add` opens a form and the last stop closes it. `a.` and `b.` follow the same rule:
the variable's name opens, and the stop glued to it closes. A literal such as `1` or `"hi"` is not a name, so it takes no stop. When a
form sits inside a form, the inner one is closed first, by its own stop.

```lowent
let total u64 add 1 2 . .
let mixed u64 add 1 mul 2 3 . . .
```

Prefix notation has no precedence. Someone reading `1 + 2 * 3` knows that multiplication comes first because they *memorised* it,
but in `add 1 mul 2 3 . .` the stops already say so --- the first stop closes `mul`, the second closes `add`. The principle is the same
for ops you write yourself. `write_out out. 1 "hi" .`, `field p. x .` and `mean xs. .` all put the name first.

Parentheses are *decoration*: they never change the meaning. Use them to make an inner form stand out, as in `add 1 (mul 2 3 .) .`.
The form inside the parentheses still closes with its own stop --- parentheses do not replace it.

#qa[
  When arithmetic gets long, don't the stops pile up and become hard to read?
][
  That is why there is an `expr` island. Inside a place starting with `expr`, the four arithmetic operators, comparisons, `and` and
  `or` may be written infix. An expression in the island is translated to *exactly the same meaning* as the prefix form and costs
  nothing at run time. The island's boundaries appear later in this chapter and in detail in #chref("expr").
]

== A free-standing full stop closes

#idx("closer")
Every open form is closed by a full stop `.` --- a value, a statement, a clause, a declaration. That stop is called a *closer*. One
stop closes *the innermost form that is still open*. A variable is written with the stop glued on, `a.` (the spaced `a .` means the
same). A newline is not a closer --- it is
whitespace, exactly like a space. So wherever you break a line, the meaning stays the same.

#demo("examples/ch03/poly.low")

The `return` in `poly` spans two lines but is one form; it ends at the full stop at the end of the second line. That is why there is
no line-continuation marker (C's trailing backslash, a trailing comma, indentation rules). `poly_expr` in the same file writes the
same computation as an `expr` island, and the two ops give the same answer.

`note WHY … WHY` is a multi-line comment; it ends at a line where the word written after `note` stands alone, starting in the first column. `rem` is a line comment.
The language has no symbolic comments like `//` or `/* */` --- comments too are opened with words.

The number of full stops works as a kind of *checksum*. If the forms opened and the stops that close them do not match, the compiler
says so. Leaving one stop out looks like this.

#demo("examples/ch03/dots.low")

The diagnostic says it reached `end` with nothing closing the `return`. An open form cannot cross a block's `end`. So one missing
stop is caught inside that block instead of silently swallowing the rest of the file. The other way round, a stop with nothing left
to close is rejected with `E-CLOSER-EXTRA`.

#misconception[A semicolon closes statements too][
  It once did. When the processor met `;` it literally produced a full stop. Two spellings of one meaning force a reader to know
  both, so it was removed. Today `;` is rejected with `E-VOCAB-REMOVED`.
]

#demo("examples/ch03/semi.low")

== A block is always `do … end`

Every place that groups statements opens with `do` and closes with `end`: an op body, the body of `if`, `while` and `for`, the arms of
`match`, and the bodies of declarations such as `struct`, `enum`, `trait` and `actor`.

```lowent
def struct point do
  x u64 .
  y u64 .
end .

fn sum_to input n u64 . output u64 .
do
  var total u64 0 .
  var i u64 1 .
  while le i. n. . do
    set total. add total. i. . .
    set i. add i. 1 . .
  end .
  return total. .
end .
```

`do … end` works like a pair of braces. `end` closes *only its own `do`* and leaves everything outside alone. And `end` closes the
block, *not the form* --- a form is always closed by a stop. So there are three things to know.

- *A form that owns a block still ends with a stop.* Op declarations, `struct`·`enum`, `if`·`while`·`for`·`match` all end in `end .`,
  as in the example above. Leaving the stop out is rejected with `E-DOT-MISSING`.
- *A chain joined by `else` is one form.* In `if … do … end else do … end .` there is a single stop, at the end.
- *A block inside a value gives two stops in a row.* In `let p lit point do x 1 . y 2 . end . .` the first stop closes the `lit`
  value and the second closes the `let` statement.

C has no `;` after `if (c) { … }` but one after `p = (struct point){ 1, 2 };`. Lowent makes no such distinction --- what was opened is
closed by a stop.

```text
while le i. n. . do  …  end .              the condition closes with its own stop, the while form with the stop after end
      └ condition ┘
└──────── while form ──────┘

let p lit point do x 1 . end . .           the first stop closes the lit value, the second the let statement
      └───── lit value ─────┘
└──────── let statement ─────────┘
```

Each statement inside a block must end with its own full stop too; `end` does not close an open statement for you. A bare
`do … end` with nothing opening it is not allowed either (`E-BLOCK-NOHEAD`) --- a block always has a head. Opening a declaration
block with only a line break instead of `do` is rejected with `E-STMT-NODO`.
The clauses of an op header work the same way. Each ends with its own stop --- `input n u64 .` · `output u64 .` --- and the next
clause word does not close the one before it; leaving the stop out is `E-DOT-MISSING`.

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

#dtable(
  columns: 4,
  id: "surface-escapes",
  caption: [String escapes --- these fourteen only],
  [*Written*], [*Meaning*], [*Written*], [*Meaning*],
  [`\\`], [backslash], [`\v`], [vertical tab],
  [`\"`], [double quote], [`\0`], [zero byte],
  [`\'`], [single quote], [`\xNN`], [two hex digits --- any single byte],
  [`\a`], [alert], [`\uXXXX`], [code point, four digits],
  [`\b`], [backspace], [`\UXXXXXXXX`], [code point, eight digits],
  [`\f`], [form feed], [`\n`], [newline],
  [`\r`], [carriage return], [`\t`], [horizontal tab],
)

`\uXXXX` and `\UXXXXXXXX` write a code point, and the prefix decides how many units carry it --- `"\U0001F600"` is 4 bytes, `u"…"` is 2
UTF-16 code units (a surrogate pair) and `U"…"` is 1 code point. Surrogate values (D800 … DFFF) and values above 10FFFF are rejected. There
are no octal escapes --- the language has no octal notation at all, so reviving it only in literals would make that the one exception.

Using every shape in the table in one file looks like this.

#demo("examples/ch03/literals.low")

- `ints` is 10 (`0b1010`) + 1000000 (`1_000_000`) + 65535 (`0xFF_FF`) + 755 (`0755`). An underscore goes only *between* digits and does not
  change the value; it makes long numbers easier to read. `0755` is 755, not 493, a choice that removes a C trap.
- `floats` is 1500 (`1.5e3`) + 3 (`0x1.8p1` --- 1.5 × 2¹). Hexadecimal floats are for when the bits must be written exactly. Decimal `0.1`
  cannot be written exactly in binary, but `0x1.8p1` is exactly 3.
- `chars` is 65 (`'A'`) + 44032 (`u'가'`) + 128512 (`U'😀'`). The prefix decides the width of the element. Writing a character that does not
  fit in one byte, such as `'가'`, without a prefix is refused with `E-CHAR-WIDTH`.
- In `texts`, `"\x41e"` is 2 bytes, `u"가나"` is 2 code units and `text DOC … DOC` is 13 bytes. A text literal holds, *exactly as
  written*, everything from the tag after `text` to the line where the same tag stands alone, starting in the first column. An indented tag is part of the text, and the dot that closes the statement goes on the next line. The `\n` in the body is two characters, not a
  newline, and no newline is added after the last line. It is there to paste long descriptions or test input without escapes.
- The `note END … END` at the top is a multi-line comment. It turns a whole block into a comment without `rem` on every line.

== Names

A name starts with an ASCII letter or underscore and continues with letters, digits and underscores. Two things differ from other
languages.

First, *declared names contain no dots.* A place with a dot attached is a path *referring* to a name, and it has only three meanings
--- a module's name (`allocs.byte_allocator`), a variant's name (`node.num`), and the name of a declaration attached to a type
(`rect.area`). Looking inside a value (reading a field, calling a method) is not a glued dot but a form: `field p. x .` · `method s. area. .`.

#idx("shadowing")
Second, *there is no shadowing.* An inner name that reuses the spelling of an outer name is rejected.

#demo("examples/ch03/shadow.low")

A shadowed name makes someone who read the head *think of a different value*. If you had to work out on every line whether `n` is the
parameter or the new local, that is exactly semantic entropy. No parameter, module name, core op name or name still alive in an
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
  [Locals and flow], [`let` `var` `set` `return` `if` `else` `while` `for` `repeat` `range` `cycle` `next` `step` `guard` `match` `case` `try` `break` `continue` `expr` `keep`],
  [Values], [`lit` `true` `false` `none`],
  [Others], [`spawn` `send` `drop` `test` `expect` `satisfies` `do` `end`],
)

`add`, `len`, `neg` and the like are not words but *core ops*. They cannot be used as parameter names either, but they occupy the
space of names rather than grammar. Removed words (`in`, `loop`, `as`, `to` and so on) are not quietly accepted; `E-VOCAB-REMOVED`
tells you what to use instead.

== The boundaries of the `expr` island

What the island allows fits in one table. `*` and `/` bind tighter than `+` and `-`, comparisons (`eq`, `lt` and so on) come below
them, and `and` binds tighter than `or`. Bitwise operations, remainder, minimum and the like are written in prefix form even inside the
island. The precedence of bitwise operations differs between languages, so putting them in the island would not make them easier to
read --- it would add something to look up.

The same rule holds inside the island. A variable is written `a.`, and the island that `expr` opened is closed by one stop:
`expr a. + b. * 2 .`.

Comparisons cannot be chained.

#demo("examples/ch03/chain.low")

Mathematical `a < b < c` means "a is less than b and b is less than c", but in many languages that spelling reads as `(a < b) < c`. A
spelling that means one thing to people and another to the machine is a place where code is silently wrong. As the diagnostic
suggests, write `expr (a. lt b.) and (b. lt c.) .`.

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
  requires gt len src. . 0 . .
do
  return write_out out. 1 src. . .
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

== Common mistakes

Surface mistakes mostly come from *habits carried over from other languages*. Some diagnostics point somewhere unexpected, so it helps
to remember the shapes too.

#antipattern[Writing comments with `//`][
  #demo("examples/ch03/mistake_slash.low")

  Comments in this language are `rem` (line comment) and `note WHY … WHY` (multi-line), nothing else. `//` is not a comment, so it is
  rejected with `E-VOCAB-REMOVED`, and the diagnostic tells you to write `rem`. Comments open with a word to keep symbols down ---
  `//`, `#` and `--` differ from language to language, so one word was chosen. The fix: `rem double it`.
]

#antipattern[Reading a field with a glued dot][
  #demo("examples/ch03/mistake_glued.low")

  `p.x` is everyday C or Python, but here it is `E-FIELD-GLUED`. The dot already serves as a *path to a name* (module
  `allocs.bump_bytes`, variant `color.red`); if it also looked inside values, every `a.b` would need working out. Read a field with
  `field p. x .` (#chref("structs-enums")).
]

#antipattern[Writing the head's clauses in any order][
  #demo("examples/ch03/mistake_order.low")

  Clauses have exactly one order (#tblref("surface-clause-order")). If the order were free, the same head could be written many ways and
  readers would have to hunt for each clause. The diagnostic says what came after what, and `lowentc --fmt` puts the order right.
]

#antipattern[Opening an `if` body without `do`][
  #demo("examples/ch03/mistake_ifdo.low")

  Bodies are not opened by indentation as in Python. Once the condition is closed by its own stop, `do` must come next. Anything
  else there is rejected with `E-CTRL-NODO` (a `do … end` block belongs here). The fix: `if gt a. 3 . do`.
]

#antipattern[Writing a string in single quotes][
  #demo("examples/ch03/mistake_quote.low")

  Single quotes are the literal for *one character* (`'a'` is the byte 97). Two characters do not fit in one unit, so you get
  `E-CHAR-WIDTH`. Strings always use double quotes: `len "hi" .` is 2. The two kinds of quote mean different things because "one character"
  and "several bytes" are different types (#tblref("surface-literals")).
]

#misconception[A number with a leading zero is octal][
  In C, `0755` is octal 493, but Lowent has no octal notation.

  #demo("examples/ch03/octal.low")

  It was removed because the same characters mean different numbers in different languages. For values where octal is handy, such as
  permission bits, write `0x1ED` (hexadecimal) or use bit operations.
]

== This chapter's syntax at a glance

#dtable(
  columns: 3,
  id: "surface-glance",
  caption: [Surface rules --- shape · meaning · why it looks this way],
  [*Shape*], [*Meaning*], [*Why*],
  [`add a. mul b. c. . .`], [prefix notation --- a name opens, a stop closes], [no precedence rules to memorise],
  [`… .`], [a stop closes the innermost open form (value, statement, clause, declaration)], [newlines never change meaning --- break lines anywhere],
  [`do … end`], [every block], [only one way to open a block],
  [`rem …` · `note WHY … WHY`], [line comment · multi-line comment], [no symbol comments (`//`, `#`) --- they open with a word],
  [`42` · `0x2A` · `0b101010` · `1_000`], [integer literals (the position decides the type)], [no octal --- `0755` is 755],
  [`"hi\n"` · `'a'`], [a string (several bytes) · one character], [the quote itself is the type difference],
  [`true` · `false` · `none`], [booleans · no value], [values are words too],
  [`field p. x .` · `method s. area. .`], [read a field · call a method], [the dot is kept for name paths only],
  [`allocs.bump_bytes` · `color.red`], [a name inside a module · a variant name], [one dot means "that name inside this name"],
  [`expr a + b * c`], [an infix island --- arithmetic, comparisons, `and`/`or` only], [long arithmetic reads easily; same meaning as prefix],
  [Clause order in an op head], [#tblref("surface-clause-order")], [each clause comes before the ones that use it],
  [`0b1010` · `1_000_000` · `0x1.8p1` · `u'가'` · `U"…"`], [binary · digit separator · hex float · prefixed char and string], [no octal --- `0755` is 755],
  [`text DOC … DOC` · `note END … END`], [text literal (escapes are not processed) · multi-line comment], [long text goes in as written],
)

#recap[
  A free-standing full stop closes a form, and a newline is whitespace. Expressions are prefix with the name first, and infix is
  written only inside `expr` islands. A block is always `do … end`. Names contain no dots, and shadowing is forbidden. A literal that
  does not fit its place's type is rejected. The clauses of an op head have one order, arranged so that earlier clauses are used by
  later ones.
]
