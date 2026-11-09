#import "../lib.typ": *

#clause("7", "Effects and capabilities")[
  #para("1")[
    If #cref("6") specified computation, this clause specifies #strong[how that computation touches the outside world];.
  ]
  #sub("7.1", "Effects")[
    #para("1")[
      An #t("효과", "effect") is an influence an op has on the outside world. Reading a file, allocating
      memory, waiting and the like are each an effect.
    ]
    #para("2")[
      Effects are a fixed set of #strong[atoms];. The list of atoms is fixed, and the author cannot make new
      atoms.
    ]
    #tbl("Effect atoms")[
      #table(columns: (auto, 1fr), stroke: 0.5pt + rgb("#bbb"), inset: 6pt,
      [*Atom*], [*What it means*],
      [`none`], [no effect at all. This is the bottom],
      [`alloc`], [obtains or gives back memory from #strong[a fixed window]; — that window does not grow (⟦§8.1⟧)],
      [`heap`], [obtains memory from #strong[a growing root]; — more can be fetched during execution. Exists only on machines with an operating system (⟦§8.1⟧)],
      [`io`], [exchanges data with the outside (files · connections · standard input and output)],
      [`wait`], [waits. It wakes up someday without anyone else's help],
      [`concurrent`], [its completion #strong[depends on the progress of another flow of execution];],
      [`lock`], [takes a lock],
      [`atomic`], [does indivisible reads and writes],
      [`state`], [changes the state a module or actor holds],
      [`panic`], [can end the program with an explicit `panic` — traps such as out-of-bounds indexing and overflow are the business of contracts, not effects (⟦§6.4.6⟧)],
      [`device`], [touches a device directly],
      [`unsafe`], [does what the language cannot check],
      [`page_fault`], [a memory access may cause a page fault],
      [`blocking`], [can hold up a flow of execution],
      [`cancel`], [can be cancelled],
      [`detach`], [detaches a flow of execution it made],
      )
    ]
    #para("2a")[
      Of these, five --- `lock` · `page_fault` · `blocking` · `cancel` · `detach` --- #strong[have no core op yet]; that does that work.
      They can be written and spread upward to the callers ((4) below), but the processor has no way of finding out whether the body does that work,
      so it #strong[believes what is written];. It does not refuse them when declared but not done ((6)),
      and it reports that it took them on trust.
    ]
    #para("2b")[
      `device` is produced by touching device registers (`read_volatile` · `write_volatile` aimed at a device's registers,
      reading and writing the fields of a device register view) and by interrupt handlers (ops with a `vector` clause). Like other effects,
      if produced it shall be written, and if written but not produced it is reported ((6)).
    ]
    #para("3")[
      An op shall write the effects it produces in its `effects` clause. #strong[If it produces more effects than written,
      translation is refused.];
    ]
    #para("4")[
      Effects #strong[spread upward]; to the callers. Calling an op adds that op's effects to the caller's
      effects.
    ]
    #para("4a")[
      `state` means changing #strong[the state the op itself holds]; (module state · the state fields of an actor).
      Writing to storage the caller handed over (`mut` · `mut_ref` positions, `collect into <name>`) is not `state`
      — that write #strong[is already declared by the type];, and the caller sees it through its own name.
      It is not hidden state, so there is nothing to count as an effect. Such an op writing `effects state`
      is allowed, and writing it is not counted as «declared but not done» (`W-EFFECT-OVER`).
    ]
    #para("5")[
      Some effects #strong[bring other effects along.]; The following table is all of them.
    ]
    #tbl("Effects brought along (the closure relation)")[
      #table(columns: (auto, auto, 1fr), stroke: 0.5pt + rgb("#bbb"), inset: 6pt,
      [*If written*], [*also counts as written*], [*Why*],
      [`concurrent`], [`wait`], [if it has to wait for a peer's progress, it may be suspended],
      )
    ]
    #para("5a")[
      So a declared effect set `S` is looked at #strong[closed]; — `close(S) = S ∪ {wait | concurrent ∈ S}`.
      Comparing two sets is also done after closing: a requirement `I` is contained in a declaration `D` when
      `close(I) ⊆ close(D)`. So writing `concurrent` makes writing `wait` separately unnecessary,
      and writing only `wait` while doing something `concurrent` is refused.
    ]
    #para("5b")[
      Closing happens #strong[in one place only];. The table of atoms (above) stays flat, and the inclusion relation is not
      mixed into that table.
    ]
    #diagram("effects spread upward to the callers and are compared after closing", " worker  effects concurrent         close = {concurrent, wait}
   ▲ called by boss
 boss    effects state wait         close = {state, wait}               → E-EFFECT (no concurrent)
 boss    effects state concurrent   close = {state, concurrent, wait}   → ok")
    #plain[
      What an effect declaration does, in one sentence — #emph["what can happen when this function is called is written in its
      signature."]; It can be known without opening the body and without following the other functions
      it calls.
    ]
    #para("6")[
      Conversely, #strong[an effect declared but not produced]; is a mistake too. The processor reports it. Declared
      effects are a cost the caller has to bear (a pure op cannot call an op that produces `io`),
      so writing effects that are not produced #strong[overstates]; what this op does.
    ]
    #para("7")[
      An `fn` is an op whose purity #strong[is fixed by contract];, so its effect set is already `none`.
      So #strong[no]; `effects` clause is written on an `fn`. If one is written, the processor refuses
      the program — the meaning is the same, but it becomes #strong[two notations for one meaning];, and then readers and
      tools have to know both. `proc` is different: there the `effects` clause
      #strong[narrows];. A `proc` #strong[always]; writes that clause — without it, it is `E-EFFECT-MISSING` (if there are no effects, `effects none .`).
      When a meaning was fixed for the absence of the clause, that meaning parted from place to place where it was read.
    ]
    #ex("declaring effects", "module ex_effect .

