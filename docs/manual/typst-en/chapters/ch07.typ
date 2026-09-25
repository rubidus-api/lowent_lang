#import "../../typst-ko/lib.typ": *

= Flow --- branches, loops and leaving early

#chapter-toc()

#prereq(
  ([#chref("numbers"), Numbers], [booleans are not numbers]),
  ([#chref("locals"), Locals], [`if` is a statement that yields no value]),
)

#deepqa[
  In #chref("locals"), why was `let x u64 be if gt a 1 . 5 else 6 .` rejected, and how do you set a value per branch?
][
  `if` is a statement that yields no value, so it cannot stand in an expression position (`E-IF-VALUE`). To set a value per branch, make
  a `var` with a default and `set` it in the branches, or `return` from each branch. This chapter covers the branches themselves, loops,
  and the rules for leaving early.
]

#why[
  Flow statements exist in every language, so there seems to be nothing new to learn. But Lowent's flow statements come with *checks*.
  Conditions must be booleans, the `else` of a `guard` must leave, an op that returns a value must return on every path, and a `match`
  must cover every case. These checks remove "on this path there is no value" defects at translation time. The promises of flow are set
  up before data (Part III).
]

#organizer[
  You will learn to use `if … else`, `while`, `for`, `break` and `continue`. You will see how `guard` turns a condition into *a fact about
  the code below it*, and why an `else` that does not leave is rejected. You will also meet the rule that every path must return a value,
  `match` over numbers and ranges with its exhaustiveness check, and the fact that `panic` is an effect.
]

#chapter-questions()

== Conditions and loops

`if` runs a block when its condition is true, and `else` gives the block for false. `while` repeats while its condition is true, and
`for` walks the elements of a slice in order. Every condition must be a `bool`.

#demo("examples/ch07/loops.low")

- `count_big` counts from 0 up to `n` and counts only numbers greater than 5. Look at the shape `while lt i n . do … end` --- the
  condition is a form too, so it is closed with a full stop, and `do` opens the body after it.
- `first_zero` leaves the loop with `break` when it meets a 0, and returns the length if there is none.
- `odd_sum` walks the elements with `for x xs do … end` and skips even ones with `continue`. The `for` name `x` has the slice's element
  type (`u8`) and lives only inside the block.

Writing `for x in xs` is rejected with `E-VOCAB-REMOVED`. The thing to walk comes right after the name.

#qa[
  How do you write a loop counting from 0 to `n` with `for`?
][
  `for` is a tool for walking slices and does not take a numeric range directly. Counting loops are written with `while` and `var`.
  Having one shape for counting loops makes the range of the counter easy for the compiler to see and keeps the analysis that removes
  bounds checks simple. Filtering, counting and collecting over a slice are `pipe`'s job (#chref("pipe")).
]

== `guard` --- turning a condition into a fact

#idx("guard")
`guard <condition> . else <leave> .` *leaves on the spot* if the condition is not true. The leaving statements are `return`, `break`,
`continue` and `panic`.

#demo("examples/ch07/guards.low")

The code below the `guard` in `head_or_zero` lives only in a world where the slice is not empty. That is why `index data 0` is safe.
`grade` filters out out-of-range scores first with `guard`, and splits the rest with `if … end else do … end` below it.

`guard` is not another name for `if not`, because its `else` must leave. If it does not, the code is rejected.

#demo("examples/ch07/fallthrough.low")

If the `else` flows on instead of leaving, the code below the `guard` believes `n` is at most 5 while it is not. Rather than letting code
believe an unreliable fact, translation is refused. If you do not mean to leave, use `if`.

#misconception[`guard` is syntactic sugar for shorter code][
  Being shorter is a side effect. The real point of `guard` is *handing a fact to the compiler and the reader*. After a `guard` the
  compiler knows the condition is true and uses it to remove bounds checks. You can write the same meaning with `if … do return … end`
  and get the same behaviour, but leaving is not guaranteed by the grammar.
]

== Every path returns a value

An op that returns a value must return one on *every path*.

#demo("examples/ch07/partial.low")

On the path where `a` is 0 or less, there is no `return`. The old tool quietly returned 0 on that path --- a value that appears nowhere in
the source. Now it is rejected. The statements recognised as returning are `return`, an `if … else` where both branches return, and a
`match` where every arm returns. `while`, `for` and `guard` have ways out, so they do not count as returning by themselves. That is why a
loop is always followed by a `return`.

An op that returns nothing (`output void`) may run to the end of its body without `return`. Where it ends is where it returns.

== `match` --- every case, none missing

#idx("match")
`match` splits a value into cases. Unlike an `if` chain, it *must handle every case*.

#demo("examples/ch07/bands.low")

- `band` splits by `lo to hi` ranges (inclusive at both ends). Integers have many cases, so a `case _ .` covering the rest is required.
- `half` needs no `_` because its two ranges cover `u8`'s 0 … 255 with no gap.
- `case y when gt y 100 .` in `big` binds the whole value to `y` and narrows it further with a guard. An arm with a guard might be false,
  so it does not count towards exhaustiveness; `_` is still needed.

A gap is rejected.

#demo("examples/ch07/inexhaustive.low")

10 falls in no arm. Writing the same case twice, or an arm after `_`, is rejected with `E-MATCH-REDUNDANT`. It is an error, not a warning
--- an arm that can never run usually hides a defect.

