#import "../../typst-ko/lib.typ": *

= Hardware — registers, interrupts, machine instructions

#chapter-toc()

#prereq(
  ([#chref("named-types"), Named types], [`layout`, `view` and field access markers]),
  ([#chref("fixed-memory"), Allocators and fixed memory], [fixed windows on machines without an operating system]),
  ([#chref("ffi"), Meeting C], [places that cannot be checked are confined by mark, right and effects line]),
)

#deepqa[
  What were the three things an op calling C had to have in #chref("ffi"), and whom did each tell?
][
  The `unsafe` mark (the person), `cap c` (the processor) and the effects line (the caller). This chapter confines device registers and machine instructions the same
  way. Only the capability and effect names change.
]

#why[
  The lowest place of a systems language is firmware reading and writing device registers directly, without an operating system. Defects here are quiet and fatal. A
  compiler deletes a register read that "looks useless", a write-only register is read and moves the device, or an interrupt handler is called like an ordinary
  function and runs on the wrong stack. Lowent stops these at translation. This chapter gathers how the tools set up in earlier chapters --- capabilities, effects,
  layout, fixed windows --- are used on hardware.
]

#organizer[
  You will write a device's register map as a struct with `mmio <address>`, and learn access through `read_volatile` and `write_volatile`. You will see that
  `cap mmio`, the `device` effect and the read-only and write-only markers are enforced at translation, and why a device cannot be received by value. You will also
  make interrupt handlers with the `vector` clause, decide the effects a machine supports with `build tier`, and confine machine instructions with the `asm` clause.
]

#chapter-questions()

== The register map

#demo("examples/ch30/gpio.low")

#idx("mmio")
- `struct gpio do mmio 0x40020000 . … end` is the device's *map*. The number after `mmio` is the start address, and the fields become registers in order. There is no
  new word; a struct just gained a clause.
- `rw`, `ro` and `wo` after a field are its access.
- `read_volatile` and `write_volatile` reach a register. The processor *does not merge, delete or reorder* these accesses. Reading a device register is itself work
  and can change state.
- To reach registers, receive `cap mmio` and write `effects device`. Hardware access without a capability is exactly the ambient-authority problem seen with
  allocators.
- `build tier t1 .` declares that this module is for a small machine (covered below).

On the VM this *really runs*. Instead of a device, a byte buffer handed over by the caller takes the registers' place, and `view gpio regs` lays the map over it. In
the argument shown in the result, `[2,0,0,0,7,0,0,0,1,0,0,0]`, you can see 2 written to `moder`, 1 to `bsrr`, and the 7 in `idr` read back. The first `0` in the run
arguments is a placeholder filling the `cap mmio` position.

#qa[
  C adds the `volatile` qualifier. What is different?
][
  C's `volatile` is a property of a variable and easy to forget. Leave it off and the optimiser may delete reads without warning. In Lowent, reaching a register is a
  named *operation*, `read_volatile` or `write_volatile`, and the head of an op using it carries the `device` effect and `cap mmio`. Whether a read may be deleted is
  decided by the operation's name, not by remembering a qualifier on a variable.
]

== Access is enforced at translation

Writing a read-only register is rejected.

#demo("examples/ch30/ro_write.low")

Reading a write-only register is the same. Reading one yields garbage or the read itself moves the device. That read is not useless but *wrong*. Once you have said
what a device accepts, the type says it back.

Nor can a device be received by value.

#demo("examples/ch30/byvalue.low")

Struct parameters are copied. Copying a device means reading every register at once, write-only ones included, and writes to the copy never reach the device. A
program written that way does nothing and says nothing. So translation stops it. Groups of registers are handled by laying them over a slice with `view`. Trying to
open an absolute address on a machine with an operating system is rejected with `E-MMIO-NOHOST` --- that address is not a device.

== Interrupt handlers

#idx("interrupt handler")
Adding a `vector <number> .` clause to an op makes it an interrupt handler. Urgency is written with `priority <number> .`.

#demo("examples/ch30/isr.low")

This example is checked with `--target cortex_m`. The machine calls interrupt handlers, so they keep four rules.

#dtable(
  columns: 3,
  id: "hw-isr",
  caption: [What an interrupt handler must keep],
  [*Rule*], [*When broken*], [*Why*],
  [Nobody calls it], [`E-ISR-CALLED`], [Called from our code it runs on the wrong stack and priority],
  [No parameters], [`E-ISR-PARAMS`], [The machine passes no arguments],
  [Returns nothing], [`E-ISR-OUTPUT`], [There is nobody to return to],
  [Writes `effects device`], [`E-ISR-EFFECT`], [It exists because of a device],
)

