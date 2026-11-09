# 10 Concurrency

(1) This clause specifies the places where several things **proceed together**. Concurrency is not a special feature
      in this language but one more place where the effect rules of ⟦§7⟧ apply unchanged.

## 10.1 Concurrency is an effect too

(1) Making another flow of execution, or waiting for its progress, is an effect. `wait` and `concurrent` are
      those (⟦§7.1⟧), and they shall be written in the contract.

(2) What separates `wait` from `concurrent` is **who wakes it up**.

> [!표] The two kinds of waiting
> #table(columns: (auto, 1fr), stroke: 0.5pt + rgb("#bbb"), inset: 6pt,
> [*Effect*], [*Meaning*],
> [`wait`], [it can be suspended. What wakes it **is not another task of the same program** (the kernel · a device wakes it)],
> [`concurrent`], [it ends only **when a peer runs**. There has to be an executor to run that peer],
> )

> [!산문]
> For example, waiting for data to come from a file is `wait` — what wakes it is not another task of the same
> program but the outside.

> [!참고]
> Why this distinction matters: a small device without an operating system may have no «executor that runs peers».
> Then a program that uses only `wait` runs as it is, but a program that uses `concurrent`
> does not. It is written in the effects, so it can be known **at translation time**.

## 10.2 Actors

(1) An ⟦액터|actor⟧ is a unit of execution with its own state. The state exists only inside the actor and
      cannot be touched directly from outside.

(1a) So **reading the state fields** of an actor value **from outside** is refused (`E-ACTOR-FIELD`).
      The only way to reach the state is messages — put an op that returns what is needed in the actor.

(1b) A state field cannot be **a borrow** (`ref` · `mut_ref`) (`E-ACTOR-STATE-REF`). A borrow cannot outlive what it was
      borrowed from (⟦§8.4.1⟧), but a state field remains for as long as the actor lives, and there is no place to write
      what was borrowed.

(1c) The state fields of an actor just made with `spawn actor` are **all 0**. 0 is not a slice, so
      **reading before setting up** a field that holds a slice or a borrow does not conform
      (`E-ACTOR-UNINIT`). The processor refuses the case where **the first message sent** to that actor reads such a field
      without setting it up — it looks within the straight order of statements.

> [!산문]
> Until 2026-09-16, before (1c), that place was **where the two back ends parted**: the VM stopped with «not a
> slice», and native silently gave `none` (defect note #53). One of the two was a lie, but
> the canon did not say which. Instead of choosing an answer, it makes that place **unreachable**.

(1d) **A name holding an actor can start with the literal `0`** — an actor not yet spawned (the same as handle 0 of ⟦§10.3⟧ (2d)). Other numbers
      do not go into an actor place (`E-TYPE-LET` · `E-TYPE-VAR`). Sending a message with `send` · `spawn send` · `try spawn send` to an actor that is 0, or
      `drain`ing it, stops the program (`E-VM-HANDLE-ZERO`). Use it after it holds `spawn actor <name>`.

```lowent-거부: filling an actor place with a number other than 0 · E-TYPE-VAR
module ex_actor_number .

actor counter do
  state do
    n u64 .
  end .
  fn get output u64 . do
    return n. .
  end .
end .

export proc f output u64 . effects state .
do
  var c counter 7 .
  return 0 .
end .
```

(2) Ops inside actors also follow the kinds of ⟦§6.4.1⟧ — if it changes state it is `proc` and
      shall write `effects state`; if it only reads, it is `fn`.

(3) Declaring it pure while changing state is refused at translation.

(4) If an actor's op `panic`s (⟦§6.5.9⟧), that actor **can be set up again.** Then that
      actor's state returns to the start and that message is processed again. What comes back to life is
      **only its own state**; other actors are not touched.

(5) Until when it is set up again is decided by the clause `failure restart <policy> .`. The policies are the following three and no more.

> [!표] Actor failure policies — a closed set of three
> #table(columns: (auto, 1fr), stroke: 0.5pt + rgb("#bbb"), inset: 6pt,
> [*Policy*], [*Meaning*],
> [`max <number> [within <number> <unit>]`], [sets it up again that many times. When used up, passes it upward],
> [`never`], [does not set it up again — passes it upward the first time it blows up],
> [`always`], [keeps setting it up again without counting],
> )

