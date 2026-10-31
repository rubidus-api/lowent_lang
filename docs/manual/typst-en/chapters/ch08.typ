#import "../../typst-ko/lib.typ": *

= Expressions --- prefix notation and the `expr` island

#chapter-toc()

#prereq(
  ([#chref("surface"), The surface], [the name first, then the arguments]),
  ([#chref("numbers"), Numbers], [bitwise operations are written as words]),
  ([#chref("control"), Flow], [`match` must cover every case]),
)

#deepqa[
  Why did #chref("surface") reject `expr a. lt b. lt c. .`? What did the diagnostic say to write instead?
][
  Chained comparisons read as "a < b and b < c" in mathematics but as `(a < b) < c` in many languages. Since the same spelling means
  different things to people and to the machine, it was rejected with `E-EXPR-CHAIN`. The diagnostic said to write
  `expr (a. lt b.) and (b. lt c.) .`. This chapter covers every rule inside that island.
]

#why[
  Expressions are the most used part of the grammar. In Lowent, prefix notation is the default and infix is allowed only inside an `expr`
  island. You need to understand this two-layer structure to read prefix expressions with their runs of stops, and to know what the island
  does not allow so you do not get lost. As the last chapter of Part II, it gathers the rules of expressions seen piecemeal so far.
]

#organizer[
  You will learn why prefix notation has no precedence, and the island's short precedence table. You will pick up that an op call
  inside the island still closes with its own stop, that there are no unary operators, and that comparisons mixed with `and` and `or`
  are parenthesised. You will also see how short-circuiting `and` and `or` keep conditions safe, and `size_of` and `comptime`, which are
  computed at translation time.
]

#chapter-questions()

== Nothing to memorise in prefix notation

In prefix notation the operation's name comes first and its arguments follow. The name opens a form and a stop closes it. This shape
has no precedence; the stops say everything about what is computed first --- the inner form closes first.

#demo("examples/ch08/island.low")

`add a. mul b. 2 . .` in `score_prefix` and `expr a. + b. * 2 .` in `score` are the same expression. The two ops give the same answer, and since
the island is translated to prefix, the run-time cost is the same too. The island is *a projection of notation*, not a different
operation.

The reason for words instead of symbols is the same. `^` is exponentiation in some languages and exclusive or in others. `bit_xor` means
one thing wherever it is read. And words can be read aloud.

== The island's precedence table

#idx("expr island")
Precedence exists only inside the island, and this table is all of it.

#dtable(
  columns: 4,
  id: "expr-precedence",
  caption: [Precedence inside an `expr` island (tightest at the top)],
  [*Level*], [*Operations*], [*Associativity*], [*Prefix equivalent*],
  [5], [`( )` grouping], [---], [Only marks grouping; not a value],
  [4], [`*` `/`], [left], [`mul` · `div`],
  [3], [`+` `-`], [left], [`add` · `sub`],
  [2], [`eq` `ne` `lt` `le` `gt` `ge`], [cannot chain], [the prefix op of the same name],
  [1b], [`and`], [left], [`and` (short-circuit)],
  [1a], [`or`], [left], [`or` (short-circuit)],
)

Parentheses change the order, as in `grouped`'s `expr (a. + b.) * 2 .`. Operations not in the table --- the remainder `mod`, bitwise
operations, `min` and `max` --- are written prefix even inside the island. `spread`'s
`expr (max a. b. .) - (min a. b. .) .` has that shape. A prefix form closes with its own stop, so the parentheses there are decoration.

`in_range` joins two comparisons with `and`. By the table, comparisons bind tighter than `and`, so `expr lo. le x. and x. le hi. .`
means the same without parentheses (the "Common mistakes" section of this chapter shows it). Still, when mixing comparisons with `and`
or `or`, parenthesise each comparison. The reader no longer needs to recall the table either.

#qa[
  The VM shows `in_range(5, 1, 9) = 1`. Has the `bool` become a number?
][
  No. The VM's result line merely *displays* a `bool` as 0 or 1. Inside a program a `bool` does not mix with numbers (#chref("numbers")).
  In the C that native code is emitted as, a `bool` is also one byte holding 0 or 1.
]

== What the island cannot do

An op can be called inside the island too. A call is opened by its name and closed by its own stop, so the stops say where `twice`
ends.

#demo("examples/ch08/app.low")

In `expr twice a. . + 1 .` the stop after `a.` closes `twice`, and the last stop closes the island. To make the call stand out, wrap it
in parentheses as `bump_marked` does --- the meaning is the same.

The island is a world of infix operators, so one thing is missing: unary operators. The island has no operator without a left operand.

#demo("examples/ch08/unary_bad.low")

To flip a sign, write `neg a. .` in prefix, or inside the island write `expr 0 - a. .`. `not b. .` and `bit_not x. .` are the same.

