#import "../../typst-ko/lib.typ": *

= `bigint` --- big-number modular arithmetic (the machine picks the width) <mod-bigint>

#modhead(file: "lib/bigint.low", layer: [L0 --- pure computation (the caller's backing)], caps: [none])

The op names, inputs and meanings are the same as #modref("bigint32")[`bigint32`]'s. One thing differs: *the machine picks the limb width* --- when the
target's *multiplier width* (`machine.mul_bits`) is 16 it calls #modref("bigint16")[`bigint16`], when 32 `bigint32`, otherwise #modref("bigint64")[`bigint64`].
The standard library's #modref("rsa")[`rsa`] · #modref("p256")[`p256`] · #modref("p384")[`p384`] · #modref("ecdsa")[`ecdsa`] · #modref("ed25519")[`ed25519`] · #modref("verify")[`verify`] use it.

#dtable(
  columns: 3,
  id: "mod-bigint-mulbits",
  caption: [The multiplier width of each target --- the value of `machine.mul_bits`],
  [*Target*], [*Multiplier width*], [*Module it picks*],
  [`x86_64` · `arm64` · `riscv64` · `win64`], [64], [`bigint64`],
  [`cortex_m` · `mips_be` · `cheriot`], [32], [`bigint32`],
  [`word16`], [16], [`bigint16`],
)

*To change the width for the whole program*, `lowentc --mul-bits 16 …` (or `32` · `64`) --- the value of `machine.mul_bits` changes, and every library
that uses `bigint` runs at that width. *To use another width in one place only*, pass a width type to an op that takes one (`rsa.verify_pss_by` …;
see #modref("bigwidth")[`bigwidth`]).

#aside[The multiplier width is a speed default][
  Every width gives *the same answer* on every machine --- values sit in `u64` slots anyway. Nor does this rule always pick the fastest width: an x86
  32-bit build (`-m32`) has a 32-bit multiplier, yet `bigint64` was 1.35 times faster than `bigint32` there (RSA-2048 exponentiation, 2026-10-22).
  Cortex-M0 · M0+ are known to have no instruction for the upper half of a product, so 16 may suit them, but the one `cortex_m` row covers them
  together with M3 · M4 (not checked).
]

The width is fixed *at translation*, so choosing costs nothing --- the value goes into the emitted C as a literal and `cc` folds the comparison. The
slots a number needs do not depend on the width: ⌈`nb` / 2⌉ + 2 slots are enough (`bigint16` uses the most).

#dtable(
  columns: 3,
  id: "mod-bigint-newops",
  caption: [Ops added to all four on 2026-10-23 --- the limb arithmetic the curve modules used to do themselves],
  [*op*], [*What it does*], [*Requires · notes*],
  [`add_mod` · `sub_mod`], [`o ← (a ± b) mod m`], [`a`, `b` are below `m` · `o` may be the same slots as `a` or `b`],
  [`is_zero`], [Is the `nb`-byte number 0 (0 · 1)], [],
  [`equal`], [Are the two numbers equal (0 · 1)], [not named `eq` because that is a builtin],
  [`bit`], [Bit `i` of the number (0 is the lowest)], [to read an exponent or scalar from the top],
)

*What is checked* --- the new ops of the four modules (`bigint16` · `bigint32` · `bigint64` · `bigint`) against Python's integer arithmetic (17 · 32 ·
33 · 48 bytes), and curve signature verification and RSA verification giving the same answer at all three widths (`--mul-bits 16 · 32 · 64`). The
five files (`bigint16/32/64` · `bigwidth` · `bigint`) come from one template (`scripts/gen-bigint.py`) --- editing one file alone fails the comparison
with the template.
