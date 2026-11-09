## 6.3 Expressions and operations

(1) An ⟦식|expression⟧ is something that produces a value. A literal is an expression, a name is an expression,
      and the application of an operation is an expression.

## 6.3.1 Prefix notation is the default

(1) Expressions are written in ⟦전위 표기|prefix notation⟧ — the name of the operation comes first and the arguments
      follow.

(2) Prefix notation **has no precedence rules.** What is computed first is said entirely by the dots and the order
      — a form is opened by a name and closed by its own dot (⟦§6.1.5⟧), and the inner form closes first.

(2a) Nesting calls needs no parentheses — in `add 1 mul a. b. . .` the first dot closes `mul` and the second
      closes `add`. The end of a form is decided by **that form's dot**. The number of arguments an operation takes does not decide
      the end — whether the number of arguments is right is looked at separately after the form has closed (`E-IR-ARITY` if not).

(2b) Parentheses are **decoration that makes a grouping stand out** — `add 1 (mul a. b. .) .` is the same expression as that of (2a).
      A form inside parentheses closes with its own dot too, and the parentheses do not change the meaning.

(2c) **The order in which operands are computed is a ⟦자유 선택|free choice⟧** (⟦§4.3⟧). The processor may compute the operands of an operation
      in any order. Core ops · builtin ops and ops the author declares are the same. But the following are fixed.
      - The operation is done **after all** its operands have been computed.
      - Each operand is computed **only once**.
      - **Operands that produce effects are computed in the order written, from the left.** Here an effect means an effect other than `panic` (⟦§7⟧) —
        being able to stop does not by itself bind the order.
      - `and` and `or` are the exception — they compute the first operand first, and if that alone decides the result, they do not compute
        the second operand (⟦§6.3.3⟧ (4)).

> [!자유] FC-0001 — the order in which operands are computed
> The processor may compute, in any order, those operands of an operation **that produce no effect**. That order is the only range of choice — computing all operands before the operation, computing each once, and computing effect-producing operands in the order written are fixed.
> In most programs the answer is the same whatever the order. There is one time the difference shows — when two operands stop for different reasons, **which stop occurs**.
> If that difference matters, put what is to be computed first into a name with `let` — statements are executed in the order written.

```lowent 예제: prefix notation
let a u32 add 1 2 . .
let b u32 add 1 mul 2 3 . . .
let c bool lt a. b. . .
```

> [!산문]
> Someone seeing `1 + 2 * 3` for the first time has to **memorise** whether `*` or `+` comes first.
> `add 1 mul 2 3 . .` has nothing to memorise — the dots say it as it is. That is why this language takes prefix
> as the default.

## 6.3.2 Infix notation — the `expr` island

(1) Long arithmetic is hard to read, so at a place that begins with `expr` ⟦중위 표기|infix notation⟧ can be used. This place is called an ⟦섬|island⟧.

(2) The notation inside an island turns into **exactly the same meaning as prefix**. An island is only a projection of notation
      and has no execution cost.

(3) Precedence exists **only inside islands**. The following table is all of it.

> [!표] Precedence inside an `expr` island (strongest at the top)
> #table(columns: (auto, auto, auto, 1fr), stroke: 0.5pt + rgb("#bbb"), inset: 6pt,
> [*Level*], [*Operation*], [*Associativity*], [*Prefix with the same meaning*],
> [5], [`( )` grouping], [—], [only a grouping mark, not a value],
> [4], [`*` `/`], [left], [`mul` · `div`],
> [3], [`+` `-`], [left], [`add` · `sub`],
> [2], [`eq` `ne` `lt` `le` `gt` `ge`], [**cannot be chained**], [the prefix operation of the same name],
> [1b], [`and`], [left], [`and` (short-circuit)],
> [1a], [`or`], [left], [`or` (short-circuit)],
> )

(4) `and` binds more strongly than `or`. That is, `expr a or b and c` is the same as `or a. and b. c. . .`.

(5) Comparisons **cannot be chained.** `expr a lt b lt c` is refused.

(6) The table above is **all** of the island. This set is closed and does not grow. Operations not in the table
      — including bit operations (⟦§6.3.5⟧) · the remainder `mod` · the minimum `min` and maximum `max` —
      are **written in prefix** even inside an island.

(7) There are no unary operations in an island either. To flip a sign, write `expr 0 - a`, or wrap a prefix form in
      parentheses (`expr (not b. .) or c. .`).

