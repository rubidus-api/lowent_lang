#import "../../typst-ko/lib.typ": *

= Tasks and channels --- exchange between bound flows

#chapter-toc()

#prereq(
  ([#chref("effects"), Effects], [`concurrent` brings `wait` with it]),
  ([#chref("actors"), Actors], [state locked inside, messages handled one at a time]),
  ([#chref("first-program"), A first program], [`test` blocks run under `--test`]),
)

#deepqa[
  How were `concurrent` and `wait` told apart in #chref("effects")?
][
  `wait` is waiting that ends eventually without anyone's help (the kernel or a device wakes it), and `concurrent` is waiting that ends only when *another flow
  of the same program makes progress*. That is why writing `concurrent` counts as writing `wait` too. This chapter covers making those "other flows" and
  exchanging values between them.
]

#why[
  Actors (#chref("actors")) were the shape that locks state inside. But splitting work over several flows and gathering results, or producers and consumers
  handing values over, are awkward to write with actors alone. Flow defects come in three common kinds: flows made and forgotten, flows waiting forever for a
  partner that does not exist, and flows waiting on each other in deadlock. Lowent ties flow lifetimes to blocks to remove the first by grammar, and rejects at
  translation the second and third when they can be seen from what is written. It also provides a tool that tests *every ordering* of several flows.
]

#organizer[
  You will learn to make flows with `spawn <op>` inside a `task_group` and receive results with `await`. You will see the rule that a flow cannot outlive the
  block that made it. You will exchange values with `channel`, `chsend` and `chrecv` and understand why channel operations are the `concurrent` effect. You will
  also see waiting without a partner and deadlocks with no sender rejected at translation, and `schedule explore_interleavings` tests that run every possible
  ordering.
]

#chapter-questions()

== Flows are bound to blocks

#demo("examples/ch26/await.low")

#idx("task_group")
- `task_group do … end` binds the flows made inside it into one. Where the block ends, all bound flows end, and none leave with any still running.
- `spawn square 5` makes a flow running `square 5` and gives that flow's *handle*.
- `await h1` waits until that flow ends and gives its result. While waiting, other flows progress.

A flow *cannot outlive the block that made it*. So making a flow outside a binding place is rejected.

#demo("examples/ch26/scope.low")

A flow made and forgotten survives after the program ends, touches things already gone, or produces failures nobody reads. When a block holds the lifetime,
those three vanish by grammar --- there is no place to forget.

`test join_any_order schedule explore_interleavings` in the same file runs *every ordering* of the two flows and checks that the answers agree. "2 interleavings
agree, exhaustive" in the result line says so.

#qa[
  What does `task_group cancel_on_error` do?
][
  When one bound flow ends with an error, siblings that have not started or are waiting are treated as ended with that error. But cancellation *does not
  unwind*. The effects of flows that already did work are not rolled back; it only stops future work. Which siblings were cancelled depends on the ordering,
  which the specification does not fix. The error itself shows up through `await`.
]

== Channels --- containers with an order

#idx("channel")
A channel is a container for putting and taking values between flows. Values come out in the order put in, and the number it can hold is fixed.

#demo("examples/ch26/chan.low")

- `channel u64` makes a channel. `chsend ch n` puts in and `chrecv ch` takes out.
- A flow taking from an empty channel stops, and so does a flow putting into a full one. A stopped flow continues when its counterpart comes.
- Two consumers and two producers are bound in one group. Whatever the ordering, both 10 and 32 go into `sink` and the result is 42.

"58 interleavings agree, exhaustive" in the test line means *all* 58 orderings in which the four flows stop and continue were run, and every one answered 42.
Concurrency defects usually show only in rare orderings, so tests that run a few times do not catch them. For a small group, running every ordering is the sure
way.

Channel operations are the `concurrent` effect, because their completion *depends on another flow's progress*.

#demo("examples/ch26/pure_chan.low")

A producer written as `effects none` calls `chsend` and is rejected for not declaring `concurrent` and `wait`. With this effect in the head, the fact that the op
cannot be used on a machine without an operating system --- which has no executor to run partner flows --- shows at translation time.

== Deadlocks you can see from what is written

A binding place that makes only one flow, where that flow waits for a partner, is rejected.

#demo("examples/ch26/alone.low")

Waiting without a partner cannot end in any ordering. It is reported at translation rather than waiting for a hang.

If every bound flow only receives and none sends, it is rejected too.

#demo("examples/ch26/deadlock.low")

#misconception[With the deadlock check, deadlocks cannot happen in this language][
  The check catches only deadlocks *visible from what is written*. Two flows waiting crosswise on different channels, or deadlocks that happen only under some
  condition, are not caught by translation. For those, the processor reports `E-VM-DEADLOCK` at run time when every flow stops, and small cases are found by
  `explore_interleavings` tests running every ordering. That some deadlocks are not caught makes clear what this rule promises.
]

== What does not exist yet

Channels without an end (`channel … unbounded`) and lock state shared by several flows (`lock`, `rwlock`) are not accepted yet (`E-CHAN-UNBOUNDED`,
`E-LOCK-NOTYET`). One could accept what does not exist and build it later, but then programs written meanwhile would find that what seemed to work does not. It
is more honest to say it does not exist. Handling the same memory atomically from several flows is covered in #chref("parallel-atomic").

#recap[
  Inside a `task_group`, `spawn <op>` makes a flow and gives a handle, and `await` receives the result. A flow cannot outlive the block that made it, so `spawn`
  outside a binding place is rejected. `channel`, `chsend` and `chrecv` form an ordered container of fixed size, and channel operations are the `concurrent`
  effect. Waiting without a partner and deadlocks with no sender are rejected at translation, and `schedule explore_interleavings` tests run every ordering and
  compare answers.
]
