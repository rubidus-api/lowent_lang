#import "../../typst-ko/lib.typ": *

= Effects --- the marks an op leaves on the world

#chapter-toc()

#prereq(
  ([#chref("ops"), Ops], [a `fn` is pure and a `proc`'s `effects` is a narrowing clause]),
  ([#chref("contracts"), Contracts], [an unenforced promise is not used as a fact]),
)

#deepqa[
  In #chref("ops"), the pure `helper` did not print by itself and only called `say`, yet it was rejected. Why?
][
  Because effects spread to the caller. `say` performs `io`, so `helper`, which calls it, performs `io` too, and a pure `fn` cannot perform
  effects. This chapter covers what effects are made of, and what happens when the declaration and reality diverge in either direction.
]

#why[
  If a contract is a promise about values (#chref("contracts")), an effect is a promise about *behaviour*. Does it read files, obtain memory,
  possibly stop, wait for another flow? Without these answers in the head, a caller must follow the body to the end. Lowent has effects written as a
  set of closed words and the compiler checks both directions. Capabilities (#chref("capabilities")) are the partner that writes *who allowed* the
  effect, so effects come first.
]

#organizer[
  You will learn that effects are a closed set of atoms and what each atom means. You will pick up that effects spread along calls, that doing more
  than declared is rejected and declaring what is never done is warned about. You will see the rules that follow from the effects line being a set,
  the closure by which `concurrent` brings `wait` with it, and `via`, which lets a type argument decide the effects. Finally you will understand what
  purity allows the compiler to do.
]

#chapter-questions()

== Effects are closed words

#idx("effect")
An *effect* is an influence an op has on the outside. Authors cannot invent new effects; the list is fixed by the language.

#dtable(
  columns: 2,
  id: "effects-atoms",
  caption: [Effect atoms],
  [*Atom*], [*Meaning*],
  [`none`], [No effect at all. The bottom],
  [`io`], [Exchanges data with the outside (files, connections, standard I/O)],
  [`alloc`], [Obtains or returns memory from a *fixed window*],
  [`heap`], [Obtains memory from a *growing root*. Exists only on machines with an operating system],
  [`state`], [Changes state held by a module or actor, or the caller's storage],
  [`panic`], [May stop the program],
  [`atomic`], [Performs indivisible reads and writes],
  [`concurrent`], [Completion depends on the progress of another flow],
  [`wait`], [Waits; wakes up eventually without anyone's help],
  [`lock`], [Takes a lock],
  [`device`], [Touches a device directly],
  [`unsafe`], [Does work the language cannot check],
  [`page_fault` · `blocking` · `cancel` · `detach`], [a flow stopping or being cut off --- no primitive produces them yet],
)

The nine `io`, `alloc`, `heap`, `state`, `panic`, `atomic`, `concurrent`, `wait` and `unsafe` have primitives that actually produce them, and the
compiler enforces them --- a body that does it must say so, and saying it without doing it is reported. The other six (`lock`, `device`, `page_fault`,
`blocking`, `cancel`, `detach`) have no primitive yet. They are accepted as declarations and *spread* to callers, but whether the body really does
it cannot be asked. A word not on the list is rejected.

#demo("examples/ch15/undef.low")

Quietly accepting a typo would make that op read as having no effect --- the case the diagnostic describes as "a typo here silently declares the op
PURE".

== Effects spread to the caller

Calling an op adds its effects to the caller's. Performing more effects than declared is rejected.

#demo("examples/ch15/spread.low")

`compute` declares only `effects panic .`, but calls `log_line` and so also performs `io`. The diagnostic says "add it to `effects`, or stop calling
what needs it". Conversely, the layers effects do not spread into are clear too.

#demo("examples/ch15/layered.low")

`celsius_to_f` is a pure `fn` and can be called anywhere. `print_digit` performs `io`, so it can be called only from `main`, which declares `io`. The
computing layer and the layer that touches the outside separate in the head. Good design keeps the pure layer wide and the effectful layer thin.

#qa[
  To leave one log line, must a whole pure op become a `proc`?
][
  Yes. An op that logs leaves a trace outside, so it is not pure, and the head must show it. There is a common way out, though. Keep the computation
  pure and *return the result*, and log from the outer layer that receives it. This shape, pushing effects outward, is also easy to test --- the pure
  layer can be checked right away with `--run` and `test` without capabilities.
]

== Declared but not done is reported

Declaring an effect that is never performed is also a problem, because a declared effect is a cost the caller must bear.

#demo("examples/ch15/over.low")

`double` says it may stop, but it never stops. Then every op calling it must declare `panic`, and pure `fn`s cannot call it --- it exaggerated what it
does. The diagnostic says to remove it, or to say so if a future version will actually perform it.

#misconception[Declaring effects generously saves work later][
  Generous effects spread to every caller. Declaring one `panic` in advance makes every calling op declare `panic`, and computations that should be
  pure lose their purity. An op that loses purity also loses optimisations such as reordering and memoisation. Declare effects only as far as they are
  actually performed.
]

== The effects line is a set

The effects line is a *set*, not a list. Writing the same word twice is rejected.

#demo("examples/ch15/dup.low")

The second `panic` says nothing the first did not. `none` cannot appear with any other atom.

#demo("examples/ch15/nonemix.low")

An op cannot both have effects and have none; write the effects or write that there are none. And a `fn` does not even carry `effects none`
(#chref("ops")).

Some effects bring others with them. Today there is one such rule --- writing `concurrent` counts as writing `wait` too, since waiting for another flow's
progress may mean being suspended. Two sets are therefore compared after applying this closure. Writing `concurrent` means `wait` need not be written
separately, but writing only `wait` and doing something `concurrent` is rejected.

== `via` --- a type argument decides the effects

Generic containers are in an awkward spot, because their effects depend on which allocator they receive. Opened on a bump allocator over borrowed
bytes, the effect is just `state`; opened on an allocator standing on the growing heap, `heap` appears. Always declaring the largest effect any allocator
could produce is a lie, and declaring only the smallest is a *hidden allocation*.

```lowent
export proc append
  input comptime t type .
  input comptime a type .
  input g mut vec t a .
  input x t .
  output bool .
  effects state via a .
  requires allocs.byte_allocator a. . .
```

This is the head of `append` in the standard library's `vecgen`. `effects state via a .` means "the allocation-family effects (`alloc`, `heap`,
`atomic`) declared by the ops of type `a` are also this op's declaration". Each monomorphised instance has its effects decided by that type. Generic
containers return in #chref("lib-containers").

== What purity allows

A `fn` --- an op whose effects are `none` --- satisfies three properties.

- The same inputs always give the same result.
- Changing the order of calls does not change the program's result.
- If its result is unused, it need not be called.

These three allow optimisations. Two pure calls with the same arguments can be reduced to one (common-subexpression elimination), results can be
remembered (memoisation), unused calls can be removed, and calls that do not depend on each other can be reordered or split up. That these optimisations
do not change a program's meaning is proven in Coq (#chref("proofs-effects")). Call boundaries and the timing of traps are outside that
proof.

#realcase[Where effect declarations become premises of a proof][
  The soundness theorem of the effect system says "the declared effects cover the effects that actually happen". That theorem is what the theorems on
  effect-based optimisation stand on. So the compiler rejects code that does *more* than declared (the theorem's premise) and warns about code that does
  *less* (harmless to the theorem, but pushing false costs onto callers). That is why the two directions carry different weight.
]

== Common mistakes

#antipattern[Using `panic` in a pure `fn`][
  #demo("examples/ch15/mistake_fnpanic.low")

  Stopping the program leaves a mark on the outside too. If a caller memoises this op or reorders it, the moment of the stop
  changes, so a `fn` cannot use `panic` and is rejected with `E-EFFECT-CALC`. Attaching `effects panic .` to a `fn` is rejected with
  the same code --- the words `fn` and `proc` already say whether an op is pure. There are two fixes: make it a `proc` that declares
  stopping as an effect, or move the condition into a contract, which makes it the caller's responsibility.

  #demo("examples/ch15/fnpanic_fixed.low")

  The second fix is usually better. `nonzero_strict` stays pure, and the diagnostic says the fault of passing 0 lies with the caller.
]

