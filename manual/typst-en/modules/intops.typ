#import "../../typst-ko/lib.typ": *

= `intops` --- exact comparison and wide arithmetic <mod-intops>

#modhead(file: "lib/intops.low", layer: [L0 --- pure computation], caps: [none])

Three things an integer computation may want *as values* instead of losing them --- an exact comparison of two integers of different
sign or width, the carry and borrow of an addition and a subtraction, and the upper half of a product. The basic type system
(basic_type_system 02 §2.6 · §2.7) asks every implementation for these operations, and all of them are written in Lowent --- only the
innermost upper half of a `u64` product calls the builtin `mul_hi`.

*Exact comparison* --- `cmp_eq` · `cmp_lt` · `cmp_le` compare the *mathematical values* of two integers. An infix `lt` is refused when the
signs are mixed (`E-TYPE-SIGN`), so a program that wants the values compared says so by name. The type arguments come first.

```text
intops.cmp_lt i64. u64. a. b. .      ← true for a = −1, b = 0
intops.cmp_eq i64. u64. a. b. .      ← false for a = −1, b = 2^64 − 1 (same bits, different values)
```

Any pair of integer types is accepted --- `usize` · `idiff` and 128 bits included. The method looks at the signs first: two negative
values are compared as `i128`, two non-negative ones as `u128`. Every integer fits one of the two.

*Carry · borrow · wide product* --- one op per width (a struct has no type parameter).

#table(columns: (auto, auto, 1fr), stroke: 0.5pt + rgb("#bbb"), inset: 6pt,
  [*op*], [*result*], [*meaning*],
  [`add_carry_<w>`], [`{sum, carry}`], [$(a + b + c) mod 2^N$ and the carry. The incoming `c` is 0 or 1 (a contract)],
  [`sub_borrow_<w>`], [`{diff, borrow}`], [$(a - b - c) mod 2^N$ and the borrow],
  [`mul_wide_<w>`], [`{lo, hi}`], [the low $N$ bits and the high $N$ bits of the product],
  [`mul_wide_s_<w>`], [`{lo, hi}`], [a signed product --- `lo` is the unsigned low half, `hi` the signed high half],
)

The widths are the unsigned `u8` · `u16` · `u32` · `u64` · `usize` and the signed `i8` · `i16` · `i32` · `i64` · `idiff`. The product of a
`usize` or an `idiff` is formed in `u128` · `i128` and split at the target's address width (`machine.addr_bits`) --- two 16-bit halves on a
16-bit target.

*How it is known to be right* --- the golden's `intops` section checks 26 hand-computed answers (e.g. `hi` of $(-2^63)^2$ is $2^62$) on the
VM and natively, and the differential sweep compares both back ends for every op of `impl/tests/vm_intops.low`.