> [!주의] The island is small not because it lacks something
> The only value of infix notation is **readability**, and that holds only when **nobody has to look up**
> the precedence. The twelve in the table are the same as the arithmetic learned at school, and everyone reads them the same way —
> nobody disputes that multiplication binds more strongly than addition.
> Bit operations are exactly the opposite. In C, `a & b == c` does not group as `(a & b) == c` but as
> `a & (b == c)`. When `a` · `b` · `c` are all 6 the former is true and the latter
> false — the answers diverge. This is a trap that catches even people who have written C for a long time, and many languages
> fix this precedence differently from one another.
> So putting bit operations in the island **would not make it easier to read but would increase what has to be looked up.**
> Prefix notation has no precedence at all (⟦§6.3.1⟧), so the parentheses say the answer —
> it gets longer but **cannot be wrong.** This language chooses not the shorter side but the side that cannot be wrong
> (⟦§1.3⟧).

> [!산문]
> A notation projection exists to make things easier to read, not to give every operation a **second grammar**.
> If there are two ways of writing the same meaning, a reader has to know both — that cost is greater
> than what is gained by shortening.

```lowent 예제: infix notation
module ex_infix .

export fn score input a u32 . input b u32 . output u32 .
  requires le a. 1000 . .
  requires le b. 1000 . .
do
  return expr a. + b. * 2 . .
end .
```

```lowent-거부: comparisons cannot be chained · E-EXPR-CHAIN
module ex_chain .

proc p input a u32 . input b u32 . input c u32 . output bool . effects none .
do
  return expr a. lt b. lt c. . .     rem two comparisons cannot be chained
end .
```

> [!주의] Why comparisons cannot be chained
> In mathematics `a < b < c` means «a is less than b, and b is less than c». But in many
> languages that notation is read with **a different meaning** (first `a < b` is computed to get a truth value, and that is
> compared again with `c`). If the same characters mean different things to people and to machines, that is a place that
> silently goes wrong. So this language refuses it outright.

## 6.3.3 Core ops and builtin ops

(1) The operations whose meaning the language fixes are of two kinds. A ⟦기본 연산|core op⟧ is called anywhere by its name alone and shares
      **one namespace** with the names the author makes. A ⟦빌트인 연산|builtin op⟧ is called only after `call_builtin` and has **its own
      namespace** ((1c) · (1f)). For both kinds the list of names is fixed, and the whole of it is in annex D. Annex D also states, for each operation, **what it takes and what it gives, when translation
      is refused and when execution stops**.

(1a) Not all the names of core ops have the same rule. Most cannot be used as local
      names (⟦§6.1.3⟧), but some are operations **only within a context**, and outside it they can be
      the author's names. Which is which is stated by annex D.

(1b) What is listed below **shows the basic ones by kind**; it is not all of them. The full list and
      the meaning of each operation are in annex D.

(1c) A builtin op stands only after the head word `call_builtin`: `call_builtin <name> <operand>*`. Its names form
      **a closed set**, and that is the only place it is called. Which names are such is stated by annex D.
      Those that reach the outside world — files · the network · processes (`file_*` · `dir_*` · `path_remove` · `path_rename` · `link_type` · `net_*` · `proc_*`),
      standard input and output (`write_out` · `read_in`), arguments and the environment (`arg` · `env_get`), clocks (`time_now` · `time_local` · `time_sleep`),
      terminals (`tty_raw` · `tty_read` · `tty_size`), random numbers (`random_bytes`), the reactor (`reactor_new` · `r_read` · `r_write`) — take
      a capability as their first operand: `call_builtin net_send <capability> <handle> <bytes>`.

(1c-1) Builtin ops that take no capability are **computations** — floating-point functions (⟦§6.3.9⟧), the sum of a piece (⟦§6.3.7.1⟧), and
      hashing · cryptography · carry-less multiplication · random sequences (`hash_bytes` · `crc32` · `sha256` · `sha384` · `sha512` · `aes_round` ·
      `aes_round_last` · `aes_ctr` · `ghash` · `aes_gcm` · `chacha20` · `poly1305` · `chacha_poly` · `clmul_lo` · `clmul_hi` ·
      `mul_hi` · `rng_next`).

(1c-2) The standard library provides ops that wrap these builtin ops (⟦§9.3⟧ (1a)) — `stdio` · `args` · `env` · `clock` · `tty` ·
      `random` · `reactor` · `math` and so on. A wrapper passes its operands on as they are, so the meaning is the same.

(1f) **The namespace of builtin ops is separate from the author's namespace.** The word directly after `call_builtin` is always read as the name of a builtin
      op, and the same spelling in any other place is always read as the author's name. So the author can
      give a spelling the same as a builtin op to their own op · parameter · local, and can use both together in one unit.
      Since the place where a word stands decides which it is, a spelling never has two meanings in one place.

> [!산문]
> Builtin ops are **not meant for ordinary programs to use.** They are doors the processor selects and opens for what cannot be written as a library —
> computations that go down to machine instructions, work that reaches the outside world, things only the processor knows.
> Those who use these doors are the people building system libraries and the people building processors; application programs call the ops of the standard library
> that wrap them (⟦§9⟧). That is also why the namespace is separate — a library must be able to provide its wrappers under natural
> names such as `sha256`.

