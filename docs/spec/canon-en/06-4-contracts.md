## 6.4 Declarations and contracts

## 6.4.1 Declaring an op

(1) An ⟦op|op⟧ is a unit of execution with a name and a contract. Ops are divided into two kinds.

> [!용어] fn (pure op)
> An op that produces no effect at all. It always gives the same result for the same input. Since it does not touch the outside
> world, executing it in a different order gives the same result.

> [!용어] proc (effectful op)
> An op that produces effects. It shall write in its own contract which effects it produces (⟦§7.1⟧), and if it produces more
> effects than written, translation is refused.

(2) The kind **is always written.** There is no default — whether it is `fn` or `proc` is written in front of the name.

(3) The declaration of an op consists of, in order, the name, inputs, output, effects, contract and body. Inputs are written with `input`,
      the output with `output`. The order is laid out **so that what comes first is used by what comes after**:
      - A named promise (`satisfies`) comes directly after the name — it says first **what** this op is.
      - Translation-time inputs (`comptime`) come next — the types of the following inputs and output use those names.
      - Capability inputs come before data inputs — who permitted it is seen before what it receives.
      - The output comes after the inputs — the output type may use the inputs' type parameters.
      - Effects come after the output and before the contract — they write **what is done** with the capabilities received earlier.
      - The contract is in the order input conditions (`requires`) · output promises (`ensures`) · failures (`errors`) · tests (`tests`).

(3a) The clauses of the head are written **in one order only**. From front to back:
`satisfies` · `lowdoc` · `vector` · `priority` · `comptime` inputs · capability and region inputs (whose type is `cap …` · `region …`) ·
      `use` · data inputs · `output` · `effects` · `link` · `variadic` · `asm` · `absorbs` · `reference` · `why` · `access` · `inplace` · `invalidates` · `parallel` · `reduce` ·
      `requires` · `ensures` · `errors` · `tests` · `schedule`. Clauses in the same position keep the order in which they are written.
      The method signatures of a trait (⟦§6.11.2⟧) and `proc`s inside an actor follow the same order.
      Violating this order is refused at translation (`E-CLAUSE-ORDER`). The order of inputs is also the order of the caller's arguments —
      so capabilities are always passed **before** data.

(3b) The formatter (`--fmt`) moves clauses that are not inputs, and `use` clauses, into this order. It does not move inputs among themselves —
      moving inputs would change the meaning of calls.

(3c) Each clause of the head **is closed by its own dot** — the same as statements (⟦§6.1.6⟧ (2)). A clause word does not close the previous clause,
      and neither does a newline. Meeting the next clause word or `do` without a dot is refused at translation (`E-DOT-MISSING`).
      The list of clauses is the head, so clauses begin with the first word after the name — even if the name has the same spelling as a clause word
      (`proc link …`), it is read as the name.

(3d) The meanings of the clause words that appear in the order of (3a), and where they are defined, are as follows.
      - `satisfies` — a named promise this op keeps (⟦§6.4.5⟧ · ⟦§6.11.2⟧). `lowdoc` — a description for people to read (⟦§6.4.3⟧).
      - `vector` · `priority` — make this op an interrupt handler: the number of the interrupt it receives and its urgency (⟦§7.7⟧).
      - `comptime` inputs — parameters fixed at translation time (⟦§6.8⟧). Capability inputs — the right to produce effects (⟦§7.2⟧).
      - `use` — the allocator this op uses (⟦§8.14⟧).
      - `link` · `variadic` — the name on the C side · arguments whose number is not fixed (⟦§7.4⟧).
      - `asm` — the place that writes machine instructions directly (⟦§6.9.1⟧).
      - `absorbs` · `reference` · `why` — the declaration that the spread of `unsafe` stops here, and the pure version and assumptions that ground it (⟦§7.2.1⟧).
      - `access` · `inplace` · `invalidates` — what it touches and in which mode · writing in place · what it invalidates (⟦§6.4.3⟧ · ⟦§8.12⟧).
      - `parallel` · `reduce` — a loop that runs divided, and the place where the steps gather (⟦§6.5.10⟧).
      - `requires` · `ensures` · `errors` · `tests` — the clauses of the contract (⟦§6.4.3⟧).
      - `schedule` — comes only in the head of a test (`test`). `schedule explore_interleavings .` runs **every order** in which messages are delivered
        and checks that the result is the same for each order. With `limit <number>` it runs only up to that number. If the result differs by order,
        the test fails (`E-SCHED-NONDET`). The meaning is specified by ⟦§6.4.13⟧ (4).

