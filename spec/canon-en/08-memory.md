# 8 Memory and ownership

(1) This clause specifies **where values live and when they disappear**. Lowent has no garbage collector,
      and none of the mistakes that come from releasing by hand — in place of both there are translation-time rules.

## 8.1 Where values live

(1) **A stored value** lives in some ⟦영역|region⟧. The region is fixed together with the type and
      is written in the source.

(1a) Not every value has a region. A small value that exists only during a computation may have no place
      to be stored, and such a value has no region. A region says **where a value is stored**,
      not how a value exists.

(2) The regions of stored values are of three kinds.

> [!표] Where values live
> #table(columns: (auto, 1fr), stroke: 0.5pt + rgb("#bbb"), inset: 6pt,
> [*Kind*], [*Explanation*],
> [local], [born inside an op and gone when the op ends. The most common],
> [static], [exists for as long as the program lives],
> [obtained], [obtained from a root. Who gives it back is decided by ownership (#cref("8.4"))],
> )

(2a) There are **two** roots to obtain from. They are carved separately and rewound separately.

> [!표] The two roots
> #table(columns: (auto, auto, auto, 1fr), stroke: 0.5pt + rgb("#bbb"), inset: 6pt,
> [*Root*], [*Carved with*], [*Effect*], [*Property*],
> [fixed window], [`cap allocator` · regions other than `heap`], [`alloc`], [**does not grow.** When used up it gives `none`. On a machine without an operating system it lies between two bounds the linker gives — the size is not fixed into the executable, and that place is written when the device boots],
> [heap], [`cap heap` · `region <name> heap`], [`heap`], [**grows.** More is fetched during execution, and bytes already given are not moved. Exists only on machines with an operating system],
> )

(3) Which region a value lives in **is written in the source.** The processor does not move it secretly.

(3a) Apart from what is obtained from roots, the values that `some` · `ok` · `spawn actor` make live in a **finite pool** of the processor
      (the place and lifetime of element lists `lit array` · `lit slice` are specified by ⟦§6.2.6⟧ (7), and the place of struct value `lit`s by ⟦§6.2.6⟧ (7c-1) — they live in the op's
      frame, and when they go into the load of `some` · `ok` · a container · a channel · a letter · a task they are copied as values and live in the pool). This
      pool is the op's implicit ⟦frame⟧ — it has no effect, writes no `alloc` · `heap`, and needs no capability. The pool is rewound on every
      iteration of a loop, and if the values alive at the same time exceed the size of the pool, it **stops** on the spot (not a value but a halt).
      The size is decided by the machine (small on machines without an operating system), and the person building can adjust it. This size is not normative but a fact the processor
      states.