rem A pure op — it touches nothing in the outside world. `fn` itself is that declaration.
export fn double input n u32 . output u32 .
  requires le n. 1000 . .
do
  return mul n. 2 . .
end .

rem An op that produces effects — which effects is written in the contract.
export proc note_and_add input n u32 . output u32 .
  effects panic .
  requires le n. 1000 . .
do
  if gt n. 500 . do panic . end .
  return add n. 1 . .
end .")
    #caution("An effect declaration is a check, not documentation")[
      It differs from writing #emph["this function reads a file"]; as a comment. A comment stays the same even when the code changes,
      but an effect declaration #strong[makes translation refuse]; the moment the body does more.
      And #strong[doing less]; is reported too — if the declaration and the body differ either way, that clause is a lie.
    ]
  ]
  #sub("7.1.1", "How to write the effects line")[
    #para("1")[
      The effects line is #strong[a set];, not a list. Writing the same word twice is refused
      (`E-EFFECT-DUP`) — the second says nothing the first did not.
    ]
    #para("2")[
      A word not in the list is refused (`E-EFFECT-UNDEF`). The names of effects come from the closed
      set of #cref("7.1").
    ]
    #para("3")[
      `none` #strong[cannot come together with anything else]; (`E-EFFECT-NONE-MIX`). Something cannot have effects and have none
      at once — either write the effects or write that there are none.
    ]
    #para("4")[
      Adding `effects none .` to an op that is already pure by contract is refused
      (`E-EFFECT-REDUNDANT`). One meaning has #strong[one spelling];.
    ]
    #para("5")[
      A pure op #strong[cannot declare]; effects (`E-EFFECT-CALC`). Being pure and having effects are
      different kinds, and what chooses the kind is a word, not a clause.
    ]
    #para("6")[
      `via <type>` can be written in the effects line. It means «the #strong[allocation-family effects];
      (`alloc` · `heap` · `atomic`) written by that type's ops are also this op's declaration». If a generic op writes a type parameter
      (`effects state via a .`), that type decides for each monomorphised instance — opened with a bump over borrowed bytes it is
      only `state`, and opened with an allocator standing on a growing root, `heap` stands in the instance's declaration. Effects that came in through `via`
      are not counted as «declared but not produced» even if this op produces only some of them.
    ]
    #plain[
      Without `via`, a generic container has to do one of two things — always write the largest effect any allocator
      could have (false when used with a bump), or write only the smallest (a #strong[hidden allocation]; when used with a heap).
      `via` lets #strong[the type argument decide]; that choice.
    ]
    #plain[
      What (1) · (4) prevent is not a wrong program but #strong[a second way of saying the same thing];.
      When there are two ways, readers go looking for the difference between them and spend time finding out
      that there is none. And someday only one of them gets fixed.
    ]
    #rejected("a pure op cannot declare effects", "module ex_calc_eff .

fn f output u8 . effects io .     rem if it has effects, it is a `proc`
do
  return 1 .