#demo("examples/ch30/isr_called.low")

State shared by interrupt handlers and ordinary code moves by the discipline of priorities and queues. That discipline is a library's job, not the language's. The
`spsc` ring buffer, through which one producer and one consumer pass values without locks, is used there (#chref("lib-containers")).

== What a machine supports --- `build tier`

#idx("build tier")
`build tier <name> .` says what this machine *supports*. Each tier fixes the effects that may be used.

#dtable(
  columns: 3,
  id: "hw-tiers",
  caption: [Tiers and the effects they support],
  [*Tier*], [*Which machine*], [*Newly supported effects*],
  [`t0`], [Very small machine, no operating system], [`none` `unsafe` `panic` `state` `wait` `cancel`],
  [`t1`], [Small machine], [The above plus `io` `device`],
  [`t2`], [Real-time operating system], [The above plus `alloc` `lock` `atomic` `blocking` `concurrent`],
  [`t3`], [Machine with an operating system], [The above plus `heap` `page_fault` `detach` --- that is, everything],
)

#demo("examples/ch30/tier.low")

A `t1` machine was declared unable to support allocation (`alloc`), so it is rejected. This is not a matter of slowing down at run time; it simply cannot be loaded
onto that machine. Effects rise along the call chain, so the rejection also happens at calling sites, not only where the effect first arises. Without a tier it is
`t3`, and nothing is blocked.

