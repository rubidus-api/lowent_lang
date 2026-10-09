#import "../../typst-ko/lib.typ": *

= Locals --- `let` and `var`

#chapter-toc()

#prereq(
  ([#chref("surface"), The surface], [shadowing is forbidden]),
  ([#chref("ops"), Ops], [mutation confined inside an op counts as pure]),
)

#deepqa[
  `running_total` in #chref("ops") accumulated with `var` and was still a `fn`. Why was that allowed?
][
  Because purity is observational. `total` is a local that lives only inside the op, so the caller cannot see it change. Only writes the
  caller can see (through a `mut` parameter) count as effects. This chapter covers the two words that make locals and the span over
  which a local lives.
]

#why[
  A value that changes is expensive for the reader, who has to ask again at every use "what is this value here?". Lowent separates names
  that do not change from names that do *by word*, and closes the door on naming something without a value. Before using loops and
  branches in #chref("control"), this chapter fixes the containers that hold values inside them.
]

#organizer[
  You will learn that `let` is immutable and `var` is mutable and changed only with `set`. You will pick up when to write the type and
  when to leave it out, and why a binding without a value is rejected (especially the `.5` trap). You will also see that a local lives
  only inside its block, that a live outer name cannot be reused inside, and that `if` is a statement that yields no value.
]

#chapter-questions()

== The word says whether it changes

#idx("local")
A name that holds a value in an op body is a *local*. There are only two kinds.

- `let <name> <type> <expr> .` --- immutable. Once set, it does not change.
- `var <name> <type> <expr> .` --- mutable. It is changed with `set <name> <expr> .`.

#demo("examples/ch06/sumto.low")

`total` and `i` change as the loop runs, so they are `var`s. Only values that change are made with `var`; the rest are `let` by default.
Using `set` on a `let` is rejected. The other way round, a `var` that never changes gets `W-VAR-NEVER-SET` --- write it as a `let`
(RFC-0113 O4).

#demo("examples/ch06/immutable.low")

According to the long explanation in the diagnostic, `let` and `var` once meant the same thing. Two spellings for one meaning means one of
them says nothing. Today `let` *promises* immutability and the compiler checks the promise.

#qa[
  Can the elements of a slice bound with `let` be changed?
][
  What `let` forbids is *putting a different value into the name*. Whether the elements of a slice can be written is decided by the
  slice's type --- `mut slice` allows writing elements and `slice` only reading (#chref("slices")). Immutability of the name and
  immutability of the bytes it points to are different questions.
]

== The type goes in front of the value

A binding is `let <name> <type> <value> .`. The type stands right after the name and before the value, and it is *always written* --- the
processor never guesses a type.

#demo("examples/ch06/infer.low")

In `let b u8 add a. 1 . .`, `b` is a `u8`, so adding 1 to 255 overflows `u8` and stops. The width is the boundary of overflow
(#chref("numbers")), so when the type is visible in the source, the places that can stop are visible with it.

Leaving the type out --- `let x 300 .` --- is rejected with `E-LET-NOTYPE`. When guessing was allowed, that bare literal slipped past the
width check and an op returning `u8` answered 300. The old form with `be` between the name and the type, `let x be u8 4 .`, is `E-LET-BE` --- `be` is no longer a word of the language. A long
type gets a name with `def type`.

#demo("examples/ch06/mistake_be.low")

== No name without a value

There must be a value after the type. There is no way to make a name first and give it a value later.

#demo("examples/ch06/novalue.low")

This is not a blank left by mistake. The author believes they wrote the value `.5`. But `.5` is not a floating-point literal --- the
free-standing full stop closes the form, and only the type is left after the name. The old tool quietly put 0 there, and a value that appears nowhere
in the source got into the program. Now it is rejected, and the diagnostic names the trap. A floating-point half is written `0.5`.

#misconception[An uninitialised variable is zero][
  In C, an uninitialised local has an unspecified value. Some languages fill in zero. Lowent has neither --- there is no such thing as an
  uninitialised variable. There is no moment at which a value is undecided, so there is no defect that reads at that moment.
]

== How long a local lives

A local lives until the end of the block it was made in. When the block ends, the name can be used again.

#demo("examples/ch06/scope.low")