(3e) **The output can be named** — `output <name> <type> .`. That name is a local of the body and **starts at 0**:
      `0` · `0.0` for numbers, `false` for `bool`, `none` for `option`, and for a struct the 0 of each field. The body writes to that name and
      returns it with `return <name> .` (as with other ops that return a value, every path shall end with `return` — ⟦§6.5.5⟧ (3)).
      - Distinguishing: there is one namespace (⟦§6.10⟧), so if the first word after `output` **is a type name**, the whole output is a type;
        if it is not a type name and what follows is one type, that word is the name of the result.
      - A type without a fixed zero (slices · `result` · `owned` · enums · newtypes · generic parameters, and structs with such fields)
        cannot be named (`E-RESULT-NOZERO`). Such an output is written without a name and returns a value.
      - An op without a body (`extern` · the method signatures of a trait) cannot name its result (`E-RESULT-NAMED`).
      - An output that returns an array (`output [<name>] array <length> <type> .`) **has its place provided by the caller** — ⟦§6.2.6⟧ (1c).
      - `ensures` still refers to the result as `ret` (⟦§6.4.12⟧). The name of the result is visible only inside the body.

```lowent 예제: a named result starts at 0
module ex_named_result .

struct span do
  lo u64 .
  hi u64 .
  seen bool .
end .

export fn bounds input xs slice u64 . output s span .
do
  for x xs. do
    if or not field s. seen . . lt x. field s. lo . . . do set field s. lo . x. . end .
    if gt x. field s. hi . . do set field s. hi . x. . end .
    set field s. seen . true .
  end .
  return s. .
end .
```

```lowent-거부: a name given to a type without a fixed zero · E-RESULT-NOZERO
module ex_named_nozero .

fn first input xs slice u8 . output r slice u8 .
do
  return xs. .
end .
```

```lowent 예제: declaring an op
module ex_op .

export fn twice input n u32 . output u32 .
  requires le n. 2147483647 . .
do
  return mul n. 2 . .
end .
```

```lowent-거부: the output written before the input · E-CLAUSE-ORDER
module ex_clause_order .

fn twice output u32 .
  input n u32 .
  requires le n. 2147483647 . .
do
  return mul n. 2 . .
end .
```

```lowent-거부: a head clause without a dot · E-DOT-MISSING
module ex_clause_dot .

fn twice input n u32 output u32 .
do
  return mul n. 2 . .
end .
```

> [!산문]
> `export` is the mark that lets other modules use this op. Without it, it is used only inside its
> module.

## 6.4.2 What a contract is

(1) A ⟦계약|contract⟧ is the **promise an op writes for itself** about its input and output.
      A contract is not a comment but **a statement that is checked**.

(2) Contracts are used in three places.

(3) a) **The processor uses them as facts** — on the premise that the contract is true, it removes checks (⟦§6.4.5⟧).

(4) b) **They are enforced during execution** — a contract the processor could not prove is confirmed during execution,
      and if broken it traps. But whether the remaining checks are actually kept is decided by **the build mode** (⟦§6.4.8⟧).

(5) c) **They speak to readers** — what must be kept to call this op is visible without opening
      the body.

(6) There are two places where a contract breaks, and the diagnostic separates **whose fault it is**.

