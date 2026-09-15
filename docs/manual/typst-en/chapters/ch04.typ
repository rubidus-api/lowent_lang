#import "../../typst-ko/lib.typ": *

= Numbers --- fixed-width integers and floating point

#chapter-toc()

#prereq(
  ([#chref("first-program"), A first program], [the VM and native code must agree]),
  ([#chref("surface"), The surface], [a literal that does not fit its place's type is rejected]),
)

#deepqa[
  What happened to `let x u8 be 300 .` in #chref("surface")? How does C handle the same thing?
][
  It was rejected with `E-TYPE-WIDTH`, because 300 is outside the `u8` range 0 … 255. C silently truncates it to 44, so the value in
  the source and the actual value differ. This chapter looks at how the same principle applies when a *computed result*, not a
  literal, exceeds its width.
]

#why[
  Integer defects are among the oldest and most common in systems programs. Values overflow into small numbers, negative numbers turn
  into huge positive ones, and division by zero becomes undefined behaviour. Most of it happens *silently* and surfaces much later in
  an unrelated place. Numbers open Part II because all computation happens on them, and because they show in the simplest form how
  Lowent removes "silently" --- by making you write the chosen treatment as a name.
]

#organizer[
  You will learn that integer types are named by sign and width and have the same size everywhere. You will pick up the rule that
  widening is automatic and narrowing must be written, that overflow and division by zero stop execution, and how to choose *by
  name* to wrap, saturate or get the failure back as a value instead. You will also see that booleans are not numbers, and the
  behaviour of bitwise operations and floating point.
]

#chapter-questions()

== Integer types name their width

An integer type's name is its sign and bit width. `u` means unsigned, `i` means signed.

#dtable(
  columns: 3,
  id: "numbers-int",
  caption: [Integer types and their ranges],
  [*Type*], [*Width*], [*Range*],
  [`u8` · `u16` · `u32` · `u64`], [8 · 16 · 32 · 64], [0 … 2#super[N]−1],
  [`i8` · `i16` · `i32` · `i64`], [8 · 16 · 32 · 64], [−2#super[N−1] … 2#super[N−1]−1],
  [`usize` · `isize`], [address width], [set by the execution environment],
)

C's `int` can have different sizes on different machines, but Lowent's `u32` is 32 bits everywhere. Negative numbers use two's
complement. `usize` is a *different type* from `u64` even on a machine where they have the same width, and the two do not convert
automatically --- so that places mixing address counts with plain numbers show in the source.

== Widening is automatic, narrowing is written

#idx("widening")
A conversion that loses no value is called *widening*. Passing a `u8` where a `u32` is expected keeps the value, so the compiler does
it for you. Even with mixed signs, arithmetic is allowed if a value-preserving widening exists --- every `u8` fits in an `i16`.

#demo("examples/ch04/widths.low")

`mix` adds a `u8` and an `i16`, and the result has the wider type, `i16`. To make the widening visible, write `widen u64 x`. An `i32`
and a `u32` of the same width cannot be mixed, because neither fits entirely in the other.

#demo("examples/ch04/sign_bad.low")

#idx("narrowing")
A conversion that can lose a value is *narrowing*, and it must be written. `shrink` in the same file narrows a `u64` to a `u8` with
`narrow u8 x`. 200 fits and comes out unchanged, but 300 does not, so execution stops (`E-VM-CAST`). If you do not want it to stop,
choose the treatment you want by name. `narrow_sat` stops at the end value 255, and `narrow_wrap` wraps like C and gives 44.
`narrow_try` gives `none` when the value does not fit.

#qa[
  If `narrow_wrap` does what C does, why isn't wrapping the default?
][
  To leave in the source whether wrapping was *intended or a mistake*. If wrapping were the default, someone reading a place where
  `narrow u8 x` gave 44 could not tell whether that was the wanted value. With stopping as the default, an author who wanted wrapping
  *wrote* `narrow_wrap`. The name written is the record of the author's intent.
]

== Overflow stops

#idx("overflow")
Arithmetic happens *at the declared width*. If the result does not fit the width, execution stops (a trap).

#demo("examples/ch04/overflow.low")

