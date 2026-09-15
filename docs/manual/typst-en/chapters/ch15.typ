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
  [`page_fault` · `blocking` · `cancel` · `detach`], [Accepted as vocabulary, but no primitive produces them yet],
)

`io`, `alloc`, `heap`, `state`, `panic`, `atomic`, `concurrent` and `unsafe` have primitives that actually produce them, and the compiler enforces
them. The last row is accepted as declarations but has nothing to enforce yet. A word not on the list is rejected.

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
  requires allocs.byte_allocator a .
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

#recap[
  Effects are a closed set of atoms, and words outside the list are rejected. Effects spread along calls; doing more than declared is rejected and
  declaring what is not done is warned about. The effects line is a set, so duplicates and mixing with `none` are rejected, and `concurrent` brings
  `wait`. `via` lets a type argument decide allocation-family effects. Purity allows optimisations such as reordering, memoisation and removal.
]
