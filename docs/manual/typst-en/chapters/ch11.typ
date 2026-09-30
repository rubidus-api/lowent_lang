#import "../../typst-ko/lib.typ": *

= Types that hold answers --- `option` and `result`

#chapter-toc()

#prereq(
  ([#chref("control"), Flow], [`guard` and `panic`]),
  ([#chref("structs-enums"), Aggregates], [enums carrying values, and `match`]),
)

#deepqa[
  What does `case rect w h .` in #chref("structs-enums") do? And what happens when a `match` leaves out a variant?
][
  When the variant is `rect`, it binds the two values the variant carries to `w` and `h`. Leaving out a variant is rejected with
  `E-MATCH-INEXHAUSTIVE`. The `option` and `result` of this chapter behave like two-variant enums the language made in advance --- a value is
  there or not, it succeeded or failed.
]

#why[
  The habit of signalling failure with a special value such as −1 or a null pointer makes it impossible to tell from the source whether
  "this −1 is an error or just −1". And forgetting the check goes unnoticed. `option` and `result` move that question into the type. This
  chapter sits in the middle of Part III because nearly every op that takes data out --- a lookup, a conversion, a parser --- returns "may be
  missing" or "may fail".
]

#organizer[
  You will learn to make an `option` with `some` and `none`, and to use it by asking and taking out, by giving a fallback with `value_or`, or
  by splitting with `match`. You will see that taking out without checking stops. You will also see that `result` pairs with an `errors`
  clause, that `try` passes a failure upwards, and that the `else_none` and `else_error` tails cross between the two. Finally you will settle
  the criterion that separates the three ways of reporting failure --- `result`, `option` and `panic` --- and see or-patterns, nested patterns
  and `match` folded at translation time.
]

#chapter-questions()

== `option` --- a value, or none

#idx("option")
`option t` is either a value of `t` (`some v`) or nothing (`none`). Together with `result`, seen later, it helps to draw them as *two kinds of box*
that hold a value.

```text
 option u64         ┌──────────┐              result u64 e     ┌──────────────┐
                    │ some 20  │  a value                       │ ok 20        │  a value
                    ├──────────┤                               ├──────────────┤
                    │ none     │  empty                         │ error bad    │  it failed ---
                    └──────────┘  (nothing was there)           └──────────────┘  and carries why (bad)
```

`none` is not something strange but the normal answer "there was nothing", and `error` is the answer "what I tried failed". Either way, the value
inside cannot be used *before the box is opened*.

#demo("examples/ch11/lookup.low")

`find`, the producer, wraps values with `return none .` and `return some (mul k 10) .`, and the VM shows the results as `some 20` and `none`.
The receiver can use it three ways.

- `find_or` --- `value_or (find k) 99` gives the value if there is one, and 99 otherwise.
- `find_asked` --- asks first with `guard is_some r . else …` and takes the value out with `some_value r`.
- `find_match` --- splits with `match` into `case some v .` and `case none .`. The two arms cover every case.

`find_raw` takes the value out without asking. Translation passes, but execution stops at 7, which has no value (`E-VM-NONE`). Taking a value
out is a *partial operation*. On which paths a value exists is something the author knows and the processor cannot always know, so
translation does not block it. Instead it is not silent when wrong --- it does not hand out 0 and carry on.

#qa[
  If I put an expensive computation in `value_or`'s default, is it computed every time?
][
  No. The default is computed *only when there is no value*. `value_or (some 7) (div 1 0)` is 7, and no division by zero happens. So you may
  put a computation that can fail in the default. This behaviour was once the other way round and was fixed to match the specification.
]

== `result` and the `errors` clause

#idx("result")
`result t e` is either a successful value (`ok v`) or an error (`error <variant>`). The error type `e` is usually an `enum`. And an op that
#idx("errors clause")
returns a `result` writes *when it produces which error* in its `errors` clause.

#demo("examples/ch11/halve.low")

`halve`'s head lists two errors. `errors too_big gt a 200 .` is the promise "if `a` is greater than 200, produce `too_big`". This clause is a
*contract on the way out*. Returning normally while the condition holds, or returning an error not written, is a contract violation. Code that
returns an error not written is rejected at translation.

#demo("examples/ch11/undeclared.low")

`too_long` is a variant of `parse_error`, but it is not in `first_byte`'s `errors` clause. A caller reading the head would believe handling
`empty` is enough. When what is written and what is produced differ, one side meets failures it never saw and the other handles failures that
never come.

== `try` --- passing failure upwards

#idx("try")
Writing the failure check by hand every time makes code long, and long code skips checks. `try` takes a `result` and, on success, takes out
the value; on failure it *returns that error as is and leaves the op*. `halve_plus_one` in `halve.low` has that shape.

```lowent
let v be u8 try halve a .
return ok (add v 1) .
```

