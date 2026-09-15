#import "../../book/lib.typ": *

= What is not proven

#chapter-toc()

#prereq(
  ([#chref("proofs-why"), Why prove], [proven, exhaustively checked and sketch are not mixed]),
  ([#chref("build-test"), Building and testing], [five ways of checking the compiler against something]),
  ([#chref("proofs-syntax-hash"), Proofs about syntax and hashes], [the front end is the weakest seam]),
)

#deepqa[
  Why did #chref("proofs-why") call "a program written in a proven language is correct" a misconception?
][
  Because proofs are about *the language's rules*, not *your program*. The theorem that the borrowing rules are sound says only that programs passing translation have no borrowing
  violations. This chapter follows that boundary to the end and gathers in one place what the proofs rely on and what they do not cover.
]

#why[
  Documents boasting of proofs usually do one of two things --- they do not write the boundaries, or they excuse themselves in one line at the front and forget. Both lead readers to
  false confidence. *The value of a proof comes from knowing what it does not cover.* That this chapter ends the book is no editorial accident. Only after knowing what the earlier
  chapters claimed do the boundaries mean anything. Quoting this book without reading this chapter makes it say things it did not say.
]

#organizer[
  You will learn the trusted base the proofs rely on (the Coq kernel, the C compiler, BLAKE3, borrowed proofs and so on) and how much collapses if each is wrong. You will separate the
  devices bridging the gap between model and implementation into empirical and static, and understand that the weakest seam is the front end. You will also sort out what remains by
  topic, why this language must not be called "fully statically safe", and in the end what you may trust.
]

#chapter-questions()

#idx("trusted base")
== The trusted base --- what proofs rely on

#dtable(
  columns: 3,
  id: "limits-tcb",
  caption: [The trusted base],
  [*Trusted*], [*Why trusted*], [*If wrong*],
  [The Coq/Rocq kernel], [A small core, and two versions (8.20, 9.2) give the same answer], [Every theorem becomes meaningless],
  [Iris], [Needed only for the lock proofs], [Only the lock theorems collapse],
  [iRC11 · gpfsl · Iris's `rw_spin_lock`], [Proofs of weak memory, SPSC and rwlock read side were borrowed], [Only those theorems collapse],
  [The C compiler (gcc, clang)], [There is no alternative], [Native code diverges from the intermediate representation],
  [BLAKE3's collision resistance], [A premise of content addressing], [The cache sees different contents as the same],
  [Operating system and hardware], [There is no alternative], [Everything],
  [`proven_c_lib` (base library)], [Has its own tests], [Data structures go silently wrong],
  [The model captures the language correctly], [Confirmed by people reading it], [The gap below],
)

== The largest gap --- between model and implementation

```text
The theorems are about a model.
The model was written by people.
The compiler is different code written in a different language.
That the two are the same is not proven.
```

These are the devices joining the seam.

#dtable(
  columns: 3,
  id: "limits-seams",
  caption: [Devices joining model and implementation],
  [*Device*], [*What it does*], [*Character*],
  [Bounded exhaustive model checking], [Whether model and implementation verdicts agree on every case of small size], [Empirical],
  [Events extracted from real programs], [Feeds borrowing events of real programs to the same model], [Empirical · real programs],
  [Back-end cross-check], [Whether the VM and native code give the same answer], [Empirical],
  [Analysis self-accusation (`E-VM-ANALYSIS`)], [Refutes the compiler's "may be removed" by execution], [Empirical],
  [Certificate recheck], [An independent checker re-examines the arithmetic grounds of removed checks], [Static],
  [Regression tests], [Changed output becomes visible], [Regressions only],
)

The first four are empirical. They say not "the output is correct now" but "these inputs did not disagree". The certificate recheck is different in character. It speaks even when no
input hits that place. But it too *trusts the facts and checks only the reasoning*. The two layers catch different things. Cross-checks caught defects proofs missed, and proofs caught
defects cross-checks missed. That is why they are overlapped.

*The weakest seam is the front end (lexer and parser).* The back-end cross-check compares the VM and native code, but both pass through the same front end. If the front end is wrong,
both are wrong the same way. An attempt to cross-check the parser directly against the grammar model did not get enough pairs to conclude anything, because the model is smaller than the
real grammar. Closing this place requires a second independent implementation of the front end to compare against, and that cost (maintaining two) has not been paid yet.

#realcase[Discrepancies this book found][
  Every example in this book was run on the VM and natively and compared, and rejected examples were checked down to their diagnostic codes. In the process, close to twenty
  discrepancies between the compiler and the specification came to light. Some are written in the text --- the effects of ops called through `method` not spreading to the caller
  (#chref("traits")), `ensures` with an expression going unchecked without warning (#chref("contracts")), `arg` numbering differing between the two back ends (#chref("first-program")),
  and the precedence of comparisons and `and` in the `expr` island differing from the specification's table (#chref("expr")). All are discrepancies on the side of *implementation* and
  *documentation*, not the proofs' *model*. Without the discipline of actually running examples before printing them, this book would have written those discrepancies as facts.
]

== What remains --- by topic

#dtable(
  columns: 2,
  id: "limits-core",
  caption: [Language core and tools],
  [*What*], [*Status*],
  [Effect system], [Proven. The rule by which `task_group` absorbs `concurrent` and effect closure are outside the model],
  [Effect-based optimisation], [Proven (reordering, common subexpression elimination, memoisation, dead code elimination). Call boundaries and the moment of stopping are outside],
  [Loop termination], [Not covered. Infinite loops are legitimate programs],
  [Soundness of the whole type system], [Partial. There is no effect-row and no proof of it],
  [Completeness of normalisation], [Partial. Effects normalised as a set (proven). Contract clauses are sensitive to written order, proven to be the safe direction],
  [Multi-implementation compatibility of bundle hashes], [Unsettled. A specification-level promise],
  [Parser ≡ grammar model], [Not proven],
)

#dtable(
  columns: 2,
  id: "limits-conc",
  caption: [Concurrency],
  [*What*], [*Status*],
  [General RC11 weak orderings], [Partial. Metatheorems within the model are proven. Only SPSC (a borrowed proof) verifies a specific lock-free algorithm under weak orderings],
  [Deadlock freedom], [Proven. But the ascending lock order discipline must be kept by people (the tool does not enforce it)],
  [No starvation], [Proven. Assumes round-robin and yielding; priorities and blocking are outside],
  [Implementation of level 1 parallelism], [Implemented. The theorems do not verify the implementation],
  [Lock-free data structures], [One SPSC ring buffer. MPSC, MPMC and seqlocks have no proof to borrow, so they were left out],
  [Atomic orderings → machine code], [Emitted 1:1 as C11 atomics, so the rest is the C compiler's (trusted base) job. The mapping itself is confirmed by tests],
)

#dtable(
  columns: 2,
  id: "limits-scope",
  caption: [Feature scope],
  [*What*], [*Status*],
  [Floating-point properties (rounding, NaN, −0)], [Not covered],
  [Floating-point support in the interval analysis], [None],
  [Effect atoms such as `page_fault` and `blocking`], [Vocabulary only, no primitive operations],
  [Effect-row polymorphism], [None],
)

#demo("examples/ch43/floats.low")

0.1 plus 0.2 is not equal to 0.3. This is not a defect but a property of IEEE 754, and this language's proofs did not cover that property. Floating-point equality is asked with a
tolerance, as in `close_enough`. In computations involving floating point, this book's "proven" guarantees nothing.

== Not sold as "fully statically safe"

```text
Stopped statically   borrowing · regions · reference escape · parallel overlap · integer widening
Stopped at run time  bounds checks where unproven · overflow · contract checks
                     dangling references of generational handles
```

This language is a *mixed* design. And it does not call that static. Written honestly as a comparison, Rust has proofs through RustBelt, and Zig does not claim safety (an honest stance
too). Lowent is "has proofs --- *mainly for the sequential fragment*". Remove that qualifier and it becomes a lie. And the `release_fast` build mode removes the remaining contract checks,
opening one more trust boundary (#chref("contracts")). "No undefined behaviour" is true only within the safe subset.

#qa[
  So what do all these proofs buy in the end?
][
  Two things. First, *what the compiler must reject* is decided by argument, not feel. Whether one rule is enough, whether a condition is `≤` or `<`, come from proofs. Second, defects
  the implementation hit remain as *theorems*. Stories are forgotten, but if someone removes the check, the proof breaks. Proofs do not guarantee that programs are correct, but they
  systematically reduce the places where the language is silently wrong.
]

== So what may you trust

```text
✔ Integers never silently change value                  (numbers · proven)
✔ Code passing translation has no borrowing violations  (ownership · proven, sequential)
✔ Effect declarations cover effects that happen         (effects · proven, within the model)
✔ Safe code has no data races                           (concurrency · proven)
✔ Parallel results equal sequential, bit for bit        (parallel · proven)
✔ With the default ordering you may think sequentially  (weak memory · proven, ∀ executions)
✔ Removed bounds checks are at places argued in range   (bounds · proven + rechecked)

△ The compiler follows this model exactly               (empirical)
△ Code stating weak orderings                           (only SPSC proven — the rest is subject to audit)
✘ Floating-point details                                (not covered)
```

You may trust documents that have these last lines. Documents without them need a second look.

#recap[
  The proofs rely on a trusted base such as the Coq kernel, the C compiler, BLAKE3, borrowed proofs and the operating system. The largest gap is between model and implementation,
  bridged empirically by exhaustive checking, cross-checks and self-accusation, and statically by certificate rechecks. The weakest seam is the front end. Loop termination, floating
  point, effect-rows and the parser's correspondence to the model are outside the proofs. This language mixes static and run-time checks, and its proofs are mainly about the sequential
  fragment. Read with what you may trust, what is empirical and what is not covered kept apart.
]