(3a-1) For each entry op, the processor counts at translation the **upper bound** of values the deepest call path can leave in the pool — it adds up the number of places
      on that path that make values (a place inside a loop counts one iteration's worth · struct values living in frames are not counted). For paths that cannot be counted (recursion · piling up in fields · loops that cannot be rewound),
      it states the reason instead of a number. If the counted bound is larger than that machine's pool, the program **does not conform** (`E-POOL-BOUND`). This count
      is shown by `--stack-report` (the same place as the frame count of ⟦§6.2.6⟧ (7c)).

(3b) A root's cursor is not shared between flows of execution. So an op spawned as a task cannot obtain from a root
      (`E-ALLOC-TASK`).

(3c) One allocator being **placed in two or more flows at the same time** at one place does not conform
      (`E-ALLOC-SHARED`) — handing it to two tasks, or handing it to a task while using it alongside. If its
      `reserve` moves the cursor atomically (the `atomic` effect), it is not so. Giving each task **its own
      allocator** conforms — there is one cursor each, so there is nothing to compete over.

> [!산문]
> Until 2026-09-16, (3c) blocked «handing an allocator to a task» itself. So doing exactly what the diagnostic
> recommended — «give each task its own allocator» — was refused with the same diagnostic (defect note
> #75). **If the way a diagnostic recommends is blocked, that diagnostic closes a way rather than opening one.**

> [!산문]
> What are commonly called «the stack» and «the heap» correspond to local and obtained. The names differ
> because in this language they are not machine structures but **properties of values**.

## 8.2 Places that receive regions

(1) Regions **are handed over as arguments**. It is written `input <name> region <type> . .`, and the type after it
      is the name by which that region is called.

(2) An op that obtains places from a region has the `alloc` effect (⟦§7.1⟧). That is, **there is no op that allocates
      secretly** — an op that uses places has that written in its contract.

(3) What is obtained from a region cannot outlive that region. When the region ends, what was obtained from it ends too.

(3a) A region is not given back piece by piece. It is collected **all at once** when the region ends.

```lowent 예제: receiving a region and obtaining places
module ex_region .

type scratch u64 .

proc build input temp region scratch . output u64 . effects alloc .
do
  let s stack u64 stack_new temp capacity 4 . .
  push s. 10 .
  push s. 20 .
  return 2 .
end .
```

> [!산문]
> Note that `effects alloc` is written. Obtaining places is **an effect**, so a pure
> op (`fn`, `effects none`) cannot use regions. It means that where memory comes from is written in the
> signature.

(4) The statement that opens a region writes its name together with its **kind**: `region <name> <kind> do … end .`.
      The kinds are a closed eight — `stack` · `frame` · `arena` · `static` · `heap` · `mmap` ·
      `disk` · `device`. Any other word does not conform (`E-REGION-KIND`).

(4a) **Carrying out** of a region a value obtained inside it does not conform
      (`E-REGION-ESCAPE`). When the region closes, that value no longer exists, so a name left outside points at
      what does not exist. The places of carrying out are `return`, assigning to a name outside the region, and assigning to
      **a field or element** of a name outside the region (`set field h. store . b. .`). Values that do not hold the region's bytes —
      those bound to scalar types like integers · truth values, and expressions that give scalars like `len` · `idx` —
      are not carried out. Whether something holds the region's bytes is decided **by following the flow**: the result of an op call holds
      only what belongs to the inputs that flow into the result in that op's body. If the result is an aggregate built with `lit`, each field is decided
      separately — taking out and carrying away a field that holds the region's bytes does not conform, and fields that do not hold them
      do not matter. If the flow cannot be traced, as when the body puts the result into a local and then returns it, it is regarded as coming from
      all the inputs.

```lowent-거부: putting a slice of the region into a field of an outer aggregate · E-REGION-ESCAPE
module ex_region_field .

struct holder do store mut slice u8 . end .

proc f output u64 . effects alloc . do
  var h lit holder do store subslice "abcd" 0 0 . . end . .
  region r arena do
    let g option mut slice u8 alloc_bytes r. capacity 16 . .
    if is_some g. . do
      let b mut slice u8 some_value g. . .
      set field h. store . b. .
    end .
  end .
  return len field h. store . . .
end .
```

(4b) Opening a region with a name that is not a region parameter of that op does not conform either
      (`E-REGION-UNDEF`).

(4c) **Carving with the name of an outer source** while a region of the same root (⟦§8.1⟧ (2a)) is open inside
      does not conform (`E-ALLOC-NESTED`). This holds for the name of an outer region block, for a region parameter, and for a capability of the same
      root (`cap allocator` for the fixed window · `cap heap` for the heap). Each root has one carving place, so
      when the inner region ends, those bytes are rewound too. A region of another root being open inside
      does not matter.

```도해: one cursor per root — when the inner region ends, the cursor is rewound
            ▼ where outer was opened
                        ▼ where inner was opened
            [ a ][ ··· ][ b ][ ✘ c ]
                        ▲ when inner ends, the cursor is rewound to here → c disappears with it
   a = outer's · b = inner's · c = carved with outer's name while inner was open (✘ E-ALLOC-NESTED)
```

```lowent-거부: carving from the outer region while the inner region is open · E-ALLOC-NESTED
module ex_region_nested .

proc f output u64 . effects alloc . do
  region outer arena do
    region inner arena do
      let g option mut slice u8 alloc_bytes outer. capacity 8 . .
    end .
  end .
  return 0 .
end .
```

(4d) Sending bytes obtained inside a region to **an actor born outside the region** does not conform
      (`E-ALLOC-OUTLIVES`). The actor outlives the region, and whether it keeps the slice it received is not visible at
      translation. Make the actor inside the region, or hand over memory that outlives the region.

(4e) On **every path** that leaves a region block — reaching the end, `return`, `break` · `continue` of a loop enclosing the region —
      that region is rewound. If the name of a region block is passed as an argument, the receiver takes it as a region parameter
      and carves from the same root.

(5) The kind decides **which root it carves from** (⟦§8.1⟧ (2a)): `heap` carves from the heap, and the other seven
      carve from and rewind the fixed window. Regions of the two roots can be opened overlapping within one op, and rewinding the inner region
      leaves outer values of the other root in place. The processor of this edition **does not treat** the seven other than `heap`
      **differently from one another** — those seven are words that tell the reader **where the storage comes from**,
      and having the processor distinguish them is the work of a later edition.

> [!참고]
> **Closing the vocabulary and separating behaviour are different things.** If any word could be used, that
> word would say nothing — because it was closed at eight, someone who reads `region t arena` knows
> *"it is bump allocation and is given back all at once"*. The distinction of behaviour goes as far as **the two roots** (fixed window · heap),
> and there is no distinction yet among the other seven. That fact is written down here.

## 8.3 Moving and copying

(1) When a value is put into another name, if that value has ownership it is **moved** (the original name can no longer
      be used). If it is a value without ownership, it is copied.

(2) Using the original name after it has been moved is refused at translation.

## 8.3.1 Where a value is put — temporaries and destinations

(1) Sometimes an expression that makes a value has **a place where it will be put** (a ⟦목적지|destination⟧). The initial value of `let`,
      the value of `return`, argument positions and the fields of aggregates are such.

(2) If all of the following three hold, that value **is made directly in the destination** — neither an intermediate temporary nor moving it
      happens.

> [!표] The three conditions for being made directly
> #table(columns: (auto, 1fr), stroke: 0.5pt + rgb("#bbb"), inset: 6pt,
> [*Condition*], [*Meaning*],
> [the destination **is empty**], [`let` initialisation · a field not yet filled · a place emptied by a move],
> [the value **does not fail midway**], [there is no path that leaves before reaching the destination],
> [the destination **belongs to that value alone**], [two values do not compete for the same place],
> )

(3) If the conditions do not hold, a ⟦임시값|temporary⟧ is made. A temporary is put in the smallest enclosing
      scope, and when that scope ends the temporaries disappear **in the reverse order of their making**.

(4) A reference pointing at a temporary cannot go outside that scope (⟦§8.4.1⟧).

(5) There is no separate operation that moves a value. Moving is **changing places**, not making
      a value.

> [!참고]
> Why write «made directly» as a norm? Left as *"do it if possible"*, the same source would
> copy on some processors and not on others — then **the cost would not be visible in the source**.
> The three conditions above are things a reader can count from the source alone, so when they hold one **may believe
> there is no copy**.

> [!참고]
> C++ solves this place with «value categories» and «move constructors». Lowent does not bring in that
> vocabulary — moving is **the business of places**, not of types.
> So there is no special declaration for moving, nor any rule that calls one.

## 8.4 References

(1) A ⟦참조|reference⟧ points at a value somewhere else. There are two kinds.

> [!용어] ref (shared reference)
> A read reference. Several can hold one at the same time. A value cannot be changed through it.

> [!용어] mut_ref (exclusive reference)
> A write reference. For one value there can be **only one at a time**, and meanwhile there can be no read references
> either.

(2) Violating this rule is refused at translation.

(3) A reference is not a value. Using a name that received a reference as it is in arithmetic or comparison does not
      conform (`E-TYPE-REFVAL`) — to use the value pointed at, take it out **in writing** with `deref`.

(4) This edition has no notation for borrowing a field of a struct separately. Writing a field borrow does not
      conform (`E-BORROW-FIELD`) — borrowing takes the whole value as its unit (⟦§8.12⟧).

```lowent-거부: using a reference like a value in arithmetic · E-TYPE-REFVAL
module ex_ref_value .

fn twice input p ref u64 . output u64 .
do
  return add p. p. . .    rem the value pointed at is taken out with `deref p`
end .
```

## 8.4.1 A reference cannot outlive what it points at

(1) A reference pointing at a local value remaining after that value has disappeared is called an ⟦탈출|escape⟧.
      Lowent **refuses escapes at translation time**.

(2) That is, a reference pointing at a value made inside an op cannot be returned outside that op.

```lowent 예제: several can hold read references together
module ex_ref .

export fn sum_two input a ref u32 . input b ref u32 . output u32 .
  requires le deref a. . 1000 . .
  requires le deref b. . 1000 . .
do
  return add deref a. . deref b. . . .
end .
```

```lowent-거부: a reference pointing at a local value cannot be returned · E-ESCAPE: reference to a local escapes the op (dangling)
module ex_escape .

export fn leak output ref u32 .
do
  let here u32 42 .
  return ref here . .     rem `here` disappears when this op ends
end .
```

> [!산문]
> If the code above were translated, the returned reference would point at **a value that has already disappeared**. If that reference
> were read, nobody knows what value would come out. So this language does not accept that code
> at all — it is known **at translation**, not by running it and finding out.

> [!주의] There are no null references
> A reference always points at a live value. If *"pointing at nothing"* has to be expressed,
> use `option` (⟦§6.2.8⟧) — then the processor enforces a check before taking it out.

> [!참고]
> The rule that separates many readers from one writer prevents two things at once. One is **another flow
> changing it at the same time**, and the other is **the value changing while being read**. Both are kinds of mistake
> that cannot be known by reading the source, so they are removed by rule.

## 8.5 Ownership and destroying

(1) ⟦소유|ownership⟧ says who is responsible for destroying a value.
      `owned t` is a value that has ownership.

(2) A value with ownership shall be destroyed **exactly once**. Destroying it twice, and discarding it without
      destroying it, are both refused at translation time.

```도해: ownership moves around — the responsibility to destroy is always in one place
 var h be owned buffer v .      h ──▶ [ buffer ]            responsibility: here
 consume h .                    h (empty name)  ──move──▶ h of consume ──▶ [ buffer ]   responsibility: consume
 drop h .   (inside consume)                                  [ buffer ] gone          responsibility: ended
 using h again                  E-OWN-MOVED                 (use after move)
```

(3) Destroying a value is of **two kinds**, and this language treats the two differently.

> [!용어] 해제 (release)
> Work that **always succeeds and does not wait**, like giving memory back. It has no failure, so
> it **may happen silently** where the value's lifetime ends. There is no failure to swallow.

> [!용어] 완결 (completion)
> Work that **can fail and can wait**, like closing a file · flushing a buffer. If it happened silently,
> **there would be no place to hand that failure to.** So the author shall always call it in writing.

(4) Whether a value requires completion is **decided by the program's own declarations** — if there is an op that takes that type
      by value and returns a `result`, that is the declaration *"ending this can fail"*.

(5) Discarding without calling it a value that needs completion does not conform. The processor issues a diagnostic
      (`E-OWN-INCOMPLETE`).

(6) Using a moved value again does not conform either (`E-OWN-MOVED`).

(7) When binding a value that needs completion, **`owned` shall be written in that place**
      (`E-OWN-BARE`). Without that word nothing requires ending, and what the type
      says and what the binding says part.

(8) At a place where branches split and meet again, the state of ownership **shall be the same on every path**
      (`E-OWN-JOIN`). If it is destroyed on one path and kept alive on another, **nobody can say** whether that value is alive
      after they meet.

(8a) A path that leaves a loop is a path too. A path that leaves a loop with `break` meets the ordinary end after that loop — if the value was moved
      on only one side, it is refused (`E-OWN-JOIN`). A `break` with a number meets after the loop that many levels outward. A `while` whose condition is
      `true` ends only with `break`, so there is no path to meet — the state of that path is the state after. Going to the next iteration with `continue`
      after moving a value is refused, since the next iteration meets the moved value again (`E-OWN-MOVED`).

(9) After moving one `owned` field of an aggregate, **moving the whole aggregate again** does not
      conform (`E-OWN-PARTIAL`). The receiver believes it received a whole aggregate, but one field inside it
      already belongs to someone else.

> [!산문]
> These three are all different faces of the same question — that *"who is responsible for destroying this value now"* shall
> **always have one answer**. If the answer differs by branch (8), or only part has moved (9),
> or the question was never asked (7), the end of that value becomes a place nobody is responsible for.

```lowent 예제: release — it cannot fail, so `drop` is enough
module ex_own .

type buffer u8 .

fn sink input h owned buffer . output u8 .
do
  drop h. .
  return 0 .
end .
```

```lowent-거부: it cannot be destroyed twice · E-OWN-MOVED
module ex_own_bad .

type buffer u8 .

fn twice input h owned buffer . output u8 .
do
  drop h. .
  drop h. .     rem destroys again what is already gone
  return 0 .
end .
```

> [!산문]
> It matters that there is only one criterion separating the two kinds — **can ending it fail.**
> If it cannot fail, no information is lost when the language cleans up by itself. If it can fail,
> someone has to receive that failure, and the only one who can is the author.

> [!참고]
> It is also worth noticing that no new word was made. Completion is **an ordinary op** — a function that takes an owned value
> and returns a `result`. That shape itself becomes the declaration, so there was no reason to add a word
> to the language.

> [!산문]
> Catching «forgot to destroy» at translation time is the core of this clause. It does not expect the operating system
> to clean up when the program ends, so the same code holds even in an environment without an operating system
> (⟦§5.1⟧).

> [!용어] 스스로를 품는 마디 (self-owning node)
> A struct one of whose fields is `option (owned <its own type>)`. That field is not a value but **one address slot** of another node built
> in allocator bytes, so the size is fixed, and nodes are linked to build trees.

(10) A ⟦스스로를 품는 마디|self-owning node⟧ is a struct with a field `<name> option (owned <itself>) .` (⟦§6.2.7⟧ (3b)).
      Nodes are built only in bytes an allocator gave — the binding's `use` chooses that allocator:
      `let n use al. option (owned node) lit node do … end . .`. If there is not enough room, the value is `none`.
      Nodes are treated like **values that need completion** — discarding an `owned node` · `option (owned node)` without moving or returning it
      (end of block · an early `return`), or overwriting a value still held, does not conform (`E-OWN-INCOMPLETE`).

(11) A node is given back only with `drop <name>. use <allocator>. .`. If that allocator gives back piece by piece (`freeing_allocator`),
      the bytes go back to it; otherwise the allocator keeps holding the bytes. If the name is an `option` and empty, it does nothing.
      `drop`ping a node without an allocator, or writing `use` on a value that is not a node, does not conform (`E-DROP-USE`).

(12) Giving back a node whose child fields still hold something does not conform (`E-OWN-NODE-KIDS`) — move the fields out first. If that node is inside an `option`
      and not visible at translation, execution stops on the spot (`E-VM-NODE-KIDS`). Deep trees are torn down with loops, not recursion.

(13) Taking a child out of a node borrowed with `ref` does not conform, and a child taken out of a node borrowed with `mut_ref` shall be put back before
      the op ends (`E-OWN-NODE-REF`) — the lender gets back a node with a hole. Only reading (`is_some field t. left .`)
      is not taking out. Moving as a whole a node whose child was taken out does not conform, as in (9) (`E-OWN-PARTIAL`).

```lowent 예제: a borrowed node is only read
module ex_node_read .

struct node do
  key u64 .
  left option (owned node) .
end .

fn has_left input t ref node . output bool . do
  return is_some field t. left . . .
end .
```

```lowent-거부: a node is given back to its allocator · E-DROP-USE
module ex_node_drop .

struct node do
  key u64 .
  left option (owned node) .
end .

fn lose input n owned node . output u64 . do
  drop n. .
  return 0 .
end .
```

> [!산문]
> Nodes do not hold their allocator because it would be waste — the head of a tree holds one allocator. So a node does not know «where it came from»,
> and the one who writes where to give it back is the author. If the node type is not exported from its module, the mistake of giving one tree's nodes
> back to another allocator is confined to that module (the standard library `tree`).

## 8.6 How far memory safety is guaranteed

(1) This document **writes the strength** of its guarantees **separately.** Otherwise it would make promises
      it could not keep.

> [!표] Strengths of guarantee
> #table(columns: (auto, 1fr), stroke: 0.5pt + rgb("#bbb"), inset: 6pt,
> [*Strength*], [*Meaning*],
> [proved], [there is a machine-checked proof],
> [static], [prevented by translation-time rules. There is no proof of the rules themselves yet],
> [dynamic], [checked during execution. Violations trap],
> )