end .", "E-EFFECT-CALC")
  ]
  #sub("7.2", "Capabilities")[
    #para("1")[
      To produce an effect, there shall be the right to produce it. The value that stands for that right is
      a #t("권한", "capability").
    ]
    #para("2")[
      Capabilities #strong[are handed over as arguments.]; There is no global capability that can be taken out and used anywhere.
    ]
    #para("3")[
      Capabilities have kinds. Which kind is written in the type, and if the kind does not match, translation is
      refused. A place that requires a capability authorises #strong[by kind]; — the mere existence of a capability value
      is not enough.
    ]
    #tbl("Kinds of capability")[
      #table(columns: (auto, 1fr), stroke: 0.5pt + rgb("#bbb"), inset: 6pt,
      [*Kind*], [*What can be done with it*],
      [`cap io`], [standard input and output — reading and writing],
      [`cap clock`], [asking the time],
      [`cap random`], [reading the operating system's entropy],
      [`cap net`], [making connections and exchanging data over them --- #strong[turning names into addresses belongs here too];],
      [`cap file_system`], [opening · reading · writing files and directories],
      [`cap process`], [starting other programs, reading their output, waiting for them to end or ending them],
      [`cap tty`], [asking the terminal's size and changing its mode],
      [`cap allocator`], [obtaining memory from a fixed window (it does not grow)],
      [`cap heap`], [obtaining memory from a growing root — can be received only on machines with an operating system],
      [`cap env`], [reading the variables of the execution environment],
      [`cap c`], [calling C functions (#cref("6.9"))],
      [`cap mmio`], [reading and writing device registers],
      [`cap args`], [counting · reading the arguments given when the program was called],
      [`cap machine`], [issuing machine instructions directly (⟦§6.9⟧) — the processor cannot see inside],
      )
    ]
    #para("3a")[
      The #strong[names]; of capabilities are not closed — the author can make their own capability and receive it as a parameter.
      What is closed is #strong[the kinds whose meaning the processor fixes];, and those are the rows of the table above.
    ]
    #para("3b")[
      To do work whose meaning the processor fixes (asking the time · opening a connection and the like), a capability of #strong[the kind that work requires];
      shall be passed. If it does not match, it is refused in one of the following two ways.
    ]
    #tbl("The two shapes of a mismatched capability")[
      #table(columns: (auto, 1fr), stroke: 0.5pt + rgb("#bbb"), inset: 6pt,
      [*What was passed*], [*What happens*],
      [a capability of #strong[another kind]; (`cap io` to a clock)], [refused as being of a different kind],
      [a capability with #strong[a name the processor does not know];], [refused because that effect cannot be produced with that capability],
      )
    ]
    #note[
      Kinds are divided for #strong[least authority];. If there were only one capability, a right received to read a file
      could also open connections, and then an op's signature would no longer say #emph["what this op can do"];.
      Because kinds are divided, the extent of the outside world that can be reached is known from the signature
      alone.
    ]
    #diagram("capabilities are received at the entry point and go down only as arguments", " environment ──▶ main  input out cap io .  input fs cap file_system .
                   ├──▶ save fs     holds file_system only → can open files, cannot print
                   └──▶ show out    holds io only          → can print, cannot open files
 an op handed no capability cannot have that effect — there is no global way to get one")
    #para("4")[
      The entry point of a program receives capabilities from the execution environment (#cref("5.5")). A capability the entry point did not receive
      exists nowhere in the program.
    ]
    #para("5")[
      A capability is #strong[a mark at translation time];, not a value at run time. So an op that receives a capability
      does not receive one more run-time argument because of it.
    ]
    #plain[
      That is, writing `input k cap io .` is #strong[free];. An op that receives a capability and an op that does not have #strong[the same signature];
      at the C boundary — a capability asks #emph["is there the right to call this op"]; at translation,
      and nothing is left to ask at execution.
      The point of this design is that the cost of guarding the boundary does not come back as a run-time cost.
      #emph["Making it safe makes it slow"]; is not true here.
    ]
    #para("6")[
      #strong[Effects and capabilities are a pair.]; The effects line writes #emph[what]; the op does, and the capability inputs write #emph[who permitted it];.
      The same thing is written from two sides, and each checks the other:
      - The work written in the effects line shall receive as an input the capability that permits it (refused if absent).
      - If the body does work that a capability opens, that effect shall be written in the effects line (`E-EFFECT` if absent).
      - Merely receiving a capability does not create an effect — a capability received and not used does no work.
    ]
    #tbl("Effects and the capabilities that permit them")[
      #table(columns: (auto, 1fr, auto), stroke: 0.5pt + rgb("#bbb"), inset: 6pt,
      [*Effect*], [*Permitting capability*], [*When absent*],
      [`io`], [`cap io` · `cap file_system` · `cap net` · `cap tty` · `cap clock` · `cap random`], [`E-EFFECT-NO-CAP`],
      [`alloc`], [`cap allocator` (or a lexical `region` block)], [`E-ALLOC-NOCAP`],
      [`heap`], [`cap heap`], [`E-HEAP-NOCAP`],
      [`atomic`], [`cap atomic`], [`E-ATOMIC-NOCAP`],
      [`device`], [`cap mmio` — at the place that actually touches registers], [`E-MMIO-NOCAP`],
      [`state` · `panic` · `wait` · `concurrent`], [— written without a capability], [—],
      )
    ]
    #plain[
      They are a pair so that one line of signature answers two questions. From `effects io` alone one knows that #emph["it talks to the outside"];
      but not #strong[with what];. `input fs cap file_system .` narrows that — it can open files but
      cannot open connections. Conversely, with only a capability and no effects line, the caller cannot know whether that op is pure.
    ]
    #ex("it can be produced only with a capability received", "module ex_io .

