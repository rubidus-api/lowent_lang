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

  `set (field g moder) 2` and `field g idr` also pass in this edition, and on the VM they give the same answers as `read_volatile`. The access
  modes (`ro`·`wo`) are enforced here too. But ordinary field access is an operation the processor may merge or remove. If code that reads the
  same register twice is reduced to one read, a real device behaves differently. Whether the canon allows this spelling is being checked in
  the development repository. For device registers, always use `read_volatile` and `write_volatile`.
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
