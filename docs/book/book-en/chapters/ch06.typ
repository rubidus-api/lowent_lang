#import "../../book/lib.typ": *

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

- `let <name> <type> be <expr> .` --- immutable. Once set, it does not change.
- `var <name> <type> be <expr> .` --- mutable. It is changed with `set <name> <expr> .`.

#demo("examples/ch06/sumto.low")

`total` and `i` change as the loop runs, so they are `var`s. Only values that change are made with `var`; the rest are `let` by default.
Using `set` on a `let` is rejected.

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

== Write the type, or leave it to the value

A local's type may be written or left out. When left out, the value decides the type.

#demo("examples/ch06/infer.low")

In `let b be add a 1 .`, `a` is a `u8`, so `b` is a `u8` too. That is why adding 1 to 255 overflows `u8` and stops. If the type cannot be
determined from the value, it is rejected and must be written.

This book usually writes the type. The width is the boundary of overflow (#chref("numbers")), so when the type is visible in the source,
the places that can stop are visible with it. Types are left out only for short intermediate values or where the type is obvious.

== No name without a value

There must be a value after `be`. There is no way to make a name first and give it a value later.

#demo("examples/ch06/novalue.low")

This is not a blank left by mistake. The author believes they wrote the value `.5`. But `.5` is not a floating-point literal --- the
free-standing full stop closes the form, and nothing is left after `be`. The old tool quietly put 0 there, and a value that appears nowhere
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

#recap[
  There are two kinds of local, `let` (immutable) and `var` (mutable), and `set` works only on a `var`. The type may be written or left to
  the value, but a value after `be` is required. A local lives until the end of its block, and a live name cannot be made again even in an
  inner block. `if` is a statement that yields no value.
]