use stdio .

rem To output, `cap io` shall be received as an argument.
proc main input out cap io . output u8 . effects io .
do
  let n usize stdio.write out. 1 \"hello\\n\" . .
  return cast u8 n. . .
end .")
    #rejected("without a capability not one byte can be put out", "module ex_io_bad .

use stdio .

proc main output u8 . effects io .
do
  let n u64 stdio.write 1 \"hello\\n\" . .   rem no capability was received
  return narrow u8 n. . .
end .", "E-EFFECT-NO-CAP")
    #plain[
      The only difference between the two examples is one argument. The first received the output capability and the second did not.
      #strong[There is no hidden standard output]; — no function can put bytes outside without a capability.
    ]
    #para("6a")[
      There is no special notation for passing a capability on. The received name is written like any other argument.
    ]
    #para("7")[
      An op that did not receive a capability cannot call an op that requires it. There is nothing to pass on.
    ]
    #ex("capabilities go down along the chain", "module ex_cap_chain .

use stdio .

rem The capability is received as an argument — its name is `k`, its type `cap io`.
proc say input k cap io . input msg slice u8 . output usize . effects io .
do
  return stdio.write k. 1 msg. . .
end .

rem The caller **just passes on by name** the `k` it received.
proc say_twice input k cap io . input msg slice u8 . output usize . effects io .
do
  let a usize say k. msg. . .
  let b usize say k. msg. . .
  return add a. b. . .
end .

rem The entry point receives the capability **from outside** — nobody can make one themselves.
proc main input k cap io . output u8 . effects io .
do
  let n usize say_twice k. \"hi\\n\" . .
  guard eq n. 6 . else return 1 . .
  return 0 .
end .")
    #plain[
      The chain looks like this — outside → `k` of `main` → `k` of `say_twice` → `k` of `say` →
      `stdio.write` → the builtin op `write_out`. An op that did not receive the capability cannot call `say`. #strong[There is nothing to pass on.];
      That is, what guards the boundary is not grammar but #strong[arguments];. There is no rule to memorise, and by looking at the call sites
      one can follow by eye where the right came from.
    ]
    #note[
      The value of this rule lies in being able to answer #emph["can this program touch files"]; #strong[by looking only at the entry
      point];. No function deep inside can secretly open a file —
      without having received the capability it cannot open one, and if it did receive one, that chain is visible all the way to the entry point.
    ]
    #plain[
      It can be compared to a key. To enter a room there has to be a key, and a key exists only when someone hands it
      over. Nobody can make up a key when none was given.
    ]
  ]
  #sub("7.3", "What purity means")[
    #para("1")[
      An op that is `effects none` is #t("순수", "pure"). A pure op satisfies the following.
    ]
    #para("2")[
      a) It always gives the same result for the same input.
    ]
    #para("3")[
      b) Changing the order in which it is called does not change the result of the program.
    ]
    #para("4")[
      c) If its result is not used, it need not be called.
    ]
    #para("5")[
      A pure op cannot call an op that is not pure.
    ]
    #para("6")[
      If an `fn` writes to storage it received as `mut`, that write is visible to the caller, so it is not pure (`E-EFFECT-PURITY`). Whether it writes directly,
      through a local name that sees that storage (rebound with `subslice` · `view_array`), or by passing it to a `mut` input of a `proc`,
      it is the same — if the receiver writes, the caller's storage changes. Passing it to a `mut` input of an `fn` is not counted here, since that `fn` is checked by the same
      rule. Writing to storage built inside the op is not visible from outside, so it does not break purity.
    ]
    #rejected("an `fn` hands the `mut` piece it received to a `proc` to write", "module ex_purity_via .

proc put input out mut slice u8 . output u64 . effects none . do
  set idx out. 0 . 7 .
  return 0 .
end .

fn f input out mut slice u8 . output u64 . do
  return put out. . .     rem the caller's piece changes