> [!표] When a contract is broken
> #table(columns: (auto, auto, 1fr), stroke: 0.5pt + rgb("#bbb"), inset: 6pt,
> [*Clause*], [*When it is checked*], [*Whose fault if broken*],
> [`requires`], [on entry], [**the caller's** — it did not keep the condition it had to keep],
> [`ensures`], [on exit], [**this op's** — it broke its own promise (the caller was told a lie)],
> )

> [!산문]
> This distinction really pays off. When a program stops, the diagnostic says at once *"did I call it wrongly, or was that function
> built wrongly"*.

> [!주의] A contract is an asset, not a cost
> It is easy to think *"writing checks makes it slower"*. In Lowent it is the opposite. When a contract is written,
> the processor **uses it as a fact** and removes checks during execution. If it is not written, the processor does not know, so
> the check remains — that is, **writing honestly is faster**.

## 6.4.3 The six clauses of a contract

(1) A contract consists of the following six clauses. Not every clause is required.

> [!용어] requires (precondition)
> A condition that shall be true at the moment of entering the op. The caller is responsible for keeping it.
> It is checked on entry, and if it is broken it traps.

> [!용어] ensures (postcondition)
> A condition that is true when the op ends normally. The maker is responsible for keeping it.
> The value returned can be referred to by the name `ret`.

> [!용어] errors (error condition)
> When the op fails, and the error value then. It states which error comes under which condition.
> The condition is read with **the values on entry** to the op — ⟦(1c)⟧.

> [!용어] effects (effect declaration)
> The list of effects this op produces (⟦§7.1⟧). If it produces no effect at all it is `effects none`.

> [!용어] access (access declaration)
> What this op touches, and how — written `access <name> <mode> .`. The modes are the seven in the table below
> and no more; any other word is refused (`E-CONTRACT-MODE`).

> [!표] The modes of `access` — a closed set of seven
> #table(columns: (auto, auto, 1fr), stroke: 0.5pt + rgb("#bbb"), inset: 6pt,
> [*Mode*], [*Enforced now*], [*Meaning*],
> [`shared_read`], [**yes**], [**does not write** to this place. Writing is refused],
> [`write_only`], [**yes**], [**does not read** this place. Reading is refused],
> [`sequential`], [no], [goes through in order],
> [`random`], [no], [touches any place],
> [`streaming`], [no], [passes once and does not look again],
> [`tiled`], [no], [goes through in blocks],
> [`read_mostly`], [no], [mostly reads and rarely writes],
> )

> [!용어] tests (examples)
> Examples that show how this op is used. The processor can run them as tests.

(1a) The clause `tests <name>…` writes one or more **names of ops** that test this op. The names written shall be ops visible
      at that place; if not, it is refused (`E-CONTRACT-UNDEF`). The name of a test block (⟦§6.4.13⟧) is not an op,
      so it cannot be written here. The `tests` clause **neither defines nor runs** tests — it **only points** at
      what already exists. If an op pointed at disappears, the head reports it.

(1b) The `lowdoc` clause writes the description of this op. One line is `lowdoc "<text>" .`; several lines are opened with
      `lowdoc text <end mark>`, and the text runs up to the line on which that mark stands alone. This text is read by the document output
      (`--doc`) and does not affect the meaning of the translated program.

(1c) The condition of the `errors` clause is read with **the values on entry** to the op.

(1c1) If the body **never** raises that error, the processor reports it
      (`W-ERRORS-UNRAISED`). The clause is read as the promise «this op can fail this way»,
      and the caller builds branches by looking at it; a branch that cannot be taken becomes **dead code that the reader cannot tell apart
      from live branches**. An op that passes on someone else's `result` as it is is regarded as raising that
      error (`try` · `return <call>`).

(1d) So the only names that can stand in that condition are **those that are the same on entry and on exit** — namely
      inputs that do not change and module constants. Writing in the condition a name the body can change (an actor's state field · a module `var` ·
      an argument in a `mut` position) is refused (`E-ERRORS-STATE`).

> [!산문]
> Why on entry? In the table of ⟦(6)⟧ `errors` stands on the same side as `requires` — it is a clause that says **what the caller
> did wrong**. What the caller did is only **what it handed over**, so the only values that can measure that
> mistake are what was handed over. Read with the values on exit, a successful execution would accuse
> itself — an op that subtracted 30 from a balance of 50 and succeeded would, because the condition «more than the balance» is read again on exit,
> become *"it did not raise the error it should have"*.

> [!주의]
> (1d) refuses generously. A slice in a `mut` position does not change length, so a condition that reads only `len`
> is in fact the same on entry and on exit, but the processor does not distinguish **which part** of the value a condition
> reads. Rather than pretending to distinguish what cannot be distinguished, it is better to draw the line thickly and write
> that down — write it with `guard` inside the body.

(2) The conditions used in a contract shall be **pure expressions** — something that produces effects cannot be written in a contract.

(3) `shared_read` and `write_only` are **disciplines of reading and writing**, so the processor enforces them now. That is,
      writing to a place declared `shared_read` is refused, and reading a place declared `write_only` is refused
      (`E-ACCESS-MODE`).
      The other five are **hints** about usage, and the processor **says** that it does not use them yet
      (`W-NOT-YET`). It does not silently ignore them.