(2) The exclusivity rule of references, the ban on escapes, and destroying owned values once are **static**.

(3) Slice bounds and contracts are **dynamic**, but checks disappear when the processor proves them (⟦§6.4.6⟧).

> [!주의] The word «safe» means something only when it says what is safe and how
> When a language claims *"memory safety"*, it matters whether that is prevented at translation time or checked during execution,
> and whether it is proved or only a rule. This document writes the strengths separately
> **so as not to make promises it cannot keep**.

## 8.7 Reading bytes again — `bit_cast` and `view`

(1) **Reading the same bytes as another type** is divided into two kinds in this language. They are divided
      because if that work were put off wholesale as «unsafe», nobody could say how far the harm
      spreads.

> [!표] The two kinds of reading bytes again
> #table(columns: (auto, auto, 1fr), stroke: 0.5pt + rgb("#bbb"), inset: 6pt,
> [*Kind*], [*op*], [*What it is*],
> [moving a value's bits as they are], [`bit_cast`], [between two scalars of the same width. The bit pattern is left as it is and **only the way of reading** changes],
> [laying a shape over bytes], [`view` · `try_view` · `view_array`], [**reads** a piece of bytes with the layout of some shape. It does not copy],
> )

## 8.7.1 `bit_cast` — the bits as they are, only the way of reading