end .", "E-EFFECT-PURITY")
  ]
  #sub("7.4", "Where other languages are met")[
    #para("1")[
      Lowent can exchange with C. That boundary is marked with `extern`.
    ]
    #para("2")[
      At the place that crosses the boundary, the language's guarantees do not carry over by themselves. So at the boundary
      #strong[contracts shall be written];, and the processor enforces those contracts at the boundary.
    ]
    #para("3")[
      The names going out to the C side are decided by the author. The processor does not make up names —
      that name is a promise made to another language, so the one who promises shall write it.
    ]
    #para("4")[
      The name on the C side is written with the clause `link "<name>" .`. This is the «name the author decides» that (3) speaks of,
      and the processor does not make it up from the op's name.
    ]
    #para("5")[
      An `extern` with the clause `variadic .` takes, after its fixed arguments, #strong[arguments whose number is not fixed];.
      Contracts do not reach such arguments, so the caller is responsible for that place.
    ]
    #para("6")[
      C can also be made #strong[to call Lowent];. The address of an `export extern` op is obtained with
      `unsafe_fn <op>` and passed as a value.
    ]
    #para("7")[
      At that place #strong[the direction of the boundary reverses.]; Handing it over is `unsafe`, since this language cannot know when, how many times, or in which flow
      it will be called, but #strong[the place where the call comes in]; is this side's
      door, so that op's contract is enforced on the arguments. That is, inside the door this language is responsible, outside the door
      C is.
    ]
    #caution("A boundary is a door, not a hole")[
      If the place where another language is met were left as #emph["from here on there is no guarantee"];, from that moment
      the guarantees of the whole program would become meaningless. Lowent #strong[sets up contracts]; at that place and makes people write what is
      promised to them and what they promise.
    ]
  ]
  #sub("7.5", "Doing things atomically")[
    #para("1")[
      Operations with the `atomic` effect #strong[are not split]; — another flow cannot see their middle.
      There are reading · writing · adding · bit operations · swapping (swap) · compare-and-swap.
    ]
    #para("1a")[
      The result of an atomic operation that gives a value is of the element type of that slot. A read gives the slot's value, and add · subtract · bit operations · swap give
      the value #strong[before the change];. Compare-and-swap (`atomic_cas`) gives 1 if it swapped and 0 otherwise, in that type.
    ]
    #para("1b")[
      Each machine has fixed widths for atomic operations done without a lock — the width for reads · writes and the width for read-modify-writes (add · subtract · bit operations · swap ·
      compare-and-swap). If the element of the slot is wider than that, it is refused (`E-ATOMIC-TARGET`). In this processor `cortex_m` is 4 bytes for both,
      `cortex_m0` is 4 bytes for reads · writes and has no read-modify-writes, and other machines go up to 8 bytes. The processor shows those numbers per machine.
    ]
    #para("2")[
      `atomic_fence` computes no value and fixes #strong[only order]; — it makes what came before it visible before what comes
      after.
    ]
    #para("3")[
      Atomic operations shall write their effect in the contract. Using them without writing it does not conform.
    ]
    #para("4")[
      `order <name>` can be attached after an atomic operation to fix #strong[what is visible to other flows, and when];.
      The names are the following five and no more; anything else is refused (`E-ATOMIC-ORDER`).
    ]
    #tbl("Memory ordering")[
      #table(columns: (auto, 1fr), stroke: 0.5pt + rgb("#bbb"), inset: 6pt,
      [*Name*], [*Meaning*],
      [`relaxed`], [atomicity only, #strong[no order];. Only that the value does not tear],
      [`acquire`], [what comes #strong[after]; this read does not leak forward],
      [`release`], [what comes #strong[before]; this write does not leak backward],
      [`acq_rel`], [both. Used for operations that read and write],
      [`seq_cst`], [every flow sees #strong[one and the same order];. The strongest],
      )
    ]
    #para("5")[
      If no order is written, it is taken as `seq_cst`. The strongest is the default because it is the easiest
      to reason about — weakening it is something done #strong[by someone who writes it knowing its value];.
    ]
    #para("6")[
      The orders that can be used differ by operation. #strong[Combinations without meaning are refused]; (`E-ATOMIC-ORDER`).
    ]
    #tbl("The orders each operation can use")[
      #table(columns: (auto, 1fr), stroke: 0.5pt + rgb("#bbb"), inset: 6pt,
      [*Operation*], [*Orders that can be used*],
      [read (`atomic_load`)], [`relaxed` · `acquire` · `seq_cst` — a read cannot be `release`],
      [write (`atomic_store`)], [`relaxed` · `release` · `seq_cst` — a write cannot be `acquire`],
      [fence (`atomic_fence`)], [`acquire` · `release` · `acq_rel` · `seq_cst` — a fence with no order at all is not a fence, so `relaxed` cannot be used],
      )
    ]
    #plain[
      Why block combinations too? `atomic_load … order release` #strong[makes sense as a sentence but has no
      meaning]; — what would a read release? C left it as undefined behaviour,
      and undefined behaviour is silent. This language leaves nothing silent.
    ]
    #note[
      Atomicity #strong[is not free.]; Using atomic operations on a value only one flow touches only makes that value
      slower. So this language does not make it the default but #strong[has it chosen by name]; —
      so that the cost is written in the signature.
    ]
  ]
  #sub("7.6", "Where devices are talked to")[
    #para("1")[
      Reading and writing device registers is not an ordinary memory access. It is written with `read_volatile` and
      `write_volatile`, and the processor #strong[shall not merge, remove or reorder];
      those accesses.
    ]
    #para("2")[
      These accesses require the `device` effect and the matching capability (#cref("7.2")).
    ]
    #para("3")[
      Reading or writing that place #strong[like an ordinary field]; is refused (`E-MMIO-PLAIN`) --- that is,
      reading with `field <aggregate> <register>` or writing with `set field <aggregate>. <register> . <value> .`.
      An ordinary field access is an operation the processor #strong[may merge or remove];, so a device access written in that spelling
      #strong[cannot keep]; what (1) requires. If there are two ways of writing the same meaning
      and only one keeps that promise, the other is not a way but a trap.
    ]
    #rejected("touching a device register like an ordinary field", "module ex_mmio_plain .