Notice that `halve_plus_one` writes `errors` in its own head too. To pass an error up with `try`, it must itself be able to return that error,
and it must say so in its contract. There is no path by which a failure silently disappears. To handle the failure here instead of passing it
up, ask with `is_error` and take out with `ok_value`, as `halve_or_zero` does.

#misconception[`try` is the `try` of exception handling][
  Java's or C++'s `try` is a place that catches exceptions thrown anywhere in a block. Lowent's `try` is attached to *one expression* and marks
  that expression's failure as *passed upwards*; it is closer to Rust's `?`. There is no throw-and-catch control flow. Failures always come
  back as values, and where they can be passed on is written in the source.
]

== Binding `else` --- take the content, or leave

#idx("else")
When you handle a failure *right here* instead of passing it up, the check-then-take-out pair of lines can be one line. Write the
binding's type as the content's type and attach what to do when it is empty with `else`.

#demo("examples/ch11/bindelse.low")

- `let at be u64 find xs k . else return 99 .` --- `find` returns an `option u64`. If there is a value, that `u64` is bound to `at`;
  if not, control goes to `else`. A `result` works the same way --- an error goes to `else`.
- `else` *must leave* (`return` · `break` · `continue` · `panic`). So on the lines that use `at` the value has already been taken out,
  and using it unchecked cannot happen.
- Where stopping is acceptable, write `. else panic "…" .`. The place that may stop is visible once in the source, and in a pure `fn`
  the effect rules refuse it.
- Binding an `option` as its content without `else` is refused --- the diagnostic tells you to add `else` or to declare the binding
  `option …`. Forgetting that something can fail is caught at translation.
- To pass the error up unchanged, use `try`. Use `else` when you want to handle it differently *here*.

== Crossing between the two channels

Sometimes the calling op and the called op use different channels. A tail on `try` changes the container.

#demo("examples/ch11/tails.low")

#dtable(
  columns: 3,
  id: "optres-tails",
  caption: [Changing channel with a `try` tail],
  [*Shape*], [*Direction*], [*What is lost or gained*],
  [`try <expr> else_none`], [`result` → `option`], [The error is *discarded*; why it failed is no longer said],
  [`try <expr> else_error <variant>`], [`option` → `result`], [Absence *gets a name*],
)

A `try` with a tail also changes the type. The type of `try (halve a) else_none` is `option u8`, not `u8`. That is why `maybe_half` returns it
as is, and why putting it into a value type, as in `let v be u8 try … else_error …`, is rejected.

`else_none` is a choice that throws information away. It is convenient, so it easily becomes a habit, but from that moment the caller can no
longer ask "why". Throw it away only where it is worth throwing away.

== Three ways to report failure

#dtable(
  columns: 3,
  id: "optres-channels",
  caption: [Three ways to report failure],
  [*What*], [*What it says*], [*What the caller does*],
  [`result t e`], [A failure that can be fixed], [Asks which it is and handles it, or passes it up],
  [`option t`], [There is no value], [Asks whether it exists and takes it, or gives a fallback],
  [`panic` · contract violation], [A promise was broken], [Cannot handle it; the program stops],
)

The question that separates the three is "what can the caller do?". If a file is missing, another file can be tried, so it is a `result`. If
what you look for is not in the list, it simply is not there, so it is an `option`. If the caller broke a contract, the promise is already
broken and cannot be fixed, so execution stops. `panic` does not unwind; there is no way to catch it midway and carry on. How to design an op's
failures on this criterion is revisited in #chref("errors-design").

== Combining and nesting patterns

Now that `option`, `result` and `enum` have all appeared, `match` patterns can be used more widely.

#demo("examples/ch11/patterns.low")

#idx("pattern")
- *Or-patterns.* `case red or green .` is taken if either matches. Each alternative counts towards exhaustiveness, so once `blue` is handled no
  `_` is needed. When or-ing variants that carry values, *every alternative must bind the same names* --- `combine` takes out `l` and `r` whether
  the variant is `plus` or `times`. Mismatched names are `E-MATCH-ORBIND`.
- *Nested patterns.* `case ok (some x) .` splits the `option` inside a `result` in one go. Nested patterns short-circuit, so if it is not `ok`
  the inner part is never looked at. That is why `error` never tries to take out a value and stop.
