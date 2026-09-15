#import "../../typst-ko/lib.typ": *

= Ownership --- one party responsible for disposal

#chapter-toc()

#prereq(
  ([#chref("references"), Borrowing], [many readers or one writer]),
  ([#chref("regions"), Regions], [a region is reclaimed all at once when it ends]),
  ([#chref("errors-design"), Designing failure], [someone has to receive a failure]),
)

#deepqa[
  The regions of #chref("regions") reclaim values all at once. Can resources that are disposed of *one at a time*, and whose closing *may fail* --- file
  handles, say --- be handled with regions alone?
][
  A region only rewinds bytes all at once; it cannot close files or flush buffers. And if closing fails, there is no place at a region's `end` to receive that
  failure. Such resources need *ownership*, where each value has a party responsible for disposing of it. That is this chapter.
]

#why[
  The old names of memory defects are *double free* and *missed free*, and the newer one is *use after move*. All three arise when "who is responsible for
  disposing of this value now?" has more than one answer. Lowent checks at translation that there is always exactly one answer. It adds one more thing ---
  if disposing *can fail*, it is not done silently. Regions (#chref("regions")) handled values that die together; this chapter handles values that die one by
  one.
]

#organizer[
  You will learn what `owned t` is and what `drop` does. You will pick up why reusing a moved value or disposing twice is rejected, and why differing ownership
  states across branches are rejected. You will see that disposal comes in two kinds, *release* (cannot fail) and *completion* (can fail), and why silently
  discarding a value that needs completion is rejected. You will also sort out at what strength this language's memory safety is guaranteed.
]

#chapter-questions()

== `owned` and `drop`

#idx("ownership")
`owned t` is a value that carries ownership. A value with ownership must be disposed of *exactly once*.

#demo("examples/ch19/sink.low")

`consume` takes an `owned buffer` and disposes of it with `drop h .`. `use_once` makes an owned value with `var h owned buffer be v .` and passes it to
`consume`. At that moment ownership *moves*. Now `consume` is responsible for disposing of `h`, and `use_once` can no longer use `h`.

Using a moved value again is rejected.

#demo("examples/ch19/moved.low")

Disposing twice is rejected too.

#demo("examples/ch19/twice.low")

The diagnostic notes that the specification always called this an error and that for a while nothing enforced it. Today translation stops it.

#qa[
  What happens when a value without ownership is passed?
][
  It is copied. Values like `u64` or `point` are copied when passed, and the original name stays usable. Only owned values move. Large values whose copying
  cost worries you are passed by borrowing with `ref` (#chref("references")). And when the place where a value-producing expression will land is empty, the
  expression cannot fail on the way, and the place belongs to that value alone, the value is built directly in place --- no intermediate temporary and no
  moving copy. These three conditions can be counted from the source alone.
]

== Where branches meet

Where an `if` splits and rejoins, the ownership state must be the same on every path.

#demo("examples/ch19/join.low")

On the path where `c` is true, `h` was disposed of; on the false path it is still alive. After the paths meet, nobody can say whether `h` is alive. Some
languages keep a hidden "already dropped" flag here, but Lowent requires the ownership state to be *statically* one thing. Dispose of it on both paths, or
pass it on both paths.

By the same principle, moving one `owned` field of an aggregate and then moving the whole aggregate again is rejected (`E-OWN-PARTIAL`). The receiver thinks it
got a whole aggregate, but one field inside already belongs to someone else.

== Release and completion

Disposal comes in two kinds.

#dtable(
  columns: 3,
  id: "own-release",
  caption: [The two kinds of disposal],
  [*Kind*], [*Examples*], [*How it is handled*],
  [Release], [Giving memory back], [Cannot fail, so it may happen silently where the lifetime ends],
  [Completion], [Closing a file, flushing a buffer, committing a transaction], [Can fail, so the author must write the call],
)

Release has no failure to swallow. Completion, done silently, has nowhere to hand its failure. The criterion is one: *can finishing fail?*

#idx("completion")
Which types require completion is declared by the program itself. If there is an op that takes the type as `owned` and returns a `result`, that is the
declaration "finishing this can fail". No new word is involved.

#demo("examples/ch19/complete.low")

Because `finish` takes an `owned journal` and returns a `result`, `journal` becomes a type needing completion. `session` finishes it by calling `finish j` and
returns that (possibly failing) result as its own. Letting it go out of scope without calling the completion is rejected.

#demo("examples/ch19/incomplete.low")

As the diagnostic says, automatic disposal is a release, and a release has nowhere to return a failure, so it would *swallow* the failure. Finishing that can
fail --- flush, commit, close --- must be written and called by the author.

#misconception[It is convenient for a destructor to close the file automatically][
  A C++ destructor or Rust `Drop` closes a resource when it goes out of scope. What if closing fails? A destructor cannot return a value, so it ignores the
  failure or stops the program. That is where the fact that a full disk kept the last buffer from being written silently disappears. Lowent allows this
  convenience only for release and makes completion explicit. If you really mean to discard both the value and its failure, you *say you are discarding it*
  with `drop` --- the diagnostic points that way too. Something vanishing silently and an author writing that it is discarded are different things.
]

== Not counting on the operating system to clean up

Many programs lean on "the operating system cleans up when the process ends anyway". Lowent does not. Because a forgotten disposal is caught at translation,
the same code holds in environments without an operating system --- firmware whose process never ends.

== At what strength is memory safety guaranteed?

The words "memory safe" mean something only when they say what is safe and how. This language separates the strengths.

#dtable(
  columns: 3,
  id: "own-strength",
  caption: [Memory rules and their strength],
  [*Rule*], [*Strength*], [*Meaning*],
  [Borrow exclusivity · no reference escape], [static + proven], [Stopped by translation; the rules' soundness is proven in Coq (sequential model)],
  [Dispose exactly once · no region escape], [static], [Stopped by translation],
  [Slice bounds · contracts], [dynamic], [Checked at run time; the check disappears when proven],
  [Dangling generational handles], [dynamic], [Generations compared at run time (#chref("lib-containers"))],
)

So calling this language "fully statically safe" would be wrong. It is a design that mixes what is stopped statically, what is stopped at run time, and what
is proven. What is proven, and what gap lies between the proofs and the compiler, is covered in #chrefs("proofs-ownership", "proofs-limits").

== Common mistakes

#antipattern[Passing an owned value inside a loop][
  #demo("examples/ch19/mistake_loopmove.low")

  Once `consume h` takes ownership in the first round, `h` in the second round already belongs to someone else. Translation does not
  need to follow the rounds one by one; it sees that "the loop body ends in a different shape than it began" and rejects it with
  `E-OWN-MOVED`. There are two fixes: move the consuming call out of the loop, or refill the moved place with a new value using `set`,
  so each round ends in the same shape.

  #demo("examples/ch19/loopmove_fixed.low")
]

#antipattern[Moving a value while thinking you only read it][
  #demo("examples/ch19/mistake_readmove.low")

  `let v u8 be h .` does not look at `h`; it *moves* it into `v`. A value with ownership moves the moment it is stored under a name, so
  the later `drop h` tries to destroy a moved value. To only look, borrow it with `ref h` inside the same op. As the diagnostic notes,
  borrows across an op boundary are not lowered yet in this edition.
]

#antipattern[Believing a second name makes a copy][
  #demo("examples/ch19/mistake_twonames.low")

  A value like `u64` is copied when stored under another name. A value with ownership is not copied; it moves. With two copies nobody
  could say who destroys it, and destroying both would destroy it twice. After `var h2 owned buffer be h .`, `h2` is the only owner.
]