build tier t1 .

struct gpio do
  mmio 0x40020000 .
  moder u32 rw .
end .

unsafe proc drive input dev cap mmio . input regs mut slice u8 . output u32 . effects device unsafe .
do
  let g mut view gpio view gpio regs. . .
  set field g. moder . 2 .       rem an ordinary field write — an operation the processor may remove
  return 0 .
end .", "E-MMIO-PLAIN")
    #note[
      The purpose of an ordinary read is #emph["knowing the value"];, so the processor may remove a second read.
      For a device register #strong[the act of reading is itself the work]; — reading sometimes changes the state.
      So it must not be removed, and the language has to know that.
    ]
  ]
  #sub("7.6.1", "Register blocks — `mmio`")[
    #para("1")[
      A device's registers are written as an aggregate, and `mmio <start address> .` is attached to that aggregate to declare it
      #strong[a map of the device];. The start address shall be an integer (`E-MMIO-BASE`) — a map has a start.
    ]
    #para("2")[
      Opening that place without a map is refused (`E-MMIO-NOBASE`).
    ]
    #para("3")[
      The address of a register block #strong[comes from a capability.]; Only an op holding `cap mmio` reaches that place (#cref("7.2")), and the start address
      is read as an address within the range that capability permits. On a machine whose pointers are capabilities (#cref("5.3") (4a)), the block's pointer is #strong[derived];
      from the capability received by the entry point — no pointer is made from an integer address alone. On a machine without such capabilities, the capability is only
      a mark, and the meaning is the same.
    ]
    #tbl("Places where a register block is refused")[
      #table(columns: (auto, 1fr), stroke: 0.5pt + rgb("#bbb"), inset: 6pt,
      [*Diagnostic*], [*What went wrong*],
      [`E-MMIO-BASE`], [the start address is not an integer],
      [`E-MMIO-NOBASE`], [trying to open at its own address an aggregate that did not declare `mmio`],
      [`E-MMIO-FIELD`], [a register is not of a shape the machine can reach at once — its width shall be 1 · 2 · 4 · 8 and it shall be in its own place],
      [`E-MMIO-BYVALUE`], [trying to receive a register block #strong[by value];. Receiving by value is copying, and #strong[a device cannot be copied];],
      [`E-MMIO-PERM`], [trying to write a read-only (`ro`) register],
      [`E-MMIO-PLAIN`], [reading or writing a register like an ordinary field --- it shall be written with `read_volatile` · `write_volatile`],
      [`E-MMIO-NOCAP` · `E-MMIO-NOEFFECT`], [the capability (`cap mmio`) or the effect (`device`) is missing],
      [`E-MMIO-NOHOST`], [trying to open an absolute address on a machine with an operating system — that address is not a device],
      )
    ]
    #plain[
      That a device cannot be received by value is the place in this clause people run into most often. Passing an aggregate by value
      is #strong[copying];, and writing to the copy does not reach the device. A program written that way
      does nothing while saying nothing — so it is stopped at translation.
    ]
  ]
  #sub("7.7", "Where interrupts are received")[
    #para("1")[
      Attaching a `vector <number> .` clause to an op makes that op #strong[an interrupt handler];. To write its urgency as well,
      attach `priority <number> .`. No new word is introduced — clauses do that work.
    ]
    #para("2")[
      An interrupt handler #strong[is called by the machine.]; So it shall keep the following four.
    ]
    #tbl("What an interrupt handler shall keep")[
      #table(columns: (auto, auto, 1fr), stroke: 0.5pt + rgb("#bbb"), inset: 6pt,
      [*To keep*], [*If violated*], [*Why*],
      [nobody calls it], [`E-ISR-CALLED`], [if our code calls it, the interrupt runs in a wrong state, #strong[on the wrong stack and at the wrong priority];],
      [no parameters], [`E-ISR-PARAMS`], [the machine passes no arguments],
      [returns nothing], [`E-ISR-OUTPUT`], [there is nobody to return to — the machine woke it, and the machine resumes what was interrupted],
      [writes `effects device`], [`E-ISR-EFFECT`], [it is a place that exists because of a device],
      )
    ]
    #para("3")[
      Shared state passes between interrupt handlers and ordinary code by #strong[a discipline of priorities and queues];.
      That discipline is not fixed by this document; it is the work of libraries (#cref("9.5")).
    ]
  ]
  #sub("7.2.1", "Where capabilities mismatch")[
    #para("1")[
      Capabilities are judged #strong[by kind];. Receiving a capability of another kind does not open that door
      (`E-CAP-KIND`). Not having one but #strong[what it is]; makes the authority.
    ]
    #para("1a")[
      Core ops and builtin ops that require a capability (input/output · terminals · clocks · random numbers · files · the network · the environment · arguments) #strong[write that capability as their first operand.];
      Not writing it does not conform (`E-CAP-MISSING`) — merely having received the capability does not let it be used, and there is no way to reach a capability
      without a name.
    ]
    #para("1b")[
      The name written in that place is #strong[the name of a capability this op received as an input];. A capability is not moved around like a value
      but handed from head line to head line (#cref("7.2")). So a line that #strong[binds a capability to a local name];
      (`let` · `var` written with `cap <kind>`) does not conform by itself (`E-CAP-LOCAL`) —
      whether or not that name is used.
    ]
    #para("1c")[
      The same holds at #strong[the place that calls an op]; taking a capability as an input. The name written in that op's capability position shall be the name of a capability
      the calling op received as an input (`E-CAP-LOCAL`), the kind shall be the same (`E-CAP-KIND`), and an op whose first input is a capability cannot be called
      without a capability (`E-CAP-MISSING`). Builtin ops that require capabilities are wrapped and provided by the standard library (#cref("9.3") (1a)),
      so the place where an application program meets this rule is usually the line that calls that wrapper.
    ]
    #para("1d")[
      An op #strong[does not give out]; capabilities. An op that writes `cap <kind>` in its `output` clause — as it is, or inside another type
      such as `option` — does not conform (`E-CAP-OUTPUT`). An op that returns a capability becomes a second source invisible in the caller's
      head line. Spawning an actor with capability fields is specified separately by #cref("8.6") (`E-CAP-FORGE`).
    ]
    #rejected("binding a received capability to a local name", "module ex_cap_local .

