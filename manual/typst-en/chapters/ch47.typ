#import "../../typst-ko/lib.typ": *

= Proofs about locks --- where heavy tools are really needed

#chapter-toc()

#prereq(
  ([#chref("tasks-channels"), Tasks and channels], [shared lock state does not exist in this edition yet (`E-LOCK-NOTYET`)]),
  ([#chref("lib-containers"), Containers and sorting], [`spsc` is a lock-free single-producer single-consumer ring buffer]),
  ([#chref("proofs-weak-memory"), Proofs about weak memory], [`release` and `acquire` synchronise message passing]),
)

#deepqa[
  Where did #chref("proofs-races") say heavy concurrency logic is needed, and why only there?
][
  At level 3, and in particular for locks. The discipline of levels 1 and 2 removes two flows touching the same place altogether, so set theory was enough. But a
  spinlock is a device where two flows *really do hammer the same word at the same time*. Here discipline cannot give safety; an invariant must. This chapter covers
  that reasoning --- concurrent separation logic.
]

#why[
  The design decided that "lock and rwlock are libraries over atomic CAS". For that sentence to be true, a spinlock built from one CAS must really give mutual
  exclusion. And to trust the judgement of using tools only as much as needed, you must understand why locks alone use Iris while most proof files in this repository
  are pure Coq. This chapter introduces the vocabulary of separation logic from scratch, reads the theorems about locks, rwlocks, deadlock and starvation, and sees
  how a *borrowed* proof in weak memory backs the standard library's `spsc`. Locks are also the topic with the most boundaries in this part.
]

#organizer[
  You will learn the connective `∗` meaning "separate pieces", Hoare triples for writing specifications, invariants shared by several flows, and ghost tokens that
  exist only in the proof, not at run time. Starting from the theorem that there cannot be two tokens, you will pick up what the specifications for creating,
  acquiring and releasing a lock stop. You will also see the two proofs for rwlocks, deadlock freedom with ascending lock order and no starvation under round
  robin, and the borrowing of a weak-memory program logic to confirm `spsc`, together with its price.
]

#chapter-questions()

== The vocabulary of separation logic

#idx("separation logic")
In ordinary logic, `A ∧ B` is "both are true". Separation logic adds one more connective. `A ∗ B` reads "A and B are true, and they are about *separate pieces of
memory*". Claims about memory must imply *ownership*. `x ↦ 3` does not mean "x holds 3" but "I own x and its value is 3". So `x ↦ 3 ∗ y ↦ 4` says x and y are
different places, and writing x does not spoil the claim about y. That is local reasoning.

It is the same idea as #chref("references")'s exclusivity rule. Actually the order is the reverse --- Rust's borrow checker moved the idea of separation logic into a
type system, and this language's exclusivity rule is in that lineage. #chref("proofs-ownership") is this chapter's idea brought down to translation time.

#dtable(
  columns: 2,
  id: "plock-vocab",
  caption: [The vocabulary of lock proofs],
  [*Term*], [*Meaning*],
  [Hoare triple `{{{ P }}} e {{{ Q }}}`], [Running `e` from a state satisfying `P` makes `Q` hold when it ends],
  [Invariant `lock_inv γ lk R`], [`(lk ↦ false ∗ R) ∨ (lk ↦ true)` --- if open, resource `R` is inside the invariant; if locked, it is in the locking flow's hands],
  [Ghost token `locked γ`], [`own γ (Excl ())` --- an accounting device absent at run time, circulating only in the proof],
)

The one-line invariant is the whole lock. `R` cannot be in two places at once, so only one flow holds `R`.

```text
          acquire — the CAS turns false into true
   ┌────────────────────────────────────────────┐
   │                                            ▼
 open    lk ↦ false ∗ R                  locked  lk ↦ true
         R sits inside the invariant             R and the token locked γ are in the holder's hands
   ▲                                            │
   └────────────────────────────────────────────┘
          release — hand back both the token and R
```

== There cannot be two tokens

#mathbox[Theorems in `LowentLock.v`][
  `locked_exclusive : locked γ -∗ locked γ -∗ False.` Assuming two tokens is a contradiction. This is the mechanical heart of mutual exclusion.

  `newlock_spec : {{{ R }}} newlock #() {{{ lk γ, RET lk; is_lock γ lk R }}}.` *Hand over* the resource to protect and you get a lock. Making the lock costs giving
  up the resource --- now the only way to reach it is to acquire the lock.

  `acquire_spec : {{{ is_lock γ lk R }}} acquire lk {{{ RET #(); locked γ ∗ R }}}.` Acquiring gives *both* the token and the resource. `∗` does the work --- the
  resource received is mine and overlaps with no one's, so afterwards you reason as if there were one flow.

  `release_spec : {{{ is_lock γ lk R ∗ locked γ ∗ R }}} release lk {{{ RET #(); True }}}.` To release you must return both token and resource, and nothing remains
  afterwards.
]

If two flows acquired at once there would be two tokens, which is false. One release specification stops two defects. Releasing without acquiring lacks
`locked γ` and cannot meet the precondition; using the resource after releasing fails because `R` was already returned.

The proofs share a three-step shape: open the invariant, run the CAS (splitting into success and failure), close the invariant. That the invariant may be opened
*only at an atomic moment* is Iris's rule, and thanks to it "no other flow can see the invariant briefly broken" is guaranteed.

#mathbox[A practical example --- two flows incrementing (`incr_spec`, `two_threads_spec`)][
  Two flows run `acquire ; c ← !c + 1 ; release` at the same time. Even incrementing concurrently they finish without races and the program yields a value. A
  lock-free `c ← !c + 1` --- the classic lost update --- cannot be proven, because `c ↦ n` can be used only after receiving it from the lock. Honestly: this theorem
  does not say the result is *exactly 2*. Saying the exact total needs more ghost counting. The goal of this file was that the lock protects the resource.
]