(2) The target type of `bit_cast` shall be **a `plain` scalar**. `plain` means a type in which **every bit pattern
      is a useful value and there is no padding**.

(3) `bool` and ⟦열거|enum⟧ are **not** `plain` — because they have useless bit patterns (⟦덫 표현|trap
      representation⟧s). So these two are outside this kind and are refused
      (`E-TYPE-BITCAST`).

(3a) Source and target shall have **the same width**. If the widths differ it is refused at translation (`E-TYPE-BITCAST`) — nothing can say where the
      extra or missing bits come from. Change the width first with `widen` · `narrow`.

> [!주의]
> «Is it `plain`» shall be asked **directly**. A way that asks indirectly, treating what is not in the width table as not `plain`,
> silently flips the day the width table is filled. A verdict that held by coincidence disappears without a word
> when that coincidence disappears.

```lowent-거부: `bool` has trap representations, so it is not `plain` · E-TYPE-BITCAST
module ex_bitcast .

fn f input a u8 . output bool .
do
  return bit_cast bool a. . .     rem not every bit pattern is true or false
end .
```

## 8.7.2 `view` — laying a layout on

(4) `view` lays the layout of a shape over a piece of bytes. **It does not copy** — it reads the bytes in that place
      as they are.

(5) Two things shall fit for it to be laid on.

> [!표] The conditions for `view` to stand
> #table(columns: (auto, auto, 1fr), stroke: 0.5pt + rgb("#bbb"), inset: 6pt,
> [*Condition*], [*If violated*], [*Why*],
> [the piece is as long as the layout], [`E-VM-VIEW`], [reading missing bytes is exactly what this language prevents],
> [the start address keeps `align`], [`E-VM-ALIGN`], [`align n` is **a contract**, and this boundary is exactly where the contract is checked],
> )

(6) `try_view` does the same, but if it cannot stand it gives **absence instead of a lie** (⟦option⟧). That is,
      the failure comes back as a value.

(7) `view_array` lays **several** of the same shape on. The conditions are the same.

(7a) **Row views** — `view_array array m t b.` lays one row (`m × |t|` bytes) on as one element. The result is `slice array m t` (the same type as the sequence of rows of ⟦§6.2⟧ (9a)),
      and `idx g. r.` is a `slice t` that sees row r. The conditions are the same as (7) — if the length of `b` is not a multiple of the row bytes it stops (`E-VM-VIEW`),
      `t` shall be `plain` (`bool` is `E-TYPE-BITCAST`), and `m` is a literal of 1 or more. Whether it can be written follows `b`, and its source is `b` (§8 (8a)) —
      it does not copy, so the rules of invalidation · escape stand as those of `b`.

> [!산문]
> If alignment is treated as «a matter of speed», places that break it pass silently and one day collapse only on some
> machine. This language calls it **a contract** and checks it at the boundary — it refuses misaligned addresses for
> **exactly the same reason** it refuses short pieces. Both are «reading what is not there as if
> it were».