Tier (what is supported) and profile (#chref("actors") --- which concurrency arrangement is used) are different axes. There are small machines with operating systems
and large ones without.

#misconception[Giving `--target cortex_m` sets the tier automatically][
  The target (`--target`) decides machine code and `machine.*` constants (whether there is a heap, and so on), which is why the heap is rejected by target alone
  (#chref("regions")). A tier is a *promise* written in the source. On the same target, writing `t0` can block even input/output. Only the one who writes a promise
  carries it, and the processor does not invent promises nobody wrote.
]

== Machine instructions --- `asm`

Some things portable code cannot reach: privileged instructions, system calls, exact cycle counts. There, an op's body is written in machine instructions.

#demo("examples/ch30/asm.low")

- The `asm x86_64 .` clause fixes the machine. Assembly for a machine other than the one being built for is rejected. The processor does not pretend it is portable.
- `reg a .` loads input `a` into a register and `out reg r .` receives the result. `clobber flags .` states what is clobbered, and `options …` states promises made to
  the processor.
- The body is a single `text ASM … ASM` heredoc. Assembly and ordinary code cannot be mixed.
- There are four confinements --- the `unsafe` mark, `cap machine`, `effects unsafe` and the machine name.

The processor cannot read inside the template. But the operand list and the template's `{name}`s are two expressions of the same thing, so each is checked against
the other.

#demo("examples/ch30/asm_unbound.low")

If the template names an undeclared `{c}`, the assembler rejects it or, worse, just reads whatever register was there. Conversely, a declared operand the template
does not use is rejected too (`E-ASM-UNUSED`). `options pure` promises "no side effects", so the processor may trust it and delete or merge calls; if the effects line
says it touches input/output or devices, both cannot be true, so it is rejected (`E-ASM-OPTLIE`). The VM cannot run machine instructions and says so (`E-VM-ASM`). It
runs only natively.

== The absorbing boundary --- where `unsafe` stops

The four guards of the previous section carry a price: `effects unsafe` *travels with every call.* Write one line of assembly and the op that calls it must declare `unsafe`, and so must its caller, all the way up. That is why machine instructions were never used in the standard library (measured 2026-09-23: zero `asm` in `lib/`'s sixty-four modules).

The travelling is right --- if the caller does not know about work the processor cannot see, the effect row is a lie. What was missing was *a place that takes responsibility.*

// snippet: skip — absorption needs the manifest to allow the module (`build absorb …`); a wrapped snippet has no manifest
```lowent
export proc add2 input a u64 . input b u64 . output u64 . effects none .
  absorbs machine k .          rem it stops here; `k` is this body's `cap machine`
  reference add2_soft .        rem a pure version that must give the same answer
  why "it adds two registers and touches no memory (options pure nomem nostack)." .
  requires ge a 0 .
do
  return asm_add2 k a b .
end .
```

- Callers of this op *write nothing.* That is the whole value of absorption.
- `absorbs machine <name>` makes that name a `cap machine` inside the body. This is where that right is born --- `machine` is not in the list an entry point may receive.
- Only `machine` may be absorbed. Capabilities that touch the world (io, C, heap) may not (`E-ABSORB-SCOPE`): minting one would create authority the caller cannot see.

Leave a prerequisite out and it is refused: a non-empty effect row is `E-ABSORB-IMPURE`, a missing reference implementation `E-ABSORB-NOREF`, a missing `requires` `E-ABSORB-NOCONTRACT`, an empty `why` `E-ABSORB-NOWHY`.

*Who may absorb is the manifest's call.* Absorption says a human vouches, so the source that wants the right cannot grant it to itself. Only a module named in `pkg.low` with `build absorb <module> .` may use the clause; otherwise it is `E-ABSORB-PLACE`. With no manifest at all, nobody may absorb.

*The tool names the places.* `lowentc --absorbs f.low` prints, one line each, which op absorbed what and why. A region the tool cannot see is made visible, not hidden.

```
  add2                      absorbs machine as `k`  ref=add2_soft  line 40
      why: it adds two registers and touches no memory…
absorbs: 1 op(s) stop `unsafe` here
```

*Timing is written down, not checked.* Whether the code takes value-dependent time is something the tool cannot verify. So the absorb registry has a timing column a human fills in, and leaving it empty fails the gate --- «unknown» is a valid answer.

#aside[Why demand a reference implementation][
  Without a pure version that computes the same answer there is no way to tell «right» from «consistently wrong». It happened here: a GHASH rewritten with machine instructions passed the seal-then-open test while computing a different product entirely. Sealing and opening share the code, so the round trip holds as long as it is self-consistent. What caught it was comparing against the plain computation.
]

== Using what the machine has --- `--hw`

Whether to use the instructions is the builder's choice.

#table(columns: (auto, 1fr), stroke: 0.5pt + rgb("#bbb"), inset: 6pt,
  [*What you pick*], [*What happens*],
  [`--hw none` (default)], [everything in plain code; stands on any machine],
  [`--hw pclmul,aes,sse2,avx2`], [emitted *assuming* those instructions; will not run where they are missing],
  [`--hw auto`], [carry them all and choose *once at start*; stands anywhere, fast where the instructions exist],
)

What is carried is not only an instruction that replaces a computation. What `sse2` and `avx2` give is *width* --- the room to put four or eight independent pieces of work side by side in one register, which is exactly the shape of ChaCha20's blocks. The rules are the same: the answer does not change, a machine that lacks the set cannot carry it, and the VM always runs the plain code.

- The answer is the same either way. What differs is speed and timing behaviour --- a computation that reads tables reads at a value-dependent place; these instructions do not.
- Asking for an instruction set the target does not have is refused (`E-HW-TARGET`). There is no silent fallback: the builder must know what will run.
- The VM always runs the plain code, so this repository's oracle — «do the VM and native agree?» — is also the test for the machine path.

== Common mistakes