(5b) The units of `within` are a closed three, and both singular and plural are accepted —
      `second`/`seconds` · `minute`/`minutes` · `hour`/`hours`. Any other word does not
      conform.

(5c) If the processor cannot count the period of `within`, it **says so** (`W-NOT-YET`). It does not silently read a clause that writes a period
      as `max <number>`.

> [!참고]
> Both singular and plural are accepted for reading's sake — `within 1 second` and
> `within 5 seconds` are written as people write. This is not **a second way of saying the same
> thing** (⟦§7.1.1⟧): the meaning is one and only the spelling differs with the number, and the processor
> reads both as the same number.

(5a) An actor has **one** failure policy (`E-ACTOR-FAILURE`). So does its mailbox policy
      (`E-ACTOR-MAILBOX`, ⟦§10.5⟧). With two policies **nobody could say** which stands,
      and the answer would become the processor's internal affair.

(6) If it blows up after using them all, that failure **is passed upward.** No actor is left silently
      stopped.

(7) Setting up again applies only to `panic`. A broken contract (⟦§6.4⟧) is not set up again
      — that is the program breaking a promise it wrote itself, so doing it again gives the same.

> [!산문]
> Why «let it blow up»? There are failures for which setting up again is better — cases where the state somehow became strange.
> Code that rolls back such failures one by one by hand is long, and when it is long that code itself
> goes wrong. Returning the state to the start is **short recovery that is always right**.

```lowent 예제: an actor
module ex_actor .

actor counter do
  state do
    value u64 .
  end .

  rem It changes state, so it is `proc` and `effects state`.
  proc inc output u64 . effects state .
  do
    set value. expr value. + 1 . .
    return value. .
  end .

  rem It only reads, so it is `fn` and has no effects.
  fn get output u64 .
  do
    return value. .
  end .
end .
```

```lowent-거부: declared pure while changing state · E-EFFECT-PURITY
module ex_actor_bad .

actor counter do
  state do
    value u64 .
  end .

  fn inc output u64 .
  do
    set value. expr value. + 1 . . rem it changes state but is an `fn`
    return value. .
  end .
end .
```

## 10.3 Making flows of execution

(1) `spawn` makes a new flow of execution. Making one is an effect and shall be written in the contract.

(2) Whether it **waits for** the flow it made **to end** or detaches it is separated by effects —
      detaching is `detach`.

(2a) `spawn <op> [arguments…]` gives a **handle** that points at that flow. The type of the handle is `task T`, where `T` is that op's
      output type. If it is used only as a statement without receiving the handle, that flow is detached.

(2b) `await <handle>` waits until that flow ends and gives its result. The type of the result is the handle's `T`. Waiting for a value that is not
      a `task T` is refused (`E-TYPE-KIND`). While waiting, other flows proceed — waiting does not stop the machine.

(2c) Waiting for something that cannot end stops the program. The processor shall say where.

(2d) **Handle 0 is a handle that points at no flow.** A name declared `task T` can start with the literal `0` — the spelling for leaving the place
      empty before receiving a flow. No other number can be a handle (`E-TYPE-LET` · `E-TYPE-VAR` — numbers do not point at flows).
      Waiting for handle 0 stops the program (`E-VM-HANDLE-ZERO`). The processor builds the handles `spawn` gives as values other than 0,
      and filters out 0 at every place that uses a handle — an empty handle **never silently points at another flow.**

```lowent-거부: making a handle from a number other than 0 · E-TYPE-VAR
module ex_handle_number .

fn one output u64 . do
  return 1 .
end .

export proc pick output u64 . effects state concurrent .
do
  var h task u64 7 .
  task_group do
    set h. spawn one . .
  end .
  return await h. . .
end .
```

> [!참고]
> Handle 0 is in the same position as the null pointer of other languages. Being able to leave it empty is convenient, but using what was left empty
> shall **stop on the spot** rather than grab something unrelated. This language counts handles from 1 so that 0 overlaps no flow,
> and checks for 0 at every place it is used.

> [!참고]
> Why waiting is an effect. `await` computes no value but **spends time** — the caller has to put
> that cost into its budget. So a pure op cannot wait (⟦§7.3⟧).