> [!산문]
> Why confine them to a place? If an operation a program uses **once** enlarges everyone's vocabulary, the names a reader must memorise
> grow. That cost is paid not by the one who writes but by **everyone who reads**. For the same reason
> pipe stage names have meaning only inside `pipe` (⟦§6.12⟧), and type words stand only in their own places.

(1d) If a word called by its bare name is not among the author's names and has the spelling of a builtin op, the processor regards it as having
      left out `call_builtin` and refuses it (`E-BUILTIN-BARE`). Putting a word outside that closed set after `call_builtin` does not conform either
      (`E-BUILTIN-NAME`) — that place does not silently pass over to the author's names.

```lowent-거부: a builtin op cannot be called by its bare name · E-BUILTIN-BARE
module ex_bare .

proc p input s slice u8 . input o mut slice u8 . output u64 . effects none .
do
  return sha256 s. o. . .
end .
```

```lowent 예제: written to fit its place, the same thing stands
module ex_call .

proc p input s slice u8 . input o mut slice u8 . output usize . effects none .
do
  return call_builtin sha256 s. o. . .
end .
```

```lowent 예제: the author's `sha256` and the builtin op `sha256` stand together in one unit
module ex_both .

fn sha256 input x u64 . output u64 . do
  return add x. 1 . .
end .

proc p input s slice u8 . input o mut slice u8 . output u64 . effects none .
do
  let n usize call_builtin sha256 s. o. . .
  return sha256 cast u64 n. . . .
end .
```

(1e) There are three ways of keeping names whose meaning the language fixes out of the global vocabulary.
      - **Head** — the name position of `call_builtin <name>` ((1c)). Builtin ops are such.
      - **Prefix** — a group of ops shares the same leading part: `bitset_*` (sets of small numbers, ⟦§6.2.12⟧) ·
        `lane_*` and `reduce_*` (lanes) · `atomic_*` · `wrap_*` · `sat_*` · `chk_*` · `narrow_*`.
        Prefixed names are global names, but since the group does not occupy short words (`union` · `remove` · `select` · `any` …),
        the author can use those words as their own names.
      - **Place** — words that have meaning only inside a fixed context. The stages and terminals of `pipe` are such (⟦§6.12⟧):
        the terminals `any` · `all` are words inside `pipe`, and the lane predicates are `lane_any` · `lane_all`.
      An op has one spelling. When a name is moved, the old spelling is not left behind as a synonym.

(2) The arithmetic operations are `add` `sub` `mul` `div` `mod`. The spelling of the operation that gives a remainder is `mod`,
      and `rem` is **the word that opens a comment** (⟦§6.1.2⟧) — the same spelling is not used for two meanings.

(2a) `mod` is the remainder **of integers**. The remainder of floating-point numbers is `fmod` (⟦§6.3.9⟧), and using `mod` on floating-point operands is
      refused at translation (`E-TYPE-KIND`, 2026-11-02 — before that it passed the check and stopped during execution).

(3) The comparison operations are `eq` `ne` `lt` `le` `gt` `ge`, and their result is `bool`.

(3a) A comparison between integers follows the same rule as arithmetic — the two operands shall be comparable by widening, and if they cannot be, translation is
      refused (`E-TYPE-SIGN`, ⟦§6.3.4⟧ (1)). The processor does not secretly change signs for a comparison. To compare the **mathematical values** of two integers
      even when their signs or widths differ, call `cmp_eq` · `cmp_lt` · `cmp_le` of the standard library `intops` by name (⟦§9⟧) —
      `intops.cmp_lt i64. u64. a. b. .` is true when `a` is −1 and `b` is 0. Those ops take any pair of integer types
      (`usize` · `idiff` · 128 bits included).

(4) The logical operations are `and` `or` `not`, and their operands and results are all `bool`. `and` and `or`
      ⟦단락 평가|short-circuit⟧ — if the first part alone decides the result, the second is not computed.

(5) The slice operations are `len` `idx` (⟦§6.2.6⟧).

(6) The conversion operations are `widen` `narrow` (⟦§6.2.5⟧).

(7) The bit operations are in ⟦§6.3.5⟧.

> [!자유] FC-0002 — when NaN takes part in min and max
> In `min` · `max` of floating-point numbers, if one operand is NaN, the processor gives the value of the side that is not NaN, or gives NaN. Which of the two is chosen by the processor.
> If neither operand is NaN, the result is uniquely fixed. Machine min · max instructions answer differently at this place, so nailing it to one side would slow down what one instruction could finish.

(8) `neg` flips the sign. The operand shall be **a signed integer or a floating-point number** — an unsigned integer has
      no negative numbers, so it is refused at translation (`E-TYPE-SIGN`). The type of the result is the type of the operand.