#antipattern[Reading a write-only register to check what was just written][
  #demo("examples/ch30/mistake_woread.low")

  For an ordinary variable, writing and reading back is a good habit, but reading a write-only register yields garbage or makes the device
  do something. Hence `E-MMIO-PERM`. If you need the value you just wrote, keep it in a local before writing, and check the device's real
  state through the read register the datasheet defines (here, `idr`).
]

#antipattern[Giving an interrupt handler parameters][
  #demo("examples/ch30/mistake_isrparams.low")

  The hardware passes no arguments when it calls a handler. Which pin fired is learned inside the handler by reading the device's status
  register. Values shared with ordinary code travel through the priority and queue discipline (the `spsc` ring buffer). Hence
  `E-ISR-PARAMS`.
]

#antipattern[Leaving an interrupt handler's effects line empty][
  #demo("examples/ch30/mistake_isreffect.low")

  Even with nothing to do yet, an interrupt handler exists because of a device. Writing `effects device .` keeps in the head the fact that
  this op is device-side code, and the tier (`build tier`) and capability checks follow that fact. Without it, `E-ISR-EFFECT`.
]

#antipattern[Spelling the `asm` machine name the way another toolchain does][
  #demo("examples/ch30/mistake_asmtarget.low")

  GCC and LLVM say `aarch64`, but this processor's target name is `arm64`. The names are a closed list (`x86_64`·`arm64`·`cortex_m`·
  `riscv64`·`mips_be`), and a name outside it matches no build, so the op would never be built. Instead of skipping it silently, the tool
  stops with `E-ASM-TARGET-UNKNOWN`.
]

#antipattern[Reading and writing registers as ordinary fields][
  #demo("examples/ch30/mistake_plainfield.low")

  `set (field g moder) 2` and `field g idr` are refused (`E-MMIO-PLAIN`). Ordinary field access is an operation the processor may merge or
  remove --- two reads of one register becoming one read, or a store nothing reads going away, change nothing in ordinary memory. On a device
  the access itself is the work, so the answers change. For device registers write `read_volatile` and `write_volatile`, which is also where
  the access modes (`ro`·`wo`) are checked.
]

== This chapter's syntax at a glance

#dtable(
  columns: 3,
  id: "hardware-glance",
  caption: [Hardware syntax --- shape · meaning · why it looks this way],
  [*Shape*], [*Meaning*], [*Why*],
  [`struct gpio do mmio 0x40020000 . moder u32 rw . … end .`], [a device's register map], [one clause on a struct, no new words],
  [`rw` · `ro` · `wo`], [access modes --- enforced at translation], [violations: `E-MMIO-PERM`],
  [`read_volatile g idr` · `write_volatile g moder 2`], [access that is never merged or removed], [reading is itself an action],
  [`input dev cap mmio .` + `effects device`], [device capability and effect], [no hardware access without authority],
  [`var g gpio be view gpio regs .`], [lay the map over bytes], [taking a device by value: `E-MMIO-BYVALUE`],
  [`proc on_exti vector 6 . priority 2 . output void . effects device .`], [an interrupt handler], [calling it: `E-ISR-CALLED` · arguments: `E-ISR-PARAMS`],
  [`build tier t1 .`], [the tier of effects this machine can bear], [what cannot be carried is stopped at translation],
  [`asm x86_64 . reg a . out reg r . clobber flags . options pure .`], [head of a body written in machine code], [confined by `unsafe`, `cap machine`, effects line, machine name],
  [`text ASM … {a} … ASM`], [the template --- checked against the operands], [`E-ASM-UNBOUND` · `E-ASM-UNUSED` · `E-ASM-OPTLIE`],
)

#recap[
  A struct with `mmio <address>` is a device's map, and `read_volatile` and `write_volatile` are accesses never merged or deleted. They need `cap mmio` and the `device`
  effect, the `ro` and `wo` markers are enforced at translation, and devices cannot be received by value. An op with a `vector` clause is an interrupt handler that
  can neither be called nor take arguments. `build tier` fixes the effects a machine supports. The `asm` clause confines machine instructions with `unsafe`,
  `cap machine`, an effects line and a machine name, and checks operands and template against each other.
]
