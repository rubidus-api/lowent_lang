#import "../../typst-ko/lib.typ": *

= Actors --- living with state, by messages

#chapter-toc()

#prereq(
  ([#chref("effects"), Effects], [`state` and `panic` are effects written without capabilities]),
  ([#chref("ownership"), Ownership], [once ownership is handed over, the sender cannot use the value]),
  ([#chref("fixed-memory"), Allocators and fixed memory], [an allocator is an actor satisfying a trait]),
)

#deepqa[
  How was #chref("fixed-memory")'s bump allocator made, and how was it called? Where did its cursor live?
][
  It was made with `spawn actor allocs.bump_bytes` and called with `send a reserve 3`. The cursor lived in the actor's state, and the outside reached that state
  only through messages. This chapter covers actors properly.
]

#why[
  Part VII covers places where several things progress together. Most concurrency defects come from *several flows touching the same state together*. Locks
  can prevent that, but a forgotten lock or a wrong order is silently wrong. Actors approach from the other side. State is kept only inside the actor, and
  messages are handled one at a time. Then there is simply no way to touch that state concurrently. Part VII opens with actors because this shape is already
  used throughout the language, as in the standard library's allocators.
]

#organizer[
  You will learn to declare an actor with `actor … do state do … end … end`, make one with `spawn actor`, and send messages with `send`. You will see that ops
  inside actors follow the `fn`/`proc` rules too, and how to carry values in messages and hand over ownership. You will also see mailboxes filled with
  `spawn send` and emptied later with `drain`, restarting crashed actors with `failure restart`, and `build profile`, which decides where actors may be used.
]

#chapter-questions()

== Declare, make, talk

#demo("examples/ch25/counter.low")

#idx("actor")
- `actor counter do … end` declares an actor. `state do value u64 . end` is its state, which exists only inside the actor and cannot be touched directly
  from outside.
- `inc`, which changes the state, is a `proc` with `effects state`. `get`, which only reads, is a `fn`. Ops inside actors follow the kind rules of
  #chref("ops") as is.
- `var c counter be spawn actor counter .` makes one actor. Its state starts at 0.
- `send c inc` sends the `inc` message to `c`, waits until it is handled, and receives the result.

An actor handles messages *one at a time*. So its state is never touched concurrently. That is why there is no need to take locks by hand.

Writing `fn` for something that changes state is rejected.

#demo("examples/ch25/pure_bad.low")

As the diagnostic says, that write is visible to *the next message*. If a caller remembered results or reordered calls, the answer would be wrong.

== Carrying values in messages

Values are written side by side after the message name. On the receiving side the actor itself is treated like the first parameter, and the carried values
follow.

#demo("examples/ch25/args.low")

`send acct deposit a` puts `a` into `deposit`'s `amount`. Message ops can have contracts too.

Sending a value with ownership hands the ownership over.

#demo("examples/ch25/handoff.low")

Once `j` has gone out with the first message, `give_twice` can no longer use `j`. When two flows hold the same value together, races arise. When ownership
travels only by message, only one party can touch the value at any moment, and a race is not so much prevented as *left no place to happen*.

#qa[
  Isn't calling an actor just a function call in the end?
][
  `send` resembles a call in syntax, but two things differ. State is locked inside the actor, so the caller has no way to read or write its fields, and handling
  one message at a time is guaranteed. Also, holding something borrowed from an actor while talking to that actor again is rejected (`E-BORROW-EXCL`), because
  we would be looking at the state while the actor changes it.
]

== Mailboxes --- putting in and emptying

#idx("mailbox")
`send` waits until handling finishes. Deliveries whose results are not needed are only *put* into the mailbox with `spawn send`.

#demo("examples/ch25/mailbox.low")

`drive` puts `inc` in three times, empties the mailbox in the order put in with `drain c`, and then reads 3. `nodrain` did not empty it, so the result is 0.
Messages in a mailbox *are not handled by themselves*.

The processor does not decide delivery times on its own for the sake of *determinism*. The same program must give the same answer, and the delivery time is
the answer. Because a person picks where to empty, the VM and native code deliver in the same order, and the cross-check of two back ends keeps it so.
`schedule .` empties the mailboxes of every actor at once. Fixing the mailbox size with `mailbox bounded 2 .` makes overflowing puts stop, and
`try spawn send` gives the error as a value instead of stopping.

#misconception[Every actor is its own thread][
  An actor is a *shape* of execution, not a thread. In this edition's processor, `send`, `spawn send` and `drain` are delivered deterministically within one
  flow. What actors give is not parallelism but *isolation of state*. Splitting work over several flows is the job of tasks and channels
  (#chref("tasks-channels")), and computing data split up at the same time is the job of parallel loops (#chref("parallel-atomic")).
]

== Let it crash, then restart

When an actor's op `panic`s, the actor can be restarted. Its state returns to the beginning and the message is handled again.

#demo("examples/ch25/restart.low")

- `recov` sets `bad` to 1 on the first `step` and `panic`s on the second. With `failure restart max 3 .`, the state goes back to the start (`bad = 0`) and the
  message is handled again, giving 42.
- `doomed` always crashes. If it still crashes after two restarts, the failure *goes upwards* and the program stops. No actor is left silently stopped.

There are three policies --- `restart max <count>` (that many times), `never` (pass upwards on the first crash) and `always` (restart without counting).
Restarting applies only to `panic`. A contract violation is the program breaking a promise it wrote itself, and doing it again gives the same result, so it is
not restarted. Only *the actor's own state* is revived; other actors are not touched.

Code that undoes, one by one, failures where state became strange by chance is long, and long code is itself wrong. Resetting the state to the beginning is a
short recovery that is always right.

== Where actors may be used

A program can state where it runs with `build profile <name> .`. When it does, concurrency that place cannot support is rejected at translation.

#dtable(
  columns: 3,
  id: "actors-profiles",
  caption: [Profiles and the levels they open],
  [*Profile*], [*Level*], [*Up to*],
  [`freestanding`], [0], [No operating system. No concurrency],
  [`embedded`], [1], [Running fixed work split up (flows fixed statically)],
  [`native`], [2], [Making flows and exchanging over channels],
  [`server`], [3], [Actors],
)