(8a) `neg` is arithmetic too. If the result does not fit its width, it stops (⟦§6.3.4⟧ (3)) — the smallest value of a signed type is
      that case (−128 of `i8`).

> [!산문]
> The names may look unfamiliar, but the rule is one — **words instead of symbols**. `+` is `add`,
> `<` is `lt` (less than), `==` is `eq` (equal). Symbols are not used for two reasons:
> symbols differ subtly in meaning from language to language (for example `^` is exponentiation in some places and exclusive or in others),
> and a notation that can be read out as words carries over as it is to voice input and reading aloud.

## 6.3.4 The rules of arithmetic

(1) The two operands of binary arithmetic shall be **comparable by widening** (⟦§6.2.5⟧).
      If they are not comparable, translation is refused. If only their widths differ, they are computed at the wider one.

(1a) Even with mixed signs, it can be computed **if there is a widening that keeps the value**. It is enough that the unsigned side fits
      in the result type without losing its value — `u8` and `i16` can be mixed.

(1b) If there is no such widening, translation is refused. `u32` and `i32` have the same width, so the unsigned side cannot
      fit in the signed side, and they cannot be mixed.

(2) The type of the result is the **wider** of the two operands. The processor does not invent a third type that is
      neither of the two operands.

(3) Arithmetic happens **at the declared width**. If the result does not fit that width, it **traps**.

(4) Dividing an integer by 0 traps. Dividing a floating-point number by 0 gives infinity, as
      IEEE 754 specifies.

(5) `div` and `mod` are interpreted **with the declared sign**.

(5a) `div` **truncates the quotient towards 0** — `div -7 2 .` is −3.

(5b) `mod` is **the remainder of floored division**. The sign of the remainder **follows the divisor** — `mod -7 2 .` is 1, `mod 7 -2 .` is −1.
      So when the divisor is positive the result is always 0 or more and less than the divisor: if `0 < n` then `0 ≤ mod i n < n`,
      even when `i` is negative.

(5c) So `div` and `mod` **are not a pair.** `(div a b) × b + (mod a b)` equals `a` only when the two numbers have the same sign
      or divide evenly. The remainder that pairs with the quotient truncated towards 0 is written `sub a. mul div a. b. . b. . .`.

(5d) `mod` of the smallest value of a signed type by −1 is 0. `div` stops at the same place (the quotient is not in that width),
      but the remainder 0 is within that width.

> [!산문]
> Why does `mod` follow the sign of the divisor? The place where a remainder is used is almost always **choosing a position** — the slot of a ring
> buffer, the slot of a hash table, the day of the week. The property needed there is «the result is 0 or more and less than the size», and only the remainder
> of floored division keeps it even for negative numbers. C's `%` gives −1 for `-7 % 2`, so that property breaks.

```lowent 예제: even with mixed signs, a widening that keeps the value is enough · 결과: ok_widen(200, -100) = 100 · ok_wider(200, -100) = 100
module ex_mixsign .

rem `u8` fits in `i16` without losing its value.
fn ok_widen input a u8 . input b i16 . output i16 . do return add a. b. . . end .

rem A wider place may be chosen for the result.
fn ok_wider input a u8 . input b i16 . output i32 . do return add a. b. . . end .
```

```lowent-거부: without a widening that keeps the value it is refused · E-TYPE-SIGN
module ex_sign .

proc p input a i32 . input b u32 . output i32 . effects none .
do
  return add a. b. . .     rem `u32` does not fit in `i32` — the widths are the same
end .
```

> [!주의] Overflow does not wrap silently
> In many languages adding 1 to 255 of `u8` gives 0 (wrapping). Lowent **traps**.
> If wrapping is needed, use **an operation written as such**, like `wrap_add` — whether wrapping was wanted or
> was a mistake shall remain in the source.

> [!산문]
> *"It stops when it overflows"* may look inconvenient. But if computation continues with an overflowed value, that
> program's answer is already wrong; it merely becomes **unknown where it went wrong**. Stopping is
> the failure that can be fixed.

> [!참고]
> Why traps do not hurt performance is in ⟦§6.4.6⟧. If a contract proves the range,
> that check disappears at translation time — that is, **writing honestly makes it faster**.

## 6.3.5 Bitwise operations

(1) Bit operations treat a value as **a sequence of bits**. Operands and results are integers, and signed
      integers can be handled too.

(1′) If the two operands of `bit_and` · `bit_or` · `bit_xor` are of the same type, the result is of that type too; if one side is a literal, it is of the other side's type.
      The result of `bit_not` and of shifts · rotations (`shl` · `shr` · `rotl` · `rotr`) is of the type of the (left) operand. Putting `usize` · `idiff` and
      a fixed-width integer in one operation is refused at translation (`E-TYPE-NOMINAL`, ⟦§6.2⟧ (3-2)) — the shift count is not covered by this rule.