#misconception[The island is small because it is unfinished][
  Its size is intentional. Infix notation is only worth having *when nobody needs to look up precedence*. The operations in the table
  work like school arithmetic, so everyone reads them the same way. Adding bitwise or unary operators would add things to look up rather
  than making code easier to read. The specification states that the island will not grow.
]

== Short-circuiting guards conditions

#idx("short-circuit evaluation")
`and` and `or` do not compute the right-hand side when the left-hand side already decides the answer. Using that, you can put the
condition that makes the right-hand side safe on the left.

#demo("examples/ch08/shortcircuit.low")

`safe_first_is_zero` does not compute `idx xs. 0 .` on an empty slice, because if `gt len xs. . 0 .` on the left is false, the answer is
already false. `ratio_ok` likewise skips the division when the denominator is 0. This shape suits conditions too small for a `guard`. If
the condition is a fact about the whole op, `guard` is better (#chref("control")).

== Expressions computed at translation time

Some expressions have values before the program runs.

#demo("examples/ch08/compt.low")

`size_of u32 .` gives the number of bytes of one value of that type at translation time. It lets code that takes a type as a parameter
compute its costs honestly instead of assuming the widest type (#chref("generics")).

`comptime add 2 3 .` marks an expression to be computed at translation time. When the value a `match` splits on is a translation-time
constant, the `match` folds to the one matching arm and the run-time comparisons disappear. The folded-away arms are still type-checked,
unlike code removed with C's `#ifdef`, which is not even checked. `config <option>`, which reads the build configuration, is also used as a
translation-time constant (#chref("build-test")).

A `comptime` expression can also read names: module constants, a local `let` whose value is known at translation time, `machine.*`,
`native_lanes <type>` and `config <option>`. A local `let`'s value is known at translation time when it is written as a literal, one such
name, `native_lanes <type>` or `comptime <expr>`. A `var` and an input are not read. A module constant's value can itself be written as
`comptime <expr>`.

#demo("examples/ch08/compt_names.low")

The answer is a constant. The place that receives it measures it like a literal, so an answer that does not fit is refused just as
`return 600 .` is.

#demo("examples/ch08/compt_width.low")

A call to a pure `fn` is something `comptime` can compute too. The tool really runs that `fn` at translation time, and only the answer, one
number, is left at run time. For now it runs a `fn` whose inputs and answer are each one integer or `bool`. A `proc`, which may have
effects, is not run. If the run overflows, breaks a contract or does not end within a set number of steps, it is refused with
`E-COMPTIME-NONCONST`, and the message says why.

#demo("examples/ch08/compt_fn.low")

A `fn` that returns an array (#chref("slices")) runs at translation time too. Its elements are sized numbers. The answer becomes a
read-only table baked into the program, so nothing builds the table at run time. Give the answer a name whose length and element
type match the `fn`'s result: `let x array <length> <type> comptime f … .`. It may be a `let` in a body or a module constant. The table
is read only --- writing an element (`set` · `copy` · `swap`) or passing it to a `mut` input is refused. If you need a copy to change,
`copy` it into a `var` made with `lit array <length> <type> _ .`.

#demo("examples/ch08/compt_table.low")

An array's elements may also be `bool` or rows (an array of arrays). A `fn` that returns a slice or a struct runs at translation time
too. A slice answer becomes read-only bytes like an array answer, and a struct answer becomes a value of that struct --- the same as a
`lit` with the computed value in each field. Besides integers and `bool`, an input may be a slice whose bytes are fixed at translation
time: a quoted string, a `text` literal, a list of constants, or an array or slice module constant.

#demo("examples/ch08/compt_values.low")

A struct can go in too: a `lit` with a constant in each field, or the struct answer of a `fn` run at translation time (the struct needs a
byte layout --- no slice or array fields). A slice of structs can be an input and an answer. A `fn` run at translation time builds no
new memory, so a slice answer is a piece of its input or a constant. A struct answer with a 128-bit field keeps the upper half too.

#demo("examples/ch08/compt_structs.low")

A `fn` run at translation time is pure, so it allocates no new memory. It can still build a table: the place for the answer comes from
outside (the place of an array result), and the slice fields in the answer point at pieces of the input text. The tool keeps those pieces
as read-only bytes and makes the fields point there. A struct with slice fields can be an element of a list and of an array result (each
slice field is two words, an address and a length). If you do not know the number of lines in advance, give generous room and count the
filled lines separately. If the room runs out, the write to the next element stops out of bounds, and translation is refused at that
constant's declaration.

#demo("examples/ch08/compt_messages.low")

To keep the source text of a table in a file instead of in the program, use `include_text "<path>"`. When translating, the tool reads
that file and hands its bytes (`slice u8`) to a `fn` run at translation time. The path is read from the folder of this source file, so
the same file is read wherever you build. A path outside that folder (absolute, or with `..`) is refused. `include_text` goes only in the
input place of a `fn` run at translation time --- anywhere else it would look like reading a file while the program runs, so it is
refused with `E-INCLUDE-PLACE`. When the file changes, the `fn` runs again instead of using a kept answer.

#demo("examples/ch08/compt_include.low")

One `fn` run at translation time must finish within 1,000,000 steps. For a table that takes longer, write
`build comptime_steps 50000000 .` in `pkg.low` or give `--comptime-steps 50000000` on the command line (the command line wins). If the
environment variable `LOWENT_CTFE_CACHE` names a folder, the answers are kept there and used again by the next translation. A kept
answer is not used if the `fn`'s definition or its inputs changed, or if the tool version, the target or the step budget differ.

== Common mistakes

Comparison (level 2) binds tighter than `and` (level 1b), so mixing them without parentheses has exactly one meaning.

#demo("examples/ch08/mixcmp.low")

Writing the parentheses anyway spares the reader from recalling the table. Below is the same thing written that way.

#demo("examples/ch08/mixcmp_fixed.low")

#antipattern[Writing the remainder as `%`][
  #demo("examples/ch08/mistake_percent.low")

  The island has exactly the operators in the table. `%` is the remainder in C, but languages disagree on its sign for negative numbers,
  so the same symbol gives different answers. Lowent's remainder is the one name `mod`, and its sign follows the divisor
  (#chref("numbers")). Inside the island it is still called in prefix form.

  #demo("examples/ch08/percent_fixed.low")
]

#antipattern[Writing powers as `^`, or using `math.pow` on integers][
  `^` is not an island operator (`E-CHAR`) --- it means power in some languages and exclusive or in others, so it was left out. Square an
  integer with `mul a. a. .`. And `math.pow` is the power of *floating-point* numbers --- both inputs are `f64`.

  #demo("examples/ch08/mistake_pow.low")

  The canon makes `pow`, `sqrt`, `sin`, `cos`, `exp`, `log` and `fmod` floating-point only (§6.3.9). The ops of `math` declare their inputs as `f64`, so an integer is refused with `E-TYPE-ARG` --- the argument type does not match. Until 2026-09-16 it was not: `square 5` answered 5 instead of 25, and neither translation nor the run said
  anything --- a quiet wrong answer. Write an integer power as multiplication, and convert with `cast f64 n. .` to compute in floating point.
]

  #demo("examples/ch08/pow_fixed.low")

#misconception[Nested calls must always be parenthesised][
  #demo("examples/ch08/noparen.low")

  Parentheses are decoration. Where an inner call ends is decided by that call's own stop. So `add 1 mul a. b. . .` and
  `add 1 (mul a. b. .) .` are the same expression. Up to edition 1.7 it was *the number of arguments each op takes* that decided the
  end, so you had to know an op's declaration to read an expression. Now the stops are enough --- write parentheses only where you want
  a grouping to stand out in a long expression.
]

== This chapter's syntax at a glance

#dtable(
  columns: 3,
  id: "expr-glance",
  caption: [Expression syntax --- shape · meaning · why it looks this way],
  [*Shape*], [*Meaning*], [*Why*],
  [`add a. mul b. c. . .`], [prefix notation --- no precedence], [the stops say the whole order of evaluation],
  [`expr a. + b. * c. .`], [infix island --- `*` `/` bind tighter than `+` `-`], [only what school arithmetic taught goes infix],
  [`expr (a. + b.) * c. .`], [grouping inside the island], [parentheses group; they are not values],
  [`expr (lo. le x.) and (x. le hi.) .`], [comparisons mixed with logic], [parentheses per comparison --- no table to recall],
  [`expr twice a. . + 1 .`], [calling an op inside the island], [a call closes with its own stop --- parentheses are decoration],
  [`expr mod a. 10 . + k. .`], [an operation not in the table (`mod`, bitwise, `min`)], [the island never grows --- call it prefix],
  [`expr 0 - a. .` · `neg a. .`], [flip the sign], [the island has no unary operators (`E-EXPR-UNARY`)],
  [`and` · `or`], [short-circuit --- the right side is skipped once the left decides], [put the condition that makes the right side safe first],
  [`size_of u32 .`], [bytes in one value of a type (at compile time)], [generic code counts its cost honestly],
  [`comptime add 2 3 .`], [evaluate at compile time], [folded branches are still type-checked],
)

#recap[
  The default for expressions is prefix notation, which has no precedence. The `expr` island allows only arithmetic, comparisons, `and` and
  `or` in infix; op calls inside it close with their own stops, and there are no unary operators. When mixing comparisons with `and` or `or`,
  parenthesise each comparison. Short-circuiting `and` and `or` can guard the right-hand side, and `size_of` and `comptime` are computed at
  translation time.
]