#demo("examples/ch25/profile.low")

*Declaring* an `actor` alone is level 3. Without a profile, nothing is restricted --- only whoever writes it takes on the promise.

== Actors that hold a capability, and what state may contain

An actor's state may have a capability field. A capability is a mark that exists only at translation, so the field has no size at run time.
In return, a rule fixes what fills it.

#demo("examples/ch25/capfield.low")

- `root cap allocator .` is the capability field. `write` calls `alloc_bytes root capacity 16` through it, so the capability need not be passed
  as an argument in every message.
- `spawn actor logbook` is allowed because `main` received `cap allocator`. The field is filled with *the capability at the spawn site*.
- After two entries, `count` answers 2.

An op that did not receive the capability is refused when it spawns the same actor.

#demo("examples/ch25/mistake_capfield.low")

If this were allowed, one `spawn` line would conjure a capability out of nothing. A capability field only records the fact that "the place
that created this actor already held the capability". The standard allocators `allocs.fixed_bytes` and `heap_bytes` are ordinary actors that
follow this rule (#chref("fixed-memory")).

This is what a state field may and may not hold.

#dtable(
  columns: 3,
  id: "actors-state-fields",
  caption: [Types of actor state fields],
  [*Type*], [*Accepted?*], [*Why*],
  [numbers · `bool` · `option` · structs], [yes], [they are values, so they stay inside the actor],
  [`slice` · `mut slice`], [yes], [this is how an allocator holds its backing bytes (`allocs.bump_bytes`)],
  [`cap allocator` · `cap heap`], [yes --- the spawning op must hold the same capability], [`E-CAP-FORGE` stops forging],
  [`array <count> <type>`], [refused --- `E-TYPE-ARRAY`], [fixed-length arrays are accepted only as op inputs; there is nowhere to keep the length],
  [`ref` · `mut_ref`], [accepted in this edition --- but using it stops the program], [there is nowhere to say what it borrows; see "Common mistakes" below],
)

== Designing with actors --- a transfer between two accounts

This section gathers the pieces so far into one design. Each account is an actor, and a transfer is an op that talks to the two actors in
turn.

#demo("examples/ch25/transfer.low")

- The balance lives only in the state of `account`. The outside reaches an account only through three messages: `deposit`, `withdraw` and
  `peek_balance`.
- When the balance is short, `withdraw` returns `error insufficient` *without touching the state*. It does not stop --- a short balance
  is a failure the account's user can handle (#chref("errors-design")).
- `move` deposits only when the withdrawal succeeded. `move 30` leaves 20 with the sender and 30 with the receiver, giving 20030. With
  `move 80` the withdrawal fails and both accounts stay at 50 and 0 (50000).
- Messages are processed one at a time, so no other message can slip in between `withdraw` checking the balance and reducing it. The race
  between "check, then write" does not exist in this shape.

The `errors insufficient .` of `withdraw` has no condition on purpose. The fourth item of "Common mistakes" below explains why.

== Common mistakes

#antipattern[Putting messages into a bounded mailbox without draining it][
  #demo("examples/ch25/mistake_bounded.low")

  `mailbox bounded 2 .` promises "at most two pending messages". The third `spawn send` overflows and stops with `E-VM-MAILBOX-FULL`. To
  handle it instead of stopping, send with `try spawn send`. The overflow then comes back as a `result`, and you can drain and send again.

  #demo("examples/ch25/bounded_fixed.low")
]