(1″) If the two operands of `bit_and` · `bit_or` · `bit_xor` are integers that differ only in width or sign, they follow the same rule as arithmetic (⟦§6.3.4⟧ (1)–(2)):
      if they are comparable by widening, the result is of the wider type; if not, translation is refused (`E-TYPE-SIGN`).
      The result of `count_ones` · `leading_zeros` · `trailing_zeros` is of the type of `a`.

(1a) The right shift `shr` means different things depending on the sign. For unsigned integers 0 comes into the vacated
      places (logical shift); for signed integers **the sign bit comes in** (arithmetic shift). So
      a negative number stays negative when shifted right.

(2) The bitwise logical operations are `bit_and` `bit_or` `bit_xor` `bit_not`. The first three take two values
      and `bit_not` takes one.

(3) The shifts are `shl` (left) and `shr` (right). The rotations are `rotl` and `rotr`, in which the bits pushed out
      come back in on the opposite side.

(3a) The count of a rotation may be of any integer type, and it rotates by **the remainder after dividing by the width** — rotating a type of width 8 by 9 is the same
      as rotating it by 1. Rotations do not stop.

(4) The shift count shall be **less than** the width of the type. Otherwise it traps.

(4a) The count of `shl` · `shr` · `wrap_shl` · `wrap_shr` is **an unsigned integer type** (any width). Putting a value of a signed integer type
      as the count is refused at translation (`E-SHIFT-SIGNED`) — a negative count has no single fixed meaning. To use a signed value as the count,
      convert it explicitly to an unsigned type. A literal takes the type of its place, so it is used as it is (negative literals: (9)).

(5) `wrap_shl` and `wrap_shr` **wrap** the shift count into the width before using it. On a type of width 8,
      9 places are read as 1 place.

```lowent-거부: the shift count is an unsigned integer type · E-SHIFT-SIGNED
module ex_shift_signed .

fn up input a u8 . input n i8 . output u8 . do return shl a. n. . . end .
```

(6) The counting operations are `count_ones` (the number of 1 bits), `leading_zeros` (the number of consecutive 0s from the front) and
      `trailing_zeros` (the number of consecutive 0s from the back).

(7) `byte_swap` reverses the order of the bytes. It is used when exchanging values with a place of different endianness.

```lowent 예제: bitwise logic and inversion · 결과: mask(12,10) = 8 · both(12,10) = 14 · diff(12,10) = 6 · flip(12) = 243
module ex_bitlogic .

rem 12 = 0000_1100 · 10 = 0000_1010
fn mask input a u8 . input b u8 . output u8 . do return bit_and a. b. . . end .
fn both input a u8 . input b u8 . output u8 . do return bit_or  a. b. . . end .
fn diff input a u8 . input b u8 . output u8 . do return bit_xor a. b. . . end .
fn flip input a u8 . output u8 . do return bit_not a. . . end .
```

> [!산문]
> `flip(12)` is 243 because it is `u8` — inverting `0000_1100` gives `1111_0011`, and
> that is 243. With a type of a different width the answer is different too. The answer of a bit operation is **always bound to the width
> of the type.**

```lowent 예제: shifts and rotations · 결과: up(3,2) = 12 · down(12,2) = 3 · spin(129,1) = 3 · back(3,1) = 129
module ex_bitshift .

fn up   input a u8 . input n u8 . output u8 . do return shl  a. n. . . end .
fn down input a u8 . input n u8 . output u8 . do return shr  a. n. . . end .
fn spin input a u8 . input n u8 . output u8 . do return rotl a. n. . . end .
fn back input a u8 . input n u8 . output u8 . do return rotr a. n. . . end .
```

> [!산문]
> `spin(129, 1)` being 3 shows the difference between rotating and shifting. 129 is `1000_0001`,
> and rotating it one place left brings the leading 1 round to the end, giving `0000_0011` = 3.
> Shifting the same value with `shl` **throws** that 1 **away**.

> [!주의] A shift count of the width or more traps — a place that differs from C
> Shifting a value of width 8 by 9 places stops the processor (`E-VM-SHIFT`). In C the same thing is
> **undefined behaviour**; the program keeps running and the machine decides the answer. Here it is a contract.
> If wrapping is wanted, **say it by name** — `wrap_shl` · `wrap_shr`. At width 8,
> 9 places are read as 1 place (`wrap_shr(12, 9) = 6`). Wrapping silently and writing that you want wrapping
> are different things (⟦§1.3⟧).

