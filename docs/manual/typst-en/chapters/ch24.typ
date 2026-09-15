#import "../../typst-ko/lib.typ": *

= `pipe` --- one line for each thing you mean to do

#chapter-toc()

#prereq(
  ([#chref("control"), Flow], [`for` walks slices and counting loops use `while`]),
  ([#chref("slices"), Sequences], [elements can be written only through a `mut slice`]),
  ([#chref("generics"), Generics], [named ops are passed instead of values]),
)

#deepqa[
  When #chref("control") asked "how do you write a loop counting from 0 to `n` with `for`?", what did it say takes care of filtering, counting and collecting
  over a slice?
][
  `pipe`. Counting loops are written with `while` and `var`, and filtering, transforming and collecting over a slice with `pipe`. This chapter covers that
  `pipe`.
]

#why[
  A loop walking a slice almost always has the same skeleton: keep an index, check for the end, take an element, test it, accumulate something, advance the
  index. Written by hand, *what you mean to do* ("keep only the digits and count them") gets buried among indices and counters, and off-by-one mistakes happen
  there. `pipe` hands the skeleton to the language and lets people write only what they mean, one word per line. And it still walks once, just like the
  hand-written loop. As the last chapter of Part VI on abstraction, it shows an abstraction that hides no cost.
]

#organizer[
  You will learn the shape `pipe <source> do <stages…> <terminal> end` and its seven stages and five terminals. You will pick up how to pass named ops to
  stages, how to collect into the caller's buffer with `collect into`, and that `fold`, `any`, `all` and `take` read only as much as needed. You will also
  understand that walking once without intermediate arrays is not an optimisation but *the definition* of `pipe`.
]

#chapter-questions()

== The same work, two shapes

#demo("examples/ch24/digits.low")

The two ops give the same answer. `digits_loop` has a person line up the counter `n`, the index `i`, the end condition and the increments. `digits_pipe` is
two lines.

#idx("pipe")
- `filter is_digit .` --- keeps only elements for which `is_digit` is true.
- `count .` --- counts what is left. The word that ends the flow (the *terminal*) is exactly one.

`pipe` is a statement, but when it ends with a value-producing terminal (`count`, `fold`, `any`, `all`) it can be used like an expression, as in
`return pipe s do … end .`.

== Stages and terminals

#dtable(
  columns: 3,
  id: "pipe-words",
  caption: [The words of `pipe`],
  [*Position*], [*Word*], [*What it does*],
  [stage], [`filter <op>`], [Passes only elements for which the op is true],
  [], [`map <op>`], [Applies the op to each element],
  [], [`take <n>` · `skip <n>`], [Passes · drops the first `n`],
  [], [`enumerate <op>`], [Gives the position and element to the op],
  [], [`zip <slice> <op>`], [Combines with the matching element of another slice through the op],
  [], [`scan <init> <op>`], [Passes the accumulated value on as the element],
  [terminal], [`collect into <buffer>`], [Puts the remaining elements into the buffer],
  [], [`fold <init> <op>`], [Accumulates into a single value],
  [], [`count`], [Counts the remaining elements],
  [], [`any <op>` · `all <op>`], [Is any true · are all true],
)

#demo("examples/ch24/stages.low")

- `big_doubled` keeps only elements greater than 2 (`filter`), doubles them (`map`) and puts them into `out` (`collect into`). In the result `[6,8,10,0,0]`,
  positions not filled stay as they were. The buffer is *given by the caller*; `pipe` does not allocate.
- `fold 0 addu` in `sum_big` starts from 0 and accumulates with `addu`.
- `has_zero` is `any is_zero`, and `all_small` is `all under10`.
- `middle` puts the middle two elements into `out` with `skip 1` and `take 2`.

A stage is given a *named op*. This language has no anonymous functions (lambdas). A stage op takes one element and receives no capabilities. The ops of `fold`
and `scan` take the accumulator and an element.

Without tuples, how are positions or pairs handled? The pair is *not built but passed to the op as arguments*.

#demo("examples/ch24/pairs.low")

`enumerate idxadd` gives `idxadd` the position and the element. `zip ys addb` gives `addb` an element of `xs` and the element of `ys` at the same position; when
the shorter one ends the whole flow ends, so pairs always match. `scan 0 addb` passes the running sum on as the element. Since pairs are never built and
immediately taken apart, there is no hidden allocation.

#qa[
  An op that uses `collect into` has `effects none` in its head. It changes the caller's buffer --- is that not an effect?
][
  The specification's examples also write `effects none`, and the processor in this edition does not count the writes of `collect into` as the `state` effect
  (declaring `state` produces a "declared but not performed" warning). On the other hand, writing to the caller's slice with `set (index xs i) …` requires
  `effects state`. The two judgements disagree, and this book regards that as a defect. Either way, the caller knows writes may happen where it hands over a
  `mut slice` (#chref("references")).
]

== Walking once is the definition

A single `pipe` runs *in one pass*, and no intermediate arrays are created between stages. This is not an optimisation the processor may try but the definition
of `pipe`, and a processor that creates intermediate arrays does not conform.

Why make it the definition? If fusion were an optimisation, "how far fusion goes" would differ between processors, and users would hit *cliffs* --- change one
line and suddenly an intermediate array appears and things slow down. That cliff is invisible in the source. Lowent *left operations that cannot be fused out
of the stage words entirely*. That is why operations needing to see everything, such as sorting, are not stages.

#demo("examples/ch24/bad_stage.low")

The list of stages is closed. `pipe` being a statement rather than an expression has the same reason: as an expression, stages could be detached and passed as
values, and where one stream ends would not be visible in the source. `do … end` makes that boundary visible.

== Reading only as much as needed

`any` stops at the first element giving true, `all` at the first giving false, and `take n` after passing `n` elements. Elements after the stop are not read,
and stages are not run for them. `has_zero [4,0,9]` never looks at 9. Only because this property is written into the meaning can `pipe` handle sources without
an end.

A terminal ends the flow. A stage after the terminal is rejected.

#demo("examples/ch24/no_terminal.low")

#misconception[`pipe` is convenient but slower than a hand-written loop][
  In many languages iterator chains leave intermediate objects or indirect calls behind, or hope the optimiser removes them. For `pipe`, walking once is the
  definition, and stage ops are monomorphised direct calls. One `pipe` lowers to one loop, and its costs arise at the same places as in a hand-written loop.
  The words were chosen so that the convenient way and the fast way do not diverge.
]

#recap[
  `pipe <source> do … end` passes through stages (`filter`, `map`, `take`, `skip`, `enumerate`, `zip`, `scan`) and ends with one terminal (`collect into`,
  `fold`, `count`, `any`, `all`). Stages take named ops, and the buffer to collect into is given by the caller. Walking once with no intermediate arrays is the
  definition, and operations that cannot be fused are not in the vocabulary. `any`, `all` and `take` read only as much as needed.
]