(4) Not being enforced does not mean they may be written carelessly. A mode is a **claim** about what this op does,
      and the processor may someday question that claim.

## 6.4.4 How to write contracts

(1) Conditions are written in prefix notation. Comparison and logical operations, arithmetic, `len` and `idx` can be used.

(2) If there are several `requires`, **all** shall be true.

(3) A condition about **every element** of a slice is written with the following four. `elem_lt <slice> <value>`
      means *"every element is less than that value"*, and `elem_le` · `elem_gt` · `elem_ge` likewise.

(4) These four exist because a contract **cannot write going through one by one**. A condition is a pure
      expression and a loop is a statement, so to speak about all the elements there has to be a word that says it.

```lowent 예제: a contract
module ex_contract .

rem This op reads two bytes from a slice and makes one larger number.
export fn read_pair input data slice u8 . output u32 .
  requires ge len data. . 2 . .
  ensures le ret. 65535 . .
do
  let hi u32 widen u32 idx data. 0 . . .
  let lo u32 widen u32 idx data. 1 . . .
  return add mul hi. 256 . lo. . .
end .
```

> [!산문]
> How to read the example above: *"this function shall receive a row of bytes of length 2 or more (requires),
> promises that the value it returns is at most 65535 (ensures), and touches nothing in the outside
> world (effects none)."* — this much can be known without opening the body.

## 6.4.5 Giving a contract a name

(1) If several ops require the same condition, that condition can be given a name. It is written with `contract`.

(2) An op writes that name with `satisfies` and so takes on that contract.

```lowent 예제: a named contract
module ex_named .

rem If several ops require the same condition, that condition can be given a name.
contract nonneg do
  requires ge a. 1 . .
end .

fn half satisfies nonneg . input a u8 . output u8 .
do
  return div a. 2 . .
end .
```

> [!산문]
> If the same condition is repeated by hand in several places, one gets fixed and another forgotten.
> Giving it a name makes one place to fix.

## 6.4.6 Contracts remove checks

(1) The processor **takes contracts as facts** and narrows the range a value can have. If that range shows safety,
      it removes that check.

(2) The checks that can be removed are these — overflow, division by 0, narrowing, slice bounds.

(3) If a contract **is not enforced, it is not used as a fact.** That is, nothing is believed without confirmation.
      Believing without confirmation and removing checks is not being faster but being wrong.

(4) The processor can report where checks it could not remove remain. A performance model that does not report
      is itself knowledge outside the source (⟦§1.3⟧).

```lowent 예제: contracts remove checks
module ex_elim .

rem Without a contract, the overflow check remains.
export fn bare input a u8 . output u8 .
do
  return add a. 1 . .
end .

rem With a contract, the processor proves it and removes the check.
export fn proven input a u8 . output u8 .
  requires le a. 200 . .
do
  return add a. 1 . .
end .
```

> [!참고]
> The two ops have the same body. The only difference is one line of contract, and that one line removes one check during
> execution. This is the largest thing a contract does in this language.

```도해: a contract becomes a fact at entry and removes the overflow check
 proven 250  ──▶ requires le a 200   entry check fails → E-VM-CONTRACT (caller's fault)
 proven 150  ──▶ requires le a 200   passes → from here on a ∈ [0, 200] is a fact
                 add a 1             a + 1 ∈ [1, 201] ⊂ u8 [0, 255] → overflow check removed
 bare 255    ──▶ add a 1             no fact about a → the check stays → E-VM-OVERFLOW
```

## 6.4.7 The build mode decides the remaining checks


(1) The ⟦빌드 모드|build mode⟧ is written in the source as `build <mode> .`. A proved contract leaves no check in any
      mode. What the mode decides is the disposition of **contracts that could not be proved**.

(1a) The modes are **a closed four** — `debug` · `test` · `release_safe` · `release_fast`.
      Any other word is refused (`E-BUILD-MODE`).

(2) `debug` keeps the remaining checks and, if one breaks, stops saying what broke.