```lowent 예제: the sign changes the meaning of a shift · 결과: s_shr(-8, 1) = -4 · u_shr(4294967288, 1) = 2147483644 · s_and(-8, 12) = 8 · s_not(0) = -1
module ex_signed_shift .

rem Signed integer — the sign bit comes into the vacated places (arithmetic shift). The count is an unsigned type ((4a)).
fn s_shr input a i32 . input n u32 . output i32 . do return shr a. n. . . end .

rem Unsigned integer — 0 comes into the vacated places (logical shift).
fn u_shr input a u32 . input n u32 . output u32 . do return shr a. n. . . end .

rem Bitwise logical operations handle signed integers too.
fn s_and input a i32 . input b i32 . output i32 . do return bit_and a. b. . . end .
fn s_not input a i32 . output i32 . do return bit_not a. . . end .
```

> [!산문]
> `-8` and `4294967288` are **the same bit pattern** in 32 bits. But shifting them one place right,
> the answers diverge — the former gives `-4`, the latter `2147483644`. Being a bit operation does not mean the type
> is not looked at. **The sign decides the meaning.**

```lowent 예제: counting and byte reversal · 결과: ones(7) = 3 · lead(1) = 7 · tail(8) = 3 · endian(1) = 16777216
module ex_bitcount .

fn ones input a u8 . output u8 . do return count_ones a. . . end .
fn lead input a u8 . output u8 . do return leading_zeros a. . . end .
fn tail input a u8 . output u8 . do return trailing_zeros a. . . end .
fn endian input a u32 . output u32 . do return byte_swap a. . . end .
```

> [!산문]
> `lead(1)` being 7 is also because of the width — in `u8`, 1 is `0000_0001`, so there are seven leading
> zeros. `endian(1)` is 16777216 because reversing the bytes of `0x00000001` gives
> `0x01000000`.

(8) Bit operations take **only integers** — bits are a property of machine words, so for values without that property
      they have no meaning. This mismatch is caught **at the place of execution** (`E-BITOP-TYPE`).

> [!주의]
> Here is a place that cannot be caught at translation. It is written down because, if it were not,
> readers would **believe that translation protects them** — letting people believe that something is protected when it is not
> is the worst thing a norm can do (⟦§8.6⟧).

(9) If the shift count is **proven not to be less than** the width of its type, it is refused (`E-SHIFT-RANGE`).

## 6.3.6 Choosing **by name** what to do on overflow

(1) `add` · `sub` · `mul` stop on overflow (⟦§6.3.4⟧). If another disposition is needed, use **the op whose name states that
      disposition**. That is, a disposition is not a setting but **a word**.

> [!표] Arithmetic whose disposition is chosen by name
> #table(columns: (auto, auto, 1fr), stroke: 0.5pt + rgb("#bbb"), inset: 6pt,
> [*Disposition*], [*op*], [*What happens on overflow*],
> [stop (default)], [`add` · `sub` · `mul`], [it traps — it does not go on with a wrong value],
> [wrap], [`wrap_add` · `wrap_sub` · `wrap_mul`], [it wraps round within the width],
> [saturate], [`sat_add` · `sat_sub` · `sat_mul`], [it stops at the end value of the width],
> [as a value], [`chk_add` · `chk_sub` · `chk_mul`], [it returns the overflow **as a value** (⟦option⟧)],
> [no check], [`div_nz`], [division whose divisor is **guaranteed by its type not to be 0**. No check remains at that place],
> )

(2) Division by 0 is the same. At a place where the divisor is **guaranteed by its type not to be 0**, use `div_nz`,
      and no check remains at that place.

(2a) The way to obtain that guarantee is `nonzero_of`. `nonzero_of <value>` reports **as a value** that the value is not 0
      (⟦option⟧) — none if it is 0. Once obtained, it is not asked again at each place
      that divides.

> [!산문]
> Instead of checking «not divided by 0» every time, **proving it once and carrying it as a value** is
> the same as what this language does with contracts. Removing a check is not ignoring the danger;
> it is confirming **in one place** that there is no danger and carrying that fact in the type.

(2b) To receive an overflowing carry · borrow **as a value instead of discarding it**, use the standard library `intops` (⟦§9⟧) — `add_carry_<width>` returns
      the sum and the carry (0 or 1), `sub_borrow_<width>` the difference and the borrow, and `mul_wide_<width>` the lower half `lo` and the upper half `hi` of the product,
      as a struct (for signed widths `mul_wide_s_<width>` — `lo` is unsigned and `hi` signed). The widths are unsigned `u8` · `u16` ·
      `u32` · `u64` · `usize` and signed `i8` · `i16` · `i32` · `i64` · `idiff`. All are written in Lowent (⟦§9.3⟧ (5)).

(3) Reducing a width also chooses its disposition by name.

> [!표] The ops that change width
> #table(columns: (auto, auto, 1fr), stroke: 0.5pt + rgb("#bbb"), inset: 6pt,
> [*op*], [*Shape*], [*What it does*],
> [`widen`], [`widen <type> <value>`], [widens. The value is preserved, so no disposition is needed],
> [`narrow`], [`narrow <type> <value>`], [reduces. If it does not fit, it **stops**],
> [`narrow_wrap`], [], [wraps if it does not fit],
> [`narrow_sat`], [], [stands at the end value if it does not fit],
> [`narrow_try`], [], [reports **as a value** if it does not fit (⟦option⟧)],
> )

