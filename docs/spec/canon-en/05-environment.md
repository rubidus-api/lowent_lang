# 5 Environment

(1) This clause specifies **the place where Lowent programs are translated and executed**. The grammar of the language is
      specified by ⟦§6⟧; here the ground on which that grammar stands is specified.

## 5.1 The translation environment and the execution environment

(1) The ⟦번역 환경|translation environment⟧ is the place where a Lowent source is turned into a form that can be
      executed. The ⟦실행 환경|execution environment⟧ is the place where the result actually runs.
      The two may be the same machine or different machines.

> [!용어] 번역 단위 (translation unit)
> One source file translated together. In Lowent a translation unit is exactly one module, and
> a module writes its own name on its first line.

> [!용어] 프로그램 (program)
> The set of translation units bound together into one unit of execution. A program shall have exactly one
> entry point.

(2) Execution environments are of two kinds.

> [!용어] 호스티드 (hosted)
> An environment with an operating system. Files, clocks, standard input and output and the like can be received and used as capabilities.

> [!용어] 프리스탠딩 (freestanding)
> An environment without an operating system. The language itself holds unchanged; only the capabilities the host gives
> (⟦§7.2⟧) cannot be received.

(3) ⟦§6⟧ to ⟦§8⟧ of this document hold **in both environments**.
      What differs between environments is only which capabilities can be received.

> [!산문]
> Put simply — a computation written in Lowent has **the same grammar and meaning** even on a small device without an
> operating system. What differs is only the part that touches the outside world, such as «can it read a file».

## 5.2 Representation of source files

(1) A Lowent source file shall be a Unicode string encoded in UTF-8. Other encodings
      do not conform, and the processor shall issue a diagnostic.

(2) The end of a line is represented by the line feed character (U+000A). The processor may accept a carriage return (U+000D)
      in front of it, and then treats the two characters as one line end.

(3) A byte order mark (⟦BOM|byte order mark⟧, U+FEFF) cannot appear anywhere in a source file.
      Even at the very start it is a **lexical error**, and the processor shall issue a diagnostic.

(4) Only ASCII characters can be used in an ⟦이름|identifier⟧ (⟦§6.1.3⟧). Unicode characters can be used freely in
      the **contents** of comments, strings and text literals.

(5) The encoding in which the source is read (the bytes the processor reads) and the encoding of the values literals have (the bytes
      the program handles) are **separate**. The source is always UTF-8, and a literal may have as its value bytes that are
      not UTF-8.

> [!주의] Identifiers being ASCII is a decision, not a restriction
> Allowing Unicode identifiers creates **names that look the same but are different** (the Greek letter Α and the Latin A,
> two Hangul spellings in different normal forms, and so on). A person reading the source then reaches a wrong conclusion,
> and that is the uncertainty this language sets out to remove (⟦§1.3⟧). So identifiers are fixed to ASCII, and
> text meant for people is written in Unicode in comments and strings.

> [!산문]
> A function cannot be named in Hangul, but comments and strings can be written in Hangul as much as you like.
> The examples in this repository are in fact written that way.

## 5.3 Minimum requirements of the execution environment

(1) A conforming execution environment shall satisfy the following.

(2) a) A byte (⟦옥텟|octet⟧) shall be exactly 8 bits.

(3) b) Integers shall be represented in two's complement (⟦투스 컴플리먼트|two's complement⟧).

(4) c) The address width shall be 16 bits or more.

(4a) **A pointer is not the same thing as an address.** A pointer value may carry bounds or permissions in addition to an address, so its size
      **may be larger** than the address width. The size and alignment of a pointer value, and whether it is a ⟦능력|capability⟧ (a value that
      cannot be made from an integer and is derived only from another valid pointer), are determined by the execution environment (implementation-defined).
      A program asks for these values at translation time with `machine.addr_bits` · `machine.ptr_bytes` · `machine.ptr_align` ·
      `machine.ptr_caps` (`machine.ptr_width` is the same as `machine.addr_bits`).

> [!용어] 능력 (capability)
> A pointer value that carries, together with an address, **the range it may reach and what it may do** (read · write · execute). The hardware
> separately remembers whether the value is valid, so it cannot be made or rewritten from an integer and is obtained only by **narrowing**
> another valid capability. Reaching outside the range stops execution.

(4b) Values of this language **do not expose pointers as integers.** What points is a slice · a reference · a capability, and no integer type
      can hold a pointer and bring it back to life. So even on a machine whose pointers are capabilities the meaning of a program does not change.

