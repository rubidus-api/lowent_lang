#import "../../typst-ko/lib.typ": *

= What Lowent sets out to do

#chapter-toc()

#why[
  When the first chapter of a new language opens with syntax, the reader memorises words and misses *why those words look the way
  they do*. Most of Lowent's syntax follows from a single goal --- *reading only the head of an op should tell you what that op can
  and cannot do.* With that goal in place first, the unfamiliar rules that come later (clauses closed by a full stop, `fn` marking
  purity, capabilities handed in as arguments) stop looking like arbitrary taste and start looking like faces of one design. So the
  goal and the way to read this book come first.
]

#organizer[
  You will learn what problem Lowent was built to solve and the five ideas that make up its answer (marking purity, contracts,
  effects paired with capabilities, memory without a garbage collector, and cross-checking two back ends). You will also settle
  that the language is still experimental, how the examples in this book are verified, and which devices to read it with.
]

#chapter-questions()

== Can you tell from the head alone?

The thing people who fix programs do most is *find out what someone else's function does before calling it*. It is called
`parse_header`, but does it open a file? Does it allocate? How does it report failure? Does it start a thread? In most languages
the answers live in the *body*. You read the body, then the bodies of the functions it calls, then the level below that.

Lowent lifts those answers up into the *head*. Here is the first op head this book meets.

```lowent
proc main input out cap io . output u8 . effects io .
```

Even without knowing the words, some things can be read. This op is a `proc` (it is not pure). It *receives* an `io` capability
under the name `out`. It returns a `u8`. It performs an effect called `io`. What is not written can be read too --- this op does not
allocate (there is no `alloc`), has no file-system capability, and starts no threads (there is no `concurrent`). *What is not
written cannot be done.* The compiler checks it.

Let us look at a small program that actually runs.

#demo("examples/ch01/heads.low")

- `module heads .` is this file's name tag. Every file starts with this one line.
- Lines starting with `rem` are words for people (comments). The compiler does not read them.
- `fn area … do … end` is an op that computes an area. `input w u64 .` means "take an unsigned 64-bit integer under the name `w`",
  and `output u64 .` is the type of the value returned. `requires le w 1000 .` is a promise: "`w` must be at most 1000". It is there
  so the product cannot overflow.
- `return mul w h .` returns `w` times `h`. The operation name comes first and the arguments follow. A statement ends with a detached
  period.
- `main` is the op where the program starts. `let a be u64 area 3 4 .` gets 12 into `a`, and `write_out out 1 "…"` writes a line to
  standard output (number 1). That line is possible because the head receives `cap io` and declares `effects io`. The 12 it returns
  becomes the exit value handed to the operating system.

Now let the head lie. An op declared as a pure `fn` tries to write to the screen on the quiet.

#demo("examples/ch01/mistake_hiddenio.low")

The compiler refuses without running anything. The single word `fn` is a promise that "this op does not reach outside", and when the
body breaks that promise, translation stops. So whoever calls a `fn` need not open its body. To fix it, say what is true:
`proc area_loud … effects io .`.

#qa[
  If the head has to say that much, doesn't the code get longer?
][
  It does. The reading gets shorter in exchange. An op is written once, but it is read at every place that calls it, on every day
  someone fixes it. Lowent bets on that asymmetry. And because the compiler checks every clause of the head, the head does not go
  stale the way a comment does.
]

== Five ideas

Five mechanisms make the head trustworthy. Parts II to VIII of this book unfold them one by one.

