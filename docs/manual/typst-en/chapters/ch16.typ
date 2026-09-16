#import "../../typst-ko/lib.typ": *

= Capabilities --- power that is handed over

#chapter-toc()

#prereq(
  ([#chref("first-program"), A first program], [printing needs `input out cap io .`]),
  ([#chref("effects"), Effects], [effects are a closed set of atoms that spread along calls]),
)

#deepqa[
  #chref("effects", cap: true) said the effects line writes *what* an op does. What, then, can `effects io` alone not tell you?
][
  *What* the input/output is with. Writing to standard output, opening a file and opening a connection are not told apart by the single word `io`. The
  capabilities of this chapter narrow that down --- an op that received `input fs cap file_system .` can open files but not connections.
]

#why[
  In most languages any function can write to standard output, open files and read the clock. That power is scattered globally, so finding out "does
  this library touch the network?" means reading all its source. Lowent has no such ambient authority. Power is *handed over as an argument, like a
  value*. Effects (#chref("effects")) and capabilities write the same thing from two sides and check each other, and with this chapter the three pillars
  of Part IV (contracts, effects, capabilities) stand.
]

#organizer[
  You will learn the kinds of capability and the work each opens. You will pick up that capabilities are passed down call chains by name, and that
  looking at the entry point alone tells you what outside a program can reach. You will see the table pairing effects with capabilities, and the
  diagnostics for declaring an effect without a capability, handing over the wrong kind, and using a capability without writing it. You will also
  understand which capabilities an entry point may receive, and that capabilities cost nothing at run time.
]

#chapter-questions()

== Kinds of capability

#idx("capability")
A capability is received as `input <name> cap <kind> .`. The kind decides what can be done with it.

#dtable(
  columns: 2,
  id: "caps-kinds",
  caption: [Capability kinds the processor gives meaning to],
  [*Capability*], [*What it opens*],
  [`cap io`], [Standard input and output],
  [`cap file_system`], [Files and directories],
  [`cap net`], [Making connections and exchanging data],
  [`cap tty`], [Terminal size and modes],
  [`cap clock`], [Asking the time],
  [`cap random`], [Operating-system entropy],
  [`cap args` · `cap env`], [Program arguments · environment variables],
  [`cap allocator`], [Obtaining memory from a fixed window (does not grow)],
  [`cap heap`], [Obtaining memory from a growing root (only on machines with an operating system)],
  [`cap atomic`], [Atomic operations],
  [`cap mmio`], [Device registers],
  [`cap c`], [Calling C functions],
  [`cap machine`], [Emitting machine instructions directly],
)

The reason for kinds is *least privilege*. With only one capability, the permission received to read files could also open connections, and the op's
head would no longer say "what this op can do".

== Capabilities travel down the chain

Passing a capability on needs no special notation. Write the received name like any other argument.

#demo("examples/ch16/chain.low")

The chain looks like this --- outside → `main`'s `k` → `say_twice`'s `k` → `say`'s `k` → `write_out`. An op that received no capability cannot call
`say`, because it has nothing to pass. So "can this program touch files?" can be answered *by looking only at the entry point*. No op deep inside can
secretly open a file: without the capability it cannot, and with it, the chain is visible all the way back to the entry.

#qa[
  Passing capabilities as arguments --- doesn't that add an argument per call at run time and slow things down?
][
  It does not. A capability is *a mark at translation time*, not a run-time value. Ops that receive capabilities and ops that do not have the same
  signature at the C boundary. A capability asks "is this op allowed to be called?" at translation time, and nothing is left to ask at run time. The value
  that guards the boundary does not come back as a run-time cost.
]

== Effects and capabilities are a pair

The effects line writes *what* an op does; capability inputs write *who allowed* it. The same thing is written from two sides, and each checks the other.