`bump 254` fits as 255, but `bump 255` would have to be 256, so the VM stops with `E-VM-OVERFLOW`. Native code stops at the same place
with a non-zero exit code (the verification script checks that both stop). As with widening, you change the treatment by choosing a
name.

#dtable(
  columns: 3,
  id: "numbers-overflow",
  caption: [Choosing the treatment of overflow by name],
  [*Treatment*], [*Ops*], [*On overflow*],
  [Stop (default)], [`add` · `sub` · `mul`], [Execution stops],
  [Wrap], [`wrap_add` · `wrap_sub` · `wrap_mul`], [Wraps around within the width],
  [Saturate], [`sat_add` · `sat_sub` · `sat_mul`], [Stops at the end value of the width],
  [As a value], [`chk_add` · `chk_sub` · `chk_mul`], [Reports it as an `option`],
)

The "as a value" family turns overflow into an event the program can handle.

#demo("examples/ch04/chk.low")

`chk_add a 1` gives an `option u8`. On overflow it is `none`, and `guard is_some r . else …` handles that case first. You will meet this
shape again in #chref("option-result").

#misconception[Checking for overflow makes code slow][
  It would if every addition kept its check. But where a contract or a range type proves the range of a value, the check is removed at
  translation time (#chref("contracts")). Places with an obvious range, such as loop counters, are cleaned up by the compiler's interval
  analysis. The checks that remain are at places that *really can overflow*, and there the check is not a cost but the value that
  stops a defect.
]

== Division and remainder

Dividing an integer by zero stops. Signed division truncates towards zero, and the sign of the remainder `mod` follows the *divisor*.

#demo("examples/ch04/divide.low")

`quot -7 2` is −3 and `modulo -7 2` is 1. Because the remainder follows the divisor's sign, `mod h n` for a positive `n` is always at
least 0 and less than `n`. That property is why the bounds check in `index slots (mod h n)` disappears when a hash table picks a slot,
and it is proven in Coq (#chref("proofs-numbers")).

The only case where signed division overflows is `MIN / −1`, and that stops too. In C it is undefined behaviour.

There is also a way to check once that a divisor is not zero and carry that fact around. `nonzero_of b` gives an `option nonzero u32`,
and the value inside can be passed to `div_nz`. No zero check remains at the `div_nz` site. The check was not removed but *moved to one
place, with its result carried in the type*.

== Booleans are not numbers

`bool` has two values, `true` and `false`. An integer in a condition is rejected.

#demo("examples/ch04/cond_bad.low")

C's `if (n)` reads as "n is not zero", but the reader has to guess from context whether it means "n exists" or "n is true". Lowent makes
you write what you are asking --- `if gt n 0 .`. Conversely, a `bool` cannot be added like a number. `and`, `or` and `not` take only
booleans, and `and` and `or` skip the right-hand side when the left-hand side already decides the answer.

== Bitwise operations

Bitwise operations treat a value as a sequence of bits. They use words instead of symbols.

#demo("examples/ch04/bits.low")

- `bit_and` · `bit_or` · `bit_xor` · `bit_not` --- bitwise logic. `flip 12` is 243 because the type is `u8`. The answer of a bitwise
  operation is always tied to the width of its type.
- `shl` and `shr` shift; `rotl` and `rotr` rotate. `spin 129 1` is 3 because the top 1 came back in at the bottom; with `shl` that 1
  would have been discarded.
- `shr` means different things depending on sign. −8 and 4294967288 are the same bit pattern in 32 bits, but the signed one shifts in
  the sign bit and becomes −4, while the unsigned one shifts in 0 and becomes 2147483644.
- `count_ones` · `leading_zeros` · `trailing_zeros` count bits, and `byte_swap` reverses byte order.

Shifting by at least the width of the type stops. In C that is undefined behaviour. If you want the shift amount wrapped into the width,
use `wrap_shl` and `wrap_shr`.

#qa[
  Why can't bitwise operations be written infix inside an `expr` island?
][
  In C, `a & b == c` groups as `a & (b == c)`. It is a trap even for long-time users, and languages disagree on bitwise precedence.
  Putting them in the island would add something to look up instead of making them easier to read. Written prefix, the parentheses
  state the answer and nothing can be misread.
]

