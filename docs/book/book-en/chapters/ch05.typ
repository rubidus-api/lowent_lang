#import "../../book/lib.typ": *

= Ops --- `fn` and `proc`

#chapter-toc()

#prereq(
  ([#chref("surface"), The surface], [clause order in an op head]),
  ([#chref("numbers"), Numbers], [overflow stops and the treatment is chosen by name]),
)

#deepqa[
  In #chref("surface")'s clause-order table, what do `output` and `effects` each come after? Why there?
][
  `output` comes after the data inputs, because the output type may use an input's type parameter. `effects` comes after `output` and
  before the contracts, because it is where you write what is done with the capabilities received earlier. This chapter covers why the
  unit of execution that has such a head --- the op --- splits in two.
]

#why[
  The basic unit of a Lowent program is the op, and an op is always either a `fn` or a `proc`. That split is the floor on which the
  effect system of Part IV, the traits of Part VI and the parallelism of Part VII all stand, because a pure op may have its result
  remembered, be reordered, or be run in parallel. So the two kinds of op come right after numbers, before locals and flow.
]

#organizer[
  You will learn the difference between `fn` and `proc`, and what it means that purity is *observational* (mutation confined inside the
  op counts as pure). You will pick up how to receive parameters with `input` clauses and return with `output`, recursion, and the fact
  that `neg` is the only unary arithmetic op. You will also see what gets rejected when a pure op calls an impure one, and when a
  `mut` parameter writes back to the caller.
]

#chapter-questions()

== Two kinds

An *op* (operation) is a unit of execution with a name and a contract. On the surface it always opens with one of two words.

#idx("fn")
- `fn` --- a *pure* op. The same inputs give the same result, and it leaves no trace outside.
#idx("proc")
- `proc` --- an op that may perform effects. Which effects it performs is written in its `effects` clause.

There is no default. Whether it is a `fn` or a `proc` is always written before the name. Builtin operations such as `add` and `len` are
ops too --- ops the language made in advance, called *builtin ops*.

#demo("examples/ch05/kinds.low")

All three ops are `fn`s. `add3` adds three arguments, and `fact` is recursive, calling itself. `fact`'s `requires le n 20 .` is there
because 21! exceeds `u64`. Without that contract, `fact 21` would stop at a multiplication (#chref("numbers")). With it, execution stops
*on entry*, and where it stopped tells you it was the caller's fault.

== Purity is decided by observation

#idx("purity")
Look at `running_total` again. Its body has `var total` and changes it with `set`. Yet it is a `fn`. Lowent's purity is
*observational*: mutating a local variable confined inside the op cannot be seen from outside, so it counts as pure. Even if memory is
written at the machine level, it is not an effect if the caller cannot observe it.

On the other hand, a write the caller can see is an effect. A `fn` that writes through a `mut` parameter is rejected.

#demo("examples/ch05/mutparam.low")

As the diagnostic explains, a `fn` is an enforced purity contract. A caller may remember a `fn`'s result (memoise), reorder calls, or
drop a call whose result is unused. None of that is possible for an op that changes the caller's storage. So it must be a `proc`.

#demo("examples/ch05/mutproc.low")

`effects state .` declares that this op changes state outside itself. The VM's `arg0 (written) = [0,8,9]` is the trace --- the first
element of the slice the caller passed was changed.

#misconception[A `proc` is slower than a `fn`][
  The kind has nothing to do with speed. Both go down to C the same way. The difference is in *what the compiler and the caller may do*.
  Reordering, common-subexpression elimination and memoisation are allowed for a `fn` and not for a `proc`. Writing pure work as a
  `proc` throws those chances away.
]

== Don't write `effects` on a `fn`

A `fn`'s effects are already `none`. So adding `effects none .` is rejected.

#demo("examples/ch05/redundant.low")

Not because it is wrong, but because it would be *two spellings of one meaning*. On a `proc` things are different. A `proc`'s `effects`
clause *narrows*. A `proc` without the clause reads as not narrowed --- it is assumed able to do input/output, allocation and state. So a
`proc` should carry the clause; doing more than it lists is rejected, and effects it lists but never performs are reported.

#qa[
  What happens if a `proc` actually performs no effect?
][
  Writing `effects none .` on a `proc` is allowed. Older code sometimes keeps an op as a `proc` to emphasise local mutable state or
  loops. But if no effect is observable, writing it as a `fn` is the shape this language recommends. Purity has to show in the head for
  callers and the compiler to use it.
]

== Effects spread to the caller

Calling an op adds its effects to the caller's. When a pure `fn` calls a `proc` that performs `io`, that `fn` is no longer pure.

#demo("examples/ch05/calls.low")

`helper` does not print by itself; `say` does. It is still rejected. Thanks to this rule, the head of an op tells you the effects of every
op it calls. There is no way to pass an effect along hidden. Which effect pairs with which capability is covered in
#chrefs("effects", "capabilities").

== Parameters and return

A parameter is one `input <name> <type> .` clause. For several, repeat the clause. The caller supplies arguments in clause order. The
returned value is a single `output <type> .`, and the body returns it with `return <expr> .`.

```lowent
fn compare input a i32 . input b i32 . output i32 .
do
  return sub a b .
end .
```

Words in front of the type say ownership, mutability and presence. The ones you meet often are these.

#dtable(
  columns: 2,
  id: "ops-params",
  caption: [Words attached to parameter types],
  [*Written*], [*Meaning*],
  [`input xs slice u8 .`], [Borrows someone's bytes for reading],
  [`input xs mut slice u8 .`], [Borrows someone's bytes for writing (must be a `proc`)],
  [`input p ref point .` · `mut_ref point`], [Borrows one value for reading · writing (#chref("references"))],
  [`input o option u64 .`], [A value that may or may not be there (#chref("option-result"))],
  [`input out cap io .`], [A capability (#chref("capabilities"))],
  [`input comptime t type .`], [A type fixed at translation time (#chref("generics"))],
)

There is one return value. To return several, group them in a `struct` (#chref("structs-enums")) or write into a `mut` slice the caller
passed.

== Only `neg` is unary

The arithmetic ops `add`, `sub`, `mul`, `div` and `mod` take two arguments. The only arithmetic op that takes one is `neg`, which flips
the sign.

#demo("examples/ch05/neg.low")

`neg` is for signed types. On an unsigned value it overflows. `abs` is the absolute value, and it stops when given a value that cannot be
represented as a positive, like the smallest `i64`. There are no unary operators inside the `expr` island either; to flip a sign there,
write `expr 0 - a` or call `(neg a)` in parentheses.

== Modifiers

Modifiers can go in front of an op head.

- `export` --- exports the op so other modules can call it. When emitting native code it becomes a symbol callable from C. An op
  without it is used only inside its module, because exporting should be a choice, not a side effect.
- `extern` --- the implementation is outside (in C). Instead of a body, a `link` clause names it (#chref("ffi")).
- `unsafe` --- does work the language cannot check. It pairs with `effects unsafe`.

```lowent
unsafe extern proc c_area input k cap c . input w i64 . input h i64 . output i64 .
  effects unsafe . link lw_c_area .
end .
```

#recap[
  An op is a `fn` or a `proc`, and the kind is always written. Purity is observational: mutation confined in the op counts as pure, but a
  write visible to the caller through a `mut` parameter needs a `proc`. A `fn` carries no `effects` clause, and a `proc`'s `effects` is a
  narrowing clause. Effects spread to the caller. One clause per parameter, one return value, and the only unary arithmetic op is `neg`.
]