(3) `spawn actor` makes an actor, and `send` sends a message to that actor. An actor processes messages
      **one at a time**.

(3a) Values can be carried with a message — written **side by side** after the message name, as in `send <actor> <message> <value>…`.
      On the receiving side the actor instance is treated **like the first parameter**,
      and the values written follow it.

(3b) Sending a value with ownership **hands over** that ownership. The sender can no longer use that value,
      and using it is refused at translation (⟦§8.5⟧).

> [!산문]
> Why hand it over? If two flows hold the same value together, races arise. If ownership moves only
> through messages, at any moment only one flow can touch that value, and then races
> **cannot happen** — not prevented, but with no place to exist.

```lowent 예제: making an actor and sending a message
module ex_spawn .

actor counter do
  state do
    value u64 .
  end .

  proc inc output u64 . effects state .
  do
    set value. expr value. + 1 . .
    return value. .
  end .
end .

rem Make an actor (`spawn`) and send a message (`send`).
export proc use_counter output u64 . effects state .
do
  var c counter spawn actor counter . .
  return send c. inc . .
end .
```

> [!산문]
> It matters that an actor processes messages one at a time — the state inside **is not touched
> at the same time**. That is why locks need not be taken by hand.

> [!주의] Concurrency is not a world outside the checks
> In many languages concurrency is the area of *"be careful from here on"*. In Lowent
> **the same effect rules apply unchanged** — what it waits for, whether it changes state, whether an executor
> is needed, are all written in the contract and checked by the processor.

## 10.4 The place that binds flows — `task_group`

(1) `task_group do <statements> end .` **binds into one** the flows made inside it. At the place where the block
      ends, the bound flows have all ended, and it does not leave with even one remaining.

(2) So a flow **cannot outlive the block that made it.** This is how this language handles the lifetime of
      flows, and detaching (⟦§10.3⟧ (2)) is the only exception to that discipline.

```도해: the lifetime of flows is confined inside the task_group block
op        ──┬── task_group do ─────────────────── end ──▶ continues
            │
h1          │   spawn ├── work ──┤ end
h2          │       spawn ├── work ──────────┤ end
            │
            └─ opened at task_group and closed at end --- flows live only in between
   await h1 · await h2 wait for each flow to end. No flow remains when passing end
```

(3) Inside a group, `spawn <op> [arguments…]` **puts a job in the queue.** It is the same word, but
      `spawn actor <name>` makes one actor, and the two are told apart by what follows.

(4) `task_group cancel_on_error do … end .` **propagates cancellation.** If one bound flow ends with an error,
      the **siblings** not yet started or waiting are treated as having ended with that error.

(5) Cancellation **does not roll back.** The effects of flows that already did work do not go back, and cancellation only
      keeps work still to come from being done. So *"which siblings were cancelled"* depends on order,
      and this document does not fix that order.

(6) The error itself shows through `await` (⟦§10.3⟧ (2b)) — cancellation does not swallow errors.

> [!산문]
> Why bind them? A flow made and forgotten remains after the program ends, or touches what is already gone,
> or produces a failure nobody reads. When the block holds that lifetime, those three disappear **by grammar**
> — there is no place to forget.

## 10.5 Delivering messages later

(1) `send` (⟦§10.3⟧ (3)) waits until the actor processes the message. `spawn send <actor>
      <message> [arguments…]` does not wait but **puts it in the mailbox and returns at once.**

(1a) `try spawn send …` does not stop when the mailbox is full but gives that as a value — its type is
      `result u64 mailbox_full`.

(2) What is in a mailbox **is not processed by itself.** `drain <actor>` empties that actor's mailbox
      **in the order put in**.

(3) The processor does not decide by itself when to deliver because of **determinism** — the same
      program shall give the same answer, and the moment of delivery is the answer.

> [!주의]
> If messages are only put in a mailbox and it is never emptied, those messages **are not processed.** This is not a forgotten
> failure but what was written — a person chooses the place that empties it.

(4) `schedule .` delivers the pending messages of **all actors** until none remain.
      If `drain` empties one, this empties all.

## 10.6 Exchanging between flows — channels