> [!참고]
> Machines whose pointers are larger than their addresses really exist — capability-pointer machines (64-bit addresses · 128-bit pointers, or
> 32-bit addresses · 64-bit pointers) carry bounds and permissions in the pointer and make pointers that went through an integer unusable.
> There are also machines whose pointer size differs per address space (microcontrollers with separate program and data memory,
> the spaces of accelerators). (4a) · (4b) are written so that, on the day such a machine is met, the language's promises do not break first.

(4c) **The multiplier width** is the widest width (8 · 16 · 32 · 64 bits) at which the product of two integers, up to its upper half, is produced
      by one instruction. It is determined by the execution environment (implementation-defined), and a program asks for it at translation time
      with `machine.mul_bits`. A translator may provide a means of changing that value for one translation. It may differ from the
      address width. The processor also fixes the **limb width** used for big-number arithmetic and answers it with `machine.limb_bits`
      (16 · 32 · 64 bits, implementation-defined) — the value measured and fixed on the machine if there is one, otherwise the multiplier width
      (16 bits if the multiplier width is narrower than 16 bits). A translator may provide a means of changing the limb width for one
      translation. The multiplier width is a fact of the machine and does not change.

> [!참고]
> Big-number arithmetic splits numbers into limbs and multiplies limb by limb. Which limb width is fast is decided not by the address width
> but by the multiplier — some machines with 32-bit addresses cannot produce the upper half of a 32×32-bit product (Cortex-M0). But the
> measured result can differ from the multiplier width (on a 32-bit x86 build, 64-bit limbs were faster), so the chosen width is kept
> separately. The standard library's big-number module `bigint` chooses its limb width with `machine.limb_bits` and gives the same
> answer at every width (RFC-0152).

(5) d) For a program that uses floating-point numbers, the execution environment shall provide the IEEE 754 binary 32-bit or
      64-bit format. A program that does not use floating-point numbers is unaffected by this requirement.

> [!참고]
> Almost every machine in use today already satisfies a)–c). This document **writes them down**
> because otherwise «where does this language run» would become knowledge outside the source.
> Some signal-processing devices whose byte is not 8 bits are not targets of this language.

## 5.4 Diagnostics

(1) The processor shall issue a ⟦진단|diagnostic⟧ for a program that does not conform.

(2) A diagnostic shall have a **stable identifier**. Identifiers consist of capital letters and hyphens
      (for example `E-TYPE-INSTANCE`), and the meaning of an identifier, once fixed, does not change.

(3) A diagnostic shall give the **place** (file · line) where the error occurred.

(4) Diagnostic identifiers fall into the following kinds.

> [!표] Kinds of diagnostic identifier
> #table(columns: (auto, auto, 1fr), stroke: 0.5pt + rgb("#bbb"), inset: 6pt,
> [*Prefix*], [*Meaning*], [*Examples*],
> [`E-`], [Error. Translation does not succeed], [`E-TYPE-INSTANCE`, `E-EFFECT`],
> [`W-`], [Warning. Translation succeeds, but the author should look], [`W-EXPORT-NOSYM`],
> [`E-VM-`], [Trap during execution], [`E-VM-CONTRACT`, `E-VM-BOUNDS`],
> )

(5) A trap during execution stops the program at once and reports the fact to the execution environment. Computation does not continue
      after a trap.

> [!주의] The text of a diagnostic message is not a contract
> What is a contract is the **identifier**; the sentence people read is a convenience. Someone building tools should look at the
> identifier, not pick the sentence apart with regular expressions — the sentence may be polished.

## 5.5 Program start and end

(1) **A program that is executed** shall have exactly one ⟦시작점|entry point⟧.
      The entry point is the op named `main`.

(1a) A translation unit without an entry point also conforms — it is a ⟦라이브러리 모듈|library module⟧
      other programs take and use. An entry point is needed when an executable result is
      made.

(2) The value the entry point returns is the program's ⟦종료 상태|exit status⟧. 0 means success,
      and the meaning of other values is determined by the execution environment (implementation-defined).

(2a) What can be used as the output of the entry point: a small unsigned integer (`u8` · `u32` and the like) ·
      `void` (success when it ends) · `result` (0 on success; on error, the status that error determines).

(2b) If the value is wider than the execution environment's exit status, how it is narrowed is implementation-defined.

(3) In a hosted environment the entry point may receive as arguments the capabilities the execution environment gives. Which capabilities
      it receives is written by the entry point in its own contract, and the execution environment hands over only what is written (⟦§7.2⟧).

(4) When the entry point returns, the program ends. Resources open before the end shall already have been closed according to the ownership rules
      (⟦§8.5⟧); if a resource is left unclosed, the program is
      refused at translation.

