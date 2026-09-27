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
`return pipe s do … end .` --- the final stop belongs to the `return` statement (`end` closes only the `pipe` block).

Drawn as a tube that elements flow through:

```text
 s = "a1b22"
               ┌─────────────────┐            ┌───────┐
  a 1 b 2 2 ─▶ │ filter is_digit │ ─▶ 1 2 2 ─▶ │ count │ ─▶ 3
               └─────────────────┘            └───────┘
                 stage: filters                 terminal: ends the flow and gives a value
```

Elements pass through the tube one at a time. No separate "array of just the digits" is built in between.

== Stages and terminals

#dtable(
  columns: 4,
  id: "pipe-words",
  caption: [The words of `pipe` --- element type `t`, accumulator type `a`],
  [*Word*], [*Shape of the op*], [*What it does*], [*End · empty flow*],
  [`filter <op>`], [`t → bool`], [Passes only elements for which the op is true], [---],
  [`map <op>`], [`t → u`], [Passes the op's answer for each element; the type may become `u`], [---],
  [`take <n>`], [---], [Passes the first `n`], [*stops walking* after `n`],
  [`skip <n>`], [---], [Drops the first `n`], [---],
  [`enumerate <op>`], [`(u64, t) → u`], [Gives the position (from 0) and the element to the op], [---],
  [`zip <slice> <op>`], [`(t, t2) → u`], [Gives the element and the other slice's element at the same place to the op], [ends when the shorter side ends],
  [`scan <init> <op>`], [`(a, t) → a`], [Passes the accumulator *after* each step (the initial value is not passed)], [---],
  [`collect into <buffer>`], [---], [*Terminal.* Fills the buffer from the front and gives *how many it stored* (`u64`)], [a buffer too short is refused at translation when the lengths are known (`E-COLLECT-FULL`), otherwise the run stops],
  [`fold <init> <op>`], [`(a, t) → a`], [*Terminal.* Gives the last accumulator (`a`)], [empty: the initial value],
  [`count`], [---], [*Terminal.* Gives the number of elements (`u64`)], [empty: 0],
  [`any <op>`], [`t → bool`], [*Terminal.* Is any element true], [stops at the first true · empty: false],
  [`all <op>`], [`t → bool`], [*Terminal.* Are all elements true], [stops at the first false · empty: true],
)

The op given to `filter`·`any`·`all` is a *predicate* and must answer `bool`. An op that answers a number is refused with
`E-PIPE-PRED` --- there is no rule here that reads a nonzero number as true (the same reason as `cast` in #chref("named-types")).

Used as a value --- `return pipe … end .` --- a `pipe` is worth what its terminal gives. Each stage hands elements on to the
next line, and the terminal takes them last and makes one value.

```text
 xs = [1, 5, 2, 7]
      │
      ▼  filter big      (big = gt x 2)       5, 7          1 and 2 are dropped here
      ▼  map dbl         (dbl = x + x)        10, 14
      ▼  collect into out                     out = [10, 14, …]   answer = 2 (how many stored)
```

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
  The specification's examples also write `effects none`. `collect into` does not *require* `effects state`, but if you write it the processor counts the
  write to caller storage, so no "declared but not performed" warning appears --- the same judgement it makes for writes through a `mut` or `mut_ref`
  parameter. Either way, the caller knows writes may happen where it hands over a `mut slice` (#chref("references")).
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

== The built-in `map` · `filter` with the same names

`map` and `filter` also exist outside `pipe`. These are not stages but *built-in operations that copy one slice into another in a
single statement*, and they take three arguments --- `map <sink> <op> <source> .` · `filter <sink> <op> <source> .`.

```lowent
module sink_map .

fn dbl input a u8 . output u8 . do return wrap_add a a . end

proc doubled input xs slice u8 . input out mut slice u8 . output u64 . effects none .
do
  map out dbl xs .
  return 0 .
end
```

Putting `[1,2]` into a three-slot `out` gives `[2,4,0]`. The other way round, putting `[1,2,3]` into two slots stops the run as the third is written ---
a full sink never drops the rest in silence (the same as `collect into`). The sink must be a `mut slice` (`E-MAP-SINK`) and
the element a scalar (`E-MAP-ELEM`). The word is the same, but inside `pipe` it takes one op --- the number of arguments tells which is meant.

== Common mistakes

#antipattern[Writing an expression in a stage to imitate a lambda][
  #demo("examples/ch24/mistake_lambda.low")

  Carrying over `filter(x => x > 2)` from another language easily gives `filter gt 2`. A stage takes *one named op*, so this is `E-FOLD-OP`
  (the diagnostic only says "no such op"). Naming a condition may feel like extra work, but the name `over2` becomes the explanation of that
  line, and the same condition can be reused in other `pipe`s.

  #demo("examples/ch24/lambda_fixed.low")
]

#antipattern[Collecting a `map` that produces wide values into a narrow buffer][
  #demo("examples/ch24/mistake_widecollect.low")

  `times1000` produces `u64`, but `out` is a `u8` buffer. A narrowing that loses value happens only where it is written (§6.2.5), so this
  is refused with `E-TYPE-COLLECT`. Until 2026-09-16 it passed and 1000 and 2000 were stored silently as 232 and 208. If narrowing is
  needed, write `narrow`, `narrow_wrap` or `narrow_sat` inside the op given to `map`, so the place where it may stop is visible.
]

#antipattern[Swapping the accumulator and the element in a `fold` op][
  #demo("examples/ch24/mistake_foldorder.low")

  `fold` gives its op *the accumulator first and the element second*. `add_small_swapped` takes them the other way round, so `x` receives
  the accumulator and `acc` receives the element. Each step is `acc = op(acc, element)`, so the first input must have the same type as the
  output; here they are `u8` and `u64`, and the program is refused with `E-FOLD-ORDER`. Always write the op of `fold` or `scan` in the
  order `input acc … . input x … .`.
]