> [!용어] 채널 (channel)
> A container that passes values between flows. Values come out in the order put in, and the number it can hold is fixed.
> When full or empty, the flow using it stops and continues when the other side comes.

(1) A ⟦채널|channel⟧ is **an ordered container** into which values are put and from which they are taken between flows. It is made with `channel [<type>]`,
      put into with `chsend <channel> <value>`, and taken from with `chrecv <channel>`.

(1a) The type of `channel T` is `chan T`. The value put in with `chsend` shall fit `T` (`E-TYPE-WIDTH` · `E-TYPE-SIGN`), and `chsend` gives `u64` 0.
      `chrecv` gives `T`. Putting into or taking from a value that is not `chan T` is refused (`E-TYPE-KIND`).

(1b) For channel handles too, 0 is the empty handle (⟦§10.3⟧ (2d)). A name declared `chan T` can start with the literal `0`, and other numbers are not
      accepted. Putting into or taking from handle 0 stops the program (`E-VM-HANDLE-ZERO`).

```lowent 예제: the types of handles and channels · 결과: both() = 61
module ex_typed_flow .

fn square input n u64 . output u64 .
  requires le n. 1000 . .
do
  return mul n. n. . .
end .

proc producer input ch chan u64 . output u64 . effects concurrent .
do
  return chsend ch. 25 . .
end .

proc consumer input ch chan u64 . output u64 . effects concurrent .
do
  return chrecv ch. . .
end .

rem The handle is `task u64` and the channel `chan u64` — the types of the value a flow will give and of the values a channel holds are carried in the type.
export proc both output u64 . effects state concurrent .
do
  var r u64 0 .
  var ch chan u64 channel u64 . .
  task_group do
    let h task u64 spawn square 6 . .
    let g task u64 spawn consumer ch. . .
    spawn producer ch. .
    set r. add await h. . await g. . . .
  end .
  return r. .
end .
```

```lowent-거부: receiving a handle as a number · E-TYPE-LET
module ex_handle_as_number .

fn square input n u64 . output u64 .
  requires le n. 1000 . .
do
  return mul n. n. . .
end .

export proc loose output u64 . effects state concurrent .
do
  var r u64 0 .
  task_group do
    let h u64 spawn square 5 . .
    set r. 1 .
  end .
  return r. .
end .
```

(2) A channel **has a fixed size.** Trying to put into a full channel stops the putting flow, and trying to take from an empty
      channel stops the taking flow. A stopped flow **continues** when the other side comes.

(3) So channel operations produce the `concurrent` effect (⟦§7.1⟧) — because their completion **depends on the progress of another
      flow**. This is a different requirement from `wait`, which the kernel wakes up someday, and that
      difference is carried in the signature.

(4) `yield` makes the currently running flow **give way.** Stopping on a channel is built on top of this
      giving way.

(5) If every flow stops on channels and nobody can proceed, that is **a halt**, and the processor
      shall say so. It does not hang silently.

> [!산문]
> Why do the effects differ? An op that `wait`s ends someday even if left alone, but an op that is `concurrent`
> **never ends without a peer.** So if there is only one bound flow and it requires a peer,
> that program cannot end in any order, and the processor refuses it **at translation**
> — it does not wait for it to run and hang.


## 10.7 How far it can be used — `build profile`

(1) A program can write with `build profile <name> .` in which place it runs.
      Once written, the processor refuses **at translation** the concurrency that place cannot bear.

(2) The profiles are the following four and no more; other names are refused (`E-PROFILE-UNKNOWN`).

> [!표] Profiles and the levels they open
> #table(columns: (auto, auto, 1fr), stroke: 0.5pt + rgb("#bbb"), inset: 6pt,
> [*Profile*], [*Level opened*], [*What it means*],
> [`freestanding`], [0], [no operating system. Uses no concurrency],
> [`embedded`], [1], [up to running fixed jobs divided — flows are fixed **statically**],
> [`native`], [2], [makes flows and exchanges through channels],
> [`server`], [3], [up to actors — flows with state live by messages],
> )

(3) If what a program uses is higher than the level the profile opens, it is refused (`E-PROFILE-LEVEL`).
      Merely declaring an `actor` is level 3, and `spawn` and `task_group` are level 1.