## 8.8 Changeability flows **only as written**

(1) Pieces and references are **read** by default. To change them, `mut` shall be written in that place.

> [!표] Places where changeability does not match
> #table(columns: (auto, 1fr), stroke: 0.5pt + rgb("#bbb"), inset: 6pt,
> [*Diagnostic*], [*What went wrong*],
> [`E-TYPE-MUT`], [writing to an element of a piece not written `mut`],
> [`E-TYPE-REF`], [writing through a shared read reference — to change it, it shall be `mut_ref`],
> [`E-TYPE-ARGMUT`], [passing a read-only value to a place that takes `mut` · `owned` · `mut_ref`],
> [`E-TYPE-RETMUT`], [writing that it gives something changeable and returning something read-only],
> )

(2) That is, changeability narrows **in one direction only**. Passing something changeable as read is
      allowed, but passing something read-only to a changeable place is not.

> [!산문]
> What this rule protects is **the reader's trust**. If I received a value as read, I shall be able to trust that it does not change
> while I am looking at it, and that trust stands only when nobody secretly obtains the right to write.
> So there is only one direction of narrowing.

## 8.9 Same name or not, types differ

(1) Even with the same representation, **different names are different types** (`E-TYPE-NOMINAL`). Those separated
      with `newtype`, and different integer types of the same size, are such.

(2) An undeclared type name is refused (`E-TYPE-UNDEF`).

(3) Integers and floating-point numbers **do not mix by themselves** (`E-TYPE-MIX`). To cross, use the op written
      for it.

(4) A type holding itself is refused (`E-TYPE-CYCLE`). Even when the processor cannot follow that recursion to the end,
      it **does not pass silently** but says so (`E-TYPE-CYCLE-LIMIT`).

```lowent-거부: integers and floating point do not mix by themselves · E-TYPE-MIX
module ex_mix .

fn f input a u32 . input b f64 . output f64 .
do
  return add a. b. . .     rem to cross, use the op written for it
end .
```

## 8.10 Storage the linker gives — `storage reserved`

(1) A `storage reserved .` block opens static storage **that the linker sets aside for that type**.
      It is not obtained by the program during execution; its place is already fixed when it is built.

(2) An op that opens this block shall have the following three.

> [!표] What `storage reserved` shall have
> #table(columns: (auto, auto, 1fr), stroke: 0.5pt + rgb("#bbb"), inset: 6pt,
> [*To have*], [*If missing*], [*Why*],
> [a capability], [`E-RESERVE-NOCAP`], [a place the linker hands over is also **handed over**, not picked up],
> [`effects state`], [`E-RESERVE-NOEFFECT`], [it is a place that remains while the program lives, so touching it is touching state],
> [the machine can give it], [`E-RESERVE-NOHOST`], [on an operating system, the linker does not give that place that way],
> )

## 8.11 Parentheses do not cross blocks

(1) An opened `(` shall be closed **within the same block**. If the block ends while it is unclosed, it is refused
      (`E-PAREN-ESCAPE`), and if it is never closed, it is refused (`E-PAREN-UNCLOSED`).

(2) A `)` without a partner, and a `)` that looks for its partner across a block boundary, are refused (`E-PAREN-STRAY`).

> [!산문]
> If parentheses could look for their partners across blocks, a source missing one parenthesis could be **read
> wholly with another meaning**. Then the processor would build a different program instead of reporting the mistake.
> If they cannot cross the boundary, an omission always shows as an omission.

```lowent-거부: the parenthesis is not closed before the block ends · E-PAREN-ESCAPE
module ex_paren .

fn f output u8 .
do
  return (add 1 2 .
end .
```

## 8.12 Exclusivity — many readers **or** one writer

(1) The ways that reach a value at one moment are **many reads** or **one write**, and those two overlapping
      does not conform (`E-EXCL`). If they overlapped, nobody could say **when** the value the reader sees
      changes.

```도해: borrows of one value n, in time order
 time →          ①        ②        ③        ④        ⑤
 ref r1 n     ├─────────────────┤                          two reads may overlap
 ref r2 n              ├─────────────────┤
 mut_ref w n                                   ├────────┤  a write only when alone
 ✘ ref r n    ├──────────────────────────┤
   set n 5                   ●                             the value changes while being read  E-EXCL
```

(2) What is borrowed **cannot outlive** the value it was borrowed from. If the lent value has been moved while the borrow
      remains, it does not conform (`E-EXCL-MOVED`).

(3) A borrow **cannot go outside** the block that opened it (`E-BORROW-ESCAPE`).

(4) Holding something borrowed from an actor and talking to that actor again does not conform
      (`E-BORROW-EXCL`) — while that actor changes its own state, we are looking at that state.

(5) A name bound with `let` **cannot be changed** (`E-IMMUTABLE`). Bind a name to be changed with `var`.

(6) One call passing the same storage to **two or more `mut` positions** does not conform
      (`E-EXCL`). There are two writers, so it violates (1) directly. Which write remains is decided not by the calling place but
      **by the order of the callee's statements**, and the caller's text has no way of knowing it.

(6a) Whether it is the same storage is decided not by name but **by place**: an alias of one name, overlapping pieces cut from the same storage, **the same field**
      of the same value, and **the whole** of a value **and its field** are the same storage. **Different fields** of one value (`field s. a .` and `field s. b .`) are different
      storage — passing a field to a `mut` position passes that field's place, so two different fields can be passed together to two write positions.

(7) Passing the same storage together to **one** `mut` position and a reading position (an in-place operation) conforms only when the callee has
      **declared** the pair of those two inputs with the clause `inplace <write input> <read input> .` (⟦§6.4.1⟧ (3a)) and the two storages are **the same range**
      (`E-EXCL-INPLACE`). The same range means the whole of the same storage, or pieces cut from the same storage with the same start and the same end.
      If the ranges overlap out of step or one is part of the other (**partial overlap**), it does not conform even with the declaration — a body that reads and writes
      element by element is correct only on the same range, and the caller cannot know where in the storage the callee writes.