```lowent 예제: the entry point receives capabilities
module ex_env .

use stdio .

rem A capability not received exists nowhere in this program.
proc main input out cap io . output u8 . effects io .
do
  let n usize stdio.write out. 1 "hello\n" . .
  return cast u8 n. . .
end .
```

```lowent-거부: the entry point returns `u8` · E-ENTRY-OUTPUT
module ex_entry .

proc main output u64 . effects none .
do
  return 0 .
end .
```

> [!산문]
> The last paragraph is where this language differs most from others. It is often said that «when a program ends, the operating
> system cleans up», but Lowent **refuses at translation the very fact of having forgotten to clean up**.
> So the same code holds even in an environment without an operating system.

## 5.6 Machine names

(1) The processor knows which ⟦기계|target⟧ it builds for. Machines differ in word size, byte order,
      the presence of floating-point numbers and so on, and where those differences reach the meaning of a program this document
      says so.

> [!용어] 기계 (target)
> The place a program will run. Its word size, byte order, presence of floating-point numbers and so on are fixed,
> and the processor knows them before building. It may be the same as or different from the place where translation happens
> (the translation environment of ⟦§5.1⟧).

(2) Machine names form a **closed set**, and the following is all of it.

> [!표] Machine names
> #table(columns: (auto, auto, auto, 1fr), stroke: 0.5pt + rgb("#bbb"), inset: 6pt,
> [*Name*], [*Word (bits)*], [*Byte order*], [*Remarks*],
> [`x86_64`], [64], [little-endian], [floating-point numbers · vectors],
> [`arm64`], [64], [little-endian], [floating-point numbers · vectors],
> [`riscv64`], [64], [little-endian], [floating-point numbers · vectors are optional, so not in the base],
> [`mips_be`], [32], [**big-endian**], [floating-point numbers],
> [`cortex_m`], [32], [little-endian], [**no heap · no floating-point numbers** — a machine that runs alone],
> [`cortex_m0`], [32], [little-endian], [the same as `cortex_m` except that the multiplier width is 16 bits (⟦§5.3⟧ (4c)) — Cortex-M0 · M0+],
> [`win64`], [64], [little-endian], [crossing only (⟦§5.6⟧ (4))],
> [`cheriot`], [32], [little-endian], [**pointers are capabilities** (8 bytes, ⟦§5.3⟧ (4a)) · no heap · no floating-point numbers · crossing only (⟦§5.6⟧ (6))],
> [`atmega328p`], [16], [little-endian], [AVR (Arduino Uno · Nano) · alignment 1 byte · multiplier width 8 bits (⟦§5.3⟧ (4c)) · no heap · no floating-point numbers · emitted and run (⟦§5.6⟧ (8))],
> [`atmega2560`], [16], [little-endian], [AVR (Arduino Mega 2560) · the same values as `atmega328p` · emitted and run (⟦§5.6⟧ (8))],
> [`atmega32u4`], [16], [little-endian], [AVR (Arduino Leonardo · Micro) · the same values as `atmega328p` · up to building (⟦§5.6⟧ (8))],
> [`atmega4809`], [16], [little-endian], [AVR (Arduino Nano Every) · the same values as `atmega328p` · up to building (⟦§5.6⟧ (8))],
> [`word16`], [16], [little-endian], [no heap · no floating-point numbers · **stands up to translation only** (⟦§5.6⟧ (7))],
> )

(2a) Machine names form a **tree**. A node inherits its parent's values and states only the values it changes — `cortex_m0` changes only the
      multiplier width from `cortex_m`, and `atmega328p` · `atmega2560` · `atmega32u4` · `atmega4809` inherit the values of the group `avr` unchanged. **Groups** that stand only at
      branches of the tree (`lowent` · `riscv32` · `avr`) are not machine names and so are refused under (3). The processor shows the expanded values of each node.

(3) A name not in this list matches no build and so is refused. If a name that does not match were
      accepted, the op would **silently drop out or be silently built wrong** — both are what this document
      sets out to prevent.

(4) **What a machine does not have cannot be used.** The processor refuses it at translation.

> [!표] What machines lack — refused on the spot
> #table(columns: (auto, auto, 1fr), stroke: 0.5pt + rgb("#bbb"), inset: 6pt,
> [*What is missing*], [*Which machines*], [*If used*],
> [a growing root (heap)], [`cortex_m` · `cheriot` · `atmega328p` · `atmega2560` · `atmega32u4` · `atmega4809` · `word16`], [the `heap` effect · `cap heap` · `region <name> heap` are refused (`E-HEAP-NOHOST`). `alloc` carving from a fixed window can be used (⟦§8.1⟧)],
> [64-bit floating-point numbers (`f64`)], [`cortex_m` · `cheriot` · `atmega328p` · `atmega2560` · `atmega32u4` · `atmega4809` · `word16`], [`f64` and floating literals without a type word are refused (`E-FLOAT-NOFLOAT`)],
> )

