#import "../lib.typ": *

#clause("5", "Environment")[
  #para("1")[
    This clause specifies #strong[the place where Lowent programs are translated and executed];. The grammar of the language is
      specified by #cref("6"); here the ground on which that grammar stands is specified.
  ]
  #sub("5.1", "The translation environment and the execution environment")[
    #para("1")[
      The #t("번역 환경", "translation environment") is the place where a Lowent source is turned into a form that can be
      executed. The #t("실행 환경", "execution environment") is the place where the result actually runs.
      The two may be the same machine or different machines.
    ]
    #term("번역 단위", "translation unit")[
      One source file translated together. In Lowent a translation unit is exactly one module, and
      a module writes its own name on its first line.
    ]
    #term("프로그램", "program")[
      The set of translation units bound together into one unit of execution. A program shall have exactly one
      entry point.
    ]
    #para("2")[
      Execution environments are of two kinds.
    ]
    #term("호스티드", "hosted")[
      An environment with an operating system. Files, clocks, standard input and output and the like can be received and used as capabilities.
    ]
    #term("프리스탠딩", "freestanding")[
      An environment without an operating system. The language itself holds unchanged; only the capabilities the host gives
      (#cref("7.2")) cannot be received.
    ]
    #para("3")[
      #cref("6") to #cref("8") of this document hold #strong[in both environments];.
      What differs between environments is only which capabilities can be received.
    ]
    #plain[
      Put simply — a computation written in Lowent has #strong[the same grammar and meaning]; even on a small device without an
      operating system. What differs is only the part that touches the outside world, such as «can it read a file».
    ]
  ]
  #sub("5.2", "Representation of source files")[
    #para("1")[
      A Lowent source file shall be a Unicode string encoded in UTF-8. Other encodings
      do not conform, and the processor shall issue a diagnostic.
    ]
    #para("2")[
      The end of a line is represented by the line feed character (U+000A). The processor may accept a carriage return (U+000D)
      in front of it, and then treats the two characters as one line end.
    ]
    #para("3")[
      A byte order mark (#t("BOM", "byte order mark"), U+FEFF) cannot appear anywhere in a source file.
      Even at the very start it is a #strong[lexical error];, and the processor shall issue a diagnostic.
    ]
    #para("4")[
      Only ASCII characters can be used in an #t("이름", "identifier") (#cref("6.1.3")). Unicode characters can be used freely in
      the #strong[contents]; of comments, strings and text literals.
    ]
    #para("5")[
      The encoding in which the source is read (the bytes the processor reads) and the encoding of the values literals have (the bytes
      the program handles) are #strong[separate];. The source is always UTF-8, and a literal may have as its value bytes that are
      not UTF-8.
    ]
    #caution("Identifiers being ASCII is a decision, not a restriction")[
      Allowing Unicode identifiers creates #strong[names that look the same but are different]; (the Greek letter Α and the Latin A,
      two Hangul spellings in different normal forms, and so on). A person reading the source then reaches a wrong conclusion,
      and that is the uncertainty this language sets out to remove (#cref("1.3")). So identifiers are fixed to ASCII, and
      text meant for people is written in Unicode in comments and strings.
    ]
    #plain[
      A function cannot be named in Hangul, but comments and strings can be written in Hangul as much as you like.
      The examples in this repository are in fact written that way.
    ]
  ]
  #sub("5.3", "Minimum requirements of the execution environment")[
    #para("1")[
      A conforming execution environment shall satisfy the following.
    ]
    #para("2")[
      a) A byte (#t("옥텟", "octet")) shall be exactly 8 bits.
    ]
    #para("3")[
      b) Integers shall be represented in two's complement (#t("투스 컴플리먼트", "two's complement")).
    ]
    #para("4")[
      c) The address width shall be 16 bits or more.
    ]
    #para("4a")[
      #strong[A pointer is not the same thing as an address.]; A pointer value may carry bounds or permissions in addition to an address, so its size
      #strong[may be larger]; than the address width. The size and alignment of a pointer value, and whether it is a #t("능력", "capability") (a value that
      cannot be made from an integer and is derived only from another valid pointer), are determined by the execution environment (implementation-defined).
      A program asks for these values at translation time with `machine.addr_bits` · `machine.ptr_bytes` · `machine.ptr_align` ·
      `machine.ptr_caps` (`machine.ptr_width` is the same as `machine.addr_bits`).
    ]
    #term("능력", "capability")[
      A pointer value that carries, together with an address, #strong[the range it may reach and what it may do]; (read · write · execute). The hardware
      separately remembers whether the value is valid, so it cannot be made or rewritten from an integer and is obtained only by #strong[narrowing];
      another valid capability. Reaching outside the range stops execution.
    ]
    #para("4b")[
      Values of this language #strong[do not expose pointers as integers.]; What points is a slice · a reference · a capability, and no integer type
      can hold a pointer and bring it back to life. So even on a machine whose pointers are capabilities the meaning of a program does not change.
    ]
    #note[
      Machines whose pointers are larger than their addresses really exist — capability-pointer machines (64-bit addresses · 128-bit pointers, or
      32-bit addresses · 64-bit pointers) carry bounds and permissions in the pointer and make pointers that went through an integer unusable.
      There are also machines whose pointer size differs per address space (microcontrollers with separate program and data memory,
      the spaces of accelerators). (4a) · (4b) are written so that, on the day such a machine is met, the language's promises do not break first.
    ]
    #para("4c")[
      #strong[The multiplier width]; is the widest width (8 · 16 · 32 · 64 bits) at which the product of two integers, up to its upper half, is produced
      by one instruction. It is determined by the execution environment (implementation-defined), and a program asks for it at translation time
      with `machine.mul_bits`. A translator may provide a means of changing that value for one translation. It may differ from the
      address width. The processor also fixes the #strong[limb width]; used for big-number arithmetic and answers it with `machine.limb_bits`
      (16 · 32 · 64 bits, implementation-defined) — the value measured and fixed on the machine if there is one, otherwise the multiplier width
      (16 bits if the multiplier width is narrower than 16 bits). A translator may provide a means of changing the limb width for one
      translation. The multiplier width is a fact of the machine and does not change.
    ]
    #note[
      Big-number arithmetic splits numbers into limbs and multiplies limb by limb. Which limb width is fast is decided not by the address width
      but by the multiplier — some machines with 32-bit addresses cannot produce the upper half of a 32×32-bit product (Cortex-M0). But the
      measured result can differ from the multiplier width (on a 32-bit x86 build, 64-bit limbs were faster), so the chosen width is kept
      separately. The standard library's big-number module `bigint` chooses its limb width with `machine.limb_bits` and gives the same
      answer at every width (RFC-0152).
    ]
    #para("5")[
      d) For a program that uses floating-point numbers, the execution environment shall provide the IEEE 754 binary 32-bit or
      64-bit format. A program that does not use floating-point numbers is unaffected by this requirement.
    ]
    #note[
      Almost every machine in use today already satisfies a)–c). This document #strong[writes them down];
      because otherwise «where does this language run» would become knowledge outside the source.
      Some signal-processing devices whose byte is not 8 bits are not targets of this language.
    ]
  ]
  #sub("5.4", "Diagnostics")[
    #para("1")[
      The processor shall issue a #t("진단", "diagnostic") for a program that does not conform.
    ]
    #para("2")[
      A diagnostic shall have a #strong[stable identifier];. Identifiers consist of capital letters and hyphens
      (for example `E-TYPE-INSTANCE`), and the meaning of an identifier, once fixed, does not change.
    ]
    #para("3")[
      A diagnostic shall give the #strong[place]; (file · line) where the error occurred.
    ]
    #para("4")[
      Diagnostic identifiers fall into the following kinds.
    ]
    #tbl("Kinds of diagnostic identifier")[
      #table(columns: (auto, auto, 1fr), stroke: 0.5pt + rgb("#bbb"), inset: 6pt,
      [*Prefix*], [*Meaning*], [*Examples*],
      [`E-`], [Error. Translation does not succeed], [`E-TYPE-INSTANCE`, `E-EFFECT`],
      [`W-`], [Warning. Translation succeeds, but the author should look], [`W-EXPORT-NOSYM`],
      [`E-VM-`], [Trap during execution], [`E-VM-CONTRACT`, `E-VM-BOUNDS`],
      )
    ]
    #para("5")[
      A trap during execution stops the program at once and reports the fact to the execution environment. Computation does not continue
      after a trap.
    ]
    #caution("The text of a diagnostic message is not a contract")[
      What is a contract is the #strong[identifier];; the sentence people read is a convenience. Someone building tools should look at the
      identifier, not pick the sentence apart with regular expressions — the sentence may be polished.
    ]
  ]
  #sub("5.5", "Program start and end")[
    #para("1")[
      #strong[A program that is executed]; shall have exactly one #t("시작점", "entry point").
      The entry point is the op named `main`.
    ]
    #para("1a")[
      A translation unit without an entry point also conforms — it is a #t("라이브러리 모듈", "library module")
      other programs take and use. An entry point is needed when an executable result is
      made.
    ]
    #para("2")[
      The value the entry point returns is the program's #t("종료 상태", "exit status"). 0 means success,
      and the meaning of other values is determined by the execution environment (implementation-defined).
    ]
    #para("2a")[
      What can be used as the output of the entry point: a small unsigned integer (`u8` · `u32` and the like) ·
      `void` (success when it ends) · `result` (0 on success; on error, the status that error determines).
    ]
    #para("2b")[
      If the value is wider than the execution environment's exit status, how it is narrowed is implementation-defined.
    ]
    #para("3")[
      In a hosted environment the entry point may receive as arguments the capabilities the execution environment gives. Which capabilities
      it receives is written by the entry point in its own contract, and the execution environment hands over only what is written (#cref("7.2")).
    ]
    #para("4")[
      When the entry point returns, the program ends. Resources open before the end shall already have been closed according to the ownership rules
      (#cref("8.5")); if a resource is left unclosed, the program is
      refused at translation.
    ]
    #ex("the entry point receives capabilities", "module ex_env .