> [!산문]
> Why is the disposition a name and not a build setting? As a setting, **the same source gives a different answer
> for each build**, and which answer it is cannot be known by reading the source. As a name,
> whether wrapping **was wanted or was a mistake** remains at that place — the same principle this language
> repeats in contracts and effects.

## 6.3.7 Ops that handle lanes

(1) A `vec` (⟦§6.2.11⟧) does arithmetic per lane separately, but **crossing lanes** is done by separate
      ops.

> [!표] Ops that handle lanes
> #table(columns: (auto, auto, 1fr), stroke: 0.5pt + rgb("#bbb"), inset: 6pt,
> [*op*], [*Shape*], [*What it does*],
> [`splat`], [`splat <value>`], [fills every lane with one value],
> [`load`], [`load <piece> <position>`], [reads lanes from memory],
> [`store`], [`store <piece> <position> <value>`], [writes lanes to memory],
> [`load_masked` · `store_masked`], [＋ a mask and a default], [reads or writes only the lanes whose mask is on],
> [`lane_reverse`], [`lane_reverse <value>`], [reverses the order of the lanes],
> [`lane_rotate`], [`lane_rotate <value> <places>`], [rotates the lanes by that much],
> [`reduce_add` · `reduce_mul`], [`reduce_add <value>`], [adds · multiplies all lanes],
> [`reduce_min` · `reduce_max`], [`reduce_min <value>`], [the smallest · largest of the lanes],
> [`lane_avg`], [`lane_avg <value> <value>`], [the rounded average per lane],
> [`native_lanes`], [`native_lanes <type>`], [asks **at translation time** how many lanes this machine handles at once],
> )

> [!주의]
> The value of `lane_avg` **is fixed as (a+b+1)>>1.** If the lane is narrow, `a+b` overflows that lane, so
> the processor shall add in a wider place and then narrow --- the two back ends of this edition do that.
> A processor that later wants to lower this to one machine instruction shall check that **the instruction gives exactly this value**.
> x86's PAVGB/PAVGW and ARM's VRHADD/URHADD give this value, but for RISC-V's vaaddu the
> rounding mode depends on the vxrm register, so that cannot be said. If the same name gives different values on different
> machines, that drops something this language has decided to keep.

(1a) `splat` takes the lane count **from the declared type**. So the only place it can be used is the value position of a binding that writes
      a vector type: `var lim vec 4 u32 splat 5 . .`. Writing it directly inside an expression does not
      conform (`E-VEC-SPLAT`) — nothing in that place says the lane count.

(1b) `lane_avg` belongs to **integer lanes** (the value above is an integer computation). Using it on floating-point lanes is refused at translation (`E-TYPE-KIND`, 2026-11-02).

(2) An op that crosses lanes gathers as many as the lane count **into one**. The type of the result is the type of the lanes. Signed lanes are added and
      multiplied by sign, and if the value of `reduce_add` · `reduce_mul` does not fit that type, it stops (⟦§6.3.4⟧ (3)).

> [!참고]
> Earlier, the front of this table listed `sum` and `sum_fast` as «adds all lanes».
> No op **did** that under those names --- what adds lanes is `reduce_add`, and
> the name `sum` was being used in the processor for **the floating-point sum of a piece** (⟦§6.3.7.1⟧).
> One name meant two things, so the names were separated (2026-09-17).

## 6.3.7.1 Adding up a piece

(1) There are two builtin ops that add up all the **floating-point numbers** held in a piece (⟦slice⟧), and the name says **how
      it adds**. The standard library `math` wraps them as ops of the same names (`math.sum_neumaier` · `math.sum_seq`).

(1a) `sum_neumaier <piece>` adds while **collecting the lost digits separately and giving them back.** So its error is not bound to the number
      of terms.

(1b) `sum_seq <piece>` adds once from front to back. It is fast, and its error **is proportional to** the number of terms.

```lowent-거부: returning a floating-point sum in an integer place · E-TYPE-RETURN
module ex_slice_sum .

use math .

fn total input b slice u8 . output u64 .
do
  var xs slice f64 view_array f64 b. . .
  return math.sum_neumaier xs. . .      rem the result is a floating-point number, but the head says `u64`
end .
```

(2) The result of both ops is **a floating-point number**. Returning it as it is to a place written as an integer is refused
      (`E-TYPE-RETURN`).

> [!산문]
> Why does the name say the method? Adding `[1e16, 1.0, -1e16]` from front to back with `sum_seq` gives 0, and
> adding it with `sum_neumaier` gives 1 --- neither is a wrong answer; they are **different promises**. If the name
> were only `sum`, the caller could not know which promise it got, and switching to the faster one
> would become **silently changing the answer**. Cost and error are what the name carries in this language.