#dtable(
  columns: 3,
  id: "caps-pairs",
  caption: [Effects and the capabilities that authorise them],
  [*Effect*], [*Authorising capability*], [*When missing*],
  [`io`], [`cap io` · `file_system` · `net` · `tty` · `clock` · `random`], [`E-EFFECT-NO-CAP`],
  [`alloc`], [`cap allocator` (or a region)], [`E-ALLOC-NOCAP`],
  [`heap`], [`cap heap`], [`E-HEAP-NOCAP`],
  [`atomic`], [`cap atomic`], [`E-ATOMIC-NOCAP`],
  [`device`], [`cap mmio`], [`E-MMIO-NOCAP`],
  [`state` · `panic` · `wait` · `concurrent`], [written without a capability], [---],
)

#demo("examples/ch16/pairs.low")

`main` receives two capabilities. `cap io` authorises `effects io`, and `cap allocator` authorises `effects alloc`. `alloc_bytes al capacity 16` asks the
allocator for 16 bytes and gives an `option`, since it may fail.

There are three ways the pair can go wrong. Declaring an effect with no authorising capability is rejected.

#demo("examples/ch16/nocap.low")

Allocation is the same: it declares using memory but not where the memory comes from.

#demo("examples/ch16/alloc_nocap.low")

Receiving a capability and never handing it to the place that uses it is rejected too. A builtin op that requires a capability takes it as its *first
operand*.

#demo("examples/ch16/missing.low")

There is no way to reach a capability without naming it --- in other words, there is no hidden standard output.

== Kind, not presence

Holding some capability does not open every door.

#demo("examples/ch16/kind.low")

`time_now` is the primitive that reads the clock. A `cap io` was handed over, so it is rejected as the wrong kind. Capabilities authorise by *what they
are*.

#misconception[Reading the time with `cap clock` requires declaring the `io` effect][
  The standard library's `clock.now_ms` takes a `cap clock` but is `effects none`. A clock breaks determinism, so it needs a capability, but it leaves no
  trace outside. Capabilities and effects are not always one-to-one. The `io` row of the pair table is a rule in one direction: "to perform the `io`
  effect you must receive one of these".
]

Authors can name capabilities of their own. Receiving your own capability as a parameter, as in `input logger cap audit .`, lets you build ops that only
code holding that capability can call. What the processor gives meaning to are the kinds in the table above, and work of those kinds requires those kinds.

== What the entry point receives

The op a program starts from receives *only capabilities*. It does not receive data, because nobody is there to hand it over.

#demo("examples/ch16/entry.low")

There is no magic argument like C's `argv`; program arguments are received through the `cap args` capability too. The capabilities an entry point may
receive form a closed list --- `args`, `env`, `io`, `allocator`, `heap`, `file_system`, `net`, `tty`, `clock`, `random`, `atomic`. Other capabilities
(`mmio`, `machine`, `c`) are not something the executing environment can hand over, so an entry point requiring them is rejected with `E-ENTRY-CAP`. Such
capabilities must be created where someone is entitled to grant them and flow in as arguments (#chrefs("ffi", "hardware")).

#realcase[Finding out whether a dependency reaches the network][
  What worries you most when bringing in someone else's library is what that code touches. In Lowent you look through the heads of the library's
  `export`ed ops for any that take `cap net` (also look for `cap c`, which escapes into C, and `cap machine`, for machine code --- the processor cannot see
  inside those). If there are none, the library cannot open connections. If there are, you must hand over `cap net` where you call them, so that place is
  the one to audit. A common supply-chain attack --- imported code quietly connecting outward --- becomes visible in the grammar.
]

Environment variables and operating-system randomness are also received as capabilities.

#demo("examples/ch16/envrandom.low")

`env_get e "…"` needs `cap env`, and `random.bytes r buf` needs `cap random`. An environment variable is a channel that quietly changes behaviour
from outside the program, and randomness is a channel that changes the answer for the same input. Both must be visible in the head so that the
head alone tells whether "this program's answer is decided by its inputs". With no variable, `env_get` gives `none` and the example returns 8,
the number of bytes filled. When tests need repeatable random numbers, use `rng_next` (the next number from a seed), which needs no capability.

== Common mistakes

