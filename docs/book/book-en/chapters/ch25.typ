#import "../../book/lib.typ": *

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

#recap[
  An actor locks its state inside and handles messages one at a time. It is made with `spawn actor` and called with `send`, which waits, and message ops follow
  the `fn`/`proc` rules. Messages carry values, and sending an owned value hands over ownership. `spawn send` only puts messages into the mailbox, and `drain`
  and `schedule` empty it. `failure restart` resets only the state of an actor that `panic`ked, and once exhausted passes the failure upwards. `build profile`
  decides where actors may be used.
]
