#import "../lib.typ": *

#clause("10", "Concurrency")[
  #para("1")[
    This clause specifies the places where several things #strong[proceed together];. Concurrency is not a special feature
      in this language but one more place where the effect rules of #cref("7") apply unchanged.
  ]
  #sub("10.1", "Concurrency is an effect too")[
    #para("1")[
      Making another flow of execution, or waiting for its progress, is an effect. `wait` and `concurrent` are
      those (#cref("7.1")), and they shall be written in the contract.
    ]
    #para("2")[
      What separates `wait` from `concurrent` is #strong[who wakes it up];.
    ]
    #tbl("The two kinds of waiting")[
      #table(columns: (auto, 1fr), stroke: 0.5pt + rgb("#bbb"), inset: 6pt,
      [*Effect*], [*Meaning*],
      [`wait`], [it can be suspended. What wakes it #strong[is not another task of the same program]; (the kernel · a device wakes it)],
      [`concurrent`], [it ends only #strong[when a peer runs];. There has to be an executor to run that peer],
      )
    ]
    #plain[
      For example, waiting for data to come from a file is `wait` — what wakes it is not another task of the same
      program but the outside.
    ]
    #note[
      Why this distinction matters: a small device without an operating system may have no «executor that runs peers».
      Then a program that uses only `wait` runs as it is, but a program that uses `concurrent`
      does not. It is written in the effects, so it can be known #strong[at translation time];.
    ]
  ]
  #sub("10.2", "Actors")[
    #para("1")[
      An #t("액터", "actor") is a unit of execution with its own state. The state exists only inside the actor and
      cannot be touched directly from outside.
    ]
    #para("1a")[
      So #strong[reading the state fields]; of an actor value #strong[from outside]; is refused (`E-ACTOR-FIELD`).
      The only way to reach the state is messages — put an op that returns what is needed in the actor.
    ]
    #para("1b")[
      A state field cannot be #strong[a borrow]; (`ref` · `mut_ref`) (`E-ACTOR-STATE-REF`). A borrow cannot outlive what it was
      borrowed from (#cref("8.4.1")), but a state field remains for as long as the actor lives, and there is no place to write
      what was borrowed.
    ]
    #para("1c")[
      The state fields of an actor just made with `spawn actor` are #strong[all 0];. 0 is not a slice, so
      #strong[reading before setting up]; a field that holds a slice or a borrow does not conform
      (`E-ACTOR-UNINIT`). The processor refuses the case where #strong[the first message sent]; to that actor reads such a field
      without setting it up — it looks within the straight order of statements.
    ]
    #plain[
      Until 2026-09-16, before (1c), that place was #strong[where the two back ends parted];: the VM stopped with «not a
      slice», and native silently gave `none` (defect note #53). One of the two was a lie, but
      the canon did not say which. Instead of choosing an answer, it makes that place #strong[unreachable];.
    ]
    #para("1d")[
      #strong[A name holding an actor can start with the literal `0`]; — an actor not yet spawned (the same as handle 0 of #cref("10.3") (2d)). Other numbers
      do not go into an actor place (`E-TYPE-LET` · `E-TYPE-VAR`). Sending a message with `send` · `spawn send` · `try spawn send` to an actor that is 0, or
      `drain`ing it, stops the program (`E-VM-HANDLE-ZERO`). Use it after it holds `spawn actor <name>`.
    ]
    #rejected("filling an actor place with a number other than 0", "module ex_actor_number .

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
end .", "E-TYPE-VAR")
    #para("2")[
      Ops inside actors also follow the kinds of #cref("6.4.1") — if it changes state it is `proc` and
      shall write `effects state`; if it only reads, it is `fn`.
    ]
    #para("3")[
      Declaring it pure while changing state is refused at translation.
    ]
    #para("4")[
      If an actor's op `panic`s (#cref("6.5.9")), that actor #strong[can be set up again.]; Then that
      actor's state returns to the start and that message is processed again. What comes back to life is
      #strong[only its own state];; other actors are not touched.
    ]
    #para("4a")[
      Setting an actor up again #strong[does not depend on how the message came]; — sent with `send`, waiting, or put in the
      mailbox with `spawn send` and delivered by `drain` · `schedule` (#cref("10.5")), it is the same. The messages #strong[still in the
      mailbox]; for that actor #strong[are not touched]; — the actor set up again processes them in the order they were put in.
    ]
    #plain[
      The remaining messages are not thrown away because `spawn send` has already returned saying it #strong[took]; them. Throwing them
      away silently afterwards would hide a failure. It also agrees with processing even the message that blew up again (4) — there
      is no reason to redo the message that went wrong and throw away the innocent ones behind it. A message that keeps blowing up is
      ended by the policy of (5). If a message sent against the old state has no meaning in the state returned to the start, that
      actor writes `failure restart never`.
    ]
    #para("5")[
      Until when it is set up again is decided by the clause `failure restart <policy> .`. The policies are the following three and no more.
    ]
    #tbl("Actor failure policies — a closed set of three")[
      #table(columns: (auto, 1fr), stroke: 0.5pt + rgb("#bbb"), inset: 6pt,
      [*Policy*], [*Meaning*],
      [`max <number> [within <number> <unit>]`], [sets it up again that many times. When used up, passes it upward],
      [`never`], [does not set it up again — passes it upward the first time it blows up],
      [`always`], [keeps setting it up again without counting],
      )
    ]
    #para("5b")[
      The units of `within` are a closed three, and both singular and plural are accepted —
      `second`/`seconds` · `minute`/`minutes` · `hour`/`hours`. Any other word does not
      conform.
    ]
    #para("5c")[
      If the processor cannot count the period of `within`, it #strong[says so]; (`W-NOT-YET`). It does not silently read a clause that writes a period
      as `max <number>`.
    ]
    #note[
      Both singular and plural are accepted for reading's sake — `within 1 second` and
      `within 5 seconds` are written as people write. This is not #strong[a second way of saying the same
      thing]; (#cref("7.1.1")): the meaning is one and only the spelling differs with the number, and the processor
      reads both as the same number.
    ]
    #para("5a")[
      An actor has #strong[one]; failure policy (`E-ACTOR-FAILURE`). So does its mailbox policy
      (`E-ACTOR-MAILBOX`, #cref("10.5")). With two policies #strong[nobody could say]; which stands,
      and the answer would become the processor's internal affair.
    ]
    #para("6")[
      If it blows up after using them all, that failure #strong[is passed upward.]; No actor is left silently
      stopped.
    ]
    #para("7")[
      Setting up again applies only to `panic`. A broken contract (#cref("6.4")) is not set up again
      — that is the program breaking a promise it wrote itself, so doing it again gives the same.
    ]
    #plain[
      Why «let it blow up»? There are failures for which setting up again is better — cases where the state somehow became strange.
      Code that rolls back such failures one by one by hand is long, and when it is long that code itself
      goes wrong. Returning the state to the start is #strong[short recovery that is always right];.
    ]
    #ex("an actor", "module ex_actor .

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
end .")
    #rejected("declared pure while changing state", "module ex_actor_bad .