#antipattern[Using a stage op that can stop, in a `fn`][
  #demo("examples/ch24/mistake_stageeffect.low")

  A `pipe` is a loop that calls its stage ops, so the effects of a stage op spread to the op containing the `pipe`. `nonzero` may `panic`,
  so `count_checked` performs `panic` too, and as a `fn` it is `E-EFFECT-CALC`. Write it as `proc … effects panic .`, or instead of stopping,
  use a pure op that filters out the elements that do not meet the condition.
]

#misconception[`collect into` drops the rest when the buffer is full][
  #demo("examples/ch24/short_buffer.low")

  From `[1,3,4]` two elements remain (3, 4), and they fit in two slots. `[3,4,5,6]` leaves four, so the run *stops* as the third is stored.
  It used to end quietly after two, and nobody learned that the other two were gone. When both lengths are known at translation (an
  `array u8 5` input, say), there is nothing to stop: it is refused with `E-COLLECT-FULL`. A `pipe` does not allocate, so it does not grow the
  buffer either. To keep only what fits, *write* `take 2`.
]

== This chapter's syntax at a glance

#dtable(
  columns: 3,
  id: "pipe-glance",
  caption: [`pipe` syntax --- shape · meaning · why it looks this way],
  [*Shape*], [*Meaning*], [*Why*],
  [`pipe xs do … end`], [scan the source `xs` once], [the language owns the skeleton (index, end test)],
  [`filter over2 .` · `map dbl .`], [keep · transform --- pass a named op], [no lambdas --- the name is the explanation],
  [`take 2 .` · `skip 1 .`], [only the first few · drop the first few], [read only as much as needed],
  [`enumerate idxadd .` · `zip ys addb .`], [pass the index or partner as op arguments], [no tuples are built],
  [`scan 0 addb .` · `fold 0 addu .`], [emit running values · accumulate into one value], [the op takes the accumulator first, then the element],
  [`count .` · `any is_zero .` · `all under10 .`], [terminators that produce a value], [usable as `return pipe … end`],
  [`collect into out .`], [store into the caller's buffer], [a `pipe` never allocates --- a buffer too short is refused or stops the run],
  [exactly one terminator, at the end], [a stage after it is `E-PIPE-NO-TERMINAL`], [the end of the flow is in one place],
  [a word like `sort`], [does not exist --- `E-PIPE-STAGE`], [operations that cannot fuse were left out],
)

#recap[
  `pipe <source> do … end` passes through stages (`filter`, `map`, `take`, `skip`, `enumerate`, `zip`, `scan`) and ends with one terminal (`collect into`,
  `fold`, `count`, `any`, `all`). Stages take named ops, and the buffer to collect into is given by the caller. Walking once with no intermediate arrays is the
  definition, and operations that cannot be fused are not in the vocabulary. `any`, `all` and `take` read only as much as needed.
]