#misconception[Leaving out `drop` leaks the value][
  #demo("examples/ch19/implicit_release.low")

  The path where `c` is 0 leaves without `drop`, yet it is neither rejected nor leaked. *Release*, which gives memory back, cannot fail,
  so it happens quietly where the lifetime ends. Only *completion*, which can fail, must be written by the author (`incomplete.low`).
  Where branches *meet again*, though, the ownership state must match (`join.low`). A path that leaves never meets the other, so that
  rule does not apply.
]

== This chapter's syntax at a glance

#dtable(
  columns: 3,
  id: "ownership-glance",
  caption: [Ownership syntax --- shape · meaning · why it looks this way],
  [*Shape*], [*Meaning*], [*Why*],
  [`input h owned buffer .` · `var h owned buffer be v .`], [a value with ownership], [one name is responsible for destroying it],
  [`consume h`], [passing moves ownership], [no use after the move --- `E-OWN-MOVED`],
  [`drop h .`], [say it is destroyed now], [destroying twice is rejected],
  [`set h v .` (after a move)], [refill the moved place], [a loop body ends in the same shape],
  [different ownership states per branch after `if`], [rejected (`E-OWN-JOIN`)], [no hidden "was it dropped" flag],
  [`fn finish input j owned journal . output result …`], [declares that finishing this type can fail], [completion declared without a new word],
  [leaving scope without calling completion], [rejected (`E-OWN-INCOMPLETE`)], [a failing finish is never swallowed],
  [leaving without a word, when no completion is needed], [released quietly], [release cannot fail],
)

#recap[
  `owned t` must be disposed of exactly once and moves when passed. Reusing a moved value, disposing twice, and differing ownership state across branches are
  rejected. Disposal splits into release, which cannot fail, and completion, which can; a type needing completion is declared by an op that takes it `owned`
  and returns a `result`. Silently discarding a value that needs completion is rejected. Understand memory rules by strength: static, dynamic and proven.
]