#antipattern[A `fn` writing into a slice received as `mut`][
  #demo("examples/ch15/mistake_fnmutwrite.low")

  The zeros written by `clear` stay in the caller's slice. To the caller the outside has changed, and such an op cannot be memoised,
  removed or reordered. Hence `E-EFFECT-PURITY`. Change the head to `proc clear … effects state .`. As the diagnostic adds, changing
  locals inside an op does not break purity (see the misconception below).
]

#antipattern[Putting commas between effects][
  #demo("examples/ch15/mistake_effcomma.low")

  The habit of separating a list with commas becomes `E-VOCAB-REMOVED` here. The comma was once in the grammar but was only ever
  used to continue a line; newlines no longer close a form, so it was removed. Separate the words with spaces, as in
  `effects io panic .`. The form ends at the stop.
]

#misconception[Using `var` and `set` makes an op impure][
  #demo("examples/ch15/pure_local.low")

  `sum_to` keeps changing two locals, yet it is a `fn`. The storage that changes lives only inside this op; the caller sees only the
  result 55. It always gives the same answer for the same input, and not calling it leaves nothing behind. Purity does not mean
  "changes nothing inside"; it means "leaves no mark visible from outside".
]

#misconception[A `fn` never stops][
  #demo("examples/ch15/fn_can_stop.low")

  What a `fn` cannot use is the `panic` *effect*. A stop raised by the processor because a contract or a bound broke --- such as
  reading the first slot of an empty slice --- is treated as the result of a wrong call, not as something the op did (canon 6.5.9).
  So the pure `first` also stops when given `[]`. To avoid the stop, expose the responsibility to the caller with
  `requires ge len xs. . 1 . .`, or return an `option`.
]