(4-1) `f32` **exists on every target.** On a target without a floating-point unit it is computed in software, and the result is identical, to the bit, to that of a target with one.
      `f64` exists only on targets with the unit — it cannot be used on the targets of the table above.

(4-2) The processor accepts software `f32` only on targets **where it has been measured**. The processor of this edition accepts it on the four AVR chips (`atmega328p` · `atmega2560` · `atmega32u4` ·
      `atmega4809`) and still refuses it on `cortex_m` · `cheriot` · `word16` (`E-FLOAT-NOTYET`). That refusal is not a rule of the language but
      the processor saying it has not yet been built.

> [!산문]
> Why `f32` and `f64` are separated: 32-bit floating point can be computed exactly in software even on small machines, and there is much work
> that needs it there, such as sensor values and control coefficients. 64-bit is several times heavier to compute, and C compilers for small machines sometimes make `double` 32 bits —
> refusing is better than accepting a value that cannot be produced honestly.

(5) Whether to use what a machine **has** is decided by the person building. A build chooses **which instruction sets the binary
      contains**, and if it contains two or more, the program chooses **once, when it starts**, and fixes the choice. It does not
      change during execution — if what ran differed from run to run, a measured number could not be reproduced.

> [!표] The range of instruction sets — the build chooses
> #table(columns: (auto, 1fr), stroke: 0.5pt + rgb("#bbb"), inset: 6pt,
> [*Chosen*], [*What happens*],
> [contain nothing], [every computation is done with plain code. Stands on every machine],
> [contain one or more sets], [code is emitted **assuming those instructions exist**. It does not run on a machine without them],
> [contain them but «choose»], [both are contained, and one is detected and fixed once at start. Stands on every machine, and is fast on machines that have them],
> )

(5a) Whichever is chosen, **the answer is the same**. Machine instructions and plain code are two implementations of the same meaning; what differs is
      only speed and **timing properties** — a computation that reads a table reads at a place that depends on the value, and those instructions do
      not. Where constant time is promised, the difference is written down (⟦§9.2⟧).

(5c) What can be contained is not only **instructions that replace a computation** — **width** (placing several independent jobs side by side
      in one register) is in the same range too. The rule is the same: the answer is the same (⟦§5.6⟧ (5a)), it cannot be contained
      for a machine without it (⟦§5.6⟧ (5b)), and the choice is made **once, at start**.

(5b) If the machine lacks the instruction, it cannot be contained. Asking for it to be contained makes translation refuse — it is not silently
      replaced with plain code. The person building shall know what will run.

(4a) That is, «code that runs on embedded systems too» is **something measured, not something hoped for** — a program that uses a growing root or
      floating-point numbers stops translating the moment that machine is chosen.

> [!산문]
> Why stop it at **translation** rather than at execution? Sending an allocation to a machine without a heap makes it blow up
> late, on that machine, at the place hardest to fix. It is better to say **in advance** that what is missing is
> missing — that is the opposite of what this language calls «hidden cost» in §1.3.

(5d) `win64` has meaning **only as far as crossing**. C source for that machine can be emitted and built, and that is
      as far as this document speaks; it says nothing about running on it.
      An op that uses something only POSIX has is refused at translation — it is better to **cry first at the place where the reason is
      known** than to blow up later because a header is missing.

(6) `cheriot` too has meaning **only as far as crossing** — emitting C source for that machine and building it. This machine's
      pointers are capabilities (⟦§5.3⟧ (4a)), and `machine.ptr_caps` is true. The pointer of a register block is derived from the
      device-memory capability the execution environment gives (⟦§7.6.1⟧ (3)).

(7) `word16` has meaning **only as far as translation**. It is not the name of a real machine but the place for measuring whether the same text stands
      in an implementation where the width of `usize` · `idiff` is 16 bits (⟦§6.2⟧ (3-1)). The processor translates and checks text for this target but
      neither emits C source nor runs the text — because whether what is emitted at that width is correct has never been measured. The upper bound of a slice length is 2¹⁵.