== Floating point

`f32` and `f64` are the IEEE 754 binary 32- and 64-bit formats. Operations happen at the width of their operands, and integers and
floating-point values do not convert to each other automatically.

#demo("examples/ch04/floats.low")

The VM shows floating-point results briefly (`0.333333`). Dividing a floating-point value by zero does not stop; it becomes infinity as
IEEE 754 prescribes. Transcendental functions such as `sqrt`, `sin` and `exp` are floating-point only and available only on machines
with an operating system. The exact last bit of their results is up to the machine and its maths library.

#misconception[Floating-point values can be compared with `eq` too][
  0.1 plus 0.2 does not have the same bits as 0.3. Floating-point equality usually has to be asked as "close enough", and the standard
  library's `math` module provides that question (`close`) (#chref("lib-map")). Rounding, NaN and −0 are also properties the proofs of
  this language do not cover (#chref("proofs-limits")).
]

`f32` and the size types follow the same rules.

#demo("examples/ch04/sizes.low")

- `usize` and `isize` are unsigned and signed integers with the machine's address width. Use them to exchange lengths and indexes with C's
  `size_t` and `ptrdiff_t` (#chref("ffi")). Their width may differ between machines, so use `u64` and `i64` for fixed-width arithmetic. Moving to a
  type of the other signedness goes through a named operation such as `cast isize n`.
- `f32` takes half the memory and has about seven significant digits. Use it where quantity matters more than precision, as in large arrays or
  graphics.
- `same_third` computes the same 1/3 as `f32` and as `f64`, widens, and compares. The answer is false (0). Even though the VM shows both briefly
  as `0.333333`, they are different numbers.

== Common mistakes

Many number mistakes pass compilation and only show up *while running*. Where other languages would quietly produce a wrong value,
Lowent stops --- and the place it stops is the place to fix.

#antipattern[Finding the middle by adding first, then halving][
  #demo("examples/ch04/mistake_midpoint.low")

  `200 + 100` is 300, and `u8` only goes up to 255, so it overflows before the division. This is the very bug that lurked for decades in
  `(lo + hi) / 2` inside binary searches. C would quietly give 22, half of the wrapped value 44. The fix is to compute *at a wider width*.

  #demo("examples/ch04/midpoint_fixed.low")

  Widen to `u16`, add, divide, then `narrow u8` back down. The middle always fits in `u8`, so the narrowing never stops --- and if the
  computation is ever changed by mistake, it stops right there and tells you. (When `lo <= hi` is guaranteed,
  `add lo (div (sub hi lo) 2)` does not overflow either.)
]

#antipattern[Subtracting a larger number from an unsigned one][
  #demo("examples/ch04/mistake_usub.low")

  `u64` has nothing below 0. `3 − 5` is not −2 but an *overflow*, so it stops (C would give 18446744073709551614). If you want the size of
  the difference, subtract the smaller from the larger --- `if ge a b . do return sub a b . end . return sub b a .` --- and if a negative
  result is meaningful, compute in `i64` from the start.
]

#antipattern[Giving a loop counter too narrow a type][
  #demo("examples/ch04/mistake_narrowloop.low")

  A `u8` can never reach 256, so `lt i 256` is always true. In C, `i` would wrap from 255 back to 0 and the loop would *never end*. In
  Lowent the 256th `add i 1` overflows and stops, so at least it does not hang silently. Give a loop counter a type wider than the largest
  value it counts to (`u64`).
]

#antipattern[Doing float arithmetic with an integer literal][
  #demo("examples/ch04/mistake_floatint.low")

  Integers and floats never convert into each other automatically. To divide an `f64`, write the literal as a float too: `2.0`. This
  edition's tool does not catch the mix at compile time and stops *while running* with `E-VM-TYPE` --- it ought to be rejected at compile
  time, and the development repository records it as a defect. When you see this stop, look for the mixed literal.
]