> [!주의]
> `native_lanes` **is not a value that changes the answer.** The lane count is part of the type (⟦§6.2.11⟧),
> so the answer is the same whether the number obtained by this question or a number written by hand is used. But choosing a number that fits
> the machine computes **more at once**.

## 6.3.8 Asking the size — `size_of`

(1) `size_of <type>` gives the number of bytes one value of that type occupies, **at translation time**. The type of that number is `usize` — so are the
      alignment `align_of` gives and the number of lanes `native_lanes` gives.

(2) It cannot be used on a type whose size cannot be known.

> [!산문]
> Without this question, code that takes a type as a parameter would have to **assume the widest width**.
> Then a container holding one-byte things uses eight times as much. Being able to ask the size lets code that takes parameters
> produce its costs honestly.

## 6.3.9 Transcendental functions

(1) The builtin ops `sin` · `cos` · `exp` · `log` · `pow` · `sqrt` · `floor` · `ceil` · `round` · `fmod` are **floating-point only**.
      The standard library `math` wraps them as ops of the same names (`math.sqrt x.`). Application programs call those ops.

(1a) `abs` takes floating-point numbers and **signed integers**. The type of the result is the type of the operand. At the smallest value of a
      signed integer it stops — its absolute value is not in that width (⟦§6.3.4⟧ (3)).

(2) These require no capability and can be used **only on machines with an operating system**.

> [!모호] M-0007 — the last bit of the results of transcendental functions
> This document does not fix what value the results of `sin` · `cos` · `exp` · `log` · `pow` are, down to the last bit. But which kind of processor discretion it is (⟦§4.3⟧) is not marked.
> Reading A: leave it a free choice — write the range of choice as something like «one of the two floating-point numbers nearest the true value». Reading B: make it implementation-defined — the processor documents the math library it uses. Reading C: fix it as the correctly rounded value.
> The processor currently gives the value the machine's math library gives.

> [!주의]
> What value the results of these functions are, down to the last bit, is not fixed by this document. The machine and
> the math library under it decide. If a computation depends on the last bit, use them **knowing that**.

## 6.3.10 A truth value is not a number

(1) `and` · `or` · `not` take **only truth values**. Passing an integer is refused
      (`E-TYPE-LOGICAL`).

(2) Places where a condition comes (⟦§6.5⟧) likewise take only truth values (`E-TYPE-COND`).

(3) That is, there is **no** promise that 0 is false and everything else true. What is being asked shall be written at that
      place — as in `gt a. 0 .`.

> [!산문]
> In languages that let an integer stand in a condition position, a reader **guesses from context** whether `if x` means
> *"x is not 0"*, *"x exists"* or *"x is true"*. A place that needs guessing
> is also a place where the guess goes wrong. Making people write what is asked removes that place.

```lowent-거부: an integer is not a condition · E-TYPE-COND
module ex_cond .

fn f input a u8 . output u8 .
do
  if a. do return 1 . end .     rem write what is being asked — `gt a. 0 .`
  return 0 .
end .
```

## 6.3.11 Widths and lanes are part of the type

(1) Two `vec`s with different lane counts are different types and do not mix (`E-TYPE-LANES`).

(2) Only a mask can come in a place that takes a mask (`E-TYPE-MASK`).

(3) Even with the same field layout, **different names are different types** (`E-TYPE-WRAP`) — the same
      layout does not make the same type (⟦§8.9⟧).

(4) Putting a value outside a narrowed range (⟦§6.2.14⟧) in that place is refused (`E-TYPE-RANGE`).
      So is writing a range that **no value can satisfy** — that is not a condition but
      a lie.

```lowent-거부: two with different lane counts are different types · E-TYPE-LANES
module ex_lanes .

fn f input a vec 4 u8 . input b vec 8 u8 . output vec 4 u8 .
do
  return add a. b. . .
end .
```

## 6.3.12 The rules inside an `expr` island

(1) Each piece inside an `expr` island (⟦§6.3.2⟧) is one form — `a.` if it is a name, a form closed by its own dot if it is a call
      (`expr twice a. . + 1 .`). Since a form has its own end, calls need not be wrapped in parentheses. The island is closed by its own dot.

(2) Inside an island there are **only infix operators**. There is no one-place operator attached in front
      (`E-EXPR-UNARY`) — to flip a sign, call the op written as such (`neg a. .`).

```lowent-거부: there is no operator attached in front inside an island · E-EXPR-UNARY
module m .
fn flip input a i32 . output i32 . do
  return expr - a. . .
end .
```

> [!산문]
> The island exists to let arithmetic be read **in the familiar order**, not to bring in a second
> grammar. So what can be done inside an island is narrow, and because it is narrow, the meaning inside
> does not waver.