(2a) `test` keeps checks like `debug`. What runs tests (⟦§6.4.13⟧) is not this mode but **a request to run
      tests** — that request runs tests in any mode, and even in `test` mode an ordinary execution does not run tests.
      If an `expect` inside a test is false, that test fails (`E-TEST-FAIL`) — it is not the processor's
      error but **the claim of the person who wrote it being wrong**.

(3) `release_safe` keeps the remaining checks and stops if one breaks. What it reports when it stops is **a short fixed message** naming the kind of check that broke — the contract's text (the source
      expression) is not put in the executable. What differs from `debug` is that, as in (4), it removes contracts of `debug` grade.

(4) `release_fast` **removes** the checks of `requires` that could not be proved — both those written on the op and those of named contracts
      taken in with `satisfies`. `release_safe` removes the `requires` checks of `debug` grade. No mode removes the exit checks of
      `ensures`.

(4a) A contract whose checks the mode removed **is not a fact.** The processor does not remove other checks on the ground of that contract — the op
      runs **as if that contract had not been written**. So if a call that broke the contract overflows or goes out of range inside that op,
      the check of that computation stops it. This is for the same reason that `assume` is not a fact (⟦§6.4.9⟧).

(4b) The checks the processor puts at each computation — overflow · division by 0 · `MIN / -1` · indices · shift counts · narrowing · tags — are not removed
      in any mode. Those checks disappear only when they are proved.

(5) That is, `release_fast` is the same as opening one more **trust boundary** (⟦§4.4⟧) — a person promises that the contracts
      are true. If the promise is wrong, the program runs the same as a program written without the contracts.
      It does not go into undefined behaviour.

```lowent-거부: the build modes are a closed four · E-BUILD-MODE
module ex_build_mode .

build nosuchmode .

fn f output u8 . do return 1 . end .
```

## 6.4.8 Names and scopes

(1) Names nest in four layers — the module, the op, the block, and blocks inside it.

(2) In one scope a name points at exactly one entity. A name already alive **cannot be hidden** by declaring it
      again. Doing so is refused at translation.

(3) The things that cannot be hidden are these four — names in the module, the names of core ops (⟦§6.1.3⟧),
      the parameter names of the same op, and local names still alive in an outer block.

(4) Declaring the same name twice in the same block does not conform either. The second declaration **does not replace**
      the first — the same characters would merely point at two entities.

(5) On leaving a block, the names declared in that block disappear. So two side-by-side blocks each declaring the same name
      conforms — the two names are not alive at the same time.

```lowent-거부: a module name cannot be hidden · E-NAME-SHADOW
module ex_shadow .

let g u32 7 .

proc p output u32 . effects none .
do
  let g u32 1 .     rem hides the module's `g`
  return g. .
end .
```

```lowent-거부: a parameter cannot be hidden · E-NAME-SHADOW
module ex_shadow_param .

proc p input n u32 . output u32 . effects none .
do
  let n u32 1 .     rem hides the parameter `n`
  return n. .
end .
```

```lowent-거부: the same name cannot be declared twice in the same block · E-NAME-SHADOW
module ex_shadow_twice .

proc p output u32 . effects none .
do
  let a u32 1 .
  let a u32 2 .     rem does not replace the first
  return a. .
end .
```

```lowent-거부: an inner block cannot hide an outer name · E-NAME-SHADOW
module ex_shadow_inner .

proc p input n u32 . output u32 . effects none .
do
  let a u32 1 .
  guard gt n. 0 . else do
    let a u32 2 .   rem the outer `a` is still alive
    return a. .
  end .
  return a. .
end .
```

(6) The order of looking up names is local → module → imported modules. If imported names collide,
      which one is meant shall be written out.

## 6.4.9 Grades of contracts

(1) A ⟦등급|grade⟧ can be attached to one contract clause to fix **when and with what that condition is handled**
      — it comes directly after the clause word, as in `requires <grade> <condition> .`. The grades are the following three
      and no more.

> [!용어] 등급 (grade)
> A mark attached to one contract clause that fixes **when, and who, is responsible for** that condition. At translation
> the processor proves it (`static`), at run time it is checked (`debug`), or a person
> promises that it is true (`assume`).

> [!표] Grades of contracts — a closed set of three
> #table(columns: (auto, auto, 1fr), stroke: 0.5pt + rgb("#bbb"), inset: 6pt,
> [*Grade*], [*When it is looked at*], [*Meaning*],
> [`static`], [at translation], [the processor **shall prove it**. If it cannot, translation fails],
> [`debug`], [at run time], [looked at only while the build mode keeps checks],
> [`assume`], [**not looked at**], [only written down. The processor **does not use it as a fact**],
> )

