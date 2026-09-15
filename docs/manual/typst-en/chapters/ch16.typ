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

#recap[
  Capabilities are handed over as `input <name> cap <kind> .`, and the received name is passed on as is. Looking at the entry point shows what outside a
  program can reach. Effects and capabilities are a pair: declaring an effect requires receiving an authorising capability, and builtin ops that use a
  capability take it as their first operand. Capabilities authorise by kind and cost nothing at run time. The entry point receives only capabilities from
  a closed list.
]
