#import "../../typst-ko/lib.typ": *

= `math` --- floating-point maths <mod-math>

#modhead(file: "lib/math.low", layer: [L0 --- pure computation (host only)], caps: [none])

Adds commonly used things on top of core ops like `sqrt`, `sin` and `exp` --- comparison (`close`), constants (`pi`, `e`), angle conversion, hypotenuse, logarithms with a
base, and linear interpolation. It has no capabilities but links to the C maths library, so it is *host only* --- targets without an operating system may have no floating
point at all (`cortex_m` is `no_float`).

#aside[Do not compare with `==` --- use `close`][
  ```lowent
  guard math.close math.hyp 3.0 4.0 . 5.0 0.000001 . else return 1 . .
  ```
  "Equal" is not well defined for floating point (#chref("numbers")). So this module provides `close(a, b, tol)`, and the library's own tests judge with it.
]

*Why checking is not bit-exact.* C maths libraries *may differ in the last digits between implementations*. Claiming exact bits would cause false failures on other
machines and libcs, with tests failing while the tool is fine. So this module is checked against *known answers* (`sin(0) = 0`) and *identities* (`sin²+cos² = 1`,
`exp(log x) = x`), judged with `close`. How close counts as equal (`tol`) is the caller's question, not something the library should decide.

#dtable(
  columns: 2,
  id: "mod-math-ops",
  caption: [Ops of `math`],
  [*op*], [*What it does*],
  [`close`], [`|a − b| ≤ tol` --- this module's way of comparing],
  [`pi` · `e`], [Constants],
  [`deg_to_rad` · `rad_to_deg`], [Angle conversion],
  [`hyp`], [`sqrt(x² + y²)`],
  [`log_base`], [Logarithm with a base (`log x / log b`)],
  [`lerp`], [Linear interpolation `a + (b − a)·t`],
)

Used directly as core ops --- `sqrt`, `abs`, `floor`, `ceil`, `round`, `sin`, `cos`, `exp`, `log`, `pow`.

*Not built* --- `atan2`, `asin`, `acos`, `tan`, `log2`, `log10`, `cbrt`, complex numbers, fixed point, an `f32`-only face. If needed they attach as one core op plus one line
here. `hyp` computes `sqrt(x²+y²)` as is --- unlike libm's `hypot` it does not rescale to avoid overflow, so it can answer differently for very large values.