(2) If no grade is written, the processor decides — it proves what it can prove, and what it cannot it keeps as the build
      mode (⟦§6.4.7⟧) decides. That is, a grade is **a mark that overturns the default disposition**, not
      something needed to write a contract.

(3) A condition that is `assume`d **is not used as a fact.** The processor neither keeps it nor relies on it
      to remove other checks — that is, that condition is **something said to the reader**.

(4) This is normative because the opposite does not hold. If a condition that is not kept were taken as a fact,
      then when that condition is false the processor would believe it is right **while having removed checks it should not have removed**.
      That is, to use a contract as a fact **it shall be enforced**, and what is not enforced is not a fact.

> [!표] The same condition, different dispositions
> #table(columns: (auto, auto, 1fr), stroke: 0.5pt + rgb("#bbb"), inset: 6pt,
> [*Written*], [*At entry*], [*Following checks*],
> [`requires le a 200`], [checked], [**removed** — because it became a fact],
> [`requires assume le a 200`], [not checked], [remain — because it is not a fact],
> )

> [!산문]
> Then what is `assume` for? It is the place to **write down** what the processor cannot prove yet but a person
> knows. Written down, the reader knows it, and when the processor grows later that place can be raised to
> `static`. Not written down, that knowledge stays only in a person's head.

```lowent 예제: the same program ends differently by mode — `debug` · 결과: bump(10) = 11 · bump(250) → E-VM-CONTRACT (trap)
module ex_mode_debug .

build debug .

fn bump input a u8 . output u8 .
  requires le a. 200 . .
do
  return add a. 1 . .
end .
```

```lowent 예제: the same program — `release_fast` · 결과: bump(10) = 11 · bump(250) = 251 (does not stop)
module ex_mode_fast .

build release_fast .

fn bump input a u8 . output u8 .
  requires le a. 200 . .
do
  return add a. 1 . .
end .
```

```lowent 예제: the same mode — where the contract kept the overflow away · 결과: bump(10) = 11 · bump(255) → E-VM-OVERFLOW (trap)
module ex_mode_fact .

build release_fast .

fn bump input a u8 . output u8 .
  requires le a. 254 . .
do
  return add a. 1 . .
end .
```

> [!주의] `release_fast` is outside the safe subset
> The same program ends differently by mode. Breaking the contract `requires le a. 200 . .` by calling
> with `a` as 250 stops in `debug` (`E-VM-CONTRACT`), and in `release_fast`
> **does not stop and gives 251.** In the third example the contract was the ground for proving that `add a. 1 .` does not overflow. When the mode
> removes the contract, that ground goes too, so the overflow check comes back, and `bump(255)` stops instead of giving 256 (⟦§6.4.7⟧ (4a)).
> This is one of the reasons this document limits its claim of having removed undefined behaviour to
> **the safe subset** (⟦§4.4⟧). Removing checks for speed is something that may be chosen, but
> **the fact of choosing shall remain in the source** — that is why the mode is not a command-line flag but a `build`
> statement.

## 6.4.10 Violations that can be decided now

(1) If a call site breaks the callee's `requires` and **both sides are constants**, that violation is decided
      **at translation**, without waiting for execution. It is then refused
      (`E-CONTRACT-IMPOSSIBLE`).

(2) This does not make contracts stricter; it brings forward **when the answer comes**.
      Before, it was translated green and caught a trap during execution.

> [!산문]
> If a contract whose answer comes at translation is postponed until execution, that program becomes a program that **is known
> to be wrong only by running it**. Programs carrying a bit budget that did not fit really were
> shipped. If the answer comes now, it is said now.

```lowent-거부: both sides are constants, so it is decided now · E-CONTRACT-IMPOSSIBLE
module ex_impossible .

fn g input a u8 . output u8 . requires lt a. 10 . .
do
  return a. .
end .

fn f output u8 .
do
  return g 200 . .       rem 200 is never less than 10
end .
```

## 6.4.11 What cannot be a name

(1) Apart from what cannot be hidden (⟦§6.4.8⟧), there are **characters that cannot be a name in the first place**.

