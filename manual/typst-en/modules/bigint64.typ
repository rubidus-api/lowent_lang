#import "../../typst-ko/lib.typ": *

= `bigint64` --- big-number modular arithmetic (64-bit limbs) <mod-bigint64>

#modhead(file: "lib/bigint64.low", layer: [L0 --- pure computation (the caller's backing)], caps: [none])

The op names, inputs and meanings are the same as #modref("bigint32")[`bigint32`]'s; numbers are held in *64-bit limbs*. How to use it and the op table are on that
page --- change `use bigint32 .` to `use bigint64 .`.

*One product is 128 bits.* The lower half is `wrap_mul`, the upper half the builtin `mul_hi`, and a carry out of an addition is read as «the sum came out smaller than what
was added». There are a quarter as many limb products as in `bigint32`, but each costs more. Measured (RSA-2048 exponentiation · native `-O2` · 2026-10-22): 1.6 to 1.9
times faster than `bigint32` on x86-64, and 1.35 times faster in a 32-bit build (x86 `-m32`) as well --- values are `u64` anyway, so 32-bit limbs also do 64-bit
arithmetic there. The same computation hand-written in C (product and addition in one 128-bit integer) is about 2 times faster than this module; of that, «multiply and
add in one go» was about 10 %.

#dtable(
  columns: 3,
  id: "mod-bigint64-extra",
  caption: [The op only `bigint64` has],
  [*op*], [*What it does*], [*Notes*],
  [`mul_hi`], [The upper 64 bits of the product of two `u64`], [the lower half is `wrap_mul` · wraps the builtin `mul_hi` (applications call this)],
)
