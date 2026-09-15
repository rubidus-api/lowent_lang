#import "../../book/lib.typ": *

= Generics --- parameters fixed at translation time

#chapter-toc()

#prereq(
  ([#chref("expr"), Expressions], [`size_of` and `comptime` are computed at translation time]),
  ([#chref("fixed-memory"), Allocators and fixed memory], [allocators are taken as `input comptime a type .` and swapped]),
)

#deepqa[
  #chref("fixed-memory")'s `two_from` accepted both `bump_bytes` and `bump_aligned`. Why was swapping allocators said to cost nothing?
][
  Because the allocator's *type* is fixed to a concrete type at translation, and code specific to that type is made for each call. There is no virtual
  function table and no indirect call. This chapter covers "parameters fixed at translation time" --- `comptime` --- in general.
]

#why[
  Copying the same algorithm for every type multiplies the places to fix by the number of types. Passing comparison as a function pointer looks free at the
  call site but hides the cost of an indirect call. Lowent takes types and constants as *translation-time parameters* and makes a real instance for each
  combination used, and what gets made is visible at the call site. Traits (#chref("traits")) and `pipe` (#chref("pipe")) both stand on this, so it is set up
  first as the tool of abstraction.
]

#organizer[
  You will learn to take values at translation time, as in `input comptime n u8 .`, and types, as in `input comptime t type .`. You will see that giving a value
  unknown at translation time is rejected, that a real instance is made for each combination used (monomorphisation), and that their number is the amount of
  code. You will also see how to put a condition on a type parameter with `requires <trait> t .`, and what happens when a type that does not satisfy it is
  given.
]

#chapter-questions()

== Taking values and types at translation time

#idx("comptime")
A parameter marked `comptime` must have its value fixed at translation time. Types can be taken this way too.

#demo("examples/ch22/sizes.low")

- `bytes_for`'s `input comptime t type .` takes a type. `size_of t` in the body gives the size of that type at translation time. `bytes_for u8 100` is 100 and
  `bytes_for u64 100` is 800.
- `add_const`'s `input comptime n u8 .` takes a value. `add_const 7 10` is 17.
- Call sites write the type or constant *in front, like an ordinary argument*. There are no angle brackets (`<T>`) and no inference.

comptime parameters come at the very front of the head (even before capability inputs) (#chref("surface")), because the following input and output types use
their names.

Giving a value unknown at translation time is rejected.

#demo("examples/ch22/rt_arg.low")

`k` is only known at run time. Once the position says `comptime`, receiving a run-time value would make that word a lie. What may stand in a comptime position
is an integer literal or a module-level `let` constant.

#qa[
  Why not infer type arguments? Couldn't `max_of a b` be worked out from the type of `a`?
][
  It could. But with inference, *what gets made* is not visible at the call site. The same line would call different instances depending on argument types,
  and you would have to follow the source to count them. Lowent chose to leave countable at the call site the cost that the number of combinations used is the
  amount of code made.
]

== An instance per combination

#idx("monomorphisation")
The processor makes an op specific to each combination used. This is called *monomorphisation*. `bytes_for u8` and `bytes_for u64` are two different
functions in native code. Because they are made after the type is fixed, sizes and operations are baked in as constants, and calls are direct.

The price is the amount of code. Calling with ten types makes ten copies. That cost does not hide; the types are written at the call sites, so the number of
copies can be counted from the source.

== Putting conditions on types

An op that takes a type usually needs that type to know how to do something. To pick a maximum, it must know how to compare. That condition is put as
`requires <trait> <type> .`.

#demo("examples/ch22/bound.low")

- `trait ordered` is the promise "has an op called `less`". `score` declares it will satisfy that promise with `satisfies ordered .` and actually does so with
  `score.less` (#chref("traits")).
- `max_of`'s `requires ordered t .` is the type condition. The body trusts it and calls `method a less b`.
- `bigger` calls `max_of score …`. The processor makes a `score`-specific instance of `max_of`; in the emitted C you can see `max_of_score` in that function's
  name.

Calling with a type that does not satisfy the condition is rejected.

#demo("examples/ch22/unsat.low")

A type condition is a promise, and letting a caller make an instance past it would make the callee's contract a lie. The diagnostic says to add `satisfies` and
the required op to that type.

#misconception[Generic code is slow][
  It depends on the language. Erasing types, boxing every value and dispatching through indirect calls is slow. Monomorphising becomes the same machine code
  as copying the code by hand for each type. Lowent monomorphises. The price is not speed but the amount of code, and that amount can be counted at the call
  sites.
]

== The type carries it, not a value

C's `qsort` takes the comparison function *as a value*. Lowent has no first-class functions, and even if it did, the indirect call through a function pointer
would hide. Instead *the type carries the comparison*. The head of the standard library's `sortgen` has that shape.

```lowent
export trait ordered do
  less input a self . input b self . output bool . effects none .
end .

export proc sort_by input comptime t type . input s mut slice t .
  output void .
  effects none .
  requires ordered t .
```

To change the ordering, use a type with a different `less`. Wrapping in a one-field struct keeps the layout unchanged, so it costs nothing. Descending order or
multiple keys are also a matter of writing `less` that way. Instead of mode arguments, the type carries the meaning (#chref("lib-containers")).

#realcase[The type decides even the allocation effects][
  #chref("effects", cap: true)'s `via` is where generics reach effects. `vecgen.append` is written `effects state via a .`, so monomorphised on a bump over
  borrowed bytes only `state` appears in that instance's signature, and monomorphised on `heap_bytes`, which carves from the heap, `heap state` appears. The type
  argument decides not only the algorithm but the *effects*. That is how one source serves both machines without an operating system and servers.
]

#recap[
  `comptime` parameters have their values fixed at translation time, and types are taken as `input comptime t type .`. Call sites write types and constants as
  leading arguments, with no inference. A run-time value in a comptime position is rejected. An instance is made for each combination used, calls are direct,
  and their number is the amount of code. `requires <trait> t .` is a type condition, and types that do not satisfy it are rejected. Behaviour such as
  comparison is carried by the type, not by a value.
]