> [!표] What cannot be used as a name
> #table(columns: (auto, auto, 1fr), stroke: 0.5pt + rgb("#bbb"), inset: 6pt,
> [*What*], [*Diagnostic*], [*Why*],
> [keywords], [`E-NAME-KEYWORD`], [a keyword is a mark by which the reader **tells construct from value at a glance**],
> [names of core ops], [`E-NAME-BUILTIN`], [the namespace **is flat** (no hiding), so that name would mean two things],
> [a lone `_`], [`E-NAME-WILDCARD`], [a `_` standing alone is a mark that means **anything**, not a name],
> [names beginning with `__`], [`E-NAME-VENDOR-RESERVED`], [a place **reserved** for those who make processors and libraries],
> [characters outside ASCII], [`E-NAME-ASCII`], [text · strings · `rem` comments hold any character, but **names are ASCII**],
> )

(1a) Here «core op» is not only those named in ⟦§6.3⟧. Anything **called by its bare name** belongs to that set —
      the table of core ops in annex D is all of it. The names of builtin ops that stand after `call_builtin` (files · the network · processes · standard input and output ·
      clocks · random numbers · the environment · terminals · floating-point functions …) are not included — they are in their own namespace (⟦§6.3.3⟧ (1f)).
      **That the names of core ops are taken is normative** — using one as a local name is refused.

> [!산문]
> It may look strange to rule «names in that list cannot be used» without writing the list.
> But the two questions are different — **what exists** is answered by the counting side (the ledger), and
> **whether it occupies the namespace** is answered by the norm. The former grows and shrinks, but the latter is
> always true, and **only what is always true is written in this document.**

(2) Two imported names **being bound to the same name** is refused too (`E-NAME-COLLISION`). Importing
      **does not overwrite** a name that already exists.

(3) In a dotted name `<front>.<back>`, the front shall be **a declared type** (`E-NAME-QUALIFIER`).
      That shape puts the op in the namespace of that type; it does not point at a module.

> [!산문]
> Why ASCII? This language lets text and comments hold any character while keeping **only names** narrow.
> Names are not only read by people but moved, attached and compared by tools, and characters that cannot be told apart by eye
> becoming different names makes **mistakes that cannot be found**.

> [!주의]
> What reserving `__` protects is not the processor but **the person writing**. Once it is fixed that that place always
> means «something internal», when reading someone else's code you know from the name alone that it is not yours.

## 6.4.12 What contracts refer to

(1) The names a contract clause calls shall be **visible** at that place. Calling a name that does not exist
      is refused (`E-REQ-UNDEF` · `E-ENS-UNDEF`).

(2) A contract that calls something the processor **cannot bring** to that place is refused too
      (`E-REQ-UNSUP` · `E-ENS-UNSUP`). It does not pretend to judge what it cannot judge.

(3) If a named contract (⟦§6.4.5⟧) points at something not declared, it is refused
      (`E-CONTRACT-UNDEF`).

(4) Declaring an error that **cannot** happen is refused (`E-CONTRACT-DEAD`) — if `requires`
      already filters out that case, that error branch never occurs.

(4a) If two `requires` on the same inputs **cannot be true together**, it is refused
      (`E-CONTRACT-UNSAT`) — whatever arguments it is called with, it stops at entry, so the body of that op never
      runs. The same place as (4): code that does not run is written in the signature.

> [!산문]
> What (4) prevents is not a wrong program but **a wrong picture**. If a failure that never occurs is written in
> the signature, the caller writes code to handle it, and that code never
> runs. Code that does not run is not tested, and untested code is wrong someday.

(4b) A contract clause the processor **cannot enforce** reports that fact (`W-CONTRACT-IGNORED`).
      The entry check (`requires`) and the exit check (`ensures`) each know certain shapes; other shapes are
      stopped by nobody during execution, and interval analysis does not learn from them either. Such a clause stands only in documentation.