#antipattern[Sending a message the actor does not handle][
  #demo("examples/ch25/mistake_unknownmsg.low")

  A message name is looked up *in the receiving actor's type*. `counter` has no `dec`, so this is `E-IR-UNDEF`. As the diagnostic's note
  tells, lookup was once by bare name, and two actors with a handler of the same name silently shared one.
]

#antipattern[Reading an actor's state field from outside][
  #demo("examples/ch25/mistake_peekstate.low")

  The first promise of this chapter was "the state lives only inside the actor and cannot be touched directly from outside". `field c value`
  tries to go around that door and is refused with `E-ACTOR-FIELD`. If state were readable from outside, handling one message at a time would
  buy nothing --- the reader would see a value between two messages. If you need the state, give the actor a read message such as `get` and
  ask with `send c get`.
]

#antipattern[Using a state field in the error condition of a message op][
  #demo("examples/ch25/mistake_errorsstate.low")

  `errors insufficient gt amount balance .` was written to mean "this error when withdrawing more than the balance". But an `errors`
  condition is read on the values the op was *entered* with (canon 6.4.2). `errors` stands on the same side as `requires` --- it says what
  the *caller* got wrong, and what the caller did is hand things over. So a name the body can change (a state field, a module `var`, a `mut`
  parameter) cannot stand in the condition, and translation refuses it with `E-ERRORS-STATE`. Read on exit, a successful run would accuse
  itself: the balance dropped from 50 to 20, so on exit `gt 30 20` is true, which amounts to "the condition holds, yet the error was not
  returned". Write the error condition over the *inputs*, and let the `guard` in the body judge the state --- as `transfer.low` above does
  with a bare `errors insufficient .`.
]

#antipattern[Keeping a borrow in a state field][
  #demo("examples/ch25/mistake_reffield.low")

  A borrow (`ref`) cannot outlive what it borrows (#chref("references")). An actor's state stays for as long as the actor lives, so the field
  has nowhere to say what it borrows. So the declaration is refused with `E-ACTOR-STATE-REF`. Until 2026-09-16 the declaration was accepted
  and the actor spawned with the field empty; only at `deref r` did the VM stop with `E-VM-TYPE` and native code with a `panic`. Keep values
  in state instead of borrows, and if a value is large, keep a slice the actor owns for its lifetime.
]

#misconception[A restarted actor continues from the state just before it blew up][
  #demo("examples/ch25/restart_resets.low")

  After two `inc`s the value is 2, and the third message blows up. A restart puts the state back to *the beginning* (0) and processes that
  message again, so the answer is 1, not 3. The state just before the failure may be the very reason for the failure, so it is not trusted.
  Keep values that must not be lost outside the actor --- in another actor or a file --- and let the restarted actor read them.
]

#misconception[Values of the same actor type share their state][
  #demo("examples/ch25/separate_state.low")

  `spawn actor counter` twice makes two states. Incrementing `left` twice leaves `right` counting from 1. An actor type is a blueprint; a new
  state is made on every `spawn`. If many parties must see the same value, keep *one* actor that holds it and have everyone talk to that
  actor.
]

== This chapter's syntax at a glance

#dtable(
  columns: 3,
  id: "actors-glance",
  caption: [Actor syntax --- shape · meaning · why it looks this way],
  [*Shape*], [*Meaning*], [*Why*],
  [`actor counter do state do value u64 . end . … end .`], [declare a unit of execution that encloses state], [there is no way to touch the state concurrently],
  [`proc inc … effects state .` · `fn get …`], [a message that changes state · one that only reads], [`fn`/`proc` rules unchanged --- a `fn` that writes is `E-EFFECT-PURITY`],
  [`var c counter be spawn actor counter .`], [create one actor (state starts at 0)], [each `spawn` has its own state],
  [`send c inc` · `send acct deposit a`], [send and wait until processed · carry a value], [actor first --- a message is an op taking the actor as first parameter],
  [`spawn send c inc .` · `drain c .` · `schedule .`], [put in the mailbox · drain that actor's mailbox · drain all], [a person picks the delivery point --- determinism],
  [`mailbox bounded 2 .` · `try spawn send`], [mailbox size · receive overflow as a value], [overflow stops, or becomes a `result`],
  [`failure restart max 3 .` · `never` · `always`], [restart a panicked actor from its initial state], [when used up, the failure goes upward],
  [`build profile server .`], [where actors (level 3) may be used], [only those who write it take on the promise],
  [`state do root cap allocator . … end .`], [a capability field --- size 0 at run time], [`E-CAP-FORGE` if the spawning op lacks that capability],
)

#recap[
  An actor locks its state inside and handles messages one at a time. It is made with `spawn actor` and called with `send`, which waits, and message ops follow
  the `fn`/`proc` rules. Messages carry values, and sending an owned value hands over ownership. `spawn send` only puts messages into the mailbox, and `drain`
  and `schedule` empty it. `failure restart` resets only the state of an actor that `panic`ked, and once exhausted passes the failure upwards. `build profile`
  decides where actors may be used.
]