(4) **If no profile is written, there is no gating.** Only those who write it take on that promise —
      a program that does not write it has not said where it runs, and the processor does not make up
      what was not said.

> [!산문]
> Why not judge the level by a single word? `send` is used for both actors and channels, and `drain`
> may be the name of an op the user made. Raising the level by looking at words alone **would accuse unrelated
> programs.** So level 3 is judged only by an `actor` **declaration** — if one word has two meanings,
> judgment is not made by that word.

## 10.8 What a machine can bear — `build tier`

(1) If the profile of ⟦§10.7⟧ says *"in which place it runs"*, `build tier <name> .` says
      *"what this machine **can bear**"*. The two are different axes — one separates the constructs used,
      and the other separates **effects**.

(2) There are four tiers, a closed set. Other names are refused (`E-BUILD-TIER`).

> [!표] Tiers and the effects they bear
> #table(columns: (auto, auto, 1fr), stroke: 0.5pt + rgb("#bbb"), inset: 6pt,
> [*Tier*], [*What machine*], [*Effects newly borne*],
> [`t0`], [cooperative floor — very small machines, no operating system], [`none` `unsafe` `panic` `state` `wait` `cancel`],
> [`t1`], [small machines], [in addition to the above, `io` `device`],
> [`t2`], [real-time operating systems], [in addition to the above, `alloc` `lock` `atomic` `blocking` `concurrent`],
> [`t3`], [machines with an operating system], [in addition to the above, `heap` `page_fault` `detach` — that is, **everything**],
> )

(3) Using an effect the declared tier cannot bear is refused **at translation** (`E-TIER-EFFECT`).
      This is not a matter of being slow during execution but of **something that cannot be loaded onto that machine at all**.

(4) Effects rise along the calling chain (⟦§7.1⟧), so the refusal occurs **at the calling place** —
      not at the place where the effect first arose.

(5) **If it is not written, nothing is blocked.** The default for a program that does not write it is `t3`, that is, nothing
      is blocked. The gate is **something chosen**.

> [!산문]
> Why not merge the two axes into one? Profiles separate *constructs* (actors · flows · channels), and
> tiers separate *effects* (the heap · locks · atomics). There are small machines with an operating system, and large machines
> without one. Binding the two into one word would make machines where the two diverge **impossible to describe**.
> Two axes, two words.

## 10.9 Where running side by side goes wrong

(1) Making a flow (`spawn`) can be done **only inside a binding place** (`E-SPAWN-SCOPE`,
      ⟦§10.4⟧). A flow nobody waits for is not made.

(2) If a binding place makes **only one** flow and that flow waits for a partner, it is refused
      (`E-CONC-ALONE`) — waiting without a partner never ends.

(3) If all bound flows only receive and **nobody sends**, it is refused
      (`E-CONC-DEADLOCK`). It is a deadlock known **just by looking at what is written**, not one known only by running it,
      so it is stopped on the spot.

(3a) Sending an actor a message **that actor does not handle** does not conform
      (`E-IR-UNDEF`). The messages handled are looked up in that actor's declaration, and those of another actor
      with the same name **are not silently borrowed.**

(4) Channels without an end (`channel … unbounded`) are not accepted
      (`E-CHAN-UNBOUNDED`). Use a channel with a fixed size.

(5) The shared state types (`lock` · `rwlock` · `shared_read`) have not been built yet
      (`E-LOCK-NOTYET`).

> [!산문]
> (4) is **a decision not to build it** (RFC-0150 Q6 ⓒ, 2026-10-15). A channel without an end has a value whose memory use
> cannot be written up front, which collides with the principle that costs are visible in the text (⟦§1.3⟧). A channel that fills
> stops the sending side, and that stop is visible in the text.
>
> (5) is a refusal **because it does not exist yet**. There is also the way of accepting what does not exist and building it later,
> but then programs written in between experience **thinking it works and then finding it does not**.
> It is more honest to say it does not exist when it does not, and an honest refusal is easy to reverse.

> [!주의]
> (3) does not catch every deadlock — it catches only those known from what is written. That there are deadlocks it cannot
> catch does not mean this rule is weak; it makes clear **what this rule
> promises** (⟦§8.6⟧).