use stdio .

rem A capability not received exists nowhere in this program.
proc main input out cap io . output u8 . effects io .
do
  let n usize stdio.write out. 1 \"hello\\n\" . .
  return cast u8 n. . .
end .")
    #rejected("the entry point returns `u8`", "module ex_entry .

proc main output u64 . effects none .
do
  return 0 .
end .", "E-ENTRY-OUTPUT")
    #plain[
      The last paragraph is where this language differs most from others. It is often said that «when a program ends, the operating
      system cleans up», but Lowent #strong[refuses at translation the very fact of having forgotten to clean up];.
      So the same code holds even in an environment without an operating system.
    ]
  ]
  #sub("5.6", "Machine names")[
    #para("1")[
      The processor knows which #t("기계", "target") it builds for. Machines differ in word size, byte order,
      the presence of floating-point numbers and so on, and where those differences reach the meaning of a program this document
      says so.
    ]
    #term("기계", "target")[
      The place a program will run. Its word size, byte order, presence of floating-point numbers and so on are fixed,
      and the processor knows them before building. It may be the same as or different from the place where translation happens
      (the translation environment of #cref("5.1")).
    ]
    #para("2")[
      Machine names form a #strong[closed set];, and the following is all of it.
    ]
    #tbl("Machine names")[
      #table(columns: (auto, auto, auto, 1fr), stroke: 0.5pt + rgb("#bbb"), inset: 6pt,
      [*Name*], [*Word (bits)*], [*Byte order*], [*Remarks*],
      [`x86_64`], [64], [little-endian], [floating-point numbers · vectors],
      [`arm64`], [64], [little-endian], [floating-point numbers · vectors],
      [`riscv64`], [64], [little-endian], [floating-point numbers · vectors are optional, so not in the base],
      [`mips_be`], [32], [#strong[big-endian];], [floating-point numbers],
      [`cortex_m`], [32], [little-endian], [#strong[no heap · no floating-point numbers]; — a machine that runs alone],
      [`cortex_m0`], [32], [little-endian], [the same as `cortex_m` except that the multiplier width is 16 bits (⟦§5.3⟧ (4c)) — Cortex-M0 · M0+],
      [`win64`], [64], [little-endian], [crossing only (⟦§5.6⟧ (4))],
      [`cheriot`], [32], [little-endian], [#strong[pointers are capabilities]; (8 bytes, ⟦§5.3⟧ (4a)) · no heap · no floating-point numbers · crossing only (⟦§5.6⟧ (6))],
      [`atmega328p`], [16], [little-endian], [AVR (Arduino Uno · Nano) · alignment 1 byte · multiplier width 8 bits (⟦§5.3⟧ (4c)) · no heap · no floating-point numbers · emitted and run (⟦§5.6⟧ (8))],
      [`atmega2560`], [16], [little-endian], [AVR (Arduino Mega 2560) · the same values as `atmega328p` · emitted and run (⟦§5.6⟧ (8))],
      [`atmega32u4`], [16], [little-endian], [AVR (Arduino Leonardo · Micro) · the same values as `atmega328p` · up to building (⟦§5.6⟧ (8))],
      [`atmega4809`], [16], [little-endian], [AVR (Arduino Nano Every) · the same values as `atmega328p` · up to building (⟦§5.6⟧ (8))],
      [`word16`], [16], [little-endian], [no heap · no floating-point numbers · #strong[stands up to translation only]; (⟦§5.6⟧ (7))],
      )
    ]
    #para("2a")[
      Machine names form a #strong[tree];. A node inherits its parent's values and states only the values it changes — `cortex_m0` changes only the
      multiplier width from `cortex_m`, and `atmega328p` · `atmega2560` · `atmega32u4` · `atmega4809` inherit the values of the group `avr` unchanged. #strong[Groups]; that stand only at
      branches of the tree (`lowent` · `riscv32` · `avr`) are not machine names and so are refused under (3). The processor shows the expanded values of each node.
    ]
    #para("3")[
      A name not in this list matches no build and so is refused. If a name that does not match were
      accepted, the op would #strong[silently drop out or be silently built wrong]; — both are what this document
      sets out to prevent.
    ]
    #para("4")[
      #strong[What a machine does not have cannot be used.]; The processor refuses it at translation.
    ]
    #tbl("What machines lack — refused on the spot")[
      #table(columns: (auto, auto, 1fr), stroke: 0.5pt + rgb("#bbb"), inset: 6pt,
      [*What is missing*], [*Which machines*], [*If used*],
      [a growing root (heap)], [`cortex_m` · `cheriot` · `atmega328p` · `atmega2560` · `atmega32u4` · `atmega4809` · `word16`], [the `heap` effect · `cap heap` · `region <name> heap` are refused (`E-HEAP-NOHOST`). `alloc` carving from a fixed window can be used (⟦§8.1⟧)],
      [64-bit floating-point numbers (`f64`)], [`cortex_m` · `cheriot` · `atmega328p` · `atmega2560` · `atmega32u4` · `atmega4809` · `word16`], [`f64` and floating literals without a type word are refused (`E-FLOAT-NOFLOAT`)],
      )
    ]
    #para("4-1")[
      `f32` #strong[exists on every target.]; On a target without a floating-point unit it is computed in software, and the result is identical, to the bit, to that of a target with one.
      `f64` exists only on targets with the unit — it cannot be used on the targets of the table above.
    ]
    #para("4-2")[
      The processor accepts software `f32` on every target without a floating-point unit. On the four AVR chips (`atmega328p` · `atmega2560` ·
      `atmega32u4` · `atmega4809`) it has been run and measured to the bit; on `cortex_m` · `cheriot` · `word16` it is built but has not been run —
      a declaration that uses `f32` there is accepted, and translating it warns (`W-FLOAT-UNTESTED`: it may not work correctly; if a result looks
      wrong, tell the developer). That warning is not a rule of the language but the processor saying it has not yet measured it. A target
      for which software `f32` is not built yet refuses `f32` (`E-FLOAT-NOTYET`) — this edition has no such target.
    ]
    #plain[
      Why `f32` and `f64` are separated: 32-bit floating point can be computed exactly in software even on small machines, and there is much work
      that needs it there, such as sensor values and control coefficients. 64-bit is several times heavier to compute, and C compilers for small machines sometimes make `double` 32 bits —
      refusing is better than accepting a value that cannot be produced honestly.
    ]
    #para("5")[
      Whether to use what a machine #strong[has]; is decided by the person building. A build chooses #strong[which instruction sets the binary
      contains];, and if it contains two or more, the program chooses #strong[once, when it starts];, and fixes the choice. It does not
      change during execution — if what ran differed from run to run, a measured number could not be reproduced.
    ]
    #tbl("The range of instruction sets — the build chooses")[
      #table(columns: (auto, 1fr), stroke: 0.5pt + rgb("#bbb"), inset: 6pt,
      [*Chosen*], [*What happens*],
      [contain nothing], [every computation is done with plain code. Stands on every machine],
      [contain one or more sets], [code is emitted #strong[assuming those instructions exist];. It does not run on a machine without them],
      [contain them but «choose»], [both are contained, and one is detected and fixed once at start. Stands on every machine, and is fast on machines that have them],
      )
    ]
    #para("5a")[
      Whichever is chosen, #strong[the answer is the same];. Machine instructions and plain code are two implementations of the same meaning; what differs is
      only speed and #strong[timing properties]; — a computation that reads a table reads at a place that depends on the value, and those instructions do
      not. Where constant time is promised, the difference is written down (#cref("9.2")).
    ]
    #para("5c")[
      What can be contained is not only #strong[instructions that replace a computation]; — #strong[width]; (placing several independent jobs side by side
      in one register) is in the same range too. The rule is the same: the answer is the same (#cref("5.6") (5a)), it cannot be contained
      for a machine without it (#cref("5.6") (5b)), and the choice is made #strong[once, at start];.
    ]
    #para("5b")[
      If the machine lacks the instruction, it cannot be contained. Asking for it to be contained makes translation refuse — it is not silently
      replaced with plain code. The person building shall know what will run.
    ]
    #para("4a")[
      That is, «code that runs on embedded systems too» is #strong[something measured, not something hoped for]; — a program that uses a growing root or
      floating-point numbers stops translating the moment that machine is chosen.
    ]
    #plain[
      Why stop it at #strong[translation]; rather than at execution? Sending an allocation to a machine without a heap makes it blow up
      late, on that machine, at the place hardest to fix. It is better to say #strong[in advance]; that what is missing is
      missing — that is the opposite of what this language calls «hidden cost» in §1.3.
    ]
    #para("5d")[
      `win64` has meaning #strong[only as far as crossing];. C source for that machine can be emitted and built, and that is
      as far as this document speaks; it says nothing about running on it.
      An op that uses something only POSIX has is refused at translation — it is better to #strong[cry first at the place where the reason is
      known]; than to blow up later because a header is missing.
    ]
    #para("6")[
      `cheriot` too has meaning #strong[only as far as crossing]; — emitting C source for that machine and building it. This machine's
      pointers are capabilities (#cref("5.3") (4a)), and `machine.ptr_caps` is true. The pointer of a register block is derived from the
      device-memory capability the execution environment gives (#cref("7.6.1") (3)).
    ]
    #para("7")[
      `word16` has meaning #strong[only as far as translation];. It is not the name of a real machine but the place for measuring whether the same text stands
      in an implementation where the width of `usize` · `idiff` is 16 bits (#cref("6.2") (3-1)). The processor translates and checks text for this target but
      neither emits C source nor runs the text — because whether what is emitted at that width is correct has never been measured. The upper bound of a slice length is 2¹⁵.
    ]
    #para("8")[
      `atmega328p` and `atmega2560` have meaning as far as emitting C source and running it. The C source emitted for that machine is built by
      that chip's C compiler, and the answers from running it on a machine that emulates the chip have been measured to equal other executions of the same text.
      `atmega32u4` and `atmega4809` have meaning #strong[as far as building]; — there is no emulating machine, so the answers from running could not be measured.
      C `int` is 16 bits, so the processor emits 32-bit words as fixed-width C types. Atomic operations are emitted as read-and-write while interrupts are
      blocked — these chips have one core. The upper bound of a slice length is 2¹⁵.
    ]
  ]
  #sub("5.7", "Where a unit states what it is — `package`")[
    #para("1")[
      A translation unit may state #strong[what it is]; with `package <key> <value> .`.
      This has #strong[the same shape]; as a build declaration (`build tier t0 .` and the like) — two idioms
      would be a second expression too, so no new construct is made.
    ]
    #para("2")[
      The keys form a closed set of six. Any other name is refused (`E-PKG-KEY`).
    ]
    #tbl("The keys of `package`")[
      #table(columns: (auto, auto, 1fr), stroke: 0.5pt + rgb("#bbb"), inset: 6pt,
      [*Key*], [*Required*], [*What is written*],
      [`name`], [yes], [the name of this unit],
      [`version`], [yes], [the version number. It shall be in a form #strong[that can be ordered];],
      [`description`], [no], [a one-line description],
      [`date`], [no], [a date],
      [`authors`], [no], [the authors],
      [`license`], [no], [the conditions of use],
      )
    ]
    #para("3")[
      If even one key is written, `name` and `version` #strong[shall]; be present
      (`E-PKG-NONAME` · `E-PKG-NOVERSION`). An identity place without an identity is decoration.
    ]
    #para("4")[
      Writing the same key twice is refused (`E-PKG-DUP`). An identity has #strong[one]; value —
      with two, each tool that reads this place picks one, and not all pick the same.
    ]
    #para("5")[
      `version` shall be in #strong[a form that can be ordered];, or it is refused
      (`E-PKG-VERSION`). A version number nobody can interpret cannot #strong[order]; editions,
      and ordering is the only reason a version number exists.
    ]
    #para("6")[
      A shape other than `package <key> <value> .` is refused (`E-PKG-FORM`).
    ]
    #plain[
      This place exists so that tools and people can learn the identity of this unit cheaply #strong[without reading the whole source];.
      But if what is read cannot be trusted, the value is as good as absent — so
      here alone #strong[what is written is checked.]; A key nobody reads is a key nobody checks,
      and an unchecked duplicate rots into a lie (#cref("1.1")).
    ]
  ]
  #sub("5.8", "Building differently per build — `build option` and `config`")[
    #para("1")[
      A program may declare #strong[knobs];. `build option <name> <kind> …` makes a knob,
      and its value is fixed where the program is built.
    ]
    #para("2")[
      There are three kinds; anything else is refused (`E-OPT-TYPE`).
    ]
    #tbl("Kinds of knob")[
      #table(columns: (auto, 1fr), stroke: 0.5pt + rgb("#bbb"), inset: 6pt,
      [*Kind*], [*What it holds*],
      [`bool`], [on and off],
      [`int`], [an integer],
      [`choice`], [one of the listed values],
      )
    ]
    #para("3")[
      A knob may #strong[depend]; on another knob (`depends`). If the depending knob is a `bool` and is #strong[on];
      while the knob it depends on is off, the build is refused (`E-CONFIG-DEPENDS`) — because #strong[such a build
      does not exist];; without this check `depends` would be decoration.
    ]
    #para("3a")[
      If the depending knob is a `choice` or an `int`, there is no place called «on» — that knob always has a
      value. A mismatched dependency is then #strong[not a nonexistent build but an unused knob];,
      and if the configuration nevertheless picks a value, it is reported (`W-CONFIG-DEPENDS`).
    ]
    #plain[
      Before (3a), «on» was measured only as a non-zero value, so a `choice` knob that could not pick 0
      became #strong[always on];. So #strong[no configuration at all could exist]; that switched off a knob it depended
      on — a knob that no configuration can switch off is not a knob (defect note #18).
    ]
    #para("4")[
      The value of a knob is read with `config <name>`, and it is a #strong[translation-time constant]; (#cref("6.8")).
      Reading a name that does not exist is refused (`E-CONFIG-UNDEF`).
    ]
    #para("4a")[
      If the knob depended on #strong[does not exist at all];, it is refused (`E-OPT-DEPENDS`).
    ]
    #para("4b")[
      If a configuration gives a knob #strong[a value its kind cannot accept];, it is refused (`E-CONFIG-TYPE`).
      So is writing an unknown property (`E-CFG-UNKNOWN-PROP`).
    ]
    #para("5")[
      A knob that is declared but #strong[read by no code]; is refused (`E-OPT-UNUSED`). Such a knob
      appears in the configuration, a user switches it off, and nothing happens —
      #strong[something that looks like a decision but is not one];.
    ]
    #para("6")[
      Once the values are fixed, #strong[switched-off branches do not remain in the output.]; The cost is 0.
    ]
    #caution("Switched-off branches **are still checked**")[
      In other languages' conditional compilation, code for combinations that are not switched on rots unread by anyone.
      That cannot happen here — what is folded away is #strong[code emission];, not parsing and type checking.
      So «it does not build in that combination» does not happen.
    ]
  ]
  #sub("5.9", "Decided but not built: tools — verified versions · the package file · private sources · splitting a large build")[
    #para("1")[
      This clause writes what the #strong[tools]; around the implementation are to do whose meaning is decided but which are not built yet. They are
      not words of the language, so there is no word to refuse. As in #cref("6.9.4"), they are built when a use appears and dropped if shown
      unnecessary. Whether to build or drop them is the owner's decision.
    ]
    #para("2")[
      #strong[Verified version numbers]; — the kind of a version (one that breaks compatibility · one that does not) is decided not by the author but by
      the tool, from the difference of the unit's #strong[visible surface]; (signatures · contracts · effects · layout). Marking a version that breaks
      the surface as non-breaking refuses the release. When a dependency is written as a range (any compatible version), the tool picks the newest
      compatible one, and asks a person before moving to a breaking version. The spelling of the range is decided when built. What stands today:
      a version number must be orderable (#cref("5.7") (5)), and the lock file (`lowent.lock`) pins the content hash of each dependency.
    ]
    #para("3")[
      #strong[The package file]; (`.lowpkg`) — one file that bundles the manifest · sources · metadata · signature for release. It invents no new
      cryptographic format and stays a thin convention over verified bundles. What stands today: dependencies taken from a path · content
      hashes · signatures and trusted keys (`lowentc key` · `lowentc verify`) · mirrors.
    ]
    #para("4")[
      #strong[Authentication to private sources]; — identifying oneself when taking a package from a private server. What stands today: the policy of
      where packages may come from (anywhere · this machine only · signed with an official key only) and content hashes — wherever it came from,
      the same hash is the same package.
    ]
    #para("5")[
      #strong[Splitting a large build]; — dividing build information into pieces by meaning (the role of a piece · size limits · the members of a set),
      and a screen tool (`lowent config`) that chooses the values of the knobs (#cref("5.8")). What stands today: knobs and `config`, and `pkg.low`.
    ]
    #note[
      The sources are RFC-0023 (verified versions · dependency ranges) · RFC-0032 · 0033 (sources · authentication) · RFC-0034 (`.lowpkg`) ·
      RFC-0036 D1 to D4 (splitting a large build). The sweep of 2026-11-11 (F7) also wrote the RC11 memory orders (RFC-0018) · the decision tree of
      `match` (RFC-0081 MM9) · signatures · mirrors · knobs as «not built», but that was #strong[wrong]; — those five are built (the five orders of #cref("7") ·
      jump tables · `lowentc verify` · the mirror setting · #cref("5.8")). It was corrected on 2026-11-12. The owner decided, as for #cref("6.9.4"), to «put them
      in formally, state that they are dropped if they later prove unnecessary, and keep tracking them» (2026-11-12).
    ]
  ]
]