#antipattern[Calling an op with a rare effect from a pure `fn`][
  #demo("examples/ch15/mistake_blocking.low")

  `blocking` means "may hold the flow of execution", so a `fn` calling such an op is not pure, and it is refused with `E-EFFECT-CALC`.
  Until 2026-09-16 six atoms --- `lock`, `device`, `page_fault`, `blocking`, `cancel` and `detach` --- did *not* spread to the caller, so this
  passed; `device`, which touches hardware directly, could hide behind a pure function. The same shape with `wait` was refused even then.

  #demo("examples/ch15/blocking_wait.low")

  These six have no primitive that performs them yet, so they are not asked about by "declared but never performed" (`W-EFFECT-OVER`)
  --- there is no body to ask. They only propagate.
]

== This chapter's syntax at a glance

#dtable(
  columns: 3,
  id: "effects-glance",
  caption: [Effect syntax --- shape · meaning · why it looks this way],
  [*Shape*], [*Meaning*], [*Why*],
  [`fn …`], [no effects --- write no `effects` clause], [purity is visible in one word],
  [`proc … effects io panic .`], [the set of effects this op may perform], [reading the head tells you what it does],
  [`proc …` (no clause)], [rejected (`E-EFFECT-MISSING`)], [a `proc` always carries the clause --- `effects none .` when it performs none],
  [atoms `io`·`alloc`·`heap`·`state`·`panic`·…], [a closed list fixed by the language], [so a typo never silently means "pure"],
  [`effects panic panic .` · `none panic`], [rejected (`E-EFFECT-DUP` · `E-EFFECT-NONE-MIX`)], [a set has no repeats or contradictions],
  [doing more than declared · declaring and not doing], [error (`E-EFFECT`) · warning (`W-EFFECT-OVER`)], [premise of the proofs · a false cost for callers],
  [`concurrent`], [brings `wait` along], [waiting on another flow can block],
  [`effects state via a .`], [inherit the allocation effects of type argument `a`], [each allocator gets exact effects],
)

#recap[
  Effects are a closed set of atoms, and words outside the list are rejected. Effects spread along calls; doing more than declared is rejected and
  declaring what is not done is warned about. The effects line is a set, so duplicates and mixing with `none` are rejected, and `concurrent` brings
  `wait`. `via` lets a type argument decide allocation-family effects. Purity allows optimisations such as reordering, memoisation and removal.
]