proc main input out cap io . output u8 . effects none .
do
  let k cap io out. .   rem this line is refused even if it is not used
  return 0 .
end .", "E-CAP-LOCAL")
    #rejected("an op that gives out a capability", "module ex_cap_output .

fn screen input out cap io . output cap io .
do
  return out. .
end .", "E-CAP-OUTPUT")
    #para("2")[
      Requiring a capability the machine being built for cannot give is refused (`E-CAP-NOHOST`).
    ]
    #para("3")[
      An op that writes the `alloc` effect and #strong[does not receive a capability to obtain it]; is refused (`E-ALLOC-NOCAP`).
      It wrote that it would use the heap but not where that place is received from. The capability is
      a `cap allocator` input, a region parameter, or a region block of a kind other than `heap`.
    ]
    #para("3a")[
      The `heap` effect is the same: written without a `cap heap` input or a `region <name> heap` block, it is refused
      (`E-HEAP-NOCAP`). `cap allocator` is the capability of a fixed window and does not authorise growing.
    ]
    #para("3b")[
      On a machine without an operating system (`machine.no_heap`), a growing root cannot be requested #strong[from any place]; —
      the `heap` effect, a `cap heap` input and a `region <name> heap` block are all refused (`E-HEAP-NOHOST`).
      On the same machine, `alloc` carving a fixed window can be used.
    ]
    #para("4")[
      An op marked `unsafe` that does not write the `unsafe` effect is refused (`E-UNSAFE-UNUSED`) —
      the mark has meaning only when it reaches the caller, and what carries it there is the effects line.
      Conversely, doing such work without the effect is refused (`E-UNSAFE-UNDECLARED`).
    ]
    #para("4a")[
      `unsafe` #strong[spreads all the way]; along calls. There is only one place where it stops: an op that writes the clause `absorbs machine <name> .`.
      That op can call `unsafe` things in its body and #strong[does not carry it in its own effects
      line];. And `<name>` is a `cap machine` inside that body — that is where that capability is born.
      No other kind can be absorbed (`E-ABSORB-SCOPE`): making up a capability that reaches the world would create an authority
      the caller cannot see.
    ]
    #para("4b")[
      An absorbing op shall have #strong[all]; of the following; if even one is missing, it is refused.
    ]
    #tbl("What absorbing requires")[
      #table(columns: (auto, auto, 1fr), stroke: 0.5pt + rgb("#bbb"), inset: 6pt,
      [*To have*], [*Diagnostic*], [*Why*],
      [an effects line of `none`], [`E-ABSORB-IMPURE`], [absorbing stops only `unsafe` — the caller has to know about other effects],
      [`reference <op> .`], [`E-ABSORB-NOREF`], [without #strong[a pure version]; that gives the same answer, there is no way to tell «correct» from «consistently wrong»],
      [one or more `requires`], [`E-ABSORB-NOCONTRACT`], [the inside cannot be seen but #strong[the door can]; — length · alignment · range are checked at the door],
      [`why "…" .`], [`E-ABSORB-NOWHY`], [a promise whose assumptions are not written can be confirmed by nobody],
      [permission in the manifest], [`E-ABSORB-PLACE`], [#strong[who]; may vouch is decided not by the source that wants that right but by the project's identity file],
      )
    ]
    #para("4b′")[
      The manifest writes by name the modules permitted to absorb. A module not written there that absorbs is refused,
      and if there is no manifest at all, #strong[nobody]; can absorb — the default is no permission.
    ]
    #para("4c")[
      The processor #strong[shall be able to say]; where absorbing happened — which op absorbed what, and on what ground,
      one line each. A zone it cannot see is not hidden but left #strong[visible];.
    ]
    #para("4d")[
      Absorbing is #strong[a choice];, not a duty. An op that does not absorb passes `unsafe` on as now.
    ]
    #plain[
      Spreading is right — if the caller does not know what the processor cannot see, the effects line becomes a lie. But without a place
      where it stops, that door becomes #strong[a door nobody can use]; (measured 2026-09-23: uses of `asm` in the standard library,
      0). So a place to stop is opened #strong[narrowly and checkably];. The tool lists those places, and the ledger holds the signature and
      the hash of the body — what people agree to keep is either counted or disappears.
    ]
    #rejected("saying it will use the heap without receiving that capability", "module ex_alloc_nocap .