> [!산문]
> (4b) pairs with (2). (2) refuses **what cannot be brought**, and (4b) reports **shapes that can be brought
> but cannot be set up**. Both keep the same one thing — a promise that is not checked is not left looking as if it were
> checked. On the `ensures` side this report did not exist until 2026-09-16, so such clauses were
> **silently discarded** (defect note #11).

(5) If the type passed to a translation-time parameter **does not provide what that place requires**,
      it is refused (`E-BOUND-UNSAT`, ⟦§6.11.2⟧).

## 6.4.13 Test blocks

> [!용어] 시험 블록 (test block)
> A body without inputs or outputs, written `test <name> do … end .`. It is executed only when a request to run tests is received.

> [!용어] 단언 (assertion)
> `expect <condition> .` — a condition a test claims to be true. If it is false, that test fails.

(1) Test blocks come at the top level of a module. The head has the name and, if present, a `schedule` clause (⟦§6.4.1⟧ (3d)); the body has
      the forms that come in the body of an op. The body is checked by **the same type rules** as the body of an op — being inside a test
      does not let `let x u8 300 .` pass.

(1a) The name of a test is in **the same namespace** as the names of ops · types · modules. If tests in one module, or a test and
      another declaration, use the same name, it is refused (`E-NAME-DUP`, ⟦§6.4.8⟧ (2)). Tests are reported by name and called
      by name, so if names overlap it cannot be said which is meant.

(2) The condition of `expect <condition> .` shall be `bool`. Giving a number is refused (`E-TYPE-COND`) — this language has no rule
      that reads non-zero numbers as true (⟦§6.2.16⟧ (4)).

(2a) If, when tests are run, the condition of an `expect` is false, that test fails (`E-TEST-FAIL`). This is not a contract violation
      — a contract violation is code breaking **its own promise**; a test failure is **the claim of the person who wrote it** differing
      from the code. The place to fix is different.

(2b) The processor does not remove `expect` in any build mode (⟦§6.4.7⟧). A vanished assertion is a test that did not run.

(2c) `expect` comes only inside test blocks. Writing it in the body of an op or anywhere else is refused (`E-EXPECT-PLACE`).
      Promises about inputs or results are written with `requires` · `ensures` (⟦§6.4.3⟧), and stopping deliberately with `panic` (⟦§7.1⟧).

(3) Tests are executed only when a request to run tests (`lowentc --test`) is received. Ordinary execution and checking (`--check`)
      do not run tests, and checking reports that fact (`W-TEST-NOT-RUN`) — so that passing the check is not
      read as passing the tests.

(3a) The tests of a unit that translation refuses do not run. A request to run tests also goes through the same checks as `--check` first.
      If a refused program reports «pass», that green is false.

(3b) The result of running tests is the number of tests run · the number passed · the number failed. If there are no tests at all, it says so.
      If even one test failed, that request ends in failure.

(4) A test with the clause `schedule explore_interleavings [limit <number>] .` is run repeatedly while changing **the order** in which messages are
      delivered. What changes is only the order ⟦§10.5⟧ (4b) leaves open — the order inside one actor is one (⟦§10.5⟧ (4a)).
      Without `limit` it runs up to a bound the processor decides. If it runs through every order before the bound, the result is reported
      as «exhaustive»; if it stops at the bound, as «up to the bound». Stopping at the bound is not reported as exhaustive.

(4a) If it passes in every order, the test passes. If it passes in some orders and fails in others, the
      test fails (`E-SCHED-NONDET`) — the result depends on the order of delivery, and the answer from running it once is only the answer for that one
      order. If it fails in every order, it is an ordinary test failure (`E-TEST-FAIL`).

```lowent 예제: test blocks and assertions
module ex_test .

fn clamp8 input v u64 . output u8 .
do
  return narrow_sat u8 v. . .
end .

test clamp_keeps_small do
  expect eq clamp8 7 . 7 . .
end .

test clamp_saturates do
  expect eq clamp8 1000 . 255 . .
end .
```

```lowent-거부: the condition of an assertion is bool — a number is not true · E-TYPE-COND
module ex_test_num .

test counts do
  expect 1 .
end .
```

```lowent-거부: two tests in one module shall have different names · E-NAME-DUP
module ex_test_dup .

test same do
  expect eq 1 1 . .
end .

test same do
  expect eq 2 2 . .
end .
```

```lowent-거부: tests and ops share one namespace · E-NAME-DUP
module ex_test_ns .

fn check output bool . do return true . end .

test check do
  expect check. .
end .
```

```lowent-거부: expect comes only inside test blocks · E-EXPECT-PLACE
module ex_expect_place .

fn half input n u8 . output u8 .
do
  expect lt n. 200 . .
  return div n. 2 . .
end .
```