(7a) The `inplace` clause writes **two** names: the first is a `mut` input of that op, the second another input of that op. One clause is written per pair.
      A mismatched shape is `E-INPLACE-FORM`. Whether that declaration **is true** — whether the body runs correctly on the same range — shall be shown by one of two
      shapes of the body: ⓐ it does not write to the write input up to the last statement that reads the read input, or ⓑ within that last statement
      it writes only with one index V as `set idx <write>. V. .` and reads only with the same index as `idx <read>. V. .`, and, inside a loop,
      does not read after writing in the same iteration (in place, element by element). Passing the same pair to an op that declared it `inplace` is left to that op's
      declaration. If it is not shown by either shape, it is `E-INPLACE-UNPROVEN`. This check is conservative — it may refuse a correct body
      but never admits a wrong one, and a refused body is rewritten as ⓐ or ⓑ.

(7b) When whether two ranges overlap cannot be decided at translation (when a cut end is not a constant), the processor asks (6) · (7) **during execution**. On entering,
      an op looks, for each pair of slice · view parameters — at least one of them `mut` — at whether the two pieces overlap by even one byte, and stops if they do
      (`E-VM-EXCL`; natively a panic with the same sentence). It passes if that op allowed the write · read pair with `inplace` and the two pieces are the same range.
      An empty piece overlaps nothing. Overlap is decided by byte places, so two windows with different element widths overlap too if they see the same bytes.
      Calls already decided at translation (constant ranges) are refused with the diagnostics of (6) · (7), and this check does not change the answers of calls that do not overlap.
      This check looks only at pieces that are parameters — pieces inside struct fields are not compared.
      The processor may drop the comparison of one pair from an op's entry. But only when every way of reaching that op within the translation unit is a call site (not reached
      through function values · actors · tasks), and **every** such site already shows that the two pieces cannot overlap. The shapes that show it are four:
      ⓐ two pieces cut from the same storage are empty, or the end of one piece does not pass the start of the other — an end is a constant, or an expression that reads, for the same
      run-time value `v`, as `k + m·v` (`k` · `m` constants, `m` ≥ 0) (from 0 up to before `k` and from `k` up to before `n` · from 0 up to before `n` and from `n` up to before `2n`). If that pair
      was allowed with `inplace`, it may be the same range. A piece seen as other elements with `view_array` is the same bytes. A piece cut again from a piece with a constant end (or that piece
      seen with `view_array`) lies **inside** that piece even if its end is unknown — `subslice` stops execution if the end is outside (`E-VM-BOUNDS`) — so
      if the two enclosing constant ranges split front and back, they do not overlap,
      ⓑ the two pieces are two inputs of the calling op (or pieces inside them), and the calling op has already compared that pair as not overlapping at its own entry
      (for a pair compared with `inplace`, only when both are passed as they are and the receiver also allows that pair with `inplace`),
      ⓒ one piece is a string literal (or a piece of one), or a name whose value is always a string literal — a writable place cannot point at read-only
      bytes (⟦§6.2.6⟧ (7b)),
      ⓓ one piece is a piece of a list the calling op built itself (a slot in the frame of ⟦§6.2.6⟧ (7) ⓐ) or of bytes that op obtained with `alloc_bytes` (its `some_value`),
      and the other piece is a list of another declaration · the answer of another `alloc_bytes`, or an input of the calling op — each declaration and each obtaining has its own bytes
      (a root does not give bytes it gave again, and rewinding happens only when a region ends — ⟦§8.1⟧ (2a) · ⟦§8.2⟧ (4c)), so they do not overlap each other, nor bytes that
      existed before the call. Taking the same answer out into two names gives the same bytes.
      A name that binds a piece with `let` is seen as the value of that `let` — only when that name and the names in the value are bound once in the op and not lent by reference
      (for an input, only when the body does not write it again). An entry from outside the translation unit (a name C
      calls) compares the dropped pairs at that entry. So a call that can overlap is compared and stopped whichever way it comes — dropping changes neither answers
      nor stops.

> [!산문]
> Until 2026-11-04, (7b) was «not asked». So `wr2 (subslice s 0 k) (subslice s 0 k)` passed the check and the later write
> overwrote the earlier one (external report REQ-0036). Of three options — treating the same name and same-number ends as the same range at translation, writing it down as a limit and allowing it,
> or comparing during execution — the owner chose the third (2026-11-04). When comparing began, three calls in the corpus stopped: two that passed a whole buffer and a window cut inside it with `view_array`
> to writing places (the static check could not follow that alias), and one where the wrapper of an op that declared it takes the same range had not copied over
> that permission (`inplace`). All three were fixed to cut the storage without overlap or to write the permission.

> [!산문]
> Until 2026-09-21, (7) was **the author's duty** for «ops that write only after they have finished reading», and the tool did not look (RFC-0115 §8-15 ⓒ).
> So violating calls showed up not as diagnostics but as wrong answers. Now the permission **is written in the op's head** and can be asked
> at the one place of the caller (RFC-0116 D2 B1, owner decision ⓑ of 2026-09-22). Partial overlaps the old rule allowed are blocked by the new rule — the seven such calls
> in the corpus were fixed to cut the storage without overlap.

```lowent-거부: passing the same storage as write and read together to an op that did not declare it · E-EXCL-INPLACE
module ex_inplace_undeclared .

proc scale input o mut slice u64 . input a slice u64 . output u64 . effects none . do
  set idx o. 0 . mul idx a. 0 . 2 . .
  return 1 .
end .

proc f input w mut slice u64 . output u64 . effects none . do
  return scale w. w. . .
end .
```

```lowent 예제: an in-place call of an op that allowed the same range
module ex_inplace_ok .

proc scale input o mut slice u64 . input a slice u64 . output u64 . effects none . inplace o a . do
  set idx o. 0 . mul idx a. 0 . 2 . .
  return 1 .
end .

proc f input w mut slice u64 . output u64 . effects none . do
  return scale subslice w. 0 4 . subslice w. 0 4 . . .
end .
```

```lowent-거부: writing inplace on a body that reads after writing on the same range · E-INPLACE-UNPROVEN
module ex_inplace_unproven .

proc late_read input o mut slice u64 . input a slice u64 . output u64 . effects none . inplace o a . do
  set idx o. 0 . 1 .
  return idx a. 0 . .
end .
```

(8) An op can write the clause `invalidates <input> .` (⟦§6.4.1⟧ (3a)) in its head to declare that it **makes views that came from that input's storage invalid** —
      giving a block back, moving to another place while growing, and rewinding are such. It writes **one** name, and it shall be an input of that op
      (`E-INVALIDATES-FORM`).