#antipattern[Thinking a `fn` may print because it received a capability][
  #demo("examples/ch16/mistake_fncap.low")

  A capability says "who allowed it"; an effect says "what it does". Neither stands in for the other. Even with `cap io`, printing is
  the `io` effect, and a pure `fn` cannot perform effects, so this is `E-EFFECT-CALC`. Change it to `proc say … effects io .`. Both the
  permission (capability) and the action (effect) must be in the head, so that each checks the other.
]

#antipattern[Moving a received capability into a local name][
  #demo("examples/ch16/mistake_caplocal.low")

  A capability is passed down the chain *under the name it was received with*. Moving it into a local name makes the chain look
  broken, so it is rejected with `E-CAP-LOCAL`; the message names the local and the kind that was copied into it. Write the
  parameter name directly, as in `write_out out 1 …`.
]

#antipattern[Writing to standard output with a capability of another kind][
  #demo("examples/ch16/mistake_fscapout.low")

  `cap file_system` opens files and directories; it is not the capability for standard output. Capabilities are authorised *by kind,
  not by possession*, so this is rejected with `E-CAP-KIND`, which prints the kind required beside the kind handed over. The fix:
  the entry receives `input out cap io .` and passes that name.
]

#antipattern[Passing a number where a capability belongs][
  #demo("examples/ch16/mistake_capforge.low")

  `start` receives no capability at all, so it should not be able to print. Passing the number 0 in the `cap io` position of `say` looks
  like a way around that rule, and it is refused with `E-CAP-FORGE`. Only a *name you were handed* (`input … cap …`) or an actor's
  capability field may stand in a capability position. Without that, the promise of this chapter --- "the entry point tells you everything
  the program can reach" --- would fall to a single line. The placeholder `0` that `--run` uses means something only at the tool's door
  (#chref("build-test")).
]

#misconception[It is convenient to take capabilities and effects in advance, in case they are needed later][
  #demo("examples/ch16/capjustincase.low")

  `area` only multiplies, yet it declares `cap io` and `effects io`. Now every caller must find a `cap io` to pass, and pure code cannot
  call it at all. The tool warns with `W-EFFECT-OVER`. Take as few capabilities as possible --- the list of received capabilities *is*
  "what this op can reach", so a generous list tells a lie.
]

== This chapter's syntax at a glance

#dtable(
  columns: 3,
  id: "capabilities-glance",
  caption: [Capability syntax --- shape · meaning · why it looks this way],
  [*Shape*], [*Meaning*], [*Why*],
  [`input out cap io .`], [receive the standard I/O capability], [no ambient authority --- power is handed in as an argument],
  [`input fs cap file_system .` · `cap net` · `cap clock` …], [each kind opens different things], [least authority --- the head says what it can reach],
  [`say k msg`], [pass a received capability name as is], [the chain is visible up to the entry point],
  [`write_out out 1 "…"`], [builtins that use a capability take it as the first operand], [no way to reach a capability without a name],
  [`effects io` + `cap io`], [an effect and the capability that allows it], [missing: `E-EFFECT-NO-CAP`],
  [`effects alloc` + `cap allocator`], [the pair for fixed-window allocation], [missing: `E-ALLOC-NOCAP`],
  [`proc main input … cap … . output u8 .`], [the entry point receives only capabilities], [nobody can hand it data],
  [`input logger cap audit .`], [a capability named by the author], [only ops that received it can call the guarded op],
  [`env_get e "…"` · `random.bytes r buf`], [environment variable · OS randomness --- `cap env` · `cap random`], [outside channels that change the answer show in the head],
)

#recap[
  Capabilities are handed over as `input <name> cap <kind> .`, and the received name is passed on as is. Looking at the entry point shows what outside a
  program can reach. Effects and capabilities are a pair: declaring an effect requires receiving an authorising capability, and builtin ops that use a
  capability take it as their first operand. Capabilities authorise by kind and cost nothing at run time. The entry point receives only capabilities from
  a closed list.
]