actor counter do
  state do
    value u64 .
  end .

  fn inc output u64 .
  do
    set value. expr value. + 1 . . rem it changes state but is an `fn`
    return value. .
  end .
end .", "E-EFFECT-PURITY")
  ]
  #sub("10.3", "Making flows of execution")[
    #para("1")[
      `spawn` makes a new flow of execution. Making one is an effect and shall be written in the contract.
    ]
    #para("2")[
      Whether it #strong[waits for]; the flow it made #strong[to end]; or detaches it is separated by effects —
      detaching is `detach`.
    ]
    #para("2a")[
      `spawn <op> [arguments…]` gives a #strong[handle]; that points at that flow. The type of the handle is `task T`, where `T` is that op's
      output type. If it is used only as a statement without receiving the handle, that flow is detached.
    ]
    #para("2b")[
      `await <handle>` waits until that flow ends and gives its result. The type of the result is the handle's `T`. Waiting for a value that is not
      a `task T` is refused (`E-TYPE-KIND`). While waiting, other flows proceed — waiting does not stop the machine.
    ]
    #para("2c")[
      Waiting for something that cannot end stops the program. The processor shall say where.
    ]
    #para("2d")[
      #strong[Handle 0 is a handle that points at no flow.]; A name declared `task T` can start with the literal `0` — the spelling for leaving the place
      empty before receiving a flow. No other number can be a handle (`E-TYPE-LET` · `E-TYPE-VAR` — numbers do not point at flows).
      Waiting for handle 0 stops the program (`E-VM-HANDLE-ZERO`). The processor builds the handles `spawn` gives as values other than 0,
      and filters out 0 at every place that uses a handle — an empty handle #strong[never silently points at another flow.];
    ]
    #rejected("making a handle from a number other than 0", "module ex_handle_number .

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
end .", "E-TYPE-VAR")
    #note[
      Handle 0 is in the same position as the null pointer of other languages. Being able to leave it empty is convenient, but using what was left empty
      shall #strong[stop on the spot]; rather than grab something unrelated. This language counts handles from 1 so that 0 overlaps no flow,
      and checks for 0 at every place it is used.
    ]
    #note[
      Why waiting is an effect. `await` computes no value but #strong[spends time]; — the caller has to put
      that cost into its budget. So a pure op cannot wait (#cref("7.3")).
    ]
    #para("3")[
      `spawn actor` makes an actor, and `send` sends a message to that actor. An actor processes messages
      #strong[one at a time];.
    ]
    #para("3a")[
      Values can be carried with a message — written #strong[side by side]; after the message name, as in `send <actor> <message> <value>…`.
      On the receiving side the actor instance is treated #strong[like the first parameter];,
      and the values written follow it.
    ]
    #para("3b")[
      Sending a value with ownership #strong[hands over]; that ownership. The sender can no longer use that value,
      and using it is refused at translation (#cref("8.5")).
    ]
    #plain[
      Why hand it over? If two flows hold the same value together, races arise. If ownership moves only
      through messages, at any moment only one flow can touch that value, and then races
      #strong[cannot happen]; — not prevented, but with no place to exist.
    ]
    #ex("making an actor and sending a message", "module ex_spawn .

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
end .")
    #plain[
      It matters that an actor processes messages one at a time — the state inside #strong[is not touched
      at the same time];. That is why locks need not be taken by hand.
    ]
    #caution("Concurrency is not a world outside the checks")[
      In many languages concurrency is the area of #emph["be careful from here on"];. In Lowent
      #strong[the same effect rules apply unchanged]; — what it waits for, whether it changes state, whether an executor
      is needed, are all written in the contract and checked by the processor.
    ]
  ]
  #sub("10.4", "The place that binds flows — `task_group`")[
    #para("1")[
      `task_group do <statements> end .` #strong[binds into one]; the flows made inside it. At the place where the block
      ends, the bound flows have all ended, and it does not leave with even one remaining.
    ]
    #para("2")[
      So a flow #strong[cannot outlive the block that made it.]; This is how this language handles the lifetime of
      flows, and detaching (#cref("10.3") (2)) is the only exception to that discipline.
    ]
    #diagram("the lifetime of flows is confined inside the task_group block", "op        ──┬── task_group do ─────────────────── end ──▶ continues
            │
