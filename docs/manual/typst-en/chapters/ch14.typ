#import "../../typst-ko/lib.typ": *

= Contracts --- write them, have them checked, lose the checks

#chapter-toc()

#prereq(
  ([#chref("first-program"), A first program], [a contract stops on entry]),
  ([#chref("slices"), Sequences], [writing the length condition as a contract removes bounds checks]),
  ([#chref("option-result"), Types that hold answers], [an `errors` clause is a contract on the way out]),
)

#deepqa[
  What did the single line `requires le n (len a) .` do in #chref("slices")'s `sum_first`? And why must an index itself not use `le`?
][
  It gathered the check into one check on entry and let the bounds check on `index a i` inside the loop be removed. With `le` on an index,
  `i = len a` would be allowed, and that is one past the end, so indices use `lt`. This chapter covers all of it: what that contract is, who
  keeps it, and when it is checked and when it disappears.
]

#why[
  Part IV is where this language earns its name. The goal of knowing what an op does from its head alone (#chref("intro")) is carried by three
  things: contracts, effects and capabilities. The first of them, contracts, has already appeared in pieces --- stopping on entry, removing
  bounds checks, promising errors. This chapter gathers those pieces into one system. If effects (#chref("effects")) and capabilities
  (#chref("capabilities")) are about "what it does", contracts are about "what it takes and what it gives back".
]

#organizer[
  You will learn whose responsibility `requires`, `ensures` and `errors` each are, when they are checked, and how diagnostics assign blame when
  they break. You will pick up the principle by which contracts remove checks, conditions over every element of a slice (`elem_le` and so on),
  and naming contracts with `contract` and `satisfies`. You will also see violations decided at translation time, declarations of errors that
  can never happen, contract grades (`static`, `debug`, `assume`) and how build modes treat the checks that remain.
]

#chapter-questions()

== A contract is a checked promise

#idx("contract")
A *contract* is the promise an op makes about its own inputs and outputs. Unlike a comment, it is checked.

#demo("examples/ch14/pair.low")

#idx("ensures")
Read aloud, the head says: "this op must be given a byte sequence of length at least 2 (`requires`), promises that the value it returns is at
most 65535 (`ensures`), and is pure (`fn`)." You know that much without opening the body. In `ensures`, `ret` stands for the returned value.

A contract is used in three places. The processor uses it *as a fact* to remove checks. A contract the processor could not prove is checked *at
run time* and stops when broken. And it tells the reader what must be kept to call this op.

== Whose fault is it?

There are two places a contract breaks, and the diagnostic assigns blame.

#idx("requires")
#dtable(
  columns: 3,
  id: "contracts-blame",
  caption: [When a contract breaks],
  [*Clause*], [*When checked*], [*Whose fault when broken*],
  [`requires`], [on entry], [*the caller* --- did not keep the condition],
  [`ensures`], [on exit], [*this op* --- broke its own promise],
  [`errors … <condition>`], [on exit], [*this op* --- the condition held but it did not produce the error],
)

#demo("examples/ch14/blame.low")

`percent_of 250 200` has a part larger than the whole, so it is the caller's fault, and the VM says "the caller broke the contract".
`clamp_to_100 120` is the fault of an op that forgot to clamp values from 101 to 150, and the VM says "THIS op broke its own promise". When a
program stops, the diagnostic answers at once "did I call it wrong, or is that op built wrong?".

#qa[
  `ensures` is the op checking itself --- how does that differ from a test?
][
  A test checks that the answer is right for a few inputs. `ensures` checks that the promise holds *on every call*. Even if the promise breaks
  outside the inputs a test chose, `ensures` stops on that call. And callers may use `ensures` as a fact: a caller of `clamp_to_100` may trust
  that the result is at most 100 and have the check in `narrow u8` removed. It is also why the development repository's tooling can test ops
  from their contracts alone, without expected outputs (#chref("build-test")).
]

== Contracts remove checks

The processor uses contracts as facts to narrow the ranges values can take. When those ranges show safety, it removes overflow, division by
zero, narrowing and slice bounds checks. So in this language *writing honestly makes code faster*.

```lowent
fn bare input a u8 . output u8 .
do
  return add a 1 .          rem the overflow check remains
end .

fn proven input a u8 . output u8 .
  requires le a 200 .
do
  return add a 1 .          rem no check --- it cannot exceed 201
end .
```

The two ops have the same body. The only difference is one line of contract, and that line removes a run-time check. The check did not vanish
but was *moved* to one check on entry. If the caller calls with a constant, or proves the range with its own contract, even the entry check
disappears.

There is one important rule. *A contract that is not enforced is not used as a fact.* Trusting without checking and removing checks is not being
faster but being wrong. This rule returns below with grades and build modes.

== Conditions over every element

A contract's condition is a pure expression. Loops are statements, so "every element is at most 9" cannot be written as a loop. There are words
that say it instead.

#demo("examples/ch14/elems.low")

`elem_le ds 9` means every element is at most 9. There are also `elem_lt`, `elem_gt` and `elem_ge`. The condition is checked once on entry, and
the arithmetic in the body uses the fact that the elements are at most 9.

== Naming a contract

When several ops require the same condition, give the condition a name.

#demo("examples/ch14/named.low")

`contract positive do … end` is a named contract, and an op adopts it with `satisfies positive .` at the very front of its head. Repeating the
same condition by hand in several places means fixing one and forgetting another; a name leaves one place to fix. `satisfies` comes *first* in
the head because it says what the op is first (#chref("surface")).

== Decide now what can be decided now

When a call breaks the callee's `requires` and both sides are constants, translation rejects it without waiting to run.

#demo("examples/ch14/impossible.low")

As the diagnostic's story says, this once translated green and only stopped at run time. A program carrying a bit budget that did not fit shipped
in runnable form. A contract whose answer is available now is decided now.

The mistake in the other direction is rejected too. Declaring an error for a condition that `requires` already excludes means that error can
never happen.

#demo("examples/ch14/dead.low")

With `requires ne b 0 .`, `errors by_zero eq b 0 .` can never be true. If the declaration stays, callers write code to handle `by_zero`, and that
code never runs. Code that never runs is never tested, and untested code is wrong someday. The diagnostic says to remove one of the two ---
either make it the caller's responsibility (`requires`) or have the op handle it itself (`errors`).

#misconception[The more contracts you write, the safer][
  Contracts that overlap or promise impossible cases draw a wrong picture. Contracts must be *facts*. A `requires` and an `errors` fighting over
  the same condition blur who is responsible, which is why it is rejected. Good contracts are short and put responsibility in one place.
]

== Contract grades

#idx("contract grades")
A grade on a single contract clause decides when and how its condition is treated. The shape is `requires <grade> <condition> .`.

#dtable(
  columns: 3,
  id: "contracts-grades",
  caption: [Contract grades],
  [*Grade*], [*When looked at*], [*Meaning*],
  [(none)], [whenever possible], [Prove it if possible; otherwise leave it as the build mode says],
  [`static`], [at translation], [The processor must prove it; if it cannot, translation fails],
  [`debug`], [at run time], [Checked only while the build mode keeps checks],
  [`assume`], [never], [Only written down. Not used as a fact],
)

#demo("examples/ch14/grades.low")

The same condition is written two ways. `bump_checked` rejects 250 on entry and then removes the overflow check in `add a 1`. `bump_assumed` does
not check on entry. In exchange it does not use the condition as a fact either, so the overflow check *remains*. That is why giving it 255 stops
with overflow (`E-VM-OVERFLOW`), not with a contract violation.

`assume` is a place to write down what the processor cannot yet prove but a person knows. Written down, readers know it, and when the processor
grows it can be raised to `static`. Treating an unenforced condition as a fact would mean removing checks that should not have been removed, and
believing that was right when the condition turns out false. So `assume` is not a fact.

== Build modes decide the remaining checks

#idx("build mode")
Proven contracts leave no check in any mode. What a build mode decides is the treatment of contracts that *could not be proven*. The mode is written
in the source as `build <mode> .`.

#dtable(
  columns: 2,
  id: "contracts-modes",
  caption: [The four build modes],
  [*Mode*], [*Remaining contract checks*],
  [`debug`], [Kept; stop and say what broke],
  [`test`], [Kept like `debug`; tests are also built and run],
  [`release_safe`], [Kept; stop without a message],
  [`release_fast`], [*Removed*. Execution may continue with a broken contract],
)

#demo("examples/ch14/fast.low")

Under `build release_fast .`, `bump 250` does not stop and gives 251. The same source stops under `debug`. `release_fast` opens one more trust
boundary, where *a person promises* the contracts are true. It is one reason the claim that this language has no undefined behaviour is limited to
"the safe subset". Removing checks for speed is a choice you may make, but the fact of choosing must stay in the source. That is why the mode is a
`build` statement and not a command-line flag.

#realcase[Entry checks the processor cannot build][
  Not every expression in a contract can become an entry check. The processor in this edition builds entry checks from `requires` of shapes like
  `name comparison constant`, `len`, `elem_*` and field paths, and reports `W-CONTRACT-IGNORED` when it meets an expression it cannot build. While
  writing this book it turned out that an `ensures` containing an expression (`ensures le (mul ret 2) n .`) was not checked, without any warning. An
  unenforced contract is not used as a fact either, so no wrong optimisation results, but the fact that a promise goes unchecked should be
  reported. That is a place where the processor ought to speak, so it is a defect of this edition.
]

#recap[
  `requires` is the caller's responsibility, `ensures` and `errors` the op's, and the diagnostic says whose fault it was. Enforced contracts become
  facts and remove overflow, bounds and division-by-zero checks. `elem_*` expresses conditions over every element, and `contract` with `satisfies`
  names contracts. Violations between constants are rejected at translation, and so are declarations of impossible errors. `assume` is not a fact;
  build modes decide the treatment of unproven contracts, and `release_fast` removes those checks.
]