proc f output u8 . effects alloc .    rem there is no `input k cap allocator .`
do
  return 1 .
end .", "E-ALLOC-NOCAP")
  ]
  #sub("7.2.2", "Where things come in and where they go out")[
    #para("1")[
      The op at which a program starts receives #strong[only capabilities]; (`E-ENTRY-PARAMS`). It receives no data —
      there is nobody to hand it over.
    ]
    #para("2")[
      The capabilities the starting place can receive are #strong[a closed row]; — `args` · `env` · `io` ·
      `allocator` · `heap` · `file_system` · `net` · `process` · `tty` · `clock` · `random` · `atomic`.
      (`process` — the right to start other programs and watch them.)
      If the starting place requires any other capability, it is refused (`E-ENTRY-CAP`) — writing a right nobody can give
      is decoration, and #strong[decoration is a lie.];
    ]
    #plain[
      Why only seven? The capabilities of the starting place shall be #strong[what the executing side can actually hand over];.
      Capabilities not on the list (for example atomics · machine instructions · devices) #strong[cannot be made up by the program
      itself];, nor are they something the executing side can hand over — such capabilities shall be made at a place entitled to
      give them and flow as arguments. Devices (`mmio`) and machine instructions
      (`machine`) are such, and so is the door into C (`c`).
    ]
    #para("3")[
      Names of other modules can be used only if that side `export`ed them (`E-VISIBILITY`). Writing the module
      in front of a name does not create an authority that was not there.
    ]
    #plain[
      The one thing these clauses repeat — #strong[capabilities are handed over, not picked up.];
      The heap, the door into C, machine instructions, devices and the starting place are all the same. A power whose source
      is not written does not exist in this language.
    ]
  ]
]