#antipattern[Testing with the largest `u64` passed as a `--run` argument][
  #demo("examples/ch04/mistake_runmax.low")

  `echo 18446744073709551615` should return what it received, yet 9223372036854775807 (the largest `i64`) comes out. In this edition `--run`
  reads command-line arguments as signed 64-bit numbers and cuts them down. The VM and the native build do the same, so comparing the two back
  ends does not reveal it either (recorded as a defect in the development repository). A literal stored with `let` inside a `test` block is intact,
  so write boundary-value tests there, as `largest_u64` does. Printed results are also shown as `i64` in this edition, so for `u64` values above
  the `i64` maximum, trust the comparisons of `expect` rather than the printed number.
]

#misconception[It is written `f32`, so the value is already 32 bits --- a hole in this edition][
  #demo("examples/ch04/mistake_f32literal.low")

  `stored` widens an `f32` holding the literal 0.1 and compares it with the `f64` 0.1, and the answer is true. Had it been rounded to 32 bits it
  would be false. This edition's tool does not round a literal (or an argument passed with `--run`) when storing it in an `f32` place; the value
  becomes 32 bits only after one operation (`computed` is false). The VM and native code agree on this. It is recorded as a defect in the
  development repository. Code that relies on the exact bits of an `f32` (hashing, serialisation, comparison tests) should not trust literal
  values as they are in this edition.
]

#misconception[`div 7 2` is 3.5][
  Integer division keeps only the quotient and drops the fraction.

  #demo("examples/ch04/intdiv.low")

  `avg2 3 4` is 3. If you need the fraction, take the inputs as floats and divide by `2.0`. If you need rounding, *write* it --- add
  `div d 2` before dividing, for example --- because the language never rounds for you.
]

The floating-point remainder gets its own name, `fmod`, separate from the integer `mod`.

#demo("examples/ch04/floatmod.low")

`fmod 7.5 2.0` is 1.5 and `mod 7 2` is 1. Not loading two kinds onto one name is the same reason as for `div` --- what is computed shows in the
name.

== This chapter's syntax at a glance

#dtable(
  columns: 3,
  id: "numbers-glance",
  caption: [Number syntax --- shape · meaning · why it looks this way],
  [*Shape*], [*Meaning*], [*Why*],
  [`u8` … `u64` · `i8` … `i64` · `usize` · `isize`], [integers with the width in the name], [sizes never change from machine to machine],
  [`f32` · `f64`], [IEEE 754 floating point], [never mixed with integers --- the literal is `2.0` too],
  [`widen u64 x`], [widening (loses nothing)], [happens automatically, but can be written to make it visible],
  [`narrow u8 x`], [narrowing --- stops if it does not fit], [so values never change silently],
  [`narrow_sat` · `narrow_wrap` · `narrow_try`], [clamp to the end value · wrap · as an `option`], [pick the outcome by name],
  [`add` · `sub` · `mul`], [arithmetic that stops on overflow], [overflow is a bug by default],
  [`wrap_add` · `sat_add` · `chk_add`], [wrap · saturate · `option`], [the intended outcome stays in the source],
  [`div` · `mod`], [quotient (towards zero) · remainder (sign of the divisor)], [division by zero and `MIN / −1` stop],
  [`nonzero_of b` · `div_nz`], [check "not zero" once and carry it in the type], [moves the check to one place],
  [`bit_and` · `bit_or` · `bit_xor` · `bit_not`], [bitwise logic], [no symbol-precedence traps],
  [`shl` · `shr` · `rotl` · `rotr`], [shift · rotate], [shifting by the width or more stops],
  [`count_ones` · `leading_zeros` · `trailing_zeros` · `byte_swap`], [count bits · reverse bytes], [common jobs get one name],
  [`true` · `false` · `and` · `or` · `not`], [booleans and their logic], [numbers are never used as conditions],
  [`usize` · `isize` · `f32`], [address-width integers · 32-bit float], [lengths exchanged with C · when quantity matters more than precision],
)

#recap[
  Integer types carry sign and width in their names and have the same size everywhere. Widening is automatic and narrowing is written
  with `narrow`. Overflow, division by zero, out-of-range narrowing and shifting by the width or more all stop, and other treatments
  (wrap, saturate, as a value) are chosen by names like `wrap_`, `sat_`, `chk_` and `narrow_try`. The sign of `mod` follows the divisor.
  Booleans are not numbers, bitwise operations are written as words, and width and sign decide their meaning.
]