- *Folded at translation time.* If the value being split is a translation-time constant --- a literal, `comptime <expr>`, `config <name>` --- the
  `match` folds to the one matching arm, with no comparison at run time. Dead arms are still type-checked. That is the difference from C's
  `#ifdef` (#chref("build-test")).
- *Ranges cover a type.* The two ranges in `half` cover 0 … 255 of `u8` without a gap, so it is exhaustive without `_`. A gap is
  `E-MATCH-INEXHAUSTIVE`; overlapping ranges are `E-MATCH-REDUNDANT`.

An arm after `_` is rejected.

#demo("examples/ch11/arm_after_wild.low")

`_` has already taken everything, so the arm after it can never run. A dead arm is an error, not a warning --- a `match` where each case does not
appear exactly once when read hides defects.

== Common mistakes

#antipattern[Using an `option` in arithmetic as if it were a number][
  #demo("examples/ch11/mistake_optarith.low")

  `find k` does not give back a `u64`; it gives back "a box that may or may not hold a `u64`". You cannot add 1 to a box. In
  other languages a null flows into the calculation and blows up much later; Lowent stops you right here with `E-TYPE-RETURN`.
  There are three fixes: supply a stand-in with `value_or (find k) 0`, ask with `is_some` and take it out with `some_value`, or
  split with `match`. Which one to pick depends on "what should happen when it is absent".
]

#antipattern[Forgetting `some` in an op that returns an `option`][
  #demo("examples/ch11/mistake_nosome.low")

  Once the head says `output option u64`, the value you return must be a box too. `none` is a box, but `mul k 10` is a bare
  number, so this is `E-TYPE-RETURN`. Some languages wrap the value for you; Lowent does not. Writing `return some (mul k 10) .`
  spells out "it is there", so the reader sees both branches.
]

#antipattern[Using `try` in an op that does not return a `result`][
  #demo("examples/ch11/mistake_trynoresult.low")

  `try` *passes failure upwards*, so this op must be able to return that failure itself (§6.5.8(2)). `plus` returns only a `u8` and has
  no `errors` clause, so it is refused with `E-TRY-NORESULT`. Until 2026-09-16 it passed: `plus 250` printed `err too_big` where a `u8`
  belongs, and the native build would not even compile. Adding a tail (`else_none`, `else_error`) or handling it here (`is_ok` and the
  rest of §6.5.8(3)) keeps the op free of `result`; to pass the failure up, give the head a `result` output and an `errors` clause.

  #demo("examples/ch11/trynoresult_fixed.low")
]

Nested patterns count towards exhaustiveness too. An outer tag is covered when *its inner pattern is itself exhaustive*.

#demo("examples/ch11/nestedwild.low")

`ok (some x)` and `ok none` together cover `ok`, and `error e` covers the rest, so no `_` is needed. Leave one case out and it is
refused with `E-MATCH-INEXHAUSTIVE` --- better than a `_` that covers nothing. A `_` says nothing when a variant is added later.

#misconception[With `value_or` you can still tell when a value was absent][
  #demo("examples/ch11/valueor_blind.low")

  `value_or` *covers* absence with a stand-in. When the stand-in collides with a real value, the two cannot be told apart. Above,
  the real 0 in slot 0 and the absence in slot 7 both come out as 0. Use `value_or` only where "treat absent as 0" is truly
  fine; when presence matters, ask with `is_some` or `match` before covering it.
]

== This chapter's syntax at a glance

#dtable(
  columns: 3,
  id: "option-result-glance",
  caption: [Syntax of answer-carrying types --- shape · meaning · why it looks this way],
  [*Shape*], [*Meaning*], [*Why*],
  [`output option u8 .`], [a value, or none], [absence (null) shows in the type],
  [`some v` · `none`], [present · absent], ["present" is written too, so both branches are visible],
  [`output result u8 e .`], [a value or an error (a variant of `e`)], [failure is returned as a value --- there are no exceptions],
  [`ok v` · `error too_big`], [success · failure], [which branch is written in the source],
  [`errors too_big <condition> .`], [promise which error happens when], [written in the contract so the caller can prepare],
  [`is_some r` · `some_value r`], [ask whether present · take it out], [taking out is partial --- ask first],
  [`is_error r` · `ok_value r`], [ask whether failed · take out the success value], [same reason],
  [`value_or r 99`], [a stand-in when absent], [one line, but it covers absence],
  [`try <expr>`], [on failure, leave returning that error], [so checks are never forgotten --- like Rust's `?`],
  [`let n be u64 find xs k . else return 0 .`], [take the content, or leave through `else`], [no unchecked use],
  [`try <expr> else_none` · `else_error e`], [`result` → `option` · `option` → `result`], [changing channel shows what is lost],
  [`case ok (some x) .` · `case a or b .`], [nested pattern · several variants at once], [split in one go, still covering every case],
)

#recap[
  `option` is made with `some` and `none`, `result` with `ok` and `error`. The receiver asks and takes out (`is_some`, `some_value`,
  `is_error`, `ok_value`), gives a fallback with `value_or`, or splits with `match`. Taking out is partial, so taking from the missing side
  stops. An op returning a `result` promises its errors in an `errors` clause, `try` passes failures up, and the `else_none` and `else_error`
  tails change channel and type. Patterns can be or-ed (binding the same names) and nested, and a `match` on a constant folds at translation.
]