#qa[
  Besides exhaustiveness, what makes `match` better than an `if` chain?
][
  It shows when cases grow. Add a variant to an enum and every `match` over that enum stops at translation and tells you where to fix it
  (#chref("structs-enums")). An `if` chain lets the new case slide silently into its last `else`. Also, `--ir` has the tool report the
  dispatch cost, such as the number of arms and the worst-case number of comparisons.
]

== `panic` is an effect

#idx("panic")
`panic` stops the program immediately. It is an irreversible stop, so it is an effect, and an op that uses it must be a `proc` declaring
`effects panic`.

#demo("examples/ch07/panics.low")

The VM reports `E-VM-PANIC` and makes clear that *this is not a contract violation* --- the code chose to stop; it did not break a promise.
A pure `fn` cannot `panic`. But stops the processor raises for overflow or contract violations still happen inside a `fn`. Those are not
something the op did but the processor making promises hold.

Use `panic` only for situations that cannot be recovered from. Failures a caller can handle are returned as values
(#chref("option-result")). How to tell the two apart is covered in #chref("errors-design").

== Common mistakes

#antipattern[Writing `for x in xs`][
  #demo("examples/ch07/mistake_forin.low")

  `for` is `for <name> <slice> do`. The thing to walk comes right after the name, and `do` already marks where the body starts, so `in`
  would carry nothing. It was removed so that one meaning has one spelling. The long diagnostic suggests the glued dot (`a.b`) as an
  alternative, but that spelling is also rejected nowadays --- the message is out of date in this edition. The fix: `for x xs do`.
]

#antipattern[Chaining branches with `elif`][
  #demo("examples/ch07/mistake_elif.low")

  `elif`, `elsif` and `else if:` vary between languages. Lowent joins words it already has: close the previous block with `end` and add
  `else if`. `elif` is not a word, so `elif lt n 80 .` is read as a statement of its own, and the `do … end` after it
  becomes a block with no head to open it (`E-BLOCK-NOHEAD`) --- a `do … end` is always opened by a head such as `if`, `while` or `fn`.

  #demo("examples/ch07/elseif.low")

  The last branch is `end else do … end`. When there are three or more branches that all split one value, `match` is a better fit.
]

#antipattern[Putting `else` inside the block, C style][
  #demo("examples/ch07/mistake_innerelse.low")

  In C and several other languages `else` follows the previous block, as in `if … { … } else { … }`. In Lowent `else` comes *after* the
  previous block is closed with `end` (`end else do`). With `else` on its own line inside the block the program is rejected with
  `E-STMT-ELSE`; it used to translate and then stop at run time with `E-VM-UNSUP`, while the native build left the op out entirely.
]

#antipattern[Using a symbol such as `<` in a condition][
  #demo("examples/ch07/mistake_less.low")

  Comparisons are words --- `lt` (less than), `le` (less or equal), `gt`, `ge`, `eq`, `ne`. `<` is a character the language does not
  know, hence `E-CHAR`. The choice spares you from memorising symbol precedence, and long arithmetic has the `expr` island
  (#chref("expr")). The fix: `while lt i n . do`.
]

#antipattern[`continue` skipping the increment][
  If you count with `while` and `continue` in the middle of the body, you also skip the `set i (add i 1) .` placed below it.

  ```lowent
  fn odd_count input xs slice u8 . output u64 .
  do
    var n u64 be 0 .
    var i u64 be 0 .
    while lt i (len xs) . do
      if eq (mod (index xs i) 2) 0 . do
        continue .
      end
      set n (add n 1) .
      set i (add i 1) .
    end
    return n .
  end
  ```

  At the first even number, `i` stops increasing and the loop never ends. Neither compilation nor run-time checks catch this --- running
  forever is not an overflow. Move the increment to the *top* of the body (keeping the pre-increment value for indexing), or, if you are
  walking elements, use `for x xs do` in the first place. With `for`, moving to the next element is the language's job, so this bug has
  nowhere to live.
]

There are two ways to write the slot that takes the rest. `case _ .` and a final `else` do the same thing.

#demo("examples/ch07/matchelse.low")

Nothing may follow an `else` --- it has already taken everything, so a later arm never runs and is refused with `E-MATCH-REDUNDANT`.

#misconception[`match` arms fall through like C's `switch`][
  #demo("examples/ch07/nofall.low")

  Only *the one* matching arm runs, and it never falls into the next. There is no `break` to remember and no bug from forgetting it. To
  do the same thing for two cases, join the arms with `or` (#chref("option-result")).
]

== This chapter's syntax at a glance

#dtable(
  columns: 3,
  id: "control-glance",
  caption: [Control-flow syntax --- shape · meaning · why it looks this way],
  [*Shape*], [*Meaning*], [*Why*],
  [`if c . do … end`], [run the block when the condition is true], [the condition is a form closed by a stop; the body opens with `do`],
  [`if c . do … end else do … end`], [one of two], [openers and closers always pair up],
  [`… end else if c2 . do … end`], [chaining branches], [joins existing words instead of adding `elif`],
  [`while c . do … end`], [repeat while the condition is true], [the one shape for counting loops],
  [`for x xs do … end`], [each element of a slice in turn], [moving to the next element is the language's job],
  [`break .` · `continue .`], [leave the loop · go to the next round], [statements that change the flow],
  [`guard c . else return … .`], [leave unless the condition holds --- afterwards it is a fact], [`else` must always leave],
  [`return e .`], [return a value and finish], [on every path of an op that produces a value],
  [`match v do case … . statement … end`], [split by cases], [complete and non-overlapping --- no fall-through],
  [`case 1 to 9 .` · `case _ .` · `case y when c .`], [a range · everything else · a guarded arm], [integers have many cases, so `_` is often needed],
  [`panic "…" .`], [an irreversible stop (an effect)], [only in a `proc` that declares `effects panic`],
)

#recap[
  Conditions must be `bool`s. `while` repeats on a condition and `for` over a slice, and `break` and `continue` change the flow. The `else`
  of a `guard` must leave, and after the `guard` the condition is a fact. An op that returns a value must return on every path. `match`
  must cover cases with no gaps and no overlaps. `panic` is an effect.
]