(8) `atmega328p` and `atmega2560` have meaning as far as emitting C source and running it. The C source emitted for that machine is built by
      that chip's C compiler, and the answers from running it on a machine that emulates the chip have been measured to equal other executions of the same text.
      `atmega32u4` and `atmega4809` have meaning **as far as building** — there is no emulating machine, so the answers from running could not be measured.
      C `int` is 16 bits, so the processor emits 32-bit words as fixed-width C types. Atomic operations are emitted as read-and-write while interrupts are
      blocked — these chips have one core. The upper bound of a slice length is 2¹⁵.


## 5.7 Where a unit states what it is — `package`

(1) A translation unit may state **what it is** with `package <key> <value> .`.
      This has **the same shape** as a build declaration (`build tier t0 .` and the like) — two idioms
      would be a second expression too, so no new construct is made.

(2) The keys form a closed set of six. Any other name is refused (`E-PKG-KEY`).

> [!표] The keys of `package`
> #table(columns: (auto, auto, 1fr), stroke: 0.5pt + rgb("#bbb"), inset: 6pt,
> [*Key*], [*Required*], [*What is written*],
> [`name`], [yes], [the name of this unit],
> [`version`], [yes], [the version number. It shall be in a form **that can be ordered**],
> [`description`], [no], [a one-line description],
> [`date`], [no], [a date],
> [`authors`], [no], [the authors],
> [`license`], [no], [the conditions of use],
> )

(3) If even one key is written, `name` and `version` **shall** be present
      (`E-PKG-NONAME` · `E-PKG-NOVERSION`). An identity place without an identity is decoration.

(4) Writing the same key twice is refused (`E-PKG-DUP`). An identity has **one** value —
      with two, each tool that reads this place picks one, and not all pick the same.

(5) `version` shall be in **a form that can be ordered**, or it is refused
      (`E-PKG-VERSION`). A version number nobody can interpret cannot **order** editions,
      and ordering is the only reason a version number exists.

(6) A shape other than `package <key> <value> .` is refused (`E-PKG-FORM`).

> [!산문]
> This place exists so that tools and people can learn the identity of this unit cheaply **without reading the whole source**.
> But if what is read cannot be trusted, the value is as good as absent — so
> here alone **what is written is checked.** A key nobody reads is a key nobody checks,
> and an unchecked duplicate rots into a lie (⟦§1.1⟧).

## 5.8 Building differently per build — `build option` and `config`

(1) A program may declare **knobs**. `build option <name> <kind> …` makes a knob,
      and its value is fixed where the program is built.

(2) There are three kinds; anything else is refused (`E-OPT-TYPE`).

> [!표] Kinds of knob
> #table(columns: (auto, 1fr), stroke: 0.5pt + rgb("#bbb"), inset: 6pt,
> [*Kind*], [*What it holds*],
> [`bool`], [on and off],
> [`int`], [an integer],
> [`choice`], [one of the listed values],
> )

(3) A knob may **depend** on another knob (`depends`). If the depending knob is a `bool` and is **on**
      while the knob it depends on is off, the build is refused (`E-CONFIG-DEPENDS`) — because **such a build
      does not exist**; without this check `depends` would be decoration.

(3a) If the depending knob is a `choice` or an `int`, there is no place called «on» — that knob always has a
      value. A mismatched dependency is then **not a nonexistent build but an unused knob**,
      and if the configuration nevertheless picks a value, it is reported (`W-CONFIG-DEPENDS`).

> [!산문]
> Before (3a), «on» was measured only as a non-zero value, so a `choice` knob that could not pick 0
> became **always on**. So **no configuration at all could exist** that switched off a knob it depended
> on — a knob that no configuration can switch off is not a knob (defect note #18).

(4) The value of a knob is read with `config <name>`, and it is a **translation-time constant** (⟦§6.8⟧).
      Reading a name that does not exist is refused (`E-CONFIG-UNDEF`).

(4a) If the knob depended on **does not exist at all**, it is refused (`E-OPT-DEPENDS`).

(4b) If a configuration gives a knob **a value its kind cannot accept**, it is refused (`E-CONFIG-TYPE`).
      So is writing an unknown property (`E-CFG-UNKNOWN-PROP`).

(5) A knob that is declared but **read by no code** is refused (`E-OPT-UNUSED`). Such a knob
      appears in the configuration, a user switches it off, and nothing happens —
      **something that looks like a decision but is not one**.

(6) Once the values are fixed, **switched-off branches do not remain in the output.** The cost is 0.

> [!주의] Switched-off branches **are still checked**
> In other languages' conditional compilation, code for combinations that are not switched on rots unread by anyone.
> That cannot happen here — what is folded away is **code emission**, not parsing and type checking.
> So «it does not build in that combination» does not happen.
