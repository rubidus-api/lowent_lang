## 6.2 Types

(1) Every value has exactly one ⟦타입|type⟧. A type fixes both what the value can be and
      what can be done with it.

(2) Every type in Lowent **has a fixed size and representation.** There is no place where the processor decides at will
      according to the machine.

(2a) The processor decides the type of the value of every expression. If a value whose type the processor cannot decide is put in a place where a type is written — a bound name · an assignment · an op's arguments and return · the elements of a row ·
      the fields of an aggregate — translation is refused (`E-TYPE-UNKNOWN`). A value whose type is not decided
      cannot be checked, so such a place is not let through as «correct».

```lowent-거부: binding a word that gives no value to a number · E-TYPE-UNKNOWN
module ex_type_unknown .

fn f input s mut slice u64 . output u64 . do
  let x u64 swap s. 0 1 . .
  return x. .
end .
```

> [!산문]
> For those who know C: C's `int` may differ in size from machine to machine. Lowent has no such type
> — `u32` is 32 bits everywhere. So there is never a need to ask *"how many bytes is it on this machine?"*.

## 6.2.1 Kinds of type

(1) Types are divided into the following kinds.

> [!표] Kinds of type
> #table(columns: (auto, 1fr), stroke: 0.5pt + rgb("#bbb"), inset: 6pt,
> [*Kind*], [*What it is*],
> [integer], [`u8` `u16` `u32` `u64` `usize` `i8` `i16` `i32` `i64` `idiff`],
> [floating point], [`f32` `f64`],
> [truth value], [`bool`],
> [aggregate], [`struct` (a collection of named fields) · `enum` (one of several)],
> [row], [`array n t` (a row of fixed length) · `slice t` (a range that carries its length) · `matrix k d… t` (a row of several dimensions of fixed size)],
> [answer], [`result t e` (a success value or an error) · `option t` (a value or none)],
> [pointing], [`ref t` (read reference) · `mut_ref t` (write reference) · `owned t` (ownership)],
> [capability], [`cap k` (the right to produce an effect, #cref("7.2"))],
> [flow], [`task t` (the handle of a flow — `t` is the type of the value the flow will give, #cref("10.3")) · `chan t` (a channel — `t` is the type of the values it holds, #cref("10.6"))],
> )

> [!참고]
> There is **no pointer** in this list. `ref` · `slice` · `owned` divide among them the work a pointer used to do —
> *"what does it point at"*, *"how many are there"* and *"who is responsible for destroying it"* are different
> questions, and if one pointer tries to answer all three, none of them can be checked.

## 6.2.2 Integer types

(1) The name of an integer type is made of a sign and a width. `u` means unsigned, `i` signed,
      and the number after it is the width in bits.

(2) Integers are represented in two's complement (⟦§5.3⟧).

(3) `usize` and `idiff` are integers wide enough to count the **sizes and indices** of the address space. Even if they have the same width as
      `u64` · `i64`, they are **different types**, and they do not convert into each other automatically. `usize` is the type for lengths and indices,
      `idiff` the type for **the difference between two positions**.

(3-1) `usize` and `idiff` have the same width, and it is **chosen by the implementation** (implementation-defined). That width is 16 bits or more and
      does not exceed the width of the widest integer type the implementation has. The implementation documents the width. `usize` is unsigned and `idiff` a
      two's-complement signed integer, so the positive numbers `idiff` holds are half the range `usize` holds.

(3-2) `usize` · `idiff` **do not mix with any other integer type.** The widening table (⟦§6.2.5⟧ (4)) does not include them —
      there is no automatic conversion from `u8` to `usize`, nor from `usize` to `u64`. If only one of the two operands of an arithmetic operation or comparison is `usize` (or
      `idiff`), translation is refused (`E-TYPE-NOMINAL`). The same holds between `usize` and `idiff`. Number literals follow the type of their place and so
      are not caught by this rule.

(3-3) A conversion between `usize` · `idiff` and other integer types **is written with `cast`** (⟦§6.2.16⟧ (2)). `cast` gives the value if it fits the target type
      and traps if it does not. `widen` and `narrow` are refused at translation when the source or the target is `usize` · `idiff`
      (`E-TYPE-NOMINAL`) — whether that conversion widens or narrows depends on the implementation's width, so a word that states a direction cannot write
      text that is true on every implementation. `narrow_try` · `narrow_sat` · `narrow_wrap`, whose names choose what happens when the value does not fit,
      accept `usize` · `idiff` as targets.

(3-4) If a literal does not fit the implementation's `usize` (or `idiff`), translation is refused (`E-TYPE-WIDTH`). This is the only place **among integer conversions**
      where the verdict on the same text depends on the implementation — the value has a size not in that implementation's address space, so it is refused earlier
      than a trap during execution. Refusals of text that uses what a target lacks (⟦§5.6⟧ (4) · (4-2) — heap · floating point · 128-bit integers · atomic widths) are outside this sentence.

> [!주의] Text that does not rely on the width of `usize`
> For the same text to stand on 16 · 32 · 64-bit implementations alike — ① write only lengths · indices · byte counts as `usize`, and keep positions inside files · times ·
> hashes · protocol fields in types of fixed width. ② Compute where the two meet **on the wider side** (`add off. cast u64 len b. . . .`).
> ③ When moving a number from outside into `usize`, take it with `narrow_try`, so that the program refuses what does not fit. ④ Arithmetic on `usize`
> overflows at its width (⟦§6.3⟧), so places that multiply sizes take `chk_mul`. ⑤ A `usize` field of a struct differs in size between implementations,
> so it is not put in a layout that goes outside. ⑥ When taking the difference between two positions as `idiff`, `cast idiff` traps in the upper half of the `usize`
> range.

```lowent 예제: between `usize` and other integers, `cast`
module ex_usize .

fn bucket input xs slice u8 . input h u64 . output u8 .
  requires gt len xs. . 0 . .
do
  let slot u64 mod h. cast u64 len xs. . . . .
  return idx xs. cast usize slot. . . .
end .

fn checked input want u64 . output usize . do
  let p option usize narrow_try usize want. . .
  guard is_some p. . else return 0 . .
  return some_value p. . .
end .
```

```lowent-거부: `usize` and `u64` mixed as they are · E-TYPE-NOMINAL
module ex_usize_mix .

fn end_of input start usize . input more u64 . output u64 . do
  return add start. more. . .
end .
```

```lowent-거부: `usize` moved with `widen` · E-TYPE-NOMINAL
module ex_usize_widen .

fn as_u64 input n usize . output u64 . do
  return widen u64 n. . .
end .
```

(3b) `u128` · `i128` are integers of width 128. They stand **only on targets of implementations that have 128-bit integers** — using these words on other
      targets is refused at translation (`E-INT128-TARGET`). The implementation documents the targets on which they stand.

(3b-1) From integers of other widths to a 128-bit place **there is no automatic widening** — write `widen u128 …` · `widen i128 …` (`E-TYPE-WIDTH`).
      The two operands of arithmetic · comparison · bit operations are both 128 bits or neither is. Number literals follow the type of their place — a literal
      beyond 64 bits fits only in a 128-bit place, and in other places translation is refused (`E-LIT-RANGE`, ⟦§6.1⟧).

(3b-2) 128-bit integers stand in scalar places (locals · inputs · results · the contents of `option` · `result`) and also in struct fields and as the elements of slices · arrays · views ·
      stacks. Their byte layout is the same as the target's C compiler gives 128-bit integers — size 16 bytes, alignment 16 bytes,
      least significant end first (a field written `big` reverses it). Putting them as elements of vectors (`vec` · `matrix`) or at the `extern` boundary is refused at translation
      (`E-INT128-PLACE`) — there is no vector unit with 128-bit integer lanes, and the ABI of the C boundary has not yet been opened.

(3b-3) A 128-bit input taken from the command line is the decimal number as written. A number outside the range of the type (a negative number or 2#super[128] or more for `u128`, outside
      ±2#super[127] for `i128`) is refused, not truncated — the same as the type range check at the entry boundary.

```lowent 예제: the whole product of two 64-bit numbers in 128 bits
module ex_u128 .

fn wide_mul input a u64 . input b u64 . output u128 . do
  let x u128 widen u128 a. . .
  let y u128 widen u128 b. . .
  return mul x. y. . .
end .

fn top_half input a u64 . input b u64 . output u64 . do
  return cast u64 shr wide_mul a. b. . 64 . . .
end .
```

```lowent 예제: a 128-bit integer in a struct field — the layout is the same as C's
module ex_u128_field .

struct acc do
  lo u64 .
  big u128 .
end .

fn acc_bytes output usize . do
  return size_of acc . .
end .
```

```lowent-거부: a 128-bit integer put at the C boundary · E-INT128-PLACE
module ex_u128_extern .

export extern fn take input a u128 . output u64 . do
  return 0 .
end .
```

(3a) `usize` **is not a type that holds pointers.** Its width follows the address width, but a pointer value may be larger than an address (⟦§5.3⟧ (4a))
      and cannot be brought back from an integer. Use `usize` for sizes · lengths · indices, and write what points as slices and references.

> [!표] Ranges of the integer types
> #table(columns: (auto, auto, 1fr), stroke: 0.5pt + rgb("#bbb"), inset: 6pt,
> [*Type*], [*Width*], [*Range*],
> [`u8`], [8], [0 … 255],
> [`u16`], [16], [0 … 65 535],
> [`u32`], [32], [0 … 4 294 967 295],
> [`u64`], [64], [0 … 2#super[64]−1],
> [`i8`], [8], [−128 … 127],
> [`i16`], [16], [−32 768 … 32 767],
> [`i32`], [32], [−2 147 483 648 … 2 147 483 647],
> [`i64`], [64], [−2#super[63] … 2#super[63]−1],
> [`u128`], [128 (depending on the target — (3b))], [0 … 2#super[128]−1],
> [`i128`], [128 (depending on the target — (3b))], [−2#super[127] … 2#super[127]−1],
> [`usize`/`idiff`], [16 or more (implementation-defined)], [follows the width — (3-1)],
> )

## 6.2.3 The truth-value type

(1) `bool` has only two values, true and false. Its representation is 1 byte, and the only valid values are 0 and 1.

(2) `bool` and integers **do not convert into each other.** An integer cannot be used in a condition position, and a `bool` cannot
      be counted as a number.

```lowent-거부: an integer cannot be used in a condition position · E-TYPE-COND
module ex_cond .

proc p input n u32 . output u8 . effects none .
do
  if n. do return 1 . end .     rem `n` is not a bool
  return 0 .
end .
```

> [!주의] 0 is not false
> C-family languages treat 0 as false and everything else as true. Lowent does not.
> Using an integer in a condition, as in `if n. do`, is refused at translation — **what is being asked** shall be written,
> as in `if gt n. 0 . do`. If what is asked is not written in the source, the reader has to guess it.

## 6.2.4 Floating-point types

(1) `f32` and `f64` are the IEEE 754 binary 32-bit and 64-bit formats respectively.

(2) Floating-point operations happen **at the width of their operands**. Operations between `f32`s are computed as `f32`,
      and the processor does not secretly compute in wider precision.

(2a) One operation is **one rounding**. The processor does not merge two operations into one rounding — it does not turn a multiply-and-add (`add mul a. b. . c.`)
      into one fused multiply-add instruction (FMA). Rounding is the single mode «to nearest, ties to even», and a program cannot change it.
      Code emitted as C carries this promise in its source too — it writes directives so that the C compiler building that code does not merge them itself.

(3) Integers and floating-point numbers do not convert into each other automatically. To convert, it shall be written explicitly.

(4) If the result of an operation is NaN, that it is NaN is fixed. The sign bit and the remaining bits of that NaN are a ⟦자유 선택|free choice⟧ (FC-0004).

> [!자유] FC-0004 — the sign of a NaN produced by an operation
> When the result of a floating-point operation (including per-lane operations and `reduce_*`) is NaN, its sign bit and remaining bits are chosen by the processor. That the result is NaN, and the value of every result that is not NaN, are the same either way.
> The difference shows when the value is printed (`nan` and `-nan`) and when it is read as bits. IEEE 754 does not fix this place either, and machines and C compilers give different signs where two NaNs meet or at `0.0 / 0.0`. Nailing it to one side would add a correcting instruction to every operation.
> To know whether a value is NaN, compare it with itself, as in `ne x. x.` — do not ask its sign.

## 6.2.5 Conversions between types

(1) Lowent has **no implicit conversion that loses a value.** A narrowing place is always written in the source. The only conversion the processor does by itself
      is the widening below; the word that writes any other conversion is `cast` (⟦§6.2.16⟧).

(2) A conversion that loses no value is called ⟦넓히기|widening⟧. Widening does not change the value, so the processor may do it automatically
      — a `u8` value may be passed as it is to a place that takes `u32`.

(2a) But **widening may also be written in the source.** `widen u64 x. .` widens `x` to `u64`.
      Even though the result is the same, it is used when the place where the width changes should be visible.

(2b) Widening to 128-bit integers (`u128` · `i128`) **is not automatic** — it shall always be written with `widen` (⟦§6.2.2⟧ (3b-1)).

> [!산문]
> In short — **widening is automatic, narrowing is by hand.** Widening keeps the value, so there is no room for
> mistakes, but narrowing can make a value disappear, so the author shall leave in the source that they know it.

(3) A conversion that may lose a value is called ⟦좁히기|narrowing⟧ and is written with `narrow`.
      Narrowing **traps** if the value does not fit the target type. It is not truncated silently.

(4) The relations in which widening is allowed are as follows. A combination not listed here is not widening, and writing it is
      refused at translation (`E-WIDEN-KIND` · `E-WIDEN-SIGN` · `E-WIDEN-NARROW`).

> [!표] Relations in which widening is allowed
> #table(columns: (auto, 1fr), stroke: 0.5pt + rgb("#bbb"), inset: 6pt,
> [*Relation*], [*Explanation*],
> [`uN` → `uM` (N ≤ M)], [an unsigned integer to a wider unsigned integer],
> [`iN` → `iM` (N ≤ M)], [a signed integer to a wider signed integer],
> [`uN` → `iM` (N < M)], [an unsigned integer to a **wider** signed integer],
> [`iN` → `uM`], [**not allowed** — negative numbers cannot be represented],
> [`f32` → `f64`], [a floating-point number to a wider floating-point type],
> [integer ↔ floating point], [**not widening** — when the kinds differ, it shall be converted explicitly],
> [`usize` · `idiff` ↔ other integers], [**not widening** — the width depends on the implementation. Write it with `cast` (⟦§6.2⟧ (3-3))],
> )

```도해: widening — following the arrows, the value fits as it is
 u8 ──▶ u16 ──▶ u32 ──▶ u64          f32 ──▶ f64
   ╲       ╲       ╲
    ▼       ▼       ▼
 i8 ──▶ i16 ──▶ i32 ──▶ i64
 there is no arrow down at the same width (u8 → i8), and no arrow from the i row to the u row
 usize · idiff are not in this picture — there is no arrow either way, and the word that crosses is cast
```

```lowent 예제: widening and narrowing
let small u8 200 .
let wide u64 widen u64 small. . .
let back u8 narrow u8 wide. . .
```

> [!주의] A negative number cannot be widened to an unsigned type
> Widening −1 of `i8` to `u64` turns the value into a very large number. That is not widening but
> **becoming a different value**, so this language does not accept it as widening.

## 6.2.6 Rows — arrays and slices

(1) `array n t` is a row of exactly `n` values of type `t`. The length is part of the type and
      is fixed at translation time.

(1a) Write **the length `n` first, as an integer literal, and the element type after it** — the same order as `vec n t` · `matrix k d… t`. Since the element type
      comes at the very end, the type knows its own end. Written the other way round (`array t n` — a type in the length position), it is refused (`E-TYPE-ORDER`;
      `lowentc --fmt` moves it to the new order). If the length is not a literal, it is refused (`E-TYPE-ARRAY`).

(1b) An op input `input x array n t .` means that it takes a `slice t` of length exactly `n`, and that length is
      **checked at entry**, like `requires eq len x. . n. . .`.

(1c) An op **can return** an array — `output array n t .`, or with a name, `output r array n t .` (⟦§6.4.1⟧ (3e)). An array has no way
      of being returned as a value, so **the caller provides the place**:
      - The caller binds the result to a name — `var x array n t f a … .` (`let` is the same). The bytes in that name's frame are the place of the result,
        and the callee builds directly there (it does not copy).
      - Inside an op that returns the same kind of array, `return g … .` passes on its own place as it is. But if an argument given to `g` can see the same bytes
        as that place (the named result itself, or a name derived from it — a local whose initialiser or `set` right side mentioned it), the processor **takes it into a temporary
        place and copies once** — while `g` writes, the reading side does not see new values, so the answer is the same as when they do not overlap. It passes the place on directly only when
        they are seen not to overlap (when the arguments mention no such name). A `return` outside a block (`guard … else return …`) has no place to insert a
        statement, so this rule does not stand there — the `E-RESULT-PLACE` below applies as it is.
      - Calling an op that returns an array in any other place (the middle of an expression · an argument of another call · the source of `copy`) has no place, so
        translation is refused (`E-RESULT-PLACE`). Bind it to a name first.
      - A named result starts at 0 (0 in every slot). `return v. .` of an unnamed result copies `v` into the place and returns. This shall be a `return`
        inside a block — outside a block, as in `guard … else return v. . .`, it is refused (`E-RESULT-PLACE`). Such an op names its result.
      - The elements are of number types and the length is an integer literal (anything else is `E-TYPE-ARRAY`).

(1d) **Arrays inside arrays** — the element type may itself be an array. `array n array m t` is a row of `n` rows of length `m` laid out row-major
      without gaps (the same layout as C's `t a[n][m]`). Since the element type comes at the very end, no parentheses are needed ((1a)).
      - `idx a. i. .` is a `slice t` (length `m`, no copy) that **sees** the bytes of row `i` — it follows the same rules as the sequence of rows of (9a). If the outer
        one is `mut`, the slots of that row can be written too, and there is no `set idx a. i. . <row> .` that replaces a whole row (`E-TYPE-SET`).
      - An op input `input x array n array m t .` takes a slice of rows `slice array m t`, and that the number of rows is `n` is **checked at entry** ((1b)).
        A sequence of rows, and a row view over bytes (⟦§8.7⟧ (7a)), can be passed as they are.
      - As a struct field it holds the bytes of `n × m` elements **inside the record** (the same as the array field of ⟦§6.2.7⟧ (1a) — copying the struct copies them too).
      - A value is built with `lit array n array m t <row>… [_] .`. Each row is `lit array m t …`, and a final `_` fills the remaining rows with 0 ((6a)).
      - The elements are sized numbers and `bool`. There is no limit on the number of levels — `array 2 array 3 array 4 u8` is 2 planes of 3 rows of length 4 (C's
        `u8 a[2][3][4]`), and `idx a. i. .` is a slice of rows `slice array 4 u8` that **sees** plane `i`. Each `idx` peels off one level.
        The rows of a sequence of rows · of a row view can also be arrays inside arrays (`slice array 3 array 4 u8` · `view_array array 3 array 4 u8 b.`).
      - The depth of levels is part of the type — passing or binding a slice of rows of a different depth is refused (`E-TYPE-ARG` · `E-TYPE-LET`). A slice of rows
        does not fit a place for a slice of elements either (`idx` gives a row instead of a number).
      - An op **can return** an array inside an array too — `output r array n array m t .`. As in (1c), the caller provides the place (`var x array n array m t f … .`),
        and a named result starts as rows of 0.

```lowent 예제: arrays inside arrays
module ex_nested_array .

fn rowsum input g array 2 array 3 u8 . input r usize . output u64 .
  requires lt r. 2 . .
do
  var s u64 0 .
  for x idx g. r. . do
    set s. add s. widen u64 x. . . .
  end .
  return s. .
end .

export fn table output u64 . do
  var g lit array 2 array 3 u8 lit array 3 u8 1 2 3 . _ . .
  set idx idx g. 1 . 2 . 7 .
  return add mul rowsum g. 0 . 100 . rowsum g. 1 . . .
end .
```

```lowent 예제: three levels — each idx sees the level below
module ex_nested_three .

export fn f output usize . do
  var g lit array 2 array 3 array 4 u8 _ . .
  set idx idx idx g. 1 . 2 . 3 . 9 .
  let plane slice array 4 u8 idx g. 1 . .
  return add len plane. . cast usize idx idx plane. 2 . 3 . . . .
end .
```

```lowent-거부: a slice of rows of a different depth · E-TYPE-ARG
module ex_nested_depth .

fn rows input g array 3 array 4 u8 . output usize . do
  return len g. . .
end .

export fn f output usize . do
  let c lit array 2 array 3 array 4 u8 _ . .
  return rows c. . .
end .
```

```lowent 예제: an op that returns an array — the caller provides the place
module ex_array_result .

fn squares input n u64 . output r array 3 u64 .
  requires le n. 1000 . .
do
  repeat i usize 3 do
    let v u64 add n. cast u64 i. . . .
    set idx r. i. . mul v. v. . .
  end .
  return r. .
end .

export fn total input n u64 . output u64 .
  requires le n. 1000 . .
do
  let s array 3 u64 squares n. . .
  return add add idx s. 0 . idx s. 1 . . idx s. 2 . . .
end .
```

```lowent-거부: calling an op that returns an array in the middle of an expression — there is no place for the result · E-RESULT-PLACE
module ex_array_result_expr .

fn squares input n u64 . output r array 3 u64 .
  requires le n. 1000 . .
do
  return r. .
end .

export fn first input n u64 . output u64 .
  requires le n. 1000 . .
do
  return idx squares n. . 0 . .
end .
```

(2) `slice t` points at a range of values of type `t` laid out contiguously, and **carries a start and a length
      together.**

(3) The length of a slice is read with `len`. `len s. .` is the number of elements of `s`, and its type is `usize` (⟦§6.2⟧ (3a)).
      The numbers given by `bitset_count` · `stack_count` and `capacity` are `usize` too. `len` used on the capability `cap args` is the number of arguments received.

(3′) Indices and bounds (the `i` of `idx`, the start and end of `subslice`, the index of atomic operations, the `capacity` of `alloc_bytes`) **are** `usize`. Putting a value
      of another integer type in that place is refused at translation (`E-TYPE-NOMINAL`). Integer literals are written as they are. Where they meet numbers that are not lengths,
      write the conversion — `cast u64 n. .` moves a `usize` to `u64`, `cast usize x. .` moves a `u64` to `usize`, and either way
      it traps if the value does not fit (⟦§6.2⟧ (3-3)).

(3″) A value written to an element shall be of the slice's **element type** — in `set idx s. i. . v .`, if `v` does not widen to the element type,
      translation is refused (`E-TYPE-WIDTH` · `E-TYPE-SIGN` · `E-TYPE-NOMINAL`). `slice usize` and `slice u64` are **different types** even on an
      implementation where their elements have the same width (`E-TYPE-NOMINAL`) — declare a table that holds only positions as `slice usize`, and keep a table that holds numbers of several meanings
      at a fixed width and write `cast` where it is read and written.

(3‴) `usize` counts **what belongs to one piece**: its length · an index within it · the number one operation read, wrote or processed in that piece. Such numbers
      cannot exceed the length of that piece, so they fit `usize` on every implementation. **Running totals** accumulated over many operations, and sizes · positions outside memory
      (the size and position of a file · the sum of bytes passed through a stream · the bit length of a whole message), can be large regardless of the address width, so they are
      **not** `usize` — keep them in fixed-width integers (`u64`). Hash values · checksums · random numbers are not lengths: keep them at the width of the value.

(4) `idx s. i. .` is the `i`-th element of `s`. The number of the first element is 0.

(5) If `i` is not less than `len s. .`, it **traps**. If the processor proves that the condition is always true,
      the check disappears (⟦§6.4.6⟧).

```도해: a slice carries a start and a length together
 memory   [ 10 ][ 20 ][ 30 ][ 40 ][ 50 ]
                  ^
 s = { start ─────┘ , len 3 }
 idx s 0 = 20 · idx s 2 = 40 · idx s 3 → trap (len s = 3)
```

```lowent 예제: a slice
module ex_slice .

rem A slice carries a start and a length together.
export fn head input data slice u8 . output u8 .
  requires ge len data. . 1 . .
do
  return idx data. 0 . .
end .
```

```lowent-거부: the element type was written before the length · E-TYPE-ORDER
module ex_array_order .

export fn last input xs array u64 4 . output u64 .
do
  return idx xs. 3 . .
end .
```

(6) **Element-list literals** — `lit array n t v₁ … vₖ .` is an `array n t` value, and `lit slice t v₁ … vₖ .` a `slice t` value of `k`
      elements. A list is **a form closed by its own full stop**: at the end of a statement the list's dot and the statement's dot come one after
      another (`let t lit array 4 u8 1 2 3 4 . .`); in the middle it is closed with one dot and the next operand follows.
      Since `lit` shows the type of the value, the binding statement does not write the type separately.

(6a) The number of array elements shall equal `n`. If there are fewer, a `_` shall be written at the **end** of the list to say «the remaining slots are 0»;
      if there are more, or `_` is somewhere other than the end, or a slice list has `_`, it is refused (`E-LIT-COUNT`). Slots are not filled
      silently.

(6b) Each element shall fit `t` — a negative number does not fit an unsigned `t` either (`E-TYPE-WIDTH`). `bool` elements are only
      `true` · `false`.

(6c) **Filling chosen slots** — `lit array n t do <index> <value> . … [_ <value> .] end .` puts each value into the slot whose index is written. The indices
      are integer literals from 0 to `n − 1`, each written once. `_ <value> .` comes only at the end and means «every slot whose index is not written».
      Every slot shall be decided — if slots remain and there is no `_ <value> .`, or `_` has no slot to fill, it is refused
      (`E-LIT-COUNT`). If an index is not a constant, is outside the length, appears twice, or `_` is somewhere other than the end, it is refused
      (`E-LIT-INDEX`). Values may be expressions and are computed in the order written; the value of `_` is computed **once**. A list written as a block is a form too and is closed by its own dot —
      in `let t lit array 8 u8 do 2 5 . _ 0 . end . .` the first dot closes the list and the second closes `let`. It is not mixed with the form that lays out elements in a row

(6d) **SIMD values** — `lit vec n t v₁ … [_] .` is a `vec n t` value. The element rules are the same as for array lists; in addition `t` is a sized
      number (not `bool`), and `n` is a power of 2 from 1 to 16 (`E-LIT-COUNT`). The type of the value shall agree with the `vec`
      type of the binding place in number of lanes, element kind and width (`E-TYPE-LANES` · `E-TYPE-WIDTH`). Since it ends with placing values into lanes,
      the place rules of (7) do not concern it.

(7) **Where a list is placed and how long it lives** is decided by the processor at translation as one of three. The author does not choose.
      ⓐ A list bound with `var`, and a `let` list containing elements computed during execution, have **one slot per declaration site** in that op's
      frame. That slot is filled anew each time the declaration is executed (slots not written are 0) and lives until **the block that holds the declaration** ends —
      a declaration inside a loop is filled again on every iteration, and recursive calls each have their own slot.
      ⓑ A list whose elements are all translation-time constants and which is bound with `let`, or used only for reading without a receiving place, is **read-only**
      bytes fixed in the program. It lives to the end of the program, so it may be held anywhere. Binding it to a `mut` name (putting it back with `set` is the same), or passing
      the name bound with `let` to a `mut` · `owned` · `mut_ref` place, is refused (`E-TYPE-ARGMUT`). **Whether two lists with the same contents (string literals too)
      are the same place is a ⟦자유 선택|free choice⟧** (⟦§4.3⟧ · FC-0003) — the processor may merge them into one copy or keep them apart. So a `same_slice` for which it is visible at translation
      that both sides came from such literals is refused (`E-LIT-IDENTITY` — the literal itself · a name bound to it with `let` · its
      `subslice`). Cases not visible across calls are not refused, but a program that relies on that answer does not
      conform.
      ⓒ A list used without a receiving name that contains elements computed during execution, or is passed directly to a `mut` · `owned` · `mut_ref` place, is
      a temporary in the frame that lives **until that statement ends**. The receiving op may rewrite those slots.

> [!자유] FC-0003 — whether read-only lists with the same contents are the same place
> The processor may **merge** two read-only lists with the same contents (a string literal is a list too) into one copy at the same place, or keep them **apart**. Those two are the only range of choice — the contents and length of the list, and that it lives to the end of the program, are the same either way.
> There is one place where the difference shows — `same_slice`, which asks whether two lists are the same place. So if it is visible at translation that both sides came from such literals, the question is refused (`E-LIT-IDENTITY`), and where it is not visible, a program that relies on the answer does not conform.
> To know whether the contents are the same, compare the contents — do not ask about the place.

(7a) If a value that sees a slot of ⓐ · ⓒ is put in a place that lasts longer than its lifetime — `return`, a name declared outside the declaring block (including parameters and
      their fields · elements), a message sent to an actor declared outside the declaring block — it is refused (`E-LIT-ESCAPE`).
      It may be handed to an actor declared in the same block or further in, and the values that actor gives afterwards receive the same lifetime. Putting a temporary of ⓒ into a
      name is refused too — bind the list with `var` and use it as ⓐ. The flow is followed across calls: the result of a call carries only what belongs to the inputs that flow into
      the result in that op's body, and a call that returns a scalar carries nothing out (the same distinction as region escape in ⟦§8⟧).

(7a-1) **An unnamed struct value** (the result of a call · `lit`) ends at the end of its statement. Putting a value that sees an array field of that value into a name · `return` · an outer place
      is refused (`E-VIEW-TEMP`) — once the statement ends, those bytes belong to the next value made. Only reading on the spot (taking numbers out with `len` · `idx`)
      conforms. To see a field for long, bind the struct to a name first (`let b T … .` and then `field b. <field> .`).

(7b) A list bound with `var` is a **writable** slice — slots are written with `set idx buf. i. . v. .`. `set buf …`, which rebinds that name to other storage,
      is refused (`E-ARRAY-SET`). Each element follows the width rule of (6b), and elements computed during execution shall agree with the element
      type (`E-TYPE-SET` · `E-TYPE-WIDTH`).
      A slot reached through `idx` and `subslice` is the same storage too — in `set idx subslice buf. a. b. . i. . v. .`, if `buf` is not a writable
      name it is refused (`E-TYPE-MUT`).
      A slice that cannot be written cannot be rebound to a `mut slice` name — `let w mut slice u32 g. .` and
      `let w mut slice u32 subslice g. a. b. . .` are refused if `g` is not a writable name (`E-TYPE-MUT`) — and so is putting it back into a `var` of type `mut slice`
      with `set w. g. .`. The new name
      points at the same storage, so writing through it is writing to `g`. A view `view_array <type> g.` does not copy either, so it is the same —
      binding it to a `mut` name, passing it to a `mut` input, or doing `set` · `copy` · `store` through it requires `g` to be a writable name
      (`E-TYPE-MUT` · `E-TYPE-ARGMUT`). The same holds when the receiving side of `copy` and `store` is a `subslice`.
      A `var` of a slice (or view) type without `mut` can be rebound to another slice but is not a name that writes slots — writing a slot through that name
      is `E-TYPE-MUT`, and passing it to a `mut` · `owned` input is `E-TYPE-ARGMUT` (whatever it holds — a read input or a string literal alike).

(7b-1) **Copying contents** — `copy <receiver> <giver> .` copies the contents of the giving slice into the receiver. The receiver shall be writable
      (`E-TYPE-MUT`), the two element types shall be the same (`E-TYPE-SET`), and the two lengths shall be the same — if they are known to differ at translation it is
      refused (`E-COPY-LEN`), and if they differ during execution it stops. Even if two pieces of the same row overlap, it copies correctly (like a move whose ends overlap).
      Since it writes to the receiver, an `fn` that copies into a parameter is not pure (`E-EFFECT-PURITY`).

(7b-2) **Swapping** — `swap <slice> <i> <j> .` exchanges the values of two slots. Since it writes slots, that slice shall be writable
      (`E-TYPE-MUT` — a `let` list · a string · a parameter that is not `mut` · an array built at translation cannot be written).

(7c) The total size of all slots of ⓐ · ⓒ in one op has a limit the processor states, and exceeding it is refused (`E-FRAME-SIZE` — it is not silently
      moved to the heap). This processor's limit is 65,536 bytes on a machine with an operating system · 4,096 bytes on a machine without one.
      If an op with such slots can reach itself (even through other ops), it is refused (`E-FRAME-RECURSIVE`) — frames multiply by the recursion depth,
      and that depth is not known at translation, so it cannot be shown that the stack does not run out. A list inside recursion is taken from an allocator
      ((7d)). If, following the call relation, the sum of ⓐ · ⓒ slots alive at the same time on the deepest path exceeds the budget below, translation is
      refused (`E-STACK-BUDGET`) — it is measured per entry (an op nobody calls). Ops that run as tasks (`spawn <op>` · the handler of `spawn send`)
      are measured separately against the green-thread budget. During execution, too, the sum of ⓐ · ⓒ slots and the struct slots of (7c-1) **alive at the same time**
      on one call chain has a budget the processor states, and it **stops** rather than enter an op that would exceed it — the stack does not overflow and break silently.
      This processor's budget is 4 MiB on a machine with an operating system · 64 KiB per green thread ·
      16 KiB on a machine without an operating system, and every back end counts with the same numbers and stops at the same place.

(7c-1) **The place of a struct value is its op's frame.** For each op the processor decides at translation the number of frame slots that struct value `lit`s and calls returning a struct
      will use — a value at a place for which there are not enough slots lives in the pool of ⟦§8.1⟧ (3a). The share of the slots (8 bytes per field · the bytes of an array field) counts
      towards the budget of (7c). When the frame ends, the value ends too, and a returned value is **copied** into the caller's place. Recursion is not refused for frame slots — frames
      pile up by the depth, and the budget of (7c) stops it during execution. If, on a path without recursion, struct values push the deepest path beyond the budget, translation is refused
      (`E-STACK-BUDGET`). An op that takes or makes references can write to someone else's place, so it does not use frame slots (its values live in the pool).

(7d) **A list taken from an allocator** — `var <name> use <allocator> mut slice t lit array n t … else <statement>` (`lit slice` is the same)
      asks that allocator for the byte count of the list (`send <allocator> reserve <bytes>`); if it receives them, it fills those bytes as in (6)–(6c) and binds them to the name;
      if not, it goes to `else` (⟦§6.5.1⟧ (5)). To carry the failure along as an `option`, write the whole type —
      `let <name> use <allocator> option mut slice t lit … .`. If there is neither `else` nor `option`, or it is `lit vec`, it is refused
      (`E-LIT-USING`). If the element of the binding type differs from the element type of the list, it is refused (`E-BIND-ELSE`). The bytes belong to the allocator, so unless (7e)
      applies they do not receive the declaring-block lifetime of (7a) but follow the allocator's lifetime: if the allocator received a slot of ⓐ as its backing
      (`send <allocator> init buf`), the bytes that allocator gives receive that slot's lifetime too.
      A struct value is built in allocator bytes with the same spelling: `var <name> use <allocator>. s lit s do … end . else <statement> .` asks for
      `size_of s .` bytes; if it receives them, it fills them with 0, lays the layout of s over them (the same representation as `view` of ⟦§8.7.2⟧) and writes the fields given.
      The binding type shall be s (`E-BIND-ELSE`). **A slice field of numbers** is placed in the allocator bytes as two words (address, length) —
      the same as C's `struct { const T *p; size_t n; }`, aligned to 8 bytes. This layout exists only for structs built in an allocator:
      laying such a struct over someone else's bytes with `view`, or asking its size with `size_of`, is still refused (bytes must not become addresses).
      If a value given to a slice field sees a place in a frame, the struct receives that place's lifetime too — even if kept with `keep`, carrying it longer than that place
      is refused (`E-LIT-ESCAPE`). Structs with `owned` · actor · capability fields, slice fields whose elements are not numbers, or inner structs without a layout
      are not built (`E-LIT-UNBUILT`) — those bytes would have to end together with their fields.

(7e) **Given back at the end of the block** — if the allocator provides `freeing_allocator` (⟦§8.13⟧ (6)), bytes received through (7d) are given back to the allocator when leaving the block
      that declared the name (`send <allocator> release <the piece received>`). This holds on reaching the end of the block · on `return` · on `break` · `continue` inside
      that block. If several were received in one block, they are given back in reverse order of declaration, and what was not received (what went to `else`) has nothing
      to give back. The answer of `release` is not used — whether to take it back is the allocator's policy. So these bytes receive the declaring-block lifetime of (7a):
      carrying them outside the block is refused (`E-LIT-ESCAPE`). Bytes of an allocator that is not a `freeing_allocator`
      stay with the allocator. When stopping with `panic`, they are not given back — stopping hands that op over to the recovery unit.
      The author can give them back earlier with `drop <name> .`. Then they are not given back again at the end of the block. `drop` is accepted only in the block that declared
      the name (in an inner block it is `E-OWN-JOIN`), and using the name after it is refused (`E-OWN-MOVED`).

(7f) **Kept** — `var <name> keep <allocator> … else <statement>` switches off (7e): the bytes are not given back on leaving the block,
      so they follow the allocator's lifetime (⟦§6.2.6⟧ (7d)) and can be carried outside the block — by `return` too. Giving them back is
      the author's job (`send <allocator> release <piece>`). Otherwise they all go back together when the allocator ends. `keep <allocator>`
      stands in the place of `use <allocator>` (directly after the name) and is used only on bindings that take a list or struct literal (`E-USING-FORM`).

(8) An array list bound with `let` is a **slice that sees** those bytes. The list decides the length.

(9) The elements of a list may be structs with a byte layout (all fields sized numbers — structs on which `view` of ⟦§8.7.2⟧ can stand).
      An element is a copy of the value given, and the list is a `slice s` with a view like `view_array` laid over those bytes. A field of an element
      is written with `set field <list>. <position> <field> . v. .`. Place · lifetime · `_` · filling chosen slots · the allocator clause are the same as for number elements. If the value
      in an element position is a number or a truth value it is refused with `E-TYPE-FIELD`; if it is another struct, with `E-TYPE-STRUCT`. Lists this processor has not yet built
      — structs without a byte layout · structs whose fields are structs · filling chosen rows (`do … end`) — are refused saying what is not yet built
      (`E-LIT-UNBUILT`). Array fields inside structs are specified by ⟦§6.2.7⟧ (1a).

(9-1) A struct whose fields are only sized numbers · `bool` and **slices of numbers** can also be an element of a list. Inside an element a slice field is placed as two words (the place it points at,
      the length) — the same layout as a struct built in allocator bytes ((7d)). The fields are written at execution, so such a list stands in a frame place
      of ⓐ. A `slice s` input taking the same elements, and an array result `output r array n s .`, use this layout too. A slice field only points at someone else's bytes,
      so copying an element does not copy the bytes it points at.

(9a) **A list of rows** — the type of a list `lit slice array m t <row>… .` whose elements are fixed-length arrays `(array m t)` is
      `slice array m t`. Like other slice lists ((6)) the number of rows is not written and there is no `_` ((6a)) — a row of 0 is written
      `lit array m t _ .`. `lit array n array m t <row>… [_] .`, with the number of rows written, is the value of an array inside an array ((1d)). Over existing bytes a row view of the same type is laid with `view_array array m t b.` (⟦§8.7⟧ (7a)). `len` is the number of rows, and `idx g. r. .` is a `slice t` (length m, no copy) that **sees** the bytes of row r —
      if the outer one is `mut`, the slots of that row can be written too. Row values are copied slot by slot, and if the shape of a row literal differs from `array m t` it is
      refused (`E-LIT-COUNT`). There is no `set idx g. r. . <row> .` that replaces a whole row (`E-TYPE-SET`) — write the slots, or copy
      with `copy`. The rows (and struct elements) of a list in a frame see that frame's bytes, so carrying them outside the block is `E-LIT-ESCAPE`.

```lowent 예제: element-list literals
module ex_list_literal .

fn total input xs slice u32 . output u64 . do
  var s u64 0 .
  for x xs. do
    set s. add s. widen u64 x. . . .
  end .
  return s. .
end .

rem An array list is closed by its own dot — at the end of a statement there are two dots.
export fn f output u64 . do
  let t lit array 4 u32 10 20 30 _ . .
  return add total t. . total lit slice u32 1 2 . . . .
end .
```

```lowent 예제: a var array filled by a loop
module ex_frame_list .

export fn squares input n usize . output u64 . do
  var buf lit array 8 u64 _ . .
  var i usize 0 .
  while lt i. 8 . do
    set idx buf. i. . cast u64 mul i. i. . . .
    set i. add i. 1 . .
  end .
  guard lt n. 8 . else return 0 . .
  return idx buf. n. . .
end .
```

```lowent 예제: filling chosen slots
module ex_cell_fill .

export fn pick input a u64 . input i usize . output u64 . do
  let t lit array 6 u64 do 0 100 . 5 mul a. 2 . . _ 1 . end . .
  guard lt i. 6 . else return 0 . .
  return idx t. i. . .
end .
```

```lowent-거부: a slot index written twice · E-LIT-INDEX
module ex_cell_twice .

export fn f output u64 . do
  let t lit array 4 u8 do 1 1 . 1 2 . _ 0 . end . .
  return len t. . .
end .
```

```lowent-거부: returning a var array from inside a block · E-LIT-ESCAPE
module ex_frame_escape .

export fn f output slice u8 . do
  var b lit array 2 u8 1 2 . .
  return b. .
end .
```

```lowent-거부: fewer elements than the length, and no _ at the end · E-LIT-COUNT
module ex_list_short .

export fn f output u64 . do
  let t lit array 4 u8 1 2 3 . .
  return len t. . .
end .
```

> [!산문]
> In a language with only pointers, a person has to know separately *"how many are there where this pointer points"*.
> That knowledge is not written in the source, so it is easily wrong, and when it is wrong someone else's memory
> is read. A slice removes the problem by **putting that knowledge inside the value**.

## 6.2.6.1 Rows of several dimensions — `matrix`

(1) `matrix k d1 … dk t` is a row of `k` dimensions holding `d1 × … × dk` values of type `t`. The number of dimensions and the sizes are part of the
      type and are fixed at translation. If even one of the number of dimensions or the sizes differs, it is a different type — `matrix 2 3 4 u32` differs
      from `matrix 2 4 3 u32` and from `array 12 u32`.

(1a) Write the number of dimensions first, then as many sizes as there are dimensions, and the element type at the very end (the same order as ⟦§6.2.6⟧ (1a)). The number of dimensions
      is from 2 to 8 and the sizes are 1 or more, all plain decimal literals (in an op input a size may be the name of a translation-time value input — (3a)).
      The old order (`matrix t k d…` — element type first) is refused (`E-TYPE-ORDER`). The number of elements (the product of the sizes) cannot exceed 2^32. `matrix` or `array` cannot
      be written as the element type. Violations are refused (`E-MATRIX-TYPE`).

> [!참고]
> Since the number of dimensions is written first, where the sizes end is known from the characters alone (a size may also be the name of an input). Even when elements follow (`lit matrix 2 2 3 u32 1 2 3 4 5 6 .`),
> where the sizes end and the elements begin is separated by the characters alone.

(2) **Layout.** The elements are laid out contiguously in one block, row-major — the last index runs fastest. Size and alignment are
      the same as `array n t` (`n` is the product of the sizes), and no header is added.

> [!참고]
> This layout is the same as C's `t a[d1]…[dk]`. Passing `flat m. .` to an `extern` op with a `slice t` input, C receives a pointer and the number
> of elements, and reading that pointer as `t (*)[d2]…[dk]` points at the same slots.

(3) A `matrix` type is written **in places that make a name** — the type of `let` · `var`, an op's `input` and `output`, a struct field. It cannot be written
      as the element of another type, as a type argument or in the place of an alias (`E-MATRIX-USE`).

(3a) Only in an op's `input`, the name of a translation-time value input of that op can be written in a size position —
      `input comptime r u64 . input comptime c u64 . input m matrix 2 r c u32 .`. The caller writes a number for that input
      (`sum 3 4 a. .`), and the shape of the argument shall be the same as the shape read with those numbers (`E-MATRIX-SHAPE`). The sizes of `let` · `var` · `output` · struct
      fields and of `lit matrix` shall be numbers (`E-MATRIX-TYPE`).

> [!참고]
> An input that takes its sizes by name gets no entry contract on its length. What can come in that place is only a `matrix` whose shape the caller compared,
> and reading and writing elements receive the checks of (5) unchanged.

(4) **Values.** `lit matrix k d1 … dk t <element>… .` lists the elements in row-major order. The number of elements shall equal the product of the sizes;
      if there are fewer, write `_` at the end to fill the rest with 0. If a binding writes both a type and a literal, the two shapes shall be the same.
      Violations are refused (`E-MATRIX-SHAPE`). In a struct literal, the value of a `matrix` field is a `lit matrix` of the same shape as
      that field.

(5) **Reading and writing.** `idx m. i1 … ik .` takes one index per dimension. If the number of indices differs from the number of dimensions, it is refused
      (`E-MATRIX-INDEX`) — an `idx` with fewer indices does not mean a row (a row is the `row` of (7a)). Each index is checked **against the size of its own dimension**: if any
      one of them is that size or more, execution stops. Index expressions are computed once each, in the order written.
      `set idx m. i1 … ik . v. .` writes that slot, and `m` shall be a writable name (`E-TYPE-MUT`).

(6) `dim m. j .` is the size of dimension `j` (counting from 0). `j` shall be a plain decimal literal less than the number of dimensions
      (`E-MATRIX-INDEX`). Its value is a translation-time constant.

(7) `flat m. .` is a `slice t` that sees the same elements as one row. It does not copy, and its length is the number of elements.

(7a) `row m. i1 … i(k−1) .` is the **row** that leaves one last dimension — one index for each dimension except the last, and its value is a `slice t` that sees
      the elements of that row. It does not copy, and its length is the size of the last dimension. If the number of indices differs it is refused (`E-MATRIX-INDEX`),
      and each index is checked against the size of its own dimension as in (5). If `m` is a writable name, that row can be bound to a `mut slice t` name
      and written; otherwise it cannot be bound that way (`E-TYPE-MUT` · `E-IMMUTABLE` if it is a field of a struct bound with `let`). There is no view that leaves two
      or more dimensions (a plane).

(7b) In the `m` position of (5) · (6) · (7) · (7a), not only a name written as `matrix` stands but also `field x. g .` that points at a `matrix` field —
      `idx field b. g . r. c. .`.

(8) `len m. .` and `for x m. do … end .` are refused (`E-MATRIX-USE`). The size of one dimension is written with `dim`, and going over all
      elements with `for x flat m. . do … end .`.

(9) The places where a `matrix` name can be written whole are the first argument of `idx` · `row` · `dim` · `flat`, an argument of an op with an input of the same shape,
      and the `return` of an op with a result of the same shape. Other places are refused (`E-MATRIX-USE`). Arguments and inputs, and the type of a binding
      and the result of the op called, shall agree in both number of dimensions and sizes (`E-MATRIX-SHAPE`).

(9a) A call of an op whose result is a `matrix` is placed in only two places — the value of a binding that makes a `matrix` name of the same shape, and the value of the `return`
      of an op with a result of the same shape. The `return` of an op whose result is a `matrix` gives a `matrix` name of that shape or a call of an op that gives that
      shape. Violations, including binding it to an `array` name or returning it from an op that gives an `array`, are refused
      (`E-MATRIX-SHAPE`).

(10) This type specifies only **places**. It does not specify computations such as matrix multiplication.

```lowent 예제: fill a table of 3 rows and 4 columns and add up one row
module ex_matrix .

fn table output t matrix 2 3 4 u32 .
do
  repeat r usize dim t. 0 . do
    repeat c usize dim t. 1 . do
      set idx t. r. c. . cast u32 add mul r. 10 . c. . . .
    end .
  end .
  return t. .
end .

export fn row_sum input r usize . output u64 .
  requires lt r. 3 . .
do
  let t matrix 2 3 4 u32 table . .
  var s u64 0 .
  repeat c usize dim t. 1 . do
    set s. add s. widen u64 idx t. r. c. . . . .
  end .
  return s. .
end .
```

```lowent 예제: a table as a struct field, one row, an op that takes any size
module ex_matrix_more .

struct board do
  id u64 .
  g matrix 2 3 4 u32 .
end .

fn sum input comptime r usize . input comptime c usize . input m matrix 2 r c u32 . output u64 .
do
  var t u64 0 .
  repeat i usize dim m. 0 . do
    for x row m. i. . do set t. add t. widen u64 x. . . . end .
  end .
  return t. .
end .

export fn total output u64 .
do
  var b lit board do id 1 . g lit matrix 2 3 4 u32 1 2 3 4 5 6 7 8 9 10 11 12 . . end . .
  set idx field b. g . 0 0 . 100 .
  let small lit matrix 2 2 2 u32 1 2 3 4 . .
  return add widen u64 idx field b. g . 0 0 . . sum 2 2 small. . . .
end .
```

```lowent-거부: two dimensions, but only one index given · E-MATRIX-INDEX
module ex_matrix_row .

export fn f output u64 . do
  let m lit matrix 2 2 3 u32 1 2 3 4 5 6 . .
  return widen u64 idx m. 1 . . .
end .
```

```lowent-거부: passing 4 rows of 3 columns to an op that takes 3 rows of 4 columns · E-MATRIX-SHAPE
module ex_matrix_shape .

fn first input m matrix 2 3 4 u32 . output u32 . do
  return idx m. 0 0 . .
end .

export fn f output u32 . do
  let m lit matrix 2 4 3 u32 _ . .
  return first m. . .
end .
```

## 6.2.7 Aggregates — struct and enum

(1) A `struct` is a collection of named fields. Each field has its own type.

(1a) **Array fields** — if the type of a field is `array n t`, that field holds the bytes of `n` elements **inside the record** (since the length is in the
      type, the place is fixed). `t` is a sized number or `bool` — fields whose elements are structs or arrays this processor has not yet
      built (`E-LIT-UNBUILT`). The sum of the bytes of the array fields of one struct has the same limit as a frame (`E-FRAME-SIZE`, ⟦§6.2.6⟧ (7c)).
      `field r. body .` is a slice that sees those bytes, and `set idx field r. body . i. . v. .` writes an element of the record. A list literal given to the field
      shall have the same element type and length as the field (`E-TYPE-FIELD`). In a byte layout (`view` · `encode` · `size_of` · the C-side
      layout) an array field lies **as it is inside the struct bytes**, as in C — its size is `n × |t|` and its alignment that of `t`
      (`len u8 . body array 4 u8 . tail u16 .` is 8 bytes — `len` from byte 0 · `body` from byte 1 · `tail` from byte 6). The array field of a view is a slice
      that sees those bytes, and writing the field copies into that place.

(2) An `enum` is one of several. Each variant has a name and may carry values.

(2a) Each variant **is closed with `.`.** A newline is not a closer, so variants written one per line without dots run together into one variant
      and are refused (`E-ENUM-DOT`). The values a variant carries are written as `<field name> <type>` pairs — if the pairs do not match,
      it is refused (`E-ENUM-FIELD`).

(2b) An enum value is not a number — to use a variant's number as a number, write `cast` (⟦§6.2.16⟧ (5)).

(2c) **Every variant has a number.** The first variant is 1, and the next variant is the number of the previous variant plus 1. An integer written after a variant name
      (`ga 11 .`) becomes that variant's number, and the next variant counts on from there. The numbers shall **increase** in the order declared — a number not greater than
      the previous variant's number is refused (`E-ENUM-NUMBER`). Numbers are written only in an enum with no variant that carries values (`E-ENUM-NUMBER`). 0 is not the first variant's
      number — in an enum whose numbers are not written, 0 is no variant.

```lowent 예제: numbers written on variants · 결과: total() = 226
module ex_enum_numbers .

enum hangul do
  ga 11 .
  na .
  da 101 .
  ra .
end .

fn n input h hangul . output u32 . do
  return cast u32 h. . .
end .

rem 11 + 12 + 101 + 102
fn total output u32 . do
  return add add n ga. . n na. . . add n da. . n ra. . . . .
end .
```

```lowent-거부: the numbers do not increase · E-ENUM-NUMBER
module ex_enum_number_falls .

enum level do
  low 5 .
  mid .
  high 6 .
end .
```

(2d) **The width of an enum.** An enum whose variants carry no values may have one line `layout <width> .` in its block. The width is one of `u8` · `u16` · `u32` ·
      `u64`, and is the number of bytes the enum occupies when it enters a byte layout (⟦§6.2.13⟧) — the bytes hold the variant's **number** (2c).
      This line is not a variant. An enum without a width has no byte layout. The following are refused (`E-ENUM-LAYOUT`): a width outside the four,
      a variant number that does not fit the width, an enum with a variant that carries values, two `layout` lines.
      ☞ Why the width is not chosen automatically: adding one variant must not silently change a layout beyond a wire or a file.

```lowent 예제: an enum with a width is laid in one byte as its number · 결과: kind_of([7,0,3]) = 2 · kind_of([2,0,3]) = 0
module ex_enum_layout .

enum kind do
  layout u8 .
  ping .
  data 7 .
  close .
end .

struct hdr do
  layout packed .
  k kind .
  len u16 big .
end .

fn kind_of input b slice u8 . output u8 . do
  let o option view hdr try_view hdr b. . .
  guard is_some o. . else return 0 . .   rem 2 is the number of no variant
  let h view hdr some_value o. . .
  match field h. k . do
    case ping do return 1 . end .
    case data do return 2 . end .
    case close do return 3 . end .
  end .
end .
```

```lowent-거부: a number does not fit the width · E-ENUM-LAYOUT
module ex_enum_layout_fit .

enum code do
  layout u8 .
  ok .
  far 300 .
end .
```

(3) A struct **cannot have itself as a field.** If it cycles, directly or through other types,
      translation is refused
      (`E-STRUCT-CYCLE`). Because its size would not be fixed.

(3a) If it points to itself through a reference, the size is fixed. But if that reference is one **whose manager cannot be
      known**, it is refused (`E-STRUCT-REF-UNMANAGED`) — having a size does not mean the lifetime
      is settled.

(3b) If the type of a field is exactly `option (owned <that struct>)`, it is not regarded as the cycle of (3). That field is not a value but **one address slot** pointing at
      another node built in allocator bytes (`none` when empty), so the size is fixed. Such a struct is called a node that holds
      itself, and the rules for building and giving it back are specified by ⟦§8.5⟧ (10)–(13). `owned <itself>` without `option`, and ownership cycles that
      come back through another struct, are still caught by (3).

```lowent 예제: struct and enum
module ex_shape .

struct point do
  x u32 .
  y u32 .
end .

enum color do
  red .
  green .
end .
```

(4) A struct value is made with `lit`. When made, **every field shall be filled**. `_ <value> .` on the last line puts that value into every field not written
      (`lit pt do y a. . _ 0 . end .`) — the value is one name or literal (`E-LIT-INDEX`); if a field remains that the value cannot go into,
      it is refused naming that field (`E-TYPE-FIELD`); and if there is no field to fill, it is refused (`E-LIT-COUNT`).

(4a) **A struct is a value.** When it is made or copied (`var q point p. .`), the bytes of its array fields and the other struct values in its fields are
      copied with it — changing the copy does not change the original. Actor instances (things with an identity) and structs with `owned`
      fields are not copied but point at the same thing.

(4b) **Writing a field is writing to that record.** `set field r. x . v. .` and writing an element of an array field are accepted only when `r` is a writable
      place: a `var` or a `mut` parameter. Writing to a record bound with `let` is refused (`E-IMMUTABLE`) —
      so is taking an array field out as a writable slice (`mut slice`). Writing a field of a parameter received by value writes to that op's **local
      copy** — the caller's record does not change (⟦§6.5.1⟧ (2a)).

(5) A field is read with `field <value> <field name>`. There is no form that attaches a dot and a field name after the value
      (`E-FIELD-GLUED`) — the dot is already used for module qualification and variant names.

```lowent 예제: making and reading a struct
module ex_make .

struct point do
  x u32 .
  y u32 .
end .

export fn origin output point .
do
  return lit point do x 0 . y 0 . end . .
end .

export fn get_x input p point . output u32 .
do
  return field p. x . .
end .
```

```lowent 예제: copying a struct with an array field
module ex_struct_array .

struct pkt do
  len u8 .
  body array 4 u8 .
end .

export fn copy_keeps input a u8 . output u64 . do
  var p lit pkt do len 2 . body lit array 4 u8 1 a. _ . . end . .
  var q pkt p. .
  set idx field q. body . 0 . 100 .
  return add widen u64 idx field p. body . 0 . . widen u64 idx field q. body . 0 . . . .
end .
```

```lowent-거부: writing the array field of a record bound with let · E-IMMUTABLE
module ex_struct_array_let .

struct pkt do
  body array 4 u8 .
end .

export fn f output u64 . do
  let p lit pkt do body lit array 4 u8 _ . . end . .
  set idx field p. body . 0 . 1 .
  return 0 .
end .
```

```lowent-거부: variants not closed with dots · E-ENUM-DOT
module ex_enum_dot .

enum color do
  red
  green
end .
```

> [!참고]
> Cycles are forbidden because of size. A struct that holds itself would have infinite size.
> If a tree-like data structure is needed, link it by **numbers (indices)** — then the size is fixed, and
> bounds checks hold as they are.

## 6.2.8 Types that hold an answer — result and option

(1) `result t e` holds either a successful value (`t`) or an error (`e`).

(2) `option t` holds either a value or none.

(3) Values of these two types **shall be checked for which side they are before being taken out.**

(3a) Taking out is a **partial operation** — taking out without checking is not itself refused at translation,
      and taking out the absent side during execution **traps**. That is, the check is not done for the author by translation;
      the author does it.

(3b) The type of the contained value is part of the type. `option usize` and `option u64`, `result u32 e` and `result u64 e` are different
      types, and putting one in the place of the other is refused at translation (`E-TYPE-NOMINAL` · `E-TYPE-WIDTH` · `E-TYPE-SIGN`). The type of a value taken out
      (`some_value` · `ok_value`) is the type of the contained value. When `some` · `ok` wrap a literal, it follows the type of the receiving place.

```lowent 예제: returning failure with result
module ex_result .

enum err do
  too_small .
end .

rem If it is less than 2, report that it cannot be halved.
export fn half input n u32 . output result u32 err .
  errors too_small lt n. 2 . .
do
  guard ge n. 2 . else return error too_small . . .
  return ok div n. 2 . . .
end .
```

> [!산문]
> There is a custom of using **special values** such as −1 or a null pointer to signal failure. Then
> *"is this −1 an error or just −1"* cannot be known from the source alone. `result` and `option`
> move that question into the type, so that the processor asks it instead.

(4) The making side wraps values with `ok` (success) `error` (error) `some` (a value is present) `none` (no value).

(5) The receiving side first asks which side it is — `is_error` (is it an error) `is_some` (is a value present).
      Then it takes out — `ok_value` (the success value) `error_value` (the error) `some_value` (the present value).

```도해: wrap · ask · take out
 ok v  | error e  ──▶ is_error r ── false ──▶ ok_value r    = v
                                 └─ true ──▶ error_value r = e
 some v | none    ──▶ is_some o  ── true  ──▶ some_value o  = v
                                 └─ false ─▶ some_value o  → trap
```

(6) `value_or` gives the value if there is one, otherwise the value given in its place. It reduces the two steps of asking and taking out
      to one.

(7) `try` takes a `result`, takes out the value on success, and on failure **passes that error on upward as it is**
      (⟦§6.5.7⟧).

(8) If a `result` is received and **nobody looks at it**, the processor reports it
      (`W-RESULT-DISCARD`). The design of returning failure as a value is kept only when the receiving side **looks** at that value.
      There are two shapes of not looking --- calling the op **as a statement** and discarding the value without even a name,
      and **putting it into a name and never reading it**.

(8a) A place that **deliberately lets** a failure pass is written `drop <name> .`. Then it is not reported.
      There are places where there really is nothing to do with a failure --- such as the place on an error path that closes resources and leaves.
      `drop` already means «I end this here», so no new word is put in that place.

> [!산문]
> Why a report and not a refusal? Ignoring a failure is not **always** wrong --- what is wrong is ignoring it
> **silently**. So what this clause requires is not «do not ignore» but
> «write that you ignore», and once it is written, a reader reads it as **the author's judgment**.
> What is not written, nobody knows whether it is judgment or a mistake.

```lowent 예제: taking out without checking stops during execution · 결과: raw(2) = 20 · raw(7) → E-VM-NONE (trap)
module ex_partial .

fn mk input k u8 . output option u8 .
do
  guard lt k. 3 . else return none . .
  return some mul k. 10 . . .
end .

rem Take it out at once without checking — translation passes.
fn raw input k u8 . output u8 .
do
  return some_value mk k. . . .
end .
```

> [!산문]
> `raw(2)` gives 20. `raw(7)` takes out a value that is not there, so it **stops** — it does not silently give 0.
> Translation does not prevent this in advance because on which path a value is present is **what the author
> knows**, and the processor cannot always know it. Instead, when it is wrong, **it is not silent.**

```lowent 예제: option — the making side and the receiving side · 결과: use_ask(2) = 20 · use_ask(7) = 255 · use_or(2) = 20 · use_or(7) = 99
module ex_option .

rem The making side — some if there is a value, none if not.
fn lookup input k u8 . output option u8 .
do
  guard lt k. 3 . else return none . .
  return some mul k. 10 . . .
end .

rem The receiving side ① — ask, then take out.
fn use_ask input k u8 . output u8 .
do
  let r option u8 lookup k. . .
  guard is_some r. . else return 255 . .
  return some_value r. . .
end .

rem The receiving side ② — if absent, give a value to use instead.
fn use_or input k u8 . output u8 .
do
  return value_or lookup k. . 99 . .
end .
```

> [!산문]
> There is one dot in `output option u8 .` — `option` takes **one** type, so it ends at that number of arguments,
> and the dot closes the `output` clause. The old shape that doubled a dot for every type (`option u8 . .`) is refused (⟦§6.1.6⟧ (2e)).

```lowent 예제: result — asking and taking out, and passing on with `try` · 결과: use_ask(100) = 50 · use_ask(250) = 0 · use_try(100) = ok 51
module ex_result_use .

enum io_error do
  too_big .
end .

rem The making side — when it fails is written in the contract.
fn halve input a u8 . output result u8 io_error .
  errors too_big gt a. 200 . .
do
  guard le a. 200 . else return error too_big . . .
  return ok div a. 2 . . .
end .

rem The receiving side ① — ask, then take out.
fn use_ask input a u8 . output u8 .
do
  let r result u8 io_error halve a. . .
  guard not is_error r. . . else return 0 . .
  return ok_value r. . .
end .

rem The receiving side ② — try passes the failure on upward as it is.
fn use_try input a u8 . output result u8 io_error .
  errors too_big gt a. 200 . .
do
  let v u8 try halve a. . . .
  return ok add v. 1 . . .
end .
```

> [!산문]
> Note that `use_try` writes `errors` itself too. To pass a failure on with `try`, it
> **shall be able to return that error itself**, and it shall write that in its contract. There is no path by which a failure
> silently disappears.

## 6.2.9 Named types

(1) `type` gives an existing type another name. The two names are **the same type** and can be used interchangeably.

(2) `newtype` makes **a different type** with the same representation as an existing type. They cannot be used interchangeably.

(2a) The shapes are `type <name> <type> .` and `newtype <name> <type> .`. No word is inserted between the name and the type.

```lowent-거부: a `newtype` does not mix with its base type — tests (2) · E-TYPE-NOMINAL
module ex_newtype_mix .

newtype node_id u64 .

fn f input a node_id . input b u64 . output u64 . do
  return add a. b. . .
end .
```

(2b) The type of `type` and `newtype` cannot itself be a **row type** — `slice` · `array` · `matrix`. The same holds with `mut`
      in front. A row type is written as it is where it is used. A row type **inside** another type can be given
      a name (`type found option slice u8 .`). If a type with its own name is needed, put it as a struct field (⟦§6.2.7⟧).

> [!표] Diagnostics for naming a row type
> #table(columns: (1fr, auto), stroke: 0.5pt + rgb("#bbb"), inset: 6pt,
> [*Violation*], [*Diagnostic*],
> [(2b) — the whole type is a `slice`], [`E-TYPE-ROWNAME`],
> [(2b) — the whole type is an `array`], [`E-TYPE-ARRAY`],
> [(2b) — the whole type is a `matrix`], [`E-MATRIX-USE`],
> )

```lowent-거부: another name given to a slice — tests (2b) · E-TYPE-ROWNAME
module ex_rowname .

type bytes slice u8 .

fn f input b bytes . output u64 . do
  return len b. . .
end .
```

> [!산문]
> A row type shows its element type and its length · dimensions in its words. Hidden behind another name, a reader has to go to the declaration
> to know them — and there become two ways of writing the same type. A struct field is different: a different name makes a different type, so
> that name does not hide but **separates**.

> [!참고]
> The two are separated to prevent mistakes. If user numbers and order numbers are both `u64`,
> the processor does not notice when they are swapped. Separated with `newtype`, the processor catches that mistake.

(3) When a `newtype` is used as **a name that separates storage** (⟦상표|brand⟧), one brand points at **one**
      storage. Opening storage twice with the same brand is refused
      (`E-BRAND-REUSED`) — then one name would point at two, and handles from different storage
      **would look like the same type.** That is exactly the confusion the brand was meant to prevent.
      If they must be separated, declare one more `newtype`.

## 6.2.10 Reading inside a value — field

(1) `field` is the form that reads what is inside a value. Its shape is
      `field <value> <step> …`, with **one or more** steps. With several steps it goes down
      one level at a time from left to right — `field o. inner deep .` means `inner` of `o`,
      and `deep` inside that.

(2) A step is one of two things. A **name** is a field of a struct, and an **integer** is
      a position in a row (array · slice). The two may be mixed.

(3) `field` is also a ⟦자리|place⟧ — it can come on the left of `set`.
      The reading place and the writing place have **the same spelling**.

(4) If a field not in the declaration is written, the processor refuses the program. Positions in rows
      follow the bounds rules of ⟦§6.2.6⟧.

```lowent 예제: reading and writing a multi-level field
module ex_field .

struct inner do
  a u64 .
end .

struct outer do
  i inner .
end .

export fn read_deep input o outer . output u64 .
do
  return field o. i a . .
end .

export proc bump_deep input o mut outer .
  effects state .
do
  set field o. i a . 1 .
end .
```

> [!참고]
> The notation with attached dots (`o.i.a`) **does not exist**. If operations on values are made to look like
> name paths, the meaning is decided only once the reader knows whether `o` is a value or a module —
> and that is the work of name resolution, not of grammar. Written as a form, it is decided **the moment it
> is read**.

## 6.2.11 Values with lanes — `vec`

(1) `vec <lane count> <element type>` holds several values of the same type **as one value** (the same order as ⟦§6.2.6⟧ (1a) — the lane count comes first;
      the old order `vec <element type> <lane count>` is `E-TYPE-ORDER`). `scalable` can also stand in the lane-count position (`vec scalable u8`).
      The lane count shall be a value fixed at translation time (⟦§6.8⟧).

(2) Two `vec`s with different lane counts are different types and do not mix.

(3) Arithmetic on a `vec` happens **per lane**, separately. Values do not cross between lanes.

(3a) The per-lane arithmetic is `add` · `sub` · `mul` · `div` · `mod` · `neg` · `abs` · `min` · `max`. Overflow of integer lanes **wraps** at the lane's width
      (unlike scalars — the quotient of the smallest value of a signed lane divided by −1, and its `neg` · `abs`, are that value itself). If an integer lane's
      `div` · `mod` has a lane whose divisor is 0, it stops as for scalars. The quotient is truncated towards 0, `mod` follows the sign of the divisor
      (⟦§6.3⟧), and `min` · `max` of signed lanes compare by sign (2026-11-02). Floating-point lanes have no `mod` — as for scalars,
      translation is refused (⟦§6.3.3⟧ (2a) · `E-TYPE-KIND`).

> [!참고]
> If the machine can compute several lanes at once it does so; if not, it computes them one by one.
> **Either way the answer is the same** — the lane count is not a performance hint but part of the type.

```lowent
module ex_vec_lanes .

export fn sum4 input xs slice u32 . output u32 .
  requires ge len xs. . 4 . .
do
  let v vec 4 u32 load xs. 0 . .
  return reduce_add v. . .
end .
```

```lowent-거부: the lane count written after the element type · E-TYPE-ORDER
module ex_vec_order .

export fn sum4 input xs slice u32 . output u32 .
  requires ge len xs. . 4 . .
do
  let v vec u32 4 load xs. 0 . .
  return reduce_add v. . .
end .
```

## 6.2.12 Sets made of bits — `bitset`

(1) `bitset <bit count>` holds whether each value from 0 up to, but not including, that count is present.
      The bit count shall be a value fixed at translation time.

(2) Inserting or asking about a value outside the range does not conform.

(3) A `bitset` is **a set**, not the bits of a word. Handling the bits of a word is bit operations
      (⟦§6.3⟧), and the two are different things.

(4) Inserting an element is `bitset_insert <set> <value> .`, removing one is `bitset_remove <set> <value> .` — both change the set
      in place. `add` means only the addition of numbers, so using it on a set does not conform (`E-BITSET-ADD`).

(4a) The number of elements in a set is `bitset_count <set>`, and its type is `usize`. The number of elements of a stack is `stack_count <stack>`.
      Using one in place of the other, or using `len` (the length of a piece) on a set · stack, is refused at translation (`E-TYPE-ARG`).

```lowent 예제: inserting into and removing from a set · 결과: members() = 1
module ex_bitset .

fn members output usize . do
  var s bitset 8 bitset_new 8 . .
  bitset_insert s. 3 .
  bitset_insert s. 5 .
  bitset_remove s. 3 .
  return bitset_count s. . .
end .
```

## 6.2.13 An eye laid over bytes — views

(1) A byte slice can be read, **without copying**, as if it were an array of another type. This is called a view.

(2) When a view is obtained, the alignment and length contract is checked. If the contract is broken, the program stops.

(3) A view **does not copy** bytes. Writing through a view changes the original bytes.

> [!주의] A view counts its length in elements
> The length of a byte slice is the number of bytes, but seen as an array of four-byte elements its length
> is **the number of elements**. Seeing the same storage with two eyes, `len` answers differently — because they are counting
> different things.

(4) The opposite of a view is `encode`. `encode <shape> <value>` **writes** the value **into bytes** in the layout the shape specifies.
      If laying over and reading is `view`, writing down is `encode`.

(5) **A view laid over a struct is a window.** `view <struct> <bytes>` gives a **window** that lays the layout of that struct over a byte slice.
      Reading a field of the window reads the bytes at that place, and writing a field writes the bytes at that place — it is not a copy. The type of a window is
      `view T` (a read window) or `mut view T` (a write window), different from the type `T` of a struct value. `try_view` does the same but gives
      `none` if the length · alignment do not fit or a field does not fit (9a) — its type is `option view T` · `option mut view T`, and `some_value`
      is the window. The conditions on length and alignment are specified by §8.7.2.

(6) **Windows and values do not mix.** Putting a window in the place of a struct name `T` · `option T` — binding · returning · putting a whole struct value
      into a window name — is refused at translation (`E-VIEW-COPY`). A **copy** that lives apart from the bytes is made with `cast T <window> .` (⟦§6.2.16⟧ — an explicit
      conversion is `cast`). Binding a window to another window name sees the same bytes. A window can also be passed to an input that takes a value (`input p T .`) — the receiver
      only reads.

> [!표] Windows and values
> #table(columns: (auto, 1fr), stroke: 0.5pt + rgb("#bbb"), inset: 6pt,
> [*Written*], [*What it is*],
> [`let h view pt view pt b. .`], [a read window over `b`. Reading a field reads `b`],
> [`let h mut view pt view pt b. .`], [a write window over `b`. `set field h. y . 9 .` changes `b` — `b` shall be writable],
> [`let k mut view pt h. .`], [another write window that sees the same bytes],
> [`var k pt cast pt h. . .`], [a struct value that copied the fields of `h`. Writing to `k` leaves `b` as it is],
> [`put h. .` (the input of `put` is `mut pt`)], [the bytes of `b`. If the receiver writes a field, `b` changes — it shall be a write window],
> [`output view pt` · `output mut view pt`], [returns a window that sees the bytes of an input],
> )

(7) **Whether something can be changed is written in its type** (§8.8 (2)). A write window comes only from writable bytes — binding a write window from a shared input,
      a slice bound without `mut`, a string literal, a read window, or the result of an op that gives a read window is refused (`E-TYPE-MUT`). Writing a field through a read window or doing
      `write_volatile` is `E-TYPE-MUT`, passing a read window to a `mut` input is `E-TYPE-ARGMUT`, and returning a window over read-only bytes when the output
      is `mut view T` is `E-TYPE-RETMUT`. If an `fn` writes through a window over bytes it received as `mut` — whether laid directly or returned by another op —
      that is visible to the caller, so it is not pure (`E-EFFECT-PURITY`, §7.3 (6)).

(8) A struct a view can be laid over is a struct **whose byte layout is fixed**. Each field shall be a number of fixed width (`u8`…`u64` · `i8`…`i64` ·
      `f32` · `f64`), a `bool`, an enum with a written width (⟦§6.2.7⟧ (2d)), a fixed array of those, or a struct made of such fields.
      Enums without a written width · slices · `option` · owned-handle fields have no fixed bytes, so neither a view nor `encode` can be used on such
      a struct (`E-IR-UNDEF`).

(9) Every field of a struct read with `view` and `view_array` shall be `plain` (§8.7.1 (2)). A `bool` field that receives a byte other than 0 · 1 becomes
      a value that is neither true nor false, and an enum field that receives a number belonging to no variant becomes a value that is no variant, so if there is even one such field
      (including in inner structs) it is refused (`E-TYPE-BITCAST`) — for the same reason that `bit_cast bool` is refused. `view_array bool`
      is the same. `encode`, which writes values down into bytes, has no such restriction.

(9a) `try_view` **measures** such fields — if the byte of a `bool` field is not 0 · 1, or the number of an enum field is not the number of any variant of that enum,
      it gives `none`. It measures the fields of inner structs too. Reading an enum field of a value obtained that way gives the variant of that number, and writing an enum field puts that
      variant's number into the bytes. If, after measuring, the underlying bytes change by another path so that they are no variant's number, execution stops
      the moment that field is read.

```lowent 예제: writing a field of a view changes the bytes, and binding from name to name copies · 결과: write_through([1,2]) = 9 · copy_by_name([1,2]) = 2
module ex_struct_view .

struct pt do
  x u8 .
  y u8 .
end .

proc write_through input b mut slice u8 . output u8 . effects none . do
  let h mut view pt view pt b. . .
  set field h. y . 9 .     rem b becomes [1,9]
  return idx b. 1 . .
end .

proc copy_by_name input b mut slice u8 . output u8 . effects none . do
  let h view pt view pt b. . .
  var k pt cast pt h. . .  rem a copy that is a struct value
  set field k. y . 7 .
  return idx b. 1 . .      rem b is unchanged
end .
```

```lowent-거부: binding a write window from bytes received read-only · E-TYPE-MUT
module ex_struct_view_ro .

struct pt do
  x u8 .
  y u8 .
end .

proc f input b slice u8 . output u8 . effects none . do
  let h mut view pt view pt b. . .  rem writing a field would change the caller's b
  set field h. y . 9 .
  return 0 .
end .
```

```lowent-거부: binding a window in the place of a struct value · E-VIEW-COPY
module ex_struct_view_copy .

struct pt do
  x u8 .
  y u8 .
end .

fn f input b slice u8 . output u8 . do
  let h pt view pt b. . .  rem a window or a copy? — `view pt` or `cast pt`
  return field h. y . .
end .
```

```lowent-거부: a `bool` field can receive a byte other than 0 · 1 · E-TYPE-BITCAST
module ex_struct_view_bool .

struct flag do
  active bool .
  n u8 .
end .

fn f input b slice u8 . output u8 . do
  let h view flag view flag b. . .
  return field h. n . .
end .
```

## 6.2.14 Parameters with a narrowed range of values — `range`

(1) If `range <low> <high>` is written in the type position of a parameter, that parameter accepts only values between the two ends,
      both included.

(2) If the caller cannot prove that the value is within that range, translation is refused. A value that came from **outside** the program
      (an argument of the entry point and the like) has no caller to prove it, so it is checked **at the boundary**, and
      the program stops if it is out of range.

(2a) If the caller's type is **wider** than the range, that alone is refused (`E-TYPE-WIDTH`). A value of the same width,
      which is not caught by that check, has its range checked **at the op's entry** and stops if it is out of range
      (`E-VM-CONTRACT`) — the same check as writing `requires ge <name> <low> .` and `requires le <name> <high> .`.

(3) A declared range is used with the same standing as a contract of ⟦§6.4⟧ — the checker may take it as a fact
      and remove the checks that follow.

> [!산문]
> (3) **makes** (2a) **necessary**. If the checker takes the range as a fact and removes bounds checks, while
> nobody keeps that fact, values outside the range pass through where the checks were removed. Until 2026-09-16
> a `range` parameter of the same width was checked at no layer (defect note #10) —
> **the only thing to trust is what is checked.**

```lowent-거부: the caller's type is wider than the range · E-TYPE-WIDTH
module ex_range_wide .

fn scale input a range 0 100 . output u8 .
do
  return narrow u8 a. . .
end .

fn call_scale input x u64 . output u8 .
do
  return scale x. . .    rem `u64` is wider than 0 … 100 — it was not shown to be within the range
end .
```

> [!참고]
> The same thing can be written with `requires` too. The difference is **where it is written**. Written in the type position,
> the range becomes the **shape** of the parameter, and the caller knows it from the signature alone.
> Written as a contract clause, one has to read the contract to know it.

## 6.2.15 Nailing down the byte layout — `layout` and `align`

(1) A layout can be written inside a struct declaration. If none is written, fields are placed **in declaration order**, each aligned to
      its type's alignment (with as much padding between as alignment requires). The processor does not change the order.

(2) `layout packed .` **puts no padding** between fields — the fields are packed together in declaration
      order.

(2a) The layout words are a closed two — `packed` and `native`. Anything else is refused
      (`E-TYPE-LAYOUT`).

(3) `align <n> .` nails down the alignment of the place where that struct begins. n shall be a power of 2.
      The alignment shall be **a power of 2** (`E-TYPE-ALIGN`) — no machine can
      keep any other number.

> [!용어] 표지 (marker)
> A word attached after a field that fixes **how that field is placed and how it can be reached**.
> It is not a type, so it does not change the size of the value; it fixes only the byte order and the way of reaching.

(4) A ⟦표지|marker⟧ can be attached after a field. The markers are these five and no more; writing any other
      word is refused at translation (`E-FIELD-MARK`).

> [!표] Field markers — a closed set of five
> #table(columns: (auto, auto, 1fr), stroke: 0.5pt + rgb("#bbb"), inset: 6pt,
> [*Marker*], [*What it fixes*], [*Meaning*],
> [`big`], [byte order], [placed **big-endian**],
> [`little`], [byte order], [placed **little-endian**],
> [`rw`], [way of reaching], [can be read and written],
> [`ro`], [way of reaching], [**read only** — writing is refused at translation],
> [`wo`], [way of reaching], [**write only** — reading is refused at translation],
> )

(4a) If no byte order is written, the machine's order is followed (⟦§5.6⟧).

(4b) The way of reaching is enforced only in **structs that mirror device registers** (⟦§7.6⟧). A violation is refused
      **at translation**, not at execution (`E-MMIO-PERM`).

> [!산문]
> Why is reading `wo` an error? Reading a write-only register yields garbage, or the act of reading
> itself moves the device. That is, that read is **not useless but wrong**.
> Once the device has said what it accepts, the type says it back.

```lowent 예제: a header going out on the wire — no padding, big-endian
module ex_layout .

struct wire_header do
  layout packed .
  magic u32 big .
  length u16 big .
  kind u8 .
end .

fn hdr_kind input b slice u8 . output u64 . do
  let v view wire_header view wire_header b. . .
  return widen u64 field v. kind . . .
end .

```

> [!참고]
> **Nailing the layout down and not nailing it down buy different things.** Not nailed down, the processor can choose to fit
> the machine, and it is fast. Nailed down, those bytes have **the same meaning outside too** — what goes out on a wire
> or is read by a device needs that. So nail it down only where needed.

## 6.2.16 Explicit conversion — `cast`

(1) **Reading a value as another type happens only where the source says so.** The only conversion the processor does by itself is widening (⟦§6.2.5⟧ (2));
      every other conversion is written by the author with a word. The centre of those words is `cast <type> <value>` — **`cast` is the word that takes charge of
      explicit conversion in this language.** `widen` is used only where **the value does not change**, and conversions that may change the value are written with `cast` —
      places that change the sign, cross kinds, or narrow.

(1a) There are three words that write conversions, each with its own place.

> [!표] The three words that write conversions
> #table(columns: (auto, 1fr, auto), stroke: 0.5pt + rgb("#bbb"), inset: 6pt,
> [*Word*], [*Its place*], [*If the value does not fit*],
> [`widen`], [widening in which the value fits **as it is** (the table of ⟦§6.2.5⟧ (4)). The processor also does it by itself — writing it makes it visible], [does not happen],
> [`narrow`], [reduces the width within the same kind (integer → narrower integer)], [traps],
> [`cast`], [**every other conversion** — changing the sign · between integers and floating point · between `usize` · `idiff`, whose width depends on the implementation, and other integers · truth value → integer · enum ↔ integer], [traps],
> )

(1b) **A conversion that could be read either way does not happen by itself; it goes through `cast`.** That is a conversion where the same bits could be read as two numbers,
      or that widens or narrows depending on the target machine, or that changes the kind of the value. This rule also applies to types and conversions
      that enter the language in the future — if a new conversion is ambiguous its spelling is `cast`, and nothing is decided in the direction of adding implicit conversions.

> [!산문]
> When conversions are written in the source, a reader **can find every place where a value may change with their own eyes.** Having found and read the `cast`s, they have seen every place
> in that program where a type changes. If there were even one place where the processor changed it secretly, that promise would be broken. So this language has no implicit
> conversions for convenience. Writing them takes a little effort, but when reading there is no guessing «what changed here».

(2) `cast` **traps if the value does not fit the target type.** It is not truncated silently.
      That is, `cast` is not a mark that says «anything may happen» but a mark that says *"I know this is a place where the value
      may change"*.

(3) Converting a floating-point number to an integer **truncates towards 0** — `7.9` becomes `7` and `-2.9` becomes `-2`.
      Only the fractional part is discarded; if the integer part does not fit the target type, it traps as in (2).

> [!주의]
> Discarding the fractional part **does not trap.** So `cast` does not violate *"there is no implicit conversion that loses a value"*
> of §6.2.5 (1) — because that conversion **is not implicit.**
> The author wrote `cast`, and that word itself is the mark that says «the value may change here».

(3-1) **A `cast` to a floating-point type** — whether from an integer or from another floating-point type — gives **the nearest value** in the target type,
      and when two are equally near, the one whose last digit is even. Rounding happens **once** — converting an integer to `f32` does not go through `f64` and round
      twice. A value that rounds beyond the largest finite value of the target type becomes the infinity of the same sign, and NaN is NaN.
      This conversion does not trap as in (2) — a floating-point type has an infinity for an overflowing value to go to. From `f32` to `f64` the value is
      unchanged. `cast f32 <f64 1e300>` is `+∞`, and `cast f32 <u64 16777217>` is `16777216`.

(3a) **Between `usize` · `idiff` and other integer types** the only crossing is `cast` (⟦§6.2⟧ (3-3)). This conversion is both a widening and a narrowing depending on
      the implementation's width, so `cast` is its spelling — if the value fits it is given as it is; if not, it traps as in (2). On an implementation where the width is the same or
      grows, this `cast` leaves no check.

(4) **From a truth value to an integer** goes by `cast` — `cast <integer type> <truth value>` is `1` for true and `0` for false. That value fits
      every integer type, so the trap of (2) does not happen. A floating-point type is not a target of this conversion (`E-TYPE-KIND`).

(4a) There is no `cast` **from an integer to a truth value** — `bool` cannot be the target of `cast` (`E-TYPE-KIND`). Write what is being asked:
      `ne n. 0 .` or `eq n. 0 .`. Which values of the number are true is decided by that question.

(4b) Truth values are not in the table of `widen` (⟦§6.2.5⟧ (4)) — `widen <integer type> <truth value>` is refused (`E-WIDEN-KIND`). The spelling that turns a truth value
      into a number is `cast` alone.

```lowent 예제: a truth value as a number · 결과: big(5) = 1 · big(1) = 0 · count_big(5, 7) = 2
module ex_bool_cast .

fn big input a u64 . output u8 . do
  return cast u8 gt a. 2 . . .
end .

fn count_big input a u64 . input b u64 . output u64 . do
  return add cast u64 gt a. 2 . . cast u64 gt b. 2 . . . .
end .
```

```lowent-거부: there is no cast that turns a number into a truth value · E-TYPE-KIND
module ex_cast_to_bool .
fn nonzero input n u64 . output bool . do
  return cast bool n. . .     rem write what is being asked — `ne n. 0 .`
end .
```

(5) **An enum value is not a number.** Putting an enum value in a place that takes a number — a name · field · argument · return declared as a number, arithmetic, comparison with numbers, indices, bit operations, widening ·
      narrowing — is refused at translation (`E-TYPE-KIND` · `E-TYPE-LET` · `E-TYPE-RETURN` · `E-TYPE-ARG` · `E-WIDEN-KIND`), and putting a number in a place that takes
      an enum is refused too, literals included. If the variant's number (⟦§6.2.7⟧ (2c)) is needed, write `cast <integer type> <enum value>` —
      if the number does not fit that type, it traps as in (2). Two values of the same enum can be compared with `eq` · `ne`, and with `lt` · `le` · `gt` · `ge`
      — the magnitude is the order of the numbers, and the numbers increase in declaration order, so it is the same as the order of declaration. Values of different enums are not compared (`E-TYPE-KIND`).

(5a) **From a number to an enum** goes by `cast <enum> <integer>` — it gives the variant with that number. If the number belongs to no variant, it **traps**
      (`E-VM-CAST`). This is the place where a number read from a file or a wire is turned back into an enum. The source shall be an integer (`E-TYPE-KIND`).

(5b) An enum with even one variant that carries values (⟦§6.2.19⟧) can be neither the source nor the target of `cast` (`E-TYPE-KIND`). One number cannot say
      everything about such a value — which variant it is is asked with `isa` · `match`, and the value is built with `<enum>.<variant> …`.

```lowent 예제: a variant's number is read with cast · 결과: code_of_blue() = 3 · same() = 1
module ex_enum_cast .

enum color do
  red .
  green .
  blue .
end .

fn code input c color . output u8 . do
  return cast u8 c. . .
end .

fn code_of_blue output u8 . do
  return code blue. . .
end .

fn same output u8 . do
  return cast u8 eq green. green. . . .
end .
```

```lowent-거부: giving an enum value as a number · E-TYPE-RETURN
module ex_enum_as_number .

enum color do
  red .
  green .
end .

fn code input c color . output u8 . do
  return c. .     rem a variant's number is read with `cast u8 c. .`
end .
```

```lowent 예제: turning a number read back into an enum and filtering by range · 결과: kind_of(12) = 1 · kind_of(21) = 2 · round_trip(102) = 102
module ex_enum_from_number .

enum item do
  weapon_start 10 .
  sword .
  stick .
  weapon_end .
  tool_start 20 .
  hammer .
  tool_end .
end .

rem 1 = weapon · 2 = tool — values of the same enum are compared by magnitude
fn kind_of input n u8 . output u8 . do
  let it item cast item n. . .
  if and gt it. weapon_start. . lt it. weapon_end. . . do return 1 . end .
  if and gt it. tool_start. . lt it. tool_end. . . do return 2 . end .
  return 0 .
end .

enum hangul do
  ga 11 .
  na .
  da 101 .
  ra .
end .

fn round_trip input n u32 . output u32 . do
  return cast u32 cast hangul n. . . .
end .
```

```lowent-거부: comparing values of different enums · E-TYPE-KIND
module ex_enum_cross_order .

enum color do
  red .
  green .
end .

enum size do
  small .
  large .
end .

fn f input c color . input s size . output bool . do
  return lt c. s. . .
end .
```

```lowent-거부: doing arithmetic with an enum · E-TYPE-KIND
module ex_enum_arith .

enum level do
  low .
  high .
end .

fn next input l level . output u8 . do
  return add l. 1 . .
end .
```

## 6.2.17 What holds characters — a string is not a type

(1) This language **has no string type.** What holds characters is a piece of bytes (`slice u8`),
      and that is all.

(2) So **the length is the truth.** A mark that signals the end of a string (a null byte) is not part of the value,
      and scanning the bytes to count the length does not happen.

> [!산문]
> A null-terminated string is a design that **hides the length inside the value**. So to know the length one has to scan,
> and if the mark is missing while scanning, what lies beyond is read — in practice the most common memory accident came from here.
> A piece **carries its length outside**, so that accident has no place to stand.

(3) The null-terminated shape is needed only where C is met (⟦§6.9.2⟧). It is **a shape built at the boundary**,
      not a type this language has.

(4) Handling characters — cutting · searching · joining · Unicode checks — is **the work of libraries**
      (⟦§9.5⟧). The processor knows only pieces and bytes.

## 6.2.18 Type names without meaning

(1) Some names **are accepted but have no meaning.** If such a name is written in a signature, the processor
      warns (`W-NOT-YET`). Those names are gathered in annex A.9 (2) — names such as `byte` · `char` · `str` ·
      `string` · `bytes_view` · `dyn` · `atomic`.

(2) Having no meaning means that **the processor does not know the shape of the values that name holds**. Neither the size, the layout,
      nor what can be done with the value is fixed, so checks that rely on that name **do not stand**.

> [!주의]
> This is why this place **shall speak**. Translation passes and answers come out, so the checks that did not stand
> show nowhere. **What silently dropped out, nobody finds** — so the processor says so.

(3) There are two ways to give a name meaning: **write the underlying type directly**, or declare that
      name (`type str option u64 .`). Once declared, it has a meaning, so there is no warning.

```lowent 예제: a name without meaning is warned about but not refused · 결과: f() = 1
module ex_name_only .

rem `str` is not built in — without a meaning it gets W-NOT-YET. Here it is given a meaning by declaring it.
type str option u64 .

fn f input s str . output u8 . do return 1 . end .
```

## 6.2.19 Variants that carry values — building and taking apart

(1) The variants of an `enum` can carry values. When writing a variant, the names and types of what it carries are written
      with it.

(2) The shape that builds such a value is `<enum>.<variant>`, and it gives **all** of what the variant was declared to carry.
      If something is missing it is refused (`E-ENUM-ARITY`) — a half-built value is not made silently.

(3) There are two ways to handle an enum that carries values.

> [!표] The two ops that handle variants carrying values
> #table(columns: (auto, auto, 1fr), stroke: 0.5pt + rgb("#bbb"), inset: 6pt,
> [*op*], [*Shape*], [*What it does*],
> [`isa`], [`isa <value> <variant>`], [asks whether it is that variant now],
> [`payload`], [`payload <value> <variant> <name>`], [reads what that variant carries],
> )

(4) Naming a variant that does not exist is refused (`E-ENUM-NOVARIANT`), and trying to read a name that variant does not carry
      is refused (`E-ENUM-NOFIELD`). Both are decided **at translation** — the variant and
      the name are written in the source, so there is no reason to wait for execution.

(5) In an enum with several variants, `payload` can be used **only after it has been narrowed to that variant**
      (`E-ENUM-UNCHECKED`). What narrows is `isa`, usually written as `guard isa <value> <variant> else …`.

(5a) Using `payload` at a place already narrowed to **another variant** is refused too
      (`E-ENUM-VARIANT`) — at that place it has already been proven to be another variant, so that `payload`
      **reads the wrong thing.**

(5b) The type of what a variant carries is **written as one word** (`E-ENUM-PAYLOAD`). A variant line has no mark that separates fields,
      so for types written as several words (`slice u8` · `option t` · `owned t` · `ref t`) where they break would not be
      fixed. This is a restriction of spelling, not of size — the size of the carried value is not asked about.
      A multi-word type is written by naming a struct that has it as a field and using that name.

> [!산문]
> What (5) does is prevent «reading what is not there». Reading without narrowing is right only when the value is that
> variant, and when it is not it reads **unrelated bytes** — that is not an answer being wrong but an answer that is
> **right by chance**, the shape this language dislikes most.

```lowent 예제: a multi-word type is carried through a named struct · 결과: f([1,2,3]) = 3 · g() = 10
module ex_enum_named_payload .

struct bytes do
  b slice u8 .
end .

struct pt do
  x u64 .
  y u64 .
  z u64 .
end .

enum msg do
  data d bytes .
  dot p pt .
  blank .
end .

fn size_of_data input m msg . output usize . do
  guard isa m. data . else return 0 . .
  let d bytes payload m. data d . .
  return len field d. b . . .
end .

export fn f input s slice u8 . output usize . do
  let m msg msg.data lit bytes do b s. . end . . .
  return size_of_data m. . .
end .

export fn g output u64 . do
  let m msg msg.dot lit pt do x 1 . y 2 . z 7 . end . . .
  guard isa m. dot . else return 0 . .
  let p pt payload m. dot p . .
  return add add field p. x . field p. y . . field p. z . . .
end .
```

(6) If an enum carries itself as a value, its size is not fixed, so it is refused
      (`E-ENUM-INFINITE`). A recursive structure is built by **putting something in between**.

> [!산문]
> A variant that carries values is not a new construct. There is just one field that indicates the variant and an aggregate
> holding what the variant carries; building is building an aggregate, and reading is reading a field. So this feature
> **added not a single machine instruction** — it lowers to what already existed, so it ran on both back ends
> by itself.

## 6.2.20 Seeing scattered pieces as one — `segments`

(1) Several pieces that are not contiguous can be seen with **one eye**. It consists of two things: the bytes lying behind,
      and a descriptor that says where in them and how much.

> [!표] The ops that handle scattered pieces
> #table(columns: (auto, auto, 1fr), stroke: 0.5pt + rgb("#bbb"), inset: 6pt,
> [*op*], [*Shape*], [*What it does*],
> [`view_segments`], [`view_segments <bytes> <descriptor>`], [binds the two into one eye],
> [`segview.piece_count`], [`segview.piece_count <eye>`], [counts how many pieces there are (standard module `segview`)],
> [`segview.piece_at`], [`segview.piece_at <eye> <number>`], [gives the piece with that number as a piece (slice) (standard module `segview`)],
> )

(1a) An eye is an aggregate holding the fields `back` (the bytes behind) and `descs` (the descriptor). The number of pieces and the piece of a number are
      written by reading and cutting those fields, so they are not operations of the language but ops of the standard module `segview` (⟦§9.3⟧).

(2) A descriptor is a piece holding pairs of «from where» and «how much». So these three **add neither a new type
      nor a new machine instruction** — they lower to building an aggregate, reading fields and cutting.

> [!산문]
> Without a way to handle scattered pieces, the user has no choice but to **gather them into one place**.
> Gathering is copying, and copying where it need not be done is the cost this language sets out to remove.
> Given an eye, the copying disappears.

## 6.2.21 Fetching ahead — `prefetch`

(1) `prefetch <piece> <number>` tells the machine that the bytes at that place **will be used soon**.

(2) This is **only a hint**. It leaves no value and does not change the meaning of the program,
      and if the machine ignores it, the answer is the same.

> [!주의]
> A hint is a hint only if **being wrong does not make the program wrong**. Something that changes the meaning is not a hint but
> a command, and such things do not come in this place.

## 6.2.22 What it means for types to match

(1) In every place where a value is put, the type of the value shall be **the same** as the type that place states.

> [!표] Places where types do not match
> #table(columns: (auto, 1fr), stroke: 0.5pt + rgb("#bbb"), inset: 6pt,
> [*Diagnostic*], [*Where*],
> [`E-TYPE-LET`], [binding with `let` — the value given differs from the type written],
> [`E-TYPE-VAR`], [binding with `var` — the same mismatch, at a different place],
> [`E-TYPE-SET`], [rewriting — the new value differs from the type of that name],
> [`E-TYPE-RETURN`], [returning — what is returned differs from what the op states],
> [`E-TYPE-ARG`], [passing — an argument differs from the parameter],
> [`E-TYPE-FIELD`], [**making** — a field that does not exist is given, a field's value differs from its type, or a field is missing],
> [`E-TYPE-TRY`], [`try` — a value with nothing to strip off],
> )

(2) Aggregates and enums are declared with `struct <name> do … end .` · `enum <name> do … end .`. Trying to declare them in another shape
      is refused (`E-TYPE-DECL`).

(3) Even with the same field layout, **different names are different aggregates** (`E-TYPE-STRUCT`, ⟦§8.9⟧).

> [!산문]
> The seven in this table are all different places of the same sentence — the source wrote *"the type of what comes here is this"*,
> and the processor compares what is written with what actually comes. There is a separate diagnostic for each place because
> **where it mismatched** is the fact the person fixing it needs most.

## 6.2.23 The shape of writing fields

(1) A field of an aggregate is **a name followed by a type** (`x u64 .`). Anything not in that shape is refused
      (`E-FIELD-FORM`).

(2) A field is read with `field <value> <name> …`. The old spelling that reads with an attached dot does not exist
      in this language (`E-FIELD-GLUED`).

(3) A `vec` and a mask are **values themselves**, and pointing or borrowing markers cannot be attached to them
      (`E-VEC-QUAL`). Their lanes are positions inside a word, not positions in memory.

```lowent-거부: a field is a name followed by a type · E-FIELD-FORM
module ex_field_form .

struct p do
  x .              rem there is no type
end .

fn f output u8 . do return 1 . end .
```

## 6.2.24 Integers of arbitrary width — `bits`

(1) `type <name> bits <number> .` makes an integer type with that many bits. The width is
      **from 1 to 64**.

(2) The width need not be a power of 2 — `bits 3` and `bits 10` both stand.

> [!주의]
> If the width does not match the machine word, **speed is not guaranteed.** What this language guarantees is
> **correctness** — `bits 10` gives the answer computed in ten bits, but it does not say that this is faster than
> a sixteen-bit computation. To avoid making places whose cost is invisible, what is not guaranteed **shall be written down as not
> guaranteed**.