(8a) Within the body of one op, a view (a local or borrowed name whose type contains `slice`) carries as its **source** the storage that appeared in its initialiser — making a view from a view
      continues the source. Using that view after passing that storage to an op that declared `invalidates` does not conform (`E-VIEW-INVALIDATED`).
      Reassigning the name as a whole revives it with a new source. If it becomes invalid on **any one branch**, it is invalid after they join, and in a loop the invalidity continues to the head
      of the next iteration. **Values** such as handles and numbers are not views.

(8b) The sources of views **are lumped together** — the result of a call is regarded as coming from all its arguments. So it may refuse safe uses but never misses an invalidated
      view. Op boundaries are crossed like this:
      - Even an op that does not write `invalidates` is regarded as an op that invalidates an input if, in its body, it passes that input to an op that declared `invalidates` (including ops so inferred)
        — invalidation does not disappear by going through a wrapping op.
      - If a view is put into a field of an aggregate (`set field h. f . <view> .`), that field holds the view. Reading and using that field after the field's source becomes invalid is
        `E-VIEW-INVALIDATED`, and reassigning the field revives it.

(8c) Two `mut` views obtained by calling an op without side effects (`effects none`) twice **with the same arguments** are regarded as the same storage. Keeping both alive outside a `borrow`
      and using them violates (6) (`E-EXCL`) — taking out two write views with the same handle of a pool is such. If the arguments are not names or constants,
      whether they are the same is not asked.

```lowent-거부: using an old view after an op that moves while growing · E-VIEW-INVALIDATED
module ex_view_invalidated .

struct buf do
  data mut slice u8 .
  n u64 .
end .

proc grow input b mut buf . output u64 . effects none . invalidates b .
do
  set field b. n . add field b. n . 1 . .
  return 1 .
end .

proc view_of input b buf . output slice u8 . effects none .
do
  return field b. data . .
end .

proc f input b mut buf . output u64 . effects none .
do
  let v slice u8 view_of b. . .
  let r u64 grow b. . .
  return len v. . .       rem `grow` invalidated the views of `b` — take the view again
end .
```

> [!산문]
> What this rule protects is the same as ⟦§8.8⟧ — **the reader's trust**. But where §8.8 decides «can it be written» by type,
> this one decides «who is here together right now».
> There are places where, even with the types right, things must not be together, and seeing them is the exclusivity rule.

> [!주의]
> `mut ref slice` is not used (`E-MREF-SLICE`) — `mut slice` already lets its elements be changed,
> so the former **gives nothing the latter does not.** It is a second spelling that says
> the same thing.

```lowent-거부: a name bound with `let` cannot be changed · E-IMMUTABLE
module ex_immutable .

fn f output u8 .
do
  let a u8 1 .
  set a. 2 .            rem to change it, it shall be bound with `var`
  return a. .
end .
```

## 8.13 Allocators — capability · policy · state

> [!용어] 얼로케이터 (allocator)
> A value that **hands out small places** from bytes received from a root or borrowed. In this language it is an actor that provides the trait
> `byte_allocator`.

(1) What hands out places over a root (⟦§8.1⟧ (2a)) is called an ⟦얼로케이터|allocator⟧. An allocator is not
      a special device of the language but **an actor that provides the trait `byte_allocator`**. There are three things in that word.

> [!표] The three that make up an allocator
> #table(columns: (auto, auto, 1fr), stroke: 0.5pt + rgb("#bbb"), inset: 6pt,
> [*Layer*], [*When it exists*], [*What it is*],
> [capability], [only at translation], [whether a root may be reached — `cap allocator` · `cap heap` (⟦§7.2⟧)],
> [policy], [at translation (the type)], [by which rule it carves — **a type** that provides `byte_allocator`],
> [state], [during execution], [the cursor · backing bytes — **a value** of that type],
> )

(2) Capability fields (`root cap heap .` · `root cap allocator .`) can be put in an actor's state. Calling
      `alloc_bytes` with that field carves from that capability's root. A capability is not a run-time value, so that field has no size.

(3) An actor with capability fields can be spawned only in **an op where a capability of the same kind is visible** — one that received that capability as an input, or an op of an actor that itself has a field of the same
      kind (`E-CAP-FORGE`). This is so that a way of reaching a root in one line cannot be made up in a place without
      the capability.

(4) On a machine without an operating system, actors with a `cap heap` field and ops of actors that write the `heap` effect are refused
      (`E-HEAP-NOHOST`) — except that actors spawned nowhere in that unit are not asked about.

(5) The standard library gives two default allocators in this shape: `allocs.fixed_bytes` (fixed window) ·
      `allocs.heap_bytes` (heap). So default allocators and allocators the user built go into **the same place** —
      the receiving side (`input comptime a type . input al a . requires allocs.byte_allocator a. . .`) does not tell the two
      apart.

(6) The `heap` effect means only «the root grows». **Giving places back piece by piece** is the work of policy, and such an allocator
      provides the trait `freeing_allocator` — in addition to the ops of `byte_allocator`, `release` takes a piece and answers with a truth value whether it took it
      back. So even on a machine without an operating system, an allocator that gives back piece by piece over a fixed window can be used.

(7) When an allocator asks «is this the piece I gave last», it asks **by identity, not by length**: `grow` and `release`
      take the piece itself, and the core op `same_slice a. b. .` checks whether two slices start at the same place and have the same length. Recognising by length alone
      would grow or take back someone else's piece of the same length. `same_slice` does not expose addresses — the answer is a truth value.

```lowent-거부: spawning an actor with a capability field without the capability · E-CAP-FORGE
module ex_cap_forge .

actor grower do
  state do
    root cap heap .
  end .
  proc take input n u64 . output u64 . effects heap . do
    let g option mut slice u8 alloc_bytes root. capacity n. . .
    if is_some g. . do return n. . end .
    return 0 .
  end .
end .

proc f output u64 . effects heap state . do
  var g grower spawn actor grower . . rem there is no `input h cap heap .`
  return send g. take 8 . .
end .
```

## 8.14 Choosing an allocator per object — the `use` clause of an op head and the `use` · `keep` of bindings