The two `if` blocks each make a `doubled`. When the first block ends, the first `doubled` is gone, so the second block may make one with
the same name. There is no moment at which both names are *alive at once*.

On the other hand, an inner block making a name that is still alive outside is rejected.

#demo("examples/ch06/outer.low")

Inside the inner block `t` would be 2, and outside it 1 again. When the same letters mean different values on different lines, the reader
thinks of the wrong value. It is the same rule as the parameter shadowing in #chref("surface") --- the name space is flat.

== `if` yields no value

`if` is a statement. It cannot stand where an expression is expected.

#demo("examples/ch06/ifvalue.low")

To decide a value per branch, do what the diagnostic suggests: make a `var` with a default and `set` it inside the branches, or `return`
from each branch. When the value is one of several cases over and over, `match` (#chref("control")) is the better tool.

#qa[
  If `if` could be an expression, couldn't we have fewer `var`s?
][
  We could. In exchange, blocks would enter expressions, and on meeting a `return` or `break` inside such a block you would have to work
  out where that expression leaves to. Lowent chose to separate expressions from statements. Expressions make values; statements change
  flow.
]

== Common mistakes

#antipattern[Misspelling the name in a `set`][
  #demo("examples/ch06/mistake_typo.low")

  `set` only puts a new value into a `var` that *already exists*. Some languages quietly create a new variable when you assign to an
  unknown name; here it is rejected with `E-IR-UNDEF`. If a typo became a new variable, the real one would never change and the program
  would silently compute the wrong answer. That is why declaring (`var`) and changing (`set`) are different words.
]

#antipattern[Assigning with `=`][
  #demo("examples/ch06/mistake_equals.low")

  In most languages `=` means assignment, while in mathematics it means equality. One symbol switching between two meanings has produced
  bugs like `if (x = 0)`. Lowent uses no symbol for either: declaring is `let`/`var … `, changing is `set`, and asking whether two
  values are equal is `eq`. `=` is not a character the language knows at all, hence `E-CHAR`.
]

#antipattern[Naming a local after a common English word][
  #demo("examples/ch06/mistake_localname.low")

  Names such as `count`, `len`, `min`, `max`, `ok` and `sqrt` are already core ops. The name space is flat, so if a local took one
  of them, `f count data` would group differently depending on whether `count` is the core op call or the local. The same letters would
  build a different tree, so `E-NAME-BUILTIN` rules it out. Use names like `n`, `total` or `size`.
]

#antipattern[Reading a local outside the block that declared it][
  #demo("examples/ch06/mistake_outside.low")

  `big` was declared inside the `if` block, so it disappears when the block ends. Reading that name outside the block is rejected with
  `E-NAME-SCOPE`; it used to *quietly give 0* on the path that never entered the block --- a value that appears nowhere in the source.
  Declare a value you need after the block *before* the block.

  #demo("examples/ch06/outside_fixed.low")

  Now the 0 is a default written in the source. The answer is the same, but *where the 0 comes from* is visible.
]

== This chapter's syntax at a glance

#dtable(
  columns: 3,
  id: "locals-glance",
  caption: [Local syntax --- shape · meaning · why it looks this way],
  [*Shape*], [*Meaning*], [*Why*],
  [`let x T e .`], [a name that never changes], [immutable by default --- only changing values stand out],
  [`var x T e .`], [a name that may change], [say that it will change at the moment you declare it],
  [`set x e .`], [put a new value into a `var`], [declaring and changing are different words --- a typo never becomes a new variable],
  [`let x e .`], [let the value decide the type], [only for short intermediate values --- the width is the overflow boundary],
  [the value after the type], [always required], [there is no such thing as an uninitialised variable],
  [the end of a block], [locals declared inside it disappear], [names live close to where they are used],
  [re-declaring an outer name inside], [rejected (`E-NAME-SHADOW`)], [the same letters point to one value only],
  [`eq a. b. .`], [ask whether two values are equal], [there is no `=` --- assignment and equality never mix],
)

#recap[
  There are two kinds of local, `let` (immutable) and `var` (mutable), and `set` works only on a `var`. The type may be written or left to
  the value, but a value after the type is required. A local lives until the end of its block, and a live name cannot be made again even in an
  inner block. `if` is a statement that yields no value.
]