#dtable(
  columns: 3,
  id: "intro-five",
  caption: [The five ideas behind Lowent],
  [*Idea*], [*What the head says*], [*Where*],
  [Separate pure from impure], [`fn` or `proc`], [#chref("first-program")],
  [Write promises and have them checked], [`requires` · `ensures` · `errors`], [#chref("contracts")],
  [Pair what is done with who allowed it], [`effects` · `input … cap …`], [#chrefs("effects", "capabilities")],
  [Keep memory safe without a collector], [`region` · `owned` · `ref`], [#chrange("regions", "fixed-memory")],
  [Run two ways and compare], [(a tool, not the head)], [#chref("build-test")],
)

*Marking purity.* A `fn` is pure --- the same inputs give the same output and it leaves no trace outside. A `proc` may not be. Which
one it is is *always* written. If a pure `fn` tries to print, the compiler rejects it.

*Contracts.* An op writes what it requires of its caller (`requires`) and what it guarantees on return (`ensures`). When the values
are constants, the check happens at translation time; otherwise it happens at run time, on entry and on return. A proven contract
removes checks in the body --- a contract is both documentation and grounds for optimisation.

*Effects and capabilities.* An op declares the marks it leaves on the world with `effects`, and is *handed* the capability to do that
#idx("ambient authority")
work as a `cap` input. Lowent has no ambient authority --- no global output or global allocator that can be used secretly from
anywhere. If a capability is not visible in the argument list, the op cannot do that work.

*Memory without a collector.* To use memory safely without a garbage collector, Lowent ties the lifetime of values to *regions* and
checks a borrowing rule of one writer or many readers. For machines without an operating system there is a separate path that only
allocates from a fixed window that never grows.

*Cross-checking two back ends.* The compiler, `lowentc`, runs the same intermediate representation two ways. One is a virtual machine
(VM) that runs by itself; the other is native code, emitted as C and turned into machine code by a C compiler. If the two give
different answers, that is a compiler defect. Every example in this book passed this cross-check.

#misconception[Lowent proves everything statically][
  It does not. Borrowing, regions and integer widening are stopped at translation time, but bounds checks in unproven places,
  overflow, and contract checks are stopped *at run time*. It is a mixed design. What is proven and what is not is gathered in
  Part X --- #chref("proofs-limits", cap: true) is devoted entirely to *what is not proven*.
]

== One meaning, one spelling

The *entropy* in Lowent's name is the measure of disorder. The language tries to reduce the places where one meaning can be written
several ways. A few choices follow.

- A field is read only as `field p x`. There is no glued dot like `p.x`.
- Statements and clauses are closed with a free-standing full stop `.`, and a block is always `do … end`.
- The clauses of a head are written in one fixed order. A wrong order is rejected, and `--fmt` fixes it.
- Removed words (`loop`, `give`, `on` and so on) are not quietly accepted; they are rejected with `E-VOCAB-REMOVED`.

With one spelling per meaning, people and tools read the same code in the same shape. The examples in this book all look alike not
because of the author's taste but because the language only allows that shape.

#qa[
  If the grammar is that narrow, how do new features get in?
][
  Instead of adding words, a feature comes in *as one more clause in a frame that already exists*. `pipe` is not a new loop syntax
  but a frame that writes one word per line inside a `do … end` block (#chref("pipe")), and parallelism is not a new construct but a
  `parallel` clause in an op head (#chref("parallel-atomic")). That is why the clause-order table is one of the most important
  tables in this language (#chref("surface")).
]

== Where the language stands

Lowent is an *experimental language*. It has not reached version 1, and its grammar and standard library still change. This book is
written against the grammar the compiler in the repository actually accepts today, and when the compiler changes the book follows.
That is why the book has its own edition number.

The compiler is written in C23, and its only external dependency is a vendored copy of `proven_c_lib`. Native code is emitted as C
and built with the system's C compiler, so it usually runs wherever there is a C compiler, and microcontrollers without an operating
system are among its targets (#chref("hardware")).

#realcase[How the examples in this book are verified][
  Every `.low` file in this book lives in `docs/manual/examples/`, and each carries a one-line directive at the top. With `rem run: …`
  it must be run on both the VM and native code and give the same output; with `rem expect: E-…` the compiler must reject it with
  exactly that diagnostic; with no directive it must pass `--check`. The run results on the page were not copied by hand --- they
  are exactly what the verification script left behind.
]

== How to read this book

This book was written with two kinds of reader in mind.

*Readers new to programming* read from Part I in order. Every piece of syntax comes with both "what it does" and "why it looks this
way", and each example is followed by an explanation of what its lines do. The "Common mistakes" at the end of Chapters 2 to 37
show the errors beginners really run into, with their diagnostics as they are. There is no need to fear error messages. In this language a diagnostic
does not say "you are wrong"; it says "this is where the code differs from its promise". Learn the first words from the table below.

*Readers who have used other languages* can follow just the example code and each chapter's "syntax at a glance" table. The examples
and that table show the chapter's syntax together. If you know C, Parts V and VIII will come easily; if you know Rust, the borrowing rules
of Part V will look familiar. Places where an idea carried over from a familiar language goes wrong here are collected in the
"A common misconception" boxes.

#dtable(
  columns: 2,
  id: "intro-first-words",
  caption: [First words],
  [*Word*], [*Meaning*],
  [source · module], [one `.low` file a person writes · the named unit that file forms (#chref("modules"))],
  [compiler · translation], [the program that reads the source, checks it and turns it into something runnable (`lowentc`) · that work],
  [diagnostic], [an error (`E-`), warning (`W-`) or note (`N-`) from the compiler. Its name is the key to look it up (Appendix B)],
  [value · type], [one piece of data such as a number or text · what kind it is and how many bits (`u64` is an unsigned 64-bit integer)],
  [local name], [a name given to a value: `let` if it never changes, `var` if it does (#chref("locals"))],
  [op], [a named piece of work; a function in other languages. `fn` if pure, `proc` otherwise (#chref("ops"))],
  [effect · capability], [the trace an op leaves outside (`effects io`) · the permission to do that (`cap io`) (#chrefs("effects", "capabilities"))],
  [contract], [conditions an op requires before it takes a call and ensures when it returns (`requires` · `ensures`) (#chref("contracts"))],
)

Every chapter opens the same way. It lists the earlier chapters it leans on (*What to know first*), asks one question that makes you
recall them, says why the chapter sits where it does and what you will have by the end, and then shows the questions the chapter
answers. The devices you meet inside the text are these.

#dtable(
  columns: 2,
  id: "intro-devices",
  caption: [Devices used in the text],
  [*Device*], [*Use*],
  [Q & A], [A question likely to come up while reading, and its answer],
  [A common misconception], [A plausible but wrong idea, and why it is wrong],
  [In practice], [Something that actually happened in the language or its standard library],
  [The mathematics], [A formal explanation you can skip without losing the thread],
  [Demonstration], [An example file and the actual output left by the verification script],
)

On a first reading, take Parts I to IV in order. After that you can jump to whichever part you need. For the standard library go
to Part IX; to find out how far this language's claims are true, go to Part X.