== Tools only as needed

#dtable(
  columns: 3,
  id: "plock-tools",
  caption: [Proof files and tools],
  [*Files*], [*Tool*], [*Why*],
  [Borrowing · loops · races · parallelism · RC11 · blocks · places · laundering · numbers], [Pure Coq], [Discipline, set theory, lattices and graph decisions suffice],
  [Locks · rwlock read side], [Iris], [Two flows really hammer the same place],
  [Weak memory programs · SPSC], [iRC11 · gpfsl (borrowed)], [Programs must be verified over weak memory],
)

This is also why Coq was chosen. Iris is Coq-only and Lean has no mature counterpart. *Where the tools that would one day be needed lived* decided the proof
language.

== rwlock --- two proofs of the same sentence

For the classic rwlock packing three meanings into one word --- 0 empty, n > 0 n readers, −1 write-locked --- *exclusivity of the write lock* was proven directly
(`wlocked_exclusive`, `wlock_spec`, `wunlock_spec` in `LowentRWLock.v`). Because CAS succeeds *only from 0*, "a writer enters while readers are present" is stopped.
Most real rwlock defects are exactly that.

*Fractional ownership* on the read side was already in Iris. Its rwlock interface is written with a fractional predicate `Φ : Qp → iProp`, and `rw_spin_lock` is a
verified instance. Readers get a fraction `Φ q` (several at once), writing and reading cannot coexist, and a writer gets all of `Φ 1`. What is valuable is that
"writing and reading are exclusive" has *two proofs of the same sentence*. Ours shows it by "CAS succeeds only from 0" (close to the implementation); the borrowed one
by ghost state (general, and covering fractions). The principle of building two ways and cross-checking (#chref("build-test")) holds once more at the proof level.

== Deadlock freedom and no starvation

#mathbox[Deadlock freedom (`no_deadlock` in `LowentDeadlock.v`)][
  If every flow takes locks *only in ascending order*, some flow can always progress in any state --- they are never all blocked at once. The seed is one sentence.
  Look at *the flow wanting the largest lock*. If it is blocked, another flow holds that lock, and by the discipline what that flow wants is larger still ---
  contradicting maximality. That breaking the discipline really deadlocks was also shown by computing a state (`violating_the_order_deadlocks`).
]

```text
 ascending order kept (A < B)          discipline broken
 flow 1   holds A → wants B            flow 1   holds A → wants B
 flow 2   wants A (waits)              flow 2   holds B → wants A
 → flow 1 takes B and goes on          → each waits for the other (deadlock)
```

Honestly --- *the tool does not enforce this discipline.* So it is "written this way it is safe", not "the compiler stops it". Making it a check would require static
orders on locks. And the lock's own specification (acquiring gives the resource) does not prevent deadlock, because it says "acquiring gives" and not "it will be
acquired". Acquire a lock twice and release once, and the second acquire never arrives.