h1          │   spawn ├── work ──┤ end
h2          │       spawn ├── work ──────────┤ end
            │
            └─ opened at task_group and closed at end --- flows live only in between
   await h1 · await h2 wait for each flow to end. No flow remains when passing end")
    #para("3")[
      Inside a group, `spawn <op> [arguments…]` #strong[puts a job in the queue.]; It is the same word, but
      `spawn actor <name>` makes one actor, and the two are told apart by what follows.
    ]
    #para("4")[
      `task_group cancel_on_error do … end .` #strong[propagates cancellation.]; If one bound flow ends with an error,
      the #strong[siblings]; not yet started or waiting are treated as having ended with that error.
    ]
    #para("5")[
      Cancellation #strong[does not roll back.]; The effects of flows that already did work do not go back, and cancellation only
      keeps work still to come from being done. So #emph["which siblings were cancelled"]; depends on order,
      and this document does not fix that order.
    ]
    #para("6")[
      The error itself shows through `await` (#cref("10.3") (2b)) — cancellation does not swallow errors.
    ]
    #plain[
      Why bind them? A flow made and forgotten remains after the program ends, or touches what is already gone,
      or produces a failure nobody reads. When the block holds that lifetime, those three disappear #strong[by grammar];
      — there is no place to forget.
    ]
  ]
  #sub("10.5", "Delivering messages later")[
    #para("1")[
      `send` (#cref("10.3") (3)) waits until the actor processes the message. `spawn send <actor>
      <message> [arguments…]` does not wait but #strong[puts it in the mailbox and returns at once.];
    ]
    #para("1a")[
      `try spawn send …` does not stop when the mailbox is full but gives that as a value — its type is
      `result u64 mailbox_full`.
    ]
    #para("2")[
      What is in a mailbox #strong[is not processed by itself.]; `drain <actor>` empties that actor's mailbox
      #strong[in the order put in];.
    ]
    #para("3")[
      The processor does not decide by itself when to deliver because of #strong[determinism]; — the same
      program shall give the same answer, and the moment of delivery is the answer.
    ]
    #caution("")[
      If messages are only put in a mailbox and it is never emptied, those messages #strong[are not processed.]; This is not a forgotten
      failure but what was written — a person chooses the place that empties it.
    ]
    #para("4")[
      `schedule .` delivers the pending messages of #strong[all actors]; until none remain.
      If `drain` empties one, this empties all.
    ]
    #para("4a")[
      `schedule` too handles #strong[the messages sent to one actor in the order put in, one at a time]; — the same as `drain`.
      Even if the handler gives up its place with `yield` (#cref("10.6") (4)) or stops on a channel, that actor's next message
      starts #strong[after]; the earlier one has finished.
    ]
    #para("4b")[
      The order between messages of different actors, and between messages and tasks, is not specified by this document. Whether
      an answer leans on that order is checked by `schedule explore_interleavings` (#cref("6.4.13") (4)), which runs every open order.
    ]
    #plain[
      The order inside one actor is fixed because an actor #strong[holds its own state alone]; (#cref("10.2") (1)). If two messages to the same
      actor went on together, the two would take turns changing the same state, and a race would arise inside the actor — the reason
      for using an actor would be gone. The order between actors is left open because fixing it gains nothing and loses something:
      once fixed, a program that leans on that order does not show up in tests, and when the work is divided over several cores every
      actor would have to wait for the others.
    ]
    #ex("inside one actor, in the order put in", "module ex_schedule_order .

actor log do
  state do h u64 . end .
  proc put input x u64 . output u64 . effects state . do
    set h. expr h. * 10 + x. . .
    return h. .
  end .
  fn get output u64 . do return h. . end .
end .

export proc run output u64 . effects state .
do
  var c log spawn actor log . .
  spawn send c. put 1 . .
  spawn send c. put 2 . .
  spawn send c. put 3 . .
  spawn send c. put 4 . .
  spawn send c. put 5 . .
  schedule .
  return send c. get . .
end .",
      out: "run() = 12345")
  ]
  #sub("10.6", "Exchanging between flows — channels")[
    #term("채널", "channel")[
      A container that passes values between flows. Values come out in the order put in, and the number it can hold is fixed.
      When full or empty, the flow using it stops and continues when the other side comes.
    ]
    #para("1")[
      A #t("채널", "channel") is #strong[an ordered container]; into which values are put and from which they are taken between flows. It is made with `channel [<type>]`,
      put into with `chsend <channel> <value>`, and taken from with `chrecv <channel>`.
    ]
    #para("1a")[
      The type of `channel T` is `chan T`. The value put in with `chsend` shall fit `T` (`E-TYPE-WIDTH` · `E-TYPE-SIGN`), and `chsend` gives `u64` 0.
      `chrecv` gives `T`. Putting into or taking from a value that is not `chan T` is refused (`E-TYPE-KIND`).
    ]
    #para("1b")[
      For channel handles too, 0 is the empty handle (#cref("10.3") (2d)). A name declared `chan T` can start with the literal `0`, and other numbers are not
      accepted. Putting into or taking from handle 0 stops the program (`E-VM-HANDLE-ZERO`).
    ]
    #ex("the types of handles and channels", "module ex_typed_flow .

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
end .",
      out: "both() = 61")
    #rejected("receiving a handle as a number", "module ex_handle_as_number .

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
end .", "E-TYPE-LET")
    #para("2")[
      A channel #strong[has a fixed size.]; Trying to put into a full channel stops the putting flow, and trying to take from an empty
      channel stops the taking flow. A stopped flow #strong[continues]; when the other side comes.
    ]
    #para("3")[
      So channel operations produce the `concurrent` effect (#cref("7.1")) — because their completion #strong[depends on the progress of another
      flow];. This is a different requirement from `wait`, which the kernel wakes up someday, and that
      difference is carried in the signature.
    ]
    #para("4")[
      `yield` makes the currently running flow #strong[give way.]; Stopping on a channel is built on top of this
      giving way.
    ]
    #para("5")[
      If every flow stops on channels and nobody can proceed, that is #strong[a halt];, and the processor
      shall say so. It does not hang silently.
    ]
    #plain[
      Why do the effects differ? An op that `wait`s ends someday even if left alone, but an op that is `concurrent`
      #strong[never ends without a peer.]; So if there is only one bound flow and it requires a peer,
      that program cannot end in any order, and the processor refuses it #strong[at translation];
      — it does not wait for it to run and hang.
    ]
  ]
  #sub("10.7", "How far it can be used — `build profile`")[
    #para("1")[
      A program can write with `build profile <name> .` in which place it runs.
      Once written, the processor refuses #strong[at translation]; the concurrency that place cannot bear.
    ]
    #para("2")[
      The profiles are the following four and no more; other names are refused (`E-PROFILE-UNKNOWN`).
    ]
    #tbl("Profiles and the levels they open")[
      #table(columns: (auto, auto, 1fr), stroke: 0.5pt + rgb("#bbb"), inset: 6pt,
      [*Profile*], [*Level opened*], [*What it means*],
      [`freestanding`], [0], [no operating system. Uses no concurrency],
      [`embedded`], [1], [up to running fixed jobs divided — flows are fixed #strong[statically];],
      [`native`], [2], [makes flows and exchanges through channels],
      [`server`], [3], [up to actors — flows with state live by messages],
      )
    ]
    #para("3")[
      If what a program uses is higher than the level the profile opens, it is refused (`E-PROFILE-LEVEL`).
      Merely declaring an `actor` is level 3, and `spawn` and `task_group` are level 1.
    ]
    #para("4")[
      #strong[If no profile is written, there is no gating.]; Only those who write it take on that promise —
      a program that does not write it has not said where it runs, and the processor does not make up
      what was not said.
    ]
    #plain[
      Why not judge the level by a single word? `send` is used for both actors and channels, and `drain`
      may be the name of an op the user made. Raising the level by looking at words alone #strong[would accuse unrelated
      programs.]; So level 3 is judged only by an `actor` #strong[declaration]; — if one word has two meanings,
      judgment is not made by that word.
    ]
  ]
  #sub("10.8", "What a machine can bear — `build tier`")[
    #para("1")[
      If the profile of #cref("10.7") says #emph["in which place it runs"];, `build tier <name> .` says
      #emph["what this machine #strong[can bear];"];. The two are different axes — one separates the constructs used,
      and the other separates #strong[effects];.
    ]
    #para("2")[
      There are four tiers, a closed set. Other names are refused (`E-BUILD-TIER`).
    ]
    #tbl("Tiers and the effects they bear")[
      #table(columns: (auto, auto, 1fr), stroke: 0.5pt + rgb("#bbb"), inset: 6pt,
      [*Tier*], [*What machine*], [*Effects newly borne*],
      [`t0`], [cooperative floor — very small machines, no operating system], [`none` `unsafe` `panic` `state` `wait` `cancel`],
      [`t1`], [small machines], [in addition to the above, `io` `device`],
      [`t2`], [real-time operating systems], [in addition to the above, `alloc` `lock` `atomic` `blocking` `concurrent`],
      [`t3`], [machines with an operating system], [in addition to the above, `heap` `page_fault` `detach` — that is, #strong[everything];],
      )
    ]
    #para("3")[
      Using an effect the declared tier cannot bear is refused #strong[at translation]; (`E-TIER-EFFECT`).
      This is not a matter of being slow during execution but of #strong[something that cannot be loaded onto that machine at all];.
    ]
    #para("4")[
      Effects rise along the calling chain (#cref("7.1")), so the refusal occurs #strong[at the calling place]; —
      not at the place where the effect first arose.
    ]
    #para("5")[
      #strong[If it is not written, nothing is blocked.]; The default for a program that does not write it is `t3`, that is, nothing
      is blocked. The gate is #strong[something chosen];.
    ]
    #plain[
      Why not merge the two axes into one? Profiles separate #emph[constructs]; (actors · flows · channels), and
      tiers separate #emph[effects]; (the heap · locks · atomics). There are small machines with an operating system, and large machines
      without one. Binding the two into one word would make machines where the two diverge #strong[impossible to describe];.
      Two axes, two words.
    ]
  ]
  #sub("10.9", "Where running side by side goes wrong")[
    #para("1")[
      Making a flow (`spawn`) can be done #strong[only inside a binding place]; (`E-SPAWN-SCOPE`,
      #cref("10.4")). A flow nobody waits for is not made.
    ]
    #para("2")[
      If a binding place makes #strong[only one]; flow and that flow waits for a partner, it is refused
      (`E-CONC-ALONE`) — waiting without a partner never ends.
    ]
    #para("3")[
      If all bound flows only receive and #strong[nobody sends];, it is refused
      (`E-CONC-DEADLOCK`). It is a deadlock known #strong[just by looking at what is written];, not one known only by running it,
      so it is stopped on the spot.
    ]
    #para("3a")[
      Sending an actor a message #strong[that actor does not handle]; does not conform
      (`E-IR-UNDEF`). The messages handled are looked up in that actor's declaration, and those of another actor
      with the same name #strong[are not silently borrowed.];
    ]
    #para("4")[
      Channels without an end (`channel … unbounded`) are not accepted
      (`E-CHAN-UNBOUNDED`). Use a channel with a fixed size.
    ]
    #para("5")[
      The shared state types (`lock` · `rwlock` · `shared_read`) have not been built yet
      (`E-LOCK-NOTYET`).
    ]
    #plain[
      (4) is #strong[a decision not to build it]; (RFC-0150 Q6 ⓒ, 2026-10-15). A channel without an end has a value whose memory use
      cannot be written up front, which collides with the principle that costs are visible in the text (#cref("1.3")). A channel that fills
      stops the sending side, and that stop is visible in the text.
    ]
    #caution("")[
      (3) does not catch every deadlock — it catches only those known from what is written. That there are deadlocks it cannot
      catch does not mean this rule is weak; it makes clear #strong[what this rule
      promises]; (#cref("8.6")).
    ]
  ]
]