(1) An op can write the clause `use <name> <type> .` in its head to declare **the allocator it carves from**.
      `<type>` is a type that provides `byte_allocator`, or a comptime type parameter with such a bound (`requires allocs.byte_allocator a. . .`).
      In the body, `<name>` is an ordinary name.

(1a) An op writes **only one** `use` clause (`E-USING-DUP`). The clause is two words, a name and a type. The allocator clause of a binding is written
      directly after the name as `use <source>` (gives back at the end of the block) or `keep <source>` (does not give back) (⟦§6.5.1⟧ (3c)) —
      a mismatched shape is `E-USING-FORM`.

(1b) The receiving side and the choosing side use **one word** — `use <name> <type> .` in an op head is «this op carves here», and `use <source>.`
      of a binding is «this object with this». The `use` of a head is told apart from importing (⟦§6.10⟧) by place: importing stands outside declarations,
      and this clause stands between the op's name and `do`. A head clause has no distinction between giving back and carrying out — `keep` stands only on bindings.
      The old spelling `using <name> <type> .` is refused (`E-USING-OLD`).

(2) The `use` clause **is not an input.** The caller does not write that allocator positionally, and if `<type>` is a type
      parameter, it does not write that type argument either — it comes from the allocator's type.

(3) The caller chooses the allocator at the binding: `let <name> use <source> <type> <expr> .`. The chosen source goes into the
      **head call** of that initialiser. If the head call is an op without a `use` clause, it does not conform
      (`E-ALLOC-USING-UNUSED`) — an object that already holds its own allocator does not take the caller's choice.
      If the initialiser is a `lit`, its bytes are received from the chosen source (⟦§6.2.6⟧ (7e)). A self-owning node (⟦§8.5⟧ (10)) is bound as `owned`,
      so it is not given back at the end of the block — the responsibility for giving it back is carried by that value, and the `drop … use` of ⟦§8.5⟧ (11) gives it back.

(4) If nothing is chosen, the default is decided in the following order. The first that fits is used.

> [!표] The order that decides the default
> #table(columns: (auto, 1fr), stroke: 0.5pt + rgb("#bbb"), inset: 6pt,
> [*Order*], [*Source*],
> [1], [the binding's `use <source>` · `keep <source>`],
> [2], [the name of this op's own `use` clause (if the type fits)],
> [3], [the **only** one among this op's inputs · bindings whose type fits],
> [none], [`E-ALLOC-NOSOURCE` — no allocator is visible],
> [two or more], [`E-ALLOC-AMBIGUOUS` — it does not guess. Write it in the binding],
> )

(5) The default **does not cross op boundaries.** The source comes only from that op's signature and body, and the caller's allocator
      does not flow in by itself. Fields are not counted as sources either. That is, **there is no global allocator.**

(5a) The choice of allocator is resolved **on the tree** — after the processor has stood forms up by arity, before monomorphisation. If checked in a comparison
      mode that does not build the tree, unresolved clauses remain, and the processor does not silently check them as an op short of one input but
      says so with `E-USING-UNRESOLVED`.

(6) The root operand can be left out, as in `alloc_bytes capacity <n>`, only **inside a region block**, and then the innermost
      region goes into that place. Capabilities (`cap allocator` · `cap heap`) cannot be left out and are written by name — so as not to make a way of
      reaching a root without a name. An `alloc_bytes` that points at no root — left out outside a region, or naming something that is none of a capability input ·
      a region parameter · an open region block · a capability field — does not conform (`E-ALLOC-NOROOT`).
      The root of `stack_new` is a region parameter and cannot be left out (the same code).

(6a) The size is written after the `capacity` marker: `alloc_bytes <root> capacity <n>` · `stack_new <region> capacity <n>`.
      Without the marker it does not conform (`E-ALLOC-CAPACITY-MARK`) — that marker is what separates the shortened form from the form with a name.

> [!산문]
> An allocator is a value — the cursor and the backing bytes have to live somewhere during execution. The `use` of an op head moves the place where that value
> is written **without hiding it**: the receiving side writes «I carve from this allocator» in its signature, and the caller writes «with this» on the line
> that makes the object. The machine code between the two is the same as passing one argument.

```lowent 예제: alternating between two allocators in one op
module ex_using .

trait carver do
  reserve input s self . input n u64 . output u64 . effects state .
end .

rem Two policies — one counts what is asked, the other twice as much
actor exact do
  satisfies carver .
  state do
    used u64 .
  end .
  proc reserve input n u64 . output u64 . effects state . do
    set used. add used. n. . .
    return used. .
  end .
end .

actor doubled do
  satisfies carver .
  state do
    used u64 .
  end .
  proc reserve input n u64 . output u64 . effects state . do
    set used. add used. mul n. 2 . . .
    return used. .
  end .
end .

rem The receiving side — the caller writes neither the allocator nor its type
proc take input comptime a type . use al a . input n u64 . output u64 . effects state . requires carver a. . . do
  return send al. reserve n. . .
end .

proc main output u8 . effects state . do
  var e exact spawn actor exact . .
  var d doubled spawn actor doubled . .
  let x use e. u64 take 3 . .      rem 3
  let y use d. u64 take 3 . .      rem 6
  return narrow u8 add x. y. . . .
end .
```

```lowent-거부: the allocator clause of an op head is `use` — tests (1b) · E-USING-OLD
module ex_using_head_old .

trait carver do
  reserve input s self . input n u64 . output u64 . effects state .
end .

proc take input comptime a type . using al a . input n u64 . output u64 . effects state . requires carver a. . . do
  return send al. reserve n. . .
end .
```

```lowent-거부: two allocators are visible and none was chosen · E-ALLOC-AMBIGUOUS
module ex_using_ambiguous .

trait carver do
  reserve input s self . input n u64 . output u64 . effects state .
end .

actor exact do
  satisfies carver .
  state do
    used u64 .
  end .
  proc reserve input n u64 . output u64 . effects state . do
    set used. add used. n. . .
    return used. .
  end .
end .

proc take input comptime a type . use al a . input n u64 . output u64 . effects state . requires carver a. . . do
  return send al. reserve n. . .
end .

proc main output u8 . effects state . do
  var e exact spawn actor exact . .
  var f exact spawn actor exact . .
  let x u64 take 3 . .              rem e or f? — it does not guess
  return narrow u8 x. . .
end .
```