#mathbox[No starvation (`no_starvation` in `LowentFair.v`)][
  Under a cooperative round-robin scheduler a ready task always runs *within the number ahead of it + 1 steps*. Not "eventually" but bounded. That the induction
  measure is *position*, not queue length, is the point --- rotation preserves length. That without rotation tasks starve was proven too.
]

The limits are clear. Cooperative scheduling *assumes yielding.* Before a task looping forever without `yield`, the scheduler can do nothing. Priorities and blocking
are outside this model too.

#misconception[Using locks also protects you from deadlock][
  A lock's specification is "if you acquire it, you receive the resource", not "it will always be acquired". Deadlock freedom
  (`no_deadlock`) holds only under the discipline that every flow takes locks *in ascending order only*, and this edition's tool does not
  enforce that discipline. Take a lock twice and release it once, and the second acquisition never comes. What the proof gives is a design
  rule, "used this way it is safe"; keeping that rule is still a person's job.
]

== A borrowed proof in weak memory --- `spsc`

Iris's language (`heap_lang`) is a sequentially consistent model. Proving code that writes weak memory orderings needs iRC11 and gpfsl, the Iris logics over RC11.
`LowentIRC11.v` built the first theorem on them --- in a message-passing program written with a `release` write and an `acquire` read, the reader always sees the data
*under RC11*. It does not assume sequential consistency. #chref("proofs-weak-memory")'s E4 rose from one execution to *a whole program*.

With that tool one standard library module was built.

#demo("examples/ch47/ring.low")

The producer *first* fills the slot and then publishes the index with `release`. The consumer reads that index with `acquire` before reading the slot.
`lowent_spsc_push_is_correct` and `lowent_spsc_pop_is_correct` say this pair is correct under RC11 (including full and empty checks).

```text
 producer                                 consumer
 buf[i] ← x          (fill the slot first)
 tail ← i+1  release ─────────────────▶   tail  acquire  sees i+1, then
                                          reads buf[i] → it must see x (under RC11)
```

#realcase[What "borrowed" means][
  The algorithm-level proof is gpfsl's `circ_buff`, and `mp_instance_gen_inv` is gpfsl's too. What this repository did was state and connect "the memory orderings
  we emit are exactly what that specification requires". So gpfsl and the Iris development version join the trusted base. There is one reason not to rebuild it ---
  keep two copies of the same thing and they diverge. And that is why *only one* was added. MPSC, MPMC and seqlocks have no grounds to borrow, and adding them without
  grounds would turn the standard library into a warehouse of "written as verified but verified by no one". This work, once noted as "heavy", turned out on measuring to
  be mostly assembling tools with no real risk. Before measuring, you do not know the cost.
]

#qa[
  If there is a proven lock, why is there no `lock` in this edition?
][
  What was proven is a CAS spinlock written in Iris's language, and that the real runtime's lock matches it is not verified --- the largest gap between this file and
  the implementation. And the shared lock state type has not been built yet (`E-LOCK-NOTYET`, #chref("tasks-channels")). Here proof comes first and implementation
  later. Instead of accepting what does not exist and building it later, it says it does not exist.
]

== What is not proven

- *The lock model is sequentially consistent.* Weak memory orderings are not in the `heap_lang` lock proofs. But since the default ordering is `seq_cst`
  (#chref("proofs-weak-memory")), level 3 code written with defaults is inside that model, and only code writing weak orderings is left to iRC11. Containment holds here
  too.
- *There is no guarantee the implementation is this spinlock.*
- *`two_threads_spec` does not state the exact total.*
- *Deadlock freedom relies on a discipline people keep.* No starvation assumes yielding; priorities and blocking are outside.
- *There is no guarantee our compiler emits that `spsc` program.* One-to-one emission of memory orderings and not reordering them are confirmed by tests.
- *Borrowed proofs enter the trusted base.* Two checkers (Coq 8.20.1 · Rocq 9.2) giving the same answer reduces the chance of checker defects, but the two files
  needing gpfsl are confirmed only under Rocq 9.2.

#recap[
  Separation logic's `∗` means separate pieces and gives local reasoning, and a lock is proven with one invariant --- "if open the resource is inside, if locked it is in
  the locking flow's hands" --- plus a ghost token. There cannot be two tokens, acquiring gives token and resource, and releasing returns both. The rwlock's exclusivity
  was confirmed by a direct proof and a borrowed one, ascending lock order does not deadlock, and round robin runs tasks within a bound. `spsc` was confirmed by borrowing
  a weak-memory program logic, at the price of a larger trusted base.
]
